#!/usr/bin/env node

import { createHash } from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { cp, lstat, mkdir, mkdtemp, readFile, readdir, rm, stat, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { basename, dirname, join, relative, resolve } from 'node:path';

const schema = 1;
const target = 'wasm-web';
const partSize = 99 * 1024 * 1024;
const repoRoot = resolve(dirname(new URL(import.meta.url).pathname), '..', '..');
const lanes = ['wasm32', 'wasm32-simd', 'wasm64', 'wasm64-simd'];

function usage() {
  console.log(`Usage: node scripts/wasm-deps/package.mjs [--output-repo <path>] [--lock-file <path>] [--skip-build]
       node scripts/wasm-deps/package.mjs --lane <lane> --stage <directory>
       node scripts/wasm-deps/package.mjs --from-stage <directory> [--output-repo <path>] [--lock-file <path>]

Packages the browser dependency SDK below:
  wasm-deps/v1/wasm-web/<recipe-hash>/manifest.json
  wasm-deps/v1/wasm-web/<recipe-hash>/parts/effindom-wasm-deps.tar.zst.part-000

The default output repository is ../EffinDOM-Native-Deps. Each part is below
GitHub's 100 MB blob limit and every member is SHA-256 verified by consumers.

Without --lane or --from-stage it builds every lane (unless --skip-build) and
publishes the SDK. A CI job per lane builds its lane
(EFFINDOM_WASM_LANES=<lane> EFFINDOM_WASM_LANES_ONLY=1) and stages it with
--lane, the wasm32 lane adding the shared sources, licenses and ICU data;
--from-stage publishes the SDK from the directory the lanes were staged into.
The lock file defaults to the runtime export template's, or to
WasmDeps.lock.json in an exported checkout.
`);
}

function parseArgs() {
  const result = { outputRepo: process.env.EFFINDOM_WASM_DEPS_REPO ?? '', skipBuild: false, lane: '', stage: '', fromStage: '', lockFile: '' };
  const args = process.argv.slice(2);
  while (args.length > 0) {
    const arg = args.shift();
    if (arg === '--output-repo') result.outputRepo = args.shift() ?? '';
    else if (arg === '--skip-build') result.skipBuild = true;
    else if (arg === '--lane') result.lane = args.shift() ?? '';
    else if (arg === '--stage') result.stage = args.shift() ?? '';
    else if (arg === '--from-stage') result.fromStage = args.shift() ?? '';
    else if (arg === '--lock-file') result.lockFile = args.shift() ?? '';
    else if (arg === '-h' || arg === '--help') { usage(); process.exit(0); }
    else throw new Error(`Unknown argument: ${arg}`);
  }
  if (result.lane && !lanes.includes(result.lane)) throw new Error(`Unknown lane: ${result.lane} (expected ${lanes.join(', ')})`);
  if (Boolean(result.lane) !== Boolean(result.stage)) throw new Error('--lane and --stage go together.');
  return result;
}

async function defaultOutputRepo() {
  const sibling = resolve(repoRoot, '..', 'EffinDOM-Native-Deps');
  if ((await lstat(join(sibling, '.git')).catch(() => null))?.isDirectory()) return sibling;
  // The source monorepo keeps generated public repositories under ../zion-sati.
  return resolve(repoRoot, '..', 'zion-sati', 'EffinDOM-Native-Deps');
}

function run(command, args, options = {}) {
  const result = spawnSync(command, args, { cwd: repoRoot, stdio: 'inherit', ...options });
  if (result.status !== 0) throw new Error(`${command} failed with exit code ${result.status ?? 'unknown'}`);
}

function runText(command, args) {
  const result = spawnSync(command, args, { cwd: repoRoot, encoding: 'utf8' });
  if (result.status !== 0) throw new Error(`${command} ${args.join(' ')} failed: ${result.stderr}`);
  return result.stdout.trim();
}

function hash(content) {
  return createHash('sha256').update(content).digest('hex');
}

async function hashFile(path) {
  return hash(await readFile(path));
}

async function walk(root) {
  const files = [];
  for (const entry of await readdir(root)) {
    const path = join(root, entry);
    const info = await lstat(path);
    if (info.isDirectory()) files.push(...await walk(path));
    else if (info.isFile()) files.push(path);
  }
  return files;
}

async function required(source, destination) {
  if (!(await stat(source).catch(() => null))?.isFile()) throw new Error(`Missing required WASM dependency artifact: ${source}`);
  await mkdir(dirname(destination), { recursive: true });
  await cp(source, destination);
}

async function requiredDirectory(source, destination) {
  if (!(await stat(source).catch(() => null))?.isDirectory()) throw new Error(`Missing required WASM dependency directory: ${source}`);
  await mkdir(dirname(destination), { recursive: true });
  await cp(source, destination, { recursive: true });
}

async function copyLicense(source, destination, name) {
  const candidates = (await readdir(source)).filter((entry) => /^(license|copying)/i.test(entry));
  if (candidates.length === 0) throw new Error(`No license file found in ${source}`);
  await mkdir(destination, { recursive: true });
  await cp(join(source, candidates[0]), join(destination, `${name}-${candidates[0]}`));
}

async function splitFile(source, destination) {
  const input = await readFile(source);
  const parts = [];
  for (let offset = 0, index = 0; offset < input.length; offset += partSize, index += 1) {
    const name = `effindom-wasm-deps.tar.zst.part-${String(index).padStart(3, '0')}`;
    const content = input.subarray(offset, Math.min(offset + partSize, input.length));
    await writeFile(join(destination, name), content);
    parts.push({ path: `parts/${name}`, bytes: content.length, sha256: hash(content) });
  }
  return parts;
}

function laneBuildDirectory(lane) {
  return join(repoRoot, 'build', `build-v2-ui-${lane}`);
}

/**
 * A fetched dependency's source tree: the shared revision-keyed cache (cmake/EffinDomFetchContentSources.cmake), or
 * FetchContent's own copy in the lane's build.
 */
async function fetchedSource(build, dependency) {
  const cache = join(repoRoot, 'build', 'fetchcontent-sources');
  const cached = (await readdir(cache).catch(() => [])).filter((entry) => entry.startsWith(`${dependency}-`)).sort();
  if (cached.length > 1) throw new Error(`More than one cached ${dependency} source: ${cached.join(', ')}`);
  return cached.length === 1 ? join(cache, cached[0]) : join(build, '_deps', `${dependency}-src`);
}

async function findIcuData() {
  const root = join(repoRoot, 'build', 'build-v2-browser-bridge-icu', 'data', 'out', 'tmp');
  const files = await readdir(root).catch(() => []);
  const name = files.find((entry) => /^icudt.*l\.dat$/.test(entry));
  if (!name) throw new Error('Missing filtered ICU data. Run the browser bridge build before packaging.');
  return join(root, name);
}

async function lockPath(lockFile) {
  if (lockFile) return resolve(lockFile);
  const template = join(repoRoot, 'scripts', 'oss-export', 'templates', 'runtime');
  if ((await stat(template).catch(() => null))?.isDirectory()) return join(template, 'WasmDeps.lock.json');
  return join(repoRoot, 'WasmDeps.lock.json');
}

/** Copies one built lane into the SDK bundle; the wasm32 lane also brings the shared sources, licenses and ICU data. */
async function stageLane(lane, bundle) {
  const build = laneBuildDirectory(lane);
  if (!(await stat(build).catch(() => null))?.isDirectory()) throw new Error(`Missing WASM build output: ${build}`);
  const laneRoot = join(bundle, 'lanes', lane);
  const skiaDirectory = lane === 'wasm32' ? 'wasm' : lane === 'wasm32-simd' ? 'wasm-simd' : lane === 'wasm64' ? 'wasm64' : 'wasm64-simd';
  await requiredDirectory(join(repoRoot, 'skia', skiaDirectory), join(laneRoot, 'skia'));
  const libraries = {
    'harfbuzz.a': join(build, 'effindom-harfbuzz-meson-build', 'src', 'libharfbuzz.a'),
    'icu-common.a': join(build, 'libeffindom_icu_common.a'),
    'icu-stubdata.a': join(build, 'libeffindom_icu_stubdata.a'),
    'icu-i18n.a': join(build, 'libeffindom_icu_i18n.a'),
    'yoga.a': join(build, '_deps', 'yoga-build', 'libyogacore.a'),
    'woff2-common.a': join(build, 'libeffindom_woff2common.a'),
    'woff2-dec.a': join(build, 'libeffindom_woff2dec.a'),
    'brotli-common.a': join(build, '_deps', 'effindom_pinned_brotli-build', 'libbrotlicommon.a'),
    'brotli-dec.a': join(build, '_deps', 'effindom_pinned_brotli-build', 'libbrotlidec.a'),
  };
  for (const [name, source] of Object.entries(libraries)) await required(source, join(laneRoot, 'lib', name));
  await required(join(build, 'effindom-harfbuzz-meson-build', 'config.h'), join(laneRoot, 'generated', 'harfbuzz', 'config.h'));
  await required(join(build, 'effindom-harfbuzz-meson-build', 'src', 'hb-version.h'), join(laneRoot, 'generated', 'harfbuzz', 'hb-version.h'));
  await required(join(build, 'effindom-harfbuzz-meson-build', 'src', 'hb-features.h'), join(laneRoot, 'generated', 'harfbuzz', 'hb-features.h'));
  if (lane !== 'wasm32') return;

  const icu = await fetchedSource(build, 'effindom_skia_pinned_icu');
  const sources = {
    yoga: join(await fetchedSource(build, 'yoga'), 'yoga'),
    harfbuzz: join(await fetchedSource(build, 'effindom_skia_pinned_harfbuzz'), 'src'),
    'icu-common': join(icu, 'source', 'common'),
    'icu-i18n': join(icu, 'source', 'i18n'),
    woff2: join(await fetchedSource(build, 'effindom_pinned_woff2'), 'include'),
    brotli: join(await fetchedSource(build, 'effindom_pinned_brotli'), 'c', 'include'),
  };
  for (const [name, source] of Object.entries(sources)) await requiredDirectory(source, join(bundle, 'sources', name));
  await required(await findIcuData(), join(bundle, 'icu-data', 'icudtl.dat'));
  const licenseRoots = {
    yoga: dirname(sources.yoga),
    harfbuzz: dirname(sources.harfbuzz),
    icu: dirname(dirname(sources['icu-common'])),
    woff2: dirname(sources.woff2),
    brotli: dirname(dirname(sources.brotli)),
  };
  for (const [name, source] of Object.entries(licenseRoots)) await copyLicense(source, join(bundle, 'licenses'), name);
}

/** Archives a staged bundle into the output repository and pins it in the lock file. */
async function publish(bundle, options) {
  for (const lane of lanes) {
    if (!(await stat(join(bundle, 'lanes', lane, 'skia')).catch(() => null))?.isDirectory()) throw new Error(`The staged SDK is missing the ${lane} lane.`);
  }
  if (!(await stat(join(bundle, 'icu-data', 'icudtl.dat')).catch(() => null))?.isFile()) throw new Error('The staged SDK is missing the wasm32 lane\'s sources and ICU data.');
  const outputRepo = options.outputRepo ? resolve(options.outputRepo) : await defaultOutputRepo();
  if (!(await lstat(join(outputRepo, '.git')).catch(() => null))?.isDirectory()) {
    throw new Error(`Expected a Git checkout at ${outputRepo}. Clone EffinDOM-Native-Deps there or pass --output-repo.`);
  }
  const emscripten = runText('emcc', ['--version']).split('\n')[0];
  const recipe = {
    schema,
    target,
    emscripten,
    effindom_commit: runText('git', ['rev-parse', 'HEAD']),
    inputs_sha256: hash(Buffer.concat(await Promise.all([
      'CMakeLists.txt', 'cmake/PrebuiltWasmDeps.cmake', 'cmake/SkiaWasm.cmake', 'cmake/SkiaPinnedTextDeps.cmake', 'cmake/PinnedWoff2Deps.cmake',
      'scripts/build_skia_wasm.sh', 'scripts/wasm-deps/package.mjs', 'scripts/wasm-deps/prepare.mjs',
      'v2/browser-bridge/scripts/build.sh', 'v2/core/scripts/build_wasm_arch.sh', 'v2/ui/scripts/build_wasm_arch.sh',
    ].map((file) => readFile(join(repoRoot, file)))))),
  };
  const recipeHash = hash(JSON.stringify(recipe));
  const destination = join(outputRepo, 'wasm-deps', `v${schema}`, target, recipeHash);
  const temporary = await mkdtemp(join(tmpdir(), 'effindom-wasm-deps-'));
  try {
    const archive = join(temporary, `${bundleName}.tar.zst`);
    run('tar', ['-cf', join(temporary, `${bundleName}.tar`), '-C', dirname(bundle), basename(bundle)]);
    run('zstd', ['-T0', '-19', '-q', `${bundleName}.tar`, '-o', archive], { cwd: temporary });
    const archiveContent = await readFile(archive);
    await rm(destination, { recursive: true, force: true });
    await mkdir(join(destination, 'parts'), { recursive: true });
    const parts = await splitFile(archive, join(destination, 'parts'));
    const files = [];
    for (const file of (await walk(bundle)).sort()) {
      const info = await stat(file);
      files.push({ path: relative(bundle, file).replaceAll('\\', '/'), bytes: info.size, sha256: await hashFile(file) });
    }
    const manifest = { schema, recipe_hash: recipeHash, recipe, archive: { bytes: archiveContent.length, sha256: hash(archiveContent), parts }, files };
    await writeFile(join(destination, 'manifest.json'), `${JSON.stringify(manifest, null, 2)}\n`);
    const baseUrl = process.env.EFFINDOM_WASM_DEPS_BASE_URL ?? 'https://native-deps.effindom.dev';
    const manifestUrl = `${baseUrl}/wasm-deps/v${schema}/${target}/${recipeHash}/manifest.json`;
    const lock = await lockPath(options.lockFile);
    await writeFile(lock, `${JSON.stringify({ schema, base_url: baseUrl, targets: { [target]: { recipe_hash: recipeHash, manifest_url: manifestUrl, manifest_sha256: await hashFile(join(destination, 'manifest.json')), emscripten } } }, null, 2)}\n`);
    console.log(`\nWASM dependency SDK complete:\n  ${destination}\n\nPublish:\n  cd ${outputRepo}\n  git add wasm-deps/v${schema}/${target}/${recipeHash}\n  git commit -m "Publish WASM deps for ${target}"\n  git push\n\nUpdated lock file:\n  ${lock}`);
  } finally {
    await rm(temporary, { recursive: true, force: true });
  }
}

const bundleName = 'effindom-wasm-deps';

async function main() {
  const options = parseArgs();
  if (options.lane) {
    const bundle = join(resolve(options.stage), bundleName);
    await stageLane(options.lane, bundle);
    console.log(`Staged the ${options.lane} lane into ${bundle}.`);
    return;
  }
  if (options.fromStage) {
    await publish(join(resolve(options.fromStage), bundleName), options);
    return;
  }
  if (!options.skipBuild) run('bash', ['v2/browser-bridge/scripts/build.sh'], { env: { ...process.env, EFFINDOM_WASM_DEPS_MODE: 'source' } });
  const temporary = await mkdtemp(join(tmpdir(), 'effindom-wasm-deps-stage-'));
  try {
    const bundle = join(temporary, bundleName);
    for (const lane of lanes) await stageLane(lane, bundle);
    await publish(bundle, options);
  } finally {
    await rm(temporary, { recursive: true, force: true });
  }
}

main().catch((error) => { console.error(`ERROR: ${error.message}`); process.exit(1); });
