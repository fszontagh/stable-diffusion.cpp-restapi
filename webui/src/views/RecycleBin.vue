<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { api, type Job, type RecycleBinSettings } from '../api/client'

// State
const loading = ref(false)
const items = ref<Job[]>([])
const settings = ref<RecycleBinSettings | null>(null)
const error = ref<string | null>(null)
const actionInProgress = ref<string | null>(null)

// Fetch recycle bin contents
async function fetchRecycleBin() {
  loading.value = true
  error.value = null
  try {
    const response = await api.getRecycleBin()
    items.value = response.items
    settings.value = {
      enabled: response.enabled,
      retention_minutes: response.retention_minutes
    }
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Failed to fetch recycle bin'
  } finally {
    loading.value = false
  }
}

// Restore a job
async function restoreJob(jobId: string) {
  actionInProgress.value = jobId
  try {
    await api.restoreJob(jobId)
    // Remove from local list
    items.value = items.value.filter(item => item.job_id !== jobId)
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Failed to restore job'
  } finally {
    actionInProgress.value = null
  }
}

// Permanently delete a job
async function purgeJob(jobId: string) {
  if (!confirm('Permanently delete this item? This cannot be undone.')) {
    return
  }
  actionInProgress.value = jobId
  try {
    await api.purgeJob(jobId)
    // Remove from local list
    items.value = items.value.filter(item => item.job_id !== jobId)
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Failed to purge job'
  } finally {
    actionInProgress.value = null
  }
}

// Clear entire recycle bin
async function clearAll() {
  if (!confirm(`Permanently delete all ${items.value.length} items? This cannot be undone.`)) {
    return
  }
  loading.value = true
  try {
    await api.clearRecycleBin()
    items.value = []
  } catch (e) {
    error.value = e instanceof Error ? e.message : 'Failed to clear recycle bin'
  } finally {
    loading.value = false
  }
}

// Format time remaining until auto-purge
function formatTimeRemaining(deletedAt: string): string {
  if (!settings.value) return ''

  const deletedTime = new Date(deletedAt).getTime()
  const expiresAt = deletedTime + (settings.value.retention_minutes * 60 * 1000)
  const now = Date.now()
  const remaining = expiresAt - now

  if (remaining <= 0) return 'Expiring soon'

  const minutes = Math.floor(remaining / (60 * 1000))
  const hours = Math.floor(minutes / 60)
  const days = Math.floor(hours / 24)

  if (days > 0) return `${days}d ${hours % 24}h remaining`
  if (hours > 0) return `${hours}h ${minutes % 60}m remaining`
  return `${minutes}m remaining`
}

// Format date
function formatDate(dateStr: string): string {
  const date = new Date(dateStr)
  return date.toLocaleString()
}

// Get job type label
function getTypeLabel(type: Job['type']): string {
  const labels: Record<string, string> = {
    'txt2img': 'Text to Image',
    'img2img': 'Image to Image',
    'txt2vid': 'Text to Video',
    'upscale': 'Upscale',
    'convert': 'Convert',
    'model_download': 'Download',
    'model_hash': 'Hash'
  }
  return labels[type] || type
}

// Get prompt preview
function getPromptPreview(job: Job): string {
  if (!job.params?.prompt) return 'No prompt'
  const prompt = job.params.prompt as string
  return prompt.length > 100 ? prompt.substring(0, 100) + '...' : prompt
}

// Initial fetch
onMounted(() => {
  fetchRecycleBin()
})
</script>

<template>
  <div class="recycle-bin-page">
    <div class="page-header">
      <div class="header-content">
        <h1>Recycle Bin</h1>
        <p class="page-description">
          Deleted items are kept here before permanent removal.
          <span v-if="settings">
            Items are automatically purged after {{ Math.floor(settings.retention_minutes / 60 / 24) }} days.
          </span>
        </p>
      </div>
      <div class="header-actions" v-if="items.length > 0">
        <button
          class="btn-danger"
          @click="clearAll"
          :disabled="loading"
        >
          Empty Recycle Bin
        </button>
      </div>
    </div>

    <!-- Disabled message -->
    <div v-if="settings && !settings.enabled" class="notice warning">
      Recycle bin is disabled. Deleted items are permanently removed immediately.
    </div>

    <!-- Error message -->
    <div v-if="error" class="notice error">
      {{ error }}
      <button @click="error = null" class="dismiss">Dismiss</button>
    </div>

    <!-- Loading state -->
    <div v-if="loading && items.length === 0" class="loading">
      Loading recycle bin...
    </div>

    <!-- Empty state -->
    <div v-else-if="items.length === 0 && !loading" class="empty-state">
      <div class="empty-icon">&#x1F5D1;</div>
      <h2>Recycle Bin is Empty</h2>
      <p>Deleted queue items will appear here.</p>
    </div>

    <!-- Items list -->
    <div v-else class="items-list">
      <div
        v-for="item in items"
        :key="item.job_id"
        class="item-card"
      >
        <div class="item-header">
          <span class="item-type">{{ getTypeLabel(item.type) }}</span>
          <span class="item-status" :class="item.previous_status">
            was: {{ item.previous_status }}
          </span>
        </div>

        <div class="item-content">
          <p class="item-prompt">{{ getPromptPreview(item) }}</p>
          <div class="item-meta">
            <span class="meta-item">
              <strong>Created:</strong> {{ formatDate(item.created_at) }}
            </span>
            <span class="meta-item">
              <strong>Deleted:</strong> {{ formatDate(item.deleted_at!) }}
            </span>
            <span class="meta-item time-remaining">
              {{ formatTimeRemaining(item.deleted_at!) }}
            </span>
          </div>
        </div>

        <div class="item-actions">
          <button
            class="btn-primary"
            @click="restoreJob(item.job_id)"
            :disabled="actionInProgress === item.job_id"
          >
            {{ actionInProgress === item.job_id ? 'Restoring...' : 'Restore' }}
          </button>
          <button
            class="btn-danger"
            @click="purgeJob(item.job_id)"
            :disabled="actionInProgress === item.job_id"
          >
            {{ actionInProgress === item.job_id ? 'Deleting...' : 'Delete Forever' }}
          </button>
        </div>
      </div>
    </div>

    <!-- Item count -->
    <div v-if="items.length > 0" class="item-count">
      {{ items.length }} item{{ items.length !== 1 ? 's' : '' }} in recycle bin
    </div>
  </div>
