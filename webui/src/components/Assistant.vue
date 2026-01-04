<script setup lang="ts">
import { ref, watch, nextTick, onMounted, onUnmounted, computed } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { useAssistantStore } from '../stores/assistant'
import type { AssistantAction } from '../api/client'
import AssistantSettings from './AssistantSettings.vue'
import { marked } from 'marked'

// Configure marked for safe rendering
marked.setOptions({
  breaks: true,  // Convert \n to <br>
  gfm: true,     // GitHub Flavored Markdown
})

const store = useAssistantStore()
const route = useRoute()
const router = useRouter()

const inputMessage = ref('')
const messagesContainer = ref<HTMLElement | null>(null)
const showSettings = ref(false)

// Map routes to view names
const viewMap: Record<string, string> = {
  '/dashboard': 'dashboard',
  '/models': 'models',
  '/generate': 'generate',
  '/upscale': 'upscale',
  '/queue': 'queue',
  '/chat': 'chat'
}

// Hide floating assistant when on the dedicated chat page
const isOnChatPage = computed(() => route.path === '/chat')

// Watch route changes to update current view
watch(() => route.path, (path) => {
  store.setCurrentView(viewMap[path] || 'unknown')
}, { immediate: true })

// Scroll to bottom helper
function scrollToBottom() {
  nextTick(() => {
    if (messagesContainer.value) {
      messagesContainer.value.scrollTop = messagesContainer.value.scrollHeight
    }
  })
}

// Auto-scroll to bottom when new messages arrive
watch(() => store.messages.length, scrollToBottom)

// Scroll to bottom when chat window is opened
watch(() => store.isMinimized, (isMinimized) => {
  if (!isMinimized) {
    scrollToBottom()
  }
})

// Handle navigation events from assistant
function handleNavigate(event: Event) {
  const customEvent = event as CustomEvent<{ view: string }>
  const view = customEvent.detail.view
  if (viewMap[`/${view}`] !== undefined || view in viewMap) {
    router.push(`/${view}`)
  }
}

