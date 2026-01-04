<script setup lang="ts">
import { ref, computed, watch } from 'vue'
import { api, type DownloadParams, type CivitAIModelInfo, type HuggingFaceModelInfo } from '../api/client'
import { useAppStore } from '../stores/app'

const appStore = useAppStore()

// Source type selection
const sourceType = ref<'url' | 'civitai' | 'huggingface'>('url')

// Form fields
const modelType = ref<DownloadParams['model_type']>('checkpoint')
const subfolder = ref('')
const url = ref('')
const civitaiId = ref('')
const hfRepoId = ref('')
const hfFilename = ref('')
const hfRevision = ref('main')

// State
const loading = ref(false)
const fetchingInfo = ref(false)
const error = ref<string | null>(null)
const success = ref<string | null>(null)
const civitaiInfo = ref<CivitAIModelInfo | null>(null)
const hfInfo = ref<HuggingFaceModelInfo | null>(null)

// Model type options
const modelTypes = [
  { value: 'checkpoint', label: 'Checkpoint (SD 1.x/2.x/XL)' },
  { value: 'diffusion', label: 'Diffusion (Flux/SD3/Qwen/Wan)' },
  { value: 'vae', label: 'VAE' },
  { value: 'lora', label: 'LoRA' },
  { value: 'clip', label: 'CLIP' },
  { value: 't5', label: 'T5' },
  { value: 'embedding', label: 'Embedding' },
  { value: 'controlnet', label: 'ControlNet' },
  { value: 'llm', label: 'LLM' },
  { value: 'esrgan', label: 'ESRGAN (Upscaler)' },
  { value: 'taesd', label: 'TAESD (Preview)' }
]

// Download jobs from queue
const downloadJobs = computed(() => {
  const items = appStore.queue?.items ?? []
  return items.filter(j => j.type === 'model_download' || j.type === 'model_hash')
})

// Format file size
function formatFileSize(bytes: number): string {
  if (bytes === 0) return '0 B'
  const k = 1024
  const sizes = ['B', 'KB', 'MB', 'GB', 'TB']
  const i = Math.floor(Math.log(bytes) / Math.log(k))
  return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i]
}

// Fetch CivitAI info
async function fetchCivitaiInfo() {
  if (!civitaiId.value.trim()) return

  fetchingInfo.value = true
  error.value = null
  civitaiInfo.value = null

  try {
    civitaiInfo.value = await api.getCivitAIInfo(civitaiId.value.trim())

    // Auto-detect model type from CivitAI
    const typeMap: Record<string, DownloadParams['model_type']> = {
      'Checkpoint': 'checkpoint',
      'TextualInversion': 'embedding',
      'LORA': 'lora',
      'LoCon': 'lora',
      'VAE': 'vae',
      'Controlnet': 'controlnet',
      'Upscaler': 'esrgan'
    }
    if (civitaiInfo.value.type && typeMap[civitaiInfo.value.type]) {
      modelType.value = typeMap[civitaiInfo.value.type]
    }
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Failed to fetch CivitAI info'
  } finally {
    fetchingInfo.value = false
  }
}

// Fetch HuggingFace info
async function fetchHFInfo() {
  if (!hfRepoId.value.trim() || !hfFilename.value.trim()) return

  fetchingInfo.value = true
  error.value = null
  hfInfo.value = null

  try {
    hfInfo.value = await api.getHuggingFaceInfo(
      hfRepoId.value.trim(),
      hfFilename.value.trim(),
      hfRevision.value || 'main'
    )
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Failed to fetch HuggingFace info'
  } finally {
    fetchingInfo.value = false
  }
}

// Submit download
async function submitDownload() {
  loading.value = true
  error.value = null
  success.value = null

  try {
    const params: DownloadParams = {
      model_type: modelType.value,
      subfolder: subfolder.value || undefined
    }

    if (sourceType.value === 'url') {
      params.url = url.value
    } else if (sourceType.value === 'civitai') {
      params.model_id = civitaiId.value
    } else if (sourceType.value === 'huggingface') {
      params.repo_id = hfRepoId.value
      params.filename = hfFilename.value
      params.revision = hfRevision.value || 'main'
    }

    const result = await api.downloadModel(params)
    success.value = `Download started! Job ID: ${result.download_job_id}`

    // Refresh queue to show new job
    await appStore.fetchQueue()

    // Reset form
    resetForm()
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Download failed'
  } finally {
    loading.value = false
  }
}

function resetForm() {
  url.value = ''
  civitaiId.value = ''
  hfRepoId.value = ''
  hfFilename.value = ''
  hfRevision.value = 'main'
  subfolder.value = ''
  civitaiInfo.value = null
  hfInfo.value = null
}

