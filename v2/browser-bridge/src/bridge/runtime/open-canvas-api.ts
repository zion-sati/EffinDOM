import { cloneSemanticTree } from '../../semantic';
import type {
  OpenCanvasApi,
  OpenCanvasFindOptions,
  UiModule,
} from '../../core-types';
import { findTextInOpenCanvasDocuments } from '../find-session';
import { FindController } from './find-controller';
import { SemanticController } from './semantic-controller';
import { TextDocumentController } from './text-documents';

interface OpenCanvasApiAdapterOptions {
  readonly ui: UiModule;
  readonly semantic: SemanticController;
  readonly find: FindController;
  readonly textDocuments: TextDocumentController;
  readonly commitFrame: () => void;
  readonly flushPendingCommit: () => Uint32Array | null;
}

export class OpenCanvasApiAdapter {
  private readonly api: OpenCanvasApi;

  public constructor(private readonly options: OpenCanvasApiAdapterOptions) {
    this.api = {
      getSemanticTree: () => cloneSemanticTree(this.options.semantic.getSemanticTree()),
      getBoundingBox: (handle: string) => this.options.semantic.getBoundingBox(handle),
      getTextVisibleBounds: (handle: string) => {
        const bounds = this.options.textDocuments.readVisibleTextBounds(handle);
        return bounds === null ? null : { ...bounds };
      },
      getTextDocument: (handle: string) => {
        const snapshot = this.options.textDocuments.readTextDocumentSnapshot(handle);
        return snapshot === null ? null : { ...snapshot.document };
      },
      getRangeRects: (handle: string, start: number, end: number) =>
        this.options.textDocuments.readRangeRects(handle, start, end).map((rect) => ({ ...rect })),
      findText: (query: string, options?: OpenCanvasFindOptions) => {
        const results = findTextInOpenCanvasDocuments(this.options.textDocuments.readFindDocuments(), query, options);
        return {
          query: results.query,
          options: { ...results.options },
          matches: results.matches.map((match) => ({ ...match })),
        };
      },
      setFindState: (state, revealActive = false) => this.options.find.setFindState(state, revealActive),
      getFindState: () => this.options.find.getFindState(),
      setFindMatch: (match) => this.options.find.activateFindMatch(match, false),
      revealRange: (handle: string, start: number, end: number) => {
        const range = this.options.textDocuments.resolveTextRange(handle, start, end);
        if (range === null) {
          return false;
        }
        if (this.options.ui._ui_reveal_text_range(range.handleArg, range.start, range.end) === 0) {
          return false;
        }
        this.options.commitFrame();
        this.options.flushPendingCommit();
        return true;
      },
    };
    window.__OPEN_CANVAS_API__ = this.api;
  }

  public getApi(): OpenCanvasApi {
    return this.api;
  }

  public destroy(): void {
    delete window.__OPEN_CANVAS_API__;
  }
}
