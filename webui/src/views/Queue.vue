<script setup lang="ts">
import { ref, computed, watch, onMounted, onUnmounted } from 'vue'
import { useRouter } from 'vue-router'
import { useAppStore } from '../stores/app'
import { api, type Job, type LoadModelParams, type QueueFilters, type QueueDateGroup } from '../api/client'
import Modal from '../components/Modal.vue'
import ProgressBar from '../components/ProgressBar.vue'
import Lightbox from '../components/Lightbox.vue'
import SkeletonList from '../components/SkeletonList.vue'

const store = useAppStore()
const router = useRouter()

const selectedJob = ref<Job | null>(null)
const showDetailsModal = ref(false)
const cancelling = ref<string | null>(null)
const restarting = ref<string | null>(null)
const deleting = ref<string | null>(null)

// Filter state
const statusFilter = ref<string>('all')
const searchQuery = ref<string>('')
const searchDebounceTimer = ref<ReturnType<typeof setTimeout> | null>(null)
const isFetching = ref(false)

// View mode: 'pages' for numerical pagination, 'dates' for date grouping
const viewMode = ref<'pages' | 'dates'>('pages')

// Pagination state
const itemsPerPage = ref(20)
const currentPage = ref(1)

// Date groups for grouped view
const dateGroups = ref<QueueDateGroup[]>([])

// Model load confirmation modal
const showModelConfirmModal = ref(false)
const pendingRestartJob = ref<Job | null>(null)

// Selective-reload modal: lets the user choose which sections of a
// queue job to import into the Generate page (prompt, negative, settings,
// LoRAs, model, advanced). Each section row shows a truncated preview and
// a hover tooltip with the full value, so the user can verify what they're
// about to overwrite before committing.
//
// (The legacy "model differs, load it too?" follow-up modal was removed —
// the selector already shows the model name + components and a checkbox
// for the user to opt in to the model swap, so a second confirmation
// dialog was redundant.)
type ReloadSectionKey = 'prompt' | 'negative' | 'model' | 'settings' | 'loras' | 'advanced'
interface ReloadSection {
  key: ReloadSectionKey
  label: string
  preview: string     // truncated for display
  full: string        // full text shown via title= tooltip
  available: boolean  // false → don't render the row at all (no data on this job)
  selected: boolean
}
const showReloadSelectorModal = ref(false)
const reloadSelectorJob = ref<Job | null>(null)
const reloadSections = ref<ReloadSection[]>([])

// Lightbox state
const showLightbox = ref(false)
const lightboxImages = ref<string[]>([])
const lightboxIndex = ref(0)

// Upscale image selection modal
const showUpscaleSelectModal = ref(false)
const upscaleSelectJob = ref<Job | null>(null)

// Build current filters object
function getCurrentFilters(): QueueFilters {
  const filters: QueueFilters = {
    limit: itemsPerPage.value
  }

  if (viewMode.value === 'dates') {
    filters.group_by = 'date'
    filters.page = currentPage.value
  } else {
    filters.offset = (currentPage.value - 1) * itemsPerPage.value
  }

  if (statusFilter.value !== 'all') {
    filters.status = statusFilter.value
  }
  if (searchQuery.value.trim()) {
    filters.search = searchQuery.value.trim()
  }

  return filters
}

// Computed pagination info
const totalPages = computed(() => {
  if (store.queue?.total_pages) return store.queue.total_pages
  if (!store.queue?.total_count) return 1
  return Math.ceil(store.queue.total_count / itemsPerPage.value)
})

const hasNextPage = computed(() => store.queue?.has_more ?? false)
const hasPrevPage = computed(() => store.queue?.has_prev ?? currentPage.value > 1)

// Visible page numbers for smart pagination
const visiblePageNumbers = computed(() => {
  const total = totalPages.value
  const current = currentPage.value
  const pages: (number | 'ellipsis')[] = []

  if (total <= 7) {
    // Show all pages if 7 or less
    for (let i = 1; i <= total; i++) {
      pages.push(i)
    }
  } else {
    // Always show first page
    pages.push(1)

    if (current > 3) {
      pages.push('ellipsis')
    }

    // Show pages around current
    const start = Math.max(2, current - 1)
    const end = Math.min(total - 1, current + 1)

    for (let i = start; i <= end; i++) {
      pages.push(i)
    }

    if (current < total - 2) {
      pages.push('ellipsis')
    }

    // Always show last page
    pages.push(total)
  }

  return pages
})

// Pagination navigation
function goToPage(page: number) {
  if (page < 1 || page > totalPages.value) return
  currentPage.value = page
  fetchWithFilters()
  scrollToTop()
}

function nextPage() {
  if (hasNextPage.value) {
    currentPage.value++
    fetchWithFilters()
    scrollToTop()
  }
}

function prevPage() {
  if (hasPrevPage.value) {
    currentPage.value--
    fetchWithFilters()
    scrollToTop()
  }
}

function changeItemsPerPage(count: number) {
  itemsPerPage.value = count
  currentPage.value = 1  // Reset to first page
  fetchWithFilters()
  scrollToTop()
}

// Switch view mode
function setViewMode(mode: 'pages' | 'dates') {
  if (viewMode.value === mode) return
  viewMode.value = mode
  currentPage.value = 1  // Reset to first page
  fetchWithFilters()
}

// Fetch queue with current filters (prevents concurrent fetches)
async function fetchWithFilters() {
  if (isFetching.value) return
  isFetching.value = true
  try {
    await store.fetchQueue(getCurrentFilters())
    // Update date groups if in date mode
    if (viewMode.value === 'dates' && store.queue?.groups) {
      dateGroups.value = store.queue.groups
    }
  } finally {
    isFetching.value = false
  }
}

// Debounced search - reset to page 1 on search
function onSearchInput() {
  if (searchDebounceTimer.value) {
    clearTimeout(searchDebounceTimer.value)
  }
  searchDebounceTimer.value = setTimeout(() => {
    currentPage.value = 1
    fetchWithFilters()
  }, 300)
}

// Watch status filter changes (immediate) - reset to page 1
watch(statusFilter, () => {
  currentPage.value = 1
  fetchWithFilters()
})

// Sort jobs. Members of the same variation_group_id MUST stay adjacent
// regardless of their individual statuses, otherwise the group visually
// dissolves once jobs start completing at different times. The sort uses a
// per-job "effective key" that is the GROUP's leading status + most recent
// created_at when the job is grouped; standalone jobs use their own values.
//
// Status priorities: processing < pending < (failed | completed) < cancelled.
// A group bubbles up to the position of its most-active member.
const sortedJobs = computed(() => {
  if (!store.queue?.items) return []
  const items = [...store.queue.items]
  const statusOrder: Record<string, number> = { processing: 0, pending: 1, failed: 2, completed: 2, cancelled: 3, deleted: 4 }
  type Params = { variation_group_id?: string; variation_index?: number } | undefined

  // Pre-pass: collect (min status_order, max created_at) per group.
  const groupMeta = new Map<string, { order: number; created: number }>()
  for (const j of items) {
    const gid = (j.params as Params)?.variation_group_id
    if (!gid) continue
    const o = statusOrder[j.status] ?? 5
    const c = new Date(j.created_at).getTime()
    const prev = groupMeta.get(gid)
    if (!prev) {
      groupMeta.set(gid, { order: o, created: c })
    } else {
      if (o < prev.order) prev.order = o
      if (c > prev.created) prev.created = c
    }
  }

  function keyOf(j: import('../api/client').Job) {
    const gid = (j.params as Params)?.variation_group_id ?? ''
    if (gid && groupMeta.has(gid)) {
      const m = groupMeta.get(gid)!
      const idx = (j.params as Params)?.variation_index ?? 0
      return { order: m.order, created: m.created, gid, idx }
    }
    return {
      order: statusOrder[j.status] ?? 5,
      created: new Date(j.created_at).getTime(),
      gid: '',
      idx: 0,
    }
  }

  return items.sort((a, b) => {
    const ka = keyOf(a)
    const kb = keyOf(b)
    if (ka.order !== kb.order) return ka.order - kb.order
    // Stable secondary sort: newer effective-created first.
    if (ka.created !== kb.created) return kb.created - ka.created
    // Within the same group, ascending by variation_index.
    if (ka.gid !== kb.gid) return ka.gid.localeCompare(kb.gid)
    return ka.idx - kb.idx
  })
})

// Variation-group helpers. When a job was created via expand_prompt, its
// params carry variation_group_id / _index / _total / variation_template.
// We render consecutive same-group jobs under a sticky collapsible header
// and tint their left border with an accent so the group is visually
// obvious in the list.
const collapsedGroups = ref<Set<string>>(new Set())
function toggleGroup(groupId: string) {
  if (collapsedGroups.value.has(groupId)) collapsedGroups.value.delete(groupId)
  else collapsedGroups.value.add(groupId)
}
function getGroupId(j: import('../api/client').Job): string | null {
  const p = j.params as { variation_group_id?: string } | undefined
  return p?.variation_group_id || null
}
function getGroupTemplate(j: import('../api/client').Job): string {
  const p = j.params as { variation_template?: string } | undefined
  return p?.variation_template || ''
}
function getGroupTotal(j: import('../api/client').Job): number {
  const p = j.params as { variation_total?: number } | undefined
  return p?.variation_total || 0
}
// Returns true if this job is the FIRST occurrence of its group_id in the
// sorted list (i.e., where the group header should render). Now reliable
// because the new sort guarantees same-group adjacency.
function isGroupHead(job: import('../api/client').Job, idx: number): boolean {
  const gid = getGroupId(job)
  if (!gid) return false
  if (idx === 0) return true
  const prev = sortedJobs.value[idx - 1]
  return getGroupId(prev) !== gid
}
// Returns true if this job is the LAST occurrence of its group_id (so the
// container's bottom border + bottom-radius render here).
function isGroupTail(job: import('../api/client').Job, idx: number): boolean {
  const gid = getGroupId(job)
  if (!gid) return false
  if (idx === sortedJobs.value.length - 1) return true
  const next = sortedJobs.value[idx + 1]
  return getGroupId(next) !== gid
}
// Returns true if the job is in a group AND its group is currently collapsed
// AND the job is not the first member (the first remains visible to anchor
// the header).
function isHiddenByCollapse(job: import('../api/client').Job, idx: number): boolean {
  const gid = getGroupId(job)
  if (!gid || !collapsedGroups.value.has(gid)) return false
  return !isGroupHead(job, idx)
}

function clearFilters() {
  // Set searchQuery first (doesn't trigger fetch)
  searchQuery.value = ''
  // Then set statusFilter (triggers watch which fetches)
  statusFilter.value = 'all'
}

// Periodic refresh with filters (only when WebSocket is disconnected)
let refreshInterval: ReturnType<typeof setInterval> | null = null
const REFRESH_INTERVAL = 3000  // Poll every 3 seconds when WS is down

function startPollingIfNeeded() {
  // Only poll if WebSocket is not connected
  if (!store.wsConnected && !refreshInterval) {
    refreshInterval = setInterval(() => {
      // Double-check WS is still disconnected
      if (!store.wsConnected) {
        fetchWithFilters()
      }
    }, REFRESH_INTERVAL)
  }
}

function stopPolling() {
  if (refreshInterval) {
    clearInterval(refreshInterval)
    refreshInterval = null
  }
}

// Watch WebSocket connection state to start/stop polling
watch(() => store.wsConnected, (connected) => {
  if (connected) {
    // WebSocket connected - stop polling
    stopPolling()
  } else {
    // WebSocket disconnected - start fallback polling
    startPollingIfNeeded()
  }
})

onMounted(() => {
  fetchWithFilters()
  // Start polling only if WebSocket is not connected
  startPollingIfNeeded()
})

onUnmounted(() => {
  stopPolling()
  stopElapsedTick()
  if (searchDebounceTimer.value) {
    clearTimeout(searchDebounceTimer.value)
  }
  // Clear queue filters when leaving the page so polling uses no filters
  store.setQueueFilters(undefined)
})

// Live-ticking "now" used by processing jobs to show elapsed time before
// completed_at is set. Only ticks while at least one processing job exists in
// the queue, so an idle queue costs no timers.
//
// IMPORTANT: this block must precede the watch() below — the watch is set up
// with { immediate: true }, which fires its callback synchronously during
// setup. The callback calls startElapsedTick(), which reads
// elapsedTickInterval (a let). If the let isn't yet initialized at that
// point we'd hit a TDZ ReferenceError ("Cannot access 'X' before
// initialization"). Function declarations are hoisted, lets are not.
const nowMs = ref(Date.now())
let elapsedTickInterval: ReturnType<typeof setInterval> | null = null
function startElapsedTick() {
  if (elapsedTickInterval) return
  nowMs.value = Date.now()
  elapsedTickInterval = setInterval(() => { nowMs.value = Date.now() }, 1000)
}
function stopElapsedTick() {
  if (elapsedTickInterval) {
    clearInterval(elapsedTickInterval)
    elapsedTickInterval = null
  }
}

