#include "model_manager.hpp"
#include "websocket_server.hpp"
#include "utils.hpp"
#include "memory_utils.hpp"
#include "sd_error_capture.hpp"

#include <iostream>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_set>

#include "stable-diffusion.h"

#ifdef SDCPP_USE_CUDA
#include "cuda_arch_check.hpp"
#endif

namespace fs = std::filesystem;

// Thread-local pointer for progress callback context
static thread_local sdcpp::ModelManager* g_loading_model_manager = nullptr;

// Progress callback for model loading
static void model_loading_progress_callback(int step, int total_steps, float /*time*/, void* /*data*/) {
    if (g_loading_model_manager) {
        g_loading_model_manager->update_loading_progress(step, total_steps);
    }
}

namespace sdcpp {

// Helper functions to convert strings to sd.cpp enums
static rng_type_t string_to_rng_type(const std::string& str) {
    if (str == "std_default" || str == "std") return STD_DEFAULT_RNG;
    if (str == "cuda") return CUDA_RNG;
    if (str == "cpu") return CPU_RNG;
    return CUDA_RNG;  // default
}

static prediction_t string_to_prediction_type(const std::string& str) {
    if (str.empty()) return PREDICTION_COUNT;  // auto-detect
    if (str == "eps") return EPS_PRED;
    if (str == "v") return V_PRED;
    if (str == "edm_v") return EDM_V_PRED;
    if (str == "flow" || str == "sd3_flow") return FLOW_PRED;
    if (str == "flux_flow") return FLUX_FLOW_PRED;
    if (str == "sefi_flow") return SEFI_FLOW_PRED;      // leejet PR #1707
    if (str == "minit2i_flow") return MINIT2I_FLOW_PRED; // leejet PR #1683
    // FLUX2_FLOW_PRED was removed upstream — Flux2 now uses FLUX_FLOW_PRED.
    // Accept the legacy "flux2_flow" string and map it there so old configs
    // don't 400.
    if (str == "flux2_flow") return FLUX_FLOW_PRED;
    return PREDICTION_COUNT;  // auto-detect for unknown
}

static lora_apply_mode_t string_to_lora_apply_mode(const std::string& str) {
    if (str == "immediately") return LORA_APPLY_IMMEDIATELY;
    if (str == "at_runtime" || str == "runtime") return LORA_APPLY_AT_RUNTIME;
    return LORA_APPLY_AUTO;  // default
}

// The legacy offload helpers (string_to_offload_mode, string_to_vram_estimation)
// only exist on the feature/vram-offloading-v2 fork branch. The unified-streaming
// branch dropped the sd_offload_mode_t / sd_vram_estimation_t enums entirely
// in favor of a single bool stream_layers + max_vram budget. Gate both helpers
// with the negation of SDCPP_UNIFIED_STREAMING to skip them on the new variant.
#if defined(SDCPP_EXPERIMENTAL_OFFLOAD) && !defined(SDCPP_UNIFIED_STREAMING)
static sd_offload_mode_t string_to_offload_mode(const std::string& str) {
    if (str == "cond_only") return SD_OFFLOAD_COND_ONLY;
    if (str == "cond_diffusion") return SD_OFFLOAD_COND_DIFFUSION;
    if (str == "aggressive") return SD_OFFLOAD_AGGRESSIVE;
    if (str == "layer_streaming") return SD_OFFLOAD_LAYER_STREAMING;
    return SD_OFFLOAD_NONE;  // default
}

static sd_vram_estimation_t string_to_vram_estimation(const std::string& str) {
    if (str == "formula") return SD_VRAM_EST_FORMULA;
    return SD_VRAM_EST_DRYRUN;  // default - accurate graph-based estimation
}
#endif

std::string model_type_to_string(ModelType type) {
    switch (type) {
        case ModelType::Checkpoint: return "checkpoint";
        case ModelType::Diffusion: return "diffusion";
        case ModelType::VAE: return "vae";
        case ModelType::LoRA: return "lora";
        case ModelType::CLIP: return "clip";
        case ModelType::T5: return "t5";
        case ModelType::Embedding: return "embedding";
        case ModelType::ControlNet: return "controlnet";
        case ModelType::LLM: return "llm";
        case ModelType::ESRGAN: return "esrgan";
        case ModelType::TAESD: return "taesd";
        default: return "unknown";
    }
}

ModelType string_to_model_type(const std::string& str) {
    if (str == "checkpoint") return ModelType::Checkpoint;
    if (str == "diffusion") return ModelType::Diffusion;
    if (str == "vae") return ModelType::VAE;
    if (str == "lora") return ModelType::LoRA;
    if (str == "clip") return ModelType::CLIP;
    if (str == "t5") return ModelType::T5;
    if (str == "embedding") return ModelType::Embedding;
    if (str == "controlnet") return ModelType::ControlNet;
    if (str == "llm") return ModelType::LLM;
    if (str == "esrgan") return ModelType::ESRGAN;
    if (str == "taesd") return ModelType::TAESD;
    return ModelType::Checkpoint;
}

nlohmann::json ModelInfo::to_json() const {
    return nlohmann::json{
        {"name", name},
        {"type", model_type_to_string(type)},
        {"file_extension", file_extension},
        {"size_bytes", file_size},
        {"hash", hash.empty() ? nullptr : nlohmann::json(hash)}
    };
}

// Helper function for case-insensitive substring search
static bool contains_case_insensitive(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    
    auto it = std::search(
        haystack.begin(), haystack.end(),
        needle.begin(), needle.end(),
        [](char ch1, char ch2) {
            return std::tolower(static_cast<unsigned char>(ch1)) == 
                   std::tolower(static_cast<unsigned char>(ch2));
        }
    );
    return it != haystack.end();
}

bool ModelFilter::matches(const ModelInfo& model) const {
    // Filter by type
    if (type.has_value() && model.type != type.value()) {
        return false;
    }
    
    // Filter by extension (case-insensitive)
    if (extension.has_value()) {
        std::string ext_lower = extension.value();
        std::string model_ext_lower = model.file_extension;
        std::transform(ext_lower.begin(), ext_lower.end(), ext_lower.begin(), 
                      [](unsigned char c) { return std::tolower(c); });
        std::transform(model_ext_lower.begin(), model_ext_lower.end(), model_ext_lower.begin(),
                      [](unsigned char c) { return std::tolower(c); });
        if (ext_lower != model_ext_lower) {
            return false;
        }
    }
    
    // Filter by name search (case-insensitive substring match)
    if (search.has_value() && !search.value().empty()) {
        if (!contains_case_insensitive(model.name, search.value())) {
            return false;
        }
    }
    
    return true;
}

bool ModelFilter::is_empty() const {
    return !type.has_value() && !extension.has_value() && 
           (!search.has_value() || search.value().empty());
}

// Reject unknown keys at the JSON-parsing boundary. Silently dropping
// unrecognized fields used to cause user-facing bugs ("I sent diffusion_fa
// but it had no effect" — wrong field name, parser ignored it). Strict
// rejection makes typos surface immediately as 400.
//
// `where` is a human-readable label for the message; `j` is the object to
// scan; `known` is the closed set of accepted keys at this level.
static void reject_unknown_keys(const std::string& where,
                                const nlohmann::json& j,
                                const std::unordered_set<std::string>& known) {
    if (!j.is_object()) return;
    std::vector<std::string> unknown;
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (known.find(it.key()) == known.end()) {
            unknown.push_back(it.key());
        }
    }
    if (!unknown.empty()) {
        std::string msg = "Unknown field(s) in " + where + ": ";
        for (size_t i = 0; i < unknown.size(); ++i) {
            if (i) msg += ", ";
            msg += unknown[i];
        }
        msg += ". Check the spelling against the OpenAPI schema at /openapi.json.";
        throw std::runtime_error(msg);
    }
}

