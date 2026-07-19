<script setup lang="ts">
import { ref, computed, onMounted, watch } from 'vue'
import { useRouter } from 'vue-router'
import { useAppStore } from '../stores/app'
import { api, type LoadModelParams, type ModelInfo, type ArchitecturePreset, type OptionDescription } from '../api/client'
import { useArchitectures, extractWeightType, suggestComponents, findPresetForArchitecture, type ComponentSuggestion } from '../composables/useArchitectures'
import RecHint from '../components/RecHint.vue'
import BackendAssignmentEditor from '../components/BackendAssignmentEditor.vue'

const props = defineProps<{
  modelName: string
}>()

const router = useRouter()
const store = useAppStore()
const { detectArchitecture, detecting, architectures } = useArchitectures()

// State
const loading = ref(false)
const selectedModel = ref<ModelInfo | null>(null)
const selectedArchitecture = ref<string>('')
const isAutoDetected = ref(false)
const optionDescriptions = ref<Record<string, OptionDescription>>({})

// True when any model is currently loaded server-side. The backend
// refuses POST /models/load with 409 in this state, so we mirror that
// in the UI: disable the Load button and offer an Unload action.
const anyModelLoaded = computed(() => store.modelLoaded)
const loadedModelName = computed(() => store.health?.model_name ?? '')

// Three-state button label:
//   - clicking on the model that's already loaded → "Apply Changes"
//     (we unload-then-load it with the form's current options).
//   - clicking on a different model while another is loaded → "Switch Model"
//     (same orchestration; just labelled honestly so the user understands
//     it'll unload the current one first).
//   - nothing loaded → "Load Model".
const isReloadingSame = computed(() =>
  anyModelLoaded.value && loadedModelName.value === selectedModel.value?.name
)
const loadButtonLabel = computed(() => {
  if (loading.value) return 'Loading...'
  if (isReloadingSame.value) return 'Apply Changes'
  if (anyModelLoaded.value) return 'Switch Model'
  return 'Load Model'
})
const loadButtonTitle = computed(() => {
  if (isReloadingSame.value) return 'Unload and reload with current options'
  if (anyModelLoaded.value) return `Unload ${loadedModelName.value} first, then load this model`
  return ''
})

// Load params
const loadParams = ref<LoadModelParams>({
  model_name: '',
  model_type: 'checkpoint',
  vae: '',
  clip_l: '',
  clip_g: '',
  t5xxl: '',
  controlnet: '',
  motion_module: '',
  llm: '',
  taesd: '',
  options: {
    n_threads: -1,
    flash_attn: true,
    diffusion_flash_attn: false,
    enable_mmap: true,
    vae_conv_direct: false,
    diffusion_conv_direct: false,
    tae_preview_only: false,
    force_sdxl_vae_conv_scale: false,
    max_vram: 0,
    // Residency-aware streaming (unified in leejet master post-1ceb5bd).
    stream_layers: false,
    // Eager-load params at model-load time (leejet PR #1687). Default true
    // in the restapi — long-lived server, first generation should be fast.
    eager_load: true,
    // Compute / params backend overrides (replace keep_*/offload_to_cpu).
    backend: '',
    params_backend: '',
    rpc_servers: '',
    // Weight type + per-tensor rules (empty = auto-detect from file metadata).
    weight_type: '',
    tensor_type_rules: '',
    // RNG / prediction / LoRA-apply — defaults match the restapi struct defaults
    // so the <select> shows the correct option highlighted on first render.
    rng_type: 'cuda',
    sampler_rng_type: '',
    prediction: '',
    lora_apply_mode: 'auto',
    // Model-specific args (leejet PR #1757). Empty string = defaults.
    // Replaces chroma_use_dit_mask / chroma_use_t5_mask / chroma_t5_mask_pad /
    // qwen_image_zero_cond_t individual flags.
    model_args: '',
    // VAE format override.
    vae_format: 'auto'
    // circular_x / circular_y moved to per-generation in leejet PR #1748 —
    // toggling no longer requires a model reload. See the Generate page.
  }
})

// Computed: Available models by type
const vaeModels = computed(() => store.models?.vae || [])
const clipModels = computed(() => store.models?.clip || [])
const t5Models = computed(() => store.models?.t5 || [])
const controlnetModels = computed(() => store.models?.controlnets || [])
const motionModuleModels = computed(() => store.models?.motion_modules || [])
const llmModels = computed(() => store.models?.llm || [])
const taesdModels = computed(() => store.models?.taesd || [])

// Computed: Current architecture preset. Uses the JSON-driven resolver so
// `selectedArchitecture` values that aren't exact preset keys (e.g. the
// "SeFi-Image" string sd.cpp reports, when the actual presets are
// SeFi-Image-Turbo / -Base / -RL) still resolve via match.architecture +
// match.nameRegex against the loaded model's filename.
const currentArchitecture = computed((): ArchitecturePreset | null =>
  findPresetForArchitecture(
    selectedArchitecture.value,
    props.modelName,
    store.architectures?.architectures
  )
)

// Computed: Model weight type
const modelWeightType = computed(() => extractWeightType(props.modelName))

// Computed: Component suggestions. Pass the resolved preset so JSON-driven
// componentScoring rules apply (the legacy hardcoded scoring keeps working
// as a fallback for presets that don't have componentScoring entries).
const vaeSuggestions = computed(() =>
  suggestComponents('vae', currentArchitecture.value, modelWeightType.value, vaeModels.value)
)
const clipSuggestions = computed(() =>
  suggestComponents('clip', currentArchitecture.value, modelWeightType.value, clipModels.value)
)
const t5Suggestions = computed(() =>
  suggestComponents('t5', currentArchitecture.value, modelWeightType.value, t5Models.value)
)
const llmSuggestions = computed(() =>
  suggestComponents('llm', currentArchitecture.value, modelWeightType.value, llmModels.value)
)

