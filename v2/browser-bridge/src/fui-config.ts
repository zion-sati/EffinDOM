export const FUI_CONFIG_SCHEMA_VERSION = 1;

export const FUI_CONFIG_SCHEMA_URL = 'https://effindom.dev/schemas/fui-config.schema.json';

export type FuiPageZoomMode = 'disabled' | 'enabled';
export type FuiDevToolsDomMirrorMode = 'disabled' | 'enabled' | 'on-requested';

export interface FuiApplicationConfig {
  readonly pageZoom?: FuiPageZoomMode;
}

export interface FuiWebLoadingConfig {
  readonly delayMs?: number;
  readonly minimumVisibleMs?: number;
}

export interface FuiWebDevToolsConfig {
  readonly domMirror?: FuiDevToolsDomMirrorMode;
}

export interface FuiWebConfig {
  readonly loading?: FuiWebLoadingConfig;
  readonly devTools?: FuiWebDevToolsConfig;
}

export interface FuiConfig {
  readonly $schema?: string;
  readonly version: typeof FUI_CONFIG_SCHEMA_VERSION;
  readonly application?: FuiApplicationConfig;
  readonly web?: FuiWebConfig;
}

declare global {
  interface Window {
    __effindomFuiConfig?: FuiConfig;
  }
}

function record(value: unknown, path: string): Record<string, unknown> {
  if (typeof value !== 'object' || value === null || Array.isArray(value)) {
    throw new Error(`${path} must be an object.`);
  }
  return value as Record<string, unknown>;
}

function rejectUnknown(value: Record<string, unknown>, allowed: readonly string[], path: string): void {
  const allowedKeys = new Set(allowed);
  for (const key of Object.keys(value)) {
    if (!allowedKeys.has(key)) {
      throw new Error(`${path}.${key} is not a supported FUI setting.`);
    }
  }
}

function optionalDuration(value: unknown, path: string): number | undefined {
  if (value === undefined) return undefined;
  if (typeof value !== 'number' || !Number.isFinite(value) || value < 0) {
    throw new Error(`${path} must be a finite non-negative number.`);
  }
  return value;
}

function optionalEnum<T extends string>(
  value: unknown,
  allowed: readonly T[],
  path: string,
): T | undefined {
  if (value === undefined) return undefined;
  if (typeof value !== 'string' || !allowed.includes(value as T)) {
    throw new Error(`${path} must be one of: ${allowed.join(', ')}.`);
  }
  return value as T;
}

export function parseFuiConfig(value: unknown): FuiConfig {
  const root = record(value, 'fui-config.json');
  rejectUnknown(root, ['$schema', 'version', 'application', 'web'], 'fui-config.json');
  if (root.version !== FUI_CONFIG_SCHEMA_VERSION) {
    throw new Error(`fui-config.json.version must be ${String(FUI_CONFIG_SCHEMA_VERSION)}.`);
  }
  if (root.$schema !== undefined && typeof root.$schema !== 'string') {
    throw new Error('fui-config.json.$schema must be a string.');
  }

  let application: FuiApplicationConfig | undefined;
  if (root.application !== undefined) {
    const source = record(root.application, 'fui-config.json.application');
    rejectUnknown(source, ['pageZoom'], 'fui-config.json.application');
    const pageZoom = optionalEnum(source.pageZoom, ['disabled', 'enabled'], 'fui-config.json.application.pageZoom');
    application = pageZoom === undefined ? {} : { pageZoom };
  }

  let web: FuiWebConfig | undefined;
  if (root.web !== undefined) {
    const source = record(root.web, 'fui-config.json.web');
    rejectUnknown(source, ['loading', 'devTools'], 'fui-config.json.web');
    let loading: FuiWebLoadingConfig | undefined;
    if (source.loading !== undefined) {
      const loadingSource = record(source.loading, 'fui-config.json.web.loading');
      rejectUnknown(loadingSource, ['delayMs', 'minimumVisibleMs'], 'fui-config.json.web.loading');
      const delayMs = optionalDuration(loadingSource.delayMs, 'fui-config.json.web.loading.delayMs');
      const minimumVisibleMs = optionalDuration(
        loadingSource.minimumVisibleMs,
        'fui-config.json.web.loading.minimumVisibleMs',
      );
      loading = {
        ...(delayMs === undefined ? {} : { delayMs }),
        ...(minimumVisibleMs === undefined ? {} : { minimumVisibleMs }),
      };
    }
    let devTools: FuiWebDevToolsConfig | undefined;
    if (source.devTools !== undefined) {
      const devToolsSource = record(source.devTools, 'fui-config.json.web.devTools');
      rejectUnknown(devToolsSource, ['domMirror'], 'fui-config.json.web.devTools');
      const domMirror = optionalEnum(
        devToolsSource.domMirror,
        ['disabled', 'enabled', 'on-requested'],
        'fui-config.json.web.devTools.domMirror',
      );
      devTools = domMirror === undefined ? {} : { domMirror };
    }
    web = {
      ...(loading === undefined ? {} : { loading }),
      ...(devTools === undefined ? {} : { devTools }),
    };
  }

  return {
    ...(root.$schema === undefined ? {} : { $schema: root.$schema }),
    version: FUI_CONFIG_SCHEMA_VERSION,
    ...(application === undefined ? {} : { application }),
    ...(web === undefined ? {} : { web }),
  };
}

export function createFuiConfigBootstrapScript(config: FuiConfig): string {
  return `window.__effindomFuiConfig = ${JSON.stringify(config)};\n`;
}
