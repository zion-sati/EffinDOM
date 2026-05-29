import type { CoreModule, UiModule, WasmHandleLike } from '../../core-types';
import { normalizePointerForWasm, pointerToHeapOffset } from './encoding';

const textEncoder = new TextEncoder();

export function writeUtf8ToHeap(
  module: Pick<UiModule | CoreModule, 'HEAPU8' | '_malloc' | '_free' | 'refreshHeapViews' | 'usesMemory64'>,
  text: string,
): { readonly ptr: WasmHandleLike; readonly offset: number; readonly len: number; dispose(): void } {
  const bytes = textEncoder.encode(text);
  const ptr = normalizePointerForWasm(module, bytes.byteLength === 0 ? 0 : module._malloc(bytes.byteLength));
  const offset = pointerToHeapOffset(ptr);
  module.refreshHeapViews?.();
  if (bytes.byteLength > 0 && offset === 0) {
    throw new Error('WASM string malloc failed.');
  }
  if (bytes.byteLength > 0) {
    module.HEAPU8.set(bytes, offset);
  }
  return {
    ptr,
    offset,
    len: bytes.byteLength,
    dispose: () => {
      if (offset !== 0) {
        module._free(ptr);
      }
    },
  };
}

export function writeBytesToHeap(
  module: Pick<UiModule | CoreModule, 'HEAPU8' | '_malloc' | '_free' | 'refreshHeapViews' | 'usesMemory64'>,
  bytes: Uint8Array,
): { readonly ptr: WasmHandleLike; readonly offset: number; readonly len: number; dispose(): void } {
  const ptr = normalizePointerForWasm(module, bytes.byteLength === 0 ? 0 : module._malloc(bytes.byteLength));
  const offset = pointerToHeapOffset(ptr);
  module.refreshHeapViews?.();
  if (bytes.byteLength > 0 && offset === 0) {
    throw new Error('WASM bytes malloc failed.');
  }
  if (bytes.byteLength > 0) {
    module.HEAPU8.set(bytes, offset);
  }
  return {
    ptr,
    offset,
    len: bytes.byteLength,
    dispose: () => {
      if (offset !== 0) {
        module._free(ptr);
      }
    },
  };
}

export function extractCommandBuffer(ui: UiModule): Uint32Array {
  const lengthPtr = normalizePointerForWasm(ui, ui._malloc(4));
  const lengthOffset = pointerToHeapOffset(lengthPtr);
  if (lengthOffset === 0) {
    throw new Error('ui length malloc failed.');
  }

  try {
    const bufferPtr = ui._ui_get_command_buffer(lengthPtr);
    ui.refreshHeapViews?.();
    const wordCount = ui.HEAPU32[lengthOffset >>> 2] ?? 0;
    const bufferOffset = pointerToHeapOffset(normalizePointerForWasm(ui, bufferPtr));
    if (bufferOffset === 0 || wordCount === 0) {
      return new Uint32Array();
    }
    const wordOffset = bufferOffset >>> 2;
    return ui.HEAPU32.slice(wordOffset, wordOffset + wordCount);
  } finally {
    ui._free(lengthPtr);
  }
}

export function extractSemanticBuffer(ui: UiModule): Uint32Array {
  const lengthPtr = normalizePointerForWasm(ui, ui._malloc(4));
  const lengthOffset = pointerToHeapOffset(lengthPtr);
  if (lengthOffset === 0) {
    throw new Error('ui semantic length malloc failed.');
  }

  try {
    const bufferPtr = ui._ui_get_semantic_buffer(lengthPtr);
    ui.refreshHeapViews?.();
    const wordCount = ui.HEAPU32[lengthOffset >>> 2] ?? 0;
    const bufferOffset = pointerToHeapOffset(normalizePointerForWasm(ui, bufferPtr));
    if (bufferOffset === 0 || wordCount === 0) {
      return new Uint32Array();
    }
    const wordOffset = bufferOffset >>> 2;
    return ui.HEAPU32.slice(wordOffset, wordOffset + wordCount);
  } finally {
    ui._free(lengthPtr);
  }
}

export function executeCommandBuffer(core: CoreModule, words: Uint32Array): void {
  if (words.length === 0) {
    return;
  }
  const ptr = normalizePointerForWasm(core, core._malloc(words.byteLength));
  const offset = pointerToHeapOffset(ptr);
  core.refreshHeapViews?.();
  if (offset === 0) {
    throw new Error('core command malloc failed.');
  }

  try {
    core.HEAPU32.set(words, offset >>> 2);
    core._ed_execute_command_buffer(ptr, words.length);
  } finally {
    core._free(ptr);
  }
}
