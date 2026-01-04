<script setup lang="ts">
import { ref, computed, onMounted, onBeforeUnmount } from 'vue'
import { useAppStore } from '../stores/app'
import { api } from '../api/client'
import ImageUploader from '../components/ImageUploader.vue'
import Modal from '../components/Modal.vue'

const store = useAppStore()

// Handle assistant set image event (for upscaler)
function handleAssistantSetImage(e: Event) {
  const event = e as CustomEvent<{ target: string; imageBase64: string }>
  if (event.detail.target === 'upscaler') {
    // Set the input image (needs data URL format)
    inputImage.value = `data:image/png;base64,${event.detail.imageBase64}`
  }
}

const loading = ref(false)
const submitting = ref(false)

// Upscaler load params
const selectedModel = ref('')
const tileSize = ref(128)

// Upscale params
const inputImage = ref<string | undefined>()
const repeats = ref(1)

// Confirmation modal for replacing loaded upscaler
const showReplaceModal = ref(false)
const pendingLoadModel = ref('')

const esrganModels = computed(() => store.models?.esrgan || [])

// Request to load upscaler - shows confirmation if one is already loaded
function requestLoadUpscaler() {
  if (!selectedModel.value) {
    store.showToast('Please select an upscaler model', 'warning')
    return
  }

  // Check if upscaler is already loaded
  if (store.upscalerLoaded) {
    pendingLoadModel.value = selectedModel.value
    showReplaceModal.value = true
    return
  }

  loadUpscaler()
}

// Confirm and replace existing upscaler
async function confirmReplaceUpscaler() {
  showReplaceModal.value = false
  selectedModel.value = pendingLoadModel.value
  await loadUpscaler()
}

async function loadUpscaler() {
  if (!selectedModel.value) {
    store.showToast('Please select an upscaler model', 'warning')
    return
  }

  loading.value = true
  try {
    await api.loadUpscaler({
      model_name: selectedModel.value,
      tile_size: tileSize.value
    })
    store.showToast('Upscaler loaded successfully', 'success')
    await store.fetchHealth()  // Wait for health to update so upscalerLoaded reflects the change
  } catch (e) {
    store.showToast(e instanceof Error ? e.message : 'Failed to load upscaler', 'error')
  } finally {
    loading.value = false
  }
}

async function unloadUpscaler() {
  loading.value = true
  try {
    await api.unloadUpscaler()
    store.showToast('Upscaler unloaded', 'success')
    store.fetchHealth()
  } catch (e) {
    store.showToast(e instanceof Error ? e.message : 'Failed to unload upscaler', 'error')
  } finally {
    loading.value = false
  }
}

async function handleUpscale() {
  if (!inputImage.value) {
    store.showToast('Please upload an image', 'warning')
    return
  }

  if (!store.upscalerLoaded) {
    store.showToast('Please load an upscaler first', 'warning')
    return
  }

  submitting.value = true
  try {
    // Extract base64 data from data URL (remove "data:image/...;base64," prefix)
    let base64Data = inputImage.value
    if (base64Data.startsWith('data:')) {
      const commaIndex = base64Data.indexOf(',')
      if (commaIndex !== -1) {
        base64Data = base64Data.substring(commaIndex + 1)
      }
    }

    const result = await api.upscale({
      image_base64: base64Data,
      tile_size: tileSize.value,
      repeats: repeats.value
    })
    store.showToast(`Upscale job submitted: ${result.job_id}. Image kept for additional upscales.`, 'success')
    // Don't navigate away - keep the image so user can submit more upscale jobs
  } catch (e) {
    store.showToast(e instanceof Error ? e.message : 'Failed to submit upscale job', 'error')
  } finally {
    submitting.value = false
  }
}

onMounted(() => {
  store.fetchModels()
  window.addEventListener('assistant-set-image', handleAssistantSetImage)

  // Check if there's an image waiting in the store (from navigation from Queue page)
  if (store.upscaleInputImage) {
    inputImage.value = store.upscaleInputImage
    // Clear it from store after loading (one-time use)
    store.clearUpscaleInputImage()
  }
})

