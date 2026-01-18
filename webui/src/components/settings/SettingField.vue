<script setup lang="ts">
import { computed } from 'vue'

interface Props {
  label: string
  description?: string
  modelValue: string | number
  type?: 'text' | 'number' | 'select' | 'textarea'
  options?: { value: string; label: string }[]
  placeholder?: string
  disabled?: boolean
  error?: string
}

interface Emits {
  (e: 'update:modelValue', value: string | number): void
}

const props = withDefaults(defineProps<Props>(), {
  type: 'text',
  disabled: false,
})

const emit = defineEmits<Emits>()

const currentValue = computed({
  get: () => props.modelValue,
  set: (value) => {
    if (props.type === 'number') {
      emit('update:modelValue', Number(value) || 0)
    } else {
      emit('update:modelValue', value)
    }
  }
})
</script>

<template>
  <div class="setting-field" :class="{ hasError: !!error, disabled }">
    <div v-if="label || description" class="setting-labels">
      <label v-if="label" class="label">{{ label }}</label>
      <p v-if="description" class="description">{{ description }}</p>
    </div>

    <div class="setting-input-wrapper">
      <select
        v-if="type === 'select' && options"
        :value="currentValue"
        :disabled="disabled"
        class="setting-input select"
        @change="(e: Event) => currentValue = (e.target as HTMLSelectElement).value"
      >
        <option v-for="opt in options" :key="opt.value" :value="opt.value">
          {{ opt.label }}
        </option>
      </select>

      <textarea
        v-else-if="type === 'textarea'"
        :value="currentValue"
        :placeholder="placeholder"
        :disabled="disabled"
        class="setting-input textarea"
        rows="4"
        @input="(e: Event) => currentValue = (e.target as HTMLTextAreaElement).value"
      />

      <input
        v-else
        :type="type"
        :value="currentValue"
        :placeholder="placeholder"
        :disabled="disabled"
        class="setting-input"
        @input="(e: Event) => {
          const target = e.target as HTMLInputElement
          currentValue = target.value
        }"
      />
    </div>

    <p v-if="error" class="error">{{ error }}</p>
  </div>
</template>

<style scoped>
.setting-field {
  margin-bottom: 1.5rem;
}

.setting-field.disabled {
  opacity: 0.5;
  pointer-events: none;
}

.setting-labels {
  margin-bottom: 0.5rem;
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

.setting-input-wrapper {
  position: relative;
}

.setting-input {
  width: 100%;
  padding: 0.625rem 0.75rem;
  border: 1px solid var(--border-color);
  border-radius: 6px;
  background: var(--bg-primary);
  color: var(--text-primary);
  font-size: 0.875rem;
  font-family: inherit;
  transition: border-color 0.2s, box-shadow 0.2s;
  box-sizing: border-box;
}

.setting-input:focus {
  outline: none;
  border-color: var(--accent-primary);
  box-shadow: 0 0 0 3px rgba(99, 102, 241, 0.1);
}

.setting-input:disabled {
  background: var(--bg-secondary);
  cursor: not-allowed;
}

.setting-input.select {
  cursor: pointer;
  appearance: none;
  background-image: url('data:image/svg+xml;base64,PHN2ZyB3aWR0aD0iMTIiIGhlaWdodD0iOCIgdmlld0JveD0iMCAwIDEyIDgiIGZpbGw9Im5vbmUiIHhtbG5zPSJodHRwOi8vd3d3LnczLm9yZy8yMDAwL3N2ZyI+PHBhdGggZD0iTTFMSDZsNS4wMzYgNW0wLTEwTDEgNWwtNS4wMzYgNSIgc3Ryb2tlPSI2NjYiIHN0cm9rZS13aWR0aD0iLjciLz48L3N2Zz4=');
  background-repeat: no-repeat;
  background-position: right 0.75rem center;
  background-size: 12px;
  padding-right: 2.5rem;
}

.setting-input.textarea {
  resize: vertical;
  min-height: 80px;
  line-height: 1.5;
}

.error {
  display: block;
  font-size: 0.8125rem;
  color: #ef4444;
  margin-top: 0.375rem;
}

.setting-field.hasError .setting-input {
  border-color: #ef4444;
  animation: shake 0.4s ease-in-out;
}

@keyframes shake {
  0%, 100% { transform: translateX(0); }
  25% { transform: translateX(-4px); }
  75% { transform: translateX(4px); }
}
</style>