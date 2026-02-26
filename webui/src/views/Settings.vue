<script setup lang="ts">
import { ref, computed, watch } from 'vue'
import { useDebounceFn } from '@vueuse/core'
import { useAppStore } from '../stores/app'
import { api, type ModelCapabilitiesResponse } from '../api/client'
import { useSettingsExport } from '../composables/useSettingsExport'
import ThemeSelector from '../components/ThemeSelector.vue'
import SettingField from '../components/settings/SettingField.vue'
import SwitchField from '../components/settings/SwitchField.vue'
import SliderField from '../components/settings/SliderField.vue'

const store = useAppStore()
const { downloadSettings, importSettings } = useSettingsExport()

const activeTab = ref('general')
const loading = ref(false)

// UI Preferences
const desktopNotifications = ref(true)
const theme = ref('default')

// Generation preferences - mode sub-tab
const generationMode = ref<'txt2img' | 'img2img' | 'txt2vid'>('txt2img')
const generationPrefs = ref({
  txt2img: {} as Record<string, unknown>,
  img2img: {} as Record<string, unknown>,
  txt2vid: {} as Record<string, unknown>
})

// Current generation settings (editable)
const genWidth = ref(512)
const genHeight = ref(512)
const genSteps = ref(20)
const genCfgScale = ref(7.0)
const genDistilledGuidance = ref(3.5)
const genSeed = ref(-1)
const genSampler = ref('euler_a')
const genScheduler = ref('discrete')
const genBatchCount = ref(1)
const genClipSkip = ref(-1)
const genSlgScale = ref(0.0)
const genEasycache = ref(false)
const genEasycacheThreshold = ref(0.2)
const genVaeTiling = ref(false)
const genVaeTileSizeX = ref(0)
const genVaeTileSizeY = ref(0)
const genVaeTileOverlap = ref(0.5)
// img2img specific
const genStrength = ref(0.75)
const genControlStrength = ref(0.9)
// txt2vid specific
const genVideoFrames = ref(33)
const genFps = ref(16)
const genFlowShift = ref(3.0)

// Advanced section collapsed state
const showAdvanced = ref(false)

// Preview settings
const previewSettings = ref({
  enabled: true,
  mode: 'tae',
  interval: 1,
  max_size: 256,
  quality: 75
})

// Assistant settings
const assistantSettings = ref({
  enabled: false,
  endpoint: 'http://localhost:11434',
  model: '',
  temperature: 0.7,
  max_tokens: 2000,
  timeout_seconds: 120,
  proactive_suggestions: true,
  system_prompt: ''
})
const assistantDefaultPrompt = ref('')
const assistantConnectionTesting = ref(false)
const assistantConnectionStatus = ref<'unknown' | 'connected' | 'disconnected'>('unknown')
const assistantAvailableModels = ref<string[]>([])
const modelCapabilities = ref<ModelCapabilitiesResponse | null>(null)
const loadingCapabilities = ref(false)

const fileInput = ref<HTMLInputElement | null>(null)
const importing = ref(false)

const tabs = [
  { id: 'general', label: 'General', icon: '⚙️' },
  { id: 'generation', label: 'Generation', icon: '🎯' },
  { id: 'preview', label: 'Preview', icon: '👁️' },
  { id: 'assistant', label: 'Assistant', icon: '🧠' },
  { id: 'themes', label: 'Themes', icon: '🎨' },
  { id: 'import-export', label: 'Import/Export', icon: '📦' }
]

// Samplers and schedulers from store
const samplers = computed(() =>
  store.samplers.length > 0
    ? store.samplers.map(s => ({ value: s, label: s }))
    : [
        { value: 'euler', label: 'euler' },
        { value: 'euler_a', label: 'euler_a' },
        { value: 'heun', label: 'heun' },
        { value: 'dpm2', label: 'dpm2' },
        { value: 'dpm++2s_a', label: 'dpm++2s_a' },
        { value: 'dpm++2m', label: 'dpm++2m' },
        { value: 'dpm++2mv2', label: 'dpm++2mv2' },
        { value: 'ipndm', label: 'ipndm' },
        { value: 'ipndm_v', label: 'ipndm_v' },
        { value: 'lcm', label: 'lcm' },
        { value: 'ddim_trailing', label: 'ddim_trailing' },
        { value: 'tcd', label: 'tcd' }
      ]
)

const schedulers = computed(() =>
  store.schedulers.length > 0
    ? store.schedulers.map(s => ({ value: s, label: s }))
    : [
        { value: 'discrete', label: 'discrete' },
        { value: 'karras', label: 'karras' },
        { value: 'exponential', label: 'exponential' },
        { value: 'ays', label: 'ays' },
        { value: 'gits', label: 'gits' },
        { value: 'sgm_uniform', label: 'sgm_uniform' },
        { value: 'simple', label: 'simple' },
        { value: 'smoothstep', label: 'smoothstep' },
        { value: 'lcm', label: 'lcm' }
      ]
)

// Preview mode options
const previewModeOptions = [
  { value: 'none', label: 'Disabled' },
  { value: 'proj', label: 'Projection (Fast)' },
  { value: 'tae', label: 'TAESD (Balanced)' },
  { value: 'vae', label: 'VAE (High Quality)' }
]

