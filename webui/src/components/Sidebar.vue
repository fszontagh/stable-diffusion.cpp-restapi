<script setup lang="ts">
import { computed } from 'vue'
import { useRoute } from 'vue-router'
import { useAssistantStore } from '../stores/assistant'

const route = useRoute()
const assistantStore = useAssistantStore()

const navItems = computed(() => {
  const items = [
    { path: '/dashboard', name: 'Dashboard', icon: '&#128200;' },
    { path: '/models', name: 'Models', icon: '&#128230;' },
    { path: '/downloads', name: 'Downloads', icon: '&#11015;' },
    { path: '/generate', name: 'Generate', icon: '&#127912;' },
    { path: '/upscale', name: 'Upscale', icon: '&#128269;' },
    { path: '/queue', name: 'Queue', icon: '&#128203;' }
  ]

  // Add chat if assistant is enabled
  if (assistantStore.enabled) {
    items.push({ path: '/chat', name: 'Assistant', icon: '&#129302;' })
  }

  return items
})
</script>

<template>
  <aside class="sidebar">
    <nav class="nav">
      <router-link
        v-for="item in navItems"
        :key="item.path"
        :to="item.path"
        :class="['nav-item', { active: route.path === item.path }]"
      >
        <span class="nav-icon" v-html="item.icon"></span>
        <span class="nav-label">{{ item.name }}</span>
      </router-link>
    </nav>
    <div class="sidebar-footer">
      <a href="/output" target="_blank" class="nav-item">
        <span class="nav-icon">&#128193;</span>
        <span class="nav-label">Output Files</span>
      </a>
    </div>
  </aside>
</template>

<style scoped>
.sidebar {
  width: var(--sidebar-width);
  background: var(--bg-secondary);
  border-right: 1px solid var(--border-color);
  display: flex;
  flex-direction: column;
  flex-shrink: 0;
}

.nav {
  flex: 1;
  padding: 12px 8px;
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.nav-item {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 12px 16px;
  color: var(--text-secondary);
  text-decoration: none;
  border-radius: var(--border-radius-sm);
  transition: all var(--transition-fast);
}

.nav-item:hover {
  background: var(--bg-hover);
  color: var(--text-primary);
  text-decoration: none;
}

.nav-item.active {
  background: var(--bg-tertiary);
  color: var(--accent-primary);
}

.nav-icon {
  font-size: 18px;
  width: 24px;
  text-align: center;
}

.nav-label {
  font-size: 14px;
  font-weight: 500;
}

.sidebar-footer {
  padding: 12px 8px;
  border-top: 1px solid var(--border-color);
}

@media (max-width: 768px) {
  .nav-label {
    display: none;
  }
  .nav-item {
    justify-content: center;
    padding: 12px;
  }
  .nav-icon {
    margin: 0;
  }
}
</style>
