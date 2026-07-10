#!/usr/bin/env bash

set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/lib/common.sh"

PACKAGE_DIR="${REPO_ROOT}/v2/browser-bridge"

log_step "Synchronizing runtime repository dependencies"
run_in_dir "${REPO_ROOT}" npm install

log_step "Running @effindomv2/runtime publish checks"
run_in_dir "${PACKAGE_DIR}" npm run lint
run_in_dir "${PACKAGE_DIR}" npm run typecheck
run_in_dir "${PACKAGE_DIR}" npm run build:package
run_in_dir "${PACKAGE_DIR}" npm pack --ignore-scripts --json --dry-run | node --input-type=module -e '
let input = "";
process.stdin.setEncoding("utf8");
process.stdin.on("data", (chunk) => { input += chunk; });
process.stdin.on("end", () => {
  const result = JSON.parse(input);
  const files = new Set(result[0]?.files?.map((entry) => entry.path) ?? []);
  for (const required of [
    "dist/bridge.js",
    "dist/effindom.v2.manifest.json",
    "dist/runtime/",
  ]) {
    if (![...files].some((file) => file === required || file.startsWith(required))) {
      throw new Error(`Runtime npm tarball is missing required asset: ${required}`);
    }
  }
});
'
stage_local_package "${PACKAGE_DIR}"
