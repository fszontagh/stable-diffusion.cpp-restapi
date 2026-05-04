<script setup lang="ts">
import { computed, ref } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useAssistantStore } from '../stores/assistant'
import { useAuthStore } from '../stores/auth'
import MountDialog from './MountDialog.vue'

const route = useRoute()
const router = useRouter()
const assistantStore = useAssistantStore()
const auth = useAuthStore()

const showMountDialog = ref(false)

function openMountDialog() {
  showMountDialog.value = true
}

function closeMountDialog() {
  showMountDialog.value = false
}

function handleLogout() {
  // Best-effort revoke server-side; even if it fails (network down,
  // already-expired token, etc.) we still clear local state and bounce.
  fetch('/auth/logout', {
    method: 'POST',
    headers: auth.token
      ? { 'Authorization': `Bearer ${auth.token}` }
      : {}
  }).catch(() => { /* ignore */ })
  auth.clear()
  router.replace('/login')
}

const navItems = computed(() => {
  const items = [
    { path: '/dashboard', name: 'Dashboard', icon: '&#128200;' },
    { path: '/models', name: 'Models', icon: '&#128230;' },
    { path: '/downloads', name: 'Downloads', icon: '&#11015;' },
    { path: '/generate', name: 'Generate', icon: '&#127912;' },
    { path: '/upscale', name: 'Upscale', icon: '&#128269;' },
    { path: '/queue', name: 'Queue', icon: '&#128203;' },
    { path: '/recycle-bin', name: 'Recycle Bin', icon: '&#128465;' }
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
      <router-link to="/settings" :class="['nav-item', { active: route.path === '/settings' }]">
        <span class="nav-icon">&#9881;</span>
        <span class="nav-label">Settings</span>
      </router-link>
      <a href="/output" target="_blank" class="nav-item">
        <span class="nav-icon">&#128193;</span>
        <span class="nav-label">Output Files</span>
      </a>
      <button
        v-if="auth.isAuthenticated"
        type="button"
        class="nav-item nav-mount"
        title="Mount this server's WebDAV share on your computer"
        @click="openMountDialog"
      >
        <span class="nav-icon">&#128190;</span>
        <span class="nav-label">Mount</span>
      </button>
      <button
        v-if="auth.isAuthenticated"
        type="button"
        class="nav-item nav-logout"
        :title="auth.username ? `Logged in as ${auth.username}` : 'Sign out'"
        @click="handleLogout"
      >
        <span class="nav-icon">&#128274;</span>
        <span class="nav-label">{{ auth.username || 'Sign out' }}</span>
      </button>
    </div>
    <MountDialog :show="showMountDialog" @close="closeMountDialog" />
  </aside>
</template>

<style scoped>
.sidebar {
  width: var(--sidebar-width);
  background: var(--bg-secondary);
  display: flex;
  flex-direction: column;
  flex-shrink: 0;
  box-shadow: 2px 0 8px rgba(0, 0, 0, 0.15);
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

/* The logout / mount entries are buttons — reset browser defaults so they
   inherit .nav-item styling */
.nav-logout,
.nav-mount {
  background: none;
  border: none;
  font: inherit;
  text-align: left;
  cursor: pointer;
  width: 100%;
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
  background: var(--bg-tertiary);
}

@media (max-width: 768px) {
  .sidebar {
    width: 60px;
  }
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

@media (max-width: 640px) {
  .sidebar {
    display: none;
  }
}
</style>
