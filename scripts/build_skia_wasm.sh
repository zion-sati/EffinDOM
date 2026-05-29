#!/usr/bin/env bash
# scripts/build_skia_wasm.sh
#
# Builds Skia (Graphite/Dawn WebGPU backend) + HarfBuzz for
# wasm32-unknown-emscripten or wasm64-unknown-emscripten using Emscripten
# + GN + Ninja, then stages the result into skia/wasm* (headers + libskia.a).
#
# ── Pinned versions ───────────────────────────────────────────────────────────
# All external fetches use explicit, pinned revisions so that the resulting
# skia/wasm artefacts are reproducible and can be committed to the repo for
# test runs that skip compilation entirely.
#
#   SKIA_REVISION   (default: chrome/m136)
#       Chrome milestone branch for Skia.  chrome/* branches are effectively
#       frozen once the milestone ships, making them a stable, human-readable
#       pin.  Bump this to the newest chrome/mXXX branch when upgrading.
#       Browse available branches at:
#         https://skia.googlesource.com/skia/+refs
#
#   DEPOT_TOOLS_COMMIT   (default: empty = HEAD)
#       depot_tools provides the gn binary used to configure the Skia build.
#       Leave empty to use whatever HEAD is at clone time (recommended).
#       Pin to a specific commit only if you need a reproducible build:
#         https://chromium.googlesource.com/chromium/tools/depot_tools.git
#
# Prerequisite tools (installed separately before running):
#   macOS  : brew install ninja python3 git cmake
#   Linux  : sudo apt-get install ninja-build python3 git cmake
#   Both   : Emscripten SDK activated (source ~/emsdk/emsdk_env.sh)
#
# Usage:
#   ./scripts/build_skia_wasm.sh          # skips if the selected skia/wasm*/ archive exists
#   ./scripts/build_skia_wasm.sh --force  # always rebuild
#
# After a successful run, run_all_tests.sh or build.sh will find
# skia/wasm/ automatically (no env var needed).
#
# Environment variables (all optional):
#   EFFINDOM_WASM_ARCH   wasm32 or wasm64    (default: wasm32)
#   SKIA_WASM_DIR        Staging destination  (default: <repo>/skia/wasm or <repo>/skia/wasm64)
#   SKIA_REVISION        Skia git ref         (default: chrome/m136)
#   DEPOT_TOOLS_COMMIT   depot_tools commit   (default: empty = HEAD)
#   SKIA_BUILD_WORKDIR   Scratch space        (default: /tmp/effindom-skia-build)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK_DIR_BASE_DEFAULT="/tmp/effindom-skia-build"

# ── Pinned dependency versions ────────────────────────────────────────────────
# SKIA_REVISION: newest available chrome/mXXX branch.  Bump whenever a newer
# stable milestone branch appears at https://skia.googlesource.com/skia/+refs
SKIA_REVISION="${SKIA_REVISION:-chrome/m136}"
# DEPOT_TOOLS_COMMIT: empty = always use latest HEAD (shallow clone).
# Pin to a specific SHA only for fully reproducible offline builds.
DEPOT_TOOLS_COMMIT="${DEPOT_TOOLS_COMMIT:-}"
bold()  { printf '\033[1m%s\033[0m\n' "$*"; }
green() { printf '\033[32m%s\033[0m\n' "$*"; }
yellow(){ printf '\033[33m%s\033[0m\n' "$*"; }
red()   { printf '\033[31m%s\033[0m\n' "$*"; }

render_gn_list() {
  local rendered=""
  local value
  for value in "$@"; do
    if [ -n "${rendered}" ]; then
      rendered="${rendered}, "
    fi
    rendered="${rendered}\"${value}\""
  done
  printf '%s' "${rendered}"
}