// Assistant model options
const assistantModelOptions = computed(() => {
  if (assistantAvailableModels.value.length === 0) {
    return assistantSettings.value.model
      ? [{ value: assistantSettings.value.model, label: assistantSettings.value.model }]
      : []
  }
  return assistantAvailableModels.value.map(m => ({ value: m, label: m }))
})

async function loadSettings() {
  loading.value = true
  try {
    await Promise.all([
      loadUIPreferences(),
      loadGenerationDefaults(),
      loadPreviewSettings(),
      loadAssistantSettings()
    ])
  } catch (e) {
    console.error('Failed to load settings:', e)
  } finally {
    loading.value = false
  }
}

async function loadUIPreferences() {
  const prefs = await api.getUIPreferences()
  desktopNotifications.value = prefs.desktop_notifications
  theme.value = prefs.theme
}

async function loadGenerationDefaults() {
  const defaults = await api.getGenerationDefaults()
  generationPrefs.value.txt2img = (defaults as any).txt2img || {}
  generationPrefs.value.img2img = (defaults as any).img2img || {}
  generationPrefs.value.txt2vid = (defaults as any).txt2vid || {}
  // Load values for current mode
  loadGenerationModeValues(generationMode.value)
}

function loadGenerationModeValues(mode: 'txt2img' | 'img2img' | 'txt2vid') {
  const prefs = generationPrefs.value[mode] || {}

  // Common params
  genWidth.value = (prefs.width as number) || 512
  genHeight.value = (prefs.height as number) || 512
  genSteps.value = (prefs.steps as number) || 20
  genCfgScale.value = (prefs.cfgScale as number) || 7.0
  genDistilledGuidance.value = (prefs.distilledGuidance as number) || 3.5
  genSeed.value = (prefs.seed as number) ?? -1
  genSampler.value = (prefs.sampler as string) || 'euler_a'
  genScheduler.value = (prefs.scheduler as string) || 'discrete'
  genBatchCount.value = (prefs.batchCount as number) || 1
  genClipSkip.value = (prefs.clipSkip as number) ?? -1
  genSlgScale.value = (prefs.slgScale as number) || 0.0
  genEasycache.value = (prefs.easycache as boolean) || false
  genEasycacheThreshold.value = (prefs.easycacheThreshold as number) || 0.2
  genVaeTiling.value = (prefs.vaeTiling as boolean) || false
  genVaeTileSizeX.value = (prefs.vaeTileSizeX as number) || 0
  genVaeTileSizeY.value = (prefs.vaeTileSizeY as number) || 0
  genVaeTileOverlap.value = (prefs.vaeTileOverlap as number) || 0.5

  // img2img specific
  genStrength.value = (prefs.strength as number) || 0.75
  genControlStrength.value = (prefs.controlStrength as number) || 0.9

  // txt2vid specific
  genVideoFrames.value = (prefs.videoFrames as number) || 33
  genFps.value = (prefs.fps as number) || 16
  genFlowShift.value = (prefs.flowShift as number) || 3.0
}

function getCurrentGenerationSettings() {
  const settings: Record<string, unknown> = {
    width: genWidth.value,
    height: genHeight.value,
    steps: genSteps.value,
    cfgScale: genCfgScale.value,
    distilledGuidance: genDistilledGuidance.value,
    seed: genSeed.value,
    sampler: genSampler.value,
    scheduler: genScheduler.value,
    batchCount: genBatchCount.value,
    clipSkip: genClipSkip.value,
    slgScale: genSlgScale.value,
    easycache: genEasycache.value,
    easycacheThreshold: genEasycacheThreshold.value,
    vaeTiling: genVaeTiling.value,
    vaeTileSizeX: genVaeTileSizeX.value,
    vaeTileSizeY: genVaeTileSizeY.value,
    vaeTileOverlap: genVaeTileOverlap.value
  }

  if (generationMode.value === 'img2img') {
    settings.strength = genStrength.value
    settings.controlStrength = genControlStrength.value
  }

  if (generationMode.value === 'txt2vid') {
    settings.videoFrames = genVideoFrames.value
    settings.fps = genFps.value
    settings.flowShift = genFlowShift.value
  }

  return settings
}

// Debounced save for generation settings
const debouncedSaveGeneration = useDebounceFn(async () => {
  const settings = getCurrentGenerationSettings()
  generationPrefs.value[generationMode.value] = settings
  try {
    await api.updateGenerationDefaultsForMode(generationMode.value, settings as any)
  } catch (e) {
    console.error('Failed to save generation settings:', e)
  }
}, 500)

// Watch for generation mode changes
watch(generationMode, (newMode) => {
  loadGenerationModeValues(newMode)
})

// Watch for generation setting changes
watch([
  genWidth, genHeight, genSteps, genCfgScale, genDistilledGuidance, genSeed,
  genSampler, genScheduler, genBatchCount, genClipSkip, genSlgScale,
  genEasycache, genEasycacheThreshold, genVaeTiling, genVaeTileSizeX,
  genVaeTileSizeY, genVaeTileOverlap, genStrength, genControlStrength,
  genVideoFrames, genFps, genFlowShift
], debouncedSaveGeneration)

