<script setup lang="ts">
import { ref, onErrorCaptured } from 'vue'

const error = ref<Error | null>(null)
const errorInfo = ref<string | null>(null)

onErrorCaptured((err: unknown, instance, info) => {
  if (err instanceof Error) {
    error.value = err
    errorInfo.value = info
    console.error('[ErrorBoundary] Caught error:', err)
    console.error('[ErrorBoundary] Component info:', info)
    console.error('[ErrorBoundary] Stack:', err.stack)
    
    // Prevent error from propagating
    return false
  }
})

function reloadPage() {
  window.location.reload()
}

function clearError() {
  error.value = null
  errorInfo.value = null
}
</script>

<template>
  <div v-if="error" class="error-boundary">
    <div class="error-boundary-content">
      <div class="error-icon">&#9888;</div>
      <h2 class="error-title">Something went wrong</h2>
      <p class="error-message">{{ error.message }}</p>
      
      <details class="error-details">
        <summary>Technical Details</summary>
        <div class="error-stack">
          <p><strong>Component:</strong> {{ errorInfo }}</p>
          <pre>{{ error.stack }}</pre>
        </div>
      </details>
      
      <div class="error-actions">
        <button class="btn btn-primary" @click="reloadPage">
          Reload Page
        </button>
        <button class="btn btn-secondary" @click="clearError">
          Try to Continue
        </button>
      </div>
      
      <p class="error-help">
        If this error persists, please check the browser console for more details.
      </p>
    </div>
  </div>
  <slot v-else />
</template>

<style scoped>
.error-boundary {
  position: fixed;
  inset: 0;
  background: var(--bg-primary);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 10000;
  padding: 20px;
}

.error-boundary-content {
  max-width: 600px;
  width: 100%;
  text-align: center;
}

.error-icon {
  font-size: 64px;
  color: var(--accent-error);
  margin-bottom: 24px;
  animation: pulse 2s ease-in-out infinite;
}

@keyframes pulse {
  0%, 100% { opacity: 1; transform: scale(1); }
  50% { opacity: 0.7; transform: scale(1.1); }
}

.error-title {
  font-size: 28px;
  font-weight: 600;
  color: var(--text-primary);
  margin-bottom: 16px;
}

.error-message {
  font-size: 16px;
  color: var(--text-secondary);
  margin-bottom: 24px;
  padding: 16px;
  background: var(--bg-secondary);
  border: 1px solid var(--accent-error);
  border-radius: var(--border-radius);
}

.error-details {
  margin-bottom: 24px;
  text-align: left;
  background: var(--bg-secondary);
  border: 1px solid var(--border-color);
  border-radius: var(--border-radius);
}

.error-details summary {
  padding: 12px 16px;
  cursor: pointer;
  font-weight: 500;
  color: var(--text-primary);
  user-select: none;
}

.error-details summary:hover {
  background: var(--bg-tertiary);
}

.error-stack {
  padding: 16px;
  border-top: 1px solid var(--border-color);
}

.error-stack pre {
  background: var(--bg-primary);
  padding: 12px;
  border-radius: var(--border-radius-sm);
  font-family: var(--font-mono);
  font-size: 12px;
  color: var(--text-secondary);
  overflow-x: auto;
  white-space: pre-wrap;
  word-wrap: break-word;
  margin-top: 8px;
}

.error-actions {
  display: flex;
  gap: 12px;
  justify-content: center;
  margin-bottom: 24px;
}

.error-help {
  font-size: 13px;
  color: var(--text-muted);
}

@media (max-width: 768px) {
  .error-title {
    font-size: 22px;
  }
  
  .error-icon {
    font-size: 48px;
  }
  
  .error-actions {
    flex-direction: column;
  }
}
</style>
