#include "sd_wrapper.hpp"
#include "sd_error_capture.hpp"
#include "utils.hpp"

#include <iostream>
#include <iomanip>
#include <filesystem>
#include <cstring>
#include <stdexcept>
#include <regex>
#include <map>
#include <unistd.h>

#include "stable-diffusion.h"
#include "stb_image.h"
#include "stb_image_write.h"

namespace fs = std::filesystem;

namespace sdcpp {

// ============================================================================
// Type-safe JSON parsing helpers with clear error messages
// These handle the case where values might come as strings instead of numbers
// ============================================================================

namespace {

// Parse integer from JSON, accepting both number and string representations
int parse_int(const nlohmann::json& j, const std::string& key, int default_value) {
    if (!j.contains(key) || j[key].is_null()) {
        return default_value;
    }

    const auto& val = j[key];

    if (val.is_number_integer()) {
        return val.get<int>();
    }

    if (val.is_number_float()) {
        return static_cast<int>(val.get<double>());
    }

    if (val.is_string()) {
        const std::string& str = val.get<std::string>();
        if (str.empty()) {
            return default_value;
        }
        try {
            size_t pos;
            int result = std::stoi(str, &pos);
            if (pos != str.length()) {
                throw std::runtime_error("Parameter '" + key + "' must be an integer, got string with non-numeric characters: \"" + str + "\"");
            }
            return result;
        } catch (const std::invalid_argument&) {
            throw std::runtime_error("Parameter '" + key + "' must be an integer, got invalid string: \"" + str + "\"");
        } catch (const std::out_of_range&) {
            throw std::runtime_error("Parameter '" + key + "' value out of range: \"" + str + "\"");
        }
    }

    throw std::runtime_error("Parameter '" + key + "' must be an integer, got " + std::string(val.type_name()));
}

// Parse int64 from JSON, accepting both number and string representations
int64_t parse_int64(const nlohmann::json& j, const std::string& key, int64_t default_value) {
    if (!j.contains(key) || j[key].is_null()) {
        return default_value;
    }

    const auto& val = j[key];

    if (val.is_number_integer()) {
        return val.get<int64_t>();
    }

    if (val.is_number_float()) {
        return static_cast<int64_t>(val.get<double>());
    }

    if (val.is_string()) {
        const std::string& str = val.get<std::string>();
        if (str.empty()) {
            return default_value;
        }
        try {
            size_t pos;
            int64_t result = std::stoll(str, &pos);
            if (pos != str.length()) {
                throw std::runtime_error("Parameter '" + key + "' must be an integer, got string with non-numeric characters: \"" + str + "\"");
            }
            return result;
        } catch (const std::invalid_argument&) {
            throw std::runtime_error("Parameter '" + key + "' must be an integer, got invalid string: \"" + str + "\"");
        } catch (const std::out_of_range&) {
            throw std::runtime_error("Parameter '" + key + "' value out of range: \"" + str + "\"");
        }
    }

    throw std::runtime_error("Parameter '" + key + "' must be an integer, got " + std::string(val.type_name()));
}

// Parse float from JSON, accepting both number and string representations
float parse_float(const nlohmann::json& j, const std::string& key, float default_value) {
    if (!j.contains(key) || j[key].is_null()) {
        return default_value;
    }

    const auto& val = j[key];

    if (val.is_number()) {
        return val.get<float>();
    }

    if (val.is_string()) {
        const std::string& str = val.get<std::string>();
        if (str.empty()) {
            return default_value;
        }
        try {
            size_t pos;
            float result = std::stof(str, &pos);
            if (pos != str.length()) {
                throw std::runtime_error("Parameter '" + key + "' must be a number, got string with non-numeric characters: \"" + str + "\"");
            }
            return result;
        } catch (const std::invalid_argument&) {
            throw std::runtime_error("Parameter '" + key + "' must be a number, got invalid string: \"" + str + "\"");
        } catch (const std::out_of_range&) {
            throw std::runtime_error("Parameter '" + key + "' value out of range: \"" + str + "\"");
        }
    }

    throw std::runtime_error("Parameter '" + key + "' must be a number, got " + std::string(val.type_name()));
}

// Parse bool from JSON, accepting bool, number, and string representations
bool parse_bool(const nlohmann::json& j, const std::string& key, bool default_value) {
    if (!j.contains(key) || j[key].is_null()) {
        return default_value;
    }

    const auto& val = j[key];

    if (val.is_boolean()) {
        return val.get<bool>();
    }

    if (val.is_number()) {
        return val.get<int>() != 0;
    }

    if (val.is_string()) {
        std::string str = val.get<std::string>();
        // Convert to lowercase for comparison
        for (char& c : str) {
            c = std::tolower(static_cast<unsigned char>(c));
        }
        if (str == "true" || str == "1" || str == "yes" || str == "on") {
            return true;
        }
        if (str == "false" || str == "0" || str == "no" || str == "off" || str.empty()) {
            return false;
        }
        throw std::runtime_error("Parameter '" + key + "' must be a boolean, got invalid string: \"" + val.get<std::string>() + "\"");
    }

    throw std::runtime_error("Parameter '" + key + "' must be a boolean, got " + std::string(val.type_name()));
}

// Parse string from JSON
std::string parse_string(const nlohmann::json& j, const std::string& key, const std::string& default_value) {
    if (!j.contains(key) || j[key].is_null()) {
        return default_value;
    }

    const auto& val = j[key];

    if (val.is_string()) {
        return val.get<std::string>();
    }

    // Convert numbers to string
    if (val.is_number_integer()) {
        return std::to_string(val.get<int64_t>());
    }
    if (val.is_number_float()) {
        return std::to_string(val.get<double>());
    }
    if (val.is_boolean()) {
        return val.get<bool>() ? "true" : "false";
    }

    throw std::runtime_error("Parameter '" + key + "' must be a string, got " + std::string(val.type_name()));
}

/**
 * Build an error message that includes any captured SD errors.
 * This allows CUDA OOM and other sd.cpp errors to be propagated to job failures.
 */
std::string build_error_message(const std::string& base_message) {
    std::string sd_error = get_sd_error();
    if (sd_error.empty()) {
        return base_message;
    }
    return base_message + ": " + sd_error;
}

} // anonymous namespace

ProgressCallback SDWrapper::progress_callback_ = nullptr;
int SDWrapper::expected_diffusion_steps_ = 0;

void SDWrapper::set_progress_callback(ProgressCallback callback, int expected_steps) {
    progress_callback_ = callback;
    expected_diffusion_steps_ = expected_steps;
    sd_set_progress_callback(internal_progress_callback, nullptr);
}

void SDWrapper::clear_progress_callback() {
    progress_callback_ = nullptr;
    expected_diffusion_steps_ = 0;
    sd_set_progress_callback(nullptr, nullptr);
}

// Preview callback support
PreviewCallback SDWrapper::preview_callback_ = nullptr;
int SDWrapper::preview_max_size_ = 256;
int SDWrapper::preview_quality_ = 75;

void SDWrapper::set_preview_callback(PreviewCallback callback, PreviewMode mode, int interval, int max_size, int quality) {
    preview_callback_ = callback;
    preview_max_size_ = max_size;
    preview_quality_ = quality;

    if (callback && mode != PreviewMode::None) {
        // Map our PreviewMode to sd.cpp's preview_t enum
        preview_t sd_mode;
        switch (mode) {
            case PreviewMode::Proj: sd_mode = PREVIEW_PROJ; break;
            case PreviewMode::Tae:  sd_mode = PREVIEW_TAE; break;
            case PreviewMode::Vae:  sd_mode = PREVIEW_VAE; break;
            default:                sd_mode = PREVIEW_NONE; break;
        }
        // Set preview callback: mode, interval, denoised=true, noisy=false
        sd_set_preview_callback(internal_preview_callback, sd_mode, interval, true, false, nullptr);
    } else {
        sd_set_preview_callback(nullptr, PREVIEW_NONE, 0, false, false, nullptr);
    }
}

void SDWrapper::clear_preview_callback() {
    preview_callback_ = nullptr;
    sd_set_preview_callback(nullptr, PREVIEW_NONE, 0, false, false, nullptr);
}

void SDWrapper::internal_preview_callback(int step, int frame_count, sd_image_t* frames, bool is_noisy, void* /*data*/) {
    if (!preview_callback_ || !frames || frame_count == 0) {
        return;
    }

    // Use the first frame for preview (for images frame_count=1, for video it's current frames)
    const sd_image_t& frame = frames[0];

    if (!frame.data || frame.width == 0 || frame.height == 0) {
        return;
    }

    int width = static_cast<int>(frame.width);
    int height = static_cast<int>(frame.height);
    int channels = static_cast<int>(frame.channel);

    // Skip server-side resize - client will resize as needed (CSS/canvas)
    // This eliminates O(n*m) CPU work per preview

    // Encode as JPEG directly from source frame
    std::vector<uint8_t> jpeg_data = encode_jpeg_memory(frame.data, width, height, channels, preview_quality_);

    if (!jpeg_data.empty()) {
        preview_callback_(step, frame_count, jpeg_data, width, height, is_noisy);
    }
}

std::vector<uint8_t> SDWrapper::resize_image_bilinear(const uint8_t* data, int src_w, int src_h, int channels, int dst_w, int dst_h) {
    std::vector<uint8_t> result(dst_w * dst_h * channels);

    float x_ratio = static_cast<float>(src_w) / dst_w;
    float y_ratio = static_cast<float>(src_h) / dst_h;

    for (int y = 0; y < dst_h; y++) {
        for (int x = 0; x < dst_w; x++) {
            float src_x = x * x_ratio;
            float src_y = y * y_ratio;

            int x0 = static_cast<int>(src_x);
            int y0 = static_cast<int>(src_y);
            int x1 = std::min(x0 + 1, src_w - 1);
            int y1 = std::min(y0 + 1, src_h - 1);

            float x_diff = src_x - x0;
            float y_diff = src_y - y0;

            for (int c = 0; c < channels; c++) {
                float val00 = data[(y0 * src_w + x0) * channels + c];
                float val01 = data[(y1 * src_w + x0) * channels + c];
                float val10 = data[(y0 * src_w + x1) * channels + c];
                float val11 = data[(y1 * src_w + x1) * channels + c];

                float val = val00 * (1 - x_diff) * (1 - y_diff) +
                           val10 * x_diff * (1 - y_diff) +
                           val01 * (1 - x_diff) * y_diff +
                           val11 * x_diff * y_diff;

                result[(y * dst_w + x) * channels + c] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, val)));
            }
        }
    }

    return result;
}

