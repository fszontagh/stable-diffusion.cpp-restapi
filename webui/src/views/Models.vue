<script setup lang="ts">
import { ref, computed, onMounted, watch } from 'vue'
import { useAppStore } from '../stores/app'
import { api, type LoadModelParams, type ModelInfo, type ConvertParams } from '../api/client'
import Modal from '../components/Modal.vue'
import SkeletonCard from '../components/SkeletonCard.vue'

const store = useAppStore()

// Conversion modal
const showConvertModal = ref(false)
const convertModel = ref<ModelInfo | null>(null)
const converting = ref(false)
const convertParams = ref<ConvertParams>({
  input_path: '',
  output_type: 'q8_0',
  output_path: ''
})

// Filters
const searchQuery = ref('')
const selectedType = ref<string>('')

// Load model form
const showLoadModal = ref(false)
const selectedModel = ref<ModelInfo | null>(null)
const loading = ref(false)
const selectedPreset = ref<string>('')

// Model presets with required components and optimal settings
interface ModelPreset {
  id: string
  name: string
  description: string
  requiredComponents: {
    vae?: boolean
    clip_l?: boolean
    clip_g?: boolean
    t5xxl?: boolean
    llm?: boolean
  }
  options: {
    n_threads?: number
    keep_clip_on_cpu?: boolean
    keep_vae_on_cpu?: boolean
    keep_controlnet_on_cpu?: boolean
    flash_attn?: boolean
    offload_to_cpu?: boolean
    enable_mmap?: boolean
    vae_decode_only?: boolean
    vae_conv_direct?: boolean
    diffusion_conv_direct?: boolean
    flow_shift?: number
    weight_type?: string
    chroma_use_dit_mask?: boolean
  }
  generationDefaults?: {
    cfg_scale?: number
    steps?: number
    sampler?: string
    scheduler?: string
    width?: number
    height?: number
  }
}

