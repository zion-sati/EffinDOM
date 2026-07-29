import { spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import {
  chmodSync,
  copyFileSync,
  existsSync,
  lstatSync,
  mkdirSync,
  mkdtempSync,
  readFileSync,
  readdirSync,
  renameSync,
  rmSync,
  statSync,
  utimesSync,
  writeFileSync,
} from "node:fs";
import { tmpdir } from "node:os";
import { basename, dirname, join, resolve, sep } from "node:path";

export const NATIVE_RUNTIME_MANIFEST_SCHEMA_VERSION = 1;
export const NATIVE_RUNTIME_ARTIFACT_MANIFEST = "native-runtime-artifact.json";
export const REQUIRED_NATIVE_RUNTIME_TARGETS = Object.freeze([
  "linux-arm64",
  "linux-x64",
  "macos-arm64",
  "macos-x64",
  "windows-arm64",
  "windows-x64",
]);

const fileRoles = new Set([
  "host-library",
  "launcher",
  "link-metadata",
  "runtime-asset",
  "runtime-library",
]);
const normalizedTimestamp = new Date("1980-01-01T00:00:00.000Z");

function fail(message) {
  throw new Error(message);
}

function run(command, args, options = {}) {
  const result = spawnSync(command, args, {
    cwd: options.cwd,
    encoding: "utf8",
    stdio: options.capture ? ["ignore", "pipe", "pipe"] : "inherit",
    env: options.env ? { ...process.env, ...options.env } : process.env,
  });
  if (result.status !== 0) {
    const detail = [result.stdout, result.stderr].filter(Boolean).join("\n").trim();
    fail(`${command} failed${detail ? `:\n${detail}` : ""}`);
  }
  return result.stdout?.trim() ?? "";
}

function archiveFormat(target) {
  return target.startsWith("windows-") ? "zip" : "tar-zstd";
}

function archiveName(target) {
  return `effindom-native-${target}.${archiveFormat(target) === "zip" ? "zip" : "tar.zst"}`;
}

function sha256Bytes(bytes) {
  return createHash("sha256").update(bytes).digest("hex");
}

function sha256File(path) {
  return sha256Bytes(readFileSync(path));
}

function canonicalJson(value) {
  return `${JSON.stringify(value, null, 2)}\n`;
}

function validateDigest(name, value) {
  if (!/^[0-9a-f]{64}$/.test(value)) fail(`${name} must be a lowercase SHA-256 digest`);
}

function validateCommit(value) {
  if (!/^(?:[0-9a-f]{40}|[0-9a-f]{64})$/.test(value)) {
    fail("sourceCommit must be a lowercase 40- or 64-character hexadecimal digest");
  }
}

function validateRelativePath(value) {
  if (
    typeof value !== "string" ||
    value.length === 0 ||
    value.startsWith("/") ||
    value.startsWith("\\") ||
    /^[A-Za-z]:/.test(value) ||
    value.includes("\\") ||
    value.split("/").some((part) => part === "" || part === "." || part === "..")
  ) {
    fail(`runtime path must be safe and relative: ${JSON.stringify(value)}`);
  }
}

function expectedMinimumFamily(target) {
  if (target.startsWith("macos-")) return "macos";
  if (target.startsWith("windows-")) return "windows";
  return "glibc";
}

function validateTarget(target) {
  if (!REQUIRED_NATIVE_RUNTIME_TARGETS.includes(target)) {
    fail(`unsupported native runtime target: ${target}`);
  }
}

function validateMinimumOs(target, minimumOs) {
  if (
    minimumOs?.family !== expectedMinimumFamily(target) ||
    typeof minimumOs?.version !== "string" ||
    minimumOs.version.trim() === ""
  ) {
    fail(`target ${target} has invalid minimum OS metadata`);
  }
}

function validateFiles(files) {
  if (!Array.isArray(files) || files.length === 0) fail("runtime artifact contains no files");
  const paths = new Set();
  for (const file of files) {
    validateRelativePath(file.path);
    if (paths.has(file.path)) fail(`runtime path appears more than once: ${file.path}`);
    paths.add(file.path);
    if (!Number.isSafeInteger(file.bytes) || file.bytes < 0) fail(`invalid byte length for ${file.path}`);
    validateDigest(`runtime file ${file.path}`, file.sha256);
    if (!fileRoles.has(file.role)) fail(`invalid runtime file role: ${file.role}`);
    if (file.executable !== undefined && typeof file.executable !== "boolean") {
      fail(`invalid executable flag for ${file.path}`);
    }
  }
}

function validateBundleManifest(manifest) {
  if (manifest.schemaVersion !== NATIVE_RUNTIME_MANIFEST_SCHEMA_VERSION) fail("unsupported runtime manifest schema");
  validateCommit(manifest.sourceCommit);
  validateTarget(manifest.target);
  if (!Number.isInteger(manifest.coreAbi) || manifest.coreAbi <= 0) fail("coreAbi must be positive");
  if (!Number.isInteger(manifest.uiAbi) || manifest.uiAbi <= 0) fail("uiAbi must be positive");
  validateMinimumOs(manifest.target, manifest.minimumOs);
  validateFiles(manifest.files);
}

function canonicalBundleManifest(manifest) {
  validateBundleManifest(manifest);
  return {
    schemaVersion: manifest.schemaVersion,
    sourceCommit: manifest.sourceCommit,
    target: manifest.target,
    coreAbi: manifest.coreAbi,
    uiAbi: manifest.uiAbi,
    minimumOs: {
      family: manifest.minimumOs.family,
      version: manifest.minimumOs.version,
    },
    files: [...manifest.files]
      .sort((left, right) => left.path.localeCompare(right.path))
      .map((file) => ({
        path: file.path,
        bytes: file.bytes,
        sha256: file.sha256,
        role: file.role,
        ...(file.executable ? { executable: true } : {}),
      })),
  };
}

function createArchive(bundle, archive, format) {
  if (format === "zip") {
    if (process.platform === "win32") {
      run("powershell", [
        "-NoProfile",
        "-Command",
        "Compress-Archive -Path (Join-Path $env:EFFINDOM_ARCHIVE_SOURCE '*') -DestinationPath $env:EFFINDOM_ARCHIVE_DESTINATION -CompressionLevel Optimal",
      ], {
        env: {
          EFFINDOM_ARCHIVE_SOURCE: bundle,
          EFFINDOM_ARCHIVE_DESTINATION: archive,
        },
      });
    } else {
      run("zip", ["-q", "-r", archive, "."], { cwd: bundle });
    }
    return;
  }
  const tarPath = `${archive}.tar`;
  const entries = readdirSync(bundle).sort();
  run("tar", ["-cf", tarPath, "-C", bundle, ...entries]);
  run("zstd", ["-19", "-q", "-f", tarPath, "-o", archive]);
  rmSync(tarPath, { force: true });
}

function listedArchivePaths(archive, format) {
  const output =
    format === "zip" && process.platform !== "win32"
      ? run("unzip", ["-Z1", archive], { capture: true })
      : run("tar", ["-tf", archive], { capture: true });
  return output.split(/\r?\n/).filter(Boolean);
}

function validateArchivePaths(paths) {
  const seen = new Set();
  for (const original of paths) {
    const path = original.replace(/^\.\//, "").replace(/\/$/, "");
    if (!path) continue;
    validateRelativePath(path);
    if (seen.has(path)) fail(`archive path appears more than once: ${path}`);
    seen.add(path);
  }
}

function extractArchive(archive, destination, format) {
  validateArchivePaths(listedArchivePaths(archive, format));
  mkdirSync(destination, { recursive: true });
  if (format === "zip") {
    if (process.platform === "win32") {
      run("powershell", [
        "-NoProfile",
        "-Command",
        "Expand-Archive -LiteralPath $env:EFFINDOM_ARCHIVE_SOURCE -DestinationPath $env:EFFINDOM_ARCHIVE_DESTINATION",
      ], {
        env: {
          EFFINDOM_ARCHIVE_SOURCE: archive,
          EFFINDOM_ARCHIVE_DESTINATION: destination,
        },
      });
    } else {
      run("unzip", ["-q", archive, "-d", destination]);
    }
    return;
  }
  const tarPath = join(destination, ".runtime.tar");
  run("zstd", ["-d", "-q", "-f", archive, "-o", tarPath]);
  run("tar", ["-xf", tarPath, "-C", destination]);
  rmSync(tarPath, { force: true });
}

function walkFiles(root, directory = root) {
  return readdirSync(directory, { withFileTypes: true }).flatMap((entry) => {
    const path = join(directory, entry.name);
    if (entry.isDirectory()) return walkFiles(root, path);
    if (!entry.isFile()) fail(`unsupported runtime artifact entry: ${path}`);
    return [path.slice(root.length + 1).split(sep).join("/")];
  });
}

export function createNativeRuntimeArtifact(request) {
  if (request.schemaVersion !== NATIVE_RUNTIME_MANIFEST_SCHEMA_VERSION) fail("unsupported artifact request schema");
  validateCommit(request.sourceCommit);
  validateTarget(request.target);
  validateMinimumOs(request.target, request.minimumOs);
  if (!Array.isArray(request.files) || request.files.length === 0) fail("runtime artifact has no inputs");
  const destination = resolve(request.destination);
  if (existsSync(destination)) fail(`runtime artifact destination already exists: ${destination}`);
  mkdirSync(dirname(destination), { recursive: true });
  const staging = mkdtempSync(join(dirname(destination), `.${basename(destination)}-staging-`));
  const bundle = join(staging, "bundle");
  mkdirSync(bundle, { recursive: true });
  try {
    const seen = new Set();
    const files = [];
    for (const input of [...request.files].sort((left, right) => left.path.localeCompare(right.path))) {
      validateRelativePath(input.path);
      if (input.path === NATIVE_RUNTIME_ARTIFACT_MANIFEST || seen.has(input.path)) {
        fail(`duplicate or reserved runtime path: ${input.path}`);
      }
      seen.add(input.path);
      if (!fileRoles.has(input.role)) fail(`invalid runtime file role: ${input.role}`);
      const metadata = lstatSync(input.source);
      if (!metadata.isFile() || metadata.isSymbolicLink()) fail(`runtime input must be a regular file: ${input.source}`);
      const target = join(bundle, ...input.path.split("/"));
      mkdirSync(dirname(target), { recursive: true });
      copyFileSync(input.source, target);
      chmodSync(target, input.executable ? 0o755 : 0o644);
      utimesSync(target, normalizedTimestamp, normalizedTimestamp);
      files.push({
        path: input.path,
        bytes: statSync(target).size,
        sha256: sha256File(target),
        role: input.role,
        ...(input.executable ? { executable: true } : {}),
      });
    }
    const manifest = canonicalBundleManifest({
      schemaVersion: NATIVE_RUNTIME_MANIFEST_SCHEMA_VERSION,
      sourceCommit: request.sourceCommit,
      target: request.target,
      coreAbi: request.coreAbi,
      uiAbi: request.uiAbi,
      minimumOs: request.minimumOs,
      files,
    });
    const manifestBytes = Buffer.from(canonicalJson(manifest));
    const embeddedManifest = join(bundle, NATIVE_RUNTIME_ARTIFACT_MANIFEST);
    writeFileSync(embeddedManifest, manifestBytes);
    chmodSync(embeddedManifest, 0o644);
    utimesSync(embeddedManifest, normalizedTimestamp, normalizedTimestamp);
    const format = archiveFormat(request.target);
    const name = archiveName(request.target);
    const archive = join(staging, name);
    createArchive(bundle, archive, format);
    const archiveSha256 = sha256File(archive);
    writeFileSync(join(staging, `${name}.sha256`), `${archiveSha256}  ${name}\n`);
    writeFileSync(join(staging, NATIVE_RUNTIME_ARTIFACT_MANIFEST), manifestBytes);
    const artifact = {
      target: request.target,
      archive: name,
      archiveFormat: format,
      archiveBytes: statSync(archive).size,
      archiveSha256,
      bundleManifestSha256: sha256Bytes(manifestBytes),
      coreAbi: request.coreAbi,
      uiAbi: request.uiAbi,
      minimumOs: manifest.minimumOs,
      files: manifest.files,
    };
    renameSync(staging, destination);
    return { root: destination, artifact };
  } catch (error) {
    rmSync(staging, { recursive: true, force: true });
    throw error;
  }
}

function validateArtifact(artifact) {
  validateTarget(artifact.target);
  if (artifact.archive !== archiveName(artifact.target)) fail(`invalid archive name for ${artifact.target}`);
  if (artifact.archiveFormat !== archiveFormat(artifact.target)) fail(`invalid archive format for ${artifact.target}`);
  if (!Number.isSafeInteger(artifact.archiveBytes) || artifact.archiveBytes <= 0) fail("runtime archive is empty");
  validateDigest("runtime archive", artifact.archiveSha256);
  validateDigest("bundle manifest", artifact.bundleManifestSha256);
  if (!Number.isInteger(artifact.coreAbi) || artifact.coreAbi <= 0) fail("coreAbi must be positive");
  if (!Number.isInteger(artifact.uiAbi) || artifact.uiAbi <= 0) fail("uiAbi must be positive");
  validateMinimumOs(artifact.target, artifact.minimumOs);
  validateFiles(artifact.files);
}

export function verifyNativeRuntimeArtifact(artifactRoot, descriptor, sourceCommit) {
  validateCommit(sourceCommit);
  validateArtifact(descriptor);
  const archive = join(artifactRoot, descriptor.archive);
  if (statSync(archive).size !== descriptor.archiveBytes) fail(`archive byte length mismatch: ${archive}`);
  if (sha256File(archive) !== descriptor.archiveSha256) fail(`archive checksum mismatch: ${archive}`);
  const extraction = mkdtempSync(join(tmpdir(), "effindom-runtime-verify-"));
  try {
    extractArchive(archive, extraction, descriptor.archiveFormat);
    const manifestPath = join(extraction, NATIVE_RUNTIME_ARTIFACT_MANIFEST);
    const manifestBytes = readFileSync(manifestPath);
    if (sha256Bytes(manifestBytes) !== descriptor.bundleManifestSha256) fail("bundle manifest checksum mismatch");
    const manifest = canonicalBundleManifest(JSON.parse(manifestBytes));
    if (manifest.sourceCommit !== sourceCommit) fail("bundle source commit mismatch");
    const expected = canonicalBundleManifest({
      schemaVersion: NATIVE_RUNTIME_MANIFEST_SCHEMA_VERSION,
      sourceCommit,
      target: descriptor.target,
      coreAbi: descriptor.coreAbi,
      uiAbi: descriptor.uiAbi,
      minimumOs: descriptor.minimumOs,
      files: descriptor.files,
    });
    if (canonicalJson(manifest) !== canonicalJson(expected)) fail("bundle manifest does not match descriptor");
    const actualPaths = walkFiles(extraction).filter((path) => path !== NATIVE_RUNTIME_ARTIFACT_MANIFEST).sort();
    const expectedPaths = descriptor.files.map((file) => file.path).sort();
    if (canonicalJson(actualPaths) !== canonicalJson(expectedPaths)) fail("runtime archive file set mismatch");
    for (const file of descriptor.files) {
      const path = join(extraction, ...file.path.split("/"));
      if (statSync(path).size !== file.bytes || sha256File(path) !== file.sha256) {
        fail(`runtime file verification failed: ${file.path}`);
      }
    }
    return manifest;
  } finally {
    rmSync(extraction, { recursive: true, force: true });
  }
}

export function createNativeRuntimeReleaseManifest({ release, sourceCommit, artifacts }) {
  if (typeof release !== "string" || !/^\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$/.test(release)) {
    fail(`invalid semantic version: ${release}`);
  }
  validateCommit(sourceCommit);
  if (!Array.isArray(artifacts) || artifacts.length !== REQUIRED_NATIVE_RUNTIME_TARGETS.length) {
    fail(`release must contain ${REQUIRED_NATIVE_RUNTIME_TARGETS.length} artifacts`);
  }
  for (const artifact of artifacts) validateArtifact(artifact);
  const sorted = [...artifacts].sort((left, right) => left.target.localeCompare(right.target));
  if (canonicalJson(sorted.map((artifact) => artifact.target)) !== canonicalJson(REQUIRED_NATIVE_RUNTIME_TARGETS)) {
    fail("native runtime target set is incomplete or duplicated");
  }
  return {
    schemaVersion: NATIVE_RUNTIME_MANIFEST_SCHEMA_VERSION,
    release,
    sourceCommit,
    artifacts: sorted.map((artifact) => ({
      ...artifact,
      files: [...artifact.files].sort((left, right) => left.path.localeCompare(right.path)),
    })),
  };
}

export function writeNativeRuntimeReleaseManifest(request, destination) {
  const manifest = createNativeRuntimeReleaseManifest(request);
  writeFileSync(destination, canonicalJson(manifest));
  return manifest;
}