std::vector<uint8_t> SDWrapper::encode_jpeg_memory(const uint8_t* data, int width, int height, int channels, int quality) {
    std::vector<uint8_t> result;

    auto write_func = [](void* context, void* data, int size) {
        auto* vec = static_cast<std::vector<uint8_t>*>(context);
        auto* bytes = static_cast<uint8_t*>(data);
        vec->insert(vec->end(), bytes, bytes + size);
    };

    stbi_write_jpg_to_func(write_func, &result, width, height, channels, data, quality);
    return result;
}

void SDWrapper::internal_progress_callback(int step, int steps, float time, void* /*data*/) {
    // Determine which phase this callback is from
    // - Diffusion phase: steps matches expected_diffusion_steps_
    // - VAE/other phase: steps differs from expected
    // - Report-all mode: expected_diffusion_steps_ == 0 means report all progress (for upscale, etc.)
    bool is_diffusion_phase = (expected_diffusion_steps_ > 0 && steps == expected_diffusion_steps_);
    bool is_other_phase = (steps > 0 && steps != expected_diffusion_steps_);
    bool report_all_mode = (expected_diffusion_steps_ == 0);

    // Report progress to UI callback:
    // - In diffusion phase (txt2img, img2img, etc.)
    // - In report-all mode (upscale, where we want to see VAE tiling progress)
    if (progress_callback_ && (is_diffusion_phase || report_all_mode)) {
        progress_callback_(step, steps);
    }

    // Console output: only on first step, last step, or every 5th step to reduce I/O overhead
    if (step == 0 || step == steps || (step % 5 == 0)) {
        // Skip non-diffusion operations with very few steps (typically internal operations)
        // This filters out CLIP encoding, embeddings loading, and other quick operations
        if (!is_diffusion_phase && steps <= 3) {
            return;  // Skip logging very short operations
        }

        // Check if stderr is a TTY (terminal) - use \r for in-place updates only in terminals
        // When running as a service (journald), \r appears as binary blob data
        static bool is_tty = isatty(STDERR_FILENO);

        // Determine phase label for logging
        const char* phase_label = is_diffusion_phase ? "Diffusion" :
                                  is_other_phase ? "VAE" : "Processing";

        if (is_tty) {
            // Terminal: use carriage return for in-place update
            fprintf(stderr, "\r[SDWrapper] %s %d/%d (%.1f%%) - %.2fs",
                    phase_label, step, steps, (steps > 0 ? (100.0f * step / steps) : 0.0f), time);
            if (step == steps) {
                fprintf(stderr, "\n");
            }
        } else {
            // Service/journald: use newlines, less verbose output
            if (step == 0) {
                fprintf(stderr, "[SDWrapper] %s starting (%d steps)\n", phase_label, steps);
            } else if (step == steps) {
                fprintf(stderr, "[SDWrapper] %s completed in %.2fs\n", phase_label, time);
            }
            // Skip intermediate progress for non-TTY to avoid log spam
        }
        fflush(stderr);
    }
}

std::pair<std::string, std::vector<SDWrapper::ParsedLora>> SDWrapper::parse_loras_from_prompt(
    const std::string& prompt,
    const std::string& lora_dir
) {
    std::vector<ParsedLora> loras;
    std::string cleaned_prompt = prompt;

    if (lora_dir.empty()) {
        return {prompt, loras};
    }

    // Regex pattern: <lora:name:weight> or <lora:|high_noise|name:weight>
    static const std::regex lora_re(R"(<lora:([^:>]+):([^>]+)>)");
    static const std::vector<std::string> valid_ext = {".pt", ".safetensors", ".gguf"};

    std::smatch match;
    std::string temp = prompt;

    // Map to accumulate multipliers (same LoRA can appear multiple times)
    std::map<std::string, float> lora_map;
    std::map<std::string, float> high_noise_lora_map;

    while (std::regex_search(temp, match, lora_re)) {
        std::string raw_path = match[1].str();
        const std::string raw_mul = match[2].str();

        // Parse multiplier
        float multiplier = 0.0f;
        try {
            multiplier = std::stof(raw_mul);
        } catch (...) {
            // Invalid multiplier, skip this lora tag
            temp = match.suffix().str();
            cleaned_prompt = std::regex_replace(cleaned_prompt, lora_re, "", std::regex_constants::format_first_only);
            continue;
        }

        // Check for high_noise prefix
        bool is_high_noise = false;
        static const std::string prefix = "|high_noise|";
        if (raw_path.rfind(prefix, 0) == 0) {
            raw_path.erase(0, prefix.size());
            is_high_noise = true;
        }

        // Resolve path
        fs::path final_path;
        if (fs::path(raw_path).is_absolute()) {
            final_path = raw_path;
        } else {
            final_path = fs::path(lora_dir) / raw_path;
        }

        // Check if file exists, try adding extensions if not
        if (!fs::exists(final_path)) {
            bool found = false;
            for (const auto& ext : valid_ext) {
                fs::path try_path = final_path;
                try_path += ext;
                if (fs::exists(try_path)) {
                    final_path = try_path;
                    found = true;
                    break;
                }
            }
            if (!found) {
                std::cerr << "[SDWrapper] LoRA not found: " << final_path.lexically_normal().string() << std::endl;
                temp = match.suffix().str();
                cleaned_prompt = std::regex_replace(cleaned_prompt, lora_re, "", std::regex_constants::format_first_only);
                continue;
            }
        }

        // Add to map (accumulate multiplier if same LoRA appears multiple times)
        const std::string key = final_path.lexically_normal().string();
        if (is_high_noise) {
            high_noise_lora_map[key] += multiplier;
        } else {
            lora_map[key] += multiplier;
        }

        // Remove the lora tag from prompt
        cleaned_prompt = std::regex_replace(cleaned_prompt, lora_re, "", std::regex_constants::format_first_only);
        temp = match.suffix().str();
    }

    // Build lora vector from maps
    for (const auto& [path, mul] : lora_map) {
        ParsedLora lora;
        lora.path = path;
        lora.multiplier = mul;
        lora.is_high_noise = false;
        loras.push_back(lora);
        std::cout << "[SDWrapper] LoRA: " << path << " (weight=" << mul << ")" << std::endl;
    }

    for (const auto& [path, mul] : high_noise_lora_map) {
        ParsedLora lora;
        lora.path = path;
        lora.multiplier = mul;
        lora.is_high_noise = true;
        loras.push_back(lora);
        std::cout << "[SDWrapper] LoRA (high_noise): " << path << " (weight=" << mul << ")" << std::endl;
    }

    return {cleaned_prompt, loras};
}

// Helper to parse array of integers
std::vector<int> parse_int_array(const nlohmann::json& j, const std::string& key, const std::vector<int>& default_value) {
    if (!j.contains(key) || j[key].is_null()) {
        return default_value;
    }
    const auto& val = j[key];
    if (!val.is_array()) {
        return default_value;
    }
    std::vector<int> result;
    for (const auto& item : val) {
        if (item.is_number_integer()) {
            result.push_back(item.get<int>());
        } else if (item.is_number_float()) {
            result.push_back(static_cast<int>(item.get<double>()));
        } else if (item.is_string()) {
            try {
                result.push_back(std::stoi(item.get<std::string>()));
            } catch (...) {}
        }
    }
    return result;
}

// Helper to parse array of floats
std::vector<float> parse_float_array(const nlohmann::json& j, const std::string& key) {
    if (!j.contains(key) || j[key].is_null()) {
        return {};
    }
    const auto& val = j[key];
    if (!val.is_array()) {
        return {};
    }
    std::vector<float> result;
    for (const auto& item : val) {
        if (item.is_number()) {
            result.push_back(item.get<float>());
        } else if (item.is_string()) {
            try {
                result.push_back(std::stof(item.get<std::string>()));
            } catch (...) {}
        }
    }
    return result;
}

// Helper to parse array of strings
std::vector<std::string> parse_string_array(const nlohmann::json& j, const std::string& key) {
    if (!j.contains(key) || j[key].is_null()) {
        return {};
    }
    const auto& val = j[key];
    if (!val.is_array()) {
        return {};
    }
    std::vector<std::string> result;
    for (const auto& item : val) {
        if (item.is_string()) {
            result.push_back(item.get<std::string>());
        }
    }
    return result;
}

