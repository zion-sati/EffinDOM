#!/usr/bin/env bash

set -euo pipefail

ROOT="${1:-}"
MAX_GLIBC="2.28"
MAX_GLIBCXX="3.4.25"

if [[ -z "${ROOT}" || ! -d "${ROOT}" ]]; then
  echo "Usage: scripts/verify-linux-runtime-abi.sh <build-directory>" >&2
  exit 2
fi

version_is_newer() {
  [[ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | tail -n 1)" == "$1" && "$1" != "$2" ]]
}

checked=0
while IFS= read -r -d '' candidate; do
  description="$(file -b "${candidate}")"
  [[ "${description}" == *ELF* ]] || continue
  checked=$((checked + 1))
  versions="$(readelf --version-info --wide "${candidate}" 2>/dev/null || true)"
  while IFS= read -r version; do
    [[ -n "${version}" ]] || continue
    if version_is_newer "${version}" "${MAX_GLIBC}"; then
      echo "${candidate} requires GLIBC_${version}; maximum allowed is GLIBC_${MAX_GLIBC}." >&2
      exit 1
    fi
  done < <(printf '%s\n' "${versions}" | grep -oE 'GLIBC_[0-9]+(\.[0-9]+)+' | sed 's/^GLIBC_//' | sort -Vu || true)
  while IFS= read -r version; do
    [[ -n "${version}" ]] || continue
    if version_is_newer "${version}" "${MAX_GLIBCXX}"; then
      echo "${candidate} requires GLIBCXX_${version}; maximum allowed is GLIBCXX_${MAX_GLIBCXX}." >&2
      exit 1
    fi
  done < <(printf '%s\n' "${versions}" | grep -oE 'GLIBCXX_[0-9]+(\.[0-9]+)+' | sed 's/^GLIBCXX_//' | sort -Vu || true)
done < <(find "${ROOT}" -type f -print0)

if [[ "${checked}" -eq 0 ]]; then
  echo "No ELF binaries were found under ${ROOT}." >&2
  exit 1
fi

echo "Verified ${checked} ELF binaries against GLIBC_${MAX_GLIBC} / GLIBCXX_${MAX_GLIBCXX}."
