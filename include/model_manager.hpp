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
    std::optional<std::string> uncond_diffusion_model;  // Unconditional diffusion model (leejet PR #1640+)
    std::optional<std::string> photo_maker;     // PhotoMaker model
    std::optional<std::string> pulid_weights;   // PuLID-Flux identity injection weights (leejet PR #1595)
    std::optional<std::string> audio_vae;       // Audio VAE (LTXAV / LTX 2.3 — for video models with sound)
    std::optional<std::string> embeddings_connectors;  // Embeddings connectors (LTXAV)

    // Options
    int n_threads = -1;
    bool flash_attn = true;
    bool diffusion_flash_attn = false;          // Flash attention specifically for the diffusion model
                                                 // (UNet/DiT/Flux). sd.cpp keeps this separate from
                                                 // flash_attn (which is also for CLIP/T5/conditioner).
    bool enable_mmap = true;                    // Use memory-mapped file loading for models
    bool vae_conv_direct = false;               // Use ggml_conv2d_direct in VAE
    bool diffusion_conv_direct = false;         // Use ggml_conv2d_direct in diffusion
    bool tae_preview_only = false;              // Only use TAESD for preview, not final
    // Per-component CPU placement and "always keep all weights in RAM" are no longer
    // bool flags on sd_ctx_params_t. Upstream (leejet PR #1654) routes them through
    // the `backend` / `params_backend` strings instead — e.g. "diffusion=cuda0,vae=cpu"
    // for per-component, or params_backend="*=cpu" for the global flag that used to
    // be offload_to_cpu. Same for vae_decode_only and free_params_immediately —
    // their semantics moved into sd.cpp's planner and are no longer surfaced.
    // flow_shift / vae_tiling are per-generation now, not load-time — see the
    // request schemas + sd_wrapper for where they're wired into sd_sample_params_t /
    // sd_tiling_params_t respectively.
    float max_vram = 0.0f;                      // GiB budget for graph-cut segmented param offload (0 = disabled).
                                                 // Lives on sd_ctx_params_t (not the offload struct) and is
                                                 // available on both OFFLOAD=ON and OFFLOAD=OFF builds. When
                                                 // set, sd.cpp partitions the diffusion graph so peak weight
                                                 // residency stays under this budget — independent of
                                                 // offload_mode, can be combined with it.
    std::string weight_type;                    // Weight type (f32, f16, q8_0, q4_0, etc.)
    std::string tensor_type_rules;              // Per-tensor weight rules (e.g., "^vae\.=f16")

#if defined(SDCPP_EXPERIMENTAL_OFFLOAD) && !defined(SDCPP_UNIFIED_STREAMING)
    // ── feature/vram-offloading-v2 fields (legacy multi-mode offload API) ──
    std::string offload_mode = "none";          // none, cond_only, cond_diffusion, aggressive, layer_streaming
    std::string vram_estimation = "dryrun";     // dryrun (accurate), formula (fast)
    bool offload_cond_stage = true;             // Offload LLM/CLIP after conditioning
    bool offload_diffusion = false;             // Offload diffusion after sampling
    bool reload_cond_stage = true;              // Reload LLM/CLIP after generation
    bool reload_diffusion = true;               // Reload diffusion after generation
    bool log_offload_events = true;             // Log offload/reload events
    size_t min_offload_size_mb = 0;             // Minimum component size to offload (MB)
    size_t target_free_vram_mb = 0;             // Target free VRAM before VAE decode (MB), 0 = always offload

    // Layer streaming options (for layer_streaming mode - enables models larger than VRAM)
    int streaming_prefetch_layers = 1;          // Number of layers to prefetch ahead
    int streaming_keep_layers_behind = 0;       // Layers to keep after execution (for skip connections)
    size_t streaming_min_free_vram_mb = 0;      // Minimum VRAM to keep free during streaming (MB)
#else
    // ── feature/unified-streaming field (new minimal API) ──────────────────
    // Single bool that engages sd.cpp's residency-aware streaming planner on
    // top of max_vram. Has no effect when max_vram == 0 — the planner uses
    // the max_vram budget to decide which layers stay resident vs streamed,
    // with async H2D prefetch overlapping next-segment load against current
    // segment compute.
    bool stream_layers = false;
