import * as Sentry from '@sentry/vue'
import type { App } from 'vue'
import type { Router } from 'vue-router'

/**
 * Initialize Sentry error tracking
 * Only initializes in production mode if DSN is configured
 */
export function initSentry(app: App, router: Router) {
  // Don't initialize in development
  if (import.meta.env.DEV) {
    console.log('[Sentry] Skipping initialization in development mode')
    return
  }

  // Check if DSN is configured
  const dsn = import.meta.env.VITE_SENTRY_DSN
  if (!dsn || dsn === 'your-sentry-dsn-here') {
    console.log('[Sentry] No DSN configured, skipping initialization')
    return
  }

  try {
    Sentry.init({
      app,
      dsn,
      environment: import.meta.env.MODE || 'production',
      release: import.meta.env.VITE_APP_VERSION || '1.0.0',
      
      integrations: [
        Sentry.browserTracingIntegration({ router }),
        Sentry.replayIntegration({
          maskAllText: true,
          maskAllInputs: true,
        }),
      ],

      // Performance Monitoring
      tracesSampleRate: 0.1, // 10% of transactions for performance monitoring
      
      // Session Replay
      replaysSessionSampleRate: 0.1, // 10% of sessions
      replaysOnErrorSampleRate: 1.0, // 100% of sessions with errors

      // Filter out known benign errors
      beforeSend(event, hint) {
        // Filter out ResizeObserver errors (common browser quirk)
        if (hint.originalException && 
            typeof hint.originalException === 'object' && 
            'message' in hint.originalException &&
            typeof hint.originalException.message === 'string' &&
            hint.originalException.message.includes('ResizeObserver')) {
          return null
        }
        
        return event
      },
    })

    console.log('[Sentry] Initialized successfully')
  } catch (error) {
    console.error('[Sentry] Failed to initialize:', error)
  }
}

/**
 * Capture an exception with additional context
 */
export function captureException(error: Error, context?: Record<string, unknown>) {
  if (!Sentry.isInitialized()) {
    console.error('[Sentry] Not initialized, logging error:', error)
    return
  }

  Sentry.withScope((scope) => {
    if (context) {
      Object.entries(context).forEach(([key, value]) => {
        scope.setContext(key, value as Record<string, unknown>)
      })
    }
    Sentry.captureException(error)
  })
}

/**
 * Capture a message with optional level
 */
export function captureMessage(message: string, level: Sentry.SeverityLevel = 'info') {
  if (!Sentry.isInitialized()) {
    console.log('[Sentry] Not initialized, logging message:', message)
    return
  }

  Sentry.captureMessage(message, level)
}

/**
 * Add a breadcrumb for context
 */
export function addBreadcrumb(category: string, message: string, data?: Record<string, unknown>) {
  if (!Sentry.isInitialized()) {
    return
  }

  Sentry.addBreadcrumb({
    category,
    message,
    data,
    level: 'info',
  })
}

/**
 * Set user context
 */
export function setUser(id: string, email?: string, username?: string) {
  if (!Sentry.isInitialized()) {
    return
  }

  Sentry.setUser({
    id,
    email,
    username,
  })
}

/**
 * Clear user context
 */
export function clearUser() {
  if (!Sentry.isInitialized()) {
    return
  }

  Sentry.setUser(null)
}

/**
 * Set custom tag
 */
export function setTag(key: string, value: string) {
  if (!Sentry.isInitialized()) {
    return
  }

  Sentry.setTag(key, value)
}

/**
 * Check if Sentry is initialized
 */
export function isInitialized(): boolean {
  return Sentry.isInitialized()
}
