#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <optional>
#include <chrono>

#include <nlohmann/json.hpp>

#include "config.hpp"

namespace sdcpp {

/**
 * Default system prompt for prompt enhancement
 */
constexpr const char* DEFAULT_OLLAMA_SYSTEM_PROMPT =
    "You are an expert at enhancing image generation prompts for Stable Diffusion models. "
    "When given a prompt, improve it by adding artistic details, lighting descriptions, "
    "style references, quality tags, and technical parameters that will help generate "
    "better images. Keep the original intent but make it more descriptive and effective. "
    "Output ONLY the enhanced prompt without explanations or commentary.";

/**
 * Prompt enhancement history entry
 */
struct PromptEnhancement {
    std::string id;                  // UUID
    std::string original_prompt;
    std::string enhanced_prompt;
    std::string model_used;
    int64_t created_at;              // Unix timestamp
    bool success = true;
    std::string error_message;

    nlohmann::json to_json() const;
    static PromptEnhancement from_json(const nlohmann::json& j);
};

/**
 * Ollama API client for prompt enhancement
 * Thread-safe, with persistent history storage
 */
class OllamaClient {
public:
    /**
     * Constructor
     * @param config Ollama configuration
     * @param data_dir Directory for storing history file
     * @param config_file_path Path to config.json for persisting settings (optional)
     */
    explicit OllamaClient(const OllamaConfig& config, const std::string& data_dir,
                          const std::string& config_file_path = "");

    ~OllamaClient();

    // Non-copyable
    OllamaClient(const OllamaClient&) = delete;
    OllamaClient& operator=(const OllamaClient&) = delete;

    /**
     * Enhance a prompt using Ollama API
     * @param prompt Original prompt to enhance
     * @param custom_system_prompt Optional custom system prompt override
     * @return Pair of (success, enhanced_prompt or error message)
     */
    std::pair<bool, std::string> enhance_prompt(
        const std::string& prompt,
        const std::optional<std::string>& custom_system_prompt = std::nullopt
    );

    /**
     * Get enhancement history
     * @param limit Maximum entries to return (0 = all)
     * @param offset Offset for pagination
     * @return Vector of history entries (newest first)
     */
    std::vector<PromptEnhancement> get_history(size_t limit = 50, size_t offset = 0) const;

    /**
     * Get total history count
     */
    size_t get_history_count() const;

    /**
     * Get a specific history entry by ID
     */
    std::optional<PromptEnhancement> get_history_entry(const std::string& id) const;

    /**
     * Delete a specific history entry
     * @return true if entry was found and deleted
     */
    bool delete_history_entry(const std::string& id);

    /**
     * Clear all history
     */
    void clear_history();

    /**
     * Test connection to Ollama server
     * @return true if connection successful
     */
    bool test_connection();

    /**
     * Get available models from Ollama server
     * @return List of model names or empty on failure
     */
    std::vector<std::string> get_available_models();

    /**
     * Check if Ollama is enabled in config
     */
    bool is_enabled() const { return config_.enabled; }

    /**
     * Get current configuration (without API key for security)
     */
    nlohmann::json get_status() const;

    /**
     * Get current settings (for settings UI)
     * Returns config as JSON (API key masked)
     */
    nlohmann::json get_settings() const;

    /**
     * Update settings at runtime
     * @param settings JSON with settings to update
     * @return true if successful
     */
    bool update_settings(const nlohmann::json& settings);

private:
    void load_history();
    void save_history();
    void add_to_history(const PromptEnhancement& entry);
    void prune_history();  // Keep only max_history entries

    /**
     * Save current settings to config file
     * @return true if saved successfully
     */
    bool save_to_config();

    /**
     * Generate a UUID for history entries
     */
    static std::string generate_uuid();

    /**
     * Parse URL into host, port, and path components
     */
    static bool parse_url(const std::string& url, std::string& host, int& port, std::string& path, bool& is_ssl);

    OllamaConfig config_;
    std::string history_file_;
    std::string config_file_path_;  // Path to config.json for persistence

    mutable std::mutex config_mutex_;
    mutable std::mutex history_mutex_;
    std::vector<PromptEnhancement> history_;
};

} // namespace sdcpp
