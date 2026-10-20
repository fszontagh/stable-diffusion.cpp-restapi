<script setup lang="ts">
import { ref, computed, onMounted, watch, nextTick } from 'vue'
import { useRouter, useRoute } from 'vue-router'
import { useAppStore } from '../stores/app'
import { api, ApiError, type ModelInfo, type ConvertParams } from '../api/client'
import Modal from '../components/Modal.vue'
import SkeletonCard from '../components/SkeletonCard.vue'
import ArchLoadBar from '../components/ArchLoadBar.vue'
import type { ArchitecturePreset } from '../api/client'
import { archReadiness, convertCandidates, isConvertible, type ConvertCandidate } from '../composables/archReadiness'

const store = useAppStore()
const router = useRouter()
const route = useRoute()

// Conversion modal
const showConvertModal = ref(false)
const convertModel = ref<ModelInfo | null>(null)
const converting = ref(false)
const convertParams = ref<ConvertParams>({
  input_path: '',
  model_type: '',
  output_type: 'q8_0',
  output_path: ''
})

// Files grid or architecture list. The architecture list shows every
// preset that has at least one main model on disk, with Load (pre-filled
// load page) and Convert (pick which of its files to quantize).
const viewMode = ref<'files' | 'architectures'>('architectures')

// /models?arch=<id> (from the Downloads page) scrolls to that architecture
// and highlights it, so the user can pick its model variant.
const focusedArch = ref<string | null>(null)
async function focusArchFromRoute() {
  const id = route.query.arch
  if (typeof id !== 'string') return
  viewMode.value = 'architectures'
  focusedArch.value = id
  await nextTick()
  const el = document.querySelector(`[data-arch="${CSS.escape(id)}"]`)
  el?.scrollIntoView({ behavior: 'smooth', block: 'center' })
}
watch(() => route.query.arch, focusArchFromRoute)
// The row only exists once models and presets are loaded.
watch(() => [store.models, store.architectures], () => { if (focusedArch.value) void focusArchFromRoute() })

function openDownloads(preset: ArchitecturePreset) {
  router.push({ path: '/downloads', query: { arch: preset.id } })
}

const localArchitectures = computed(() => {
  const presets = Object.values(store.architectures?.architectures ?? {})
  const needle = searchQuery.value.trim().toLowerCase()
  return presets
    .filter(p => archReadiness(p, store.models).mains.length > 0)
    .filter(p => !needle || (p.name + ' ' + p.id).toLowerCase().includes(needle))
    .sort((a, b) => a.name.localeCompare(b.name))
})

const allModelsByKey = computed(() => {
  const map = new Map<string, ModelInfo>()
  const m = store.models
  if (!m) return map
  for (const list of [m.checkpoints, m.diffusion_models, m.vae, m.loras, m.clip, m.t5, m.controlnets, m.llm, m.esrgan, m.taesd ?? []]) {
    for (const model of list ?? []) map.set(model.type + '::' + model.name, model)
  }
  return map
})

function candidateSize(c: ConvertCandidate): number | null {
  return allModelsByKey.value.get(c.modelType + '::' + c.name)?.size_bytes ?? null
}

const archConvertPreset = ref<ArchitecturePreset | null>(null)
const archConvertList = ref<ConvertCandidate[]>([])
const archConvertSelected = ref<Set<string>>(new Set())
const archConvertType = ref('q8_0')
const archConverting = ref(false)

function candidateKey(c: ConvertCandidate): string {
  return c.modelType + '::' + c.name
}

function openArchConvert(preset: ArchitecturePreset) {
  archConvertPreset.value = preset
  archConvertList.value = convertCandidates(preset, store.models)
  archConvertSelected.value = new Set()
  archConvertType.value = 'q8_0'
}

function toggleCandidate(c: ConvertCandidate) {
  const next = new Set(archConvertSelected.value)
  const k = candidateKey(c)
  if (next.has(k)) next.delete(k)
  else next.add(k)
  archConvertSelected.value = next
}

