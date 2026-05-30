import type {
  BridgeRuntime,
  ClipboardWritePayload,
  EffinDomCallbacks,
  PointerEventLog,
  ScrollEventLog,
} from '../core-types';
import { enrichClipboardPayload, writeClipboardPayload } from '../clipboard';
import { createEditorSession, type EditorSession } from './interaction/editor-session';
import { createBridgeLogs } from './interaction/logs';
import { handleToString } from './utils/encoding';

export function installCallbacks(runtimeRef: { current: BridgeRuntime | null }): EditorSession {
  const logs = createBridgeLogs();
  const editorSession = createEditorSession(runtimeRef, logs);

  const callbacks: EffinDomCallbacks = {
    onPointerEvent: (handle, eventType) => {
      const { x, y } = editorSession.getLastPointerPosition();
      const modifiers = editorSession.getLastPointerModifiers();
      const entry: PointerEventLog = { handle: handleToString(handle), eventType };
      logs.pointerEvents.push(entry);
      window.__effindomCallbacks?.onPointerEventWithCoords?.(
        eventType,
        handle,
        x,
        y,
        modifiers,
      );
    },
    onFocusChanged: editorSession.handleFocusChanged,
    onTextChanged: editorSession.handleTextChanged,
    onRequestSemanticAnnouncement: editorSession.handleRequestSemanticAnnouncement,
    onTextReplaced: editorSession.handleTextReplaced,
    onSelectionChanged: editorSession.handleSelectionChanged,
    onScroll: (handle, offsetX, offsetY, contentWidth, contentHeight, viewportWidth, viewportHeight) => {
      const entry: ScrollEventLog = {
        handle: handleToString(handle),
        offsetX,
        offsetY,
        contentWidth,
        contentHeight,
        viewportWidth,
        viewportHeight,
      };
      logs.scrollEvents.push(entry);
    },
    onCrossSelectionChanged: (areaHandle, text) => {
      logs.crossSelectionChanges.push({ areaHandle: handleToString(areaHandle), text });
    },
    onClipboardWrite: (payload: ClipboardWritePayload) => {
      logs.clipboardWrites.push(payload.plainText);
      const runtime = runtimeRef.current;
      const enrichedPayload =
        runtime === null
          ? payload
          : enrichClipboardPayload(payload, (fontId) => runtime.getClipboardFontUrl(fontId));
      void writeClipboardPayload(enrichedPayload).catch(() => undefined);
    },
    onClipboardRead: editorSession.handleClipboardRead,
    onRequestFontLoad: (fontId, url) => {
      const runtime = runtimeRef.current;
      if (runtime === null || url.length === 0) {
        return;
      }
      void runtime.loadFont(fontId, url).catch((error: unknown) => {
        const message = error instanceof Error ? error.message : String(error);
        console.error(`[fui_host] font ${String(fontId)} failed lazy load from ${url}: ${message}`);
      });
    },
    onMissingFontCoverage: (fontId, coverageKind, sampleText) => {
      const runtime = runtimeRef.current;
      if (runtime === null) {
        return;
      }
      logs.missingFontCoverageRequests.push({
        fontId,
        coverageKind,
        sampleText,
      });
      runtime.handleMissingFontCoverage(fontId, coverageKind, sampleText);
    },
  };

  window.__effindomCallbacks = callbacks;
  return editorSession;
}
