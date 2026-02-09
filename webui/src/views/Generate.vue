<script setup lang="ts">
import { ref, computed, onMounted, onBeforeUnmount, watch, nextTick } from 'vue'
import { useDebounceFn } from '@vueuse/core'
import { useAppStore } from '../stores/app'
import { api, type GenerationParams, type Img2ImgParams, type Txt2VidParams } from '../api/client'
import ImageUploader from '../components/ImageUploader.vue'
import PromptEnhancer from '../components/PromptEnhancer.vue'
import ProgressBar from '../components/ProgressBar.vue'
import Lightbox from '../components/Lightbox.vue'
import LoadedModelPanel from '../components/LoadedModelPanel.vue'
import HighlightedPrompt from '../components/HighlightedPrompt.vue'

const store = useAppStore()

// Highlighted setting for assistant navigation
const highlightedSetting = ref<string | null>(null)
let highlightTimeout: ReturnType<typeof setTimeout> | null = null

// Ollama integration
const ollamaEnabled = computed(() => store.ollamaStatus?.enabled ?? false)

const mode = ref<'txt2img' | 'img2img' | 'txt2vid'>('txt2img')
const submitting = ref(false)
const cooldown = ref(false)
let cooldownTimer: ReturnType<typeof setTimeout> | null = null

// Common params
const prompt = ref('')
const negativePrompt = ref('')
const width = ref(512)
const height = ref(512)
const steps = ref(20)
const cfgScale = ref(7.0)
const distilledGuidance = ref(3.5)
const seed = ref(-1)
const sampler = ref('euler_a')
const scheduler = ref('discrete')
const batchCount = ref(1)
const clipSkip = ref(-1)

// img2img params
const initImage = ref<string | undefined>()
const strength = ref(0.75)
const maskImage = ref<string | undefined>()

// ControlNet params
const controlImage = ref<string | undefined>()
const controlStrength = ref(0.9)

// txt2vid params
const videoFrames = ref(33)
const fps = ref(16)
const flowShift = ref(3.0)

// Advanced params
const showAdvanced = ref(false)
const slgScale = ref(0.0)
const easycache = ref(false)
const easycacheThreshold = ref(0.2)
const vaeTiling = ref(false)
const vaeTileSizeX = ref(0)
const vaeTileSizeY = ref(0)
const vaeTileOverlap = ref(0.5)

// Copy settings dropdown
const showCopyMenu = ref(false)

// Lightbox for previews
const showLightbox = ref(false)
const lightboxImages = ref<string[]>([])
const lightboxIndex = ref(0)

// Track current generating job
const lastSubmittedJobId = ref<string | null>(null)

// Get current settings as object
function getCurrentSettings() {
  return {
    prompt: prompt.value,
    negativePrompt: negativePrompt.value,
    width: width.value,
    height: height.value,
    steps: steps.value,
    cfgScale: cfgScale.value,
    distilledGuidance: distilledGuidance.value,
    seed: seed.value,
    sampler: sampler.value,
    scheduler: scheduler.value,
    batchCount: batchCount.value,
    clipSkip: clipSkip.value,
    strength: strength.value,
    controlStrength: controlStrength.value,
    videoFrames: videoFrames.value,
    fps: fps.value,
    flowShift: flowShift.value,
    slgScale: slgScale.value,
    easycache: easycache.value,
    easycacheThreshold: easycacheThreshold.value,
    vaeTiling: vaeTiling.value,
    vaeTileSizeX: vaeTileSizeX.value,
    vaeTileSizeY: vaeTileSizeY.value,
    vaeTileOverlap: vaeTileOverlap.value
  }
}

// Apply settings from object
function applySettings(settings: Record<string, unknown>) {
  if (settings.prompt !== undefined) prompt.value = settings.prompt as string
  if (settings.negativePrompt !== undefined) negativePrompt.value = settings.negativePrompt as string
  if (settings.width !== undefined) width.value = settings.width as number
  if (settings.height !== undefined) height.value = settings.height as number
  if (settings.steps !== undefined) steps.value = settings.steps as number
  if (settings.cfgScale !== undefined) cfgScale.value = settings.cfgScale as number
  if (settings.distilledGuidance !== undefined) distilledGuidance.value = settings.distilledGuidance as number
  if (settings.seed !== undefined) seed.value = settings.seed as number
  if (settings.sampler !== undefined) sampler.value = settings.sampler as string
  if (settings.scheduler !== undefined) scheduler.value = settings.scheduler as string
  if (settings.batchCount !== undefined) batchCount.value = settings.batchCount as number
  if (settings.clipSkip !== undefined) clipSkip.value = settings.clipSkip as number
  if (settings.strength !== undefined) strength.value = settings.strength as number
  if (settings.controlStrength !== undefined) controlStrength.value = settings.controlStrength as number
  if (settings.videoFrames !== undefined) videoFrames.value = settings.videoFrames as number
  if (settings.fps !== undefined) fps.value = settings.fps as number
  if (settings.flowShift !== undefined) flowShift.value = settings.flowShift as number
  if (settings.slgScale !== undefined) slgScale.value = settings.slgScale as number
  if (settings.easycache !== undefined) easycache.value = settings.easycache as boolean
  if (settings.easycacheThreshold !== undefined) easycacheThreshold.value = settings.easycacheThreshold as number
  if (settings.vaeTiling !== undefined) vaeTiling.value = settings.vaeTiling as boolean
  if (settings.vaeTileSizeX !== undefined) vaeTileSizeX.value = settings.vaeTileSizeX as number
  if (settings.vaeTileSizeY !== undefined) vaeTileSizeY.value = settings.vaeTileSizeY as number
  if (settings.vaeTileOverlap !== undefined) vaeTileOverlap.value = settings.vaeTileOverlap as number
}

// Merge architecture defaults with user preferences (user prefs take precedence)
function mergeWithDefaults(architectureDefaults: Record<string, unknown>, userPrefs: Record<string, unknown>) {
  return { ...architectureDefaults, ...userPrefs }
}

// Get architecture defaults for current model
function getArchitectureDefaultsForMode(): Record<string, unknown> {
  if (!store.modelArchitecture) return {}

  const architectures = store.modelArchitecture
  for (const [archId, archData] of Object.entries(architectures)) {
    if (archData && typeof archData === 'object' && 'generationDefaults' in archData) {
      // Map simplified keys to our internal keys
      const defaults = (archData as any).generationDefaults || {}
      const mapped: Record<string, unknown> = {}
      for (const [key, value] of Object.entries(defaults)) {
        if (key === 'cfg_scale') mapped.cfgScale = value
        else if (key === 'steps') mapped.steps = value
        else mapped[key] = value
      }
      console.log('Loading architecture defaults for:', archId)
      return mapped
    }
  }
  return {}
}

