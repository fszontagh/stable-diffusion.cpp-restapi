import { defineStore } from 'pinia'
import { ref } from 'vue'

/**
 * Auth store — minimal in-SPA copy of the authenticated user's identity.
 *
 * The actual auth (token validity, expiry, etc.) lives entirely server-side
 * in the `sdcpp_auth` HttpOnly cookie. This store only mirrors what the
 * SPA needs to *display* — currently just the username, populated from the
 * /health response. The server enforces auth at the middleware level, so
 * the SPA doesn't need a navigation guard or an isAuthenticated flag.
 *
 * On logout the SPA POSTs /auth/logout (server clears the cookie) and
 * window.location's the user back to /login.
 */
export const useAuthStore = defineStore('auth', () => {
  const username = ref<string | null>(null)

  function setUsername(name: string | null) {
    username.value = name
  }

  function clear() {
    username.value = null
  }

  return { username, setUsername, clear }
})
