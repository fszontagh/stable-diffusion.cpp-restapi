<script setup lang="ts">
import { ref, computed, watch, onMounted } from 'vue'
import { api, type DownloadParams, type CivitAIModelInfo, type HuggingFaceModelInfo, type ArchitecturePreset, type ArchitectureDownload } from '../api/client'
import { useAppStore } from '../stores/app'

const appStore = useAppStore()

// Top-level mode. Manual keeps the existing URL/CivitAI/HF form.
// "architecture" reads the download catalog from data/model_architectures.json
// (via /architectures) and offers one card per canonical file.
const mode = ref<'manual' | 'architecture'>('manual')

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

// -------------------- By-Architecture tab --------------------
//
// Populates from /architectures once on mount, keeps only presets that have
// a non-empty downloads[] array, groups their download entries by component
// so the UI can render one heading per slot (diffusion / vae / t5xxl / ...).
// A per-entry "downloading" flag prevents accidental double-submits.

const archFilter = ref('')
const inFlight = ref<Record<string, boolean>>({})

// Re-scan models whenever a model_download job flips to completed - the
// backend already rescans on its own, but this pulls the fresh list into
// the store so the "Already downloaded" chip appears without a manual
// reload. Debounced trivially by tracking which job IDs we've already
// reacted to.
const seenCompleted = new Set<string>()
watch(downloadJobs, (jobs) => {
  for (const j of jobs) {
    if (j.type === 'model_download' && j.status === 'completed' && !seenCompleted.has(j.job_id)) {
      seenCompleted.add(j.job_id)
      appStore.fetchModels?.().catch(() => { /* fine */ })
    }
  }
}, { deep: true })

onMounted(async () => {
  if (!appStore.architectures) {
    try { await appStore.fetchArchitectures() } catch { /* handled elsewhere */ }
  }
  // Populate the scanned models list so we can render "Already downloaded"
  // badges. Kept best-effort: if the fetch fails the panel just falls back
  // to always showing the Download button, which is safe (backend still
  // short-circuits on already-existing files).
  if (!appStore.models) {
    try { await appStore.fetchModels?.() } catch { /* fine */ }
  }
})

const archsWithDownloads = computed(() => {
  const all = appStore.architectures?.architectures ?? {}
  const list: ArchitecturePreset[] = []
  const needle = archFilter.value.trim().toLowerCase()
  for (const preset of Object.values(all)) {
    if (!preset.downloads || preset.downloads.length === 0) continue
    if (needle) {
      const hay = (preset.id + ' ' + preset.name + ' ' + (preset.description ?? '')).toLowerCase()
      if (!hay.includes(needle)) continue
    }
    list.push(preset)
  }
  return list.sort((a, b) => a.name.localeCompare(b.name))
})

function groupByComponent(entries: ArchitectureDownload[]): Array<{ component: string; items: ArchitectureDownload[] }> {
  const map = new Map<string, ArchitectureDownload[]>()
  for (const e of entries) {
    const key = e.component || 'other'
    if (!map.has(key)) map.set(key, [])
    map.get(key)!.push(e)
  }
  return Array.from(map.entries())
    .map(([component, items]) => ({ component, items }))
    .sort((a, b) => a.component.localeCompare(b.component))
}

// Map target_type -> the ModelsResponse array key. Kept explicit because the
// JSON's target_type values (diffusion / t5 / lora / esrgan) don't always
// match the pluralised store keys (diffusion_models / t5 / loras / esrgan).
const TARGET_TYPE_TO_STORE_KEY: Record<string, keyof NonNullable<typeof appStore.models> | ''> = {
  checkpoint: 'checkpoints',
  diffusion: 'diffusion_models',
  vae: 'vae',
  lora: 'loras',
  clip: 'clip',
  t5: 't5',
  embedding: 'embeddings',
  controlnet: 'controlnets',
  llm: 'llm',
  esrgan: 'esrgan',
  taesd: 'taesd',
  motion_module: 'motion_modules',
  adetailer: 'adetailers',
  ip_adapter: 'ip_adapters'
}

/**
 * Cross-references the scanned models list with a download entry. For
 * single-file HF/URL downloads, matches by basename (case-insensitive) so
 * a user who put a file in a subfolder still gets credit. For HF directory
 * bundles, matches by the repo basename directory name.
 * Purely advisory: even a false positive here is safe because the backend
 * itself short-circuits an existing target file with already_exists=true.
 */
