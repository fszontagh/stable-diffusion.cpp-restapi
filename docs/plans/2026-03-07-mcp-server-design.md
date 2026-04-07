# MCP Server Support - Design & Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add MCP support so any MCP-compatible AI client can discover and use the server's image/video generation tools and resources.

**Architecture:** Standalone `McpServer` class with a single `POST /mcp` endpoint on the existing cpp-httplib server. JSON-RPC 2.0 dispatcher routes to tool handlers (generate, load, queue) and resource handlers (health, models, memory). Direct access to existing `ModelManager` and `QueueManager` — no duplication.

**Tech Stack:** C++20, cpp-httplib (existing), nlohmann/json (existing), no new dependencies.

---

## Design Decisions

- **Target:** General MCP client support (not Claude Desktop-specific)
- **Capabilities:** Tools + Resources
- **Transport:** Streamable HTTP only (POST /mcp endpoint)
- **Build:** Compile-time optional via `SDCPP_MCP` CMake option, enabled by default
- **Approach:** Standalone McpServer class, single endpoint, direct access to existing managers

## Architecture

### New Files

- `include/mcp_server.hpp` - McpServer class declaration
- `src/mcp_server.cpp` - Implementation

### Class Design

```
McpServer
  References: ModelManager&, QueueManager&, httplib::Server&
  register_endpoint()     - attaches POST /mcp to httplib
  handle_request()        - JSON-RPC 2.0 dispatcher
  handle_initialize()     - MCP handshake
  handle_list_tools()     - returns tool definitions
  handle_call_tool()      - dispatches to tool handlers
  handle_list_resources() - returns resource list
  handle_read_resource()  - dispatches to resource handlers
```

McpServer receives references to the same ModelManager and QueueManager that REST handlers use.

### Build Integration

```cmake
option(SDCPP_MCP "Build MCP (Model Context Protocol) server support" ON)

if(SDCPP_MCP)
    list(APPEND SDCPP_SOURCES src/mcp_server.cpp)
    target_compile_definitions(sdcpp-restapi PRIVATE SDCPP_MCP_ENABLED=1)
endif()
```

### main.cpp Integration

```cpp
#ifdef SDCPP_MCP_ENABLED
    McpServer mcp_server(server, model_manager, queue_manager);
    mcp_server.register_endpoint();
#endif
```

## JSON-RPC 2.0 Protocol

Single endpoint: `POST /mcp`
Response: `Content-Type: application/json` (no SSE streaming)
Session: Stateless

### MCP Methods

| Method | Purpose |
|--------|---------|
| `initialize` | Client handshake - returns server info and capabilities |
| `notifications/initialized` | Client confirms init (no response) |
| `tools/list` | Returns tool definitions with input schemas |
| `tools/call` | Executes a tool |
| `resources/list` | Returns available resources |
| `resources/read` | Reads a specific resource |
| `ping` | Health check |

### Error Codes

| Code | Meaning |
|------|---------|
| -32700 | Parse error (invalid JSON) |
| -32600 | Invalid request |
| -32601 | Method not found |
| -32602 | Invalid params |
| -32603 | Internal error |

## Tools

| Tool | Description | Key Parameters |
|------|-------------|----------------|
| `generate_image` | Text-to-image (queued) | prompt, negative_prompt, width, height, steps, cfg_scale, sampler, scheduler, seed, batch_count |
| `generate_image_from_image` | Image-to-image (queued) | prompt, init_image_base64, strength, + above |
| `generate_video` | Text-to-video (queued) | prompt, width, height, steps, video_frames, fps, cfg_scale |
| `upscale_image` | ESRGAN upscaling (queued) | image_base64, upscale_factor |
| `load_model` | Load model with components | model_name, model_type, vae, clip_l, clip_g, t5xxl, llm, options |
| `unload_model` | Unload current model | (none) |
| `list_models` | List available models | model_type (optional) |
| `get_job_status` | Check job progress/result | job_id |
| `cancel_job` | Cancel pending/running job | job_id |

All tools return JSON-stringified responses matching the REST API format. Generation tools return `{"job_id": "..."}`.

## Resources

| URI | Description |
|-----|-------------|
| `sdcpp://health` | Server status (same as GET /health) |
| `sdcpp://memory` | Memory usage (same as GET /memory) |
| `sdcpp://models` | Available models (same as GET /models) |
| `sdcpp://models/loaded` | Currently loaded model details |
| `sdcpp://queue` | Recent queue items (last 20) |
| `sdcpp://queue/{job_id}` | Specific job details (resource template) |
| `sdcpp://architectures` | Architecture presets |

