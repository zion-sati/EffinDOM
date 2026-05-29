import * as fs from 'node:fs';
import * as path from 'node:path';

import { expect, test, type Page } from '@playwright/test';

import { startStaticServer } from './helpers/static_server';

declare global {
  interface Window {
    __effindomV2UiReady?: boolean;
    __effindomV2UiError?: string;
    __effindomV2UiState?: {
      readonly commandWordCount: number;
      readonly commandWords: readonly number[];
      readonly rootHandle: string;
      readonly childHandle: string;
    };
  }
}

interface RenderedPixel {
  readonly red: number;
  readonly green: number;
  readonly blue: number;
  readonly alpha: number;
}

interface BoundsSnapshot {
  readonly x: number;
  readonly y: number;
  readonly width: number;
  readonly height: number;
}

function decodeFloat32(word: number): number {
  const buffer = new ArrayBuffer(4);
  const view = new DataView(buffer);
  view.setUint32(0, word >>> 0, true);
  return view.getFloat32(0, true);
}

function parseBoundsAt(words: readonly number[], xIndex: number): Map<bigint, BoundsSnapshot> {
  const bounds = new Map<bigint, BoundsSnapshot>();

  for (let index = 0; index < words.length;) {
    const opcode = words[index];
    if (opcode === CMD_SET_BOUNDS) {
      const low = BigInt(words[index + 1] ?? 0);
      const high = BigInt(words[index + 2] ?? 0);
      bounds.set((high << 32n) | low, {
        x: decodeFloat32(words[index + xIndex] ?? 0),
        y: decodeFloat32(words[index + xIndex + 1] ?? 0),
        width: decodeFloat32(words[index + xIndex + 2] ?? 0),
        height: decodeFloat32(words[index + xIndex + 3] ?? 0),
      });
      index += 16;
      continue;
    }
    if (opcode === CMD_CREATE_NODE || opcode === CMD_DELETE_NODE) {
      index += 3;
      continue;
    }
    if (opcode === CMD_SET_BOX_STYLE) {
      index += 13;
      continue;
    }
    if (opcode === CMD_COMMIT_PAINT_ORDER) {
      index += 2 + ((words[index + 1] ?? 0) * 2);
      continue;
    }
    if (opcode === CMD_COMMIT_SCENE) {
      index += 2 + ((words[index + 1] ?? 0) * 5);
      continue;
    }
    break;
  }

  return bounds;
}

async function readScenePixel(page: Page, x: number, y: number): Promise<RenderedPixel> {
  return await page.evaluate(async ({ sampleX, sampleY }) => {
    const overlay = document.querySelector('[data-effindom-software-overlay="true"]');
    const canvas = overlay instanceof HTMLCanvasElement ? overlay : document.getElementById('fui-canvas');
    if (!(canvas instanceof HTMLCanvasElement)) {
      throw new Error('Expected scene canvas.');
    }
    const image = new Image();
    const loaded = new Promise<void>((resolve, reject) => {
      image.addEventListener('load', () => {
        resolve();
      }, { once: true });
      image.addEventListener('error', () => {
        reject(new Error('Failed to decode scene image.'));
      }, { once: true });
    });
    image.src = canvas.toDataURL();
    await loaded;
    const probe = document.createElement('canvas');
    probe.width = canvas.width;
    probe.height = canvas.height;
    const context = probe.getContext('2d');
    if (context === null) {
      throw new Error('Expected 2D probe context.');
    }
    context.drawImage(image, 0, 0);
    const clampedX = Math.max(0, Math.min(probe.width - 1, Math.round(sampleX)));
    const clampedY = Math.max(0, Math.min(probe.height - 1, Math.round(sampleY)));
    const pixel = context.getImageData(clampedX, clampedY, 1, 1).data;
    return {
      red: pixel[0] ?? 0,
      green: pixel[1] ?? 0,
      blue: pixel[2] ?? 0,
      alpha: pixel[3] ?? 0,
    };
  }, { sampleX: x, sampleY: y });
}

const PUBLIC_DIR = path.join(__dirname, '..', '..', '..', '..', 'public');
const SCREENSHOT_DIR = path.join(__dirname, 'screenshots');