// Watch for URL changes to auto-detect source
watch(url, (newUrl) => {
  if (newUrl.includes('civitai.com')) {
    sourceType.value = 'civitai'
    // Extract model ID from URL
    const match = newUrl.match(/\/models\/(\d+)(?:[\/?]|$)/)
    if (match) {
      civitaiId.value = match[1]
      // Check for version ID in URL
      const versionMatch = newUrl.match(/[?&]modelVersionId=(\d+)/)
      if (versionMatch) {
        civitaiId.value = match[1] + ':' + versionMatch[1]
      }
      fetchCivitaiInfo()
    }
  } else if (newUrl.includes('huggingface.co')) {
    sourceType.value = 'huggingface'
    // Extract repo ID and filename from URL
    const match = newUrl.match(/huggingface\.co\/([^/]+\/[^/]+)(?:\/(?:blob|resolve)\/([^/]+)\/(.+))?/)
    if (match) {
      hfRepoId.value = match[1]
      if (match[2]) hfRevision.value = match[2]
      if (match[3]) hfFilename.value = match[3]
      if (hfRepoId.value && hfFilename.value) {
        fetchHFInfo()
      }
    }
  }
})

// Debounced CivitAI ID fetch
let civitaiTimeout: ReturnType<typeof setTimeout>
watch(civitaiId, (newId) => {
  clearTimeout(civitaiTimeout)
  if (newId.trim()) {
    civitaiTimeout = setTimeout(fetchCivitaiInfo, 500)
  } else {
    civitaiInfo.value = null
  }
})
</script>

