const platformAliases = new Map([
  ["darwin", "macos"],
  ["linux", "linux"],
  ["win32", "windows"],
]);

const architectureAliases = new Map([
  ["arm64", "arm64"],
  ["x64", "x64"],
]);

export function assertNativeRuntimePackagingHost(
  targetPlatform,
  targetArchitecture,
  hostPlatform = process.platform,
  hostArchitecture = process.arch,
) {
  const normalizedPlatform = platformAliases.get(hostPlatform);
  const normalizedArchitecture = architectureAliases.get(hostArchitecture);
  if (
    normalizedPlatform !== targetPlatform ||
    normalizedArchitecture !== targetArchitecture
  ) {
    throw new Error(
      `Cannot package ${targetPlatform}-${targetArchitecture} runtime artifacts on ` +
        `${hostPlatform}-${hostArchitecture}. Run the packaging step on the target platform and architecture.`,
    );
  }
}
