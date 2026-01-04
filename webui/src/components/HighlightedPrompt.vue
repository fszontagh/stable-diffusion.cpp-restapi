<script setup lang="ts">
import { ref, computed, watch, onMounted, nextTick } from 'vue'
import { useAppStore } from '../stores/app'

const props = defineProps<{
  modelValue: string
  placeholder?: string
  rows?: number
}>()

const emit = defineEmits<{
  'update:modelValue': [value: string]
}>()

const appStore = useAppStore()
const textareaRef = ref<HTMLTextAreaElement | null>(null)
const highlightRef = ref<HTMLDivElement | null>(null)

// Get available embeddings from app store
const embeddingNames = computed(() => {
  if (!appStore.models?.embeddings) return []
  return appStore.models.embeddings.map(e => {
    // Get just the filename without extension and path
    const name = e.name.split('/').pop()?.replace(/\.(pt|safetensors|bin)$/i, '') || e.name
    return name
  })
})

// Find duplicate words/phrases in text
function findDuplicates(text: string): Set<string> {
  const duplicates = new Set<string>()

  // Split by commas and clean up
  const parts = text.split(',').map(p => p.trim().toLowerCase()).filter(p => p.length > 2)

  // Count occurrences
  const counts = new Map<string, number>()
  for (const part of parts) {
    counts.set(part, (counts.get(part) || 0) + 1)
  }

  // Find duplicates
  for (const [word, count] of counts) {
    if (count > 1) {
      duplicates.add(word)
    }
  }

  return duplicates
}

// Generate highlighted HTML from prompt text
const highlightedHtml = computed(() => {
  let text = props.modelValue || ''
  if (!text) return '&nbsp;' // Placeholder for empty content

  const duplicates = findDuplicates(text)

  // Escape HTML first
  text = text
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')

  // Highlight LoRA syntax: <lora:name:weight> or <lora:name>
  text = text.replace(
    /&lt;lora:([^:&]+)(?::([^&]+))?&gt;/gi,
    '<span class="hl-lora">&lt;lora:$1' + (text.includes(':') ? ':$2' : '') + '&gt;</span>'
  )

  // Better LoRA regex after escaping
  text = text.replace(
    /&lt;lora:([^&]+)&gt;/gi,
    (match) => `<span class="hl-lora">${match}</span>`
  )

  // Highlight embeddings
  for (const embName of embeddingNames.value) {
    // Match embedding name as whole word (case insensitive)
    const escapedName = embName.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')
    const regex = new RegExp(`\\b(${escapedName})\\b`, 'gi')
    text = text.replace(regex, '<span class="hl-embedding">$1</span>')
  }

  // Highlight duplicates (check each comma-separated part)
  if (duplicates.size > 0) {
    // Split by comma, process each part, rejoin
    const parts = text.split(',')
    text = parts.map(part => {
      const trimmedLower = part.trim().toLowerCase()
        .replace(/<[^>]+>/g, '') // Remove HTML tags for comparison
        .replace(/&lt;|&gt;|&amp;/g, '') // Remove escaped HTML entities

      if (duplicates.has(trimmedLower)) {
        // Wrap the content (not the leading/trailing whitespace)
        return part.replace(/^(\s*)(.+?)(\s*)$/, '$1<span class="hl-duplicate">$2</span>$3')
      }
      return part
    }).join(',')
  }

  // Preserve newlines
  text = text.replace(/\n/g, '<br>')

  // Add trailing space to match textarea behavior
  if (props.modelValue?.endsWith('\n')) {
    text += '<br>&nbsp;'
  }

  return text
})

// Sync scroll position
function syncScroll() {
  if (textareaRef.value && highlightRef.value) {
    highlightRef.value.scrollTop = textareaRef.value.scrollTop
    highlightRef.value.scrollLeft = textareaRef.value.scrollLeft
  }
}

// Handle input
function handleInput(event: Event) {
  const target = event.target as HTMLTextAreaElement
  emit('update:modelValue', target.value)
}

// Watch for external changes and sync scroll
watch(() => props.modelValue, () => {
  nextTick(syncScroll)
})

onMounted(() => {
  // Fetch models if not loaded (for embeddings list)
  if (!appStore.models) {
    appStore.fetchModels()
  }
})
</script>

<template>
  <div class="highlighted-prompt-container">
    <div
      ref="highlightRef"
      class="highlight-backdrop"
      v-html="highlightedHtml"
    ></div>
    <textarea
      ref="textareaRef"
      :value="modelValue"
      :placeholder="placeholder"
      :rows="rows || 3"
      class="form-textarea highlight-textarea"
      @input="handleInput"
      @scroll="syncScroll"
    ></textarea>
  </div>
</template>

<style scoped>
.highlighted-prompt-container {
  position: relative;
  width: 100%;
}

.highlight-backdrop {
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  padding: 10px 14px;
  font-family: inherit;
  font-size: 14px;
  line-height: 1.5;
  white-space: pre-wrap;
  word-wrap: break-word;
  overflow: auto;
  pointer-events: none;
  color: transparent;
  background: var(--bg-tertiary);
  border: 1px solid transparent;
  border-radius: var(--border-radius-sm);
}

.highlight-textarea {
  position: relative;
  background: transparent !important;
  color: var(--text-primary);
  caret-color: var(--text-primary);
  resize: vertical;
  z-index: 1;
}

/* Highlight styles */
.highlight-backdrop :deep(.hl-lora) {
  background: rgba(139, 92, 246, 0.25);
  color: #a78bfa;
  border-radius: 3px;
  padding: 0 2px;
}

.highlight-backdrop :deep(.hl-embedding) {
  background: rgba(34, 197, 94, 0.25);
  color: #4ade80;
  border-radius: 3px;
  padding: 0 2px;
}

.highlight-backdrop :deep(.hl-duplicate) {
  background: rgba(251, 191, 36, 0.3);
  border-bottom: 2px wavy #f59e0b;
  color: #fbbf24;
}
</style>
