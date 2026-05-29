# v2 mobile support gaps and native-feel investigation

## Purpose

This note captures the current gaps between the existing v2 retained browser stack and a **native-feeling mobile experience**.

The goal is not just "make touch scrolling work." The goal is to understand what the current stack already does, what is still desktop-shaped, and what needs explicit design before we can honestly say the mobile experience feels native.

## Scope audited

This investigation looked across:

- `v2/browser-bridge/src/bridge/events.ts`
- `v2/browser-bridge/src/bridge/interaction.ts`
- `v2/fui-as/browser/src/common-harness.ts`
- `v2/ui/src/UiRuntimeInput.cpp`
- representative FUI-AS controls such as `Button`, `NavLink`, and `ContextMenu`
- current docs and tests around scrolling and input

## Executive summary

The current stack is still fundamentally **desktop-first**.

There is already a strong retained foundation:

- a unified pointer event bridge,
- retained scroll views with velocity/friction support,
- focus and keyboard routing,
- hidden-input / `EditContext` text plumbing,
- a semantic projection layer,
- and now branch-local touch-scroll / fling plumbing in the browser bridge and UI runtime.

But that is only a start. A native-feeling mobile experience still needs design and implementation work in at least these areas:

1. **Touch gesture arbitration**
2. **Viewport / on-screen keyboard / safe-area behavior**
3. **Mobile text editing and selection UX**
4. **Desktop-only affordance cleanup**
5. **Accessibility validation on mobile screen readers**
6. **A real mobile test matrix**

In other words: **touch scrolling is necessary, but nowhere near sufficient**.

## Recent Phase 1 progress

Two desktop-shaped behaviors were removed from the current mobile/coarse-pointer path:

- coarse-pointer mode now disables focus-driven snap-to-view, so keyboard focus changes do not auto-scroll controls into view on mobile-style hosts,
- touch scrolling can now apply horizontal and vertical deltas independently, keep active-touch axes out of the fling/momentum path while the finger is still down, only unlock the secondary axis after extra movement, and tolerate a small near-edge touch slop around explicit scroll-proxy surfaces so real finger starts do not fall off a 1-pixel scroll-target cliff, letting diagonal gestures drive nested horizontal + vertical scrollers together without the earlier curved-gesture drift or tiny-jitter cross-axis scroll,
- coarse-pointer hosts now also ship a basic browser-bridge pull-to-refresh affordance: a host-side floating refresh chip appears above the canvas, rotates/fades in as the user pulls down from the top while the active vertical scroll target is already at offset zero, and triggers a real page reload once the drag passes the configured threshold.

This closes one important native-feel gap, but it does **not** change the broader assessment below: mobile still needs dedicated viewport, text-editing, accessibility, and test-matrix work.

## What the stack already has

### 1. A real retained pointer pipeline

The browser bridge already routes pointer events into Tier 2 and keeps a retained notion of hover, focus, pointer capture, and drag state.

Evidence:

- `v2/browser-bridge/src/bridge/events.ts`
- `v2/ui/src/UiRuntimeInput.cpp`

### 2. Retained scroll state and momentum support

Tier 2 scroll views already have:

- retained offsets,
- per-axis enable flags,
- velocity fields,
- friction-based decay during commit frames.

Evidence:

- `v2/ui/src/UiTypes.h`
- `v2/ui/src/UiRuntime.cpp`
- `v2/ui/src/UiRuntimeInput.cpp`

This is important: mobile fling should build on this retained momentum path, not invent a second browser-only physics system.

### 3. Text-input plumbing above a canvas app

The browser bridge already maintains:

- a hidden off-screen `<input>`,
- optional `EditContext`,
- focused text state syncing,
- clipboard callbacks,
- retained selection change notifications.

Evidence:

- `v2/browser-bridge/src/bridge/interaction.ts`

That means mobile text support is **not zero**, but it is not yet mobile-first.

### 4. A semantic layer exists

The runtime already projects semantics and FUI-AS sets semantic roles on many controls.

That gives us a base for mobile accessibility work, but it does **not** prove the mobile experience is already good.

## Major gaps

## 1. Gesture handling is still too simple for mobile

### Current state

The canvas explicitly sets:

```ts
canvas.style.touchAction = 'none';
```

in `v2/browser-bridge/src/bridge/events.ts`.

That means the browser's native touch panning/zoom behavior is intentionally disabled and the app must implement the full gesture story itself.

The current bridge tracks only a single active touch gesture shape:

- one `pointerId`,
- one start point,
- one last point,
- one boolean `scrolling`.

Evidence:

- `v2/browser-bridge/src/bridge/events.ts`

### Gaps

We still do **not** have a complete mobile gesture model for:

- tap-vs-scroll slop policy beyond a very small threshold,
- axis locking,
- nested scroll negotiation,
- parent/child scroll handoff,
- overscroll behavior,
- pinch-zoom policy,
- double-tap zoom policy,
- multi-touch ownership,
- long-press gesture classification,
- cancel/recover rules when a press turns into a scroll.

### Why it matters

On mobile, "native feel" comes from gesture arbitration as much as from rendering.

