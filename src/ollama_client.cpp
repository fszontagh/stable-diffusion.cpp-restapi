#include "ollama_client.hpp"
#include "utils.hpp"

#include <httplib.h>

#include <fstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <regex>

namespace fs = std::filesystem;

namespace sdcpp {

// PromptEnhancement JSON serialization
nlohmann::json PromptEnhancement::to_json() const {
    return {
        {"id", id},
        {"original_prompt", original_prompt},
        {"enhanced_prompt", enhanced_prompt},
        {"model_used", model_used},
        {"created_at", created_at},
        {"success", success},
        {"error_message", error_message}
    };
}

PromptEnhancement PromptEnhancement::from_json(const nlohmann::json& j) {
    PromptEnhancement entry;
    entry.id = j.value("id", "");
    entry.original_prompt = j.value("original_prompt", "");
    entry.enhanced_prompt = j.value("enhanced_prompt", "");
    entry.model_used = j.value("model_used", "");
    entry.created_at = j.value("created_at", 0LL);
    entry.success = j.value("success", true);
    entry.error_message = j.value("error_message", "");
    return entry;
}

// OllamaClient implementation
OllamaClient::OllamaClient(const OllamaConfig& config, const std::string& data_dir,
                           const std::string& config_file_path)
    : config_(config)
    , history_file_((fs::path(data_dir) / "ollama_history.json").string())
    , config_file_path_(config_file_path)
{
    load_history();
    std::cout << "[OllamaClient] Initialized with endpoint: " << config_.endpoint
              << " | model: " << config_.model
              << " | enabled: " << (config_.enabled ? "true" : "false");
    if (!config_file_path_.empty()) {
        std::cout << " | config persistence enabled";
    }
    std::cout << std::endl;
}

OllamaClient::~OllamaClient() {
    // History is saved after each modification
}

bool OllamaClient::parse_url(const std::string& url, std::string& host, int& port, std::string& path, bool& is_ssl) {
    // Simple URL parser for http(s)://host:port/path format
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

std::string OllamaClient::generate_uuid() {
    return utils::generate_uuid();
}

std::pair<bool, std::string> OllamaClient::enhance_prompt(
    const std::string& prompt,
    const std::optional<std::string>& custom_system_prompt)
{
    if (!config_.enabled) {
        return {false, "Ollama integration is disabled"};
    }

    if (prompt.empty()) {
        return {false, "Prompt cannot be empty"};
    }

    // Parse endpoint URL
    std::string host, path;
    int port;
    bool is_ssl;

    if (!parse_url(config_.endpoint, host, port, path, is_ssl)) {
        return {false, "Invalid Ollama endpoint URL"};
    }

    // Create HTTP client
    std::unique_ptr<httplib::Client> client;
    if (is_ssl) {
        client = std::make_unique<httplib::Client>(host, port);
        // Note: For production, you'd want to configure SSL properly
    } else {
        client = std::make_unique<httplib::Client>(host, port);
    }

    client->set_connection_timeout(config_.timeout_seconds);
    client->set_read_timeout(config_.timeout_seconds);
    client->set_write_timeout(config_.timeout_seconds);

    // Prepare headers
    httplib::Headers headers;
    if (!config_.api_key.empty()) {
        headers.emplace("Authorization", "Bearer " + config_.api_key);
    }
    headers.emplace("Content-Type", "application/json");

    // Determine system prompt to use
    std::string system_prompt = custom_system_prompt.value_or(
        config_.system_prompt.empty() ? DEFAULT_OLLAMA_SYSTEM_PROMPT : config_.system_prompt
    );

    // Build request body for /api/chat (works better with chat-tuned models like llama3.2)
    nlohmann::json messages = nlohmann::json::array();
    messages.push_back({{"role", "system"}, {"content", system_prompt}});
    messages.push_back({{"role", "user"}, {"content", prompt}});

    nlohmann::json request_body = {
        {"model", config_.model},
        {"messages", messages},
        {"stream", false},
        {"think", true},  // Enable thinking for better quality responses
        {"options", {
            {"temperature", config_.temperature},
            {"num_predict", config_.max_tokens}
        }}
    };

    std::cout << "[OllamaClient] Sending enhance request to " << config_.endpoint
              << "/api/chat for prompt: \"" << prompt.substr(0, 50) << "...\"" << std::endl;

    // Make request
    auto res = client->Post("/api/chat", headers, request_body.dump(), "application/json");

    // Create history entry
    PromptEnhancement entry;
    entry.id = generate_uuid();
    entry.original_prompt = prompt;
    entry.model_used = config_.model;
    entry.created_at = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    if (!res) {
        entry.success = false;
        entry.error_message = "Failed to connect to Ollama server";
        add_to_history(entry);
        std::cerr << "[OllamaClient] Connection error: " << httplib::to_string(res.error()) << std::endl;
        return {false, entry.error_message};
    }

    if (res->status != 200) {
        entry.success = false;
        entry.error_message = "Ollama API error: HTTP " + std::to_string(res->status);
        add_to_history(entry);
        std::cerr << "[OllamaClient] API error: " << res->status << " - " << res->body << std::endl;
        return {false, entry.error_message};
    }

    // Parse response
    try {
        std::cout << "[OllamaClient] Raw response body: " << res->body << std::endl;

        auto response_json = nlohmann::json::parse(res->body);

        // Chat API returns message.content, generate API returns response
        std::string response_text;
        bool is_thinking_model = false;

        if (response_json.contains("message")) {
            const auto& message = response_json["message"];

            // Check if this is a thinking model (has thinking field)
            if (message.contains("thinking") && message["thinking"].is_string() &&
                !message["thinking"].get<std::string>().empty()) {
                is_thinking_model = true;
            }

            // Get content (the actual response)
            if (message.contains("content") && message["content"].is_string()) {
                response_text = message["content"].get<std::string>();
            }

            if (!response_text.empty()) {
                std::cout << "[OllamaClient] Extracted from message.content" << std::endl;
            }
        } else if (response_json.contains("response")) {
            // Generate API format: {"response": "..."}
            response_text = response_json["response"].get<std::string>();
            std::cout << "[OllamaClient] Extracted from response" << std::endl;
        } else {
            entry.success = false;
            entry.error_message = "Invalid response format from Ollama. Keys found: ";
            for (auto& [key, val] : response_json.items()) {
                entry.error_message += key + " ";
            }
            add_to_history(entry);
            std::cerr << "[OllamaClient] Invalid response format: " << res->body << std::endl;
            return {false, entry.error_message};
        }

        entry.enhanced_prompt = response_text;

        // Trim whitespace from enhanced prompt
        auto& ep = entry.enhanced_prompt;
        ep.erase(0, ep.find_first_not_of(" \t\n\r"));
        if (!ep.empty()) {
            ep.erase(ep.find_last_not_of(" \t\n\r") + 1);
        }

        // Check if response is empty after trimming
        if (ep.empty()) {
            entry.success = false;

            // Check for specific reasons why content might be empty
            if (is_thinking_model) {
                // Thinking models use tokens for reasoning, leaving fewer for the actual response
                entry.error_message = "Thinking model used all tokens for reasoning without producing a final response. "
                    "Increase 'max_tokens' in Ollama settings (try 2000-4000 for thinking models) "
                    "to allow tokens for both thinking and the response.";
            } else if (response_json.contains("done_reason")) {
                std::string done_reason = response_json["done_reason"].get<std::string>();
                if (done_reason == "content_filter") {
                    entry.error_message = "Content was filtered by the model. Try a different prompt or model.";
                } else {
                    entry.error_message = "Ollama returned empty response (reason: " + done_reason + ")";
                }
            } else {
                entry.error_message = "Ollama returned an empty response. Check your model and system prompt.";
            }

            add_to_history(entry);
            std::cerr << "[OllamaClient] Empty response from Ollama: " << entry.error_message << std::endl;
            return {false, entry.error_message};
        }

        entry.success = true;
        add_to_history(entry);
        std::cout << "[OllamaClient] Enhancement successful, response length: "
                  << entry.enhanced_prompt.length() << std::endl;
        return {true, entry.enhanced_prompt};
    } catch (const nlohmann::json::exception& e) {
        entry.success = false;
        entry.error_message = "Failed to parse Ollama response: " + std::string(e.what());
        add_to_history(entry);
        return {false, entry.error_message};
    }
}

std::vector<PromptEnhancement> OllamaClient::get_history(size_t limit, size_t offset) const {
    std::lock_guard<std::mutex> lock(history_mutex_);

    std::vector<PromptEnhancement> result;

    if (offset >= history_.size()) {
        return result;
    }

    // History is stored newest first
    size_t start = offset;
    size_t end = (limit == 0) ? history_.size() : std::min(offset + limit, history_.size());

    for (size_t i = start; i < end; ++i) {
        result.push_back(history_[i]);
    }

    return result;
}

size_t OllamaClient::get_history_count() const {
    std::lock_guard<std::mutex> lock(history_mutex_);
    return history_.size();
}

std::optional<PromptEnhancement> OllamaClient::get_history_entry(const std::string& id) const {
    std::lock_guard<std::mutex> lock(history_mutex_);

    for (const auto& entry : history_) {
        if (entry.id == id) {
            return entry;
        }
    }

    return std::nullopt;
}

bool OllamaClient::delete_history_entry(const std::string& id) {
    std::lock_guard<std::mutex> lock(history_mutex_);

    auto it = std::find_if(history_.begin(), history_.end(),
        [&id](const PromptEnhancement& entry) { return entry.id == id; });

    if (it != history_.end()) {
        history_.erase(it);
        save_history();
        return true;
    }

    return false;
}

void OllamaClient::clear_history() {
    std::lock_guard<std::mutex> lock(history_mutex_);
    history_.clear();
    save_history();
    std::cout << "[OllamaClient] History cleared" << std::endl;
}

bool OllamaClient::test_connection() {
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
    client.set_connection_timeout(5);  // Short timeout for connection test
    client.set_read_timeout(5);

    auto res = client.Get("/api/tags");
    return res && res->status == 200;
}

std::vector<std::string> OllamaClient::get_available_models() {
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
            std::cerr << "[OllamaClient] Failed to parse models response: " << e.what() << std::endl;
        }
    }

