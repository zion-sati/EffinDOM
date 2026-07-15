#!/usr/bin/env bash

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK_DIR="${SKIA_BUILD_WORKDIR:-/tmp/effindom-skia-native-build}"
SKIA_REVISION="${SKIA_REVISION:-chrome/m136}"
DEPOT_TOOLS_COMMIT="${DEPOT_TOOLS_COMMIT:-}"
STAGING="${SKIA_NATIVE_DIR:-${REPO_ROOT}/skia/native}"
BIN_DIR="out/native-metal"
BACKEND_ID="native-ganesh-metal-${SKIA_REVISION}"
BACKEND_STAMP="${STAGING}/.effindom-skia-backend"
FORCE=false

bold()  { printf '\033[1m%s\033[0m\n' "$*"; }
green() { printf '\033[32m%s\033[0m\n' "$*"; }
red()   { printf '\033[31m%s\033[0m\n' "$*"; }

for arg in "$@"; do
  case "$arg" in
    --force|-f) FORCE=true ;;
    *) echo "Unknown argument: $arg" >&2; exit 1 ;;
  esac
done

if [ "$FORCE" = false ] && [ -f "${BACKEND_STAMP}" ] && [ "$(cat "${BACKEND_STAMP}")" = "${BACKEND_ID}" ] && [ -f "${STAGING}/libskia.a" ] && [ -f "${STAGING}/libsvg.a" ] && [ -f "${STAGING}/libskshaper.a" ] && [ -f "${STAGING}/modules/svg/include/SkSVGDOM.h" ] && [ -f "${STAGING}/modules/skresources/include/SkResources.h" ] && [ -f "${STAGING}/modules/skshaper/include/SkShaper_factory.h" ] && [ -f "${STAGING}/src/core/SkTHash.h" ] && [ -f "${STAGING}/src/base/SkMathPriv.h" ]; then
  green "=== Native Skia already staged at ${STAGING} — skipping ==="
  exit 0
fi

for tool in python3 git ninja; do
  if ! command -v "${tool}" >/dev/null 2>&1; then
    red "ERROR: ${tool} is required to build native Skia."
    exit 1
  fi
done

mkdir -p "${WORK_DIR}"

DEPOT_TOOLS_DIR="${WORK_DIR}/depot_tools"
if [ ! -d "${DEPOT_TOOLS_DIR}" ]; then
  bold "-- Cloning depot_tools..."
  if [ -n "${DEPOT_TOOLS_COMMIT}" ]; then
    git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git "${DEPOT_TOOLS_DIR}"
    git -C "${DEPOT_TOOLS_DIR}" checkout "${DEPOT_TOOLS_COMMIT}"
  else
    git clone --depth 1 https://chromium.googlesource.com/chromium/tools/depot_tools.git "${DEPOT_TOOLS_DIR}"
  fi
fi
export PATH="${DEPOT_TOOLS_DIR}:${PATH}"

bold "-- Bootstrapping depot_tools..."
if [ -x "${DEPOT_TOOLS_DIR}/ensure_bootstrap" ]; then
  "${DEPOT_TOOLS_DIR}/ensure_bootstrap" >/dev/null 2>&1 || true
fi
if command -v gclient >/dev/null 2>&1; then
  gclient --version >/dev/null 2>&1 || true
fi

SKIA_DIR="${WORK_DIR}/skia"
if [ ! -d "${SKIA_DIR}" ]; then
  bold "-- Cloning Skia..."
  git clone https://skia.googlesource.com/skia.git "${SKIA_DIR}"
fi

cd "${SKIA_DIR}"
bold "-- Checking out ${SKIA_REVISION}..."
git fetch origin "${SKIA_REVISION}"
git checkout FETCH_HEAD

bold "-- Syncing Skia dependencies..."
python3 tools/git-sync-deps

TARGET_CPU="x64"
case "$(uname -m)" in
  arm64|aarch64) TARGET_CPU="arm64" ;;
  x86_64|amd64) TARGET_CPU="x64" ;;
esac

SKIA_USE_CORETEXT=false
if [ "$(uname -s)" = "Darwin" ]; then
  SKIA_USE_CORETEXT=true
fi

bold "-- Generating GN files..."
gn gen "${BIN_DIR}" --args="
is_official_build=true
is_debug=false
target_cpu=\"${TARGET_CPU}\"
skia_enable_gpu=true
skia_use_gl=false
skia_use_vulkan=false
skia_use_metal=true
skia_use_dawn=false
skia_use_egl=false
skia_use_expat=true
skia_use_fontconfig=false
skia_use_freetype=true
skia_use_coretext=${SKIA_USE_CORETEXT}
skia_use_icu=false
skia_use_libwebp_decode=false
skia_use_libwebp_encode=false
skia_enable_pdf=false
skia_enable_svg=true
skia_enable_skottie=false
skia_enable_tools=false
skia_enable_skshaper=true
skia_use_system_freetype2=false
skia_use_system_libjpeg_turbo=false
skia_use_system_libpng=false
skia_use_system_zlib=false
"

bold "-- Building libskia.a and libsvg.a..."
ninja -C "${BIN_DIR}" skia
ninja -C "${BIN_DIR}" svg

mkdir -p "${STAGING}"
cp "${BIN_DIR}/libskia.a" "${STAGING}/libskia.a"
cp "${BIN_DIR}/libsvg.a" "${STAGING}/libsvg.a"
cp "${BIN_DIR}/libskshaper.a" "${STAGING}/libskshaper.a"
rm -rf "${STAGING}/include" "${STAGING}/modules" "${STAGING}/src"
cp -R include "${STAGING}/include"
mkdir -p "${STAGING}/modules"
cp -R modules/skcms "${STAGING}/modules/skcms"
cp -R modules/skresources "${STAGING}/modules/skresources"
cp -R modules/skshaper "${STAGING}/modules/skshaper"
cp -R modules/svg "${STAGING}/modules/svg"
mkdir -p "${STAGING}/src"
cp -R src/base "${STAGING}/src/base"
cp -R src/core "${STAGING}/src/core"
printf '%s\n' "${BACKEND_ID}" > "${BACKEND_STAMP}"

green "=== Native Skia staged at ${STAGING} ==="