async function resetGenerationDefaults() {
  // Reset to architecture defaults if available, otherwise basic defaults
  genWidth.value = 512
  genHeight.value = 512
  genSteps.value = 20
  genCfgScale.value = 7.0
  genDistilledGuidance.value = 3.5
  genSeed.value = -1
  genSampler.value = 'euler_a'
  genScheduler.value = 'discrete'
  genBatchCount.value = 1
  genClipSkip.value = -1
  genSlgScale.value = 0.0
  genEasycache.value = false
  genEasycacheThreshold.value = 0.2
  genVaeTiling.value = false
  genVaeTileSizeX.value = 0
  genVaeTileSizeY.value = 0
  genVaeTileOverlap.value = 0.5
  genStrength.value = 0.75
  genControlStrength.value = 0.9
  genVideoFrames.value = 33
  genFps.value = 16
  genFlowShift.value = 3.0

  // Save immediately
  debouncedSaveGeneration()
  store.showToast('Defaults reset for ' + generationMode.value, 'success')
}

async function loadPreviewSettings() {
  const settings = await api.getPreviewSettings()
  previewSettings.value = settings
}

async function loadAssistantSettings() {
  try {
    const settings = await api.getAssistantSettings()
    assistantSettings.value = settings
    // Store the default prompt for reference
    if (settings.default_system_prompt) {
      assistantDefaultPrompt.value = settings.default_system_prompt
      // If no custom prompt is set, populate with default
      if (!settings.system_prompt) {
        assistantSettings.value.system_prompt = settings.default_system_prompt
      }
    }
    // Try to get available models
    if (settings.enabled) {
      testAssistantConnection()
    }
  } catch (e) {
    // Assistant might not be configured
  }
}

async function saveUIPreferences() {
  try {
    await api.updateUIPreferences({
      desktop_notifications: desktopNotifications.value,
      theme: theme.value
    })
    store.showToast('Settings saved', 'success')
  } catch (e) {
    store.showToast('Failed to save settings', 'error')
  }
}

async function savePreviewSettings() {
  try {
    await api.updatePreviewSettings({
      enabled: previewSettings.value.enabled,
      mode: previewSettings.value.mode as 'none' | 'proj' | 'tae' | 'vae',
      interval: previewSettings.value.interval,
      max_size: previewSettings.value.max_size,
      quality: previewSettings.value.quality
    })
    store.showToast('Preview settings saved', 'success')
  } catch (e) {
    store.showToast('Failed to save preview settings', 'error')
  }
}

// Debounced save for preview settings
const debouncedSavePreview = useDebounceFn(savePreviewSettings, 500)

async function saveAssistantSettings() {
  try {
    const result = await api.updateAssistantSettings(assistantSettings.value)
    if (result.success) {
      assistantSettings.value = result.settings
      store.showToast('Assistant settings saved', 'success')
    }
  } catch (e) {
    store.showToast('Failed to save Assistant settings', 'error')
  }
}

// Debounced save for assistant settings
const debouncedSaveAssistant = useDebounceFn(saveAssistantSettings, 500)

async function testAssistantConnection() {
  assistantConnectionTesting.value = true
  try {
    const status = await api.getAssistantStatus()
    assistantConnectionStatus.value = status.connected ? 'connected' : 'disconnected'
    if (status.available_models) {
      assistantAvailableModels.value = status.available_models
    }
    // Fetch model capabilities after successful connection
    if (status.connected && assistantSettings.value.model) {
      await fetchModelCapabilities(assistantSettings.value.model)
    }
  } catch (e) {
    assistantConnectionStatus.value = 'disconnected'
    assistantAvailableModels.value = []
  } finally {
    assistantConnectionTesting.value = false
  }
}

async function fetchModelCapabilities(modelName: string) {
  if (!modelName) {
    modelCapabilities.value = null
    return
  }

  loadingCapabilities.value = true
  try {
    modelCapabilities.value = await api.getModelCapabilities(modelName)
  } catch (e) {
    console.error('[Settings] Failed to fetch model capabilities:', e)
    modelCapabilities.value = null
  } finally {
    loadingCapabilities.value = false
  }
}

// Watch for model changes to refresh capabilities
watch(
  () => assistantSettings.value.model,
  async (newModel) => {
    if (newModel && assistantConnectionStatus.value === 'connected') {
      await fetchModelCapabilities(newModel)
    } else {
      modelCapabilities.value = null
    }
  }
)

function resetAssistantSystemPrompt() {
  assistantSettings.value.system_prompt = assistantDefaultPrompt.value
  debouncedSaveAssistant()
  store.showToast('System prompt reset to default', 'success')
}

async function handleExport() {
  downloadSettings()
  store.showToast('Settings exported', 'success')
}

function triggerFileInput() {
  fileInput.value?.click()
}

async function handleImport(event: Event) {
  const target = event.target as HTMLInputElement
  const file = target.files?.[0]
  if (!file) return

  importing.value = true
  try {
    await importSettings(file)
    store.showToast('Settings imported', 'success')
    await loadSettings()
  } catch (e) {
    store.showToast('Failed to import settings', 'error')
  } finally {
    importing.value = false
    if (target) target.value = ''
  }
}

