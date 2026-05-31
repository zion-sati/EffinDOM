import type {
  OpenCanvasApi,
  OpenCanvasFindMatch,
  OpenCanvasFindState,
  OpenCanvasTextDocument,
  SemanticNode,
} from './open-canvas';

export type {
  OpenCanvasApi,
  OpenCanvasFindMatch,
  OpenCanvasFindOptions,
  OpenCanvasFindResults,
  OpenCanvasFindState,
  OpenCanvasHandle,
  OpenCanvasResolvedFindOptions,
  OpenCanvasTextDocument,
  SemanticBounds,
  SemanticNode,
  SemanticState,
} from './open-canvas';

export type WasmHandleLike =
  | number
  | bigint
  | string
  | {
    valueOf(): unknown;
    toString(): string;
  };

export interface AssetLoadResult {
  readonly width: number;
  readonly height: number;
}

export interface BridgeFontRegistration {
  readonly id: number;
  readonly url: string;
  readonly fallbackIds?: readonly number[];
}

export interface BridgeFontStackRegistration {
  readonly primary: BridgeFontRegistration;
  readonly fallbacks?: readonly BridgeFontRegistration[];
}

export interface CoreModule {
  HEAPU8: Uint8Array;
  HEAPU32: Uint32Array;
  wasmMemory?: WebAssembly.Memory;
  usesMemory64?: boolean;
  refreshHeapViews?(): void;
  locateFile?(path: string, prefix?: string): string;
  instantiateWasm?(
    imports: WebAssembly.Imports,
    successCallback: (instance: WebAssembly.Instance, module?: WebAssembly.Module) => void,
  ): object;
  onAbort?(what?: unknown): void;
  canvas?: HTMLCanvasElement;
  onRuntimeInitialized?: () => void;
  _malloc(size: number): WasmHandleLike;
  _free(ptr: WasmHandleLike): void;
  _ed_init(width: number, height: number, dpr: number): void;
  _ed_init_webgl(width: number, height: number, dpr: number): void;
  _ed_init_sw(width: number, height: number, dpr: number): void;
  _ed_resize(width: number, height: number, dpr: number): void;
  _ed_register_font(fontId: number, bytesPtr: WasmHandleLike, len: number): void;
  _ed_unregister_font(fontId: number): void;
  _ed_register_svg(svgId: number, bytesPtr: WasmHandleLike, len: number): void;
  _ed_execute_command_buffer(ptr: WasmHandleLike, length: number): void;
  _ed_register_texture_rgba(
    textureId: number,
    rgbaPtr: WasmHandleLike,
    width: number,
    height: number,
    byteLength: number,
  ): void;
  _ed_unregister_texture(textureId: number): void;
  _ed_reset_scene(): void;
  _ed_render_frame(currentTimeMs: number): void;
  _ed_clear_focus_state?(): void;
  _ed_clear_text_input_state?(): void;
  _ed_recover_device(): void;
  _ed_hit_test(x: number, y: number): WasmHandleLike;
  _ed_get_sw_framebuffer(): WasmHandleLike;
  _ed_get_backend_type(): number;
  _ed_get_device_state(): number;
  _ed_notify_webgl_context_lost?(): void;
  _ed_debug_simulate_device_lost?(): void;
}

export const EdBackendType = {
  NONE: 0,
  WEBGPU: 1,
  WEBGL2: 2,
  CPU: 3,
} as const;

export type EdBackendType = (typeof EdBackendType)[keyof typeof EdBackendType];

export const EdDeviceState = {
  OK: 0,
  LOST: 1,
  RECOVERING: 2,
} as const;

export type EdDeviceState = (typeof EdDeviceState)[keyof typeof EdDeviceState];