// Save settings for current mode to backend
async function saveSettings() {
  const settings = getCurrentSettings()
  // Don't save prompt to persistent settings - just generation parameters
  const { prompt: _, negativePrompt: __, ...toSave } = settings

  try {
    await api.updateGenerationDefaultsForMode(mode.value, toSave as any)
    // Silently save, don't show toast for each change
  } catch (e) {
    console.error('Failed to save settings:', e)
  }
}

// Load settings for a specific mode from backend
async function loadSettingsForMode(targetMode: 'txt2img' | 'img2img' | 'txt2vid') {
  try {
    // Get architecture defaults from current model
    const architectureDefaults = getArchitectureDefaultsForMode()

    // Get user preferences from backend
    const userPrefs = (store.generationDefaults?.[targetMode] as Record<string, unknown>) || {}

    // Merge: user prefs override architecture defaults
    const settings = mergeWithDefaults(architectureDefaults, userPrefs)
    applySettings(settings)
  } catch (e) {
    console.error('Failed to load settings:', e)
    // Fall back to architecture defaults if loading fails
    const architectureDefaults = getArchitectureDefaultsForMode()
    applySettings(architectureDefaults)
  }
}

// Copy settings from another mode
async function copySettingsFrom(sourceMode: 'txt2img' | 'img2img' | 'txt2vid') {
  try {
    const userPrefs = (store.generationDefaults?.[sourceMode] as Record<string, unknown>) || {}
    if (Object.keys(userPrefs).length === 0) {
      store.showToast(`No saved preferences for ${getModeLabel(sourceMode)}`, 'warning')
      showCopyMenu.value = false
      return
    }

    applySettings(userPrefs)
    await saveSettings()
    store.showToast(`Settings copied from ${getModeLabel(sourceMode)}`, 'success')
  } catch (e) {
    store.showToast('Failed to copy settings', 'error')
  }
  showCopyMenu.value = false
}

function getModeLabel(m: string): string {
  const labels: Record<string, string> = {
    txt2img: 'Text to Image',
    img2img: 'Image to Image',
    txt2vid: 'Text to Video'
  }
  return labels[m] || m
}

// Get other modes for copy menu
const otherModes = computed(() => {
  const modes: Array<'txt2img' | 'img2img' | 'txt2vid'> = ['txt2img', 'img2img', 'txt2vid']
  return modes.filter(m => m !== mode.value)
})

// Watch for mode changes - save current, load new
watch(mode, async (newMode, oldMode) => {
  if (oldMode) {
    // Save settings for old mode before switching
    await saveSettings()
  }
  // Load settings for new mode
  await loadSettingsForMode(newMode)
})

// Debounced save for settings (500ms delay)
const debouncedSaveSettings = useDebounceFn(() => {
  saveSettings()
}, 500)

// Watch for generation parameter changes with debounce
watch([
  width, height, steps, cfgScale, distilledGuidance, seed, sampler,
  scheduler, batchCount, clipSkip, strength, controlStrength, videoFrames,
  fps, flowShift, slgScale, easycache, easycacheThreshold, vaeTiling,
  vaeTileSizeX, vaeTileSizeY, vaeTileOverlap
], debouncedSaveSettings)

// Close copy menu when clicking outside
function handleClickOutside(event: MouseEvent) {
  const target = event.target as HTMLElement
  if (!target.closest('.copy-menu-container')) {
    showCopyMenu.value = false
  }
}

// Samplers and schedulers from store (with fallbacks)
const samplers = computed(() =>
  store.samplers.length > 0
    ? store.samplers
    : ['euler', 'euler_a', 'heun', 'dpm2', 'dpm++2s_a', 'dpm++2m', 'dpm++2mv2', 'ipndm', 'ipndm_v', 'lcm', 'ddim_trailing', 'tcd', 'res_multistep', 'res_2s']
)

const schedulers = computed(() =>
  store.schedulers.length > 0
    ? store.schedulers
    : ['discrete', 'karras', 'exponential', 'ays', 'gits', 'sgm_uniform', 'simple', 'smoothstep', 'kl_optimal', 'lcm', 'bong_tangent']
)

// Recommended settings per model architecture
interface RecommendedSettings {
  sampler: string
  scheduler: string
  steps: number
  cfgScale: number
  distilledGuidance: number
  clipSkip: number
  width: number
  height: number
  slgScale?: number
  easycache?: boolean
}

const modelRecommendations: Record<string, RecommendedSettings> = {
  'Z-Image': {
    sampler: 'euler',
    scheduler: 'smoothstep',
    steps: 9,
    cfgScale: 1.0,
    distilledGuidance: 3.5,
    clipSkip: -1,
    width: 1024,
    height: 1024,
    easycache: true
  },
  'Flux': {
    sampler: 'euler',
    scheduler: 'simple',
    steps: 20,
    cfgScale: 1.0,
    distilledGuidance: 3.5,
    clipSkip: -1,
    width: 1024,
    height: 1024
  },
  'Flux Schnell': {
    sampler: 'euler',
    scheduler: 'simple',
    steps: 4,
    cfgScale: 1.0,
    distilledGuidance: 0.0,
    clipSkip: -1,
    width: 1024,
    height: 1024
  },
  'SD3': {
    sampler: 'euler',
    scheduler: 'simple',
    steps: 28,
    cfgScale: 4.5,
    distilledGuidance: 0.0,
    clipSkip: -1,
    width: 1024,
    height: 1024
  },
  'SDXL': {
    sampler: 'dpm++2m',
    scheduler: 'karras',
    steps: 25,
    cfgScale: 7.0,
    distilledGuidance: 0.0,
    clipSkip: -1,
    width: 1024,
    height: 1024
  },
  'SD1': {
    sampler: 'euler_a',
    scheduler: 'karras',
    steps: 20,
    cfgScale: 7.0,
    distilledGuidance: 0.0,
    clipSkip: -1,
    width: 512,
    height: 512
  },
  'SD2': {
    sampler: 'euler_a',
    scheduler: 'karras',
    steps: 20,
    cfgScale: 7.0,
    distilledGuidance: 0.0,
    clipSkip: -1,
    width: 768,
    height: 768
  },
  'Wan': {
    sampler: 'euler',
    scheduler: 'simple',
    steps: 30,
    cfgScale: 5.0,
    distilledGuidance: 0.0,
    clipSkip: -1,
    width: 832,
    height: 480
  },
  'Chroma': {
    sampler: 'euler',
    scheduler: 'simple',
    steps: 20,
    cfgScale: 1.0,
    distilledGuidance: 4.0,
    clipSkip: -1,
    width: 1024,
    height: 1024
  }
}

