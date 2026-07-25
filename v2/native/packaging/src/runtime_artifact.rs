use crate::archive::{
    absolute_output, create_dir_all, decode_tar_zstd, decode_zip, encode_tar_zstd, encode_zip,
    publish_directory, sha256_file, unique_sibling, validate_relative_path,
};
use crate::{
    decode_native_runtime_bundle_manifest, encode_native_runtime_bundle_manifest, Error,
    NativeRuntimeArchiveFormat, NativeRuntimeArtifact, NativeRuntimeBundleManifest,
    NativeRuntimeFile, NativeRuntimeFileRole, NativeRuntimeMinimumOs, NativeRuntimeTarget,
    OverwritePolicy, Result, NATIVE_RUNTIME_MANIFEST_SCHEMA_VERSION,
};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::collections::BTreeSet;
use std::fs;
use std::io;
use std::path::{Path, PathBuf};

pub const NATIVE_RUNTIME_ARTIFACT_MANIFEST: &str = "native-runtime-artifact.json";

#[derive(Clone, Debug, Deserialize, Serialize, PartialEq, Eq)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub struct NativeRuntimeArtifactInput {
    pub source: PathBuf,
    pub path: String,
    pub role: NativeRuntimeFileRole,
    #[serde(default)]
    pub executable: bool,
}

#[derive(Clone, Debug, Deserialize, Serialize, PartialEq, Eq)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub struct NativeRuntimeArtifactRequest {
    pub schema_version: u32,
    pub source_commit: String,
    pub target: NativeRuntimeTarget,
    pub core_abi: u32,
    pub ui_abi: u32,
    pub minimum_os: NativeRuntimeMinimumOs,
    pub destination: PathBuf,
    pub files: Vec<NativeRuntimeArtifactInput>,
}

#[derive(Clone, Debug, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct NativeRuntimeArtifactOutput {
    pub root: PathBuf,
    pub archive: PathBuf,
    pub checksum: PathBuf,
    pub bundle_manifest: PathBuf,
    pub artifact: NativeRuntimeArtifact,
}

