<script setup lang="ts">
import { ref, watch, nextTick, onMounted, onUnmounted, computed } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useAssistantStore } from '../stores/assistant'
import { useAppStore } from '../stores/app'
import type { AssistantAction } from '../api/client'
import { marked } from 'marked'

// Configure marked for safe rendering
marked.setOptions({
  breaks: true,  // Convert \n to <br>
  gfm: true,     // GitHub Flavored Markdown
})

const store = useAssistantStore()
const appStore = useAppStore()
const route = useRoute()
const router = useRouter()

const inputMessage = ref('')
const messagesContainer = ref<HTMLElement | null>(null)

// Map routes to view names
const viewMap: Record<string, string> = {
  '/dashboard': 'dashboard',
  '/models': 'models',
  '/generate': 'generate',
  '/upscale': 'upscale',
  '/queue': 'queue',
  '/chat': 'chat'
}

// Watch route changes to update current view
watch(() => route.path, (path) => {
  store.setCurrentView(viewMap[path] || 'unknown')
}, { immediate: true })

// Scroll to bottom helper
function scrollToBottom() {
  nextTick(() => {
    // Use setTimeout to ensure DOM is fully rendered
    setTimeout(() => {
      if (messagesContainer.value) {
        messagesContainer.value.scrollTop = messagesContainer.value.scrollHeight
      }
    }, 50)
  })
}

// Auto-scroll to bottom when new messages arrive
watch(() => store.messages.length, scrollToBottom)

// Handle navigation events from assistant
function handleNavigate(event: Event) {
  const customEvent = event as CustomEvent<{ view: string }>
  const view = customEvent.detail.view
  if (viewMap[`/${view}`] !== undefined || view in viewMap) {
    router.push(`/${view}`)
  }
}

onMounted(() => {
  // Initialize if not already done
  if (!store.enabled) {
    store.initialize()
  }
  window.addEventListener('assistant-navigate', handleNavigate)

  // Scroll to bottom on mount
  scrollToBottom()
})

onUnmounted(() => {
  window.removeEventListener('assistant-navigate', handleNavigate)
})

async function handleSend() {
  const message = inputMessage.value.trim()
  if (!message || store.isLoading) return

  inputMessage.value = ''
  await store.sendMessage(message)
}

function handleKeyDown(event: KeyboardEvent) {
  if (event.key === 'Enter' && !event.shiftKey) {
    event.preventDefault()
    handleSend()
  }
}

// Format action for display
function formatAction(action: AssistantAction): string {
  switch (action.type) {
    case 'set_setting':
      return `Set ${action.parameters.field} = ${action.parameters.value}`
    case 'load_model':
      return `Load ${action.parameters.model_name}`
    case 'unload_model':
      return 'Unload model'
    case 'set_component':
      return `Set ${action.parameters.component_type}: ${action.parameters.model_name}`
    case 'navigate':
      return `Go to ${action.parameters.view}`
    case 'cancel_job':
      return `Cancel job ${action.parameters.job_id}`
    case 'generate':
      return `Generate ${action.parameters.type || 'image'}`
    case 'apply_recommended_settings':
      return 'Apply recommended settings'
    case 'highlight_setting':
      return `Show ${action.parameters.field}`
    case 'set_image':
      return `Set image for ${action.parameters.target}`
    case 'load_upscaler':
      return `Load upscaler ${action.parameters.model_name}`
    case 'unload_upscaler':
      return 'Unload upscaler'
    case 'upscale':
      return 'Start upscale'
    case 'ask_user':
      return `Asked: ${(action.parameters.question as string)?.substring(0, 30)}...`
    case 'download_model':
      return `Download ${action.parameters.model_type} model`
    case 'get_job':
      return `Get job ${action.parameters.job_id}`
    case 'search_jobs':
      return `Search jobs${action.parameters.prompt ? ` for "${action.parameters.prompt}"` : ''}`
    case 'load_job_model':
      return `Load model from job ${action.parameters.job_id}`
    default:
      // For any unknown action, show the type and first parameter if available
      const params = Object.keys(action.parameters || {})
      return params.length > 0
        ? `${action.type}: ${params[0]}=${action.parameters[params[0]]}`
        : action.type
  }
}