All resources return `application/json` content.

## Error Handling

- No model loaded: JSON-RPC error -32603 "No model loaded"
- Invalid params: JSON-RPC error -32602 with descriptive message
- Unknown tool/resource: JSON-RPC error -32601
- Notifications (no id): Process silently, no response
- Batch requests: Not supported initially, return error for JSON arrays

## Documentation Updates

- README.md: Add MCP section with build option and usage
- CLAUDE.md: Add MCP pattern documentation

---

# Implementation Plan

## Task 1: CMake Build Integration

**Files:**
- Modify: `CMakeLists.txt` (around line 52, near other options; around line 266, near other source additions; around line 336, near other compile defs)

**Step 1: Add CMake option and source**

Add after the `SD_EXPERIMENTAL_OFFLOAD` option (line 53):

```cmake
option(SDCPP_MCP "Build MCP (Model Context Protocol) server support" ON)
```

Add MCP source conditionally after the WebSocket block (after line 272):

```cmake
# Add MCP server source only if enabled
if(SDCPP_MCP)
    list(APPEND SDCPP_SOURCES src/mcp_server.cpp)
endif()
```

Add compile definition after the WebSocket compile definition block (after line 338):

```cmake
# MCP compile definition
if(SDCPP_MCP)
    target_compile_definitions(sdcpp-restapi PRIVATE SDCPP_MCP_ENABLED=1)
endif()
```

Add to the configuration summary (after line 385):

```cmake
message(STATUS "MCP Server:      ${SDCPP_MCP}")
```

**Step 2: Verify cmake configures without errors**

Run: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSD_CUDA=ON`
Expected: Configuration succeeds, shows `MCP Server: ON` in summary. Build will fail (source doesn't exist yet — that's expected).

**Step 3: Commit**

```bash
git add CMakeLists.txt
git commit -m "feat(mcp): add SDCPP_MCP cmake build option"
```

---

## Task 2: McpServer Header

**Files:**
- Create: `include/mcp_server.hpp`

**Step 1: Create the header**

```cpp
#pragma once

#include "httplib_compat.h"
#include <nlohmann/json.hpp>
#include <string>
#include <functional>
#include <unordered_map>

namespace sdcpp {

// Forward declarations
class ModelManager;
class QueueManager;

/**
 * MCP (Model Context Protocol) Server
 *
 * Implements Streamable HTTP transport for MCP over a single POST /mcp endpoint.
 * Handles JSON-RPC 2.0 messages for tool execution and resource reading.
 */
class McpServer {
public:
    McpServer(httplib::Server& server, ModelManager& model_manager, QueueManager& queue_manager);

    /** Register the POST /mcp endpoint on the HTTP server */
    void register_endpoint();

private:
    // JSON-RPC 2.0 dispatcher
    void handle_request(const httplib::Request& req, httplib::Response& res);

    // MCP protocol methods
    nlohmann::json handle_initialize(const nlohmann::json& params);
    nlohmann::json handle_ping();
    nlohmann::json handle_list_tools();
    nlohmann::json handle_call_tool(const nlohmann::json& params);
    nlohmann::json handle_list_resources();
    nlohmann::json handle_read_resource(const nlohmann::json& params);

    // Tool implementations
    nlohmann::json tool_generate_image(const nlohmann::json& args);
    nlohmann::json tool_generate_image_from_image(const nlohmann::json& args);
    nlohmann::json tool_generate_video(const nlohmann::json& args);
    nlohmann::json tool_upscale_image(const nlohmann::json& args);
    nlohmann::json tool_load_model(const nlohmann::json& args);
    nlohmann::json tool_unload_model(const nlohmann::json& args);
    nlohmann::json tool_list_models(const nlohmann::json& args);
    nlohmann::json tool_get_job_status(const nlohmann::json& args);
    nlohmann::json tool_cancel_job(const nlohmann::json& args);

    // Resource implementations
    nlohmann::json resource_health();
    nlohmann::json resource_memory();
    nlohmann::json resource_models();
    nlohmann::json resource_models_loaded();
    nlohmann::json resource_queue();
    nlohmann::json resource_queue_job(const std::string& job_id);
    nlohmann::json resource_architectures();

    // JSON-RPC helpers
    nlohmann::json make_response(const nlohmann::json& id, const nlohmann::json& result);
    nlohmann::json make_error(const nlohmann::json& id, int code, const std::string& message);
    nlohmann::json make_tool_result(const std::string& text, bool is_error = false);

