#!/usr/bin/env node

import { copyFileSync, mkdirSync, readFileSync, readdirSync, rmSync } from 'node:fs';
import { basename, dirname, join, resolve } from 'node:path';

import { REQUIRED_NATIVE_RUNTIME_TARGETS, verifyNativeRuntimeArtifact, writeNativeRuntimeReleaseManifest } from './native-runtime-artifact.mjs';

function fail(message) { console.error(`ERROR: ${message}`); process.exit(1); }
function option(name) { const index = process.argv.indexOf(name); return index < 0 ? '' : process.argv[index + 1] ?? ''; }
function walk(directory) { return readdirSync(directory, { withFileTypes: true }).flatMap((entry) => { const path = join(directory, entry.name); return entry.isDirectory() ? walk(path) : entry.isFile() ? [path] : []; }); }

const release = option('--release');
const sourceCommit = option('--source-commit');
const input = resolve(option('--input'));
const output = resolve(option('--output'));
if (!release || !/^[0-9a-f]{40}$|^[0-9a-f]{64}$/.test(sourceCommit) || !option('--input') || !option('--output')) {
  fail('usage: assemble-native-runtime-release.mjs --release <semver> --source-commit <sha> --input <download-root> --output <directory>');
}
const descriptorPaths = walk(input).filter((path) => basename(path) === 'native-runtime-artifact-descriptor.json');
if (descriptorPaths.length !== REQUIRED_NATIVE_RUNTIME_TARGETS.length) fail(`expected ${REQUIRED_NATIVE_RUNTIME_TARGETS.length} descriptors, found ${descriptorPaths.length}`);
const entries = descriptorPaths.map((path) => ({ path, root: dirname(path), artifact: JSON.parse(readFileSync(path, 'utf8')) }));
const actualTargets = entries.map(({ artifact }) => artifact.target).sort();
if (JSON.stringify(actualTargets) !== JSON.stringify(REQUIRED_NATIVE_RUNTIME_TARGETS)) fail(`native target set is incomplete: ${actualTargets.join(', ')}`);
rmSync(output, { recursive: true, force: true });
mkdirSync(output, { recursive: true });
for (const entry of entries) {
  verifyNativeRuntimeArtifact(entry.root, entry.artifact, sourceCommit);
  copyFileSync(join(entry.root, entry.artifact.archive), join(output, entry.artifact.archive));
  copyFileSync(join(entry.root, `${entry.artifact.archive}.sha256`), join(output, `${entry.artifact.archive}.sha256`));
}
writeNativeRuntimeReleaseManifest(
  { release, sourceCommit, artifacts: entries.map(({ artifact }) => artifact) },
  join(output, 'native-runtime-manifest.json'),
);
