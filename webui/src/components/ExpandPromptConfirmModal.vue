<template>
  <Modal
    :show="state.open"
    :title="title"
    @close="cancel"
  >
    <!-- Parse error path — template was malformed. Show error + only Cancel. -->
    <div v-if="state.parseError" class="expand-modal-body">
      <div class="parse-error">
        <strong>Prompt template error:</strong>
        <div class="parse-error-msg">{{ state.parseError }}</div>
      </div>
      <p class="hint">
        Fix the template syntax (mismatched braces, invalid pick-N count) and try again.
      </p>
      <div class="modal-actions">
        <button class="btn btn-secondary" @click="cancel">Close</button>
      </div>
    </div>

    <!-- Normal path — show count, prompt sample, optional collapsible all-N list. -->
    <div v-else class="expand-modal-body">
      <div class="summary">
        <div class="summary-row">
          <span class="summary-label">Variations from template:</span>
          <span class="summary-count">{{ state.variations.length }}</span>
        </div>
        <div v-if="batchCount > 1" class="summary-row">
          <span class="summary-label">Batch count per variation:</span>
          <span class="summary-count">{{ batchCount }}</span>
        </div>
        <div class="summary-row total">
          <span class="summary-label">Total queue items:</span>
          <span class="summary-count">{{ state.variations.length }}</span>
        </div>
        <div v-if="batchCount > 1" class="summary-row total">
          <span class="summary-label">Total images:</span>
          <span class="summary-count">{{ state.variations.length * batchCount }}</span>
        </div>
        <div v-if="state.request?.source === 'assistant'" class="summary-row source-tag">
          <span class="summary-label">Requested by:</span>
          <span class="badge badge-assistant">Assistant</span>
        </div>
      </div>

      <!-- Always-visible preview of first 3 — quick sanity check. -->
      <div class="preview-block">
        <div class="preview-block-header">First {{ Math.min(3, state.variations.length) }} variations:</div>
        <ol class="preview-list">
          <li v-for="(v, i) in firstFew" :key="i" :title="v">{{ v }}</li>
        </ol>
      </div>

      <!-- Collapsible full list — only meaningful when there are more than 3
           variations. Closed by default since a 200-item list would dominate
           the modal. -->
      <details v-if="state.variations.length > 3" class="full-list" @toggle="onToggle">
        <summary>
          Show all {{ state.variations.length }} variations
        </summary>
        <ol class="preview-list scrollable">
          <li v-for="(v, i) in state.variations" :key="i" :title="v">{{ v }}</li>
        </ol>
      </details>

      <div class="modal-actions">
        <button class="btn btn-secondary" @click="cancel">Cancel</button>
        <button
          class="btn btn-primary"
          @click="confirm"
          :title="`Create ${state.variations.length} queue item(s)`"
        >
          Create {{ state.variations.length }} job{{ state.variations.length > 1 ? 's' : '' }}
        </button>
      </div>
    </div>
  </Modal>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import Modal from './Modal.vue'
import { useExpandPromptModalBindings } from '../composables/useExpandPromptModal'

const { state, confirm, cancel } = useExpandPromptModalBindings()

const batchCount = computed(() => state.value.request?.batchCount ?? 1)

const title = computed(() => {
  if (state.value.parseError) return 'Prompt template error'
  const n = state.value.variations.length
  return `Create ${n} variation${n > 1 ? 's' : ''} from template?`
})

const firstFew = computed(() => state.value.variations.slice(0, 3))

// Toggling the full list is the user's choice; nothing to do here, but
// the empty handler keeps the <details> a controlled component for any
// future needs (e.g. lazy rendering for huge lists).
function onToggle() {}
</script>

<style scoped>
.expand-modal-body {
  display: flex;
  flex-direction: column;
  gap: 16px;
  min-width: 380px;
  max-width: 600px;
}

.parse-error {
  padding: 12px 14px;
  border: 1px solid var(--accent-error);
  border-radius: var(--border-radius-sm);
  background: rgba(255, 99, 99, 0.06);
  color: var(--text-primary);
}
.parse-error-msg {
  margin-top: 6px;
  font-family: var(--font-mono, monospace);
  font-size: 12px;
  color: var(--text-secondary);
}

.summary {
  display: flex;
  flex-direction: column;
  gap: 6px;
  padding: 12px 14px;
  background: var(--bg-tertiary);
  border-radius: var(--border-radius-sm);
}
.summary-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  font-size: 13px;
}
.summary-label { color: var(--text-secondary); }
.summary-count {
  font-family: var(--font-mono, monospace);
  font-weight: 600;
  color: var(--text-primary);
}
.summary-row.total .summary-count { color: var(--accent-primary); }

.source-tag .badge-assistant {
  padding: 2px 8px;
  border-radius: 10px;
  font-size: 10px;
  font-weight: 600;
  text-transform: uppercase;
  letter-spacing: 0.5px;
  background: var(--accent-primary);
  color: var(--bg-primary);
}

.preview-block-header {
  font-size: 12px;
  font-weight: 600;
  color: var(--text-secondary);
  margin-bottom: 6px;
}
.preview-list {
  margin: 0;
  padding-left: 22px;
  font-family: var(--font-mono, monospace);
  font-size: 12px;
  color: var(--text-primary);
}
.preview-list li {
  padding: 2px 0;
  word-break: break-word;
}
.preview-list.scrollable {
  max-height: 240px;
  overflow-y: auto;
  padding-top: 4px;
}

.full-list summary {
  cursor: pointer;
  font-size: 12px;
  font-weight: 600;
  color: var(--accent-primary);
  user-select: none;
  padding: 4px 0;
}
.full-list summary:hover { text-decoration: underline; }

.hint {
  font-size: 12px;
  color: var(--text-secondary);
  margin: 0;
}

.modal-actions {
  display: flex;
  justify-content: flex-end;
  gap: 8px;
  margin-top: 4px;
}
</style>