// Tick once per second only while at least one job is processing — idle queue
// has zero timer overhead, and the watcher fires on the next ws update when a
// job moves into 'processing'.
const hasProcessingJob = computed(() =>
  Boolean(store.queue?.items?.some(j => j.status === 'processing'))
)
watch(hasProcessingJob, (hasIt) => {
  if (hasIt) startElapsedTick(); else stopElapsedTick()
}, { immediate: true })

function formatDate(dateStr: string): string {
  const date = new Date(dateStr)
  return date.toLocaleString()
}

function formatRelativeTime(dateStr: string): string {
  const date = new Date(dateStr)
  const now = new Date()
  const diffMs = now.getTime() - date.getTime()
  const diffSec = Math.floor(diffMs / 1000)
  const diffMin = Math.floor(diffSec / 60)
  const diffHour = Math.floor(diffMin / 60)
  const diffDay = Math.floor(diffHour / 24)

  if (diffSec < 60) return 'just now'
  if (diffMin < 60) return `${diffMin}m ago`
  if (diffHour < 24) return `${diffHour}h ago`
  if (diffDay < 7) return `${diffDay}d ago`
  return date.toLocaleDateString()
}

function formatElapsed(startStr: string, endMs: number): string {
  const startMs = new Date(startStr).getTime()
  const diffSec = Math.max(0, Math.floor((endMs - startMs) / 1000))
  if (diffSec < 60) return `${diffSec}s`
  const min = Math.floor(diffSec / 60)
  const sec = diffSec % 60
  return `${min}m ${sec}s`
}

function formatDuration(startStr: string, endStr: string): string {
  return formatElapsed(startStr, new Date(endStr).getTime())
}

function isRefImagesJob(job: Job): boolean {
  if (job.type !== 'txt2img') return false
  const p = job.params
  if (!p) return false
  // Backend stores ref_images_count (number) instead of the actual base64 array
  if (typeof p.ref_images_count === 'number' && p.ref_images_count > 0) return true
  if (Array.isArray(p.ref_images) && (p.ref_images as unknown[]).length > 0) return true
  return false
}

// ControlNet + inpaint-mask detection for input thumbnails on the queue
// row. Both are persisted to <job_dir>/control.png and <job_dir>/mask.png
// by sd_wrapper.cpp at generation time — the presence of the base64 field
// in params is enough to know we should try to render the thumbnail.
function hasControlImage(job: Job): boolean {
  const p = job.params
  if (!p) return false
  return typeof p.control_image_base64 === 'string' && (p.control_image_base64 as string).length > 0
}
function hasMaskImage(job: Job): boolean {
  const p = job.params
  if (!p) return false
  return typeof p.mask_image_base64 === 'string' && (p.mask_image_base64 as string).length > 0
}

// Generation jobs produce image/video outputs that have thumbnails.
// Non-generation jobs (model_download, model_hash, convert) do not.
const GENERATION_JOB_TYPES = ['txt2img', 'img2img', 'txt2vid', 'upscale'] as const
function isGenerationJob(job: Job): boolean {
  return (GENERATION_JOB_TYPES as readonly string[]).includes(job.type)
}

// Larger glyph for non-generation jobs (used in the visual area in place of a thumbnail)
function getNonGenerationOutputIcon(type: string): string {
  const icons: Record<string, string> = {
    model_download: '&#128229;', // inbox tray
    model_hash: '&#35;',          // # hash mark
    convert: '&#128260;'          // counterclockwise arrows / convert
  }
  return icons[type] || '&#128190;' // generic floppy disk
}

function getTypeIcon(type: string, job?: Job): string {
  if (job && isRefImagesJob(job)) return '&#128247;'
  const icons: Record<string, string> = {
    txt2img: '&#127912;',
    img2img: '&#128247;',
    txt2vid: '&#127916;',
    upscale: '&#128269;',
    convert: '&#128259;',
    model_download: '&#11015;',
    model_hash: '&#128274;'
  }
  return icons[type] || '&#10067;'
}

function getTypeName(type: string, job?: Job): string {
  if (job && isRefImagesJob(job)) return 'Image Edit'
  const names: Record<string, string> = {
    txt2img: 'Text to Image',
    img2img: 'Image to Image',
    txt2vid: 'Text to Video',
    upscale: 'Upscale',
    convert: 'Model Conversion',
    model_download: 'Model Download',
    model_hash: 'Model Hash'
  }
  return names[type] || type
}

function truncateText(text: string, maxLength: number): string {
  if (!text) return ''
  if (text.length <= maxLength) return text
  return text.substring(0, maxLength) + '...'
}

function getJobPrompt(job: Job): string {
  return (job.params?.prompt as string) || ''
}

function getJobNegativePrompt(job: Job): string {
  return (job.params?.negative_prompt as string) || ''
}

function getJobDimensions(job: Job): string {
  const width = job.params?.width as number
  const height = job.params?.height as number
  if (width && height) return `${width}x${height}`
  return ''
}

function getJobSteps(job: Job): number | null {
  return (job.params?.steps as number) || null
}

function getJobSeed(job: Job): number | null {
  return (job.params?.seed as number) || null
}

function getJobSampler(job: Job): string {
  return (job.params?.sampler as string) || ''
}

function getJobScheduler(job: Job): string {
  return (job.params?.scheduler as string) || ''
}

function getJobBatchCount(job: Job): number {
  return (job.params?.batch_count as number) || 1
}

// Format component name to short display name
function formatComponentName(path: string): string {
  if (!path) return ''
  const filename = path.split('/').pop() || path
  // Remove extension
  return filename.replace(/\.(safetensors|ckpt|pt|bin|gguf)$/i, '')
}

// Get job model components as array of {label, value} pairs
function getJobModelComponents(job: Job): Array<{label: string, value: string}> {
  const components: Array<{label: string, value: string}> = []
  const loaded = job.model_settings?.loaded_components
  if (!loaded) return components

  if (loaded.vae) components.push({ label: 'VAE', value: formatComponentName(loaded.vae) })
  if (loaded.clip_l) components.push({ label: 'CLIP-L', value: formatComponentName(loaded.clip_l) })
  if (loaded.clip_g) components.push({ label: 'CLIP-G', value: formatComponentName(loaded.clip_g) })
  if (loaded.t5xxl) components.push({ label: 'T5', value: formatComponentName(loaded.t5xxl) })
  if (loaded.llm) components.push({ label: 'LLM', value: formatComponentName(loaded.llm) })
  if (loaded.controlnet) components.push({ label: 'CN', value: formatComponentName(loaded.controlnet) })

  return components
}

// Check if job is a convert job
function isConvertJob(job: Job): boolean {
  return job.type === 'convert'
}

// Get convert job input path (source model)
function getConvertInput(job: Job): string {
  return (job.params?.input_path as string) || ''
}

// Get convert job output path (destination file)
function getConvertOutput(job: Job): string {
  return (job.params?.output_path as string) || ''
}

// Get convert job output type (quantization type like q5_0, q8_0, f16)
function getConvertType(job: Job): string {
  return (job.params?.output_type as string) || ''
}

// Extract file extension from a path (returns ".safetensors", ".gguf", etc.,
// or empty string if no extension or unrecognized).
function getFileExt(path: string): string {
  if (!path) return ''
  const filename = path.split('/').pop() || path
  const m = filename.match(/\.(safetensors|ckpt|pt|bin|gguf|pth)$/i)
  return m ? '.' + m[1].toLowerCase() : ''
}

function getConvertInputExt(job: Job): string {
  return getFileExt(getConvertInput(job))
}

function getConvertOutputExt(job: Job): string {
  return getFileExt(getConvertOutput(job))
}

// Scroll to top of page
function scrollToTop() {
  window.scrollTo({ top: 0, behavior: 'smooth' })
}

function viewDetails(job: Job) {
  selectedJob.value = job
  showDetailsModal.value = true
}

async function cancelJob(jobId: string) {
  cancelling.value = jobId
  try {
    await api.cancelJob(jobId)
    store.showToast('Job cancelled', 'success')
    store.fetchQueue()
  } catch (e) {
    store.showToast(e instanceof Error ? e.message : 'Failed to cancel job', 'error')
  } finally {
    cancelling.value = null
  }
}

async function deleteJob(jobId: string) {
  deleting.value = jobId
  try {
    await api.deleteJobs([jobId])
    // Optimistically remove from local state for instant UI feedback
    if (store.queue?.items) {
      store.queue.items = store.queue.items.filter(j => j.job_id !== jobId)
    }
    store.showToast('Job deleted', 'success')
    await store.fetchQueue()
  } catch (e) {
    store.showToast(e instanceof Error ? e.message : 'Failed to delete job', 'error')
  } finally {
    deleting.value = null
  }
}

function getOutputUrl(path: string): string {
  return `/output/${path}`
}

function getThumbUrl(path: string): string {
  return `/thumb/${path}`
}

function openLightbox(outputs: string[], index: number) {
  lightboxImages.value = outputs.map(getOutputUrl)
  lightboxIndex.value = index
  showLightbox.value = true
}

function openPreviewLightbox(previewImage: string) {
  lightboxImages.value = [previewImage]
  lightboxIndex.value = 0
  showLightbox.value = true
}

// Check if job's model differs from currently loaded model
function isModelDifferent(job: Job): boolean {
  if (!job.model_settings) return false
  const jobModel = job.model_settings.model_name
  const currentModel = store.modelName
  return jobModel !== currentModel
}

// Reload click handler — opens the SELECTOR modal first. The user picks
// which sections of the job to import, then we either go straight to
// /generate (no model change needed) or fall through to the existing
// "model differs, load it too?" modal when 'model' was in the selection.
function reloadSettings(job: Job) {
  if (!job.params) {
    store.showToast('Job has no parameters to reload', 'warning')
    return
  }
  reloadSelectorJob.value = job
  reloadSections.value = buildReloadSections(job)
  showReloadSelectorModal.value = true
}

