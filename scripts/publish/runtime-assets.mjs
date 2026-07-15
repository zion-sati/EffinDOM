#!/usr/bin/env node

import {
  copyFileSync,
  existsSync,
  mkdirSync,
  mkdtempSync,
  readFileSync,
  realpathSync,
  renameSync,
  readdirSync,
  rmSync,
  statSync,
  writeFileSync,
} from 'node:fs';
import { tmpdir } from 'node:os';
import { basename, dirname, extname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

import {
  computeRuntimeSetHash,
  contentHash,
  contentIntegrity,
} from '../../v2/browser-bridge/scripts/runtime-set.mjs';

const scriptDir = dirname(fileURLToPath(import.meta.url));
const repoRoot = resolve(scriptDir, '..', '..');
const runtimePackageDir = join(repoRoot, 'v2', 'browser-bridge');
const runtimePackageJson = readJson(join(runtimePackageDir, 'package.json'));
const expectedPackageName = '@effindomv2/runtime';

function fail(message) {
  throw new Error(message);
}

function readJson(path) {
  return JSON.parse(readFileSync(path, 'utf8'));
}

function parseArgs(args) {
  let destination = resolve(repoRoot, '..', 'EffinDOM-Runtimes');
  let tarball = null;
  for (let index = 0; index < args.length; index += 1) {
    const argument = args[index];
    if (argument === '--destination') {
      destination = resolve(args[index + 1] ?? fail('--destination requires a path.'));
      index += 1;
      continue;
    }
    if (argument === '--tarball') {
      tarball = resolve(args[index + 1] ?? fail('--tarball requires a path.'));
      index += 1;
      continue;
    }
    fail(`Unknown argument: ${argument}`);
  }
  return { destination, tarball };
}

function run(command, args, options = {}) {
  const result = spawnSync(command, args, {
    cwd: options.cwd ?? repoRoot,
    encoding: 'utf8',
    stdio: options.capture ? 'pipe' : 'inherit',
  });
  if (result.status !== 0) {
    const detail = options.capture ? `\n${result.stderr || result.stdout}` : '';
    fail(`${command} ${args.join(' ')} failed.${detail}`);
  }
  return result.stdout ?? '';
}

function verifyDestination(destination) {
  if (!existsSync(destination) || !statSync(destination).isDirectory()) {
    fail(`Missing sibling EffinDOM-Runtimes repository: ${destination}`);
  }
  const readme = join(destination, 'README.md');
  if (!existsSync(readme) || !readFileSync(readme, 'utf8').includes('EffinDOM-Runtimes')) {
    fail(`Destination does not identify itself as EffinDOM-Runtimes: ${destination}`);
  }
}

function acquireTarball(packageName, packageVersion, suppliedTarball, workDir) {
  if (suppliedTarball !== null) {
    if (!existsSync(suppliedTarball)) {
      fail(`Runtime package tarball does not exist: ${suppliedTarball}`);
    }
    return suppliedTarball;
  }
  const output = run(
    'npm',
    ['pack', `${packageName}@${packageVersion}`, '--json', '--ignore-scripts', '--pack-destination', workDir],
    { capture: true },
  );
  const result = JSON.parse(output)[0];
  if (typeof result?.filename !== 'string') {
    fail(`npm pack did not return a tarball for ${packageName}@${packageVersion}.`);
  }
  return join(workDir, result.filename);
}

function writeAtomic(path, text) {
  mkdirSync(dirname(path), { recursive: true });
  const temporary = `${path}.tmp-${process.pid}`;
  writeFileSync(temporary, text, 'utf8');
  renameSync(temporary, path);
}

function writeImmutable(path, text) {
  if (existsSync(path)) {
    if (readFileSync(path, 'utf8') !== text) {
      fail(`Refusing to replace immutable runtime file with different content: ${path}`);
    }
    return false;
  }
  writeAtomic(path, text);
  return true;
}

function contentAddressedName(fileName, data) {
  const extension = extname(fileName);
  const stem = fileName.slice(0, fileName.length - extension.length);
  return `${stem}.${contentHash(data)}${extension}`;
}

function stageAsset(source, assetDirectory, preferredName, counters) {
  const data = readFileSync(source);
  const destination = join(assetDirectory, preferredName);
  mkdirSync(assetDirectory, { recursive: true });
  if (existsSync(destination)) {
    if (!readFileSync(destination).equals(data)) {
      fail(`Content-address collision for ${destination}.`);
    }
    counters.reused.push(`v2/assets/${preferredName}`);
  } else {
    copyFileSync(source, destination);
    counters.added.push(`v2/assets/${preferredName}`);
  }
  return {
    url: `../assets/${preferredName}`,
    integrity: contentIntegrity(data),
  };
}

function runtimeSourceForUrl(runtimeDirectory, url) {
  const fileName = basename(url);
  const source = join(runtimeDirectory, fileName);
  if (!existsSync(source)) {
    fail(`Runtime manifest references a missing asset: ${url}`);
  }
  return source;
}

function stageRuntimeManifest(packageRoot, destination, packageVersion, counters) {
  const distDirectory = join(packageRoot, 'dist');
  const runtimeDirectory = join(distDirectory, 'runtime');
  const fontDirectory = join(distDirectory, 'fonts');
  const sourceManifest = readJson(join(distDirectory, 'effindom.v2.manifest.json'));
  const assetDirectory = join(destination, 'v2', 'assets');
  const architectures = {};

  for (const architecture of Object.keys(sourceManifest.architectures).sort()) {
    architectures[architecture] = {};
    for (const bundleName of Object.keys(sourceManifest.architectures[architecture]).sort()) {
      const bundle = sourceManifest.architectures[architecture][bundleName];
      const js = stageAsset(
        runtimeSourceForUrl(runtimeDirectory, bundle.js),
        assetDirectory,
        basename(bundle.js),
        counters,
      );
      const wasm = stageAsset(
        runtimeSourceForUrl(runtimeDirectory, bundle.wasm),
        assetDirectory,
        basename(bundle.wasm),
        counters,
      );
      if (js.integrity !== bundle.js_integrity || wasm.integrity !== bundle.wasm_integrity) {
        fail(`Integrity mismatch in ${architecture}/${bundleName}.`);
      }
      architectures[architecture][bundleName] = {
        js: js.url,
        js_integrity: js.integrity,
        wasm: wasm.url,
        wasm_integrity: wasm.integrity,
      };
    }
  }

  const icuSource = runtimeSourceForUrl(runtimeDirectory, sourceManifest.assets.icu.url);
  const icu = stageAsset(icuSource, assetDirectory, basename(icuSource), counters);
  if (icu.integrity !== sourceManifest.assets.icu.integrity) {
    fail('ICU integrity does not match the runtime manifest.');
  }

  const fonts = {};
  const canonicalFonts = {};
  if (existsSync(fontDirectory)) {
    for (const fileName of readdirSync(fontDirectory).sort()) {
      const source = join(fontDirectory, fileName);
      if (!statSync(source).isFile()) {
        continue;
      }
      const data = readFileSync(source);
      const staged = stageAsset(source, assetDirectory, contentAddressedName(fileName, data), counters);
      fonts[fileName] = staged;
      canonicalFonts[fileName] = staged.integrity;
    }
  }

  for (const fileName of readdirSync(runtimeDirectory).sort()) {
    const source = join(runtimeDirectory, fileName);
    if (statSync(source).isFile() && !existsSync(join(assetDirectory, fileName))) {
      stageAsset(source, assetDirectory, fileName, counters);
    }
  }

  const runtimeSetHash = computeRuntimeSetHash(sourceManifest, canonicalFonts);
  if (sourceManifest.runtime_set_hash !== undefined && sourceManifest.runtime_set_hash !== runtimeSetHash) {
    fail('Published package runtime_set_hash does not match its assets.');
  }
  const manifest = {
    version: sourceManifest.version,
    manifest_hash: sourceManifest.manifest_hash,
    runtime_set_hash: runtimeSetHash,
    architectures,
    assets: {
      icu,
      fonts,
    },
  };
  const manifestRelativePath = `v2/manifests/${runtimeSetHash}.json`;
  const manifestAdded = writeImmutable(
    join(destination, manifestRelativePath),
    `${JSON.stringify(manifest, null, 2)}\n`,
  );
  (manifestAdded ? counters.added : counters.reused).push(manifestRelativePath);

  const releasesPath = join(destination, 'v2', 'releases.json');
  const releases = existsSync(releasesPath)
    ? readJson(releasesPath)
    : { version: '1.0', packages: { [expectedPackageName]: {} } };
  releases.packages ??= {};
  releases.packages[expectedPackageName] ??= {};
  const existingRelease = releases.packages[expectedPackageName][packageVersion];
  if (existingRelease !== undefined && existingRelease !== runtimeSetHash) {
    fail(`Runtime package ${packageVersion} is already mapped to a different runtime set.`);
  }
  releases.packages[expectedPackageName][packageVersion] = runtimeSetHash;
  writeAtomic(releasesPath, `${JSON.stringify(releases, null, 2)}\n`);

  return runtimeSetHash;
}

function initializeRepository(destination) {
  const readme = `# EffinDOM-Runtimes

Immutable, content-addressed runtime assets for [EffinDOM](https://effindom.dev).

Files under \`v2/assets/\` and hashed manifests under \`v2/manifests/\` are published artifacts. They must never be modified or removed because released applications may depend on them indefinitely.

Runtime releases are staged by the canonical publisher in the EffinDOM repository. This repository does not build runtime binaries from source.
`;
  writeAtomic(join(destination, 'README.md'), readme);
  if (!existsSync(join(destination, 'CNAME'))) {
    writeFileSync(join(destination, 'CNAME'), 'runtimes.effindom.dev\n', 'utf8');
  }
  if (!existsSync(join(destination, '.gitignore'))) {
    writeFileSync(join(destination, '.gitignore'), '.DS_Store\n', 'utf8');
  }
}

function main() {
  if (runtimePackageJson.name !== expectedPackageName) {
    fail(`Expected runtime package ${expectedPackageName}, found ${runtimePackageJson.name}.`);
  }
  const packageVersion = runtimePackageJson.version;
  if (typeof packageVersion !== 'string' || packageVersion.length === 0) {
    fail('Runtime package.json does not contain a version.');
  }
  const options = parseArgs(process.argv.slice(2));
  verifyDestination(options.destination);
  const destination = realpathSync(options.destination);
  const workDirectory = mkdtempSync(join(tmpdir(), 'effindom-runtime-assets-'));
  try {
    console.log(`Staging ${expectedPackageName}@${packageVersion}`);
    console.log(`Destination: ${destination}`);
    const tarball = acquireTarball(expectedPackageName, packageVersion, options.tarball, workDirectory);
    const unpackDirectory = join(workDirectory, 'unpacked');
    mkdirSync(unpackDirectory);
    run('tar', ['-xzf', tarball, '-C', unpackDirectory]);
    const packageRoot = join(unpackDirectory, 'package');
    const packedPackageJson = readJson(join(packageRoot, 'package.json'));
    if (packedPackageJson.name !== expectedPackageName || packedPackageJson.version !== packageVersion) {
      fail(`Tarball identity ${packedPackageJson.name}@${packedPackageJson.version} does not match ${expectedPackageName}@${packageVersion}.`);
    }
    const counters = { added: [], reused: [] };
    initializeRepository(destination);
    const runtimeSetHash = stageRuntimeManifest(packageRoot, destination, packageVersion, counters);
    console.log(`Runtime set: ${runtimeSetHash}`);
    console.log(`Added: ${counters.added.length}`);
    for (const path of counters.added) console.log(`  + ${path}`);
    console.log(`Reused: ${counters.reused.length}`);
    for (const path of counters.reused) console.log(`  = ${path}`);
  } finally {
    rmSync(workDirectory, { recursive: true, force: true });
  }
}

try {
  main();
} catch (error) {
  console.error(error instanceof Error ? error.message : String(error));
  process.exitCode = 1;
}
