<script setup lang="ts">
import { ref, computed, watch } from 'vue'
import { useAppStore } from '../stores/app'

const store = useAppStore()

// State
const isMinimized = ref(false)
const isDragging = ref(false)

// Position state for dragging
const position = ref({ x: 20, y: 20 })
const dragStart = ref({ x: 0, y: 0 })

// Computed
const hasPreview = computed(() => {
  return store.currentPreview && store.currentPreview.image
})

const processingJob = computed(() => {
  if (!store.queue?.items) return null
  return store.queue.items.find(job => job.status === 'processing')
})

const progress = computed(() => {
  if (!processingJob.value) return 0
  const job = processingJob.value
  if (job.progress.total_steps <= 0) return 0
  return Math.round((job.progress.step / job.progress.total_steps) * 100)
})

const isVisible = computed(() => {
  return hasPreview.value && processingJob.value
})

// Auto-show when preview becomes available
watch(hasPreview, (has) => {
  if (has) {
    isMinimized.value = false
  }
})

function toggleMinimize() {
  isMinimized.value = !isMinimized.value
}

function close() {
  isMinimized.value = true
}

// Drag handling
function startDrag(e: MouseEvent | TouchEvent) {
  isDragging.value = true
  const clientX = 'touches' in e ? e.touches[0].clientX : e.clientX
  const clientY = 'touches' in e ? e.touches[0].clientY : e.clientY
  dragStart.value = {
    x: clientX - position.value.x,
    y: clientY - position.value.y
  }
  e.preventDefault()
}

function onDrag(e: MouseEvent | TouchEvent) {
  if (!isDragging.value) return
  const clientX = 'touches' in e ? e.touches[0].clientX : e.clientX
  const clientY = 'touches' in e ? e.touches[0].clientY : e.clientY
  position.value = {
    x: Math.max(0, Math.min(window.innerWidth - 200, clientX - dragStart.value.x)),
    y: Math.max(0, Math.min(window.innerHeight - 200, clientY - dragStart.value.y))
  }
}

function endDrag() {
  isDragging.value = false
}

// Add global listeners for drag
if (typeof window !== 'undefined') {
  window.addEventListener('mousemove', onDrag)
  window.addEventListener('mouseup', endDrag)
  window.addEventListener('touchmove', onDrag)
  window.addEventListener('touchend', endDrag)
}
</script>

<template>
  <Transition name="preview-fade">
    <div
      v-if="isVisible"
      class="floating-preview"
      :class="{ minimized: isMinimized, dragging: isDragging }"
      :style="{ right: position.x + 'px', bottom: position.y + 'px' }"
    >
      <!-- Minimized state -->
      <button v-if="isMinimized" class="preview-minimized" @click="toggleMinimize">
        <span class="preview-icon">&#128444;</span>
        <span class="preview-badge" v-if="progress > 0">{{ progress }}%</span>
      </button>

      <!-- Expanded state -->
      <div v-else class="preview-expanded">
        <div
          class="preview-header"
          @mousedown="startDrag"
          @touchstart="startDrag"
        >
          <span class="preview-title">
            <span class="preview-icon">&#128444;</span>
            Preview
          </span>
          <div class="preview-controls">
            <button class="control-btn" @click="toggleMinimize" title="Minimize">
              &#8722;
            </button>
            <button class="control-btn" @click="close" title="Close">
              &times;
            </button>
          </div>
        </div>

        <div class="preview-content">
          <div class="preview-image-container">
            <img
              v-if="store.currentPreview?.image"
              :src="store.currentPreview.image"
              alt="Generation preview"
              class="preview-image"
            />
            <div v-else class="preview-placeholder">
              <span class="spinner"></span>
            </div>
          </div>

          <div class="preview-info">
            <div class="preview-progress">
              <div class="progress-bar-bg">
                <div class="progress-bar-fill" :style="{ width: progress + '%' }"></div>
              </div>
              <span class="progress-text">
                {{ processingJob?.progress.step || 0 }}/{{ processingJob?.progress.total_steps || 0 }}
              </span>
            </div>
          </div>
        </div>
      </div>
    </div>
  </Transition>
