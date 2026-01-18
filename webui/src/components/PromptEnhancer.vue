<script setup lang="ts">
import { ref, computed } from 'vue'
import { useRouter } from 'vue-router'
import { api, type PromptHistoryEntry } from '../api/client'
import { useAppStore } from '../stores/app'
import Modal from './Modal.vue'

const props = defineProps<{
  modelValue: string
}>()

const emit = defineEmits<{
  'update:modelValue': [value: string]
}>()

const router = useRouter()
const store = useAppStore()

// State
const enhancing = ref(false)
const showPreview = ref(false)
const showHistory = ref(false)
const enhancedPrompt = ref('')
const currentEntryId = ref('')
const history = ref<PromptHistoryEntry[]>([])
const loadingHistory = ref(false)
const historyTotalCount = ref(0)

// Computed
const canEnhance = computed(() => props.modelValue.trim().length > 0)

// Enhance the current prompt
async function enhancePrompt() {
  if (!canEnhance.value) {
    store.showToast('Please enter a prompt first', 'warning')
    return
  }

  enhancing.value = true
  try {
    const result = await api.enhancePrompt({ prompt: props.modelValue })
    if (result.success && result.enhanced_prompt) {
      enhancedPrompt.value = result.enhanced_prompt
      currentEntryId.value = result.id || ''
      showPreview.value = true
    } else {
      store.showToast(result.error || 'Enhancement failed', 'error')
    }
  } catch (e) {
    store.showToast(e instanceof Error ? e.message : 'Enhancement failed', 'error')
  } finally {
    enhancing.value = false
  }
}

// Apply enhanced prompt
function applyEnhanced() {
  emit('update:modelValue', enhancedPrompt.value)
  showPreview.value = false
  store.showToast('Enhanced prompt applied', 'success')
}

// Load history
async function loadHistory() {
  loadingHistory.value = true
  try {
    const result = await api.getPromptHistory(20)
    history.value = result.items
    historyTotalCount.value = result.total_count
    showHistory.value = true
  } catch (e) {
    store.showToast('Failed to load history', 'error')
  } finally {
    loadingHistory.value = false
  }
}

// Apply from history
function applyOriginalFromHistory(entry: PromptHistoryEntry, event: Event) {
  event.stopPropagation()
  emit('update:modelValue', entry.original_prompt)
  showHistory.value = false
  store.showToast('Original prompt applied', 'success')
}

function applyEnhancedFromHistory(entry: PromptHistoryEntry, event: Event) {
  event.stopPropagation()
  emit('update:modelValue', entry.enhanced_prompt)
  showHistory.value = false
  store.showToast('Enhanced prompt applied', 'success')
}

// Delete history entry
async function deleteHistoryEntry(entry: PromptHistoryEntry, event: Event) {
  event.stopPropagation()
  try {
    await api.deletePromptHistoryEntry(entry.id)
    history.value = history.value.filter(h => h.id !== entry.id)
    historyTotalCount.value--
    store.showToast('Entry deleted', 'success')
  } catch (e) {
    store.showToast('Failed to delete entry', 'error')
  }
}

// Format timestamp
function formatDate(timestamp: number): string {
  return new Date(timestamp * 1000).toLocaleString()
}

// Truncate text for display
function truncate(text: string, maxLength: number): string {
  if (text.length <= maxLength) return text
  return text.substring(0, maxLength) + '...'
}
</script>

<template>
  <div class="prompt-enhancer">
    <button
      class="btn btn-sm btn-secondary enhancer-btn"
      :disabled="enhancing || !canEnhance"
      @click="enhancePrompt"
      title="Enhance prompt with AI"
    >
      <span v-if="enhancing" class="spinner-inline"></span>
      <span v-else>Enhance</span>
    </button>
    <button
      class="btn btn-sm btn-secondary enhancer-btn"
      :disabled="loadingHistory"
      @click="loadHistory"
      title="View enhancement history"
    >
      <span v-if="loadingHistory" class="spinner-inline"></span>
      <span v-else>History</span>
    </button>
    <button
      class="btn btn-sm btn-secondary enhancer-btn settings-btn"
      @click="router.push('/settings')"
      title="Settings"
    >
      <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
        <circle cx="12" cy="12" r="3"/>
        <path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2-2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2h-.09a1.65 1.65 0 0 0-1.51 1z"/>
      </svg>
    </button>

    <!-- Preview Modal -->
    <Modal :show="showPreview" title="Enhanced Prompt" @close="showPreview = false">
      <div class="preview-content">
        <div class="prompt-comparison">
          <div class="comparison-section">
            <h4 class="comparison-label">Original</h4>
            <div class="comparison-text original-text">{{ modelValue }}</div>
          </div>
          <div class="comparison-arrow">
            <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
              <path d="M5 12h14M12 5l7 7-7 7"/>
            </svg>
          </div>
          <div class="comparison-section">
            <h4 class="comparison-label">Enhanced</h4>
            <div class="comparison-text enhanced-text">{{ enhancedPrompt }}</div>
          </div>
        </div>
      </div>
      <template #footer>
        <button class="btn btn-secondary" @click="showPreview = false">Cancel</button>
        <button class="btn btn-primary" @click="applyEnhanced">Apply Enhanced</button>
      </template>
    </Modal>

    <!-- History Modal -->
    <Modal :show="showHistory" title="Enhancement History" @close="showHistory = false">
      <div class="history-list">
        <div v-if="history.length === 0" class="empty-state">
          No enhancement history yet
        </div>
        <div
          v-for="entry in history"
          :key="entry.id"
          class="history-entry"
          :class="{ 'history-entry-failed': !entry.success }"
        >
          <div class="entry-content">
            <div class="entry-row">
              <span class="entry-label">Original:</span>
              <span class="entry-text">{{ truncate(entry.original_prompt, 100) }}</span>
              <button
                class="btn btn-sm btn-secondary entry-apply-btn"
                @click="applyOriginalFromHistory(entry, $event)"
                title="Use original prompt"
              >
                Use
              </button>
            </div>
            <div class="entry-row" v-if="entry.success">
              <span class="entry-label">Enhanced:</span>
              <span class="entry-text enhanced">{{ truncate(entry.enhanced_prompt, 100) }}</span>
              <button
                class="btn btn-sm btn-primary entry-apply-btn"
                @click="applyEnhancedFromHistory(entry, $event)"
                title="Use enhanced prompt"
              >
                Use
              </button>
            </div>
            <div class="entry-error" v-else>{{ entry.error_message }}</div>
          </div>
          <div class="entry-meta">
            <span class="entry-model">{{ entry.model_used }}</span>
            <span class="entry-date">{{ formatDate(entry.created_at) }}</span>
            <button
              class="btn btn-sm btn-danger entry-delete"
              @click="deleteHistoryEntry(entry, $event)"
              title="Delete"
            >
              &times;
            </button>
          </div>
        </div>
      </div>
      <template #footer>
        <div class="history-footer">
          <span class="history-count">{{ historyTotalCount }} entries</span>
          <button class="btn btn-secondary" @click="showHistory = false">Close</button>
        </div>
      </template>
    </Modal>
  </div>
