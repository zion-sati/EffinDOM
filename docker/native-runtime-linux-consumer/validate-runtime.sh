#!/usr/bin/env bash
set -euo pipefail

bundle_root="${1:?usage: validate-effindom-runtime BUNDLE_ROOT EXPECTED_MACHINE}"
expected_machine="${2:?usage: validate-effindom-runtime BUNDLE_ROOT EXPECTED_MACHINE}"
runtime_lib="$bundle_root/runtime/lib"

case "$expected_machine" in
    arm64) expected_file_pattern='ARM aarch64' ;;
    x64) expected_file_pattern='x86-64' ;;
    *) echo "Unsupported expected machine: $expected_machine" >&2; exit 2 ;;
esac

checked=0
for library in "$runtime_lib"/*.so; do
    description="$(file -b "$library")"
    if [[ "$description" != *"ELF 64-bit"* || "$description" != *"$expected_file_pattern"* ]]; then
        echo "Unexpected runtime library architecture: $library: $description" >&2
        exit 1
    fi
    dependencies="$(LD_LIBRARY_PATH="$runtime_lib" ldd "$library")"
    if grep -Fq 'not found' <<<"$dependencies"; then
        echo "Unresolved dependencies for $library:" >&2
        echo "$dependencies" >&2
        exit 1
    fi
    checked=$((checked + 1))
done

if (( checked == 0 )); then
    echo "No EffinDOM runtime libraries found under $runtime_lib." >&2
    exit 1
fi

. /etc/os-release
echo "Validated $checked EffinDOM runtime libraries on $ID $VERSION_ID."
