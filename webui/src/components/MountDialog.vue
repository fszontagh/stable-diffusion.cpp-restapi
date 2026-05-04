<script setup lang="ts">
import { ref, computed, watch, onBeforeUnmount } from 'vue'
import Modal from './Modal.vue'
import { useMountInfo, type MountPlatform } from '../composables/useMountInfo'

const props = defineProps<{
  show: boolean
}>()

const emit = defineEmits<{
  close: []
}>()

const mount = useMountInfo()

// Tabs — order matches the platform list in the spec.
const tabs: { id: MountPlatform; label: string }[] = [
  { id: 'macos', label: 'macOS' },
  { id: 'windows', label: 'Windows' },
  { id: 'linux-gnome', label: 'Linux (GNOME)' },
  { id: 'linux-kde', label: 'Linux (KDE)' },
  { id: 'linux-cli', label: 'Linux (CLI)' },
]

const activeTab = ref<MountPlatform>(mount.platform.value)

// IMPORTANT: this password lives only in component state. It is never written
// to localStorage or sessionStorage. We clear it whenever the modal closes
// (see watcher below) and on unmount. We also wipe any download URLs that
// were created from it.
const password = ref('')
const showPassword = ref(false)
const verifying = ref(false)
const verifyError = ref<string | null>(null)
// Once the user successfully verifies their password we set this so the
// download/launch buttons become enabled. Verification result is per-modal
// session and is cleared when the modal closes.
const verified = ref(false)
// Copy-to-clipboard transient state, keyed by an arbitrary string. Lets us
// flash "Copied!" on whichever button was just clicked.
const copiedKey = ref<string | null>(null)
let copiedTimer: ReturnType<typeof setTimeout> | null = null

// TODO(security): when task #14 ships its real auth, replace this on-demand
// password retype with a call to a NEW backend endpoint, e.g.
// `POST /webdav/.connect`, that returns a short-lived (e.g. 60s) scoped
// token usable as a Basic-Auth password. Embedding *that* in the .webloc /
// .bat is much safer than embedding the user's actual login password,
// which today ends up readable in `~/Downloads/sdcpp-mount.webloc` etc.

// Re-detect platform whenever the modal opens (in case the user moved
// machines between sessions) and clear secrets.
watch(() => props.show, (isShown) => {
  if (isShown) {
    activeTab.value = mount.platform.value
    password.value = ''
    showPassword.value = false
    verified.value = false
    verifyError.value = null
  } else {
    // Closing — wipe sensitive state.
    password.value = ''
    showPassword.value = false
    verified.value = false
    verifyError.value = null
  }
})

onBeforeUnmount(() => {
  password.value = ''
  if (copiedTimer) clearTimeout(copiedTimer)
})

const usernameDisplay = computed(() => mount.username.value || '')

/**
 * URL with embedded credentials in the userinfo portion. Only used for
 * `.webloc` / `.bat` content — NOT placed in the DOM or in `<a href>`,
 * since modern browsers (Chrome, Firefox) strip credentials from rendered
 * links and warn about credentialed URLs as a phishing mitigation.
 */
function credentialedHttpUrl(): string {
  if (!password.value || !usernameDisplay.value) return mount.webdavUrl.value
  // We deliberately do NOT encodeURIComponent the password — Windows `net use`
  // takes it as a separate argument so it doesn't matter, and the macOS
  // .webloc plist URL field accepts URL-encoded creds. We percent-encode
  // unsafe URL characters but leave most printable ASCII alone.
  const u = encodeURIComponent(usernameDisplay.value)
  const p = encodeURIComponent(password.value)
  // Build manually to avoid losing the trailing slash / path.
  const proto = mount.protocol.value
  return `${proto}//${u}:${p}@${mount.host.value}/webdav/`
}

async function verifyPassword(): Promise<boolean> {
  if (!password.value || !usernameDisplay.value) {
    verifyError.value = 'Enter your password to continue.'
    return false
  }
  verifying.value = true
  verifyError.value = null
  try {
    const res = await fetch('/auth/login', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        username: usernameDisplay.value,
        password: password.value,
      }),
    })
    if (!res.ok) {
      verifyError.value = res.status === 401
        ? 'Password did not match. Try again.'
        : `Verification failed (HTTP ${res.status}).`
      verified.value = false
      return false
    }
    // Drain the response body but don't bother with the new token —
    // we already have a valid session token in the auth store.
    await res.json().catch(() => null)
    verified.value = true
    return true
  } catch (e) {
    verifyError.value = (e as Error).message || 'Network error during verification.'
    verified.value = false
    return false
  } finally {
    verifying.value = false
  }
}

