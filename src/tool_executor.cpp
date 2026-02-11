#include "tool_executor.hpp"
#include "model_manager.hpp"
#include "queue_manager.hpp"
#include "architecture_manager.hpp"

#include <algorithm>
#include <iostream>
#include <set>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <iomanip>

#include "stb_image.h"
#include "stb_image_write.h"
#include "stb_image_resize.h"

namespace fs = std::filesystem;

namespace sdcpp {

// Backend tools that are executed server-side
const std::set<std::string> ToolExecutor::BACKEND_TOOLS = {
    "get_status",
    "get_models",
    "get_architectures",
    "get_job",
    "search_jobs",
    "list_jobs",
    "analyze_image"
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
        } else if (tool_name == "analyze_image") {
            return execute_analyze_image(parameters);
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

// Helper: Get value from JSON using dot-notation path (e.g., "params.prompt", "model_settings.model_name")
static nlohmann::json get_json_value(const nlohmann::json& j, const std::string& path) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : path) {
        if (c == '.') {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        parts.push_back(current);
    }

    const nlohmann::json* ptr = &j;
    for (const auto& part : parts) {
        if (!ptr->is_object() || !ptr->contains(part)) {
            return nullptr;
        }
        ptr = &(*ptr)[part];
    }
    return *ptr;
}

// Helper: Case-insensitive string contains
static bool str_contains_insensitive(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    if (haystack.empty()) return false;

    std::string h = haystack;
    std::string n = needle;
    std::transform(h.begin(), h.end(), h.begin(), ::tolower);
    std::transform(n.begin(), n.end(), n.begin(), ::tolower);

    return h.find(n) != std::string::npos;
}

// Helper: Check if a JSON value matches a filter value (case-insensitive partial match for strings)
static bool json_matches_filter(const nlohmann::json& value, const nlohmann::json& filter_value) {
    if (value.is_null()) return false;

    // String matching: case-insensitive partial match
    if (filter_value.is_string()) {
        std::string filter_str = filter_value.get<std::string>();
        if (value.is_string()) {
            return str_contains_insensitive(value.get<std::string>(), filter_str);
        }
        // Try to match other types as string
        return str_contains_insensitive(value.dump(), filter_str);
    }

    // Exact match for other types
    return value == filter_value;
}

nlohmann::json ToolExecutor::execute_search_jobs(const nlohmann::json& params) {
    std::cout << "[ToolExecutor] search_jobs params: " << params.dump() << std::endl;

    // Get all jobs first (we'll filter dynamically)
    QueueFilter base_filter;
    base_filter.limit = 10000;  // Get all jobs for dynamic filtering
    auto all_jobs = queue_manager_.get_jobs_paginated(base_filter);

    // Parse dynamic filters - support both nested "filters" object and direct params
    nlohmann::json filters;
    if (params.contains("filters") && params["filters"].is_object()) {
        filters = params["filters"];
    }

    // Also accept common filter params directly (backwards compatibility / ease of use)
    // These will be added to filters if not already present
    const std::vector<std::string> direct_filter_params = {"status", "type", "job_id"};
    for (const auto& key : direct_filter_params) {
        if (params.contains(key) && !filters.contains(key)) {
            filters[key] = params[key];
        }
    }

    // Normalize status value: "error" -> "failed" (common user/LLM mistake)
    if (filters.contains("status") && filters["status"].is_string()) {
        std::string status = filters["status"].get<std::string>();
        if (status == "error") {
            filters["status"] = "failed";
            std::cout << "[ToolExecutor] Normalized status 'error' -> 'failed'" << std::endl;
        }
    }

    std::cout << "[ToolExecutor] search_jobs filters: " << filters.dump() << std::endl;

    // Date range filters (Unix timestamps in milliseconds)
    std::optional<int64_t> date_from;
    std::optional<int64_t> date_to;
    if (params.contains("date_from") && params["date_from"].is_number()) {
        date_from = params["date_from"].get<int64_t>();
    }
    if (params.contains("date_to") && params["date_to"].is_number()) {
        date_to = params["date_to"].get<int64_t>();
    }

    // Limit
    size_t limit = params.value("limit", 10);

    // Order parameters
    bool ascending = false;
    std::string order_by = "created_at";  // Default sort by created_at
    if (params.contains("order") && params["order"].is_string()) {
        std::string order = params["order"].get<std::string>();
        ascending = (order == "ASC" || order == "asc");
    }
    if (params.contains("order_by") && params["order_by"].is_string()) {
        order_by = params["order_by"].get<std::string>();
    }

    // Track which filter keys were used for dynamic result fields
    std::set<std::string> filter_keys;
    for (auto& [key, val] : filters.items()) {
        filter_keys.insert(key);
    }

    // Filter jobs dynamically (collect all matches first, then sort and limit)
    std::vector<QueueItem> matching_items;
    for (const auto& item : all_jobs.items) {
        // Convert item to JSON for dynamic filtering
        nlohmann::json item_json = item.to_json();

        // Check date range
        auto created_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            item.created_at.time_since_epoch()).count();
        if (date_from.has_value() && created_ms < date_from.value()) {
            continue;
        }
        if (date_to.has_value() && created_ms > date_to.value()) {
            continue;
        }

        // Check all dynamic filters
        bool matches = true;
        for (auto& [key, filter_value] : filters.items()) {
            nlohmann::json item_value = get_json_value(item_json, key);
            if (!json_matches_filter(item_value, filter_value)) {
                matches = false;
                break;
            }
        }

        if (matches) {
            matching_items.push_back(item);
        }
    }

