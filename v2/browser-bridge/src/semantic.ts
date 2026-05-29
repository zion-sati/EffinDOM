import type { SemanticBounds, SemanticNode } from './core-types';

const ROLE_NONE = 0;
const ROLE_BUTTON = 1;
const ROLE_TEXTBOX = 2;
const ROLE_LINK = 3;
const ROLE_HEADING = 4;
const ROLE_FORM = 5;
const ROLE_LIST = 6;
const ROLE_LIST_ITEM = 7;
const ROLE_IMAGE = 8;
const ROLE_DIALOG = 9;
const ROLE_STATIC_TEXT = 10;
const ROLE_CHECKBOX = 11;
const ROLE_RADIO = 12;
const ROLE_RADIO_GROUP = 13;
const ROLE_SWITCH = 14;
const ROLE_SLIDER = 15;
const ROLE_COMBOBOX = 16;

const STATE_HAS_SELECTED = 1 << 0;
const STATE_IS_SELECTED = 1 << 1;
const STATE_HAS_EXPANDED = 1 << 2;
const STATE_IS_EXPANDED = 1 << 3;
const STATE_HAS_DISABLED = 1 << 4;
const STATE_IS_DISABLED = 1 << 5;
const STATE_HAS_VALUE_RANGE = 1 << 6;
const STATE_HAS_READONLY = 1 << 7;
const STATE_IS_READONLY = 1 << 8;
const STATE_HAS_MULTILINE = 1 << 9;
const STATE_IS_MULTILINE = 1 << 10;

const CHECKED_NONE = 0;
const CHECKED_FALSE = 1;
const CHECKED_TRUE = 2;
const CHECKED_MIXED = 3;

const ORIENTATION_NONE = 0;
const ORIENTATION_HORIZONTAL = 1;
const ORIENTATION_VERTICAL = 2;

const textDecoder = new TextDecoder();
const floatWordView = new DataView(new ArrayBuffer(4));
const textRunFitCache = new WeakMap<HTMLSpanElement, {
  readonly key: string;
  readonly scaleX: number;
  readonly scaleY: number;
  readonly translateX: number;
  readonly translateY: number;
}>();

interface SemanticRoleDescriptor {
  readonly roleName: string;
  readonly tagName: string;
  readonly ariaRole: string | null;
}

interface SemanticTextLayout {
  readonly bounds: SemanticBounds;
}

const SEMANTIC_ROLE_DESCRIPTORS: Record<number, SemanticRoleDescriptor> = {
  [ROLE_NONE]: { roleName: 'none', tagName: 'span', ariaRole: null },
  [ROLE_BUTTON]: { roleName: 'button', tagName: 'button', ariaRole: 'button' },
  [ROLE_TEXTBOX]: { roleName: 'textbox', tagName: 'input', ariaRole: 'textbox' },
  [ROLE_LINK]: { roleName: 'link', tagName: 'a', ariaRole: 'link' },
  [ROLE_HEADING]: { roleName: 'heading', tagName: 'h1', ariaRole: 'heading' },
  [ROLE_FORM]: { roleName: 'form', tagName: 'form', ariaRole: 'form' },
  [ROLE_LIST]: { roleName: 'list', tagName: 'ul', ariaRole: 'list' },
  [ROLE_LIST_ITEM]: { roleName: 'listitem', tagName: 'li', ariaRole: 'listitem' },
  [ROLE_IMAGE]: { roleName: 'img', tagName: 'div', ariaRole: 'img' },
  [ROLE_DIALOG]: { roleName: 'dialog', tagName: 'dialog', ariaRole: 'dialog' },
  [ROLE_STATIC_TEXT]: { roleName: 'text', tagName: 'p', ariaRole: null },
  [ROLE_CHECKBOX]: { roleName: 'checkbox', tagName: 'input', ariaRole: 'checkbox' },
  [ROLE_RADIO]: { roleName: 'radio', tagName: 'input', ariaRole: 'radio' },
  [ROLE_RADIO_GROUP]: { roleName: 'radiogroup', tagName: 'div', ariaRole: 'radiogroup' },
  [ROLE_SWITCH]: { roleName: 'switch', tagName: 'button', ariaRole: 'switch' },
  [ROLE_SLIDER]: { roleName: 'slider', tagName: 'div', ariaRole: 'slider' },
  [ROLE_COMBOBOX]: { roleName: 'combobox', tagName: 'div', ariaRole: 'combobox' },
};

