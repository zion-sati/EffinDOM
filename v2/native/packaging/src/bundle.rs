use crate::{Error, Result};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::collections::BTreeMap;
use std::fs::{self, File};
use std::io::{self, Read};
use std::path::{Component, Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};

const PACKAGE_RECORD_SCHEMA_VERSION: u32 = 2;
static NEXT_STAGING_ID: AtomicU64 = AtomicU64::new(1);

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum OverwritePolicy {
    Reject,
    Replace,
}

#[derive(Clone, Copy, Debug, Deserialize, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub enum BundleFileRole {
    ApplicationExecutable,
    EffinDomRuntimeLibrary,
    ThirdPartyLibrary,
    RuntimeResource,
    ApplicationResource,
    MetadataArtifact,
}

#[derive(Clone, Copy, Debug, Deserialize, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub enum PackageOperatingSystem {
    MacOs,
    Windows,
    Linux,
}

#[derive(Clone, Copy, Debug, Deserialize, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub enum PackageArchitecture {
    Arm64,
    X64,
    Universal,
}

#[derive(Clone, Copy, Debug, Deserialize, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub enum PackageBuildMode {
    Debug,
    Release,
}

#[derive(Clone, Debug, Deserialize, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct PackageMetadata {
    pub application_identifier: String,
    pub application_version: String,
    pub operating_system: PackageOperatingSystem,
    pub architecture: PackageArchitecture,
    pub target_triple: String,
    pub build_mode: PackageBuildMode,
    pub core_abi: u32,
    pub ui_abi: u32,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct BundleFile {
    pub source: PathBuf,
    pub destination: PathBuf,
}

