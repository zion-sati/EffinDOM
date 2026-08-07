import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";

import { nativeHttpLinkContract } from "../native-runtime-link-contract.mjs";

test("native HTTP static dependency order follows CPR's CMake link closure", () => {
  assert.deepEqual(nativeHttpLinkContract("macos").staticLibraries, [
    "cpr",
    "curl",
  ]);
  assert.deepEqual(nativeHttpLinkContract("windows").staticLibraries, [
    "cpr",
    "curl",
  ]);
  assert.deepEqual(nativeHttpLinkContract("linux").staticLibraries, [
    "cpr",
    "curl",
    "ssl",
    "crypto",
  ]);
});

test("native HTTP system dependencies match each TLS backend", () => {
  assert.deepEqual(nativeHttpLinkContract("macos").systemLibraries, [
    "Security.framework",
    "SystemConfiguration.framework",
  ]);
  assert.deepEqual(nativeHttpLinkContract("windows").systemLibraries, [
    "crypt32",
    "secur32",
    "bcrypt",
    "advapi32",
  ]);
  assert.deepEqual(nativeHttpLinkContract("linux").systemLibraries, ["z"]);
});

test("native HTTP link contract rejects unknown platforms", () => {
  assert.throws(
    () => nativeHttpLinkContract("android"),
    /Unsupported native HTTP link platform: android/,
  );
});

test("Windows package validation recognizes native HTTP system DLLs", () => {
  const validator = readFileSync(
    new URL(
      "../../v2/native/windows/cmake/VerifyWindowsNativeDependencies.cmake",
      import.meta.url,
    ),
    "utf8",
  );

  for (const library of nativeHttpLinkContract("windows").systemLibraries) {
    assert.match(
      validator,
      new RegExp(`(?:\\\\||\\\\\\()${library}(?:\\\\||\\\\\\))`, "i"),
      `${library}.dll must be classified as a Windows system dependency`,
    );
  }
});
