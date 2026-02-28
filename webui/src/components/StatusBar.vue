<script setup lang="ts">
import { computed } from 'vue'
import { useAppStore } from '../stores/app'
import { wsService } from '../services/websocket'

const store = useAppStore()

// Notification toggle handler
async function toggleNotifications() {
  const enabled = await store.toggleDesktopNotifications()
  if (enabled) {
    store.showToast('Desktop notifications enabled', 'success')
  } else {
    store.showToast('Desktop notifications disabled', 'info')
  }
}

// Manual reconnect handler
function handleReconnect() {
  wsService.manualReconnect()
  store.showToast('Attempting to reconnect...', 'info')
}

// Check if WebSocket is available at build time
const wsAvailable = computed(() => {
  return store.health?.ws_port !== null && store.health?.ws_port !== undefined
})

// Connection status text
const connectionStatusText = computed(() => {
  // If WebSocket is not available at build time, show polling mode
  if (!wsAvailable.value) {
    return 'Polling Mode'
  }

  switch (store.wsState) {
    case 'connected':
      return 'Real-time'
    case 'connecting':
      return 'Connecting...'
    case 'reconnecting':
      return 'Reconnecting...'
    case 'disconnected':
      // Show different status if HTTP works but WS doesn't
      return store.wsOnlyDisconnected ? 'HTTP Only' : 'Disconnected'
    default:
      return 'Unknown'
  }
})

// Calculate progress percentage for header background
const progressPercent = computed(() => {
  // Model loading progress
  const loadingStep = store.loadingStep ?? 0
  const loadingTotal = store.loadingTotalSteps ?? 0
  if (store.modelLoading && loadingTotal > 0) {
    return (loadingStep / loadingTotal) * 100
  }

  // Job processing progress
  const processingJob = store.queue?.items?.find(job => job.status === 'processing')
  if (processingJob && processingJob.progress.total_steps > 0) {
    return (processingJob.progress.step / processingJob.progress.total_steps) * 100
  }

  return 0
})

const hasProgress = computed(() => {
  return store.modelLoading || store.queueStats.processing > 0
})

// Memory info from health response
const memoryInfo = computed(() => store.health?.memory)

// Format memory usage percentage with color class
const ramUsageClass = computed(() => {
  const percent = memoryInfo.value?.system?.usage_percent ?? 0
  if (percent > 90) return 'critical'
  if (percent > 75) return 'warning'
  return 'normal'
})

const gpuUsageClass = computed(() => {
  const percent = memoryInfo.value?.gpu?.usage_percent ?? 0
  if (percent > 90) return 'critical'
  if (percent > 75) return 'warning'
  return 'normal'
})

// Format MB to human-readable string
function formatMB(mb: number): string {
  if (mb >= 1024) {
    return (mb / 1024).toFixed(1) + ' GB'
  }
  return mb.toFixed(0) + ' MB'
}
</script>

