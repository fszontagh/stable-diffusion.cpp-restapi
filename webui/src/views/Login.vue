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
const showPassword = ref(false)

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
      error.value = body.message || body.error || `Sign-in failed (HTTP ${res.status})`
      return
    }
    const body = await res.json() as { token: string; expires_at: number; username?: string }
    auth.setToken(body.token, body.expires_at, body.username || username.value)
    const redirect = (route.query.redirect as string) || '/dashboard'
    router.replace(redirect)
  } catch (e) {
    error.value = (e as Error).message || 'Network error — could not reach the server.'
  } finally {
    submitting.value = false
  }
}
</script>

<template>
  <div class="login-stage">
    <!-- Soft animated background — gradient + grid for product feel -->
    <div class="login-bg" aria-hidden="true">
      <div class="login-bg-glow"></div>
      <div class="login-bg-grid"></div>
    </div>

    <div class="login-shell">
      <header class="login-brand">
        <span class="login-mark" aria-hidden="true">&#129302;</span>
        <span class="login-name">sd.cpp</span>
        <span class="login-sub">REST API</span>
      </header>

      <form class="login-card" @submit.prevent="handleLogin" autocomplete="on">
        <h1 class="login-title">Welcome back</h1>
        <p class="login-tagline">Sign in to your stable-diffusion.cpp server.</p>

        <label class="login-field">
          <span class="login-label">Username</span>
          <input
            v-model="username"
            type="text"
            name="username"
            autocomplete="username"
            spellcheck="false"
            autocapitalize="off"
            required
            autofocus
            :disabled="submitting"
            placeholder="admin"
          />
        </label>

        <label class="login-field">
          <span class="login-label">Password</span>
          <div class="login-pw-wrap">
            <input
              v-model="password"
              :type="showPassword ? 'text' : 'password'"
              name="password"
              autocomplete="current-password"
              required
              :disabled="submitting"
              placeholder="••••••••"
            />
            <button
              type="button"
              class="login-pw-toggle"
              :title="showPassword ? 'Hide password' : 'Show password'"
              @click="showPassword = !showPassword"
              tabindex="-1"
            >
              {{ showPassword ? '🙈' : '👁' }}
            </button>
          </div>
        </label>

        <Transition name="fade">
          <p v-if="error" class="login-error" role="alert">{{ error }}</p>
        </Transition>

        <button
          type="submit"
          class="login-submit"
          :disabled="submitting || !username || !password"
        >
          <span v-if="!submitting">Sign in</span>
          <span v-else class="login-spinner" aria-hidden="true"></span>
        </button>

        <p class="login-hint">
          Server admin sets credentials via <code>config.json</code> or the
          <code>SDCPP_AUTH_USERNAME</code> / <code>SDCPP_AUTH_PASSWORD</code> env vars.
        </p>
      </form>

      <footer class="login-foot">
        <span>stable-diffusion.cpp REST API</span>
        <span class="login-dot">&middot;</span>
        <a href="/docs/" target="_blank" rel="noopener">docs</a>
      </footer>
    </div>
  </div>
</template>

