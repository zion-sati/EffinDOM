export type {
  BridgeRuntime,
  BridgeState,
  EffinDomCallbacks,
  WasmHandleLike,
} from './core-types';

export {
  computeModifiers,
  handleToBigInt,
  toHeapPointer,
  normalizePointerForWasm,
  pointerToHeapOffset,
} from './bridge/utils/encoding';

export {
  getPointerPosition,
} from './bridge/events/canvas-geometry';

export {
  writeBytesToHeap,
} from './bridge/utils/heap';

export type {
  EffinDomRuntimeAssetUrls,
  EffinDomRuntimeConfig,
} from './runtime-config';

export {
  EFFINDOM_RUNTIME_ARTIFACT_DIR,
  EFFINDOM_RUNTIME_BRIDGE_SCRIPT,
  EFFINDOM_RUNTIME_DIST_DIR,
  EFFINDOM_RUNTIME_FONTS_DIR,
  EFFINDOM_RUNTIME_HARNESS_SCRIPT,
  EFFINDOM_RUNTIME_MANIFEST_FILE,
  applyRuntimeConfig,
  createRuntimeConfig,
  createRuntimeConfigScript,
  resolveRuntimeAssetUrls,
} from './runtime-config';
