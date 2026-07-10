# EffinDom v2 Quickstart

This is the top-level quickstart for the **v2-only** stack.

Use this page to:

1. install the shared prerequisites once
2. run the full v2 rebuild via `build.sh`
3. jump to the surface-specific quickstart you need

## 1. Install shared prerequisites

You need:

- Bash 5.x
- Node.js 24+
- npm / npx
- CMake
- Ninja
- Python 3
- Git
- **Emscripten 5.0.6** from `~/emsdk`
- Playwright browser binaries (for browser smoke lanes)

### macOS

```bash
brew install cmake ninja python3 node git bash binaryen
xcode-select --install
```

### Linux (Debian / Ubuntu)

```bash
sudo apt-get update
sudo apt-get install -y cmake ninja-build python3 python3-pip git build-essential curl bash binaryen
curl -fsSL https://deb.nodesource.com/setup_24.x | sudo -E bash -
sudo apt-get install -y nodejs
```

### Emscripten SDK (required)

```bash
git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
cd ~/emsdk
./emsdk install 5.0.6
./emsdk activate 5.0.6
source ~/emsdk/emsdk_env.sh
```

### JavaScript dependencies

From the repository root:

```bash
npm ci
npx playwright install chromium
```

### Optional: Rust toolchain for `v2/fui-rs`

`build.sh` can complete without Rust (it skips `v2/fui-rs` if `cargo` is missing), but install Rust if you want the Rust SDK lane:

#### macOS

```bash
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source "$HOME/.cargo/env"
rustup target add wasm32-unknown-unknown
```

`binaryen` provides `wasm-opt`, which `v2/fui-rs` uses when available to run a speed-oriented WebAssembly optimization pass after Rust builds.

#### Linux (Debian / Ubuntu)

```bash
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source "$HOME/.cargo/env"
rustup target add wasm32-unknown-unknown
```

`binaryen` provides `wasm-opt`, which `v2/fui-rs` uses when available to run a speed-oriented WebAssembly optimization pass after Rust builds.

## 2. Build the whole v2 stack

From the repository root:

```bash
./build.sh --rebuild-all
# optional: build and run the full v2 test sweep
./build.sh --rebuild-all --with-tests
```

This builds:

- `v2/core`
- `v2/ui`
- `v2/browser-bridge`
- `v2/fui-as`
- `v2/fui-rs` (only when `cargo` is available)

## 3. Surface quickstarts

- [v2 Core quickstart](./v2/core/QUICKSTART.md)
- [v2 architecture positioning](./v2/core/ARCHITECTURE.md#positioning-why-this-is-not-a-game-engine-or-a-mobile-port-runtime)
- [v2 UI quickstart](./v2/ui/QUICKSTART.md)
- [v2 Browser Bridge quickstart](./v2/browser-bridge/QUICKSTART.md)
- [v2 Browser Bridge DevTools DOM Mirror](./v2/browser-bridge/DEVTOOLS_DOM_MIRROR.md)
- [v2 FUI AssemblyScript quickstart](./v2/fui-as/QUICKSTART.md)
- [v2 FUI AssemblyScript app scaffolding (`npm create @effindomv2/fui-as-app`)](./v2/fui-as/QUICKSTART.md#scaffold-a-new-app)
- [v2 FUI AssemblyScript SDK docs index](./v2/fui-as/SDK_INDEX.md)
- [v2 FUI AssemblyScript API reference](./v2/fui-as/API_REFERENCE.md)
- [v2 FUI AssemblyScript controls and nodes](./v2/fui-as/CONTROLS_AND_NODES.md)
- [v2 FUI AssemblyScript accessibility and semantics](./v2/fui-as/ACCESSIBILITY_AND_SEMANTICS.md)
- [v2 FUI AssemblyScript per-type reference](./v2/fui-as/reference/README.md)
- [v2 FUI AssemblyScript events and callbacks](./v2/fui-as/EVENTS_AND_CALLBACKS.md)
- [v2 FUI AssemblyScript theming and style matrix](./v2/fui-as/THEMING_STYLE_MATRIX.md)
- [v2 FUI Rust quickstart](./v2/fui-rs/QUICKSTART.md)
