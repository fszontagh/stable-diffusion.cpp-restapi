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

/**
 * Score a component for suggestion based on weight type matching
 */
function scoreComponent(
  componentName: string,
  modelWeightType: string | null,
  architectureId: string | null
): number {
  let score = 0
  const name = componentName.toLowerCase()

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

    // Flux/Z-Image use ae.gguf style VAE
    if ((archLower.includes('flux') || archLower.includes('z-image') || archLower.includes('zimage'))
        && name.includes('ae')) {
      score += 80
    }

    // Z-Image prefers Qwen3 4B for LLM
    if (archLower.includes('z-image') || archLower.includes('zimage')) {
      if (name.includes('qwen') && (name.includes('4b') || name.includes('3-4b'))) {
        score += 90
      }
    }

    // SDXL uses specific VAE
    if (archLower.includes('sdxl') && name.includes('sdxl')) {
      score += 80
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
 * Sort and score components for suggestion
 */
export function suggestComponents(
  _componentType: 'vae' | 'clip' | 't5' | 'llm' | 'controlnet' | 'taesd',
  architectureId: string | null,
  modelWeightType: string | null,
  availableModels: ModelInfo[]
): ComponentSuggestion[] {
  return availableModels
    .map(model => ({
      name: model.name,
      score: scoreComponent(model.name, modelWeightType, architectureId),
      isSuggested: false
    }))
    .sort((a, b) => b.score - a.score)
    .map((item, index) => ({
      ...item,
      isSuggested: index === 0 && item.score > 50  // Only suggest if score is significant
    }))
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