loadSettings()
</script>

<template>
  <div class="settings">
    <div class="page-header">
      <h1 class="page-title">Settings</h1>
      <p class="page-subtitle">Configure your experience</p>
    </div>

    <div class="settings-container" v-if="!loading">
      <div class="settings-nav">
        <button
          v-for="tab in tabs"
          :key="tab.id"
          class="nav-item"
          :class="{ active: activeTab === tab.id }"
          @click="activeTab = tab.id"
        >
          <span>{{ tab.icon }}</span>
          <span>{{ tab.label }}</span>
        </button>
      </div>

      <div class="settings-content">
        <div class="settings-section">
          <!-- General Tab -->
          <div v-if="activeTab === 'general'" class="tab-panel">
            <h3>General Settings</h3>

            <div class="settings-card">
              <div class="settings-card-header">
                <h4>Notifications</h4>
              </div>
              <div class="settings-card-body">
                <SwitchField
                  v-model="desktopNotifications"
                  label="Desktop Notifications"
                  description="Show system notifications when generation completes"
                  @update:model-value="saveUIPreferences"
                />
              </div>
            </div>
          </div>

          <!-- Generation Tab -->
          <div v-if="activeTab === 'generation'" class="tab-panel">
            <h3>Generation Defaults</h3>
            <p class="help-text">Configure default parameters for each generation mode. These will be used as starting values on the Generate page.</p>

            <!-- Mode Sub-Tabs -->
            <div class="mode-tabs">
              <button
                :class="['mode-tab', { active: generationMode === 'txt2img' }]"
                @click="generationMode = 'txt2img'"
              >
                Text to Image
              </button>
              <button
                :class="['mode-tab', { active: generationMode === 'img2img' }]"
                @click="generationMode = 'img2img'"
              >
                Image to Image
              </button>
              <button
                :class="['mode-tab', { active: generationMode === 'txt2vid' }]"
                @click="generationMode = 'txt2vid'"
              >
                Text to Video
              </button>
            </div>

            <!-- Core Parameters -->
            <div class="settings-card">
              <div class="settings-card-header">
                <h4>Core Parameters</h4>
              </div>
              <div class="settings-card-body">
                <div class="form-row">
                  <SettingField
                    v-model="genWidth"
                    label="Width"
                    description="Output width in pixels (64-2048)"
                    type="number"
                  />
                  <SettingField
                    v-model="genHeight"
                    label="Height"
                    description="Output height in pixels (64-2048)"
                    type="number"
                  />
                </div>

                <SliderField
                  v-model="genSteps"
                  label="Steps"
                  description="Number of denoising steps. More steps = higher quality but slower."
                  :min="1"
                  :max="150"
                  :step="1"
                  :precision="0"
                />

                <SliderField
                  v-model="genCfgScale"
                  label="CFG Scale"
                  description="Classifier-free guidance scale. Higher values follow prompt more closely."
                  :min="0"
                  :max="30"
                  :step="0.1"
                  :precision="1"
                />

                <SettingField
                  v-model="genSeed"
                  label="Seed"
                  description="Random seed for reproducibility. -1 means random."
                  type="number"
                />
              </div>
            </div>

            <!-- Sampler & Scheduler -->
            <div class="settings-card">
              <div class="settings-card-header">
                <h4>Sampler & Scheduler</h4>
              </div>
              <div class="settings-card-body">
                <div class="form-row">
                  <SettingField
                    v-model="genSampler"
                    label="Sampler"
                    description="Sampling algorithm for denoising"
                    type="select"
                    :options="samplers"
                  />
                  <SettingField
                    v-model="genScheduler"
                    label="Scheduler"
                    description="Noise schedule for sampling"
                    type="select"
                    :options="schedulers"
                  />
                </div>

                <SettingField
                  v-model="genClipSkip"
                  label="CLIP Skip"
                  description="Skip last N layers of CLIP. -1 means default for the model."
                  type="number"
                />

                <SettingField
                  v-model="genBatchCount"
                  label="Batch Count"
                  description="Number of images to generate per job"
                  type="number"
                />
              </div>
            </div>

            <!-- Advanced Options -->
            <details class="settings-card collapsible" :open="showAdvanced">
              <summary class="settings-card-header clickable" @click.prevent="showAdvanced = !showAdvanced">
                <h4>Advanced Options</h4>
                <span class="collapse-icon">{{ showAdvanced ? '▼' : '▶' }}</span>
              </summary>
              <div class="settings-card-body">
                <SliderField
                  v-model="genDistilledGuidance"
                  label="Distilled Guidance"
                  description="Guidance for distilled models (Flux, etc.). Set to 0 to disable."
                  :min="0"
                  :max="10"
                  :step="0.5"
                  :precision="1"
                />

                <SliderField
                  v-model="genSlgScale"
                  label="SLG Scale"
                  description="Skip Layer Guidance scale. 0 to disable."
                  :min="0"
                  :max="10"
                  :step="0.5"
                  :precision="1"
                />

                <SwitchField
                  v-model="genEasycache"
                  label="EasyCache"
                  description="Enable caching for faster DiT model generation"
                />

                <SliderField
                  v-if="genEasycache"
                  v-model="genEasycacheThreshold"
                  label="EasyCache Threshold"
                  description="Cache activation threshold (0-1)"
                  :min="0"
                  :max="1"
                  :step="0.05"
                  :precision="2"
                />

                <SwitchField
                  v-model="genVaeTiling"
                  label="VAE Tiling"
                  description="Process VAE in tiles for large images (reduces VRAM)"
                />

                <div v-if="genVaeTiling" class="nested-settings">
                  <div class="form-row">
                    <SettingField
                      v-model="genVaeTileSizeX"
                      label="Tile Size X"
                      description="Tile width (0 = auto)"
                      type="number"
                    />
                    <SettingField
                      v-model="genVaeTileSizeY"
                      label="Tile Size Y"
                      description="Tile height (0 = auto)"
                      type="number"
                    />
                  </div>
                  <SliderField
                    v-model="genVaeTileOverlap"
                    label="Tile Overlap"
                    description="Overlap between tiles (0-1)"
                    :min="0"
                    :max="1"
                    :step="0.05"
                    :precision="2"
                  />
                </div>
              </div>
            </details>

            <!-- Mode-Specific: img2img -->
            <div v-if="generationMode === 'img2img'" class="settings-card">
              <div class="settings-card-header">
                <h4>Image to Image Settings</h4>
              </div>
              <div class="settings-card-body">
                <SliderField
                  v-model="genStrength"
                  label="Strength"
                  description="How much to transform the input image (0 = no change, 1 = complete regeneration)"
                  :min="0"
                  :max="1"
                  :step="0.05"
                  :precision="2"
                />

                <SliderField
                  v-model="genControlStrength"
                  label="Control Strength"
                  description="ControlNet influence strength"
                  :min="0"
                  :max="1"
                  :step="0.05"
                  :precision="2"
                />
              </div>
            </div>

            <!-- Mode-Specific: txt2vid -->
            <div v-if="generationMode === 'txt2vid'" class="settings-card">
              <div class="settings-card-header">
                <h4>Video Settings</h4>
              </div>
              <div class="settings-card-body">
                <SettingField
                  v-model="genVideoFrames"
                  label="Video Frames"
                  description="Number of frames to generate (1-129)"
                  type="number"
                />

                <SettingField
                  v-model="genFps"
                  label="FPS"
                  description="Frames per second for output video"
                  type="number"
                />

                <SliderField
                  v-model="genFlowShift"
                  label="Flow Shift"
                  description="Temporal flow shift for video models"
                  :min="0"
                  :max="10"
                  :step="0.5"
                  :precision="1"
                />
              </div>
            </div>

            <!-- Reset Button -->
            <div class="settings-actions">
              <button class="btn btn-secondary" @click="resetGenerationDefaults">
                Reset Defaults for {{ generationMode === 'txt2img' ? 'Text to Image' : generationMode === 'img2img' ? 'Image to Image' : 'Text to Video' }}
              </button>
            </div>
          </div>

          <!-- Preview Tab -->
          <div v-if="activeTab === 'preview'" class="tab-panel">
            <h3>Preview Settings</h3>
            <p class="help-text">Configure live preview during image generation.</p>

            <div class="settings-card">
              <div class="settings-card-header">
                <h4>Preview Configuration</h4>
              </div>
              <div class="settings-card-body">
                <SwitchField
                  v-model="previewSettings.enabled"
                  label="Enable Preview"
                  description="Show live preview images during generation"
                  @update:model-value="debouncedSavePreview"
                />

                <div :class="{ 'settings-disabled': !previewSettings.enabled }">
                  <SettingField
                    v-model="previewSettings.mode"
                    label="Preview Mode"
                    description="Method for generating preview images"
                    type="select"
                    :options="previewModeOptions"
                    :disabled="!previewSettings.enabled"
                    @update:model-value="debouncedSavePreview"
                  />

                  <SliderField
                    v-model="previewSettings.interval"
                    label="Preview Interval"
                    description="Generate preview every N steps"
                    :min="1"
                    :max="20"
                    :step="1"
                    :precision="0"
                    unit="steps"
                    :disabled="!previewSettings.enabled"
                    @update:model-value="debouncedSavePreview"
                  />

                  <SliderField
                    v-model="previewSettings.max_size"
                    label="Max Preview Size"
                    description="Maximum dimension of preview images"
                    :min="64"
                    :max="1024"
                    :step="64"
                    :precision="0"
                    unit="px"
                    :disabled="!previewSettings.enabled"
                    @update:model-value="debouncedSavePreview"
                  />

                  <SliderField
                    v-model="previewSettings.quality"
                    label="Preview Quality"
                    description="JPEG quality for preview images"
                    :min="10"
                    :max="100"
                    :step="5"
                    :precision="0"
                    unit="%"
                    :disabled="!previewSettings.enabled"
                    @update:model-value="debouncedSavePreview"
                  />
                </div>
              </div>
            </div>
          </div>

          <!-- Assistant Tab -->
          <div v-if="activeTab === 'assistant'" class="tab-panel">
            <h3>AI Assistant</h3>
            <p class="help-text">Configure the AI assistant for interactive help with generation settings.</p>

            <!-- Connection Status Banner -->
            <div class="connection-status" :class="assistantConnectionStatus">
              <div class="status-info">
                <span class="status-indicator"></span>
                <span class="status-text">
                  {{ assistantConnectionStatus === 'connected' ? 'Connected' :
                     assistantConnectionStatus === 'disconnected' ? 'Disconnected' : 'Unknown' }}
                </span>
                <span class="status-endpoint">{{ assistantSettings.endpoint }}</span>
              </div>
              <button
                class="btn btn-secondary btn-sm"
                @click="testAssistantConnection"
                :disabled="assistantConnectionTesting"
              >
                {{ assistantConnectionTesting ? 'Testing...' : 'Test Connection' }}
              </button>
            </div>

            <!-- Enable Toggle -->
            <div class="settings-card">
              <div class="settings-card-body">
                <SwitchField
                  v-model="assistantSettings.enabled"
                  label="Enable AI Assistant"
                  description="Enable interactive AI assistant for generation help"
                  @update:model-value="debouncedSaveAssistant"
                />

                <SwitchField
                  v-model="assistantSettings.proactive_suggestions"
                  label="Proactive Suggestions"
                  description="Allow assistant to offer suggestions without being asked"
                  :disabled="!assistantSettings.enabled"
                  @update:model-value="debouncedSaveAssistant"
                />
              </div>
            </div>

            <!-- Connection Settings -->
            <div class="settings-card" :class="{ 'settings-disabled': !assistantSettings.enabled }">
              <div class="settings-card-header">
                <h4>Connection Settings</h4>
              </div>
              <div class="settings-card-body">
                <div class="form-row">
                  <SettingField
                    v-model="assistantSettings.endpoint"
                    label="Endpoint URL"
                    description="LLM API endpoint (Ollama or compatible)"
                    type="text"
                    placeholder="http://localhost:11434"
                    :disabled="!assistantSettings.enabled"
                    @update:model-value="debouncedSaveAssistant"
                  />
                  <SettingField
                    v-model="assistantSettings.model"
                    label="Model"
                    :description="assistantAvailableModels.length > 0 ? 'Select from available models' : 'Enter model name'"
                    :type="assistantAvailableModels.length > 0 ? 'select' : 'text'"
                    :options="assistantModelOptions"
                    placeholder="llama2"
                    :disabled="!assistantSettings.enabled"
                    @update:model-value="debouncedSaveAssistant"
                  />
                </div>

                <!-- Model Capabilities Display -->
                <div v-if="assistantSettings.model && assistantConnectionStatus === 'connected'" class="model-capabilities">
                  <div v-if="loadingCapabilities" class="capabilities-loading">
                    <span class="loading-spinner"></span>
                    Loading model info...
                  </div>
                  <div v-else-if="modelCapabilities" class="capabilities-content">
                    <div class="capabilities-badges">
                      <span
                        v-for="cap in modelCapabilities.capabilities"
                        :key="cap"
                        class="capability-badge"
                        :class="{
                          'badge-vision': cap === 'vision',
                          'badge-completion': cap === 'completion',
                          'badge-tools': cap === 'tools'
                        }"
                      >
                        {{ cap }}
                      </span>
                    </div>
                    <div class="capabilities-info">
                      <span v-if="modelCapabilities.context_length" class="info-item">
                        <span class="info-label">Context:</span>
                        {{ modelCapabilities.context_length.toLocaleString() }} tokens
                      </span>
                      <span v-if="modelCapabilities.family" class="info-item">
                        <span class="info-label">Family:</span>
                        {{ modelCapabilities.family }}
                      </span>
                      <span v-if="modelCapabilities.parameter_size" class="info-item">
                        <span class="info-label">Size:</span>
                        {{ modelCapabilities.parameter_size }}
                      </span>
                    </div>
                  </div>
                </div>

                <SettingField
                  v-model="assistantSettings.timeout_seconds"
                  label="Timeout"
                  description="Request timeout in seconds"
                  type="number"
                  :disabled="!assistantSettings.enabled"
                  @update:model-value="debouncedSaveAssistant"
                />
              </div>
            </div>

            <!-- Generation Parameters -->
            <div class="settings-card" :class="{ 'settings-disabled': !assistantSettings.enabled }">
              <div class="settings-card-header">
                <h4>Generation Parameters</h4>
              </div>
              <div class="settings-card-body">
                <SliderField
                  v-model="assistantSettings.temperature"
                  label="Temperature"
                  description="Creativity of responses. Higher = more creative, lower = more focused."
                  :min="0"
                  :max="2"
                  :step="0.1"
                  :precision="1"
                  :disabled="!assistantSettings.enabled"
                  @update:model-value="debouncedSaveAssistant"
                />

                <SettingField
                  v-model="assistantSettings.max_tokens"
                  label="Max Tokens"
                  description="Maximum response length"
                  type="number"
                  :disabled="!assistantSettings.enabled"
                  @update:model-value="debouncedSaveAssistant"
                />
              </div>
            </div>

            <!-- System Prompt -->
            <div class="settings-card" :class="{ 'settings-disabled': !assistantSettings.enabled }">
              <div class="settings-card-header">
                <h4>System Prompt</h4>
                <button
                  class="btn btn-secondary btn-sm"
                  @click="resetAssistantSystemPrompt"
                  :disabled="!assistantSettings.enabled"
                >
                  Reset to Default
                </button>
              </div>
              <div class="settings-card-body">
                <SettingField
                  v-model="assistantSettings.system_prompt"
                  label=""
                  description="System prompt that defines the assistant's behavior and capabilities."
                  type="textarea"
                  :disabled="!assistantSettings.enabled"
                  @update:model-value="debouncedSaveAssistant"
                />
              </div>
            </div>
          </div>

          <!-- Themes Tab -->
          <div v-if="activeTab === 'themes'" class="tab-panel">
            <h3>Theme</h3>
            <ThemeSelector />
          </div>

          <!-- Import/Export Tab -->
          <div v-if="activeTab === 'import-export'" class="tab-panel">
            <h3>Import & Export</h3>
            <div class="export-import-grid">
              <div class="card">
                <h4>Export Settings</h4>
                <p>Download your settings as JSON.</p>
                <button class="btn btn-primary" @click="handleExport">Export</button>
              </div>
              <div class="card">
                <h4>Import Settings</h4>
                <p>Load settings from a JSON file.</p>
                <input ref="fileInput" type="file" style="display: none" @change="handleImport" />
                <button class="btn btn-secondary" @click="triggerFileInput" :disabled="importing">
                  {{ importing ? 'Importing...' : 'Import' }}
                </button>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>

    <div v-if="loading" class="loading">Loading settings...</div>
  </div>
