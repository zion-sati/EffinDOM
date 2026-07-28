#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
target="linux-arm64"

while (($#)); do
    case "$1" in
        --target)
            target="${2:?--target requires linux-arm64 or linux-x64}"
            shift 2
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 2
            ;;
    esac
done

case "$target" in
    linux-arm64)
        docker_platform="linux/arm64"
        machine="arm64"
        package_root="/tmp/effindom-linux-arm64-package/bundle"
        distros=(ubuntu-24.04 debian-12)
        ;;
    linux-x64)
        docker_platform="linux/amd64"
        machine="x64"
        package_root="/tmp/effindom-linux-x64-package/bundle"
        distros=(ubuntu-24.04 debian-12 arch)
        ;;
    *)
        echo "Unsupported target: $target" >&2
        exit 2
        ;;
esac

if [[ ! -d "$package_root/runtime/lib" ]]; then
    echo "Packaged runtime not found at $package_root." >&2
    echo "Run scripts/package-native-runtime.mjs for $target first." >&2
    exit 1
fi

context="$repo_root/docker/native-runtime-linux-consumer"
for distro in "${distros[@]}"; do
    image="effindom-native-runtime-consumer-$distro-$machine:local"
    docker build \
        --platform "$docker_platform" \
        --file "$context/Dockerfile.$distro" \
        --tag "$image" \
        "$context"
    docker run --rm \
        --platform "$docker_platform" \
        --volume "$package_root:/runtime:ro" \
        "$image" /runtime "$machine"
done
