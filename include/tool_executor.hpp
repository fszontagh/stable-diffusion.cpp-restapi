#pragma once

#include <string>
#include <set>
#include <nlohmann/json.hpp>

namespace sdcpp {

// Forward declarations
class ModelManager;
class QueueManager;
class ArchitectureManager;

/**
 * ToolExecutor - Executes LLM assistant query tools on the backend
 *
 * This class handles execution of read-only query tools that gather
 * information from the backend, allowing the LLM to receive real data
 * before formulating its response.
 *
 * Query tools executed here:
 * - get_status: Current model/upscaler state and queue stats
 * - get_models: Available models by type
 * - get_architectures: Architecture presets with requirements
 * - get_job: Details of a specific job
 * - search_jobs: Search jobs with filters
 * - list_jobs: List jobs with pagination
 *
 * UI/action tools are passed to the frontend for execution:
 * - navigate, highlight_setting, set_setting, ask_user, etc.
 */
class ToolExecutor {
public:
    /**
     * Constructor
     * @param model_manager Reference to model manager
     * @param queue_manager Reference to queue manager
     * @param architecture_manager Pointer to architecture manager (may be null)
     */
    ToolExecutor(ModelManager& model_manager,
                 QueueManager& queue_manager,
                 ArchitectureManager* architecture_manager);

    /**
     * Execute a tool and return the result
     * @param tool_name Name of the tool to execute
     * @param parameters Tool parameters as JSON
     * @return JSON result of the tool execution
     */
    nlohmann::json execute(const std::string& tool_name,
                           const nlohmann::json& parameters);

    /**
     * Check if a tool should be executed on the backend
     * @param tool_name Name of the tool
     * @return true if this is a backend query tool
     */
    bool is_backend_tool(const std::string& tool_name) const;

private:
    // Tool implementations
    nlohmann::json execute_get_status();
    nlohmann::json execute_get_models();
    nlohmann::json execute_get_architectures();
    nlohmann::json execute_get_job(const std::string& job_id);
    nlohmann::json execute_search_jobs(const nlohmann::json& params);
    nlohmann::json execute_list_jobs(const nlohmann::json& params);

    ModelManager& model_manager_;
    QueueManager& queue_manager_;
    ArchitectureManager* architecture_manager_;

    // Set of tools that execute on the backend
    static const std::set<std::string> BACKEND_TOOLS;
};

} // namespace sdcpp