<template>
  <div class="downloads-page">
    <h1>Download Models</h1>
    <p class="page-description">
      Download models from URLs, CivitAI, or HuggingFace. Downloaded models will be saved to the appropriate folder.
    </p>

    <!-- Source Tabs -->
    <div class="source-tabs">
      <button
        v-for="src in [
          { value: 'url', label: 'Direct URL' },
          { value: 'civitai', label: 'CivitAI' },
          { value: 'huggingface', label: 'HuggingFace' }
        ]"
        :key="src.value"
        :class="['tab', { active: sourceType === src.value }]"
        @click="sourceType = src.value as 'url' | 'civitai' | 'huggingface'"
      >
        {{ src.label }}
      </button>
    </div>

    <form @submit.prevent="submitDownload" class="download-form">
      <!-- URL Source -->
      <div v-if="sourceType === 'url'" class="form-section">
        <label for="url">Model URL</label>
        <input
          id="url"
          v-model="url"
          type="url"
          placeholder="https://example.com/model.safetensors"
          required
        />
        <p class="hint">
          Paste a direct download link to a model file. Supported formats: .safetensors, .ckpt, .pt, .pth, .bin, .gguf
        </p>
      </div>

      <!-- CivitAI Source -->
      <div v-else-if="sourceType === 'civitai'" class="form-section">
        <label for="civitai-id">CivitAI Model ID</label>
        <div class="input-with-button">
          <input
            id="civitai-id"
            v-model="civitaiId"
            type="text"
            placeholder="123456 or 123456:789012"
            required
          />
          <button
            type="button"
            class="btn-secondary"
            :disabled="!civitaiId.trim() || fetchingInfo"
            @click="fetchCivitaiInfo"
          >
            {{ fetchingInfo ? 'Fetching...' : 'Fetch Info' }}
          </button>
        </div>
        <p class="hint">
          Enter model ID (e.g., 123456) or model:version (e.g., 123456:789012). Find the ID in the CivitAI URL.
        </p>

        <!-- CivitAI Info Card -->
        <div v-if="civitaiInfo" class="info-card">
          <h3>{{ civitaiInfo.name }}</h3>
          <p class="version">Version: {{ civitaiInfo.version_name }}</p>
          <div class="info-grid">
            <div><strong>Type:</strong> {{ civitaiInfo.type }}</div>
            <div><strong>Base Model:</strong> {{ civitaiInfo.base_model }}</div>
            <div><strong>File:</strong> {{ civitaiInfo.filename }}</div>
            <div><strong>Size:</strong> {{ formatFileSize(civitaiInfo.file_size) }}</div>
          </div>
        </div>
      </div>

      <!-- HuggingFace Source -->
      <div v-else-if="sourceType === 'huggingface'" class="form-section">
        <label for="hf-repo">Repository ID</label>
        <input
          id="hf-repo"
          v-model="hfRepoId"
          type="text"
          placeholder="organization/model-name"
          required
        />

        <label for="hf-file">Filename</label>
        <div class="input-with-button">
          <input
            id="hf-file"
            v-model="hfFilename"
            type="text"
            placeholder="model.safetensors"
            required
          />
          <button
            type="button"
            class="btn-secondary"
            :disabled="!hfRepoId.trim() || !hfFilename.trim() || fetchingInfo"
            @click="fetchHFInfo"
          >
            {{ fetchingInfo ? 'Fetching...' : 'Fetch Info' }}
          </button>
        </div>

        <label for="hf-revision">Revision (optional)</label>
        <input
          id="hf-revision"
          v-model="hfRevision"
          type="text"
          placeholder="main"
        />
        <p class="hint">Branch, tag, or commit hash. Default: main</p>

        <!-- HuggingFace Info Card -->
        <div v-if="hfInfo" class="info-card">
          <h3>{{ hfInfo.repo_id }}</h3>
          <div class="info-grid">
            <div><strong>File:</strong> {{ hfInfo.filename }}</div>
            <div><strong>Revision:</strong> {{ hfInfo.revision }}</div>
            <div><strong>Size:</strong> {{ formatFileSize(hfInfo.file_size) }}</div>
          </div>
        </div>
      </div>

      <!-- Common Fields -->
      <div class="form-section">
        <label for="model-type">Model Type</label>
        <select id="model-type" v-model="modelType" required>
          <option
            v-for="mt in modelTypes"
            :key="mt.value"
            :value="mt.value"
          >
            {{ mt.label }}
          </option>
        </select>
        <p class="hint">Determines which folder the model will be saved to.</p>
      </div>

      <div class="form-section">
        <label for="subfolder">Subfolder (optional)</label>
        <input
          id="subfolder"
          v-model="subfolder"
          type="text"
          placeholder="e.g., SD1.5 or SDXL/anime"
        />
        <p class="hint">Create subfolders within the model type directory. Use "/" for nested folders.</p>
      </div>

      <!-- Status Messages -->
      <div v-if="error" class="message error">
        {{ error }}
      </div>
      <div v-if="success" class="message success">
        {{ success }}
      </div>

      <!-- Submit Button -->
      <div class="form-actions">
        <button type="submit" class="btn-primary" :disabled="loading">
          {{ loading ? 'Starting Download...' : 'Start Download' }}
        </button>
        <button type="button" class="btn-secondary" @click="resetForm">
          Reset
        </button>
      </div>
    </form>

    <!-- Recent Download Jobs -->
    <div class="recent-downloads">
      <h2>Recent Download Jobs</h2>
      <div v-if="downloadJobs.length === 0" class="no-jobs">
        No download jobs yet. Downloads will appear here when started.
      </div>
      <div v-else class="job-list">
        <div
          v-for="job in downloadJobs"
          :key="job.job_id"
          :class="['job-item', job.status]"
        >
          <div class="job-icon">
            <span v-if="job.type === 'model_download'">&#x2B07;</span>
            <span v-else>#</span>
          </div>
          <div class="job-info">
            <div class="job-type">
              {{ job.type === 'model_download' ? 'Download' : 'Hash' }}
            </div>
            <div class="job-details">
              <span v-if="job.params?.model_type">{{ job.params.model_type }}</span>
              <span v-if="job.params?.model_id">CivitAI: {{ job.params.model_id }}</span>
              <span v-if="job.params?.repo_id">HF: {{ job.params.repo_id }}/{{ job.params.filename }}</span>
              <span v-if="job.params?.url && !job.params?.model_id && !job.params?.repo_id">URL download</span>
            </div>
            <div v-if="job.error" class="job-error">{{ job.error }}</div>
            <div v-if="job.outputs.length > 0" class="job-outputs">
              {{ job.outputs[0] }}
            </div>
          </div>
          <div class="job-status">
            <span :class="['status-badge', job.status]">{{ job.status }}</span>
            <div v-if="job.status === 'processing' && job.progress" class="progress-text">
              {{ job.progress.step }}%
            </div>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.downloads-page {
  max-width: 800px;
  margin: 0 auto;
  padding: 24px;
}

h1 {
  margin: 0 0 8px;
  color: var(--text-primary);
}

.page-description {
  color: var(--text-secondary);
  margin-bottom: 24px;
}

.source-tabs {
  display: flex;
  gap: 4px;
  margin-bottom: 24px;
  background: var(--bg-secondary);
  padding: 4px;
  border-radius: var(--border-radius);
}

.tab {
  flex: 1;
  padding: 12px 16px;
  border: none;
  background: transparent;
  color: var(--text-secondary);
  cursor: pointer;
  border-radius: var(--border-radius-sm);
  font-size: 14px;
  font-weight: 500;
  transition: all var(--transition-fast);
}

.tab:hover {
  color: var(--text-primary);
  background: var(--bg-hover);
}

.tab.active {
  background: var(--accent-primary);
  color: #fff;
}

.download-form {
  background: var(--bg-secondary);
  padding: 24px;
  border-radius: var(--border-radius);
  border: 1px solid var(--border-color);
}

.form-section {
  margin-bottom: 20px;
}

