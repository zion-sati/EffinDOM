import type { BridgeRuntime } from '../../core-types';
import { commitIfVisualWork } from '../commit-policy';
import { handleToBigInt } from '../utils/encoding';
import { writeUtf8ToHeap } from '../utils/heap';
import { advanceCodeUnitIndex, codeUnitIndexToUtf8ByteOffset, retreatCodeUnitIndex, utf8ByteLength } from './text-encoding';
import {
  applyUtf8ByteReplacementEdit,
  buildClampedTextboxEdit,
  computeReplacementEdit,
  mapPendingBatchCurrentIndexToBaseIndex,
  type HiddenEditorWindow,
  type HiddenTextEditor,
  type PendingLocalReplacementEcho,
  type PendingLocalSelectionEcho,
  type PendingPasteInput,
  type PendingTextMutationBatch,
  type ReplacementEdit,
} from './editor-model';

const MAX_BUFFERED_TYPING_MUTATIONS = 5;

function currentInteractionTimeMs(): bigint {
  return BigInt(Math.floor(performance.now()));
}

export interface EditorMutationController {
  applyActiveTextDeletion(forward: boolean): boolean;
  attachHiddenEditorListeners(editor: HiddenTextEditor): void;
  clearPendingTextMutations(): void;
  flushPendingTextMutationsToRuntime(): void;
  hasPendingTextMutations(): boolean;
  materializePendingTextMutations(): boolean;
  reset(): void;
}

interface EditorMutationControllerOptions {
  readonly runtimeRef: { current: BridgeRuntime | null };
  readonly textByHandle: Record<string, string>;
  readonly selectionsByHandle: Record<string, { start: number; end: number }>;
  readonly textByteLengthsByHandle: Record<string, number>;
  getActiveEditor(): HiddenTextEditor;
  getActiveEditorWindow(): HiddenEditorWindow;
  getActiveTextEditable(): boolean;
  getActiveTextHandle(): bigint | null;
  isActiveEditorFocused(): boolean;
  setPendingLocalReplacementEcho(value: PendingLocalReplacementEcho | null): void;
  setPendingLocalSelectionEcho(value: PendingLocalSelectionEcho | null): void;
  syncFocusedInputState(): void;
  updateActiveEditorWindowText(text: string): void;
}