impl BundleFile {
    pub fn new(source: impl Into<PathBuf>, destination: impl Into<PathBuf>) -> Self {
        Self {
            source: source.into(),
            destination: destination.into(),
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PackagingInputs {
    pub destination: PathBuf,
    pub package_record: PathBuf,
    pub metadata: PackageMetadata,
    pub application_executable: BundleFile,
    pub effindom_runtime_libraries: Vec<BundleFile>,
    pub third_party_libraries: Vec<BundleFile>,
    pub runtime_resources: Vec<BundleFile>,
    pub application_resources: Vec<BundleFile>,
    pub metadata_artifacts: Vec<BundleFile>,
}

#[derive(Clone, Debug, Deserialize, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct PackageRecord {
    pub schema_version: u32,
    pub metadata: PackageMetadata,
    pub files: Vec<PackageRecordFile>,
}

#[derive(Clone, Debug, Deserialize, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct PackageRecordFile {
    pub path: String,
    pub role: BundleFileRole,
    pub byte_length: u64,
    pub sha256: String,
}

#[derive(Clone)]
struct ValidatedEntry {
    source: PathBuf,
    destination: PathBuf,
    record_path: String,
    role: BundleFileRole,
}

struct ValidatedInputs {
    destination: PathBuf,
    package_record: PathBuf,
    metadata: PackageMetadata,
    entries: Vec<ValidatedEntry>,
}

pub fn stage_bundle(inputs: &PackagingInputs, overwrite: OverwritePolicy) -> Result<PackageRecord> {
    let validated = validate_inputs(inputs, overwrite)?;
    let staging = unique_sibling(&validated.destination, "staging")?;
    create_dir_all(&staging, "create bundle staging directory")?;

    let result = stage_validated_bundle(&validated, &staging).and_then(|record| {
        publish_staging(&staging, &validated.destination, overwrite, || Ok(()))?;
        Ok(record)
    });
    if result.is_err() {
        let _ = fs::remove_dir_all(&staging);
    }
    result
}

pub fn verify_bundle(
    bundle_root: impl AsRef<Path>,
    package_record: impl AsRef<Path>,
) -> Result<PackageRecord> {
    let package_record = validate_relative_path(package_record.as_ref())?;
    let root = fs::canonicalize(bundle_root.as_ref()).map_err(|source| Error::BundleIo {
        operation: "resolve bundle root",
        path: bundle_root.as_ref().to_path_buf(),
        source,
    })?;
    let record_path = root.join(&package_record);
    let bytes = fs::read(&record_path).map_err(|source| Error::BundleIo {
        operation: "read package record",
        path: record_path.clone(),
        source,
    })?;
    let record: PackageRecord =
        serde_json::from_slice(&bytes).map_err(|source| Error::ParsePackageRecord {
            path: record_path,
            source,
        })?;
    if record.schema_version != PACKAGE_RECORD_SCHEMA_VERSION {
        return Err(Error::UnsupportedPackageRecordSchema(record.schema_version));
    }
    for file in &record.files {
        let relative = validate_relative_path(Path::new(&file.path))?;
        let candidate = root.join(&relative);
        let canonical = fs::canonicalize(&candidate)
            .map_err(|_| Error::PackageRecordArtifactMissing(relative.clone()))?;
        if !canonical.starts_with(&root) {
            return Err(Error::InvalidBundlePath {
                path: relative,
                message: "must remain inside the bundle root",
            });
        }
        let actual_length = fs::metadata(&canonical)
            .map_err(|source| Error::BundleIo {
                operation: "inspect packaged artifact",
                path: canonical.clone(),
                source,
            })?
            .len();
        if actual_length != file.byte_length {
            return Err(Error::PackageRecordLengthMismatch {
                path: relative,
                expected: file.byte_length,
                actual: actual_length,
            });
        }
        let actual_sha256 = sha256_file(&canonical)?;
        if actual_sha256 != file.sha256 {
            return Err(Error::PackageRecordChecksumMismatch {
                path: relative,
                expected: file.sha256.clone(),
                actual: actual_sha256,
            });
        }
    }
    Ok(record)
}

fn validate_inputs(
    inputs: &PackagingInputs,
    overwrite: OverwritePolicy,
) -> Result<ValidatedInputs> {
    let destination = absolute_destination(&inputs.destination)?;
    let package_record = validate_relative_path(&inputs.package_record)?;
    let mut entries = Vec::new();
    push_entry(
        &mut entries,
        &inputs.application_executable,
        BundleFileRole::ApplicationExecutable,
        &destination,
    )?;
    for entry in &inputs.effindom_runtime_libraries {
        push_entry(
            &mut entries,
            entry,
            BundleFileRole::EffinDomRuntimeLibrary,
            &destination,
        )?;
    }
    for entry in &inputs.third_party_libraries {
        push_entry(
            &mut entries,
            entry,
            BundleFileRole::ThirdPartyLibrary,
            &destination,
        )?;
    }
    for entry in &inputs.runtime_resources {
        push_entry(
            &mut entries,
            entry,
            BundleFileRole::RuntimeResource,
            &destination,
        )?;
    }
    for entry in &inputs.application_resources {
        push_entry(
            &mut entries,
            entry,
            BundleFileRole::ApplicationResource,
            &destination,
        )?;
    }
    for entry in &inputs.metadata_artifacts {
        push_entry(
            &mut entries,
            entry,
            BundleFileRole::MetadataArtifact,
            &destination,
        )?;
    }

    entries.sort_by(|left, right| left.record_path.cmp(&right.record_path));
    let mut destinations = BTreeMap::new();
    destinations.insert(path_key(&package_record)?, ());
    for entry in &entries {
        if destinations.insert(entry.record_path.clone(), ()).is_some() {
            return Err(Error::DuplicateBundlePath(entry.destination.clone()));
        }
    }
    if destination.exists() && overwrite == OverwritePolicy::Reject {
        return Err(Error::BundleOutputExists(destination));
    }
    Ok(ValidatedInputs {
        destination,
        package_record,
        metadata: inputs.metadata.clone(),
        entries,
    })
}

fn push_entry(
    entries: &mut Vec<ValidatedEntry>,
    entry: &BundleFile,
    role: BundleFileRole,
    output: &Path,
) -> Result<()> {
    let destination = validate_relative_path(&entry.destination)?;
    let metadata = fs::symlink_metadata(&entry.source).map_err(|source| {
        if source.kind() == io::ErrorKind::NotFound {
            Error::MissingBundleInput(entry.source.clone())
        } else {
            Error::BundleIo {
                operation: "inspect bundle input",
                path: entry.source.clone(),
                source,
            }
        }
    })?;
    if !metadata.file_type().is_file() || metadata.file_type().is_symlink() {
        return Err(Error::UnsupportedBundleInput(entry.source.clone()));
    }
    let source = fs::canonicalize(&entry.source).map_err(|source| Error::BundleIo {
        operation: "resolve bundle input",
        path: entry.source.clone(),
        source,
    })?;
    if source.starts_with(output) {
        return Err(Error::SourceOutputOverlap {
            source,
            destination: output.to_path_buf(),
        });
    }
    entries.push(ValidatedEntry {
        source,
        record_path: path_key(&destination)?,
        destination,
        role,
    });
    Ok(())
}

fn stage_validated_bundle(inputs: &ValidatedInputs, staging: &Path) -> Result<PackageRecord> {
    let mut files = Vec::with_capacity(inputs.entries.len());
    for entry in &inputs.entries {
        let target = staging.join(&entry.destination);
        let parent = target.parent().ok_or_else(|| Error::InvalidBundlePath {
            path: entry.destination.clone(),
            message: "must have a parent directory",
        })?;
        create_dir_all(parent, "create bundle payload directory")?;
        fs::copy(&entry.source, &target).map_err(|source| Error::BundleIo {
            operation: "copy bundle input",
            path: entry.source.clone(),
            source,
        })?;
        let byte_length = fs::metadata(&target)
            .map_err(|source| Error::BundleIo {
                operation: "inspect staged bundle file",
                path: target.clone(),
                source,
            })?
            .len();
        files.push(PackageRecordFile {
            path: entry.record_path.clone(),
            role: entry.role,
            byte_length,
            sha256: sha256_file(&target)?,
        });
    }
    let record = PackageRecord {
        schema_version: PACKAGE_RECORD_SCHEMA_VERSION,
        metadata: inputs.metadata.clone(),
        files,
    };
    let record_path = staging.join(&inputs.package_record);
    let parent = record_path
        .parent()
        .ok_or_else(|| Error::InvalidBundlePath {
            path: inputs.package_record.clone(),
            message: "must have a parent directory",
        })?;
    create_dir_all(parent, "create package record directory")?;
    let mut json = serde_json::to_vec_pretty(&record).map_err(Error::SerializePackageRecord)?;
    json.push(b'\n');
    fs::write(&record_path, json).map_err(|source| Error::BundleIo {
        operation: "write package record",
        path: record_path,
        source,
    })?;
    Ok(record)
}

fn publish_staging<F>(
    staging: &Path,
    destination: &Path,
    overwrite: OverwritePolicy,
    before_publish: F,
) -> Result<()>
where
    F: FnOnce() -> io::Result<()>,
{
    if overwrite == OverwritePolicy::Replace && destination.exists() {
        let backup = unique_sibling(destination, "backup")?;
        fs::rename(destination, &backup).map_err(|source| Error::PublishBundle {
            from: destination.to_path_buf(),
            to: backup.clone(),
            source,
        })?;
        if let Err(source) = before_publish().and_then(|()| fs::rename(staging, destination)) {
            fs::rename(&backup, destination).map_err(|rollback| Error::RollbackBundle {
                backup,
                destination: destination.to_path_buf(),
                source: rollback,
            })?;
            return Err(Error::PublishBundle {
                from: staging.to_path_buf(),
                to: destination.to_path_buf(),
                source,
            });
        }
        fs::remove_dir_all(&backup).map_err(|source| Error::BundleIo {
            operation: "remove replaced bundle backup",
            path: backup,
            source,
        })?;
        return Ok(());
    }
    before_publish().map_err(|source| Error::PublishBundle {
        from: staging.to_path_buf(),
        to: destination.to_path_buf(),
        source,
    })?;
    fs::rename(staging, destination).map_err(|source| Error::PublishBundle {
        from: staging.to_path_buf(),
        to: destination.to_path_buf(),
        source,
    })
}

fn absolute_destination(destination: &Path) -> Result<PathBuf> {
    let name = destination
        .file_name()
        .ok_or_else(|| Error::InvalidBundlePath {
            path: destination.to_path_buf(),
            message: "must name a bundle directory",
        })?;
    let parent = destination.parent().unwrap_or_else(|| Path::new("."));
    let parent = fs::canonicalize(parent).map_err(|source| Error::BundleIo {
        operation: "resolve bundle output parent",
        path: parent.to_path_buf(),
        source,
    })?;
    Ok(parent.join(name))
}

fn validate_relative_path(path: &Path) -> Result<PathBuf> {
    if path.as_os_str().is_empty() || path.is_absolute() {
        return Err(Error::InvalidBundlePath {
            path: path.to_path_buf(),
            message: "must be a non-empty relative path",
        });
    }
    let mut normalized = PathBuf::new();
    for component in path.components() {
        match component {
            Component::Normal(value) if value.to_str().is_some() => normalized.push(value),
            _ => {
                return Err(Error::InvalidBundlePath {
                    path: path.to_path_buf(),
                    message: "must contain only UTF-8 normal path components",
                });
            }
        }
    }
    Ok(normalized)
}

fn path_key(path: &Path) -> Result<String> {
    let mut parts = Vec::new();
    for component in path.components() {
        match component {
            Component::Normal(value) => {
                parts.push(value.to_str().ok_or_else(|| Error::InvalidBundlePath {
                    path: path.to_path_buf(),
                    message: "must be valid UTF-8",
                })?)
            }
            _ => {
                return Err(Error::InvalidBundlePath {
                    path: path.to_path_buf(),
                    message: "must be relative and normalized",
                });
            }
        }
    }
    Ok(parts.join("/"))
}

fn create_dir_all(path: &Path, operation: &'static str) -> Result<()> {
    fs::create_dir_all(path).map_err(|source| Error::BundleIo {
        operation,
        path: path.to_path_buf(),
        source,
    })
}

fn unique_sibling(destination: &Path, kind: &str) -> Result<PathBuf> {
    let parent = destination
        .parent()
        .ok_or_else(|| Error::InvalidBundlePath {
            path: destination.to_path_buf(),
            message: "must have a parent directory",
        })?;
    let name = destination
        .file_name()
        .and_then(|name| name.to_str())
        .ok_or_else(|| Error::InvalidBundlePath {
            path: destination.to_path_buf(),
            message: "must have a UTF-8 file name",
        })?;
    loop {
        let id = NEXT_STAGING_ID.fetch_add(1, Ordering::Relaxed);
        let candidate = parent.join(format!(
            ".{name}.effindom-{kind}-{}-{id}",
            std::process::id()
        ));
        if !candidate.exists() {
            return Ok(candidate);
        }
    }
}

fn sha256_file(path: &Path) -> Result<String> {
    let mut file = File::open(path).map_err(|source| Error::BundleIo {
        operation: "open staged bundle file for hashing",
        path: path.to_path_buf(),
        source,
    })?;
    let mut digest = Sha256::new();
    let mut buffer = [0u8; 64 * 1024];
    loop {
        let count = file.read(&mut buffer).map_err(|source| Error::BundleIo {
            operation: "hash staged bundle file",
            path: path.to_path_buf(),
            source,
        })?;
        if count == 0 {
            break;
        }
        digest.update(&buffer[..count]);
    }
    Ok(digest
        .finalize()
        .iter()
        .map(|byte| format!("{byte:02x}"))
        .collect())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn failed_replacement_restores_the_previous_bundle() {
        let root = std::env::temp_dir().join(format!(
            "effindom-bundle-rollback-{}-{}",
            std::process::id(),
            NEXT_STAGING_ID.fetch_add(1, Ordering::Relaxed)
        ));
        let destination = root.join("app");
        let staging = root.join("staging");
        fs::create_dir_all(&destination).expect("create destination");
        fs::create_dir_all(&staging).expect("create staging");
        fs::write(destination.join("old.txt"), "old").expect("write old bundle");
        fs::write(staging.join("new.txt"), "new").expect("write staged bundle");

        let error = publish_staging(&staging, &destination, OverwritePolicy::Replace, || {
            Err(io::Error::other("injected publication failure"))
        })
        .expect_err("publication must fail");
        assert!(matches!(error, Error::PublishBundle { .. }));
        assert_eq!(
            fs::read_to_string(destination.join("old.txt")).expect("read restored bundle"),
            "old"
        );
        assert!(!destination.join("new.txt").exists());
        fs::remove_dir_all(root).expect("remove rollback fixture");
    }
}
