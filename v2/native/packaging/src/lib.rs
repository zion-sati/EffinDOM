mod archive;
mod bundle;
mod distribution;
mod error;
mod icon_encoding;
mod icon_source;
mod metadata;
mod platform_layout;
mod runtime_artifact;
mod runtime_release;
mod universal_macos;

pub use archive::{
    create_release_archive, extract_release_archive, ReleaseArchiveArtifact, ReleaseArchiveFormat,
    ReleaseArchiveSpec,
};
pub use bundle::{
    stage_bundle, verify_bundle, BundleFile, BundleFileRole, OverwritePolicy, PackageArchitecture,
    PackageBuildMode, PackageMetadata, PackageOperatingSystem, PackageRecord, PackageRecordFile,
    PackagingInputs,
};
pub use distribution::{
    create_appimage, create_dmg, create_msix, AppImageInputs, DistributionArtifact, DmgInputs,
    MsixInputs,
};
pub use error::{Error, Result};
pub use icon_encoding::{
    encode_browser_favicon, encode_linux_hicolor, encode_macos_icns, encode_windows_ico,
    IconArtifact,
};
pub use icon_source::{
    load_icon_source, IconAlpha, IconQualityWarning, IconRaster, IconRasterSet, IconSource,
    IconSourceFormat, CANONICAL_ICON_SIZES, MINIMUM_PNG_ICON_SIZE, RECOMMENDED_PNG_ICON_SIZE,
};
pub use metadata::{
    encode_application_icon_png, encode_linux_desktop_entry, encode_macos_info_plist,
    encode_windows_resource_script, ApplicationMetadata, NativeVersion,
};
pub use platform_layout::{NativeLibrarySearch, NativePackageLayout, NativePlatform};
pub use runtime_artifact::{
    create_native_runtime_artifact, extract_native_runtime_artifact,
    verify_native_runtime_directory, NativeRuntimeArtifactInput, NativeRuntimeArtifactOutput,
    NativeRuntimeArtifactRequest, NATIVE_RUNTIME_ARTIFACT_MANIFEST,
};
pub use runtime_release::{
    decode_native_runtime_bundle_manifest, decode_native_runtime_release_manifest,
    encode_native_runtime_bundle_manifest, encode_native_runtime_release_manifest,
    NativeRuntimeArchiveFormat, NativeRuntimeArtifact, NativeRuntimeBundleManifest,
    NativeRuntimeFile, NativeRuntimeFileRole, NativeRuntimeMinimumOs, NativeRuntimeReleaseManifest,
    NativeRuntimeTarget, NATIVE_RUNTIME_MANIFEST_SCHEMA_VERSION, REQUIRED_NATIVE_RUNTIME_TARGETS,
};
pub use universal_macos::{
    assemble_universal_macos_app, UniversalMacOsArtifact, UniversalMacOsInputs,
};
