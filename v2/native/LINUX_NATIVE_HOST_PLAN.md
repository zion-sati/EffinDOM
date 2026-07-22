# Linux native host implementation plan

## Objective

Add a native Linux executable that can be built and run directly on Ubuntu using
the existing SDL3 platform boundary, shared native host core, native Tier 1/2
libraries, and FUI-RS demo application.

The first implementation is validated on this development machine:

- Ubuntu 24.04 LTS, x86_64
- GNOME desktop
- current session: X11

The implementation must remain suitable for a later Debian build. Ubuntu is the
initial validation environment, not an API boundary.

## Fixed decisions

- Add one `LinuxNativePlatform`; do not add Ubuntu-, GNOME-, Wayland-, or
  X11-specific platform hosts.
- Let SDL3 select its video backend. Do not force `SDL_VIDEO_DRIVER` in product
  code. X11 and Wayland are runtime strategies owned by SDL.
- Start wheel and resize handling with SDL's ordinary event path. Add a bridge
  only when characterization demonstrates the same kind of information loss or
  event-loop/presentation failure that justified the AppKit and Win32 bridges.
- Use Skia Ganesh Vulkan as the preferred visible Linux surface, matching the
  Metal and Direct3D native hosts. Keep `NativeRasterSurface` only as a hidden
  snapshot path and startup fallback when Vulkan cannot initialize.
- Coalesce only consecutive SDL logical-size, pixel-size, and display-scale
  messages for the same Linux window. Keep the newest geometry, stop at the
  first non-resize event, and preserve that event for the next pump turn.
- Use the existing null/no-op accessibility path. Do not add AT-SPI, GTK
  accessibility, or accessibility-related build dependencies in this pass.
- Build the executable on the target distro. Packaging, redistribution,
  portable binaries, and cross-distro ABI compatibility are out of scope.
- Prefer SDL3 and freedesktop interfaces over GNOME or Ubuntu APIs. Do not link
  GTK, Mutter, GSettings, or Ubuntu-specific libraries.
- Preserve the existing native host ABI and FUI host ABI. This work should not
  require an ABI version bump or generated binding changes.

## Intended architecture

```text
NativeHost
    -> NativePlatformFactory
        -> LinuxNativePlatform
            -> NativeHostCore                 shared orchestration
            -> NativeGraphicsCoordinator      shared lifecycle/recovery
                -> LinuxVulkanSurface          preferred visible renderer
                -> NativeRasterSurface         hidden/startup fallback
            -> SdlEventAdapter                pointer/keyboard/wheel baseline
            -> SDL window events              resize/expose baseline
            -> optional backend bridge        only after a characterization gate
            -> SdlUiDispatcher                shared SDL user-event adapter
            -> SdlFileDialogs                 shared SDL dialog adapter
            -> SdlDropTarget                  shared SDL drop adapter
            -> LinuxPlatformServices          Linux/freedesktop effects
            -> null accessibility adapter     intentional first-pass no-op
```

`LinuxNativePlatform` owns composition and Linux policy. It must not duplicate
rendering, retained UI, asset lifecycle, context-menu, or input-routing logic
already owned by `v2/native/common`.

## Phase 1: consolidate the already-portable SDL adapters

Before adding a third copy, move the byte-for-byte-equivalent macOS and Windows
implementations into `v2/native/common`:

- `MacosUiDispatcher` / `WindowsUiDispatcher` -> `SdlUiDispatcher`
- `MacosFileDialogs` / `WindowsFileDialogs` -> `SdlFileDialogs`
- `MacosDropTarget` / `WindowsDropTarget` -> `SdlDropTarget`

Keep their existing contracts and behavior unchanged:

- UI work is queued under a mutex and wakes the main loop through a registered
  SDL user event.
- file dialogs remain asynchronous, reject duplicate request IDs, retain filter
  storage until callback completion, marshal callbacks back through SDL events,
  and discard stale completions after shutdown.
- external drops retain the current enter/over/drop/leave ordering and payload
  encoding.

Update macOS and Windows hosts and tests to consume the common class names before
using them from Linux. This is a mechanical strategy extraction, not a behavior
rewrite.

