<script setup lang="ts">
import { ref, computed, onMounted, watch } from 'vue'
import { useRouter } from 'vue-router'
import { useAppStore } from '../stores/app'
import { api, type ModelInfo, type ConvertParams } from '../api/client'
import Modal from '../components/Modal.vue'
import SkeletonCard from '../components/SkeletonCard.vue'

const store = useAppStore()
const router = useRouter()

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

// vaeModels computed only needed for convert modal
const vaeModels = computed(() => store.models?.vae || [])

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

// Navigate to model load page
function openLoadPage(model: ModelInfo) {
  router.push(`/models/load/${encodeURIComponent(model.name)}`)
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
        <button class="btn btn-secondary" @click="store.refreshModels()" :disabled="store.loading">
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
            :class="['btn', 'btn-sm', model.is_loaded ? 'btn-secondary' : 'btn-primary']"
            @click="openLoadPage(model)"
          >
            {{ model.is_loaded ? 'Edit' : 'Load' }}
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
</style>
