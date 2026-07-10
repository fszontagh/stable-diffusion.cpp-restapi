<script setup lang="ts">
import { ref, computed, onMounted, onBeforeUnmount, watch, nextTick } from 'vue'
import { useDebounceFn } from '@vueuse/core'
import { useAppStore } from '../stores/app'
import { api, type GenerationParams, type Img2ImgParams, type Txt2VidParams, type LoraEntry, type LoraSettings, type OptionDescription } from '../api/client'
import RecHint from '../components/RecHint.vue'
import ImageUploader from '../components/ImageUploader.vue'
import ProgressBar from '../components/ProgressBar.vue'
import Lightbox from '../components/Lightbox.vue'
import LoadedModelPanel from '../components/LoadedModelPanel.vue'
import HighlightedPrompt from '../components/HighlightedPrompt.vue'
import LoraPanel from '../components/LoraPanel.vue'
import { hasTemplateSyntax, expandPrompt as expandPromptUtil } from '../utils/promptTemplate'
import { openExpandPromptConfirm } from '../composables/useExpandPromptModal'
import { findPresetForArchitecture } from '../composables/useArchitectures'

const store = useAppStore()

// Per-field documentation fetched from /options/generation. The same JSON
// drives the API.md doc tables and the WebUI tooltips, so descriptions
// stay in lockstep across surfaces.
const genOptionDescriptions = ref<Record<string, OptionDescription>>({})
// Returns just the description text — for use as a :title tooltip.
function genOpt(key: string): string | undefined {
  return genOptionDescriptions.value[key]?.description
}
// Returns the full descriptor object — for use with <RecHint> which
// reads .recommended off it.
function genOptDesc(key: string): OptionDescription | undefined {
  return genOptionDescriptions.value[key]
}

// Highlighted setting for assistant navigation
const highlightedSetting = ref<string | null>(null)
let highlightTimeout: ReturnType<typeof setTimeout> | null = null

const mode = ref<'txt2img' | 'img2img' | 'img_edit' | 'txt2vid'>('txt2img')
const submitting = ref(false)
const cooldown = ref(false)
let cooldownTimer: ReturnType<typeof setTimeout> | null = null

// Prompt-expansion (a1111-style {a|b|c} / {N$$a|b|c} dynamic prompts).
// On by default — when the prompt contains template syntax we want users
// to get all variations. Users can flip the toggle off if they want the
// braces treated as literal text instead, and the choice is remembered
// across page navigation (a session-scoped UX preference, not a per-mode
// generation parameter, so it lives in its own localStorage key).
const EXPAND_PROMPT_STORAGE_KEY = 'generateSettings_expandPrompt'
const expandPromptEnabled = ref(
  // Read once at module load. localStorage may be unavailable in some
  // private-browsing modes — fall back to the default in that case.
  (() => {
    try {
      const v = localStorage.getItem(EXPAND_PROMPT_STORAGE_KEY)
      if (v === null) return true   // never set -> use the default
      return v === '1'
    } catch {
      return true
    }
  })()
)

// Prompt persistence keys (per-mode to keep prompts separate across tabs)
function getPromptStorageKey(m: string) { return `generateSettings_prompt_${m}` }
function getNegativePromptStorageKey(m: string) { return `generateSettings_negativePrompt_${m}` }
const INIT_IMAGE_STORAGE_KEY = 'generateSettings_initImage'
const MASK_IMAGE_STORAGE_KEY = 'generateSettings_maskImage'
const CONTROL_IMAGE_STORAGE_KEY = 'generateSettings_controlImage'
const REF_IMAGE_STORAGE_KEY = 'generateSettings_refImage'

// Common params
const prompt = ref('')
const title = ref('')
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

// img_edit params (separate from initImage)
const refImage = ref<string | undefined>()

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
const cacheMode = ref('')
const easycacheThreshold = ref(0.2)
// TaylorSeer (cache_mode='taylorseer')
const taylorseerNDerivatives = ref(2)
const taylorseerSkipInterval = ref(0)
const spectrumW = ref(0.5)
const spectrumM = ref(5)
const spectrumLam = ref(0.5)
const spectrumWindowSize = ref(3)
const spectrumFlexWindow = ref(0.5)
const spectrumWarmupSteps = ref(2)
const spectrumStopPercent = ref(0.8)
const vaeTiling = ref(false)
const vaeTileSizeX = ref(0)
const vaeTileSizeY = ref(0)
const vaeTileOverlap = ref(0.5)
const vaeTileRelSizeX = ref(0.0)
const vaeTileRelSizeY = ref(0.0)

// Advanced Guidance (sd_guidance_params_t passthrough)
const imgCfgScale = ref(-1)         // -1 = inherit cfg_scale
const extraSampleArgs = ref('')      // key=value pass-through

// PuLID-Flux (sd_pulid_params_t)
const pulidIdEmbeddingPath = ref('')
const pulidIdWeight = ref(1.0)

// sd.cpp native hi-res-fix (sd_hires_params_t)
// Distinct from the post-gen ESRGAN upscale (state.upscale + upscaler model).
const hiresEnabled = ref(false)
const hiresUpscaler = ref('model')
const hiresModelPath = ref('')
const hiresScale = ref(2.0)
const hiresTargetWidth = ref(0)
const hiresTargetHeight = ref(0)
const hiresSteps = ref(0)
const hiresDenoisingStrength = ref(0.4)
const hiresUpscaleTileSize = ref(0)

// Circular RoPE / tileable position embeddings — moved from load params to
// per-generation in leejet PR #1748. Toggling doesn't need an unload+reload
// anymore. Independent axes so users can pick horizontal-only, vertical-only,
// or fully-tileable output.
const circularX = ref(false)
const circularY = ref(false)
// Qwen-Image layered rendering (leejet PR #1119). Image path only.
const qwenImageLayers = ref(0)

// High-noise pass extras (txt2vid only). The shared `high_noise_*` knobs
// (steps/cfg/sampler/distilled_guidance/slg_*) are not modeled in UI yet,
// but parity is reached for the brand-new fields listed below.
const highNoiseImgCfg = ref(-1)
const highNoiseScheduler = ref('')
const highNoiseEta = ref(0)
const highNoiseShiftedTimestep = ref(0)
const highNoiseFlowShift = ref(0)
const highNoiseExtraSampleArgs = ref('')
// custom_sigmas is a comma-separated list in the UI; we parse to number[]
// at submit time.
const highNoiseCustomSigmas = ref('')

// Flag to suppress prompt restoration during queue reload
let suppressPromptRestore = false

// Copy settings dropdown
const showCopyMenu = ref(false)

// LoRA state
const positiveLoraList = ref<LoraEntry[]>([])
const negativeLoraList = ref<LoraEntry[]>([])
const loraSettings = ref<LoraSettings>({
  defaultWeight: 1.0,
  minWeight: -1.0,
  maxWeight: 1.0
})

// LoRA prompt parsing
const LORA_REGEX = /<lora:([^:>]+):([^>]+)>/g

const availableLoras = computed(() => store.models?.loras || [])
// ESRGAN upscaler list — used by the Hi-Res Fix model selector. Matches
// the pattern Upscale.vue uses for its own load dropdown.
const esrganModels = computed(() => store.models?.esrgan || [])

// Build a map of base names (without extension) to full names for matching
const loraNameMap = computed(() => {
  const map = new Map<string, string>()
  for (const lora of availableLoras.value) {
    // Add full name
    map.set(lora.name, lora.name)
    // Add name without extension (e.g., "add_detail.safetensors" -> "add_detail")
    const baseName = lora.name.replace(/\.(safetensors|gguf|pt|pth|ckpt)$/i, '')
    if (!map.has(baseName)) {
      map.set(baseName, lora.name)
    }
  }
  return map
})

// Check if a LoRA name is valid (matches full name or base name without extension)
function isValidLoraName(name: string): boolean {
  return loraNameMap.value.has(name)
}

// Get the canonical (full) name for a LoRA
function getCanonicalLoraName(name: string): string {
  return loraNameMap.value.get(name) || name
}

// Invalid LoRAs in prompts (for warning display)
const invalidLorasInPositivePrompt = computed(() => {
  const matches = prompt.value.matchAll(LORA_REGEX)
  const invalid: string[] = []
  for (const match of matches) {
    if (!isValidLoraName(match[1])) {
      invalid.push(match[1])
    }
  }
  return invalid
})

const invalidLorasInNegativePrompt = computed(() => {
  const matches = negativePrompt.value.matchAll(LORA_REGEX)
  const invalid: string[] = []
  for (const match of matches) {
    if (!isValidLoraName(match[1])) {
      invalid.push(match[1])
    }
  }
  return invalid
})

// Live preview of how many variations the current prompt expands to. Surfaced
// next to the "Expand prompt" toggle so the user sees the impact before they
// click Generate. count = null when there's no template syntax; error is
// non-null when the syntax is malformed (e.g. unterminated brace).
const promptTemplateInfo = computed<{
  hasSyntax: boolean
  count: number | null
  error: string | null
}>(() => {
  const has = hasTemplateSyntax(prompt.value)
  if (!has) return { hasSyntax: false, count: null, error: null }
  try {
    const variations = expandPromptUtil(prompt.value)
    return { hasSyntax: true, count: variations.length, error: null }
  } catch (e) {
    return {
      hasSyntax: true,
      count: null,
      error: e instanceof Error ? e.message : String(e)
    }
  }
})

// Parse LoRAs from prompt and add to list
function parseLorasFromPrompt(promptText: string, currentList: LoraEntry[]): { cleanedPrompt: string; newLoras: LoraEntry[] } {
  const existingNames = new Set(currentList.map(l => l.name))
  const newLoras: LoraEntry[] = []

  const cleanedPrompt = promptText.replace(LORA_REGEX, (match, name, weight) => {
    if (isValidLoraName(name)) {
      const canonicalName = getCanonicalLoraName(name)
      if (!existingNames.has(canonicalName)) {
        const weightNum = parseFloat(weight)
        newLoras.push({
          name: canonicalName,
          weight: isNaN(weightNum) ? loraSettings.value.defaultWeight : weightNum,
          isValid: true
        })
        existingNames.add(canonicalName) // Prevent duplicates in same parse
      }
      return '' // Remove from prompt
    }
    return match // Keep invalid LoRAs in prompt
  })

  return { cleanedPrompt: cleanedPrompt.replace(/\s+/g, ' ').trim(), newLoras }
}

// Debounced prompt parsing
const debouncedParsePositivePrompt = useDebounceFn(() => {
  if (!prompt.value) return
  const { cleanedPrompt, newLoras } = parseLorasFromPrompt(prompt.value, positiveLoraList.value)
  if (newLoras.length > 0) {
    prompt.value = cleanedPrompt
    positiveLoraList.value = [...positiveLoraList.value, ...newLoras]
  }
}, 300)

const debouncedParseNegativePrompt = useDebounceFn(() => {
  if (!negativePrompt.value) return
  const { cleanedPrompt, newLoras } = parseLorasFromPrompt(negativePrompt.value, negativeLoraList.value)
  if (newLoras.length > 0) {
    negativePrompt.value = cleanedPrompt
    negativeLoraList.value = [...negativeLoraList.value, ...newLoras]
  }
}, 300)

// Watch prompts for LoRA tags
watch(prompt, () => {
  debouncedParsePositivePrompt()
})

watch(negativePrompt, () => {
  debouncedParseNegativePrompt()
})

// Flag to track if we need to retry LoRA parsing when models become available
const pendingLoraParseFromJobReload = ref(false)

