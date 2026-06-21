#include "request_handlers.hpp"
#include "model_manager.hpp"
#include "queue_manager.hpp"
#include "download_manager.hpp"
#include "settings_manager.hpp"
#include "memory_utils.hpp"
#include "auth_manager.hpp"
#include "url_utils.hpp"
#include "version.hpp"
#include "utils.hpp"
#include "prompt_template.hpp"
#include "sd_wrapper.hpp"

#ifdef SDCPP_ASSISTANT_ENABLED
#include "assistant_client.hpp"
#include "tool_executor.hpp"
#endif

#include <iostream>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cmath>
#include <optional>
#include <regex>
#include <vector>
#include <array>
#include <cstdint>
#include <chrono>
#include <unordered_set>
#ifndef _WIN32
#include <unistd.h>  // getpid() for atomic-rename temp files in WebDAV PUT
#else
#include <process.h>
#define getpid _getpid
#endif

#include "stb_image.h"
#include "stb_image_write.h"

namespace fs = std::filesystem;

namespace sdcpp {

// Serve a regular file with proper HTTP caching and chunked streaming.
//
// Why this exists: the previous code path read each file into a
// std::stringstream, copied the contents into a std::string, then handed
// that to res.set_content() — three allocations per request and a
// worker thread pinned for the entire transfer. Worse, no cache headers
// were sent at all, so every browser repaint of an output image did a
// full re-download. This helper:
//
//   - Sends Cache-Control: public, max-age=31536000, immutable. Outputs
//     are write-once (the filename embeds job_id) so aggressive caching
//     is safe.
//   - Sends ETag (size-mtime) + Last-Modified, and short-circuits with
//     304 Not Modified when the client's If-None-Match / If-Modified-Since
//     matches.
//   - Streams the file via set_content_provider() with a 64 KB buffer
//     instead of buffering the whole thing in RAM. httplib auto-handles
//     Range: requests on top of this (Accept-Ranges: bytes), which is
//     what mobile video players need to seek.
static void serve_file_cached(const httplib::Request& req,
                              httplib::Response& res,
                              const fs::path& full_path,
                              const std::string& mime) {
    std::error_code ec;
    auto file_size = fs::file_size(full_path, ec);
    if (ec) {
        res.status = 500;
        res.set_content("Cannot stat file", "text/plain");
        return;
    }
    auto file_mtime = fs::last_write_time(full_path, ec);
    if (ec) {
        res.status = 500;
        res.set_content("Cannot stat file", "text/plain");
        return;
    }
    // C++20 file_clock → system_clock for an RFC 1123 timestamp.
    auto sys_mtime = std::chrono::file_clock::to_sys(file_mtime);
    std::time_t mtime_t = std::chrono::system_clock::to_time_t(sys_mtime);

    std::string etag =
        "\"" + std::to_string(static_cast<uint64_t>(file_size))
        + "-" + std::to_string(static_cast<int64_t>(mtime_t)) + "\"";

    char lm_buf[64];
    std::strftime(lm_buf, sizeof(lm_buf),
                  "%a, %d %b %Y %H:%M:%S GMT", std::gmtime(&mtime_t));
    std::string last_modified(lm_buf);

    // 304 short-circuit. Strong-compare the ETag (no W/ prefix in use)
    // and exact-match Last-Modified. Sending the same caching headers on
    // the 304 lets the browser keep its existing cache entry alive.
    std::string inm = req.get_header_value("If-None-Match");
    std::string ims = req.get_header_value("If-Modified-Since");
    bool not_modified =
        (!inm.empty() && inm == etag) ||
        (!ims.empty() && ims == last_modified);
    if (not_modified) {
        res.status = 304;
        res.set_header("ETag", etag);
        res.set_header("Last-Modified", last_modified);
        res.set_header("Cache-Control", "public, max-age=31536000, immutable");
        return;
    }

    res.set_header("ETag", etag);
    res.set_header("Last-Modified", last_modified);
    res.set_header("Cache-Control", "public, max-age=31536000, immutable");
    res.set_header("Accept-Ranges", "bytes");

    const std::string path_str = full_path.string();
    res.set_content_provider(
        static_cast<size_t>(file_size), mime,
        [path_str](size_t offset, size_t length, httplib::DataSink& sink) -> bool {
            // Fixed-length provider: do NOT call sink.done() — that field
            // is only wired up by httplib for set_content_provider_without_length().
            // Calling it here throws std::bad_function_call. We just write
            // exactly `length` bytes from `offset` and return true; httplib
            // tracks completion via the declared content length.
            std::ifstream f(path_str, std::ios::binary);
            if (!f) return false;
            f.seekg(static_cast<std::streamoff>(offset));
            if (!f) return false;
            constexpr size_t CHUNK = 64 * 1024;
            std::array<char, CHUNK> buf{};
            size_t remaining = length;
            while (remaining > 0 && f) {
                size_t to_read = std::min(remaining, CHUNK);
                f.read(buf.data(), static_cast<std::streamsize>(to_read));
                auto n = f.gcount();
                if (n <= 0) break;
                if (!sink.write(buf.data(), static_cast<size_t>(n))) {
                    // Client disconnected; bail out cleanly.
                    return false;
                }
                remaining -= static_cast<size_t>(n);
            }
            return true;
        });
}

// Decorate a serialized QueueItem with absolute `output_urls[]` derived
// from the current request. Caller-provided base_url is computed once
// per request via compute_base_url() so listing endpoints don't pay the
// header-parse cost N times.
static void inject_output_urls(nlohmann::json& job_json, const std::string& base_url) {
    if (!job_json.contains("outputs") || !job_json["outputs"].is_array()) {
        return;
    }
    std::vector<std::string> outs;
    outs.reserve(job_json["outputs"].size());
    for (const auto& v : job_json["outputs"]) {
        if (v.is_string()) outs.push_back(v.get<std::string>());
    }
    job_json["output_urls"] = build_output_urls(outs, base_url);
}

RequestHandlers::RequestHandlers(ModelManager& model_manager, QueueManager& queue_manager,
                                 AuthManager& auth_manager,
                                 const Config& config,
                                 const std::string& output_dir, const std::string& webui_dir,
                                 const AssistantConfig& assistant_config,
                                 const std::string& config_file_path,
                                 const std::string& docs_dir)
    : model_manager_(model_manager), queue_manager_(queue_manager),
      auth_manager_(auth_manager),
      paths_config_(config.paths),
      allow_public_outputs_(config.auth.allow_public_outputs),
      trusted_proxies_(config.server.trusted_proxies),
      output_dir_(output_dir), webui_dir_(webui_dir), docs_dir_(docs_dir)
    // ArchitectureManager uses config directory (where model_architectures.json lives), not output directory
    , architecture_manager_(std::make_unique<ArchitectureManager>(
          config_file_path.empty() ? output_dir : fs::path(config_file_path).parent_path().string()))
    , settings_manager_(std::make_unique<SettingsManager>(config_file_path, output_dir)) {

    // Initialize settings manager
    if (!settings_manager_->initialize()) {
        std::cerr << "[RequestHandlers] Warning: Failed to initialize settings manager" << std::endl;
    }

#ifdef SDCPP_ASSISTANT_ENABLED
    // Build the documentation index up front so the assistant's
    // search_docs tool can answer "how do I…" questions about features
    // (auth, mount, RunPod, MCP, etc.). Empty / missing docs_dir → empty
    // index → tool returns no results gracefully.
    docs_index_ = std::make_unique<DocsIndex>(docs_dir);

    // Create tool executor for backend query tool execution
    tool_executor_ = std::make_unique<ToolExecutor>(
        model_manager_, queue_manager_, architecture_manager_.get());
    tool_executor_->set_output_dir(output_dir);
    tool_executor_->set_settings_manager(settings_manager_.get());
    tool_executor_->set_docs_index(docs_index_.get());

    // Create assistant client with tool executor
    assistant_client_ = std::make_unique<AssistantClient>(
        assistant_config, output_dir, config_file_path, tool_executor_.get());
#else
    (void)assistant_config;  // Suppress unused parameter warning
#endif
}

void RequestHandlers::register_routes(httplib::Server& server) {
    using namespace api;
    using FT = schema::FieldType;

    // Create API registry
    api_registry_ = std::make_unique<ApiRegistry>(
        "sdcpp-restapi",
        sdcpp::get_version(),
        "REST API for stable-diffusion.cpp image and video generation"
    );
    auto& api = *api_registry_;

    // Register shared schemas that are referenced by $ref but not directly as endpoint types
    api.registerSchema("LoadOptions", LoadOptions::schema());

    // ── Authentication ───────────────────────────────────────────────
    // POST /auth/login is intentionally always-allowed (the user can't have
    // a token yet). POST /auth/logout requires a valid token to identify
    // which token to revoke; the auth middleware enforces that.
    api.addEndpoint<LoginRequest, LoginResponse>(
        server, "POST", "/auth/login",
        "Issue a bearer token for the configured credentials", "Auth", 200,
        [this](auto& req, auto& res) { handle_auth_login(req, res); });

    api.addEndpoint<void, LogoutResponse>(
        server, "POST", "/auth/logout",
        "Revoke the bearer token used in this request", "Auth", 200,
        [this](auto& req, auto& res) { handle_auth_logout(req, res); });

    // Register the 401 schema so OpenAPI consumers know the response shape.
    api.registerSchema("UnauthorizedResponse", UnauthorizedResponse::schema());

    // Auth middleware — runs before any route handler. The /auth/login
    // endpoint and a small allowlist (health, openapi.json, static UI / docs)
    // bypass it. Everything else must present `Authorization: Bearer <token>`.
    //
    // The /webdav/ prefix is a special case: WebDAV clients (Finder, Explorer,
    // dolphin, davfs) speak HTTP Basic, not Bearer. The Bearer middleware
    // skips /webdav/ via AuthManager::is_always_allowed(), and we dispatch
    // the WebDAV-specific methods (PROPFIND/MKCOL/MOVE/COPY/OPTIONS) below
    // after enforcing Basic auth ourselves. GET/HEAD/PUT/DELETE on /webdav/
    // are registered as normal routes and gated by handle_webdav_get/put/delete.
    {
        server.set_pre_routing_handler(
            [this](const httplib::Request& req, httplib::Response& res) -> httplib::Server::HandlerResponse {
                // ── WebDAV branch ─────────────────────────────────────
                // Runs even when the global auth_manager is disabled — the
                // pre-routing handler is the only place we can intercept
                // the custom HTTP methods (PROPFIND, MKCOL, MOVE, COPY).
                bool is_webdav_path =
                    req.path == "/webdav" ||
                    (req.path.size() >= 8 && req.path.compare(0, 8, "/webdav/") == 0);
                if (is_webdav_path) {
                    if (!verify_basic_auth(req)) {
                        send_webdav_unauthorized(res);
                        return httplib::Server::HandlerResponse::Handled;
                    }
                    if (req.method == "OPTIONS"  || req.method == "PROPFIND" ||
                        req.method == "MKCOL"    || req.method == "MOVE"     ||
                        req.method == "COPY") {
                        // Custom HTTP methods are handled here in pre_routing,
                        // before cpp-httplib reads the body. If the request
                        // had a body (PROPFIND XML, etc.), those bytes are
                        // still on the wire. Force Connection: close so
                        // they don't poison the next keep-alive request.
                        res.set_header("Connection", "close");
                        if (req.method == "OPTIONS")  return handle_webdav_options(req, res);
                        if (req.method == "PROPFIND") return handle_webdav_propfind(req, res);
                        if (req.method == "MKCOL")    return handle_webdav_mkcol(req, res);
                        if (req.method == "MOVE")     return handle_webdav_move(req, res);
                        if (req.method == "COPY")     return handle_webdav_copy(req, res);
                    }
                    if (req.method == "LOCK" || req.method == "UNLOCK") {
                        // v1: not implemented. macOS Finder soft-fails but
                        // continues; Windows Explorer also tolerates 405 here.
                        res.status = 405;
                        res.set_header("Allow", "OPTIONS, GET, HEAD, PROPFIND, PUT, DELETE, MKCOL, MOVE, COPY");
                        return httplib::Server::HandlerResponse::Handled;
                    }
                    // GET / HEAD / PUT / DELETE fall through to the regular
                    // route handlers registered below.
                    return httplib::Server::HandlerResponse::Unhandled;
                }

                // ── Cookie / Bearer / WS-token branch (everything else) ──
                if (!auth_manager_.enabled()) {
                    return httplib::Server::HandlerResponse::Unhandled;
                }

                // Always allow CORS preflight.
                if (req.method == "OPTIONS") {
                    return httplib::Server::HandlerResponse::Unhandled;
                }

                // /login is a public page. /assets, /favicon, /login.css are
                // referenced by it and must be reachable too.
                if (req.path == "/login" ||
                    req.path == "/login.css" ||
                    req.path == "/login.html" ||
                    req.path == "/favicon.ico" ||
                    req.path == "/favicon.svg") {
                    return httplib::Server::HandlerResponse::Unhandled;
                }

                // Allowlist (health, openapi, /auth/login, /ui/, /docs/,
                // /output/, /thumb/, /webdav/, /assets/).
                if (AuthManager::is_always_allowed(req.path)) {
                    return httplib::Server::HandlerResponse::Unhandled;
                }

                // Optional public-outputs bypass. When `auth.allow_public_outputs`
                // is true (default), generated images and thumbnails can be
                // fetched without auth so they can be embedded in third-party
                // clients via direct URL. Toggleable in config; only takes
                // effect when auth is otherwise enabled.
                if (allow_public_outputs_) {
                    auto starts_with = [&](const std::string& p) {
                        return req.path.size() >= p.size() &&
                               req.path.compare(0, p.size(), p) == 0;
                    };
                    if (starts_with("/output/") || starts_with("/thumb/")) {
                        return httplib::Server::HandlerResponse::Unhandled;
                    }
                }

                // Resolve "who is this caller?" trying, in order:
                //   1. Cookie (sdcpp_auth=<token>) — set by /auth/login,
                //      auto-attached by browsers on every same-origin
                //      request including <img>/<video>/WS handshakes.
                //   2. Bearer token (Authorization: Bearer <token>) —
                //      curl, scripts, MCP clients.
                //   3. ?token=<token> query param (legacy WS path; cookie
                //      should also work for WS but kept as a fallback).
                std::optional<std::string> user;

                std::string cookie_token = extract_cookie_token(req);
                if (!cookie_token.empty()) {
                    user = auth_manager_.verify_token(cookie_token);
                }

                if (!user.has_value()) {
                    std::string auth_header = req.get_header_value("Authorization");
                    const std::string bearer_prefix = "Bearer ";
                    if (auth_header.size() > bearer_prefix.size() &&
                        auth_header.compare(0, bearer_prefix.size(), bearer_prefix) == 0) {
                        std::string token = auth_header.substr(bearer_prefix.size());
                        if (!token.empty()) user = auth_manager_.verify_token(token);
                    }
                }

                if (!user.has_value() && req.path == "/ws") {
                    std::string ws_token = req.has_param("token")
                        ? req.get_param_value("token") : "";
                    if (!ws_token.empty()) user = auth_manager_.verify_token(ws_token);
                }

                if (!user.has_value()) {
                    // Browser-style requests for /ui/* should redirect to the
                    // login page rather than getting a 401 JSON body. We
                    // detect "browser-style" via Accept: text/html and the
                    // path being under /ui/. Everything else (API clients)
                    // gets the JSON 401.
                    bool wants_html = req.get_header_value("Accept").find("text/html") != std::string::npos;
                    bool is_ui_path = (req.path == "/ui" || req.path == "/ui/" ||
                                       (req.path.size() >= 4 && req.path.compare(0, 4, "/ui/") == 0));
                    if (wants_html && is_ui_path) {
                        std::string redirect = "/login?redirect=" + req.path;
                        res.status = 302;
                        res.set_header("Location", redirect);
                        return httplib::Server::HandlerResponse::Handled;
                    }
                    res.status = 401;
                    res.set_header("WWW-Authenticate", "Bearer realm=\"sdcpp-restapi\"");
                    nlohmann::json body = {
                        {"error", "unauthorized"},
                        {"message", "Authentication required. POST credentials to /auth/login to obtain a bearer token."}
                    };
                    res.set_content(body.dump(), "application/json");
                    return httplib::Server::HandlerResponse::Handled;
                }

                // Stash the authenticated username on the request for downstream
                // handlers that might want it (e.g. logout). httplib's Request
                // is const here, so we use a header that handlers can read.
                // (set_header on Request isn't exposed; we leave the token in
                // the Authorization header instead and re-read it where needed.)
                return httplib::Server::HandlerResponse::Unhandled;
            });
        if (auth_manager_.enabled()) {
            std::cout << "[Auth] Pre-routing auth middleware installed (Bearer + WebDAV/Basic)" << std::endl;
        } else {
            std::cout << "[Auth] Pre-routing handler installed for WebDAV (Bearer auth disabled)" << std::endl;
        }
    }

    // ── Health & Status ──────────────────────────────────────────────
    api.addEndpoint<void, HealthResponse>(
        server, "GET", "/health",
        "Server health check", "Status", 200,
        [this](auto& req, auto& res) { handle_health(req, res); });

    api.addEndpoint<void, MemoryResponse>(
        server, "GET", "/memory",
        "System and GPU memory status", "Status", 200,
        [this](auto& req, auto& res) { handle_memory(req, res); });

    api.addEndpoint<void, OptionsResponse>(
        server, "GET", "/options",
        "Available samplers, schedulers, quantization types", "Status", 200,
        [this](auto& req, auto& res) { handle_get_options(req, res); });

    api.addEndpoint<void, OptionDescriptionsResponse>(
        server, "GET", "/options/descriptions",
        "Descriptions of all available model-load options (sourced from data/load_options.json)",
        "Status", 200,
        [this](auto& req, auto& res) { handle_get_option_descriptions(req, res); });

    server.Get("/options/generation", [this](const httplib::Request& req, httplib::Response& res) {
        handle_get_generation_option_descriptions(req, res);
    });

    // ── Models ───────────────────────────────────────────────────────
    api.addEndpoint<void, ModelListResponse>(
        server, "GET", "/models",
        "List all available models", "Models", 200,
        [this](auto& req, auto& res) { handle_get_models(req, res); })
        .query_enum("type", "Filter by model type", MODEL_TYPE_VALUES)
        .query("extension", FT::String, "Filter by file extension")
        .query("search", FT::String, "Search in model name")
        .query("name", FT::String, "Search alias for 'search' parameter");

    api.addEndpoint<void, ModelRefreshResponse>(
        server, "POST", "/models/refresh",
        "Rescan model directories", "Models", 200,
        [this](auto& req, auto& res) { handle_refresh_models(req, res); });

    api.addEndpoint<LoadModelRequest, LoadModelResponse>(
        server, "POST", "/models/load",
        "Load a model into memory", "Models", 200,
        [this](auto& req, auto& res) { handle_load_model(req, res); });

    api.addEndpoint<void, SuccessResponse>(
        server, "POST", "/models/unload",
        "Unload the current model", "Models", 200,
        [this](auto& req, auto& res) { handle_unload_model(req, res); });

    api.addEndpointRaw(
        server, "GET", "/models/hash/{model_type}/{model_name}",
        R"(/models/hash/([^/]+)/(.+))",
        "Compute model SHA256 hash", "Models", 200,
        [this](auto& req, auto& res) { handle_get_model_hash(req, res); })
        .path_param("model_type", FT::String, "Model type category")
        .path_param("model_name", FT::String, "Model filename");

    api.addEndpoint<void, ModelPathsResponse>(
        server, "GET", "/models/paths",
        "Get configured model storage paths", "Models", 200,
        [this](auto& req, auto& res) { handle_get_model_paths(req, res); });

    // Multipart/form-data upload — request body schema is NOT modeled in
    // the OpenAPI spec yet (SchemaBuilder doesn't have multipart support).
    // The endpoint is auth-protected automatically via the pre-routing
    // middleware. See handle_upload_model() for the field contract.
    api.addEndpoint<void, UploadModelResponse>(
        server, "POST", "/models/upload",
        "Upload a model file (multipart/form-data: file, model_type, [filename], [subfolder])",
        "Models", 201,
        [this](auto& req, auto& res) { handle_upload_model(req, res); });

    // ── Model Downloads ──────────────────────────────────────────────
    api.addEndpoint<DownloadModelRequest, DownloadModelResponse>(
        server, "POST", "/models/download",
        "Download model from URL, CivitAI, or HuggingFace", "Downloads", 202,
        [this](auto& req, auto& res) { handle_download_model(req, res); });

    api.addEndpointRaw(
        server, "GET", "/models/civitai/{id}",
        R"(/models/civitai/(\d+(?:(?::|%3A)\d+)?))",
        "Get CivitAI model metadata", "Downloads", 200,
        [this](auto& req, auto& res) { handle_get_civitai_info(req, res); })
        .path_param("id", FT::String, "CivitAI model ID (format: id or id:version)");

    api.addEndpoint<void, HuggingfaceInfoResponse>(
        server, "GET", "/models/huggingface",
        "Get HuggingFace model file metadata", "Downloads", 200,
        [this](auto& req, auto& res) { handle_get_huggingface_info(req, res); })
        .query("repo_id", FT::String, "HuggingFace repository ID", true)
        .query("filename", FT::String, "File name in repository", true)
        .query("revision", FT::String, "Git revision", false, "main");

    // ── Generation ───────────────────────────────────────────────────
    api.addEndpoint<Txt2ImgRequest, JobCreatedResponse>(
        server, "POST", "/txt2img",
        "Generate image from text prompt", "Generation", 202,
        [this](auto& req, auto& res) { handle_txt2img(req, res); });

    api.addEndpoint<Img2ImgRequest, JobCreatedResponse>(
        server, "POST", "/img2img",
        "Generate image from source image and text", "Generation", 202,
        [this](auto& req, auto& res) { handle_img2img(req, res); });

    api.addEndpoint<Txt2VidRequest, JobCreatedResponse>(
        server, "POST", "/txt2vid",
        "Generate video from text prompt", "Generation", 202,
        [this](auto& req, auto& res) { handle_txt2vid(req, res); });

    api.addEndpoint<UpscaleRequest, JobCreatedResponse>(
        server, "POST", "/upscale",
        "Upscale image using ESRGAN", "Generation", 202,
        [this](auto& req, auto& res) { handle_upscale(req, res); });

    api.addEndpoint<ConvertRequest, JobCreatedResponse>(
        server, "POST", "/convert",
        "Convert model format (safetensors to GGUF)", "Generation", 202,
        [this](auto& req, auto& res) { handle_convert(req, res); });

    // ── Upscaler ─────────────────────────────────────────────────────
    api.addEndpoint<LoadUpscalerRequest, SuccessResponse>(
        server, "POST", "/upscaler/load",
        "Load an ESRGAN upscaler model", "Upscaler", 200,
        [this](auto& req, auto& res) { handle_load_upscaler(req, res); });

    api.addEndpoint<void, SuccessResponse>(
        server, "POST", "/upscaler/unload",
        "Unload the current upscaler", "Upscaler", 200,
        [this](auto& req, auto& res) { handle_unload_upscaler(req, res); });

    // ── Queue ────────────────────────────────────────────────────────
    api.addEndpoint<void, QueueListResponse>(
        server, "GET", "/queue",
        "List jobs with filtering and pagination", "Queue", 200,
        [this](auto& req, auto& res) { handle_get_queue(req, res); })
        .query_enum("status", "Filter by job status", JOB_STATUS_VALUES)
        .query_enum("type", "Filter by job type", JOB_TYPE_VALUES)
        .query("search", FT::String, "Search in prompt/params")
        .query("architecture", FT::String, "Filter by model architecture")
        .query("group_by", FT::String, "Group results (date)")
        .query("limit", FT::Integer, "Results per page", false, 20)
        .query("page", FT::Integer, "Page number", false, 1)
        .query("offset", FT::Integer, "Pagination offset")
        .query("before", FT::Integer, "Get jobs before timestamp (unix)")
        .query("after", FT::Integer, "Get jobs after timestamp (unix)")
        .query("sort", FT::String, "Sort field", false, "created_at")
        .query_enum("order", "Sort order", {"asc", "desc"});

    api.addEndpointRaw(
        server, "GET", "/queue/{job_id}",
        R"(/queue/([a-f0-9\-]+))",
        "Get job status", "Queue", 200,
        [this](auto& req, auto& res) { handle_get_job(req, res); })
        .path_param("job_id", FT::String, "Job UUID");

    api.addEndpointRaw(
        server, "DELETE", "/queue/{job_id}",
        R"(/queue/([a-f0-9\-]+))",
        "Cancel a pending job", "Queue", 200,
        [this](auto& req, auto& res) { handle_cancel_job(req, res); })
        .path_param("job_id", FT::String, "Job UUID");

    api.addEndpoint<DeleteJobsRequest, DeleteJobsResponse>(
        server, "DELETE", "/queue/jobs",
        "Batch delete jobs (soft-delete to recycle bin)", "Queue", 200,
        [this](auto& req, auto& res) { handle_delete_jobs(req, res); });

    // ── Recycle Bin ──────────────────────────────────────────────────
    api.addEndpoint<void, RecycleBinResponse>(
        server, "GET", "/queue/recycle-bin",
        "List deleted jobs in recycle bin", "Recycle Bin", 200,
        [this](auto& req, auto& res) { handle_get_recycle_bin(req, res); });

    api.addEndpointRaw(
        server, "POST", "/queue/{job_id}/restore",
        R"(/queue/([a-f0-9\-]+)/restore)",
        "Restore a job from recycle bin", "Recycle Bin", 200,
        [this](auto& req, auto& res) { handle_restore_job(req, res); })
        .path_param("job_id", FT::String, "Job UUID");

    api.addEndpointRaw(
        server, "DELETE", "/queue/{job_id}/purge",
        R"(/queue/([a-f0-9\-]+)/purge)",
        "Permanently delete a job", "Recycle Bin", 200,
        [this](auto& req, auto& res) { handle_purge_job(req, res); })
        .path_param("job_id", FT::String, "Job UUID");

    api.addEndpoint<void, SuccessResponse>(
        server, "DELETE", "/queue/recycle-bin",
        "Clear all items from recycle bin", "Recycle Bin", 200,
        [this](auto& req, auto& res) { handle_clear_recycle_bin(req, res); });

    api.addEndpoint<void, RecycleBinSettingsResponse>(
        server, "GET", "/settings/recycle-bin",
        "Get recycle bin settings", "Recycle Bin", 200,
        [this](auto& req, auto& res) { handle_get_recycle_bin_settings(req, res); });

    // ── Output folder grouping ───────────────────────────────────────
    server.Get("/settings/output", [this](const auto& req, auto& res) {
        handle_get_output_settings(req, res);
    });
    server.Put("/settings/output", [this](const auto& req, auto& res) {
        handle_set_output_settings(req, res);
    });

    // ── Job Preview ──────────────────────────────────────────────────
    api.addEndpointRaw(
        server, "GET", "/jobs/{job_id}/preview",
        R"(/jobs/([a-f0-9\-]+)/preview)",
        "Get live preview JPEG for a processing job", "Queue", 200,
        [this](auto& req, auto& res) { handle_get_job_preview(req, res); })
        .path_param("job_id", FT::String, "Job UUID")
        .response_type("image/jpeg");

    // ── File Browser & Thumbnails ────────────────────────────────────
    api.addEndpointRaw(
        server, "GET", "/thumb/{path}",
        R"(/thumb/(.*))",
        "Get thumbnail for image/video file", "Files", 200,
        [this](auto& req, auto& res) { handle_thumbnail(req, res); })
        .path_param("path", FT::String, "File path relative to output directory")
        .response_type("image/jpeg");

    api.addEndpoint<void, void>(
        server, "GET", "/output",
        "Browse output directory (root)", "Files", 200,
        [this](auto& req, auto& res) { handle_file_browser(req, res); })
        .query("sort", FT::String, "Sort field (name, size, mtime)")
        .query_enum("order", "Sort order", {"asc", "desc"});

    api.addEndpointRaw(
        server, "GET", "/output/{path}",
        R"(/output/(.*))",
        "Browse or download output file", "Files", 200,
        [this](auto& req, auto& res) { handle_file_browser(req, res); })
        .path_param("path", FT::String, "File or directory path");

    // ── Web UI ───────────────────────────────────────────────────────
    if (!webui_dir_.empty()) {
        std::cout << "[Routes] Registering WebUI routes (webui_dir=" << webui_dir_ << ")" << std::endl;
        server.Get("/ui", [](const httplib::Request& /*req*/, httplib::Response& res) {
            res.set_redirect("/ui/");
        });
        server.Get("/ui/", [this](const httplib::Request& req, httplib::Response& res) {
            handle_webui(req, res);
        });
        server.Get(R"(/ui/(.*))", [this](const httplib::Request& req, httplib::Response& res) {
            handle_webui(req, res);
        });

        // Server-rendered login page. Lives next to the SPA bundle in
        // <webui_dir>/login.{html,css}. The pre-routing handler skips auth
        // for these paths so anyone can reach them.
        auto serve_static = [this](const std::string& filename, const std::string& mime,
                                    const httplib::Request&, httplib::Response& res) {
            namespace fs = std::filesystem;
            fs::path p = fs::path(webui_dir_) / filename;
            std::ifstream f(p, std::ios::binary);
            if (!f) {
                res.status = 404;
                res.set_content("Not found", "text/plain");
                return;
            }
            std::stringstream buf;
            buf << f.rdbuf();
            res.set_content(buf.str(), mime.c_str());
        };
        server.Get("/login", [serve_static](const httplib::Request& req, httplib::Response& res) {
            serve_static("login.html", "text/html; charset=utf-8", req, res);
        });
        server.Get("/login.html", [serve_static](const httplib::Request& req, httplib::Response& res) {
            serve_static("login.html", "text/html; charset=utf-8", req, res);
        });
        server.Get("/login.css", [serve_static](const httplib::Request& req, httplib::Response& res) {
            serve_static("login.css", "text/css; charset=utf-8", req, res);
        });
    } else {
        std::cout << "[Routes] WebUI routes NOT registered (webui_dir is empty)" << std::endl;
    }

    // ── Documentation ────────────────────────────────────────────────
    if (!docs_dir_.empty()) {
        std::cout << "[Routes] Registering Docs routes (docs_dir=" << docs_dir_ << ")" << std::endl;
        server.Get("/docs", [](const httplib::Request& /*req*/, httplib::Response& res) {
            res.set_redirect("/docs/");
        });
        server.Get("/docs/", [this](const httplib::Request& req, httplib::Response& res) {
            handle_docs(req, res);
        });
        server.Get(R"(/docs/(.*))", [this](const httplib::Request& req, httplib::Response& res) {
            handle_docs(req, res);
        });
    } else {
        std::cout << "[Routes] Docs routes NOT registered (docs_dir is empty)" << std::endl;
    }

    // ── Preview Settings ─────────────────────────────────────────────
    api.addEndpoint<void, PreviewSettingsResponse>(
        server, "GET", "/preview/settings",
        "Get preview generation settings", "Settings", 200,
        [this](auto& req, auto& res) { handle_get_preview_settings(req, res); });

    api.addEndpoint<UpdatePreviewSettingsRequest, SuccessResponse>(
        server, "PUT", "/preview/settings",
        "Update preview settings", "Settings", 200,
        [this](auto& req, auto& res) { handle_update_preview_settings(req, res); });

#ifdef SDCPP_ASSISTANT_ENABLED
    // ── Assistant ────────────────────────────────────────────────────
    api.addEndpoint<AssistantChatRequest, void>(
        server, "POST", "/assistant/chat",
        "Chat with assistant LLM", "Assistant", 200,
        [this](auto& req, auto& res) { handle_assistant_chat(req, res); });

    api.addEndpoint<AssistantChatRequest, void>(
        server, "POST", "/assistant/chat/stream",
        "Chat with streaming (SSE)", "Assistant", 200,
        [this](auto& req, auto& res) { handle_assistant_chat_stream(req, res); })
        .response_type("text/event-stream");

    api.addEndpoint<void, void>(
        server, "GET", "/assistant/history",
        "Get chat history", "Assistant", 200,
        [this](auto& req, auto& res) { handle_assistant_history(req, res); });

    api.addEndpoint<void, SuccessResponse>(
        server, "DELETE", "/assistant/history",
        "Clear chat history", "Assistant", 200,
        [this](auto& req, auto& res) { handle_assistant_clear_history(req, res); });

    api.addEndpoint<void, AssistantStatusResponse>(
        server, "GET", "/assistant/status",
        "Get assistant status", "Assistant", 200,
        [this](auto& req, auto& res) { handle_assistant_status(req, res); });

    api.addEndpoint<void, void>(
        server, "GET", "/assistant/settings",
        "Get assistant settings", "Assistant", 200,
        [this](auto& req, auto& res) { handle_assistant_get_settings(req, res); });

    api.addEndpoint<void, SuccessResponse>(
        server, "PUT", "/assistant/settings",
        "Update assistant settings", "Assistant", 200,
        [this](auto& req, auto& res) { handle_assistant_update_settings(req, res); });

    api.addEndpoint<void, void>(
        server, "GET", "/assistant/model-info",
        "Get loaded assistant LLM info", "Assistant", 200,
        [this](auto& req, auto& res) { handle_assistant_model_info(req, res); });
#endif

    // ── Settings ─────────────────────────────────────────────────────
    api.addEndpoint<void, void>(
        server, "GET", "/settings/generation",
        "Get all generation defaults", "Settings", 200,
        [this](auto& req, auto& res) { handle_get_generation_defaults(req, res); });

    api.addEndpoint<void, SuccessResponse>(
        server, "PUT", "/settings/generation",
        "Update all generation defaults", "Settings", 200,
        [this](auto& req, auto& res) { handle_update_generation_defaults(req, res); });

    api.addEndpointRaw(
        server, "GET", "/settings/generation/{mode}",
        R"(/settings/generation/([^/]+))",
        "Get generation defaults for a mode", "Settings", 200,
        [this](auto& req, auto& res) { handle_get_generation_defaults_for_mode(req, res); })
        .path_param("mode", FT::String, "Generation mode (txt2img, img2img, txt2vid, upscale)");

    api.addEndpointRaw(
        server, "PUT", "/settings/generation/{mode}",
        R"(/settings/generation/([^/]+))",
        "Update generation defaults for a mode", "Settings", 200,
        [this](auto& req, auto& res) { handle_update_generation_defaults_for_mode(req, res); })
        .path_param("mode", FT::String, "Generation mode");

    api.addEndpoint<void, void>(
        server, "GET", "/settings/preferences",
        "Get UI preferences", "Settings", 200,
        [this](auto& req, auto& res) { handle_get_ui_preferences(req, res); });

    api.addEndpoint<void, SuccessResponse>(
        server, "PUT", "/settings/preferences",
        "Update UI preferences", "Settings", 200,
        [this](auto& req, auto& res) { handle_update_ui_preferences(req, res); });

    api.addEndpoint<void, SuccessResponse>(
        server, "POST", "/settings/reset",
        "Reset all settings to defaults", "Settings", 200,
        [this](auto& req, auto& res) { handle_reset_settings(req, res); });

    // ── Architectures ────────────────────────────────────────────────
    api.addEndpoint<void, ArchitecturesResponse>(
        server, "GET", "/architectures",
        "Get model architecture presets", "Architectures", 200,
        [this](auto& req, auto& res) { handle_get_architectures(req, res); });

    api.addEndpoint<void, DetectArchitectureResponse>(
        server, "GET", "/architectures/detect",
        "Detect architecture of a model", "Architectures", 200,
        [this](auto& req, auto& res) { handle_detect_architecture(req, res); })
        .query("model", FT::String, "Model name to detect", true);

    // ── WebDAV (read+write file mount) ───────────────────────────────
    // OPTIONS / PROPFIND / MKCOL / MOVE / COPY are dispatched by the
    // pre-routing handler above (so we can intercept the custom HTTP
    // methods before cpp-httplib's regular dispatch sees them). What
    // remains here are the standard methods that cpp-httplib already
    // routes natively. They run AFTER the pre-routing handler has done
    // Basic auth, so by the time they fire, the request is authenticated.
    // cpp-httplib's Server::routing() dispatches HEAD requests to the GET
    // handler chain (line ~11412 of httplib.h: GET || HEAD → get_handlers_).
    // The framework strips the response body for HEAD before sending. So a
    // single Get() registration covers both verbs.
    server.Get(R"(/webdav/(.*))", [this](const httplib::Request& req, httplib::Response& res) {
        handle_webdav_get(req, res, /*head_only=*/(req.method == "HEAD"));
    });
    server.Put(R"(/webdav/(.*))", [this](const httplib::Request& req, httplib::Response& res) {
        handle_webdav_put(req, res);
    });
    server.Delete(R"(/webdav/(.*))", [this](const httplib::Request& req, httplib::Response& res) {
        handle_webdav_delete(req, res);
    });

    // ── OpenAPI Schema ───────────────────────────────────────────────
    api.serveOpenApiSpec(server, "/openapi.json");

    // ── llms.txt (LLM discoverability) ───────────────────────────────
    // Convention from https://llmstxt.org/ — a markdown file at the root
    // of a service that an LLM agent can fetch without auth to learn
    // what's here and where the machine-readable docs live. Both /llms.txt
    // (canonical) and /llm.txt (common typo) serve the same content.
    auto serve_llms_txt = [](const httplib::Request& /*req*/, httplib::Response& res) {
        static const char* body =
            "# sdcpp-restapi\n"
            "\n"
            "> A REST API wrapping stable-diffusion.cpp for image and video generation.\n"
            "> Provides HTTP endpoints, WebSocket progress streaming, an MCP server, and\n"
            "> a Vue.js WebUI. Supports a1111-style dynamic-prompts (`{a|b|c}`) with\n"
            "> per-variation queue items grouped under a shared `variation_group_id`.\n"
            "\n"
            "## Authentication\n"
            "\n"
            "Most endpoints require a session token. Obtain one by POSTing\n"
            "`{username, password}` JSON to `/auth/login`, then send\n"
            "`Authorization: Bearer <token>` on subsequent requests. Browser clients\n"
            "use an HttpOnly cookie set by the same endpoint.\n"
            "\n"
            "Endpoints exempt from auth: `/health`, `/openapi.json`, `/docs/`, `/llms.txt`.\n"
            "\n"
            "## API reference\n"
            "\n"
            "- [OpenAPI 3.1 schema](/openapi.json) — machine-readable, always in sync with the running build.\n"
            "- [REST API guide](/docs/API.md) — human-readable, with worked examples.\n"
            "- [LLM agent guide](/docs/LLM_GUIDE.md) — workflows tailored to autonomous agents.\n"
            "- [MCP server reference](/docs/MCP.md) — JSON-RPC over `/mcp`, alternative to REST.\n"
            "\n"
            "## Per-field option documentation\n"
            "\n"
            "Both endpoints below return JSON describing each option's purpose, default, and recommended usage — sourced from sd.cpp's actual code. Useful for an LLM choosing values for `/models/load` or generation calls.\n"
            "\n"
            "- [Model-load options](/options/descriptions) — every field accepted by `POST /models/load`'s `options` object.\n"
            "- [Generation options](/options/generation) — every field accepted by `/txt2img`, `/img2img`, `/txt2vid`, `/upscale`.\n"
            "\n"
            "## Optional\n"
            "\n"
            "- [Library reference](/docs/LIBRARY_REFERENCE.md) — internal C++ libraries.\n"
            "- [Health check](/health) — server status, loaded model, memory snapshot.\n";
        res.set_content(body, "text/markdown; charset=utf-8");
    };
    server.Get("/llms.txt", serve_llms_txt);
    server.Get("/llm.txt", serve_llms_txt);

    std::cout << "[Routes] All API routes registered. OpenAPI spec available at /openapi.json" << std::endl;
}

void RequestHandlers::handle_auth_login(const httplib::Request& req, httplib::Response& res) {
    // If auth is disabled at the server level, /auth/login is meaningless —
    // tell the caller, but don't 500.
    if (!auth_manager_.enabled()) {
        nlohmann::json body = {
            {"error", "auth_disabled"},
            {"message", "Authentication is disabled on this server. No token is required."}
        };
        send_json(res, body, 400);
        return;
    }

    nlohmann::json body;
    try {
        body = parse_json_body(req);
    } catch (const std::exception& e) {
        send_error(res, std::string("Invalid JSON: ") + e.what(), 400);
        return;
    }

    std::string username = body.value("username", "");
    std::string password = body.value("password", "");

    if (username.empty() || password.empty()) {
        // Run a verify_credentials call anyway to keep timing similar.
        (void)auth_manager_.verify_credentials(username, password);
        res.status = 401;
        res.set_header("WWW-Authenticate", "Bearer realm=\"sdcpp-restapi\"");
        nlohmann::json err = {
            {"error", "invalid_credentials"},
            {"message", "Username and password are required."}
        };
        res.set_content(err.dump(), "application/json");
        return;
    }

    if (!auth_manager_.verify_credentials(username, password)) {
        std::cerr << "[Auth] Failed login attempt for user='" << username << "'" << std::endl;
        res.status = 401;
        res.set_header("WWW-Authenticate", "Bearer realm=\"sdcpp-restapi\"");
        nlohmann::json err = {
            {"error", "invalid_credentials"},
            {"message", "Invalid username or password."}
        };
        res.set_content(err.dump(), "application/json");
        return;
    }

    std::string token = auth_manager_.issue_token(username);
    auto now = std::chrono::system_clock::now();
    auto expires_at_tp = now + std::chrono::seconds(auth_manager_.token_ttl_seconds());
    int64_t expires_at = std::chrono::duration_cast<std::chrono::seconds>(
        expires_at_tp.time_since_epoch()).count();

    // Set the auth cookie alongside the JSON token so:
    //   - Browsers attach it automatically to every same-origin request
    //     (including <img> tags hitting /output, WS handshakes, etc.) — no
    //     custom Authorization header machinery needed in the SPA.
    //   - HttpOnly: JS cannot read it, mitigates XSS exfiltration.
    //   - SameSite=Strict: CSRF-immune at the browser level.
    //   - Path=/: cookie sent on every server route.
    {
        std::ostringstream cookie;
        cookie << "sdcpp_auth=" << token
               << "; HttpOnly; SameSite=Strict; Path=/"
               << "; Max-Age=" << auth_manager_.token_ttl_seconds();
        res.set_header("Set-Cookie", cookie.str());
    }

    // JSON body still includes the token for non-browser clients (curl,
    // scripts, MCP) that prefer Bearer.
    nlohmann::json out = {
        {"token", token},
        {"token_type", "Bearer"},
        {"expires_at", expires_at},
        {"username", username}
    };
    send_json(res, out, 200);
}

void RequestHandlers::handle_auth_logout(const httplib::Request& req, httplib::Response& res) {
    // If we got past the middleware, the caller is authenticated.
    // Revoke whichever token form they used (Bearer header OR cookie) and
    // clear the cookie via Max-Age=0 so the browser drops it.
    if (auth_manager_.enabled()) {
        std::string auth_header = req.get_header_value("Authorization");
        const std::string bearer_prefix = "Bearer ";
        if (auth_header.size() > bearer_prefix.size() &&
            auth_header.compare(0, bearer_prefix.size(), bearer_prefix) == 0) {
            auth_manager_.revoke_token(auth_header.substr(bearer_prefix.size()));
        }
        // Cookie-based session
        std::string cookie_token = extract_cookie_token(req);
        if (!cookie_token.empty()) {
            auth_manager_.revoke_token(cookie_token);
        }
    }
    // Always clear the cookie regardless — defensive (user may have a stale
    // one even after auth is disabled at the server).
    res.set_header(
        "Set-Cookie",
        "sdcpp_auth=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0");

    nlohmann::json body = {
        {"success", true},
        {"message", "Logged out."}
    };
    send_json(res, body, 200);
}

void RequestHandlers::handle_health(const httplib::Request& req, httplib::Response& res) {
    auto loaded_info = model_manager_.get_loaded_models_info();

    // Resolve current authenticated username (if any) for SPA display.
    // /health is a no-auth endpoint so callers without a session see null;
    // callers with a valid cookie/bearer get their username back.
    nlohmann::json username = nullptr;
    if (auth_manager_.enabled()) {
        std::string token = extract_cookie_token(req);
        if (token.empty()) {
            std::string h = req.get_header_value("Authorization");
            const std::string p = "Bearer ";
            if (h.size() > p.size() && h.compare(0, p.size(), p) == 0) {
                token = h.substr(p.size());
            }
        }
        if (!token.empty()) {
            auto u = auth_manager_.verify_token(token);
            if (u.has_value()) username = *u;
        }
    }
    auto memory_info = get_memory_info();

    nlohmann::json response = {
        {"status", "ok"},
        {"version", sdcpp::get_version()},
        {"git_commit", sdcpp::get_git_commit()},
        {"model_loaded", loaded_info["model_loaded"]},
        {"model_loading", loaded_info["model_loading"]},
        {"loading_model_name", loaded_info["loading_model_name"]},
        {"loading_step", loaded_info["loading_step"]},
        {"loading_total_steps", loaded_info["loading_total_steps"]},
        {"last_error", loaded_info["last_error"]},
        {"model_name", loaded_info["model_name"]},
        {"model_type", loaded_info["model_type"]},
        {"model_architecture", loaded_info["model_architecture"]},
        {"loaded_components", loaded_info["loaded_components"]},
        {"load_options", loaded_info.contains("load_options") ? loaded_info["load_options"] : nlohmann::json(nullptr)},
        {"username", username},
        {"upscaler_loaded", loaded_info["upscaler_loaded"]},
        {"upscaler_name", loaded_info["upscaler_name"]},
#ifdef SDCPP_WEBSOCKET_ENABLED
        {"ws_enabled", true},
#else
        {"ws_enabled", false},
#endif
        {"memory", memory_info.to_json()},
        {"features", {
#ifdef SDCPP_EXPERIMENTAL_OFFLOAD
            {"experimental_offload", true},
#else
            {"experimental_offload", false},
#endif
            // Distinguishes which fork branch backs the experimental_offload
            // build. true → fork's feature/unified-streaming (single
            // stream_layers + max_vram); false → fork's feature/vram-offloading-v2
            // (legacy offload_mode + offload_config). Only meaningful when
            // experimental_offload is true.
#ifdef SDCPP_UNIFIED_STREAMING
            {"unified_streaming", true},
#else
            {"unified_streaming", false},
#endif
#ifdef SDCPP_MCP_ENABLED
            {"mcp", true},
#else
            {"mcp", false},
#endif
            {"auth_required", auth_manager_.enabled()}
        }}
    };

    send_json(res, response);
}

void RequestHandlers::handle_memory(const httplib::Request& /*req*/, httplib::Response& res) {
    auto memory_info = get_memory_info();
    send_json(res, memory_info.to_json());
}

void RequestHandlers::handle_get_options(const httplib::Request& /*req*/, httplib::Response& res) {
    // Return available samplers, schedulers, and quantization types
    nlohmann::json response = {
        {"samplers", {
            "euler",
            "euler_a",
            "heun",
            "dpm2",
            "dpm++2s_a",
            "dpm++2m",
            "dpm++2mv2",
            "ipndm",
            "ipndm_v",
            "lcm",
            "ddim_trailing",
            "tcd",
            "res_multistep",
            "res_2s",
            "er_sde",
            "euler_cfg_pp",
            "euler_a_cfg_pp",
            "euler_ge"
        }},
        {"schedulers", {
            "discrete",
            "karras",
            "exponential",
            "ays",
            "gits",
            "sgm_uniform",
            "simple",
            "smoothstep",
            "kl_optimal",
            "lcm",
            "bong_tangent",
            "ltx2"
        }},
        {"quantization_types", nlohmann::json::array({
            {{"id", "f32"}, {"name", "F32 (32-bit float)"}, {"bits", 32}},
            {{"id", "f16"}, {"name", "F16 (16-bit float)"}, {"bits", 16}},
            {{"id", "bf16"}, {"name", "BF16 (Brain float 16)"}, {"bits", 16}},
            {{"id", "q8_0"}, {"name", "Q8_0 (8-bit)"}, {"bits", 8}},
            {{"id", "q5_0"}, {"name", "Q5_0 (5-bit)"}, {"bits", 5}},
            {{"id", "q5_1"}, {"name", "Q5_1 (5-bit)"}, {"bits", 5}},
            {{"id", "q4_0"}, {"name", "Q4_0 (4-bit)"}, {"bits", 4}},
            {{"id", "q4_1"}, {"name", "Q4_1 (4-bit)"}, {"bits", 4}},
            {{"id", "q4_k"}, {"name", "Q4_K (4-bit K-quant)"}, {"bits", 4}},
            {{"id", "q5_k"}, {"name", "Q5_K (5-bit K-quant)"}, {"bits", 5}},
            {{"id", "q6_k"}, {"name", "Q6_K (6-bit K-quant)"}, {"bits", 6}},
            {{"id", "q8_k"}, {"name", "Q8_K (8-bit K-quant)"}, {"bits", 8}},
            {{"id", "q3_k"}, {"name", "Q3_K (3-bit K-quant)"}, {"bits", 3}},
            {{"id", "q2_k"}, {"name", "Q2_K (2-bit K-quant)"}, {"bits", 2}},
            {{"id", "mxfp4"}, {"name", "MXFP4 (4-bit OCP microscaling float)"}, {"bits", 4}},
            {{"id", "nvfp4"}, {"name", "NVFP4 (4-bit NVIDIA float)"}, {"bits", 4}},
            {{"id", "q1_0"}, {"name", "Q1_0 (1-bit, experimental)"}, {"bits", 1}}
        })}
    };

    send_json(res, response);
}

void RequestHandlers::handle_get_models(const httplib::Request& req, httplib::Response& res) {
    ModelFilter filter;

    // Parse query parameters
    if (req.has_param("type")) {
        std::string type_str = req.get_param_value("type");
        // Validate type
        if (type_str == "checkpoint" || type_str == "diffusion" ||
            type_str == "vae" || type_str == "lora" ||
            type_str == "clip" || type_str == "t5" || type_str == "embedding" ||
            type_str == "controlnet" || type_str == "llm" || type_str == "esrgan") {
            filter.type = string_to_model_type(type_str);
        }
    }

    if (req.has_param("extension")) {
        std::string ext = req.get_param_value("extension");
        // Remove leading dot if present
        if (!ext.empty() && ext[0] == '.') {
            ext = ext.substr(1);
        }
        filter.extension = ext;
    }

    if (req.has_param("search")) {
        filter.search = req.get_param_value("search");
    }

    // Also support 'name' as alias for 'search'
    if (req.has_param("name") && !filter.search.has_value()) {
        filter.search = req.get_param_value("name");
    }

    send_json(res, model_manager_.get_models_json(filter));
}

void RequestHandlers::handle_refresh_models(const httplib::Request& /*req*/, httplib::Response& res) {
    std::cout << "[RequestHandlers] Refreshing model list..." << std::endl;

    // Rescan all model directories
    model_manager_.scan_models();

    // Return success with updated model list
    nlohmann::json response = {
        {"success", true},
        {"message", "Model list refreshed"},
        {"models", model_manager_.get_models_json(ModelFilter{})}
    };

    std::cout << "[RequestHandlers] Model list refreshed successfully" << std::endl;
    send_json(res, response);
}

void RequestHandlers::handle_load_model(const httplib::Request& req, httplib::Response& res) {
    // Default flow is async: validate the request synchronously, then
    // dispatch the heavy work (sd.cpp model load — minutes for big GGUFs)
    // to a detached thread so the cpp-httplib worker pool is free for
    // concurrent traffic (WebDAV, /health, WS pings) that would otherwise
    // feel "frozen" while a load was in progress. Completion is reported
    // via WebSocket events: model_loading_progress (during), model_loaded
    // (success), model_load_failed (failure).
    //
    // Optional ?wait=true holds the HTTP response until the load
    // completes (or the optional ?timeout=<sec> elapses). Useful for
    // scripts that want a single blocking call instead of polling
    // /health. Tradeoff: the request thread stays parked, so a few
    // concurrent wait=true calls can saturate the worker pool. Default
    // timeout 600 sec covers a 10-min load on slow disks.
    try {
        // Strict query-param validation: reject anything not in the
        // closed allow-list with a 400. Same UX rationale as the body
        // validator — typos like ?waitt=true should fail loudly instead
        // of being silently ignored (the user thinks they enabled
        // synchronous wait, the server doesn't).
        static const std::unordered_set<std::string> KNOWN_QUERY = {
            "wait", "timeout",
        };
        std::vector<std::string> unknown_qp;
        for (const auto& kv : req.params) {
            if (KNOWN_QUERY.find(kv.first) == KNOWN_QUERY.end()) {
                unknown_qp.push_back(kv.first);
            }
        }
        if (!unknown_qp.empty()) {
            std::string msg = "Unknown query parameter(s): ";
            for (size_t i = 0; i < unknown_qp.size(); ++i) {
                if (i) msg += ", ";
                msg += unknown_qp[i];
            }
            msg += ". Accepted: wait, timeout.";
            send_error(res, msg, 400);
            return;
        }

        auto body = parse_json_body(req);
        auto params = ModelLoadParams::from_json(body);

        // Parse the wait/timeout query params up front. Tolerant of
        // common true-ish strings ("1", "true", "yes").
        bool wait = false;
        if (req.has_param("wait")) {
            std::string v = req.get_param_value("wait");
            for (auto& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            wait = (v == "1" || v == "true" || v == "yes" || v == "on");
        }
        int timeout_sec = 600;  // 10 min default
        if (req.has_param("timeout")) {
            try {
                timeout_sec = std::stoi(req.get_param_value("timeout"));
                if (timeout_sec <= 0) timeout_sec = 600;
                if (timeout_sec > 3600) timeout_sec = 3600;  // hard cap 1 hour
            } catch (...) { /* fall through to default */ }
        }

        // Reject if a load is already in progress — saves the user from
        // queuing on a context_mutex_ that will hold for minutes.
        if (model_manager_.is_loading()) {
            send_error(res,
                "Another model is already loading. Wait for it to finish "
                "or call /models/unload first.", 409);
            return;
        }

        // Reject if a model is already loaded. We require an explicit
        // /models/unload before loading another one so that automation
        // submitting POST /models/load twice in a row can't accidentally
        // tear down an in-use model. Caller decides when to unload.
        if (model_manager_.is_model_loaded()) {
            nlohmann::json err = {
                {"error", "A model is already loaded. Call POST /models/unload "
                          "first, then POST /models/load."},
                {"loaded_model", model_manager_.get_loaded_model_name()}
            };
            res.status = 409;
            res.set_header("Content-Type", "application/json");
            res.set_content(err.dump(), "application/json");
            return;
        }

        std::cout << "[RequestHandlers] Loading model (" << (wait ? "wait" : "async") << "): "
                  << params.model_name
                  << (wait ? " timeout=" + std::to_string(timeout_sec) + "s" : "")
                  << std::endl;

        // Detach: the load runs to completion regardless of whether the
        // HTTP request stays open. ModelManager::load_model handles its
        // own progress reporting + WS broadcast via the model_manager
        // internals.
        std::thread([this, params]() {
            try {
                model_manager_.load_model(params);
            } catch (const std::exception& e) {
                // ModelManager::load_model emits a model_load_failed WS
                // event on failure; we just log here for server-side
                // forensics.
                std::cerr << "[RequestHandlers] (async) model load threw: "
                          << e.what() << std::endl;
            }
        }).detach();

        // ── Async branch (default) ────────────────────────────────────
        if (!wait) {
            res.status = 202;
            nlohmann::json response = {
                {"success",     true},
                {"status",      "loading"},
                {"message",     "Model load started. Watch /health.model_loading "
                                "or the model_loading_progress / model_loaded WS "
                                "events for completion. Pass ?wait=true to block "
                                "until the load finishes."},
                {"model_name",  params.model_name},
                {"model_type",  model_type_to_string(params.model_type)}
            };
            res.set_header("Content-Type", "application/json");
            res.set_content(response.dump(), "application/json");
            return;
        }

        // ── Synchronous wait branch ───────────────────────────────────
        // Poll is_loading() at 200ms intervals until the detached thread
        // finishes or our timeout expires. is_loading() goes true inside
        // load_model() before any heavy work, so we never miss the
        // transition. After it returns false, sample model_loaded /
        // last_load_error to decide success vs failure.
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(timeout_sec);
        // Brief settle so the worker thread has a chance to flip
        // is_loading() to true before we start polling — otherwise a
        // very fast validation failure could race the poll and we'd
        // observe is_loading()==false before it ever went true.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        while (model_manager_.is_loading()) {
            if (std::chrono::steady_clock::now() >= deadline) {
                // Caller's timeout exceeded. The load thread keeps
                // running in the background; the WS events still fire.
                res.status = 504;
                nlohmann::json response = {
                    {"success",     false},
                    {"status",      "loading"},
                    {"error",       "Model load did not finish within "
                                    + std::to_string(timeout_sec)
                                    + " seconds. Loading continues in the "
                                    "background — poll /health or watch "
                                    "the model_loaded WebSocket event."},
                    {"model_name",  params.model_name},
                    {"timeout_sec", timeout_sec}
                };
                res.set_header("Content-Type", "application/json");
                res.set_content(response.dump(), "application/json");
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        // Loading flag cleared. Decide success vs failure from
        // model_loaded + last_load_error (set by load_model on failure
        // before is_loading flips back to false).
        if (model_manager_.is_model_loaded()
            && model_manager_.get_loaded_model_name() == params.model_name) {
            res.status = 200;
            nlohmann::json response = {
                {"success",       true},
                {"status",        "loaded"},
                {"model_name",    model_manager_.get_loaded_model_name()},
                {"model_type",    model_type_to_string(params.model_type)},
                {"architecture",  model_manager_.get_loaded_model_architecture()}
            };
            res.set_header("Content-Type", "application/json");
            res.set_content(response.dump(), "application/json");
        } else {
            // Failure path. Echo the captured error from ModelManager
            // (CUDA OOM, weights mismatch, file not found, etc.).
            std::string err = model_manager_.get_last_load_error();
            if (err.empty()) err = "Model load failed (no error message captured)";
            res.status = 500;
            nlohmann::json response = {
                {"success",    false},
                {"status",     "failed"},
                {"error",      err},
                {"model_name", params.model_name}
            };
            res.set_header("Content-Type", "application/json");
            res.set_content(response.dump(), "application/json");
        }
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "[RequestHandlers] JSON parse error: " << e.what() << std::endl;
        send_error(res, std::string("Invalid JSON in request body: ") + e.what(), 400);
    } catch (const std::runtime_error& e) {
        std::cerr << "[RequestHandlers] Model load error: " << e.what() << std::endl;
        send_error(res, e.what(), 400);
    } catch (const std::exception& e) {
        std::cerr << "[RequestHandlers] Unexpected error: " << e.what() << std::endl;
        send_error(res, std::string("Internal error: ") + e.what(), 500);
    }
}

void RequestHandlers::handle_unload_model(const httplib::Request& /*req*/, httplib::Response& res) {
    model_manager_.unload_model();
    send_json(res, {
        {"success", true},
        {"message", "Model unloaded"}
    });
}

void RequestHandlers::handle_get_model_hash(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string model_type = req.matches[1];
        std::string model_name = req.matches[2];
        
        ModelType type = string_to_model_type(model_type);
        std::string hash = model_manager_.compute_model_hash(model_name, type);
        
        send_json(res, {
            {"model_name", model_name},
            {"model_type", model_type},
            {"hash", hash}
        });
    } catch (const std::exception& e) {
        send_error(res, e.what(), 404);
    }
}

void RequestHandlers::handle_txt2img(const httplib::Request& req, httplib::Response& res) {
    submit_generation_jobs(req, res, static_cast<int>(GenerationType::Text2Image));
}

void RequestHandlers::handle_img2img(const httplib::Request& req, httplib::Response& res) {
    submit_generation_jobs(req, res, static_cast<int>(GenerationType::Image2Image));
}

void RequestHandlers::handle_txt2vid(const httplib::Request& req, httplib::Response& res) {
    submit_generation_jobs(req, res, static_cast<int>(GenerationType::Text2Video));
}

namespace {
// Maximum prompts a single expand_prompt request may produce. Exists to
// stop a runaway template ("{a|b|c|...} {d|e|...}") from filling the queue
// with thousands of jobs by accident. The frontend's count preview catches
// these earlier; this is the server-side backstop.
constexpr size_t MAX_PROMPT_VARIATIONS = 200;

// Normalize a generation request body so the queued job has clean JSON
// types. Naive HTTP clients (shell scripts, form-encoded wrappers) often
// send numbers and booleans as strings — `"steps": "9"`, `"easycache":
// "false"`. The typed param structs' from_json methods already coerce
// permissively (parse_int/parse_float/parse_bool helpers), but the worker
// then writes the raw body straight back to the queue's stored params,
// where strings linger and confuse downstream consumers (tooltips, MCP
// readers, restart-from-job flows). Doing the typed roundtrip here at
// the API boundary means:
//   - Bad input (a non-numeric "steps": "abc") fails fast as 400 instead
//     of crashing the worker after queue insertion.
//   - The stored params are clean numerics, so /queue/<id>, MCP, and
//     reload-from-job all see the same type-correct values.
//   - The worker still uses tolerant parsing as defense-in-depth (e.g.
//     for jobs persisted before this normalization existed).
//
// Throws std::runtime_error with the parse helper's friendly message when
// a field can't be coerced. Caller turns that into 400.
nlohmann::json normalize_generation_body(GenerationType type,
                                         const nlohmann::json& body) {
    nlohmann::json normalized;
    switch (type) {
        case GenerationType::Text2Image:
            normalized = Txt2ImgParams::from_json(body).to_json();
            break;
        case GenerationType::Image2Image:
            normalized = Img2ImgParams::from_json(body).to_json();
            break;
        case GenerationType::Text2Video:
            normalized = Txt2VidParams::from_json(body).to_json();
            break;
        default:
            // Other types (upscale, convert, ...) don't go through this
            // path — they have their own handlers. Pass through untouched.
            return body;
    }
    // Preserve fields the typed struct doesn't enumerate:
    //   - Image payloads (init_image_base64, mask_image_base64,
    //     control_image_base64, ref_images[], pm_id_images[])
    //   - Variation metadata stamped by the expand-prompt path
    //     (variation_group_id, variation_index, variation_total,
    //     variation_template) — these are added by the caller AFTER
    //     normalization, but pre-existing keys on a re-submission also
    //     survive.
    //   - Anything we haven't explicitly modeled yet.
    if (body.is_object()) {
        for (auto it = body.begin(); it != body.end(); ++it) {
            if (!normalized.contains(it.key())) {
                normalized[it.key()] = it.value();
            }
        }
    }
    return normalized;
}
} // anonymous namespace

void RequestHandlers::submit_generation_jobs(const httplib::Request& req,
                                              httplib::Response& res,
                                              int generation_type_int) {
    try {
        if (!model_manager_.is_model_loaded()) {
            send_error(res, "No model loaded", 400);
            return;
        }

        auto body = parse_json_body(req);
        const auto type = static_cast<GenerationType>(generation_type_int);

        // Decide whether this is a template-expansion submission, then
        // strip the helper flag before normalization. expand_prompt isn't a
        // generation param (the typed struct doesn't model it) and the
        // strict validator inside normalize_generation_body() would reject
        // it as an unknown field. Stripping here also keeps it out of the
        // per-variation params persisted on each queued job.
        bool expand = body.value("expand_prompt", false);
        body.erase("expand_prompt");

        // Optional user-supplied display title. Stored on the QueueItem
        // (not on params) so it never reaches the typed generation
        // structs — strip before strict validation.
        std::string title;
        if (body.contains("title") && body["title"].is_string()) {
            title = body["title"].get<std::string>();
        }
        body.erase("title");

        // Type-coerce + validate the request body at the API boundary so
        // bad input (e.g. "steps": "abc") fails fast as a 400 rather than
        // crashing the worker, and unknown fields ("diffusion_fa") are
        // surfaced as 400 instead of being silently dropped. See the
        // normalize_generation_body() comment + Txt2ImgParams::from_json's
        // KNOWN set for the accepted shape.
        try {
            body = normalize_generation_body(type, body);
        } catch (const std::exception& e) {
            send_error(res, std::string("Invalid request body: ") + e.what(), 400);
            return;
        }

        std::string prompt;
        if (body.contains("prompt") && body["prompt"].is_string()) {
            prompt = body["prompt"].get<std::string>();
        }

        // Fast path: no expansion requested, or no template syntax present.
        // Behaves identically to the previous direct add_job() flow.
        if (!expand || prompt.find('{') == std::string::npos) {
            std::string job_id = queue_manager_.add_job(type, body, title);
            auto status = queue_manager_.get_status();
            send_json(res, {
                {"job_id", job_id},
                {"status", "pending"},
                {"position", status["pending_count"]}
            }, 202);
            return;
        }

        // Expansion path. The parser throws std::runtime_error on malformed
        // input (unterminated brace, pick-N > options, etc.) — bubble that up
        // as a 400 with the parser's message.
        std::vector<std::string> variations;
        try {
            variations = expand_prompt_template(prompt);
        } catch (const std::exception& e) {
            send_error(res, std::string("Prompt template error: ") + e.what(), 400);
            return;
        }
        if (variations.empty()) {
            send_error(res, "Prompt template expanded to 0 variations", 400);
            return;
        }
        if (variations.size() > MAX_PROMPT_VARIATIONS) {
            send_error(res,
                "Prompt template would create " + std::to_string(variations.size())
                + " jobs, exceeds limit of " + std::to_string(MAX_PROMPT_VARIATIONS)
                + ". Reduce the number of choices in the template.", 400);
            return;
        }

        // Generate the shared group_id and create N jobs. Each job carries
        // the original templated prompt under `variation_template` so the UI
        // can display it as the group header.
        std::string group_id = utils::generate_uuid();
        std::vector<std::string> job_ids;
        job_ids.reserve(variations.size());
        for (size_t i = 0; i < variations.size(); ++i) {
            nlohmann::json job_params = body;
            job_params["prompt"] = variations[i];
            job_params["variation_group_id"] = group_id;
            job_params["variation_index"] = static_cast<int>(i);
            job_params["variation_total"] = static_cast<int>(variations.size());
            job_params["variation_template"] = prompt;
            job_ids.push_back(queue_manager_.add_job(type, job_params, title));
        }

        auto status = queue_manager_.get_status();
        send_json(res, {
            {"group_id", group_id},
            {"variation_count", variations.size()},
            {"job_ids", job_ids},
            {"status", "pending"},
            {"position", status["pending_count"]}
        }, 202);
    } catch (const std::exception& e) {
        send_error(res, e.what(), 400);
    }
}

void RequestHandlers::handle_upscale(const httplib::Request& req, httplib::Response& res) {
    try {
        if (!model_manager_.is_upscaler_loaded()) {
            send_error(res, "No upscaler loaded. Load an ESRGAN model first using POST /upscaler/load", 400);
            return;
        }

        auto body = parse_json_body(req);

        // Optional user-supplied display title — strip before strict
        // validation since UpscaleParams doesn't model it.
        std::string title;
        if (body.contains("title") && body["title"].is_string()) {
            title = body["title"].get<std::string>();
        }
        body.erase("title");

        // Convenience: resolve `job_id` (+ optional `image_index`, defaults 0)
        // into the `image_base64` payload that UpscaleParams::from_json + the
        // worker expect. Previously this resolution was promised by inline
        // comments but never implemented — the body queued with job_id only,
        // and the worker failed at "No image provided for upscaling" because
        // the parser saw no image_base64. Mirrors the MCP path's resolution
        // in tool_executor.cpp (analyze_image), including the "/output/" strip
        // and absolute/relative-path normalization.
        if (body.contains("job_id") && body["job_id"].is_string()) {
            std::string src_job_id = body["job_id"].get<std::string>();
            int image_index = 0;
            if (body.contains("image_index") && body["image_index"].is_number_integer()) {
                image_index = body["image_index"].get<int>();
            }

            auto src_job = queue_manager_.get_job(src_job_id);
            if (!src_job) {
                send_error(res, "Source job not found: " + src_job_id, 404);
                return;
            }
            if (src_job->outputs.empty()) {
                send_error(res, "Source job has no output images", 400);
                return;
            }
            if (image_index < 0 ||
                static_cast<size_t>(image_index) >= src_job->outputs.size()) {
                send_error(res, "Invalid image_index. Source job has " +
                                std::to_string(src_job->outputs.size()) + " output(s).", 400);
                return;
            }

            // Outputs are stored as relative paths like "<group>/<job>/image.png".
            // Strip any leading "/output/" prefix (legacy serialization) and
            // resolve against output_dir_. Absolute paths pass through.
            std::string output_path = src_job->outputs[image_index];
            fs::path full_path;
            if (fs::path(output_path).is_absolute()) {
                full_path = output_path;
            } else {
                if (output_path.rfind("/output/", 0) == 0) {
                    output_path = output_path.substr(8);
                }
                full_path = fs::path(output_dir_) / output_path;
            }
            if (!fs::exists(full_path)) {
                send_error(res, "Source image file not found on disk: " + full_path.string(), 404);
                return;
            }

            // Read the raw file bytes and base64-encode them. UpscaleParams::
            // from_json's decode_base64_image path expects encoded file bytes
            // (matches what direct image_base64 callers send), not decoded
            // pixel data — so we just slurp + encode, no stbi round-trip.
            std::ifstream f(full_path, std::ios::binary);
            if (!f) {
                send_error(res, "Cannot open source image: " + full_path.string(), 500);
                return;
            }
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                       std::istreambuf_iterator<char>());

            body["image_base64"] = utils::base64_encode(bytes);
            body.erase("job_id");
            body.erase("image_index");
        }

        // Validate body shape at the boundary so unknown fields fail fast
        // as 400 (instead of being silently dropped on the way to the
        // worker). Discard the parsed result — we store the raw body on
        // the job and let the worker re-parse via UpscaleParams::from_json.
        try {
            (void) UpscaleParams::from_json(body);
        } catch (const std::exception& e) {
            send_error(res, std::string("Invalid request body: ") + e.what(), 400);
            return;
        }

        std::string job_id = queue_manager_.add_job(GenerationType::Upscale, body, title);
        
        auto status = queue_manager_.get_status();
        
        send_json(res, {
            {"job_id", job_id},
            {"status", "pending"},
            {"position", status["pending_count"]}
        }, 202);
    } catch (const std::exception& e) {
        send_error(res, e.what(), 400);
    }
}

void RequestHandlers::handle_convert(const httplib::Request& req, httplib::Response& res) {
    try {
        auto body = parse_json_body(req);

        // Optional user-supplied display title — strip before strict
        // validation since it isn't a convert parameter.
        std::string title;
        if (body.contains("title") && body["title"].is_string()) {
            title = body["title"].get<std::string>();
        }
        body.erase("title");

        // Strict body validation: reject unknown fields.
        static const std::unordered_set<std::string> KNOWN_CONVERT_KEYS = {
            "input_path", "output_path", "output_type",
            "model_type", "model_name",     // model_name = alias for input_path resolution
            "vae_path",                       // optional VAE to bake into the converted file
            "tensor_type_rules",              // per-tensor weight rules string
        };
        std::vector<std::string> unknown;
        for (auto it = body.begin(); it != body.end(); ++it) {
            if (KNOWN_CONVERT_KEYS.find(it.key()) == KNOWN_CONVERT_KEYS.end()) {
                unknown.push_back(it.key());
            }
        }
        if (!unknown.empty()) {
            std::string msg = "Unknown field(s) in /convert body: ";
            for (size_t i = 0; i < unknown.size(); ++i) {
                if (i) msg += ", ";
                msg += unknown[i];
            }
            msg += ". Accepted: input_path, output_path, output_type, model_type, model_name, vae_path, tensor_type_rules.";
            send_error(res, msg, 400);
            return;
        }

        // Validate required parameters
        if (!body.contains("input_path") || body["input_path"].get<std::string>().empty()) {
            send_error(res, "input_path is required", 400);
            return;
        }
        if (!body.contains("output_type") || body["output_type"].get<std::string>().empty()) {
            send_error(res, "output_type (quantization type) is required", 400);
            return;
        }

        // Get paths
        std::string input_path = body["input_path"].get<std::string>();
        std::string output_type = body["output_type"].get<std::string>();

        // If model_type is provided, resolve the model name to full path
        if (body.contains("model_type") && !body["model_type"].get<std::string>().empty()) {
            std::string model_type_str = body["model_type"].get<std::string>();
            ModelType model_type = string_to_model_type(model_type_str);

            auto model_info = model_manager_.get_model(input_path, model_type);
            if (!model_info) {
                send_error(res, "Model not found: '" + input_path + "' of type '" + model_type_str + "'", 404);
                return;
            }
            input_path = model_info->full_path;
            // Update body with resolved path for queue job
            body["input_path"] = input_path;
        }

        // Check if the model is an LLM - LLM conversion is not supported by sd.cpp
        // LLM re-quantization requires llama.cpp's llama-quantize tool instead
        auto paths_config = model_manager_.get_paths_config();
        if (paths_config.contains("llm") && !paths_config["llm"].is_null()) {
            std::string llm_base_path = paths_config["llm"].get<std::string>();
            if (!llm_base_path.empty()) {
                fs::path input_fs(input_path);
                fs::path llm_fs(llm_base_path);
                // Check if input path is under LLM directory
                auto input_canonical = fs::weakly_canonical(input_fs);
                auto llm_canonical = fs::weakly_canonical(llm_fs);
                std::string input_str = input_canonical.string();
                std::string llm_str = llm_canonical.string();
                if (input_str.find(llm_str) == 0) {
                    send_error(res, "LLM model conversion is not supported. The sd.cpp convert function only works with Stable Diffusion models. To re-quantize LLM models, use llama.cpp's llama-quantize tool instead.", 400);
                    return;
                }
            }
        }

        // Generate output path if not provided
        // Default naming: modelname.quanttype.gguf
        std::string output_path;
        if (body.contains("output_path") && !body["output_path"].get<std::string>().empty()) {
            output_path = body["output_path"].get<std::string>();
            // Coerce bare filenames (no directory) into the input's parent dir.
            // The WebUI used to send these from generateOutputPath() when the
            // model was passed by name only; we now treat them as relative-
            // to-source so the converted file lands next to the original.
            fs::path output_p(output_path);
            if (!output_p.has_parent_path()) {
                output_path = (fs::path(input_path).parent_path() / output_p).string();
            }
        } else {
            // Generate default output path
            fs::path input_p(input_path);
            std::string stem = input_p.stem().string();
            // Remove any existing quantization suffix from stem (e.g., .q8_0)
            size_t quant_pos = stem.rfind('.');
            if (quant_pos != std::string::npos) {
                std::string suffix = stem.substr(quant_pos + 1);
                // Check if suffix looks like a quant type
                if (suffix.size() >= 2 && (suffix[0] == 'q' || suffix[0] == 'Q' ||
                    suffix == "f32" || suffix == "f16" || suffix == "bf16" ||
                    suffix == "F32" || suffix == "F16" || suffix == "BF16")) {
                    stem = stem.substr(0, quant_pos);
                }
            }
            output_path = (input_p.parent_path() / (stem + "." + output_type + ".gguf")).string();
        }

        // Update body with output_path for the queue job
        body["output_path"] = output_path;

        std::string job_id = queue_manager_.add_job(GenerationType::Convert, body, title);

        auto status = queue_manager_.get_status();

        send_json(res, {
            {"job_id", job_id},
            {"status", "pending"},
            {"position", status["pending_count"]},
            {"output_path", output_path}
        }, 202);
    } catch (const nlohmann::json::exception& e) {
        send_error(res, std::string("Invalid JSON: ") + e.what(), 400);
    } catch (const std::exception& e) {
        send_error(res, e.what(), 400);
    }
}

void RequestHandlers::handle_load_upscaler(const httplib::Request& req, httplib::Response& res) {
    try {
        auto body = parse_json_body(req);
        
        std::string model_name = body.value("model_name", "");
        if (model_name.empty()) {
            send_error(res, "model_name is required", 400);
            return;
        }
        
        int n_threads = body.value("n_threads", -1);
        int tile_size = body.value("tile_size", 128);
        
        std::cout << "[RequestHandlers] Loading upscaler: " << model_name << std::endl;
        
        model_manager_.load_upscaler(model_name, n_threads, tile_size);
        
        send_json(res, {
            {"success", true},
            {"message", "Upscaler loaded successfully"},
            {"model_name", model_name},
            {"upscale_factor", model_manager_.get_upscale_factor()}
        });
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "[RequestHandlers] JSON parse error: " << e.what() << std::endl;
        send_error(res, std::string("Invalid JSON in request body: ") + e.what(), 400);
    } catch (const std::runtime_error& e) {
        std::cerr << "[RequestHandlers] Upscaler load error: " << e.what() << std::endl;
        send_error(res, e.what(), 400);
    } catch (const std::exception& e) {
        std::cerr << "[RequestHandlers] Unexpected error: " << e.what() << std::endl;
        send_error(res, std::string("Internal error: ") + e.what(), 500);
    }
}

void RequestHandlers::handle_unload_upscaler(const httplib::Request& /*req*/, httplib::Response& res) {
    model_manager_.unload_upscaler();
    send_json(res, {
        {"success", true},
        {"message", "Upscaler unloaded"}
    });
}

void RequestHandlers::handle_get_queue(const httplib::Request& req, httplib::Response& res) {
    // Strict query-param validation: reject anything not in the closed
    // allow-list with 400. Same UX rationale as the body validators.
    static const std::unordered_set<std::string> KNOWN_QUERY = {
        "status", "type", "search", "architecture", "group_by",
        "limit", "offset", "page", "before", "after",
    };
    std::vector<std::string> unknown_qp;
    for (const auto& kv : req.params) {
        if (KNOWN_QUERY.find(kv.first) == KNOWN_QUERY.end()) {
            unknown_qp.push_back(kv.first);
        }
    }
    if (!unknown_qp.empty()) {
        std::string msg = "Unknown query parameter(s): ";
        for (size_t i = 0; i < unknown_qp.size(); ++i) {
            if (i) msg += ", ";
            msg += unknown_qp[i];
        }
        msg += ". Accepted: status, type, search, architecture, group_by, limit, offset, page, before, after.";
        send_error(res, msg, 400);
        return;
    }

    // Parse filter parameters from query string
    QueueFilter filter;

    // Status filter
    if (req.has_param("status")) {
        std::string status_str = req.get_param_value("status");
        if (!status_str.empty() && status_str != "all") {
            filter.status = string_to_queue_status(status_str);
        }
    }

    // Type filter
    if (req.has_param("type")) {
        std::string type_str = req.get_param_value("type");
        if (!type_str.empty() && type_str != "all") {
            filter.type = string_to_generation_type(type_str);
        }
    }

    // Search filter
    if (req.has_param("search")) {
        std::string search_str = req.get_param_value("search");
        if (!search_str.empty()) {
            filter.search = search_str;
        }
    }

    // Architecture filter
    if (req.has_param("architecture")) {
        std::string arch_str = req.get_param_value("architecture");
        if (!arch_str.empty()) {
            filter.architecture = arch_str;
        }
    }

    // Check for group_by parameter
    std::string group_by;
    if (req.has_param("group_by")) {
        group_by = req.get_param_value("group_by");
    }

    // Pagination parameters.
    // The endpoint accepts either `page` (1-based) or `offset` (0-based item
    // index), not both. They're equivalent expressions of the same cursor —
    // accepting both in one request would silently let one override the other,
    // so we 400 instead of guessing intent.
    if (req.has_param("page") && req.has_param("offset")) {
        send_error(res, "Pass either 'page' or 'offset', not both.", 400);
        return;
    }

    size_t limit = 20;
    size_t page = 1;

    if (req.has_param("limit")) {
        try {
            limit = std::stoul(req.get_param_value("limit"));
        } catch (...) {
            limit = 20;
        }
        // Match how page=0 is clamped to 1. Without this, limit=0 would
        // make filter.offset arithmetic below a no-op and force the queue
        // manager to silently substitute its internal default (20), so the
        // echoed `limit` would disagree with what the caller asked for.
        if (limit < 1) limit = 1;
    }

    if (req.has_param("page")) {
        try {
            page = std::stoul(req.get_param_value("page"));
            if (page < 1) page = 1;
        } catch (...) {
            page = 1;
        }
    }

    if (req.has_param("offset")) {
        try {
            filter.offset = std::stoul(req.get_param_value("offset"));
        } catch (...) {
            filter.offset = 0;
        }
    } else if (req.has_param("page")) {
        // Derive offset from page so the non-grouped path actually paginates.
        // Without this, `?page=N` is parsed but never reaches the queue
        // manager, so every page returns offset=0 (the first page).
        filter.offset = (page - 1) * limit;
    }

    filter.limit = limit;

    // Date-based pagination
    if (req.has_param("before")) {
        try {
            filter.before_timestamp = std::stoll(req.get_param_value("before"));
        } catch (...) {}
    }

    if (req.has_param("after")) {
        try {
            filter.after_timestamp = std::stoll(req.get_param_value("after"));
        } catch (...) {}
    }

    // Build applied filters for response
    nlohmann::json applied_filters = nlohmann::json::object();
    if (filter.status.has_value()) {
        applied_filters["status"] = queue_status_to_string(filter.status.value());
    }
    if (filter.type.has_value()) {
        applied_filters["type"] = generation_type_to_string(filter.type.value());
    }
    if (filter.search.has_value()) {
        applied_filters["search"] = filter.search.value();
    }
    if (filter.architecture.has_value()) {
        applied_filters["architecture"] = filter.architecture.value();
    }

    auto status = queue_manager_.get_status();

    // Handle grouped response
    const std::string base_url = compute_base_url(req, trusted_proxies_);
    if (group_by == "date") {
        auto grouped_result = queue_manager_.get_jobs_grouped_by_date(filter, page, limit);

        nlohmann::json groups = nlohmann::json::array();
        for (const auto& group : grouped_result.groups) {
            nlohmann::json group_json;
            group_json["date"] = group.date;
            group_json["label"] = group.label;
            group_json["timestamp"] = group.timestamp;
            group_json["count"] = group.count;

            nlohmann::json items = nlohmann::json::array();
            for (const auto& job : group.items) {
                nlohmann::json job_json = job.to_json();
                inject_output_urls(job_json, base_url);
                items.push_back(std::move(job_json));
            }
            group_json["items"] = items;
            groups.push_back(group_json);
        }

        status["groups"] = groups;
        status["total_count"] = static_cast<int>(grouped_result.total_count);
        status["page"] = static_cast<int>(grouped_result.page);
        status["total_pages"] = static_cast<int>(grouped_result.total_pages);
        status["limit"] = static_cast<int>(grouped_result.limit);
        status["has_more"] = grouped_result.has_more;
        status["has_prev"] = grouped_result.has_prev;
        status["group_by"] = "date";

        if (!applied_filters.empty()) {
            status["applied_filters"] = applied_filters;
        }

        send_json(res, status);
        return;
    }

    // Standard paginated response
    auto page_result = queue_manager_.get_jobs_paginated(filter);
    nlohmann::json items = nlohmann::json::array();

    for (const auto& job : page_result.items) {
        nlohmann::json job_json = job.to_json();
        inject_output_urls(job_json, base_url);
        items.push_back(std::move(job_json));
    }

    status["items"] = items;
    status["total_count"] = static_cast<int>(page_result.total_count);
    status["filtered_count"] = static_cast<int>(page_result.filtered_count);
    status["offset"] = static_cast<int>(page_result.offset);
    status["limit"] = static_cast<int>(page_result.limit);
    status["has_more"] = page_result.has_more;

    // Calculate page info for numerical pagination
    size_t total_pages = (page_result.total_count + page_result.limit - 1) / page_result.limit;
    if (total_pages == 0) total_pages = 1;
    size_t current_page = (page_result.offset / page_result.limit) + 1;

    status["page"] = static_cast<int>(current_page);
    status["total_pages"] = static_cast<int>(total_pages);
    status["has_prev"] = current_page > 1;

    // Include timestamp bounds for cursor-based pagination
    if (page_result.newest_timestamp.has_value()) {
        status["newest_timestamp"] = page_result.newest_timestamp.value();
    }
    if (page_result.oldest_timestamp.has_value()) {
        status["oldest_timestamp"] = page_result.oldest_timestamp.value();
    }

    // Include applied filters in response
    if (!applied_filters.empty()) {
        status["applied_filters"] = applied_filters;
    }

    send_json(res, status);
}

void RequestHandlers::handle_get_job(const httplib::Request& req, httplib::Response& res) {
    std::string job_id = req.matches[1];

    auto job = queue_manager_.get_job(job_id);
    if (!job) {
        send_error(res, "Job not found", 404);
        return;
    }

    nlohmann::json body = job->to_json();
    inject_output_urls(body, compute_base_url(req, trusted_proxies_));
    send_json(res, body);
}

void RequestHandlers::handle_get_job_preview(const httplib::Request& req, httplib::Response& res) {
    std::string job_id = req.matches[1];

    auto preview = queue_manager_.get_preview(job_id);
    if (!preview) {
        send_error(res, "No preview available", 404);
        return;
    }

    // Set headers for preview image
    res.set_header("Cache-Control", "no-cache");
    res.set_header("X-Preview-Width", std::to_string(preview->width));
    res.set_header("X-Preview-Height", std::to_string(preview->height));
    res.set_header("X-Preview-Step", std::to_string(preview->step));

    // Serve JPEG directly from memory buffer
    res.set_content(reinterpret_cast<const char*>(preview->jpeg_data.data()),
                    preview->jpeg_data.size(), "image/jpeg");
}

void RequestHandlers::handle_cancel_job(const httplib::Request& req, httplib::Response& res) {
    std::string job_id = req.matches[1];
    
    if (queue_manager_.cancel_job(job_id)) {
        send_json(res, {
            {"success", true},
            {"message", "Job cancelled"}
        });
    } else {
        send_error(res, "Cannot cancel job (not found or already processing)", 400);
    }
}

void RequestHandlers::handle_delete_jobs(const httplib::Request& req, httplib::Response& res) {
    try {
        nlohmann::json body = parse_json_body(req);
        
        if (!body.contains("job_ids") || !body["job_ids"].is_array()) {
            send_error(res, "job_ids array is required in request body", 400);
            return;
        }
        
        auto job_ids = body["job_ids"].get<std::vector<std::string>>();
        
        if (job_ids.empty()) {
            send_error(res, "job_ids array cannot be empty", 400);
            return;
        }
        
        int deleted = 0;
        int failed = 0;
        std::vector<std::string> failed_job_ids;
        
        for (const auto& job_id : job_ids) {
            if (queue_manager_.delete_job(job_id)) {
                deleted++;
            } else {
                failed++;
                failed_job_ids.push_back(job_id);
            }
        }
        
        nlohmann::json response = {
            {"success", true},
            {"deleted", deleted},
            {"failed", failed},
            {"total", static_cast<int>(job_ids.size())}
        };
        
        if (!failed_job_ids.empty()) {
            response["failed_job_ids"] = failed_job_ids;
        }
        
        send_json(res, response);
        
    } catch (const std::exception& e) {
        send_error(res, std::string("Failed to delete jobs: ") + e.what(), 500);
    }
}

void RequestHandlers::handle_get_recycle_bin(const httplib::Request& req, httplib::Response& res) {
    auto deleted_jobs = queue_manager_.get_deleted_jobs();

    const std::string base_url = compute_base_url(req, trusted_proxies_);
    nlohmann::json items = nlohmann::json::array();
    for (const auto& item : deleted_jobs) {
        nlohmann::json job_json = item.to_json();
        inject_output_urls(job_json, base_url);
        items.push_back(std::move(job_json));
    }

    send_json(res, {
        {"success", true},
        {"enabled", queue_manager_.is_recycle_bin_enabled()},
        {"retention_minutes", queue_manager_.get_recycle_bin_retention_minutes()},
        {"count", deleted_jobs.size()},
        {"items", items}
    });
}

void RequestHandlers::handle_restore_job(const httplib::Request& req, httplib::Response& res) {
    std::string job_id = req.matches[1];

    if (queue_manager_.restore_job(job_id)) {
        send_json(res, {
            {"success", true},
            {"message", "Job restored from recycle bin"}
        });
    } else {
        send_error(res, "Cannot restore job (not found or not in recycle bin)", 400);
    }
}

void RequestHandlers::handle_purge_job(const httplib::Request& req, httplib::Response& res) {
    std::string job_id = req.matches[1];

    if (queue_manager_.purge_job(job_id)) {
        send_json(res, {
            {"success", true},
            {"message", "Job permanently deleted"}
        });
    } else {
        send_error(res, "Cannot purge job (not found or still processing)", 400);
    }
}

void RequestHandlers::handle_clear_recycle_bin(const httplib::Request& /*req*/, httplib::Response& res) {
    int purged = queue_manager_.clear_recycle_bin();

    send_json(res, {
        {"success", true},
        {"purged", purged},
        {"message", "Recycle bin cleared"}
    });
}

void RequestHandlers::handle_get_output_settings(const httplib::Request& /*req*/, httplib::Response& res) {
    send_json(res, {
        {"output_group_folders", queue_manager_.get_group_folders_enabled()}
    }, 200);
}

void RequestHandlers::handle_set_output_settings(const httplib::Request& req, httplib::Response& res) {
    try {
        auto body = parse_json_body(req);
        // Strict body validation: only output_group_folders is accepted.
        for (auto it = body.begin(); it != body.end(); ++it) {
            if (it.key() != "output_group_folders") {
                send_error(res,
                    "Unknown field(s) in /settings/output body: " + it.key()
                    + ". Accepted: output_group_folders.", 400);
                return;
            }
        }
        if (body.contains("output_group_folders") && body["output_group_folders"].is_boolean()) {
            queue_manager_.set_group_folders_enabled(body["output_group_folders"].get<bool>());
        }
        send_json(res, {
            {"output_group_folders", queue_manager_.get_group_folders_enabled()}
        }, 200);
    } catch (const std::exception& e) {
        send_error(res, e.what(), 400);
    }
}

void RequestHandlers::handle_get_recycle_bin_settings(const httplib::Request& /*req*/, httplib::Response& res) {
    send_json(res, {
        {"enabled", queue_manager_.is_recycle_bin_enabled()},
        {"retention_minutes", queue_manager_.get_recycle_bin_retention_minutes()}
    });
}

void RequestHandlers::send_json(httplib::Response& res, const nlohmann::json& json, int status) {
    res.status = status;
    res.set_content(json.dump(), "application/json");
}

void RequestHandlers::send_error(httplib::Response& res, const std::string& message, int status) {
    res.status = status;
    nlohmann::json error = {{"error", message}};
    res.set_content(error.dump(), "application/json");
}

nlohmann::json RequestHandlers::parse_json_body(const httplib::Request& req) {
    if (req.body.empty()) {
        return nlohmann::json::object();
    }
    return nlohmann::json::parse(req.body);
}

std::string RequestHandlers::get_mime_type(const std::string& filepath) {
    std::string ext = fs::path(filepath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    static const std::map<std::string, std::string> mime_types = {
        {".html", "text/html"},
        {".htm", "text/html"},
        {".css", "text/css"},
        {".js", "application/javascript"},
        {".json", "application/json"},
        {".png", "image/png"},
        {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".gif", "image/gif"},
        {".webp", "image/webp"},
        {".svg", "image/svg+xml"},
        {".ico", "image/x-icon"},
        {".txt", "text/plain"},
        {".mp4", "video/mp4"},
        {".webm", "video/webm"},
        {".mp3", "audio/mpeg"},
        {".wav", "audio/wav"},
        {".pdf", "application/pdf"},
        {".zip", "application/zip"}
    };

    auto it = mime_types.find(ext);
    if (it != mime_types.end()) {
        return it->second;
    }
    return "application/octet-stream";
}

std::string RequestHandlers::format_file_size(size_t size) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit_index = 0;
    double display_size = static_cast<double>(size);

    while (display_size >= 1024.0 && unit_index < 4) {
        display_size /= 1024.0;
        unit_index++;
    }

    std::ostringstream oss;
    if (unit_index == 0) {
        oss << size << " " << units[unit_index];
    } else {
        oss << std::fixed << std::setprecision(1) << display_size << " " << units[unit_index];
    }
    return oss.str();
}

size_t RequestHandlers::calculate_directory_size(const std::string& path) {
    size_t total = 0;
    try {
        for (const auto& entry : fs::recursive_directory_iterator(path, fs::directory_options::skip_permission_denied)) {
            if (entry.is_regular_file()) {
                total += entry.file_size();
            }
        }
    } catch (...) {
        // Ignore errors (permission denied, etc.)
    }
    return total;
}

// Get the content-based modification time for a directory (excluding .thumbs)
// Returns the most recent modification time of files in the directory
std::time_t RequestHandlers::get_directory_content_mtime(const std::string& path) {
    std::time_t latest = 0;
    try {
        for (const auto& entry : fs::directory_iterator(path, fs::directory_options::skip_permission_denied)) {
            // Skip .thumbs directory
            if (entry.is_directory() && entry.path().filename() == ".thumbs") {
                continue;
            }

            auto ftime = entry.last_write_time();
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
            std::time_t mtime = std::chrono::system_clock::to_time_t(sctp);

            if (mtime > latest) {
                latest = mtime;
            }
        }
    } catch (...) {
        // Ignore errors
    }

    // If no files found, fallback to directory's own mtime
    if (latest == 0) {
        try {
            auto ftime = fs::last_write_time(path);
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
            latest = std::chrono::system_clock::to_time_t(sctp);
        } catch (...) {}
    }

    return latest;
}

int RequestHandlers::count_directory_files(const std::string& path) {
    int count = 0;
    try {
        for (const auto& entry : fs::directory_iterator(path, fs::directory_options::skip_permission_denied)) {
            if (entry.is_regular_file()) count++;
        }
    } catch (...) {}
    return count;
}

bool RequestHandlers::is_image_file(const std::string& path) {
    std::string ext = fs::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".gif" || ext == ".webp" || ext == ".bmp";
}

bool RequestHandlers::is_video_file(const std::string& path) {
    std::string ext = fs::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".mp4" || ext == ".webm" || ext == ".avi" || ext == ".mov" || ext == ".mkv";
}

std::string RequestHandlers::get_thumbnail_path(const std::string& source_path) {
    // Create .thumbs directory in the same folder as the source
    fs::path source(source_path);
    fs::path thumb_dir = source.parent_path() / ".thumbs";
    fs::path thumb_file = thumb_dir / (source.stem().string() + ".jpg");
    return thumb_file.string();
}

bool RequestHandlers::generate_thumbnail(const std::string& source_path, const std::string& thumb_path, int target_size) {
    // Load source image
    int width, height, channels;
    unsigned char* data = stbi_load(source_path.c_str(), &width, &height, &channels, 3);
    if (!data) {
        return false;
    }

    // Calculate crop and resize dimensions (square center crop, then resize)
    int crop_size = std::min(width, height);
    int crop_x = (width - crop_size) / 2;
    int crop_y = (height - crop_size) / 2;

    // Allocate thumbnail buffer
    std::vector<unsigned char> thumb_data(target_size * target_size * 3);

    // Simple bilinear resize with center crop
    float scale = static_cast<float>(crop_size) / target_size;
    for (int ty = 0; ty < target_size; ty++) {
        for (int tx = 0; tx < target_size; tx++) {
            float sx = crop_x + tx * scale;
            float sy = crop_y + ty * scale;

            int x0 = static_cast<int>(sx);
            int y0 = static_cast<int>(sy);
            int x1 = std::min(x0 + 1, width - 1);
            int y1 = std::min(y0 + 1, height - 1);

            float fx = sx - x0;
            float fy = sy - y0;

            for (int c = 0; c < 3; c++) {
                float v00 = data[(y0 * width + x0) * 3 + c];
                float v10 = data[(y0 * width + x1) * 3 + c];
                float v01 = data[(y1 * width + x0) * 3 + c];
                float v11 = data[(y1 * width + x1) * 3 + c];

                float v = v00 * (1 - fx) * (1 - fy) +
                          v10 * fx * (1 - fy) +
                          v01 * (1 - fx) * fy +
                          v11 * fx * fy;

                thumb_data[(ty * target_size + tx) * 3 + c] = static_cast<unsigned char>(v);
            }
        }
    }

    stbi_image_free(data);

    // Ensure thumbnail directory exists
    fs::create_directories(fs::path(thumb_path).parent_path());

    // Save as JPEG (quality 85)
    return stbi_write_jpg(thumb_path.c_str(), target_size, target_size, 3, thumb_data.data(), 85) != 0;
}

void RequestHandlers::handle_thumbnail(const httplib::Request& req, httplib::Response& res) {
    // Extract path after /thumb/
    std::string rel_path;
    if (req.matches.size() > 1) {
        rel_path = req.matches[1].str();
    }

    // URL decode
    std::string decoded_path;
    for (size_t i = 0; i < rel_path.size(); ++i) {
        if (rel_path[i] == '%' && i + 2 < rel_path.size()) {
            int hex_val;
            std::istringstream iss(rel_path.substr(i + 1, 2));
            if (iss >> std::hex >> hex_val) {
                decoded_path += static_cast<char>(hex_val);
                i += 2;
                continue;
            }
        }
        decoded_path += rel_path[i];
    }
    rel_path = decoded_path;

    // Security: prevent path traversal
    if (rel_path.find("..") != std::string::npos) {
        send_error(res, "Invalid path", 400);
        return;
    }

    // Build full path
    fs::path source_path = fs::path(output_dir_) / rel_path;

    if (!fs::exists(source_path) || !fs::is_regular_file(source_path)) {
        send_error(res, "Not found", 404);
        return;
    }

    // Check if it's an image or video
    bool is_img = is_image_file(source_path.string());
    bool is_vid = is_video_file(source_path.string());

    if (!is_img && !is_vid) {
        send_error(res, "Not a media file", 400);
        return;
    }

    // For videos, return a placeholder SVG
    if (is_vid) {
        std::string svg = R"(<svg xmlns="http://www.w3.org/2000/svg" width="120" height="120" viewBox="0 0 120 120">
            <rect width="120" height="120" fill="#1a1a2e"/>
            <circle cx="60" cy="60" r="30" fill="none" stroke="#c792ea" stroke-width="3"/>
            <polygon points="52,45 52,75 78,60" fill="#c792ea"/>
        </svg>)";
        res.set_content(svg, "image/svg+xml");
        return;
    }

