# EffinDom Runtime

> A publishable runtime package for Fui-* apps.

This repository contains the runtime lane of EffinDom: the browser bridge, manifest assets, packaged runtime resources, and URL helpers that Fui-AS and Fui-RS apps load against.

It is not the app SDK itself. The Fui-* packages provide the app-facing controls and builders; this runtime owns loading, manifest resolution, asset staging, and browser-host integration.

## What ships

- `@effindomv2/runtime` (`v2/browser-bridge`)
- `bridge.js` and `harness.js` bundles
- `effindom.v2.manifest.json`
- runtime asset payloads under `dist/runtime/`
- bundled fonts under `dist/fonts/`

## Build and local publish

```bash
npm install
npm run build
npm run publish:local
```

`npm run build` bootstraps Skia if needed, type-checks, builds and stages the runtime assets, and validates npm packability with a dry run.

`npm run publish:local` creates an unpacked package under `published/<package-name>-<version>/` and writes the matching tarball into `published/`.

## Publishing

Use `npm run publish:npm` only after `npm run publish:local` succeeds and you are ready to publish the package to npm.
