#include "config.hpp"

#include <fstream>
#include <stdexcept>
#include <filesystem>

namespace fs = std::filesystem;

namespace sdcpp {

// ServerConfig JSON serialization
void to_json(nlohmann::json& j, const ServerConfig& c) {
    j = nlohmann::json{
        {"host", c.host},
        {"port", c.port},
        {"ws_port", c.ws_port},
        {"threads", c.threads}
    };
}

void from_json(const nlohmann::json& j, ServerConfig& c) {
    c.host = j.value("host", "0.0.0.0");
    c.port = j.value("port", 8080);
    c.ws_port = j.value("ws_port", 8081);
    c.threads = j.value("threads", 8);
}

// PathsConfig JSON serialization
void to_json(nlohmann::json& j, const PathsConfig& c) {
    j = nlohmann::json{
        {"checkpoints", c.checkpoints},
        {"diffusion_models", c.diffusion_models},
        {"vae", c.vae},
        {"lora", c.lora},
        {"clip", c.clip},
        {"t5", c.t5},
        {"embeddings", c.embeddings},
        {"controlnet", c.controlnet},
        {"llm", c.llm},
        {"esrgan", c.esrgan},
        {"taesd", c.taesd},
        {"output", c.output},
        {"webui", c.webui}
    };
}

void from_json(const nlohmann::json& j, PathsConfig& c) {
    c.checkpoints = j.value("checkpoints", "");
    c.diffusion_models = j.value("diffusion_models", "");
    c.vae = j.value("vae", "");
    c.lora = j.value("lora", "");
    c.clip = j.value("clip", "");
    c.t5 = j.value("t5", "");
    c.embeddings = j.value("embeddings", "");
    c.controlnet = j.value("controlnet", "");
    c.llm = j.value("llm", "");
    c.esrgan = j.value("esrgan", "");
    c.taesd = j.value("taesd", "");
    c.output = j.value("output", "");
    c.webui = j.value("webui", "");
}

// SDDefaultsConfig JSON serialization
void to_json(nlohmann::json& j, const SDDefaultsConfig& c) {
    j = nlohmann::json{
        {"n_threads", c.n_threads},
        {"keep_clip_on_cpu", c.keep_clip_on_cpu},
        {"keep_vae_on_cpu", c.keep_vae_on_cpu},
        {"flash_attn", c.flash_attn},
        {"offload_to_cpu", c.offload_to_cpu}
    };
}

void from_json(const nlohmann::json& j, SDDefaultsConfig& c) {
    c.n_threads = j.value("n_threads", -1);
    c.keep_clip_on_cpu = j.value("keep_clip_on_cpu", true);
    c.keep_vae_on_cpu = j.value("keep_vae_on_cpu", false);
    c.flash_attn = j.value("flash_attn", true);
    c.offload_to_cpu = j.value("offload_to_cpu", false);
}

// PreviewConfig JSON serialization
void to_json(nlohmann::json& j, const PreviewConfig& c) {
    j = nlohmann::json{
        {"enabled", c.enabled},
        {"mode", c.mode},
        {"interval", c.interval},
        {"max_size", c.max_size},
        {"quality", c.quality}
    };
}

void from_json(const nlohmann::json& j, PreviewConfig& c) {
    c.enabled = j.value("enabled", true);
    c.mode = j.value("mode", "tae");
    c.interval = j.value("interval", 1);
    c.max_size = j.value("max_size", 256);
    c.quality = j.value("quality", 75);
}

// AssistantConfig JSON serialization
void to_json(nlohmann::json& j, const AssistantConfig& c) {
    j = nlohmann::json{
        {"enabled", c.enabled},
        {"endpoint", c.endpoint},
        {"api_key", c.api_key},
        {"model", c.model},
        {"temperature", c.temperature},
        {"max_tokens", c.max_tokens},
        {"timeout_seconds", c.timeout_seconds},
        {"system_prompt", c.system_prompt},
        {"max_history_turns", c.max_history_turns},
        {"proactive_suggestions", c.proactive_suggestions}
    };
}

void from_json(const nlohmann::json& j, AssistantConfig& c) {
    c.enabled = j.value("enabled", false);
    c.endpoint = j.value("endpoint", "http://localhost:11434");
    c.api_key = j.value("api_key", "");
    c.model = j.value("model", "llama3.2");
    c.temperature = j.value("temperature", 0.7f);
    c.max_tokens = j.value("max_tokens", 2000);
    c.timeout_seconds = j.value("timeout_seconds", 120);
    c.system_prompt = j.value("system_prompt", "");
    c.max_history_turns = j.value("max_history_turns", 20);
    c.proactive_suggestions = j.value("proactive_suggestions", true);
}

// RecycleBinConfig JSON serialization
void to_json(nlohmann::json& j, const RecycleBinConfig& c) {
    j = nlohmann::json{
        {"enabled", c.enabled},
        {"retention_minutes", c.retention_minutes}
    };
}

void from_json(const nlohmann::json& j, RecycleBinConfig& c) {
    c.enabled = j.value("enabled", true);
    c.retention_minutes = j.value("retention_minutes", 10080);
}

// Config JSON serialization
void to_json(nlohmann::json& j, const Config& c) {
    j = nlohmann::json{
        {"server", c.server},
        {"paths", c.paths},
        {"sd_defaults", c.sd_defaults},
        {"preview", c.preview},
        {"assistant", c.assistant},
        {"recycle_bin", c.recycle_bin}
    };
}

void from_json(const nlohmann::json& j, Config& c) {
    if (j.contains("server")) {
        c.server = j["server"].get<ServerConfig>();
    }
    if (j.contains("paths")) {
        c.paths = j["paths"].get<PathsConfig>();
    }
    if (j.contains("sd_defaults")) {
        c.sd_defaults = j["sd_defaults"].get<SDDefaultsConfig>();
    }
    if (j.contains("preview")) {
        c.preview = j["preview"].get<PreviewConfig>();
    }
    if (j.contains("assistant")) {
        c.assistant = j["assistant"].get<AssistantConfig>();
    }
    if (j.contains("recycle_bin")) {
        c.recycle_bin = j["recycle_bin"].get<RecycleBinConfig>();
    }
}

Config Config::load(const std::string& path) {
    if (!fs::exists(path)) {
        throw std::runtime_error("Configuration file not found: " + path);
    }
    
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open configuration file: " + path);
    }
    