<style scoped>
.login-stage {
  position: relative;
  min-height: 100vh;
  width: 100%;
  display: flex;
  align-items: center;
  justify-content: center;
  background: var(--bg-primary, #0a0a0c);
  color: var(--text-primary, #e8e8e8);
  overflow: hidden;
  isolation: isolate;
}

/* Background — radial accent glow + faint grid */
.login-bg {
  position: absolute;
  inset: 0;
  z-index: 0;
  pointer-events: none;
}

.login-bg-glow {
  position: absolute;
  top: -25%;
  left: 50%;
  transform: translateX(-50%);
  width: 80vw;
  height: 80vh;
  background: radial-gradient(
    ellipse at center,
    rgba(74, 158, 255, 0.18) 0%,
    rgba(74, 158, 255, 0.05) 35%,
    transparent 70%
  );
  filter: blur(40px);
  animation: login-glow 12s ease-in-out infinite;
}

@keyframes login-glow {
  0%, 100% { transform: translate(-50%, 0) scale(1); }
  50%      { transform: translate(-50%, 4%) scale(1.05); }
}

.login-bg-grid {
  position: absolute;
  inset: 0;
  background-image:
    linear-gradient(rgba(255, 255, 255, 0.03) 1px, transparent 1px),
    linear-gradient(90deg, rgba(255, 255, 255, 0.03) 1px, transparent 1px);
  background-size: 48px 48px;
  mask-image: radial-gradient(ellipse at center, #000 0%, #000 30%, transparent 75%);
  -webkit-mask-image: radial-gradient(ellipse at center, #000 0%, #000 30%, transparent 75%);
}

/* Shell + brand */
.login-shell {
  position: relative;
  z-index: 1;
  width: 100%;
  max-width: 420px;
  padding: 2rem 1.25rem;
  display: flex;
  flex-direction: column;
  align-items: stretch;
  gap: 1.5rem;
}

.login-brand {
  display: flex;
  align-items: baseline;
  justify-content: center;
  gap: 0.5rem;
  font-size: 1.05rem;
  letter-spacing: 0.01em;
}

.login-mark {
  font-size: 1.5rem;
  filter: drop-shadow(0 2px 8px rgba(74, 158, 255, 0.4));
}

.login-name {
  font-weight: 700;
  font-size: 1.5rem;
  color: var(--text-primary, #f0f0f0);
}

.login-sub {
  color: var(--text-muted, #888);
  font-size: 0.85rem;
  text-transform: uppercase;
  letter-spacing: 0.12em;
  margin-left: 0.25rem;
  align-self: center;
}

/* Card */
.login-card {
  background: var(--bg-secondary, rgba(20, 22, 28, 0.85));
  backdrop-filter: blur(12px);
  -webkit-backdrop-filter: blur(12px);
  border: 1px solid var(--border-color, rgba(255, 255, 255, 0.08));
  border-radius: 14px;
  padding: 2rem 1.75rem 1.5rem;
  display: flex;
  flex-direction: column;
  gap: 1rem;
  box-shadow:
    0 1px 1px rgba(255, 255, 255, 0.05) inset,
    0 12px 40px rgba(0, 0, 0, 0.4),
    0 4px 12px rgba(0, 0, 0, 0.2);
}

.login-title {
  margin: 0;
  font-size: 1.5rem;
  font-weight: 600;
  color: var(--text-primary, #f0f0f0);
}

.login-tagline {
  margin: 0 0 0.75rem 0;
  color: var(--text-muted, #888);
  font-size: 0.9rem;
  line-height: 1.4;
}

.login-field {
  display: flex;
  flex-direction: column;
  gap: 0.4rem;
}

.login-label {
  font-size: 0.8rem;
  font-weight: 500;
  color: var(--text-secondary, #aaa);
  text-transform: uppercase;
  letter-spacing: 0.08em;
}

.login-field input {
  width: 100%;
  padding: 0.75rem 0.9rem;
  border: 1px solid var(--border-color, rgba(255, 255, 255, 0.1));
  border-radius: 8px;
  background: var(--bg-primary, rgba(0, 0, 0, 0.25));
  color: var(--text-primary, #e8e8e8);
  font-size: 0.95rem;
  font-family: inherit;
  transition: border-color 0.15s, background 0.15s, box-shadow 0.15s;
}

.login-field input::placeholder {
  color: var(--text-muted, #555);
}

.login-field input:focus {
  outline: none;
  border-color: var(--accent-primary, #4a9eff);
  background: var(--bg-primary, rgba(0, 0, 0, 0.4));
  box-shadow: 0 0 0 3px rgba(74, 158, 255, 0.15);
}

.login-field input:disabled {
  opacity: 0.6;
  cursor: not-allowed;
}

/* Password toggle */
.login-pw-wrap {
  position: relative;
}

.login-pw-toggle {
  position: absolute;
  right: 0.5rem;
  top: 50%;
  transform: translateY(-50%);
  background: transparent;
  border: none;
  color: var(--text-muted, #888);
  font-size: 1rem;
  cursor: pointer;
  padding: 0.4rem 0.5rem;
  border-radius: 6px;
  line-height: 1;
}

.login-pw-toggle:hover {
  background: rgba(255, 255, 255, 0.06);
  color: var(--text-primary, #ddd);
}

/* Error */
.login-error {
  margin: 0;
  padding: 0.6rem 0.8rem;
  border-radius: 8px;
  background: rgba(255, 80, 80, 0.08);
  border: 1px solid rgba(255, 80, 80, 0.25);
  color: #ff8a8a;
  font-size: 0.875rem;
  line-height: 1.4;
}

/* Submit */
.login-submit {
  margin-top: 0.25rem;
  padding: 0.8rem 1rem;
  border: none;
  border-radius: 8px;
  background: linear-gradient(180deg, #4a9eff 0%, #3a8eef 100%);
  color: #fff;
  font-size: 0.95rem;
  font-weight: 600;
  cursor: pointer;
  transition: filter 0.15s, transform 0.05s, box-shadow 0.15s;
  box-shadow: 0 4px 14px rgba(74, 158, 255, 0.3);
  display: flex;
  align-items: center;
  justify-content: center;
  min-height: 2.6rem;
}

.login-submit:hover:not(:disabled) {
  filter: brightness(1.08);
  box-shadow: 0 6px 18px rgba(74, 158, 255, 0.4);
}

.login-submit:active:not(:disabled) {
  transform: translateY(1px);
}

.login-submit:disabled {
  opacity: 0.55;
  cursor: not-allowed;
  box-shadow: none;
}

/* Spinner inside submit button */
.login-spinner {
  width: 18px;
  height: 18px;
  border: 2px solid rgba(255, 255, 255, 0.4);
  border-top-color: #fff;
  border-radius: 50%;
  animation: login-spin 0.8s linear infinite;
}

@keyframes login-spin {
  to { transform: rotate(360deg); }
}

/* Hint */
.login-hint {
  margin: 0.5rem 0 0 0;
  padding-top: 0.75rem;
  border-top: 1px solid var(--border-color, rgba(255, 255, 255, 0.06));
  color: var(--text-muted, #777);
  font-size: 0.78rem;
  line-height: 1.5;
  text-align: center;
}

.login-hint code {
  padding: 0.1em 0.35em;
  border-radius: 3px;
  background: rgba(255, 255, 255, 0.05);
  font-size: 0.85em;
  font-family: var(--font-mono, ui-monospace, "SF Mono", Menlo, monospace);
  color: var(--text-secondary, #ccc);
}

/* Footer */
.login-foot {
  display: flex;
  justify-content: center;
  gap: 0.5rem;
  font-size: 0.75rem;
  color: var(--text-muted, #666);
}

.login-foot a {
  color: var(--text-secondary, #aaa);
  text-decoration: none;
}

.login-foot a:hover {
  color: var(--accent-primary, #4a9eff);
  text-decoration: underline;
}

.login-dot {
  opacity: 0.5;
}

/* Error fade transition */
.fade-enter-active, .fade-leave-active {
  transition: opacity 0.2s, transform 0.2s;
}

.fade-enter-from, .fade-leave-to {
  opacity: 0;
  transform: translateY(-4px);
}

/* Tighter on phones */
@media (max-width: 480px) {
  .login-shell {
    padding: 1.25rem 0.75rem;
  }

  .login-card {
    padding: 1.5rem 1.25rem 1.25rem;
  }

  .login-bg-glow {
    width: 120vw;
  }
}
</style>
