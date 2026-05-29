# Renderer Backend Fallback & Device-Lost Recovery

This document explains the design decisions, failure modes, and implementation
details behind the three-tier renderer fallback ladder in the v2 browser bridge.
It is intentionally written as an experience report — capturing the _why_ and
the _pitfalls_, not just the _what_.

---

## Why a fallback ladder exists

The bridge targets every modern browser. WebGPU is the preferred backend (lowest
latency, best rendering quality), but as of 2025–2026 it is unavailable or
broken in several common environments:

| Environment | Situation |
|---|---|
| **Chromium on Ubuntu (X11/Wayland without hardware acceleration)** | WebGPU surface creation fails at runtime even though the API exists in `navigator`. The adapter is null or the device drops immediately. |
| **Firefox (all platforms)** | WebGPU is behind a flag and disabled by default. |
| **Safari ≤ 17** | No WebGPU at all. |
| **Headless CI / Playwright workers** | Usually no GPU, so WebGPU and often WebGL2 both fail. |

Without a fallback the app is a blank white canvas in these environments. The
fallback ladder makes the bridge work everywhere by trying cheaper backends in
order:

```
WebGPU  →  WebGL2  →  CPU software rasterizer
```

Tier 2 (the UI layout and command-buffer engine) is completely unaffected — it
produces exactly the same command buffer regardless of which backend the bridge
selects. Only the final _present_ step differs.

---

## The three backends

### WebGPU (preferred)

Initialised by calling `_ed_init(w, h, dpr)` in the Core WASM. Uses Dawn's
WebGPU backend via Skia's `GrDirectContext`. The Core creates a swapchain tied
to the canvas passed via `Module.canvas` before instantiation.

