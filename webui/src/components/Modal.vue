<script setup lang="ts">
import { ref, watch, onBeforeUnmount, nextTick } from 'vue'

const props = defineProps<{
  title: string
  show: boolean
}>()

const emit = defineEmits<{
  close: []
}>()

const modalContent = ref<HTMLElement | null>(null)
const previousFocus = ref<HTMLElement | null>(null)

// Focus management
watch(() => props.show, async (isShown) => {
  if (isShown) {
    // Store previously focused element
    previousFocus.value = document.activeElement as HTMLElement
    
    // Wait for modal to render
    await nextTick()
    
    // Focus the modal content
    if (modalContent.value) {
      const firstFocusable = modalContent.value.querySelector(
        'button, [href], input, select, textarea, [tabindex]:not([tabindex="-1"])'
      ) as HTMLElement
      
      if (firstFocusable) {
        firstFocusable.focus()
      } else {
        modalContent.value.focus()
      }
    }
  } else if (previousFocus.value) {
    // Restore focus to previously focused element
    previousFocus.value.focus()
    previousFocus.value = null
  }
})

// Handle Escape key
function handleKeydown(event: KeyboardEvent) {
  if (event.key === 'Escape' && props.show) {
    emit('close')
  }
}

// Trap focus within modal
function handleFocusTrap(event: KeyboardEvent) {
  if (!props.show || event.key !== 'Tab' || !modalContent.value) return
  
  const focusableElements = modalContent.value.querySelectorAll(
    'button, [href], input, select, textarea, [tabindex]:not([tabindex="-1"])'
  )
  const firstElement = focusableElements[0] as HTMLElement
  const lastElement = focusableElements[focusableElements.length - 1] as HTMLElement
  
  if (event.shiftKey) {
    if (document.activeElement === firstElement) {
      event.preventDefault()
      lastElement?.focus()
    }
  } else {
    if (document.activeElement === lastElement) {
      event.preventDefault()
      firstElement?.focus()
    }
  }
}

// Add event listeners
watch(() => props.show, (isShown) => {
  if (isShown) {
    document.addEventListener('keydown', handleKeydown)
    document.addEventListener('keydown', handleFocusTrap)
  } else {
    document.removeEventListener('keydown', handleKeydown)
    document.removeEventListener('keydown', handleFocusTrap)
  }
})

onBeforeUnmount(() => {
  document.removeEventListener('keydown', handleKeydown)
  document.removeEventListener('keydown', handleFocusTrap)
})
</script>

<template>
  <Teleport to="body">
    <div v-if="show" class="modal-overlay" @click.self="emit('close')" role="dialog" aria-modal="true" :aria-labelledby="`modal-title-${title}`">
      <div ref="modalContent" class="modal-content" tabindex="-1">
        <div class="modal-header">
          <h3 :id="`modal-title-${title}`" class="modal-title">{{ title }}</h3>
          <button class="modal-close" @click="emit('close')" aria-label="Close modal">&times;</button>
        </div>
        <div class="modal-body">
          <slot />
        </div>
        <div class="modal-footer" v-if="$slots.footer">
          <slot name="footer" />
        </div>
      </div>
    </div>
  </Teleport>
</template>
