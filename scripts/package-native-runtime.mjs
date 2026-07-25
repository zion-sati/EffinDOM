#!/usr/bin/env node

import { spawnSync } from 'node:child_process';
import { chmodSync, copyFileSync, existsSync, mkdirSync, readdirSync, writeFileSync } from 'node:fs';
import { basename, dirname, join, relative, resolve, sep } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const targets = {
  'macos-arm64': { buildDir: 'build/native-macos-arm64-appleclang-release-metal', family: 'macos', minimum: '13.0', host: 'libeffindom_v2_macos_native_host.a', common: 'libeffindom_v2_native_common.a', core: 'libeffindom_core.dylib', ui: 'libeffindom_ui.dylib', sdl: 'libSDL3.0.dylib', system: ['Metal.framework', 'QuartzCore.framework', 'AppKit.framework', 'CoreText.framework', 'ApplicationServices.framework', 'CoreFoundation.framework', 'CoreGraphics.framework', 'CoreServices.framework', 'ImageIO.framework', 'Foundation.framework', 'expat', 'm'] },
  'macos-x64': { buildDir: 'build/native-macos-x64-appleclang-release-metal', family: 'macos', minimum: '13.0', host: 'libeffindom_v2_macos_native_host.a', common: 'libeffindom_v2_native_common.a', core: 'libeffindom_core.dylib', ui: 'libeffindom_ui.dylib', sdl: 'libSDL3.0.dylib', system: ['Metal.framework', 'QuartzCore.framework', 'AppKit.framework', 'CoreText.framework', 'ApplicationServices.framework', 'CoreFoundation.framework', 'CoreGraphics.framework', 'CoreServices.framework', 'ImageIO.framework', 'Foundation.framework', 'expat', 'm'] },
  'linux-arm64': { buildDir: 'build/native-linux-arm64-gnu-release-vulkan', family: 'glibc', minimum: '2.36', host: 'libeffindom_v2_linux_native_host.a', common: 'libeffindom_v2_native_common.a', core: 'libeffindom_core.so', ui: 'libeffindom_ui.so', sdl: 'libSDL3.so', system: ['vulkan', 'fontconfig', 'dbus-1', 'X11', 'Xext', 'dl', 'pthread', 'expat', 'm'] },
  'linux-x64': { buildDir: 'build/native-linux-x64-gnu-release-vulkan', family: 'glibc', minimum: '2.36', host: 'libeffindom_v2_linux_native_host.a', common: 'libeffindom_v2_native_common.a', core: 'libeffindom_core.so', ui: 'libeffindom_ui.so', sdl: 'libSDL3.so', system: ['vulkan', 'fontconfig', 'dbus-1', 'X11', 'Xext', 'dl', 'pthread', 'expat', 'm'] },
  'windows-arm64': { buildDir: 'build/native-windows-arm64-msvc-release-d3d', family: 'windows', minimum: '10.0.17763', host: 'effindom_v2_windows_native_host.lib', common: 'effindom_v2_native_common.lib', core: 'effindom_core.dll', coreImport: 'effindom_core.lib', ui: 'effindom_ui.dll', uiImport: 'effindom_ui.lib', sdl: 'SDL3.dll', sdlImport: 'SDL3.lib', system: ['shell32', 'dwrite', 'ole32', 'comctl32', 'dwmapi', 'uiautomationcore', 'd3d12', 'dxgi', 'd3dcompiler', 'ws2_32', 'ntdll', 'userenv'] },
  'windows-x64': { buildDir: 'build/native-windows-x64-msvc-release-d3d', family: 'windows', minimum: '10.0.17763', host: 'effindom_v2_windows_native_host.lib', common: 'effindom_v2_native_common.lib', core: 'effindom_core.dll', coreImport: 'effindom_core.lib', ui: 'effindom_ui.dll', uiImport: 'effindom_ui.lib', sdl: 'SDL3.dll', sdlImport: 'SDL3.lib', system: ['shell32', 'dwrite', 'ole32', 'comctl32', 'dwmapi', 'uiautomationcore', 'd3d12', 'dxgi', 'd3dcompiler', 'ws2_32', 'ntdll', 'userenv'] },
};

function fail(message) { console.error(`ERROR: ${message}`); process.exit(1); }
function option(name) { const index = process.argv.indexOf(name); return index < 0 ? '' : process.argv[index + 1] ?? ''; }
function run(command, args, options = {}) {
  const result = spawnSync(command, args, { cwd: root, encoding: options.capture ? 'utf8' : undefined, stdio: options.capture ? ['ignore', 'pipe', 'inherit'] : 'inherit', env: options.env ?? process.env });
  if (result.status !== 0) process.exit(result.status ?? 1);
  return result.stdout?.trim() ?? '';
}
function walk(directory) {
  if (!existsSync(directory)) return [];
  return readdirSync(directory, { withFileTypes: true }).flatMap((entry) => {
    const path = join(directory, entry.name);
    return entry.isDirectory() ? walk(path) : entry.isFile() ? [path] : [];
  });
}
function findUnique(roots, name) {
  const excluded = [`${sep}output${sep}`, `${sep}test-output${sep}`, `${sep}relocated${sep}`];
  const matches = roots.flatMap(walk).filter((path) => basename(path) === name && !excluded.some((part) => path.includes(part)));
  if (matches.length !== 1) fail(`expected one ${name}, found ${matches.length}: ${matches.join(', ')}`);
  return matches[0];
}
function input(source, path, role, executable = false) { return { source, path, role, executable }; }