// Get recommended settings for current model
const recommended = computed((): RecommendedSettings | null => {
  const arch = store.modelArchitecture
  if (!arch) return null

  // Try exact match first
  if (modelRecommendations[arch]) {
    return modelRecommendations[arch]
  }

  // Try partial match (e.g., "SD1.5" matches "SD1")
  for (const key of Object.keys(modelRecommendations)) {
    if (arch.toLowerCase().includes(key.toLowerCase()) ||
        key.toLowerCase().includes(arch.toLowerCase())) {
      return modelRecommendations[key]
    }
  }

  return null
})

// Check if a setting differs from recommended
function isDifferentFromRecommended(field: keyof RecommendedSettings, value: unknown): boolean {
  if (!recommended.value) return false
  const rec = recommended.value[field]
  if (rec === undefined) return false
  return rec !== value
}

// Apply recommended settings
function applyRecommendedSettings() {
  if (!recommended.value) return

  sampler.value = recommended.value.sampler
  scheduler.value = recommended.value.scheduler
  steps.value = recommended.value.steps
  cfgScale.value = recommended.value.cfgScale
  distilledGuidance.value = recommended.value.distilledGuidance
  clipSkip.value = recommended.value.clipSkip
  width.value = recommended.value.width
  height.value = recommended.value.height

  if (recommended.value.slgScale !== undefined) {
    slgScale.value = recommended.value.slgScale
  }
  if (recommended.value.easycache !== undefined) {
    easycache.value = recommended.value.easycache
  }

  store.showToast('Applied recommended settings', 'success')
}

const presets = [
  { w: 512, h: 512, label: '512x512' },
  { w: 768, h: 768, label: '768x768' },
  { w: 1024, h: 1024, label: '1024x1024' },
  { w: 1024, h: 768, label: '1024x768' },
  { w: 768, h: 1024, label: '768x1024' },
  { w: 832, h: 480, label: '832x480 (Video)' }
]

const hasControlNet = computed(() => store.loadedComponents?.controlnet)

// Handle assistant setting changes
function handleAssistantSettingChange(event: Event) {
  const customEvent = event as CustomEvent<{ field: string; value: unknown }>
  const { field, value } = customEvent.detail

  // Map field names to refs
  const fieldMap: Record<string, { get: () => unknown; set: (v: unknown) => void }> = {
    prompt: { get: () => prompt.value, set: (v) => prompt.value = v as string },
    negative_prompt: { get: () => negativePrompt.value, set: (v) => negativePrompt.value = v as string },
    negativePrompt: { get: () => negativePrompt.value, set: (v) => negativePrompt.value = v as string },
    width: { get: () => width.value, set: (v) => width.value = v as number },
    height: { get: () => height.value, set: (v) => height.value = v as number },
    steps: { get: () => steps.value, set: (v) => steps.value = v as number },
    cfg_scale: { get: () => cfgScale.value, set: (v) => cfgScale.value = v as number },
    cfgScale: { get: () => cfgScale.value, set: (v) => cfgScale.value = v as number },
    distilled_guidance: { get: () => distilledGuidance.value, set: (v) => distilledGuidance.value = v as number },
    distilledGuidance: { get: () => distilledGuidance.value, set: (v) => distilledGuidance.value = v as number },
    seed: { get: () => seed.value, set: (v) => seed.value = v as number },
    sampler: { get: () => sampler.value, set: (v) => sampler.value = v as string },
    scheduler: { get: () => scheduler.value, set: (v) => scheduler.value = v as string },
    batch_count: { get: () => batchCount.value, set: (v) => batchCount.value = v as number },
    batchCount: { get: () => batchCount.value, set: (v) => batchCount.value = v as number },
    strength: { get: () => strength.value, set: (v) => strength.value = v as number },
    slg_scale: { get: () => slgScale.value, set: (v) => slgScale.value = v as number },
    slgScale: { get: () => slgScale.value, set: (v) => slgScale.value = v as number },
    vae_tiling: { get: () => vaeTiling.value, set: (v) => vaeTiling.value = v as boolean },
    vaeTiling: { get: () => vaeTiling.value, set: (v) => vaeTiling.value = v as boolean },
    vae_tile_size_x: { get: () => vaeTileSizeX.value, set: (v) => vaeTileSizeX.value = v as number },
    vaeTileSizeX: { get: () => vaeTileSizeX.value, set: (v) => vaeTileSizeX.value = v as number },
    vae_tile_size_y: { get: () => vaeTileSizeY.value, set: (v) => vaeTileSizeY.value = v as number },
    vaeTileSizeY: { get: () => vaeTileSizeY.value, set: (v) => vaeTileSizeY.value = v as number },
    vae_tile_overlap: { get: () => vaeTileOverlap.value, set: (v) => vaeTileOverlap.value = v as number },
    vaeTileOverlap: { get: () => vaeTileOverlap.value, set: (v) => vaeTileOverlap.value = v as number },
    easycache: { get: () => easycache.value, set: (v) => easycache.value = v as boolean },
    video_frames: { get: () => videoFrames.value, set: (v) => videoFrames.value = v as number },
    videoFrames: { get: () => videoFrames.value, set: (v) => videoFrames.value = v as number },
    fps: { get: () => fps.value, set: (v) => fps.value = v as number }
  }

  const handler = fieldMap[field]
  if (handler) {
    handler.set(value)
    saveSettings()  // Persist the change
  }
}

// Handle apply recommended settings from assistant
function handleApplyRecommended() {
  applyRecommendedSettings()
}

