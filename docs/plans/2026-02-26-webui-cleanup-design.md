# WebUI Cleanup Design: Shadow-Based Depth + Mobile Navigation

**Date:** 2026-02-26
**Status:** Approved

## Overview

Enhance the WebUI with a cleaner visual design using fewer borders and add mobile navigation support via a bottom tab bar.

## Goals

1. Reduce visual clutter by replacing borders with shadows and background differentiation
2. Fix mobile navigation (currently broken at <=480px where sidebar disappears)
3. Ensure consistent experience across all screen sizes

## Design Approach: Shadow-Based Depth

### Philosophy

Replace explicit borders with:
- Subtle drop shadows for elevation/depth
- Background color variations for section separation
- Spacing and typography for hierarchy

This approach is used by modern UIs (Discord, GitHub, VS Code) and provides a cleaner, more cohesive look.

---

## Component Changes

### Cards (`.card`, `.settings-card`, `.model-card`, `.stat-card`)

**Before:**
```css
border: 1px solid var(--border-color);
```

**After:**
```css
border: none;
box-shadow: 0 2px 8px rgba(0, 0, 0, 0.15);
```

### Card Headers (`.card-header`, `.settings-card-header`)

- Remove `border-bottom`
- Keep darker background (`var(--bg-tertiary)`) for visual separation

### Nested Components

| Component | Change |
|-----------|--------|
| `.component-item` | Remove border, keep background |
| `.setting-item` | Remove border, keep background |
| `.detail-row` | Remove `border-bottom`, use spacing |
| `.recommended-panel` | Remove border, add subtle shadow |
| `.tiling-options` | Remove border, keep background |

### Modals (`.modal-content`)

- Remove border (shadow already provides separation)
- Remove `.modal-header` border-bottom
- Remove `.modal-footer` border-top

### Accordions (`.accordion`)

- Remove outer border
- Keep header background differentiation

### Tables

- Keep only horizontal row separators
- Enhanced hover states with background change

### Form Inputs

- Keep borders (necessary for usability)
- Reduce border opacity slightly on unfocused state

---

## Responsive Breakpoints

### Desktop (>1024px)
- Full sidebar with labels (220px width)
- All features visible

### Tablet Landscape (769px - 1024px)
- Sidebar with labels (200px width)
- Grid layouts adapt (4-col → 2-col)

### Tablet Portrait (641px - 768px)
- Collapsed sidebar with icons only (60px width)
- Navigation labels hidden
- 2-column grids collapse to 1-column

### Mobile (<=640px)
- Sidebar completely hidden
- Bottom tab bar visible (56px height)
- Main content gets `padding-bottom: 64px` (tab bar + safe area)
- Single column layouts
- Increased touch targets

### Small Mobile (<=480px)
- Same as mobile
- Further reduced padding for more content space
- Smaller font sizes where appropriate

---

## Mobile Bottom Tab Bar

### Structure

```
┌─────────────────────────────────────────┐
│  [Dashboard] [Models] [Generate] [Queue] [More] │
└─────────────────────────────────────────┘
```

### Primary Tabs (always visible)
1. Dashboard (chart icon)
2. Models (box icon)
3. Generate (paint icon)
4. Queue (list icon)
5. More... (menu icon)

### "More" Menu Items
- Downloads
- Upscale
- Chat/Assistant (if enabled)
- Settings
- Output Files (external link)

### Tab Bar Styling

```css
.mobile-tab-bar {
  position: fixed;
  bottom: 0;
  left: 0;
  right: 0;
  height: 56px;
  background: var(--bg-secondary);
  box-shadow: 0 -2px 10px rgba(0, 0, 0, 0.3);
  display: none; /* shown via media query */
  justify-content: space-around;
  align-items: center;
  z-index: 1000;
  padding-bottom: env(safe-area-inset-bottom, 0);
}

@media (max-width: 640px) {
  .mobile-tab-bar { display: flex; }
  .sidebar { display: none; }
  .main-content { padding-bottom: calc(56px + env(safe-area-inset-bottom, 8px)); }
}
```

### Tab Item Styling

```css
.tab-item {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 2px;
  padding: 8px 12px;
  color: var(--text-secondary);
  text-decoration: none;
  font-size: 10px;
  transition: color 0.15s;
}

.tab-item.active {
  color: var(--accent-primary);
}

.tab-icon {
  font-size: 20px;
}
```

### "More" Menu Behavior

- Tapping "More" shows a slide-up sheet/popover
- Sheet contains remaining navigation items
- Tap outside or on item to dismiss
- Smooth animation (slide up from bottom)

---

## Files to Modify

1. `webui/src/style.css` - Global border/shadow changes
2. `webui/src/App.vue` - Add MobileTabBar component
3. `webui/src/components/Sidebar.vue` - Hide on mobile
4. `webui/src/components/MobileTabBar.vue` - New component
5. `webui/src/views/Dashboard.vue` - Component-specific cleanup
6. `webui/src/views/Settings.vue` - Card styling cleanup
7. `webui/src/views/Generate.vue` - Card styling cleanup
8. `webui/src/views/Models.vue` - Card styling cleanup

---

## Testing Checklist

- [ ] Desktop (1920x1080) - Full sidebar, shadows visible
- [ ] Tablet landscape (1024x768) - Sidebar with labels
- [ ] Tablet portrait (768x1024) - Icon-only sidebar
- [ ] Mobile landscape (640x360) - Bottom tab bar
- [ ] Mobile portrait (375x667) - Bottom tab bar, "More" menu works
- [ ] Small mobile (320x568) - All content accessible
- [ ] Dark theme looks cohesive
- [ ] All navigation links work
- [ ] "More" menu opens/closes correctly
