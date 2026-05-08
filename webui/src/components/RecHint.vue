<!--
  Inline "recommended" hint shown below a form control.

  Two reasons it exists:
   1. Tooltips (`:title` attribute) don't work on touch devices, so the
      per-field rationale we sourced from sd.cpp was invisible on mobile.
      RecHint surfaces the same `recommended` text inline so the form
      is self-documenting.
   2. The visibility is user-toggleable via UIPreferences.show_option_hints
      (Settings → General). On by default — power users who already know
      what each option does can declutter the form.

  Usage:
    <RecHint :desc="getOptionDesc('flash_attn')" />
    <RecHint :desc="genOpt('cfg_scale', 'recommended')" />

  The `desc` prop is whatever the parent's lookup helper returns —
  RecHint just reads `.recommended` off it. Renders nothing when:
    - the option name isn't in the description JSON,
    - or the user has hidden hints in Settings.
-->
<template>
  <small v-if="visible" class="rec-hint-inline">{{ text }}</small>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { useAppStore } from '../stores/app'

const props = defineProps<{
  desc?: { recommended?: string; description?: string } | null
  /** Override which field to show. Defaults to "recommended". */
  field?: 'recommended' | 'description'
}>()

const store = useAppStore()
const text = computed(() => {
  if (!props.desc) return ''
  const f = props.field ?? 'recommended'
  return (props.desc[f] ?? '').trim()
})
const visible = computed(() => {
  // Default on if the preference hasn't loaded yet — better to show than
  // to hide on first paint.
  const enabled = store.uiPreferences?.show_option_hints
  if (enabled === false) return false
  return !!text.value
})
</script>

<style scoped>
.rec-hint-inline {
  display: block;
  margin-top: 4px;
  font-size: 12px;
  line-height: 1.4;
  color: var(--text-muted);
  font-style: italic;
}
</style>
