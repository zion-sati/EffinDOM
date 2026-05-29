import type { BridgeRuntime } from '../../core-types';
import type { BridgeInteractionState } from '../local-types';
import { commitIfVisualWork } from '../commit-policy';
import { PointerMoveCoalescer } from '../pointer-move-coalescer';
import { PULL_TO_REFRESH_THRESHOLD, type PullToRefreshOverlay } from '../pull-to-refresh';
import {
  getPointerPosition,
  isPointerInsideCanvas,
  normalizeWheelDelta,
} from './canvas-geometry';
import { findSemanticTextboxHandleAtPoint } from './semantic-hit-testing';
import {
  type TouchGestureState,
  transitionTouchGesture,
} from '../touch-gesture';
import { computeModifiers, handleToBigInt } from '../utils/encoding';

const UI_EVENT_POINTER_DOWN = 1;
const UI_EVENT_POINTER_UP = 2;
const UI_EVENT_POINTER_MOVE = 3;
const UI_EVENT_POINTER_LEAVE = 5;
const EDGE_AUTOSCROLL_THRESHOLD = 30;
const TOUCH_SCROLL_THRESHOLD = 8;
const TOUCH_AXIS_LOCK_DOMINANCE_RATIO = 1.25;
const TOUCH_AXIS_BREAKOUT_DISTANCE = 32;
const TOUCH_AXIS_BREAKOUT_RATIO = 0.72;
const TOUCH_AXIS_BREAKOUT_STEP_THRESHOLD = 8;

interface PendingPointerMove {
  handle: bigint;
  x: number;
  y: number;
  clientX: number;
  clientY: number;
  pointerInsideCanvas: boolean;
  modifiers: number;
}

function resolvePrimaryTouchAxis(deltaX: number, deltaY: number): 'x' | 'y' {
  const absX = Math.abs(deltaX);
  const absY = Math.abs(deltaY);
  if (absX >= absY * TOUCH_AXIS_LOCK_DOMINANCE_RATIO) {
    return 'x';
  }
  if (absY >= absX * TOUCH_AXIS_LOCK_DOMINANCE_RATIO) {
    return 'y';
  }
  return absX >= absY ? 'x' : 'y';
}

function shouldUnlockTouchAxis(axisMode: 'x' | 'y' | 'xy' | null, travelX: number, travelY: number): boolean {
  if (axisMode === null || axisMode === 'xy') {
    return false;
  }
  const primaryTravel = axisMode === 'x' ? travelX : travelY;
  const secondaryTravel = axisMode === 'x' ? travelY : travelX;
  if (primaryTravel < TOUCH_AXIS_BREAKOUT_DISTANCE || secondaryTravel < TOUCH_AXIS_BREAKOUT_DISTANCE) {
    return false;
  }
  return secondaryTravel >= TOUCH_AXIS_BREAKOUT_DISTANCE &&
    secondaryTravel >= primaryTravel * TOUCH_AXIS_BREAKOUT_RATIO;
}

function currentInteractionTimeMs(): bigint {
  return BigInt(Math.floor(performance.now()));
}

function semanticRoleAtHandle(runtime: BridgeRuntime, handle: bigint): string | null {
  const handleKey = handle.toString();
  const node = runtime.getSemanticTree().find((entry) => entry.handle === handleKey);
  return node?.roleName ?? null;
}

