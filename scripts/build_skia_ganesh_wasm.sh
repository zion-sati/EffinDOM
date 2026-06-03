#!/usr/bin/env bash
# scripts/build_skia_ganesh_wasm.sh
#
# Builds Skia (Ganesh/WebGL2 backend only, no Dawn/Graphite) + HarfBuzz for
# wasm32-unknown-emscripten or wasm64-unknown-emscripten, then stages the
# result into skia/wasm-ganesh* (headers + versioned side-module .wasm).
#
# This produces a lean Skia side module with NO WebGPU/Dawn symbols, making
# the effindom.ganesh bundle fully self-contained for WebGL2-only deployments
# (no emdawnwebgpu overhead).
#
# Until this script has been run, build.sh falls back to the shared skia/wasm/
# (graphite-capable) Skia build, which temporarily includes emdawnwebgpu in
# the ganesh bundle for wgpu* symbol resolution.
#
# ── Pinned versions ───────────────────────────────────────────────────────────
#   SKIA_REVISION   (default: chrome/m136)
#       Chrome milestone branch.  For a lean ganesh-only build without Dawn,
#       chrome/m140 is the last milestone with full Ganesh support.
#       Upgrade to chrome/m140 when testing confirms compatibility.
#
# Usage:
#   ./scripts/build_skia_ganesh_wasm.sh          # skips if the selected skia/wasm-ganesh*/ exists
#   ./scripts/build_skia_ganesh_wasm.sh --force  # always rebuild
#
# Environment variables (all optional):
#   EFFINDOM_WASM_ARCH     wasm32 or wasm64    (default: wasm32)
#   SKIA_GANESH_WASM_DIR   Staging destination  (default: <repo>/skia/wasm-ganesh or <repo>/skia/wasm-ganesh64)
#   SKIA_REVISION          Skia git ref         (default: chrome/m136)
#   DEPOT_TOOLS_COMMIT     depot_tools commit   (default: empty = HEAD)
#   SKIA_BUILD_WORKDIR     Scratch space        (default: /tmp/effindom-skia-build)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK_DIR="${SKIA_BUILD_WORKDIR:-/tmp/effindom-skia-build}"

SKIA_REVISION="${SKIA_REVISION:-chrome/m136}"
DEPOT_TOOLS_COMMIT="${DEPOT_TOOLS_COMMIT:-}"
WASM_ARCH="${EFFINDOM_WASM_ARCH:-wasm32}"
FORCE=false

bold()  { printf '\033[1m%s\033[0m\n' "$*"; }
green() { printf '\033[32m%s\033[0m\n' "$*"; }
red()   { printf '\033[31m%s\033[0m\n' "$*"; }

case "${WASM_ARCH}" in
  wasm32)
    STAGING_DEFAULT="${REPO_ROOT}/skia/wasm-ganesh"
    BIN_DIR="out/wasm-ganesh"
    SIDE_MODULE_PREFIX="effindom-core"
    EMCC_ARCH_FLAGS=()
    GN_EXTRA_CFLAGS=$'    "-O3",\n'
    GN_EXTRA_LDFLAGS=$'  extra_ldflags=["-O3"]\n'
    ;;
  wasm64)
    STAGING_DEFAULT="${REPO_ROOT}/skia/wasm-ganesh64"
    BIN_DIR="out/wasm64-ganesh"
    SIDE_MODULE_PREFIX="effindom-core64"
    EMCC_ARCH_FLAGS=(-sMEMORY64=1)
    GN_EXTRA_CFLAGS=$'    "-O3",\n    "-sMEMORY64=1"\n'
    GN_EXTRA_LDFLAGS=$'  extra_ldflags=["-O3", "-sMEMORY64=1"]\n'
    ;;
  *)
    red "ERROR: Unsupported EFFINDOM_WASM_ARCH=${WASM_ARCH}. Use wasm32 or wasm64."
    exit 1
    ;;
esac

STAGING="${SKIA_GANESH_WASM_DIR:-${STAGING_DEFAULT}}"

for arg in "$@"; do
  case "$arg" in
    --force|-f) FORCE=true ;;
    *) echo "Unknown argument: $arg"; exit 1 ;;
  esac
done

content_hash() {
  python3 "${REPO_ROOT}/scripts/content_hash.py" "$1"
}

# ── Skip if already staged ────────────────────────────────────────────────────
if [ "$FORCE" = false ] && [ -f "$STAGING/skia.version" ]; then
  EXISTING_VER=$(cat "$STAGING/skia.version")
  if [ -f "$STAGING/${EXISTING_VER}.wasm" ]; then
    green "=== Skia Ganesh already built at $STAGING (${EXISTING_VER}) — skipping (use --force to rebuild) ==="
    exit 0
  fi
