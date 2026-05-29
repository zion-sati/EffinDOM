import type {
  BridgeFontRegistration,
  BridgeFontStackRegistration,
  BridgeLoaderInfo,
  BridgeRuntime,
  CoreModule,
  UiModule,
} from '../core-types';
import type { BridgeInteractionState } from './local-types';
import { resetBridgeLogs } from './utils/assets';
import { normalizeBackendType, handleToBigInt } from './utils/encoding';
import { executeCommandBuffer, extractCommandBuffer } from './utils/heap';
import { setActiveRenderer } from './utils/backends';
import { getCanvasSizeSource, readCanvasLogicalSize } from './events';
import { AssetManager } from './runtime/asset-manager';
import { IncrementalFontManager } from './runtime/font-manager';
import { FindController } from './runtime/find-controller';
import { OpenCanvasApiAdapter } from './runtime/open-canvas-api';
import { SemanticController } from './runtime/semantic-controller';
import { TextDocumentController } from './runtime/text-documents';

const DEFAULT_LOGICAL_WIDTH = 320;
const DEFAULT_LOGICAL_HEIGHT = 220;
const UI_EVENT_POINTER_ENTER = 4;
const UI_EVENT_POINTER_LEAVE = 5;

export function createBridgeRuntime(
  core: CoreModule,
  ui: UiModule,
  canvas: HTMLCanvasElement,
  interactionState: BridgeInteractionState,
  loaderInfo: BridgeLoaderInfo,
): { runtime: BridgeRuntime; destroy(): void } {
  let logicalWidth = DEFAULT_LOGICAL_WIDTH;
  let logicalHeight = DEFAULT_LOGICAL_HEIGHT;
  let needsCommit = false;
  let appFrameHandler: ((timestampMs: number) => void) | null = null;
  let frameRequester: (() => void) | null = null;
  let runtime!: BridgeRuntime;

  const fontManager = new IncrementalFontManager(core, ui, interactionState.logs, () => runtime.commitFrame());
  const assetManager = new AssetManager(core, fontManager, () => runtime.commitFrame());
  const textDocuments = new TextDocumentController(ui);
  const semanticController = new SemanticController(canvas, ui, interactionState, textDocuments);
  const findController = new FindController({
    canvas,
    ui,
    textDocuments,
    commitFrame: () => runtime.commitFrame(),
    flushPendingCommit: () => runtime.flushPendingCommit(),
  });
  const openCanvasApiAdapter = new OpenCanvasApiAdapter({
    ui,
    semantic: semanticController,
    find: findController,
    textDocuments,
    commitFrame: () => runtime.commitFrame(),
    flushPendingCommit: () => runtime.flushPendingCommit(),
  });

  const syncSemanticAndFindState = (): void => {
    semanticController.syncSemanticState();
    findController.syncDocuments();
  };

  const dispatchAppPointerEvent = (eventType: number, handle: bigint, x: number, y: number, modifiers = 0): void => {
    if (handle === 0n) {
      return;
    }
    window.__effindomCallbacks?.onPointerEventWithCoords?.(eventType, handle, x, y, modifiers);
  };

  const isLastPointerStillOverCanvas = (): boolean => {
    const { x, y } = interactionState.getLastPointerClientPosition();
    if (x === null || y === null) {
      return false;
    }
    const rect = canvas.getBoundingClientRect();
    return x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
  };

  const syncAppPointerHover = (): void => {
    const { x, y } = interactionState.getLastPointerPosition();
    const previousHandle = interactionState.getLastInteractivePointerHandle();
    const capturedHandle = interactionState.getCapturedPointerHandle();
    if (capturedHandle !== null) {
      if (previousHandle === capturedHandle) {
        return;
      }
      if (previousHandle !== null) {
        dispatchAppPointerEvent(UI_EVENT_POINTER_LEAVE, previousHandle, x, y);
      }
      interactionState.setLastInteractivePointerHandle(capturedHandle);
      dispatchAppPointerEvent(UI_EVENT_POINTER_ENTER, capturedHandle, x, y);
      return;
    }
    const pointerInsideCanvas = interactionState.isPointerInsideCanvas() || isLastPointerStillOverCanvas();
    if (!pointerInsideCanvas) {
      if (previousHandle !== null) {
        interactionState.setLastInteractivePointerHandle(null);
        dispatchAppPointerEvent(UI_EVENT_POINTER_LEAVE, previousHandle, x, y);
      }
      return;
    }

    const hitHandle = handleToBigInt(core._ed_hit_test(x, y));
    const currentHandle = hitHandle === 0n ? null : hitHandle;
    if (currentHandle === previousHandle) {
      return;
    }
    if (previousHandle !== null) {
      dispatchAppPointerEvent(UI_EVENT_POINTER_LEAVE, previousHandle, x, y);
    }
    interactionState.setLastInteractivePointerHandle(currentHandle);
    if (currentHandle !== null) {
      dispatchAppPointerEvent(UI_EVENT_POINTER_ENTER, currentHandle, x, y);
    }
  };

  const updateCanvasSize = (): void => {
    const dpr = Math.max(1, window.devicePixelRatio || 1);
    const size = readCanvasLogicalSize(canvas);
    logicalWidth = size.width;
    logicalHeight = size.height;
    const physicalWidth = Math.round(logicalWidth * dpr);
    const physicalHeight = Math.round(logicalHeight * dpr);

    canvas.style.width = `${String(logicalWidth)}px`;
    canvas.style.height = `${String(logicalHeight)}px`;
    if (physicalWidth !== canvas.width || physicalHeight !== canvas.height) {
      canvas.width = physicalWidth;
      canvas.height = physicalHeight;
    }

    semanticController.syncSize(logicalWidth, logicalHeight);
    findController.syncSize(logicalWidth, logicalHeight);
    core._ed_resize(physicalWidth, physicalHeight, dpr);
    ui._ui_resize_window(logicalWidth, logicalHeight);
    syncSemanticAndFindState();
    setActiveRenderer(loaderInfo, normalizeBackendType(core._ed_get_backend_type()));
  };

  runtime = {
    core,
    ui,
    canvas,
    openCanvasApi: openCanvasApiAdapter.getApi(),
    logs: interactionState.logs,
    updateCanvasSize,
    extractCommandBuffer: () => extractCommandBuffer(ui),
    executeCommandBuffer: (words: Uint32Array) => {
      executeCommandBuffer(core, words);
    },
    syncCommandBufferToCore: () => {
      const words = extractCommandBuffer(ui);
      executeCommandBuffer(core, words);
      syncSemanticAndFindState();
      syncAppPointerHover();
      return words;
    },
    flushPendingCommit: () => {
      if (!needsCommit && !interactionState.hasPendingTextMutations()) {
        return null;
      }
      if (interactionState.hasPendingTextMutations()) {
        if (needsCommit) {
          needsCommit = false;
          runtime.syncCommandBufferToCore();
        }
        if (interactionState.materializePendingTextMutations()) {
          ui._ui_commit_frame();
          needsCommit = true;
        }
      }
      if (!needsCommit) {
        return null;
      }
      needsCommit = false;
      return runtime.syncCommandBufferToCore();
    },
    hasPendingCommit: () => needsCommit || interactionState.hasPendingTextMutations(),
    commitFrame: () => {
      if (interactionState.hasPendingTextMutations()) {
        if (needsCommit) {
          runtime.flushPendingCommit();
        }
        if (interactionState.materializePendingTextMutations()) {
          ui._ui_commit_frame();
          needsCommit = true;
          frameRequester?.();
          return;
        }
      }
      if (needsCommit) {
        runtime.flushPendingCommit();
      }
      ui._ui_commit_frame();
      needsCommit = true;
      frameRequester?.();
    },
    requestFrame: () => {
      frameRequester?.();
    },
    setFrameRequester: (requester: (() => void) | null) => {
      frameRequester = requester;
    },
    getSemanticTree: () => semanticController.getSemanticTree(),
    getActiveTextHandle: () => interactionState.getActiveTextHandle(),
    getCapturedPointerHandle: () => interactionState.getCapturedPointerHandle(),
    setCapturedPointerHandle: (handle: bigint | null) => {
      interactionState.setCapturedPointerHandle(handle);
    },
    setAppFrameHandler: (handler: ((timestampMs: number) => void) | null) => {
      appFrameHandler = handler;
      frameRequester?.();
    },
    runAppFrameHandler: (timestampMs: number) => {
      appFrameHandler?.(timestampMs);
    },
    uiHasPendingVisualWork: () => ui._ui_has_pending_visual_work() !== 0,
    uiNeedsAnimationFrame: () => ui._ui_needs_animation_frame() !== 0,
    getHandleFromPoint: (x: number, y: number) => handleToBigInt(core._ed_hit_test(x, y)),
    clearPointerHover: () => {
      interactionState.setLastInteractivePointerHandle(null);
    },
    refreshPointerHover: () => {
      syncAppPointerHover();
    },
    getFindDocuments: () => findController.getFindDocuments(),
    activateFindMatch: (match, reveal = true) => findController.activateFindMatch(match, reveal),
    syncFindSelection: (clearOnMissing = false) => findController.syncFindSelection(clearOnMissing),
    clearFindMatch: () => findController.clearFindMatch(),
    ensureFont: async (fontId: number) => {
      await fontManager.ensureFont(fontId);
    },
    ensureBuiltInFont: async (fontId: number) => {
      await fontManager.ensureBuiltInFont(fontId);
    },
    isFontLoaded: (fontId: number, url?: string) => fontManager.isFontLoaded(fontId, url),
    getIncrementalFontState: (fontId: number) => fontManager.getIncrementalFontState(fontId),
    getIncrementalFontCacheState: () => fontManager.getIncrementalFontCacheState(),
    getIncrementalFontPolicy: () => fontManager.getIncrementalFontPolicy(),
    setIncrementalFontPolicy: (policy) => {
      fontManager.setIncrementalFontPolicy(policy);
    },
    getClipboardFontUrl: (fontId: number) => fontManager.getClipboardFontUrl(fontId),
    registerLazyFont: (fontId: number, url: string) => {
      fontManager.registerLazyFont(fontId, url);
    },
    registerFontFallback: (fontId: number, fallbackFontId: number) => {
      fontManager.registerFontFallback(fontId, fallbackFontId);
    },
    handleMissingFontCoverage: (fontId: number, coverageKind: number, sampleText: string) => {
      fontManager.handleMissingFontCoverage(fontId, coverageKind, sampleText);
    },
    loadFont: async (fontId: number, url: string) => {
      await fontManager.loadFont(fontId, url);
    },
    registerFont: async (font: BridgeFontRegistration) => {
      await fontManager.registerFont(font);
    },
    registerFontStack: async (stack: BridgeFontStackRegistration) => {
      await fontManager.registerFontStack(stack);
    },
    loadSvg: async (svgId: number, url: string) => {
      return await assetManager.loadSvg(svgId, url);
    },
    loadTexture: async (textureId: number, url: string) => {
      return await assetManager.loadTexture(textureId, url);
    },
    releaseSvg: (svgId: number) => {
      assetManager.releaseSvg(svgId);
    },
    releaseTexture: (textureId: number) => {
      assetManager.releaseTexture(textureId);
    },
    replayLoadedAssets: async () => {
      await assetManager.replayLoadedAssets();
    },
    resetLogs: () => {
      resetBridgeLogs(interactionState.logs);
    },
    resetAppSession: () => {
      interactionState.resetAppSession();
    },
  };

  const refreshCanvas = (): void => {
    runtime.updateCanvasSize();
    runtime.commitFrame();
  };

  const canvasSizeSource = getCanvasSizeSource(canvas);
  const resizeObserver = typeof ResizeObserver !== 'undefined'
    ? new ResizeObserver(() => {
        refreshCanvas();
      })
    : null;
  if (resizeObserver !== null) {
    resizeObserver.observe(canvasSizeSource);
  }

  return {
    runtime,
    destroy: () => {
      resizeObserver?.disconnect();
      runtime.setFrameRequester(null);
      runtime.clearPointerHover();
      openCanvasApiAdapter.destroy();
      findController.destroy();
      semanticController.destroy();
      canvas.style.cursor = 'default';
    },
  };
}