// Build the section list for a given job. Skipped (available:false)
// sections aren't rendered. Truncates previews to keep the modal compact;
// the full text is exposed via title= for hover.
function buildReloadSections(job: Job): ReloadSection[] {
  const p = (job.params ?? {}) as Record<string, unknown>
  const truncate = (s: string, n: number) =>
    s.length > n ? s.slice(0, n).trimEnd() + '…' : s
  const out: ReloadSection[] = []

  // Prompt
  const promptStr = typeof p.prompt === 'string' ? p.prompt : ''
  out.push({
    key: 'prompt',
    label: 'Prompt',
    preview: promptStr ? truncate(promptStr, 70) : '(empty)',
    full: promptStr || '(empty)',
    available: !!promptStr,
    selected: !!promptStr,
  })

  // Negative prompt
  const negStr = typeof p.negative_prompt === 'string' ? p.negative_prompt : ''
  out.push({
    key: 'negative',
    label: 'Negative prompt',
    preview: negStr ? truncate(negStr, 70) : '(none)',
    full: negStr || '(none)',
    available: !!negStr,
    selected: !!negStr,
  })

  // Model + components — only meaningful if the job recorded them and the
  // current loaded model differs (else there's nothing to reload).
  if (job.model_settings?.model_name) {
    const m = job.model_settings
    const modelName = m.model_name as string  // narrowed by the if above
    const compNames: string[] = []
    const lc = m.loaded_components || {}
    if (lc.vae) compNames.push('VAE')
    if (lc.clip_l) compNames.push('CLIP-L')
    if (lc.clip_g) compNames.push('CLIP-G')
    if (lc.t5xxl) compNames.push('T5')
    if (lc.controlnet) compNames.push('CN')
    if (lc.llm) compNames.push('LLM')
    const fullLines = [
      `Model: ${modelName}`,
      m.model_architecture ? `Architecture: ${m.model_architecture}` : '',
      lc.vae ? `VAE: ${lc.vae}` : '',
      lc.clip_l ? `CLIP-L: ${lc.clip_l}` : '',
      lc.clip_g ? `CLIP-G: ${lc.clip_g}` : '',
      lc.t5xxl ? `T5-XXL: ${lc.t5xxl}` : '',
      lc.controlnet ? `ControlNet: ${lc.controlnet}` : '',
      lc.llm ? `LLM: ${lc.llm}` : '',
    ].filter(Boolean)
    out.push({
      key: 'model',
      label: 'Model & components',
      preview: formatComponentName(modelName) + (compNames.length ? ` (+${compNames.join(', ')})` : ''),
      full: fullLines.join('\n'),
      available: true,
      selected: isModelDifferent(job),
    })
  }

  // Generation settings — show the most-relevant subset
  const w = p.width as number | undefined
  const h = p.height as number | undefined
  const stepsP = p.steps as number | undefined
  const cfg = p.cfg_scale as number | undefined
  const samp = p.sampler as string | undefined
  const sched = p.scheduler as string | undefined
  const sd = p.seed as number | undefined
  const bc = p.batch_count as number | undefined
  const previewParts: string[] = []
  if (w && h) previewParts.push(`${w}×${h}`)
  if (stepsP !== undefined) previewParts.push(`${stepsP} steps`)
  if (cfg !== undefined) previewParts.push(`cfg ${cfg}`)
  if (samp) previewParts.push(samp)
  const settingsAvailable = previewParts.length > 0
  out.push({
    key: 'settings',
    label: 'Generation settings',
    preview: settingsAvailable ? previewParts.join(' · ') : '(none)',
    full: [
      w && h ? `Resolution: ${w}×${h}` : '',
      stepsP !== undefined ? `Steps: ${stepsP}` : '',
      cfg !== undefined ? `CFG scale: ${cfg}` : '',
      samp ? `Sampler: ${samp}` : '',
      sched ? `Scheduler: ${sched}` : '',
      sd !== undefined ? `Seed: ${sd}` : '',
      bc !== undefined && bc > 1 ? `Batch count: ${bc}` : '',
    ].filter(Boolean).join('\n'),
    available: settingsAvailable,
    selected: settingsAvailable,
  })

  // LoRAs — extract <lora:name:weight> tags from prompt + negative
  const loraRegex = /<lora:([^:>]+):([0-9.]+)>/g
  const loraNames: string[] = []
  const collect = (s: string) => {
    let m: RegExpExecArray | null
    while ((m = loraRegex.exec(s)) !== null) {
      const name = m[1]
      const w = m[2]
      loraNames.push(`${name}:${w}`)
    }
  }
  collect(promptStr)
  collect(negStr)
  if (loraNames.length > 0) {
    const previewN = loraNames.length > 2
      ? `${loraNames.slice(0, 2).join(', ')}, +${loraNames.length - 2}`
      : loraNames.join(', ')
    out.push({
      key: 'loras',
      label: `LoRAs (${loraNames.length})`,
      preview: previewN,
      full: loraNames.join('\n'),
      available: true,
      selected: true,
    })
  }

  // Advanced — surface anything non-default we know about
  const advLines: string[] = []
  const slg = p.slg_scale as number | undefined
  if (slg !== undefined && slg > 0) advLines.push(`SLG scale: ${slg}`)
  const cm = p.cache_mode as string | undefined
  if (cm) advLines.push(`Cache mode: ${cm}`)
  if (p.vae_tiling) advLines.push('VAE tiling: on')
  const cs = p.control_strength as number | undefined
  if (cs !== undefined) advLines.push(`ControlNet strength: ${cs}`)
  const dg = p.distilled_guidance as number | undefined
  if (dg !== undefined) advLines.push(`Distilled guidance: ${dg}`)
  const vf = p.video_frames as number | undefined
  if (vf) advLines.push(`Video frames: ${vf}`)
  const fps = p.fps as number | undefined
  if (fps) advLines.push(`FPS: ${fps}`)
  const fs = p.flow_shift as number | undefined
  if (fs !== undefined) advLines.push(`Flow shift: ${fs}`)
  const strn = p.strength as number | undefined
  if (strn !== undefined && job.type === 'img2img') advLines.push(`img2img strength: ${strn}`)
  if (advLines.length > 0) {
    out.push({
      key: 'advanced',
      label: 'Advanced',
      preview: advLines.length === 1 ? advLines[0] : `${advLines.length} options`,
      full: advLines.join('\n'),
      available: true,
      selected: false,  // off by default — user usually wants the basics
    })
  }

  return out.filter(s => s.available)
}

// Selected-count for the Apply button label.
function selectedReloadCount(): number {
  return reloadSections.value.filter(s => s.selected).length
}

function reloadSelectAll() {
  for (const s of reloadSections.value) s.selected = true
}
function reloadSelectNone() {
  for (const s of reloadSections.value) s.selected = false
}

// Apply the user's selection. The selector modal already shows the model
// name + components and the "model differs" highlight, so checking the
// "Model & components" box IS the user's confirmation — we kick the model
// load directly here without a second dialog. Navigation to /generate is
// non-blocking (model load runs in the background; the status bar shows
// progress).
function applyReloadSelection() {
  const job = reloadSelectorJob.value
  if (!job) return

  const selected = reloadSections.value.filter(s => s.selected)
  if (selected.length === 0) {
    store.showToast('Select at least one section to reload', 'warning')
    return
  }
  const keys = selected.map(s => s.key)
  const wantsModel = keys.includes('model')

  // Strip 'model' from the keys passed to Generate.vue — model loading is
  // a backend operation, not a Generate-form section.
  const generateSections = keys.filter(k => k !== 'model')

  // Close selector modal first.
  showReloadSelectorModal.value = false
  reloadSelectorJob.value = null

  // Navigate immediately so the user sees the form populate. If 'model'
  // was checked AND the loaded model differs, kick the load in the
  // background — the StatusBar progress bar tracks it from there.
  navigateWithSettings(job, generateSections)
  if (wantsModel && isModelDifferent(job) && job.model_settings?.model_name) {
    void loadModelInBackground(job)
  }
}

function cancelReloadSelector() {
  showReloadSelectorModal.value = false
  reloadSelectorJob.value = null
  reloadSections.value = []
}

// Background model swap triggered by the "Model & components" checkbox.
// Compatibility shim for job snapshots whose load_options predate the fields
// the current backend expects. Two failure modes this fixes:
//
// 1. **Pre-#11a4228 snapshots** were captured before loaded_options_ echoed
//    `backend` / `params_backend` / `rpc_servers`. Reloading on Z-Image bf16
//    (or anything that ran with CPU-side params) hits OOM because
//    stream_layers=true but params_backend is missing.
//
// 2. **Pre-alignment snapshots** carry legacy fields the strict backend
//    allowlist now rejects (`keep_clip_on_cpu`, `offload_to_cpu`,
//    `vae_decode_only`, the whole experimental-offload set, etc.). Sending
//    them as-is would 400.
//
// Translates legacy bool flags to the new backend-spec strings (mirroring
// sd-cli's prepare_backend_assignments), applies the stream_layers →
// params_backend="*=cpu" auto-fill rule from ModelLoad.vue, and strips dead
// keys before handing the result to /models/load.
function normalizeLegacyLoadOptions(input: unknown): LoadModelParams['options'] {
  if (!input || typeof input !== 'object') return {}
  const opts: Record<string, unknown> = { ...(input as Record<string, unknown>) }

  // Legacy offload_to_cpu → params_backend="*=cpu"
  if (opts.offload_to_cpu === true && !opts.params_backend) {
    opts.params_backend = '*=cpu'
  }

  // Legacy keep_*_on_cpu → backend "te=cpu,vae=cpu,controlnet=cpu" (prepended,
  // preserving anything the snapshot already had in `backend`).
  const backendParts: string[] = []
  if (typeof opts.backend === 'string' && opts.backend) backendParts.push(opts.backend as string)
  if (opts.keep_clip_on_cpu === true && !backendParts.some((p) => /\bte=cpu\b/.test(p))) backendParts.push('te=cpu')
  if (opts.keep_vae_on_cpu === true && !backendParts.some((p) => /\bvae=cpu\b/.test(p))) backendParts.push('vae=cpu')
  if (opts.keep_controlnet_on_cpu === true && !backendParts.some((p) => /\bcontrolnet=cpu\b/.test(p))) backendParts.push('controlnet=cpu')
  if (backendParts.length > 0) opts.backend = backendParts.join(',')

  // stream_layers=true without params_backend → fill with "*=cpu" (matches
  // the in-form auto-watcher in ModelLoad.vue).
  if (opts.stream_layers === true && !opts.params_backend) {
    opts.params_backend = '*=cpu'
  }

  // Strip every dead/legacy field that the strict backend allowlist rejects.
  const dead = [
    'keep_clip_on_cpu', 'keep_vae_on_cpu', 'keep_controlnet_on_cpu',
    'offload_to_cpu', 'vae_decode_only', 'free_params_immediately',
    'flow_shift', 'vae_tiling', 'vae_tile_size_x', 'vae_tile_size_y', 'vae_tile_overlap',
    'offload_mode', 'vram_estimation',
    'offload_cond_stage', 'offload_diffusion',
    'reload_cond_stage', 'reload_diffusion',
    'log_offload_events', 'min_offload_size_mb', 'target_free_vram_mb',
    'streaming_prefetch_layers', 'streaming_keep_layers_behind', 'streaming_min_free_vram_mb',
  ]
  for (const k of dead) delete opts[k]

  return opts as LoadModelParams['options']
}

// Reads the job's recorded components + load_options and hands them to
// /models/load. Failures surface as a toast — the form is already
// populated from navigateWithSettings, so the user can retry the load
// from the ModelLoad page if needed.
async function loadModelInBackground(job: Job) {
  const settings = job.model_settings
  if (!settings?.model_name) return

  try {
    if (store.modelLoaded && store.modelName !== settings.model_name) {
      store.showToast('Unloading current model...', 'info')
      await api.unloadModel()
    }

    store.showToast(`Loading model: ${settings.model_name}...`, 'info')

    const loadParams: LoadModelParams = {
      model_name: settings.model_name,
      model_type: settings.model_type === 'diffusion' ? 'diffusion' : 'checkpoint',
    }
    if (settings.loaded_components) {
      const lc = settings.loaded_components
      if (lc.vae) loadParams.vae = lc.vae
      if (lc.clip_l) loadParams.clip_l = lc.clip_l
      if (lc.clip_g) loadParams.clip_g = lc.clip_g
      if (lc.t5xxl) loadParams.t5xxl = lc.t5xxl
      if (lc.controlnet) loadParams.controlnet = lc.controlnet
      if (lc.llm) loadParams.llm = lc.llm
      if (lc.llm_vision) loadParams.llm_vision = lc.llm_vision
    }
    if (settings.load_options) {
      loadParams.options = normalizeLegacyLoadOptions(settings.load_options)
    }

    api.loadModel(loadParams).then(() => {
      store.showToast('Model loaded successfully', 'success')
    }).catch((e) => {
      store.showToast(e instanceof Error ? e.message : 'Failed to load model', 'error')
    })
  } catch (e) {
    store.showToast(e instanceof Error ? e.message : 'Failed to start model loading', 'error')
  }
}

// Navigate to Generate with job settings + optional section selection.
function navigateWithSettings(job: Job, sections?: string[]) {
  const payload: Record<string, unknown> = {
    type: job.type,
    params: job.params,
    job_id: job.job_id,
  }
  if (sections && sections.length > 0) {
    payload.sections = sections
  }
  sessionStorage.setItem('reloadJobParams', JSON.stringify(payload))

  router.push('/generate')
  const label = sections && sections.length > 0
    ? `Loaded ${sections.length} section${sections.length > 1 ? 's' : ''} from job`
    : 'Settings loaded into Generate form'
  store.showToast(label, 'success')
}

// Restart a job - check model first
async function handleRestartJob(job: Job) {
  if (!job.params) {
    store.showToast('Job has no parameters to restart', 'warning')
    return
  }

  // Check if model is different
  if (isModelDifferent(job)) {
    pendingRestartJob.value = job
    showModelConfirmModal.value = true
    return
  }

  // Check if model is loaded at all
  if (!store.modelLoaded) {
    store.showToast('No model loaded. Please load a model first.', 'warning')
    return
  }

  // Restart directly
  await restartJobDirectly(job)
}

// Restart job directly without model check
async function restartJobDirectly(job: Job) {
  restarting.value = job.job_id
  try {
    const result = await api.restartJob(job)
    store.showToast(`Job restarted: ${result.job_id}`, 'success')
    store.fetchQueue()
  } catch (e) {
    store.showToast(e instanceof Error ? e.message : 'Failed to restart job', 'error')
  } finally {
    restarting.value = null
  }
}

