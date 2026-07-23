import { execFileSync } from 'node:child_process';
import { appendFileSync, readFileSync } from 'node:fs';

const exactPaths = new Set([
  'package.json',
  'package-lock.json',
  'CMakeLists.txt',
  'CMakePresets.json',
  'WasmDeps.lock.json',
  'NativeDeps.lock.json',
  '.github/workflows/runtime-ci.yml',
]);

const sharedPrefixes = [
  'cmake/',
  'scripts/ci/',
  'v2/core/',
  'v2/ui/',
];

const scopes = {
  wasm: {
    exact: new Set(['.github/workflows/wasm-ci.yml']),
    prefixes: [
      ...sharedPrefixes,
      'scripts/wasm-deps/',
      'v2/abi/',
      'v2/hostgen/',
      'v2/fonts/',
      'v2/browser-bridge/',
    ],
  },
  macos_arm64: {
    exact: new Set(['.github/workflows/macos-native-ci.yml']),
    prefixes: [...sharedPrefixes, 'scripts/build-native-runtime.mjs', 'v2/native/common/', 'v2/native/macos/'],
  },
  macos_x64: {
    exact: new Set(['.github/workflows/macos-x64-native-ci.yml']),
    prefixes: [...sharedPrefixes, 'scripts/build-native-runtime.mjs', 'v2/native/common/', 'v2/native/macos/'],
  },
  linux_x64: {
    exact: new Set(['.github/workflows/linux-x64-native-ci.yml']),
    prefixes: [...sharedPrefixes, 'scripts/build-native-runtime.mjs', 'v2/native/common/', 'v2/native/linux/'],
  },
  linux_arm64: {
    exact: new Set(['.github/workflows/linux-arm64-native-ci.yml']),
    prefixes: [...sharedPrefixes, 'scripts/build-native-runtime.mjs', 'v2/native/common/', 'v2/native/linux/'],
  },
  windows_x64: {
    exact: new Set(['.github/workflows/windows-x64-native-ci.yml']),
    prefixes: [...sharedPrefixes, 'scripts/build-native-runtime.mjs', 'scripts/windows/', 'v2/native/common/', 'v2/native/windows/'],
  },
  windows_arm64: {
    exact: new Set(['.github/workflows/windows-arm64-native-ci.yml']),
    prefixes: [...sharedPrefixes, 'scripts/build-native-runtime.mjs', 'scripts/windows/', 'v2/native/common/', 'v2/native/windows/'],
  },
};

export const scopeNames = Object.freeze(Object.keys(scopes));

function normalize(path) {
  return path.replaceAll('\\', '/').replace(/^\.\//, '');
}

export function affectsScope(path, scopeName) {
  const scope = scopes[scopeName];
  if (scope === undefined) {
    throw new Error(`Unknown runtime CI scope: ${scopeName}`);
  }
  const normalized = normalize(path);
  return exactPaths.has(normalized)
    || scope.exact.has(normalized)
    || scope.prefixes.some((prefix) => normalized.startsWith(prefix));
}

export function classifyPaths(paths) {
  const result = Object.fromEntries(scopeNames.map((scopeName) => [scopeName, false]));
  for (const path of paths) {
    for (const scopeName of scopeNames) {
      result[scopeName] ||= affectsScope(path, scopeName);
    }
  }
  return result;
}

function changedPaths(base, head) {
  const output = execFileSync('git', ['diff', '--name-only', base, head], { encoding: 'utf8' });
  return output.split('\n').map((path) => path.trim()).filter(Boolean);
}

function writeGithubOutput(result) {
  const outputPath = process.env.GITHUB_OUTPUT;
  if (outputPath === undefined) {
    throw new Error('GITHUB_OUTPUT is required for --github-output.');
  }
  const output = scopeNames.map((scopeName) => `${scopeName}=${result[scopeName]}`).join('\n');
  appendFileSync(outputPath, `${output}\n`);
}

const [command, ...arguments_] = process.argv.slice(2);
if (command === 'classify') {
  const paths = arguments_.includes('--stdin')
    ? readFileSync(0, 'utf8').split('\n').map((path) => path.trim()).filter(Boolean)
    : arguments_;
  const result = classifyPaths(paths);
  if (arguments_.includes('--github-output')) {
    writeGithubOutput(result);
  } else {
    process.stdout.write(`${JSON.stringify(result)}\n`);
  }
} else if (command === 'changed') {
  const [scopeName, base, head] = arguments_;
  if (scopeName === undefined || base === undefined || head === undefined) {
    throw new Error('Usage: changed <scope> <base-sha> <head-sha>');
  }
  process.stdout.write(`${classifyPaths(changedPaths(base, head))[scopeName]}\n`);
} else if (command !== undefined) {
  throw new Error(`Unknown command: ${command}`);
}