</template>

<style scoped>
.floating-preview {
  position: fixed;
  z-index: 900;
  transition: all 0.2s ease;
}

.floating-preview.dragging {
  transition: none;
}

.preview-minimized {
  position: relative;
  width: 56px;
  height: 56px;
  border-radius: 50%;
  background: linear-gradient(135deg, var(--accent-primary), var(--accent-purple));
  border: none;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  box-shadow: 0 4px 16px rgba(0, 0, 0, 0.4);
  transition: transform 0.2s, box-shadow 0.2s;
}

.preview-minimized:hover {
  transform: scale(1.1);
  box-shadow: 0 6px 24px rgba(0, 0, 0, 0.5);
}

.preview-minimized .preview-icon {
  font-size: 24px;
}

.preview-badge {
  position: absolute;
  top: -4px;
  right: -4px;
  min-width: 24px;
  height: 24px;
  padding: 0 6px;
  background: var(--accent-success);
  color: var(--bg-primary);
  font-size: 11px;
  font-weight: 600;
  border-radius: 12px;
  display: flex;
  align-items: center;
  justify-content: center;
}

.preview-expanded {
  width: 280px;
  background: var(--bg-secondary);
  border: 1px solid var(--border-color);
  border-radius: var(--border-radius-lg);
  box-shadow: 0 8px 32px rgba(0, 0, 0, 0.4);
  overflow: hidden;
}

.preview-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 12px 16px;
  background: var(--bg-tertiary);
  cursor: move;
  user-select: none;
}

.preview-title {
  display: flex;
  align-items: center;
  gap: 8px;
  font-weight: 600;
  font-size: 14px;
}

.preview-title .preview-icon {
  font-size: 18px;
}

.preview-controls {
  display: flex;
  gap: 4px;
}

.control-btn {
  width: 28px;
  height: 28px;
  border: none;
  background: var(--bg-secondary);
  color: var(--text-secondary);
  border-radius: var(--border-radius-sm);
  cursor: pointer;
  font-size: 16px;
  display: flex;
  align-items: center;
  justify-content: center;
  transition: all 0.15s;
}

.control-btn:hover {
  background: var(--bg-hover);
  color: var(--text-primary);
}

.preview-content {
  padding: 12px;
}

.preview-image-container {
  width: 100%;
  aspect-ratio: 1;
  background: var(--bg-primary);
  border-radius: var(--border-radius);
  overflow: hidden;
  display: flex;
  align-items: center;
  justify-content: center;
}

.preview-image {
  width: 100%;
  height: 100%;
  object-fit: contain;
}

.preview-placeholder {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 100%;
  height: 100%;
}

.preview-info {
  margin-top: 12px;
}

.preview-progress {
  display: flex;
  align-items: center;
  gap: 12px;
}

.progress-bar-bg {
  flex: 1;
  height: 6px;
  background: var(--bg-tertiary);
  border-radius: 3px;
  overflow: hidden;
}

.progress-bar-fill {
  height: 100%;
  background: linear-gradient(90deg, var(--accent-primary), var(--accent-purple));
  border-radius: 3px;
  transition: width 0.3s ease;
}

.progress-text {
  font-size: 12px;
  color: var(--text-secondary);
  min-width: 60px;
  text-align: right;
}

/* Transition */
.preview-fade-enter-active,
.preview-fade-leave-active {
  transition: opacity 0.3s ease, transform 0.3s ease;
}

.preview-fade-enter-from,
.preview-fade-leave-to {
  opacity: 0;
  transform: scale(0.8);
}

/* Mobile adjustments */
@media (max-width: 768px) {
  .preview-expanded {
    width: 200px;
  }

  .preview-header {
    padding: 10px 12px;
  }

  .preview-content {
    padding: 8px;
  }
}
</style>
