import type { ArchitectureDownload, ArchitecturePreset, ModelInfo, ModelsResponse } from '../api/client'
import { extractWeightType, suggestComponents, suggestVisionComponents } from './useArchitectures'

// Which /models list holds the files for each load component slot.
const COMPONENT_LISTS: Record<string, keyof ModelsResponse> = {
  vae: 'vae',
  audio_vae: 'vae',
  clip_l: 'clip',
  clip_g: 'clip',
  clip_vision: 'clip',
  t5xxl: 't5',
  embeddings_connectors: 't5',
  llm: 'llm',
  llm_vision: 'llm',
  tokenizer: 'tokenizers',
  taesd: 'taesd',
  motion_module: 'motion_modules',
  controlnet: 'controlnets',
  uncond_diffusion_model: 'diffusion_models',
  high_noise_diffusion_model: 'diffusion_models',
}

// Slots with filename scoring rules in useArchitectures.
const SCORED: Record<string, 'vae' | 'clip' | 't5' | 'llm'> = {
  vae: 'vae', clip_l: 'clip', clip_g: 'clip', t5xxl: 't5', llm: 'llm',
}

// "base" is how the AnimateDiff presets name their SD1.5 checkpoint: it is
// the main model, not an extra component.
const MAIN_COMPONENTS = new Set(['diffusion', 'checkpoint', 'base'])

const TARGET_TYPE_LISTS: Record<string, keyof ModelsResponse> = {
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
  ip_adapter: 'ip_adapters',
}

function listFor(models: ModelsResponse | null | undefined, key: keyof ModelsResponse | undefined): ModelInfo[] {
  if (!models || !key) return []
  const list = models[key]
  return Array.isArray(list) ? (list as ModelInfo[]) : []
}

/**
 * Name of the local file a catalog entry was downloaded to, or null.
 * Single files match on subfolder/filename first, then on the bare basename
 * so files placed by hand still count. Directory bundles match a scanned
 * folder named after the repo.
 */
export function findLocalFile(entry: ArchitectureDownload, models: ModelsResponse | null | undefined): string | null {
  // tokenizer.json lands in the LLM dir but is listed separately.
  const key = entry.component === 'tokenizer' ? 'tokenizers' : TARGET_TYPE_LISTS[entry.target_type]
  const list = listFor(models, key)
  if (list.length === 0) return null

  if (entry.bundle === 'directory' && entry.repo_id) {
    const repoBase = entry.repo_id.split('/').pop()?.toLowerCase() ?? ''
    const hit = list.find(m => {
      if (!m.is_directory) return false
      return (m.name.replace(/\/$/, '').split('/').pop()?.toLowerCase() ?? '') === repoBase
    })
    return hit?.name ?? null
  }

  const target = entry.filename?.split('/').pop()?.toLowerCase()
  if (!target) return null
  if (entry.subfolder) {
    const expected = (entry.subfolder + '/' + target).toLowerCase()
    const hit = list.find(m => m.name.toLowerCase() === expected)
    if (hit) return hit.name
  }
  return list.find(m => (m.name.split('/').pop() ?? '').toLowerCase() === target)?.name ?? null
}

export interface MainCandidate {
  name: string
  modelType: 'checkpoint' | 'diffusion'
  label: string
  recommended: boolean
}

export interface ResolvedComponent {
  key: string
  required: boolean
  description: string
  file: string | null
  // catalog: a downloaded catalog file; suggested: picked by filename scoring
  source: 'catalog' | 'suggested' | null
  // false when the load API has no slot for this component
  supported: boolean
}

export interface ArchReadiness {
  mains: MainCandidate[]
  main: MainCandidate | null
  components: ResolvedComponent[]
  missing: string[]
  ready: boolean
}

function mainCandidates(preset: ArchitecturePreset, models: ModelsResponse | null | undefined): MainCandidate[] {
  const out: MainCandidate[] = []
  const seen = new Set<string>()
  const entries = (preset.downloads ?? []).filter(e => MAIN_COMPONENTS.has(e.component))
  // Recommended files first, then catalog order.
  const ordered = [...entries.filter(e => e.recommended), ...entries.filter(e => !e.recommended)]
  for (const e of ordered) {
    const name = findLocalFile(e, models)
    if (!name || seen.has(name)) continue
    seen.add(name)
    out.push({
      name,
      modelType: e.target_type === 'checkpoint' ? 'checkpoint' : 'diffusion',
      label: e.label,
      recommended: !!e.recommended,
    })
  }
  return out
}