// Handle highlight setting from assistant
function handleHighlightSetting(event: Event) {
  const customEvent = event as CustomEvent<{ field: string }>
  const { field } = customEvent.detail

  // Map field name to element data attribute
  const fieldToElementMap: Record<string, string> = {
    prompt: 'prompt',
    negative_prompt: 'negative-prompt',
    negativePrompt: 'negative-prompt',
    width: 'width',
    height: 'height',
    steps: 'steps',
    cfg_scale: 'cfg-scale',
    cfgScale: 'cfg-scale',
    distilled_guidance: 'distilled-guidance',
    distilledGuidance: 'distilled-guidance',
    seed: 'seed',
    sampler: 'sampler',
    scheduler: 'scheduler',
    batch_count: 'batch-count',
    batchCount: 'batch-count',
    clip_skip: 'clip-skip',
    clipSkip: 'clip-skip',
    slg_scale: 'slg-scale',
    slgScale: 'slg-scale',
    vae_tiling: 'vae-tiling',
    vaeTiling: 'vae-tiling',
    vae_tile_size_x: 'vae-tile-size-x',
    vaeTileSizeX: 'vae-tile-size-x',
    vae_tile_size_y: 'vae-tile-size-y',
    vaeTileSizeY: 'vae-tile-size-y',
    vae_tile_overlap: 'vae-tile-overlap',
    vaeTileOverlap: 'vae-tile-overlap',
    easycache: 'easycache',
    video_frames: 'video-frames',
    videoFrames: 'video-frames',
    fps: 'fps'
  }

  const elementId = fieldToElementMap[field]
  if (!elementId) return

  // Set highlight
  highlightedSetting.value = elementId

  // Find and scroll to element
  nextTick(() => {
    const element = document.querySelector(`[data-setting="${elementId}"]`)
    if (element) {
      element.scrollIntoView({ behavior: 'smooth', block: 'center' })

      // Clear highlight after 3 seconds
      if (highlightTimeout) clearTimeout(highlightTimeout)
      highlightTimeout = setTimeout(() => {
        highlightedSetting.value = null
      }, 3000)
    }
  })
}

// Handle assistant set image event (for img2img)
function handleAssistantSetImage(e: Event) {
  const event = e as CustomEvent<{ target: string; imageBase64: string }>
  if (event.detail.target === 'img2img') {
    // Switch to img2img mode
    mode.value = 'img2img'
    // Set the init image (needs data URL format)
    initImage.value = `data:image/png;base64,${event.detail.imageBase64}`
    // Save settings
    nextTick(() => saveSettings())
  }
}

// Load settings on mount
onMounted(async () => {
  document.addEventListener('click', handleClickOutside)
  window.addEventListener('assistant-setting-change', handleAssistantSettingChange)
  window.addEventListener('assistant-apply-recommended', handleApplyRecommended)
  window.addEventListener('assistant-highlight-setting', handleHighlightSetting)
  window.addEventListener('assistant-set-image', handleAssistantSetImage)

  // First check if we have reloaded job params from Queue view
  const savedParams = sessionStorage.getItem('reloadJobParams')
  if (savedParams) {
    try {
      const data = JSON.parse(savedParams)
      // Set mode first without triggering watch (to avoid loading old settings)
      if (data.type === 'txt2img' || data.type === 'img2img' || data.type === 'txt2vid') {
        mode.value = data.type
      }
      loadJobParams(data.type, data.params)
      sessionStorage.removeItem('reloadJobParams')
      // Save these as the current settings for this mode
      saveSettings()
    } catch (e) {
      console.error('Failed to load job params:', e)
    }
  } else {
    // Restore last used mode from local storage
    const lastMode = localStorage.getItem('generateSettings_lastMode') as 'txt2img' | 'img2img' | 'txt2vid' | null
    if (lastMode && ['txt2img', 'img2img', 'txt2vid'].includes(lastMode)) {
      mode.value = lastMode
    }
    // Load settings for current mode
    await loadSettingsForMode(mode.value)
  }
})

onBeforeUnmount(() => {
  document.removeEventListener('click', handleClickOutside)
  window.removeEventListener('assistant-setting-change', handleAssistantSettingChange)
  window.removeEventListener('assistant-apply-recommended', handleApplyRecommended)
  window.removeEventListener('assistant-highlight-setting', handleHighlightSetting)
  window.removeEventListener('assistant-set-image', handleAssistantSetImage)
  if (cooldownTimer) clearTimeout(cooldownTimer)
  if (highlightTimeout) clearTimeout(highlightTimeout)
})

function loadJobParams(type: string, params: Record<string, unknown>) {
  // Set mode based on job type
  if (type === 'txt2img' || type === 'img2img' || type === 'txt2vid') {
    mode.value = type
  }

  // Common params
  if (params.prompt !== undefined) prompt.value = params.prompt as string
  if (params.negative_prompt !== undefined) negativePrompt.value = params.negative_prompt as string
  if (params.width !== undefined) width.value = params.width as number
  if (params.height !== undefined) height.value = params.height as number
  if (params.steps !== undefined) steps.value = params.steps as number
  if (params.cfg_scale !== undefined) cfgScale.value = params.cfg_scale as number
  if (params.distilled_guidance !== undefined) distilledGuidance.value = params.distilled_guidance as number
  if (params.seed !== undefined) seed.value = params.seed as number
  if (params.sampler !== undefined) sampler.value = params.sampler as string
  if (params.scheduler !== undefined) scheduler.value = params.scheduler as string
  if (params.batch_count !== undefined) batchCount.value = params.batch_count as number
  if (params.clip_skip !== undefined) clipSkip.value = params.clip_skip as number

  // img2img params
  if (params.strength !== undefined) strength.value = params.strength as number
  // Note: init_image_base64 and mask_image_base64 are not reloaded as they may be too large

  // ControlNet params
  if (params.control_strength !== undefined) controlStrength.value = params.control_strength as number

  // txt2vid params
  if (params.video_frames !== undefined) videoFrames.value = params.video_frames as number
  if (params.fps !== undefined) fps.value = params.fps as number
  if (params.flow_shift !== undefined) flowShift.value = params.flow_shift as number

  // Advanced params
  if (params.slg_scale !== undefined) slgScale.value = params.slg_scale as number
  if (params.easycache !== undefined) easycache.value = params.easycache as boolean
  if (params.easycache_threshold !== undefined) easycacheThreshold.value = params.easycache_threshold as number
  if (params.vae_tiling !== undefined) vaeTiling.value = params.vae_tiling as boolean
  if (params.vae_tile_size_x !== undefined) vaeTileSizeX.value = params.vae_tile_size_x as number
  if (params.vae_tile_size_y !== undefined) vaeTileSizeY.value = params.vae_tile_size_y as number
  if (params.vae_tile_overlap !== undefined) vaeTileOverlap.value = params.vae_tile_overlap as number
}

function setPreset(preset: { w: number; h: number }) {
  width.value = preset.w
  height.value = preset.h
}

