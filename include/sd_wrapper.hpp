#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cstdint>

#include <nlohmann/json.hpp>
#include "stable-diffusion.h"

namespace sdcpp {

/**
 * Progress callback type
 * @param step Current step
 * @param total Total steps
 */
using ProgressCallback = std::function<void(int step, int total)>;

/**
 * Preview callback type
 * @param step Current step
 * @param frame_count Number of frames (1 for images, multiple for video)
 * @param jpeg_data JPEG-encoded preview image data
 * @param width Preview image width
 * @param height Preview image height
 * @param is_noisy Whether this is the noisy (pre-denoise) version
 */
using PreviewCallback = std::function<void(int step, int frame_count,
    const std::vector<uint8_t>& jpeg_data, int width, int height, bool is_noisy)>;

/**
 * Preview mode for generation
 */
enum class PreviewMode {
    None,   // No previews
    Proj,   // Fast projection-based preview (low quality)
    Tae,    // TAESD tiny autoencoder (balanced)
    Vae     // Full VAE preview (high quality, slower)
};

/**
 * Text-to-Image generation parameters
 * 
 * Note: LoRAs are specified directly in the prompt using sd.cpp syntax:
 *   <lora:filename:weight> e.g. <lora:add_detail:0.8>
 */
struct Txt2ImgParams {
    std::string prompt;
    std::string negative_prompt;
    int width = 512;
    int height = 512;
    int steps = 20;
    float cfg_scale = 7.0f;
    int64_t seed = -1;
    std::string sampler = "euler_a";
    std::string scheduler = "discrete";
    int batch_count = 1;
    int clip_skip = -1;

    // Advanced guidance parameters
    float distilled_guidance = 3.5f;    // For Flux/distilled models
    float eta = 0.0f;                   // For DDIM/TCD samplers
    int shifted_timestep = 0;           // For NitroFusion models (250-500)

    // Skip Layer Guidance (SLG) for DiT models
    float slg_scale = 0.0f;             // 0 = disabled, 2.5 for SD3.5 medium
    std::vector<int> skip_layers = {7, 8, 9};
    float slg_start = 0.01f;
    float slg_end = 0.2f;

    // Custom sigma schedule (overrides scheduler)
    std::vector<float> custom_sigmas;

    // Reference images for Flux Kontext
    std::vector<std::string> ref_images_base64;
    bool auto_resize_ref_image = true;
    bool increase_ref_index = false;

    // ControlNet (optional - requires ControlNet loaded with model)
    std::vector<uint8_t> control_image_data;
    int control_image_width = 0;
    int control_image_height = 0;
    int control_image_channels = 3;
    float control_strength = 0.9f;

    // VAE tiling for large images
    bool vae_tiling = false;
    int vae_tile_size_x = 0;            // 0 = auto
    int vae_tile_size_y = 0;
    float vae_tile_overlap = 0.5f;

    // EasyCache for DiT models (optional)
    bool easycache_enabled = false;
    float easycache_threshold = 0.2f;
    float easycache_start = 0.15f;
    float easycache_end = 0.95f;

    // PhotoMaker (optional - requires PhotoMaker model loaded)
    std::vector<std::string> pm_id_images_base64;  // Array of base64-encoded ID images
    std::string pm_id_embed_path;                   // Path to ID embedding file
    float pm_style_strength = 20.0f;                // Style strength (default 20)

    // Upscaling (optional - requires upscaler loaded)
    bool upscale = false;               // Enable upscaling after generation
    bool upscale_auto_unload = true;    // Unload upscaler after upscaling
    int upscale_repeats = 1;            // Run upscaler multiple times

    static Txt2ImgParams from_json(const nlohmann::json& j);
    nlohmann::json to_json() const;
};

/**
 * Image-to-Image generation parameters
 * 
 * Note: LoRAs are specified directly in the prompt using sd.cpp syntax:
 *   <lora:filename:weight> e.g. <lora:add_detail:0.8>
 */
struct Img2ImgParams {
    std::string prompt;
    std::string negative_prompt;
    std::vector<uint8_t> init_image_data;   // Raw image data (decoded from base64)
    int init_image_width = 0;
    int init_image_height = 0;
    int init_image_channels = 3;
    float strength = 0.75f;
    int width = 512;
    int height = 512;
    int steps = 20;
    float cfg_scale = 7.0f;
    float img_cfg_scale = -1.0f;        // Image CFG for instruct-pix2pix (-1 = same as cfg_scale)
    int64_t seed = -1;
    std::string sampler = "euler_a";
    std::string scheduler = "discrete";
    int batch_count = 1;
    int clip_skip = -1;