<template>
  <header class="status-bar" :class="{ 'has-progress': hasProgress }">
    <!-- Progress bar background -->
    <div
      v-if="hasProgress"
      class="progress-bg"
      :style="{ width: progressPercent + '%' }"
    ></div>

    <div class="status-left">
      <div class="logo">
        <span class="logo-icon">&#129302;</span>
        <span class="logo-text">sd.cpp</span>
        <span class="version-badge" :title="`Version ${store.serverVersion} (${store.serverGitCommit})`">
          v{{ store.serverVersion }}
        </span>
      </div>
    </div>
    <div class="status-center">
      <div class="model-info loading" v-if="store.modelLoading">
        <span class="spinner"></span>
        <span class="model-label">Loading:</span>
        <span class="model-name">{{ store.loadingModelName }}</span>
        <span class="loading-progress" v-if="store.loadingTotalSteps && store.loadingTotalSteps > 0">
          ({{ store.loadingStep }}/{{ store.loadingTotalSteps }})
        </span>
      </div>
      <div class="model-info" v-else-if="store.modelLoaded">
        <span class="model-label">Model:</span>
        <span class="model-name">{{ store.modelName }}</span>
        <span class="model-arch" v-if="store.modelArchitecture">({{ store.modelArchitecture }})</span>
      </div>
      <div class="model-info error" v-else-if="store.lastLoadError">
        <span class="error-label">Error:</span>
        <span class="error-text" :title="store.lastLoadError">{{ store.lastLoadError }}</span>
      </div>
      <div class="model-info empty" v-else>
        No model loaded
      </div>
    </div>
    <div class="status-right">
      <!-- Memory status -->
      <div class="memory-status" v-if="memoryInfo">
        <!-- GPU Memory (if available) -->
        <div
          v-if="memoryInfo.gpu?.available"
          class="memory-item gpu"
          :class="gpuUsageClass"
          :title="`GPU: ${memoryInfo.gpu.name}\nUsed: ${formatMB(memoryInfo.gpu.used_mb)} / ${formatMB(memoryInfo.gpu.total_mb)}`"
        >
          <span class="memory-icon">GPU</span>
          <span class="memory-value">{{ memoryInfo.gpu.usage_percent.toFixed(0) }}%</span>
        </div>
        <!-- System RAM -->
        <div
          class="memory-item ram"
          :class="ramUsageClass"
          :title="`RAM Used: ${formatMB(memoryInfo.system.used_mb)} / ${formatMB(memoryInfo.system.total_mb)}\nProcess: ${formatMB(memoryInfo.process.rss_mb)}`"
        >
          <span class="memory-icon">RAM</span>
          <span class="memory-value">{{ memoryInfo.system.usage_percent.toFixed(0) }}%</span>
        </div>
      </div>

      <!-- Desktop notifications toggle -->
      <button
        class="notification-toggle"
        :class="{ enabled: store.desktopNotificationsEnabled }"
        @click="toggleNotifications"
        :title="store.desktopNotificationsEnabled ? 'Desktop notifications enabled (click to disable)' : 'Enable desktop notifications'"
        :aria-label="store.desktopNotificationsEnabled ? 'Disable desktop notifications' : 'Enable desktop notifications'"
        :aria-pressed="store.desktopNotificationsEnabled"
      >
        <svg v-if="store.desktopNotificationsEnabled" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
          <path d="M18 8A6 6 0 0 0 6 8c0 7-3 9-3 9h18s-3-2-3-9"/>
          <path d="M13.73 21a2 2 0 0 1-3.46 0"/>
        </svg>
        <svg v-else viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
          <path d="M18 8A6 6 0 0 0 6 8c0 7-3 9-3 9h18s-3-2-3-9"/>
          <path d="M13.73 21a2 2 0 0 1-3.46 0"/>
          <line x1="1" y1="1" x2="23" y2="23"/>
        </svg>
      </button>
      <div class="connection-status" :class="{ connected: store.wsConnected, polling: !wsAvailable, 'http-only': store.wsOnlyDisconnected }" role="status" :aria-label="`Connection status: ${connectionStatusText}`">
        <span class="status-dot" :class="wsAvailable ? (store.wsConnected ? 'connected' : (store.wsOnlyDisconnected ? 'http-only' : 'disconnected')) : 'polling'" aria-hidden="true"></span>
        <span class="status-text">{{ connectionStatusText }}</span>
        <button
          v-if="wsAvailable && store.wsState === 'disconnected'"
          class="reconnect-btn"
          @click="handleReconnect"
          title="Attempt to reconnect"
          aria-label="Reconnect to server"
        >
          &#128257;
        </button>
      </div>
      <div class="queue-indicator" v-if="store.queueStats.processing > 0 || store.queueStats.pending > 0">
        <span class="spinner"></span>
        <span>{{ store.queueStats.processing + store.queueStats.pending }}</span>
      </div>
    </div>
  </header>
</template>

<style scoped>
.status-bar {
  position: relative;
  height: var(--header-height);
  background: var(--bg-secondary);
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.15);
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 20px;
  flex-shrink: 0;
  overflow: hidden;
}

.progress-bg {
  position: absolute;
  top: 0;
  left: 0;
  height: 100%;
  background: linear-gradient(90deg,
    rgba(0, 217, 255, 0.15) 0%,
    rgba(199, 146, 234, 0.15) 100%
  );
  transition: width 0.3s ease;
  pointer-events: none;
}

.status-bar.has-progress::after {
  content: '';
  position: absolute;
  bottom: 0;
  left: 0;
  height: 2px;
  width: 100%;
  background: linear-gradient(90deg, var(--accent-primary), var(--accent-purple));
  animation: progress-glow 2s ease-in-out infinite;
}

@keyframes progress-glow {
  0%, 100% { opacity: 0.5; }
  50% { opacity: 1; }
}