    httplib::Server& server_;
    ModelManager& model_manager_;
    QueueManager& queue_manager_;
};

} // namespace sdcpp
```

**Step 2: Commit**

```bash
git add include/mcp_server.hpp
git commit -m "feat(mcp): add McpServer header with tool and resource declarations"
```

---

## Task 3: McpServer Core — JSON-RPC Dispatcher + Initialize/Ping

**Files:**
- Create: `src/mcp_server.cpp`

**Step 1: Implement the core dispatcher and protocol methods**

```cpp
#include "mcp_server.hpp"
#include "model_manager.hpp"
#include "queue_manager.hpp"
#include "memory_utils.hpp"

#include <iostream>

namespace sdcpp {

McpServer::McpServer(httplib::Server& server, ModelManager& model_manager, QueueManager& queue_manager)
    : server_(server), model_manager_(model_manager), queue_manager_(queue_manager) {}

void McpServer::register_endpoint() {
    server_.Post("/mcp", [this](const httplib::Request& req, httplib::Response& res) {
        handle_request(req, res);
    });
    std::cout << "MCP server registered at POST /mcp" << std::endl;
}

// ── JSON-RPC 2.0 Helpers ────────────────────────────────────────

nlohmann::json McpServer::make_response(const nlohmann::json& id, const nlohmann::json& result) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
}

nlohmann::json McpServer::make_error(const nlohmann::json& id, int code, const std::string& message) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
}

nlohmann::json McpServer::make_tool_result(const std::string& text, bool is_error) {
    nlohmann::json result;
    result["content"] = nlohmann::json::array({{{"type", "text"}, {"text", text}}});
    if (is_error) {
        result["isError"] = true;
    }
    return result;
}

// ── Main Dispatcher ─────────────────────────────────────────────

void McpServer::handle_request(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json body;

    // Parse JSON body
    try {
        body = nlohmann::json::parse(req.body);
    } catch (const nlohmann::json::exception&) {
        auto error = make_error(nullptr, -32700, "Parse error: invalid JSON");
        res.set_content(error.dump(), "application/json");
        return;
    }

    // Reject batch requests (JSON arrays)
    if (body.is_array()) {
        auto error = make_error(nullptr, -32600, "Batch requests are not supported");
        res.set_content(error.dump(), "application/json");
        return;
    }

    // Validate JSON-RPC structure
    if (!body.contains("jsonrpc") || body["jsonrpc"] != "2.0" || !body.contains("method")) {
        auto error = make_error(body.value("id", nlohmann::json(nullptr)), -32600, "Invalid JSON-RPC request");
        res.set_content(error.dump(), "application/json");
        return;
    }

    std::string method = body["method"].get<std::string>();
    nlohmann::json params = body.value("params", nlohmann::json::object());
    nlohmann::json id = body.value("id", nlohmann::json(nullptr));

    // Notifications (no id) — process silently, return 204 No Content
    bool is_notification = !body.contains("id");
    if (is_notification) {
        // notifications/initialized is the only one we expect
        res.status = 204;
        return;
    }

    // Dispatch to method handlers
    nlohmann::json response;
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
        response = make_error(id, -32603, std::string("Internal error: ") + e.what());
    }

    res.set_content(response.dump(), "application/json");
}

// ── MCP Protocol Methods ────────────────────────────────────────

nlohmann::json McpServer::handle_initialize(const nlohmann::json& /*params*/) {
    return {
        {"protocolVersion", "2025-06-18"},
        {"capabilities", {
            {"tools", nlohmann::json::object()},
            {"resources", nlohmann::json::object()}
        }},
        {"serverInfo", {
            {"name", "sdcpp-restapi"},
            {"version", SDCPP_VERSION}
        }}
    };
}

