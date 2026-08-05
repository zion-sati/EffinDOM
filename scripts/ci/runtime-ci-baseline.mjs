#!/usr/bin/env node

import { appendFileSync } from 'node:fs';
import { pathToFileURL } from 'node:url';
import { spawnSync } from 'node:child_process';

const requiredArtifacts = [
  'release-runtime-package-inputs',
  'release-native-runtime-inputs',
];

export async function selectMaterializedBaseline({
  runs,
  currentSha,
  isAncestor,
  artifactsForRun,
  jobsForRun,
}) {
  for (const run of runs) {
    if (run.status !== 'completed' || !isAncestor(run.head_sha, currentSha)) continue;
    const artifacts = await artifactsForRun(run.id);
    if (!requiredArtifacts.every((name) => artifacts.has(name))) continue;
    const jobs = await jobsForRun(run.id);
    if (jobs.some((job) => job.name === 'materialize_artifacts'
      && job.status === 'completed'
      && job.conclusion === 'success')) {
      return run;
    }
  }
  return undefined;
}

async function main() {
  const repository = process.env.GITHUB_REPOSITORY;
  const token = process.env.GITHUB_TOKEN;
  const currentSha = process.env.GITHUB_SHA;
  const outputPath = process.env.GITHUB_OUTPUT;
  if (repository === undefined || token === undefined || currentSha === undefined || outputPath === undefined) {
    throw new Error('GITHUB_REPOSITORY, GITHUB_TOKEN, GITHUB_SHA, and GITHUB_OUTPUT are required.');
  }

  async function api(path) {
    const response = await fetch(`https://api.github.com${path}`, {
      headers: {
        Accept: 'application/vnd.github+json',
        Authorization: `Bearer ${token}`,
        'X-GitHub-Api-Version': '2022-11-28',
      },
    });
    if (!response.ok) throw new Error(`GitHub API ${path} failed: ${response.status} ${await response.text()}`);
    return response.json();
  }

  const runs = [];
  for (let page = 1; page <= 10; page += 1) {
    const result = await api(`/repos/${repository}/actions/workflows/runtime-ci.yml/runs?event=push&status=completed&per_page=100&page=${page}`);
    runs.push(...result.workflow_runs);
    if (result.workflow_runs.length < 100) break;
  }

  const artifacts = new Map();
  const jobs = new Map();
  const baseline = await selectMaterializedBaseline({
    runs,
    currentSha,
    isAncestor(ancestor, descendant) {
      return spawnSync('git', ['merge-base', '--is-ancestor', ancestor, descendant]).status === 0;
    },
    async artifactsForRun(runId) {
      if (!artifacts.has(runId)) {
        const result = await api(`/repos/${repository}/actions/runs/${runId}/artifacts?per_page=100`);
        artifacts.set(runId, new Set(result.artifacts
          .filter((artifact) => !artifact.expired)
          .map((artifact) => artifact.name)));
      }
      return artifacts.get(runId);
    },
    async jobsForRun(runId) {
      if (!jobs.has(runId)) {
        const result = await api(`/repos/${repository}/actions/runs/${runId}/jobs?per_page=100`);
        jobs.set(runId, result.jobs);
      }
      return jobs.get(runId);
    },
  });

  appendFileSync(outputPath, `sha=${baseline?.head_sha ?? ''}\n`);
  console.log(baseline === undefined
    ? 'No complete materialized Runtime CI baseline is available; every scope must rebuild.'
    : `Runtime CI materialized baseline: ${baseline.head_sha} (run ${baseline.id}).`);
}

if (process.argv[1] !== undefined && import.meta.url === pathToFileURL(process.argv[1]).href) await main();
