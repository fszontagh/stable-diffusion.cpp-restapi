// API Types
export interface QuantizationType {
  id: string
  name: string
  bits: number
}

export interface OptionsResponse {
  samplers: string[]
  schedulers: string[]
  quantization_types: QuantizationType[]
}

export interface HealthResponse {
  status: string
  model_loaded: boolean
  model_loading: boolean
  loading_model_name: string | null
  loading_step: number | null
  loading_total_steps: number | null
  last_error: string | null
  model_name: string | null
  model_type: string | null
  model_architecture: string | null
  loaded_components: {
    vae: string | null
    clip_l: string | null
    clip_g: string | null
    t5xxl: string | null
    controlnet: string | null
    llm: string | null
    llm_vision: string | null
  }
  upscaler_loaded: boolean
  upscaler_name: string | null
  ws_port: number | null
}

export interface ModelInfo {
  name: string
  type: string
  file_extension: string
  size_bytes: number
  hash: string | null
  is_loaded?: boolean
}

export interface ModelsResponse {
  checkpoints: ModelInfo[]
  diffusion_models: ModelInfo[]
  vae: ModelInfo[]
  loras: ModelInfo[]
  clip: ModelInfo[]
  t5: ModelInfo[]
  controlnets: ModelInfo[]
  llm: ModelInfo[]
  esrgan: ModelInfo[]
  taesd: ModelInfo[]
  embeddings: ModelInfo[]
  loaded_model: string | null
  loaded_model_type: string | null
  applied_filters?: {
    type?: string
    search?: string
    extension?: string
  }
}

export interface LoadModelParams {
  model_name: string
  model_type?: 'checkpoint' | 'diffusion'
  vae?: string
  clip_l?: string
  clip_g?: string
  clip_vision?: string
  t5xxl?: string
  controlnet?: string
  llm?: string
  llm_vision?: string
  taesd?: string
  high_noise_diffusion_model?: string
  photo_maker?: string
  options?: {
    n_threads?: number
    keep_clip_on_cpu?: boolean
    keep_vae_on_cpu?: boolean
    keep_controlnet_on_cpu?: boolean
    vae_conv_direct?: boolean
    diffusion_conv_direct?: boolean
    flash_attn?: boolean
    offload_to_cpu?: boolean
    enable_mmap?: boolean
    vae_decode_only?: boolean
    tae_preview_only?: boolean
    free_params_immediately?: boolean
    flow_shift?: number
    weight_type?: string
    tensor_type_rules?: string
    rng_type?: string
    sampler_rng_type?: string
    prediction?: string
    lora_apply_mode?: string
    vae_tiling?: boolean
    vae_tile_size_x?: number
    vae_tile_size_y?: number
    vae_tile_overlap?: number
    chroma_use_dit_mask?: boolean
    chroma_use_t5_mask?: boolean
    chroma_t5_mask_pad?: number
  }
}

export interface GenerationParams {
  prompt: string
  negative_prompt?: string
  width?: number
  height?: number
  steps?: number
  cfg_scale?: number
  distilled_guidance?: number
  eta?: number
  shifted_timestep?: number
  seed?: number
  sampler?: string
  scheduler?: string
  batch_count?: number
  clip_skip?: number
  slg_scale?: number
  skip_layers?: number[]
  slg_start?: number
  slg_end?: number
  custom_sigmas?: number[]
  ref_images?: string[]
  auto_resize_ref_image?: boolean
  increase_ref_index?: boolean
  control_image_base64?: string
  control_strength?: number
  vae_tiling?: boolean
  vae_tile_size_x?: number
  vae_tile_size_y?: number
  vae_tile_overlap?: number
  easycache?: boolean
  easycache_threshold?: number
  easycache_start?: number
  easycache_end?: number
  pm_id_images?: string[]
  pm_id_embed_path?: string
  pm_style_strength?: number
  upscale?: boolean
  upscale_repeats?: number
  upscale_auto_unload?: boolean
}

export interface Img2ImgParams extends GenerationParams {
  init_image_base64: string
  strength?: number
  img_cfg_scale?: number
  mask_image_base64?: string
}

export interface Txt2VidParams extends GenerationParams {
  video_frames?: number
  fps?: number
  flow_shift?: number
  init_image_base64?: string
  end_image_base64?: string
  strength?: number
  control_frames?: string[]
  high_noise_steps?: number
  high_noise_cfg_scale?: number
  high_noise_sampler?: string
  high_noise_distilled_guidance?: number
  high_noise_slg_scale?: number
  high_noise_skip_layers?: number[]
  high_noise_slg_start?: number
  high_noise_slg_end?: number
  moe_boundary?: number
  vace_strength?: number
}

