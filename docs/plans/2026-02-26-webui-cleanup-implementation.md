# WebUI Cleanup Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace borders with shadows for cleaner visuals and add mobile bottom tab bar navigation.

**Architecture:** Update global CSS variables and component styles to use shadows instead of borders. Create new MobileTabBar.vue component that renders at <=640px breakpoint. Modify App.vue layout to conditionally show sidebar/tab bar.

**Tech Stack:** Vue 3, CSS custom properties, responsive media queries

---

## Task 1: Update Global CSS - Shadow Variables and Card Styles

**Files:**
- Modify: `webui/src/style.css:107-130` (card styles)

**Step 1: Add shadow variable and update card class**

In `style.css`, find the `.card` class and update it:

```css
/* Cards */
.card {
  background: var(--bg-secondary);
  border-radius: var(--border-radius);
  padding: 20px;
  margin-bottom: 16px;
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.2);
}

.card-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 16px;
  padding-bottom: 12px;
}
```

Note: Remove `border: 1px solid var(--border-color);` from `.card` and `border-bottom` from `.card-header`.

**Step 2: Verify changes in browser**

Run: `cd webui && npm run dev`
Expected: Cards should have subtle shadow instead of hard border, card headers have no bottom line.

**Step 3: Commit**

```bash
git add webui/src/style.css
git commit -m "style: replace card borders with shadows"
```

---

## Task 2: Update Global CSS - Stat Cards and Tables

**Files:**
- Modify: `webui/src/style.css:833-860` (stat cards)
- Modify: `webui/src/style.css:422-444` (tables)

**Step 1: Update stat-card styles**

```css
.stat-card {
  background: var(--bg-secondary);
  border-radius: var(--border-radius);
  padding: 16px;
  text-align: center;
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.2);
}
```

Remove `border: 1px solid var(--border-color);`

**Step 2: Simplify table borders**

```css
.table th,
.table td {
  padding: 12px;
  text-align: left;
  border-bottom: 1px solid var(--border-color);
}

.table th {
  font-weight: 600;
  color: var(--text-secondary);
  font-size: 12px;
  text-transform: uppercase;
  background: var(--bg-tertiary);
}
```

**Step 3: Commit**

```bash
git add webui/src/style.css
git commit -m "style: update stat cards and tables"
```

---

## Task 3: Update Global CSS - Modal Styles

**Files:**
- Modify: `webui/src/style.css:696-765` (modal styles)

**Step 1: Update modal-content, header, and footer**

```css
.modal-content {
  background: var(--bg-secondary);
  border-radius: var(--border-radius-lg);
  max-width: 600px;
  width: 100%;
  max-height: 90vh;
  overflow-y: auto;
  box-shadow: var(--shadow-lg);
}

.modal-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 20px;
  background: var(--bg-tertiary);
}

.modal-footer {
  display: flex;
  justify-content: flex-end;
  gap: 12px;
  padding: 16px 20px;
  background: var(--bg-tertiary);
}
```

Remove all `border` and `border-bottom/top` from these classes.

**Step 2: Commit**

```bash
git add webui/src/style.css
git commit -m "style: remove modal borders, use background differentiation"
```

---

## Task 4: Update Global CSS - Accordion Styles

**Files:**
- Modify: `webui/src/style.css:570-608` (accordion styles)

**Step 1: Update accordion styles**

```css
.accordion {
  border-radius: var(--border-radius);
  overflow: hidden;
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.15);
}

.accordion-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 14px 16px;
  background: var(--bg-tertiary);
  cursor: pointer;
  font-weight: 500;
  transition: background var(--transition-fast);
}

.accordion-content {
  padding: 16px;
  background: var(--bg-secondary);
}
```

Remove `border: 1px solid var(--border-color);` from `.accordion`.

**Step 2: Commit**

```bash
git add webui/src/style.css
git commit -m "style: update accordion to use shadow"
```

---

## Task 5: Update Global CSS - Toast Styles

**Files:**
- Modify: `webui/src/style.css:767-816` (toast styles)

**Step 1: Update toast styles**

```css
.toast {
  padding: 14px 20px;
  border-radius: var(--border-radius);
  background: var(--bg-secondary);
  box-shadow: var(--shadow-md);
  display: flex;
  align-items: center;
  gap: 12px;
  min-width: 280px;
  animation: slideIn 0.3s ease;
}
```

Remove `border: 1px solid var(--border-color);`. Keep the colored left border for status indication (those are intentional visual cues).

**Step 2: Commit**