function randomizeSeed() {
  seed.value = Math.floor(Math.random() * 2147483647)
}

// Preview settings functions
function openPreviewLightbox() {
  if (store.currentPreview?.image) {
    lightboxImages.value = [store.currentPreview.image]
    lightboxIndex.value = 0
    showLightbox.value = true
  }
}

// Computed property to check if there's a processing job for our last submission
const isCurrentJobProcessing = computed(() => {
  if (!lastSubmittedJobId.value) return false
  return store.queue?.items?.some(
    j => j.job_id === lastSubmittedJobId.value && j.status === 'processing'
  )
})

const currentJobProgress = computed(() => {
  if (!lastSubmittedJobId.value) return null
  const job = store.queue?.items?.find(j => j.job_id === lastSubmittedJobId.value)
  return job?.progress || null
})

const hasPreviewForCurrentJob = computed(() => {
  return store.currentPreview?.jobId === lastSubmittedJobId.value
})

async function handleSubmit() {
  if (!prompt.value.trim()) {
    store.showToast('Please enter a prompt', 'warning')
    return
  }

  if (!store.modelLoaded) {
    store.showToast('Please load a model first', 'warning')
    return
  }

  submitting.value = true
  try {
    const baseParams: GenerationParams = {
      prompt: prompt.value,
      negative_prompt: negativePrompt.value,
      width: width.value,
      height: height.value,
      steps: steps.value,
      cfg_scale: cfgScale.value,
      distilled_guidance: distilledGuidance.value,
      seed: seed.value,
      sampler: sampler.value,
      scheduler: scheduler.value,
      batch_count: batchCount.value,
      clip_skip: clipSkip.value
    }

    // Advanced params
    if (slgScale.value > 0) {
      baseParams.slg_scale = slgScale.value
    }
    if (easycache.value) {
      baseParams.easycache = true
      baseParams.easycache_threshold = easycacheThreshold.value
    }
    if (vaeTiling.value) {
      baseParams.vae_tiling = true
      if (vaeTileSizeX.value > 0) baseParams.vae_tile_size_x = vaeTileSizeX.value
      if (vaeTileSizeY.value > 0) baseParams.vae_tile_size_y = vaeTileSizeY.value
      baseParams.vae_tile_overlap = vaeTileOverlap.value
    }

    // Control image
    if (controlImage.value && hasControlNet.value) {
      baseParams.control_image_base64 = controlImage.value
      baseParams.control_strength = controlStrength.value
    }

    let result
    if (mode.value === 'txt2img') {
      result = await api.txt2img(baseParams)
    } else if (mode.value === 'img2img') {
      if (!initImage.value) {
        store.showToast('Please upload an input image', 'warning')
        submitting.value = false
        return
      }
      const params: Img2ImgParams = {
        ...baseParams,
        init_image_base64: initImage.value,
        strength: strength.value
      }
      if (maskImage.value) {
        params.mask_image_base64 = maskImage.value
      }
      result = await api.img2img(params)
    } else {
      const params: Txt2VidParams = {
        ...baseParams,
        video_frames: videoFrames.value,
        fps: fps.value,
        flow_shift: flowShift.value
      }
      result = await api.txt2vid(params)
    }

    store.showToast(`Job submitted: ${result.job_id}`, 'success')
    lastSubmittedJobId.value = result.job_id

    // Start cooldown to prevent double submissions
    cooldown.value = true
    if (cooldownTimer) clearTimeout(cooldownTimer)
    cooldownTimer = setTimeout(() => {
      cooldown.value = false
    }, 5000)
  } catch (e) {
    store.showToast(e instanceof Error ? e.message : 'Failed to submit job', 'error')
  } finally {
    submitting.value = false
  }
}
</script>