</template>

<style scoped>
.settings {
  max-width: 1400px;
  margin: 0 auto;
  padding: 2rem;
}

.page-header {
  margin-bottom: 2rem;
}

.page-title {
  font-size: 2rem;
  margin: 0 0 0.5rem 0;
  color: var(--text-primary);
}

.page-subtitle {
  font-size: 1rem;
  color: var(--text-secondary);
  margin: 0;
}

.settings-container {
  display: grid;
  grid-template-columns: 200px 1fr;
  gap: 2rem;
}

.settings-nav {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.nav-item {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  padding: 0.75rem 1rem;
  background: var(--bg-secondary);
  border-radius: 6px;
  color: var(--text-secondary);
  cursor: pointer;
  transition: all 0.2s;
}

.nav-item:hover {
  background: var(--bg-hover);
}

.nav-item.active {
  background: var(--accent-primary);
  color: white;
  border-color: var(--accent-primary);
}

.settings-content {
  background: var(--bg-secondary);
  border-radius: 8px;
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.2);
  min-height: 500px;
}

.settings-section {
  padding: 1.5rem;
}

.tab-panel h3 {
  margin: 0 0 0.5rem 0;
  color: var(--text-primary);
}

.help-text {
  color: var(--text-secondary);
  font-size: 0.875rem;
  margin: 0 0 1.5rem 0;
}

