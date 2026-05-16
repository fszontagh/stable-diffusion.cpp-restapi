/**
 * SDCPP-RESTAPI - Main Entry Point
 *
 * REST API server for stable-diffusion.cpp
 */

#include <iostream>
#include <string>
#include <csignal>
#include <cstring>
#include <atomic>
#include <filesystem>
#include <unistd.h>

#include "httplib_compat.h"
#include "config.hpp"
#include "model_manager.hpp"
#include "queue_manager.hpp"
#include "request_handlers.hpp"
#include "memory_utils.hpp"
#include "auth_manager.hpp"
#ifdef SDCPP_WEBSOCKET_ENABLED
#include "websocket_server.hpp"
#endif
#ifdef SDCPP_MCP_ENABLED
#include "mcp_server.hpp"
#endif
#include "sd_error_capture.hpp"
#include "stable-diffusion.h"

#include <curl/curl.h>

#ifdef SDCPP_USE_CUDA
#include <cuda_runtime.h>
#include "cuda_arch_check.hpp"
#endif

namespace fs = std::filesystem;

/**
 * Callback for stable-diffusion.cpp internal logging
 * Captures SD_LOG_ERROR messages for job error reporting
 * Only outputs debug/info in debug builds (SDCPP_DEBUG=1)
 */
// Minimum sd.cpp log level forwarded to the server log. Set from config
// (server.sd_log_level: "debug" | "info" | "warn" | "error" | "off") at
// startup. Default mirrors the previous release-mode behavior (warn+).
static std::atomic<int> g_sd_log_threshold{static_cast<int>(SD_LOG_WARN)};

// off > error > warn > info > debug. SD_LOG_DEBUG=0..ERROR=3 in ggml-style
// so we just compare against the int value of the floor; "off" = INT_MAX.
static constexpr int kSdLogOff = 99;

void set_sd_log_threshold_from_string(const std::string& level) {
    int t;
    if      (level == "debug") t = static_cast<int>(SD_LOG_DEBUG);
    else if (level == "info")  t = static_cast<int>(SD_LOG_INFO);
    else if (level == "warn")  t = static_cast<int>(SD_LOG_WARN);
    else if (level == "error") t = static_cast<int>(SD_LOG_ERROR);
    else if (level == "off")   t = kSdLogOff;
    else {
        std::cerr << "[Config] unknown sd_log_level '" << level
                  << "', falling back to 'warn'" << std::endl;
        t = static_cast<int>(SD_LOG_WARN);
    }
    g_sd_log_threshold.store(t, std::memory_order_relaxed);
}

void sd_log_callback(sd_log_level_t level, const char* text, void* /*data*/) {
    // Remove trailing newline if present
    std::string msg(text);
    while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) {
        msg.pop_back();
    }

    // Capture errors for job failure reporting (independent of forwarding).
    if (level == SD_LOG_ERROR) {
        sdcpp::capture_sd_error(msg);
    }

    if (static_cast<int>(level) < g_sd_log_threshold.load(std::memory_order_relaxed)) {
        return;
    }

    const char* level_str = (level == SD_LOG_DEBUG) ? "DEBUG"
                          : (level == SD_LOG_INFO)  ? "INFO"
                          : (level == SD_LOG_WARN)  ? "WARN"
                          : (level == SD_LOG_ERROR) ? "ERROR"
                                                    : "???";
    // Errors/warnings to stderr, info/debug to stdout. journald captures both.
    std::ostream& out = (level >= SD_LOG_WARN) ? std::cerr : std::cout;
    out << "[SD:" << level_str << "] " << msg << std::endl;
}

// Global flag for graceful shutdown
static std::atomic<bool> g_running{true};
static std::atomic<int> g_signal_count{0};
static httplib::Server* g_server = nullptr;
#ifdef SDCPP_WEBSOCKET_ENABLED
static sdcpp::WebSocketServer* g_ws_server = nullptr;
#endif

/**
 * Signal handler for graceful shutdown
 * First signal: graceful shutdown
 * Second signal: force immediate exit
 */
