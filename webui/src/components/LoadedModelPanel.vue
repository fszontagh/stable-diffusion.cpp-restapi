<script setup lang="ts">
import { ref, computed, watch, onMounted } from 'vue'
import { useAppStore } from '../stores/app'
import { api, type LoadModelParams } from '../api/client'

const store = useAppStore()

// Component state
const swapping = ref(false)
const selectedVae = ref('')
const selectedTaesd = ref('')
const taePreviewOnly = ref(false)

// Models lists
const vaeModels = computed(() => store.models?.vae || [])
const taesdModels = computed(() => store.models?.taesd || [])

// Loaded model info
const modelLoaded = computed(() => store.modelLoaded)
const modelName = computed(() => store.modelName)
const modelArchitecture = computed(() => store.modelArchitecture)
const loadedComponents = computed(() => store.loadedComponents)

// Current loaded components (extract filenames)
const currentVae = computed(() => {
  const vae = loadedComponents.value?.vae
  return vae ? vae.split('/').pop() : null
})

// Determine which components are available based on architecture
const supportsExternalVae = computed(() => {
  const architecture = modelArchitecture.value?.toLowerCase() || ''
  // Most modern architectures support external VAE
  return ['flux', 'z-image', 'sd3', 'sdxl', 'chroma', 'wan', 'qwen'].some(a => architecture.includes(a))
})

const supportsTaesd = computed(() => {
  // TAESD is available for most architectures
  // SD1.x uses taesd, SDXL uses taesdxl, Flux/Z-Image uses taef1
  return modelLoaded.value
})

// Get recommended TAESD type based on architecture
const recommendedTaesdType = computed(() => {
  const arch = modelArchitecture.value?.toLowerCase() || ''
  if (arch.includes('flux') || arch.includes('z-image')) return 'taef1'
  if (arch.includes('sdxl') || arch.includes('sd3')) return 'taesdxl'
  return 'taesd'
})

// Initialize selections from current state
function initializeSelections() {
  if (currentVae.value) {
    // Find matching VAE in the list
    const match = vaeModels.value.find(m => m.name === currentVae.value)
    selectedVae.value = match ? match.name : ''
  } else {
    selectedVae.value = ''
  }
  selectedTaesd.value = ''
  taePreviewOnly.value = false
}

watch(modelLoaded, (loaded) => {
  if (loaded) {
    initializeSelections()
  }
})

onMounted(() => {
  if (modelLoaded.value) {
    initializeSelections()
  }
  // Ensure models are loaded
  if (!store.models) {
    store.fetchModels()
  }
})

// Check if changes were made
const hasChanges = computed(() => {
  const vaeChanged = selectedVae.value !== (currentVae.value || '')
  const taesdChanged = selectedTaesd.value !== ''
  return vaeChanged || taesdChanged
})

// Apply changes by reloading the model with new components
async function applyChanges() {
  if (!modelName.value || !hasChanges.value) return

  swapping.value = true
  try {
    // Build load params - we need to reload the model with the new components
    const params: LoadModelParams = {
      model_name: modelName.value,
      model_type: store.modelType === 'diffusion' ? 'diffusion' : 'checkpoint'
    }

    // Add VAE if selected
    if (selectedVae.value) {
      params.vae = selectedVae.value
    }

    // Add TAESD if selected
    if (selectedTaesd.value) {
      params.taesd = selectedTaesd.value
      params.options = {
        ...params.options,
        tae_preview_only: taePreviewOnly.value
      }
    }

    // Preserve existing loaded components
    if (loadedComponents.value?.clip_l) {
      params.clip_l = loadedComponents.value.clip_l
    }
    if (loadedComponents.value?.clip_g) {
      params.clip_g = loadedComponents.value.clip_g
    }
    if (loadedComponents.value?.t5xxl) {
      params.t5xxl = loadedComponents.value.t5xxl
    }
    if (loadedComponents.value?.llm) {
      params.llm = loadedComponents.value.llm
    }
    if (loadedComponents.value?.controlnet) {
      params.controlnet = loadedComponents.value.controlnet
    }

    await api.loadModel(params)
    store.showToast('Model components updated', 'success')
    store.fetchHealth()
  } catch (e) {
    store.showToast(e instanceof Error ? e.message : 'Failed to update components', 'error')
  } finally {
    swapping.value = false
  }
}
</script>

