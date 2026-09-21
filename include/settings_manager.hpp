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
    // Show inline "recommended" hints below each option in ModelLoad +
    // Generate forms. Tooltips don't work on mobile, so the inline form
    // is the primary surface; power users who already know each option
    // can hide them. Default on (more discoverable for new users).
    bool show_option_hints = true;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(UIPreferences, desktop_notifications, theme, theme_custom, show_option_hints)
};

/**
 * Integration secrets - runtime-configurable API tokens for the
 * third-party services the download manager talks to. Kept in the
 * settings file (600-permissioned) rather than the systemd env-file so
 * they can be changed from the WebUI without a restart. Both are
 * optional - public HF repos and public CivitAI models work without a
 * token; tokens improve rate limits and unlock gated / early-access
 * content. Env vars (HF_TOKEN / CIVITAI_API_KEY) remain a fallback when
 * the settings-managed value is empty.
 */
struct IntegrationSettings {
    std::string hf_token;         // HuggingFace access token (Bearer)
    std::string civitai_api_key;  // CivitAI Authorization Bearer

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(IntegrationSettings, hf_token, civitai_api_key)
};

/**
 * Settings structure - stores user-saved preferences
 */
struct Settings {
    GenerationPreferences generation;
    UIPreferences ui;
    IntegrationSettings integrations;

    // NOTE: not using NLOHMANN_DEFINE_TYPE_INTRUSIVE here because
    // integrations is a new field and existing settings.json files won't
    // have it - the intrusive macro would throw on load. We define to_json
    // / from_json manually so a missing key is tolerated.
    friend void to_json(nlohmann::json& j, const Settings& s);
    friend void from_json(const nlohmann::json& j, Settings& s);
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
     * Get integration secrets (HF / CivitAI tokens).
     */
    IntegrationSettings get_integrations() const;

    /**
     * Update integration secrets. Empty string clears a token.
     */
    void set_integrations(const IntegrationSettings& integrations);
    
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