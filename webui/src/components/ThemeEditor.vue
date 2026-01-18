<template>
  <Modal :show="show" title="Theme Editor" @close="handleClose">
    <div class="theme-editor">
      <div class="form-group">
        <label class="form-label">Theme Name</label>
        <input
          v-model="editingTheme.name"
          type="text"
          class="form-input"
          placeholder="My Custom Theme"
        />
      </div>

      <div class="form-group">
        <label class="form-label">Description (Optional)</label>
        <input
          v-model="editingTheme.description"
          type="text"
          class="form-input"
          placeholder="A beautiful custom theme"
        />
      </div>

      <div class="color-sections">
        <div class="color-section">
          <h4>Background Colors</h4>
          <div class="color-grid">
            <div class="color-input-group">
              <label>Primary Background</label>
              <input v-model="editingTheme.colors.bgPrimary" type="color" class="color-input" />
              <input v-model="editingTheme.colors.bgPrimary" type="text" class="hex-input" />
            </div>
            <div class="color-input-group">
              <label>Secondary Background</label>
              <input v-model="editingTheme.colors.bgSecondary" type="color" class="color-input" />
              <input v-model="editingTheme.colors.bgSecondary" type="text" class="hex-input" />
            </div>
            <div class="color-input-group">
              <label>Tertiary Background</label>
              <input v-model="editingTheme.colors.bgTertiary" type="color" class="color-input" />
              <input v-model="editingTheme.colors.bgTertiary" type="text" class="hex-input" />
            </div>
            <div class="color-input-group">
              <label>Input Background</label>
              <input v-model="editingTheme.colors.bgInput" type="color" class="color-input" />
              <input v-model="editingTheme.colors.bgInput" type="text" class="hex-input" />
            </div>
            <div class="color-input-group">
              <label>Hover Background</label>
              <input v-model="editingTheme.colors.bgHover" type="color" class="color-input" />
              <input v-model="editingTheme.colors.bgHover" type="text" class="hex-input" />
            </div>
          </div>
        </div>

        <div class="color-section">
          <h4>Text Colors</h4>
          <div class="color-grid">
            <div class="color-input-group">
              <label>Primary Text</label>
              <input v-model="editingTheme.colors.textPrimary" type="color" class="color-input" />
              <input v-model="editingTheme.colors.textPrimary" type="text" class="hex-input" />
            </div>
            <div class="color-input-group">
              <label>Secondary Text</label>
              <input v-model="editingTheme.colors.textSecondary" type="color" class="color-input" />
              <input v-model="editingTheme.colors.textSecondary" type="text" class="hex-input" />
            </div>
            <div class="color-input-group">
              <label>Muted Text</label>
              <input v-model="editingTheme.colors.textMuted" type="color" class="color-input" />
              <input v-model="editingTheme.colors.textMuted" type="text" class="hex-input" />
            </div>
          </div>
        </div>

        <div class="color-section">
          <h4>Accent Colors</h4>
          <div class="color-grid">
            <div class="color-input-group">
              <label>Primary Accent</label>
              <input v-model="editingTheme.colors.accentPrimary" type="color" class="color-input" />
              <input v-model="editingTheme.colors.accentPrimary" type="text" class="hex-input" />
            </div>
            <div class="color-input-group">
              <label>Success</label>
              <input v-model="editingTheme.colors.accentSuccess" type="color" class="color-input" />
              <input v-model="editingTheme.colors.accentSuccess" type="text" class="hex-input" />
            </div>
            <div class="color-input-group">
              <label>Warning</label>
              <input v-model="editingTheme.colors.accentWarning" type="color" class="color-input" />
              <input v-model="editingTheme.colors.accentWarning" type="text" class="hex-input" />
            </div>
            <div class="color-input-group">
              <label>Error</label>
              <input v-model="editingTheme.colors.accentError" type="color" class="color-input" />
              <input v-model="editingTheme.colors.accentError" type="text" class="hex-input" />
            </div>
            <div class="color-input-group">
              <label>Purple</label>
              <input v-model="editingTheme.colors.accentPurple" type="color" class="color-input" />
              <input v-model="editingTheme.colors.accentPurple" type="text" class="hex-input" />
            </div>
          </div>
        </div>

        <div class="color-section">
          <h4>Border & Structure</h4>
          <div class="color-grid">
            <div class="color-input-group">
              <label>Border Color</label>
              <input v-model="editingTheme.colors.borderColor" type="color" class="color-input" />
              <input v-model="editingTheme.colors.borderColor" type="text" class="hex-input" />
            </div>
          </div>
        </div>
      </div>

      <div class="preview-section">
        <h4>Preview</h4>
        <div class="preview-box" :style="getPreviewStyle()">
          <div class="preview-card">
            <h5>Sample Card</h5>
            <p>This is how your theme will look with primary text.</p>
            <p class="muted">This is muted text for less important information.</p>
            <div class="preview-buttons">
              <button class="preview-btn preview-btn-primary">Primary Button</button>
              <button class="preview-btn preview-btn-success">Success</button>
              <button class="preview-btn preview-btn-error">Error</button>
            </div>
          </div>
        </div>
      </div>
    </div>

    <template #footer>
      <button class="btn btn-secondary" @click="handleClose">
        Cancel
      </button>
      <button class="btn btn-primary" @click="handleSave">
        Save Theme
      </button>
    </template>
  </Modal>
