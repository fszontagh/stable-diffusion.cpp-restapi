<script setup lang="ts">
import { useAppStore } from '../stores/app'
import { api } from '../api/client'
import { ref } from 'vue'

const store = useAppStore()
const unloading = ref(false)
const unloadingUpscaler = ref(false)
const showAllSettings = ref(false)

async function handleUnloadModel() {
  unloading.value = true
  try {
    await api.unloadModel()
    store.showToast('Model unloaded successfully', 'success')
    store.fetchHealth()
  } catch (e) {
    store.showToast(e instanceof Error ? e.message : 'Failed to unload model', 'error')
  } finally {
    unloading.value = false
  }
}

async function handleUnloadUpscaler() {
  unloadingUpscaler.value = true
  try {
    await api.unloadUpscaler()
    store.showToast('Upscaler unloaded successfully', 'success')
    store.fetchHealth()
  } catch (e) {
    store.showToast(e instanceof Error ? e.message : 'Failed to unload upscaler', 'error')
  } finally {
    unloadingUpscaler.value = false
  }
}

function formatComponentValue(value: string | null): string {
  if (!value) return 'Not loaded'
  return value
}

function formatOptionLabel(key: string): string {
  // Convert snake_case to readable labels
  const labels: Record<string, string> = {
    n_threads: 'Threads',
    keep_clip_on_cpu: 'CLIP on CPU',
    keep_vae_on_cpu: 'VAE on CPU',
    keep_controlnet_on_cpu: 'ControlNet on CPU',
    flash_attn: 'Flash Attention',
    offload_to_cpu: 'Offload to CPU',
    enable_mmap: 'Memory Mapping',
    vae_decode_only: 'VAE Decode Only',
    vae_conv_direct: 'VAE Conv Direct',
    diffusion_conv_direct: 'Diffusion Conv Direct',
    tae_preview_only: 'TAE Preview Only',
    free_params_immediately: 'Free Params Immediately',
    flow_shift: 'Flow Shift',
    weight_type: 'Weight Type',
    tensor_type_rules: 'Tensor Type Rules',
    rng_type: 'RNG Type',
    sampler_rng_type: 'Sampler RNG',
    prediction: 'Prediction',
    lora_apply_mode: 'LoRA Apply Mode',
    vae_tiling: 'VAE Tiling',
    vae_tile_size_x: 'VAE Tile X',
    vae_tile_size_y: 'VAE Tile Y',
    vae_tile_overlap: 'VAE Tile Overlap',
    chroma_use_dit_mask: 'Chroma DiT Mask',
    chroma_use_t5_mask: 'Chroma T5 Mask',
    chroma_t5_mask_pad: 'Chroma T5 Mask Pad',
    offload_mode: 'Dynamic VRAM Mode',
    offload_cond_stage: 'Offload Cond Stage',
    offload_diffusion: 'Offload Diffusion',
    reload_cond_stage: 'Reload Cond Stage',
    log_offload_events: 'Log Offload Events',
    min_offload_size_mb: 'Min Offload Size (MB)'
  }
  return labels[key] || key.replace(/_/g, ' ').replace(/\b\w/g, l => l.toUpperCase())
}

function formatOptionValue(value: unknown): string {
  if (value === null || value === undefined) return '-'
  if (typeof value === 'boolean') return value ? 'Yes' : 'No'
  if (typeof value === 'number') return value === -1 ? 'Auto' : String(value)
  return String(value)
}

// Important settings to show prominently
const importantSettings = [
  'weight_type', 'flash_attn', 'keep_clip_on_cpu', 'keep_vae_on_cpu',
  'vae_tiling', 'offload_mode', 'lora_apply_mode'
]
</script>