Do not generalize platform services in this phase. Clipboard and cursor code is
similar, but filesystem integration and fallback-font discovery are genuinely
platform-specific and should remain behind `LinuxPlatformServices`.

## Phase 2: add Linux build selection and targets

Add the following root CMake behavior:

- `EFFINDOM_BUILD_NATIVE_LINUX`, defaulting on when
  `CMAKE_SYSTEM_NAME STREQUAL "Linux"`.
- Include `v2/native/common` when any macOS, Windows, or Linux native host is
  enabled.
- Add `v2/native/linux` only for Linux when the option is enabled.
- Make `EFFINDOM_NATIVE_GRAPHICS_BACKEND=vulkan` the Linux default. Retain the
  `raster` token for deterministic fallback characterization.
- Retain the existing normalized Rust targets
  (`x86_64-unknown-linux-gnu`, `aarch64-unknown-linux-gnu`, and
  `i686-unknown-linux-gnu`). Initial validation is x86_64 only.

Create `v2/native/linux/CMakeLists.txt` with:

- `effindom_v2_linux_native_host`, a static platform-host library.
- `effindom_v2_linux_native`, using the shared `NativeApplicationMain.cpp`.
- `effindom_v2_linux_native_tests`.
- the existing `effindom_v2_native_common_contract_tests` instantiated against
  the native FUI-RS application.
- CTest discovery for both Linux-specific and common contract tests.

The CMake target may stage the existing logical `bin/`, `lib/`, and `resources/`
tree for test execution, but must not add `.deb`, AppImage, Flatpak, install,
RPATH-portability, or dependency-bundling work. Staging is a test fixture, not
packaging.

## Phase 3: implement the Linux platform composition root

Add:

- `src/platform/LinuxNativePlatform.h/.cpp`
- `src/platform/LinuxPlatformFactory.cpp`
- `src/platform/LinuxPlatformServices.h/.cpp`
- `src/platform/LinuxAssetEnvironment.h/.cpp`

Construct the host in this order:

1. Create `NativeHostCore` with Linux desktop input policy:
   - no control-click secondary emulation;
   - cancel active pointer interaction on focus loss;
   - do not synthesize button state from the current button.
2. Initialize SDL video and events without a video-driver hint.
3. Create a resizable, high-pixel-density SDL window; add `SDL_WINDOW_HIDDEN`
   for characterization tests.
4. Attach the existing null/no-op accessibility implementation.
5. Create the common SDL dispatcher, drop target, and file-dialog adapters.
6. Create `NativeGraphicsCoordinator` with:
   - `NativePixelDensitySource::DisplayScale`;
   - `LinuxVulkanSurface` as the preferred surface for visible windows when the
     Vulkan backend is selected;
   - the shared raster surface as an initialization-only fallback;
   - hidden snapshot hosts forced to raster so X11 synchronization remains
     deterministic without a swapchain.
7. Register `LinuxPlatformServices` as the global `UiPlatformHost`.
8. Initialize the engine and load the three packaged Noto defaults.

The event pump should follow the shared macOS/Windows lifecycle:

- wait with a bounded SDL timeout only while idle;
- run UI-dispatch, file-dialog, drop, then portable input adapters;
- stop on quit or close request;
- request a frame for resize, pixel-size, display-scale, exposure, restoration,
  focus gain, and theme changes;
- cancel pointer state and SDL mouse capture on focus loss;
- suspend presentation while hidden, minimized, or occluded;
- resume presentation when shown, restored, exposed, or focused;
- request graphics recovery on display changes so Vulkan device/surface state
  is rebuilt through the shared recovery contract.

Use `SDL_CaptureMouse` for the `SetNativePointerCapture` strategy. Use the SDL
event coordinates directly initially; do not add X11- or Wayland-coordinate
branches unless characterization demonstrates a mismatch.

## Wheel and resize characterization gates

The macOS and Windows bridges are remedies for observed failures at the SDL
boundary:

- AppKit exposes precise scroll phases and momentum information that SDL does
  not preserve, and AppKit's modal live-resize loop blocks the normal pump.
- Win32 exposes raw sub-detent wheel values and requires synchronous painting
  during resize; the native subclass also fixes active-drag delivery timing.

