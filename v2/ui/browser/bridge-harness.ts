interface CoreModule {
  HEAPU8: Uint8Array;
  HEAPU32: Uint32Array;
  wasmMemory?: WebAssembly.Memory;
  refreshHeapViews?(): void;
  canvas?: HTMLCanvasElement;
  onRuntimeInitialized?: () => void;
  effindomV2FramePresented?: () => void;
  effindomV2Context?: CanvasRenderingContext2D;
  _malloc(size: number): number;
  _free(ptr: number): void;
  _ed_init(width: number, height: number, dpr: number): void;
  _ed_init_webgl(width: number, height: number, dpr: number): void;
  _ed_init_sw(width: number, height: number, dpr: number): void;
  _ed_execute_command_buffer(ptr: number, length: number): void;
  _ed_render_frame(currentTimeMs: number): void;
  _ed_get_sw_framebuffer(): number | bigint;
  _ed_get_backend_type(): number;
  _ed_get_device_state(): number;
}

interface UiModule {
  HEAPU32: Uint32Array;
  wasmMemory?: WebAssembly.Memory;
  refreshHeapViews?(): void;
  _malloc(size: number): number;
  _free(ptr: number): void;
  _ui_reset(): void;
  _ui_create_node(type: number): number | bigint;
  _ui_node_add_child(parent: number | bigint, child: number | bigint): void;
  _ui_set_root(handle: number | bigint): void;
  _ui_resize_window(logicalWidth: number, logicalHeight: number): void;
  _ui_set_width(handle: number | bigint, value: number, unitEnum: number): void;
  _ui_set_height(handle: number | bigint, value: number, unitEnum: number): void;
  _ui_set_fill_width(handle: number | bigint, fill: boolean): void;
  _ui_set_fill_height(handle: number | bigint, fill: boolean): void;
  _ui_set_padding(handle: number | bigint, left: number, top: number, right: number, bottom: number): void;
  _ui_set_clip_to_bounds(handle: number | bigint, clip: boolean): void;
  _ui_set_bg_color(handle: number | bigint, color: number): void;
  _ui_commit_frame(timestampMs?: number): void;
  _ui_get_command_buffer(outLengthPtr: number): number;
}

interface BridgeState {
  readonly commandWordCount: number;
  readonly commandWords: readonly number[];
  readonly rootHandle: string;
  readonly childHandle: string;
}

type UiFactory = (module?: object) => Promise<UiModule>;

declare const HEAPU8: Uint8Array | undefined;
declare const HEAPU32: Uint32Array | undefined;

declare global {
  interface Window {
    Module?: CoreModule;
    EffinDomUiV2ModuleFactory?: UiFactory;
    __effindomV2UiReady?: boolean;
    __effindomV2UiError?: string;
    __effindomV2UiState?: BridgeState;
  }
}

function requireCanvas(id: string): HTMLCanvasElement {
  const element = document.getElementById(id);
  if (!(element instanceof HTMLCanvasElement)) {
    throw new Error(`Expected #${id} canvas.`);
  }
  return element;
}

function loadScript(url: string): Promise<void> {
  return new Promise<void>((resolve, reject) => {
    const script = document.createElement('script');
    script.src = url;
    script.async = true;
    script.addEventListener('load', () => {
      resolve();
    });
    script.addEventListener('error', () => {
      reject(new Error(`Failed to load ${url}`));
    });
    document.head.appendChild(script);
  });
}

async function loadCoreModule(scriptUrl: string, canvas: HTMLCanvasElement): Promise<CoreModule> {
  return await new Promise<CoreModule>((resolve, reject) => {
    const module: CoreModule = {
      HEAPU8: new Uint8Array(),
      HEAPU32: new Uint32Array(),
      refreshHeapViews: () => {
        if (module.wasmMemory !== undefined) {
          const buffer = module.wasmMemory.buffer;
          module.HEAPU8 = new Uint8Array(buffer);
          module.HEAPU32 = new Uint32Array(buffer);
          return;
        }
        if (typeof HEAPU8 !== 'undefined') {
          module.HEAPU8 = HEAPU8;
        }
        if (typeof HEAPU32 !== 'undefined') {
          module.HEAPU32 = HEAPU32;
        }
      },
      canvas,
      onRuntimeInitialized: () => {
        const emMemory = (module as { memory?: unknown }).memory;
        if (emMemory instanceof WebAssembly.Memory) {
          module.wasmMemory = emMemory;
        }
        module.refreshHeapViews?.();
        resolve(module);
      },
      _malloc: () => 0,
      _free: () => undefined,
      _ed_init: () => undefined,
      _ed_init_webgl: () => undefined,
      _ed_init_sw: () => undefined,
      _ed_execute_command_buffer: () => undefined,
      _ed_render_frame: () => undefined,
      _ed_get_sw_framebuffer: () => 0,
      _ed_get_backend_type: () => ED_BACKEND_NONE,
      _ed_get_device_state: () => ED_DEVICE_RECOVERING,
    };
    window.Module = module;
    void loadScript(scriptUrl).catch(reject);
  });
}

