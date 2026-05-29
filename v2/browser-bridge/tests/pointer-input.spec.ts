import { expect, test, type Page } from '@playwright/test';

import {
  setupServer,
  teardownServer,
  type EditableSceneState,
  type EditableSceneOptions,
  type SelectionAreaSceneState,
  type BoxSceneState,
  type ScrollSceneState,
  type NestedProxyScrollSceneState,
  type SemanticSceneState,
  PUBLIC_DIR,
  SCREENSHOT_DIR,
  screenshotPath,
  readWrappedTextFixture,
  getWrappedTextFixtureTargets,
  readScenePixel,
  decodeFloat32,
  parseGlyphRuns,
  parseHighlightRects,
  getWrappedTextIndexPoint,
  getWrappedSelectionDragPoints,
  gotoBridgePage,
  readActiveRenderer,
  buildEditableTextScene,
  buildReadonlyTextScene,
  buildScrollableEditableTextScene,
  buildSelectionAreaScene,
  buildSemanticScene,
  buildClippedSemanticScene,
  buildInteractiveBoxScene,
  buildScrollScene,
  buildNestedProxyScrollScene,
  buildWrappedThaiScene,
  buildColorEmojiScene,
  readCanvasInkStats,
  readCanvasColorStats,
  waitForCanvasInk,
  CMD_COMMIT_PAINT_ORDER,
  type StaticServerHandle,
} from './test-utils';

test.beforeAll(async () => {
  await setupServer();
});

test.afterAll(async () => {
  await teardownServer();
});

test('pointer events use Core hit-testing and surface the real handle back through callbacks', async ({ page }) => {
  await gotoBridgePage(page);
  const scene = await buildInteractiveBoxScene(page);

  const result = await page.evaluate(() => {
    const runtime = window.EffinDomBrowserBridge?.getRuntime();
    if (runtime === null || runtime === undefined) {
      throw new Error('Bridge runtime is not ready.');
    }
    const hitHandle = runtime.getHandleFromPoint(40, 40);
    runtime.ui._ui_set_interaction_time(BigInt(Math.floor(performance.now())));
    runtime.ui._ui_on_pointer_event(1, hitHandle, 40, 40);
    runtime.ui._ui_on_pointer_event(2, hitHandle, 40, 40);
    runtime.commitFrame();
    runtime.flushPendingCommit();
    return {
      hitHandle: hitHandle.toString(),
      pointerEvents: window.__bridgeLogs?.pointerEvents ?? [],
    };
  });

  expect(result.hitHandle).toBe(scene.boxHandle);
  expect(result.pointerEvents.some((entry) => entry.handle === scene.boxHandle)).toBe(true);
});


test('bridge preserves earlier pending UI commits when a later commit overwrites the buffer', async ({ page }) => {
  await gotoBridgePage(page);

  const result = await page.evaluate(async () => {
    const runtime = window.EffinDomBrowserBridge?.getRuntime();
    if (runtime === null || runtime === undefined) {
      throw new Error('Bridge runtime is not ready.');
    }

    const ui = runtime.ui;
    const bridge = window.EffinDomBrowserBridge;
    if (bridge === undefined) {
      throw new Error('Bridge state is not ready.');
    }
    const toHandle = (handle: unknown): string =>
      bridge.handleToString(handle as string | number | bigint | { valueOf(): unknown; toString(): string });

    runtime.resetLogs();
    runtime.resetAppSession();
    ui._ui_reset();

    const root = toHandle(ui._ui_create_node(0));
    const box = toHandle(ui._ui_create_node(0));
    ui._ui_set_root(root);
    ui._ui_node_add_child(root, box);
    ui._ui_set_width(root, 320, 0);
    ui._ui_set_height(root, 220, 0);
    ui._ui_set_bg_color(root, 0x111827ff);
    ui._ui_set_width(box, 120, 0);
    ui._ui_set_height(box, 120, 0);
    ui._ui_set_bg_color(box, 0x2563ebff);
    ui._ui_set_interactive(box, 1);

    runtime.commitFrame();
    runtime.commitFrame();
    await new Promise<void>((resolve) => {
      requestAnimationFrame(() => {
        requestAnimationFrame(() => {
          resolve();
        });
      });
    });

    return {
      boxHandle: box,
      hitHandle: runtime.getHandleFromPoint(40, 40).toString(),
      latestCommandWords: Array.from(runtime.extractCommandBuffer()),
    };
  });

  expect(result.latestCommandWords[0]).toBe(CMD_COMMIT_PAINT_ORDER);
  expect(result.hitHandle).toBe(result.boxHandle);
});


