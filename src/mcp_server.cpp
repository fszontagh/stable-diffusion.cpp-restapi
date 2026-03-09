#include "mcp_server.hpp"
#include "model_manager.hpp"
#include "queue_manager.hpp"
#include "memory_utils.hpp"

#include <iostream>
#include <fstream>

namespace sdcpp {

using json = nlohmann::json;

// ─── Constructor ─────────────────────────────────────────────────────────────

McpServer::McpServer(httplib::Server& server, ModelManager& model_manager, QueueManager& queue_manager)
    : server_(server), model_manager_(model_manager), queue_manager_(queue_manager) {}

// ─── Endpoint Registration ───────────────────────────────────────────────────

void McpServer::register_endpoint() {
    server_.Post("/mcp", [this](const httplib::Request& req, httplib::Response& res) {
        handle_request(req, res);
    });
    std::cout << "MCP server registered at POST /mcp" << std::endl;
}

// ─── JSON-RPC Helpers ────────────────────────────────────────────────────────

json McpServer::make_response(const json& id, const json& result) {
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", result}
    };
}

json McpServer::make_error(const json& id, int code, const std::string& message) {
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"error", {
            {"code", code},
            {"message", message}
        }}
    };
}

json McpServer::make_tool_result(const std::string& text, bool is_error) {
    json result = {
        {"content", json::array({
            {{"type", "text"}, {"text", text}}
        })}
    };
    if (is_error) {
        result["isError"] = true;
    }
    return result;
}

// ─── JSON-RPC 2.0 Dispatcher ────────────────────────────────────────────────

void McpServer::handle_request(const httplib::Request& req, httplib::Response& res) {
    try {
        json body;
        try {
            body = json::parse(req.body);
        } catch (const json::parse_error& e) {
            res.status = 400;
            res.set_content(make_error(nullptr, -32700, "Parse error: " + std::string(e.what())).dump(), "application/json");
            return;
        }

        // Reject batch requests (arrays)
        if (body.is_array()) {
            res.status = 400;
            res.set_content(make_error(nullptr, -32600, "Batch requests are not supported").dump(), "application/json");
            return;
        }

        // Validate jsonrpc field
        if (!body.contains("jsonrpc") || body["jsonrpc"] != "2.0") {
            res.status = 400;
            res.set_content(make_error(nullptr, -32600, "Invalid Request: missing or wrong jsonrpc version").dump(), "application/json");
            return;
        }

        // Validate method field
        if (!body.contains("method") || !body["method"].is_string()) {
            res.status = 400;
            res.set_content(make_error(nullptr, -32600, "Invalid Request: missing method").dump(), "application/json");
            return;
        }

        std::string method = body["method"].get<std::string>();
        json params = body.value("params", json::object());

        // Notification (no "id" field) → 204 No Content
        if (!body.contains("id")) {
            res.status = 204;
            return;
        }

        json id = body["id"];
        json response;

        try {
            if (method == "initialize") {
                response = make_response(id, handle_initialize(params));
            } else if (method == "ping") {
                response = make_response(id, handle_ping());
            } else if (method == "tools/list") {
                response = make_response(id, handle_list_tools());
            } else if (method == "tools/call") {
                response = make_response(id, handle_call_tool(params));
            } else if (method == "resources/list") {
                response = make_response(id, handle_list_resources());
            } else if (method == "resources/read") {
                response = make_response(id, handle_read_resource(params));
            } else {
                response = make_error(id, -32601, "Method not found: " + method);
            }
        } catch (const std::exception& e) {
            response = make_error(id, -32603, "Internal error: " + std::string(e.what()));
        }

        res.status = 200;
        res.set_content(response.dump(), "application/json");

    } catch (const std::exception& e) {
        res.status = 500;
        res.set_content(make_error(nullptr, -32603, "Internal error: " + std::string(e.what())).dump(), "application/json");
    }
}

// ─── MCP Protocol Methods ────────────────────────────────────────────────────