    // Advanced guidance parameters
    float distilled_guidance = 3.5f;    // For Flux/distilled models
    float eta = 0.0f;                   // For DDIM/TCD samplers
    int shifted_timestep = 0;           // For NitroFusion models (250-500)

    // Skip Layer Guidance (SLG) for DiT models
    float slg_scale = 0.0f;             // 0 = disabled, 2.5 for SD3.5 medium
    std::vector<int> skip_layers = {7, 8, 9};
    float slg_start = 0.01f;
    float slg_end = 0.2f;

    // Custom sigma schedule (overrides scheduler)
    std::vector<float> custom_sigmas;

    // Inpainting mask (optional)
    std::vector<uint8_t> mask_image_data;
    int mask_image_width = 0;
    int mask_image_height = 0;

    // Reference images for Flux Kontext
    std::vector<std::string> ref_images_base64;
    bool auto_resize_ref_image = true;
    bool increase_ref_index = false;

    // ControlNet (optional - requires ControlNet loaded with model)
    std::vector<uint8_t> control_image_data;
    int control_image_width = 0;
    int control_image_height = 0;
    int control_image_channels = 3;
    float control_strength = 0.9f;

    // VAE tiling for large images
    bool vae_tiling = false;
    int vae_tile_size_x = 0;            // 0 = auto
    int vae_tile_size_y = 0;
    float vae_tile_overlap = 0.5f;

    // EasyCache for DiT models (optional)
    bool easycache_enabled = false;
    float easycache_threshold = 0.2f;
    float easycache_start = 0.15f;
    float easycache_end = 0.95f;

    // PhotoMaker (optional - requires PhotoMaker model loaded)
    std::vector<std::string> pm_id_images_base64;  // Array of base64-encoded ID images
    std::string pm_id_embed_path;                   // Path to ID embedding file
    float pm_style_strength = 20.0f;                // Style strength (default 20)

    // Upscaling (optional - requires upscaler loaded)
    bool upscale = false;               // Enable upscaling after generation
    bool upscale_auto_unload = true;    // Unload upscaler after upscaling
    int upscale_repeats = 1;            // Run upscaler multiple times

    static Img2ImgParams from_json(const nlohmann::json& j);
    nlohmann::json to_json() const;
};

/**
 * Upscale parameters
 */
struct UpscaleParams {
    std::vector<uint8_t> image_data;    // Raw image data (decoded from base64)
    int image_width = 0;
    int image_height = 0;
    int image_channels = 3;
    int upscale_factor = 4;             // Target upscale factor (model determines actual)
    int tile_size = 128;                // Tile size for ESRGAN (VRAM optimization)
    int repeats = 1;                    // Run upscaler multiple times

    static UpscaleParams from_json(const nlohmann::json& j);
    nlohmann::json to_json() const;
};

/**
 * Text-to-Video generation parameters
 *
 * Note: LoRAs are specified directly in the prompt using sd.cpp syntax:
 *   <lora:filename:weight> e.g. <lora:add_detail:0.8>
 */
struct Txt2VidParams {
    std::string prompt;
    std::string negative_prompt;
    int width = 832;
    int height = 480;
    int video_frames = 33;
    int fps = 16;                       // Video FPS
    int steps = 30;
    float cfg_scale = 6.0f;
    int64_t seed = -1;
    std::string sampler = "euler";
    std::string scheduler = "discrete";
    float flow_shift = 3.0f;
    int clip_skip = -1;

    // Advanced guidance parameters
    float distilled_guidance = 3.5f;    // For Flux/distilled models
    float eta = 0.0f;                   // For DDIM/TCD samplers

    // Skip Layer Guidance (SLG) for DiT models
    float slg_scale = 0.0f;
    std::vector<int> skip_layers = {7, 8, 9};
    float slg_start = 0.01f;
    float slg_end = 0.2f;

