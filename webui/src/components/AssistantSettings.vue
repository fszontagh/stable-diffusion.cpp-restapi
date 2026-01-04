<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { api, type AssistantSettings } from '../api/client'
import { useAssistantStore } from '../stores/assistant'
import { useAppStore } from '../stores/app'

const emit = defineEmits<{
  close: []
}>()

const assistantStore = useAssistantStore()
const appStore = useAppStore()

// State
const loading = ref(true)
const saving = ref(false)
const settings = ref<AssistantSettings | null>(null)
const availableModels = ref<string[]>([])
const newApiKey = ref('')
const showApiKeyInput = ref(false)

// Form values
const form = ref({
  enabled: false,
  endpoint: 'http://localhost:11434',
  model: 'llama3.2',
  temperature: 0.7,
  max_tokens: 2000,
  timeout_seconds: 120,
  max_history_turns: 20,
  proactive_suggestions: true,
  system_prompt: ''
})

// Load settings on mount
onMounted(async () => {
  await loadSettings()
  await loadModels()
})

async function loadSettings() {
  loading.value = true
  try {
    settings.value = await api.getAssistantSettings()
    // Copy to form
    form.value = {
      enabled: settings.value.enabled,
      endpoint: settings.value.endpoint,
      model: settings.value.model,
      temperature: settings.value.temperature,
      max_tokens: settings.value.max_tokens,
      timeout_seconds: settings.value.timeout_seconds,
      max_history_turns: settings.value.max_history_turns,
      proactive_suggestions: settings.value.proactive_suggestions,
      system_prompt: settings.value.system_prompt
    }
  } catch (e) {
    appStore.showToast('Failed to load assistant settings', 'error')
  } finally {
    loading.value = false
  }
}

async function loadModels() {
  try {
    // Use the same models endpoint as Ollama since they use the same backend
    const result = await api.getOllamaModels()
    availableModels.value = result.models || []
  } catch (e) {
    availableModels.value = []
  }
}

async function saveSettings() {
  saving.value = true
  try {
    const updates: Partial<AssistantSettings> & { api_key?: string } = { ...form.value }

    // Include API key if provided
    if (newApiKey.value) {
      updates.api_key = newApiKey.value
    }

    const result = await api.updateAssistantSettings(updates)
    if (result.success) {
      settings.value = result.settings
      newApiKey.value = ''
      showApiKeyInput.value = false
      appStore.showToast('Settings saved', 'success')

      // Refresh assistant status in store
      await assistantStore.refreshStatus()
    }
  } catch (e) {
    appStore.showToast(e instanceof Error ? e.message : 'Failed to save settings', 'error')
  } finally {
    saving.value = false
  }
}

async function testConnection() {
  try {
    // Save current settings first
    await saveSettings()
    // Then check status
    const status = await api.getAssistantStatus()
    if (status.connected) {
      appStore.showToast('Connection successful', 'success')
      // Refresh models
      await loadModels()
    } else {
      appStore.showToast('Connection failed', 'error')
    }
  } catch (e) {
    appStore.showToast('Connection failed', 'error')
  }
}

function closeModal() {
  emit('close')
}

function stopPropagation(e: Event) {
  e.stopPropagation()
}
</script>

