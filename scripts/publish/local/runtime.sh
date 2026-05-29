#!/usr/bin/env bash

set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/lib/common.sh"

PACKAGE_DIR="${REPO_ROOT}/v2/browser-bridge"

log_step "Running @effindomv2/runtime publish checks"
run_in_dir "${PACKAGE_DIR}" npm run typecheck
run_in_dir "${PACKAGE_DIR}" npm run build:package
run_in_dir "${PACKAGE_DIR}" npm pack --dry-run >/dev/null