/* Mode tabs for Generation */
.mode-tabs {
  display: flex;
  gap: 0.5rem;
  margin-bottom: 1.5rem;
  flex-wrap: wrap;
}

.mode-tab {
  padding: 0.5rem 1rem;
  border: 1px solid var(--border-color);
  background: var(--bg-primary);
  color: var(--text-secondary);
  border-radius: 6px;
  cursor: pointer;
  font-size: 0.875rem;
  font-weight: 500;
  transition: all 0.2s;
}

.mode-tab:hover {
  border-color: var(--accent-primary);
  color: var(--text-primary);
}

.mode-tab.active {
  background: var(--accent-primary);
  color: white;
  border-color: var(--accent-primary);
}

/* Settings Cards */
.settings-card {
  background: var(--bg-primary);
  border-radius: 8px;
  box-shadow: 0 1px 4px rgba(0, 0, 0, 0.15);
  margin-bottom: 1rem;
  overflow: hidden;
}

.settings-card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 0.75rem 1rem;
  background: var(--bg-tertiary);
}

.settings-card-header h4 {
  margin: 0;
  font-size: 0.9375rem;
  font-weight: 600;
  color: var(--text-primary);
}

.settings-card-header.clickable {
  cursor: pointer;
  user-select: none;
}

.settings-card-header.clickable:hover {
  background: var(--bg-hover);
}