// Watch for models to become available and retry LoRA parsing if needed
// This handles the case where job params are loaded before the models list is fetched
watch(availableLoras, (newLoras) => {
  if (pendingLoraParseFromJobReload.value && newLoras.length > 0) {
    pendingLoraParseFromJobReload.value = false
    // Immediately parse LoRAs now that models are available (no debounce)
    if (prompt.value) {
      const { cleanedPrompt, newLoras: posLoras } = parseLorasFromPrompt(prompt.value, positiveLoraList.value)
      if (posLoras.length > 0) {
        prompt.value = cleanedPrompt
        positiveLoraList.value = [...positiveLoraList.value, ...posLoras]
      }
    }
    if (negativePrompt.value) {
      const { cleanedPrompt, newLoras: negLoras } = parseLorasFromPrompt(negativePrompt.value, negativeLoraList.value)
      if (negLoras.length > 0) {
        negativePrompt.value = cleanedPrompt
        negativeLoraList.value = [...negativeLoraList.value, ...negLoras]
      }
    }
  }
})

// Debounced prompt persistence to localStorage (per-mode)
const debouncedSavePrompt = useDebounceFn(() => {
  localStorage.setItem(getPromptStorageKey(mode.value), prompt.value)
}, 500)

const debouncedSaveNegativePrompt = useDebounceFn(() => {
  localStorage.setItem(getNegativePromptStorageKey(mode.value), negativePrompt.value)
}, 500)

// Watch prompts for localStorage persistence
watch(prompt, debouncedSavePrompt)
watch(negativePrompt, debouncedSaveNegativePrompt)

// Persist the expand-prompt toggle so user-driven off-state survives
// navigation. Not debounced — it's a single boolean, write is cheap.
watch(expandPromptEnabled, (v) => {
  try { localStorage.setItem(EXPAND_PROMPT_STORAGE_KEY, v ? '1' : '0') } catch {}
})

// Debounced image persistence to sessionStorage (separate from prompts due to size)
const debouncedSaveInitImage = useDebounceFn(() => {
  if (initImage.value) {
    try {
      sessionStorage.setItem(INIT_IMAGE_STORAGE_KEY, initImage.value)
    } catch (e) {
      // Storage quota exceeded - just skip
      console.warn('Failed to persist init image:', e)
    }
  } else {
    sessionStorage.removeItem(INIT_IMAGE_STORAGE_KEY)
  }
}, 500)

const debouncedSaveMaskImage = useDebounceFn(() => {
  if (maskImage.value) {
    try {
      sessionStorage.setItem(MASK_IMAGE_STORAGE_KEY, maskImage.value)
    } catch (e) {
      console.warn('Failed to persist mask image:', e)
    }
  } else {
    sessionStorage.removeItem(MASK_IMAGE_STORAGE_KEY)
  }
}, 500)

const debouncedSaveControlImage = useDebounceFn(() => {
  if (controlImage.value) {
    try {
      sessionStorage.setItem(CONTROL_IMAGE_STORAGE_KEY, controlImage.value)
    } catch (e) {
      console.warn('Failed to persist control image:', e)
    }
  } else {
    sessionStorage.removeItem(CONTROL_IMAGE_STORAGE_KEY)
  }
}, 500)

const debouncedSaveRefImage = useDebounceFn(() => {
  if (refImage.value) {
    try {
      sessionStorage.setItem(REF_IMAGE_STORAGE_KEY, refImage.value)
    } catch (e) {
      console.warn('Failed to persist ref image:', e)
    }
  } else {
    sessionStorage.removeItem(REF_IMAGE_STORAGE_KEY)
  }
}, 500)

// Watch images for sessionStorage persistence
watch(initImage, debouncedSaveInitImage)
watch(maskImage, debouncedSaveMaskImage)
watch(controlImage, debouncedSaveControlImage)
watch(refImage, debouncedSaveRefImage)

// Build final prompt with LoRAs
function buildPromptWithLoras(basePrompt: string, loras: LoraEntry[]): string {
  const validLoras = loras.filter(l => l.isValid)
  if (validLoras.length === 0) return basePrompt

  const loraStrings = validLoras.map(l => `<lora:${l.name}:${l.weight}>`)
  return `${basePrompt} ${loraStrings.join(' ')}`
}

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
    cacheMode: cacheMode.value,
    easycacheThreshold: easycacheThreshold.value,
    taylorseerNDerivatives: taylorseerNDerivatives.value,
    taylorseerSkipInterval: taylorseerSkipInterval.value,
    spectrumW: spectrumW.value,
    vaeTiling: vaeTiling.value,
    vaeTileSizeX: vaeTileSizeX.value,
    vaeTileSizeY: vaeTileSizeY.value,
    vaeTileOverlap: vaeTileOverlap.value,
    vaeTileRelSizeX: vaeTileRelSizeX.value,
    vaeTileRelSizeY: vaeTileRelSizeY.value,
    imgCfgScale: imgCfgScale.value,
    extraSampleArgs: extraSampleArgs.value,
    pulidIdEmbeddingPath: pulidIdEmbeddingPath.value,
    pulidIdWeight: pulidIdWeight.value,
    hiresEnabled: hiresEnabled.value,
    hiresUpscaler: hiresUpscaler.value,
    hiresModelPath: hiresModelPath.value,
    hiresScale: hiresScale.value,
    hiresTargetWidth: hiresTargetWidth.value,
    hiresTargetHeight: hiresTargetHeight.value,
    hiresSteps: hiresSteps.value,
    hiresDenoisingStrength: hiresDenoisingStrength.value,
    hiresUpscaleTileSize: hiresUpscaleTileSize.value,
    circularX: circularX.value,
    circularY: circularY.value,
    qwenImageLayers: qwenImageLayers.value,
    highNoiseImgCfg: highNoiseImgCfg.value,
    highNoiseScheduler: highNoiseScheduler.value,
    highNoiseEta: highNoiseEta.value,
    highNoiseShiftedTimestep: highNoiseShiftedTimestep.value,
    highNoiseFlowShift: highNoiseFlowShift.value,
    highNoiseExtraSampleArgs: highNoiseExtraSampleArgs.value,
    highNoiseCustomSigmas: highNoiseCustomSigmas.value
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
  if (settings.cacheMode !== undefined) cacheMode.value = settings.cacheMode as string
  if (settings.easycache !== undefined && settings.cacheMode === undefined) {
    cacheMode.value = settings.easycache ? 'easycache' : ''
  }
  if (settings.easycacheThreshold !== undefined) easycacheThreshold.value = settings.easycacheThreshold as number
  if (settings.taylorseerNDerivatives !== undefined) taylorseerNDerivatives.value = settings.taylorseerNDerivatives as number
  if (settings.taylorseerSkipInterval !== undefined) taylorseerSkipInterval.value = settings.taylorseerSkipInterval as number
  if (settings.spectrumW !== undefined) spectrumW.value = settings.spectrumW as number
  if (settings.vaeTiling !== undefined) vaeTiling.value = settings.vaeTiling as boolean
  if (settings.vaeTileSizeX !== undefined) vaeTileSizeX.value = settings.vaeTileSizeX as number
  if (settings.vaeTileSizeY !== undefined) vaeTileSizeY.value = settings.vaeTileSizeY as number
  if (settings.vaeTileOverlap !== undefined) vaeTileOverlap.value = settings.vaeTileOverlap as number
  if (settings.vaeTileRelSizeX !== undefined) vaeTileRelSizeX.value = settings.vaeTileRelSizeX as number
  if (settings.vaeTileRelSizeY !== undefined) vaeTileRelSizeY.value = settings.vaeTileRelSizeY as number
  if (settings.imgCfgScale !== undefined) imgCfgScale.value = settings.imgCfgScale as number
  if (settings.extraSampleArgs !== undefined) extraSampleArgs.value = settings.extraSampleArgs as string
  if (settings.pulidIdEmbeddingPath !== undefined) pulidIdEmbeddingPath.value = settings.pulidIdEmbeddingPath as string
  if (settings.pulidIdWeight !== undefined) pulidIdWeight.value = settings.pulidIdWeight as number
  if (settings.hiresEnabled !== undefined) hiresEnabled.value = settings.hiresEnabled as boolean
  if (settings.hiresUpscaler !== undefined) hiresUpscaler.value = settings.hiresUpscaler as string
  if (settings.hiresModelPath !== undefined) hiresModelPath.value = settings.hiresModelPath as string
  if (settings.hiresScale !== undefined) hiresScale.value = settings.hiresScale as number
  if (settings.hiresTargetWidth !== undefined) hiresTargetWidth.value = settings.hiresTargetWidth as number
  if (settings.hiresTargetHeight !== undefined) hiresTargetHeight.value = settings.hiresTargetHeight as number
  if (settings.hiresSteps !== undefined) hiresSteps.value = settings.hiresSteps as number
  if (settings.hiresDenoisingStrength !== undefined) hiresDenoisingStrength.value = settings.hiresDenoisingStrength as number
  if (settings.hiresUpscaleTileSize !== undefined) hiresUpscaleTileSize.value = settings.hiresUpscaleTileSize as number
  if (settings.circularX !== undefined) circularX.value = settings.circularX as boolean
  if (settings.circularY !== undefined) circularY.value = settings.circularY as boolean
  if (settings.qwenImageLayers !== undefined) qwenImageLayers.value = settings.qwenImageLayers as number
  if (settings.highNoiseImgCfg !== undefined) highNoiseImgCfg.value = settings.highNoiseImgCfg as number
  if (settings.highNoiseScheduler !== undefined) highNoiseScheduler.value = settings.highNoiseScheduler as string
  if (settings.highNoiseEta !== undefined) highNoiseEta.value = settings.highNoiseEta as number
  if (settings.highNoiseShiftedTimestep !== undefined) highNoiseShiftedTimestep.value = settings.highNoiseShiftedTimestep as number
  if (settings.highNoiseFlowShift !== undefined) highNoiseFlowShift.value = settings.highNoiseFlowShift as number
  if (settings.highNoiseExtraSampleArgs !== undefined) highNoiseExtraSampleArgs.value = settings.highNoiseExtraSampleArgs as string
  if (settings.highNoiseCustomSigmas !== undefined) highNoiseCustomSigmas.value = settings.highNoiseCustomSigmas as string
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

  // img_edit shares txt2img backend settings
  const backendMode = mode.value === 'img_edit' ? 'txt2img' : mode.value

  // Optimistic local cache update: mirror the freshly-configured values into
  // store.generationDefaults BEFORE the HTTP PUT lands. Without this, the
  // "navigate to Queue and back" flow re-reads store.generationDefaults on
  // remount and sees stale values — the user's configured settings look
  // "lost" even though the backend save fired at onBeforeUnmount. Same
  // pattern saveLoraSettings uses (see line 705 comment).
  if (store.generationDefaults) {
    const existing = (store.generationDefaults[backendMode] as Record<string, unknown>) || {}
    store.generationDefaults = {
      ...store.generationDefaults,
      [backendMode]: { ...existing, ...(toSave as Record<string, unknown>) }
    }
  }

  try {
    await api.updateGenerationDefaultsForMode(backendMode, toSave as any)
    // Silently save, don't show toast for each change
  } catch (e) {
    console.error('Failed to save settings:', e)
  }
}

