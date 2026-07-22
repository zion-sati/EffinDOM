# Native Desktop Platform Differences

This document freezes the intentional macOS and Windows host differences before their duplicated orchestration is extracted into `v2/native/common`.

Anything not listed here is presumed to be shared behavior and should move behind the common host contracts defined by `NATIVE_HOST_COMMON_CORE_REFACTOR_PLAN.md`.

## Window and display integration

| Responsibility | macOS | Windows |
| --- | --- | --- |
| Native window system | AppKit through SDL3 | Win32 through SDL3 |
| Initial scale | SDL window pixel density | SDL display scale, including initial logical-size adjustment |
| Live resize/exposure | SDL event watch reconciles and presents during AppKit live resize | Win32 paint bridge renders synchronously for `WM_PAINT` |
| Platform focus loss | explicit host dispatch is currently exposed for deterministic AppKit cancellation | native/SDL event path cancels Windows pointer state |

## Input integration

| Responsibility | macOS | Windows |
| --- | --- | --- |
| Portable events | SDL3 | SDL3 |
| Precise scrolling | AppKit bridge preserves native gesture phases and momentum | Win32 bridge preserves precision touchpad phases and deltas |
| Native mouse path | SDL for ordinary pointer events | Win32 bridge supplies display-pixel normalization and immediate drag updates where SDL timing is insufficient |
| Click timing/count | host-provided SDL/AppKit count using the configured macOS double-click interval | host-provided Win32/SDL count using Windows system behavior |

Pointer capture, metadata, hit testing, event delivery, context-menu policy, and commit demand are not intentional platform differences.

## Graphics

| Responsibility | macOS | Windows |
| --- | --- | --- |
| GPU backend | Skia Ganesh on Metal | Skia Ganesh on Direct3D |
| Native surface | `CAMetalLayer` | Direct3D swap-chain/native window surface |
| Startup raster fallback | SDL renderer and streaming texture | SDL renderer and streaming texture |
| Recovery trigger | Metal command/context failure and display/surface recreation | Direct3D device/surface loss and window recreation |

Raster allocation, texture upload, snapshots, frame demand, suspension state,
generation counters, and fallback policy are shared behavior. Raster fallback
is selected only when the preferred backend cannot initialize at startup; a
runtime device failure recreates the backend that was already selected.

## Platform services

| Responsibility | macOS | Windows |
| --- | --- | --- |
| Clipboard/external opening | AppKit/Foundation APIs | Win32 shell/clipboard APIs |
| File dialogs | macOS native dialog adapter | Windows native dialog adapter |
| External drops | AppKit/SDL window adapter | Win32/SDL window adapter |
| UI dispatch wake-up | macOS dispatcher | Windows dispatcher |
| System font fallback | CoreText | DirectWrite/system font APIs |
| Cursor creation | SDL cursor backed by macOS | SDL cursor backed by Windows |

Asset registration, release, completion, fallback-ID ownership, and frame invalidation are shared behavior. Only OS resource discovery and effects are platform strategies.

## Packaging

- macOS uses an executable-relative `../lib` runtime path and app bundle-compatible resource layout.
- Windows stages DLLs beside the executable as well as in the native output `lib` directory and may stage PDB files.
- signing, installers, and platform metadata remain platform-specific.

The logical `bin/`, `lib/`, `resources/effindom/`, and `resources/app/` layout is shared.

## Invariants shared by both platforms

- one retained Tier 2 implementation
- native FUI-RS application mount, remount, and deterministic disposal
- demand-driven rendering with no idle frame loop
- logical/physical viewport reconciliation
- pointer capture and click-count delivery
- keyboard routing and event consumption
- scrolling and gesture targeting
- retained context-menu policy
- default and fallback font behavior
- image and SVG lifecycle
- clipboard semantics
- file-dialog and external-drop completion contracts
- graphics suspension, recreation, and diagnostic raster fallback
- deterministic hidden-host snapshots and test helpers
