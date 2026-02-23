#include "request_handlers.hpp"
#include "model_manager.hpp"
#include "queue_manager.hpp"
#include "download_manager.hpp"
#include "settings_manager.hpp"

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
#include <regex>

#include "stb_image.h"
#include "stb_image_write.h"

namespace fs = std::filesystem;

namespace sdcpp {

RequestHandlers::RequestHandlers(ModelManager& model_manager, QueueManager& queue_manager,
                                 const std::string& output_dir, const std::string& webui_dir,
                                 int ws_port,
                                 const AssistantConfig& assistant_config,
                                 const std::string& config_file_path)
    : model_manager_(model_manager), queue_manager_(queue_manager), output_dir_(output_dir), webui_dir_(webui_dir), ws_port_(ws_port)
    // ArchitectureManager uses config directory (where model_architectures.json lives), not output directory
    , architecture_manager_(std::make_unique<ArchitectureManager>(
          config_file_path.empty() ? output_dir : fs::path(config_file_path).parent_path().string()))
    , settings_manager_(std::make_unique<SettingsManager>(config_file_path, output_dir)) {

    // Initialize settings manager
    if (!settings_manager_->initialize()) {
        std::cerr << "[RequestHandlers] Warning: Failed to initialize settings manager" << std::endl;
    }

#ifdef SDCPP_ASSISTANT_ENABLED
    // Create tool executor for backend query tool execution
    tool_executor_ = std::make_unique<ToolExecutor>(
        model_manager_, queue_manager_, architecture_manager_.get());
    tool_executor_->set_output_dir(output_dir);
    tool_executor_->set_settings_manager(settings_manager_.get());

    // Create assistant client with tool executor
    assistant_client_ = std::make_unique<AssistantClient>(
        assistant_config, output_dir, config_file_path, tool_executor_.get());
#else
    (void)assistant_config;  // Suppress unused parameter warning
#endif
}

void RequestHandlers::register_routes(httplib::Server& server) {
    // Health check
    server.Get("/health", [this](const httplib::Request& req, httplib::Response& res) {
        handle_health(req, res);
    });

    // Options (samplers, schedulers)
    server.Get("/options", [this](const httplib::Request& req, httplib::Response& res) {
        handle_get_options(req, res);
    });

    // Model routes
    server.Get("/models", [this](const httplib::Request& req, httplib::Response& res) {
        handle_get_models(req, res);
    });
    server.Post("/models/refresh", [this](const httplib::Request& req, httplib::Response& res) {
        handle_refresh_models(req, res);
    });
    server.Post("/models/load", [this](const httplib::Request& req, httplib::Response& res) {
        handle_load_model(req, res);
    });
    server.Post("/models/unload", [this](const httplib::Request& req, httplib::Response& res) {
        handle_unload_model(req, res);
    });
    server.Get(R"(/models/hash/([^/]+)/(.+))", [this](const httplib::Request& req, httplib::Response& res) {
        handle_get_model_hash(req, res);
    });
    
    // Generation routes
    server.Post("/txt2img", [this](const httplib::Request& req, httplib::Response& res) {
        handle_txt2img(req, res);
    });
    server.Post("/img2img", [this](const httplib::Request& req, httplib::Response& res) {
        handle_img2img(req, res);
    });
    server.Post("/txt2vid", [this](const httplib::Request& req, httplib::Response& res) {
        handle_txt2vid(req, res);
    });
    server.Post("/upscale", [this](const httplib::Request& req, httplib::Response& res) {
        handle_upscale(req, res);
    });
    server.Post("/convert", [this](const httplib::Request& req, httplib::Response& res) {
        handle_convert(req, res);
    });

    // Upscaler routes
    server.Post("/upscaler/load", [this](const httplib::Request& req, httplib::Response& res) {
        handle_load_upscaler(req, res);
    });
    server.Post("/upscaler/unload", [this](const httplib::Request& req, httplib::Response& res) {
        handle_unload_upscaler(req, res);
    });
    
    // Queue routes
    server.Get("/queue", [this](const httplib::Request& req, httplib::Response& res) {
        handle_get_queue(req, res);
    });
    server.Get(R"(/queue/([a-f0-9\-]+))", [this](const httplib::Request& req, httplib::Response& res) {
        handle_get_job(req, res);
    });
    server.Delete(R"(/queue/([a-f0-9\-]+))", [this](const httplib::Request& req, httplib::Response& res) {
        handle_cancel_job(req, res);
    });
    server.Delete("/queue/jobs", [this](const httplib::Request& req, httplib::Response& res) {
        handle_delete_jobs(req, res);
    });

    // Job preview endpoint - serves in-memory preview JPEG
    server.Get(R"(/jobs/([a-f0-9\-]+)/preview)", [this](const httplib::Request& req, httplib::Response& res) {
        handle_get_job_preview(req, res);
    });

    // Thumbnail endpoint - must be before file browser
    server.Get(R"(/thumb/(.*))", [this](const httplib::Request& req, httplib::Response& res) {
        handle_thumbnail(req, res);
    });

    // File browser - handles both /output and /output/...
    server.Get("/output", [this](const httplib::Request& req, httplib::Response& res) {
        handle_file_browser(req, res);
    });
    server.Get(R"(/output/(.*))", [this](const httplib::Request& req, httplib::Response& res) {
        handle_file_browser(req, res);
    });

    // Web UI routes (only if webui_dir is set)
    if (!webui_dir_.empty()) {
        std::cout << "[Routes] Registering WebUI routes (webui_dir=" << webui_dir_ << ")" << std::endl;
        // Redirect /ui to /ui/
        server.Get("/ui", [](const httplib::Request& /*req*/, httplib::Response& res) {
            res.set_redirect("/ui/");
        });

        // Serve /ui/ and /ui/*
        server.Get("/ui/", [this](const httplib::Request& req, httplib::Response& res) {
            handle_webui(req, res);
        });
        server.Get(R"(/ui/(.*))", [this](const httplib::Request& req, httplib::Response& res) {
            handle_webui(req, res);
        });
    } else {
        std::cout << "[Routes] WebUI routes NOT registered (webui_dir is empty)" << std::endl;
    }

    // Preview settings routes
    server.Get("/preview/settings", [this](const httplib::Request& req, httplib::Response& res) {
        handle_get_preview_settings(req, res);
    });
    server.Put("/preview/settings", [this](const httplib::Request& req, httplib::Response& res) {
        handle_update_preview_settings(req, res);
    });

#ifdef SDCPP_ASSISTANT_ENABLED
    // Assistant routes (LLM helper)
    server.Post("/assistant/chat", [this](const httplib::Request& req, httplib::Response& res) {
        handle_assistant_chat(req, res);
    });
    server.Post("/assistant/chat/stream", [this](const httplib::Request& req, httplib::Response& res) {
        handle_assistant_chat_stream(req, res);
    });
    server.Get("/assistant/history", [this](const httplib::Request& req, httplib::Response& res) {
        handle_assistant_history(req, res);
    });
    server.Delete("/assistant/history", [this](const httplib::Request& req, httplib::Response& res) {
        handle_assistant_clear_history(req, res);
    });
    server.Get("/assistant/status", [this](const httplib::Request& req, httplib::Response& res) {
        handle_assistant_status(req, res);
    });
    server.Get("/assistant/settings", [this](const httplib::Request& req, httplib::Response& res) {
        handle_assistant_get_settings(req, res);
    });
    server.Put("/assistant/settings", [this](const httplib::Request& req, httplib::Response& res) {
        handle_assistant_update_settings(req, res);
    });
    server.Get("/assistant/model-info", [this](const httplib::Request& req, httplib::Response& res) {
        handle_assistant_model_info(req, res);
    });
#endif

    // Settings endpoints
    server.Get("/settings/generation", [this](const httplib::Request& req, httplib::Response& res) {
        handle_get_generation_defaults(req, res);
    });
    server.Put("/settings/generation", [this](const httplib::Request& req, httplib::Response& res) {
        handle_update_generation_defaults(req, res);
    });
    server.Get(R"(/settings/generation/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        handle_get_generation_defaults_for_mode(req, res);
    });
    server.Put(R"(/settings/generation/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        handle_update_generation_defaults_for_mode(req, res);
    });
    server.Get("/settings/preferences", [this](const httplib::Request& req, httplib::Response& res) {
        handle_get_ui_preferences(req, res);
    });
    server.Put("/settings/preferences", [this](const httplib::Request& req, httplib::Response& res) {
        handle_update_ui_preferences(req, res);
    });
    server.Post("/settings/reset", [this](const httplib::Request& req, httplib::Response& res) {
        handle_reset_settings(req, res);
    });

    // Architecture presets
    server.Get("/architectures", [this](const httplib::Request& req, httplib::Response& res) {
        handle_get_architectures(req, res);
    });

    // Model download routes
    server.Post("/models/download", [this](const httplib::Request& req, httplib::Response& res) {
        handle_download_model(req, res);
    });
    server.Get(R"(/models/civitai/(\d+(?:(?::|%3A)\d+)?))", [this](const httplib::Request& req, httplib::Response& res) {
        handle_get_civitai_info(req, res);
    });
    server.Get("/models/huggingface", [this](const httplib::Request& req, httplib::Response& res) {
        handle_get_huggingface_info(req, res);
    });
    server.Get("/models/paths", [this](const httplib::Request& req, httplib::Response& res) {
        handle_get_model_paths(req, res);
    });
}