json McpServer::handle_initialize(const json& /*params*/) {
    return {
        {"protocolVersion", "2025-06-18"},
        {"capabilities", {
            {"tools", json::object()},
            {"resources", json::object()}
        }},
        {"serverInfo", {
            {"name", "sdcpp-restapi"},
            {"version", SDCPP_VERSION}
        }}
    };
}

json McpServer::handle_ping() {
    return json::object();
}

// ─── Tool Definitions ────────────────────────────────────────────────────────

json McpServer::handle_list_tools() {
    json tools = json::array();

    // 1. generate_image
    tools.push_back({
        {"name", "generate_image"},
        {"description", "Generate an image from a text prompt (txt2img). Requires a model to be loaded."},
        {"inputSchema", {
            {"type", "object"},
            {"required", json::array({"prompt"})},
            {"properties", {
                {"prompt", {{"type", "string"}, {"description", "Text prompt describing the image to generate"}}},
                {"negative_prompt", {{"type", "string"}, {"description", "Negative prompt to avoid certain elements"}}},
                {"width", {{"type", "integer"}, {"description", "Image width in pixels"}}},
                {"height", {{"type", "integer"}, {"description", "Image height in pixels"}}},
                {"steps", {{"type", "integer"}, {"description", "Number of sampling steps"}}},
                {"cfg_scale", {{"type", "number"}, {"description", "Classifier-free guidance scale"}}},
                {"seed", {{"type", "integer"}, {"description", "Random seed (-1 for random)"}}},
                {"batch_count", {{"type", "integer"}, {"description", "Number of images to generate"}}},
                {"sampler", {{"type", "string"}, {"description", "Sampler name (e.g., euler, euler_a, dpm++2m)"}}},
                {"scheduler", {{"type", "string"}, {"description", "Scheduler name (e.g., normal, karras, sgm_uniform)"}}}
            }}
        }}
    });

    // 2. generate_image_from_image
    tools.push_back({
        {"name", "generate_image_from_image"},
        {"description", "Generate an image from an input image and text prompt (img2img). Requires a model to be loaded."},
        {"inputSchema", {
            {"type", "object"},
            {"required", json::array({"prompt", "init_image_base64"})},
            {"properties", {
                {"prompt", {{"type", "string"}, {"description", "Text prompt describing the desired output"}}},
                {"init_image_base64", {{"type", "string"}, {"description", "Base64-encoded input image"}}},
                {"negative_prompt", {{"type", "string"}, {"description", "Negative prompt"}}},
                {"strength", {{"type", "number"}, {"description", "Denoising strength (0.0-1.0)"}}},
                {"steps", {{"type", "integer"}, {"description", "Number of sampling steps"}}},
                {"cfg_scale", {{"type", "number"}, {"description", "Classifier-free guidance scale"}}},
                {"seed", {{"type", "integer"}, {"description", "Random seed (-1 for random)"}}}
            }}
        }}
    });

    // 3. generate_video
    tools.push_back({
        {"name", "generate_video"},
        {"description", "Generate a video from a text prompt (txt2vid). Requires a video-capable model to be loaded."},
        {"inputSchema", {
            {"type", "object"},
            {"required", json::array({"prompt"})},
            {"properties", {
                {"prompt", {{"type", "string"}, {"description", "Text prompt describing the video to generate"}}},
                {"negative_prompt", {{"type", "string"}, {"description", "Negative prompt"}}},
                {"width", {{"type", "integer"}, {"description", "Video width in pixels"}}},
                {"height", {{"type", "integer"}, {"description", "Video height in pixels"}}},
                {"steps", {{"type", "integer"}, {"description", "Number of sampling steps"}}},
                {"cfg_scale", {{"type", "number"}, {"description", "Classifier-free guidance scale"}}},
                {"seed", {{"type", "integer"}, {"description", "Random seed (-1 for random)"}}}
            }}
        }}
    });

    // 4. upscale_image
    tools.push_back({
        {"name", "upscale_image"},
        {"description", "Upscale an image using a loaded ESRGAN upscaler model."},
        {"inputSchema", {
            {"type", "object"},
            {"required", json::array({"image_base64"})},
            {"properties", {
                {"image_base64", {{"type", "string"}, {"description", "Base64-encoded image to upscale"}}}
            }}
        }}
    });

    // 5. load_model
    tools.push_back({
        {"name", "load_model"},
        {"description", "Load a Stable Diffusion model for image/video generation."},
        {"inputSchema", {
            {"type", "object"},
            {"required", json::array({"model_name"})},
            {"properties", {
                {"model_name", {{"type", "string"}, {"description", "Name of the model to load (relative path from models directory)"}}},
                {"model_type", {{"type", "string"}, {"description", "Model type: checkpoint, diffusion, vae, lora, clip, t5, embedding, controlnet, llm, esrgan, taesd"}}},
                {"vae", {{"type", "string"}, {"description", "VAE model name"}}},
                {"clip_l", {{"type", "string"}, {"description", "CLIP-L model name"}}},
                {"clip_g", {{"type", "string"}, {"description", "CLIP-G model name"}}},
                {"t5xxl", {{"type", "string"}, {"description", "T5-XXL model name"}}},
                {"llm", {{"type", "string"}, {"description", "LLM model name for multimodal architectures"}}},
                {"flash_attn", {{"type", "boolean"}, {"description", "Enable flash attention"}}},
                {"n_threads", {{"type", "integer"}, {"description", "Number of CPU threads (-1 for auto)"}}}
            }}
        }}
    });

    // 6. unload_model
    tools.push_back({
        {"name", "unload_model"},
        {"description", "Unload the currently loaded model to free memory."},
        {"inputSchema", {
            {"type", "object"},
            {"properties", json::object()}
        }}
    });

    // 7. list_models
    tools.push_back({
        {"name", "list_models"},
        {"description", "List available models, optionally filtered by type."},
        {"inputSchema", {
            {"type", "object"},
            {"properties", {
                {"model_type", {{"type", "string"}, {"description", "Filter by model type: checkpoint, diffusion, vae, lora, clip, t5, embedding, controlnet, llm, esrgan, taesd"}}}
            }}
        }}
    });

    // 8. get_job_status
    tools.push_back({
        {"name", "get_job_status"},
        {"description", "Get the status and details of a generation job."},
        {"inputSchema", {
            {"type", "object"},
            {"required", json::array({"job_id"})},
            {"properties", {
                {"job_id", {{"type", "string"}, {"description", "The job UUID to check"}}}
            }}
        }}
    });

    // 9. cancel_job
    tools.push_back({
        {"name", "cancel_job"},
        {"description", "Cancel a pending or processing generation job."},
        {"inputSchema", {
            {"type", "object"},
            {"required", json::array({"job_id"})},
            {"properties", {
                {"job_id", {{"type", "string"}, {"description", "The job UUID to cancel"}}}
            }}
        }}
    });

    tools.push_back({
        {"name", "search_queue"},
        {"description", "Search and filter the job queue with pagination. Returns max 10 items per page. Use this instead of the queue resource for filtered or paginated access."},
        {"inputSchema", {
            {"type", "object"},
            {"properties", {
                {"search", {{"type", "string"}, {"description", "Search in prompt and negative_prompt text (case-insensitive)"}}},
                {"status", {{"type", "string"}, {"enum", {"pending", "processing", "completed", "failed", "cancelled"}}, {"description", "Filter by job status"}}},
                {"type", {{"type", "string"}, {"enum", {"txt2img", "img2img", "txt2vid", "upscale"}}, {"description", "Filter by generation type"}}},
                {"architecture", {{"type", "string"}, {"description", "Filter by model architecture (partial match, e.g. 'flux', 'z-image')"}}},
                {"model", {{"type", "string"}, {"description", "Filter by model name (partial match)"}}},
                {"before", {{"type", "integer"}, {"description", "Only jobs created before this Unix timestamp"}}},
                {"after", {{"type", "integer"}, {"description", "Only jobs created after this Unix timestamp"}}},
                {"limit", {{"type", "integer"}, {"description", "Max items to return (1-10, default 10)"}}},
                {"offset", {{"type", "integer"}, {"description", "Number of items to skip for pagination"}}}
            }}
        }}
    });

    return {{"tools", tools}};
}