async function loadUiModule(scriptUrl: string): Promise<UiModule> {
  if (window.EffinDomUiV2ModuleFactory === undefined) {
    await loadScript(scriptUrl);
  }
  if (window.EffinDomUiV2ModuleFactory === undefined) {
    throw new Error('EffinDomUiV2ModuleFactory did not load.');
  }
  const ui = await window.EffinDomUiV2ModuleFactory({});
  const emMemory = (ui as { memory?: unknown }).memory;
  if (emMemory instanceof WebAssembly.Memory) {
    ui.wasmMemory = emMemory;
  }
  ui.refreshHeapViews = () => {
    if (ui.wasmMemory !== undefined) {
      ui.HEAPU32 = new Uint32Array(ui.wasmMemory.buffer);
      return;
    }
    if (typeof HEAPU32 !== 'undefined') {
      ui.HEAPU32 = HEAPU32;
    }
  };
  ui.refreshHeapViews();
  return ui;
}

function extractCommandWords(uiModule: UiModule): Uint32Array {
  const lengthPtr = uiModule._malloc(4);
  if (lengthPtr === 0) {
    throw new Error('ui length malloc failed.');
  }

  try {
    const bufferPtr = uiModule._ui_get_command_buffer(lengthPtr);
    uiModule.refreshHeapViews?.();
    const wordCount = uiModule.HEAPU32[lengthPtr >>> 2];
    if (wordCount === undefined) {
      throw new Error('ui command word count was unreadable.');
    }
    if (bufferPtr === 0 || wordCount === 0) {
      return new Uint32Array();
    }
    const wordOffset = bufferPtr >>> 2;
    return uiModule.HEAPU32.slice(wordOffset, wordOffset + wordCount);
  } finally {
    uiModule._free(lengthPtr);
  }
}

function executeOnCore(coreModule: CoreModule, words: Uint32Array): void {
  if (words.length === 0) {
    throw new Error('ui command buffer was empty.');
  }
  const ptr = coreModule._malloc(words.byteLength);
  coreModule.refreshHeapViews?.();
  if (ptr === 0) {
    throw new Error('core command malloc failed.');
  }

  try {
    coreModule.HEAPU32.set(words, ptr >>> 2);
    coreModule._ed_execute_command_buffer(ptr, words.length);
  } finally {
    coreModule._free(ptr);
  }
}

async function waitForFrame(): Promise<void> {
  await new Promise<void>((resolve) => {
    requestAnimationFrame(() => {
      resolve();
    });
  });
  await new Promise<void>((resolve) => {
    requestAnimationFrame(() => {
      resolve();
    });
  });
  await new Promise<void>((resolve) => window.setTimeout(resolve, 30));
}

function toHandleValue(handle: number | bigint): bigint {
  return typeof handle === 'bigint' ? handle : BigInt(handle);
}

const UI_NODE_FLEX_BOX = 0;
const UI_SIZE_UNIT_PIXEL = 0;
const ED_BACKEND_NONE = 0;
const ED_BACKEND_WEBGPU = 1;
const ED_BACKEND_WEBGL2 = 2;
const ED_BACKEND_CPU = 3;
const ED_DEVICE_RECOVERING = 2;

async function waitForCoreBackend(coreModule: CoreModule): Promise<number> {
  const deadline = performance.now() + 1_500;
  while (performance.now() < deadline) {
    const backend = coreModule._ed_get_backend_type();
    if (backend !== ED_BACKEND_NONE) {
      return backend;
    }
    if (coreModule._ed_get_device_state() !== ED_DEVICE_RECOVERING) {
      return backend;
    }
    await new Promise<void>((resolve) => {
      requestAnimationFrame(() => {
        resolve();
      });
    });
  }
  return coreModule._ed_get_backend_type();
}

async function initCoreRenderer(coreModule: CoreModule, canvas: HTMLCanvasElement, dpr: number): Promise<number> {
  coreModule._ed_init(canvas.width, canvas.height, dpr);
  let backend = await waitForCoreBackend(coreModule);
  if (backend === ED_BACKEND_WEBGPU) {
    return backend;
  }

  coreModule._ed_init_webgl(canvas.width, canvas.height, dpr);
  backend = coreModule._ed_get_backend_type();
  if (backend === ED_BACKEND_WEBGL2) {
    return backend;
  }

  coreModule._ed_init_sw(canvas.width, canvas.height, dpr);
  backend = coreModule._ed_get_backend_type();
  if (backend === ED_BACKEND_CPU) {
    return backend;
  }

  throw new Error('Failed to initialize a Core renderer backend.');
}

