#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace sdcpp {

/**
 * Server configuration
 */
struct ServerConfig {
    std::string host = "0.0.0.0";
    int port = 8080;
    int ws_port = 0;  // DEPRECATED: WebSocket now uses same port as HTTP. Ignored if set.
    int threads = 32;
    // Floor for forwarding stable-diffusion.cpp's own log messages to the
    // server log. One of: "debug", "info", "warn", "error", "off". Default
    // "warn" matches the previous release-mode behavior.
    std::string sd_log_level = "warn";

    // Peers (IPs or IPv4 CIDRs) we trust to inject X-Forwarded-Proto /
    // X-Forwarded-Host. Used when constructing absolute URLs returned in
    // responses (e.g. job output_urls). Empty list = never trust the
    // forwarded headers — the literal Host header is used instead. Set
    // this to the address(es) of your reverse proxy (e.g. ["127.0.0.1",
    // "10.0.0.0/8"]) when the server runs behind nginx/Caddy/Traefik.
    std::vector<std::string> trusted_proxies;
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
    // Server-wide defaults for sd.cpp load params. Currently only n_threads +
    // flash_attn are honored — per-component CPU placement (formerly
    // keep_clip_on_cpu / keep_vae_on_cpu) and the global offload_to_cpu flag
    // were removed upstream; users now route placement through the per-load
    // `backend` / `params_backend` strings (e.g. "diffusion=cuda0,vae=cpu").
    int n_threads = -1;             // -1 = auto-detect
    bool flash_attn = true;
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
 * Recycle bin configuration for queue items
 * Allows soft-deletion with automatic cleanup after retention period
 */
struct RecycleBinConfig {
    bool enabled = true;                    // Enable/disable recycle bin (if disabled, items are permanently deleted)
    int retention_minutes = 10080;          // Time to keep deleted items (default: 7 days = 7*24*60)
};

/**
 * LLM Assistant configuration
 * Provides an AI assistant that can help with settings, prompt enhancement, and more
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
 * Authentication configuration
 *
 * Single credential pair for the whole server. If `enabled` is true and
 * neither config nor environment variables provide credentials, the server
 * refuses to start.
 *
 * Source priority (per field): config value → environment variable
 *   - username: config.auth.username, else SDCPP_AUTH_USERNAME
 *   - password: config.auth.password, else SDCPP_AUTH_PASSWORD
 */
struct AuthConfig {
    bool enabled = true;             // Enforce auth on all non-public endpoints
    std::string username;            // Configured username (empty = use env var)
    std::string password;            // Configured password (empty = use env var)
    int token_ttl_minutes = 1440;    // Bearer token lifetime (default 24h)

    // When true (default), the static `/output/*` and `/thumb/*` handlers
    // bypass the bearer/cookie middleware so generated images can be
    // embedded in third-party clients via direct URL (Discord, Slack, an
    // <img src="..."> in a separate site, etc.). When false, those paths
    // require the same auth as everything else. Has no effect when
    // `enabled=false` — there's no auth to bypass.
    bool allow_public_outputs = true;
};

/**
 * Complete application configuration
 */
struct Config {
    ServerConfig server;
    PathsConfig paths;
    SDDefaultsConfig sd_defaults;
    PreviewConfig preview;
    AssistantConfig assistant;
    RecycleBinConfig recycle_bin;
    AuthConfig auth;

    // When true, jobs created via expand_prompt write outputs into
    // <output>/<group_id>/<job_id>/ instead of flat <output>/<job_id>/. Lets
    // users locate all variations of a single template-expansion together.
    // Toggleable from the WebUI Settings page.
    bool output_group_folders = true;
    
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

void to_json(nlohmann::json& j, const PreviewConfig& c);
void from_json(const nlohmann::json& j, PreviewConfig& c);

void to_json(nlohmann::json& j, const AssistantConfig& c);
void from_json(const nlohmann::json& j, AssistantConfig& c);

void to_json(nlohmann::json& j, const RecycleBinConfig& c);
void from_json(const nlohmann::json& j, RecycleBinConfig& c);

void to_json(nlohmann::json& j, const AuthConfig& c);
void from_json(const nlohmann::json& j, AuthConfig& c);

void to_json(nlohmann::json& j, const Config& c);
void from_json(const nlohmann::json& j, Config& c);

} // namespace sdcpp