export interface UiModule {
  HEAPU8: Uint8Array;
  HEAPU32: Uint32Array;
  HEAPF32: Float32Array;
  wasmMemory?: WebAssembly.Memory;
  usesMemory64?: boolean;
  refreshHeapViews?(): void;
  _malloc(size: number): WasmHandleLike;
  _free(ptr: WasmHandleLike): void;
  _ui_reset(): void;
  _ui_arena_alloc(size: number): WasmHandleLike;
  _ui_register_icu_data(ptr: WasmHandleLike, len: number): void;
  _ui_create_node(type: number): WasmHandleLike;
  _ui_delete_node(handle: WasmHandleLike): void;
  _ui_node_add_child(parent: WasmHandleLike, child: WasmHandleLike): void;
  _ui_set_root(handle: WasmHandleLike): void;
  _ui_set_node_id(handle: WasmHandleLike, strPtr: WasmHandleLike, len: number): void;
  _ui_set_semantic_role(handle: WasmHandleLike, role: number): void;
  _ui_set_semantic_label(handle: WasmHandleLike, strPtr: WasmHandleLike, len: number): void;
  _ui_set_semantic_checked(handle: WasmHandleLike, checkedState: number): void;
  _ui_set_semantic_selected(handle: WasmHandleLike, hasSelected: number, selected: number): void;
  _ui_set_semantic_expanded(handle: WasmHandleLike, hasExpanded: number, expanded: number): void;
  _ui_set_semantic_disabled(handle: WasmHandleLike, hasDisabled: number, disabled: number): void;
  _ui_set_semantic_value_range(
    handle: WasmHandleLike,
    hasValueRange: number,
    valueNow: number,
    valueMin: number,
    valueMax: number,
  ): void;
  _ui_set_semantic_orientation(handle: WasmHandleLike, orientation: number): void;
  _ui_request_semantic_announcement(handle: WasmHandleLike): void;
  _ui_push_semantic_scope(handle: WasmHandleLike): number;
  _ui_remove_semantic_scope(token: number): void;
  _ui_set_width(handle: WasmHandleLike, value: number, unit: number): void;
  _ui_set_height(handle: WasmHandleLike, value: number, unit: number): void;
  _ui_set_fill_width(handle: WasmHandleLike, fill: number): void;
  _ui_set_fill_height(handle: WasmHandleLike, fill: number): void;
  _ui_set_flex_direction(handle: WasmHandleLike, direction: number): void;
  _ui_set_flex_basis(handle: WasmHandleLike, basis: number): void;
  _ui_set_justify_content(handle: WasmHandleLike, justify: number): void;
  _ui_set_align_items(handle: WasmHandleLike, align: number): void;
  _ui_set_padding(handle: WasmHandleLike, left: number, top: number, right: number, bottom: number): void;
  _ui_set_margin(handle: WasmHandleLike, left: number, top: number, right: number, bottom: number): void;
  _ui_set_position_type(handle: WasmHandleLike, positionType: number): void;
  _ui_set_position(handle: WasmHandleLike, left: number, top: number, right: number, bottom: number): void;
  _ui_node_remove_child(parent: WasmHandleLike, child: WasmHandleLike): void;
  _ui_set_is_portal(handle: WasmHandleLike, portal: number): void;
  _ui_set_is_shared_size_scope(handle: WasmHandleLike, isScope: number): void;
  _ui_grid_set_columns(handle: WasmHandleLike, count: number, valuesPtr: WasmHandleLike, typesPtr: WasmHandleLike): void;
  _ui_grid_set_rows(handle: WasmHandleLike, count: number, valuesPtr: WasmHandleLike, typesPtr: WasmHandleLike): void;
  _ui_grid_set_column_shared_size_group(handle: WasmHandleLike, index: number, strPtr: WasmHandleLike, len: number): void;
  _ui_grid_set_row_shared_size_group(handle: WasmHandleLike, index: number, strPtr: WasmHandleLike, len: number): void;
  _ui_node_set_grid_placement(handle: WasmHandleLike, row: number, col: number, rowSpan: number, colSpan: number): void;
  _ui_set_bg_color(handle: WasmHandleLike, color: number): void;
  _ui_set_box_style(
    handle: WasmHandleLike,
    bgColor: number,
    topLeftRadius: number,
    topRightRadius: number,
    bottomRightRadius: number,
    bottomLeftRadius: number,
    borderWidth: number,
    borderColor: number,
    borderStyle: number,
    borderDashOn: number,
    borderDashOff: number,
  ): void;
  _ui_set_drop_shadow(
    handle: WasmHandleLike,
    color: number,
    offsetX: number,
    offsetY: number,
    blurSigma: number,
    spread: number,
  ): void;
  _ui_set_layer_effect(handle: WasmHandleLike, opacity: number, blurSigma: number, blendMode: number): void;
  _ui_set_background_blur(handle: WasmHandleLike, blurSigma: number): void;
  _ui_set_image(handle: WasmHandleLike, textureId: number, objectFit: number): void;
  _ui_set_image_nine(
    handle: WasmHandleLike,
    textureId: number,
    insetLeft: number,
    insetTop: number,
    insetRight: number,
    insetBottom: number,
  ): void;
  _ui_set_svg(handle: WasmHandleLike, svgId: number, tintColor: number): void;
  _ui_set_linear_gradient(
    handle: WasmHandleLike,
    startX: number,
    startY: number,
    endX: number,
    endY: number,
    stopCount: number,
    offsetsPtr: WasmHandleLike,
    colorsPtr: WasmHandleLike,
  ): void;
  _ui_set_clip_to_bounds(handle: WasmHandleLike, clip: number): void;
  _ui_set_visibility(handle: WasmHandleLike, visibility: number): void;
  _ui_set_font(handle: WasmHandleLike, fontId: number, size: number): void;
  _ui_set_line_height(handle: WasmHandleLike, lineHeight: number): void;
  _ui_set_text(handle: WasmHandleLike, strPtr: WasmHandleLike, len: number): void;
  _ui_set_text_style_runs(handle: WasmHandleLike, runCount: number, runsWordsPtr: WasmHandleLike): void;
  _ui_set_text_color(handle: WasmHandleLike, color: number): void;
  _ui_set_text_align(handle: WasmHandleLike, align: number): void;
  _ui_set_text_vertical_align(handle: WasmHandleLike, align: number): void;
  _ui_set_text_limits(handle: WasmHandleLike, maxChars: number, maxLines: number): void;
  _ui_set_text_wrapping(handle: WasmHandleLike, wrap: number): void;
  _ui_set_text_overflow(handle: WasmHandleLike, overflow: number): void;
  _ui_set_text_overflow_fade(handle: WasmHandleLike, horizontal: number, vertical: number): void;
  _ui_set_text_obscured(handle: WasmHandleLike, obscured: number): void;
  _ui_set_scroll_offset(handle: WasmHandleLike, x: number, y: number): void;
  _ui_has_pending_visual_work(): number;
  _ui_needs_animation_frame(): number;
  _ui_has_pointer_autoscroll(): number;
  _ui_selection_autoscroll(x: number, y: number, edgeThreshold: number): WasmHandleLike;
  _ui_get_bounds(
    handle: WasmHandleLike,
    outX: WasmHandleLike,
    outY: WasmHandleLike,
    outWidth: WasmHandleLike,
    outHeight: WasmHandleLike,
  ): number;
  _ui_set_selectable(handle: WasmHandleLike, selectable: number, color: number): void;
  _ui_set_selection_area(handle: WasmHandleLike, isArea: number): void;
  _ui_set_selection_area_barrier(handle: WasmHandleLike, isBarrier: number): void;
  _ui_clear_selection(handle: WasmHandleLike): void;
  _ui_retarget_selection(fromHandle: WasmHandleLike, toHandle: WasmHandleLike): void;
  _ui_is_point_in_selection(x: number, y: number): number;
  _ui_set_text_selection_range(handle: WasmHandleLike, selectionStart: number, selectionEnd: number): void;
  _ui_get_text_scene_position_x(handle: WasmHandleLike, byteIndex: number): number;
  _ui_get_text_scene_position_y(handle: WasmHandleLike, byteIndex: number): number;
  _ui_get_text_snapshot_handle_count(): number;
  _ui_copy_text_snapshot_handles(outPtr: WasmHandleLike, maxHandleCount: number): number;
  _ui_set_text_find_match(handle: WasmHandleLike, start: number, end: number): number;
  _ui_clear_text_find_match(): void;
  _ui_push_text_find_highlight(handle: WasmHandleLike, start: number, end: number, color: number): number;
  _ui_clear_text_find_highlights(): void;
  _ui_get_text_document_utf8_length(handle: WasmHandleLike): number;
  _ui_copy_text_document_utf8(handle: WasmHandleLike, outPtr: WasmHandleLike, bufferLength: number): number;
  _ui_get_text_visible_bounds(
    handle: WasmHandleLike,
    outXPtr: WasmHandleLike,
    outYPtr: WasmHandleLike,
    outWidthPtr: WasmHandleLike,
    outHeightPtr: WasmHandleLike,
  ): number;
  _ui_get_text_range_rect_count(handle: WasmHandleLike, start: number, end: number): number;
  _ui_copy_text_range_rects(
    handle: WasmHandleLike,
    start: number,
    end: number,
    outPtr: WasmHandleLike,
    maxRectCount: number,
  ): number;
  _ui_reveal_text_range(handle: WasmHandleLike, start: number, end: number): number;
  _ui_clear_current_selection(): void;
  _ui_copy_current_selection(): void;
  _ui_can_undo_text_edit(handle: WasmHandleLike): number;
  _ui_can_redo_text_edit(handle: WasmHandleLike): number;
  _ui_has_text_selection(handle: WasmHandleLike): number;
  _ui_undo_text_edit(handle: WasmHandleLike): void;
  _ui_redo_text_edit(handle: WasmHandleLike): void;
  _ui_copy_text_selection(handle: WasmHandleLike): void;
  _ui_cut_text_selection(handle: WasmHandleLike): void;
  _ui_paste_text(handle: WasmHandleLike): void;
  _ui_select_all_text(handle: WasmHandleLike): void;
  _ui_set_interactive(handle: WasmHandleLike, interactive: number): void;
  _ui_set_scroll_proxy_target(handle: WasmHandleLike, scrollHandle: WasmHandleLike): void;
  _ui_set_scroll_enabled(handle: WasmHandleLike, enabledX: number, enabledY: number): void;
  _ui_set_show_scrollbars(handle: WasmHandleLike, showScrollbars: number): void;
  _ui_set_scroll_friction(handle: WasmHandleLike, friction: number): void;
  _ui_set_scroll_content_size(handle: WasmHandleLike, contentWidth: number, contentHeight: number): void;
  _ui_set_editable(handle: WasmHandleLike, editable: number): void;
  _ui_set_caret_color(handle: WasmHandleLike, color: number): void;
  _ui_set_focusable(handle: WasmHandleLike, focusable: number, tabIndex: number): void;
  _ui_request_focus(handle: WasmHandleLike): void;
  _ui_commit_frame(): void;
  _ui_get_command_buffer(outLenPtr: WasmHandleLike): WasmHandleLike;
  _ui_get_semantic_buffer(outLenPtr: WasmHandleLike): WasmHandleLike;
  _ui_resize_window(w: number, h: number): void;
  _ui_set_key_modifiers(modifiers: number): void;
  _ui_on_pointer_event(event: number, handle: WasmHandleLike, x: number, y: number): void;
  _ui_on_wheel_event(deltaX: number, deltaY: number): void;
  _ui_touch_scroll_begin(handle: WasmHandleLike, x: number, y: number): void;
  _ui_touch_scroll_update(deltaX: number, deltaY: number): void;
  _ui_touch_scroll_end(): void;
  _ui_clear_momentum_scroll(): void;
  _ui_touch_scroll_allows_pull_to_refresh(): number;
  _ui_set_coarse_pointer_mode(coarsePointerMode: number): void;
  _ui_set_platform_family(platformFamily: number): void;
  _ui_on_key_event(type: number, strPtr: WasmHandleLike, len: number, mods: number): void;
  _ui_on_ime_update(handle: WasmHandleLike, strPtr: WasmHandleLike, len: number, caretIdx: number): void;
  _ui_replace_text_range(
    handle: WasmHandleLike,
    startIdx: number,
    endIdx: number,
    strPtr: WasmHandleLike,
    len: number,
    caretIdx: number,
  ): void;
  _ui_on_paste_text(handle: WasmHandleLike, strPtr: WasmHandleLike, len: number): void;
  _ui_set_interaction_time(ms: WasmHandleLike): void;
  _ui_measure_text(
    strPtr: WasmHandleLike,
    len: number,
    fontId: number,
    size: number,
    maxWidth: number,
    outWidthPtr: WasmHandleLike,
    outHeightPtr: WasmHandleLike,
  ): void;
  _ui_register_font(id: number, bytesPtr: WasmHandleLike, len: number): number;
  _ui_register_font_fallback(fontId: number, fallbackFontId: number): void;
  _ui_unregister_font_fallback(fontId: number, fallbackFontId: number): number;
  _ui_unregister_font(fontId: number): number;
  _ui_font_loaded(fontId: number): void;
}

