#include "model_manager.hpp"
#include "websocket_server.hpp"
#include "utils.hpp"

#include <iostream>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <cmath>

#include "stable-diffusion.h"

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
    if (str == "flux2_flow") return FLUX2_FLOW_PRED;
    return PREDICTION_COUNT;  // auto-detect for unknown
}

static lora_apply_mode_t string_to_lora_apply_mode(const std::string& str) {
    if (str == "immediately") return LORA_APPLY_IMMEDIATELY;
    if (str == "at_runtime" || str == "runtime") return LORA_APPLY_AT_RUNTIME;
    return LORA_APPLY_AUTO;  // default
}

#ifdef SDCPP_EXPERIMENTAL_OFFLOAD
static sd_offload_mode_t string_to_offload_mode(const std::string& str) {
    if (str == "cond_only") return SD_OFFLOAD_COND_ONLY;
    if (str == "cond_diffusion") return SD_OFFLOAD_COND_DIFFUSION;
    if (str == "aggressive") return SD_OFFLOAD_AGGRESSIVE;
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

ModelLoadParams ModelLoadParams::from_json(const nlohmann::json& j) {
    ModelLoadParams params;

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
    if (j.contains("photo_maker") && !j["photo_maker"].is_null()) {
        params.photo_maker = j["photo_maker"].get<std::string>();
    }

    if (j.contains("options")) {
        auto& opts = j["options"];
        params.n_threads = opts.value("n_threads", -1);
        params.keep_clip_on_cpu = opts.value("keep_clip_on_cpu", true);
        params.keep_vae_on_cpu = opts.value("keep_vae_on_cpu", false);
        params.keep_controlnet_on_cpu = opts.value("keep_controlnet_on_cpu", false);
        params.flash_attn = opts.value("flash_attn", true);
        params.offload_to_cpu = opts.value("offload_to_cpu", false);
        params.enable_mmap = opts.value("enable_mmap", true);
        params.vae_decode_only = opts.value("vae_decode_only", true);
        params.vae_conv_direct = opts.value("vae_conv_direct", false);
        params.diffusion_conv_direct = opts.value("diffusion_conv_direct", false);
        params.tae_preview_only = opts.value("tae_preview_only", false);
        params.free_params_immediately = opts.value("free_params_immediately", false);
        // INFINITY means auto-detect based on model type
        params.flow_shift = opts.contains("flow_shift") ? opts.value("flow_shift", INFINITY) : INFINITY;
        params.weight_type = opts.value("weight_type", "");
        params.tensor_type_rules = opts.value("tensor_type_rules", "");

        // RNG options
        params.rng_type = opts.value("rng_type", "cuda");
        params.sampler_rng_type = opts.value("sampler_rng_type", "");

        // Prediction type override (empty = auto)
        params.prediction = opts.value("prediction", "");

        // LoRA apply mode - default to runtime to avoid caching issues with incompatible LoRAs
        // In "auto" mode, sd.cpp caches LoRA state even if the LoRA didn't match the model,
        // causing warnings on subsequent generations without LoRAs.
        params.lora_apply_mode = opts.value("lora_apply_mode", "runtime");

        // VAE tiling options
        params.vae_tiling = opts.value("vae_tiling", false);
        params.vae_tile_size_x = opts.value("vae_tile_size_x", 0);
        params.vae_tile_size_y = opts.value("vae_tile_size_y", 0);
        params.vae_tile_overlap = opts.value("vae_tile_overlap", 0.5f);
        params.force_sdxl_vae_conv_scale = opts.value("force_sdxl_vae_conv_scale", false);

        // Chroma options
        params.chroma_use_dit_mask = opts.value("chroma_use_dit_mask", true);
        params.chroma_use_t5_mask = opts.value("chroma_use_t5_mask", false);
        params.chroma_t5_mask_pad = opts.value("chroma_t5_mask_pad", 1);

#ifdef SDCPP_EXPERIMENTAL_OFFLOAD
        // Dynamic tensor offloading options (experimental)
        params.offload_mode = opts.value("offload_mode", "none");
        params.vram_estimation = opts.value("vram_estimation", "dryrun");
        params.offload_cond_stage = opts.value("offload_cond_stage", true);
        params.offload_diffusion = opts.value("offload_diffusion", false);
        params.reload_cond_stage = opts.value("reload_cond_stage", true);
        params.reload_diffusion = opts.value("reload_diffusion", true);
        params.log_offload_events = opts.value("log_offload_events", true);
        params.min_offload_size_mb = opts.value("min_offload_size_mb", 0);
#endif
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
        
        ModelInfo info;
        info.name = rel_path;
        info.full_path = full_path;
        info.type = type;
        info.file_extension = utils::get_file_extension(full_path);
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
#ifdef SDCPP_EXPERIMENTAL_OFFLOAD
        // Free all GPU resources before unloading to prevent memory leaks
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

    // Set PhotoMaker if specified
    std::string photo_maker_path;
    if (photo_maker_info) {
        photo_maker_path = photo_maker_info->full_path;
        ctx_params.photo_maker_path = photo_maker_path.c_str();
        std::cout << "[ModelManager] PhotoMaker: " << *params.photo_maker << std::endl;
    }

    // Note: LoRA paths are now passed directly in generate_image params via sd_lora_t
    // The lora_model_dir parameter was removed in sd.cpp c3ad6a1

    // Set options
    ctx_params.n_threads = params.n_threads > 0 ? params.n_threads : sd_get_num_physical_cores();
    ctx_params.keep_clip_on_cpu = params.keep_clip_on_cpu;
    ctx_params.keep_vae_on_cpu = params.keep_vae_on_cpu;
    ctx_params.keep_control_net_on_cpu = params.keep_controlnet_on_cpu;
    ctx_params.flash_attn = params.flash_attn;  // Enable flash attention for all models (CLIP, VAE, diffusion)
    ctx_params.offload_params_to_cpu = params.offload_to_cpu;
    ctx_params.enable_mmap = params.enable_mmap;
    ctx_params.vae_decode_only = params.vae_decode_only;
    ctx_params.vae_conv_direct = params.vae_conv_direct;
    ctx_params.diffusion_conv_direct = params.diffusion_conv_direct;
    ctx_params.tae_preview_only = params.tae_preview_only;
    ctx_params.free_params_immediately = params.free_params_immediately;
    ctx_params.flow_shift = params.flow_shift;
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

#ifdef SDCPP_EXPERIMENTAL_OFFLOAD
    // Dynamic tensor offloading options (experimental)
    ctx_params.offload_config.mode = string_to_offload_mode(params.offload_mode);
    ctx_params.offload_config.vram_estimation = string_to_vram_estimation(params.vram_estimation);
    ctx_params.offload_config.offload_cond_stage = params.offload_cond_stage;
    ctx_params.offload_config.offload_diffusion = params.offload_diffusion;
    ctx_params.offload_config.reload_cond_stage = params.reload_cond_stage;
    ctx_params.offload_config.reload_diffusion = params.reload_diffusion;
    ctx_params.offload_config.log_offload_events = params.log_offload_events;
    ctx_params.offload_config.min_offload_size = params.min_offload_size_mb * 1024 * 1024;  // Convert MB to bytes
#endif

    std::cout << "[ModelManager] Loading model: " << params.model_name << std::endl;
    std::cout << "[ModelManager] Using " << ctx_params.n_threads << " threads" << std::endl;
    std::cout << "[ModelManager] Options: "
              << "keep_clip_on_cpu=" << ctx_params.keep_clip_on_cpu
              << ", keep_vae_on_cpu=" << ctx_params.keep_vae_on_cpu
              << ", keep_controlnet_on_cpu=" << ctx_params.keep_control_net_on_cpu
              << ", vae_decode_only=" << ctx_params.vae_decode_only
              << ", vae_conv_direct=" << ctx_params.vae_conv_direct
              << ", diffusion_conv_direct=" << ctx_params.diffusion_conv_direct
              << ", flash_attn=" << ctx_params.flash_attn
              << ", offload_to_cpu=" << ctx_params.offload_params_to_cpu
              << ", free_params_immediately=" << ctx_params.free_params_immediately
              << ", tae_preview_only=" << ctx_params.tae_preview_only
              << ", flow_shift=" << (std::isinf(ctx_params.flow_shift) ? "auto" : std::to_string(ctx_params.flow_shift))
              << ", rng=" << params.rng_type
              << ", lora_mode=" << params.lora_apply_mode
#ifdef SDCPP_EXPERIMENTAL_OFFLOAD
              << ", offload_mode=" << params.offload_mode
#endif
              << std::endl;

    // Set up progress callback for model loading
    g_loading_model_manager = this;
    sd_set_progress_callback(model_loading_progress_callback, nullptr);

    // Create context
    context_ = new_sd_ctx(&ctx_params);

    // Clear progress callback after loading
    sd_set_progress_callback(nullptr, nullptr);
    g_loading_model_manager = nullptr;

    if (context_ == nullptr) {
        std::string error_msg = "Failed to load model: " + params.model_name;
        last_load_error_ = error_msg;
        // Clear stale model info on failure
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

        // Broadcast model load failure via WebSocket
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
    loaded_options_["keep_clip_on_cpu"] = params.keep_clip_on_cpu;
    loaded_options_["keep_vae_on_cpu"] = params.keep_vae_on_cpu;
    loaded_options_["keep_controlnet_on_cpu"] = params.keep_controlnet_on_cpu;
    loaded_options_["flash_attn"] = params.flash_attn;
    loaded_options_["offload_to_cpu"] = params.offload_to_cpu;
    loaded_options_["enable_mmap"] = params.enable_mmap;
    loaded_options_["vae_decode_only"] = params.vae_decode_only;
    loaded_options_["vae_conv_direct"] = params.vae_conv_direct;
    loaded_options_["diffusion_conv_direct"] = params.diffusion_conv_direct;
    loaded_options_["tae_preview_only"] = params.tae_preview_only;
    loaded_options_["free_params_immediately"] = params.free_params_immediately;
    if (!std::isinf(params.flow_shift)) {
        loaded_options_["flow_shift"] = params.flow_shift;
    }
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
    loaded_options_["vae_tiling"] = params.vae_tiling;
    loaded_options_["vae_tile_size_x"] = params.vae_tile_size_x;
    loaded_options_["vae_tile_size_y"] = params.vae_tile_size_y;
    loaded_options_["vae_tile_overlap"] = params.vae_tile_overlap;
    loaded_options_["chroma_use_dit_mask"] = params.chroma_use_dit_mask;
    loaded_options_["chroma_use_t5_mask"] = params.chroma_use_t5_mask;
    loaded_options_["chroma_t5_mask_pad"] = params.chroma_t5_mask_pad;

#ifdef SDCPP_EXPERIMENTAL_OFFLOAD
    // Dynamic tensor offloading options (experimental)
    loaded_options_["offload_mode"] = params.offload_mode;
    loaded_options_["vram_estimation"] = params.vram_estimation;
    loaded_options_["offload_cond_stage"] = params.offload_cond_stage;
    loaded_options_["offload_diffusion"] = params.offload_diffusion;
    loaded_options_["reload_cond_stage"] = params.reload_cond_stage;
    loaded_options_["reload_diffusion"] = params.reload_diffusion;
    loaded_options_["log_offload_events"] = params.log_offload_events;
    loaded_options_["min_offload_size_mb"] = params.min_offload_size_mb;
#endif

    // Set atomic flag for lock-free checks
    model_loaded_ = true;

    // Clear loading state on success
    clear_loading();

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

void ModelManager::unload_model() {
    std::lock_guard<std::mutex> lock(context_mutex_);

    // Clear error state when unloading
    last_load_error_.clear();

    if (context_ != nullptr) {
        std::string model_name = loaded_model_name_;  // Capture before clearing
        std::cout << "[ModelManager] Unloading model: " << model_name << std::endl;

        // Clear atomic flag first
        model_loaded_ = false;

#ifdef SDCPP_EXPERIMENTAL_OFFLOAD
        // Free all GPU resources before unloading to prevent memory leaks
        // (matches the cleanup pattern in load_model when replacing a model)
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

    // Add upscaler info
    {
        std::lock_guard<std::mutex> upscaler_lock(upscaler_mutex_);
        result["upscaler_loaded"] = (upscaler_context_ != nullptr);
        result["upscaler_name"] = loaded_upscaler_name_.empty() ? nullptr : nlohmann::json(loaded_upscaler_name_);
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
    
    // Load upscaler
    upscaler_context_ = new_upscaler_ctx(
        model_info->full_path.c_str(),
        false,      // offload_params_to_cpu
        false,      // direct
        threads,
        tile_size
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
