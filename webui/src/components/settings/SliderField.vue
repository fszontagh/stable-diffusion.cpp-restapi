<script setup lang="ts">
import { ref, watch } from 'vue'

interface Props {
  label: string
  description?: string
  modelValue: number
  min: number
  max: number
  step?: number
  unit?: string
  disabled?: boolean
  format?: (value: number) => string
  precision?: number
}

interface Emits {
  (e: 'update:modelValue', value: number): void
}

const props = withDefaults(defineProps<Props>(), {
  step: 1,
  disabled: false,
  format: undefined,
  precision: 2,
})

const emit = defineEmits<Emits>()

const internalValue = ref(props.modelValue)
let updateTimeout: ReturnType<typeof setTimeout> | null = null

watch(() => props.modelValue, (newValue) => {
  internalValue.value = newValue
})

function handleInput(e: Event) {
  const target = e.target as HTMLInputElement
  internalValue.value = parseFloat(target.value) || props.min
  
  if (updateTimeout) clearTimeout(updateTimeout)
  updateTimeout = setTimeout(() => {
    emitUpdate()
  }, 100)
}

function handleChange(e: Event) {
  const target = e.target as HTMLInputElement
  const value = parseFloat(target.value) || props.min
  internalValue.value = value
  emit('update:modelValue', value)
}

function emitUpdate() {
  const clampedValue = Math.max(props.min, Math.min(props.max, internalValue.value))
  if (clampedValue !== props.modelValue) {
    emit('update:modelValue', clampedValue)
    internalValue.value = clampedValue
  }
}

function formatValue(value: number): string {
  if (props.format) {
    return props.format(value)
  }
  const formatted = value.toFixed(props.precision)
  if (props.unit) {
    return `${formatted} ${props.unit}`
  }
  return formatted
}

function decrement() {
  const newValue = Math.max(props.min, internalValue.value - props.step)
  internalValue.value = newValue
  emit('update:modelValue', newValue)
}

function increment() {
  const newValue = Math.min(props.max, internalValue.value + props.step)
  internalValue.value = newValue
  emit('update:modelValue', newValue)
}
</script>

<template>
  <div class="slider-field" :class="{ disabled }">
    <div class="slider-header">
      <div class="slider-labels">
        <label v-if="label" class="label">{{ label }}</label>
        <p v-if="description" class="description">{{ description }}</p>
      </div>
      <div class="slider-controls">
        <button class="control-btn" @click="decrement" :disabled="disabled">−</button>
        <span class="value-display">{{ formatValue(internalValue) }}</span>
        <button class="control-btn" @click="increment" :disabled="disabled">+</button>
      </div>
    </div>

    <div class="slider-input-wrapper">
      <input
        type="range"
        :min="min"
        :max="max"
        :step="step"
        :value="internalValue"
        :disabled="disabled"
        class="slider-input"
        @input="handleInput"
        @change="handleChange"
      />
      <div class="slider-fill" :style="{ left: '0%', width: `${((internalValue - min) / (max - min)) * 100}%` }"></div>
    </div>
  </div>
</template>

<style scoped>
.slider-field {
  margin-bottom: 1.5rem;
}

.slider-field.disabled {
  opacity: 0.5;
  pointer-events: none;
}

.slider-header {
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
  margin-bottom: 0.75rem;
  gap: 1rem;
}

.slider-labels {
  flex: 1;
}

.label {
  display: block;
  font-size: 0.875rem;
  font-weight: 500;
  color: var(--text-primary);
  margin-bottom: 0.25rem;
}

.description {
  display: block;
  font-size: 0.8125rem;
  color: var(--text-secondary);
  margin: 0;
  line-height: 1.4;
}

.slider-controls {
  display: flex;
  align-items: center;
  gap: 0.5rem;
}

.control-btn {
  width: 28px;
  height: 28px;
  padding: 0;
  border: 1px solid var(--border-color);
  border-radius: 4px;
  background: var(--bg-secondary);
  color: var(--text-primary);
  font-size: 1.125rem;
  font-weight: 500;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  transition: all 0.2s;
}

.control-btn:hover:not(:disabled) {
  background: var(--bg-hover);
  border-color: var(--accent-primary);
}

.control-btn:disabled {
  opacity: 0.4;
  cursor: not-allowed;
}

.value-display {
  min-width: 80px;
  text-align: right;
  font-size: 0.875rem;
  font-weight: 500;
  color: var(--text-primary);
  font-variant-numeric: tabular-nums;
}

.slider-input-wrapper {
  position: relative;
  height: 8px;
}

.slider-input {
  appearance: none;
  width: 100%;
  height: 8px;
  background: var(--bg-secondary);
  border-radius: 4px;
  cursor: pointer;
  padding: 0;
}

.slider-input:hover {
  background: var(--bg-tertiary);
}

.slider-input:focus {
  outline: none;
}

.slider-input::-webkit-slider-thumb {
  appearance: none;
  width: 16px;
  height: 16px;
  background: var(--accent-primary);
  border-radius: 50%;
  cursor: pointer;
  transition: transform 0.2s, box-shadow 0.2s;
  box-shadow: 0 2px 4px rgba(0, 0, 0, 0.2);
}

.slider-input::-webkit-slider-thumb:hover {
  transform: scale(1.2);
  box-shadow: 0 4px 8px rgba(0, 0, 0, 0.3);
}

.slider-input::-moz-range-thumb {
  width: 16px;
  height: 16px;
  background: var(--accent-primary);
  border: none;
  border-radius: 50%;
  cursor: pointer;
  transition: transform 0.2s, box-shadow 0.2s;
  box-shadow: 0 2px 4px rgba(0, 0, 0, 0.2);
}

.slider-input::-moz-range-thumb:hover {
  transform: scale(1.2);
  box-shadow: 0 4px 8px rgba(0, 0, 0, 0.3);
}

.slider-fill {
  position: absolute;
  top: 0;
  left: 0;
  height: 8px;
  background: var(--accent-primary);
  border-radius: 4px;
  opacity: 0.3;
  pointer-events: none;
}

@media (max-width: 640px) {
  .slider-header {
    flex-direction: column;
    align-items: stretch;
  }

  .slider-controls {
    justify-content: space-between;
    margin-top: 0.5rem;
  }

  .value-display {
    text-align: center;
  }
}
</style>