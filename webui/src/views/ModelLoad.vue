<script setup lang="ts">
import { ref, computed, onMounted, watch } from 'vue'
import { useRouter } from 'vue-router'
import { useAppStore } from '../stores/app'
import { api, type LoadModelParams, type ModelInfo, type ArchitecturePreset, type OptionDescription } from '../api/client'
import { useArchitectures, extractWeightType, suggestComponents, type ComponentSuggestion } from '../composables/useArchitectures'
import RecHint from '../components/RecHint.vue'

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
  llm: '',
  taesd: '',
  options: {
    n_threads: -1,
    keep_clip_on_cpu: true,
    keep_vae_on_cpu: false,
    keep_controlnet_on_cpu: false,
    flash_attn: true,
    diffusion_flash_attn: false,
    offload_to_cpu: false,
    enable_mmap: true,
    vae_decode_only: false,
    tae_preview_only: false,
    offload_mode: 'none',
    vram_estimation: 'dryrun',
    offload_cond_stage: true,
    offload_diffusion: false,
    reload_cond_stage: true,
    reload_diffusion: true,
    target_free_vram_mb: 0,
    // Layer streaming tuning (only meaningful when offload_mode='layer_streaming').
    // The mode itself is what enables streaming inside sd.cpp.
    streaming_prefetch_layers: 1,
    streaming_keep_layers_behind: 0,
    streaming_min_free_vram_mb: 0,
    // Unified-streaming variant: replaces the multi-mode offload_mode + streaming_*
    // tuning with a single toggle. Honored on leejet master (>= post-1ceb5bd
    // merge of the unified-streaming API) AND on fork builds compiled with
    // -DSD_UNIFIED_STREAMING=ON. Legacy fork builds parse but ignore it.
    stream_layers: false,
    // leejet master post-1ceb5bd: VAE format override + tileable RoPE.
    vae_format: 'auto',
    circular_x: false,
    circular_y: false
  }
})

// Computed: Available models by type
const vaeModels = computed(() => store.models?.vae || [])
const clipModels = computed(() => store.models?.clip || [])
const t5Models = computed(() => store.models?.t5 || [])
const controlnetModels = computed(() => store.models?.controlnets || [])
const llmModels = computed(() => store.models?.llm || [])
const taesdModels = computed(() => store.models?.taesd || [])

// Computed: Current architecture preset
const currentArchitecture = computed((): ArchitecturePreset | null => {
  if (!selectedArchitecture.value || !store.architectures?.architectures) return null
  return store.architectures.architectures[selectedArchitecture.value] || null
})

// Computed: Model weight type
const modelWeightType = computed(() => extractWeightType(props.modelName))