</template>

<script setup lang="ts">
import { ref, watch } from 'vue'
import Modal from './Modal.vue'
import { DEFAULT_THEME, type Theme } from '../types/theme'

const props = defineProps<{
  show: boolean
  initialTheme?: Theme
}>()

const emit = defineEmits<{
  close: []
  save: [theme: Theme]
}>()

const editingTheme = ref<Theme>(getInitialTheme())

function getInitialTheme(): Theme {
  if (props.initialTheme) {
    return JSON.parse(JSON.stringify(props.initialTheme))
  }
  return {
    id: `custom-${Date.now()}`,
    name: 'My Custom Theme',
    description: '',
    isBuiltIn: false,
    colors: JSON.parse(JSON.stringify(DEFAULT_THEME.colors))
  }
}

watch(() => props.show, (isShown) => {
  if (isShown) {
    editingTheme.value = getInitialTheme()
  }
})

function getPreviewStyle() {
  const c = editingTheme.value.colors
  return {
    '--preview-bg-primary': c.bgPrimary,
    '--preview-bg-secondary': c.bgSecondary,
    '--preview-text-primary': c.textPrimary,
    '--preview-text-secondary': c.textSecondary,
    '--preview-text-muted': c.textMuted,
    '--preview-accent-primary': c.accentPrimary,
    '--preview-accent-success': c.accentSuccess,
    '--preview-accent-error': c.accentError,
    '--preview-border': c.borderColor
  }
}

function handleClose() {
  emit('close')
}

function handleSave() {
  emit('save', editingTheme.value)
}
</script>

<style scoped>
.theme-editor {
  display: flex;
  flex-direction: column;
  gap: 1.5rem;
}

.color-sections {
  display: flex;
  flex-direction: column;
  gap: 1.5rem;
}

.color-section h4 {
  font-size: 1rem;
  font-weight: 600;
  margin-bottom: 0.75rem;
  color: var(--text-primary);
}

.color-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(200px, 1fr));
  gap: 1rem;
}

.color-input-group {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.color-input-group label {
  font-size: 0.875rem;
  color: var(--text-secondary);
  font-weight: 500;
}

.color-input {
  width: 100%;
  height: 48px;
  border: 1px solid var(--border-color);
  border-radius: 4px;
  cursor: pointer;
}

.hex-input {
  font-family: var(--font-mono);
  font-size: 0.875rem;
  padding: 0.5rem;
  background: var(--bg-input);
  border: 1px solid var(--border-color);
  border-radius: 4px;
  color: var(--text-primary);
}

.hex-input:focus {
  outline: none;
  border-color: var(--accent-primary);
}

.preview-section h4 {
  font-size: 1rem;
  font-weight: 600;
  margin-bottom: 0.75rem;
  color: var(--text-primary);
}

.preview-box {
  background: var(--preview-bg-primary);
  border: 1px solid var(--preview-border);
  border-radius: 8px;
  padding: 1.5rem;
  min-height: 200px;
}

.preview-card {
  background: var(--preview-bg-secondary);
  border: 1px solid var(--preview-border);
  border-radius: 8px;
  padding: 1.5rem;
}

.preview-card h5 {
  color: var(--preview-text-primary);
  margin-bottom: 0.75rem;
  font-size: 1.125rem;
}

.preview-card p {
  color: var(--preview-text-secondary);
  margin-bottom: 0.5rem;
}

.preview-card p.muted {
  color: var(--preview-text-muted);
  font-size: 0.875rem;
}

.preview-buttons {
  display: flex;
  gap: 0.5rem;
  margin-top: 1rem;
}

.preview-btn {
  padding: 0.5rem 1rem;
  border: none;
  border-radius: 4px;
  font-size: 0.875rem;
  font-weight: 500;
  cursor: pointer;
  transition: opacity 0.2s;
}

.preview-btn:hover {
  opacity: 0.9;
}

.preview-btn-primary {
  background: var(--preview-accent-primary);
  color: var(--preview-bg-primary);
}

.preview-btn-success {
  background: var(--preview-accent-success);
  color: var(--preview-bg-primary);
}

.preview-btn-error {
  background: var(--preview-accent-error);
  color: white;
}
</style>
