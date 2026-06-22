<script setup lang="ts">
// Structured editor for sd.cpp's backend / params_backend assignment strings.
//
// sd.cpp accepts a comma-separated list of `component=device` assignments —
// e.g. "diffusion=cuda0,vae=cpu" or "*=cpu". Typing that string by hand is
// what users were getting wrong: forgetting the comma, mistyping the device
// name, or not knowing which component keys are valid. This component
// turns it into a row editor with dropdowns, and serializes back to the
// same string the backend expects so nothing else changes.
//
// Power-user escape hatch: a "raw" toggle exposes the underlying string
// for cases where the user needs a device name we don't list (e.g.
// cuda4, vulkan2). Switching to raw mode and back re-parses cleanly.

import { ref, watch } from 'vue'

interface Props {
    // Accepts undefined so callers can v-model an optional LoadOptions field
    // (the TS interface marks `backend` / `params_backend` as `?: string`).
    // Undefined is normalized to "" everywhere downstream.
    modelValue: string | undefined
    placeholder?: string
    // Different known component sets for backend vs params_backend. Both
    // accept the wildcard "*" — that's the form sd.cpp uses internally for
    // the global fallback (and the form sd-cli's --offload-to-cpu emits).
    components?: string[]
    // If true, default new rows to "*=cpu" — the common case for the
    // params_backend "keep all weights in RAM" pattern.
    defaultGlobal?: boolean
}
const props = withDefaults(defineProps<Props>(), {
    placeholder: '',
    components: () => ['*', 'diffusion', 'te', 'vae', 'taesd', 'controlnet'],
    defaultGlobal: false,
})
const emit = defineEmits<{ 'update:modelValue': [string] }>()

// Devices that sd.cpp's backend name parser accepts. Kept as a flat list
// without device-availability detection — users with exotic configs (more
// CUDA devices, multiple Vulkan, custom OpenCL indices) fall back to raw
// mode. Order is roughly "most common first" so the dropdown is fast.
const DEVICES = [
    'cpu',
    'cuda0', 'cuda1', 'cuda2', 'cuda3',
    'vulkan0', 'vulkan1',
    'metal',
    'rocm',
    'opencl0',
]

interface Row {
    component: string
    device: string
}

function parse(s: string): Row[] {
    if (!s) return []
    return s.split(',')
        .map((part) => {
            const [c, d] = part.split('=', 2).map((x) => x.trim())
            return { component: c || '', device: d || '' }
        })
        .filter((r) => r.component && r.device)
}

function serialize(rows: Row[]): string {
    return rows
        .filter((r) => r.component && r.device)
        .map((r) => `${r.component}=${r.device}`)
        .join(',')
}

const rows = ref<Row[]>(parse(props.modelValue ?? ''))
const rawMode = ref(false)
const rawValue = ref(props.modelValue ?? '')

// External model-value updates (architecture preset apply, stream_layers
// watcher auto-filling params_backend, etc.) need to re-parse into rows
// so the UI reflects the new value. Guard against the value we just
// emitted ourselves — otherwise typing in raw mode would race.
watch(
    () => props.modelValue,
    (newVal) => {
        const v = newVal ?? ''
        if (v === rawValue.value) return
        rawValue.value = v
        if (!rawMode.value) rows.value = parse(v)
    }
)

function emitChange() {
    const s = serialize(rows.value)
    rawValue.value = s
    emit('update:modelValue', s)
}

function onRawInput() {
    rows.value = parse(rawValue.value)
    emit('update:modelValue', rawValue.value)
}

function addRow() {
    rows.value.push(
        props.defaultGlobal
            ? { component: '*', device: 'cpu' }
            : { component: props.components[0] === '*' ? props.components[1] || 'diffusion' : props.components[0], device: 'cpu' }
    )
    emitChange()
}

function removeRow(i: number) {
    rows.value.splice(i, 1)
    emitChange()
}
</script>

<template>
    <div class="backend-editor">
        <div v-if="!rawMode" class="backend-rows">
            <div v-for="(row, i) in rows" :key="i" class="backend-row">
                <select v-model="row.component" class="form-select" @change="emitChange">
                    <option v-for="c in components" :key="c" :value="c">{{ c }}</option>
                </select>
                <span class="backend-eq">=</span>
                <select v-model="row.device" class="form-select" @change="emitChange">
                    <option v-for="d in DEVICES" :key="d" :value="d">{{ d }}</option>
                </select>
                <button
                    type="button"
                    class="btn btn-ghost backend-remove"
                    :title="'Remove ' + row.component + '=' + row.device"
                    @click="removeRow(i)"
                >
                    &times;
                </button>
            </div>
            <button type="button" class="btn btn-sm btn-secondary backend-add" @click="addRow">
                + Add assignment
            </button>
        </div>
        <div v-else class="backend-raw">
            <input
                v-model="rawValue"
                type="text"
                class="form-input"
                :placeholder="placeholder"
                @input="onRawInput"
            />
        </div>
        <label class="form-checkbox backend-rawmode">
            <input v-model="rawMode" type="checkbox" />
            <span>Edit as text</span>
        </label>
    </div>
</template>

<style scoped>
.backend-editor {
    display: flex;
    flex-direction: column;
    gap: 0.4rem;
}
.backend-rows {
    display: flex;
    flex-direction: column;
    gap: 0.3rem;
}
.backend-row {
    display: flex;
    align-items: center;
    gap: 0.4rem;
}
.backend-row .form-select {
    flex: 1;
    min-width: 0;
}
.backend-eq {
    opacity: 0.7;
    font-family: var(--font-mono, monospace);
    padding: 0 0.1rem;
}
.backend-remove {
    flex: 0 0 auto;
    padding: 0.25rem 0.55rem;
    font-size: 1.1rem;
    line-height: 1;
}
.backend-add {
    align-self: flex-start;
}
.backend-rawmode {
    margin-top: 0.2rem;
    font-size: 0.8rem;
    opacity: 0.75;
}
</style>
