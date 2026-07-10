# EffinDom v2 Browser Bridge Quickstart

This guide is the fast path for working on the browser-side v2 bridge under `v2/browser-bridge/`.

## What this covers

- the bridge TypeScript build/typecheck lane
- the real multi-lane Core/UI wasm staging flow the bridge consumes
- the filtered ICU data build the bridge packages
- the isolated browser smoke lane for `public/v2/browser-bridge/`

For the current slice scope and artifacts, read these first:

- `docs/v2/browser-bridge/plan.md`
- `docs/v2/browser-bridge/OPEN_CANVAS_API.md`
- `docs/v2/browser-bridge/HOTLOAD_METHOD.md`
- `docs/v2/browser-bridge/DEVTOOLS_DOM_MIRROR.md`

## 1. Install prerequisites

Install the shared v2 toolchain first:

- `docs/QUICKSTART.md`

You also need:

- Bash 5.x
- Node.js 24+
- CMake
- Meson
- Python 3
- Git
- Playwright browser binaries
- **Emscripten 5.0.6**

### macOS

```bash
brew install cmake meson ninja python3 node git bash
xcode-select --install
```

### Linux (Debian / Ubuntu)

```bash
sudo apt-get update
sudo apt-get install -y cmake meson ninja-build python3 python3-pip git build-essential curl
curl -fsSL https://deb.nodesource.com/setup_24.x | sudo -E bash -
sudo apt-get install -y nodejs
```

### Emscripten SDK

```bash
git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
cd ~/emsdk && ./emsdk install 5.0.6 && ./emsdk activate 5.0.6
source ~/emsdk/emsdk_env.sh
```

## 2. Install JS dependencies

```bash
npm ci
npx playwright install chromium
```

## 3. Run the full browser-bridge build

This is the preferred entry point for bridge work:

```bash
bash v2/browser-bridge/scripts/build.sh
```

It runs, in order:

1. filtered ICU data configure/build
2. all four Tier 1/Tier 2 wasm runtime lanes (`wasm32`, `wasm32-simd`, `wasm64`, `wasm64-simd`)
3. bridge and harness bundling with esbuild
4. manifest generation plus runtime asset staging under `public/v2/browser-bridge/`

The generated manifest is the single source of truth for browser runtime assets.
Downstream pages should consume that shared manifest instead of keeping their own
copied `runtime/` trees, so a fresh bridge build immediately updates every v2
browser surface.

## 4. Run individual lanes when you only need one

### Typed TypeScript validation

```bash
npm run lint:v2
npm run typecheck:v2
```

### Bridge-only TypeScript validation

```bash
cd v2/browser-bridge
npm run typecheck
```

### Build the staged browser bridge

```bash
npm run build:v2:browser-bridge
```

### Browser smoke

```bash
npx playwright test -c v2/browser-bridge/playwright.config.ts
```

or:

```bash
npm run test:v2:browser-bridge
```

## Touch input

The bridge owns touch input on the canvas and sets `touch-action: none`. This
keeps EffinDom scrollbars, hit testing, overlays, and accessibility geometry in
the same coordinate system instead of letting browser page zoom scale the whole
document.

Framework-owned pinch zoom and control-owned two-finger gestures are planned as
EffinDom gestures rather than browser visual viewport zoom.

## 5. Open the bridge in a real browser

The bridge page is a static site under `public/v2/browser-bridge/`. WASM
streaming and `SharedArrayBuffer` require proper HTTP headers, so it must be
served — opening `index.html` via `file://` won't work.

### Serve with `npx serve` (quickest)

```bash
npx serve public/v2/browser-bridge -p 4000 \
  --set-headers "Cross-Origin-Opener-Policy: same-origin" \
  --set-headers "Cross-Origin-Embedder-Policy: require-corp"
```

Then open <http://localhost:4000> in your browser of choice.

## Clipboard contract

Tier 2 copy now exits wasm as a structured clipboard payload instead of a plain
string-only callback.

- `plainText` is always present and remains the fallback path.
- Rich `RichText` selections can also include bridge-generated `text/html`.
- The bridge also writes a custom web clipboard payload:
  `web application/x-effindom-richtext+json`.

The browser bridge owns the actual `navigator.clipboard.write(...)` call. If
the browser rejects rich clipboard writes, it falls back to
`navigator.clipboard.writeText(plainText)` so plain copy still succeeds.

## Debugging retained UI

Use the [DevTools DOM Mirror](./DEVTOOLS_DOM_MIRROR.md) when you need to inspect
the retained EffinDom tree in browser DevTools. Debug builds default the mirror
to on-requested, release builds default it to disabled, and apps can override
`window.__effindomRuntime.devToolsDomMirror` or the harness option explicitly.

Press `Shift+Meta+F12` to open the debug dialog when the mode is enabled or
on-requested. The dialog can enable the mirror and toggle Inspect Mode.

### Serve with Python (no extra deps)

```bash
python3 -c "
import http.server, socketserver

class Handler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        super().end_headers()

with socketserver.TCPServer(('', 4000), Handler) as httpd:
    print('Serving at http://localhost:4000')
    httpd.serve_forever()
" --directory public/v2/browser-bridge
```

### Which renderer am I using?

Open the browser console. The bridge logs the selected renderer on boot:

```
[EffinDom] renderer: webgpu    ← best case
[EffinDom] renderer: webgl2    ← WebGPU unavailable (console.warn)
[EffinDom] renderer: cpu       ← GPU entirely unavailable (console.error)
```