// ─── Tool Dispatcher ─────────────────────────────────────────────────────────

json McpServer::handle_call_tool(const json& params) {
    if (!params.contains("name") || !params["name"].is_string()) {
        return make_tool_result("Missing or invalid tool name", true);
    }

    std::string name = params["name"].get<std::string>();
    json args = params.value("arguments", json::object());

    if (name == "generate_image") return tool_generate_image(args);
    if (name == "generate_image_from_image") return tool_generate_image_from_image(args);
    if (name == "generate_video") return tool_generate_video(args);
    if (name == "upscale_image") return tool_upscale_image(args);
    if (name == "load_model") return tool_load_model(args);
    if (name == "unload_model") return tool_unload_model(args);
    if (name == "list_models") return tool_list_models(args);
    if (name == "get_job_status") return tool_get_job_status(args);
    if (name == "cancel_job") return tool_cancel_job(args);
    if (name == "search_queue") return tool_search_queue(args);

    return make_tool_result("Unknown tool: " + name, true);
}

// ─── Tool Implementations ────────────────────────────────────────────────────

json McpServer::tool_generate_image(const json& args) {
    if (!model_manager_.is_model_loaded()) {
        return make_tool_result("No model loaded. Use load_model first.", true);
    }
    if (!args.contains("prompt") || !args["prompt"].is_string()) {
        return make_tool_result("Missing required parameter: prompt", true);
    }

    try {
        std::string job_id = queue_manager_.add_job(GenerationType::Text2Image, args);
        json result = {
            {"job_id", job_id},
            {"status", "pending"},
            {"message", "Image generation job queued"}
        };
        return make_tool_result(result.dump());
    } catch (const std::exception& e) {
        return make_tool_result("Failed to queue job: " + std::string(e.what()), true);
    }
}

