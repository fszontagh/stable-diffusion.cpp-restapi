<script setup lang="ts">
import { ref, computed, watch } from 'vue'
import { useRouter } from 'vue-router'
import type { ArchitecturePreset } from '../api/client'
import { useAppStore } from '../stores/app'
import { archReadiness, buildLoadPrefill } from '../composables/archReadiness'
import { setLoadPrefill, loadPagePath } from '../composables/loadPrefill'

// "Load this architecture" bar: picks the main model when several are on
// disk, lists which file fills each required component, and opens the model
// load page pre-filled with them and the preset's load options.
// target "models": the button goes to the Models page architecture view
// focused on this preset instead, where the user picks the model variant.
const props = defineProps<{
  preset: ArchitecturePreset
  compact?: boolean
  target?: 'load' | 'models'
}>()
const emit = defineEmits<{ navigate: [] }>()

const store = useAppStore()
const router = useRouter()

const mainName = ref<string | null>(null)
watch(() => props.preset.id, () => { mainName.value = null })

const readiness = computed(() => archReadiness(props.preset, store.models, mainName.value))

function openLoad() {
  if (props.target === 'models') {
    emit('navigate')
    router.push({ path: '/models', query: { arch: props.preset.id } })
    return
  }
  const prefill = buildLoadPrefill(props.preset, readiness.value)
  if (!prefill) return
  setLoadPrefill(prefill)
  emit('navigate')
  router.push(loadPagePath(prefill.model_name))
}
</script>

<template>
  <div :class="['arch-load', { ready: readiness.ready, compact }]">
    <div class="arch-load-main">
      <template v-if="readiness.mains.length > 1">
        <label class="arch-load-label" :for="'main-' + preset.id">Model</label>
        <select
          :id="'main-' + preset.id"
          class="form-select arch-load-select"
          :value="readiness.main?.name"
          @change="mainName = ($event.target as HTMLSelectElement).value"
        >
          <option v-for="m in readiness.mains" :key="m.name" :value="m.name">
            {{ m.name }}{{ m.recommended ? ' ★' : '' }}
          </option>
        </select>
      </template>
      <span v-else-if="readiness.main" class="arch-load-file" :title="readiness.main.name">
        <span class="arch-load-label">Model</span> {{ readiness.main.name }}
      </span>
      <span v-else class="arch-load-missing">No main model downloaded yet</span>

      <button
        type="button"
        class="btn btn-primary btn-sm arch-load-btn"
        :disabled="!readiness.ready"
        :title="readiness.ready ? 'Open the load page with these files and the recommended settings' : 'Missing: ' + readiness.missing.join(', ')"
        @click="openLoad"
      >Load model...</button>
    </div>

    <ul v-if="!compact && readiness.components.length" class="arch-load-components">
      <li v-for="c in readiness.components" :key="c.key">
        <span class="comp-key">{{ c.key }}</span>
        <span v-if="c.file" class="comp-file" :title="c.source === 'suggested' ? 'Picked by file name' : 'Downloaded catalog file'">
          {{ c.file }}<span v-if="c.source === 'suggested'" class="comp-hint"> (by name)</span>
        </span>
        <span v-else-if="!c.supported" class="arch-load-missing">not supported by the load form</span>
        <span v-else-if="c.required" class="arch-load-missing">missing</span>
        <span v-else class="comp-hint">not found (optional)</span>
      </li>
    </ul>
    <p v-else-if="compact && !readiness.ready && readiness.main" class="arch-load-missing compact-missing">
      Missing: {{ readiness.missing.join(', ') }}
    </p>
  </div>
</template>

<style scoped>
.arch-load {
  border: 1px solid var(--border-color);
  border-radius: var(--border-radius-sm);
  background: var(--bg-primary);
  padding: 10px 12px;
}

.arch-load.ready {
  border-color: var(--accent-success);
}

.arch-load.compact {
  border: none;
  background: transparent;
  padding: 0;
}

.arch-load-main {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-wrap: wrap;
}

.arch-load-label {
  font-size: 11px;
  text-transform: uppercase;
  letter-spacing: 0.5px;
  color: var(--text-secondary);
}

.arch-load-select {
  flex: 1;
  min-width: 180px;
  max-width: 480px;
}

.arch-load-file {
  flex: 1;
  min-width: 0;
  font-size: 13px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.arch-load-btn {
  margin-left: auto;
  white-space: nowrap;
}

.arch-load-missing {
  color: var(--accent-warning);
  font-size: 12px;
}

.arch-load-components {
  list-style: none;
  margin: 8px 0 0;
  padding: 0;
  display: grid;
  grid-template-columns: auto minmax(0, 1fr);
  gap: 3px 12px;
  font-size: 12px;
}

.arch-load-components li {
  display: contents;
}

.comp-key {
  color: var(--text-secondary);
}

.comp-file {
  overflow-wrap: anywhere;
}

.comp-hint {
  color: var(--text-secondary);
}

.compact-missing {
  margin: 4px 0 0;
}
</style>
