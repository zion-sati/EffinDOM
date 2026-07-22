#!/usr/bin/env node

import { spawnSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';

const root = resolve(dirname(new URL(import.meta.url).pathname), '..');
const targets = {
  'macos-arm64': { preset: 'macos-arm64-appleclang-release-metal', buildDir: 'build/native-macos-arm64-appleclang-release-metal' },
  'macos-x64': { preset: 'macos-x64-appleclang-release-metal', buildDir: 'build/native-macos-x64-appleclang-release-metal' },
  'linux-x64': { buildDir: 'build/native-linux-x64-gnu-release-vulkan', architecture: 'x64' },
  'linux-arm64': { buildDir: 'build/native-linux-arm64-gnu-release-vulkan', architecture: 'arm64' },
  'windows-x64': { preset: 'windows-x64-msvc-release-d3d', architecture: 'x64' },
  'windows-arm64': { preset: 'windows-arm64-msvc-release-d3d', architecture: 'arm64' },
};

function run(command, args) {
  const result = spawnSync(command, args, { cwd: root, stdio: 'inherit' });
  if (result.status !== 0) process.exit(result.status ?? 1);
}

const args = process.argv.slice(2);
const targetIndex = args.indexOf('--target');
const targetName = targetIndex === -1 ? '' : args[targetIndex + 1];
const withTests = args.includes('--with-tests');
if (!targets[targetName]) {
  console.error(`Usage: node scripts/build-native-runtime.mjs --target <${Object.keys(targets).join('|')}> [--with-tests]`);
  process.exit(1);
}
if (!existsSync(join(root, 'cmake', 'EffinDomOssDistribution.cmake'))) {
  console.error('build-native-runtime requires the EffinDOM distribution configuration.');
  process.exit(1);
}

const target = targets[targetName];
const deps = spawnSync('node', [join(root, 'scripts', 'prepare-native-deps.mjs'), '--target', targetName], { cwd: root, encoding: 'utf8' });
if (deps.status !== 0) {
  process.stderr.write(deps.stderr);
  process.exit(deps.status ?? 1);
}
const dependencyRoot = deps.stdout.trim();
const common = [`-DEFFINDOM_NATIVE_DEPS_ROOT=${dependencyRoot}`, '-DEFFINDOM_BUILD_NATIVE_FUI_RS_DEMO=OFF'];

if (targetName.startsWith('windows-')) {
  run('cmake', ['--preset', target.preset, ...common]);
  run('cmake', ['--build', '--preset', target.preset]);
  if (withTests) run('ctest', ['--preset', target.preset]);
} else {
  if (targetName.startsWith('macos-')) {
    run('cmake', ['--preset', target.preset, ...common]);
  } else {
    run('cmake', ['-S', root, '-B', join(root, target.buildDir), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', `-DEFFINDOM_TARGET_ARCH=${target.architecture}`, '-DEFFINDOM_BUILD_NATIVE_LINUX=ON', '-DEFFINDOM_NATIVE_GRAPHICS_BACKEND=vulkan', ...common]);
  }
  run('cmake', ['--build', join(root, target.buildDir)]);
  if (withTests) run('ctest', ['--test-dir', join(root, target.buildDir), '--output-on-failure']);
}