function isAlreadyDownloaded(entry: ArchitectureDownload): boolean {
  const storeKey = TARGET_TYPE_TO_STORE_KEY[entry.target_type]
  if (!storeKey) return false
  const models = appStore.models
  if (!models) return false
  const list = (models[storeKey] ?? []) as Array<{ name: string; is_directory?: boolean }>

  if (entry.bundle === 'directory' && entry.repo_id) {
    const repoBase = entry.repo_id.split('/').pop()?.toLowerCase() ?? ''
    return list.some(m => {
      if (!m.is_directory) return false
      const bn = m.name.replace(/\/$/, '').split('/').pop()?.toLowerCase() ?? ''
      return bn === repoBase
    })
  }

  const target = entry.filename ? entry.filename.split('/').pop()?.toLowerCase() : ''
  if (!target) return false
  // When the entry pins a subfolder, prefer a match rooted at that
  // subfolder - otherwise two arches that both call their file
  // model.safetensors would each look "already downloaded" after the first.
  // Fallback still accepts a bare basename at the root, because users who
  // installed a file manually (or from a pre-subfolder version of the JSON)
  // shouldn't be prompted to re-fetch a multi-GB file - only ambiguous
  // filenames like model.safetensors risk being wrongly deduped, and the
  // backend's own basename check will catch it anyway.
  if (entry.subfolder) {
    const expected = (entry.subfolder + '/' + target).toLowerCase()
    if (list.some(m => m.name.toLowerCase() === expected)) return true
  }
  return list.some(m => (m.name.split('/').pop() ?? '').toLowerCase() === target)
}

/**
 * Look for an in-flight or queued download job that matches this entry.
 * Matched by (repo_id + filename) for single-file HF, (repo_id + bundle) for
 * HF directory bundles, url for direct-URL entries, model_id for CivitAI.
 * A job is "in progress" while its status is pending or processing; a job
 * that has already completed shows up via the scanned-models list instead.
 */
function entryQueuedJob(entry: ArchitectureDownload) {
  const items = appStore.queue?.items ?? []
  for (const job of items) {
    if (job.type !== 'model_download') continue
    if (job.status !== 'pending' && job.status !== 'processing') continue
    const p = (job.params ?? {}) as Record<string, unknown>
    if (entry.source === 'huggingface') {
      if (p.repo_id !== entry.repo_id) continue
      if (entry.bundle === 'directory') {
        if (p.bundle === 'directory') return job
      } else {
        if (p.filename === entry.filename &&
            (p.subfolder ?? '') === (entry.subfolder ?? '')) return job
      }
    } else if (entry.source === 'civitai' && entry.model_id) {
      if (p.model_id === entry.model_id) return job
    } else if (entry.source === 'url' && entry.url) {
      if (p.url === entry.url) return job
    }
  }
  return null
}

function sourceBadgeLabel(entry: ArchitectureDownload): string {
  if (entry.source === 'huggingface' && entry.bundle === 'directory') return 'HF (dir)'
  if (entry.source === 'huggingface') return 'HF'
  if (entry.source === 'civitai') return 'CivitAI'
  return 'URL'
}

async function downloadArchEntry(preset: ArchitecturePreset, entry: ArchitectureDownload): Promise<void> {
  const key = `${preset.id}::${entry.id}`
  if (inFlight.value[key]) return
  inFlight.value[key] = true

  const params: DownloadParams = {
    source: entry.source,
    model_type: entry.target_type as DownloadParams['model_type']
  }
  if (entry.source === 'url' && entry.url) {
    params.url = entry.url
    if (entry.filename) params.filename = entry.filename
  } else if (entry.source === 'civitai' && entry.model_id) {
    params.model_id = entry.model_id
  } else if (entry.source === 'huggingface' && entry.repo_id) {
    params.repo_id = entry.repo_id
    if (entry.revision) params.revision = entry.revision
    if (entry.subfolder) params.subfolder = entry.subfolder
    if (entry.bundle === 'directory') {
      params.bundle = 'directory'
      if (entry.include_patterns) params.include_patterns = entry.include_patterns
      if (entry.exclude_patterns) params.exclude_patterns = entry.exclude_patterns
    } else if (entry.filename) {
      params.filename = entry.filename
    }
  }

  try {
    const res = await api.downloadModel(params)
    // Toast rather than an inline banner - user is likely mid-scroll through
    // a long arch catalog and would miss the message otherwise.
    appStore.showToast(`Started: ${entry.label} (job ${res.download_job_id.slice(0, 8)})`, 'success')
  } catch (e: unknown) {
    appStore.showToast(e instanceof Error ? e.message : String(e), 'error')
  } finally {
    inFlight.value[key] = false
  }
}
</script>