export function installPointerHandlers(
  runtime: BridgeRuntime,
  interactionState: BridgeInteractionState,
  pullToRefresh: PullToRefreshOverlay,
): () => void {
  const { canvas, ui } = runtime;
  let primaryPointerDown = false;
  let activePrimaryPointerId: number | null = null;
  let suppressedContextMenuPointerId: number | null = null;
  let edgeAutoScrollTickScheduled = false;
  let activePrimaryPointerType: string | null = null;
  let activeTouchGesture: TouchGestureState | null = null;
  let touchGestureBreakoutTravel = { x: 0.0, y: 0.0 };

  const applySelectionAutoScroll = (x: number, y: number): void => {
    if (!primaryPointerDown || activePrimaryPointerType === 'touch') {
      return;
    }
    if (handleToBigInt(ui._ui_selection_autoscroll(x, y, EDGE_AUTOSCROLL_THRESHOLD)) === 0n) {
      return;
    }
    runtime.flushPendingCommit();
    runtime.requestFrame();
  };

  const processPointerMove = (pending: PendingPointerMove): void => {
    interactionState.setPointerInsideCanvas(pending.pointerInsideCanvas);
    interactionState.setLastPointerClientPosition(pending.clientX, pending.clientY);
    interactionState.setLastPointerPosition(pending.x, pending.y);
    interactionState.setLastPointerModifiers(pending.modifiers);
    ui._ui_set_interaction_time(currentInteractionTimeMs());
    ui._ui_on_pointer_event(UI_EVENT_POINTER_MOVE, pending.handle, pending.x, pending.y);
    commitIfVisualWork(runtime);
    if (pending.handle === 0n) {
      const appCapturedHandle = runtime.getCapturedPointerHandle();
      if (appCapturedHandle !== null) {
        window.__effindomCallbacks?.onPointerEventWithCoords?.(
          UI_EVENT_POINTER_MOVE,
          appCapturedHandle,
          pending.x,
          pending.y,
          pending.modifiers,
        );
      }
    }
    applySelectionAutoScroll(pending.x, pending.y);
    scheduleEdgeAutoScrollTick();
  };
  const pointerMoveCoalescer = new PointerMoveCoalescer<PendingPointerMove>(processPointerMove);

  const scheduleEdgeAutoScrollTick = (): void => {
    if (edgeAutoScrollTickScheduled || !primaryPointerDown || activePrimaryPointerType === 'touch') {
      return;
    }
    edgeAutoScrollTickScheduled = true;
    requestAnimationFrame(() => {
      edgeAutoScrollTickScheduled = false;
      if (!primaryPointerDown) {
        return;
      }
      const { x, y } = interactionState.getLastPointerPosition();
      if (handleToBigInt(ui._ui_selection_autoscroll(x, y, EDGE_AUTOSCROLL_THRESHOLD)) === 0n) {
        return;
      }
      runtime.commitFrame();
      runtime.flushPendingCommit();
      runtime.requestFrame();
      scheduleEdgeAutoScrollTick();
    });
  };

  const releaseCanvasPointerCapture = (pointerId: number): void => {
    if (canvas.hasPointerCapture(pointerId)) {
      canvas.releasePointerCapture(pointerId);
    }
  };

  const captureCanvasPointer = (pointerId: number): void => {
    try {
      canvas.setPointerCapture(pointerId);
    } catch {
      // Synthetic touch events in tests and some browser/device combinations can reject explicit capture.
    }
  };

  const cancelPressedPointerInteraction = (x: number, y: number): void => {
    const capturedHandle = interactionState.getCapturedPointerHandle();
    interactionState.setCapturedPointerHandle(null);
    primaryPointerDown = false;
    activePrimaryPointerId = null;
    activePrimaryPointerType = null;
    pointerMoveCoalescer.clear();
    ui._ui_set_interaction_time(currentInteractionTimeMs());
    ui._ui_on_pointer_event(UI_EVENT_POINTER_LEAVE, capturedHandle ?? 0n, x, y);
    runtime.commitFrame();
  };

  const handleTouchPointerScroll = (
    event: PointerEvent,
    position: { readonly x: number; readonly y: number },
    modifiers: number,
  ): boolean => {
    if (event.pointerType !== 'touch' || activeTouchGesture?.pointerId !== event.pointerId) {
      return false;
    }

    const deltaFromStartX = position.x - activeTouchGesture.startX;
    const deltaFromStartY = position.y - activeTouchGesture.startY;
    const distanceSquared = (deltaFromStartX * deltaFromStartX) + (deltaFromStartY * deltaFromStartY);

    if (activeTouchGesture.phase === 'pressed') {
      if (distanceSquared < TOUCH_SCROLL_THRESHOLD * TOUCH_SCROLL_THRESHOLD) {
        event.preventDefault();
        return true;
      }
      const primaryAxis = resolvePrimaryTouchAxis(deltaFromStartX, deltaFromStartY);
      activeTouchGesture = transitionTouchGesture(activeTouchGesture, {
        type: 'scroll-threshold-crossed',
        axis: primaryAxis,
      });
      if (activeTouchGesture === null) {
        return true;
      }
      interactionState.cancelTouchTextFocusDeferral();
      cancelPressedPointerInteraction(position.x, position.y);
      ui._ui_touch_scroll_begin(
        runtime.getHandleFromPoint(activeTouchGesture.startX, activeTouchGesture.startY),
        activeTouchGesture.startX,
        activeTouchGesture.startY,
      );
      touchGestureBreakoutTravel = { x: 0.0, y: 0.0 };
    }

    const prevState = activeTouchGesture;
    activeTouchGesture = transitionTouchGesture(activeTouchGesture, {
      type: 'move',
      x: position.x,
      y: position.y,
    });
    if (activeTouchGesture === null) {
      return true;
    }

    const deltaX = prevState.lastX - position.x;
    const deltaY = prevState.lastY - position.y;
    const absDeltaX = Math.abs(deltaX);
    const absDeltaY = Math.abs(deltaY);

    if (absDeltaX >= TOUCH_AXIS_BREAKOUT_STEP_THRESHOLD && absDeltaY >= TOUCH_AXIS_BREAKOUT_STEP_THRESHOLD) {
      touchGestureBreakoutTravel.x += absDeltaX;
      touchGestureBreakoutTravel.y += absDeltaY;
    } else {
      touchGestureBreakoutTravel = { x: 0.0, y: 0.0 };
    }

    if (shouldUnlockTouchAxis(
      activeTouchGesture.axisMode,
      touchGestureBreakoutTravel.x,
      touchGestureBreakoutTravel.y,
    )) {
      activeTouchGesture = transitionTouchGesture(activeTouchGesture, {
        type: 'axis-unlocked',
      });
      if (activeTouchGesture === null) {
        return true;
      }
      touchGestureBreakoutTravel = { x: 0.0, y: 0.0 };
    }

    const scrollDeltaX = activeTouchGesture.axisMode === 'x' || activeTouchGesture.axisMode === 'xy' ? deltaX : 0.0;
    const scrollDeltaY = activeTouchGesture.axisMode === 'y' || activeTouchGesture.axisMode === 'xy' ? deltaY : 0.0;

    if (!activeTouchGesture.startedOnTextbox && !activeTouchGesture.pullToRefreshCaptured) {
      const canCapturePullToRefresh =
        activeTouchGesture.pullToRefreshEligible &&
        deltaFromStartY > 0.0 &&
        ui._ui_touch_scroll_allows_pull_to_refresh() !== 0;
      if (canCapturePullToRefresh) {
        activeTouchGesture = transitionTouchGesture(activeTouchGesture, {
          type: 'pull-to-refresh-captured',
        }) ?? activeTouchGesture;
      }
    }

    const appliedScrollDeltaY = activeTouchGesture.pullToRefreshCaptured ? 0.0 : scrollDeltaY;

    interactionState.setPointerInsideCanvas(isPointerInsideCanvas(canvas, event));
    interactionState.setLastPointerClientPosition(event.clientX, event.clientY);
    interactionState.setLastPointerPosition(position.x, position.y);
    interactionState.setLastPointerModifiers(modifiers);
    ui._ui_set_interaction_time(currentInteractionTimeMs());
    ui._ui_on_pointer_event(UI_EVENT_POINTER_MOVE, runtime.getHandleFromPoint(position.x, position.y), position.x, position.y);
    ui._ui_touch_scroll_update(scrollDeltaX, appliedScrollDeltaY);

    const pullToRefreshDistance = activeTouchGesture.pullToRefreshCaptured
      ? Math.max(0.0, deltaFromStartY)
      : 0.0;

    activeTouchGesture.pullToRefreshDistance = pullToRefreshDistance;

    if (activeTouchGesture.pullToRefreshCaptured) {
      pullToRefresh.show(pullToRefreshDistance);
    } else {
      pullToRefresh.hide();
    }
    runtime.commitFrame();
    event.preventDefault();
    return true;
  };

  const forwardPointerEvent = (type: number, useHitTest = true) => (event: PointerEvent): void => {
    const modifiers = computeModifiers(event);
    const pointerInsideCanvas = type === UI_EVENT_POINTER_LEAVE ? false : isPointerInsideCanvas(canvas, event);
    const position = getPointerPosition(canvas, event);
    if (event.pointerType === 'touch' && event.cancelable) {
      event.preventDefault();
    }
    if (type === UI_EVENT_POINTER_DOWN && event.button === 2) {
      window.__effindomCallbacks?.onBeforeContextMenuHitTest?.();
      const handle = runtime.getHandleFromPoint(position.x, position.y);
      const activeTextHandle = interactionState.getActiveTextHandle();
      const refocusActiveTextInputAfterContextMenu =
        activeTextHandle !== null &&
        handle === activeTextHandle;
      suppressedContextMenuPointerId = event.pointerId;
      interactionState.setPointerInsideCanvas(pointerInsideCanvas);
      interactionState.setLastPointerClientPosition(event.clientX, event.clientY);
      interactionState.setLastPointerPosition(position.x, position.y);
      interactionState.setLastPointerModifiers(modifiers);
      if (!refocusActiveTextInputAfterContextMenu) {
        canvas.focus({ preventScroll: true });
      }
      ui._ui_set_interaction_time(currentInteractionTimeMs());
      window.__effindomCallbacks?.onContextMenu?.(
        handle,
        position.x,
        position.y,
      );
      if (refocusActiveTextInputAfterContextMenu) {
        interactionState.refocusActiveTextInput();
      }
      event.preventDefault();
      return;
    }
    if (suppressedContextMenuPointerId === event.pointerId) {
      interactionState.setPointerInsideCanvas(pointerInsideCanvas);
      interactionState.setLastPointerClientPosition(event.clientX, event.clientY);
      interactionState.setLastPointerPosition(position.x, position.y);
      interactionState.setLastPointerModifiers(modifiers);
      if (type === UI_EVENT_POINTER_UP || type === UI_EVENT_POINTER_LEAVE) {
        suppressedContextMenuPointerId = null;
      }
      event.preventDefault();
      return;
    }
    const isTouchEvent = event.pointerType === 'touch';
    const isPointerCancel = event.type === 'pointercancel';
    let touchTapCandidateHandle: bigint | null = null;
    let touchTapDiscarded = false;

    if (type === UI_EVENT_POINTER_DOWN) {
      captureCanvasPointer(event.pointerId);
      primaryPointerDown = true;
      activePrimaryPointerId = event.pointerId;
      activePrimaryPointerType = event.pointerType;
      if (isTouchEvent) {
        ui._ui_clear_momentum_scroll();

        activeTouchGesture = transitionTouchGesture(null, {
          type: 'press-start',
          pointerId: event.pointerId,
          x: position.x,
          y: position.y,
          startedOnTextbox: false,
          pendingTextHandle: null,
        });
        touchGestureBreakoutTravel = { x: 0.0, y: 0.0 };
      }
    } else if (activeTouchGesture !== null && activeTouchGesture.pointerId === event.pointerId) {
      if (type === UI_EVENT_POINTER_MOVE && handleTouchPointerScroll(event, position, modifiers)) {
        return;
      }
      if (type === UI_EVENT_POINTER_UP || type === UI_EVENT_POINTER_LEAVE) {
        const pendingTapTextHandle = activeTouchGesture.pendingTapTextHandle;
        const wasCancelled = isPointerCancel || type === UI_EVENT_POINTER_LEAVE;
        const scrolling = activeTouchGesture.phase === 'scrolling';
        const pullToRefreshDistance = activeTouchGesture.pullToRefreshDistance;

        activeTouchGesture = transitionTouchGesture(activeTouchGesture, {
          type: wasCancelled ? 'cancel' : 'ended',
          triggered: false,
        });

        const triggerRefresh = scrolling && pullToRefreshDistance >= PULL_TO_REFRESH_THRESHOLD;
        if (scrolling) {
          interactionState.cancelTouchTextFocusDeferral();
          interactionState.setCapturedPointerHandle(null);
          primaryPointerDown = false;
          activePrimaryPointerId = null;
          activePrimaryPointerType = null;
          pointerMoveCoalescer.clear();
          ui._ui_touch_scroll_end();
          if (triggerRefresh) {
            pullToRefresh.hide(true);
            window.location.reload();
          } else {
            pullToRefresh.hide();
          }
          releaseCanvasPointerCapture(event.pointerId);
          event.preventDefault();
          return;
        }
        if (wasCancelled || type !== UI_EVENT_POINTER_UP) {
          interactionState.cancelTouchTextFocusDeferral();
          touchTapDiscarded = true;
        } else {
          touchTapCandidateHandle = pendingTapTextHandle;
        }
        pullToRefresh.hide(true);
      }
    }
    const capturedHandle = interactionState.getCapturedPointerHandle();
    const activeTextHandle = interactionState.getActiveTextHandle();
    const rawHitHandle = useHitTest ? runtime.getHandleFromPoint(position.x, position.y) : 0n;
    const semanticTextboxHandle =
      (type === UI_EVENT_POINTER_DOWN || activeTextHandle !== null) && useHitTest
        ? findSemanticTextboxHandleAtPoint(runtime, position.x, position.y)
        : 0n;
    const shouldPreferSemanticTextbox =
      semanticTextboxHandle !== 0n &&
      (rawHitHandle === 0n || semanticRoleAtHandle(runtime, rawHitHandle) !== 'textbox') &&
      (type === UI_EVENT_POINTER_DOWN ||
        semanticTextboxHandle === activeTextHandle ||
        semanticTextboxHandle === capturedHandle);
    const hitHandle = shouldPreferSemanticTextbox ? semanticTextboxHandle : rawHitHandle;
    const handle = type === UI_EVENT_POINTER_DOWN
      ? hitHandle
      : ((useHitTest && pointerInsideCanvas) ? hitHandle : (capturedHandle ?? hitHandle));
    const refocusActiveTextInputAfterPointerDown =
      type === UI_EVENT_POINTER_DOWN &&
      activeTextHandle !== null &&
      handle === activeTextHandle &&
      !isTouchEvent;
    const delayCanvasFocusUntilAfterPointerDown =
      type === UI_EVENT_POINTER_DOWN &&
      activeTextHandle !== null &&
      handle !== activeTextHandle;
    const keepTouchEditorFocusedOnPointerDown =
      type === UI_EVENT_POINTER_DOWN &&
      isTouchEvent &&
      activeTextHandle !== null &&
      handle === activeTextHandle &&
      interactionState.isActiveTextInputFocused();
    const refocusActiveTextInputAfterPointerUp =
      type === UI_EVENT_POINTER_UP &&
      activeTextHandle !== null &&
      handle === activeTextHandle &&
      !isTouchEvent;
    const shouldCommitDeferredTouchFocus =
      isTouchEvent &&
      type === UI_EVENT_POINTER_UP &&
      !touchTapDiscarded &&
      touchTapCandidateHandle !== null &&
      handle === touchTapCandidateHandle;
    if (type === UI_EVENT_POINTER_UP || type === UI_EVENT_POINTER_LEAVE) {
      const pending = pointerMoveCoalescer.takePending();
      if (pending !== null) {
        processPointerMove(pending);
      }
    }
    if (type === UI_EVENT_POINTER_DOWN) {
      if (isTouchEvent) {
        const touchDownTextboxHandle = semanticRoleAtHandle(runtime, handle) === 'textbox' ? handle : 0n;
        if (touchDownTextboxHandle !== 0n) {
          const touchDownOnAlreadyFocusedText =
            activeTextHandle !== null &&
            touchDownTextboxHandle === activeTextHandle &&
            interactionState.isActiveTextInputFocused();
          if (activeTouchGesture !== null) {
            activeTouchGesture.startedOnTextbox = true;
            activeTouchGesture.pendingTapTextHandle = touchDownOnAlreadyFocusedText ? null : touchDownTextboxHandle;
          }
          interactionState.beginTouchTextFocusDeferral(touchDownTextboxHandle);
        } else {
          interactionState.cancelTouchTextFocusDeferral();
        }
      }
      interactionState.setPointerInsideCanvas(pointerInsideCanvas);
      interactionState.setLastPointerClientPosition(event.clientX, event.clientY);
      interactionState.setLastPointerPosition(position.x, position.y);
      interactionState.setLastPointerModifiers(modifiers);
      if (!refocusActiveTextInputAfterPointerDown &&
        !delayCanvasFocusUntilAfterPointerDown &&
        !keepTouchEditorFocusedOnPointerDown) {
        canvas.focus({ preventScroll: true });
      }
      interactionState.setCapturedPointerHandle(handle === 0n ? null : handle);
      ui._ui_set_interaction_time(currentInteractionTimeMs());
      ui._ui_on_pointer_event(type, handle, position.x, position.y);
      runtime.commitFrame();
      if (delayCanvasFocusUntilAfterPointerDown && interactionState.getActiveTextHandle() === null) {
        canvas.focus({ preventScroll: true });
      }
      if (refocusActiveTextInputAfterPointerDown) {
        interactionState.refocusActiveTextInput();
      } else if (!isTouchEvent && interactionState.getActiveTextHandle() !== null && !interactionState.isActiveTextInputFocused()) {
        interactionState.refocusActiveTextInput();
      }
      scheduleEdgeAutoScrollTick();
    } else if (type === UI_EVENT_POINTER_MOVE) {
      pointerMoveCoalescer.enqueue({
        handle,
        x: position.x,
        y: position.y,
        clientX: event.clientX,
        clientY: event.clientY,
        pointerInsideCanvas,
        modifiers,
      });
      return;
    } else {
      interactionState.setPointerInsideCanvas(pointerInsideCanvas);
      interactionState.setLastPointerClientPosition(event.clientX, event.clientY);
      interactionState.setLastPointerPosition(position.x, position.y);
      interactionState.setLastPointerModifiers(modifiers);
      ui._ui_set_interaction_time(currentInteractionTimeMs());
      ui._ui_on_pointer_event(type, handle, position.x, position.y);
      runtime.commitFrame();
      if (shouldCommitDeferredTouchFocus) {
        interactionState.commitTouchTextFocusDeferral(handle);
      } else if (refocusActiveTextInputAfterPointerUp) {
        interactionState.refocusActiveTextInput();
      } else if (!isTouchEvent && interactionState.getActiveTextHandle() !== null && !interactionState.isActiveTextInputFocused()) {
        interactionState.refocusActiveTextInput();
      }
      if (isTouchEvent && type === UI_EVENT_POINTER_UP && !shouldCommitDeferredTouchFocus) {
        interactionState.cancelTouchTextFocusDeferral();
      }
      if (handle === 0n) {
        const appCapturedHandle = runtime.getCapturedPointerHandle();
        if (appCapturedHandle !== null) {
          window.__effindomCallbacks?.onPointerEventWithCoords?.(type, appCapturedHandle, position.x, position.y, modifiers);
        }
      }
      scheduleEdgeAutoScrollTick();
    }
    if (type === UI_EVENT_POINTER_UP || type === UI_EVENT_POINTER_LEAVE) {
      primaryPointerDown = false;
      activePrimaryPointerId = null;
      activePrimaryPointerType = null;
      interactionState.setCapturedPointerHandle(null);
    }
    if (type === UI_EVENT_POINTER_UP || type === UI_EVENT_POINTER_LEAVE) {
      releaseCanvasPointerCapture(event.pointerId);
    }
  };

  const handleContextMenu = (event: Event): void => {
    event.preventDefault();
  };
  const handlePointerDown = forwardPointerEvent(UI_EVENT_POINTER_DOWN);
  const handlePointerUp = forwardPointerEvent(UI_EVENT_POINTER_UP);
  const handlePointerMove = forwardPointerEvent(UI_EVENT_POINTER_MOVE);
  const handleCapturedPointerExit = (event: PointerEvent): void => {
    if (canvas.hasPointerCapture(event.pointerId)) {
      if (primaryPointerDown) {
        forwardPointerEvent(UI_EVENT_POINTER_MOVE, false)(event);
      }
      return;
    }
    forwardPointerEvent(UI_EVENT_POINTER_LEAVE, false)(event);
  };
  const handlePointerLeave = (event: PointerEvent): void => {
    handleCapturedPointerExit(event);
  };
  const handlePointerOut = (event: PointerEvent): void => {
    handleCapturedPointerExit(event);
  };
  const handlePointerCancel = (event: PointerEvent): void => {
    forwardPointerEvent(UI_EVENT_POINTER_LEAVE, false)(event);
  };
  const shouldHandleWindowPointerEvent = (event: PointerEvent): boolean => {
    return primaryPointerDown &&
      activePrimaryPointerId !== null &&
      event.pointerId === activePrimaryPointerId &&
      event.target !== canvas;
  };
  const handleWindowPointerMove = (event: PointerEvent): void => {
    if (!shouldHandleWindowPointerEvent(event)) {
      return;
    }
    handlePointerMove(event);
  };
  const handleWindowPointerUp = (event: PointerEvent): void => {
    if (!shouldHandleWindowPointerEvent(event)) {
      return;
    }
    handlePointerUp(event);
  };
  const handleWindowPointerCancel = (event: PointerEvent): void => {
    if (!shouldHandleWindowPointerEvent(event)) {
      return;
    }
    handlePointerCancel(event);
  };
  const handleWheel = (event: WheelEvent): void => {
    event.preventDefault();
    const position = getPointerPosition(canvas, event);
    interactionState.setPointerInsideCanvas(isPointerInsideCanvas(canvas, event));
    interactionState.setLastPointerClientPosition(event.clientX, event.clientY);
    interactionState.setLastPointerPosition(position.x, position.y);
    interactionState.setLastPointerModifiers(computeModifiers(event));
    ui._ui_set_interaction_time(currentInteractionTimeMs());
    const handle = runtime.getHandleFromPoint(position.x, position.y);
    ui._ui_on_pointer_event(UI_EVENT_POINTER_MOVE, handle, position.x, position.y);
    const delta = normalizeWheelDelta(event, canvas);
    ui._ui_on_wheel_event(delta.x, delta.y);
    commitIfVisualWork(runtime);
  };

  canvas.addEventListener('contextmenu', handleContextMenu);
  canvas.addEventListener('pointerdown', handlePointerDown, { passive: false });
  canvas.addEventListener('pointerup', handlePointerUp, { passive: false });
  canvas.addEventListener('pointermove', handlePointerMove, { passive: false });
  canvas.addEventListener('pointerleave', handlePointerLeave, { passive: false });
  canvas.addEventListener('pointerout', handlePointerOut, { passive: false });
  canvas.addEventListener('pointercancel', handlePointerCancel, { passive: false });
  window.addEventListener('pointermove', handleWindowPointerMove, { passive: false });
  window.addEventListener('pointerup', handleWindowPointerUp, { passive: false });
  window.addEventListener('pointercancel', handleWindowPointerCancel, { passive: false });
  canvas.addEventListener('wheel', handleWheel, { passive: false });

  return () => {
    canvas.removeEventListener('contextmenu', handleContextMenu);
    canvas.removeEventListener('pointerdown', handlePointerDown);
    canvas.removeEventListener('pointerup', handlePointerUp);
    canvas.removeEventListener('pointermove', handlePointerMove);
    canvas.removeEventListener('pointerleave', handlePointerLeave);
    canvas.removeEventListener('pointerout', handlePointerOut);
    canvas.removeEventListener('pointercancel', handlePointerCancel);
    window.removeEventListener('pointermove', handleWindowPointerMove);
    window.removeEventListener('pointerup', handleWindowPointerUp);
    window.removeEventListener('pointercancel', handleWindowPointerCancel);
    canvas.removeEventListener('wheel', handleWheel);
  };
}