    // For images, generate/serve thumbnail
    std::string thumb_path = get_thumbnail_path(source_path.string());

    // Check if thumbnail exists and is newer than source
    bool need_generate = true;
    if (fs::exists(thumb_path)) {
        auto source_time = fs::last_write_time(source_path);
        auto thumb_time = fs::last_write_time(thumb_path);
        need_generate = (source_time > thumb_time);
    }

    if (need_generate) {
        if (!generate_thumbnail(source_path.string(), thumb_path, 120)) {
            // Failed to generate, return placeholder
            std::string svg = R"(<svg xmlns="http://www.w3.org/2000/svg" width="120" height="120" viewBox="0 0 120 120">
                <rect width="120" height="120" fill="#1a1a2e"/>
                <text x="60" y="65" text-anchor="middle" fill="#ff6b9d" font-size="40">🖼️</text>
            </svg>)";
            res.set_content(svg, "image/svg+xml");
            return;
        }
    }

    // Serve the thumbnail via the cached streaming helper (same
    // ETag / Last-Modified / 304 / chunked-streaming pipeline as
    // /output, so the gallery doesn't refetch thumbnails on each
    // repaint).
    serve_file_cached(req, res, fs::path(thumb_path), "image/jpeg");
}

std::string RequestHandlers::generate_directory_html(const std::string& dir_path, const std::string& url_path,
                                                     const std::string& sort_by, bool sort_asc,
                                                     int page, int per_page) {
    std::ostringstream html;

    // Build sort URL helper (preserves pagination)
    auto make_sort_url = [&](const std::string& col) -> std::string {
        std::string new_order = (sort_by == col && sort_asc) ? "desc" : "asc";
        return url_path + "?sort=" + col + "&order=" + new_order + "&per_page=" + std::to_string(per_page);
    };

    // Sort indicator
    auto sort_indicator = [&](const std::string& col) -> std::string {
        if (sort_by != col) return "";
        return sort_asc ? " ↑" : " ↓";
    };

    // Current sort state for JavaScript
    std::string sort_order_str = sort_asc ? "asc" : "desc";

    // Start HTML with enhanced mobile-friendly CSS and thumbnails
    html << R"(<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
    <title>Index of )" << url_path << R"(</title>
    <style>
        * { box-sizing: border-box; margin: 0; padding: 0; }
        html { font-size: 16px; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif;
            background: linear-gradient(135deg, #1a1a2e 0%, #16213e 100%);
            color: #eee;
            min-height: 100vh;
            padding: 16px;
            padding-bottom: env(safe-area-inset-bottom, 16px);
        }
        .container { max-width: 1400px; margin: 0 auto; }
        h1 {
            color: #00d9ff;
            font-size: 1.25rem;
            margin-bottom: 16px;
            padding: 12px 16px;
            background: rgba(0,217,255,0.1);
            border-radius: 8px;
            word-break: break-all;
        }
        .stats {
            display: flex;
            gap: 16px;
            margin-bottom: 16px;
            flex-wrap: wrap;
        }
        .stat {
            background: #16213e;
            padding: 10px 16px;
            border-radius: 8px;
            font-size: 0.85rem;
        }
        .stat-value { color: #00d9ff; font-weight: 600; }
        table {
            width: 100%;
            border-collapse: collapse;
            background: #16213e;
            border-radius: 12px;
            overflow: hidden;
            box-shadow: 0 4px 24px rgba(0,0,0,0.3);
        }
        th, td {
            padding: 12px 16px;
            text-align: left;
            border-bottom: 1px solid #0f3460;
            vertical-align: middle;
        }
        th {
            background: #0f3460;
            color: #00d9ff;
            font-weight: 600;
            cursor: pointer;
            user-select: none;
            white-space: nowrap;
            position: sticky;
            top: 0;
            z-index: 10;
        }
        th:hover { background: #1a3a6e; }
        th a { color: #00d9ff; text-decoration: none; display: block; }
        th a:hover { color: #fff; }
        tbody tr { transition: background 0.15s; }
        tbody tr:hover { background: #1a1a4e; }
        tbody tr:last-child td { border-bottom: none; }
        tbody tr:active { background: #2a2a5e; }
        td a { color: #00d9ff; text-decoration: none; word-break: break-word; }
        td a:hover { text-decoration: underline; }
        .icon { margin-right: 8px; font-size: 1.1em; }
        .dir { color: #ffd700; }
        .img { color: #ff6b9d; }
        .vid { color: #c792ea; }
        .json { color: #98c379; }
        .size { color: #aaa; white-space: nowrap; }
        .date { color: #888; white-space: nowrap; }
        .name-cell { display: flex; align-items: center; gap: 12px; }
        .parent-row { background: rgba(255,215,0,0.05); }
        .parent-row:hover { background: rgba(255,215,0,0.1); }

        /* Thumbnail styles */
        .thumb {
            width: 60px;
            height: 60px;
            border-radius: 6px;
            object-fit: cover;
            background: #0f3460;
            flex-shrink: 0;
            cursor: pointer;
            transition: transform 0.2s, box-shadow 0.2s;
        }
        .thumb:hover {
            transform: scale(1.05);
            box-shadow: 0 4px 12px rgba(0,217,255,0.3);
        }
        .thumb-placeholder {
            width: 60px;
            height: 60px;
            border-radius: 6px;
            background: #0f3460;
            display: flex;
            align-items: center;
            justify-content: center;
            flex-shrink: 0;
            font-size: 1.5em;
        }
        .file-info {
            display: flex;
            flex-direction: column;
            min-width: 0;
        }
        .file-name {
            display: flex;
            align-items: center;
        }

        /* Lightbox */
        .lightbox {
            display: none;
            position: fixed;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
            background: rgba(0,0,0,0.95);
            z-index: 1000;
            align-items: center;
            justify-content: center;
            cursor: zoom-out;
        }
        .lightbox.active { display: flex; }
        .lightbox img, .lightbox video {
            max-width: calc(100vw - 80px);
            max-height: calc(100vh - 80px);
            width: auto;
            height: auto;
            object-fit: contain;
            border-radius: 8px;
        }
        .lightbox-close {
            position: absolute;
            top: 20px;
            right: 20px;
            color: #fff;
            font-size: 2rem;
            cursor: pointer;
            background: rgba(0,0,0,0.5);
            width: 40px;
            height: 40px;
            border-radius: 50%;
            display: flex;
            align-items: center;
            justify-content: center;
        }

        /* Pagination */
        .pagination {
            display: flex;
            align-items: center;
            justify-content: center;
            gap: 6px;
            margin-top: 16px;
            padding: 12px;
            flex-wrap: wrap;
        }
        .page-btn {
            display: inline-flex;
            align-items: center;
            justify-content: center;
            min-width: 36px;
            height: 36px;
            padding: 0 10px;
            border-radius: 6px;
            background: #16213e;
            color: #00d9ff;
            text-decoration: none;
            font-size: 0.85rem;
            transition: background 0.15s;
            cursor: pointer;
        }
        .page-btn:hover:not(.disabled):not(.active) { background: #1a3a6e; }
        .page-btn.active {
            background: #00d9ff;
            color: #1a1a2e;
            font-weight: 600;
            cursor: default;
        }
        .page-btn.disabled { color: #555; cursor: default; }
        .page-ellipsis { color: #555; padding: 0 4px; }

        /* Mobile styles */
        @media (max-width: 768px) {
            body { padding: 12px; }
            h1 { font-size: 1.1rem; padding: 10px 12px; }
            th, td { padding: 10px 12px; font-size: 0.9rem; }
            .stats { gap: 8px; }
            .stat { padding: 8px 12px; font-size: 0.8rem; }
            .thumb, .thumb-placeholder { width: 50px; height: 50px; }
        }
        @media (max-width: 600px) {
            .date-col { display: none; }
            th, td { padding: 8px 10px; }
            .thumb, .thumb-placeholder { width: 44px; height: 44px; }
            .name-cell { gap: 10px; }
        }
        @media (max-width: 400px) {
            th, td { padding: 6px 8px; font-size: 0.85rem; }
            .size { font-size: 0.8rem; }
            .thumb, .thumb-placeholder { width: 40px; height: 40px; font-size: 1.2em; }
        }

        /* Touch-friendly tap targets */
        @media (hover: none) {
            tbody tr { min-height: 60px; }
        }
    </style>
</head>
<body>
<div class="container">
    <h1>)" << url_path << R"(</h1>
)";

    // Collect entries with lightweight metadata (no recursive dir scanning)
    struct Entry {
        std::string name;
        bool is_dir;
        bool is_media;
        bool is_image;
        bool is_video;
        size_t size;       // file size only; 0 for directories until page slice
        std::time_t mtime;
        int file_count;    // for directories: number of immediate files (set after page slice)
    };
    std::vector<Entry> entries;
    int dir_count = 0, file_count = 0;

    try {
        for (const auto& entry : fs::directory_iterator(dir_path)) {
            Entry e;
            e.name = entry.path().filename().string();
            e.file_count = 0;

            // Skip .thumbs directories
            if (e.name == ".thumbs") continue;

            e.is_dir = entry.is_directory();
            e.is_image = !e.is_dir && is_image_file(entry.path().string());
            e.is_video = !e.is_dir && is_video_file(entry.path().string());
            e.is_media = e.is_image || e.is_video;

            // Lightweight metadata: use directory's own mtime, skip recursive size calc
            auto ftime = entry.last_write_time();
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
            e.mtime = std::chrono::system_clock::to_time_t(sctp);

            if (e.is_dir) {
                e.size = 0;
                dir_count++;
            } else {
                e.size = entry.file_size();
                file_count++;
            }

            entries.push_back(e);
        }
    } catch (const std::exception& ex) {
        html << "<p style='color:#ff6b6b;'>Error reading directory: " << ex.what() << "</p>";
    }

    // Sort entries
    if (sort_by == "size") {
        std::sort(entries.begin(), entries.end(), [sort_asc](const Entry& a, const Entry& b) {
            return sort_asc ? (a.size < b.size) : (a.size > b.size);
        });
    } else if (sort_by == "date") {
        std::sort(entries.begin(), entries.end(), [sort_asc](const Entry& a, const Entry& b) {
            return sort_asc ? (a.mtime < b.mtime) : (a.mtime > b.mtime);
        });
    } else { // name (default)
        std::sort(entries.begin(), entries.end(), [sort_asc](const Entry& a, const Entry& b) {
            std::string al = a.name, bl = b.name;
            std::transform(al.begin(), al.end(), al.begin(), ::tolower);
            std::transform(bl.begin(), bl.end(), bl.begin(), ::tolower);
            return sort_asc ? (al < bl) : (al > bl);
        });
    }

    // Pagination
    int total_entries = static_cast<int>(entries.size());
    int total_pages = std::max(1, (total_entries + per_page - 1) / per_page);
    page = std::clamp(page, 1, total_pages);
    int start_idx = (page - 1) * per_page;
    int end_idx = std::min(start_idx + per_page, total_entries);

    // Only compute expensive dir metadata for visible page entries
    for (int i = start_idx; i < end_idx; i++) {
        if (entries[i].is_dir) {
            std::string full = dir_path + "/" + entries[i].name;
            entries[i].file_count = count_directory_files(full);
        }
    }

    // Stats bar with pagination info
    auto make_page_url = [&](int p) -> std::string {
        return url_path + "?sort=" + sort_by + "&order=" + sort_order_str
             + "&page=" + std::to_string(p) + "&per_page=" + std::to_string(per_page);
    };

    html << R"(    <div class="stats">
        <div class="stat"><span class="stat-value">)" << dir_count << R"(</span> folders</div>
        <div class="stat"><span class="stat-value">)" << file_count << R"(</span> files</div>
        <div class="stat">Showing <span class="stat-value">)" << (start_idx + 1) << R"(</span>–<span class="stat-value">)" << end_idx << R"(</span> of <span class="stat-value">)" << total_entries << R"(</span></div>
    </div>
)";

    // Table header with sort links
    html << "    <table>\n"
         << "        <thead>\n"
         << "            <tr>\n"
         << "                <th><a href=\"" << make_sort_url("name") << "\" onclick=\"saveSort('name')\">Name" << sort_indicator("name") << "</a></th>\n"
         << "                <th><a href=\"" << make_sort_url("size") << "\" onclick=\"saveSort('size')\">Size" << sort_indicator("size") << "</a></th>\n"
         << "                <th class=\"date-col\"><a href=\"" << make_sort_url("date") << "\" onclick=\"saveSort('date')\">Modified" << sort_indicator("date") << "</a></th>\n"
         << "            </tr>\n"
         << "        </thead>\n"
         << "        <tbody>\n";

    // Parent directory link (if not root)
    if (url_path != "/output" && url_path != "/output/") {
        fs::path parent_url = fs::path(url_path).parent_path();
        html << R"(            <tr class="parent-row">
                <td data-label="Name">
                    <div class="name-cell">
                        <div class="thumb-placeholder">📁</div>
                        <div class="file-info">
                            <div class="file-name"><a href=")" << parent_url.string() << R"(">..</a></div>
                        </div>
                    </div>
                </td>
                <td class="size" data-label="Size">—</td>
                <td class="date date-col" data-label="Modified">—</td>
            </tr>
)";
    }

    // Build relative path for thumbnails
    std::string rel_base = url_path;
    if (rel_base.substr(0, 7) == "/output") {
        rel_base = rel_base.substr(7);
    }
    if (!rel_base.empty() && rel_base[0] == '/') {
        rel_base = rel_base.substr(1);
    }

    // Generate rows (only for current page)
    for (int i = start_idx; i < end_idx; i++) {
        const auto& entry = entries[i];
        std::string entry_url = url_path;
        if (!entry_url.empty() && entry_url.back() != '/') entry_url += '/';
        entry_url += entry.name;

        // Build thumbnail URL
        std::string thumb_rel = rel_base;
        if (!thumb_rel.empty() && thumb_rel.back() != '/') thumb_rel += '/';
        thumb_rel += entry.name;

        // Determine icon and class
        std::string icon, css_class;
        std::string ext = fs::path(entry.name).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (entry.is_dir) {
            icon = "📁";
            css_class = "dir";
        } else if (entry.is_image) {
            icon = "🖼️";
            css_class = "img";
        } else if (entry.is_video) {
            icon = "🎬";
            css_class = "vid";
        } else if (ext == ".json") {
            icon = "📋";
            css_class = "json";
        } else {
            icon = "📄";
            css_class = "";
        }

        // Format date
        std::tm* tm = std::localtime(&entry.mtime);
        char date_buf[64];
        std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d %H:%M", tm);

        // Format size: for directories show file count, for files show size
        std::string size_str;
        if (entry.is_dir) {
            size_str = std::to_string(entry.file_count) + " file" + (entry.file_count != 1 ? "s" : "");
        } else {
            size_str = format_file_size(entry.size);
        }

        html << "            <tr>\n";
        html << "                <td data-label=\"Name\">\n";
        html << "                    <div class=\"name-cell\">\n";

        // Thumbnail or placeholder
        if (entry.is_image) {
            html << "                        <img class=\"thumb\" src=\"/thumb/" << thumb_rel
                 << "\" alt=\"\" loading=\"lazy\" onclick=\"openLightbox('" << entry_url << "', 'image')\">\n";
        } else if (entry.is_video) {
            html << "                        <img class=\"thumb\" src=\"/thumb/" << thumb_rel
                 << "\" alt=\"\" loading=\"lazy\" onclick=\"openLightbox('" << entry_url << "', 'video')\">\n";
        } else {
            html << "                        <div class=\"thumb-placeholder " << css_class << "\">" << icon << "</div>\n";
        }

        html << "                        <div class=\"file-info\">\n";
        html << "                            <div class=\"file-name\">";
        if (!entry.is_media) html << "<span class=\"icon " << css_class << "\">" << icon << "</span>";
        html << "<a href=\"" << entry_url << "\">" << entry.name;
        if (entry.is_dir) html << "/";
        html << "</a></div>\n";
        html << "                        </div>\n";
        html << "                    </div>\n";
        html << "                </td>\n";
        html << "                <td class=\"size\" data-label=\"Size\">" << size_str << "</td>\n";
        html << "                <td class=\"date date-col\" data-label=\"Modified\">" << date_buf << "</td>\n";
        html << "            </tr>\n";
    }

    html << R"HTML(        </tbody>
    </table>
)HTML";

    // Pagination controls
    if (total_pages > 1) {
        html << "    <div class=\"pagination\">\n";

        // Previous button
        if (page > 1) {
            html << "        <a class=\"page-btn\" href=\"" << make_page_url(page - 1) << "\">&#8592; Prev</a>\n";
        } else {
            html << "        <span class=\"page-btn disabled\">&#8592; Prev</span>\n";
        }

        // Page numbers with ellipsis
        int window = 2; // pages around current
        for (int p = 1; p <= total_pages; p++) {
            if (p == 1 || p == total_pages || (p >= page - window && p <= page + window)) {
                if (p == page) {
                    html << "        <span class=\"page-btn active\">" << p << "</span>\n";
                } else {
                    html << "        <a class=\"page-btn\" href=\"" << make_page_url(p) << "\">" << p << "</a>\n";
                }
            } else if (p == page - window - 1 || p == page + window + 1) {
                html << "        <span class=\"page-ellipsis\">...</span>\n";
            }
        }

        // Next button
        if (page < total_pages) {
            html << "        <a class=\"page-btn\" href=\"" << make_page_url(page + 1) << "\">Next &#8594;</a>\n";
        } else {
            html << "        <span class=\"page-btn disabled\">Next &#8594;</span>\n";
        }

        html << "    </div>\n";
    }

    html << R"HTML(</div>

<!-- Lightbox -->
<div class="lightbox" id="lightbox" onclick="closeLightbox(event)">
    <div class="lightbox-close" onclick="closeLightbox(event)">&times;</div>
    <div id="lightbox-content"></div>
</div>

<script>
// Save sort preference to localStorage
function saveSort(col) {
    const currentSort = ')HTML" << sort_by << R"HTML(';
    const currentOrder = ')HTML" << sort_order_str << R"HTML(';
    const newOrder = (col === currentSort && currentOrder === 'asc') ? 'desc' : 'asc';
    localStorage.setItem('sdcpp_sort', col);
    localStorage.setItem('sdcpp_order', newOrder);
}

// Check and apply saved sort preference on page load
(function() {
    const savedSort = localStorage.getItem('sdcpp_sort');
    const savedOrder = localStorage.getItem('sdcpp_order');
    const currentSort = ')HTML" << sort_by << R"HTML(';
    const currentOrder = ')HTML" << sort_order_str << R"HTML(';

    // Only redirect if we have saved preferences and they differ from current
    if (savedSort && (savedSort !== currentSort || savedOrder !== currentOrder)) {
        const url = new URL(window.location.href);
        const urlSort = url.searchParams.get('sort');
        const urlOrder = url.searchParams.get('order');

        // Only redirect if URL doesn't already have sort params (user manually clicked)
        if (!urlSort && !urlOrder) {
            url.searchParams.set('sort', savedSort);
            url.searchParams.set('order', savedOrder || 'asc');
            window.location.replace(url.toString());
        }
    }
})();

// Lightbox functions
function openLightbox(url, type) {
    const lightbox = document.getElementById('lightbox');
    const content = document.getElementById('lightbox-content');

    if (type === 'image') {
        content.innerHTML = '<img src="' + url + '" alt="">';
    } else if (type === 'video') {
        content.innerHTML = '<video src="' + url + '" controls autoplay></video>';
    }

    lightbox.classList.add('active');
    document.body.style.overflow = 'hidden';
}

function closeLightbox(event) {
    if (event.target.id === 'lightbox' || event.target.classList.contains('lightbox-close')) {
        const lightbox = document.getElementById('lightbox');
        const content = document.getElementById('lightbox-content');

        // Stop video if playing
        const video = content.querySelector('video');
        if (video) video.pause();

        lightbox.classList.remove('active');
        content.innerHTML = '';
        document.body.style.overflow = '';
    }
}

// Close lightbox on escape key
document.addEventListener('keydown', function(e) {
    if (e.key === 'Escape') {
        const lightbox = document.getElementById('lightbox');
        if (lightbox.classList.contains('active')) {
            closeLightbox({target: lightbox});
        }
    }
});
</script>
</body>
</html>
)HTML";

    return html.str();
}

void RequestHandlers::handle_file_browser(const httplib::Request& req, httplib::Response& res) {
    // Extract the path after /output
    std::string rel_path;
    if (req.matches.size() > 1) {
        rel_path = req.matches[1].str();
    }

    // URL decode the path
    std::string decoded_path;
    for (size_t i = 0; i < rel_path.size(); ++i) {
        if (rel_path[i] == '%' && i + 2 < rel_path.size()) {
            int hex_val;
            std::istringstream iss(rel_path.substr(i + 1, 2));
            if (iss >> std::hex >> hex_val) {
                decoded_path += static_cast<char>(hex_val);
                i += 2;
                continue;
            }
        }
        decoded_path += rel_path[i];
    }
    rel_path = decoded_path;

    // Security: prevent path traversal
    if (rel_path.find("..") != std::string::npos) {
        send_error(res, "Invalid path", 400);
        return;
    }

    // Build full filesystem path
    fs::path full_path = fs::path(output_dir_) / rel_path;

    // Check if path exists
    if (!fs::exists(full_path)) {
        std::cerr << "[FileServer] Path not found: " << full_path.string()
                  << " (output_dir=" << output_dir_ << ", rel_path=" << rel_path << ")" << std::endl;
        send_error(res, "Not found", 404);
        return;
    }

    // Build URL path for display
    std::string url_path = "/output";
    if (!rel_path.empty()) {
        url_path += "/" + rel_path;
    }

    if (fs::is_directory(full_path)) {
        // Parse sort parameters
        std::string sort_by = "name";
        bool sort_asc = true;

        if (req.has_param("sort")) {
            std::string s = req.get_param_value("sort");
            if (s == "size" || s == "date" || s == "name") {
                sort_by = s;
            }
        }
        if (req.has_param("order")) {
            sort_asc = (req.get_param_value("order") != "desc");
        }

        // Parse pagination parameters
        int page = 1;
        int per_page = 50;
        if (req.has_param("page")) {
            try { page = std::max(1, std::stoi(req.get_param_value("page"))); } catch (...) {}
        }
        if (req.has_param("per_page")) {
            try { per_page = std::clamp(std::stoi(req.get_param_value("per_page")), 10, 200); } catch (...) {}
        }

        // Directory: generate listing HTML
        std::string html = generate_directory_html(full_path.string(), url_path, sort_by, sort_asc, page, per_page);
        res.set_content(html, "text/html");
    } else {
        // File: stream via the cached helper. Adds Cache-Control / ETag /
        // Last-Modified, handles If-None-Match → 304, and streams the
        // body in 64 KB chunks (which also gives us Range: support).
        serve_file_cached(req, res, full_path, get_mime_type(full_path.string()));
    }
}

void RequestHandlers::handle_webui(const httplib::Request& req, httplib::Response& res) {
    if (webui_dir_.empty()) {
        std::cerr << "[WebUI] webui_dir_ is empty, returning 404" << std::endl;
        send_error(res, "Web UI not configured", 404);
        return;
    }

    // Extract relative path from URL
    std::string rel_path;
    if (req.matches.size() > 1) {
        rel_path = req.matches[1].str();
    }

    // URL decode the path
    std::string decoded_path;
    for (size_t i = 0; i < rel_path.size(); ++i) {
        if (rel_path[i] == '%' && i + 2 < rel_path.size()) {
            int value;
            std::istringstream iss(rel_path.substr(i + 1, 2));
            if (iss >> std::hex >> value) {
                decoded_path += static_cast<char>(value);
                i += 2;
                continue;
            }
        }
        decoded_path += rel_path[i];
    }
    rel_path = decoded_path;

    // Default to index.html for empty path
    if (rel_path.empty()) {
        rel_path = "index.html";
    }

    // Security: prevent path traversal
    if (rel_path.find("..") != std::string::npos) {
        send_error(res, "Invalid path", 400);
        return;
    }

    fs::path full_path = fs::path(webui_dir_) / rel_path;

    // If path doesn't exist or is a directory, serve index.html for SPA routing
    if (!fs::exists(full_path) || fs::is_directory(full_path)) {
        full_path = fs::path(webui_dir_) / "index.html";
    }

    // Read and serve the file
    if (!fs::exists(full_path)) {
        std::cerr << "[WebUI] File not found: " << full_path.string()
                  << " (webui_dir=" << webui_dir_ << ")" << std::endl;
        send_error(res, "Web UI files not found", 404);
        return;
    }

    std::ifstream file(full_path, std::ios::binary);
    if (!file) {
        send_error(res, "Cannot read file", 500);
        return;
    }

    std::ostringstream oss;
    oss << file.rdbuf();
    std::string content = oss.str();

    std::string mime_type = get_mime_type(full_path.string());
    res.set_content(content, mime_type);
}

// Top-level directories under docs/ that are kept on disk but hidden from the
// /docs endpoint (internal design artifacts, not user-facing reference docs).
static const std::vector<std::string> kDocsHiddenDirs = {"plans"};

static bool is_docs_path_hidden(const std::string& rel_path) {
    fs::path p(rel_path);
    auto it = p.begin();
    if (it == p.end()) return false;
    std::string top = it->string();
    for (const auto& hidden : kDocsHiddenDirs) {
        if (top == hidden) return true;
    }
    return false;
}

std::string RequestHandlers::generate_docs_toc() {
    std::ostringstream md;
    md << "# Documentation\n\n";
    md << "Available documentation files:\n\n";

    // Collect all .md files recursively, sorted
    std::vector<std::string> files;
    for (const auto& entry : fs::recursive_directory_iterator(docs_dir_)) {
        if (entry.is_regular_file() && entry.path().extension() == ".md") {
            auto rel = fs::relative(entry.path(), docs_dir_).string();
            if (is_docs_path_hidden(rel)) continue;
            files.push_back(rel);
        }
    }
    std::sort(files.begin(), files.end());

    // Group by directory
    std::string current_dir;
    for (const auto& file : files) {
        auto dir = fs::path(file).parent_path().string();
        if (dir != current_dir) {
            current_dir = dir;
            if (!dir.empty()) {
                md << "\n### " << dir << "/\n\n";
            }
        }
        // Extract title from first heading line, or use filename
        std::string title = fs::path(file).stem().string();
        fs::path full = fs::path(docs_dir_) / file;
        std::ifstream f(full);
        if (f) {
            std::string line;
            while (std::getline(f, line)) {
                if (line.size() > 2 && line[0] == '#' && line[1] == ' ') {
                    title = line.substr(2);
                    break;
                }
            }
        }
        md << "- [" << title << "](/docs/" << file << ")\n";
    }

    return md.str();
}

void RequestHandlers::handle_docs(const httplib::Request& req, httplib::Response& res) {
    if (docs_dir_.empty()) {
        send_error(res, "Documentation not configured", 404);
        return;
    }

    // Extract relative path from URL
    std::string rel_path;
    if (req.matches.size() > 1) {
        rel_path = req.matches[1].str();
    }

    // URL decode
    std::string decoded_path;
    for (size_t i = 0; i < rel_path.size(); ++i) {
        if (rel_path[i] == '%' && i + 2 < rel_path.size()) {
            int value;
            std::istringstream iss(rel_path.substr(i + 1, 2));
            if (iss >> std::hex >> value) {
                decoded_path += static_cast<char>(value);
                i += 2;
                continue;
            }
        }
        decoded_path += rel_path[i];
    }
    rel_path = decoded_path;

    // Security: prevent path traversal
    if (rel_path.find("..") != std::string::npos) {
        send_error(res, "Invalid path", 400);
        return;
    }

    // Empty path or trailing slash → table of contents
    if (rel_path.empty()) {
        res.set_content(generate_docs_toc(), "text/markdown; charset=utf-8");
        return;
    }

    // Hidden directories are kept on disk but not exposed via HTTP.
    if (is_docs_path_hidden(rel_path)) {
        send_error(res, "Document not found: " + rel_path, 404);
        return;
    }

    fs::path full_path = fs::path(docs_dir_) / rel_path;

    if (!fs::exists(full_path)) {
        send_error(res, "Document not found: " + rel_path, 404);
        return;
    }

    // Directory → list files in that subdirectory as markdown
    if (fs::is_directory(full_path)) {
        std::ostringstream md;
        md << "# " << rel_path << "/\n\n";
        std::vector<std::string> entries;
        for (const auto& entry : fs::directory_iterator(full_path)) {
            if (entry.is_regular_file() && entry.path().extension() == ".md") {
                entries.push_back(entry.path().filename().string());
            } else if (entry.is_directory()) {
                entries.push_back(entry.path().filename().string() + "/");
            }
        }
        std::sort(entries.begin(), entries.end());
        for (const auto& e : entries) {
            std::string link_path = rel_path;
            if (!link_path.empty() && link_path.back() != '/') link_path += '/';
            link_path += e;
            md << "- [" << e << "](/docs/" << link_path << ")\n";
        }
        res.set_content(md.str(), "text/markdown; charset=utf-8");
        return;
    }

    // Serve the file
    std::ifstream file(full_path, std::ios::binary);
    if (!file) {
        send_error(res, "Cannot read file", 500);
        return;
    }

    std::ostringstream oss;
    oss << file.rdbuf();

    // Determine content type
    std::string ext = full_path.extension().string();
    std::string content_type = "text/plain; charset=utf-8";
    if (ext == ".md") {
        content_type = "text/markdown; charset=utf-8";
    }

    res.set_content(oss.str(), content_type);
}

void RequestHandlers::handle_get_preview_settings(const httplib::Request& /*req*/, httplib::Response& res) {
    auto settings = queue_manager_.get_preview_settings();

    // Convert PreviewMode enum to string
    std::string mode_str;
    switch (settings.mode) {
        case PreviewMode::None: mode_str = "none"; break;
        case PreviewMode::Proj: mode_str = "proj"; break;
        case PreviewMode::Tae:  mode_str = "tae"; break;
        case PreviewMode::Vae:  mode_str = "vae"; break;
        default:                mode_str = "tae"; break;
    }

    send_json(res, {
        {"enabled", settings.mode != PreviewMode::None},
        {"mode", mode_str},
        {"interval", settings.interval},
        {"max_size", settings.max_size},
        {"quality", settings.quality}
    });
}

void RequestHandlers::handle_update_preview_settings(const httplib::Request& req, httplib::Response& res) {
    auto json = parse_json_body(req);
    if (json.is_null()) {
        send_error(res, "Invalid JSON body", 400);
        return;
    }

    // Strict body validation
    static const std::unordered_set<std::string> KNOWN_PREVIEW = {
        "enabled", "mode", "interval", "max_size", "quality",
    };
    std::vector<std::string> unknown;
    if (json.is_object()) {
        for (auto it = json.begin(); it != json.end(); ++it) {
            if (KNOWN_PREVIEW.find(it.key()) == KNOWN_PREVIEW.end()) {
                unknown.push_back(it.key());
            }
        }
    }
    if (!unknown.empty()) {
        std::string msg = "Unknown field(s) in /preview/settings body: ";
        for (size_t i = 0; i < unknown.size(); ++i) {
            if (i) msg += ", ";
            msg += unknown[i];
        }
        msg += ". Accepted: enabled, mode, interval, max_size, quality.";
        send_error(res, msg, 400);
        return;
    }

    // Parse settings from JSON
    bool enabled = json.value("enabled", true);
    std::string mode_str = json.value("mode", "tae");
    int interval = json.value("interval", 1);
    int max_size = json.value("max_size", 256);
    int quality = json.value("quality", 75);

    // Convert string mode to enum
    PreviewMode mode = PreviewMode::Tae;
    if (!enabled || mode_str == "none") {
        mode = PreviewMode::None;
    } else if (mode_str == "proj") {
        mode = PreviewMode::Proj;
    } else if (mode_str == "tae") {
        mode = PreviewMode::Tae;
    } else if (mode_str == "vae") {
        mode = PreviewMode::Vae;
    }

    // Validate parameters
    if (interval < 1) interval = 1;
    if (interval > 100) interval = 100;
    if (max_size < 64) max_size = 64;
    if (max_size > 1024) max_size = 1024;
    if (quality < 1) quality = 1;
    if (quality > 100) quality = 100;

    // Update settings
    queue_manager_.set_preview_settings(mode, interval, max_size, quality);

    // Return updated settings
    send_json(res, {
        {"success", true},
        {"settings", {
            {"enabled", mode != PreviewMode::None},
            {"mode", mode_str},
            {"interval", interval},
            {"max_size", max_size},
            {"quality", quality}
        }}
    });
}

#ifdef SDCPP_ASSISTANT_ENABLED
// ==================== Assistant Handlers ====================

void RequestHandlers::handle_assistant_chat(const httplib::Request& req, httplib::Response& res) {
    auto json = parse_json_body(req);
    if (json.is_null()) {
        send_error(res, "Invalid JSON body", 400);
        return;
    }

    // Extract message and context
    std::string message = json.value("message", "");
    if (message.empty()) {
        send_error(res, "Message is required", 400);
        return;
    }

    nlohmann::json context = json.value("context", nlohmann::json::object());

    // Send to assistant
    auto response = assistant_client_->chat(message, context);

    if (response.success) {
        nlohmann::json result = {
            {"success", true},
            {"message", response.message}
        };

        // Include thinking/reasoning if present
        if (!response.thinking.empty()) {
            result["thinking"] = response.thinking;
        }

        // Include tool calls info if any
        if (!response.tool_calls.empty()) {
            nlohmann::json tool_calls_json = nlohmann::json::array();
            for (const auto& tc : response.tool_calls) {
                tool_calls_json.push_back(tc.to_json());
            }
            result["tool_calls"] = tool_calls_json;
        }

        if (!response.actions.empty()) {
            nlohmann::json actions_json = nlohmann::json::array();
            for (const auto& action : response.actions) {
                actions_json.push_back(action.to_json());
            }
            result["actions"] = actions_json;
        }

        send_json(res, result);
    } else {
        send_json(res, {
            {"success", false},
            {"error", response.error.value_or("Unknown error")}
        }, 500);
    }
}

void RequestHandlers::handle_assistant_chat_stream(const httplib::Request& req, httplib::Response& res) {
    auto json = parse_json_body(req);
    if (json.is_null()) {
        res.set_header("Content-Type", "text/event-stream");
        res.set_header("Cache-Control", "no-cache");
        res.set_header("Connection", "keep-alive");
        res.set_header("X-Accel-Buffering", "no");
        res.body = "event: error\ndata: {\"error\": \"Invalid JSON body\"}\n\n";
        res.status = 400;
        return;
    }

    std::string message = json.value("message", "");
    if (message.empty()) {
        res.set_header("Content-Type", "text/event-stream");
        res.set_header("Cache-Control", "no-cache");
        res.set_header("Connection", "keep-alive");
        res.set_header("X-Accel-Buffering", "no");
        res.body = "event: error\ndata: {\"error\": \"Message is required\"}\n\n";
        res.status = 400;
        return;
    }

    nlohmann::json context = json.value("context", nlohmann::json::object());

    // Set SSE headers
    res.set_header("Content-Type", "text/event-stream");
    res.set_header("Cache-Control", "no-cache");
    res.set_header("Connection", "keep-alive");
    res.set_header("X-Accel-Buffering", "no");

    // Use chunked content provider for streaming
    res.set_chunked_content_provider(
        "text/event-stream",
        [this, message, context](size_t /*offset*/, httplib::DataSink& sink) {
            std::cout << "[SSE] Starting chunked content provider" << std::endl;
            bool success = assistant_client_->chat_stream(
                message,
                context,
                [&sink](const std::string& event, const nlohmann::json& data) {
                    std::string sse = "event: " + event + "\ndata: " + data.dump() + "\n\n";
                    std::cout << "[SSE] Writing event: " << event << " (" << sse.size() << " bytes)" << std::endl;
                    bool result = sink.write(sse.c_str(), sse.size());
                    std::cout << "[SSE] Write result: " << (result ? "success" : "failed") << std::endl;
                    return result;
                }
            );

            if (!success) {
                std::string error_sse = "event: error\ndata: {\"error\": \"Stream failed\"}\n\n";
                sink.write(error_sse.c_str(), error_sse.size());
            }

            sink.done();
            return true;
        }
    );
}

void RequestHandlers::handle_assistant_history(const httplib::Request& /*req*/, httplib::Response& res) {
    auto history = assistant_client_->get_history();

    nlohmann::json messages = nlohmann::json::array();
    for (const auto& msg : history) {
        messages.push_back(msg.to_json());
    }

    send_json(res, {
        {"messages", messages},
        {"count", history.size()}
    });
}

void RequestHandlers::handle_assistant_clear_history(const httplib::Request& /*req*/, httplib::Response& res) {
    assistant_client_->clear_history();
    send_json(res, {{"success", true}});
}

void RequestHandlers::handle_assistant_status(const httplib::Request& /*req*/, httplib::Response& res) {
    send_json(res, assistant_client_->get_status());
}

void RequestHandlers::handle_assistant_get_settings(const httplib::Request& /*req*/, httplib::Response& res) {
    send_json(res, assistant_client_->get_settings());
}

void RequestHandlers::handle_assistant_update_settings(const httplib::Request& req, httplib::Response& res) {
    auto json = parse_json_body(req);
    if (json.is_null()) {
        send_error(res, "Invalid JSON body", 400);
        return;
    }

    if (assistant_client_->update_settings(json)) {
        send_json(res, {
            {"success", true},
            {"settings", assistant_client_->get_settings()}
        });
    } else {
        send_error(res, "Failed to update settings", 500);
    }
}

void RequestHandlers::handle_assistant_model_info(const httplib::Request& req, httplib::Response& res) {
    // Optional: get model name from query param, otherwise uses current model
    std::string model_name;
    if (req.has_param("model")) {
        model_name = req.get_param_value("model");
    }

    auto caps = assistant_client_->get_model_info(model_name);
    send_json(res, caps.to_json());
}
#endif // SDCPP_ASSISTANT_ENABLED

void RequestHandlers::handle_get_architectures(const httplib::Request& /*req*/, httplib::Response& res) {
    // Get current model architecture if loaded
    std::string current_arch;
    auto loaded_info = model_manager_.get_loaded_models_info();
    if (loaded_info["model_architecture"].is_string()) {
        current_arch = loaded_info["model_architecture"].get<std::string>();
    }

    // Get all architecture presets
    auto architectures = architecture_manager_->to_json();

    // If we have a current architecture, include its info separately for convenience
    const ArchitecturePreset* current_preset = nullptr;
    if (!current_arch.empty()) {
        current_preset = architecture_manager_->get(current_arch);
    }

    nlohmann::json response = {
        {"architectures", architectures},
        {"current_architecture", current_arch.empty() ? nullptr : nlohmann::json(current_arch)},
        {"current_preset", current_preset ? current_preset->to_json() : nullptr}
    };

    send_json(res, response);
}

void RequestHandlers::handle_detect_architecture(const httplib::Request& req, httplib::Response& res) {
    auto model = req.get_param_value("model");
    if (model.empty()) {
        send_error(res, "Missing 'model' query parameter", 400);
        return;
    }

    const auto* preset = architecture_manager_->get(model);
    if (preset) {
        send_json(res, {
            {"detected", true},
            {"architecture", preset->to_json()}
        });
    } else {
        send_json(res, {
            {"detected", false},
            {"architecture", nullptr}
        });
    }
}

// Search the standard data/ candidate paths and serve the first matching
// JSON file. Used by /options/descriptions (load options) and
// /options/generation. Returns {"options": {}} when not found, so the
// frontend can degrade gracefully (no tooltips, but no error either).
void RequestHandlers::serve_options_json(httplib::Response& res, const std::string& filename) {
    std::vector<std::string> search_paths = {
        "data/" + filename,
        "../data/" + filename,
    };
    std::error_code ec;
    auto exe_path = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        auto exe_dir = exe_path.parent_path();
        search_paths.push_back((exe_dir / "data" / filename).string());
        search_paths.push_back((exe_dir.parent_path() / "data" / filename).string());
        // Install layout: /opt/sdcpp-restapi/share/sdcpp-restapi/data/<filename>
        search_paths.push_back((exe_dir.parent_path() / "share" / "sdcpp-restapi" / "data" / filename).string());
    }
    for (const auto& path : search_paths) {
        if (!std::filesystem::exists(path)) continue;
        try {
            std::ifstream f(path);
            if (!f.is_open()) continue;
            nlohmann::json j = nlohmann::json::parse(f);
            send_json(res, j);
            return;
        } catch (const std::exception&) {
            // Try next candidate
        }
    }
    send_json(res, {{"options", nlohmann::json::object()}});
}

void RequestHandlers::handle_get_option_descriptions(const httplib::Request& /*req*/, httplib::Response& res) {
    serve_options_json(res, "load_options.json");
}

void RequestHandlers::handle_get_generation_option_descriptions(const httplib::Request& /*req*/, httplib::Response& res) {
    serve_options_json(res, "generation_options.json");
}

// ==================== Download Handlers ====================

void RequestHandlers::handle_download_model(const httplib::Request& req, httplib::Response& res) {
    try {
        auto body = parse_json_body(req);

        // Required parameter: model_type
        std::string model_type = body.value("model_type", "");
        if (model_type.empty()) {
            send_error(res, "model_type is required (checkpoint, vae, lora, clip, t5, embedding, controlnet, llm, esrgan, diffusion, taesd)", 400);
            return;
        }

        // Validate model_type
        const std::vector<std::string> valid_types = {
            "checkpoint", "vae", "lora", "clip", "t5", "embedding",
            "controlnet", "llm", "esrgan", "diffusion", "taesd"
        };
        bool valid_type = false;
        for (const auto& t : valid_types) {
            if (model_type == t) {
                valid_type = true;
                break;
            }
        }
        if (!valid_type) {
            send_error(res, "Invalid model_type. Valid types: checkpoint, vae, lora, clip, t5, embedding, controlnet, llm, esrgan, diffusion, taesd", 400);
            return;
        }

        // Determine source (auto-detect or explicit)
        std::string source = body.value("source", "");
        std::string url = body.value("url", "");
        std::string model_id = body.value("model_id", "");
        std::string repo_id = body.value("repo_id", "");
        std::string filename = body.value("filename", "");
        std::string subfolder = body.value("subfolder", "");
        std::string revision = body.value("revision", "main");

        // Auto-detect source if not specified
        if (source.empty()) {
            if (!url.empty()) {
                // Check if it's a CivitAI or HuggingFace URL
                if (url.find("civitai.com") != std::string::npos) {
                    source = "civitai";
                } else if (url.find("huggingface.co") != std::string::npos) {
                    source = "huggingface";
                } else {
                    source = "url";
                }
            } else if (!model_id.empty()) {
                source = "civitai";
            } else if (!repo_id.empty() && !filename.empty()) {
                source = "huggingface";
            } else {
                send_error(res, "Unable to determine download source. Provide url, model_id (CivitAI), or repo_id+filename (HuggingFace)", 400);
                return;
            }
        }

        // Build params for the download job
        nlohmann::json download_params = {
            {"source", source},
            {"model_type", model_type},
            {"subfolder", subfolder}
        };

        if (source == "url") {
            if (url.empty()) {
                send_error(res, "url is required for URL source", 400);
                return;
            }
            download_params["url"] = url;
            if (!filename.empty()) {
                download_params["filename"] = filename;
            }
        } else if (source == "civitai") {
            if (model_id.empty() && !url.empty()) {
                // Extract model ID from CivitAI URL
                // URL formats: https://civitai.com/models/123456 or https://civitai.com/models/123456?modelVersionId=789
                std::regex civitai_regex(R"(/models/(\d+)(?:\?modelVersionId=(\d+)|/(\d+))?)");
                std::smatch match;
                if (std::regex_search(url, match, civitai_regex)) {
                    model_id = match[1].str();
                    if (match[2].matched) {
                        model_id += ":" + match[2].str();
                    } else if (match[3].matched) {
                        model_id += ":" + match[3].str();
                    }
                }
            }
            if (model_id.empty()) {
                send_error(res, "model_id is required for CivitAI source", 400);
                return;
            }
            download_params["model_id"] = model_id;
        } else if (source == "huggingface") {
            if (repo_id.empty() && !url.empty()) {
                // Extract repo_id and filename from HuggingFace URL
                // URL format: https://huggingface.co/org/repo/blob/main/file.safetensors
                std::regex hf_regex(R"(huggingface\.co/([^/]+/[^/]+)(?:/(?:blob|resolve)/([^/]+)/(.+))?)");
                std::smatch match;
                if (std::regex_search(url, match, hf_regex)) {
                    repo_id = match[1].str();
                    if (match[2].matched) {
                        revision = match[2].str();
                    }
                    if (match[3].matched) {
                        filename = match[3].str();
                    }
                }
            }
            if (repo_id.empty()) {
                send_error(res, "repo_id is required for HuggingFace source", 400);
                return;
            }
            if (filename.empty()) {
                send_error(res, "filename is required for HuggingFace source", 400);
                return;
            }
            download_params["repo_id"] = repo_id;
            download_params["filename"] = filename;
            download_params["revision"] = revision;
        } else {
            send_error(res, "Invalid source. Valid sources: url, civitai, huggingface", 400);
            return;
        }

        // Add download job (with automatic hash job)
        auto [download_job_id, hash_job_id] = queue_manager_.add_download_job(download_params);

        auto status = queue_manager_.get_status();

        send_json(res, {
            {"success", true},
            {"download_job_id", download_job_id},
            {"hash_job_id", hash_job_id},
            {"source", source},
            {"model_type", model_type},
            {"position", status["pending_count"]}
        }, 202);

    } catch (const nlohmann::json::exception& e) {
        std::cerr << "[RequestHandlers] JSON parse error: " << e.what() << std::endl;
        send_error(res, std::string("Invalid JSON in request body: ") + e.what(), 400);
    } catch (const std::exception& e) {
        std::cerr << "[RequestHandlers] Download error: " << e.what() << std::endl;
        send_error(res, std::string("Download error: ") + e.what(), 500);
    }
}

void RequestHandlers::handle_get_civitai_info(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string model_id = req.matches[1].str();

        // Decode URL-encoded colon (%3A -> :)
        size_t pos = model_id.find("%3A");
        if (pos != std::string::npos) {
            model_id.replace(pos, 3, ":");
        }

        // Create download manager to fetch info
        auto paths_config = model_manager_.get_paths_config();
        DownloadManager dm(paths_config);

        auto info = dm.get_civitai_info(model_id);
        if (!info) {
            send_error(res, "Model not found on CivitAI", 404);
            return;
        }

        send_json(res, {
            {"success", true},
            {"model_id", info->model_id},
            {"version_id", info->version_id},
            {"name", info->name},
            {"version_name", info->version_name},
            {"type", info->type},
            {"base_model", info->base_model},
            {"filename", info->filename},
            {"file_size", info->file_size},
            {"sha256", info->sha256},
            {"download_url", info->download_url}
        });

    } catch (const std::exception& e) {
        std::cerr << "[RequestHandlers] CivitAI info error: " << e.what() << std::endl;
        send_error(res, std::string("Failed to fetch CivitAI info: ") + e.what(), 500);
    }
}

void RequestHandlers::handle_get_huggingface_info(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string repo_id = req.get_param_value("repo_id");
        std::string filename = req.get_param_value("filename");
        std::string revision = req.get_param_value("revision");

        if (repo_id.empty()) {
            send_error(res, "repo_id query parameter is required", 400);
            return;
        }
        if (filename.empty()) {
            send_error(res, "filename query parameter is required", 400);
            return;
        }
        if (revision.empty()) {
            revision = "main";
        }

        // Create download manager to fetch info
        auto paths_config = model_manager_.get_paths_config();
        DownloadManager dm(paths_config);

        auto info = dm.get_huggingface_info(repo_id, filename, revision);
        if (!info) {
            send_error(res, "File not found on HuggingFace", 404);
            return;
        }

        send_json(res, {
            {"success", true},
            {"repo_id", info->repo_id},
            {"filename", info->filename},
            {"revision", info->revision},
            {"file_size", info->file_size},
            {"download_url", info->download_url}
        });

    } catch (const std::exception& e) {
        std::cerr << "[RequestHandlers] HuggingFace info error: " << e.what() << std::endl;
        send_error(res, std::string("Failed to fetch HuggingFace info: ") + e.what(), 500);
    }
}

void RequestHandlers::handle_get_model_paths(const httplib::Request& /*req*/, httplib::Response& res) {
    auto paths_config = model_manager_.get_paths_config();
    send_json(res, paths_config);
}

// ──────────────────────────────────────────────────────────────────────
// POST /models/upload — multipart/form-data upload of a model file.
//
// Form fields:
//   file       (required) — the binary file part
//   model_type (required) — checkpoint|diffusion|vae|lora|clip|t5|
//                           embedding|controlnet|llm|esrgan|taesd
//   filename   (optional) — overrides the multipart Content-Disposition
//                           filename if present
//   subfolder  (optional) — relative path under the model_type directory;
//                           rejected if it contains ".." or starts with "/"
//
// v1 LIMITATION: cpp-httplib's multipart parser buffers the full file in
// memory (req.form.files[<key>].content). The server's max payload length
// in main.cpp gates how large an upload can be; once that's exceeded the
// request is rejected with 413 by httplib before reaching this handler.
// TODO: switch to streaming multipart parsing once cpp-httplib offers it
// or once we replace the parser, and write directly to disk like
// DownloadManager does for downloads.
// ──────────────────────────────────────────────────────────────────────
void RequestHandlers::handle_upload_model(const httplib::Request& req, httplib::Response& res) {
    if (!req.is_multipart_form_data()) {
        send_error(res, "Content-Type must be multipart/form-data", 400);
        return;
    }

    // model_type is sent as a regular text field.
    std::string model_type;
    if (req.form.has_field("model_type")) {
        model_type = req.form.get_field("model_type");
    }
    if (model_type.empty()) {
        send_error(res, "model_type form field is required", 400);
        return;
    }

    // Validate against the supported set used by DownloadManager and the
    // OpenAPI MODEL_TYPE_VALUES list. string_to_model_type silently maps
    // unknown values to Checkpoint, so we validate explicitly first.
    static const std::vector<std::string> kValidTypes = {
        "checkpoint", "diffusion", "vae", "lora", "clip", "t5",
        "embedding", "controlnet", "llm", "esrgan", "taesd"
    };
    if (std::find(kValidTypes.begin(), kValidTypes.end(), model_type) == kValidTypes.end()) {
        send_error(res, "Invalid model_type. Valid: checkpoint, diffusion, vae, lora, clip, t5, embedding, controlnet, llm, esrgan, taesd", 400);
        return;
    }

    // The file part is mandatory.
    if (!req.form.has_file("file")) {
        send_error(res, "file form field is required", 400);
        return;
    }
    const auto file_part = req.form.get_file("file");

    // Determine the destination filename. Explicit "filename" form field
    // wins; otherwise fall back to the Content-Disposition filename from
    // the file part itself.
    std::string filename_override;
    if (req.form.has_field("filename")) {
        filename_override = req.form.get_field("filename");
    }
    std::string filename = filename_override.empty() ? file_part.filename : filename_override;
    if (filename.empty()) {
        send_error(res, "filename could not be determined (provide one or set Content-Disposition filename)", 400);
        return;
    }

    // Defense-in-depth: strip any path components a malicious client may
    // have stuffed into the filename. Only the basename is honoured;
    // subdirectory placement must come from the explicit `subfolder`
    // form field (which we validate separately below).
    {
        fs::path fname_path(filename);
        filename = fname_path.filename().string();
        if (filename.empty() || filename == "." || filename == "..") {
            send_error(res, "filename is invalid", 400);
            return;
        }
    }

    // Extension whitelist mirrors DownloadManager::is_supported_extension.
    fs::path filename_path(filename);
    if (!DownloadManager::is_supported_extension(filename_path.extension().string())) {
        send_error(res,
                   "Unsupported file extension. Supported: .safetensors, .ckpt, .pt, .pth, .bin, .gguf",
                   400);
        return;
    }

    // Subfolder validation — block traversal and absolute paths.
    std::string subfolder;
    if (req.form.has_field("subfolder")) {
        subfolder = req.form.get_field("subfolder");
    }
    if (!subfolder.empty()) {
        if (subfolder.front() == '/' || subfolder.front() == '\\') {
            send_error(res, "subfolder must be relative (must not start with '/')", 400);
            return;
        }
        if (subfolder.find("..") != std::string::npos) {
            send_error(res, "subfolder must not contain '..'", 400);
            return;
        }
    }

    // Resolve destination directory from the model paths config.
    auto paths_config = model_manager_.get_paths_config();
    static const std::map<std::string, std::string> kTypeKey = {
        {"checkpoint", "checkpoints"},
        {"diffusion",  "diffusion_models"},
        {"vae",        "vae"},
        {"lora",       "lora"},
        {"clip",       "clip"},
        {"t5",         "t5"},
        {"embedding",  "embeddings"},
        {"controlnet", "controlnet"},
        {"llm",        "llm"},
        {"esrgan",     "esrgan"},
        {"taesd",      "taesd"},
    };
    auto key_it = kTypeKey.find(model_type);
    if (key_it == kTypeKey.end() ||
        !paths_config.contains(key_it->second) ||
        !paths_config[key_it->second].is_string()) {
        send_error(res, "Server has no directory configured for model_type=" + model_type, 500);
        return;
    }
    fs::path base_dir(paths_config[key_it->second].get<std::string>());
    fs::path dest_dir = subfolder.empty() ? base_dir : (base_dir / subfolder);
    fs::path dest_path = dest_dir / filename;

    // Sanity check the resolved destination really lives under base_dir.
    // weakly_canonical handles components that don't exist yet (the
    // destination file itself), unlike canonical().
    try {
        fs::path canonical_base = fs::weakly_canonical(base_dir);
        fs::path canonical_dest = fs::weakly_canonical(dest_path);
        auto base_str = canonical_base.string();
        auto dest_str = canonical_dest.string();
        if (dest_str.size() < base_str.size() ||
            dest_str.compare(0, base_str.size(), base_str) != 0) {
            send_error(res, "Resolved destination path escapes the model directory", 400);
            return;
        }
    } catch (const std::exception& e) {
        send_error(res, std::string("Failed to resolve destination path: ") + e.what(), 500);
        return;
    }

    // Refuse to clobber an existing file (use the convert/rename flow
    // for replacements; uploads are append-only).
    std::error_code ec;
    if (fs::exists(dest_path, ec) && fs::file_size(dest_path, ec) > 0) {
        send_error(res, "A file with that name already exists at " + dest_path.string(), 409);
        return;
    }

    // Ensure the parent directory exists.
    fs::create_directories(dest_dir, ec);
    if (ec) {
        send_error(res, "Failed to create destination directory: " + ec.message(), 500);
        return;
    }

    // Write the in-memory buffer to disk. v1 path: the parser already
    // buffered everything; we just persist it in one pass.
    {
        std::ofstream ofs(dest_path, std::ios::binary | std::ios::trunc);
        if (!ofs) {
            send_error(res, "Failed to open destination file for writing", 500);
            return;
        }
        ofs.write(file_part.content.data(),
                  static_cast<std::streamsize>(file_part.content.size()));
        if (!ofs) {
            // Best-effort cleanup so we don't leave a half-written model behind.
            std::error_code rm_ec;
            fs::remove(dest_path, rm_ec);
            send_error(res, "Failed to write upload to disk", 500);
            return;
        }
    }

    const size_t bytes_written = file_part.content.size();
    std::cout << "[RequestHandlers] Uploaded " << bytes_written << " bytes to "
              << dest_path.string() << " (model_type=" << model_type << ")" << std::endl;

    // Refresh the in-memory model index so the new file is visible to
    // /models, the WebUI, and load operations without an explicit refresh.
    try {
        model_manager_.scan_models();
    } catch (const std::exception& e) {
        std::cerr << "[RequestHandlers] scan_models after upload failed: "
                  << e.what() << std::endl;
        // Non-fatal — the file is on disk; clients can call /models/refresh.
    }

    nlohmann::json body = {
        {"success",    true},
        {"filename",   filename},
        {"model_type", model_type},
        {"size_bytes", bytes_written},
        {"full_path",  dest_path.string()},
    };
    if (!subfolder.empty()) {
        body["subfolder"] = subfolder;
    }
    send_json(res, body, 201);
}

// ==================== Settings Handlers ====================

void RequestHandlers::handle_get_generation_defaults(const httplib::Request& /*req*/, httplib::Response& res) {
    if (!settings_manager_) {
        send_error(res, "Settings manager not initialized", 500);
        return;
    }

    auto settings = settings_manager_->get_settings();
    send_json(res, {
        {"txt2img", settings.generation.txt2img},
        {"img2img", settings.generation.img2img},
        {"txt2vid", settings.generation.txt2vid}
    });
}

void RequestHandlers::handle_update_generation_defaults(const httplib::Request& req, httplib::Response& res) {
    if (!settings_manager_) {
        send_error(res, "Settings manager not initialized", 500);
        return;
    }

    auto json = parse_json_body(req);
    if (json.is_null()) {
        send_error(res, "Invalid JSON body", 400);
        return;
    }

    try {
        sdcpp::Settings settings = settings_manager_->get_settings();

        // Update each mode if provided
        if (json.contains("txt2img")) {
            settings.generation.txt2img = json["txt2img"];
        }
        if (json.contains("img2img")) {
            settings.generation.img2img = json["img2img"];
        }
        if (json.contains("txt2vid")) {
            settings.generation.txt2vid = json["txt2vid"];
        }

        settings_manager_->set_settings(settings);

        send_json(res, {
            {"success", true},
            {"settings", {
                {"txt2img", settings.generation.txt2img},
                {"img2img", settings.generation.img2img},
                {"txt2vid", settings.generation.txt2vid}
            }}
        });
    } catch (const std::exception& e) {
        send_error(res, std::string("Failed to update settings: ") + e.what(), 500);
    }
}

void RequestHandlers::handle_get_generation_defaults_for_mode(const httplib::Request& req, httplib::Response& res) {
    if (!settings_manager_) {
        send_error(res, "Settings manager not initialized", 500);
        return;
    }

    std::string mode = req.matches[1];
    auto preferences = settings_manager_->get_generation_preferences(mode);

    send_json(res, preferences);
}

void RequestHandlers::handle_update_generation_defaults_for_mode(const httplib::Request& req, httplib::Response& res) {
    if (!settings_manager_) {
        send_error(res, "Settings manager not initialized", 500);
        return;
    }

    std::string mode = req.matches[1];
    auto json = parse_json_body(req);
    if (json.is_null()) {
        send_error(res, "Invalid JSON body", 400);
        return;
    }

    try {
        settings_manager_->set_generation_preferences(mode, json);

        send_json(res, {
            {"success", true},
            {"mode", mode},
            {"preferences", json}
        });
    } catch (const std::exception& e) {
        send_error(res, std::string("Failed to update settings: ") + e.what(), 500);
    }
}

void RequestHandlers::handle_get_ui_preferences(const httplib::Request& /*req*/, httplib::Response& res) {
    if (!settings_manager_) {
        send_error(res, "Settings manager not initialized", 500);
        return;
    }

    auto preferences = settings_manager_->get_ui_preferences();
    send_json(res, {
        {"desktop_notifications", preferences.desktop_notifications},
        {"theme", preferences.theme},
        {"theme_custom", preferences.theme_custom.empty() ? nullptr : preferences.theme_custom}
    });
}

void RequestHandlers::handle_update_ui_preferences(const httplib::Request& req, httplib::Response& res) {
    if (!settings_manager_) {
        send_error(res, "Settings manager not initialized", 500);
        return;
    }

    auto json = parse_json_body(req);
    if (json.is_null()) {
        send_error(res, "Invalid JSON body", 400);
        return;
    }

    try {
        auto preferences = settings_manager_->get_ui_preferences();

        // Update only provided fields
        if (json.contains("desktop_notifications")) {
            preferences.desktop_notifications = json["desktop_notifications"];
        }
        if (json.contains("theme")) {
            preferences.theme = json["theme"];
        }
        if (json.contains("theme_custom")) {
            preferences.theme_custom = json["theme_custom"];
        }

        settings_manager_->set_ui_preferences(preferences);

        send_json(res, {
            {"success", true},
            {"preferences", {
                {"desktop_notifications", preferences.desktop_notifications},
                {"theme", preferences.theme},
                {"theme_custom", preferences.theme_custom.empty() ? nullptr : preferences.theme_custom}
            }}
        });
    } catch (const std::exception& e) {
        send_error(res, std::string("Failed to update preferences: ") + e.what(), 500);
    }
}

void RequestHandlers::handle_reset_settings(const httplib::Request& /*req*/, httplib::Response& res) {
    if (!settings_manager_) {
        send_error(res, "Settings manager not initialized", 500);
        return;
    }

    try {
        settings_manager_->reset_settings();

        send_json(res, {
            {"success", true},
            {"message", "Settings reset to defaults"}
        });
    } catch (const std::exception& e) {
        send_error(res, std::string("Failed to reset settings: ") + e.what(), 500);
    }
}

// ─────────────────────────────────────────────────────────────────────
// WebDAV implementation
// ─────────────────────────────────────────────────────────────────────

namespace {

// URL-decode a percent-encoded path component. Stops at '?' (query) — but
// httplib already strips the query string off req.path, so this is just
// belt-and-braces.
std::string url_decode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+') {
            out.push_back(' ');
        } else if (s[i] == '%' && i + 2 < s.size()) {
            int v = 0;
            char hi = s[i + 1], lo = s[i + 2];
            auto hexv = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
                if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
                return -1;
            };
            int a = hexv(hi), b = hexv(lo);
            if (a < 0 || b < 0) {
                out.push_back(s[i]);
            } else {
                v = (a << 4) | b;
                out.push_back(static_cast<char>(v));
                i += 2;
            }
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

// URL-encode a single path segment for inclusion in <D:href>. Percent-encodes
// everything that isn't a "safe" path character per RFC 3986 §3.3.
std::string url_encode_segment(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') ||
                    c == '-' || c == '_' || c == '.' || c == '~';
        if (safe) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0x0F]);
        }
    }
    return out;
}

// XML-escape a string for inclusion as a text node or attribute value.
std::string xml_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:   out.push_back(c); break;
        }
    }
    return out;
}

// Format a filesystem time as RFC 1123 (HTTP-date), e.g.
// "Fri, 03 May 2024 12:34:56 GMT".
std::string rfc1123_from_filetime(std::filesystem::file_time_type ft) {
    namespace fs = std::filesystem;
    namespace ch = std::chrono;
    auto sctp = ch::time_point_cast<ch::system_clock::duration>(
        ft - fs::file_time_type::clock::now() + ch::system_clock::now());
    std::time_t tt = ch::system_clock::to_time_t(sctp);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &tm);
    return std::string(buf);
}

// Decode a base64-encoded credentials blob (the bit after "Basic ").
// Tolerates both standard and URL-safe alphabets; ignores trailing padding
// and whitespace. Returns empty on garbage input.
std::string base64_decode(const std::string& s) {
    // Build the decode table once. Function-local static = thread-safe init.
    static const std::array<int8_t, 256> T = []() {
        std::array<int8_t, 256> tbl{};
        for (auto& c : tbl) c = -1;
        const char* alpha =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; ++i) {
            tbl[(unsigned char)alpha[i]] = static_cast<int8_t>(i);
        }
        tbl[(unsigned char)'-'] = 62;  // URL-safe variant
        tbl[(unsigned char)'_'] = 63;  // URL-safe variant
        return tbl;
    }();

    std::string out;
    out.reserve((s.size() / 4) * 3);
    uint32_t buf = 0;
    int have = 0;
    for (char c : s) {
        if (c == '=' || c == ' ' || c == '\r' || c == '\n' || c == '\t') continue;
        int8_t v = T[(unsigned char)c];
        if (v < 0) return {};  // invalid char
        buf = (buf << 6) | static_cast<uint32_t>(v);
        have += 6;
        if (have >= 8) {
            have -= 8;
            out.push_back(static_cast<char>((buf >> have) & 0xFF));
        }
    }
    return out;
}

