<script setup lang="ts">
import { computed } from 'vue'

const props = defineProps<{
  name: string
  weight: number
  isValid: boolean
  minWeight: number
  maxWeight: number
}>()

const emit = defineEmits<{
  'update:weight': [value: number]
  'remove': []
}>()

const displayWeight = computed({
  get: () => props.weight,
  set: (val: number) => {
    const clamped = Math.max(props.minWeight, Math.min(props.maxWeight, val))
    emit('update:weight', Math.round(clamped * 10) / 10)
  }
})

function handleSliderInput(event: Event) {
  const target = event.target as HTMLInputElement
  displayWeight.value = parseFloat(target.value)
}

function handleNumberInput(event: Event) {
  const target = event.target as HTMLInputElement
  const val = parseFloat(target.value)
  if (!isNaN(val)) {
    displayWeight.value = val
  }
}
</script>

<template>
  <div class="lora-item" :class="{ 'lora-invalid': !isValid }">
    <div class="lora-info">
      <span class="lora-name" :title="name">{{ name }}</span>
      <span v-if="!isValid" class="lora-warning" title="LoRA not found">
        &#9888;
      </span>
    </div>
    <div class="lora-controls">
      <input
        type="range"
        class="lora-slider"
        :min="minWeight"
        :max="maxWeight"
        step="0.1"
        :value="weight"
        @input="handleSliderInput"
      />
      <input
        type="number"
        class="lora-weight-input"
        :min="minWeight"
        :max="maxWeight"
        step="0.1"
        :value="weight.toFixed(1)"
        @change="handleNumberInput"
      />
      <button class="lora-remove" @click="emit('remove')" title="Remove LoRA">
        &times;
      </button>
    </div>
  </div>
</template>

<style scoped>
.lora-item {
  display: flex;
  flex-direction: column;
  gap: 0.25rem;
  padding: 0.5rem;
  background: var(--bg-secondary, #2a2a2a);
  border-radius: 4px;
  border: 1px solid var(--border-color, #444);
}

.lora-item.lora-invalid {
  border-color: var(--warning-color, #f0ad4e);
  background: rgba(240, 173, 78, 0.1);
}

.lora-info {
  display: flex;
  align-items: center;
  gap: 0.5rem;
}

.lora-name {
  flex: 1;
  font-size: 0.875rem;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.lora-warning {
  color: var(--warning-color, #f0ad4e);
  font-size: 1rem;
}

.lora-controls {
  display: flex;
  align-items: center;
  gap: 0.5rem;
}

.lora-slider {
  flex: 1;
  height: 4px;
  cursor: pointer;
}

.lora-weight-input {
  width: 60px;
  padding: 0.25rem;
  font-size: 0.75rem;
  text-align: center;
  background: var(--bg-primary, #1a1a1a);
  border: 1px solid var(--border-color, #444);
  border-radius: 4px;
  color: var(--text-primary, #fff);
}

.lora-weight-input:focus {
  outline: none;
  border-color: var(--primary-color, #007bff);
}

.lora-remove {
  background: none;
  border: none;
  color: var(--text-secondary, #888);
  font-size: 1.25rem;
  cursor: pointer;
  padding: 0 0.25rem;
  line-height: 1;
}

.lora-remove:hover {
  color: var(--error-color, #dc3545);
}
</style>
