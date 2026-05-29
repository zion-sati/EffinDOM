#!/usr/bin/env bash

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "${REPO_ROOT}"

NATIVE_BUILD_DIR="${NATIVE_BUILD_DIR:-build/build-v2-core}"
CPU_COUNT="$(nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 1)"
REBUILD_SKIA=false

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

print_usage() {
  cat <<'EOF'
Usage: ./v2/core/scripts/build_all.sh [--rebuild-skia]

Options:
  --rebuild-skia  Force the shared skia/wasm cache for this wasm lane to rebuild.
  -h, --help      Show this help text.
EOF
}

for arg in "$@"; do
  case "$arg" in
    --rebuild-skia) REBUILD_SKIA=true ;;
    -h|--help)
      print_usage
      exit 0
      ;;
    *)
      red "Unknown argument: ${arg}"
      print_usage
      exit 1
      ;;
  esac
done

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
check_tool cmake "Install CMake before running the v2/core build."
check_tool meson "Install Meson before running the v2/core build."
check_tool python3 "Install Python 3 before running the v2/core build."
check_tool node "Install Node.js 24+ before running the v2/core build."
check_tool emcc "Source ~/emsdk/emsdk_env.sh (Emscripten 5.0.6) before running the v2/core build."
ensure_node_deps
green "Tooling is ready"

step "2/5  Typed TypeScript checks"
npm run lint:v2
npm run typecheck:v2
green "v2 TypeScript lint and typecheck passed"

step "3/5  Native Tier 1 build and tests"
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

cmake --build "${NATIVE_BUILD_DIR}" \
  --target effindom_v2_core_tests effindom_v2_core_generate_goldens \
  --parallel "${CPU_COUNT}"

if [ -f "${NATIVE_BUILD_DIR}/build.ninja" ] && grep -q "effindom_v2_core_fuzz" "${NATIVE_BUILD_DIR}/build.ninja"; then
  cmake --build "${NATIVE_BUILD_DIR}" --target effindom_v2_core_fuzz --parallel "${CPU_COUNT}"
fi

"${NATIVE_BUILD_DIR}/v2/core/effindom_v2_core_tests"
if [ -x "${NATIVE_BUILD_DIR}/v2/core/effindom_v2_core_fuzz" ]; then
  "${NATIVE_BUILD_DIR}/v2/core/effindom_v2_core_fuzz" --iterations 2500 --max-words 256
fi
"${NATIVE_BUILD_DIR}/v2/core/effindom_v2_core_generate_goldens"
green "Native tests and goldens passed"

step "4/5  Wasm bundle"
if [ "${REBUILD_SKIA}" = true ]; then
  bash v2/core/scripts/build_wasm_arch.sh --rebuild-skia
else
  bash v2/core/scripts/build_wasm_arch.sh
fi
green "v2/core wasm bundle built"

step "5/5  Browser smoke"
npx playwright test -c v2/core/playwright.config.ts
green "v2/core browser smoke passed"

echo
green "Artifacts:"
echo "  Goldens    : v2/core/tests/goldens/"
echo "  Screenshot : v2/core/tests/integration/screenshots/chromium-core-smoke.png"