// Map an extension to a Content-Type. Small built-in table — we don't want
// to drag in libmagic for this. Defaults to application/octet-stream.
std::string mime_for_extension(const std::filesystem::path& p) {
    std::string ext = p.extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext == ".png")  return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".webp") return "image/webp";
    if (ext == ".gif")  return "image/gif";
    if (ext == ".bmp")  return "image/bmp";
    if (ext == ".mp4")  return "video/mp4";
    if (ext == ".webm") return "video/webm";
    if (ext == ".json") return "application/json";
    if (ext == ".txt")  return "text/plain; charset=utf-8";
    if (ext == ".md")   return "text/markdown; charset=utf-8";
    if (ext == ".html") return "text/html; charset=utf-8";
    if (ext == ".safetensors") return "application/octet-stream";
    if (ext == ".gguf")        return "application/octet-stream";
    if (ext == ".ckpt" || ext == ".pt" || ext == ".pth" || ext == ".bin")
        return "application/octet-stream";
    return "application/octet-stream";
}

}  // namespace

// Walk the segments of a URL-decoded /webdav/... path and collapse `.`/`/`
// and reject any `..` segment. Returns the path tail relative to the root
// (e.g. for "/webdav/output/foo/bar.png" with root "/webdav/output", returns
// "foo/bar.png").
static std::optional<std::string>
sanitize_relative_segments(const std::string& tail) {
    std::vector<std::string> segs;
    size_t i = 0;
    while (i < tail.size()) {
        size_t j = tail.find('/', i);
        if (j == std::string::npos) j = tail.size();
        std::string seg = tail.substr(i, j - i);
        if (!seg.empty() && seg != ".") {
            if (seg == "..") {
                return std::nullopt;  // traversal attempt
            }
            // Reject NUL or control bytes — should never occur after URL decode.
            for (char c : seg) {
                if (static_cast<unsigned char>(c) < 0x20) return std::nullopt;
            }
            segs.push_back(seg);
        }
        i = j + 1;
    }
    std::string out;
    for (size_t k = 0; k < segs.size(); ++k) {
        if (k) out.push_back('/');
        out += segs[k];
    }
    return out;
}