const targetName = option('--target');
const sourceCommit = option('--source-commit');
const destination = resolve(option('--destination'));
const target = targets[targetName];
if (!target || !/^[0-9a-f]{40}$|^[0-9a-f]{64}$/.test(sourceCommit) || !option('--destination')) {
  fail(`usage: package-native-runtime.mjs --target <${Object.keys(targets).join('|')}> --source-commit <sha> --destination <directory>`);
}
const buildRoot = resolve(root, target.buildDir);
if (!existsSync(buildRoot)) fail(`native build is missing: ${buildRoot}`);
const dependencyRoot = run('node', [join(root, 'scripts/prepare-native-deps.mjs'), '--target', targetName], { capture: true });
const packageTarget = join(buildRoot, 'native-packager');
run('cargo', ['build', '--locked', '--release', '--manifest-path', join(root, 'v2/native/packaging/Cargo.toml'), '--bin', 'effindom-native-packager'], { env: { ...process.env, CARGO_TARGET_DIR: packageTarget } });
const executableName = process.platform === 'win32' ? 'effindom-native-packager.exe' : 'effindom-native-packager';
const packager = join(packageTarget, 'release', executableName);

const from = (name) => findUnique([buildRoot], name);
const fromDeps = (name) => findUnique([dependencyRoot], name);
const files = [
  input(from(target.host), `sdk/lib/${target.host}`, 'host-library'),
  input(from(target.common), `sdk/lib/${target.common}`, 'host-library'),
  input(from(target.core), `runtime/lib/${target.core}`, 'runtime-library'),
  input(from(target.ui), `runtime/lib/${target.ui}`, 'runtime-library'),
  input(fromDeps(target.sdl), `runtime/lib/${target.sdl}`, 'runtime-library'),
  input(join(root, 'v2/native/common/src/NativeApplicationMain.cpp'), 'sdk/launcher/NativeApplicationMain.cpp', 'launcher'),
  input(packager, `tools/${executableName}`, 'packager', true),
];
for (const [name, output] of [[target.coreImport, 'sdk/lib'], [target.uiImport, 'sdk/lib'], [target.sdlImport, 'sdk/lib']]) {
  if (name) files.push(input(name === target.sdlImport ? fromDeps(name) : from(name), `${output}/${name}`, 'host-library'));
}
for (const header of walk(join(root, 'v2/native/common/include'))) {
  files.push(input(header, `sdk/include/${relative(join(root, 'v2/native/common/include'), header).split(sep).join('/')}`, 'launcher'));
}
const dependencyLinkInputs = walk(dependencyRoot).filter((path) => {
  const name = basename(path).toLowerCase();
  return (name.endsWith('.a') || name.endsWith('.lib')) && name !== 'sdl3.lib';
});
for (const library of dependencyLinkInputs) {
  files.push(input(library, `sdk/deps/${relative(dependencyRoot, library).split(sep).join('/')}`, 'host-library'));
}
for (const font of walk(join(root, 'v2/fonts'))) {
  files.push(input(font, `runtime/assets/fonts/${relative(join(root, 'v2/fonts'), font).split(sep).join('/')}`, 'runtime-asset'));
}
const metadataRoot = join(buildRoot, 'native-runtime-package-inputs');
mkdirSync(metadataRoot, { recursive: true });
const linkMetadata = join(metadataRoot, 'link.json');
const suffix = targetName.startsWith('windows-') ? '.lib' : '.a';
const dependencyPath = (stem) => {
  const names = [`${stem}${suffix}`, `lib${stem}${suffix}`];
  const library = dependencyLinkInputs.find((path) => names.includes(basename(path).toLowerCase()));
  if (!library) fail(`required native link input is missing: ${names.join(' or ')}`);
  return `sdk/deps/${relative(dependencyRoot, library).split(sep).join('/')}`;
};
const orderedDependencies = [
  'svg', 'skshaper', 'skia', 'harfbuzz',
  'icu-i18n', 'icu-stubdata', 'icu-common', 'yoga',
].map(dependencyPath);
const runtimeLibrary = (name, importName) => importName ? `sdk/lib/${importName}` : `runtime/lib/${name}`;
writeFileSync(linkMetadata, `${JSON.stringify({ schemaVersion: 1, target: targetName, cxxStandard: 17, libraries: [`sdk/lib/${target.host}`, '<application-static-library>', `sdk/lib/${target.common}`, runtimeLibrary(target.core, target.coreImport), ...orderedDependencies.slice(0, 3), runtimeLibrary(target.ui, target.uiImport), ...orderedDependencies.slice(3), runtimeLibrary(target.sdl, target.sdlImport)], systemLibraries: target.system, runtimeLibraryDirectory: 'runtime/lib', includeDirectory: 'sdk/include', launcher: 'sdk/launcher/NativeApplicationMain.cpp' }, null, 2)}\n`);
files.push(input(linkMetadata, 'sdk/link.json', 'link-metadata'));
mkdirSync(dirname(destination), { recursive: true });
const requestPath = join(metadataRoot, 'request.json');
writeFileSync(requestPath, `${JSON.stringify({ schemaVersion: 1, sourceCommit, target: targetName, coreAbi: 2, uiAbi: 1, minimumOs: { family: target.family, version: target.minimum }, destination, files }, null, 2)}\n`);
const output = run(packager, ['create-runtime-artifact', requestPath], { capture: true });
const parsed = JSON.parse(output);
writeFileSync(join(destination, 'native-runtime-artifact-descriptor.json'), `${JSON.stringify(parsed.artifact, null, 2)}\n`);
copyFileSync(packager, join(destination, executableName));
if (process.platform !== 'win32') chmodSync(join(destination, executableName), 0o755);
console.log(destination);