fi

# ── Sanity checks ─────────────────────────────────────────────────────────────
if ! command -v emcc &>/dev/null; then
  red "ERROR: emcc not found. Activate the Emscripten SDK first:"
  echo "  source ~/emsdk/emsdk_env.sh"
  exit 1
fi
for tool in python3 ninja; do
  if ! command -v "$tool" &>/dev/null; then
    red "ERROR: $tool is required."
    exit 1
  fi
done

bold "=== Building Skia Ganesh-only WASM for EffinDom (${WASM_ARCH}) ==="
echo "  Work dir : $WORK_DIR"
echo "  Revision : $SKIA_REVISION"
echo "  Output   : $STAGING"
echo

mkdir -p "$WORK_DIR"

# ── Step 1: depot_tools ───────────────────────────────────────────────────────
DEPOT_TOOLS_DIR="$WORK_DIR/depot_tools"
if [ ! -d "$DEPOT_TOOLS_DIR" ]; then
  bold "-- Cloning depot_tools..."
  if [ -n "$DEPOT_TOOLS_COMMIT" ]; then
    git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git \
      "$DEPOT_TOOLS_DIR"
    git -C "$DEPOT_TOOLS_DIR" checkout "$DEPOT_TOOLS_COMMIT"
  else
    git clone --depth 1 https://chromium.googlesource.com/chromium/tools/depot_tools.git \
      "$DEPOT_TOOLS_DIR"
  fi
fi
export PATH="$DEPOT_TOOLS_DIR:$PATH"

EMCC_BIN="$(command -v emcc)"
EMCC_EMSDK_DIR="$(dirname "$(dirname "$(dirname "$EMCC_BIN")")")"
bold "-- Using Emscripten: $EMCC_BIN ($(${EMCC_BIN} --version 2>&1 | head -1))"

# ── Step 2: Skia source ───────────────────────────────────────────────────────
SKIA_DIR="$WORK_DIR/skia"
if [ ! -d "$SKIA_DIR" ]; then
  bold "-- Cloning Skia..."
  git clone https://skia.googlesource.com/skia.git "$SKIA_DIR"
fi
cd "$SKIA_DIR"

bold "-- Checking out revision: $SKIA_REVISION"
git fetch origin "$SKIA_REVISION"
git checkout FETCH_HEAD

bold "-- Syncing Skia dependencies (python3 tools/git-sync-deps)..."
python3 tools/git-sync-deps

LIBPNG_DIR="${SKIA_DIR}/third_party/externals/libpng"
if [ -f "${LIBPNG_DIR}/scripts/pnglibconf.h.prebuilt" ] && \
   [ ! -f "${LIBPNG_DIR}/pnglibconf.h" ]; then
  cp "${LIBPNG_DIR}/scripts/pnglibconf.h.prebuilt" "${LIBPNG_DIR}/pnglibconf.h"
fi

# ── Emscripten 5.x GL sync signature fix (same as graphite build) ─────────────
AUTOGEN="${SKIA_DIR}/src/gpu/ganesh/gl/GrGLAssembleWebGLInterfaceAutogen.cpp"
if grep -q "GET_PROC(ClientWaitSync)" "$AUTOGEN" 2>/dev/null; then
  python3 - "$AUTOGEN" <<'PYEOF'
import sys, re

path = sys.argv[1]
with open(path) as f:
    src = f.read()

if 'GET_PROC(ClientWaitSync)' not in src and 'GET_PROC(WaitSync)' not in src:
    sys.exit(0)

client_wait_repl = (
    '// emcc5+: emscripten_glClientWaitSync uses GLuint64; Skia expects split (lo,hi).\n'
    '        functions->fClientWaitSync = [](GrGLsync s, GrGLbitfield f,\n'
    '                                        GrGLint lo, GrGLint hi) -> GrGLenum {\n'
    '            GLuint64 t = ((GLuint64)(GLuint)hi << 32) | (GLuint64)(GLuint)lo;\n'
    '            return emscripten_glClientWaitSync((GLsync)(uintptr_t)(unsigned long)s, f, t);\n'
    '        };'
)
wait_repl = (
    '// emcc5+: emscripten_glWaitSync uses GLuint64; Skia expects split (lo,hi).\n'
    '        functions->fWaitSync = [](GrGLsync s, GrGLbitfield f,\n'
    '                                  GrGLuint lo, GrGLuint hi) -> GrGLvoid {\n'
    '            GLuint64 t = ((GLuint64)hi << 32) | (GLuint64)lo;\n'
    '            emscripten_glWaitSync((GLsync)(uintptr_t)(unsigned long)s, f, t);\n'
    '        };'
)