.collapse-icon {
  font-size: 0.75rem;
  color: var(--text-secondary);
  transition: transform 0.2s;
}

.settings-card-body {
  padding: 1rem;
}

.settings-card.collapsible summary {
  list-style: none;
}

.settings-card.collapsible summary::-webkit-details-marker {
  display: none;
}

/* Form Row */
.form-row {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 1rem;
}

@media (max-width: 640px) {
  .form-row {
    grid-template-columns: 1fr;
  }
}

/* Nested settings */
.nested-settings {
  margin-top: 1rem;
  padding: 1rem;
  background: var(--bg-tertiary);
  border-radius: 6px;
}

/* Disabled state */
.settings-disabled {
  opacity: 0.5;
  pointer-events: none;
}

/* Connection Status Banner */
.connection-status {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 0.75rem 1rem;
  border-radius: 8px;
  margin-bottom: 1rem;
  background: var(--bg-tertiary);
}

.connection-status.connected {
  border-color: var(--success-color, #22c55e);
  background: rgba(34, 197, 94, 0.1);
}

.connection-status.disconnected {
  border-color: var(--error-color, #ef4444);
  background: rgba(239, 68, 68, 0.1);
}

.status-info {
  display: flex;
  align-items: center;
  gap: 0.75rem;
}

.status-indicator {
  width: 10px;
  height: 10px;
  border-radius: 50%;
  background: var(--text-tertiary);
}

.connection-status.connected .status-indicator {
  background: var(--success-color, #22c55e);
  box-shadow: 0 0 6px var(--success-color, #22c55e);
}

.connection-status.disconnected .status-indicator {
  background: var(--error-color, #ef4444);
}

.status-text {
  font-weight: 500;
  color: var(--text-primary);
}

.status-endpoint {
  color: var(--text-secondary);
  font-size: 0.8125rem;
}

/* Model Capabilities */
.model-capabilities {
  margin-top: 1rem;
  padding: 0.875rem 1rem;
  background: var(--bg-secondary);
  border-radius: 8px;
}

.capabilities-loading {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  color: var(--text-secondary);
  font-size: 0.875rem;
}

.loading-spinner {
  width: 14px;
  height: 14px;
  border: 2px solid var(--border-color);
  border-top-color: var(--accent-primary);
  border-radius: 50%;
  animation: spin 0.8s linear infinite;
}

@keyframes spin {
  to { transform: rotate(360deg); }
}

.capabilities-content {
  display: flex;
  flex-direction: column;
  gap: 0.75rem;
}

.capabilities-badges {
  display: flex;
  flex-wrap: wrap;
  gap: 0.5rem;
}

.capability-badge {
  padding: 0.25rem 0.625rem;
  font-size: 0.75rem;
  font-weight: 500;
  text-transform: uppercase;
  letter-spacing: 0.025em;
  border-radius: 4px;
  background: var(--bg-tertiary);
  color: var(--text-secondary);
  border: 1px solid var(--border-color);
}

.capability-badge.badge-vision {
  background: rgba(139, 92, 246, 0.15);
  color: #a78bfa;
  border-color: rgba(139, 92, 246, 0.3);
}

.capability-badge.badge-completion {
  background: rgba(34, 197, 94, 0.15);
  color: #4ade80;
  border-color: rgba(34, 197, 94, 0.3);
}

.capability-badge.badge-tools {
  background: rgba(59, 130, 246, 0.15);
  color: #60a5fa;
  border-color: rgba(59, 130, 246, 0.3);
}

.capabilities-info {
  display: flex;
  flex-wrap: wrap;
  gap: 1rem;
}

.info-item {
  font-size: 0.8125rem;
  color: var(--text-secondary);
}

.info-label {
  color: var(--text-tertiary);
  margin-right: 0.25rem;
}

/* Settings Actions */
.settings-actions {
  margin-top: 1.5rem;
  display: flex;
  gap: 1rem;
}

/* Export/Import Grid */
.export-import-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
  gap: 1.5rem;
}

.card {
  padding: 1.5rem;
  background: var(--bg-primary);
  border: 1px solid var(--border-color);
  border-radius: 8px;
}

.card h4 {
  margin: 0 0 0.5rem 0;
  color: var(--text-primary);
}

.card p {
  margin: 0 0 1rem 0;
  color: var(--text-secondary);
  font-size: 0.875rem;
}

/* Buttons */
.btn {
  padding: 0.625rem 1.25rem;
  border: none;
  border-radius: 6px;
  font-weight: 500;
  cursor: pointer;
  transition: opacity 0.2s, background-color 0.2s;
}

.btn-primary {
  background: var(--accent-primary);
  color: white;
}

.btn-primary:hover {
  opacity: 0.9;
}

.btn-secondary {
  background: var(--bg-hover);
  color: var(--text-primary);
  border: 1px solid var(--border-color);
}

.btn-secondary:hover {
  background: var(--bg-tertiary);
}

.btn-sm {
  padding: 0.375rem 0.75rem;
  font-size: 0.8125rem;
}

.btn:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.loading {
  padding: 4rem;
  text-align: center;
  color: var(--text-secondary);
}

@media (max-width: 768px) {
  .settings {
    padding: 1rem;
  }

  .settings-container {
    grid-template-columns: 1fr;
  }

  .settings-nav {
    flex-direction: row;
    overflow-x: auto;
    padding-bottom: 0.5rem;
  }

  .nav-item {
    white-space: nowrap;
    flex-shrink: 0;
  }

  .mode-tabs {
    overflow-x: auto;
    flex-wrap: nowrap;
    -webkit-overflow-scrolling: touch;
  }

  .mode-tab {
    flex-shrink: 0;
  }

  .connection-status {
    flex-direction: column;
    gap: 0.75rem;
    align-items: flex-start;
  }

  .connection-status .btn {
    width: 100%;
  }
}
</style>
