#!/usr/bin/env bash

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT_DIR="${REPO_ROOT}/public/v2/browser-bridge"
source "${REPO_ROOT}/v2/browser-bridge/scripts/font_assets.sh"
MANIFEST_SCRIPT="${REPO_ROOT}/v2/browser-bridge/scripts/generate_manifest.py"
MANIFEST_FILE="${OUT_DIR}/effindom.v2.manifest.json"

ICU_ROOT=""
ICU_ROOT_CANDIDATES=(
  "${REPO_ROOT}/build/build-v2-ui/_deps/effindom_skia_pinned_icu-src"
  "${REPO_ROOT}/build/build-v2-ui-wasm32/_deps/effindom_skia_pinned_icu-src"
  "${REPO_ROOT}/build/build-v2-core/_deps/effindom_skia_pinned_icu-src"
  "${REPO_ROOT}/build/build-v2-core-wasm32/_deps/effindom_skia_pinned_icu-src"
)
ICU_BUILD_DIR="${REPO_ROOT}/build/build-v2-browser-bridge-icu"
ICU_FILTER="${REPO_ROOT}/v2/browser-bridge/icu-filter.json"
ICU_CONFIG_STAMP="${ICU_BUILD_DIR}/.effindom-icu-config"
ICU_CONFIG_STATUS="${ICU_BUILD_DIR}/config.status"
ICU_JOBS="${ICU_JOBS:-4}"

rm -rf "${OUT_DIR}"
mkdir -p "${REPO_ROOT}/build"
STAGE_DIR="$(mktemp -d "${REPO_ROOT}/build/build-v2-browser-bridge-stage-XXXXXX")"

# Publish checks should always start from fresh ganesh staging dirs so stale
# cross-arch archives cannot leak into a later variant.
rm -rf \
  "${REPO_ROOT}/skia/wasm-ganesh" \
  "${REPO_ROOT}/skia/wasm-ganesh-simd" \
  "${REPO_ROOT}/skia/wasm64-ganesh" \
  "${REPO_ROOT}/skia/wasm64-ganesh-simd"

if ! command -v emcmake >/dev/null 2>&1 || ! command -v emcc >/dev/null 2>&1; then
  if [ -f "${HOME}/emsdk/emsdk_env.sh" ]; then
    # shellcheck disable=SC1091
    source "${HOME}/emsdk/emsdk_env.sh" >/dev/null
  fi
fi

mkdir -p "${OUT_DIR}"

current_variant_pid=""
SKIA_WORKDIR_BASE="${SKIA_BUILD_WORKDIR:-${HOME}/.cache/effindom-skia-build}"
SKIA_SOURCE_MIRROR_DIR="${SKIA_WORKDIR_BASE}/mirror/skia.git"
SKIA_DEPOT_TOOLS_DIR="${SKIA_WORKDIR_BASE}/depot_tools"

collect_descendant_pids() {
  local frontier=("$@")
  local next=()
  local pid=""
  local ppid=""

  while [ "${#frontier[@]}" -gt 0 ]; do
    next=()
    while read -r pid ppid; do
      [ -n "${pid}" ] || continue
      [ -n "${ppid}" ] || continue
      for parent in "${frontier[@]}"; do
        if [ "${ppid}" = "${parent}" ]; then
          next+=("${pid}")
          break
        fi
      done
    done < <(ps -axo pid=,ppid=)

    if [ "${#next[@]}" -eq 0 ]; then
      break
    fi

    printf '%s\n' "${next[@]}"
    frontier=("${next[@]}")
  done
}

kill_process_tree() {
  local roots=("$@")
  local descendants=()
  local pid=""

  while IFS= read -r pid; do
    [ -n "${pid}" ] && descendants+=("${pid}")
  done < <(collect_descendant_pids "${roots[@]}")

  for pid in "${descendants[@]}"; do
    kill -TERM "${pid}" >/dev/null 2>&1 || true
  done
  for pid in "${roots[@]}"; do
    kill -TERM "${pid}" >/dev/null 2>&1 || true
  done

  sleep 2

  descendants=()
  while IFS= read -r pid; do
    [ -n "${pid}" ] && descendants+=("${pid}")
  done < <(collect_descendant_pids "${roots[@]}")

  for pid in "${descendants[@]}" "${roots[@]}"; do
    kill -KILL "${pid}" >/dev/null 2>&1 || true
  done
}

kill_tracked_pids() {
  local pid=""
  for pid in "$@"; do
    if [ -n "${pid}" ]; then
      kill_process_tree "${pid}"
    fi
  done
}

wait_for_pids() {
  local status=0
  local pid=""
  for pid in "$@"; do
    if [ -n "${pid}" ] && ! wait "${pid}"; then
      status=1
    fi
  done
  return "${status}"
}

cleanup() {
  local exit_code=$?
  trap - EXIT INT TERM HUP
  kill_process_tree "${current_variant_pid}"
  wait_for_pids "${current_variant_pid}" >/dev/null 2>&1 || true
  rm -rf "${STAGE_DIR}"
  exit "${exit_code}"
}

