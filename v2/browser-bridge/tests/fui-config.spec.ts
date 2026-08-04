import { expect, test } from '@playwright/test';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import {
  FUI_CONFIG_SCHEMA_VERSION,
  createFuiConfigBootstrapScript,
  parseFuiConfig,
} from '../src/fui-config';

test('parses the narrow versioned FUI configuration contract', () => {
  const config = parseFuiConfig({
    $schema: 'https://effindom.dev/schemas/fui-config.schema.json',
    version: 1,
    application: { pageZoom: 'disabled' },
    web: {
      loading: { delayMs: 125, minimumVisibleMs: 250 },
      devTools: { domMirror: 'on-requested' },
    },
  });
  expect(config.version).toBe(FUI_CONFIG_SCHEMA_VERSION);
  expect(config.application?.pageZoom).toBe('disabled');
  expect(config.web?.loading).toEqual({ delayMs: 125, minimumVisibleMs: 250 });
  expect(config.web?.devTools?.domMirror).toBe('on-requested');
  expect(createFuiConfigBootstrapScript(config)).toContain('window.__effindomFuiConfig');
});

test('rejects unsupported settings and malformed values', () => {
  for (const value of [
    { version: 2 },
    { version: 1, secrets: {} },
    { version: 1, application: { pageZom: 'enabled' } },
    { version: 1, application: { pageZoom: 'automatic' } },
    { version: 1, web: { loading: { delayMs: -1 } } },
    { version: 1, web: { devTools: { domMirror: 'sometimes' } } },
    { version: 1, web: { routes: [] } },
  ]) {
    expect(() => parseFuiConfig(value)).toThrow();
  }
});

test('matches the shared cross-language conformance corpus', () => {
  const fixtures = JSON.parse(
    readFileSync(resolve(__dirname, '../../../schemas/fui-config-fixtures.json'), 'utf8'),
  ) as {
    valid: { name: string; config: unknown }[];
    invalid: { name: string; config: unknown }[];
  };
  for (const fixture of fixtures.valid) {
    expect(() => parseFuiConfig(fixture.config), fixture.name).not.toThrow();
  }
  for (const fixture of fixtures.invalid) {
    expect(() => parseFuiConfig(fixture.config), fixture.name).toThrow();
  }
});

test('published runtime schema is synchronized with the language-neutral source', () => {
  const canonical = JSON.parse(readFileSync(resolve(__dirname, '../../../schemas/fui-config.schema.json'), 'utf8')) as unknown;
  const packaged = JSON.parse(readFileSync(resolve(__dirname, '../src/fui-config.schema.json'), 'utf8')) as unknown;
  expect(packaged).toEqual(canonical);
});
