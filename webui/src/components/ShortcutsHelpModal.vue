<template>
  <Modal
    :show="isOpen"
    title="Keyboard Shortcuts"
    @close="close"
  >
    <div class="shortcuts-help">
      <div v-if="shortcutGroups.length === 0" class="no-shortcuts">
        <p>No keyboard shortcuts registered</p>
      </div>
      
      <div v-else class="shortcuts-list">
        <div
          v-for="group in shortcutGroups"
          :key="group.category"
          class="shortcut-group"
        >
          <h3 class="group-title">{{ group.category }}</h3>
          <div class="shortcuts">
            <div
              v-for="shortcut in group.shortcuts"
              :key="shortcut.key"
              class="shortcut-item"
            >
              <div class="shortcut-key">
                <kbd v-for="(key, index) in formatKey(shortcut.key)" :key="index">
                  {{ key }}
                </kbd>
              </div>
              <div class="shortcut-description">
                {{ shortcut.description }}
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>

    <template #footer>
      <button
        class="btn btn-primary"
        @click="close"
        aria-label="Close shortcuts help dialog"
      >
        Close
      </button>
    </template>
  </Modal>
</template>

<script setup lang="ts">
import { ref, computed } from 'vue'
import Modal from './Modal.vue'

interface Shortcut {
  key: string
  description: string
  category: string
}

const isOpen = ref(false)
const shortcuts = ref<Shortcut[]>([])

const shortcutGroups = computed(() => {
  const groups = new Map<string, Shortcut[]>()
  
  shortcuts.value.forEach(shortcut => {
    if (!groups.has(shortcut.category)) {
      groups.set(shortcut.category, [])
    }
    groups.get(shortcut.category)!.push(shortcut)
  })
  
  return Array.from(groups.entries()).map(([category, categoryShortcuts]) => ({
    category,
    shortcuts: categoryShortcuts.sort((a, b) => a.key.localeCompare(b.key))
  }))
})

const formatKey = (key: string): string[] => {
  // Split combined keys like "Ctrl+K" into separate parts
  return key.split('+').map(k => {
    // Capitalize first letter for better display
    return k.charAt(0).toUpperCase() + k.slice(1)
  })
}

const show = (registeredShortcuts: Shortcut[]) => {
  shortcuts.value = registeredShortcuts
  isOpen.value = true
}

const close = () => {
  isOpen.value = false
}

defineExpose({
  show,
  close
})
</script>

<style scoped>
.shortcuts-help {
  padding: 1rem 0;
}

.no-shortcuts {
  text-align: center;
  padding: 2rem;
  color: var(--text-muted);
}

.shortcuts-list {
  display: flex;
  flex-direction: column;
  gap: 2rem;
}

.shortcut-group {
  display: flex;
  flex-direction: column;
  gap: 0.75rem;
}

.group-title {
  font-size: 1.125rem;
  font-weight: 600;
  color: var(--text-primary);
  margin: 0;
  padding-bottom: 0.5rem;
  border-bottom: 1px solid var(--border-color);
}

.shortcuts {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.shortcut-item {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0.5rem;
  border-radius: 4px;
  transition: background-color 0.2s;
}

.shortcut-item:hover {
  background-color: var(--bg-secondary);
}

.shortcut-key {
  display: flex;
  gap: 0.25rem;
  align-items: center;
  min-width: 120px;
}

.shortcut-key kbd {
  display: inline-block;
  padding: 0.25rem 0.5rem;
  font-family: var(--font-mono, 'Courier New', monospace);
  font-size: 0.875rem;
  color: var(--text-primary);
  background-color: var(--bg-tertiary);
  border: 1px solid var(--border-color);
  border-radius: 4px;
  box-shadow: 0 1px 2px rgba(0, 0, 0, 0.1);
  min-width: 2rem;
  text-align: center;
}

.shortcut-description {
  flex: 1;
  color: var(--text-secondary);
  font-size: 0.9375rem;
}

.btn {
  padding: 0.5rem 1.5rem;
  border: none;
  border-radius: 4px;
  font-size: 0.9375rem;
  cursor: pointer;
  transition: all 0.2s;
}

.btn-primary {
  background-color: var(--primary-color);
  color: white;
}

.btn-primary:hover {
  background-color: var(--primary-hover);
}

.btn-primary:focus-visible {
  outline: 2px solid var(--primary-color);
  outline-offset: 2px;
}
</style>