std::optional<std::filesystem::path>
RequestHandlers::resolve_webdav_path(const std::string& url_path,
                                     std::string& out_url_root) const {
    namespace fs = std::filesystem;
    if (url_path.size() < 8 || url_path.compare(0, 8, "/webdav/") != 0) {
        return std::nullopt;
    }
    std::string raw_after = url_path.substr(8);  // strip "/webdav/"
    std::string after = url_decode(raw_after);

    // Determine root: "output" or "models/<type>".
    fs::path root_dir;
    std::string url_root;  // the URL prefix that maps to root_dir, no trailing slash
    std::string tail;      // the part after the root's URL prefix

    auto starts_with = [&](const std::string& s, const std::string& p) {
        return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
    };

    if (after == "output" || starts_with(after, "output/")) {
        if (paths_config_.output.empty()) return std::nullopt;
        root_dir = paths_config_.output;
        url_root = "/webdav/output";
        tail = (after == "output") ? "" : after.substr(7);
    } else if (after == "models" || starts_with(after, "models/")) {
        // Need /webdav/models/<type>/...
        std::string after_models = (after == "models") ? "" : after.substr(7);
        // Extract <type>
        size_t slash = after_models.find('/');
        std::string type = (slash == std::string::npos) ? after_models : after_models.substr(0, slash);
        std::string rest = (slash == std::string::npos) ? "" : after_models.substr(slash + 1);

        if (type.empty()) {
            // /webdav/models/ — pseudo-root; no real filesystem mapping.
            // Caller must handle this case (typically PROPFIND lists the
            // available types). Signal "models pseudo-root" by returning a
            // sentinel: empty path with url_root = "/webdav/models".
            out_url_root = "/webdav/models";
            return fs::path();  // empty path → caller treats as pseudo-collection
        }

        const std::string* dir = nullptr;
        if      (type == "checkpoints")       dir = &paths_config_.checkpoints;
        else if (type == "diffusion_models")  dir = &paths_config_.diffusion_models;
        else if (type == "vae")               dir = &paths_config_.vae;
        else if (type == "loras" || type == "lora") dir = &paths_config_.lora;
        else if (type == "clip")              dir = &paths_config_.clip;
        else if (type == "t5")                dir = &paths_config_.t5;
        else if (type == "controlnet")        dir = &paths_config_.controlnet;
        else if (type == "llm")               dir = &paths_config_.llm;
        else if (type == "esrgan")            dir = &paths_config_.esrgan;
        else if (type == "taesd")             dir = &paths_config_.taesd;
        else if (type == "embeddings")        dir = &paths_config_.embeddings;
        else return std::nullopt;             // unknown type → 404

        if (dir->empty()) return std::nullopt;
        root_dir = *dir;
        // We intentionally normalize "lora" → "loras" in the canonical URL.
        std::string canonical_type = (type == "lora") ? "loras" : type;
        url_root = "/webdav/models/" + canonical_type;
        tail = rest;
    } else {
        return std::nullopt;
    }

    auto sanitized = sanitize_relative_segments(tail);
    if (!sanitized.has_value()) return std::nullopt;

    out_url_root = url_root;
    fs::path full = root_dir;
    if (!sanitized->empty()) {
        full /= *sanitized;
    }
    return full;
}

