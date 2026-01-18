<script setup lang="ts">
import { computed } from 'vue'

interface Props {
  label: string
  description?: string
  modelValue: boolean
  disabled?: boolean
}

interface Emits {
  (e: 'update:modelValue', value: boolean): void
}

const props = withDefaults(defineProps<Props>(), {
  disabled: false,
})

const emit = defineEmits<Emits>()

const checked = computed({
  get: () => props.modelValue,
  set: (value: boolean) => {
    emit('update:modelValue', value)
  }
})
</script>

<template>
  <div class="switch-field" :class="{ checked, disabled }" :role="disabled ? undefined : 'switch'" :tabindex="disabled ? -1 : 0" @keyup.enter.exact="!disabled && (checked = !checked)">
    <div class="switch-content">
      <div v-if="label || description" class="switch-labels">
        <label v-if="label" class="label">{{ label }}</label>
        <p v-if="description" class="description">{{ description }}</p>
      </div>
    </div>

    <button
      type="button"
      class="switch-button"
      :class="{ active: checked }"
      :disabled="disabled"
      @click="checked = !checked"
    >
      <span class="switch-thumb"></span>
    </button>
  </div>
</template>

<style scoped>
.switch-field {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  gap: 1rem;
  padding: 0.75rem 0;
  cursor: pointer;
}

.switch-field.disabled {
  opacity: 0.5;
  pointer-events: none;
}

.switch-content {
  flex: 1;
}

.switch-labels {
  margin-bottom: 0;
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

.switch-button {
  position: relative;
  width: 48px;
  height: 26px;
  padding: 0;
  border: 2px solid var(--border-color);
  border-radius: 13px;
  background: var(--bg-secondary);
  cursor: pointer;
  transition: all 0.3s ease;
  flex-shrink: 0;
  outline: none;
}

.switch-button:hover:not(:disabled) {
  border-color: var(--accent-primary);
}

.switch-button:disabled {
  cursor: not-allowed;
  opacity: 0.5;
}

.switch-button.active {
  background: var(--accent-primary);
  border-color: var(--accent-primary);
}

.switch-thumb {
  position: absolute;
  top: 2px;
  left: 2px;
  width: 20px;
  height: 20px;
  background: white;
  border-radius: 50%;
  transition: transform 0.3s ease;
  box-shadow: 0 2px 4px rgba(0, 0, 0, 0.2);
}

.switch-button.active .switch-thumb {
  transform: translateX(20px);
}

@media (max-width: 640px) {
  .switch-button {
    width: 44px;
    height: 24px;
  }

  .switch-thumb {
    width: 18px;
    height: 18px;
  }

  .switch-button.active .switch-thumb {
    transform: translateX(18px);
  }
}
</style>