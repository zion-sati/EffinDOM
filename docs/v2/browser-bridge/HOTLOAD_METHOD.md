# EffinDom browser-bridge hotload method

This document describes the current in-page route-swap method used by the
routed `v2/fui-as/demo/**` browser shell.

## Summary

The current method is a **warm reboot**, not classic HMR.

We keep the **page** and the **cached browser-runtime assets/modules** hot, but
we recreate the live runtime/app session so each routed page starts from a
fresh memory/state boundary.

That is the current meaning of “hotload” in this repo.

## Why this exists

Full page reloads were safe, but slower than necessary because they threw away:

- the already-loaded browser shell DOM
- fetched runtime assets
- compiled `WebAssembly.Module`s
- manifest/runtime-lane selection work

The earlier app-only route swap attempt was faster, but unsafe because it kept
too much live state around.

The warm-reboot approach keeps the expensive reusable pieces hot while still
resetting the parts that must not leak across routed pages.

## The live state split

A live v2 browser page has state in three places:

1. **App wasm**
   - route-local retained tree state
   - counters, page logic, local control state
2. **Core/UI wasm**
   - retained layout and scene state
   - hit testing
   - focus and selection
   - scroll state
   - semantic buffers
   - renderer/runtime state
3. **Bridge/host JS**
   - pointer hover/capture mirrors
   - hidden DOM semantic projection
   - hidden input / `EditContext` plumbing
   - clipboard callbacks
   - resize observers
   - render-loop subscriptions
   - same-origin history/navigation handlers
   - debug mirrors such as `window.__bridgeSemanticTree`

This split is intentional because browser APIs live in JS, not inside wasm.
It also means that recreating only the app wasm does **not** produce a fully
fresh routed page session.

## What stays hot vs what gets recreated

### Kept hot

- page/document
- shell DOM
- managed history state
  - current routed entries now keep only a tiny UI snapshot pointer in
    `history.state`; the actual persisted payload lives in browser IndexedDB
- fetched asset bytes
- compiled `WebAssembly.Module`s
- manifest/runtime-lane selection results

### Recreated fresh on each internal route swap

- app wasm instance
- Core wasm instance
- UI wasm instance
- bridge interaction state
- hidden DOM semantic projection
- event-handler registrations
- render loop wiring
- pointer hover/capture session state
- editable/clipboard session plumbing

## Current implementation map

The current route-swap path is:

1. `v2/fui-as/demo/harness.ts`
   - `navigateToRoute(...)` updates managed history, marks the route as loading,
     detects internal route swaps, and calls `controller.recreateRuntime()`.
2. `v2/fui-as/browser/src/common-harness.ts`
   - `controller.recreateRuntime()` drops the current routed app session and
     asks the browser bridge for a fresh runtime session.
   - `controller.loadApp(...)` then instantiates the target route wasm on that
     fresh runtime.
3. `v2/browser-bridge/src/bridge.ts`
   - `recreateRuntime()` queues a new bridge runtime boot while reusing the
     prepared asset/module promises.
4. `v2/browser-bridge/src/bridge/init.ts`
   - `createBridgeSession(...)` builds one fresh bridge session: renderer,
     event handlers, render loop, semantic projector, and default fonts.

## Warm-reboot lifecycle

For an internal routed swap:

1. keep the page and browser shell in place
2. update managed history without doing a full page navigation
   - same-document route entries may refresh their `history.state.uiSnapshotId`
     before navigation so Back/Forward can point at persisted UI snapshots
   - fresh entry / reload loads stay fresh; persisted restore is reserved for
     Back/Forward history entries and duplicated-tab clones
3. tear down the old bridge session
4. create fresh Core/UI wasm instances
5. create a fresh routed app wasm instance
6. bind the new app instance to the fresh bridge runtime session
7. mark the route ready again without showing the full loading overlay

The result is:

- **fast** because the expensive asset/module work is reused
- **safe** because page-local runtime state is recreated instead of shared

## Guardrails

Two guardrails matter for this design:

1. **Do not reload generated Emscripten JS into global scope twice.**
   - The bridge runs the generated core bootstrap in isolated function scope so
     warm reboots do not redeclare globals such as `EmscriptenEH`.
2. **Do not resolve bridge-owned assets from the current document URL.**
   - Warm reboots can happen after same-document history changes, so
     bridge-owned assets such as default fonts must resolve from the loaded
     `bridge.js` bundle location, not from the active route URL.

## What this method is not

This is **not**:

- browser-module HMR
- app-state-preserving route swap
- “reuse the old wasm memory and just point it at another page”

We explicitly do **not** want routed pages to retain live app/runtime state
across internal route changes.
