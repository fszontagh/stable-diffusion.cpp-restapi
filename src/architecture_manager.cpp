#include "architecture_manager.hpp"
#include <fstream>
#include <iostream>
#include <algorithm>

namespace sdcpp {

nlohmann::json ArchitecturePreset::to_json() const {
    return {
        {"id", id},
        {"name", name},
        {"description", description},
        {"aliases", aliases},
        {"requiredComponents", requiredComponents},
        {"optionalComponents", optionalComponents},
        {"loadOptions", loadOptions},
        {"generationDefaults", generationDefaults}
    };
}

ArchitectureManager::ArchitectureManager(const std::string& data_dir) {
    config_path_ = data_dir + "/model_architectures.json";
    load_from_file();
    start_file_watcher();
}

ArchitectureManager::~ArchitectureManager() {
    stop_file_watcher();
}

void ArchitectureManager::load_from_file() {
    std::lock_guard<std::mutex> lock(mutex_);

    presets_.clear();
    alias_map_.clear();

    if (!std::filesystem::exists(config_path_)) {
        std::cerr << "[ArchitectureManager] Config file not found: " << config_path_ << std::endl;
        return;
    }

    try {
        std::ifstream file(config_path_);
        if (!file.is_open()) {
            std::cerr << "[ArchitectureManager] Failed to open config file" << std::endl;
            return;
        }

        nlohmann::json config = nlohmann::json::parse(file);

        if (!config.contains("architectures") || !config["architectures"].is_object()) {
            std::cerr << "[ArchitectureManager] Invalid config: missing 'architectures' object" << std::endl;
            return;
        }

        for (auto& [key, value] : config["architectures"].items()) {
            ArchitecturePreset preset;
            preset.id = key;
            preset.name = value.value("name", key);
            preset.description = value.value("description", "");

            // Parse aliases
            if (value.contains("aliases") && value["aliases"].is_array()) {
                for (const auto& alias : value["aliases"]) {
                    if (alias.is_string()) {
                        preset.aliases.push_back(alias.get<std::string>());
                    }
                }
            }

            // Parse required components
            if (value.contains("requiredComponents") && value["requiredComponents"].is_object()) {
                for (auto& [comp, desc] : value["requiredComponents"].items()) {
                    preset.requiredComponents[comp] = desc.is_string() ? desc.get<std::string>() : "";
                }
            }

            // Parse optional components
            if (value.contains("optionalComponents") && value["optionalComponents"].is_object()) {
                for (auto& [comp, desc] : value["optionalComponents"].items()) {
                    preset.optionalComponents[comp] = desc.is_string() ? desc.get<std::string>() : "";
                }
            }

            // Load options and generation defaults as-is
            preset.loadOptions = value.value("loadOptions", nlohmann::json::object());
            preset.generationDefaults = value.value("generationDefaults", nlohmann::json::object());

            presets_[key] = preset;

            // Build alias map (lowercase for case-insensitive lookup)
            std::string key_lower = key;
            std::transform(key_lower.begin(), key_lower.end(), key_lower.begin(), ::tolower);
            alias_map_[key_lower] = key;

            for (const auto& alias : preset.aliases) {
                std::string alias_lower = alias;
                std::transform(alias_lower.begin(), alias_lower.end(), alias_lower.begin(), ::tolower);
                alias_map_[alias_lower] = key;
            }
        }

        last_mtime_ = std::filesystem::last_write_time(config_path_);
        std::cout << "[ArchitectureManager] Loaded " << presets_.size() << " architecture presets" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "[ArchitectureManager] Failed to parse config: " << e.what() << std::endl;
    }
}

void ArchitectureManager::start_file_watcher() {
    watching_ = true;
    watcher_thread_ = std::thread(&ArchitectureManager::file_watcher_loop, this);
}

void ArchitectureManager::stop_file_watcher() {
    watching_ = false;
    if (watcher_thread_.joinable()) {
        watcher_thread_.join();
    }
}

void ArchitectureManager::file_watcher_loop() {
    while (watching_) {
        std::this_thread::sleep_for(std::chrono::seconds(2));

        if (!watching_) break;

        try {
            if (std::filesystem::exists(config_path_)) {
                auto current_mtime = std::filesystem::last_write_time(config_path_);
                if (current_mtime != last_mtime_) {
                    std::cout << "[ArchitectureManager] Config file changed, reloading..." << std::endl;
                    load_from_file();
                }
            }
        } catch (const std::exception& e) {
            // Ignore errors during file watching
        }
    }
}

std::vector<ArchitecturePreset> ArchitectureManager::get_all() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ArchitecturePreset> result;
    result.reserve(presets_.size());
    for (const auto& [id, preset] : presets_) {
        result.push_back(preset);
    }
    return result;
}

const ArchitecturePreset* ArchitectureManager::get(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);

    // First try direct lookup
    auto it = presets_.find(name);
    if (it != presets_.end()) {
        return &it->second;
    }

    // Try case-insensitive alias lookup
    std::string name_lower = name;
    std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);

    auto alias_it = alias_map_.find(name_lower);
    if (alias_it != alias_map_.end()) {
        auto preset_it = presets_.find(alias_it->second);
        if (preset_it != presets_.end()) {
            return &preset_it->second;
        }
    }

    // Try partial match
    for (const auto& [id, preset] : presets_) {
        std::string id_lower = id;
        std::transform(id_lower.begin(), id_lower.end(), id_lower.begin(), ::tolower);
        if (name_lower.find(id_lower) != std::string::npos ||
            id_lower.find(name_lower) != std::string::npos) {
            return &preset;
        }
    }

    return nullptr;
}

nlohmann::json ArchitectureManager::to_json() const {
    std::lock_guard<std::mutex> lock(mutex_);
    nlohmann::json result = nlohmann::json::object();
    for (const auto& [id, preset] : presets_) {
        result[id] = preset.to_json();
    }
    return result;
}

bool ArchitectureManager::reload() {
    try {
        load_from_file();
        return true;
    } catch (...) {
        return false;
    }
}

std::filesystem::file_time_type ArchitectureManager::get_file_mtime() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_mtime_;
}

} // namespace sdcpp
