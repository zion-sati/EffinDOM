import assert from 'node:assert/strict';
import { mkdtempSync, mkdirSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import test from 'node:test';

import { resolveNativeBuildOutput } from '../native-runtime-package-layout.mjs';

function fixture() {
  const root = mkdtempSync(join(tmpdir(), 'effindom-runtime-layout-'));
  const create = (path) => {
    mkdirSync(dirname(path), { recursive: true });
    writeFileSync(path, path);
    return path;
  };
  return { root, create, remove: () => rmSync(root, { recursive: true, force: true }) };
}

test('selects canonical target outputs instead of copied headless runtime libraries', () => {
  const build = fixture();
  try {
    const canonical = build.create(join(build.root, 'v2', 'core', 'effindom_core.dll'));
    build.create(join(build.root, 'v2', 'native', 'headless', 'effindom_core.dll'));

    assert.equal(resolveNativeBuildOutput(build.root, 'core', 'effindom_core.dll'), canonical);
  } finally {
    build.remove();
  }
});

test('maps every runtime SDK build-output role to its canonical directory', () => {
  const build = fixture();
  try {
    const ui = build.create(join(build.root, 'v2', 'ui', 'effindom_ui.dll'));
    const common = build.create(join(build.root, 'v2', 'native', 'common', 'effindom_v2_native_common.lib'));
    const host = build.create(join(build.root, 'v2', 'native', 'windows', 'effindom_v2_windows_native_host.lib'));

    assert.equal(resolveNativeBuildOutput(build.root, 'ui', 'effindom_ui.dll'), ui);
    assert.equal(resolveNativeBuildOutput(build.root, 'commonHost', 'effindom_v2_native_common.lib'), common);
    assert.equal(resolveNativeBuildOutput(build.root, 'platformHost', 'effindom_v2_windows_native_host.lib', 'windows'), host);
  } finally {
    build.remove();
  }
});

test('fails diagnostically when the canonical output is absent', () => {
  const build = fixture();
  try {
    build.create(join(build.root, 'v2', 'native', 'headless', 'effindom_core.dll'));
    assert.throws(
      () => resolveNativeBuildOutput(build.root, 'core', 'effindom_core.dll'),
      /Canonical native build output is missing:.*v2.*core.*effindom_core\.dll/,
    );
  } finally {
    build.remove();
  }
});