json McpServer::tool_generate_image_from_image(const json& args) {
    if (!model_manager_.is_model_loaded()) {
        return make_tool_result("No model loaded. Use load_model first.", true);
    }
    if (!args.contains("prompt") || !args["prompt"].is_string()) {
        return make_tool_result("Missing required parameter: prompt", true);
    }
    if (!args.contains("init_image_base64") || !args["init_image_base64"].is_string()) {
        return make_tool_result("Missing required parameter: init_image_base64", true);
    }

    try {
        std::string job_id = queue_manager_.add_job(GenerationType::Image2Image, args);
        json result = {
            {"job_id", job_id},
            {"status", "pending"},
            {"message", "Image-to-image generation job queued"}
        };
        return make_tool_result(result.dump());
    } catch (const std::exception& e) {
        return make_tool_result("Failed to queue job: " + std::string(e.what()), true);
    }
}

json McpServer::tool_generate_video(const json& args) {
    if (!model_manager_.is_model_loaded()) {
        return make_tool_result("No model loaded. Use load_model first.", true);
    }
    if (!args.contains("prompt") || !args["prompt"].is_string()) {
        return make_tool_result("Missing required parameter: prompt", true);
    }

    try {
        std::string job_id = queue_manager_.add_job(GenerationType::Text2Video, args);
        json result = {
            {"job_id", job_id},
            {"status", "pending"},
            {"message", "Video generation job queued"}
        };
        return make_tool_result(result.dump());
    } catch (const std::exception& e) {
        return make_tool_result("Failed to queue job: " + std::string(e.what()), true);
    }
}