<template>
  <div class="generate">
    <div class="page-header">
      <h1 class="page-title">Generate</h1>
      <p class="page-subtitle">Create images and videos from text prompts</p>
    </div>

    <!-- Mode Tabs -->
    <div class="tabs-container">
      <div class="tabs">
        <button :class="['tab', { active: mode === 'txt2img' }]" @click="mode = 'txt2img'">
          Text to Image
        </button>
        <button :class="['tab', { active: mode === 'img2img' }]" @click="mode = 'img2img'">
          Image to Image
        </button>
        <button :class="['tab', { active: mode === 'txt2vid' }]" @click="mode = 'txt2vid'">
          Text to Video
        </button>
      </div>
      <div class="copy-menu-container">
        <button
          class="btn btn-secondary btn-sm"
          @click.stop="showCopyMenu = !showCopyMenu"
          title="Copy settings from another mode"
        >
          Copy from...
        </button>
        <div v-if="showCopyMenu" class="copy-menu">
          <button
            v-for="m in otherModes"
            :key="m"
            class="copy-menu-item"
            @click="copySettingsFrom(m)"
          >
            {{ getModeLabel(m) }}
          </button>
        </div>
      </div>
    </div>

    <div class="generate-form">
      <!-- Loaded Model Panel -->
      <LoadedModelPanel />

      <!-- Prompt Section -->
      <div class="card">
        <div class="form-group" data-setting="prompt" :class="{ 'setting-highlighted': highlightedSetting === 'prompt' }">
          <div class="form-label-row">
            <label class="form-label">Prompt</label>
            <PromptEnhancer v-if="ollamaEnabled" v-model="prompt" />
          </div>
          <HighlightedPrompt
            v-model="prompt"
            placeholder="A beautiful landscape with mountains and a lake..."
            :rows="3"
          />
          <div class="form-hint">Use &lt;lora:name:weight&gt; to apply LoRA models. Embeddings and duplicates are highlighted.</div>
        </div>

        <div class="form-group" data-setting="negative-prompt" :class="{ 'setting-highlighted': highlightedSetting === 'negative-prompt' }">
          <label class="form-label">Negative Prompt (optional)</label>
          <HighlightedPrompt
            v-model="negativePrompt"
            placeholder="blurry, low quality, bad anatomy..."
            :rows="2"
          />
        </div>
      </div>

      <!-- img2img Input -->
      <div v-if="mode === 'img2img'" class="card">
        <div class="card-header">
          <h3 class="card-title">Input Image</h3>
        </div>
        <ImageUploader v-model="initImage" label="Initial Image" />
        <div class="form-group">
          <label class="form-label">Strength: {{ strength.toFixed(2) }}</label>
          <input v-model.number="strength" type="range" class="form-range" min="0" max="1" step="0.05" />
        </div>
        <ImageUploader v-model="maskImage" label="Mask Image (optional - white = repaint)" />
      </div>

      <!-- ControlNet Section -->
      <div v-if="hasControlNet" class="card">
        <div class="card-header">
          <h3 class="card-title">ControlNet</h3>
        </div>
        <ImageUploader v-model="controlImage" label="Control Image" />
        <div class="form-group">
          <label class="form-label">Control Strength: {{ controlStrength.toFixed(2) }}</label>
          <input v-model.number="controlStrength" type="range" class="form-range" min="0" max="1" step="0.05" />
        </div>
      </div>

      <!-- Parameters -->
      <div class="card">
        <div class="card-header">
          <h3 class="card-title">Parameters</h3>
          <button
            v-if="recommended"
            class="btn btn-secondary btn-sm"
            @click="applyRecommendedSettings"
            title="Apply recommended settings for this model"
          >
            Apply Recommended
          </button>
        </div>

        <!-- Recommended Settings Summary -->
        <div v-if="recommended" class="recommended-panel">
          <div class="recommended-header">
            <span class="recommended-icon">&#128161;</span>
            <span class="recommended-title">Recommended for {{ store.modelArchitecture }}</span>
          </div>
          <div class="recommended-items">
            <span class="rec-item">
              <span class="rec-label">Sampler:</span>
              <span class="rec-value" :class="{ 'rec-different': isDifferentFromRecommended('sampler', sampler) }">{{ recommended.sampler }}</span>
            </span>
            <span class="rec-item">
              <span class="rec-label">Scheduler:</span>
              <span class="rec-value" :class="{ 'rec-different': isDifferentFromRecommended('scheduler', scheduler) }">{{ recommended.scheduler }}</span>
            </span>
            <span class="rec-item">
              <span class="rec-label">Steps:</span>
              <span class="rec-value" :class="{ 'rec-different': isDifferentFromRecommended('steps', steps) }">{{ recommended.steps }}</span>
            </span>
            <span class="rec-item">
              <span class="rec-label">CFG:</span>
              <span class="rec-value" :class="{ 'rec-different': isDifferentFromRecommended('cfgScale', cfgScale) }">{{ recommended.cfgScale }}</span>
            </span>
            <span class="rec-item">
              <span class="rec-label">Size:</span>
              <span class="rec-value" :class="{ 'rec-different': width !== recommended.width || height !== recommended.height }">{{ recommended.width }}x{{ recommended.height }}</span>
            </span>
            <span v-if="recommended.distilledGuidance > 0" class="rec-item">
              <span class="rec-label">Dist. Guidance:</span>
              <span class="rec-value" :class="{ 'rec-different': isDifferentFromRecommended('distilledGuidance', distilledGuidance) }">{{ recommended.distilledGuidance }}</span>
            </span>
          </div>
        </div>

        <!-- Size Presets -->
        <div class="form-group">
          <label class="form-label">Size Presets</label>
          <div class="preset-buttons">
            <button
              v-for="preset in presets"
              :key="preset.label"
              :class="['btn', 'btn-sm', width === preset.w && height === preset.h ? 'btn-primary' : 'btn-secondary']"
              @click="setPreset(preset)"
            >
              {{ preset.label }}
            </button>
          </div>
        </div>

        <div class="form-row">
          <div class="form-group" data-setting="width" :class="{ 'setting-highlighted': highlightedSetting === 'width' }">
            <label class="form-label">Width</label>
            <input v-model.number="width" type="number" class="form-input" step="8" min="64" max="2048" />
          </div>
          <div class="form-group" data-setting="height" :class="{ 'setting-highlighted': highlightedSetting === 'height' }">
            <label class="form-label">Height</label>
            <input v-model.number="height" type="number" class="form-input" step="8" min="64" max="2048" />
          </div>
        </div>

        <div class="form-row">
          <div class="form-group" data-setting="steps" :class="{ 'setting-highlighted': highlightedSetting === 'steps' }">
            <label class="form-label">
              Steps: {{ steps }}
              <span v-if="recommended && isDifferentFromRecommended('steps', steps)" class="rec-hint">
                (rec: {{ recommended.steps }})
              </span>
            </label>
            <input v-model.number="steps" type="range" class="form-range" min="1" max="150" />
          </div>
          <div class="form-group" data-setting="cfg-scale" :class="{ 'setting-highlighted': highlightedSetting === 'cfg-scale' }">
            <label class="form-label">
              CFG Scale: {{ cfgScale.toFixed(1) }}
              <span v-if="recommended && isDifferentFromRecommended('cfgScale', cfgScale)" class="rec-hint">
                (rec: {{ recommended.cfgScale }})
              </span>
            </label>
            <input v-model.number="cfgScale" type="range" class="form-range" min="1" max="20" step="0.5" />
          </div>
        </div>

        <div class="form-row">
          <div class="form-group" data-setting="sampler" :class="{ 'setting-highlighted': highlightedSetting === 'sampler' }">
            <label class="form-label">
              Sampler
              <span v-if="recommended && isDifferentFromRecommended('sampler', sampler)" class="rec-hint">
                (rec: {{ recommended.sampler }})
              </span>
            </label>
            <select v-model="sampler" class="form-select">
              <option v-for="s in samplers" :key="s" :value="s">{{ s }}</option>
            </select>
          </div>
          <div class="form-group" data-setting="scheduler" :class="{ 'setting-highlighted': highlightedSetting === 'scheduler' }">
            <label class="form-label">
              Scheduler
              <span v-if="recommended && isDifferentFromRecommended('scheduler', scheduler)" class="rec-hint">
                (rec: {{ recommended.scheduler }})
              </span>
            </label>
            <select v-model="scheduler" class="form-select">
              <option v-for="s in schedulers" :key="s" :value="s">{{ s }}</option>
            </select>
          </div>
        </div>

        <div class="form-row">
          <div class="form-group" data-setting="seed" :class="{ 'setting-highlighted': highlightedSetting === 'seed' }">
            <label class="form-label">Seed (-1 = random)</label>
            <div class="input-with-button">
              <input v-model.number="seed" type="number" class="form-input" />
              <button class="btn btn-secondary btn-icon" @click="randomizeSeed" title="Randomize">
                &#127922;
              </button>
            </div>
          </div>
          <div class="form-group" data-setting="batch-count" :class="{ 'setting-highlighted': highlightedSetting === 'batch-count' }">
            <label class="form-label">Batch Count</label>
            <input v-model.number="batchCount" type="number" class="form-input" min="1" max="16" />
          </div>
        </div>

        <!-- txt2vid specific -->
        <div v-if="mode === 'txt2vid'" class="form-row">
          <div class="form-group" data-setting="video-frames" :class="{ 'setting-highlighted': highlightedSetting === 'video-frames' }">
            <label class="form-label">Video Frames</label>
            <input v-model.number="videoFrames" type="number" class="form-input" min="1" max="129" />
          </div>
          <div class="form-group" data-setting="fps" :class="{ 'setting-highlighted': highlightedSetting === 'fps' }">
            <label class="form-label">FPS</label>
            <input v-model.number="fps" type="number" class="form-input" min="1" max="60" />
          </div>
          <div class="form-group">
            <label class="form-label">Flow Shift</label>
            <input v-model.number="flowShift" type="number" class="form-input" step="0.5" min="0" max="10" />
          </div>
        </div>
      </div>

      <!-- Advanced Options -->
      <details class="card accordion">
        <summary class="accordion-header" @click="showAdvanced = !showAdvanced">
          Advanced Options
        </summary>
        <div class="accordion-content">
          <div class="form-row">
            <div class="form-group" data-setting="clip-skip" :class="{ 'setting-highlighted': highlightedSetting === 'clip-skip' }">
              <label class="form-label">
                CLIP Skip (-1 = default)
                <span v-if="recommended && isDifferentFromRecommended('clipSkip', clipSkip)" class="rec-hint">
                  (rec: {{ recommended.clipSkip }})
                </span>
              </label>
              <input v-model.number="clipSkip" type="number" class="form-input" min="-1" max="12" />
            </div>
            <div class="form-group" data-setting="distilled-guidance" :class="{ 'setting-highlighted': highlightedSetting === 'distilled-guidance' }">
              <label class="form-label">
                Distilled Guidance
                <span v-if="recommended && isDifferentFromRecommended('distilledGuidance', distilledGuidance)" class="rec-hint">
                  (rec: {{ recommended.distilledGuidance }})
                </span>
              </label>
              <input v-model.number="distilledGuidance" type="number" class="form-input" step="0.5" min="0" max="10" />
            </div>
          </div>

          <div class="form-group" data-setting="slg-scale" :class="{ 'setting-highlighted': highlightedSetting === 'slg-scale' }">
            <label class="form-label">
              SLG Scale (Skip Layer Guidance)
              <span v-if="recommended?.slgScale !== undefined && isDifferentFromRecommended('slgScale', slgScale)" class="rec-hint">
                (rec: {{ recommended.slgScale }})
              </span>
            </label>
            <input v-model.number="slgScale" type="number" class="form-input" step="0.5" min="0" max="10" />
          </div>

          <label class="form-checkbox" data-setting="easycache" :class="{ 'setting-highlighted': highlightedSetting === 'easycache' }">
            <input v-model="easycache" type="checkbox" />
            Enable EasyCache (faster DiT models)
            <span v-if="recommended?.easycache !== undefined && recommended.easycache !== easycache" class="rec-hint">
              (rec: {{ recommended.easycache ? 'enabled' : 'disabled' }})
            </span>
          </label>

          <div v-if="easycache" class="form-group mt-2">
            <label class="form-label">EasyCache Threshold</label>
            <input v-model.number="easycacheThreshold" type="number" class="form-input" step="0.05" min="0" max="1" />
          </div>

          <label class="form-checkbox" data-setting="vae-tiling" :class="{ 'setting-highlighted': highlightedSetting === 'vae-tiling' }">
            <input v-model="vaeTiling" type="checkbox" />
            VAE Tiling (for large images{{ mode === 'txt2vid' ? '/videos' : '' }})
          </label>

          <div v-if="vaeTiling" class="tiling-options">
            <div class="form-row">
              <div class="form-group" data-setting="vae-tile-size-x" :class="{ 'setting-highlighted': highlightedSetting === 'vae-tile-size-x' }">
                <label class="form-label">Tile Size X (0 = auto)</label>
                <input v-model.number="vaeTileSizeX" type="number" class="form-input" min="0" max="2048" step="64" />
              </div>
              <div class="form-group" data-setting="vae-tile-size-y" :class="{ 'setting-highlighted': highlightedSetting === 'vae-tile-size-y' }">
                <label class="form-label">Tile Size Y (0 = auto)</label>
                <input v-model.number="vaeTileSizeY" type="number" class="form-input" min="0" max="2048" step="64" />
              </div>
            </div>
            <div class="form-group" data-setting="vae-tile-overlap" :class="{ 'setting-highlighted': highlightedSetting === 'vae-tile-overlap' }">
              <label class="form-label">Tile Overlap: {{ vaeTileOverlap.toFixed(2) }}</label>
              <input v-model.number="vaeTileOverlap" type="range" class="form-range" min="0" max="1" step="0.05" />
            </div>
            <div class="form-hint">VAE tiling reduces VRAM usage for high-resolution images/videos by processing in tiles.</div>
          </div>
        </div>
      </details>

      <!-- Submit Button -->
      <div class="submit-section">
        <button
          class="btn btn-primary btn-lg"
          @click="handleSubmit"
          :disabled="submitting || cooldown || !store.modelLoaded"
        >
          <span v-if="submitting" class="spinner"></span>
          {{ submitting ? 'Submitting...' : cooldown ? 'Please wait...' : `Generate ${mode === 'txt2vid' ? 'Video' : 'Image'}` }}
        </button>
        <p v-if="!store.modelLoaded" class="text-error mt-2">
          Please load a model first
        </p>
      </div>

      <!-- Live Preview Section -->
      <div v-if="isCurrentJobProcessing" class="card live-preview-card">
        <div class="card-header">
          <h3 class="card-title">Generating...</h3>
          <span v-if="currentJobProgress" class="progress-text">
            Step {{ currentJobProgress.step }} / {{ currentJobProgress.total_steps }}
          </span>
        </div>
        <div class="live-preview-content">
          <div class="live-preview-image-container">
            <img
              v-if="hasPreviewForCurrentJob && store.currentPreview?.image"
              :src="store.currentPreview.image"
              alt="Generation preview"
              class="live-preview-image"
              @click="openPreviewLightbox"
            />
            <div v-else class="live-preview-placeholder">
              <span class="spinner spinner-lg"></span>
              <span class="placeholder-text">Waiting for preview...</span>
            </div>
          </div>
          <div class="live-preview-info">
            <ProgressBar
              v-if="currentJobProgress"
              :progress="currentJobProgress.total_steps > 0 ? (currentJobProgress.step / currentJobProgress.total_steps) * 100 : 0"
              :show-label="true"
             />
             <p class="preview-hint">
               Configure preview settings in Settings page
             </p>
           </div>
        </div>
      </div>
    </div>

    <!-- Lightbox for preview -->
    <Lightbox
      :show="showLightbox"
      :images="lightboxImages"
      :current-index="lightboxIndex"
      @close="showLightbox = false"
      @update:current-index="lightboxIndex = $event"
    />
  </div>
