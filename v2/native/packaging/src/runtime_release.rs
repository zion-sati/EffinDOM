use crate::{Error, Result};
use serde::{Deserialize, Serialize};
use std::collections::BTreeSet;
use std::path::{Component, Path};

pub const NATIVE_RUNTIME_MANIFEST_SCHEMA_VERSION: u32 = 1;

#[derive(Clone, Copy, Debug, Deserialize, Serialize, PartialEq, Eq, PartialOrd, Ord)]
#[serde(rename_all = "kebab-case")]
pub enum NativeRuntimeTarget {
    MacosArm64,
    MacosX64,
    WindowsArm64,
    WindowsX64,
    LinuxArm64,
    LinuxX64,
}

pub const REQUIRED_NATIVE_RUNTIME_TARGETS: [NativeRuntimeTarget; 6] = [
    NativeRuntimeTarget::LinuxArm64,
    NativeRuntimeTarget::LinuxX64,
    NativeRuntimeTarget::MacosArm64,
    NativeRuntimeTarget::MacosX64,
    NativeRuntimeTarget::WindowsArm64,
    NativeRuntimeTarget::WindowsX64,
];

impl NativeRuntimeTarget {
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::MacosArm64 => "macos-arm64",
            Self::MacosX64 => "macos-x64",
            Self::WindowsArm64 => "windows-arm64",
            Self::WindowsX64 => "windows-x64",
            Self::LinuxArm64 => "linux-arm64",
            Self::LinuxX64 => "linux-x64",
        }
    }

    pub const fn archive_format(self) -> NativeRuntimeArchiveFormat {
        match self {
            Self::WindowsArm64 | Self::WindowsX64 => NativeRuntimeArchiveFormat::Zip,
            _ => NativeRuntimeArchiveFormat::TarZstd,
        }
    }

    pub fn archive_name(self) -> String {
        format!(
            "effindom-native-{}.{}",
            self.as_str(),
            self.archive_format().extension()
        )
    }
}

#[derive(Clone, Copy, Debug, Deserialize, Serialize, PartialEq, Eq)]
#[serde(rename_all = "kebab-case")]
pub enum NativeRuntimeArchiveFormat {
    TarZstd,
    Zip,
}

impl NativeRuntimeArchiveFormat {
    pub const fn extension(self) -> &'static str {
        match self {
            Self::TarZstd => "tar.zst",
            Self::Zip => "zip",
        }
    }
}

#[derive(Clone, Copy, Debug, Deserialize, Serialize, PartialEq, Eq, PartialOrd, Ord)]
#[serde(rename_all = "kebab-case")]
pub enum NativeRuntimeFileRole {
    HostLibrary,
    Launcher,
    LinkMetadata,
    Packager,
    RuntimeAsset,
    RuntimeLibrary,
}

#[derive(Clone, Debug, Deserialize, Serialize, PartialEq, Eq)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub struct NativeRuntimeMinimumOs {
    pub family: String,
    pub version: String,
}

#[derive(Clone, Debug, Deserialize, Serialize, PartialEq, Eq)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub struct NativeRuntimeFile {
    pub path: String,
    pub bytes: u64,
    pub sha256: String,
    pub role: NativeRuntimeFileRole,
    #[serde(default, skip_serializing_if = "is_false")]
    pub executable: bool,
}

#[derive(Clone, Debug, Deserialize, Serialize, PartialEq, Eq)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub struct NativeRuntimeBundleManifest {
    pub schema_version: u32,
    pub source_commit: String,
    pub target: NativeRuntimeTarget,
    pub core_abi: u32,
    pub ui_abi: u32,
    pub minimum_os: NativeRuntimeMinimumOs,
    pub files: Vec<NativeRuntimeFile>,
}

#[derive(Clone, Debug, Deserialize, Serialize, PartialEq, Eq)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub struct NativeRuntimeArtifact {
    pub target: NativeRuntimeTarget,
    pub archive: String,
    pub archive_format: NativeRuntimeArchiveFormat,
    pub archive_bytes: u64,
    pub archive_sha256: String,
    pub bundle_manifest_sha256: String,
    pub core_abi: u32,
    pub ui_abi: u32,
    pub minimum_os: NativeRuntimeMinimumOs,
    pub files: Vec<NativeRuntimeFile>,
}