void RequestHandlers::handle_health(const httplib::Request& /*req*/, httplib::Response& res) {
    auto loaded_info = model_manager_.get_loaded_models_info();

    nlohmann::json response = {
        {"status", "ok"},
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
        {"upscaler_loaded", loaded_info["upscaler_loaded"]},
        {"upscaler_name", loaded_info["upscaler_name"]},
        {"ws_port", ws_port_ > 0 ? nlohmann::json(ws_port_) : nlohmann::json(nullptr)}
    };

    send_json(res, response);
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
            "res_2s"
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
            "bong_tangent"
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
            {{"id", "q2_k"}, {"name", "Q2_K (2-bit K-quant)"}, {"bits", 2}}
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
    try {
        auto body = parse_json_body(req);
        auto params = ModelLoadParams::from_json(body);
        
        std::cout << "[RequestHandlers] Loading model: " << params.model_name << std::endl;
        
        model_manager_.load_model(params);
        
        // Get full loaded model info including components
        auto loaded_info = model_manager_.get_loaded_models_info();
        
        nlohmann::json response = {
            {"success", true},
            {"message", "Model loaded successfully"},
            {"model_name", loaded_info["model_name"]},
            {"model_type", loaded_info["model_type"]},
            {"loaded_components", loaded_info["loaded_components"]}
        };
        
        send_json(res, response);
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "[RequestHandlers] JSON parse error: " << e.what() << std::endl;
        send_error(res, std::string("Invalid JSON in request body: ") + e.what(), 400);
    } catch (const std::runtime_error& e) {
        std::cerr << "[RequestHandlers] Model load error: " << e.what() << std::endl;
        send_error(res, e.what(), 400);  // Use 400 for validation errors
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
    try {
        if (!model_manager_.is_model_loaded()) {
            send_error(res, "No model loaded", 400);
            return;
        }
        
        auto body = parse_json_body(req);
        std::string job_id = queue_manager_.add_job(GenerationType::Text2Image, body);
        
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

void RequestHandlers::handle_img2img(const httplib::Request& req, httplib::Response& res) {
    try {
        if (!model_manager_.is_model_loaded()) {
            send_error(res, "No model loaded", 400);
            return;
        }
        
        auto body = parse_json_body(req);
        std::string job_id = queue_manager_.add_job(GenerationType::Image2Image, body);
        
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

void RequestHandlers::handle_txt2vid(const httplib::Request& req, httplib::Response& res) {
    try {
        if (!model_manager_.is_model_loaded()) {
            send_error(res, "No model loaded", 400);
            return;
        }
        
        auto body = parse_json_body(req);
        std::string job_id = queue_manager_.add_job(GenerationType::Text2Video, body);
        
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

void RequestHandlers::handle_upscale(const httplib::Request& req, httplib::Response& res) {
    try {
        if (!model_manager_.is_upscaler_loaded()) {
            send_error(res, "No upscaler loaded. Load an ESRGAN model first using POST /upscaler/load", 400);
            return;
        }
        
        auto body = parse_json_body(req);
        std::string job_id = queue_manager_.add_job(GenerationType::Upscale, body);
        
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

        // Generate output path if not provided
        // Default naming: modelname.quanttype.gguf
        std::string output_path;
        if (body.contains("output_path") && !body["output_path"].get<std::string>().empty()) {
            output_path = body["output_path"].get<std::string>();
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

        std::string job_id = queue_manager_.add_job(GenerationType::Convert, body);

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

    // Pagination parameters
    size_t limit = 20;
    size_t page = 1;

    if (req.has_param("limit")) {
        try {
            limit = std::stoul(req.get_param_value("limit"));
        } catch (...) {
            limit = 20;
        }
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
                items.push_back(job.to_json());
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
        items.push_back(job.to_json());
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

    send_json(res, job->to_json());
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

    // Serve the thumbnail
    std::ifstream file(thumb_path, std::ios::binary);
    if (!file) {
        send_error(res, "Cannot read thumbnail", 500);
        return;
    }

    std::ostringstream oss;
    oss << file.rdbuf();
    res.set_content(oss.str(), "image/jpeg");
}

std::string RequestHandlers::generate_directory_html(const std::string& dir_path, const std::string& url_path,
                                                     const std::string& sort_by, bool sort_asc) {
    std::ostringstream html;

    // Build sort URL helper
    auto make_sort_url = [&](const std::string& col) -> std::string {
        std::string new_order = (sort_by == col && sort_asc) ? "desc" : "asc";
        return url_path + "?sort=" + col + "&order=" + new_order;
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
            max-width: 95%;
            max-height: 95%;
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

    // Collect entries with sizes (skip .thumbs folder)
    struct Entry {
        std::string name;
        bool is_dir;
        bool is_media;
        bool is_image;
        bool is_video;
        size_t size;
        std::time_t mtime;
    };
    std::vector<Entry> entries;
    size_t total_size = 0;
    int dir_count = 0, file_count = 0;

    try {
        for (const auto& entry : fs::directory_iterator(dir_path)) {
            Entry e;
            e.name = entry.path().filename().string();

            // Skip .thumbs directories
            if (e.name == ".thumbs") continue;

            e.is_dir = entry.is_directory();
            e.is_image = !e.is_dir && is_image_file(entry.path().string());
            e.is_video = !e.is_dir && is_video_file(entry.path().string());
            e.is_media = e.is_image || e.is_video;

            if (e.is_dir) {
                e.size = calculate_directory_size(entry.path().string());
                // Use content-based mtime for directories (excludes .thumbs changes)
                e.mtime = get_directory_content_mtime(entry.path().string());
                dir_count++;
            } else {
                e.size = entry.file_size();
                auto ftime = entry.last_write_time();
                auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
                e.mtime = std::chrono::system_clock::to_time_t(sctp);
                file_count++;
            }
            total_size += e.size;

            entries.push_back(e);
        }
    } catch (const std::exception& ex) {
        html << "<p style='color:#ff6b6b;'>Error reading directory: " << ex.what() << "</p>";
    }

    // Stats bar
    html << R"(    <div class="stats">
        <div class="stat"><span class="stat-value">)" << dir_count << R"(</span> folders</div>
        <div class="stat"><span class="stat-value">)" << file_count << R"(</span> files</div>
        <div class="stat"><span class="stat-value">)" << format_file_size(total_size) << R"(</span> total</div>
    </div>
)";

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

    // Generate rows
    for (const auto& entry : entries) {
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
        html << "                <td class=\"size\" data-label=\"Size\">" << format_file_size(entry.size) << "</td>\n";
        html << "                <td class=\"date date-col\" data-label=\"Modified\">" << date_buf << "</td>\n";
        html << "            </tr>\n";
    }

    html << R"HTML(        </tbody>
    </table>
</div>

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

        // Directory: generate listing HTML
        std::string html = generate_directory_html(full_path.string(), url_path, sort_by, sort_asc);
        res.set_content(html, "text/html");
    } else {
        // File: serve it
        std::ifstream file(full_path, std::ios::binary);
        if (!file) {
            send_error(res, "Cannot read file", 500);
            return;
        }

        // Read file content
        std::ostringstream oss;
        oss << file.rdbuf();
        std::string content = oss.str();

        // Set content type and serve
        std::string mime_type = get_mime_type(full_path.string());
        res.set_content(content, mime_type);
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

} // namespace sdcpp
