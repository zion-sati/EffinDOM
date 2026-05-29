import type { BridgeRuntime } from '../../core-types';
import { handleToBigInt } from '../utils/encoding';

function pointInSemanticBounds(
  bounds: { readonly x: number; readonly y: number; readonly width: number; readonly height: number },
  x: number,
  y: number,
): boolean {
  return x >= bounds.x &&
    x <= (bounds.x + bounds.width) &&
    y >= bounds.y &&
    y <= (bounds.y + bounds.height);
}

export function findSemanticTextboxHandleAtPoint(runtime: BridgeRuntime, x: number, y: number): bigint {
  const tree = runtime.getSemanticTree();
  for (let i = tree.length - 1; i >= 0; i -= 1) {
    const node = tree[i];
    if (node?.roleName !== 'textbox') {
      continue;
    }
    if (pointInSemanticBounds(node.bounds, x, y)) {
      return handleToBigInt(node.handle);
    }
  }
  return 0n;
}
