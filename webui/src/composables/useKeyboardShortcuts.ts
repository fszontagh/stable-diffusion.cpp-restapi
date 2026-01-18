import { onMounted, onUnmounted } from 'vue'
import { useRouter } from 'vue-router'

export interface KeyboardShortcut {
  key: string
  ctrl?: boolean
  shift?: boolean
  alt?: boolean
  meta?: boolean
  description: string
  action: () => void
}

const registeredShortcuts = new Map<string, KeyboardShortcut>()

/**
 * Generate a unique key for the shortcut combination
 */
function getShortcutKey(shortcut: KeyboardShortcut): string {
  const parts = []
  if (shortcut.ctrl) parts.push('Ctrl')
  if (shortcut.shift) parts.push('Shift')
  if (shortcut.alt) parts.push('Alt')
  if (shortcut.meta) parts.push('Meta')
  parts.push(shortcut.key.toUpperCase())
  return parts.join('+')
}

/**
 * Check if the event matches the shortcut
 */
function matchesShortcut(event: KeyboardEvent, shortcut: KeyboardShortcut): boolean {
  return (
    event.key.toLowerCase() === shortcut.key.toLowerCase() &&
    !!event.ctrlKey === !!shortcut.ctrl &&
    !!event.shiftKey === !!shortcut.shift &&
    !!event.altKey === !!shortcut.alt &&
    !!event.metaKey === !!shortcut.meta
  )
}

/**
 * Check if the current element is an input field
 */
function isInputElement(element: Element | null): boolean {
  if (!element) return false
  const tagName = element.tagName.toLowerCase()
  return ['input', 'textarea', 'select'].includes(tagName) ||
         (element as HTMLElement).isContentEditable
}

/**
 * Global keyboard event handler
 */
function handleKeyboardEvent(event: KeyboardEvent) {
  // Don't trigger shortcuts when typing in input fields
  if (isInputElement(event.target as Element)) {
    return
  }

  for (const shortcut of registeredShortcuts.values()) {
    if (matchesShortcut(event, shortcut)) {
      event.preventDefault()
      shortcut.action()
      break
    }
  }
}

/**
 * Register a keyboard shortcut
 */
export function registerShortcut(shortcut: KeyboardShortcut): () => void {
  const key = getShortcutKey(shortcut)
  registeredShortcuts.set(key, shortcut)

  // Return unregister function
  return () => {
    registeredShortcuts.delete(key)
  }
}

/**
 * Get all registered shortcuts
 */
export function getAllShortcuts(): KeyboardShortcut[] {
  return Array.from(registeredShortcuts.values())
}

/**
 * Composable for using keyboard shortcuts in components
 */
export function useKeyboardShortcuts() {
  const router = useRouter()

  onMounted(() => {
    // Register default navigation shortcuts
    registerShortcut({
      key: 'k',
      ctrl: true,
      description: 'Go to Queue',
      action: () => router.push('/queue')
    })

    registerShortcut({
      key: 'g',
      ctrl: true,
      description: 'Go to Generate',
      action: () => router.push('/generate')
    })

    registerShortcut({
      key: 'm',
      ctrl: true,
      description: 'Go to Models',
      action: () => router.push('/models')
    })

    registerShortcut({
      key: 'd',
      ctrl: true,
      description: 'Go to Dashboard',
      action: () => router.push('/dashboard')
    })

    registerShortcut({
      key: '/',
      ctrl: true,
      description: 'Show keyboard shortcuts',
      action: () => {
        // Emit custom event to show shortcuts modal
        window.dispatchEvent(new CustomEvent('show-shortcuts-help'))
      }
    })

    // Add global listener if not already added
    if (!document.body.hasAttribute('data-shortcuts-listener')) {
      document.addEventListener('keydown', handleKeyboardEvent)
      document.body.setAttribute('data-shortcuts-listener', 'true')
    }
  })

  onUnmounted(() => {
    // Cleanup is handled globally
  })

  return {
    registerShortcut,
    getAllShortcuts
  }
}
