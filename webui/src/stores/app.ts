import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import { api, type HealthResponse, type ModelsResponse, type QueueResponse, type OptionsResponse, type Job, type QueueFilters, type GenerationDefaults, type UIPreferences } from '../api/client'
import {
  wsService,
  type ConnectionState,
  type JobAddedData,
  type JobStatusChangedData,
  type JobProgressData,
  type JobPreviewData,
  type JobCancelledData,
  type ModelLoadingProgressData,
  type ModelLoadedData,
  type ModelUnloadedData,
  type UpscalerLoadedData,
  type UpscalerUnloadedData
} from '../services/websocket'
import { notificationService } from '../services/notifications'

const DEFAULT_TITLE = 'SD.cpp WebUI'

// Polling interval when WebSocket is not connected (fallback only)
const POLL_INTERVAL_FALLBACK = 2000

export const useAppStore = defineStore('app', () => {
  // State
  const health = ref<HealthResponse | null>(null)
  const models = ref<ModelsResponse | null>(null)
  const queue = ref<QueueResponse | null>(null)
  const options = ref<OptionsResponse | null>(null)
  const connected = ref(false)
  const loading = ref(false)
  const error = ref<string | null>(null)
  const isInitialLoading = ref(true)

  // Queue filters - stored here so all fetchQueue calls use them
  const queueFilters = ref<QueueFilters | undefined>(undefined)

  // WebSocket state
  const wsConnected = ref(false)
  const wsState = ref<ConnectionState>('disconnected')
  const lastDisconnectTime = ref<number | null>(null)

  // API availability - derived from WebSocket state
  const apiAvailable = computed(() => {
    return wsState.value === 'connected' || wsState.value === 'connecting' || wsState.value === 'reconnecting'
  })

  // Current preview during generation
  const currentPreview = ref<{ jobId: string; image: string; step: number } | null>(null)

  // Track previous job states to detect changes (limited to prevent memory leaks)
  const previousJobStates = ref<Map<string, string>>(new Map())
  const MAX_TRACKED_JOB_STATES = 200  // Limit tracked job states to prevent memory leaks

  // Track model loading state for toasts
  const wasModelLoading = ref(false)
  const previousModelName = ref<string | null>(null)

  // Toast notifications (limited to prevent memory leaks)
  const toasts = ref<Array<{ id: number; message: string; type: 'success' | 'error' | 'warning' | 'info' }>>([])
  let toastId = 0
  const MAX_TOASTS = 10  // Limit concurrent toasts
  const toastTimeouts = ref<Map<number, ReturnType<typeof setTimeout>>>(new Map())

  // Preview cleanup timer
  let previewCleanupTimer: ReturnType<typeof setTimeout> | null = null
  const PREVIEW_CLEANUP_DELAY = 30000  // Clear stale preview after 30 seconds of no updates

  // Recent errors history for assistant context (limited to prevent memory leaks)
  const recentErrors = ref<Array<{ timestamp: number; message: string; source: string }>>([])
  const MAX_RECENT_ERRORS = 20

  // Upscale input image - persists across navigation
  const upscaleInputImage = ref<string | null>(null)

  function setUpscaleInputImage(imageDataUrl: string | null) {
    upscaleInputImage.value = imageDataUrl
  }

  function clearUpscaleInputImage() {
    upscaleInputImage.value = null
  }

  // Settings state
  const generationDefaults = ref<GenerationDefaults | null>(null)
  const uiPreferences = ref<UIPreferences | null>(null)

  async function fetchGenerationDefaults(): Promise<GenerationDefaults> {
    try {
      const defaults = await api.getGenerationDefaults()
      generationDefaults.value = {
        txt2img: (defaults as any).txt2img || {},
        img2img: (defaults as any).img2img || {},
        txt2vid: (defaults as any).txt2vid || {}
      }
      return generationDefaults.value
    } catch (e) {
      console.error('Failed to fetch generation defaults:', e)
      throw e
    }
  }

  async function updateGenerationDefaults(defaults: Partial<GenerationDefaults>): Promise<void> {
    try {
      const result = await api.updateGenerationDefaults(defaults)
      generationDefaults.value = {
        txt2img: (result.settings as any).txt2img || {},
        img2img: (result.settings as any).img2img || {},
        txt2vid: (result.settings as any).txt2vid || {}
      }
    } catch (e) {
      console.error('Failed to update generation defaults:', e)
      throw e
    }
  }

  async function fetchUIPreferences(): Promise<UIPreferences> {
    try {
      const prefs = await api.getUIPreferences()
      uiPreferences.value = prefs
      return prefs
    } catch (e) {
      console.error('Failed to fetch UI preferences:', e)
      throw e
    }
  }

  async function updateUIPreferences(prefs: Partial<UIPreferences>): Promise<void> {
    try {
      const result = await api.updateUIPreferences(prefs)
      uiPreferences.value = result.settings
    } catch (e) {
      console.error('Failed to update UI preferences:', e)
      throw e
    }
  }

  async function resetAllSettings(): Promise<void> {
    try {
      await api.resetSettings()
      await Promise.all([fetchGenerationDefaults(), fetchUIPreferences()])
      showToast('Settings reset to defaults', 'success')
    } catch (e) {
      console.error('Failed to reset settings:', e)
      throw e
    }
  }

  // Desktop notifications state
  const desktopNotificationsEnabled = ref(notificationService.isEnabled())
  const desktopNotificationsPermission = computed(() => notificationService.getPermission())

  // Toggle desktop notifications
  async function toggleDesktopNotifications(): Promise<boolean> {
    const enabled = await notificationService.toggle()
    desktopNotificationsEnabled.value = enabled
    return enabled
  }

  // Add error to history
  function addRecentError(message: string, source: string) {
    recentErrors.value.unshift({
      timestamp: Date.now(),
      message,
      source
    })
    // Limit size
    if (recentErrors.value.length > MAX_RECENT_ERRORS) {
      recentErrors.value = recentErrors.value.slice(0, MAX_RECENT_ERRORS)
    }
    // Emit event for assistant proactive suggestions
    window.dispatchEvent(new CustomEvent('app-error-occurred', {
      detail: { message, source }
    }))
  }

  // Computed
  const modelLoaded = computed(() => health.value?.model_loaded ?? false)
  const modelLoading = computed(() => health.value?.model_loading ?? false)
  const loadingModelName = computed(() => health.value?.loading_model_name ?? null)
  const loadingStep = computed(() => health.value?.loading_step ?? null)
  const loadingTotalSteps = computed(() => health.value?.loading_total_steps ?? null)
  const lastLoadError = computed(() => health.value?.last_error ?? null)
  const modelName = computed(() => health.value?.model_name ?? null)
  const modelType = computed(() => health.value?.model_type ?? null)
  const modelArchitecture = computed(() => health.value?.model_architecture ?? null)
  const loadedComponents = computed(() => health.value?.loaded_components ?? null)
  const upscalerLoaded = computed(() => health.value?.upscaler_loaded ?? false)
  const upscalerName = computed(() => health.value?.upscaler_name ?? null)

  const queueStats = computed(() => ({
    pending: queue.value?.pending_count ?? 0,
    processing: queue.value?.processing_count ?? 0,
    completed: queue.value?.completed_count ?? 0,
    failed: queue.value?.failed_count ?? 0,
    total: queue.value?.total_count ?? 0
  }))

  // Options (samplers, schedulers, quantization types)
  const samplers = computed(() => options.value?.samplers ?? [])
  const schedulers = computed(() => options.value?.schedulers ?? [])
  const quantizationTypes = computed(() => options.value?.quantization_types ?? [])

  // Actions
  async function fetchHealth() {
    try {
      const newHealth = await api.getHealth()

      // Detect model loading state changes
      detectModelLoadingChanges(newHealth)

      health.value = newHealth
      connected.value = true
      error.value = null
      // Update document title with model loading progress
      updateDocumentTitle()
    } catch (e) {
      connected.value = false
      error.value = e instanceof Error ? e.message : 'Connection failed'
      updateDocumentTitle()
    }
  }

  function detectModelLoadingChanges(newHealth: HealthResponse) {
    const isLoading = newHealth.model_loading
    const modelName = newHealth.model_name
    const loadError = newHealth.last_error

    // Only show toasts from polling when WebSocket is NOT connected
    // When WS is connected, the WS event handlers show toasts in real-time
    // This prevents duplicate toasts
    const shouldShowToasts = !wsConnected.value

    // Model started loading
    if (isLoading && !wasModelLoading.value && shouldShowToasts) {
      const loadingName = newHealth.loading_model_name ?? 'model'
      const shortName = loadingName.split('/').pop() ?? loadingName
      showToast(`Loading model: ${shortName}`, 'info')
    }

    // Model finished loading (was loading, now not loading)
    if (!isLoading && wasModelLoading.value) {
      if (loadError) {
        if (shouldShowToasts) {
          showToast(`Model load failed: ${loadError}`, 'error')
        }
        // Always log errors regardless of WS state
        addRecentError(loadError, 'model_load')
      } else if (modelName && shouldShowToasts) {
        const shortName = modelName.split('/').pop() ?? modelName
        showToast(`Model loaded: ${shortName}`, 'success')
      }
    }

    // Model unloaded
    if (!modelName && previousModelName.value && !isLoading && shouldShowToasts) {
      showToast('Model unloaded', 'info')
    }

    wasModelLoading.value = isLoading
    previousModelName.value = modelName
  }

  async function fetchModels(filters?: { type?: string; extension?: string; search?: string }) {
    loading.value = true
    try {
      models.value = await api.getModels(filters)
      error.value = null
    } catch (e) {
      error.value = e instanceof Error ? e.message : 'Failed to fetch models'
    } finally {
      loading.value = false
    }
  }

  async function fetchQueue(filters?: QueueFilters) {
    try {
      // If filters are explicitly passed, update the stored filters
      // Otherwise use the stored filters (this allows polling and WS to use current filters)
      if (filters !== undefined) {
        queueFilters.value = filters
      }

      const newQueue = await api.getQueue(queueFilters.value)

      // Detect status changes and show toasts
      if (newQueue.items) {
        detectQueueChanges(newQueue.items)
      }

      queue.value = newQueue
      error.value = null

      // Update document title with progress
      updateDocumentTitle()
    } catch (e) {
      error.value = e instanceof Error ? e.message : 'Failed to fetch queue'
    }
  }

  function setQueueFilters(filters?: QueueFilters) {
    queueFilters.value = filters
  }

  function detectQueueChanges(jobs: Job[]) {
    const currentJobIds = new Set(jobs.map(j => j.job_id))

    // Only show toasts from polling when WebSocket is NOT connected
    // When WS is connected, the WS event handlers show toasts in real-time
    // This prevents duplicate toasts
    const shouldShowToasts = !wsConnected.value

    for (const job of jobs) {
      const prevStatus = previousJobStates.value.get(job.job_id)

      if (prevStatus === undefined) {
        // New job added (only show if pending, to avoid toasts on page load)
        if (shouldShowToasts && previousJobStates.value.size > 0 && job.status === 'pending') {
          showToast(`Job queued: ${getJobTypeLabel(job.type)}`, 'info')
        }
      } else if (prevStatus !== job.status) {
        // Status changed - only show toast if not using WebSocket
        if (shouldShowToasts) {
          switch (job.status) {
            case 'processing':
              showToast(`Job started: ${getJobTypeLabel(job.type)}`, 'info')
              break
            case 'completed':
              showToast(`Job completed: ${getJobTypeLabel(job.type)}`, 'success')
              break
            case 'failed':
              showToast(`Job failed: ${getJobTypeLabel(job.type)}${job.error ? ' - ' + job.error : ''}`, 'error')
              addRecentError(`${getJobTypeLabel(job.type)} failed: ${job.error || 'Unknown error'}`, 'job_failed')
              break
            case 'cancelled':
              showToast(`Job cancelled: ${getJobTypeLabel(job.type)}`, 'warning')
              break
          }
        } else if (job.status === 'failed') {
          // Always add to error history even with WS (error logging is separate from toasts)
          addRecentError(`${getJobTypeLabel(job.type)} failed: ${job.error || 'Unknown error'}`, 'job_failed')
        }
      }
      // Update the state
      previousJobStates.value.set(job.job_id, job.status)
    }

    // Clean up: Remove job IDs that are no longer in the current queue
    // This prevents the map from growing indefinitely
    for (const jobId of previousJobStates.value.keys()) {
      if (!currentJobIds.has(jobId)) {
        previousJobStates.value.delete(jobId)
      }
    }

    // Safety limit: If the map somehow grows too large, trim it
    if (previousJobStates.value.size > MAX_TRACKED_JOB_STATES) {
      const entries = Array.from(previousJobStates.value.entries())
      previousJobStates.value = new Map(entries.slice(-MAX_TRACKED_JOB_STATES))
    }
  }

  function getJobTypeLabel(type: string): string {
    switch (type) {
      case 'txt2img': return 'Text to Image'
      case 'img2img': return 'Image to Image'
      case 'txt2vid': return 'Text to Video'
      case 'upscale': return 'Upscale'
      case 'convert': return 'Model Conversion'
      default: return type
    }
  }

  function updateDocumentTitle() {
    // Priority 1: Model loading progress
    if (health.value?.model_loading) {
      const step = health.value.loading_step ?? 0
      const total = health.value.loading_total_steps ?? 0
      const modelName = health.value.loading_model_name ?? 'model'
      // Extract just the filename from the path
      const shortName = modelName.split('/').pop()?.substring(0, 20) ?? modelName
      if (total > 0) {
        const percent = Math.round((step / total) * 100)
        document.title = `[${percent}%] Loading ${shortName}... - ${DEFAULT_TITLE}`
      } else {
        document.title = `Loading ${shortName}... - ${DEFAULT_TITLE}`
      }
      return
    }

    // Priority 2: Job processing progress
    const processingJob = queue.value?.items?.find(j => j.status === 'processing')
    if (processingJob) {
      const step = processingJob.progress?.step ?? 0
      const total = processingJob.progress?.total_steps ?? 0
      const typeLabel = getJobTypeLabel(processingJob.type)
      if (total > 0) {
        const percent = Math.round((step / total) * 100)
        document.title = `[${percent}%] ${typeLabel} - ${DEFAULT_TITLE}`
      } else {
        document.title = `Processing ${typeLabel}... - ${DEFAULT_TITLE}`
      }
      return
    }

    // Priority 3: Pending jobs count
    const pendingCount = queue.value?.pending_count ?? 0
    if (pendingCount > 0) {
      document.title = `(${pendingCount} pending) ${DEFAULT_TITLE}`
      return
    }

    // Default title
    document.title = DEFAULT_TITLE
  }

  async function fetchOptions() {
    try {
      options.value = await api.getOptions()
    } catch (e) {
      console.error('Failed to fetch options:', e)
    }
  }

  function showToast(message: string, type: 'success' | 'error' | 'warning' | 'info' = 'info') {
    const id = ++toastId
    toasts.value.push({ id, message, type })

    // Limit the number of toasts to prevent memory issues
    if (toasts.value.length > MAX_TOASTS) {
      toasts.value = toasts.value.slice(-MAX_TOASTS)
    }

    // Save timeout ID to allow cleanup
    const timeoutId = setTimeout(() => {
      toasts.value = toasts.value.filter(t => t.id !== id)
      toastTimeouts.value.delete(id)
    }, 4000)
    toastTimeouts.value.set(id, timeoutId)
  }

  function removeToast(id: number) {
    // Clear timeout if exists
    const timeout = toastTimeouts.value.get(id)
    if (timeout) {
      clearTimeout(timeout)
      toastTimeouts.value.delete(id)
    }
    toasts.value = toasts.value.filter(t => t.id !== id)
  }

  // Polling
  let pollInterval: ReturnType<typeof setInterval> | null = null
  let wsUnsubscribers: Array<() => void> = []

  function setupWebSocketHandlers() {
    // Clear any existing handlers
    wsUnsubscribers.forEach(unsub => unsub())
    wsUnsubscribers = []

    // Track WebSocket state changes
    wsUnsubscribers.push(
      wsService.onStateChange((state) => {
        wsState.value = state
        wsConnected.value = state === 'connected'

        // Track disconnect time for UI display
        if (state === 'disconnected') {
          lastDisconnectTime.value = wsService.getLastDisconnectTime()
        }

        // Stop polling when WebSocket is connected, start when disconnected
        if (state === 'connected') {
          // WebSocket connected - stop polling entirely
          if (pollInterval) {
            clearInterval(pollInterval)
            pollInterval = null
          }
        } else if (state === 'disconnected') {
          // WebSocket disconnected - start fallback polling
          if (!pollInterval) {
            pollInterval = setInterval(() => {
              fetchHealth()
              fetchQueue()
            }, POLL_INTERVAL_FALLBACK)
          }
        }
        // Note: 'connecting' and 'reconnecting' states don't change polling
      })
    )

    // Job events
    wsUnsubscribers.push(
      wsService.on<JobAddedData>('job_added', (data) => {
        showToast(`Job queued: ${getJobTypeLabel(data.type)}`, 'info')
        // Pre-register the job ID to prevent duplicate toast from detectQueueChanges
        previousJobStates.value.set(data.job_id, 'pending')
        // Update pending count immediately
        if (queue.value) {
          queue.value.pending_count = (queue.value.pending_count ?? 0) + 1
          queue.value.total_count = (queue.value.total_count ?? 0) + 1
        }
        // Refresh queue to get full job data
        fetchQueue()
        updateDocumentTitle()
      })
    )

    wsUnsubscribers.push(
      wsService.on<JobStatusChangedData>('job_status_changed', (data) => {
        // Get old status BEFORE updating
        const oldStatus = previousJobStates.value.get(data.job_id)

        // Pre-register the status change to prevent duplicate toast from detectQueueChanges
        previousJobStates.value.set(data.job_id, data.status)

        // Update job in queue if exists
        if (queue.value?.items) {
          const jobIndex = queue.value.items.findIndex(j => j.job_id === data.job_id)
          if (jobIndex !== -1) {
            queue.value.items[jobIndex].status = data.status
            if (data.outputs) {
              queue.value.items[jobIndex].outputs = data.outputs
            }
            if (data.error) {
              queue.value.items[jobIndex].error = data.error
            }
          }
        }

        // Update queue counts based on status change
        if (queue.value && oldStatus && oldStatus !== data.status) {
          // Decrement old status count
          if (oldStatus === 'pending') {
            queue.value.pending_count = Math.max(0, (queue.value.pending_count ?? 0) - 1)
          } else if (oldStatus === 'processing') {
            queue.value.processing_count = Math.max(0, (queue.value.processing_count ?? 0) - 1)
          }
          // Increment new status count
          if (data.status === 'processing') {
            queue.value.processing_count = (queue.value.processing_count ?? 0) + 1
          } else if (data.status === 'completed') {
            queue.value.completed_count = (queue.value.completed_count ?? 0) + 1
          } else if (data.status === 'failed') {
            queue.value.failed_count = (queue.value.failed_count ?? 0) + 1
          }
        }

        // Clear preview when job completes/fails/cancels
        if (data.status === 'completed' || data.status === 'failed' || data.status === 'cancelled') {
          if (currentPreview.value?.jobId === data.job_id) {
            currentPreview.value = null
          }
        }

        // Show toast notification and desktop notification
        switch (data.status) {
          case 'processing':
            showToast(`Job started`, 'info')
            break
          case 'completed':
            showToast(`Job completed`, 'success')
            notificationService.notifyJobComplete('Generation')
            break
          case 'failed':
            showToast(`Job failed${data.error ? ': ' + data.error : ''}`, 'error')
            notificationService.notifyJobFailed('Generation', data.error)
            addRecentError(`Job failed: ${data.error || 'Unknown error'}`, 'job_failed')
            break
          case 'cancelled':
            showToast(`Job cancelled`, 'warning')
            break
        }

        updateDocumentTitle()
      })
    )

    wsUnsubscribers.push(
      wsService.on<JobProgressData>('job_progress', (data) => {
        // Update progress in queue
        if (queue.value?.items) {
          const job = queue.value.items.find(j => j.job_id === data.job_id)
          if (job) {
            job.progress = {
              step: data.step,
              total_steps: data.total_steps
            }
          }
        }
        updateDocumentTitle()
      })
    )

    // Handle live preview images
    wsUnsubscribers.push(
      wsService.on<JobPreviewData>('job_preview', (data) => {
        currentPreview.value = {
          jobId: data.job_id,
          image: data.image,
          step: data.step
        }

        // Reset preview cleanup timer - clear stale preview after no updates
        if (previewCleanupTimer) {
          clearTimeout(previewCleanupTimer)
        }
        previewCleanupTimer = setTimeout(() => {
          currentPreview.value = null
          previewCleanupTimer = null
        }, PREVIEW_CLEANUP_DELAY)
      })
    )

    wsUnsubscribers.push(
      wsService.on<JobCancelledData>('job_cancelled', (data) => {
        // Pre-register the status to prevent duplicate toast from detectQueueChanges
        previousJobStates.value.set(data.job_id, 'cancelled')

        if (queue.value?.items) {
          const job = queue.value.items.find(j => j.job_id === data.job_id)
          if (job) {
            job.status = 'cancelled'
          }
        }
        showToast('Job cancelled', 'warning')
      })
    )

    // Model events
    wsUnsubscribers.push(
      wsService.on<ModelLoadingProgressData>('model_loading_progress', (data) => {
        if (health.value) {
          health.value.model_loading = true
          health.value.loading_model_name = data.model_name
          health.value.loading_step = data.step
          health.value.loading_total_steps = data.total_steps
        }
        updateDocumentTitle()
      })
    )

    wsUnsubscribers.push(
      wsService.on<ModelLoadedData>('model_loaded', (data) => {
        if (health.value) {
          health.value.model_loading = false
          health.value.model_loaded = true
          health.value.model_name = data.model_name
          health.value.model_type = data.model_type
          health.value.model_architecture = data.model_architecture
          health.value.loading_step = null
          health.value.loading_total_steps = null
        }
        const shortName = data.model_name.split('/').pop() ?? data.model_name
        showToast(`Model loaded: ${shortName}`, 'success')
        notificationService.notifyModelLoaded(data.model_name)
        updateDocumentTitle()
      })
    )

    wsUnsubscribers.push(
      wsService.on<ModelUnloadedData>('model_unloaded', () => {
        if (health.value) {
          health.value.model_loaded = false
          health.value.model_name = null
          health.value.model_type = null
          health.value.model_architecture = null
        }
        showToast('Model unloaded', 'info')
        updateDocumentTitle()
      })
    )

    // Upscaler events
    wsUnsubscribers.push(
      wsService.on<UpscalerLoadedData>('upscaler_loaded', (data) => {
        if (health.value) {
          health.value.upscaler_loaded = true
          health.value.upscaler_name = data.model_name
        }
        showToast(`Upscaler loaded: ${data.model_name} (${data.upscale_factor}x)`, 'success')
      })
    )

    wsUnsubscribers.push(
      wsService.on<UpscalerUnloadedData>('upscaler_unloaded', () => {
        if (health.value) {
          health.value.upscaler_loaded = false
          health.value.upscaler_name = null
        }
        showToast('Upscaler unloaded', 'info')
      })
    )
  }

  async function startPolling() {
    isInitialLoading.value = true
    
    try {
      // Initial data fetch - fetch health first to get ws_port
      await fetchHealth()
      
      // Fetch initial data in parallel
      await Promise.all([
        fetchQueue(),
        fetchOptions(),
        fetchGenerationDefaults(),
        fetchUIPreferences()
      ])

      // Setup WebSocket handlers and connect using port from health response
      setupWebSocketHandlers()
      const wsPort = health.value?.ws_port ?? undefined
      wsService.connect(wsPort)

      // Start fallback polling until WebSocket connects
      // (will be stopped by onStateChange handler when WS connects)
      if (!pollInterval) {
        pollInterval = setInterval(() => {
          fetchHealth()
          fetchQueue()
        }, POLL_INTERVAL_FALLBACK)
      }
    } finally {
      isInitialLoading.value = false
    }
  }

  function stopPolling() {
    // Stop polling
    if (pollInterval) {
      clearInterval(pollInterval)
      pollInterval = null
    }

    // Clear preview cleanup timer
    if (previewCleanupTimer) {
      clearTimeout(previewCleanupTimer)
      previewCleanupTimer = null
    }

    // Clear all toast timeouts
    toastTimeouts.value.forEach(timeout => clearTimeout(timeout))
    toastTimeouts.value.clear()

    // Clear preview data
    currentPreview.value = null

    // Disconnect WebSocket
    wsService.disconnect()

    // Cleanup handlers
    wsUnsubscribers.forEach(unsub => unsub())
    wsUnsubscribers = []

    // Clear tracked job states to free memory
    previousJobStates.value.clear()

    // Reset document title when stopping
    document.title = DEFAULT_TITLE
  }

  return {
    // State
    health,
    models,
    queue,
    options,
    connected,
    loading,
    error,
    isInitialLoading,
    toasts,
    queueFilters,
    recentErrors,

    // WebSocket state
    wsConnected,
    wsState,
    lastDisconnectTime,
    apiAvailable,

    // Preview
    currentPreview,

    // Computed
    modelLoaded,
    modelLoading,
    loadingModelName,
    loadingStep,
    loadingTotalSteps,
    lastLoadError,
    modelName,
    modelType,
    modelArchitecture,
    loadedComponents,
    upscalerLoaded,
    upscalerName,
    queueStats,
    samplers,
    schedulers,
    quantizationTypes,

    // Actions
    fetchHealth,
    fetchModels,
    fetchQueue,
    fetchOptions,
    setQueueFilters,
    showToast,
    removeToast,
    addRecentError,
    startPolling,
    stopPolling,

    // Upscale image
    upscaleInputImage,
    setUpscaleInputImage,
    clearUpscaleInputImage,

    // Settings
    generationDefaults,
    uiPreferences,
    fetchGenerationDefaults,
    updateGenerationDefaults,
    fetchUIPreferences,
    updateUIPreferences,
    resetAllSettings,

    // Desktop notifications
    desktopNotificationsEnabled,
    desktopNotificationsPermission,
    toggleDesktopNotifications
  }
})