void RequestHandlers::send_webdav_unauthorized(httplib::Response& res) const {
    res.status = 401;
    res.set_header("WWW-Authenticate", "Basic realm=\"sdcpp-restapi\"");
    res.set_header("Content-Type", "text/plain; charset=utf-8");
    res.body = "401 Unauthorized — WebDAV requires HTTP Basic credentials.";
}

// Pick 400 (path traversal) vs 404 (unknown root / unconfigured type) when
// resolve_webdav_path returned nullopt. Centralized so all DAV verbs agree.
static int webdav_resolve_failure_status(const std::string& url_path) {
    std::string decoded;
    // Inline copy of url_decode (we're in a different translation-unit-internal
    // namespace context here — keep it simple).
    decoded.reserve(url_path.size());
    for (size_t i = 0; i < url_path.size(); ++i) {
        if (url_path[i] == '%' && i + 2 < url_path.size()) {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
                if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
                return -1;
            };
            int a = hex(url_path[i + 1]), b = hex(url_path[i + 2]);
            if (a >= 0 && b >= 0) { decoded.push_back(static_cast<char>((a << 4) | b)); i += 2; continue; }
        }
        decoded.push_back(url_path[i]);
    }
    // Any literal ".." segment → traversal attempt → 400.
    auto has_dotdot = [](const std::string& s) {
        size_t i = 0;
        while (i < s.size()) {
            size_t j = s.find('/', i);
            if (j == std::string::npos) j = s.size();
            if (s.compare(i, j - i, "..") == 0) return true;
            i = j + 1;
        }
        return false;
    };
    return has_dotdot(decoded) ? 400 : 404;
}