// Load the model from job settings and then restart
async function loadModelAndRestart() {
  const job = pendingRestartJob.value
  if (!job?.model_settings) return

  showModelConfirmModal.value = false

  const settings = job.model_settings
  if (!settings.model_name) {
    store.showToast('Job has no model information', 'error')
    return
  }

  try {
    store.showToast(`Loading model: ${settings.model_name}...`, 'info')

    // Build load params from job's model settings
    const loadParams: LoadModelParams = {
      model_name: settings.model_name,
      model_type: settings.model_type === 'diffusion' ? 'diffusion' : 'checkpoint'
    }

    // Add components if they were used
    if (settings.loaded_components) {
      if (settings.loaded_components.vae) loadParams.vae = settings.loaded_components.vae
      if (settings.loaded_components.clip_l) loadParams.clip_l = settings.loaded_components.clip_l
      if (settings.loaded_components.clip_g) loadParams.clip_g = settings.loaded_components.clip_g
      if (settings.loaded_components.t5xxl) loadParams.t5xxl = settings.loaded_components.t5xxl
      if (settings.loaded_components.controlnet) loadParams.controlnet = settings.loaded_components.controlnet
      if (settings.loaded_components.llm) loadParams.llm = settings.loaded_components.llm
      if (settings.loaded_components.llm_vision) loadParams.llm_vision = settings.loaded_components.llm_vision
    }

    // Add load options if stored (critical for correct VRAM usage and behavior).
    // Run them through the legacy-compat normalizer so pre-#11a4228 snapshots
    // (which lack backend/params_backend) still reload correctly.
    if (settings.load_options) {
      loadParams.options = normalizeLegacyLoadOptions(settings.load_options)
    }

    await api.loadModel(loadParams)
    store.showToast('Model loaded successfully', 'success')

    // Now restart the job
    await restartJobDirectly(job)
  } catch (e) {
    store.showToast(e instanceof Error ? e.message : 'Failed to load model', 'error')
  } finally {
    pendingRestartJob.value = null
  }
}

// Restart without loading model (use current model)
async function restartWithCurrentModel() {
  const job = pendingRestartJob.value
  showModelConfirmModal.value = false
  pendingRestartJob.value = null

  if (job) {
    if (!store.modelLoaded) {
      store.showToast('No model loaded. Please load a model first.', 'warning')
      return
    }
    await restartJobDirectly(job)
  }
}

// Cancel model load dialog
function cancelModelLoad() {
  showModelConfirmModal.value = false
  pendingRestartJob.value = null
}

// Use job output for upscaling
function useForUpscale(job: Job) {
  if (!job.outputs || job.outputs.length === 0) {
    store.showToast('Job has no output images', 'warning')
    return
  }

  // If multiple images, show selection modal
  if (job.outputs.length > 1) {
    upscaleSelectJob.value = job
    showUpscaleSelectModal.value = true
    return
  }

  // Single image - use it directly
  sendImageToUpscale(job.outputs[0])
}

// Send selected image to upscale page
async function sendImageToUpscale(outputPath: string) {
  try {
    const imageUrl = getOutputUrl(outputPath)
    const response = await fetch(imageUrl)
    if (!response.ok) {
      throw new Error('Failed to fetch image')
    }
    const blob = await response.blob()
    const base64DataUrl = await new Promise<string>((resolve, reject) => {
      const reader = new FileReader()
      reader.onloadend = () => resolve(reader.result as string)
      reader.onerror = reject
      reader.readAsDataURL(blob)
    })

    // Store the image in the app store so it persists across navigation
    store.setUpscaleInputImage(base64DataUrl)

    // Close modal if open
    showUpscaleSelectModal.value = false
    upscaleSelectJob.value = null

    // Navigate to upscale page
    router.push('/upscale')
    store.showToast('Image loaded for upscaling', 'success')
  } catch (e) {
    store.showToast(e instanceof Error ? e.message : 'Failed to load image', 'error')
  }
}
</script>

