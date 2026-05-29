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
  type StaticServerHandle,
} from './test-utils';

test.beforeAll(async () => {
  await setupServer();
});

test.afterAll(async () => {
  await teardownServer();
});

test('focused multiline textbox page keys stay owned by the bridge instead of scrolling the page', async ({ page }) => {
  await gotoBridgePage(page);
  const scene = await buildEditableTextScene(
    page,
    Array.from({ length: 20 }, (_, index) => `Line ${index.toString().padStart(2, '0')}`).join('\n'),
    0,
    { multiline: true, wrapping: false, nodeHeight: 160 },
  );

  await page.evaluate((textHandle) => {
    const runtime = window.EffinDomBrowserBridge?.getRuntime();
    if (runtime === null || runtime === undefined) {
      throw new Error('Bridge runtime is not ready.');
    }
    const filler = document.createElement('div');
    filler.style.height = '2400px';
    document.body.appendChild(filler);
    window.scrollTo(0, 600);
    const bridge = window.EffinDomBrowserBridge;
    if (bridge === undefined) {
      throw new Error('Bridge state is not ready.');
    }
    const handleArg = bridge.handleToBigInt(textHandle);
    runtime.ui._ui_set_interaction_time(BigInt(Math.floor(performance.now())));
    runtime.ui._ui_on_pointer_event(1, handleArg, 1, 12);
    runtime.ui._ui_on_pointer_event(2, handleArg, 1, 12);
    runtime.commitFrame();
    runtime.flushPendingCommit();
  }, scene.textHandle);

  await expect.poll(async () => {
    return await page.evaluate(() => {
      const hiddenTextarea = document.querySelector('textarea[data-effindom-hidden-editor="true"]');
      return hiddenTextarea instanceof HTMLTextAreaElement && document.activeElement === hiddenTextarea;
    });
  }).toBe(true);

  const initialScrollY = await page.evaluate(() => window.scrollY);
  await page.keyboard.press('PageDown');
  await page.keyboard.press('PageDown');

  await expect.poll(async () => {
    return await page.evaluate(() => {
      const hiddenTextarea = document.querySelector('textarea[data-effindom-hidden-editor="true"]');
      return {
        scrollY: window.scrollY,
        hiddenFocused: hiddenTextarea instanceof HTMLTextAreaElement && document.activeElement === hiddenTextarea,
      };
    });
  }).toEqual({
    scrollY: initialScrollY,
    hiddenFocused: true,
  });
});


test('non-text focus does not attach bridge text input state', async ({ page }) => {
  await gotoBridgePage(page);
  const scene = await buildInteractiveBoxScene(page);

  const focusState = await page.evaluate((boxHandle) => {
    const runtime = window.EffinDomBrowserBridge?.getRuntime();
    const canvas = document.getElementById('fui-canvas');
    const hiddenInput = document.querySelector('input[data-effindom-hidden-editor="true"]');
    if (
      runtime === null ||
      runtime === undefined ||
      !(canvas instanceof HTMLCanvasElement) ||
      !(hiddenInput instanceof HTMLInputElement)
    ) {
      throw new Error('Bridge runtime is not ready.');
    }

    const bridge = window.EffinDomBrowserBridge;
    if (bridge === undefined) {
      throw new Error('Bridge state is not ready.');
    }
    const handleArg = bridge.handleToBigInt(boxHandle);
    runtime.ui._ui_set_interaction_time(BigInt(Math.floor(performance.now())));
    runtime.ui._ui_on_pointer_event(1, handleArg, 10, 10);
    runtime.ui._ui_on_pointer_event(2, handleArg, 10, 10);
    runtime.commitFrame();
    runtime.flushPendingCommit();

    return {
      activeTextHandle: runtime.getActiveTextHandle()?.toString() ?? null,
      hiddenInputFocused: document.activeElement === hiddenInput,
      hiddenInputValue: hiddenInput.value,
    };
  }, scene.boxHandle);

  expect(focusState.activeTextHandle).toBeNull();
  expect(focusState.hiddenInputFocused).toBe(false);
  expect(focusState.hiddenInputValue).toBe('');
});