    return models;
}

nlohmann::json OllamaClient::get_status() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    bool connected = const_cast<OllamaClient*>(this)->test_connection();
    auto models = const_cast<OllamaClient*>(this)->get_available_models();

    return {
        {"enabled", config_.enabled},
        {"connected", connected},
        {"endpoint", config_.endpoint},
        {"model", config_.model},
        {"available_models", models}
        // Note: API key is intentionally not included for security
    };
}

nlohmann::json OllamaClient::get_settings() const {
    std::lock_guard<std::mutex> lock(config_mutex_);

    // Return effective system prompt (use default if empty)
    std::string effective_system_prompt = config_.system_prompt.empty()
        ? DEFAULT_OLLAMA_SYSTEM_PROMPT
        : config_.system_prompt;

    return {
        {"enabled", config_.enabled},
        {"endpoint", config_.endpoint},
        {"model", config_.model},
        {"temperature", config_.temperature},
        {"max_tokens", config_.max_tokens},
        {"timeout_seconds", config_.timeout_seconds},
        {"max_history", config_.max_history},
        {"system_prompt", effective_system_prompt},
        {"has_api_key", !config_.api_key.empty()}  // Don't expose the actual key
    };
}

bool OllamaClient::update_settings(const nlohmann::json& settings) {
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
        if (settings.contains("max_history") && settings["max_history"].is_number()) {
            config_.max_history = settings["max_history"].get<int>();
        }
        if (settings.contains("system_prompt") && settings["system_prompt"].is_string()) {
            config_.system_prompt = settings["system_prompt"].get<std::string>();
        }
        if (settings.contains("api_key") && settings["api_key"].is_string()) {
            config_.api_key = settings["api_key"].get<std::string>();
        }

        std::cout << "[OllamaClient] Settings updated - model: " << config_.model
                  << ", endpoint: " << config_.endpoint << std::endl;

        // Persist to config file
        if (!config_file_path_.empty()) {
            if (!save_to_config()) {
                std::cerr << "[OllamaClient] Warning: Failed to persist settings to config file" << std::endl;
            }
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "[OllamaClient] Failed to update settings: " << e.what() << std::endl;
        return false;
    }
}