<template>
  <div class="modal-overlay" @click="closeModal">
    <div class="modal-content settings-modal" @click="stopPropagation">
      <div class="modal-header">
        <h3>Assistant Settings</h3>
        <button class="close-btn" @click="closeModal">&times;</button>
      </div>

      <div v-if="loading" class="loading-state">
        Loading settings...
      </div>

      <div v-else class="modal-body">
        <div class="form-group">
          <label class="checkbox-label">
            <input type="checkbox" v-model="form.enabled" />
            <span>Enable Assistant</span>
          </label>
          <p class="form-hint">The assistant can help with generation settings and troubleshooting.</p>
        </div>

        <div class="form-group">
          <label class="checkbox-label">
            <input type="checkbox" v-model="form.proactive_suggestions" />
            <span>Proactive Suggestions</span>
          </label>
          <p class="form-hint">Show suggestions when the assistant detects potential improvements.</p>
        </div>

        <div class="form-group">
          <label class="form-label">Endpoint</label>
          <input
            type="text"
            v-model="form.endpoint"
            class="form-input"
            placeholder="http://localhost:11434"
          />
          <p class="form-hint">Ollama-compatible API endpoint</p>
        </div>

        <div class="form-group">
          <label class="form-label">Model</label>
          <div class="model-select-row">
            <select v-model="form.model" class="form-select">
              <option v-if="!availableModels.length" :value="form.model">{{ form.model || 'No models available' }}</option>
              <option v-for="model in availableModels" :key="model" :value="model">
                {{ model }}
              </option>
            </select>
            <input
              type="text"
              v-model="form.model"
              class="form-input model-input"
              placeholder="Or enter model name"
            />
          </div>
        </div>

        <div class="form-row">
          <div class="form-group">
            <label class="form-label">Temperature</label>
            <input
              type="number"
              v-model.number="form.temperature"
              class="form-input"
              min="0"
              max="2"
              step="0.1"
            />
          </div>

          <div class="form-group">
            <label class="form-label">Max Tokens</label>
            <input
              type="number"
              v-model.number="form.max_tokens"
              class="form-input"
              min="100"
              max="8192"
            />
          </div>
        </div>

        <div class="form-row">
          <div class="form-group">
            <label class="form-label">Timeout (seconds)</label>
            <input
              type="number"
              v-model.number="form.timeout_seconds"
              class="form-input"
              min="10"
              max="600"
            />
          </div>

          <div class="form-group">
            <label class="form-label">Max History Turns</label>
            <input
              type="number"
              v-model.number="form.max_history_turns"
              class="form-input"
              min="1"
              max="100"
            />
          </div>
        </div>

        <div class="form-group">
          <label class="form-label">System Prompt</label>
          <textarea
            v-model="form.system_prompt"
            class="form-textarea"
            rows="4"
            placeholder="Custom system prompt (leave empty for default)"
          ></textarea>
        </div>

        <div class="form-group">
          <label class="form-label">API Key</label>
          <div class="api-key-row">
            <span v-if="settings?.has_api_key && !showApiKeyInput" class="api-key-status">
              API key is set
            </span>
            <span v-else-if="!settings?.has_api_key && !showApiKeyInput" class="api-key-status muted">
              No API key set (optional)
            </span>
            <input
              v-if="showApiKeyInput"
              type="password"
              v-model="newApiKey"
              class="form-input"
              placeholder="Enter new API key"
            />
            <button
              class="btn btn-sm"
              @click="showApiKeyInput = !showApiKeyInput"
            >
              {{ showApiKeyInput ? 'Cancel' : (settings?.has_api_key ? 'Change' : 'Set') }}
            </button>
          </div>
        </div>
      </div>

      <div class="modal-footer">
        <button class="btn btn-secondary" @click="testConnection" :disabled="saving">
          Test Connection
        </button>
        <div class="footer-right">
          <button class="btn btn-secondary" @click="closeModal">Cancel</button>
          <button class="btn btn-primary" @click="saveSettings" :disabled="saving">
            {{ saving ? 'Saving...' : 'Save' }}
          </button>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.settings-modal {
  max-width: 520px;
  width: 90vw;
}

.loading-state {
  padding: 2rem;
  text-align: center;
  color: var(--text-secondary);
}

.form-group {
  margin-bottom: 1rem;
}

.form-label {
  display: block;
  margin-bottom: 0.25rem;
  font-size: 0.875rem;
  color: var(--text-secondary);
}

.form-hint {
  margin: 0.25rem 0 0;
  font-size: 0.75rem;
  color: var(--text-tertiary);
}

.form-input,
.form-select,
.form-textarea {
  width: 100%;
  padding: 0.5rem;
  border: 1px solid var(--border-color);
  border-radius: 4px;
  background: var(--bg-primary);
  color: var(--text-primary);
  font-size: 0.875rem;
}

.form-input:focus,
.form-select:focus,
.form-textarea:focus {
  outline: none;
  border-color: var(--accent-color);
}

.form-textarea {
  resize: vertical;
  font-family: monospace;
}

.form-row {
  display: flex;
  gap: 1rem;
}

.form-row .form-group {
  flex: 1;
}

.model-select-row {
  display: flex;
  gap: 0.5rem;
}

.model-select-row .form-select {
  flex: 1;
}

.model-select-row .model-input {
  flex: 1;
}

.checkbox-label {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  cursor: pointer;
}

.checkbox-label input {
  width: auto;
}

.api-key-row {
  display: flex;
  align-items: center;
  gap: 0.5rem;
}

.api-key-row .form-input {
  flex: 1;
}

.api-key-status {
  flex: 1;
  font-size: 0.875rem;
  color: var(--success-color);
}

.api-key-status.muted {
  color: var(--text-secondary);
}

.modal-footer {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding-top: 1rem;
  border-top: 1px solid var(--border-color);
}

.footer-right {
  display: flex;
  gap: 0.5rem;
}

.btn-sm {
  padding: 0.25rem 0.5rem;
  font-size: 0.75rem;
}
</style>