export interface PointerEventLog {
  readonly handle: string;
  readonly eventType: number;
}

export interface FocusEventLog {
  readonly handle: string;
  readonly isFocused: boolean;
}

export interface TextChangeLog {
  readonly handle: string;
  readonly text: string;
  readonly textLength?: number;
  readonly truncated?: boolean;
}

export interface SelectionChangeLog {
  readonly handle: string;
  readonly start: number;
  readonly end: number;
}

export interface CrossSelectionChangeLog {
  readonly areaHandle: string;
  readonly text: string;
}

export interface ScrollEventLog {
  readonly handle: string;
  readonly offsetX: number;
  readonly offsetY: number;
  readonly contentWidth: number;
  readonly contentHeight: number;
  readonly viewportWidth: number;
  readonly viewportHeight: number;
}

export interface MissingFontCoverageLog {
  readonly fontId: number;
  readonly coverageKind: number;
  readonly sampleText: string;
}

export interface IncrementalFontPackageRequestLog {
  readonly primaryFontId: number;
  readonly coverageKind: number;
  readonly packageId: string;
  readonly segmentIds: readonly string[];
  readonly sampleText: string;
}

export type IncrementalFontAutoGrowBlockReason =
  | 'auto-grow-disabled'
  | 'font-not-allowed'
  | 'package-blocked';