// Computed: Required components based on architecture
const requiredComponents = computed(() => {
  if (!currentArchitecture.value) return {}
  return currentArchitecture.value.requiredComponents || {}
})

// Format file size
function formatSize(bytes: number): string {
  const units = ['B', 'KB', 'MB', 'GB', 'TB']
  let size = bytes
  let unitIndex = 0
  while (size >= 1024 && unitIndex < units.length - 1) {
    size /= 1024
    unitIndex++
  }
  return `${size.toFixed(1)} ${units[unitIndex]}`
}

// Get option description
function getOptionDesc(key: string): OptionDescription | undefined {
  return optionDescriptions.value[key]
}

// Apply architecture load options
function applyArchitectureOptions(arch: ArchitecturePreset) {
  if (arch.loadOptions) {
    loadParams.value.options = {
      ...loadParams.value.options,
      ...arch.loadOptions as Record<string, unknown>
    }
  }
}

// Handle architecture change
function onArchitectureChange(archId: string) {
  selectedArchitecture.value = archId
  isAutoDetected.value = false
  const arch = store.architectures?.architectures?.[archId]
  if (arch) {
    applyArchitectureOptions(arch)
  }
}

// Auto-select suggested component
function autoSelectComponent(
  field: 'vae' | 'clip_l' | 'clip_g' | 't5xxl' | 'llm',
  suggestions: ComponentSuggestion[]
) {
  const suggested = suggestions.find(s => s.isSuggested)
  if (suggested && !loadParams.value[field]) {
    loadParams.value[field] = suggested.name
  }
}

// Load the model. If a model is already loaded, the backend now refuses
// /models/load with 409 — so we transparently call /models/unload first
// (synchronous on the server, fast on every backend) and then submit the
// new load. From the user's perspective this is "Apply Changes": same
// model with different options, or swap to a different model entirely.
async function handleLoadModel() {
  loading.value = true
  try {
    if (anyModelLoaded.value) {
      store.showToast(`Unloading ${loadedModelName.value}...`, 'info')
      await api.unloadModel()
      store.fetchHealth()
    }

    const params: LoadModelParams = {
      model_name: loadParams.value.model_name,
      model_type: loadParams.value.model_type
    }

    // Add optional components
    if (loadParams.value.vae) params.vae = loadParams.value.vae
    if (loadParams.value.clip_l) params.clip_l = loadParams.value.clip_l
    if (loadParams.value.clip_g) params.clip_g = loadParams.value.clip_g
    if (loadParams.value.t5xxl) params.t5xxl = loadParams.value.t5xxl
    if (loadParams.value.controlnet) params.controlnet = loadParams.value.controlnet
    if (loadParams.value.motion_module) params.motion_module = loadParams.value.motion_module
    if (loadParams.value.llm) params.llm = loadParams.value.llm
    if (loadParams.value.taesd) params.taesd = loadParams.value.taesd
    params.options = loadParams.value.options

    // Backend now returns 202 Accepted immediately; the actual load runs
    // in a detached thread and reports completion via WebSocket events
    // (model_loaded / model_load_failed). The api client doesn't reject
    // on 202 (any 2xx is "ok"), so this await resolves quickly.
    await api.loadModel(params)
    // Persist the params we used so the form can restore them next time.
    store.setLastLoadOptions(params)
    store.showToast('Model load started — progress in the status bar.', 'info')
    store.fetchHealth()
    router.push('/models')
  } catch (e) {
    store.showToast(e instanceof Error ? e.message : 'Failed to load model', 'error')
  } finally {
    loading.value = false
  }
}