Txt2ImgParams Txt2ImgParams::from_json(const nlohmann::json& j) {
    Txt2ImgParams p;

    // String parameters
    p.prompt = parse_string(j, "prompt", "");
    p.negative_prompt = parse_string(j, "negative_prompt", "");
    p.sampler = parse_string(j, "sampler", "euler_a");
    p.scheduler = parse_string(j, "scheduler", "discrete");

    // Integer parameters
    p.width = parse_int(j, "width", 512);
    p.height = parse_int(j, "height", 512);
    p.steps = parse_int(j, "steps", 20);
    p.batch_count = parse_int(j, "batch_count", 1);
    p.clip_skip = parse_int(j, "clip_skip", -1);

    // Float parameters
    p.cfg_scale = parse_float(j, "cfg_scale", 7.0f);

    // Seed (int64)
    p.seed = parse_int64(j, "seed", -1);

    // Advanced guidance parameters
    p.distilled_guidance = parse_float(j, "distilled_guidance", 3.5f);
    p.eta = parse_float(j, "eta", 0.0f);
    p.shifted_timestep = parse_int(j, "shifted_timestep", 0);
    p.flow_shift = parse_float(j, "flow_shift", 3.0f);

    // Skip Layer Guidance (SLG)
    p.slg_scale = parse_float(j, "slg_scale", 0.0f);
    p.skip_layers = parse_int_array(j, "skip_layers", {7, 8, 9});
    p.slg_start = parse_float(j, "slg_start", 0.01f);
    p.slg_end = parse_float(j, "slg_end", 0.2f);

    // Custom sigmas
    p.custom_sigmas = parse_float_array(j, "custom_sigmas");

    // Reference images for Flux Kontext
    p.ref_images_base64 = parse_string_array(j, "ref_images");
    p.auto_resize_ref_image = parse_bool(j, "auto_resize_ref_image", true);
    p.increase_ref_index = parse_bool(j, "increase_ref_index", false);

    // ControlNet support
    p.control_strength = parse_float(j, "control_strength", 0.9f);
    if (j.contains("control_image_base64") && !j["control_image_base64"].is_null()) {
        std::string base64 = parse_string(j, "control_image_base64", "");
        if (!base64.empty()) {
            p.control_image_data = SDWrapper::decode_base64_image(
                base64, p.control_image_width, p.control_image_height, p.control_image_channels
            );
        }
    }

    // VAE tiling
    p.vae_tiling = parse_bool(j, "vae_tiling", false);
    p.vae_tile_size_x = parse_int(j, "vae_tile_size_x", 0);
    p.vae_tile_size_y = parse_int(j, "vae_tile_size_y", 0);
    p.vae_tile_overlap = parse_float(j, "vae_tile_overlap", 0.5f);

    // EasyCache support for DiT models
    p.easycache_enabled = parse_bool(j, "easycache", false);
    p.easycache_threshold = parse_float(j, "easycache_threshold", 0.2f);
    p.easycache_start = parse_float(j, "easycache_start", 0.15f);
    p.easycache_end = parse_float(j, "easycache_end", 0.95f);

    // PhotoMaker parameters
    p.pm_id_images_base64 = parse_string_array(j, "pm_id_images");
    p.pm_id_embed_path = parse_string(j, "pm_id_embed_path", "");
    p.pm_style_strength = parse_float(j, "pm_style_strength", 20.0f);

    // Upscale options
    p.upscale = parse_bool(j, "upscale", false);
    p.upscale_auto_unload = parse_bool(j, "upscale_auto_unload", true);
    p.upscale_repeats = parse_int(j, "upscale_repeats", 1);

    return p;
}

nlohmann::json Txt2ImgParams::to_json() const {
    nlohmann::json j = {
        {"prompt", prompt},
        {"negative_prompt", negative_prompt},
        {"width", width},
        {"height", height},
        {"steps", steps},
        {"cfg_scale", cfg_scale},
        {"seed", seed},
        {"sampler", sampler},
        {"scheduler", scheduler},
        {"batch_count", batch_count},
        {"clip_skip", clip_skip},
        {"distilled_guidance", distilled_guidance},
        {"eta", eta},
        {"shifted_timestep", shifted_timestep},
        {"flow_shift", flow_shift}
    };

    // SLG params
    if (slg_scale > 0.0f) {
        j["slg_scale"] = slg_scale;
        j["skip_layers"] = skip_layers;
        j["slg_start"] = slg_start;
        j["slg_end"] = slg_end;
    }

    // Custom sigmas
    if (!custom_sigmas.empty()) {
        j["custom_sigmas"] = custom_sigmas;
    }

    // Reference images
    if (!ref_images_base64.empty()) {
        j["ref_images_count"] = ref_images_base64.size();
        j["auto_resize_ref_image"] = auto_resize_ref_image;
        j["increase_ref_index"] = increase_ref_index;
    }

    // Include control_strength if control image was provided
    if (!control_image_data.empty()) {
        j["control_strength"] = control_strength;
    }

    // VAE tiling
    if (vae_tiling) {
        j["vae_tiling"] = true;
        j["vae_tile_size_x"] = vae_tile_size_x;
        j["vae_tile_size_y"] = vae_tile_size_y;
        j["vae_tile_overlap"] = vae_tile_overlap;
    }

    // Include easycache if enabled
    if (easycache_enabled) {
        j["easycache"] = true;
        j["easycache_threshold"] = easycache_threshold;
        j["easycache_start"] = easycache_start;
        j["easycache_end"] = easycache_end;
    }

    // PhotoMaker
    if (!pm_id_images_base64.empty()) {
        j["pm_id_images_count"] = pm_id_images_base64.size();
        j["pm_style_strength"] = pm_style_strength;
    }
    if (!pm_id_embed_path.empty()) {
        j["pm_id_embed_path"] = pm_id_embed_path;
    }

    // Upscale
    if (upscale) {
        j["upscale"] = true;
        j["upscale_repeats"] = upscale_repeats;
    }

    return j;
}

Img2ImgParams Img2ImgParams::from_json(const nlohmann::json& j) {
    Img2ImgParams p;

    // String parameters
    p.prompt = parse_string(j, "prompt", "");
    p.negative_prompt = parse_string(j, "negative_prompt", "");
    p.sampler = parse_string(j, "sampler", "euler_a");
    p.scheduler = parse_string(j, "scheduler", "discrete");

    // Integer parameters
    p.width = parse_int(j, "width", 512);
    p.height = parse_int(j, "height", 512);
    p.steps = parse_int(j, "steps", 20);
    p.batch_count = parse_int(j, "batch_count", 1);
    p.clip_skip = parse_int(j, "clip_skip", -1);

    // Float parameters
    p.strength = parse_float(j, "strength", 0.75f);
    p.cfg_scale = parse_float(j, "cfg_scale", 7.0f);
    p.img_cfg_scale = parse_float(j, "img_cfg_scale", -1.0f);

    // Seed (int64)
    p.seed = parse_int64(j, "seed", -1);

    // Advanced guidance parameters
    p.distilled_guidance = parse_float(j, "distilled_guidance", 3.5f);
    p.eta = parse_float(j, "eta", 0.0f);
    p.shifted_timestep = parse_int(j, "shifted_timestep", 0);
    p.flow_shift = parse_float(j, "flow_shift", 3.0f);

    // Skip Layer Guidance (SLG)
    p.slg_scale = parse_float(j, "slg_scale", 0.0f);
    p.skip_layers = parse_int_array(j, "skip_layers", {7, 8, 9});
    p.slg_start = parse_float(j, "slg_start", 0.01f);
    p.slg_end = parse_float(j, "slg_end", 0.2f);

    // Custom sigmas
    p.custom_sigmas = parse_float_array(j, "custom_sigmas");

    // Init image (required for img2img)
    if (j.contains("init_image_base64") && !j["init_image_base64"].is_null()) {
        std::string base64 = parse_string(j, "init_image_base64", "");
        if (!base64.empty()) {
            p.init_image_data = SDWrapper::decode_base64_image(
                base64, p.init_image_width, p.init_image_height, p.init_image_channels
            );
        }
    }

    // Inpainting mask (optional)
    if (j.contains("mask_image_base64") && !j["mask_image_base64"].is_null()) {
        std::string base64 = parse_string(j, "mask_image_base64", "");
        if (!base64.empty()) {
            int channels = 1;  // Mask is grayscale
            p.mask_image_data = SDWrapper::decode_base64_image(
                base64, p.mask_image_width, p.mask_image_height, channels
            );
        }
    }

    // Reference images for Flux Kontext
    p.ref_images_base64 = parse_string_array(j, "ref_images");
    p.auto_resize_ref_image = parse_bool(j, "auto_resize_ref_image", true);
    p.increase_ref_index = parse_bool(j, "increase_ref_index", false);

    // ControlNet support
    p.control_strength = parse_float(j, "control_strength", 0.9f);
    if (j.contains("control_image_base64") && !j["control_image_base64"].is_null()) {
        std::string base64 = parse_string(j, "control_image_base64", "");
        if (!base64.empty()) {
            p.control_image_data = SDWrapper::decode_base64_image(
                base64, p.control_image_width, p.control_image_height, p.control_image_channels
            );
        }
    }

    // VAE tiling
    p.vae_tiling = parse_bool(j, "vae_tiling", false);
    p.vae_tile_size_x = parse_int(j, "vae_tile_size_x", 0);
    p.vae_tile_size_y = parse_int(j, "vae_tile_size_y", 0);
    p.vae_tile_overlap = parse_float(j, "vae_tile_overlap", 0.5f);

    // EasyCache support for DiT models
    p.easycache_enabled = parse_bool(j, "easycache", false);
    p.easycache_threshold = parse_float(j, "easycache_threshold", 0.2f);
    p.easycache_start = parse_float(j, "easycache_start", 0.15f);
    p.easycache_end = parse_float(j, "easycache_end", 0.95f);

    // PhotoMaker parameters
    p.pm_id_images_base64 = parse_string_array(j, "pm_id_images");
    p.pm_id_embed_path = parse_string(j, "pm_id_embed_path", "");
    p.pm_style_strength = parse_float(j, "pm_style_strength", 20.0f);

    // Upscale options
    p.upscale = parse_bool(j, "upscale", false);
    p.upscale_auto_unload = parse_bool(j, "upscale_auto_unload", true);
    p.upscale_repeats = parse_int(j, "upscale_repeats", 1);

    return p;
}

