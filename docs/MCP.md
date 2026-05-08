# MCP Server

sdcpp-restapi ships an optional **Model Context Protocol** server so any MCP-compatible client (Claude Desktop, mcp-cli, custom agents) can drive image/video generation, model management, and queue inspection.

The MCP server is a thin JSON-RPC 2.0 adapter over the existing `ModelManager` and `QueueManager` — it exposes the same capabilities as the REST API through a single endpoint.

---

## Table of Contents

1. [Enabling / Disabling](#enabling--disabling)
2. [Runtime Detection](#runtime-detection)
3. [Endpoint & Transport](#endpoint--transport)
4. [Initialize Handshake](#initialize-handshake)
5. [Tools](#tools)
   - [`generate`](#tool-generate)
   - [`model`](#tool-model)
   - [`job`](#tool-job)
6. [Resources](#resources)
7. [Client Configuration](#client-configuration)
8. [Behavioral Notes](#behavioral-notes)
9. [Error Codes](#error-codes)

---

## Enabling / Disabling

MCP support is compile-time optional and **enabled by default**.

```bash
# Enabled (default)
cmake ..

# Explicitly disable
cmake -DSDCPP_MCP=OFF ..
```

When disabled, `src/mcp_server.cpp` is excluded from the build and `POST /mcp` is not registered.

## Runtime Detection

The `GET /health` endpoint reports whether MCP support is compiled in:

```bash
curl -s http://localhost:8080/health | jq '.features.mcp'
# true  → POST /mcp is available
# false → server was built with -DSDCPP_MCP=OFF
```

## Endpoint & Transport

- **Endpoint:** `POST /mcp`
- **Transport:** Streamable HTTP (single endpoint, request/response per call)
- **Protocol:** JSON-RPC 2.0
- **Protocol version reported by server:** `2025-06-18`
- **Content-Type:** `application/json`

### Limitations

| Feature | Supported |
|---|---|
| Single JSON-RPC request | Yes |
| Batch requests (array body) | **No** — returns 400 |
| Server-Sent Events (SSE) | **No** |
| Notifications (no `id` field) | Yes — server returns `204 No Content` |

### Supported JSON-RPC methods

| Method | Purpose |
|---|---|
| `initialize` | Handshake; returns protocol version and capabilities |
| `ping` | Returns `{}` |
| `tools/list` | Returns the 3 tool definitions |
| `tools/call` | Invokes a tool by name with arguments |
| `resources/list` | Returns 6 static resources + 1 resource template |
| `resources/read` | Reads a resource by URI |

Any other method returns JSON-RPC error `-32601 Method not found`.

## Initialize Handshake

```bash
curl -s -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "id": 1,
    "method": "initialize",
    "params": {
      "protocolVersion": "2025-06-18",
      "capabilities": {},
      "clientInfo": {"name": "my-client", "version": "1.0"}
    }
  }'
```

Response:

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "protocolVersion": "2025-06-18",
    "capabilities": {"tools": {}, "resources": {}},
    "serverInfo": {"name": "sdcpp-restapi", "version": "0.2.0"}
  }
}
```

## Tools

sdcpp-restapi exposes **3 consolidated tools**. Each uses a discriminator field (`type` or `action`) to select the sub-operation — this keeps the tool catalog small and easy for an LLM client to reason about.

### Tool: `generate`

Unified generation tool. Dispatches to txt2img, img2img, txt2vid, or upscale based on `type`.

**Required parameters:** `type`, `prompt` (except when `type="upscale"`, which requires `init_image_base64` instead of `prompt`).

| `type` | Requires loaded model | Extra required params |
|---|---|---|
| `txt2img` | Yes | — |
| `img2img` | Yes | `init_image_base64` |
| `video` | Yes | — |
| `upscale` | **No** (needs an ESRGAN upscaler loaded instead) | `init_image_base64` |

**Common optional parameters:** `negative_prompt`, `width`, `height`, `steps`, `cfg_scale`, `seed`, `batch_count` (txt2img only), `sampler`, `scheduler`, `strength` (img2img only).

**Example — queue a txt2img job:**

```json
{
  "jsonrpc": "2.0", "id": 2, "method": "tools/call",
  "params": {
    "name": "generate",
    "arguments": {
      "type": "txt2img",
      "prompt": "a cat in a spacesuit",
      "steps": 20,
      "cfg_scale": 7.0,
      "seed": 42
    }
  }
}
```

The tool result's `text` field contains a JSON object: `{"job_id": "...", "status": "pending", "message": "..."}`. Use `job(action:"status")` to poll.

### Tool: `model`

Manage the currently loaded diffusion model.

| `action` | Required params | Behavior |
|---|---|---|
| `load` | `model_name` | See full load-parameter reference below |
| `unload` | — | Unloads current model |
| `list` | — | Optional filter: `model_type` (checkpoint, diffusion, vae, lora, clip, t5, embedding, controlnet, llm, esrgan, taesd) |

**Full `load` parameter set** (all optional except `model_name`):

*Components:*
`model_type`, `vae`, `clip_l`, `clip_g`, `clip_vision`, `t5xxl`, `controlnet`, `llm`, `llm_vision`, `taesd`, `high_noise_diffusion_model`, `photo_maker`.

*Common load options:*
`flash_attn`, `n_threads`, `keep_clip_on_cpu`, `keep_vae_on_cpu`, `keep_controlnet_on_cpu`, `offload_to_cpu`, `enable_mmap`, `vae_decode_only`, `vae_tiling`, `vae_tile_size_x`, `vae_tile_size_y`, `vae_tile_overlap`, `weight_type`, `tensor_type_rules`, `flow_shift`, `rng_type`, `prediction`, `lora_apply_mode`, `free_params_immediately`.

*Experimental VRAM offloading* (only when server is built with `SD_EXPERIMENTAL_OFFLOAD=ON`; check `features.experimental_offload`):
`offload_mode` (enum: `none` / `cond_only` / `cond_diffusion` / `aggressive` / `layer_streaming`), `vram_estimation` (`dryrun` / `formula`), `offload_cond_stage`, `offload_diffusion`, `reload_cond_stage`, `reload_diffusion`, `target_free_vram_mb`, `min_offload_size_mb`, `streaming_prefetch_layers`, `streaming_keep_layers_behind`, `streaming_min_free_vram_mb`.

> Note: setting `offload_mode = "layer_streaming"` is enough to enable per-layer streaming inside sd.cpp — the `streaming_*` fields above only tune behavior, they don't gate it.

> **Schema note:** The tool's `inputSchema` is **flat** — pass all fields at the top level of `arguments`. The MCP handler partitions them correctly for the underlying parser. Any field not explicitly listed above that matches a `ModelLoadParams` key (see `include/model_manager.hpp`) is passed through as well.

**Example — load a large model with VRAM offloading:**

```json
{
  "jsonrpc": "2.0", "id": 3, "method": "tools/call",
  "params": {
    "name": "model",
    "arguments": {
      "action": "load",
      "model_name": "z_image_turbo_bf16.safetensors",
      "model_type": "diffusion",
      "vae": "ae.safetensors",
      "llm": "Qwen3-4b-Z-Engineer-V2.gguf",
      "flash_attn": true,
      "offload_mode": "layer_streaming",
      "streaming_prefetch_layers": 1,
      "keep_clip_on_cpu": true,
      "vae_tiling": true
    }
  }
}
```

**Example — standard Flux load:**

```json
{
  "jsonrpc": "2.0", "id": 3, "method": "tools/call",
  "params": {
    "name": "model",
    "arguments": {
      "action": "load",
      "model_name": "flux1-dev.safetensors",
      "vae": "ae.safetensors",
      "clip_l": "clip_l.safetensors",
      "t5xxl": "t5xxl_fp16.safetensors",
      "flash_attn": true
    }
  }
}
```

### Tool: `job`

Manage generation jobs: inspect status, cancel, soft-delete, or search the queue.

| `action` | Required params | Returns |
|---|---|---|
| `status` | `job_id` | Full job record (with output URLs rewritten to absolute) |
| `cancel` | `job_id` | `{success, message, job_id}` |
| `delete` | `job_id` | Moves job to recycle bin (`{success, message, job_id}`) |
| `search` | — | Paginated queue listing |

**`search` filters** (all optional):

| Field | Description |
|---|---|
| `search` | Case-insensitive substring match against prompt |
| `status_filter` | `pending` / `processing` / `completed` / `failed` / `cancelled` |
| `type_filter` | `txt2img` / `img2img` / `txt2vid` / `upscale` |
| `architecture` | Partial match on architecture name |
| `model` | Partial match on model name |
| `before` | Unix timestamp — only jobs created before |
| `after` | Unix timestamp — only jobs created after |
| `limit` | 1–10 (capped at 10, default 10) |
| `offset` | Pagination offset |

**Example — find recent failed Flux jobs:**

```json
{
  "jsonrpc": "2.0", "id": 4, "method": "tools/call",
  "params": {
    "name": "job",
    "arguments": {
      "action": "search",
      "status_filter": "failed",
      "architecture": "flux",
      "limit": 10
    }
  }
}
```

Response payload:

```json
{
  "items": [ ... ],
  "total_count": 128,
  "returned_count": 10,
  "offset": 0,
  "limit": 10,
  "has_more": true
}
```

## Resources

Resources are idempotent reads. Clients call `resources/list` to discover them, then `resources/read` with a `uri` param.

| URI | Description |
|---|---|
| `sdcpp://health` | Server health: loaded model, memory, feature flags |
| `sdcpp://memory` | System RAM and GPU VRAM usage |
| `sdcpp://models` | All available models on disk |
| `sdcpp://models/loaded` | Currently loaded model and its components |
| `sdcpp://queue` | Last 10 queue items (see queue cap below) |
| `sdcpp://architectures` | Supported model architectures and their default settings |
| `sdcpp://queue/{job_id}` | Detailed info for one job (resource template) |

**Example — read the queue resource:**

```bash
curl -s -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0", "id": 5, "method": "resources/read",
    "params": {"uri": "sdcpp://queue"}
  }'
```

## Client Configuration

### Generic HTTP MCP client

Point any Streamable-HTTP MCP client at:

```
http://<host>:<port>/mcp
```

No auth, no custom headers required (beyond `Content-Type: application/json`).

### Claude Desktop

Claude Desktop natively speaks MCP. Add an entry to `claude_desktop_config.json`:

```json
{
  "mcpServers": {
    "sdcpp": {
      "url": "http://localhost:8080/mcp"
    }
  }
}
```

### Command-line probe

Quick smoke test with `curl` + `jq`:

```bash
# List tools
curl -s -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/list"}' | jq '.result.tools[].name'
# "generate"
# "model"
# "job"

# List resources
curl -s -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":2,"method":"resources/list"}' | jq '.result.resources[].uri'
```

## Behavioral Notes

### Queue response cap

All MCP queue responses are hard-capped at **10 items maximum**. This applies to:

- The `sdcpp://queue` resource (returns the last 10)
- The `job(action:"search")` tool — `limit` is clamped to `[1, 10]`

This keeps responses small enough to fit comfortably in an LLM context window. For unbounded queue access, use the REST `/queue` endpoint.

### Output URL rewriting

Any `outputs` array in job records is rewritten to absolute URLs using the `Host` header from the incoming request. This means a client connecting to `http://my-box:8080/mcp` sees output URLs like `http://my-box:8080/output/<file>`, even when the server is bound to `0.0.0.0`.

### Notifications

JSON-RPC messages without an `id` field are treated as notifications: the server processes them and returns `204 No Content` with no body.

### Tool result shape

Every tool returns the standard MCP content envelope:

```json
{
  "content": [{"type": "text", "text": "<payload>"}],
  "isError": false
}
```

When the tool succeeds, `text` is the JSON payload (as a string). When it fails, `isError` is `true` and `text` is the human-readable error message. The JSON-RPC envelope itself does **not** carry the error — you must inspect `result.isError`.

## Error Codes

Standard JSON-RPC 2.0 error codes used by the server:

| Code | Meaning |
|---|---|
| `-32700` | Parse error — request body was not valid JSON |
| `-32600` | Invalid request — missing `jsonrpc: "2.0"`, missing `method`, or batch array |
| `-32601` | Method not found |
| `-32603` | Internal error — an exception escaped a handler |

Tool-level failures (e.g., "model not loaded", "job not found") are **not** JSON-RPC errors — they come back inside the successful envelope with `result.isError: true`.
