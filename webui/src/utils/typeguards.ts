/**
 * Type guard utilities for runtime type checking
 * These help prevent errors from invalid API responses or data
 */

import type { Job, ModelInfo, HealthResponse } from '../api/client'

/**
 * Check if value is a valid Job object
 */
export function isJob(value: unknown): value is Job {
  return (
    typeof value === 'object' &&
    value !== null &&
    'job_id' in value &&
    'type' in value &&
    'status' in value &&
    'created_at' in value &&
    typeof (value as Job).job_id === 'string' &&
    typeof (value as Job).type === 'string' &&
    typeof (value as Job).status === 'string'
  )
}

/**
 * Check if value is a valid queue status
 */
export function isValidQueueStatus(value: unknown): value is 'pending' | 'processing' | 'completed' | 'failed' | 'cancelled' {
  return (
    typeof value === 'string' &&
    ['pending', 'processing', 'completed', 'failed', 'cancelled'].includes(value)
  )
}

/**
 * Check if value is a valid ModelInfo object
 */
export function isModelInfo(value: unknown): value is ModelInfo {
  return (
    typeof value === 'object' &&
    value !== null &&
    'name' in value &&
    'type' in value &&
    'file_extension' in value &&
    typeof (value as ModelInfo).name === 'string' &&
    typeof (value as ModelInfo).type === 'string' &&
    typeof (value as ModelInfo).file_extension === 'string'
  )
}

/**
 * Check if value is a valid HealthResponse object
 */
export function isHealthResponse(value: unknown): value is HealthResponse {
  return (
    typeof value === 'object' &&
    value !== null &&
    'status' in value &&
    'model_loaded' in value &&
    typeof (value as HealthResponse).status === 'string' &&
    typeof (value as HealthResponse).model_loaded === 'boolean'
  )
}

/**
 * Check if value is a string
 */
export function isString(value: unknown): value is string {
  return typeof value === 'string'
}

/**
 * Check if value is a number
 */
export function isNumber(value: unknown): value is number {
  return typeof value === 'number' && !isNaN(value)
}

/**
 * Check if value is a boolean
 */
export function isBoolean(value: unknown): value is boolean {
  return typeof value === 'boolean'
}

/**
 * Check if value is a non-null object
 */
export function isObject(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value)
}

/**
 * Check if value is an array
 */
export function isArray<T = unknown>(value: unknown): value is T[] {
  return Array.isArray(value)
}

/**
 * Check if value is a valid error object
 */
export function isError(value: unknown): value is Error {
  return value instanceof Error
}

/**
 * Safely parse JSON with type guard
 */
export function safeJSONParse<T>(json: string, guard: (value: unknown) => value is T): T | null {
  try {
    const parsed = JSON.parse(json)
    return guard(parsed) ? parsed : null
  } catch {
    return null
  }
}

/**
 * Assert that a value matches a type guard, throw if not
 */
export function assertType<T>(value: unknown, guard: (value: unknown) => value is T, message?: string): asserts value is T {
  if (!guard(value)) {
    throw new TypeError(message || 'Type assertion failed')
  }
}

/**
 * Get value with type guard, return default if guard fails
 */
export function withDefault<T>(value: unknown, guard: (value: unknown) => value is T, defaultValue: T): T {
  return guard(value) ? value : defaultValue
}
