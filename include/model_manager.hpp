#pragma once

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <memory>
#include <optional>
#include <atomic>
#include <cmath>

#include "config.hpp"

// Forward declaration of sd.cpp types
struct sd_ctx_t;
struct upscaler_ctx_t;

namespace sdcpp {

/**
 * Model type enumeration
 */
enum class ModelType {
    Checkpoint,     // SD1.x, SD2.x, SDXL (model_path)
    Diffusion,      // Flux, SD3, Qwen, Wan, Z-Image (diffusion_model_path)
    VAE,
    LoRA,
    CLIP,
    T5,
    Embedding,
    ControlNet,     // ControlNet models
    LLM,            // LLM models for multimodal (Qwen, etc.)
    ESRGAN,         // ESRGAN upscaler models
    TAESD           // TAESD tiny autoencoder models for preview
};

/**
 * Model file information
 */
struct ModelInfo {
    std::string name;           // Relative path from base dir (e.g., "SD1x/model.safetensors")
    std::string full_path;      // Full filesystem path
    ModelType type;
    std::string file_extension; // "safetensors", "gguf", "ckpt"
    size_t file_size = 0;
    std::string hash;           // SHA256, computed on demand
    
    nlohmann::json to_json() const;
};

/**
 * Filter parameters for model listing
 */
struct ModelFilter {
    std::optional<ModelType> type;           // Filter by model type
    std::optional<std::string> extension;    // Filter by file extension (e.g., "safetensors", "gguf")
    std::optional<std::string> search;       // Search in model name (case-insensitive substring match)
    
    /**
     * Check if a model matches the filter criteria
     */
    bool matches(const ModelInfo& model) const;
    
    /**
     * Check if the filter is empty (no filtering)
     */
    bool is_empty() const;
};

/**
 * Parameters for loading a model
 */
struct ModelLoadParams {
    std::string model_name;         // Main model name
    ModelType model_type = ModelType::Checkpoint;

    // Optional components (paths relative to respective directories)
    std::optional<std::string> vae;
    std::optional<std::string> clip_l;
    std::optional<std::string> clip_g;
    std::optional<std::string> clip_vision;     // CLIP vision encoder (IP-Adapter)
    std::optional<std::string> t5xxl;
    std::optional<std::string> controlnet;      // ControlNet model (optional)
    std::optional<std::string> llm;             // LLM for multimodal (e.g., Qwen)
    std::optional<std::string> llm_vision;      // LLM vision model (optional)
    std::optional<std::string> taesd;           // Tiny AutoEncoder for previews
    std::optional<std::string> high_noise_diffusion_model;  // High-noise diffusion (MoE)
    std::optional<std::string> photo_maker;     // PhotoMaker model

    // Options
    int n_threads = -1;
    bool keep_clip_on_cpu = true;
    bool keep_vae_on_cpu = false;
    bool keep_controlnet_on_cpu = false;        // Keep ControlNet on CPU for low VRAM
    bool flash_attn = true;
    bool offload_to_cpu = false;
    bool enable_mmap = true;                    // Use memory-mapped file loading for models
    bool vae_decode_only = true;
    bool vae_conv_direct = false;               // Use ggml_conv2d_direct in VAE
    bool diffusion_conv_direct = false;         // Use ggml_conv2d_direct in diffusion
    bool tae_preview_only = false;              // Only use TAESD for preview, not final
    bool free_params_immediately = false;       // Keep model loaded in GPU (false = keep for server use)
    float flow_shift = INFINITY;                // INFINITY = auto-detect based on model
    std::string weight_type;                    // Weight type (f32, f16, q8_0, q4_0, etc.)
    std::string tensor_type_rules;              // Per-tensor weight rules (e.g., "^vae\.=f16")

    // RNG options
    std::string rng_type = "cuda";              // std_default, cuda, cpu
    std::string sampler_rng_type = "";          // Empty = use rng_type

    // Prediction type override (empty = auto)
    std::string prediction = "";                // eps, v, edm_v, sd3_flow, flux_flow, flux2_flow

    // LoRA apply mode
    std::string lora_apply_mode = "auto";       // auto, immediately, at_runtime

    // VAE tiling for large images
    bool vae_tiling = false;
    int vae_tile_size_x = 0;                    // 0 = auto
    int vae_tile_size_y = 0;
    float vae_tile_overlap = 0.5f;
    bool force_sdxl_vae_conv_scale = false;

    // Chroma options
    bool chroma_use_dit_mask = true;
    bool chroma_use_t5_mask = false;
    int chroma_t5_mask_pad = 1;

    static ModelLoadParams from_json(const nlohmann::json& j);
};

/**
 * Model Manager - handles model discovery, loading, and unloading
 * Thread-safe for concurrent access
 */
class ModelManager {
public:
    explicit ModelManager(const Config& config);
    ~ModelManager();
    
    // Non-copyable
    ModelManager(const ModelManager&) = delete;
    ModelManager& operator=(const ModelManager&) = delete;
    