function decodeCheckedState(checkedState: number): 'false' | 'true' | 'mixed' | undefined {
  if (checkedState === CHECKED_FALSE) {
    return 'false';
  }
  if (checkedState === CHECKED_TRUE) {
    return 'true';
  }
  if (checkedState === CHECKED_MIXED) {
    return 'mixed';
  }
  return undefined;
}

function decodeOrientation(orientation: number): 'horizontal' | 'vertical' | undefined {
  if (orientation === ORIENTATION_HORIZONTAL) {
    return 'horizontal';
  }
  if (orientation === ORIENTATION_VERTICAL) {
    return 'vertical';
  }
  return undefined;
}

function wordToFloat(word: number): number {
  floatWordView.setUint32(0, word >>> 0, true);
  return floatWordView.getFloat32(0, true);
}

function describeRole(role: number): SemanticRoleDescriptor {
  return SEMANTIC_ROLE_DESCRIPTORS[role] ?? {
    roleName: `unknown-${String(role)}`,
    tagName: 'span',
    ariaRole: null,
  };
}

function resolveTagName(node: SemanticNode, descriptor: SemanticRoleDescriptor): string {
  if (node.role === ROLE_TEXTBOX && node.state.multiline === true) {
    return 'textarea';
  }
  return descriptor.tagName;
}

function decodeLabel(words: Uint32Array, startWordIndex: number, labelLength: number): string {
  if (labelLength === 0) {
    return '';
  }
  const byteOffset = words.byteOffset + (startWordIndex * 4);
  const paddedByteLength = Math.ceil(labelLength / 4) * 4;
  const labelBytes = new Uint8Array(words.buffer, byteOffset, paddedByteLength);
  return textDecoder.decode(labelBytes.subarray(0, labelLength));
}

export function parseSemanticBuffer(words: Uint32Array): SemanticNode[] {
  if (words.length === 0) {
    return [];
  }

  const recordCount = words[0] ?? 0;
  if (recordCount === 0) {
    return [];
  }

  let index = 1;
  const nodes: SemanticNode[] = [];
  nodes.length = 0;

  for (let recordIndex = 0; recordIndex < recordCount; recordIndex += 1) {
    if ((index + 14) > words.length) {
      throw new Error('Semantic buffer ended mid-record.');
    }

    const role = words[index] ?? ROLE_NONE;
    const handleLow = words[index + 1] ?? 0;
    const handleHigh = words[index + 2] ?? 0;
    const stateFlags = words[index + 7] ?? 0;
    const checkedState = words[index + 8] ?? CHECKED_NONE;
    const orientation = words[index + 9] ?? ORIENTATION_NONE;
    const valueNow = wordToFloat(words[index + 10] ?? 0);
    const valueMin = wordToFloat(words[index + 11] ?? 0);
    const valueMax = wordToFloat(words[index + 12] ?? 0);
    const labelLength = words[index + 13] ?? 0;
    const labelWordCount = Math.ceil(labelLength / 4);
    const descriptor = describeRole(role);
    const handle = ((BigInt(handleHigh) << 32n) | BigInt(handleLow)).toString();
    const bounds: SemanticBounds = {
      x: wordToFloat(words[index + 3] ?? 0),
      y: wordToFloat(words[index + 4] ?? 0),
      width: wordToFloat(words[index + 5] ?? 0),
      height: wordToFloat(words[index + 6] ?? 0),
    };
    const state: {
      checked?: 'false' | 'true' | 'mixed';
      selected?: boolean;
      expanded?: boolean;
      disabled?: boolean;
      readonly?: boolean;
      multiline?: boolean;
      orientation?: 'horizontal' | 'vertical';
      valueNow?: number;
      valueMin?: number;
      valueMax?: number;
    } = {};
    const checked = decodeCheckedState(checkedState);
    if (checked !== undefined) {
      state.checked = checked;
    }
    if ((stateFlags & STATE_HAS_SELECTED) !== 0) {
      state.selected = (stateFlags & STATE_IS_SELECTED) !== 0;
    }
    if ((stateFlags & STATE_HAS_EXPANDED) !== 0) {
      state.expanded = (stateFlags & STATE_IS_EXPANDED) !== 0;
    }
    if ((stateFlags & STATE_HAS_DISABLED) !== 0) {
      state.disabled = (stateFlags & STATE_IS_DISABLED) !== 0;
    }
    if ((stateFlags & STATE_HAS_READONLY) !== 0) {
      state.readonly = (stateFlags & STATE_IS_READONLY) !== 0;
    }
    if ((stateFlags & STATE_HAS_MULTILINE) !== 0) {
      state.multiline = (stateFlags & STATE_IS_MULTILINE) !== 0;
    }
    const decodedOrientation = decodeOrientation(orientation);
    if (decodedOrientation !== undefined) {
      state.orientation = decodedOrientation;
    }
    if ((stateFlags & STATE_HAS_VALUE_RANGE) !== 0) {
      state.valueNow = valueNow;
      state.valueMin = valueMin;
      state.valueMax = valueMax;
    }
    index += 14;
    if ((index + labelWordCount) > words.length) {
      throw new Error('Semantic buffer label exceeded record bounds.');
    }
    const label = decodeLabel(words, index, labelLength);
    index += labelWordCount;
    nodes.push({
      role,
      roleName: descriptor.roleName,
      handle,
      bounds,
      label,
      state,
    });
  }

  return nodes;
}