handle_shutdown_signal() {
  kill_process_tree "${current_variant_pid}"
}

trap cleanup EXIT
trap handle_shutdown_signal INT TERM HUP

icu_config_is_stale() {
  if [ ! -f "${ICU_CONFIG_STATUS}" ]; then
    return 0
  fi

  local input=""
  for input in \
    "${ICU_ROOT}/source/configure" \
    "${ICU_ROOT}/source/common/unicode/uvernum.h" \
    "${ICU_FILTER}"; do
    if [ "${input}" -nt "${ICU_CONFIG_STATUS}" ]; then
      return 0
    fi
  done

  return 1
}

prepare_icu_data() {
  local candidate=""
  local expected_config=""
  local current_config=""

  for candidate in "${ICU_ROOT_CANDIDATES[@]}"; do
    if [ -d "${candidate}" ]; then
      ICU_ROOT="${candidate}"
      break
    fi
  done

  if [ -z "${ICU_ROOT}" ]; then
    echo "Could not find ICU source tree after building v2/core and v2/ui lanes." >&2
    exit 1
  fi

  mkdir -p "${ICU_BUILD_DIR}"
  expected_config=$(printf '%s\n%s\n' "${ICU_ROOT}" "${ICU_FILTER}")
  if [ -f "${ICU_CONFIG_STAMP}" ]; then
    current_config="$(cat "${ICU_CONFIG_STAMP}")"
  fi

  if [ ! -f "${ICU_BUILD_DIR}/Makefile" ] || [ "${current_config}" != "${expected_config}" ] || icu_config_is_stale; then
    rm -rf "${ICU_BUILD_DIR}"
    mkdir -p "${ICU_BUILD_DIR}"
    pushd "${ICU_BUILD_DIR}" >/dev/null
    ICU_DATA_FILTER_FILE="${ICU_FILTER}" \
      "${ICU_ROOT}/source/runConfigureICU" \
      --enable-debug \
      --disable-release \
      Linux/gcc \
      --disable-tests \
      --disable-layoutex \
      --enable-rpath \
      --prefix="${ICU_BUILD_DIR}"
    popd >/dev/null
    printf '%s' "${expected_config}" > "${ICU_CONFIG_STAMP}"
  fi

  pushd "${ICU_BUILD_DIR}" >/dev/null
  make -j"${ICU_JOBS}"
  ICU_VERSION="$(egrep '^SO_TARGET.*MAJOR' icudefs.mk | awk '{print $3}')"
  ICU_SOURCE="${ICU_BUILD_DIR}/data/out/tmp/icudt${ICU_VERSION}l.dat"
  popd >/dev/null

  if [ ! -f "${ICU_SOURCE}" ]; then
    echo "Filtered ICU data build did not produce ${ICU_SOURCE}." >&2
    exit 1
  fi
}

prepare_shared_deps() {
  mkdir -p "${SKIA_WORKDIR_BASE}/mirror"

  if [ ! -d "${SKIA_SOURCE_MIRROR_DIR}/objects" ]; then
    rm -rf "${SKIA_SOURCE_MIRROR_DIR}"
    git clone --mirror https://skia.googlesource.com/skia.git "${SKIA_SOURCE_MIRROR_DIR}"
  fi

  if [ ! -d "${SKIA_DEPOT_TOOLS_DIR}" ]; then
    git clone --depth 1 https://chromium.googlesource.com/chromium/tools/depot_tools.git "${SKIA_DEPOT_TOOLS_DIR}"
  fi
}