void signal_handler(int signal) {
    int count = ++g_signal_count;

    if (count == 1) {
        // First signal - try graceful shutdown
        // Use write() instead of cout for async-signal-safety
        const char* msg = "\nReceived signal, shutting down gracefully... (press Ctrl+C again to force quit)\n";
        if (write(STDERR_FILENO, msg, strlen(msg)) < 0) { /* ignore in signal handler */ }

        g_running = false;

        // Only stop the HTTP server from signal handler (it's designed for this)
        // WebSocket and queue cleanup happens after server.listen() returns
        if (g_server) {
            g_server->stop();
        }

        // Signal WebSocket server to stop (but don't join thread from signal handler)
#ifdef SDCPP_WEBSOCKET_ENABLED
        if (g_ws_server) {
            g_ws_server->request_stop();
        }
#endif
    } else {
        // Second signal - force immediate exit
        const char* msg = "\nForce quit!\n";
        if (write(STDERR_FILENO, msg, strlen(msg)) < 0) { /* ignore in signal handler */ }
        _exit(128 + signal);
    }
}

/**
 * Print usage information
 */
void print_usage(const char* program_name) {
    std::cout << "SDCPP-RESTAPI - REST API server for stable-diffusion.cpp\n\n";
    std::cout << "Usage: " << program_name << " [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  -c, --config <path>   Path to configuration JSON file (required)\n";
    std::cout << "  -h, --help            Show this help message\n";
    std::cout << "  -v, --version         Show version information\n";
    std::cout << "\nExample:\n";
    std::cout << "  " << program_name << " --config config.json\n";
}

/**
 * Print version information
 */
void print_version() {
    std::cout << "sdcpp-restapi version 0.1.0\n";
    std::cout << "Built with stable-diffusion.cpp\n";
}

/**
 * Parse command line arguments
 * @return Config file path or empty string on error
 */
std::string parse_args(int argc, char* argv[]) {
    std::string config_path;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            std::exit(0);
        }
        else if (arg == "-v" || arg == "--version") {
            print_version();
            std::exit(0);
        }
        else if (arg == "-c" || arg == "--config") {
            if (i + 1 < argc) {
                config_path = argv[++i];
            } else {
                std::cerr << "Error: --config requires a path argument\n";
                std::exit(1);
            }
        }
        else {
            std::cerr << "Error: Unknown option: " << arg << "\n";
            print_usage(argv[0]);
            std::exit(1);
        }
    }

    return config_path;
}

