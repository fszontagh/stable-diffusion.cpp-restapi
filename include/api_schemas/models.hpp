#pragma once

#include "api_schemas/common.hpp"

namespace sdcpp {
namespace api {

struct LoadModelRequest {
    static schema::SchemaDescriptor schema() {
        auto builder = schema::SchemaBuilder("LoadModelRequest", "Load a model into memory");
        builder
            .required_field("model_name", schema::FieldType::String, "Name of the model file to load")
            .enum_field("model_type", "Type of model", {"checkpoint", "diffusion"}, "checkpoint")
            .optional_field("vae", schema::FieldType::String, "VAE model name")
            .optional_field("clip_l", schema::FieldType::String, "CLIP-L text encoder name")
            .optional_field("clip_g", schema::FieldType::String, "CLIP-G text encoder name")
            .optional_field("clip_vision", schema::FieldType::String, "CLIP vision model name")
            .optional_field("t5xxl", schema::FieldType::String, "T5-XXL text encoder name")
            .optional_field("controlnet", schema::FieldType::String, "ControlNet model name")
            .optional_field("llm", schema::FieldType::String, "LLM model name")
            .optional_field("llm_vision", schema::FieldType::String, "LLM vision model name")
            .optional_field("taesd", schema::FieldType::String, "TAESD model for fast preview")
            .optional_field("high_noise_diffusion_model", schema::FieldType::String, "High-noise diffusion model (for dual-stage)")
            .optional_field("photo_maker", schema::FieldType::String, "PhotoMaker model name")
            .ref_field("options", "LoadOptions", "Model loading options");
        return builder.build();
    }
};

struct LoadOptions {
    static schema::SchemaDescriptor schema() {
        auto builder = schema::SchemaBuilder("LoadOptions", "Model loading options");
        builder
            .optional_field("n_threads", schema::FieldType::Integer, "Number of CPU threads (-1 for auto)", -1)
            .optional_field("keep_clip_on_cpu", schema::FieldType::Boolean, "Keep CLIP on CPU to save VRAM", false)
            .optional_field("keep_vae_on_cpu", schema::FieldType::Boolean, "Keep VAE on CPU", false)
            .optional_field("keep_controlnet_on_cpu", schema::FieldType::Boolean, "Keep ControlNet on CPU", false)
            .optional_field("vae_conv_direct", schema::FieldType::Boolean, "Direct VAE convolution", false)
            .optional_field("diffusion_conv_direct", schema::FieldType::Boolean, "Direct diffusion convolution", false)
            .optional_field("flash_attn", schema::FieldType::Boolean, "Enable flash attention", true)
            .optional_field("offload_to_cpu", schema::FieldType::Boolean, "Offload model to CPU when idle", false)
            .optional_field("enable_mmap", schema::FieldType::Boolean, "Enable memory-mapped file access", true)
            .optional_field("vae_decode_only", schema::FieldType::Boolean, "VAE decode only (no encode)", false)
            .optional_field("tae_preview_only", schema::FieldType::Boolean, "Use TAESD for preview only", false)
            .optional_field("free_params_immediately", schema::FieldType::Boolean, "Free params right after use", false)
            .optional_field("flow_shift", schema::FieldType::Number, "Flow matching shift parameter")
            .enum_field("weight_type", "Weight precision type", WEIGHT_TYPE_VALUES)
            .optional_field("tensor_type_rules", schema::FieldType::String, "Custom tensor type rules string")
            .enum_field("rng_type", "Random number generator type", RNG_TYPE_VALUES, "cuda")
            .optional_field("sampler_rng_type", schema::FieldType::String, "Sampler-specific RNG type")
            .enum_field("prediction", "Prediction type", {"eps", "v", "edm_v", "sd3_flow", "flux_flow", "flux2_flow", ""})
            .enum_field("lora_apply_mode", "LoRA application mode", {"auto", "immediately", "at_runtime"}, "auto")
            .optional_field("vae_tiling", schema::FieldType::Boolean, "Enable VAE tiling", false)
            .optional_field("vae_tile_size_x", schema::FieldType::Integer, "VAE tile width", 0)
            .optional_field("vae_tile_size_y", schema::FieldType::Integer, "VAE tile height", 0)
            .optional_field("vae_tile_overlap", schema::FieldType::Number, "VAE tile overlap ratio", 0.5)
            .optional_field("chroma_use_dit_mask", schema::FieldType::Boolean, "Use DiT attention mask (Chroma)", false)
            .optional_field("chroma_use_t5_mask", schema::FieldType::Boolean, "Use T5 attention mask (Chroma)", false)
            .optional_field("chroma_t5_mask_pad", schema::FieldType::Integer, "T5 mask padding (Chroma)", 0)
#ifdef SDCPP_EXPERIMENTAL_OFFLOAD
            .enum_field("offload_mode", "VRAM offload strategy", OFFLOAD_MODE_VALUES, "none")
            .enum_field("vram_estimation", "VRAM estimation method", VRAM_ESTIMATION_VALUES, "dryrun")
            .optional_field("offload_cond_stage", schema::FieldType::Boolean, "Offload conditioning after use", false)
            .optional_field("offload_diffusion", schema::FieldType::Boolean, "Offload diffusion after sampling", false)
            .optional_field("reload_cond_stage", schema::FieldType::Boolean, "Reload conditioning for next gen", true)
            .optional_field("reload_diffusion", schema::FieldType::Boolean, "Reload diffusion for next gen", true)
            .optional_field("log_offload_events", schema::FieldType::Boolean, "Log offload events", false)
            .optional_field("min_offload_size_mb", schema::FieldType::Integer, "Minimum component size to offload (MB)", 0)
            .optional_field("target_free_vram_mb", schema::FieldType::Integer, "Target free VRAM before VAE (MB)", 0)
            .optional_field("layer_streaming_enabled", schema::FieldType::Boolean, "Enable layer-by-layer streaming", false)
            .optional_field("streaming_prefetch_layers", schema::FieldType::Integer, "Layers to prefetch ahead", 1)
            .optional_field("streaming_keep_layers_behind", schema::FieldType::Integer, "Layers to keep after execution", 0)
            .optional_field("streaming_min_free_vram_mb", schema::FieldType::Integer, "Min free VRAM during streaming (MB)", 0)
#endif
            ;
        return builder.build();
    }
};

struct LoadModelResponse {
    static schema::SchemaDescriptor schema() {
        return schema::SchemaBuilder("LoadModelResponse", "Model load result")
            .required_field("success", schema::FieldType::Boolean, "Whether load succeeded")
            .required_field("message", schema::FieldType::String, "Status message")
            .optional_field("model_name", schema::FieldType::String, "Name of loaded model")
            .optional_field("model_type", schema::FieldType::String, "Type of loaded model")
            .object_field("loaded_components", "Active model components")
            .build();
    }
};

struct ModelListResponse {
    static schema::SchemaDescriptor schema() {
        return schema::SchemaBuilder("ModelListResponse", "List of all available models by category")
            .array_field("checkpoints", schema::FieldType::Object, "Checkpoint models")
            .array_field("diffusion_models", schema::FieldType::Object, "Diffusion models")
            .array_field("vae", schema::FieldType::Object, "VAE models")
            .array_field("loras", schema::FieldType::Object, "LoRA models")
            .array_field("clip", schema::FieldType::Object, "CLIP models")
            .array_field("t5", schema::FieldType::Object, "T5 models")
            .array_field("controlnets", schema::FieldType::Object, "ControlNet models")
            .array_field("llm", schema::FieldType::Object, "LLM models")
            .array_field("esrgan", schema::FieldType::Object, "ESRGAN upscaler models")
            .array_field("taesd", schema::FieldType::Object, "TAESD preview models")
            .array_field("embeddings", schema::FieldType::Object, "Embedding models")
            .optional_field("loaded_model", schema::FieldType::String, "Currently loaded model name")
            .optional_field("loaded_model_type", schema::FieldType::String, "Type of currently loaded model")
            .build();
    }
};

struct ModelHashResponse {
    static schema::SchemaDescriptor schema() {
        return schema::SchemaBuilder("ModelHashResponse", "Model hash computation result")
            .required_field("model_name", schema::FieldType::String, "Model name")
            .required_field("model_type", schema::FieldType::String, "Model type")
            .required_field("hash", schema::FieldType::String, "SHA256 hash (hex)")
            .build();
    }
};

struct ModelPathsResponse {
    static schema::SchemaDescriptor schema() {
        return schema::SchemaBuilder("ModelPathsResponse", "Configured model storage paths")
            .optional_field("checkpoints", schema::FieldType::String, "Checkpoint models directory")
            .optional_field("diffusion_models", schema::FieldType::String, "Diffusion models directory")
            .optional_field("vae", schema::FieldType::String, "VAE models directory")
            .optional_field("lora", schema::FieldType::String, "LoRA models directory")
            .optional_field("clip", schema::FieldType::String, "CLIP models directory")
            .optional_field("t5", schema::FieldType::String, "T5 models directory")
            .optional_field("embeddings", schema::FieldType::String, "Embeddings directory")
            .optional_field("controlnet", schema::FieldType::String, "ControlNet models directory")
            .optional_field("llm", schema::FieldType::String, "LLM models directory")
            .optional_field("esrgan", schema::FieldType::String, "ESRGAN models directory")
            .optional_field("taesd", schema::FieldType::String, "TAESD models directory")
            .build();
    }
};

struct DownloadModelRequest {
    static schema::SchemaDescriptor schema() {
        return schema::SchemaBuilder("DownloadModelRequest", "Download a model from external source")
            .required_enum("model_type", "Target model type category", MODEL_TYPE_VALUES)
            .enum_field("source", "Download source", {"url", "civitai", "huggingface"})
            .optional_field("model_id", schema::FieldType::String, "CivitAI model ID (format: id or id:version)")
            .optional_field("url", schema::FieldType::String, "Direct download URL")
            .optional_field("repo_id", schema::FieldType::String, "HuggingFace repository ID")
            .optional_field("filename", schema::FieldType::String, "Target filename")
            .optional_field("subfolder", schema::FieldType::String, "Subfolder within HF repo")
            .optional_field("revision", schema::FieldType::String, "Git revision for HF repo", "main")
            .build();
    }
};

struct DownloadModelResponse {
    static schema::SchemaDescriptor schema() {
        return schema::SchemaBuilder("DownloadModelResponse", "Download job created")
            .required_field("success", schema::FieldType::Boolean, "Whether download was initiated")
            .required_field("download_job_id", schema::FieldType::String, "Download job UUID")
            .optional_field("hash_job_id", schema::FieldType::String, "Hash computation job UUID")
            .required_field("source", schema::FieldType::String, "Download source used")
            .required_field("model_type", schema::FieldType::String, "Target model type")
            .optional_field("position", schema::FieldType::Integer, "Queue position")
            .build();
    }
};

struct CivitaiInfoResponse {
    static schema::SchemaDescriptor schema() {
        return schema::SchemaBuilder("CivitaiInfoResponse", "CivitAI model metadata")
            .required_field("success", schema::FieldType::Boolean, "Whether lookup succeeded")
            .optional_field("model_id", schema::FieldType::Integer, "CivitAI model ID")
            .optional_field("version_id", schema::FieldType::Integer, "CivitAI version ID")
            .optional_field("name", schema::FieldType::String, "Model name")
            .optional_field("version_name", schema::FieldType::String, "Version name")
            .optional_field("type", schema::FieldType::String, "Model type")
            .optional_field("base_model", schema::FieldType::String, "Base model architecture")
            .optional_field("filename", schema::FieldType::String, "Primary file name")
            .optional_field("file_size", schema::FieldType::Integer, "File size in bytes")
            .optional_field("sha256", schema::FieldType::String, "SHA256 hash")
            .optional_field("download_url", schema::FieldType::String, "Download URL")
            .build();
    }
};

struct HuggingfaceInfoResponse {
    static schema::SchemaDescriptor schema() {
        return schema::SchemaBuilder("HuggingfaceInfoResponse", "HuggingFace model file metadata")
            .required_field("success", schema::FieldType::Boolean, "Whether lookup succeeded")
            .optional_field("repo_id", schema::FieldType::String, "Repository ID")
            .optional_field("filename", schema::FieldType::String, "File name")
            .optional_field("revision", schema::FieldType::String, "Git revision")
            .optional_field("file_size", schema::FieldType::Integer, "File size in bytes")
            .optional_field("download_url", schema::FieldType::String, "Download URL")
            .build();
    }
};

struct LoadUpscalerRequest {
    static schema::SchemaDescriptor schema() {
        return schema::SchemaBuilder("LoadUpscalerRequest", "Load an ESRGAN upscaler model")
            .required_field("model_name", schema::FieldType::String, "ESRGAN model name")
            .optional_field("n_threads", schema::FieldType::Integer, "CPU threads (-1 for auto)", -1)
            .optional_field("tile_size", schema::FieldType::Integer, "Processing tile size", 128)
            .build();
    }
};

} // namespace api
} // namespace sdcpp