// Computed: Component suggestions
const vaeSuggestions = computed(() =>
  suggestComponents('vae', selectedArchitecture.value, modelWeightType.value, vaeModels.value)
)
const clipSuggestions = computed(() =>
  suggestComponents('clip', selectedArchitecture.value, modelWeightType.value, clipModels.value)
)
const t5Suggestions = computed(() =>
  suggestComponents('t5', selectedArchitecture.value, modelWeightType.value, t5Models.value)
)
const llmSuggestions = computed(() =>
  suggestComponents('llm', selectedArchitecture.value, modelWeightType.value, llmModels.value)
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

  // Fetch architectures if not loaded
  if (!store.architectures) {
    await store.fetchArchitectures()
  }

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
      loadParams.value.llm = store.loadedComponents.llm || ''

      // Pre-populate load options from currently loaded model.
      // Spread store.loadOptions wholesale so every field the backend exposes
      // is restored — without this, fields not explicitly listed (e.g.
      // vae_conv_direct, diffusion_conv_direct, vae_tiling, lora_apply_mode,
      // rng_type, chroma_*, prediction, free_params_immediately, log_offload_events,
      // min_offload_size_mb, ...) get silently dropped on edit. The pre-existing
      // loadParams.options layer underneath provides defaults for any field the
      // backend didn't return (undefined keys are not enumerated by spread).
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
      // values like offload_mode='layer_streaming' the user already chose.
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

// (Removed: the previous stream_layers→offload_to_cpu watcher.
// leejet PR #1654 deleted offload_params_to_cpu from sd_ctx_params_t,
// routing CPU placement through backend specs instead. The stream_layers
// flag still works, but the per-component "keep on CPU" coupling is now
// expressed via the `backend` string — e.g. "diffusion=cuda0,vae=cpu" —
// not a separate boolean. Nothing to auto-tick at the UI layer anymore.)

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
               vae_decode_only, free_params_immediately). ModelLoadParams still
               accepts those fields for back-compat but stops wiring them to
               ctx_params, so the WebUI no longer surfaces them — explicit
               placement now goes through `backend` / `params_backend` strings
               like "diffusion=cuda0,vae=cpu". Max VRAM survives unchanged. -->
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
              <small class="form-hint">For per-component CPU placement (formerly keep_*/offload_to_cpu checkboxes), use the <code>backend</code> field in Advanced — e.g. <code>diffusion=cuda0,vae=cpu</code>.</small>
            </div>
          </div>

          <!-- VAE Settings -->
          <div class="options-group">
            <h4 class="options-group-title">VAE Settings</h4>
            <div class="options-grid">
              <label class="form-checkbox" :title="getOptionDesc('vae_tiling')?.description">
                <input v-model="loadParams.options!.vae_tiling" type="checkbox" />
                <span>VAE Tiling</span>
                <RecHint :desc="getOptionDesc('vae_tiling')" />
              </label>
              <label class="form-checkbox" :title="getOptionDesc('vae_conv_direct')?.description">
                <input v-model="loadParams.options!.vae_conv_direct" type="checkbox" />
                <span>VAE Direct Convolution</span>
                <RecHint :desc="getOptionDesc('vae_conv_direct')" />
              </label>
              <label v-if="loadParams.taesd" class="form-checkbox" :title="getOptionDesc('tae_preview_only')?.description">
                <input v-model="loadParams.options!.tae_preview_only" type="checkbox" />
                <span>TAE Preview Only</span>
                <RecHint :desc="getOptionDesc('tae_preview_only')" />
              </label>
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
                <option value="flux2">Flux.2 — Flux.2 latents</option>
              </select>
              <small class="form-hint">
                {{ getOptionDesc('vae_format')?.description || "Leave on Auto. Required for PiD; otherwise sd.cpp detects from the VAE file's metadata." }}
              </small>
            </div>
          </div>

          <!-- Tileable position embeddings (leejet PR #1627 — circular RoPE).
               Required for Ideogram4 tileable-texture workflows. Two
               independent axes so users can pick horizontal-only,
               vertical-only, or fully-tileable output. -->
          <div class="options-group">
            <h4 class="options-group-title">Tileable Output (Circular RoPE)</h4>
            <div class="options-grid">
              <label class="form-checkbox">
                <input v-model="loadParams.options!.circular_x" type="checkbox" />
                <span>Circular X (horizontal seam)</span>
                <RecHint :desc="getOptionDesc('circular_x')" />
              </label>
              <label class="form-checkbox">
                <input v-model="loadParams.options!.circular_y" type="checkbox" />
                <span>Circular Y (vertical seam)</span>
                <RecHint :desc="getOptionDesc('circular_y')" />
              </label>
            </div>
            <small class="form-hint">
              Off for normal generation. Enable one or both for seamless / tileable output
              (skyboxes, repeating textures, Ideogram4 panoramas). Tile both for fully-wrappable textures.
            </small>
          </div>
        </div>
      </details>

      <!-- VRAM Offloading. The unified stream_layers + max_vram API was merged
           into leejet master after 1ceb5bd, so the accordion always renders.
           Two mutually-exclusive inner UIs:
             - Legacy multi-mode dropdown:  shows only on the legacy fork
               compile (SDCPP_EXPERIMENTAL_OFFLOAD && !SDCPP_UNIFIED_STREAMING).
             - Default stream_layers UI:    everything else (leejet master,
               unified-streaming fork). -->
      <details class="card section-card accordion">
        <summary class="accordion-header">VRAM Offloading</summary>
        <div class="accordion-content">

          <!-- ── stream_layers UI (default — leejet master + unified fork) ──
               Single toggle on top of max_vram. The planner handles
               residency split + async H2D prefetch automatically. -->
          <template v-if="!store.experimentalOffloadEnabled || store.unifiedStreamingEnabled">
            <div class="form-group">
              <label class="form-checkbox">
                <input v-model="loadParams.options!.stream_layers" type="checkbox" />
                <span>Stream layers (residency + async prefetch)</span>
              </label>
              <small class="form-hint">
                Engages sd.cpp's residency-aware streaming planner on top of the <code>max_vram</code>
                budget. The planner decides which transformer segments stay resident vs streamed,
                and overlaps next-segment H2D copy with current-segment compute. Requires
                <strong>max_vram &gt; 0</strong> to do anything; otherwise this flag is silently a no-op.
                Auto-enables <strong>Offload to CPU</strong> (Performance section) since
                streaming pulls segments from host-resident weights — sd.cpp would do the same
                implicit coupling at load time, this just keeps the form state honest.
              </small>
            </div>
            <div v-if="loadParams.options!.stream_layers && !loadParams.options!.max_vram" class="form-hint" style="color: var(--color-warning, #c80);">
              ⚠ stream_layers is enabled but max_vram is 0 — set max_vram in Performance section above
              (typically ~80% of free VRAM) for streaming to engage.
            </div>
          </template>

          <!-- ── feature/vram-offloading-v2 UI (legacy multi-mode) ──────── -->
          <template v-else>
            <div class="form-group">
              <label class="form-label">Offload Mode</label>
              <select v-model="loadParams.options!.offload_mode" class="form-select">
                <option value="none">Disabled (keep all on GPU)</option>
                <option value="cond_only">After Conditioning (offload LLM/CLIP)</option>
                <option value="cond_diffusion">After Cond + Diffusion</option>
                <option value="aggressive">Aggressive (offload each component)</option>
                <option value="layer_streaming">Layer Streaming (for models larger than VRAM)</option>
              </select>
              <small class="form-hint">
                {{ getOptionDesc('offload_mode')?.description || 'Temporarily move model components to CPU during generation to free VRAM for VAE decode.' }}
              </small>
            </div>

            <div v-if="loadParams.options!.offload_mode !== 'none'" class="offload-options">
              <div class="form-group">
                <label class="form-label">VRAM Estimation Method</label>
                <select v-model="loadParams.options!.vram_estimation" class="form-select">
                  <option value="dryrun">Dry-run (Accurate, default)</option>
                  <option value="formula">Formula (Faster, approximate)</option>
                </select>
                <small class="form-hint">
                  {{ getOptionDesc('vram_estimation')?.description || 'How to estimate VRAM needed before VAE decode.' }}
                </small>
              </div>

              <div class="options-grid">
                <label class="form-checkbox">
                  <input v-model="loadParams.options!.offload_cond_stage" type="checkbox" />
                  <span>Offload LLM/CLIP after conditioning</span>
                </label>
                <label
                  v-if="loadParams.options!.offload_mode === 'aggressive' || loadParams.options!.offload_mode === 'cond_diffusion'"
                  class="form-checkbox"
                >
                  <input v-model="loadParams.options!.offload_diffusion" type="checkbox" />
                  <span>Offload diffusion model after sampling</span>
                </label>
                <label class="form-checkbox">
                  <input v-model="loadParams.options!.reload_cond_stage" type="checkbox" />
                  <span>Reload LLM/CLIP after generation</span>
                </label>
                <label class="form-checkbox">
                  <input v-model="loadParams.options!.reload_diffusion" type="checkbox" />
                  <span>Reload diffusion model after generation</span>
                </label>
              </div>

              <!-- Advanced VRAM settings -->
              <div class="form-group mt-3">
                <label class="form-label">Target Free VRAM (MB)</label>
                <input
                  v-model.number="loadParams.options!.target_free_vram_mb"
                  type="number"
                  class="form-input"
                  min="0"
                  step="256"
                  placeholder="0 = always offload"
                />
                <small class="form-hint">Target free VRAM before VAE decode. 0 = always offload when mode is set.</small>
              </div>
            </div>

            <!-- Layer Streaming Options (for layer_streaming mode) -->
            <div v-if="loadParams.options!.offload_mode === 'layer_streaming'" class="layer-streaming-options">
              <h4 class="options-group-title">Layer Streaming Configuration</h4>
              <p class="form-hint mb-3">
                Layer streaming enables running models larger than your VRAM by streaming layers one-by-one.
              </p>

              <div class="form-group">
                <label class="form-label">Prefetch Layers</label>
                <input
                  v-model.number="loadParams.options!.streaming_prefetch_layers"
                  type="number"
                  class="form-input"
                  min="0"
                  max="4"
                />
                <small class="form-hint">Number of layers to prefetch ahead (default: 1). Higher = more VRAM, potentially faster.</small>
              </div>

              <div class="form-group">
                <label class="form-label">Keep Layers Behind</label>
                <input
                  v-model.number="loadParams.options!.streaming_keep_layers_behind"
                  type="number"
                  class="form-input"
                  min="0"
                  max="4"
                />
                <small class="form-hint">Layers to keep after execution for skip connections. Increase if you see artifacts.</small>
              </div>

              <div class="form-group">
                <label class="form-label">Min Free VRAM (MB)</label>
                <input
                  v-model.number="loadParams.options!.streaming_min_free_vram_mb"
                  type="number"
                  class="form-input"
                  min="0"
                  step="256"
                />
                <small class="form-hint">Minimum VRAM to keep free during streaming. 0 = no limit.</small>
              </div>
            </div>
          </template>

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
