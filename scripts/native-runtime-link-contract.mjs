const contracts = Object.freeze({
  linux: Object.freeze({
    staticLibraries: Object.freeze(["cpr", "curl", "ssl", "crypto"]),
    systemLibraries: Object.freeze(["z"]),
  }),
  macos: Object.freeze({
    staticLibraries: Object.freeze(["cpr", "curl"]),
    systemLibraries: Object.freeze([
      "Security.framework",
      "SystemConfiguration.framework",
    ]),
  }),
  windows: Object.freeze({
    staticLibraries: Object.freeze(["cpr", "curl"]),
    systemLibraries: Object.freeze(["crypt32", "secur32", "bcrypt", "advapi32"]),
  }),
});

export function nativeHttpLinkContract(platform) {
  const contract = contracts[platform];
  if (contract === undefined) {
    throw new Error(`Unsupported native HTTP link platform: ${platform}.`);
  }
  return contract;
}