bool OllamaClient::save_to_config() {
    // Note: This is called with config_mutex_ already held
    if (config_file_path_.empty()) {
        return false;
    }

    try {
        // Read the existing config file
        std::ifstream in_file(config_file_path_);
        if (!in_file.is_open()) {
            std::cerr << "[OllamaClient] Failed to open config file for reading: " << config_file_path_ << std::endl;
            return false;
        }

        nlohmann::json config_json;
        in_file >> config_json;
        in_file.close();

        // Update only the ollama section
        config_json["ollama"] = {
            {"enabled", config_.enabled},
            {"endpoint", config_.endpoint},
            {"api_key", config_.api_key},
            {"model", config_.model},
            {"temperature", config_.temperature},
            {"max_tokens", config_.max_tokens},
            {"system_prompt", config_.system_prompt},
            {"timeout_seconds", config_.timeout_seconds},
            {"max_history", config_.max_history}
        };

        // Write back to file
        std::ofstream out_file(config_file_path_);
        if (!out_file.is_open()) {
            std::cerr << "[OllamaClient] Failed to open config file for writing: " << config_file_path_ << std::endl;
            return false;
        }

        out_file << config_json.dump(2);
        out_file.close();

        std::cout << "[OllamaClient] Settings persisted to " << config_file_path_ << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[OllamaClient] Failed to save config: " << e.what() << std::endl;
        return false;
    }
}

void OllamaClient::add_to_history(const PromptEnhancement& entry) {
    std::lock_guard<std::mutex> lock(history_mutex_);

    // Insert at beginning (newest first)
    history_.insert(history_.begin(), entry);
    prune_history();
    save_history();
}

void OllamaClient::prune_history() {
    // Keep only max_history entries
    if (history_.size() > static_cast<size_t>(config_.max_history)) {
        history_.resize(config_.max_history);
    }
}

void OllamaClient::save_history() {
    try {
        nlohmann::json state;
        nlohmann::json items = nlohmann::json::array();

        for (const auto& entry : history_) {
            items.push_back(entry.to_json());
        }

        state["items"] = items;
        state["version"] = 1;

        std::ofstream file(history_file_);
        if (file.is_open()) {
            file << state.dump(2);
        }
    } catch (const std::exception& e) {
        std::cerr << "[OllamaClient] Failed to save history: " << e.what() << std::endl;
    }
}

void OllamaClient::load_history() {
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
                history_.push_back(PromptEnhancement::from_json(item));
            }
        }

        std::cout << "[OllamaClient] Loaded " << history_.size() << " history entries" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[OllamaClient] Failed to load history: " << e.what() << std::endl;
        history_.clear();
    }
}

} // namespace sdcpp