<template>
  <div class="downloads-page">
    <h1>Download Models</h1>
    <p class="page-description">
      Download models from URLs, CivitAI, or HuggingFace. Downloaded models will be saved to the appropriate folder.
    </p>

    <!-- Mode Tabs: Manual (existing free-form form) vs By Architecture (curated catalog) -->
    <div class="mode-tabs">
      <button
        :class="['mode-tab', { active: mode === 'manual' }]"
        @click="mode = 'manual'"
      >Manual</button>
      <button
        :class="['mode-tab', { active: mode === 'architecture' }]"
        @click="mode = 'architecture'"
      >By Architecture</button>
    </div>

    <div class="downloads-layout">
    <!-- Left column: current mode's content (Manual form or Architecture catalog) -->
    <div class="downloads-main">
    <!-- By Architecture: curated catalog from data/model_architectures.json -->
    <div v-if="mode === 'architecture'" class="architecture-catalog">
      <div class="catalog-filter">
        <input
          v-model="archFilter"
          type="text"
          placeholder="Filter architectures (name / description)"
        />
      </div>

      <div v-if="archsWithDownloads.length === 0" class="no-jobs">
        <p v-if="!appStore.architectures">Loading architectures...</p>
        <p v-else-if="archFilter">No architecture matches "{{ archFilter }}".</p>
        <p v-else>No architectures with download catalogs yet.</p>
      </div>

      <div
        v-for="preset in archsWithDownloads"
        :key="preset.id"
        class="arch-card"
      >
        <div class="arch-header">
          <h2>{{ preset.name }}</h2>
          <code class="arch-id">{{ preset.id }}</code>
        </div>
        <p v-if="preset.description" class="arch-description">{{ preset.description }}</p>

        <div
          v-for="group in groupByComponent(preset.downloads ?? [])"
          :key="group.component"
          class="component-group"
        >
          <h3 class="component-heading">{{ group.component }}</h3>
          <div class="download-cards">
            <div
              v-for="entry in group.items"
              :key="entry.id"
              class="download-card"
            >
              <div class="download-card-head">
                <span class="download-label">
                  <span v-if="entry.recommended" class="recommended-star" title="Recommended">&#9733;</span>
                  {{ entry.label }}
                </span>
                <span class="source-badge">{{ sourceBadgeLabel(entry) }}</span>
              </div>
              <div class="download-meta">
                <span v-if="entry.repo_id"><code>{{ entry.repo_id }}</code></span>
                <span v-if="entry.filename && entry.bundle !== 'directory'">/ {{ entry.filename }}</span>
                <span v-if="entry.bundle === 'directory'" class="dir-note">(whole repo)</span>
                <span v-if="entry.size_bytes" class="download-size">{{ formatFileSize(entry.size_bytes) }}</span>
              </div>
              <p v-if="entry.notes" class="download-notes">{{ entry.notes }}</p>
              <div class="download-actions">
                <span v-if="isAlreadyDownloaded(entry)" class="already-downloaded">
                  &#10003; Already downloaded
                </span>
                <template v-else>
                  <button
                    v-if="entryQueuedJob(entry)"
                    type="button"
                    class="btn-secondary"
                    disabled
                    :title="'Job ' + (entryQueuedJob(entry)?.job_id.slice(0, 8) ?? '')"
                  >
                    <span v-if="entryQueuedJob(entry)?.status === 'processing'">
                      Downloading{{ entryQueuedJob(entry)?.progress?.step ? ' ' + entryQueuedJob(entry)?.progress?.step + '%' : '...' }}
                    </span>
                    <span v-else>Queued</span>
                  </button>
                  <button
                    v-else
                    type="button"
                    class="btn-primary"
                    :disabled="!!inFlight[preset.id + '::' + entry.id]"
                    @click="downloadArchEntry(preset, entry)"
                  >
                    {{ inFlight[preset.id + '::' + entry.id] ? 'Starting...' : 'Download' }}
                  </button>
                </template>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>

    <!-- Source Tabs (Manual mode) -->
    <div v-if="mode === 'manual'" class="source-tabs">
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

    <form v-if="mode === 'manual'" @submit.prevent="submitDownload" class="download-form">
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
    </div>

    <!-- Right column: Recent Download Jobs (sticky) -->
    <aside class="downloads-aside">
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
    </aside>
    </div>
  </div>
