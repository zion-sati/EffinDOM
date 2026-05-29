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

NATIVE_BUILD_DIR="${NATIVE_BUILD_DIR:-build/build-v2-ui}"
CPU_COUNT="$(nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 1)"

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

NATIVE_BUILD_DIR="$(resolve_path "${NATIVE_BUILD_DIR}")"
ensure_cmake_build_dir "${NATIVE_BUILD_DIR}"

bold()  { printf '\033[1m%s\033[0m\n' "$*"; }
green() { printf '\033[32m%s\033[0m\n' "$*"; }
red()   { printf '\033[31m%s\033[0m\n' "$*"; }
step()  { echo; bold "══ $* ══"; }

check_tool() {
  local tool="$1"
  local hint="$2"
  if ! command -v "${tool}" >/dev/null 2>&1; then
    red "Missing required tool: ${tool}"
    echo "  ${hint}"
    exit 1
  fi
}

ensure_node_deps() {
  if [ ! -d "${REPO_ROOT}/node_modules" ]; then
    bold "Installing npm dependencies"
    npm ci --silent
  fi
}

step "1/5  Prerequisites"
check_tool cmake "Install CMake before running the v2/ui build."
check_tool node "Install Node.js 24+ before running the v2/ui build."
check_tool emcc "Source ~/emsdk/emsdk_env.sh (Emscripten 5.0.6) before running the v2/ui build."
ensure_node_deps
green "Tooling is ready"

step "2/5  Typed TypeScript checks"
npm run lint:v2
npm run typecheck:v2
green "v2 TypeScript lint and typecheck passed"

step "3/5  Native Tier 2 build and tests"
if ! cmake_output="$(
  cmake -S . -B "${NATIVE_BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Debug \
    --log-level=WARNING \
    -Wno-dev 2>&1
)"; then
  printf '%s\n' "${cmake_output}"
  exit 1
fi
printf '%s\n' "${cmake_output}" | grep -v "^-- " || true

cmake --build "${NATIVE_BUILD_DIR}" --target effindom_v2_ui_tests --parallel "${CPU_COUNT}"
"${NATIVE_BUILD_DIR}/v2/ui/effindom_v2_ui_tests"
green "Native v2/ui tests passed"

step "4/5  Wasm bundles and browser assets"
bash v2/core/scripts/build_wasm_arch.sh
bash v2/ui/scripts/build_wasm_arch.sh
green "v2/core and v2/ui wasm bundles built"

step "5/5  Browser smoke"
npx playwright test -c v2/ui/playwright.config.ts
green "v2/ui browser smoke passed"

echo
green "Artifacts:"
echo "  Screenshot : v2/ui/tests/integration/screenshots/chromium-ui-bridge-smoke.png"