#[derive(Clone, Debug, Deserialize, Serialize, PartialEq, Eq)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub struct NativeRuntimeReleaseManifest {
    pub schema_version: u32,
    pub release: String,
    pub source_commit: String,
    pub artifacts: Vec<NativeRuntimeArtifact>,
}

impl NativeRuntimeBundleManifest {
    pub fn validate(&self) -> Result<()> {
        validate_schema(self.schema_version)?;
        validate_commit(&self.source_commit)?;
        validate_abi(self.core_abi, self.ui_abi)?;
        validate_minimum_os(self.target, &self.minimum_os)?;
        validate_files(&self.files)
    }

    fn canonicalized(mut self) -> Result<Self> {
        self.validate()?;
        self.files.sort_by(|left, right| left.path.cmp(&right.path));
        Ok(self)
    }
}

impl NativeRuntimeReleaseManifest {
    pub fn validate(&self) -> Result<()> {
        validate_schema(self.schema_version)?;
        semver::Version::parse(&self.release).map_err(|source| {
            invalid(format!(
                "release {:?} is not semantic versioning: {source}",
                self.release
            ))
        })?;
        validate_commit(&self.source_commit)?;
        if self.artifacts.len() != REQUIRED_NATIVE_RUNTIME_TARGETS.len() {
            return Err(invalid(format!(
                "release must contain exactly {} target artifacts, found {}",
                REQUIRED_NATIVE_RUNTIME_TARGETS.len(),
                self.artifacts.len()
            )));
        }
        let mut targets = BTreeSet::new();
        let mut archives = BTreeSet::new();
        for artifact in &self.artifacts {
            if !targets.insert(artifact.target) {
                return Err(invalid(format!(
                    "target {} appears more than once",
                    artifact.target.as_str()
                )));
            }
            if !archives.insert(&artifact.archive) {
                return Err(invalid(format!(
                    "archive {:?} appears more than once",
                    artifact.archive
                )));
            }
            validate_artifact(artifact)?;
        }
        let required = REQUIRED_NATIVE_RUNTIME_TARGETS
            .into_iter()
            .collect::<BTreeSet<_>>();
        if targets != required {
            return Err(invalid("release target set is incomplete"));
        }
        Ok(())
    }

    pub fn artifact(&self, target: NativeRuntimeTarget) -> Option<&NativeRuntimeArtifact> {
        self.artifacts
            .iter()
            .find(|artifact| artifact.target == target)
    }

    fn canonicalized(mut self) -> Result<Self> {
        self.validate()?;
        self.artifacts
            .sort_by_key(|artifact| artifact.target.as_str());
        for artifact in &mut self.artifacts {
            artifact
                .files
                .sort_by(|left, right| left.path.cmp(&right.path));
        }
        Ok(self)
    }
}

pub fn encode_native_runtime_bundle_manifest(
    manifest: &NativeRuntimeBundleManifest,
) -> Result<Vec<u8>> {
    encode(&manifest.clone().canonicalized()?)
}

pub fn decode_native_runtime_bundle_manifest(bytes: &[u8]) -> Result<NativeRuntimeBundleManifest> {
    let manifest: NativeRuntimeBundleManifest =
        serde_json::from_slice(bytes).map_err(Error::ParseNativeRuntimeManifest)?;
    manifest.validate()?;
    Ok(manifest)
}

pub fn encode_native_runtime_release_manifest(
    manifest: &NativeRuntimeReleaseManifest,
) -> Result<Vec<u8>> {
    encode(&manifest.clone().canonicalized()?)
}

pub fn decode_native_runtime_release_manifest(
    bytes: &[u8],
) -> Result<NativeRuntimeReleaseManifest> {
    let manifest: NativeRuntimeReleaseManifest =
        serde_json::from_slice(bytes).map_err(Error::ParseNativeRuntimeManifest)?;
    manifest.validate()?;
    Ok(manifest)
}