<template>
  <div class="queue">
    <div class="page-header">
      <h1 class="page-title">Queue</h1>
      <p class="page-subtitle">Monitor generation jobs and view results</p>
    </div>

    <!-- Stats -->
    <div class="stats-grid">
      <button class="stat-card stat-clickable" :class="{ 'stat-active': statusFilter === 'pending' }" @click="statusFilter = statusFilter === 'pending' ? 'all' : 'pending'">
        <div class="stat-value" style="color: var(--accent-warning)">{{ store.queueStats.pending }}</div>
        <div class="stat-label">Pending</div>
      </button>
      <button class="stat-card stat-clickable" :class="{ 'stat-active': statusFilter === 'processing' }" @click="statusFilter = statusFilter === 'processing' ? 'all' : 'processing'">
        <div class="stat-value" style="color: var(--accent-primary)">{{ store.queueStats.processing }}</div>
        <div class="stat-label">Processing</div>
      </button>
      <button class="stat-card stat-clickable" :class="{ 'stat-active': statusFilter === 'completed' }" @click="statusFilter = statusFilter === 'completed' ? 'all' : 'completed'">
        <div class="stat-value" style="color: var(--accent-success)">{{ store.queueStats.completed }}</div>
        <div class="stat-label">Completed</div>
      </button>
      <button class="stat-card stat-clickable" :class="{ 'stat-active': statusFilter === 'failed' }" @click="statusFilter = statusFilter === 'failed' ? 'all' : 'failed'">
        <div class="stat-value" style="color: var(--accent-error)">{{ store.queueStats.failed }}</div>
        <div class="stat-label">Failed</div>
      </button>
    </div>

    <!-- Filters -->
    <div class="filters-bar">
      <div class="filter-group">
        <label class="filter-label">Status</label>
        <select v-model="statusFilter" class="filter-select">
          <option value="all">All</option>
          <option value="pending">Pending</option>
          <option value="processing">Processing</option>
          <option value="completed">Completed</option>
          <option value="failed">Failed</option>
          <option value="cancelled">Cancelled</option>
        </select>
      </div>
      <div class="filter-group filter-search">
        <label class="filter-label">Search</label>
        <input
          v-model="searchQuery"
          @input="onSearchInput"
          type="text"
          class="filter-input"
          placeholder="Search in prompts..."
        />
      </div>
      <div class="filter-group">
        <label class="filter-label">View</label>
        <div class="view-mode-toggle">
          <button
            class="view-mode-btn"
            :class="{ active: viewMode === 'pages' }"
            @click="setViewMode('pages')"
            title="Numerical pagination"
          >
            <span class="view-icon">&#128203;</span> Pages
          </button>
          <button
            class="view-mode-btn"
            :class="{ active: viewMode === 'dates' }"
            @click="setViewMode('dates')"
            title="Group by date"
          >
            <span class="view-icon">&#128197;</span> Dates
          </button>
        </div>
      </div>
      <button
        v-if="statusFilter !== 'all' || searchQuery"
        class="btn btn-secondary btn-sm filter-clear"
        @click="clearFilters"
      >
        Clear Filters
      </button>
      <div class="filter-results">
        {{ store.queue?.filtered_count ?? store.queue?.total_count ?? sortedJobs.length }} of {{ store.queue?.total_count || 0 }} jobs
      </div>
    </div>

    <!-- Top Pagination -->
    <div v-if="store.queue && store.queue.total_count > itemsPerPage" class="pagination-bar pagination-top">
      <div class="pagination-info">
        <template v-if="viewMode === 'pages'">
          Showing {{ (currentPage - 1) * itemsPerPage + 1 }}-{{ Math.min(currentPage * itemsPerPage, store.queue.total_count) }}
          of {{ store.queue.total_count }} jobs
        </template>
        <template v-else>
          Page {{ currentPage }} of {{ totalPages }} ({{ store.queue.total_count }} jobs)
        </template>
      </div>

      <div class="pagination-controls">
        <button
          class="btn btn-sm btn-secondary"
          :disabled="!hasPrevPage"
          @click="prevPage"
        >
          &laquo; Prev
        </button>

        <div class="page-numbers">
          <template v-for="item in visiblePageNumbers" :key="item">
            <span v-if="item === 'ellipsis'" class="page-ellipsis">...</span>
            <button
              v-else
              class="btn btn-sm"
              :class="currentPage === item ? 'btn-primary' : 'btn-secondary'"
              @click="goToPage(item)"
            >
              {{ item }}
            </button>
          </template>
        </div>

        <button
          class="btn btn-sm btn-secondary"
          :disabled="!hasNextPage"
          @click="nextPage"
        >
          Next &raquo;
        </button>
      </div>

      <div class="pagination-options">
        <label class="per-page-label">Per page:</label>
        <select class="per-page-select" :value="itemsPerPage" @change="changeItemsPerPage(Number(($event.target as HTMLSelectElement).value))">
          <option :value="10">10</option>
          <option :value="20">20</option>
          <option :value="50">50</option>
          <option :value="100">100</option>
        </select>
      </div>
    </div>

    <!-- Loading skeleton -->
    <SkeletonList v-if="isFetching && !store.queue" :count="5" />

    <!-- Jobs List - Pages View -->
    <TransitionGroup v-else-if="viewMode === 'pages'" name="job-list" tag="div" class="jobs-list">
      <template v-for="(job, idx) in sortedJobs" :key="job.job_id">
        <!-- Variation-group header rendered once before the first member.
             Click toggles collapse; tinted background ties it visually
             to the cards underneath via the same accent left-border. -->
        <div
          v-if="isGroupHead(job, idx)"
          :key="`gh-${getGroupId(job)}`"
          class="job-group-header"
          @click="toggleGroup(getGroupId(job)!)"
          :title="getGroupTemplate(job)"
        >
          <span class="group-toggle">{{ collapsedGroups.has(getGroupId(job)!) ? '▸' : '▾' }}</span>
          <span class="group-icon">&#128279;</span>
          <span class="group-id">Group {{ getGroupId(job)!.slice(0, 8) }}</span>
          <span class="group-count">{{ getGroupTotal(job) }} variations</span>
          <span v-if="getGroupTemplate(job)" class="group-template">{{ getGroupTemplate(job) }}</span>
        </div>
        <div
          v-show="!isHiddenByCollapse(job, idx)"
          :class="[
            'job-card',
            `job-${job.status}`,
            {
              'in-variation-group': !!getGroupId(job),
              'group-first': isGroupHead(job, idx),
              'group-last': isGroupTail(job, idx),
            }
          ]"
        >
        <!-- Job Header -->
        <div class="job-header">
          <div class="job-type">
            <span class="job-icon" v-html="getTypeIcon(job.type, job)"></span>
            <span class="job-type-label">{{ getTypeName(job.type, job) }}</span>
            <span v-if="job.title" class="job-title-suffix"> - {{ job.title }}</span>
          </div>
          <div class="job-header-right">
            <span class="job-time-relative" :title="formatDate(job.created_at)">{{ formatRelativeTime(job.created_at) }}</span>
            <span :class="['badge', `badge-${job.status}`]">{{ job.status }}</span>
          </div>
        </div>

        <!-- Job Content -->
        <div class="job-content">
          <!-- Left: Prompt and details -->
          <div class="job-details">
            <!-- Convert job details -->
            <div v-if="isConvertJob(job)" class="job-convert-details">
              <div class="convert-row">
                <span class="convert-label">Input:</span>
                <span class="convert-value" :title="getConvertInput(job)">{{ formatComponentName(getConvertInput(job)) }}</span>
                <span v-if="getConvertInputExt(job)" class="convert-ext-badge">{{ getConvertInputExt(job) }}</span>
              </div>
              <div class="convert-arrow">&#8595;</div>
              <div class="convert-row">
                <span class="convert-label">Output:</span>
                <span class="convert-value" :title="getConvertOutput(job)">{{ formatComponentName(getConvertOutput(job)) }}</span>
                <span v-if="getConvertOutputExt(job)" class="convert-ext-badge">{{ getConvertOutputExt(job) }}</span>
              </div>
              <div v-if="getConvertType(job)" class="convert-type">
                <span class="convert-type-badge">{{ getConvertType(job).toUpperCase() }}</span>
              </div>
            </div>

            <!-- Prompt preview (for non-convert jobs) -->
            <div v-else-if="getJobPrompt(job)" class="job-prompt">
              <span class="prompt-text" :title="getJobPrompt(job)">{{ truncateText(getJobPrompt(job), 150) }}</span>
            </div>

            <!-- Negative prompt (if exists) -->
            <div v-if="getJobNegativePrompt(job)" class="job-negative-prompt">
              <span class="negative-label">Negative:</span>
              <span class="negative-text" :title="getJobNegativePrompt(job)">{{ truncateText(getJobNegativePrompt(job), 80) }}</span>
            </div>

            <!-- Job metadata -->
            <div class="job-meta">
              <span v-if="getJobDimensions(job)" class="meta-item" title="Dimensions">
                <span class="meta-icon">&#128207;</span>{{ getJobDimensions(job) }}
              </span>
              <span v-if="getJobSteps(job)" class="meta-item" title="Steps">
                <span class="meta-icon">&#128200;</span>{{ getJobSteps(job) }} steps
              </span>
              <span v-if="getJobSampler(job)" class="meta-item" title="Sampler">
                <span class="meta-icon">&#9881;</span>{{ getJobSampler(job) }}
              </span>
              <span v-if="getJobScheduler(job)" class="meta-item" title="Scheduler">
                <span class="meta-icon">&#128336;</span>{{ getJobScheduler(job) }}
              </span>
              <span v-if="getJobSeed(job) !== null" class="meta-item" title="Seed">
                <span class="meta-icon">&#127793;</span>{{ getJobSeed(job) }}
              </span>
              <span v-if="getJobBatchCount(job) > 1" class="meta-item" title="Batch count">
                <span class="meta-icon">&#128451;</span>{{ getJobBatchCount(job) }}x
              </span>
              <span v-if="job.started_at && job.completed_at" class="meta-item meta-duration" title="Duration">
                <span class="meta-icon">&#9201;</span>{{ formatDuration(job.started_at, job.completed_at) }}
              </span>
              <span v-else-if="job.started_at && job.status === 'processing'" class="meta-item meta-duration meta-elapsed-live" title="Elapsed">
                <span class="meta-icon">&#9201;</span>{{ formatElapsed(job.started_at, nowMs) }}
              </span>
            </div>

            <!-- Model info section (skip for convert jobs — they show input/output paths instead) -->
            <div v-if="job.model_settings && !isConvertJob(job)" class="job-model-section">
              <!-- Main model badge -->
              <div v-if="job.model_settings.model_name" class="job-model-info">
                <span class="model-badge" :class="{ 'model-different': isModelDifferent(job) }">
                  <span class="model-arch" v-if="job.model_settings.model_architecture">[{{ job.model_settings.model_architecture }}]</span>
                  {{ formatComponentName(job.model_settings.model_name) }}
                  <span v-if="isModelDifferent(job)" class="model-warning" title="Different from currently loaded model">*</span>
                </span>
              </div>
              <!-- Model components -->
              <div v-if="getJobModelComponents(job).length > 0" class="job-components">
                <span
                  v-for="comp in getJobModelComponents(job)"
                  :key="comp.label"
                  class="component-tag"
                  :title="comp.value"
                >
                  <span class="comp-label">{{ comp.label }}:</span>
                  <span class="comp-value">{{ comp.value }}</span>
                </span>
              </div>
            </div>
          </div>

          <!-- Right: Outputs or Progress -->
          <div class="job-visual">
            <!-- Progress for processing jobs -->
            <div v-if="job.status === 'processing'" class="job-processing-container">
              <!-- Live preview thumbnail -->
              <div v-if="store.currentPreview?.jobId === job.job_id" class="job-preview-thumb">
                <img
                  :src="store.currentPreview.image"
                  alt="Live preview"
                  class="preview-image"
                  @click="openPreviewLightbox(store.currentPreview.image)"
                />
              </div>
              <div class="job-progress">
                <ProgressBar
                  :progress="job.progress.total_steps > 0 ? (job.progress.step / job.progress.total_steps) * 100 : 0"
                  :show-label="true"
                />
                <div class="progress-details">
                  Step {{ job.progress.step }}/{{ job.progress.total_steps }}
                </div>
              </div>
            </div>

            <!-- Outputs for completed jobs -->
            <div v-else-if="job.status === 'completed' && job.outputs.length > 0" class="job-outputs">
              <!-- Generation jobs: render image thumbnails -->
              <template v-if="isGenerationJob(job)">
                <!-- Input thumbnails: source (init/ref), mask, control.
                     Rendered inline before the arrow so a user can see all
                     inputs that fed the job at a glance. Each thumbnail is
                     independent — a job can have any combination (e.g. a
                     txt2img with ControlNet has control but no source; an
                     inpaint has source + mask + potentially control). -->
                <template v-if="job.type === 'upscale' || job.type === 'img2img' || isRefImagesJob(job)">
                  <div class="source-image">
                    <span class="source-label">Source</span>
                    <button
                      class="output-thumb source-thumb"
                      @click="openLightbox([job.job_id + '/source.png'], 0)"
                      title="View source image"
                    >
                      <img :src="getThumbUrl(job.job_id + '/source.png')" alt="Source" />
                    </button>
                  </div>
                </template>
                <template v-if="hasMaskImage(job)">
                  <div class="source-image">
                    <span class="source-label">Mask</span>
                    <button
                      class="output-thumb source-thumb"
                      @click="openLightbox([job.job_id + '/mask.png'], 0)"
                      title="View inpaint mask"
                    >
                      <img :src="getThumbUrl(job.job_id + '/mask.png')" alt="Mask" />
                    </button>
                  </div>
                </template>
                <template v-if="hasControlImage(job)">
                  <div class="source-image">
                    <span class="source-label">Control</span>
                    <button
                      class="output-thumb source-thumb"
                      @click="openLightbox([job.job_id + '/control.png'], 0)"
                      title="View ControlNet input image"
                    >
                      <img :src="getThumbUrl(job.job_id + '/control.png')" alt="Control" />
                    </button>
                  </div>
                </template>
                <template v-if="job.type === 'upscale' || job.type === 'img2img' || isRefImagesJob(job) || hasMaskImage(job) || hasControlImage(job)">
                  <span class="arrow-separator">→</span>
                </template>
                <button
                  v-for="(output, idx) in job.outputs.slice(0, 4)"
                  :key="output"
                  class="output-thumb"
                  @click="openLightbox(job.outputs, idx)"
                  :title="'Click to view full size'"
                >
                  <img :src="getThumbUrl(output)" :alt="output" />
                </button>
                <button
                  v-if="job.outputs.length > 4"
                  class="more-outputs"
                  @click="openLightbox(job.outputs, 4)"
                >
                  +{{ job.outputs.length - 4 }}
                </button>
              </template>
              <!-- Non-generation jobs: show a typed icon instead of a (broken) thumbnail -->
              <div v-else class="job-output-icon" :title="getTypeName(job.type, job)">
                <span class="job-output-icon-glyph" v-html="getNonGenerationOutputIcon(job.type)"></span>
                <span class="job-output-icon-label">{{ getTypeName(job.type, job) }}</span>
              </div>
            </div>

            <!-- Error for failed jobs -->
            <div v-else-if="job.status === 'failed' && job.error" class="job-error" :title="job.error">
              <span class="error-text">{{ truncateText(job.error, 100) }}</span>
              <button v-if="job.error.length > 100" class="error-expand-btn" @click.stop="selectedJob = job; showDetailsModal = true">
                View Full
              </button>
            </div>

            <!-- Pending indicator -->
            <div v-else-if="job.status === 'pending'" class="job-pending-indicator">
              <span class="pending-icon">&#9203;</span>
              <span class="pending-text">Waiting...</span>
            </div>
          </div>
        </div>

        <!-- Job Footer -->
        <div class="job-footer">
          <div class="job-id-row">
            <span class="job-id text-mono">{{ job.job_id }}</span>
          </div>
          <div class="job-actions">
            <button
              v-if="job.params"
              class="btn btn-secondary btn-sm"
              @click="reloadSettings(job)"
              title="Load these settings into Generate form"
            >
              Reload
            </button>
            <button
              v-if="(job.status === 'failed' || job.status === 'completed' || job.status === 'cancelled') && job.params"
              class="btn btn-primary btn-sm"
              @click="handleRestartJob(job)"
              :disabled="restarting === job.job_id"
              title="Restart this job"
            >
              {{ restarting === job.job_id ? '...' : 'Restart' }}
            </button>
            <button
              v-if="job.status === 'completed' && job.outputs.length > 0 && job.type !== 'txt2vid'"
              class="btn btn-secondary btn-sm"
              @click="useForUpscale(job)"
              title="Use output for upscaling"
            >
              Upscale
            </button>
            <button class="btn btn-secondary btn-sm" @click="viewDetails(job)">
              Details
            </button>
            <button
              v-if="job.status === 'pending'"
              class="btn btn-danger btn-sm"
              @click="cancelJob(job.job_id)"
              :disabled="cancelling === job.job_id"
            >
              {{ cancelling === job.job_id ? '...' : 'Cancel' }}
            </button>
            <button
              v-if="job.status === 'completed' || job.status === 'failed' || job.status === 'cancelled'"
              class="btn btn-danger btn-sm"
              @click="deleteJob(job.job_id)"
              :disabled="deleting === job.job_id"
              title="Move to recycle bin"
            >
              {{ deleting === job.job_id ? '...' : 'Delete' }}
            </button>
          </div>
        </div>
      </div>
      </template>

    </TransitionGroup>

    <!-- Jobs List - Date Groups View -->
    <div v-else-if="viewMode === 'dates'" class="jobs-list date-grouped">
      <template v-for="group in dateGroups" :key="group.date">
        <div class="date-group-header">
          <span class="date-label">{{ group.label }}</span>
          <span class="date-count">{{ group.count }} {{ group.count === 1 ? 'job' : 'jobs' }}</span>
        </div>
        <TransitionGroup name="job-list" tag="div" class="date-group-items">
          <div
            v-for="job in group.items"
            :key="job.job_id"
            :class="['job-card', `job-${job.status}`]"
          >
            <!-- Job Header -->
            <div class="job-header">
              <div class="job-type">
                <span class="job-icon" v-html="getTypeIcon(job.type, job)"></span>
                <span class="job-type-label">{{ getTypeName(job.type, job) }}</span>
                <span v-if="job.title" class="job-title-suffix"> - {{ job.title }}</span>
              </div>
              <div class="job-header-right">
                <span class="job-time-relative" :title="formatDate(job.created_at)">{{ formatRelativeTime(job.created_at) }}</span>
                <span :class="['badge', `badge-${job.status}`]">{{ job.status }}</span>
              </div>
            </div>

            <!-- Job Content -->
            <div class="job-content">
              <!-- Left: Prompt and details -->
              <div class="job-details">
                <!-- Convert job details -->
                <div v-if="isConvertJob(job)" class="job-convert-details">
                  <div class="convert-row">
                    <span class="convert-label">Input:</span>
                    <span class="convert-value" :title="getConvertInput(job)">{{ formatComponentName(getConvertInput(job)) }}</span>
                    <span v-if="getConvertInputExt(job)" class="convert-ext-badge">{{ getConvertInputExt(job) }}</span>
                  </div>
                  <div class="convert-arrow">&#8595;</div>
                  <div class="convert-row">
                    <span class="convert-label">Output:</span>
                    <span class="convert-value" :title="getConvertOutput(job)">{{ formatComponentName(getConvertOutput(job)) }}</span>
                    <span v-if="getConvertOutputExt(job)" class="convert-ext-badge">{{ getConvertOutputExt(job) }}</span>
                  </div>
                  <div v-if="getConvertType(job)" class="convert-type">
                    <span class="convert-type-badge">{{ getConvertType(job).toUpperCase() }}</span>
                  </div>
                </div>
                <div v-else-if="getJobPrompt(job)" class="job-prompt">
                  <span class="prompt-text" :title="getJobPrompt(job)">{{ truncateText(getJobPrompt(job), 150) }}</span>
                </div>
                <div v-if="getJobNegativePrompt(job)" class="job-negative-prompt">
                  <span class="negative-label">Negative:</span>
                  <span class="negative-text" :title="getJobNegativePrompt(job)">{{ truncateText(getJobNegativePrompt(job), 80) }}</span>
                </div>
                <div class="job-meta">
                  <span v-if="getJobDimensions(job)" class="meta-item" title="Dimensions">
                    <span class="meta-icon">&#128207;</span>{{ getJobDimensions(job) }}
                  </span>
                  <span v-if="getJobSteps(job)" class="meta-item" title="Steps">
                    <span class="meta-icon">&#128200;</span>{{ getJobSteps(job) }} steps
                  </span>
                  <span v-if="getJobSampler(job)" class="meta-item" title="Sampler">
                    <span class="meta-icon">&#9881;</span>{{ getJobSampler(job) }}
                  </span>
                  <span v-if="getJobSeed(job) !== null" class="meta-item" title="Seed">
                    <span class="meta-icon">&#127793;</span>{{ getJobSeed(job) }}
                  </span>
                  <span v-if="job.started_at && job.completed_at" class="meta-item meta-duration" title="Duration">
                    <span class="meta-icon">&#9201;</span>{{ formatDuration(job.started_at, job.completed_at) }}
                  </span>
                  <span v-else-if="job.started_at && job.status === 'processing'" class="meta-item meta-duration meta-elapsed-live" title="Elapsed">
                    <span class="meta-icon">&#9201;</span>{{ formatElapsed(job.started_at, nowMs) }}
                  </span>
                </div>
                <!-- Model info section (skip for convert jobs — they show input/output paths instead) -->
                <div v-if="job.model_settings && !isConvertJob(job)" class="job-model-section">
                  <!-- Main model badge -->
                  <div v-if="job.model_settings.model_name" class="job-model-info">
                    <span class="model-badge" :class="{ 'model-different': isModelDifferent(job) }">
                      <span class="model-arch" v-if="job.model_settings.model_architecture">[{{ job.model_settings.model_architecture }}]</span>
                      {{ formatComponentName(job.model_settings.model_name) }}
                      <span v-if="isModelDifferent(job)" class="model-warning" title="Different from currently loaded model">*</span>
                    </span>
                  </div>
                  <!-- Model components -->
                  <div v-if="getJobModelComponents(job).length > 0" class="job-components">
                    <span
                      v-for="comp in getJobModelComponents(job)"
                      :key="comp.label"
                      class="component-tag"
                      :title="comp.value"
                    >
                      <span class="comp-label">{{ comp.label }}:</span>
                      <span class="comp-value">{{ comp.value }}</span>
                    </span>
                  </div>
                </div>
              </div>

              <!-- Right: Outputs or Progress -->
              <div class="job-visual">
                <div v-if="job.status === 'processing'" class="job-processing-container">
                  <!-- Live preview thumbnail -->
                  <div v-if="store.currentPreview?.jobId === job.job_id" class="job-preview-thumb">
                    <img
                      :src="store.currentPreview.image"
                      alt="Live preview"
                      class="preview-image"
                      @click="openPreviewLightbox(store.currentPreview.image)"
                    />
                  </div>
                  <div class="job-progress">
                    <ProgressBar
                      :progress="job.progress.total_steps > 0 ? (job.progress.step / job.progress.total_steps) * 100 : 0"
                      :show-label="true"
                    />
                    <div class="progress-details">
                      Step {{ job.progress.step }}/{{ job.progress.total_steps }}
                    </div>
                  </div>
                </div>
                <div v-else-if="job.status === 'completed' && job.outputs.length > 0" class="job-outputs">
                  <!-- Generation jobs: render image thumbnails -->
                  <template v-if="isGenerationJob(job)">
                    <!-- Source image for upscale and img2img jobs -->
                    <template v-if="job.type === 'upscale' || job.type === 'img2img'">
                      <div class="source-image">
                        <span class="source-label">Source</span>
                        <button
                          class="output-thumb source-thumb"
                          @click="openLightbox([job.job_id + '/source.png'], 0)"
                          title="View source image"
                        >
                          <img :src="getThumbUrl(job.job_id + '/source.png')" alt="Source" />
                        </button>
                      </div>
                      <span class="arrow-separator">→</span>
                    </template>
                    <button
                      v-for="(output, idx) in job.outputs.slice(0, 4)"
                      :key="output"
                      class="output-thumb"
                      @click="openLightbox(job.outputs, idx)"
                      :title="'Click to view full size'"
                    >
                      <img :src="getThumbUrl(output)" :alt="output" />
                    </button>
                    <button
                      v-if="job.outputs.length > 4"
                      class="more-outputs"
                      @click="openLightbox(job.outputs, 4)"
                    >
                      +{{ job.outputs.length - 4 }}
                    </button>
                  </template>
                  <!-- Non-generation jobs: show a typed icon instead of a (broken) thumbnail -->
                  <div v-else class="job-output-icon" :title="getTypeName(job.type, job)">
                    <span class="job-output-icon-glyph" v-html="getNonGenerationOutputIcon(job.type)"></span>
                    <span class="job-output-icon-label">{{ getTypeName(job.type, job) }}</span>
                  </div>
                </div>
                <div v-else-if="job.status === 'failed' && job.error" class="job-error" :title="job.error">
                  <span class="error-text">{{ truncateText(job.error, 100) }}</span>
                  <button v-if="job.error.length > 100" class="error-expand-btn" @click.stop="selectedJob = job; showDetailsModal = true">
                    View Full
                  </button>
                </div>
                <div v-else-if="job.status === 'pending'" class="job-pending-indicator">
                  <span class="pending-icon">&#9203;</span>
                  <span class="pending-text">Waiting...</span>
                </div>
              </div>
            </div>

            <!-- Job Footer -->
            <div class="job-footer">
              <div class="job-id-row">
                <span class="job-id text-mono">{{ job.job_id }}</span>
              </div>
              <div class="job-actions">
                <button
                  v-if="job.params"
                  class="btn btn-secondary btn-sm"
                  @click="reloadSettings(job)"
                  title="Load these settings into Generate form"
                >
                  Reload
                </button>
                <button
                  v-if="(job.status === 'failed' || job.status === 'completed' || job.status === 'cancelled') && job.params"
                  class="btn btn-primary btn-sm"
                  @click="handleRestartJob(job)"
                  :disabled="restarting === job.job_id"
                  title="Restart this job"
                >
                  {{ restarting === job.job_id ? '...' : 'Restart' }}
                </button>
                <button
                  v-if="job.status === 'completed' && job.outputs.length > 0 && job.type !== 'txt2vid'"
                  class="btn btn-secondary btn-sm"
                  @click="useForUpscale(job)"
                  title="Use output for upscaling"
                >
                  Upscale
                </button>
                <button class="btn btn-secondary btn-sm" @click="viewDetails(job)">
                  Details
                </button>
                <button
                  v-if="job.status === 'pending'"
                  class="btn btn-danger btn-sm"
                  @click="cancelJob(job.job_id)"
                  :disabled="cancelling === job.job_id"
                >
                  {{ cancelling === job.job_id ? '...' : 'Cancel' }}
                </button>
                <button
                  v-if="job.status === 'completed' || job.status === 'failed' || job.status === 'cancelled'"
                  class="btn btn-danger btn-sm"
                  @click="deleteJob(job.job_id)"
                  :disabled="deleting === job.job_id"
                  title="Move to recycle bin"
                >
                  {{ deleting === job.job_id ? '...' : 'Delete' }}
                </button>
              </div>
            </div>
          </div>
        </TransitionGroup>
      </template>
    </div>

    <!-- Pagination -->
    <div v-if="store.queue && store.queue.total_count > 0" class="pagination-bar">
      <div class="pagination-info">
        <template v-if="viewMode === 'pages'">
          Showing {{ (currentPage - 1) * itemsPerPage + 1 }}-{{ Math.min(currentPage * itemsPerPage, store.queue.total_count) }}
          of {{ store.queue.total_count }} jobs
        </template>
        <template v-else>
          Page {{ currentPage }} of {{ totalPages }} ({{ store.queue.total_count }} jobs)
        </template>
      </div>

      <div class="pagination-controls">
        <button
          class="btn btn-sm btn-secondary"
          :disabled="!hasPrevPage"
          @click="prevPage"
        >
          &laquo; Prev
        </button>

        <div class="page-numbers">
          <template v-for="item in visiblePageNumbers" :key="item">
            <span v-if="item === 'ellipsis'" class="page-ellipsis">...</span>
            <button
              v-else
              class="btn btn-sm"
              :class="currentPage === item ? 'btn-primary' : 'btn-secondary'"
              @click="goToPage(item)"
            >
              {{ item }}
            </button>
          </template>
        </div>

        <button
          class="btn btn-sm btn-secondary"
          :disabled="!hasNextPage"
          @click="nextPage"
        >
          Next &raquo;
        </button>
      </div>

      <div class="pagination-options">
        <label class="per-page-label">Per page:</label>
        <select class="per-page-select" :value="itemsPerPage" @change="changeItemsPerPage(Number(($event.target as HTMLSelectElement).value))">
          <option :value="10">10</option>
          <option :value="20">20</option>
          <option :value="50">50</option>
          <option :value="100">100</option>
        </select>
      </div>
    </div>

    <div v-if="(viewMode === 'pages' && sortedJobs.length === 0 || viewMode === 'dates' && dateGroups.length === 0) && (statusFilter !== 'all' || searchQuery)" class="empty-state">
      <div class="empty-state-icon">&#128270;</div>
      <div class="empty-state-title">No matching jobs</div>
      <p>Try adjusting your filters or <button class="link-btn" @click="clearFilters">clear all filters</button></p>
    </div>

    <div v-else-if="(viewMode === 'pages' && sortedJobs.length === 0 || viewMode === 'dates' && dateGroups.length === 0)" class="empty-state">
      <div class="empty-state-icon">&#128203;</div>
      <div class="empty-state-title">No jobs yet</div>
      <p>Go to <router-link to="/generate">Generate</router-link> to create your first image</p>
    </div>

    <!-- Details Modal -->
    <Modal
      :show="showDetailsModal"
      :title="`Job: ${selectedJob?.job_id.substring(0, 8)}...`"
      @close="showDetailsModal = false"
    >
      <div v-if="selectedJob">
        <div class="detail-section">
          <h4>Status</h4>
          <span :class="['badge', `badge-${selectedJob.status}`]">{{ selectedJob.status }}</span>
        </div>

        <div class="detail-section">
          <h4>Type</h4>
          <span>{{ selectedJob.type }}</span>
        </div>

        <div class="detail-section">
          <h4>Created</h4>
          <span>{{ formatDate(selectedJob.created_at) }}</span>
        </div>

        <div v-if="selectedJob.started_at" class="detail-section">
          <h4>Started</h4>
          <span>{{ formatDate(selectedJob.started_at) }}</span>
        </div>

        <div v-if="selectedJob.completed_at" class="detail-section">
          <h4>Completed</h4>
          <span>{{ formatDate(selectedJob.completed_at) }}</span>
        </div>

        <div v-if="selectedJob.params" class="detail-section">
          <h4>Parameters</h4>
          <pre class="params-json">{{ JSON.stringify(selectedJob.params, null, 2) }}</pre>
        </div>

        <div v-if="selectedJob.outputs.length > 0" class="detail-section">
          <h4>Outputs</h4>
          <div class="outputs-gallery">
            <template v-if="isGenerationJob(selectedJob)">
              <button
                v-for="(output, idx) in selectedJob.outputs"
                :key="output"
                class="output-link"
                @click="openLightbox(selectedJob.outputs, idx)"
              >
                <img :src="getThumbUrl(output)" :alt="output" />
                <span class="output-name">{{ output.split('/').pop() }}</span>
              </button>
            </template>
            <template v-else>
              <div
                v-for="output in selectedJob.outputs"
                :key="output"
                class="output-link"
              >
                <span class="job-output-icon-glyph" v-html="getNonGenerationOutputIcon(selectedJob.type)"></span>
                <span class="output-name">{{ output.split('/').pop() }}</span>
              </div>
            </template>
          </div>
        </div>

        <div v-if="selectedJob.error" class="detail-section">
          <h4>Error</h4>
          <div class="error-message">{{ selectedJob.error }}</div>
        </div>
      </div>
    </Modal>

    <!-- Model Confirmation Modal (for Restart) -->
    <Modal
      :show="showModelConfirmModal"
      title="Different Model Detected"
      @close="cancelModelLoad"
    >
      <div class="model-confirm-content">
        <p>This job was created with a different model:</p>
        <div class="model-comparison">
          <div class="model-item">
            <span class="model-label">Job's model:</span>
            <span class="model-name">{{ pendingRestartJob?.model_settings?.model_name || 'Unknown' }}</span>
          </div>
          <div class="model-item">
            <span class="model-label">Current model:</span>
            <span class="model-name">{{ store.modelName || 'None loaded' }}</span>
          </div>
        </div>
        <p class="model-question">Would you like to load the original model first?</p>
        <div class="modal-actions">
          <button class="btn btn-primary" @click="loadModelAndRestart">
            Load Model & Restart
          </button>
          <button class="btn btn-secondary" @click="restartWithCurrentModel">
            Use Current Model
          </button>
          <button class="btn btn-secondary" @click="cancelModelLoad">
            Cancel
          </button>
        </div>
      </div>
    </Modal>

    <!-- Selective-reload picker.
         Per-section checkboxes with a truncated preview + tooltip-on-hover
         showing the full value. Apply hands selection over to either the
         existing model-confirm modal (if 'model' is checked + differs) or
         straight to the Generate page via sessionStorage. -->
    <Modal
      :show="showReloadSelectorModal"
      title="Reload from job"
      @close="cancelReloadSelector"
    >
      <div class="reload-selector">
        <div class="reload-selector-header">
          <span class="reload-job-meta" v-if="reloadSelectorJob">
            <span class="reload-job-id">{{ reloadSelectorJob.job_id.slice(0, 8) }}</span>
            <span :class="['badge', `badge-${reloadSelectorJob.status}`]">{{ reloadSelectorJob.status }}</span>
            <span class="reload-job-type">{{ getTypeName(reloadSelectorJob.type, reloadSelectorJob) }}</span>
          </span>
          <div class="reload-selector-quickactions">
            <button class="reload-link" type="button" @click="reloadSelectAll">All</button>
            <span class="reload-link-sep">·</span>
            <button class="reload-link" type="button" @click="reloadSelectNone">None</button>
          </div>
        </div>

        <ul class="reload-section-list">
          <li v-for="s in reloadSections" :key="s.key" class="reload-section">
            <label class="reload-row" :title="s.full">
              <input type="checkbox" v-model="s.selected" />
              <span class="reload-row-body">
                <span class="reload-row-label">{{ s.label }}</span>
                <span class="reload-row-preview">{{ s.preview }}</span>
              </span>
            </label>
          </li>
        </ul>

        <div class="modal-actions">
          <button class="btn btn-secondary" @click="cancelReloadSelector">Cancel</button>
          <button
            class="btn btn-primary"
            :disabled="selectedReloadCount() === 0"
            @click="applyReloadSelection"
          >
            Reload {{ selectedReloadCount() }} section{{ selectedReloadCount() === 1 ? '' : 's' }}
          </button>
        </div>
      </div>
    </Modal>

    <!-- Lightbox -->
    <Lightbox
      :show="showLightbox"
      :images="lightboxImages"
      :currentIndex="lightboxIndex"
      @close="showLightbox = false"
      @update:currentIndex="lightboxIndex = $event"
    />

    <!-- Upscale Image Selection Modal -->
    <Modal
      :show="showUpscaleSelectModal"
      title="Select Image to Upscale"
      @close="showUpscaleSelectModal = false; upscaleSelectJob = null"
    >
      <p class="upscale-select-hint">Click on an image to send it to the upscaler:</p>
      <div v-if="upscaleSelectJob" class="upscale-select-grid">
        <button
          v-for="(output, idx) in upscaleSelectJob.outputs"
          :key="output"
          class="upscale-select-thumb"
          @click="sendImageToUpscale(output)"
          :title="`Select image ${idx + 1}`"
        >
          <img :src="getThumbUrl(output)" :alt="`Output ${idx + 1}`" />
          <span class="upscale-select-index">{{ idx + 1 }}</span>
        </button>
      </div>
    </Modal>
  </div>
