#include "assistant_client.hpp"
#include "utils.hpp"

#include <httplib.h>

#include <fstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <regex>

namespace fs = std::filesystem;

namespace sdcpp {

// ConversationMessage JSON serialization
nlohmann::json ConversationMessage::to_json() const {
    return {
        {"role", role_to_string(role)},
        {"content", content},
        {"timestamp", timestamp}
    };
}

ConversationMessage ConversationMessage::from_json(const nlohmann::json& j) {
    ConversationMessage msg;
    msg.role = string_to_role(j.value("role", "user"));
    msg.content = j.value("content", "");
    msg.timestamp = j.value("timestamp", 0LL);
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

// AssistantResponse JSON serialization
nlohmann::json AssistantResponse::to_json() const {
    nlohmann::json j = {
        {"success", success},
        {"message", message}
    };

    if (!actions.empty()) {
        nlohmann::json actions_json = nlohmann::json::array();
        for (const auto& action : actions) {
            actions_json.push_back(action.to_json());
        }
        j["actions"] = actions_json;
    }

    if (error.has_value()) {
        j["error"] = error.value();
    }

    return j;
}

// AssistantClient implementation
AssistantClient::AssistantClient(const AssistantConfig& config, const std::string& data_dir,
                                   const std::string& config_file_path)
    : config_(config)
    , history_file_((fs::path(data_dir) / "assistant_history.json").string())
    , config_file_path_(config_file_path)
{
    load_history();
    std::cout << "[AssistantClient] Initialized with endpoint: " << config_.endpoint
              << " | model: " << config_.model
              << " | enabled: " << (config_.enabled ? "true" : "false");
    if (!config_file_path_.empty()) {
        std::cout << " | config persistence enabled";
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

    // Add context as a system message (before the user message)
    std::string context_str = "## Current Application Context\n```json\n" + context.dump(2) + "\n```";
    messages.push_back({
        {"role", "system"},
        {"content", context_str}
    });

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
            {"parameters", {{"type", "object"}, {"properties", {}}}}
        }}
    });

    // refresh_models tool
    tools.push_back({
        {"type", "function"},
        {"function", {
            {"name", "refresh_models"},
            {"description", "Refresh the list of available models by rescanning model directories. Use this after downloading or converting models to make them available for loading."},
            {"parameters", {{"type", "object"}, {"properties", {}}}}
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

    // search_jobs tool
    tools.push_back({
        {"type", "function"},
        {"function", {
            {"name", "search_jobs"},
            {"description", "Search for jobs by prompt text, status, or type"},
            {"parameters", {
                {"type", "object"},
                {"properties", {
                    {"prompt", {{"type", "string"}, {"description", "Search for jobs containing this text in prompt"}}},
                    {"status", {{"type", "string"}, {"enum", {"pending", "processing", "completed", "failed", "cancelled"}}, {"description", "Filter by status"}}},
                    {"type", {{"type", "string"}, {"enum", {"txt2img", "img2img", "txt2vid", "upscale"}}, {"description", "Filter by job type"}}},
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
            {"parameters", {{"type", "object"}, {"properties", {}}}}
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
            {"parameters", {{"type", "object"}, {"properties", {}}}}
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

    // convert_model tool
    tools.push_back({
        {"type", "function"},
        {"function", {
            {"name", "convert_model"},
            {"description", "Convert a model to GGUF format with specified quantization type. Common types: q8_0, q5_k, q4_k, f16, bf16 (full list from /options API)"},
            {"parameters", {
                {"type", "object"},
                {"required", nlohmann::json::array({"output_type"})},
                {"properties", {
                    {"model_name", {{"type", "string"}, {"description", "Model name from available_models (relative path)"}}},
                    {"input_path", {{"type", "string"}, {"description", "Full absolute path to the model file"}}},
                    {"model_type", {{"type", "string"}, {"enum", {"checkpoint", "diffusion", "vae", "lora", "clip", "t5", "controlnet", "llm", "esrgan", "taesd"}}, {"description", "Type of model (default: checkpoint)"}}},
                    {"output_type", {{"type", "string"}, {"description", "Quantization type (e.g., q8_0, q5_k, q4_k, f16, bf16)"}}},
                    {"output_path", {{"type", "string"}, {"description", "Custom output path (default: auto-generated with .gguf extension)"}}},
                    {"vae_path", {{"type", "string"}, {"description", "Path to VAE to bake into the model"}}},
                    {"tensor_type_rules", {{"type", "string"}, {"description", "Custom tensor type rules"}}}
                }}
            }}
        }}
    });

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

    // Note: Native tool calling is disabled for now.
    // Ollama's tool calling requires multi-turn conversation with role="tool" responses,
    // which would require significant refactoring since our actions are executed in the frontend.
    // We rely on text-based json:action blocks instead, which work with all models.

    // Build request body (without native tools)
    nlohmann::json request_body = {
        {"model", config_.model},
        {"messages", messages},
        {"stream", false},
        {"options", {
            {"temperature", config_.temperature},
            {"num_predict", config_.max_tokens}
        }}
    };

    std::cout << "[AssistantClient] Sending chat request to " << config_.endpoint
              << "/api/chat" << std::endl;

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
    try {
        auto response_json = nlohmann::json::parse(res->body);

        std::string response_text;
        std::vector<AssistantAction> tool_actions;
        bool used_native_tools = false;

        if (response_json.contains("message")) {
            const auto& message = response_json["message"];

            // First, try to extract native tool_calls (Ollama tool calling)
            if (message.contains("tool_calls") && message["tool_calls"].is_array() &&
                !message["tool_calls"].empty()) {
                tool_actions = extract_tool_calls(message);
                used_native_tools = !tool_actions.empty();
                if (used_native_tools) {
                    std::cout << "[AssistantClient] Using native Ollama tool calling: "
                              << tool_actions.size() << " tool calls" << std::endl;
                }
            }

            // Get text content (may be empty if only tool calls)
            if (message.contains("content") && message["content"].is_string()) {
                response_text = message["content"].get<std::string>();
            }
        } else if (response_json.contains("response")) {
            response_text = response_json["response"].get<std::string>();
        }

        // If we got native tool calls, use those; otherwise try text-based extraction
        if (used_native_tools) {
            response.success = true;
            response.actions = tool_actions;
            response.message = response_text;

            // Trim whitespace from message
            auto& msg = response.message;
            msg.erase(0, msg.find_first_not_of(" \t\n\r"));
            if (!msg.empty()) {
                msg.erase(msg.find_last_not_of(" \t\n\r") + 1);
            }
        } else {
            // Fall back to text-based action extraction
            if (response_text.empty()) {
                response.error = "LLM returned an empty response";
                return response;
            }
            // Parse the response to extract message and actions from text
            response = parse_response(response_text);
        }

        // Add user message to history
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
            // For native tool calls, store a summary of the calls for history
            if (used_native_tools && response_text.empty()) {
                std::string tool_summary = "[Tool calls: ";
                for (size_t i = 0; i < tool_actions.size(); ++i) {
                    if (i > 0) tool_summary += ", ";
                    tool_summary += tool_actions[i].type;
                }
                tool_summary += "]";
                assistant_msg.content = tool_summary;
            } else {
                assistant_msg.content = response_text;
            }
            assistant_msg.timestamp = user_msg.timestamp;
            history_.insert(history_.begin(), assistant_msg);

            prune_history();
            save_history();
        }

        std::cout << "[AssistantClient] Chat successful"
                  << ", message length: " << response.message.length()
                  << ", actions: " << response.actions.size()
                  << ", native tools: " << (used_native_tools ? "yes" : "no") << std::endl;

    } catch (const nlohmann::json::exception& e) {
        response.error = "Failed to parse LLM response: " + std::string(e.what());
        return response;
    }

    return response;
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

        return true;
    } catch (const std::exception& e) {
        std::cerr << "[AssistantClient] Failed to update settings: " << e.what() << std::endl;
        return false;
    }
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