// One convert job per selected file; the backend names each output
// <model>.<type>.gguf next to its source.
async function startArchConvert() {
  const picked = archConvertList.value.filter(c => archConvertSelected.value.has(candidateKey(c)))
  if (picked.length === 0) return
  archConverting.value = true
  let queued = 0
  try {
    for (const c of picked) {
      try {
        await api.convert({ input_path: c.name, model_type: c.modelType, output_type: archConvertType.value })
        queued++
      } catch (e) {
        store.showToast(`${c.name}: ${e instanceof Error ? e.message : 'convert failed'}`, 'error')
      }
    }
    if (queued > 0) {
      store.showToast(`Queued ${queued} conversion${queued > 1 ? 's' : ''}`, 'success')
      archConvertPreset.value = null
    }
  } finally {
    archConverting.value = false
  }
}

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

  const matched = allModels.filter(model => {
    const matchesType = !selectedType.value || model.type === selectedType.value
    const matchesSearch = !searchQuery.value ||
      model.name.toLowerCase().includes(searchQuery.value.toLowerCase())
    return matchesType && matchesSearch
  })

  // Loaded models float to the top of the (already-filtered) list. This is
  // a stable sort: among loaded-vs-loaded and unloaded-vs-unloaded the
  // original directory ordering is preserved, so users see their currently-
  // loaded checkpoint/components first regardless of which filter they applied.
  return matched.slice().sort((a, b) => {
    const aLoaded = a.is_loaded ? 0 : 1
    const bLoaded = b.is_loaded ? 0 : 1
    return aLoaded - bLoaded
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
  // Extract model name from path. A sharded folder ("Name/") is named
  // after the folder and the output lands next to it.
  const isFolder = inputPath.endsWith('/')
  const parts = inputPath.replace(/\/+$/, '').split('/')
  const filename = parts[parts.length - 1]
  // Remove existing extension
  const dotIndex = filename.lastIndexOf('.')
  const stem = dotIndex > 0 && !isFolder ? filename.substring(0, dotIndex) : filename
  // Remove existing quant suffix if present (e.g., .f16, .q8_0)
  const lastDot = stem.lastIndexOf('.')
  let baseName = stem
  if (lastDot > 0) {
    const suffix = stem.substring(lastDot + 1).toLowerCase()
    if (suffix.match(/^(f32|f16|bf16|q\d+_\d+|q\d+_k)$/)) {
      baseName = stem.substring(0, lastDot)
    }
  }
  // Build output path in same directory.
  // If inputPath has no directory part (just a filename — happens when the
  // backend was given a model by name and we don't know the full path),
  // return empty so the request omits output_path entirely; the backend will
  // auto-generate using the resolved input file's parent directory. Sending
  // a directory-less filename caused converted files to land in the server's
  // CWD instead of the model directory, where the scanner couldn't find them.
  const dir = parts.slice(0, -1).join('/')
  if (!dir) return ''
  return dir + '/' + baseName + '.' + quantType + '.gguf'
}

function openConvertModal(model: ModelInfo) {
  convertModel.value = model
  // Pass model name and type - backend resolves to full path
  convertParams.value.input_path = model.name
  convertParams.value.model_type = model.type
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

// ─── Upload modal state ──────────────────────────────────────────────
const showUploadModal = ref(false)
const uploadFile = ref<File | null>(null)
const uploadModelType = ref<string>('checkpoint')
const uploadSubfolder = ref<string>('')
const uploading = ref(false)
const uploadProgress = ref(0)        // 0..1
const uploadLoaded = ref(0)          // bytes
const uploadTotal = ref(0)           // bytes
const uploadDragging = ref(false)
let uploadAbortController: AbortController | null = null
const uploadFileInput = ref<HTMLInputElement | null>(null)

// Reuse the modelTypes list but drop the empty ("All Types") sentinel —
// uploads must target a concrete type.
const uploadModelTypeOptions = computed(() =>
  modelTypes.filter(t => t.value !== '')
)

function openUploadModal() {
  uploadFile.value = null
  uploadModelType.value = 'checkpoint'
  uploadSubfolder.value = ''
  uploadProgress.value = 0
  uploadLoaded.value = 0
  uploadTotal.value = 0
  showUploadModal.value = true
}

function closeUploadModal() {
  if (uploading.value) {
    cancelUpload()
  }
  showUploadModal.value = false
}

function pickUploadFile() {
  uploadFileInput.value?.click()
}

function onUploadFileChange(ev: Event) {
  const target = ev.target as HTMLInputElement
  const f = target.files?.[0]
  if (f) uploadFile.value = f
}

function onUploadDrop(ev: DragEvent) {
  ev.preventDefault()
  uploadDragging.value = false
  const f = ev.dataTransfer?.files?.[0]
  if (f) uploadFile.value = f
}

function onUploadDragOver(ev: DragEvent) {
  ev.preventDefault()
  uploadDragging.value = true
}

function onUploadDragLeave() {
  uploadDragging.value = false
}

function cancelUpload() {
  if (uploadAbortController) {
    uploadAbortController.abort()
    uploadAbortController = null
  }
}

async function startUpload() {
  if (!uploadFile.value) {
    store.showToast('Choose a file first', 'error')
    return
  }
  if (!uploadModelType.value) {
    store.showToast('Select a model type', 'error')
    return
  }

  // Client-side guards mirroring the server validation, so users get
  // immediate feedback instead of a 400 after a long upload.
  const lower = uploadFile.value.name.toLowerCase()
  const supported = ['.safetensors', '.ckpt', '.pt', '.pth', '.bin', '.gguf']
  if (!supported.some(ext => lower.endsWith(ext))) {
    store.showToast('Unsupported file extension', 'error')
    return
  }
  if (uploadSubfolder.value.includes('..') || uploadSubfolder.value.startsWith('/')) {
    store.showToast('Subfolder must be relative and must not contain "..".', 'error')
    return
  }

  uploading.value = true
  uploadProgress.value = 0
  uploadLoaded.value = 0
  uploadTotal.value = uploadFile.value.size
  uploadAbortController = new AbortController()

  try {
    const result = await api.uploadModel(
      uploadFile.value,
      uploadModelType.value,
      uploadSubfolder.value || undefined,
      (loaded, total) => {
        uploadLoaded.value = loaded
        uploadTotal.value = total
        uploadProgress.value = total > 0 ? loaded / total : 0
      },
      uploadAbortController.signal
    )
    store.showToast(`Uploaded ${result.filename} (${formatSize(result.size_bytes)})`, 'success')
    showUploadModal.value = false
    // Pull the freshly scanned list so the new model is visible.
    await store.fetchModels()
  } catch (e) {
    if (e instanceof ApiError && e.message === 'Upload cancelled') {
      store.showToast('Upload cancelled', 'info')
    } else {
      store.showToast(e instanceof Error ? e.message : 'Upload failed', 'error')
    }
  } finally {
    uploading.value = false
    uploadAbortController = null
  }
}

onMounted(() => {
  void focusArchFromRoute()
  store.fetchModels()
  store.fetchOptions()  // Fetch options to get quantization types
  if (!store.architectures) store.fetchArchitectures().catch(() => { /* list stays empty */ })
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
      <!-- Custom flex toolbar instead of .form-row (which is a grid that
           would force the buttons into 200px-min columns of their own and
           ignore the flex:2 / flex:1 hints on the inputs). -->
      <div class="models-filter-row">
        <input
          v-model="searchQuery"
          type="text"
          class="form-input filter-search"
          placeholder="Search models..."
        />
        <div class="view-toggle" role="group" aria-label="View">
          <button :class="['view-btn', { active: viewMode === 'files' }]" @click="viewMode = 'files'">Files</button>
          <button :class="['view-btn', { active: viewMode === 'architectures' }]" @click="viewMode = 'architectures'">By architecture</button>
        </div>
        <select v-if="viewMode === 'files'" v-model="selectedType" class="form-select filter-type">
          <option v-for="type in modelTypes" :key="type.value" :value="type.value">
            {{ type.label }}
          </option>
        </select>
        <button class="btn btn-secondary filter-btn" @click="store.refreshModels()" :disabled="store.loading">
          &#8635; Refresh
        </button>
        <button class="btn btn-primary filter-btn" @click="openUploadModal">
          &#8593; Upload
        </button>
      </div>
    </div>

    <!-- Loading skeleton -->
    <div v-if="!store.models" class="models-grid">
      <SkeletonCard v-for="i in 6" :key="i" />
    </div>

    <!-- Architecture view -->
    <div v-else-if="viewMode === 'architectures'" class="arch-view">
      <p v-if="!store.architectures" class="empty-state">Loading architectures...</p>
      <p v-else-if="localArchitectures.length === 0" class="empty-state">
        No architecture has a downloaded main model yet. Get one from Downloads, By Architecture.
      </p>
      <div
        v-for="preset in localArchitectures"
        :key="preset.id"
        :data-arch="preset.id"
        :class="['arch-view-row', 'card', { focused: focusedArch === preset.id }]"
      >
        <div class="arch-view-head">
          <span class="arch-view-name">{{ preset.name }}</span>
          <span
            :class="['arch-view-state', archReadiness(preset, store.models).ready ? 'ok' : 'warn']"
          >{{ archReadiness(preset, store.models).ready ? 'ready' : 'missing: ' + archReadiness(preset, store.models).missing.join(', ') }}</span>
          <button
            class="btn btn-secondary btn-sm"
            title="Downloads for this architecture: other variants, originals, missing components"
            @click="openDownloads(preset)"
          >Downloads</button>
          <button
            class="btn btn-secondary btn-sm"
            :disabled="convertCandidates(preset, store.models).length === 0"
            :title="convertCandidates(preset, store.models).length ? 'Quantize some of this architecture\'s files' : 'Nothing left to convert: every file is already quantized'"
            @click="openArchConvert(preset)"
          >Convert...</button>
        </div>
        <ArchLoadBar :preset="preset" compact />
      </div>
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
            v-if="['checkpoint', 'diffusion', 'vae', 'lora', 'clip', 't5', 'llm'].includes(model.type) && isConvertible(model.name)"
            class="btn btn-secondary btn-sm"
            @click="openConvertModal(model)"
            title="Convert to GGUF with quantization"
          >
            Convert
          </button>
        </div>
      </div>
    </div>

    <div v-if="viewMode === 'files' && filteredModels.length === 0" class="empty-state">
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

    <!-- Convert by architecture -->
    <Modal
      :show="!!archConvertPreset"
      :title="'Convert ' + (archConvertPreset?.name ?? '')"
      @close="archConvertPreset = null"
    >
      <p class="convert-info">
        Pick the files to quantize. Each one is converted on its own and saved as
        &lt;name&gt;.&lt;type&gt;.gguf next to the source. Already quantized files are not listed.
      </p>
      <ul class="arch-convert-list">
        <li v-for="c in archConvertList" :key="candidateKey(c)">
          <label>
            <input
              type="checkbox"
              :checked="archConvertSelected.has(candidateKey(c))"
              @change="toggleCandidate(c)"
            />
            <span class="arch-convert-comp">{{ c.component }}</span>
            <span class="arch-convert-name" :title="c.name">{{ c.name }}</span>
            <span v-if="candidateSize(c)" class="arch-convert-size">{{ formatSize(candidateSize(c)!) }}</span>
          </label>
        </li>
      </ul>
      <div class="form-group">
        <label class="form-label">Quantization Type</label>
        <select v-model="archConvertType" class="form-select">
          <option v-for="qtype in quantizationTypes" :key="qtype.id" :value="qtype.id">{{ qtype.name }}</option>
        </select>
      </div>
      <template #footer>
        <button class="btn btn-secondary" @click="archConvertPreset = null">Cancel</button>
        <button
          class="btn btn-primary"
          :disabled="archConverting || archConvertSelected.size === 0"
          @click="startArchConvert"
        >
          <span v-if="archConverting" class="spinner"></span>
          {{ archConverting ? 'Queuing...' : `Convert ${archConvertSelected.size || ''}` }}
        </button>
      </template>
    </Modal>

    <!-- Upload Model Modal -->
    <Modal :show="showUploadModal" title="Upload Model" @close="closeUploadModal">
      <div class="convert-info">
        <p>Upload a model file from your computer to the server. Use this when you can't reach this machine via scp/runpodctl.</p>
      </div>

      <div class="form-group">
        <label class="form-label">Model Type</label>
        <select v-model="uploadModelType" class="form-select" :disabled="uploading">
          <option v-for="t in uploadModelTypeOptions" :key="t.value" :value="t.value">
            {{ t.label }}
          </option>
        </select>
      </div>

      <div class="form-group">
        <label class="form-label">Subfolder (optional)</label>
        <input
          v-model="uploadSubfolder"
          type="text"
          class="form-input"
          placeholder="e.g. SDXL/character"
          :disabled="uploading"
        />
        <div class="form-hint">
          Relative path under the type directory. Must not start with "/" or contain "..".
        </div>
      </div>

      <div class="form-group">
        <label class="form-label">File</label>
        <div
          :class="['upload-dropzone', { dragging: uploadDragging, has_file: !!uploadFile, disabled: uploading }]"
          @click="!uploading && pickUploadFile()"
          @drop="onUploadDrop"
          @dragover="onUploadDragOver"
          @dragleave="onUploadDragLeave"
        >
          <input
            ref="uploadFileInput"
            type="file"
            accept=".safetensors,.ckpt,.pt,.pth,.bin,.gguf"
            style="display:none"
            @change="onUploadFileChange"
          />
          <template v-if="uploadFile">
            <div class="upload-filename">{{ uploadFile.name }}</div>
            <div class="upload-meta">{{ formatSize(uploadFile.size) }}</div>
            <button
              v-if="!uploading"
              type="button"
              class="btn btn-secondary btn-sm"
              @click.stop="uploadFile = null"
            >Choose another file</button>
          </template>
          <template v-else>
            <div class="upload-prompt">Drop a model file here, or click to browse</div>
            <div class="upload-meta">.safetensors / .gguf / .ckpt / .pt / .pth / .bin</div>
          </template>
        </div>
      </div>

      <div v-if="uploading" class="upload-progress">
        <div class="upload-progress-bar">
          <div class="upload-progress-fill" :style="{ width: (uploadProgress * 100).toFixed(1) + '%' }"></div>
        </div>
        <div class="upload-progress-text">
          {{ formatSize(uploadLoaded) }} / {{ formatSize(uploadTotal) }}
          ({{ (uploadProgress * 100).toFixed(1) }}%)
        </div>
      </div>

      <template #footer>
        <button
          v-if="!uploading"
          class="btn btn-secondary"
          @click="closeUploadModal"
        >Cancel</button>
        <button
          v-else
          class="btn btn-secondary"
          @click="cancelUpload"
        >Abort Upload</button>
        <button
          class="btn btn-primary"
          @click="startUpload"
          :disabled="uploading || !uploadFile"
        >
          <span v-if="uploading" class="spinner"></span>
          {{ uploading ? 'Uploading...' : 'Upload' }}
        </button>
      </template>
    </Modal>
  </div>
</template>

<style scoped>
.filters {
  margin-bottom: 24px;
}

/* Toolbar row: search grows, type select stays modest, refresh + upload
   take only their natural width. align-items:stretch makes buttons match
   input height exactly so the row reads as one bar. */
.view-toggle {
  display: flex;
  background: var(--bg-tertiary, var(--bg-hover));
  border-radius: var(--border-radius-sm);
  padding: 3px;
  gap: 2px;
  flex: none;
}
.view-btn {
  border: none;
  background: transparent;
  color: var(--text-secondary);
  padding: 6px 12px;
  border-radius: var(--border-radius-sm);
  font-size: 13px;
  cursor: pointer;
  white-space: nowrap;
}
.view-btn.active {
  background: var(--accent-primary);
  color: #fff;
}
.arch-view {
  display: flex;
  flex-direction: column;
  gap: 8px;
}
.arch-view-row {
  padding: 10px 14px;
}
.arch-view-row.focused {
  outline: 2px solid var(--accent-primary);
  outline-offset: -1px;
}
.arch-view-head {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-bottom: 8px;
}
.arch-view-name {
  font-weight: 600;
}
.arch-view-state {
  font-size: 12px;
  margin-right: auto;
}
.arch-view-state.ok {
  color: var(--accent-success);
}
.arch-view-state.warn {
  color: var(--accent-warning);
}
.arch-convert-list {
  list-style: none;
  margin: 0 0 16px;
  padding: 0;
  display: flex;
  flex-direction: column;
  gap: 6px;
}
.arch-convert-list label {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 13px;
  cursor: pointer;
}
.arch-convert-comp {
  font-size: 11px;
  text-transform: uppercase;
  color: var(--text-secondary);
  min-width: 72px;
}
.arch-convert-name {
  flex: 1;
  min-width: 0;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.arch-convert-size {
  color: var(--text-secondary);
  white-space: nowrap;
}
@media (max-width: 720px) {
  .models-filter-row {
    flex-wrap: wrap;
  }
}
.models-filter-row {
  display: flex;
  gap: 12px;
  align-items: stretch;
}
.filter-search {
  flex: 1 1 auto;
  min-width: 0;
}
.filter-type {
  flex: 0 0 200px;
}
.filter-btn {
  flex: 0 0 auto;
  white-space: nowrap;
}

.models-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));
  gap: 16px;
}

.model-card {
  background: var(--bg-secondary);
  border-radius: var(--border-radius);
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.15);
  padding: 16px;
  transition: all var(--transition-fast);
  /* Flex column so the action row can pin to the card bottom regardless
     of how many lines model-name occupies. Grid rows already stretch
     same-height; this lines up the buttons across the row. */
  display: flex;
  flex-direction: column;
}

.model-card:hover {
  box-shadow: 0 4px 16px rgba(0, 0, 0, 0.25);
  transform: translateY(-2px);
}

.model-card.loaded {
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.15), 0 0 0 2px var(--accent-success);
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
  gap: 8px;
  margin-top: auto; /* pin to card bottom — works because parent is flex column */
}

