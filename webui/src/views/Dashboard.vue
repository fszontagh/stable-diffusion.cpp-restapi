<script setup lang="ts">
import { useAppStore } from '../stores/app'
import { api } from '../api/client'
import { ref } from 'vue'

const store = useAppStore()
const unloading = ref(false)
const unloadingUpscaler = ref(false)

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
  border-bottom: 1px solid var(--border-color);
}

.detail-row:last-child {
  border-bottom: none;
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
</style>
