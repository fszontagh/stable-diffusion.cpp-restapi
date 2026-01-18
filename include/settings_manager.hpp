#pragma once

#include <string>
#include <mutex>
#include <optional>

#include <nlohmann/json.hpp>

namespace sdcpp {

/**
 * Generation preferences - user-saved overrides for generation parameters
 * These are applied ON TOP of architecture defaults
 */
struct GenerationPreferences {
    // txt2img preferences (user overrides)
    nlohmann::json txt2img;
    // img2img preferences (user overrides)
    nlohmann::json img2img;
    // txt2vid preferences (user overrides)
    nlohmann::json txt2vid;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(GenerationPreferences, txt2img, img2img, txt2vid)
};

/**
 * UI preferences
 */
struct UIPreferences {
    bool desktop_notifications = true;
    std::string theme = "default";
    nlohmann::json theme_custom = nlohmann::json();

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(UIPreferences, desktop_notifications, theme, theme_custom)
};

/**
 * Settings structure - stores user-saved preferences
 */
struct Settings {
    GenerationPreferences generation;
    UIPreferences ui;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Settings, generation, ui)
};

/**
 * Combined defaults result
 * Contains both architecture defaults and user preferences
 */
struct CombinedDefaults {
    nlohmann::json architecture_defaults;  // From model_architectures.json (immutable)
    nlohmann::json user_preferences;       // From user settings (overrides)
    nlohmann::json effective;              // Combined result (user overrides take precedence)
};

/**
 * Settings Manager - manages user-saved preferences with persistence
 * Thread-safe, persists to JSON file
 * 
 * User preferences are applied ON TOP of architecture defaults
 */
class SettingsManager {
public:
    /**
     * Constructor
     * @param config_file_path Path to main config file
     * @param settings_dir Directory for settings files
     */
    SettingsManager(const std::string& config_file_path, const std::string& settings_dir);
    
    ~SettingsManager() = default;
    
    // Non-copyable, non-movable
    SettingsManager(const SettingsManager&) = delete;
    SettingsManager& operator=(const SettingsManager&) = delete;
    SettingsManager(SettingsManager&&) = delete;
    SettingsManager& operator=(SettingsManager&&) = delete;
    
    /**
     * Initialize settings manager, load from disk
     * @return true if successful
     */
    bool initialize();
    
    /**
     * Get all user settings
     */
    Settings get_settings() const;
    
    /**
     * Update all user settings
     */
    void set_settings(const Settings& settings);
    
    /**
     * Get generation preferences for a mode
     */
    nlohmann::json get_generation_preferences(const std::string& mode) const;
    
    /**
     * Set generation preferences for a mode
     */
    void set_generation_preferences(const std::string& mode, const nlohmann::json& preferences);
    
    /**
     * Get UI preferences
     */
    UIPreferences get_ui_preferences() const;
    
    /**
     * Set UI preferences
     */
    void set_ui_preferences(const UIPreferences& preferences);
    
    /**
     * Reset all user settings to empty (no overrides)
     */
    void reset_settings();

private:
    void load_settings();
    void save_settings();
    
    std::string config_file_path_;
    std::string settings_dir_;
    std::string settings_file_;
    
    mutable std::mutex settings_mutex_;
    Settings settings_;
    
    bool initialized_ = false;
};

} // namespace sdcpp