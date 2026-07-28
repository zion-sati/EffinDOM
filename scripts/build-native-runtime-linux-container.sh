#!/usr/bin/env bash

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET=""
WITH_TESTS=false

usage() {
  cat <<'EOF'
Usage: scripts/build-native-runtime-linux-container.sh --target <linux-x64|linux-arm64> [--with-tests]

Builds the released Linux runtime inside the Rocky Linux 8 compatibility
container and rejects binaries requiring symbols newer than its glibc 2.28
and GCC 8 libstdc++ baseline.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --target)
      [[ $# -ge 2 ]] || { usage >&2; exit 2; }
      TARGET="$2"
      shift 2
      ;;
    --with-tests)
      WITH_TESTS=true
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      usage >&2
      exit 2
      ;;
  esac
done

case "${TARGET}" in
  linux-x64)
    PLATFORM="linux/amd64"
    BUILD_DIR="build/native-linux-x64-gnu-release-vulkan"
    ;;
  linux-arm64)
    PLATFORM="linux/arm64"
    BUILD_DIR="build/native-linux-arm64-gnu-release-vulkan"
    ;;
  *)
    usage >&2
    exit 2
    ;;
esac

command -v docker >/dev/null 2>&1 || {
  echo "Docker is required to build the released Linux runtime." >&2
  exit 1
}

IMAGE="effindom-native-runtime-${TARGET}:rockylinux8"
CACHE="${EFFINDOM_NATIVE_DEPS_CACHE:-${HOME}/.cache/effindom/native-deps}"
mkdir -p "${CACHE}"

docker build \
  --platform "${PLATFORM}" \
  --tag "${IMAGE}" \
  "${REPO_ROOT}/docker/native-runtime-linux"

container_command=(node scripts/build-native-runtime.mjs --target "${TARGET}")
if [[ "${WITH_TESTS}" == true ]]; then
  container_command+=(--with-tests)
fi

docker run --rm \
  --platform "${PLATFORM}" \
  --user "$(id -u):$(id -g)" \
  --env HOME=/tmp/effindom-home \
  --env CC=clang \
  --env CXX=clang++ \
  --env EFFINDOM_NATIVE_DEPS_CACHE=/native-deps-cache \
  --volume "${CACHE}:/native-deps-cache" \
  --volume "${REPO_ROOT}:/work/EffinDOM" \
  --workdir /work/EffinDOM \
  "${IMAGE}" \
  xvfb-run --auto-servernum "${container_command[@]}"

docker run --rm \
  --platform "${PLATFORM}" \
  --user "$(id -u):$(id -g)" \
  --volume "${REPO_ROOT}:/work/EffinDOM:ro" \
  --workdir /work/EffinDOM \
  "${IMAGE}" \
  bash scripts/verify-linux-runtime-abi.sh "${BUILD_DIR}"