    // Init and end images for vid2vid / FLF2V
    std::vector<uint8_t> init_image_data;
    int init_image_width = 0;
    int init_image_height = 0;
    int init_image_channels = 3;
    std::vector<uint8_t> end_image_data;    // For FLF2V
    int end_image_width = 0;
    int end_image_height = 0;
    int end_image_channels = 3;
    float strength = 0.75f;                 // For vid2vid

    // ControlNet (optional - requires ControlNet loaded with model)
    // Single control image applies to all frames, or multiple control frames
    std::vector<uint8_t> control_image_data;
    int control_image_width = 0;
    int control_image_height = 0;
    int control_image_channels = 3;
    std::vector<std::string> control_frames_base64;  // Multiple control frames

    // High-noise phase parameters (MoE models like Wan2.2)
    int high_noise_steps = -1;          // -1 = auto
    float high_noise_cfg_scale = 7.0f;
    std::string high_noise_sampler = "";  // Empty = use main sampler
    float high_noise_distilled_guidance = 3.5f;
    float high_noise_slg_scale = 0.0f;
    std::vector<int> high_noise_skip_layers = {7, 8, 9};
    float high_noise_slg_start = 0.01f;
    float high_noise_slg_end = 0.2f;
    float moe_boundary = 0.875f;        // Timestep boundary for MoE
    float vace_strength = 1.0f;         // WAN VACE strength

    // VAE tiling for large videos
    bool vae_tiling = false;
    int vae_tile_size_x = 0;            // 0 = auto
    int vae_tile_size_y = 0;
    float vae_tile_overlap = 0.5f;

    // EasyCache for DiT models (optional)
    bool easycache_enabled = false;
    float easycache_threshold = 0.2f;
    float easycache_start = 0.15f;
    float easycache_end = 0.95f;

    static Txt2VidParams from_json(const nlohmann::json& j);
    nlohmann::json to_json() const;
};

/**
 * SD Wrapper - wraps stable-diffusion.cpp functionality
 */
class SDWrapper {
public:
    /**
     * Set global progress callback
     * @param callback Progress callback function
     */
    static void set_progress_callback(ProgressCallback callback);
    
    /**
     * Clear progress callback
     */
    static void clear_progress_callback();

    /**
     * Set global preview callback for live preview images
     * @param callback Preview callback function
     * @param mode Preview mode (None, Proj, Tae, Vae)
     * @param interval Generate preview every N steps (default: 1)
     * @param max_size Maximum preview dimension in pixels (default: 256)
     * @param quality JPEG quality 1-100 (default: 75)
     */
    static void set_preview_callback(PreviewCallback callback,
                                     PreviewMode mode = PreviewMode::Tae,
                                     int interval = 1,
                                     int max_size = 256,
                                     int quality = 75);

    /**
     * Clear preview callback
     */
    static void clear_preview_callback();
    
    /**
     * Generate images from text
     * @param ctx SD context
     * @param params Generation parameters
     * @param lora_dir Directory containing LoRA files
     * @param output_dir Directory to save output
     * @param job_id Job ID for output naming
     * @return List of output file paths (relative to output_dir)
     */
    static std::vector<std::string> generate_txt2img(
        sd_ctx_t* ctx,
        const Txt2ImgParams& params,
        const std::string& lora_dir,
        const std::string& output_dir,
        const std::string& job_id
    );
    
    /**
     * Generate images from image
     * @param ctx SD context
     * @param params Generation parameters
     * @param lora_dir Directory containing LoRA files
     * @param output_dir Directory to save output
     * @param job_id Job ID for output naming
     * @return List of output file paths (relative to output_dir)
     */
    static std::vector<std::string> generate_img2img(
        sd_ctx_t* ctx,
        const Img2ImgParams& params,
        const std::string& lora_dir,
        const std::string& output_dir,
        const std::string& job_id
    );
    
    /**
     * Generate video frames from text
     * @param ctx SD context (must be loaded with video-capable model)
     * @param params Generation parameters
     * @param lora_dir Directory containing LoRA files
     * @param output_dir Directory to save output
     * @param job_id Job ID for output naming
     * @return List of output file paths (relative to output_dir)
     */
    static std::vector<std::string> generate_txt2vid(
        sd_ctx_t* ctx,
        const Txt2VidParams& params,
        const std::string& lora_dir,
        const std::string& output_dir,
        const std::string& job_id
    );
    
