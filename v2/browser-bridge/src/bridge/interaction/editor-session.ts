import type {
  BridgeLogs,
  BridgeRuntime,
  FocusEventLog,
  SelectionChangeLog,
  WasmHandleLike,
} from '../../core-types';
import type { BridgeInteractionState } from '../local-types';
import { codeUnitIndexToUtf8ByteOffset, utf8ByteLength } from './text-encoding';
import {
  applyUtf8ByteReplacementEdit,
  buildClampedTextboxEdit,
  buildHiddenEditorWindow,
  clearHiddenTextEditor,
  createHiddenTextEditor,
  type HiddenEditorWindow,
  type HiddenTextEditor,
  type PendingLocalReplacementEcho,
  type PendingLocalSelectionEcho,
  summarizeTextChange,
  utf8ByteOffsetToCodeUnitIndex,
} from './editor-model';
import { createEditorMutationController } from './editor-mutations';
import { handleToBigInt } from '../utils/encoding';
import { writeUtf8ToHeap } from '../utils/heap';

export interface EditorSession extends BridgeInteractionState {
  handleClipboardRead(handle: WasmHandleLike): void;
  handleFocusChanged(handle: WasmHandleLike, isFocused: boolean): void;
  handleRequestSemanticAnnouncement(handle: WasmHandleLike): void;
  handleSelectionChanged(handle: WasmHandleLike, start: number, end: number): void;
  handleTextChanged(handle: WasmHandleLike, text: string): void;
  handleTextReplaced(handle: WasmHandleLike, start: number, end: number, text: string): void;
}

const CARET_BLINK_INTERVAL_MS = 500;

function currentInteractionTimeMs(): bigint {
  return BigInt(Math.floor(performance.now()));
}