test('wheel scrolling hands proxy-owned nested scroll input off to the outer scrollview at every edge', async ({ page }) => {
  await gotoBridgePage(page);
  const scene = await buildNestedProxyScrollScene(page);

  const result = await page.evaluate((handles) => {
    const runtime = window.EffinDomBrowserBridge?.getRuntime();
    if (runtime === null || runtime === undefined) {
      throw new Error('Bridge runtime is not ready.');
    }
    const canvas = document.getElementById('fui-canvas');
    if (!(canvas instanceof HTMLCanvasElement)) {
      throw new Error('Expected scene canvas.');
    }

    const ui = runtime.ui;
    const rect = canvas.getBoundingClientRect();
    const resetOffsets = (outerX: number, outerY: number, innerX: number, innerY: number): void => {
      ui._ui_set_scroll_offset(handles.outerScrollHandle, outerX, outerY);
      ui._ui_set_scroll_offset(handles.innerScrollHandle, innerX, innerY);
      runtime.commitFrame();
      runtime.flushPendingCommit();
      runtime.resetLogs();
    };
    const dispatchWheel = (deltaX: number, deltaY: number): boolean => {
      const event = new WheelEvent('wheel', {
        bubbles: true,
        cancelable: true,
        clientX: rect.left + 40,
        clientY: rect.top + 30,
        deltaX,
        deltaY,
      });
      canvas.dispatchEvent(event);
      return event.defaultPrevented;
    };
    const snapshot = () => {
      return (window.__bridgeLogs?.scrollEvents ?? []).map((entry) => ({
        handle: entry.handle,
        offsetX: entry.offsetX,
        offsetY: entry.offsetY,
      }));
    };

    resetOffsets(0, 20, 0, 0);
    const topPrevented = dispatchWheel(0, -24);
    const topLogs = snapshot();

    resetOffsets(0, 20, 0, 140);
    const bottomPrevented = dispatchWheel(0, 24);
    const bottomLogs = snapshot();

    resetOffsets(20, 0, 0, 0);
    const leftPrevented = dispatchWheel(-24, 0);
    const leftLogs = snapshot();

    resetOffsets(20, 0, 140, 0);
    const rightPrevented = dispatchWheel(24, 0);
    const rightLogs = snapshot();

    return {
      topPrevented,
      topLogs,
      bottomPrevented,
      bottomLogs,
      leftPrevented,
      leftLogs,
      rightPrevented,
      rightLogs,
    };
  }, scene);

  expect(result.topPrevented).toBe(true);
  expect(result.bottomPrevented).toBe(true);
  expect(result.leftPrevented).toBe(true);
  expect(result.rightPrevented).toBe(true);

  expect(result.topLogs.some((entry) => entry.handle === scene.outerScrollHandle && entry.offsetY < 20)).toBe(true);
  expect(result.topLogs.some((entry) => entry.handle === scene.innerScrollHandle)).toBe(false);

  expect(result.bottomLogs.some((entry) => entry.handle === scene.outerScrollHandle && entry.offsetY > 20)).toBe(true);
  expect(result.bottomLogs.some((entry) => entry.handle === scene.innerScrollHandle)).toBe(false);

  expect(result.leftLogs.some((entry) => entry.handle === scene.outerScrollHandle && entry.offsetX < 20)).toBe(true);
  expect(result.leftLogs.some((entry) => entry.handle === scene.innerScrollHandle)).toBe(false);

  expect(result.rightLogs.some((entry) => entry.handle === scene.outerScrollHandle && entry.offsetX > 20)).toBe(true);
  expect(result.rightLogs.some((entry) => entry.handle === scene.innerScrollHandle)).toBe(false);
});


