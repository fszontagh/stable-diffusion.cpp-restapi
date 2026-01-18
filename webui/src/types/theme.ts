export interface ThemeColors {
  // Background colors
  bgPrimary: string
  bgSecondary: string
  bgTertiary: string
  bgInput: string
  bgHover: string

  // Text colors
  textPrimary: string
  textSecondary: string
  textMuted: string

  // Accent colors
  accentPrimary: string
  accentSuccess: string
  accentWarning: string
  accentError: string
  accentPurple: string

  // Border and structure
  borderColor: string
}

export interface Theme {
  id: string
  name: string
  description?: string
  colors: ThemeColors
  isBuiltIn: boolean
  isDark?: boolean  // For auto-detection pairing
}

// SD Dark - Professional dark theme inspired by Stable Diffusion WebUI (New Default)
export const SD_DARK_THEME: Theme = {
  id: 'sd-dark',
  name: 'SD Dark',
  description: 'Professional dark theme inspired by Stable Diffusion WebUI',
  isBuiltIn: true,
  isDark: true,
  colors: {
    bgPrimary: '#0b0f19',
    bgSecondary: '#111827',
    bgTertiary: '#1f2937',
    bgInput: '#1a2332',
    bgHover: '#374151',
    textPrimary: '#e5e7eb',
    textSecondary: '#9ca3af',
    textMuted: '#6b7280',
    accentPrimary: '#f97316',
    accentSuccess: '#22c55e',
    accentWarning: '#eab308',
    accentError: '#ef4444',
    accentPurple: '#a855f7',
    borderColor: '#374151'
  }
}

// SD Light - Clean light theme matching SD Dark
export const SD_LIGHT_THEME: Theme = {
  id: 'sd-light',
  name: 'SD Light',
  description: 'Clean light theme matching SD Dark',
  isBuiltIn: true,
  isDark: false,
  colors: {
    bgPrimary: '#f9fafb',
    bgSecondary: '#ffffff',
    bgTertiary: '#f3f4f6',
    bgInput: '#ffffff',
    bgHover: '#e5e7eb',
    textPrimary: '#111827',
    textSecondary: '#4b5563',
    textMuted: '#9ca3af',
    accentPrimary: '#ea580c',
    accentSuccess: '#16a34a',
    accentWarning: '#ca8a04',
    accentError: '#dc2626',
    accentPurple: '#9333ea',
    borderColor: '#e5e7eb'
  }
}

// Classic Dark - The original SD.cpp WebUI theme
export const CLASSIC_DARK_THEME: Theme = {
  id: 'classic-dark',
  name: 'Classic Dark',
  description: 'The classic SD.cpp WebUI theme',
  isBuiltIn: true,
  isDark: true,
  colors: {
    bgPrimary: '#1a1a2e',
    bgSecondary: '#16213e',
    bgTertiary: '#0f3460',
    bgInput: '#1e2a4a',
    bgHover: '#253a5e',
    textPrimary: '#eee',
    textSecondary: '#aaa',
    textMuted: '#666',
    accentPrimary: '#00d9ff',
    accentSuccess: '#98c379',
    accentWarning: '#ffd700',
    accentError: '#ff6b9d',
    accentPurple: '#c792ea',
    borderColor: '#2a3f5f'
  }
}

// Keep DEFAULT_THEME for backward compatibility
export const DEFAULT_THEME = SD_DARK_THEME