</template>

<style scoped>
/* Filter Bar */
.filters-bar {
  display: flex;
  align-items: flex-end;
  gap: 16px;
  margin-bottom: 20px;
  padding: 12px 16px;
  background: var(--bg-secondary);
  border: 1px solid var(--border-color);
  border-radius: var(--border-radius);
  flex-wrap: wrap;
}

.filter-group {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.filter-label {
  font-size: 11px;
  color: var(--text-secondary);
  text-transform: uppercase;
  font-weight: 500;
}

.filter-select {
  padding: 6px 10px;
  border: 1px solid var(--border-color);
  border-radius: var(--border-radius-sm);
  background: var(--bg-tertiary);
  color: var(--text-primary);
  font-size: 13px;
  min-width: 120px;
}

.filter-input {
  padding: 6px 10px;
  border: 1px solid var(--border-color);
  border-radius: var(--border-radius-sm);
  background: var(--bg-tertiary);
  color: var(--text-primary);
  font-size: 13px;
  min-width: 200px;
}

.filter-search {
  flex: 1;
  min-width: 200px;
}

.filter-search .filter-input {
  width: 100%;
}

.filter-clear {
  align-self: flex-end;
}

.filter-results {
  margin-left: auto;
  font-size: 12px;
  color: var(--text-secondary);
  align-self: flex-end;
}

/* View Mode Toggle */
.view-mode-toggle {
  display: flex;
  border: 1px solid var(--border-color);
  border-radius: var(--border-radius-sm);
  overflow: hidden;
}

.view-mode-btn {
  display: flex;
  align-items: center;
  gap: 4px;
  padding: 6px 12px;
  background: var(--bg-tertiary);
  border: none;
  color: var(--text-secondary);
  font-size: 12px;
  cursor: pointer;
  transition: all 0.2s;
}

.view-mode-btn:first-child {
  border-right: 1px solid var(--border-color);
}

.view-mode-btn:hover {
  background: var(--bg-secondary);
  color: var(--text-primary);
}

.view-mode-btn.active {
  background: var(--accent-primary);
  color: var(--bg-primary);
}

.view-icon {
  font-size: 14px;
}

/* Date Groups View - Mail client style */
.date-grouped {
  gap: 0;
}

.date-group-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 10px 16px;
  margin-top: 24px;
  background: var(--bg-tertiary);
  border: 1px solid var(--border-color);
  border-radius: var(--border-radius) var(--border-radius) 0 0;
  position: sticky;
  top: 0;
  z-index: 10;
}