.status-left {
  display: flex;
  align-items: center;
  gap: 16px;
}

.logo {
  display: flex;
  align-items: center;
  gap: 8px;
}

.logo-icon {
  font-size: 24px;
}

.logo-text {
  font-size: 18px;
  font-weight: 700;
  color: var(--accent-primary);
}

.version-badge {
  font-size: 10px;
  font-weight: 500;
  color: var(--text-muted);
  background: var(--bg-tertiary);
  padding: 2px 6px;
  border-radius: var(--border-radius-sm);
  cursor: default;
}

.status-center {
  flex: 1;
  display: flex;
  justify-content: center;
}

.model-info {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 13px;
}

.model-info.empty {
  color: var(--text-muted);
}

.model-info.loading {
  color: var(--accent-primary);
}

.model-info.loading .spinner {
  width: 14px;
  height: 14px;
  margin-right: 4px;
}

.loading-progress {
  color: var(--text-secondary);
  font-size: 12px;
}

.model-info.error {
  color: var(--accent-error);
}

.error-label {
  color: var(--accent-error);
}

.error-text {
  max-width: 300px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.model-label {
  color: var(--text-secondary);
}

.model-name {
  color: var(--text-primary);
  font-weight: 500;
  max-width: 300px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.model-arch {
  color: var(--accent-purple);
  font-size: 12px;
}

.status-right {
  display: flex;
  align-items: center;
  gap: 16px;
}

.notification-toggle {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 32px;
  height: 32px;
  padding: 0;
  background: transparent;
  border-radius: var(--border-radius-sm);
  color: var(--text-muted);
  cursor: pointer;
  transition: all 0.2s ease;
}

.notification-toggle:hover {
  background: var(--bg-tertiary);
  color: var(--text-primary);
}

.notification-toggle.enabled {
  color: var(--accent-primary);
}

.notification-toggle.enabled:hover {
  background: rgba(0, 217, 255, 0.1);
}

.notification-toggle svg {
  width: 18px;
  height: 18px;
}

.connection-status {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 13px;
  color: var(--text-secondary);
}

.connection-status.connected .status-text {
  color: var(--accent-success);
}

.connection-status.polling .status-text {
  color: var(--text-secondary);
}

.connection-status.http-only .status-text {
  color: var(--accent-warning, #f0a000);
}

.status-dot.http-only {
  background: var(--accent-warning, #f0a000);
  animation: pulse 2s ease-in-out infinite;
}

.status-dot.polling {
  background: var(--text-muted);
  animation: pulse 2s ease-in-out infinite;
}

@keyframes pulse {
  0%, 100% { opacity: 0.5; }
  50% { opacity: 1; }
}

.reconnect-btn {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 24px;
  height: 24px;
  padding: 0;
  background: transparent;
  border-radius: var(--border-radius-sm);
  color: var(--text-muted);
  cursor: pointer;
  font-size: 12px;
  transition: all 0.2s ease;
}

.reconnect-btn:hover {
  background: var(--bg-tertiary);
  color: var(--accent-primary);
}

.queue-indicator {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 6px 12px;
  background: var(--bg-tertiary);
  border-radius: var(--border-radius-sm);
  font-size: 13px;
  color: var(--accent-primary);
}

.queue-indicator .spinner {
  width: 14px;
  height: 14px;
}

/* Memory status styles */
.memory-status {
  display: flex;
  align-items: center;
  gap: 8px;
}

.memory-item {
  display: flex;
  align-items: center;
  gap: 4px;
  padding: 4px 8px;
  background: var(--bg-tertiary);
  border-radius: var(--border-radius-sm);
  font-size: 11px;
  font-weight: 500;
  transition: all 0.2s ease;
}

.memory-icon {
  color: var(--text-muted);
  font-size: 10px;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}

.memory-value {
  color: var(--text-secondary);
  font-variant-numeric: tabular-nums;
}

.memory-item.normal .memory-value {
  color: var(--accent-success);
}

.memory-item.warning .memory-value {
  color: var(--accent-warning, #f0a000);
}

.memory-item.critical .memory-value {
  color: var(--accent-error);
}

.memory-item.gpu .memory-icon {
  color: var(--accent-purple);
}

.memory-item.ram .memory-icon {
  color: var(--accent-primary);
}

@media (max-width: 768px) {
  .status-center {
    display: none;
  }
  .status-text {
    display: none;
  }
}
</style>