export function createEditorMutationController(
  options: EditorMutationControllerOptions,
): EditorMutationController {
  let hiddenInputIsComposing = false;
  let pendingTextMutationFlushFrame: number | null = null;
  let pendingTextMutationBatch: PendingTextMutationBatch | null = null;
  let pendingPasteInput: PendingPasteInput | null = null;
  let pendingPasteText = '';

  const clearPendingTextMutations = (): void => {
    if (pendingTextMutationFlushFrame !== null) {
      cancelAnimationFrame(pendingTextMutationFlushFrame);
      pendingTextMutationFlushFrame = null;
    }
    pendingTextMutationBatch = null;
    pendingPasteInput = null;
    pendingPasteText = '';
  };

  const materializePendingTextMutations = (): boolean => {
    const runtime = options.runtimeRef.current;
    const batch = pendingTextMutationBatch;
    if (runtime === null || batch === null) {
      return false;
    }
    pendingTextMutationBatch = null;
    const handle = handleToBigInt(batch.handle);
    const absoluteCaret = batch.docStart + codeUnitIndexToUtf8ByteOffset(batch.currentWindowText, batch.caret);
    runtime.ui._ui_set_interaction_time(batch.interactionTime);
    const replacement = computeReplacementEdit(batch.baseWindowText, batch.currentWindowText);
    if (replacement === null) {
      options.setPendingLocalSelectionEcho({
        handle: batch.handle,
        start: absoluteCaret,
        end: absoluteCaret,
      });
      runtime.ui._ui_set_text_selection_range(handle, absoluteCaret, absoluteCaret);
      return true;
    }

    const absoluteStart = batch.docStart + codeUnitIndexToUtf8ByteOffset(batch.baseWindowText, replacement.start);
    const absoluteEnd = batch.docStart + codeUnitIndexToUtf8ByteOffset(batch.baseWindowText, replacement.end);
    options.setPendingLocalReplacementEcho({
      handle: batch.handle,
      start: absoluteStart,
      end: absoluteEnd,
      text: replacement.insertedText,
    });
    options.setPendingLocalSelectionEcho({
      handle: batch.handle,
      start: absoluteCaret,
      end: absoluteCaret,
    });
    const heapString = writeUtf8ToHeap(runtime.ui, replacement.insertedText);
    try {
      runtime.ui._ui_replace_text_range(
        handle,
        absoluteStart,
        absoluteEnd,
        heapString.ptr,
        heapString.len,
        absoluteCaret,
      );
    } finally {
      heapString.dispose();
    }
    return true;
  };

  const restorePendingTypingState = (editor: HiddenTextEditor): boolean => {
    const batch = pendingTextMutationBatch;
    const activeTextHandle = options.getActiveTextHandle();
    const activeEditorWindow = options.getActiveEditorWindow();
    if (
      batch === null ||
      batch.kind !== 'typing' ||
      batch.mutationCount < MAX_BUFFERED_TYPING_MUTATIONS ||
      activeTextHandle === null ||
      batch.handle !== activeTextHandle.toString() ||
      batch.docStart !== activeEditorWindow.docStart
    ) {
      return false;
    }
    editor.value = batch.currentWindowText;
    editor.setSelectionRange(batch.caret, batch.caret, 'none');
    return true;
  };

  const flushPendingTextMutationsToRuntime = (): void => {
    const runtime = options.runtimeRef.current;
    if (runtime === null || pendingTextMutationBatch === null) {
      return;
    }
    if (materializePendingTextMutations()) {
      runtime.commitFrame();
    }
  };

  const schedulePendingTextMutationFlush = (): void => {
    if (pendingTextMutationFlushFrame !== null) {
      return;
    }
    pendingTextMutationFlushFrame = requestAnimationFrame(() => {
      pendingTextMutationFlushFrame = null;
      options.runtimeRef.current?.flushPendingCommit();
    });
  };

  const commitReplacementEdit = (
    previousText: string,
    nextText: string,
    replacement: ReplacementEdit,
    caret: number,
    kind: 'typing' | 'paste' = 'typing',
    replacePendingBatch = false,
  ): void => {
    const runtime = options.runtimeRef.current;
    const activeTextHandle = options.getActiveTextHandle();
    const activeEditorWindow = options.getActiveEditorWindow();
    if (runtime === null || activeTextHandle === null) {
      return;
    }
    const clampedStart = Math.max(0, Math.min(replacement.start, previousText.length));
    const clampedEnd = Math.max(clampedStart, Math.min(replacement.end, previousText.length));
    const activeHandleKey = activeTextHandle.toString();
    if (pendingTextMutationBatch === null && runtime.hasPendingCommit()) {
      runtime.flushPendingCommit();
    }

    const absoluteStart = activeEditorWindow.docStart + codeUnitIndexToUtf8ByteOffset(previousText, clampedStart);
    const absoluteEnd = activeEditorWindow.docStart + codeUnitIndexToUtf8ByteOffset(previousText, clampedEnd);
    const intendedAbsoluteCaret =
      activeEditorWindow.docStart + codeUnitIndexToUtf8ByteOffset(nextText, Math.max(0, Math.min(caret, nextText.length)));
    let fullPreviousText = options.textByHandle[activeHandleKey] ?? '';
    if (
      replacePendingBatch &&
      pendingTextMutationBatch !== null &&
      pendingTextMutationBatch.handle === activeHandleKey &&
      pendingTextMutationBatch.docStart === activeEditorWindow.docStart
    ) {
      const pendingReplacement = computeReplacementEdit(
        pendingTextMutationBatch.baseWindowText,
        pendingTextMutationBatch.currentWindowText,
      );
      if (pendingReplacement !== null) {
        const pendingAbsoluteStart =
          pendingTextMutationBatch.docStart +
          codeUnitIndexToUtf8ByteOffset(pendingTextMutationBatch.baseWindowText, pendingReplacement.start);
        const pendingAbsoluteEnd = pendingAbsoluteStart + utf8ByteLength(pendingReplacement.insertedText);
        fullPreviousText = applyUtf8ByteReplacementEdit(
          fullPreviousText,
          pendingAbsoluteStart,
          pendingAbsoluteEnd,
          pendingTextMutationBatch.baseWindowText.slice(pendingReplacement.start, pendingReplacement.end),
        );
      }
    }
    const clampedEdit = buildClampedTextboxEdit(
      fullPreviousText,
      absoluteStart,
      absoluteEnd,
      replacement.insertedText,
      intendedAbsoluteCaret,
    );
    if (clampedEdit.clampChanged) {
      if (pendingTextMutationBatch !== null) {
        if (materializePendingTextMutations()) {
          runtime.commitFrame();
        }
      }
      if (runtime.hasPendingCommit()) {
        runtime.flushPendingCommit();
      }
      clearPendingTextMutations();
      options.textByHandle[activeHandleKey] = clampedEdit.fullNextText;
      options.textByteLengthsByHandle[activeHandleKey] = utf8ByteLength(clampedEdit.fullNextText);
      options.selectionsByHandle[activeHandleKey] = { start: clampedEdit.caretByte, end: clampedEdit.caretByte };
      options.syncFocusedInputState();
    } else {
      options.textByHandle[activeHandleKey] = clampedEdit.fullNextText;
      options.textByteLengthsByHandle[activeHandleKey] = utf8ByteLength(clampedEdit.fullNextText);
      options.selectionsByHandle[activeHandleKey] = { start: clampedEdit.caretByte, end: clampedEdit.caretByte };
      options.updateActiveEditorWindowText(nextText);
    }

    if (clampedEdit.replacement === null) {
      return;
    }
    if (clampedEdit.clampChanged) {
      const replacementStartByte = codeUnitIndexToUtf8ByteOffset(fullPreviousText, clampedEdit.replacement.start);
      const replacementEndByte = codeUnitIndexToUtf8ByteOffset(fullPreviousText, clampedEdit.replacement.end);
      options.setPendingLocalReplacementEcho({
        handle: activeHandleKey,
        start: replacementStartByte,
        end: replacementEndByte,
        text: clampedEdit.replacement.insertedText,
      });
      options.setPendingLocalSelectionEcho({
        handle: activeHandleKey,
        start: clampedEdit.caretByte,
        end: clampedEdit.caretByte,
      });
      runtime.ui._ui_set_interaction_time(currentInteractionTimeMs());
      const heapString = writeUtf8ToHeap(runtime.ui, clampedEdit.replacement.insertedText);
      try {
        runtime.ui._ui_replace_text_range(
          activeTextHandle,
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
      return;
    }

    if (
      pendingTextMutationBatch !== null &&
      (
        pendingTextMutationBatch.handle !== activeHandleKey ||
        pendingTextMutationBatch.docStart !== activeEditorWindow.docStart ||
        (!replacePendingBatch && pendingTextMutationBatch.kind !== kind)
      )
    ) {
      if (materializePendingTextMutations()) {
        runtime.commitFrame();
      }
      runtime.flushPendingCommit();
    }

    const activeEditor = options.getActiveEditor();
    pendingTextMutationBatch = pendingTextMutationBatch === null || replacePendingBatch
      ? {
          handle: activeHandleKey,
          docStart: activeEditorWindow.docStart,
          baseWindowText: previousText,
          currentWindowText: activeEditor.value,
          caret: activeEditor.selectionStart ?? activeEditor.value.length,
          interactionTime: currentInteractionTimeMs(),
          kind,
          mutationCount: 1,
        }
      : {
          ...pendingTextMutationBatch,
          currentWindowText: activeEditor.value,
          caret: activeEditor.selectionStart ?? activeEditor.value.length,
          interactionTime: currentInteractionTimeMs(),
          mutationCount: kind === 'paste' ? 1 : pendingTextMutationBatch.mutationCount + 1,
        };
    schedulePendingTextMutationFlush();
    runtime.requestFrame();
  };

  const commitImeEdit = (
    text: string,
    caret: number,
    kind: 'typing' | 'paste' = 'typing',
    replacePendingBatch = false,
  ): void => {
    const previousText = options.getActiveEditorWindow().text;
    const replacement = computeReplacementEdit(previousText, text);
    if (replacement === null) {
      const runtime = options.runtimeRef.current;
      if (runtime !== null) {
        commitIfVisualWork(runtime);
      }
      return;
    }
    commitReplacementEdit(previousText, text, replacement, caret, kind, replacePendingBatch);
  };

  const applyActiveTextDeletion = (forward: boolean): boolean => {
    if (
      options.getActiveTextHandle() === null ||
      !options.getActiveTextEditable() ||
      hiddenInputIsComposing ||
      !options.isActiveEditorFocused()
    ) {
      return false;
    }
    const editor = options.getActiveEditor();
    const text = editor.value;
    const selectionStart = editor.selectionStart ?? text.length;
    const selectionEnd = editor.selectionEnd ?? selectionStart;
    const rangeStart = Math.min(selectionStart, selectionEnd);
    const rangeEnd = Math.max(selectionStart, selectionEnd);
    let nextText = text;
    let nextCaret = rangeStart;
    let replacementStart = rangeStart;
    let replacementEnd = rangeEnd;
    if (rangeStart !== rangeEnd) {
      nextText = text.slice(0, rangeStart) + text.slice(rangeEnd);
    } else if (forward) {
      if (rangeStart >= text.length) {
        return true;
      }
      replacementEnd = advanceCodeUnitIndex(text, rangeStart);
      nextText = text.slice(0, rangeStart) + text.slice(replacementEnd);
    } else {
      if (rangeStart === 0) {
        return true;
      }
      replacementStart = retreatCodeUnitIndex(text, rangeStart);
      nextCaret = replacementStart;
      nextText = text.slice(0, nextCaret) + text.slice(rangeStart);
    }
    editor.value = nextText;
    editor.setSelectionRange(nextCaret, nextCaret, 'none');
    commitReplacementEdit(
      text,
      nextText,
      {
        start: replacementStart,
        end: replacementEnd,
        insertedText: '',
      },
      nextCaret,
    );
    return true;
  };

  const attachHiddenEditorListeners = (editor: HiddenTextEditor): void => {
    editor.addEventListener('paste', (event) => {
      const clipboardEvent = event as ClipboardEvent;
      pendingPasteText = clipboardEvent.clipboardData?.getData('text/plain') ?? '';
    });

    editor.addEventListener('beforeinput', (event) => {
      const activeTextHandle = options.getActiveTextHandle();
      const activeEditorWindow = options.getActiveEditorWindow();
      if (!(event instanceof InputEvent) || event.inputType !== 'insertFromPaste' || activeTextHandle === null) {
        return;
      }
      const handleKey = activeTextHandle.toString();
      if (
        pendingTextMutationBatch !== null &&
        pendingTextMutationBatch.handle === handleKey &&
        pendingTextMutationBatch.docStart === activeEditorWindow.docStart &&
        pendingTextMutationBatch.kind !== 'paste'
      ) {
        flushPendingTextMutationsToRuntime();
      }
      const selectionStart = editor.selectionStart ?? editor.value.length;
      const selectionEnd = editor.selectionEnd ?? selectionStart;
      pendingPasteInput = {
        handle: handleKey,
        docStart: activeEditorWindow.docStart,
        selectionStart,
        selectionEnd,
        text: typeof event.data === 'string' ? event.data : pendingPasteText,
      };
      pendingPasteText = '';
    });

    editor.addEventListener('compositionstart', () => {
      hiddenInputIsComposing = true;
    });

    editor.addEventListener('input', (event) => {
      const activeTextHandle = options.getActiveTextHandle();
      const activeEditorWindow = options.getActiveEditorWindow();
      if (hiddenInputIsComposing) {
        return;
      }
      if (event instanceof InputEvent && event.inputType === 'insertText' && restorePendingTypingState(editor)) {
        return;
      }
      if (event instanceof InputEvent && event.inputType === 'insertFromPaste') {
        const pasteInput = pendingPasteInput;
        pendingPasteInput = null;
        pendingPasteText = '';
        if (
          pasteInput !== null &&
          activeTextHandle !== null &&
          activeTextHandle.toString() === pasteInput.handle &&
          activeEditorWindow.docStart === pasteInput.docStart
        ) {
          const pendingBatch = pendingTextMutationBatch !== null &&
            pendingTextMutationBatch.handle === pasteInput.handle &&
            pendingTextMutationBatch.docStart === pasteInput.docStart &&
            pendingTextMutationBatch.kind === 'paste'
            ? pendingTextMutationBatch
            : null;
          const baseWindowText = pendingBatch?.baseWindowText ?? activeEditorWindow.text;
          let selectionStart = pasteInput.selectionStart;
          let selectionEnd = pasteInput.selectionEnd;
          if (pendingBatch !== null) {
            selectionStart = mapPendingBatchCurrentIndexToBaseIndex(pendingBatch, selectionStart);
            selectionEnd = mapPendingBatchCurrentIndexToBaseIndex(pendingBatch, selectionEnd);
          }
          const rangeStart = Math.min(selectionStart, selectionEnd);
          const rangeEnd = Math.max(selectionStart, selectionEnd);
          const nextText =
            `${baseWindowText.slice(0, rangeStart)}${pasteInput.text}${baseWindowText.slice(rangeEnd)}`;
          const nextCaret = rangeStart + pasteInput.text.length;
          if (editor.value !== nextText) {
            editor.value = nextText;
          }
          editor.setSelectionRange(nextCaret, nextCaret, 'none');
          commitReplacementEdit(
            baseWindowText,
            nextText,
            {
              start: rangeStart,
              end: rangeEnd,
              insertedText: pasteInput.text,
            },
            nextCaret,
            'paste',
            pendingBatch !== null,
          );
          return;
        }
      }
      pendingPasteInput = null;
      pendingPasteText = '';
      commitImeEdit(editor.value, editor.selectionStart ?? editor.value.length, 'typing');
    });

    editor.addEventListener('compositionend', () => {
      hiddenInputIsComposing = false;
      pendingPasteInput = null;
      pendingPasteText = '';
      commitImeEdit(editor.value, editor.selectionStart ?? editor.value.length, 'typing');
    });
  };

  return {
    applyActiveTextDeletion,
    attachHiddenEditorListeners,
    clearPendingTextMutations,
    flushPendingTextMutationsToRuntime,
    hasPendingTextMutations: () => pendingTextMutationBatch !== null,
    materializePendingTextMutations,
    reset: () => {
      hiddenInputIsComposing = false;
      clearPendingTextMutations();
    },
  };
}
