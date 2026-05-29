export const EFFINDOM_RUNTIME_DIST_DIR = 'dist';
export const EFFINDOM_RUNTIME_MANIFEST_FILE = 'effindom.v2.manifest.json';
export const EFFINDOM_RUNTIME_BRIDGE_SCRIPT = 'bridge.js';
export const EFFINDOM_RUNTIME_HARNESS_SCRIPT = 'harness.js';
export const EFFINDOM_RUNTIME_ARTIFACT_DIR = 'runtime';
export const EFFINDOM_RUNTIME_FONTS_DIR = 'fonts';

export interface EffinDomRuntimeConfig {
  readonly manifestUrl: string;
}

export interface EffinDomRuntimeAssetUrls {
  readonly packageBaseUrl: string;
  readonly distBaseUrl: string;
  readonly manifestUrl: string;
  readonly bridgeScriptUrl: string;
  readonly harnessScriptUrl: string;
  readonly runtimeBaseUrl: string;
  readonly fontsBaseUrl: string;
}

interface RuntimeWindowLike {
  __effindomRuntime?: Partial<EffinDomRuntimeConfig>;
}

function ensureTrailingSlash(url: string): string {
  return url.endsWith('/') ? url : `${url}/`;
}

function resolveFromBase(
  value: string | URL,
  base?: string | URL,
): URL {
  if (value instanceof URL) {
    return new URL(value.toString());
  }
  if (base !== undefined) {
    return new URL(value, base);
  }
  if (typeof document !== 'undefined') {
    return new URL(value, document.baseURI);
  }
  return new URL(value);
}

export function resolveRuntimeAssetUrls(
  packageBaseUrl: string | URL,
  relativeTo?: string | URL,
): EffinDomRuntimeAssetUrls {
  const packageBase = resolveFromBase(packageBaseUrl, relativeTo);
  packageBase.pathname = ensureTrailingSlash(packageBase.pathname);
  const distBase = new URL(`${EFFINDOM_RUNTIME_DIST_DIR}/`, packageBase);
  const runtimeBase = new URL(`${EFFINDOM_RUNTIME_ARTIFACT_DIR}/`, distBase);
  const fontsBase = new URL(`${EFFINDOM_RUNTIME_FONTS_DIR}/`, distBase);

  return {
    packageBaseUrl: packageBase.toString(),
    distBaseUrl: distBase.toString(),
    manifestUrl: new URL(EFFINDOM_RUNTIME_MANIFEST_FILE, distBase).toString(),
    bridgeScriptUrl: new URL(EFFINDOM_RUNTIME_BRIDGE_SCRIPT, distBase).toString(),
    harnessScriptUrl: new URL(EFFINDOM_RUNTIME_HARNESS_SCRIPT, distBase).toString(),
    runtimeBaseUrl: runtimeBase.toString(),
    fontsBaseUrl: fontsBase.toString(),
  };
}

export function createRuntimeConfig(
  packageBaseUrl: string | URL,
  relativeTo?: string | URL,
): EffinDomRuntimeConfig {
  const urls = resolveRuntimeAssetUrls(packageBaseUrl, relativeTo);
  return {
    manifestUrl: urls.manifestUrl,
  };
}

export function applyRuntimeConfig(
  config: EffinDomRuntimeConfig,
  target?: RuntimeWindowLike,
): EffinDomRuntimeConfig {
  const destination = target ?? (typeof window !== 'undefined' ? (window as RuntimeWindowLike) : undefined);
  if (destination === undefined) {
    throw new Error('applyRuntimeConfig requires a browser window-like target outside browser contexts.');
  }
  destination.__effindomRuntime = Object.assign({}, destination.__effindomRuntime, config);
  return {
    manifestUrl: destination.__effindomRuntime.manifestUrl ?? config.manifestUrl,
  };
}

export function createRuntimeConfigScript(
  config: EffinDomRuntimeConfig,
): string {
  const manifest = JSON.stringify(config.manifestUrl);
  return `window.__effindomRuntime = Object.assign({}, window.__effindomRuntime, {\n  manifestUrl: ${manifest},\n});\n`;
}