onBeforeUnmount(() => {
  window.removeEventListener('assistant-set-image', handleAssistantSetImage)
})
</script>

<template>
  <div class="upscale">
    <div class="page-header">
      <h1 class="page-title">Upscale</h1>
      <p class="page-subtitle">Enhance image resolution with ESRGAN</p>
    </div>

    <!-- Upscaler Model Section -->
    <div class="card">
      <div class="card-header">
        <h3 class="card-title">Upscaler Model</h3>
        <span v-if="store.upscalerLoaded" class="badge badge-completed">
          {{ store.upscalerName }}
        </span>
      </div>

      <div class="form-group">
        <label class="form-label">Select ESRGAN Model</label>
        <select v-model="selectedModel" class="form-select">
          <option value="">-- Select Model --</option>
          <option v-for="m in esrganModels" :key="m.name" :value="m.name">
            {{ m.name }}
          </option>
        </select>
      </div>

      <div class="form-group">
        <label class="form-label">Tile Size</label>
        <input v-model.number="tileSize" type="number" class="form-input" min="32" max="512" step="32" />
        <div class="form-hint">Smaller tiles use less VRAM but may be slower</div>
      </div>

      <div class="upscaler-actions">
        <button
          class="btn btn-primary"
          @click="requestLoadUpscaler"
          :disabled="loading || !selectedModel"
        >
          <span v-if="loading" class="spinner"></span>
          {{ loading ? 'Loading...' : (store.upscalerLoaded ? 'Switch Upscaler' : 'Load Upscaler') }}
        </button>
        <button
          v-if="store.upscalerLoaded"
          class="btn btn-danger"
          @click="unloadUpscaler"
          :disabled="loading"
        >
          <span v-if="loading" class="spinner"></span>
          {{ loading ? 'Unloading...' : 'Unload' }}
        </button>
      </div>

      <div v-if="esrganModels.length === 0" class="empty-state-small mt-3">
        <p class="text-muted">No ESRGAN models found. Add models to the esrgan directory.</p>
      </div>
    </div>

    <!-- Replace Upscaler Confirmation Modal -->
    <Modal :show="showReplaceModal" title="Replace Upscaler?" @close="showReplaceModal = false">
      <p>An upscaler model is currently loaded: <strong>{{ store.upscalerName }}</strong></p>
      <p>Do you want to unload it and load <strong>{{ pendingLoadModel }}</strong> instead?</p>
      <template #footer>
        <button class="btn btn-secondary" @click="showReplaceModal = false">Cancel</button>
        <button class="btn btn-primary" @click="confirmReplaceUpscaler">Replace</button>
      </template>
    </Modal>

    <!-- Upscale Form -->
    <div class="card">
      <div class="card-header">
        <h3 class="card-title">Upscale Image</h3>
      </div>

      <ImageUploader v-model="inputImage" label="Input Image" />

      <div class="form-group">
        <label class="form-label">Repeats</label>
        <input v-model.number="repeats" type="number" class="form-input" min="1" max="4" />
        <div class="form-hint">Run upscaler multiple times (e.g., 2x 4x = 16x total)</div>
      </div>

      <button
        class="btn btn-primary btn-lg"
        @click="handleUpscale"
        :disabled="submitting || !store.upscalerLoaded || !inputImage"
      >
        <span v-if="submitting" class="spinner"></span>
        {{ submitting ? 'Submitting...' : 'Upscale Image' }}
      </button>

      <p v-if="!store.upscalerLoaded" class="text-warning mt-2">
        Load an upscaler model first
      </p>
    </div>
  </div>
</template>

<style scoped>
.upscale {
  max-width: 600px;
}

.upscaler-actions {
  display: flex;
  gap: 12px;
  align-items: center;
}

.empty-state-small {
  padding: 12px;
  text-align: center;
}
</style>