.date-group-header:first-child {
  margin-top: 0;
}

.date-label {
  font-weight: 600;
  font-size: 13px;
  color: var(--accent-primary);
  display: flex;
  align-items: center;
  gap: 8px;
}

.date-label::before {
  content: '📅';
  font-size: 14px;
}

.date-count {
  font-size: 11px;
  color: var(--text-muted);
  background: var(--bg-secondary);
  padding: 2px 10px;
  border-radius: 10px;
  border: 1px solid var(--border-color);
}

.date-group-items {
  display: flex;
  flex-direction: column;
  gap: 0;
  border-left: 1px solid var(--border-color);
  border-right: 1px solid var(--border-color);
  margin-bottom: 0;
}

.date-group-items .job-card {
  border-radius: 0;
  border-top: none;
  border-left: none;
  border-right: none;
  margin: 0;
}

.date-group-items .job-card:last-child {
  border-radius: 0 0 var(--border-radius) var(--border-radius);
  border-bottom: 1px solid var(--border-color);
}

/* Stat cards as filter buttons */
.stat-clickable {
  cursor: pointer;
  transition: all 0.2s;
  border: 2px solid transparent;
}

.stat-clickable:hover {
  transform: translateY(-2px);
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.2);
}

.stat-active {
  border-color: var(--accent-primary);
  background: var(--bg-tertiary);
}

/* Jobs List */
.jobs-list {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

/* TransitionGroup animations */
.job-list-enter-active,
.job-list-leave-active {
  transition: all 0.2s ease;
}

.job-list-enter-from {
  opacity: 0;
  transform: translateX(-20px);
}

.job-list-leave-to {
  opacity: 0;
  transform: translateX(20px);
}

.job-list-move {
  transition: transform 0.2s ease;
}

.job-card {
  background: var(--bg-secondary);
  border: 1px solid var(--border-color);
  border-radius: var(--border-radius);
  padding: 16px;
}

.job-card.job-processing {
  border-left: 4px solid var(--accent-primary);
}

.job-card.job-completed {
  border-left: 4px solid var(--accent-success);
}

.job-card.job-failed {
  border-left: 4px solid var(--accent-error);
}

.job-card.job-pending {
  border-left: 4px solid var(--accent-warning);
}

.job-card.job-cancelled {
  border-left: 4px solid var(--text-muted);
  opacity: 0.7;
}

.job-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 12px;
}

.job-header-right {
  display: flex;
  align-items: center;
  gap: 12px;
}

.job-time-relative {
  font-size: 12px;
  color: var(--text-muted);
}

.job-type {
  display: flex;
  align-items: center;
  gap: 8px;
}

.job-icon {
  font-size: 18px;
}

.job-type-label {
  font-weight: 500;
}

.job-title-suffix {
  margin-left: 4px;
  color: var(--text-muted);
  font-weight: 400;
  font-style: italic;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

/* Job Content Layout */
.job-content {
  display: flex;
  gap: 16px;
  margin-bottom: 12px;
}

.job-details {
  flex: 1;
  min-width: 0;
}

.job-visual {
  flex-shrink: 0;
  width: 200px;
  display: flex;
  align-items: flex-start;
  justify-content: flex-end;
}

/* Prompt Display */
.job-prompt {
  margin-bottom: 8px;
}

.prompt-text {
  font-size: 13px;
  color: var(--text-primary);
  line-height: 1.4;
  word-break: break-word;
}

.job-negative-prompt {
  margin-bottom: 8px;
  font-size: 12px;
}

.negative-label {
  color: var(--accent-error);
  font-weight: 500;
  margin-right: 4px;
}

.negative-text {
  color: var(--text-secondary);
  font-style: italic;
}

/* Job Metadata */
.job-meta {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  margin-bottom: 8px;
}

.meta-item {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  padding: 2px 8px;
  background: var(--bg-tertiary);
  border-radius: var(--border-radius-sm);
  font-size: 11px;
  color: var(--text-secondary);
}

.meta-icon {
  font-size: 12px;
}

.meta-duration {
  background: var(--accent-success);
  color: var(--bg-primary);
}

/* Variation group container effect.
   The header + N cards visually form one continuous rounded box: the header
   has rounded TOP corners + top/left/right border, middle cards have only
   side borders, the last card has rounded BOTTOM corners + bottom border.
   Faint accent-tint background unifies the inside. Negative margin between
   the header and the first card eliminates the gap so the box reads as a
   single unit. */
.job-group-header {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 10px 14px;
  margin-top: 8px;
  background: rgba(var(--accent-rgb, 249, 115, 22), 0.10);
  border: 1px solid rgba(var(--accent-rgb, 249, 115, 22), 0.45);
  border-bottom: none;
  border-left-width: 3px;
  border-radius: var(--border-radius) var(--border-radius) 0 0;
  cursor: pointer;
  user-select: none;
  font-size: 12px;
}
.job-group-header:hover {
  background: rgba(var(--accent-rgb, 249, 115, 22), 0.16);
}
.group-toggle {
  font-family: var(--font-mono, monospace);
  font-size: 14px;
  color: var(--accent-primary);
  width: 12px;
  display: inline-block;
}
.group-icon { font-size: 14px; }
.group-id {
  font-family: var(--font-mono, monospace);
  font-weight: 600;
  color: var(--accent-primary);
}
.group-count {
  color: var(--text-secondary);
  font-size: 11px;
  padding: 1px 8px;
  background: rgba(var(--accent-rgb, 249, 115, 22), 0.18);
  border-radius: 10px;
}
.group-template {
  flex: 1 1 0;
  color: var(--text-secondary);
  font-style: italic;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  font-size: 11px;
}

/* Cards inside a group: tinted bg + side borders. Top/bottom borders only
   on first/last to make the column read as one container. */
.in-variation-group {
  background: rgba(var(--accent-rgb, 249, 115, 22), 0.04);
  border-left: 3px solid rgba(var(--accent-rgb, 249, 115, 22), 0.45);
  border-right: 1px solid rgba(var(--accent-rgb, 249, 115, 22), 0.45);
  border-radius: 0;
  margin-top: 0 !important;
  margin-bottom: 0 !important;
}
.in-variation-group + .in-variation-group {
  /* Inner separator between adjacent group cards — hairline, not the full
     accent so it doesn't look like the container is broken. */
  border-top: 1px dashed rgba(var(--accent-rgb, 249, 115, 22), 0.25);
}
.in-variation-group.group-last {
  border-bottom: 1px solid rgba(var(--accent-rgb, 249, 115, 22), 0.45);
  border-radius: 0 0 var(--border-radius) var(--border-radius);
  margin-bottom: 8px !important;
}
/* group-first carries no top border (the header handles it) and no top
   radius (the header rounds for it). It just butts directly against the
   header — the header already has border-bottom:none. */

/* Job ID in footer */
.job-id-row {
  display: flex;
  align-items: center;
}

.job-id {
  font-size: 11px;
  color: var(--text-muted);
}

/* Progress */
.job-processing-container {
  display: flex;
  gap: 12px;
  align-items: flex-start;
  width: 100%;
}

.job-preview-thumb {
  flex-shrink: 0;
  width: 80px;
  height: 80px;
  border-radius: var(--border-radius-sm);
  overflow: hidden;
  background: var(--bg-tertiary);
  border: 2px solid var(--accent-primary);
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.2);
  cursor: pointer;
  transition: transform 0.2s, border-color 0.2s;
}

.job-preview-thumb:hover {
  transform: scale(1.05);
  border-color: var(--accent-secondary);
}

.job-preview-thumb .preview-image {
  width: 100%;
  height: 100%;
  object-fit: cover;
}

.job-progress {
  flex: 1;
  min-width: 0;
}

.progress-details {
  font-size: 11px;
  color: var(--text-secondary);
  margin-top: 4px;
  text-align: center;
}

/* Outputs */
.job-outputs {
  display: flex;
  gap: 4px;
  flex-wrap: wrap;
  justify-content: flex-end;
  align-items: center;
}

.source-image {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 2px;
}

