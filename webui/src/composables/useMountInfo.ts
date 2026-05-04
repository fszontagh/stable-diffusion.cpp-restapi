import { computed } from 'vue'
import { useAuthStore } from '../stores/auth'

export type MountPlatform = 'macos' | 'windows' | 'linux-gnome' | 'linux-kde' | 'linux-cli'

/**
 * Detect the user's platform from `navigator.userAgent`.
 *
 * macOS / Windows are unambiguous from UA. For Linux we cannot tell whether
 * the user runs GNOME or KDE from the browser, so we default to GNOME and let
 * the modal show all Linux variants in tabs.
 */
function detectPlatform(): MountPlatform {
  if (typeof navigator === 'undefined') return 'linux-gnome'
  const ua = navigator.userAgent.toLowerCase()
  if (ua.includes('mac os x') || ua.includes('macintosh')) return 'macos'
  if (ua.includes('windows')) return 'windows'
  // Anything Linux-y (incl. ChromeOS, BSD) — pick GNOME as default tab; user
  // can switch to KDE / CLI tabs.
  return 'linux-gnome'
}

/**
 * Reactive accessor for the info needed to render mount instructions.
 *
 * The base URL is taken from `window.location` so the WebDAV mount works
 * whether the server is reached via http://localhost:8080, an https reverse
 * proxy, a tunnel, etc. We use the *current page's* origin because the
 * WebDAV server (task #14) is mounted under `/webdav/` on the same host.
 */
export function useMountInfo() {
  const auth = useAuthStore()

  const origin = computed(() => {
    if (typeof window === 'undefined') return ''
    return window.location.origin
  })

  const host = computed(() => {
    if (typeof window === 'undefined') return ''
    return window.location.host
  })

  const protocol = computed(() => {
    if (typeof window === 'undefined') return 'https:'
    return window.location.protocol
  })

  const isHttps = computed(() => protocol.value === 'https:')

  /** Plain WebDAV URL with no embedded credentials. */
  const webdavUrl = computed(() => `${origin.value}/webdav/`)

  /** WebDAV URL for the output share specifically. */
  const webdavOutputUrl = computed(() => `${origin.value}/webdav/output/`)

  /** WebDAV URL for the models share specifically. */
  const webdavModelsUrl = computed(() => `${origin.value}/webdav/models/`)

  /**
   * davs:// scheme — used by GNOME (Nautilus / GVfs). HTTPS variant.
   * For plain HTTP we fall back to dav://.
   */
  const gnomeUrl = computed(() => {
    const scheme = isHttps.value ? 'davs' : 'dav'
    const userPart = auth.username ? `${encodeURIComponent(auth.username)}@` : ''
    return `${scheme}://${userPart}${host.value}/webdav/`
  })

  /**
   * webdavs:// scheme — used by KDE (Dolphin / KIO). HTTPS variant.
   * Plain HTTP uses webdav://.
   */
  const kdeUrl = computed(() => {
    const scheme = isHttps.value ? 'webdavs' : 'webdav'
    const userPart = auth.username ? `${encodeURIComponent(auth.username)}@` : ''
    return `${scheme}://${userPart}${host.value}/webdav/`
  })

  /**
   * macOS uses the webdav:// or webdavs:// scheme via Finder's "Connect to
   * Server". macOS Finder accepts http(s):// directly too, but webdav(s)://
   * is the canonical hint.
   */
  const macosUrl = computed(() => {
    const scheme = isHttps.value ? 'webdavs' : 'webdav'
    const userPart = auth.username ? `${encodeURIComponent(auth.username)}@` : ''
    return `${scheme}://${userPart}${host.value}/webdav/`
  })

  const platform = computed<MountPlatform>(() => detectPlatform())

  return {
    origin,
    host,
    protocol,
    isHttps,
    webdavUrl,
    webdavOutputUrl,
    webdavModelsUrl,
    gnomeUrl,
    kdeUrl,
    macosUrl,
    platform,
    username: computed(() => auth.username),
  }
}