export interface IncrementalFontPolicy {
  readonly autoGrow: boolean;
  readonly maxCachedShardFonts: number;
  readonly allowedFontIds: readonly number[] | null;
  readonly blockedPackageIds: readonly string[] | null;
}

export interface IncrementalFontCacheState {
  readonly maxCachedShardFonts: number;
  readonly cachedShardCount: number;
  readonly cachedShardKeys: readonly string[];
  readonly evictedShardKeys: readonly string[];
}

export interface IncrementalFontRuntimeState {
  readonly fontId: number;
  readonly sourceUrl: string | null;
  readonly sourceState: 'unknown' | 'known' | 'loading' | 'loaded' | 'failed';
  readonly loaded: boolean;
  readonly requestedSegmentIds: readonly string[];
  readonly pendingSegmentIds: readonly string[];
  readonly appliedSegmentIds: readonly string[];
  readonly evictedSegmentIds: readonly string[];
  readonly revision: number;
  readonly autoGrowAllowed: boolean;
  readonly blockedPackageIds: readonly string[];
  readonly lastBlockedReason: IncrementalFontAutoGrowBlockReason | null;
}

export interface ClipboardRichTextPart {
  readonly text: string;
  readonly fontId?: number;
  readonly fontSize?: number;
  readonly color?: number;
  readonly bgColor?: number;
  readonly decorationFlags?: number;
  readonly fontUrl?: string;
}

