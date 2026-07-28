import assert from "node:assert/strict";
import test from "node:test";

import { assertNativeRuntimePackagingHost } from "../native-runtime-host-compatibility.mjs";

test("accepts matching native packaging hosts", () => {
  assert.doesNotThrow(() =>
    assertNativeRuntimePackagingHost("linux", "arm64", "linux", "arm64"),
  );
  assert.doesNotThrow(() =>
    assertNativeRuntimePackagingHost("macos", "x64", "darwin", "x64"),
  );
  assert.doesNotThrow(() =>
    assertNativeRuntimePackagingHost("windows", "x64", "win32", "x64"),
  );
});

test("rejects cross-platform and cross-architecture runtime packages", () => {
  assert.throws(
    () => assertNativeRuntimePackagingHost("linux", "arm64", "darwin", "arm64"),
    /Cannot package linux-arm64 runtime artifacts on darwin-arm64/u,
  );
  assert.throws(
    () => assertNativeRuntimePackagingHost("linux", "x64", "linux", "arm64"),
    /Cannot package linux-x64 runtime artifacts on linux-arm64/u,
  );
});