const modelPresets: ModelPreset[] = [
  {
    id: 'sd15',
    name: 'SD 1.x / SD 1.5',
    description: 'Standard SD 1.x models. VAE usually bundled.',
    requiredComponents: {},
    options: {
      keep_clip_on_cpu: false,
      flash_attn: false,
      vae_decode_only: true
    },
    generationDefaults: {
      cfg_scale: 7.0,
      steps: 20,
      sampler: 'euler_a',
      scheduler: 'discrete',
      width: 512,
      height: 512
    }
  },
  {
    id: 'sdxl',
    name: 'SDXL',
    description: 'SDXL models. Optional external VAE.',
    requiredComponents: { vae: false },
    options: {
      keep_clip_on_cpu: false,
      flash_attn: true,
      vae_decode_only: true
    },
    generationDefaults: {
      cfg_scale: 7.0,
      steps: 20,
      sampler: 'euler_a',
      scheduler: 'discrete',
      width: 1024,
      height: 1024
    }
  },
  {
    id: 'sd3',
    name: 'SD3 / SD3.5',
    description: 'Requires CLIP-L, CLIP-G, and T5-XXL text encoders.',
    requiredComponents: { clip_l: true, clip_g: true, t5xxl: true },
    options: {
      keep_clip_on_cpu: true,
      flash_attn: true,
      vae_decode_only: true
    },
    generationDefaults: {
      cfg_scale: 4.5,
      steps: 20,
      sampler: 'euler',
      scheduler: 'discrete',
      width: 1024,
      height: 1024
    }
  },
  {
    id: 'flux',
    name: 'Flux',
    description: 'Requires VAE (ae.safetensors), CLIP-L, and T5-XXL.',
    requiredComponents: { vae: true, clip_l: true, t5xxl: true },
    options: {
      keep_clip_on_cpu: true,
      flash_attn: true,
      vae_decode_only: true
    },
    generationDefaults: {
      cfg_scale: 1.0,
      steps: 20,
      sampler: 'euler',
      scheduler: 'discrete',
      width: 1024,
      height: 1024
    }
  },
  {
    id: 'flux_schnell',
    name: 'Flux Schnell',
    description: 'Fast Flux variant. Same requirements, fewer steps.',
    requiredComponents: { vae: true, clip_l: true, t5xxl: true },
    options: {
      keep_clip_on_cpu: true,
      flash_attn: true,
      vae_decode_only: true
    },
    generationDefaults: {
      cfg_scale: 1.0,
      steps: 4,
      sampler: 'euler',
      scheduler: 'discrete',
      width: 1024,
      height: 1024
    }
  },
  {
    id: 'zimage',
    name: 'Z-Image Turbo',
    description: 'Requires VAE (ae.gguf) and LLM (Qwen3 4B). Fast generation.',
    requiredComponents: { vae: true, llm: true },
    options: {
      flash_attn: true,
      vae_decode_only: true,
      vae_conv_direct: true,
      weight_type: 'q5_0'
    },
    generationDefaults: {
      cfg_scale: 1.0,
      steps: 8,
      sampler: 'euler',
      scheduler: 'smoothstep',
      width: 1024,
      height: 688
    }
  },
  {
    id: 'qwen_image',
    name: 'Qwen-Image',
    description: 'Requires VAE and LLM (Qwen2.5-VL 7B).',
    requiredComponents: { vae: true, llm: true },
    options: {
      flash_attn: true,
      offload_to_cpu: true,
      vae_decode_only: true,
      flow_shift: 3.0
    },
    generationDefaults: {
      cfg_scale: 2.5,
      steps: 20,
      sampler: 'euler',
      scheduler: 'discrete',
      width: 1024,
      height: 1024
    }
  },
  {
    id: 'chroma',
    name: 'Chroma',
    description: 'Requires VAE (ae.safetensors) and T5-XXL.',
    requiredComponents: { vae: true, t5xxl: true },
    options: {
      keep_clip_on_cpu: true,
      flash_attn: true,
      vae_decode_only: true,
      chroma_use_dit_mask: false
    },
    generationDefaults: {
      cfg_scale: 4.0,
      steps: 20,
      sampler: 'euler',
      scheduler: 'discrete',
      width: 1024,
      height: 1024
    }
  },
  {
    id: 'wan_t2v',
    name: 'Wan T2V (Video)',
    description: 'Text-to-Video. Requires VAE and T5-XXL (umt5).',
    requiredComponents: { vae: true, t5xxl: true },
    options: {
      flash_attn: true,
      offload_to_cpu: true,
      vae_decode_only: true,
      flow_shift: 3.0
    },
    generationDefaults: {
      cfg_scale: 6.0,
      steps: 30,
      sampler: 'euler',
      scheduler: 'discrete',
      width: 832,
      height: 480
    }
  }
]

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
    tae_preview_only: false
  }
})

// Watch for preset changes
watch(selectedPreset, (presetId) => {
  if (!presetId) return
  const preset = modelPresets.find(p => p.id === presetId)
  if (!preset) return

  // Apply preset options
  loadParams.value.options = {
    ...loadParams.value.options,
    ...preset.options
  }
})

const modelTypes = [
  { value: '', label: 'All Types' },
  { value: 'checkpoint', label: 'Checkpoints' },
  { value: 'diffusion', label: 'Diffusion Models' },
  { value: 'vae', label: 'VAE' },
  { value: 'lora', label: 'LoRA' },
  { value: 'clip', label: 'CLIP' },
  { value: 't5', label: 'T5' },
  { value: 'controlnet', label: 'ControlNet' },
  { value: 'llm', label: 'LLM' },
  { value: 'esrgan', label: 'ESRGAN' },
  { value: 'taesd', label: 'TAESD' }
]

const filteredModels = computed(() => {
  if (!store.models) return []

  const allModels = [
    ...store.models.checkpoints,
    ...store.models.diffusion_models,
    ...store.models.vae,
    ...store.models.loras,
    ...store.models.clip,
    ...store.models.t5,
    ...store.models.controlnets,
    ...store.models.llm,
    ...store.models.esrgan,
    ...(store.models.taesd || [])
  ]

  return allModels.filter(model => {
    const matchesType = !selectedType.value || model.type === selectedType.value
    const matchesSearch = !searchQuery.value ||
      model.name.toLowerCase().includes(searchQuery.value.toLowerCase())
    return matchesType && matchesSearch
  })
})

