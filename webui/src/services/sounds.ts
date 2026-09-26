/*
 * Sound notification service.
 *
 * A single Audio pool + a tiny catalog of event -> slug mappings, plus
 * localStorage-persisted per-event enable flags. Everything else in the
 * WebUI stays unchanged: showToast, WS handlers and store actions call
 * `playSound('job.completed', 'txt2img')` alongside their usual toast,
 * and this file decides which mp3 (if any) to play.
 *
 * Files live at /sounds/<slug>.mp3 (shipped via webui/public/sounds/).
 * Missing files just fail the play() promise silently - a friendly
 * degradation for hand-edited installs where the operator removed
 * one of the mp3s.
 */

// Every event the WebUI knows about + its default slug. `null` = no
// default sound; user can opt in later via Settings.
export interface SoundEvent {
  id: SoundEventId
  label: string
  category: 'job' | 'model' | 'server'
  defaultSlug: string | null
}

export type SoundEventId =
  | 'job.queued'
  | 'job.txt2img.completed'
  | 'job.img2img.completed'
  | 'job.txt2vid.completed'
  | 'job.upscale.completed'
  | 'job.convert.completed'
  | 'job.model_download.completed'
  | 'job.model_hash.completed'
  | 'job.failed'
  | 'job.cancelled'
  | 'model.load.started'
  | 'model.load.completed'
  | 'model.load.failed'
  | 'model.unloaded'
  | 'upscaler.loaded'
  | 'upscaler.unloaded'
  | 'server.shutdown'

export const SOUND_EVENTS: readonly SoundEvent[] = [
  { id: 'job.queued',                    label: 'Job queued',           category: 'job',    defaultSlug: 'chime-queued' },
  { id: 'job.txt2img.completed',         label: 'Image job done',       category: 'job',    defaultSlug: 'chime-image' },
  { id: 'job.img2img.completed',         label: 'Image edit done',      category: 'job',    defaultSlug: 'chime-image' },
  { id: 'job.txt2vid.completed',         label: 'Video job done',       category: 'job',    defaultSlug: 'chime-video' },
  { id: 'job.upscale.completed',         label: 'Upscale done',         category: 'job',    defaultSlug: 'chime-upscale' },
  { id: 'job.convert.completed',         label: 'Convert done',         category: 'job',    defaultSlug: 'chime-convert' },
  { id: 'job.model_download.completed',  label: 'Download done',        category: 'job',    defaultSlug: 'chime-download' },
  { id: 'job.model_hash.completed',      label: 'Hash done',            category: 'job',    defaultSlug: 'chime-hash' },
  { id: 'job.failed',                    label: 'Job failed',           category: 'job',    defaultSlug: 'tone-error' },
  { id: 'job.cancelled',                 label: 'Job cancelled',        category: 'job',    defaultSlug: 'tone-cancel' },
  { id: 'model.load.started',            label: 'Model load started',   category: 'model',  defaultSlug: 'tone-loading' },
  { id: 'model.load.completed',          label: 'Model loaded',         category: 'model',  defaultSlug: 'tone-ready' },
  { id: 'model.load.failed',             label: 'Model load failed',    category: 'model',  defaultSlug: 'tone-error' },
  { id: 'model.unloaded',                label: 'Model unloaded',       category: 'model',  defaultSlug: null },
  { id: 'upscaler.loaded',               label: 'Upscaler loaded',      category: 'model',  defaultSlug: null },
  { id: 'upscaler.unloaded',             label: 'Upscaler unloaded',    category: 'model',  defaultSlug: null },
  { id: 'server.shutdown',               label: 'Server shutdown',      category: 'server', defaultSlug: 'tone-shutdown' },
] as const

// Persisted settings. Kept in localStorage so the sound pack works
// per-browser (matches the desktop-notification pattern).
interface StoredPrefs {
  master: boolean
  volume: number                         // 0.0 - 1.0
  perEvent: Partial<Record<SoundEventId, { enabled: boolean; slug: string | null }>>
}

const STORAGE_KEY = 'sdcpp_sound_prefs_v1'
const DEFAULT_VOLUME = 0.6

