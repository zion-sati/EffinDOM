#!/usr/bin/env bash

set -euo pipefail

source_root=/package
relocated_root=/tmp/effindom-relocated
rm -rf "${relocated_root}"
mkdir -p "${relocated_root}"
cp -a "${source_root}/." "${relocated_root}/"

executable="${relocated_root}/bin/effindom_v2_linux_native"
if [[ ! -x "${executable}" ]]; then
  echo "Relocated Linux executable is missing: ${executable}" >&2
  exit 1
fi

cd /tmp
xvfb-run --auto-servernum "${executable}" \
  --hidden --screenshot /tmp/effindom-relocated-launch.png
test -s /tmp/effindom-relocated-launch.png
echo "Relocated EffinDOM Linux package launched successfully."
