# DevTools DOM Mirror

The DevTools DOM Mirror is an optional debugging surface that projects the
retained EffinDom tree into browser DOM nodes so normal browser DevTools can
inspect the UI structure.

It is separate from the semantic projection. The semantic projection serves
accessibility, browser find, text selection, and browser services. The DevTools
DOM Mirror is debug tooling only.

## Modes

Runtime config accepts:

```ts
window.__effindomRuntime = Object.assign({}, window.__effindomRuntime, {
  buildMode: "debug",
  devToolsDomMirror: "on-requested",
});
```

`buildMode` can be:

- `"debug"`
- `"release"`

`devToolsDomMirror` can be:

- `"disabled"`: never create the mirror.
- `"enabled"`: create and keep the mirror active.
- `"on-requested"`: create the mirror only after a user/debug request.

If `devToolsDomMirror` is omitted, debug builds default to `"on-requested"` and
release builds default to `"disabled"`.

Generated `create-fui-as-app` templates emit only `buildMode`. That keeps the
template harness simple while still allowing users to override
`window.__effindomRuntime.devToolsDomMirror` before the bridge loads.

Harness code can also opt in explicitly:

```ts
startHarness({
  wasmPath: "./app.wasm",
  devToolsDomMirror: "enabled",
});
```

Routed apps use the same option:

```ts
startRoutedHarness({
  shellId: "fui-routes",
  routeBase: "/",
  routes,
  devToolsDomMirror: "on-requested",
  run(exports, route) {
    exports.__runApp();
  },
});
```

## Activation

When the runtime is loaded, the bridge exposes:

```ts
EffinDomBrowserBridge.devTools.enableDomMirror()
EffinDomBrowserBridge.devTools.disableDomMirror()
EffinDomBrowserBridge.devTools.toggleDomMirror()
EffinDomBrowserBridge.devTools.isDomMirrorEnabled()
EffinDomBrowserBridge.devTools.openDebugDialog()
EffinDomBrowserBridge.devTools.closeDebugDialog()
EffinDomBrowserBridge.devTools.toggleDebugDialog()
EffinDomBrowserBridge.devTools.isDebugDialogOpen()
EffinDomBrowserBridge.devTools.selectHandle(handle)
EffinDomBrowserBridge.devTools.clearSelection()
EffinDomBrowserBridge.devTools.getSelectedHandle()
```

`Shift+Meta+F12` toggles the debug dialog. The dialog can enable the DOM Mirror
and toggle Inspect Mode. `Escape` exits Inspect Mode first, then closes the
dialog when appropriate.

The console APIs return `false` when the current mode disallows the operation,
for example when `devToolsDomMirror` is `"disabled"` or when a selected handle
does not exist in the latest retained-tree snapshot.

## Inspect Mode

Inspect Mode uses EffinDom hit testing rather than DOM hit testing. Hovering the
canvas highlights the retained node under the pointer; clicking selects it and
prevents the app from receiving that click.

Selecting or inspecting a node does not scroll the app. Offscreen retained
nodes can exist in the mirror tree, but highlight overlays are pinned to the
rendered canvas and only show currently visible geometry.

## DOM Shape

The mirror root is:

```html
<div id="effindom-devtools-dom-mirror" data-fui-devtools-dom-mirror="true">
  ...
</div>
```

The overlay root is:

```html
<div id="effindom-devtools-overlay" data-fui-devtools-overlay="true"></div>
```

The debug dialog root is:

```html
<div id="effindom-devtools-debug-dialog" data-fui-devtools-debug-dialog="true"></div>
```

Mirror elements use readable custom tags where the runtime has enough type or
semantic information:

- `fui-button`
- `fui-textbox`
- `fui-link`
- `fui-heading`
- `fui-form`
- `fui-list`
- `fui-list-item`
- `fui-image`
- `fui-dialog`
- `fui-checkbox`
- `fui-radio`
- `fui-radio-group`
- `fui-switch`
- `fui-slider`
- `fui-combo-box`
- `fui-flex-box`
- `fui-text`
- `fui-svg`
- `fui-scroll-view`
- `fui-grid`
- `fui-path`
- `fui-node` fallback

Useful mirror attributes include:

- `data-fui-handle`
- `data-fui-parent-handle`
- `data-fui-node-id`
- `data-fui-type`
- `data-fui-render-node-type`
- `data-fui-node-type`
- `data-fui-semantic-role`
- `data-fui-semantic-role-name`
- `data-fui-semantic-label`
- `data-fui-bounds`
- `data-fui-visible-bounds`
- `data-fui-clipped`
- `data-fui-scroll`
- `data-fui-scroll-ancestor`
- `data-fui-interactive`
- `data-fui-focusable`
- `data-fui-editable`
- `data-fui-custom-drawable`
- `data-fui-portal`
- `data-fui-selected`
- `data-fui-inspect-hovered`

Bounds are logical canvas coordinates, not device-pixel-multiplied backing
store coordinates.

The mirror is marked as debug infrastructure, not app content. It is hidden from
accessibility and should not be used as a public DOM, styling, automation, or
accessibility contract.

## Generated App Defaults

The generated templates use:

- `npm run dev`: debug build mode, mirror defaults to on-requested.
- `npm run build`: debug build mode, mirror defaults to on-requested.
- `npm run publish`: release build mode, mirror defaults to disabled.

To override the default in a generated app, set the field before `bridge.js`
loads:

```html
<script>
  window.__effindomRuntime = Object.assign({}, window.__effindomRuntime, {
    devToolsDomMirror: "enabled",
  });
</script>
```

## Runtime Config Helpers

`@effindomv2/runtime` exports:

- `BuildMode`
- `DevToolsDomMirrorMode`
- `createRuntimeConfig(...)`
- `applyRuntimeConfig(...)`
- `createRuntimeConfigScript(...)`
- `normalizeBuildMode(...)`
- `normalizeDevToolsDomMirrorMode(...)`
- `normalizeRuntimeConfig(...)`
- `resolveDevToolsDomMirrorConfig(...)`

`resolveDevToolsDomMirrorConfig(...)` applies the defaulting rule: debug implies
on-requested, release implies disabled, and an explicit mirror mode wins.

## Testing

For browser-bridge changes:

```bash
npm run typecheck:v2
npm run build:v2:browser-bridge
npx playwright test -c v2/browser-bridge/playwright.config.ts tests/devtools-dom-mirror.spec.ts
```

For FUI-AS demo integration:

```bash
npm run build:v2:fui-as
cd v2/fui-as && npx playwright test -c playwright.config.ts tests/demo-devtools-dom-mirror.spec.ts
```
