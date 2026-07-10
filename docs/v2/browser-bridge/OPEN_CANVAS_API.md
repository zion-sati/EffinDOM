# Open Canvas API

The browser bridge exposes a host contract at:

```ts
window.__OPEN_CANVAS_API__
```

This is the long-term integration surface for browsers, extensions, accessibility tools, automation, password managers, spellcheckers, translators, and multi-app hosts that need to work with an open-canvas scene **without** scraping bridge-private DOM.

The design goal is not “hidden DOM, but documented.” The design goal is a **proper contract**.

---

## Contract lens

Treat Open Canvas as three contract families, not one bag of methods:

| Contract family | Purpose | Typical consumers |
|---|---|---|
| **Read models** | Inspect current scene state | accessibility, automation, extension discovery, overlays |
| **Commands** | Mutate the scene through intentful operations | autofill, translators, spellcheck replace, action invocation |
| **Events** | React to retained state changes without polling | browser UI, extensions, host integration, assistive tooling |

This is the DDD framing for the surface:

- **Read models** describe retained state in host terms.
- **Commands** express intention (`replaceRange`, `invokeAction`, `setFindState`) instead of transport details.
- **Events** publish domain changes (`textSelectionChanged`, `focusedHandleChanged`) instead of forcing consumers to poll every frame.

Open Canvas should stay stable even if bridge plumbing changes underneath it.

---

## Glossary

### Handle

A **handle** is an opaque retained-node identity token.

- Internally the runtime owns handles as 64-bit values.
- In the browser contract they are exposed as **strings** so JavaScript never loses precision on wasm64 / native 64-bit values.
- A handle is **not** a DOM id, CSS selector, or application business id.
- A handle is stable for the lifetime of the realized retained node inside the current runtime instance.

Recommended host rule:

> Treat handles as opaque capability-free identifiers. Compare them, store them, and pass them back to the API, but do not interpret them.

### SemanticNode

A `SemanticNode` is the host-facing semantic read model for one realized retained node.

It is intentionally **not** a DOM clone and not a full accessibility tree object model. It is the bridge snapshot that Open Canvas exposes so hosts can reason about the scene without touching bridge-private DOM projection.

Current shape:

```ts
export interface SemanticNode {
  readonly role: number;
  readonly roleName: string;
  readonly handle: OpenCanvasHandle;
  readonly bounds: SemanticBounds;
  readonly label: string;
  readonly state: SemanticState;
}
```

Field meaning:

| Field | Meaning |
|---|---|
| `role` | Engine enum value for the semantic role |
| `roleName` | Host-readable role label such as `button` or `textbox` |
| `handle` | Opaque retained-node identity |
| `bounds` | Realized logical canvas bounds |
| `label` | Current semantic text/label payload |
| `state` | Role-relevant semantic state flags (`readonly`, `multiline`, `checked`, etc.) |

### Logical canvas coordinates

All geometry returned from Open Canvas is expressed in **logical canvas coordinates**, not CSS pixels and not device pixels.

### UTF-8 byte offsets

Current text range APIs use **UTF-8 byte offsets** at the Open Canvas boundary because that is what the Ui runtime already owns today.

That must be treated as part of the contract for the current shipped text/find slice.

---

## Current shipped source of truth

The current type surface lives in:

- contract types: `v2/browser-bridge/src/open-canvas.ts`
- runtime wiring: `v2/browser-bridge/src/bridge/runtime.ts`

Current shipped interfaces:

```ts
export type OpenCanvasHandle = string;

export interface SemanticBounds {
  readonly x: number;
  readonly y: number;
  readonly width: number;
  readonly height: number;
}

export interface SemanticState {
  readonly checked?: 'false' | 'true' | 'mixed';
  readonly selected?: boolean;
  readonly expanded?: boolean;
  readonly disabled?: boolean;
  readonly readonly?: boolean;
  readonly multiline?: boolean;
  readonly orientation?: 'horizontal' | 'vertical';
  readonly valueNow?: number;
  readonly valueMin?: number;
  readonly valueMax?: number;
}

export interface SemanticNode {
  readonly role: number;
  readonly roleName: string;
  readonly handle: OpenCanvasHandle;
  readonly bounds: SemanticBounds;
  readonly label: string;
  readonly state: SemanticState;
}

export interface OpenCanvasTextDocument {
  readonly handle: OpenCanvasHandle;
  readonly text: string;
}

export type OpenCanvasAutofillHint =
  | 'none'
  | 'username'
  | 'current-password'
  | 'new-password'
  | 'email'
  | 'one-time-code';

export type OpenCanvasEditableTextKind = 'text' | 'password' | 'email';

export interface OpenCanvasEditableTextDocument extends OpenCanvasTextDocument {
  readonly selectionStart: number;
  readonly selectionEnd: number;
  readonly multiline: boolean;
  readonly readOnly: boolean;
  readonly disabled: boolean;
  readonly kind: OpenCanvasEditableTextKind;
  readonly autofillHint: OpenCanvasAutofillHint;
  readonly stableFieldName: string | null;
  readonly formHandle: OpenCanvasHandle | null;
}

export interface OpenCanvasFindMatch {
  readonly handle: OpenCanvasHandle;
  readonly start: number;
  readonly end: number;
}

export interface OpenCanvasFindOptions {
  readonly highlightAll?: boolean;
  readonly matchCase?: boolean;
  readonly matchDiacritics?: boolean;
  readonly wholeWords?: boolean;
}

export interface OpenCanvasResolvedFindOptions {
  readonly highlightAll: boolean;
  readonly matchCase: boolean;
  readonly matchDiacritics: boolean;
  readonly wholeWords: boolean;
}

export interface OpenCanvasFindResults {
  readonly query: string;
  readonly options: OpenCanvasResolvedFindOptions;
  readonly matches: readonly OpenCanvasFindMatch[];
}

export interface OpenCanvasFindState extends OpenCanvasFindResults {
  readonly activeMatchIndex: number;
}

export interface OpenCanvasApi {
  getSemanticTree(): SemanticNode[];
  getForms(): OpenCanvasForm[];
  getForm(handle: OpenCanvasHandle): OpenCanvasForm | null;
  getBoundingBox(handle: OpenCanvasHandle): SemanticBounds | null;
  getTextVisibleBounds(handle: OpenCanvasHandle): SemanticBounds | null;
  getFocusedHandle(): OpenCanvasHandle | null;
  getActiveTextHandle(): OpenCanvasHandle | null;
  getTextDocument(handle: OpenCanvasHandle): OpenCanvasTextDocument | null;
  getEditableTextDocument(handle: OpenCanvasHandle): OpenCanvasEditableTextDocument | null;
  getRangeRects(handle: OpenCanvasHandle, start: number, end: number): readonly SemanticBounds[];
  findText(query: string, options?: OpenCanvasFindOptions): OpenCanvasFindResults;
  setFindState(state: OpenCanvasFindState | null, revealActive?: boolean): boolean;
  getFindState(): OpenCanvasFindState | null;
  setFindMatch(match: OpenCanvasFindMatch | null): boolean;
  revealRange(handle: OpenCanvasHandle, start: number, end: number): boolean;
}
```

### What is shipped today

| Surface | Status | Notes |
|---|---|---|
| `getSemanticTree()` | **Shipped** | Cached semantic snapshot |
| `getForms()` | **Shipped** | Cached semantic form snapshot |
| `getForm(handle)` | **Shipped** | Specific semantic form snapshot |
| `getBoundingBox(handle)` | **Shipped** | Logical bounds snapshot |
| `getTextVisibleBounds(handle)` | **Shipped** | Realized visible bounds for non-editable `Text` |
| `getTextDocument(handle)` | **Shipped** | Realized non-editable `Text` only |
| `getFocusedHandle()` | **Shipped** | Current focused retained node handle |
| `getActiveTextHandle()` | **Shipped** | Current active text-editing handle |
| `getEditableTextDocument(handle)` | **Shipped** | Editable textbox/textarea snapshot including selection and autofill metadata |
| `getRangeRects(handle, start, end)` | **Shipped** | Realized range geometry |
| `findText(query, options)` | **Shipped** | Query-only Find read model |
| `setFindState(state, revealActive)` | **Shipped** | Retained Find session command |
| `getFindState()` | **Shipped** | Current retained Find session snapshot |
| `setFindMatch(match)` | **Shipped** | Legacy convenience command for one active match |
| `revealRange(handle, start, end)` | **Shipped** | Scroll/reveal command |

### Current scope limits

- text/find is currently **realized `Text` only**
- editable `TextInput` / `TextArea` documents are part of the shipped contract through `getEditableTextDocument(handle)`
- virtualized/unrealized content is **not** part of the shipped contract yet
- current text indices are **UTF-8 byte offsets**

