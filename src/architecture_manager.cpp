#include <cctype>
#include "architecture_manager.hpp"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <vector>

namespace sdcpp {

nlohmann::json ArchitecturePreset::to_json() const {
    nlohmann::json j = {
        {"id", id},
        {"name", name},
        {"description", description},
        {"aliases", aliases},
        {"requiredComponents", requiredComponents},
        {"optionalComponents", optionalComponents},
        {"loadOptions", loadOptions},
        {"generationDefaults", generationDefaults}
    };
    if (!imageEditMode.empty()) {
        j["imageEditMode"] = imageEditMode;
    }
    // Pass-through WebUI matching/scoring rules. We don't validate the shape
    // here — that's the WebUI's concern; we just forward whatever's in the
    // JSON so a JSON-only architecture addition needs no WebUI rebuild.
    if (!match.is_null() && !match.empty()) {
        j["match"] = match;
    }
    if (!componentScoring.is_null() && !componentScoring.empty()) {
        j["componentScoring"] = componentScoring;
    }
    if (!downloads.is_null() && !downloads.empty()) {
        j["downloads"] = downloads;
    }
    return j;
}

ArchitectureManager::ArchitectureManager(const std::string& data_dir) {
    // Try multiple locations for model_architectures.json
    std::vector<std::string> search_paths;

    // Primary: provided data directory (same as config.json)
    search_paths.push_back(data_dir + "/model_architectures.json");

    // Fallback: data/ subdirectory of current working directory
    search_paths.push_back("data/model_architectures.json");

    // Fallback: data/ subdirectory relative to executable
    // Get executable path via /proc/self/exe on Linux
    std::error_code ec;
    auto exe_path = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        auto exe_dir = exe_path.parent_path();
        search_paths.push_back((exe_dir / "data" / "model_architectures.json").string());
        search_paths.push_back((exe_dir.parent_path() / "data" / "model_architectures.json").string());
    }

    // Find the first existing path
    for (const auto& path : search_paths) {
        if (std::filesystem::exists(path)) {
            config_path_ = std::filesystem::canonical(path).string();
            std::cout << "[ArchitectureManager] Found config at: " << config_path_ << std::endl;
            break;
        }
    }

    // If none found, use the primary path (will show error in load_from_file)
    if (config_path_.empty()) {
        config_path_ = search_paths[0];
        std::cerr << "[ArchitectureManager] Could not find model_architectures.json in any of these locations:" << std::endl;
        for (const auto& path : search_paths) {
            std::cerr << "  - " << path << std::endl;
        }
    }

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

            // Load options, image edit mode, and generation defaults
            preset.loadOptions = value.value("loadOptions", nlohmann::json::object());
            preset.imageEditMode = value.value("imageEditMode", std::string(""));
            preset.generationDefaults = value.value("generationDefaults", nlohmann::json::object());
            // Optional WebUI-driven matching + scoring rules. Forwarded as-is.
            preset.match = value.value("match", nlohmann::json::object());
            preset.componentScoring = value.value("componentScoring", nlohmann::json::object());
            // Optional download catalog. Kept opaque here - the WebUI validates
            // per-entry shape when it renders. An empty array is treated the
            // same as an absent field.
            preset.downloads = value.value("downloads", nlohmann::json::array());

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

namespace {

// Lowercase and drop every non-alphanumeric character, so that punctuation
// differences between a filename and a preset alias stop mattering:
//   "flux-2-klein-9b-Q4_0.gguf" -> "flux2klein9bq40gguf"
//   "Flux.2-klein-9B"           -> "flux2klein9b"       (a substring of it)
std::string normalize_for_match(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (unsigned char c : in) {
        if (std::isalnum(c)) {
            out.push_back(static_cast<char>(std::tolower(c)));
        }
    }
    return out;
}

// Split on runs of non-alphanumerics, lowercased.
//   "wan2.2-ti2v-5b-Q4_0.gguf" -> {wan2, 2, ti2v, 5b, q4, 0, gguf}
std::vector<std::string> tokenize_for_match(const std::string& in) {
    std::vector<std::string> out;
    std::string cur;
    for (unsigned char c : in) {
        if (std::isalnum(c)) {
            cur.push_back(static_cast<char>(std::tolower(c)));
        } else if (!cur.empty()) {
            out.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// Short needles (<4 chars: SD1, SD2, SD3, LCM, Wan, PiD) are matched only at a
// token boundary, never as a free substring. Plain substring matching at that
// length produces nonsense - "pid" sits inside "rapid-mix-v2", which would
// otherwise resolve to the PiD architecture. A trailing digit is allowed to be
// part of the token so "wan" still matches "wan2.2-ti2v-5b".
bool short_token_hit(const std::string& needle, const std::vector<std::string>& tokens) {
    for (const auto& t : tokens) {
        if (t == needle) return true;
        if (t.size() > needle.size() &&
            t.compare(0, needle.size(), needle) == 0 &&
            std::isdigit(static_cast<unsigned char>(t[needle.size()]))) {
            return true;
        }
    }
    return false;
}

} // namespace

std::optional<ArchitecturePreset> ArchitectureManager::get(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const ArchitecturePreset* preset = find_unlocked(name);
    if (!preset) {
        return std::nullopt;
    }
    return *preset;
}

const ArchitecturePreset* ArchitectureManager::find_unlocked(const std::string& name) const {
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

    // Partial match, preferring the longest (most specific) hit.
    //
    // Two things this has to get right, because the common caller is
    // /architectures/detect passing a raw filename:
    //
    //  1. Aliases must participate. Matching preset ids alone meant
    //     "flux-2-klein-9b-Q4_0.gguf" degraded to plain "Flux" - the only id
    //     whose text appears anywhere in that filename - and the user got the
    //     Flux preset's 20 steps instead of Klein's 4.
    //  2. Separators must be ignored. Releases punctuate the same model every
    //     which way ("Flux.2-klein-9B", "flux2_klein_9b", "flux-2-klein-9b"),
    //     and none of it is meaningful. Compare with [^a-z0-9] stripped.
    //  3. Short needles need a token boundary. See short_token_hit().
    const std::string name_norm = normalize_for_match(name);
    if (name_norm.empty()) {
        return nullptr;
    }
    const std::vector<std::string> name_tokens = tokenize_for_match(name);

    const ArchitecturePreset* best_match = nullptr;
    size_t best_match_len = 0;

    auto consider = [&](const std::string& candidate, const ArchitecturePreset& preset) {
        const std::string cand_norm = normalize_for_match(candidate);
        if (cand_norm.empty() || cand_norm.size() <= best_match_len) {
            return;
        }
        const bool hit = cand_norm.size() < 4
            ? short_token_hit(cand_norm, name_tokens)
            : (name_norm.find(cand_norm) != std::string::npos ||
               cand_norm.find(name_norm) != std::string::npos);
        if (hit) {
            best_match = &preset;
            best_match_len = cand_norm.size();
        }
    };

    for (const auto& [id, preset] : presets_) {
        consider(id, preset);
        for (const auto& alias : preset.aliases) {
            consider(alias, preset);
        }
    }

    return best_match;
}

int ArchitectureManager::get_default_steps(const std::string& arch_name, int fallback) const {
    const auto preset = get(arch_name);
    if (preset && preset->generationDefaults.contains("steps")) {
        return preset->generationDefaults["steps"].get<int>();
    }
    return fallback;
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