// Initialize on mount
onMounted(async () => {
  // Always refresh /health so the loaded-model state we restore from is
  // fresh. Without this, navigating Models → Edit right after loading
  // could see a stale `is_loaded` flag on the cached models list and skip
  // the restore branch — the form would then show defaults instead of
  // the values the user actually loaded with.
  await store.fetchHealth()

  // Ensure we have models data
  if (!store.models) {
    await store.fetchModels()
  }

  // Always refetch architectures on mount — drops the stale-cache problem
  // where a tab opened before the server was upgraded keeps an old preset
  // list and would silently miss newly-added presets (SeFi-Image, etc.).
  // Cheap call, returns ~30 KB on a fully-stocked install.
  await store.fetchArchitectures()

  // Fetch option descriptions
  try {
    const desc = await api.getOptionDescriptions()
    optionDescriptions.value = desc.options || {}
  } catch (e) {
    console.error('Failed to load option descriptions:', e)
  }

  // Find the selected model
  const allModels = [
    ...(store.models?.checkpoints || []),
    ...(store.models?.diffusion_models || [])
  ]
  const decodedName = decodeURIComponent(props.modelName)
  selectedModel.value = allModels.find(m => m.name === decodedName) || null

  if (selectedModel.value) {
    loadParams.value.model_name = selectedModel.value.name
    loadParams.value.model_type = selectedModel.value.type === 'diffusion' ? 'diffusion' : 'checkpoint'

    // Track whether we already restored prior user choices (from /health or
    // localStorage). If so, architecture auto-detect should NOT clobber the
    // user's options with raw architecture defaults.
    let restoredFromPriorLoad = false

    // Decide "is THIS model currently loaded?" from live /health, not just
    // the cached `is_loaded` flag on the models list. The list can be
    // stale (e.g. last fetched before the load completed); /health is the
    // source of truth. Comparing the canonical loaded name to the
    // selected model name closes that window.
    const thisModelLoaded =
      (selectedModel.value.is_loaded === true) ||
      (store.modelLoaded && store.health?.model_name === selectedModel.value.name)

    // If model is already loaded, pre-populate with current components and options
    if (thisModelLoaded && store.loadedComponents) {
      loadParams.value.vae = store.loadedComponents.vae || ''
      loadParams.value.clip_l = store.loadedComponents.clip_l || ''
      loadParams.value.clip_g = store.loadedComponents.clip_g || ''
      loadParams.value.t5xxl = store.loadedComponents.t5xxl || ''
      loadParams.value.controlnet = store.loadedComponents.controlnet || ''
      loadParams.value.motion_module = (store.loadedComponents as Record<string, string | null | undefined>).motion_module || ''
      loadParams.value.llm = store.loadedComponents.llm || ''

      // Pre-populate load options from currently loaded model.
      // Spread store.loadOptions wholesale so every field the backend exposes
      // is restored — without this, fields not explicitly listed (e.g.
      // vae_conv_direct, diffusion_conv_direct, lora_apply_mode, rng_type,
      // chroma_*, prediction, backend, params_backend, ...) get silently
      // dropped on edit. The pre-existing loadParams.options layer underneath
      // provides defaults for any field the backend didn't return (undefined
      // keys are not enumerated by spread).
      if (store.loadOptions) {
        loadParams.value.options = {
          ...loadParams.value.options,
          ...store.loadOptions,
        }
        restoredFromPriorLoad = true
      }

      // Use current architecture if loaded
      if (store.modelArchitecture) {
        selectedArchitecture.value = store.modelArchitecture
      }
    } else if (store.lastLoadOptions && store.lastLoadOptions.model_name === selectedModel.value.name) {
      // No model currently loaded, but user previously loaded *this* model in
      // a prior session — restore those choices from localStorage instead of
      // overwriting with raw architecture defaults.
      const last = store.lastLoadOptions
      loadParams.value.vae = last.vae || ''
      loadParams.value.clip_l = last.clip_l || ''
      loadParams.value.clip_g = last.clip_g || ''
      loadParams.value.t5xxl = last.t5xxl || ''
      loadParams.value.controlnet = last.controlnet || ''
      loadParams.value.motion_module = (last as unknown as { motion_module?: string | null }).motion_module || ''
      loadParams.value.llm = last.llm || ''
      loadParams.value.taesd = last.taesd || ''
      if (last.options) {
        loadParams.value.options = {
          ...loadParams.value.options,
          ...last.options
        }
      }
      restoredFromPriorLoad = true
    }

    // Auto-detect architecture from model name
    const detected = await detectArchitecture(decodedName)
    if (detected) {
      selectedArchitecture.value = detected.id
      isAutoDetected.value = true
      // Only apply raw architecture defaults if we did NOT restore prior user
      // options — otherwise the detected arch's loadOptions would clobber
      // values like stream_layers / params_backend the user already chose.
      if (!restoredFromPriorLoad) {
        applyArchitectureOptions(detected)
      }
    }
  }
})

// Watch for architecture changes to auto-suggest components
watch(selectedArchitecture, () => {
  if (currentArchitecture.value) {
    // Auto-select suggested components for required fields
    if (requiredComponents.value.vae) {
      autoSelectComponent('vae', vaeSuggestions.value)
    }
    if (requiredComponents.value.clip_l || requiredComponents.value.clip) {
      autoSelectComponent('clip_l', clipSuggestions.value)
    }
    if (requiredComponents.value.t5xxl || requiredComponents.value.t5) {
      autoSelectComponent('t5xxl', t5Suggestions.value)
    }
    if (requiredComponents.value.llm) {
      autoSelectComponent('llm', llmSuggestions.value)
    }
  }
})

// Convenience: when the user enables stream_layers, default params_backend
// to "*=cpu" if it's empty. This mirrors sd-cli's --offload-to-cpu helper,
// which prepends "*=cpu" to params_backend. We never *clear* params_backend
// when stream_layers flips off — the user may have set a custom value
// independently (e.g. "vae=cpu") that we shouldn't stomp.
watch(() => loadParams.value.options?.stream_layers, (enabled) => {
  if (enabled && loadParams.value.options && !loadParams.value.options.params_backend) {
    loadParams.value.options.params_backend = '*=cpu'
  }
})

// Convenience toggle for the most common params_backend pattern. Ticked →
// the entire string becomes "*=cpu" (global "keep all weights in RAM").
// Unticked → if the current value is exactly "*=cpu" we clear it; if it's
// some custom value the user typed themselves (e.g. "vae=cpu,diffusion=cuda0")
// we leave it alone — the toggle only owns the "=cpu" preset case.
function onKeepAllInRam(e: Event) {
  const checked = (e.target as HTMLInputElement).checked
  if (!loadParams.value.options) return
  if (checked) {
    loadParams.value.options.params_backend = '*=cpu'
  } else if (loadParams.value.options.params_backend === '*=cpu') {
    loadParams.value.options.params_backend = ''
  }
}

</script>