<template>
  <div class="dashboard">
    <div class="page-header">
      <h1 class="page-title">Dashboard</h1>
      <p class="page-subtitle">Server status and loaded model information</p>
    </div>

    <!-- Stats -->
    <div class="stats-grid">
      <div class="stat-card">
        <div class="stat-value" :class="store.connected ? 'text-success' : 'text-error'">
          {{ store.connected ? 'Online' : 'Offline' }}
        </div>
        <div class="stat-label">Server Status</div>
      </div>
      <div class="stat-card">
        <div class="stat-value">{{ store.queueStats.pending }}</div>
        <div class="stat-label">Pending Jobs</div>
      </div>
      <div class="stat-card">
        <div class="stat-value">{{ store.queueStats.processing }}</div>
        <div class="stat-label">Processing</div>
      </div>
      <div class="stat-card">
        <div class="stat-value">{{ store.queueStats.completed }}</div>
        <div class="stat-label">Completed</div>
      </div>
    </div>

    <!-- Model Card -->
    <div class="card">
      <div class="card-header">
        <h2 class="card-title">Loaded Model</h2>
        <button
          v-if="store.modelLoaded"
          class="btn btn-danger btn-sm"
          @click="handleUnloadModel"
          :disabled="unloading"
        >
          <span v-if="unloading" class="spinner"></span>
          {{ unloading ? 'Unloading...' : 'Unload Model' }}
        </button>
      </div>

      <!-- Model Loaded -->
      <div v-if="store.modelLoaded" class="model-details">
        <div class="detail-row">
          <span class="detail-label">Name</span>
          <span class="detail-value">{{ store.modelName }}</span>
        </div>
        <div class="detail-row">
          <span class="detail-label">Type</span>
          <span class="detail-value">{{ store.modelType }}</span>
        </div>
        <div class="detail-row">
          <span class="detail-label">Architecture</span>
          <span class="detail-value architecture">{{ store.modelArchitecture || 'Unknown' }}</span>
        </div>
      </div>

      <!-- Model Loading -->
      <div v-else-if="store.modelLoading" class="loading-state">
        <div class="loading-spinner-large"></div>
        <div class="loading-title">Loading Model</div>
        <div class="loading-model-name">{{ store.loadingModelName }}</div>
      </div>

      <!-- Load Error -->
      <div v-else-if="store.lastLoadError" class="error-state">
        <div class="error-icon">&#9888;</div>
        <div class="error-title">Model Load Failed</div>
        <pre class="error-message">{{ store.lastLoadError }}</pre>
        <router-link to="/models" class="btn btn-primary btn-sm">Try Again</router-link>
      </div>

      <!-- No Model -->
      <div v-else class="empty-state">
        <div class="empty-state-icon">&#128230;</div>
        <div class="empty-state-title">No Model Loaded</div>
        <p>Go to the <router-link to="/models">Models</router-link> page to load a model</p>
      </div>
    </div>

    <!-- Components Card -->
    <div class="card" v-if="store.modelLoaded && store.loadedComponents">
      <div class="card-header">
        <h2 class="card-title">Loaded Components</h2>
      </div>
      <div class="components-grid">
        <div class="component-item">
          <span class="component-label">VAE</span>
          <span :class="['component-value', { loaded: store.loadedComponents.vae }]">
            {{ formatComponentValue(store.loadedComponents.vae) }}
          </span>
        </div>
        <div class="component-item">
          <span class="component-label">CLIP-L</span>
          <span :class="['component-value', { loaded: store.loadedComponents.clip_l }]">
            {{ formatComponentValue(store.loadedComponents.clip_l) }}
          </span>
        </div>
        <div class="component-item">
          <span class="component-label">CLIP-G</span>
          <span :class="['component-value', { loaded: store.loadedComponents.clip_g }]">
            {{ formatComponentValue(store.loadedComponents.clip_g) }}
          </span>
        </div>
        <div class="component-item">
          <span class="component-label">T5-XXL</span>
          <span :class="['component-value', { loaded: store.loadedComponents.t5xxl }]">
            {{ formatComponentValue(store.loadedComponents.t5xxl) }}
          </span>
        </div>
        <div class="component-item">
          <span class="component-label">ControlNet</span>
          <span :class="['component-value', { loaded: store.loadedComponents.controlnet }]">
            {{ formatComponentValue(store.loadedComponents.controlnet) }}
          </span>
        </div>
        <div class="component-item">
          <span class="component-label">LLM</span>
          <span :class="['component-value', { loaded: store.loadedComponents.llm }]">
            {{ formatComponentValue(store.loadedComponents.llm) }}
          </span>
        </div>
      </div>
    </div>

    <!-- Load Settings Card -->
    <div class="card" v-if="store.modelLoaded && store.loadOptions">
      <div class="card-header">
        <h2 class="card-title">Load Settings</h2>
        <button
          class="btn btn-ghost btn-sm"
          @click="showAllSettings = !showAllSettings"
        >
          {{ showAllSettings ? 'Show Less' : 'Show All' }}
        </button>
      </div>

      <!-- Important settings always visible -->
      <div class="settings-grid">
        <template v-for="key in importantSettings" :key="key">
          <div v-if="store.loadOptions[key as keyof typeof store.loadOptions] !== undefined" class="setting-item important">
            <span class="setting-label">{{ formatOptionLabel(key) }}</span>
            <span class="setting-value" :class="{
              'text-success': store.loadOptions[key as keyof typeof store.loadOptions] === true,
              'text-muted': store.loadOptions[key as keyof typeof store.loadOptions] === false
            }">
              {{ formatOptionValue(store.loadOptions[key as keyof typeof store.loadOptions]) }}
            </span>
          </div>
        </template>
      </div>

      <!-- All other settings (expandable) -->
      <div v-if="showAllSettings" class="settings-grid all-settings">
        <template v-for="(value, key) in store.loadOptions" :key="key">
          <div v-if="!importantSettings.includes(key as string)" class="setting-item">
            <span class="setting-label">{{ formatOptionLabel(key as string) }}</span>
            <span class="setting-value" :class="{
              'text-success': value === true,
              'text-muted': value === false
            }">
              {{ formatOptionValue(value) }}
            </span>
          </div>
        </template>
      </div>
    </div>

    <!-- Upscaler Card -->
    <div class="card">
      <div class="card-header">
        <h2 class="card-title">Upscaler</h2>
        <button
          v-if="store.upscalerLoaded"
          class="btn btn-danger btn-sm"
          @click="handleUnloadUpscaler"
          :disabled="unloadingUpscaler"
        >
          <span v-if="unloadingUpscaler" class="spinner"></span>
          {{ unloadingUpscaler ? 'Unloading...' : 'Unload' }}
        </button>
      </div>

      <div v-if="store.upscalerLoaded" class="model-details">
        <div class="detail-row">
          <span class="detail-label">Model</span>
          <span class="detail-value">{{ store.upscalerName }}</span>
        </div>
      </div>

      <div v-else class="empty-state-small">
        <p class="text-muted">No upscaler loaded. Go to <router-link to="/upscale">Upscale</router-link> to load one.</p>
      </div>
    </div>
  </div>