// Load settings for a specific mode from backend
async function loadSettingsForMode(targetMode: 'txt2img' | 'img2img' | 'img_edit' | 'txt2vid') {
  // img_edit shares txt2img backend settings
  const backendMode = targetMode === 'img_edit' ? 'txt2img' : targetMode
  try {
    // Get architecture defaults from current model
    const architectureDefaults = getArchitectureDefaultsForMode()

    // Get user preferences from backend
    const userPrefs = (store.generationDefaults?.[backendMode] as Record<string, unknown>) || {}

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
async function copySettingsFrom(sourceMode: 'txt2img' | 'img2img' | 'img_edit' | 'txt2vid') {
  // img_edit shares txt2img backend settings
  const backendMode = sourceMode === 'img_edit' ? 'txt2img' : sourceMode
  try {
    const userPrefs = (store.generationDefaults?.[backendMode] as Record<string, unknown>) || {}
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
    img_edit: 'Image Edit',
    txt2vid: 'Text to Video'
  }
  return labels[m] || m
}

// Get other modes for copy menu
const otherModes = computed(() => {
  const modes: Array<'txt2img' | 'img2img' | 'img_edit' | 'txt2vid'> = ['txt2img', 'img2img', 'img_edit', 'txt2vid']
  return modes.filter(m => m !== mode.value)
})

// Watch for mode changes - save current, load new
// During queue reload (suppressPromptRestore=true), loadJobParams handles all settings
// so we skip both saving old-mode values and loading new-mode values
watch(mode, async (newMode, oldMode) => {
  // Persist selected mode always
  localStorage.setItem('generateSettings_lastMode', newMode)

  if (suppressPromptRestore) return

  if (oldMode) {
    // Save prompts and settings for old mode before switching
    localStorage.setItem(getPromptStorageKey(oldMode), prompt.value)
    localStorage.setItem(getNegativePromptStorageKey(oldMode), negativePrompt.value)
    await saveSettings()
    // Save the OLD mode's LoRA selection BEFORE we change anything — using
    // mode.value here would write the OLD selection into the NEW mode's slot
    // because mode.value has already been updated by the time the watcher
    // fires.
    await saveLoraSettings(oldMode as 'txt2img' | 'img2img' | 'img_edit' | 'txt2vid')
  }
  // Load settings for new mode
  await loadSettingsForMode(newMode)
  // img_edit shares txt2img LoRA settings
  const loraMode = newMode === 'img_edit' ? 'txt2img' : newMode
  loadLoraSettingsForMode(loraMode)
  // Restore prompts for new mode
  const savedPrompt = localStorage.getItem(getPromptStorageKey(newMode))
  const savedNegativePrompt = localStorage.getItem(getNegativePromptStorageKey(newMode))
  prompt.value = savedPrompt ?? ''
  negativePrompt.value = savedNegativePrompt ?? ''
})

// Debounced save for settings (500ms delay)
const debouncedSaveSettings = useDebounceFn(() => {
  saveSettings()
}, 500)

// True while loadLoraSettingsForMode() is mutating the lora refs. The
// `watch([positiveLoraList, ...], debouncedSaveLoraSettings)` would otherwise
// fire as a consequence of the load and schedule a save with stale values.
let isLoadingLoras = false

// LoRA settings persistence. Takes an optional explicit mode so the
// mode-change watcher can save against the OLD mode (avoid clobbering the
// new mode's slot with the previous mode's LoRA selection).
async function saveLoraSettings(forMode?: 'txt2img' | 'img2img' | 'img_edit' | 'txt2vid') {
  const currentPrefs = store.uiPreferences || {} as any
  const loraLists = (currentPrefs as any).lora_lists || {}

  // img_edit shares txt2img LoRA settings
  const target = forMode ?? mode.value
  const loraMode = target === 'img_edit' ? 'txt2img' : target
  const newPrefs = {
    ...currentPrefs,
    lora_settings: loraSettings.value,
    lora_lists: {
      ...loraLists,
      [loraMode]: {
        positive: positiveLoraList.value,
        negative: negativeLoraList.value
      }
    }
  }

  // Optimistic local cache update so a remount during the in-flight write
  // (e.g. navigating to Queue and back) sees the new lora_lists immediately.
  // Without this the next loadLoraSettingsForMode() reads stale store data
  // and the watcher then writes that stale data back, dropping the LoRA.
  store.uiPreferences = newPrefs

  try {
    await api.updateUIPreferences(newPrefs)
  } catch (e) {
    console.error('Failed to save LoRA settings:', e)
  }
}

function loadLoraSettingsForMode(targetMode: 'txt2img' | 'img2img' | 'txt2vid') {
  isLoadingLoras = true
  try {
    const prefs = store.uiPreferences
    if (prefs?.lora_settings) {
      loraSettings.value = { ...loraSettings.value, ...prefs.lora_settings }
    }
    const stored = prefs?.lora_lists?.[targetMode]
    if (stored) {
      positiveLoraList.value = stored.positive ?? []
      negativeLoraList.value = stored.negative ?? []
    } else {
      // No persisted entry for this mode yet. Don't overwrite whatever's
      // currently selected — that would clobber an in-progress edit on a
      // mode switch. Only zero-out if both lists are already empty (fresh
      // mount with no prior selection).
      if (positiveLoraList.value.length === 0 && negativeLoraList.value.length === 0) {
        positiveLoraList.value = []
        negativeLoraList.value = []
      }
    }
  } finally {
    // Defer the flag flip past the next tick so any cascading reactivity
    // settles before the watcher is re-armed.
    nextTick(() => { isLoadingLoras = false })
  }
}

const debouncedSaveLoraSettings = useDebounceFn(() => {
  saveLoraSettings()
}, 500)

// Watch for generation parameter changes with debounce
watch([
  width, height, steps, cfgScale, distilledGuidance, seed, sampler,
  scheduler, batchCount, clipSkip, strength, controlStrength, videoFrames,
  fps, flowShift, slgScale, cacheMode, easycacheThreshold,
  taylorseerNDerivatives, taylorseerSkipInterval, spectrumW, vaeTiling,
  vaeTileSizeX, vaeTileSizeY, vaeTileOverlap, vaeTileRelSizeX, vaeTileRelSizeY,
  imgCfgScale, extraSampleArgs,
  pulidIdEmbeddingPath, pulidIdWeight,
  hiresEnabled, hiresUpscaler, hiresModelPath, hiresScale,
  hiresTargetWidth, hiresTargetHeight, hiresSteps,
  hiresDenoisingStrength, hiresUpscaleTileSize,
  highNoiseImgCfg, highNoiseScheduler, highNoiseEta,
  highNoiseShiftedTimestep, highNoiseFlowShift,
  highNoiseExtraSampleArgs, highNoiseCustomSigmas
], debouncedSaveSettings)

// Watch for LoRA list and settings changes. Suppress while loading from
// store — otherwise the load itself triggers a save that races against the
// load and can write empty lists back into the store.
watch([positiveLoraList, negativeLoraList, loraSettings], () => {
  if (isLoadingLoras) return
  debouncedSaveLoraSettings()
}, { deep: true })

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
    : ['euler', 'euler_a', 'heun', 'dpm2', 'dpm++2s_a', 'dpm++2m', 'dpm++2mv2', 'ipndm', 'ipndm_v', 'lcm', 'ddim_trailing', 'tcd', 'res_multistep', 'res_2s', 'er_sde', 'euler_cfg_pp', 'euler_a_cfg_pp', 'euler_ge']
)

const schedulers = computed(() =>
  store.schedulers.length > 0
    ? store.schedulers
    : ['discrete', 'karras', 'exponential', 'ays', 'gits', 'sgm_uniform', 'simple', 'smoothstep', 'kl_optimal', 'lcm', 'bong_tangent', 'ltx2']
)

// Recommended settings per model architecture - now loaded from architecture JSON
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
  cacheMode?: string
}

// Resolve the current architecture preset using JSON-driven match rules
// (preset.match.architecture + preset.match.nameRegex) defined in
// model_architectures.json. When sd.cpp reports e.g. "SeFi-Image" and
// multiple presets claim that architecture, the nameRegex against the
// loaded model's filename disambiguates (Turbo / Base / RL pick the right
// defaults without any hardcoded WebUI logic). Presets without `match`
// keep working via direct key lookup — fully backward-compatible.
const currentArchPreset = computed(() =>
  findPresetForArchitecture(
    store.modelArchitecture,
    store.modelName,
    store.architectures?.architectures
  )
)

// Image Edit mode ("edit this reference image" — sd-cli's `-r`) is only
// supported by DiT / flow-based architectures that carry a ref_images path
// (Flux Kontext, Flux2, Z-Image, Qwen Image Edit, etc.). The classic
// U-Net families (SD1/SD2/SDXL/LCM/SSD-1B/…) don't handle ref_images — for
// those the img2img flow with an init image is the "edit" primitive.
//
// Data-driven: check whether the current preset declares an imageEditMode
// in model_architectures.json. Empty / absent = tab stays visible but
// disabled with a hint, so the user can see it exists and understand why
// it's off. If no model is loaded yet we don't know, so leave enabled.
const supportsImageEdit = computed<boolean>(() => {
  const preset = currentArchPreset.value
  if (!preset) return true  // no preset yet — don't preemptively disable
  return Boolean(preset.imageEditMode)
})

// Tooltip displayed on hover of the disabled Image Edit tab. Kept as a
// separate computed so the string can contain apostrophes without
// escaping in the template attribute (Vue's SFC parser rejects
// backslash-escaped single quotes inside a double-quoted :attr).
const imageEditTabTooltip = computed<string>(() => {
  if (supportsImageEdit.value) return ''
  return 'The loaded model does not support ref-image editing. Use Image to Image with an init image instead — that is what sd-cli’s -r does for the classic U-Net families (SD1/SD2/SDXL/LCM).'
})