ModelLoadParams ModelLoadParams::from_json(const nlohmann::json& j) {
    ModelLoadParams params;

    // Top-level allowed keys for POST /models/load body.
    static const std::unordered_set<std::string> KNOWN_TOP_LEVEL = {
        "model_name", "model_type",
        // Component model paths
        "vae", "clip_l", "clip_g", "clip_vision", "t5xxl", "controlnet",
        "llm", "llm_vision", "taesd",
        "high_noise_diffusion_model", "uncond_diffusion_model", "photo_maker",
        "pulid_weights",
        "audio_vae", "embeddings_connectors",
        // Nested options object
        "options",
    };
    reject_unknown_keys("/models/load body", j, KNOWN_TOP_LEVEL);

    params.model_name = j.value("model_name", "");

    if (j.contains("model_type")) {
        params.model_type = string_to_model_type(j["model_type"].get<std::string>());
    }

    // Component model paths
    if (j.contains("vae") && !j["vae"].is_null()) {
        params.vae = j["vae"].get<std::string>();
    }
    if (j.contains("clip_l") && !j["clip_l"].is_null()) {
        params.clip_l = j["clip_l"].get<std::string>();
    }
    if (j.contains("clip_g") && !j["clip_g"].is_null()) {
        params.clip_g = j["clip_g"].get<std::string>();
    }
    if (j.contains("clip_vision") && !j["clip_vision"].is_null()) {
        params.clip_vision = j["clip_vision"].get<std::string>();
    }
    if (j.contains("t5xxl") && !j["t5xxl"].is_null()) {
        params.t5xxl = j["t5xxl"].get<std::string>();
    }
    if (j.contains("controlnet") && !j["controlnet"].is_null()) {
        params.controlnet = j["controlnet"].get<std::string>();
    }
    if (j.contains("llm") && !j["llm"].is_null()) {
        params.llm = j["llm"].get<std::string>();
    }
    if (j.contains("llm_vision") && !j["llm_vision"].is_null()) {
        params.llm_vision = j["llm_vision"].get<std::string>();
    }
    if (j.contains("taesd") && !j["taesd"].is_null()) {
        params.taesd = j["taesd"].get<std::string>();
    }
    if (j.contains("high_noise_diffusion_model") && !j["high_noise_diffusion_model"].is_null()) {
        params.high_noise_diffusion_model = j["high_noise_diffusion_model"].get<std::string>();
    }
    if (j.contains("uncond_diffusion_model") && !j["uncond_diffusion_model"].is_null()) {
        params.uncond_diffusion_model = j["uncond_diffusion_model"].get<std::string>();
    }
    if (j.contains("pulid_weights") && !j["pulid_weights"].is_null()) {
        params.pulid_weights = j["pulid_weights"].get<std::string>();
    }
    if (j.contains("photo_maker") && !j["photo_maker"].is_null()) {
        params.photo_maker = j["photo_maker"].get<std::string>();
    }
    if (j.contains("audio_vae") && !j["audio_vae"].is_null()) {
        params.audio_vae = j["audio_vae"].get<std::string>();
    }
    if (j.contains("embeddings_connectors") && !j["embeddings_connectors"].is_null()) {
        params.embeddings_connectors = j["embeddings_connectors"].get<std::string>();
    }

    if (j.contains("options")) {
        auto& opts = j["options"];
        // Allowed keys inside the "options" sub-object. Must enumerate every
        // key the parser below consumes — drift here means real options get
        // rejected as unknown. Sorted by section to mirror the parse order
        // for diff-friendliness.
        static const std::unordered_set<std::string> KNOWN_OPTIONS = {
            // Performance / threading
            "n_threads", "flash_attn", "diffusion_flash_attn",
            // Memory residency — upstream now expresses per-component and
            // global CPU placement via the `backend` / `params_backend` strings
            // below (e.g. "diffusion=cuda0,vae=cpu" or params_backend="*=cpu"),
            // so the legacy bool flags (keep_clip_on_cpu, keep_vae_on_cpu,
            // keep_controlnet_on_cpu, offload_to_cpu, free_params_immediately,
            // vae_decode_only) are no longer accepted.
            "enable_mmap", "max_vram",
            // VAE/diffusion compute
            "vae_conv_direct", "diffusion_conv_direct",
            "tae_preview_only", "force_sdxl_vae_conv_scale",
            // Weight type / per-tensor rules — flow_shift is per-generation now.
            "weight_type", "tensor_type_rules",
            // RNG
            "rng_type", "sampler_rng_type",
            // Model behavior
            "prediction", "lora_apply_mode",
            // Chroma-specific
            "chroma_use_dit_mask", "chroma_use_t5_mask", "chroma_t5_mask_pad",
            // Qwen-Image specific
            "qwen_image_zero_cond_t",
            // VAE format override (leejet PR for sd_vae_format_t enum)
            "vae_format",
            // Tileable / seamless texture position embeddings (leejet PR #1627
            // — circular RoPE for ideogram4 + Flux). Independent X/Y axes so
            // users can request horizontal-only or vertical-only tiling.
            "circular_x", "circular_y",
            // Backend routing (sd.cpp post-2026-05-16; rpc_servers added in leejet PR #1629)
            "backend", "params_backend", "rpc_servers",
            // Experimental offload (only honored when SDCPP_EXPERIMENTAL_OFFLOAD is on,
            // but accepted in the schema regardless so OFFLOAD=OFF builds don't 400
            // on configs authored against an OFFLOAD=ON build). Both fork variants'
            // options live in the same allowlist — the irrelevant ones get parsed
            // into params fields that nobody reads, which is harmless.
            //
            // feature/vram-offloading-v2 fields:
            "offload_mode", "vram_estimation",
            "offload_cond_stage", "offload_diffusion",
            "reload_cond_stage", "reload_diffusion",
            "log_offload_events", "min_offload_size_mb", "target_free_vram_mb",
            "streaming_prefetch_layers",
            "streaming_keep_layers_behind", "streaming_min_free_vram_mb",
            // feature/unified-streaming fields:
            "stream_layers",
            // leejet PR #1687 — eager-load params at model-load time
            "eager_load",
        };
        reject_unknown_keys("/models/load options", opts, KNOWN_OPTIONS);
        params.n_threads = opts.value("n_threads", -1);
        params.flash_attn = opts.value("flash_attn", true);
        params.diffusion_flash_attn = opts.value("diffusion_flash_attn", false);
        params.enable_mmap = opts.value("enable_mmap", true);
        params.vae_conv_direct = opts.value("vae_conv_direct", false);
        params.diffusion_conv_direct = opts.value("diffusion_conv_direct", false);
        params.tae_preview_only = opts.value("tae_preview_only", false);
        // 0.0 = disabled (sd.cpp default).
        params.max_vram = opts.value("max_vram", 0.0f);
        params.weight_type = opts.value("weight_type", "");
        params.tensor_type_rules = opts.value("tensor_type_rules", "");

        // RNG options
        params.rng_type = opts.value("rng_type", "cuda");
        params.sampler_rng_type = opts.value("sampler_rng_type", "");

        // Prediction type override (empty = auto)
        params.prediction = opts.value("prediction", "");

        // LoRA apply mode — default `auto` so sd.cpp picks the best path for the
        // current load. Previously hard-defaulted to `runtime` to dodge a
        // historical caching issue where sd.cpp held incompatible LoRA state
        // across generations; that's no longer load-bearing, and `runtime`
        // actively hurts the unified-streaming path because it forces a
        // per-step LoRA application against streamed chunks (the base tensor
        // gets pulled in fresh every chunk, then the runtime LoRA delta is
        // re-applied each time). `auto` lets sd.cpp pick `immediately` when
        // the base weights are stable and `runtime` only when the workflow
        // demands it, matching the streaming planner's residency assumptions.
        params.lora_apply_mode = opts.value("lora_apply_mode", "auto");

        // VAE tiling is per-generation now (sd_tiling_params_t), not load-time.

        params.force_sdxl_vae_conv_scale = opts.value("force_sdxl_vae_conv_scale", false);

        // Chroma options
        params.chroma_use_dit_mask = opts.value("chroma_use_dit_mask", true);
        params.chroma_use_t5_mask = opts.value("chroma_use_t5_mask", false);
        params.chroma_t5_mask_pad = opts.value("chroma_t5_mask_pad", 1);

        // Qwen-Image specific
        params.qwen_image_zero_cond_t = opts.value("qwen_image_zero_cond_t", false);

        // VAE format override + tileable position embeddings
        params.vae_format = opts.value("vae_format", "auto");
        params.circular_x = opts.value("circular_x", false);
        params.circular_y = opts.value("circular_y", false);

        // Backend routing
        params.backend = opts.value("backend", "");
        params.params_backend = opts.value("params_backend", "");
        params.rpc_servers = opts.value("rpc_servers", "");

#if defined(SDCPP_EXPERIMENTAL_OFFLOAD) && !defined(SDCPP_UNIFIED_STREAMING)
        // ── feature/vram-offloading-v2 path ──────────────────────────────
        // Dynamic tensor offloading options (legacy multi-mode API).
        params.offload_mode = opts.value("offload_mode", "none");
        params.vram_estimation = opts.value("vram_estimation", "dryrun");
        // offload_cond_stage default is mode-aware: in layer_streaming the
        // diffusion is already paying for VRAM via per-layer dispatch — adding
        // a per-gen kick of the LLM/CLIP back to CPU then trying to reload it
        // produces seconds-to-minutes of disk thrashing on each generation
        // (mmap fault-in of multi-GB encoder weights). Default `false` for
        // layer_streaming so cond_stage stays resident across gens. Other
        // offload modes keep the original `true` default — those modes are
        // explicitly opt-in for aggressive VRAM recycling.
        const bool offload_cond_stage_default =
            params.offload_mode != "layer_streaming";
        params.offload_cond_stage =
            opts.value("offload_cond_stage", offload_cond_stage_default);
        params.offload_diffusion = opts.value("offload_diffusion", false);
        params.reload_cond_stage = opts.value("reload_cond_stage", true);
        params.reload_diffusion = opts.value("reload_diffusion", true);
        params.log_offload_events = opts.value("log_offload_events", true);
        params.min_offload_size_mb = opts.value("min_offload_size_mb", 0);
        params.target_free_vram_mb = opts.value("target_free_vram_mb", 0);

        // Layer streaming options
        params.streaming_prefetch_layers = opts.value("streaming_prefetch_layers", 1);
        params.streaming_keep_layers_behind = opts.value("streaming_keep_layers_behind", 0);
        params.streaming_min_free_vram_mb = opts.value("streaming_min_free_vram_mb", 0);
#else
        // ── feature/unified-streaming path ───────────────────────────────
        // The new fork branch removed sd_offload_config_t entirely. Streaming
        // is engaged via a single stream_layers bool on top of the existing
        // max_vram budget — sd.cpp's planner picks the residency split, runs
        // async H2D prefetch for the next segment while computing the current.
        params.stream_layers = opts.value("stream_layers", false);
#endif
        // Default true — see ModelLoadParams.eager_load comment for why
        // the restapi flips the upstream default.
        params.eager_load = opts.value("eager_load", true);
    }

    return params;
}

