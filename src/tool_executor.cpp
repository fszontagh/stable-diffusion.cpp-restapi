#include "tool_executor.hpp"
#include "model_manager.hpp"
#include "queue_manager.hpp"
#include "architecture_manager.hpp"

#include <iostream>

namespace sdcpp {

// Backend tools that are executed server-side
const std::set<std::string> ToolExecutor::BACKEND_TOOLS = {
    "get_status",
    "get_models",
    "get_architectures",
    "get_job",
    "search_jobs",
    "list_jobs"
};

ToolExecutor::ToolExecutor(ModelManager& model_manager,
                           QueueManager& queue_manager,
                           ArchitectureManager* architecture_manager)
    : model_manager_(model_manager)
    , queue_manager_(queue_manager)
    , architecture_manager_(architecture_manager) {
}

bool ToolExecutor::is_backend_tool(const std::string& tool_name) const {
    return BACKEND_TOOLS.find(tool_name) != BACKEND_TOOLS.end();
}

nlohmann::json ToolExecutor::execute(const std::string& tool_name,
                                      const nlohmann::json& parameters) {
    std::cout << "[ToolExecutor] Executing tool: " << tool_name << std::endl;

    try {
        if (tool_name == "get_status") {
            return execute_get_status();
        } else if (tool_name == "get_models") {
            return execute_get_models();
        } else if (tool_name == "get_architectures") {
            return execute_get_architectures();
        } else if (tool_name == "get_job") {
            std::string job_id = parameters.value("job_id", "");
            if (job_id.empty()) {
                return {{"error", "get_job requires job_id parameter"}};
            }
            return execute_get_job(job_id);
        } else if (tool_name == "search_jobs") {
            return execute_search_jobs(parameters);
        } else if (tool_name == "list_jobs") {
            return execute_list_jobs(parameters);
        } else {
            return {{"error", "Unknown backend tool: " + tool_name}};
        }
    } catch (const std::exception& e) {
        std::cerr << "[ToolExecutor] Error executing " << tool_name << ": " << e.what() << std::endl;
        return {{"error", std::string("Tool execution failed: ") + e.what()}};
    }
}

nlohmann::json ToolExecutor::execute_get_status() {
    // Combine model info and queue status
    nlohmann::json result;

    // Model information
    result["model_info"] = model_manager_.get_loaded_models_info();

    // Upscaler information
    result["upscaler_info"] = {
        {"loaded", model_manager_.is_upscaler_loaded()},
        {"name", model_manager_.get_loaded_upscaler_name()}
    };

    // Queue statistics
    result["queue_stats"] = queue_manager_.get_status();

    // Get recent jobs (last 10) for quick context
    QueueFilter filter;
    filter.limit = 10;
    auto recent = queue_manager_.get_jobs_paginated(filter);

    nlohmann::json recent_jobs = nlohmann::json::array();
    for (const auto& item : recent.items) {
        nlohmann::json job_entry = {
            {"job_id", item.job_id},
            {"type", generation_type_to_string(item.type)},
            {"status", queue_status_to_string(item.status)},
            {"prompt", item.params.value("prompt", "")}
        };
        // Include model info so LLM knows which model was used for each job
        if (!item.model_settings.empty()) {
            job_entry["model_name"] = item.model_settings.value("model_name", "");
            job_entry["model_architecture"] = item.model_settings.value("model_architecture", "");
        }
        recent_jobs.push_back(job_entry);
    }
    result["recent_jobs"] = recent_jobs;

    std::cout << "[ToolExecutor] get_status: model_loaded="
              << result["model_info"].value("loaded", false) << std::endl;

    return result;
}

nlohmann::json ToolExecutor::execute_get_models() {
    nlohmann::json models = model_manager_.get_models_json();
    std::cout << "[ToolExecutor] get_models: returned model list" << std::endl;
    return models;
}

nlohmann::json ToolExecutor::execute_get_architectures() {
    if (!architecture_manager_) {
        return {{"error", "Architecture manager not available"}};
    }

    nlohmann::json archs = architecture_manager_->to_json();
    std::cout << "[ToolExecutor] get_architectures: returned "
              << archs.size() << " presets" << std::endl;
    return archs;
}

nlohmann::json ToolExecutor::execute_get_job(const std::string& job_id) {
    auto job = queue_manager_.get_job(job_id);
    if (!job) {
        return {{"error", "Job not found: " + job_id}};
    }

    std::cout << "[ToolExecutor] get_job: " << job_id
              << " status=" << queue_status_to_string(job->status) << std::endl;

    return job->to_json();
}

nlohmann::json ToolExecutor::execute_search_jobs(const nlohmann::json& params) {
    QueueFilter filter;

    // Search text in prompt
    if (params.contains("prompt") && params["prompt"].is_string()) {
        filter.search = params["prompt"].get<std::string>();
    }

    // Status filter
    if (params.contains("status") && params["status"].is_string()) {
        std::string status_str = params["status"].get<std::string>();
        filter.status = string_to_queue_status(status_str);
    }

    // Type filter
    if (params.contains("type") && params["type"].is_string()) {
        std::string type_str = params["type"].get<std::string>();
        filter.type = string_to_generation_type(type_str);
    }

    // Architecture filter
    if (params.contains("architecture") && params["architecture"].is_string()) {
        filter.architecture = params["architecture"].get<std::string>();
    }

    // Model filter (search in model settings)
    if (params.contains("model") && params["model"].is_string()) {
        // Model search is handled via the search parameter in the architecture field
        // since the queue doesn't have a dedicated model field in QueueFilter
        // We'll use the search field if no prompt search is specified
        if (!filter.search.has_value()) {
            filter.search = params["model"].get<std::string>();
        }
    }

    // Limit
    filter.limit = params.value("limit", 10);

    auto result = queue_manager_.get_jobs_paginated(filter);

    nlohmann::json jobs = nlohmann::json::array();
    for (const auto& item : result.items) {
        jobs.push_back(item.to_json());
    }

    std::cout << "[ToolExecutor] search_jobs: found " << jobs.size() << " jobs" << std::endl;

    return {
        {"jobs", jobs},
        {"total_count", result.total_count},
        {"returned_count", result.filtered_count}
    };
}

nlohmann::json ToolExecutor::execute_list_jobs(const nlohmann::json& params) {
    QueueFilter filter;

    // Limit and offset for pagination
    filter.limit = params.value("limit", 10);
    filter.offset = params.value("offset", 0);

    auto result = queue_manager_.get_jobs_paginated(filter);

    // For list_jobs, we return only job IDs and minimal info (as per the plan)
    nlohmann::json job_ids = nlohmann::json::array();
    for (const auto& item : result.items) {
        job_ids.push_back({
            {"job_id", item.job_id},
            {"type", generation_type_to_string(item.type)},
            {"status", queue_status_to_string(item.status)}
        });
    }

    std::cout << "[ToolExecutor] list_jobs: listed " << job_ids.size()
              << " jobs (offset=" << filter.offset << ", limit=" << filter.limit << ")" << std::endl;

    return {
        {"jobs", job_ids},
        {"total_count", result.total_count},
        {"offset", result.offset},
        {"limit", result.limit},
        {"has_more", result.has_more}
    };
}

} // namespace sdcpp