Linux must not receive analogous bridges merely for structural symmetry. First
run the Linux host through the ordinary SDL path and record exactly what SDL
preserves and when it permits presentation.

### Wheel baseline and characterization

The baseline is the existing `SdlEventAdapter`:

- consume `SDL_EVENT_MOUSE_WHEEL` once;
- apply `SDL_MOUSEWHEEL_FLIPPED`;
- convert SDL wheel units through `WheelDeltaToLogicalPixels`;
- dispatch through `NativeInputRouter::DispatchWheel`.

Add diagnostic-only characterization that records, for each wheel event:

- current SDL video driver;
- floating `x`/`y` deltas and accumulated `integer_x`/`integer_y` ticks;
- direction, mouse position, timestamp, and inter-event interval;
- resulting EffinDOM scroll offset and retained animation state.

Exercise a physical detent wheel and a precision touchpad on X11 and Wayland
where available. The required observations are:

- whether SDL distinguishes coarse and high-resolution input reliably;
- whether touchpad deltas track the fingers without retained-animation lag;
- whether momentum is delivered after finger release;
- whether gesture start, stop, and cancellation are observable;
- whether horizontal and natural-direction scrolling remain correct.

SDL's public wheel event has floating deltas and accumulated integer ticks, but
no portable source or gesture-phase fields. Fractional values alone are not
proof of a reliable device/gesture boundary, so do not commit an idle-timeout
heuristic before measuring real devices.

Decision gate:

1. If ordinary SDL wheel behavior satisfies the retained scrolling contract,
   add no Linux wheel bridge.
2. If SDL exposes enough stable information but needs different routing, add a
   portable SDL wheel strategy with pure, tested classification policy. This is
   still not a native Linux bridge.
3. If SDL discards information required by the contract, investigate separate
   X11 and Wayland native strategies. Document the missing field/timing and
   prove that interception can coexist with SDL without duplicate delivery
   before adding either strategy.

Any eventual native implementation stays behind one Linux wheel strategy chosen
from `SDL_GetCurrentVideoDriver()`; it must not fork `LinuxNativePlatform`.

### Resize baseline and characterization

The baseline is the ordinary SDL event pump:

- request demand-driven frames for `SDL_EVENT_WINDOW_RESIZED`,
  `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED`,
  `SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED`, and exposure/restoration events;
- query final output size and pixel density at render time through
  `NativeGraphicsCoordinator` instead of caching event dimensions;
- convert requested logical dimensions to SDL window coordinates using the
  display-content scale (`display scale / backing pixel density`), then call
  `SDL_SetWindowSize` followed by `SDL_SyncWindow`;

Resize scheduling invariants:

- coalesce a contiguous run of resize geometry messages before rendering, but
  never cross input, expose, lifecycle, display-change, or custom events;
- render once for the native paint/expose opportunity associated with the latest
  reconciled geometry;
- never render both synchronously in an event watch and again when the same SDL
  expose event is dequeued;
- include display-scale changes in the contiguous geometry run so the newest
  logical/pixel/DPR state is reconciled atomically;
- rely on Vulkan present pacing and the window system's native invalidation,
  matching Metal/AppKit and D3D/Win32 rather than adding a Linux-only queue
  policy.

Do not enable X11 `_NET_WM_SYNC_REQUEST` for a Vulkan window. SDL deliberately
limits that strategy to OpenGL because a compositor waiting for the XSync
counter can withhold the Vulkan WSI image required to render the acknowledgement
frame. Keep the bridge as a null strategy for Vulkan. When the driver exposes
`VK_EXT_surface_maintenance1` and `VK_EXT_swapchain_maintenance1`, create the
swapchain with one-to-one presentation scaling and minimum X/Y gravity. This
keeps the most recently presented image anchored at the top-left while the
native extent is between submitted Vulkan frames, without bypassing FIFO
hardware pacing or blocking the event pump. Continue recreating and repainting
at the coalesced current extent; drivers without these extensions retain the
portable Vulkan behavior.

Characterize interactive resize on X11 and Wayland while recording event order,
logical/pixel sizes, scale, frame count, presentation time, exposure markers,
and suspension state. Verify:

