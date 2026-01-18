#include "settings_manager.hpp"
#include "utils.hpp"

#include <fstream>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace sdcpp {

SettingsManager::SettingsManager(const std::string& config_file_path, const std::string& settings_dir)
    : config_file_path_(config_file_path), settings_dir_(settings_dir) {
    // Construct settings file path
    settings_file_ = fs::path(settings_dir) / "user_settings.json";
}

bool SettingsManager::initialize() {
    try {
        // Create settings directory if it doesn't exist
        if (!fs::exists(settings_dir_)) {
            fs::create_directories(settings_dir_);
        }

        // Load settings from file or create defaults (empty preferences)
        load_settings();
        
        initialized_ = true;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[SettingsManager] Failed to initialize: " << e.what() << std::endl;
        return false;
    }
}

Settings SettingsManager::get_settings() const {
    std::lock_guard<std::mutex> lock(settings_mutex_);
    return settings_;
}

void SettingsManager::set_settings(const Settings& settings) {
    std::lock_guard<std::mutex> lock(settings_mutex_);
    settings_ = settings;
    save_settings();
}

nlohmann::json SettingsManager::get_generation_preferences(const std::string& mode) const {
    std::lock_guard<std::mutex> lock(settings_mutex_);
    
    if (mode == "txt2img") {
        return settings_.generation.txt2img;
    } else if (mode == "img2img") {
        return settings_.generation.img2img;
    } else if (mode == "txt2vid") {
        return settings_.generation.txt2vid;
    }
    
    return nlohmann::json{};
}

void SettingsManager::set_generation_preferences(const std::string& mode, const nlohmann::json& preferences) {
    std::lock_guard<std::mutex> lock(settings_mutex_);
    
    if (mode == "txt2img") {
        settings_.generation.txt2img = preferences;
    } else if (mode == "img2img") {
        settings_.generation.img2img = preferences;
    } else if (mode == "txt2vid") {
        settings_.generation.txt2vid = preferences;
    } else {
        return; // Invalid mode
    }
    
    save_settings();
}

UIPreferences SettingsManager::get_ui_preferences() const {
    std::lock_guard<std::mutex> lock(settings_mutex_);
    return settings_.ui;
}

void SettingsManager::set_ui_preferences(const UIPreferences& preferences) {
    std::lock_guard<std::mutex> lock(settings_mutex_);
    settings_.ui = preferences;
    save_settings();
}

void SettingsManager::reset_settings() {
    std::lock_guard<std::mutex> lock(settings_mutex_);
    // Reset to empty preferences (user will rely on architecture defaults)
    settings_ = Settings{};
    settings_.ui.desktop_notifications = true;
    settings_.ui.theme = "default";
    settings_.ui.theme_custom = nlohmann::json();

    save_settings();
}

void SettingsManager::load_settings() {
    try {
        // Try to load from file
        if (fs::exists(settings_file_)) {
            std::ifstream file(settings_file_);
            nlohmann::json j;
            file >> j;
            
            // Parse settings
            if (j.contains("generation")) {
                settings_.generation = j["generation"].get<GenerationPreferences>();
            }
            if (j.contains("ui")) {
                settings_.ui = j["ui"].get<UIPreferences>();
            }
        } else {
            // Use empty preferences (user will rely on architecture defaults)
            settings_ = Settings{};
            settings_.ui.desktop_notifications = true;
            settings_.ui.theme = "default";
        }
    } catch (const std::exception& e) {
        std::cerr << "[SettingsManager] Failed to load settings, using empty: " << e.what() << std::endl;
        settings_ = Settings{};
        settings_.ui.desktop_notifications = true;
        settings_.ui.theme = "default";
    }
}

void SettingsManager::save_settings() {
    try {
        // Ensure directory exists
        if (!fs::exists(settings_dir_)) {
            fs::create_directories(settings_dir_);
        }

        // Serialize to JSON
        nlohmann::json j;
        j["generation"] = settings_.generation;
        j["ui"] = settings_.ui;

        // Write to file
        std::ofstream file(settings_file_);
        file << j.dump(2) << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[SettingsManager] Failed to save settings: " << e.what() << std::endl;
    }
}

} // namespace sdcpp