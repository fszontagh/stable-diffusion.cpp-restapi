// Global confirmation modal for prompt expansion. Any code that submits a
// generation job and might trigger expand_prompt should funnel through
// openExpandPromptConfirm() — it parses the template, computes the variation
// count, and shows the modal so the user explicitly confirms creating N
// queue items.
//
// State is module-scoped (singleton) so the same modal is shared by:
//   - Generate.vue submit button (user-driven path)
//   - Assistant action handler (programmatic path — when the LLM emits a
//     `generate` action whose prompt has template syntax)
//
// The modal itself is mounted once at the App.vue level via
// <ExpandPromptConfirmModal />; it reads the same module state.

import { ref } from 'vue'
import { expandPrompt, hasTemplateSyntax } from '../utils/promptTemplate'

export interface ExpandPromptConfirmRequest {
  prompt: string
  batchCount?: number
  jobType?: 'txt2img' | 'img2img' | 'txt2vid'
  // Tag for telemetry / wording — does not affect logic.
  source?: 'user' | 'assistant'
}

export interface ExpandPromptConfirmResult {
  // True if the user clicked Confirm. False on Cancel, ESC, backdrop click,
  // or parse failure (parseError set in that case).
  confirmed: boolean
  variations?: string[]
  parseError?: string
}

interface ModalState {
  open: boolean
  request: ExpandPromptConfirmRequest | null
  variations: string[]
  parseError: string | null
  // Private — never expose.
  resolver: ((r: ExpandPromptConfirmResult) => void) | null
}

const state = ref<ModalState>({
  open: false,
  request: null,
  variations: [],
  parseError: null,
  resolver: null,
})

/**
 * Open the confirmation modal. Returns a promise that resolves when the
 * user clicks Confirm or Cancel (or the modal is dismissed). If the prompt
 * contains no template syntax, resolves immediately with confirmed=true and
 * a single-element variations array — callers don't need to special-case
 * the no-expansion path.
 */
export function openExpandPromptConfirm(
  req: ExpandPromptConfirmRequest
): Promise<ExpandPromptConfirmResult> {
  // Fast path: no template syntax — nothing to confirm, single-prompt run.
  if (!hasTemplateSyntax(req.prompt)) {
    return Promise.resolve({ confirmed: true, variations: [req.prompt] })
  }

  return new Promise(resolve => {
    let variations: string[] = []
    let parseError: string | null = null
    try {
      variations = expandPrompt(req.prompt)
    } catch (e) {
      parseError = e instanceof Error ? e.message : String(e)
    }
    state.value = {
      open: true,
      request: req,
      variations,
      parseError,
      resolver: resolve,
    }
  })
}

/**
 * Component bindings — used only by ExpandPromptConfirmModal.vue. Returns
 * the reactive state plus confirm/cancel handlers. Don't use these from
 * application code; call openExpandPromptConfirm() instead.
 */
export function useExpandPromptModalBindings() {
  function confirm() {
    const r = state.value.resolver
    const variations = state.value.variations
    state.value.open = false
    state.value.resolver = null
    r?.({ confirmed: true, variations })
  }
  function cancel() {
    const r = state.value.resolver
    state.value.open = false
    state.value.resolver = null
    r?.({ confirmed: false })
  }
  return { state, confirm, cancel }
}