export interface UpscaleParams {
  image_base64: string
  upscale_factor?: number
  tile_size?: number
  repeats?: number
}

export interface LoadUpscalerParams {
  model_name: string
  n_threads?: number
  tile_size?: number
}

export interface ConvertParams {
  input_path: string
  output_type: string
  output_path?: string
  vae_path?: string
  tensor_type_rules?: string
}

export interface ConvertResponse extends JobSubmitResponse {
  output_path: string
}

export interface JobProgress {
  step: number              // Current step (raw from sd.cpp callback)
  total_steps: number       // Total steps (raw from sd.cpp callback)
}

export interface JobModelSettings {
  model_loaded: boolean
  model_name: string | null
  model_type: string | null
  model_architecture: string | null
  loaded_components: {
    vae: string | null
    clip_l: string | null
    clip_g: string | null
    t5xxl: string | null
    controlnet: string | null
    llm: string | null
    llm_vision: string | null
  }
  load_options?: {
    n_threads?: number
    keep_clip_on_cpu?: boolean
    keep_vae_on_cpu?: boolean
    keep_controlnet_on_cpu?: boolean
    flash_attn?: boolean
    offload_to_cpu?: boolean
    vae_decode_only?: boolean
    vae_conv_direct?: boolean
    diffusion_conv_direct?: boolean
    tae_preview_only?: boolean
    free_params_immediately?: boolean
    flow_shift?: number
    weight_type?: string
    tensor_type_rules?: string
    rng_type?: string
    sampler_rng_type?: string
    prediction?: string
    lora_apply_mode?: string
    vae_tiling?: boolean
    vae_tile_size_x?: number
    vae_tile_size_y?: number
    vae_tile_overlap?: number
    chroma_use_dit_mask?: boolean
    chroma_use_t5_mask?: boolean
    chroma_t5_mask_pad?: number
  }
  upscaler_loaded: boolean
  upscaler_name: string | null
}

export interface Job {
  job_id: string
  type: 'txt2img' | 'img2img' | 'txt2vid' | 'upscale' | 'convert' | 'model_download' | 'model_hash'
  status: 'pending' | 'processing' | 'completed' | 'failed' | 'cancelled'
  progress: JobProgress
  created_at: string
  started_at?: string
  completed_at?: string
  outputs: string[]
  params?: Record<string, unknown>
  model_settings?: JobModelSettings
  error?: string
  linked_job_id?: string
}

export interface QueueDateGroup {
  date: string           // YYYY-MM-DD
  label: string          // "Today", "Yesterday", "Dec 21, 2025"
  timestamp: number      // Start of day timestamp
  count: number          // Total items in this date group
  items: Job[]
}

export interface QueueResponse {
  pending_count: number
  processing_count: number
  completed_count: number
  failed_count: number
  total_count: number
  filtered_count?: number
  items?: Job[]
  // Grouped response
  groups?: QueueDateGroup[]
  group_by?: 'date'
  // Pagination
  offset?: number
  limit: number
  page?: number
  total_pages?: number
  has_more: boolean
  has_prev?: boolean
  newest_timestamp?: number
  oldest_timestamp?: number
  applied_filters?: {
    status?: string
    type?: string
    search?: string
  }
}

export interface QueueFilters {
  status?: string
  type?: string
  search?: string
  architecture?: string   // Filter by model architecture (case-insensitive partial match)
  limit?: number
  offset?: number
  page?: number           // Page number (1-based)
  group_by?: 'date'       // Group jobs by date
  before?: number         // Timestamp for cursor-based pagination
  after?: number          // Timestamp for cursor-based pagination
}

export interface JobSubmitResponse {
  job_id: string
  status: string
  position: number
}

export interface ModelHashResponse {
  model_name: string
  model_type: string
  hash: string
}

export class ApiError extends Error {
  constructor(
    message: string,
    public status: number
  ) {
    super(message)
    this.name = 'ApiError'
  }
}

class ApiClient {
  private baseUrl: string

  constructor(baseUrl = '') {
    this.baseUrl = baseUrl
  }