```bash
git add webui/src/style.css
git commit -m "style: update toast shadows"
```

---

## Task 6: Create MobileTabBar Component

**Files:**
- Create: `webui/src/components/MobileTabBar.vue`

**Step 1: Create the component**

```vue
<script setup lang="ts">
import { ref, computed } from 'vue'
import { useRoute } from 'vue-router'
import { useAssistantStore } from '../stores/assistant'

const route = useRoute()
const assistantStore = useAssistantStore()
const showMoreMenu = ref(false)

const primaryTabs = [
  { path: '/dashboard', name: 'Dashboard', icon: '&#128200;' },
  { path: '/models', name: 'Models', icon: '&#128230;' },
  { path: '/generate', name: 'Generate', icon: '&#127912;' },
  { path: '/queue', name: 'Queue', icon: '&#128203;' }
]

const secondaryItems = computed(() => {
  const items = [
    { path: '/downloads', name: 'Downloads', icon: '&#11015;' },
    { path: '/upscale', name: 'Upscale', icon: '&#128269;' },
    { path: '/settings', name: 'Settings', icon: '&#9881;' }
  ]

  if (assistantStore.enabled) {
    items.splice(2, 0, { path: '/chat', name: 'Assistant', icon: '&#129302;' })
  }

  return items
})

function isActive(path: string): boolean {
  return route.path === path
}

function isMoreActive(): boolean {
  return secondaryItems.value.some(item => route.path === item.path)
}

function closeMoreMenu() {
  showMoreMenu.value = false
}
</script>

<template>
  <nav class="mobile-tab-bar">
    <router-link
      v-for="tab in primaryTabs"
      :key="tab.path"
      :to="tab.path"
      :class="['tab-item', { active: isActive(tab.path) }]"
    >
      <span class="tab-icon" v-html="tab.icon"></span>
      <span class="tab-label">{{ tab.name }}</span>
    </router-link>

    <button
      :class="['tab-item', { active: isMoreActive() }]"
      @click="showMoreMenu = !showMoreMenu"
    >
      <span class="tab-icon">&#8942;</span>
      <span class="tab-label">More</span>
    </button>

    <!-- More Menu Overlay -->
    <Teleport to="body">
      <div v-if="showMoreMenu" class="more-menu-overlay" @click="closeMoreMenu">
        <div class="more-menu" @click.stop>
          <router-link
            v-for="item in secondaryItems"
            :key="item.path"
            :to="item.path"
            :class="['more-menu-item', { active: isActive(item.path) }]"
            @click="closeMoreMenu"
          >
            <span class="more-icon" v-html="item.icon"></span>
            <span class="more-label">{{ item.name }}</span>
          </router-link>
          <a href="/output" target="_blank" class="more-menu-item" @click="closeMoreMenu">
            <span class="more-icon">&#128193;</span>
            <span class="more-label">Output Files</span>
            <span class="external-icon">&#8599;</span>
          </a>
        </div>
      </div>
    </Teleport>
  </nav>
</template>

<style scoped>
.mobile-tab-bar {
  display: none;
  position: fixed;
  bottom: 0;
  left: 0;
  right: 0;
  height: 56px;
  background: var(--bg-secondary);
  box-shadow: 0 -2px 10px rgba(0, 0, 0, 0.3);
  justify-content: space-around;
  align-items: center;
  z-index: 1000;
  padding-bottom: env(safe-area-inset-bottom, 0);
}

@media (max-width: 640px) {
  .mobile-tab-bar {
    display: flex;
  }
}

.tab-item {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 2px;
  padding: 8px 12px;
  color: var(--text-secondary);
  text-decoration: none;
  font-size: 10px;
  background: none;
  border: none;
  cursor: pointer;
  transition: color 0.15s;
  min-width: 56px;
}

.tab-item:hover,
.tab-item.active {
  color: var(--accent-primary);
}

.tab-icon {
  font-size: 20px;
  line-height: 1;
}

.tab-label {
  font-weight: 500;
}

/* More Menu */
.more-menu-overlay {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.5);
  z-index: 1001;
  display: flex;
  align-items: flex-end;
  justify-content: center;
  padding-bottom: calc(56px + env(safe-area-inset-bottom, 0));
}

.more-menu {
  background: var(--bg-secondary);
  border-radius: var(--border-radius-lg) var(--border-radius-lg) 0 0;
  width: 100%;
  max-width: 400px;
  padding: 8px 0;
  box-shadow: 0 -4px 20px rgba(0, 0, 0, 0.3);
  animation: slideUp 0.2s ease-out;
}

@keyframes slideUp {
  from {
    transform: translateY(100%);
    opacity: 0;
  }
  to {
    transform: translateY(0);
    opacity: 1;
  }
}

.more-menu-item {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 14px 20px;
  color: var(--text-primary);
  text-decoration: none;
  transition: background 0.15s;
}

.more-menu-item:hover {
  background: var(--bg-hover);
}

.more-menu-item.active {
  color: var(--accent-primary);
}

.more-icon {
  font-size: 20px;
  width: 28px;
  text-align: center;
}

.more-label {
  flex: 1;
  font-size: 14px;
  font-weight: 500;
}

.external-icon {
  color: var(--text-muted);
  font-size: 12px;
}
</style>
```