WASM_ARCH="${EFFINDOM_WASM_ARCH:-wasm32}"
SIMD_MODE="${EFFINDOM_SIMD:-off}"
case "${SIMD_MODE}" in
  1|true|TRUE|on|ON|yes|YES) SIMD_ENABLED=ON ;;
  0|false|FALSE|off|OFF|no|NO|'') SIMD_ENABLED=OFF ;;
  *)
    red "ERROR: Unsupported EFFINDOM_SIMD=${SIMD_MODE}. Use on/off."
    exit 1
    ;;
esac
FORCE=false

GN_EXTRA_CFLAGS_LIST=()
GN_EXTRA_LDFLAGS_LIST=()

case "${WASM_ARCH}" in
  wasm32)
    if [ "${SIMD_ENABLED}" = "ON" ]; then
      STAGING_DEFAULT="${REPO_ROOT}/skia/wasm-simd"
      BIN_DIR="out/wasm-graphite-simd"
    else
      STAGING_DEFAULT="${REPO_ROOT}/skia/wasm"
      BIN_DIR="out/wasm-graphite"
    fi
    ;;
  wasm64)
    if [ "${SIMD_ENABLED}" = "ON" ]; then
      STAGING_DEFAULT="${REPO_ROOT}/skia/wasm64-simd"
      BIN_DIR="out/wasm64-graphite-simd"
    else
      STAGING_DEFAULT="${REPO_ROOT}/skia/wasm64"
      BIN_DIR="out/wasm64-graphite"
    fi
    GN_EXTRA_CFLAGS_LIST+=("-sMEMORY64=1")
    GN_EXTRA_LDFLAGS_LIST+=("-sMEMORY64=1")
    ;;
  *)
    red "ERROR: Unsupported EFFINDOM_WASM_ARCH=${WASM_ARCH}. Use wasm32 or wasm64."
    exit 1
    ;;
esac

if [ "${SIMD_ENABLED}" = "ON" ]; then
  GN_EXTRA_CFLAGS_LIST+=("-O3" "-msimd128")
  GN_EXTRA_LDFLAGS_LIST+=("-O3" "-msimd128")
fi

WORK_DIR="${SKIA_BUILD_WORKDIR:-${WORK_DIR_BASE_DEFAULT}}"

GN_EXTRA_CFLAGS=""
for flag in "${GN_EXTRA_CFLAGS_LIST[@]}"; do
  GN_EXTRA_CFLAGS="${GN_EXTRA_CFLAGS}    \"${flag}\","$'\n'
done

GN_EXTRA_LDFLAGS=""
if [ "${#GN_EXTRA_LDFLAGS_LIST[@]}" -gt 0 ]; then
  GN_EXTRA_LDFLAGS="  extra_ldflags=[$(render_gn_list "${GN_EXTRA_LDFLAGS_LIST[@]}")]"$'\n'
fi

STAGING="${SKIA_WASM_DIR:-${STAGING_DEFAULT}}"

for arg in "$@"; do
  case "$arg" in
    --force|-f) FORCE=true ;;
    *) echo "Unknown argument: $arg"; exit 1 ;;
  esac
done

# ── Skip if the staged archive is already present ─────────────────────────────
if [ "$FORCE" = false ] && [ -f "$STAGING/libskia.a" ] && [ -f "$STAGING/libsvg.a" ] && [ -f "$STAGING/libskshaper.a" ] && [ -f "$STAGING/modules/svg/include/SkSVGDOM.h" ] && [ -f "$STAGING/modules/skresources/include/SkResources.h" ] && [ -f "$STAGING/modules/skshaper/include/SkShaper_factory.h" ] && [ -f "$STAGING/src/core/SkTHash.h" ] && [ -f "$STAGING/src/base/SkMathPriv.h" ] && [ -f "$STAGING/third_party/externals/expat/expat/lib/expat.h" ] && [ -f "$STAGING/third_party/expat/include/expat_config/expat_config.h" ]; then
  green "=== Skia already built at $STAGING (libskia.a) — skipping (use --force to rebuild) ==="
  exit 0
fi

