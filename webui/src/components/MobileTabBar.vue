<script setup lang="ts">
import { ref, computed } from 'vue'
import { useRoute } from 'vue-router'
import { useAssistantStore } from '../stores/assistant'

const route = useRoute()
const assistantStore = useAssistantStore()
const showMoreMenu = ref(false)

const primaryTabs = [
  { path: '/dashboard', name: 'Dashboard', icon: '&#128200;' },
  { path: '/models', name: 'Models', icon: '&#128230;' },
  { path: '/generate', name: 'Generate', icon: '&#127912;' },
  { path: '/queue', name: 'Queue', icon: '&#128203;' }
]

const secondaryItems = computed(() => {
  const items = [
    { path: '/downloads', name: 'Downloads', icon: '&#11015;' },
    { path: '/upscale', name: 'Upscale', icon: '&#128269;' },
    { path: '/settings', name: 'Settings', icon: '&#9881;' }
  ]

  if (assistantStore.enabled) {
    items.splice(2, 0, { path: '/chat', name: 'Assistant', icon: '&#129302;' })
  }

  return items
})

function isActive(path: string): boolean {
  return route.path === path
}

function isMoreActive(): boolean {
  return secondaryItems.value.some(item => route.path === item.path)
}

function closeMoreMenu() {
  showMoreMenu.value = false
}
</script>

<template>
  <nav class="mobile-tab-bar">
    <router-link
      v-for="tab in primaryTabs"
      :key="tab.path"
      :to="tab.path"
      :class="['tab-item', { active: isActive(tab.path) }]"
    >
      <span class="tab-icon" v-html="tab.icon"></span>
      <span class="tab-label">{{ tab.name }}</span>
    </router-link>

    <button
      :class="['tab-item', { active: isMoreActive() }]"
      @click="showMoreMenu = !showMoreMenu"
    >
      <span class="tab-icon">&#8942;</span>
      <span class="tab-label">More</span>
    </button>

    <!-- More Menu Overlay -->
    <Teleport to="body">
      <div v-if="showMoreMenu" class="more-menu-overlay" @click="closeMoreMenu">
        <div class="more-menu" @click.stop>
          <router-link
            v-for="item in secondaryItems"
            :key="item.path"
            :to="item.path"
            :class="['more-menu-item', { active: isActive(item.path) }]"
            @click="closeMoreMenu"
          >
            <span class="more-icon" v-html="item.icon"></span>
            <span class="more-label">{{ item.name }}</span>
          </router-link>
          <a href="/output" target="_blank" class="more-menu-item" @click="closeMoreMenu">
            <span class="more-icon">&#128193;</span>
            <span class="more-label">Output Files</span>
            <span class="external-icon">&#8599;</span>
          </a>
        </div>
      </div>
    </Teleport>
  </nav>
</template>

<style scoped>
.mobile-tab-bar {
  display: none;
  position: fixed;
  bottom: 0;
  left: 0;
  right: 0;
  height: 56px;
  background: var(--bg-secondary);
  box-shadow: 0 -2px 10px rgba(0, 0, 0, 0.3);
  justify-content: space-around;
  align-items: center;
  z-index: 1000;
  padding-bottom: env(safe-area-inset-bottom, 0);
}

@media (max-width: 640px) {
  .mobile-tab-bar {
    display: flex;
  }
}

.tab-item {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 2px;
  padding: 8px 12px;
  color: var(--text-secondary);
  text-decoration: none;
  font-size: 10px;
  background: none;
  border: none;
  cursor: pointer;
  transition: color 0.15s;
  min-width: 56px;
}

.tab-item:hover,
.tab-item.active {
  color: var(--accent-primary);
}

.tab-icon {
  font-size: 20px;
  line-height: 1;
}

.tab-label {
  font-weight: 500;
}

/* More Menu */
.more-menu-overlay {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.5);
  z-index: 1001;
  display: flex;
  align-items: flex-end;
  justify-content: center;
  padding-bottom: calc(56px + env(safe-area-inset-bottom, 0));
}

.more-menu {
  background: var(--bg-secondary);
  border-radius: var(--border-radius-lg) var(--border-radius-lg) 0 0;
  width: 100%;
  max-width: 400px;
  padding: 8px 0;
  box-shadow: 0 -4px 20px rgba(0, 0, 0, 0.3);
  animation: slideUp 0.2s ease-out;
}

@keyframes slideUp {
  from {
    transform: translateY(100%);
    opacity: 0;
  }
  to {
    transform: translateY(0);
    opacity: 1;
  }
}

.more-menu-item {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 14px 20px;
  color: var(--text-primary);
  text-decoration: none;
  transition: background 0.15s;
}

.more-menu-item:hover {
  background: var(--bg-hover);
}

.more-menu-item.active {
  color: var(--accent-primary);
}

.more-icon {
  font-size: 20px;
  width: 28px;
  text-align: center;
}

.more-label {
  flex: 1;
  font-size: 14px;
  font-weight: 500;
}

.external-icon {
  color: var(--text-muted);
  font-size: 12px;
}
</style>