**Step 2: Commit**

```bash
git add webui/src/components/MobileTabBar.vue
git commit -m "feat: add MobileTabBar component for mobile navigation"
```

---

## Task 7: Integrate MobileTabBar into App.vue

**Files:**
- Modify: `webui/src/App.vue`

**Step 1: Import and add MobileTabBar**

Add import at top of script:
```typescript
import MobileTabBar from './components/MobileTabBar.vue'
```

**Step 2: Add component to template**

After `<ShortcutsHelpModal ref="shortcutsModalRef" />`, add:
```vue
<MobileTabBar />
```

**Step 3: Commit**

```bash
git add webui/src/App.vue
git commit -m "feat: integrate MobileTabBar into main layout"
```

---

## Task 8: Update Sidebar Responsive Behavior

**Files:**
- Modify: `webui/src/components/Sidebar.vue:110-121`

**Step 1: Update media queries**

Replace the existing `@media (max-width: 768px)` block and add mobile hiding:

```css
@media (max-width: 768px) {
  .sidebar {
    width: 60px;
  }
  .nav-label {
    display: none;
  }
  .nav-item {
    justify-content: center;
    padding: 12px;
  }
  .nav-icon {
    margin: 0;
  }
}

@media (max-width: 640px) {
  .sidebar {
    display: none;
  }
}
```

**Step 2: Remove border from sidebar**

Update `.sidebar` class:
```css
.sidebar {
  width: var(--sidebar-width);
  background: var(--bg-secondary);
  display: flex;
  flex-direction: column;
  flex-shrink: 0;
  box-shadow: 2px 0 8px rgba(0, 0, 0, 0.15);
}
```

Remove `border-right: 1px solid var(--border-color);`

**Step 3: Update sidebar-footer**

```css
.sidebar-footer {
  padding: 12px 8px;
  background: var(--bg-tertiary);
}
```

Remove `border-top: 1px solid var(--border-color);`

**Step 4: Commit**

```bash
git add webui/src/components/Sidebar.vue
git commit -m "style: update sidebar responsive behavior and remove borders"
```

---

## Task 9: Update Global CSS - Mobile Content Padding

**Files:**
- Modify: `webui/src/style.css:456-521` (responsive section)

**Step 1: Update mobile breakpoints**

Find the `@media (max-width: 480px)` section and update to 640px, add padding for tab bar:

```css
@media (max-width: 640px) {
  :root { --sidebar-width: 0px; }

  .main-content {
    padding: 16px;
    padding-bottom: calc(56px + 16px + env(safe-area-inset-bottom, 0));
  }

  .grid-4, .grid-3, .grid-2 { grid-template-columns: 1fr; }

  .page-header {
    text-align: center;
  }

  .page-title {
    font-size: 20px;
  }

  .card {
    padding: 16px;
  }

  .form-row {
    grid-template-columns: 1fr;
  }

  .modal-content {
    margin: 16px;
    max-height: calc(100vh - 32px);
  }

  .stats-grid {
    grid-template-columns: repeat(2, 1fr);
    gap: 8px;
  }

  .stat-card {
    padding: 12px;
  }

  .stat-value {
    font-size: 22px;
  }

  .btn {
    padding: 8px 16px;
    font-size: 13px;
  }

  .btn-sm {
    padding: 5px 10px;
    font-size: 11px;
  }
}

@media (max-width: 480px) {
  .main-content {
    padding: 12px;
    padding-bottom: calc(56px + 12px + env(safe-area-inset-bottom, 0));
  }

  .stats-grid {
    grid-template-columns: 1fr 1fr;
  }
}
```

**Step 2: Update 768px breakpoint**

Keep the existing tablet breakpoint but remove sidebar-width change (handled by Sidebar.vue):