function applyNodeFrame(element: HTMLElement, bounds: SemanticBounds): void {
  element.style.left = `${String(bounds.x)}px`;
  element.style.top = `${String(bounds.y)}px`;
  element.style.width = `${String(bounds.width)}px`;
  element.style.height = `${String(bounds.height)}px`;
}

function ensureTextRun(element: HTMLElement): HTMLSpanElement {
  const existing = element.firstElementChild;
  if (existing instanceof HTMLSpanElement && existing.getAttribute('data-semantic-text-run') === 'true') {
    return existing;
  }
  const textRun = document.createElement('span');
  textRun.setAttribute('data-semantic-text-run', 'true');
  textRun.style.position = 'absolute';
  textRun.style.left = '0';
  textRun.style.top = '0';
  textRun.style.display = 'block';
  textRun.style.pointerEvents = 'none';
  textRun.style.margin = '0';
  textRun.style.padding = '0';
  textRun.style.border = '0';
  element.replaceChildren(textRun);
  return textRun;
}

function ensureTextRunContent(textRun: HTMLSpanElement): HTMLSpanElement {
  const existing = textRun.firstElementChild;
  if (existing instanceof HTMLSpanElement && existing.getAttribute('data-semantic-text-content') === 'true') {
    return existing;
  }
  const textContent = document.createElement('span');
  textContent.setAttribute('data-semantic-text-content', 'true');
  textContent.style.display = 'inline-block';
  textContent.style.margin = '0';
  textContent.style.padding = '0';
  textContent.style.border = '0';
  textContent.style.whiteSpace = 'pre-wrap';
  textContent.style.overflowWrap = 'break-word';
  textContent.style.color = 'inherit';
  textContent.style.webkitTextFillColor = 'inherit';
  textContent.style.transformOrigin = 'top left';
  textRun.replaceChildren(textContent);
  return textContent;
}

function ensureTextNode(textContent: HTMLSpanElement): Text {
  if (textContent.childNodes.length === 1) {
    const onlyChild = textContent.firstChild;
    if (onlyChild instanceof Text) {
      return onlyChild;
    }
  }
  const textNode = document.createTextNode('');
  textContent.replaceChildren(textNode);
  return textNode;
}

function clearTextRun(element: HTMLElement): void {
  const existing = element.firstElementChild;
  if (existing instanceof HTMLSpanElement && existing.getAttribute('data-semantic-text-run') === 'true') {
    textRunFitCache.delete(existing);
    existing.remove();
  }
}