export function createEditorSession(
  runtimeRef: { current: BridgeRuntime | null },
  logs: BridgeLogs,
): EditorSession {
  const textByHandle = Object.create(null) as Record<string, string>;
  const selectionsByHandle = Object.create(null) as Record<string, { start: number; end: number }>;
  const hiddenInput = createHiddenTextEditor(false) as HTMLInputElement;
  const hiddenTextarea = createHiddenTextEditor(true) as HTMLTextAreaElement;
  let activeTextHandle: bigint | null = null;
  let lastPointerClientX: number | null = null;
  let lastPointerClientY: number | null = null;
  let lastPointerX = 0;
  let lastPointerY = 0;
  let lastPointerModifiers = 0;
  let lastInteractivePointerHandle: bigint | null = null;
  let capturedPointerHandle: bigint | null = null;
  let pointerInsideCanvas = false;
  let appSessionVersion = 0;
  let activeTextEditable = false;
  let activeTextMultiline = false;
  let activeEditorWindow: HiddenEditorWindow = { text: '', docStart: 0, docEnd: 0, textStart: 0, textEnd: 0 };
  const textByteLengthsByHandle = Object.create(null) as Record<string, number>;
  const pendingCaretRevealByHandle = Object.create(null) as Record<string, boolean>;
  let pendingCaretRevealFrame: number | null = null;
  let pendingLocalReplacementEcho: PendingLocalReplacementEcho | null = null;
  let pendingLocalSelectionEcho: PendingLocalSelectionEcho | null = null;
  let deferredTouchFocusHandle: string | null = null;
  let caretBlinkTimer: ReturnType<typeof setTimeout> | null = null;
  let focusedHandle: string | null = null;
  const pendingSemanticAnnouncements = new Set<string>();
  window.__bridgeLogs = logs;
  window.__bridgeTextByHandle = textByHandle;
  window.__bridgeSelectionsByHandle = selectionsByHandle;
  window.__bridgeActiveEditorWindow = { handle: null, ...activeEditorWindow };

  const getActiveEditor = (): HiddenTextEditor => (activeTextMultiline ? hiddenTextarea : hiddenInput);

  const isActiveEditorFocused = (): boolean => document.activeElement === getActiveEditor();

  const queueSemanticAnnouncement = (handleKey: string): void => {
    pendingSemanticAnnouncements.add(handleKey);
    runtimeRef.current?.requestFrame();
  };

  const clampSelectionToText = (
    length: number,
    selection: { start: number; end: number },
  ): { start: number; end: number } => ({
    start: Math.max(0, Math.min(selection.start, length)),
    end: Math.max(0, Math.min(selection.end, length)),
  });

  const clearCaretBlinkTimer = (): void => {
    if (caretBlinkTimer !== null) {
      clearTimeout(caretBlinkTimer);
      caretBlinkTimer = null;
    }
  };

  const shouldRunCaretBlinkTimer = (): boolean => {
    if (activeTextHandle === null || !isActiveEditorFocused()) {
      return false;
    }
    const handleKey = activeTextHandle.toString();
    const text = textByHandle[handleKey] ?? '';
    const textByteLength = textByteLengthsByHandle[handleKey] ?? utf8ByteLength(text);
    const selection = selectionsByHandle[handleKey] ?? { start: textByteLength, end: textByteLength };
    const { start, end } = clampSelectionToText(textByteLength, selection);
    return start === end;
  };

  const armCaretBlinkTimer = (): void => {
    if (caretBlinkTimer !== null) {
      return;
    }
    caretBlinkTimer = setTimeout(() => {
      caretBlinkTimer = null;
      if (!shouldRunCaretBlinkTimer()) {
        return;
      }
      runtimeRef.current?.requestFrame();
      armCaretBlinkTimer();
    }, CARET_BLINK_INTERVAL_MS);
  };

  const updateCaretBlinkTimer = (resetPhase = false): void => {
    if (!shouldRunCaretBlinkTimer()) {
      clearCaretBlinkTimer();
      return;
    }
    if (resetPhase) {
      clearCaretBlinkTimer();
    }
    armCaretBlinkTimer();
  };

  const syncActiveEditorWindowDebug = (handle: string | null): void => {
    window.__bridgeActiveEditorWindow = {
      handle,
      text: activeEditorWindow.text,
      docStart: activeEditorWindow.docStart,
      docEnd: activeEditorWindow.docEnd,
    };
  };

  const clearActiveEditorWindow = (): void => {
    activeEditorWindow = { text: '', docStart: 0, docEnd: 0, textStart: 0, textEnd: 0 };
    syncActiveEditorWindowDebug(null);
  };

  const clearPendingCaretReveal = (): void => {
    if (pendingCaretRevealFrame !== null) {
      cancelAnimationFrame(pendingCaretRevealFrame);
      pendingCaretRevealFrame = null;
    }
    for (const key of Object.keys(pendingCaretRevealByHandle)) {
      delete pendingCaretRevealByHandle[key];
    }
  };

  const updateActiveEditorWindowText = (text: string): void => {
    activeEditorWindow = {
      text,
      docStart: activeEditorWindow.docStart,
      docEnd: activeEditorWindow.docStart + utf8ByteLength(text),
      textStart: activeEditorWindow.textStart,
      textEnd: activeEditorWindow.textStart + text.length,
    };
    syncActiveEditorWindowDebug(activeTextHandle?.toString() ?? null);
  };

  const detachBridgeTextInput = (): void => {
    hiddenInput.blur();
    hiddenTextarea.blur();
  };

  const getTextboxState = (
    handleKey: string,
  ): { isTextbox: boolean; isEditable: boolean; isMultiline: boolean } => {
    const runtime = runtimeRef.current;
    if (runtime === null) {
      return { isTextbox: false, isEditable: false, isMultiline: false };
    }
    const node = runtime.getSemanticTree().find((entry) => entry.handle === handleKey);
    if (node?.roleName !== 'textbox') {
      return { isTextbox: false, isEditable: false, isMultiline: false };
    }
    return {
      isTextbox: true,
      isEditable: node.state.readonly !== true,
      isMultiline: node.state.multiline === true,
    };
  };

  const syncFocusedInputState = (): void => {
    if (activeTextHandle === null) {
      clearCaretBlinkTimer();
      clearActiveEditorWindow();
      detachBridgeTextInput();
      clearHiddenTextEditor(hiddenInput);
      clearHiddenTextEditor(hiddenTextarea);
      return;
    }

    const activeEditor = getActiveEditor();
    const handleKey = activeTextHandle.toString();
    const text = textByHandle[handleKey] ?? '';
    const textByteLength = textByteLengthsByHandle[handleKey] ?? utf8ByteLength(text);
    textByteLengthsByHandle[handleKey] = textByteLength;
    const selection = selectionsByHandle[handleKey] ?? { start: textByteLength, end: textByteLength };
    const { start: startByte, end: endByte } = clampSelectionToText(textByteLength, selection);
    const start = utf8ByteOffsetToCodeUnitIndex(text, startByte, textByteLength);
    const end = utf8ByteOffsetToCodeUnitIndex(text, endByte, textByteLength);
    const direction = start === end ? 'none' : (startByte < endByte ? 'forward' : 'backward');
    const normalizedStart = Math.min(start, end);
    const normalizedEnd = Math.max(start, end);
    activeEditorWindow = buildHiddenEditorWindow(text, normalizedStart, normalizedEnd, textByteLength, activeEditorWindow);
    syncActiveEditorWindowDebug(handleKey);
    const localStart = normalizedStart - activeEditorWindow.textStart;
    const localEnd = normalizedEnd - activeEditorWindow.textStart;
    if (activeEditor.value !== activeEditorWindow.text) {
      activeEditor.value = activeEditorWindow.text;
    }
    activeEditor.readOnly = !activeTextEditable;
    activeEditor.setSelectionRange(localStart, localEnd, direction);
    updateCaretBlinkTimer();
  };

  const mutationController = createEditorMutationController({
    runtimeRef,
    textByHandle,
    selectionsByHandle,
    textByteLengthsByHandle,
    getActiveEditor,
    getActiveEditorWindow: () => activeEditorWindow,
    getActiveTextEditable: () => activeTextEditable,
    getActiveTextHandle: () => activeTextHandle,
    isActiveEditorFocused,
    setPendingLocalReplacementEcho: (value) => {
      pendingLocalReplacementEcho = value;
    },
    setPendingLocalSelectionEcho: (value) => {
      pendingLocalSelectionEcho = value;
    },
    syncFocusedInputState,
    updateActiveEditorWindowText,
  });

  const clearPendingTextMutations = (): void => {
    mutationController.clearPendingTextMutations();
  };

  const focusHiddenEditorNow = (): void => {
    getActiveEditor().focus({ preventScroll: true });
    syncFocusedInputState();
    updateCaretBlinkTimer(true);
  };

  const refocusActiveTextInput = (): void => {
    if (activeTextHandle === null) {
      if (deferredTouchFocusHandle === null) {
        return;
      }
      const textboxState = getTextboxState(deferredTouchFocusHandle);
      if (!textboxState.isTextbox) {
        return;
      }
      activeTextHandle = handleToBigInt(deferredTouchFocusHandle);
      activeTextEditable = textboxState.isEditable;
      activeTextMultiline = textboxState.isMultiline;
      syncFocusedInputState();
      focusHiddenEditorNow();
      return;
    }
    focusHiddenEditorNow();
  };

  const beginTouchTextFocusDeferral = (handle: bigint): void => {
    deferredTouchFocusHandle = handle.toString();
  };

  const cancelTouchTextFocusDeferral = (): void => {
    deferredTouchFocusHandle = null;
  };

  const commitTouchTextFocusDeferral = (handle: bigint): void => {
    const handleKey = handle.toString();
    if (deferredTouchFocusHandle !== handleKey) {
      return;
    }
    deferredTouchFocusHandle = null;
    if (activeTextHandle?.toString() !== handleKey) {
      return;
    }
    if (!isActiveEditorFocused()) {
      focusHiddenEditorNow();
    }
  };

  const scheduleCaretRevealReplay = (handleKey: string): void => {
    if (!pendingCaretRevealByHandle[handleKey] || activeTextHandle?.toString() !== handleKey) {
      return;
    }
    if (pendingCaretRevealFrame !== null) {
      return;
    }
    pendingCaretRevealFrame = requestAnimationFrame(() => {
      pendingCaretRevealFrame = null;
      if (!pendingCaretRevealByHandle[handleKey] || activeTextHandle?.toString() !== handleKey) {
        return;
      }
      delete pendingCaretRevealByHandle[handleKey];
      const runtime = runtimeRef.current;
      if (runtime === null) {
        return;
      }
      const text = textByHandle[handleKey] ?? '';
      const textByteLength = utf8ByteLength(text);
      const selection = selectionsByHandle[handleKey] ?? { start: textByteLength, end: textByteLength };
      const { start, end } = clampSelectionToText(textByteLength, selection);
      runtime.ui._ui_set_text_selection_range(handleToBigInt(handleKey), start, end);
    });
  };

  const clearRecordMap = <T>(record: Record<string, T>): void => {
    for (const key of Object.keys(record)) {
      delete record[key];
    }
  };

  const resetAppSession = (): void => {
    appSessionVersion += 1;
    clearRecordMap(textByHandle);
    clearRecordMap(textByteLengthsByHandle);
    clearRecordMap(selectionsByHandle);
    clearPendingCaretReveal();
    mutationController.reset();
    pendingLocalReplacementEcho = null;
    pendingLocalSelectionEcho = null;
    deferredTouchFocusHandle = null;
    clearCaretBlinkTimer();
    activeTextHandle = null;
    activeTextEditable = false;
    activeTextMultiline = false;
    clearActiveEditorWindow();
    focusedHandle = null;
    pendingSemanticAnnouncements.clear();
    lastInteractivePointerHandle = null;
    capturedPointerHandle = null;
    hiddenInput.value = '';
    hiddenInput.setSelectionRange(0, 0, 'none');
    hiddenTextarea.value = '';
    hiddenTextarea.setSelectionRange(0, 0, 'none');
    detachBridgeTextInput();
  };
  mutationController.attachHiddenEditorListeners(hiddenInput);
  mutationController.attachHiddenEditorListeners(hiddenTextarea);

  const handleFocusChanged = (handle: WasmHandleLike, isFocused: boolean): void => {
    const handleKey = handle.toString();
    const entry: FocusEventLog = { handle: handleKey, isFocused };
    logs.focusEvents.push(entry);
    if (isFocused) {
      focusedHandle = handleKey;
    } else if (focusedHandle === handleKey) {
      focusedHandle = null;
    }
    if (isFocused) {
      const textboxState = getTextboxState(handleKey);
      if (!textboxState.isTextbox) {
        deferredTouchFocusHandle = null;
        delete pendingCaretRevealByHandle[handleKey];
        activeTextHandle = null;
        activeTextEditable = false;
        activeTextMultiline = false;
        syncFocusedInputState();
        updateCaretBlinkTimer();
        queueSemanticAnnouncement(handleKey);
        return;
      }
      activeTextHandle = handleToBigInt(handle);
      activeTextEditable = textboxState.isEditable;
      activeTextMultiline = textboxState.isMultiline;
      syncFocusedInputState();
      if (deferredTouchFocusHandle === handleKey) {
        updateCaretBlinkTimer();
        queueSemanticAnnouncement(handleKey);
        return;
      }
      window.setTimeout(() => {
        if (activeTextHandle !== null && activeTextHandle.toString() === handleKey) {
          focusHiddenEditorNow();
        }
      }, 0);
    } else if (activeTextHandle !== null && activeTextHandle.toString() === handleKey) {
      if (deferredTouchFocusHandle === handleKey) {
        updateCaretBlinkTimer();
        return;
      }
      deferredTouchFocusHandle = null;
      runtimeRef.current?.flushPendingCommit();
      delete pendingCaretRevealByHandle[handleKey];
      pendingLocalReplacementEcho = null;
      pendingLocalSelectionEcho = null;
      clearPendingTextMutations();
      activeTextHandle = null;
      activeTextEditable = false;
      activeTextMultiline = false;
      syncFocusedInputState();
    }
    updateCaretBlinkTimer();
    if (isFocused) {
      queueSemanticAnnouncement(handleKey);
    }
  };

  const handleTextChanged = (handle: WasmHandleLike, text: string): void => {
    const handleKey = handle.toString();
    if (pendingLocalReplacementEcho !== null && pendingLocalReplacementEcho.handle === handleKey) {
      pendingLocalReplacementEcho = null;
    }
    textByHandle[handleKey] = text;
    textByteLengthsByHandle[handleKey] = utf8ByteLength(text);
    logs.textChanges.push(summarizeTextChange(handleKey, text));
    if (activeTextHandle !== null && activeTextHandle.toString() === handleKey) {
      pendingCaretRevealByHandle[handleKey] = true;
      if (!isActiveEditorFocused()) {
        focusHiddenEditorNow();
        updateCaretBlinkTimer(true);
        return;
      }
      syncFocusedInputState();
      updateCaretBlinkTimer(true);
    }
  };

  const handleRequestSemanticAnnouncement = (handle: WasmHandleLike): void => {
    queueSemanticAnnouncement(handle.toString());
  };

  const handleTextReplaced = (handle: WasmHandleLike, start: number, end: number, text: string): void => {
    const handleKey = handle.toString();
    const previousText = textByHandle[handleKey] ?? '';
    const previousTextByteLength = textByteLengthsByHandle[handleKey] ?? utf8ByteLength(previousText);
    const isLocalEcho = pendingLocalReplacementEcho !== null
      && pendingLocalReplacementEcho.handle === handleKey
      && pendingLocalReplacementEcho.start === start
      && pendingLocalReplacementEcho.end === end
      && pendingLocalReplacementEcho.text === text
      && activeTextHandle !== null
      && activeTextHandle.toString() === handleKey
      && isActiveEditorFocused();
    const nextText = isLocalEcho
      ? previousText
      : applyUtf8ByteReplacementEdit(previousText, start, end, text);
    if (!isLocalEcho) {
      textByHandle[handleKey] = nextText;
      textByteLengthsByHandle[handleKey] = previousTextByteLength - (end - start) + utf8ByteLength(text);
    }
    if (isLocalEcho) {
      pendingLocalReplacementEcho = null;
    }
    logs.textChanges.push(summarizeTextChange(handleKey, nextText));
    if (activeTextHandle !== null && activeTextHandle.toString() === handleKey) {
      pendingCaretRevealByHandle[handleKey] = true;
      if (!isActiveEditorFocused()) {
        focusHiddenEditorNow();
        updateCaretBlinkTimer(true);
        return;
      }
      if (!isLocalEcho) {
        syncFocusedInputState();
      }
      updateCaretBlinkTimer(true);
    }
  };

  const handleSelectionChanged = (handle: WasmHandleLike, start: number, end: number): void => {
    const handleKey = handle.toString();
    const isLocalEcho = pendingLocalSelectionEcho !== null
      && pendingLocalSelectionEcho.handle === handleKey
      && pendingLocalSelectionEcho.start === start
      && pendingLocalSelectionEcho.end === end
      && activeTextHandle !== null
      && activeTextHandle.toString() === handleKey
      && isActiveEditorFocused();
    selectionsByHandle[handleKey] = { start, end };
    if (pendingLocalSelectionEcho !== null && pendingLocalSelectionEcho.handle === handleKey) {
      pendingLocalSelectionEcho = null;
    }
    const entry: SelectionChangeLog = { handle: handleKey, start, end };
    logs.selectionChanges.push(entry);
    if (activeTextHandle !== null && activeTextHandle.toString() === handleKey) {
      if (!isActiveEditorFocused()) {
        focusHiddenEditorNow();
        updateCaretBlinkTimer(true);
        return;
      }
      if (!isLocalEcho) {
        syncFocusedInputState();
      }
      updateCaretBlinkTimer(true);
      scheduleCaretRevealReplay(handleKey);
    }
  };

  const handleClipboardRead = (handle: WasmHandleLike): void => {
    const runtime = runtimeRef.current;
    if (runtime === null) {
      return;
    }
    const handleValue = handleToBigInt(handle);
    const requestSessionVersion = appSessionVersion;
    logs.clipboardReadRequests.push(handleValue.toString());
    void navigator.clipboard.readText().then((text) => {
      if (requestSessionVersion !== appSessionVersion) {
        return;
      }
      const handleKey = handleValue.toString();
      const currentText = textByHandle[handleKey] ?? '';
      const currentTextByteLength = textByteLengthsByHandle[handleKey] ?? utf8ByteLength(currentText);
      const selection = selectionsByHandle[handleKey] ?? { start: currentTextByteLength, end: currentTextByteLength };
      const rangeStart = Math.max(0, Math.min(selection.start, selection.end));
      const rangeEnd = Math.max(rangeStart, Math.min(currentTextByteLength, Math.max(selection.start, selection.end)));
      const clampedEdit = buildClampedTextboxEdit(
        currentText,
        rangeStart,
        rangeEnd,
        text,
        rangeStart + utf8ByteLength(text),
      );
      textByHandle[handleKey] = clampedEdit.fullNextText;
      textByteLengthsByHandle[handleKey] = utf8ByteLength(clampedEdit.fullNextText);
      selectionsByHandle[handleKey] = { start: clampedEdit.caretByte, end: clampedEdit.caretByte };
      if (activeTextHandle !== null && activeTextHandle.toString() === handleKey) {
        syncFocusedInputState();
      }
      if (clampedEdit.replacement === null) {
        return;
      }
      runtime.ui._ui_set_interaction_time(currentInteractionTimeMs());
      const replacementStartByte = codeUnitIndexToUtf8ByteOffset(currentText, clampedEdit.replacement.start);
      const replacementEndByte = codeUnitIndexToUtf8ByteOffset(currentText, clampedEdit.replacement.end);
      pendingLocalReplacementEcho = {
        handle: handleKey,
        start: replacementStartByte,
        end: replacementEndByte,
        text: clampedEdit.replacement.insertedText,
      };
      pendingLocalSelectionEcho = {
        handle: handleKey,
        start: clampedEdit.caretByte,
        end: clampedEdit.caretByte,
      };
      const heapString = writeUtf8ToHeap(runtime.ui, clampedEdit.replacement.insertedText);
      try {
        runtime.ui._ui_replace_text_range(
          handleValue,
          replacementStartByte,
          replacementEndByte,
          heapString.ptr,
          heapString.len,
          clampedEdit.caretByte,
        );
      } finally {
        heapString.dispose();
      }
      runtime.commitFrame();
    }).catch(() => undefined);
  };

  return {
    logs,
    textByHandle,
    selectionsByHandle,
    hasPendingTextMutations: mutationController.hasPendingTextMutations,
    materializePendingTextMutations: mutationController.materializePendingTextMutations,
    getActiveTextEditable: () => activeTextEditable,
    getActiveTextHandle: () => activeTextHandle,
    getActiveTextMultiline: () => activeTextMultiline,
    getCapturedPointerHandle: () => capturedPointerHandle,
    getLastPointerClientPosition: () => ({ x: lastPointerClientX, y: lastPointerClientY }),
    getLastPointerPosition: () => ({ x: lastPointerX, y: lastPointerY }),
    getLastPointerModifiers: () => lastPointerModifiers,
    getLastInteractivePointerHandle: () => lastInteractivePointerHandle,
    isActiveTextInputFocused: isActiveEditorFocused,
    isPointerInsideCanvas: () => pointerInsideCanvas,
    applyActiveTextDeletion: mutationController.applyActiveTextDeletion,
    beginTouchTextFocusDeferral,
    cancelTouchTextFocusDeferral,
    commitTouchTextFocusDeferral,
    refocusActiveTextInput,
    resetAppSession,
    consumePendingSemanticAnnouncements: () => {
      const handles = Array.from(pendingSemanticAnnouncements.values());
      pendingSemanticAnnouncements.clear();
      return handles;
    },
    getFocusedHandle: () => focusedHandle,
    setCapturedPointerHandle: (handle: bigint | null) => {
      capturedPointerHandle = handle;
    },
    setLastPointerClientPosition: (x: number, y: number) => {
      lastPointerClientX = x;
      lastPointerClientY = y;
    },
    setLastPointerModifiers: (modifiers: number) => {
      lastPointerModifiers = modifiers;
    },
    setLastPointerPosition: (x: number, y: number) => {
      lastPointerX = x;
      lastPointerY = y;
    },
    setLastInteractivePointerHandle: (handle: bigint | null) => {
      lastInteractivePointerHandle = handle;
    },
    setPointerInsideCanvas: (flag: boolean) => {
      pointerInsideCanvas = flag;
    },
    handleClipboardRead,
    handleFocusChanged,
    handleRequestSemanticAnnouncement,
    handleSelectionChanged,
    handleTextChanged,
    handleTextReplaced,
  };
}
