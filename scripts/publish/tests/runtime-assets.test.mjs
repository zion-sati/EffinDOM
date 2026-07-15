import { strict as assert } from 'node:assert';
import { createHash } from 'node:crypto';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

const testDir = dirname(fileURLToPath(import.meta.url));
const repoRoot = resolve(testDir, '..', '..', '..');
const publisher = join(repoRoot, 'scripts', 'publish', 'runtime-assets.mjs');
const packageVersion = JSON.parse(readFileSync(join(repoRoot, 'v2', 'browser-bridge', 'package.json'), 'utf8')).version;
const root = mkdtempSync(join(tmpdir(), 'effindom-runtime-assets-test-'));

function digest(data) {
  return createHash('sha256').update(data).digest();
}

function integrity(data) {
  return `sha256-${digest(data).toString('base64')}`;
}

function run(command, args, cwd = repoRoot) {
  const result = spawnSync(command, args, { cwd, encoding: 'utf8' });
  assert.equal(result.status, 0, result.stderr || result.stdout);
  return result.stdout;
}

try {
  const packageRoot = join(root, 'package');
  const runtimeDir = join(packageRoot, 'dist', 'runtime');
  const fontsDir = join(packageRoot, 'dist', 'fonts');
  mkdirSync(runtimeDir, { recursive: true });
  mkdirSync(fontsDir, { recursive: true });
  const js = Buffer.from('export default function runtime() {}\n');
  const wasm = Buffer.from([0, 97, 115, 109, 1, 0, 0, 0]);
  const icu = Buffer.from('icu');
  writeFileSync(join(runtimeDir, 'core.hash.js'), js);
  writeFileSync(join(runtimeDir, 'core.hash.wasm'), wasm);
  writeFileSync(join(runtimeDir, 'icu.hash.dat'), icu);
  writeFileSync(join(fontsDir, 'Example.ttf'), Buffer.from('font'));
  writeFileSync(join(packageRoot, 'package.json'), JSON.stringify({
    name: '@effindomv2/runtime',
    version: packageVersion,
  }));
  writeFileSync(join(packageRoot, 'dist', 'effindom.v2.manifest.json'), JSON.stringify({
    version: '1.0',
    manifest_hash: 'fixture',
    architectures: {
      wasm32: {
        core: {
          js: './runtime/core.hash.js',
          js_integrity: integrity(js),
          wasm: './runtime/core.hash.wasm',
          wasm_integrity: integrity(wasm),
        },
      },
    },
    assets: {
      icu: { url: './runtime/icu.hash.dat', integrity: integrity(icu) },
    },
  }));
  const tarball = join(root, 'runtime.tgz');
  run('tar', ['-czf', tarball, '-C', root, 'package']);

  const destination = join(root, 'EffinDOM-Runtimes');
  mkdirSync(destination);
  writeFileSync(join(destination, 'README.md'), '# EffinDOM-Runtimes\n');
  const args = [publisher, '--destination', destination, '--tarball', tarball];
  const first = run(process.execPath, args);
  const second = run(process.execPath, args);
  assert.match(first, /Added: 5/);
  assert.match(second, /Added: 0/);
  const releases = JSON.parse(readFileSync(join(destination, 'v2', 'releases.json'), 'utf8'));
  const runtimeSetHash = releases.packages['@effindomv2/runtime'][packageVersion];
  const manifest = JSON.parse(readFileSync(join(destination, 'v2', 'manifests', `${runtimeSetHash}.json`), 'utf8'));
  assert.equal(manifest.runtime_set_hash, runtimeSetHash);
  assert.match(manifest.assets.fonts['Example.ttf'].url, /^\.\.\/assets\/Example\..+\.ttf$/);
  assert.equal(readFileSync(join(destination, 'CNAME'), 'utf8'), 'runtimes.effindom.dev\n');
  assert.equal(existsSync(join(destination, '.nojekyll')), false);
  console.log('runtime-assets publisher tests passed');
} finally {
  rmSync(root, { recursive: true, force: true });
}