function applyTextRunLayout(
  textRun: HTMLSpanElement,
  nodeBounds: SemanticBounds,
  textLayout: SemanticTextLayout | undefined,
): void {
  if (textLayout === undefined) {
    textRunFitCache.delete(textRun);
    const textContent = ensureTextRunContent(textRun);
    textRun.style.left = '0';
    textRun.style.top = '0';
    textRun.style.width = '';
    textRun.style.height = '';
    textRun.style.overflow = 'visible';
    textContent.style.width = '';
    textContent.style.height = '';
    textContent.style.transform = '';
    return;
  }

  const textContent = ensureTextRunContent(textRun);
  const offsetX = textLayout.bounds.x - nodeBounds.x;
  const offsetY = textLayout.bounds.y - nodeBounds.y;
  textRun.style.left = `${String(offsetX)}px`;
  textRun.style.top = `${String(offsetY)}px`;
  textRun.style.width = `${String(textLayout.bounds.width)}px`;
  textRun.style.height = `${String(textLayout.bounds.height)}px`;
  textRun.style.overflow = 'hidden';
  textContent.style.width = `${String(textLayout.bounds.width)}px`;
  textContent.style.height = '';
  textContent.style.transform = '';
  const firstChild = textContent.firstChild;
  const textValue = firstChild instanceof Text ? firstChild.data : '';
  const fitKey = [
    textValue,
    textLayout.bounds.width.toFixed(3),
    textLayout.bounds.height.toFixed(3),
  ].join('\u001f');
  const cachedFit = textRunFitCache.get(textRun);
  if (cachedFit?.key === fitKey) {
    textContent.style.transform = `matrix(${String(cachedFit.scaleX)}, 0, 0, ${String(cachedFit.scaleY)}, ${String(cachedFit.translateX)}, ${String(cachedFit.translateY)})`;
    return;
  }
  const measuredChild = textContent.firstChild;
  let scaleX = 1;
  let scaleY = 1;
  let translateX = 0;
  let translateY = 0;
  if (measuredChild instanceof Text && measuredChild.data.length > 0) {
    const range = document.createRange();
    range.selectNodeContents(measuredChild);
    const measured = range.getBoundingClientRect();
    const contentRect = textContent.getBoundingClientRect();
    const nextScaleX = measured.width > 0 ? textLayout.bounds.width / measured.width : 1;
    const nextScaleY = measured.height > 0 ? textLayout.bounds.height / measured.height : 1;
    scaleX = Number.isFinite(nextScaleX) && nextScaleX > 0 ? nextScaleX : 1;
    scaleY = Number.isFinite(nextScaleY) && nextScaleY > 0 ? nextScaleY : 1;
    const localX = measured.x - contentRect.x;
    const localY = measured.y - contentRect.y;
    translateX = Number.isFinite(localX) ? -(localX * scaleX) : 0;
    translateY = Number.isFinite(localY) ? -(localY * scaleY) : 0;
  }
  textContent.style.transform = `matrix(${String(scaleX)}, 0, 0, ${String(scaleY)}, ${String(translateX)}, ${String(translateY)})`;
  textRunFitCache.set(textRun, {
    key: fitKey,
    scaleX,
    scaleY,
    translateX,
    translateY,
  });
}

function syncOrderedChildren(container: HTMLElement, orderedElements: readonly HTMLElement[]): void {
  let nextChild = container.firstElementChild as HTMLElement | null;
  for (const element of orderedElements) {
    if (element === nextChild) {
      nextChild = nextChild.nextElementSibling as HTMLElement | null;
      continue;
    }
    container.insertBefore(element, nextChild);
  }
  while (nextChild !== null) {
    const stale = nextChild;
    nextChild = stale.nextElementSibling as HTMLElement | null;
    stale.remove();
  }
}

function boundsContain(container: SemanticBounds, candidate: SemanticBounds): boolean {
  return candidate.x >= container.x &&
    candidate.y >= container.y &&
    (candidate.x + candidate.width) <= (container.x + container.width) &&
    (candidate.y + candidate.height) <= (container.y + container.height);
}