  private async request<T>(method: string, endpoint: string, data?: unknown): Promise<T> {
    const options: RequestInit = {
      method,
      headers: {
        'Content-Type': 'application/json'
      }
    }

    if (data) {
      options.body = JSON.stringify(data)
    }

    const response = await fetch(this.baseUrl + endpoint, options)
    const json = await response.json()

    if (!response.ok) {
      throw new ApiError(json.error || 'Request failed', response.status)
    }

    return json as T
  }

  // Health
  async getHealth(): Promise<HealthResponse> {
    return this.request<HealthResponse>('GET', '/health')
  }

  async getOptions(): Promise<OptionsResponse> {
    return this.request<OptionsResponse>('GET', '/options')
  }

  // Models
  async getModels(filters?: { type?: string; extension?: string; search?: string }): Promise<ModelsResponse> {
    const params = new URLSearchParams()
    if (filters?.type) params.append('type', filters.type)
    if (filters?.extension) params.append('extension', filters.extension)
    if (filters?.search) params.append('search', filters.search)
    const query = params.toString()
    return this.request<ModelsResponse>('GET', '/models' + (query ? '?' + query : ''))
  }

  async loadModel(params: LoadModelParams): Promise<{ success: boolean; message: string; model_name: string; model_type: string; loaded_components: Record<string, string | null> }> {
    return this.request('POST', '/models/load', params)
  }

  async unloadModel(): Promise<{ success: boolean; message: string }> {
    return this.request('POST', '/models/unload', {})
  }

  async refreshModels(): Promise<{ success: boolean; message: string; models: ModelsResponse }> {
    return this.request('POST', '/models/refresh', {})
  }

  async getModelHash(modelType: string, modelName: string): Promise<ModelHashResponse> {
    return this.request('GET', `/models/hash/${modelType}/${encodeURIComponent(modelName)}`)
  }

  // Model Downloads
  async downloadModel(params: DownloadParams): Promise<DownloadResponse> {
    return this.request('POST', '/models/download', params)
  }

  async getCivitAIInfo(modelId: string): Promise<CivitAIModelInfo> {
    return this.request('GET', `/models/civitai/${encodeURIComponent(modelId)}`)
  }

  async getHuggingFaceInfo(repoId: string, filename: string, revision = 'main'): Promise<HuggingFaceModelInfo> {
    const params = new URLSearchParams({ repo_id: repoId, filename, revision })
    return this.request('GET', `/models/huggingface?${params}`)
  }

  async getModelPaths(): Promise<ModelPathsConfig> {
    return this.request('GET', '/models/paths')
  }

  // Generation
  async txt2img(params: GenerationParams): Promise<JobSubmitResponse> {
    return this.request('POST', '/txt2img', params)
  }

  async img2img(params: Img2ImgParams): Promise<JobSubmitResponse> {
    return this.request('POST', '/img2img', params)
  }

  async txt2vid(params: Txt2VidParams): Promise<JobSubmitResponse> {
    return this.request('POST', '/txt2vid', params)
  }

  async upscale(params: UpscaleParams): Promise<JobSubmitResponse> {
    return this.request('POST', '/upscale', params)
  }

  async convert(params: ConvertParams): Promise<ConvertResponse> {
    return this.request('POST', '/convert', params)
  }

  // Upscaler
  async loadUpscaler(params: LoadUpscalerParams): Promise<{ success: boolean; message: string; model_name: string; upscale_factor: number }> {
    return this.request('POST', '/upscaler/load', params)
  }

  async unloadUpscaler(): Promise<{ success: boolean; message: string }> {
    return this.request('POST', '/upscaler/unload', {})
  }

  // Queue
  async getQueue(filters?: QueueFilters): Promise<QueueResponse> {
    const params = new URLSearchParams()
    if (filters?.status && filters.status !== 'all') params.append('status', filters.status)
    if (filters?.type && filters.type !== 'all') params.append('type', filters.type)
    if (filters?.search) params.append('search', filters.search)
    if (filters?.limit !== undefined) params.append('limit', String(filters.limit))
    if (filters?.offset !== undefined) params.append('offset', String(filters.offset))
    if (filters?.page !== undefined) params.append('page', String(filters.page))
    if (filters?.group_by) params.append('group_by', filters.group_by)
    if (filters?.before !== undefined) params.append('before', String(filters.before))
    if (filters?.after !== undefined) params.append('after', String(filters.after))
    const query = params.toString()
    return this.request<QueueResponse>('GET', '/queue' + (query ? '?' + query : ''))
  }

  async getJob(jobId: string): Promise<Job> {
    return this.request<Job>('GET', `/queue/${jobId}`)
  }