**Failure modes:**
- `navigator.gpu` is `undefined` (browser doesn't support WebGPU).
- `requestAdapter()` resolves to `null` (no adapter available).
- `requestDevice()` throws or the device is immediately lost.
- `ed_init` throws synchronously (Emscripten trap, e.g., Dawn assertion failure).

Because WebGPU init is partially async inside the WASM (Dawn spins up a device
asynchronously), the bridge polls `_ed_get_backend_type()` after a short delay
rather than treating `_ed_init` returning without throwing as a success signal.
See `waitForWebGpuInit` in `bridge.ts`.

### WebGL2 (first fallback)

Initialised by `_ed_init_webgl(w, h, dpr)`. Uses Skia's GL backend with
Emscripten's `EMSCRIPTEN_WEBGL_CONTEXT_HANDLE`. The same canvas element is
used; Emscripten creates a WebGL2 context internally.

**Key constraint:** Once a WebGL2 context is created on a canvas, that canvas
is permanently locked to WebGL2 — you cannot later get a `'2d'` context on it,
and vice versa. This is why the software path uses a _separate overlay canvas_
(see below).

### CPU software rasterizer (last resort)

Initialised by `_ed_init_sw(w, h, dpr)`. Uses Skia's `SkSurface::MakeRaster`
(pure CPU rasterisation). The Core allocates a flat `std::vector<uint8_t>` of
`width × height × 4` bytes (`RGBA8888`) and renders into it each frame.
The bridge reads the pixel buffer back via `_ed_get_sw_framebuffer()` (returns
a pointer into WASM linear memory) and blits it to the overlay canvas using
`ctx.putImageData`.

**Performance:** CPU rasterisation is orders of magnitude slower than GPU
backends. It is deliberately logged with `console.error` to make sure operators
notice.

---

## Boot-time backend selection

`initRenderer` in `bridge.ts` tries backends in order:

```
WebGPU → WebGL2 → CPU
```

Each attempt is wrapped in a `try/catch`. If an attempt throws, or if
`_ed_get_backend_type()` returns `NONE` after the call (meaning the backend
initialised but immediately fell back inside the Core), the bridge moves to
the next backend. If all three fail, the bridge throws a fatal error and
sets `window.__bridgeError`.

The selected backend is stored in `BridgeRuntime.activeBackend` (as an
`EdBackendType` constant) and mirrored as the human-readable string
`activeRenderer` on `window.__bridgeLoaderInfo`.

---

## The software overlay canvas

GPU contexts (WebGL2 and WebGPU) lock the `#fui-canvas` canvas to their API. You
cannot call `canvas.getContext('2d')` on a canvas that already has a GPU
context — it returns `null`. Conversely, if the bridge tried WebGPU, failed,
and then called `canvas.getContext('2d')` for software rendering, the call
would fail because the GPU attempt already claimed the canvas.

The software renderer solves this with an **overlay canvas**:

1. A new `<canvas>` element is created and appended to the parent of `#fui-canvas`.
2. It is positioned absolutely over `#fui-canvas` via CSS (`position: absolute`,
   `pointer-events: none`, `z-index: 1`).
3. Its parent is forced to `position: relative` if it isn't already.
4. It carries the attribute `data-effindom-software-overlay="true"` so tests
   can find it.
5. A `CanvasRenderingContext2D` is obtained once at creation time and kept for
   the module lifetime.

The overlay stays `display: none` until `presentSoftwareFrame` has a valid
framebuffer pointer, preventing a blank overlay from covering the page.

The `readScenePixel` helper in tests checks for the overlay first and reads
from its own `getImageData` directly (avoiding a `toDataURL → Image → drawImage`
roundtrip that is susceptible to canvas-tainting).

---

## Device-lost recovery

After init, the WebGPU device can be lost at any time (GPU driver crash, laptop
sleep/wake, VRAM exhaustion, Thunderbolt GPU disconnect). The Core polls device
state each frame via `wgpuDeviceTick` and sets an internal `DeviceState::Lost`
flag. The bridge queries this via `_ed_get_device_state()` every RAF.

On detection of `EdDeviceState.LOST`:

1. The bridge cancels queued frames (a `recoveryPromise` flag suppresses the
   render loop body).
2. `recoverAfterLoss` runs:
   - For WebGPU: up to `DEVICE_RECOVERY_RETRIES` (3) calls to
     `_ed_recover_device()` spaced `DEVICE_RECOVERY_RETRY_DELAY_MS` (333 ms)
     apart. Dawn attempts to re-create the device internally.
   - On each attempt the bridge polls `waitForWebGpuInit` to see if the backend
     came back.
   - If recovery succeeds: re-register fonts (they were wiped with the old
     device), then call `commitFrame()` to re-flush the UI command buffer.
   - If all retries fail: fall through to the next backend (`WebGL2`, then
     `CPU`), re-initialise from scratch.
3. `activeBackend` and `window.__bridgeLoaderInfo.activeRenderer` are updated to
   reflect the new backend.

Tier 2's UI tree and interaction state are untouched throughout — only the
rasteriser changes. After recovery, the scene reappears exactly as it was.

---

## The stale HEAPU8 problem

This section documents a subtle but severe bug that caused the CPU fallback
test to silently produce blank pixels. It is preserved here because the same
trap will re-appear whenever Emscripten WASM memory grows.

### Background: how Emscripten manages heap views

Emscripten represents WASM linear memory as a set of typed `ArrayBuffer` views
(`HEAPU8`, `HEAPU32`, etc.). When the WASM instance grows its memory (via
`memory.grow`), the underlying `ArrayBuffer` is replaced with a larger one.
All existing `Uint8Array`/`Uint32Array` views over the old buffer become
**detached** — their `buffer.byteLength` becomes 0 and any access throws
`TypeError: Cannot perform Construct on a detached ArrayBuffer`.

Emscripten handles this inside a closure-local `updateMemoryViews()` function
that reassigns the closure-scoped `var HEAPU8`. In non-module (legacy global)
builds it also writes through to `window.HEAPU8`. What it does **not** do is
update `Module['HEAPU8']` — that property is assigned once at init time and
never touched again.

### How the bug manifested

The CPU path is the first place WASM memory growth is reliably triggered during
tests. `ed_init_sw` allocates a `std::vector<uint8_t>` of
`320 × 220 × 4 = 281,600` bytes for the pixel buffer. This pushed the WASM
heap over a 16 MB boundary, triggering `memory.grow`. After growth:

- Emscripten's closure `var HEAPU8` pointed to the new (valid) buffer.
- `window.HEAPU8` pointed to the new buffer.
- `core.HEAPU8` (`Module['HEAPU8']`) still pointed to the **old, detached** buffer.

The bridge called `core.HEAPU8.subarray(offset, offset + byteLength)` in
`presentSoftwareFrame`. `Uint8Array.prototype.subarray` internally constructs a
new `Uint8Array` over the source buffer — it called `[[Construct]]` on the
detached buffer and V8 threw `TypeError: Cannot perform Construct on a detached ArrayBuffer`.

The RAF loop was killed silently. `activeRenderer` was already `'cpu'` (set
before the crash), so the test saw the correct string but read all-zero pixels
because the overlay never received a `putImageData` call.

### Initial fix: read from window.HEAPU8 each frame

The first fix read `window.HEAPU8` (which Emscripten kept current) at the top
of every RAF frame and wrote it back to `module.HEAPU8`:

```ts
module.refreshHeapViews = () => {
  if (typeof HEAPU8 !== 'undefined') {
    module.HEAPU8 = HEAPU8; // window.HEAPU8
  }
};
```

This worked but was fragile: it depended on Emscripten's undocumented behaviour
of promoting `var HEAPU8` to a global in non-module script context.

### Robust fix: use WebAssembly.Memory directly

The canonical solution uses `WebAssembly.Memory.buffer`, which is always the
**current** live `ArrayBuffer` (it changes object identity on growth but is
never detached). Emscripten exposes the `WebAssembly.Memory` instance as
`Module["memory"]` by the time `onRuntimeInitialized` fires:

```js
// Inside the generated Emscripten bundle:
Module["memory"] = wasmMemory = wasmExports["<mangled-export-name>"];
```

The bridge captures this in `onRuntimeInitialized` (Core) and after the module
factory resolves (UI):

```ts
const emMemory = (module as unknown as Record<string, unknown>)['memory'];
if (emMemory instanceof WebAssembly.Memory) {
  module.wasmMemory = emMemory;
}
```

`refreshHeapViews` is then:

```ts
refreshHeapViews: () => {
  if (module.wasmMemory !== undefined) {
    const buffer = module.wasmMemory.buffer;
    module.HEAPU8  = new Uint8Array(buffer);
    module.HEAPU32 = new Uint32Array(buffer);
  }
}
```

The property name is `"memory"`, not `"wasmMemory"` — the Emscripten output
reveals this directly:
```
Module["memory"] = wasmMemory = wasmExports["he"];
```

A `window.HEAPU8` fallback is retained for older Emscripten builds that may
not expose the memory on the module object.

### Where refreshHeapViews must be called

| Call site | Reason |
|---|---|
| Top of every RAF frame (core + ui) | Any WASM call can grow memory internally. |
| `onRuntimeInitialized` | Set initial views from the live buffer. |
| After `loadUiModule` resolves | UI module factory doesn't call `onRuntimeInitialized`; views must be set once before any heap access. |
| After `_ui_get_command_buffer` / `_ui_get_semantic_buffer` | These calls may internally allocate (growing memory), invalidating the view before `HEAPU32` is read. |
| Inside `writeUtf8ToHeap` / `writeBytesToHeap` after `_malloc` | `_malloc` itself can trigger growth. |

The overhead is negligible: two `new Uint8Array(buffer)` / `new Uint32Array(buffer)`
calls per frame share the same underlying `ArrayBuffer` object — only the view
wrapper is allocated, not a copy of the memory.

---

## Why WebGPU fails on Ubuntu / Chromium

WebGPU on Chromium/Linux requires:

1. **A hardware GPU with a Vulkan driver** (Dawn's primary backend on Linux is
   Vulkan; the OpenGL/ANGLE fallback is not always compiled in).
2. **Chromium launched with `--enable-unsafe-webgpu`** (or Chrome ≥ 113 on
   supported platforms without the flag).
3. **No GPU blocklist hit** — many Linux GPU driver versions are on Chromium's
   GPU blocklist, disabling WebGPU unconditionally.

On macOS, WebGPU uses the Metal backend, which is stable and enabled by default.
On Ubuntu with mesa/Radeon or intel i915, the Vulkan driver version may be
blocklisted or missing entirely, causing `requestAdapter()` to return `null`
even when the API surface is present.

In Playwright tests on Ubuntu you can force WebGPU with:
```
--enable-unsafe-webgpu --use-angle=vulkan
```
But in CI without a real GPU this still fails. The fallback ladder handles this
transparently — WebGPU init fails silently and the bridge continues on WebGL2.

---

## Enum reference

### EdBackendType (TypeScript + C ABI)

| Value | Constant | Meaning |
|---|---|---|
| 0 | `NONE` | No backend initialised yet |
| 1 | `WEBGPU` | Dawn WebGPU backend |
| 2 | `WEBGL2` | Emscripten WebGL2 backend |
| 3 | `CPU` | Skia CPU rasterizer |

### EdDeviceState (TypeScript + C ABI)

| Value | Constant | Meaning |
|---|---|---|
| 0 | `OK` | Device is healthy |
| 1 | `LOST` | Device was lost; recovery pending |
| 2 | `RECOVERING` | Bridge is attempting re-init |

Both enums are defined in `v2/browser-bridge/src/core-types.ts` and mirrored in
`v2/core/include/effindom.h`.

---

## Files involved

| File | Role |
|---|---|
| `v2/browser-bridge/src/bridge.ts` | Fallback ladder, device-lost recovery loop, software overlay, `refreshHeapViews` integration |
| `v2/browser-bridge/src/core-types.ts` | `EdBackendType`, `EdDeviceState`, `CoreModule`, `UiModule` interfaces incl. `wasmMemory` |
| `v2/core/include/effindom.h` | C ABI: `EdBackendType`, `EdDeviceState`, `ed_init*`, `ed_get_backend_type`, `ed_get_device_state`, `ed_get_sw_framebuffer` |
| `v2/core/src/Wasm.cpp` | Emscripten entry points; multi-backend state machine; software pixel buffer allocation |
| `v2/browser-bridge/tests/smoke.spec.ts` | Playwright tests covering WebGPU, WebGL2, CPU fallback paths and pixel verification |
| `docs/v2/core/ARCHITECTURE.md` | ABI-level reference for the v2 backend functions |
