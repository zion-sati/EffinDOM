#!/usr/bin/env bash

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${REPO_ROOT}"

resolve_path() {
  local path="$1"
  case "${path}" in
    /*) printf '%s\n' "${path}" ;;
    *) printf '%s/%s\n' "${REPO_ROOT}" "${path#./}" ;;
  esac
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
      printf 'build/build-wasm64-simd'
    else
      printf 'build/build-wasm64'
    fi
  else
    if [ "${SIMD_ENABLED}" = "ON" ]; then
      printf 'build/build-wasm-simd'
    else
      printf 'build/build-wasm'
    fi
  fi
)}"
GRAPHITE_DIR="${SKIA_GRAPHITE_WASM_DIR:-$(
  if [ "${WASM_ARCH}" = "wasm64" ]; then
    if [ "${SIMD_ENABLED}" = "ON" ]; then
      printf '%s/skia/wasm64-simd' "${REPO_ROOT}"
    else
      printf '%s/skia/wasm64' "${REPO_ROOT}"
    fi
  else
    if [ "${SIMD_ENABLED}" = "ON" ]; then
      printf '%s/skia/wasm-simd' "${REPO_ROOT}"
    else
      printf '%s/skia/wasm' "${REPO_ROOT}"
    fi
  fi
)}"
GANESH_DIR="${SKIA_GANESH_WASM_DIR:-${GRAPHITE_DIR}}"
GRAPHITE_DIR="$(resolve_path "${GRAPHITE_DIR}")"
GANESH_DIR="$(resolve_path "${GANESH_DIR}")"

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

if ! cmake_output="$(
  emcmake cmake -S . -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    --log-level=WARNING \
    -Wno-dev \
    "-DEFFINDOM_WASM_ARCH_SUFFIX=${OUTPUT_SUFFIX}" \
    "-DEFFINDOM_SIMD=${SIMD_ENABLED}" \
    "-DSKIA_GRAPHITE_WASM_DIR=${GRAPHITE_DIR}" \
    "-DSKIA_GANESH_WASM_DIR=${GANESH_DIR}" \
    "${MEMORY64_FLAGS[@]}" 2>&1
)"; then
  printf '%s\n' "${cmake_output}"
  exit 1
fi
printf '%s\n' "${cmake_output}" | grep -v "^-- " || true

cmake --build "${BUILD_DIR}" --target effindom_ui_wasm effindom_core_wasm --parallel