export interface ClipboardRichTextPayload {
  readonly version: 1;
  readonly parts: readonly ClipboardRichTextPart[];
}

export interface ClipboardWritePayload {
  readonly plainText: string;
  readonly richText?: ClipboardRichTextPayload;
}

export interface BridgeLogs {
  readonly pointerEvents: PointerEventLog[];
  readonly focusEvents: FocusEventLog[];
  readonly textChanges: TextChangeLog[];
  readonly selectionChanges: SelectionChangeLog[];
  readonly crossSelectionChanges: CrossSelectionChangeLog[];
  readonly clipboardWrites: string[];
  readonly clipboardReadRequests: string[];
  readonly scrollEvents: ScrollEventLog[];
  readonly missingFontCoverageRequests: MissingFontCoverageLog[];
  readonly incrementalFontPackageRequests: IncrementalFontPackageRequestLog[];
}

export interface BridgeRuntime {
  readonly core: CoreModule;
  readonly ui: UiModule;
  readonly canvas: HTMLCanvasElement;
  readonly openCanvasApi: OpenCanvasApi;
  readonly logs: BridgeLogs;
  updateCanvasSize(): void;
  extractCommandBuffer(): Uint32Array;
  executeCommandBuffer(words: Uint32Array): void;
  syncCommandBufferToCore(): Uint32Array;
  flushPendingCommit(): Uint32Array | null;
  hasPendingCommit(): boolean;
  commitFrame(): void;
  requestFrame(): void;
  setFrameRequester(requester: (() => void) | null): void;
  getSemanticTree(): readonly SemanticNode[];
  getActiveTextHandle(): bigint | null;
  getCapturedPointerHandle(): bigint | null;
  setCapturedPointerHandle(handle: bigint | null): void;
  setAppFrameHandler(handler: ((timestampMs: number) => void) | null): void;
  runAppFrameHandler(timestampMs: number): void;
  uiHasPendingVisualWork(): boolean;
  uiNeedsAnimationFrame(): boolean;
  getHandleFromPoint(x: number, y: number): bigint;
  clearPointerHover(): void;
  refreshPointerHover(): void;
  getFindDocuments(): readonly OpenCanvasTextDocument[];
  activateFindMatch(match: OpenCanvasFindMatch | null, reveal?: boolean): boolean;
  syncFindSelection(clearOnMissing?: boolean): boolean;
  clearFindMatch(): boolean;
  ensureFont(fontId: number): Promise<void>;
  ensureBuiltInFont(fontId: number): Promise<void>;
  isFontLoaded(fontId: number, url?: string): boolean;
  getIncrementalFontState(fontId: number): IncrementalFontRuntimeState | null;
  getIncrementalFontCacheState(): IncrementalFontCacheState;
  getIncrementalFontPolicy(): IncrementalFontPolicy;
  setIncrementalFontPolicy(policy: Partial<IncrementalFontPolicy>): void;
  getClipboardFontUrl(fontId: number): string | null;
  registerLazyFont(fontId: number, url: string): void;
  registerFontFallback(fontId: number, fallbackFontId: number): void;
  handleMissingFontCoverage(fontId: number, coverageKind: number, sampleText: string): void;
  loadFont(fontId: number, url: string): Promise<void>;
  registerFont(font: BridgeFontRegistration): Promise<void>;
  registerFontStack(stack: BridgeFontStackRegistration): Promise<void>;
  loadSvg(svgId: number, url: string): Promise<AssetLoadResult>;
  loadTexture(textureId: number, url: string): Promise<AssetLoadResult>;
  releaseSvg(svgId: number): void;
  releaseTexture(textureId: number): void;
  replayLoadedAssets(): Promise<void>;
  resetLogs(): void;
  resetAppSession(): void;
}

