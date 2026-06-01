# EffinDom v2 Core Quickstart

This guide is the fast path for working on **Tier 1 only** under `v2/core/`.

## What this covers

- the native Tier 1 build
- the real wasm/browser build for `public/v2/core/`
- the v2/core typed TypeScript lint and typecheck lane
- the four-layer Tier 1 validation flow

For the Tier 1 ABI and test strategy, read these first:

- [Architecture](./ARCHITECTURE.md)
- [Test pyramid](./TEST_PYRAMID.md)
- [Plan](./plan.md)
- [Architecture positioning](./ARCHITECTURE.md#positioning-why-this-is-not-a-game-engine-or-a-mobile-port-runtime)

## 1. Install prerequisites

You need:

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

For the top-level v2 build entrypoint, see [docs/QUICKSTART.md](../../QUICKSTART.md).
For the v2 FUI-AS docs hub, see [docs/v2/fui-as/SDK_INDEX.md](../fui-as/SDK_INDEX.md).
For the v2 FUI-AS public SDK surface, see [docs/v2/fui-as/API_REFERENCE.md](../fui-as/API_REFERENCE.md).
For the v2 FUI-AS control/node guide, see [docs/v2/fui-as/CONTROLS_AND_NODES.md](../fui-as/CONTROLS_AND_NODES.md).
For v2 FUI-AS semantic defaults and a11y contracts, see [docs/v2/fui-as/ACCESSIBILITY_AND_SEMANTICS.md](../fui-as/ACCESSIBILITY_AND_SEMANTICS.md).
For per-type control/node pages, see [docs/v2/fui-as/reference/README.md](../fui-as/reference/README.md).
For callback/event contracts, see [docs/v2/fui-as/EVENTS_AND_CALLBACKS.md](../fui-as/EVENTS_AND_CALLBACKS.md).
For control theming/override precedence, see [docs/v2/fui-as/THEMING_STYLE_MATRIX.md](../fui-as/THEMING_STYLE_MATRIX.md).

## 3. Build Skia support once per wasm architecture

`v2/core` reuses the repo's production wasm Skia staging flow. Use `--force` flag to do a clean build.

```bash
./scripts/build_skia_wasm.sh
EFFINDOM_WASM_ARCH=wasm64 ./scripts/build_skia_wasm.sh
```

## 4. Run the full Tier 1 build-all flow

This is the preferred entry point for Tier 1 work:

```bash
./v2/core/scripts/build_all.sh
```

It runs, in order:

1. v2 typed TypeScript linting (`typescript-eslint`)
2. v2 TypeScript typecheck
3. native Tier 1 configure/build
4. native tests and fuzz smoke
5. golden generation
6. real wasm bundle build for `public/v2/core/`
7. isolated Playwright smoke for v2/core

## 5. Run individual lanes when you only need one

### Typed TypeScript validation

```bash
npm run lint:v2
npm run typecheck:v2
```

### Native Tier 1 tests

```bash
cmake -S . -B build/build-v2-core -DCMAKE_BUILD_TYPE=Debug
cmake --build build/build-v2-core --target effindom_v2_core_tests --parallel
./build/build-v2-core/v2/core/effindom_v2_core_tests
```

### Golden generation

```bash
cmake --build build/build-v2-core --target effindom_v2_core_generate_goldens --parallel
./build/build-v2-core/v2/core/effindom_v2_core_generate_goldens
```

### Wasm/browser build

```bash
./v2/core/scripts/build_wasm_arch.sh
```

### Browser smoke

```bash
npx playwright test -c v2/core/playwright.config.ts
```

## Artifacts

- **Level 2 goldens:** `v2/core/tests/goldens/`
- **Background blur golden:** `v2/core/tests/goldens/background-blur-scene.png`
- **Level 4 screenshot:** `v2/core/tests/integration/screenshots/chromium-core-smoke.png`
- **Browser harness:** `public/v2/core/index.html`
- **Tier 1 wasm bundle:** `public/v2/core/effindom-core-v2.{js,wasm}`