std::string RequestHandlers::extract_cookie_token(const httplib::Request& req) {
    // The Cookie header is a single line of the form
    //   Cookie: name1=val1; name2=val2; ...
    // We only care about our `sdcpp_auth` entry. Hand-parse rather than pull
    // in a cookie library — the format is dead simple and we want to be
    // strict about whitespace tolerance.
    std::string h = req.get_header_value("Cookie");
    if (h.empty()) return std::string();
    static const std::string key = "sdcpp_auth=";
    size_t pos = 0;
    while (pos < h.size()) {
        // Skip leading whitespace
        while (pos < h.size() && (h[pos] == ' ' || h[pos] == '\t')) ++pos;
        size_t end = h.find(';', pos);
        if (end == std::string::npos) end = h.size();
        if (h.compare(pos, key.size(), key) == 0) {
            return h.substr(pos + key.size(), end - (pos + key.size()));
        }
        pos = end + 1;
    }
    return std::string();
}

bool RequestHandlers::verify_basic_auth(const httplib::Request& req) const {
    // If the global auth manager is disabled, /webdav/ is also unauthenticated.
    // (This mirrors REST behavior: turning auth off opens everything.)
    if (!auth_manager_.enabled()) return true;
    std::string h = req.get_header_value("Authorization");
    const std::string prefix = "Basic ";
    if (h.size() <= prefix.size() || h.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }
    std::string encoded = h.substr(prefix.size());
    // Trim whitespace.
    while (!encoded.empty() && (encoded.front() == ' ' || encoded.front() == '\t')) encoded.erase(0, 1);
    while (!encoded.empty() && (encoded.back()  == ' ' || encoded.back()  == '\t')) encoded.pop_back();
    std::string decoded = base64_decode(encoded);
    if (decoded.empty()) return false;
    auto colon = decoded.find(':');
    if (colon == std::string::npos) return false;
    std::string user = decoded.substr(0, colon);
    std::string pass = decoded.substr(colon + 1);
    return auth_manager_.verify_credentials(user, pass);
}

