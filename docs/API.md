# SDCPP-RESTAPI API Documentation

REST API for stable-diffusion.cpp image and video generation.

**Base URL:** `http://<host>:<port>`  
**Default:** `http://localhost:8080`

**Content-Type:** All requests and responses use `application/json`

---

## Table of Contents

- [Health Check](#health-check)
- [Model Management](#model-management)
  - [List Models](#list-models)
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
- [Preview Settings](#preview-settings)
  - [Get Preview Settings](#get-preview-settings)
  - [Update Preview Settings](#update-preview-settings)
- [File Browser & Output Serving](#file-browser--output-serving)
- [Server Options](#server-options)
- [Model Conversion](#model-conversion)
- [Ollama Integration](#ollama-integration)
  - [Enhance Prompt](#enhance-prompt)
  - [Get Enhancement History](#get-enhancement-history)
  - [Get History Entry](#get-history-entry)
  - [Delete History Entry](#delete-history-entry)
  - [Clear History](#clear-history)
  - [Get Ollama Status](#get-ollama-status)
  - [Get Ollama Models](#get-ollama-models)
  - [Get Ollama Settings](#get-ollama-settings)
  - [Update Ollama Settings](#update-ollama-settings)
- [Assistant Integration](#assistant-integration)
  - [Chat with Assistant](#chat-with-assistant)
  - [Get Assistant History](#get-assistant-history)
  - [Clear Assistant History](#clear-assistant-history)
  - [Get Assistant Status](#get-assistant-status)
  - [Get Assistant Settings](#get-assistant-settings)
  - [Update Assistant Settings](#update-assistant-settings)
- [Model Architecture](#model-architecture)
- [Model Downloads](#model-downloads)
  - [Download Model](#download-model)
  - [Get CivitAI Model Info](#get-civitai-model-info)
  - [Get HuggingFace Model Info](#get-huggingface-model-info)
  - [Get Model Paths](#get-model-paths)
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
    "model_loaded": true,
    "model_loading": false,
    "loading_model_name": null,
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
    "upscaler_name": null
}
```

**Response during model loading:**

```json
{
    "status": "ok",
    "model_loaded": false,
    "model_loading": true,
    "loading_model_name": "flux1-dev-Q4_K_S.gguf",
    "last_error": null,
    "model_name": null,
    "model_type": null,
    "model_architecture": null,
    "loaded_components": {}
}
```

**Response after load failure:**

```json
{
    "status": "ok",
    "model_loaded": false,
    "model_loading": false,
    "loading_model_name": null,
    "last_error": "Failed to load model: invalid_model.gguf",
    "model_name": null,
    "model_type": null,
    "model_architecture": null,
    "loaded_components": {}
}
```

| Field | Type | Description |
|-------|------|-------------|
| `status` | string | Always `"ok"` if server is running |
| `model_loaded` | boolean | Whether a model is currently loaded |
| `model_loading` | boolean | Whether a model is currently being loaded |
| `loading_model_name` | string\|null | Name of model being loaded, or null if not loading |
| `last_error` | string\|null | Last model load error message, or null if no error |
| `model_name` | string\|null | Name of loaded model, or null if none |
| `model_type` | string\|null | Type of loaded model (`checkpoint` or `diffusion`), or null if none |
| `model_architecture` | string\|null | Auto-detected architecture (e.g., `Z-Image`, `Flux`, `SDXL`, `SD1.5`), or null |
| `loaded_components` | object | Component models loaded with the main model |
| `loaded_components.vae` | string\|null | Loaded VAE model name |
| `loaded_components.clip_l` | string\|null | Loaded CLIP-L model name |
| `loaded_components.clip_g` | string\|null | Loaded CLIP-G model name |
| `loaded_components.t5xxl` | string\|null | Loaded T5-XXL model name |
| `loaded_components.controlnet` | string\|null | Loaded ControlNet model name |
| `loaded_components.llm` | string\|null | Loaded LLM model name |
| `loaded_components.llm_vision` | string\|null | Loaded LLM vision model name |
| `upscaler_loaded` | boolean | Whether an upscaler is currently loaded |
| `upscaler_name` | string\|null | Name of loaded upscaler model, or null if none |

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
    "diffusion_models": [
        {
            "name": "flux1-dev-Q4_K_S.gguf",
            "type": "diffusion",
            "file_extension": ".gguf",
            "size_bytes": 6876543210,
            "hash": null,
            "is_loaded": false
        }
    ],
    "vae": [
        {
            "name": "vae-ft-mse-840000-ema-pruned.safetensors",
            "type": "vae",
            "file_extension": ".safetensors",
            "size_bytes": 334643268,
            "hash": null
        }
    ],
    "loras": [
        {
            "name": "add_detail.safetensors",
            "type": "lora",
            "file_extension": ".safetensors",
            "size_bytes": 144664,
            "hash": null
        }
    ],
    "clip": [
        {
            "name": "clip_l.safetensors",
            "type": "clip",
            "file_extension": ".safetensors",
            "size_bytes": 246144152,
            "hash": null
        }
    ],
    "t5": [
        {
            "name": "t5xxl_fp16.safetensors",
            "type": "t5",
            "file_extension": ".safetensors",
            "size_bytes": 9334567890,
            "hash": null
        }
    ],
    "controlnets": [
        {
            "name": "control_v11p_sd15_canny.safetensors",
            "type": "controlnet",
            "file_extension": ".safetensors",
            "size_bytes": 1450958904,
            "hash": null
        }
    ],
    "llm": [
        {
            "name": "qwen_3_4b.Q8_0.gguf",
            "type": "llm",
            "file_extension": ".gguf",
            "size_bytes": 4234567890,
            "hash": null
        }
    ],
    "esrgan": [
        {
            "name": "RealESRGAN_x4plus.pth",
            "type": "esrgan",
            "file_extension": ".pth",
            "size_bytes": 67040989,
            "hash": null
        }
    ],
    "taesd": [
        {
            "name": "taesd_decoder.safetensors",
            "type": "taesd",
            "file_extension": ".safetensors",
            "size_bytes": 4915200,
            "hash": null
        },
        {
            "name": "taef1_decoder.safetensors",
            "type": "taesd",
            "file_extension": ".safetensors",
            "size_bytes": 4915200,
            "hash": null
        }
    ],
    "embeddings": [],
    "loaded_model": null,
    "loaded_model_type": null
}
```

**Response (with filters applied):**

When filters are applied, the response includes an `applied_filters` object showing active filters:

```
GET /models?type=diffusion&search=flux
```

```json
{
    "checkpoints": [],
    "diffusion_models": [
        {
            "name": "flux1-dev-Q4_K_S.gguf",
            "type": "diffusion",
            "file_extension": ".gguf",
            "size_bytes": 6876543210,
            "hash": null,
            "is_loaded": false
        },
        {
            "name": "flux1-schnell-Q5_K_M.gguf",
            "type": "diffusion",
            "file_extension": ".gguf",
            "size_bytes": 8234567890,
            "hash": null,
            "is_loaded": false
        }
    ],
    "vae": [],
    "loras": [],
    "clip": [],
    "t5": [],
    "controlnets": [],
    "llm": [],
    "esrgan": [],
    "taesd": [],
    "embeddings": [],
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
| `checkpoints` | array | Checkpoint models (filtered if applicable) |
| `diffusion_models` | array | Diffusion models (filtered if applicable) |
| `vae` | array | VAE models (filtered if applicable) |
| `loras` | array | LoRA models (filtered if applicable) |
| `clip` | array | CLIP models (filtered if applicable) |
| `t5` | array | T5 models (filtered if applicable) |
| `controlnets` | array | ControlNet models (filtered if applicable) |
| `llm` | array | LLM models for multimodal (filtered if applicable) |
| `esrgan` | array | ESRGAN upscaler models (filtered if applicable) |
| `embeddings` | array | Textual inversion embeddings (filtered if applicable) |
| `loaded_model` | string\|null | Currently loaded model name |
| `loaded_model_type` | string\|null | Type of loaded model |
| `applied_filters` | object | (Optional) Shows which filters were applied |

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

**Parameters:**

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
| `taesd` | string | No | null | TAESD model name for preview (from `paths.taesd` directory) |
| `high_noise_diffusion_model` | string | No | null | High-noise diffusion model for MoE |
| `photo_maker` | string | No | null | PhotoMaker model path |
| `options.n_threads` | integer | No | -1 (auto) | Number of CPU threads |
| `options.keep_clip_on_cpu` | boolean | No | true | Keep CLIP on CPU to save VRAM |
| `options.keep_vae_on_cpu` | boolean | No | false | Keep VAE on CPU |
| `options.keep_controlnet_on_cpu` | boolean | No | false | Keep ControlNet on CPU to save VRAM |
| `options.vae_conv_direct` | boolean | No | false | Use ggml_conv2d_direct in VAE |
| `options.diffusion_conv_direct` | boolean | No | false | Use ggml_conv2d_direct in diffusion model |
| `options.flash_attn` | boolean | No | true | Enable Flash Attention |
| `options.offload_to_cpu` | boolean | No | false | Offload model to CPU |
| `options.enable_mmap` | boolean | No | true | Use memory-mapped file loading for models |
| `options.vae_decode_only` | boolean | No | true | VAE decode only mode |
| `options.tae_preview_only` | boolean | No | false | Only use TAESD for preview, not final |
| `options.free_params_immediately` | boolean | No | false | Free model params after loading |
| `options.flow_shift` | float | No | auto | Flow shift (INFINITY = auto-detect) |
| `options.weight_type` | string | No | "" | Weight type (f32, f16, q8_0, q5_0, q4_0) |
| `options.tensor_type_rules` | string | No | "" | Per-tensor weight rules (e.g., "^vae\.=f16") |
| `options.rng_type` | string | No | "cuda" | RNG type: std_default, cuda, cpu |
| `options.sampler_rng_type` | string | No | "" | Sampler RNG (empty = use rng_type) |
| `options.prediction` | string | No | "" | Prediction type override (eps, v, edm_v, sd3_flow, flux_flow, flux2_flow, or empty for auto) |
| `options.lora_apply_mode` | string | No | "auto" | LoRA apply mode: auto, immediately, at_runtime |
| `options.vae_tiling` | boolean | No | false | Enable VAE tiling for large images |
| `options.vae_tile_size_x` | integer | No | 0 | VAE tile size X (0 = auto) |
| `options.vae_tile_size_y` | integer | No | 0 | VAE tile size Y (0 = auto) |
| `options.vae_tile_overlap` | float | No | 0.5 | VAE tile overlap (0.0-1.0) |
| `options.chroma_use_dit_mask` | boolean | No | true | Chroma: use DiT mask |
| `options.chroma_use_t5_mask` | boolean | No | false | Chroma: use T5 mask |
| `options.chroma_t5_mask_pad` | integer | No | 1 | Chroma: T5 mask padding |

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

**Error Response (500):**

```json
{
    "error": "Internal error: Model not found: invalid_model.safetensors"
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
| `control_image_base64` | string | No | - | Base64-encoded pre-processed control image (requires ControlNet loaded with model) |
| `control_strength` | float | No | 0.9 | ControlNet influence strength (0.0 - 1.0) |
| `vae_tiling` | boolean | No | false | Enable VAE tiling for large images |
| `vae_tile_size_x` | integer | No | 0 | VAE tile X size (0 = auto) |
| `vae_tile_size_y` | integer | No | 0 | VAE tile Y size (0 = auto) |
| `vae_tile_overlap` | float | No | 0.5 | VAE tile overlap |
| `easycache` | boolean | No | false | Enable caching for DiT models (speeds up generation) |
| `easycache_threshold` | float | No | 0.2 | Cache reuse threshold (higher = more reuse, lower quality) |
| `easycache_start` | float | No | 0.15 | Cache start percent (when to start caching) |
| `easycache_end` | float | No | 0.95 | Cache end percent (when to stop caching) |
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
| `control_image_base64` | string | No | - | Base64-encoded pre-processed control image (requires ControlNet loaded with model) |
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
| `easycache` | boolean | No | false | Enable caching for DiT models (speeds up generation) |
| `easycache_threshold` | float | No | 0.2 | Cache reuse threshold (higher = more reuse, lower quality) |
| `easycache_start` | float | No | 0.15 | Cache start percent (when to start caching) |
| `easycache_end` | float | No | 0.95 | Cache end percent (when to stop caching) |

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

Get all jobs and queue status.

**Response:**

```json
{
    "pending_count": 2,
    "processing_count": 1,
    "completed_count": 5,
    "failed_count": 0,
    "total_count": 8,
    "items": [
        {
            "job_id": "550e8400-e29b-41d4-a716-446655440000",
            "type": "txt2img",
            "status": "completed",
            "progress": {
                "step": 20,
                "total_steps": 20,
                "current_image": 0,
                "total_images": 1,
                "all_steps": 20,
                "current_all_steps": 20,
                "percentage": 100.0
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
            }
        },
        {
            "job_id": "550e8400-e29b-41d4-a716-446655440001",
            "type": "txt2img",
            "status": "processing",
            "progress": {
                "step": 10,
                "total_steps": 20,
                "current_image": 1,
                "total_images": 4,
                "all_steps": 80,
                "current_all_steps": 30,
                "percentage": 37.5
            },
            "created_at": "2024-01-01T12:01:00Z",
            "started_at": "2024-01-01T12:01:05Z",
            "outputs": [],
            "params": {
                "prompt": "a dog in the park",
                "width": 512,
                "height": 512
            }
        }
    ]
}
```

**Queue Status Fields:**

| Field | Type | Description |
|-------|------|-------------|
| `pending_count` | integer | Jobs waiting to be processed |
| `processing_count` | integer | Jobs currently being processed (0 or 1) |
| `completed_count` | integer | Successfully completed jobs |
| `failed_count` | integer | Failed jobs |
| `total_count` | integer | Total jobs in history |

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
        "total_steps": 20,
        "current_image": 0,
        "total_images": 1,
        "all_steps": 20,
        "current_all_steps": 20,
        "percentage": 100.0
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
    }
}
```

**Job Object Fields:**

| Field | Type | Description |
|-------|------|-------------|
| `job_id` | string | Unique job identifier (UUID) |
| `type` | string | Job type: `txt2img`, `img2img`, `txt2vid`, `upscale` |
| `status` | string | Job status (see [Job Status](#job-status)) |
| `progress` | object | Current progress information |
| `progress.step` | integer | Current step within current image |
| `progress.total_steps` | integer | Total steps per image (diffusion steps) |
| `progress.current_image` | integer | Current image being generated (0-indexed) |
| `progress.total_images` | integer | Total images in batch |
| `progress.all_steps` | integer | Total steps for entire job (total_steps × total_images) |
| `progress.current_all_steps` | integer | Current step across all images |
| `progress.percentage` | float | Overall progress percentage (0.0 - 100.0) |
| `created_at` | string | ISO 8601 timestamp when job was created |
| `started_at` | string | ISO 8601 timestamp when processing started (only if processing/completed/failed) |
| `completed_at` | string | ISO 8601 timestamp when job completed (only if completed/failed) |
| `outputs` | array | List of output file paths (relative to `/output/`) |
| `params` | object | Original request parameters (optional) |
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
- Returns interactive HTML file browser with:
  - Folder and file listing with icons
  - Thumbnails for images and videos
  - File size and modification date
  - Sortable columns
  - Breadcrumb navigation
  - Lightbox for image/video preview
  - Statistics (folder count, file count, total size)

**File Response (200):**
- Content-Type: Determined by file extension
- Returns raw file content

**Example Requests:**

```bash
# Browse root output directory
curl http://localhost:8080/output

# Browse a job folder
curl http://localhost:8080/output/550e8400-e29b-41d4-a716-446655440000

# Download a specific image
curl -O http://localhost:8080/output/550e8400-e29b-41d4-a716-446655440000/image_0.png

# Sort by date, descending
curl "http://localhost:8080/output?sort=date&order=desc"
```

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

**Example:**

```bash
curl http://localhost:8080/thumb/550e8400-e29b-41d4-a716-446655440000/image_0.png > thumbnail.jpg
```

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

### `GET /options`

Get available generation options including samplers, schedulers, and quantization types.

**Response:**

```json
{
    "samplers": [
        "euler", "euler_a", "heun", "dpm2", "dpm++2s_a", "dpm++2m", "dpm++2mv2",
        "ipndm", "ipndm_v", "lcm", "ddim_trailing", "tcd"
    ],
    "schedulers": [
        "discrete", "karras", "exponential", "ays", "gits", "sgm_uniform",
        "simple", "smoothstep", "lcm"
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

## Ollama Integration

Integration with Ollama LLM for prompt enhancement.

### Enhance Prompt

#### `POST /ollama/enhance`

Enhance an image generation prompt using Ollama LLM.

**Request:**

```json
{
    "prompt": "a cat",
    "system_prompt": "Custom system prompt (optional)"
}
```

**Response:**

```json
{
    "success": true,
    "id": "550e8400-e29b-41d4-a716-446655440000",
    "original_prompt": "a cat",
    "enhanced_prompt": "a beautiful fluffy orange tabby cat sitting gracefully...",
    "model_used": "llama3.2",
    "created_at": 1704067200
}
```

### Get Enhancement History

#### `GET /ollama/history`

Retrieve prompt enhancement history with pagination.

**Query Parameters:**

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `limit` | integer | 50 | Number of entries to return |
| `offset` | integer | 0 | Pagination offset |

**Response:**

```json
{
    "total_count": 42,
    "items": [
        {
            "id": "550e8400-e29b-41d4-a716-446655440000",
            "original_prompt": "a cat",
            "enhanced_prompt": "a beautiful fluffy orange tabby cat...",
            "model_used": "llama3.2",
            "created_at": 1704067200,
            "success": true,
            "error_message": ""
        }
    ]
}
```

### Get History Entry

#### `GET /ollama/history/{id}`

Get a specific prompt enhancement history entry.

**Response:** Same as single item in history list.

**Error:** 404 if not found.

### Delete History Entry

#### `DELETE /ollama/history/{id}`

Delete a specific history entry.

**Response:**

```json
{
    "success": true
}
```

### Clear History

#### `DELETE /ollama/history`

Clear all prompt enhancement history.

**Response:**

```json
{
    "success": true
}
```

### Get Ollama Status

#### `GET /ollama/status`

Get Ollama client connection status.

**Response:**

```json
{
    "enabled": true,
    "connected": true,
    "base_url": "http://localhost:11434",
    "model": "llama3.2",
    "max_history": 100,
    "history_count": 42
}
```

### Get Ollama Models

#### `GET /ollama/models`

Get list of available models from Ollama server.

**Response:**

```json
{
    "models": ["llama3.2", "mistral", "neural-chat"]
}
```

### Get Ollama Settings

#### `GET /ollama/settings`

Get current Ollama configuration.

**Response:**

```json
{
    "enabled": true,
    "base_url": "http://localhost:11434",
    "model": "llama3.2",
    "timeout_seconds": 60,
    "max_history": 100,
    "api_key": "***masked***"
}
```

### Update Ollama Settings

#### `PUT /ollama/settings`

Update Ollama configuration at runtime.

**Request:**

```json
{
    "enabled": true,
    "base_url": "http://localhost:11434",
    "model": "llama3.2",
    "timeout_seconds": 60,
    "max_history": 100,
    "api_key": "optional_key"
}
```

**Response:**

```json
{
    "success": true,
    "settings": {
        "enabled": true,
        "base_url": "http://localhost:11434",
        "model": "llama3.2",
        "timeout_seconds": 60,
        "max_history": 100
    }
}
```

---

## Assistant Integration

LLM-powered assistant for the Web UI with context awareness.

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

**Response:**

```json
{
    "success": true,
    "message": "I'll load the SDXL model for you. Here's what I'm doing...",
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

The `actions` array contains suggested actions that the Web UI can execute.

### Get Assistant History

#### `GET /assistant/history`

Get conversation history with the assistant.

**Response:**

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

### Clear Assistant History

#### `DELETE /assistant/history`

Clear conversation history.

**Response:**

```json
{
    "success": true
}
```

### Get Assistant Status

#### `GET /assistant/status`

Get assistant connection status.

**Response:**

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

### Get Assistant Settings

#### `GET /assistant/settings`

Get assistant configuration.

**Response:**

```json
{
    "enabled": true,
    "base_url": "http://localhost:11434",
    "model": "llama3.2",
    "timeout_seconds": 120,
    "max_history": 50,
    "api_key": "***masked***"
}
```

### Update Assistant Settings

#### `PUT /assistant/settings`

Update assistant configuration at runtime.

**Request:**

```json
{
    "enabled": true,
    "base_url": "http://localhost:11434",
    "model": "llama3.2",
    "timeout_seconds": 120,
    "max_history": 50,
    "api_key": "optional_key"
}
```

**Response:**

```json
{
    "success": true,
    "settings": {
        "enabled": true,
        "base_url": "http://localhost:11434",
        "model": "llama3.2",
        "timeout_seconds": 120,
        "max_history": 50
    }
}
```

---

## Model Architecture

### `GET /architectures`

Get all supported model architecture presets and current architecture info.

**Response:**

```json
{
    "architectures": {
        "SD1": {
            "display_name": "Stable Diffusion 1.5",
            "requiredComponents": ["clip"],
            "optionalComponents": ["vae", "lora", "controlnet"],
            "generationDefaults": {
                "width": 512,
                "height": 512,
                "steps": 20,
                "cfg_scale": 7.0
            }
        },
        "SDXL": {
            "display_name": "Stable Diffusion XL",
            "requiredComponents": ["clip_l", "clip_g"],
            "optionalComponents": ["vae", "lora", "controlnet"],
            "generationDefaults": {
                "width": 1024,
                "height": 1024,
                "steps": 25,
                "cfg_scale": 7.0
            }
        },
        "Flux": {
            "display_name": "Flux",
            "requiredComponents": ["clip_l", "t5xxl", "vae"],
            "optionalComponents": ["lora"],
            "generationDefaults": {
                "width": 1024,
                "height": 1024,
                "steps": 20,
                "cfg_scale": 1.0
            }
        }
    },
    "current_architecture": "SDXL",
    "current_preset": {
        "display_name": "SDXL",
        "requiredComponents": ["clip_l", "clip_g"],
        "generationDefaults": {}
    }
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
| `model_type` | string | Yes | Target model type (checkpoint, vae, lora, etc.) |
| `source` | string | No | Source type: `url`, `civitai`, `huggingface` (auto-detected) |
| `model_id` | string | For CivitAI | CivitAI model ID |
| `url` | string | For URL | Direct download URL |
| `repo_id` | string | For HuggingFace | HuggingFace repository ID |
| `filename` | string | For HuggingFace | Filename in repository |
| `subfolder` | string | No | Target subfolder in model directory |
| `revision` | string | No | Git revision for HuggingFace (default: "main") |

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

### Get CivitAI Model Info

#### `GET /models/civitai/{id}`

Get metadata for a CivitAI model.

**Path Parameters:**

- `id`: CivitAI model ID. Supports format `123456` or `123456:789012` (model:version)

**Response:**

```json
{
    "success": true,
    "model_id": "123456",
    "version_id": "789012",
    "name": "Model Name",
    "version_name": "v1.0",
    "type": "checkpoint",
    "base_model": "SDXL 1.0",
    "filename": "model.safetensors",
    "file_size": 5368709120,
    "sha256": "abc123...",
    "download_url": "https://civitai.com/..."
}
```

### Get HuggingFace Model Info

#### `GET /models/huggingface`

Get metadata for a HuggingFace model file.

**Query Parameters:**

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `repo_id` | string | Yes | HuggingFace repository ID (e.g., "stabilityai/sdxl-turbo") |
| `filename` | string | Yes | Filename within repository |
| `revision` | string | No | Git revision (default: "main") |

**Response:**

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

### Get Model Paths

#### `GET /models/paths`

Get configured model storage paths.

**Response:**

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
| `lcm` | LCM scheduler |

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
    "prompt": "A solitary 27-year-old woman sits sideways on a weathered, moss-flecked tree trunk that lies diagonally across a quiet forest clearing; she faces camera-left, torso gently twisted toward the viewer. She wears matte-black aviator sunglasses, a charcoal canvas field jacket closed at the front, and a loosely draped camel-brown wool scarf. Bare oak and birch trunks rise in soft tiers behind her, their branches delicate and leafless; the ground is carpeted with dry, pale ochre grass. Diffused late-afternoon sunlight filters through the canopy. Shot on 35mm film aesthetic, fine grain, natural color balance, serene and contemplative mood.",
    "negative_prompt": "piercings, jewelry, tattoos",
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

**Z-Image with LoRA:**

```bash
curl -X POST http://localhost:8080/txt2img \
  -H "Content-Type: application/json" \
  -d '{
    "prompt": "<lora:zimage_amberrayne_v1:0.6>A cinematic photograph of a solitary hooded figure walking through a rain-slicked metropolis at night. The city lights are a chaotic blur of neon orange and cool blue, reflecting on the wet asphalt. Moody, atmospheric, dark academic.",
    "width": 1024,
    "height": 512,
    "steps": 8,
    "cfg_scale": 1.0,
    "sampler": "euler",
    "scheduler": "smoothstep"
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

**Load Wan2.1 T2V 14B (quantized, with CPU offload for low VRAM):**

```bash
curl -X POST http://localhost:8080/models/load \
  -H "Content-Type: application/json" \
  -d '{
    "model_name": "wan2.1-t2v-14b-Q8_0.gguf",
    "model_type": "diffusion",
    "vae": "wan_2.1_vae.safetensors",
    "t5xxl": "umt5-xxl-encoder-Q8_0.gguf",
    "options": {
      "flash_attn": true,
      "offload_to_cpu": true,
      "flow_shift": 3.0
    }
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
    print(f"Status: {status['status']}, Image {prog['current_image']}/{prog['total_images']}, "
          f"Step {prog['step']}/{prog['total_steps']}, Progress: {prog['percentage']:.1f}%")

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
  CUR_IMG=$(echo $STATUS | jq -r '.progress.current_image')
  TOTAL_IMG=$(echo $STATUS | jq -r '.progress.total_images')
  PROGRESS=$(echo $STATUS | jq -r '.progress.percentage')
  echo "Status: $STATE, Image $CUR_IMG/$TOTAL_IMG, Step $STEP/$TOTAL, Progress: $PROGRESS%"

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

---

## WebSocket Support

Real-time progress updates are available via WebSocket connection on a separate port (default: 8081).

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
| `model_unloaded` | Model was unloaded |
| `upscaler_loaded` | Upscaler finished loading |
| `upscaler_unloaded` | Upscaler was unloaded |

#### Server Events

| Event | Description |
|-------|-------------|
| `server_status` | Periodic heartbeat/status update |

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
