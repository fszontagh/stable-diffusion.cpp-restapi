<script setup lang="ts">
import { ref, watch, onMounted, onUnmounted } from 'vue'

const props = defineProps<{
  show: boolean
  images: string[]
  currentIndex: number
}>()

const emit = defineEmits<{
  (e: 'close'): void
  (e: 'update:currentIndex', index: number): void
}>()

const localIndex = ref(props.currentIndex)

watch(() => props.currentIndex, (val) => {
  localIndex.value = val
})

watch(() => props.show, (val) => {
  if (val) {
    document.body.style.overflow = 'hidden'
  } else {
    document.body.style.overflow = ''
  }
})

function prev() {
  if (localIndex.value > 0) {
    localIndex.value--
    emit('update:currentIndex', localIndex.value)
  }
}

function next() {
  if (localIndex.value < props.images.length - 1) {
    localIndex.value++
    emit('update:currentIndex', localIndex.value)
  }
}

function close() {
  emit('close')
}

function handleKeydown(e: KeyboardEvent) {
  if (!props.show) return
  switch (e.key) {
    case 'Escape':
      close()
      break
    case 'ArrowLeft':
      prev()
      break
    case 'ArrowRight':
      next()
      break
  }
}

function handleBackdropClick(e: MouseEvent) {
  if ((e.target as HTMLElement).classList.contains('lightbox-backdrop')) {
    close()
  }
}

onMounted(() => {
  document.addEventListener('keydown', handleKeydown)
})

onUnmounted(() => {
  document.removeEventListener('keydown', handleKeydown)
  document.body.style.overflow = ''
})
</script>

<template>
  <Teleport to="body">
    <Transition name="lightbox">
      <div v-if="show" class="lightbox-backdrop" @click="handleBackdropClick">
        <div class="lightbox-container">
          <button class="lightbox-close" @click="close" title="Close (Esc)">
            &times;
          </button>

          <button
            v-if="images.length > 1"
            class="lightbox-nav lightbox-prev"
            @click.stop="prev"
            :disabled="localIndex === 0"
            title="Previous (Left Arrow)"
          >
            &#10094;
          </button>

          <div class="lightbox-content">
            <img
              :src="images[localIndex]"
              :alt="`Image ${localIndex + 1}`"
              class="lightbox-image"
            />
          </div>

          <button
            v-if="images.length > 1"
            class="lightbox-nav lightbox-next"
            @click.stop="next"
            :disabled="localIndex === images.length - 1"
            title="Next (Right Arrow)"
          >
            &#10095;
          </button>

          <div v-if="images.length > 1" class="lightbox-counter">
            {{ localIndex + 1 }} / {{ images.length }}
          </div>

          <div v-if="images.length > 1" class="lightbox-thumbnails">
            <button
              v-for="(img, idx) in images"
              :key="idx"
              :class="['lightbox-thumb', { active: idx === localIndex }]"
              @click.stop="localIndex = idx; emit('update:currentIndex', idx)"
            >
              <img :src="img" :alt="`Thumbnail ${idx + 1}`" />
            </button>
          </div>
        </div>
      </div>
    </Transition>
  </Teleport>
</template>

<style scoped>
.lightbox-backdrop {
  position: fixed;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  background: rgba(0, 0, 0, 0.95);
  z-index: 10000;
  display: flex;
  align-items: center;
  justify-content: center;
}

.lightbox-container {
  position: relative;
  width: 100%;
  height: 100%;
  display: flex;
  align-items: center;
  justify-content: center;
}

.lightbox-close {
  position: absolute;
  top: 16px;
  right: 16px;
  width: 48px;
  height: 48px;
  background: rgba(255, 255, 255, 0.1);
  border: none;
  border-radius: 50%;
  color: white;
  font-size: 32px;
  cursor: pointer;
  transition: all 0.2s;
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 10001;
}

.lightbox-close:hover {
  background: rgba(255, 255, 255, 0.2);
  transform: scale(1.1);
}

.lightbox-nav {
  position: absolute;
  top: 50%;
  transform: translateY(-50%);
  width: 56px;
  height: 56px;
  background: rgba(255, 255, 255, 0.1);
  border: none;
  border-radius: 50%;
  color: white;
  font-size: 24px;
  cursor: pointer;
  transition: all 0.2s;
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 10001;
}

.lightbox-nav:hover:not(:disabled) {
  background: rgba(255, 255, 255, 0.2);
}

.lightbox-nav:disabled {
  opacity: 0.3;
  cursor: not-allowed;
}

.lightbox-prev {
  left: 16px;
}

.lightbox-next {
  right: 16px;
}

.lightbox-content {
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 80px;
}

.lightbox-image {
  max-width: calc(100vw - 160px);
  max-height: calc(100vh - 160px);
  width: auto;
  height: auto;
  object-fit: contain;
  border-radius: 8px;
  box-shadow: 0 8px 32px rgba(0, 0, 0, 0.5);
}

.lightbox-counter {
  position: absolute;
  top: 24px;
  left: 50%;
  transform: translateX(-50%);
  color: rgba(255, 255, 255, 0.8);
  font-size: 14px;
  font-weight: 500;
  background: rgba(0, 0, 0, 0.5);
  padding: 6px 16px;
  border-radius: 20px;
}

.lightbox-thumbnails {
  position: absolute;
  bottom: 16px;
  left: 50%;
  transform: translateX(-50%);
  display: flex;
  gap: 8px;
  max-width: calc(100% - 32px);
  overflow-x: auto;
  padding: 8px;
  background: rgba(0, 0, 0, 0.5);
  border-radius: 8px;
}

.lightbox-thumb {
  width: 60px;
  height: 60px;
  flex-shrink: 0;
  padding: 0;
  background: none;
  border: 2px solid transparent;
  border-radius: 4px;
  cursor: pointer;
  overflow: hidden;
  transition: all 0.2s;
}

.lightbox-thumb:hover {
  border-color: rgba(255, 255, 255, 0.5);
}

.lightbox-thumb.active {
  border-color: var(--accent-primary, #00d9ff);
}

.lightbox-thumb img {
  width: 100%;
  height: 100%;
  object-fit: cover;
}

/* Transitions */
.lightbox-enter-active,
.lightbox-leave-active {
  transition: opacity 0.2s ease;
}

.lightbox-enter-from,
.lightbox-leave-to {
  opacity: 0;
}

.lightbox-enter-active .lightbox-image,
.lightbox-leave-active .lightbox-image {
  transition: transform 0.2s ease;
}

.lightbox-enter-from .lightbox-image {
  transform: scale(0.9);
}

.lightbox-leave-to .lightbox-image {
  transform: scale(0.9);
}
</style>