    /**
     * Scan all configured directories for models
     */
    void scan_models();
    
    /**
     * Get list of all available models by type
     */
    std::vector<ModelInfo> get_models(ModelType type) const;
    
    /**
     * Get all models as JSON
     */
    nlohmann::json get_models_json() const;
    
    /**
     * Get models as JSON with optional filtering
     * @param filter Filter parameters
     */
    nlohmann::json get_models_json(const ModelFilter& filter) const;
    
    /**
     * Get specific model info
     * @param name Model name (relative path)
     * @param type Model type
     * @return ModelInfo if found
     */
    std::optional<ModelInfo> get_model(const std::string& name, ModelType type) const;
    
    /**
     * Load a model into memory
     * @param params Load parameters
     * @return true if successful
     * @throws std::runtime_error on failure
     */
    bool load_model(const ModelLoadParams& params);
    
    /**
     * Unload current model
     */
    void unload_model();
    
    /**
     * Check if a model is currently loaded
     */
    bool is_model_loaded() const;
    
    /**
     * Get name of currently loaded model
     */
    std::string get_loaded_model_name() const;
    
    /**
     * Get info about all currently loaded models as JSON
     */
    nlohmann::json get_loaded_models_info() const;
    
    /**
     * Compute hash for a model (thread-safe, caches result)
     * @param name Model name
     * @param type Model type
     * @return SHA256 hash
     */
    std::string compute_model_hash(const std::string& name, ModelType type);
    
    /**
     * Get SD context for generation (caller must hold lock)
     * @return Pointer to sd_ctx_t or nullptr if not loaded
     */
    sd_ctx_t* get_context();
    
    /**
     * Get mutex for locking context during generation
     */
    std::mutex& get_context_mutex();
    
    /**
     * Get the LoRA directory path
     */
    std::string get_lora_dir() const;
    
    // ==================== Upscaler Management ====================
    
    /**
     * Load an upscaler model
     * @param model_name ESRGAN model name
     * @param n_threads Number of threads (-1 = auto)
     * @param tile_size Tile size for processing (default 128)
     * @return true if successful
     */
    bool load_upscaler(const std::string& model_name, int n_threads = -1, int tile_size = 128);
    
    /**
     * Unload current upscaler model
     */
    void unload_upscaler();
    
    /**
     * Check if an upscaler is loaded
     */
    bool is_upscaler_loaded() const;
    
    /**
     * Get upscaler context
     */
    upscaler_ctx_t* get_upscaler_context();
    
    /**
     * Get upscaler mutex
     */
    std::mutex& get_upscaler_mutex();
    
    /**
     * Get loaded upscaler model name
     */
    std::string get_loaded_upscaler_name() const;
    
    /**
     * Get upscale factor from currently loaded model
     */
    int get_upscale_factor() const;

    /**
     * Update loading progress (called from progress callback)
     * Thread-safe, updates atomic variables
     */
    void update_loading_progress(int step, int total_steps);

    /**
     * Get paths configuration as JSON for download manager
     */
    nlohmann::json get_paths_config() const;

private:
    void scan_directory(const std::string& base_path, ModelType type);
    std::string get_base_path(ModelType type) const;
    
    Config config_;
    
    // Model registry
    mutable std::mutex registry_mutex_;
    std::map<ModelType, std::map<std::string, ModelInfo>> models_;
    
    // Currently loaded model
    mutable std::mutex context_mutex_;
    sd_ctx_t* context_ = nullptr;
    std::atomic<bool> model_loaded_{false};  // Lock-free check for is_model_loaded
    std::atomic<bool> model_loading_{false}; // Lock-free check for loading in progress
    std::atomic<int> loading_step_{0};       // Current loading step (from sd.cpp callback)
    std::atomic<int> loading_total_steps_{0}; // Total loading steps (from sd.cpp callback)
    std::string loaded_model_name_;
    std::string loading_model_name_;         // Name of model currently being loaded
    std::string last_load_error_;            // Last model load error message
    ModelType loaded_model_type_ = ModelType::Checkpoint;
    std::string loaded_model_architecture_;  // Cached architecture name (e.g., "Z-Image", "Flux")
    
    // Track all loaded component models
    std::string loaded_vae_;
    std::string loaded_clip_l_;
    std::string loaded_clip_g_;
    std::string loaded_t5_;
    std::string loaded_controlnet_;
    std::string loaded_llm_;
    std::string loaded_llm_vision_;

    // Store the load options used when loading the model
    nlohmann::json loaded_options_;
    
    // Upscaler context (separate from main SD context)
    mutable std::mutex upscaler_mutex_;
    upscaler_ctx_t* upscaler_context_ = nullptr;
    std::atomic<bool> upscaler_loaded_{false};  // Lock-free check for is_upscaler_loaded
    std::string loaded_upscaler_name_;
};

// String conversions
std::string model_type_to_string(ModelType type);
ModelType string_to_model_type(const std::string& str);

} // namespace sdcpp
