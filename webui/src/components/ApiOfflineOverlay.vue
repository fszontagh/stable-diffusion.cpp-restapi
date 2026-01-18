<script setup lang="ts">
import { ref, computed, onMounted, onBeforeUnmount } from 'vue'
import { useAppStore } from '../stores/app'
import { wsService } from '../services/websocket'

const store = useAppStore()

// Countdown to next reconnect attempt
const countdown = ref(0)
let countdownInterval: ReturnType<typeof setInterval> | null = null

// Calculate time since disconnect
const timeSinceDisconnect = computed(() => {
  if (!store.lastDisconnectTime) return null
  
  const seconds = Math.floor((Date.now() - store.lastDisconnectTime) / 1000)
  
  if (seconds < 60) return `${seconds}s ago`
  const minutes = Math.floor(seconds / 60)
  if (minutes < 60) return `${minutes}m ago`
  const hours = Math.floor(minutes / 60)
  return `${hours}h ago`
})

// Reconnect status message
const statusMessage = computed(() => {
  if (store.wsState === 'reconnecting') {
    return 'Attempting to reconnect...'
  }
  return 'Connection lost to API server'
})

// Manual reconnect
function handleReconnect() {
  wsService.manualReconnect()
}

// Update countdown display
function updateCountdown() {
  // This is a simple countdown - actual reconnect timing is handled by wsService
  if (countdown.value > 0) {
    countdown.value--
  }
}

onMounted(() => {
  // Start countdown timer
  countdownInterval = setInterval(updateCountdown, 1000)
})

onBeforeUnmount(() => {
  if (countdownInterval) {
    clearInterval(countdownInterval)
  }
})
</script>

<template>
  <Transition name="fade">
    <div v-if="!store.apiAvailable" class="api-offline-overlay" role="alertdialog" aria-labelledby="offline-title" aria-describedby="offline-message">
      <div class="overlay-backdrop" aria-hidden="true" />
      <div class="overlay-content">
        <div class="status-icon">
          <span v-if="store.wsState === 'reconnecting'" class="spinner spinner-lg"></span>
          <span v-else class="disconnect-icon">&#128721;</span>
        </div>
        
        <h2 id="offline-title" class="overlay-title">{{ statusMessage }}</h2>
        
        <p v-if="store.lastDisconnectTime" class="disconnect-time">
          Disconnected {{ timeSinceDisconnect }}
        </p>
        
        <p id="offline-message" class="overlay-message">
          The WebUI requires a connection to the API server.
          <br />
          Please ensure the server is running and accessible.
        </p>
        
        <div class="overlay-actions">
          <button 
            class="btn btn-primary btn-lg"
            @click="handleReconnect"
            :disabled="store.wsState === 'reconnecting'"
          >
            <span v-if="store.wsState === 'reconnecting'" class="spinner"></span>
            {{ store.wsState === 'reconnecting' ? 'Reconnecting...' : 'Reconnect Now' }}
          </button>
        </div>
        
        <p class="help-text">
          If the problem persists, check your server configuration and network connection.
        </p>
      </div>
    </div>
  </Transition>
</template>

<style scoped>
.api-offline-overlay {
  position: fixed;
  inset: 0;
  z-index: 9999;
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 20px;
}

.overlay-backdrop {
  position: absolute;
  inset: 0;
  background: rgba(0, 0, 0, 0.85);
  backdrop-filter: blur(8px);
}

.overlay-content {
  position: relative;
  max-width: 500px;
  width: 100%;
  background: var(--bg-secondary);
  border: 2px solid var(--accent-error);
  border-radius: var(--border-radius-lg);
  padding: 48px 32px;
  text-align: center;
  box-shadow: var(--shadow-lg);
}

.status-icon {
  font-size: 72px;
  margin-bottom: 24px;
  height: 72px;
  display: flex;
  align-items: center;
  justify-content: center;
}

.disconnect-icon {
  color: var(--accent-error);
  animation: pulse 2s ease-in-out infinite;
}

@keyframes pulse {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.5; }
}

.overlay-title {
  font-size: 24px;
  font-weight: 600;
  color: var(--text-primary);
  margin-bottom: 8px;
}

.disconnect-time {
  font-size: 14px;
  color: var(--text-secondary);
  margin-bottom: 16px;
}

.overlay-message {
  font-size: 15px;
  color: var(--text-secondary);
  line-height: 1.6;
  margin-bottom: 32px;
}

.overlay-actions {
  margin-bottom: 24px;
}

.help-text {
  font-size: 13px;
  color: var(--text-muted);
  line-height: 1.5;
}

/* Fade transition */
.fade-enter-active,
.fade-leave-active {
  transition: opacity 0.3s ease;
}

.fade-enter-from,
.fade-leave-to {
  opacity: 0;
}

/* Responsive */
@media (max-width: 768px) {
  .overlay-content {
    padding: 32px 24px;
  }
  
  .overlay-title {
    font-size: 20px;
  }
  
  .status-icon {
    font-size: 56px;
    height: 56px;
  }
}
</style>