pub fn create_native_runtime_artifact(
    request: &NativeRuntimeArtifactRequest,
    overwrite: OverwritePolicy,
) -> Result<NativeRuntimeArtifactOutput> {
    if request.schema_version != NATIVE_RUNTIME_MANIFEST_SCHEMA_VERSION {
        return Err(Error::InvalidNativeRuntimeManifest(format!(
            "artifact request schema {} is unsupported; expected {}",
            request.schema_version, NATIVE_RUNTIME_MANIFEST_SCHEMA_VERSION
        )));
    }
    let destination = absolute_output(&request.destination)?;
    if destination.exists() && overwrite == OverwritePolicy::Reject {
        return Err(Error::ArchiveOutputExists(destination));
    }
    let staging = unique_sibling(&destination, "runtime-artifact")?;
    let bundle = staging.join("bundle");
    create_dir_all(&bundle, "create runtime artifact staging directory")?;

    let result = (|| {
        let mut paths = BTreeSet::new();
        let mut files = Vec::with_capacity(request.files.len());
        for input in &request.files {
            let relative = Path::new(&input.path);
            validate_relative_path(relative)?;
            if input.path == NATIVE_RUNTIME_ARTIFACT_MANIFEST || !paths.insert(input.path.clone()) {
                return Err(Error::DuplicateBundlePath(relative.to_path_buf()));
            }
            let metadata = fs::symlink_metadata(&input.source).map_err(|source| {
                if source.kind() == io::ErrorKind::NotFound {
                    Error::MissingBundleInput(input.source.clone())
                } else {
                    Error::BundleIo {
                        operation: "inspect native runtime input",
                        path: input.source.clone(),
                        source,
                    }
                }
            })?;
            if !metadata.file_type().is_file() || metadata.file_type().is_symlink() {
                return Err(Error::UnsupportedBundleInput(input.source.clone()));
            }
            let source = fs::canonicalize(&input.source).map_err(|source| Error::BundleIo {
                operation: "resolve native runtime input",
                path: input.source.clone(),
                source,
            })?;
            if source.starts_with(&destination) || destination.starts_with(&source) {
                return Err(Error::SourceOutputOverlap {
                    source,
                    destination: destination.clone(),
                });
            }
            let target = bundle.join(relative);
            create_dir_all(
                target.parent().expect("validated file path has a parent"),
                "create native runtime artifact directory",
            )?;
            fs::copy(&source, &target).map_err(|source| Error::BundleIo {
                operation: "copy native runtime input",
                path: input.source.clone(),
                source,
            })?;
            set_executable(&target, input.executable)?;
            files.push(NativeRuntimeFile {
                path: input.path.clone(),
                bytes: fs::metadata(&target)
                    .map_err(|source| Error::BundleIo {
                        operation: "inspect staged native runtime input",
                        path: target.clone(),
                        source,
                    })?
                    .len(),
                sha256: sha256_file(&target)?,
                role: input.role,
                executable: input.executable,
            });
        }

        files.sort_by(|left, right| left.path.cmp(&right.path));
        let bundle_record = NativeRuntimeBundleManifest {
            schema_version: NATIVE_RUNTIME_MANIFEST_SCHEMA_VERSION,
            source_commit: request.source_commit.clone(),
            target: request.target,
            core_abi: request.core_abi,
            ui_abi: request.ui_abi,
            minimum_os: request.minimum_os.clone(),
            files,
        };
        let record_bytes = encode_native_runtime_bundle_manifest(&bundle_record)?;
        let record_path = bundle.join(NATIVE_RUNTIME_ARTIFACT_MANIFEST);
        fs::write(&record_path, &record_bytes).map_err(|source| Error::BundleIo {
            operation: "write native runtime artifact manifest",
            path: record_path,
            source,
        })?;
        let bundle_manifest_sha256 = sha256_bytes(&record_bytes);
        let archive_name = request.target.archive_name();
        let archive = staging.join(&archive_name);
        match request.target.archive_format() {
            NativeRuntimeArchiveFormat::TarZstd => encode_tar_zstd(&bundle, &archive)?,
            NativeRuntimeArchiveFormat::Zip => encode_zip(&bundle, &archive)?,
        }
        let archive_sha256 = sha256_file(&archive)?;
        let checksum = staging.join(format!("{archive_name}.sha256"));
        fs::write(&checksum, format!("{archive_sha256}  {archive_name}\n")).map_err(|source| {
            Error::ArchiveIo {
                operation: "write native runtime artifact checksum",
                path: checksum.clone(),
                source,
            }
        })?;
        let sidecar = staging.join(NATIVE_RUNTIME_ARTIFACT_MANIFEST);
        fs::write(&sidecar, &record_bytes).map_err(|source| Error::BundleIo {
            operation: "write native runtime artifact sidecar",
            path: sidecar,
            source,
        })?;
        let artifact = NativeRuntimeArtifact {
            target: request.target,
            archive: archive_name,
            archive_format: request.target.archive_format(),
            archive_bytes: fs::metadata(&archive)
                .map_err(|source| Error::ArchiveIo {
                    operation: "inspect native runtime archive",
                    path: archive,
                    source,
                })?
                .len(),
            archive_sha256,
            bundle_manifest_sha256,
            core_abi: request.core_abi,
            ui_abi: request.ui_abi,
            minimum_os: request.minimum_os.clone(),
            files: bundle_record.files,
        };
        publish_directory(&staging, &destination, overwrite)?;
        Ok(NativeRuntimeArtifactOutput {
            archive: destination.join(&artifact.archive),
            checksum: destination.join(format!("{}.sha256", artifact.archive)),
            bundle_manifest: destination.join(NATIVE_RUNTIME_ARTIFACT_MANIFEST),
            root: destination,
            artifact,
        })
    })();
    if result.is_err() {
        let _ = fs::remove_dir_all(staging);
    }
    result
}

pub fn extract_native_runtime_artifact(
    artifact_root: impl AsRef<Path>,
    destination: impl AsRef<Path>,
    source_commit: &str,
    expected: &NativeRuntimeArtifact,
    overwrite: OverwritePolicy,
) -> Result<NativeRuntimeBundleManifest> {
    let artifact_root =
        fs::canonicalize(artifact_root.as_ref()).map_err(|source| Error::ArchiveIo {
            operation: "resolve native runtime artifact root",
            path: artifact_root.as_ref().to_path_buf(),
            source,
        })?;
    let destination = absolute_output(destination.as_ref())?;
    if destination.exists() && overwrite == OverwritePolicy::Reject {
        return Err(Error::BundleOutputExists(destination));
    }
    let archive = artifact_root.join(&expected.archive);
    let actual_archive_sha256 = sha256_file(&archive)?;
    if actual_archive_sha256 != expected.archive_sha256 {
        return Err(Error::ArchiveChecksumMismatch {
            path: archive,
            expected: expected.archive_sha256.clone(),
            actual: actual_archive_sha256,
        });
    }
    let staging = unique_sibling(&destination, "runtime-extract")?;
    create_dir_all(&staging, "create runtime extraction staging directory")?;
    let result = (|| {
        match expected.archive_format {
            NativeRuntimeArchiveFormat::TarZstd => decode_tar_zstd(&archive, &staging)?,
            NativeRuntimeArchiveFormat::Zip => decode_zip(&archive, &staging)?,
        }
        let record_path = staging.join(NATIVE_RUNTIME_ARTIFACT_MANIFEST);
        let record_bytes = fs::read(&record_path).map_err(|source| Error::BundleIo {
            operation: "read extracted native runtime manifest",
            path: record_path,
            source,
        })?;
        let record_sha256 = sha256_bytes(&record_bytes);
        if record_sha256 != expected.bundle_manifest_sha256 {
            return Err(Error::PackageRecordChecksumMismatch {
                path: PathBuf::from(NATIVE_RUNTIME_ARTIFACT_MANIFEST),
                expected: expected.bundle_manifest_sha256.clone(),
                actual: record_sha256,
            });
        }
        let record = decode_native_runtime_bundle_manifest(&record_bytes)?;
        if record.source_commit != source_commit
            || record.target != expected.target
            || record.core_abi != expected.core_abi
            || record.ui_abi != expected.ui_abi
            || record.minimum_os != expected.minimum_os
            || record.files != expected.files
        {
            return Err(Error::InvalidNativeRuntimeManifest(
                "embedded artifact manifest does not match the release manifest".to_string(),
            ));
        }
        verify_extracted_files(&staging, &record)?;
        publish_directory(&staging, &destination, overwrite)?;
        Ok(record)
    })();
    if result.is_err() {
        let _ = fs::remove_dir_all(staging);
    }
    result
}

