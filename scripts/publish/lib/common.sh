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

stage_local_package() {
  local package_dir="$1"
  local published_root="${2:-${REPO_ROOT}/published}"
  assert_publishable_package "${package_dir}"

  mkdir -p "${REPO_ROOT}/build"
  mkdir -p "${published_root}"

  local pack_output_file
  pack_output_file="$(mktemp "${REPO_ROOT}/build/.npm-pack.XXXXXX")"

  run_in_dir "${package_dir}" npm pack --json --ignore-scripts > "${pack_output_file}"

  local pack_info
  pack_info="$(node --input-type=module -e '
import { readFileSync } from "node:fs";
const items = JSON.parse(readFileSync(process.argv[1], "utf8"));
const item = items[0];
if (!item?.filename || !item?.name || !item?.version) {
  process.exit(1);
}
process.stdout.write(item.filename + "\n" + item.name + "\n" + item.version + "\n");
' "${pack_output_file}")"
  rm -f "${pack_output_file}" >/dev/null 2>&1 || true

  local tarball_name
  local package_name
  local package_version
  tarball_name="$(printf '%s' "${pack_info}" | sed -n '1p')"
  package_name="$(printf '%s' "${pack_info}" | sed -n '2p')"
  package_version="$(printf '%s' "${pack_info}" | sed -n '3p')"

  local sanitized_package_name
  sanitized_package_name="${package_name//@/}"
  sanitized_package_name="${sanitized_package_name//\//-}"

  local package_output_dir="${published_root}/${sanitized_package_name}-${package_version}"
  local tarball_path="${package_dir}/${tarball_name}"
  local unpack_dir
  unpack_dir="$(mktemp -d "${REPO_ROOT}/build/.npm-pack-unpack-XXXXXX")"

  rm -rf "${package_output_dir}"
  mkdir -p "${package_output_dir}"
  tar -xzf "${tarball_path}" -C "${unpack_dir}"
  if [ -d "${unpack_dir}/package" ]; then
    cp -R "${unpack_dir}/package/." "${package_output_dir}/"
  else
    cp -R "${unpack_dir}/." "${package_output_dir}/"
  fi
  rm -rf "${unpack_dir}"
  mv "${tarball_path}" "${published_root}/${tarball_name}"

  log_step "Staged ${package_name}@${package_version} into ${package_output_dir}"
  log_step "Wrote tarball ${published_root}/${tarball_name}"
}