.form-section label {
  display: block;
  margin-bottom: 8px;
  font-weight: 500;
  color: var(--text-primary);
}

.form-section input,
.form-section select {
  width: 100%;
  padding: 12px;
  border: 1px solid var(--border-color);
  border-radius: var(--border-radius-sm);
  background: var(--bg-primary);
  color: var(--text-primary);
  font-size: 14px;
}

.form-section input:focus,
.form-section select:focus {
  outline: none;
  border-color: var(--accent-primary);
}

.hint {
  margin-top: 6px;
  font-size: 12px;
  color: var(--text-muted);
}

.input-with-button {
  display: flex;
  gap: 8px;
}

.input-with-button input {
  flex: 1;
}

.btn-primary,
.btn-secondary {
  padding: 12px 24px;
  border: none;
  border-radius: var(--border-radius-sm);
  font-size: 14px;
  font-weight: 500;
  cursor: pointer;
  transition: all var(--transition-fast);
}

.btn-primary {
  background: var(--accent-primary);
  color: #fff;
}

.btn-primary:hover:not(:disabled) {
  background: var(--accent-secondary);
}

.btn-primary:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.btn-secondary {
  background: var(--bg-tertiary);
  color: var(--text-primary);
  border: 1px solid var(--border-color);
}

.btn-secondary:hover:not(:disabled) {
  background: var(--bg-hover);
}

.btn-secondary:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.info-card {
  margin-top: 16px;
  padding: 16px;
  background: var(--bg-tertiary);
  border-radius: var(--border-radius-sm);
  border: 1px solid var(--border-color);
}

.info-card h3 {
  margin: 0 0 4px;
  color: var(--text-primary);
  font-size: 16px;
}

.info-card .version {
  color: var(--text-secondary);
  font-size: 13px;
  margin-bottom: 12px;
}

.info-grid {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 8px;
  font-size: 13px;
}

.info-grid div {
  color: var(--text-secondary);
}

.info-grid strong {
  color: var(--text-primary);
}

.message {
  padding: 12px 16px;
  border-radius: var(--border-radius-sm);
  margin-bottom: 16px;
  font-size: 14px;
}

.message.error {
  background: rgba(255, 107, 107, 0.1);
  color: var(--error);
  border: 1px solid var(--error);
}

.message.success {
  background: rgba(0, 217, 255, 0.1);
  color: var(--accent-primary);
  border: 1px solid var(--accent-primary);
}

.form-actions {
  display: flex;
  gap: 12px;
  margin-top: 24px;
}

.recent-downloads {
  margin-top: 32px;
}

.recent-downloads h2 {
  margin: 0 0 16px;
  font-size: 18px;
  color: var(--text-primary);
}

.no-jobs {
  padding: 24px;
  text-align: center;
  color: var(--text-muted);
  background: var(--bg-secondary);
  border-radius: var(--border-radius);
}

.job-list {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.job-item {
  display: flex;
  align-items: center;
  gap: 16px;
  padding: 16px;
  background: var(--bg-secondary);
  border-radius: var(--border-radius);
  border: 1px solid var(--border-color);
}

.job-item.processing {
  border-color: var(--accent-primary);
}

.job-item.failed {
  border-color: var(--error);
}

.job-item.completed {
  border-color: var(--success);
}

.job-icon {
  width: 32px;
  height: 32px;
  display: flex;
  align-items: center;
  justify-content: center;
  background: var(--bg-tertiary);
  border-radius: 50%;
  font-size: 16px;
}

.job-info {
  flex: 1;
  min-width: 0;
}

.job-type {
  font-weight: 500;
  color: var(--text-primary);
}

.job-details {
  font-size: 12px;
  color: var(--text-secondary);
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
}

.job-error {
  font-size: 12px;
  color: var(--error);
  margin-top: 4px;
}

.job-outputs {
  font-size: 11px;
  color: var(--text-muted);
  margin-top: 4px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.job-status {
  text-align: right;
}

.status-badge {
  display: inline-block;
  padding: 4px 8px;
  border-radius: 4px;
  font-size: 11px;
  font-weight: 500;
  text-transform: uppercase;
}

.status-badge.pending {
  background: var(--bg-tertiary);
  color: var(--text-secondary);
}

.status-badge.processing {
  background: rgba(0, 217, 255, 0.15);
  color: var(--accent-primary);
}

.status-badge.completed {
  background: rgba(0, 200, 83, 0.15);
  color: var(--success);
}

.status-badge.failed {
  background: rgba(255, 107, 107, 0.15);
  color: var(--error);
}

.status-badge.cancelled {
  background: var(--bg-tertiary);
  color: var(--text-muted);
}

.progress-text {
  font-size: 12px;
  color: var(--accent-primary);
  margin-top: 4px;
}
</style>