</template>

<style scoped>
.prompt-enhancer {
  display: flex;
  gap: 0.5rem;
}

.enhancer-btn {
  padding: 0.25rem 0.5rem;
  font-size: 0.75rem;
}

.settings-btn {
  padding: 0.25rem;
  display: flex;
  align-items: center;
  justify-content: center;
}

.settings-btn svg {
  display: block;
}

.spinner-inline {
  display: inline-block;
  width: 12px;
  height: 12px;
  border: 2px solid rgba(255, 255, 255, 0.3);
  border-radius: 50%;
  border-top-color: #fff;
  animation: spin 1s linear infinite;
}

@keyframes spin {
  to { transform: rotate(360deg); }
}

.preview-content {
  max-height: 60vh;
  overflow-y: auto;
}

.prompt-comparison {
  display: flex;
  flex-direction: column;
  gap: 1rem;
}

.comparison-section {
  flex: 1;
}

.comparison-label {
  margin: 0 0 0.5rem 0;
  font-size: 0.875rem;
  color: var(--text-secondary);
  font-weight: 600;
}

.comparison-text {
  padding: 0.75rem;
  background: var(--bg-secondary);
  border-radius: 4px;
  font-size: 0.875rem;
  line-height: 1.5;
  white-space: pre-wrap;
  word-break: break-word;
}

.original-text {
  border-left: 3px solid var(--text-secondary);
}

.enhanced-text {
  border-left: 3px solid var(--accent-color);
}

.comparison-arrow {
  display: flex;
  justify-content: center;
  color: var(--text-secondary);
}

.history-list {
  max-height: 50vh;
  overflow-y: auto;
}

.empty-state {
  text-align: center;
  padding: 2rem;
  color: var(--text-secondary);
}

.history-entry {
  padding: 0.75rem;
  margin-bottom: 0.5rem;
  background: var(--bg-secondary);
  border-radius: 4px;
  transition: background 0.2s;
}

.history-entry:hover {
  background: var(--bg-tertiary);
}

.history-entry-failed {
  opacity: 0.6;
}

.entry-content {
  font-size: 0.875rem;
  margin-bottom: 0.5rem;
}

.entry-row {
  display: flex;
  align-items: flex-start;
  gap: 0.5rem;
  margin-bottom: 0.5rem;
}

.entry-row:last-child {
  margin-bottom: 0;
}

.entry-label {
  flex-shrink: 0;
  font-size: 0.75rem;
  color: var(--text-tertiary);
  width: 60px;
  padding-top: 0.125rem;
}

.entry-text {
  flex: 1;
  color: var(--text-secondary);
  line-height: 1.4;
  word-break: break-word;
}

.entry-text.enhanced {
  color: var(--text-primary);
}

.entry-apply-btn {
  flex-shrink: 0;
  padding: 0.125rem 0.5rem;
  font-size: 0.7rem;
}

.entry-error {
  color: var(--error-color);
  font-style: italic;
}

.entry-meta {
  display: flex;
  gap: 0.75rem;
  font-size: 0.75rem;
  color: var(--text-tertiary);
  align-items: center;
}

.entry-model {
  background: var(--bg-tertiary);
  padding: 0.125rem 0.375rem;
  border-radius: 3px;
}

.entry-date {
  flex: 1;
}

.entry-delete {
  padding: 0.125rem 0.375rem;
  font-size: 0.875rem;
  opacity: 0;
  transition: opacity 0.2s;
}

.history-entry:hover .entry-delete {
  opacity: 1;
}

.history-footer {
  display: flex;
  justify-content: space-between;
  align-items: center;
  width: 100%;
}

.history-count {
  color: var(--text-secondary);
  font-size: 0.875rem;
}
</style>