int main(int argc, char* argv[]) {
    // Parse command line arguments
    std::string config_path = parse_args(argc, argv);
    config_path = std::filesystem::canonical(config_path).generic_string();

    if (config_path.empty()) {
        std::cerr << "Error: Configuration file is required\n";
        print_usage(argv[0]);
        return 1;
    }

    // Setup signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Initialize libcurl globally (used by DownloadManager for model downloads).
    // Must be called before any threads use curl_easy_*; curl_global_cleanup() is
    // called after server shutdown below.
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) {
        std::cerr << "Warning: curl_global_init failed; downloads may be unstable." << std::endl;
    }

    try {
        // Set up SD logging callback
        sd_set_log_callback(sd_log_callback, nullptr);

        // Load configuration
        std::cout << "Loading configuration from: " << config_path << std::endl;
        sdcpp::Config config = sdcpp::Config::load(config_path);

        // Validate configuration
        std::cout << "Validating configuration..." << std::endl;
        config.validate();

        // Apply sd.cpp log forwarding floor from config.
        set_sd_log_threshold_from_string(config.server.sd_log_level);
        std::cout << "SD log level: " << config.server.sd_log_level << std::endl;

        // Ensure output directory exists
        fs::path output_path(config.paths.output);
        if (!fs::exists(output_path)) {
            std::cout << "Creating output directory: " << config.paths.output << std::endl;
            fs::create_directories(output_path);
        }

        // Configure CUDA device scheduling for lower CPU usage during generation
#ifdef SDCPP_USE_CUDA
        // By default, CUDA uses spin-waiting which causes 100% CPU on one core during
        // GPU operations. Setting BlockingSync makes the CPU thread sleep while waiting
        // for GPU operations, significantly reducing CPU usage.
        cudaError_t cuda_err = cudaSetDeviceFlags(cudaDeviceScheduleBlockingSync);
        if (cuda_err != cudaSuccess && cuda_err != cudaErrorSetOnActiveProcess) {
            std::cerr << "Warning: Failed to set CUDA device scheduling: "
                      << cudaGetErrorString(cuda_err) << std::endl;
        } else {
            std::cout << "CUDA device scheduling: BlockingSync (low CPU usage)" << std::endl;
        }

        // Sanity-check that this binary's compiled CUDA archs include the
        // active GPU's compute capability. Catches the "no kernel image is
        // available for execution on the device" failure mode that otherwise
        // only surfaces at first kernel dispatch (mid-inference) — at which
        // point the abort kills the whole server with a confusing trace.
        {
            auto check = sdcpp::cuda::check_arch_compatibility();
            std::cout << "CUDA arch check: device='" << check.device_name
                      << "' compute_cap=sm_" << check.device_compute_cap << std::endl;
            if (!check.ok) {
                std::cerr << "FATAL: " << check.message << std::endl;
                return 1;
            }
            if (!check.message.empty()) {
                std::cout << "CUDA arch check: " << check.message << std::endl;
            }
        }
#endif

        // Initialize Auth Manager (must be before request handlers / MCP / WS).
        // This will throw if auth.enabled is true and no credentials are
        // available from either config or environment variables.
        std::cout << "Initializing auth manager..." << std::endl;
        sdcpp::AuthManager auth_manager(config);

        // Initialize Model Manager
        std::cout << "Initializing model manager..." << std::endl;
        sdcpp::ModelManager model_manager(config);

        // Scan for models
        std::cout << "Scanning for models..." << std::endl;
        model_manager.scan_models();

        // Print discovered models summary
        auto checkpoints = model_manager.get_models(sdcpp::ModelType::Checkpoint);
        auto diffusion = model_manager.get_models(sdcpp::ModelType::Diffusion);
        auto vaes = model_manager.get_models(sdcpp::ModelType::VAE);
        auto loras = model_manager.get_models(sdcpp::ModelType::LoRA);
        auto clips = model_manager.get_models(sdcpp::ModelType::CLIP);
        auto t5s = model_manager.get_models(sdcpp::ModelType::T5);
        auto controlnets = model_manager.get_models(sdcpp::ModelType::ControlNet);
        auto llms = model_manager.get_models(sdcpp::ModelType::LLM);

        std::cout << "Found models:\n";
        std::cout << "  - Checkpoints: " << checkpoints.size() << "\n";
        std::cout << "  - Diffusion models: " << diffusion.size() << "\n";
        std::cout << "  - VAE: " << vaes.size() << "\n";
        std::cout << "  - LoRA: " << loras.size() << "\n";
        std::cout << "  - CLIP: " << clips.size() << "\n";
        std::cout << "  - T5: " << t5s.size() << "\n";
        std::cout << "  - ControlNet: " << controlnets.size() << "\n";
        std::cout << "  - LLM: " << llms.size() << "\n";

        // Initialize Queue Manager
        std::string state_file = (output_path / "queue_state.json").string();
        std::cout << "Initializing queue manager (state file: " << state_file << ")..." << std::endl;
        std::cout << "  Recycle bin: " << (config.recycle_bin.enabled ? "enabled" : "disabled")
                  << " (retention: " << config.recycle_bin.retention_minutes << " minutes)" << std::endl;
        sdcpp::QueueManager queue_manager(model_manager, config.paths.output, state_file, config.recycle_bin);
        queue_manager.set_group_folders_enabled(config.output_group_folders);

        // Initialize preview settings from config
        if (config.preview.enabled) {
            sdcpp::PreviewMode preview_mode = sdcpp::PreviewMode::Tae;
            if (config.preview.mode == "none") preview_mode = sdcpp::PreviewMode::None;
            else if (config.preview.mode == "proj") preview_mode = sdcpp::PreviewMode::Proj;
            else if (config.preview.mode == "tae") preview_mode = sdcpp::PreviewMode::Tae;
            else if (config.preview.mode == "vae") preview_mode = sdcpp::PreviewMode::Vae;

            queue_manager.set_preview_settings(
                preview_mode,
                config.preview.interval,
                config.preview.max_size,
                config.preview.quality
            );
            std::cout << "Preview enabled: mode=" << config.preview.mode
                      << ", interval=" << config.preview.interval << std::endl;
        } else {
            queue_manager.set_preview_settings(sdcpp::PreviewMode::None, 1, 256, 75);
            std::cout << "Preview disabled" << std::endl;
        }

        // Initialize HTTP Server
        std::cout << "Initializing HTTP server..." << std::endl;
        httplib::Server server;
        g_server = &server;

        // Set server options.
        // 50 GiB ceiling so multipart model uploads (POST /models/upload) can
        // accept large checkpoints. Image uploads remain orders of magnitude
        // smaller; this only sets the upper bound. v1 limitation: cpp-httplib
        // buffers multipart bodies in memory — keep that in mind when sizing.
        server.set_payload_max_length(static_cast<size_t>(50) * 1024 * 1024 * 1024);

        // Wire the configured worker pool size. Without this, cpp-httplib
        // uses its default (≈ hardware_concurrency-1) and the
        // `server.threads` knob in config.json is silently ignored. A
        // larger pool keeps `/output` and `/thumb` GETs responsive when a
        // generation job is also tying up workers with long-poll progress
        // and WS frames. Clamp to a sane range so a misconfigured value
        // doesn't allocate thousands of threads.
        {
            int t = config.server.threads;
            if (t < 4)   t = 4;
            if (t > 256) t = 256;
            server.new_task_queue = [t]() { return new httplib::ThreadPool(t); };
            std::cout << "HTTP worker pool size: " << t
                      << (t == config.server.threads ? "" : " (clamped)") << std::endl;
        }

        // Set error logger for debugging HTTP connection issues
        server.set_error_logger([](const httplib::Error& err, const httplib::Request* req) {
            std::string path = req ? req->path : "unknown";
            std::cerr << "[HTTP Error] " << static_cast<int>(err)
                      << " on path: " << path << std::endl;
        });

        // Determine webui path
        std::string webui_path;
#ifdef SDCPP_WEBUI_ENABLED
        // Use config value if set, otherwise use compile-time default
        if (!config.paths.webui.empty()) {
            webui_path = config.paths.webui;
        } else {
            webui_path = SDCPP_WEBUI_DEFAULT_PATH;
        }
        // Allow environment variable override
        if (const char* env_path = std::getenv("SDCPP_WEBUI_PATH")) {
            webui_path = env_path;
        }
        // Verify webui path exists
        if (!webui_path.empty() && !fs::exists(webui_path)) {
            std::cout << "Warning: Web UI path does not exist: " << webui_path << std::endl;
            webui_path.clear();
        }
#endif

        // Determine docs path
        std::string docs_path;
#ifdef SDCPP_DOCS_DEFAULT_PATH
        docs_path = SDCPP_DOCS_DEFAULT_PATH;
#endif
        // Allow environment variable override
        if (const char* env_path = std::getenv("SDCPP_DOCS_PATH")) {
            docs_path = env_path;
        }
        // Verify docs path exists
        if (!docs_path.empty() && !fs::exists(docs_path)) {
            std::cout << "Warning: Docs path does not exist: " << docs_path << std::endl;
            docs_path.clear();
        }

        // Initialize Request Handlers and register routes
        std::cout << "Registering API routes..." << std::endl;
        sdcpp::RequestHandlers handlers(model_manager, queue_manager, auth_manager,
                                        config,
                                        config.paths.output, webui_path, config.assistant,
                                        config_path, docs_path);
        handlers.register_routes(server);

        // Initialize MCP server (if enabled at build time)
#ifdef SDCPP_MCP_ENABLED
        std::cout << "Initializing MCP server..." << std::endl;
        sdcpp::McpServer mcp_server(server, model_manager, queue_manager, auth_manager);
        mcp_server.register_endpoint();
#else
        std::cout << "MCP server disabled at build time" << std::endl;
#endif

        // Set error handler for HTTP errors (404, etc.)
        server.set_error_handler([](const httplib::Request& /*req*/, httplib::Response& res) {
            // Only set content if not already set by our handlers
            if (res.body.empty()) {
                nlohmann::json error_json;
                error_json["error"] = "HTTP error";
                error_json["status"] = res.status;

                switch (res.status) {
                    case 404: error_json["message"] = "Endpoint not found"; break;
                    case 405: error_json["message"] = "Method not allowed"; break;
                    default: error_json["message"] = "HTTP error occurred"; break;
                }

                res.set_content(error_json.dump(), "application/json");
            }
        });

        // Set exception handler for uncaught exceptions
        server.set_exception_handler([](const httplib::Request& /*req*/, httplib::Response& res, std::exception_ptr ep) {
            std::string error_message = "Unknown internal error";
            try {
                if (ep) {
                    std::rethrow_exception(ep);
                }
            } catch (const std::exception& e) {
                error_message = e.what();
                std::cerr << "[Server] Uncaught exception: " << error_message << std::endl;
            }

            nlohmann::json error_json;
            error_json["error"] = error_message;
            res.status = 500;
            res.set_content(error_json.dump(), "application/json");
        });

        // Initialize WebSocket server (if enabled at build time)
#ifdef SDCPP_WEBSOCKET_ENABLED
        std::cout << "Initializing WebSocket server..." << std::endl;
        auto ws_server = std::make_unique<sdcpp::WebSocketServer>(&auth_manager);
        sdcpp::set_websocket_server(ws_server.get());
        g_ws_server = ws_server.get();

        // Set up status provider for WebSocket clients
        sdcpp::set_status_provider([&model_manager]() {
            auto status = model_manager.get_loaded_models_info();
            status["memory"] = sdcpp::get_memory_info().to_json();
            return status;
        });

        // Register /ws endpoint on the HTTP server (must be before listen())
        ws_server->setup_endpoint(server);
        ws_server->start();
#else
        std::cout << "WebSocket server disabled at build time" << std::endl;
#endif

        // If a previous run persisted a loaded-model identity, try to
        // re-load that model now. This used to run synchronously, but bf16
        // models can take 5+ minutes to load — blocking the HTTP server
        // from listening for that whole window. Now we spawn a detached
        // thread; the HTTP server starts listening immediately and the
        // model loads in parallel. Queued jobs that need a model will
        // wait via the existing model_loading state machine; new ones
        // submitted before the load completes will fail with "No model
        // loaded" same as before, which is honest behavior.
        std::cout << "Checking for persisted model state (async)..." << std::endl;
        std::thread([&model_manager]() {
            try {
                model_manager.try_auto_reload_from_disk();
            } catch (const std::exception& e) {
                std::cerr << "[main] (async) auto-reload threw: "
                          << e.what() << std::endl;
            }
        }).detach();

        // Start the queue worker
        std::cout << "Starting queue worker..." << std::endl;
        queue_manager.start();

        // Start HTTP server
        std::cout << "\n========================================" << std::endl;
        std::cout << "SDCPP-RESTAPI Server Started" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "HTTP API:     http://" << config.server.host << ":" << config.server.port << std::endl;
#ifdef SDCPP_WEBSOCKET_ENABLED
        std::cout << "WebSocket:    ws://" << config.server.host << ":" << config.server.port << "/ws" << std::endl;
#endif
        std::cout << "Output URL:   http://" << config.server.host << ":" << config.server.port << "/output/" << std::endl;
        if (!webui_path.empty()) {
            std::cout << "Web UI:       http://" << config.server.host << ":" << config.server.port << "/ui/" << std::endl;
        }
        std::cout << "Auth:         " << (auth_manager.enabled() ? "ENABLED (POST /auth/login to obtain bearer token)" : "DISABLED") << std::endl;
        std::cout << "Press Ctrl+C to stop (twice to force quit)\n" << std::endl;

        if (!server.listen(config.server.host, config.server.port)) {
            if (g_running) {
                std::cerr << "Error: Failed to start server on "
                          << config.server.host << ":" << config.server.port << std::endl;
                return 1;
            }
        }

        // Cleanup
        // Stop WebSocket server first (to prevent new event broadcasts)
#ifdef SDCPP_WEBSOCKET_ENABLED
        if (ws_server) {
            std::cout << "Stopping WebSocket server..." << std::endl;
            ws_server->stop();
            sdcpp::set_websocket_server(nullptr);
            g_ws_server = nullptr;
        }
#endif

        std::cout << "Stopping queue worker..." << std::endl;
        queue_manager.stop();

        std::cout << "Unloading model..." << std::endl;
        model_manager.unload_model();

        g_server = nullptr;
        std::cout << "Server shutdown complete." << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        curl_global_cleanup();
        return 1;
    }

    curl_global_cleanup();
    return 0;
}