#endif

    // Eager-load params into the params backend at model-load time instead of
    // lazily on first use (leejet PR #1687). Pairs naturally with stream_layers
    // on a CPU params backend — the first generation no longer pays for
    // lazy fault-in. No effect when the params backend matches the compute
    // backend (then load is always eager anyway).
    //
    // Default is `true` in the restapi — upstream sd_ctx_params_t.eager_load
    // defaults to false because the CLI is a one-shot tool where lazy load
    // is acceptable; the restapi is a long-lived server where the first
    // request after a load should be fast, so we flip the default. Users
    // can still pass eager_load=false to opt out.
    bool eager_load = true;

    // RNG options
    std::string rng_type = "cuda";              // std_default, cuda, cpu
    std::string sampler_rng_type = "";          // Empty = use rng_type

    // Prediction type override (empty = auto)
    std::string prediction = "";                // eps, v, edm_v, sd3_flow, flux_flow, flux2_flow

    // LoRA apply mode
    std::string lora_apply_mode = "auto";       // auto, immediately, at_runtime

    // VAE tiling is per-generation (sd_tiling_params_t) — wired in the request schemas + sd_wrapper.
    bool force_sdxl_vae_conv_scale = false;

    // Chroma options
    bool chroma_use_dit_mask = true;
    bool chroma_use_t5_mask = false;
    int chroma_t5_mask_pad = 1;

    // Qwen-Image specific: zero out the conditional T branch (upstream addition).
    // Off by default — only Qwen-Image checkpoints look at this knob.
    bool qwen_image_zero_cond_t = false;

    // VAE format override (leejet master, post 1ceb5bd). Values: "auto" (default,
    // sd.cpp detects from the VAE weights), "flux", "sd3", "flux2". Maps to
    // sd_vae_format_t on the ctx_params. Useful when a VAE file lacks the
    // metadata sd.cpp uses for autodetection.
    std::string vae_format = "auto";

    // Circular RoPE / tileable position embeddings. Used for seamless texture
    // generation — when on, the position embedding wraps so the output tiles
    // perfectly on the chosen axis. Required for Ideogram4-style workflows
    // (leejet PR #1627). Both bools are on sd_ctx_params_t.
    bool circular_x = false;
    bool circular_y = false;

    // Backend routing (sd.cpp post-2026-05-16). Empty = let sd.cpp pick the
    // default backend (matches the historical behavior of selecting the
    // first available accelerator at build time).
    std::string backend = "";                   // Main compute backend ("cuda", "vulkan", "rocm", "metal", "opencl", "")
    std::string params_backend = "";            // Override for parameter storage backend (typically empty)
    std::string rpc_servers = "";               // RPC distributed-backend node list (leejet PR #1629).
                                                 // Format is sd.cpp's own — comma-separated "host:port" pairs.
                                                 // Empty = no RPC, ctx_params.rpc_servers stays nullptr.

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
     * If a previous run persisted its last-loaded model identity to disk,
     * try to load that same model now. Called once at server startup so
     * queued jobs picked up from /workspace/output/queue_state.json find
     * a model already loaded — without this they fail with "No model
     * loaded" because the in-memory model state is lost on restart.
     *
     * Returns true if a model was successfully reloaded, false if there
     * was nothing to restore or the reload failed (last_load_error_ will
     * have details). Does NOT throw — designed to be best-effort.
     */
    bool try_auto_reload_from_disk();
    
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
     * Lock-free check for "a load is currently in progress".
     * Used by /models/load to reject concurrent loads with 409 instead of
     * letting the second request queue on context_mutex_ for minutes.
     */
    bool is_loading() const { return model_loading_.load(); }

    /**
     * Last load failure message, or empty string if the most recent load
     * succeeded. Cleared at the start of every load. Used by the
     * synchronous wait flow (POST /models/load?wait=true) to surface a
     * meaningful 5xx body when the awaited load fails. Lock-free read of
     * a string member is technically a data race; the value is only
     * sampled AFTER is_loading() returns false (the loading thread has
     * finished writing by then), so this is safe in practice.
     */
    std::string get_last_load_error() const { return last_load_error_; }
    
    /**
     * Get name of currently loaded model
     */
    std::string get_loaded_model_name() const;

    /**
     * Get architecture of currently loaded model
     */
    std::string get_loaded_model_architecture() const;

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
