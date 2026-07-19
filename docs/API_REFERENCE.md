# sdcpp-restapi - API Reference

> **Auto-generated** from `/openapi.json` by `scripts/generate-api-docs.py`. Do not edit by hand.
> Version: `0.5.0`

This document is the canonical field-level reference. Hand-written guides
(`API.md`, `LLM_GUIDE.md`, `MCP.md`) explain concepts and workflows and
link here for the current field list.

## Endpoints

### Architectures

- [`GET /architectures`](#op-get-/architectures) - Get model architecture presets
- [`GET /architectures/detect`](#op-get-/architectures/detect) - Detect architecture of a model

### Assistant

- [`POST /assistant/chat`](#op-post-/assistant/chat) - Chat with assistant LLM
- [`POST /assistant/chat/stream`](#op-post-/assistant/chat/stream) - Chat with streaming (SSE)
- [`DELETE /assistant/history`](#op-delete-/assistant/history) - Clear chat history
- [`GET /assistant/history`](#op-get-/assistant/history) - Get chat history
- [`GET /assistant/model-info`](#op-get-/assistant/model-info) - Get loaded assistant LLM info
- [`GET /assistant/settings`](#op-get-/assistant/settings) - Get assistant settings
- [`PUT /assistant/settings`](#op-put-/assistant/settings) - Update assistant settings
- [`GET /assistant/status`](#op-get-/assistant/status) - Get assistant status

### Auth

- [`POST /auth/login`](#op-post-/auth/login) - Issue a bearer token for the configured credentials
- [`POST /auth/logout`](#op-post-/auth/logout) - Revoke the bearer token used in this request

### Downloads

- [`GET /models/civitai/{id}`](#op-get-/models/civitai/{id}) - Get CivitAI model metadata
- [`POST /models/download`](#op-post-/models/download) - Download model from URL, CivitAI, or HuggingFace
- [`GET /models/huggingface`](#op-get-/models/huggingface) - Get HuggingFace model file metadata

### Files

- [`GET /output`](#op-get-/output) - Browse output directory (root)
- [`GET /output/{path}`](#op-get-/output/{path}) - Browse or download output file
- [`GET /thumb/{path}`](#op-get-/thumb/{path}) - Get thumbnail for image/video file

### Generation

- [`POST /convert`](#op-post-/convert) - Convert model format (safetensors to GGUF)
- [`POST /img2img`](#op-post-/img2img) - Generate image from source image and text
- [`POST /txt2img`](#op-post-/txt2img) - Generate image from text prompt
- [`POST /txt2vid`](#op-post-/txt2vid) - Generate video from text prompt
- [`POST /upscale`](#op-post-/upscale) - Upscale image using ESRGAN

### Models

- [`GET /models`](#op-get-/models) - List all available models
- [`GET /models/hash/{model_type}/{model_name}`](#op-get-/models/hash/{model_type}/{model_name}) - Compute model SHA256 hash
- [`POST /models/load`](#op-post-/models/load) - Load a model into memory
- [`GET /models/paths`](#op-get-/models/paths) - Get configured model storage paths
- [`POST /models/refresh`](#op-post-/models/refresh) - Rescan model directories
- [`POST /models/unload`](#op-post-/models/unload) - Unload the current model
- [`POST /models/upload`](#op-post-/models/upload) - Upload a model file (multipart/form-data: file, model_type, [filename], [subfolder])

### Queue

- [`GET /jobs/{job_id}/preview`](#op-get-/jobs/{job_id}/preview) - Get live preview JPEG for a processing job
- [`GET /queue`](#op-get-/queue) - List jobs with filtering and pagination
- [`DELETE /queue/jobs`](#op-delete-/queue/jobs) - Batch delete jobs (soft-delete to recycle bin)
- [`DELETE /queue/{job_id}`](#op-delete-/queue/{job_id}) - Cancel a pending job
- [`GET /queue/{job_id}`](#op-get-/queue/{job_id}) - Get job status

### Recycle Bin

- [`DELETE /queue/recycle-bin`](#op-delete-/queue/recycle-bin) - Clear all items from recycle bin
- [`GET /queue/recycle-bin`](#op-get-/queue/recycle-bin) - List deleted jobs in recycle bin
- [`DELETE /queue/{job_id}/purge`](#op-delete-/queue/{job_id}/purge) - Permanently delete a job
- [`POST /queue/{job_id}/restore`](#op-post-/queue/{job_id}/restore) - Restore a job from recycle bin
- [`GET /settings/recycle-bin`](#op-get-/settings/recycle-bin) - Get recycle bin settings

### Settings

- [`GET /preview/settings`](#op-get-/preview/settings) - Get preview generation settings
- [`PUT /preview/settings`](#op-put-/preview/settings) - Update preview settings
- [`GET /settings/generation`](#op-get-/settings/generation) - Get all generation defaults
- [`PUT /settings/generation`](#op-put-/settings/generation) - Update all generation defaults
- [`GET /settings/generation/{mode}`](#op-get-/settings/generation/{mode}) - Get generation defaults for a mode
- [`PUT /settings/generation/{mode}`](#op-put-/settings/generation/{mode}) - Update generation defaults for a mode
- [`GET /settings/preferences`](#op-get-/settings/preferences) - Get UI preferences
- [`PUT /settings/preferences`](#op-put-/settings/preferences) - Update UI preferences
- [`POST /settings/reset`](#op-post-/settings/reset) - Reset all settings to defaults

### Status

- [`GET /health`](#op-get-/health) - Server health check
- [`GET /memory`](#op-get-/memory) - System and GPU memory status
- [`GET /options`](#op-get-/options) - Available samplers, schedulers, quantization types
- [`GET /options/descriptions`](#op-get-/options/descriptions) - Descriptions of all available model-load options (sourced from data/load_options.json)

### Upscaler

- [`POST /upscaler/load`](#op-post-/upscaler/load) - Load an ESRGAN upscaler model
- [`POST /upscaler/unload`](#op-post-/upscaler/unload) - Unload the current upscaler

## Endpoint details

#### Architectures

### `GET /architectures` <a id="op-get-/architectures"></a>

**Get model architecture presets**

Tags: `Architectures`

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation | `application/json` -> [ArchitecturesResponse](#schema-architecturesresponse) |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `GET /architectures/detect` <a id="op-get-/architectures/detect"></a>

**Detect architecture of a model**

Tags: `Architectures`

**Parameters**

| in | name | type | required | description |
|---|---|---|---|---|
| query | `model` | string | yes | Model name to detect |

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation | `application/json` -> [DetectArchitectureResponse](#schema-detectarchitectureresponse) |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

#### Assistant

### `POST /assistant/chat` <a id="op-post-/assistant/chat"></a>

**Chat with assistant LLM**

Tags: `Assistant`

**Request body** (`application/json`): [AssistantChatRequest](#schema-assistantchatrequest)

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation |  |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `POST /assistant/chat/stream` <a id="op-post-/assistant/chat/stream"></a>

**Chat with streaming (SSE)**

Tags: `Assistant`

**Request body** (`application/json`): [AssistantChatRequest](#schema-assistantchatrequest)

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation |  |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `DELETE /assistant/history` <a id="op-delete-/assistant/history"></a>

**Clear chat history**

Tags: `Assistant`

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation | `application/json` -> [SuccessResponse](#schema-successresponse) |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `GET /assistant/history` <a id="op-get-/assistant/history"></a>

**Get chat history**

Tags: `Assistant`

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation |  |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `GET /assistant/model-info` <a id="op-get-/assistant/model-info"></a>

**Get loaded assistant LLM info**

Tags: `Assistant`

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation |  |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `GET /assistant/settings` <a id="op-get-/assistant/settings"></a>

**Get assistant settings**

Tags: `Assistant`

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation |  |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `PUT /assistant/settings` <a id="op-put-/assistant/settings"></a>

**Update assistant settings**

Tags: `Assistant`

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation | `application/json` -> [SuccessResponse](#schema-successresponse) |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `GET /assistant/status` <a id="op-get-/assistant/status"></a>

**Get assistant status**

Tags: `Assistant`

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation | `application/json` -> [AssistantStatusResponse](#schema-assistantstatusresponse) |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

#### Auth

### `POST /auth/login` <a id="op-post-/auth/login"></a>

**Issue a bearer token for the configured credentials**

Tags: `Auth`

**Request body** (`application/json`): [LoginRequest](#schema-loginrequest)

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation | `application/json` -> [LoginResponse](#schema-loginresponse) |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `POST /auth/logout` <a id="op-post-/auth/logout"></a>

**Revoke the bearer token used in this request**

Tags: `Auth`

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation | `application/json` -> [LogoutResponse](#schema-logoutresponse) |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

#### Downloads

### `GET /models/civitai/{id}` <a id="op-get-/models/civitai/{id}"></a>

**Get CivitAI model metadata**

Tags: `Downloads`

**Parameters**

| in | name | type | required | description |
|---|---|---|---|---|
| path | `id` | string | yes | CivitAI model ID (format: id or id:version) |

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation |  |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `POST /models/download` <a id="op-post-/models/download"></a>

**Download model from URL, CivitAI, or HuggingFace**

Tags: `Downloads`

**Request body** (`application/json`): [DownloadModelRequest](#schema-downloadmodelrequest)

**Responses**

| status | description | body |
|---|---|---|
| `202` | Accepted (job queued) | `application/json` -> [DownloadModelResponse](#schema-downloadmodelresponse) |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `GET /models/huggingface` <a id="op-get-/models/huggingface"></a>

**Get HuggingFace model file metadata**

Tags: `Downloads`

**Parameters**

| in | name | type | required | description |
|---|---|---|---|---|
| query | `repo_id` | string | yes | HuggingFace repository ID |
| query | `filename` | string | yes | File name in repository |
| query | `revision` | string |  | Git revision |

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation | `application/json` -> [HuggingfaceInfoResponse](#schema-huggingfaceinforesponse) |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

#### Files

### `GET /output` <a id="op-get-/output"></a>

**Browse output directory (root)**

Tags: `Files`

**Parameters**

| in | name | type | required | description |
|---|---|---|---|---|
| query | `sort` | string |  | Sort field (name, size, mtime) |
| query | `order` | enum (`asc`, `desc`) |  | Sort order |

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation |  |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `GET /output/{path}` <a id="op-get-/output/{path}"></a>

**Browse or download output file**

Tags: `Files`

**Parameters**

| in | name | type | required | description |
|---|---|---|---|---|
| path | `path` | string | yes | File or directory path |

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation |  |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `GET /thumb/{path}` <a id="op-get-/thumb/{path}"></a>

**Get thumbnail for image/video file**

Tags: `Files`

**Parameters**

| in | name | type | required | description |
|---|---|---|---|---|
| path | `path` | string | yes | File path relative to output directory |

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation |  |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

#### Generation

### `POST /convert` <a id="op-post-/convert"></a>

**Convert model format (safetensors to GGUF)**

Tags: `Generation`

**Request body** (`application/json`): [ConvertRequest](#schema-convertrequest)

**Responses**

| status | description | body |
|---|---|---|
| `202` | Accepted (job queued) | `application/json` -> [JobCreatedResponse](#schema-jobcreatedresponse) |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `POST /img2img` <a id="op-post-/img2img"></a>

**Generate image from source image and text**

Tags: `Generation`

**Request body** (`application/json`): [Img2ImgRequest](#schema-img2imgrequest)

**Responses**

| status | description | body |
|---|---|---|
| `202` | Accepted (job queued) | `application/json` -> [JobCreatedResponse](#schema-jobcreatedresponse) |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `POST /txt2img` <a id="op-post-/txt2img"></a>

**Generate image from text prompt**

Tags: `Generation`

**Request body** (`application/json`): [Txt2ImgRequest](#schema-txt2imgrequest)

**Responses**

| status | description | body |
|---|---|---|
| `202` | Accepted (job queued) | `application/json` -> [JobCreatedResponse](#schema-jobcreatedresponse) |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `POST /txt2vid` <a id="op-post-/txt2vid"></a>

**Generate video from text prompt**

Tags: `Generation`

**Request body** (`application/json`): [Txt2VidRequest](#schema-txt2vidrequest)

**Responses**

| status | description | body |
|---|---|---|
| `202` | Accepted (job queued) | `application/json` -> [JobCreatedResponse](#schema-jobcreatedresponse) |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `POST /upscale` <a id="op-post-/upscale"></a>

**Upscale image using ESRGAN**

Tags: `Generation`

**Request body** (`application/json`): [UpscaleRequest](#schema-upscalerequest)

**Responses**

| status | description | body |
|---|---|---|
| `202` | Accepted (job queued) | `application/json` -> [JobCreatedResponse](#schema-jobcreatedresponse) |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

#### Models

### `GET /models` <a id="op-get-/models"></a>

**List all available models**

Tags: `Models`

**Parameters**

| in | name | type | required | description |
|---|---|---|---|---|
| query | `type` | enum (`checkpoint`, `diffusion`, `vae`, `lora`, `clip`, `t5`, `embedding`, `controlnet`, `llm`, `esrgan`, `taesd`) |  | Filter by model type |
| query | `extension` | string |  | Filter by file extension |
| query | `search` | string |  | Search in model name |
| query | `name` | string |  | Search alias for 'search' parameter |

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation | `application/json` -> [ModelListResponse](#schema-modellistresponse) |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `GET /models/hash/{model_type}/{model_name}` <a id="op-get-/models/hash/{model_type}/{model_name}"></a>

**Compute model SHA256 hash**

Tags: `Models`

**Parameters**

| in | name | type | required | description |
|---|---|---|---|---|
| path | `model_type` | string | yes | Model type category |
| path | `model_name` | string | yes | Model filename |

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation |  |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `POST /models/load` <a id="op-post-/models/load"></a>

**Load a model into memory**

Tags: `Models`

**Request body** (`application/json`): [LoadModelRequest](#schema-loadmodelrequest)

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation | `application/json` -> [LoadModelResponse](#schema-loadmodelresponse) |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `GET /models/paths` <a id="op-get-/models/paths"></a>

**Get configured model storage paths**

Tags: `Models`

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation | `application/json` -> [ModelPathsResponse](#schema-modelpathsresponse) |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `POST /models/refresh` <a id="op-post-/models/refresh"></a>

**Rescan model directories**

Tags: `Models`

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation | `application/json` -> [ModelRefreshResponse](#schema-modelrefreshresponse) |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `POST /models/unload` <a id="op-post-/models/unload"></a>

**Unload the current model**

Tags: `Models`

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation | `application/json` -> [SuccessResponse](#schema-successresponse) |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `POST /models/upload` <a id="op-post-/models/upload"></a>

**Upload a model file (multipart/form-data: file, model_type, [filename], [subfolder])**

Tags: `Models`

**Responses**

| status | description | body |
|---|---|---|
| `201` | Created | `application/json` -> [UploadModelResponse](#schema-uploadmodelresponse) |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

#### Queue

### `GET /jobs/{job_id}/preview` <a id="op-get-/jobs/{job_id}/preview"></a>

**Get live preview JPEG for a processing job**

Tags: `Queue`

**Parameters**

| in | name | type | required | description |
|---|---|---|---|---|
| path | `job_id` | string | yes | Job UUID |

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation |  |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `GET /queue` <a id="op-get-/queue"></a>

**List jobs with filtering and pagination**

Tags: `Queue`

**Parameters**

| in | name | type | required | description |
|---|---|---|---|---|
| query | `status` | enum (`pending`, `processing`, `completed`, `failed`, `cancelled`, `deleted`) |  | Filter by job status |
| query | `type` | enum (`txt2img`, `img2img`, `txt2vid`, `upscale`, `convert`, `model_download`, `model_hash`) |  | Filter by job type |
| query | `search` | string |  | Search in prompt/params |
| query | `architecture` | string |  | Filter by model architecture |
| query | `group_by` | string |  | Group results (date) |
| query | `limit` | integer |  | Results per page |
| query | `page` | integer |  | Page number |
| query | `offset` | integer |  | Pagination offset |
| query | `before` | integer |  | Get jobs before timestamp (unix) |
| query | `after` | integer |  | Get jobs after timestamp (unix) |
| query | `sort` | string |  | Sort field |
| query | `order` | enum (`asc`, `desc`) |  | Sort order |

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation | `application/json` -> [QueueListResponse](#schema-queuelistresponse) |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `DELETE /queue/jobs` <a id="op-delete-/queue/jobs"></a>

**Batch delete jobs (soft-delete to recycle bin)**

Tags: `Queue`

**Request body** (`application/json`): [DeleteJobsRequest](#schema-deletejobsrequest)

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation | `application/json` -> [DeleteJobsResponse](#schema-deletejobsresponse) |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `DELETE /queue/{job_id}` <a id="op-delete-/queue/{job_id}"></a>

**Cancel a pending job**

Tags: `Queue`

**Parameters**

| in | name | type | required | description |
|---|---|---|---|---|
| path | `job_id` | string | yes | Job UUID |

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation |  |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `GET /queue/{job_id}` <a id="op-get-/queue/{job_id}"></a>

**Get job status**

Tags: `Queue`

**Parameters**

| in | name | type | required | description |
|---|---|---|---|---|
| path | `job_id` | string | yes | Job UUID |

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation |  |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

#### Recycle Bin

### `DELETE /queue/recycle-bin` <a id="op-delete-/queue/recycle-bin"></a>

**Clear all items from recycle bin**

Tags: `Recycle Bin`

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation | `application/json` -> [SuccessResponse](#schema-successresponse) |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `GET /queue/recycle-bin` <a id="op-get-/queue/recycle-bin"></a>

**List deleted jobs in recycle bin**

Tags: `Recycle Bin`

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation | `application/json` -> [RecycleBinResponse](#schema-recyclebinresponse) |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `DELETE /queue/{job_id}/purge` <a id="op-delete-/queue/{job_id}/purge"></a>

**Permanently delete a job**

Tags: `Recycle Bin`

**Parameters**

| in | name | type | required | description |
|---|---|---|---|---|
| path | `job_id` | string | yes | Job UUID |

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation |  |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `POST /queue/{job_id}/restore` <a id="op-post-/queue/{job_id}/restore"></a>

**Restore a job from recycle bin**

Tags: `Recycle Bin`

**Parameters**

| in | name | type | required | description |
|---|---|---|---|---|
| path | `job_id` | string | yes | Job UUID |

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation |  |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `GET /settings/recycle-bin` <a id="op-get-/settings/recycle-bin"></a>

**Get recycle bin settings**

Tags: `Recycle Bin`

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation | `application/json` -> [RecycleBinSettingsResponse](#schema-recyclebinsettingsresponse) |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

#### Settings

### `GET /preview/settings` <a id="op-get-/preview/settings"></a>

**Get preview generation settings**

Tags: `Settings`

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation | `application/json` -> [PreviewSettingsResponse](#schema-previewsettingsresponse) |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `PUT /preview/settings` <a id="op-put-/preview/settings"></a>

**Update preview settings**

Tags: `Settings`

**Request body** (`application/json`): [UpdatePreviewSettingsRequest](#schema-updatepreviewsettingsrequest)

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation | `application/json` -> [SuccessResponse](#schema-successresponse) |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `GET /settings/generation` <a id="op-get-/settings/generation"></a>

**Get all generation defaults**

Tags: `Settings`

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation |  |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `PUT /settings/generation` <a id="op-put-/settings/generation"></a>

**Update all generation defaults**

Tags: `Settings`

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation | `application/json` -> [SuccessResponse](#schema-successresponse) |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `GET /settings/generation/{mode}` <a id="op-get-/settings/generation/{mode}"></a>

**Get generation defaults for a mode**

Tags: `Settings`

**Parameters**

| in | name | type | required | description |
|---|---|---|---|---|
| path | `mode` | string | yes | Generation mode (txt2img, img2img, txt2vid, upscale) |

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation |  |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `PUT /settings/generation/{mode}` <a id="op-put-/settings/generation/{mode}"></a>

**Update generation defaults for a mode**

Tags: `Settings`

**Parameters**

| in | name | type | required | description |
|---|---|---|---|---|
| path | `mode` | string | yes | Generation mode |

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation |  |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `GET /settings/preferences` <a id="op-get-/settings/preferences"></a>

**Get UI preferences**

Tags: `Settings`

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation |  |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `PUT /settings/preferences` <a id="op-put-/settings/preferences"></a>

**Update UI preferences**

Tags: `Settings`

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation | `application/json` -> [SuccessResponse](#schema-successresponse) |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `POST /settings/reset` <a id="op-post-/settings/reset"></a>

**Reset all settings to defaults**

Tags: `Settings`

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation | `application/json` -> [SuccessResponse](#schema-successresponse) |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

#### Status

### `GET /health` <a id="op-get-/health"></a>

**Server health check**

Tags: `Status`

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation | `application/json` -> [HealthResponse](#schema-healthresponse) |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `GET /memory` <a id="op-get-/memory"></a>

**System and GPU memory status**

Tags: `Status`

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation | `application/json` -> [MemoryResponse](#schema-memoryresponse) |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `GET /options` <a id="op-get-/options"></a>

**Available samplers, schedulers, quantization types**

Tags: `Status`

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation | `application/json` -> [OptionsResponse](#schema-optionsresponse) |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `GET /options/descriptions` <a id="op-get-/options/descriptions"></a>

**Descriptions of all available model-load options (sourced from data/load_options.json)**

Tags: `Status`

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation | `application/json` -> [OptionDescriptionsResponse](#schema-optiondescriptionsresponse) |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

#### Upscaler

### `POST /upscaler/load` <a id="op-post-/upscaler/load"></a>

**Load an ESRGAN upscaler model**

Tags: `Upscaler`

**Request body** (`application/json`): [LoadUpscalerRequest](#schema-loadupscalerrequest)

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation | `application/json` -> [SuccessResponse](#schema-successresponse) |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

### `POST /upscaler/unload` <a id="op-post-/upscaler/unload"></a>

**Unload the current upscaler**

Tags: `Upscaler`

**Responses**

| status | description | body |
|---|---|---|
| `200` | Successful operation | `application/json` -> [SuccessResponse](#schema-successresponse) |
| `400` | Bad request | `application/json` -> [ErrorResponse](#schema-errorresponse) |
| `500` | Internal server error | `application/json` -> [ErrorResponse](#schema-errorresponse) |

## Schemas

### schema `ArchitecturesResponse` <a id="schema-architecturesresponse"></a>

Available model architecture presets

| field | type | required | default | description |
|---|---|---|---|---|
| `architectures` | object | yes |  | Architecture presets keyed by ID |

### schema `AssistantChatRequest` <a id="schema-assistantchatrequest"></a>

Chat with assistant LLM

| field | type | required | default | description |
|---|---|---|---|---|
| `message` | string | yes |  | User message text |

### schema `AssistantStatusResponse` <a id="schema-assistantstatusresponse"></a>

Assistant status and model info

| field | type | required | default | description |
|---|---|---|---|---|
| `available` | boolean | yes |  | Whether assistant is available |
| `model_loaded` | boolean |  |  | Whether LLM is loaded |
| `model_name` | string |  |  | Loaded LLM model name |

### schema `ConvertRequest` <a id="schema-convertrequest"></a>

Model format conversion request

| field | type | required | default | description |
|---|---|---|---|---|
| `input_path` | string | yes |  | Input model path or name |
| `model_type` | enum (`checkpoint`, `diffusion`, `vae`, `lora`, `clip`, `t5`, `embedding`, `controlnet`, `llm`, `esrgan`, `taesd`) |  |  | Model type for name resolution |
| `output_path` | string |  |  | Output path (auto-generated if empty) |
| `output_type` | string | yes |  | Target quantization type |
| `title` | string |  |  | Optional display title for the queue job |

### schema `DeleteJobsRequest` <a id="schema-deletejobsrequest"></a>

Batch delete jobs request

| field | type | required | default | description |
|---|---|---|---|---|
| `job_ids` | array<string> | yes |  | Job UUIDs to delete |

### schema `DeleteJobsResponse` <a id="schema-deletejobsresponse"></a>

Batch delete result

| field | type | required | default | description |
|---|---|---|---|---|
| `deleted` | integer | yes |  | Number of jobs deleted |
| `failed` | integer | yes |  | Number of deletions that failed |
| `failed_job_ids` | array<string> |  |  | IDs of failed deletions |
| `success` | boolean | yes |  | Whether operation completed |
| `total` | integer | yes |  | Total jobs requested |

### schema `DetectArchitectureResponse` <a id="schema-detectarchitectureresponse"></a>

Architecture detection result

| field | type | required | default | description |
|---|---|---|---|---|
| `architecture` | object |  |  | Detected architecture details (null if not detected) |
| `detected` | boolean | yes |  | Whether architecture was detected |

### schema `DownloadModelRequest` <a id="schema-downloadmodelrequest"></a>

Download a model from external source

| field | type | required | default | description |
|---|---|---|---|---|
| `filename` | string |  |  | Target filename |
| `model_id` | string |  |  | CivitAI model ID (format: id or id:version) |
| `model_type` | enum (`checkpoint`, `diffusion`, `vae`, `lora`, `clip`, `t5`, `embedding`, `controlnet`, `llm`, `esrgan`, `taesd`) | yes |  | Target model type category |
| `repo_id` | string |  |  | HuggingFace repository ID |
| `revision` | string |  | main | Git revision for HF repo |
| `source` | enum (`url`, `civitai`, `huggingface`) |  |  | Download source |
| `subfolder` | string |  |  | Subfolder within HF repo |
| `url` | string |  |  | Direct download URL |

### schema `DownloadModelResponse` <a id="schema-downloadmodelresponse"></a>

Download job created

| field | type | required | default | description |
|---|---|---|---|---|
| `download_job_id` | string | yes |  | Download job UUID |
| `hash_job_id` | string |  |  | Hash computation job UUID |
| `model_type` | string | yes |  | Target model type |
| `position` | integer |  |  | Queue position |
| `source` | string | yes |  | Download source used |
| `success` | boolean | yes |  | Whether download was initiated |

### schema `ErrorResponse` <a id="schema-errorresponse"></a>

| field | type | required | default | description |
|---|---|---|---|---|
| `error` | string | yes |  | Error message |

### schema `GenerationRequestBase` <a id="schema-generationrequestbase"></a>

Common generation parameters

| field | type | required | default | description |
|---|---|---|---|---|
| `auto_resize_ref_image` | boolean |  | true | Auto-resize reference images to match output |
| `batch_count` | integer |  | 1 | Number of images to generate |
| `cache_mode` | string |  |  | Cache acceleration mode: easycache, ucache, dbcache, taylorseer, cache_dit, spectrum (empty = disabled) |
| `cfg_scale` | number |  |  | Classifier-free guidance scale (sd_guidance_params_t.txt_cfg) (default from model_architectures.json) |
| `circular_x` | boolean |  | false | Circular RoPE on the X axis — seamless/tileable output across the horizontal seam. Required for Ideogram4-style tileable-texture workflows. |
| `circular_y` | boolean |  | false | Circular RoPE on the Y axis — seamless/tileable output across the vertical seam. |
| `clip_skip` | integer |  | -1 | CLIP layers to skip (-1 for default) |
| `control_image_base64` | string |  |  | ControlNet input image as base64 |
| `control_strength` | number |  | 0.9 | ControlNet guidance strength |
| `custom_sigmas` | array<number> |  |  | Custom sigma schedule values |
| `distilled_guidance` | number |  | 3.5 | Distilled guidance scale (Flux/distilled models) |
| `easycache` | boolean |  | false | Enable EasyCache (deprecated alias for cache_mode='easycache') |
| `easycache_end` | number |  | 0.95 | Cache end percentage (shared across modes) |
| `easycache_start` | number |  | 0.15 | Cache start percentage (shared across modes) |
| `easycache_threshold` | number |  | 0.2 | Reuse threshold for similarity-based caches (EasyCache/UCache/DBCache) |
| `eta` | number |  | 0.0 | Eta for DDIM-like samplers |
| `extra_sample_args` | string |  |  | Pass-through key=value list for sd.cpp's sample arg parser (model-specific knobs) |
| `extra_tiling_args` | string |  |  | Extra key=value tiling args (passed through to sd.cpp's tiling parser, model-specific) |
| `height` | integer |  |  | Image height in pixels (default from model_architectures.json) |
| `hires_denoising_strength` | number |  | 0.4 | Hires denoising strength |
| `hires_enabled` | boolean |  | false | Enable sd.cpp's native two-pass hi-res-fix refine |
| `hires_model_path` | string |  |  | Upscaler model path (when hires_upscaler='model') |
| `hires_scale` | number |  | 2.0 | Hires scale factor |
| `hires_steps` | integer |  | 0 | Hires sampling steps (0 = inherit main steps) |
| `hires_target_height` | integer |  | 0 | Hires target height |
| `hires_target_width` | integer |  | 0 | Hires target width (0 = derive from scale) |
| `hires_upscale_tile_size` | integer |  | 0 | Hires upscaler tile size |
| `hires_upscaler` | string |  | model | Hires upscaler (sd_hires_upscaler_t): none, latent, latent_nearest, latent_nearest_exact, latent_antialiased, latent_bicubic, latent_bicubic_antialiased, lanczos, nearest, model |
| `img_cfg_scale` | number |  | -1.0 | Image CFG (sd_guidance_params_t.img_cfg). -1 = inherit cfg_scale. |
| `increase_ref_index` | boolean |  | false | Increase reference index for multi-ref |
| `negative_prompt` | string |  |  | Negative prompt |
| `pm_id_embed_path` | string |  |  | Path to PhotoMaker ID embedding |
| `pm_id_images` | array<string> |  |  | PhotoMaker identity images as base64 |
| `pm_style_strength` | number |  | 20.0 | PhotoMaker style strength |
| `prompt` | string | yes |  | Text prompt for generation |
| `pulid_id_embedding_path` | string |  |  | PuLID-Flux identity embedding path |
| `pulid_id_weight` | number |  | 1.0 | PuLID-Flux identity weight |
| `qwen_image_layers` | integer |  | 0 | Qwen-Image layered rendering (image path only; 0 = disabled) |
| `ref_images` | array<string> |  |  | Reference images as base64 strings |
| `sampler` | enum (`euler`, `euler_a`, `heun`, `dpm2`, `dpm++2s_a`, `dpm++2m`, `dpm++2mv2`, `ipndm`, `ipndm_v`, `lcm`, `ddim_trailing`, `tcd`, `res_multistep`, `res_2s`, `er_sde`, `euler_cfg_pp`, `euler_a_cfg_pp`, `euler_ge`, `dpm++2m_sde`, `dpm++2m_sde_bt`) |  |  | Sampling algorithm (default from model_architectures.json) |
| `scheduler` | enum (`discrete`, `karras`, `exponential`, `ays`, `gits`, `sgm_uniform`, `simple`, `smoothstep`, `kl_optimal`, `lcm`, `bong_tangent`, `ltx2`, `logit_normal`, `flux`, `flux2`, `beta`, `normal`) |  |  | Noise scheduler (default from model_architectures.json) |
| `seed` | integer |  | -1 | RNG seed (-1 for random) |
| `shifted_timestep` | integer |  | 0 | Shifted timestep value (NitroFusion: 250-500) |
| `skip_layers` | array<integer> |  |  | Layer indices for skip-layer guidance |
| `slg_end` | number |  | 0.2 | SLG end percentage |
| `slg_scale` | number |  | 0.0 | Skip-layer guidance scale |
| `slg_start` | number |  | 0.01 | SLG start percentage |
| `spectrum_flex_window` | number |  | 0.5 | Spectrum cache flex window |
| `spectrum_lam` | number |  | 0.5 | Spectrum cache lambda |
| `spectrum_m` | integer |  | 5 | Spectrum cache M parameter |
| `spectrum_stop_percent` | number |  | 0.8 | Spectrum cache stop percentage |
| `spectrum_w` | number |  | 0.5 | Spectrum cache weight parameter |
| `spectrum_warmup_steps` | integer |  | 2 | Spectrum cache warmup steps |
| `spectrum_window_size` | integer |  | 3 | Spectrum cache window size |
| `steps` | integer |  |  | Number of sampling steps (default from model_architectures.json) |
| `taylorseer_n_derivatives` | integer |  | 2 | TaylorSeer: derivative order (typically 2-4) |
| `taylorseer_skip_interval` | integer |  | 0 | TaylorSeer: predict-every-N-steps interval (0 = adaptive) |
| `temporal_tiling` | boolean |  | false | Enable temporal VAE tiling (LTX video models — splits the time axis into tiles to reduce memory pressure during decode) |
| `title` | string |  |  | Optional display title for the queue job (free-form, shown next to the type label in the WebUI) |
| `upscale` | boolean |  | false | Auto-run a loaded ESRGAN upscaler after generation (post-gen, distinct from hires_enabled) |
| `upscale_auto_unload` | boolean |  | true | Unload upscaler after use |
| `upscale_repeats` | integer |  | 1 | Number of upscale passes |
| `vae_tile_overlap` | number |  | 0.5 | VAE tile overlap ratio |
| `vae_tile_rel_size_x` | number |  | 0.0 | VAE tile width as fraction of image (0 = use absolute tile_size_x) |
| `vae_tile_rel_size_y` | number |  | 0.0 | VAE tile height as fraction of image |
| `vae_tile_size_x` | integer |  | 0 | VAE tile width (0 for auto) |
| `vae_tile_size_y` | integer |  | 0 | VAE tile height (0 for auto) |
| `vae_tiling` | boolean |  | false | Enable VAE tiling for large images |
| `width` | integer |  |  | Image width in pixels (default from model_architectures.json) |

### schema `HealthResponse` <a id="schema-healthresponse"></a>

Server health and status information

| field | type | required | default | description |
|---|---|---|---|---|
| `features` | object |  |  | Enabled feature flags |
| `git_commit` | string |  |  | Git commit hash |
| `last_error` | string |  |  | Last error message |
| `loaded_components` | object |  |  | Loaded model components (vae, clip_l, etc.) |
| `loading_model_name` | string |  |  | Name of model being loaded |
| `loading_step` | integer |  |  | Current loading step |
| `loading_total_steps` | integer |  |  | Total loading steps |
| `memory` | object |  |  | System/GPU memory information |
| `model_architecture` | string |  |  | Detected architecture |
| `model_loaded` | boolean | yes |  | Whether a model is currently loaded |
| `model_loading` | boolean |  |  | Whether a model is being loaded |
| `model_name` | string |  |  | Name of loaded model |
| `model_type` | string |  |  | Type of loaded model |
| `status` | string | yes |  | Server status (ok) |
| `upscaler_loaded` | boolean |  |  | Whether an upscaler is loaded |
| `upscaler_name` | string |  |  | Loaded upscaler name |
| `version` | string | yes |  | Server version string |
| `ws_enabled` | boolean |  |  | Whether WebSocket is enabled |

### schema `HuggingfaceInfoResponse` <a id="schema-huggingfaceinforesponse"></a>

HuggingFace model file metadata

| field | type | required | default | description |
|---|---|---|---|---|
| `download_url` | string |  |  | Download URL |
| `file_size` | integer |  |  | File size in bytes |
| `filename` | string |  |  | File name |
| `repo_id` | string |  |  | Repository ID |
| `revision` | string |  |  | Git revision |
| `success` | boolean | yes |  | Whether lookup succeeded |

### schema `Img2ImgRequest` <a id="schema-img2imgrequest"></a>

Image-to-image generation request

Extends: [GenerationRequestBase](#schema-generationrequestbase)

| field | type | required | default | description |
|---|---|---|---|---|
| `img_cfg_scale` | number |  | -1.0 | Image CFG scale (-1 for auto) |
| `init_image_base64` | string | yes |  | Source image as base64-encoded string |
| `mask_image_base64` | string |  |  | Inpainting mask as base64 (white=inpaint) |
| `strength` | number |  | 0.75 | Denoising strength (0.0-1.0) |

### schema `JobCreatedResponse` <a id="schema-jobcreatedresponse"></a>

Response when a job is queued

| field | type | required | default | description |
|---|---|---|---|---|
| `job_id` | string | yes |  | UUID of the created job |
| `position` | integer | yes |  | Position in queue |
| `status` | string | yes |  | Job status (pending) |

### schema `LoadModelRequest` <a id="schema-loadmodelrequest"></a>

Load a model into memory

| field | type | required | default | description |
|---|---|---|---|---|
| `audio_vae` | string |  |  | Audio VAE (LTXAV / LTX 2.3 — for video models that produce sound) |
| `clip_g` | string |  |  | CLIP-G text encoder name |
| `clip_l` | string |  |  | CLIP-L text encoder name |
| `clip_vision` | string |  |  | CLIP vision model name |
| `controlnet` | string |  |  | ControlNet model name |
| `embeddings_connectors` | string |  |  | Embeddings connectors (LTXAV / LTX 2.3) |
| `high_noise_diffusion_model` | string |  |  | High-noise diffusion model (for dual-stage) |
| `llm` | string |  |  | LLM model name |
| `llm_vision` | string |  |  | LLM vision model name |
| `model_name` | string | yes |  | Name of the model file to load |
| `model_type` | enum (`checkpoint`, `diffusion`) |  | checkpoint | Type of model |
| `options` | [LoadOptions](#schema-loadoptions) |  |  | Model loading options |
| `photo_maker` | string |  |  | PhotoMaker model name |
| `pulid_weights` | string |  |  | PuLID-Flux identity-injection weights (leejet PR #1595). Looked up under the checkpoints directory. |
| `t5xxl` | string |  |  | T5-XXL text encoder name |
| `taesd` | string |  |  | TAESD model for fast preview |
| `uncond_diffusion_model` | string |  |  | Unconditional diffusion model (leejet master post-1ceb5bd) |
| `vae` | string |  |  | VAE model name |

### schema `LoadModelResponse` <a id="schema-loadmodelresponse"></a>

Model load result

| field | type | required | default | description |
|---|---|---|---|---|
| `loaded_components` | object |  |  | Active model components |
| `message` | string | yes |  | Status message |
| `model_name` | string |  |  | Name of loaded model |
| `model_type` | string |  |  | Type of loaded model |
| `success` | boolean | yes |  | Whether load succeeded |

### schema `LoadOptions` <a id="schema-loadoptions"></a>

Model loading options

| field | type | required | default | description |
|---|---|---|---|---|
| `backend` | string |  |  | Main compute backend override (empty = sd.cpp picks). Use per-component placement here too — e.g. "diffusion=cuda0,vae=cpu" — that's how per-component CPU keeping is expressed now (formerly keep_clip_on_cpu / keep_vae_on_cpu / keep_controlnet_on_cpu). |
| `diffusion_conv_direct` | boolean |  | false | Direct diffusion convolution |
| `diffusion_flash_attn` | boolean |  | false | Enable flash attention specifically for the diffusion model (UNet/DiT/Flux) |
| `eager_load` | boolean |  | true | Pre-load all params into the params backend at model-load time instead of lazily on first use (leejet PR #1687). Pairs naturally with stream_layers on a CPU params backend — the first generation no longer pays for lazy fault-in. Restapi defaults to true (long-lived server: first request after load should be fast); upstream sd-cli defaults to false (one-shot tool). |
| `enable_mmap` | boolean |  | true | Enable memory-mapped file access |
| `flash_attn` | boolean |  | true | Enable flash attention for CLIP/T5/conditioner |
| `lora_apply_mode` | enum (`auto`, `immediately`, `at_runtime`) |  | auto | LoRA application mode |
| `max_vram` | number |  | 0 | GiB budget for graph-cut segmented param offload (0 = disabled) |
| `model_args` | string |  |  | Model-specific args (key=value list) — replaces the old chroma_*/qwen_image_zero_cond_t individual load flags. |
| `n_threads` | integer |  | -1 | Number of CPU threads (-1 for auto) |
| `params_backend` | string |  |  | Parameter storage backend override (empty = same as backend). Set to "*=cpu" for the global "keep all weights in RAM" mode that was previously offload_to_cpu. |
| `prediction` | enum (`eps`, `v`, `edm_v`, `sd3_flow`, `flux_flow`, `flux2_flow`, `sefi_flow`, `minit2i_flow`, ``) |  |  | Prediction type |
| `rng_type` | enum (`cuda`, `std_default`, `cpu`) |  | cuda | Random number generator type |
| `rpc_servers` | string |  |  | RPC distributed-backend node list, comma-separated host:port pairs (leejet PR #1629). Empty = no RPC. |
| `sampler_rng_type` | string |  |  | Sampler-specific RNG type |
| `stream_layers` | boolean |  | false | Engage residency+async-prefetch streaming on top of max_vram. Requires max_vram > 0; no effect when max_vram == 0. sd.cpp's planner picks the residency split automatically and overlaps next-segment H2D with current-segment compute. |
| `tae_preview_only` | boolean |  | false | Use TAESD for preview only |
| `tensor_type_rules` | string |  |  | Custom tensor type rules string |
| `vae_conv_direct` | boolean |  | false | Direct VAE convolution |
| `vae_format` | enum (`auto`, `flux`, `sd3`, `flux2`) |  | auto | VAE weight format override (auto = sd.cpp detects from the file) |
| `weight_type` | enum (`f32`, `f16`, `bf16`, `q8_0`, `q5_0`, `q5_1`, `q4_0`, `q4_1`, `q4_k`, `q5_k`, `q6_k`, `q8_k`, `q3_k`, `q2_k`, `mxfp4`, `nvfp4`, `q1_0`) |  |  | Weight precision type |

### schema `LoadUpscalerRequest` <a id="schema-loadupscalerrequest"></a>

Load an ESRGAN upscaler model

| field | type | required | default | description |
|---|---|---|---|---|
| `model_name` | string | yes |  | ESRGAN model name |
| `n_threads` | integer |  | -1 | CPU threads (-1 for auto) |
| `tile_size` | integer |  | 128 | Processing tile size |

### schema `LoginRequest` <a id="schema-loginrequest"></a>

Username + password pair for issuing a bearer token

| field | type | required | default | description |
|---|---|---|---|---|
| `password` | string | yes |  | Server-configured password |
| `username` | string | yes |  | Server-configured username |

### schema `LoginResponse` <a id="schema-loginresponse"></a>

Issued bearer token plus its absolute expiration time

| field | type | required | default | description |
|---|---|---|---|---|
| `expires_at` | integer | yes |  | Token expiration as Unix epoch seconds |
| `token` | string | yes |  | Opaque bearer token (~43 base64url characters) |
| `token_type` | string | yes |  | Always "Bearer" — included to match common OAuth-style clients |

### schema `LogoutResponse` <a id="schema-logoutresponse"></a>

Response to logout request

| field | type | required | default | description |
|---|---|---|---|---|
| `message` | string | yes |  | Confirmation message |
| `success` | boolean | yes |  | Always true |

### schema `MemoryResponse` <a id="schema-memoryresponse"></a>

Detailed memory usage information

| field | type | required | default | description |
|---|---|---|---|---|
| `gpu` | object |  |  | GPU VRAM usage (if available) |
| `process` | object |  |  | Process memory (RSS and virtual) |
| `system` | object |  |  | System RAM usage (total/used/free in bytes and MB) |

### schema `ModelListResponse` <a id="schema-modellistresponse"></a>

List of all available models by category

| field | type | required | default | description |
|---|---|---|---|---|
| `checkpoints` | array<object> |  |  | Checkpoint models |
| `clip` | array<object> |  |  | CLIP models |
| `controlnets` | array<object> |  |  | ControlNet models |
| `diffusion_models` | array<object> |  |  | Diffusion models |
| `embeddings` | array<object> |  |  | Embedding models |
| `esrgan` | array<object> |  |  | ESRGAN upscaler models |
| `llm` | array<object> |  |  | LLM models |
| `loaded_model` | string |  |  | Currently loaded model name |
| `loaded_model_type` | string |  |  | Type of currently loaded model |
| `loras` | array<object> |  |  | LoRA models |
| `t5` | array<object> |  |  | T5 models |
| `taesd` | array<object> |  |  | TAESD preview models |
| `vae` | array<object> |  |  | VAE models |

### schema `ModelPathsResponse` <a id="schema-modelpathsresponse"></a>

Configured model storage paths

| field | type | required | default | description |
|---|---|---|---|---|
| `checkpoints` | string |  |  | Checkpoint models directory |
| `clip` | string |  |  | CLIP models directory |
| `controlnet` | string |  |  | ControlNet models directory |
| `diffusion_models` | string |  |  | Diffusion models directory |
| `embeddings` | string |  |  | Embeddings directory |
| `esrgan` | string |  |  | ESRGAN models directory |
| `llm` | string |  |  | LLM models directory |
| `lora` | string |  |  | LoRA models directory |
| `t5` | string |  |  | T5 models directory |
| `taesd` | string |  |  | TAESD models directory |
| `vae` | string |  |  | VAE models directory |

### schema `ModelRefreshResponse` <a id="schema-modelrefreshresponse"></a>

Model list refresh result

| field | type | required | default | description |
|---|---|---|---|---|
| `message` | string | yes |  | Status message |
| `models` | object |  |  | Updated model list |
| `success` | boolean | yes |  | Whether refresh succeeded |

### schema `OptionDescriptionsResponse` <a id="schema-optiondescriptionsresponse"></a>

Descriptions of all available options

| field | type | required | default | description |
|---|---|---|---|---|
| `rng_types` | object |  |  | RNG type name to description mapping |
| `samplers` | object |  |  | Sampler name to description mapping |
| `schedulers` | object |  |  | Scheduler name to description mapping |

### schema `OptionsResponse` <a id="schema-optionsresponse"></a>

Available generation options

| field | type | required | default | description |
|---|---|---|---|---|
| `quantization_types` | array<object> | yes |  | Available quantization types |
| `samplers` | array<string> | yes |  | Available sampling algorithms |
| `schedulers` | array<string> | yes |  | Available noise schedulers |

### schema `PreviewSettingsResponse` <a id="schema-previewsettingsresponse"></a>

Preview generation settings

| field | type | required | default | description |
|---|---|---|---|---|
| `enabled` | boolean | yes |  | Whether live preview is enabled |
| `interval` | integer | yes |  | Preview interval (every N steps) |
| `max_size` | integer |  |  | Maximum preview dimension |
| `mode` | enum (`none`, `proj`, `tae`, `vae`) | yes |  | Preview decode mode |
| `quality` | integer |  |  | JPEG quality (1-100) |

### schema `QueueListResponse` <a id="schema-queuelistresponse"></a>

Queue listing with pagination and status counts

| field | type | required | default | description |
|---|---|---|---|---|
| `applied_filters` | object |  |  | Active filter parameters |
| `cancelled_count` | integer | yes |  | Number of cancelled jobs |
| `completed_count` | integer | yes |  | Number of completed jobs |
| `failed_count` | integer | yes |  | Number of failed jobs |
| `filtered_count` | integer | yes |  | Count after filters applied |
| `has_more` | boolean |  |  | Whether more results exist |
| `has_prev` | boolean |  |  | Whether previous page exists |
| `items` | array<object> | yes |  | Job entries |
| `limit` | integer |  |  | Results per page |
| `newest_timestamp` | integer |  |  | Newest job timestamp (unix) |
| `offset` | integer |  |  | Pagination offset |
| `oldest_timestamp` | integer |  |  | Oldest job timestamp (unix) |
| `page` | integer |  |  | Current page number |
| `pending_count` | integer | yes |  | Number of pending jobs |
| `processing_count` | integer | yes |  | Number of processing jobs |
| `total_count` | integer | yes |  | Total job count |
| `total_pages` | integer |  |  | Total number of pages |

### schema `RecycleBinResponse` <a id="schema-recyclebinresponse"></a>

Recycle bin contents

| field | type | required | default | description |
|---|---|---|---|---|
| `count` | integer | yes |  | Number of items in recycle bin |
| `enabled` | boolean | yes |  | Whether recycle bin is enabled |
| `items` | array<object> |  |  | Deleted job entries |
| `retention_minutes` | integer |  |  | Auto-purge retention period (minutes) |
| `success` | boolean | yes |  | Operation success |

### schema `RecycleBinSettingsResponse` <a id="schema-recyclebinsettingsresponse"></a>

Recycle bin configuration

| field | type | required | default | description |
|---|---|---|---|---|
| `enabled` | boolean | yes |  | Whether recycle bin is enabled |
| `retention_minutes` | integer | yes |  | Auto-purge retention period |

### schema `SuccessResponse` <a id="schema-successresponse"></a>

Standard success response

| field | type | required | default | description |
|---|---|---|---|---|
| `message` | string | yes |  | Human-readable status message |
| `success` | boolean | yes |  | Whether the operation succeeded |

### schema `Txt2ImgRequest` <a id="schema-txt2imgrequest"></a>

Text-to-image generation request

Extends: [GenerationRequestBase](#schema-generationrequestbase)

_(no properties)_

### schema `Txt2VidRequest` <a id="schema-txt2vidrequest"></a>

Text-to-video generation request

Extends: [GenerationRequestBase](#schema-generationrequestbase)

| field | type | required | default | description |
|---|---|---|---|---|
| `control_frames` | array<string> |  |  | Control frames as base64 strings |
| `end_image_base64` | string |  |  | Ending frame image as base64 |
| `flow_shift` | number |  | 3.0 | Flow matching shift parameter |
| `fps` | integer |  | 16 | Output video frames per second |
| `high_noise_cfg_scale` | number |  | 7.0 | CFG scale for high-noise phase |
| `high_noise_custom_sigmas` | array<number> |  |  | Custom sigma schedule for high-noise phase |
| `high_noise_distilled_guidance` | number |  | 3.5 | Distilled guidance for high-noise |
| `high_noise_eta` | number |  | 0.0 | Eta for high-noise phase |
| `high_noise_extra_sample_args` | string |  |  | Pass-through key=value sample args for high-noise phase |
| `high_noise_flow_shift` | number |  | 0.0 | Flow shift for high-noise phase (0 = inherit main flow_shift) |
| `high_noise_img_cfg` | number |  | -1.0 | Image CFG for high-noise phase (-1 = inherit high_noise_cfg_scale) |
| `high_noise_sampler` | string |  |  | Sampler for high-noise phase (empty = inherit main) |
| `high_noise_scheduler` | string |  |  | Scheduler for high-noise phase (empty = inherit main) |
| `high_noise_shifted_timestep` | integer |  | 0 | Shifted timestep for high-noise phase |
| `high_noise_skip_layers` | array<integer> |  |  | Skip layers for high-noise SLG |
| `high_noise_slg_end` | number |  | 0.2 | SLG end for high-noise |
| `high_noise_slg_scale` | number |  | 0.0 | SLG scale for high-noise phase |
| `high_noise_slg_start` | number |  | 0.01 | SLG start for high-noise |
| `high_noise_steps` | integer |  | -1 | High-noise sampling steps (-1 for auto) |
| `init_image_base64` | string |  |  | Starting frame image as base64 |
| `moe_boundary` | number |  | 0.875 | MoE boundary for WAN models |
| `strength` | number |  | 0.75 | Denoising strength for init image |
| `vace_strength` | number |  | 1.0 | VACE control strength |
| `video_frames` | integer |  | 33 | Number of video frames to generate |

### schema `UnauthorizedResponse` <a id="schema-unauthorizedresponse"></a>

Returned with HTTP 401 when authentication is missing or invalid

| field | type | required | default | description |
|---|---|---|---|---|
| `error` | string | yes |  | Short error code |
| `message` | string | yes |  | Human-readable message |

### schema `UpdatePreviewSettingsRequest` <a id="schema-updatepreviewsettingsrequest"></a>

Update preview settings

| field | type | required | default | description |
|---|---|---|---|---|
| `enabled` | boolean |  |  | Enable/disable live preview |
| `interval` | integer |  |  | Preview interval (every N steps) |
| `max_size` | integer |  |  | Maximum preview dimension |
| `mode` | enum (`none`, `proj`, `tae`, `vae`) |  |  | Preview decode mode |
| `quality` | integer |  |  | JPEG quality (1-100) |

### schema `UploadModelResponse` <a id="schema-uploadmodelresponse"></a>

Result of a model file upload

| field | type | required | default | description |
|---|---|---|---|---|
| `filename` | string | yes |  | Stored filename |
| `full_path` | string | yes |  | Absolute path of the stored file on the server |
| `model_type` | string | yes |  | Model type the file was stored under |
| `size_bytes` | integer | yes |  | Size of the stored file in bytes |
| `subfolder` | string |  |  | Subfolder under the model_type directory (if any) |
| `success` | boolean | yes |  | Whether the upload succeeded |

### schema `UpscaleRequest` <a id="schema-upscalerequest"></a>

Image upscaling request

| field | type | required | default | description |
|---|---|---|---|---|
| `image_base64` | string | yes |  | Image to upscale as base64 |
| `repeats` | integer |  | 1 | Number of upscale passes |
| `tile_size` | integer |  | 128 | Processing tile size |
| `title` | string |  |  | Optional display title for the queue job |
| `upscale_factor` | integer |  | 4 | Upscale factor |

