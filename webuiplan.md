# WebUI Implementation Plan

## Overview

This document contains the comprehensive implementation plan for enhancing the SD++ WebUI with critical fixes, performance optimizations, accessibility improvements, and new features.

## Configuration Decisions

1. **Virtual Scrolling**: Using `vue-virtual-scroller` library
2. **Error Tracking**: Sentry integration
3. **Theme System**: Full custom themes support (not just light/dark)
4. **API Availability**: WebSocket-based detection with full UI blocking when offline
5. **Testing**: Skipped for this implementation

## Implementation Queue (Logical Order)

```
Phase 1: Foundation (WebSocket-based API Availability) ⬅️ START HERE
Phase 2: Critical Security Fixes
Phase 3: Error Tracking (Sentry)
Phase 4: High Priority Fixes
Phase 5: UX & Loading States
Phase 6: Accessibility
Phase 7: Performance
Phase 8: Reliability Features
Phase 9: Custom Theme System
Phase 10: Feature Enhancements
Phase 11: Security Hardening
```

---

## Phase 1: WebSocket-based API Availability (Foundation)

### 1.1 Modify WebSocket Service for Availability Detection
**File**: `webui/src/services/websocket.ts`

**Changes**:
- Add public `isServerAvailable()` method that checks state and connection
- Export connection state to app store via existing state change handlers
- Ensure state transitions properly indicate server unavailability
- Add server disconnect timestamp for display purposes

### 1.2 Integrate WebSocket State with App Store
**File**: `webui/src/stores/app.ts`

**Changes**:
- Add `apiAvailable` computed property derived from WebSocket state
- Add `lastDisconnectTime` ref for UI display
- Update when WebSocket state changes to 'disconnected'
- Update when WebSocket reconnects successfully

### 1.3 Create Global API Offline Overlay
**File**: `webui/src/components/ApiOfflineOverlay.vue` (new)

**Implementation**:
```typescript
// Show when apiAvailable is false
- Centered modal-like overlay with backdrop blur
- Large icon showing server disconnected
- Message: "API Server Unavailable"
- Last disconnect time display
- Countdown to next automatic reconnect
- "Reconnect Now" button (triggers wsService.reconnect())
- Cannot be dismissed - blocks all interaction
```

**Styling**:
- Full screen overlay with dark backdrop
- Z-index: 9999 (above everything)
- Smooth fade-in animation
- Spinner when in "reconnecting" state

### 1.4 Add Offline Overlay to App.vue
**File**: `webui/src/App.vue`

**Changes**:
- Import and place `ApiOfflineOverlay` component at top level
- Conditionally render based on `store.apiAvailable`
- Ensure it renders above router-view
- Disable all interactions when overlay is shown

### 1.5 Update StatusBar with Connection Status
**File**: `webui/src/components/StatusBar.vue`

**Changes**:
- Add connection status indicator (green dot / red dot)
- Add status text: "Connected" / "Disconnected"
- Show when API is unavailable
- Add click handler to trigger reconnect when disconnected

---

## Phase 2: Critical Security Fixes

### 2.1 Fix XSS Vulnerability in Markdown Rendering
**File**: `webui/package.json`

**Action**: Install dependencies
```bash
npm install dompurify @types/dompurify
```

**File**: `webui/src/components/Assistant.vue`

**Changes**:
```typescript
// Add import at top
import DOMPurify from 'dompurify'

// Configure allowed tags
const allowedTags = ['b', 'i', 'em', 'strong', 'code', 'pre', 'blockquote', 'ul', 'ol', 'li', 'p', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'a']
DOMPurify.setConfig({ ALLOWED_TAGS: allowedTags })

// Update formatMessage function (line 164)
return DOMPurify.sanitize(marked.parse(cleaned) as string)
```

### 2.2 Fix Toast Memory Leak
**File**: `webui/src/stores/app.ts`

**Changes**:
```typescript
// Add after existing refs
const toastTimeouts = ref<Map<number, ReturnType<typeof setTimeout>>>(new Map())

// In showToast function, save timeout ID
const timeoutId = setTimeout(() => {
  toasts.value = toasts.value.filter(t => t.id !== id)
  toastTimeouts.value.delete(id)
}, 4000)
toastTimeouts.value.set(id, timeoutId)

// In removeToast function
function removeToast(id: number) {
  const timeout = toastTimeouts.value.get(id)
  if (timeout) {
    clearTimeout(timeout)
    toastTimeouts.value.delete(id)
  }
  toasts.value = toasts.value.filter(t => t.id !== id)
}

// In cleanup function
function stopPolling() {
  toastTimeouts.value.forEach(timeout => clearTimeout(timeout))
  toastTimeouts.value.clear()
  // ... existing cleanup
}
```

