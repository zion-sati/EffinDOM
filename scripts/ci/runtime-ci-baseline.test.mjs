import assert from 'node:assert/strict';
import test from 'node:test';

import { selectMaterializedBaseline } from './runtime-ci-baseline.mjs';

const completeArtifacts = new Set([
  'release-runtime-package-inputs',
  'release-native-runtime-inputs',
]);
const successfulMaterialization = [{
  name: 'materialize_artifacts',
  status: 'completed',
  conclusion: 'success',
}];

test('selects the newest ancestor with complete non-expired materialized artifacts', async () => {
  const runs = [
    { id: 3, head_sha: 'failed', status: 'completed' },
    { id: 2, head_sha: 'complete', status: 'completed' },
    { id: 1, head_sha: 'older', status: 'completed' },
  ];
  const baseline = await selectMaterializedBaseline({
    runs,
    currentSha: 'head',
    isAncestor: (sha) => sha !== 'unrelated',
    artifactsForRun: async (id) => (id === 3 ? new Set() : completeArtifacts),
    jobsForRun: async () => successfulMaterialization,
  });
  assert.equal(baseline?.id, 2);
});

test('rejects incomplete, expired, failed, and unrelated materialization baselines', async () => {
  const runs = [
    { id: 4, head_sha: 'unrelated', status: 'completed' },
    { id: 3, head_sha: 'failed', status: 'completed' },
    { id: 2, head_sha: 'expired', status: 'completed' },
    { id: 1, head_sha: 'running', status: 'in_progress' },
  ];
  const baseline = await selectMaterializedBaseline({
    runs,
    currentSha: 'head',
    isAncestor: (sha) => sha !== 'unrelated',
    artifactsForRun: async (id) => (id === 2 ? new Set() : completeArtifacts),
    jobsForRun: async (id) => (id === 3 ? [{
      name: 'materialize_artifacts',
      status: 'completed',
      conclusion: 'failure',
    }] : successfulMaterialization),
  });
  assert.equal(baseline, undefined);
});
