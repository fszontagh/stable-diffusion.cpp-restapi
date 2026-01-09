<script setup lang="ts">
// Simple prompt textarea component (syntax highlighting removed)
defineProps<{
  modelValue: string
  placeholder?: string
  rows?: number
}>()

const emit = defineEmits<{
  'update:modelValue': [value: string]
}>()

function handleInput(event: Event) {
  const target = event.target as HTMLTextAreaElement
  emit('update:modelValue', target.value)
}
</script>

<template>
  <textarea
    :value="modelValue"
    :placeholder="placeholder"
    :rows="rows || 3"
    class="form-textarea"
    @input="handleInput"
  ></textarea>
</template>

<style scoped>
.form-textarea {
  resize: vertical;
}
</style>
