# CLAUDE.md - Project Context for Claude Code

## Project Overview

**sdcpp-restapi** is a REST API server wrapping stable-diffusion.cpp for image/video generation. It provides:
- HTTP REST API for txt2img, img2img, txt2vid, upscaling
- WebSocket for real-time progress updates
- Vue.js WebUI for browser-based interaction
- Queue-based job processing with soft-delete recycle bin

## Repository Structure

```
/data/sdcpp-restapi/
├── src/                    # C++ backend source
│   ├── main.cpp           # Entry point, server setup
│   ├── request_handlers.cpp # HTTP route handlers + API registry setup
│   ├── api_registry.cpp   # OpenAPI 3.1 schema generator
│   ├── queue_manager.cpp  # Job queue with recycle bin
│   ├── model_manager.cpp  # Model loading/unloading
│   └── sd_wrapper.cpp     # sd.cpp integration
├── include/               # C++ headers
│   ├── api_schema.hpp     # Schema field types, SchemaBuilder fluent API
│   ├── api_registry.hpp   # ApiRegistry class (route + schema registration)
│   ├── api_schemas.hpp    # Master include for all schema structs
│   └── api_schemas/       # Schema struct definitions by domain
│       ├── common.hpp     # Shared enums, SuccessResponse, JobCreatedResponse
│       ├── generation.hpp # GenerationRequestBase, Txt2Img/Img2Img/Txt2Vid/Upscale
│       ├── models.hpp     # LoadModelRequest, LoadOptions, download schemas
│       ├── queue.hpp      # Queue listing, job status, recycle bin schemas
│       ├── health.hpp     # Health, memory, options response schemas
│       ├── settings.hpp   # Preview, architecture, settings schemas
│       └── assistant.hpp  # Assistant chat/status schemas
├── webui/                 # Vue.js frontend
│   ├── src/views/         # Page components
│   ├── src/components/    # Reusable components
│   ├── src/stores/app.ts  # Pinia state store
│   └── src/api/client.ts  # API client with types
├── CMakeLists.txt         # Build configuration
└── config.example.json    # Server configuration template
```

## Build Commands

```bash
# Standard build
mkdir build && cd build
cmake ..
make -j$(nproc)

# With experimental VRAM offloading (uses forked sd.cpp)
cmake -DSD_EXPERIMENTAL_OFFLOAD=ON ..
make -j$(nproc)

# WebUI build
cd webui
npm install
npm run build
```

## Experimental Features

### VRAM Offloading (SD_EXPERIMENTAL_OFFLOAD)

Enables dynamic tensor offloading during generation to reduce VRAM usage. Uses the forked sd.cpp from `feature/vram-offloading` branch.

**To enable:**
```bash
cmake -DSD_EXPERIMENTAL_OFFLOAD=ON ..
```

**Offload Modes:**
- `none`: Keep all components on GPU (default, fastest)
- `cond_only`: Offload only conditioning (LLM/CLIP) after use
- `cond_diffusion`: Offload conditioning + diffusion, keep VAE
- `aggressive`: Offload each component after use (saves most VRAM)
- `layer_streaming`: Stream layers one-by-one (enables models larger than VRAM)

**General Offload Options:**
- `vram_estimation`: dryrun (accurate), formula (fast)
- `offload_cond_stage`: Offload LLM/CLIP after conditioning
- `offload_diffusion`: Offload diffusion model after sampling
- `reload_cond_stage/reload_diffusion`: Reload components after generation
- `target_free_vram_mb`: Target free VRAM before VAE decode (0 = always offload)
- `min_offload_size_mb`: Minimum component size to offload

**Layer Streaming Options (for `layer_streaming` mode):**
- `layer_streaming_enabled`: Enable layer-by-layer streaming execution
- `streaming_prefetch_layers`: Number of layers to prefetch ahead (default: 1)
- `streaming_keep_layers_behind`: Layers to keep after execution (for skip connections)
- `streaming_min_free_vram_mb`: Minimum VRAM to keep free during streaming

**Backend implementation:**
- `include/model_manager.hpp`: `ModelLoadParams` struct with all offload options
- `src/model_manager.cpp`: Parses options, converts to `sd_offload_config_t`
- `src/request_handlers.cpp`: Exposes `features.experimental_offload` in health endpoint

**WebUI implementation:**
- `webui/src/api/client.ts`: TypeScript types for offload options
- `webui/src/stores/app.ts`: `experimentalOffloadEnabled` computed property
- `webui/src/views/ModelLoad.vue`: VRAM Offloading accordion with layer streaming UI
- `webui/src/views/Dashboard.vue`: Displays active offload settings with labels

**Conditional compilation pattern:**
```cpp
#ifdef SDCPP_EXPERIMENTAL_OFFLOAD
    // Experimental code here
#endif
```

## Key Patterns

### OpenAPI Schema System (Auto-Generated)

The server generates an OpenAPI 3.1 specification at `GET /openapi.json` that is always in sync with the code. The schema is built from:

1. **Schema structs** in `include/api_schemas/*.hpp` using `SchemaBuilder` fluent API
2. **ApiRegistry** in `include/api_registry.hpp` that registers routes + schemas together
3. **`addEndpoint<ReqType, ResType>()`** in `register_routes()` replaces direct `server.Get/Post` calls

**Adding a new endpoint:**
1. Define request/response schema structs in `include/api_schemas/` (use `SchemaBuilder`)
2. Use `api.addEndpoint<MyRequest, MyResponse>(server, "POST", "/path", ...)` in `register_routes()`
3. The OpenAPI spec automatically includes the new endpoint and its schemas

**Schema features:**
- **Inheritance via `allOf`**: e.g., `Txt2ImgRequest` inherits `GenerationRequestBase`
- **`x-architecture-default: true`**: Marks fields whose defaults come from `data/model_architectures.json`
- **Conditional compilation**: `#ifdef SDCPP_EXPERIMENTAL_OFFLOAD` includes/excludes offload fields
- **`$ref` schemas**: Shared schemas like `LoadOptions` need explicit `api.registerSchema()` call
- **Query/path params**: Use `.query()` / `.path_param()` fluent methods on the endpoint builder

**Example:**
```cpp
api.addEndpoint<Txt2ImgRequest, JobCreatedResponse>(
    server, "POST", "/txt2img",
    "Generate image from text prompt", "Generation", 202,
    [this](auto& req, auto& res) { handle_txt2img(req, res); });
```

### Adding New Model Load Options

1. Add to `ModelLoadParams` struct in `include/model_manager.hpp`
2. Parse from JSON in `ModelLoadParams::from_json()` in `src/model_manager.cpp`
3. Apply in `load_model()` function
4. Store in `loaded_options_` for health endpoint
5. Add field to `LoadOptions::schema()` in `include/api_schemas/models.hpp` (for OpenAPI)
6. Add TypeScript types in `webui/src/api/client.ts` (LoadOptions, LoadModelParams)
7. Add UI controls in `webui/src/views/ModelLoad.vue`
8. Add display label in `webui/src/views/Dashboard.vue` formatOptionLabel()

### WebSocket Events

Events broadcast from `src/websocket_server.cpp`:
- `job_added`, `job_status_changed`, `job_progress`, `job_preview`, `job_cancelled`
- `model_loading_progress`, `model_loaded`, `model_load_failed`, `model_unloaded`
- `upscaler_loaded`, `upscaler_unloaded`
- `server_status`, `server_shutdown`

### Queue Soft-Delete (Recycle Bin)

Jobs are soft-deleted with `status=deleted` and `deleted_at` timestamp. Auto-purged after configurable retention period. See `src/queue_manager.cpp`.

## Testing

```bash
# Run server
./sdcpp-restapi --config ../config.json

# Health check
curl http://localhost:8080/health

# OpenAPI schema (auto-generated, always current)
curl http://localhost:8080/openapi.json | jq '.paths | keys | length'  # endpoint count
curl http://localhost:8080/openapi.json | jq '.components.schemas | keys'  # schema names

# Check if experimental offload is enabled
curl -s http://localhost:8080/health | jq '.features.experimental_offload'
```

## Supported Model Architectures

Architecture presets are defined in `data/model_architectures.json` and auto-reloaded on change.

**Key architectures:**
- SD1/SD2/SDXL: Standard Stable Diffusion (VAE optional)
- SD3: Requires CLIP-L, CLIP-G, T5-XXL
- Flux: Requires VAE (ae.safetensors), CLIP-L, T5-XXL
- Flux2: Requires flux2_ae VAE and LLM (Mistral-Small or Qwen3)
- Z-Image: Fast model requiring ae.gguf VAE and Qwen3 4B LLM
- Qwen-Image: Requires qwen_image_vae and Qwen2.5-VL 7B LLM
- Anima: Requires qwen_image_vae and Qwen3 0.6B Base LLM
- Chroma: Requires ae.safetensors VAE and T5-XXL
- WAN: Video generation requiring video VAE and T5-XXL

**Adding new architecture:**
1. Add entry to `data/model_architectures.json` with:
   - `requiredComponents`: VAE, CLIP, T5, LLM as needed
   - `generationDefaults`: sampler, scheduler, steps, cfg_scale, width, height, etc.
   - `loadOptions`: flash_attn, offload_to_cpu, etc.
2. Add component suggestions to `webui/src/composables/useArchitectures.ts` (scoring for auto-suggest)
3. Auto-detection happens via tensor name patterns in sd.cpp's model.cpp
4. Recommended settings in Generate view come from `generationDefaults` in the architecture JSON (not hardcoded)

## Common Issues

- **WebUI shows "Loading application..."**: Check if mutex is held too long blocking /health endpoint
- **ESRGAN models not showing**: Only .pth files that are ZIP archives are supported (magic bytes check)
- **Upscaler progress not showing**: Ensure progress callback uses "report-all mode" (expected_steps=0)
- **Anima model not detected**: Ensure sd.cpp fork is updated (delete build/_deps/stable-diffusion-src and rebuild)