</template>

<style scoped>
.tabs-container {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 16px;
  margin-bottom: 16px;
  flex-wrap: wrap;
}

.tabs-container .tabs {
  margin-bottom: 0;
}

.copy-menu-container {
  position: relative;
}

.copy-menu {
  position: absolute;
  top: 100%;
  right: 0;
  margin-top: 4px;
  background: var(--bg-secondary);
  border: 1px solid var(--border-color);
  border-radius: var(--border-radius-sm);
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.3);
  z-index: 100;
  min-width: 150px;
  overflow: hidden;
}

.copy-menu-item {
  display: block;
  width: 100%;
  padding: 10px 16px;
  text-align: left;
  background: none;
  border: none;
  color: var(--text-primary);
  font-size: 13px;
  cursor: pointer;
  transition: background-color 0.15s;
}

.copy-menu-item:hover {
  background: var(--bg-tertiary);
}

.copy-menu-item:not(:last-child) {
  border-bottom: 1px solid var(--border-color);
}

.generate {
  max-width: 900px;
  margin: 0 auto;
}

.generate-form {
  max-width: 100%;
}

@media (max-width: 768px) {
  .generate {
    padding: 0;
  }

  .tabs-container {
    flex-direction: column;
    align-items: stretch;
  }

  .tabs-container .tabs {
    overflow-x: auto;
    -webkit-overflow-scrolling: touch;
  }

  .copy-menu-container {
    align-self: flex-end;
  }

  .preset-buttons {
    justify-content: center;
  }
}