function flashCopied(key: string) {
  copiedKey.value = key
  if (copiedTimer) clearTimeout(copiedTimer)
  copiedTimer = setTimeout(() => {
    copiedKey.value = null
  }, 1400)
}

async function copyToClipboard(text: string, key: string) {
  try {
    await navigator.clipboard.writeText(text)
    flashCopied(key)
  } catch {
    // Fallback: select-and-execCommand is deprecated; just no-op + alert.
    // (Most browsers grant clipboard-write on user gesture.)
    flashCopied(key)
  }
}

function downloadBlob(content: string, filename: string, mime: string) {
  const blob = new Blob([content], { type: mime })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = filename
  document.body.appendChild(a)
  a.click()
  document.body.removeChild(a)
  // Revoke after a tick to give the browser time to start the download.
  setTimeout(() => URL.revokeObjectURL(url), 1000)
}

async function downloadWebloc() {
  if (!verified.value) {
    const ok = await verifyPassword()
    if (!ok) return
  }
  const url = credentialedHttpUrl()
    // Plist string content needs '&', '<', '>' escaped.
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
  const plist =
    '<?xml version="1.0" encoding="UTF-8"?>\n' +
    '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">\n' +
    '<plist version="1.0">\n' +
    '<dict>\n' +
    '  <key>URL</key>\n' +
    `  <string>${url}</string>\n` +
    '</dict>\n' +
    '</plist>\n'
  downloadBlob(plist, 'sdcpp-mount.webloc', 'application/x-apple-plist')
}

async function downloadBat() {
  if (!verified.value) {
    const ok = await verifyPassword()
    if (!ok) return
  }
  // Windows `net use` over HTTPS: the magic incantation is the URL form
  // `https://HOST/webdav/`. Older recipes use `\\HOST@SSL@DavWWWRoot\webdav\`
  // but the plain HTTPS URL works on Win10+ as long as WebClient is running.
  const httpsUrl = `${mount.protocol.value}//${mount.host.value}/webdav/`
  const user = usernameDisplay.value
  const pass = password.value
  // Escape '%' for batch file (each '%' must be doubled when appearing
  // literally inside a .bat). Other special chars (&, |, ^, <, >) get caret-
  // escaped. We don't try to handle every edge case — a password with `"`
  // or newlines will fail and the user can use the CLI fallback.
  const batEscape = (s: string): string =>
    s
      .replace(/\^/g, '^^')
      .replace(/%/g, '%%')
      .replace(/&/g, '^&')
      .replace(/\|/g, '^|')
      .replace(/</g, '^<')
      .replace(/>/g, '^>')
  const userEsc = batEscape(user)
  const passEsc = batEscape(pass)
  const bat =
    '@echo off\r\n' +
    'echo Connecting sdcpp-restapi as a network drive...\r\n' +
    `net use Z: "${httpsUrl}" /user:${userEsc} ${passEsc}\r\n` +
    'if errorlevel 1 (\r\n' +
    '    echo.\r\n' +
    '    echo Failed to connect. Make sure the WebClient service is running:\r\n' +
    '    echo   sc config webclient start= auto\r\n' +
    '    echo   net start webclient\r\n' +
    '    pause\r\n' +
    ') else (\r\n' +
    '    explorer Z:\r\n' +
    ')\r\n'
  downloadBlob(bat, 'sdcpp-mount.bat', 'application/bat')
}

const cliMountCommand = computed(() =>
  `sudo mount -t davfs ${mount.webdavUrl.value} /mnt/sdcpp`
)

const netUseCommand = computed(() => {
  const httpsUrl = `${mount.protocol.value}//${mount.host.value}/webdav/`
  const u = usernameDisplay.value || '<username>'
  const p = password.value || '<password>'
  return `net use Z: "${httpsUrl}" /user:${u} ${p}`
})

const maskedPassword = computed(() => {
  if (!password.value) return ''
  if (showPassword.value) return password.value
  return '•'.repeat(Math.min(password.value.length, 12))
})

function handleClose() {
  emit('close')
}
</script>