test('touch scrolling hands proxy-owned nested scroll input off to the outer scrollview at every edge', async ({ page }) => {
  await gotoBridgePage(page);
  const scene = await buildNestedProxyScrollScene(page);

  const result = await page.evaluate((handles) => {
    const runtime = window.EffinDomBrowserBridge?.getRuntime();
    if (runtime === null || runtime === undefined) {
      throw new Error('Bridge runtime is not ready.');
    }
    const canvas = document.getElementById('fui-canvas');
    if (!(canvas instanceof HTMLCanvasElement)) {
      throw new Error('Expected scene canvas.');
    }

    const ui = runtime.ui;
    const rect = canvas.getBoundingClientRect();
    const resetOffsets = (outerX: number, outerY: number, innerX: number, innerY: number): void => {
      ui._ui_set_scroll_offset(handles.outerScrollHandle, outerX, outerY);
      ui._ui_set_scroll_offset(handles.innerScrollHandle, innerX, innerY);
      runtime.commitFrame();
      runtime.flushPendingCommit();
      runtime.resetLogs();
    };
    const dispatchTouchPath = (points: { x: number; y: number }[], pointerId: number): void => {
      const emit = (type: string, point: { x: number; y: number }): void => {
        canvas.dispatchEvent(new PointerEvent(type, {
          bubbles: true,
          cancelable: true,
          pointerId,
          pointerType: 'touch',
          isPrimary: true,
          button: 0,
          buttons: type === 'pointerup' ? 0 : 1,
          clientX: rect.left + point.x,
          clientY: rect.top + point.y,
        }));
      };

      emit('pointerdown', points[0]!);
      for (let index = 1; index < points.length; index += 1) {
        emit('pointermove', points[index]!);
      }
      emit('pointerup', points[points.length - 1]!);
    };
    const snapshot = () => {
      return (window.__bridgeLogs?.scrollEvents ?? []).map((entry) => ({
        handle: entry.handle,
        offsetX: entry.offsetX,
        offsetY: entry.offsetY,
      }));
    };

    resetOffsets(0, 20, 0, 0);
    dispatchTouchPath([{ x: 40, y: 30 }, { x: 40, y: 54 }, { x: 40, y: 78 }], 21);
    const topLogs = snapshot();

    resetOffsets(0, 20, 0, 140);
    dispatchTouchPath([{ x: 40, y: 78 }, { x: 40, y: 54 }, { x: 40, y: 30 }], 22);
    const bottomLogs = snapshot();

    resetOffsets(20, 0, 0, 0);
    dispatchTouchPath([{ x: 40, y: 30 }, { x: 64, y: 30 }, { x: 88, y: 30 }], 23);
    const leftLogs = snapshot();

    resetOffsets(20, 0, 140, 0);
    dispatchTouchPath([{ x: 88, y: 30 }, { x: 64, y: 30 }, { x: 40, y: 30 }], 24);
    const rightLogs = snapshot();

    return { topLogs, bottomLogs, leftLogs, rightLogs };
  }, scene);

  expect(result.topLogs.some((entry) => entry.handle === scene.outerScrollHandle && entry.offsetY < 20)).toBe(true);
  expect(result.topLogs.some((entry) => entry.handle === scene.innerScrollHandle)).toBe(false);

  expect(result.bottomLogs.some((entry) => entry.handle === scene.outerScrollHandle && entry.offsetY > 20)).toBe(true);
  expect(result.bottomLogs.some((entry) => entry.handle === scene.innerScrollHandle)).toBe(false);

  expect(result.leftLogs.some((entry) => entry.handle === scene.outerScrollHandle && entry.offsetX < 20)).toBe(true);
  expect(result.leftLogs.some((entry) => entry.handle === scene.innerScrollHandle)).toBe(false);

  expect(result.rightLogs.some((entry) => entry.handle === scene.outerScrollHandle && entry.offsetX > 20)).toBe(true);
  expect(result.rightLogs.some((entry) => entry.handle === scene.innerScrollHandle)).toBe(false);
});