- content continues updating while the pointer is held during resize;
- the normal SDL pump is not blocked by a compositor or window-manager loop;
- there are no stale frames, white/black uncovered edges, or erase flashes;
- rapid resize and scale changes converge on the latest geometry;
- minimized/hidden suspension and restored/exposed recovery remain correct;
- programmatic resize completes with correct logical and physical dimensions.

Decision gate:

1. If normal SDL pumping presents correctly throughout interactive resize, add
   no Linux resize bridge or event watch.
2. If SDL continues pumping but requires synchronous handling of its documented
   live-resize expose event, add the smallest guarded SDL event-watch strategy.
3. If a backend blocks SDL or requires a native paint/configure lifecycle,
   implement only the affected X11 or Wayland strategy and document the observed
   failure it fixes.

Any optional strategy is selected behind a Linux resize policy using
`SDL_GetCurrentVideoDriver()`. Direct Xlib/XInput2/Wayland integration is not
authorized by the plan until its characterization gate has failed.

### Vulkan graphics strategy

Extend the native Skia build with a Linux `vulkan` backend that enables Ganesh
and Vulkan support while leaving the existing `raster` staging variant intact.
The configuration-qualified staging directory and backend stamp must prevent
Vulkan and raster archives from contaminating one another.

Add `LinuxVulkanSurface` behind the existing `NativeGraphicsSurface` contract.
It owns only Linux/Vulkan graphics policy:

- create the Vulkan instance using SDL's required instance extensions and
  create the window surface through SDL, so X11 versus Wayland remains SDL's
  runtime decision;
- select a physical device, graphics/present queue families, logical device,
  swapchain format, image count, and presentation mode by advertised capability
  rather than vendor or distro; prefer hardware-paced
  `VK_PRESENT_MODE_FIFO_LATEST_READY_EXT` so a vblank can discard obsolete
  ready resize frames, then mailbox when available, with ordinary FIFO as the
  required portable fallback;
- create a Ganesh Vulkan backend context and wrap acquired swapchain images as
  Skia render targets at the current physical size;
- acquire swapchain images without blocking the native event pump; if every
  image is temporarily owned by the compositor, leave the frame pending and
  return to SDL rather than queueing native input behind an infinite wait;
- rely on the selected Vulkan present mode for hardware/compositor frame
  pacing; do not use immediate presentation or a software FPS limiter;
- when `VK_KHR_present_id` and `VK_KHR_present_wait` are available, tag every
  present and poll the previous presentation's compositor completion with a
  zero timeout before rendering another steady-state frame. A pending present
  returns control to SDL immediately, so hardware feedback provides the frame
  gate without blocking native event delivery; new swapchain geometry is not
  held behind an obsolete swapchain's present ID;
- recreate swapchain-dependent resources for size, scale, out-of-date, and
  suboptimal results while preserving the Vulkan device and Skia context where
  valid; do not call `vkDeviceWaitIdle` at each interactive size change because
  the compositor may retain an old presented image until the resize completes;
- retire replaced swapchains and keep their Skia surfaces and synchronization
  objects alive only until `VK_KHR_present_wait` reports their last tagged
  presentation complete, then reclaim them non-blockingly during interactive
  resize. Drivers without presentation feedback retain the quiet-period
  device-idle fallback; recovery and shutdown remain unconditional cleanup
  boundaries;
- report device/surface failure through the existing graphics recovery contract,
  recreating the Vulkan device, context, surface, and swapchain in place;
- release Skia surfaces before swapchain images, then destroy swapchain, device,
  surface, and instance in dependency order.

Do not expose Vulkan objects through `NativeHost`, the FUI ABI, or Tier 1/2.
This is a native graphics strategy replacement and does not require an ABI
version bump.

Expose `FUI_PLATFORM_LINUX` and the same first-pass desktop capabilities already
implemented on macOS and Windows:

- open external URI;
- clipboard read;
- clipboard write;
- file dialogs.

Return a stable neutral accent when the system does not expose one through the
existing SDL surface. Accent discovery is not a reason to introduce a GNOME
dependency.

## Phase 4: implement distro-neutral Linux services

### Clipboard and cursors

Use SDL3 clipboard and system-cursor APIs. Do not call Xlib, Wayland protocols,
or GTK directly.

