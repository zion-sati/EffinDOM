import type {
  OpenCanvasTextDocument,
  SemanticNode,
  UiModule,
} from '../../core-types';
import type { FindOnPageDocument } from '../../find-on-page';
import { handleToBigInt, toHeapPointer } from '../utils/encoding';

const INVALID_TEXT_DOCUMENT_LENGTH = 0xFFFFFFFF;
const openCanvasTextDecoder = new TextDecoder();

export interface TextDocumentMeta {
  readonly handleArg: number | bigint;
  readonly byteLength: number;
}

export interface ResolvedTextRange extends TextDocumentMeta {
  readonly start: number;
  readonly end: number;
}

export class TextDocumentController {
  public constructor(private readonly ui: UiModule) {}

  public readTextDocumentMeta(handle: string): TextDocumentMeta | null {
    const handleArg = this.toUiHandleArgument(handle);
    if (handleArg === null) {
      return null;
    }
    const byteLength = this.ui._ui_get_text_document_utf8_length(handleArg);
    if (byteLength < 0 || (byteLength >>> 0) === INVALID_TEXT_DOCUMENT_LENGTH) {
      return null;
    }
    return { handleArg, byteLength };
  }

  public resolveTextRange(handle: string, start: number, end: number): ResolvedTextRange | null {
    const meta = this.readTextDocumentMeta(handle);
    const range = this.normalizeByteRange(start, end);
    if (meta === null || range === null || range.end > meta.byteLength) {
      return null;
    }
    return {
      handleArg: meta.handleArg,
      byteLength: meta.byteLength,
      start: range.start,
      end: range.end,
    };
  }

  public readTextDocumentSnapshot(
    handle: string,
  ): { readonly document: OpenCanvasTextDocument; readonly byteLength: number } | null {
    const meta = this.readTextDocumentMeta(handle);
    if (meta === null) {
      return null;
    }
    if (meta.byteLength === 0) {
      return {
        document: { handle, text: '' },
        byteLength: 0,
      };
    }

    const textAllocation = toHeapPointer(this.ui, this.ui._malloc(meta.byteLength));
    const textPtr = textAllocation.ptr;
    const textOffset = textAllocation.offset;
    if (textOffset === 0) {
      throw new Error('ui text document malloc failed.');
    }

    try {
      const copied = this.ui._ui_copy_text_document_utf8(meta.handleArg, textPtr, meta.byteLength);
      this.ui.refreshHeapViews?.();
      if (copied === 0) {
        return null;
      }
      const text = openCanvasTextDecoder.decode(this.ui.HEAPU8.slice(textOffset, textOffset + meta.byteLength));
      return {
        document: { handle, text },
        byteLength: meta.byteLength,
      };
    } finally {
      this.ui._free(textPtr);
    }
  }

  public readVisibleTextBounds(handle: string): SemanticNode['bounds'] | null {
    const handleArg = this.toUiHandleArgument(handle);
    if (handleArg === null) {
      return null;
    }
    const allocation = toHeapPointer(this.ui, this.ui._malloc(16));
    if (allocation.offset === 0) {
      throw new Error('ui visible text bounds malloc failed.');
    }
    const xPtr = allocation.ptr;
    const yPtr = this.addPointerOffset(allocation.ptr, 4);
    const widthPtr = this.addPointerOffset(allocation.ptr, 8);
    const heightPtr = this.addPointerOffset(allocation.ptr, 12);
    try {
      const copied = this.ui._ui_get_text_visible_bounds(handleArg, xPtr, yPtr, widthPtr, heightPtr);
      this.ui.refreshHeapViews?.();
      if (copied === 0) {
        return null;
      }
      const base = allocation.offset >>> 2;
      return {
        x: this.ui.HEAPF32[base] ?? 0,
        y: this.ui.HEAPF32[base + 1] ?? 0,
        width: this.ui.HEAPF32[base + 2] ?? 0,
        height: this.ui.HEAPF32[base + 3] ?? 0,
      };
    } finally {
      this.ui._free(allocation.ptr);
    }
  }

