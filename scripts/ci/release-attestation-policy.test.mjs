import assert from 'node:assert/strict';
import test from 'node:test';

import { isVerifiedWasmProducer } from './release-attestation-policy.mjs';

test('a successful WASM job remains reusable when an unrelated native job fails', () => {
  const run = { status: 'completed', conclusion: 'failure' };
  const jobs = [
    { name: 'wasm / test', status: 'completed', conclusion: 'success' },
    { name: 'linux_x64 / test', status: 'completed', conclusion: 'failure' },
  ];

  assert.equal(isVerifiedWasmProducer(run, jobs), true);
});

test('an absent, skipped, running, or failed WASM job is not a verified producer', () => {
  const run = { status: 'completed', conclusion: 'success' };

  assert.equal(isVerifiedWasmProducer(run, []), false);
  assert.equal(isVerifiedWasmProducer(run, [
    { name: 'wasm / test', status: 'completed', conclusion: 'skipped' },
  ]), false);
  assert.equal(isVerifiedWasmProducer(run, [
    { name: 'wasm / test', status: 'in_progress', conclusion: null },
  ]), false);
  assert.equal(isVerifiedWasmProducer(run, [
    { name: 'wasm / test', status: 'completed', conclusion: 'failure' },
  ]), false);
  assert.equal(isVerifiedWasmProducer(
    { status: 'in_progress', conclusion: null },
    [{ name: 'wasm / test', status: 'completed', conclusion: 'success' }],
  ), false);
});
