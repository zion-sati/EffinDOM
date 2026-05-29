import type { BridgeRuntime } from '../../core-types';
import type { BridgeInteractionState } from '../local-types';
import { DesktopFindDialogController } from '../find-dialog';
import { computeModifiers } from '../utils/encoding';
import { writeUtf8ToHeap } from '../utils/heap';

const UI_KEY_EVENT_DOWN = 1;
const UI_KEY_EVENT_UP = 2;

function currentInteractionTimeMs(): bigint {
  return BigInt(Math.floor(performance.now()));
}

function isVerticalCanvasNavigationKey(key: string): boolean {
  return key === 'ArrowUp' ||
    key === 'ArrowDown' ||
    key === 'PageUp' ||
    key === 'PageDown' ||
    key === 'Home' ||
    key === 'End';
}

function isTextNavigationKey(key: string): boolean {
  return key === 'ArrowLeft' ||
    key === 'ArrowRight' ||
    key === 'ArrowUp' ||
    key === 'ArrowDown' ||
    key === 'PageUp' ||
    key === 'PageDown' ||
    key === 'Home' ||
    key === 'End';
}

export function installKeyAndWindowHandlers(
  runtime: BridgeRuntime,
  interactionState: BridgeInteractionState,
  desktopFindDialog: DesktopFindDialogController,
): () => void {
  const { ui } = runtime;

  const forwardKeyEvent = (type: number) => (event: KeyboardEvent): void => {
    if (desktopFindDialog.consumeGlobalKeyEvent(event, type === UI_KEY_EVENT_DOWN ? 'down' : 'up')) {
      return;
    }
    const modifiers = computeModifiers(event);
    const activeTextHandle = interactionState.getActiveTextHandle();
    const activeTextEditable = interactionState.getActiveTextEditable();
    const activeTextMultiline = interactionState.getActiveTextMultiline();
    const activeTextInputFocused = interactionState.isActiveTextInputFocused();
    const activeEditableTextOwnsNativeEditingKey =
      activeTextHandle !== null &&
      activeTextEditable &&
      activeTextInputFocused &&
      !event.ctrlKey &&
      !event.metaKey &&
      !event.altKey &&
      (
        event.key.length === 1 ||
        (activeTextMultiline && event.key === 'Enter') ||
        event.key === 'Backspace' ||
        event.key === 'Delete'
      );
    const activeEditableTextNeedsRuntimeEditingFallback =
      activeTextHandle !== null &&
      activeTextEditable &&
      !activeTextInputFocused &&
      !event.ctrlKey &&
      !event.metaKey &&
      !event.altKey &&
      (
        event.key.length === 1 ||
        (activeTextMultiline && event.key === 'Enter') ||
        event.key === 'Backspace' ||
        event.key === 'Delete'
      );
    if (activeEditableTextOwnsNativeEditingKey) {
      if (
        type === UI_KEY_EVENT_DOWN &&
        (event.key === 'Backspace' || event.key === 'Delete') &&
        interactionState.applyActiveTextDeletion(event.key === 'Delete')
      ) {
        event.preventDefault();
      }
      return;
    }
    const activeTextOwnsRuntimeNavigationKey =
      activeTextHandle !== null &&
      isTextNavigationKey(event.key);
    const activeReadonlyTextOwnsRuntimeKey =
      activeTextHandle !== null &&
      !activeTextEditable &&
      (
        event.key.length === 1 ||
        event.key === 'Backspace' ||
        event.key === 'Delete' ||
        isTextNavigationKey(event.key)
      );
    if (
      activeEditableTextNeedsRuntimeEditingFallback ||
      activeTextOwnsRuntimeNavigationKey ||
      activeReadonlyTextOwnsRuntimeKey
    ) {
      event.preventDefault();
    }
    ui._ui_set_interaction_time(currentInteractionTimeMs());
    const shouldAlwaysPreventDefault =
      event.key === 'Tab' ||
      ((event.ctrlKey || event.metaKey) &&
        (
          event.key === 'c' || event.key === 'C' ||
          event.key === 'a' || event.key === 'A' ||
          event.key === 'x' || event.key === 'X' ||
          event.key === 'v' || event.key === 'V' ||
          event.key === 'z' || event.key === 'Z' ||
          event.key === 'y' || event.key === 'Y'
        ));
    if (shouldAlwaysPreventDefault) {
      event.preventDefault();
    }
    const heapString = writeUtf8ToHeap(ui, event.key);
    let callbackHandled = false;
    try {
      ui._ui_on_key_event(type, heapString.ptr, heapString.len, modifiers);
      if (
        !activeEditableTextNeedsRuntimeEditingFallback &&
        !activeTextOwnsRuntimeNavigationKey &&
        !activeReadonlyTextOwnsRuntimeKey
      ) {
        callbackHandled = window.__effindomCallbacks?.onKeyEventWithKey?.(type, event.key, modifiers) === true;
      }
    } finally {
      heapString.dispose();
    }
    if (
      type === UI_KEY_EVENT_DOWN &&
      (
        callbackHandled ||
        (!event.ctrlKey && !event.altKey && !event.metaKey && isVerticalCanvasNavigationKey(event.key))
      )
    ) {
      event.preventDefault();
    }
    runtime.commitFrame();
    if (interactionState.getActiveTextHandle() !== null && !interactionState.isActiveTextInputFocused()) {
      interactionState.refocusActiveTextInput();
    }
  };

  const handleKeyDown = forwardKeyEvent(UI_KEY_EVENT_DOWN);
  const handleKeyUp = forwardKeyEvent(UI_KEY_EVENT_UP);
  const keyListenerCapture = true;
  const reconcileFindSelection = (): void => {
    requestAnimationFrame(() => {
      runtime.syncFindSelection(true);
    });
  };
  const handleWindowBlur = (): void => {
    if (interactionState.getActiveTextHandle() === null) {
      reconcileFindSelection();
      return;
    }
    ui._ui_request_focus(0n);
    runtime.commitFrame();
    reconcileFindSelection();
  };
  const handleWindowFocus = (): void => {
    reconcileFindSelection();
  };
  const handleResize = (): void => {
    runtime.updateCanvasSize();
    runtime.commitFrame();
  };

  window.addEventListener('keydown', handleKeyDown, keyListenerCapture);
  window.addEventListener('keyup', handleKeyUp, keyListenerCapture);
  window.addEventListener('blur', handleWindowBlur);
  window.addEventListener('focus', handleWindowFocus);
  window.addEventListener('resize', handleResize);

  return () => {
    window.removeEventListener('keydown', handleKeyDown, keyListenerCapture);
    window.removeEventListener('keyup', handleKeyUp, keyListenerCapture);
    window.removeEventListener('blur', handleWindowBlur);
    window.removeEventListener('focus', handleWindowFocus);
    window.removeEventListener('resize', handleResize);
  };
}
