import assert from 'node:assert/strict';
import { existsSync, readFileSync } from 'node:fs';
import test from 'node:test';

import {
  affectsArtifactScope,
  affectsScope,
  classifyPaths,
  nativeScopeNames,
  scopeNames,
} from './runtime-dependency-scope.mjs';

test('the scope classifier itself reruns every runtime CI target', () => {
  for (const path of [
    'scripts/ci/runtime-ci-baseline.mjs',
    'scripts/ci/runtime-dependency-scope.mjs',
  ]) {
    const result = classifyPaths([path]);
    assert.deepEqual(result, Object.fromEntries(scopeNames.map((scope) => [scope, true])));
  }
});

test('release orchestration does not invalidate reusable runtime artifacts', () => {
  for (const scope of scopeNames) {
    assert.equal(affectsScope('scripts/ci/verify-release-attestation.mjs', scope), false);
    assert.equal(affectsArtifactScope('scripts/ci/verify-release-attestation.mjs', scope), false);
  }
});

test('WASM source changes invalidate WASM artifacts without affecting native-only scopes', () => {
  assert.equal(affectsArtifactScope('v2/browser-bridge/src/bridge.ts', 'wasm'), true);
  assert.equal(affectsArtifactScope('v2/browser-bridge/src/bridge.ts', 'windows_x64'), false);
});

test('native packaging changes rerun native targets without invalidating WASM artifacts', () => {
  for (const path of ['scripts/package-native-runtime.mjs', 'scripts/native-runtime-artifact.mjs']) {
    assert.equal(affectsArtifactScope(path, 'wasm'), false);
    for (const scope of scopeNames.filter((scope) => scope !== 'wasm')) {
      assert.equal(affectsArtifactScope(path, scope), true);
    }
  }
});

test('runtime CI is triggered by native packaging changes', () => {
  const workflowPath = [
    '.github/workflows/runtime-ci.yml',
    'scripts/oss-export/templates/runtime/.github/workflows/runtime-ci.yml',
  ].find(existsSync);
  assert.notEqual(workflowPath, undefined);
  const workflow = readFileSync(workflowPath, 'utf8');
  assert.match(workflow, /^\s+- 'scripts\/package-native-runtime\.mjs'$/m);
  assert.match(workflow, /^\s+- 'scripts\/native-runtime-artifact\.mjs'$/m);
});

test('target workflow changes invalidate only that target artifact', () => {
  assert.equal(affectsArtifactScope('.github/workflows/wasm-ci.yml', 'wasm'), true);
  assert.equal(affectsArtifactScope('.github/workflows/wasm-ci.yml', 'macos_arm64'), false);
});

test('a change to one native target rebuilds one complete native provenance set', () => {
  const result = classifyPaths(['v2/native/linux/src/platform/LinuxNativePlatform.cpp']);
  assert.equal(result.wasm, false);
  for (const scopeName of nativeScopeNames) assert.equal(result[scopeName], true);
});
