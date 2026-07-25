#!/usr/bin/env node

import { spawnSync } from 'node:child_process';
import { chmodSync, copyFileSync, existsSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { basename, dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const requiredTargets = ['linux-arm64', 'linux-x64', 'macos-arm64', 'macos-x64', 'windows-arm64', 'windows-x64'];
function fail(message) { console.error(`ERROR: ${message}`); process.exit(1); }
function option(name) { const index = process.argv.indexOf(name); return index < 0 ? '' : process.argv[index + 1] ?? ''; }
function walk(directory) { return readdirSync(directory, { withFileTypes: true }).flatMap((entry) => { const path = join(directory, entry.name); return entry.isDirectory() ? walk(path) : entry.isFile() ? [path] : []; }); }
function run(command, args) { const result = spawnSync(command, args, { cwd: root, stdio: 'inherit' }); if (result.status !== 0) process.exit(result.status ?? 1); }

const release = option('--release');
const sourceCommit = option('--source-commit');
const input = resolve(option('--input'));
const output = resolve(option('--output'));
if (!release || !/^[0-9a-f]{40}$|^[0-9a-f]{64}$/.test(sourceCommit) || !option('--input') || !option('--output')) {
  fail('usage: assemble-native-runtime-release.mjs --release <semver> --source-commit <sha> --input <download-root> --output <directory>');
}
const descriptorPaths = walk(input).filter((path) => basename(path) === 'native-runtime-artifact-descriptor.json');
if (descriptorPaths.length !== requiredTargets.length) fail(`expected ${requiredTargets.length} descriptors, found ${descriptorPaths.length}`);
const entries = descriptorPaths.map((path) => ({ path, root: dirname(path), artifact: JSON.parse(readFileSync(path, 'utf8')) }));
const actualTargets = entries.map(({ artifact }) => artifact.target).sort();
if (JSON.stringify(actualTargets) !== JSON.stringify(requiredTargets)) fail(`native target set is incomplete: ${actualTargets.join(', ')}`);
const linux = entries.find(({ artifact }) => artifact.target === 'linux-x64');
const packager = join(linux.root, 'effindom-native-packager');
if (!existsSync(packager)) fail(`verified Linux packager is missing: ${packager}`);
chmodSync(packager, 0o755);
rmSync(output, { recursive: true, force: true });
mkdirSync(output, { recursive: true });
const verification = join(output, '.verification');
mkdirSync(verification);
for (const entry of entries) {
  run(packager, ['verify-runtime-artifact', entry.root, entry.path, sourceCommit, join(verification, entry.artifact.target)]);
  copyFileSync(join(entry.root, entry.artifact.archive), join(output, entry.artifact.archive));
  copyFileSync(join(entry.root, `${entry.artifact.archive}.sha256`), join(output, `${entry.artifact.archive}.sha256`));
}
rmSync(verification, { recursive: true, force: true });
const request = join(output, '.native-runtime-manifest.request.json');
writeFileSync(request, `${JSON.stringify({ schemaVersion: 1, release, sourceCommit, artifacts: entries.map(({ artifact }) => artifact) }, null, 2)}\n`);
run(packager, ['create-release-manifest', request, join(output, 'native-runtime-manifest.json')]);
rmSync(request);