Without a deliberate gesture model, the app will feel:

- too eager to click,
- too eager to scroll,
- confused inside nested scrollers,
- and inconsistent across iOS Safari vs Android Chrome.

## 2. Text selection and editing are still desktop-shaped

### Current state

The retained selection system is pointer-drag driven:

- selection starts from pointer down,
- extends via pointer move,
- finalizes on pointer up,
- and cross-selection is also drag-oriented.

Evidence:

- `v2/ui/src/UiRuntimeInput.cpp`

The browser bridge uses an off-screen hidden input:

```ts
input.style.position = 'fixed';
input.style.left = '-9999px';
input.style.top = '0';
input.style.width = '1px';
input.style.height = '1px';
input.style.opacity = '0';
```

Evidence:

- `v2/browser-bridge/src/bridge/interaction.ts`

The hidden input and any future hidden DOM selection mirror should be treated as **bridge plumbing**, not the stable selection contract. The right long-term boundary is a read-only selection snapshot on `window.__OPEN_CANVAS_API__` so the bridge, SDK, accessibility tools, desktop selection popups, and mobile host integrations can all consume the same engine-owned selection data independently.

### Gaps

We do **not** yet have mobile-native text affordances such as:

- long-press to place caret / select word,
- draggable selection handles,
- caret handle dragging,
- selection magnifier / loupe behavior,
- touch-first copy/paste callout strategy,
- long-press context actions,
- soft-keyboard action mapping (`Done`, `Go`, `Next`, `Search`, etc.),
- mobile-friendly multiline editing behaviors,
- explicit design for when retained editing should defer to native browser editing affordances.

### Why it matters

Canvas text editing that works on desktop can still feel completely wrong on mobile if:

- there are no handles,
- the keyboard covers the caret,
- selection depends on precision dragging,
- or the user cannot rely on familiar long-press behavior.

## 3. Viewport and on-screen keyboard handling are incomplete

### Current state

The FUI-AS browser harness listens to:

- `window.resize`,
- dark mode changes,
- `popstate`,
- blur / visibility changes.

Evidence:

- `v2/fui-as/browser/src/common-harness.ts`

What is notably missing is any `visualViewport` integration.

### Gaps

We currently have no explicit mobile design for:

- soft-keyboard occlusion,
- `visualViewport` height/offset changes,
- focused-control reveal relative to keyboard insets,
- safe-area insets,
- bottom home-indicator avoidance,
- top notch/status-bar avoidance,
- orientation change policies beyond generic resize,
- viewport unit stability on mobile browsers.

### Why it matters

This is one of the biggest mobile gaps.

A retained canvas app can look correct until the keyboard opens, then immediately fail if:

- the focused field is hidden,
- the canvas size is stale,
- or the app still thinks the full viewport is visible.

## 4. Desktop-only affordances are still prominent

### Current state

Several controls and browser interactions are explicitly desktop-flavored:

- `ContextMenu` closes on `Escape`
- the browser bridge uses the DOM `contextmenu` event
- `NavLink` shows hover-driven URL preview behavior
- `Button` and other controls still use hover state as a first-class visual
- docs still describe `ScrollBar` in terms of wheel, track clicks, and thumb drag

Evidence:

- `v2/browser-bridge/src/bridge/events.ts`
- `v2/fui-as/src/controls/ContextMenu.ts`
- `v2/fui-as/src/controls/NavLink.ts`
- `v2/fui-as/src/controls/Button.ts`
- `docs/v2/fui-as/QUICKSTART.md`

### Gaps

We need a deliberate mobile alternative for:

- right-click-only actions,
- hover-only hints,
- pointer-cursor assumptions,
- keyboard-only escape hatches,
- tiny drag handles / precise scrollbar usage,
- URL preview behavior that depends on hover.

### Why it matters

If mobile gets the desktop UX with touch substituted in, it will feel wrong even if it technically works.

Native-feeling mobile often needs:

- action sheets instead of context menus,
- always-visible cues instead of hover cues,
- bigger targets,
- and simpler direct manipulation patterns.

## 5. Scroll behavior still needs mobile-native design, not just plumbing

### Current state

The retained runtime supports:

- scroll offset mutation,
- drag-based scrolling,
- velocity seeding,
- friction decay.

Branch-local work now also adds dedicated touch-scroll begin/update/end entry points.

Evidence:

- `v2/ui/src/UiRuntime.cpp`
- `v2/ui/src/UiRuntimeInput.cpp`
- `v2/browser-bridge/src/bridge/events.ts`

### Remaining gaps

Even with touch drag + fling, we still need design for:

- nested scroll containers,
- scroll chaining,
- edge resistance / rubber-banding policy,
- momentum cancellation on retouch,
- horizontal vs vertical gesture arbitration,
- selection-vs-scroll arbitration in editable text,
- scrollbar visibility rules on touch devices,
- whether scrollbars should be thinner, transient, or touch-targeted on mobile.

### Why it matters

Good scroll physics alone do not produce native feel if the app still lacks the rest of the mobile scroll model.

## 6. Mobile accessibility is unverified

### Current state

There is a semantic projection layer and semantic roles exist across the retained stack.

