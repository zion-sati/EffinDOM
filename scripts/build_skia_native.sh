#!/usr/bin/env bash

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK_DIR="${SKIA_BUILD_WORKDIR:-${HOME}/.cache/effindom-skia-build/native}"
SKIA_REVISION="${SKIA_REVISION:-chrome/m136}"
DEPOT_TOOLS_COMMIT="${DEPOT_TOOLS_COMMIT:-}"
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

for tool in python3 git ninja; do
  if ! command -v "${tool}" >/dev/null 2>&1; then
    red "ERROR: ${tool} is required to build native Skia."
    exit 1
  fi
done

TARGET_CPU="x64"
case "${SKIA_TARGET_ARCH:-$(uname -m)}" in
  arm64|aarch64) TARGET_CPU="arm64" ;;
  x64|x86_64|amd64) TARGET_CPU="x64" ;;
  x86|i386|i686) TARGET_CPU="x86" ;;
  *) red "ERROR: unsupported native Skia architecture '${SKIA_TARGET_ARCH:-$(uname -m)}'."; exit 1 ;;
esac

CONFIGURATION="${SKIA_BUILD_CONFIGURATION:-Release}"
CONFIGURATION_TOKEN="$(printf '%s' "${CONFIGURATION}" | tr '[:upper:]' '[:lower:]')"
case "${CONFIGURATION}" in
  Debug|debug) IS_DEBUG=true; IS_OFFICIAL_BUILD=false ;;
  Release|release|RelWithDebInfo|relwithdebinfo) IS_DEBUG=false; IS_OFFICIAL_BUILD=true ;;
  *) red "ERROR: unsupported native Skia configuration '${CONFIGURATION}'."; exit 1 ;;
esac

PLATFORM_TOKEN="$(uname -s | tr '[:upper:]' '[:lower:]')"
case "${PLATFORM_TOKEN}" in
  darwin) PLATFORM_TOKEN="macos"; DEFAULT_BACKEND="metal" ;;
  linux) DEFAULT_BACKEND="vulkan" ;;
  *) red "ERROR: unsupported native Skia platform '$(uname -s)'."; exit 1 ;;
esac

BACKEND="${SKIA_NATIVE_BACKEND:-${DEFAULT_BACKEND}}"
if [ "${BACKEND}" = "metal" ] && [ "${PLATFORM_TOKEN}" != "macos" ]; then
  red "ERROR: the Metal native Skia backend requires macOS."
  exit 1
fi
if [ "${BACKEND}" = "vulkan" ] && [ "${PLATFORM_TOKEN}" != "linux" ]; then
  red "ERROR: the Vulkan native Skia backend currently requires Linux."
  exit 1
fi
if [ "${BACKEND}" != "metal" ] && [ "${BACKEND}" != "vulkan" ] && [ "${BACKEND}" != "raster" ]; then
  red "ERROR: unsupported native Skia backend '${BACKEND}'."
  exit 1
fi

COMPILER_TOKEN="${SKIA_COMPILER_ID:-}"
if [ -z "${COMPILER_TOKEN}" ]; then
  if [ "${PLATFORM_TOKEN}" = "macos" ]; then COMPILER_TOKEN="appleclang"; else COMPILER_TOKEN="clang"; fi
fi
COMPILER_TOKEN="$(printf '%s' "${COMPILER_TOKEN}" | tr '[:upper:]' '[:lower:]')"
STAGING="${SKIA_NATIVE_DIR:-${REPO_ROOT}/skia/native/${PLATFORM_TOKEN}-${TARGET_CPU}-${COMPILER_TOKEN}-${CONFIGURATION_TOKEN}-${BACKEND}}"
BIN_DIR="out/${PLATFORM_TOKEN}-${TARGET_CPU}-${CONFIGURATION_TOKEN}-${BACKEND}"
BACKEND_ID="native-ganesh-${PLATFORM_TOKEN}-${TARGET_CPU}-${CONFIGURATION_TOKEN}-${BACKEND}-${SKIA_REVISION}"
BACKEND_STAMP="${STAGING}/.effindom-skia-backend"
SKIA_CC="${CC:-clang}"
SKIA_CXX="${CXX:-clang++}"

if [ "$FORCE" = false ] && [ -f "${BACKEND_STAMP}" ] && [ "$(cat "${BACKEND_STAMP}")" = "${BACKEND_ID}" ] && [ -f "${STAGING}/libskia.a" ] && [ -f "${STAGING}/libsvg.a" ] && [ -f "${STAGING}/libskshaper.a" ] && [ -f "${STAGING}/modules/svg/include/SkSVGDOM.h" ] && [ -f "${STAGING}/modules/skresources/include/SkResources.h" ] && [ -f "${STAGING}/modules/skshaper/include/SkShaper_factory.h" ] && [ -f "${STAGING}/src/core/SkTHash.h" ] && [ -f "${STAGING}/src/base/SkMathPriv.h" ]; then
  green "=== Native Skia already staged at ${STAGING} — skipping ==="
  exit 0
fi

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

SKIA_USE_CORETEXT=false
SKIA_USE_METAL=false
SKIA_USE_VULKAN=false
SKIA_ENABLE_GPU=false
if [ "${PLATFORM_TOKEN}" = "macos" ]; then
  SKIA_USE_CORETEXT=true
fi
if [ "${BACKEND}" = "metal" ]; then
  SKIA_USE_METAL=true
  SKIA_ENABLE_GPU=true
fi
if [ "${BACKEND}" = "vulkan" ]; then
  SKIA_USE_VULKAN=true
  SKIA_ENABLE_GPU=true
fi

bold "-- Generating GN files..."
gn gen "${BIN_DIR}" --args="
is_official_build=${IS_OFFICIAL_BUILD}
is_debug=${IS_DEBUG}
target_cpu=\"${TARGET_CPU}\"
cc=\"${SKIA_CC}\"
cxx=\"${SKIA_CXX}\"
skia_enable_gpu=${SKIA_ENABLE_GPU}
skia_use_gl=false
skia_use_vulkan=${SKIA_USE_VULKAN}
skia_use_metal=${SKIA_USE_METAL}
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
