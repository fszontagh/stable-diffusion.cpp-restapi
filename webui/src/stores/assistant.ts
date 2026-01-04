import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import {
  api,
  type AssistantMessage,
  type AssistantAction,
  type AssistantContext,
  type AssistantSettings,
  type LoadModelParams,
  type GenerationParams,
  type Img2ImgParams,
  type Txt2VidParams,
  type UpscaleParams,
  type ConvertParams
} from '../api/client'
import { useAppStore } from './app'
import { wsService, type JobStatusChangedData } from '../services/websocket'

export const useAssistantStore = defineStore('assistant', () => {
  const appStore = useAppStore()

  // State
  const enabled = ref(false)
  const connected = ref(false)
  const isOpen = ref(false)
  const isMinimized = ref(true)
  const isLoading = ref(false)
  const messages = ref<AssistantMessage[]>([])
  const hasSuggestion = ref(false)
  const suggestionText = ref('')
  const currentView = ref('dashboard')
  const proactiveSuggestions = ref(true)

  // Settings from server
  const settings = ref<AssistantSettings | null>(null)

  // Error state
  const lastError = ref<string | null>(null)

  // Track results of last executed actions (for feedback to LLM)
  const lastActionResults = ref<{ successes: string[]; errors: string[] } | null>(null)

  // Track pending job continuations - when a job completes, trigger a follow-up message
  // Maps job_id -> continuation context (what the assistant was doing when it started this job)
  interface PendingContinuation {
    jobId: string
    context: string  // Brief description of what the assistant is working on
    startedAt: number
  }
  const pendingContinuations = ref<Map<string, PendingContinuation>>(new Map())

  // WebSocket unsubscriber
  let wsUnsubscriber: (() => void) | null = null

  // Question modal state - for ask_user action
  interface PendingQuestion {
    id: string
    title: string
    question: string
    options: string[]  // Predefined answer options
    allowCustom: boolean  // Allow custom text input
    timestamp: number
  }
  const pendingQuestion = ref<PendingQuestion | null>(null)
  const questionMinimized = ref(false)
  let questionResolve: ((answer: string) => void) | null = null

  // Computed
  const messageCount = computed(() => messages.value.length)

  const hasMessages = computed(() => messages.value.length > 0)

  // Initialize - fetch status and history
  async function initialize() {
    try {
      // Always fetch architectures (needed for model recommendations)
      await fetchArchitectures()

      const status = await api.getAssistantStatus()
      enabled.value = status.enabled
      connected.value = status.connected
      proactiveSuggestions.value = status.proactive_suggestions

      if (enabled.value) {
        await loadHistory()
        await loadSettings()
        // Ensure models are loaded for context
        if (!appStore.models) {
          await appStore.fetchModels()
        }
        // Set up WebSocket listener for job completions
        setupJobCompletionListener()
      }
    } catch (e) {
      console.error('[AssistantStore] Failed to initialize:', e)
      enabled.value = false
      connected.value = false
    }
  }

  // Set up WebSocket listener to handle job completions for continuations
  function setupJobCompletionListener() {
    // Clean up existing listener if any
    if (wsUnsubscriber) {
      wsUnsubscriber()
    }

    wsUnsubscriber = wsService.on<JobStatusChangedData>('job_status_changed', async (data) => {
      // Check if this job has a pending continuation
      const continuation = pendingContinuations.value.get(data.job_id)
      if (!continuation) return

      // Only trigger on completion or failure
      if (data.status !== 'completed' && data.status !== 'failed') return

      // Remove from pending
      pendingContinuations.value.delete(data.job_id)

      // Build the follow-up message
      let followUpMessage: string
      if (data.status === 'completed') {
        followUpMessage = `Job ${data.job_id} completed successfully. ${continuation.context ? `Continue with: ${continuation.context}` : 'Please continue with the next step.'}`
      } else {
        followUpMessage = `Job ${data.job_id} failed${data.error ? `: ${data.error}` : ''}. ${continuation.context ? `Original task: ${continuation.context}` : 'How would you like to proceed?'}`
      }

      console.log(`[AssistantStore] Job ${data.job_id} ${data.status}, triggering continuation`)

      // Small delay to let the queue update
      await new Promise(resolve => setTimeout(resolve, 500))

      // Send follow-up message to assistant (this will be a system-like message from the user perspective)
      // We mark it as a system continuation so the UI can style it differently if needed
      await sendMessage(followUpMessage, true)
    })
  }

  // Clean up WebSocket listener
  function cleanup() {
    if (wsUnsubscriber) {
      wsUnsubscriber()
      wsUnsubscriber = null
    }
  }

  // Load conversation history
  async function loadHistory() {
    try {
      const result = await api.getAssistantHistory()
      // Backend returns newest-first, reverse to get chronological order (oldest first)
      // This matches our push() behavior for new messages
      messages.value = result.messages.reverse()
    } catch (e) {
      console.error('[AssistantStore] Failed to load history:', e)
    }
  }

  // Load settings
  async function loadSettings() {
    try {
      settings.value = await api.getAssistantSettings()
      proactiveSuggestions.value = settings.value.proactive_suggestions
    } catch (e) {
      console.error('[AssistantStore] Failed to load settings:', e)
    }
  }

  // Helper function to fetch an image and convert to base64
  async function fetchImageAsBase64(url: string): Promise<string | null> {
    try {
      // Handle relative URLs
      const fullUrl = url.startsWith('/') ? window.location.origin + url : url
      const response = await fetch(fullUrl)
      if (!response.ok) {
        console.error(`[Assistant] Failed to fetch image: ${response.status}`)
        return null
      }
      const blob = await response.blob()
      return new Promise((resolve) => {
        const reader = new FileReader()
        reader.onloadend = () => {
          const result = reader.result as string
          // Remove the data URL prefix if present (we just want the base64)
          const base64 = result.includes(',') ? result.split(',')[1] : result
          resolve(base64)
        }
        reader.onerror = () => resolve(null)
        reader.readAsDataURL(blob)
      })
    } catch (e) {
      console.error('[Assistant] Error fetching image:', e)
      return null
    }
  }

  // Architecture presets fetched from API
  interface ArchitecturePreset {
    id: string
    name: string
    description: string
    aliases: string[]
    requiredComponents: Record<string, string>
    optionalComponents: Record<string, string>
    loadOptions: Record<string, unknown>
    generationDefaults: Record<string, unknown>
  }

  // State for architecture presets (fetched from /architectures API)
  const architectures = ref<Record<string, ArchitecturePreset>>({})
  const currentArchitecturePreset = ref<ArchitecturePreset | null>(null)

  // Fetch architecture presets from API
  async function fetchArchitectures() {
    try {
      const response = await fetch('/architectures')
      if (!response.ok) {
        console.warn('[AssistantStore] Failed to fetch architectures:', response.status)
        return
      }
      const data = await response.json()
      architectures.value = data.architectures || {}
      currentArchitecturePreset.value = data.current_preset || null
      console.log('[AssistantStore] Loaded', Object.keys(architectures.value).length, 'architecture presets')
    } catch (e) {
      console.error('[AssistantStore] Error fetching architectures:', e)
    }
  }

  // Get architecture preset by name (with alias and partial matching)
  function getArchitecturePreset(archName: string): ArchitecturePreset | null {
    if (!archName) return null

    const archs = architectures.value
    const nameLower = archName.toLowerCase()

    // Try exact match first
    if (archs[archName]) {
      return archs[archName]
    }

    // Try case-insensitive match on ID
    for (const [id, preset] of Object.entries(archs)) {
      if (id.toLowerCase() === nameLower) {
        return preset
      }
    }

    // Try alias match
    for (const preset of Object.values(archs)) {
      if (preset.aliases?.some(alias => alias.toLowerCase() === nameLower)) {
        return preset
      }
    }

    // Try partial match
    for (const [id, preset] of Object.entries(archs)) {
      if (nameLower.includes(id.toLowerCase()) || id.toLowerCase().includes(nameLower)) {
        return preset
      }
    }

    return null
  }

  // Get recommended settings for current model
  function getRecommendedSettings(): Record<string, unknown> | null {
    const arch = appStore.modelArchitecture
    if (!arch) return null

    // Use current preset from API response if available
    if (currentArchitecturePreset.value) {
      return currentArchitecturePreset.value.generationDefaults
    }

    // Otherwise look up by architecture name
    const preset = getArchitecturePreset(arch)
    if (preset) {
      return preset.generationDefaults
    }

    return null
  }

  // Model type to appStore.models property mapping
  type ModelCategory = 'checkpoint' | 'diffusion' | 'vae' | 'clip' | 't5' | 'controlnet' | 'lora' | 'esrgan' | 'taesd'

  function getModelList(modelType: ModelCategory) {
    const models = appStore.models
    if (!models) return null

    switch (modelType) {
      case 'checkpoint': return models.checkpoints
      case 'diffusion': return models.diffusion_models
      case 'vae': return models.vae
      case 'clip': return models.clip
      case 't5': return models.t5
      case 'controlnet': return models.controlnets
      case 'lora': return models.loras
      case 'esrgan': return models.esrgan
      case 'taesd': return models.taesd
      default: return null
    }
  }

  // Find the best matching model name from available models
  // Supports partial name matching (e.g., "ailtmAnimeModel" matches "SD1x/ailtmAnimeModel_v045Beta.safetensors")
  function findMatchingModel(searchName: string, modelType: ModelCategory): string | null {
    const models = appStore.models
    if (!models) {
      console.warn('[Assistant] findMatchingModel: models not loaded')
      return null
    }

    const modelList = getModelList(modelType)

    if (!modelList || modelList.length === 0) {
      console.warn(`[Assistant] findMatchingModel: no ${modelType} models available`)
      return null
    }

    console.log(`[Assistant] findMatchingModel: searching for "${searchName}" in ${modelList.length} ${modelType} models`)

    const searchLower = searchName.toLowerCase()
    const withoutExt = searchLower.replace(/\.(safetensors|ckpt|pt|bin|gguf)$/i, '')

    // 1. Try exact match first
    const exactMatch = modelList.find(m => m.name === searchName)
    if (exactMatch) {
      console.log(`[Assistant] Found exact match: ${exactMatch.name}`)
      return exactMatch.name
    }

    // 2. Try case-insensitive exact match
    const caseMatch = modelList.find(m => m.name.toLowerCase() === searchLower)
    if (caseMatch) {
      console.log(`[Assistant] Found case-insensitive match: ${caseMatch.name}`)
      return caseMatch.name
    }

    // 3. Try matching without extension
    const extMatch = modelList.find(m => {
      const nameLower = m.name.toLowerCase().replace(/\.(safetensors|ckpt|pt|bin|gguf)$/i, '')
      return nameLower === withoutExt
    })
    if (extMatch) {
      console.log(`[Assistant] Found match without extension: ${extMatch.name}`)
      return extMatch.name
    }

    // 4. Try matching the filename part (without path)
    const filenameMatch = modelList.find(m => {
      const filename = m.name.split('/').pop()?.toLowerCase() || ''
      const filenameNoExt = filename.replace(/\.(safetensors|ckpt|pt|bin|gguf)$/i, '')
      return filename === searchLower || filenameNoExt === withoutExt
    })
    if (filenameMatch) {
      console.log(`[Assistant] Found filename match: ${filenameMatch.name}`)
      return filenameMatch.name
    }

    // 5. Try partial match (search term is contained in model name)
    const partialMatches = modelList.filter(m => {
      const nameLower = m.name.toLowerCase()
      const filenameNoExt = nameLower.split('/').pop()?.replace(/\.(safetensors|ckpt|pt|bin|gguf)$/i, '') || ''
      return nameLower.includes(withoutExt) || filenameNoExt.includes(withoutExt)
    })

    if (partialMatches.length === 1) {
      console.log(`[Assistant] Found single partial match: ${partialMatches[0].name}`)
      return partialMatches[0].name
    } else if (partialMatches.length > 1) {
      // If multiple matches, prefer the one with the shortest name (most specific)
      partialMatches.sort((a, b) => a.name.length - b.name.length)
      console.log(`[Assistant] Found ${partialMatches.length} partial matches, using: ${partialMatches[0].name}`)
      return partialMatches[0].name
    }

    console.warn(`[Assistant] No match found for "${searchName}" in ${modelType} models`)
    console.log(`[Assistant] Available ${modelType} models:`, modelList.map(m => m.name).slice(0, 10))
    return null
  }

  // Gather current context for the assistant
  function gatherContext(): AssistantContext {
    // Get current generation settings from sessionStorage
    const currentSettings = gatherCurrentSettings()

    // Get recent errors from the error history and model load errors
    const recentErrorsList: string[] = []

    // Add model load error if present
    if (appStore.lastLoadError) {
      recentErrorsList.push(`Model load error: ${appStore.lastLoadError}`)
    }

    // Add errors from the error history (last 10)
    const errorHistory = appStore.recentErrors?.slice(0, 10) || []
    for (const err of errorHistory) {
      const timeAgo = Math.round((Date.now() - err.timestamp) / 1000 / 60)
      const timeStr = timeAgo < 1 ? 'just now' : `${timeAgo}m ago`
      recentErrorsList.push(`[${timeStr}] ${err.source}: ${err.message}`)
    }

    // Get recent jobs with full details (model settings, params) for context
    const recentJobs = (appStore.queue?.items || []).slice(0, 10).map(job => ({
      job_id: job.job_id,
      type: job.type,
      status: job.status,
      error: job.error,
      // Include model settings so agent knows what model was used
      model_settings: job.model_settings ? {
        model_name: job.model_settings.model_name,
        model_type: job.model_settings.model_type,
        model_architecture: job.model_settings.model_architecture,
        loaded_components: job.model_settings.loaded_components
      } : undefined,
      // Include generation params
      params: job.params ? {
        prompt: job.params.prompt,
        width: job.params.width,
        height: job.params.height,
        steps: job.params.steps,
        cfg_scale: job.params.cfg_scale,
        sampler: job.params.sampler,
        scheduler: job.params.scheduler,
        seed: job.params.seed
      } : undefined,
      outputs: job.outputs?.slice(0, 2) // Include first 2 output URLs
    }))

    // Add failed job errors from queue (if not already in error history)
    recentJobs
      .filter(j => j.status === 'failed' && j.error)
      .slice(0, 3)
      .forEach(job => {
        const errMsg = `Job ${job.type} failed: ${job.error}`
        if (!recentErrorsList.some(e => e.includes(job.error!))) {
          recentErrorsList.push(errMsg)
        }
      })

    // Gather available models (just names for context, not full details)
    const models = appStore.models
    const availableModels = {
      checkpoints: (models?.checkpoints || []).map(m => m.name),
      diffusion_models: (models?.diffusion_models || []).map(m => m.name),
      vae: (models?.vae || []).map(m => m.name),
      loras: (models?.loras || []).map(m => m.name),
      clip: (models?.clip || []).map(m => m.name),
      t5: (models?.t5 || []).map(m => m.name),
      controlnets: (models?.controlnets || []).map(m => m.name),
      esrgan: (models?.esrgan || []).map(m => m.name),
      llm: (models?.llm || []).map(m => m.name)
    }

    // Include last action results if any (for LLM feedback)
    const actionResults = lastActionResults.value
    // Clear after reading so it's only sent once
    lastActionResults.value = null

    // Get current architecture preset info (includes required components)
    const currentArch = appStore.modelArchitecture
    const archPreset = currentArch ? getArchitecturePreset(currentArch) : null

    return {
      current_view: currentView.value,
      settings: currentSettings,
      model_info: {
        loaded: appStore.modelLoaded,
        name: appStore.modelName,
        type: appStore.modelType,
        architecture: appStore.modelArchitecture,
        components: appStore.loadedComponents || {},
        // Include architecture requirements so LLM knows what's needed
        architecture_info: archPreset ? {
          name: archPreset.name,
          description: archPreset.description,
          requiredComponents: archPreset.requiredComponents,
          optionalComponents: archPreset.optionalComponents,
          generationDefaults: archPreset.generationDefaults
        } : undefined
      },
      upscaler_info: {
        loaded: appStore.upscalerLoaded,
        name: appStore.upscalerName
      },
      available_models: availableModels,
      // Include all architecture presets so LLM can help with any model
      architecture_presets: Object.fromEntries(
        Object.entries(architectures.value).map(([id, preset]) => [id, {
          name: preset.name,
          description: preset.description,
          requiredComponents: preset.requiredComponents,
          generationDefaults: preset.generationDefaults
        }])
      ),
      queue_stats: appStore.queueStats,
      recommended_settings: getRecommendedSettings() || undefined,
      recent_errors: recentErrorsList,
      recent_jobs: recentJobs,
      last_action_results: actionResults || undefined
    }
  }

  // Get current generation settings from sessionStorage
  function gatherCurrentSettings(): Record<string, unknown> {
    // Get the last active mode
    const lastMode = sessionStorage.getItem('generateSettings_lastMode') || 'txt2img'

    // Try to get settings for the current/last mode first
    const currentSettings = sessionStorage.getItem(`generateSettings_${lastMode}`)
    if (currentSettings) {
      try {
        const settings = JSON.parse(currentSettings)
        settings._mode = lastMode // Include the mode in context
        return settings
      } catch {
        // Fall through to try other modes
      }
    }

    // Fallback: try other modes
    const modes = ['txt2img', 'img2img', 'txt2vid']
    for (const mode of modes) {
      if (mode === lastMode) continue // Already tried
      const savedSettings = sessionStorage.getItem(`generateSettings_${mode}`)
      if (savedSettings) {
        try {
          const settings = JSON.parse(savedSettings)
          settings._mode = mode
          return settings
        } catch {
          continue
        }
      }
    }
    return { _mode: lastMode }
  }

  // Maximum retries when actions fail
  const MAX_ACTION_RETRIES = 2

  // Send a message to the assistant
  // isContinuation: true when this is an automated follow-up from job completion
  async function sendMessage(userMessage: string, isContinuation = false, retryCount = 0) {
    if (!enabled.value || isLoading.value || !userMessage.trim()) return

    // Ensure models are loaded for context (refresh if stale)
    if (!appStore.models || appStore.models.checkpoints.length === 0) {
      await appStore.fetchModels()
    }

    // Only add user message on first attempt (not retries)
    if (retryCount === 0) {
      messages.value.push({
        role: isContinuation ? 'system' : 'user',
        content: isContinuation ? `🔄 ${userMessage}` : userMessage,
        timestamp: Date.now()
      })
    }

    isLoading.value = true
    lastError.value = null

    try {
      const response = await api.assistantChat({
        message: userMessage,
        context: gatherContext()
      })

      if (response.success) {
        // Log what we received from the API
        console.log(`[AssistantStore] API response received:`, {
          hasMessage: !!response.message?.trim(),
          messageLength: response.message?.length,
          actionsCount: response.actions?.length || 0,
          actionTypes: response.actions?.map(a => a.type) || []
        })

        // Add assistant response if there's a message or actions
        const hasMessage = response.message && response.message.trim() !== ''
        const hasActions = response.actions && response.actions.length > 0

        if (hasMessage || hasActions) {
          messages.value.push({
            role: 'assistant',
            content: response.message || '',
            timestamp: Date.now(),
            actions: response.actions
          })
        }

        // Execute actions if any
        if (hasActions && response.actions) {
          const errors = await executeActions(response.actions)

          // Auto-retry if actions failed and we haven't exceeded retry limit
          if (errors.length > 0 && retryCount < MAX_ACTION_RETRIES) {
            // Small delay before retry
            await new Promise(resolve => setTimeout(resolve, 500))

            // Send retry message - the error feedback is already in lastActionResults
            const retryMessage = `Some actions failed. Please check last_action_results in context and try again with the correct action type.`
            isLoading.value = false // Reset so recursive call works
            await sendMessage(retryMessage, true, retryCount + 1)
            return
          }

          // Auto-continue if actions succeeded and produced results that need follow-up
          // This enables multi-step workflows like: search -> load -> apply settings
          const actionResults = lastActionResults.value
          if (actionResults && actionResults.successes.length > 0 && errors.length === 0) {
            // Check if any action produced data that needs follow-up (e.g., search results, job details)
            const needsFollowUp = actionResults.successes.some(s =>
              s.includes('search_jobs:') ||
              s.includes('get_job:') ||
              s.includes('Results:')
            )

            if (needsFollowUp && retryCount < MAX_ACTION_RETRIES) {
              // Small delay before continuation
              await new Promise(resolve => setTimeout(resolve, 300))

              // Send continuation message - the results are in lastActionResults
              const continueMessage = `Actions completed successfully. The results are in last_action_results. Please continue with the task.`
              isLoading.value = false // Reset so recursive call works
              await sendMessage(continueMessage, true, retryCount + 1)
              return
            }
          }
        }

        hasSuggestion.value = false
      } else {
        lastError.value = response.error || 'Unknown error'
        appStore.showToast(`Assistant error: ${lastError.value}`, 'error')
      }
    } catch (e) {
      const errorMsg = e instanceof Error ? e.message : 'Unknown error'
      lastError.value = errorMsg
      appStore.showToast(`Assistant error: ${errorMsg}`, 'error')
    } finally {
      isLoading.value = false
    }
  }

  // List of valid setting field names that can be changed
  const validSettingFields = new Set([
    'prompt', 'negative_prompt', 'negativePrompt',
    'width', 'height', 'steps', 'cfg_scale', 'cfgScale',
    'distilled_guidance', 'distilledGuidance', 'seed',
    'sampler', 'scheduler', 'batch_count', 'batchCount',
    'clip_skip', 'clipSkip', 'slg_scale', 'slgScale',
    'vae_tiling', 'vaeTiling', 'easycache',
    'video_frames', 'videoFrames', 'fps'
  ])

  // Execute actions requested by the assistant
  async function executeActions(actions: AssistantAction[]): Promise<string[]> {
    const errors: string[] = []
    const successes: string[] = []

    // Log all actions received for debugging
    console.log(`[AssistantStore] executeActions: Received ${actions.length} actions:`, actions.map(a => a.type))

    // Ensure models are loaded for load_model actions
    const hasLoadModelAction = actions.some(a => a.type === 'load_model' || a.type === 'set_component')
    if (hasLoadModelAction && (!appStore.models || appStore.models.checkpoints.length === 0)) {
      await appStore.fetchModels()
    }

    for (let i = 0; i < actions.length; i++) {
      const action = actions[i]
      console.log(`[AssistantStore] Executing action ${i + 1}/${actions.length}: ${action.type}`, action.parameters)
      try {
        switch (action.type) {
          case 'set_setting': {
            const result = executeSetSetting(action.parameters)
            if (!result.success) {
              errors.push(result.error || 'Unknown error')
            } else {
              successes.push(`set_setting: ${action.parameters.field} = ${action.parameters.value}`)
            }
            break
          }

          case 'apply_recommended_settings': {
            const recommended = getRecommendedSettings()
            if (recommended) {
              // Emit event to apply all recommended settings
              window.dispatchEvent(new CustomEvent('assistant-apply-recommended', {}))
              appStore.showToast('Applied recommended settings', 'success')
              successes.push('apply_recommended_settings: applied settings for ' + appStore.modelArchitecture)
            } else {
              errors.push('No recommended settings available for current model architecture')
            }
            break
          }

          case 'highlight_setting': {
            if (action.parameters.field) {
              // Emit event to highlight/scroll to setting
              window.dispatchEvent(new CustomEvent('assistant-highlight-setting', {
                detail: { field: action.parameters.field as string }
              }))
              successes.push(`highlight_setting: ${action.parameters.field}`)
            }
            break
          }

          case 'load_model': {
            // Check if a model is already loading
            if (appStore.modelLoading) {
              errors.push('Cannot load model: another model is currently loading. Please wait for it to complete.')
              break
            }

            const requestedName = action.parameters.model_name as string
            let modelType = (action.parameters.model_type as 'checkpoint' | 'diffusion') || 'checkpoint'

            console.log(`[Assistant] load_model action: requested="${requestedName}", type="${modelType}"`)
            console.log(`[Assistant] Models loaded: ${appStore.models ? 'yes' : 'no'}, checkpoints: ${appStore.models?.checkpoints?.length || 0}`)

            // Find the best matching model name
            let matchedName = findMatchingModel(requestedName, modelType)
            console.log(`[Assistant] Matched name: ${matchedName || 'NOT FOUND'}`)

            if (!matchedName) {
              // Try the other type if not found
              const altType = modelType === 'checkpoint' ? 'diffusion' : 'checkpoint'
              const altMatch = findMatchingModel(requestedName, altType)
              console.log(`[Assistant] Alt match (${altType}): ${altMatch || 'NOT FOUND'}`)

              if (altMatch) {
                matchedName = altMatch
                modelType = altType
              } else {
                errors.push(`Model not found: "${requestedName}". Check available_models in context for valid names.`)
                break
              }
            }

            // Build load params - include any components specified by the agent
            const loadParams: Record<string, unknown> = {
              model_name: matchedName,
              model_type: modelType
            }

            // Copy any additional params (vae, clip_l, t5xxl, llm, etc.) from action
            const componentKeys = ['vae', 'clip_l', 'clip_g', 't5xxl', 'llm', 'controlnet', 'taesd']
            for (const key of componentKeys) {
              if (action.parameters[key]) {
                // Try to find the matching component model name
                const componentCategory = key === 't5xxl' ? 't5' : key === 'clip_l' || key === 'clip_g' ? 'clip' : key as ModelCategory
                const componentMatch = findMatchingModel(action.parameters[key] as string, componentCategory)
                if (componentMatch) {
                  loadParams[key] = componentMatch
                } else {
                  loadParams[key] = action.parameters[key]
                }
              }
            }

            // Copy load options if provided
            if (action.parameters.options) {
              loadParams.options = action.parameters.options
            }

            console.log(`[Assistant] Loading model with params:`, loadParams)
            await api.loadModel(loadParams as unknown as LoadModelParams)
            appStore.showToast(`Loading model: ${matchedName.split('/').pop()}`, 'info')

            // BLOCKING: Wait for model to finish loading before continuing
            // This ensures the agent can proceed with its next actions after the model is ready
            console.log(`[Assistant] Waiting for model to finish loading...`)
            const maxWaitTime = 5 * 60 * 1000 // 5 minutes max
            const pollInterval = 1000 // Check every second
            const startTime = Date.now()

            await new Promise<void>((resolve, reject) => {
              const checkLoading = () => {
                // Refresh model status from app store
                if (!appStore.modelLoading) {
                  // Model finished loading (success or failure)
                  if (appStore.modelLoaded && appStore.modelName) {
                    console.log(`[Assistant] Model loaded successfully: ${appStore.modelName}`)
                    resolve()
                  } else {
                    const errorMsg = appStore.lastLoadError || 'Model failed to load'
                    console.log(`[Assistant] Model loading failed: ${errorMsg}`)
                    reject(new Error(errorMsg))
                  }
                  return
                }

                // Check timeout
                if (Date.now() - startTime > maxWaitTime) {
                  reject(new Error('Model loading timed out after 5 minutes'))
                  return
                }

                // Continue polling
                setTimeout(checkLoading, pollInterval)
              }

              // Start polling after a brief delay to let the loading state update
              setTimeout(checkLoading, 500)
            })

            successes.push(`load_model: loaded ${matchedName.split('/').pop()} (type: ${modelType})`)
            break
          }

          case 'unload_model':
            await api.unloadModel()
            appStore.showToast('Model unloaded', 'info')
            successes.push('unload_model: model unloaded successfully')
            break

          case 'set_component': {
            // Check if a model is already loading (set_component triggers a reload)
            if (appStore.modelLoading) {
              errors.push('Cannot set component: another model operation is currently in progress. Please wait for it to complete.')
              break
            }

            // Set a component model (VAE, CLIP, T5, ControlNet, TAESD)
            const componentType = action.parameters.component_type as string
            const componentName = action.parameters.model_name as string

            if (!componentType || !componentName) {
              errors.push('set_component requires component_type and model_name parameters')
              break
            }

            // Map component type to model category
            const typeMap: Record<string, ModelCategory> = {
              'vae': 'vae',
              'clip': 'clip',
              'clip_l': 'clip',
              'clip_g': 'clip',
              't5': 't5',
              't5xxl': 't5',
              'controlnet': 'controlnet',
              'taesd': 'taesd'
            }

            const category = typeMap[componentType.toLowerCase()]
            if (!category) {
              errors.push(`Unknown component type: ${componentType}. Valid types: vae, clip, clip_l, clip_g, t5, t5xxl, controlnet, taesd`)
              break
            }

            // Find matching model
            const matchedComponent = findMatchingModel(componentName, category)
            if (!matchedComponent) {
              errors.push(`Component model not found: "${componentName}" (type: ${componentType}). Check available_models.${category} for valid names.`)
              break
            }

            // Build load params with the component
            const componentParams: Record<string, string> = {}
            const paramKey = componentType.toLowerCase().replace('_', '')
            if (paramKey === 'clipl') componentParams.clip_l = matchedComponent
            else if (paramKey === 'clipg') componentParams.clip_g = matchedComponent
            else if (paramKey === 't5' || paramKey === 't5xxl') componentParams.t5xxl = matchedComponent
            else componentParams[componentType.toLowerCase()] = matchedComponent

            // Need to reload current model with new component
            if (!appStore.modelLoaded || !appStore.modelName) {
              errors.push('Cannot set component: no main model is loaded. Load a checkpoint or diffusion model first.')
              break
            }

            try {
              await api.loadModel({
                model_name: appStore.modelName,
                model_type: appStore.modelType as 'checkpoint' | 'diffusion',
                ...componentParams
              } as unknown as LoadModelParams)
              appStore.showToast(`Setting ${componentType}: ${matchedComponent.split('/').pop()}`, 'info')
              successes.push(`set_component: setting ${componentType} to ${matchedComponent.split('/').pop()}`)
            } catch (e) {
              throw new Error(`Failed to set ${componentType}: ${e instanceof Error ? e.message : 'Unknown error'}`)
            }
            break
          }

          case 'cancel_job':
            if (action.parameters.job_id) {
              await api.cancelJob(action.parameters.job_id as string)
              appStore.showToast('Job cancelled', 'info')
              successes.push(`cancel_job: cancelled job ${action.parameters.job_id}`)
            }
            break

          case 'navigate':
            if (action.parameters.view) {
              // Emit event for navigation (handled by App.vue or router)
              window.dispatchEvent(new CustomEvent('assistant-navigate', {
                detail: { view: action.parameters.view }
              }))
              successes.push(`navigate: navigated to ${action.parameters.view}`)
            }
            break

          case 'set_image': {
            // Set an image for img2img or upscaler
            const target = action.parameters.target as string
            const source = action.parameters.source as string
            const jobId = action.parameters.job_id as string

            if (!target || (!source && !jobId)) {
              errors.push('set_image requires target ("img2img" or "upscaler") and either source (URL) or job_id')
              break
            }

            if (!['img2img', 'upscaler'].includes(target)) {
              errors.push(`Invalid target: "${target}". Must be "img2img" or "upscaler"`)
              break
            }

            try {
              let imageBase64: string | null = null

              if (jobId) {
                // Get image from a completed job
                const job = await api.getJob(jobId)
                if (!job || job.status !== 'completed' || !job.outputs || job.outputs.length === 0) {
                  errors.push(`Job ${jobId} has no output images. Job status: ${job?.status || 'not found'}`)
                  break
                }
                // Fetch the first output image
                const imageUrl = job.outputs[0]
                imageBase64 = await fetchImageAsBase64(imageUrl)
              } else if (source) {
                // Fetch from URL
                imageBase64 = await fetchImageAsBase64(source)
              }

              if (!imageBase64) {
                errors.push('Failed to fetch image')
                break
              }

              // Emit event for the component to receive
              window.dispatchEvent(new CustomEvent('assistant-set-image', {
                detail: { target, imageBase64 }
              }))

              // Also navigate to the appropriate view
              const view = target === 'img2img' ? 'generate' : 'upscale'
              window.dispatchEvent(new CustomEvent('assistant-navigate', {
                detail: { view }
              }))

              appStore.showToast(`Image set for ${target}`, 'success')
              successes.push(`set_image: set image for ${target}`)
            } catch (e) {
              throw new Error(`Failed to set image: ${e instanceof Error ? e.message : 'Unknown error'}`)
            }
            break
          }

          case 'generate': {
            // Submit a generation job to the queue
            const genType = (action.parameters.type as string) || (gatherCurrentSettings()._mode as string) || 'txt2img'

            // Get current settings from sessionStorage as base (uses camelCase keys)
            const currentSettings = gatherCurrentSettings()

            // Merge with any provided parameters (action params override)
            // Note: sessionStorage uses camelCase, API uses snake_case
            const jobParams: GenerationParams = {
              prompt: (action.parameters.prompt as string) || (currentSettings.prompt as string) || '',
              negative_prompt: (action.parameters.negative_prompt as string) || (currentSettings.negativePrompt as string),
              width: (action.parameters.width as number) || (currentSettings.width as number) || 512,
              height: (action.parameters.height as number) || (currentSettings.height as number) || 512,
              steps: (action.parameters.steps as number) || (currentSettings.steps as number) || 20,
              cfg_scale: (action.parameters.cfg_scale as number) || (currentSettings.cfgScale as number) || 7,
              seed: (action.parameters.seed as number) ?? (currentSettings.seed as number) ?? -1,
              sampler: (action.parameters.sampler as string) || (currentSettings.sampler as string),
              scheduler: (action.parameters.scheduler as string) || (currentSettings.scheduler as string),
              batch_count: (action.parameters.batch_count as number) || (currentSettings.batchCount as number) || 1,
              distilled_guidance: (action.parameters.distilled_guidance as number) || (currentSettings.distilledGuidance as number),
              clip_skip: (action.parameters.clip_skip as number) || (currentSettings.clipSkip as number),
            }

            // Check for continuation option - if set, LLM will be triggered again when job completes
            const continueOnComplete = action.parameters.continue_on_complete as string | undefined

            // Validate prompt
            if (!jobParams.prompt || jobParams.prompt.trim() === '') {
              errors.push('Cannot generate: no prompt provided. Please set a prompt first.')
              break
            }

            // Validate model is loaded
            if (!appStore.modelLoaded) {
              errors.push('Cannot generate: no model is loaded. Please load a model first.')
              break
            }

            try {
              let response
              if (genType === 'img2img') {
                const img2imgParams: Img2ImgParams = {
                  ...jobParams,
                  init_image_base64: action.parameters.init_image_base64 as string || '',
                  strength: (action.parameters.strength as number) || 0.75
                }
                response = await api.img2img(img2imgParams)
              } else if (genType === 'txt2vid') {
                const txt2vidParams: Txt2VidParams = {
                  ...jobParams,
                  video_frames: (action.parameters.video_frames as number) || 33,
                  fps: (action.parameters.fps as number) || 16
                }
                response = await api.txt2vid(txt2vidParams)
              } else {
                response = await api.txt2img(jobParams)
              }

              // If continue_on_complete is set, register this job for continuation
              if (continueOnComplete && response.job_id) {
                pendingContinuations.value.set(response.job_id, {
                  jobId: response.job_id,
                  context: continueOnComplete,
                  startedAt: Date.now()
                })
                console.log(`[AssistantStore] Registered continuation for job ${response.job_id}: "${continueOnComplete}"`)
              }

              appStore.showToast(`Generation job queued: ${response.job_id}`, 'success')
              successes.push(`generate: queued ${genType} job (id: ${response.job_id})${continueOnComplete ? ' [will continue on complete]' : ''}`)
            } catch (e) {
              throw new Error(`Failed to queue ${genType} job: ${e instanceof Error ? e.message : 'Unknown error'}`)
            }
            break
          }

          case 'load_upscaler': {
            const modelName = action.parameters.model_name as string
            const tileSize = (action.parameters.tile_size as number) || 128

            if (!modelName) {
              errors.push('load_upscaler requires model_name parameter')
              break
            }

            // Find matching model in esrgan models
            const esrganModels = appStore.models?.esrgan || []
            const matchedModel = esrganModels.find(m =>
              m.name.toLowerCase().includes(modelName.toLowerCase()) ||
              modelName.toLowerCase().includes(m.name.toLowerCase())
            )

            const finalModelName = matchedModel?.name || modelName

            try {
              await api.loadUpscaler({
                model_name: finalModelName,
                tile_size: tileSize
              })
              appStore.showToast(`Upscaler loaded: ${finalModelName}`, 'success')
              successes.push(`load_upscaler: loaded ${finalModelName}`)
            } catch (e) {
              throw new Error(`Failed to load upscaler: ${e instanceof Error ? e.message : 'Unknown error'}`)
            }
            break
          }

          case 'unload_upscaler': {
            if (!appStore.upscalerLoaded) {
              errors.push('No upscaler is currently loaded')
              break
            }

            try {
              await api.unloadUpscaler()
              appStore.showToast('Upscaler unloaded', 'info')
              successes.push('unload_upscaler: upscaler unloaded successfully')
            } catch (e) {
              throw new Error(`Failed to unload upscaler: ${e instanceof Error ? e.message : 'Unknown error'}`)
            }
            break
          }

          case 'upscale': {
            // Submit an upscale job to the queue
            // Requires an upscaler to be loaded

            if (!appStore.upscalerLoaded) {
              errors.push('Cannot upscale: no upscaler is loaded. Please load an upscaler first using load_upscaler action.')
              break
            }

            // Get image from job_id or image_base64
            const jobId = action.parameters.job_id as string | undefined
            const imageBase64Param = action.parameters.image_base64 as string | undefined

            let imageBase64: string | null = null

            if (jobId) {
              // Get image from a completed job
              try {
                const job = await api.getJob(jobId)
                if (!job || job.status !== 'completed' || !job.outputs || job.outputs.length === 0) {
                  errors.push(`Job ${jobId} has no output images. Job status: ${job?.status || 'not found'}`)
                  break
                }
                // Fetch the first output image
                const imageUrl = `/output/${job.outputs[0]}`
                imageBase64 = await fetchImageAsBase64(imageUrl)
                if (!imageBase64) {
                  errors.push('Failed to fetch image from job output')
                  break
                }
              } catch (e) {
                throw new Error(`Failed to get job output: ${e instanceof Error ? e.message : 'Unknown error'}`)
              }
            } else if (imageBase64Param) {
              imageBase64 = imageBase64Param
            } else {
              errors.push('upscale requires either job_id (to use output from a completed job) or image_base64')
              break
            }

            // Build upscale params - ensure we send raw base64, not data URL
            let cleanBase64 = imageBase64
            if (cleanBase64.startsWith('data:')) {
              const commaIndex = cleanBase64.indexOf(',')
              if (commaIndex !== -1) {
                cleanBase64 = cleanBase64.substring(commaIndex + 1)
              }
            }

            const upscaleParams: UpscaleParams = {
              image_base64: cleanBase64,
              tile_size: (action.parameters.tile_size as number) || 128,
              repeats: (action.parameters.repeats as number) || 1
            }

            try {
              const response = await api.upscale(upscaleParams)
              appStore.showToast(`Upscale job queued: ${response.job_id}`, 'success')
              successes.push(`upscale: queued upscale job (id: ${response.job_id})`)
            } catch (e) {
              throw new Error(`Failed to queue upscale job: ${e instanceof Error ? e.message : 'Unknown error'}`)
            }
            break
          }

          case 'ask_user': {
            // Show a question modal to the user and wait for their answer
            const title = (action.parameters.title as string) || 'Question'
            const question = action.parameters.question as string
            const options = (action.parameters.options as string[]) || []
            const allowCustom = action.parameters.allow_custom !== false // Default to true

            if (!question) {
              errors.push('ask_user requires a question parameter')
              break
            }

            // Show the question modal and wait for answer
            const answer = await showQuestion({
              title,
              question,
              options,
              allowCustom
            })

            successes.push(`ask_user: received answer "${answer}"`)

            // IMPORTANT: Stop processing remaining actions in this batch.
            // The LLM needs to receive the user's answer to decide next steps.
            // Any remaining actions will be handled by the LLM in its next response.
            if (i < actions.length - 1) {
              console.log(`[AssistantStore] ask_user received answer, stopping further action processing. Remaining ${actions.length - i - 1} actions will be handled by LLM after receiving the answer.`)
            }

            // Send the answer back to the LLM as a continuation
            // Use a small delay to allow UI to update
            setTimeout(async () => {
              await sendMessage(`User answered: ${answer}`, true)
            }, 100)

            // Return early - remaining actions should be determined by LLM based on user's answer
            return errors
          }

          case 'get_job': {
            // Get detailed information about a specific job by ID
            const jobId = action.parameters.job_id as string

            if (!jobId) {
              errors.push('get_job requires job_id parameter')
              break
            }

            try {
              const job = await api.getJob(jobId)
              if (!job) {
                errors.push(`Job not found: ${jobId}`)
                break
              }

              // Format job details for the LLM
              const jobDetails = {
                job_id: job.job_id,
                type: job.type,
                status: job.status,
                error: job.error,
                progress: job.progress,
                model_settings: job.model_settings,
                params: job.params,
                outputs: job.outputs,
                created_at: job.created_at,
                completed_at: job.completed_at
              }

              successes.push(`get_job: Found job ${jobId} (status: ${job.status})`)

              // Send the job details back to the LLM so it can continue with its task
              // This is an info-gathering action, so we need to give the LLM the results
              const jobDetailsMsg = `Here are the details for job ${jobId}:\n\`\`\`json\n${JSON.stringify(jobDetails, null, 2)}\n\`\`\`\nPlease continue with the task using this information.`

              // Trigger a follow-up to let the LLM continue with the retrieved info
              setTimeout(async () => {
                await sendMessage(jobDetailsMsg, true)
              }, 100)

              // Return early - the follow-up message will continue the task
              return errors
            } catch (e) {
              throw new Error(`Failed to get job: ${e instanceof Error ? e.message : 'Unknown error'}`)
            }
          }

          case 'load_job_model': {
            // Load the exact model configuration from a completed job
            const jobId = action.parameters.job_id as string

            if (!jobId) {
              errors.push('load_job_model requires job_id parameter')
              break
            }

            try {
              const job = await api.getJob(jobId)
              if (!job) {
                errors.push(`Job not found: ${jobId}`)
                break
              }

              if (!job.model_settings) {
                errors.push(`Job ${jobId} has no model settings recorded`)
                break
              }

              const settings = job.model_settings
              if (!settings.model_name) {
                // Log what we have for debugging
                console.error(`[AssistantStore] Job ${jobId} has empty model_name. Settings:`, settings)
                errors.push(`Job ${jobId} has no model name in settings. The job may have been created before model tracking was implemented.`)
                break
              }

              // Check if another model operation is in progress
              if (appStore.modelLoading) {
                errors.push('Cannot load model: another model operation is currently in progress. Please wait for it to complete.')
                break
              }

              // Build load params with all components from the job (matching Queue.vue implementation)
              const loadParams: Record<string, unknown> = {
                model_name: settings.model_name,
                model_type: settings.model_type === 'diffusion' ? 'diffusion' : 'checkpoint'
              }

              // Add all loaded components from the job
              const components = settings.loaded_components
              if (components) {
                if (components.vae) loadParams.vae = components.vae
                if (components.clip_l) loadParams.clip_l = components.clip_l
                if (components.clip_g) loadParams.clip_g = components.clip_g
                if (components.t5xxl) loadParams.t5xxl = components.t5xxl
                if (components.llm) loadParams.llm = components.llm
                if (components.llm_vision) loadParams.llm_vision = components.llm_vision
                if (components.controlnet) loadParams.controlnet = components.controlnet
              }

              // Add load options if stored (critical for correct VRAM usage and behavior)
              if (settings.load_options) {
                loadParams.options = settings.load_options
              }

              console.log(`[AssistantStore] Loading model from job ${jobId}:`, loadParams)
              await api.loadModel(loadParams as unknown as LoadModelParams)

              const shortName = settings.model_name.split('/').pop() || settings.model_name
              appStore.showToast(`Loading model from job: ${shortName}`, 'info')

              // BLOCKING: Wait for model to finish loading before continuing
              console.log(`[Assistant] Waiting for job model to finish loading...`)
              const maxWaitTime = 5 * 60 * 1000 // 5 minutes max
              const pollInterval = 1000
              const startTime = Date.now()

              await new Promise<void>((resolve, reject) => {
                const checkLoading = () => {
                  if (!appStore.modelLoading) {
                    if (appStore.modelLoaded && appStore.modelName) {
                      console.log(`[Assistant] Job model loaded successfully: ${appStore.modelName}`)
                      resolve()
                    } else {
                      const errorMsg = appStore.lastLoadError || 'Model failed to load'
                      console.log(`[Assistant] Job model loading failed: ${errorMsg}`)
                      reject(new Error(errorMsg))
                    }
                    return
                  }
                  if (Date.now() - startTime > maxWaitTime) {
                    reject(new Error('Model loading timed out after 5 minutes'))
                    return
                  }
                  setTimeout(checkLoading, pollInterval)
                }
                setTimeout(checkLoading, 500)
              })

              // Build component summary for feedback
              const componentList: string[] = []
              if (components?.vae) componentList.push(`VAE: ${components.vae.split('/').pop()}`)
              if (components?.clip_l) componentList.push(`CLIP-L: ${components.clip_l.split('/').pop()}`)
              if (components?.clip_g) componentList.push(`CLIP-G: ${components.clip_g.split('/').pop()}`)
              if (components?.t5xxl) componentList.push(`T5: ${components.t5xxl.split('/').pop()}`)
              if (components?.llm) componentList.push(`LLM: ${components.llm.split('/').pop()}`)
              if (components?.controlnet) componentList.push(`ControlNet: ${components.controlnet.split('/').pop()}`)

              const componentSummary = componentList.length > 0 ? ` with ${componentList.join(', ')}` : ''
              successes.push(`load_job_model: Loaded ${shortName} (${settings.model_architecture || settings.model_type})${componentSummary}`)
            } catch (e) {
              throw new Error(`Failed to load job model: ${e instanceof Error ? e.message : 'Unknown error'}`)
            }
            break
          }

          case 'search_jobs': {
            // Search jobs by prompt text, model architecture, or other criteria
            const searchPrompt = action.parameters.prompt as string
            const searchStatus = action.parameters.status as string
            const searchType = action.parameters.type as string
            const searchModel = action.parameters.model as string
            const searchArchitecture = action.parameters.architecture as string
            const limit = (action.parameters.limit as number) || 10

            const queue = appStore.queue?.items || []

            // Filter jobs based on search criteria
            let results = queue.filter(job => {
              let match = true

              // Filter by prompt text (case-insensitive partial match)
              if (searchPrompt) {
                const jobPrompt = ((job.params?.prompt as string) || '').toLowerCase()
                match = match && jobPrompt.includes(searchPrompt.toLowerCase())
              }

              // Filter by status
              if (searchStatus) {
                match = match && job.status === searchStatus
              }

              // Filter by type
              if (searchType) {
                match = match && job.type === searchType
              }

              // Filter by model name (case-insensitive partial match)
              if (searchModel) {
                const jobModel = ((job.model_settings?.model_name as string) || '').toLowerCase()
                match = match && jobModel.includes(searchModel.toLowerCase())
              }

              // Filter by model architecture (case-insensitive partial match)
              if (searchArchitecture) {
                const jobArch = ((job.model_settings?.model_architecture as string) || '').toLowerCase()
                match = match && jobArch.includes(searchArchitecture.toLowerCase())
              }

              return match
            })

            // Limit results
            results = results.slice(0, limit)

            // Format results for the LLM - include full details for each job
            const formattedResults = results.map(job => {
              const promptStr = (job.params?.prompt as string) || ''
              return {
                job_id: job.job_id,
                type: job.type,
                status: job.status,
                prompt: promptStr.substring(0, 200) + (promptStr.length > 200 ? '...' : ''),
                negative_prompt: ((job.params?.negative_prompt as string) || '').substring(0, 100),
                model_settings: job.model_settings,
                params: {
                  width: job.params?.width,
                  height: job.params?.height,
                  steps: job.params?.steps,
                  cfg_scale: job.params?.cfg_scale,
                  sampler: job.params?.sampler,
                  seed: job.params?.seed
                },
                error: job.error,
                outputs: job.outputs?.slice(0, 2)
              }
            })

            // Add detailed results to successes so LLM can continue with the data
            successes.push(`search_jobs: Found ${results.length} matching jobs. Results:\n${JSON.stringify(formattedResults, null, 2)}`)
            break
          }

          case 'download_model': {
            // Download a model from URL, CivitAI, or HuggingFace
            const modelType = action.parameters.model_type as string

            if (!modelType) {
              errors.push('download_model requires model_type parameter (checkpoint, vae, lora, clip, t5, embedding, controlnet, llm, esrgan, diffusion, taesd)')
              break
            }

            // Validate model type
            const validModelTypes = ['checkpoint', 'diffusion', 'vae', 'lora', 'clip', 't5', 'embedding', 'controlnet', 'llm', 'esrgan', 'taesd']
            if (!validModelTypes.includes(modelType)) {
              errors.push(`Invalid model_type: "${modelType}". Valid types: ${validModelTypes.join(', ')}`)
              break
            }

            // Build download params
            const downloadParams: Record<string, unknown> = {
              model_type: modelType
            }

            // Add source-specific parameters
            if (action.parameters.url) {
              downloadParams.url = action.parameters.url
            }
            if (action.parameters.model_id) {
              downloadParams.model_id = action.parameters.model_id
            }
            if (action.parameters.repo_id) {
              downloadParams.repo_id = action.parameters.repo_id
            }
            if (action.parameters.filename) {
              downloadParams.filename = action.parameters.filename
            }
            if (action.parameters.subfolder) {
              downloadParams.subfolder = action.parameters.subfolder
            }
            if (action.parameters.revision) {
              downloadParams.revision = action.parameters.revision
            }
            if (action.parameters.source) {
              downloadParams.source = action.parameters.source
            }

            // Validate we have at least one source identifier
            if (!downloadParams.url && !downloadParams.model_id && !downloadParams.repo_id) {
              errors.push('download_model requires url, model_id (CivitAI), or repo_id+filename (HuggingFace)')
              break
            }

            // HuggingFace requires filename
            if (downloadParams.repo_id && !downloadParams.filename) {
              errors.push('download_model from HuggingFace requires both repo_id and filename')
              break
            }

            try {
              const response = await api.downloadModel(downloadParams as unknown as Parameters<typeof api.downloadModel>[0])
              appStore.showToast(`Download started: ${downloadParams.model_id || downloadParams.repo_id || 'URL'}`, 'success')
              successes.push(`download_model: download job queued (id: ${response.download_job_id}, hash: ${response.hash_job_id})`)

              // Navigate to queue to show download progress
              window.dispatchEvent(new CustomEvent('assistant-navigate', {
                detail: { view: 'queue' }
              }))
            } catch (e) {
              throw new Error(`Failed to start download: ${e instanceof Error ? e.message : 'Unknown error'}`)
            }
            break
          }

          case 'convert_model': {
            // Convert a model to GGUF format with specified quantization
            const modelName = action.parameters.model_name as string
            const inputPath = action.parameters.input_path as string
            const outputType = action.parameters.output_type as string
            const modelType = (action.parameters.model_type as string) || 'checkpoint'

            if (!outputType) {
              errors.push('convert_model requires output_type parameter (quantization type like q4_0, q8_0, f16, bf16)')
              break
            }

            // Fetch valid quantization types from API
            let validOutputTypes: string[] = []
            try {
              const options = await api.getOptions()
              validOutputTypes = options.quantization_types.map(qt => qt.id.toLowerCase())
            } catch (e) {
              console.warn('[AssistantStore] Failed to fetch quantization types, using fallback')
              // Fallback list if API fails
              validOutputTypes = ['f32', 'f16', 'bf16', 'q8_0', 'q5_0', 'q5_1', 'q4_0', 'q4_1', 'q4_k', 'q5_k', 'q6_k', 'q2_k', 'q3_k']
            }

            if (!validOutputTypes.includes(outputType.toLowerCase())) {
              errors.push(`Invalid output_type: "${outputType}". Valid types: ${validOutputTypes.join(', ')}`)
              break
            }

            // Need either model_name or input_path
            if (!modelName && !inputPath) {
              errors.push('convert_model requires either model_name (relative name from available_models) or input_path (full path)')
              break
            }

            let finalInputPath = inputPath
            if (!finalInputPath && modelName) {
              // Get model paths config to build full path
              try {
                const pathsConfig = await api.getModelPaths()

                // Map model type to path config key
                const typeToPathKey: Record<string, keyof typeof pathsConfig> = {
                  'checkpoint': 'models',
                  'diffusion': 'diffusion',
                  'vae': 'vae',
                  'lora': 'loras',
                  'clip': 'clip',
                  't5': 't5',
                  'controlnet': 'controlnet',
                  'llm': 'llm',
                  'esrgan': 'esrgan',
                  'taesd': 'taesd'
                }

                const basePath = pathsConfig[typeToPathKey[modelType] || 'models']
                if (!basePath) {
                  errors.push(`Unknown model_type: ${modelType}`)
                  break
                }

                // Build full path
                finalInputPath = `${basePath}/${modelName}`
              } catch (e) {
                throw new Error(`Failed to get model paths: ${e instanceof Error ? e.message : 'Unknown error'}`)
              }
            }

            // Build convert params
            const convertParams: ConvertParams = {
              input_path: finalInputPath,
              output_type: outputType.toLowerCase()
            }

            // Optional output path
            if (action.parameters.output_path) {
              convertParams.output_path = action.parameters.output_path as string
            }

            // Optional VAE path (for baking VAE into the model)
            if (action.parameters.vae_path) {
              convertParams.vae_path = action.parameters.vae_path as string
            }

            // Optional tensor type rules
            if (action.parameters.tensor_type_rules) {
              convertParams.tensor_type_rules = action.parameters.tensor_type_rules as string
            }

            try {
              const response = await api.convert(convertParams)
              appStore.showToast(`Conversion started: ${outputType}`, 'success')
              successes.push(`convert_model: conversion job queued (id: ${response.job_id}, output: ${response.output_path})`)

              // Navigate to queue to show progress
              window.dispatchEvent(new CustomEvent('assistant-navigate', {
                detail: { view: 'queue' }
              }))
            } catch (e) {
              throw new Error(`Failed to start conversion: ${e instanceof Error ? e.message : 'Unknown error'}`)
            }
            break
          }
        }
      } catch (e) {
        const errorMsg = e instanceof Error ? e.message : 'Unknown error'
        errors.push(`Action ${action.type} failed: ${errorMsg}`)
        console.error(`[AssistantStore] Action ${action.type} threw error:`, e)
      }
      console.log(`[AssistantStore] Action ${i + 1}/${actions.length} completed. Successes: ${successes.length}, Errors: ${errors.length}`)
    }

    console.log(`[AssistantStore] All ${actions.length} actions processed. Final - Successes: ${successes.length}, Errors: ${errors.length}`)

    // Store action results for feedback in next request
    if (successes.length > 0 || errors.length > 0) {
      lastActionResults.value = { successes, errors }
    }

    // If there were errors, add a system message to inform the user in UI
    if (errors.length > 0) {
      messages.value.push({
        role: 'system',
        content: `⚠️ Some actions failed:\n${errors.map(e => `• ${e}`).join('\n')}`,
        timestamp: Date.now()
      })
    }

    return errors
  }

  // Map of field names to their canonical storage key names
  // Map field names to the camelCase keys used by Generate.vue in sessionStorage
  const fieldToStorageKey: Record<string, string> = {
    prompt: 'prompt',
    negative_prompt: 'negativePrompt',
    negativePrompt: 'negativePrompt',
    width: 'width',
    height: 'height',
    steps: 'steps',
    cfg_scale: 'cfgScale',
    cfgScale: 'cfgScale',
    distilled_guidance: 'distilledGuidance',
    distilledGuidance: 'distilledGuidance',
    seed: 'seed',
    sampler: 'sampler',
    scheduler: 'scheduler',
    batch_count: 'batchCount',
    batchCount: 'batchCount',
    clip_skip: 'clipSkip',
    clipSkip: 'clipSkip',
    slg_scale: 'slgScale',
    slgScale: 'slgScale',
    vae_tiling: 'vaeTiling',
    vaeTiling: 'vaeTiling',
    easycache: 'easycache',
    video_frames: 'videoFrames',
    videoFrames: 'videoFrames',
    fps: 'fps'
  }

  // Execute set_setting action with validation
  function executeSetSetting(params: Record<string, unknown>): { success: boolean; error?: string } {
    if (!params.field || params.value === undefined) {
      return { success: false, error: 'Missing field or value parameter' }
    }

    const field = params.field as string

    // Validate that the field exists
    if (!validSettingFields.has(field)) {
      return {
        success: false,
        error: `Unknown setting field: "${field}". Valid fields are: ${Array.from(validSettingFields).join(', ')}`
      }
    }

    // Get the canonical storage key for this field
    const storageKey = fieldToStorageKey[field] || field

    // Update sessionStorage directly so settings persist even when Generate.vue is not mounted
    const lastMode = sessionStorage.getItem('generateSettings_lastMode') || 'txt2img'

    // Update settings for the current/last mode
    const settingsKey = `generateSettings_${lastMode}`
    try {
      const currentSettings = JSON.parse(sessionStorage.getItem(settingsKey) || '{}')
      currentSettings[storageKey] = params.value
      sessionStorage.setItem(settingsKey, JSON.stringify(currentSettings))
    } catch (e) {
      console.error('[AssistantStore] Failed to update sessionStorage:', e)
    }

    // Also emit event for Generate.vue to pick up (if it's mounted, it will update its local refs)
    window.dispatchEvent(new CustomEvent('assistant-setting-change', {
      detail: { field, value: params.value }
    }))

    appStore.showToast(`Setting ${field} updated to ${params.value}`, 'info')
    return { success: true }
  }

  // Set current view (called by components when route changes)
  function setCurrentView(view: string) {
    currentView.value = view
  }

  // Toggle open/minimize
  function toggleOpen() {
    if (isMinimized.value) {
      isMinimized.value = false
      isOpen.value = true
    } else {
      isMinimized.value = true
    }
  }

  // Open the chat
  function open() {
    isMinimized.value = false
    isOpen.value = true
  }

  // Close/minimize
  function close() {
    isMinimized.value = true
  }

  // Clear conversation history
  async function clearHistory() {
    try {
      await api.clearAssistantHistory()
      messages.value = []
      appStore.showToast('Conversation cleared', 'info')
    } catch (e) {
      console.error('[AssistantStore] Failed to clear history:', e)
      appStore.showToast('Failed to clear conversation', 'error')
    }
  }

  // Update settings
  async function updateSettings(newSettings: Partial<AssistantSettings>) {
    try {
      const response = await api.updateAssistantSettings(newSettings)
      if (response.success) {
        settings.value = response.settings
        enabled.value = response.settings.enabled
        proactiveSuggestions.value = response.settings.proactive_suggestions
        appStore.showToast('Settings updated', 'success')
        return true
      }
      return false
    } catch (e) {
      console.error('[AssistantStore] Failed to update settings:', e)
      appStore.showToast('Failed to update settings', 'error')
      return false
    }
  }

  // Refresh status (called after settings change)
  async function refreshStatus() {
    try {
      const status = await api.getAssistantStatus()
      enabled.value = status.enabled
      connected.value = status.connected
      proactiveSuggestions.value = status.proactive_suggestions
    } catch (e) {
      console.error('[AssistantStore] Failed to refresh status:', e)
    }
  }

  // Show a question modal and wait for user's answer
  interface QuestionParams {
    title: string
    question: string
    options: string[]
    allowCustom: boolean
  }

  function showQuestion(params: QuestionParams): Promise<string> {
    return new Promise((resolve) => {
      // Store the resolve function to call when user answers
      questionResolve = resolve

      // Set the pending question
      pendingQuestion.value = {
        id: `q_${Date.now()}`,
        title: params.title,
        question: params.question,
        options: params.options,
        allowCustom: params.allowCustom,
        timestamp: Date.now()
      }

      // Ensure modal is visible
      questionMinimized.value = false
    })
  }

  // Answer the pending question
  function answerQuestion(answer: string) {
    if (questionResolve) {
      questionResolve(answer)
      questionResolve = null
    }
    pendingQuestion.value = null
    questionMinimized.value = false
  }

  // Dismiss the question (used if modal is closed without answering)
  function dismissQuestion() {
    if (questionResolve) {
      questionResolve('[User dismissed the question]')
      questionResolve = null
    }
    pendingQuestion.value = null
    questionMinimized.value = false
  }

  // Toggle question modal minimized state
  function toggleQuestionMinimized() {
    questionMinimized.value = !questionMinimized.value
  }

  return {
    // State
    enabled,
    connected,
    isOpen,
    isMinimized,
    isLoading,
    messages,
    hasSuggestion,
    suggestionText,
    currentView,
    proactiveSuggestions,
    settings,
    lastError,

    // Question modal state
    pendingQuestion,
    questionMinimized,

    // Computed
    messageCount,
    hasMessages,

    // Actions
    initialize,
    loadHistory,
    loadSettings,
    gatherContext,
    sendMessage,
    executeActions,
    setCurrentView,
    toggleOpen,
    open,
    close,
    clearHistory,
    updateSettings,
    refreshStatus,
    cleanup,

    // Question actions
    answerQuestion,
    dismissQuestion,
    toggleQuestionMinimized
  }
})