```css
@media (max-width: 768px) {
  .grid-4 { grid-template-columns: repeat(2, 1fr); }
  .grid-3 { grid-template-columns: repeat(2, 1fr); }
  .main-content { padding: 16px; }
}
```

**Step 3: Commit**

```bash
git add webui/src/style.css
git commit -m "style: update responsive breakpoints for mobile tab bar"
```

---

## Task 10: Update Dashboard Component Styles

**Files:**
- Modify: `webui/src/views/Dashboard.vue:295-475`

**Step 1: Update component-item and detail-row**

Find and update these styles:

```css
.component-item {
  display: flex;
  flex-direction: column;
  gap: 4px;
  padding: 12px;
  background: var(--bg-tertiary);
  border-radius: var(--border-radius-sm);
}

.detail-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 8px 0;
}

.detail-row:not(:last-child) {
  border-bottom: 1px solid rgba(255, 255, 255, 0.05);
}
```

Remove explicit `border` from `.component-item`.
Make `.detail-row` border much more subtle.

**Step 2: Update setting-item styles**

```css
.setting-item {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 8px 12px;
  background: var(--bg-tertiary);
  border-radius: var(--border-radius-sm);
  gap: 8px;
}

.setting-item.important {
  background: var(--bg-secondary);
  box-shadow: 0 1px 4px rgba(0, 0, 0, 0.15);
}
```

Remove `border: 1px solid var(--border-color);` from `.setting-item.important`.

**Step 3: Commit**

```bash
git add webui/src/views/Dashboard.vue
git commit -m "style: remove borders from dashboard components"
```

---

## Task 11: Update Settings Component Styles

**Files:**
- Modify: `webui/src/views/Settings.vue:1044-1495`

**Step 1: Update nav-item**

```css
.nav-item {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  padding: 0.75rem 1rem;
  background: var(--bg-secondary);
  border-radius: 6px;
  color: var(--text-secondary);
  cursor: pointer;
  transition: all 0.2s;
}

.nav-item:hover {
  background: var(--bg-hover);
  color: var(--text-primary);
}
```

Remove `border: 1px solid var(--border-color);`.

**Step 2: Update settings-content**

```css
.settings-content {
  background: var(--bg-secondary);
  border-radius: 8px;
  min-height: 500px;
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.2);
}
```

Remove `border: 1px solid var(--border-color);`.

**Step 3: Update settings-card**

```css
.settings-card {
  background: var(--bg-primary);
  border-radius: 8px;
  margin-bottom: 1rem;
  overflow: hidden;
  box-shadow: 0 1px 4px rgba(0, 0, 0, 0.15);
}

.settings-card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 0.75rem 1rem;
  background: var(--bg-tertiary);
}
```

Remove borders from both.

**Step 4: Update nested-settings, connection-status, model-capabilities**

```css
.nested-settings {
  margin-top: 1rem;
  padding: 1rem;
  background: var(--bg-tertiary);
  border-radius: 6px;
}

.connection-status {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 0.75rem 1rem;
  border-radius: 8px;
  margin-bottom: 1rem;
  background: var(--bg-tertiary);
}

.model-capabilities {
  margin-top: 1rem;
  padding: 0.875rem 1rem;
  background: var(--bg-secondary);
  border-radius: 8px;
}
```

Remove all explicit borders.

**Step 5: Commit**

```bash
git add webui/src/views/Settings.vue
git commit -m "style: remove borders from settings page"
```

---

## Task 12: Update Generate Component Styles

**Files:**
- Modify: `webui/src/views/Generate.vue:1398-1760`

**Step 1: Update recommended-panel**

```css
.recommended-panel {
  background: var(--bg-tertiary);
  border-radius: var(--border-radius-sm);
  padding: 12px;
  margin-bottom: 16px;
}
```

Remove `border: 1px solid var(--border-color);`.

**Step 2: Update copy-menu**

```css
.copy-menu {
  position: absolute;
  top: 100%;
  right: 0;
  margin-top: 4px;
  background: var(--bg-secondary);
  border-radius: var(--border-radius-sm);
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.3);
  z-index: 100;
  min-width: 150px;
  overflow: hidden;
}

.copy-menu-item:not(:last-child) {
  border-bottom: 1px solid rgba(255, 255, 255, 0.05);
}
```

Remove the outer border.

**Step 3: Update tiling-options**

```css
.tiling-options {
  margin-top: 12px;
  padding: 12px;
  background: var(--bg-tertiary);
  border-radius: var(--border-radius-sm);
}
```

Remove `border: 1px solid var(--border-color);`.

