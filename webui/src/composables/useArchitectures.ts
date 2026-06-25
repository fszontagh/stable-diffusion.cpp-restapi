import { ref, computed } from 'vue'
import { api, type ArchitecturePreset, type ModelInfo } from '../api/client'
import { useAppStore } from '../stores/app'

/**
 * Extract weight/quantization type from a model filename
 * Examples:
 *   flux1-dev-Q8_0.gguf → 'q8_0'
 *   sdxl_turbo.f16.safetensors → 'f16'
 *   model.safetensors → null
 */
export function extractWeightType(filename: string): string | null {
  const name = filename.toLowerCase()

  // Common quantization patterns
  const patterns = [
    /[_\-\.]q8[_\-]?0/,     // q8_0, q8-0, .q8_0
    /[_\-\.]q5[_\-]?0/,     // q5_0
    /[_\-\.]q5[_\-]?1/,     // q5_1
    /[_\-\.]q4[_\-]?0/,     // q4_0
    /[_\-\.]q4[_\-]?1/,     // q4_1
    /[_\-\.]q4[_\-]?k/,     // q4_k
    /[_\-\.]q5[_\-]?k/,     // q5_k
    /[_\-\.]q6[_\-]?k/,     // q6_k
    /[_\-\.]f32/,           // f32
    /[_\-\.]f16/,           // f16
    /[_\-\.]bf16/,          // bf16
  ]

  for (const pattern of patterns) {
    const match = name.match(pattern)
    if (match) {
      // Normalize the match (remove leading punctuation, standardize format)
      return match[0].replace(/^[_\-\.]/, '').replace('-', '_').toLowerCase()
    }
  }

  return null
}

type ComponentType = 'vae' | 'clip' | 't5' | 'llm' | 'controlnet' | 'taesd'

/**
 * Score a component for suggestion. Two scoring paths run in sequence and
 * accumulate:
 *   1. JSON-driven scoring from preset.componentScoring (the preferred path —
 *      adding a new architecture is a model_architectures.json change only).
 *   2. Legacy hardcoded scoring for architectures that don't have explicit
 *      componentScoring rules in their preset (kept so we don't have to
 *      backfill every existing preset at once).
 *
 * componentType identifies which scoring rule list to read; weight-type
 * matching is universal and runs regardless of preset content.
 */
function scoreComponent(
  componentType: ComponentType,
  componentName: string,
  modelWeightType: string | null,
  preset: ArchitecturePreset | null
): number {
  let score = 0
  const name = componentName.toLowerCase()
  const architectureId = preset?.id ?? null

  // Weight type matching
  if (modelWeightType) {
    const componentWeight = extractWeightType(componentName)
    if (componentWeight === modelWeightType) {
      score += 100  // Exact weight type match
    } else if (componentWeight && modelWeightType) {
      // Similar quantization level
      const modelQuant = parseInt(modelWeightType.match(/\d+/)?.[0] || '0')
      const compQuant = parseInt(componentWeight.match(/\d+/)?.[0] || '0')
      if (Math.abs(modelQuant - compQuant) <= 1) {
        score += 50  // Close quantization level
      }
    }
  }

  // Architecture-specific patterns
  if (architectureId) {
    const archLower = architectureId.toLowerCase()

    // Strip directory + extension so we score on the file stem only.
    // Without this, a substring match like `name.includes('ae')` would
    // hit *every* filename with the letters "ae" — `sdxl_vae.safetensors`
    // would score the same as `ae.safetensors` for Z-Image/Flux and the
    // sort tiebreak picked the wrong VAE.
    const basename = name.split('/').pop() || name
    const stem = basename.replace(/\.(safetensors|gguf|bin|pt|pth|ckpt)$/i, '')

    // Flux/Z-Image use a VAE literally named `ae[.ext]` (sometimes with
    // a quant/precision suffix like `ae_fp16` or `ae-q8_0`). Match the
    // stem strictly: starts with "ae" followed by a separator or end.
    // This excludes `sdxl_vae`, `vae-ft-mse-...`, `qwen_image_vae`, etc.
    if (archLower.includes('flux') || archLower.includes('z-image') || archLower.includes('zimage')) {
      if (/^ae(?:[_.\-]|$)/i.test(stem)) {
        score += 80
      }
      // Strongly demote SDXL-style VAEs for these architectures so a
      // partial match elsewhere can't accidentally promote them.
      if (stem.toLowerCase().includes('sdxl') || /(^|[_.\-])vae([_.\-]|$)/i.test(stem)) {
        score -= 40
      }
    }

    // Z-Image prefers Qwen3 4B for LLM
    if (archLower.includes('z-image') || archLower.includes('zimage')) {
      if (name.includes('qwen') && (name.includes('4b') || name.includes('3-4b'))) {
        score += 90
      }
    }

    // SeFi-Image scoring lives in the preset's componentScoring JSON now,
    // not here — see model_architectures.json. The legacy block below is
    // kept for architectures that don't have componentScoring rules yet.

    // Anima prefers Qwen3 0.6B for LLM and qwen_image_vae for VAE
    if (archLower.includes('anima')) {
      if (name.includes('qwen') && (name.includes('0.6b') || name.includes('06b') || name.includes('0_6b'))) {
        score += 95  // Strong preference for 0.6B
      }
      if (name.includes('qwen_image_vae') || name.includes('qwen-image-vae')) {
        score += 90
      }
    }

    // Qwen models use qwen_image_vae
    if (archLower.includes('qwen') && (name.includes('qwen_image_vae') || name.includes('qwen-image-vae'))) {
      score += 85
    }

    // SDXL uses specific VAE — match the SDXL token in the stem so it
    // doesn't fire when the path happens to contain "sdxl" elsewhere.
    if (archLower.includes('sdxl') && /(^|[_.\-])sdxl([_.\-]|$)/i.test(stem)) {
      score += 80
    }
  }

  // JSON-driven scoring: each rule is { regex, score } applied against the
  // raw (not stemmed) component filename. Rules accumulate, and bad regex
  // strings silently skip so a malformed JSON entry doesn't poison the
  // whole sort. This is the path new architectures should use.
  const rules = preset?.componentScoring?.[componentType]
  if (rules && Array.isArray(rules)) {
    for (const rule of rules) {
      if (!rule?.regex || typeof rule.score !== 'number') continue
      try {
        if (new RegExp(rule.regex, 'i').test(name)) {
          score += rule.score
        }
      } catch {
        // Malformed regex in JSON — skip silently
      }
    }
  }

  return score
}

