<template>
  <div class="theme-selector">
    <div class="theme-selector-header">
      <h3>Themes</h3>
      <button class="btn btn-sm btn-primary" @click="openEditor()">
        <span>+</span> Create Custom
      </button>
    </div>

    <!-- System Theme Toggle -->
    <div class="system-theme-option">
      <label class="system-theme-label">
        <input
          type="checkbox"
          :checked="usingSystemTheme"
          @change="handleSystemThemeToggle"
        />
        <span class="checkbox-text">Follow system theme</span>
        <span class="checkbox-hint">Automatically switch between SD Dark and SD Light based on your system preference</span>
      </label>
    </div>

    <div class="theme-grid">
      <!-- Built-in themes -->
      <div
        v-for="theme in builtInThemes"
        :key="theme.id"
        class="theme-card"
        :class="{ active: currentTheme.id === theme.id }"
        @click="selectTheme(theme.id)"
      >
        <div class="theme-preview" :style="getThemePreviewStyle(theme)">
          <div class="theme-preview-bg"></div>
          <div class="theme-preview-accent"></div>
        </div>
        <div class="theme-info">
          <div class="theme-name">{{ theme.name }}</div>
          <div v-if="theme.description" class="theme-description">
            {{ theme.description }}
          </div>
        </div>
        <div v-if="currentTheme.id === theme.id" class="theme-active-badge">
          <span>✓</span> Active
        </div>
      </div>

      <!-- Custom themes -->
      <div
        v-for="theme in customThemes"
        :key="theme.id"
        class="theme-card theme-card-custom"
        :class="{ active: currentTheme.id === theme.id }"
      >
        <div class="theme-preview" :style="getThemePreviewStyle(theme)" @click="selectTheme(theme.id)">
          <div class="theme-preview-bg"></div>
          <div class="theme-preview-accent"></div>
        </div>
        <div class="theme-info" @click="selectTheme(theme.id)">
          <div class="theme-name">
            {{ theme.name }}
            <span class="custom-badge">Custom</span>
          </div>
          <div v-if="theme.description" class="theme-description">
            {{ theme.description }}
          </div>
        </div>
        <div class="theme-actions">
          <button
            class="btn-icon btn-icon-sm"
            @click.stop="openEditor(theme)"
            title="Edit theme"
          >
            ✎
          </button>
          <button
            class="btn-icon btn-icon-sm btn-icon-danger"
            @click.stop="confirmDelete(theme)"
            title="Delete theme"
          >
            🗑
          </button>
        </div>
        <div v-if="currentTheme.id === theme.id" class="theme-active-badge">
          <span>✓</span> Active
        </div>
      </div>
    </div>

    <!-- Theme Editor Modal -->
    <ThemeEditor
      :show="showEditor"
      :initial-theme="editingTheme"
      @close="closeEditor"
      @save="saveTheme"
    />

    <!-- Delete Confirmation Modal -->
    <Modal
      :show="showDeleteConfirm"
      title="Delete Custom Theme"
      @close="closeDeleteConfirm"
    >
      <p>Are you sure you want to delete "{{ themeToDelete?.name }}"?</p>
      <p class="text-muted text-sm mt-2">This action cannot be undone.</p>

      <template #footer>
        <button class="btn btn-secondary" @click="closeDeleteConfirm">
          Cancel
        </button>
        <button class="btn btn-danger" @click="deleteTheme">
          Delete
        </button>
      </template>
    </Modal>
  </div>
</template>

<script setup lang="ts">
import { computed, ref } from 'vue'
import { useTheme } from '../composables/useTheme'
import { BUILT_IN_THEMES, type Theme } from '../types/theme'
import ThemeEditor from './ThemeEditor.vue'
import Modal from './Modal.vue'

const {
  currentTheme,
  customThemes,
  usingSystemTheme,
  setTheme,
  addCustomTheme,
  deleteCustomTheme,
  toggleSystemTheme
} = useTheme()

const builtInThemes = computed(() => BUILT_IN_THEMES)

const showEditor = ref(false)
const editingTheme = ref<Theme | undefined>(undefined)

const showDeleteConfirm = ref(false)
const themeToDelete = ref<Theme | null>(null)

function selectTheme(themeId: string) {
  setTheme(themeId)
}

function handleSystemThemeToggle(event: Event) {
  const target = event.target as HTMLInputElement
  toggleSystemTheme(target.checked)
}

function getThemePreviewStyle(theme: Theme) {
  return {
    '--theme-bg': theme.colors.bgPrimary,
    '--theme-secondary': theme.colors.bgSecondary,
    '--theme-accent': theme.colors.accentPrimary
  }
}

