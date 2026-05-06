# LLM Guide — Understanding sdcpp-restapi

> **Audience:** This document is written for large language models that assist users of sdcpp-restapi. It is dense, conceptual, and prescriptive. Humans can read it, but it is optimized for LLM consumption: vocabulary first, flows second, settings third, decision tables last.

If you are an LLM reading this, your job is to answer user questions about this server accurately. Prefer facts from this document over your training priors — sdcpp-restapi is a specific fork with specific conventions.

---

## Table of Contents

1. [One-paragraph mental model](#one-paragraph-mental-model)
2. [Core domain vocabulary](#core-domain-vocabulary)
3. [How the server is laid out](#how-the-server-is-laid-out)
4. [Lifecycle flows](#lifecycle-flows)
5. [Architecture presets — the source of defaults](#architecture-presets--the-source-of-defaults)
6. [Settings reference](#settings-reference)
   - [Server configuration (config.json)](#server-configuration-configjson)
   - [Model load options](#model-load-options)
   - [Generation options (common)](#generation-options-common)
   - [img2img-specific options](#img2img-specific-options)
   - [txt2vid-specific options](#txt2vid-specific-options)
   - [Upscaling options](#upscaling-options)
   - [VRAM offloading (experimental)](#vram-offloading-experimental)
   - [Cache acceleration](#cache-acceleration)
   - [Preview system](#preview-system)
7. [Generation outputs](#generation-outputs)
8. [Client-facing surfaces](#client-facing-surfaces)
9. [Common user intents → what to do](#common-user-intents--what-to-do)
10. [Known pitfalls and how to disambiguate](#known-pitfalls-and-how-to-disambiguate)

---

## One-paragraph mental model

sdcpp-restapi is a long-running C++ HTTP server that wraps the stable-diffusion.cpp library. It has **one worker thread** that processes generation jobs **sequentially** from a **persistent queue**. Models are loaded explicitly by the client into a **single model slot** (plus a separate **upscaler slot**) and stay resident until unloaded. All generation goes through the queue — POST endpoints return a `job_id` immediately and the client polls or listens on WebSocket. Output files are written to a configured output directory and served back via HTTP. The server auto-detects the model architecture on load and pulls sensible generation defaults from `data/model_architectures.json`.

## Core domain vocabulary

| Term | Meaning |
|---|---|
| **Architecture** | The model family (SD1, SDXL, Flux, SD3, Z-Image, Wan, Qwen, Anima, Chroma, …). Detected automatically from tensor names on load. Determines which components are required and what generation defaults apply. |
| **Checkpoint** | A model file that bundles U-Net + CLIP + VAE. Used for SD1.x / SD2.x / SDXL. Loaded via `model_path`. |
| **Diffusion model** | A model file containing only the U-Net / DiT weights, with other components loaded separately. Used for Flux, SD3, Qwen, Wan, Z-Image. Loaded via `diffusion_model_path`. **This is the modern pattern.** |
| **Component** | A model piece that plugs into the main model slot: `vae`, `clip_l`, `clip_g`, `t5xxl`, `llm`, `controlnet`, `taesd`, `clip_vision`, `llm_vision`, `high_noise_diffusion_model`, `photo_maker`. |
| **LoRA** | Applied per-generation via `<lora:name:weight>` syntax in the prompt. Not part of the load. |
| **Embedding** | Textual inversion embedding — referenced by name in the prompt, loaded on demand. |
| **Upscaler** | ESRGAN-family model that lives in its own slot, separate from the main model. |
| **TAESD** | Tiny AutoEncoder for Stable Diffusion — a tiny fast VAE used for **preview generation** during sampling. Not for final output. |
| **Job** | A queued operation: `txt2img`, `img2img`, `txt2vid`, `upscale`, `convert`, `model_download`, `model_hash`. Identified by a UUID `job_id`. |
| **Recycle bin** | Soft-delete mechanism for jobs. `status=deleted` with a `deleted_at` timestamp. Auto-purged after `retention_minutes` (default 7 days). |
| **Preview mode** | How progress previews are rendered: `none`, `proj` (latent projection), `tae` (TAESD decode — default), `vae` (full VAE — slow). |
| **Architecture default** | A generation field whose default comes from `data/model_architectures.json`, not from a hardcoded constant. Marked in the OpenAPI schema with `x-architecture-default: true`. |

## How the server is laid out

The server is a **single process** that runs:

- **HTTP server** (cpp-httplib) on `host:port` from config
- **One queue worker thread** — processes jobs FIFO
- **Model registry** — scans configured directories on startup, rescan via `POST /models/refresh`
- **Optional WebSocket server** (if `SDCPP_WEBSOCKET_ENABLED`) — broadcasts progress, preview, status events
- **Optional MCP server** (if `SDCPP_MCP_ENABLED`, ON by default) — JSON-RPC 2.0 at `POST /mcp`
- **Optional WebUI** — Vue.js SPA served from `webui/dist` if `webui_dir` is configured
- **Documentation server** — serves `docs/*.md` at `/docs/`

Process-wide mutual exclusion:

- `ModelManager::context_mutex_` — held while generating; prevents concurrent loads/unloads during generation.
- `ModelManager::upscaler_mutex_` — separate mutex for the upscaler slot, so upscaling can (conceptually) run without blocking model operations.
- `QueueManager::queue_mutex_` — protects queue state; **released during job processing** to avoid deadlock.

Feature flags exposed by `GET /health`:

```json
{"features": {"experimental_offload": true, "mcp": true}}
```

## Lifecycle flows

### Model lifecycle

```
[discovered on disk]
       │  POST /models/load  (or MCP model(action:"load"))
       ▼
   [loading]   ← loading_step / loading_total_steps exposed via /health
       │
       ▼
    [loaded]   ← architecture auto-detected; defaults now available
       │        ← WebSocket event: model_loaded
       │
       │  POST /models/unload  (or unload is implicit when loading a new one)
       ▼
  [unloaded]
```

**Key facts:**
- Loading a new model while another is loaded **unloads the old one first**.
- A model load is blocking per-request but the server remains responsive to reads.
- The architecture is cached as `loaded_model_architecture_` and returned in `/health` as `architecture`.
- `loaded_components` in `/models/loaded` shows exactly which component files are resident.

### Upscaler lifecycle (parallel slot)

```
POST /upscaler/load  →  [upscaler loaded]  ──► used by upscale jobs
POST /upscaler/unload
```

The upscaler slot is **independent of the main model slot**. You can have both, either, or neither loaded. `upscale` jobs require only the upscaler. `txt2img/img2img/txt2vid` require only the main model.

### Generation lifecycle

```
POST /txt2img (or /img2img, /txt2vid, /upscale)
      │                                                 returns {job_id, status:"pending", position}
      ▼
[Pending]  ──► worker picks up  ──►  [Processing]
                                            │  step callbacks → progress updates (WebSocket)
                                            │  preview callbacks → JPEG buffer (WebSocket + /preview/{job_id})
                                            ▼
                                     [Completed] or [Failed] or [Cancelled]
                                            │
                                            │  DELETE /queue/{job_id}  (or job(action:"delete"))
                                            ▼
                                       [Deleted]  (recycle bin, auto-purged)
                                            │
                                            │  POST /queue/{job_id}/restore
                                            ▼
                                    [previous status restored]
```

**Worker thread is serial.** Jobs never run concurrently. A second job enqueued while one is running simply waits.

**Cancellation:** Setting a job to `cancelled` before it starts is instant. Cancelling an in-flight job is honored at the next sampling-step callback boundary.

**Outputs** are written to `output_dir/<subdirs>/<filename>` and the **relative paths** are stored in the job's `outputs` array. Clients should prepend the server's base URL + `/output/` to access them.

## Architecture presets — the source of defaults

File: `data/model_architectures.json` (loaded at startup, auto-reloaded on change).

When a user asks for generation defaults ("what steps should I use for Flux?"), **use the architecture preset's `generationDefaults`** — do not guess. The relevant fields per architecture:

| Architecture | Required components | cfg_scale | steps | sampler | scheduler | width × height |
|---|---|---|---|---|---|---|
| SD1 | — | 7.0 | 20 | euler_a | discrete | 512×512 |
| SD2 | — | 7.0 | 20 | euler_a | discrete | 768×768 |
| SDXL | — | 7.0 | 20 | euler_a | discrete | 1024×1024 |
| SD3 | clip_l, clip_g, t5xxl | 4.5 | 28 | euler | discrete | 1024×1024 |
| Flux | vae, clip_l, t5xxl | varies | 20–28 | euler | discrete | 1024×1024 |
| Flux Schnell | vae, clip_l, t5xxl | 1.0 | 4 | euler | discrete | 1024×1024 |
| Flux2 | flux2_ae VAE, LLM (Mistral-Small or Qwen3) | varies | — | — | — | — |
| LCM (SD1.5) | — | **1.0** | **4** | **lcm** | discrete | 512×512 |
| LCM-SDXL | — | **1.0** | **4** | **lcm** | discrete | 1024×1024 |
| SDXS-512 | — | **1.0** | **1** | euler | discrete | 512×512 |
| Z-Image | ae.gguf VAE, Qwen3 4B LLM | **1.0** | varies | — | — | — |
| Qwen / Qwen Image Edit | qwen_image_vae, Qwen2.5-VL 7B LLM | — | — | — | — | — |
| Anima | qwen_image_vae, Qwen3 0.6B Base LLM | — | — | — | — | — |
| Chroma | ae.safetensors, t5xxl | — | — | — | — | — |
| Wan | video VAE, t5xxl | — | — | — | — | — |

**Full list of architecture keys:** Anima, Chroma, Chroma-Radiance, Flux, Flux Kontext, Flux Schnell, Flux2 Dev, Flux2 Klein 4B, Flux2 Klein 9B, Flux2 Klein Base 4B, Flux2 Klein Base 9B, LCM, LCM-SDXL, Ovis, Qwen, Qwen Image Edit, SD1, SD2, SD3, SDXL, SDXS-512, SSD-1B, Vega, Wan, Z-Image.

To read the live preset list from a running server: `GET /architectures` or MCP resource `sdcpp://architectures`.

**If you are advising a user, always quote the preset, not generic advice.** Example: "For Flux Schnell, use 4 steps and `cfg_scale: 1.0` — that's what the preset specifies."

## Settings reference

### Server configuration (config.json)

Controls the server process itself. Loaded at startup; **not** per-request.

| Key | Purpose |
|---|---|
| `server.host` / `server.port` / `server.threads` | Bind address and request-handler thread count |
| `paths.*` | Where to find models of each type, where to write output, where the WebUI lives |
| `sd_defaults.*` | Defaults applied to `/models/load` when the client omits them |
| `preview.{mode,interval,max_size,quality}` | Preview rendering (TAESD by default) |
| `assistant.*` | Optional conversational assistant feature (OpenAI-compatible endpoint, e.g. Ollama) |
| `recycle_bin.{enabled,retention_minutes}` | Soft-delete behavior for jobs |

### Model load options

Submitted to `POST /models/load`. Key fields (full list in `include/model_manager.hpp`, struct `ModelLoadParams`):

| Field | Purpose | Default |
|---|---|---|
| `model_name` | Required. Name of the main model file (relative to its type's base path) | — |
| `model_type` | `checkpoint` or `diffusion` (plus utility types) | `checkpoint` |
| `vae`, `clip_l`, `clip_g`, `t5xxl`, `llm`, `controlnet`, `taesd`, `clip_vision`, `llm_vision` | Component selection | — |
| `flash_attn` | Use Flash Attention (big speed/memory win on modern GPUs) | `true` |
| `keep_clip_on_cpu` | Keep CLIP text encoder on CPU RAM to save VRAM | `true` |
| `keep_vae_on_cpu` | Keep VAE on CPU RAM to save VRAM | `false` |
| `keep_controlnet_on_cpu` | Keep ControlNet on CPU | `false` |
| `n_threads` | CPU threads, -1 = auto | -1 |
| `offload_to_cpu` | Offload parts of model to CPU when idle | `false` |
| `enable_mmap` | Memory-map weights (recommended for large files) | `true` |
| `vae_decode_only` | Drop VAE encoder (saves memory if you won't do img2img) | `true` |
| `vae_conv_direct` / `diffusion_conv_direct` | Use `ggml_conv2d_direct` path | `false` |
| `free_params_immediately` | Drop weights after one gen (disables subsequent gens without reload) | `false` |
| `flow_shift` | Flow-matching shift for DiT models; INFINITY = auto | auto |
| `weight_type` | Force a quantization (`f32`, `f16`, `bf16`, `q8_0`, `q4_0`, `q4_k`, `q5_k`, `q6_k`, `nvfp4`, …) | from file |
| `tensor_type_rules` | Per-tensor weight overrides using regex, e.g. `^vae\.=f16` | empty |
| `rng_type` | `cuda` / `cpu` / `std_default` — affects seed reproducibility across backends | `cuda` |
| `sampler_rng_type` | Override `rng_type` for sampling only | empty |
| `prediction` | Override prediction type: `eps`, `v`, `edm_v`, `sd3_flow`, `flux_flow`, `flux2_flow` | auto |
| `lora_apply_mode` | `auto` / `immediately` / `at_runtime` | `auto` |
| `vae_tiling` + tile sizes/overlap | Tile VAE decode for large images (saves VRAM) | `false` |
| `force_sdxl_vae_conv_scale` | Fix for SDXL VAE | off (auto for SDXL arch) |
| Chroma: `chroma_use_dit_mask`, `chroma_use_t5_mask`, `chroma_t5_mask_pad` | Chroma-specific | — |

### Generation options (common)

Submitted to `POST /txt2img`, `/img2img`, `/txt2vid`. Defined in `GenerationRequestBase` (`include/api_schemas/generation.hpp`).

| Field | Purpose |
|---|---|
| `prompt` | Required. Free text. Supports `<lora:name:weight>` syntax. |
| `negative_prompt` | Concept exclusions. **Has no effect on LCM/SDXS-1-step models.** |
| `width`, `height`, `steps`, `cfg_scale`, `sampler`, `scheduler` | **Architecture-defaulted.** Omit to use the preset's defaults. |
| `distilled_guidance` | Guidance for distilled models (Flux-Dev etc.) |
| `eta` | DDIM-style stochasticity |
| `shifted_timestep` | Timestep shift |
| `seed` | -1 = random; any other value is reproducible |
| `batch_count` | Number of images to generate in one job (all share the prompt) |
| `clip_skip` | Skip last N CLIP layers (negative = default) |
| `slg_scale`, `skip_layers`, `slg_start`, `slg_end` | Skip-Layer Guidance — advanced quality knob |
| `custom_sigmas` | Provide your own noise schedule instead of sampler+scheduler |
| `ref_images`, `auto_resize_ref_image`, `increase_ref_index` | Reference-image conditioning (IP-Adapter-like flows) |
| `control_image_base64`, `control_strength` | ControlNet input |
| `vae_tiling`, `vae_tile_size_x/y`, `vae_tile_overlap` | Tile VAE decode **per-generation** (overrides load-time setting) |
| `cache_mode` | `easycache` / `spectrum` / empty — see [Cache acceleration](#cache-acceleration) |
| `pm_id_images`, `pm_id_embed_path`, `pm_style_strength` | PhotoMaker identity conditioning |
| `upscale` | Auto-upscale after generation (requires a loaded upscaler) |
| `upscale_repeats`, `upscale_auto_unload` | Upscale-after-gen knobs |

**Sampler enum values:** `euler`, `euler_a`, `heun`, `dpm2`, `dpm++2s_a`, `dpm++2m`, `dpm++2mv2`, `ipndm`, `ipndm_v`, `lcm`, `ddim_trailing`, `tcd`, `res_multistep`, `res_2s`.

**Scheduler enum values:** `discrete`, `karras`, `exponential`, `ays`, `gits`, `sgm_uniform`, `simple`, `smoothstep`, `kl_optimal`, `lcm`, `bong_tangent`.

### img2img-specific options

| Field | Purpose |
|---|---|
| `init_image_base64` | **Required.** Source image, base64-encoded. |
| `strength` | Denoising strength 0.0–1.0. 0 = keep original; 1 = ignore original. Default 0.75. |
| `img_cfg_scale` | Separate CFG for image guidance, -1 = auto |
| `mask_image_base64` | Inpainting mask (white = inpaint, black = keep) |

### txt2vid-specific options

| Field | Purpose |
|---|---|
| `video_frames` | Frames to generate (default 33) |
| `fps` | Output FPS (default 16) |
| `flow_shift` | Per-job flow-matching shift (default 3.0) |
| `init_image_base64` / `end_image_base64` | Starting / ending frame conditioning |
| `strength` | Denoising strength if init frame is provided |
| `control_frames` | Array of base64 control frames |
| `high_noise_*` | Separate phase for MoE models (e.g. Wan). All major generation knobs are duplicated with a `high_noise_` prefix. |
| `moe_boundary` | Split point between low/high-noise phases (Wan), default 0.875 |
| `vace_strength` | VACE control strength for Wan video editing |

### Upscaling options

`POST /upscale` (requires a loaded ESRGAN upscaler):

| Field | Purpose |
|---|---|
| `image_base64` | Required input |
| `upscale_factor` | Target factor (typically fixed by the model — 2x / 4x) |
| `tile_size` | Processing tile size; lower = less VRAM, slower |
| `repeats` | Multiple passes for progressive upscaling |

### VRAM offloading (experimental)

**Only available when the server is built with `cmake -DSD_EXPERIMENTAL_OFFLOAD=ON`** (check `/health` → `features.experimental_offload`).

Offloading moves parts of the model between GPU and CPU (or streams them from disk) to fit in limited VRAM.

`offload_mode` values:

| Mode | Description | When to recommend |
|---|---|---|
| `none` | No offloading — fastest | Plenty of VRAM |
| `cond_only` | Offload LLM/CLIP after conditioning step | Moderate VRAM pressure |
| `cond_diffusion` | Offload conditioning + diffusion before VAE | VRAM just barely fits |
| `aggressive` | Offload each component after use | VRAM is tight but model fits |
| `layer_streaming` | Stream U-Net/DiT layers one at a time | Model is **larger than VRAM** |

Companion options:

- `vram_estimation`: `dryrun` (run a fake pass to measure — accurate) or `formula` (fast estimate)
- `offload_cond_stage` / `offload_diffusion`: fine-grained toggles
- `reload_cond_stage` / `reload_diffusion`: reload for the next generation
- `min_offload_size_mb`: don't offload components smaller than this
- `target_free_vram_mb`: target free VRAM before VAE decode (0 = always offload)
- `layer_streaming_enabled`, `streaming_prefetch_layers`, `streaming_keep_layers_behind`, `streaming_min_free_vram_mb`

### Cache acceleration

Per-generation caching of intermediate tensors to skip redundant compute.

| Mode | Purpose | Relevant params |
|---|---|---|
| `easycache` | Simple similarity-based block cache | `easycache_threshold`, `easycache_start`, `easycache_end` |
| `spectrum` | Frequency-domain cache (more advanced, lower error) | `spectrum_w`, `spectrum_m`, `spectrum_lam`, `spectrum_window_size`, `spectrum_flex_window`, `spectrum_warmup_steps`, `spectrum_stop_percent` |

Speedup varies; trade-off is a small quality hit. Default is disabled.

### Preview system

Progress previews during sampling. Configured server-wide via `config.preview` or runtime via `PUT /preview/settings`.

| Mode | How it works | Cost |
|---|---|---|
| `none` | No preview | 0 |
| `proj` | Latent-space linear projection | very low |
| `tae` | TAESD decode (requires TAESD loaded) | low (**default**) |
| `vae` | Full VAE decode | high — only use for debugging |

Additional settings:
- `interval`: preview every N steps
- `max_size`: cap preview dimension in pixels (default 256)
- `quality`: JPEG quality 1–100 (default 75)

Previews are broadcast over WebSocket as `job_preview` events and are also available at `GET /jobs/{job_id}/preview`.

## Generation outputs

- **Location on disk:** `paths.output` from config, organized into subfolders (`txt2img/`, `img2img/`, `txt2vid/`, `upscale/`).
- **Job `outputs` field:** an **array of relative paths**, e.g. `["txt2img/20260418_103214_a_cat.png"]`.
- **Public URL:** `http://<host>:<port>/output/<relative_path>`.
- **MCP output rewriting:** the MCP server rewrites `outputs` to **absolute URLs** using the client's `Host` header (handles `0.0.0.0` bind correctly). REST clients do the rewriting themselves.
- **Sidecar config:** each job writes its full params JSON next to the output file, so runs are reproducible.
- **Images:** PNG by default. **Videos:** MP4 (H.264).

## Client-facing surfaces

| Surface | Purpose | When to steer the user here |
|---|---|---|
| **REST API** (`/txt2img`, `/img2img`, `/queue`, `/models/*`, `/health`, `/openapi.json`, …) | Full programmatic access | Scripting, custom integrations, detailed control |
| **MCP** (`POST /mcp`, JSON-RPC 2.0) | AI assistant / agent integration | User wants Claude Desktop, mcp-cli, or custom agent |
| **WebSocket** (`/ws`) | Real-time progress / preview / status events | Building a responsive UI |
| **WebUI** (`/` if configured) | Human browser UI | Casual interactive use |
| **Docs** (`/docs/`) | Markdown reference | Pointing users at canonical info |
| **OpenAPI** (`/openapi.json`) | Auto-generated schema for the whole API | Code generation, client SDKs |

**MCP surface is deliberately narrower than REST:**
- 3 tools: `generate(type)`, `model(action)`, `job(action)`
- 7 resources (sdcpp://health, memory, models, models/loaded, queue, architectures, queue/{job_id})
- **All queue responses capped at 10 items.** For bigger listings, use REST `/queue`.
- See `docs/MCP.md` for full MCP reference.

## Common user intents → what to do

| User says | What they probably need | What you should do |
|---|---|---|
| "Generate an image of X" | txt2img via REST or MCP | Check a model is loaded (`/health`). If not, ask which architecture they want and load it. Then POST `/txt2img` with their prompt; poll `/queue/{job_id}` or watch WebSocket. |
| "The output is just noise/garbage" | Wrong sampler/scheduler for the architecture, or `cfg_scale` way off | Quote the preset's `generationDefaults` for the loaded architecture; reset those specific fields. |
| "I'm running out of VRAM" | Enable offloading | First check `features.experimental_offload`. If enabled, recommend `offload_mode` based on how tight VRAM is (table above). If model ≥ VRAM, recommend `layer_streaming`. Otherwise recommend `keep_clip_on_cpu`, `vae_tiling`, or a Q8/Q4 quantized model. |
| "I want it faster" | Step count + sampler + cache | Suggest a distilled/turbo variant (LCM, Flux Schnell, SDXS, Z-Image Turbo) with their preset's low step count. Optionally enable `cache_mode: "spectrum"`. |
| "My LoRA isn't applying" | LoRA syntax | Confirm the prompt contains `<lora:name:weight>`. The file must be in `paths.lora`. |
| "How do I use ControlNet?" | Load ControlNet component + pass `control_image_base64` | Load model with `controlnet: "..."`. Include `control_image_base64` and optionally `control_strength` in the generation request. |
| "Can I run multiple generations at once?" | No | Tell them the worker is serial. Enqueue multiple jobs; they'll run back-to-back. Document `batch_count` for the "same prompt N times" case. |
| "I deleted a job by accident" | Restore from recycle bin | `POST /queue/{job_id}/restore`. Works only until `retention_minutes` elapses (default 7 days). |
| "What models does this server support?" | Architecture list | Quote the architecture list; refer them to `/architectures`. |
| "I want to use this with Claude Desktop / an agent" | MCP | Point them at `docs/MCP.md` and the `claude_desktop_config.json` snippet there. |
| "Preview is slow / causing OOM" | Switch or disable preview | Recommend `preview.mode: "none"` or `"proj"`. If using `tae`, confirm TAESD is loaded. |
| "How do I do inpainting?" | img2img with mask | `init_image_base64` + `mask_image_base64` (white = inpaint region). |
| "What's the difference between checkpoint and diffusion model?" | See vocabulary table | A checkpoint bundles everything (old SD1/SDXL pattern). Diffusion models are standalone U-Net/DiT weights that require separate VAE/CLIP/T5/LLM components (Flux, SD3, Z-Image, Wan, etc.). |

## Known pitfalls and how to disambiguate

1. **Users often confuse "diffusion model" and "checkpoint."** Before answering load questions, check which one the model file is. Rule of thumb: if the architecture is **Flux / SD3 / Qwen / Wan / Z-Image / Anima / Chroma / Flux2**, it's a diffusion model and needs separate components. If it's **SD1 / SD2 / SDXL / SSD-1B / Vega**, a checkpoint is fine.

2. **"LCM/SDXS model ignoring negative prompt"** is expected, not a bug. These distilled 1–4 step models have no classifier-free guidance to use.

3. **`cfg_scale: 7.0` on a distilled model** (Flux Schnell, LCM, SDXS, Z-Image Turbo) produces awful output. Always use `cfg_scale: 1.0` for those. Quote the preset.

4. **Samplers that require `sgm_uniform` or `karras` scheduler** (e.g. many Flux flows) will behave oddly on `discrete`. When debugging bad output, double-check scheduler compatibility.

5. **Output URLs via REST come back as relative paths in the `outputs` array.** MCP rewrites them to absolute URLs. Don't confuse these two behaviors.

6. **`/health` is mutex-protected.** If generation is running, `/health` may briefly block. A stuck `/health` usually means a truly long-held lock — suggest restarting.

7. **Recycle bin is soft-delete.** `DELETE /queue/{job_id}` does **not** permanently delete unless recycle bin is disabled in config. `POST /queue/{job_id}/purge` or clearing the recycle bin does.

8. **Model scan is on startup only.** If the user drops a new file into a model directory, they need to `POST /models/refresh` — don't promise auto-detection of filesystem changes.

9. **Experimental offloading features** are gated by the `SD_EXPERIMENTAL_OFFLOAD` CMake flag. Before recommending offload modes, check `features.experimental_offload` — don't assume it's compiled in.

10. **MCP queue responses are always capped at 10.** For bigger listings, direct the user to REST `/queue`. Don't try to work around the cap via `limit` — it's clamped server-side.