httplib::Server::HandlerResponse
RequestHandlers::handle_webdav_options(const httplib::Request& /*req*/, httplib::Response& res) {
    // Compliance class 1 (basic DAV); class 2 (LOCK/UNLOCK) is NOT advertised
    // because we don't implement it. macOS Finder gripes about missing class
    // 2 in logs but mounts and reads/writes fine; Windows Explorer is happy
    // with class 1 alone. Some clients (cadaver, davfs2) require class 2 for
    // write — they'll still work because we return 405 for LOCK and the
    // clients fall back to lock-free PUT. A future v1.1 will add LOCK/UNLOCK.
    res.status = 200;
    res.set_header("DAV", "1");
    res.set_header("MS-Author-Via", "DAV");
    res.set_header("Allow",
                   "OPTIONS, GET, HEAD, PROPFIND, PUT, DELETE, MKCOL, MOVE, COPY");
    // 0-byte body, but set content-length explicitly so clients don't hang.
    res.set_header("Content-Length", "0");
    return httplib::Server::HandlerResponse::Handled;
}

namespace {

// Build a single <D:response> block for one filesystem entry.
// `href` is the absolute URL path (already URL-encoded); `display_name`
// is the (XML-escaped) human-readable name.
void append_propfind_response(std::string& out,
                              const std::string& href_encoded,
                              const std::string& display_name_escaped,
                              bool is_collection,
                              uintmax_t size,
                              const std::string& last_modified_rfc1123,
                              const std::string& content_type) {
    out += "<D:response>";
    out += "<D:href>" + href_encoded + "</D:href>";
    out += "<D:propstat>";
    out += "<D:prop>";
    out += "<D:displayname>" + display_name_escaped + "</D:displayname>";
    if (is_collection) {
        out += "<D:resourcetype><D:collection/></D:resourcetype>";
        out += "<D:getcontentlength>0</D:getcontentlength>";
    } else {
        out += "<D:resourcetype/>";
        out += "<D:getcontentlength>" + std::to_string(size) + "</D:getcontentlength>";
        if (!content_type.empty()) {
            out += "<D:getcontenttype>" + xml_escape(content_type) + "</D:getcontenttype>";
        }
    }
    if (!last_modified_rfc1123.empty()) {
        out += "<D:getlastmodified>" + last_modified_rfc1123 + "</D:getlastmodified>";
    }
    out += "</D:prop>";
    out += "<D:status>HTTP/1.1 200 OK</D:status>";
    out += "</D:propstat>";
    out += "</D:response>";
}

}  // namespace