onMounted(() => {
  store.initialize()
  window.addEventListener('assistant-navigate', handleNavigate)
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
</script>

<template>
  <div v-if="store.enabled && !isOnChatPage" class="assistant-container">
    <!-- Minimized Icon -->
    <button
      v-if="store.isMinimized"
      class="assistant-icon"
      :class="{ 'has-suggestion': store.hasSuggestion }"
      @click="store.toggleOpen"
      title="Open Assistant"
    >
      <span class="icon-robot">&#129302;</span>
      <span v-if="store.hasSuggestion" class="notification-badge"></span>
    </button>

    <!-- Expanded Chat Window -->
    <div v-else class="assistant-window">
      <div class="assistant-header">
        <div class="header-left">
          <span class="header-icon">&#129302;</span>
          <span class="header-title">Assistant</span>
          <span v-if="store.isLoading" class="typing-indicator">
            <span></span><span></span><span></span>
          </span>
          <span v-else-if="!store.connected" class="status-badge disconnected">Offline</span>
        </div>
        <div class="header-actions">
          <button class="icon-btn" @click="showSettings = true" title="Settings">
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <circle cx="12" cy="12" r="3"/>
              <path d="M12 1v4M12 19v4M4.22 4.22l2.83 2.83M16.95 16.95l2.83 2.83M1 12h4M19 12h4M4.22 19.78l2.83-2.83M16.95 7.05l2.83-2.83"/>
            </svg>
          </button>
          <button class="icon-btn" @click="store.clearHistory" title="Clear history">
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <path d="M3 6h18M19 6v14a2 2 0 01-2 2H7a2 2 0 01-2-2V6m3 0V4a2 2 0 012-2h4a2 2 0 012 2v2"/>
            </svg>
          </button>
          <button class="icon-btn" @click="store.close" title="Minimize">
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <line x1="5" y1="12" x2="19" y2="12"/>
            </svg>
          </button>
        </div>
      </div>

      <div class="assistant-messages" ref="messagesContainer">
        <div v-if="!store.hasMessages" class="empty-state">
          <div class="empty-icon">&#129302;</div>
          <p class="empty-title">Hi! I'm your SD assistant.</p>
          <p class="empty-text">Ask me about generation settings, troubleshooting, or let me help optimize your workflow.</p>
        </div>

        <!-- Loading indicator -->
        <div v-if="store.isLoading" class="message message-assistant loading">
          <div class="typing-dots">
            <span></span><span></span><span></span>
          </div>
        </div>

        <!-- Messages (newest first) -->
        <div
          v-for="(msg, index) in store.messages"
          :key="index"
          :class="['message', `message-${msg.role}`]"
        >
          <div class="message-content" v-html="formatMessage(msg.content)"></div>

          <!-- Action badges for assistant messages -->
          <div v-if="msg.role === 'assistant' && msg.actions?.length" class="message-actions">
            <span class="actions-label">Applied changes:</span>
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
      </div>

      <div class="assistant-input">
        <textarea
          v-model="inputMessage"
          placeholder="Ask me anything..."
          @keydown="handleKeyDown"
          :disabled="store.isLoading || !store.connected"
          rows="1"
        ></textarea>
        <button
          class="send-btn"
          @click="handleSend"
          :disabled="!inputMessage.trim() || store.isLoading || !store.connected"
        >
          <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
            <line x1="22" y1="2" x2="11" y2="13"/>
            <polygon points="22 2 15 22 11 13 2 9 22 2"/>
          </svg>
        </button>
      </div>
    </div>

    <!-- Settings Modal -->
    <AssistantSettings
      v-if="showSettings"
      @close="showSettings = false"
    />
  </div>
</template>

<style scoped>
.assistant-container {
  position: fixed;
  bottom: 20px;
  right: 20px;
  z-index: 1000;
  font-family: system-ui, -apple-system, sans-serif;
}

/* Floating Icon Button */
.assistant-icon {
  width: 56px;
  height: 56px;
  border-radius: 50%;
  background: linear-gradient(135deg, #6366f1, #8b5cf6);
  border: none;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  box-shadow: 0 4px 12px rgba(99, 102, 241, 0.4);
  transition: transform 0.2s, box-shadow 0.2s;
  position: relative;
}

.assistant-icon:hover {
  transform: scale(1.05);
  box-shadow: 0 6px 20px rgba(99, 102, 241, 0.5);
}

.assistant-icon.has-suggestion {
  animation: pulse 2s infinite;
}

@keyframes pulse {
  0%, 100% { box-shadow: 0 4px 12px rgba(99, 102, 241, 0.4); }
  50% { box-shadow: 0 4px 24px rgba(99, 102, 241, 0.7); }
}

.icon-robot {
  font-size: 28px;
  line-height: 1;
}

.notification-badge {
  position: absolute;
  top: -2px;
  right: -2px;
  width: 14px;
  height: 14px;
  background: #ef4444;
  border-radius: 50%;
  border: 2px solid #1a1a2e;
}

/* Chat Window */
.assistant-window {
  width: 380px;
  height: 520px;
  background: #1a1a2e;
  border-radius: 16px;
  box-shadow: 0 8px 32px rgba(0, 0, 0, 0.5);
  display: flex;
  flex-direction: column;
  overflow: hidden;
  border: 1px solid #2d2d44;
}

/* Header */
.assistant-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 12px 16px;
  background: #252542;
  border-bottom: 1px solid #2d2d44;
}

.header-left {
  display: flex;
  align-items: center;
  gap: 8px;
}

.header-icon {
  font-size: 20px;
}

.header-title {
  font-weight: 600;
  color: #e2e8f0;
  font-size: 15px;
}

.status-badge {
  font-size: 11px;
  padding: 2px 6px;
  border-radius: 4px;
  font-weight: 500;
}

.status-badge.disconnected {
  background: rgba(239, 68, 68, 0.2);
  color: #ef4444;
}

.header-actions {
  display: flex;
  gap: 4px;
}

.icon-btn {
  width: 32px;
  height: 32px;
  border-radius: 8px;
  border: none;
  background: transparent;
  color: #94a3b8;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  transition: background 0.2s, color 0.2s;
}

.icon-btn:hover {
  background: #2d2d44;
  color: #e2e8f0;
}

/* Messages Area */
.assistant-messages {
  flex: 1;
  overflow-y: auto;
  padding: 16px;
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.empty-state {
  text-align: center;
  padding: 40px 20px;
  color: #94a3b8;
}

.empty-icon {
  font-size: 48px;
  margin-bottom: 16px;
  opacity: 0.5;
}

.empty-title {
  font-size: 16px;
  font-weight: 600;
  color: #e2e8f0;
  margin: 0 0 8px;
}

.empty-text {
  font-size: 14px;
  margin: 0;
  line-height: 1.5;
}

/* Messages */
.message {
  max-width: 85%;
  padding: 10px 14px;
  border-radius: 16px;
  font-size: 14px;
  line-height: 1.5;
  position: relative;
}

.message-user {
  align-self: flex-end;
  background: linear-gradient(135deg, #6366f1, #8b5cf6);
  color: white;
  border-bottom-right-radius: 4px;
}

.message-assistant {
  align-self: flex-start;
  background: #252542;
  color: #e2e8f0;
  border-bottom-left-radius: 4px;
}

.message-system {
  align-self: center;
  background: rgba(239, 68, 68, 0.15);
  border: 1px solid rgba(239, 68, 68, 0.3);
  color: #fca5a5;
  max-width: 95%;
  font-size: 13px;
}

.message-content :deep(code) {
  background: rgba(0, 0, 0, 0.3);
  padding: 2px 6px;
  border-radius: 4px;
  font-family: ui-monospace, monospace;
  font-size: 13px;
}

.message-content :deep(pre) {
  background: rgba(0, 0, 0, 0.3);
  padding: 10px 12px;
  border-radius: 6px;
  overflow-x: auto;
  margin: 8px 0;
}

.message-content :deep(pre code) {
  background: none;
  padding: 0;
  font-size: 12px;
  line-height: 1.4;
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
  margin: 12px 0 8px;
  font-weight: 600;
  line-height: 1.3;
}

.message-content :deep(h1) { font-size: 1.3em; }
.message-content :deep(h2) { font-size: 1.2em; }
.message-content :deep(h3) { font-size: 1.1em; }
.message-content :deep(h4) { font-size: 1em; }

.message-content :deep(ul),
.message-content :deep(ol) {
  margin: 8px 0;
  padding-left: 20px;
}

.message-content :deep(li) {
  margin: 4px 0;
}

.message-content :deep(p) {
  margin: 8px 0;
}

.message-content :deep(p:first-child) {
  margin-top: 0;
}

.message-content :deep(p:last-child) {
  margin-bottom: 0;
}

.message-content :deep(blockquote) {
  border-left: 3px solid #6366f1;
  margin: 8px 0;
  padding-left: 12px;
  color: #94a3b8;
}

.message-content :deep(a) {
  color: #818cf8;
  text-decoration: underline;
}

.message-content :deep(a:hover) {
  color: #a5b4fc;
}

.message-content :deep(hr) {
  border: none;
  border-top: 1px solid #2d2d44;
  margin: 12px 0;
}

.message-content :deep(table) {
  border-collapse: collapse;
  margin: 8px 0;
  font-size: 13px;
}

.message-content :deep(th),
.message-content :deep(td) {
  border: 1px solid #2d2d44;
  padding: 6px 10px;
}

.message-content :deep(th) {
  background: rgba(0, 0, 0, 0.2);
  font-weight: 600;
}

.message-time {
  font-size: 10px;
  color: #64748b;
  margin-top: 4px;
  text-align: right;
}

.message-user .message-time {
  color: rgba(255, 255, 255, 0.7);
}

/* Actions */
.message-actions {
  margin-top: 8px;
  padding-top: 8px;
  border-top: 1px solid #2d2d44;
}

.actions-label {
  font-size: 11px;
  color: #64748b;
  display: block;
  margin-bottom: 6px;
}

.action-list {
  display: flex;
  flex-wrap: wrap;
  gap: 4px;
}

.action-badge {
  font-size: 11px;
  padding: 3px 8px;
  background: rgba(99, 102, 241, 0.2);
  border-radius: 4px;
  color: #a5b4fc;
}

/* Typing Indicator */
.typing-indicator,
.typing-dots {
  display: flex;
  gap: 4px;
  align-items: center;
}

.typing-indicator span,
.typing-dots span {
  width: 6px;
  height: 6px;
  background: #6366f1;
  border-radius: 50%;
  animation: bounce 1.4s infinite ease-in-out;
}

.typing-indicator span:nth-child(1),
.typing-dots span:nth-child(1) { animation-delay: 0s; }
.typing-indicator span:nth-child(2),
.typing-dots span:nth-child(2) { animation-delay: 0.2s; }
.typing-indicator span:nth-child(3),
.typing-dots span:nth-child(3) { animation-delay: 0.4s; }

@keyframes bounce {
  0%, 80%, 100% { transform: scale(0.6); opacity: 0.5; }
  40% { transform: scale(1); opacity: 1; }
}

.message.loading {
  padding: 12px 16px;
}

/* Input Area */
.assistant-input {
  display: flex;
  gap: 8px;
  padding: 12px 16px;
  background: #252542;
  border-top: 1px solid #2d2d44;
}

.assistant-input textarea {
  flex: 1;
  resize: none;
  border: 1px solid #2d2d44;
  border-radius: 12px;
  padding: 10px 14px;
  background: #1a1a2e;
  color: #e2e8f0;
  font-size: 14px;
  font-family: inherit;
  max-height: 100px;
  line-height: 1.4;
}

.assistant-input textarea::placeholder {
  color: #64748b;
}

.assistant-input textarea:focus {
  outline: none;
  border-color: #6366f1;
}

.assistant-input textarea:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.send-btn {
  width: 42px;
  height: 42px;
  border-radius: 12px;
  border: none;
  background: linear-gradient(135deg, #6366f1, #8b5cf6);
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

/* Scrollbar */
.assistant-messages::-webkit-scrollbar {
  width: 6px;
}

.assistant-messages::-webkit-scrollbar-track {
  background: transparent;
}

.assistant-messages::-webkit-scrollbar-thumb {
  background: #2d2d44;
  border-radius: 3px;
}

.assistant-messages::-webkit-scrollbar-thumb:hover {
  background: #3d3d54;
}

/* Mobile Responsiveness */
@media (max-width: 480px) {
  .assistant-container {
    bottom: 10px;
    right: 10px;
    left: 10px;
  }

  .assistant-window {
    width: 100%;
    height: calc(100vh - 100px);
    max-height: 600px;
  }
}
</style>