const vaeModels = computed(() => store.models?.vae || [])
const clipModels = computed(() => store.models?.clip || [])
const t5Models = computed(() => store.models?.t5 || [])
const controlnetModels = computed(() => store.models?.controlnets || [])
const llmModels = computed(() => store.models?.llm || [])
const taesdModels = computed(() => store.models?.taesd || [])

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

// Get current preset info
const currentPreset = computed(() => {
  if (!selectedPreset.value) return null
  return modelPresets.find(p => p.id === selectedPreset.value) || null
})

function openLoadModal(model: ModelInfo) {
  selectedModel.value = model
  loadParams.value.model_name = model.name
  loadParams.value.model_type = model.type === 'diffusion' ? 'diffusion' : 'checkpoint'
  selectedPreset.value = ''
  // Reset components
  loadParams.value.vae = ''
  loadParams.value.clip_l = ''
  loadParams.value.clip_g = ''
  loadParams.value.t5xxl = ''
  loadParams.value.controlnet = ''
  loadParams.value.llm = ''
  loadParams.value.taesd = ''
  // Reset options to defaults
  loadParams.value.options = {
    n_threads: -1,
    keep_clip_on_cpu: true,
    keep_vae_on_cpu: false,
    keep_controlnet_on_cpu: false,
    flash_attn: true,
    offload_to_cpu: false,
    vae_decode_only: true,
    tae_preview_only: false
  }
  showLoadModal.value = true
}

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
    showLoadModal.value = false
    store.fetchHealth()
  } catch (e) {
    store.showToast(e instanceof Error ? e.message : 'Failed to load model', 'error')
  } finally {
    loading.value = false
  }
}

// Get quantization types from store
const quantizationTypes = computed(() => store.quantizationTypes)

// Generate default output path for conversion
function generateOutputPath(inputPath: string, quantType: string): string {
  // Extract model name from path
  const parts = inputPath.split('/')
  const filename = parts[parts.length - 1]
  // Remove existing extension
  const dotIndex = filename.lastIndexOf('.')
  const stem = dotIndex > 0 ? filename.substring(0, dotIndex) : filename
  // Remove existing quant suffix if present (e.g., .f16, .q8_0)
  const lastDot = stem.lastIndexOf('.')
  let baseName = stem
  if (lastDot > 0) {
    const suffix = stem.substring(lastDot + 1).toLowerCase()
    if (suffix.match(/^(f32|f16|bf16|q\d+_\d+|q\d+_k)$/)) {
      baseName = stem.substring(0, lastDot)
    }
  }
  // Build output path in same directory
  const dir = parts.slice(0, -1).join('/')
  return (dir ? dir + '/' : '') + baseName + '.' + quantType + '.gguf'
}

function openConvertModal(model: ModelInfo) {
  convertModel.value = model
  // Build full path from model info
  // Note: The API returns model paths relative to model dirs, but we need full path
  // For now we use model name which may contain subdirectories
  convertParams.value.input_path = model.name
  convertParams.value.output_type = 'q8_0'
  convertParams.value.output_path = generateOutputPath(model.name, 'q8_0')
  showConvertModal.value = true
}

// Watch for quantization type changes to update output path
watch(() => convertParams.value.output_type, (newType) => {
  if (convertParams.value.input_path && newType) {
    convertParams.value.output_path = generateOutputPath(convertParams.value.input_path, newType)
  }
})

async function handleConvert() {
  if (!convertParams.value.input_path || !convertParams.value.output_type) {
    store.showToast('Please select input model and quantization type', 'error')
    return
  }

  converting.value = true
  try {
    const response = await api.convert(convertParams.value)
    store.showToast(`Conversion queued (Job ID: ${response.job_id})`, 'success')
    showConvertModal.value = false
    // Refresh models list after a delay to show new converted model
    setTimeout(() => store.fetchModels(), 2000)
  } catch (e) {
    store.showToast(e instanceof Error ? e.message : 'Failed to start conversion', 'error')
  } finally {
    converting.value = false
  }
}

