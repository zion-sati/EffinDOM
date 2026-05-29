import * as fs from 'node:fs';
import * as path from 'node:path';

import { expect, test, type Page } from '@playwright/test';

import { startStaticServer } from './helpers/static_server';

declare global {
  interface Window {
    __effindomV2Ready?: boolean;
    __effindomV2LastHit?: number;
  }
}

const PUBLIC_DIR = path.join(__dirname, '..', '..', '..', '..', 'public');
const SCREENSHOT_DIR = path.join(__dirname, 'screenshots');
const EXPECTED_HIT_HANDLE = 0x1_0000_0002;

interface RenderedPixel {
  readonly red: number;
  readonly green: number;
  readonly blue: number;
  readonly alpha: number;
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

test('real core wasm boots, paints pixels, routes pointer hits, and stores a screenshot', async ({ page }, testInfo) => {
  const server = await startStaticServer(PUBLIC_DIR, 10_900 + testInfo.workerIndex);
  fs.mkdirSync(SCREENSHOT_DIR, { recursive: true });

  try {
    await page.goto(`http://127.0.0.1:${String(server.port)}/v2/core/index.html`);
    await page.waitForFunction(() => window.__effindomV2Ready === true);
    await expect.poll(async () => {
      const pixel = await readScenePixel(page, 160, 110);
      return pixel.red + pixel.green + pixel.blue;
    }).toBeGreaterThan(100);
    await expect.poll(async () => {
      const pixel = await readScenePixel(page, 160, 110);
      return pixel.alpha;
    }).toBeGreaterThan(200);
    const renderedPixel = await readScenePixel(page, 160, 110);

    expect(renderedPixel.red + renderedPixel.green + renderedPixel.blue).toBeGreaterThan(100);
    expect(renderedPixel.alpha).toBeGreaterThan(200);

    const screenshotPath = path.join(SCREENSHOT_DIR, 'chromium-core-smoke.png');
    await page.locator('#fui-canvas').screenshot({ path: screenshotPath });
    expect(fs.existsSync(screenshotPath)).toBe(true);

    const box = await page.locator('#fui-canvas').boundingBox();
    if (box === null) {
      throw new Error('Expected #fui-canvas to have a bounding box.');
    }

    await page.mouse.click(box.x + 200, box.y + 120);

    await expect
      .poll(() => page.evaluate(() => window.__effindomV2LastHit ?? null))
      .toBe(EXPECTED_HIT_HANDLE);
  } finally {
    await server.close();
  }
});