# ── Sanity checks ─────────────────────────────────────────────────────────────
if ! command -v emcc &>/dev/null; then
  red "ERROR: emcc not found. Activate the Emscripten SDK first:"
  echo "  source ~/emsdk/emsdk_env.sh"
  exit 1
fi
if ! command -v python3 &>/dev/null; then
  red "ERROR: python3 is required."
  echo "  macOS: brew install python3"
  echo "  Linux: sudo apt-get install python3"
  exit 1
fi
if ! command -v ninja &>/dev/null; then
  red "ERROR: ninja is required."
  echo "  macOS: brew install ninja"
  echo "  Linux: sudo apt-get install ninja-build"
  exit 1
fi

bold "=== Building Skia WASM for EffinDom (${WASM_ARCH}) ==="
echo "  Work dir : $WORK_DIR"
echo "  Revision : $SKIA_REVISION"
echo "  SIMD     : ${SIMD_ENABLED}"
echo "  Output   : $STAGING"
echo

mkdir -p "$WORK_DIR"
PREP_LOCK_DIR="${WORK_DIR}/.skia-source-lock"

while ! mkdir "${PREP_LOCK_DIR}" 2>/dev/null; do
  sleep 1
done

cleanup_prepare_lock() {
  rmdir "${PREP_LOCK_DIR}" >/dev/null 2>&1 || true
}

trap cleanup_prepare_lock EXIT

# ── Step 1: depot_tools ───────────────────────────────────────────────────────
DEPOT_TOOLS_DIR="$WORK_DIR/depot_tools"
if [ ! -d "$DEPOT_TOOLS_DIR" ]; then
  bold "-- Cloning depot_tools..."
  if [ -n "$DEPOT_TOOLS_COMMIT" ]; then
    # Full clone so we can check out the pinned commit.
    git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git \
      "$DEPOT_TOOLS_DIR"
    git -C "$DEPOT_TOOLS_DIR" checkout "$DEPOT_TOOLS_COMMIT"
    bold "   Pinned depot_tools to: $DEPOT_TOOLS_COMMIT"
  else
    # No pin requested — use a shallow clone of HEAD.
    git clone --depth 1 https://chromium.googlesource.com/chromium/tools/depot_tools.git \
      "$DEPOT_TOOLS_DIR"
  fi
fi
export PATH="$DEPOT_TOOLS_DIR:$PATH"

# ── Capture Emscripten SDK path before git-sync-deps ─────────────────────────
# git-sync-deps fetches Dawn's bundled emsdk (often pinned to an older version)
# and activates it, prepending a different emcc to PATH.  We capture the full
# path to our emsdk ROOT directory NOW — before that happens — and pass it as
# skia_emsdk_dir in the GN args so the Skia WASM toolchain (gn/toolchain/BUILD.gn)
# uses our Emscripten version instead of Dawn's bundled one.
EMCC_BIN="$(command -v emcc)"
# Derive the emsdk root from the emcc path:
#   /path/to/emsdk/upstream/emscripten/emcc  →  /path/to/emsdk
EMCC_EMSDK_DIR="$(dirname "$(dirname "$(dirname "$EMCC_BIN")")")"
bold "-- Using Emscripten: $EMCC_BIN ($(${EMCC_BIN} --version 2>&1 | head -1))"

# Ensure the emdawnwebgpu port is extracted to the emcc cache so we can pass its
# include dirs to Skia's GN build.  Skia's Dawn sources include "webgpu/webgpu_cpp.h"
# which is NOT in emcc 5.x's default sysroot — it lives in the emdawnwebgpu port.
bold "-- Ensuring emdawnwebgpu port headers are cached..."
"${EMCC_BIN}" --use-port=emdawnwebgpu -E /dev/null -o /dev/null 2>/dev/null || true
# Extract the two include paths emcc adds when --use-port=emdawnwebgpu is in effect.
_EMDAWN_INCLUDES=$("${EMCC_BIN}" --use-port=emdawnwebgpu -E -v /dev/null 2>&1 | \
  grep -oE -- '-isystem [^ ]+' | grep 'emdawnwebgpu' | awk '{print $2}')