function ensureProjectedElement(
  layer: HTMLElement,
  byHandle: Map<string, HTMLElement>,
  node: SemanticNode,
): HTMLElement {
  const descriptor = describeRole(node.role);
  const tagName = resolveTagName(node, descriptor);
  const existing = byHandle.get(node.handle);
  if (existing?.tagName.toLowerCase() === tagName) {
    return existing;
  }

  const created = document.createElement(tagName);
  created.setAttribute('data-handle', node.handle);
  created.style.position = 'absolute';
  created.style.pointerEvents = 'none';
  created.style.margin = '0';
  created.style.padding = '0';
  created.style.boxSizing = 'border-box';
  created.style.background = 'transparent';
  created.style.border = '0';
  created.style.outline = 'none';
  created.style.appearance = 'none';
  created.style.webkitAppearance = 'none';
  created.style.color = 'transparent';
  created.style.webkitTextFillColor = 'transparent';
  created.style.caretColor = 'transparent';
  created.style.lineHeight = 'normal';
  created.tabIndex = -1;

  if (tagName === 'input') {
    const input = created as HTMLInputElement;
    if (node.role === ROLE_CHECKBOX) {
      input.type = 'checkbox';
    } else if (node.role === ROLE_RADIO) {
      input.type = 'radio';
    } else {
      input.type = 'text';
    }
    input.readOnly = node.state.readonly ?? true;
  } else if (tagName === 'textarea') {
    const textarea = created as HTMLTextAreaElement;
    textarea.readOnly = node.state.readonly ?? true;
    textarea.rows = 1;
    textarea.style.resize = 'none';
  }

  if (node.role === ROLE_TEXTBOX && node.state.multiline === true) {
    created.setAttribute('aria-multiline', 'true');
  }

  if (existing?.parentElement === layer) {
    layer.replaceChild(created, existing);
  } else {
    layer.appendChild(created);
  }

  byHandle.set(node.handle, created);
  return created;
}

function cloneNode(node: SemanticNode): SemanticNode {
  return {
    role: node.role,
    roleName: node.roleName,
    handle: node.handle,
    bounds: {
      x: node.bounds.x,
      y: node.bounds.y,
      width: node.bounds.width,
      height: node.bounds.height,
    },
    label: node.label,
    state: { ...node.state },
  };
}

function roleNeedsAriaLabel(role: number): boolean {
  return role === ROLE_BUTTON ||
    role === ROLE_TEXTBOX ||
    role === ROLE_IMAGE ||
    role === ROLE_DIALOG ||
    role === ROLE_CHECKBOX ||
    role === ROLE_RADIO ||
    role === ROLE_SLIDER ||
    role === ROLE_COMBOBOX;
}

function roleUsesTextContent(role: number): boolean {
  return role !== ROLE_BUTTON && role !== ROLE_IMAGE;
}

function describeAnnouncementRole(node: SemanticNode): string {
  switch (node.role) {
    case ROLE_BUTTON:
      return 'button';
    case ROLE_TEXTBOX:
      return node.state.multiline === true ? 'text area' : 'text input';
    case ROLE_LINK:
      return 'link';
    case ROLE_HEADING:
      return 'heading';
    case ROLE_FORM:
      return 'form';
    case ROLE_LIST:
      return 'list';
    case ROLE_LIST_ITEM:
      return 'list item';
    case ROLE_IMAGE:
      return 'image';
    case ROLE_DIALOG:
      return 'dialog';
    case ROLE_STATIC_TEXT:
      return 'text';
    case ROLE_CHECKBOX:
      return 'checkbox';
    case ROLE_RADIO:
      return 'radio button';
    case ROLE_RADIO_GROUP:
      return 'radio group';
    case ROLE_SWITCH:
      return 'switch';
    case ROLE_SLIDER:
      return 'slider';
    case ROLE_COMBOBOX:
      return 'combo box';
    default:
      return node.roleName;
  }
}