json McpServer::tool_upscale_image(const json& args) {
    if (!args.contains("image_base64") || !args["image_base64"].is_string()) {
        return make_tool_result("Missing required parameter: image_base64", true);
    }

    try {
        std::string job_id = queue_manager_.add_job(GenerationType::Upscale, args);
        json result = {
            {"job_id", job_id},
            {"status", "pending"},
            {"message", "Upscale job queued"}
        };
        return make_tool_result(result.dump());
    } catch (const std::exception& e) {
        return make_tool_result("Failed to queue job: " + std::string(e.what()), true);
    }
}

json McpServer::tool_load_model(const json& args) {
    if (!args.contains("model_name") || !args["model_name"].is_string()) {
        return make_tool_result("Missing required parameter: model_name", true);
    }

    try {
        auto params = ModelLoadParams::from_json(args);
        model_manager_.load_model(params);

        auto loaded_info = model_manager_.get_loaded_models_info();
        json result = {
            {"success", true},
            {"message", "Model loaded successfully"},
            {"model_name", loaded_info["model_name"]},
            {"model_type", loaded_info["model_type"]},
            {"loaded_components", loaded_info["loaded_components"]}
        };
        return make_tool_result(result.dump());
    } catch (const std::exception& e) {
        return make_tool_result("Failed to load model: " + std::string(e.what()), true);
    }
}

json McpServer::tool_unload_model(const json& /*args*/) {
    try {
        model_manager_.unload_model();
        json result = {
            {"success", true},
            {"message", "Model unloaded"}
        };
        return make_tool_result(result.dump());
    } catch (const std::exception& e) {
        return make_tool_result("Failed to unload model: " + std::string(e.what()), true);
    }
}

json McpServer::tool_list_models(const json& args) {
    try {
        ModelFilter filter;
        if (args.contains("model_type") && args["model_type"].is_string()) {
            filter.type = string_to_model_type(args["model_type"].get<std::string>());
        }

        json models = model_manager_.get_models_json(filter);
        return make_tool_result(models.dump());
    } catch (const std::exception& e) {
        return make_tool_result("Failed to list models: " + std::string(e.what()), true);
    }
}

json McpServer::tool_get_job_status(const json& args) {
    if (!args.contains("job_id") || !args["job_id"].is_string()) {
        return make_tool_result("Missing required parameter: job_id", true);
    }

    std::string job_id = args["job_id"].get<std::string>();
    auto job = queue_manager_.get_job(job_id);
    if (!job.has_value()) {
        return make_tool_result("Job not found: " + job_id, true);
    }

    return make_tool_result(job->to_json().dump());
}

json McpServer::tool_cancel_job(const json& args) {
    if (!args.contains("job_id") || !args["job_id"].is_string()) {
        return make_tool_result("Missing required parameter: job_id", true);
    }

    std::string job_id = args["job_id"].get<std::string>();
    bool cancelled = queue_manager_.cancel_job(job_id);

    if (cancelled) {
        json result = {
            {"success", true},
            {"message", "Job cancelled"},
            {"job_id", job_id}
        };
        return make_tool_result(result.dump());
    } else {
        return make_tool_result("Failed to cancel job " + job_id + ": job may not be in a cancellable state", true);
    }
}