### 2.3 Fix Pending Continuations Leak
**File**: `webui/src/stores/assistant.ts`

**Changes**:
```typescript
// Add cleanup interval ref
const cleanupInterval = ref<ReturnType<typeof setInterval> | null>(null)

// Add cleanup function
function cleanupStaleContinuations() {
  const now = Date.now()
  const staleThreshold = 60 * 60 * 1000 // 1 hour

  for (const [id, continuation] of pendingContinuations.value) {
    if (now - continuation.startedAt > staleThreshold) {
      console.log(`[Assistant] Cleaning up stale continuation: ${id}`)
      pendingContinuations.value.delete(id)
    }
  }
}

// In initialize function, start periodic cleanup
cleanupInterval.value = setInterval(cleanupStaleContinuations, 5 * 60 * 1000) // Every 5 minutes

// In cleanup function
function cleanup() {
  if (cleanupInterval.value) {
    clearInterval(cleanupInterval.value)
    cleanupInterval.value = null
  }
  // ... existing cleanup
}
```

### 2.4 Create Error Boundary Component
**File**: `webui/src/components/ErrorBoundary.vue` (new)

**Implementation**:
```vue
<script setup lang="ts">
import { ref, onErrorCaptured } from 'vue'

const error = ref<Error | null>(null)

onErrorCaptured((err: unknown) => {
  if (err instanceof Error) {
    error.value = err
    console.error('[ErrorBoundary]', err)
    return false // Prevent error from propagating
  }
})

function reloadPage() {
  window.location.reload()
}
</script>

<template>
  <div v-if="error" class="error-boundary">
    <div class="error-boundary-content">
      <div class="error-icon">&#9888;</div>
      <h2>Something went wrong</h2>
      <p>{{ error.message }}</p>
      <button class="btn btn-primary" @click="reloadPage">Reload Page</button>
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
}
.error-boundary-content {
  text-align: center;
  max-width: 400px;
}
.error-icon {
  font-size: 64px;
  margin-bottom: 16px;
}
</style>
```

**File**: `webui/src/App.vue`

**Changes**:
```vue
<template>
  <div id="app">
    <ApiOfflineOverlay />
    <ErrorBoundary>
      <router-view />
    </ErrorBoundary>
    <Toast />
    <!-- Other global components -->
  </div>
</template>
```

---

## Phase 3: Error Tracking with Sentry

### 3.1 Install Sentry SDK
**File**: `webui/package.json`

**Action**:
```bash
npm install @sentry/vue @sentry/tracing
```

### 3.2 Configure Sentry
**File**: `webui/src/services/sentry.ts` (new)

**Implementation**:
```typescript
import * as Sentry from '@sentry/vue'
import type { Router } from 'vue-router'
import { useAppStore } from '../stores/app'

export function initSentry(app: any, router: Router) {
  // Don't initialize in development
  if (import.meta.env.DEV) {
    return
  }

  const store = useAppStore()

  Sentry.init({
    app,
    dsn: import.meta.env.VITE_SENTRY_DSN,
    environment: import.meta.env.MODE,
    release: import.meta.env.VITE_APP_VERSION || '1.0.0',
    
    integrations: [
      new Sentry.BrowserTracing({
        routingInstrumentation: Sentry.vueRouterInstrumentation(router),
      }),
      new Sentry.Replay({
        maskAllText: true,
        maskAllInputs: true,
      }),
    ],

    tracesSampleRate: 0.1,
    replaysSessionSampleRate: 0.1,
    replaysOnErrorSampleRate: 1.0,

    beforeSend(event) {
      // Add context
      event.contexts = {
        ...event.contexts,
        app: {
          apiAvailable: store.apiAvailable,
          modelLoaded: store.modelLoaded,
          modelName: store.modelName,
        },
      }
      return event
    },
  })
}

export function captureException(error: Error, context?: Record<string, unknown>) {
  Sentry.withScope((scope) => {
    if (context) {
      Object.entries(context).forEach(([key, value]) => {
        scope.setContext(key, value as Record<string, unknown>)
      })
    }
    Sentry.captureException(error)
  })
}

export function captureMessage(message: string, level: Sentry.SeverityLevel = 'info') {
  Sentry.captureMessage(message, level)
}

export function addBreadcrumb(category: string, message: string, data?: Record<string, unknown>) {
  Sentry.addBreadcrumb({
    category,
    message,
    data,
    level: 'info',
  })
}
```

