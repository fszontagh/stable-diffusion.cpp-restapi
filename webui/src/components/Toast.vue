<script setup lang="ts">
import { useAppStore } from '../stores/app'

const store = useAppStore()

const icons: Record<string, string> = {
  success: '&#10004;',
  error: '&#10060;',
  warning: '&#9888;',
  info: '&#8505;'
}
</script>

<template>
  <div class="toast-container">
    <div
      v-for="toast in store.toasts"
      :key="toast.id"
      :class="['toast', `toast-${toast.type}`]"
      @click="store.removeToast(toast.id)"
    >
      <span class="toast-icon" v-html="icons[toast.type]"></span>
      <span class="toast-message">{{ toast.message }}</span>
    </div>
  </div>
</template>

<style scoped>
.toast-icon {
  font-size: 16px;
}

.toast-message {
  flex: 1;
}
</style>