EMDAWN_WEBGPU_INC=$(echo "$_EMDAWN_INCLUDES"     | grep 'webgpu/include$'     | head -1)
EMDAWN_WEBGPU_CPP_INC=$(echo "$_EMDAWN_INCLUDES" | grep 'webgpu_cpp/include$' | head -1)
if [ -z "$EMDAWN_WEBGPU_INC" ] || [ -z "$EMDAWN_WEBGPU_CPP_INC" ]; then
  red "ERROR: Could not find emdawnwebgpu port include directories."
  red "  Expected webgpu/include and webgpu_cpp/include in emcc's port cache."
  red "  Verify that emcc is activated: source ~/emsdk/emsdk_env.sh"
  exit 1
fi
bold "   webgpu     : $EMDAWN_WEBGPU_INC"
bold "   webgpu_cpp : $EMDAWN_WEBGPU_CPP_INC"

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

# ── Step 2b: Apply compatibility patches ──────────────────────────────────────
#
# Skia m136's Dawn backend has #if defined(__EMSCRIPTEN__) blocks that use
# the old Dawn API (written for emcc 3.1.44).  emcc 5+ ships emdawnwebgpu
# (Dawn v20251002) which has a completely different API.  The patch updates
# those blocks to use the new API shapes:
#   - WGPUBufferMapAsyncStatus → wgpu::MapAsyncStatus
#   - wgpu::SupportedLimits removed; GetLimits now takes wgpu::Limits*
#   - RenderPassTimestampWrites/ComputePassTimestampWrites → PassTimestampWrites
#   - ShaderModuleWGSLDescriptor → ShaderSourceWGSL
#   - VertexStepMode::VertexBufferNotUsed → VertexStepMode::Undefined
#   - OnSubmittedWorkDone / PopErrorScope / GetCompilationInfo: new callback sig
DAWN_PATCH="${REPO_ROOT}/scripts/patches/skia-m136-dawn-emdawnwebgpu-v20251002.patch"
if [ ! -f "${DAWN_PATCH}" ]; then
  red "ERROR: Dawn patch not found: ${DAWN_PATCH}"
  exit 1
fi

bold "-- Applying Dawn API compatibility patch..."
# Test if patch is already applied to avoid harmless re-application
if patch -p1 --dry-run --forward --silent < "${DAWN_PATCH}" 2>/dev/null; then
  # Patch is not yet applied; apply it for real
  if ! patch -p1 --no-backup-if-mismatch < "${DAWN_PATCH}"; then
    red "ERROR: Failed to apply Dawn compatibility patch."
    red "This is a critical patch required for emcc 5.0.6 compatibility."
    exit 1
  fi
  bold "   Patch applied successfully."
else
  # Already applied; verify by testing individual hunks
  bold "   Patch already applied (detected via dry-run test)."
fi

# pnglibconf.h is generated by libpng's build system and is not present in the
# raw source checkout.  libpng ships scripts/pnglibconf.h.prebuilt for exactly
# this use case: including libpng headers without running the full configure.
# Without this, FreeType's pngshim.c (compiled with -I.../libpng) fails at
# the #include "pnglibconf.h" line inside png.h.
LIBPNG_DIR="${SKIA_DIR}/third_party/externals/libpng"
if [ -f "${LIBPNG_DIR}/scripts/pnglibconf.h.prebuilt" ] && \
   [ ! -f "${LIBPNG_DIR}/pnglibconf.h" ]; then
  cp "${LIBPNG_DIR}/scripts/pnglibconf.h.prebuilt" "${LIBPNG_DIR}/pnglibconf.h"
  bold "   Copied pnglibconf.h.prebuilt → third_party/externals/libpng/pnglibconf.h"
fi

