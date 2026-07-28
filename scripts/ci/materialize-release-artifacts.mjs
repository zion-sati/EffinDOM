#!/usr/bin/env node

import { execFileSync, spawnSync } from 'node:child_process';
import { mkdirSync, rmSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';

import { hasSuccessfulJobs, isVerifiedWasmProducer } from './release-attestation-policy.mjs';
import { affectsArtifactScope, nativeScopeNames } from './runtime-dependency-scope.mjs';

const nativeArtifacts = [
  ['macos_arm64 / test', 'native-runtime-macos-arm64'],
  ['macos_x64 / test', 'native-runtime-macos-x64'],
  ['windows_arm64 / test', 'native-runtime-windows-arm64'],
  ['windows_x64 / test', 'native-runtime-windows-x64'],
  ['linux_arm64 / test', 'native-runtime-linux-arm64'],
  ['linux_x64 / test', 'native-runtime-linux-x64'],
];

const repository = process.env.GITHUB_REPOSITORY;
const token = process.env.GITHUB_TOKEN;
const currentRunId = Number(process.env.GITHUB_RUN_ID);
const currentSha = process.env.GITHUB_SHA;
const outputRoot = process.env.RELEASE_ARTIFACT_OUTPUT;
const wasmRequired = process.env.WASM_REQUIRED === 'true';
const nativeRequired = process.env.NATIVE_REQUIRED === 'true';

if (repository === undefined || token === undefined || !Number.isSafeInteger(currentRunId)
  || currentSha === undefined || outputRoot === undefined) {
  throw new Error('GITHUB_REPOSITORY, GITHUB_TOKEN, GITHUB_RUN_ID, GITHUB_SHA, and RELEASE_ARTIFACT_OUTPUT are required.');
}

async function api(path) {
  const response = await fetch(`https://api.github.com${path}`, {
    headers: {
      Accept: 'application/vnd.github+json',
      Authorization: `Bearer ${token}`,
      'X-GitHub-Api-Version': '2022-11-28',
    },
  });
  if (!response.ok) {
    throw new Error(`GitHub API ${path} failed: ${response.status} ${await response.text()}`);
  }
  return response.json();
}

function isAncestor(ancestor, descendant) {
  const result = spawnSync('git', ['merge-base', '--is-ancestor', ancestor, descendant]);
  return result.status === 0;
}

function changedPaths(base, head) {
  return execFileSync('git', ['diff', '--name-only', base, head], { encoding: 'utf8' })
    .split('\n')
    .map((path) => path.trim())
    .filter(Boolean);
}

async function completedRuntimeCiRuns() {
  const runs = [];
  for (let page = 1; page <= 10; page += 1) {
    const result = await api(`/repos/${repository}/actions/workflows/runtime-ci.yml/runs?event=push&status=completed&per_page=100&page=${page}`);
    runs.push(...result.workflow_runs);
    if (result.workflow_runs.length < 100) break;
  }
  return runs;
}

const artifactCache = new Map();
async function artifactsForRun(runId) {
  if (!artifactCache.has(runId)) {
    const result = await api(`/repos/${repository}/actions/runs/${runId}/artifacts?per_page=100`);
    artifactCache.set(runId, new Set(result.artifacts
      .filter((artifact) => !artifact.expired)
      .map((artifact) => artifact.name)));
  }
  return artifactCache.get(runId);
}

const jobCache = new Map();
async function jobsForRun(runId) {
  if (!jobCache.has(runId)) {
    const result = await api(`/repos/${repository}/actions/runs/${runId}/jobs?per_page=100`);
    jobCache.set(runId, result.jobs);
  }
  return jobCache.get(runId);
}

function hasChangedScope(run, scopeNames) {
  const paths = changedPaths(run.head_sha, currentSha);
  return scopeNames.some((scopeName) => paths.some((path) => affectsArtifactScope(path, scopeName)));
}

async function resolveWasmProducer(runs) {
  for (const run of runs) {
    if (!isAncestor(run.head_sha, currentSha) || hasChangedScope(run, ['wasm'])) continue;
    if (!(await artifactsForRun(run.id)).has('runtime-package-inputs')) continue;
    if (isVerifiedWasmProducer(run, await jobsForRun(run.id))) return run;
  }
  throw new Error('No non-expired verified WASM artifact is compatible with this CI commit.');
}

async function resolveNativeProducer(runs) {
  const requiredArtifacts = nativeArtifacts.map(([, artifactName]) => artifactName);
  const requiredJobs = nativeArtifacts.map(([jobName]) => jobName);
  for (const run of runs) {
    if (!isAncestor(run.head_sha, currentSha) || hasChangedScope(run, nativeScopeNames)) continue;
    const artifacts = await artifactsForRun(run.id);
    if (!requiredArtifacts.every((artifactName) => artifacts.has(artifactName))) continue;
    if (hasSuccessfulJobs(run, await jobsForRun(run.id), requiredJobs)) return run;
  }
  throw new Error('No non-expired complete native artifact set is compatible with this CI commit.');
}

function downloadArtifact(runId, artifactName, destination) {
  rmSync(destination, { recursive: true, force: true });
  mkdirSync(destination, { recursive: true });
  const result = spawnSync('gh', [
    'run', 'download', String(runId),
    '--repo', repository,
    '--name', artifactName,
    '--dir', destination,
  ], { stdio: 'inherit' });
  if (result.status !== 0) {
    throw new Error(`Could not download ${artifactName} from Runtime CI run ${runId}.`);
  }
}

const output = resolve(outputRoot);
rmSync(output, { recursive: true, force: true });
const runs = wasmRequired && nativeRequired ? [] : await completedRuntimeCiRuns();
const wasmProducer = wasmRequired
  ? { id: currentRunId, head_sha: currentSha, html_url: `https://github.com/${repository}/actions/runs/${currentRunId}` }
  : await resolveWasmProducer(runs);
const nativeProducer = nativeRequired
  ? { id: currentRunId, head_sha: currentSha, html_url: `https://github.com/${repository}/actions/runs/${currentRunId}` }
  : await resolveNativeProducer(runs);

downloadArtifact(wasmProducer.id, 'runtime-package-inputs', resolve(output, 'wasm', 'dist'));
for (const [, artifactName] of nativeArtifacts) {
  downloadArtifact(nativeProducer.id, artifactName, resolve(output, 'native', 'artifacts', artifactName));
}

const materializedBy = { runId: currentRunId, sourceCommit: currentSha };
writeFileSync(resolve(output, 'wasm', 'provenance.json'), `${JSON.stringify({
  schemaVersion: 1,
  materializedBy,
  producer: { runId: wasmProducer.id, sourceCommit: wasmProducer.head_sha },
}, null, 2)}\n`);
writeFileSync(resolve(output, 'native', 'provenance.json'), `${JSON.stringify({
  schemaVersion: 1,
  materializedBy,
  producer: { runId: nativeProducer.id, sourceCommit: nativeProducer.head_sha },
}, null, 2)}\n`);

console.log(`Materialized WASM inputs from: ${wasmProducer.html_url}`);
console.log(`Materialized native inputs from: ${nativeProducer.html_url}`);