function openEditor(theme?: Theme) {
  editingTheme.value = theme
  showEditor.value = true
}

function closeEditor() {
  showEditor.value = false
  editingTheme.value = undefined
}

function saveTheme(theme: Theme) {
  addCustomTheme(theme)
  setTheme(theme.id)
  closeEditor()
}

function confirmDelete(theme: Theme) {
  themeToDelete.value = theme
  showDeleteConfirm.value = true
}

function closeDeleteConfirm() {
  showDeleteConfirm.value = false
  themeToDelete.value = null
}

function deleteTheme() {
  if (themeToDelete.value) {
    deleteCustomTheme(themeToDelete.value.id)
    closeDeleteConfirm()
  }
}
</script>

<style scoped>
.theme-selector {
  padding: 1.5rem;
}

.theme-selector-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 1.5rem;
}

.theme-selector-header h3 {
  font-size: 1.25rem;
  font-weight: 600;
  color: var(--text-primary);
  margin: 0;
}

/* System Theme Toggle */
.system-theme-option {
  margin-bottom: 1.5rem;
  padding: 1rem;
  background: var(--bg-secondary);
  border: 1px solid var(--border-color);
  border-radius: 8px;
}

.system-theme-label {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 0.75rem;
  cursor: pointer;
}

.system-theme-label input[type="checkbox"] {
  width: 18px;
  height: 18px;
  accent-color: var(--accent-primary);
  cursor: pointer;
}

.checkbox-text {
  font-weight: 600;
  color: var(--text-primary);
}

.checkbox-hint {
  flex-basis: 100%;
  font-size: 0.875rem;
  color: var(--text-secondary);
  margin-left: calc(18px + 0.75rem);
}

.theme-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));
  gap: 1rem;
}

.theme-card {
  position: relative;
  border: 2px solid var(--border-color);
  border-radius: 8px;
  overflow: hidden;
  cursor: pointer;
  transition: all 0.2s;
  background: var(--bg-secondary);
}

.theme-card:hover {
  border-color: var(--accent-primary);
  transform: translateY(-2px);
}

.theme-card.active {
  border-color: var(--accent-primary);
  box-shadow: 0 0 0 2px var(--accent-primary);
}

.theme-preview {
  height: 100px;
  position: relative;
  overflow: hidden;
}

.theme-preview-bg {
  position: absolute;
  inset: 0;
  background: var(--theme-bg);
}

.theme-preview-accent {
  position: absolute;
  bottom: 0;
  left: 0;
  right: 0;
  height: 30%;
  background: linear-gradient(135deg, var(--theme-secondary) 0%, var(--theme-accent) 100%);
  opacity: 0.8;
}

.theme-info {
  padding: 1rem;
}

.theme-name {
  font-weight: 600;
  color: var(--text-primary);
  margin-bottom: 0.25rem;
  display: flex;
  align-items: center;
  gap: 0.5rem;
}

.custom-badge {
  display: inline-block;
  font-size: 0.625rem;
  padding: 0.125rem 0.375rem;
  background: var(--accent-purple);
  color: white;
  border-radius: 3px;
  text-transform: uppercase;
  font-weight: 700;
  letter-spacing: 0.5px;
}

.theme-description {
  font-size: 0.875rem;
  color: var(--text-secondary);
}

.theme-active-badge {
  position: absolute;
  top: 0.5rem;
  right: 0.5rem;
  background: var(--accent-success);
  color: var(--bg-primary);
  font-size: 0.75rem;
  font-weight: 600;
  padding: 0.25rem 0.5rem;
  border-radius: 4px;
  display: flex;
  align-items: center;
  gap: 0.25rem;
}

.theme-actions {
  position: absolute;
  top: 0.5rem;
  left: 0.5rem;
  display: flex;
  gap: 0.25rem;
  opacity: 0;
  transition: opacity 0.2s;
}

.theme-card-custom:hover .theme-actions {
  opacity: 1;
}

.btn-icon-sm {
  padding: 0.375rem;
  background: var(--bg-secondary);
  border: 1px solid var(--border-color);
  border-radius: 4px;
  color: var(--text-primary);
  cursor: pointer;
  font-size: 0.875rem;
  transition: all 0.2s;
}

.btn-icon-sm:hover {
  background: var(--bg-hover);
  border-color: var(--accent-primary);
}

.btn-icon-danger:hover {
  background: var(--accent-error);
  border-color: var(--accent-error);
  color: white;
}
</style>