<template>
  <div v-if="modelLoaded" class="loaded-model-panel">
    <div class="panel-header">
      <h4 class="panel-title">Loaded Model</h4>
      <span class="architecture-badge">{{ modelArchitecture || 'Unknown' }}</span>
    </div>

    <div class="model-info">
      <span class="model-name" :title="modelName || ''">{{ modelName?.split('/').pop() }}</span>
    </div>

    <!-- Current Components -->
    <div class="components-section">
      <div class="component-row" v-if="currentVae">
        <span class="component-label">VAE:</span>
        <span class="component-value">{{ currentVae }}</span>
      </div>
    </div>

    <!-- Component Swap Section -->
    <div v-if="supportsExternalVae || supportsTaesd" class="swap-section">
      <div class="swap-header">Swap Components</div>

      <!-- VAE Swap -->
      <div v-if="supportsExternalVae" class="form-group compact">
        <label class="form-label-sm">VAE</label>
        <select v-model="selectedVae" class="form-select form-select-sm">
          <option value="">{{ currentVae ? `Current: ${currentVae}` : 'None (bundled)' }}</option>
          <option v-for="m in vaeModels" :key="m.name" :value="m.name">{{ m.name }}</option>
        </select>
      </div>

      <!-- TAESD Swap -->
      <div v-if="supportsTaesd" class="form-group compact">
        <label class="form-label-sm">
          TAESD
          <span class="hint">({{ recommendedTaesdType }} recommended)</span>
        </label>
        <select v-model="selectedTaesd" class="form-select form-select-sm">
          <option value="">None</option>
          <option v-for="m in taesdModels" :key="m.name" :value="m.name">{{ m.name }}</option>
        </select>
      </div>

      <!-- TAE Preview Only -->
      <label v-if="selectedTaesd" class="form-checkbox compact">
        <input v-model="taePreviewOnly" type="checkbox" />
        <span>TAE for preview only (VAE for final)</span>
      </label>

      <!-- Apply Button -->
      <button
        v-if="hasChanges"
        class="btn btn-primary btn-sm apply-btn"
        @click="applyChanges"
        :disabled="swapping"
      >
        <span v-if="swapping" class="spinner spinner-sm"></span>
        {{ swapping ? 'Reloading...' : 'Apply Changes' }}
      </button>

      <div v-if="hasChanges" class="reload-notice">
        Model will be reloaded with new components
      </div>
    </div>
  </div>
</template>

<style scoped>
.loaded-model-panel {
  background: var(--bg-secondary);
  border: 1px solid var(--border-color);
  border-radius: var(--border-radius);
  padding: 12px;
  margin-bottom: 16px;
}

.panel-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 8px;
}

.panel-title {
  font-size: 13px;
  font-weight: 600;
  margin: 0;
  color: var(--text-primary);
}

.architecture-badge {
  font-size: 10px;
  padding: 2px 8px;
  border-radius: 4px;
  background: var(--accent-primary);
  color: var(--bg-primary);
  font-weight: 600;
  text-transform: uppercase;
}

.model-info {
  margin-bottom: 12px;
}

.model-name {
  font-size: 12px;
  color: var(--text-secondary);
  word-break: break-all;
}

.components-section {
  margin-bottom: 12px;
  padding-bottom: 12px;
  border-bottom: 1px solid var(--border-color);
}

.component-row {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 12px;
}

.component-label {
  color: var(--text-muted);
  min-width: 50px;
}

.component-value {
  color: var(--text-primary);
  font-family: monospace;
  font-size: 11px;
}

.swap-section {
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.swap-header {
  font-size: 11px;
  font-weight: 600;
  color: var(--text-muted);
  text-transform: uppercase;
  letter-spacing: 0.5px;
}

.form-group.compact {
  margin-bottom: 0;
}

.form-label-sm {
  font-size: 11px;
  color: var(--text-secondary);
  margin-bottom: 4px;
  display: block;
}

.form-label-sm .hint {
  color: var(--text-muted);
  font-weight: normal;
}

.form-select-sm {
  padding: 6px 10px;
  font-size: 12px;
}

.form-checkbox.compact {
  font-size: 11px;
  color: var(--text-secondary);
}

.form-checkbox.compact input {
  margin-right: 6px;
}

.apply-btn {
  margin-top: 4px;
}

.reload-notice {
  font-size: 10px;
  color: var(--text-muted);
  font-style: italic;
}

.spinner-sm {
  width: 12px;
  height: 12px;
  margin-right: 6px;
}
</style>
