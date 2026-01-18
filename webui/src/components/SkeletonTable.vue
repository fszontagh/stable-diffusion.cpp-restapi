<script setup lang="ts">
defineProps<{
  rows?: number
  columns?: number
}>()
</script>

<template>
  <div class="skeleton-table">
    <div class="skeleton-table-header">
      <div v-for="col in (columns || 4)" :key="`header-${col}`" class="skeleton-table-cell"></div>
    </div>
    <div v-for="row in (rows || 5)" :key="`row-${row}`" class="skeleton-table-row">
      <div v-for="col in (columns || 4)" :key="`cell-${row}-${col}`" class="skeleton-table-cell"></div>
    </div>
  </div>
</template>

<style scoped>
.skeleton-table {
  background: var(--bg-secondary);
  border: 1px solid var(--border-color);
  border-radius: var(--border-radius);
  overflow: hidden;
}

.skeleton-table-header {
  display: grid;
  grid-template-columns: repeat(var(--columns, 4), 1fr);
  gap: 12px;
  padding: 16px;
  background: var(--bg-tertiary);
  border-bottom: 1px solid var(--border-color);
}

.skeleton-table-row {
  display: grid;
  grid-template-columns: repeat(var(--columns, 4), 1fr);
  gap: 12px;
  padding: 16px;
  border-bottom: 1px solid var(--border-color);
  animation: pulse 1.5s ease-in-out infinite;
}

.skeleton-table-row:last-child {
  border-bottom: none;
}

.skeleton-table-cell {
  height: 20px;
  background: var(--bg-tertiary);
  border-radius: 4px;
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
