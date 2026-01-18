<script setup lang="ts">
import { onMounted, onUnmounted } from 'vue'
import { useAppStore } from './stores/app'
import Sidebar from './components/Sidebar.vue'
import StatusBar from './components/StatusBar.vue'
import Toast from './components/Toast.vue'
import Assistant from './components/Assistant.vue'
import AssistantQuestion from './components/AssistantQuestion.vue'
import FloatingPreview from './components/FloatingPreview.vue'
import ApiOfflineOverlay from './components/ApiOfflineOverlay.vue'
import ErrorBoundary from './components/ErrorBoundary.vue'

const store = useAppStore()

onMounted(() => {
  store.startPolling()
})

onUnmounted(() => {
  store.stopPolling()
})
</script>

<template>
  <!-- API Offline Overlay - renders above everything when disconnected -->
  <ApiOfflineOverlay />
  
  <ErrorBoundary>
    <StatusBar />
    <div class="app-layout">
      <Sidebar />
      <main class="main-content">
        <router-view />
      </main>
    </div>
    <Toast />
    <FloatingPreview />
    <Assistant />
    <AssistantQuestion />
  </ErrorBoundary>
</template>

<style scoped>
</style>