  async cancelJob(jobId: string): Promise<{ success: boolean; message: string }> {
    return this.request('DELETE', `/queue/${jobId}`)
  }

  // Restart a job by resubmitting with same params
  async restartJob(job: Job): Promise<JobSubmitResponse> {
    if (!job.params) {
      throw new ApiError('Job has no parameters to restart', 400)
    }
    switch (job.type) {
      case 'txt2img':
        return this.txt2img(job.params as unknown as GenerationParams)
      case 'img2img':
        return this.img2img(job.params as unknown as Img2ImgParams)
      case 'txt2vid':
        return this.txt2vid(job.params as unknown as Txt2VidParams)
      case 'upscale':
        return this.upscale(job.params as unknown as UpscaleParams)
      default:
        throw new ApiError(`Unknown job type: ${job.type}`, 400)
    }
  }

  // Ollama - Prompt Enhancement
  async enhancePrompt(params: EnhancePromptRequest): Promise<EnhancePromptResponse> {
    return this.request('POST', '/ollama/enhance', params)
  }

  async getPromptHistory(limit = 50, offset = 0): Promise<PromptHistoryResponse> {
    return this.request('GET', `/ollama/history?limit=${limit}&offset=${offset}`)
  }

  async getPromptHistoryEntry(id: string): Promise<PromptHistoryEntry> {
    return this.request('GET', `/ollama/history/${id}`)
  }

  async deletePromptHistoryEntry(id: string): Promise<{ success: boolean }> {
    return this.request('DELETE', `/ollama/history/${id}`)
  }

  async clearPromptHistory(): Promise<{ success: boolean }> {
    return this.request('DELETE', '/ollama/history')
  }

  async getOllamaStatus(): Promise<OllamaStatusResponse> {
    return this.request('GET', '/ollama/status')
  }

  async getOllamaModels(): Promise<{ models: string[] }> {
    return this.request('GET', '/ollama/models')
  }

  async getOllamaSettings(): Promise<OllamaSettings> {
    return this.request('GET', '/ollama/settings')
  }

  async updateOllamaSettings(settings: Partial<OllamaSettings>): Promise<OllamaSettingsUpdateResponse> {
    return this.request('PUT', '/ollama/settings', settings)
  }

  // Preview settings
  async getPreviewSettings(): Promise<PreviewSettings> {
    return this.request('GET', '/preview/settings')
  }

  async updatePreviewSettings(settings: Partial<PreviewSettings>): Promise<PreviewSettingsUpdateResponse> {
    return this.request('PUT', '/preview/settings', settings)
  }

  // Assistant (LLM helper)
  async assistantChat(request: AssistantChatRequest): Promise<AssistantChatResponse> {
    return this.request('POST', '/assistant/chat', request)
  }

  async getAssistantHistory(): Promise<AssistantHistoryResponse> {
    return this.request('GET', '/assistant/history')
  }

  async clearAssistantHistory(): Promise<{ success: boolean }> {
    return this.request('DELETE', '/assistant/history')
  }

  async getAssistantStatus(): Promise<AssistantStatusResponse> {
    return this.request('GET', '/assistant/status')
  }

  async getAssistantSettings(): Promise<AssistantSettings> {
    return this.request('GET', '/assistant/settings')
  }

  async updateAssistantSettings(settings: Partial<AssistantSettings>): Promise<AssistantSettingsUpdateResponse> {
    return this.request('PUT', '/assistant/settings', settings)
  }
}

// Ollama Types
export interface EnhancePromptRequest {
  prompt: string
  system_prompt?: string
}

export interface EnhancePromptResponse {
  success: boolean
  id?: string
  original_prompt?: string
  enhanced_prompt?: string
  model_used?: string
  created_at?: number
  error?: string
}

export interface PromptHistoryEntry {
  id: string
  original_prompt: string
  enhanced_prompt: string
  model_used: string
  created_at: number
  success: boolean
  error_message?: string
}

export interface PromptHistoryResponse {
  total_count: number
  items: PromptHistoryEntry[]
}

export interface OllamaStatusResponse {
  enabled: boolean
  connected: boolean
  endpoint: string
  model: string
  available_models?: string[]
}

export interface OllamaSettings {
  enabled: boolean
  endpoint: string
  model: string
  temperature: number
  max_tokens: number
  timeout_seconds: number
  max_history: number
  system_prompt: string
  has_api_key: boolean
}