// Get recommended settings for current model from architecture JSON
const recommended = computed((): RecommendedSettings | null => {
  const archData = currentArchPreset.value
  if (!archData?.generationDefaults) return null

  const defaults = archData.generationDefaults as {
    sampler?: string
    scheduler?: string
    steps?: number
    cfg_scale?: number
    distilled_guidance?: number
    clip_skip?: number
    width?: number
    height?: number
    slg_scale?: number
    cache_mode?: string
    easycache?: boolean
  }
  return {
    sampler: defaults.sampler || 'euler',
    scheduler: defaults.scheduler || 'discrete',
    steps: defaults.steps || 20,
    cfgScale: defaults.cfg_scale ?? 7.0,
    distilledGuidance: defaults.distilled_guidance ?? 0.0,
    clipSkip: defaults.clip_skip ?? -1,
    width: defaults.width || 1024,
    height: defaults.height || 1024,
    slgScale: defaults.slg_scale,
    cacheMode: defaults.cache_mode || (defaults.easycache ? 'easycache' : undefined)
  }
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
  if (recommended.value.cacheMode !== undefined) {
    cacheMode.value = recommended.value.cacheMode
  }

  store.showToast('Applied recommended settings', 'success')
}

// Apply a single recommended setting by field name
function applyRecommendedSetting(field: keyof RecommendedSettings) {
  if (!recommended.value) return
  const val = recommended.value[field]
  if (val === undefined) return
  const setters: Record<string, () => void> = {
    sampler: () => { sampler.value = val as string },
    scheduler: () => { scheduler.value = val as string },
    steps: () => { steps.value = val as number },
    cfgScale: () => { cfgScale.value = val as number },
    distilledGuidance: () => { distilledGuidance.value = val as number },
    clipSkip: () => { clipSkip.value = val as number },
    slgScale: () => { slgScale.value = val as number },
    cacheMode: () => { cacheMode.value = val as string },
  }
  setters[field]?.()
}

// Apply recommended width and height together
function applyRecommendedSize() {
  if (!recommended.value) return
  width.value = recommended.value.width
  height.value = recommended.value.height
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
    cacheMode: { get: () => cacheMode.value, set: (v) => cacheMode.value = v as string },
    cache_mode: { get: () => cacheMode.value, set: (v) => cacheMode.value = v as string },
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
    cacheMode: 'cache-mode',
    cache_mode: 'cache-mode',
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

  // Fire-and-forget: load per-field descriptions for the input tooltips.
  // Failure is fine (silent fallback to no tooltips); the API client
  // caches the result for 10 minutes so navigating to /generate twice
  // doesn't re-fetch.
  api.getGenerationOptionDescriptions()
    .then(r => { genOptionDescriptions.value = r.options ?? {} })
    .catch(() => { /* silent — degrades to no tooltips */ })

  // Refetch architectures unconditionally so the "Recommended settings"
  // strip (and currentArchPreset / recommended computeds) pick up presets
  // added on the backend after the WebUI was first loaded — e.g. a server
  // upgrade that adds a new architecture (SeFi-Image, etc.). Without this,
  // an open tab from before the upgrade keeps showing the stale preset
  // list and a freshly-loaded model whose architecture exists ONLY in the
  // new preset list reports no recommended defaults.
  store.fetchArchitectures().catch(() => { /* silent — degrades to no recs */ })

  // Force-refresh /health on mount so `loaded_components` reflects the
  // currently-loaded model even if the ModelLoaded WS event fired before
  // this tab was opened (or the WS was briefly disconnected). Without this,
  // navigating Model Load → Generate immediately after a load could see a
  // stale `loaded_components.controlnet` and the ControlNet input widget
  // wouldn't appear until the user manually refreshed.
  store.fetchHealth().catch(() => { /* silent — computeds fall through to null */ })

  // First check if we have reloaded job params from Queue view
  const savedParams = sessionStorage.getItem('reloadJobParams')
  if (savedParams) {
    try {
      const data = JSON.parse(savedParams)
      // Optional section selection from the selective-reload modal. When
      // missing or empty, full-reload behavior (legacy) is preserved.
      const selection: Set<ReloadSection> | undefined = Array.isArray(data.sections) && data.sections.length > 0
        ? new Set(data.sections as ReloadSection[])
        : undefined
      // Suppress prompt restore from localStorage only if 'prompt' or 'negative'
      // is being loaded — otherwise the user's currently-typed prompt should
      // survive a settings-only reload.
      const willTouchPrompt = !selection || selection.has('prompt')
      const willTouchNegative = !selection || selection.has('negative')
      if (willTouchPrompt || willTouchNegative) {
        suppressPromptRestore = true
      }
      if (data.type === 'txt2img' || data.type === 'img2img' || data.type === 'txt2vid') {
        // loadJobParams will set the correct mode (including img_edit for ref_images jobs)
        // Set the base mode here; loadJobParams below may override to img_edit.
        // For partial reloads that don't include 'settings'/'advanced', skip
        // the mode flip so the user stays on whatever tab they were on.
        if (!selection || selection.has('settings') || selection.has('advanced')) {
          mode.value = data.type as 'txt2img' | 'img2img' | 'txt2vid'
        }
      }
      // For partial reloads (selection defined) we restore the user's
      // last-used settings for the current mode FIRST, so unselected
      // sections show the user's actual prefs from localStorage rather
      // than the hardcoded ref() defaults that a fresh component mount
      // would otherwise display. loadJobParams then only overwrites the
      // selected sections — e.g. prompt-only reload preserves steps /
      // sampler / scheduler / resolution / etc. as the user expects.
      // Full reload (selection undefined) skips this — loadJobParams
      // overrides everything anyway.
      if (selection) {
        await loadSettingsForMode(mode.value)
      }
      loadJobParams(data.type, data.params, selection)
      sessionStorage.removeItem('reloadJobParams')
      // Save these as the current settings for this mode
      saveSettings()

      // Handle LoRA parsing: if models are already loaded, parse immediately
      // Otherwise, set flag to retry when models become available
      if (availableLoras.value.length > 0) {
        // Models already loaded - parse LoRAs immediately
        if (prompt.value) {
          const { cleanedPrompt, newLoras: posLoras } = parseLorasFromPrompt(prompt.value, positiveLoraList.value)
          if (posLoras.length > 0) {
            prompt.value = cleanedPrompt
            positiveLoraList.value = [...positiveLoraList.value, ...posLoras]
          }
        }
        if (negativePrompt.value) {
          const { cleanedPrompt, newLoras: negLoras } = parseLorasFromPrompt(negativePrompt.value, negativeLoraList.value)
          if (negLoras.length > 0) {
            negativePrompt.value = cleanedPrompt
            negativeLoraList.value = [...negativeLoraList.value, ...negLoras]
          }
        }
      } else {
        // Models not loaded yet - set flag to retry when they become available
        pendingLoraParseFromJobReload.value = true
      }

      // For image edit jobs reloaded from queue, fetch the saved source image
      if (mode.value === 'img_edit' && data.job_id) {
        try {
          const sourceUrl = `/output/${data.job_id}/source.png`
          const response = await fetch(sourceUrl)
          if (response.ok) {
            const blob = await response.blob()
            const reader = new FileReader()
            reader.onload = () => { refImage.value = reader.result as string }
            reader.readAsDataURL(blob)
          }
        } catch {
          // Source image not available - user can re-upload
        }
      }

      // ControlNet + mask + init images are persisted to <job_dir>/*.png at
      // generation time (sd_wrapper.cpp). Fetch them on reload so the
      // widgets aren't broken <img> tags — the base64 payloads aren't in
      // the queue snapshot (too heavy). Same 404-tolerant pattern as the
      // img_edit source fetch above.
      const restoreImageFromDisk = async (
        url: string,
        setter: (v: string) => void,
        clearer: () => void
      ) => {
        try {
          const response = await fetch(url)
          if (response.ok) {
            const blob = await response.blob()
            const reader = new FileReader()
            reader.onload = () => { setter(reader.result as string) }
            reader.readAsDataURL(blob)
          } else {
            clearer()
          }
        } catch {
          clearer()
        }
      }
      if (data.job_id && data.params) {
        const p = data.params as Record<string, unknown>
        if (p.control_image_base64 !== undefined) {
          await restoreImageFromDisk(
            `/output/${data.job_id}/control.png`,
            (v) => { controlImage.value = v },
            () => { controlImage.value = undefined }
          )
        }
        if (p.mask_image_base64 !== undefined) {
          await restoreImageFromDisk(
            `/output/${data.job_id}/mask.png`,
            (v) => { maskImage.value = v },
            () => { maskImage.value = undefined }
          )
        }
        if (mode.value === 'img2img' && p.init_image_base64 !== undefined) {
          await restoreImageFromDisk(
            `/output/${data.job_id}/source.png`,
            (v) => { initImage.value = v },
            () => { initImage.value = undefined }
          )
        }
      }
      // Reset suppress flag after Vue's deferred watchers have executed
      await nextTick()
      suppressPromptRestore = false
    } catch (e) {
      console.error('Failed to load job params:', e)
      suppressPromptRestore = false
    }
  } else {
    // Restore last used mode from local storage
    const lastMode = localStorage.getItem('generateSettings_lastMode') as 'txt2img' | 'img2img' | 'img_edit' | 'txt2vid' | null
    if (lastMode && ['txt2img', 'img2img', 'img_edit', 'txt2vid'].includes(lastMode)) {
      mode.value = lastMode
    }
    // Load settings for current mode
    await loadSettingsForMode(mode.value)
    // Load LoRA settings (img_edit shares txt2img)
    const loraMode = mode.value === 'img_edit' ? 'txt2img' : mode.value
    loadLoraSettingsForMode(loraMode)

    // Restore prompts from localStorage for current mode (only when NOT reloading job params)
    const savedPrompt = localStorage.getItem(getPromptStorageKey(mode.value))
    const savedNegativePrompt = localStorage.getItem(getNegativePromptStorageKey(mode.value))
    if (savedPrompt !== null) {
      prompt.value = savedPrompt
    }
    if (savedNegativePrompt !== null) {
      negativePrompt.value = savedNegativePrompt
    }

    // Migrate old single-key prompts to per-mode keys (one-time migration)
    const oldPrompt = localStorage.getItem('generateSettings_prompt')
    const oldNegativePrompt = localStorage.getItem('generateSettings_negativePrompt')
    if (oldPrompt !== null) {
      // Only migrate to modes that don't have prompts yet
      for (const m of ['txt2img', 'img2img', 'txt2vid']) {
        if (localStorage.getItem(getPromptStorageKey(m)) === null) {
          localStorage.setItem(getPromptStorageKey(m), oldPrompt)
        }
      }
      localStorage.removeItem('generateSettings_prompt')
    }
    if (oldNegativePrompt !== null) {
      for (const m of ['txt2img', 'img2img', 'txt2vid']) {
        if (localStorage.getItem(getNegativePromptStorageKey(m)) === null) {
          localStorage.setItem(getNegativePromptStorageKey(m), oldNegativePrompt)
        }
      }
      localStorage.removeItem('generateSettings_negativePrompt')
    }

    // Restore images from sessionStorage
    const savedInitImage = sessionStorage.getItem(INIT_IMAGE_STORAGE_KEY)
    const savedMaskImage = sessionStorage.getItem(MASK_IMAGE_STORAGE_KEY)
    const savedControlImage = sessionStorage.getItem(CONTROL_IMAGE_STORAGE_KEY)
    const savedRefImage = sessionStorage.getItem(REF_IMAGE_STORAGE_KEY)
    if (savedInitImage) {
      initImage.value = savedInitImage
    }
    if (savedMaskImage) {
      maskImage.value = savedMaskImage
    }
    if (savedControlImage) {
      controlImage.value = savedControlImage
    }
    if (savedRefImage) {
      refImage.value = savedRefImage
    }
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

  // Save prompts to localStorage for persistence across navigation (per-mode)
  localStorage.setItem(getPromptStorageKey(mode.value), prompt.value)
  localStorage.setItem(getNegativePromptStorageKey(mode.value), negativePrompt.value)

  // Force-save LoRA settings immediately before unmount (debounce may not have fired yet)
  saveLoraSettings()

  // Force-save generation settings immediately before unmount
  saveSettings()

  // Force-save images immediately before unmount (debounce may not have fired yet)
  try {
    if (initImage.value) sessionStorage.setItem(INIT_IMAGE_STORAGE_KEY, initImage.value)
    if (maskImage.value) sessionStorage.setItem(MASK_IMAGE_STORAGE_KEY, maskImage.value)
    if (controlImage.value) sessionStorage.setItem(CONTROL_IMAGE_STORAGE_KEY, controlImage.value)
    if (refImage.value) sessionStorage.setItem(REF_IMAGE_STORAGE_KEY, refImage.value)
  } catch {
    // Storage quota exceeded - skip silently
  }
})

// Section keys understood by selective reload. Keep in sync with Queue.vue's
// selector modal — both files have to agree on the strings.
type ReloadSection = 'prompt' | 'negative' | 'settings' | 'loras' | 'advanced'

function loadJobParams(
  type: string,
  params: Record<string, unknown>,
  sections?: Set<ReloadSection>
) {
  // sections == undefined means "load everything" (legacy behavior). Otherwise
  // each block is gated on whether its section is in the selection.
  const has = (k: ReloadSection) => !sections || sections.has(k)

  // Mode is structural — only flip when we're loading something mode-specific
  // (settings/advanced). A prompt-only or LoRAs-only reload should leave the
  // user on whatever tab they're currently using.
  if (!sections || has('settings') || has('advanced')) {
    if (type === 'txt2img' && (params.ref_images || (typeof params.ref_images_count === 'number' && params.ref_images_count > 0))) {
      mode.value = 'img_edit'
    } else if (type === 'txt2img' || type === 'img2img' || type === 'txt2vid') {
      mode.value = type
    }
  }

  // Clear LoRA lists when we're going to repopulate them. The downstream
  // post-mount extractor (onMounted block) will re-parse <lora:...> tags
  // out of whichever prompt strings get set below. If the user picked
  // 'loras' WITHOUT 'prompt' / 'negative' we extract directly from the
  // job's params here — see "loras-only" path below.
  if (has('loras')) {
    positiveLoraList.value = []
    negativeLoraList.value = []
  }

  // Prompts
  if (has('prompt') && params.prompt !== undefined) prompt.value = params.prompt as string
  if (has('negative') && params.negative_prompt !== undefined) negativePrompt.value = params.negative_prompt as string

  // LoRAs-only path: prompts not being touched, but the user wants the LoRA
  // panel populated from the job. Extract directly from params.prompt /
  // params.negative_prompt (don't mutate prompt.value). Skips silently if
  // models aren't loaded yet (availableLoras empty) — the user will see an
  // empty LoRA panel and can re-trigger after the model loads.
  if (has('loras') && !has('prompt') && availableLoras.value.length > 0) {
    if (typeof params.prompt === 'string') {
      const { newLoras } = parseLorasFromPrompt(params.prompt, [])
      if (newLoras.length > 0) positiveLoraList.value = newLoras
    }
  }
  if (has('loras') && !has('negative') && availableLoras.value.length > 0) {
    if (typeof params.negative_prompt === 'string') {
      const { newLoras } = parseLorasFromPrompt(params.negative_prompt, [])
      if (newLoras.length > 0) negativeLoraList.value = newLoras
    }
  }

  // Generation settings (resolution, steps, sampler, …)
  if (has('settings')) {
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
    if (params.video_frames !== undefined) videoFrames.value = params.video_frames as number
    if (params.fps !== undefined) fps.value = params.fps as number
    if (params.flow_shift !== undefined) flowShift.value = params.flow_shift as number
  }

  // Advanced (img2img strength, ControlNet, SLG, cache, VAE tiling, …)
  if (has('advanced')) {
    if (params.strength !== undefined) strength.value = params.strength as number
    // Note: init_image_base64 and mask_image_base64 are not reloaded as they may be too large
    if (params.control_strength !== undefined) controlStrength.value = params.control_strength as number
    if (params.slg_scale !== undefined) slgScale.value = params.slg_scale as number
    if (params.cache_mode !== undefined) cacheMode.value = params.cache_mode as string
    if (params.easycache !== undefined && params.cache_mode === undefined) {
      cacheMode.value = params.easycache ? 'easycache' : ''
    }
    if (params.easycache_threshold !== undefined) easycacheThreshold.value = params.easycache_threshold as number
    if (params.taylorseer_n_derivatives !== undefined) taylorseerNDerivatives.value = params.taylorseer_n_derivatives as number
    if (params.taylorseer_skip_interval !== undefined) taylorseerSkipInterval.value = params.taylorseer_skip_interval as number
    if (params.vae_tiling !== undefined) vaeTiling.value = params.vae_tiling as boolean
    if (params.vae_tile_size_x !== undefined) vaeTileSizeX.value = params.vae_tile_size_x as number
    if (params.vae_tile_size_y !== undefined) vaeTileSizeY.value = params.vae_tile_size_y as number
    if (params.vae_tile_overlap !== undefined) vaeTileOverlap.value = params.vae_tile_overlap as number
    if (params.vae_tile_rel_size_x !== undefined) vaeTileRelSizeX.value = params.vae_tile_rel_size_x as number
    if (params.vae_tile_rel_size_y !== undefined) vaeTileRelSizeY.value = params.vae_tile_rel_size_y as number
    if (params.img_cfg_scale !== undefined) imgCfgScale.value = params.img_cfg_scale as number
    if (params.extra_sample_args !== undefined) extraSampleArgs.value = params.extra_sample_args as string
    if (params.pulid_id_embedding_path !== undefined) pulidIdEmbeddingPath.value = params.pulid_id_embedding_path as string
    if (params.pulid_id_weight !== undefined) pulidIdWeight.value = params.pulid_id_weight as number
    if (params.hires_enabled !== undefined) hiresEnabled.value = params.hires_enabled as boolean
    if (params.hires_upscaler !== undefined) hiresUpscaler.value = params.hires_upscaler as string
    if (params.hires_model_path !== undefined) hiresModelPath.value = params.hires_model_path as string
    if (params.hires_scale !== undefined) hiresScale.value = params.hires_scale as number
    if (params.hires_target_width !== undefined) hiresTargetWidth.value = params.hires_target_width as number
    if (params.hires_target_height !== undefined) hiresTargetHeight.value = params.hires_target_height as number
    if (params.hires_steps !== undefined) hiresSteps.value = params.hires_steps as number
    if (params.hires_denoising_strength !== undefined) hiresDenoisingStrength.value = params.hires_denoising_strength as number
    if (params.hires_upscale_tile_size !== undefined) hiresUpscaleTileSize.value = params.hires_upscale_tile_size as number
    if (params.circular_x !== undefined) circularX.value = params.circular_x as boolean
    if (params.circular_y !== undefined) circularY.value = params.circular_y as boolean
    if (params.qwen_image_layers !== undefined) qwenImageLayers.value = params.qwen_image_layers as number
    if (params.high_noise_img_cfg !== undefined) highNoiseImgCfg.value = params.high_noise_img_cfg as number
    if (params.high_noise_scheduler !== undefined) highNoiseScheduler.value = params.high_noise_scheduler as string
    if (params.high_noise_eta !== undefined) highNoiseEta.value = params.high_noise_eta as number
    if (params.high_noise_shifted_timestep !== undefined) highNoiseShiftedTimestep.value = params.high_noise_shifted_timestep as number
    if (params.high_noise_flow_shift !== undefined) highNoiseFlowShift.value = params.high_noise_flow_shift as number
    if (params.high_noise_extra_sample_args !== undefined) highNoiseExtraSampleArgs.value = params.high_noise_extra_sample_args as string
    if (Array.isArray(params.high_noise_custom_sigmas)) {
      // params arrives as number[], UI keeps it as comma-separated string
      highNoiseCustomSigmas.value = (params.high_noise_custom_sigmas as number[]).join(', ')
    }
  }
}

function setPreset(preset: { w: number; h: number }) {
  width.value = preset.w
  height.value = preset.h
}

function swapResolution() {
  const w = width.value
  width.value = height.value
  height.value = w
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

// Show sidebar when there's content to display (source images, preview,
// or a ControlNet input widget). Without the hasControlNet check, moving
// the ControlNet card from the main column into the sidebar meant users
// on the Text-to-Image tab with a ControlNet loaded couldn't see the
// Control Image uploader at all — the sidebar was collapsed.
const showSidebar = computed(() => {
  if (mode.value === 'img2img' || mode.value === 'img_edit') return true
  if (isCurrentJobProcessing.value) return true
  if (hasControlNet.value && mode.value !== 'txt2vid') return true
  return false
})


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
    // Build prompts with LoRAs appended
    const finalPrompt = buildPromptWithLoras(prompt.value, positiveLoraList.value)
    const finalNegativePrompt = buildPromptWithLoras(negativePrompt.value, negativeLoraList.value)

    const baseParams: GenerationParams = {
      prompt: finalPrompt,
      negative_prompt: finalNegativePrompt,
      ...(title.value.trim() ? { title: title.value.trim() } : {}),
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
    if (cacheMode.value) {
      baseParams.cache_mode = cacheMode.value
      // Similarity-threshold caches share the easycache_threshold knob.
      if (cacheMode.value === 'easycache' ||
          cacheMode.value === 'ucache' ||
          cacheMode.value === 'dbcache') {
        baseParams.easycache_threshold = easycacheThreshold.value
      } else if (cacheMode.value === 'taylorseer') {
        baseParams.taylorseer_n_derivatives = taylorseerNDerivatives.value
        baseParams.taylorseer_skip_interval = taylorseerSkipInterval.value
      } else if (cacheMode.value === 'spectrum') {
        baseParams.spectrum_w = spectrumW.value
        baseParams.spectrum_m = spectrumM.value
        baseParams.spectrum_lam = spectrumLam.value
        baseParams.spectrum_window_size = spectrumWindowSize.value
        baseParams.spectrum_flex_window = spectrumFlexWindow.value
        baseParams.spectrum_warmup_steps = spectrumWarmupSteps.value
        baseParams.spectrum_stop_percent = spectrumStopPercent.value
      }
      // cache_dit needs no extra knobs.
    }
    if (vaeTiling.value) {
      baseParams.vae_tiling = true
      if (vaeTileSizeX.value > 0) baseParams.vae_tile_size_x = vaeTileSizeX.value
      if (vaeTileSizeY.value > 0) baseParams.vae_tile_size_y = vaeTileSizeY.value
      baseParams.vae_tile_overlap = vaeTileOverlap.value
      // Relative tile size (fraction of image). 0 = use absolute tile_size_*.
      if (vaeTileRelSizeX.value > 0) baseParams.vae_tile_rel_size_x = vaeTileRelSizeX.value
      if (vaeTileRelSizeY.value > 0) baseParams.vae_tile_rel_size_y = vaeTileRelSizeY.value
    }

    // Advanced guidance (sd_guidance_params_t passthrough)
    // -1 = inherit cfg_scale, so only send when the user explicitly overrode.
    if (imgCfgScale.value !== -1) {
      baseParams.img_cfg_scale = imgCfgScale.value
    }
    if (extraSampleArgs.value.trim()) {
      baseParams.extra_sample_args = extraSampleArgs.value.trim()
    }

    // PuLID-Flux (sd_pulid_params_t). Only ship when path is set —
    // sending an empty path would still hit the model's PuLID branch.
    if (pulidIdEmbeddingPath.value.trim()) {
      baseParams.pulid_id_embedding_path = pulidIdEmbeddingPath.value.trim()
      baseParams.pulid_id_weight = pulidIdWeight.value
    }

    // sd.cpp native two-pass hi-res-fix (sd_hires_params_t). Distinct from
    // the post-gen ESRGAN upscale flag (baseParams.upscale).
    if (hiresEnabled.value) {
      baseParams.hires_enabled = true
      baseParams.hires_upscaler = hiresUpscaler.value
      if (hiresUpscaler.value === 'model' && hiresModelPath.value.trim()) {
        baseParams.hires_model_path = hiresModelPath.value.trim()
      }
      baseParams.hires_scale = hiresScale.value
      if (hiresTargetWidth.value > 0) baseParams.hires_target_width = hiresTargetWidth.value
      if (hiresTargetHeight.value > 0) baseParams.hires_target_height = hiresTargetHeight.value
      if (hiresSteps.value > 0) baseParams.hires_steps = hiresSteps.value
      baseParams.hires_denoising_strength = hiresDenoisingStrength.value
      if (hiresUpscaleTileSize.value > 0) baseParams.hires_upscale_tile_size = hiresUpscaleTileSize.value
    }

    // Circular RoPE / tileable position embeddings (per-gen since PR #1748).
    // Only send when true — omitting matches sd.cpp's default (both false).
    if (circularX.value) baseParams.circular_x = true
    if (circularY.value) baseParams.circular_y = true
    // Qwen-Image layered (image path only; > 0 = enabled).
    if (qwenImageLayers.value > 0) baseParams.qwen_image_layers = qwenImageLayers.value

    // Control image
    if (controlImage.value && hasControlNet.value) {
      baseParams.control_image_base64 = controlImage.value
      baseParams.control_strength = controlStrength.value
    }

    // If the user enabled expand_prompt and the prompt actually has template
    // syntax, run the confirmation modal first. The modal also handles parse
    // errors (malformed braces) and lets the user cancel without losing form
    // state. When confirmed, we set baseParams.expand_prompt so the backend
    // does the authoritative expansion + group_id assignment.
    if (expandPromptEnabled.value && hasTemplateSyntax(finalPrompt)) {
      const conf = await openExpandPromptConfirm({
        prompt: finalPrompt,
        batchCount: batchCount.value,
        jobType: mode.value === 'txt2vid' ? 'txt2vid'
               : mode.value === 'img2img' ? 'img2img'
               : 'txt2img',
        source: 'user',
      })
      if (!conf.confirmed) {
        submitting.value = false
        return
      }
      baseParams.expand_prompt = true
    }

    let result
    if (mode.value === 'txt2img') {
      result = await api.txt2img(baseParams)
    } else if (mode.value === 'img_edit') {
      if (!refImage.value) {
        store.showToast('Please upload a reference image', 'warning')
        submitting.value = false
        return
      }
      // Reference-based editing → txt2img endpoint with ref_images
      const imgData = refImage.value.includes(',')
        ? refImage.value.split(',')[1]
        : refImage.value
      result = await api.txt2img({ ...baseParams, ref_images: [imgData] })
    } else if (mode.value === 'img2img') {
      if (!initImage.value) {
        store.showToast('Please upload an input image', 'warning')
        submitting.value = false
        return
      }
      // Traditional img2img → img2img endpoint
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
      // High-noise MoE pass extras. Only emit overrides; -1/0/"" mean inherit.
      if (highNoiseImgCfg.value !== -1) {
        params.high_noise_img_cfg = highNoiseImgCfg.value
      }
      if (highNoiseScheduler.value.trim()) {
        params.high_noise_scheduler = highNoiseScheduler.value.trim()
      }
      if (highNoiseEta.value !== 0) {
        params.high_noise_eta = highNoiseEta.value
      }
      if (highNoiseShiftedTimestep.value !== 0) {
        params.high_noise_shifted_timestep = highNoiseShiftedTimestep.value
      }
      if (highNoiseFlowShift.value !== 0) {
        params.high_noise_flow_shift = highNoiseFlowShift.value
      }
      if (highNoiseExtraSampleArgs.value.trim()) {
        params.high_noise_extra_sample_args = highNoiseExtraSampleArgs.value.trim()
      }
      if (highNoiseCustomSigmas.value.trim()) {
        // UI accepts a comma-separated list; backend expects number[].
        const sigmas = highNoiseCustomSigmas.value
          .split(',')
          .map(s => parseFloat(s.trim()))
          .filter(n => !isNaN(n))
        if (sigmas.length > 0) {
          params.high_noise_custom_sigmas = sigmas
        }
      }
      result = await api.txt2vid(params)
    }

    // Expansion submissions return group_id + job_ids[]; single submissions
    // return job_id. Use whichever is present.
    if (result.group_id && result.job_ids && result.job_ids.length > 0) {
      store.showToast(
        `Created ${result.job_ids.length} jobs in group ${result.group_id.slice(0, 8)}`,
        'success'
      )
      lastSubmittedJobId.value = result.job_ids[0]
    } else if (result.job_id) {
      store.showToast(`Job submitted: ${result.job_id}`, 'success')
      lastSubmittedJobId.value = result.job_id
    }

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
        <button
          :class="['tab', { active: mode === 'img_edit', disabled: !supportsImageEdit }]"
          :disabled="!supportsImageEdit"
          :title="imageEditTabTooltip"
          @click="supportsImageEdit && (mode = 'img_edit')"
        >
          Image Edit
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

    <!-- Two-column layout -->
    <div :class="['generate-layout', { 'single-column': !showSidebar }]">
      <!-- LEFT: Main form controls -->
      <div class="generate-main">
        <!-- Loaded Model Panel -->
        <LoadedModelPanel />

        <!-- Prompt Section -->
        <div class="card">
          <div class="form-group">
            <label class="form-label">Title (optional)</label>
            <input
              v-model="title"
              type="text"
              class="form-input"
              maxlength="200"
              placeholder="Display label for this queue job"
            />
          </div>
          <div class="form-group" data-setting="prompt" :class="{ 'setting-highlighted': highlightedSetting === 'prompt' }">
            <label class="form-label">Prompt</label>
            <HighlightedPrompt
              v-model="prompt"
              placeholder="A beautiful landscape with mountains and a lake..."
              :rows="3"
            />
            <div class="form-hint">
              LoRAs typed as &lt;lora:name:weight&gt; are auto-detected and moved to the LoRA panel.
              <span v-if="invalidLorasInPositivePrompt.length > 0" class="lora-warning-hint">
                &#9888; Unknown LoRAs: {{ invalidLorasInPositivePrompt.join(', ') }}
              </span>
            </div>
            <!-- Wildcard / dynamic-prompt expansion toggle.
                 Always rendered when the prompt contains template syntax so
                 the user knows the feature exists even before opting in. -->
            <div v-if="promptTemplateInfo.hasSyntax" class="expand-prompt-toggle">
              <label class="form-checkbox">
                <input v-model="expandPromptEnabled" type="checkbox" />
                <span>Expand prompt template</span>
              </label>
              <span v-if="expandPromptEnabled && promptTemplateInfo.count !== null"
                    class="expand-count-chip"
                    :title="`Each variation runs ${batchCount}x = ${promptTemplateInfo.count * batchCount} images total`">
                {{ promptTemplateInfo.count }} variation{{ promptTemplateInfo.count === 1 ? '' : 's' }}
                <span v-if="batchCount > 1">&times; {{ batchCount }} batches = {{ promptTemplateInfo.count * batchCount }} images</span>
              </span>
              <span v-if="expandPromptEnabled && promptTemplateInfo.error"
                    class="expand-count-chip expand-count-error"
                    :title="promptTemplateInfo.error">
                Syntax error
              </span>
            </div>
          </div>

          <div class="form-group" data-setting="negative-prompt" :class="{ 'setting-highlighted': highlightedSetting === 'negative-prompt' }">
            <label class="form-label">Negative Prompt (optional)</label>
            <HighlightedPrompt
              v-model="negativePrompt"
              placeholder="blurry, low quality, bad anatomy..."
              :rows="2"
            />
            <div v-if="invalidLorasInNegativePrompt.length > 0" class="form-hint lora-warning-hint">
              &#9888; Unknown LoRAs: {{ invalidLorasInNegativePrompt.join(', ') }}
            </div>
          </div>
        </div>

        <!-- LoRA Panel -->
        <LoraPanel
          v-if="availableLoras.length > 0"
          :positive-list="positiveLoraList"
          :negative-list="negativeLoraList"
          :available-loras="availableLoras"
          :settings="loraSettings"
          @update:positive-list="positiveLoraList = $event"
          @update:negative-list="negativeLoraList = $event"
          @update:settings="loraSettings = $event"
        />

        <!-- ControlNet input moved to the right sidebar (see the RIGHT
             column further down) so it matches the img2img/img_edit layout —
             control image lives next to the other input images. -->

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
              <span class="rec-item rec-clickable" @click="applyRecommendedSetting('sampler')" title="Click to apply">
                <span class="rec-label">Sampler:</span>
                <span class="rec-value" :class="{ 'rec-different': isDifferentFromRecommended('sampler', sampler) }">{{ recommended.sampler }}</span>
              </span>
              <span class="rec-item rec-clickable" @click="applyRecommendedSetting('scheduler')" title="Click to apply">
                <span class="rec-label">Scheduler:</span>
                <span class="rec-value" :class="{ 'rec-different': isDifferentFromRecommended('scheduler', scheduler) }">{{ recommended.scheduler }}</span>
              </span>
              <span class="rec-item rec-clickable" @click="applyRecommendedSetting('steps')" title="Click to apply">
                <span class="rec-label">Steps:</span>
                <span class="rec-value" :class="{ 'rec-different': isDifferentFromRecommended('steps', steps) }">{{ recommended.steps }}</span>
              </span>
              <span class="rec-item rec-clickable" @click="applyRecommendedSetting('cfgScale')" title="Click to apply">
                <span class="rec-label">CFG:</span>
                <span class="rec-value" :class="{ 'rec-different': isDifferentFromRecommended('cfgScale', cfgScale) }">{{ recommended.cfgScale }}</span>
              </span>
              <span class="rec-item rec-clickable" @click="applyRecommendedSize()" title="Click to apply">
                <span class="rec-label">Size:</span>
                <span class="rec-value" :class="{ 'rec-different': width !== recommended.width || height !== recommended.height }">{{ recommended.width }}x{{ recommended.height }}</span>
              </span>
              <span v-if="recommended.distilledGuidance > 0" class="rec-item rec-clickable" @click="applyRecommendedSetting('distilledGuidance')" title="Click to apply">
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

          <div class="form-row form-row-resolution">
            <div class="form-group" data-setting="width" :title="genOpt('width')" :class="{ 'setting-highlighted': highlightedSetting === 'width' }">
              <label class="form-label">Width</label>
              <input v-model.number="width" type="number" class="form-input" step="8" min="64" max="2048" />
              <RecHint :desc="genOptDesc('width')" />
            </div>
            <button
              type="button"
              class="resolution-swap"
              @click="swapResolution"
              title="Swap width and height"
              aria-label="Swap width and height"
            >&#8646;</button>
            <div class="form-group" data-setting="height" :title="genOpt('height')" :class="{ 'setting-highlighted': highlightedSetting === 'height' }">
              <label class="form-label">Height</label>
              <input v-model.number="height" type="number" class="form-input" step="8" min="64" max="2048" />
              <RecHint :desc="genOptDesc('height')" />
            </div>
          </div>

          <div class="form-row">
            <div class="form-group" data-setting="steps" :title="genOpt('steps')" :class="{ 'setting-highlighted': highlightedSetting === 'steps' }">
              <label class="form-label">
                Steps: {{ steps }}
                <span v-if="recommended && isDifferentFromRecommended('steps', steps)" class="rec-hint rec-clickable" @click.prevent="applyRecommendedSetting('steps')" title="Click to apply">
                  (rec: {{ recommended.steps }})
                </span>
              </label>
              <input v-model.number="steps" type="range" class="form-range" min="1" max="150" />
              <RecHint :desc="genOptDesc('steps')" />
            </div>
            <div class="form-group" data-setting="cfg-scale" :title="genOpt('cfg_scale')" :class="{ 'setting-highlighted': highlightedSetting === 'cfg-scale' }">
              <label class="form-label">
                CFG Scale: {{ cfgScale.toFixed(1) }}
                <span v-if="recommended && isDifferentFromRecommended('cfgScale', cfgScale)" class="rec-hint rec-clickable" @click.prevent="applyRecommendedSetting('cfgScale')" title="Click to apply">
                  (rec: {{ recommended.cfgScale }})
                </span>
              </label>
              <input v-model.number="cfgScale" type="range" class="form-range" min="0" max="20" step="0.1" />
              <RecHint :desc="genOptDesc('cfg_scale')" />
            </div>
          </div>

          <div class="form-row">
            <div class="form-group" data-setting="sampler" :title="genOpt('sampler')" :class="{ 'setting-highlighted': highlightedSetting === 'sampler' }">
              <label class="form-label">
                Sampler
                <span v-if="recommended && isDifferentFromRecommended('sampler', sampler)" class="rec-hint rec-clickable" @click.prevent="applyRecommendedSetting('sampler')" title="Click to apply">
                  (rec: {{ recommended.sampler }})
                </span>
              </label>
              <select v-model="sampler" class="form-select">
                <option v-for="s in samplers" :key="s" :value="s">{{ s }}</option>
              </select>
              <RecHint :desc="genOptDesc('sampler')" />
            </div>
            <div class="form-group" data-setting="scheduler" :title="genOpt('scheduler')" :class="{ 'setting-highlighted': highlightedSetting === 'scheduler' }">
              <label class="form-label">
                Scheduler
                <span v-if="recommended && isDifferentFromRecommended('scheduler', scheduler)" class="rec-hint rec-clickable" @click.prevent="applyRecommendedSetting('scheduler')" title="Click to apply">
                  (rec: {{ recommended.scheduler }})
                </span>
              </label>
              <select v-model="scheduler" class="form-select">
                <option v-for="s in schedulers" :key="s" :value="s">{{ s }}</option>
              </select>
              <RecHint :desc="genOptDesc('scheduler')" />
            </div>
          </div>

          <div class="form-row">
            <div class="form-group" data-setting="seed" :title="genOpt('seed')" :class="{ 'setting-highlighted': highlightedSetting === 'seed' }">
              <label class="form-label">Seed (-1 = random)</label>
              <div class="input-with-button">
                <input v-model.number="seed" type="number" class="form-input" />
                <button class="btn btn-secondary btn-icon" @click="randomizeSeed" title="Randomize">
                  &#127922;
                </button>
              </div>
              <RecHint :desc="genOptDesc('seed')" />
            </div>
            <div class="form-group" data-setting="batch-count" :title="genOpt('batch_count')" :class="{ 'setting-highlighted': highlightedSetting === 'batch-count' }">
              <label class="form-label">Batch Count</label>
              <input v-model.number="batchCount" type="number" class="form-input" min="1" max="16" />
              <RecHint :desc="genOptDesc('batch_count')" />
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
                  <span v-if="recommended && isDifferentFromRecommended('clipSkip', clipSkip)" class="rec-hint rec-clickable" @click.prevent="applyRecommendedSetting('clipSkip')" title="Click to apply">
                    (rec: {{ recommended.clipSkip }})
                  </span>
                </label>
                <input v-model.number="clipSkip" type="number" class="form-input" min="-1" max="12" />
              </div>
              <div class="form-group" data-setting="distilled-guidance" :class="{ 'setting-highlighted': highlightedSetting === 'distilled-guidance' }">
                <label class="form-label">
                  Distilled Guidance
                  <span v-if="recommended && isDifferentFromRecommended('distilledGuidance', distilledGuidance)" class="rec-hint rec-clickable" @click.prevent="applyRecommendedSetting('distilledGuidance')" title="Click to apply">
                    (rec: {{ recommended.distilledGuidance }})
                  </span>
                </label>
                <input v-model.number="distilledGuidance" type="number" class="form-input" step="0.5" min="0" max="10" />
              </div>
            </div>

            <div class="form-group" data-setting="slg-scale" :class="{ 'setting-highlighted': highlightedSetting === 'slg-scale' }">
              <label class="form-label">
                SLG Scale (Skip Layer Guidance)
                <span v-if="recommended?.slgScale !== undefined && isDifferentFromRecommended('slgScale', slgScale)" class="rec-hint rec-clickable" @click.prevent="applyRecommendedSetting('slgScale')" title="Click to apply">
                  (rec: {{ recommended.slgScale }})
                </span>
              </label>
              <input v-model.number="slgScale" type="number" class="form-input" step="0.5" min="0" max="10" />
            </div>

            <div class="form-group" data-setting="cache-mode" :class="{ 'setting-highlighted': highlightedSetting === 'cache-mode' }">
              <label class="form-label">Cache Acceleration
                <span v-if="recommended?.cacheMode !== undefined && recommended.cacheMode !== cacheMode" class="rec-hint rec-clickable" @click.prevent="applyRecommendedSetting('cacheMode')" title="Click to apply">
                  (rec: {{ recommended.cacheMode || 'disabled' }})
                </span>
              </label>
              <select v-model="cacheMode" class="form-input">
                <option value="">Disabled</option>
                <option value="easycache">EasyCache</option>
                <option value="ucache">UCache</option>
                <option value="dbcache">DBCache</option>
                <option value="taylorseer">TaylorSeer</option>
                <option value="cache_dit">Cache-DiT</option>
                <option value="spectrum">Spectrum</option>
              </select>
            </div>

            <!-- Similarity-threshold caches (EasyCache, UCache, DBCache) share the same knob. -->
            <div v-if="cacheMode === 'easycache' || cacheMode === 'ucache' || cacheMode === 'dbcache'" class="form-group mt-2">
              <label class="form-label">Cache Threshold</label>
              <input v-model.number="easycacheThreshold" type="number" class="form-input" step="0.05" min="0" max="1" />
              <div class="form-hint">Reuse threshold for similarity-based caches.</div>
            </div>

            <template v-if="cacheMode === 'taylorseer'">
              <div class="form-row mt-2">
                <div class="form-group">
                  <label class="form-label">TaylorSeer Derivatives</label>
                  <input v-model.number="taylorseerNDerivatives" type="number" class="form-input" min="1" max="6" step="1" />
                  <div class="form-hint">Typically 2-4.</div>
                </div>
                <div class="form-group">
                  <label class="form-label">Skip Interval (0 = adaptive)</label>
                  <input v-model.number="taylorseerSkipInterval" type="number" class="form-input" min="0" max="20" step="1" />
                </div>
              </div>
            </template>

            <template v-if="cacheMode === 'spectrum'">
              <div class="form-group mt-2">
                <label class="form-label">Spectrum W</label>
                <input v-model.number="spectrumW" type="number" class="form-input" step="0.1" min="0" max="1" />
              </div>
              <div class="form-group mt-2">
                <label class="form-label">Spectrum Stop %</label>
                <input v-model.number="spectrumStopPercent" type="number" class="form-input" step="0.05" min="0" max="1" />
              </div>
            </template>

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
              <!-- Relative tile sizes (fraction of image). 0 = use absolute tile_size_*. -->
              <div class="form-row">
                <div class="form-group">
                  <label class="form-label">Tile Rel Size X (0 = use absolute)</label>
                  <input v-model.number="vaeTileRelSizeX" type="number" class="form-input" min="0" max="1" step="0.05" />
                </div>
                <div class="form-group">
                  <label class="form-label">Tile Rel Size Y (0 = use absolute)</label>
                  <input v-model.number="vaeTileRelSizeY" type="number" class="form-input" min="0" max="1" step="0.05" />
                </div>
              </div>
              <div class="form-group" data-setting="vae-tile-overlap" :class="{ 'setting-highlighted': highlightedSetting === 'vae-tile-overlap' }">
                <label class="form-label">Tile Overlap: {{ vaeTileOverlap.toFixed(2) }}</label>
                <input v-model.number="vaeTileOverlap" type="range" class="form-range" min="0" max="1" step="0.05" />
              </div>
              <div class="form-hint">
                VAE tiling reduces VRAM usage for high-resolution images/videos by processing in tiles.
                Use either absolute (Tile Size X/Y) or relative (Tile Rel Size X/Y) — relative takes precedence when &gt; 0.
              </div>
            </div>
          </div>
        </details>

        <!-- Advanced Guidance (sd_guidance_params_t passthrough) -->
        <details class="card accordion">
          <summary class="accordion-header">
            Advanced Guidance
          </summary>
          <div class="accordion-content">
            <div class="form-group">
              <label class="form-label">Image CFG (-1 = inherit cfg_scale)</label>
              <input v-model.number="imgCfgScale" type="number" class="form-input" step="0.5" min="-1" max="20" />
              <div class="form-hint">Maps to sd_guidance_params_t.img_cfg.</div>
            </div>
            <div class="form-group">
              <label class="form-label">Extra Sample Args</label>
              <input v-model="extraSampleArgs" type="text" class="form-input" placeholder="key1=val1,key2=val2" />
              <div class="form-hint">Pass-through key=value list for sd.cpp's sample arg parser (model-specific knobs).</div>
            </div>
          </div>
        </details>

        <!-- Circular RoPE / tileable position embeddings. Was a load-time
             option (Ideogram4 tileable-texture workflows) until leejet PR
             #1748 moved it to per-generation params — no more model reload
             just to toggle. -->
        <details class="card accordion">
          <summary class="accordion-header">Circular RoPE (Tileable Output)</summary>
          <div class="accordion-content">
            <label class="form-checkbox">
              <input v-model="circularX" type="checkbox" />
              Circular X (horizontal seam)
            </label>
            <label class="form-checkbox mt-1">
              <input v-model="circularY" type="checkbox" />
              Circular Y (vertical seam)
            </label>
            <div class="form-hint mt-1">
              Off for normal generation. Enable one for horizontal-only or
              vertical-only tiling; enable both for fully-wrappable textures
              (skyboxes, repeating patterns, Ideogram4 panoramas).
            </div>
            <!-- Qwen-Image layered rendering. Image path only — the video
                 pipeline (sd_vid_gen_params_t) doesn't expose this. -->
            <div v-if="mode !== 'txt2vid'" class="form-group mt-2">
              <label class="form-label">Qwen-Image Layers</label>
              <input v-model.number="qwenImageLayers" type="number" min="0"
                     class="form-input small" placeholder="0 = disabled" />
              <div class="form-hint">
                Qwen-Image layered rendering (leejet PR #1119). 0 = disabled.
                Only meaningful for Qwen-Image checkpoints.
              </div>
            </div>
          </div>
        </details>

        <!-- Hi-Res Fix (sd.cpp native sd_hires_params_t).
             Distinct from the post-gen ESRGAN upscale flag below. -->
        <details class="card accordion">
          <summary class="accordion-header">
            Hi-Res Fix (sd.cpp native)
          </summary>
          <div class="accordion-content">
            <label class="form-checkbox">
              <input v-model="hiresEnabled" type="checkbox" />
              Enable hi-res-fix two-pass refine
            </label>
            <div class="form-hint">Distinct from the post-gen ESRGAN upscale checkbox.</div>

            <template v-if="hiresEnabled">
              <div class="form-group mt-2">
                <label class="form-label">Upscaler</label>
                <select v-model="hiresUpscaler" class="form-input">
                  <optgroup label="Latent (in-VAE-space)">
                    <option value="latent">latent</option>
                    <option value="latent_nearest">latent_nearest</option>
                    <option value="latent_nearest_exact">latent_nearest_exact</option>
                    <option value="latent_antialiased">latent_antialiased</option>
                    <option value="latent_bicubic">latent_bicubic</option>
                    <option value="latent_bicubic_antialiased">latent_bicubic_antialiased</option>
                  </optgroup>
                  <optgroup label="Pixel-space">
                    <option value="lanczos">lanczos</option>
                    <option value="nearest">nearest</option>
                  </optgroup>
                  <optgroup label="Model-based">
                    <option value="model">model (ESRGAN-style)</option>
                  </optgroup>
                  <optgroup label="Other">
                    <option value="none">none</option>
                  </optgroup>
                </select>
              </div>

              <div v-if="hiresUpscaler === 'model'" class="form-group mt-2">
                <label class="form-label">Upscaler Model</label>
                <select v-if="esrganModels.length > 0" v-model="hiresModelPath" class="form-select">
                  <option value="">— Select an upscaler —</option>
                  <option v-for="m in esrganModels" :key="m.name" :value="m.name">{{ m.name }}</option>
                </select>
                <input
                  v-else
                  v-model="hiresModelPath"
                  type="text"
                  class="form-input"
                  placeholder="e.g. RealESRGAN_x4plus.pth"
                />
                <small v-if="esrganModels.length === 0" class="form-hint">
                  No ESRGAN models found in the configured upscaler directory. Drop one in and re-scan, or type the filename manually.
                </small>
              </div>

              <div class="form-row">
                <div class="form-group">
                  <label class="form-label">Scale</label>
                  <input v-model.number="hiresScale" type="number" class="form-input" step="0.5" min="1" max="8" />
                </div>
                <div class="form-group">
                  <label class="form-label">Denoising Strength</label>
                  <input v-model.number="hiresDenoisingStrength" type="number" class="form-input" step="0.05" min="0" max="1" />
                </div>
              </div>

              <div class="form-row">
                <div class="form-group">
                  <label class="form-label">Target Width (0 = derive)</label>
                  <input v-model.number="hiresTargetWidth" type="number" class="form-input" min="0" max="4096" step="8" />
                </div>
                <div class="form-group">
                  <label class="form-label">Target Height (0 = derive)</label>
                  <input v-model.number="hiresTargetHeight" type="number" class="form-input" min="0" max="4096" step="8" />
                </div>
              </div>

              <div class="form-row">
                <div class="form-group">
                  <label class="form-label">Steps (0 = inherit)</label>
                  <input v-model.number="hiresSteps" type="number" class="form-input" min="0" max="150" />
                </div>
                <div class="form-group">
                  <label class="form-label">Upscale Tile Size (0 = auto)</label>
                  <input v-model.number="hiresUpscaleTileSize" type="number" class="form-input" min="0" max="2048" step="64" />
                </div>
              </div>
            </template>
          </div>
        </details>

        <!-- PuLID-Flux (sd_pulid_params_t). Always shown — the backend safely
             ignores these fields unless the model was loaded with pulid_weights. -->
        <details class="card accordion">
          <summary class="accordion-header">
            PuLID-Flux
          </summary>
          <div class="accordion-content">
            <div class="form-hint">Requires pulid_weights loaded on the model.</div>
            <div class="form-group mt-2">
              <label class="form-label">ID Embedding Path</label>
              <input v-model="pulidIdEmbeddingPath" type="text" class="form-input" placeholder="Path to PuLID identity embedding" />
            </div>
            <div class="form-group mt-2">
              <label class="form-label">ID Weight</label>
              <input v-model.number="pulidIdWeight" type="number" class="form-input" step="0.05" min="0" max="2" />
            </div>
          </div>
        </details>

        <!-- High-Noise Pass (MoE models like Wan2.2). Txt2Vid only. -->
        <details v-if="mode === 'txt2vid'" class="card accordion">
          <summary class="accordion-header">
            High-Noise Pass (MoE)
          </summary>
          <div class="accordion-content">
            <div class="form-hint">
              Only used by MoE video models (Wan2.2 and similar). Leave blank/0/-1 to inherit the main-pass setting.
            </div>
            <div class="form-row mt-2">
              <div class="form-group">
                <label class="form-label">Image CFG (-1 = inherit)</label>
                <input v-model.number="highNoiseImgCfg" type="number" class="form-input" step="0.5" min="-1" max="20" />
              </div>
              <div class="form-group">
                <label class="form-label">Scheduler (blank = inherit)</label>
                <select v-model="highNoiseScheduler" class="form-input">
                  <option value="">(inherit main)</option>
                  <option v-for="s in schedulers" :key="s" :value="s">{{ s }}</option>
                </select>
              </div>
            </div>

            <div class="form-row">
              <div class="form-group">
                <label class="form-label">Eta</label>
                <input v-model.number="highNoiseEta" type="number" class="form-input" step="0.05" min="0" max="1" />
              </div>
              <div class="form-group">
                <label class="form-label">Shifted Timestep (0 = inherit)</label>
                <input v-model.number="highNoiseShiftedTimestep" type="number" class="form-input" min="0" max="1000" step="50" />
              </div>
            </div>

            <div class="form-group">
              <label class="form-label">Flow Shift (0 = inherit main)</label>
              <input v-model.number="highNoiseFlowShift" type="number" class="form-input" step="0.5" min="0" max="10" />
            </div>

            <div class="form-group">
              <label class="form-label">Extra Sample Args</label>
              <input v-model="highNoiseExtraSampleArgs" type="text" class="form-input" placeholder="key1=val1,key2=val2" />
            </div>

            <div class="form-group">
              <label class="form-label">Custom Sigmas (comma-separated)</label>
              <input v-model="highNoiseCustomSigmas" type="text" class="form-input" placeholder="e.g. 14.6, 8.3, 4.1, 1.8, 0.7" />
              <div class="form-hint">Override the high-noise sigma schedule. Leave blank to use the scheduler.</div>
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
      </div>

      <!-- RIGHT: Sidebar (preview + source images) -->
      <div v-if="showSidebar" class="generate-sidebar">
        <!-- Preview widget (if enabled + generating) -->
        <div v-if="isCurrentJobProcessing && store.previewEnabled" class="card sidebar-preview-card">
          <div class="card-header">
            <h3 class="card-title">Generating...</h3>
            <span v-if="currentJobProgress" class="progress-text">
              Step {{ currentJobProgress.step }} / {{ currentJobProgress.total_steps }}
            </span>
          </div>
          <div class="sidebar-preview-content">
            <div class="sidebar-preview-image-container">
              <img
                v-if="hasPreviewForCurrentJob && store.currentPreview?.image"
                :src="store.currentPreview.image"
                alt="Generation preview"
                class="sidebar-preview-image"
                @click="openPreviewLightbox"
              />
              <div v-else class="sidebar-preview-placeholder">
                <span class="spinner spinner-lg"></span>
                <span class="placeholder-text">Waiting for preview...</span>
              </div>
            </div>
            <ProgressBar
              v-if="currentJobProgress"
              :progress="currentJobProgress.total_steps > 0 ? (currentJobProgress.step / currentJobProgress.total_steps) * 100 : 0"
              :show-label="true"
            />
          </div>
        </div>

        <!-- Progress only (if preview disabled but generating) -->
        <div v-else-if="isCurrentJobProcessing" class="card sidebar-progress-card">
          <div class="card-header">
            <h3 class="card-title">Generating...</h3>
            <span v-if="currentJobProgress" class="progress-text">
              Step {{ currentJobProgress.step }} / {{ currentJobProgress.total_steps }}
            </span>
          </div>
          <div class="sidebar-progress-content">
            <ProgressBar
              v-if="currentJobProgress"
              :progress="currentJobProgress.total_steps > 0 ? (currentJobProgress.step / currentJobProgress.total_steps) * 100 : 0"
              :show-label="true"
            />
          </div>
        </div>

        <!-- img2img: source images -->
        <div v-if="mode === 'img2img'" class="card">
          <div class="card-header">
            <h3 class="card-title">Source Image</h3>
          </div>
          <ImageUploader v-model="initImage" label="Initial Image" />
          <div class="form-group">
            <label class="form-label">Strength: {{ strength.toFixed(2) }}</label>
            <input v-model.number="strength" type="range" class="form-range" min="0" max="1" step="0.05" />
          </div>
          <ImageUploader v-model="maskImage" label="Mask Image (optional - white = repaint)" />
        </div>

        <!-- img_edit: reference image -->
        <div v-if="mode === 'img_edit'" class="card">
          <div class="card-header">
            <h3 class="card-title">Image Edit</h3>
          </div>
          <div class="info-hint">
            Upload a reference image and describe the changes in the prompt.
          </div>
          <ImageUploader v-model="refImage" label="Reference Image" />
        </div>

        <!-- ControlNet input — visible in any generating mode when a
             ControlNet component is loaded on the current model. Placed here
             to match the img2img/img_edit layout (input images live in the
             right column). -->
        <div v-if="hasControlNet && mode !== 'txt2vid'" class="card">
          <div class="card-header">
            <h3 class="card-title">ControlNet</h3>
          </div>
          <ImageUploader v-model="controlImage" label="Control Image" />
          <div class="form-group">
            <label class="form-label">Control Strength: {{ controlStrength.toFixed(2) }}</label>
            <input v-model.number="controlStrength" type="range" class="form-range" min="0" max="1" step="0.05" />
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
/* Layout */
.generate {
  max-width: 1400px;
  margin: 0 auto;
}

/* Resolution row with swap button between W/H inputs.
   .form-row is a CSS grid with auto-fit min 200px columns — without an
   override the swap button would get its own 200px-wide column. Force
   the columns to (1fr | auto | 1fr) so the button takes only its own
   width and the inputs share the rest. align-items:end pins the button
   bottom to the input bottom (otherwise grid items stretch). */
.form-row-resolution {
  grid-template-columns: 1fr auto 1fr;
  align-items: end;
}

.resolution-swap {
  width: 40px;
  height: 40px;       /* matches .form-input height (10+14+10+font line-height) */
  padding: 0;
  font-size: 18px;
  line-height: 1;
  color: var(--text-secondary);
  background: var(--bg-tertiary);
  border: 1px solid var(--border-color);
  border-radius: var(--border-radius-sm);
  cursor: pointer;
  transition: color 0.15s, border-color 0.15s, transform 0.15s;
}

.resolution-swap:hover {
  color: var(--accent-primary);
  border-color: var(--accent-primary);
  transform: rotate(180deg);
}

/* Expand-prompt toggle that appears under the prompt textarea when the
   prompt contains {a|b|c} / {N$$a|b|c} syntax. */
.expand-prompt-toggle {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 10px;
  margin-top: 6px;
}
.expand-count-chip {
  display: inline-block;
  padding: 2px 10px;
  font-family: var(--font-mono, monospace);
  font-size: 11px;
  color: var(--accent-primary);
  background: rgba(var(--accent-rgb, 249, 115, 22), 0.1);
  border: 1px solid rgba(var(--accent-rgb, 249, 115, 22), 0.4);
  border-radius: 10px;
}
.expand-count-error {
  color: var(--accent-error);
  background: rgba(255, 99, 99, 0.1);
  border-color: rgba(255, 99, 99, 0.4);
}

.generate-layout {
  display: grid;
  grid-template-columns: 1fr 380px;
  gap: 24px;
  align-items: start;
}

.generate-layout.single-column {
  grid-template-columns: 1fr;
  max-width: 900px;
}

.generate-main {
  min-width: 0;
}

.generate-sidebar {
  position: sticky;
  top: 24px;
  max-height: calc(100vh - 48px);
  overflow-y: auto;
  display: flex;
  flex-direction: column;
  gap: 16px;
}

/* Tabs */
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

/* Form elements */
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

.rec-clickable {
  cursor: pointer;
}

.rec-clickable:hover {
  text-decoration: underline;
}

.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 12px;
}

.info-hint {
  font-size: 0.85rem;
  color: var(--text-secondary, #888);
  padding: 8px 12px;
  margin-bottom: 12px;
  border-left: 3px solid var(--primary, #6366f1);
  background: var(--surface-alt, rgba(99, 102, 241, 0.05));
  border-radius: 4px;
}

.mt-4 {
  margin-top: 16px;
}

.tiling-options {
  margin-top: 12px;
  padding: 12px;
  background: var(--bg-tertiary);
  border-radius: var(--border-radius-sm);
}

/* Sidebar preview */
.sidebar-preview-card {
  animation: slideIn 0.3s ease-out;
}

.sidebar-progress-card {
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

.sidebar-preview-content {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.sidebar-preview-image-container {
  width: 100%;
  max-height: 500px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: var(--bg-tertiary);
  border-radius: var(--border-radius-sm);
  overflow: hidden;
}

.sidebar-preview-image {
  max-width: 100%;
  max-height: 500px;
  object-fit: contain;
  cursor: pointer;
  transition: transform 0.2s;
}

.sidebar-preview-image:hover {
  transform: scale(1.02);
}

.sidebar-preview-placeholder {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 12px;
  color: var(--text-secondary);
  padding: 40px;
  min-height: 200px;
}

.sidebar-progress-content {
  padding: 4px 0;
}

.placeholder-text {
  font-size: 13px;
}

.spinner-lg {
  width: 32px;
  height: 32px;
}

.progress-text {
  font-size: 13px;
  color: var(--text-secondary);
}

/* Responsive */
@media (max-width: 1024px) {
  .generate-layout {
    grid-template-columns: 1fr;
  }

  .generate-sidebar {
    position: static;
    max-height: none;
  }
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

/* LoRA warning styles */
.lora-warning-hint {
  color: var(--warning-color, #f0ad4e);
  display: block;
  margin-top: 4px;
}
</style>