onMounted(() => {
  store.fetchModels()
  store.fetchOptions()  // Fetch options to get quantization types
})
</script>

<template>
  <div class="models">
    <div class="page-header">
      <h1 class="page-title">Models</h1>
      <p class="page-subtitle">Browse and load available models</p>
    </div>

    <!-- Filters -->
    <div class="filters card">
      <div class="form-row">
        <div class="form-group" style="flex: 2">
          <input
            v-model="searchQuery"
            type="text"
            class="form-input"
            placeholder="Search models..."
          />
        </div>
        <div class="form-group" style="flex: 1">
          <select v-model="selectedType" class="form-select">
            <option v-for="type in modelTypes" :key="type.value" :value="type.value">
              {{ type.label }}
            </option>
          </select>
        </div>
        <button class="btn btn-secondary" @click="store.fetchModels()">
          &#8635; Refresh
        </button>
      </div>
    </div>

    <!-- Loading skeleton -->
    <div v-if="!store.models" class="models-grid">
      <SkeletonCard v-for="i in 6" :key="i" />
    </div>

    <!-- Models List -->
    <div v-else class="models-grid">
      <div
        v-for="model in filteredModels"
        :key="`${model.type}-${model.name}`"
        :class="['model-card', { loaded: model.is_loaded }]"
      >
        <div class="model-header">
          <span :class="['model-type-badge', `badge-${model.type}`]">{{ model.type }}</span>
          <span v-if="model.is_loaded" class="badge badge-completed">Loaded</span>
        </div>
        <div class="model-name" :title="model.name">{{ model.name }}</div>
        <div class="model-meta">
          <span class="model-size">{{ formatSize(model.size_bytes) }}</span>
          <span class="model-ext">{{ model.file_extension }}</span>
        </div>
        <div class="model-actions">
          <button
            v-if="model.type === 'checkpoint' || model.type === 'diffusion'"
            class="btn btn-primary btn-sm"
            @click="openLoadModal(model)"
            :disabled="model.is_loaded"
          >
            {{ model.is_loaded ? 'Loaded' : 'Load' }}
          </button>
          <button
            v-if="model.type === 'checkpoint' || model.type === 'diffusion' || model.type === 'vae' || model.type === 'lora' || model.type === 'clip' || model.type === 't5' || model.type === 'llm'"
            class="btn btn-secondary btn-sm"
            @click="openConvertModal(model)"
            title="Convert to GGUF with quantization"
          >
            Convert
          </button>
        </div>
      </div>
    </div>

    <div v-if="filteredModels.length === 0" class="empty-state">
      <div class="empty-state-icon">&#128269;</div>
      <div class="empty-state-title">No models found</div>
      <p>Try adjusting your search or filters</p>
    </div>

    <!-- Load Model Modal -->
    <Modal :show="showLoadModal" title="Load Model" @close="showLoadModal = false">
      <div class="form-group">
        <label class="form-label">Model</label>
        <input :value="loadParams.model_name" class="form-input" disabled />
      </div>

      <div class="form-group">
        <label class="form-label">Model Type</label>
        <select v-model="loadParams.model_type" class="form-select">
          <option value="checkpoint">Checkpoint</option>
          <option value="diffusion">Diffusion</option>
        </select>
      </div>

      <!-- Preset Selector -->
      <div class="form-group">
        <label class="form-label">Model Preset</label>
        <select v-model="selectedPreset" class="form-select">
          <option value="">Select preset (optional)...</option>
          <option v-for="preset in modelPresets" :key="preset.id" :value="preset.id">
            {{ preset.name }}
          </option>
        </select>
        <div v-if="currentPreset" class="preset-info">
          <div class="preset-description">{{ currentPreset.description }}</div>
          <div v-if="currentPreset.generationDefaults" class="preset-defaults">
            <strong>Recommended settings:</strong>
            CFG {{ currentPreset.generationDefaults.cfg_scale }},
            {{ currentPreset.generationDefaults.steps }} steps,
            {{ currentPreset.generationDefaults.sampler }},
            {{ currentPreset.generationDefaults.width }}x{{ currentPreset.generationDefaults.height }}
          </div>
        </div>
      </div>

      <div :class="['form-group', { required: currentPreset?.requiredComponents.vae }]">
        <label class="form-label">
          VAE
          <span v-if="currentPreset?.requiredComponents.vae" class="required-badge">Required</span>
          <span v-else class="optional-badge">Optional</span>
        </label>
        <select v-model="loadParams.vae" class="form-select">
          <option value="">None</option>
          <option v-for="m in vaeModels" :key="m.name" :value="m.name">{{ m.name }}</option>
        </select>
      </div>

      <div :class="['form-group', { required: currentPreset?.requiredComponents.clip_l }]">
        <label class="form-label">
          CLIP-L
          <span v-if="currentPreset?.requiredComponents.clip_l" class="required-badge">Required</span>
          <span v-else class="optional-badge">Optional</span>
        </label>
        <select v-model="loadParams.clip_l" class="form-select">
          <option value="">None</option>
          <option v-for="m in clipModels" :key="m.name" :value="m.name">{{ m.name }}</option>
        </select>
      </div>

      <div :class="['form-group', { required: currentPreset?.requiredComponents.clip_g }]">
        <label class="form-label">
          CLIP-G
          <span v-if="currentPreset?.requiredComponents.clip_g" class="required-badge">Required</span>
          <span v-else class="optional-badge">Optional</span>
        </label>
        <select v-model="loadParams.clip_g" class="form-select">
          <option value="">None</option>
          <option v-for="m in clipModels" :key="m.name" :value="m.name">{{ m.name }}</option>
        </select>
      </div>

      <div :class="['form-group', { required: currentPreset?.requiredComponents.t5xxl }]">
        <label class="form-label">
          T5-XXL
          <span v-if="currentPreset?.requiredComponents.t5xxl" class="required-badge">Required</span>
          <span v-else class="optional-badge">Optional</span>
        </label>
        <select v-model="loadParams.t5xxl" class="form-select">
          <option value="">None</option>
          <option v-for="m in t5Models" :key="m.name" :value="m.name">{{ m.name }}</option>
        </select>
      </div>

      <div class="form-group">
        <label class="form-label">
          ControlNet
          <span class="optional-badge">Optional</span>
        </label>
        <select v-model="loadParams.controlnet" class="form-select">
          <option value="">None</option>
          <option v-for="m in controlnetModels" :key="m.name" :value="m.name">{{ m.name }}</option>
        </select>
      </div>

      <div :class="['form-group', { required: currentPreset?.requiredComponents.llm }]">
        <label class="form-label">
          LLM
          <span v-if="currentPreset?.requiredComponents.llm" class="required-badge">Required</span>
          <span v-else class="optional-badge">Optional</span>
        </label>
        <select v-model="loadParams.llm" class="form-select">
          <option value="">None</option>
          <option v-for="m in llmModels" :key="m.name" :value="m.name">{{ m.name }}</option>
        </select>
      </div>

      <div class="form-group">
        <label class="form-label">
          TAESD (Preview)
          <span class="optional-badge">Optional</span>
        </label>
        <select v-model="loadParams.taesd" class="form-select">
          <option value="">None</option>
          <option v-for="m in taesdModels" :key="m.name" :value="m.name">{{ m.name }}</option>
        </select>
        <small class="text-muted">
          Tiny AutoEncoder for fast preview generation.
          <span v-if="loadParams.options?.tae_preview_only" class="tae-preview-hint">
            TAE will be used ONLY for previews, VAE for final output.
          </span>
          <span v-else-if="loadParams.taesd">
            TAE will be used for both previews AND final decode (faster but lower quality).
          </span>
        </small>
      </div>

      <details class="accordion mt-4">
        <summary class="accordion-header">Advanced Options</summary>
        <div class="accordion-content">
          <div class="form-group">
            <label class="form-label">Threads (-1 = auto)</label>
            <input v-model.number="loadParams.options!.n_threads" type="number" class="form-input" />
          </div>
          <label class="form-checkbox">
            <input v-model="loadParams.options!.keep_clip_on_cpu" type="checkbox" />
            Keep CLIP on CPU
          </label>
          <label class="form-checkbox">
            <input v-model="loadParams.options!.keep_vae_on_cpu" type="checkbox" />
            Keep VAE on CPU
          </label>
          <label class="form-checkbox">
            <input v-model="loadParams.options!.keep_controlnet_on_cpu" type="checkbox" />
            Keep ControlNet on CPU
          </label>
          <label class="form-checkbox">
            <input v-model="loadParams.options!.flash_attn" type="checkbox" />
            Flash Attention
          </label>
          <label class="form-checkbox">
            <input v-model="loadParams.options!.offload_to_cpu" type="checkbox" />
            Offload to CPU
          </label>
          <label class="form-checkbox">
            <input v-model="loadParams.options!.enable_mmap" type="checkbox" />
            Memory-mapped Loading
          </label>
          <label class="form-checkbox">
            <input v-model="loadParams.options!.vae_decode_only" type="checkbox" />
            VAE Decode Only
          </label>
          <label class="form-checkbox" v-if="loadParams.taesd">
            <input v-model="loadParams.options!.tae_preview_only" type="checkbox" />
            TAE Preview Only
            <span class="checkbox-hint">(use TAESD for previews, VAE for final output)</span>
          </label>
        </div>
      </details>

      <template #footer>
        <button class="btn btn-secondary" @click="showLoadModal = false">Cancel</button>
        <button class="btn btn-primary" @click="handleLoadModel" :disabled="loading">
          <span v-if="loading" class="spinner"></span>
          {{ loading ? 'Loading...' : 'Load Model' }}
        </button>
      </template>
    </Modal>

    <!-- Convert Model Modal -->
    <Modal :show="showConvertModal" title="Convert Model" @close="showConvertModal = false">
      <div class="convert-info">
        <p>Convert model to GGUF format with quantization. Smaller quantization saves memory but may affect quality.</p>
      </div>

      <div class="form-group">
        <label class="form-label">Source Model</label>
        <input :value="convertModel?.name" class="form-input" disabled />
      </div>

      <div class="form-group">
        <label class="form-label">Current Size</label>
        <input :value="convertModel ? formatSize(convertModel.size_bytes) : ''" class="form-input" disabled />
      </div>

      <div class="form-group">
        <label class="form-label">Quantization Type</label>
        <select v-model="convertParams.output_type" class="form-select">
          <option v-for="qtype in quantizationTypes" :key="qtype.id" :value="qtype.id">
            {{ qtype.name }}
          </option>
        </select>
        <div class="form-hint">
          Lower bits = smaller file &amp; faster inference, but potentially lower quality
        </div>
      </div>

      <div class="form-group">
        <label class="form-label">Output Filename</label>
        <input v-model="convertParams.output_path" class="form-input" placeholder="Auto-generated based on model name" />
        <div class="form-hint">
          Output will be saved in the same directory as the source model
        </div>
      </div>

      <details class="accordion mt-4">
        <summary class="accordion-header">Advanced Options</summary>
        <div class="accordion-content">
          <div class="form-group">
            <label class="form-label">VAE Path (optional)</label>
            <select v-model="convertParams.vae_path" class="form-select">
              <option value="">None (use bundled VAE if exists)</option>
              <option v-for="m in vaeModels" :key="m.name" :value="m.name">{{ m.name }}</option>
            </select>
            <div class="form-hint">
              Specify external VAE to bake into the converted model
            </div>
          </div>

          <div class="form-group">
            <label class="form-label">Tensor Type Rules (optional)</label>
            <input v-model="convertParams.tensor_type_rules" class="form-input" placeholder="e.g., blk.0.*=q8_0" />
            <div class="form-hint">
              Advanced: per-tensor quantization rules (see sd.cpp docs)
            </div>
          </div>
        </div>
      </details>

      <template #footer>
        <button class="btn btn-secondary" @click="showConvertModal = false">Cancel</button>
        <button class="btn btn-primary" @click="handleConvert" :disabled="converting">
          <span v-if="converting" class="spinner"></span>
          {{ converting ? 'Converting...' : 'Start Conversion' }}
        </button>
      </template>
    </Modal>
  </div>
