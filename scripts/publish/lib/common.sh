#!/usr/bin/env bash

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"

log_step() {
  echo "==> $*"
}

run_in_dir() {
  local directory="$1"
  shift
  (
    cd "${directory}"
    "$@"
  )
}

package_json_field() {
  local package_dir="$1"
  local field="$2"
  node --input-type=module -e '
import { readFileSync } from "node:fs";
const [packageJsonPath, key] = process.argv.slice(1);
const packageJson = JSON.parse(readFileSync(packageJsonPath, "utf8"));
const value = packageJson[key];
if (value === undefined) {
  process.exit(2);
}
if (typeof value === "string") {
  process.stdout.write(value);
} else {
  process.stdout.write(String(value));
}
' "${package_dir}/package.json" "${field}"
}

assert_publishable_package() {
  local package_dir="$1"
  local package_name
  local is_private
  package_name="$(package_json_field "${package_dir}" "name")"
  is_private="$(package_json_field "${package_dir}" "private" || true)"
  if [ "${is_private}" = "true" ]; then
    echo "Package ${package_name} is marked private=true and cannot be published." >&2
    exit 1
  fi
}

ensure_npm_logged_in() {
  if ! npm whoami >/dev/null 2>&1; then
    echo "Not logged in to npm. Run 'npm login' first." >&2
    exit 1
  fi
}

publish_package() {
  local package_dir="$1"
  shift
  assert_publishable_package "${package_dir}"
  ensure_npm_logged_in
  local package_name
  local package_version
  package_name="$(package_json_field "${package_dir}" "name")"
  package_version="$(package_json_field "${package_dir}" "version")"
  log_step "Publishing ${package_name}@${package_version}"
  run_in_dir "${package_dir}" npm publish --access public "$@"
}
