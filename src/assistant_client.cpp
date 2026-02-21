#include "assistant_client.hpp"
#include "tool_executor.hpp"
#include "utils.hpp"
#include "queue_item_fields.hpp"

#include <httplib.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <regex>

namespace fs = std::filesystem;

namespace sdcpp {

// ConversationMessage JSON serialization
nlohmann::json ConversationMessage::to_json() const {
    nlohmann::json j = {
        {"role", role_to_string(role)},
        {"content", content},
        {"timestamp", timestamp}
    };

    // Include thinking if present
    if (!thinking.empty()) {
        j["thinking"] = thinking;
    }

    // Include tool calls if any
    if (!tool_calls.empty()) {
        nlohmann::json tc_array = nlohmann::json::array();
        for (const auto& tc : tool_calls) {
            tc_array.push_back(tc.to_json());
        }
        j["tool_calls"] = tc_array;
    }

    return j;
}

ConversationMessage ConversationMessage::from_json(const nlohmann::json& j) {
    ConversationMessage msg;
    msg.role = string_to_role(j.value("role", "user"));
    msg.content = j.value("content", "");
    msg.timestamp = j.value("timestamp", 0LL);

    // Load thinking if present
    if (j.contains("thinking") && j["thinking"].is_string()) {
        msg.thinking = j["thinking"].get<std::string>();
    }

    // Load tool calls if present
    if (j.contains("tool_calls") && j["tool_calls"].is_array()) {
        for (const auto& tc : j["tool_calls"]) {
            ToolCallInfo info;
            info.name = tc.value("name", "");
            info.parameters = tc.value("parameters", nlohmann::json::object());
            info.result = tc.value("result", "");
            info.executed_on_backend = tc.value("executed_on_backend", false);
            msg.tool_calls.push_back(info);
        }
    }

    return msg;
}

std::string ConversationMessage::role_to_string(MessageRole role) {
    switch (role) {
        case MessageRole::System: return "system";
        case MessageRole::User: return "user";
        case MessageRole::Assistant: return "assistant";
        default: return "user";
    }
}

MessageRole ConversationMessage::string_to_role(const std::string& str) {
    if (str == "system") return MessageRole::System;
    if (str == "assistant") return MessageRole::Assistant;
    return MessageRole::User;
}

// AssistantAction JSON serialization
nlohmann::json AssistantAction::to_json() const {
    return {
        {"type", type},
        {"parameters", parameters}
    };
}

AssistantAction AssistantAction::from_json(const nlohmann::json& j) {
    AssistantAction action;
    action.type = j.value("type", "");
    action.parameters = j.value("parameters", nlohmann::json::object());
    return action;
}

// ToolCallInfo JSON serialization
nlohmann::json ToolCallInfo::to_json() const {
    nlohmann::json j = {
        {"name", name},
        {"parameters", parameters},
        {"executed_on_backend", executed_on_backend}
    };
    if (!result.empty()) {
        j["result"] = result;
    }
    return j;
}

// AssistantResponse JSON serialization
nlohmann::json AssistantResponse::to_json() const {
    nlohmann::json j = {
        {"success", success},
        {"message", message}
    };

    if (!thinking.empty()) {
        j["thinking"] = thinking;
    }

    if (!actions.empty()) {
        nlohmann::json actions_json = nlohmann::json::array();
        for (const auto& action : actions) {
            actions_json.push_back(action.to_json());
        }
        j["actions"] = actions_json;
    }

    if (!tool_calls.empty()) {
        nlohmann::json tool_calls_json = nlohmann::json::array();
        for (const auto& tc : tool_calls) {
            tool_calls_json.push_back(tc.to_json());
        }
        j["tool_calls"] = tool_calls_json;
    }

    if (error.has_value()) {
        j["error"] = error.value();
    }

    return j;
}

// AssistantClient implementation
AssistantClient::AssistantClient(const AssistantConfig& config, const std::string& data_dir,
                                   const std::string& config_file_path,
                                   ToolExecutor* tool_executor)
    : config_(config)
    , history_file_((fs::path(data_dir) / "assistant_history.json").string())
    , config_file_path_(config_file_path)
    , tool_executor_(tool_executor)
{
    load_history();
    std::cout << "[AssistantClient] Initialized with endpoint: " << config_.endpoint
              << " | model: " << config_.model
              << " | enabled: " << (config_.enabled ? "true" : "false");
    if (!config_file_path_.empty()) {
        std::cout << " | config persistence enabled";
    }
    if (tool_executor_) {
        std::cout << " | backend tool execution enabled";
    }
    std::cout << std::endl;
}

AssistantClient::~AssistantClient() {
    // History is saved after each modification
}

bool AssistantClient::parse_url(const std::string& url, std::string& host, int& port,
                                 std::string& path, bool& is_ssl) {
    std::regex url_regex(R"(^(https?)://([^:/]+)(?::(\d+))?(/.*)?$)", std::regex::icase);
    std::smatch match;

    if (!std::regex_match(url, match, url_regex)) {
        return false;
    }

    std::string scheme = match[1].str();
    is_ssl = (scheme == "https" || scheme == "HTTPS");
    host = match[2].str();

    if (match[3].matched) {
        port = std::stoi(match[3].str());
    } else {
        port = is_ssl ? 443 : 80;
    }

    path = match[4].matched ? match[4].str() : "";

    return true;
}

std::string AssistantClient::build_system_prompt() const {
    // Always use the default system prompt as base (contains action definitions)
    // Append custom system prompt if provided (for additional personality/instructions)
    std::string prompt = DEFAULT_ASSISTANT_SYSTEM_PROMPT;

    if (!config_.system_prompt.empty()) {
        prompt += "\n\n## Additional Instructions\n" + config_.system_prompt;
    }

    return prompt;
}

nlohmann::json AssistantClient::build_messages(const std::string& user_message,
                                                const nlohmann::json& context) const {
    nlohmann::json messages = nlohmann::json::array();

    // Add system prompt
    messages.push_back({
        {"role", "system"},
        {"content", build_system_prompt()}
    });

    // Add conversation history (limited to max_history_turns)
    {
        std::lock_guard<std::mutex> lock(history_mutex_);

        // Get recent history (excluding the current message we're about to add)
        size_t history_limit = std::min(
            static_cast<size_t>(config_.max_history_turns * 2),  // Each turn has user + assistant
            history_.size()
        );

        // History is stored newest first, so we need to reverse for chronological order
        for (size_t i = history_limit; i > 0; --i) {
            const auto& msg = history_[i - 1];
            if (msg.role != MessageRole::System) {
                messages.push_back({
                    {"role", ConversationMessage::role_to_string(msg.role)},
                    {"content", msg.content}
                });
            }
        }
    }

    // NOTE: Context is no longer injected here - assistant must use tools to query state
    // This ensures the assistant always gets fresh data and avoids stale context issues

    // Add current user message
    messages.push_back({
        {"role", "user"},
        {"content", user_message}
    });

    return messages;
}

nlohmann::json AssistantClient::build_tools() const {
    nlohmann::json tools = nlohmann::json::array();

    // set_setting tool
    tools.push_back({
        {"type", "function"},
        {"function", {
            {"name", "set_setting"},
            {"description", "Modify a generation parameter (prompt, steps, cfg_scale, dimensions, sampler, etc.)"},
            {"parameters", {
                {"type", "object"},
                {"required", nlohmann::json::array({"field", "value"})},
                {"properties", {
                    {"field", {{"type", "string"}, {"description", "Setting field: prompt, negativePrompt, width, height, steps, cfgScale, distilledGuidance, seed, sampler, scheduler, batchCount, clipSkip, slgScale, vaeTiling, easycache, videoFrames, fps"}}},
                    {"value", {{"type", "string"}, {"description", "The new value for the setting (use string representation for numbers/booleans)"}}}
                }}
            }}
        }}
    });

    // generate tool
    tools.push_back({
        {"type", "function"},
        {"function", {
            {"name", "generate"},
            {"description", "Add an image generation job to the queue"},
            {"parameters", {
                {"type", "object"},
                {"properties", {
                    {"type", {{"type", "string"}, {"enum", {"txt2img", "img2img", "txt2vid"}}, {"description", "Generation type"}}},
                    {"prompt", {{"type", "string"}, {"description", "Text prompt for generation"}}},
                    {"steps", {{"type", "integer"}, {"description", "Number of steps"}}},
                    {"cfg_scale", {{"type", "number"}, {"description", "CFG scale value"}}},
                    {"width", {{"type", "integer"}, {"description", "Image width"}}},
                    {"height", {{"type", "integer"}, {"description", "Image height"}}},
                    {"seed", {{"type", "integer"}, {"description", "Random seed (-1 for random)"}}},
                    {"sampler", {{"type", "string"}, {"description", "Sampler name"}}},
                    {"scheduler", {{"type", "string"}, {"description", "Scheduler name"}}},
                    {"continue_on_complete", {{"type", "string"}, {"description", "Instruction for follow-up action after generation completes"}}}
                }}
            }}
        }}
    });

    // load_model tool
    tools.push_back({
        {"type", "function"},
        {"function", {
            {"name", "load_model"},
            {"description", "Load a main model with optional component models (VAE, CLIP, T5, LLM)"},
            {"parameters", {
                {"type", "object"},
                {"required", nlohmann::json::array({"model_name", "model_type"})},
                {"properties", {
                    {"model_name", {{"type", "string"}, {"description", "Name of the model to load"}}},
                    {"model_type", {{"type", "string"}, {"enum", {"checkpoint", "diffusion"}}, {"description", "Type of model"}}},
                    {"vae", {{"type", "string"}, {"description", "VAE model to load"}}},
                    {"clip_l", {{"type", "string"}, {"description", "CLIP-L model to load"}}},
                    {"clip_g", {{"type", "string"}, {"description", "CLIP-G model to load"}}},
                    {"t5xxl", {{"type", "string"}, {"description", "T5-XXL model to load"}}},
                    {"llm", {{"type", "string"}, {"description", "LLM model to load (for Z-Image)"}}},
                    {"continue_on_complete", {{"type", "string"}, {"description", "Instruction for follow-up action after model loads"}}}
                }}
            }}
        }}
    });

    // set_component tool
    tools.push_back({
        {"type", "function"},
        {"function", {
            {"name", "set_component"},
            {"description", "Load a component model alongside already loaded main model"},
            {"parameters", {
                {"type", "object"},
                {"required", nlohmann::json::array({"component_type", "model_name"})},
                {"properties", {
                    {"component_type", {{"type", "string"}, {"enum", {"vae", "clip", "clip_l", "clip_g", "t5", "t5xxl", "controlnet", "taesd"}}, {"description", "Type of component"}}},
                    {"model_name", {{"type", "string"}, {"description", "Name of the component model"}}}
                }}
            }}
        }}
    });

    // unload_model tool
    tools.push_back({
        {"type", "function"},
        {"function", {
            {"name", "unload_model"},
            {"description", "Unload the currently loaded model"},
            {"parameters", {{"type", "object"}, {"properties", nlohmann::json::object()}}}
        }}
    });

    // refresh_models tool
    tools.push_back({
        {"type", "function"},
        {"function", {
            {"name", "refresh_models"},
            {"description", "Refresh the list of available models by rescanning model directories. Use this after downloading or converting models to make them available for loading."},
            {"parameters", {{"type", "object"}, {"properties", nlohmann::json::object()}}}
        }}
    });

    // get_status tool - IMPORTANT: Use this to check current state before taking actions
    tools.push_back({
        {"type", "function"},
        {"function", {
            {"name", "get_status"},
            {"description", "Get current application status including loaded model info, upscaler state, and queue statistics. ALWAYS call this first before loading models or making assumptions about state."},
            {"parameters", {{"type", "object"}, {"properties", nlohmann::json::object()}}}
        }}
    });

    // get_models tool - Get available models
    tools.push_back({
        {"type", "function"},
        {"function", {
            {"name", "get_models"},
            {"description", "Get list of all available models organized by type (checkpoints, diffusion_models, vae, loras, clip, t5, llm, esrgan, etc.)"},
            {"parameters", {{"type", "object"}, {"properties", nlohmann::json::object()}}}
        }}
    });

    // get_settings tool - Get current generation settings
    tools.push_back({
        {"type", "function"},
        {"function", {
            {"name", "get_settings"},
            {"description", "Get current generation settings (prompt, dimensions, steps, cfg_scale, sampler, scheduler, etc.)"},
            {"parameters", {{"type", "object"}, {"properties", nlohmann::json::object()}}}
        }}
    });

    // get_architectures tool - Get architecture presets
    tools.push_back({
        {"type", "function"},
        {"function", {
            {"name", "get_architectures"},
            {"description", "Get all architecture presets with their required components and recommended settings (SD1.5, SDXL, Flux, SD3, Z-Image, etc.)"},
            {"parameters", {{"type", "object"}, {"properties", nlohmann::json::object()}}}
        }}
    });

    // get_job tool
    tools.push_back({
        {"type", "function"},
        {"function", {
            {"name", "get_job"},
            {"description", "Get detailed information about a specific job by ID"},
            {"parameters", {
                {"type", "object"},
                {"required", nlohmann::json::array({"job_id"})},
                {"properties", {
                    {"job_id", {{"type", "string"}, {"description", "The job ID to retrieve"}}}
                }}
            }}
        }}
    });

    // search_jobs tool - dynamic filtering on any queue item field
    tools.push_back({
        {"type", "function"},
        {"function", {
            {"name", "search_jobs"},
            {"description", QueueItemFields::get_filter_keys_description()},
            {"parameters", {
                {"type", "object"},
                {"properties", {
                    {"status", {{"type", "string"}, {"enum", {"pending", "processing", "completed", "failed", "cancelled"}}, {"description", "Filter by job status. Use 'failed' for error jobs."}}},
                    {"type", {{"type", "string"}, {"enum", {"txt2img", "img2img", "txt2vid", "upscale", "convert", "model_download", "model_hash"}}, {"description", "Filter by job type"}}},
                    {"filters", {{"type", "object"}, {"description", "Additional key-value filters. Use dot notation for nested fields (e.g., 'params.prompt', 'model_settings.model_name'). String values use case-insensitive partial matching."}}},
                    {"date_from", {{"type", "integer"}, {"description", "Filter jobs created after this Unix timestamp (milliseconds)"}}},
                    {"date_to", {{"type", "integer"}, {"description", "Filter jobs created before this Unix timestamp (milliseconds)"}}},
                    {"order", {{"type", "string"}, {"enum", {"ASC", "DESC"}}, {"description", "Sort order (default: DESC for newest first)"}}},
                    {"order_by", {{"type", "string"}, {"description", "Field to sort by using dot notation (default: 'created_at')"}}},
                    {"limit", {{"type", "integer"}, {"description", "Maximum results to return (default 10)"}}}
                }}
            }}
        }}
    });

    // load_job_model tool
    tools.push_back({
        {"type", "function"},
        {"function", {
            {"name", "load_job_model"},
            {"description", "Load the exact model configuration from a completed job including all components"},
            {"parameters", {
                {"type", "object"},
                {"required", nlohmann::json::array({"job_id"})},
                {"properties", {
                    {"job_id", {{"type", "string"}, {"description", "The job ID to load model from"}}}
                }}
            }}
        }}
    });

    // set_image tool
    tools.push_back({
        {"type", "function"},
        {"function", {
            {"name", "set_image"},
            {"description", "Set an image for img2img or upscaler from URL or job output"},
            {"parameters", {
                {"type", "object"},
                {"required", nlohmann::json::array({"target"})},
                {"properties", {
                    {"target", {{"type", "string"}, {"enum", {"img2img", "upscaler"}}, {"description", "Target to set image for"}}},
                    {"source", {{"type", "string"}, {"description", "Image URL"}}},
                    {"job_id", {{"type", "string"}, {"description", "Job ID to use output from"}}}
                }}
            }}
        }}
    });

    // cancel_job tool
    tools.push_back({
        {"type", "function"},
        {"function", {
            {"name", "cancel_job"},
            {"description", "Cancel a queue job"},
            {"parameters", {
                {"type", "object"},
                {"required", nlohmann::json::array({"job_id"})},
                {"properties", {
                    {"job_id", {{"type", "string"}, {"description", "The job ID to cancel"}}}
                }}
            }}
        }}
    });

    // navigate tool
    tools.push_back({
        {"type", "function"},
        {"function", {
            {"name", "navigate"},
            {"description", "Navigate to a view in the UI"},
            {"parameters", {
                {"type", "object"},
                {"required", nlohmann::json::array({"view"})},
                {"properties", {
                    {"view", {{"type", "string"}, {"enum", {"dashboard", "models", "generate", "queue", "upscale", "chat"}}, {"description", "View to navigate to"}}}
                }}
            }}
        }}
    });

    // apply_recommended_settings tool
    tools.push_back({
        {"type", "function"},
        {"function", {
            {"name", "apply_recommended_settings"},
            {"description", "Apply recommended settings for the current model architecture"},
            {"parameters", {{"type", "object"}, {"properties", nlohmann::json::object()}}}
        }}
    });

    // highlight_setting tool
    tools.push_back({
        {"type", "function"},
        {"function", {
            {"name", "highlight_setting"},
            {"description", "Scroll to and highlight a setting field in the UI"},
            {"parameters", {
                {"type", "object"},
                {"required", nlohmann::json::array({"field"})},
                {"properties", {
                    {"field", {{"type", "string"}, {"description", "Setting field to highlight"}}}
                }}
            }}
        }}
    });

    // load_upscaler tool
    tools.push_back({
        {"type", "function"},
        {"function", {
            {"name", "load_upscaler"},
            {"description", "Load an ESRGAN upscaler model"},
            {"parameters", {
                {"type", "object"},
                {"required", nlohmann::json::array({"model_name"})},
                {"properties", {
                    {"model_name", {{"type", "string"}, {"description", "Name of the upscaler model"}}},
                    {"tile_size", {{"type", "integer"}, {"description", "Tile size for processing (default 128)"}}}
                }}
            }}
        }}
    });

    // unload_upscaler tool
    tools.push_back({
        {"type", "function"},
        {"function", {
            {"name", "unload_upscaler"},
            {"description", "Unload the current upscaler model"},
            {"parameters", {{"type", "object"}, {"properties", nlohmann::json::object()}}}
        }}
    });

    // upscale tool
    tools.push_back({
        {"type", "function"},
        {"function", {
            {"name", "upscale"},
            {"description", "Submit an upscale job to the queue"},
            {"parameters", {
                {"type", "object"},
                {"properties", {
                    {"job_id", {{"type", "string"}, {"description", "Job ID to upscale output from"}}},
                    {"image_base64", {{"type", "string"}, {"description", "Base64-encoded image data"}}},
                    {"tile_size", {{"type", "integer"}, {"description", "Tile size for processing"}}},
                    {"repeats", {{"type", "integer"}, {"description", "Number of upscaling passes"}}}
                }}
            }}
        }}
    });

    // ask_user tool
    tools.push_back({
        {"type", "function"},
        {"function", {
            {"name", "ask_user"},
            {"description", "Show a question modal to the user and wait for their answer"},
            {"parameters", {
                {"type", "object"},
                {"required", nlohmann::json::array({"question", "options"})},
                {"properties", {
                    {"title", {{"type", "string"}, {"description", "Title for the question modal"}}},
                    {"question", {{"type", "string"}, {"description", "The question to ask"}}},
                    {"options", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Predefined answer options as clickable buttons"}}}
                }}
            }}
        }}
    });

    // download_model tool
    tools.push_back({
        {"type", "function"},
        {"function", {
            {"name", "download_model"},
            {"description", "Download a model from URL, CivitAI, or HuggingFace"},
            {"parameters", {
                {"type", "object"},
                {"required", nlohmann::json::array({"model_type"})},
                {"properties", {
                    {"model_type", {{"type", "string"}, {"enum", {"checkpoint", "diffusion", "vae", "lora", "clip", "t5", "embedding", "controlnet", "llm", "esrgan", "taesd"}}, {"description", "Type of model to download"}}},
                    {"source", {{"type", "string"}, {"enum", {"url", "civitai", "huggingface"}}, {"description", "Download source"}}},
                    {"url", {{"type", "string"}, {"description", "Direct download URL"}}},
                    {"model_id", {{"type", "string"}, {"description", "CivitAI model ID"}}},
                    {"repo_id", {{"type", "string"}, {"description", "HuggingFace repository ID"}}},
                    {"filename", {{"type", "string"}, {"description", "Filename for HuggingFace downloads"}}},
                    {"subfolder", {{"type", "string"}, {"description", "Subfolder within the model type directory"}}}
                }}
            }}
        }}
    });

    // get_quantization_types tool
    tools.push_back({
        {"type", "function"},
        {"function", {
            {"name", "get_quantization_types"},
            {"description", "Get the list of supported quantization types for model conversion. Returns all available types with their names and bit depths."},
            {"parameters", {{"type", "object"}, {"properties", nlohmann::json::object()}}}
        }}
    });

    // convert_model tool
    tools.push_back({
        {"type", "function"},
        {"function", {
            {"name", "convert_model"},
            {"description", "Convert a model to GGUF format with specified quantization type. Use get_quantization_types tool to see all available types."},
            {"parameters", {
                {"type", "object"},
                {"required", nlohmann::json::array({"output_type"})},
                {"properties", {
                    {"model_name", {{"type", "string"}, {"description", "Model name from available_models (relative path)"}}},
                    {"input_path", {{"type", "string"}, {"description", "Full absolute path to the model file"}}},
                    {"model_type", {{"type", "string"}, {"enum", {"checkpoint", "diffusion", "vae", "lora", "clip", "t5", "controlnet", "llm", "esrgan", "taesd"}}, {"description", "Type of model (default: checkpoint)"}}},
                    {"output_type", {{"type", "string"}, {"description", "Quantization type (use get_quantization_types for available options)"}}},
                    {"output_path", {{"type", "string"}, {"description", "Custom output path (default: auto-generated with .gguf extension)"}}},
                    {"vae_path", {{"type", "string"}, {"description", "Path to VAE to bake into the model"}}},
                    {"tensor_type_rules", {{"type", "string"}, {"description", "Custom tensor type rules"}}}
                }}
            }}
        }}
    });

    // list_jobs tool
    tools.push_back({
        {"type", "function"},
        {"function", {
            {"name", "list_jobs"},
            {"description", "List jobs with pagination. Returns minimal job metadata (job_id, type, status, timestamps, error). Use get_job for full job details."},
            {"parameters", {
                {"type", "object"},
                {"properties", {
                    {"order", {{"type", "string"}, {"enum", {"ASC", "DESC"}}, {"description", "Sort order by creation time (default: DESC for newest first)"}}},
                    {"limit", {{"type", "integer"}, {"description", "Number of jobs to return (default: 10)"}}},
                    {"offset", {{"type", "integer"}, {"description", "Skip first N jobs for pagination (default: 0)"}}}
                }}
            }}
        }}
    });

    // delete_jobs tool
    tools.push_back({
        {"type", "function"},
        {"function", {
            {"name", "delete_jobs"},
            {"description", "Delete one or more queue jobs. IMPORTANT: You MUST ask the user for confirmation using the ask_user tool before deleting any jobs. Only jobs that are not currently processing can be deleted (pending, completed, failed, or cancelled jobs)."},
            {"parameters", {
                {"type", "object"},
                {"required", nlohmann::json::array({"job_ids"})},
                {"properties", {
                    {"job_ids", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Array of job IDs to delete"}}}
                }}
            }}
        }}
    });

    // Conditionally add vision tool if the model supports vision
    if (current_model_has_vision()) {
        tools.push_back({
            {"type", "function"},
            {"function", {
                {"name", "analyze_image"},
                {"description", "Analyze a generated image using vision capabilities. Returns a description of what's in the image. Only available when using a vision-capable model."},
                {"parameters", {
                    {"type", "object"},
                    {"required", nlohmann::json::array({"job_id"})},
                    {"properties", {
                        {"job_id", {{"type", "string"}, {"description", "Job ID of a completed generation with output images"}}},
                        {"image_index", {{"type", "integer"}, {"description", "0-based index of the image to analyze (default: 0)"}}},
                        {"prompt", {{"type", "string"}, {"description", "What to analyze or describe about the image (default: general description)"}}}
                    }}
                }}
            }}
        });
        std::cout << "[AssistantClient] Vision tool (analyze_image) enabled for model: " << config_.model << std::endl;
    }

    return tools;
}

std::vector<AssistantAction> AssistantClient::extract_tool_calls(const nlohmann::json& message) {
    std::vector<AssistantAction> actions;

    if (!message.contains("tool_calls") || !message["tool_calls"].is_array()) {
        return actions;
    }

    for (const auto& tool_call : message["tool_calls"]) {
        if (!tool_call.contains("function")) {
            continue;
        }

        const auto& func = tool_call["function"];
        if (!func.contains("name")) {
            continue;
        }

        AssistantAction action;
        action.type = func["name"].get<std::string>();

        // Parse arguments - may be JSON string or object
        if (func.contains("arguments")) {
            if (func["arguments"].is_string()) {
                try {
                    action.parameters = nlohmann::json::parse(func["arguments"].get<std::string>());
                } catch (...) {
                    action.parameters = nlohmann::json::object();
                }
            } else if (func["arguments"].is_object()) {
                action.parameters = func["arguments"];
            }
        }

        std::cout << "[AssistantClient] Extracted tool call: " << action.type << std::endl;
        actions.push_back(action);
    }

    std::cout << "[AssistantClient] Extracted " << actions.size() << " tool calls from response" << std::endl;
    return actions;
}

std::vector<AssistantAction> AssistantClient::extract_actions(const std::string& response) {
    std::vector<AssistantAction> actions;

    // Look for json:action blocks with 2 or 3 backticks (some LLMs use ``)
    // Pattern matches: ``json:action or ```json:action
    // Made more flexible: optional whitespace before/after content, optional newline before closing backticks
    std::regex action_regex(R"(`{2,3}\s*json:action\s*\n([\s\S]*?)\s*`{2,3})", std::regex::icase);
    std::smatch match;
    std::string search_str = response;

    std::cout << "[AssistantClient] Searching for action blocks in response (length: " << response.length() << ")" << std::endl;

    int block_count = 0;
    while (std::regex_search(search_str, match, action_regex)) {
        block_count++;
        try {
            std::string json_content = match[1].str();
            // Trim whitespace from the captured content
            size_t start = json_content.find_first_not_of(" \t\n\r");
            size_t end = json_content.find_last_not_of(" \t\n\r");
            if (start != std::string::npos && end != std::string::npos) {
                json_content = json_content.substr(start, end - start + 1);
            }

            std::cout << "[AssistantClient] Found action block " << block_count << ", parsing JSON" << std::endl;
            auto action_json = nlohmann::json::parse(json_content);

            if (action_json.contains("actions") && action_json["actions"].is_array()) {
                size_t actions_in_block = action_json["actions"].size();
                std::cout << "[AssistantClient] Block " << block_count << " contains " << actions_in_block << " actions" << std::endl;
                for (const auto& action_item : action_json["actions"]) {
                    actions.push_back(AssistantAction::from_json(action_item));
                }
            } else {
                std::cerr << "[AssistantClient] Block " << block_count << " has no 'actions' array" << std::endl;
            }
        } catch (const nlohmann::json::exception& e) {
            std::cerr << "[AssistantClient] Failed to parse action JSON in block " << block_count << ": " << e.what() << std::endl;
        }

        search_str = match.suffix().str();
    }

    std::cout << "[AssistantClient] Extracted " << actions.size() << " actions from " << block_count << " blocks" << std::endl;

    return actions;
}

AssistantResponse AssistantClient::parse_response(const std::string& llm_response) {
    AssistantResponse response;
    response.success = true;

    // Extract actions from the response
    response.actions = extract_actions(llm_response);

    // Remove action blocks from the message to get clean text (2 or 3 backticks)
    std::regex action_block_regex(R"(`{2,3}json:action\s*\n[\s\S]*?\n`{2,3})", std::regex::icase);
    response.message = std::regex_replace(llm_response, action_block_regex, "");

    // Trim whitespace
    auto& msg = response.message;
    msg.erase(0, msg.find_first_not_of(" \t\n\r"));
    if (!msg.empty()) {
        msg.erase(msg.find_last_not_of(" \t\n\r") + 1);
    }

    return response;
}

AssistantResponse AssistantClient::chat(const std::string& user_message,
                                         const nlohmann::json& context) {
    AssistantResponse response;
    response.success = false;

    if (!config_.enabled) {
        response.error = "Assistant is disabled";
        return response;
    }

    if (user_message.empty()) {
        response.error = "Message cannot be empty";
        return response;
    }

    // Parse endpoint URL
    std::string host, path;
    int port;
    bool is_ssl;

    if (!parse_url(config_.endpoint, host, port, path, is_ssl)) {
        response.error = "Invalid assistant endpoint URL";
        return response;
    }

    // Create HTTP client
    httplib::Client client(host, port);
    client.set_connection_timeout(config_.timeout_seconds);
    client.set_read_timeout(config_.timeout_seconds);
    client.set_write_timeout(config_.timeout_seconds);

    // Prepare headers
    httplib::Headers headers;
    if (!config_.api_key.empty()) {
        headers.emplace("Authorization", "Bearer " + config_.api_key);
    }
    headers.emplace("Content-Type", "application/json");

    // Build messages array with history and context
    nlohmann::json messages = build_messages(user_message, context);

    // Tool execution loop - execute backend tools and feed results back to LLM
    const int MAX_TOOL_ITERATIONS = 10;
    std::vector<AssistantAction> ui_actions;  // Actions to pass to frontend
    std::vector<ToolCallInfo> all_tool_calls; // Track all tool calls for response
    std::string final_response_text;
    std::string final_thinking_text;          // Thinking/reasoning from LLM
    int total_tool_calls = 0;

    for (int iteration = 0; iteration < MAX_TOOL_ITERATIONS; ++iteration) {
        // Build request body with native tools for Ollama
        nlohmann::json request_body = {
            {"model", config_.model},
            {"messages", messages},
            {"tools", build_tools()},
            {"stream", false},
            {"think", true},  // Enable thinking/reasoning trace for models that support it
            {"options", {
                {"temperature", config_.temperature},
                {"num_predict", config_.max_tokens}
            }}
        };

        std::cout << "[AssistantClient] Sending chat request (iteration " << iteration + 1
                  << ") to " << config_.endpoint << "/api/chat" << std::endl;

        // Make request
        auto res = client.Post("/api/chat", headers, request_body.dump(), "application/json");

        if (!res) {
            response.error = "Failed to connect to LLM server: " + httplib::to_string(res.error());
            std::cerr << "[AssistantClient] Connection error: " << response.error.value() << std::endl;
            return response;
        }

        if (res->status != 200) {
            response.error = "LLM API error: HTTP " + std::to_string(res->status);
            std::cerr << "[AssistantClient] API error: " << res->status << " - " << res->body << std::endl;
            return response;
        }

        // Parse response
        nlohmann::json response_json;
        try {
            response_json = nlohmann::json::parse(res->body);
        } catch (const nlohmann::json::exception& e) {
            response.error = "Failed to parse LLM response: " + std::string(e.what());
            return response;
        }

        if (!response_json.contains("message")) {
            // Try alternate response format
            if (response_json.contains("response")) {
                final_response_text = response_json["response"].get<std::string>();
                break;
            }
            response.error = "LLM response missing 'message' field";
            return response;
        }

        const auto& message = response_json["message"];

        // Extract thinking content if present (Ollama returns this as separate field)
        if (message.contains("thinking") && message["thinking"].is_string()) {
            std::string thinking = message["thinking"].get<std::string>();
            if (!thinking.empty()) {
                if (!final_thinking_text.empty()) {
                    final_thinking_text += "\n\n";
                }
                final_thinking_text += thinking;
                std::cout << "[AssistantClient] Received thinking content (" << thinking.length() << " chars)" << std::endl;
            }
        }

        // Check for tool_calls
        bool has_tool_calls = message.contains("tool_calls") &&
                              message["tool_calls"].is_array() &&
                              !message["tool_calls"].empty();

        if (!has_tool_calls) {
            // No tool calls - this is the final response
            if (message.contains("content") && message["content"].is_string()) {
                final_response_text = message["content"].get<std::string>();
            }
            break;
        }

        // Add assistant message with tool_calls to conversation
        messages.push_back(message);

        // Process each tool call
        bool executed_backend_tool = false;
        for (const auto& tool_call : message["tool_calls"]) {
            if (!tool_call.contains("function")) continue;

            const auto& func = tool_call["function"];
            std::string tool_name = func.value("name", "");
            if (tool_name.empty()) continue;

            // Parse arguments
            nlohmann::json params = nlohmann::json::object();
            if (func.contains("arguments")) {
                if (func["arguments"].is_string()) {
                    try {
                        params = nlohmann::json::parse(func["arguments"].get<std::string>());
                    } catch (...) {
                        params = nlohmann::json::object();
                    }
                } else if (func["arguments"].is_object()) {
                    params = func["arguments"];
                }
            }

            total_tool_calls++;

            // Track tool call info
            ToolCallInfo tc_info;
            tc_info.name = tool_name;
            tc_info.parameters = params;

            // Check if this is a backend tool
            if (tool_executor_ && tool_executor_->is_backend_tool(tool_name)) {
                // Execute on backend
                std::cout << "[AssistantClient] Executing backend tool: " << tool_name << std::endl;
                nlohmann::json result = tool_executor_->execute(tool_name, params);

                // Store result in tool call info
                tc_info.result = result.dump();
                tc_info.executed_on_backend = true;

                // Special handling for analyze_image - use Ollama vision format
                if (tool_name == "analyze_image" && result.contains("image_data") && result["success"].get<bool>()) {
                    // For vision models, add message with images array
                    std::string analysis_prompt = result.value("prompt", "Describe what you see in this image.");
                    std::string image_data = result["image_data"].get<std::string>();

                    messages.push_back({
                        {"role", "user"},
                        {"content", analysis_prompt},
                        {"images", nlohmann::json::array({image_data})}
                    });

                    std::cout << "[AssistantClient] Added vision message with image for analysis" << std::endl;
                } else {
                    // Add tool result message (Ollama format with tool_name)
                    messages.push_back({
                        {"role", "tool"},
                        {"tool_name", tool_name},
                        {"content", result.dump()}
                    });
                }
                executed_backend_tool = true;
            } else {
                // Collect as UI action for frontend
                std::cout << "[AssistantClient] Collected UI action: " << tool_name << std::endl;
                tc_info.executed_on_backend = false;

                AssistantAction action;
                action.type = tool_name;
                action.parameters = params;
                ui_actions.push_back(action);
            }

            all_tool_calls.push_back(tc_info);
        }

        // If we only collected UI actions (no backend tools executed), break the loop
        // The frontend needs to execute these actions
        if (!executed_backend_tool) {
            // Get any text content from the message
            if (message.contains("content") && message["content"].is_string()) {
                final_response_text = message["content"].get<std::string>();
            }
            break;
        }

        // Continue loop to send tool results back to LLM
    }

    // Check if we hit max iterations
    if (total_tool_calls >= MAX_TOOL_ITERATIONS * 5) {  // Reasonable upper bound
        std::cerr << "[AssistantClient] Warning: High number of tool calls: " << total_tool_calls << std::endl;
    }

    // Build final response
    response.success = true;
    response.actions = ui_actions;
    response.tool_calls = all_tool_calls;

    // If no native tool calls were used, try text-based action extraction
    if (ui_actions.empty() && !final_response_text.empty()) {
        auto text_actions = extract_actions(final_response_text);
        if (!text_actions.empty()) {
            response.actions = text_actions;
            // Remove action blocks from message
            std::regex action_block_regex(R"(`{2,3}json:action\s*\n[\s\S]*?\n`{2,3})", std::regex::icase);
            final_response_text = std::regex_replace(final_response_text, action_block_regex, "");
        }
    }

    // Trim whitespace from final response and thinking
    auto trim = [](std::string& s) {
        s.erase(0, s.find_first_not_of(" \t\n\r"));
        if (!s.empty()) {
            s.erase(s.find_last_not_of(" \t\n\r") + 1);
        }
    };
    trim(final_response_text);
    trim(final_thinking_text);
    response.message = final_response_text;
    response.thinking = final_thinking_text;

    // Add to conversation history
    {
        std::lock_guard<std::mutex> lock(history_mutex_);

        ConversationMessage user_msg;
        user_msg.role = MessageRole::User;
        user_msg.content = user_message;
        user_msg.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        history_.insert(history_.begin(), user_msg);

        // Add assistant response to history
        ConversationMessage assistant_msg;
        assistant_msg.role = MessageRole::Assistant;
        if (!final_response_text.empty()) {
            assistant_msg.content = final_response_text;
        } else if (!ui_actions.empty()) {
            // Store summary of actions for history
            std::string action_summary = "[Actions: ";
            for (size_t i = 0; i < ui_actions.size(); ++i) {
                if (i > 0) action_summary += ", ";
                action_summary += ui_actions[i].type;
            }
            action_summary += "]";
            assistant_msg.content = action_summary;
        }
        assistant_msg.thinking = final_thinking_text;
        assistant_msg.tool_calls = all_tool_calls;
        assistant_msg.timestamp = user_msg.timestamp;
        history_.insert(history_.begin(), assistant_msg);

        prune_history();
        save_history();
    }

    std::cout << "[AssistantClient] Chat completed"
              << " | iterations: " << (total_tool_calls > 0 ? "multiple" : "1")
              << " | message length: " << response.message.length()
              << " | thinking length: " << response.thinking.length()
              << " | UI actions: " << response.actions.size()
              << " | tool calls: " << response.tool_calls.size() << std::endl;

    return response;
}

bool AssistantClient::chat_stream(
    const std::string& user_message,
    const nlohmann::json& context,
    StreamCallback callback
) {
    if (!config_.enabled) {
        callback("error", {{"error", "Assistant is disabled"}});
        return false;
    }

    // Parse endpoint URL
    std::string host, path;
    int port;
    bool is_ssl;
    if (!parse_url(config_.endpoint, host, port, path, is_ssl)) {
        callback("error", {{"error", "Invalid LLM endpoint URL"}});
        return false;
    }

    // Create HTTP client (same as chat() method)
    httplib::Client client(host, port);
    client.set_connection_timeout(config_.timeout_seconds);
    client.set_read_timeout(config_.timeout_seconds);
    client.set_write_timeout(config_.timeout_seconds);

    httplib::Headers headers;
    if (!config_.api_key.empty()) {
        headers.emplace("Authorization", "Bearer " + config_.api_key);
    }
    headers.emplace("Content-Type", "application/json");

    // Build messages array
    nlohmann::json messages = build_messages(user_message, context);

    // First: Execute tool loop (non-streaming) to get tool results
    const int MAX_TOOL_ITERATIONS = 10;
    std::vector<AssistantAction> ui_actions;
    std::vector<ToolCallInfo> all_tool_calls;
    std::string accumulated_thinking;  // Track thinking across all phases
    int iteration = 0;

    while (iteration < MAX_TOOL_ITERATIONS) {
        // Non-streaming request for tool execution phase
        nlohmann::json request_body = {
            {"model", config_.model},
            {"messages", messages},
            {"tools", build_tools()},
            {"stream", false},  // Non-streaming for tool phase
            {"think", true},
            {"options", {
                {"temperature", config_.temperature},
                {"num_predict", config_.max_tokens}
            }}
        };

        std::cout << "[AssistantClient] Stream: tool iteration " << iteration + 1 << std::endl;

        auto res = client.Post("/api/chat", headers, request_body.dump(), "application/json");
        if (!res || res->status != 200) {
            callback("error", {{"error", "Failed to get tool response"}});
            return false;
        }

        nlohmann::json response_json;
        try {
            response_json = nlohmann::json::parse(res->body);
        } catch (...) {
            callback("error", {{"error", "Failed to parse tool response"}});
            return false;
        }

        if (!response_json.contains("message")) {
            callback("error", {{"error", "Invalid response format"}});
            return false;
        }

        const auto& message = response_json["message"];

        // Extract thinking content from tool iteration (same as non-streaming chat)
        if (message.contains("thinking") && message["thinking"].is_string()) {
            std::string thinking = message["thinking"].get<std::string>();
            if (!thinking.empty()) {
                if (!accumulated_thinking.empty()) {
                    accumulated_thinking += "\n\n";
                }
                accumulated_thinking += thinking;
                std::cout << "[AssistantClient] Stream: tool phase thinking (" << thinking.size() << " bytes)" << std::endl;
                // Emit thinking content so client receives it during tool execution
                if (!callback("thinking", {{"content", thinking}})) {
                    std::cerr << "[AssistantClient] Stream: callback failed for tool phase thinking" << std::endl;
                    return false;
                }
            }
        }

        // Check for tool_calls
        bool has_tool_calls = message.contains("tool_calls") &&
                              message["tool_calls"].is_array() &&
                              !message["tool_calls"].empty();

        if (!has_tool_calls) {
            // No more tool calls - we can stream the final response
            break;
        }

        // Add assistant message with tool_calls to conversation
        messages.push_back(message);

        // Process tool calls
        bool executed_backend_tool = false;
        for (const auto& tool_call : message["tool_calls"]) {
            if (!tool_call.contains("function")) continue;

            const auto& func = tool_call["function"];
            std::string tool_name = func.value("name", "");
            if (tool_name.empty()) continue;

            nlohmann::json params = nlohmann::json::object();
            if (func.contains("arguments")) {
                if (func["arguments"].is_string()) {
                    try {
                        params = nlohmann::json::parse(func["arguments"].get<std::string>());
                    } catch (...) {}
                } else if (func["arguments"].is_object()) {
                    params = func["arguments"];
                }
            }

            ToolCallInfo tc_info;
            tc_info.name = tool_name;
            tc_info.parameters = params;

            if (tool_executor_ && tool_executor_->is_backend_tool(tool_name)) {
                std::cout << "[AssistantClient] Stream: executing backend tool: " << tool_name << std::endl;
                nlohmann::json result = tool_executor_->execute(tool_name, params);

                tc_info.result = result.dump();
                tc_info.executed_on_backend = true;

                // Emit tool_call event
                callback("tool_call", tc_info.to_json());

                // Special handling for analyze_image - use Ollama vision format
                if (tool_name == "analyze_image" && result.contains("image_data") && result["success"].get<bool>()) {
                    std::string analysis_prompt = result.value("prompt", "Describe what you see in this image.");
                    std::string image_data = result["image_data"].get<std::string>();

                    messages.push_back({
                        {"role", "user"},
                        {"content", analysis_prompt},
                        {"images", nlohmann::json::array({image_data})}
                    });

                    std::cout << "[AssistantClient] Stream: Added vision message with image" << std::endl;
                } else {
                    // Add tool result message (Ollama format with tool_name)
                    messages.push_back({
                        {"role", "tool"},
                        {"tool_name", tool_name},
                        {"content", result.dump()}
                    });
                }
                executed_backend_tool = true;
            } else {
                tc_info.executed_on_backend = false;
                callback("tool_call", tc_info.to_json());

                AssistantAction action;
                action.type = tool_name;
                action.parameters = params;
                ui_actions.push_back(action);
            }

            all_tool_calls.push_back(tc_info);
        }

        if (!executed_backend_tool) {
            break;
        }

        iteration++;
    }

    // Now stream the final response
    std::cout << "[AssistantClient] Stream: starting final response stream" << std::endl;

    nlohmann::json stream_request = {
        {"model", config_.model},
        {"messages", messages},
        {"stream", true},  // Now we stream!
        {"think", true},
        {"options", {
            {"temperature", config_.temperature},
            {"num_predict", config_.max_tokens}
        }}
    };

    std::string accumulated_content;
    // accumulated_thinking already declared before tool loop - continues accumulating here
    bool success = true;

    // Use content receiver for streaming
    auto stream_res = client.Post(
        "/api/chat",
        headers,
        stream_request.dump(),
        "application/json",
        [&](const char* data, size_t data_length) {
            std::string chunk(data, data_length);

            // Ollama returns NDJSON - multiple JSON objects separated by newlines
            std::istringstream stream(chunk);
            std::string line;
            while (std::getline(stream, line)) {
                if (line.empty()) continue;

                try {
                    auto json = nlohmann::json::parse(line);

                    if (json.contains("message")) {
                        const auto& msg = json["message"];

                        // Check for thinking content (ensure it's a non-empty string)
                        if (msg.contains("thinking") && msg["thinking"].is_string()) {
                            std::string thinking = msg["thinking"].get<std::string>();
                            if (!thinking.empty()) {
                                accumulated_thinking += thinking;
                                std::cout << "[AssistantClient] Stream: emitting thinking chunk (" << thinking.size() << " bytes)" << std::endl;
                                if (!callback("thinking", {{"content", thinking}})) {
                                    std::cerr << "[AssistantClient] Stream: callback failed for thinking" << std::endl;
                                    return false;
                                }
                            }
                        }

                        // Check for regular content (ensure it's a non-empty string)
                        if (msg.contains("content") && msg["content"].is_string()) {
                            std::string content = msg["content"].get<std::string>();
                            if (!content.empty()) {
                                accumulated_content += content;
                                std::cout << "[AssistantClient] Stream: emitting content chunk (" << content.size() << " bytes)" << std::endl;
                                if (!callback("content", {{"content", content}})) {
                                    std::cerr << "[AssistantClient] Stream: callback failed for content" << std::endl;
                                    return false;
                                }
                            }
                        }
                    }

                    // Check if done
                    if (json.value("done", false)) {
                        // Done streaming
                        return true;
                    }
                } catch (const nlohmann::json::exception& e) {
                    // Skip malformed lines
                    std::cerr << "[AssistantClient] Stream: JSON parse error on line: " << line << " - " << e.what() << std::endl;
                }
            }
            return true;
        }
    );

    if (!stream_res) {
        std::cerr << "[AssistantClient] Stream: request failed - no response" << std::endl;
        callback("error", {{"error", "Stream request failed - no response from server"}});
        success = false;
    } else if (stream_res->status != 200) {
        std::cerr << "[AssistantClient] Stream: request failed - status " << stream_res->status
                  << ", body: " << stream_res->body.substr(0, 500) << std::endl;
        callback("error", {{"error", "Stream request failed with status " + std::to_string(stream_res->status)}});
        success = false;
    }

    // Send done event with actions and tool calls
    nlohmann::json done_data;
    if (!ui_actions.empty()) {
        nlohmann::json actions_json = nlohmann::json::array();
        for (const auto& action : ui_actions) {
            actions_json.push_back(action.to_json());
        }
        done_data["actions"] = actions_json;
    }
    if (!all_tool_calls.empty()) {
        nlohmann::json tool_calls_json = nlohmann::json::array();
        for (const auto& tc : all_tool_calls) {
            tool_calls_json.push_back(tc.to_json());
        }
        done_data["tool_calls"] = tool_calls_json;
    }
    callback("done", done_data);

    // Add to conversation history
    {
        std::lock_guard<std::mutex> lock(history_mutex_);

        ConversationMessage user_msg;
        user_msg.role = MessageRole::User;
        user_msg.content = user_message;
        user_msg.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        history_.insert(history_.begin(), user_msg);

        ConversationMessage assistant_msg;
        assistant_msg.role = MessageRole::Assistant;
        assistant_msg.content = accumulated_content;
        assistant_msg.thinking = accumulated_thinking;
        assistant_msg.tool_calls = all_tool_calls;
        assistant_msg.timestamp = user_msg.timestamp;
        history_.insert(history_.begin(), assistant_msg);

        prune_history();
        save_history();
    }

    std::cout << "[AssistantClient] Stream completed | content: " << accumulated_content.length()
              << " chars | thinking: " << accumulated_thinking.length() << " chars"
              << " | tool_calls: " << all_tool_calls.size() << std::endl;

    return success;
}

std::vector<ConversationMessage> AssistantClient::get_history() const {
    std::lock_guard<std::mutex> lock(history_mutex_);
    return history_;
}

size_t AssistantClient::get_history_count() const {
    std::lock_guard<std::mutex> lock(history_mutex_);
    return history_.size();
}

void AssistantClient::clear_history() {
    std::lock_guard<std::mutex> lock(history_mutex_);
    history_.clear();
    save_history();
    std::cout << "[AssistantClient] History cleared" << std::endl;
}

bool AssistantClient::test_connection() {
    if (!config_.enabled) {
        return false;
    }

    std::string host, path;
    int port;
    bool is_ssl;

    if (!parse_url(config_.endpoint, host, port, path, is_ssl)) {
        return false;
    }

    httplib::Client client(host, port);
    client.set_connection_timeout(5);
    client.set_read_timeout(5);

    auto res = client.Get("/api/tags");
    return res && res->status == 200;
}

std::vector<std::string> AssistantClient::get_available_models() {
    std::vector<std::string> models;

    if (!config_.enabled) {
        return models;
    }

    std::string host, path;
    int port;
    bool is_ssl;

    if (!parse_url(config_.endpoint, host, port, path, is_ssl)) {
        return models;
    }

    httplib::Client client(host, port);
    client.set_connection_timeout(config_.timeout_seconds);
    client.set_read_timeout(config_.timeout_seconds);

    httplib::Headers headers;
    if (!config_.api_key.empty()) {
        headers.emplace("Authorization", "Bearer " + config_.api_key);
    }

    auto res = client.Get("/api/tags", headers);

    if (res && res->status == 200) {
        try {
            auto json = nlohmann::json::parse(res->body);
            if (json.contains("models") && json["models"].is_array()) {
                for (const auto& model : json["models"]) {
                    if (model.contains("name")) {
                        models.push_back(model["name"].get<std::string>());
                    }
                }
            }
        } catch (const nlohmann::json::exception& e) {
            std::cerr << "[AssistantClient] Failed to parse models response: " << e.what() << std::endl;
        }
    }

    return models;
}

nlohmann::json AssistantClient::get_status() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    bool connected = const_cast<AssistantClient*>(this)->test_connection();
    auto models = const_cast<AssistantClient*>(this)->get_available_models();

    return {
        {"enabled", config_.enabled},
        {"connected", connected},
        {"endpoint", config_.endpoint},
        {"model", config_.model},
        {"available_models", models},
        {"proactive_suggestions", config_.proactive_suggestions}
    };
}

nlohmann::json AssistantClient::get_settings() const {
    std::lock_guard<std::mutex> lock(config_mutex_);

    // Return actual stored value, not effective value
    // The default prompt is only used internally when building chat requests
    // Also include the default system prompt for reference in the UI
    return {
        {"enabled", config_.enabled},
        {"endpoint", config_.endpoint},
        {"model", config_.model},
        {"temperature", config_.temperature},
        {"max_tokens", config_.max_tokens},
        {"timeout_seconds", config_.timeout_seconds},
        {"max_history_turns", config_.max_history_turns},
        {"proactive_suggestions", config_.proactive_suggestions},
        {"system_prompt", config_.system_prompt},
        {"default_system_prompt", DEFAULT_ASSISTANT_SYSTEM_PROMPT},
        {"has_api_key", !config_.api_key.empty()}
    };
}

bool AssistantClient::update_settings(const nlohmann::json& settings) {
    std::lock_guard<std::mutex> lock(config_mutex_);

    try {
        if (settings.contains("enabled") && settings["enabled"].is_boolean()) {
            config_.enabled = settings["enabled"].get<bool>();
        }
        if (settings.contains("endpoint") && settings["endpoint"].is_string()) {
            config_.endpoint = settings["endpoint"].get<std::string>();
        }
        if (settings.contains("model") && settings["model"].is_string()) {
            config_.model = settings["model"].get<std::string>();
        }
        if (settings.contains("temperature") && settings["temperature"].is_number()) {
            config_.temperature = settings["temperature"].get<float>();
        }
        if (settings.contains("max_tokens") && settings["max_tokens"].is_number()) {
            config_.max_tokens = settings["max_tokens"].get<int>();
        }
        if (settings.contains("timeout_seconds") && settings["timeout_seconds"].is_number()) {
            config_.timeout_seconds = settings["timeout_seconds"].get<int>();
        }
        if (settings.contains("max_history_turns") && settings["max_history_turns"].is_number()) {
            config_.max_history_turns = settings["max_history_turns"].get<int>();
        }
        if (settings.contains("proactive_suggestions") && settings["proactive_suggestions"].is_boolean()) {
            config_.proactive_suggestions = settings["proactive_suggestions"].get<bool>();
        }
        if (settings.contains("system_prompt") && settings["system_prompt"].is_string()) {
            config_.system_prompt = settings["system_prompt"].get<std::string>();
        }
        if (settings.contains("api_key") && settings["api_key"].is_string()) {
            config_.api_key = settings["api_key"].get<std::string>();
        }

        std::cout << "[AssistantClient] Settings updated - model: " << config_.model
                  << ", endpoint: " << config_.endpoint << std::endl;

        // Persist to config file
        if (!config_file_path_.empty()) {
            if (!save_to_config()) {
                std::cerr << "[AssistantClient] Warning: Failed to persist settings to config file" << std::endl;
            }
        }

        // Refresh model capabilities when model changes
        if (settings.contains("model") && settings["model"].is_string()) {
            refresh_model_capabilities();
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "[AssistantClient] Failed to update settings: " << e.what() << std::endl;
        return false;
    }
}

ModelCapabilities AssistantClient::get_model_info(const std::string& model_name) {
    ModelCapabilities caps;
    std::string target_model = model_name.empty() ? config_.model : model_name;

    if (target_model.empty()) {
        std::cerr << "[AssistantClient] get_model_info: no model specified" << std::endl;
        return caps;
    }

    caps.model_name = target_model;

    // Parse endpoint URL
    std::string host, path;
    int port;
    bool is_ssl;

    if (!parse_url(config_.endpoint, host, port, path, is_ssl)) {
        std::cerr << "[AssistantClient] get_model_info: invalid endpoint URL" << std::endl;
        return caps;
    }

    // Create HTTP client
    httplib::Client client(host, port);
    client.set_connection_timeout(10);  // Short timeout for info endpoint
    client.set_read_timeout(10);

    // Prepare headers
    httplib::Headers headers;
    if (!config_.api_key.empty()) {
        headers.emplace("Authorization", "Bearer " + config_.api_key);
    }
    headers.emplace("Content-Type", "application/json");

    // POST to /api/show
    nlohmann::json request_body = {{"name", target_model}};
    auto res = client.Post("/api/show", headers, request_body.dump(), "application/json");

    if (!res) {
        std::cerr << "[AssistantClient] get_model_info: failed to connect" << std::endl;
        return caps;
    }

    if (res->status != 200) {
        std::cerr << "[AssistantClient] get_model_info: HTTP " << res->status << std::endl;
        return caps;
    }

    try {
        auto json = nlohmann::json::parse(res->body);

        // Extract capabilities array
        if (json.contains("capabilities") && json["capabilities"].is_array()) {
            for (const auto& cap : json["capabilities"]) {
                if (cap.is_string()) {
                    caps.capabilities.push_back(cap.get<std::string>());
                }
            }
        }

        // Extract model_info - Ollama uses architecture-prefixed keys like "llama.context_length"
        if (json.contains("model_info") && json["model_info"].is_object()) {
            const auto& model_info = json["model_info"];

            // Look for context_length with any architecture prefix
            for (auto& [key, value] : model_info.items()) {
                if (key.find(".context_length") != std::string::npos && value.is_number()) {
                    caps.context_length = value.get<int>();
                    break;
                }
            }
        }

        // Extract details
        if (json.contains("details") && json["details"].is_object()) {
            const auto& details = json["details"];
            caps.family = details.value("family", "");
            caps.parameter_size = details.value("parameter_size", "");
        }

        std::cout << "[AssistantClient] Model info for " << target_model
                  << ": capabilities=[";
        for (size_t i = 0; i < caps.capabilities.size(); ++i) {
            std::cout << caps.capabilities[i];
            if (i < caps.capabilities.size() - 1) std::cout << ",";
        }
        std::cout << "], context=" << caps.context_length
                  << ", family=" << caps.family
                  << ", params=" << caps.parameter_size << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "[AssistantClient] get_model_info: parse error: " << e.what() << std::endl;
    }

    return caps;
}

void AssistantClient::refresh_model_capabilities() {
    std::lock_guard<std::mutex> lock(capabilities_mutex_);

    if (config_.model.empty()) {
        current_model_capabilities_ = ModelCapabilities{};
        return;
    }

    // Get fresh capabilities (don't hold lock during network call)
    {
        // Release config mutex temporarily
    }
    ModelCapabilities caps = get_model_info(config_.model);

    current_model_capabilities_ = caps;
    std::cout << "[AssistantClient] Cached capabilities for model: " << config_.model
              << " (vision=" << (caps.has_vision() ? "true" : "false") << ")" << std::endl;
}

bool AssistantClient::current_model_has_vision() const {
    std::lock_guard<std::mutex> lock(capabilities_mutex_);
    return current_model_capabilities_.has_vision();
}

const ModelCapabilities& AssistantClient::get_current_model_capabilities() const {
    std::lock_guard<std::mutex> lock(capabilities_mutex_);
    return current_model_capabilities_;
}

bool AssistantClient::save_to_config() {
    if (config_file_path_.empty()) {
        return false;
    }

    try {
        std::ifstream in_file(config_file_path_);
        if (!in_file.is_open()) {
            std::cerr << "[AssistantClient] Failed to open config file for reading: " << config_file_path_ << std::endl;
            return false;
        }

        nlohmann::json config_json;
        in_file >> config_json;
        in_file.close();

        // Update only the assistant section
        config_json["assistant"] = {
            {"enabled", config_.enabled},
            {"endpoint", config_.endpoint},
            {"api_key", config_.api_key},
            {"model", config_.model},
            {"temperature", config_.temperature},
            {"max_tokens", config_.max_tokens},
            {"timeout_seconds", config_.timeout_seconds},
            {"system_prompt", config_.system_prompt},
            {"max_history_turns", config_.max_history_turns},
            {"proactive_suggestions", config_.proactive_suggestions}
        };

        std::ofstream out_file(config_file_path_);
        if (!out_file.is_open()) {
            std::cerr << "[AssistantClient] Failed to open config file for writing: " << config_file_path_ << std::endl;
            return false;
        }

        out_file << config_json.dump(4);
        out_file.close();

        std::cout << "[AssistantClient] Settings persisted to " << config_file_path_ << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[AssistantClient] Failed to save config: " << e.what() << std::endl;
        return false;
    }
}

void AssistantClient::prune_history() {
    // Keep only max_history_turns * 2 messages (user + assistant pairs)
    size_t max_messages = static_cast<size_t>(config_.max_history_turns) * 2;
    if (history_.size() > max_messages) {
        history_.resize(max_messages);
    }
}

void AssistantClient::save_history() {
    try {
        nlohmann::json state;
        nlohmann::json items = nlohmann::json::array();

        for (const auto& msg : history_) {
            items.push_back(msg.to_json());
        }

        state["items"] = items;
        state["version"] = 1;

        std::ofstream file(history_file_);
        if (file.is_open()) {
            file << state.dump(2);
        }
    } catch (const std::exception& e) {
        std::cerr << "[AssistantClient] Failed to save history: " << e.what() << std::endl;
    }
}

void AssistantClient::load_history() {
    if (!utils::file_exists(history_file_)) {
        return;
    }

    try {
        std::ifstream file(history_file_);
        if (!file.is_open()) {
            return;
        }

        nlohmann::json state;
        file >> state;

        if (state.contains("items") && state["items"].is_array()) {
            for (const auto& item : state["items"]) {
                history_.push_back(ConversationMessage::from_json(item));
            }
        }

        std::cout << "[AssistantClient] Loaded " << history_.size() << " history messages" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[AssistantClient] Failed to load history: " << e.what() << std::endl;
        history_.clear();
    }
}

} // namespace sdcpp
