<template>
  <div ref="containerRef" class="lazy-image-container">
    <img
      v-if="shouldLoad"
      :src="actualSrc"
      :alt="alt"
      :class="imageClass"
      @load="onLoad"
      @error="onError"
    />
    <div v-else class="lazy-image-placeholder">
      <div class="lazy-spinner"></div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed } from 'vue'
import { useLazyLoad } from '../composables/useLazyLoad'

const props = withDefaults(defineProps<{
  src: string
  alt?: string
  placeholder?: string
  rootMargin?: string
  threshold?: number
}>(), {
  alt: '',
  placeholder: '',
  rootMargin: '100px',
  threshold: 0.01
})

const emit = defineEmits<{
  load: []
  error: []
}>()

const containerRef = ref<HTMLElement | null>(null)
const shouldLoad = ref(false)
const isLoaded = ref(false)
const hasError = ref(false)

const actualSrc = computed(() => {
  if (hasError.value && props.placeholder) {
    return props.placeholder
  }
  return props.src
})

const imageClass = computed(() => ({
  'lazy-image': true,
  'lazy-image-loaded': isLoaded.value,
  'lazy-image-error': hasError.value
}))

useLazyLoad(
  containerRef,
  () => {
    shouldLoad.value = true
  },
  {
    rootMargin: props.rootMargin,
    threshold: props.threshold
  }
)

const onLoad = () => {
  isLoaded.value = true
  emit('load')
}

const onError = () => {
  hasError.value = true
  emit('error')
}
</script>

<style scoped>
.lazy-image-container {
  position: relative;
  width: 100%;
  height: 100%;
  overflow: hidden;
}

.lazy-image {
  width: 100%;
  height: 100%;
  object-fit: cover;
  opacity: 0;
  transition: opacity 0.3s ease-in-out;
}

.lazy-image-loaded {
  opacity: 1;
}

.lazy-image-error {
  opacity: 0.5;
  filter: grayscale(100%);
}

.lazy-image-placeholder {
  width: 100%;
  height: 100%;
  display: flex;
  align-items: center;
  justify-content: center;
  background: var(--bg-tertiary);
}

.lazy-spinner {
  width: 24px;
  height: 24px;
  border: 2px solid var(--border-color);
  border-top-color: var(--accent-primary);
  border-radius: 50%;
  animation: spin 0.8s linear infinite;
}

@keyframes spin {
  to {
    transform: rotate(360deg);
  }
}
</style>
