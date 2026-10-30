# Changelog

## 0.3.0 - 2026-10-30

- Grids size to their tracks: a grid in a shrink-to-fit parent includes its fixed tracks, and an Auto track over a child with an explicit width or height measures that size instead of 0.
- The loading overlay no longer appears over an application that has already drawn: an application whose first frame is ready before the overlay's delay elapses never shows it, so a fast reload no longer flashes the page, then the overlay, then the page again.
- Linear gradient points are relative to the node, as CSS gradients and WPF's default mapping are, and a node's opacity, blur and blend mode apply to the node and its children together, so a faded container (a disabled control) fades its content too.
- Clicks are no longer lost to stale hover events: a press in the same frame as a pointer move, or a state change during a press, kept a late hover leave that cancelled the pressed control's click.
- Under an opaque solid border the fill stops halfway into the border, so rounded corners are no longer tinted by the fill and look as heavy as the straight edges.
- The runtime package's fonts folder includes the SIL Open Font License and the copyright notice of each font it ships, as the license requires.
- The runtime allocates node storage as a scene grows instead of reserving it for the maximum node count, and keeps compiled modules without their bytes, so a typical page needs about a third less memory. The UI now allows 65,536 nodes instead of 10,000.
- Runtime binaries are built with relative source paths, so they no longer contain absolute paths from the build machine.
- Swapping routes releases the replaced runtime: a WebGL context-loss listener the core added to the shared canvas kept every replaced runtime alive, so memory grew with each route change.

## 0.2.14 - 2026-08-08

- Stopped CMake build lanes from repeatedly cloning pinned source dependencies.

## 0.2.12 - 2026-08-07

- Updated verified upstream dependency pins.

## 0.2.13 - 2026-08-07

- Fixed native remote image and SVG loading against servers that reject unidentified HTTP clients.
