#include "settings_manager.hpp"
#include "utils.hpp"
#include "download_manager.hpp"

#include <fstream>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace sdcpp {

// Manual Settings JSON serialisers (see header for why we don't use the
// intrusive macro): tolerate a missing `integrations` block so upgrades
// from an older user_settings.json don't wipe the file.
void to_json(nlohmann::json& j, const Settings& s) {
    j = nlohmann::json{
        {"generation", s.generation},
        {"ui", s.ui},
        {"integrations", s.integrations}
    };
}

void from_json(const nlohmann::json& j, Settings& s) {
    if (j.contains("generation")) s.generation = j.at("generation").get<GenerationPreferences>();
    if (j.contains("ui")) s.ui = j.at("ui").get<UIPreferences>();
    if (j.contains("integrations")) s.integrations = j.at("integrations").get<IntegrationSettings>();
}

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

IntegrationSettings SettingsManager::get_integrations() const {
    std::lock_guard<std::mutex> lock(settings_mutex_);
    return settings_.integrations;
}

void SettingsManager::set_integrations(const IntegrationSettings& integrations) {
    {
        std::lock_guard<std::mutex> lock(settings_mutex_);
        settings_.integrations = integrations;
        save_settings();
    }
    // Push into DownloadManager immediately so the next queued download
    // uses the new tokens - no server restart required.
    DownloadManager::set_hf_token(integrations.hf_token);
    DownloadManager::set_civitai_api_key(integrations.civitai_api_key);
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
            if (j.contains("integrations")) {
                settings_.integrations = j["integrations"].get<IntegrationSettings>();
            }
            // Push loaded tokens into DownloadManager so downloads picked
            // up during startup already see them.
            DownloadManager::set_hf_token(settings_.integrations.hf_token);
            DownloadManager::set_civitai_api_key(settings_.integrations.civitai_api_key);
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
        j["integrations"] = settings_.integrations;

        // Write to file with 0600 so the secrets stay owner-only.
        std::ofstream file(settings_file_);
        file << j.dump(2) << std::endl;
        file.close();
        std::error_code ec;
        fs::permissions(settings_file_,
                        fs::perms::owner_read | fs::perms::owner_write,
                        fs::perm_options::replace, ec);
    } catch (const std::exception& e) {
        std::cerr << "[SettingsManager] Failed to save settings: " << e.what() << std::endl;
    }
}

} // namespace sdcpp