### 3.3 Add Environment Configuration
**File**: `webui/.env.example`

**Add**:
```
VITE_SENTRY_DSN=your-sentry-dsn-here
VITE_APP_VERSION=1.0.0
```

---

## Phase 4: High Priority Fixes

### 4.1 Add Manual WebSocket Reconnect
**File**: `webui/src/services/websocket.ts`

**Add public method**:
```typescript
public manualReconnect(): void {
  this.reconnectAttempts = 0
  this.reconnectDelay = 1000
  
  if (this.reconnectTimer) {
    clearTimeout(this.reconnectTimer)
    this.reconnectTimer = null
  }
  
  if (this.state === 'disconnected' || this.state === 'reconnecting') {
    console.log('[WebSocket] Manual reconnect triggered')
    this.scheduleReconnect()
  }
}
```

### 4.2 Optimize Generate View Watch Pattern
**Install vueuse**:
```bash
npm install @vueuse/core
```

**File**: `webui/src/views/Generate.vue`

**Replace large watch with**:
```typescript
import { useDebounceFn } from '@vueuse/core'

const debouncedSaveSettings = useDebounceFn(() => {
  saveSettings()
}, 500)

watch([width, height], debouncedSaveSettings)
watch([steps, cfgScale], debouncedSaveSettings)
watch([sampler, scheduler], debouncedSaveSettings)
watch([prompt, negativePrompt], saveSettings) // Immediate
```

### 4.3 Add Request Deduplication
**File**: `webui/src/api/client.ts`

**Add to ApiClient class**:
```typescript
private pendingRequests = new Map<string, Promise<unknown>>()

private getRequestKey(method: string, endpoint: string, data?: unknown): string {
  return `${method}:${endpoint}:${JSON.stringify(data || '')}`
}
```

### 4.4 Create Type Guards
**File**: `webui/src/utils/typeguards.ts` (new)

---

## Remaining Phases

Phases 5-11 continue with detailed implementations for:
- UX & Loading States
- Accessibility
- Performance (Virtual Scrolling with vue-virtual-scroller)
- Reliability
- Custom Theme System
- Feature Enhancements
- Security Hardening

---

## Progress Tracking

- [x] Phase 1: WebSocket-based API Availability ✅ COMPLETED
- [x] Phase 2: Critical Security Fixes ✅ COMPLETED
- [ ] Phase 3: Error Tracking (Sentry)
- [ ] Phase 4: High Priority Fixes
- [ ] Phase 5: UX & Loading States
- [ ] Phase 6: Accessibility
- [ ] Phase 7: Performance
- [ ] Phase 8: Reliability Features
- [ ] Phase 9: Custom Theme System
- [ ] Phase 10: Feature Enhancements
- [ ] Phase 11: Security Hardening

## Completed Work Summary

### Phase 1: WebSocket-based API Availability ✅
- Added `isServerAvailable()` and `getLastDisconnectTime()` methods to WebSocket service
- Tracked disconnect timestamp for UI display
- Added `apiAvailable` computed property to app store
- Created ApiOfflineOverlay component with full-screen blocking UI
- Added manual reconnect functionality
- Updated StatusBar with connection status and reconnect button
- Commits: f1a3493

### Phase 2: Critical Security Fixes ✅
- Fixed XSS vulnerability in markdown rendering using DOMPurify
- Fixed toast memory leak by tracking and clearing timeouts
- Fixed pending continuations leak with periodic cleanup (5 min interval)
- Created ErrorBoundary component for graceful error handling
- Wrapped App.vue with ErrorBoundary
- Commits: b3ea8b8, e76191b

---

## Notes

- Each phase should be completed and tested before moving to the next
- Commit after each major milestone
- Create feature branches for complex features
- Test thoroughly in development before merging to main