function buildNodeAnnouncement(
  node: SemanticNode,
  textByHandle: Readonly<Record<string, string>>,
): string {
  const parts: string[] = [];
  const label = node.role === ROLE_TEXTBOX && node.label.length === 0
    ? (textByHandle[node.handle] ?? '')
    : node.label;
  if (label.length > 0) {
    parts.push(label);
  }
  if (!(node.role === ROLE_SLIDER && label.length > 0)) {
    parts.push(describeAnnouncementRole(node));
  }
  if (node.state.checked === 'mixed') {
    parts.push('mixed');
  } else if (node.state.checked === 'true') {
    parts.push('checked');
  } else if (
    node.state.checked === 'false' &&
    (node.role === ROLE_CHECKBOX || node.role === ROLE_RADIO || node.role === ROLE_SWITCH)
  ) {
    parts.push('unchecked');
  }
  if (node.state.selected === true) {
    parts.push('selected');
  }
  if (node.state.expanded === true) {
    parts.push('expanded');
  } else if (node.state.expanded === false) {
    parts.push('collapsed');
  }
  if (node.state.readonly === true) {
    parts.push('read only');
  }
  if (node.state.disabled === true) {
    parts.push('disabled');
  }
  if (node.state.valueNow !== undefined && Number.isFinite(node.state.valueNow)) {
    parts.push(`value ${String(node.state.valueNow)}`);
  }
  return parts.join(', ');
}

class LiveAnnouncer {
  private readonly element: HTMLOutputElement;
  private pendingFrame: number | null = null;
  private pendingTimer: ReturnType<typeof setTimeout> | null = null;
  private clearTimer: ReturnType<typeof setTimeout> | null = null;

  public constructor(root: ShadowRoot) {
    const element = document.createElement('output');
    element.id = 'semantic-live-announcer';
    element.setAttribute('role', 'status');
    element.setAttribute('aria-live', 'polite');
    element.setAttribute('aria-atomic', 'true');
    element.setAttribute('data-effindom-live-announcer', 'true');
    element.style.position = 'absolute';
    element.style.width = '1px';
    element.style.height = '1px';
    element.style.margin = '-1px';
    element.style.padding = '0';
    element.style.border = '0';
    element.style.overflow = 'hidden';
    element.style.clip = 'rect(0 0 0 0)';
    element.style.clipPath = 'inset(50%)';
    element.style.whiteSpace = 'nowrap';
    element.style.opacity = '0';
    element.textContent = '\u00A0';
    root.appendChild(element);
    this.element = element;
  }

  public announce(message: string): void {
    if (message.length === 0) {
      return;
    }
    if (this.pendingFrame !== null) {
      cancelAnimationFrame(this.pendingFrame);
      this.pendingFrame = null;
    }
    if (this.pendingTimer !== null) {
      clearTimeout(this.pendingTimer);
      this.pendingTimer = null;
    }
    if (this.clearTimer !== null) {
      clearTimeout(this.clearTimer);
      this.clearTimer = null;
    }
    this.element.textContent = '\u00A0';
    this.pendingFrame = requestAnimationFrame(() => {
      this.pendingFrame = null;
      this.pendingTimer = setTimeout(() => {
        this.pendingTimer = null;
        this.element.textContent = message;
        this.clearTimer = setTimeout(() => {
          this.clearTimer = null;
          this.element.textContent = '\u00A0';
        }, 1500);
      }, 50);
    });
  }

  public destroy(): void {
    if (this.pendingFrame !== null) {
      cancelAnimationFrame(this.pendingFrame);
      this.pendingFrame = null;
    }
    if (this.pendingTimer !== null) {
      clearTimeout(this.pendingTimer);
      this.pendingTimer = null;
    }
    if (this.clearTimer !== null) {
      clearTimeout(this.clearTimer);
      this.clearTimer = null;
    }
    this.element.remove();
  }
}

export class HiddenDomProjector {
  private readonly canvas: HTMLCanvasElement;
  private readonly shell: HTMLDivElement;
  private readonly layer: HTMLDivElement;
  private readonly content: HTMLDivElement;
  private readonly announcer: LiveAnnouncer;
  private readonly elementsByHandle = new Map<string, HTMLElement>();

