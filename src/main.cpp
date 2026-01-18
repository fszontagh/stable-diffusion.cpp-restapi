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

#include "httplib.h"
#include "config.hpp"
#include "model_manager.hpp"
#include "queue_manager.hpp"
#include "request_handlers.hpp"
#include "websocket_server.hpp"
#include "sd_error_capture.hpp"
#include "stable-diffusion.h"

#ifdef SDCPP_USE_CUDA
#include <cuda_runtime.h>
#endif

namespace fs = std::filesystem;

/**
 * Callback for stable-diffusion.cpp internal logging
 * Captures SD_LOG_ERROR messages for job error reporting
 * Only outputs debug/info in debug builds (SDCPP_DEBUG=1)
 */
void sd_log_callback(sd_log_level_t level, const char* text, void* /*data*/) {
    // Remove trailing newline if present
    std::string msg(text);
    while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) {
        msg.pop_back();
    }

    // Capture errors for job failure reporting
    if (level == SD_LOG_ERROR) {
        sdcpp::capture_sd_error(msg);
    }

#if SDCPP_DEBUG
    const char* level_str = "";
    switch (level) {
        case SD_LOG_DEBUG: level_str = "DEBUG"; break;
        case SD_LOG_INFO: level_str = "INFO"; break;
        case SD_LOG_WARN: level_str = "WARN"; break;
        case SD_LOG_ERROR: level_str = "ERROR"; break;
        default: level_str = "???"; break;
    }
    std::cout << "[SD:" << level_str << "] " << msg << std::endl;
#else
    // In release mode, only show warnings and errors
    if (level >= SD_LOG_WARN) {
        const char* level_str = (level == SD_LOG_WARN) ? "WARN" : "ERROR";
        std::cerr << "[SD:" << level_str << "] " << msg << std::endl;
    }
#endif
}

// Global flag for graceful shutdown
static std::atomic<bool> g_running{true};
static std::atomic<int> g_signal_count{0};
static httplib::Server* g_server = nullptr;
static sdcpp::WebSocketServer* g_ws_server = nullptr;

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
        write(STDERR_FILENO, msg, strlen(msg));

        g_running = false;

        // Only stop the HTTP server from signal handler (it's designed for this)
        // WebSocket and queue cleanup happens after server.listen() returns
        if (g_server) {
            g_server->stop();
        }

        // Signal WebSocket server to stop (but don't join thread from signal handler)
        if (g_ws_server) {
            g_ws_server->request_stop();
        }
    } else {
        // Second signal - force immediate exit
        const char* msg = "\nForce quit!\n";
        write(STDERR_FILENO, msg, strlen(msg));
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

    try {
        // Set up SD logging callback
        sd_set_log_callback(sd_log_callback, nullptr);

        // Load configuration
        std::cout << "Loading configuration from: " << config_path << std::endl;
        sdcpp::Config config = sdcpp::Config::load(config_path);

        // Validate configuration
        std::cout << "Validating configuration..." << std::endl;
        config.validate();

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
#endif

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
        sdcpp::QueueManager queue_manager(model_manager, config.paths.output, state_file);

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

        // Set server options
        server.set_payload_max_length(1024 * 1024 * 100);  // 100MB max for image uploads

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

        // Initialize Request Handlers and register routes
        std::cout << "Registering API routes..." << std::endl;
        sdcpp::RequestHandlers handlers(model_manager, queue_manager, config.paths.output, webui_path, config.server.ws_port, config.ollama, config.assistant, config_path);
        handlers.register_routes(server);

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

        // Initialize WebSocket server (if enabled)
        std::unique_ptr<sdcpp::WebSocketServer> ws_server;
        if (config.server.ws_port > 0) {
            std::cout << "Initializing WebSocket server..." << std::endl;
            ws_server = std::make_unique<sdcpp::WebSocketServer>(config.server.ws_port);
            sdcpp::set_websocket_server(ws_server.get());
            g_ws_server = ws_server.get();
            ws_server->start();
        }

        // Start the queue worker
        std::cout << "Starting queue worker..." << std::endl;
        queue_manager.start();

        // Start HTTP server
        std::cout << "\n========================================" << std::endl;
        std::cout << "SDCPP-RESTAPI Server Started" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "HTTP API:     http://" << config.server.host << ":" << config.server.port << std::endl;
        if (config.server.ws_port > 0) {
            std::cout << "WebSocket:    ws://" << config.server.host << ":" << config.server.ws_port << std::endl;
        }
        std::cout << "Output URL:   http://" << config.server.host << ":" << config.server.port << "/output/" << std::endl;
        if (!webui_path.empty()) {
            std::cout << "Web UI:       http://" << config.server.host << ":" << config.server.port << "/ui/" << std::endl;
        }
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
        if (ws_server) {
            std::cout << "Stopping WebSocket server..." << std::endl;
            ws_server->stop();
            sdcpp::set_websocket_server(nullptr);
            g_ws_server = nullptr;
        }

        std::cout << "Stopping queue worker..." << std::endl;
        queue_manager.stop();

        std::cout << "Unloading model..." << std::endl;
        model_manager.unload_model();

        g_server = nullptr;
        std::cout << "Server shutdown complete." << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
