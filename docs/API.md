# SDCPP-RESTAPI API Documentation

REST API for stable-diffusion.cpp image and video generation.

**Base URL:** `http://<host>:<port>`
**Default:** `http://localhost:8080`

**Content-Type:** All requests and responses use `application/json`

---

## Table of Contents

- [Health Check](#health-check)
- [Memory](#memory)
- [Model Management](#model-management)
  - [List Models](#list-models)
  - [Refresh Models](#refresh-models)
  - [Load Model](#load-model)
  - [Unload Model](#unload-model)
  - [Get Model Hash](#get-model-hash)
- [Image Generation](#image-generation)
  - [Text to Image](#text-to-image)
  - [Image to Image](#image-to-image)
  - [Text to Video](#text-to-video)
  - [Upscale](#upscale)
- [Upscaler Management](#upscaler-management)
  - [Load Upscaler](#load-upscaler)
  - [Unload Upscaler](#unload-upscaler)
- [Queue Management](#queue-management)
  - [List Queue](#list-queue)
  - [Get Job Status](#get-job-status)
  - [Cancel Job](#cancel-job)
  - [Bulk Delete Jobs](#bulk-delete-jobs)
  - [Get Job Preview](#get-job-preview)
- [Recycle Bin](#recycle-bin)
  - [List Recycle Bin](#list-recycle-bin)
  - [Restore Job](#restore-job)
  - [Purge Job](#purge-job)
  - [Clear Recycle Bin](#clear-recycle-bin)
  - [Get Recycle Bin Settings](#get-recycle-bin-settings)
- [Preview Settings](#preview-settings)
  - [Get Preview Settings](#get-preview-settings)
  - [Update Preview Settings](#update-preview-settings)
- [Settings](#settings)
  - [Get All Generation Defaults](#get-all-generation-defaults)
  - [Update All Generation Defaults](#update-all-generation-defaults)
  - [Get Generation Defaults for Mode](#get-generation-defaults-for-mode)
  - [Update Generation Defaults for Mode](#update-generation-defaults-for-mode)
  - [Get UI Preferences](#get-ui-preferences)
  - [Update UI Preferences](#update-ui-preferences)
  - [Reset Settings](#reset-settings)
- [File Browser & Output Serving](#file-browser--output-serving)
- [Server Options](#server-options)
  - [Get Options](#get-options)
  - [Get Option Descriptions](#get-option-descriptions)
- [Model Conversion](#model-conversion)
- [Assistant Integration](#assistant-integration)
  - [Chat with Assistant](#chat-with-assistant)
  - [Chat with Assistant (Streaming)](#chat-with-assistant-streaming)
  - [Get Assistant History](#get-assistant-history)
  - [Clear Assistant History](#clear-assistant-history)
  - [Get Assistant Status](#get-assistant-status)
  - [Get Assistant Settings](#get-assistant-settings)
  - [Update Assistant Settings](#update-assistant-settings)
  - [Get Assistant Model Info](#get-assistant-model-info)
- [Model Architecture](#model-architecture)
  - [Get Architectures](#get-architectures)
  - [Detect Architecture](#detect-architecture)
- [Model Downloads](#model-downloads)
  - [Download Model](#download-model)
  - [Get CivitAI Model Info](#get-civitai-model-info)
  - [Get HuggingFace Model Info](#get-huggingface-model-info)
  - [Get Model Paths](#get-model-paths)
- [WebSocket Support](#websocket-support)
- [Error Responses](#error-responses)
- [Data Types](#data-types)

---

## Health Check

### `GET /health`

Check server status, loaded model, and all loaded components.

**Response:**

```json
{
    "status": "ok",
    "version": "1.2.3",
    "git_commit": "abc1234",
    "model_loaded": true,
    "model_loading": false,
    "loading_model_name": null,
    "loading_step": 0,
    "loading_total_steps": 0,
    "last_error": null,
    "model_name": "z_image_turbo-Q8_0.gguf",
    "model_type": "diffusion",
    "model_architecture": "Z-Image",
    "loaded_components": {
        "vae": "ae_q8_0.gguf",
        "clip_l": null,
        "clip_g": null,
        "t5xxl": null,
        "controlnet": null,
        "llm": "qwen_3_4b.Q8_0.gguf",
        "llm_vision": null
    },
    "upscaler_loaded": false,
    "upscaler_name": null,
    "ws_port": 8081,
    "memory": {
        "system": {
            "total_bytes": 34359738368,
            "used_bytes": 17179869184,
            "free_bytes": 17179869184,
            "total_mb": 32768,
            "used_mb": 16384,
            "free_mb": 16384,
            "usage_percent": 50.0
        },
        "process": {
            "rss_bytes": 1073741824,
            "virtual_bytes": 2147483648,
            "rss_mb": 1024,
            "virtual_mb": 2048
        },
        "gpu": {
            "available": true,
            "name": "NVIDIA GeForce RTX 4090",
            "total_bytes": 25769803776,
            "used_bytes": 8589934592,
            "free_bytes": 17179869184,
            "total_mb": 24576,
            "used_mb": 8192,
            "free_mb": 16384,
            "usage_percent": 33.3
        }
    },
    "features": {
        "experimental_offload": false
    }
}
```

**Response during model loading:**

```json
{
    "status": "ok",
    "version": "1.2.3",
    "git_commit": "abc1234",
    "model_loaded": false,
    "model_loading": true,
    "loading_model_name": "flux1-dev-Q4_K_S.gguf",
    "loading_step": 3,
    "loading_total_steps": 10,
    "last_error": null,
    "model_name": null,
    "model_type": null,
    "model_architecture": null,
    "loaded_components": {},
    "upscaler_loaded": false,
    "upscaler_name": null,
    "ws_port": 8081,
    "memory": { "..." : "..." },
    "features": {
        "experimental_offload": false
    }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `status` | string | Always `"ok"` if server is running |
| `version` | string | Server version string |
| `git_commit` | string | Git commit hash of the build |
| `model_loaded` | boolean | Whether a model is currently loaded |
| `model_loading` | boolean | Whether a model is currently being loaded |
| `loading_model_name` | string\|null | Name of model being loaded, or null if not loading |
| `loading_step` | integer | Current loading step (0 if not loading) |
| `loading_total_steps` | integer | Total loading steps (0 if not loading) |
| `last_error` | string\|null | Last model load error message, or null if no error |
| `model_name` | string\|null | Name of loaded model, or null if none |
| `model_type` | string\|null | Type of loaded model (`checkpoint` or `diffusion`), or null |
| `model_architecture` | string\|null | Auto-detected architecture (e.g., `Z-Image`, `Flux`, `SDXL`), or null |
| `loaded_components` | object | Component models loaded with the main model |
| `loaded_components.vae` | string\|null | Loaded VAE model name |
| `loaded_components.clip_l` | string\|null | Loaded CLIP-L model name |
| `loaded_components.clip_g` | string\|null | Loaded CLIP-G model name |
| `loaded_components.t5xxl` | string\|null | Loaded T5-XXL model name |
| `loaded_components.controlnet` | string\|null | Loaded ControlNet model name |
| `loaded_components.llm` | string\|null | Loaded LLM model name |
| `loaded_components.llm_vision` | string\|null | Loaded LLM vision model name |
| `upscaler_loaded` | boolean | Whether an upscaler is currently loaded |
| `upscaler_name` | string\|null | Name of loaded upscaler model, or null |
| `ws_port` | integer\|null | WebSocket port for real-time updates, or null if disabled |
| `memory` | object | System, process, and GPU memory information (see [Memory](#memory)) |
| `features` | object | Feature flags |
| `features.experimental_offload` | boolean | Whether experimental VRAM offloading is compiled in |

---

## Memory

### `GET /memory`

Get detailed system, process, and GPU memory information.

**Response:**

```json
{
    "system": {
        "total_bytes": 34359738368,
        "used_bytes": 17179869184,
        "free_bytes": 17179869184,
        "total_mb": 32768,
        "used_mb": 16384,
        "free_mb": 16384,
        "usage_percent": 50.0
    },
    "process": {
        "rss_bytes": 1073741824,
        "virtual_bytes": 2147483648,
        "rss_mb": 1024,
        "virtual_mb": 2048
    },
    "gpu": {
        "available": true,
        "name": "NVIDIA GeForce RTX 4090",
        "total_bytes": 25769803776,
        "used_bytes": 8589934592,
        "free_bytes": 17179869184,
        "total_mb": 24576,
        "used_mb": 8192,
        "free_mb": 16384,
        "usage_percent": 33.3
    }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `system.total_bytes` | integer | Total system RAM in bytes |
| `system.used_bytes` | integer | Used system RAM in bytes |
| `system.free_bytes` | integer | Free system RAM in bytes |
| `system.total_mb` | integer | Total system RAM in MB |
| `system.used_mb` | integer | Used system RAM in MB |
| `system.free_mb` | integer | Free system RAM in MB |
| `system.usage_percent` | float | System RAM usage percentage (0-100) |
| `process.rss_bytes` | integer | Process Resident Set Size in bytes |
| `process.virtual_bytes` | integer | Process virtual memory in bytes |
| `process.rss_mb` | integer | Process RSS in MB |
| `process.virtual_mb` | integer | Process virtual memory in MB |
| `gpu.available` | boolean | Whether GPU info is available |
| `gpu.name` | string | GPU device name |
| `gpu.total_bytes` | integer | Total VRAM in bytes |
| `gpu.used_bytes` | integer | Used VRAM in bytes |
| `gpu.free_bytes` | integer | Free VRAM in bytes |
| `gpu.total_mb` | integer | Total VRAM in MB |
| `gpu.used_mb` | integer | Used VRAM in MB |
| `gpu.free_mb` | integer | Free VRAM in MB |
| `gpu.usage_percent` | float | VRAM usage percentage (0-100) |

---

## Model Management

### List Models

#### `GET /models`

List all available models organized by type. Supports optional filtering via query parameters.

**Query Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `type` | string | Filter by model type: `checkpoint`, `diffusion`, `vae`, `lora`, `clip`, `t5`, `embedding`, `controlnet`, `llm`, `esrgan` |
| `extension` | string | Filter by file extension (e.g., `safetensors`, `gguf`, `ckpt`). Leading dot is optional. |
| `search` | string | Search in model name (case-insensitive substring match) |
| `name` | string | Alias for `search` parameter |

**Example Requests:**

```
GET /models                                    # All models
GET /models?type=checkpoint                    # Only checkpoints
GET /models?extension=gguf                     # Only GGUF files
GET /models?search=flux                        # Models containing "flux" in name
GET /models?type=diffusion&extension=gguf      # Diffusion models in GGUF format
GET /models?type=lora&search=detail            # LoRAs with "detail" in name
```

**Response (without filters):**

```json
{
    "checkpoints": [
        {
            "name": "SD1x/realisticStockPhoto_v30SD15.safetensors",
            "type": "checkpoint",
            "file_extension": ".safetensors",
            "size_bytes": 2132658480,
            "hash": null,
            "is_loaded": false
        }
    ],
    "diffusion_models": [ ... ],
    "vae": [ ... ],
    "loras": [ ... ],
    "clip": [ ... ],
    "t5": [ ... ],
    "controlnets": [ ... ],
    "llm": [ ... ],
    "esrgan": [ ... ],
    "taesd": [ ... ],
    "embeddings": [],
    "loaded_model": null,
    "loaded_model_type": null
}
```

**Response (with filters applied):**

When filters are applied, the response includes an `applied_filters` object:

```json
{
    "checkpoints": [],
    "diffusion_models": [ ... ],
    "...": "...",
    "loaded_model": null,
    "loaded_model_type": null,
    "applied_filters": {
        "type": "diffusion",
        "search": "flux"
    }
}
```

**Model Object:**

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Model name (relative path from model directory) |
| `type` | string | Model type: `checkpoint`, `diffusion`, `vae`, `lora`, `clip`, `t5`, `embedding`, `controlnet`, `llm`, `esrgan`, `taesd` |
| `file_extension` | string | File extension with dot (e.g., `.safetensors`, `.gguf`) |
| `size_bytes` | integer | File size in bytes |
| `hash` | string\|null | SHA256 hash (null until computed) |
| `is_loaded` | boolean | Whether this model is currently loaded (only for checkpoint/diffusion) |

**Response Fields:**

| Field | Type | Description |
|-------|------|-------------|
| `checkpoints` | array | Checkpoint models |
| `diffusion_models` | array | Diffusion models |
| `vae` | array | VAE models |
| `loras` | array | LoRA models |
| `clip` | array | CLIP models |
| `t5` | array | T5 models |
| `controlnets` | array | ControlNet models |
| `llm` | array | LLM models for multimodal |
| `esrgan` | array | ESRGAN upscaler models |
| `taesd` | array | TAESD preview models |
| `embeddings` | array | Textual inversion embeddings |
| `loaded_model` | string\|null | Currently loaded model name |
| `loaded_model_type` | string\|null | Type of loaded model |
| `applied_filters` | object | (Optional) Shows which filters were applied |

---

### Refresh Models

#### `POST /models/refresh`

Force a rescan of model directories.

**Request Body:** None required

**Response (200):**

```json
{
    "success": true,
    "message": "Model list refreshed",
    "models": [ ... ]
}
```

---

### Load Model

#### `POST /models/load`

Load a model into memory. Only one model can be loaded at a time.

**Request Body:**

```json
{
    "model_name": "SD1x/realisticStockPhoto_v30SD15.safetensors",
    "model_type": "checkpoint",
    "vae": "vae-ft-mse-840000-ema-pruned.safetensors",
    "clip_l": "clip_l.safetensors",
    "clip_g": "clip_g.safetensors",
    "clip_vision": null,
    "t5xxl": "t5xxl_fp16.safetensors",
    "controlnet": "control_v11p_sd15_canny.safetensors",
    "llm": "qwen_3_4b.Q8_0.gguf",
    "llm_vision": null,
    "taesd": null,
    "high_noise_diffusion_model": null,
    "photo_maker": null,
    "options": {
        "n_threads": 8,
        "keep_clip_on_cpu": true,
        "keep_vae_on_cpu": false,
        "keep_controlnet_on_cpu": false,
        "flash_attn": true,
        "offload_to_cpu": false,
        "vae_decode_only": true,
        "flow_shift": 0.0,
        "rng_type": "cuda",
        "prediction": "",
        "lora_apply_mode": "auto"
    }
}
```

**Component Parameters:**

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `model_name` | string | Yes | - | Model name from `/models` endpoint |
| `model_type` | string | No | `"checkpoint"` | `"checkpoint"` or `"diffusion"` |
| `vae` | string | No | null | VAE model name to use |
| `clip_l` | string | No | null | CLIP-L model name (for Flux/SD3) |
| `clip_g` | string | No | null | CLIP-G model name (for SD3) |
| `clip_vision` | string | No | null | CLIP vision encoder for IP-Adapter |
| `t5xxl` | string | No | null | T5-XXL model name (for Flux/SD3) |
| `controlnet` | string | No | null | ControlNet model name |
| `llm` | string | No | null | LLM model name for multimodal (e.g., Qwen) |
| `llm_vision` | string | No | null | LLM vision model name (optional) |
| `taesd` | string | No | null | TAESD model name for preview |
| `high_noise_diffusion_model` | string | No | null | High-noise diffusion model for MoE |
| `photo_maker` | string | No | null | PhotoMaker model path |

**Load Options (`options` object):**

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `n_threads` | integer | -1 (auto) | Number of CPU threads |
| `keep_clip_on_cpu` | boolean | true | Keep CLIP on CPU to save VRAM |
| `keep_vae_on_cpu` | boolean | false | Keep VAE on CPU |
| `keep_controlnet_on_cpu` | boolean | false | Keep ControlNet on CPU |
| `vae_conv_direct` | boolean | false | Use ggml_conv2d_direct in VAE |
| `diffusion_conv_direct` | boolean | false | Use ggml_conv2d_direct in diffusion model |
| `flash_attn` | boolean | true | Enable Flash Attention |
| `offload_to_cpu` | boolean | false | Offload model to CPU |
| `enable_mmap` | boolean | true | Use memory-mapped file loading |
| `vae_decode_only` | boolean | true | VAE decode only mode |
| `tae_preview_only` | boolean | false | Only use TAESD for preview, not final |
| `free_params_immediately` | boolean | false | Free model params after loading |
| `flow_shift` | float | auto | Flow shift (INFINITY = auto-detect) |
| `weight_type` | string | "" | Weight type (f32, f16, q8_0, q5_0, q4_0) |
| `tensor_type_rules` | string | "" | Per-tensor weight rules (e.g., `"^vae\.=f16"`) |
| `rng_type` | string | "cuda" | RNG type: `std_default`, `cuda`, `cpu` |
| `sampler_rng_type` | string | "" | Sampler RNG (empty = use rng_type) |
| `prediction` | string | "" | Prediction type override (`eps`, `v`, `edm_v`, `sd3_flow`, `flux_flow`, `flux2_flow`, or empty for auto) |
| `lora_apply_mode` | string | "auto" | LoRA apply mode: `auto`, `immediately`, `at_runtime` |
| `vae_tiling` | boolean | false | Enable VAE tiling for large images |
| `vae_tile_size_x` | integer | 0 | VAE tile size X (0 = auto) |
| `vae_tile_size_y` | integer | 0 | VAE tile size Y (0 = auto) |
| `vae_tile_overlap` | float | 0.5 | VAE tile overlap (0.0-1.0) |
| `chroma_use_dit_mask` | boolean | true | Chroma: use DiT mask |
| `chroma_use_t5_mask` | boolean | false | Chroma: use T5 mask |
| `chroma_t5_mask_pad` | integer | 1 | Chroma: T5 mask padding |

**Experimental Offload Options** (requires `-DSD_EXPERIMENTAL_OFFLOAD=ON` build):

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `offload_mode` | string | "none" | Offload mode: `none`, `cond_only`, `cond_diffusion`, `aggressive`, `layer_streaming` |
| `vram_estimation` | string | "formula" | VRAM estimation: `dryrun` (accurate), `formula` (fast) |
| `offload_cond_stage` | boolean | false | Offload LLM/CLIP after conditioning |
| `offload_diffusion` | boolean | false | Offload diffusion model after sampling |
| `reload_cond_stage` | boolean | false | Reload conditioning components after generation |
| `reload_diffusion` | boolean | false | Reload diffusion model after generation |
| `log_offload_events` | boolean | false | Log offload events for debugging |
| `min_offload_size_mb` | integer | 0 | Minimum component size to offload (MB) |
| `target_free_vram_mb` | integer | 0 | Target free VRAM before VAE decode (0 = always offload) |
| `layer_streaming_enabled` | boolean | false | Enable layer-by-layer streaming execution |
| `streaming_prefetch_layers` | integer | 1 | Number of layers to prefetch ahead |
| `streaming_keep_layers_behind` | integer | 0 | Layers to keep after execution (for skip connections) |
| `streaming_min_free_vram_mb` | integer | 0 | Minimum VRAM to keep free during streaming |

**Success Response (200):**

```json
{
    "success": true,
    "message": "Model loaded successfully",
    "model_name": "SD1x/realisticStockPhoto_v30SD15.safetensors",
    "model_type": "checkpoint",
    "loaded_components": {
        "vae": "vae-ft-mse-840000-ema-pruned.safetensors",
        "clip_l": "clip_l.safetensors",
        "clip_g": "clip_g.safetensors",
        "t5xxl": null,
        "controlnet": null,
        "llm": null,
        "llm_vision": null
    }
}
```

**Error Response (400):**

```json
{
    "error": "model_name is required"
}
```

---

### Unload Model

#### `POST /models/unload`

Unload the currently loaded model from memory.

**Request Body:** None required

**Success Response (200):**

```json
{
    "success": true,
    "message": "Model unloaded"
}
```

---

### Get Model Hash

#### `GET /models/hash/{model_type}/{model_name}`

Compute SHA256 hash of a model file. The hash is cached after first computation.

**URL Parameters:**

| Parameter | Description |
|-----------|-------------|
| `model_type` | One of: `checkpoint`, `diffusion`, `vae`, `lora`, `clip`, `t5`, `embedding`, `controlnet`, `llm`, `esrgan` |
| `model_name` | Model name (URL-encoded if contains `/`) |

**Example:**

```
GET /models/hash/checkpoint/SD1x%2FrealisticStockPhoto_v30SD15.safetensors
```

**Success Response (200):**

```json
{
    "model_name": "SD1x/realisticStockPhoto_v30SD15.safetensors",
    "model_type": "checkpoint",
    "hash": "a1b2c3d4e5f6789012345678901234567890abcdef1234567890abcdef123456"
}
```

**Error Response (404):**

```json
{
    "error": "Model not found: invalid_model.safetensors"
}
```

---

## Image Generation

All generation endpoints add jobs to a FIFO queue. Jobs are processed sequentially by a background worker.

### ControlNet Usage

ControlNet allows you to guide image generation with control images (edge maps, depth maps, poses, etc.).

**Setup:**

1. Load a ControlNet model with your main model using the `/models/load` endpoint:

```json
{
    "model_name": "v1-5-pruned.safetensors",
    "controlnet": "control_v11p_sd15_canny.safetensors"
}
```

2. Provide a pre-processed control image with your generation request:

```json
{
    "prompt": "a beautiful house",
    "control_image_base64": "<base64-encoded-canny-edge-image>",
    "control_strength": 0.9
}
```

**Important Notes:**

- You must pre-process your control images before sending them (e.g., apply Canny edge detection, extract depth maps, etc.)
- The control image will be automatically resized to match the output dimensions
- `control_strength` controls how much the ControlNet influences the output (0.0 = no influence, 1.0 = full influence)
- ControlNet must be loaded with the model - it cannot be changed per-request

### LoRA Usage

LoRAs are specified directly in the prompt using the sd.cpp syntax:

```
<lora:filename:weight>
```

**Examples:**
- `<lora:add_detail:0.8>` - Apply add_detail.safetensors with weight 0.8
- `<lora:style_anime:1.0>` - Apply style_anime.safetensors with weight 1.0

You can use multiple LoRAs in a single prompt:
```
a beautiful landscape <lora:add_detail:0.8> <lora:enhance_colors:0.5>
```

### Text to Image

#### `POST /txt2img`

Generate images from text prompt.

**Prerequisites:** A model must be loaded.

**Request Body:**

```json
{
    "prompt": "a lovely cat holding a sign that says hello world <lora:add_detail:0.8>",
    "negative_prompt": "blurry, low quality, bad anatomy",
    "width": 512,
    "height": 512,
    "steps": 20,
    "cfg_scale": 7.0,
    "seed": -1,
    "sampler": "euler_a",
    "scheduler": "discrete",
    "batch_count": 1,
    "clip_skip": -1,
    "control_image_base64": "/9j/4AAQSkZJRg...",
    "control_strength": 0.9
}
```

**Parameters:**

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `prompt` | string | Yes | - | Text prompt for generation (supports `<lora:name:weight>` syntax) |
| `negative_prompt` | string | No | `""` | Negative prompt |
| `width` | integer | No | 512 | Image width in pixels |
| `height` | integer | No | 512 | Image height in pixels |
| `steps` | integer | No | 20 | Number of sampling steps |
| `cfg_scale` | float | No | 7.0 | Classifier-free guidance scale |
| `distilled_guidance` | float | No | 3.5 | Distilled guidance for Flux/distilled models |
| `eta` | float | No | 0.0 | Eta for DDIM/TCD samplers |
| `shifted_timestep` | integer | No | 0 | Shifted timestep for NitroFusion (250-500) |
| `seed` | integer | No | -1 | Random seed (-1 for random) |
| `sampler` | string | No | `"euler_a"` | Sampling method (see [Samplers](#samplers)) |
| `scheduler` | string | No | `"discrete"` | Scheduler type (see [Schedulers](#schedulers)) |
| `batch_count` | integer | No | 1 | Number of images to generate |
| `clip_skip` | integer | No | -1 | CLIP skip layers (-1 for default) |
| `slg_scale` | float | No | 0.0 | Skip Layer Guidance scale (0 = disabled, 2.5 for SD3.5 medium) |
| `skip_layers` | array | No | [7,8,9] | Layers to skip for SLG |
| `slg_start` | float | No | 0.01 | SLG start percent |
| `slg_end` | float | No | 0.2 | SLG end percent |
| `custom_sigmas` | array | No | [] | Custom sigma schedule (overrides scheduler) |
| `ref_images` | array | No | [] | Array of base64-encoded reference images (Flux Kontext) |
| `auto_resize_ref_image` | boolean | No | true | Auto-resize reference images |
| `increase_ref_index` | boolean | No | false | Increase reference index |
| `control_image_base64` | string | No | - | Base64-encoded pre-processed control image (requires ControlNet) |
| `control_strength` | float | No | 0.9 | ControlNet influence strength (0.0 - 1.0) |
| `vae_tiling` | boolean | No | false | Enable VAE tiling for large images |
| `vae_tile_size_x` | integer | No | 0 | VAE tile X size (0 = auto) |
| `vae_tile_size_y` | integer | No | 0 | VAE tile Y size (0 = auto) |
| `vae_tile_overlap` | float | No | 0.5 | VAE tile overlap |
| `easycache` | boolean | No | false | Enable caching for DiT models (speeds up generation) |
| `easycache_threshold` | float | No | 0.2 | Cache reuse threshold (higher = more reuse, lower quality) |
| `easycache_start` | float | No | 0.15 | Cache start percent |
| `easycache_end` | float | No | 0.95 | Cache end percent |
| `pm_id_images` | array | No | [] | Array of base64-encoded PhotoMaker ID images |
| `pm_id_embed_path` | string | No | "" | Path to PhotoMaker ID embedding file |
| `pm_style_strength` | float | No | 20.0 | PhotoMaker style strength |
| `upscale` | boolean | No | false | Enable upscaling after generation (requires upscaler loaded) |
| `upscale_repeats` | integer | No | 1 | Number of times to run upscaler |
| `upscale_auto_unload` | boolean | No | true | Unload upscaler after use |

**Success Response (202 Accepted):**

```json
{
    "job_id": "550e8400-e29b-41d4-a716-446655440000",
    "status": "pending",
    "position": 1
}
```

**Error Response (400):**

```json
{
    "error": "No model loaded"
}
```

---

### Image to Image

#### `POST /img2img`

Generate images from an existing image and text prompt.

**Prerequisites:** A model must be loaded.

**Request Body:**

```json
{
    "prompt": "watercolor painting style <lora:watercolor:0.9>",
    "negative_prompt": "photo, realistic",
    "init_image_base64": "/9j/4AAQSkZJRg...",
    "strength": 0.75,
    "width": 512,
    "height": 512,
    "steps": 20,
    "cfg_scale": 7.0,
    "seed": -1,
    "sampler": "euler_a",
    "scheduler": "discrete",
    "batch_count": 1,
    "clip_skip": -1
}
```

**Additional Parameters:**

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `init_image_base64` | string | Yes | - | Base64-encoded input image (JPEG/PNG) |
| `strength` | float | No | 0.75 | Denoising strength (0.0 - 1.0) |
| `img_cfg_scale` | float | No | -1.0 | Image CFG scale for instruct-pix2pix (-1 = same as cfg_scale) |
| `mask_image_base64` | string | No | - | Base64-encoded mask image for inpainting (white = repaint, black = keep) |
| `control_image_base64` | string | No | - | Base64-encoded pre-processed control image (requires ControlNet) |
| `control_strength` | float | No | 0.9 | ControlNet influence strength (0.0 - 1.0) |

All other parameters are the same as [Text to Image](#text-to-image) including advanced guidance (SLG, distilled_guidance), reference images, VAE tiling, EasyCache, PhotoMaker, and upscaling.

**Success Response (202 Accepted):**

```json
{
    "job_id": "550e8400-e29b-41d4-a716-446655440001",
    "status": "pending",
    "position": 1
}
```

---

### Text to Video

#### `POST /txt2vid`

Generate video frames from text prompt (requires video-capable model like Wan).

**Prerequisites:** A video-capable model must be loaded.

**Request Body:**

```json
{
    "prompt": "a cat walking through a garden, cinematic",
    "negative_prompt": "static, blurry, low quality",
    "width": 832,
    "height": 480,
    "video_frames": 33,
    "steps": 30,
    "cfg_scale": 6.0,
    "seed": -1,
    "sampler": "euler",
    "scheduler": "discrete",
    "flow_shift": 3.0,
    "clip_skip": -1
}
```

**Parameters:**

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `prompt` | string | Yes | - | Text prompt for generation (supports `<lora:name:weight>` syntax) |
| `negative_prompt` | string | No | `""` | Negative prompt |
| `width` | integer | No | 832 | Video width in pixels |
| `height` | integer | No | 480 | Video height in pixels |
| `video_frames` | integer | No | 33 | Number of frames to generate |
| `fps` | integer | No | 16 | Video frames per second (metadata) |
| `steps` | integer | No | 30 | Number of sampling steps |
| `cfg_scale` | float | No | 6.0 | Classifier-free guidance scale |
| `distilled_guidance` | float | No | 3.5 | Distilled guidance for Flux/distilled models |
| `eta` | float | No | 0.0 | Eta for DDIM/TCD samplers |
| `seed` | integer | No | -1 | Random seed (-1 for random) |
| `sampler` | string | No | `"euler"` | Sampling method |
| `scheduler` | string | No | `"discrete"` | Scheduler type |
| `flow_shift` | float | No | 3.0 | Flow shift for video generation |
| `clip_skip` | integer | No | -1 | CLIP skip layers |
| `slg_scale` | float | No | 0.0 | Skip Layer Guidance scale |
| `skip_layers` | array | No | [7,8,9] | Layers to skip for SLG |
| `slg_start` | float | No | 0.01 | SLG start percent |
| `slg_end` | float | No | 0.2 | SLG end percent |
| `init_image_base64` | string | No | - | Base64-encoded init image (first frame for vid2vid) |
| `end_image_base64` | string | No | - | Base64-encoded end image (for FLF2V) |
| `strength` | float | No | 0.75 | Denoising strength for vid2vid |
| `control_image_base64` | string | No | - | Base64-encoded control image (applies to all frames) |
| `control_frames` | array | No | [] | Array of base64-encoded control frames |
| `high_noise_steps` | integer | No | -1 | High-noise phase steps (-1 = auto, for MoE models) |
| `high_noise_cfg_scale` | float | No | 7.0 | CFG scale for high-noise phase |
| `high_noise_sampler` | string | No | "" | Sampler for high-noise phase (empty = use main) |
| `high_noise_distilled_guidance` | float | No | 3.5 | Distilled guidance for high-noise phase |
| `high_noise_slg_scale` | float | No | 0.0 | SLG scale for high-noise phase |
| `high_noise_skip_layers` | array | No | [7,8,9] | Skip layers for high-noise SLG |
| `high_noise_slg_start` | float | No | 0.01 | High-noise SLG start percent |
| `high_noise_slg_end` | float | No | 0.2 | High-noise SLG end percent |
| `moe_boundary` | float | No | 0.875 | Timestep boundary for MoE models |
| `vace_strength` | float | No | 1.0 | WAN VACE strength |
| `easycache` | boolean | No | false | Enable caching for DiT models |
| `easycache_threshold` | float | No | 0.2 | Cache reuse threshold |
| `easycache_start` | float | No | 0.15 | Cache start percent |
| `easycache_end` | float | No | 0.95 | Cache end percent |

**Success Response (202 Accepted):**

```json
{
    "job_id": "550e8400-e29b-41d4-a716-446655440002",
    "status": "pending",
    "position": 1
}
```

---

### Upscale

#### `POST /upscale`

Upscale an image using ESRGAN. Requires an upscaler model to be loaded.

**Request Body:**

```json
{
    "image_base64": "/9j/4AAQSkZJRg...",
    "upscale_factor": 4,
    "tile_size": 128,
    "repeats": 1
}
```

**Parameters:**

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `image_base64` | string | Yes | - | Base64-encoded input image |
| `upscale_factor` | integer | No | 4 | Target upscale factor |
| `tile_size` | integer | No | 128 | Tile size for processing (VRAM optimization) |
| `repeats` | integer | No | 1 | Run upscaler multiple times |

**Success Response (202 Accepted):**

```json
{
    "job_id": "550e8400-e29b-41d4-a716-446655440003",
    "status": "pending",
    "position": 1
}
```

---

## Upscaler Management

### Load Upscaler

#### `POST /upscaler/load`

Load an ESRGAN upscaler model.

**Request Body:**

```json
{
    "model_name": "RealESRGAN_x4plus.pth",
    "n_threads": -1,
    "tile_size": 128
}
```

**Parameters:**

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `model_name` | string | Yes | - | ESRGAN model name from `/models` |
| `n_threads` | integer | No | -1 (auto) | Number of CPU threads |
| `tile_size` | integer | No | 128 | Tile size for processing |

**Success Response (200):**

```json
{
    "success": true,
    "message": "Upscaler loaded successfully",
    "model_name": "RealESRGAN_x4plus.pth",
    "upscale_factor": 4
}
```

---

### Unload Upscaler

#### `POST /upscaler/unload`

Unload the currently loaded upscaler model.

**Request Body:** None required

**Success Response (200):**

```json
{
    "success": true,
    "message": "Upscaler unloaded"
}
```

---

## Queue Management

### List Queue

#### `GET /queue`

Get jobs with filtering, pagination, and optional date grouping.

**Query Parameters:**

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `status` | string | `"all"` | Filter by status: `pending`, `processing`, `completed`, `failed`, `cancelled`, `all` |
| `type` | string | `"all"` | Filter by type: `txt2img`, `img2img`, `txt2vid`, `upscale`, `convert`, `model_download`, `model_hash`, `all` |
| `search` | string | - | Search in prompt/negative_prompt (case-insensitive) |
| `architecture` | string | - | Filter by model architecture (case-insensitive partial match) |
| `limit` | integer | 20 | Maximum items per page |
| `page` | integer | 1 | Page number (1-based) |
| `offset` | integer | 0 | Items to skip (alternative to page-based pagination) |
| `before` | integer | - | Items created before this Unix timestamp |
| `after` | integer | - | Items created after this Unix timestamp |
| `group_by` | string | - | Group response by `date` |

**Response (standard pagination):**

```json
{
    "pending_count": 2,
    "processing_count": 1,
    "completed_count": 5,
    "failed_count": 0,
    "cancelled_count": 0,
    "total_count": 8,
    "filtered_count": 8,
    "offset": 0,
    "limit": 20,
    "has_more": false,
    "page": 1,
    "total_pages": 1,
    "has_prev": false,
    "newest_timestamp": 1704067260,
    "oldest_timestamp": 1704067200,
    "applied_filters": {},
    "items": [
        {
            "job_id": "550e8400-e29b-41d4-a716-446655440000",
            "type": "txt2img",
            "status": "completed",
            "progress": {
                "step": 20,
                "total_steps": 20
            },
            "created_at": "2024-01-01T12:00:00Z",
            "started_at": "2024-01-01T12:00:05Z",
            "completed_at": "2024-01-01T12:01:00Z",
            "outputs": [
                "550e8400-e29b-41d4-a716-446655440000/image_0.png"
            ],
            "params": {
                "prompt": "a lovely cat",
                "width": 512,
                "height": 512
            },
            "model_settings": {
                "model_name": "v1-5-pruned.safetensors",
                "model_architecture": "SD1"
            }
        }
    ]
}
```

**Response (with `group_by=date`):**

```json
{
    "groups": [
        {
            "date": "2024-01-01",
            "label": "Today",
            "timestamp": 1704067200,
            "count": 3,
            "items": [ ... ]
        }
    ],
    "total_count": 5,
    "page": 1,
    "total_pages": 2,
    "limit": 20,
    "has_more": true,
    "has_prev": false,
    "group_by": "date",
    "applied_filters": {}
}
```

**Queue Status Fields:**

| Field | Type | Description |
|-------|------|-------------|
| `pending_count` | integer | Jobs waiting to be processed |
| `processing_count` | integer | Jobs currently being processed (0 or 1) |
| `completed_count` | integer | Successfully completed jobs |
| `failed_count` | integer | Failed jobs |
| `cancelled_count` | integer | Cancelled jobs |
| `total_count` | integer | Total jobs in history |
| `filtered_count` | integer | Total matching the current filter |
| `offset` | integer | Current pagination offset |
| `limit` | integer | Current page size limit |
| `has_more` | boolean | Whether more items exist after current page |
| `page` | integer | Current page number |
| `total_pages` | integer | Total number of pages |
| `has_prev` | boolean | Whether previous pages exist |
| `newest_timestamp` | integer | Unix timestamp of newest item |
| `oldest_timestamp` | integer | Unix timestamp of oldest item |
| `applied_filters` | object | Active filter values |

---

### Get Job Status

#### `GET /queue/{job_id}`

Get status of a specific job.

**URL Parameters:**

| Parameter | Description |
|-----------|-------------|
| `job_id` | Job UUID |

**Success Response (200):**

```json
{
    "job_id": "550e8400-e29b-41d4-a716-446655440000",
    "type": "txt2img",
    "status": "completed",
    "progress": {
        "step": 20,
        "total_steps": 20
    },
    "created_at": "2024-01-01T12:00:00Z",
    "started_at": "2024-01-01T12:00:05Z",
    "completed_at": "2024-01-01T12:01:00Z",
    "outputs": [
        "550e8400-e29b-41d4-a716-446655440000/image_0.png"
    ],
    "params": {
        "prompt": "a lovely cat",
        "negative_prompt": "",
        "width": 512,
        "height": 512,
        "steps": 20,
        "cfg_scale": 7.0,
        "seed": 12345,
        "sampler": "euler_a",
        "scheduler": "discrete",
        "batch_count": 1,
        "clip_skip": -1
    },
    "model_settings": {
        "model_name": "v1-5-pruned.safetensors",
        "model_architecture": "SD1",
        "clip_l_model": null,
        "clip_g_model": null,
        "t5xxl_model": null,
        "vae_model": null,
        "controlnet_model": null,
        "lora_model": null
    }
}
```

**Job Object Fields:**

| Field | Type | Description |
|-------|------|-------------|
| `job_id` | string | Unique job identifier (UUID) |
| `type` | string | Job type: `txt2img`, `img2img`, `txt2vid`, `upscale`, `convert`, `model_download`, `model_hash` |
| `status` | string | Job status (see [Job Status](#job-status)) |
| `progress` | object | Current progress information |
| `progress.step` | integer | Current step within current image |
| `progress.total_steps` | integer | Total steps per image |
| `created_at` | string | ISO 8601 timestamp when job was created |
| `started_at` | string | ISO 8601 timestamp when processing started (if applicable) |
| `completed_at` | string | ISO 8601 timestamp when job completed (if applicable) |
| `outputs` | array | List of output file paths (relative to `/output/`) |
| `params` | object | Original request parameters |
| `model_settings` | object | Model configuration at job creation time |
| `linked_job_id` | string | ID of linked job (e.g., hash job linked to download job) |
| `error` | string | Error message (only if status is `failed`) |

**Error Response (404):**

```json
{
    "error": "Job not found"
}
```

---

### Cancel Job

#### `DELETE /queue/{job_id}`

Cancel a pending job. Only jobs with `pending` status can be cancelled.

**URL Parameters:**

| Parameter | Description |
|-----------|-------------|
| `job_id` | Job UUID |

**Success Response (200):**

```json
{
    "success": true,
    "message": "Job cancelled"
}
```

**Error Response (400):**

```json
{
    "error": "Cannot cancel job (not found or already processing)"
}
```

---

### Bulk Delete Jobs

#### `DELETE /queue/jobs`

Delete multiple jobs at once (soft-delete to recycle bin).

**Request Body:**

```json
{
    "job_ids": ["uuid1", "uuid2", "uuid3"]
}
```

**Success Response (200):**

```json
{
    "success": true,
    "deleted": 2,
    "failed": 1,
    "total": 3,
    "failed_job_ids": ["uuid3"]
}
```

**Error Response (400):**

```json
{
    "error": "job_ids array is required in request body"
}
```

---

### Get Job Preview

#### `GET /jobs/{job_id}/preview`

Get the current live preview image for a processing job. Returns a JPEG image, not JSON.

**URL Parameters:**

| Parameter | Description |
|-----------|-------------|
| `job_id` | Job UUID |

**Response Headers:**

| Header | Description |
|--------|-------------|
| `Content-Type` | `image/jpeg` |
| `Cache-Control` | `no-cache` |
| `X-Preview-Width` | Preview image width |
| `X-Preview-Height` | Preview image height |
| `X-Preview-Step` | Current generation step |

**Response Body:** Binary JPEG image data

**Error Response (404):** No preview available

---

## Recycle Bin

Jobs are soft-deleted to a recycle bin and auto-purged after a configurable retention period.

### List Recycle Bin

#### `GET /queue/recycle-bin`

Get all soft-deleted jobs.

**Response (200):**

```json
{
    "success": true,
    "enabled": true,
    "retention_minutes": 1440,
    "count": 2,
    "items": [
        {
            "job_id": "550e8400-e29b-41d4-a716-446655440000",
            "type": "txt2img",
            "status": "deleted",
            "progress": { "step": 20, "total_steps": 20 },
            "created_at": "2024-01-01T08:00:00Z",
            "started_at": "2024-01-01T08:01:00Z",
            "completed_at": "2024-01-01T08:05:00Z",
            "outputs": ["image_0.png"],
            "params": { ... },
            "model_settings": { ... },
            "deleted_at": "2024-01-01T10:30:00Z",
            "previous_status": "completed"
        }
    ]
}
```

| Field | Type | Description |
|-------|------|-------------|
| `enabled` | boolean | Whether recycle bin is enabled |
| `retention_minutes` | integer | Auto-purge after this many minutes |
| `count` | integer | Number of items in recycle bin |
| `items[].deleted_at` | string | ISO 8601 timestamp when deleted |
| `items[].previous_status` | string | Status before deletion |

---

### Restore Job

#### `POST /queue/{job_id}/restore`

Restore a job from the recycle bin to its previous status.

**Request Body:** None required

**Success Response (200):**

```json
{
    "success": true,
    "message": "Job restored from recycle bin"
}
```

**Error Response (400):**

```json
{
    "error": "Cannot restore job (not found or not in recycle bin)"
}
```

---

### Purge Job

#### `DELETE /queue/{job_id}/purge`

Permanently delete a job from the recycle bin.

**Success Response (200):**

```json
{
    "success": true,
    "message": "Job permanently deleted"
}
```

**Error Response (400):**

```json
{
    "error": "Cannot purge job (not found or still processing)"
}
```

---

### Clear Recycle Bin

#### `DELETE /queue/recycle-bin`

Permanently delete all jobs in the recycle bin.

**Success Response (200):**

```json
{
    "success": true,
    "purged": 5,
    "message": "Recycle bin cleared"
}
```

---

### Get Recycle Bin Settings

#### `GET /settings/recycle-bin`

Get recycle bin configuration.

**Response (200):**

```json
{
    "enabled": true,
    "retention_minutes": 1440
}
```

---

## Preview Settings

Control live preview generation during image/video generation.

### Get Preview Settings

#### `GET /preview/settings`

Get current preview settings.

**Response (200):**

```json
{
    "enabled": true,
    "mode": "tae",
    "interval": 1,
    "max_size": 256,
    "quality": 75
}
```

| Field | Type | Description |
|-------|------|-------------|
| `enabled` | boolean | Whether preview generation is enabled |
| `mode` | string | Preview mode: `none`, `proj`, `tae`, `vae` |
| `interval` | integer | Generate preview every N steps |
| `max_size` | integer | Maximum preview dimension in pixels |
| `quality` | integer | JPEG quality (1-100) |

**Preview Modes:**

| Mode | Description |
|------|-------------|
| `none` | Previews disabled |
| `proj` | Fast projection-based preview (low quality) |
| `tae` | TAESD tiny autoencoder (balanced speed/quality) |
| `vae` | Full VAE preview (high quality, slower) |

---

### Update Preview Settings

#### `PUT /preview/settings`

Update preview settings.

**Request Body:**

```json
{
    "enabled": true,
    "mode": "tae",
    "interval": 1,
    "max_size": 256,
    "quality": 75
}
```

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `enabled` | boolean | No | true | Enable/disable previews |
| `mode` | string | No | "tae" | Preview mode |
| `interval` | integer | No | 1 | Steps between previews (1-100) |
| `max_size` | integer | No | 256 | Max preview size (64-1024) |
| `quality` | integer | No | 75 | JPEG quality (1-100) |

**Success Response (200):**

```json
{
    "success": true,
    "settings": {
        "enabled": true,
        "mode": "tae",
        "interval": 1,
        "max_size": 256,
        "quality": 75
    }
}
```

---

## Settings

Server-side persistence for generation defaults and UI preferences.

### Get All Generation Defaults

#### `GET /settings/generation`

Get generation defaults for all modes.

**Response (200):**

```json
{
    "txt2img": { ... },
    "img2img": { ... },
    "txt2vid": { ... }
}
```

---

### Update All Generation Defaults

#### `PUT /settings/generation`

Update generation defaults for all modes.

**Request Body:**

```json
{
    "txt2img": { ... },
    "img2img": { ... },
    "txt2vid": { ... }
}
```

**Success Response (200):**

```json
{
    "success": true,
    "settings": {
        "txt2img": { ... },
        "img2img": { ... },
        "txt2vid": { ... }
    }
}
```

---

### Get Generation Defaults for Mode

#### `GET /settings/generation/{mode}`

Get generation defaults for a specific mode.

**URL Parameters:**

| Parameter | Description |
|-----------|-------------|
| `mode` | Generation mode: `txt2img`, `img2img`, `txt2vid` |

**Response (200):** Mode-specific generation defaults object.

---

### Update Generation Defaults for Mode

#### `PUT /settings/generation/{mode}`

Update generation defaults for a specific mode.

**URL Parameters:**

| Parameter | Description |
|-----------|-------------|
| `mode` | Generation mode: `txt2img`, `img2img`, `txt2vid` |

**Request Body:** Mode-specific generation defaults object.

**Success Response (200):**

```json
{
    "success": true,
    "mode": "txt2img",
    "preferences": { ... }
}
```

---

### Get UI Preferences

#### `GET /settings/preferences`

Get UI preferences.

**Response (200):**

```json
{
    "desktop_notifications": true,
    "theme": "dark",
    "theme_custom": null
}
```

---

### Update UI Preferences

#### `PUT /settings/preferences`

Update UI preferences.

**Request Body:**

```json
{
    "desktop_notifications": true,
    "theme": "dark",
    "theme_custom": null
}
```

**Success Response (200):**

```json
{
    "success": true,
    "preferences": {
        "desktop_notifications": true,
        "theme": "dark",
        "theme_custom": null
    }
}
```

---

### Reset Settings

#### `POST /settings/reset`

Reset all settings to defaults.

**Request Body:** None required

**Success Response (200):**

```json
{
    "success": true,
    "message": "Settings reset to defaults"
}
```

---

## File Browser & Output Serving

### `GET /output`
### `GET /output/{path}`

Browse output directory or serve generated files. Returns an interactive HTML file browser for directories, or the raw file content for files.

**Path Parameters:**

| Parameter | Description |
|-----------|-------------|
| `path` | (Optional) Relative path within output directory |

**Query Parameters (for directories only):**

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `sort` | string | `name` | Sort column: `name`, `size`, `date` |
| `order` | string | `asc` | Sort order: `asc`, `desc` |

**Directory Response (200):**
- Content-Type: `text/html`
- Returns interactive HTML file browser with thumbnails, sorting, and lightbox

**File Response (200):**
- Content-Type: Determined by file extension
- Returns raw file content

**Error Responses:**
- **400 Bad Request:** Invalid path (path traversal attempt with `..`)
- **404 Not Found:** Path does not exist

---

### `GET /thumb/{path}`

Get thumbnail for an image or video file.

**Path Parameters:**

| Parameter | Description |
|-----------|-------------|
| `path` | Relative path to image/video file within output directory |

**Response (200):**
- For images: 120x120 center-cropped JPEG thumbnail
- For videos: SVG play button placeholder
- Thumbnails are cached in `.thumbs` subdirectories

**Supported Formats:**
- Images: .png, .jpg, .jpeg, .gif, .webp, .bmp
- Videos: .mp4, .webm, .avi, .mov, .mkv

**Error Responses:**
- **400 Bad Request:** Invalid path, path traversal, or not a media file
- **404 Not Found:** File does not exist

---

**Output Directory Structure:**

```
/output/
├── {job_id}/
│   ├── image_0.png       # First generated image
│   ├── image_1.png       # Second image (if batch_count > 1)
│   ├── config.json       # Generation parameters
│   └── .thumbs/          # Cached thumbnails
│       └── image_0.jpg
├── {job_id}/             # Video job
│   ├── video.mp4         # Generated video
│   └── config.json
└── queue_state.json      # Queue persistence file (internal)
```

---

## Server Options

### Get Options

#### `GET /options`

Get available generation options including samplers, schedulers, and quantization types.

**Response:**

```json
{
    "samplers": [
        "euler", "euler_a", "heun", "dpm2", "dpm++2s_a", "dpm++2m", "dpm++2mv2",
        "ipndm", "ipndm_v", "lcm", "ddim_trailing", "tcd", "res_multistep", "res_2s"
    ],
    "schedulers": [
        "discrete", "karras", "exponential", "ays", "gits", "sgm_uniform",
        "simple", "smoothstep", "kl_optimal", "lcm", "bong_tangent"
    ],
    "quantization_types": [
        {"id": "f32", "name": "F32 (32-bit float)", "bits": 32},
        {"id": "f16", "name": "F16 (16-bit float)", "bits": 16},
        {"id": "bf16", "name": "BF16 (Brain float 16)", "bits": 16},
        {"id": "q8_0", "name": "Q8_0 (8-bit)", "bits": 8},
        {"id": "q5_0", "name": "Q5_0 (5-bit)", "bits": 5},
        {"id": "q5_1", "name": "Q5_1 (5-bit)", "bits": 5},
        {"id": "q4_0", "name": "Q4_0 (4-bit)", "bits": 4},
        {"id": "q4_1", "name": "Q4_1 (4-bit)", "bits": 4},
        {"id": "q4_k", "name": "Q4_K (4-bit K-quant)", "bits": 4},
        {"id": "q5_k", "name": "Q5_K (5-bit K-quant)", "bits": 5},
        {"id": "q6_k", "name": "Q6_K (6-bit K-quant)", "bits": 6},
        {"id": "q8_k", "name": "Q8_K (8-bit K-quant)", "bits": 8},
        {"id": "q3_k", "name": "Q3_K (3-bit K-quant)", "bits": 3},
        {"id": "q2_k", "name": "Q2_K (2-bit K-quant)", "bits": 2}
    ]
}
```

---

### Get Option Descriptions

#### `GET /options/descriptions`

Get detailed descriptions of all load options including labels, types, defaults, and recommended values. Returns the contents of the `data/load_options.json` configuration file.

**Response (200):**

```json
{
    "options": {
        "flash_attn": {
            "label": "Flash Attention",
            "description": "Enable Flash Attention for faster inference",
            "type": "boolean",
            "default": true
        }
    },
    "categories": { ... }
}
```

---

## Model Conversion

### `POST /convert`

Convert a model to GGUF format with specified quantization.

**Request:**

```json
{
    "input_path": "/path/to/model.safetensors",
    "output_type": "q8_0",
    "output_path": "/path/to/output.gguf",
    "vae_path": "/path/to/vae.gguf",
    "tensor_type_rules": "string"
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `input_path` | string | Yes | Path to input model file |
| `output_type` | string | Yes | Quantization type (see `/options` for list) |
| `output_path` | string | No | Output path (auto-generated if not provided) |
| `vae_path` | string | No | VAE to bake into the model |
| `tensor_type_rules` | string | No | Custom tensor quantization rules |

**Response (202 Accepted):**

```json
{
    "job_id": "550e8400-e29b-41d4-a716-446655440000",
    "status": "pending",
    "position": 1,
    "output_path": "/path/to/output.q8_0.gguf"
}
```

---

## Assistant Integration

LLM-powered assistant for the Web UI with context awareness. Requires building with `-DSDCPP_ASSISTANT_ENABLED=ON`.

### Chat with Assistant

#### `POST /assistant/chat`

Send a message to the LLM assistant with application context.

**Request:**

```json
{
    "message": "Load the SDXL model",
    "context": {
        "current_view": "dashboard",
        "settings": {},
        "model_info": {},
        "available_models": {}
    }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `message` | string | Yes | User message |
| `context` | object | No | Application state context |

**Response (200):**

```json
{
    "success": true,
    "message": "I'll load the SDXL model for you. Here's what I'm doing...",
    "thinking": "The user wants to load an SDXL model...",
    "tool_calls": [
        {
            "name": "load_model",
            "parameters": { ... },
            "result": "Model loaded successfully",
            "executed_on_backend": true
        }
    ],
    "actions": [
        {
            "type": "load_model",
            "parameters": {
                "model_name": "sdxl-model.safetensors",
                "model_type": "checkpoint"
            }
        }
    ]
}
```

| Field | Type | Description |
|-------|------|-------------|
| `success` | boolean | Whether the request succeeded |
| `message` | string | Text response to display |
| `thinking` | string | LLM reasoning trace (optional) |
| `tool_calls` | array | Backend tool executions (optional) |
| `actions` | array | Suggested UI actions to execute |

**Error Response (500):**

```json
{
    "success": false,
    "error": "Failed to get response from assistant"
}
```

---

### Chat with Assistant (Streaming)

#### `POST /assistant/chat/stream`

Stream a response from the LLM assistant using Server-Sent Events.

**Request:** Same as [Chat with Assistant](#chat-with-assistant).

**Response:** `Content-Type: text/event-stream`

**SSE Event Format:**

```
event: content
data: {"text": "I'll load the..."}

event: thinking
data: {"text": "The user wants..."}

event: tool_call
data: {"name": "load_model", "parameters": {...}}

event: done
data: {}

event: error
data: {"error": "Something went wrong"}
```

---

### Get Assistant History

#### `GET /assistant/history`

Get conversation history with the assistant.

**Response (200):**

```json
{
    "messages": [
        {
            "role": "user",
            "content": "Load SDXL",
            "timestamp": 1704067200
        },
        {
            "role": "assistant",
            "content": "I'll load SDXL...",
            "timestamp": 1704067201
        }
    ],
    "count": 2
}
```

---

### Clear Assistant History

#### `DELETE /assistant/history`

Clear conversation history.

**Response (200):**

```json
{
    "success": true
}
```

---

### Get Assistant Status

#### `GET /assistant/status`

Get assistant connection status.

**Response (200):**

```json
{
    "enabled": true,
    "connected": true,
    "base_url": "http://localhost:11434",
    "model": "llama3.2",
    "timeout_seconds": 120,
    "history_count": 15
}
```

---

### Get Assistant Settings

#### `GET /assistant/settings`

Get assistant configuration.

**Response (200):**

```json
{
    "enabled": true,
    "endpoint": "http://localhost:11434/v1",
    "model": "llama3.2",
    "temperature": 0.7,
    "max_tokens": 4096,
    "timeout_seconds": 120,
    "max_history_turns": 50,
    "proactive_suggestions": true,
    "system_prompt": "You are a helpful assistant...",
    "default_system_prompt": "You are a helpful assistant...",
    "has_api_key": true
}
```

| Field | Type | Description |
|-------|------|-------------|
| `enabled` | boolean | Whether the assistant is enabled |
| `endpoint` | string | LLM API endpoint URL |
| `model` | string | LLM model name |
| `temperature` | float | Sampling temperature |
| `max_tokens` | integer | Maximum response tokens |
| `timeout_seconds` | integer | Request timeout |
| `max_history_turns` | integer | Maximum conversation turns to keep |
| `proactive_suggestions` | boolean | Whether to offer proactive suggestions |
| `system_prompt` | string | Current system prompt |
| `default_system_prompt` | string | Default system prompt |
| `has_api_key` | boolean | Whether an API key is configured |

---

### Update Assistant Settings

#### `PUT /assistant/settings`

Update assistant configuration at runtime.

**Request:**

```json
{
    "enabled": true,
    "endpoint": "http://localhost:11434/v1",
    "model": "llama3.2",
    "temperature": 0.7,
    "max_tokens": 4096,
    "timeout_seconds": 120,
    "max_history_turns": 50,
    "proactive_suggestions": true,
    "system_prompt": "Custom system prompt",
    "api_key": "optional_key"
}
```

**Response (200):**

```json
{
    "success": true,
    "settings": {
        "enabled": true,
        "endpoint": "http://localhost:11434/v1",
        "model": "llama3.2",
        "temperature": 0.7,
        "max_tokens": 4096,
        "timeout_seconds": 120,
        "max_history_turns": 50,
        "proactive_suggestions": true,
        "system_prompt": "Custom system prompt",
        "default_system_prompt": "You are a helpful assistant...",
        "has_api_key": true
    }
}
```

---

### Get Assistant Model Info

#### `GET /assistant/model-info`

Get information about the assistant's LLM model capabilities.

**Query Parameters:**

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `model` | string | No | Model name to query (defaults to current model) |

**Response (200):**

```json
{
    "model": "llama3.2",
    "capabilities": ["chat", "tool_use"],
    "context_length": 131072,
    "family": "llama",
    "parameter_size": "3B",
    "has_vision": false
}
```

---

## Model Architecture

### Get Architectures

#### `GET /architectures`

Get all supported model architecture presets and current architecture info.

**Response (200):**

```json
{
    "architectures": {
        "SD1": {
            "id": "SD1",
            "name": "Stable Diffusion 1.5",
            "description": "Standard SD 1.x models",
            "aliases": ["SD1.5", "SD1.x"],
            "requiredComponents": {
                "clip": "CLIP text encoder"
            },
            "optionalComponents": {
                "vae": "VAE model",
                "lora": "LoRA weights",
                "controlnet": "ControlNet model"
            },
            "loadOptions": {
                "flash_attn": true
            },
            "generationDefaults": {
                "width": 512,
                "height": 512,
                "steps": 20,
                "cfg_scale": 7.0,
                "sampler": "euler_a",
                "scheduler": "discrete"
            }
        },
        "Flux": {
            "id": "Flux",
            "name": "Flux",
            "description": "Black Forest Labs Flux models",
            "aliases": [],
            "requiredComponents": {
                "clip_l": "CLIP-L text encoder",
                "t5xxl": "T5-XXL text encoder",
                "vae": "VAE (ae.safetensors)"
            },
            "optionalComponents": {
                "lora": "LoRA weights"
            },
            "loadOptions": {
                "flash_attn": true,
                "keep_clip_on_cpu": true
            },
            "generationDefaults": {
                "width": 1024,
                "height": 1024,
                "steps": 20,
                "cfg_scale": 1.0,
                "sampler": "euler"
            },
            "imageEditMode": "ref_images"
        }
    },
    "current_architecture": "Flux",
    "current_preset": { ... }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `architectures` | object | Map of architecture ID to preset |
| `architectures.*.id` | string | Architecture identifier |
| `architectures.*.name` | string | Display name |
| `architectures.*.description` | string | Architecture description |
| `architectures.*.aliases` | array | Alternative names |
| `architectures.*.requiredComponents` | object | Required component models |
| `architectures.*.optionalComponents` | object | Optional component models |
| `architectures.*.loadOptions` | object | Recommended load options |
| `architectures.*.generationDefaults` | object | Default generation parameters |
| `architectures.*.imageEditMode` | string\|null | Image edit mode (e.g., `"ref_images"`) |
| `current_architecture` | string\|null | Currently loaded model's architecture |
| `current_preset` | object\|null | Full preset for current architecture |

---

### Detect Architecture

#### `GET /architectures/detect`

Detect the architecture of a model without loading it.

**Query Parameters:**

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `model` | string | Yes | Model name to detect |

**Response (detected):**

```json
{
    "detected": true,
    "architecture": {
        "id": "Flux",
        "name": "Flux",
        "description": "Black Forest Labs Flux models",
        "aliases": [],
        "requiredComponents": { ... },
        "optionalComponents": { ... },
        "loadOptions": { ... },
        "generationDefaults": { ... },
        "imageEditMode": "ref_images"
    }
}
```

**Response (not detected):**

```json
{
    "detected": false,
    "architecture": null
}
```

---

## Model Downloads

Endpoints for downloading models from external sources.

### Download Model

#### `POST /models/download`

Download a model from URL, CivitAI, or HuggingFace.

**Request:**

```json
{
    "model_type": "checkpoint",
    "source": "civitai",
    "model_id": "123456",
    "url": "https://...",
    "repo_id": "org/repo",
    "filename": "model.safetensors",
    "subfolder": "anime",
    "revision": "main"
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `model_type` | string | Yes | Target model type (`checkpoint`, `diffusion`, `vae`, `lora`, `clip`, `t5`, `embedding`, `controlnet`, `llm`, `esrgan`, `taesd`) |
| `source` | string | No | Source type: `url`, `civitai`, `huggingface` (auto-detected from other fields) |
| `model_id` | string | For CivitAI | CivitAI model ID (format: `"123456"` or `"123456:789012"` for version) |
| `url` | string | For URL | Direct download URL (also accepts CivitAI/HuggingFace URLs for auto-detection) |
| `repo_id` | string | For HuggingFace | HuggingFace repository ID (e.g., `"stabilityai/sdxl-turbo"`) |
| `filename` | string | For HuggingFace | Filename in repository |
| `subfolder` | string | No | Target subfolder in model directory |
| `revision` | string | No | Git revision for HuggingFace (default: `"main"`) |

**Source Auto-Detection:**

If `source` is not specified:
- URL containing `civitai.com` → `civitai`
- URL containing `huggingface.co` → `huggingface`
- `url` present → `url`
- `model_id` present → `civitai`
- `repo_id` and `filename` present → `huggingface`

**Response (202 Accepted):**

```json
{
    "success": true,
    "download_job_id": "550e8400-e29b-41d4-a716-446655440000",
    "hash_job_id": "660e8400-e29b-41d4-a716-446655440001",
    "source": "civitai",
    "model_type": "checkpoint",
    "position": 1
}
```

---

### Get CivitAI Model Info

#### `GET /models/civitai/{id}`

Get metadata for a CivitAI model.

**Path Parameters:**

- `id`: CivitAI model ID. Supports format `123456` or `123456:789012` (model:version)

**Response (200):**

```json
{
    "success": true,
    "model_id": 123456,
    "version_id": 789012,
    "name": "Model Name",
    "version_name": "v1.0",
    "type": "Checkpoint",
    "base_model": "SDXL 1.0",
    "filename": "model.safetensors",
    "file_size": 5368709120,
    "sha256": "abc123...",
    "download_url": "https://civitai.com/..."
}
```

---

### Get HuggingFace Model Info

#### `GET /models/huggingface`

Get metadata for a HuggingFace model file.

**Query Parameters:**

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `repo_id` | string | Yes | HuggingFace repository ID (e.g., `"stabilityai/sdxl-turbo"`) |
| `filename` | string | Yes | Filename within repository |
| `revision` | string | No | Git revision (default: `"main"`) |

**Response (200):**

```json
{
    "success": true,
    "repo_id": "stabilityai/sdxl-turbo",
    "filename": "sd_xl_turbo_1.0.safetensors",
    "revision": "main",
    "file_size": 5368709120,
    "download_url": "https://huggingface.co/..."
}
```

---

### Get Model Paths

#### `GET /models/paths`

Get configured model storage paths.

**Response (200):**

```json
{
    "checkpoints": "/path/to/checkpoints",
    "diffusion_models": "/path/to/diffusion_models",
    "vae": "/path/to/vae",
    "lora": "/path/to/loras",
    "clip": "/path/to/clip",
    "t5": "/path/to/t5xxl",
    "embeddings": "/path/to/embeddings",
    "controlnet": "/path/to/controlnet",
    "llm": "/path/to/llm",
    "esrgan": "/path/to/esrgan",
    "taesd": "/path/to/taesd"
}
```

---

## WebSocket Support

Real-time progress updates are available via WebSocket connection on a separate port (default: 8081, see `ws_port` in health response).

### Connection

Connect to `ws://<host>:<ws_port>` (e.g., `ws://localhost:8081`).

### Event Types

All messages are JSON with this structure:

```json
{
    "event": "event_type",
    "timestamp": "2024-01-01T12:00:00.000Z",
    "data": { ... }
}
```

#### Job Events

| Event | Description |
|-------|-------------|
| `job_added` | New job added to queue |
| `job_status_changed` | Job status changed (pending/processing/completed/failed/cancelled) |
| `job_progress` | Generation progress update (step/total_steps) |
| `job_preview` | Live preview image during generation |
| `job_cancelled` | Job was cancelled |

#### Model Events

| Event | Description |
|-------|-------------|
| `model_loading_progress` | Model loading progress |
| `model_loaded` | Model finished loading |
| `model_load_failed` | Model failed to load |
| `model_unloaded` | Model was unloaded |
| `upscaler_loaded` | Upscaler finished loading |
| `upscaler_unloaded` | Upscaler was unloaded |

#### Server Events

| Event | Description |
|-------|-------------|
| `server_status` | Periodic heartbeat/status update |
| `server_shutdown` | Server is shutting down |

### job_preview Event

Sent during generation when previews are enabled. Contains a base64-encoded JPEG image.

**Example:**

```json
{
    "event": "job_preview",
    "timestamp": "2024-01-01T12:00:30.123Z",
    "data": {
        "job_id": "550e8400-e29b-41d4-a716-446655440000",
        "step": 10,
        "frame_count": 1,
        "width": 256,
        "height": 256,
        "is_noisy": false,
        "image": "data:image/jpeg;base64,/9j/4AAQSkZJRg..."
    }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `job_id` | string | Job UUID |
| `step` | integer | Current generation step |
| `frame_count` | integer | Number of frames (1 for images, >1 for video) |
| `width` | integer | Preview image width |
| `height` | integer | Preview image height |
| `is_noisy` | boolean | Whether this is a noisy preview |
| `image` | string | Base64-encoded JPEG data URL |

### job_progress Event

**Example:**

```json
{
    "event": "job_progress",
    "timestamp": "2024-01-01T12:00:25.456Z",
    "data": {
        "job_id": "550e8400-e29b-41d4-a716-446655440000",
        "step": 10,
        "total_steps": 20
    }
}
```

### job_status_changed Event

**Example:**

```json
{
    "event": "job_status_changed",
    "timestamp": "2024-01-01T12:01:00.789Z",
    "data": {
        "job_id": "550e8400-e29b-41d4-a716-446655440000",
        "status": "completed",
        "previous_status": "processing",
        "outputs": ["550e8400-e29b-41d4-a716-446655440000/image_0.png"]
    }
}
```

### JavaScript Example

```javascript
const ws = new WebSocket('ws://localhost:8081');

ws.onmessage = (event) => {
    const msg = JSON.parse(event.data);

    switch (msg.event) {
        case 'job_progress':
            console.log(`Progress: ${msg.data.step}/${msg.data.total_steps}`);
            break;
        case 'job_preview':
            // Display preview image
            document.getElementById('preview').src = msg.data.image;
            break;
        case 'job_status_changed':
            console.log(`Job ${msg.data.job_id}: ${msg.data.status}`);
            break;
    }
};
```

---

## Error Responses

All error responses follow this format:

```json
{
    "error": "Error message describing what went wrong"
}
```

**Common HTTP Status Codes:**

| Code | Description |
|------|-------------|
| 200 | Success |
| 202 | Accepted (job queued) |
| 400 | Bad Request (invalid parameters, no model loaded) |
| 404 | Not Found (model or job not found) |
| 500 | Internal Server Error |

---

## Data Types

### Job Status

| Status | Description |
|--------|-------------|
| `pending` | Job is waiting in queue |
| `processing` | Job is currently being processed |
| `completed` | Job finished successfully |
| `failed` | Job encountered an error |
| `cancelled` | Job was cancelled by user |
| `deleted` | Job is in recycle bin (soft-deleted) |

### Generation Types

| Type | Description |
|------|-------------|
| `txt2img` | Text to Image |
| `img2img` | Image to Image |
| `txt2vid` | Text to Video |
| `upscale` | Image Upscaling |
| `convert` | Model Format Conversion |
| `model_download` | Model Download |
| `model_hash` | Model Hash Computation |

### Samplers

Available sampling methods:

| Sampler | Description |
|---------|-------------|
| `euler` | Euler sampler |
| `euler_a` | Euler Ancestral sampler |
| `heun` | Heun sampler |
| `dpm2` | DPM2 sampler |
| `dpm++2s_a` | DPM++ 2S Ancestral |
| `dpm++2m` | DPM++ 2M |
| `dpm++2mv2` | DPM++ 2M v2 |
| `ipndm` | IPNDM sampler |
| `ipndm_v` | IPNDM V sampler |
| `lcm` | LCM sampler |
| `ddim_trailing` | DDIM Trailing sampler |
| `tcd` | TCD sampler |
| `res_multistep` | Res Multistep sampler |
| `res_2s` | Res 2S sampler |

### Schedulers

Available scheduler types:

| Scheduler | Description |
|-----------|-------------|
| `discrete` | Discrete scheduler (default) |
| `karras` | Karras scheduler |
| `exponential` | Exponential scheduler |
| `ays` | AYS scheduler |
| `gits` | GITS scheduler |
| `sgm_uniform` | SGM Uniform scheduler |
| `simple` | Simple scheduler |
| `smoothstep` | Smoothstep scheduler (Z-Image) |
| `kl_optimal` | KL Optimal scheduler |
| `lcm` | LCM scheduler |
| `bong_tangent` | Bong Tangent scheduler |

### Model Types

| Type | Directory | Description |
|------|-----------|-------------|
| `checkpoint` | `checkpoints/` | SD1.x, SD2.x, SDXL full models |
| `diffusion` | `diffusion_models/` | Flux, SD3, Wan, Qwen models |
| `vae` | `vae/` | VAE models |
| `lora` | `loras/` | LoRA models |
| `clip` | `clip/` | CLIP text encoders |
| `t5` | `t5/` | T5 text encoders |
| `controlnet` | `controlnet/` | ControlNet models |
| `llm` | `llm/` | LLM models for multimodal (Qwen, etc.) |
| `esrgan` | `esrgan/` | ESRGAN upscaler models |
| `taesd` | `taesd/` | TAESD preview models |
| `embedding` | `embeddings/` | Textual inversion embeddings |

---

## Usage Examples

### cURL Examples by Model Type

#### SD1.x / SD1.5

**Load SD1.5 model:**

```bash
curl -X POST http://localhost:8080/models/load \
  -H "Content-Type: application/json" \
  -d '{
    "model_name": "v1-5-pruned-emaonly.safetensors",
    "model_type": "checkpoint"
  }'
```

**Generate image with SD1.5:**

```bash
curl -X POST http://localhost:8080/txt2img \
  -H "Content-Type: application/json" \
  -d '{
    "prompt": "a lovely cat",
    "width": 512,
    "height": 512,
    "steps": 20,
    "cfg_scale": 7.0
  }'
```

---

#### SDXL

**Load SDXL model with separate VAE:**

```bash
curl -X POST http://localhost:8080/models/load \
  -H "Content-Type: application/json" \
  -d '{
    "model_name": "sd_xl_base_1.0.safetensors",
    "model_type": "checkpoint",
    "vae": "sdxl_vae-fp16-fix.safetensors"
  }'
```

**Generate 1024x1024 image with SDXL:**

```bash
curl -X POST http://localhost:8080/txt2img \
  -H "Content-Type: application/json" \
  -d '{
    "prompt": "a lovely cat, masterpiece, best quality",
    "negative_prompt": "low quality, worst quality",
    "width": 1024,
    "height": 1024,
    "steps": 20,
    "cfg_scale": 7.0
  }'
```

---

#### SD3 / SD3.5 Large

**Load SD3.5 Large with text encoders:**

```bash
curl -X POST http://localhost:8080/models/load \
  -H "Content-Type: application/json" \
  -d '{
    "model_name": "sd3.5_large.safetensors",
    "model_type": "checkpoint",
    "clip_l": "clip_l.safetensors",
    "clip_g": "clip_g.safetensors",
    "t5xxl": "t5xxl_fp16.safetensors",
    "options": {
      "keep_clip_on_cpu": true
    }
  }'
```

**Generate with SD3.5:**

```bash
curl -X POST http://localhost:8080/txt2img \
  -H "Content-Type: application/json" \
  -d '{
    "prompt": "a lovely cat holding a sign says \"Stable diffusion 3.5 Large\"",
    "width": 1024,
    "height": 1024,
    "steps": 20,
    "cfg_scale": 4.5,
    "sampler": "euler"
  }'
```

---

#### FLUX.1-dev / FLUX.1-schnell

**Load Flux-dev (quantized GGUF):**

```bash
curl -X POST http://localhost:8080/models/load \
  -H "Content-Type: application/json" \
  -d '{
    "model_name": "flux1-dev-Q4_K_S.gguf",
    "model_type": "diffusion",
    "vae": "ae.safetensors",
    "clip_l": "clip_l.safetensors",
    "t5xxl": "t5xxl_fp16.safetensors",
    "options": {
      "keep_clip_on_cpu": true,
      "flash_attn": true
    }
  }'
```

**Generate with Flux (cfg_scale=1.0 recommended):**

```bash
curl -X POST http://localhost:8080/txt2img \
  -H "Content-Type: application/json" \
  -d '{
    "prompt": "a lovely cat holding a sign says \"flux.cpp\"",
    "width": 1024,
    "height": 1024,
    "steps": 20,
    "cfg_scale": 1.0,
    "sampler": "euler"
  }'
```

**Load Flux-schnell (faster, fewer steps):**

```bash
curl -X POST http://localhost:8080/models/load \
  -H "Content-Type: application/json" \
  -d '{
    "model_name": "flux1-schnell-Q8_0.gguf",
    "model_type": "diffusion",
    "vae": "ae.safetensors",
    "clip_l": "clip_l.safetensors",
    "t5xxl": "t5xxl_fp16.safetensors",
    "options": {
      "keep_clip_on_cpu": true
    }
  }'
```

**Generate with Flux-schnell (4 steps):**

```bash
curl -X POST http://localhost:8080/txt2img \
  -H "Content-Type: application/json" \
  -d '{
    "prompt": "a lovely cat holding a sign says \"flux.cpp\"",
    "width": 1024,
    "height": 1024,
    "steps": 4,
    "cfg_scale": 1.0,
    "sampler": "euler"
  }'
```

---

#### Qwen-Image (Multimodal with LLM)

**Load Qwen-Image with LLM text encoder:**

```bash
curl -X POST http://localhost:8080/models/load \
  -H "Content-Type: application/json" \
  -d '{
    "model_name": "qwen-image-Q8_0.gguf",
    "model_type": "diffusion",
    "vae": "qwen_image_vae.safetensors",
    "llm": "Qwen2.5-VL-7B-Instruct-Q8_0.gguf",
    "options": {
      "offload_to_cpu": true,
      "flash_attn": true,
      "flow_shift": 3.0
    }
  }'
```

**Generate with Qwen-Image:**

```bash
curl -X POST http://localhost:8080/txt2img \
  -H "Content-Type: application/json" \
  -d '{
    "prompt": "A beautiful Chinese woman wearing a T-shirt with the QWEN logo is smiling at the camera",
    "width": 1024,
    "height": 1024,
    "steps": 20,
    "cfg_scale": 2.5,
    "sampler": "euler"
  }'
```

---

#### Z-Image Turbo

Z-Image is a fast image generation model that uses Qwen3 as text encoder and can run on GPUs with 4GB VRAM or less.

**Load Z-Image Turbo:**

```bash
curl -X POST http://localhost:8080/models/load \
  -H "Content-Type: application/json" \
  -d '{
    "model_name": "z_image_turbo-Q8_0.gguf",
    "model_type": "diffusion",
    "vae": "ae_q8_0.gguf",
    "llm": "qwen_3_4b.Q8_0.gguf",
    "options": {
      "flash_attn": true,
      "vae_conv_direct": true,
      "weight_type": "q5_0"
    }
  }'
```

**Generate with Z-Image Turbo (8 steps, cfg_scale=1, smoothstep scheduler, easycache):**

```bash
curl -X POST http://localhost:8080/txt2img \
  -H "Content-Type: application/json" \
  -d '{
    "prompt": "A solitary 27-year-old woman sits sideways on a weathered tree trunk",
    "width": 1024,
    "height": 688,
    "steps": 8,
    "cfg_scale": 1.0,
    "sampler": "euler",
    "scheduler": "smoothstep",
    "seed": 42,
    "batch_count": 4,
    "easycache": true
  }'
```

---

#### Wan2.1 T2V (Text-to-Video)

**Load Wan2.1 T2V 1.3B:**

```bash
curl -X POST http://localhost:8080/models/load \
  -H "Content-Type: application/json" \
  -d '{
    "model_name": "wan2.1_t2v_1.3B_fp16.safetensors",
    "model_type": "diffusion",
    "vae": "wan_2.1_vae.safetensors",
    "t5xxl": "umt5-xxl-encoder-Q8_0.gguf",
    "options": {
      "flash_attn": true,
      "flow_shift": 3.0
    }
  }'
```

**Generate video with Wan2.1:**

```bash
curl -X POST http://localhost:8080/txt2vid \
  -H "Content-Type: application/json" \
  -d '{
    "prompt": "a lovely cat walking in a garden",
    "negative_prompt": "static, blurry, low quality",
    "width": 832,
    "height": 480,
    "video_frames": 33,
    "steps": 30,
    "cfg_scale": 6.0,
    "sampler": "euler",
    "flow_shift": 3.0
  }'
```

---

#### Using LoRAs

LoRAs are specified directly in the prompt using the syntax `<lora:name:weight>`:

**Generate with LoRA (Flux example):**

```bash
curl -X POST http://localhost:8080/txt2img \
  -H "Content-Type: application/json" \
  -d '{
    "prompt": "a lovely cat <lora:realism_lora:1.0>",
    "width": 1024,
    "height": 1024,
    "steps": 20,
    "cfg_scale": 1.0,
    "sampler": "euler"
  }'
```

**Multiple LoRAs:**

```bash
curl -X POST http://localhost:8080/txt2img \
  -H "Content-Type: application/json" \
  -d '{
    "prompt": "a beautiful landscape <lora:add_detail:0.8> <lora:enhance_colors:0.5>",
    "width": 512,
    "height": 512,
    "steps": 20,
    "cfg_scale": 7.0
  }'
```

---

#### Using ControlNet

**Load model with ControlNet:**

```bash
curl -X POST http://localhost:8080/models/load \
  -H "Content-Type: application/json" \
  -d '{
    "model_name": "v1-5-pruned-emaonly.safetensors",
    "model_type": "checkpoint",
    "controlnet": "control_v11p_sd15_canny.safetensors"
  }'
```

**Generate with control image (base64 encoded pre-processed canny edge image):**

```bash
curl -X POST http://localhost:8080/txt2img \
  -H "Content-Type: application/json" \
  -d '{
    "prompt": "a beautiful house, detailed architecture",
    "control_image_base64": "'$(base64 -w0 canny_edges.png)'",
    "control_strength": 0.9,
    "width": 512,
    "height": 512,
    "steps": 20,
    "cfg_scale": 7.0
  }'
```

---

#### img2img (Image-to-Image)

**Transform an existing image:**

```bash
curl -X POST http://localhost:8080/img2img \
  -H "Content-Type: application/json" \
  -d '{
    "prompt": "cat with blue eyes, masterpiece",
    "init_image_base64": "'$(base64 -w0 input.png)'",
    "strength": 0.4,
    "width": 512,
    "height": 512,
    "steps": 20,
    "cfg_scale": 7.0
  }'
```

---

### Common Operations

**List all available models:**

```bash
curl http://localhost:8080/models
```

**Filter models by type:**

```bash
curl "http://localhost:8080/models?type=diffusion"
```

**Search models by name:**

```bash
curl "http://localhost:8080/models?search=flux"
```

**Check server health:**

```bash
curl http://localhost:8080/health
```

**Check job status:**

```bash
curl http://localhost:8080/queue/{job_id}
```

**List all jobs:**

```bash
curl http://localhost:8080/queue
```

**List jobs with filters:**

```bash
curl "http://localhost:8080/queue?status=completed&type=txt2img&limit=10"
```

**Cancel a pending job:**

```bash
curl -X DELETE http://localhost:8080/queue/{job_id}
```

**Download generated image:**

```bash
curl -O http://localhost:8080/output/{job_id}/image_0.png
```

**Unload current model:**

```bash
curl -X POST http://localhost:8080/models/unload
```

**Compute model hash:**

```bash
curl http://localhost:8080/models/hash/checkpoint/v1-5-pruned-emaonly.safetensors
```

---

### Python Example

```python
import requests
import time
import base64

BASE_URL = "http://localhost:8080"

# Load Flux model
resp = requests.post(f"{BASE_URL}/models/load", json={
    "model_name": "flux1-dev-Q4_K_S.gguf",
    "model_type": "diffusion",
    "vae": "ae.safetensors",
    "clip_l": "clip_l.safetensors",
    "t5xxl": "t5xxl_fp16.safetensors",
    "options": {
        "keep_clip_on_cpu": True,
        "flash_attn": True
    }
})
print(resp.json())

# Generate image
resp = requests.post(f"{BASE_URL}/txt2img", json={
    "prompt": "a cute cat wearing a hat",
    "width": 1024,
    "height": 1024,
    "steps": 20,
    "cfg_scale": 1.0,
    "sampler": "euler"
})
job = resp.json()
job_id = job["job_id"]
print(f"Job created: {job_id}")

# Poll for completion
while True:
    resp = requests.get(f"{BASE_URL}/queue/{job_id}")
    status = resp.json()
    prog = status['progress']
    print(f"Status: {status['status']}, Step {prog['step']}/{prog['total_steps']}")

    if status["status"] in ["completed", "failed", "cancelled"]:
        break
    time.sleep(1)

# Download result
if status["status"] == "completed":
    for output in status["outputs"]:
        img_resp = requests.get(f"{BASE_URL}/output/{output}")
        with open(output.split("/")[-1], "wb") as f:
            f.write(img_resp.content)
        print(f"Saved: {output}")
```

---

### Bash Script Example

```bash
#!/bin/bash
# Complete workflow: load model, generate image, download result

BASE_URL="http://localhost:8080"

# 1. Load model
echo "Loading model..."
curl -s -X POST "$BASE_URL/models/load" \
  -H "Content-Type: application/json" \
  -d '{
    "model_name": "v1-5-pruned-emaonly.safetensors",
    "model_type": "checkpoint"
  }'

# 2. Submit generation job
echo -e "\n\nSubmitting job..."
RESPONSE=$(curl -s -X POST "$BASE_URL/txt2img" \
  -H "Content-Type: application/json" \
  -d '{
    "prompt": "a lovely cat sitting on a windowsill",
    "width": 512,
    "height": 512,
    "steps": 20
  }')

JOB_ID=$(echo $RESPONSE | jq -r '.job_id')
echo "Job ID: $JOB_ID"

# 3. Poll for completion
echo "Waiting for completion..."
while true; do
  STATUS=$(curl -s "$BASE_URL/queue/$JOB_ID")
  STATE=$(echo $STATUS | jq -r '.status')
  STEP=$(echo $STATUS | jq -r '.progress.step')
  TOTAL=$(echo $STATUS | jq -r '.progress.total_steps')
  echo "Status: $STATE, Step $STEP/$TOTAL"

  if [ "$STATE" = "completed" ] || [ "$STATE" = "failed" ]; then
    break
  fi
  sleep 2
done

# 4. Download result
if [ "$STATE" = "completed" ]; then
  OUTPUT=$(echo $STATUS | jq -r '.outputs[0]')
  echo "Downloading: $OUTPUT"
  curl -s -O "$BASE_URL/output/$OUTPUT"
  echo "Saved: $(basename $OUTPUT)"
fi
```
