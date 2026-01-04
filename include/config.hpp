#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace sdcpp {

/**
 * Server configuration
 */
struct ServerConfig {
    std::string host = "0.0.0.0";
    int port = 8080;
    int ws_port = 8081;  // WebSocket port, 0 to disable
    int threads = 8;
};

/**
 * Model directory paths configuration
 */
struct PathsConfig {
    std::string checkpoints;        // SD1.x, SD2.x, SDXL models
    std::string diffusion_models;   // Flux, SD3, Qwen, Wan, Z-Image
    std::string vae;                // VAE models
    std::string lora;               // LoRA models
    std::string clip;               // CLIP models
    std::string t5;                 // T5 text encoder models
    std::string embeddings;         // Textual inversion embeddings
    std::string controlnet;         // ControlNet models
    std::string llm;                // LLM models for multimodal (Qwen, etc.)
    std::string esrgan;             // ESRGAN upscaler models
    std::string taesd;              // TAESD tiny autoencoder models for preview
    std::string output;             // Generated output directory
    std::string webui;              // Web UI directory (optional)
};

/**
 * Stable diffusion default settings
 */
struct SDDefaultsConfig {
    int n_threads = -1;             // -1 = auto-detect
    bool keep_clip_on_cpu = true;
    bool keep_vae_on_cpu = false;
    bool flash_attn = true;
    bool offload_to_cpu = false;
};

/**
 * Ollama integration configuration for prompt enhancement
 */
struct OllamaConfig {
    bool enabled = false;                                    // Enable/disable Ollama integration
    std::string endpoint = "http://localhost:11434";         // Ollama API endpoint
    std::string api_key = "";                                // Optional API key for cloud endpoints
    std::string model = "llama3.2";                          // Model name to use
    float temperature = 0.7f;                                // Generation temperature
    int max_tokens = 500;                                    // Maximum tokens for response
    std::string system_prompt = "";                          // Custom system prompt (empty = use default)
    int timeout_seconds = 60;                                // Request timeout
    int max_history = 100;                                   // Maximum history entries to keep
};

/**
 * Preview configuration for live generation previews
 */
struct PreviewConfig {
    bool enabled = true;                 // Enable/disable preview generation
    std::string mode = "tae";            // Preview mode: "none", "proj", "tae", "vae"
    int interval = 1;                    // Generate preview every N steps
    int max_size = 256;                  // Maximum preview dimension in pixels
    int quality = 75;                    // JPEG quality 1-100
};

/**
 * LLM Assistant configuration (separate from Ollama prompt enhancement)
 * Provides a Clippy-like assistant that can observe user interactions and help with settings
 */
struct AssistantConfig {
    bool enabled = false;                                    // Enable/disable assistant
    std::string endpoint = "http://localhost:11434";         // LLM API endpoint (Ollama compatible)
    std::string api_key = "";                                // Optional API key for cloud endpoints
    std::string model = "llama3.2";                          // Model name to use
    float temperature = 0.7f;                                // Generation temperature (0.0-2.0)
    int max_tokens = 2000;                                   // Maximum tokens for response
    int timeout_seconds = 120;                               // Request timeout
    std::string system_prompt = "";                          // Custom system prompt (empty = use default)
    int max_history_turns = 20;                              // Maximum conversation turns to keep
    bool proactive_suggestions = true;                       // Enable proactive suggestions
};

/**
 * Complete application configuration
 */
struct Config {
    ServerConfig server;
    PathsConfig paths;
    SDDefaultsConfig sd_defaults;
    OllamaConfig ollama;
    PreviewConfig preview;
    AssistantConfig assistant;
    
    /**
     * Load configuration from JSON file
     * @param path Path to config.json
     * @return Config object
     * @throws std::runtime_error if file doesn't exist or is invalid
     */
    static Config load(const std::string& path);
    
    /**
     * Validate configuration
     * @throws std::runtime_error if configuration is invalid
     */
    void validate() const;
    
    /**
     * Convert to JSON
     */
    nlohmann::json to_json() const;
};

// JSON serialization
void to_json(nlohmann::json& j, const ServerConfig& c);
void from_json(const nlohmann::json& j, ServerConfig& c);

void to_json(nlohmann::json& j, const PathsConfig& c);
void from_json(const nlohmann::json& j, PathsConfig& c);

void to_json(nlohmann::json& j, const SDDefaultsConfig& c);
void from_json(const nlohmann::json& j, SDDefaultsConfig& c);

void to_json(nlohmann::json& j, const OllamaConfig& c);
void from_json(const nlohmann::json& j, OllamaConfig& c);

void to_json(nlohmann::json& j, const PreviewConfig& c);
void from_json(const nlohmann::json& j, PreviewConfig& c);

void to_json(nlohmann::json& j, const AssistantConfig& c);
void from_json(const nlohmann::json& j, AssistantConfig& c);

void to_json(nlohmann::json& j, const Config& c);
void from_json(const nlohmann::json& j, Config& c);

} // namespace sdcpp