  public readRangeRects(handle: string, start: number, end: number): SemanticNode['bounds'][] {
    const range = this.resolveTextRange(handle, start, end);
    if (range === null) {
      return [];
    }

    const rectCount = this.ui._ui_get_text_range_rect_count(range.handleArg, range.start, range.end);
    if (rectCount === 0) {
      return [];
    }

    const rectWordsAllocation = toHeapPointer(this.ui, this.ui._malloc(rectCount * 16));
    const rectWordsPtr = rectWordsAllocation.ptr;
    const rectWordsOffset = rectWordsAllocation.offset;
    if (rectWordsOffset === 0) {
      throw new Error('ui range rect malloc failed.');
    }

    try {
      const copiedCount = this.ui._ui_copy_text_range_rects(
        range.handleArg,
        range.start,
        range.end,
        rectWordsPtr,
        rectCount,
      );
      this.ui.refreshHeapViews?.();
      if (copiedCount === 0) {
        return [];
      }
      const words = this.ui.HEAPF32.slice(rectWordsOffset >>> 2, (rectWordsOffset >>> 2) + (copiedCount * 4));
      const rects: SemanticNode['bounds'][] = [];
      for (let index = 0; index < copiedCount; index += 1) {
        const base = index * 4;
        rects.push({
          x: words[base] ?? 0,
          y: words[base + 1] ?? 0,
          width: words[base + 2] ?? 0,
          height: words[base + 3] ?? 0,
        });
      }
      return rects;
    } finally {
      this.ui._free(rectWordsPtr);
    }
  }

  public readFindDocuments(): FindOnPageDocument[] {
    const documents: FindOnPageDocument[] = [];
    for (const handle of this.readTextSnapshotHandles()) {
      const snapshot = this.readTextDocumentSnapshot(handle);
      if (snapshot === null) {
        continue;
      }
      documents.push(snapshot.document);
    }
    return documents;
  }

  private readTextSnapshotHandles(): string[] {
    const handleCount = this.ui._ui_get_text_snapshot_handle_count();
    if (handleCount === 0) {
      return [];
    }

    const handleWordsAllocation = toHeapPointer(this.ui, this.ui._malloc(handleCount * 8));
    const handleWordsPtr = handleWordsAllocation.ptr;
    const handleWordsOffset = handleWordsAllocation.offset;
    if (handleWordsOffset === 0) {
      throw new Error('ui text snapshot handle malloc failed.');
    }

    try {
      const copiedCount = this.ui._ui_copy_text_snapshot_handles(handleWordsPtr, handleCount);
      this.ui.refreshHeapViews?.();
      if (copiedCount === 0) {
        return [];
      }
      const words = this.ui.HEAPU32.slice(handleWordsOffset >>> 2, (handleWordsOffset >>> 2) + (copiedCount * 2));
      const handles: string[] = [];
      for (let index = 0; index < copiedCount; index += 1) {
        const low = words[index * 2] ?? 0;
        const high = words[(index * 2) + 1] ?? 0;
        handles.push(((BigInt(high) << 32n) | BigInt(low)).toString());
      }
      return handles;
    } finally {
      this.ui._free(handleWordsPtr);
    }
  }

  private toUiHandleArgument(handle: string): bigint | null {
    try {
      return handleToBigInt(handle);
    } catch {
      return null;
    }
  }

  private normalizeByteRange(
    start: number,
    end: number,
  ): { readonly start: number; readonly end: number } | null {
    if (!Number.isInteger(start) || !Number.isInteger(end) || start < 0 || end < 0) {
      return null;
    }
    return {
      start: Math.min(start, end),
      end: Math.max(start, end),
    };
  }

  private addPointerOffset(pointer: number | bigint, offset: number): number | bigint {
    return typeof pointer === 'bigint' ? pointer + BigInt(offset) : pointer + offset;
  }
}