export interface ComponentSuggestion {
  name: string
  score: number
  isSuggested: boolean
}

/**
 * Sort and score components for suggestion. Pass the resolved preset (not
 * just the architecture ID) so the JSON-driven componentScoring rules
 * apply. Callers that only have an architecture ID can look up the preset
 * via store.architectures.architectures[id] or use findPresetForArchitecture.
 */
export function suggestComponents(
  componentType: ComponentType,
  preset: ArchitecturePreset | null,
  modelWeightType: string | null,
  availableModels: ModelInfo[]
): ComponentSuggestion[] {
  return availableModels
    .map(model => ({
      name: model.name,
      score: scoreComponent(componentType, model.name, modelWeightType, preset),
      isSuggested: false
    }))
    .sort((a, b) => b.score - a.score)
    .map((item, index) => ({
      ...item,
      isSuggested: index === 0 && item.score > 50  // Only suggest if score is significant
    }))
}

/**
 * Resolve the right preset for a reported architecture, using the JSON
 * `match` rules. When multiple presets share `match.architecture` (e.g. the
 * three SeFi family variants), filename regexes disambiguate. Presets
 * without `match` are treated as having `match.architecture = key` so
 * existing single-preset architectures keep working without JSON changes.
 *
 * Lookup order:
 *   1. Exact key match (preserves existing behavior for non-match presets).
 *   2. Among presets whose effective architecture equals reportedArchitecture,
 *      prefer those whose nameRegex matches the modelName.
 *   3. Among the same set, fall back to the preset without nameRegex.
 *   4. Case-insensitive key match.
 *   5. Partial match (longest key wins).
 */
export function findPresetForArchitecture(
  reportedArchitecture: string | null,
  modelName: string | null,
  allPresets: Record<string, ArchitecturePreset> | null | undefined
): ArchitecturePreset | null {
  if (!reportedArchitecture || !allPresets) return null

  // 1. Direct key hit — fastest, no surprises for legacy architectures
  if (allPresets[reportedArchitecture]) return allPresets[reportedArchitecture]

  // 2/3. Match-rule scan
  const arch = reportedArchitecture
  const archLower = arch.toLowerCase()
  const haystack = (modelName ?? '').toLowerCase()
  const matched: ArchitecturePreset[] = []
  const fallbacks: ArchitecturePreset[] = []
  for (const preset of Object.values(allPresets)) {
    const effectiveArch = preset.match?.architecture ?? preset.id
    if (!effectiveArch) continue
    if (effectiveArch !== arch && effectiveArch.toLowerCase() !== archLower) continue
    if (preset.match?.nameRegex) {
      try {
        if (new RegExp(preset.match.nameRegex, 'i').test(haystack)) {
          matched.push(preset)
        }
      } catch {
        // Bad regex in JSON — skip
      }
    } else {
      fallbacks.push(preset)
    }
  }
  if (matched.length > 0) return matched[0]
  if (fallbacks.length > 0) return fallbacks[0]

  // 4. Case-insensitive key match
  for (const [key, value] of Object.entries(allPresets)) {
    if (key.toLowerCase() === archLower) return value
  }

  // 5. Partial match — prefer longest key
  let best: ArchitecturePreset | null = null
  let bestLen = 0
  for (const [key, value] of Object.entries(allPresets)) {
    const keyLower = key.toLowerCase()
    if (archLower.includes(keyLower) || keyLower.includes(archLower)) {
      if (key.length > bestLen) {
        best = value
        bestLen = key.length
      }
    }
  }
  return best
}

/**
 * Composable for architecture detection and component suggestions
 */
export function useArchitectures() {
  const store = useAppStore()
  const detecting = ref(false)
  const detectedArchitecture = ref<ArchitecturePreset | null>(null)
  const detectionError = ref<string | null>(null)

  /**
   * Detect architecture from model name via backend API
   */
  async function detectArchitecture(modelName: string): Promise<ArchitecturePreset | null> {
    detecting.value = true
    detectionError.value = null

    try {
      const result = await api.detectArchitecture(modelName)
      if (result.detected && result.architecture) {
        detectedArchitecture.value = result.architecture
        return result.architecture
      }
      detectedArchitecture.value = null
      return null
    } catch (e) {
      detectionError.value = e instanceof Error ? e.message : 'Detection failed'
      detectedArchitecture.value = null
      return null
    } finally {
      detecting.value = false
    }
  }

  /**
   * Get all available architectures
   */
  const architectures = computed(() => {
    if (!store.architectures?.architectures) return []
    return Object.values(store.architectures.architectures)
  })

  /**
   * Get architecture by ID
   */
  function getArchitectureById(id: string): ArchitecturePreset | undefined {
    return store.architectures?.architectures?.[id]
  }

  return {
    // State
    detecting,
    detectedArchitecture,
    detectionError,
    architectures,

    // Functions
    detectArchitecture,
    getArchitectureById,
    extractWeightType,
    suggestComponents
  }
}