nlohmann::json Img2ImgParams::to_json() const {
    nlohmann::json j = {
        {"prompt", prompt},
        {"negative_prompt", negative_prompt},
        {"strength", strength},
        {"width", width},
        {"height", height},
        {"steps", steps},
        {"cfg_scale", cfg_scale},
        {"img_cfg_scale", img_cfg_scale},
        {"seed", seed},
        {"sampler", sampler},
        {"scheduler", scheduler},
        {"batch_count", batch_count},
        {"clip_skip", clip_skip},
        {"distilled_guidance", distilled_guidance},
        {"eta", eta},
        {"shifted_timestep", shifted_timestep},
        {"flow_shift", flow_shift}
    };

    // SLG params
    if (slg_scale > 0.0f) {
        j["slg_scale"] = slg_scale;
        j["skip_layers"] = skip_layers;
        j["slg_start"] = slg_start;
        j["slg_end"] = slg_end;
    }

    // Custom sigmas
    if (!custom_sigmas.empty()) {
        j["custom_sigmas"] = custom_sigmas;
    }

    // Mask
    if (!mask_image_data.empty()) {
        j["has_mask"] = true;
    }

    // Reference images
    if (!ref_images_base64.empty()) {
        j["ref_images_count"] = ref_images_base64.size();
        j["auto_resize_ref_image"] = auto_resize_ref_image;
        j["increase_ref_index"] = increase_ref_index;
    }

    // Include control_strength if control image was provided
    if (!control_image_data.empty()) {
        j["control_strength"] = control_strength;
    }

    // VAE tiling
    if (vae_tiling) {
        j["vae_tiling"] = true;
        j["vae_tile_size_x"] = vae_tile_size_x;
        j["vae_tile_size_y"] = vae_tile_size_y;
        j["vae_tile_overlap"] = vae_tile_overlap;
    }

    // Include easycache if enabled
    if (easycache_enabled) {
        j["easycache"] = true;
        j["easycache_threshold"] = easycache_threshold;
        j["easycache_start"] = easycache_start;
        j["easycache_end"] = easycache_end;
    }

    // PhotoMaker
    if (!pm_id_images_base64.empty()) {
        j["pm_id_images_count"] = pm_id_images_base64.size();
        j["pm_style_strength"] = pm_style_strength;
    }
    if (!pm_id_embed_path.empty()) {
        j["pm_id_embed_path"] = pm_id_embed_path;
    }

    // Upscale
    if (upscale) {
        j["upscale"] = true;
        j["upscale_repeats"] = upscale_repeats;
    }

    return j;
}

Txt2VidParams Txt2VidParams::from_json(const nlohmann::json& j) {
    Txt2VidParams p;

    // String parameters
    p.prompt = parse_string(j, "prompt", "");
    p.negative_prompt = parse_string(j, "negative_prompt", "");
    p.sampler = parse_string(j, "sampler", "euler");
    p.scheduler = parse_string(j, "scheduler", "discrete");

    // Integer parameters
    p.width = parse_int(j, "width", 832);
    p.height = parse_int(j, "height", 480);
    p.video_frames = parse_int(j, "video_frames", 33);
    p.fps = parse_int(j, "fps", 16);
    p.steps = parse_int(j, "steps", 30);
    p.clip_skip = parse_int(j, "clip_skip", -1);

    // Float parameters
    p.cfg_scale = parse_float(j, "cfg_scale", 6.0f);
    p.flow_shift = parse_float(j, "flow_shift", 3.0f);
    p.strength = parse_float(j, "strength", 0.75f);

    // Seed (int64)
    p.seed = parse_int64(j, "seed", -1);

    // Advanced guidance parameters
    p.distilled_guidance = parse_float(j, "distilled_guidance", 3.5f);
    p.eta = parse_float(j, "eta", 0.0f);

    // Skip Layer Guidance (SLG)
    p.slg_scale = parse_float(j, "slg_scale", 0.0f);
    p.skip_layers = parse_int_array(j, "skip_layers", {7, 8, 9});
    p.slg_start = parse_float(j, "slg_start", 0.01f);
    p.slg_end = parse_float(j, "slg_end", 0.2f);

    // Init image (vid2vid)
    if (j.contains("init_image_base64") && !j["init_image_base64"].is_null()) {
        std::string base64 = parse_string(j, "init_image_base64", "");
        if (!base64.empty()) {
            p.init_image_data = SDWrapper::decode_base64_image(
                base64, p.init_image_width, p.init_image_height, p.init_image_channels
            );
        }
    }

    // End image (FLF2V)
    if (j.contains("end_image_base64") && !j["end_image_base64"].is_null()) {
        std::string base64 = parse_string(j, "end_image_base64", "");
        if (!base64.empty()) {
            p.end_image_data = SDWrapper::decode_base64_image(
                base64, p.end_image_width, p.end_image_height, p.end_image_channels
            );
        }
    }

    // ControlNet support (single control image or multiple frames)
    if (j.contains("control_image_base64") && !j["control_image_base64"].is_null()) {
        std::string base64 = parse_string(j, "control_image_base64", "");
        if (!base64.empty()) {
            p.control_image_data = SDWrapper::decode_base64_image(
                base64, p.control_image_width, p.control_image_height, p.control_image_channels
            );
        }
    }

    // Multiple control frames
    p.control_frames_base64 = parse_string_array(j, "control_frames");

    // High-noise phase parameters (MoE models)
    p.high_noise_steps = parse_int(j, "high_noise_steps", -1);
    p.high_noise_cfg_scale = parse_float(j, "high_noise_cfg_scale", 7.0f);
    p.high_noise_sampler = parse_string(j, "high_noise_sampler", "");
    p.high_noise_distilled_guidance = parse_float(j, "high_noise_distilled_guidance", 3.5f);
    p.high_noise_slg_scale = parse_float(j, "high_noise_slg_scale", 0.0f);
    p.high_noise_skip_layers = parse_int_array(j, "high_noise_skip_layers", {7, 8, 9});
    p.high_noise_slg_start = parse_float(j, "high_noise_slg_start", 0.01f);
    p.high_noise_slg_end = parse_float(j, "high_noise_slg_end", 0.2f);
    p.moe_boundary = parse_float(j, "moe_boundary", 0.875f);
    p.vace_strength = parse_float(j, "vace_strength", 1.0f);

    // EasyCache support for DiT models
    p.easycache_enabled = parse_bool(j, "easycache", false);
    p.easycache_threshold = parse_float(j, "easycache_threshold", 0.2f);
    p.easycache_start = parse_float(j, "easycache_start", 0.15f);
    p.easycache_end = parse_float(j, "easycache_end", 0.95f);

    // VAE tiling for large videos
    p.vae_tiling = parse_bool(j, "vae_tiling", false);
    p.vae_tile_size_x = parse_int(j, "vae_tile_size_x", 0);
    p.vae_tile_size_y = parse_int(j, "vae_tile_size_y", 0);
    p.vae_tile_overlap = parse_float(j, "vae_tile_overlap", 0.5f);

    return p;
}