nlohmann::json McpServer::handle_ping() {
    return nlohmann::json::object();
}
```

**Step 2: Build to verify core compiles**

Run: `cmake --build build -j$(nproc) 2>&1 | tail -5`
Expected: Compiles (links may fail — tool/resource methods not yet implemented). If linker errors, that's fine — we'll implement them next.

**Step 3: Commit**

```bash
git add src/mcp_server.cpp
git commit -m "feat(mcp): implement JSON-RPC dispatcher with initialize and ping"
```

---

## Task 4: Tool Definitions (tools/list)

**Files:**
- Modify: `src/mcp_server.cpp`

**Step 1: Implement handle_list_tools()**

Add to `src/mcp_server.cpp` after `handle_ping()`:

```cpp
nlohmann::json McpServer::handle_list_tools() {
    nlohmann::json tools = nlohmann::json::array();

    tools.push_back({
        {"name", "generate_image"},
        {"description", "Generate an image from a text prompt. Returns a job ID for async tracking."},
        {"inputSchema", {
            {"type", "object"},
            {"properties", {
                {"prompt", {{"type", "string"}, {"description", "Text prompt describing the image to generate"}}},
                {"negative_prompt", {{"type", "string"}, {"description", "What to avoid in the image"}}},
                {"width", {{"type", "integer"}, {"description", "Image width in pixels"}}},
                {"height", {{"type", "integer"}, {"description", "Image height in pixels"}}},
                {"steps", {{"type", "integer"}, {"description", "Number of sampling steps"}}},
                {"cfg_scale", {{"type", "number"}, {"description", "Classifier-free guidance scale"}}},
                {"sampler", {{"type", "string"}, {"description", "Sampling method (euler, dpm++2m, etc.)"}}},
                {"scheduler", {{"type", "string"}, {"description", "Noise scheduler (normal, karras, etc.)"}}},
                {"seed", {{"type", "integer"}, {"description", "Random seed (-1 for random)"}}},
                {"batch_count", {{"type", "integer"}, {"description", "Number of images to generate"}}}
            }},
            {"required", {"prompt"}}
        }}
    });

    tools.push_back({
        {"name", "generate_image_from_image"},
        {"description", "Generate an image using a source image and text prompt (img2img). Returns a job ID."},
        {"inputSchema", {
            {"type", "object"},
            {"properties", {
                {"prompt", {{"type", "string"}, {"description", "Text prompt"}}},
                {"init_image_base64", {{"type", "string"}, {"description", "Source image as base64-encoded string"}}},
                {"strength", {{"type", "number"}, {"description", "How much to transform the source image (0.0-1.0)"}}},
                {"negative_prompt", {{"type", "string"}, {"description", "What to avoid"}}},
                {"width", {{"type", "integer"}, {"description", "Output width"}}},
                {"height", {{"type", "integer"}, {"description", "Output height"}}},
                {"steps", {{"type", "integer"}, {"description", "Sampling steps"}}},
                {"cfg_scale", {{"type", "number"}, {"description", "Guidance scale"}}},
                {"seed", {{"type", "integer"}, {"description", "Random seed (-1 for random)"}}}
            }},
            {"required", {"prompt", "init_image_base64"}}
        }}
    });

    tools.push_back({
        {"name", "generate_video"},
        {"description", "Generate a video from a text prompt. Returns a job ID."},
        {"inputSchema", {
            {"type", "object"},
            {"properties", {
                {"prompt", {{"type", "string"}, {"description", "Text prompt"}}},
                {"negative_prompt", {{"type", "string"}, {"description", "What to avoid"}}},
                {"width", {{"type", "integer"}, {"description", "Video width"}}},
                {"height", {{"type", "integer"}, {"description", "Video height"}}},
                {"steps", {{"type", "integer"}, {"description", "Sampling steps"}}},
                {"cfg_scale", {{"type", "number"}, {"description", "Guidance scale"}}},
                {"video_frames", {{"type", "integer"}, {"description", "Number of frames"}}},
                {"fps", {{"type", "integer"}, {"description", "Frames per second"}}},
                {"seed", {{"type", "integer"}, {"description", "Random seed (-1 for random)"}}}
            }},
            {"required", {"prompt"}}
        }}
    });

    tools.push_back({
        {"name", "upscale_image"},
        {"description", "Upscale an image using ESRGAN. Requires an upscaler model to be loaded. Returns a job ID."},
        {"inputSchema", {
            {"type", "object"},
            {"properties", {
                {"image_base64", {{"type", "string"}, {"description", "Image to upscale as base64"}}},
                {"upscale_factor", {{"type", "integer"}, {"description", "Upscale factor (default: 4)"}}}
            }},
            {"required", {"image_base64"}}
        }}
    });

    tools.push_back({
        {"name", "load_model"},
        {"description", "Load a diffusion model with optional components (VAE, CLIP, T5, LLM). This must be done before generating images."},
        {"inputSchema", {
            {"type", "object"},
            {"properties", {
                {"model_name", {{"type", "string"}, {"description", "Model filename"}}},
                {"model_type", {{"type", "string"}, {"enum", {"diffusion", "checkpoint"}}, {"description", "Model type"}}},
                {"vae", {{"type", "string"}, {"description", "VAE model filename"}}},
                {"clip_l", {{"type", "string"}, {"description", "CLIP-L text encoder filename"}}},
                {"clip_g", {{"type", "string"}, {"description", "CLIP-G text encoder filename"}}},
                {"t5xxl", {{"type", "string"}, {"description", "T5-XXL text encoder filename"}}},
                {"llm", {{"type", "string"}, {"description", "LLM text encoder filename"}}},
                {"options", {{"type", "object"}, {"description", "Advanced load options (flash_attn, offload_to_cpu, etc.)"}}}
            }},
            {"required", {"model_name"}}
        }}
    });

    tools.push_back({
        {"name", "unload_model"},
        {"description", "Unload the currently loaded model, freeing GPU/system memory."},
        {"inputSchema", {
            {"type", "object"},
            {"properties", nlohmann::json::object()}
        }}
    });

    tools.push_back({
        {"name", "list_models"},
        {"description", "List available models, optionally filtered by type."},
        {"inputSchema", {
            {"type", "object"},
            {"properties", {
                {"model_type", {{"type", "string"}, {"description", "Filter by type: diffusion, checkpoint, vae, clip, t5xxl, lora, controlnet, esrgan, llm, llm_vision"}}}
            }}
        }}
    });

    tools.push_back({
        {"name", "get_job_status"},
        {"description", "Get the status and details of a generation job. Returns progress, outputs, and model settings."},
        {"inputSchema", {
            {"type", "object"},
            {"properties", {
                {"job_id", {{"type", "string"}, {"description", "Job UUID returned by generate/upscale tools"}}}
            }},
            {"required", {"job_id"}}
        }}
    });

    tools.push_back({
        {"name", "cancel_job"},
        {"description", "Cancel a pending or running job."},
        {"inputSchema", {
            {"type", "object"},
            {"properties", {
                {"job_id", {{"type", "string"}, {"description", "Job UUID to cancel"}}}
            }},
            {"required", {"job_id"}}
        }}
    });

    return {{"tools", tools}};
}
```

**Step 2: Commit**

```bash
git add src/mcp_server.cpp
git commit -m "feat(mcp): implement tools/list with 9 tool definitions"
```

---

## Task 5: Tool Implementations (tools/call)

**Files:**
- Modify: `src/mcp_server.cpp`

**Step 1: Implement handle_call_tool() dispatcher and all tool functions**

Add after `handle_list_tools()`:

```cpp
// ── Tool Dispatcher ─────────────────────────────────────────────

