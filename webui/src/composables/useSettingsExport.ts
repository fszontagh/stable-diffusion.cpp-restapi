import { useTheme } from './useTheme'

interface ExportData {
  version: string
  exportDate: string
  theme: {
    currentThemeId: string
    customThemes: unknown[]
  }
  // Add more settings sections as needed
}

export function useSettingsExport() {
  const { currentTheme, customThemes } = useTheme()

  function exportSettings(): string {
    const data: ExportData = {
      version: '1.0.0',
      exportDate: new Date().toISOString(),
      theme: {
        currentThemeId: currentTheme.value.id,
        customThemes: customThemes.value
      }
    }

    return JSON.stringify(data, null, 2)
  }

  function downloadSettings() {
    const json = exportSettings()
    const blob = new Blob([json], { type: 'application/json' })
    const url = URL.createObjectURL(blob)
    const link = document.createElement('a')
    link.href = url
    link.download = `sdcpp-settings-${Date.now()}.json`
    document.body.appendChild(link)
    link.click()
    document.body.removeChild(link)
    URL.revokeObjectURL(url)
  }

  async function importSettings(file: File): Promise<void> {
    return new Promise((resolve, reject) => {
      const reader = new FileReader()

      reader.onload = async (e) => {
        try {
          const json = e.target?.result as string
          const data: ExportData = JSON.parse(json)

          // Validate version
          if (!data.version) {
            throw new Error('Invalid settings file: missing version')
          }

          // Import theme settings
          if (data.theme) {
            const { setTheme, addCustomTheme } = useTheme()

            // Import custom themes
            if (data.theme.customThemes && Array.isArray(data.theme.customThemes)) {
              for (const theme of data.theme.customThemes) {
                addCustomTheme(theme as never)
              }
            }

            // Set current theme
            if (data.theme.currentThemeId) {
              setTheme(data.theme.currentThemeId)
            }
          }

          resolve()
        } catch (error) {
          reject(error)
        }
      }

      reader.onerror = () => {
        reject(new Error('Failed to read file'))
      }

      reader.readAsText(file)
    })
  }

  return {
    exportSettings,
    downloadSettings,
    importSettings
  }
}
