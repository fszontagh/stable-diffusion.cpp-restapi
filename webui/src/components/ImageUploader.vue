<script setup lang="ts">
import { ref, watch } from 'vue'

const props = defineProps<{
  label?: string
  modelValue?: string
}>()

const emit = defineEmits<{
  'update:modelValue': [value: string | undefined]
}>()

const dragover = ref(false)
const preview = ref<string | undefined>(props.modelValue)

// Watch for external changes to modelValue (e.g., from assistant set image)
watch(() => props.modelValue, (newValue) => {
  if (newValue) {
    // If it's already a data URL, use it directly
    // If it's just base64, add the prefix
    preview.value = newValue.startsWith('data:') ? newValue : `data:image/png;base64,${newValue}`
  } else {
    preview.value = undefined
  }
})

function handleDragOver(e: DragEvent) {
  e.preventDefault()
  dragover.value = true
}

function handleDragLeave() {
  dragover.value = false
}

function handleDrop(e: DragEvent) {
  e.preventDefault()
  dragover.value = false
  const file = e.dataTransfer?.files[0]
  if (file) processFile(file)
}

function handleFileSelect(e: Event) {
  const input = e.target as HTMLInputElement
  const file = input.files?.[0]
  if (file) processFile(file)
}

function processFile(file: File) {
  if (!file.type.startsWith('image/')) {
    return
  }
  const reader = new FileReader()
  reader.onload = () => {
    const result = reader.result as string
    preview.value = result
    // Extract base64 data without the prefix
    const base64 = result.split(',')[1]
    emit('update:modelValue', base64)
  }
  reader.readAsDataURL(file)
}

function clearImage() {
  preview.value = undefined
  emit('update:modelValue', undefined)
}
</script>

<template>
  <div class="image-uploader">
    <label v-if="label" class="form-label">{{ label }}</label>
    <div
      v-if="!preview"
      :class="['upload-zone', { dragover }]"
      @dragover="handleDragOver"
      @dragleave="handleDragLeave"
      @drop="handleDrop"
    >
      <input
        type="file"
        accept="image/*"
        class="file-input"
        @change="handleFileSelect"
      />
      <div class="upload-content">
        <span class="upload-icon">&#128247;</span>
        <span class="upload-text">Drop image here or click to upload</span>
      </div>
    </div>
    <div v-else class="image-preview">
      <img :src="preview" alt="Preview" />
      <div class="image-preview-actions">
        <button class="btn btn-sm btn-danger" @click="clearImage">
          &#10005;
        </button>
      </div>
    </div>
  </div>
</template>

<style scoped>
.image-uploader {
  margin-bottom: 16px;
}

.upload-zone {
  position: relative;
  border: 2px dashed var(--border-color);
  border-radius: var(--border-radius);
  padding: 40px 20px;
  text-align: center;
  cursor: pointer;
  transition: all var(--transition-fast);
}

.upload-zone:hover,
.upload-zone.dragover {
  border-color: var(--accent-primary);
  background: rgba(0, 217, 255, 0.05);
}

.file-input {
  position: absolute;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
  opacity: 0;
  cursor: pointer;
}

.upload-content {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 12px;
  pointer-events: none;
}

.upload-icon {
  font-size: 36px;
  opacity: 0.5;
}

.upload-text {
  color: var(--text-secondary);
  font-size: 14px;
}

.image-preview {
  position: relative;
  border-radius: var(--border-radius);
  overflow: hidden;
  background: var(--bg-tertiary);
}

.image-preview img {
  width: 100%;
  max-height: 300px;
  object-fit: contain;
  display: block;
}

.image-preview-actions {
  position: absolute;
  top: 8px;
  right: 8px;
}
</style>