</template>

<style scoped>
.model-details {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.detail-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 8px 0;
}

.detail-row:not(:last-child) {
  border-bottom: 1px solid rgba(255, 255, 255, 0.05);
}

.detail-label {
  color: var(--text-secondary);
  font-size: 13px;
}

.detail-value {
  font-weight: 500;
  color: var(--text-primary);
}

.detail-value.architecture {
  color: var(--accent-purple);
}

.components-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
  gap: 16px;
}

.component-item {
  display: flex;
  flex-direction: column;
  gap: 4px;
  padding: 12px;
  background: var(--bg-tertiary);
  border-radius: var(--border-radius-sm);
}

.component-label {
  font-size: 12px;
  color: var(--text-secondary);
  text-transform: uppercase;
}

.component-value {
  font-size: 13px;
  color: var(--text-muted);
  word-break: break-all;
}

.component-value.loaded {
  color: var(--accent-success);
}

.empty-state-small {
  padding: 16px;
  text-align: center;
}

/* Loading state */
.loading-state {
  display: flex;
  flex-direction: column;
  align-items: center;
  padding: 32px;
  gap: 16px;
}

.loading-spinner-large {
  width: 48px;
  height: 48px;
  border: 4px solid var(--border-color);
  border-top-color: var(--accent-primary);
  border-radius: 50%;
  animation: spin 1s linear infinite;
}

@keyframes spin {
  to { transform: rotate(360deg); }
}

.loading-title {
  font-size: 18px;
  font-weight: 500;
  color: var(--text-primary);
}

.loading-model-name {
  font-size: 14px;
  color: var(--accent-primary);
  font-family: monospace;
  word-break: break-all;
  text-align: center;
}

/* Error state */
.error-state {
  display: flex;
  flex-direction: column;
  align-items: center;
  padding: 24px;
  gap: 12px;
}

.error-icon {
  font-size: 48px;
  color: var(--accent-error);
}

.error-title {
  font-size: 18px;
  font-weight: 500;
  color: var(--accent-error);
}

.error-message {
  background: rgba(255, 107, 157, 0.1);
  border: 1px solid var(--accent-error);
  border-radius: var(--border-radius-sm);
  padding: 12px 16px;
  font-size: 12px;
  color: var(--accent-error);
  max-width: 100%;
  overflow-x: auto;
  white-space: pre-wrap;
  word-break: break-word;
  margin: 8px 0;
}

/* Settings grid */
.settings-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
  gap: 12px;
}

.settings-grid.all-settings {
  margin-top: 16px;
  padding-top: 16px;
  border-top: 1px solid var(--border-color);
}

.setting-item {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 8px 12px;
  background: var(--bg-tertiary);
  border-radius: var(--border-radius-sm);
  gap: 8px;
}

.setting-item.important {
  background: var(--bg-secondary);
  box-shadow: 0 1px 4px rgba(0, 0, 0, 0.15);
}

.setting-label {
  font-size: 12px;
  color: var(--text-secondary);
  white-space: nowrap;
}

.setting-value {
  font-size: 13px;
  font-weight: 500;
  color: var(--text-primary);
  text-align: right;
  word-break: break-all;
}

.setting-value.text-success {
  color: var(--accent-success);
}

.setting-value.text-muted {
  color: var(--text-muted);
}
</style>