</template>

<style scoped>
.recycle-bin-page {
  max-width: 900px;
  margin: 0 auto;
  padding: 24px;
}

.page-header {
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
  margin-bottom: 24px;
}

.header-content h1 {
  margin: 0 0 8px;
  color: var(--text-primary);
}

.page-description {
  color: var(--text-secondary);
  margin: 0;
}

.notice {
  padding: 12px 16px;
  border-radius: var(--border-radius);
  margin-bottom: 16px;
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.notice.warning {
  background: rgba(255, 193, 7, 0.15);
  border: 1px solid rgba(255, 193, 7, 0.3);
  color: #ffc107;
}

.notice.error {
  background: rgba(244, 67, 54, 0.15);
  border: 1px solid rgba(244, 67, 54, 0.3);
  color: #f44336;
}

.notice .dismiss {
  background: transparent;
  border: none;
  color: inherit;
  cursor: pointer;
  opacity: 0.7;
}

.notice .dismiss:hover {
  opacity: 1;
}

.loading {
  text-align: center;
  padding: 48px;
  color: var(--text-secondary);
}

.empty-state {
  text-align: center;
  padding: 64px 24px;
  background: var(--bg-secondary);
  border-radius: var(--border-radius);
  border: 1px dashed var(--border-color);
}

.empty-icon {
  font-size: 64px;
  margin-bottom: 16px;
  opacity: 0.5;
}

.empty-state h2 {
  margin: 0 0 8px;
  color: var(--text-primary);
}

.empty-state p {
  margin: 0;
  color: var(--text-secondary);
}

.items-list {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.item-card {
  background: var(--bg-secondary);
  border: 1px solid var(--border-color);
  border-radius: var(--border-radius);
  padding: 16px;
}

.item-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 12px;
}

.item-type {
  font-weight: 600;
  color: var(--text-primary);
}

.item-status {
  font-size: 12px;
  padding: 4px 8px;
  border-radius: var(--border-radius-sm);
  background: var(--bg-tertiary);
  color: var(--text-secondary);
}

.item-status.completed {
  background: rgba(76, 175, 80, 0.2);
  color: #4caf50;
}

.item-status.failed {
  background: rgba(244, 67, 54, 0.2);
  color: #f44336;
}

.item-status.cancelled {
  background: rgba(158, 158, 158, 0.2);
  color: #9e9e9e;
}

.item-content {
  margin-bottom: 12px;
}

.item-prompt {
  margin: 0 0 8px;
  color: var(--text-primary);
  font-size: 14px;
  line-height: 1.5;
}

.item-meta {
  display: flex;
  flex-wrap: wrap;
  gap: 16px;
  font-size: 12px;
  color: var(--text-secondary);
}

.meta-item strong {
  color: var(--text-muted);
}

.time-remaining {
  color: var(--accent-primary);
  font-weight: 500;
}

.item-actions {
  display: flex;
  gap: 8px;
  padding-top: 12px;
  border-top: 1px solid var(--border-color);
}

.btn-primary,
.btn-danger {
  padding: 8px 16px;
  border: none;
  border-radius: var(--border-radius-sm);
  font-size: 13px;
  font-weight: 500;
  cursor: pointer;
  transition: all var(--transition-fast);
}

.btn-primary {
  background: var(--accent-primary);
  color: #fff;
}

.btn-primary:hover:not(:disabled) {
  background: var(--accent-hover);
}

.btn-danger {
  background: rgba(244, 67, 54, 0.15);
  color: #f44336;
}

.btn-danger:hover:not(:disabled) {
  background: rgba(244, 67, 54, 0.25);
}

.btn-primary:disabled,
.btn-danger:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.item-count {
  text-align: center;
  margin-top: 16px;
  padding: 12px;
  color: var(--text-secondary);
  font-size: 13px;
}
</style>