    // Sort by order_by field
    std::sort(matching_items.begin(), matching_items.end(),
        [&order_by, ascending](const QueueItem& a, const QueueItem& b) {
            nlohmann::json a_json = a.to_json();
            nlohmann::json b_json = b.to_json();
            nlohmann::json a_val = get_json_value(a_json, order_by);
            nlohmann::json b_val = get_json_value(b_json, order_by);

            // Handle null values
            if (a_val.is_null() && b_val.is_null()) return false;
            if (a_val.is_null()) return ascending;  // nulls first for ASC, last for DESC
            if (b_val.is_null()) return !ascending;

            // Compare based on type
            bool less_than = false;
            if (a_val.is_string() && b_val.is_string()) {
                less_than = a_val.get<std::string>() < b_val.get<std::string>();
            } else if (a_val.is_number() && b_val.is_number()) {
                less_than = a_val.get<double>() < b_val.get<double>();
            } else {
                less_than = a_val.dump() < b_val.dump();
            }

            return ascending ? less_than : !less_than;
        });

    // Apply limit after sorting
    if (matching_items.size() > limit) {
        matching_items.resize(limit);
    }

    // Build response with minimal metadata + filtered fields
    nlohmann::json jobs = nlohmann::json::array();
    for (const auto& item : matching_items) {
        nlohmann::json item_json = item.to_json();

        // Always include minimal metadata
        nlohmann::json job_info = {
            {"job_id", item.job_id},
            {"type", generation_type_to_string(item.type)},
            {"status", queue_status_to_string(item.status)}
        };

        // Add timestamps
        auto created_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            item.created_at.time_since_epoch()).count();
        job_info["created_at"] = created_ms;

        // Add completed_at if completed or failed
        if (item.status == QueueStatus::Completed || item.status == QueueStatus::Failed) {
            auto completed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                item.completed_at.time_since_epoch()).count();
            job_info["completed_at"] = completed_ms;
        }

        // Always include error message if failed
        if (item.status == QueueStatus::Failed && !item.error_message.empty()) {
            job_info["error"] = item.error_message;
        }

        // Include the filtered fields in the response
        for (const auto& key : filter_keys) {
            nlohmann::json value = get_json_value(item_json, key);
            if (!value.is_null()) {
                // Use the last part of the path as the key in the response
                std::string response_key = key;
                size_t last_dot = key.rfind('.');
                if (last_dot != std::string::npos) {
                    response_key = key.substr(last_dot + 1);
                }
                job_info[response_key] = value;
            }
        }

        jobs.push_back(job_info);
    }

    std::cout << "[ToolExecutor] search_jobs: found " << jobs.size() << " jobs" << std::endl;

    return {
        {"jobs", jobs},
        {"total_count", all_jobs.total_count},
        {"returned_count", matching_items.size()}
    };
}