stage_variant() {
  local architecture_name="$1"
  local wasm_arch="$2"
  local simd_mode="$3"
  local variant_dir="${STAGE_DIR}/${architecture_name}"
  local core_pid=""
  local ui_pid=""
  local exit_code=0

  mkdir -p "${variant_dir}"

  SKIA_WORKDIR="${SKIA_WORKDIR_BASE}/${architecture_name}"
  mkdir -p "${SKIA_WORKDIR}"

  handle_stage_shutdown_signal() {
   kill_tracked_pids "${core_pid:-}" "${ui_pid:-}"
  }

  cleanup_stage() {
   local exit_code=$?
   trap - EXIT INT TERM HUP
   kill_tracked_pids "${core_pid:-}" "${ui_pid:-}"
   wait_for_pids "${core_pid:-}" "${ui_pid:-}" >/dev/null 2>&1 || true
   exit "${exit_code}"
  }
  trap cleanup_stage EXIT
  trap handle_stage_shutdown_signal INT TERM HUP

  (
    cd "${REPO_ROOT}"
    SKIA_BUILD_WORKDIR="${SKIA_WORKDIR}" \
    SKIA_SOURCE_MIRROR="${SKIA_SOURCE_MIRROR_DIR}" \
    SKIA_DEPOT_TOOLS_DIR="${SKIA_DEPOT_TOOLS_DIR}" \
    EFFINDOM_WASM_ARCH="${wasm_arch}" \
    EFFINDOM_SIMD="${simd_mode}" \
    EFFINDOM_BROWSER_OUTPUT_DIR="${variant_dir}/core-out" \
    EFFINDOM_TEMP_JS_OUTPUT="${variant_dir}/core.js" \
    EFFINDOM_TEMP_WASM_OUTPUT="${variant_dir}/core.wasm" \
    EFFINDOM_TEMP_SYMBOLS_OUTPUT="${variant_dir}/core.js.symbols" \
   bash v2/core/scripts/build_wasm_arch.sh
  ) &
  core_pid="$!"

  if ! wait "${core_pid}"; then
   exit_code=1
   core_pid=""
   return "${exit_code}"
  fi
  core_pid=""

  (
   cd "${REPO_ROOT}"
   SKIA_BUILD_WORKDIR="${SKIA_WORKDIR}" \
   SKIA_SOURCE_MIRROR="${SKIA_SOURCE_MIRROR_DIR}" \
   SKIA_DEPOT_TOOLS_DIR="${SKIA_DEPOT_TOOLS_DIR}" \
   EFFINDOM_WASM_ARCH="${wasm_arch}" \
   EFFINDOM_SIMD="${simd_mode}" \
   EFFINDOM_BROWSER_OUTPUT_DIR="${variant_dir}/ui-out" \
   EFFINDOM_SKIP_BRIDGE_HARNESS=1 \
   EFFINDOM_TEMP_JS_OUTPUT="${variant_dir}/ui.js" \
   EFFINDOM_TEMP_WASM_OUTPUT="${variant_dir}/ui.wasm" \
   EFFINDOM_TEMP_SYMBOLS_OUTPUT="${variant_dir}/ui.js.symbols" \
   bash v2/ui/scripts/build_wasm_arch.sh
  ) &
  ui_pid="$!"

  if ! wait "${ui_pid}"; then
   exit_code=1
  fi
  return "${exit_code}"
}

prepare_shared_deps

for variant_args in \
  "wasm32 wasm32 off" \
  "wasm32-simd wasm32 on" \
  "wasm64 wasm64 off" \
  "wasm64-simd wasm64 on"; do
  # shellcheck disable=SC2086
  ( stage_variant ${variant_args} ) &
  current_variant_pid="$!"
  if ! wait "${current_variant_pid}"; then
    current_variant_pid=""
    exit 1
  fi
  current_variant_pid=""
done

prepare_icu_data

npx esbuild "${REPO_ROOT}/v2/browser-bridge/src/bridge.ts" \
  --bundle \
  --format=iife \
  --platform=browser \
  --target=es2020 \
  --minify \
  --outfile="${OUT_DIR}/bridge.js" \
  --sourcemap

npx esbuild "${REPO_ROOT}/v2/browser-bridge/src/harness.ts" \
  --bundle \
  --format=iife \
  --platform=browser \
  --target=es2020 \
  --minify \
  --outfile="${OUT_DIR}/harness.js" \
  --sourcemap

cp "${REPO_ROOT}/v2/browser-bridge/index.html" "${OUT_DIR}/index.html"
copy_bridge_font_assets "${REPO_ROOT}/public/v2/fonts"

rm -rf "${OUT_DIR}/runtime"
rm -f "${MANIFEST_FILE}" "${OUT_DIR}/icu-asset.json"
python3 "${MANIFEST_SCRIPT}" "${OUT_DIR}" "${STAGE_DIR}" "${ICU_SOURCE}"

# Leave the canonical v2/core and v2/ui browser outputs on the safest baseline lane.
cp "${REPO_ROOT}/v2/core/browser/index.html" "${REPO_ROOT}/public/v2/core/index.html"
cp "${STAGE_DIR}/wasm32/core.js" "${REPO_ROOT}/public/v2/core/effindom-core-v2.js"
cp "${STAGE_DIR}/wasm32/core.wasm" "${REPO_ROOT}/public/v2/core/effindom-core-v2.wasm"
if [ -f "${STAGE_DIR}/wasm32/core.js.symbols" ]; then
  cp "${STAGE_DIR}/wasm32/core.js.symbols" "${REPO_ROOT}/public/v2/core/effindom-core-v2.js.symbols"
fi

cp "${REPO_ROOT}/v2/ui/browser/index.html" "${REPO_ROOT}/public/v2/ui/index.html"
cp "${STAGE_DIR}/wasm32/ui.js" "${REPO_ROOT}/public/v2/ui/effindom-ui-v2.js"
cp "${STAGE_DIR}/wasm32/ui.wasm" "${REPO_ROOT}/public/v2/ui/effindom-ui-v2.wasm"
if [ -f "${STAGE_DIR}/wasm32/ui.js.symbols" ]; then
  cp "${STAGE_DIR}/wasm32/ui.js.symbols" "${REPO_ROOT}/public/v2/ui/effindom-ui-v2.js.symbols"
fi

mkdir -p "${REPO_ROOT}/public/v2/ui"
npx esbuild "${REPO_ROOT}/v2/ui/browser/bridge-harness.ts" \
  --bundle \
  --format=iife \
  --platform=browser \
  --target=es2020 \
  --minify \
  --outfile="${REPO_ROOT}/public/v2/ui/bridge-harness.js" \
  --sourcemap