### File dialogs

Use `SdlFileDialogs`. SDL may select an XDG portal or another available Unix
dialog backend. EffinDOM must not force the portal or `zenity` driver in product
code.

Hidden tests continue to use injected completions and must not display a real
dialog.

### External URLs and files

- Keep the current `http`/`https` validation for external URLs and use
  `SDL_OpenURL`.
- Convert validated local paths to correctly escaped `file://` URIs for opening.
- Implement reveal using the freedesktop file-manager D-Bus interface when
  available, with opening the containing directory as a graceful fallback.
- When the host is constructed hidden, validate operations without launching an
  external application, matching existing native test behavior.

No GNOME command such as `nautilus` may be hard-coded.

### Assets and fonts

Build `NativeAssetEnvironment` from:

- the executable directory resolved through `/proc/self/exe`;
- `../resources`, `../resources/effindom`, and the current working directory,
  matching the existing logical resource layout;
- ordinary UTF-8 Linux filesystem paths.

Use Fontconfig for missing-glyph fallback discovery. Keep Fontconfig isolated in
`LinuxAssetEnvironment`; the shared `NativeAssetService` remains unchanged.
Return a concrete font file and face index where possible, and return no fallback
cleanly when Fontconfig cannot resolve one.

Fontconfig and D-Bus are acceptable Linux build dependencies because the target
is built on Ubuntu. Use standard CMake/pkg-config discovery and fail configure
with a specific missing-development-package message. Do not check distro names
or invoke `apt` from the build.

### Accessibility

Attach the existing null/no-op accessibility strategy. Semantic snapshot
production may continue internally, but it must cause no platform calls and
advertise no accessibility capability. AT-SPI is explicitly deferred.

## Phase 5: tests and characterization

Create `tests/test_linux_native_host.cpp` by applying the shared native
characterization contract and Linux-specific assertions. Avoid cloning the full
macOS or Windows suite when the behavior belongs in common contract tests.

Required automated coverage:

- factory creates a Linux host and reports desktop/Linux identity;
- hidden native FUI-RS app mounts, remounts, and disposes deterministically;
- Vulkan rendering is demand-driven for visible hosts and raster rendering
  remains available for hidden snapshots and recovery;
- resize and pixel-density reconciliation remain logical-coordinate based;
- wheel diagnostics capture enough evidence to select ordinary SDL routing, a
  portable SDL policy, or a justified native strategy;
- physical wheel and touchpad behavior satisfy the chosen scrolling contract;
- live resize remains responsive and free of uncovered edges through the
  ordinary SDL/native-paint path, produces at most one presentation for each
  paint opportunity, and has no post-input resize backlog;
- pointer, keyboard, wheel, focus cancellation, capture, and context menus work;
- clipboard round-trip works;
- UI dispatch and cancellation wake and drain correctly;
- file-dialog selected/cancelled/error completions preserve the existing ABI;
- file and text drops preserve the existing payload contract;
- packaged font, SVG, texture, and fallback-font lifecycles work;
- null accessibility accepts semantic updates without platform effects or
  crashes;
- hidden screenshot execution succeeds from both the build tree and staged
  output tree.

Backend validation:

1. Run the suite with SDL's normal automatic selection on this machine. The
   current expected driver is X11.
2. Run a visible smoke test and record `SDL_GetCurrentVideoDriver()` in startup
   diagnostics.
3. Run the same smoke/characterization path with the Wayland backend when a
   Wayland session or nested compositor is available.
4. Do not maintain separate expected render goldens for X11 and Wayland unless
   an actual output difference is demonstrated.
5. Verify the visible host reports a Vulkan-backed Skia surface, not merely an
   SDL Vulkan texture-upload presenter.
6. Exercise Vulkan swapchain resize, minimize/restore, display-scale change,
   and device/surface-loss recovery back to Vulkan.

Tests may set an SDL video-driver environment variable to exercise a backend;
the executable itself may not.

## Phase 6: build verification

During implementation, use the smallest Linux-native CMake build that exercises
the changed targets. A representative flow is:

