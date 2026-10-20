# CLAUDE.md - Project Context for Claude Code

## Project Overview

**sdcpp-restapi** is a REST API server wrapping [stable-diffusion.cpp](https://github.com/leejet/stable-diffusion.cpp) for image/video generation. It provides:
- HTTP REST API for txt2img, img2img, txt2vid, upscaling, model conversion and model downloads
- WebSocket for real-time progress, previews and queue events
- Vue.js WebUI (Generate, Queue, Models, Downloads, Settings, ...)
- MCP server (Model Context Protocol) for LLM clients
- Queue-based job processing with a soft-delete recycle bin
- Auto-generated OpenAPI 3.1 spec at `/openapi.json` and raw docs at `/docs/`

Contributor rules live in `CONTRIBUTING.md` (one clear change per PR, no AI co-author trailers), security reporting in `SECURITY.md`.

## Repository Structure

```
├── src/                     # C++ backend
│   ├── main.cpp             # Entry point, server setup
│   ├── request_handlers.cpp # HTTP route handlers + API registry setup, llms.txt
│   ├── api_registry.cpp     # OpenAPI 3.1 schema generator
│   ├── queue_manager.cpp    # Job queue, recycle bin, output path templates
│   ├── model_manager.cpp    # Model scan/load/unload, load options
│   ├── architecture_manager.cpp # Architecture presets (data/model_architectures.json)
│   ├── download_manager.cpp # HF / CivitAI / URL downloads, HF directory bundles
│   ├── sd_wrapper.cpp       # sd.cpp integration, per-request param parsing
│   ├── mcp_server.cpp       # MCP server
│   ├── websocket_server.cpp # WebSocket events
│   └── utils.cpp            # uuid, path helpers (resolve_under), ...
├── include/                 # C++ headers
│   ├── api_schema.hpp       # Schema field types, SchemaBuilder fluent API
│   ├── api_registry.hpp     # ApiRegistry (route + schema registration)
│   └── api_schemas/         # Schema structs: common, generation, models,
│                            # queue, health, settings, assistant
├── data/                    # Runtime data shipped with the server
│   ├── model_architectures.json # Presets + download catalog per architecture
│   ├── load_options.json        # Load option labels/descriptions for the WebUI
│   ├── generation_options.json  # Generation option labels/descriptions
│   └── bootstrap_models.json    # Docker/RunPod first-start model list
├── webui/src/               # Vue.js frontend
│   ├── views/               # Pages (Generate, Queue, ModelLoad, Downloads, ...)
│   ├── components/          # Reusable components
│   ├── composables/         # useArchitectures.ts (preset matching, component scoring)
│   ├── stores/app.ts        # Pinia store (WS event handling, notifications)
│   ├── services/            # websocket.ts, notifications.ts, sounds
│   └── api/client.ts        # API client + TypeScript types
├── docs/                    # Served at /docs (API.md, API_REFERENCE.md, LLM_GUIDE.md, MCP.md, ...)
├── scripts/                 # install.sh, uninstall.sh, check_download_links.py,
│                            # generate-api-docs.py, bootstrap-models.sh, generate-self-cert.sh
├── Dockerfile, entrypoint.sh, runpod-config.json  # Container / RunPod setup
└── CMakeLists.txt           # Build; pins the sd.cpp commit
```

`docs/superpowers/` holds local design notes; it is gitignored and hidden from `/docs` (`kDocsHiddenDirs` in `request_handlers.cpp`).

## Build Commands

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSD_CUDA=ON
cmake --build build -j$(nproc)

# Faster local CUDA build (only the local GPU arch, non-portable binary)
cmake -S . -B build -DSD_CUDA=ON -DSD_CUDA_ARCH_NATIVE=ON

# WebUI on its own (CMake also builds it when SDCPP_WEBUI=ON)
cd webui && npm install && npm run build   # runs vue-tsc type check
```

Other options: `SD_VULKAN`, `SD_METAL`, `SD_HIPBLAS`, `SD_OPENCL`, `SDCPP_WEBUI`, `SDCPP_WEBSOCKET`, `SDCPP_ASSISTANT`, `SDCPP_MCP` (all ON except the backends).

There is no fork build: the old experimental VRAM-offload variant (`SD_EXPERIMENTAL_OFFLOAD`) was removed because upstream covers it with `max_vram`, segmented compute and prefetch. Its load option names (`offload_mode`, `streaming_*`, ...) stay in `KNOWN_OPTIONS` as accepted-and-ignored so old saved configs still load.

## stable-diffusion.cpp dependency

sd.cpp is fetched with FetchContent and pinned by a **full 40-char commit SHA** (`GIT_TAG` in the stable-diffusion FetchContent block of CMakeLists.txt). Upgrade procedure:

1. List new commits and diff `include/stable-diffusion.h`, `examples/common/common.cpp` (CLI options) and `docs/` between the old and new SHA.
2. Wire new `sd_ctx_params_t` fields as load options (see below) and new generation fields in `sd_wrapper.cpp`. Features that only exist in sd-cli (e.g. sigma presets) must be implemented here.
3. Add presets / downloads for new model architectures (match on the exact name sd.cpp reports, from `model_version_to_str` / the name table in `src/pipeline/diffusion_engine.cpp`).
4. Update `docs/API.md`, `docs/LLM_GUIDE.md`, the WebUI types and controls.

## Key Patterns

### OpenAPI Schema System (Auto-Generated)

`GET /openapi.json` is built from the code, so it is always current:

1. **Schema structs** in `include/api_schemas/*.hpp` using the `SchemaBuilder` fluent API
2. **ApiRegistry** (`include/api_registry.hpp`) registers routes and schemas together
3. **`api.addEndpoint<ReqType, ResType>(server, "POST", "/path", ...)`** in `register_routes()` instead of direct `server.Get/Post`

Schema features: inheritance via `allOf` (`Txt2ImgRequest` inherits `GenerationRequestBase`), `x-architecture-default: true` for fields whose defaults come from the presets, `#ifdef` gates, shared `$ref` schemas (need `api.registerSchema()`), `.query()` / `.path_param()` on the endpoint builder.

```cpp
api.addEndpoint<Txt2ImgRequest, JobCreatedResponse>(
    server, "POST", "/txt2img",
    "Generate image from text prompt", "Generation", 202,
    [this](auto& req, auto& res) { handle_txt2img(req, res); });
```

`docs/API_REFERENCE.md` is generated from `/openapi.json` by `scripts/generate-api-docs.py`; do not edit it by hand.

### Strict request parsing

Generation bodies (`/txt2img`, `/img2img`, `/txt2vid`, `/upscale`) and model load options are parsed against closed allowlists (`KNOWN` sets + `reject_unknown_keys()` in `sd_wrapper.cpp`, `KNOWN_OPTIONS` in `ModelLoadParams::from_json()`). An unknown key is a 400 `Unknown field(s) in ...`. Consequences:
- A new parameter must be added to the allowlist of every endpoint that accepts it.
- The WebUI must not send keys an endpoint does not accept. `Generate.vue` builds a shared `baseParams` for all modes, so image-only keys are stripped before `api.txt2vid()`.
- Server-written metadata that can round-trip back (e.g. `ref_images_count`) must be accepted-and-ignored.

### Adding a New Model Load Option

1. Field in `ModelLoadParams` (`include/model_manager.hpp`)
2. Add to `KNOWN_OPTIONS` and parse in `ModelLoadParams::from_json()` (`src/model_manager.cpp`); validate enums/syntax there so bad values are a clear 400, not a failed load
3. Apply to `sd_ctx_params_t` in `load_model()`
4. Echo into `loaded_options_` (reported by `/health`, used to restore the edit form)
5. `LoadOptions::schema()` in `include/api_schemas/models.hpp`
6. Description in `data/load_options.json` (and its category list)
7. MCP `model` tool property in `handle_list_tools()` (MCP routes options generically)
8. TypeScript type in `webui/src/api/client.ts`, control in `webui/src/views/ModelLoad.vue`, label in `Dashboard.vue` `formatOptionLabel()`

**Adding a new component slot** (a model file like `taesd`, not an option): add it to the loaded-components tracking in `ModelManager::load_model()` so `/health` reports it in `loaded_components`, and to the component restore in `webui/src/views/ModelLoad.vue`, otherwise editing the loaded model drops it.

### Architecture presets and download catalog

`data/model_architectures.json` (auto-reloaded on change) holds one preset per architecture key:
- `requiredComponents` / `optionalComponents` (drive the ModelLoad pickers), `loadOptions`, `generationDefaults` (the Generate page's recommended settings; also `custom_sigmas`), `imageEditMode`, `componentScoring` (regex scores for auto-suggest)
- `aliases`: include the **exact name sd.cpp reports** (e.g. `"Flux.3 Image Klein"`); the server's `ArchitectureManager::get()` tries key, then aliases, then the longest substring match
- `match: {architecture, nameRegex}`: lets several presets share one reported name (variants such as Turbo/Base); the WebUI matcher in `useArchitectures.ts` prefers a `nameRegex` hit over the plain key
- `downloads[]`: the Downloads page catalog (`source` huggingface / url / civitai, `repo_id`, `filename`, `subfolder`, `bundle: "directory"`, `target_type`, `recommended`, `notes`)

After changing downloads run `scripts/check_download_links.py` (`--anonymous` to see what users without an HF token get; gated links report 401/403).
Also run `scripts/check_catalog_duplicates.py`: it flags a file listed under two destinations (same SHA-256) and the same file name from two sources (e.g. `Qwen3VL-8B` vs `Qwen3-VL-8B`), which would land twice on disk.

### Static file serving

`/output`, `/thumb`, `/ui` and `/docs` must resolve request paths with `utils::resolve_under(base, rel)` (rejects absolute paths, `..` and symlinks leaving the base). cpp-httplib already URL-decodes `req.path`; never decode the regex capture a second time. `/ui` and `/docs` are unauthenticated, `/output` and `/thumb` too when `auth.allow_public_outputs` is true.

### WebSocket Events

Broadcast from `src/websocket_server.cpp`:
- Jobs: `job_added`, `job_status_changed` (carries `type` and `title`), `job_progress` (incl. `bytes_done` / `bytes_total` for downloads), `job_preview`, `job_cancelled`, `job_deleted`, `job_restored`
- Models: `model_loading_progress`, `model_loaded`, `model_load_failed`, `model_unloaded`, `auto_unloaded`, `upscaler_loaded`, `upscaler_unloaded`
- Server: `server_status`, `server_shutdown`, `memory_status`

### MCP Server (Model Context Protocol)

`POST /mcp`, JSON-RPC 2.0 over Streamable HTTP. Compile-time optional via `SDCPP_MCP` (ON by default, `#ifdef SDCPP_MCP_ENABLED`). See `docs/MCP.md`.

**Tools:**
- `generate(type, prompt, ...)`: `txt2img`, `img2img`, `video`, `upscale`
- `model(action, ...)`: `load`, `unload`, `list`
- `job(action, ...)`: `status`, `cancel`, `delete` (soft-delete to the recycle bin), `search` (filtered, max 10 per page)
- `image(job_id, [index], [size])`: returns finished outputs as inline base64 JPEG for vision models. Opt-in via `config.mcp.image_tool_enabled`; downscaled to `size` (default `mcp.image_default_max_dim` 768, max `mcp.image_max_dim` 2048), JPEG quality `mcp.image_jpeg_quality`, max 4 images, non-decodable outputs (mp4, webp) skipped. `/health` `features.mcp_image_tool` reports it.

**Resources (7):** `sdcpp://health`, `sdcpp://memory`, `sdcpp://models`, `sdcpp://models/loaded`, `sdcpp://queue` (last 10), `sdcpp://queue/{job_id}`, `sdcpp://architectures`. All queue responses are capped at 10 items.

Adding a tool action: add it to the tool's `enum` in `handle_list_tools()`, handle it in `tool_generate` / `tool_model` / `tool_job`, extend the `inputSchema`. Adding a tool: declaration in `include/mcp_server.hpp`, definition in `handle_list_tools()`, implementation, dispatch in `handle_call_tool()`. Adding a resource: declaration, entry in `handle_list_resources()`, implementation, URI case in `handle_read_resource()`.

### Queue

Jobs are soft-deleted (`status=deleted`, `deleted_at`) and auto-purged after the configured retention. Output folders follow the runtime-configurable `output_path_template` (placeholders like `{date}`, `{job_id}`, `{type}`, `{model}`), persisted to config.json. List responses strip base64 payloads to stay small. See `src/queue_manager.cpp`.

## Testing

```bash
./build/bin/sdcpp-restapi --config config.json

curl http://localhost:8080/health
curl http://localhost:8080/openapi.json | jq '.paths | keys | length'
curl http://localhost:8080/openapi.json | jq '.components.schemas | keys'

# MCP
curl -s -X POST http://localhost:8080/mcp -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}}' | jq .

# Docs
curl http://localhost:8080/docs/
curl http://localhost:8080/docs/API.md

# Download catalog links
scripts/check_download_links.py --anonymous
```

The server searches for `data/model_architectures.json` next to the binary, in `../data` and in the working directory; a test instance started elsewhere has no presets.

## Common Issues

- **`Unknown field(s) in /txt2vid body: ...`**: a client sent a key that endpoint does not accept (see Strict request parsing).
- **WebUI shows "Loading application..."**: a mutex held too long is blocking `/health`.
- **ESRGAN / MoSR models not showing**: only `.pth` files that are ZIP archives are listed (magic bytes check).
- **Upscaler progress not showing**: the progress callback must use report-all mode (`expected_steps=0`).
- **Architecture not detected / wrong preset**: check the name sd.cpp reports (`/health` `model_architecture`) against the preset key, `aliases` and `match` in `data/model_architectures.json`.
