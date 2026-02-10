<script setup lang="ts">
import { ref } from 'vue'

const props = withDefaults(defineProps<{
  title: string
  defaultOpen?: boolean
  variant?: 'tool' | 'thinking' | 'default'
  badge?: string | number
}>(), {
  defaultOpen: false,
  variant: 'default'
})

const isOpen = ref(props.defaultOpen)

function toggle() {
  isOpen.value = !isOpen.value
}
</script>

<template>
  <div class="collapsible-section" :class="[`variant-${variant}`, { open: isOpen }]">
    <button class="collapsible-header" @click="toggle" type="button">
      <span class="collapse-icon">{{ isOpen ? '▼' : '▶' }}</span>
      <span class="title">{{ title }}</span>
      <span v-if="badge !== undefined" class="badge">{{ badge }}</span>
    </button>
    <Transition name="collapse">
      <div v-show="isOpen" class="collapsible-content">
        <slot />
      </div>
    </Transition>
  </div>
</template>

<style scoped>
.collapsible-section {
  margin-top: 10px;
  border-radius: 8px;
  overflow: hidden;
}

.variant-thinking {
  background: rgba(147, 51, 234, 0.1);
  border: 1px solid rgba(147, 51, 234, 0.3);
}

.variant-tool {
  background: rgba(59, 130, 246, 0.1);
  border: 1px solid rgba(59, 130, 246, 0.3);
}

.variant-default {
  background: var(--bg-tertiary);
  border: 1px solid var(--border-color);
}

.collapsible-header {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 8px 12px;
  width: 100%;
  background: transparent;
  border: none;
  cursor: pointer;
  font-size: 12px;
  color: var(--text-secondary);
  text-align: left;
  transition: background 0.15s;
}

.collapsible-header:hover {
  background: rgba(255, 255, 255, 0.05);
}

.collapse-icon {
  font-size: 10px;
  transition: transform 0.2s;
  width: 12px;
  text-align: center;
}

.title {
  font-weight: 500;
}

.badge {
  margin-left: auto;
  padding: 2px 8px;
  border-radius: 10px;
  font-size: 11px;
  font-weight: 600;
}

.variant-thinking .badge {
  background: rgba(147, 51, 234, 0.2);
  color: rgb(192, 132, 252);
}

.variant-tool .badge {
  background: rgba(59, 130, 246, 0.2);
  color: rgb(147, 197, 253);
}

.variant-default .badge {
  background: var(--bg-secondary);
  color: var(--text-muted);
}

.collapsible-content {
  padding: 8px 12px;
  font-size: 13px;
  border-top: 1px solid rgba(255, 255, 255, 0.1);
}

/* Collapse transition */
.collapse-enter-active,
.collapse-leave-active {
  transition: all 0.2s ease;
  max-height: 500px;
  overflow: hidden;
}

.collapse-enter-from,
.collapse-leave-to {
  max-height: 0;
  opacity: 0;
  padding-top: 0;
  padding-bottom: 0;
}
</style>