### Current editable text scope

The shipped editable-text contract is intentionally narrow:

- `kind` is currently limited to `'text' | 'password' | 'email'`
- `autofillHint` is currently a string token or `null`
- `stableFieldName` is the retained field identity exposed for host integrations such as password managers
- `formHandle` identifies the semantic `Form` that the editable field belongs to, when one exists

In the current bridge implementation:

- `stableFieldName` maps to projected/hidden host editor DOM `name` / `id`
- FUI `nodeId` is the intended source for that identity
- projected host-autofill form fields are kept `aria-hidden` so they do not
  replace the retained semantic accessibility layer

---

## Why Open Canvas cannot be read-only forever

Read-only inspection is necessary but not sufficient.

If we want browser-native helpers and extensions to keep working, Open Canvas needs deliberate mutation and observation contracts too:

- **password managers** need to focus and fill
- **spellcheck / grammar tools** need to replace selected ranges
- **translation tools** need to replace or annotate text
- **accessibility tools** need action invocation and event observation
- **browser UI / host UI** need callbacks when focus, selection, or find state changes

If those consumers only get snapshot reads, they will fall back to brittle heuristics or private bridge DOM.

---

## Recommended durable contract families

## 1. Semantic discovery read model

This is the current `getSemanticTree()` / `getBoundingBox()` surface.

Keep it as the host discovery layer for:

- accessibility inspection
- automation discovery
- extension overlays
- cross-app scene introspection

## 2. Focus and active-editing contract

Hosts need to know what currently owns primary interaction or text editing.

Shipped:

```ts
interface OpenCanvasApi {
  getFocusedHandle(): OpenCanvasHandle | null;
  getActiveTextHandle(): OpenCanvasHandle | null;
}
```

Why it matters:

- browser/OS UI needs a stable focus anchor
- password managers need to target the active field
- spellcheckers and translators need to know which document is current

## 3. Text-document read model

Current `getTextDocument(handle)` is intentionally narrow for non-editable retained text.

Editable text is now shipped through a separate contract:

```ts
export interface OpenCanvasEditableTextDocument {
  readonly handle: OpenCanvasHandle;
  readonly text: string;
  readonly selectionStart: number;
  readonly selectionEnd: number;
  readonly multiline: boolean;
  readonly readOnly: boolean;
  readonly disabled: boolean;
  readonly kind: 'text' | 'password' | 'email';
  readonly autofillHint: 'none' | 'username' | 'current-password' | 'new-password' | 'email' | 'one-time-code';
  readonly stableFieldName: string | null;
}
```

This split is deliberate:

- `getTextDocument(handle)` remains the generic non-editable text read model
- `getEditableTextDocument(handle)` carries editing-specific state and host metadata
- browser-private DOM details stay out of the API surface

## 4. Selection read model

Selection needs its own first-class snapshot contract rather than bridge-private mirrors.

Recommended addition:

```ts
export interface OpenCanvasSelection {
  readonly handle: OpenCanvasHandle;
  readonly text: string;
  readonly start: number;
  readonly end: number;
  readonly rects: readonly SemanticBounds[];
  readonly isActive: boolean;
}

interface OpenCanvasApi {
  getSelection(): OpenCanvasSelection | null;
}
```

## 5. Mutation / command contract

This is where browser extensions and host tooling can **make mutations** to the scene through well-defined commands instead of DOM hacks.

Recommended additions:

```ts
interface OpenCanvasApi {
  focus(handle: OpenCanvasHandle): boolean;
  setSelection(handle: OpenCanvasHandle, start: number, end: number): boolean;
  replaceRange(handle: OpenCanvasHandle, start: number, end: number, text: string): boolean;
  setValue(handle: OpenCanvasHandle, value: string): boolean;
}
```

Design rule:

> Commands must route through the same retained editing / event model as user input. They must not be a bypass that mutates scene state behind the app’s back.

## 6. Find session contract

This is already partly shipped and should remain the model for future surfaces.

Find is not “dialog state.” It is a retained domain session:

- query
- options
- total matches
- active match
- optional secondary highlights

That is why the durable host shape is `findText(...)` plus `setFindState(...)`, not a browser-specific `showFindDialog()` API.

## 7. Action invocation contract