function loadPrefs(): StoredPrefs {
  try {
    const raw = localStorage.getItem(STORAGE_KEY)
    if (raw) {
      const parsed = JSON.parse(raw)
      return {
        master: parsed.master ?? false,
        volume: typeof parsed.volume === 'number' ? parsed.volume : DEFAULT_VOLUME,
        perEvent: parsed.perEvent ?? {},
      }
    }
  } catch { /* fall through to defaults */ }
  // Off by default - browsers block autoplay until the user interacts
  // with the page anyway, and unrequested sound in a fresh tab is
  // rude. Users flip the master toggle in Settings to opt in.
  return { master: false, volume: DEFAULT_VOLUME, perEvent: {} }
}

let prefs: StoredPrefs = loadPrefs()

function save(): void {
  try {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(prefs))
  } catch { /* private mode, quota, etc. - loss is fine */ }
}

// Small pool of HTMLAudioElements so overlapping events don't cut each
// other off. Each slot plays one sound at a time.
const POOL_SIZE = 4
const audioPool: HTMLAudioElement[] = []
let poolCursor = 0

function ensurePool(): void {
  if (audioPool.length) return
  for (let i = 0; i < POOL_SIZE; i++) {
    const a = new Audio()
    a.preload = 'auto'
    audioPool.push(a)
  }
}

function slugForEvent(id: SoundEventId): string | null {
  const override = prefs.perEvent[id]
  if (override) return override.enabled ? (override.slug ?? null) : null
  const def = SOUND_EVENTS.find(e => e.id === id)?.defaultSlug ?? null
  return def
}

export function isEventEnabled(id: SoundEventId): boolean {
  const override = prefs.perEvent[id]
  if (override) return override.enabled && override.slug !== null
  return SOUND_EVENTS.find(e => e.id === id)?.defaultSlug !== null
}

// Play the sound bound to `id`. Silent no-op when master is off, the
// event is disabled, or the file 404s / decode fails. Non-blocking.
export function playSound(id: SoundEventId): void {
  if (!prefs.master) return
  const slug = slugForEvent(id)
  if (!slug) return
  ensurePool()
  const slot = audioPool[poolCursor]
  poolCursor = (poolCursor + 1) % POOL_SIZE
  slot.src = `/ui/sounds/${slug}.mp3`
  slot.volume = Math.max(0, Math.min(1, prefs.volume))
  // Autoplay is a user-gesture-gated promise on Chromium; the promise
  // rejects when the user hasn't clicked yet. Swallowed - the toast
  // still fires.
  void slot.play().catch(() => { /* silent */ })
}

// Explicit preview for the Settings page. Always plays, regardless of
// master / event flags, so the operator can hear what they're editing.
export function previewSlug(slug: string): void {
  ensurePool()
  const slot = audioPool[poolCursor]
  poolCursor = (poolCursor + 1) % POOL_SIZE
  slot.src = `/ui/sounds/${slug}.mp3`
  slot.volume = Math.max(0, Math.min(1, prefs.volume))
  void slot.play().catch(() => { /* silent */ })
}

// Preference accessors used by Settings.vue.
export function getMaster(): boolean { return prefs.master }
export function setMaster(v: boolean): void { prefs.master = v; save() }
export function getVolume(): number { return prefs.volume }
export function setVolume(v: number): void { prefs.volume = Math.max(0, Math.min(1, v)); save() }
export function getEventOverride(id: SoundEventId): { enabled: boolean; slug: string | null } {
  const override = prefs.perEvent[id]
  if (override) return override
  const def = SOUND_EVENTS.find(e => e.id === id)?.defaultSlug ?? null
  return { enabled: def !== null, slug: def }
}
export function setEventOverride(id: SoundEventId, next: { enabled: boolean; slug: string | null }): void {
  prefs.perEvent[id] = next
  save()
}
export function resetEventOverride(id: SoundEventId): void {
  delete prefs.perEvent[id]
  save()
}

// Available slugs the picker shows. Kept in sync with the shipped
// /public/sounds directory; a future backend-listed catalog can replace
// this constant.
export const AVAILABLE_SLUGS = [
  'chime-queued',
  'chime-image',
  'chime-video',
  'chime-upscale',
  'chime-convert',
  'chime-download',
  'chime-hash',
  'tone-error',
  'tone-cancel',
  'tone-loading',
  'tone-ready',
  'tone-shutdown',
] as const
