#!/usr/bin/env bash

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUNDLE="${REPO_ROOT}/build/native-linux-x64-gnu-release-vulkan/v2/native/linux/output"
DISTROS=(debian12 ubuntu2404 arch)

usage() {
  cat <<'EOF'
Usage: ./scripts/test-native-relocation-linux-distros.sh [--bundle <path>] [--distro <name>]

Copies one already-built Linux x64 package into a fresh location and launches
it without source-tree or network access on Debian 12, Ubuntu 24.04, and Arch
Linux. Supported distro names: debian12, ubuntu2404, arch.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --bundle)
      [[ $# -ge 2 ]] || { usage >&2; exit 2; }
      BUNDLE="$2"
      shift 2
      ;;
    --distro)
      [[ $# -ge 2 ]] || { usage >&2; exit 2; }
      case "$2" in
        debian12|ubuntu2404|arch) DISTROS=("$2") ;;
        *) echo "Unsupported distribution: $2" >&2; exit 2 ;;
      esac
      shift 2
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

BUNDLE="$(cd "${BUNDLE}" 2>/dev/null && pwd)" || {
  echo "Linux package directory does not exist: ${BUNDLE}" >&2
  exit 1
}
test -x "${BUNDLE}/bin/effindom_v2_linux_native" || {
  echo "Linux package executable is missing from ${BUNDLE}" >&2
  exit 1
}

for distro in "${DISTROS[@]}"; do
  case "${distro}" in
    debian12) base=debian:12-slim ;;
    ubuntu2404) base=ubuntu:24.04 ;;
    arch) base=archlinux:base ;;
  esac
  image="effindom-native-relocation-${distro}:local"
  docker build \
    --platform linux/amd64 \
    --build-arg "BASE_IMAGE=${base}" \
    --build-arg "DISTRO=${distro}" \
    --file "${REPO_ROOT}/docker/native-relocation/Dockerfile" \
    --tag "${image}" \
    "${REPO_ROOT}"
  docker run --rm --network none --platform linux/amd64 \
    --volume "${BUNDLE}:/package:ro" \
    "${image}"
done

echo "Relocated Linux package passed the local distribution matrix."