Hosts need to activate controls in intentful terms, not by guessing which synthetic pointer/key event might map to app behavior.

Recommended additions:

```ts
export interface OpenCanvasAction {
  readonly id: string;
  readonly label?: string;
  readonly kind: 'activate' | 'submit' | 'dismiss' | 'toggle' | 'custom';
  readonly enabled: boolean;
}

interface OpenCanvasApi {
  getAvailableActions(handle: OpenCanvasHandle): readonly OpenCanvasAction[];
  invokeAction(handle: OpenCanvasHandle, actionId?: string): boolean;
}
```

## 8. Annotation contract

This is the likely path for first-class spellcheck/grammar/translation overlays.

```ts
export interface OpenCanvasAnnotation {
  readonly id: string;
  readonly source: string;
  readonly kind: 'spelling' | 'grammar' | 'translation' | 'custom';
  readonly start: number;
  readonly end: number;
  readonly severity?: 'info' | 'warning' | 'error';
  readonly message?: string;
}

interface OpenCanvasApi {
  setAnnotations(handle: OpenCanvasHandle, source: string, annotations: readonly OpenCanvasAnnotation[]): boolean;
  clearAnnotations(handle: OpenCanvasHandle, source: string): boolean;
}
```

## 9. Event / callback contract

This is the missing piece the browser ecosystem needs most after read models and commands.

Polling-only integration is too weak. Hosts and extensions need proper callbacks such as `textSelectionChanged()`.

Recommended shape:

```ts
export interface OpenCanvasSemanticTreeChangedEvent {
  readonly handles?: readonly OpenCanvasHandle[];
}

export interface OpenCanvasFocusChangedEvent {
  readonly previousHandle: OpenCanvasHandle | null;
  readonly handle: OpenCanvasHandle | null;
}

export interface OpenCanvasTextSelectionChangedEvent {
  readonly handle: OpenCanvasHandle;
  readonly selection: OpenCanvasSelection | null;
}

export interface OpenCanvasTextDocumentChangedEvent {
  readonly handle: OpenCanvasHandle;
}

export interface OpenCanvasFindStateChangedEvent {
  readonly state: OpenCanvasFindState | null;
}

export interface OpenCanvasObserver {
  semanticTreeChanged?(event: OpenCanvasSemanticTreeChangedEvent): void;
  focusedHandleChanged?(event: OpenCanvasFocusChangedEvent): void;
  textSelectionChanged?(event: OpenCanvasTextSelectionChangedEvent): void;
  textDocumentChanged?(event: OpenCanvasTextDocumentChangedEvent): void;
  findStateChanged?(event: OpenCanvasFindStateChangedEvent): void;
}

interface OpenCanvasApi {
  subscribe(observer: OpenCanvasObserver): () => void;
}
```

Why callbacks matter:

- extensions should not poll every frame
- browser UI should update when selection/focus/find changes
- automation and accessibility tooling need timely notifications
- hosts need state transitions, not just periodic snapshots

The naming should stay domain-first:

- `textSelectionChanged`
- `textDocumentChanged`
- `focusedHandleChanged`
- `findStateChanged`

not plumbing-first names like `domSelectionUpdated`.

---

## Current bridge responsibilities

The bridge may still use hidden DOM internally for browser-native compatibility work:

- accessibility projection
- hidden editors / `EditContext`
- native browser Find provenance

That is acceptable **only** as bridge plumbing.

The contract remains Open Canvas.

> **Open Canvas API is the stable contract. DOM mirrors are implementation detail.**

---

## Recommended extension integration story

If we want extensions like Grammarly, spellcheckers, translators, password managers, autofill, automation tools, and accessibility helpers to work cleanly, the intended integration stack should be:

1. **Discover** scene state through semantic and text-document read models.
2. **Observe** changes through callbacks/events.
3. **Position** overlays through range geometry.
4. **Mutate** state through explicit commands.
5. **Invoke** actions through intentful action contracts.

That is the durable path. Anything else is fallback compatibility plumbing.

---

## Current recommendation

Treat the Open Canvas contract as:

- **already shipped** for semantic reads, range geometry, and Find session state
- **next in line** for focus, selection, text-document mutation, action invocation, and event callbacks

The immediate design rule for all future additions should be:

> Add capabilities as **intentful Open Canvas contracts** first, then let the bridge decide whether it also needs hidden DOM or platform-specific plumbing underneath.