json McpServer::tool_search_queue(const json& args) {
    QueueFilter filter;

    // Text search in prompts
    if (args.contains("search") && args["search"].is_string()) {
        filter.search = args["search"].get<std::string>();
    }

    // Status filter
    if (args.contains("status") && args["status"].is_string()) {
        std::string status_str = args["status"].get<std::string>();
        if (status_str == "pending") filter.status = QueueStatus::Pending;
        else if (status_str == "processing") filter.status = QueueStatus::Processing;
        else if (status_str == "completed") filter.status = QueueStatus::Completed;
        else if (status_str == "failed") filter.status = QueueStatus::Failed;
        else if (status_str == "cancelled") filter.status = QueueStatus::Cancelled;
    }

    // Generation type filter
    if (args.contains("type") && args["type"].is_string()) {
        std::string type_str = args["type"].get<std::string>();
        if (type_str == "txt2img") filter.type = GenerationType::Text2Image;
        else if (type_str == "img2img") filter.type = GenerationType::Image2Image;
        else if (type_str == "txt2vid") filter.type = GenerationType::Text2Video;
        else if (type_str == "upscale") filter.type = GenerationType::Upscale;
    }

    // Architecture filter (partial match)
    if (args.contains("architecture") && args["architecture"].is_string()) {
        filter.architecture = args["architecture"].get<std::string>();
    }

    // Model name filter (partial match)
    if (args.contains("model") && args["model"].is_string()) {
        filter.model = args["model"].get<std::string>();
    }

    // Date range
    if (args.contains("before") && args["before"].is_number_integer()) {
        filter.before_timestamp = args["before"].get<int64_t>();
    }
    if (args.contains("after") && args["after"].is_number_integer()) {
        filter.after_timestamp = args["after"].get<int64_t>();
    }

    // Pagination (cap at 10)
    size_t limit = 10;
    if (args.contains("limit") && args["limit"].is_number_integer()) {
        limit = std::min(static_cast<size_t>(args["limit"].get<int>()), static_cast<size_t>(10));
        if (limit < 1) limit = 1;
    }
    filter.limit = limit;

    if (args.contains("offset") && args["offset"].is_number_integer()) {
        filter.offset = std::max(0, args["offset"].get<int>());
    }

    auto result = queue_manager_.get_jobs_paginated(filter);

    json items = json::array();
    for (const auto& item : result.items) {
        items.push_back(item.to_json());
    }

    json response = {
        {"items", items},
        {"total_count", result.total_count},
        {"returned_count", result.filtered_count},
        {"offset", result.offset},
        {"limit", result.limit},
        {"has_more", result.has_more}
    };

    return make_tool_result(response.dump());
}

// ─── Resource Definitions ────────────────────────────────────────────────────

json McpServer::handle_list_resources() {
    json resources = json::array({
        {
            {"uri", "sdcpp://health"},
            {"name", "Server Health"},
            {"description", "Server health status including loaded model, memory, and feature flags"},
            {"mimeType", "application/json"}
        },
        {
            {"uri", "sdcpp://memory"},
            {"name", "Memory Info"},
            {"description", "System RAM and GPU VRAM usage information"},
            {"mimeType", "application/json"}
        },
        {
            {"uri", "sdcpp://models"},
            {"name", "Available Models"},
            {"description", "List of all available models on disk"},
            {"mimeType", "application/json"}
        },
        {
            {"uri", "sdcpp://models/loaded"},
            {"name", "Loaded Model"},
            {"description", "Information about the currently loaded model and its components"},
            {"mimeType", "application/json"}
        },
        {
            {"uri", "sdcpp://queue"},
            {"name", "Job Queue"},
            {"description", "Recent generation jobs with status"},
            {"mimeType", "application/json"}
        },
        {
            {"uri", "sdcpp://architectures"},
            {"name", "Model Architectures"},
            {"description", "Supported model architectures and their default settings"},
            {"mimeType", "application/json"}
        }
    });

    json resource_templates = json::array({
        {
            {"uriTemplate", "sdcpp://queue/{job_id}"},
            {"name", "Job Details"},
            {"description", "Detailed information about a specific generation job"},
            {"mimeType", "application/json"}
        }
    });

    return {
        {"resources", resources},
        {"resourceTemplates", resource_templates}
    };
}

// ─── Resource Dispatcher ─────────────────────────────────────────────────────

