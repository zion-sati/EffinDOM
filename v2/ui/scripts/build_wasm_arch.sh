#!/usr/bin/env bash

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "${REPO_ROOT}"

if ! command -v emcmake >/dev/null 2>&1 || ! command -v emcc >/dev/null 2>&1; then
  if [ -f "${HOME}/emsdk/emsdk_env.sh" ]; then
    # shellcheck disable=SC1091
    source "${HOME}/emsdk/emsdk_env.sh" >/dev/null
  fi
fi

resolve_path() {
  local path="$1"
  case "${path}" in
    /*) printf '%s\n' "${path}" ;;
    *) printf '%s/%s\n' "${REPO_ROOT}" "${path#./}" ;;
  esac
}

ensure_cmake_build_dir() {
  local build_dir="$1"
  local cache_file="${build_dir}/CMakeCache.txt"
  local legacy_dir="${REPO_ROOT}/$(basename "${build_dir}")"
  local cached_dir=""

  if [ ! -f "${cache_file}" ]; then
    return
  fi

  cached_dir="$(sed -n 's#^CMAKE_CACHEFILE_DIR:INTERNAL=##p' "${cache_file}" | head -n 1)"
  if [ -n "${cached_dir}" ] && [ "${cached_dir}" != "${build_dir}" ]; then
    rm -rf "${build_dir}"
    return
  fi

  if grep -R -m 1 -F "${legacy_dir}" "${build_dir}" >/dev/null 2>&1; then
    rm -rf "${build_dir}"
  fi
}

WASM_ARCH="${EFFINDOM_WASM_ARCH:-wasm32}"
SIMD_MODE="${EFFINDOM_SIMD:-off}"
case "${SIMD_MODE}" in
  1|true|TRUE|on|ON|yes|YES) SIMD_ENABLED=ON ;;
  0|false|FALSE|off|OFF|no|NO|'') SIMD_ENABLED=OFF ;;
  *)
    echo "Unsupported EFFINDOM_SIMD value: ${SIMD_MODE}" >&2
    exit 1
    ;;
esac

BUILD_DIR="${WASM_BUILD_DIR:-$(
  if [ "${WASM_ARCH}" = "wasm64" ]; then
    if [ "${SIMD_ENABLED}" = "ON" ]; then
      printf 'build/build-v2-ui-wasm64-simd'
    else
      printf 'build/build-v2-ui-wasm64'
    fi
  else
    if [ "${SIMD_ENABLED}" = "ON" ]; then
      printf 'build/build-v2-ui-wasm32-simd'
    else
      printf 'build/build-v2-ui-wasm32'
    fi
  fi
)}"
BUILD_DIR="$(resolve_path "${BUILD_DIR}")"

case "${WASM_ARCH}" in
  wasm32)
    OUTPUT_SUFFIX=""
    MEMORY64_FLAGS=()
    ;;
  wasm64)
    OUTPUT_SUFFIX="64"
    MEMORY64_FLAGS=(
      "-DCMAKE_C_FLAGS=-sMEMORY64=1"
      "-DCMAKE_CXX_FLAGS=-sMEMORY64=1"
      "-DCMAKE_EXE_LINKER_FLAGS=-sMEMORY64=1"
    )
    ;;
  *)
    echo "Unsupported EFFINDOM_WASM_ARCH: ${WASM_ARCH}" >&2
    exit 1
    ;;
esac

if [ "${SIMD_ENABLED}" = "ON" ]; then
  OUTPUT_SUFFIX="${OUTPUT_SUFFIX}.simd"
fi

OUTPUT_DIR="$(resolve_path "${EFFINDOM_BROWSER_OUTPUT_DIR:-public/v2/ui}")"
ensure_cmake_build_dir "${BUILD_DIR}"

if ! cmake_output="$(
  emcmake cmake -S . -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    "-DCMAKE_C_FLAGS_RELEASE=-O3 -flto" \
    "-DCMAKE_CXX_FLAGS_RELEASE=-O3 -flto" \
    "-DCMAKE_EXE_LINKER_FLAGS_RELEASE=-O3 -flto" \
    --log-level=WARNING \
    -Wno-dev \
    "-DEFFINDOM_BUILD_LEGACY_WASM=OFF" \
    "-DEFFINDOM_WASM_ARCH_SUFFIX=${OUTPUT_SUFFIX}" \
    "-DEFFINDOM_SIMD=${SIMD_ENABLED}" \
    "-DEFFINDOM_V2_UI_BROWSER_OUTPUT_DIR=${OUTPUT_DIR}" \
    "${MEMORY64_FLAGS[@]}" 2>&1
)"; then
  printf '%s\n' "${cmake_output}"
  exit 1
fi
printf '%s\n' "${cmake_output}" | grep -v "^-- " || true

cmake --build "${BUILD_DIR}" --target effindom_v2_ui_wasm --parallel

if [ "${EFFINDOM_SKIP_BRIDGE_HARNESS:-0}" != "1" ]; then
  mkdir -p "${OUTPUT_DIR}"
  npx esbuild v2/ui/browser/bridge-harness.ts \
    --bundle \
    --format=iife \
    --platform=browser \
    --target=es2020 \
    --minify \
    --outfile="${OUTPUT_DIR}/bridge-harness.js" \
    --sourcemap
fi

TEMP_JS_OUTPUT="${EFFINDOM_TEMP_JS_OUTPUT:-}"
TEMP_WASM_OUTPUT="${EFFINDOM_TEMP_WASM_OUTPUT:-}"
TEMP_SYMBOLS_OUTPUT="${EFFINDOM_TEMP_SYMBOLS_OUTPUT:-}"

if [ -n "${TEMP_JS_OUTPUT}" ]; then
  mkdir -p "$(dirname "${TEMP_JS_OUTPUT}")"
  cp "${OUTPUT_DIR}/effindom-ui-v2.js" "${TEMP_JS_OUTPUT}"
fi

if [ -n "${TEMP_WASM_OUTPUT}" ]; then
  mkdir -p "$(dirname "${TEMP_WASM_OUTPUT}")"
  cp "${OUTPUT_DIR}/effindom-ui-v2.wasm" "${TEMP_WASM_OUTPUT}"
fi

if [ -n "${TEMP_SYMBOLS_OUTPUT}" ] && [ -f "${OUTPUT_DIR}/effindom-ui-v2.js.symbols" ]; then
  mkdir -p "$(dirname "${TEMP_SYMBOLS_OUTPUT}")"
  cp "${OUTPUT_DIR}/effindom-ui-v2.js.symbols" "${TEMP_SYMBOLS_OUTPUT}"
fi
