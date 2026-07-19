#pragma once

#include "httplib_compat.h"
#include <nlohmann/json.hpp>
#include <string>
#include <memory>
#include <optional>
#include <filesystem>

#include "config.hpp"
#include "architecture_manager.hpp"
#include "settings_manager.hpp"
#include "api_registry.hpp"
#include "api_schemas.hpp"

#ifdef SDCPP_ASSISTANT_ENABLED
#include "assistant_client.hpp"
#include "tool_executor.hpp"
#include "docs_index.hpp"
#endif

namespace sdcpp {

// Forward declarations
class ModelManager;
class QueueManager;
class AuthManager;

/**
 * Request Handlers - implements HTTP API endpoints
 */
class RequestHandlers {
public:
    /**
     * Constructor
     * @param model_manager Reference to model manager
     * @param queue_manager Reference to queue manager
     * @param auth_manager Reference to auth manager (used by login/logout + middleware)
     * @param output_dir Output directory path for file browser
     * @param webui_dir Optional webui directory path for serving web UI
     * @param assistant_config Assistant configuration for LLM helper
     * @param config_file_path Path to config.json for persisting settings (optional)
     * @param docs_dir Optional docs directory path for serving documentation
     */
    RequestHandlers(ModelManager& model_manager, QueueManager& queue_manager,
                    AuthManager& auth_manager,
                    const Config& config,
                    const std::string& output_dir, const std::string& webui_dir = "",
                    const AssistantConfig& assistant_config = AssistantConfig{},
                    const std::string& config_file_path = "",
                    const std::string& docs_dir = "");

    /**
     * Register all routes with the HTTP server
     * @param server HTTP server instance
     */
    void register_routes(httplib::Server& server);

private:
    // Model endpoints
    void handle_get_models(const httplib::Request& req, httplib::Response& res);
    void handle_refresh_models(const httplib::Request& req, httplib::Response& res);
    void handle_load_model(const httplib::Request& req, httplib::Response& res);
    void handle_unload_model(const httplib::Request& req, httplib::Response& res);
    void handle_get_model_hash(const httplib::Request& req, httplib::Response& res);
    void handle_upload_model(const httplib::Request& req, httplib::Response& res);

    // ControlNet hot-swap endpoints (sd_ctx_load_control_net / unload / has)
    void handle_controlnet_load(const httplib::Request& req, httplib::Response& res);
    void handle_controlnet_unload(const httplib::Request& req, httplib::Response& res);
    void handle_controlnet_status(const httplib::Request& req, httplib::Response& res);

    // ADetailer standalone endpoint
    void handle_adetailer(const httplib::Request& req, httplib::Response& res);

    // Generation endpoints
    void handle_txt2img(const httplib::Request& req, httplib::Response& res);
    void handle_img2img(const httplib::Request& req, httplib::Response& res);
    void handle_txt2vid(const httplib::Request& req, httplib::Response& res);
    void handle_upscale(const httplib::Request& req, httplib::Response& res);
    void handle_convert(const httplib::Request& req, httplib::Response& res);

    // Shared submission path used by txt2img/img2img/txt2vid. Honors
    // `expand_prompt: true` in the request body — when set, the prompt is
    // parsed for {a|b|c} / {N$$a|b|c} syntax, expanded into all variations,
    // and one queue item per variation is created with a shared
    // variation_group_id. When the prompt has no template syntax (or the flag
    // is false/absent), creates a single job exactly as before.
    // generation_type is GenerationType (kept as int in the header so we
    // don't need to forward-declare the enum from queue_manager.hpp).
    void submit_generation_jobs(const httplib::Request& req, httplib::Response& res,
                                int generation_type);  // GenerationType cast

    // Upscaler endpoints
    void handle_load_upscaler(const httplib::Request& req, httplib::Response& res);
    void handle_unload_upscaler(const httplib::Request& req, httplib::Response& res);

    // Queue endpoints
    void handle_get_queue(const httplib::Request& req, httplib::Response& res);
    void handle_get_job(const httplib::Request& req, httplib::Response& res);
    void handle_cancel_job(const httplib::Request& req, httplib::Response& res);
    void handle_delete_jobs(const httplib::Request& req, httplib::Response& res);

