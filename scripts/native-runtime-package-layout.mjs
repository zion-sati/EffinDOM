import { existsSync, statSync } from 'node:fs';
import { join } from 'node:path';

const outputDirectories = Object.freeze({
  core: ['v2', 'core'],
  ui: ['v2', 'ui'],
  commonHost: ['v2', 'native', 'common'],
});

export function resolveNativeBuildOutput(buildRoot, output, name, platform = '') {
  const directory = output === 'platformHost'
    ? ['v2', 'native', platform]
    : outputDirectories[output];
  if (directory === undefined || (output === 'platformHost' && !['macos', 'windows', 'linux'].includes(platform))) {
    throw new Error(`Unsupported native build output location: ${output}${platform ? ` (${platform})` : ''}.`);
  }

  const path = join(buildRoot, ...directory, name);
  if (!existsSync(path) || !statSync(path).isFile()) {
    throw new Error(`Canonical native build output is missing: ${path}`);
  }
  return path;
}