test('blurring an active textbox clears the hidden editor value', async ({ page }) => {
  await gotoBridgePage(page);
  const scene = await buildEditableTextScene(page, '');

  const state = await page.evaluate((textHandle) => {
    const runtime = window.EffinDomBrowserBridge?.getRuntime();
    const canvas = document.getElementById('fui-canvas');
    const hiddenInput = document.querySelector('input[data-effindom-hidden-editor="true"]');
    if (
      runtime === null ||
      runtime === undefined ||
      !(canvas instanceof HTMLCanvasElement) ||
      !(hiddenInput instanceof HTMLInputElement)
    ) {
      throw new Error('Bridge runtime is not ready.');
    }

    const bridge = window.EffinDomBrowserBridge;
    if (bridge === undefined) {
      throw new Error('Bridge state is not ready.');
    }
    const handleArg = bridge.handleToBigInt(textHandle);
    runtime.ui._ui_set_interaction_time(BigInt(Math.floor(performance.now())));
    runtime.ui._ui_on_pointer_event(1, handleArg, 12, 12);
    runtime.ui._ui_on_pointer_event(2, handleArg, 12, 12);
    runtime.commitFrame();
    runtime.flushPendingCommit();

    hiddenInput.value = 'focused text';
    hiddenInput.setSelectionRange(hiddenInput.value.length, hiddenInput.value.length, 'none');
    runtime.ui._ui_request_focus(0n);
    runtime.commitFrame();
    runtime.flushPendingCommit();

    return {
      activeTextHandle: runtime.getActiveTextHandle()?.toString() ?? null,
      hiddenInputFocused: document.activeElement === hiddenInput,
      hiddenInputValue: hiddenInput.value,
    };
  }, scene.textHandle);

  expect(state).toEqual({
    activeTextHandle: null,
    hiddenInputFocused: false,
    hiddenInputValue: '',
  });
});


test('bridge keyboard forwarding prevents browser undo and redo defaults', async ({ page }) => {
  await gotoBridgePage(page);

  const keyState = await page.evaluate(() => {
    const undoEvent = new KeyboardEvent('keydown', { key: 'z', ctrlKey: true, cancelable: true });
    const redoEvent = new KeyboardEvent('keydown', { key: 'y', ctrlKey: true, cancelable: true });
    window.dispatchEvent(undoEvent);
    window.dispatchEvent(redoEvent);
    return {
      undoPrevented: undoEvent.defaultPrevented,
      redoPrevented: redoEvent.defaultPrevented,
    };
  });

  expect(keyState.undoPrevented).toBe(true);
  expect(keyState.redoPrevented).toBe(true);
});

