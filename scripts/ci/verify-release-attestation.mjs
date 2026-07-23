import { execFileSync } from 'node:child_process';
import { appendFileSync } from 'node:fs';
import { affectsScope, scopeNames } from './runtime-dependency-scope.mjs';

const repository = process.env.GITHUB_REPOSITORY;
const releaseSha = process.env.RELEASE_SHA;
const token = process.env.GITHUB_TOKEN;
const outputPath = process.env.GITHUB_OUTPUT;

if (repository === undefined || releaseSha === undefined || token === undefined || outputPath === undefined) {
  throw new Error('GITHUB_REPOSITORY, RELEASE_SHA, GITHUB_TOKEN, and GITHUB_OUTPUT are required.');
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
  try {
    execFileSync('git', ['merge-base', '--is-ancestor', ancestor, descendant], { stdio: 'ignore' });
    return true;
  } catch {
    return false;
  }
}

function changedPaths(base, head) {
  return execFileSync('git', ['diff', '--name-only', base, head], { encoding: 'utf8' })
    .split('\n')
    .map((path) => path.trim())
    .filter(Boolean);
}

function changesWasmInputs(base, head) {
  return changedPaths(base, head).some((path) => affectsScope(path, 'wasm'));
}

async function successfulRuntimeCiRuns() {
  const runs = [];
  for (let page = 1; page <= 10; page += 1) {
    const result = await api(`/repos/${repository}/actions/workflows/runtime-ci.yml/runs?event=push&status=success&per_page=100&page=${page}`);
    runs.push(...result.workflow_runs);
    if (result.workflow_runs.length < 100) break;
  }
  return runs;
}

async function hasRuntimeArtifact(runId) {
  const result = await api(`/repos/${repository}/actions/runs/${runId}/artifacts?per_page=100`);
  return result.artifacts.some((artifact) => artifact.name === 'runtime-package-inputs' && !artifact.expired);
}

const runs = await successfulRuntimeCiRuns();
let attestationRun = null;
for (const run of runs) {
  if (!isAncestor(run.head_sha, releaseSha)) continue;
  const changed = changedPaths(run.head_sha, releaseSha);
  if (changed.some((path) => scopeNames.some((scopeName) => affectsScope(path, scopeName)))) continue;
  attestationRun = run;
  break;
}
if (attestationRun === null) {
  throw new Error('No successful Runtime CI run attests all runtime inputs for this release commit.');
}

let artifactRun = null;
for (const run of runs) {
  if (!isAncestor(run.head_sha, releaseSha)) continue;
  if (changesWasmInputs(run.head_sha, releaseSha)) continue;
  if (await hasRuntimeArtifact(run.id)) {
    artifactRun = run;
    break;
  }
}
if (artifactRun === null) {
  throw new Error('No non-expired verified WASM artifact is compatible with this release commit.');
}

appendFileSync(outputPath, `runtime_ci_run_id=${attestationRun.id}\nwasm_artifact_run_id=${artifactRun.id}\n`);
console.log(`Runtime CI attestation: ${attestationRun.html_url}`);
console.log(`Reusing WASM package inputs from: ${artifactRun.html_url}`);