<template>
  <div class="model-load">
    <!-- Header -->
    <div class="page-header">
      <div class="header-left">
        <button class="btn btn-ghost" @click="router.push('/models')">
          &larr; Back to Models
        </button>
      </div>
      <div class="header-right">
        <button
          class="btn btn-primary"
          @click="handleLoadModel"
          :disabled="loading || !selectedModel"
          :title="loadButtonTitle"
        >
          <span v-if="loading" class="spinner"></span>
          {{ loadButtonLabel }}
        </button>
      </div>
    </div>

    <!-- Model not found -->
    <div v-if="!selectedModel && !store.loading" class="card error-card">
      <h3>Model Not Found</h3>
      <p>The model "{{ props.modelName }}" was not found in the models list.</p>
      <button class="btn btn-primary" @click="router.push('/models')">Back to Models</button>
    </div>

    <!-- Loading state -->
    <div v-else-if="store.loading" class="loading-state">
      <div class="spinner"></div>
      <p>Loading model information...</p>
    </div>

    <!-- Model load form -->
    <div v-else-if="selectedModel" class="load-form">
      <!-- Already-loaded notice. We don't block — clicking the primary
           button performs unload+load — but we surface the state so the
           user knows the current model will be replaced. -->
      <div v-if="anyModelLoaded && !isReloadingSame" class="card warning-banner">
        <strong>{{ loadedModelName }} is currently loaded</strong>
        <p>Clicking <em>Switch Model</em> will unload it, then load the model below.</p>
      </div>
      <div v-else-if="isReloadingSame" class="card warning-banner warning-banner-info">
        <strong>This model is currently loaded</strong>
        <p>Clicking <em>Apply Changes</em> will unload it and reload it with the options below.</p>
      </div>

      <!-- Model Information -->
      <section class="card section-card">
        <h2 class="section-title">Model Information</h2>
        <div class="model-info-grid">
          <div class="info-item">
            <label>Name</label>
            <span class="info-value">{{ selectedModel.name }}</span>
          </div>
          <div class="info-item">
            <label>Size</label>
            <span class="info-value">{{ formatSize(selectedModel.size_bytes) }}</span>
          </div>
          <div class="info-item">
            <label>Type</label>
            <span class="info-value">{{ selectedModel.type }}</span>
          </div>
          <div class="info-item">
            <label>Format</label>
            <span class="info-value">{{ selectedModel.file_extension.toUpperCase() }}</span>
          </div>
          <div v-if="modelWeightType" class="info-item">
            <label>Weight Type</label>
            <span class="info-value weight-badge">{{ modelWeightType.toUpperCase() }}</span>
          </div>
          <div v-if="selectedModel.is_loaded" class="info-item">
            <label>Status</label>
            <span class="info-value status-loaded">Currently Loaded</span>
          </div>
        </div>
      </section>

      <!-- Architecture Selection -->
      <section class="card section-card">
        <h2 class="section-title">
          Architecture
          <span v-if="isAutoDetected && selectedArchitecture" class="auto-badge">Auto-detected</span>
          <span v-if="detecting" class="detecting-badge">Detecting...</span>
        </h2>
        <div class="form-group">
          <select
            :value="selectedArchitecture"
            @change="onArchitectureChange(($event.target as HTMLSelectElement).value)"
            class="form-select"
          >
            <option value="">Select architecture...</option>
            <option v-for="arch in architectures" :key="arch.id" :value="arch.id">
              {{ arch.name }}
            </option>
          </select>
        </div>
        <p v-if="currentArchitecture" class="architecture-description">
          {{ currentArchitecture.description }}
        </p>
        <!-- SeFi-Image was merged into leejet/master (PR #1707, commit
             03e9a22), so the previous "needs SD_SEFI_IMAGE=ON" warning is
             no longer applicable. Left as a historical comment in case we
             need a similar gate for some future experimental architecture. -->
        <div v-if="currentArchitecture?.generationDefaults" class="generation-defaults">
          <strong>Recommended settings:</strong>
          <span v-if="currentArchitecture.generationDefaults.cfg_scale">
            CFG {{ currentArchitecture.generationDefaults.cfg_scale }},
          </span>
          <span v-if="currentArchitecture.generationDefaults.steps">
            {{ currentArchitecture.generationDefaults.steps }} steps,
          </span>
          <span v-if="currentArchitecture.generationDefaults.width && currentArchitecture.generationDefaults.height">
            {{ currentArchitecture.generationDefaults.width }}x{{ currentArchitecture.generationDefaults.height }}
          </span>
        </div>
      </section>

      <!-- Required Components -->
      <section v-if="currentArchitecture && Object.keys(requiredComponents).length > 0" class="card section-card">
        <h2 class="section-title">Required Components</h2>
        <div class="components-grid">
          <!-- VAE -->
          <div v-if="requiredComponents.vae" class="form-group">
            <label class="form-label">
              VAE <span class="required-badge">Required</span>
            </label>
            <select v-model="loadParams.vae" class="form-select">
              <option value="">Select VAE...</option>
              <option
                v-for="s in vaeSuggestions"
                :key="s.name"
                :value="s.name"
              >
                {{ s.name }}{{ s.isSuggested ? ' ★' : '' }}
              </option>
            </select>
            <small class="form-hint">{{ requiredComponents.vae }}</small>
          </div>

          <!-- CLIP-L -->
          <div v-if="requiredComponents.clip_l || requiredComponents.clip" class="form-group">
            <label class="form-label">
              CLIP-L <span class="required-badge">Required</span>
            </label>
            <select v-model="loadParams.clip_l" class="form-select">
              <option value="">Select CLIP-L...</option>
              <option
                v-for="s in clipSuggestions"
                :key="s.name"
                :value="s.name"
              >
                {{ s.name }}{{ s.isSuggested ? ' ★' : '' }}
              </option>
            </select>
            <small class="form-hint">{{ requiredComponents.clip_l || requiredComponents.clip }}</small>
          </div>

          <!-- CLIP-G -->
          <div v-if="requiredComponents.clip_g" class="form-group">
            <label class="form-label">
              CLIP-G <span class="required-badge">Required</span>
            </label>
            <select v-model="loadParams.clip_g" class="form-select">
              <option value="">Select CLIP-G...</option>
              <option v-for="m in clipModels" :key="m.name" :value="m.name">{{ m.name }}</option>
            </select>
            <small class="form-hint">{{ requiredComponents.clip_g }}</small>
          </div>

          <!-- T5-XXL -->
          <div v-if="requiredComponents.t5xxl || requiredComponents.t5" class="form-group">
            <label class="form-label">
              T5-XXL <span class="required-badge">Required</span>
            </label>
            <select v-model="loadParams.t5xxl" class="form-select">
              <option value="">Select T5-XXL...</option>
              <option
                v-for="s in t5Suggestions"
                :key="s.name"
                :value="s.name"
              >
                {{ s.name }}{{ s.isSuggested ? ' ★' : '' }}
              </option>
            </select>
            <small class="form-hint">{{ requiredComponents.t5xxl || requiredComponents.t5 }}</small>
          </div>

          <!-- LLM -->
          <div v-if="requiredComponents.llm" class="form-group">
            <label class="form-label">
              LLM <span class="required-badge">Required</span>
            </label>
            <select v-model="loadParams.llm" class="form-select">
              <option value="">Select LLM...</option>
              <option
                v-for="s in llmSuggestions"
                :key="s.name"
                :value="s.name"
              >
                {{ s.name }}{{ s.isSuggested ? ' ★' : '' }}
              </option>
            </select>
            <small class="form-hint">{{ requiredComponents.llm }}</small>
          </div>
        </div>
      </section>

      <!-- Optional Components -->
      <section class="card section-card">
        <h2 class="section-title">Optional Components</h2>
        <div class="components-grid">
          <!-- VAE (if not required) -->
          <div v-if="!requiredComponents.vae" class="form-group">
            <label class="form-label">VAE <span class="optional-badge">Optional</span></label>
            <select v-model="loadParams.vae" class="form-select">
              <option value="">None</option>
              <option
                v-for="s in vaeSuggestions"
                :key="s.name"
                :value="s.name"
              >
                {{ s.name }}{{ s.isSuggested ? ' ★' : '' }}
              </option>
            </select>
          </div>

          <!-- CLIP-L (if not required) -->
          <div v-if="!requiredComponents.clip_l && !requiredComponents.clip" class="form-group">
            <label class="form-label">CLIP-L <span class="optional-badge">Optional</span></label>
            <select v-model="loadParams.clip_l" class="form-select">
              <option value="">None</option>
              <option v-for="m in clipModels" :key="m.name" :value="m.name">{{ m.name }}</option>
            </select>
          </div>

          <!-- ControlNet -->
          <div class="form-group">
            <label class="form-label">ControlNet <span class="optional-badge">Optional</span></label>
            <select v-model="loadParams.controlnet" class="form-select">
              <option value="">None</option>
              <option v-for="m in controlnetModels" :key="m.name" :value="m.name">{{ m.name }}</option>
            </select>
          </div>

          <!-- Motion module (AnimateDiff / PiD, SD1.5 only) -->
          <div class="form-group">
            <label class="form-label">Motion Module <span class="optional-badge">Optional</span></label>
            <select v-model="loadParams.motion_module" class="form-select">
              <option value="">None</option>
              <option v-for="m in motionModuleModels" :key="m.name" :value="m.name">{{ m.name }}</option>
            </select>
            <small class="form-hint">AnimateDiff / PiD motion module for SD1.5. Attaches at load time; enables short-video output.</small>
          </div>

          <!-- TAESD -->
          <div class="form-group">
            <label class="form-label">TAESD (Preview) <span class="optional-badge">Optional</span></label>
            <select v-model="loadParams.taesd" class="form-select">
              <option value="">None</option>
              <option v-for="m in taesdModels" :key="m.name" :value="m.name">{{ m.name }}</option>
            </select>
            <small class="form-hint">Tiny AutoEncoder for fast preview generation</small>
          </div>

          <!-- LLM (if not required) -->
          <div v-if="!requiredComponents.llm" class="form-group">
            <label class="form-label">LLM <span class="optional-badge">Optional</span></label>
            <select v-model="loadParams.llm" class="form-select">
              <option value="">None</option>
              <option v-for="m in llmModels" :key="m.name" :value="m.name">{{ m.name }}</option>
            </select>
          </div>
        </div>
      </section>

      <!-- Advanced Options -->
      <details class="card section-card accordion">
        <summary class="accordion-header">Advanced Options</summary>
        <div class="accordion-content">
          <!-- Performance -->
          <div class="options-group">
            <h4 class="options-group-title">Performance</h4>
            <div class="options-grid">
              <label class="form-checkbox" :title="getOptionDesc('flash_attn')?.description">
                <input v-model="loadParams.options!.flash_attn" type="checkbox" />
                <span>Flash Attention</span>
                <RecHint :desc="getOptionDesc('flash_attn')" />
              </label>
              <label class="form-checkbox" :title="getOptionDesc('diffusion_flash_attn')?.description">
                <input v-model="loadParams.options!.diffusion_flash_attn" type="checkbox" />
                <span>Diffusion Flash Attention</span>
                <RecHint :desc="getOptionDesc('diffusion_flash_attn')" />
              </label>
              <label class="form-checkbox" :title="getOptionDesc('enable_mmap')?.description">
                <input v-model="loadParams.options!.enable_mmap" type="checkbox" />
                <span>Memory-mapped Loading</span>
                <RecHint :desc="getOptionDesc('enable_mmap')" />
              </label>
              <div class="form-group inline-group">
                <label class="form-label">Threads (-1 = auto)</label>
                <input v-model.number="loadParams.options!.n_threads" type="number" class="form-input small" />
              </div>
            </div>
          </div>

          <!-- Memory Management.
               leejet PR #1654 routed CPU placement through backend specs and
               deleted the per-component bool flags (keep_clip_on_cpu,
               keep_vae_on_cpu, keep_controlnet_on_cpu, offload_to_cpu,
               vae_decode_only, free_params_immediately). Placement now goes
               through `backend` / `params_backend` strings — see the
               "Backend Placement" group below. Max VRAM is the GiB budget
               for the graph-cut planner. -->
          <div class="options-group">
            <h4 class="options-group-title">Memory Management</h4>
            <div class="form-group" :title="getOptionDesc('max_vram')?.description">
              <label class="form-label">Max VRAM (GiB) — graph-cut offload</label>
              <input
                v-model.number="loadParams.options!.max_vram"
                type="number"
                class="form-input"
                min="0"
                step="0.5"
                placeholder="0 = disabled"
              />
              <RecHint :desc="getOptionDesc('max_vram')" />
              <small class="form-hint">For per-component CPU placement, use the <code>backend</code> field below — e.g. <code>diffusion=cuda0,vae=cpu</code>.</small>
            </div>
          </div>

          <!-- Backend Placement (replaces keep_clip_on_cpu / keep_vae_on_cpu /
               keep_controlnet_on_cpu / offload_to_cpu). Structured row editor —
               each row is a `component=device` assignment, serialized to
               the comma-separated string sd.cpp's backend parser expects. -->
          <div class="options-group">
            <h4 class="options-group-title">Backend Placement</h4>
            <div class="form-group" :title="getOptionDesc('backend')?.description">
              <label class="form-label">Compute Backend</label>
              <BackendAssignmentEditor
                v-model="loadParams.options!.backend"
                placeholder="e.g. diffusion=cuda0,vae=cpu"
              />
              <RecHint :desc="getOptionDesc('backend')" />
              <small class="form-hint">
                Compute backend override per component. Use <code>te=cpu</code>
                to keep text encoders on CPU, <code>vae=cpu</code> for the VAE,
                etc. Empty = sd.cpp picks the default backend.
              </small>
            </div>
            <div class="form-group" :title="getOptionDesc('params_backend')?.description">
              <label class="form-label">Params Backend</label>
              <label class="form-checkbox keep-in-ram-toggle">
                <input
                  type="checkbox"
                  :checked="loadParams.options!.params_backend === '*=cpu'"
                  @change="onKeepAllInRam"
                />
                <span>Keep all weights in RAM (<code>*=cpu</code>)</span>
              </label>
              <BackendAssignmentEditor
                v-model="loadParams.options!.params_backend"
                placeholder="e.g. *=cpu"
                :default-global="true"
              />
              <RecHint :desc="getOptionDesc('params_backend')" />
              <small class="form-hint">
                Parameter storage placement. The "keep all weights in RAM"
                toggle above is the common case (replaces the old
                <code>offload_to_cpu</code>); the row editor lets you mix
                per-component placement for advanced setups.
              </small>
            </div>
            <div class="form-group" :title="getOptionDesc('rpc_servers')?.description">
              <label class="form-label">RPC Servers</label>
              <input
                v-model="loadParams.options!.rpc_servers"
                type="text"
                class="form-input"
                placeholder="host1:50052,host2:50052"
              />
              <RecHint :desc="getOptionDesc('rpc_servers')" />
              <small class="form-hint">
                Comma-separated <code>host:port</code> pairs for the distributed
                (RPC) backend. Leave empty for local-only execution.
              </small>
            </div>
          </div>

          <!-- VAE / Conv Settings -->
          <div class="options-group">
            <h4 class="options-group-title">VAE / Conv Settings</h4>
            <div class="options-grid">
              <label class="form-checkbox" :title="getOptionDesc('vae_conv_direct')?.description">
                <input v-model="loadParams.options!.vae_conv_direct" type="checkbox" />
                <span>VAE Direct Convolution</span>
                <RecHint :desc="getOptionDesc('vae_conv_direct')" />
              </label>
              <label class="form-checkbox" :title="getOptionDesc('diffusion_conv_direct')?.description">
                <input v-model="loadParams.options!.diffusion_conv_direct" type="checkbox" />
                <span>Diffusion Direct Convolution</span>
                <RecHint :desc="getOptionDesc('diffusion_conv_direct')" />
              </label>
              <label class="form-checkbox" :title="getOptionDesc('force_sdxl_vae_conv_scale')?.description">
                <input v-model="loadParams.options!.force_sdxl_vae_conv_scale" type="checkbox" />
                <span>Force SDXL VAE Conv Scale</span>
                <RecHint :desc="getOptionDesc('force_sdxl_vae_conv_scale')" />
              </label>
              <label v-if="loadParams.taesd" class="form-checkbox" :title="getOptionDesc('tae_preview_only')?.description">
                <input v-model="loadParams.options!.tae_preview_only" type="checkbox" />
                <span>TAE Preview Only</span>
                <RecHint :desc="getOptionDesc('tae_preview_only')" />
              </label>
            </div>
          </div>

          <!-- Sampling / RNG / Prediction -->
          <div class="options-group">
            <h4 class="options-group-title">Sampling &amp; RNG</h4>
            <div class="form-group" :title="getOptionDesc('rng_type')?.description">
              <label class="form-label">RNG Type</label>
              <select v-model="loadParams.options!.rng_type" class="form-select">
                <option value="">Default</option>
                <option value="std_default">std_default</option>
                <option value="cuda">cuda</option>
              </select>
              <RecHint :desc="getOptionDesc('rng_type')" />
            </div>
            <div class="form-group" :title="getOptionDesc('sampler_rng_type')?.description">
              <label class="form-label">Sampler RNG Type</label>
              <select v-model="loadParams.options!.sampler_rng_type" class="form-select">
                <option value="">Default</option>
                <option value="std_default">std_default</option>
                <option value="cuda">cuda</option>
              </select>
              <RecHint :desc="getOptionDesc('sampler_rng_type')" />
            </div>
            <div class="form-group" :title="getOptionDesc('prediction')?.description">
              <label class="form-label">Prediction</label>
              <select v-model="loadParams.options!.prediction" class="form-select">
                <option value="">Auto</option>
                <option value="eps">eps</option>
                <option value="v">v</option>
                <option value="edm_v">edm_v</option>
                <option value="flow">flow</option>
              </select>
              <RecHint :desc="getOptionDesc('prediction')" />
            </div>
            <div class="form-group" :title="getOptionDesc('lora_apply_mode')?.description">
              <label class="form-label">LoRA Apply Mode</label>
              <select v-model="loadParams.options!.lora_apply_mode" class="form-select">
                <option value="">Auto</option>
                <option value="default">default</option>
                <option value="merge">merge</option>
                <option value="apply">apply</option>
              </select>
              <RecHint :desc="getOptionDesc('lora_apply_mode')" />
            </div>
            <div class="form-group" :title="getOptionDesc('tensor_type_rules')?.description">
              <label class="form-label">Tensor Type Rules</label>
              <input
                v-model="loadParams.options!.tensor_type_rules"
                type="text"
                class="form-input"
                placeholder="e.g. *.weight=f16"
              />
              <RecHint :desc="getOptionDesc('tensor_type_rules')" />
            </div>
          </div>

          <!-- Model-specific args (leejet PR #1757). Consolidates the old
               chroma_use_dit_mask / chroma_use_t5_mask / chroma_t5_mask_pad /
               qwen_image_zero_cond_t individual toggles into one key=value
               string handed to the model parser. -->
          <div class="options-group">
            <h4 class="options-group-title">Model Args</h4>
            <div class="form-group" :title="getOptionDesc('model_args')?.description">
              <label class="form-label">Model-specific args</label>
              <input
                v-model="loadParams.options!.model_args"
                type="text"
                class="form-input"
                placeholder="e.g. chroma_use_dit_mask=true,chroma_t5_mask_pad=1,qwen_image_zero_cond_t=false"
              />
              <small class="form-hint">
                Comma-separated <code>key=value</code> pairs handed to the
                sd.cpp model parser. Common keys:
                <code>chroma_use_dit_mask</code>,
                <code>chroma_use_t5_mask</code>,
                <code>chroma_t5_mask_pad</code>,
                <code>qwen_image_zero_cond_t</code>. Leave empty for defaults.
              </small>
              <RecHint :desc="getOptionDesc('model_args')" />
            </div>
          </div>

          <!-- Weight Type -->
          <div class="options-group">
            <h4 class="options-group-title">Weight Type</h4>
            <div class="form-group">
              <select v-model="loadParams.options!.weight_type" class="form-select">
                <option value="">Auto (use model's native precision)</option>
                <option value="f32">f32 (32-bit float)</option>
                <option value="f16">f16 (16-bit float)</option>
                <option value="bf16">bf16 (Brain float 16)</option>
                <option value="q8_0">q8_0 (8-bit quantized)</option>
                <option value="q5_0">q5_0 (5-bit quantized)</option>
                <option value="q4_0">q4_0 (4-bit quantized)</option>
              </select>
              <small class="form-hint">
                {{ getOptionDesc('weight_type')?.description || 'Runtime weight precision. Lower = less VRAM, may affect quality.' }}
              </small>
            </div>
          </div>

          <!-- VAE Format Override (leejet post-1ceb5bd: sd_vae_format_t enum).
               Lets the user force the VAE family when sd.cpp can't autodetect —
               required for PiD per its docs (the standalone PiD VAE files don't
               carry the metadata sd.cpp uses to distinguish Flux/SD3/Flux.2). -->
          <div class="options-group">
            <h4 class="options-group-title">VAE Format</h4>
            <div class="form-group">
              <select v-model="loadParams.options!.vae_format" class="form-select">
                <option value="auto">Auto — detect from VAE weights (default)</option>
                <option value="flux">Flux — Flux / Z-Image / Ideogram / Lens / Ernie latents</option>
                <option value="sd3">SD3 — SD3.x latents</option>
                <option value="flux2">Flux.2 - Flux.2 latents</option>
                <option value="wan">Wan (video) - Wan-family video VAE latents</option>
              </select>
              <small class="form-hint">
                {{ getOptionDesc('vae_format')?.description || "Leave on Auto. Required for PiD; otherwise sd.cpp detects from the VAE file's metadata." }}
              </small>
            </div>
          </div>

          <!-- Circular RoPE (tileable position embeddings) moved to
               per-generation controls in leejet PR #1748 — see the Generate
               page's "Circular RoPE / Tileable Output" section. Toggling no
               longer forces a model unload+reload cycle. -->
        </div>
      </details>

      <!-- Layer Streaming. Unified `stream_layers` + `max_vram` API is the
           only streaming UI now — the legacy multi-mode offload_mode +
           streaming_* tuning was dropped upstream. Works natively on leejet
           master and the fork's unified-streaming branch. -->
      <details class="card section-card accordion">
        <summary class="accordion-header">Layer Streaming</summary>
        <div class="accordion-content">
          <div class="form-group">
            <label class="form-checkbox">
              <input v-model="loadParams.options!.stream_layers" type="checkbox" />
              <span>Stream layers (residency + async prefetch)</span>
            </label>
            <small class="form-hint">
              Engages sd.cpp's residency-aware streaming planner. The planner decides which
              transformer segments stay resident vs streamed, and overlaps next-segment H2D copy
              with current-segment compute. Pairs naturally with
              <code>params_backend='*=cpu'</code> and <code>max_vram &gt; 0</code>.
              When you tick this and <code>params_backend</code> is empty, the form auto-fills
              <code>*=cpu</code> (mirrors sd-cli's <code>--offload-to-cpu</code> shortcut).
            </small>
          </div>
          <div v-if="loadParams.options!.stream_layers && !loadParams.options!.max_vram" class="form-hint" style="color: var(--color-warning, #c80);">
            ⚠ stream_layers is enabled but max_vram is 0 — set max_vram in the
            Memory Management group (typically ~80% of free VRAM) for streaming to engage.
          </div>
          <!-- Eager-load (leejet PR #1687). Lives in this group because the
               whole point is to pre-warm the params backend for streaming runs —
               the first generation skips lazy fault-in. Default on. -->
          <div class="form-group">
            <label class="form-checkbox">
              <input v-model="loadParams.options!.eager_load" type="checkbox" />
              <span>Eager-load params at model-load time</span>
            </label>
            <small class="form-hint">
              Pre-loads all weights into the params backend during
              <code>/models/load</code> instead of lazily on first generation
              (sd.cpp PR #1687). The restapi is a long-lived server, so the
              first request after load should be fast — that's why this is on
              by default. Untick if you'd rather move that load cost to the
              first generation.
            </small>
          </div>
        </div>
      </details>

      <!-- Footer Actions -->
      <div class="form-footer">
        <button class="btn btn-secondary" @click="router.push('/models')">Cancel</button>
        <button
          class="btn btn-primary"
          @click="handleLoadModel"
          :disabled="loading || !selectedModel"
          :title="loadButtonTitle"
        >
          <span v-if="loading" class="spinner"></span>
          {{ loadButtonLabel }}
        </button>
      </div>
    </div>
  </div>
</template>

<style scoped>
.model-load {
  max-width: 900px;
  margin: 0 auto;
  padding: 1rem;
}

.page-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 1.5rem;
  padding-bottom: 1rem;
  border-bottom: 1px solid var(--border-color);
}

.header-left {
  display: flex;
  align-items: center;
  gap: 1rem;
}

.btn-ghost {
  background: transparent;
  color: var(--text-secondary);
  border: none;
  padding: 0.5rem 1rem;
  cursor: pointer;
}

.btn-ghost:hover {
  color: var(--text-primary);
  background: var(--bg-secondary);
}

.load-form {
  display: flex;
  flex-direction: column;
  gap: 1.5rem;
}

.section-card {
  padding: 1.5rem;
}

.section-title {
  font-size: 1.1rem;
  font-weight: 600;
  margin: 0 0 1rem 0;
  display: flex;
  align-items: center;
  gap: 0.5rem;
}

.auto-badge {
  font-size: 0.7rem;
  padding: 0.2rem 0.5rem;
  background: var(--success-color);
  color: white;
  border-radius: 4px;
  font-weight: 500;
}

.detecting-badge {
  font-size: 0.7rem;
  padding: 0.2rem 0.5rem;
  background: var(--warning-color);
  color: white;
  border-radius: 4px;
  font-weight: 500;
}

.model-info-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(150px, 1fr));
  gap: 1rem;
}

.info-item {
  display: flex;
  flex-direction: column;
  gap: 0.25rem;
}

.info-item label {
  font-size: 0.75rem;
  color: var(--text-muted);
  text-transform: uppercase;
}

.info-value {
  font-weight: 500;
}

.weight-badge {
  display: inline-block;
  padding: 0.2rem 0.5rem;
  background: var(--primary-color);
  color: white;
  border-radius: 4px;
  font-size: 0.85rem;
}

.status-loaded {
  color: var(--success-color);
}

.architecture-description {
  margin: 0.5rem 0;
  color: var(--text-secondary);
  font-size: 0.9rem;
}

.generation-defaults {
  margin-top: 0.5rem;
  padding: 0.5rem;
  background: var(--bg-secondary);
  border-radius: 4px;
  font-size: 0.85rem;
}

.components-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));
  gap: 1rem;
}

