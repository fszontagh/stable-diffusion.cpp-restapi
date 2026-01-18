<script setup lang="ts">
defineProps<{
  count?: number
}>()
</script>

<template>
  <div class="skeleton-list">
    <div v-for="i in (count || 5)" :key="i" class="skeleton-item">
      <div class="skeleton-avatar"></div>
      <div class="skeleton-item-content">
        <div class="skeleton-line"></div>
        <div class="skeleton-line short"></div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.skeleton-list {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.skeleton-item {
  display: flex;
  gap: 16px;
  align-items: center;
  padding: 16px;
  background: var(--bg-secondary);
  border: 1px solid var(--border-color);
  border-radius: var(--border-radius);
  animation: pulse 1.5s ease-in-out infinite;
}

.skeleton-avatar {
  width: 48px;
  height: 48px;
  background: var(--bg-tertiary);
  border-radius: var(--border-radius-sm);
  flex-shrink: 0;
}

.skeleton-item-content {
  flex: 1;
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.skeleton-line {
  height: 16px;
  background: var(--bg-tertiary);
  border-radius: 4px;
  width: 100%;
}

.skeleton-line.short {
  width: 60%;
}

@keyframes pulse {
  0%, 100% {
    opacity: 1;
  }
  50% {
    opacity: 0.5;
  }
}
</style>
