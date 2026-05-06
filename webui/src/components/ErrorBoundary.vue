<script setup lang="ts">
import { ref, onErrorCaptured } from 'vue'
import { captureException } from '../services/sentry'

const error = ref<Error | null>(null)
const errorInfo = ref<string | null>(null)

onErrorCaptured((err: unknown, _instance, info) => {
  if (err instanceof Error) {
    error.value = err
    errorInfo.value = info
    
    // Log to console
    console.error('[ErrorBoundary] Caught error:', err)
    console.error('[ErrorBoundary] Component info:', info)
    console.error('[ErrorBoundary] Stack:', err.stack)
    
    // Send to Sentry with context
    captureException(err, {
      component: 'ErrorBoundary',
      errorInfo: info,
      route: window.location.hash,
    })
    
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
/* Error boundary uses the same login-page design language: a soft radial
   glow backdrop, a faint grid (masked to fade at the edges), and a
   glassmorphism card with layered shadows. The accent here is --accent-error
   instead of --accent-primary so it visually reads as a problem, but the
   composition (gradient + grid + blurred card) matches /login. */
.error-boundary {
  position: fixed;
  inset: 0;
  background: var(--bg-primary);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 10000;
  padding: 20px;
  overflow: hidden;
}

/* Radial glow centered, error-tinted. */
.error-boundary::before {
  content: '';
  position: absolute;
  top: -25%;
  left: 50%;
  transform: translateX(-50%);
  width: 80vw;
  height: 80vh;
  background: radial-gradient(
    ellipse at center,
    rgba(var(--accent-error-rgb, 239, 68, 68), 0.18) 0%,
    rgba(var(--accent-error-rgb, 239, 68, 68), 0.05) 35%,
    transparent 70%
  );
  filter: blur(40px);
  animation: error-glow 12s ease-in-out infinite;
  pointer-events: none;
}

/* Subtle grid masked to fade away from center — same trick the login page uses. */
.error-boundary::after {
  content: '';
  position: absolute;
  inset: 0;
  background-image:
    linear-gradient(rgba(255, 255, 255, 0.03) 1px, transparent 1px),
    linear-gradient(90deg, rgba(255, 255, 255, 0.03) 1px, transparent 1px);
  background-size: 48px 48px;
  -webkit-mask-image: radial-gradient(ellipse at center, #000 0%, #000 30%, transparent 75%);
          mask-image: radial-gradient(ellipse at center, #000 0%, #000 30%, transparent 75%);
  pointer-events: none;
}

@keyframes error-glow {
  0%, 100% { transform: translate(-50%, 0) scale(1); }
  50%      { transform: translate(-50%, 4%) scale(1.05); }
}

.error-boundary-content {
  position: relative;
  z-index: 1;
  max-width: 600px;
  width: 100%;
  text-align: center;
  /* Glassmorphism card matching login-card. */
  background: rgba(17, 24, 39, 0.85);
  -webkit-backdrop-filter: blur(12px);
          backdrop-filter: blur(12px);
  border: 1px solid var(--border-color);
  border-radius: 14px;
  padding: 2.5rem 2rem 1.75rem;
  box-shadow:
    0 1px 1px rgba(255, 255, 255, 0.05) inset,
    0 12px 40px rgba(0, 0, 0, 0.4),
    0 4px 12px rgba(0, 0, 0, 0.2),
    0 0 0 1px rgba(var(--accent-error-rgb, 239, 68, 68), 0.15);
}

.error-icon {
  font-size: 64px;
  color: var(--accent-warning);
  margin-bottom: 24px;
  animation: pulse 2s ease-in-out infinite;
  filter: drop-shadow(0 4px 14px rgba(var(--accent-error-rgb, 239, 68, 68), 0.45));
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
  background: rgba(0, 0, 0, 0.25);
  border: 1px solid rgba(var(--accent-error-rgb, 239, 68, 68), 0.5);
  border-radius: var(--border-radius);
  box-shadow: 0 0 0 3px rgba(var(--accent-error-rgb, 239, 68, 68), 0.08);
}

.error-details {
  margin-bottom: 24px;
  text-align: left;
  background: rgba(0, 0, 0, 0.25);
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
  background: rgba(255, 255, 255, 0.04);
}

.error-stack {
  padding: 16px;
  border-top: 1px solid var(--border-color);
}

.error-stack pre {
  background: rgba(0, 0, 0, 0.4);
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
