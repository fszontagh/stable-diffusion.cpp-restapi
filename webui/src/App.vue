<script setup lang="ts">
import { ref, onMounted, onUnmounted } from 'vue'
import { useAppStore } from './stores/app'
import { useKeyboardShortcuts } from './composables/useKeyboardShortcuts'
import { useTheme } from './composables/useTheme'
import { wsService } from './services/websocket'
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
const wsBannerDismissed = ref(false)

// Dismiss the WebSocket warning banner
function dismissWsBanner() {
  wsBannerDismissed.value = true
}

// Try to reconnect WebSocket
function retryWsConnection() {
  wsService.manualReconnect()
  store.showToast('Attempting to reconnect WebSocket...', 'info')
}

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

    <!-- WebSocket disconnected warning banner (HTTP still works) -->
    <Transition name="slide-down">
      <div v-if="store.wsOnlyDisconnected && !wsBannerDismissed" class="ws-warning-banner">
        <span class="ws-warning-icon">&#9888;</span>
        <span class="ws-warning-text">
          WebSocket disconnected - real-time updates unavailable. The UI will poll for updates.
        </span>
        <button class="ws-retry-btn" @click="retryWsConnection" title="Retry connection">
          Retry
        </button>
        <button class="ws-dismiss-btn" @click="dismissWsBanner" title="Dismiss">
          &times;
        </button>
      </div>
    </Transition>

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

/* WebSocket warning banner */
.ws-warning-banner {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 8px 16px;
  background: linear-gradient(90deg, rgba(240, 160, 0, 0.15), rgba(240, 160, 0, 0.05));
  border-bottom: 1px solid rgba(240, 160, 0, 0.3);
  font-size: 13px;
  color: var(--text-primary);
}

.ws-warning-icon {
  color: var(--accent-warning, #f0a000);
  font-size: 16px;
}

.ws-warning-text {
  flex: 1;
  color: var(--text-secondary);
}

.ws-retry-btn {
  padding: 4px 12px;
  background: rgba(240, 160, 0, 0.2);
  border: 1px solid rgba(240, 160, 0, 0.4);
  border-radius: var(--border-radius-sm);
  color: var(--accent-warning, #f0a000);
  font-size: 12px;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.2s ease;
}

.ws-retry-btn:hover {
  background: rgba(240, 160, 0, 0.3);
  border-color: rgba(240, 160, 0, 0.6);
}

.ws-dismiss-btn {
  padding: 4px 8px;
  background: transparent;
  border: none;
  border-radius: var(--border-radius-sm);
  color: var(--text-muted);
  font-size: 18px;
  line-height: 1;
  cursor: pointer;
  transition: all 0.2s ease;
}

.ws-dismiss-btn:hover {
  background: var(--bg-tertiary);
  color: var(--text-primary);
}

/* Slide down transition */
.slide-down-enter-active,
.slide-down-leave-active {
  transition: all 0.3s ease;
}

.slide-down-enter-from,
.slide-down-leave-to {
  opacity: 0;
  transform: translateY(-100%);
}
</style>