nlohmann::json McpServer::handle_call_tool(const nlohmann::json& params) {
    if (!params.contains("name") || !params["name"].is_string()) {
        throw std::runtime_error("Missing required parameter: name");
    }

    std::string name = params["name"].get<std::string>();
    nlohmann::json args = params.value("arguments", nlohmann::json::object());

    if (name == "generate_image") return tool_generate_image(args);
    if (name == "generate_image_from_image") return tool_generate_image_from_image(args);
    if (name == "generate_video") return tool_generate_video(args);
    if (name == "upscale_image") return tool_upscale_image(args);
    if (name == "load_model") return tool_load_model(args);
    if (name == "unload_model") return tool_unload_model(args);
    if (name == "list_models") return tool_list_models(args);
    if (name == "get_job_status") return tool_get_job_status(args);
    if (name == "cancel_job") return tool_cancel_job(args);

    return make_tool_result("Unknown tool: " + name, true);
}

// ── Generation Tools ────────────────────────────────────────────

nlohmann::json McpServer::tool_generate_image(const nlohmann::json& args) {
    if (!model_manager_.is_model_loaded()) {
        return make_tool_result("No model loaded. Use load_model first.", true);
    }
    if (!args.contains("prompt")) {
        return make_tool_result("Missing required parameter: prompt", true);
    }

    std::string job_id = queue_manager_.add_job(GenerationType::Text2Image, args);
    nlohmann::json result = {{"job_id", job_id}, {"status", "pending"}};
    return make_tool_result(result.dump());
}

nlohmann::json McpServer::tool_generate_image_from_image(const nlohmann::json& args) {
    if (!model_manager_.is_model_loaded()) {
        return make_tool_result("No model loaded. Use load_model first.", true);
    }
    if (!args.contains("prompt") || !args.contains("init_image_base64")) {
        return make_tool_result("Missing required parameters: prompt and init_image_base64", true);
    }

    std::string job_id = queue_manager_.add_job(GenerationType::Image2Image, args);
    nlohmann::json result = {{"job_id", job_id}, {"status", "pending"}};
    return make_tool_result(result.dump());
}

