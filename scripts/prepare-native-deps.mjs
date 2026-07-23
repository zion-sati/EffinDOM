#!/usr/bin/env node

import { createHash } from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { appendFile, mkdir, mkdtemp, readFile, rename, rm, stat, writeFile } from 'node:fs/promises';
import { homedir, tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..');

function fail(message) {
  process.stderr.write(`ERROR: ${message}\n`);
  process.exit(1);
}

function parseArgs() {
  const result = { target: '', refresh: false };
  const args = process.argv.slice(2);
  while (args.length > 0) {
    const arg = args.shift();
    if (arg === '--target') result.target = args.shift() ?? '';
    else if (arg === '--refresh') result.refresh = true;
    else if (arg === '-h' || arg === '--help') {
      console.log('Usage: node scripts/prepare-native-deps.mjs --target <target> [--refresh]');
      process.exit(0);
    } else fail(`Unknown argument: ${arg}`);
  }
  if (!result.target) fail('--target is required.');
  return result;
}

function hash(content) {
  return createHash('sha256').update(content).digest('hex');
}

async function hashFile(path) {
  return hash(await readFile(path));
}

async function download(url) {
  const response = await fetch(url);
  if (!response.ok) throw new Error(`${url}: HTTP ${response.status}`);
  return Buffer.from(await response.arrayBuffer());
}

function run(command, args, cwd) {
  const result = spawnSync(command, args, { cwd, stdio: 'ignore' });
  if (result.status !== 0) throw new Error(`${command} failed with exit code ${result.status ?? 'unknown'}`);
}

async function verifyFiles(root, files) {
  for (const file of files) {
    const path = join(root, file.path);
    const info = await stat(path).catch(() => null);
    if (!info?.isFile() || info.size !== file.bytes || await hashFile(path) !== file.sha256) {
      throw new Error(`Extracted SDK validation failed for ${file.path}`);
    }
  }
}

async function main() {
  const options = parseArgs();
  const lock = JSON.parse(await readFile(join(repoRoot, 'NativeDeps.lock.json'), 'utf8'));
  const pinned = lock.targets?.[options.target];
  if (!pinned) fail(`No published native dependency SDK is pinned for ${options.target}. Build and publish it first, then update NativeDeps.lock.json.`);
  const cacheBase = process.env.EFFINDOM_NATIVE_DEPS_CACHE ?? join(process.env.XDG_CACHE_HOME ?? join(homedir(), '.cache'), 'effindom', 'native-deps');
  const cacheRoot = join(cacheBase, options.target, pinned.recipe_hash);
  const ready = join(cacheRoot, '.ready.json');
  if (!options.refresh) {
    const cached = await readFile(ready, 'utf8').catch(() => '');
    if (cached) {
      const metadata = JSON.parse(cached);
      if (metadata.manifest_sha256 === pinned.manifest_sha256) {
        process.stdout.write(`${join(cacheRoot, 'effindom-native-deps')}\n`);
        return;
      }
    }
  }

  const manifestContent = await download(pinned.manifest_url);
  if (hash(manifestContent) !== pinned.manifest_sha256) fail(`Manifest checksum mismatch: ${pinned.manifest_url}`);
  const manifest = JSON.parse(manifestContent.toString('utf8'));
  if (manifest.schema !== lock.schema || manifest.recipe_hash !== pinned.recipe_hash || manifest.recipe?.target !== options.target) {
    fail(`Manifest does not match the pinned ${options.target} dependency recipe.`);
  }

  const temporary = await mkdtemp(join(tmpdir(), 'effindom-native-deps-'));
  try {
    const archive = join(temporary, 'deps.tar.zst');
    for (const part of manifest.archive.parts) {
      if (!part.path.startsWith('parts/')) fail(`Unsafe part path in manifest: ${part.path}`);
      const content = await download(new URL(part.path, `${pinned.manifest_url.substring(0, pinned.manifest_url.lastIndexOf('/') + 1)}`).toString());
      if (content.length !== part.bytes || hash(content) !== part.sha256) fail(`Part checksum mismatch: ${part.path}`);
      await appendFile(archive, content);
    }
    if (await hashFile(archive) !== manifest.archive.sha256) fail('Combined archive checksum mismatch.');
    const tar = join(temporary, 'deps.tar');
    run('zstd', ['-d', '-q', '-f', archive, '-o', tar], temporary);
    run('tar', ['-xf', tar], temporary);
    const extracted = join(temporary, 'effindom-native-deps');
    await verifyFiles(extracted, manifest.files);
    await rm(cacheRoot, { recursive: true, force: true });
    await mkdir(cacheRoot, { recursive: true });
    await rename(extracted, join(cacheRoot, 'effindom-native-deps'));
    await writeFile(ready, `${JSON.stringify({ manifest_sha256: pinned.manifest_sha256 })}\n`);
  } finally {
    await rm(temporary, { recursive: true, force: true });
  }
  process.stdout.write(`${join(cacheRoot, 'effindom-native-deps')}\n`);
}

main().catch((error) => fail(error.message));