</template>

<style scoped>
.filters {
  margin-bottom: 24px;
}

.filters .form-row {
  display: flex;
  gap: 16px;
  align-items: flex-end;
}

.models-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));
  gap: 16px;
}

.model-card {
  background: var(--bg-secondary);
  border: 1px solid var(--border-color);
  border-radius: var(--border-radius);
  padding: 16px;
  transition: all var(--transition-fast);
}

.model-card:hover {
  border-color: var(--accent-primary);
}

.model-card.loaded {
  border-color: var(--accent-success);
}

.model-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 12px;
}

.model-type-badge {
  font-size: 10px;
  padding: 3px 8px;
  border-radius: 4px;
  text-transform: uppercase;
  font-weight: 600;
}

.badge-checkpoint { background: var(--accent-primary); color: var(--bg-primary); }
.badge-diffusion { background: var(--accent-purple); color: white; }
.badge-vae { background: var(--accent-success); color: var(--bg-primary); }
.badge-lora { background: var(--accent-warning); color: var(--bg-primary); }
.badge-clip { background: #ff7b9c; color: white; }
.badge-t5 { background: #9c88ff; color: white; }
.badge-controlnet { background: #00b894; color: white; }
.badge-llm { background: #e17055; color: white; }
.badge-esrgan { background: #74b9ff; color: var(--bg-primary); }
.badge-taesd { background: #a29bfe; color: white; }

.model-name {
  font-weight: 500;
  margin-bottom: 8px;
  word-break: break-all;
  font-size: 13px;
  line-height: 1.4;
}

.model-meta {
  display: flex;
  gap: 12px;
  font-size: 12px;
  color: var(--text-secondary);
  margin-bottom: 12px;
}

.model-actions {
  display: flex;
  justify-content: flex-end;
}

.accordion {
  border: 1px solid var(--border-color);
  border-radius: var(--border-radius);
}

.accordion-header {
  padding: 12px 16px;
  cursor: pointer;
  background: var(--bg-tertiary);
}

.accordion-content {
  padding: 16px;
  display: flex;
  flex-direction: column;
  gap: 12px;
}

/* Preset styles */
.preset-info {
  margin-top: 8px;
  padding: 12px;
  background: var(--bg-tertiary);
  border-radius: var(--border-radius-sm);
  font-size: 12px;
}

.preset-description {
  color: var(--text-secondary);
  margin-bottom: 8px;
}

.preset-defaults {
  color: var(--accent-primary);
  font-size: 11px;
}

.required-badge {
  background: var(--accent-error);
  color: white;
  padding: 2px 6px;
  border-radius: 4px;
  font-size: 10px;
  font-weight: 600;
  margin-left: 8px;
}

.optional-badge {
  background: var(--bg-tertiary);
  color: var(--text-muted);
  padding: 2px 6px;
  border-radius: 4px;
  font-size: 10px;
  margin-left: 8px;
}

.form-group.required .form-select {
  border-color: var(--accent-warning);
}

.form-group.required .form-label {
  color: var(--accent-warning);
}

/* Convert modal styles */
.convert-info {
  background: var(--bg-tertiary);
  border-radius: var(--border-radius-sm);
  padding: 12px 16px;
  margin-bottom: 16px;
  font-size: 13px;
  color: var(--text-secondary);
}

.convert-info p {
  margin: 0;
}

.form-hint {
  font-size: 11px;
  color: var(--text-muted);
  margin-top: 4px;
}

.model-actions {
  display: flex;
  justify-content: flex-end;
  gap: 8px;
}

.tae-preview-hint {
  color: var(--accent-success);
  font-weight: 500;
}

.checkbox-hint {
  font-size: 11px;
  color: var(--text-muted);
  margin-left: 4px;
  font-weight: normal;
}

.text-muted {
  display: block;
  margin-top: 4px;
  font-size: 12px;
  color: var(--text-muted);
}
</style>