export interface BridgeState {
  readonly ready: Promise<BridgeRuntime>;
  getRuntime(): BridgeRuntime | null;
  recreateRuntime(): Promise<BridgeRuntime>;
  resetLogs(): void;
  handleToBigInt(handle: WasmHandleLike): bigint;
  handleToString(handle: WasmHandleLike): string;
  pointerToHeapOffset(pointer: WasmHandleLike): number;
  normalizePointerForWasm(
    module: Pick<UiModule | CoreModule, 'usesMemory64'>,
    pointer: WasmHandleLike,
  ): number | bigint;
  toHeapPointer(
    module: Pick<UiModule | CoreModule, 'usesMemory64'>,
    pointer: WasmHandleLike,
  ): { readonly ptr: number | bigint; readonly offset: number };
}

export interface BridgeLoaderInfo {
  manifestHash: string | null;
  requestedWasmArchitecture: string;
  requestedRendererBackend: 'auto' | 'webgpu' | 'webgl2' | 'cpu';
  selectedWasmArchitecture: string;
  availableWasmArchitectures: readonly string[];
  memory64Supported: boolean;
  simdSupported: boolean;
  coreCompileMode: 'streaming' | 'buffer' | 'cached-module';
  uiCompileMode: 'streaming' | 'buffer' | 'cached-module';
  icuDataUrl: string | null;
  activeRenderer: 'none' | 'webgpu' | 'webgl2' | 'cpu';
  /** Incremented each time the renderer recovers from a device loss. Useful for tests. */
  deviceRecoveryCount: number;
}