httplib::Server::HandlerResponse
RequestHandlers::handle_webdav_propfind(const httplib::Request& req, httplib::Response& res) {
    namespace fs = std::filesystem;

    // Depth: 0, 1, or "infinity". We treat "infinity" as 1 to avoid recursive
    // blowup on /webdav/models/diffusion_models/ (could contain dozens of GB).
    std::string depth_hdr = req.get_header_value("Depth");
    int depth = 1;
    if (depth_hdr == "0") depth = 0;
    else if (depth_hdr == "1") depth = 1;
    else depth = 1;  // default + "infinity" both clamped to 1

    // Pseudo-roots that don't resolve to a single filesystem directory:
    //   /webdav/                — top-level: lists "output" + "models"
    //   /webdav/models/         — lists the configured model-type subdirs
    bool is_top_level_pseudo  = (req.path == "/webdav/" || req.path == "/webdav");
    bool is_models_pseudo_root = false;
    std::string url_root;
    fs::path full;

    if (is_top_level_pseudo) {
        url_root = "/webdav";
    } else {
        auto maybe_path = resolve_webdav_path(req.path, url_root);
        if (!maybe_path.has_value()) {
            res.status = webdav_resolve_failure_status(req.path);
            res.body = (res.status == 400) ? "Invalid WebDAV path (`..` not permitted)"
                                            : "Not found";
            return httplib::Server::HandlerResponse::Handled;
        }
        is_models_pseudo_root = (url_root == "/webdav/models" && maybe_path->empty());
        full = *maybe_path;
    }

    std::string body;
    body.reserve(2048);
    body += "<?xml version=\"1.0\" encoding=\"utf-8\"?>";
    body += "<D:multistatus xmlns:D=\"DAV:\">";

    auto self_href = [&](const std::string& canonical_url, bool is_dir) {
        std::string s = canonical_url;
        if (is_dir && (s.empty() || s.back() != '/')) s.push_back('/');
        return xml_escape(s);
    };

    if (is_top_level_pseudo) {
        // Self entry.
        append_propfind_response(body, self_href("/webdav/", true), xml_escape("webdav"),
                                 true, 0, "", "");
        if (depth >= 1) {
            append_propfind_response(body, self_href("/webdav/output/", true),
                                     xml_escape("output"), true, 0, "", "");
            append_propfind_response(body, self_href("/webdav/models/", true),
                                     xml_escape("models"), true, 0, "", "");
        }
    } else if (is_models_pseudo_root) {
        append_propfind_response(body, self_href("/webdav/models/", true),
                                 xml_escape("models"), true, 0, "", "");
        if (depth >= 1) {
            // Each non-empty configured model dir becomes a collection child.
            struct Entry { const char* name; const std::string* dir; };
            const Entry entries[] = {
                {"checkpoints",      &paths_config_.checkpoints},
                {"diffusion_models", &paths_config_.diffusion_models},
                {"vae",              &paths_config_.vae},
                {"loras",            &paths_config_.lora},
                {"clip",             &paths_config_.clip},
                {"t5",               &paths_config_.t5},
                {"controlnet",       &paths_config_.controlnet},
                {"llm",              &paths_config_.llm},
                {"esrgan",           &paths_config_.esrgan},
                {"taesd",            &paths_config_.taesd},
                {"embeddings",       &paths_config_.embeddings},
            };
            for (const auto& e : entries) {
                if (e.dir->empty()) continue;
                std::string href = "/webdav/models/" + std::string(e.name) + "/";
                append_propfind_response(body, xml_escape(href),
                                         xml_escape(e.name),
                                         true, 0, "", "");
            }
        }
    } else {
        // Real filesystem path.
        std::error_code ec;
        if (!fs::exists(full, ec)) {
            res.status = 404;
            res.body = "Not found";
            return httplib::Server::HandlerResponse::Handled;
        }
        bool is_dir = fs::is_directory(full, ec);

        // Self entry.
        std::string self_url = url_root;
        // The remainder after url_root, if any, is encoded path tail.
        // For PROPFIND, the canonical href should match the request path
        // (with a trailing slash for directories).
        std::string canonical_self = req.path;
        // Normalize trailing slash on directories.
        if (is_dir && (canonical_self.empty() || canonical_self.back() != '/')) {
            canonical_self.push_back('/');
        }
        std::string self_name = full.filename().string();
        if (self_name.empty()) self_name = url_root.substr(url_root.find_last_of('/') + 1);

        uintmax_t self_size = is_dir ? 0 : fs::file_size(full, ec);
        std::string lastmod;
        auto ft = fs::last_write_time(full, ec);
        if (!ec) lastmod = rfc1123_from_filetime(ft);
        std::string ctype = is_dir ? "" : mime_for_extension(full);
        append_propfind_response(body, xml_escape(canonical_self),
                                 xml_escape(self_name),
                                 is_dir, self_size, lastmod, ctype);

        // Children (depth >= 1, only for directories).
        if (is_dir && depth >= 1) {
            std::vector<fs::directory_entry> kids;
            for (auto& de : fs::directory_iterator(full, fs::directory_options::skip_permission_denied, ec)) {
                kids.push_back(de);
                // Cap at a large but bounded number to prevent OOM on
                // pathological directories (e.g. someone dumped 100k files).
                if (kids.size() >= 5000) break;
            }
            std::sort(kids.begin(), kids.end(),
                      [](const fs::directory_entry& a, const fs::directory_entry& b) {
                          return a.path().filename().string() < b.path().filename().string();
                      });
            for (auto& de : kids) {
                std::error_code ec2;
                bool kdir = de.is_directory(ec2);
                std::string kname = de.path().filename().string();
                std::string khref = canonical_self + url_encode_segment(kname);
                if (kdir) khref.push_back('/');
                uintmax_t ksize = kdir ? 0 : fs::file_size(de.path(), ec2);
                std::string klm;
                auto kft = fs::last_write_time(de.path(), ec2);
                if (!ec2) klm = rfc1123_from_filetime(kft);
                std::string kct = kdir ? "" : mime_for_extension(de.path());
                append_propfind_response(body, xml_escape(khref),
                                         xml_escape(kname),
                                         kdir, ksize, klm, kct);
            }
        }
    }

    body += "</D:multistatus>";

    res.status = 207;
    res.set_header("DAV", "1");
    res.set_content(body, "application/xml; charset=utf-8");
    return httplib::Server::HandlerResponse::Handled;
}

httplib::Server::HandlerResponse
RequestHandlers::handle_webdav_mkcol(const httplib::Request& req, httplib::Response& res) {
    namespace fs = std::filesystem;
    std::string url_root;
    auto maybe = resolve_webdav_path(req.path, url_root);
    if (!maybe.has_value()) {
        res.status = webdav_resolve_failure_status(req.path);
        res.body = (res.status == 400) ? "Invalid WebDAV path (`..` not permitted)" : "Not found";
        return httplib::Server::HandlerResponse::Handled;
    }
    if (maybe->empty()) {
        // Pseudo-root (/webdav/, /webdav/models/) — not creatable.
        res.status = 405;
        res.body = "Cannot MKCOL on a virtual root";
        return httplib::Server::HandlerResponse::Handled;
    }
    std::error_code ec;
    if (fs::exists(*maybe, ec)) {
        res.status = 405;
        res.body = "Already exists";
        return httplib::Server::HandlerResponse::Handled;
    }
    // RFC 4918 §9.3: parent must exist; intermediate parents must NOT be created.
    if (maybe->has_parent_path() && !fs::exists(maybe->parent_path(), ec)) {
        res.status = 409;
        res.body = "Parent does not exist";
        return httplib::Server::HandlerResponse::Handled;
    }
    if (!fs::create_directory(*maybe, ec)) {
        res.status = 500;
        res.body = std::string("mkdir failed: ") + ec.message();
        return httplib::Server::HandlerResponse::Handled;
    }
    res.status = 201;  // Created
    res.body = "";
    return httplib::Server::HandlerResponse::Handled;
}

namespace {

// Take a "Destination: https://host/webdav/foo" header and return just the
// /webdav/... path part. cpp-httplib doesn't expose its URI parser, so we
// hand-roll the minimum required.
std::string extract_dest_path(const std::string& dest) {
    if (dest.empty()) return "";
    // Strip scheme://authority if present.
    size_t i = 0;
    auto scheme_end = dest.find("://");
    if (scheme_end != std::string::npos) {
        size_t authority_start = scheme_end + 3;
        size_t path_start = dest.find('/', authority_start);
        if (path_start == std::string::npos) return "";
        i = path_start;
    }
    // Trim a possible fragment / query.
    std::string path = dest.substr(i);
    auto q = path.find('?');
    if (q != std::string::npos) path = path.substr(0, q);
    auto f = path.find('#');
    if (f != std::string::npos) path = path.substr(0, f);
    return path;
}

}  // namespace

httplib::Server::HandlerResponse
RequestHandlers::handle_webdav_move(const httplib::Request& req, httplib::Response& res) {
    namespace fs = std::filesystem;
    std::string url_root_src;
    auto src = resolve_webdav_path(req.path, url_root_src);
    if (!src.has_value() || src->empty()) {
        res.status = 400;
        res.body = "Invalid source path";
        return httplib::Server::HandlerResponse::Handled;
    }
    std::error_code ec;
    if (!fs::exists(*src, ec)) {
        res.status = 404;
        res.body = "Source not found";
        return httplib::Server::HandlerResponse::Handled;
    }
    std::string dest_hdr = req.get_header_value("Destination");
    std::string dest_path = extract_dest_path(dest_hdr);
    if (dest_path.empty()) {
        res.status = 400;
        res.body = "Missing or invalid Destination header";
        return httplib::Server::HandlerResponse::Handled;
    }
    std::string url_root_dst;
    auto dst = resolve_webdav_path(dest_path, url_root_dst);
    if (!dst.has_value() || dst->empty()) {
        res.status = 400;
        res.body = "Invalid destination path";
        return httplib::Server::HandlerResponse::Handled;
    }
    bool overwrite = req.get_header_value("Overwrite") != "F";  // default = T
    bool dst_exists = fs::exists(*dst, ec);
    if (dst_exists && !overwrite) {
        res.status = 412;  // Precondition Failed
        res.body = "Destination exists and Overwrite: F";
        return httplib::Server::HandlerResponse::Handled;
    }
    if (dst_exists) {
        fs::remove_all(*dst, ec);
    }
    fs::rename(*src, *dst, ec);
    if (ec) {
        // rename() across mount points fails with EXDEV — fall back to copy+remove.
        std::error_code ec2;
        fs::copy(*src, *dst, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec2);
        if (ec2) {
            res.status = 500;
            res.body = std::string("move failed: ") + ec2.message();
            return httplib::Server::HandlerResponse::Handled;
        }
        fs::remove_all(*src, ec2);
    }
    res.status = dst_exists ? 204 : 201;
    res.body = "";
    return httplib::Server::HandlerResponse::Handled;
}

httplib::Server::HandlerResponse
RequestHandlers::handle_webdav_copy(const httplib::Request& req, httplib::Response& res) {
    namespace fs = std::filesystem;
    std::string url_root_src;
    auto src = resolve_webdav_path(req.path, url_root_src);
    if (!src.has_value() || src->empty()) {
        res.status = 400;
        res.body = "Invalid source path";
        return httplib::Server::HandlerResponse::Handled;
    }
    std::error_code ec;
    if (!fs::exists(*src, ec)) {
        res.status = 404;
        res.body = "Source not found";
        return httplib::Server::HandlerResponse::Handled;
    }
    std::string dest_hdr = req.get_header_value("Destination");
    std::string dest_path = extract_dest_path(dest_hdr);
    if (dest_path.empty()) {
        res.status = 400;
        res.body = "Missing or invalid Destination header";
        return httplib::Server::HandlerResponse::Handled;
    }
    std::string url_root_dst;
    auto dst = resolve_webdav_path(dest_path, url_root_dst);
    if (!dst.has_value() || dst->empty()) {
        res.status = 400;
        res.body = "Invalid destination path";
        return httplib::Server::HandlerResponse::Handled;
    }
    bool overwrite = req.get_header_value("Overwrite") != "F";
    bool dst_exists = fs::exists(*dst, ec);
    if (dst_exists && !overwrite) {
        res.status = 412;
        res.body = "Destination exists and Overwrite: F";
        return httplib::Server::HandlerResponse::Handled;
    }
    auto opts = fs::copy_options::recursive | fs::copy_options::overwrite_existing;
    fs::copy(*src, *dst, opts, ec);
    if (ec) {
        res.status = 500;
        res.body = std::string("copy failed: ") + ec.message();
        return httplib::Server::HandlerResponse::Handled;
    }
    res.status = dst_exists ? 204 : 201;
    res.body = "";
    return httplib::Server::HandlerResponse::Handled;
}

void RequestHandlers::handle_webdav_get(const httplib::Request& req,
                                        httplib::Response& res, bool head_only) {
    namespace fs = std::filesystem;
    std::string url_root;
    auto maybe = resolve_webdav_path(req.path, url_root);
    if (!maybe.has_value()) {
        res.status = webdav_resolve_failure_status(req.path);
        res.body = (res.status == 400) ? "Invalid WebDAV path (`..` not permitted)" : "Not found";
        return;
    }
    if (maybe->empty()) {
        // Pseudo-root — Finder/Explorer issue PROPFIND not GET on these.
        // For curl users: respond with a tiny placeholder.
        res.status = 200;
        res.set_content("WebDAV root — use PROPFIND to list collections.\n",
                        "text/plain; charset=utf-8");
        return;
    }
    std::error_code ec;
    if (!fs::exists(*maybe, ec)) {
        res.status = 404;
        res.body = "Not found";
        return;
    }
    if (fs::is_directory(*maybe, ec)) {
        // GET on a collection: redirect to PROPFIND. cpp-httplib browsers
        // don't issue PROPFIND, so emit a minimal HTML index for humans.
        std::string html =
            "<!DOCTYPE html><html><body><h1>WebDAV collection</h1>"
            "<p>Use PROPFIND to enumerate, or mount via a WebDAV client.</p>"
            "</body></html>";
        res.status = 200;
        res.set_content(html, "text/html; charset=utf-8");
        return;
    }

    // Regular file. Stream from disk so we don't blow memory on big checkpoints.
    auto size = fs::file_size(*maybe, ec);
    if (ec) {
        res.status = 500;
        res.body = "stat failed";
        return;
    }
    auto ft = fs::last_write_time(*maybe, ec);
    std::string lastmod = ec ? "" : rfc1123_from_filetime(ft);
    std::string ctype = mime_for_extension(*maybe);
    if (!lastmod.empty()) res.set_header("Last-Modified", lastmod);

    if (head_only) {
        // For HEAD, populate metadata-only response. cpp-httplib will strip
        // the body, but we still need Content-Length/Content-Type set.
        res.status = 200;
        res.set_header("Content-Length", std::to_string(size));
        res.set_header("Content-Type", ctype);
        return;
    }

    // Use chunked content provider for large files. The lambda owns an
    // ifstream; cpp-httplib calls it repeatedly until offset == size.
    auto file_path = *maybe;
    auto ifs = std::make_shared<std::ifstream>(file_path, std::ios::binary);
    if (!ifs->is_open()) {
        res.status = 500;
        res.body = "open failed";
        return;
    }
    res.status = 200;
    res.set_content_provider(
        size, ctype,
        [ifs](size_t offset, size_t length, httplib::DataSink& sink) -> bool {
            ifs->seekg(static_cast<std::streamoff>(offset), std::ios::beg);
            const size_t kChunk = 256 * 1024;
            std::vector<char> buf(std::min(length, kChunk));
            ifs->read(buf.data(), static_cast<std::streamsize>(buf.size()));
            auto n = ifs->gcount();
            if (n > 0) sink.write(buf.data(), static_cast<size_t>(n));
            return n > 0;
        },
        [ifs](bool /*success*/) { ifs->close(); });
}

void RequestHandlers::handle_webdav_put(const httplib::Request& req,
                                        httplib::Response& res) {
    namespace fs = std::filesystem;
    std::string url_root;
    auto maybe = resolve_webdav_path(req.path, url_root);
    if (!maybe.has_value()) {
        res.status = webdav_resolve_failure_status(req.path);
        res.body = (res.status == 400) ? "Invalid WebDAV path (`..` not permitted)" : "Not found";
        return;
    }
    if (maybe->empty()) {
        res.status = 405;
        res.body = "Cannot PUT to a virtual root";
        return;
    }
    std::error_code ec;
    // Parent dir must exist.
    if (maybe->has_parent_path() && !fs::exists(maybe->parent_path(), ec)) {
        res.status = 409;
        res.body = "Parent collection does not exist";
        return;
    }
    if (fs::is_directory(*maybe, ec)) {
        res.status = 405;
        res.body = "Cannot PUT over a directory";
        return;
    }
    bool existed = fs::exists(*maybe, ec);

    // Write atomically: write to <name>.webdav.tmp.<pid>, then rename. This
    // prevents a partial upload from clobbering a good file if the connection
    // drops. (Many DAV clients chunk uploads; cpp-httplib has buffered the
    // whole body into req.body by the time we get here.)
    fs::path tmp_path = *maybe;
    tmp_path += ".webdav.tmp." + std::to_string(::getpid());
    {
        std::ofstream ofs(tmp_path, std::ios::binary | std::ios::trunc);
        if (!ofs.is_open()) {
            res.status = 500;
            res.body = "open for write failed";
            return;
        }
        if (!req.body.empty()) {
            ofs.write(req.body.data(), static_cast<std::streamsize>(req.body.size()));
        }
        ofs.flush();
        if (!ofs) {
            res.status = 500;
            res.body = "write failed";
            std::error_code ec2;
            fs::remove(tmp_path, ec2);
            return;
        }
    }
    fs::rename(tmp_path, *maybe, ec);
    if (ec) {
        std::error_code ec2;
        fs::remove(tmp_path, ec2);
        res.status = 500;
        res.body = std::string("rename failed: ") + ec.message();
        return;
    }
    res.status = existed ? 204 : 201;
    res.body = "";
}

void RequestHandlers::handle_webdav_delete(const httplib::Request& req,
                                           httplib::Response& res) {
    namespace fs = std::filesystem;
    std::string url_root;
    auto maybe = resolve_webdav_path(req.path, url_root);
    if (!maybe.has_value()) {
        res.status = webdav_resolve_failure_status(req.path);
        res.body = (res.status == 400) ? "Invalid WebDAV path (`..` not permitted)" : "Not found";
        return;
    }
    if (maybe->empty()) {
        res.status = 405;
        res.body = "Cannot DELETE a virtual root";
        return;
    }
    std::error_code ec;
    if (!fs::exists(*maybe, ec)) {
        res.status = 404;
        res.body = "Not found";
        return;
    }
    fs::remove_all(*maybe, ec);
    if (ec) {
        res.status = 500;
        res.body = std::string("delete failed: ") + ec.message();
        return;
    }
    res.status = 204;
    res.body = "";
}

} // namespace sdcpp