.required-badge {
  font-size: 0.7rem;
  padding: 0.15rem 0.4rem;
  background: var(--error-color);
  color: white;
  border-radius: 3px;
}

.optional-badge {
  font-size: 0.7rem;
  padding: 0.15rem 0.4rem;
  background: var(--text-muted);
  color: white;
  border-radius: 3px;
}

.form-hint {
  display: block;
  margin-top: 0.25rem;
  font-size: 0.8rem;
  color: var(--text-muted);
}

.accordion {
  overflow: hidden;
}

.accordion-header {
  cursor: pointer;
  padding: 1rem 1.5rem;
  font-weight: 600;
  list-style: none;
  user-select: none;
}

.accordion-header::-webkit-details-marker {
  display: none;
}

.accordion-header::before {
  content: '▶';
  display: inline-block;
  margin-right: 0.5rem;
  transition: transform 0.2s;
}

details[open] .accordion-header::before {
  transform: rotate(90deg);
}

.accordion-content {
  padding: 0 1.5rem 1.5rem;
}

.options-group {
  margin-bottom: 1.5rem;
}

.options-group:last-child {
  margin-bottom: 0;
}

.options-group-title {
  font-size: 0.9rem;
  font-weight: 600;
  margin: 0 0 0.75rem 0;
  color: var(--text-secondary);
}