function presentSoftwareFrame(coreModule: CoreModule, canvas: HTMLCanvasElement): void {
  const existingOverlay = document.querySelector('[data-effindom-software-overlay="true"]');
  let overlay: HTMLCanvasElement;
  if (existingOverlay instanceof HTMLCanvasElement) {
    overlay = existingOverlay;
  } else {
    overlay = document.createElement('canvas');
    overlay.dataset.effindomSoftwareOverlay = 'true';
    overlay.setAttribute('aria-hidden', 'true');
    overlay.style.position = 'absolute';
    overlay.style.pointerEvents = 'none';
    overlay.style.zIndex = '1';
    document.body.appendChild(overlay);
  }
  const context = overlay.getContext('2d');
  if (context === null) {
    throw new Error('Expected Canvas2D context for software fallback.');
  }
  const framebufferPtr = coreModule._ed_get_sw_framebuffer();
  const ptr = typeof framebufferPtr === 'bigint' ? Number(framebufferPtr) : framebufferPtr;
  if (!Number.isSafeInteger(ptr) || ptr <= 0) {
    return;
  }
  overlay.width = canvas.width;
  overlay.height = canvas.height;
  overlay.style.left = `${String(canvas.offsetLeft)}px`;
  overlay.style.top = `${String(canvas.offsetTop)}px`;
  overlay.style.width = canvas.style.width;
  overlay.style.height = canvas.style.height;
  overlay.style.borderRadius = getComputedStyle(canvas).borderRadius;
  const imageData = context.createImageData(canvas.width, canvas.height);
  imageData.data.set(coreModule.HEAPU8.subarray(ptr, ptr + (canvas.width * canvas.height * 4)));
  context.putImageData(imageData, 0, 0);
}

async function initialize(): Promise<void> {
  const logicalWidth = 180;
  const logicalHeight = 140;
  const dpr = Math.max(1, window.devicePixelRatio || 1);
  const canvas = requireCanvas('fui-canvas');
  canvas.width = Math.round(logicalWidth * dpr);
  canvas.height = Math.round(logicalHeight * dpr);
  canvas.style.width = `${String(logicalWidth)}px`;
  canvas.style.height = `${String(logicalHeight)}px`;

  window.__effindomV2UiReady = false;
  delete window.__effindomV2UiError;

  const [coreModule, uiModule] = await Promise.all([
    loadCoreModule('../core/effindom-core-v2.js', canvas),
    loadUiModule('./effindom-ui-v2.js'),
  ]);

  await initCoreRenderer(coreModule, canvas, dpr);
  uiModule._ui_reset();
  uiModule._ui_resize_window(logicalWidth, logicalHeight);
  const rootHandle = toHandleValue(uiModule._ui_create_node(UI_NODE_FLEX_BOX));
  const childHandle = toHandleValue(uiModule._ui_create_node(UI_NODE_FLEX_BOX));
  if (rootHandle === 0n || childHandle === 0n) {
    throw new Error('ui_create_node returned UI_INVALID_HANDLE.');
  }
  uiModule._ui_set_root(rootHandle);
  uiModule._ui_set_width(rootHandle, logicalWidth, UI_SIZE_UNIT_PIXEL);
  uiModule._ui_set_height(rootHandle, logicalHeight, UI_SIZE_UNIT_PIXEL);
  uiModule._ui_set_padding(rootHandle, 12, 8, 16, 10);
  uiModule._ui_set_clip_to_bounds(rootHandle, true);
  uiModule._ui_set_width(childHandle, 200, UI_SIZE_UNIT_PIXEL);
  uiModule._ui_set_height(childHandle, 60, UI_SIZE_UNIT_PIXEL);
  uiModule._ui_set_bg_color(childHandle, 0xff0000ff);
  uiModule._ui_node_add_child(rootHandle, childHandle);

  uiModule._ui_commit_frame();
  const words = extractCommandWords(uiModule);
  executeOnCore(coreModule, words);
  let framePresented = false;
  coreModule.effindomV2FramePresented = () => {
    framePresented = true;
  };
  const pumpFrame = (): void => {
    requestAnimationFrame((timestamp) => {
      coreModule._ed_render_frame(timestamp);
      if (coreModule._ed_get_backend_type() === ED_BACKEND_CPU) {
        presentSoftwareFrame(coreModule, canvas);
      }
      if (!framePresented) {
        pumpFrame();
      }
    });
  };
  pumpFrame();
  await waitForFrame();
  await waitForFrame();

  window.__effindomV2UiState = {
    commandWordCount: words.length,
    commandWords: Array.from(words),
    rootHandle: rootHandle.toString(),
    childHandle: childHandle.toString(),
  };
  window.__effindomV2UiReady = true;
}

void initialize().catch((error: unknown) => {
  const message = error instanceof Error ? error.message : String(error);
  window.__effindomV2UiError = message;
  throw error;
});

export { };