test('textbox batching caps pending typing at five edits and keeps only the latest pending paste', async ({ page }) => {
  await gotoBridgePage(page);
  const scene = await buildEditableTextScene(page, '');

  await page.evaluate((textHandle) => {
    const runtime = window.EffinDomBrowserBridge?.getRuntime();
    const bridge = window.EffinDomBrowserBridge;
    if (runtime === null || runtime === undefined || bridge === undefined) {
      throw new Error('Bridge runtime is not ready.');
    }
    const handleArg = bridge.handleToBigInt(textHandle);
    runtime.ui._ui_set_interaction_time(BigInt(Math.floor(performance.now())));
    runtime.ui._ui_on_pointer_event(1, handleArg, 12, 12);
    runtime.ui._ui_on_pointer_event(2, handleArg, 12, 12);
    runtime.commitFrame();
    runtime.flushPendingCommit();
  }, scene.textHandle);

  await expect.poll(async () => {
    return await page.evaluate(() => {
      const hiddenInput = document.querySelector('input[data-effindom-hidden-editor="true"]');
      return hiddenInput instanceof HTMLInputElement && document.activeElement === hiddenInput;
    });
  }).toBe(true);

  const batchState = await page.evaluate((textHandle) => {
    const runtime = window.EffinDomBrowserBridge?.getRuntime();
    const hiddenInput = document.querySelector('input[data-effindom-hidden-editor="true"]');
    if (runtime === null || runtime === undefined || !(hiddenInput instanceof HTMLInputElement)) {
      throw new Error('Expected bridge runtime and hidden input.');
    }

    const previousReplaceTextRange = runtime.ui._ui_replace_text_range.bind(runtime.ui);
    let replaceAbiCallCount = 0;
    runtime.ui._ui_replace_text_range = (handle, start, end, ptr, len, caret) => {
      replaceAbiCallCount += 1;
      previousReplaceTextRange(handle, start, end, ptr, len, caret);
    };

    const typeText = (value: string): void => {
      hiddenInput.value = value;
      hiddenInput.setSelectionRange(value.length, value.length, 'none');
      hiddenInput.dispatchEvent(new InputEvent('input', {
        bubbles: true,
        inputType: 'insertText',
        data: value.slice(-1),
      }));
    };

    const pasteText = (text: string): void => {
      const selectionStart = hiddenInput.selectionStart ?? hiddenInput.value.length;
      const selectionEnd = hiddenInput.selectionEnd ?? selectionStart;
      hiddenInput.dispatchEvent(new InputEvent('beforeinput', {
        bubbles: true,
        cancelable: true,
        inputType: 'insertFromPaste',
        data: text,
      }));
      const browserValue =
        `${hiddenInput.value.slice(0, selectionStart)}${text}${hiddenInput.value.slice(selectionEnd)}`;
      const caret = selectionStart + text.length;
      hiddenInput.value = browserValue;
      hiddenInput.setSelectionRange(caret, caret, 'none');
      hiddenInput.dispatchEvent(new InputEvent('input', {
        bubbles: true,
        inputType: 'insertFromPaste',
        data: text,
      }));
    };

    try {
      runtime.resetLogs();
      typeText('a');
      typeText('ab');
      typeText('abc');
      typeText('abcd');
      const afterFourth = {
        replaceAbiCallCount,
        hiddenInputValue: hiddenInput.value,
      };

      typeText('abcde');
      const afterFifth = {
        replaceAbiCallCount,
        hiddenInputValue: hiddenInput.value,
      };

      typeText('abcdef');
      const afterSixth = {
        replaceAbiCallCount,
        hiddenInputValue: hiddenInput.value,
        bridgeText: window.__bridgeTextByHandle?.[textHandle] ?? '',
      };

      pasteText('FIRST');
      pasteText('SECOND');
      const beforeFlush = {
        replaceAbiCallCount,
        hiddenInputValue: hiddenInput.value,
        bridgeText: window.__bridgeTextByHandle?.[textHandle] ?? '',
      };

      runtime.flushPendingCommit();

      return {
        afterFourth,
        afterFifth,
        afterSixth,
        beforeFlush,
        afterFlush: {
          replaceAbiCallCount,
          hiddenInputValue: hiddenInput.value,
          bridgeText: window.__bridgeTextByHandle?.[textHandle] ?? '',
        },
      };
    } finally {
      runtime.ui._ui_replace_text_range = previousReplaceTextRange;
    }
  }, scene.textHandle);

  expect(batchState).toEqual({
    afterFourth: {
      replaceAbiCallCount: 0,
      hiddenInputValue: 'abcd',
    },
    afterFifth: {
      replaceAbiCallCount: 0,
      hiddenInputValue: 'abcde',
    },
    afterSixth: {
      replaceAbiCallCount: 0,
      hiddenInputValue: 'abcde',
      bridgeText: 'abcde',
    },
    beforeFlush: {
      replaceAbiCallCount: 1,
      hiddenInputValue: 'abcdeSECOND',
      bridgeText: 'abcdeSECOND',
    },
    afterFlush: {
      replaceAbiCallCount: 2,
      hiddenInputValue: 'abcdeSECOND',
      bridgeText: 'abcdeSECOND',
    },
  });
});
