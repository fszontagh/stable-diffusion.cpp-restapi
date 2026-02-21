<script setup lang="ts">
import { ref, computed } from 'vue'
import type { ModelInfo } from '../api/client'
import LoraSearch from './LoraSearch.vue'
import LoraItem from './LoraItem.vue'

export interface LoraEntry {
  name: string
  weight: number
  isValid: boolean
}

export interface LoraSettings {
  defaultWeight: number
  minWeight: number
  maxWeight: number
}

const props = defineProps<{
  positiveList: LoraEntry[]
  negativeList: LoraEntry[]
  availableLoras: ModelInfo[]
  settings: LoraSettings
}>()

const emit = defineEmits<{
  'update:positiveList': [list: LoraEntry[]]
  'update:negativeList': [list: LoraEntry[]]
  'update:settings': [settings: LoraSettings]
}>()

const isCollapsed = ref(false)
const activeTab = ref<'positive' | 'negative'>('positive')
const showSettings = ref(false)

const currentList = computed(() =>
  activeTab.value === 'positive' ? props.positiveList : props.negativeList
)

const excludeNames = computed(() => currentList.value.map(l => l.name))

function addLora(name: string) {
  const newEntry: LoraEntry = {
    name,
    weight: props.settings.defaultWeight,
    isValid: true
  }

  if (activeTab.value === 'positive') {
    emit('update:positiveList', [...props.positiveList, newEntry])
  } else {
    emit('update:negativeList', [...props.negativeList, newEntry])
  }
}

function updateLoraWeight(index: number, weight: number) {
  if (activeTab.value === 'positive') {
    const updated = [...props.positiveList]
    updated[index] = { ...updated[index], weight }
    emit('update:positiveList', updated)
  } else {
    const updated = [...props.negativeList]
    updated[index] = { ...updated[index], weight }
    emit('update:negativeList', updated)
  }
}

function removeLora(index: number) {
  if (activeTab.value === 'positive') {
    const updated = props.positiveList.filter((_, i) => i !== index)
    emit('update:positiveList', updated)
  } else {
    const updated = props.negativeList.filter((_, i) => i !== index)
    emit('update:negativeList', updated)
  }
}

function updateDefaultWeight(event: Event) {
  const target = event.target as HTMLInputElement
  const value = parseFloat(target.value)
  if (!isNaN(value)) {
    emit('update:settings', { ...props.settings, defaultWeight: value })
  }
}

function updateMinWeight(event: Event) {
  const target = event.target as HTMLInputElement
  const value = parseFloat(target.value)
  if (!isNaN(value)) {
    emit('update:settings', { ...props.settings, minWeight: value })
  }
}

function updateMaxWeight(event: Event) {
  const target = event.target as HTMLInputElement
  const value = parseFloat(target.value)
  if (!isNaN(value)) {
    emit('update:settings', { ...props.settings, maxWeight: value })
  }
}

const positiveCount = computed(() => props.positiveList.length)
const negativeCount = computed(() => props.negativeList.length)
</script>

