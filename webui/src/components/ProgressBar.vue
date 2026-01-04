<script setup lang="ts">
defineProps<{
  progress: number
  showLabel?: boolean
}>()
</script>

<template>
  <div class="progress-wrapper">
    <div class="progress">
      <div class="progress-bar" :style="{ width: `${progress}%` }"></div>
    </div>
    <span v-if="showLabel" class="progress-label">{{ progress.toFixed(1) }}%</span>
  </div>
</template>

<style scoped>
.progress-wrapper {
  display: flex;
  align-items: center;
  gap: 12px;
}

.progress {
  flex: 1;
}

.progress-label {
  font-size: 12px;
  color: var(--text-secondary);
  min-width: 50px;
  text-align: right;
}
</style>