    // Recycle bin endpoints
    void handle_get_recycle_bin(const httplib::Request& req, httplib::Response& res);
    void handle_restore_job(const httplib::Request& req, httplib::Response& res);
    void handle_purge_job(const httplib::Request& req, httplib::Response& res);
    void handle_clear_recycle_bin(const httplib::Request& req, httplib::Response& res);
    void handle_get_recycle_bin_settings(const httplib::Request& req, httplib::Response& res);
    void handle_get_generation_option_descriptions(const httplib::Request& req, httplib::Response& res);
    // Shared "load JSON from data/<file> and serve" helper for the option-descriptions endpoints.
    void serve_options_json(httplib::Response& res, const std::string& filename);
    // Output folder grouping (variation_group_id -> nested dirs).
    void handle_get_output_settings(const httplib::Request& req, httplib::Response& res);
    void handle_set_output_settings(const httplib::Request& req, httplib::Response& res);

    // Job preview endpoint (serves in-memory preview JPEG)
    void handle_get_job_preview(const httplib::Request& req, httplib::Response& res);

    // Authentication endpoints
    void handle_auth_login(const httplib::Request& req, httplib::Response& res);
    void handle_auth_logout(const httplib::Request& req, httplib::Response& res);

    // Health endpoint
    void handle_health(const httplib::Request& req, httplib::Response& res);

    // Memory status endpoint
    void handle_memory(const httplib::Request& req, httplib::Response& res);

    // Options endpoint (samplers, schedulers)
    void handle_get_options(const httplib::Request& req, httplib::Response& res);

    // File browser endpoint
    void handle_file_browser(const httplib::Request& req, httplib::Response& res);
    void handle_thumbnail(const httplib::Request& req, httplib::Response& res);

    // Web UI endpoint
    void handle_webui(const httplib::Request& req, httplib::Response& res);

    // Preview settings endpoints
    void handle_get_preview_settings(const httplib::Request& req, httplib::Response& res);
    void handle_update_preview_settings(const httplib::Request& req, httplib::Response& res);

#ifdef SDCPP_ASSISTANT_ENABLED
    // Assistant endpoints
    void handle_assistant_chat(const httplib::Request& req, httplib::Response& res);
    void handle_assistant_chat_stream(const httplib::Request& req, httplib::Response& res);
    void handle_assistant_history(const httplib::Request& req, httplib::Response& res);
    void handle_assistant_clear_history(const httplib::Request& req, httplib::Response& res);
    void handle_assistant_status(const httplib::Request& req, httplib::Response& res);
    void handle_assistant_get_settings(const httplib::Request& req, httplib::Response& res);
    void handle_assistant_update_settings(const httplib::Request& req, httplib::Response& res);
    void handle_assistant_model_info(const httplib::Request& req, httplib::Response& res);
#endif

    // Documentation endpoints
    void handle_docs(const httplib::Request& req, httplib::Response& res);
    std::string generate_docs_toc();

    // WebDAV endpoints (under /webdav/)
    // Auth: HTTP Basic (verified inline; the bearer-token middleware skips this prefix).
    // Roots:
    //   /webdav/output/<path>      → config.paths.output
    //   /webdav/models/<type>/<p>  → config.paths.<type> (type ∈ {checkpoints,
    //                                  diffusion_models, vae, loras, clip, t5,
    //                                  controlnet, llm, esrgan, taesd, embeddings})
    bool verify_basic_auth(const httplib::Request& req) const;

    // Extract the value of the `sdcpp_auth` cookie from the request's
    // `Cookie:` header, or empty string if not present. Cookies are set
    // by /auth/login on successful sign-in (HttpOnly + SameSite=Strict)
    // so browsers attach them to every same-origin request — including
    // <img src="/output/..."> and WS handshakes that can't carry an
    // Authorization header.
    static std::string extract_cookie_token(const httplib::Request& req);
    // Resolve a /webdav/... URL path to an absolute filesystem path under
    // a configured root. Returns nullopt for traversal attempts (`..`),
    // unknown roots, or malformed paths. The resulting path may not exist.
    // out_url_root is filled with the canonical "/webdav/<root>/<...>"
    // prefix that maps to a configured filesystem directory (used to build
    // <D:href> URLs in PROPFIND output).
    std::optional<std::filesystem::path>
        resolve_webdav_path(const std::string& url_path,
                            std::string& out_url_root) const;
    httplib::Server::HandlerResponse handle_webdav_options(
        const httplib::Request& req, httplib::Response& res);
    httplib::Server::HandlerResponse handle_webdav_propfind(
        const httplib::Request& req, httplib::Response& res);
    httplib::Server::HandlerResponse handle_webdav_mkcol(
        const httplib::Request& req, httplib::Response& res);
    httplib::Server::HandlerResponse handle_webdav_move(
        const httplib::Request& req, httplib::Response& res);
    httplib::Server::HandlerResponse handle_webdav_copy(
        const httplib::Request& req, httplib::Response& res);
    void handle_webdav_get(const httplib::Request& req, httplib::Response& res, bool head_only);
    void handle_webdav_put(const httplib::Request& req, httplib::Response& res);
    void handle_webdav_delete(const httplib::Request& req, httplib::Response& res);
    void send_webdav_unauthorized(httplib::Response& res) const;