export interface EffinDomCallbacks {
  onPointerEvent?: (handle: WasmHandleLike, eventType: number) => void;
  onPointerEventWithCoords?: (eventType: number, handle: WasmHandleLike, x: number, y: number, modifiers?: number) => void;
  onBeforeContextMenuHitTest?: () => void;
  onContextMenu?: (handle: WasmHandleLike, x: number, y: number) => void;
  onKeyEventWithKey?: (eventType: number, key: string, modifiers: number) => boolean | void;
  onFocusChanged?: (handle: WasmHandleLike, isFocused: boolean) => void;
  onTextChanged?: (handle: WasmHandleLike, text: string) => void;
  onTextReplaced?: (handle: WasmHandleLike, start: number, end: number, text: string) => void;
  onSelectionChanged?: (handle: WasmHandleLike, start: number, end: number) => void;
  onScroll?: (
    handle: WasmHandleLike,
    offsetX: number,
    offsetY: number,
    contentWidth: number,
    contentHeight: number,
    viewportWidth: number,
    viewportHeight: number,
  ) => void;
  onClipboardWrite?: (payload: ClipboardWritePayload) => void;
  onClipboardRead?: (handle: WasmHandleLike) => void;
  onCrossSelectionChanged?: (areaHandle: WasmHandleLike, text: string) => void;
  onRequestFontLoad?: (fontId: number, url: string) => void;
  onMissingFontCoverage?: (fontId: number, coverageKind: number, sampleText: string) => void;
  onRequestSemanticAnnouncement?: (handle: WasmHandleLike) => void;
}

export type UiFactory = (module?: object) => Promise<UiModule>;

declare global {
  interface Window {
    Module?: CoreModule;
    EffinDomUiV2ModuleFactory?: UiFactory;
    __effindomCallbacks?: EffinDomCallbacks;
    __bridgeReady?: boolean;
    __bridgeError?: string;
    __bridgeState?: {
      readonly commandWordCount: number;
      readonly commandWords: readonly number[];
      readonly rootHandle: string;
    };
    __bridgeLogs?: BridgeLogs;
    __bridgeTextByHandle?: Record<string, string>;
    __bridgeSelectionsByHandle?: Record<string, { start: number; end: number }>;
    __bridgeActiveEditorWindow?: {
      readonly handle: string | null;
      readonly text: string;
      readonly docStart: number;
      readonly docEnd: number;
    };
    __bridgeFindMatch?: OpenCanvasFindMatch | null;
    __bridgeFindState?: OpenCanvasFindState | null;
    __bridgeSemanticTree?: readonly SemanticNode[];
    __bridgeLoaderInfo?: BridgeLoaderInfo;
    __OPEN_CANVAS_API__?: OpenCanvasApi;
    EffinDomBrowserBridge?: BridgeState;
    __bridgeDebug?: { forceDeviceLost(): void };
  }
}

export {};