json McpServer::handle_read_resource(const json& params) {
    if (!params.contains("uri") || !params["uri"].is_string()) {
        throw std::runtime_error("Missing required parameter: uri");
    }

    std::string uri = params["uri"].get<std::string>();

    json content;
    std::string mime_type = "application/json";

    if (uri == "sdcpp://health") {
        content = resource_health();
    } else if (uri == "sdcpp://memory") {
        content = resource_memory();
    } else if (uri == "sdcpp://models") {
        content = resource_models();
    } else if (uri == "sdcpp://models/loaded") {
        content = resource_models_loaded();
    } else if (uri == "sdcpp://queue") {
        content = resource_queue();
    } else if (uri == "sdcpp://architectures") {
        content = resource_architectures();
    } else if (uri.rfind("sdcpp://queue/", 0) == 0) {
        // Extract job_id from sdcpp://queue/{job_id}
        std::string job_id = uri.substr(std::string("sdcpp://queue/").length());
        if (job_id.empty()) {
            throw std::runtime_error("Missing job_id in URI");
        }
        content = resource_queue_job(job_id);
    } else {
        throw std::runtime_error("Unknown resource URI: " + uri);
    }

    return {
        {"contents", json::array({
            {
                {"uri", uri},
                {"mimeType", mime_type},
                {"text", content.dump()}
            }
        })}
    };
}

// ─── Resource Implementations ────────────────────────────────────────────────

json McpServer::resource_health() {
    auto loaded_info = model_manager_.get_loaded_models_info();
    auto memory_info = get_memory_info();

    // Extract values safely (some fields are null when no model is loaded)
    bool model_loaded = loaded_info.value("model_loaded", false);
    std::string model_name = (loaded_info.contains("model_name") && loaded_info["model_name"].is_string())
        ? loaded_info["model_name"].get<std::string>() : "";
    std::string architecture = (loaded_info.contains("model_architecture") && loaded_info["model_architecture"].is_string())
        ? loaded_info["model_architecture"].get<std::string>() : "";
    bool upscaler_loaded = loaded_info.value("upscaler_loaded", false);
    std::string upscaler_name = (loaded_info.contains("upscaler_name") && loaded_info["upscaler_name"].is_string())
        ? loaded_info["upscaler_name"].get<std::string>() : "";

    json health = {
        {"status", "ok"},
        {"model_loaded", model_loaded},
        {"model_name", model_name},
        {"architecture", architecture},
        {"upscaler_loaded", upscaler_loaded},
        {"upscaler_name", upscaler_name},
        {"memory", memory_info.to_json()},
        {"features", {
#ifdef SDCPP_EXPERIMENTAL_OFFLOAD
            {"experimental_offload", true},
#else
            {"experimental_offload", false},
#endif
#ifdef SDCPP_WEBSOCKET_ENABLED
            {"websocket", true},
#else
            {"websocket", false},
#endif
#ifdef SDCPP_ASSISTANT_ENABLED
            {"assistant", true}
#else
            {"assistant", false}
#endif
        }}
    };

    return health;
}

json McpServer::resource_memory() {
    auto memory_info = get_memory_info();
    return memory_info.to_json();
}

json McpServer::resource_models() {
    return model_manager_.get_models_json();
}

json McpServer::resource_models_loaded() {
    return model_manager_.get_loaded_models_info();
}

json McpServer::resource_queue() {
    QueueFilter filter;
    filter.limit = 10;
    auto result = queue_manager_.get_jobs_paginated(filter);

    json items = json::array();
    for (const auto& item : result.items) {
        items.push_back(item.to_json());
    }

    return {
        {"items", items},
        {"total_count", result.total_count},
        {"has_more", result.has_more},
        {"note", "Showing last 10 items. Use the search_queue tool for filtering, search, and pagination."}
    };
}

json McpServer::resource_queue_job(const std::string& job_id) {
    auto job = queue_manager_.get_job(job_id);
    if (!job.has_value()) {
        throw std::runtime_error("Job not found: " + job_id);
    }
    return job->to_json();
}

json McpServer::resource_architectures() {
    // Read the model architectures JSON file
    std::ifstream file("data/model_architectures.json");
    if (!file.is_open()) {
        throw std::runtime_error("Could not open data/model_architectures.json");
    }

    json architectures;
    try {
        file >> architectures;
    } catch (const json::parse_error& e) {
        throw std::runtime_error("Failed to parse model_architectures.json: " + std::string(e.what()));
    }

    return architectures;
}

} // namespace sdcpp
