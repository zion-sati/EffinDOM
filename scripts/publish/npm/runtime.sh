#!/usr/bin/env bash

set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/lib/common.sh"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PACKAGE_DIR="${REPO_ROOT}/v2/browser-bridge"

bash "${SCRIPT_DIR}/../local/runtime.sh"
publish_package "${PACKAGE_DIR}" "$@"
