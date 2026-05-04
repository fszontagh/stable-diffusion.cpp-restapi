import { defineStore } from 'pinia'
import { ref, computed } from 'vue'

const STORAGE_KEY = 'sdcpp_auth_token'
const STORAGE_EXP_KEY = 'sdcpp_auth_expires_at'
const STORAGE_USER_KEY = 'sdcpp_auth_username'

function readLocalStorage(key: string): string | null {
  try { return localStorage.getItem(key) } catch { return null }
}

function writeLocalStorage(key: string, value: string | null): void {
  try {
    if (value === null) localStorage.removeItem(key)
    else localStorage.setItem(key, value)
  } catch {
    // Private mode / quota — non-fatal
  }
}

/**
 * Auth store — owns the bearer token and exposes helpers.
 *
 * Token + expiry persist to localStorage so the user stays logged in across
 * browser reloads. The server's authoritative TTL is enforced server-side;
 * the client's expiry check is just for proactive logout (avoids sending
 * obviously-expired tokens). Server restart invalidates all tokens, so a
 * stored "valid" token may still 401 — handled by the api client's 401
 * interceptor which clears the store and redirects to /login.
 */
export const useAuthStore = defineStore('auth', () => {
  const token = ref<string | null>(readLocalStorage(STORAGE_KEY))
  const expiresAt = ref<number | null>(
    Number(readLocalStorage(STORAGE_EXP_KEY)) || null
  )
  const username = ref<string | null>(readLocalStorage(STORAGE_USER_KEY))

  const isAuthenticated = computed(() => {
    if (!token.value) return false
    if (expiresAt.value && Date.now() / 1000 >= expiresAt.value) return false
    return true
  })

  function setToken(newToken: string, newExpiresAt: number, newUsername: string) {
    token.value = newToken
    expiresAt.value = newExpiresAt
    username.value = newUsername
    writeLocalStorage(STORAGE_KEY, newToken)
    writeLocalStorage(STORAGE_EXP_KEY, String(newExpiresAt))
    writeLocalStorage(STORAGE_USER_KEY, newUsername)
  }

  function clear() {
    token.value = null
    expiresAt.value = null
    username.value = null
    writeLocalStorage(STORAGE_KEY, null)
    writeLocalStorage(STORAGE_EXP_KEY, null)
    writeLocalStorage(STORAGE_USER_KEY, null)
  }

  /**
   * Return the credentials this store knows about.
   *
   * IMPORTANT: by design we do NOT persist the user's password — only the
   * bearer token + username. So `password` here is always `null`. The mount
   * dialog (and any future feature that needs the raw password to embed in
   * an external file or URL) must ask the user to retype it on demand.
   *
   * This method exists to make that contract explicit in code, and to give
   * us a single place to wire up a password-cached-in-memory mode later if
   * we ever want one (e.g. session-only, never written to disk).
   */
  function getCredentials(): { username: string | null; password: string | null } {
    return { username: username.value, password: null }
  }

  return { token, expiresAt, username, isAuthenticated, setToken, clear, getCredentials }
})
