# EffinDom Runtime Quickstart

The EffinDom runtime is a standalone, MIT-licensed WebAssembly runtime that provides the core UI painting, layout, and interaction primitives for building Tier 3 SDKs (like FUI-AS).

## Prerequisites

You need:

- **Node.js 24+** and npm
- **CMake 3.20+** and **Ninja**
- **Python 3** and **pip**
- **Git**
- **Bash 5.x**
- **Emscripten 5.0.6** from `~/emsdk` (for WebAssembly builds)
- **Xcode Command Line Tools** (macOS) or build-essential (Linux)

### Install on macOS

```bash
brew install cmake ninja python3 node git bash
xcode-select --install
```

### Install Emscripten SDK

```bash
git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
cd ~/emsdk
./emsdk install 5.0.6
./emsdk activate 5.0.6
source ~/emsdk/emsdk_env.sh
```

## Build

### Step 1: Install dependencies

```bash
npm install
```

### Step 2: Build the runtime

`npm run build` now bootstraps Skia automatically if it is missing, then runs type-check + package build + npm pack dry-run:

```bash
npm run build
```

This script will:
1. Clone `depot_tools` (progress shown per file)
2. Clone and checkout Skia from the pinned Chrome milestone (3–5 minutes, shows clone progress)
3. Sync Skia dependencies with `git-sync-deps` (5–10 minutes, will show progress)
4. Configure the GN build system
5. Build the ganesh-only Skia libraries with Ninja (30–60 minutes, shows compilation progress)
6. Stage the built artifacts into `skia/wasm-ganesh/`

The script prints progress for each step, so you can monitor its status.

The built runtime package assets are output to `v2/browser-bridge/dist/` (including `effindom.v2.manifest.json`, `runtime/*`, and bundled fonts under `dist/fonts/`).

### Rebuilding Skia

To rebuild Skia in the future (e.g., to upgrade versions), use:

```bash
bash scripts/build_skia_ganesh_wasm.sh --force
```

## Publish locally (for testing)

```bash
npm run publish:local
```

This packages the runtime and outputs it to the `published/` directory without pushing to npm.

## API documentation

See `docs/v2/browser-bridge/` for bridge API and host import contracts.

See `docs/v2/core/` for Tier 1 retained UI ABI reference.
