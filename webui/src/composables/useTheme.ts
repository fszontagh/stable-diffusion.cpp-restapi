import { ref, watch } from 'vue'
import { BUILT_IN_THEMES, DEFAULT_THEME, type Theme } from '../types/theme'

const STORAGE_KEY = 'sdcpp-theme'
const CUSTOM_THEMES_KEY = 'sdcpp-custom-themes'

const currentTheme = ref<Theme>(DEFAULT_THEME)
const customThemes = ref<Theme[]>([])

// Load saved theme and custom themes from localStorage
function loadTheme() {
  try {
    const saved = localStorage.getItem(STORAGE_KEY)
    if (saved) {
      const themeId = saved
      const theme = [...BUILT_IN_THEMES, ...customThemes.value].find(t => t.id === themeId)
      if (theme) {
        currentTheme.value = theme
      }
    }

    const savedCustomThemes = localStorage.getItem(CUSTOM_THEMES_KEY)
    if (savedCustomThemes) {
      customThemes.value = JSON.parse(savedCustomThemes)
    }
  } catch (e) {
    console.error('Failed to load theme:', e)
  }
}

// Save theme to localStorage
function saveTheme() {
  try {
    localStorage.setItem(STORAGE_KEY, currentTheme.value.id)
  } catch (e) {
    console.error('Failed to save theme:', e)
  }
}

// Save custom themes to localStorage
function saveCustomThemes() {
  try {
    localStorage.setItem(CUSTOM_THEMES_KEY, JSON.stringify(customThemes.value))
  } catch (e) {
    console.error('Failed to save custom themes:', e)
  }
}

// Apply theme by updating CSS variables
function applyTheme(theme: Theme) {
  const root = document.documentElement
  const colors = theme.colors

  root.style.setProperty('--bg-primary', colors.bgPrimary)
  root.style.setProperty('--bg-secondary', colors.bgSecondary)
  root.style.setProperty('--bg-tertiary', colors.bgTertiary)
  root.style.setProperty('--bg-input', colors.bgInput)
  root.style.setProperty('--bg-hover', colors.bgHover)

  root.style.setProperty('--text-primary', colors.textPrimary)
  root.style.setProperty('--text-secondary', colors.textSecondary)
  root.style.setProperty('--text-muted', colors.textMuted)

  root.style.setProperty('--accent-primary', colors.accentPrimary)
  root.style.setProperty('--accent-success', colors.accentSuccess)
  root.style.setProperty('--accent-warning', colors.accentWarning)
  root.style.setProperty('--accent-error', colors.accentError)
  root.style.setProperty('--accent-purple', colors.accentPurple)

  root.style.setProperty('--border-color', colors.borderColor)
  
  // Also set primary-color and primary-hover for compatibility
  root.style.setProperty('--primary-color', colors.accentPrimary)
  root.style.setProperty('--primary-hover', adjustBrightness(colors.accentPrimary, 20))
}

// Helper function to adjust color brightness
function adjustBrightness(color: string, percent: number): string {
  const num = parseInt(color.replace('#', ''), 16)
  const amt = Math.round(2.55 * percent)
  const R = (num >> 16) + amt
  const G = (num >> 8 & 0x00FF) + amt
  const B = (num & 0x0000FF) + amt
  return '#' + (0x1000000 + (R < 255 ? R < 1 ? 0 : R : 255) * 0x10000 +
    (G < 255 ? G < 1 ? 0 : G : 255) * 0x100 +
    (B < 255 ? B < 1 ? 0 : B : 255))
    .toString(16)
    .slice(1)
}

// Initialize on first load
loadTheme()
applyTheme(currentTheme.value)

// Watch for theme changes
watch(currentTheme, (theme) => {
  applyTheme(theme)
  saveTheme()
})

watch(customThemes, () => {
  saveCustomThemes()
}, { deep: true })

export function useTheme() {
  function setTheme(themeId: string) {
    const theme = [...BUILT_IN_THEMES, ...customThemes.value].find(t => t.id === themeId)
    if (theme) {
      currentTheme.value = theme
    }
  }

  function getAllThemes(): Theme[] {
    return [...BUILT_IN_THEMES, ...customThemes.value]
  }

  function addCustomTheme(theme: Theme) {
    // Ensure it's marked as custom
    theme.isBuiltIn = false
    
    // Check for duplicate IDs
    const existing = customThemes.value.findIndex(t => t.id === theme.id)
    if (existing !== -1) {
      // Update existing theme
      customThemes.value[existing] = theme
    } else {
      // Add new theme
      customThemes.value.push(theme)
    }
  }

  function deleteCustomTheme(themeId: string) {
    const index = customThemes.value.findIndex(t => t.id === themeId)
    if (index !== -1) {
      customThemes.value.splice(index, 1)
      
      // If deleted theme was active, switch to default
      if (currentTheme.value.id === themeId) {
        setTheme(DEFAULT_THEME.id)
      }
    }
  }

  function resetToDefault() {
    setTheme(DEFAULT_THEME.id)
  }

  return {
    currentTheme,
    customThemes,
    setTheme,
    getAllThemes,
    addCustomTheme,
    deleteCustomTheme,
    resetToDefault
  }
}