pub fn verify_native_runtime_directory(
    root: impl AsRef<Path>,
) -> Result<NativeRuntimeBundleManifest> {
    let root = root.as_ref();
    let record_path = root.join(NATIVE_RUNTIME_ARTIFACT_MANIFEST);
    let record =
        decode_native_runtime_bundle_manifest(&fs::read(&record_path).map_err(|source| {
            Error::BundleIo {
                operation: "read native runtime directory manifest",
                path: record_path,
                source,
            }
        })?)?;
    verify_extracted_files(root, &record)?;
    Ok(record)
}

fn verify_extracted_files(root: &Path, record: &NativeRuntimeBundleManifest) -> Result<()> {
    let mut expected = record
        .files
        .iter()
        .map(|file| file.path.as_str())
        .collect::<BTreeSet<_>>();
    for file in &record.files {
        let path = root.join(&file.path);
        let metadata = fs::symlink_metadata(&path)
            .map_err(|_| Error::PackageRecordArtifactMissing(PathBuf::from(&file.path)))?;
        if !metadata.file_type().is_file() || metadata.file_type().is_symlink() {
            return Err(Error::UnsupportedArchiveEntry(PathBuf::from(&file.path)));
        }
        if metadata.len() != file.bytes {
            return Err(Error::PackageRecordLengthMismatch {
                path: PathBuf::from(&file.path),
                expected: file.bytes,
                actual: metadata.len(),
            });
        }
        let actual = sha256_file(&path)?;
        if actual != file.sha256 {
            return Err(Error::PackageRecordChecksumMismatch {
                path: PathBuf::from(&file.path),
                expected: file.sha256.clone(),
                actual,
            });
        }
    }
    verify_no_extra_files(root, root, &mut expected)?;
    if !expected.is_empty() {
        return Err(Error::InvalidNativeRuntimeManifest(
            "runtime artifact did not contain every recorded file".to_string(),
        ));
    }
    Ok(())
}

fn verify_no_extra_files(
    root: &Path,
    directory: &Path,
    expected: &mut BTreeSet<&str>,
) -> Result<()> {
    for entry in fs::read_dir(directory).map_err(|source| Error::BundleIo {
        operation: "read extracted native runtime directory",
        path: directory.to_path_buf(),
        source,
    })? {
        let entry = entry.map_err(|source| Error::BundleIo {
            operation: "read extracted native runtime entry",
            path: directory.to_path_buf(),
            source,
        })?;
        let path = entry.path();
        if path.is_dir() {
            verify_no_extra_files(root, &path, expected)?;
        } else {
            let relative = path
                .strip_prefix(root)
                .expect("entry remains below extraction root");
            let key = relative
                .components()
                .map(|component| component.as_os_str().to_string_lossy())
                .collect::<Vec<_>>()
                .join("/");
            if key == NATIVE_RUNTIME_ARTIFACT_MANIFEST {
                continue;
            }
            if !expected.remove(key.as_str()) {
                return Err(Error::UnsupportedArchiveEntry(relative.to_path_buf()));
            }
        }
    }
    Ok(())
}

fn sha256_bytes(bytes: &[u8]) -> String {
    Sha256::digest(bytes)
        .iter()
        .map(|byte| format!("{byte:02x}"))
        .collect()
}

#[cfg(unix)]
fn set_executable(path: &Path, executable: bool) -> Result<()> {
    use std::os::unix::fs::PermissionsExt;
    let mode = if executable { 0o755 } else { 0o644 };
    fs::set_permissions(path, fs::Permissions::from_mode(mode)).map_err(|source| Error::BundleIo {
        operation: "set native runtime input permissions",
        path: path.to_path_buf(),
        source,
    })
}

#[cfg(not(unix))]
fn set_executable(_path: &Path, _executable: bool) -> Result<()> {
    Ok(())
}