nlohmann::json McpServer::tool_generate_video(const nlohmann::json& args) {
    if (!model_manager_.is_model_loaded()) {
        return make_tool_result("No model loaded. Use load_model first.", true);
    }
    if (!args.contains("prompt")) {
        return make_tool_result("Missing required parameter: prompt", true);
    }

    std::string job_id = queue_manager_.add_job(GenerationType::Text2Video, args);
    nlohmann::json result = {{"job_id", job_id}, {"status", "pending"}};
    return make_tool_result(result.dump());
}

nlohmann::json McpServer::tool_upscale_image(const nlohmann::json& args) {
    if (!args.contains("image_base64")) {
        return make_tool_result("Missing required parameter: image_base64", true);
    }

    std::string job_id = queue_manager_.add_job(GenerationType::Upscale, args);
    nlohmann::json result = {{"job_id", job_id}, {"status", "pending"}};
    return make_tool_result(result.dump());
}

// ── Model Tools ─────────────────────────────────────────────────

nlohmann::json McpServer::tool_load_model(const nlohmann::json& args) {
    if (!args.contains("model_name")) {
        return make_tool_result("Missing required parameter: model_name", true);
    }

    try {
        auto params = ModelLoadParams::from_json(args);
        model_manager_.load_model(params);

        auto loaded_info = model_manager_.get_loaded_models_info();
        nlohmann::json result = {
            {"success", true},
            {"model_name", loaded_info["model_name"]},
            {"model_type", loaded_info["model_type"]},
            {"model_architecture", loaded_info.value("model_architecture", nullptr)},
            {"loaded_components", loaded_info["loaded_components"]}
        };
        return make_tool_result(result.dump());
    } catch (const std::exception& e) {
        return make_tool_result(std::string("Failed to load model: ") + e.what(), true);
    }
}

nlohmann::json McpServer::tool_unload_model(const nlohmann::json& /*args*/) {
    model_manager_.unload_model();
    return make_tool_result(R"({"success": true, "message": "Model unloaded"})");
}

nlohmann::json McpServer::tool_list_models(const nlohmann::json& args) {
    ModelFilter filter;
    if (args.contains("model_type") && args["model_type"].is_string()) {
        filter.type = string_to_model_type(args["model_type"].get<std::string>());
    }

    auto models_json = model_manager_.get_models_json(filter);
    return make_tool_result(models_json.dump());
}

// ── Queue Tools ─────────────────────────────────────────────────

nlohmann::json McpServer::tool_get_job_status(const nlohmann::json& args) {
    if (!args.contains("job_id")) {
        return make_tool_result("Missing required parameter: job_id", true);
    }

    std::string job_id = args["job_id"].get<std::string>();
    auto job = queue_manager_.get_job(job_id);
    if (!job) {
        return make_tool_result("Job not found: " + job_id, true);
    }

    nlohmann::json result = job->to_json();
    return make_tool_result(result.dump());
}

nlohmann::json McpServer::tool_cancel_job(const nlohmann::json& args) {
    if (!args.contains("job_id")) {
        return make_tool_result("Missing required parameter: job_id", true);
    }

    std::string job_id = args["job_id"].get<std::string>();
    bool cancelled = queue_manager_.cancel_job(job_id);
    nlohmann::json result = {{"success", cancelled}, {"job_id", job_id}};
    if (!cancelled) {
        result["message"] = "Job could not be cancelled (may be completed or not found)";
    }
    return make_tool_result(result.dump());
}
```

**Step 2: Build and verify compilation**

Run: `cmake --build build -j$(nproc) 2>&1 | tail -5`
Expected: Compiles. Linker may still fail due to missing resource methods — that's OK.

**Step 3: Commit**

```bash
git add src/mcp_server.cpp
git commit -m "feat(mcp): implement all 9 tool handlers"
```

---

## Task 6: Resource Implementations (resources/list + resources/read)

**Files:**
- Modify: `src/mcp_server.cpp`

**Step 1: Implement resource listing and reading**

Add after the queue tools:

```cpp
// ── Resources ───────────────────────────────────────────────────