`window.__bridgeLoaderInfo.activeRenderer` also holds the string at runtime.

### Forcing a specific backend for testing

There is no URL parameter to force a backend at runtime. To test a specific
path, disable the higher-tier backends at the browser level:

| Target backend | How to reach it |
|---|---|
| WebGL2 | Disable WebGPU: launch Chromium with `--disable-webgpu` |
| CPU | Disable both: `--disable-webgpu --disable-webgl` |

Alternatively, the Playwright tests stub `navigator.gpu` and
`canvas.getContext('webgl2')` at the JS level — see `smoke.spec.ts` for the
exact patch approach.

### Rebuilding bridge.js after source changes

The full build pipeline (`bash v2/browser-bridge/scripts/build.sh`) is slow
because it rebuilds WASM. For iterating on TypeScript alone:

```bash
npx esbuild v2/browser-bridge/src/bridge.ts \
  --bundle --format=iife --platform=browser --target=es2020 \
  --outfile=public/v2/browser-bridge/bridge.js --sourcemap
```

Then hard-refresh the browser page (`Ctrl+Shift+R` / `Cmd+Shift+R`).

Rendering backend, C ABI, and staged runtime asset changes should use the
repo-root build path instead:

```bash
./build.sh
```

That path refreshes the Core/WASM outputs and the staged browser assets used by
the public smoke pages. For targeted browser runtime iteration, run:

```bash
npm run build:v2:browser-bridge
```

Run `npm run test:v2:browser` after ABI or staged asset changes. The standalone
Core smoke page also has staged command-buffer payloads, so update it whenever a
draw command ABI changes.

## Registering custom fonts and FontStacks

The browser bridge exposes its runtime at:

```ts
const runtime = await window.EffinDomBrowserBridge!.ready;
```

### Register one custom font face

```ts
await runtime.registerFont({
  id: 100,
  url: '/fonts/Inter-Regular.ttf',
});
```

If you want the same lazy behavior used by the FUI-AS browser harness, register
the source first and fetch it only on first use:

```ts
runtime.registerLazyFont(100, '/fonts/Inter-Regular.ttf');
await runtime.ensureFont(100);
```

If you already have fallback faces loaded and want to attach them to that primary face:

```ts
await runtime.registerFont({
  id: 100,
  url: '/fonts/Inter-Regular.ttf',
  fallbackIds: [101, 102],
});
```

### Register a full FontStack in one call

```ts
await runtime.registerFontStack({
  primary: {
    id: 100,
    url: '/fonts/Inter-Regular.ttf',
  },
  fallbacks: [
    {
      id: 101,
      url: '/fonts/NotoColorEmoji.ttf',
    },
    {
      id: 102,
      url: '/fonts/NotoSansSymbols2-Regular.ttf',
    },
  ],
});
```

Then point your text node at the primary face:

```ts
runtime.ui._ui_set_font(textHandle, 100, 24);
runtime.commitFrame();
```

Rules:

- `id` values must be unique within the runtime session.
- `url` values must be fetchable by the page.
- `registerLazyFont(...)` records a custom font source without fetching it yet; `ensureFont(...)` performs the first load on demand.
- `registerFontStack(...)` loads every face first, then attaches the fallback chain to the primary face.
- Loaded fonts and fallback chains are replayed automatically if the bridge renderer is recreated.

### Using the bundled mono font lazily

The browser bridge bundles a built-in monospace pair under IDs `5` (regular) and
`6` (bold), but it does **not** fetch them during startup. Load only the face
you need:

```ts
await runtime.ensureBuiltInFont(5);
runtime.ui._ui_set_font(textHandle, 5, 15);
runtime.commitFrame();
```

- `ensureBuiltInFont(...)` only works for bundled built-in font IDs.
- The default startup preload still covers body, heading, symbols, and the bundled monochrome emoji face; the color emoji asset stays available as a lazy-loaded custom font for demo/app use.
- The bundled mono fonts are replayed automatically after renderer recovery once
  they have been loaded.

## Artifacts

- **Bridge page:** `public/v2/browser-bridge/index.html`
- **Bridge bundles:** `public/v2/browser-bridge/{bridge,harness}.js`
- **Runtime manifest:** `public/v2/browser-bridge/effindom.v2.manifest.json`
- **Runtime assets:** `public/v2/browser-bridge/runtime/`
- **Smoke screenshots:** `v2/browser-bridge/tests/screenshots/`

## npm runtime package staging (`@effindomv2/runtime`)

The publishable runtime package now stages browser assets under
`v2/browser-bridge/dist/`:

- `dist/bridge.js` + `dist/harness.js`
- `dist/effindom.v2.manifest.json`
- `dist/runtime/**`
- `dist/fonts/**` (Noto baseline set, excluding Thai/Arabic files)

Use:

```bash
cd v2/browser-bridge
npm run build:package:assets
```

The package also exports runtime URL/config helpers from
`@effindomv2/runtime/runtime-config` (and the root barrel) so framework packages
and end-user apps can resolve the packaged manifest URL without relying on
repo-private `public/v2/**` paths.

## Warm-reboot / hotload docs

The current in-page route-swap method for routed shells such as
`v2/fui-as/demo/**` is
documented in:

- `docs/v2/browser-bridge/HOTLOAD_METHOD.md`

That doc covers the live state split, what stays hot, what gets recreated on
each route swap, the bridge/harness entry points involved, and the guardrails
that keep warm reboots safe.