nlohmann::json Txt2VidParams::to_json() const {
    nlohmann::json j = {
        {"prompt", prompt},
        {"negative_prompt", negative_prompt},
        {"width", width},
        {"height", height},
        {"video_frames", video_frames},
        {"fps", fps},
        {"steps", steps},
        {"cfg_scale", cfg_scale},
        {"seed", seed},
        {"sampler", sampler},
        {"scheduler", scheduler},
        {"flow_shift", flow_shift},
        {"clip_skip", clip_skip},
        {"distilled_guidance", distilled_guidance},
        {"eta", eta}
    };

    // SLG params
    if (slg_scale > 0.0f) {
        j["slg_scale"] = slg_scale;
        j["skip_layers"] = skip_layers;
        j["slg_start"] = slg_start;
        j["slg_end"] = slg_end;
    }

    // Init/end images
    if (!init_image_data.empty()) {
        j["has_init_image"] = true;
        j["strength"] = strength;
    }
    if (!end_image_data.empty()) {
        j["has_end_image"] = true;
    }

    // Control frames
    if (!control_image_data.empty()) {
        j["has_control_image"] = true;
    }
    if (!control_frames_base64.empty()) {
        j["control_frames_count"] = control_frames_base64.size();
    }

    // High-noise params (MoE)
    if (high_noise_steps > 0) {
        j["high_noise_steps"] = high_noise_steps;
        j["high_noise_cfg_scale"] = high_noise_cfg_scale;
        j["high_noise_sampler"] = high_noise_sampler;
        j["high_noise_distilled_guidance"] = high_noise_distilled_guidance;
        j["moe_boundary"] = moe_boundary;
        j["vace_strength"] = vace_strength;
    }

    // Include easycache if enabled
    if (easycache_enabled) {
        j["easycache"] = true;
        j["easycache_threshold"] = easycache_threshold;
        j["easycache_start"] = easycache_start;
        j["easycache_end"] = easycache_end;
    }

    // VAE tiling
    if (vae_tiling) {
        j["vae_tiling"] = true;
        j["vae_tile_size_x"] = vae_tile_size_x;
        j["vae_tile_size_y"] = vae_tile_size_y;
        j["vae_tile_overlap"] = vae_tile_overlap;
    }

    return j;
}

UpscaleParams UpscaleParams::from_json(const nlohmann::json& j) {
    UpscaleParams p;

    // Upscale factor
    p.upscale_factor = parse_int(j, "upscale_factor", 4);
    p.tile_size = parse_int(j, "tile_size", 128);
    p.repeats = parse_int(j, "repeats", 1);

    // Image (required)
    if (j.contains("image_base64") && !j["image_base64"].is_null()) {
        std::string base64 = parse_string(j, "image_base64", "");
        if (!base64.empty()) {
            p.image_data = SDWrapper::decode_base64_image(
                base64, p.image_width, p.image_height, p.image_channels
            );
        }
    }

    return p;
}

nlohmann::json UpscaleParams::to_json() const {
    return nlohmann::json{
        {"image_width", image_width},
        {"image_height", image_height},
        {"upscale_factor", upscale_factor},
        {"tile_size", tile_size},
        {"repeats", repeats}
    };
}

std::vector<std::string> SDWrapper::upscale_image(
    upscaler_ctx_t* upscaler_ctx,
    const UpscaleParams& params,
    const std::string& output_dir,
    const std::string& job_id
) {
    std::vector<std::string> outputs;

    if (params.image_data.empty()) {
        throw std::runtime_error("No image provided for upscaling");
    }

    // Create output directory
    std::string job_output_dir = (fs::path(output_dir) / job_id).string();
    utils::create_directory(job_output_dir);

    // Save source image for reference
    std::string source_filename = "source.png";
    std::string source_filepath = (fs::path(job_output_dir) / source_filename).string();
    if (!save_image(source_filepath, params.image_data.data(), params.image_width, params.image_height, params.image_channels)) {
        std::cerr << "[SDWrapper] Failed to save source image to " << source_filepath << std::endl;
    }

    // Create input image structure
    sd_image_t input_image;
    input_image.width = params.image_width;
    input_image.height = params.image_height;
    input_image.channel = params.image_channels;
    input_image.data = const_cast<uint8_t*>(params.image_data.data());

    // Upscale
    sd_image_t upscaled = upscale(upscaler_ctx, input_image, params.upscale_factor);

    if (upscaled.data == nullptr) {
        throw std::runtime_error(build_error_message("Upscaling failed"));
    }

    std::string filename = "upscaled.png";
    std::string filepath = (fs::path(job_output_dir) / filename).string();

    if (!save_image(filepath, upscaled.data, upscaled.width, upscaled.height, upscaled.channel)) {
        std::cerr << "[SDWrapper] Failed to save upscaled image to " << filepath << std::endl;
    }
    outputs.push_back(job_id + "/" + filename);

    free(upscaled.data);

    return outputs;
}

std::vector<uint8_t> SDWrapper::upscale_image_data(
    upscaler_ctx_t* upscaler_ctx,
    const uint8_t* image_data,
    int width,
    int height,
    int channels,
    int& out_width,
    int& out_height
) {
    // Create input image structure
    sd_image_t input_image;
    input_image.width = width;
    input_image.height = height;
    input_image.channel = channels;
    input_image.data = const_cast<uint8_t*>(image_data);

    // Upscale (factor is determined by the model)
    sd_image_t upscaled = upscale(upscaler_ctx, input_image, 4);

    if (!upscaled.data) {
        throw std::runtime_error(build_error_message("Upscaling failed"));
    }

    out_width = upscaled.width;
    out_height = upscaled.height;

    // Copy data to vector
    size_t data_size = out_width * out_height * upscaled.channel;
    std::vector<uint8_t> result(upscaled.data, upscaled.data + data_size);

    free(upscaled.data);

    return result;
}

std::vector<std::string> SDWrapper::generate_txt2img(
    sd_ctx_t* ctx,
    const Txt2ImgParams& params,
    const std::string& lora_dir,
    const std::string& output_dir,
    const std::string& job_id
) {
    std::vector<std::string> outputs;
    sd_image_t* images = nullptr;

    // Create output directory
    std::string job_output_dir = (fs::path(output_dir) / job_id).string();
    utils::create_directory(job_output_dir);

    // Parse LoRAs from prompt
    auto [cleaned_prompt, parsed_loras] = parse_loras_from_prompt(params.prompt, lora_dir);

    // Build sd_lora_t array for sd.cpp
    std::vector<sd_lora_t> sd_loras;
    sd_loras.reserve(parsed_loras.size());
    for (const auto& lora : parsed_loras) {
        sd_lora_t sd_lora;
        sd_lora.is_high_noise = lora.is_high_noise;
        sd_lora.multiplier = lora.multiplier;
        sd_lora.path = lora.path.c_str();
        sd_loras.push_back(sd_lora);
    }

    // Log LoRA status for debugging
    std::cout << "[SDWrapper] txt2img LoRA count: " << sd_loras.size() << std::endl;

    // Initialize generation parameters
    sd_img_gen_params_t gen_params;
    sd_img_gen_params_init(&gen_params);

    // Set LoRAs - explicitly pass count=0 if no LoRAs to ensure sd.cpp clears any cached state
    gen_params.loras = sd_loras.empty() ? nullptr : sd_loras.data();
    gen_params.lora_count = static_cast<uint32_t>(sd_loras.size());

    gen_params.prompt = cleaned_prompt.c_str();
    gen_params.negative_prompt = params.negative_prompt.c_str();
    gen_params.width = params.width;
    gen_params.height = params.height;
    gen_params.seed = params.seed;
    gen_params.batch_count = params.batch_count;
    gen_params.clip_skip = params.clip_skip;

    // Sample parameters
    gen_params.sample_params.sample_steps = params.steps;
    gen_params.sample_params.guidance.txt_cfg = params.cfg_scale;
    gen_params.sample_params.guidance.distilled_guidance = params.distilled_guidance;
    gen_params.sample_params.sample_method = str_to_sample_method(params.sampler.c_str());
    gen_params.sample_params.scheduler = str_to_scheduler(params.scheduler.c_str());
    gen_params.sample_params.eta = params.eta;
    gen_params.sample_params.shifted_timestep = params.shifted_timestep;
    gen_params.sample_params.flow_shift = params.flow_shift;

    // Custom sigmas
    if (!params.custom_sigmas.empty()) {
        gen_params.sample_params.custom_sigmas = const_cast<float*>(params.custom_sigmas.data());
        gen_params.sample_params.custom_sigmas_count = static_cast<int>(params.custom_sigmas.size());
    }

    // Skip Layer Guidance (SLG)
    if (params.slg_scale > 0.0f) {
        gen_params.sample_params.guidance.slg.scale = params.slg_scale;
        gen_params.sample_params.guidance.slg.layers = const_cast<int*>(params.skip_layers.data());
        gen_params.sample_params.guidance.slg.layer_count = static_cast<uint32_t>(params.skip_layers.size());
        gen_params.sample_params.guidance.slg.layer_start = params.slg_start;
        gen_params.sample_params.guidance.slg.layer_end = params.slg_end;
    }

    // No init image for txt2img
    gen_params.init_image.data = nullptr;

    // Reference images for Flux Kontext
    std::vector<sd_image_t> ref_images;
    std::vector<std::vector<uint8_t>> ref_image_buffers;
    if (!params.ref_images_base64.empty()) {
        ref_image_buffers.reserve(params.ref_images_base64.size());
        ref_images.reserve(params.ref_images_base64.size());
        for (const auto& base64 : params.ref_images_base64) {
            int w, h, c;
            auto data = SDWrapper::decode_base64_image(base64, w, h, c);
            ref_image_buffers.push_back(std::move(data));
            sd_image_t img;
            img.width = w;
            img.height = h;
            img.channel = c;
            img.data = ref_image_buffers.back().data();
            ref_images.push_back(img);
        }
        gen_params.ref_images = ref_images.data();
        gen_params.ref_images_count = static_cast<uint32_t>(ref_images.size());
        gen_params.auto_resize_ref_image = params.auto_resize_ref_image;
        gen_params.increase_ref_index = params.increase_ref_index;
    }

    // ControlNet support
    if (!params.control_image_data.empty()) {
        gen_params.control_image.width = params.control_image_width;
        gen_params.control_image.height = params.control_image_height;
        gen_params.control_image.channel = params.control_image_channels;
        gen_params.control_image.data = const_cast<uint8_t*>(params.control_image_data.data());
        gen_params.control_strength = params.control_strength;
    } else {
        gen_params.control_image.data = nullptr;
    }

    // Cache support for DiT models (EasyCache, UCache, etc.)
    if (params.easycache_enabled) {
        gen_params.cache.mode = SD_CACHE_EASYCACHE;
        gen_params.cache.reuse_threshold = params.easycache_threshold;
        gen_params.cache.start_percent = params.easycache_start;
        gen_params.cache.end_percent = params.easycache_end;
    }

    // VAE tiling support
    if (params.vae_tiling) {
        gen_params.vae_tiling_params.enabled = true;
        gen_params.vae_tiling_params.tile_size_x = params.vae_tile_size_x;
        gen_params.vae_tiling_params.tile_size_y = params.vae_tile_size_y;
        gen_params.vae_tiling_params.target_overlap = params.vae_tile_overlap;
    }

    // PhotoMaker support
    std::vector<std::vector<uint8_t>> pm_image_buffers;
    std::vector<sd_image_t> pm_images;
    std::string pm_embed_path_str = params.pm_id_embed_path;
    if (!params.pm_id_images_base64.empty()) {
        pm_image_buffers.reserve(params.pm_id_images_base64.size());
        pm_images.reserve(params.pm_id_images_base64.size());
        for (const auto& base64 : params.pm_id_images_base64) {
            int w, h, c;
            auto data = SDWrapper::decode_base64_image(base64, w, h, c);
            pm_image_buffers.push_back(std::move(data));
            sd_image_t img;
            img.width = w;
            img.height = h;
            img.channel = c;
            img.data = pm_image_buffers.back().data();
            pm_images.push_back(img);
        }
        gen_params.pm_params.id_images = pm_images.data();
        gen_params.pm_params.id_images_count = static_cast<int>(pm_images.size());
        gen_params.pm_params.style_strength = params.pm_style_strength;
        std::cout << "[SDWrapper] PhotoMaker: " << pm_images.size() << " ID images, style_strength=" << params.pm_style_strength << std::endl;
    }
    if (!pm_embed_path_str.empty()) {
        gen_params.pm_params.id_embed_path = pm_embed_path_str.c_str();
    }

    // Generate images
    std::cout << "[SDWrapper] Generating txt2img with prompt: " << cleaned_prompt << std::endl;
    if (!parsed_loras.empty()) {
        std::cout << "[SDWrapper] Using " << parsed_loras.size() << " LoRA(s)" << std::endl;
    }
    std::cout << "[SDWrapper] Size: " << params.width << "x" << params.height << ", steps: " << params.steps << ", batch: " << params.batch_count << std::endl;
    std::cout << "[SDWrapper] Calling generate_image()..." << std::endl;

    images = generate_image(ctx, &gen_params);

    if (images == nullptr) {
        std::cerr << "[SDWrapper] generate_image returned nullptr!" << std::endl;
        throw std::runtime_error(build_error_message("Image generation failed"));
    }

    std::cout << "[SDWrapper] generate_image returned, processing " << params.batch_count << " images" << std::endl;

    for (int i = 0; i < params.batch_count; i++) {
        std::cout << "[SDWrapper] Image " << i << ": "
                  << images[i].width << "x" << images[i].height << "x" << images[i].channel
                  << ", data=" << (images[i].data ? "valid" : "NULL") << std::endl;

        if (images[i].data) {
            std::string filename = "output_" + std::to_string(i) + ".png";
            std::string filepath = (fs::path(job_output_dir) / filename).string();

            if (!save_image(filepath, images[i].data,
                      images[i].width, images[i].height, images[i].channel)) {
                std::cerr << "[SDWrapper] Failed to save image " << i << " to " << filepath << std::endl;
            }

            // Return relative path
            outputs.push_back(job_id + "/" + filename);

            free(images[i].data);
        }
    }
    free(images);

    // Check if any images were successfully generated
    if (outputs.empty()) {
        throw std::runtime_error(build_error_message("Image generation failed - no valid images produced"));
    }

    return outputs;
}