  public constructor(canvas: HTMLCanvasElement) {
    const parent = canvas.parentElement;
    if (!(parent instanceof HTMLElement)) {
      throw new Error('Expected canvas parent element for semantic projection.');
    }

    const shell = document.createElement('div');
    shell.id = 'scene-shell';
    shell.style.position = 'relative';
    shell.style.display = 'inline-block';
    shell.style.lineHeight = '0';
    parent.replaceChild(shell, canvas);

    const layer = document.createElement('div');
    layer.id = 'semantic-layer';
    layer.style.position = 'absolute';
    layer.style.left = '0';
    layer.style.top = '0';
    layer.style.pointerEvents = 'none';
    layer.style.overflow = 'hidden';
    layer.style.lineHeight = 'normal';
    layer.setAttribute('data-visibility', 'screen-reader-only');
    const shadowRoot = layer.attachShadow({ mode: 'open' });

    const content = document.createElement('div');
    content.id = 'semantic-content';
    content.style.position = 'absolute';
    content.style.left = '0';
    content.style.top = '0';
    content.style.width = '100%';
    content.style.height = '100%';
    content.style.padding = '0';
    content.style.border = '0';
    content.style.overflow = 'hidden';
    content.style.whiteSpace = 'nowrap';
    content.style.color = 'transparent';
    content.style.webkitTextFillColor = 'transparent';
    content.style.lineHeight = 'normal';
    shadowRoot.appendChild(content);
    const announcer = new LiveAnnouncer(shadowRoot);

    shell.appendChild(canvas);
    shell.appendChild(layer);

    canvas.setAttribute('role', 'application');
    canvas.setAttribute('aria-label', 'EffinDom application');

    this.canvas = canvas;
    this.shell = shell;
    this.layer = layer;
    this.content = content;
    this.announcer = announcer;
  }

  public syncSize(logicalWidth: number, logicalHeight: number): void {
    const width = `${String(logicalWidth)}px`;
    const height = `${String(logicalHeight)}px`;
    this.shell.style.width = width;
    this.shell.style.height = height;
    this.layer.style.width = width;
    this.layer.style.height = height;
  }

