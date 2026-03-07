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
            .optional_field("negative_prompt", schema::FieldType::String, "Negative prompt", "")
            .arch_default_field("width", schema::FieldType::Integer, "Image width in pixels")
            .arch_default_field("height", schema::FieldType::Integer, "Image height in pixels")
            .arch_default_field("steps", schema::FieldType::Integer, "Number of sampling steps")
            .arch_default_field("cfg_scale", schema::FieldType::Number, "Classifier-free guidance scale")
            .optional_field("distilled_guidance", schema::FieldType::Number, "Distilled guidance scale", 3.5)
            .optional_field("eta", schema::FieldType::Number, "Eta for DDIM-like samplers", 0.0)
            .optional_field("shifted_timestep", schema::FieldType::Integer, "Shifted timestep value", 0)
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
            .optional_field("vae_tiling", schema::FieldType::Boolean, "Enable VAE tiling for large images", false)
            .optional_field("vae_tile_size_x", schema::FieldType::Integer, "VAE tile width (0 for auto)", 0)
            .optional_field("vae_tile_size_y", schema::FieldType::Integer, "VAE tile height (0 for auto)", 0)
            .optional_field("vae_tile_overlap", schema::FieldType::Number, "VAE tile overlap ratio", 0.5)
            .optional_field("easycache", schema::FieldType::Boolean, "Enable EasyCache for faster generation", false)
            .optional_field("easycache_threshold", schema::FieldType::Number, "EasyCache similarity threshold", 0.2)
            .optional_field("easycache_start", schema::FieldType::Number, "EasyCache start percentage", 0.15)
            .optional_field("easycache_end", schema::FieldType::Number, "EasyCache end percentage", 0.95)
            .array_field("pm_id_images", schema::FieldType::String, "PhotoMaker identity images as base64")
            .optional_field("pm_id_embed_path", schema::FieldType::String, "Path to PhotoMaker ID embedding")
            .optional_field("pm_style_strength", schema::FieldType::Number, "PhotoMaker style strength", 20.0)
            .optional_field("upscale", schema::FieldType::Boolean, "Auto-upscale after generation", false)
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
            .optional_field("high_noise_steps", schema::FieldType::Integer, "High-noise sampling steps (-1 for auto)", -1)
            .optional_field("high_noise_cfg_scale", schema::FieldType::Number, "CFG scale for high-noise phase", 7.0)
            .optional_field("high_noise_sampler", schema::FieldType::String, "Sampler for high-noise phase")
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
            .build();
    }
};

} // namespace api
} // namespace sdcpp