    nlohmann::json j;
    try {
        file >> j;
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error("Failed to parse configuration file: " + std::string(e.what()));
    }
    
    Config config;
    from_json(j, config);
    
    return config;
}

void Config::validate() const {
    // Validate required paths
    auto validate_path = [](const std::string& path, const std::string& name) {
        if (path.empty()) {
            throw std::runtime_error("Required path not configured: " + name);
        }
        if (!fs::exists(path)) {
            throw std::runtime_error("Path does not exist: " + name + " = " + path);
        }
        if (!fs::is_directory(path)) {
            throw std::runtime_error("Path is not a directory: " + name + " = " + path);
        }
    };
    
    validate_path(paths.checkpoints, "paths.checkpoints");
    validate_path(paths.diffusion_models, "paths.diffusion_models");
    validate_path(paths.vae, "paths.vae");
    validate_path(paths.lora, "paths.lora");
    validate_path(paths.clip, "paths.clip");
    validate_path(paths.t5, "paths.t5");
    
    // Output directory - create if doesn't exist
    if (paths.output.empty()) {
        throw std::runtime_error("Required path not configured: paths.output");
    }
    if (!fs::exists(paths.output)) {
        if (!fs::create_directories(paths.output)) {
            throw std::runtime_error("Failed to create output directory: " + paths.output);
        }
    }
    
    // Validate server config
    if (server.port < 1 || server.port > 65535) {
        throw std::runtime_error("Invalid server port: " + std::to_string(server.port));
    }
    if (server.ws_port < 0 || server.ws_port > 65535) {
        throw std::runtime_error("Invalid WebSocket port: " + std::to_string(server.ws_port));
    }
    if (server.ws_port > 0 && server.ws_port == server.port) {
        throw std::runtime_error("WebSocket port must be different from HTTP port");
    }
    if (server.threads < 1) {
        throw std::runtime_error("Server threads must be at least 1");
    }
}

nlohmann::json Config::to_json() const {
    nlohmann::json j;
    sdcpp::to_json(j, *this);
    return j;
}

} // namespace sdcpp
