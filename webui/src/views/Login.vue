<script setup lang="ts">
import { ref } from 'vue'
import { useRouter, useRoute } from 'vue-router'
import { useAuthStore } from '../stores/auth'

const router = useRouter()
const route = useRoute()
const auth = useAuthStore()

const username = ref('')
const password = ref('')
const error = ref<string | null>(null)
const submitting = ref(false)

async function handleLogin() {
  error.value = null
  submitting.value = true
  try {
    const res = await fetch('/auth/login', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        username: username.value,
        password: password.value
      })
    })
    if (!res.ok) {
      const body = await res.json().catch(() => ({}))
      error.value = body.error || `Login failed (HTTP ${res.status})`
      return
    }
    const body = await res.json() as { token: string; expires_at: number; username?: string }
    auth.setToken(body.token, body.expires_at, body.username || username.value)
    const redirect = (route.query.redirect as string) || '/dashboard'
    router.replace(redirect)
  } catch (e) {
    error.value = (e as Error).message || 'Network error'
  } finally {
    submitting.value = false
  }
}
</script>

<template>
  <div class="login-wrapper">
    <form class="login-card" @submit.prevent="handleLogin">
      <h1 class="login-title">sdcpp-restapi</h1>
      <p class="login-subtitle">Sign in to continue</p>

      <label class="login-field">
        <span>Username</span>
        <input
          v-model="username"
          type="text"
          autocomplete="username"
          required
          autofocus
          :disabled="submitting"
        />
      </label>

      <label class="login-field">
        <span>Password</span>
        <input
          v-model="password"
          type="password"
          autocomplete="current-password"
          required
          :disabled="submitting"
        />
      </label>

      <p v-if="error" class="login-error" role="alert">{{ error }}</p>

      <button type="submit" class="login-submit" :disabled="submitting">
        {{ submitting ? 'Signing in…' : 'Sign in' }}
      </button>
    </form>
  </div>
</template>

<style scoped>
.login-wrapper {
  display: flex;
  align-items: center;
  justify-content: center;
  min-height: 100vh;
  padding: 1rem;
}

.login-card {
  width: 100%;
  max-width: 360px;
  display: flex;
  flex-direction: column;
  gap: 1rem;
  padding: 2rem;
  border: 1px solid var(--border-color, #2a2a2a);
  border-radius: 8px;
  background: var(--bg-secondary, #1a1a1a);
  box-shadow: 0 4px 20px rgba(0, 0, 0, 0.3);
}

.login-title {
  margin: 0;
  font-size: 1.5rem;
  font-weight: 600;
}

.login-subtitle {
  margin: 0 0 0.5rem 0;
  color: var(--text-muted, #888);
  font-size: 0.9rem;
}

.login-field {
  display: flex;
  flex-direction: column;
  gap: 0.35rem;
  font-size: 0.875rem;
}

.login-field input {
  padding: 0.6rem 0.75rem;
  border: 1px solid var(--border-color, #2a2a2a);
  border-radius: 4px;
  background: var(--bg-primary, #0d0d0d);
  color: var(--text-primary, #e8e8e8);
  font-size: 0.95rem;
}

.login-field input:focus {
  outline: 2px solid var(--accent-color, #4a9eff);
  outline-offset: -1px;
}

.login-error {
  margin: 0;
  padding: 0.6rem 0.75rem;
  border-radius: 4px;
  background: rgba(255, 80, 80, 0.1);
  border: 1px solid rgba(255, 80, 80, 0.3);
  color: #ff8080;
  font-size: 0.875rem;
}

.login-submit {
  padding: 0.7rem 1rem;
  border: none;
  border-radius: 4px;
  background: var(--accent-color, #4a9eff);
  color: #fff;
  font-size: 0.95rem;
  font-weight: 500;
  cursor: pointer;
  transition: background 0.15s;
}

.login-submit:hover:not(:disabled) {
  background: var(--accent-color-hover, #3a8eef);
}

.login-submit:disabled {
  opacity: 0.6;
  cursor: not-allowed;
}
</style>
