import type { AssetLoadResult, CoreModule } from '../../core-types';
import { writeBytesToHeap } from '../utils/heap';
import { IncrementalFontManager } from './font-manager';

interface BitmapLike {
  readonly width: number;
  readonly height: number;
  close?(): void;
}

const svgTextDecoder = new TextDecoder();

function createDecodeCanvas(width: number, height: number): HTMLCanvasElement {
  const canvas = document.createElement('canvas');
  canvas.width = width;
  canvas.height = height;
  return canvas;
}

async function decodeBlobToBitmap(blob: Blob): Promise<ImageBitmap> {
  if (createImageBitmap === undefined) {
    throw new Error('createImageBitmap is unavailable for texture decoding.');
  }
  return await createImageBitmap(blob);
}

function extractBitmapRgba(bitmap: CanvasImageSource & BitmapLike): Uint8Array {
  const canvas = createDecodeCanvas(bitmap.width, bitmap.height);
  const context = canvas.getContext('2d', { willReadFrequently: true });
  if (context === null) {
    throw new Error('Failed to allocate a 2D canvas for texture decoding.');
  }
  context.clearRect(0, 0, bitmap.width, bitmap.height);
  context.drawImage(bitmap, 0, 0);
  const imageData = context.getImageData(0, 0, bitmap.width, bitmap.height);
  return new Uint8Array(
    imageData.data.buffer.slice(
      imageData.data.byteOffset,
      imageData.data.byteOffset + imageData.data.byteLength,
    ),
  );
}

function parseSvgLength(value: string | null): number | null {
  if (value === null) {
    return null;
  }
  const trimmed = value.trim();
  if (trimmed.length === 0) {
    return null;
  }
  const match = /^([+-]?(?:\d+\.?\d*|\.\d+))/.exec(trimmed);
  const numericPrefix = match?.[1];
  if (numericPrefix === undefined) {
    return null;
  }
  const parsed = Number.parseFloat(numericPrefix);
  return Number.isFinite(parsed) && parsed > 0 ? parsed : null;
}

function parseSvgViewBox(value: string | null): AssetLoadResult | null {
  if (value === null) {
    return null;
  }
  const parts = value
    .trim()
    .split(/[\s,]+/)
    .map((entry) => Number.parseFloat(entry))
    .filter((entry) => Number.isFinite(entry));
  if (parts.length !== 4) {
    return null;
  }
  const width = parts[2];
  const height = parts[3];
  if (width === undefined || height === undefined) {
    return null;
  }
  if (width <= 0 || height <= 0) {
    return null;
  }
  return { width, height };
}

function parseSvgIntrinsicSize(bytes: Uint8Array): AssetLoadResult {
  const markup = svgTextDecoder.decode(bytes);
  const documentRoot = new DOMParser().parseFromString(markup, 'image/svg+xml').documentElement;
  const width = parseSvgLength(documentRoot.getAttribute('width'));
  const height = parseSvgLength(documentRoot.getAttribute('height'));
  if (width !== null && height !== null) {
    return { width, height };
  }
  const viewBox = parseSvgViewBox(documentRoot.getAttribute('viewBox'));
  if (viewBox !== null) {
    return viewBox;
  }
  return { width: 1, height: 1 };
}

export class AssetManager {
  private readonly loadedSvgs = new Map<number, string>();
  private readonly loadedTextures = new Map<number, string>();

  public constructor(
    private readonly core: CoreModule,
    private readonly fontManager: IncrementalFontManager,
    private readonly onCommitFrame: () => void,
  ) {}

  public async loadSvg(svgId: number, url: string): Promise<AssetLoadResult> {
    this.loadedSvgs.set(svgId, url);
    const response = await fetch(url);
    if (!response.ok) {
      throw new Error(`Failed to fetch SVG ${url}: ${String(response.status)}`);
    }
    const bytes = new Uint8Array(await response.arrayBuffer());
    const size = parseSvgIntrinsicSize(bytes);
    const svgBytes = writeBytesToHeap(this.core, bytes);
    try {
      this.core._ed_register_svg(svgId, svgBytes.ptr, svgBytes.len);
    } finally {
      svgBytes.dispose();
    }
    this.onCommitFrame();
    return size;
  }

  public async loadTexture(textureId: number, url: string): Promise<AssetLoadResult> {
    this.loadedTextures.set(textureId, url);
    const response = await fetch(url);
    if (!response.ok) {
      throw new Error(`Failed to fetch texture ${url}: ${String(response.status)}`);
    }
    const blob = await response.blob();
    const bitmap = await decodeBlobToBitmap(blob);
    const width = bitmap.width;
    const height = bitmap.height;
    try {
      const rgba = extractBitmapRgba(bitmap);
      const textureBytes = writeBytesToHeap(this.core, rgba);
      try {
        this.core._ed_register_texture_rgba(textureId, textureBytes.ptr, width, height, textureBytes.len);
      } finally {
        textureBytes.dispose();
      }
    } finally {
      bitmap.close?.();
    }
    this.onCommitFrame();
    return {
      width,
      height,
    };
  }

  public releaseSvg(svgId: number): void {
    this.loadedSvgs.delete(svgId);
  }

  public releaseTexture(textureId: number): void {
    this.loadedTextures.delete(textureId);
    this.core._ed_unregister_texture(textureId);
    this.onCommitFrame();
  }

  public async replayLoadedAssets(): Promise<void> {
    await Promise.all([
      this.fontManager.replayLoadedFonts(),
      ...Array.from(this.loadedSvgs.entries(), async ([svgId, url]) => {
        const response = await fetch(url);
        if (!response.ok) {
          throw new Error(`Failed to refetch SVG ${url}: ${String(response.status)}`);
        }
        const bytes = new Uint8Array(await response.arrayBuffer());
        const svgBytes = writeBytesToHeap(this.core, bytes);
        try {
          this.core._ed_register_svg(svgId, svgBytes.ptr, svgBytes.len);
        } finally {
          svgBytes.dispose();
        }
      }),
      ...Array.from(this.loadedTextures.entries(), async ([textureId, url]) => {
        const response = await fetch(url);
        if (!response.ok) {
          throw new Error(`Failed to refetch texture ${url}: ${String(response.status)}`);
        }
        const blob = await response.blob();
        const bitmap = await decodeBlobToBitmap(blob);
        try {
          const rgba = extractBitmapRgba(bitmap);
          const textureBytes = writeBytesToHeap(this.core, rgba);
          try {
            this.core._ed_register_texture_rgba(textureId, textureBytes.ptr, bitmap.width, bitmap.height, textureBytes.len);
          } finally {
            textureBytes.dispose();
          }
        } finally {
          bitmap.close?.();
        }
      }),
    ]);
  }
}
