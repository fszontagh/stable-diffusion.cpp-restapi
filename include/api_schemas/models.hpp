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
            .optional_field("uncond_diffusion_model", schema::FieldType::String, "Unconditional diffusion model (leejet master post-1ceb5bd)")
            .optional_field("photo_maker", schema::FieldType::String, "PhotoMaker model name")
            .optional_field("pulid_weights", schema::FieldType::String, "PuLID-Flux identity-injection weights (leejet PR #1595). Looked up under the checkpoints directory.")
            .optional_field("audio_vae", schema::FieldType::String, "Audio VAE (LTXAV / LTX 2.3 — for video models that produce sound)")
            .optional_field("embeddings_connectors", schema::FieldType::String, "Embeddings connectors (LTXAV / LTX 2.3)")
            .ref_field("options", "LoadOptions", "Model loading options");
        return builder.build();
    }
};

struct LoadOptions {
    static schema::SchemaDescriptor schema() {
        auto builder = schema::SchemaBuilder("LoadOptions", "Model loading options");
        builder
            .optional_field("n_threads", schema::FieldType::Integer, "Number of CPU threads (-1 for auto)", -1)
            .optional_field("vae_conv_direct", schema::FieldType::Boolean, "Direct VAE convolution", false)
            .optional_field("diffusion_conv_direct", schema::FieldType::Boolean, "Direct diffusion convolution", false)
            .optional_field("flash_attn", schema::FieldType::Boolean, "Enable flash attention for CLIP/T5/conditioner", true)
            .optional_field("diffusion_flash_attn", schema::FieldType::Boolean, "Enable flash attention specifically for the diffusion model (UNet/DiT/Flux)", false)
            .optional_field("enable_mmap", schema::FieldType::Boolean, "Enable memory-mapped file access", true)
            .optional_field("tae_preview_only", schema::FieldType::Boolean, "Use TAESD for preview only", false)
            .optional_field("max_vram", schema::FieldType::Number, "GiB budget for graph-cut segmented param offload (0 = disabled)", 0)
            .enum_field("weight_type", "Weight precision type", WEIGHT_TYPE_VALUES)
            .optional_field("tensor_type_rules", schema::FieldType::String, "Custom tensor type rules string")
            .enum_field("rng_type", "Random number generator type", RNG_TYPE_VALUES, "cuda")
            .optional_field("sampler_rng_type", schema::FieldType::String, "Sampler-specific RNG type")
            .enum_field("prediction", "Prediction type", {"eps", "v", "edm_v", "sd3_flow", "flux_flow", "flux2_flow", ""})
            .enum_field("lora_apply_mode", "LoRA application mode", {"auto", "immediately", "at_runtime"}, "auto")
            .optional_field("chroma_use_dit_mask", schema::FieldType::Boolean, "Use DiT attention mask (Chroma)", false)
            .optional_field("chroma_use_t5_mask", schema::FieldType::Boolean, "Use T5 attention mask (Chroma)", false)
            .optional_field("chroma_t5_mask_pad", schema::FieldType::Integer, "T5 mask padding (Chroma)", 0)
            .optional_field("qwen_image_zero_cond_t", schema::FieldType::Boolean, "Qwen-Image: zero the conditional T branch (Qwen-Image checkpoints only)", false)
            .enum_field("vae_format", "VAE weight format override (auto = sd.cpp detects from the file)", {"auto", "flux", "sd3", "flux2"}, "auto")
            .optional_field("circular_x", schema::FieldType::Boolean, "Circular RoPE on the X axis — produces seamless/tileable output across the horizontal seam. Required for Ideogram4-style tileable-texture workflows.", false)
            .optional_field("circular_y", schema::FieldType::Boolean, "Circular RoPE on the Y axis — produces seamless/tileable output across the vertical seam.", false)
            .optional_field("backend", schema::FieldType::String, "Main compute backend override (empty = sd.cpp picks). Use per-component placement here too — e.g. \"diffusion=cuda0,vae=cpu\" — that's how per-component CPU keeping is expressed now (formerly keep_clip_on_cpu / keep_vae_on_cpu / keep_controlnet_on_cpu).")
            .optional_field("params_backend", schema::FieldType::String, "Parameter storage backend override (empty = same as backend). Set to \"*=cpu\" for the global \"keep all weights in RAM\" mode that was previously offload_to_cpu.")
            .optional_field("rpc_servers", schema::FieldType::String, "RPC distributed-backend node list, comma-separated host:port pairs (leejet PR #1629). Empty = no RPC.")
#if defined(SDCPP_EXPERIMENTAL_OFFLOAD) && !defined(SDCPP_UNIFIED_STREAMING)
            // ── feature/vram-offloading-v2 fields (legacy multi-mode API) ──
            .enum_field("offload_mode", "VRAM offload strategy", OFFLOAD_MODE_VALUES, "none")
            .enum_field("vram_estimation", "VRAM estimation method", VRAM_ESTIMATION_VALUES, "dryrun")
            .optional_field("offload_cond_stage", schema::FieldType::Boolean, "Offload conditioning (LLM/CLIP/T5) to CPU after the prompt is encoded. Default is true for most offload modes; the restapi flips it to false when offload_mode=layer_streaming to avoid per-gen mmap fault-in of the encoder weights.", false)
            .optional_field("offload_diffusion", schema::FieldType::Boolean, "Offload diffusion after sampling", false)
            .optional_field("reload_cond_stage", schema::FieldType::Boolean, "Reload conditioning for next gen", true)
            .optional_field("reload_diffusion", schema::FieldType::Boolean, "Reload diffusion for next gen", true)
            .optional_field("log_offload_events", schema::FieldType::Boolean, "Log offload events", false)
            .optional_field("min_offload_size_mb", schema::FieldType::Integer, "Minimum component size to offload (MB)", 0)
            .optional_field("target_free_vram_mb", schema::FieldType::Integer, "Target free VRAM before VAE (MB)", 0)
            .optional_field("streaming_prefetch_layers", schema::FieldType::Integer, "Layers to prefetch ahead", 1)
            .optional_field("streaming_keep_layers_behind", schema::FieldType::Integer, "Layers to keep after execution", 0)
            .optional_field("streaming_min_free_vram_mb", schema::FieldType::Integer, "Min free VRAM during streaming (MB)", 0)
#else
            // ── feature/unified-streaming field (new minimal API) ──────────
            .optional_field("stream_layers", schema::FieldType::Boolean, "Engage residency+async-prefetch streaming on top of max_vram. Requires max_vram > 0; no effect when max_vram == 0. sd.cpp's planner picks the residency split automatically and overlaps next-segment H2D with current-segment compute.", false)
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

// Response for POST /models/upload (multipart/form-data).
// NOTE: The request itself is multipart and is registered without a JSON
// body schema — the OpenAPI spec currently only documents this response.
// TODO: enrich the OpenAPI registration to declare a multipart/form-data
// request body once SchemaBuilder grows multipart support.
struct UploadModelResponse {
    static schema::SchemaDescriptor schema() {
        return schema::SchemaBuilder("UploadModelResponse", "Result of a model file upload")
            .required_field("success", schema::FieldType::Boolean, "Whether the upload succeeded")
            .required_field("filename", schema::FieldType::String, "Stored filename")
            .required_field("model_type", schema::FieldType::String, "Model type the file was stored under")
            .required_field("size_bytes", schema::FieldType::Integer, "Size of the stored file in bytes")
            .required_field("full_path", schema::FieldType::String, "Absolute path of the stored file on the server")
            .optional_field("subfolder", schema::FieldType::String, "Subfolder under the model_type directory (if any)")
            .build();
    }
};

} // namespace api
} // namespace sdcpp
