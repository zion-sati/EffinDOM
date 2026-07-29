import assert from "node:assert/strict";
import { existsSync, mkdtempSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";

import {
  createNativeRuntimeArtifact,
  createNativeRuntimeReleaseManifest,
  REQUIRED_NATIVE_RUNTIME_TARGETS,
  verifyNativeRuntimeArtifact,
} from "../native-runtime-artifact.mjs";

const commit = "0123456789abcdef0123456789abcdef01234567";

function fixture() {
  const root = mkdtempSync(join(tmpdir(), "effindom-runtime-artifact-"));
  const input = join(root, "input");
  mkdirSync(input);
  writeFileSync(join(input, "library.bin"), "runtime-library");
  writeFileSync(join(input, "font.ttf"), "font");
  return { root, input, remove: () => rmSync(root, { recursive: true, force: true }) };
}

function request(value, target) {
  return {
    schemaVersion: 1,
    sourceCommit: commit,
    target,
    coreAbi: 2,
    uiAbi: 1,
    minimumOs: {
      family: target.startsWith("windows-") ? "windows" : target.startsWith("macos-") ? "macos" : "glibc",
      version: target.startsWith("windows-") ? "10.0.17763" : target.startsWith("macos-") ? "13.0" : "2.28",
    },
    destination: join(value.root, `artifact-${target}`),
    files: [
      { source: join(value.input, "library.bin"), path: "runtime/lib/library.bin", role: "runtime-library" },
      { source: join(value.input, "font.ttf"), path: "runtime/assets/fonts/font.ttf", role: "runtime-asset" },
    ],
  };
}

const artifactTargets =
  process.platform === "win32"
    ? ["windows-arm64", "windows-x64"]
    : ["linux-arm64", "windows-x64"];

for (const target of artifactTargets) {
  test(`creates and independently verifies a ${target} runtime artifact`, () => {
    const value = fixture();
    try {
      const output = createNativeRuntimeArtifact(request(value, target));
      assert.ok(existsSync(join(output.root, output.artifact.archive)));
      assert.ok(existsSync(join(output.root, `${output.artifact.archive}.sha256`)));
      assert.equal(output.artifact.files[0].path, "runtime/assets/fonts/font.ttf");
      assert.equal(output.artifact.files.some((file) => file.role === "packager"), false);
      assert.equal(verifyNativeRuntimeArtifact(output.root, output.artifact, commit).target, target);
    } finally {
      value.remove();
    }
  });
}

test("canonicalizes a complete six-target release manifest", () => {
  const value = fixture();
  try {
    const seedTarget = process.platform === "win32" ? "windows-x64" : "linux-arm64";
    const seed = createNativeRuntimeArtifact(request(value, seedTarget)).artifact;
    const artifacts = REQUIRED_NATIVE_RUNTIME_TARGETS.map((target) => ({
      ...seed,
      target,
      archive: `effindom-native-${target}.${target.startsWith("windows-") ? "zip" : "tar.zst"}`,
      archiveFormat: target.startsWith("windows-") ? "zip" : "tar-zstd",
      minimumOs: request(value, target).minimumOs,
    })).reverse();
    const manifest = createNativeRuntimeReleaseManifest({
      release: "0.2.0-alpha.1",
      sourceCommit: commit,
      artifacts,
    });
    assert.deepEqual(manifest.artifacts.map((artifact) => artifact.target), REQUIRED_NATIVE_RUNTIME_TARGETS);
    assert.throws(
      () => createNativeRuntimeReleaseManifest({ release: "0.2.0", sourceCommit: commit, artifacts: artifacts.slice(1) }),
      /must contain 6 artifacts/,
    );
    assert.doesNotThrow(() => JSON.parse(readFileSync(join(value.root, `artifact-${seedTarget}`, "native-runtime-artifact.json"))));
  } finally {
    value.remove();
  }
});
