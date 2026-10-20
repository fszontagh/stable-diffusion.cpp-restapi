import type { LoadPrefill } from './archReadiness'

// Hands a pre-filled load form from the Downloads / Models pages to the
// model load page. Kept in sessionStorage so a page reload on the load page
// still sees it; it is consumed on first read.
const KEY = 'sdcpp_load_prefill'

export function setLoadPrefill(prefill: LoadPrefill): void {
  try {
    sessionStorage.setItem(KEY, JSON.stringify(prefill))
  } catch {
    // Storage blocked: the load page falls back to auto-detection.
  }
}

/** Returns and clears the pre-fill when it is for `modelName`. */
export function takeLoadPrefill(modelName: string): LoadPrefill | null {
  try {
    const raw = sessionStorage.getItem(KEY)
    if (!raw) return null
    const prefill = JSON.parse(raw) as LoadPrefill
    if (prefill.model_name !== modelName) return null
    sessionStorage.removeItem(KEY)
    return prefill
  } catch {
    return null
  }
}

export function loadPagePath(modelName: string): string {
  return `/models/load/${encodeURIComponent(modelName)}`
}