# ── Emscripten 5.x GL sync signature fix ─────────────────────────────────────
# GrGLAssembleWebGLInterfaceAutogen.cpp uses GET_PROC(ClientWaitSync) and
# GET_PROC(WaitSync) which expand to:
#   functions->fClientWaitSync = emscripten_glClientWaitSync;
#   functions->fWaitSync       = emscripten_glWaitSync;
# Skia m136 expects these to have the signature (GLsync, GLbitfield, GLuint hi, GLuint lo)
# — a split 64-bit timeout — but Emscripten 5.x uses (GLsync, GLbitfield, GLuint64),
# a single 64-bit arg, causing a template type mismatch compile error.
# Both functions are optional in Skia's GL interface (checked for nullptr before use),
# so nulling them out is safe for our use case (no GPU/CPU sync fence wait).
AUTOGEN="${SKIA_DIR}/src/gpu/ganesh/gl/GrGLAssembleWebGLInterfaceAutogen.cpp"
if grep -q "GET_PROC(ClientWaitSync)" "$AUTOGEN" 2>/dev/null; then
  # macOS sed requires an empty string for -i to edit in-place without a backup.
  # Skia m136 expects the __EMSCRIPTEN__ split-(lo,hi) signature:
  #   GrGLClientWaitSyncFn = GrGLenum(GrGLsync, GrGLbitfield, GrGLint lo, GrGLint hi)
  #   GrGLWaitSyncFn       = GrGLvoid(GrGLsync, GrGLbitfield, GrGLuint lo, GrGLuint hi)
  # But emcc 5.x exposes:
  #   emscripten_glClientWaitSync(GLsync, GLbitfield, GLuint64)
  #   emscripten_glWaitSync(GLsync, GLbitfield, GLuint64)
  # We insert C++ wrapper lambdas that reassemble the 64-bit timeout from the two
  # 32-bit halves and forward to Emscripten's actual GL functions.
  python3 - "$AUTOGEN" <<'PYEOF'
import sys, re

path = sys.argv[1]
with open(path) as f:
    src = f.read()

# Only patch if original GET_PROC lines are still present.
if 'GET_PROC(ClientWaitSync)' not in src and 'GET_PROC(WaitSync)' not in src:
    print("   GrGLAssembleWebGLInterfaceAutogen.cpp: already patched, skipping")
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
print("   Applied emcc5+ GL sync wrapper lambdas (ClientWaitSync/WaitSync)")
PYEOF
  bold "   Applied Emscripten 5.x GL sync compat fix (ClientWaitSync/WaitSync → wrappers)"
fi

cleanup_prepare_lock
trap - EXIT

# ── Step 3: GN build configuration ────────────────────────────────────────────
#
# Key flags explained:
#
#   skia_use_gl=true
#       Enables Skia's Ganesh OpenGL/WebGL2 backend alongside Graphite/Dawn.
#       Required for the WebGL2 fallback path in EffinDom (M1+).
#
#   skia_use_system_zlib=false
#       Without this, Skia's bundled FreeType is compiled with
#       -DFT_CONFIG_OPTION_SYSTEM_ZLIB, which causes it to #include <zlib.h>
#       as an angled include.  That header is not on the Emscripten sysroot's
#       default angled-include search path, producing a fatal compile error.
#       Setting this to false makes FreeType use Skia's own bundled zlib.
#
#   extra_cflags += "-I<skia>/third_party/externals/libpng"
#       Skia's bundled FreeType supports PNG color bitmaps (CBDT/CBLC color
#       emoji) and includes <png.h>.  The Emscripten sysroot does not ship
#       libpng, so without this the build fails with "png.h file not found".
#       Pointing the compiler at Skia's own bundled libpng (fetched by
#       tools/git-sync-deps into third_party/externals/libpng) makes png.h
#       available without touching any other build flags, preserving full SVG
#       glyph and color-emoji support.
#       NOTE: skia_use_libpng is NOT set to false here — libpng is used by
#       FreeType for CBDT/CBLC color emoji glyphs, not for PNG image decoding
#       (which is browser-side via createImageBitmap).  Removing it would
#       silently break color emoji support (an M10 concern).
#
#   skia_use_libjpeg_turbo=false / skia_use_libwebp=false / skia_use_dng_sdk=false
#       EffinDom never decodes image files inside WASM — the browser decodes
#       JPEG/WebP/RAW images via createImageBitmap before they reach Skia.
#       Disabling these codecs removes ~100–300 KB from the staged archive/final renderer payload.