fn encode<T: Serialize>(value: &T) -> Result<Vec<u8>> {
    let mut bytes =
        serde_json::to_vec_pretty(value).map_err(Error::SerializeNativeRuntimeManifest)?;
    bytes.push(b'\n');
    Ok(bytes)
}

fn validate_artifact(artifact: &NativeRuntimeArtifact) -> Result<()> {
    if artifact.archive != artifact.target.archive_name() {
        return Err(invalid(format!(
            "target {} must use archive {:?}",
            artifact.target.as_str(),
            artifact.target.archive_name()
        )));
    }
    if artifact.archive_format != artifact.target.archive_format() {
        return Err(invalid(format!(
            "target {} uses the wrong archive format",
            artifact.target.as_str()
        )));
    }
    if artifact.archive_bytes == 0 {
        return Err(invalid(format!("archive {:?} is empty", artifact.archive)));
    }
    validate_sha256("archive", &artifact.archive_sha256)?;
    validate_sha256("bundle manifest", &artifact.bundle_manifest_sha256)?;
    validate_abi(artifact.core_abi, artifact.ui_abi)?;
    validate_minimum_os(artifact.target, &artifact.minimum_os)?;
    validate_files(&artifact.files)
}

fn validate_files(files: &[NativeRuntimeFile]) -> Result<()> {
    if files.is_empty() {
        return Err(invalid("runtime artifact contains no files"));
    }
    let mut paths = BTreeSet::new();
    for file in files {
        let path = Path::new(&file.path);
        if file.path.is_empty()
            || path.is_absolute()
            || path.components().any(|component| {
                matches!(
                    component,
                    Component::ParentDir | Component::RootDir | Component::Prefix(_)
                )
            })
        {
            return Err(invalid(format!(
                "runtime file path {:?} must be safe and relative",
                file.path
            )));
        }
        if !paths.insert(&file.path) {
            return Err(invalid(format!(
                "runtime file path {:?} appears more than once",
                file.path
            )));
        }
        validate_sha256(&format!("runtime file {:?}", file.path), &file.sha256)?;
    }
    Ok(())
}

fn validate_schema(schema: u32) -> Result<()> {
    if schema != NATIVE_RUNTIME_MANIFEST_SCHEMA_VERSION {
        return Err(invalid(format!(
            "schema {schema} is unsupported; expected {NATIVE_RUNTIME_MANIFEST_SCHEMA_VERSION}"
        )));
    }
    Ok(())
}

fn validate_commit(commit: &str) -> Result<()> {
    if !matches!(commit.len(), 40 | 64)
        || !commit
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
    {
        return Err(invalid(format!(
            "source commit {commit:?} must be a lowercase 40- or 64-character hexadecimal digest"
        )));
    }
    Ok(())
}

fn validate_sha256(name: &str, value: &str) -> Result<()> {
    if value.len() != 64
        || !value
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
    {
        return Err(invalid(format!(
            "{name} SHA-256 {value:?} must be 64 lowercase hexadecimal characters"
        )));
    }
    Ok(())
}

fn validate_abi(core: u32, ui: u32) -> Result<()> {
    if core == 0 || ui == 0 {
        return Err(invalid("Core and UI ABI versions must be non-zero"));
    }
    Ok(())
}

fn validate_minimum_os(
    target: NativeRuntimeTarget,
    minimum: &NativeRuntimeMinimumOs,
) -> Result<()> {
    let expected = match target {
        NativeRuntimeTarget::MacosArm64 | NativeRuntimeTarget::MacosX64 => "macos",
        NativeRuntimeTarget::WindowsArm64 | NativeRuntimeTarget::WindowsX64 => "windows",
        NativeRuntimeTarget::LinuxArm64 | NativeRuntimeTarget::LinuxX64 => "glibc",
    };
    if minimum.family != expected || minimum.version.trim().is_empty() {
        return Err(invalid(format!(
            "target {} requires non-empty minimum OS family {expected:?}",
            target.as_str()
        )));
    }
    Ok(())
}

fn invalid(message: impl Into<String>) -> Error {
    Error::InvalidNativeRuntimeManifest(message.into())
}

fn is_false(value: &bool) -> bool {
    !*value
}
