<script setup lang="ts">
import { ref, computed, onMounted, watch } from 'vue'
import { useRouter } from 'vue-router'
import { useAppStore } from '../stores/app'
import { api, type LoadModelParams, type ModelInfo, type ArchitecturePreset, type OptionDescription } from '../api/client'
import { useArchitectures, extractWeightType, suggestComponents, type ComponentSuggestion } from '../composables/useArchitectures'

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
    offload_to_cpu: false,
    enable_mmap: true,
    vae_decode_only: true,
    tae_preview_only: false,
    offload_mode: 'none',
    vram_estimation: 'dryrun',
    offload_cond_stage: true,
    offload_diffusion: false,
    reload_cond_stage: true,
    reload_diffusion: true
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

// Load the model
async function handleLoadModel() {
  loading.value = true
  try {
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

    await api.loadModel(params)
    store.showToast('Model loaded successfully', 'success')
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

    // If model is already loaded, pre-populate with current components
    if (selectedModel.value.is_loaded && store.loadedComponents) {
      loadParams.value.vae = store.loadedComponents.vae || ''
      loadParams.value.clip_l = store.loadedComponents.clip_l || ''
      loadParams.value.clip_g = store.loadedComponents.clip_g || ''
      loadParams.value.t5xxl = store.loadedComponents.t5xxl || ''
      loadParams.value.controlnet = store.loadedComponents.controlnet || ''
      loadParams.value.llm = store.loadedComponents.llm || ''

      // Use current architecture if loaded
      if (store.modelArchitecture) {
        selectedArchitecture.value = store.modelArchitecture
      }
    }

    // Auto-detect architecture from model name
    const detected = await detectArchitecture(decodedName)
    if (detected) {
      selectedArchitecture.value = detected.id
      isAutoDetected.value = true
      applyArchitectureOptions(detected)
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
        >
          <span v-if="loading" class="spinner"></span>
          {{ loading ? 'Loading...' : (selectedModel?.is_loaded ? 'Apply Changes' : 'Load Model') }}
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
                <span class="option-hint" v-if="getOptionDesc('flash_attn')">
                  {{ getOptionDesc('flash_attn')?.recommended }}
                </span>
              </label>
              <label class="form-checkbox" :title="getOptionDesc('enable_mmap')?.description">
                <input v-model="loadParams.options!.enable_mmap" type="checkbox" />
                <span>Memory-mapped Loading</span>
              </label>
              <div class="form-group inline-group">
                <label class="form-label">Threads (-1 = auto)</label>
                <input v-model.number="loadParams.options!.n_threads" type="number" class="form-input small" />
              </div>
            </div>
          </div>

          <!-- Memory Management -->
          <div class="options-group">
            <h4 class="options-group-title">Memory Management</h4>
            <div class="options-grid">
              <label class="form-checkbox" :title="getOptionDesc('keep_clip_on_cpu')?.description">
                <input v-model="loadParams.options!.keep_clip_on_cpu" type="checkbox" />
                <span>Keep CLIP on CPU</span>
              </label>
              <label class="form-checkbox" :title="getOptionDesc('keep_vae_on_cpu')?.description">
                <input v-model="loadParams.options!.keep_vae_on_cpu" type="checkbox" />
                <span>Keep VAE on CPU</span>
              </label>
              <label class="form-checkbox" :title="getOptionDesc('keep_controlnet_on_cpu')?.description">
                <input v-model="loadParams.options!.keep_controlnet_on_cpu" type="checkbox" />
                <span>Keep ControlNet on CPU</span>
              </label>
              <label class="form-checkbox" :title="getOptionDesc('offload_to_cpu')?.description">
                <input v-model="loadParams.options!.offload_to_cpu" type="checkbox" />
                <span>Offload to CPU</span>
              </label>
            </div>
          </div>

          <!-- VAE Settings -->
          <div class="options-group">
            <h4 class="options-group-title">VAE Settings</h4>
            <div class="options-grid">
              <label class="form-checkbox" :title="getOptionDesc('vae_decode_only')?.description">
                <input v-model="loadParams.options!.vae_decode_only" type="checkbox" />
                <span>VAE Decode Only</span>
                <span class="option-hint">Skip encoder weights (enable unless using img2img)</span>
              </label>
              <label class="form-checkbox" :title="getOptionDesc('vae_tiling')?.description">
                <input v-model="loadParams.options!.vae_tiling" type="checkbox" />
                <span>VAE Tiling</span>
              </label>
              <label class="form-checkbox" :title="getOptionDesc('vae_conv_direct')?.description">
                <input v-model="loadParams.options!.vae_conv_direct" type="checkbox" />
                <span>VAE Direct Convolution</span>
              </label>
              <label v-if="loadParams.taesd" class="form-checkbox" :title="getOptionDesc('tae_preview_only')?.description">
                <input v-model="loadParams.options!.tae_preview_only" type="checkbox" />
                <span>TAE Preview Only</span>
                <span class="option-hint">Use TAESD for previews, VAE for final output</span>
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
        </div>
      </details>

      <!-- Offload Settings -->
      <details class="card section-card accordion">
        <summary class="accordion-header">VRAM Offloading</summary>
        <div class="accordion-content">
          <div class="form-group">
            <label class="form-label">Offload Mode</label>
            <select v-model="loadParams.options!.offload_mode" class="form-select">
              <option value="none">Disabled (keep all on GPU)</option>
              <option value="cond_only">After Conditioning (offload LLM/CLIP)</option>
              <option value="cond_diffusion">After Cond + Diffusion</option>
              <option value="aggressive">Aggressive (offload each component)</option>
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
        >
          <span v-if="loading" class="spinner"></span>
          {{ loading ? 'Loading...' : (selectedModel?.is_loaded ? 'Apply Changes' : 'Load Model') }}
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