    // Architecture endpoints
    void handle_get_architectures(const httplib::Request& req, httplib::Response& res);
    void handle_detect_architecture(const httplib::Request& req, httplib::Response& res);
    void handle_get_option_descriptions(const httplib::Request& req, httplib::Response& res);

    // Download endpoints
    void handle_download_model(const httplib::Request& req, httplib::Response& res);
    void handle_get_civitai_info(const httplib::Request& req, httplib::Response& res);
    void handle_get_huggingface_info(const httplib::Request& req, httplib::Response& res);
    void handle_get_model_paths(const httplib::Request& req, httplib::Response& res);

    // Settings endpoints
    void handle_get_all_settings(const httplib::Request& req, httplib::Response& res);
    void handle_update_all_settings(const httplib::Request& req, httplib::Response& res);
    void handle_get_generation_defaults(const httplib::Request& req, httplib::Response& res);
    void handle_update_generation_defaults(const httplib::Request& req, httplib::Response& res);
    void handle_get_generation_defaults_for_mode(const httplib::Request& req, httplib::Response& res);
    void handle_update_generation_defaults_for_mode(const httplib::Request& req, httplib::Response& res);
    void handle_get_ui_preferences(const httplib::Request& req, httplib::Response& res);
    void handle_update_ui_preferences(const httplib::Request& req, httplib::Response& res);
    void handle_reset_settings(const httplib::Request& req, httplib::Response& res);

    // Thumbnail generation
    bool generate_thumbnail(const std::string& source_path, const std::string& thumb_path, int size = 120);
    std::string get_thumbnail_path(const std::string& source_path);
    bool is_image_file(const std::string& path);
    bool is_video_file(const std::string& path);

    // Utility methods
    void send_json(httplib::Response& res, const nlohmann::json& json, int status = 200);
    void send_error(httplib::Response& res, const std::string& message, int status = 400);
    nlohmann::json parse_json_body(const httplib::Request& req);
    std::string get_mime_type(const std::string& filepath);
    std::string format_file_size(size_t size);
    size_t calculate_directory_size(const std::string& path);
    std::time_t get_directory_content_mtime(const std::string& path);
    int count_directory_files(const std::string& path);
    std::string generate_directory_html(const std::string& path, const std::string& url_path,
                                        const std::string& sort_by = "name", bool sort_asc = true,
                                        int page = 1, int per_page = 50);

    ModelManager& model_manager_;
    QueueManager& queue_manager_;
    AuthManager& auth_manager_;
    PathsConfig paths_config_;  // Snapshot of configured model/output paths (for WebDAV mapping)
    bool allow_public_outputs_ = true;          // auth.allow_public_outputs
    std::vector<std::string> trusted_proxies_;  // server.trusted_proxies (X-Forwarded-* whitelist)
    bool mcp_image_tool_enabled_ = false;       // mcp.image_tool_enabled (surfaced in /health features)
    std::string output_dir_;
    std::string webui_dir_;
    std::string docs_dir_;
    std::unique_ptr<ArchitectureManager> architecture_manager_;
    std::unique_ptr<SettingsManager> settings_manager_;
    std::unique_ptr<ApiRegistry> api_registry_;

#ifdef SDCPP_ASSISTANT_ENABLED
    std::unique_ptr<ToolExecutor> tool_executor_;
    std::unique_ptr<AssistantClient> assistant_client_;
    std::unique_ptr<DocsIndex> docs_index_;  // BM25 index over docs/*.md
#endif
};

} // namespace sdcpp