nlohmann::json ToolExecutor::execute_list_jobs(const nlohmann::json& params) {
    QueueFilter filter;

    // Limit and offset for pagination
    filter.limit = params.value("limit", 10);
    filter.offset = params.value("offset", 0);

    // Order parameter (default DESC = newest first)
    bool ascending = false;
    if (params.contains("order") && params["order"].is_string()) {
        std::string order = params["order"].get<std::string>();
        ascending = (order == "ASC" || order == "asc");
    }

    auto result = queue_manager_.get_jobs_paginated(filter);

    // If ASC order requested, reverse the items (queue returns DESC by default)
    if (ascending && result.items.size() > 1) {
        std::reverse(result.items.begin(), result.items.end());
    }

    // Return job IDs with minimal but useful metadata
    nlohmann::json jobs = nlohmann::json::array();
    for (const auto& item : result.items) {
        nlohmann::json job_info = {
            {"job_id", item.job_id},
            {"type", generation_type_to_string(item.type)},
            {"status", queue_status_to_string(item.status)}
        };

        // Add timestamps
        auto created_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            item.created_at.time_since_epoch()).count();
        job_info["created_at"] = created_ms;

        // Add completed_at if completed or failed
        if (item.status == QueueStatus::Completed || item.status == QueueStatus::Failed) {
            auto completed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                item.completed_at.time_since_epoch()).count();
            job_info["completed_at"] = completed_ms;
        }

        // Add full error message if failed (use get_job for complete job details)
        if (item.status == QueueStatus::Failed && !item.error_message.empty()) {
            job_info["error"] = item.error_message;
        }

        jobs.push_back(job_info);
    }

    std::cout << "[ToolExecutor] list_jobs: listed " << jobs.size()
              << " jobs (offset=" << filter.offset << ", limit=" << filter.limit << ")" << std::endl;

    return {
        {"jobs", jobs},
        {"total_count", result.total_count},
        {"offset", result.offset},
        {"limit", result.limit},
        {"has_more", result.has_more}
    };
}

void ToolExecutor::set_output_dir(const std::string& output_dir) {
    output_dir_ = output_dir;
}

// Helper: Encode binary data to base64
static std::string base64_encode(const unsigned char* data, size_t len) {
    static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve(((len + 2) / 3) * 4);

    for (size_t i = 0; i < len; i += 3) {
        unsigned int b = (data[i] << 16);
        if (i + 1 < len) b |= (data[i + 1] << 8);
        if (i + 2 < len) b |= data[i + 2];

        result.push_back(chars[(b >> 18) & 0x3F]);
        result.push_back(chars[(b >> 12) & 0x3F]);
        result.push_back((i + 1 < len) ? chars[(b >> 6) & 0x3F] : '=');
        result.push_back((i + 2 < len) ? chars[b & 0x3F] : '=');
    }

    return result;
}