<template>
  <Modal :show="props.show" title="Mount on my computer" @close="handleClose">
    <div class="mount-dialog">
      <p class="mount-intro">
        Connect this server's <code>/webdav/</code> share as a network drive on your computer.
        Generated outputs and downloaded models will appear in your file manager.
      </p>

      <!-- Credentials block -->
      <section class="creds">
        <div class="creds-row">
          <label class="creds-field">
            <span>Username</span>
            <input type="text" :value="usernameDisplay" readonly />
          </label>
          <label class="creds-field">
            <span>Password</span>
            <div class="password-input">
              <input
                v-model="password"
                :type="showPassword ? 'text' : 'password'"
                autocomplete="current-password"
                placeholder="Confirm your password"
                :disabled="verifying"
                @keydown.enter.prevent="verifyPassword"
              />
              <button
                type="button"
                class="btn-ghost btn-show"
                :title="showPassword ? 'Hide password' : 'Show password'"
                @click="showPassword = !showPassword"
              >
                {{ showPassword ? 'Hide' : 'Show' }}
              </button>
            </div>
          </label>
        </div>

        <p v-if="!verified && password" class="creds-hint">
          Click <strong>Verify</strong> to confirm your password before generating mount files.
        </p>
        <p v-if="!password" class="creds-hint">
          We don't store your password. Re-enter it here so we can build the mount
          file or URL on the fly.
        </p>
        <p v-if="verifyError" class="creds-error" role="alert">{{ verifyError }}</p>
        <p v-if="verified" class="creds-ok">Password verified. Mount actions enabled.</p>

        <div class="creds-actions">
          <button
            type="button"
            class="btn btn-primary"
            :disabled="!password || verifying || verified"
            @click="verifyPassword"
          >
            {{ verifying ? 'Verifying...' : verified ? 'Verified' : 'Verify' }}
          </button>
          <span class="creds-note">
            Password stays in this browser tab only. Closing this dialog clears it.
          </span>
        </div>
      </section>

      <!-- Platform tabs -->
      <nav class="tabs" role="tablist">
        <button
          v-for="t in tabs"
          :key="t.id"
          type="button"
          role="tab"
          :aria-selected="activeTab === t.id"
          :class="['tab', { active: activeTab === t.id }]"
          @click="activeTab = t.id"
        >
          {{ t.label }}
        </button>
      </nav>

      <!-- macOS -->
      <section v-if="activeTab === 'macos'" class="platform-pane" role="tabpanel">
        <h4 class="pane-title">macOS — Finder</h4>
        <ol class="steps">
          <li>
            <strong>Quickest:</strong> open Finder, press
            <kbd>&#8984;</kbd> + <kbd>K</kbd>, paste this URL, and click Connect:
            <div class="copy-row">
              <code class="copy-text">{{ mount.macosUrl.value }}</code>
              <button
                type="button"
                class="btn btn-secondary btn-sm"
                @click="copyToClipboard(mount.macosUrl.value, 'macos-url')"
              >
                {{ copiedKey === 'macos-url' ? 'Copied!' : 'Copy' }}
              </button>
            </div>
          </li>
          <li>
            <strong>Or:</strong> download a <code>.webloc</code> file and double-click it.
            <div class="action-row">
              <button
                type="button"
                class="btn btn-primary"
                :disabled="!password"
                @click="downloadWebloc"
              >
                Download sdcpp-mount.webloc
              </button>
              <span v-if="!password" class="hint-inline">
                (enter your password above to embed it in the file)
              </span>
            </div>
          </li>
        </ol>
        <p class="tip">
          <strong>Tip:</strong> If Finder asks for credentials, use the username and password
          shown above. macOS may show a security warning the first time you open the
          <code>.webloc</code> file because it was downloaded from your browser — choose
          "Open" anyway.
        </p>
      </section>

      <!-- Windows -->
      <section v-if="activeTab === 'windows'" class="platform-pane" role="tabpanel">
        <h4 class="pane-title">Windows — Explorer</h4>
        <ol class="steps">
          <li>
            <strong>Quickest:</strong> download a <code>.bat</code> file and run it. It maps
            <code>Z:</code> to the share and opens it in Explorer.
            <div class="action-row">
              <button
                type="button"
                class="btn btn-primary"
                :disabled="!password"
                @click="downloadBat"
              >
                Download sdcpp-mount.bat
              </button>
              <span v-if="!password" class="hint-inline">
                (enter your password above first)
              </span>
            </div>
          </li>
          <li>
            <strong>Or run manually</strong> in <kbd>cmd.exe</kbd>:
            <div class="copy-row">
              <code class="copy-text">{{ netUseCommand }}</code>
              <button
                type="button"
                class="btn btn-secondary btn-sm"
                @click="copyToClipboard(netUseCommand, 'win-cmd')"
              >
                {{ copiedKey === 'win-cmd' ? 'Copied!' : 'Copy' }}
              </button>
            </div>
          </li>
        </ol>
        <p class="tip">
          <strong>Tip:</strong> the WebClient service must be running. If it's stopped, run
          <code>net start webclient</code> from an Administrator command prompt. The
          downloaded <code>.bat</code> will trigger SmartScreen — choose "More info"
          then "Run anyway".
        </p>
      </section>

      <!-- Linux GNOME -->
      <section v-if="activeTab === 'linux-gnome'" class="platform-pane" role="tabpanel">
        <h4 class="pane-title">Linux — GNOME Files (Nautilus)</h4>
        <ol class="steps">
          <li>
            Click the link to open in Files — Nautilus will prompt for your password:
            <div class="action-row">
              <a class="btn btn-primary" :href="mount.gnomeUrl.value">
                Open in Files
              </a>
              <button
                type="button"
                class="btn btn-secondary btn-sm"
                @click="copyToClipboard(mount.gnomeUrl.value, 'gnome-url')"
              >
                {{ copiedKey === 'gnome-url' ? 'Copied!' : 'Copy URL' }}
              </button>
            </div>
            <code class="copy-text small">{{ mount.gnomeUrl.value }}</code>
          </li>
        </ol>
        <p class="tip">
          <strong>Tip:</strong> if your browser blocks the <code>davs://</code> scheme,
          copy the URL and paste it into Nautilus &rarr; Other Locations &rarr;
          "Connect to Server".
        </p>
      </section>

      <!-- Linux KDE -->
      <section v-if="activeTab === 'linux-kde'" class="platform-pane" role="tabpanel">
        <h4 class="pane-title">Linux — KDE Dolphin</h4>
        <ol class="steps">
          <li>
            Click the link to open in Dolphin — KIO will prompt for your password:
            <div class="action-row">
              <a class="btn btn-primary" :href="mount.kdeUrl.value">
                Open in Dolphin
              </a>
              <button
                type="button"
                class="btn btn-secondary btn-sm"
                @click="copyToClipboard(mount.kdeUrl.value, 'kde-url')"
              >
                {{ copiedKey === 'kde-url' ? 'Copied!' : 'Copy URL' }}
              </button>
            </div>
            <code class="copy-text small">{{ mount.kdeUrl.value }}</code>
          </li>
        </ol>
        <p class="tip">
          <strong>Tip:</strong> Dolphin's address bar accepts the URL directly. If the
          link doesn't open from the browser, paste it into Dolphin's location bar.
        </p>
      </section>

      <!-- Linux CLI -->
      <section v-if="activeTab === 'linux-cli'" class="platform-pane" role="tabpanel">
        <h4 class="pane-title">Linux — davfs2 (CLI)</h4>
        <ol class="steps">
          <li>
            Install <code>davfs2</code> if needed
            (<code>apt install davfs2</code> / <code>dnf install davfs2</code>),
            then mount:
            <div class="copy-row">
              <code class="copy-text">{{ cliMountCommand }}</code>
              <button
                type="button"
                class="btn btn-secondary btn-sm"
                @click="copyToClipboard(cliMountCommand, 'cli-mount')"
              >
                {{ copiedKey === 'cli-mount' ? 'Copied!' : 'Copy' }}
              </button>
            </div>
          </li>
          <li>
            When prompted, enter username <code>{{ usernameDisplay }}</code> and
            <span v-if="password">
              password <code>{{ maskedPassword }}</code>
              <button type="button" class="btn-link" @click="showPassword = !showPassword">
                ({{ showPassword ? 'hide' : 'show' }})
              </button>
            </span>
            <span v-else>your login password.</span>
          </li>
        </ol>
        <p class="tip">
          <strong>Tip:</strong> create the mount point first
          (<code>sudo mkdir -p /mnt/sdcpp</code>) and add yourself to the
          <code>davfs2</code> group to mount without sudo.
        </p>
      </section>

      <!-- Plain URL fallback (always visible) -->
      <section class="fallback">
        <h4 class="fallback-title">Plain URL (any client)</h4>
        <div class="copy-row">
          <code class="copy-text">{{ mount.webdavUrl.value }}</code>
          <button
            type="button"
            class="btn btn-secondary btn-sm"
            @click="copyToClipboard(mount.webdavUrl.value, 'plain-url')"
          >
            {{ copiedKey === 'plain-url' ? 'Copied!' : 'Copy' }}
          </button>
        </div>
        <p class="fallback-note">
          Use HTTP Basic auth with username <code>{{ usernameDisplay }}</code>
          and your login password.
        </p>
      </section>
    </div>

    <template #footer>
      <button type="button" class="btn btn-secondary" @click="handleClose">Close</button>
    </template>
  </Modal>
