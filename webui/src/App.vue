<script setup lang="ts">
import { ref, onMounted, onUnmounted } from 'vue'
import { useAppStore } from './stores/app'
import { useKeyboardShortcuts } from './composables/useKeyboardShortcuts'
import { useTheme } from './composables/useTheme'
import Sidebar from './components/Sidebar.vue'
import StatusBar from './components/StatusBar.vue'
import Toast from './components/Toast.vue'
import Assistant from './components/Assistant.vue'
import AssistantQuestion from './components/AssistantQuestion.vue'
import FloatingPreview from './components/FloatingPreview.vue'
import ApiOfflineOverlay from './components/ApiOfflineOverlay.vue'
import ErrorBoundary from './components/ErrorBoundary.vue'
import ShortcutsHelpModal from './components/ShortcutsHelpModal.vue'
import MobileTabBar from './components/MobileTabBar.vue'

const store = useAppStore()
const shortcutsModalRef = ref<InstanceType<typeof ShortcutsHelpModal>>()

// Initialize keyboard shortcuts
useKeyboardShortcuts()

// Initialize theme system (loads saved theme from localStorage)
useTheme()

// Listen for show shortcuts help event
const handleShowShortcuts = (event: CustomEvent) => {
  shortcutsModalRef.value?.show(event.detail.shortcuts)
}

onMounted(() => {
  store.startPolling()
  window.addEventListener('show-shortcuts-help', handleShowShortcuts as EventListener)
})

onUnmounted(() => {
  store.stopPolling()
  window.removeEventListener('show-shortcuts-help', handleShowShortcuts as EventListener)
})
</script>

<template>
  <!-- API Offline Overlay - renders above everything when disconnected -->
  <ApiOfflineOverlay />
  
  <!-- Initial Loading Overlay -->
  <div v-if="store.isInitialLoading" class="initial-loading">
    <div class="loading-content">
      <div class="spinner spinner-lg"></div>
      <p class="loading-text">Loading application...</p>
    </div>
  </div>
  
  <ErrorBoundary>
    <StatusBar />
    <div class="app-layout">
      <Sidebar />
      <main class="main-content">
        <router-view />
      </main>
    </div>
    <Toast />
    <FloatingPreview />
    <Assistant />
    <AssistantQuestion />
    <ShortcutsHelpModal ref="shortcutsModalRef" />
    <MobileTabBar />
  </ErrorBoundary>
</template>

<style scoped>
.initial-loading {
  position: fixed;
  inset: 0;
  background: var(--bg-primary);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 10001;
}

.loading-content {
  text-align: center;
}

.spinner-lg {
  width: 48px;
  height: 48px;
  margin: 0 auto 16px;
}

.loading-text {
  color: var(--text-secondary);
  font-size: 14px;
}
</style>