export const BUILT_IN_THEMES: Theme[] = [
  SD_DARK_THEME,
  SD_LIGHT_THEME,
  CLASSIC_DARK_THEME,
  {
    id: 'deep-blue',
    name: 'Deep Blue',
    description: 'Ocean depths theme with vibrant blues',
    isBuiltIn: true,
    isDark: true,
    colors: {
      bgPrimary: '#0a1628',
      bgSecondary: '#0d1b2a',
      bgTertiary: '#1b263b',
      bgInput: '#1b2a41',
      bgHover: '#2c3e5a',
      textPrimary: '#e0e1dd',
      textSecondary: '#9db4c0',
      textMuted: '#5c6c7a',
      accentPrimary: '#00b4d8',
      accentSuccess: '#06ffa5',
      accentWarning: '#ffd60a',
      accentError: '#ef476f',
      accentPurple: '#7209b7',
      borderColor: '#2d4059'
    }
  },
  {
    id: 'dark-purple',
    name: 'Dark Purple',
    description: 'Rich purple theme with warm accents',
    isBuiltIn: true,
    isDark: true,
    colors: {
      bgPrimary: '#1a1423',
      bgSecondary: '#2d1b3d',
      bgTertiary: '#3e2a47',
      bgInput: '#2a1d35',
      bgHover: '#4a3556',
      textPrimary: '#f0e7ff',
      textSecondary: '#c4b0d6',
      textMuted: '#7d6b8f',
      accentPrimary: '#b565d8',
      accentSuccess: '#52de97',
      accentWarning: '#f4c430',
      accentError: '#ff6b9d',
      accentPurple: '#9d4edd',
      borderColor: '#4a3556'
    }
  },
  {
    id: 'forest-green',
    name: 'Forest Green',
    description: 'Natural green theme inspired by forests',
    isBuiltIn: true,
    isDark: true,
    colors: {
      bgPrimary: '#0d1b0e',
      bgSecondary: '#1a2a1e',
      bgTertiary: '#2d4a33',
      bgInput: '#1e3022',
      bgHover: '#345c3e',
      textPrimary: '#e8f5e9',
      textSecondary: '#aed581',
      textMuted: '#66bb6a',
      accentPrimary: '#4caf50',
      accentSuccess: '#81c784',
      accentWarning: '#ffb74d',
      accentError: '#e57373',
      accentPurple: '#ba68c8',
      borderColor: '#2e5c36'
    }
  },
  {
    id: 'midnight-slate',
    name: 'Midnight Slate',
    description: 'Cool slate gray theme with subtle blue tints',
    isBuiltIn: true,
    isDark: true,
    colors: {
      bgPrimary: '#0f1419',
      bgSecondary: '#1a1f26',
      bgTertiary: '#282e38',
      bgInput: '#1e242c',
      bgHover: '#343b47',
      textPrimary: '#d9e2ec',
      textSecondary: '#9fb3c8',
      textMuted: '#627d98',
      accentPrimary: '#3b9eff',
      accentSuccess: '#3ebd93',
      accentWarning: '#f7b731',
      accentError: '#fc5c65',
      accentPurple: '#a55eea',
      borderColor: '#3d4854'
    }
  },
  {
    id: 'sunset-orange',
    name: 'Sunset Orange',
    description: 'Warm sunset theme with orange and coral tones',
    isBuiltIn: true,
    isDark: true,
    colors: {
      bgPrimary: '#1a0f0a',
      bgSecondary: '#2a1810',
      bgTertiary: '#3d2418',
      bgInput: '#2d1c12',
      bgHover: '#4a3025',
      textPrimary: '#fff5eb',
      textSecondary: '#f4d1ae',
      textMuted: '#c69774',
      accentPrimary: '#ff8c42',
      accentSuccess: '#86c232',
      accentWarning: '#ffc857',
      accentError: '#ff6b6b',
      accentPurple: '#e76f51',
      borderColor: '#5a3825'
    }
  },
  {
    id: 'cyberpunk',
    name: 'Cyberpunk',
    description: 'Neon-infused cyberpunk aesthetic',
    isBuiltIn: true,
    isDark: true,
    colors: {
      bgPrimary: '#0a0e27',
      bgSecondary: '#1a1d3a',
      bgTertiary: '#252850',
      bgInput: '#1c1f3d',
      bgHover: '#2e3354',
      textPrimary: '#e3f2fd',
      textSecondary: '#00e5ff',
      textMuted: '#7c7f99',
      accentPrimary: '#ff006e',
      accentSuccess: '#00ff41',
      accentWarning: '#ffbe0b',
      accentError: '#ff006e',
      accentPurple: '#8338ec',
      borderColor: '#3d4265'
    }
  }
]