// Format message content using markdown
function formatMessage(content: string): string {
  let cleaned = content

  // Strip out json:action blocks - try multiple patterns
  // Pattern 1: Standard format with json:action marker (handles space after backticks)
  cleaned = cleaned.replace(/`{2,3}\s*json:action[\s\S]*?`{2,3}/gi, '')

  // Pattern 2: Any code block containing "actions": array
  cleaned = cleaned.replace(/`{2,3}[\s\S]*?"actions"\s*:\s*\[[\s\S]*?\][\s\S]*?`{2,3}/gi, '')

  // Pattern 3: Lines that are just backticks (cleanup leftovers)
  cleaned = cleaned.replace(/^\s*`{2,3}\s*$/gm, '')

  // Clean up extra whitespace left after removing blocks
  cleaned = cleaned.replace(/\n{3,}/g, '\n\n').trim()

  // If content is empty after stripping, return a placeholder
  if (!cleaned) {
    return '<em>(Action executed)</em>'
  }

  // Parse markdown using marked
  return marked.parse(cleaned) as string
}

// Format timestamp
function formatTime(timestamp: number): string {
  const date = new Date(timestamp)
  return date.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })
}

// Model info for context panel
const modelInfo = computed(() => ({
  loaded: appStore.modelLoaded,
  name: appStore.modelName,
  architecture: appStore.modelArchitecture
}))

// Filter out hidden messages (internal system messages that shouldn't be visible to user)
const visibleMessages = computed(() =>
  store.messages.filter(msg => !msg.hidden)
)
</script>

<template>
  <div class="chat-page">
    <!-- Header -->
    <div class="chat-header">
      <div class="header-left">
        <span class="header-icon">&#129302;</span>
        <div class="header-info">
          <h1 class="header-title">AI Assistant</h1>
          <p class="header-subtitle">
            <span v-if="store.connected" class="status-dot connected"></span>
            <span v-else class="status-dot disconnected"></span>
            {{ store.connected ? 'Connected' : 'Disconnected' }}
            <span v-if="modelInfo.loaded" class="model-badge">
              {{ modelInfo.architecture || 'Model' }}: {{ modelInfo.name?.split('/').pop() }}
            </span>
          </p>
        </div>
      </div>
      <div class="header-actions">
        <button class="btn btn-secondary btn-sm" @click="router.push('/settings')">
          <span class="btn-icon">&#9881;</span>
          Settings
        </button>
        <button class="btn btn-secondary btn-sm" @click="store.clearHistory">
          <span class="btn-icon">&#128465;</span>
          Clear
        </button>
      </div>
    </div>

    <!-- Not enabled state -->
    <div v-if="!store.enabled" class="chat-disabled">
      <div class="disabled-content">
        <span class="disabled-icon">&#129302;</span>
        <h2>Assistant Not Enabled</h2>
        <p>The AI assistant is not configured. Enable it in settings to start chatting.</p>
        <button class="btn btn-primary" @click="router.push('/settings')">
          Configure Assistant
        </button>
      </div>
    </div>

    <!-- Chat area -->
    <template v-else>
      <div class="chat-messages" ref="messagesContainer">
        <!-- Empty state -->
        <div v-if="!store.hasMessages" class="empty-state">
          <div class="empty-icon">&#129302;</div>
          <h2 class="empty-title">Hi! I'm your SD assistant.</h2>
          <p class="empty-text">
            I can help you with generation settings, troubleshooting, and optimizing your workflow.
            Ask me anything about Stable Diffusion!
          </p>
          <div class="suggestion-chips">
            <button class="chip" @click="inputMessage = 'What settings should I use for this model?'">
              Recommended settings
            </button>
            <button class="chip" @click="inputMessage = 'How can I improve image quality?'">
              Improve quality
            </button>
            <button class="chip" @click="inputMessage = 'Explain CFG scale'">
              Explain CFG scale
            </button>
          </div>
        </div>

        <!-- Loading indicator at bottom -->
        <div v-if="store.isLoading" class="message message-assistant loading">
          <div class="typing-dots">
            <span></span><span></span><span></span>
          </div>
        </div>

        <!-- Messages -->
        <div
          v-for="(msg, index) in visibleMessages"
          :key="index"
          :class="['message', `message-${msg.role}`]"
        >
          <div class="message-avatar" v-if="msg.role === 'assistant'">&#129302;</div>
          <div class="message-bubble">
            <div class="message-content" v-html="formatMessage(msg.content)"></div>

            <!-- Action badges for assistant messages -->
            <div v-if="msg.role === 'assistant' && msg.actions?.length" class="message-actions">
              <span class="actions-label">Applied:</span>
              <div class="action-list">
                <span
                  v-for="(action, i) in msg.actions"
                  :key="i"
                  class="action-badge"
                >
                  {{ formatAction(action) }}
                </span>
              </div>
            </div>

            <div class="message-time">{{ formatTime(msg.timestamp) }}</div>
          </div>
          <div class="message-avatar" v-if="msg.role === 'user'">&#128100;</div>
        </div>
      </div>

      <!-- Input area -->
      <div class="chat-input-area">
        <div class="chat-input-container">
          <textarea
            v-model="inputMessage"
            placeholder="Ask me anything about Stable Diffusion..."
            @keydown="handleKeyDown"
            :disabled="store.isLoading || !store.connected"
            rows="1"
            class="chat-input"
          ></textarea>
          <button
            class="send-btn"
            @click="handleSend"
            :disabled="!inputMessage.trim() || store.isLoading || !store.connected"
          >
            <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <line x1="22" y1="2" x2="11" y2="13"/>
              <polygon points="22 2 15 22 11 13 2 9 22 2"/>
            </svg>
          </button>
        </div>
        <p class="input-hint">Press Enter to send, Shift+Enter for new line</p>
      </div>
    </template>
  </div>
</template>

<style scoped>
.chat-page {
  display: flex;
  flex-direction: column;
  height: 100%;
  max-height: calc(100vh - var(--header-height));
  background: var(--bg-primary);
}

/* Header */
.chat-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 16px 24px;
  background: var(--bg-secondary);
  border-bottom: 1px solid var(--border-color);
  flex-shrink: 0;
}

.header-left {
  display: flex;
  align-items: center;
  gap: 16px;
}

.header-icon {
  font-size: 32px;
}

.header-info {
  display: flex;
  flex-direction: column;
}

.header-title {
  font-size: 20px;
  font-weight: 600;
  margin: 0;
  color: var(--text-primary);
}

.header-subtitle {
  font-size: 13px;
  color: var(--text-secondary);
  margin: 4px 0 0;
  display: flex;
  align-items: center;
  gap: 8px;
}

.status-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
}

.status-dot.connected {
  background: var(--accent-success);
}

.status-dot.disconnected {
  background: var(--accent-error);
}

.model-badge {
  background: var(--bg-tertiary);
  padding: 2px 8px;
  border-radius: 4px;
  font-size: 11px;
  margin-left: 8px;
}

.header-actions {
  display: flex;
  gap: 8px;
}

.btn-icon {
  margin-right: 6px;
}

/* Disabled state */
.chat-disabled {
  flex: 1;
  display: flex;
  align-items: center;
  justify-content: center;
}

.disabled-content {
  text-align: center;
  padding: 40px;
}

.disabled-icon {
  font-size: 64px;
  opacity: 0.5;
  display: block;
  margin-bottom: 16px;
}

.disabled-content h2 {
  font-size: 24px;
  margin: 0 0 8px;
  color: var(--text-primary);
}

.disabled-content p {
  color: var(--text-secondary);
  margin: 0 0 24px;
}

/* Messages area */
.chat-messages {
  flex: 1;
  overflow-y: auto;
  padding: 24px;
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.empty-state {
  flex: 1;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  text-align: center;
  padding: 40px;
}

.empty-icon {
  font-size: 64px;
  margin-bottom: 16px;
  opacity: 0.5;
}

.empty-title {
  font-size: 24px;
  font-weight: 600;
  margin: 0 0 8px;
  color: var(--text-primary);
}

.empty-text {
  color: var(--text-secondary);
  max-width: 400px;
  line-height: 1.6;
  margin: 0 0 24px;
}

.suggestion-chips {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  justify-content: center;
}

.chip {
  padding: 8px 16px;
  background: var(--bg-secondary);
  border: 1px solid var(--border-color);
  border-radius: 20px;
  color: var(--text-secondary);
  font-size: 13px;
  cursor: pointer;
  transition: all 0.15s;
}

.chip:hover {
  background: var(--bg-tertiary);
  color: var(--accent-primary);
  border-color: var(--accent-primary);
}

/* Messages */
.message {
  display: flex;
  gap: 12px;
  max-width: 80%;
}

.message-user {
  align-self: flex-end;
  flex-direction: row-reverse;
}

.message-assistant {
  align-self: flex-start;
}

.message-system {
  align-self: center;
  max-width: 90%;
}

.message-avatar {
  width: 36px;
  height: 36px;
  border-radius: 50%;
  background: var(--bg-tertiary);
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 18px;
  flex-shrink: 0;
}

.message-user .message-avatar {
  background: linear-gradient(135deg, var(--accent-primary), var(--accent-purple));
}

.message-bubble {
  padding: 12px 16px;
  border-radius: 16px;
  font-size: 14px;
  line-height: 1.5;
}

.message-user .message-bubble {
  background: linear-gradient(135deg, var(--accent-primary), var(--accent-purple));
  color: white;
  border-bottom-right-radius: 4px;
}

.message-assistant .message-bubble {
  background: var(--bg-secondary);
  color: var(--text-primary);
  border-bottom-left-radius: 4px;
}

.message-system .message-bubble {
  background: rgba(239, 68, 68, 0.15);
  border: 1px solid rgba(239, 68, 68, 0.3);
  color: #fca5a5;
  font-size: 13px;
}

.message-content :deep(code) {
  background: rgba(0, 0, 0, 0.2);
  padding: 2px 6px;
  border-radius: 4px;
  font-family: ui-monospace, monospace;
  font-size: 13px;
}

.message-content :deep(pre) {
  background: rgba(0, 0, 0, 0.2);
  padding: 12px 14px;
  border-radius: 8px;
  overflow-x: auto;
  margin: 10px 0;
}

.message-content :deep(pre code) {
  background: none;
  padding: 0;
  font-size: 13px;
  line-height: 1.5;
}

.message-content :deep(strong) {
  font-weight: 600;
}

.message-content :deep(em) {
  font-style: italic;
}

.message-content :deep(h1),
.message-content :deep(h2),
.message-content :deep(h3),
.message-content :deep(h4) {
  margin: 14px 0 10px;
  font-weight: 600;
  line-height: 1.3;
}

.message-content :deep(h1) { font-size: 1.4em; }
.message-content :deep(h2) { font-size: 1.25em; }
.message-content :deep(h3) { font-size: 1.15em; }
.message-content :deep(h4) { font-size: 1.05em; }

.message-content :deep(ul),
.message-content :deep(ol) {
  margin: 10px 0;
  padding-left: 24px;
}

.message-content :deep(li) {
  margin: 5px 0;
}

.message-content :deep(p) {
  margin: 10px 0;
}

.message-content :deep(p:first-child) {
  margin-top: 0;
}

.message-content :deep(p:last-child) {
  margin-bottom: 0;
}

.message-content :deep(blockquote) {
  border-left: 3px solid var(--accent-primary);
  margin: 10px 0;
  padding-left: 14px;
  color: var(--text-secondary);
}

.message-content :deep(a) {
  color: var(--accent-primary);
  text-decoration: underline;
}

.message-content :deep(a:hover) {
  color: var(--accent-purple);
}

.message-content :deep(hr) {
  border: none;
  border-top: 1px solid var(--border-color);
  margin: 14px 0;
}

.message-content :deep(table) {
  border-collapse: collapse;
  margin: 10px 0;
  font-size: 13px;
  width: 100%;
}

.message-content :deep(th),
.message-content :deep(td) {
  border: 1px solid var(--border-color);
  padding: 8px 12px;
  text-align: left;
}

.message-content :deep(th) {
  background: var(--bg-tertiary);
  font-weight: 600;
}

.message-time {
  font-size: 11px;
  color: var(--text-muted);
  margin-top: 6px;
  text-align: right;
}

.message-user .message-time {
  color: rgba(255, 255, 255, 0.7);
}

/* Actions */
.message-actions {
  margin-top: 10px;
  padding-top: 10px;
  border-top: 1px solid var(--border-color);
}

.actions-label {
  font-size: 11px;
  color: var(--text-muted);
  display: block;
  margin-bottom: 6px;
}

.action-list {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
}

.action-badge {
  font-size: 11px;
  padding: 4px 10px;
  background: rgba(99, 102, 241, 0.2);
  border-radius: 4px;
  color: var(--accent-primary);
}

/* Typing indicator */
.message.loading .message-bubble {
  padding: 16px 20px;
}

.typing-dots {
  display: flex;
  gap: 6px;
  align-items: center;
}

.typing-dots span {
  width: 8px;
  height: 8px;
  background: var(--accent-primary);
  border-radius: 50%;
  animation: bounce 1.4s infinite ease-in-out;
}

.typing-dots span:nth-child(1) { animation-delay: 0s; }
.typing-dots span:nth-child(2) { animation-delay: 0.2s; }
.typing-dots span:nth-child(3) { animation-delay: 0.4s; }

@keyframes bounce {
  0%, 80%, 100% { transform: scale(0.6); opacity: 0.5; }
  40% { transform: scale(1); opacity: 1; }
}

/* Input area */
.chat-input-area {
  padding: 16px 24px 24px;
  background: var(--bg-secondary);
  border-top: 1px solid var(--border-color);
  flex-shrink: 0;
}

.chat-input-container {
  display: flex;
  gap: 12px;
  align-items: flex-end;
}

.chat-input {
  flex: 1;
  resize: none;
  border: 1px solid var(--border-color);
  border-radius: 12px;
  padding: 14px 18px;
  background: var(--bg-primary);
  color: var(--text-primary);
  font-size: 14px;
  font-family: inherit;
  min-height: 50px;
  max-height: 150px;
  line-height: 1.4;
}

.chat-input::placeholder {
  color: var(--text-muted);
}

.chat-input:focus {
  outline: none;
  border-color: var(--accent-primary);
}

.chat-input:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.send-btn {
  width: 50px;
  height: 50px;
  border-radius: 12px;
  border: none;
  background: linear-gradient(135deg, var(--accent-primary), var(--accent-purple));
  color: white;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  transition: opacity 0.2s, transform 0.2s;
  flex-shrink: 0;
}

.send-btn:hover:not(:disabled) {
  transform: scale(1.05);
}

.send-btn:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.input-hint {
  font-size: 11px;
  color: var(--text-muted);
  margin: 8px 0 0;
  text-align: center;
}

/* Scrollbar */
.chat-messages::-webkit-scrollbar {
  width: 8px;
}

.chat-messages::-webkit-scrollbar-track {
  background: transparent;
}

.chat-messages::-webkit-scrollbar-thumb {
  background: var(--bg-tertiary);
  border-radius: 4px;
}

.chat-messages::-webkit-scrollbar-thumb:hover {
  background: var(--border-color);
}

/* Mobile */
@media (max-width: 768px) {
  .chat-header {
    padding: 12px 16px;
  }

  .header-icon {
    font-size: 24px;
  }

  .header-title {
    font-size: 16px;
  }

  .header-actions .btn {
    padding: 6px 10px;
    font-size: 12px;
  }

  .header-actions .btn span:not(.btn-icon) {
    display: none;
  }

  .chat-messages {
    padding: 16px;
  }

  .message {
    max-width: 90%;
  }

  .chat-input-area {
    padding: 12px 16px 16px;
  }

  .suggestion-chips {
    flex-direction: column;
  }
}
</style>
