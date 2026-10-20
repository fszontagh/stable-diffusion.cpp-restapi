<script setup lang="ts">
import { ref, computed, watch, onMounted } from 'vue'
import { useRoute } from 'vue-router'
import { api, type DownloadParams, type CivitAIModelInfo, type HuggingFaceModelInfo, type ArchitecturePreset, type ArchitectureDownload } from '../api/client'
import { useAppStore } from '../stores/app'
import Modal from '../components/Modal.vue'
import { findLocalFile } from '../composables/archReadiness'
import ArchLoadBar from '../components/ArchLoadBar.vue'

const appStore = useAppStore()

// Top-level mode. Manual keeps the existing URL/CivitAI/HF form.
// "architecture" reads the download catalog from data/model_architectures.json
// (via /architectures) and offers one card per canonical file.
const mode = ref<'manual' | 'architecture'>('architecture')

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

// /downloads?arch=<id> (from the Models page) opens that architecture.
const route = useRoute()
function openArchFromRoute() {
  const id = route.query.arch
  if (typeof id !== 'string' || !appStore.architectures?.architectures[id]) return
  mode.value = 'architecture'
  openArch(id)
}
watch(() => route.query.arch, openArchFromRoute)

onMounted(async () => {
  if (!appStore.architectures) {
    try { await appStore.fetchArchitectures() } catch { /* handled elsewhere */ }
  }
  openArchFromRoute()
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

function isAlreadyDownloaded(entry: ArchitectureDownload): boolean {
  return findLocalFile(entry, appStore.models) !== null
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

// Downloaded / queued / total tally for the collapsed arch summary line.
function archTally(preset: ArchitecturePreset): { done: number; queued: number; total: number } {
  const items = preset.downloads ?? []
  let done = 0
  let queued = 0
  for (const e of items) {
    if (isAlreadyDownloaded(e)) done++
    else if (entryQueuedJob(e)) queued++
  }
  return { done, queued, total: items.length }
}

// Architecture whose details modal is open. Opening it fetches the remote
// sizes of its entries so users see file sizes before they click Download.
// HF entries resolve via HEAD (Content-Length after the CDN redirect);
// bundles and non-HF sources fall through.
const openArchId = ref<string | null>(null)
const openPreset = computed<ArchitecturePreset | null>(() =>
  openArchId.value ? appStore.architectures?.architectures[openArchId.value] ?? null : null
)
function openArch(id: string) {
  openArchId.value = id
  const preset = appStore.architectures?.architectures[id]
  for (const entry of preset?.downloads ?? []) void fetchEntrySize(entry)
  void loadOptionLabels()
}

// Human labels for generation defaults and load options, from
// data/generation_options.json and data/load_options.json. Raw keys are
// shown until (or if) they fail to load.
const genLabels = ref<Record<string, string>>({})
const loadLabels = ref<Record<string, string>>({})
let labelsRequested = false
async function loadOptionLabels() {
  if (labelsRequested) return
  labelsRequested = true
  const toLabels = (opts: Record<string, { label?: string }> | undefined) =>
    Object.fromEntries(Object.entries(opts ?? {}).map(([k, v]) => [k, v.label ?? k]))
  try { genLabels.value = toLabels((await api.getGenerationOptionDescriptions()).options) } catch { /* raw keys */ }
  try {
    const desc = appStore.optionDescriptions ?? await appStore.fetchOptionDescriptions()
    loadLabels.value = toLabels(desc?.options)
  } catch { /* raw keys */ }
}

function formatSettingValue(v: unknown): string {
  if (typeof v === 'boolean') return v ? 'on' : 'off'
  if (Array.isArray(v)) return v.join(', ')
  if (v !== null && typeof v === 'object') return JSON.stringify(v)
  return String(v)
}

// Width and height are shown as one "1024 x 1024" row.
function settingRows(values: Record<string, unknown> | undefined, labels: Record<string, string>) {
  const rows: Array<{ key: string; label: string; value: string; full: string }> = []
  const v = values ?? {}
  for (const [key, raw] of Object.entries(v)) {
    if (key === 'height' && 'width' in v) continue
    let full = formatSettingValue(raw)
    let label = labels[key] ?? key
    if (key === 'width' && 'height' in v) {
      full = `${raw} x ${v.height}`
      label = 'Size'
    }
    const value = full.length > 60 ? full.slice(0, 57) + '...' : full
    rows.push({ key, label, value, full })
  }
  return rows
}

function componentSummary(preset: ArchitecturePreset): string {
  const keys = Object.keys(preset.requiredComponents ?? {})
  return keys.length ? keys.join(' · ') : 'checkpoint'
}

function defaultsSummary(preset: ArchitecturePreset): string {
  const g = preset.generationDefaults ?? {}
  const parts: string[] = []
  if (g.steps !== undefined) parts.push(`${g.steps} steps`)
  if (g.cfg_scale !== undefined) parts.push(`cfg ${g.cfg_scale}`)
  if (g.width && g.height) parts.push(`${g.width}x${g.height}`)
  return parts.join(' · ')
}

// Quant / precision tag read from the file name, e.g. "Q4_K", "bf16".
const QUANT_RE = /(?:^|[-_.])((?:i?q\d(?:_[a-z0-9]+)*)|bf16|fp16|f16|fp32|f32|fp8(?:_[a-z0-9]+)*|nf4)(?=[-_.]|$)/i
function isQuantized(entry: ArchitectureDownload): boolean {
  const f = (entry.filename ?? '').toLowerCase()
  return f.endsWith('.gguf') || /fp8|nf4/.test(f)
}
function isGated(entry: ArchitectureDownload): boolean {
  return /\bgated\b/i.test(entry.notes ?? '')
}
function formatBadge(entry: ArchitectureDownload): string {
  if (entry.bundle === 'directory') return 'full repo'
  const f = entry.filename?.split('/').pop() ?? ''
  const tag = QUANT_RE.exec(f.replace(/\.[a-z]+$/i, ''))?.[1]
  const gguf = f.toLowerCase().endsWith('.gguf')
  if (gguf) return tag ? `GGUF ${tag.toUpperCase()}` : 'GGUF'
  return tag ? tag.toLowerCase() : 'original'
}

// Page on the source site for the entry, so users can see the model card
// and license or fetch the file by hand.
function entryPageUrl(entry: ArchitectureDownload): string {
  if (entry.source === 'huggingface' && entry.repo_id) {
    const base = `https://huggingface.co/${entry.repo_id}`
    if (entry.bundle === 'directory' || !entry.filename) return base
    return `${base}/blob/${entry.revision ?? 'main'}/${entry.filename}`
  }
  if (entry.source === 'civitai' && entry.model_id) return `https://civitai.com/models/${entry.model_id}`
  return entry.url ?? ''
}
function entryPathLabel(entry: ArchitectureDownload): string {
  if (entry.source === 'huggingface' && entry.repo_id) {
    return entry.bundle === 'directory' || !entry.filename ? entry.repo_id : `${entry.repo_id}/${entry.filename}`
  }
  return entryPageUrl(entry)
}

// Per-entry remote size cache. Value shape: bytes = number (resolved),
// null = "we tried and got nothing", undefined key = not fetched yet.
const sizePreviews = ref<Map<string, number | null>>(new Map())
const sizeLoading = ref<Set<string>>(new Set())

function entryKey(entry: ArchitectureDownload): string {
  return `${entry.source}::${entry.repo_id ?? ''}::${entry.filename ?? ''}::${entry.url ?? ''}`
}

async function fetchEntrySize(entry: ArchitectureDownload): Promise<void> {
  if (entry.size_bytes) return                  // author-supplied wins
  if (entry.bundle === 'directory') return      // whole-repo bundles handled separately
  const key = entryKey(entry)
  if (sizePreviews.value.has(key) || sizeLoading.value.has(key)) return

  let url = ''
  if (entry.source === 'huggingface' && entry.repo_id && entry.filename) {
    const rev = entry.revision ?? 'main'
    url = `https://huggingface.co/${entry.repo_id}/resolve/${rev}/${entry.filename}`
  } else if (entry.source === 'url' && entry.url) {
    url = entry.url
  } else {
    return                                       // civitai needs its own API path; skip for now
  }

  sizeLoading.value = new Set(sizeLoading.value).add(key)
  try {
    // HEAD with redirect-following. HF actually returns Content-Length
    // on the resolve endpoint even before the 302 to the CDN, but
    // browsers automatically follow so we read the final response's
    // headers via fetch().
    const res = await fetch(url, { method: 'HEAD', mode: 'cors' })
    const raw = res.headers.get('x-linked-size') ?? res.headers.get('content-length')
    const bytes = raw ? parseInt(raw, 10) : NaN
    const next = new Map(sizePreviews.value)
    next.set(key, Number.isFinite(bytes) && bytes > 0 ? bytes : null)
    sizePreviews.value = next
  } catch {
    // CORS block, network error, etc. Record as null so we don't
    // hammer HF on every render.
    const next = new Map(sizePreviews.value)
    next.set(key, null)
    sizePreviews.value = next
  } finally {
    const next = new Set(sizeLoading.value)
    next.delete(key)
    sizeLoading.value = next
  }
}

function entrySize(entry: ArchitectureDownload): number | null {
  if (entry.size_bytes) return entry.size_bytes
  const v = sizePreviews.value.get(entryKey(entry))
  return v ?? null
}

function entrySizeLoading(entry: ArchitectureDownload): boolean {
  return sizeLoading.value.has(entryKey(entry))
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

      <div v-if="archsWithDownloads.length > 0" class="arch-toolbar">
        <span class="toolbar-count">{{ archsWithDownloads.length }} architectures</span>
      </div>

      <div v-if="archsWithDownloads.length > 0" class="arch-list">
        <button
          v-for="preset in archsWithDownloads"
          :key="preset.id"
          type="button"
          class="arch-item"
          @click="openArch(preset.id)"
        >
          <span class="arch-item-name">{{ preset.name }}</span>
          <span class="arch-item-tags">
            <span v-if="preset.videoModel" class="tag">video</span>
            <span v-if="preset.imageEditMode" class="tag">edit</span>
          </span>
          <span class="arch-item-comps">{{ componentSummary(preset) }}</span>
          <span class="arch-item-defaults">{{ defaultsSummary(preset) }}</span>
          <span
            :class="['arch-summary-tally', { 'all-done': archTally(preset).done === archTally(preset).total }]"
            :title="archTally(preset).done === archTally(preset).total ? 'Every listed file is already on disk' : ''"
          >
            <template v-if="archTally(preset).done === archTally(preset).total">&#10003; {{ archTally(preset).total }}/{{ archTally(preset).total }}</template>
            <template v-else>{{ archTally(preset).done }}/{{ archTally(preset).total }}<span v-if="archTally(preset).queued"> · {{ archTally(preset).queued }} queued</span></template>
          </span>
        </button>
      </div>

      <Modal
        :show="!!openPreset"
        :title="openPreset?.name ?? ''"
        wide
        @close="openArchId = null"
      >
        <div v-if="openPreset" class="arch-details">
          <p class="arch-id-line">
            <code class="arch-id">{{ openPreset.id }}</code>
            <span v-if="openPreset.videoModel" class="tag">video</span>
            <span v-if="openPreset.imageEditMode" class="tag" :title="'Image edit via ' + openPreset.imageEditMode">edit: {{ openPreset.imageEditMode === 'ref_images' ? 'reference image' : 'init image' }}</span>
          </p>
          <p v-if="openPreset.description" class="arch-description">{{ openPreset.description }}</p>

          <ArchLoadBar :preset="openPreset" class="arch-load-bar" target="models" @navigate="openArchId = null" />

          <div class="details-grid">
            <section v-if="settingRows(openPreset.generationDefaults, genLabels).length" class="details-section">
              <h4>Recommended settings</h4>
              <dl class="kv">
                <template v-for="row in settingRows(openPreset.generationDefaults, genLabels)" :key="row.key">
                  <dt :title="row.key">{{ row.label }}</dt>
                  <dd :title="row.full">{{ row.value }}</dd>
                </template>
              </dl>
            </section>

            <section v-if="settingRows(openPreset.loadOptions, loadLabels).length" class="details-section">
              <h4>Load options</h4>
              <dl class="kv">
                <template v-for="row in settingRows(openPreset.loadOptions, loadLabels)" :key="row.key">
                  <dt :title="row.key">{{ row.label }}</dt>
                  <dd :title="row.full">{{ row.value }}</dd>
                </template>
              </dl>
            </section>

            <section class="details-section">
              <h4>Components</h4>
              <dl class="kv">
                <template v-for="(desc, key) in openPreset.requiredComponents" :key="'r-' + key">
                  <dt>{{ key }} <span class="req">required</span></dt>
                  <dd>{{ desc }}</dd>
                </template>
                <template v-for="(desc, key) in openPreset.optionalComponents" :key="'o-' + key">
                  <dt>{{ key }} <span class="opt">optional</span></dt>
                  <dd>{{ desc }}</dd>
                </template>
                <template v-if="!Object.keys(openPreset.requiredComponents ?? {}).length && !Object.keys(openPreset.optionalComponents ?? {}).length">
                  <dt>-</dt>
                  <dd>Single checkpoint file, no extra components</dd>
                </template>
              </dl>
            </section>
          </div>

          <h4 class="downloads-heading">Downloads</h4>
          <table class="dl-table">
            <tbody
              v-for="group in groupByComponent(openPreset.downloads ?? [])"
              :key="group.component"
            >
              <tr class="dl-group">
                <th colspan="4">{{ group.component }}</th>
              </tr>
              <tr v-for="entry in group.items" :key="entry.id" class="dl-row">
                <td class="dl-name">
                  <div class="dl-label">
                    <span v-if="entry.recommended" class="recommended-star" title="Recommended">&#9733;</span>
                    {{ entry.label }}
                    <span :class="['format-badge', isQuantized(entry) ? 'quant' : 'original']">{{ formatBadge(entry) }}</span>
                    <span v-if="isGated(entry)" class="format-badge gated" title="Accept the license on the model page and set an HF token in Settings">gated</span>
                  </div>
                  <a
                    v-if="entryPageUrl(entry)"
                    :href="entryPageUrl(entry)"
                    target="_blank"
                    rel="noopener noreferrer"
                    class="dl-link"
                    :title="entryPageUrl(entry)"
                  >{{ entryPathLabel(entry) }}</a>
                  <p v-if="entry.notes" class="download-notes">{{ entry.notes }}</p>
                </td>
                <td class="dl-source"><span class="source-badge">{{ sourceBadgeLabel(entry) }}</span></td>
                <td class="dl-size">
                  <span v-if="entrySize(entry)" :title="entry.size_bytes ? 'From catalog' : 'Fetched from HuggingFace'">{{ formatFileSize(entrySize(entry)!) }}</span>
                  <span v-else-if="entrySizeLoading(entry)" class="text-muted">…</span>
                </td>
                <td class="dl-action">
                  <span v-if="isAlreadyDownloaded(entry)" class="chip chip-success">&#10003; On disk</span>
                  <button
                    v-else-if="entryQueuedJob(entry)"
                    type="button"
                    class="btn-secondary btn-sm"
                    disabled
                    :title="'Job ' + (entryQueuedJob(entry)?.job_id.slice(0, 8) ?? '')"
                  >
                    <span v-if="entryQueuedJob(entry)?.status === 'processing'">
                      {{ entryQueuedJob(entry)?.progress?.step ? entryQueuedJob(entry)?.progress?.step + '%' : 'Downloading' }}
                    </span>
                    <span v-else>Queued</span>
                  </button>
                  <button
                    v-else
                    type="button"
                    class="btn-primary btn-sm"
                    :disabled="!!inFlight[openPreset.id + '::' + entry.id]"
                    @click="downloadArchEntry(openPreset, entry)"
                  >
                    {{ inFlight[openPreset.id + '::' + entry.id] ? 'Starting...' : 'Download' }}
                  </button>
                </td>
              </tr>
            </tbody>
          </table>
        </div>
      </Modal>
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
            <div v-if="job.status === 'processing' && job.progress" class="progress-text" :title="job.progress.bytes_total ? formatFileSize(job.progress.bytes_done ?? 0) + ' / ' + formatFileSize(job.progress.bytes_total) : ''">
              {{ job.progress.bytes_total ? (Math.round((job.progress.bytes_done ?? 0) / job.progress.bytes_total * 100)) : job.progress.step }}%
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

.arch-toolbar {
  display: flex;
  align-items: center;
  gap: 6px;
  margin-bottom: 8px;
  padding: 0 4px;
  font-size: 12px;
  color: var(--text-secondary);
}


.toolbar-count {
  margin-left: auto;
}

.arch-list {
  display: flex;
  flex-direction: column;
  border: 1px solid var(--border-color);
  border-radius: var(--border-radius);
  background: var(--bg-secondary);
  overflow: hidden;
}

.arch-item {
  display: grid;
  grid-template-columns: minmax(140px, 1.3fr) 90px minmax(0, 1fr) minmax(0, 1.2fr) 90px;
  align-items: center;
  gap: 12px;
  padding: 8px 14px;
  border: none;
  border-bottom: 1px solid var(--border-color);
  background: transparent;
  color: var(--text-primary);
  font-size: 13px;
  text-align: left;
  cursor: pointer;
}

.arch-item:last-child {
  border-bottom: none;
}

.arch-item:hover {
  background: var(--bg-hover);
}

.arch-item-name {
  font-weight: 600;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.arch-item-tags {
  display: flex;
  gap: 4px;
}

.arch-item-comps,
.arch-item-defaults {
  color: var(--text-secondary);
  font-size: 12px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.tag {
  font-size: 11px;
  padding: 1px 7px;
  border-radius: 999px;
  background: var(--bg-hover);
  color: var(--text-secondary);
  white-space: nowrap;
}

.arch-summary-tally {
  font-size: 12px;
  color: var(--text-secondary);
  font-weight: 500;
  white-space: nowrap;
  text-align: right;
}

.arch-summary-tally.all-done {
  color: var(--accent-success);
}

@media (max-width: 720px) {
  .arch-item {
    grid-template-columns: minmax(0, 1fr) auto;
  }
  .arch-item-tags,
  .arch-item-comps,
  .arch-item-defaults {
    display: none;
  }
}

.arch-id-line {
  display: flex;
  align-items: center;
  gap: 6px;
  margin: 0 0 8px;
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

.arch-load-bar {
  margin-bottom: 14px;
}

.details-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(260px, 1fr));
  gap: 12px;
  margin-bottom: 18px;
}

.details-section {
  background: var(--bg-primary);
  border: 1px solid var(--border-color);
  border-radius: var(--border-radius-sm);
  padding: 10px 12px;
}

.details-section h4,
.downloads-heading {
  font-size: 12px;
  text-transform: uppercase;
  letter-spacing: 0.5px;
  color: var(--text-secondary);
  margin: 0 0 8px;
}

.kv {
  display: grid;
  grid-template-columns: auto minmax(0, 1fr);
  gap: 4px 12px;
  margin: 0;
  font-size: 13px;
}

.kv dt {
  color: var(--text-secondary);
  white-space: nowrap;
}

.kv dd {
  margin: 0;
  color: var(--text-primary);
  overflow-wrap: anywhere;
}

.kv .req,
.kv .opt {
  font-size: 10px;
  text-transform: uppercase;
  margin-left: 4px;
}

.kv .req {
  color: var(--accent-primary);
}

.kv .opt {
  color: var(--text-secondary);
}

.dl-table {
  width: 100%;
  border-collapse: collapse;
  font-size: 13px;
}

.dl-group th {
  text-align: left;
  font-size: 11px;
  text-transform: uppercase;
  letter-spacing: 0.5px;
  color: var(--text-secondary);
  padding: 12px 8px 4px;
  border-bottom: 1px solid var(--border-color);
}

.dl-row td {
  padding: 7px 8px;
  border-bottom: 1px solid var(--border-color);
  vertical-align: top;
}

.dl-label {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 6px;
  font-weight: 600;
  color: var(--text-primary);
}

.dl-link {
  display: block;
  font-size: 12px;
  color: var(--text-secondary);
  overflow-wrap: anywhere;
  text-decoration: none;
  margin-top: 2px;
}

.dl-link:hover {
  color: var(--accent-primary);
  text-decoration: underline;
}

.dl-source,
.dl-size,
.dl-action {
  white-space: nowrap;
  width: 1%;
}

.dl-size {
  color: var(--text-secondary);
  text-align: right;
}

.dl-action {
  text-align: right;
}

.format-badge {
  font-size: 10px;
  font-weight: 600;
  padding: 1px 6px;
  border-radius: 999px;
  white-space: nowrap;
}

.format-badge.original {
  background: rgba(34, 197, 94, 0.15);
  color: var(--accent-success);
}

.format-badge.gated {
  background: rgba(234, 179, 8, 0.18);
  color: #b45309;
}

.format-badge.quant {
  background: var(--bg-hover);
  color: var(--text-secondary);
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

.download-notes {
  color: var(--text-secondary);
  font-size: 12px;
  font-weight: 400;
  margin: 4px 0 0;
}

@media (max-width: 720px) {
  .dl-source {
    display: none;
  }
}

/* Was .already-downloaded; now uses .chip.chip-success from utilities.css. */

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
  border-color: var(--accent-success);
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
  color: var(--accent-success);
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