.source-label {
  font-size: 9px;
  color: var(--text-muted, #666);
  text-transform: uppercase;
  letter-spacing: 0.5px;
}

.source-thumb {
  border-color: var(--warning, #f0ad4e) !important;
  opacity: 0.8;
}

.source-thumb:hover {
  opacity: 1;
}

.arrow-separator {
  color: var(--text-muted, #666);
  font-size: 16px;
  margin: 0 4px;
}

.output-thumb {
  width: 48px;
  height: 48px;
  border-radius: var(--border-radius-sm);
  overflow: hidden;
  padding: 0;
  border: 2px solid transparent;
  background: var(--bg-tertiary);
  cursor: pointer;
  transition: all 0.2s;
}

.output-thumb:hover {
  border-color: var(--accent-primary);
  transform: scale(1.1);
}

.output-thumb img {
  width: 100%;
  height: 100%;
  object-fit: cover;
}

.job-output-icon {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 4px;
  padding: 8px 12px;
  min-width: 64px;
  border-radius: var(--border-radius-sm);
  background: var(--bg-tertiary);
  color: var(--text-secondary);
}

.job-output-icon-glyph {
  font-size: 24px;
  line-height: 1;
}

.job-output-icon-label {
  font-size: 10px;
  text-transform: uppercase;
  letter-spacing: 0.5px;
  color: var(--text-muted, #666);
}

.more-outputs {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 48px;
  height: 48px;
  font-size: 11px;
  color: var(--text-secondary);
  background: var(--bg-tertiary);
  border: 1px solid var(--border-color);
  border-radius: var(--border-radius-sm);
  cursor: pointer;
  transition: all 0.2s;
}

.more-outputs:hover {
  background: var(--accent-primary);
  color: var(--bg-primary);
  border-color: var(--accent-primary);
}

/* Error Display */
.job-error {
  background: rgba(255, 107, 157, 0.1);
  border: 1px solid var(--accent-error);
  border-radius: var(--border-radius-sm);
  padding: 8px 12px;
  font-size: 11px;
  color: var(--accent-error);
  max-width: 200px;
  text-align: right;
  display: flex;
  flex-direction: column;
  gap: 6px;
  align-items: flex-end;
}

.job-error .error-text {
  word-break: break-word;
}

.error-expand-btn {
  background: var(--accent-error);
  color: #fff;
  border: none;
  padding: 4px 8px;
  border-radius: var(--border-radius-sm);
  font-size: 10px;
  cursor: pointer;
  transition: all 0.2s;
  white-space: nowrap;
}

.error-expand-btn:hover {
  background: var(--accent-error-hover, #e05080);
  transform: scale(1.05);
}

/* Pending Indicator */
.job-pending-indicator {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 4px;
  padding: 12px;
  color: var(--text-muted);
}

.pending-icon {
  font-size: 24px;
  animation: pulse 2s infinite;
}

.pending-text {
  font-size: 11px;
}

@keyframes pulse {
  0%, 100% { opacity: 0.5; }
  50% { opacity: 1; }
}

/* Footer */
.job-footer {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding-top: 12px;
  border-top: 1px solid var(--border-color);
}

.job-actions {
  display: flex;
  gap: 6px;
}

/* Link button for empty state */
.link-btn {
  background: none;
  border: none;
  color: var(--accent-primary);
  cursor: pointer;
  text-decoration: underline;
  font-size: inherit;
  padding: 0;
}

.detail-section {
  margin-bottom: 16px;
}

.detail-section h4 {
  font-size: 12px;
  color: var(--text-secondary);
  text-transform: uppercase;
  margin-bottom: 4px;
}

.params-json {
  background: var(--bg-tertiary);
  padding: 12px;
  border-radius: var(--border-radius-sm);
  font-size: 11px;
  overflow-x: auto;
  max-height: 200px;
}

.outputs-gallery {
  display: flex;
  flex-wrap: wrap;
  gap: 12px;
}

.output-link {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 4px;
  text-decoration: none;
}

.output-link img {
  width: 80px;
  height: 80px;
  object-fit: cover;
  border-radius: var(--border-radius-sm);
}

.output-name {
  font-size: 10px;
  color: var(--text-secondary);
  max-width: 80px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.error-message {
  background: rgba(255, 107, 157, 0.1);
  border: 1px solid var(--accent-error);
  border-radius: var(--border-radius-sm);
  padding: 12px;
  color: var(--accent-error);
  font-size: 13px;
}

/* Model info section */
.job-model-section {
  margin: 8px 0;
}

.job-model-info {
  margin-bottom: 6px;
}

.model-badge {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  padding: 4px 8px;
  background: var(--bg-tertiary);
  border: 1px solid var(--border-color);
  border-radius: var(--border-radius-sm);
  font-size: 11px;
  color: var(--text-secondary);
  max-width: 100%;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.model-badge.model-different {
  border-color: var(--accent-warning);
  background: rgba(255, 193, 7, 0.1);
}

.model-arch {
  color: var(--accent-primary);
  font-weight: 600;
}

.model-warning {
  color: var(--accent-warning);
  font-weight: bold;
}

/* Model components */
.job-components {
  display: flex;
  flex-wrap: wrap;
  gap: 4px;
}

.component-tag {
  display: inline-flex;
  align-items: center;
  gap: 2px;
  padding: 2px 6px;
  background: var(--bg-primary);
  border: 1px solid var(--border-color);
  border-radius: 3px;
  font-size: 10px;
  max-width: 150px;
  overflow: hidden;
}

.component-tag .comp-label {
  color: var(--text-muted);
  font-weight: 500;
}

.component-tag .comp-value {
  color: var(--text-secondary);
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

/* Model confirmation modal */
.model-confirm-content p {
  margin-bottom: 12px;
  color: var(--text-primary);
}

.model-comparison {
  background: var(--bg-tertiary);
  border-radius: var(--border-radius-sm);
  padding: 12px;
  margin-bottom: 16px;
}

.model-item {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 8px 0;
}

.model-item:not(:last-child) {
  border-bottom: 1px solid var(--border-color);
}

.model-label {
  color: var(--text-secondary);
  font-size: 12px;
}

.model-name {
  color: var(--accent-primary);
  font-weight: 500;
  font-size: 13px;
  max-width: 200px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.model-components {
  margin-top: 8px;
  padding-top: 8px;
  border-top: 1px dashed var(--border-color);
}

.component-item {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 4px 0;
  font-size: 12px;
}

.component-label {
  color: var(--text-secondary);
}

.component-value {
  color: var(--text-primary);
  max-width: 180px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.current-model {
  margin-top: 8px;
  padding-top: 8px;
  border-top: 1px solid var(--border-color);
}

.model-question {
  font-weight: 500;
  margin-top: 16px;
}

.modal-actions {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  margin-top: 16px;
}

.modal-actions .btn {
  flex: 1;
  min-width: 120px;
}

/* Pagination Bar */
.pagination-bar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 16px;
  margin-top: 24px;
  padding: 12px 16px;
  background: var(--bg-secondary);
  border: 1px solid var(--border-color);
  border-radius: var(--border-radius);
  flex-wrap: wrap;
}

.pagination-bar.pagination-top {
  margin-top: 0;
  margin-bottom: 16px;
}

.pagination-info {
  font-size: 13px;
  color: var(--text-secondary);
}

.pagination-controls {
  display: flex;
  align-items: center;
  gap: 8px;
}

.page-numbers {
  display: flex;
  align-items: center;
  gap: 4px;
}

.page-numbers .btn {
  min-width: 36px;
  padding: 6px 10px;
}

.page-ellipsis {
  padding: 0 8px;
  color: var(--text-muted);
  font-size: 14px;
}

.pagination-options {
  display: flex;
  align-items: center;
  gap: 8px;
}

.per-page-label {
  font-size: 13px;
  color: var(--text-secondary);
}

.per-page-select {
  padding: 6px 10px;
  border: 1px solid var(--border-color);
  border-radius: var(--border-radius-sm);
  background: var(--bg-tertiary);
  color: var(--text-primary);
  font-size: 13px;
  cursor: pointer;
}

.per-page-select:hover {
  border-color: var(--accent-primary);
}

/* Responsive pagination */
@media (max-width: 768px) {
  .pagination-bar {
    justify-content: center;
  }

  .pagination-info {
    order: 3;
    width: 100%;
    text-align: center;
  }

  .pagination-controls {
    order: 1;
  }

  .pagination-options {
    order: 2;
  }
}

/* Upscale Image Selection Modal */
.upscale-select-hint {
  color: var(--text-secondary);
  margin-bottom: 16px;
  font-size: 14px;
}

.upscale-select-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(100px, 1fr));
  gap: 12px;
}

.upscale-select-thumb {
  position: relative;
  aspect-ratio: 1;
  border-radius: var(--border-radius);
  overflow: hidden;
  padding: 0;
  border: 2px solid transparent;
  background: var(--bg-tertiary);
  cursor: pointer;
  transition: all 0.2s;
}

.upscale-select-thumb:hover {
  border-color: var(--accent-primary);
  transform: scale(1.05);
}

.upscale-select-thumb img {
  width: 100%;
  height: 100%;
  object-fit: cover;
}

.upscale-select-index {
  position: absolute;
  bottom: 4px;
  right: 4px;
  background: rgba(0, 0, 0, 0.7);
  color: #fff;
  font-size: 11px;
  padding: 2px 6px;
  border-radius: 4px;
}

/* Convert Job Details */
.job-convert-details {
  display: flex;
  flex-direction: column;
  gap: 4px;
  padding: 12px;
  background: var(--bg-tertiary);
  border-radius: var(--border-radius-sm);
  border: 1px solid var(--border-color);
  margin-bottom: 8px;
}

.convert-row {
  display: flex;
  align-items: center;
  gap: 8px;
}

.convert-label {
  color: var(--text-muted);
  font-size: 11px;
  font-weight: 500;
  min-width: 50px;
}

.convert-value {
  color: var(--text-primary);
  font-size: 13px;
  font-weight: 500;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  max-width: 300px;
}

.convert-arrow {
  color: var(--accent-primary);
  font-size: 14px;
  text-align: center;
  padding: 2px 0;
}

.convert-type {
  margin-top: 8px;
  padding-top: 8px;
  border-top: 1px dashed var(--border-color);
}

.convert-type-badge {
  display: inline-block;
  padding: 4px 10px;
  background: var(--accent-primary);
  color: var(--bg-primary);
  font-size: 11px;
  font-weight: 600;
  border-radius: var(--border-radius-sm);
  letter-spacing: 0.5px;
}

.convert-ext-badge {
  display: inline-block;
  margin-left: 6px;
  padding: 1px 6px;
  font-family: var(--font-mono, monospace);
  font-size: 10px;
  color: var(--text-secondary);
  background: var(--bg-tertiary);
  border: 1px solid var(--border-color);
  border-radius: var(--border-radius-sm);
}

/* Selective reload modal */
.reload-selector {
  display: flex;
  flex-direction: column;
  gap: 14px;
  min-width: 420px;
  max-width: 600px;
}
.reload-selector-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 12px;
  flex-wrap: wrap;
}
.reload-job-meta {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  font-size: 12px;
}
.reload-job-id {
  font-family: var(--font-mono, monospace);
  color: var(--text-secondary);
}
.reload-job-type {
  color: var(--text-secondary);
}
.reload-selector-quickactions {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  font-size: 12px;
}
.reload-link {
  background: none;
  border: none;
  padding: 0;
  cursor: pointer;
  color: var(--accent-primary);
  font-size: 12px;
  font-weight: 600;
}
.reload-link:hover { text-decoration: underline; }
.reload-link-sep {
  color: var(--text-muted);
}
.reload-section-list {
  list-style: none;
  margin: 0;
  padding: 0;
  display: flex;
  flex-direction: column;
  gap: 4px;
}
.reload-section {
  border: 1px solid var(--border-color);
  border-radius: var(--border-radius-sm);
  background: rgba(0, 0, 0, 0.15);
  transition: border-color 0.15s, background 0.15s;
}
.reload-section:hover {
  border-color: rgba(var(--accent-rgb, 249, 115, 22), 0.5);
  background: rgba(var(--accent-rgb, 249, 115, 22), 0.04);
}
.reload-row {
  display: flex;
  align-items: flex-start;
  gap: 10px;
  padding: 10px 12px;
  cursor: pointer;
  user-select: none;
}
.reload-row input[type="checkbox"] {
  margin-top: 2px;
  flex: 0 0 auto;
  accent-color: var(--accent-primary);
  cursor: pointer;
}
.reload-row-body {
  display: flex;
  flex-direction: column;
  gap: 2px;
  flex: 1 1 0;
  min-width: 0;
}
.reload-row-label {
  font-size: 13px;
  font-weight: 600;
  color: var(--text-primary);
}
.reload-row-preview {
  font-size: 12px;
  color: var(--text-secondary);
  font-family: var(--font-mono, monospace);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
</style>
