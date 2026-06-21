#pragma once

#include "api_schemas/common.hpp"

namespace sdcpp {
namespace api {

// Base generation request - shared fields for txt2img, img2img, txt2vid
struct GenerationRequestBase {
    static schema::SchemaDescriptor schema() {
        auto builder = schema::SchemaBuilder("GenerationRequestBase", "Common generation parameters");
        builder
            .required_field("prompt", schema::FieldType::String, "Text prompt for generation")
            .optional_field("title", schema::FieldType::String, "Optional display title for the queue job (free-form, shown next to the type label in the WebUI)", "")
            .optional_field("negative_prompt", schema::FieldType::String, "Negative prompt", "")
            .arch_default_field("width", schema::FieldType::Integer, "Image width in pixels")
            .arch_default_field("height", schema::FieldType::Integer, "Image height in pixels")
            .arch_default_field("steps", schema::FieldType::Integer, "Number of sampling steps")
            .arch_default_field("cfg_scale", schema::FieldType::Number, "Classifier-free guidance scale (sd_guidance_params_t.txt_cfg)")
            .optional_field("img_cfg_scale", schema::FieldType::Number, "Image CFG (sd_guidance_params_t.img_cfg). -1 = inherit cfg_scale.", -1.0)
            .optional_field("distilled_guidance", schema::FieldType::Number, "Distilled guidance scale (Flux/distilled models)", 3.5)
            .optional_field("eta", schema::FieldType::Number, "Eta for DDIM-like samplers", 0.0)
            .optional_field("shifted_timestep", schema::FieldType::Integer, "Shifted timestep value (NitroFusion: 250-500)", 0)
            .optional_field("extra_sample_args", schema::FieldType::String, "Pass-through key=value list for sd.cpp's sample arg parser (model-specific knobs)")
            .optional_field("seed", schema::FieldType::Integer, "RNG seed (-1 for random)", -1)
            .arch_default_enum("sampler", "Sampling algorithm", SAMPLER_VALUES)
            .arch_default_enum("scheduler", "Noise scheduler", SCHEDULER_VALUES)
            .optional_field("batch_count", schema::FieldType::Integer, "Number of images to generate", 1)
            .optional_field("clip_skip", schema::FieldType::Integer, "CLIP layers to skip (-1 for default)", -1)
            .optional_field("slg_scale", schema::FieldType::Number, "Skip-layer guidance scale", 0.0)
            .array_field("skip_layers", schema::FieldType::Integer, "Layer indices for skip-layer guidance")
            .optional_field("slg_start", schema::FieldType::Number, "SLG start percentage", 0.01)
            .optional_field("slg_end", schema::FieldType::Number, "SLG end percentage", 0.2)
            .array_field("custom_sigmas", schema::FieldType::Number, "Custom sigma schedule values")
            .array_field("ref_images", schema::FieldType::String, "Reference images as base64 strings")
            .optional_field("auto_resize_ref_image", schema::FieldType::Boolean, "Auto-resize reference images to match output", true)
            .optional_field("increase_ref_index", schema::FieldType::Boolean, "Increase reference index for multi-ref", false)
            .optional_field("control_image_base64", schema::FieldType::String, "ControlNet input image as base64")
            .optional_field("control_strength", schema::FieldType::Number, "ControlNet guidance strength", 0.9)
            // VAE tiling (per-generation sd_tiling_params_t)
            .optional_field("vae_tiling", schema::FieldType::Boolean, "Enable VAE tiling for large images", false)
            .optional_field("vae_tile_size_x", schema::FieldType::Integer, "VAE tile width (0 for auto)", 0)
            .optional_field("vae_tile_size_y", schema::FieldType::Integer, "VAE tile height (0 for auto)", 0)
            .optional_field("vae_tile_overlap", schema::FieldType::Number, "VAE tile overlap ratio", 0.5)
            .optional_field("vae_tile_rel_size_x", schema::FieldType::Number, "VAE tile width as fraction of image (0 = use absolute tile_size_x)", 0.0)
            .optional_field("vae_tile_rel_size_y", schema::FieldType::Number, "VAE tile height as fraction of image", 0.0)
            .optional_field("temporal_tiling", schema::FieldType::Boolean, "Enable temporal VAE tiling (LTX video models — splits the time axis into tiles to reduce memory pressure during decode)", false)
            .optional_field("extra_tiling_args", schema::FieldType::String, "Extra key=value tiling args (passed through to sd.cpp's tiling parser, model-specific)")
            // Cache acceleration (sd_cache_params_t) — 6 upstream modes
            .optional_field("cache_mode", schema::FieldType::String, "Cache acceleration mode: easycache, ucache, dbcache, taylorseer, cache_dit, spectrum (empty = disabled)")
            .optional_field("easycache", schema::FieldType::Boolean, "Enable EasyCache (deprecated alias for cache_mode='easycache')", false)
            .optional_field("easycache_threshold", schema::FieldType::Number, "Reuse threshold for similarity-based caches (EasyCache/UCache/DBCache)", 0.2)
            .optional_field("easycache_start", schema::FieldType::Number, "Cache start percentage (shared across modes)", 0.15)
            .optional_field("easycache_end", schema::FieldType::Number, "Cache end percentage (shared across modes)", 0.95)
            .optional_field("taylorseer_n_derivatives", schema::FieldType::Integer, "TaylorSeer: derivative order (typically 2-4)", 2)
            .optional_field("taylorseer_skip_interval", schema::FieldType::Integer, "TaylorSeer: predict-every-N-steps interval (0 = adaptive)", 0)
            .optional_field("spectrum_w", schema::FieldType::Number, "Spectrum cache weight parameter", 0.5)
            .optional_field("spectrum_m", schema::FieldType::Integer, "Spectrum cache M parameter", 5)
            .optional_field("spectrum_lam", schema::FieldType::Number, "Spectrum cache lambda", 0.5)
            .optional_field("spectrum_window_size", schema::FieldType::Integer, "Spectrum cache window size", 3)
            .optional_field("spectrum_flex_window", schema::FieldType::Number, "Spectrum cache flex window", 0.5)
            .optional_field("spectrum_warmup_steps", schema::FieldType::Integer, "Spectrum cache warmup steps", 2)
            .optional_field("spectrum_stop_percent", schema::FieldType::Number, "Spectrum cache stop percentage", 0.8)
            // PhotoMaker (sd_pm_params_t)
            .array_field("pm_id_images", schema::FieldType::String, "PhotoMaker identity images as base64")
            .optional_field("pm_id_embed_path", schema::FieldType::String, "Path to PhotoMaker ID embedding")
            .optional_field("pm_style_strength", schema::FieldType::Number, "PhotoMaker style strength", 20.0)
            // PuLID-Flux per-gen (sd_pulid_params_t) — requires pulid_weights loaded on the model
            .optional_field("pulid_id_embedding_path", schema::FieldType::String, "PuLID-Flux identity embedding path")
            .optional_field("pulid_id_weight", schema::FieldType::Number, "PuLID-Flux identity weight", 1.0)
            // Built-in hi-res-fix (sd_hires_params_t) — distinct from the post-gen ESRGAN upscale flag below
            .optional_field("hires_enabled", schema::FieldType::Boolean, "Enable sd.cpp's native two-pass hi-res-fix refine", false)
            .optional_field("hires_upscaler", schema::FieldType::String, "Hires upscaler (sd_hires_upscaler_t): none, latent, latent_nearest, latent_nearest_exact, latent_antialiased, latent_bicubic, latent_bicubic_antialiased, lanczos, nearest, model", "model")
            .optional_field("hires_model_path", schema::FieldType::String, "Upscaler model path (when hires_upscaler='model')")
            .optional_field("hires_scale", schema::FieldType::Number, "Hires scale factor", 2.0)
            .optional_field("hires_target_width", schema::FieldType::Integer, "Hires target width (0 = derive from scale)", 0)
            .optional_field("hires_target_height", schema::FieldType::Integer, "Hires target height", 0)
            .optional_field("hires_steps", schema::FieldType::Integer, "Hires sampling steps (0 = inherit main steps)", 0)
            .optional_field("hires_denoising_strength", schema::FieldType::Number, "Hires denoising strength", 0.4)
            .optional_field("hires_upscale_tile_size", schema::FieldType::Integer, "Hires upscaler tile size", 0)
            // Post-gen ESRGAN upscale — restapi orchestration on top of sd.cpp, NOT the same as hires_*
            .optional_field("upscale", schema::FieldType::Boolean, "Auto-run a loaded ESRGAN upscaler after generation (post-gen, distinct from hires_enabled)", false)
            .optional_field("upscale_repeats", schema::FieldType::Integer, "Number of upscale passes", 1)
            .optional_field("upscale_auto_unload", schema::FieldType::Boolean, "Unload upscaler after use", true);
        return builder.build();
    }
};

struct Txt2ImgRequest {
    using base_schema_type = GenerationRequestBase;
    static schema::SchemaDescriptor schema() {
        return schema::SchemaBuilder("Txt2ImgRequest", "Text-to-image generation request")
            .inherits("GenerationRequestBase")
            .build();
    }
};

struct Img2ImgRequest {
    using base_schema_type = GenerationRequestBase;
    static schema::SchemaDescriptor schema() {
        return schema::SchemaBuilder("Img2ImgRequest", "Image-to-image generation request")
            .inherits("GenerationRequestBase")
            .required_field("init_image_base64", schema::FieldType::String, "Source image as base64-encoded string")
            .optional_field("strength", schema::FieldType::Number, "Denoising strength (0.0-1.0)", 0.75)
            .optional_field("img_cfg_scale", schema::FieldType::Number, "Image CFG scale (-1 for auto)", -1.0)
            .optional_field("mask_image_base64", schema::FieldType::String, "Inpainting mask as base64 (white=inpaint)")
            .build();
    }
};

struct Txt2VidRequest {
    using base_schema_type = GenerationRequestBase;
    static schema::SchemaDescriptor schema() {
        return schema::SchemaBuilder("Txt2VidRequest", "Text-to-video generation request")
            .inherits("GenerationRequestBase")
            .optional_field("video_frames", schema::FieldType::Integer, "Number of video frames to generate", 33)
            .optional_field("fps", schema::FieldType::Integer, "Output video frames per second", 16)
            .optional_field("flow_shift", schema::FieldType::Number, "Flow matching shift parameter", 3.0)
            .optional_field("init_image_base64", schema::FieldType::String, "Starting frame image as base64")
            .optional_field("end_image_base64", schema::FieldType::String, "Ending frame image as base64")
            .optional_field("strength", schema::FieldType::Number, "Denoising strength for init image", 0.75)
            .array_field("control_frames", schema::FieldType::String, "Control frames as base64 strings")
            // High-noise pass (MoE models like Wan2.2) — full sd_sample_params_t parity
            .optional_field("high_noise_steps", schema::FieldType::Integer, "High-noise sampling steps (-1 for auto)", -1)
            .optional_field("high_noise_cfg_scale", schema::FieldType::Number, "CFG scale for high-noise phase", 7.0)
            .optional_field("high_noise_img_cfg", schema::FieldType::Number, "Image CFG for high-noise phase (-1 = inherit high_noise_cfg_scale)", -1.0)
            .optional_field("high_noise_sampler", schema::FieldType::String, "Sampler for high-noise phase (empty = inherit main)")
            .optional_field("high_noise_scheduler", schema::FieldType::String, "Scheduler for high-noise phase (empty = inherit main)")
            .optional_field("high_noise_eta", schema::FieldType::Number, "Eta for high-noise phase", 0.0)
            .optional_field("high_noise_shifted_timestep", schema::FieldType::Integer, "Shifted timestep for high-noise phase", 0)
            .optional_field("high_noise_flow_shift", schema::FieldType::Number, "Flow shift for high-noise phase (0 = inherit main flow_shift)", 0.0)
            .optional_field("high_noise_extra_sample_args", schema::FieldType::String, "Pass-through key=value sample args for high-noise phase")
            .array_field("high_noise_custom_sigmas", schema::FieldType::Number, "Custom sigma schedule for high-noise phase")
            .optional_field("high_noise_distilled_guidance", schema::FieldType::Number, "Distilled guidance for high-noise", 3.5)
            .optional_field("high_noise_slg_scale", schema::FieldType::Number, "SLG scale for high-noise phase", 0.0)
            .array_field("high_noise_skip_layers", schema::FieldType::Integer, "Skip layers for high-noise SLG")
            .optional_field("high_noise_slg_start", schema::FieldType::Number, "SLG start for high-noise", 0.01)
            .optional_field("high_noise_slg_end", schema::FieldType::Number, "SLG end for high-noise", 0.2)
            .optional_field("moe_boundary", schema::FieldType::Number, "MoE boundary for WAN models", 0.875)
            .optional_field("vace_strength", schema::FieldType::Number, "VACE control strength", 1.0)
            .build();
    }
};

struct UpscaleRequest {
    static schema::SchemaDescriptor schema() {
        return schema::SchemaBuilder("UpscaleRequest", "Image upscaling request")
            .required_field("image_base64", schema::FieldType::String, "Image to upscale as base64")
            .optional_field("title", schema::FieldType::String, "Optional display title for the queue job", "")
            .optional_field("upscale_factor", schema::FieldType::Integer, "Upscale factor", 4)
            .optional_field("tile_size", schema::FieldType::Integer, "Processing tile size", 128)
            .optional_field("repeats", schema::FieldType::Integer, "Number of upscale passes", 1)
            .build();
    }
};

struct ConvertRequest {
    static schema::SchemaDescriptor schema() {
        return schema::SchemaBuilder("ConvertRequest", "Model format conversion request")
            .required_field("input_path", schema::FieldType::String, "Input model path or name")
            .required_field("output_type", schema::FieldType::String, "Target quantization type")
            .enum_field("model_type", "Model type for name resolution", MODEL_TYPE_VALUES)
            .optional_field("output_path", schema::FieldType::String, "Output path (auto-generated if empty)")
            .optional_field("title", schema::FieldType::String, "Optional display title for the queue job", "")
            .build();
    }
};

} // namespace api
} // namespace sdcpp
