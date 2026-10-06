/**
 * Desktop Notifications Service
 * Handles browser notification permissions and sending desktop notifications
 */

// Notification settings stored in localStorage
const STORAGE_KEY = 'desktop_notifications_enabled'

export type NotificationBlockReason =
  | null
  | 'unsupported'
  | 'insecure-context'
  | 'denied'
  | 'dismissed'

export interface NotificationToggleResult {
  enabled: boolean
  reason: NotificationBlockReason
  message?: string
}

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
   * Return null when notifications COULD be enabled (either already granted,
   * or 'default' and the browser will prompt). Otherwise return the concrete
   * reason so the UI can explain instead of just saying "disabled". This is
   * the piece that was missing - previously toggle() returned bare `false`
   * for every non-happy path and the toast could never tell the user WHY.
   */
  getBlockReason(): NotificationBlockReason {
    if (!this.isSupported()) return 'unsupported'
    // Notification API only works from a secure context (HTTPS) or from
    // localhost. On plain http://<hostname> the permission prompt never
    // appears and Notification.permission is stuck at 'denied' (Chromium)
    // or requestPermission returns 'denied' without a UI (Firefox).
    if (typeof window !== 'undefined' && window.isSecureContext === false) {
      return 'insecure-context'
    }
    if (Notification.permission === 'denied') return 'denied'
    return null
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
   * Toggle notifications. Returns a structured result so the UI can show
   * the actual reason instead of a generic "disabled" toast.
   */
  async toggle(): Promise<NotificationToggleResult> {
    if (this.enabled) {
      this.disable()
      return { enabled: false, reason: null, message: 'Desktop notifications disabled' }
    }

    const blocked = this.getBlockReason()
    if (blocked === 'unsupported') {
      return {
        enabled: false,
        reason: 'unsupported',
        message: 'This browser does not expose the Notification API.',
      }
    }
    if (blocked === 'insecure-context') {
      const origin = typeof window !== 'undefined' ? window.location.origin : ''
      return {
        enabled: false,
        reason: 'insecure-context',
        message: `Desktop notifications need HTTPS or a localhost URL. ${origin} is neither, so the browser blocks the prompt. Access the WebUI over HTTPS (or via http://localhost) to enable them.`,
      }
    }
    if (blocked === 'denied') {
      return {
        enabled: false,
        reason: 'denied',
        message: 'Notifications were denied in your browser. Reset the permission via the site-info menu in the address bar, then click again.',
      }
    }

    if (this.permissionGranted) {
      return {
        enabled: this.enable(),
        reason: null,
        message: 'Desktop notifications enabled',
      }
    }

    const granted = await this.requestPermission()
    if (granted) {
      return { enabled: true, reason: null, message: 'Desktop notifications enabled' }
    }
    // requestPermission() returned false but we already ruled out unsupported /
    // insecure / denied above, so this is the user dismissing the prompt.
    return {
      enabled: false,
      reason: 'dismissed',
      message: 'Permission dismissed. Click again to see the prompt.',
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
   * Notify about a finished job. Title and body are built from the job's
   * real type (image / video / upscale / conversion / download / hash) and
   * its identity (user title, prompt or file name). The tag is per job, so
   * two jobs finishing close together show two notifications instead of
   * the second silently replacing the first.
   */
  notifyJobFinished(status: 'completed' | 'failed', job: JobNotificationInfo): Notification | null {
    const { title, body } = describeJobNotification(status, job)
    return this.notify(title, { body, tag: `job-${job.jobId}` })
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

}

export interface JobNotificationInfo {
  jobId: string
  type?: string
  title?: string
  params?: Record<string, unknown>
  outputs?: string[]
  error?: string
}

const JOB_NOTIFICATION_LABELS: Record<string, { done: string; failed: string }> = {
  txt2img: { done: 'Image ready', failed: 'Image generation failed' },
  img2img: { done: 'Image ready (img2img)', failed: 'Image-to-image failed' },
  txt2vid: { done: 'Video ready', failed: 'Video generation failed' },
  upscale: { done: 'Upscale finished', failed: 'Upscale failed' },
  convert: { done: 'Model conversion finished', failed: 'Model conversion failed' },
  model_download: { done: 'Download finished', failed: 'Download failed' },
  model_hash: { done: 'Model hash computed', failed: 'Model hashing failed' },
}

function basename(path: string): string {
  return path.replace(/[\\/]+$/, '').split(/[\\/]/).pop() ?? path
}

function truncate(text: string, max = 80): string {
  const clean = text.replace(/\s+/g, ' ').trim()
  return clean.length > max ? clean.slice(0, max - 1) + '…' : clean
}

function str(value: unknown): string {
  return typeof value === 'string' ? value : ''
}

/** What the job was about: user title first, then the type-specific identity. */
function jobSubject(job: JobNotificationInfo): string {
  if (job.title) return truncate(job.title)
  const p = job.params ?? {}
  const firstOutput = job.outputs?.[0] ? basename(job.outputs[0]) : ''
  switch (job.type) {
    case 'txt2img':
    case 'img2img':
    case 'txt2vid':
      return str(p.prompt) ? truncate(str(p.prompt)) : ''
    case 'upscale':
      return firstOutput
    case 'convert':
      return str(p.output_path) ? basename(str(p.output_path)) : firstOutput
    case 'model_download':
      return firstOutput
        || (str(p.filename) && basename(str(p.filename)))
        || str(p.repo_id)
        || str(p.url)
    case 'model_hash':
      return str(p.file_name) || (str(p.file_path) && basename(str(p.file_path)))
    default:
      return ''
  }
}

export function describeJobNotification(
  status: 'completed' | 'failed',
  job: JobNotificationInfo
): { title: string; body: string } {
  const labels = (job.type && JOB_NOTIFICATION_LABELS[job.type]) || { done: 'Job finished', failed: 'Job failed' }
  const subject = jobSubject(job)

  if (status === 'failed') {
    const error = job.error ? truncate(job.error, 160) : 'Unknown error'
    return { title: labels.failed, body: subject ? `${subject}\n${error}` : error }
  }

  const count = job.outputs?.length ?? 0
  const isMedia = job.type === 'txt2img' || job.type === 'img2img' || job.type === 'txt2vid'
  const countNote = isMedia && count > 1 ? ` (${count} ${job.type === 'txt2vid' ? 'videos' : 'images'})` : ''
  return { title: labels.done + countNote, body: subject || 'Finished successfully' }
}

// Export singleton instance
export const notificationService = new NotificationService()