</template>

<style scoped>
.downloads-page {
  max-width: 1400px;
  margin: 0 auto;
  padding: 24px;
}

/* Two-column layout: main content on the left, sticky Recent Downloads
   panel on the right. Collapses to single column on narrow viewports. */
.downloads-layout {
  display: grid;
  grid-template-columns: minmax(0, 1fr) 340px;
  gap: 20px;
  align-items: start;
}

.downloads-main {
  min-width: 0;
}

.downloads-aside {
  position: sticky;
  top: 20px;
  max-height: calc(100vh - 40px);
  overflow-y: auto;
}

.downloads-aside .recent-downloads {
  margin-top: 0;
}

@media (max-width: 960px) {
  .downloads-layout {
    grid-template-columns: 1fr;
  }
  .downloads-aside {
    position: static;
    max-height: none;
  }
}

h1 {
  margin: 0 0 8px;
  color: var(--text-primary);
}

.page-description {
  color: var(--text-secondary);
  margin-bottom: 24px;
}

.mode-tabs {
  display: flex;
  gap: 4px;
  margin-bottom: 20px;
  background: var(--bg-secondary);
  padding: 4px;
  border-radius: var(--border-radius);
}

.mode-tab {
  flex: 1;
  padding: 10px 16px;
  border: none;
  background: transparent;
  color: var(--text-secondary);
  cursor: pointer;
  border-radius: var(--border-radius-sm);
  font-size: 14px;
  font-weight: 600;
  transition: all var(--transition-fast);
}

.mode-tab:hover {
  color: var(--text-primary);
  background: var(--bg-hover);
}

.mode-tab.active {
  background: var(--accent-primary);
  color: #fff;
}

.catalog-filter {
  margin-bottom: 16px;
}

.catalog-filter input {
  width: 100%;
  padding: 10px 14px;
  border: 1px solid var(--border-color);
  border-radius: var(--border-radius-sm);
  background: var(--bg-secondary);
  color: var(--text-primary);
  font-size: 14px;
}

.arch-card {
  background: var(--bg-secondary);
  border: 1px solid var(--border-color);
  border-radius: var(--border-radius);
  padding: 20px;
  margin-bottom: 20px;
}

.arch-header {
  display: flex;
  align-items: baseline;
  gap: 10px;
  margin-bottom: 8px;
}

.arch-header h2 {
  margin: 0;
  color: var(--text-primary);
  font-size: 18px;
}

.arch-id {
  color: var(--text-secondary);
  font-size: 12px;
  background: var(--bg-hover);
  padding: 2px 6px;
  border-radius: var(--border-radius-sm);
}

.arch-description {
  color: var(--text-secondary);
  font-size: 13px;
  margin: 0 0 16px;
}

.component-group {
  margin-top: 14px;
}

.component-heading {
  font-size: 12px;
  text-transform: uppercase;
  letter-spacing: 0.5px;
  color: var(--text-secondary);
  margin: 0 0 8px;
}

.download-cards {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));
  gap: 10px;
}

.download-card {
  border: 1px solid var(--border-color);
  border-radius: var(--border-radius-sm);
  padding: 12px;
  background: var(--bg-primary);
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.download-card-head {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 8px;
}

.download-label {
  font-weight: 600;
  color: var(--text-primary);
  font-size: 14px;
  display: flex;
  align-items: center;
  gap: 6px;
}

.recommended-star {
  color: var(--accent-primary);
}

.source-badge {
  font-size: 11px;
  padding: 2px 8px;
  border-radius: 999px;
  background: var(--bg-hover);
  color: var(--text-secondary);
  font-weight: 500;
}

.download-meta {
  color: var(--text-secondary);
  font-size: 12px;
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
}

.download-meta code {
  background: var(--bg-hover);
  padding: 1px 5px;
  border-radius: 3px;
}

.dir-note {
  font-style: italic;
}

.download-size {
  margin-left: auto;
}

.download-notes {
  color: var(--text-secondary);
  font-size: 12px;
  margin: 4px 0 0;
}

.download-actions {
  margin-top: 6px;
}

.already-downloaded {
  color: var(--success);
  font-size: 13px;
  font-weight: 600;
  display: inline-flex;
  align-items: center;
  gap: 4px;
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
