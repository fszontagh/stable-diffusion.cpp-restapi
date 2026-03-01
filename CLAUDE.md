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
│   ├── request_handlers.cpp # HTTP route handlers
│   ├── queue_manager.cpp  # Job queue with recycle bin
│   ├── model_manager.cpp  # Model loading/unloading
│   └── sd_wrapper.cpp     # sd.cpp integration
├── include/               # C++ headers
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

### Adding New Model Load Options

1. Add to `ModelLoadParams` struct in `include/model_manager.hpp`
2. Parse from JSON in `ModelLoadParams::from_json()` in `src/model_manager.cpp`
3. Apply in `load_model()` function
4. Store in `loaded_options_` for health endpoint
5. Add TypeScript types in `webui/src/api/client.ts` (LoadOptions, LoadModelParams)
6. Add UI controls in `webui/src/views/ModelLoad.vue`
7. Add display label in `webui/src/views/Dashboard.vue` formatOptionLabel()

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

# Check if experimental offload is enabled
curl -s http://localhost:8080/health | jq '.features.experimental_offload'
```

## Common Issues

- **WebUI shows "Loading application..."**: Check if mutex is held too long blocking /health endpoint
- **ESRGAN models not showing**: Only .pth files that are ZIP archives are supported (magic bytes check)
- **Upscaler progress not showing**: Ensure progress callback uses "report-all mode" (expected_steps=0)