.accordion {
  border-radius: var(--border-radius);
  box-shadow: 0 1px 4px rgba(0, 0, 0, 0.15);
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

/* Upload modal styles */
.upload-dropzone {
  border: 2px dashed var(--border-color, #555);
  border-radius: var(--border-radius);
  padding: 24px;
  text-align: center;
  cursor: pointer;
  transition: background var(--transition-fast), border-color var(--transition-fast);
  background: var(--bg-tertiary);
}

.upload-dropzone:hover {
  border-color: var(--accent-primary);
}

.upload-dropzone.dragging {
  border-color: var(--accent-primary);
  background: var(--bg-secondary);
}

.upload-dropzone.has_file {
  border-style: solid;
}

.upload-dropzone.disabled {
  cursor: not-allowed;
  opacity: 0.6;
}

.upload-prompt {
  font-weight: 500;
  margin-bottom: 6px;
}

.upload-filename {
  font-weight: 600;
  word-break: break-all;
  margin-bottom: 4px;
}

.upload-meta {
  font-size: 12px;
  color: var(--text-secondary);
  margin-bottom: 8px;
}

.upload-progress {
  margin-top: 12px;
}

.upload-progress-bar {
  width: 100%;
  height: 8px;
  background: var(--bg-tertiary);
  border-radius: 4px;
  overflow: hidden;
}

.upload-progress-fill {
  height: 100%;
  background: var(--accent-primary);
  transition: width 0.15s linear;
}

.upload-progress-text {
  margin-top: 6px;
  font-size: 12px;
  color: var(--text-secondary);
  text-align: right;
}
</style>