std::vector<std::string> SDWrapper::generate_img2img(
    sd_ctx_t* ctx,
    const Img2ImgParams& params,
    const std::string& lora_dir,
    const std::string& output_dir,
    const std::string& job_id
) {
    std::vector<std::string> outputs;

    if (params.init_image_data.empty()) {
        throw std::runtime_error("No init image provided for img2img");
    }

    // Create output directory
    std::string job_output_dir = (fs::path(output_dir) / job_id).string();
    utils::create_directory(job_output_dir);

    // Save source image for reference
    std::string source_filename = "source.png";
    std::string source_filepath = (fs::path(job_output_dir) / source_filename).string();
    if (!save_image(source_filepath, params.init_image_data.data(), params.init_image_width, params.init_image_height, params.init_image_channels)) {
        std::cerr << "[SDWrapper] Failed to save source image to " << source_filepath << std::endl;
    }

    // Parse LoRAs from prompt
    auto [cleaned_prompt, parsed_loras] = parse_loras_from_prompt(params.prompt, lora_dir);

    // Build sd_lora_t array for sd.cpp
    std::vector<sd_lora_t> sd_loras;
    sd_loras.reserve(parsed_loras.size());
    for (const auto& lora : parsed_loras) {
        sd_lora_t sd_lora;
        sd_lora.is_high_noise = lora.is_high_noise;
        sd_lora.multiplier = lora.multiplier;
        sd_lora.path = lora.path.c_str();
        sd_loras.push_back(sd_lora);
    }

    // Log LoRA status for debugging
    std::cout << "[SDWrapper] img2img LoRA count: " << sd_loras.size() << std::endl;

    // Initialize generation parameters
    sd_img_gen_params_t gen_params;
    sd_img_gen_params_init(&gen_params);

    // Set LoRAs - explicitly pass count=0 if no LoRAs to ensure sd.cpp clears any cached state
    gen_params.loras = sd_loras.empty() ? nullptr : sd_loras.data();
    gen_params.lora_count = static_cast<uint32_t>(sd_loras.size());

    gen_params.prompt = cleaned_prompt.c_str();
    gen_params.negative_prompt = params.negative_prompt.c_str();
    gen_params.width = params.width;
    gen_params.height = params.height;
    gen_params.seed = params.seed;
    gen_params.batch_count = params.batch_count;
    gen_params.clip_skip = params.clip_skip;
    gen_params.strength = params.strength;

    // Sample parameters
    gen_params.sample_params.sample_steps = params.steps;
    gen_params.sample_params.guidance.txt_cfg = params.cfg_scale;
    gen_params.sample_params.guidance.img_cfg = (params.img_cfg_scale < 0) ? params.cfg_scale : params.img_cfg_scale;
    gen_params.sample_params.guidance.distilled_guidance = params.distilled_guidance;
    gen_params.sample_params.sample_method = str_to_sample_method(params.sampler.c_str());
    gen_params.sample_params.scheduler = str_to_scheduler(params.scheduler.c_str());
    gen_params.sample_params.eta = params.eta;
    gen_params.sample_params.shifted_timestep = params.shifted_timestep;
    gen_params.sample_params.flow_shift = params.flow_shift;

    // Custom sigmas
    if (!params.custom_sigmas.empty()) {
        gen_params.sample_params.custom_sigmas = const_cast<float*>(params.custom_sigmas.data());
        gen_params.sample_params.custom_sigmas_count = static_cast<int>(params.custom_sigmas.size());
    }

    // Skip Layer Guidance (SLG)
    if (params.slg_scale > 0.0f) {
        gen_params.sample_params.guidance.slg.scale = params.slg_scale;
        gen_params.sample_params.guidance.slg.layers = const_cast<int*>(params.skip_layers.data());
        gen_params.sample_params.guidance.slg.layer_count = static_cast<uint32_t>(params.skip_layers.size());
        gen_params.sample_params.guidance.slg.layer_start = params.slg_start;
        gen_params.sample_params.guidance.slg.layer_end = params.slg_end;
    }

    // Set init image - resize to match aligned output dimensions
    // sd.cpp aligns width/height to spatial_multiple (vae_scale * diffusion_down_factor)
    // We align to 64 to cover all models (8*8 for worst case)
    auto align_to_multiple = [](int value, int multiple) -> int {
        return ((value + multiple - 1) / multiple) * multiple;
    };

    int aligned_width = align_to_multiple(params.width, 64);
    int aligned_height = align_to_multiple(params.height, 64);

    std::vector<uint8_t> resized_init_image;
    const uint8_t* init_image_ptr = params.init_image_data.data();
    int init_width = params.init_image_width;
    int init_height = params.init_image_height;

    if (params.init_image_width != aligned_width || params.init_image_height != aligned_height) {
        std::cout << "[SDWrapper] Resizing init image from " << params.init_image_width << "x" << params.init_image_height
                  << " to " << aligned_width << "x" << aligned_height << " (aligned from " << params.width << "x" << params.height << ")" << std::endl;
        resized_init_image = SDWrapper::resize_image_bilinear(
            params.init_image_data.data(),
            params.init_image_width, params.init_image_height,
            params.init_image_channels,
            aligned_width, aligned_height
        );
        init_image_ptr = resized_init_image.data();
        init_width = aligned_width;
        init_height = aligned_height;
    }

    gen_params.init_image.width = init_width;
    gen_params.init_image.height = init_height;
    gen_params.init_image.channel = params.init_image_channels;
    gen_params.init_image.data = const_cast<uint8_t*>(init_image_ptr);

    // Set mask image for inpainting - resize if needed to match aligned init image dimensions
    std::vector<uint8_t> resized_mask_image;
    if (!params.mask_image_data.empty()) {
        const uint8_t* mask_ptr = params.mask_image_data.data();
        int mask_width = params.mask_image_width;
        int mask_height = params.mask_image_height;

        if (params.mask_image_width != aligned_width || params.mask_image_height != aligned_height) {
            std::cout << "[SDWrapper] Resizing mask image from " << params.mask_image_width << "x" << params.mask_image_height
                      << " to " << aligned_width << "x" << aligned_height << std::endl;
            resized_mask_image = SDWrapper::resize_image_bilinear(
                params.mask_image_data.data(),
                params.mask_image_width, params.mask_image_height,
                1,  // Grayscale mask
                aligned_width, aligned_height
            );
            mask_ptr = resized_mask_image.data();
            mask_width = aligned_width;
            mask_height = aligned_height;
        }

        gen_params.mask_image.width = mask_width;
        gen_params.mask_image.height = mask_height;
        gen_params.mask_image.channel = 1;  // Grayscale mask
        gen_params.mask_image.data = const_cast<uint8_t*>(mask_ptr);
    } else {
        gen_params.mask_image.data = nullptr;
    }

    // Reference images for Flux Kontext
    std::vector<sd_image_t> ref_images;
    std::vector<std::vector<uint8_t>> ref_image_buffers;
    if (!params.ref_images_base64.empty()) {
        ref_image_buffers.reserve(params.ref_images_base64.size());
        ref_images.reserve(params.ref_images_base64.size());
        for (const auto& base64 : params.ref_images_base64) {
            int w, h, c;
            auto data = SDWrapper::decode_base64_image(base64, w, h, c);
            ref_image_buffers.push_back(std::move(data));
            sd_image_t img;
            img.width = w;
            img.height = h;
            img.channel = c;
            img.data = ref_image_buffers.back().data();
            ref_images.push_back(img);
        }
        gen_params.ref_images = ref_images.data();
        gen_params.ref_images_count = static_cast<uint32_t>(ref_images.size());
        gen_params.auto_resize_ref_image = params.auto_resize_ref_image;
        gen_params.increase_ref_index = params.increase_ref_index;
    }

    // ControlNet support
    if (!params.control_image_data.empty()) {
        gen_params.control_image.width = params.control_image_width;
        gen_params.control_image.height = params.control_image_height;
        gen_params.control_image.channel = params.control_image_channels;
        gen_params.control_image.data = const_cast<uint8_t*>(params.control_image_data.data());
        gen_params.control_strength = params.control_strength;
    } else {
        gen_params.control_image.data = nullptr;
    }

    // Cache support for DiT models (EasyCache, UCache, etc.)
    if (params.easycache_enabled) {
        gen_params.cache.mode = SD_CACHE_EASYCACHE;
        gen_params.cache.reuse_threshold = params.easycache_threshold;
        gen_params.cache.start_percent = params.easycache_start;
        gen_params.cache.end_percent = params.easycache_end;
    }

    // VAE tiling support
    if (params.vae_tiling) {
        gen_params.vae_tiling_params.enabled = true;
        gen_params.vae_tiling_params.tile_size_x = params.vae_tile_size_x;
        gen_params.vae_tiling_params.tile_size_y = params.vae_tile_size_y;
        gen_params.vae_tiling_params.target_overlap = params.vae_tile_overlap;
    }

    // PhotoMaker support
    std::vector<std::vector<uint8_t>> pm_image_buffers;
    std::vector<sd_image_t> pm_images;
    std::string pm_embed_path_str = params.pm_id_embed_path;
    if (!params.pm_id_images_base64.empty()) {
        pm_image_buffers.reserve(params.pm_id_images_base64.size());
        pm_images.reserve(params.pm_id_images_base64.size());
        for (const auto& base64 : params.pm_id_images_base64) {
            int w, h, c;
            auto data = SDWrapper::decode_base64_image(base64, w, h, c);
            pm_image_buffers.push_back(std::move(data));
            sd_image_t img;
            img.width = w;
            img.height = h;
            img.channel = c;
            img.data = pm_image_buffers.back().data();
            pm_images.push_back(img);
        }
        gen_params.pm_params.id_images = pm_images.data();
        gen_params.pm_params.id_images_count = static_cast<int>(pm_images.size());
        gen_params.pm_params.style_strength = params.pm_style_strength;
        std::cout << "[SDWrapper] PhotoMaker: " << pm_images.size() << " ID images, style_strength=" << params.pm_style_strength << std::endl;
    }
    if (!pm_embed_path_str.empty()) {
        gen_params.pm_params.id_embed_path = pm_embed_path_str.c_str();
    }

    // Generate images
    sd_image_t* images = generate_image(ctx, &gen_params);

    if (images == nullptr) {
        throw std::runtime_error(build_error_message("Image generation failed"));
    }

    for (int i = 0; i < params.batch_count; i++) {
        if (images[i].data) {
            std::string filename = "output_" + std::to_string(i) + ".png";
            std::string filepath = (fs::path(job_output_dir) / filename).string();

            if (!save_image(filepath, images[i].data,
                      images[i].width, images[i].height, images[i].channel)) {
                std::cerr << "[SDWrapper] Failed to save image " << i << " to " << filepath << std::endl;
            }

            outputs.push_back(job_id + "/" + filename);

            free(images[i].data);
        }
    }
    free(images);

    // Check if any images were successfully generated
    if (outputs.empty()) {
        throw std::runtime_error(build_error_message("Image generation failed - no valid images produced"));
    }

    return outputs;
}