  public update(
    nodes: readonly SemanticNode[],
    textByHandle: Readonly<Record<string, string>>,
    textLayoutsByHandle: Readonly<Record<string, SemanticTextLayout | undefined>>,
  ): void {
    const seenHandles = new Set<string>();
    const orderedElements: HTMLElement[] = [];

    for (const node of nodes) {
      seenHandles.add(node.handle);
      const descriptor = describeRole(node.role);
      const element = ensureProjectedElement(this.content, this.elementsByHandle, node);
      orderedElements.push(element);
      const label = node.role === ROLE_TEXTBOX && node.label.length === 0
        ? (textByHandle[node.handle] ?? '')
        : node.label;

      if (descriptor.ariaRole === null) {
        element.removeAttribute('role');
      } else {
        element.setAttribute('role', descriptor.ariaRole);
      }
      if (label.length === 0 || !roleNeedsAriaLabel(node.role)) {
        element.removeAttribute('aria-label');
      } else {
        element.setAttribute('aria-label', label);
      }
      if (node.role === ROLE_DIALOG) {
        element.setAttribute('aria-modal', 'true');
        if (element instanceof HTMLDialogElement) {
          element.setAttribute('open', '');
        }
      } else {
        element.removeAttribute('aria-modal');
      }
      if (node.state.checked === undefined) {
        element.removeAttribute('aria-checked');
      } else {
        element.setAttribute('aria-checked', node.state.checked);
      }
      if (node.state.selected === undefined) {
        element.removeAttribute('aria-selected');
      } else {
        element.setAttribute('aria-selected', node.state.selected ? 'true' : 'false');
      }
      if (node.state.expanded === undefined) {
        element.removeAttribute('aria-expanded');
      } else {
        element.setAttribute('aria-expanded', node.state.expanded ? 'true' : 'false');
      }
      if (node.state.disabled === undefined) {
        element.removeAttribute('aria-disabled');
      } else {
        element.setAttribute('aria-disabled', node.state.disabled ? 'true' : 'false');
      }
      if (node.state.readonly === undefined) {
        element.removeAttribute('aria-readonly');
      } else {
        element.setAttribute('aria-readonly', node.state.readonly ? 'true' : 'false');
      }
      if (node.state.multiline === undefined) {
        element.removeAttribute('aria-multiline');
      } else {
        element.setAttribute('aria-multiline', node.state.multiline ? 'true' : 'false');
      }
      if (node.state.orientation === undefined) {
        element.removeAttribute('aria-orientation');
      } else {
        element.setAttribute('aria-orientation', node.state.orientation);
      }
      if (node.state.valueNow === undefined) {
        element.removeAttribute('aria-valuenow');
        element.removeAttribute('aria-valuemin');
        element.removeAttribute('aria-valuemax');
      } else {
        element.setAttribute('aria-valuenow', String(node.state.valueNow));
        element.setAttribute('aria-valuemin', String(node.state.valueMin ?? 0));
        element.setAttribute('aria-valuemax', String(node.state.valueMax ?? 0));
      }
      element.id = `semantic-node-${node.handle}`;
      element.setAttribute('data-role', node.roleName);
      applyNodeFrame(element, node.bounds);

      if (element instanceof HTMLInputElement) {
        if (node.role === ROLE_CHECKBOX) {
          element.checked = node.state.checked === 'true';
          element.indeterminate = node.state.checked === 'mixed';
        } else if (node.role === ROLE_RADIO) {
          element.checked = node.state.checked === 'true';
        } else {
          const nextValue = textByHandle[node.handle] ?? '';
          if (element.value !== nextValue) {
            element.value = nextValue;
          }
          element.readOnly = node.state.readonly ?? true;
        }
      } else if (element instanceof HTMLTextAreaElement) {
        const nextValue = textByHandle[node.handle] ?? '';
        if (element.value !== nextValue) {
          element.value = nextValue;
        }
        element.readOnly = node.state.readonly ?? true;
      } else if (roleUsesTextContent(node.role)) {
        const textRun = ensureTextRun(element);
        const textNode = ensureTextNode(ensureTextRunContent(textRun));
        if (textNode.data !== label) {
          textNode.data = label;
        }
        applyTextRunLayout(textRun, node.bounds, textLayoutsByHandle[node.handle]);
      } else {
        clearTextRun(element);
        if (element.textContent !== '') {
          element.textContent = '';
        }
      }
    }

    for (const node of nodes) {
      if (node.role !== ROLE_DIALOG) {
        continue;
      }
      const dialogElement = this.elementsByHandle.get(node.handle);
      if (!(dialogElement instanceof HTMLElement)) {
        continue;
      }

      const heading = nodes.find((candidate) =>
        candidate.handle !== node.handle &&
        candidate.role === ROLE_HEADING &&
        boundsContain(node.bounds, candidate.bounds));
      const descriptionNodes = nodes.filter((candidate) =>
        candidate.handle !== node.handle &&
        candidate.role === ROLE_STATIC_TEXT &&
        boundsContain(node.bounds, candidate.bounds));

      if (heading !== undefined) {
        dialogElement.setAttribute('aria-labelledby', `semantic-node-${heading.handle}`);
      } else {
        dialogElement.removeAttribute('aria-labelledby');
      }

      if (descriptionNodes.length > 0) {
        dialogElement.setAttribute(
          'aria-describedby',
          descriptionNodes.map((candidate) => `semantic-node-${candidate.handle}`).join(' '),
        );
      } else {
        dialogElement.removeAttribute('aria-describedby');
      }
    }

    for (const [handle, element] of this.elementsByHandle.entries()) {
      if (seenHandles.has(handle)) {
        continue;
      }
      element.remove();
      this.elementsByHandle.delete(handle);
    }
    syncOrderedChildren(this.content, orderedElements);
  }

  public announceNode(
    handle: string,
    nodes: readonly SemanticNode[],
    textByHandle: Readonly<Record<string, string>>,
  ): void {
    const node = nodes.find((candidate) => candidate.handle === handle);
    if (node === undefined) {
      return;
    }
    this.announcer.announce(buildNodeAnnouncement(node, textByHandle));
  }

  public destroy(): void {
    this.announcer.destroy();
    for (const element of this.elementsByHandle.values()) {
      element.remove();
    }
    this.elementsByHandle.clear();
    this.layer.remove();
    const parent = this.shell.parentElement;
    if (parent !== null) {
      parent.replaceChild(this.canvas, this.shell);
    } else {
      this.shell.remove();
    }
  }
}

export function cloneSemanticTree(nodes: readonly SemanticNode[]): SemanticNode[] {
  return nodes.map((node) => cloneNode(node));
}
