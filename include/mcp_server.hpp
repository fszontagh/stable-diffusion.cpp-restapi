#pragma once

#include "httplib_compat.h"
#include <nlohmann/json.hpp>
#include <string>

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
    nlohmann::json tool_search_queue(const nlohmann::json& args);

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

    // URL helpers
    static std::string get_base_url(const httplib::Request& req);
    nlohmann::json rewrite_job_outputs(const nlohmann::json& job_json) const;

    httplib::Server& server_;
    ModelManager& model_manager_;
    QueueManager& queue_manager_;
    std::string base_url_;  // Set per-request from Host header
};

} // namespace sdcpp