std::vector<std::string> SDWrapper::generate_txt2vid(
    sd_ctx_t* ctx,
    const Txt2VidParams& params,
    const std::string& lora_dir,
    const std::string& output_dir,
    const std::string& job_id
) {
    std::vector<std::string> outputs;

    // Create output directory
    std::string job_output_dir = (fs::path(output_dir) / job_id).string();
    utils::create_directory(job_output_dir);

    // Parse LoRAs from prompt
    auto [cleaned_prompt, parsed_loras] = parse_loras_from_prompt(params.prompt, lora_dir);

    // Build sd_lora_t array for sd.cpp
    std::vector<sd_lora_t> sd_loras;
    sd_loras.reserve(parsed_loras.size());
    for (const auto& lora : parsed_loras) {
        sd_lora_t sd_lora;
        sd_lora.is_high_noise = lora.is_high_noise;
        sd_lora.multiplier = lora.multiplier;
        sd_lora.path = lora.path.c_str();
        sd_loras.push_back(sd_lora);
    }

    // Log LoRA status for debugging
    std::cout << "[SDWrapper] txt2vid LoRA count: " << sd_loras.size() << std::endl;

    // Initialize video generation parameters
    sd_vid_gen_params_t vid_params;
    sd_vid_gen_params_init(&vid_params);

    // Set LoRAs - explicitly pass count=0 if no LoRAs to ensure sd.cpp clears any cached state
    vid_params.loras = sd_loras.empty() ? nullptr : sd_loras.data();
    vid_params.lora_count = static_cast<uint32_t>(sd_loras.size());

    vid_params.prompt = cleaned_prompt.c_str();
    vid_params.negative_prompt = params.negative_prompt.c_str();
    vid_params.width = params.width;
    vid_params.height = params.height;
    vid_params.video_frames = params.video_frames;
    // Note: fps is stored for metadata but not passed to sd.cpp
    vid_params.seed = params.seed;
    vid_params.clip_skip = params.clip_skip;
    vid_params.strength = params.strength;
    vid_params.moe_boundary = params.moe_boundary;
    vid_params.vace_strength = params.vace_strength;

    // Sample parameters
    vid_params.sample_params.sample_steps = params.steps;
    vid_params.sample_params.guidance.txt_cfg = params.cfg_scale;
    vid_params.sample_params.guidance.distilled_guidance = params.distilled_guidance;
    vid_params.sample_params.sample_method = str_to_sample_method(params.sampler.c_str());
    vid_params.sample_params.scheduler = str_to_scheduler(params.scheduler.c_str());
    vid_params.sample_params.eta = params.eta;
    vid_params.sample_params.flow_shift = params.flow_shift;

    // Skip Layer Guidance (SLG)
    if (params.slg_scale > 0.0f) {
        vid_params.sample_params.guidance.slg.scale = params.slg_scale;
        vid_params.sample_params.guidance.slg.layers = const_cast<int*>(params.skip_layers.data());
        vid_params.sample_params.guidance.slg.layer_count = static_cast<uint32_t>(params.skip_layers.size());
        vid_params.sample_params.guidance.slg.layer_start = params.slg_start;
        vid_params.sample_params.guidance.slg.layer_end = params.slg_end;
    }

    // High-noise sample parameters (MoE models)
    if (params.high_noise_steps > 0) {
        vid_params.high_noise_sample_params.sample_steps = params.high_noise_steps;
        vid_params.high_noise_sample_params.guidance.txt_cfg = params.high_noise_cfg_scale;
        vid_params.high_noise_sample_params.guidance.distilled_guidance = params.high_noise_distilled_guidance;
        if (!params.high_noise_sampler.empty()) {
            vid_params.high_noise_sample_params.sample_method = str_to_sample_method(params.high_noise_sampler.c_str());
        }
        // High-noise SLG
        if (params.high_noise_slg_scale > 0.0f) {
            vid_params.high_noise_sample_params.guidance.slg.scale = params.high_noise_slg_scale;
            vid_params.high_noise_sample_params.guidance.slg.layers = const_cast<int*>(params.high_noise_skip_layers.data());
            vid_params.high_noise_sample_params.guidance.slg.layer_count = static_cast<uint32_t>(params.high_noise_skip_layers.size());
            vid_params.high_noise_sample_params.guidance.slg.layer_start = params.high_noise_slg_start;
            vid_params.high_noise_sample_params.guidance.slg.layer_end = params.high_noise_slg_end;
        }
    }

    // Init image (vid2vid)
    if (!params.init_image_data.empty()) {
        vid_params.init_image.width = params.init_image_width;
        vid_params.init_image.height = params.init_image_height;
        vid_params.init_image.channel = params.init_image_channels;
        vid_params.init_image.data = const_cast<uint8_t*>(params.init_image_data.data());
    } else {
        vid_params.init_image.data = nullptr;
    }

    // End image (FLF2V)
    if (!params.end_image_data.empty()) {
        vid_params.end_image.width = params.end_image_width;
        vid_params.end_image.height = params.end_image_height;
        vid_params.end_image.channel = params.end_image_channels;
        vid_params.end_image.data = const_cast<uint8_t*>(params.end_image_data.data());
    } else {
        vid_params.end_image.data = nullptr;
    }

    // ControlNet support for video uses control_frames array
    // Support single control image or multiple control frames
    std::vector<sd_image_t> control_frames_vec;
    std::vector<std::vector<uint8_t>> control_frames_buffers;
    sd_image_t control_frame;

    if (!params.control_frames_base64.empty()) {
        // Multiple control frames
        control_frames_buffers.reserve(params.control_frames_base64.size());
        control_frames_vec.reserve(params.control_frames_base64.size());
        for (const auto& base64 : params.control_frames_base64) {
            int w, h, c;
            auto data = SDWrapper::decode_base64_image(base64, w, h, c);
            control_frames_buffers.push_back(std::move(data));
            sd_image_t img;
            img.width = w;
            img.height = h;
            img.channel = c;
            img.data = control_frames_buffers.back().data();
            control_frames_vec.push_back(img);
        }
        vid_params.control_frames = control_frames_vec.data();
        vid_params.control_frames_size = static_cast<uint32_t>(control_frames_vec.size());
    } else if (!params.control_image_data.empty()) {
        // Single control image applies to all frames
        control_frame.width = params.control_image_width;
        control_frame.height = params.control_image_height;
        control_frame.channel = params.control_image_channels;
        control_frame.data = const_cast<uint8_t*>(params.control_image_data.data());
        vid_params.control_frames = &control_frame;
        vid_params.control_frames_size = 1;
    } else {
        vid_params.control_frames = nullptr;
        vid_params.control_frames_size = 0;
    }

    // Cache support for DiT models (EasyCache, UCache, etc.)
    if (params.easycache_enabled) {
        vid_params.cache.mode = SD_CACHE_EASYCACHE;
        vid_params.cache.reuse_threshold = params.easycache_threshold;
        vid_params.cache.start_percent = params.easycache_start;
        vid_params.cache.end_percent = params.easycache_end;
    }

    // VAE tiling for large videos
    if (params.vae_tiling) {
        vid_params.vae_tiling_params.enabled = true;
        vid_params.vae_tiling_params.tile_size_x = params.vae_tile_size_x;
        vid_params.vae_tiling_params.tile_size_y = params.vae_tile_size_y;
        vid_params.vae_tiling_params.target_overlap = params.vae_tile_overlap;
    }

    // Generate video frames
    int num_frames = 0;
    sd_image_t* frames = generate_video(ctx, &vid_params, &num_frames);

    if (frames == nullptr) {
        throw std::runtime_error(build_error_message("Video generation failed"));
    }

    for (int i = 0; i < num_frames; i++) {
        if (frames[i].data) {
            char filename[64];
            snprintf(filename, sizeof(filename), "frame_%04d.png", i);
            std::string filepath = (fs::path(job_output_dir) / filename).string();

            if (!save_image(filepath, frames[i].data,
                      frames[i].width, frames[i].height, frames[i].channel)) {
                std::cerr << "[SDWrapper] Failed to save frame " << i << " to " << filepath << std::endl;
            }

            outputs.push_back(job_id + "/" + std::string(filename));

            free(frames[i].data);
        }
    }
    free(frames);

    // Check if any frames were successfully generated
    if (outputs.empty()) {
        throw std::runtime_error(build_error_message("Video generation failed - no valid frames produced"));
    }

    return outputs;
}