    /**
     * Upscale an image
     * @param upscaler_ctx Upscaler context
     * @param params Upscale parameters
     * @param output_dir Directory to save output
     * @param job_id Job ID for output naming
     * @return List of output file paths (relative to output_dir)
     */
    static std::vector<std::string> upscale_image(
        upscaler_ctx_t* upscaler_ctx,
        const UpscaleParams& params,
        const std::string& output_dir,
        const std::string& job_id
    );
    
    /**
     * Upscale an image in-memory (for use in txt2img/img2img pipeline)
     * @param upscaler_ctx Upscaler context
     * @param image_data Input image data
     * @param width Image width
     * @param height Image height
     * @param channels Image channels
     * @param out_width Output width (set by function)
     * @param out_height Output height (set by function)
     * @return Upscaled image data
     */
    static std::vector<uint8_t> upscale_image_data(
        upscaler_ctx_t* upscaler_ctx,
        const uint8_t* image_data,
        int width,
        int height,
        int channels,
        int& out_width,
        int& out_height
    );
    
    /**
     * Load image from file
     * @param filepath Path to image file
     * @param width Output width
     * @param height Output height
     * @param channels Output channels
     * @return Image data as vector
     */
    static std::vector<uint8_t> load_image(
        const std::string& filepath,
        int& width,
        int& height,
        int& channels
    );
    
    /**
     * Decode base64 image data and load
     * @param base64_data Base64 encoded image
     * @param width Output width
     * @param height Output height
     * @param channels Output channels
     * @return Image data as vector
     */
    static std::vector<uint8_t> decode_base64_image(
        const std::string& base64_data,
        int& width,
        int& height,
        int& channels
    );
    
    /**
     * Save image to file
     * @param filepath Output path (extension determines format)
     * @param data Image data
     * @param width Image width
     * @param height Image height
     * @param channels Number of channels
     * @return true if successful
     */
    static bool save_image(
        const std::string& filepath,
        const uint8_t* data,
        int width,
        int height,
        int channels
    );
    
    /**
     * Convert a model to GGUF format with quantization
     * @param input_path Path to input model file
     * @param vae_path Optional path to VAE file (empty string if not needed)
     * @param output_path Path for output GGUF file
     * @param output_type Quantization type (f32, f16, q8_0, q4_0, q4_1, q5_0, q5_1, etc.)
     * @param tensor_type_rules Optional per-tensor quantization rules
     * @return true if conversion successful
     */
    static bool convert_model(
        const std::string& input_path,
        const std::string& vae_path,
        const std::string& output_path,
        const std::string& output_type,
        const std::string& tensor_type_rules = ""
    );

    /**
     * Parsed LoRA info
     */
    struct ParsedLora {
        std::string path;
        float multiplier;
        bool is_high_noise;
    };

    /**
     * Parse LoRAs from prompt and return cleaned prompt + lora list
     * Syntax: <lora:name:weight> or <lora:|high_noise|name:weight>
     * @param prompt Input prompt containing LoRA tags
     * @param lora_dir Directory to search for LoRA files
     * @return Pair of (cleaned prompt, vector of ParsedLora)
     */
    static std::pair<std::string, std::vector<ParsedLora>> parse_loras_from_prompt(
        const std::string& prompt,
        const std::string& lora_dir
    );

private:
    static ProgressCallback progress_callback_;
    static void internal_progress_callback(int step, int steps, float time, void* data);

    // Preview callback support
    static PreviewCallback preview_callback_;
    static int preview_max_size_;
    static int preview_quality_;
    static void internal_preview_callback(int step, int frame_count, sd_image_t* frames, bool is_noisy, void* data);

    // Helper functions for preview
    static std::vector<uint8_t> resize_image_bilinear(const uint8_t* data, int src_w, int src_h, int channels, int dst_w, int dst_h);
    static std::vector<uint8_t> encode_jpeg_memory(const uint8_t* data, int width, int height, int channels, int quality);
};

} // namespace sdcpp
