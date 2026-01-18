<template>
  <div class="skeleton-card">
    <div class="skeleton-header"></div>
    <div class="skeleton-content">
      <div class="skeleton-line"></div>
      <div class="skeleton-line"></div>
      <div class="skeleton-line short"></div>
    </div>
  </div>
</template>

<style scoped>
.skeleton-card {
  background: var(--bg-secondary);
  border: 1px solid var(--border-color);
  border-radius: var(--border-radius);
  padding: 20px;
  margin-bottom: 16px;
  animation: pulse 1.5s ease-in-out infinite;
}

.skeleton-header {
  height: 24px;
  background: var(--bg-tertiary);
  border-radius: 4px;
  margin-bottom: 16px;
  width: 40%;
}

.skeleton-content {
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
