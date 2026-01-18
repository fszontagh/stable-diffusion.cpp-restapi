/**
 * Input validation utilities for security hardening
 */

export const validation = {
  /**
   * Validate and sanitize numeric input
   */
  number(value: unknown, options: { min?: number; max?: number; integer?: boolean } = {}): number {
    const num = Number(value)
    
    if (isNaN(num) || !isFinite(num)) {
      throw new Error('Invalid number')
    }

    if (options.integer && !Number.isInteger(num)) {
      throw new Error('Must be an integer')
    }

    if (options.min !== undefined && num < options.min) {
      throw new Error(`Must be at least ${options.min}`)
    }

    if (options.max !== undefined && num > options.max) {
      throw new Error(`Must be at most ${options.max}`)
    }

    return num
  },

  /**
   * Validate and sanitize string input
   */
  string(value: unknown, options: { minLength?: number; maxLength?: number; pattern?: RegExp } = {}): string {
    if (typeof value !== 'string') {
      throw new Error('Must be a string')
    }

    const trimmed = value.trim()

    if (options.minLength !== undefined && trimmed.length < options.minLength) {
      throw new Error(`Must be at least ${options.minLength} characters`)
    }

    if (options.maxLength !== undefined && trimmed.length > options.maxLength) {
      throw new Error(`Must be at most ${options.maxLength} characters`)
    }

    if (options.pattern && !options.pattern.test(trimmed)) {
      throw new Error('Invalid format')
    }

    return trimmed
  },

  /**
   * Validate boolean input
   */
  boolean(value: unknown): boolean {
    if (typeof value === 'boolean') {
      return value
    }
    if (value === 'true' || value === '1' || value === 1) {
      return true
    }
    if (value === 'false' || value === '0' || value === 0) {
      return false
    }
    throw new Error('Must be a boolean value')
  },

  /**
   * Validate array input
   */
  array<T>(value: unknown, itemValidator?: (item: unknown) => T): T[] {
    if (!Array.isArray(value)) {
      throw new Error('Must be an array')
    }

    if (itemValidator) {
      return value.map((item, index) => {
        try {
          return itemValidator(item)
        } catch (error) {
          throw new Error(`Invalid item at index ${index}: ${error instanceof Error ? error.message : 'unknown error'}`)
        }
      })
    }

    return value as T[]
  },

  /**
   * Validate file path (prevent path traversal)
   */
  filePath(value: string): string {
    const cleaned = this.string(value)

    // Prevent path traversal
    if (cleaned.includes('..') || cleaned.includes('//')) {
      throw new Error('Invalid file path')
    }

    // Prevent absolute paths
    if (cleaned.startsWith('/') || /^[A-Za-z]:/.test(cleaned)) {
      throw new Error('Absolute paths not allowed')
    }

    return cleaned
  },

  /**
   * Validate URL
   */
  url(value: string, options: { allowedProtocols?: string[] } = {}): string {
    const cleaned = this.string(value)

    try {
      const url = new URL(cleaned)
      
      if (options.allowedProtocols && !options.allowedProtocols.includes(url.protocol.slice(0, -1))) {
        throw new Error(`Protocol must be one of: ${options.allowedProtocols.join(', ')}`)
      }

      return cleaned
    } catch {
      throw new Error('Invalid URL')
    }
  },

  /**
   * Validate hex color
   */
  hexColor(value: string): string {
    const cleaned = this.string(value)

    if (!/^#[0-9A-Fa-f]{6}$/.test(cleaned)) {
      throw new Error('Invalid hex color (must be #RRGGBB)')
    }

    return cleaned.toLowerCase()
  },

  /**
   * Sanitize HTML to prevent XSS (basic sanitization)
   * Note: For production, use DOMPurify
   */
  sanitizeHtml(value: string): string {
    const cleaned = this.string(value)
    
    // Remove script tags and event handlers
    return cleaned
      .replace(/<script\b[^<]*(?:(?!<\/script>)<[^<]*)*<\/script>/gi, '')
      .replace(/on\w+\s*=\s*["'][^"']*["']/gi, '')
      .replace(/javascript:/gi, '')
  },

  /**
   * Validate enum value
   */
  enum<T extends string>(value: unknown, allowedValues: readonly T[]): T {
    const str = this.string(value)
    
    if (!allowedValues.includes(str as T)) {
      throw new Error(`Must be one of: ${allowedValues.join(', ')}`)
    }

    return str as T
  },

  /**
   * Validate object shape
   */
  object<T extends Record<string, unknown>>(
    value: unknown,
    shape: { [K in keyof T]: (v: unknown) => T[K] }
  ): T {
    if (!value || typeof value !== 'object' || Array.isArray(value)) {
      throw new Error('Must be an object')
    }

    const result = {} as T
    const obj = value as Record<string, unknown>

    for (const [key, validator] of Object.entries(shape)) {
      try {
        result[key as keyof T] = validator(obj[key])
      } catch (error) {
        throw new Error(`Invalid field '${key}': ${error instanceof Error ? error.message : 'unknown error'}`)
      }
    }

    return result
  }
}

/**
 * Create a safe validator that returns a default value on error
 */
export function safeValidate<T>(
  validator: () => T,
  defaultValue: T
): T {
  try {
    return validator()
  } catch {
    return defaultValue
  }
}