nlohmann::json ToolExecutor::execute_analyze_image(const nlohmann::json& params) {
    std::string job_id = params.value("job_id", "");
    int image_index = params.value("image_index", 0);
    std::string prompt = params.value("prompt", "Describe what you see in this image.");

    if (job_id.empty()) {
        return {{"error", "analyze_image requires job_id parameter"}};
    }

    // Get the job
    auto job = queue_manager_.get_job(job_id);
    if (!job) {
        return {{"error", "Job not found: " + job_id}};
    }

    if (job->status != QueueStatus::Completed) {
        return {{"error", "Job is not completed yet (status: " + queue_status_to_string(job->status) + ")"}};
    }

    if (job->outputs.empty()) {
        return {{"error", "Job has no output images"}};
    }

    if (image_index < 0 || static_cast<size_t>(image_index) >= job->outputs.size()) {
        return {{"error", "Invalid image_index. Job has " + std::to_string(job->outputs.size()) + " outputs."}};
    }

    // Get the output path
    std::string output_path = job->outputs[image_index];

    // The outputs are stored as relative paths (e.g., "2024/01/image.png")
    // We need to construct the full path using output_dir_
    fs::path full_path;
    if (fs::path(output_path).is_absolute()) {
        full_path = output_path;
    } else {
        // Remove leading "/output/" if present
        if (output_path.rfind("/output/", 0) == 0) {
            output_path = output_path.substr(8);  // Remove "/output/"
        }
        full_path = fs::path(output_dir_) / output_path;
    }

    if (!fs::exists(full_path)) {
        return {{"error", "Image file not found: " + full_path.string()}};
    }

    std::cout << "[ToolExecutor] analyze_image: loading " << full_path.string() << std::endl;

    // Load the image
    int width, height, channels;
    unsigned char* image_data = stbi_load(full_path.string().c_str(), &width, &height, &channels, 3);  // Force RGB

    if (!image_data) {
        return {{"error", "Failed to load image: " + std::string(stbi_failure_reason())}};
    }

    std::cout << "[ToolExecutor] analyze_image: loaded " << width << "x" << height
              << " (" << channels << " channels)" << std::endl;

    // Resize if larger than 1024x1024 (preserve aspect ratio)
    const int MAX_DIM = 1024;
    bool was_resized = false;

    if (width > MAX_DIM || height > MAX_DIM) {
        float scale = std::min(
            static_cast<float>(MAX_DIM) / width,
            static_cast<float>(MAX_DIM) / height
        );
        int new_width = static_cast<int>(width * scale);
        int new_height = static_cast<int>(height * scale);

        std::cout << "[ToolExecutor] analyze_image: resizing to " << new_width << "x" << new_height << std::endl;

        unsigned char* resized_data = new unsigned char[new_width * new_height * 3];
        stbir_resize_uint8(
            image_data, width, height, 0,
            resized_data, new_width, new_height, 0,
            3
        );

        stbi_image_free(image_data);
        image_data = resized_data;
        width = new_width;
        height = new_height;
        was_resized = true;
    }

    // Encode to JPEG (quality 85) for smaller base64 size
    struct JpegBuffer {
        std::vector<unsigned char> data;
    };

    auto write_callback = [](void* context, void* data, int size) {
        auto* buffer = static_cast<JpegBuffer*>(context);
        auto* bytes = static_cast<unsigned char*>(data);
        buffer->data.insert(buffer->data.end(), bytes, bytes + size);
    };

    JpegBuffer jpeg_buffer;
    int result = stbi_write_jpg_to_func(write_callback, &jpeg_buffer, width, height, 3, image_data, 85);

    // Free image data (either original or resized)
    if (was_resized) {
        delete[] image_data;  // We allocated with new
    } else {
        stbi_image_free(image_data);
    }

    if (!result || jpeg_buffer.data.empty()) {
        return {{"error", "Failed to encode image to JPEG"}};
    }

    // Encode to base64
    std::string base64_data = base64_encode(jpeg_buffer.data.data(), jpeg_buffer.data.size());

    std::cout << "[ToolExecutor] analyze_image: encoded " << base64_data.size()
              << " bytes base64 (" << jpeg_buffer.data.size() << " bytes JPEG)" << std::endl;

    // Return the image data for the LLM to process
    // The response format is designed for Ollama's vision API
    return {
        {"success", true},
        {"job_id", job_id},
        {"image_index", image_index},
        {"width", width},
        {"height", height},
        {"prompt", prompt},
        {"image_data", base64_data},  // Base64 encoded JPEG
        {"image_format", "jpeg"}
    };
}

} // namespace sdcpp
