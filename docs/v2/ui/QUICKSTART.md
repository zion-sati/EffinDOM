# EffinDom v2 UI Bite 1 quickstart

This guide is only for the Tier 2 Bite 1 memory-plumbing slice under `v2/ui/`.

## Prerequisites

Install the shared v2 toolchain first:

- [docs/QUICKSTART.md](../../QUICKSTART.md)

Load Emscripten first if it is not already in your shell:

```bash
source ~/emsdk/emsdk_env.sh
```

From the repository root, install JS dependencies:

```bash
npm ci
npx playwright install chromium
```

## Typed TypeScript checks

```bash
npm run lint:v2
npm run typecheck:v2
```

## Native Tier 2 tests

```bash
cmake -S . -B build/build-v2-ui -DCMAKE_BUILD_TYPE=Debug
cmake --build build/build-v2-ui --target effindom_v2_ui_tests --parallel
./build/build-v2-ui/v2/ui/effindom_v2_ui_tests
```

## Wasm build

The bridge smoke uses the real Tier 1 v2 core wasm plus the dedicated Tier 2 ui wasm.

```bash
bash v2/core/scripts/build_wasm_arch.sh
bash v2/ui/scripts/build_wasm_arch.sh
```

That stages:

- `public/v2/core/effindom-core-v2.js`
- `public/v2/core/effindom-core-v2.wasm`
- `public/v2/ui/effindom-ui-v2.js`
- `public/v2/ui/effindom-ui-v2.wasm`
- `public/v2/ui/bridge-harness.js`
- `public/v2/ui/index.html`

### Important: new browser exports need two updates

When you add a new C ABI/browser-callable UI function in `v2/ui`:

1. add the declaration/implementation in the normal Tier 2 ABI/runtime files
2. also add the symbol to `v2/ui/CMakeLists.txt` `-sEXPORTED_FUNCTIONS=[...]`

If step 2 is missing, native tests can still pass while the browser bridge fails later with JS-side errors like `runtime.ui._ui_some_new_function is not a function`.

The safest validation entrypoint after any new browser-facing `v2/ui` export is:

```bash
npm run build:v2:browser-bridge
```

## Browser smoke

```bash
npx playwright test -c v2/ui/playwright.config.ts
```

The smoke screenshot is written to:

- `v2/ui/tests/integration/screenshots/chromium-ui-bridge-smoke.png`

## One-shot build

```bash
bash v2/ui/scripts/build_all.sh
```