function resolveComponent(
  key: string,
  required: boolean,
  description: string,
  preset: ArchitecturePreset,
  models: ModelsResponse | null | undefined,
  weightType: string | null,
  chosenLlm: string | null
): ResolvedComponent {
  const supported = key in COMPONENT_LISTS
  const base: ResolvedComponent = { key, required, description, file: null, source: null, supported }
  if (!supported) return base

  const entries = (preset.downloads ?? []).filter(e => e.component === key)
  const ordered = [...entries.filter(e => e.recommended), ...entries.filter(e => !e.recommended)]
  for (const e of ordered) {
    const name = findLocalFile(e, models)
    if (name) return { ...base, file: name, source: 'catalog' }
  }

  const list = listFor(models, COMPONENT_LISTS[key])
  let pick: string | undefined
  if (SCORED[key]) {
    pick = suggestComponents(SCORED[key], preset, weightType, list).find(s => s.isSuggested)?.name
  } else if (key === 'llm_vision') {
    pick = suggestVisionComponents(preset, chosenLlm, list).find(s => s.isSuggested)?.name
  } else if (key === 'tokenizer' && list.length === 1) {
    pick = list[0].name
  }
  return pick ? { ...base, file: pick, source: 'suggested' } : base
}

/**
 * Which local files can load this architecture. `mainName` picks one of
 * several downloaded main models (e.g. Q4_K and bf16); default is the
 * recommended one. Ready means a main model and every required component
 * are on disk. Optional components are only resolved for llm_vision, which
 * image editing needs.
 */
export function archReadiness(
  preset: ArchitecturePreset,
  models: ModelsResponse | null | undefined,
  mainName?: string | null
): ArchReadiness {
  const mains = mainCandidates(preset, models)
  const main = mains.find(m => m.name === mainName) ?? mains[0] ?? null
  const weightType = main ? extractWeightType(main.name) : null

  const components: ResolvedComponent[] = []
  for (const [key, desc] of Object.entries(preset.requiredComponents ?? {})) {
    if (MAIN_COMPONENTS.has(key)) continue
    components.push(resolveComponent(key, true, desc, preset, models, weightType, null))
  }
  const llm = components.find(c => c.key === 'llm')?.file ?? null
  const vision = preset.optionalComponents?.llm_vision
  if (vision) components.push(resolveComponent('llm_vision', false, vision, preset, models, weightType, llm))

  const missing = [
    ...(main ? [] : ['main model']),
    ...components.filter(c => c.required && !c.file).map(c => c.key),
  ]
  return { mains, main, components, missing, ready: missing.length === 0 }
}

/** What the model load page needs to open pre-filled for an architecture. */
export interface LoadPrefill {
  architecture: string
  model_name: string
  model_type: 'checkpoint' | 'diffusion'
  components: Record<string, string>
  options: Record<string, unknown>
}

export function buildLoadPrefill(preset: ArchitecturePreset, readiness: ArchReadiness): LoadPrefill | null {
  if (!readiness.main) return null
  const components: Record<string, string> = {}
  for (const c of readiness.components) if (c.file) components[c.key] = c.file
  return {
    architecture: preset.id,
    model_name: readiness.main.name,
    model_type: readiness.main.modelType,
    components,
    options: { ...(preset.loadOptions ?? {}) },
  }
}

// Files sd.cpp's converter can take as input: unquantized weights. GGUF is
// only offered when it is still f16/bf16/f32.
const QUANT_GGUF_RE = /(?:^|[-_.])(?:i?q\d|tq\d)/i
const NO_CONVERT_RE = /mmproj|tokenizer/i

export function isConvertible(name: string): boolean {
  const base = name.replace(/\/$/, '').split('/').pop() ?? ''
  if (NO_CONVERT_RE.test(base)) return false
  if (name.endsWith('/')) return true // sharded folder (index.json / diffusers)
  const lower = base.toLowerCase()
  if (lower.endsWith('.gguf')) return !QUANT_GGUF_RE.test(lower.replace(/\.gguf$/, ''))
  return /\.(safetensors|ckpt|pt|pth|bin)$/.test(lower)
}

export interface ConvertCandidate {
  component: string
  name: string
  modelType: string
}

/**
 * Local files of an architecture that can be converted: the downloaded main
 * models and the resolved components, minus anything already quantized.
 */
export function convertCandidates(
  preset: ArchitecturePreset,
  models: ModelsResponse | null | undefined
): ConvertCandidate[] {
  const r = archReadiness(preset, models)
  const out: ConvertCandidate[] = []
  const seen = new Set<string>()
  const add = (component: string, name: string, modelType: string) => {
    const k = modelType + '::' + name
    if (seen.has(k) || !isConvertible(name)) return
    seen.add(k)
    out.push({ component, name, modelType })
  }
  for (const m of r.mains) add('model', m.name, m.modelType)
  for (const c of r.components) {
    if (!c.file || c.key === 'tokenizer') continue
    const list = listFor(models, COMPONENT_LISTS[c.key])
    const type = list.find(m => m.name === c.file)?.type
    if (type) add(c.key, c.file, type)
  }
  // Other downloaded catalog files of this architecture (e.g. a bf16 LLM
  // when the suggested one is a GGUF).
  for (const e of preset.downloads ?? []) {
    const name = findLocalFile(e, models)
    if (!name) continue
    const key = e.component === 'tokenizer' ? 'tokenizers' : TARGET_TYPE_LISTS[e.target_type]
    const type = listFor(models, key).find(m => m.name === name)?.type
    if (type) add(MAIN_COMPONENTS.has(e.component) ? 'model' : e.component, name, type)
  }
  return out
}