Evidence:

- `v2/ui/src/UiRuntime.cpp`
- semantic plumbing across FUI-AS and browser bridge

### Gaps

We do not yet have evidence that the current model is good with:

- VoiceOver touch exploration,
- TalkBack focus movement,
- mobile rotor / reading navigation patterns,
- soft keyboard focus transitions,
- touch screen reader gesture conflicts with the canvas.

### Why it matters

Desktop accessibility success does not automatically transfer to mobile assistive tech. Touch exploration changes the interaction model substantially.

## 7. Test coverage is still too thin for mobile confidence

### Current state

Most of the mature test surface is desktop-oriented:

- wheel tests,
- pointer drag tests,
- keyboard tests,
- desktop Playwright flows.

Touch coverage is still minimal.

Evidence:

- `v2/browser-bridge/tests/smoke.spec.ts`
- `v2/fui-as/tests/**`

### Gaps

We still need:

- mobile browser emulation scenarios,
- iOS Safari real-device validation,
- Android Chrome real-device validation,
- soft keyboard / viewport change tests,
- long-press / selection / context-action tests,
- nested scroller tests,
- high-frequency gesture performance tests.

### Why it matters

Without a mobile test matrix, it is too easy to "fix" one touch bug while breaking another browser/device combination.

## 8. The current public model still reads as desktop-first

The docs and APIs still mostly emphasize:

- wheel,
- pointer enter/leave,
- track clicks,
- thumb dragging,
- keyboard focus,
- desktop-style dialogs and context menus.

Even where this is technically fine, it means we do not yet have a clearly documented **mobile interaction contract**.

That missing contract is itself a gap.

For selection specifically, the missing contract should be an **Open Canvas Selection API**, not a dependence on hidden DOM bridge internals.

## Design implications

## 1. We need a mobile interaction model, not one-off fixes

The next step should not be "keep patching touch cases."

We need a written design that defines:

- what a tap means,
- what a drag means,
- when a drag becomes a scroll,
- when text selection wins over scroll,
- how long-press works,
- how nested scrollers arbitrate,
- and which browser-native behaviors we intentionally keep vs replace.

## 2. Viewport + keyboard behavior needs its own foundation slice

Mobile viewport handling should be treated as a first-class platform subsystem, likely covering:

- `visualViewport`,
- keyboard insets,
- safe-area insets,
- focused-control reveal,
- and orientation changes.

This should not be buried inside control-specific fixes.

## 3. Text editing likely needs a separate mobile design pass

The existing retained editing model is strong, but mobile-native editing likely needs additional UI semantics and possibly host cooperation that desktop does not need.

This may include deciding where we want:

- retained custom behavior,
- browser-native affordances,
- or a hybrid approach.

It should also include defining a stable Open Canvas selection contract so custom SDK lollipops and bridge-native selection UI can evolve independently on top of the same data surface.

## 4. Some desktop UX should stay desktop-only

We should explicitly decide which surfaces get mobile-specific alternatives, for example:

- context menu -> action sheet / long-press menu,
- hover preview -> tap/press affordance or no equivalent,
- persistent tiny scrollbar -> transient touch scrollbar,
- Enter/Escape primary flows -> mobile action buttons and soft-keyboard actions.

## Recommended workstreams

## Workstream 1: mobile platform foundation

- add `visualViewport` support,
- model safe-area insets,
- model soft-keyboard insets,
- define focused-control reveal rules.

## Workstream 2: gesture system

- formalize tap/drag/scroll/long-press arbitration,
- add axis locking,
- add nested scroll negotiation,
- define pinch zoom policy,
- define overscroll policy.

## Workstream 3: mobile text UX

- long-press word selection,
- caret and selection handles,
- copy/paste action surface,
- keyboard action mapping,
- editing viewport avoidance.

## Workstream 4: mobile control pass

- audit hit target sizes,
- remove hover dependencies from critical flows,
- define mobile alternatives for context menu / preview behaviors,
- revisit scrollbar behavior on touch devices.

## Workstream 5: accessibility + validation

- VoiceOver and TalkBack test passes,
- mobile browser matrix,
- real-device smoke runs,
- touch-performance profiling.

## Open design questions

1. Should the page ever use native browser scrolling outside the canvas, or should the canvas fully own touch panning?
2. Do we want pinch zoom inside the app, page zoom, both, or neither?
3. Should long-press always prioritize text selection inside editable/selectable text?
4. Should context actions on mobile be native browser callouts, custom retained surfaces, or hybrid?
5. How much of mobile text editing should stay canvas-retained vs delegated to browser-native editing surfaces?
6. What is the supported-device baseline for "native feel" — iOS Safari only, iOS + Android Chrome, tablets too?

## Bottom line

The current v2 stack is **not blocked from mobile**, but it is also **not yet designed for mobile-native feel**.

The main lesson from the audit is:

> mobile support is a platform-design problem, not just a touch-scroll bug.

Touch drag and fling are useful pieces, but they only solve one narrow part of the mobile experience. The next real milestone should be a proper mobile interaction design for the browser host, viewport model, text editing UX, and control behavior.