```sh
cmake -S . -B build/linux-native \
  -DCMAKE_BUILD_TYPE=Release \
  -DEFFINDOM_BUILD_NATIVE_LINUX=ON \
  -DEFFINDOM_NATIVE_GRAPHICS_BACKEND=vulkan
cmake --build build/linux-native --target effindom_v2_linux_native_tests
ctest --test-dir build/linux-native --output-on-failure
```

Run the repository-root `./build.sh --with-tests` before completion if shared
native code, Tier 1/2 code, build scripts, or ABI-facing files changed. Do not
bump either ABI version unless the implementation unexpectedly changes a public
ABI contract.

## Debian-forward guardrails

The Ubuntu-first implementation is acceptable for future Debian work when all
of the following remain true:

- no Ubuntu release, package-manager, theme, or GNOME checks exist in runtime
  code;
- no direct X11 or Wayland API is used outside a characterization-justified
  strategy hidden behind the Linux platform boundary;
- desktop integration uses SDL or freedesktop contracts;
- build dependencies are discovered by capability, not distro name;
- filesystem paths use standard Linux facilities and `std::filesystem`;
- the platform factory is named Linux, not Ubuntu;
- X11 and Wayland differences remain runtime behavior, not compile-time forks.

Future Debian enablement should therefore be a build-and-characterize exercise,
not a new platform implementation.

## Explicitly deferred

- AT-SPI accessibility
- OpenGL or another non-Vulkan Linux GPU surface
- direct XInput2 or Wayland input/resize protocol integration unless a wheel or
  resize characterization gate demonstrates it is required
- IME behavior requiring direct Wayland or X11 protocol access
- packaging and installation
- portable binary compatibility or dependency bundling
- non-glibc Linux systems
- distro-specific desktop integration

## Completion criteria

The first pass is complete when:

1. A clean Ubuntu native build produces and runs `effindom_v2_linux_native`.
2. The FUI-RS native demo renders through Skia Ganesh Vulkan in the current
   GNOME/X11 session, with raster retained only as a startup fallback.
3. The Linux-specific suite and shared native characterization suite pass.
4. A Wayland smoke run passes when a Wayland environment is available, without
   source or build-option changes.
5. No Ubuntu- or GNOME-specific specialization exists, and no X11- or
   Wayland-specific bridge exists without recorded characterization evidence and
   a regression test for the failure it fixes.
6. Accessibility uses only the existing null/no-op path.
7. macOS and Windows native builds retain their existing behavior after the SDL
   adapter consolidation.

## First-pass characterization record

- Ubuntu 24.04 GNOME/X11 selected SDL's `x11` driver automatically; product
  code did not set a driver hint.
- The visible X11 smoke run continued presenting through ordinary SDL expose
  and resize events. Programmatic logical resize converged to matching logical
  and pixel sizes at density 1.0 in the automated host test.
- Physical X11 precision-scroll input arrived as fractional SDL wheel deltas
  (observed increments from 0.125 through 0.625) with SDL's accumulated integer
  ticks and natural direction intact. Retained animation remained scheduled
  while the events were routed.
- SDL exposes no portable gesture phase or device-source field on these events.
  The first pass therefore does not guess a device boundary or momentum phase,
  and it adds neither a timeout heuristic nor a Linux native wheel bridge.
- Interactive testing did expose starvation in the shared application loop:
  it rendered a complete raster frame after every queued SDL event. Pointer
  motion and wheel bursts could therefore build a visible input backlog,
  especially in an unoptimized build. The shared loop now drains a bounded
  batch of queued events before each frame. Linux resize, pixel-size, scale,
  and expose events terminate that batch. This is an event-loop scheduling
  correction expressed through the shared host contract, not evidence for a
  Linux wheel bridge.
- A second visible X11 characterization showed that frame boundaries alone did
  not fix interactive vertical resize. While dragging the bottom edge, GNOME's
  compositor preserved the old surface relative to that moving edge: expansion
  translated stale content downward and contraction translated it upward before
  the repaint. Applying X11 `NorthWestGravity` did not affect Mutter's composited
  snapshot and was removed rather than retained as ineffective specialization.
  A transitional characterization used CPU Skia raster with an accelerated SDL
  presenter and a guarded synchronous expose watch. It remained portable, but
  the duplicate CPU work exposed by sustained resize made it unsuitable as the
  final visible rendering path.