<template>
  <div class="lora-panel" :class="{ 'collapsed': isCollapsed }">
    <div class="lora-panel-header" @click="isCollapsed = !isCollapsed">
      <span class="lora-panel-title">
        LoRAs
        <span v-if="positiveCount + negativeCount > 0" class="lora-count">
          ({{ positiveCount + negativeCount }})
        </span>
      </span>
      <span class="lora-panel-toggle">{{ isCollapsed ? '&#9654;' : '&#9660;' }}</span>
    </div>

    <div v-if="!isCollapsed" class="lora-panel-content">
      <!-- Tabs -->
      <div class="lora-tabs">
        <button
          class="lora-tab"
          :class="{ 'active': activeTab === 'positive' }"
          @click="activeTab = 'positive'"
        >
          Positive
          <span v-if="positiveCount > 0" class="tab-count">{{ positiveCount }}</span>
        </button>
        <button
          class="lora-tab"
          :class="{ 'active': activeTab === 'negative' }"
          @click="activeTab = 'negative'"
        >
          Negative
          <span v-if="negativeCount > 0" class="tab-count">{{ negativeCount }}</span>
        </button>
      </div>

      <!-- Search -->
      <div class="lora-search-wrapper">
        <LoraSearch
          :available-loras="availableLoras"
          :exclude-names="excludeNames"
          @select="addLora"
        />
      </div>

      <!-- LoRA List -->
      <div class="lora-list">
        <LoraItem
          v-for="(lora, index) in currentList"
          :key="`${activeTab}-${lora.name}`"
          :name="lora.name"
          :weight="lora.weight"
          :is-valid="lora.isValid"
          :min-weight="settings.minWeight"
          :max-weight="settings.maxWeight"
          @update:weight="updateLoraWeight(index, $event)"
          @remove="removeLora(index)"
        />
        <div v-if="currentList.length === 0" class="lora-empty">
          No LoRAs added. Search above or type &lt;lora:name:weight&gt; in prompt.
        </div>
      </div>

      <!-- Settings Toggle -->
      <div class="lora-settings-toggle">
        <button class="lora-settings-btn" @click="showSettings = !showSettings">
          &#9881; Settings {{ showSettings ? '&#9650;' : '&#9660;' }}
        </button>
      </div>

      <!-- Settings -->
      <div v-if="showSettings" class="lora-settings">
        <div class="lora-setting-row">
          <label>Default Weight:</label>
          <input
            type="number"
            :value="settings.defaultWeight"
            :min="settings.minWeight"
            :max="settings.maxWeight"
            step="0.1"
            @change="updateDefaultWeight"
          />
        </div>
        <div class="lora-setting-row">
          <label>Min Weight:</label>
          <input
            type="number"
            :value="settings.minWeight"
            step="0.1"
            @change="updateMinWeight"
          />
        </div>
        <div class="lora-setting-row">
          <label>Max Weight:</label>
          <input
            type="number"
            :value="settings.maxWeight"
            step="0.1"
            @change="updateMaxWeight"
          />
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.lora-panel {
  background: var(--bg-card, #242424);
  border: 1px solid var(--border-color, #444);
  border-radius: 8px;
  overflow: hidden;
}

.lora-panel-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 0.75rem 1rem;
  background: var(--bg-secondary, #2a2a2a);
  cursor: pointer;
  user-select: none;
}

.lora-panel-header:hover {
  background: var(--bg-hover, #333);
}

.lora-panel-title {
  font-weight: 600;
  font-size: 0.9rem;
}

.lora-count {
  color: var(--text-secondary, #888);
  font-weight: normal;
}

.lora-panel-toggle {
  color: var(--text-secondary, #888);
  font-size: 0.75rem;
}

.lora-panel-content {
  padding: 0.75rem;
}

.lora-tabs {
  display: flex;
  gap: 0.25rem;
  margin-bottom: 0.75rem;
}

.lora-tab {
  flex: 1;
  padding: 0.5rem;
  background: var(--bg-secondary, #2a2a2a);
  border: 1px solid var(--border-color, #444);
  border-radius: 4px;
  color: var(--text-secondary, #888);
  font-size: 0.875rem;
  cursor: pointer;
  transition: all 0.15s ease;
}

.lora-tab:hover {
  background: var(--bg-hover, #333);
}

.lora-tab.active {
  background: var(--primary-color, #007bff);
  border-color: var(--primary-color, #007bff);
  color: white;
}

.tab-count {
  margin-left: 0.25rem;
  padding: 0.1rem 0.35rem;
  background: rgba(255, 255, 255, 0.2);
  border-radius: 10px;
  font-size: 0.75rem;
}

.lora-search-wrapper {
  margin-bottom: 0.75rem;
}

.lora-list {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
  max-height: 300px;
  overflow-y: auto;
}

.lora-empty {
  padding: 1rem;
  text-align: center;
  color: var(--text-secondary, #888);
  font-size: 0.875rem;
}

.lora-settings-toggle {
  margin-top: 0.75rem;
  padding-top: 0.75rem;
  border-top: 1px solid var(--border-color, #333);
}

.lora-settings-btn {
  width: 100%;
  padding: 0.5rem;
  background: transparent;
  border: 1px solid var(--border-color, #444);
  border-radius: 4px;
  color: var(--text-secondary, #888);
  font-size: 0.75rem;
  cursor: pointer;
}

.lora-settings-btn:hover {
  background: var(--bg-hover, #333);
}

.lora-settings {
  margin-top: 0.5rem;
  padding: 0.75rem;
  background: var(--bg-secondary, #2a2a2a);
  border-radius: 4px;
}

.lora-setting-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 0.5rem;
}

.lora-setting-row:last-child {
  margin-bottom: 0;
}

.lora-setting-row label {
  font-size: 0.75rem;
  color: var(--text-secondary, #888);
}

.lora-setting-row input {
  width: 70px;
  padding: 0.25rem;
  font-size: 0.75rem;
  text-align: center;
  background: var(--bg-primary, #1a1a1a);
  border: 1px solid var(--border-color, #444);
  border-radius: 4px;
  color: var(--text-primary, #fff);
}

.lora-setting-row input:focus {
  outline: none;
  border-color: var(--primary-color, #007bff);
}
</style>
