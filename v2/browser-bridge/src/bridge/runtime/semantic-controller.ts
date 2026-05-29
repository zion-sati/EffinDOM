import { cloneSemanticTree, HiddenDomProjector, parseSemanticBuffer } from '../../semantic';
import type { SemanticNode, UiModule } from '../../core-types';
import type { BridgeInteractionState } from '../local-types';
import { extractSemanticBuffer } from '../utils/heap';
import { TextDocumentController } from './text-documents';

const SEMANTIC_ANNOUNCEMENT_DELAY_MS = 50;

export class SemanticController {
  private readonly projector: HiddenDomProjector;
  private semanticTree: SemanticNode[] = [];
  private semanticTextLayoutsByHandle: Record<string, {
    readonly bounds: SemanticNode['bounds'];
  } | undefined> = {};
  private semanticAnnouncementTimer: number | null = null;
  private scheduledSemanticAnnouncementHandle: string | null = null;

  public constructor(
    canvas: HTMLCanvasElement,
    private readonly ui: UiModule,
    private readonly interactionState: BridgeInteractionState,
    private readonly textDocuments: TextDocumentController,
  ) {
    this.projector = new HiddenDomProjector(canvas);
  }

  public syncSize(logicalWidth: number, logicalHeight: number): void {
    this.projector.syncSize(logicalWidth, logicalHeight);
  }

  public syncSemanticState(): void {
    this.semanticTree = parseSemanticBuffer(extractSemanticBuffer(this.ui));
    for (const node of this.semanticTree) {
      if (node.roleName !== 'textbox') {
        continue;
      }
      if (this.interactionState.textByHandle[node.handle] !== undefined) {
        continue;
      }
      if (node.label.length === 0) {
        continue;
      }
      this.interactionState.textByHandle[node.handle] = node.label;
    }
    this.semanticTextLayoutsByHandle = this.buildSemanticTextLayouts();
    this.projector.update(
      this.semanticTree,
      this.interactionState.textByHandle,
      this.semanticTextLayoutsByHandle,
    );
    const focusedHandle = this.interactionState.getFocusedHandle();
    for (const handle of this.interactionState.consumePendingSemanticAnnouncements()) {
      if (focusedHandle !== null && focusedHandle === handle) {
        this.scheduleSemanticAnnouncement(handle);
      }
    }
    window.__bridgeSemanticTree = cloneSemanticTree(this.semanticTree);
  }

  public getSemanticTree(): readonly SemanticNode[] {
    return this.semanticTree;
  }

  public getBoundingBox(handle: string): SemanticNode['bounds'] | null {
    const node = this.semanticTree.find((entry) => entry.handle === handle);
    return node === undefined ? null : { ...node.bounds };
  }

  public destroy(): void {
    this.cancelPendingSemanticAnnouncement();
    this.semanticTree = [];
    this.semanticTextLayoutsByHandle = {};
    window.__bridgeSemanticTree = [];
    this.projector.destroy();
  }

  private cancelPendingSemanticAnnouncement(): void {
    if (this.semanticAnnouncementTimer !== null) {
      window.clearTimeout(this.semanticAnnouncementTimer);
      this.semanticAnnouncementTimer = null;
    }
    this.scheduledSemanticAnnouncementHandle = null;
  }

  private scheduleSemanticAnnouncement(handle: string): void {
    this.scheduledSemanticAnnouncementHandle = handle;
    if (this.semanticAnnouncementTimer !== null) {
      window.clearTimeout(this.semanticAnnouncementTimer);
    }
    this.semanticAnnouncementTimer = window.setTimeout(() => {
      this.semanticAnnouncementTimer = null;
      const targetHandle = this.scheduledSemanticAnnouncementHandle;
      this.scheduledSemanticAnnouncementHandle = null;
      if (targetHandle === null || this.interactionState.getFocusedHandle() !== targetHandle) {
        return;
      }
      this.projector.announceNode(targetHandle, this.semanticTree, this.interactionState.textByHandle);
    }, SEMANTIC_ANNOUNCEMENT_DELAY_MS);
  }

  private buildSemanticTextLayouts(): Record<string, {
    readonly bounds: SemanticNode['bounds'];
  } | undefined> {
    const layouts = Object.create(null) as Record<string, {
      readonly bounds: SemanticNode['bounds'];
    } | undefined>;
    for (const node of this.semanticTree) {
      if (node.label.length === 0) {
        continue;
      }
      const visibleTextBounds = this.textDocuments.readVisibleTextBounds(node.handle);
      if (visibleTextBounds === null) {
        continue;
      }
      const left = Math.max(node.bounds.x, visibleTextBounds.x);
      const top = Math.max(node.bounds.y, visibleTextBounds.y);
      const right = Math.min(node.bounds.x + node.bounds.width, visibleTextBounds.x + visibleTextBounds.width);
      const bottom = Math.min(node.bounds.y + node.bounds.height, visibleTextBounds.y + visibleTextBounds.height);
      if (right <= left || bottom <= top) {
        continue;
      }
      layouts[node.handle] = {
        bounds: {
          x: left,
          y: top,
          width: right - left,
          height: bottom - top,
        },
      };
    }
    return layouts;
  }
}