nlohmann::json McpServer::handle_list_resources() {
    nlohmann::json resources = nlohmann::json::array();

    resources.push_back({
        {"uri", "sdcpp://health"},
        {"name", "Server Health"},
        {"description", "Server status, loaded model info, and feature flags"},
        {"mimeType", "application/json"}
    });
    resources.push_back({
        {"uri", "sdcpp://memory"},
        {"name", "Memory Usage"},
        {"description", "System and GPU memory usage"},
        {"mimeType", "application/json"}
    });
    resources.push_back({
        {"uri", "sdcpp://models"},
        {"name", "Available Models"},
        {"description", "List of all available models by type"},
        {"mimeType", "application/json"}
    });
    resources.push_back({
        {"uri", "sdcpp://models/loaded"},
        {"name", "Loaded Model"},
        {"description", "Currently loaded model details and components"},
        {"mimeType", "application/json"}
    });
    resources.push_back({
        {"uri", "sdcpp://queue"},
        {"name", "Job Queue"},
        {"description", "Recent generation jobs (last 20)"},
        {"mimeType", "application/json"}
    });
    resources.push_back({
        {"uri", "sdcpp://architectures"},
        {"name", "Model Architectures"},
        {"description", "Supported model architecture presets and defaults"},
        {"mimeType", "application/json"}
    });

    // Resource templates
    nlohmann::json templates = nlohmann::json::array();
    templates.push_back({
        {"uriTemplate", "sdcpp://queue/{job_id}"},
        {"name", "Job Details"},
        {"description", "Details for a specific generation job"},
        {"mimeType", "application/json"}
    });

    return {{"resources", resources}, {"resourceTemplates", templates}};
}

nlohmann::json McpServer::handle_read_resource(const nlohmann::json& params) {
    if (!params.contains("uri") || !params["uri"].is_string()) {
        throw std::runtime_error("Missing required parameter: uri");
    }

    std::string uri = params["uri"].get<std::string>();
    nlohmann::json content;

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
    } else if (uri.starts_with("sdcpp://queue/")) {
        std::string job_id = uri.substr(14); // len("sdcpp://queue/") = 14
        content = resource_queue_job(job_id);
    } else {
        throw std::runtime_error("Resource not found: " + uri);
    }

    return {
        {"contents", nlohmann::json::array({{
            {"uri", uri},
            {"mimeType", "application/json"},
            {"text", content.dump()}
        }})}
    };
}

// ── Resource Implementations ────────────────────────────────────

nlohmann::json McpServer::resource_health() {
    auto loaded_info = model_manager_.get_loaded_models_info();
    auto memory_info = get_memory_info();

    nlohmann::json health = {
        {"status", "ok"},
        {"version", SDCPP_VERSION},
        {"model", loaded_info},
        {"memory", memory_info.to_json()}
    };

    // Feature flags
    nlohmann::json features;
#ifdef SDCPP_WEBSOCKET_ENABLED
    features["websocket"] = true;
#else
    features["websocket"] = false;
#endif
#ifdef SDCPP_ASSISTANT_ENABLED
    features["assistant"] = true;
#else
    features["assistant"] = false;
#endif
#ifdef SDCPP_EXPERIMENTAL_OFFLOAD
    features["experimental_offload"] = true;
#else
    features["experimental_offload"] = false;
#endif
    features["mcp"] = true;
    health["features"] = features;

    return health;
}

nlohmann::json McpServer::resource_memory() {
    return get_memory_info().to_json();
}

nlohmann::json McpServer::resource_models() {
    return model_manager_.get_models_json();
}

nlohmann::json McpServer::resource_models_loaded() {
    return model_manager_.get_loaded_models_info();
}

nlohmann::json McpServer::resource_queue() {
    QueueFilter filter;
    filter.limit = 20;
    auto result = queue_manager_.get_jobs_paginated(filter);

    nlohmann::json items = nlohmann::json::array();
    for (const auto& item : result.items) {
        items.push_back(item.to_json());
    }
    return {{"items", items}, {"total", result.total_count}};
}

nlohmann::json McpServer::resource_queue_job(const std::string& job_id) {
    auto job = queue_manager_.get_job(job_id);
    if (!job) {
        throw std::runtime_error("Job not found: " + job_id);
    }
    return job->to_json();
}

nlohmann::json McpServer::resource_architectures() {
    // Read architecture presets from the JSON file
    std::string arch_path = "data/model_architectures.json";
    std::ifstream file(arch_path);
    if (!file.is_open()) {
        return nlohmann::json::object();
    }
    return nlohmann::json::parse(file);
}

} // namespace sdcpp
```

Note: `resource_architectures()` reads from the JSON file directly. We need to add `#include <fstream>` at the top of the file.

**Step 2: Build and verify full compilation**