- The Vulkan SDL presenter removed Mutter's stale-surface anchoring artefact,
  proving that the visible problem was presentation-path related. It did not
  make Skia GPU-backed: each frame was still CPU-rasterized and fully uploaded.
  Rendering synchronously from the expose watch while also treating queued
  resize/expose events as presentation boundaries produced duplicate full-frame
  work and a one-to-two-second resize tail after sustained input. The adopted
  Linux path removes duplicate expose rendering, uses Skia Ganesh Vulkan, and
  collapses only adjacent stale geometry notifications without crossing any
  observable non-resize event boundary.
- The implemented visible host now creates its Vulkan surface through SDL,
  selects the NVIDIA GeForce RTX 3070 Ti on this X11 machine, and renders into
  swapchain images through Skia Ganesh Vulkan. Hidden snapshot tests remain on
  the deterministic raster surface. The synchronous expose watch has been
  removed, and its regression test verifies that one queued expose produces one
  presentation.
- Sustained X11 resizing exposed two blocking assumptions in the first Vulkan
  implementation: swapchain recreation waited for the entire device to become
  idle, and image acquisition used an infinite timeout. Either can stop SDL's
  event pump while the compositor owns presented images, making resize events
  appear to replay after mouse-up. The corrected strategy uses zero-timeout
  acquisition and deferred swapchain retirement.
  Bundled SDL explicitly disables its XSync resize protocol for Vulkan windows;
  the selected X11 strategy now supplies that acknowledgement bridge, while the
  portable Linux event pump coalesces contiguous geometry messages.
- An immediate-presentation experiment removed FIFO back-pressure but allowed
  a pending animation to submit a frame every 0.25--0.4 ms. Characterization
  logging accidentally throttled that loop, explaining why diagnostics appeared
  to cure resize latency. Immediate mode and the subsequent software-FPS-limit
  experiment were rejected in favor of hardware-paced FIFO-latest-ready. The
  pinned Khronos build headers expose this extension while the Ubuntu 24.04
  Vulkan loader remains the runtime dependency; unsupported drivers fall back
  to mailbox or ordinary FIFO.
- Skia's Vulkan surfaces already use `kTopLeft_GrSurfaceOrigin`; that controls
  drawing coordinates, not how Mutter composites the last submitted buffer
  between native resize boundaries. X11 `NorthWestGravity` was therefore not a
  viable solution. An X11 `_NET_WM_SYNC_REQUEST` bridge was characterized and
  rejected: SDL limits that protocol to OpenGL because Mutter's wait can leave
  a Vulkan client without an acquirable image, creating a circular dependency
  between the repaint and its acknowledgement. The adopted path uses Vulkan
  one-to-one presentation scaling with minimum X/Y gravity, when supported, to
  anchor the last image at the top-left until the replacement frame arrives.
- Resize characterization also made the original deferred-retirement policy hit
  the NVIDIA driver's live-swapchain limit, producing
  `VK_ERROR_INITIALIZATION_FAILED` and an inappropriate permanent fallback to
  SDL/OpenGL raster. Completed retired swapchains are now reclaimed through
  zero-timeout present-ID feedback during the drag, and a transient creation
  failure retains Vulkan, leaves the frame pending, and retries.
- When `EFFINDOM_LINUX_CHARACTERIZE` is enabled, log every live swapchain
  recreation with total duration and append the prominent `*** OVER 30 MS ***`
  marker whenever recreation exceeds the 30 ms interactive resize budget.
  Normal runs report failures but do not emit successful resize diagnostics.
- After installing the documented EGL development prerequisite, SDL configured
  both `wayland(dynamic)` and `x11(dynamic)` video drivers and the Linux
  executable rebuilt successfully. A Wayland runtime smoke could not be
  performed because the current login session exposes no `WAYLAND_DISPLAY`;
  runtime selection remains SDL-owned.
- Linux rendering uses SDL's window display scale as the final device-pixel
  ratio. Window sizing and pointer coordinates use the display-content portion
  (`display scale / backing pixel density`), which preserves logical geometry
  on both X11 content scaling and Wayland high-density buffers. Display-scale
  changes are presentation boundaries and reconcile the viewport before paint.