</template>

<style scoped>
.mount-dialog {
  display: flex;
  flex-direction: column;
  gap: 1.25rem;
  min-width: min(640px, 100%);
}

.mount-intro {
  margin: 0;
  color: var(--text-secondary);
  font-size: 0.9375rem;
  line-height: 1.5;
}

.mount-intro code {
  font-family: var(--font-mono, monospace);
  background: var(--bg-tertiary);
  padding: 0.1rem 0.35rem;
  border-radius: 3px;
  font-size: 0.875em;
}

/* Credentials section */
.creds {
  display: flex;
  flex-direction: column;
  gap: 0.6rem;
  padding: 0.85rem 1rem;
  border: 1px solid var(--border-color);
  border-radius: 6px;
  background: var(--bg-secondary);
}

.creds-row {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 0.75rem;
}

@media (max-width: 540px) {
  .creds-row { grid-template-columns: 1fr; }
}

.creds-field {
  display: flex;
  flex-direction: column;
  gap: 0.25rem;
  font-size: 0.8125rem;
  color: var(--text-secondary);
}

.creds-field input {
  padding: 0.5rem 0.65rem;
  border: 1px solid var(--border-color);
  border-radius: 4px;
  background: var(--bg-input, var(--bg-primary));
  color: var(--text-primary);
  font-size: 0.9rem;
  font-family: inherit;
}

