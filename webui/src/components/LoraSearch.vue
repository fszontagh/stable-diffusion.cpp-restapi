<script setup lang="ts">
import { ref, computed } from 'vue'
import type { ModelInfo } from '../api/client'

const props = defineProps<{
  availableLoras: ModelInfo[]
  excludeNames: string[]
}>()

const emit = defineEmits<{
  'select': [name: string]
}>()

const searchQuery = ref('')
const isOpen = ref(false)
const highlightedIndex = ref(-1)

const filteredLoras = computed(() => {
  const query = searchQuery.value.toLowerCase().trim()
  const excluded = new Set(props.excludeNames)

  return props.availableLoras
    .filter(lora => !excluded.has(lora.name))
    .filter(lora => query === '' || lora.name.toLowerCase().includes(query))
    .slice(0, 20) // Limit results for performance
})

function selectLora(name: string) {
  emit('select', name)
  searchQuery.value = ''
  isOpen.value = false
  highlightedIndex.value = -1
}

function handleFocus() {
  isOpen.value = true
}

function handleBlur() {
  // Delay to allow click on dropdown item
  setTimeout(() => {
    isOpen.value = false
    highlightedIndex.value = -1
  }, 150)
}

function handleKeydown(event: KeyboardEvent) {
  if (!isOpen.value && event.key !== 'Escape') {
    isOpen.value = true
  }

  switch (event.key) {
    case 'ArrowDown':
      event.preventDefault()
      highlightedIndex.value = Math.min(
        highlightedIndex.value + 1,
        filteredLoras.value.length - 1
      )
      break
    case 'ArrowUp':
      event.preventDefault()
      highlightedIndex.value = Math.max(highlightedIndex.value - 1, 0)
      break
    case 'Enter':
      event.preventDefault()
      if (highlightedIndex.value >= 0 && highlightedIndex.value < filteredLoras.value.length) {
        selectLora(filteredLoras.value[highlightedIndex.value].name)
      }
      break
    case 'Escape':
      isOpen.value = false
      highlightedIndex.value = -1
      break
  }
}
</script>

<template>
  <div class="lora-search">
    <input
      v-model="searchQuery"
      type="text"
      class="lora-search-input"
      placeholder="Search LoRAs..."
      @focus="handleFocus"
      @blur="handleBlur"
      @keydown="handleKeydown"
    />
    <div v-if="isOpen && filteredLoras.length > 0" class="lora-dropdown">
      <div
        v-for="(lora, index) in filteredLoras"
        :key="lora.name"
        class="lora-dropdown-item"
        :class="{ 'highlighted': index === highlightedIndex }"
        @mousedown.prevent="selectLora(lora.name)"
        @mouseenter="highlightedIndex = index"
      >
        <span class="lora-dropdown-name">{{ lora.name }}</span>
        <span class="lora-dropdown-ext">.{{ lora.file_extension }}</span>
      </div>
    </div>
    <div v-else-if="isOpen && searchQuery && filteredLoras.length === 0" class="lora-dropdown">
      <div class="lora-dropdown-empty">No LoRAs found</div>
    </div>
  </div>
</template>

<style scoped>
.lora-search {
  position: relative;
}

.lora-search-input {
  width: 100%;
  padding: 0.5rem;
  font-size: 0.875rem;
  background: var(--bg-primary, #1a1a1a);
  border: 1px solid var(--border-color, #444);
  border-radius: 4px;
  color: var(--text-primary, #fff);
}

.lora-search-input:focus {
  outline: none;
  border-color: var(--primary-color, #007bff);
}

.lora-search-input::placeholder {
  color: var(--text-secondary, #888);
}

.lora-dropdown {
  position: absolute;
  top: 100%;
  left: 0;
  right: 0;
  max-height: 200px;
  overflow-y: auto;
  background: var(--bg-secondary, #2a2a2a);
  border: 1px solid var(--border-color, #444);
  border-top: none;
  border-radius: 0 0 4px 4px;
  z-index: 100;
}

.lora-dropdown-item {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 0.5rem;
  cursor: pointer;
  border-bottom: 1px solid var(--border-color, #333);
}

.lora-dropdown-item:last-child {
  border-bottom: none;
}

.lora-dropdown-item:hover,
.lora-dropdown-item.highlighted {
  background: var(--bg-hover, #333);
}

.lora-dropdown-name {
  flex: 1;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  font-size: 0.875rem;
}

.lora-dropdown-ext {
  color: var(--text-secondary, #888);
  font-size: 0.75rem;
  margin-left: 0.5rem;
}

.lora-dropdown-empty {
  padding: 0.5rem;
  color: var(--text-secondary, #888);
  font-size: 0.875rem;
  text-align: center;
}
</style>