std::vector<uint8_t> SDWrapper::load_image(
    const std::string& filepath,
    int& width,
    int& height,
    int& channels
) {
    uint8_t* data = stbi_load(filepath.c_str(), &width, &height, &channels, 3);
    if (!data) {
        throw std::runtime_error("Failed to load image: " + filepath);
    }

    channels = 3;
    std::vector<uint8_t> result(data, data + (width * height * channels));
    stbi_image_free(data);

    return result;
}

std::vector<uint8_t> SDWrapper::decode_base64_image(
    const std::string& base64_data,
    int& width,
    int& height,
    int& channels
) {
    // Decode base64
    std::vector<uint8_t> binary = utils::base64_decode(base64_data);

    // Load image from memory
    uint8_t* data = stbi_load_from_memory(
        binary.data(), static_cast<int>(binary.size()),
        &width, &height, &channels, 3
    );

    if (!data) {
        throw std::runtime_error("Failed to decode base64 image");
    }

    channels = 3;
    std::vector<uint8_t> result(data, data + (width * height * channels));
    stbi_image_free(data);

    return result;
}

bool SDWrapper::save_image(
    const std::string& filepath,
    const uint8_t* data,
    int width,
    int height,
    int channels
) {
    // Validate parameters
    if (data == nullptr) {
        std::cerr << "[SDWrapper] save_image: data is null!" << std::endl;
        return false;
    }
    if (filepath.empty()) {
        std::cerr << "[SDWrapper] save_image: filepath is empty!" << std::endl;
        return false;
    }
    if (width <= 0 || height <= 0 || channels <= 0) {
        std::cerr << "[SDWrapper] save_image: invalid dimensions: "
                  << width << "x" << height << "x" << channels << std::endl;
        return false;
    }

    std::string ext = utils::get_file_extension(filepath);

    int result = 0;
    if (ext == "png") {
        result = stbi_write_png(filepath.c_str(), width, height, channels, data, 0);
    } else if (ext == "jpg" || ext == "jpeg") {
        result = stbi_write_jpg(filepath.c_str(), width, height, channels, data, 90);
    } else {
        // Default to PNG
        result = stbi_write_png(filepath.c_str(), width, height, channels, data, 0);
    }

    if (result == 0) {
        std::cerr << "[SDWrapper] save_image: failed to write " << filepath << std::endl;
    }

    return result != 0;
}

bool SDWrapper::convert_model(
    const std::string& input_path,
    const std::string& vae_path,
    const std::string& output_path,
    const std::string& output_type,
    const std::string& tensor_type_rules
) {
    // Validate input
    if (input_path.empty()) {
        throw std::runtime_error("Input model path is required");
    }
    if (output_path.empty()) {
        throw std::runtime_error("Output path is required");
    }
    if (output_type.empty()) {
        throw std::runtime_error("Output type (quantization) is required");
    }

    // Check input file exists
    if (!fs::exists(input_path)) {
        throw std::runtime_error("Input model not found: " + input_path);
    }

    // Convert output type string to sd_type_t
    sd_type_t sd_type = str_to_sd_type(output_type.c_str());
    if (sd_type == SD_TYPE_COUNT) {
        throw std::runtime_error("Invalid output type: " + output_type);
    }

    std::cout << "[SDWrapper] Converting model:" << std::endl;
    std::cout << "[SDWrapper]   Input: " << input_path << std::endl;
    if (!vae_path.empty()) {
        std::cout << "[SDWrapper]   VAE: " << vae_path << std::endl;
    }
    std::cout << "[SDWrapper]   Output: " << output_path << std::endl;
    std::cout << "[SDWrapper]   Type: " << output_type << " (" << sd_type_name(sd_type) << ")" << std::endl;
    if (!tensor_type_rules.empty()) {
        std::cout << "[SDWrapper]   Tensor rules: " << tensor_type_rules << std::endl;
    }

    // Call sd.cpp convert function
    // Note: tensor_type_rules must be empty string, not nullptr - sd.cpp passes it to a
    // function expecting std::string& which cannot be constructed from nullptr
    bool success = convert(
        input_path.c_str(),
        vae_path.empty() ? nullptr : vae_path.c_str(),
        output_path.c_str(),
        sd_type,
        tensor_type_rules.c_str(),  // Always pass c_str(), never nullptr
        false  // convert_name: keep original tensor names
    );

    if (success) {
        std::cout << "[SDWrapper] Conversion completed successfully" << std::endl;
    } else {
        std::cerr << "[SDWrapper] Conversion failed" << std::endl;
    }

    return success;
}

} // namespace sdcpp