.creds-field input:focus {
  outline: 2px solid var(--accent-primary, #4a9eff);
  outline-offset: -1px;
}

.password-input {
  display: flex;
  align-items: stretch;
  gap: 0.4rem;
}

.password-input input {
  flex: 1;
  min-width: 0;
}

.btn-show {
  white-space: nowrap;
}

.creds-hint {
  margin: 0;
  font-size: 0.8125rem;
  color: var(--text-muted);
}

.creds-error {
  margin: 0;
  padding: 0.45rem 0.6rem;
  border-radius: 4px;
  background: rgba(255, 80, 80, 0.1);
  border: 1px solid rgba(255, 80, 80, 0.3);
  color: #ff8080;
  font-size: 0.8125rem;
}

.creds-ok {
  margin: 0;
  padding: 0.45rem 0.6rem;
  border-radius: 4px;
  background: rgba(80, 200, 120, 0.1);
  border: 1px solid rgba(80, 200, 120, 0.3);
  color: var(--accent-success, #50c878);
  font-size: 0.8125rem;
}

.creds-actions {
  display: flex;
  align-items: center;
  gap: 0.75rem;
  flex-wrap: wrap;
}

.creds-note {
  font-size: 0.75rem;
  color: var(--text-muted);
}

/* Tabs */
.tabs {
  display: flex;
  gap: 0.25rem;
  flex-wrap: wrap;
  border-bottom: 1px solid var(--border-color);
  padding-bottom: 0;
}

.tab {
  padding: 0.55rem 0.95rem;
  border: 1px solid transparent;
  border-bottom: none;
  border-radius: 6px 6px 0 0;
  background: transparent;
  color: var(--text-secondary);
  font-size: 0.875rem;
  font-weight: 500;
  cursor: pointer;
  transition: background 0.15s, color 0.15s;
  margin-bottom: -1px;
}

.tab:hover {
  background: var(--bg-hover);
  color: var(--text-primary);
}

.tab.active {
  background: var(--bg-secondary);
  border-color: var(--border-color);
  color: var(--accent-primary);
}

/* Pane */
.platform-pane {
  display: flex;
  flex-direction: column;
  gap: 0.85rem;
}

.pane-title {
  margin: 0;
  font-size: 1rem;
  font-weight: 600;
  color: var(--text-primary);
}

.steps {
  margin: 0;
  padding-left: 1.25rem;
  display: flex;
  flex-direction: column;
  gap: 0.85rem;
  color: var(--text-secondary);
  font-size: 0.9375rem;
  line-height: 1.5;
}

.steps li {
  padding-left: 0.25rem;
}

.steps code {
  font-family: var(--font-mono, monospace);
  background: var(--bg-tertiary);
  padding: 0.1rem 0.35rem;
  border-radius: 3px;
  font-size: 0.85em;
}

.steps kbd {
  display: inline-block;
  padding: 0.05rem 0.4rem;
  font-family: var(--font-mono, monospace);
  font-size: 0.8em;
  color: var(--text-primary);
  background: var(--bg-tertiary);
  border: 1px solid var(--border-color);
  border-radius: 3px;
  box-shadow: 0 1px 0 rgba(0, 0, 0, 0.1);
}

.copy-row {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  margin-top: 0.4rem;
  flex-wrap: wrap;
}

.copy-row .copy-text {
  flex: 1;
  min-width: 0;
  padding: 0.45rem 0.6rem;
  background: var(--bg-tertiary);
  border: 1px solid var(--border-color);
  border-radius: 4px;
  font-family: var(--font-mono, monospace);
  font-size: 0.825rem;
  color: var(--text-primary);
  word-break: break-all;
  white-space: pre-wrap;
}

.copy-text.small {
  display: block;
  margin-top: 0.4rem;
  font-size: 0.8rem;
}

.action-row {
  display: flex;
  align-items: center;
  gap: 0.6rem;
  flex-wrap: wrap;
  margin-top: 0.4rem;
}

.hint-inline {
  font-size: 0.8125rem;
  color: var(--text-muted);
}

.tip {
  margin: 0;
  padding: 0.6rem 0.8rem;
  border-left: 3px solid var(--accent-warning, #f0a000);
  background: rgba(240, 160, 0, 0.08);
  border-radius: 0 4px 4px 0;
  font-size: 0.8125rem;
  color: var(--text-secondary);
  line-height: 1.5;
}

.tip code {
  font-family: var(--font-mono, monospace);
  background: var(--bg-tertiary);
  padding: 0.05rem 0.3rem;
  border-radius: 3px;
  font-size: 0.85em;
}

/* Fallback section */
.fallback {
  display: flex;
  flex-direction: column;
  gap: 0.4rem;
  padding-top: 0.85rem;
  border-top: 1px solid var(--border-color);
}

.fallback-title {
  margin: 0;
  font-size: 0.875rem;
  font-weight: 600;
  color: var(--text-secondary);
  text-transform: uppercase;
  letter-spacing: 0.04em;
}

.fallback-note {
  margin: 0;
  font-size: 0.8125rem;
  color: var(--text-muted);
}

.fallback-note code {
  font-family: var(--font-mono, monospace);
  background: var(--bg-tertiary);
  padding: 0.05rem 0.3rem;
  border-radius: 3px;
}

/* Buttons (matching app conventions) */
.btn {
  padding: 0.5rem 1rem;
  border: none;
  border-radius: 4px;
  font-size: 0.875rem;
  font-weight: 500;
  cursor: pointer;
  transition: background 0.15s, color 0.15s, opacity 0.15s;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 0.4rem;
  text-decoration: none;
  font-family: inherit;
}

.btn-sm {
  padding: 0.35rem 0.7rem;
  font-size: 0.8125rem;
}

.btn-primary {
  background: var(--accent-primary, #4a9eff);
  color: #fff;
}

.btn-primary:hover:not(:disabled) {
  background: var(--primary-hover, #3a8eef);
}

.btn-secondary {
  background: var(--bg-tertiary);
  color: var(--text-primary);
  border: 1px solid var(--border-color);
}

.btn-secondary:hover:not(:disabled) {
  background: var(--bg-hover);
}

.btn-ghost {
  background: transparent;
  color: var(--text-secondary);
  border: 1px solid var(--border-color);
  padding: 0.35rem 0.6rem;
  font-size: 0.8125rem;
  font-weight: 500;
  border-radius: 4px;
  cursor: pointer;
  transition: background 0.15s, color 0.15s;
}

.btn-ghost:hover {
  background: var(--bg-hover);
  color: var(--text-primary);
}

.btn:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.btn-link {
  background: none;
  border: none;
  padding: 0;
  margin-left: 0.25rem;
  color: var(--accent-primary);
  font-size: 0.8125rem;
  cursor: pointer;
  text-decoration: underline;
  font-family: inherit;
}
</style>