Run: `cmake --build build -j$(nproc) 2>&1 | tail -10`
Expected: Full clean compile and link.

**Step 3: Commit**

```bash
git add src/mcp_server.cpp
git commit -m "feat(mcp): implement resources/list and resources/read with 7 resources"
```

---

## Task 7: main.cpp Integration

**Files:**
- Modify: `src/main.cpp` (around lines 21-23 for include, around line 313 for instantiation)

**Step 1: Add MCP server include and instantiation**

Add include after WebSocket include block (after line 23):

```cpp
#ifdef SDCPP_MCP_ENABLED
#include "mcp_server.hpp"
#endif
```

Add MCP registration after `handlers.register_routes(server);` (after line 313, before error handler):

```cpp
        // Initialize MCP server (if enabled at build time)
#ifdef SDCPP_MCP_ENABLED
        std::cout << "Initializing MCP server..." << std::endl;
        sdcpp::McpServer mcp_server(server, model_manager, queue_manager);
        mcp_server.register_endpoint();
#else
        std::cout << "MCP server disabled at build time" << std::endl;
#endif
```

**Step 2: Build and verify**

Run: `cmake --build build -j$(nproc) 2>&1 | tail -5`
Expected: Clean compile and link with MCP integrated.

**Step 3: Commit**

```bash
git add src/main.cpp
git commit -m "feat(mcp): integrate McpServer into main server startup"
```

---

## Task 8: Manual Testing

**Step 1: Start the server**

Run: `./build/bin/sdcpp-restapi --config config.json`
Expected: Console shows "MCP server registered at POST /mcp"

**Step 2: Test initialize**

```bash
curl -s -X POST http://localhost:8077/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}}' | python3 -m json.tool
```

Expected: Returns server info with protocolVersion, capabilities (tools, resources), serverInfo.

**Step 3: Test tools/list**

```bash
curl -s -X POST http://localhost:8077/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}' | python3 -m json.tool
```

Expected: Returns 9 tools with names and input schemas.

**Step 4: Test resources/list**

```bash
curl -s -X POST http://localhost:8077/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":3,"method":"resources/list","params":{}}' | python3 -m json.tool
```

Expected: Returns 6 resources + 1 resource template.

**Step 5: Test resources/read (health)**

```bash
curl -s -X POST http://localhost:8077/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":4,"method":"resources/read","params":{"uri":"sdcpp://health"}}' | python3 -m json.tool
```

Expected: Returns health data as resource content.

**Step 6: Test error handling**

```bash
# Invalid JSON
curl -s -X POST http://localhost:8077/mcp -d 'not json' | python3 -m json.tool

# Unknown method
curl -s -X POST http://localhost:8077/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":5,"method":"unknown/method","params":{}}' | python3 -m json.tool
```

Expected: Proper JSON-RPC error responses with codes -32700 and -32601.

---

## Task 9: Documentation Updates

**Files:**
- Modify: `README.md`
- Modify: `CLAUDE.md`

**Step 1: Add MCP section to README.md**

Add after the WebSocket section (around line 173):

```markdown
### MCP (Model Context Protocol)

MCP support at `POST /mcp` enables AI clients to use image generation tools:

```bash
# Initialize
curl -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}}'

# List available tools
curl -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}'
```

**Available tools:** generate_image, generate_image_from_image, generate_video, upscale_image, load_model, unload_model, list_models, get_job_status, cancel_job

**Available resources:** sdcpp://health, sdcpp://memory, sdcpp://models, sdcpp://models/loaded, sdcpp://queue, sdcpp://queue/{job_id}, sdcpp://architectures

Disable at build time: `-DSDCPP_MCP=OFF`
```

Add `-DSDCPP_MCP=OFF` to the Optional Features build options list.

**Step 2: Add MCP pattern to CLAUDE.md**

Add MCP Server section documenting the pattern for adding tools and resources.

**Step 3: Commit**

```bash
git add README.md CLAUDE.md
git commit -m "docs: add MCP server documentation to README and CLAUDE.md"
```

---

## Task 10: Final Build + Tag

**Step 1: Clean build to verify everything**

Run: `cmake --build build -j$(nproc) --clean-first 2>&1 | tail -5`
Expected: Clean compile, no warnings from mcp_server.cpp.

**Step 2: Commit any remaining changes and tag**

```bash
git add -A
git commit -m "feat: MCP server support with tools and resources"
git tag -a v0.2.1 -m "Add MCP (Model Context Protocol) server support"
```