**Step 4: Commit**

```bash
git add webui/src/views/Generate.vue
git commit -m "style: remove borders from generate page"
```

---

## Task 13: Update Models Component Styles

**Files:**
- Modify: `webui/src/views/Models.vue:301-424`

**Step 1: Update model-card**

```css
.model-card {
  background: var(--bg-secondary);
  border-radius: var(--border-radius);
  padding: 16px;
  transition: all var(--transition-fast);
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.15);
}

.model-card:hover {
  box-shadow: 0 4px 16px rgba(0, 0, 0, 0.25);
  transform: translateY(-2px);
}

.model-card.loaded {
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.15), 0 0 0 2px var(--accent-success);
}
```

Remove `border: 1px solid var(--border-color);`. Use colored shadow for loaded state.

**Step 2: Update accordion in modal**

```css
.accordion {
  border-radius: var(--border-radius);
  overflow: hidden;
  box-shadow: 0 1px 4px rgba(0, 0, 0, 0.15);
}
```

Remove `border: 1px solid var(--border-color);`.

**Step 3: Commit**

```bash
git add webui/src/views/Models.vue
git commit -m "style: remove borders from models page"
```

---

## Task 14: Update StatusBar Component Styles

**Files:**
- Modify: `webui/src/components/StatusBar.vue:151-397`

**Step 1: Update status-bar**

```css
.status-bar {
  position: relative;
  height: var(--header-height);
  background: var(--bg-secondary);
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 20px;
  flex-shrink: 0;
  overflow: hidden;
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.15);
}
```

Remove `border-bottom: 1px solid var(--border-color);`.

**Step 2: Update has-progress state**

```css
.status-bar.has-progress::after {
  content: '';
  position: absolute;
  bottom: 0;
  left: 0;
  height: 2px;
  width: 100%;
  background: linear-gradient(90deg, var(--accent-primary), var(--accent-purple));
  animation: progress-glow 2s ease-in-out infinite;
}
```

Remove `border-bottom-color: var(--accent-primary);` from `.status-bar.has-progress`.

**Step 3: Update button borders**

```css
.notification-toggle {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 32px;
  height: 32px;
  padding: 0;
  background: transparent;
  border: none;
  border-radius: var(--border-radius-sm);
  color: var(--text-muted);
  cursor: pointer;
  transition: all 0.2s ease;
}

.notification-toggle:hover {
  background: var(--bg-tertiary);
  color: var(--text-primary);
}

.reconnect-btn {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 24px;
  height: 24px;
  padding: 0;
  background: transparent;
  border: none;
  border-radius: var(--border-radius-sm);
  color: var(--text-muted);
  cursor: pointer;
  font-size: 12px;
  transition: all 0.2s ease;
}

.reconnect-btn:hover {
  background: var(--bg-tertiary);
  color: var(--accent-primary);
}
```

Remove borders from icon buttons.

**Step 4: Commit**

```bash
git add webui/src/components/StatusBar.vue
git commit -m "style: remove borders from status bar"
```

---

## Task 15: Final Testing and Cleanup

**Step 1: Test all breakpoints**

Run: `cd webui && npm run dev`

Test in browser at these widths:
- 1920px (desktop)
- 1024px (tablet landscape)
- 768px (tablet portrait - icon sidebar)
- 640px (mobile - bottom tab bar appears)
- 375px (phone)
- 320px (small phone)

**Step 2: Test "More" menu**

- Tap "More" on mobile
- Verify all items appear
- Verify external link has icon
- Verify menu closes on item click
- Verify menu closes on overlay click

**Step 3: Final commit**

```bash
git add -A
git commit -m "feat: complete WebUI cleanup with shadow-based design and mobile navigation"
```

---

## Summary

| Task | Description | Files |
|------|-------------|-------|
| 1 | Card shadows | style.css |
| 2 | Stat cards & tables | style.css |
| 3 | Modal styles | style.css |
| 4 | Accordion styles | style.css |
| 5 | Toast styles | style.css |
| 6 | MobileTabBar component | MobileTabBar.vue (new) |
| 7 | Integrate tab bar | App.vue |
| 8 | Sidebar responsive | Sidebar.vue |
| 9 | Mobile content padding | style.css |
| 10 | Dashboard cleanup | Dashboard.vue |
| 11 | Settings cleanup | Settings.vue |
| 12 | Generate cleanup | Generate.vue |
| 13 | Models cleanup | Models.vue |
| 14 | StatusBar cleanup | StatusBar.vue |
| 15 | Testing | - |