ModelManager::ModelManager(const Config& config)
    : config_(config) {
}

ModelManager::~ModelManager() {
    unload_model();
    unload_upscaler();
}

void ModelManager::scan_models() {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    models_.clear();
    
    // Scan each type of model directory
    scan_directory(config_.paths.checkpoints, ModelType::Checkpoint);
    scan_directory(config_.paths.diffusion_models, ModelType::Diffusion);
    scan_directory(config_.paths.vae, ModelType::VAE);
    scan_directory(config_.paths.lora, ModelType::LoRA);
    scan_directory(config_.paths.clip, ModelType::CLIP);
    scan_directory(config_.paths.t5, ModelType::T5);
    
    if (!config_.paths.embeddings.empty() && utils::directory_exists(config_.paths.embeddings)) {
        scan_directory(config_.paths.embeddings, ModelType::Embedding);
    }
    
    if (!config_.paths.controlnet.empty() && utils::directory_exists(config_.paths.controlnet)) {
        scan_directory(config_.paths.controlnet, ModelType::ControlNet);
    }
    
    if (!config_.paths.llm.empty() && utils::directory_exists(config_.paths.llm)) {
        scan_directory(config_.paths.llm, ModelType::LLM);
    }
    
    if (!config_.paths.esrgan.empty() && utils::directory_exists(config_.paths.esrgan)) {
        scan_directory(config_.paths.esrgan, ModelType::ESRGAN);
    }

    if (!config_.paths.taesd.empty() && utils::directory_exists(config_.paths.taesd)) {
        scan_directory(config_.paths.taesd, ModelType::TAESD);
    }

    std::cout << "[ModelManager] Scanned models:" << std::endl;
    for (const auto& [type, type_models] : models_) {
        std::cout << "  " << model_type_to_string(type) << ": " << type_models.size() << " models" << std::endl;
    }
}

void ModelManager::scan_directory(const std::string& base_path, ModelType type) {
    if (base_path.empty() || !utils::directory_exists(base_path)) {
        return;
    }

    std::vector<std::string> extensions = {".safetensors", ".gguf", ".ckpt", ".pt", ".pth"};
    auto files = utils::list_files(base_path, extensions, true);

    for (const auto& rel_path : files) {
        std::string full_path = (fs::path(base_path) / rel_path).string();
        std::string ext = utils::get_file_extension(full_path);

        // For ESRGAN models, .pth/.pt files must be ZIP-based (PyTorch ZIP archives).
        // Plain serialized .pth files are not supported by sd.cpp - skip them.
        if (type == ModelType::ESRGAN && (ext == "pth" || ext == "pt")) {
            if (!utils::is_zip_archive(full_path)) {
                continue;  // Skip unsupported format
            }
        }

        ModelInfo info;
        info.name = rel_path;
        info.full_path = full_path;
        info.type = type;
        info.file_extension = ext;
        info.file_size = utils::get_file_size(full_path);

        models_[type][rel_path] = info;
    }
}

std::string ModelManager::get_base_path(ModelType type) const {
    switch (type) {
        case ModelType::Checkpoint: return config_.paths.checkpoints;
        case ModelType::Diffusion: return config_.paths.diffusion_models;
        case ModelType::VAE: return config_.paths.vae;
        case ModelType::LoRA: return config_.paths.lora;
        case ModelType::CLIP: return config_.paths.clip;
        case ModelType::T5: return config_.paths.t5;
        case ModelType::Embedding: return config_.paths.embeddings;
        case ModelType::ControlNet: return config_.paths.controlnet;
        case ModelType::LLM: return config_.paths.llm;
        case ModelType::ESRGAN: return config_.paths.esrgan;
        case ModelType::TAESD: return config_.paths.taesd;
        default: return "";
    }
}

std::vector<ModelInfo> ModelManager::get_models(ModelType type) const {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    
    std::vector<ModelInfo> result;
    auto it = models_.find(type);
    if (it != models_.end()) {
        for (const auto& [name, info] : it->second) {
            result.push_back(info);
        }
    }
    return result;
}

nlohmann::json ModelManager::get_models_json() const {
    return get_models_json(ModelFilter{});
}

nlohmann::json ModelManager::get_models_json(const ModelFilter& filter) const {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    
    nlohmann::json result;
    
    auto add_models = [&](ModelType type, const std::string& key) {
        // Skip this category if type filter is set and doesn't match
        if (filter.type.has_value() && filter.type.value() != type) {
            result[key] = nlohmann::json::array();
            return;
        }
        
        nlohmann::json arr = nlohmann::json::array();
        auto it = models_.find(type);
        if (it != models_.end()) {
            for (const auto& [name, info] : it->second) {
                // Apply filter
                if (!filter.matches(info)) {
                    continue;
                }
                
                auto j = info.to_json();
                // Add is_loaded flag for main models
                if (type == ModelType::Checkpoint || type == ModelType::Diffusion) {
                    j["is_loaded"] = (name == loaded_model_name_ && type == loaded_model_type_);
                }
                arr.push_back(j);
            }
        }
        result[key] = arr;
    };
    
    add_models(ModelType::Checkpoint, "checkpoints");
    add_models(ModelType::Diffusion, "diffusion_models");
    add_models(ModelType::VAE, "vae");
    add_models(ModelType::LoRA, "loras");
    add_models(ModelType::CLIP, "clip");
    add_models(ModelType::T5, "t5");
    add_models(ModelType::ControlNet, "controlnets");
    add_models(ModelType::LLM, "llm");
    add_models(ModelType::ESRGAN, "esrgan");
    add_models(ModelType::TAESD, "taesd");

    result["loaded_model"] = loaded_model_name_.empty() ? nullptr : nlohmann::json(loaded_model_name_);
    result["loaded_model_type"] = loaded_model_name_.empty() ? nullptr : nlohmann::json(model_type_to_string(loaded_model_type_));
    
    // Add filter info to response
    if (!filter.is_empty()) {
        nlohmann::json filter_info;
        if (filter.type.has_value()) {
            filter_info["type"] = model_type_to_string(filter.type.value());
        }
        if (filter.extension.has_value()) {
            filter_info["extension"] = filter.extension.value();
        }
        if (filter.search.has_value() && !filter.search.value().empty()) {
            filter_info["search"] = filter.search.value();
        }
        result["applied_filters"] = filter_info;
    }
    
    return result;
}

std::optional<ModelInfo> ModelManager::get_model(const std::string& name, ModelType type) const {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    
    auto type_it = models_.find(type);
    if (type_it == models_.end()) {
        return std::nullopt;
    }
    
    auto it = type_it->second.find(name);
    if (it == type_it->second.end()) {
        return std::nullopt;
    }
    
    return it->second;
}