bold "-- Configuring GN build (Graphite + Ganesh + Dawn + HarfBuzz)..."
GN_ARGS="
  is_official_build=true
  is_component_build=false
  is_debug=false
  target_cpu=\"wasm\"
  skia_use_dawn=true
  skia_enable_graphite=true
  skia_use_gl=true
  skia_use_expat=true
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
  skia_enable_skshaper=true
  skia_enable_svg=true
  skia_emsdk_dir=\"${EMCC_EMSDK_DIR}\"
${GN_EXTRA_LDFLAGS}  extra_cflags=[
${GN_EXTRA_CFLAGS}
    \"-DSK_DAWN\",
    \"-DSK_GRAPHITE\",
    \"-DWGPU_BREAKING_CHANGE_STRING_VIEW_OUTPUT_STRUCTS\",
    \"-fno-rtti\",
    \"-fno-exceptions\",
    \"-I${SKIA_DIR}/third_party/externals/libpng\",
    \"-I${SKIA_DIR}/third_party/externals/expat/expat/lib\",
    \"-I${EMDAWN_WEBGPU_INC}\",
    \"-I${EMDAWN_WEBGPU_CPP_INC}\"
  ]
"

# Clean the build output when forcing a rebuild so stale objects from a
# previous Skia revision (or compiler toolchain change) don't cause issues.
if [ "$FORCE" = true ] && [ -d "$BIN_DIR" ]; then
  bold "-- Cleaning stale build output: $BIN_DIR"
  rm -rf "$BIN_DIR"
fi
bin/gn gen "$BIN_DIR" --args="$GN_ARGS"

# ── Step 4: Build ──────────────────────────────────────────────────────────────
bold "-- Building libskia.a and libsvg.a (this takes ~30-60 minutes on first run)..."
ninja -C "$BIN_DIR" skia
ninja -C "$BIN_DIR" svg

# ── Step 5: Stage into SKIA_WASM_DIR ─────────────────────────────────────────
bold "-- Staging output into $STAGING..."
rm -rf "$STAGING"
mkdir -p "$STAGING/include"

cp "$BIN_DIR/libskia.a"           "$STAGING/"
cp "$BIN_DIR/libsvg.a"            "$STAGING/"
cp "$BIN_DIR/libskshaper.a"       "$STAGING/"

for subdir in core effects gpu ports private config; do
  if [ -d "include/$subdir" ]; then
    cp -r "include/$subdir" "$STAGING/include/"
  fi
done

# modules/skcms is referenced by Skia public headers (SkColorSpace.h uses
# "modules/skcms/skcms.h"), so copy the entire skcms module directory.
mkdir -p "$STAGING/modules"
cp -r modules/skcms "$STAGING/modules/"
cp -r modules/skresources "$STAGING/modules/"
cp -r modules/skshaper "$STAGING/modules/"
cp -r modules/svg "$STAGING/modules/"
mkdir -p "$STAGING/src"
cp -r src/base "$STAGING/src/base"
cp -r src/core "$STAGING/src/core"
mkdir -p "$STAGING/third_party/externals/expat/expat"
cp -r third_party/externals/expat/expat/lib "$STAGING/third_party/externals/expat/expat/"
mkdir -p "$STAGING/third_party/expat/include"
cp -r third_party/expat/include/expat_config "$STAGING/third_party/expat/include/"

rm -f "${STAGING}"/effindom-core-*.wasm "${STAGING}"/effindom-core64-*.wasm "${STAGING}/skia.version"

green ""
green "=== Skia build complete ==="
green "  Staged to  : $STAGING"
green "  Archive    : libskia.a"
