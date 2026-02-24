#pragma once

#include <string>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <filesystem>
#include <chrono>
#include <nlohmann/json.hpp>

namespace sdcpp {

/**
 * Model architecture preset loaded from JSON config
 */
struct ArchitecturePreset {
    std::string id;                                      // Key in JSON (e.g., "Flux")
    std::string name;                                    // Display name
    std::string description;                             // Description text
    std::vector<std::string> aliases;                    // Alternative names
    std::map<std::string, std::string> requiredComponents;   // component -> description
    std::map<std::string, std::string> optionalComponents;   // component -> description
    nlohmann::json loadOptions;                          // Options for loading
    nlohmann::json generationDefaults;                   // Default generation params

    nlohmann::json to_json() const;
};

/**
 * ArchitectureManager - Loads and manages model architecture presets from JSON
 *
 * Features:
 * - Loads from data/model_architectures.json
 * - Auto-reloads on file changes
 * - Thread-safe access
 */
class ArchitectureManager {
public:
    /**
     * Constructor
     * @param data_dir Path to data directory containing model_architectures.json
     */
    explicit ArchitectureManager(const std::string& data_dir);
    ~ArchitectureManager();

    // Non-copyable
    ArchitectureManager(const ArchitectureManager&) = delete;
    ArchitectureManager& operator=(const ArchitectureManager&) = delete;

    /**
     * Get all architecture presets
     */
    std::vector<ArchitecturePreset> get_all() const;

    /**
     * Get architecture preset by ID or alias
     * @param name Architecture ID or alias (case-insensitive)
     * @return Preset or nullptr if not found
     */
    const ArchitecturePreset* get(const std::string& name) const;

    /**
     * Get default steps for an architecture
     * @param arch_name Architecture ID or alias
     * @param fallback Default value if architecture not found
     * @return Default steps for the architecture
     */
    int get_default_steps(const std::string& arch_name, int fallback = 20) const;

    /**
     * Get all presets as JSON for API response
     */
    nlohmann::json to_json() const;

    /**
     * Reload presets from file
     * @return true if successful
     */
    bool reload();

    /**
     * Get last modified time of config file
     */
    std::filesystem::file_time_type get_file_mtime() const;

private:
    void load_from_file();
    void start_file_watcher();
    void stop_file_watcher();
    void file_watcher_loop();

    std::string config_path_;
    mutable std::mutex mutex_;
    std::map<std::string, ArchitecturePreset> presets_;
    std::map<std::string, std::string> alias_map_;  // alias -> id

    std::atomic<bool> watching_{false};
    std::thread watcher_thread_;
    std::filesystem::file_time_type last_mtime_;
};

} // namespace sdcpp