.preset-buttons {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
}

.input-with-button {
  display: flex;
  gap: 8px;
}

.input-with-button .form-input {
  flex: 1;
}

.submit-section {
  margin-top: 24px;
  text-align: center;
}

.accordion {
  margin-bottom: 16px;
}

.accordion-header {
  list-style: none;
  cursor: pointer;
  padding: 16px;
  font-weight: 500;
}

.accordion-content {
  padding: 16px;
  border-top: 1px solid var(--border-color);
}

/* Recommended settings panel */
.recommended-panel {
  background: var(--bg-tertiary);
  border: 1px solid var(--border-color);
  border-radius: var(--border-radius-sm);
  padding: 12px;
  margin-bottom: 16px;
}

.recommended-header {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 10px;
}

.recommended-icon {
  font-size: 16px;
}

.recommended-title {
  font-weight: 500;
  color: var(--text-primary);
  font-size: 13px;
}

.recommended-items {
  display: flex;
  flex-wrap: wrap;
  gap: 12px 20px;
}

.rec-item {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  font-size: 12px;
}

.rec-label {
  color: var(--text-secondary);
}

.rec-value {
  color: var(--text-primary);
  font-weight: 500;
  padding: 2px 6px;
  background: var(--bg-secondary);
  border-radius: 3px;
}

.rec-value.rec-different {
  background: var(--warning-bg, rgba(255, 193, 7, 0.15));
  color: var(--warning-color, #ffc107);
  border: 1px solid var(--warning-color, #ffc107);
}

.rec-hint {
  color: var(--warning-color, #ffc107);
  font-size: 11px;
  font-weight: normal;
  margin-left: 6px;
}

.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 12px;
}

/* Preview Mode Selector */
.preview-mode-options {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
}

.mt-4 {
  margin-top: 16px;
}

.tiling-options {
  margin-top: 12px;
  padding: 12px;
  background: var(--bg-tertiary);
  border-radius: var(--border-radius-sm);
  border: 1px solid var(--border-color);
}

/* Live Preview Section */
.live-preview-card {
  margin-top: 24px;
  animation: slideIn 0.3s ease-out;
}

@keyframes slideIn {
  from {
    opacity: 0;
    transform: translateY(-10px);
  }
  to {
    opacity: 1;
    transform: translateY(0);
  }
}

.live-preview-content {
  display: flex;
  gap: 20px;
  padding: 16px;
}

.live-preview-image-container {
  flex-shrink: 0;
  width: 256px;
  min-height: 256px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: var(--bg-tertiary);
  border-radius: var(--border-radius-sm);
  overflow: hidden;
}

.live-preview-image {
  max-width: 100%;
  max-height: 400px;
  object-fit: contain;
  cursor: pointer;
  transition: transform 0.2s;
}

.live-preview-image:hover {
  transform: scale(1.02);
}

.live-preview-placeholder {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 12px;
  color: var(--text-secondary);
  padding: 40px;
}

.placeholder-text {
  font-size: 13px;
}

.spinner-lg {
  width: 32px;
  height: 32px;
}

.live-preview-info {
  flex: 1;
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.progress-text {
  font-size: 13px;
  color: var(--text-secondary);
}

.preview-hint {
  font-size: 12px;
  color: var(--text-tertiary);
  margin: 0;
}

@media (max-width: 600px) {
  .live-preview-content {
    flex-direction: column;
  }

  .live-preview-image-container {
    width: 100%;
    min-height: 200px;
  }

  .preview-mode-options {
    flex-direction: column;
  }

  .preview-mode-options .btn {
    width: 100%;
  }
}

/* Setting highlight animation for assistant navigation */
.setting-highlighted {
  animation: highlight-pulse 3s ease-out;
  border-radius: var(--border-radius);
  position: relative;
}

.setting-highlighted::after {
  content: '';
  position: absolute;
  inset: -4px;
  border-radius: var(--border-radius);
  border: 2px solid var(--accent-primary);
  pointer-events: none;
  animation: highlight-border 3s ease-out;
}

@keyframes highlight-pulse {
  0% {
    background-color: rgba(0, 217, 255, 0.3);
  }
  50% {
    background-color: rgba(0, 217, 255, 0.15);
  }
  100% {
    background-color: transparent;
  }
}

@keyframes highlight-border {
  0% {
    opacity: 1;
  }
  70% {
    opacity: 1;
  }
  100% {
    opacity: 0;
  }
}
</style>