bool ModelManager::load_model(const ModelLoadParams& params) {
    std::lock_guard<std::mutex> lock(context_mutex_);

    // Set loading state
    loading_model_name_ = params.model_name;
    last_load_error_.clear();
    loading_step_ = 0;
    loading_total_steps_ = 0;
    model_loading_ = true;
    // Drain any stale SD_LOG_ERROR captures so the post-fail diagnostic
    // only reflects messages from THIS load attempt — a previous failed
    // generate that left errors in the ring buffer would otherwise leak
    // into the next load's user-facing failure string.
    clear_sd_errors();

    // Helper to clear loading state on exit
    auto clear_loading = [this]() {
        model_loading_ = false;
        loading_model_name_.clear();
        loading_step_ = 0;
        loading_total_steps_ = 0;
    };

    // ===== PHASE 1: Validate all model files exist before loading =====
    std::vector<std::string> errors;
    
    // Validate main model
    auto model_info = get_model(params.model_name, params.model_type);
    if (!model_info) {
        std::string type_str = model_type_to_string(params.model_type);
        std::string base_path = get_base_path(params.model_type);
        errors.push_back("Main model not found: '" + params.model_name + "' (type: " + type_str + 
                        ", searched in: " + (base_path.empty() ? "<not configured>" : base_path) + ")");
    }
    
    // Validate VAE if specified
    std::optional<ModelInfo> vae_info;
    if (params.vae) {
        vae_info = get_model(*params.vae, ModelType::VAE);
        if (!vae_info) {
            std::string base_path = get_base_path(ModelType::VAE);
            errors.push_back("VAE model not found: '" + *params.vae + 
                           "' (searched in: " + (base_path.empty() ? "<not configured>" : base_path) + ")");
        }
    }
    
    // Validate CLIP-L if specified
    std::optional<ModelInfo> clip_l_info;
    if (params.clip_l) {
        clip_l_info = get_model(*params.clip_l, ModelType::CLIP);
        if (!clip_l_info) {
            std::string base_path = get_base_path(ModelType::CLIP);
            errors.push_back("CLIP-L model not found: '" + *params.clip_l + 
                           "' (searched in: " + (base_path.empty() ? "<not configured>" : base_path) + ")");
        }
    }
    
    // Validate CLIP-G if specified
    std::optional<ModelInfo> clip_g_info;
    if (params.clip_g) {
        clip_g_info = get_model(*params.clip_g, ModelType::CLIP);
        if (!clip_g_info) {
            std::string base_path = get_base_path(ModelType::CLIP);
            errors.push_back("CLIP-G model not found: '" + *params.clip_g + 
                           "' (searched in: " + (base_path.empty() ? "<not configured>" : base_path) + ")");
        }
    }
    
    // Validate T5 if specified
    std::optional<ModelInfo> t5_info;
    if (params.t5xxl) {
        t5_info = get_model(*params.t5xxl, ModelType::T5);
        if (!t5_info) {
            std::string base_path = get_base_path(ModelType::T5);
            errors.push_back("T5 model not found: '" + *params.t5xxl + 
                           "' (searched in: " + (base_path.empty() ? "<not configured>" : base_path) + ")");
        }
    }
    
    // Validate ControlNet if specified
    std::optional<ModelInfo> controlnet_info;
    if (params.controlnet) {
        controlnet_info = get_model(*params.controlnet, ModelType::ControlNet);
        if (!controlnet_info) {
            std::string base_path = get_base_path(ModelType::ControlNet);
            errors.push_back("ControlNet model not found: '" + *params.controlnet + 
                           "' (searched in: " + (base_path.empty() ? "<not configured>" : base_path) + ")");
        }
    }
    
    // Validate LLM if specified
    std::optional<ModelInfo> llm_info;
    if (params.llm) {
        llm_info = get_model(*params.llm, ModelType::LLM);
        if (!llm_info) {
            std::string base_path = get_base_path(ModelType::LLM);
            errors.push_back("LLM model not found: '" + *params.llm + 
                           "' (searched in: " + (base_path.empty() ? "<not configured>" : base_path) + ")");
        }
    }
    
    // Validate LLM Vision if specified
    std::optional<ModelInfo> llm_vision_info;
    if (params.llm_vision) {
        llm_vision_info = get_model(*params.llm_vision, ModelType::LLM);
        if (!llm_vision_info) {
            std::string base_path = get_base_path(ModelType::LLM);
            errors.push_back("LLM Vision model not found: '" + *params.llm_vision +
                           "' (searched in: " + (base_path.empty() ? "<not configured>" : base_path) + ")");
        }
    }

    // Validate CLIP Vision if specified
    std::optional<ModelInfo> clip_vision_info;
    if (params.clip_vision) {
        clip_vision_info = get_model(*params.clip_vision, ModelType::CLIP);
        if (!clip_vision_info) {
            std::string base_path = get_base_path(ModelType::CLIP);
            errors.push_back("CLIP Vision model not found: '" + *params.clip_vision +
                           "' (searched in: " + (base_path.empty() ? "<not configured>" : base_path) + ")");
        }
    }

    // Validate TAESD if specified
    std::optional<ModelInfo> taesd_info;
    if (params.taesd) {
        taesd_info = get_model(*params.taesd, ModelType::TAESD);
        if (!taesd_info) {
            std::string base_path = get_base_path(ModelType::TAESD);
            errors.push_back("TAESD model not found: '" + *params.taesd +
                           "' (searched in: " + (base_path.empty() ? "<not configured>" : base_path) + ")");
        }
    }

    // Validate High-Noise Diffusion Model if specified
    std::optional<ModelInfo> high_noise_diffusion_info;
    if (params.high_noise_diffusion_model) {
        high_noise_diffusion_info = get_model(*params.high_noise_diffusion_model, ModelType::Diffusion);
        if (!high_noise_diffusion_info) {
            std::string base_path = get_base_path(ModelType::Diffusion);
            errors.push_back("High-noise diffusion model not found: '" + *params.high_noise_diffusion_model +
                           "' (searched in: " + (base_path.empty() ? "<not configured>" : base_path) + ")");
        }
    }

    // Validate Unconditional Diffusion Model if specified (leejet post-1ceb5bd)
    std::optional<ModelInfo> uncond_diffusion_info;
    if (params.uncond_diffusion_model) {
        uncond_diffusion_info = get_model(*params.uncond_diffusion_model, ModelType::Diffusion);
        if (!uncond_diffusion_info) {
            std::string base_path = get_base_path(ModelType::Diffusion);
            errors.push_back("Unconditional diffusion model not found: '" + *params.uncond_diffusion_model +
                           "' (searched in: " + (base_path.empty() ? "<not configured>" : base_path) + ")");
        }
    }

    // Validate PhotoMaker if specified (look in checkpoints dir)
    std::optional<ModelInfo> photo_maker_info;
    if (params.photo_maker) {
        photo_maker_info = get_model(*params.photo_maker, ModelType::Checkpoint);
        if (!photo_maker_info) {
            std::string base_path = get_base_path(ModelType::Checkpoint);
            errors.push_back("PhotoMaker model not found: '" + *params.photo_maker +
                           "' (searched in: " + (base_path.empty() ? "<not configured>" : base_path) + ")");
        }
    }

    // Validate PuLID weights if specified (leejet PR #1595). The weight file
    // is logically a checkpoint-class artifact, so we look it up in the same
    // directory PhotoMaker uses.
    std::optional<ModelInfo> pulid_weights_info;
    if (params.pulid_weights) {
        pulid_weights_info = get_model(*params.pulid_weights, ModelType::Checkpoint);
        if (!pulid_weights_info) {
            std::string base_path = get_base_path(ModelType::Checkpoint);
            errors.push_back("PuLID weights not found: '" + *params.pulid_weights +
                           "' (searched in: " + (base_path.empty() ? "<not configured>" : base_path) + ")");
        }
    }

    // ===== PHASE 1.5: GPU-format compatibility check =====
    // Refuse loads of NVFP4/FP8-quantized files on GPUs that don't support
    // those formats natively — sd.cpp would silently dequantize to fp16 at
    // load time, doubling/tripling the on-disk size in VRAM and OOM'ing
    // before any offload mode can engage. We catch it here with a clear
    // message + suggested alternative instead of letting the user discover
    // it as a generic "insufficient memory" or kernel-dispatch crash.
#ifdef SDCPP_USE_CUDA
    auto add_format_error = [&errors](const std::optional<ModelInfo>& info) {
        if (!info) return;
        auto check = sdcpp::cuda::check_model_format_compatibility(info->full_path);
        if (!check.ok) {
            std::string err = check.message;
            if (!check.suggested_alternative.empty()) {
                err += " " + check.suggested_alternative;
            }
            errors.push_back(err);
        }
    };
    add_format_error(model_info);
    add_format_error(vae_info);
    add_format_error(llm_info);
    add_format_error(llm_vision_info);
    add_format_error(clip_vision_info);
    add_format_error(taesd_info);
    add_format_error(high_noise_diffusion_info);
    add_format_error(photo_maker_info);
#endif

    // If any validation errors, throw with all error messages
    if (!errors.empty()) {
        std::string error_msg = "Model validation failed:\n";
        for (const auto& err : errors) {
            error_msg += "  - " + err + "\n";
        }
        last_load_error_ = error_msg;
        clear_loading();

        // Broadcast model load failure via WebSocket
        if (auto* ws = get_websocket_server()) {
            ws->broadcast(WSEventType::ModelLoadFailed, {
                {"model_name", params.model_name},
                {"error", error_msg}
            });
        }

        throw std::runtime_error(error_msg);
    }
    
    // ===== PHASE 2: All models validated, now unload and load =====
    
    // Unload current model if any
    if (context_ != nullptr) {
#if defined(SDCPP_EXPERIMENTAL_OFFLOAD) && !defined(SDCPP_UNIFIED_STREAMING)
        // Free all GPU resources before unloading to prevent memory leaks.
        // Only the legacy fork branch (feature/vram-offloading-v2) exposed this
        // helper; the unified-streaming branch removed it because free_sd_ctx
        // handles the equivalent teardown internally.
        sd_free_gpu_resources(context_);
#endif
        free_sd_ctx(context_);
        context_ = nullptr;
        loaded_model_name_.clear();
        loaded_model_architecture_.clear();
    }
    
    // Initialize context parameters
    sd_ctx_params_t ctx_params;
    sd_ctx_params_init(&ctx_params);

    // Set model path based on type
    if (params.model_type == ModelType::Checkpoint) {
        ctx_params.model_path = model_info->full_path.c_str();
    } else if (params.model_type == ModelType::Diffusion) {
        ctx_params.diffusion_model_path = model_info->full_path.c_str();
    }
    
    // Set VAE if specified
    std::string vae_path;
    if (vae_info) {
        vae_path = vae_info->full_path;
        ctx_params.vae_path = vae_path.c_str();
        std::cout << "[ModelManager] VAE: " << *params.vae << std::endl;
    }
    
    // Set CLIP-L if specified
    std::string clip_l_path;
    if (clip_l_info) {
        clip_l_path = clip_l_info->full_path;
        ctx_params.clip_l_path = clip_l_path.c_str();
        std::cout << "[ModelManager] CLIP-L: " << *params.clip_l << std::endl;
    }
    
    // Set CLIP-G if specified  
    std::string clip_g_path;
    if (clip_g_info) {
        clip_g_path = clip_g_info->full_path;
        ctx_params.clip_g_path = clip_g_path.c_str();
        std::cout << "[ModelManager] CLIP-G: " << *params.clip_g << std::endl;
    }
    
    // Set T5 if specified
    std::string t5_path;
    if (t5_info) {
        t5_path = t5_info->full_path;
        ctx_params.t5xxl_path = t5_path.c_str();
        std::cout << "[ModelManager] T5: " << *params.t5xxl << std::endl;
    }
    
    // Set ControlNet if specified
    std::string controlnet_path;
    if (controlnet_info) {
        controlnet_path = controlnet_info->full_path;
        ctx_params.control_net_path = controlnet_path.c_str();
        std::cout << "[ModelManager] ControlNet: " << *params.controlnet << std::endl;
    }
    
    // Set LLM if specified
    std::string llm_path;
    if (llm_info) {
        llm_path = llm_info->full_path;
        ctx_params.llm_path = llm_path.c_str();
        std::cout << "[ModelManager] LLM: " << *params.llm << std::endl;
    }
    
    // Set LLM Vision if specified
    std::string llm_vision_path;
    if (llm_vision_info) {
        llm_vision_path = llm_vision_info->full_path;
        ctx_params.llm_vision_path = llm_vision_path.c_str();
        std::cout << "[ModelManager] LLM Vision: " << *params.llm_vision << std::endl;
    }

    // Set CLIP Vision if specified
    std::string clip_vision_path;
    if (clip_vision_info) {
        clip_vision_path = clip_vision_info->full_path;
        ctx_params.clip_vision_path = clip_vision_path.c_str();
        std::cout << "[ModelManager] CLIP Vision: " << *params.clip_vision << std::endl;
    }

    // Set TAESD if specified
    std::string taesd_path;
    if (taesd_info) {
        taesd_path = taesd_info->full_path;
        ctx_params.taesd_path = taesd_path.c_str();
        std::cout << "[ModelManager] TAESD: " << *params.taesd << std::endl;
    }

    // Set High-Noise Diffusion Model if specified
    std::string high_noise_diffusion_path;
    if (high_noise_diffusion_info) {
        high_noise_diffusion_path = high_noise_diffusion_info->full_path;
        ctx_params.high_noise_diffusion_model_path = high_noise_diffusion_path.c_str();
        std::cout << "[ModelManager] High-Noise Diffusion: " << *params.high_noise_diffusion_model << std::endl;
    }

    // Set Unconditional Diffusion Model if specified
    std::string uncond_diffusion_path;
    if (uncond_diffusion_info) {
        uncond_diffusion_path = uncond_diffusion_info->full_path;
        ctx_params.uncond_diffusion_model_path = uncond_diffusion_path.c_str();
        std::cout << "[ModelManager] Uncond Diffusion: " << *params.uncond_diffusion_model << std::endl;
    }

    // Set PhotoMaker if specified
    std::string photo_maker_path;
    if (photo_maker_info) {
        photo_maker_path = photo_maker_info->full_path;
        ctx_params.photo_maker_path = photo_maker_path.c_str();
        std::cout << "[ModelManager] PhotoMaker: " << *params.photo_maker << std::endl;
    }

    // Set PuLID-Flux weights if specified (leejet PR #1595).
    std::string pulid_weights_path;
    if (pulid_weights_info) {
        pulid_weights_path = pulid_weights_info->full_path;
        ctx_params.pulid_weights_path = pulid_weights_path.c_str();
        std::cout << "[ModelManager] PuLID weights: " << *params.pulid_weights << std::endl;
    }

    // LTXAV audio VAE + embeddings connectors. Audio VAE lives in the same
    // directory as regular VAEs; embeddings connectors live alongside text
    // encoders (T5 directory). Resolution is best-effort here — sd.cpp will
    // error out at load time if the file isn't loadable.
    std::string audio_vae_path;
    if (params.audio_vae) {
        auto audio_vae_info = get_model(*params.audio_vae, ModelType::VAE);
        if (audio_vae_info) {
            audio_vae_path = audio_vae_info->full_path;
            ctx_params.audio_vae_path = audio_vae_path.c_str();
            std::cout << "[ModelManager] Audio VAE: " << *params.audio_vae << std::endl;
        }
    }
    std::string embeddings_connectors_path;
    if (params.embeddings_connectors) {
        auto ec_info = get_model(*params.embeddings_connectors, ModelType::T5);
        if (ec_info) {
            embeddings_connectors_path = ec_info->full_path;
            ctx_params.embeddings_connectors_path = embeddings_connectors_path.c_str();
            std::cout << "[ModelManager] Embeddings connectors: " << *params.embeddings_connectors << std::endl;
        }
    }

    // Note: LoRA paths are now passed directly in generate_image params via sd_lora_t
    // The lora_model_dir parameter was removed in sd.cpp c3ad6a1

    // Set options
    ctx_params.n_threads = params.n_threads > 0 ? params.n_threads : sd_get_num_physical_cores();
    ctx_params.flash_attn = params.flash_attn;
    ctx_params.diffusion_flash_attn = params.diffusion_flash_attn;
    ctx_params.enable_mmap = params.enable_mmap;
    ctx_params.vae_conv_direct = params.vae_conv_direct;
    ctx_params.diffusion_conv_direct = params.diffusion_conv_direct;
    ctx_params.tae_preview_only = params.tae_preview_only;
    // flow_shift is per-generation (sd_sample_params_t.flow_shift), wired in
    // sd_wrapper.cpp's generate_txt2img / generate_img2img / generate_txt2vid.
    ctx_params.force_sdxl_vae_conv_scale = params.force_sdxl_vae_conv_scale;

    // Set weight type if specified
    if (!params.weight_type.empty()) {
        ctx_params.wtype = str_to_sd_type(params.weight_type.c_str());
        std::cout << "[ModelManager] Weight type: " << params.weight_type << std::endl;
    }

    // Set tensor type rules if specified
    if (!params.tensor_type_rules.empty()) {
        ctx_params.tensor_type_rules = params.tensor_type_rules.c_str();
        std::cout << "[ModelManager] Tensor type rules: " << params.tensor_type_rules << std::endl;
    }

    // Set RNG type
    ctx_params.rng_type = string_to_rng_type(params.rng_type);
    if (!params.sampler_rng_type.empty()) {
        ctx_params.sampler_rng_type = string_to_rng_type(params.sampler_rng_type);
    } else {
        ctx_params.sampler_rng_type = RNG_TYPE_COUNT;  // Use same as rng_type
    }

    // Set prediction type (PREDICTION_COUNT means auto-detect)
    ctx_params.prediction = string_to_prediction_type(params.prediction);

    // Set LoRA apply mode
    ctx_params.lora_apply_mode = string_to_lora_apply_mode(params.lora_apply_mode);

    // Chroma options
    ctx_params.chroma_use_dit_mask = params.chroma_use_dit_mask;
    ctx_params.chroma_use_t5_mask = params.chroma_use_t5_mask;
    ctx_params.chroma_t5_mask_pad = params.chroma_t5_mask_pad;

    // Qwen-Image: zero the conditional T branch when requested.
    ctx_params.qwen_image_zero_cond_t = params.qwen_image_zero_cond_t;

    // VAE format override. Default "auto" → SD_VAE_FORMAT_AUTO (-1) so sd.cpp
    // detects from the weights. Explicit values: "flux", "sd3", "flux2".
    if (params.vae_format == "flux") {
        ctx_params.vae_format = SD_VAE_FORMAT_FLUX;
    } else if (params.vae_format == "sd3") {
        ctx_params.vae_format = SD_VAE_FORMAT_SD3;
    } else if (params.vae_format == "flux2") {
        ctx_params.vae_format = SD_VAE_FORMAT_FLUX2;
    } else {
        ctx_params.vae_format = SD_VAE_FORMAT_AUTO;
    }

    // Tileable / seamless texture position embeddings (leejet PR #1627).
    ctx_params.circular_x = params.circular_x;
    ctx_params.circular_y = params.circular_y;

    // Backend routing. Pointers into sd_ctx_params_t must stay valid for the
    // duration of new_sd_ctx() — `params` outlives that call here, so passing
    // c_str() is safe. Empty strings → nullptr so sd.cpp falls back to its
    // built-in selection logic.
    ctx_params.backend = params.backend.empty() ? nullptr : params.backend.c_str();
    ctx_params.params_backend = params.params_backend.empty() ? nullptr : params.params_backend.c_str();
    // RPC distributed-backend node list (leejet PR #1629). Comma-separated
    // "host:port" pairs in sd.cpp's own format. Empty → nullptr → local.
    ctx_params.rpc_servers = params.rpc_servers.empty() ? nullptr : params.rpc_servers.c_str();

    // max_vram became a string in leejet PR #1660 (bb90bfa) so it can carry
    // backend-specific budgets like "cuda:8,cpu:0" alongside the legacy float
    // semantics. Restapi keeps the input contract as float (preserves
    // ModelLoadParams.max_vram and the WebUI numeric input) and formats it
    // here. The local string must outlive new_sd_ctx() — keep it function-
    // scoped, not block-scoped. Special values: 0.0 → nullptr (disabled,
    // matches the previous behavior); negative → "-N" auto-detect sentinel.
    std::string max_vram_str;
    if (params.max_vram != 0.0f) {
        std::ostringstream oss;
        oss << params.max_vram;
        max_vram_str = oss.str();
        ctx_params.max_vram = max_vram_str.c_str();
    } else {
        ctx_params.max_vram = nullptr;
    }

#if defined(SDCPP_EXPERIMENTAL_OFFLOAD) && !defined(SDCPP_UNIFIED_STREAMING)
    // ── feature/vram-offloading-v2 path ──────────────────────────────────
    // Dynamic tensor offloading options (legacy multi-mode API).
    ctx_params.offload_config.mode = string_to_offload_mode(params.offload_mode);
    ctx_params.offload_config.vram_estimation = string_to_vram_estimation(params.vram_estimation);
    ctx_params.offload_config.offload_cond_stage = params.offload_cond_stage;
    ctx_params.offload_config.offload_diffusion = params.offload_diffusion;
    ctx_params.offload_config.reload_cond_stage = params.reload_cond_stage;
    ctx_params.offload_config.reload_diffusion = params.reload_diffusion;
    ctx_params.offload_config.log_offload_events = params.log_offload_events;
    ctx_params.offload_config.min_offload_size = params.min_offload_size_mb * 1024 * 1024;  // Convert MB to bytes
    ctx_params.offload_config.target_free_vram = params.target_free_vram_mb * 1024 * 1024;  // Convert MB to bytes

    // Layer streaming options
    ctx_params.offload_config.streaming_prefetch_layers = params.streaming_prefetch_layers;
    ctx_params.offload_config.streaming_keep_layers_behind = params.streaming_keep_layers_behind;
    ctx_params.offload_config.streaming_min_free_vram = params.streaming_min_free_vram_mb * 1024 * 1024;  // Convert MB to bytes
#else
    // ── feature/unified-streaming path ───────────────────────────────────
    // Single bool toggles the new residency+async-prefetch path. Requires
    // max_vram > 0 to do anything — sd.cpp logs a notice and silently no-ops
    // if max_vram is 0.
    ctx_params.stream_layers = params.stream_layers;
#endif
    ctx_params.eager_load = params.eager_load;

    std::cout << "[ModelManager] Loading model: " << params.model_name << std::endl;
    std::cout << "[ModelManager] Using " << ctx_params.n_threads << " threads" << std::endl;
    std::cout << "[ModelManager] Options: "
              << "vae_conv_direct=" << ctx_params.vae_conv_direct
              << ", diffusion_conv_direct=" << ctx_params.diffusion_conv_direct
              << ", flash_attn=" << ctx_params.flash_attn
              << ", tae_preview_only=" << ctx_params.tae_preview_only
              << ", rng=" << params.rng_type
              << ", lora_mode=" << params.lora_apply_mode
#if defined(SDCPP_EXPERIMENTAL_OFFLOAD) && !defined(SDCPP_UNIFIED_STREAMING)
              << ", offload_mode=" << params.offload_mode
#else
              << ", stream_layers=" << (params.stream_layers ? "true" : "false")
#endif
              << std::endl;

    // Pre-flight: verify GPU runtime is usable before we hand control to
    // sd.cpp/ggml, which would SIGABRT us on a driver/lib mismatch via
    // GGML_ABORT and freeze the whole service while apport runs.
    {
        std::string gpu_err;
        if (!verify_gpu_runtime(gpu_err)) {
            last_load_error_ = "GPU runtime not ready: " + gpu_err;
            std::cerr << "[ModelManager] " << last_load_error_ << std::endl;
            return false;
        }
    }

    // Set up progress callback for model loading
    g_loading_model_manager = this;
    sd_set_progress_callback(model_loading_progress_callback, nullptr);

    // Create context
    context_ = new_sd_ctx(&ctx_params);

    // Clear progress callback after loading
    sd_set_progress_callback(nullptr, nullptr);
    g_loading_model_manager = nullptr;

    // Validate context integrity.
    // new_sd_ctx should return nullptr on failure, but in edge cases (CUDA OOM
    // during multithreaded tensor loading) it may return a partially initialized
    // context that would crash during generation. Validate with a probe call.
    if (context_ != nullptr) {
        const char* probe = sd_get_model_version_name(context_);
        if (probe == nullptr) {
            std::cerr << "[ModelManager] Context probe failed - freeing corrupted context" << std::endl;
            free_sd_ctx(context_);
            context_ = nullptr;
        }
    }

    if (context_ == nullptr) {
        // sd.cpp returns nullptr for every failure mode (OOM, missing tensor,
        // metadata validation, backend init, etc.) and the only signal of
        // WHY is the SD_LOG_ERROR stream captured by main.cpp's log callback.
        // Pull what was logged for THIS load attempt and infer a reason
        // category — falling back to OOM only when nothing was captured (which
        // is the genuine signature of new_sd_ctx erroring out before sd.cpp
        // got a chance to log anything).
        std::string sd_errors = get_sd_error();
        std::string reason;
        if (sd_errors.empty()) {
            // No captured logs → most likely a silent OOM during ggml_init /
            // tensor allocation. The hint about quantized models / offloading
            // is the actionable advice for that case.
            reason = "insufficient memory — try a quantized model, lower max_vram, "
                     "or enable params_backend=*=cpu + stream_layers for offloading";
        } else {
            // Classify the captured errors into a one-line top-level reason,
            // then append the raw log lines so the WebUI / queue surface
            // exactly what sd.cpp said. Cheap substring checks — these are
            // log lines we control upstream of, no regex needed.
            auto contains = [&sd_errors](const char* needle) {
                return sd_errors.find(needle) != std::string::npos;
            };
            if (contains("not in model metadata") || contains("model metadata validation failed")) {
                reason = "model metadata mismatch — sd.cpp expected tensor names "
                         "that aren't in the file (likely diffusers/LDM naming "
                         "convention mismatch, missing component file, or "
                         "architecture not supported by this build)";
            } else if (contains("out of memory") || contains("OOM") ||
                       contains("CUDA error") || contains("ggml_cuda_compute_forward")) {
                reason = "insufficient memory — try a quantized model, lower max_vram, "
                         "or enable params_backend=*=cpu + stream_layers for offloading";
            } else if (contains("unknown") && contains("architecture")) {
                reason = "model architecture not recognized by this build";
            } else if (contains("no such file") || contains("does not exist") ||
                       contains("failed to open")) {
                reason = "file access error";
            } else {
                reason = "load failed";
            }
            // Append the raw sd.cpp error lines (capped to ~512 chars so a
            // 100-line tensor-missing dump doesn't make the toast unreadable).
            constexpr size_t MAX_RAW = 512;
            std::string raw = sd_errors;
            if (raw.size() > MAX_RAW) {
                raw = raw.substr(0, MAX_RAW) + "… (truncated; full log in journal)";
            }
            reason += ". sd.cpp: " + raw;
        }
        std::string error_msg = "Failed to load model: " + params.model_name + " (" + reason + ")";
        last_load_error_ = error_msg;
        loaded_model_name_.clear();
        loaded_model_architecture_.clear();
        loaded_model_type_ = ModelType::Checkpoint;
        loaded_vae_.clear();
        loaded_clip_l_.clear();
        loaded_clip_g_.clear();
        loaded_t5_.clear();
        loaded_controlnet_.clear();
        loaded_llm_.clear();
        loaded_llm_vision_.clear();
        clear_loading();

        if (auto* ws = get_websocket_server()) {
            ws->broadcast(WSEventType::ModelLoadFailed, {
                {"model_name", params.model_name},
                {"error", error_msg}
            });
        }

        throw std::runtime_error(error_msg);
    }
    
    loaded_model_name_ = params.model_name;
    loaded_model_type_ = params.model_type;

    // Cache the model architecture name (e.g., "Z-Image", "Flux", "SDXL")
    const char* arch_name = sd_get_model_version_name(context_);
    loaded_model_architecture_ = arch_name ? arch_name : "Unknown";

    // Track loaded component models
    loaded_vae_ = params.vae.value_or("");
    loaded_clip_l_ = params.clip_l.value_or("");
    loaded_clip_g_ = params.clip_g.value_or("");
    loaded_t5_ = params.t5xxl.value_or("");
    loaded_controlnet_ = params.controlnet.value_or("");
    loaded_llm_ = params.llm.value_or("");
    loaded_llm_vision_ = params.llm_vision.value_or("");

    // Store the load options for later retrieval (for queue job reload)
    loaded_options_ = nlohmann::json::object();
    loaded_options_["n_threads"] = params.n_threads;
    loaded_options_["flash_attn"] = params.flash_attn;
    loaded_options_["diffusion_flash_attn"] = params.diffusion_flash_attn;
    loaded_options_["enable_mmap"] = params.enable_mmap;
    loaded_options_["vae_conv_direct"] = params.vae_conv_direct;
    loaded_options_["diffusion_conv_direct"] = params.diffusion_conv_direct;
    loaded_options_["tae_preview_only"] = params.tae_preview_only;
    // Always emit max_vram so 0 (= disabled) round-trips correctly to
    // the WebUI Edit form — otherwise a user who enabled it then unset
    // it back to 0 wouldn't see the flag clear in the restored state.
    loaded_options_["max_vram"] = params.max_vram;
    if (!params.weight_type.empty()) {
        loaded_options_["weight_type"] = params.weight_type;
    }
    if (!params.tensor_type_rules.empty()) {
        loaded_options_["tensor_type_rules"] = params.tensor_type_rules;
    }
    loaded_options_["rng_type"] = params.rng_type;
    if (!params.sampler_rng_type.empty()) {
        loaded_options_["sampler_rng_type"] = params.sampler_rng_type;
    }
    if (!params.prediction.empty()) {
        loaded_options_["prediction"] = params.prediction;
    }
    loaded_options_["lora_apply_mode"] = params.lora_apply_mode;
    // Backend placement strings — these are how upstream now expresses
    // per-component CPU offload (e.g. params_backend="*=cpu" replaces the
    // old offload_to_cpu bool). Echoing them is what makes /queue's
    // "Reload Job" actually restore the offload setup the job ran under —
    // without this the WebUI reload picks them up as empty strings and
    // the model loads with GPU placement only, OOM'ing on anything that
    // needed CPU-side params.
    loaded_options_["backend"] = params.backend;
    loaded_options_["params_backend"] = params.params_backend;
    loaded_options_["rpc_servers"] = params.rpc_servers;
    loaded_options_["force_sdxl_vae_conv_scale"] = params.force_sdxl_vae_conv_scale;
    loaded_options_["chroma_use_dit_mask"] = params.chroma_use_dit_mask;
    loaded_options_["chroma_use_t5_mask"] = params.chroma_use_t5_mask;
    loaded_options_["chroma_t5_mask_pad"] = params.chroma_t5_mask_pad;
    loaded_options_["qwen_image_zero_cond_t"] = params.qwen_image_zero_cond_t;
    loaded_options_["vae_format"] = params.vae_format;
    loaded_options_["circular_x"] = params.circular_x;
    loaded_options_["circular_y"] = params.circular_y;

#if defined(SDCPP_EXPERIMENTAL_OFFLOAD) && !defined(SDCPP_UNIFIED_STREAMING)
    // ── feature/vram-offloading-v2 echoes back the full legacy offload set ──
    loaded_options_["offload_mode"] = params.offload_mode;
    loaded_options_["vram_estimation"] = params.vram_estimation;
    loaded_options_["offload_cond_stage"] = params.offload_cond_stage;
    loaded_options_["offload_diffusion"] = params.offload_diffusion;
    loaded_options_["reload_cond_stage"] = params.reload_cond_stage;
    loaded_options_["reload_diffusion"] = params.reload_diffusion;
    loaded_options_["log_offload_events"] = params.log_offload_events;
    loaded_options_["min_offload_size_mb"] = params.min_offload_size_mb;
    loaded_options_["target_free_vram_mb"] = params.target_free_vram_mb;

    // Layer streaming options
    loaded_options_["streaming_prefetch_layers"] = params.streaming_prefetch_layers;
    loaded_options_["streaming_keep_layers_behind"] = params.streaming_keep_layers_behind;
    loaded_options_["streaming_min_free_vram_mb"] = params.streaming_min_free_vram_mb;
#else
    // ── feature/unified-streaming echoes back the single new field ──────────
    loaded_options_["stream_layers"] = params.stream_layers;
#endif
    loaded_options_["eager_load"] = params.eager_load;

    // Set atomic flag for lock-free checks
    model_loaded_ = true;

    // Clear loading state on success
    clear_loading();

    // Persist load identity to disk so that on a server restart we can
    // auto-reload the same model. Without this, queued jobs picked up from
    // queue_state.json fail with "No model loaded" because the in-memory
    // model state is lost. Best-effort: write failures don't fail the load.
    try {
        nlohmann::json persisted = nlohmann::json::object();
        persisted["model_name"] = params.model_name;
        persisted["model_type"] = model_type_to_string(params.model_type);
        if (params.vae)         persisted["vae"]         = *params.vae;
        if (params.clip_l)      persisted["clip_l"]      = *params.clip_l;
        if (params.clip_g)      persisted["clip_g"]      = *params.clip_g;
        if (params.clip_vision) persisted["clip_vision"] = *params.clip_vision;
        if (params.t5xxl)       persisted["t5xxl"]       = *params.t5xxl;
        if (params.controlnet)  persisted["controlnet"]  = *params.controlnet;
        if (params.llm)         persisted["llm"]         = *params.llm;
        if (params.llm_vision)  persisted["llm_vision"]  = *params.llm_vision;
        if (params.taesd)       persisted["taesd"]       = *params.taesd;
        if (params.high_noise_diffusion_model)
            persisted["high_noise_diffusion_model"] = *params.high_noise_diffusion_model;
        if (params.uncond_diffusion_model)
            persisted["uncond_diffusion_model"] = *params.uncond_diffusion_model;
        if (params.photo_maker)            persisted["photo_maker"]            = *params.photo_maker;
        if (params.pulid_weights)          persisted["pulid_weights"]          = *params.pulid_weights;
        if (params.audio_vae)              persisted["audio_vae"]              = *params.audio_vae;
        if (params.embeddings_connectors)  persisted["embeddings_connectors"]  = *params.embeddings_connectors;
        // loaded_options_ already has the full set of load options in the
        // shape ModelLoadParams::from_json expects under the "options" key.
        persisted["options"] = loaded_options_;

        fs::path persist_path = fs::path(config_.paths.output) / "last_loaded_model.json";
        std::ofstream f(persist_path);
        if (f) {
            f << persisted.dump(2);
            std::cout << "[ModelManager] Load identity persisted to " << persist_path << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "[ModelManager] Warning: failed to persist load identity: "
                  << e.what() << std::endl;
    }

    std::cout << "[ModelManager] Model architecture: " << loaded_model_architecture_ << std::endl;
    std::cout << "[ModelManager] Model loaded successfully" << std::endl;

    // Broadcast model loaded via WebSocket
    if (auto* ws = get_websocket_server()) {
        ws->broadcast(WSEventType::ModelLoaded, {
            {"model_name", loaded_model_name_},
            {"model_type", model_type_to_string(loaded_model_type_)},
            {"model_architecture", loaded_model_architecture_}
        });
    }

    return true;
}

bool ModelManager::try_auto_reload_from_disk() {
    fs::path persist_path = fs::path(config_.paths.output) / "last_loaded_model.json";
    if (!fs::exists(persist_path)) {
        return false;  // Nothing to restore — first run, or user explicitly unloaded
    }

    nlohmann::json j;
    try {
        std::ifstream f(persist_path);
        if (!f) return false;
        f >> j;
    } catch (const std::exception& e) {
        std::cerr << "[ModelManager] Could not read " << persist_path << ": "
                  << e.what() << std::endl;
        return false;
    }

    std::cout << "[ModelManager] Found persisted load state: "
              << j.value("model_name", "<unknown>") << " ("
              << j.value("model_type", "<unknown>") << "). "
              << "Attempting auto-reload after restart." << std::endl;

    ModelLoadParams params;
    try {
        params = ModelLoadParams::from_json(j);
    } catch (const std::exception& e) {
        std::cerr << "[ModelManager] Persisted load state is invalid: "
                  << e.what() << ". Removing the file." << std::endl;
        std::error_code ec;
        fs::remove(persist_path, ec);
        return false;
    }

    bool ok = load_model(params);
    if (!ok) {
        std::cerr << "[ModelManager] Auto-reload failed: " << last_load_error_
                  << std::endl
                  << "[ModelManager] Queued jobs will fail with 'No model "
                  << "loaded' until you load one manually." << std::endl;
        return false;
    }

    std::cout << "[ModelManager] Auto-reload succeeded." << std::endl;
    return true;
}

void ModelManager::unload_model() {
    std::lock_guard<std::mutex> lock(context_mutex_);

    // Clear error state when unloading
    last_load_error_.clear();

    // Remove the persisted load identity so the next server restart doesn't
    // auto-reload a model the user explicitly unloaded.
    {
        std::error_code ec;
        fs::remove(fs::path(config_.paths.output) / "last_loaded_model.json", ec);
    }

    if (context_ != nullptr) {
        std::string model_name = loaded_model_name_;  // Capture before clearing
        std::cout << "[ModelManager] Unloading model: " << model_name << std::endl;

        // Clear atomic flag first
        model_loaded_ = false;

#if defined(SDCPP_EXPERIMENTAL_OFFLOAD) && !defined(SDCPP_UNIFIED_STREAMING)
        // Free all GPU resources before unloading to prevent memory leaks
        // (matches the cleanup pattern in load_model when replacing a model).
        // Unified-streaming branch doesn't expose this helper — free_sd_ctx
        // handles the cleanup itself there.
        sd_free_gpu_resources(context_);
#endif
        free_sd_ctx(context_);
        context_ = nullptr;
        loaded_model_name_.clear();
        loaded_model_architecture_.clear();

        // Clear all loaded component tracking
        loaded_vae_.clear();
        loaded_clip_l_.clear();
        loaded_clip_g_.clear();
        loaded_t5_.clear();
        loaded_controlnet_.clear();
        loaded_llm_.clear();
        loaded_llm_vision_.clear();

        // Clear load options
        loaded_options_.clear();

        // Broadcast model unloaded via WebSocket
        if (auto* ws = get_websocket_server()) {
            ws->broadcast(WSEventType::ModelUnloaded, {
                {"model_name", model_name}
            });
        }
    }
}

bool ModelManager::is_model_loaded() const {
    // Use atomic flag for lock-free check - this allows HTTP requests
    // to check model status without blocking on ongoing generation
    return model_loaded_.load();
}

std::string ModelManager::get_loaded_model_name() const {
    std::lock_guard<std::mutex> lock(context_mutex_);
    return loaded_model_name_;
}

std::string ModelManager::get_loaded_model_architecture() const {
    std::lock_guard<std::mutex> lock(context_mutex_);
    return loaded_model_architecture_;
}

std::string ModelManager::compute_model_hash(const std::string& name, ModelType type) {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    
    auto type_it = models_.find(type);
    if (type_it == models_.end()) {
        throw std::runtime_error("Model type not found");
    }
    
    auto it = type_it->second.find(name);
    if (it == type_it->second.end()) {
        throw std::runtime_error("Model not found: " + name);
    }
    
    // Return cached hash if available
    if (!it->second.hash.empty()) {
        return it->second.hash;
    }
    
    // Compute hash
    std::cout << "[ModelManager] Computing hash for: " << name << std::endl;
    std::string hash = utils::compute_sha256(it->second.full_path);
    
    // Cache it
    it->second.hash = hash;
    
    return hash;
}

sd_ctx_t* ModelManager::get_context() {
    return context_;
}

std::mutex& ModelManager::get_context_mutex() {
    return context_mutex_;
}

std::string ModelManager::get_lora_dir() const {
    return config_.paths.lora;
}

nlohmann::json ModelManager::get_loaded_models_info() const {
    // Use atomic flags for lock-free check - don't hold context_mutex_
    // to avoid blocking during generation
    nlohmann::json result;

    bool is_loaded = model_loaded_.load();
    bool is_loading = model_loading_.load();

    result["model_loaded"] = is_loaded;
    result["model_loading"] = is_loading;
    result["loading_model_name"] = is_loading && !loading_model_name_.empty()
        ? nlohmann::json(loading_model_name_) : nullptr;
    result["last_error"] = !last_load_error_.empty()
        ? nlohmann::json(last_load_error_) : nullptr;

    // Include loading progress (raw values from sd.cpp callback)
    if (is_loading) {
        result["loading_step"] = loading_step_.load();
        result["loading_total_steps"] = loading_total_steps_.load();
    } else {
        result["loading_step"] = nullptr;
        result["loading_total_steps"] = nullptr;
    }

    // Add upscaler info (independent of main model)
    {
        std::lock_guard<std::mutex> upscaler_lock(upscaler_mutex_);
        result["upscaler_loaded"] = (upscaler_context_ != nullptr);
        result["upscaler_name"] = loaded_upscaler_name_.empty() ? nullptr : nlohmann::json(loaded_upscaler_name_);
    }

    if (!is_loaded) {
        result["model_name"] = nullptr;
        result["model_type"] = nullptr;
        result["model_architecture"] = nullptr;
        result["loaded_components"] = nlohmann::json::object();
        return result;
    }

    // These cached values are safe to read without lock since they're only
    // modified when loading/unloading (which sets model_loaded_ flag)
    result["model_name"] = loaded_model_name_;
    result["model_type"] = model_type_to_string(loaded_model_type_);
    result["model_architecture"] = loaded_model_architecture_.empty() ? nullptr : nlohmann::json(loaded_model_architecture_);
    
    // Build loaded components object
    nlohmann::json components = nlohmann::json::object();
    
    if (!loaded_vae_.empty()) {
        components["vae"] = loaded_vae_;
    }
    if (!loaded_clip_l_.empty()) {
        components["clip_l"] = loaded_clip_l_;
    }
    if (!loaded_clip_g_.empty()) {
        components["clip_g"] = loaded_clip_g_;
    }
    if (!loaded_t5_.empty()) {
        components["t5xxl"] = loaded_t5_;
    }
    if (!loaded_controlnet_.empty()) {
        components["controlnet"] = loaded_controlnet_;
    }
    if (!loaded_llm_.empty()) {
        components["llm"] = loaded_llm_;
    }
    if (!loaded_llm_vision_.empty()) {
        components["llm_vision"] = loaded_llm_vision_;
    }
    
    result["loaded_components"] = components;

    // Include the load options used when loading the model
    if (!loaded_options_.empty()) {
        result["load_options"] = loaded_options_;
    }

    return result;
}

// ==================== Upscaler Management ====================

bool ModelManager::load_upscaler(const std::string& model_name, int n_threads, int tile_size) {
    std::lock_guard<std::mutex> lock(upscaler_mutex_);
    
    // Check if already loaded
    if (upscaler_context_ != nullptr && loaded_upscaler_name_ == model_name) {
        std::cout << "[ModelManager] Upscaler already loaded: " << model_name << std::endl;
        return true;
    }
    
    // Validate model exists
    auto model_info = get_model(model_name, ModelType::ESRGAN);
    if (!model_info) {
        std::string base_path = get_base_path(ModelType::ESRGAN);
        throw std::runtime_error("ESRGAN model not found: '" + model_name + 
                                "' (searched in: " + (base_path.empty() ? "<not configured>" : base_path) + ")");
    }
    
    // Unload existing upscaler
    if (upscaler_context_ != nullptr) {
        std::cout << "[ModelManager] Unloading upscaler: " << loaded_upscaler_name_ << std::endl;
        free_upscaler_ctx(upscaler_context_);
        upscaler_context_ = nullptr;
        loaded_upscaler_name_.clear();
    }
    
    // Determine thread count
    int threads = n_threads > 0 ? n_threads : sd_get_num_physical_cores();
    
    std::cout << "[ModelManager] Loading upscaler: " << model_name << std::endl;
    std::cout << "[ModelManager] Using " << threads << " threads, tile_size=" << tile_size << std::endl;
    
    // Load upscaler. New trailing args in sd.cpp (post-2026-05-16 master):
    // backend / params_backend let callers route the upscaler to a specific
    // backend by name. Empty/null = let sd.cpp pick (matches old behavior).
    upscaler_context_ = new_upscaler_ctx(
        model_info->full_path.c_str(),
        false,      // direct
        threads,
        tile_size,
        nullptr,    // backend
        nullptr     // params_backend
    );
    
    if (upscaler_context_ == nullptr) {
        throw std::runtime_error("Failed to load upscaler: " + model_name);
    }
    
    loaded_upscaler_name_ = model_name;
    
    // Set atomic flag for lock-free checks
    upscaler_loaded_ = true;
    
    int scale = ::get_upscale_factor(upscaler_context_);
    std::cout << "[ModelManager] Upscaler loaded successfully (scale: " << scale << "x)" << std::endl;

    // Broadcast upscaler loaded via WebSocket
    if (auto* ws = get_websocket_server()) {
        ws->broadcast(WSEventType::UpscalerLoaded, {
            {"model_name", model_name},
            {"upscale_factor", scale}
        });
    }

    return true;
}

void ModelManager::unload_upscaler() {
    std::lock_guard<std::mutex> lock(upscaler_mutex_);

    if (upscaler_context_ != nullptr) {
        std::string upscaler_name = loaded_upscaler_name_;  // Capture before clearing
        std::cout << "[ModelManager] Unloading upscaler: " << upscaler_name << std::endl;

        // Clear atomic flag first
        upscaler_loaded_ = false;

        free_upscaler_ctx(upscaler_context_);
        upscaler_context_ = nullptr;
        loaded_upscaler_name_.clear();

        // Broadcast upscaler unloaded via WebSocket
        if (auto* ws = get_websocket_server()) {
            ws->broadcast(WSEventType::UpscalerUnloaded, {
                {"model_name", upscaler_name}
            });
        }
    }
}

bool ModelManager::is_upscaler_loaded() const {
    // Use atomic flag for lock-free check - this allows HTTP requests
    // to check upscaler status without blocking on ongoing upscaling
    return upscaler_loaded_.load();
}

upscaler_ctx_t* ModelManager::get_upscaler_context() {
    return upscaler_context_;
}

std::mutex& ModelManager::get_upscaler_mutex() {
    return upscaler_mutex_;
}

std::string ModelManager::get_loaded_upscaler_name() const {
    std::lock_guard<std::mutex> lock(upscaler_mutex_);
    return loaded_upscaler_name_;
}

int ModelManager::get_upscale_factor() const {
    std::lock_guard<std::mutex> lock(upscaler_mutex_);
    if (upscaler_context_ == nullptr) {
        return 1;
    }
    return ::get_upscale_factor(upscaler_context_);
}

void ModelManager::update_loading_progress(int step, int total_steps) {
    loading_step_ = step;
    loading_total_steps_ = total_steps;

    // Broadcast loading progress via WebSocket
    if (auto* ws = get_websocket_server()) {
        ws->broadcast(WSEventType::ModelLoadingProgress, {
            {"model_name", loading_model_name_},
            {"step", step},
            {"total_steps", total_steps}
        });
    }
}

nlohmann::json ModelManager::get_paths_config() const {
    return {
        {"checkpoints", config_.paths.checkpoints},
        {"diffusion_models", config_.paths.diffusion_models},
        {"vae", config_.paths.vae},
        {"lora", config_.paths.lora},
        {"clip", config_.paths.clip},
        {"t5", config_.paths.t5},
        {"embeddings", config_.paths.embeddings},
        {"controlnet", config_.paths.controlnet},
        {"llm", config_.paths.llm},
        {"esrgan", config_.paths.esrgan},
        {"taesd", config_.paths.taesd}
    };
}

} // namespace sdcpp
