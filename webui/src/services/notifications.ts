/**
 * Desktop Notifications Service
 * Handles browser notification permissions and sending desktop notifications
 */

// Notification settings stored in localStorage
const STORAGE_KEY = 'desktop_notifications_enabled'

class NotificationService {
  private enabled: boolean = false
  private permissionGranted: boolean = false

  constructor() {
    // Check if notifications are supported
    if (!('Notification' in window)) {
      console.log('[Notifications] Desktop notifications not supported')
      return
    }

    // Load setting from localStorage
    const stored = localStorage.getItem(STORAGE_KEY)
    this.enabled = stored === 'true'

    // Check current permission state
    this.permissionGranted = Notification.permission === 'granted'

    // If enabled but permission not granted, disable
    if (this.enabled && !this.permissionGranted) {
      this.enabled = false
      localStorage.setItem(STORAGE_KEY, 'false')
    }
  }

  /**
   * Check if notifications are supported
   */
  isSupported(): boolean {
    return 'Notification' in window
  }

  /**
   * Check if notifications are enabled
   */
  isEnabled(): boolean {
    return this.enabled && this.permissionGranted
  }

  /**
   * Get current permission state
   */
  getPermission(): NotificationPermission | 'unsupported' {
    if (!this.isSupported()) return 'unsupported'
    return Notification.permission
  }

  /**
   * Request notification permission and enable notifications
   * Returns true if permission was granted
   */
  async requestPermission(): Promise<boolean> {
    if (!this.isSupported()) {
      console.log('[Notifications] Not supported')
      return false
    }

    // Already granted
    if (Notification.permission === 'granted') {
      this.permissionGranted = true
      this.enabled = true
      localStorage.setItem(STORAGE_KEY, 'true')
      return true
    }

    // Already denied - user must change in browser settings
    if (Notification.permission === 'denied') {
      console.log('[Notifications] Permission denied by user')
      return false
    }

    // Request permission
    try {
      const permission = await Notification.requestPermission()
      this.permissionGranted = permission === 'granted'
      if (this.permissionGranted) {
        this.enabled = true
        localStorage.setItem(STORAGE_KEY, 'true')
        console.log('[Notifications] Permission granted')
        return true
      } else {
        console.log('[Notifications] Permission not granted:', permission)
        return false
      }
    } catch (e) {
      console.error('[Notifications] Error requesting permission:', e)
      return false
    }
  }

  /**
   * Disable notifications
   */
  disable(): void {
    this.enabled = false
    localStorage.setItem(STORAGE_KEY, 'false')
    console.log('[Notifications] Disabled')
  }

  /**
   * Enable notifications (only works if permission was previously granted)
   */
  enable(): boolean {
    if (!this.permissionGranted) {
      console.log('[Notifications] Cannot enable - permission not granted')
      return false
    }
    this.enabled = true
    localStorage.setItem(STORAGE_KEY, 'true')
    console.log('[Notifications] Enabled')
    return true
  }

  /**
   * Toggle notifications
   * If not permitted, will request permission
   */
  async toggle(): Promise<boolean> {
    if (this.enabled) {
      this.disable()
      return false
    } else {
      if (this.permissionGranted) {
        return this.enable()
      } else {
        return await this.requestPermission()
      }
    }
  }

  /**
   * Send a desktop notification
   * Only sends if notifications are enabled and page is not visible
   */
  notify(title: string, options?: NotificationOptions): Notification | null {
    // Don't notify if disabled
    if (!this.isEnabled()) {
      return null
    }

    // Don't notify if page is visible (user is looking at the app)
    if (document.visibilityState === 'visible') {
      return null
    }

    try {
      const notification = new Notification(title, {
        icon: '/favicon.ico',
        badge: '/favicon.ico',
        ...options
      })

      // Auto-close after 5 seconds
      setTimeout(() => notification.close(), 5000)

      // Focus window when notification is clicked
      notification.onclick = () => {
        window.focus()
        notification.close()
      }

      return notification
    } catch (e) {
      console.error('[Notifications] Error creating notification:', e)
      return null
    }
  }

  /**
   * Helper to notify about job completion
   */
  notifyJobComplete(jobType: string): Notification | null {
    return this.notify('Job Completed', {
      body: `${jobType} finished successfully`,
      tag: 'job-complete'
    })
  }

  /**
   * Helper to notify about job failure
   */
  notifyJobFailed(jobType: string, error?: string): Notification | null {
    return this.notify('Job Failed', {
      body: error ? `${jobType}: ${error}` : `${jobType} failed`,
      tag: 'job-failed'
    })
  }

  /**
   * Helper to notify about model loading
   */
  notifyModelLoaded(modelName: string): Notification | null {
    const shortName = modelName.split('/').pop() ?? modelName
    return this.notify('Model Loaded', {
      body: shortName,
      tag: 'model-loaded'
    })
  }

  /**
   * Helper to notify about download completion
   */
  notifyDownloadComplete(filename: string): Notification | null {
    return this.notify('Download Complete', {
      body: filename,
      tag: 'download-complete'
    })
  }

  /**
   * Helper to notify about conversion completion
   */
  notifyConversionComplete(filename: string): Notification | null {
    return this.notify('Conversion Complete', {
      body: filename,
      tag: 'conversion-complete'
    })
  }
}

// Export singleton instance
export const notificationService = new NotificationService()