src = src.replace('GET_PROC(ClientWaitSync)', client_wait_repl)
src = src.replace('GET_PROC(WaitSync)', wait_repl)

with open(path, 'w') as f:
    f.write(src)
print("   Applied emcc5+ GL sync wrapper lambdas")
PYEOF
fi

export PATH="$SKIA_DIR/bin:$SKIA_DIR/third_party/gn:$PATH"

if [ -x "${SKIA_DIR}/bin/gn" ]; then
  GN_BIN="${SKIA_DIR}/bin/gn"
elif [ -x "${SKIA_DIR}/third_party/gn/gn" ]; then
  GN_BIN="${SKIA_DIR}/third_party/gn/gn"
else
  red "ERROR: Could not find a usable GN binary in ${SKIA_DIR}."
  red "  Expected ${SKIA_DIR}/bin/gn or ${SKIA_DIR}/third_party/gn/gn."
  exit 1
fi

# ── Step 3: GN build configuration (Ganesh only, no Dawn) ────────────────────
bold "-- Configuring GN build (Ganesh/WebGL2 only, no Dawn/Graphite)..."
GN_ARGS="
  is_official_build=true
  is_component_build=false
  is_debug=false
  target_cpu=\"wasm\"
  skia_use_dawn=false
  skia_enable_graphite=false
  skia_use_gl=true
  skia_use_vulkan=false
  skia_use_metal=false
  skia_use_direct3d=false
  skia_use_angle=false
  skia_use_freetype=true
  skia_use_system_freetype2=false
  skia_use_system_zlib=false
  skia_use_harfbuzz=false
  skia_use_icu=false
  skia_use_libjpeg_turbo_decode=false
  skia_use_libjpeg_turbo_encode=false
  skia_use_libwebp_decode=false
  skia_use_libwebp_encode=false
  skia_use_dng_sdk=false
  skia_enable_skottie=false
  skia_enable_skparagraph=false
  skia_enable_pdf=false
  skia_emsdk_dir=\"${EMCC_EMSDK_DIR}\"
${GN_EXTRA_LDFLAGS}  extra_cflags=[
    \"-fno-rtti\",
    \"-fno-exceptions\",
    \"-I${SKIA_DIR}/third_party/externals/libpng\",
${GN_EXTRA_CFLAGS}
  ]
"

if [ "$FORCE" = true ] && [ -d "$BIN_DIR" ]; then
  bold "-- Cleaning stale build output: $BIN_DIR"
  rm -rf "$BIN_DIR"
fi
"$GN_BIN" gen "$BIN_DIR" --args="$GN_ARGS"

# ── Step 4: Build ──────────────────────────────────────────────────────────────
bold "-- Building (this takes ~30-60 minutes on first run)..."
ninja -C "$BIN_DIR" skia

# ── Step 5: Stage ─────────────────────────────────────────────────────────────
bold "-- Staging output into $STAGING..."
rm -rf "$STAGING"
mkdir -p "$STAGING/include"

cp "$BIN_DIR/libskia.a"           "$STAGING/"

for subdir in core effects gpu ports private config; do
  if [ -d "include/$subdir" ]; then
    cp -r "include/$subdir" "$STAGING/include/"
  fi
done

mkdir -p "$STAGING/modules"
cp -r modules/skcms "$STAGING/modules/"

# ── Step 6: Link WASM side module ─────────────────────────────────────────────
SIDE_MODULE_TMP="${STAGING}/${SIDE_MODULE_PREFIX}.content-hash.tmp.wasm"

bold "-- Linking WASM side module..."
emcc \
  "${EMCC_ARCH_FLAGS[@]}" \
  -sSIDE_MODULE=1 \
  -sEXPORT_ALL=1 \
  -sGL_SUPPORT_AUTOMATIC_ENABLE_EXTENSIONS=1 \
  -O3 \
  --closure 1 \
  -Wl,--whole-archive \
  "${STAGING}/libskia.a" \
  -Wl,--no-whole-archive \
  -Wl,--export-all \
  -Wl,--no-gc-sections \
  -o "${SIDE_MODULE_TMP}"

SIDE_MODULE_HASH="$(content_hash "${SIDE_MODULE_TMP}")"
SIDE_MODULE_NAME="${SIDE_MODULE_PREFIX}-${SIDE_MODULE_HASH}"
mv "${SIDE_MODULE_TMP}" "${STAGING}/${SIDE_MODULE_NAME}.wasm"

echo "${SIDE_MODULE_NAME}" > "${STAGING}/skia.version"

green ""
green "=== Skia Ganesh build complete ==="
green "  Staged to  : $STAGING"
green "  Side module: ${SIDE_MODULE_NAME}.wasm"