test('v2 ui bridge builds a rooted tree, emits Core commands, and renders a red rectangle', async ({ page }, testInfo) => {
  const server = await startStaticServer(PUBLIC_DIR, 11_100 + testInfo.workerIndex);
  fs.mkdirSync(SCREENSHOT_DIR, { recursive: true });

  try {
    await page.goto(`http://127.0.0.1:${String(server.port)}/v2/ui/index.html`);

    await page.waitForFunction(
      () => window.__effindomV2UiReady === true || typeof window.__effindomV2UiError === 'string',
    );

    const bridgeState = await page.evaluate(() => ({
      ready: window.__effindomV2UiReady === true,
      error: window.__effindomV2UiError ?? null,
      state: window.__effindomV2UiState ?? null,
    }));

    expect(bridgeState.error).toBeNull();
    expect(bridgeState.ready).toBe(true);
    expect(BigInt(bridgeState.state?.rootHandle ?? '0')).toBeGreaterThan(0n);
    expect(BigInt(bridgeState.state?.childHandle ?? '0')).toBeGreaterThan(0n);
    expect(bridgeState.state?.commandWordCount).toBeGreaterThan(0);
    expect(bridgeState.state?.commandWords).toContain(CMD_CREATE_NODE);
    expect(bridgeState.state?.commandWords).toContain(CMD_SET_BOUNDS);
    expect(bridgeState.state?.commandWords).toContain(CMD_SET_BOX_STYLE);
    expect(bridgeState.state?.commandWords).toContain(CMD_COMMIT_PAINT_ORDER);
    expect(bridgeState.state?.commandWords).toContain(CMD_COMMIT_SCENE);

    await expect.poll(async () => (await readScenePixel(page, 50, 50)).red).toBeGreaterThan(220);
    const renderedPixel = await readScenePixel(page, 50, 50);

    expect(renderedPixel.red).toBeGreaterThan(220);
    expect(renderedPixel.green).toBeLessThan(40);
    expect(renderedPixel.blue).toBeLessThan(40);
    expect(renderedPixel.alpha).toBeGreaterThan(220);

    const screenshotPath = path.join(SCREENSHOT_DIR, 'chromium-ui-bridge-smoke.png');
    await page.locator('#fui-canvas').screenshot({ path: screenshotPath });
    expect(fs.existsSync(screenshotPath)).toBe(true);
  } finally {
    await server.close();
  }
});

test('v2 ui bridge clips oversized child visuals and hit bounds to the padded client area', async ({ page }, testInfo) => {
  const server = await startStaticServer(PUBLIC_DIR, 11_200 + testInfo.workerIndex);

  try {
    await page.goto(`http://127.0.0.1:${String(server.port)}/v2/ui/index.html`);

    await page.waitForFunction(
      () => window.__effindomV2UiReady === true || typeof window.__effindomV2UiError === 'string',
    );

    const bridgeState = await page.evaluate(() => ({
      ready: window.__effindomV2UiReady === true,
      error: window.__effindomV2UiError ?? null,
      state: window.__effindomV2UiState ?? null,
    }));

    expect(bridgeState.error).toBeNull();
    expect(bridgeState.ready).toBe(true);

    const rootHandle = BigInt(bridgeState.state?.rootHandle ?? '0');
    const childHandle = BigInt(bridgeState.state?.childHandle ?? '0');
    const hitBounds = parseBoundsAt(bridgeState.state?.commandWords ?? [], 7);
    const clipBounds = parseBoundsAt(bridgeState.state?.commandWords ?? [], 11);

    expect(clipBounds.get(rootHandle)).toEqual({
      x: 12,
      y: 8,
      width: 152,
      height: 122,
    });
    expect(hitBounds.get(childHandle)).toEqual({
      x: 12,
      y: 8,
      width: 152,
      height: 60,
    });

    const topLeftBackground = await readScenePixel(page, 4, 4);
    const rightPaddingPixel = await readScenePixel(page, 172, 40);

    expect(rightPaddingPixel).toEqual(topLeftBackground);
  } finally {
    await server.close();
  }
});

const CMD_CREATE_NODE = 1;
const CMD_DELETE_NODE = 2;
const CMD_SET_BOUNDS = 10;
const CMD_SET_BOX_STYLE = 20;
const CMD_COMMIT_PAINT_ORDER = 98;
const CMD_COMMIT_SCENE = 99;