export interface OllamaSettingsUpdateResponse {
  success: boolean
  settings: OllamaSettings
}

// Preview Types
export interface PreviewSettings {
  enabled: boolean
  mode: 'none' | 'proj' | 'tae' | 'vae'
  interval: number
  max_size: number
  quality: number
}

export interface PreviewSettingsUpdateResponse {
  success: boolean
  settings: PreviewSettings
}

// Assistant Types
export interface AssistantAction {
  type: 'set_setting' | 'load_model' | 'set_component' | 'unload_model' | 'refresh_models' | 'generate' | 'cancel_job' | 'navigate' | 'set_image' | 'apply_recommended_settings' | 'highlight_setting' | 'load_upscaler' | 'unload_upscaler' | 'upscale' | 'ask_user' | 'download_model' | 'get_job' | 'search_jobs' | 'load_job_model' | 'convert_model'
  parameters: Record<string, unknown>
}

export interface AssistantMessage {
  role: 'user' | 'assistant' | 'system'
  content: string
  timestamp: number
  actions?: AssistantAction[]
  hidden?: boolean  // Internal messages that shouldn't be shown in UI
}

export interface AssistantContext {
  current_view: string
  settings: Record<string, unknown>
  model_info: {
    loaded: boolean
    name: string | null
    type: string | null
    architecture: string | null
    components: Record<string, string | null>
    architecture_info?: {
      name: string
      description: string
      requiredComponents: Record<string, string>
      optionalComponents: Record<string, string>
      generationDefaults: Record<string, unknown>
    }
  }
  upscaler_info?: {
    loaded: boolean
    name: string | null
  }
  available_models: {
    checkpoints: string[]
    diffusion_models: string[]
    vae: string[]
    loras: string[]
    clip: string[]
    t5: string[]
    controlnets: string[]
    esrgan: string[]
    llm: string[]
  }
  architecture_presets?: Record<string, {
    name: string
    description: string
    requiredComponents: Record<string, string>
    generationDefaults: Record<string, unknown>
  }>
  queue_stats: {
    pending: number
    processing: number
    completed: number
    failed: number
  }
  recommended_settings?: Record<string, unknown>
  recent_errors: string[]
  recent_jobs: Array<{
    type: string
    status: string
    error?: string
  }>
  last_action_results?: {
    successes: string[]
    errors: string[]
  }
}

export interface AssistantChatRequest {
  message: string
  context: AssistantContext
}

export interface AssistantChatResponse {
  success: boolean
  message?: string
  actions?: AssistantAction[]
  error?: string
}

export interface AssistantHistoryResponse {
  messages: AssistantMessage[]
  count: number
}

export interface AssistantStatusResponse {
  enabled: boolean
  connected: boolean
  endpoint: string
  model: string
  available_models?: string[]
  proactive_suggestions: boolean
}

export interface AssistantSettings {
  enabled: boolean
  endpoint: string
  model: string
  temperature: number
  max_tokens: number
  timeout_seconds: number
  max_history_turns: number
  proactive_suggestions: boolean
  system_prompt: string
  has_api_key: boolean
}

export interface AssistantSettingsUpdateResponse {
  success: boolean
  settings: AssistantSettings
}

// Download Types
export interface DownloadParams {
  source?: 'url' | 'civitai' | 'huggingface'
  model_type: 'checkpoint' | 'vae' | 'lora' | 'clip' | 't5' | 'embedding' | 'controlnet' | 'llm' | 'esrgan' | 'diffusion' | 'taesd'
  subfolder?: string
  url?: string
  model_id?: string  // For CivitAI
  repo_id?: string   // For HuggingFace
  filename?: string  // For HuggingFace or custom filename
  revision?: string  // For HuggingFace
}

export interface DownloadResponse {
  success: boolean
  download_job_id: string
  hash_job_id: string
  source: string
  model_type: string
  position: number
}

export interface CivitAIModelInfo {
  success: boolean
  model_id: number
  version_id: number
  name: string
  version_name: string
  type: string
  base_model: string
  filename: string
  file_size: number
  sha256: string
  download_url: string
}

export interface HuggingFaceModelInfo {
  success: boolean
  repo_id: string
  filename: string
  revision: string
  file_size: number
  download_url: string
}

export interface ModelPathsConfig {
  models: string
  vae: string
  loras: string
  clip: string
  t5: string
  embeddings: string
  controlnet: string
  llm: string
  esrgan: string
  diffusion: string
  taesd: string
}

export const api = new ApiClient()