.options-grid {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.form-checkbox {
  display: flex;
  align-items: flex-start;
  gap: 0.5rem;
  cursor: pointer;
  padding: 0.25rem 0;
}

.form-checkbox input {
  margin-top: 0.2rem;
}

.option-hint {
  display: block;
  font-size: 0.75rem;
  color: var(--text-muted);
  margin-left: 1.5rem;
}

.inline-group {
  display: flex;
  align-items: center;
  gap: 0.5rem;
}

.inline-group .form-label {
  margin: 0;
  white-space: nowrap;
}

.form-input.small {
  width: 80px;
}

.offload-options {
  margin-top: 1rem;
  padding-top: 1rem;
  border-top: 1px solid var(--border-color);
}

.layer-streaming-options {
  margin-top: 1.5rem;
  padding-top: 1rem;
  border-top: 1px solid var(--border-color);
}

.layer-streaming-options .options-group-title {
  margin-bottom: 0.5rem;
}

.form-footer {
  display: flex;
  justify-content: flex-end;
  gap: 1rem;
  padding-top: 1rem;
  border-top: 1px solid var(--border-color);
}

.error-card {
  text-align: center;
  padding: 2rem;
}

.warning-banner {
  border-left: 4px solid var(--warning-color, #f0ad4e);
  background: rgba(240, 173, 78, 0.08);
  margin-bottom: 1rem;
}

.warning-banner strong {
  display: block;
  margin-bottom: 0.5rem;
  color: var(--warning-color, #f0ad4e);
}

.warning-banner p {
  margin: 0 0 0.75rem 0;
  color: var(--text-secondary, #888);
}

.warning-banner-info {
  border-left-color: var(--primary-color, #007bff);
  background: rgba(0, 123, 255, 0.06);
}

.warning-banner-info strong {
  color: var(--primary-color, #007bff);
}

.loading-state {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 1rem;
  padding: 3rem;
}

/* Responsive */
@media (max-width: 600px) {
  .page-header {
    flex-direction: column;
    gap: 1rem;
  }

  .header-right {
    width: 100%;
  }

  .header-right .btn {
    width: 100%;
  }

  .model-info-grid {
    grid-template-columns: repeat(2, 1fr);
  }

  .components-grid {
    grid-template-columns: 1fr;
  }
}
</style>
