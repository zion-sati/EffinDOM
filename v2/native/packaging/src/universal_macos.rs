use crate::{
    verify_bundle, Error, OverwritePolicy, PackageArchitecture, PackageMetadata,
    PackageOperatingSystem, PackageRecord, PackageRecordFile, Result,
};
use sha2::{Digest, Sha256};
use std::fs::{self, File};
use std::io::Read;
use std::path::{Component, Path, PathBuf};
use std::process::Command;
use std::sync::atomic::{AtomicU64, Ordering};

static NEXT_UNIVERSAL_ID: AtomicU64 = AtomicU64::new(1);

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct UniversalMacOsInputs {
    pub arm64_bundle: PathBuf,
    pub x64_bundle: PathBuf,
    pub destination: PathBuf,
    pub package_record: PathBuf,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct UniversalMacOsArtifact {
    pub root: PathBuf,
    pub record: PackageRecord,
    pub merged_macho_files: Vec<PathBuf>,
}

pub fn assemble_universal_macos_app(
    inputs: &UniversalMacOsInputs,
    overwrite: OverwritePolicy,
) -> Result<UniversalMacOsArtifact> {
    assemble_with_tool(inputs, overwrite, &SystemLipo)
}

trait MachOTool {
    fn architectures(&self, path: &Path) -> Result<Vec<String>>;
    fn merge(&self, arm64: &Path, x64: &Path, destination: &Path) -> Result<()>;
}

struct SystemLipo;

impl MachOTool for SystemLipo {
    fn architectures(&self, path: &Path) -> Result<Vec<String>> {
        let output = Command::new("/usr/bin/lipo")
            .arg("-archs")
            .arg(path)
            .output()
            .map_err(|source| Error::UniversalMacOsTool {
                operation: "inspect",
                path: path.to_path_buf(),
                message: source.to_string(),
            })?;
        if !output.status.success() {
            return Err(Error::UniversalMacOsTool {
                operation: "inspect",
                path: path.to_path_buf(),
                message: String::from_utf8_lossy(&output.stderr).trim().to_string(),
            });
        }
        let value =
            String::from_utf8(output.stdout).map_err(|source| Error::UniversalMacOsTool {
                operation: "inspect",
                path: path.to_path_buf(),
                message: source.to_string(),
            })?;
        Ok(value.split_whitespace().map(str::to_owned).collect())
    }

    fn merge(&self, arm64: &Path, x64: &Path, destination: &Path) -> Result<()> {
        let output = Command::new("/usr/bin/lipo")
            .args(["-create", "-output"])
            .arg(destination)
            .arg(arm64)
            .arg(x64)
            .output()
            .map_err(|source| Error::UniversalMacOsTool {
                operation: "merge",
                path: destination.to_path_buf(),
                message: source.to_string(),
            })?;
        if output.status.success() {
            Ok(())
        } else {
            Err(Error::UniversalMacOsTool {
                operation: "merge",
                path: destination.to_path_buf(),
                message: String::from_utf8_lossy(&output.stderr).trim().to_string(),
            })
        }
    }
}

fn assemble_with_tool(
    inputs: &UniversalMacOsInputs,
    overwrite: OverwritePolicy,
    tool: &impl MachOTool,
) -> Result<UniversalMacOsArtifact> {
    validate_relative_path(&inputs.package_record)?;
    let arm64_root = canonical_directory(&inputs.arm64_bundle)?;
    let x64_root = canonical_directory(&inputs.x64_bundle)?;
    let destination = absolute_output(&inputs.destination)?;
    if destination.exists() && overwrite == OverwritePolicy::Reject {
        return Err(Error::BundleOutputExists(destination));
    }
    for source in [&arm64_root, &x64_root] {
        if destination.starts_with(source) || source.starts_with(&destination) {
            return Err(Error::SourceOutputOverlap {
                source: source.clone(),
                destination: destination.clone(),
            });
        }
    }

    let arm64_record = verify_bundle(&arm64_root, &inputs.package_record)?;
    let x64_record = verify_bundle(&x64_root, &inputs.package_record)?;
    validate_slice_metadata(
        &arm64_record.metadata,
        PackageArchitecture::Arm64,
        &arm64_root,
    )?;
    validate_slice_metadata(&x64_record.metadata, PackageArchitecture::X64, &x64_root)?;
    validate_common_metadata(&arm64_record.metadata, &x64_record.metadata)?;
    if arm64_record.files.len() != x64_record.files.len() {
        return Err(mismatch(
            "file count",
            arm64_record.files.len(),
            x64_record.files.len(),
        ));
    }

    let staging = unique_sibling(&destination, "universal-staging")?;
    create_dir_all(&staging, "create universal staging directory")?;
    let result = (|| {
        let mut files = Vec::with_capacity(arm64_record.files.len());
        let mut merged_macho_files = Vec::new();
        for (arm64_file, x64_file) in arm64_record.files.iter().zip(&x64_record.files) {
            if arm64_file.path != x64_file.path || arm64_file.role != x64_file.role {
                return Err(mismatch(
                    "file manifest",
                    format!("{}:{:?}", arm64_file.path, arm64_file.role),
                    format!("{}:{:?}", x64_file.path, x64_file.role),
                ));
            }
            let relative = relative_path(&arm64_file.path)?;
            let arm64_source = arm64_root.join(&relative);
            let x64_source = x64_root.join(&relative);
            let target = staging.join(&relative);
            create_parent(&target)?;
            let arm64_macho = is_macho(&arm64_source)?;
            let x64_macho = is_macho(&x64_source)?;
            match (arm64_macho, x64_macho) {
                (true, true) => {
                    require_architecture(tool, &arm64_source, "arm64", "ARM64")?;
                    require_architecture(tool, &x64_source, "x86_64", "x64")?;
                    tool.merge(&arm64_source, &x64_source, &target)?;
                    copy_permissions(&arm64_source, &target)?;
                    let merged_architectures = tool.architectures(&target)?;
                    if !merged_architectures.iter().any(|arch| arch == "arm64")
                        || !merged_architectures.iter().any(|arch| arch == "x86_64")
                    {
                        return Err(Error::InvalidUniversalMacOsSlice {
                            path: target,
                            message: format!(
                                "merged output must contain arm64 and x86_64, found {}",
                                merged_architectures.join(", ")
                            ),
                        });
                    }
                    merged_macho_files.push(relative.clone());
                }
                (false, false) => {
                    if arm64_file.byte_length != x64_file.byte_length
                        || arm64_file.sha256 != x64_file.sha256
                    {
                        return Err(mismatch(
                            format!("resource {}", arm64_file.path),
                            &arm64_file.sha256,
                            &x64_file.sha256,
                        ));
                    }
                    fs::copy(&arm64_source, &target).map_err(|source| Error::BundleIo {
                        operation: "copy universal resource",
                        path: arm64_source.clone(),
                        source,
                    })?;
                }
                _ => {
                    return Err(mismatch(
                        format!("Mach-O type for {}", arm64_file.path),
                        arm64_macho,
                        x64_macho,
                    ));
                }
            }
            let byte_length = fs::metadata(&target)
                .map_err(|source| Error::BundleIo {
                    operation: "inspect universal output",
                    path: target.clone(),
                    source,
                })?
                .len();
            files.push(PackageRecordFile {
                path: arm64_file.path.clone(),
                role: arm64_file.role,
                byte_length,
                sha256: sha256_file(&target)?,
            });
        }
        let record = PackageRecord {
            schema_version: arm64_record.schema_version,
            metadata: PackageMetadata {
                architecture: PackageArchitecture::Universal,
                target_triple: "universal-apple-darwin".to_string(),
                ..arm64_record.metadata.clone()
            },
            files,
        };
        write_record(&staging.join(&inputs.package_record), &record)?;
        verify_bundle(&staging, &inputs.package_record)?;
        publish_directory(&staging, &destination, overwrite)?;
        Ok(UniversalMacOsArtifact {
            root: destination,
            record,
            merged_macho_files,
        })
    })();
    if result.is_err() {
        let _ = fs::remove_dir_all(staging);
    }
    result
}

fn validate_slice_metadata(
    metadata: &PackageMetadata,
    architecture: PackageArchitecture,
    path: &Path,
) -> Result<()> {
    if metadata.operating_system != PackageOperatingSystem::MacOs {
        return Err(Error::InvalidUniversalMacOsSlice {
            path: path.to_path_buf(),
            message: "package record must target macOS".to_string(),
        });
    }
    if metadata.architecture != architecture {
        return Err(Error::InvalidUniversalMacOsSlice {
            path: path.to_path_buf(),
            message: format!(
                "package record architecture is {:?}, expected {:?}",
                metadata.architecture, architecture
            ),
        });
    }
    let expected_prefix = match architecture {
        PackageArchitecture::Arm64 => "aarch64-",
        PackageArchitecture::X64 => "x86_64-",
        PackageArchitecture::Universal => unreachable!(),
    };
    if !metadata.target_triple.starts_with(expected_prefix)
        || !metadata.target_triple.ends_with("-apple-darwin")
    {
        return Err(Error::InvalidUniversalMacOsSlice {
            path: path.to_path_buf(),
            message: format!(
                "target triple {} does not match {:?}",
                metadata.target_triple, architecture
            ),
        });
    }
    Ok(())
}

fn validate_common_metadata(arm64: &PackageMetadata, x64: &PackageMetadata) -> Result<()> {
    compare(
        "application identifier",
        &arm64.application_identifier,
        &x64.application_identifier,
    )?;
    compare(
        "application version",
        &arm64.application_version,
        &x64.application_version,
    )?;
    compare("build mode", &arm64.build_mode, &x64.build_mode)?;
    compare("Core ABI", &arm64.core_abi, &x64.core_abi)?;
    compare("UI ABI", &arm64.ui_abi, &x64.ui_abi)
}

fn compare<T: std::fmt::Debug + PartialEq>(
    field: impl Into<String>,
    arm64: &T,
    x64: &T,
) -> Result<()> {
    if arm64 == x64 {
        Ok(())
    } else {
        Err(Error::UniversalMacOsMismatch {
            field: field.into(),
            arm64: format!("{arm64:?}"),
            x64: format!("{x64:?}"),
        })
    }
}

fn mismatch(field: impl Into<String>, arm64: impl ToString, x64: impl ToString) -> Error {
    Error::UniversalMacOsMismatch {
        field: field.into(),
        arm64: arm64.to_string(),
        x64: x64.to_string(),
    }
}

fn require_architecture(
    tool: &impl MachOTool,
    path: &Path,
    expected: &str,
    label: &str,
) -> Result<()> {
    let architectures = tool.architectures(path)?;
    if architectures.len() == 1 && architectures[0] == expected {
        Ok(())
    } else {
        Err(Error::InvalidUniversalMacOsSlice {
            path: path.to_path_buf(),
            message: format!(
                "{label} input must contain exactly {expected}, found {}",
                architectures.join(", ")
            ),
        })
    }
}

fn is_macho(path: &Path) -> Result<bool> {
    let mut file = File::open(path).map_err(|source| Error::BundleIo {
        operation: "open universal input",
        path: path.to_path_buf(),
        source,
    })?;
    let mut magic = [0u8; 4];
    let count = file.read(&mut magic).map_err(|source| Error::BundleIo {
        operation: "read universal input magic",
        path: path.to_path_buf(),
        source,
    })?;
    Ok(count == 4
        && matches!(
            u32::from_be_bytes(magic),
            0xfeedface
                | 0xfeedfacf
                | 0xcefaedfe
                | 0xcffaedfe
                | 0xcafebabe
                | 0xbebafeca
                | 0xcafebabf
                | 0xbfbafeca
        ))
}

fn write_record(path: &Path, record: &PackageRecord) -> Result<()> {
    create_parent(path)?;
    let mut value = serde_json::to_vec_pretty(record).map_err(Error::SerializePackageRecord)?;
    value.push(b'\n');
    fs::write(path, value).map_err(|source| Error::BundleIo {
        operation: "write universal package record",
        path: path.to_path_buf(),
        source,
    })
}

fn relative_path(value: &str) -> Result<PathBuf> {
    let path = PathBuf::from(value);
    validate_relative_path(&path)?;
    Ok(path)
}

fn validate_relative_path(path: &Path) -> Result<()> {
    if path.as_os_str().is_empty()
        || path.is_absolute()
        || !path.components().all(
            |component| matches!(component, Component::Normal(value) if value.to_str().is_some()),
        )
    {
        Err(Error::InvalidBundlePath {
            path: path.to_path_buf(),
            message: "must contain only UTF-8 normal relative path components",
        })
    } else {
        Ok(())
    }
}

fn canonical_directory(path: &Path) -> Result<PathBuf> {
    let canonical = fs::canonicalize(path).map_err(|source| Error::BundleIo {
        operation: "resolve universal input bundle",
        path: path.to_path_buf(),
        source,
    })?;
    if canonical.is_dir() {
        Ok(canonical)
    } else {
        Err(Error::InvalidUniversalMacOsSlice {
            path: path.to_path_buf(),
            message: "must be a bundle directory".to_string(),
        })
    }
}

fn absolute_output(path: &Path) -> Result<PathBuf> {
    let name = path.file_name().ok_or_else(|| Error::InvalidBundlePath {
        path: path.to_path_buf(),
        message: "must name an output bundle",
    })?;
    let parent = path.parent().unwrap_or_else(|| Path::new("."));
    let parent = fs::canonicalize(parent).map_err(|source| Error::BundleIo {
        operation: "resolve universal output parent",
        path: parent.to_path_buf(),
        source,
    })?;
    Ok(parent.join(name))
}

fn create_parent(path: &Path) -> Result<()> {
    let parent = path.parent().ok_or_else(|| Error::InvalidBundlePath {
        path: path.to_path_buf(),
        message: "must have a parent directory",
    })?;
    create_dir_all(parent, "create universal output directory")
}

fn create_dir_all(path: &Path, operation: &'static str) -> Result<()> {
    fs::create_dir_all(path).map_err(|source| Error::BundleIo {
        operation,
        path: path.to_path_buf(),
        source,
    })
}

fn copy_permissions(source: &Path, destination: &Path) -> Result<()> {
    let permissions = fs::metadata(source)
        .map_err(|error| Error::BundleIo {
            operation: "inspect universal input permissions",
            path: source.to_path_buf(),
            source: error,
        })?
        .permissions();
    fs::set_permissions(destination, permissions).map_err(|error| Error::BundleIo {
        operation: "set universal output permissions",
        path: destination.to_path_buf(),
        source: error,
    })
}

fn publish_directory(staging: &Path, destination: &Path, overwrite: OverwritePolicy) -> Result<()> {
    if overwrite == OverwritePolicy::Replace && destination.exists() {
        let backup = unique_sibling(destination, "universal-backup")?;
        fs::rename(destination, &backup).map_err(|source| Error::PublishBundle {
            from: destination.to_path_buf(),
            to: backup.clone(),
            source,
        })?;
        if let Err(source) = fs::rename(staging, destination) {
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
            operation: "remove universal backup",
            path: backup,
            source,
        })?;
    } else {
        fs::rename(staging, destination).map_err(|source| Error::PublishBundle {
            from: staging.to_path_buf(),
            to: destination.to_path_buf(),
            source,
        })?;
    }
    Ok(())
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
        .and_then(|value| value.to_str())
        .ok_or_else(|| Error::InvalidBundlePath {
            path: destination.to_path_buf(),
            message: "must have a UTF-8 file name",
        })?;
    loop {
        let id = NEXT_UNIVERSAL_ID.fetch_add(1, Ordering::Relaxed);
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
        operation: "open universal output for hashing",
        path: path.to_path_buf(),
        source,
    })?;
    let mut digest = Sha256::new();
    let mut buffer = [0u8; 64 * 1024];
    loop {
        let count = file.read(&mut buffer).map_err(|source| Error::BundleIo {
            operation: "hash universal output",
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
    use crate::{BundleFile, PackagingInputs};

    struct FakeLipo;

    impl MachOTool for FakeLipo {
        fn architectures(&self, path: &Path) -> Result<Vec<String>> {
            let bytes = fs::read(path).map_err(|source| Error::BundleIo {
                operation: "read fake Mach-O",
                path: path.to_path_buf(),
                source,
            })?;
            let value = String::from_utf8_lossy(&bytes);
            Ok(if value.contains("universal") {
                vec!["x86_64".to_string(), "arm64".to_string()]
            } else if value.contains("arm64") {
                vec!["arm64".to_string()]
            } else {
                vec!["x86_64".to_string()]
            })
        }

        fn merge(&self, _arm64: &Path, _x64: &Path, destination: &Path) -> Result<()> {
            let mut value = 0xfeedfacfu32.to_be_bytes().to_vec();
            value.extend_from_slice(b" universal");
            fs::write(destination, value).map_err(|source| Error::BundleIo {
                operation: "write fake universal Mach-O",
                path: destination.to_path_buf(),
                source,
            })
        }
    }

    #[test]
    fn merges_matching_slices_and_rejects_resource_and_abi_mismatches() {
        let root = std::env::temp_dir().join(format!(
            "effindom-universal-test-{}-{}",
            std::process::id(),
            NEXT_UNIVERSAL_ID.fetch_add(1, Ordering::Relaxed)
        ));
        fs::create_dir_all(&root).expect("create fixture");
        let arm64 = stage_slice(&root, "arm64", PackageArchitecture::Arm64, 2, "resource");
        let x64 = stage_slice(&root, "x64", PackageArchitecture::X64, 2, "resource");
        let inputs = UniversalMacOsInputs {
            arm64_bundle: arm64,
            x64_bundle: x64.clone(),
            destination: root.join("Universal.app"),
            package_record: PathBuf::from("Contents/Resources/effindom-package.json"),
        };
        let artifact = assemble_with_tool(&inputs, OverwritePolicy::Reject, &FakeLipo)
            .expect("assemble universal fixture");
        assert_eq!(
            artifact.record.metadata.architecture,
            PackageArchitecture::Universal
        );
        assert_eq!(
            artifact.merged_macho_files,
            vec![PathBuf::from("Contents/MacOS/sample")]
        );
        verify_bundle(&artifact.root, &inputs.package_record).expect("verify universal fixture");

        let bad_resource = stage_slice(
            &root,
            "bad-resource",
            PackageArchitecture::X64,
            2,
            "different",
        );
        let mut invalid = inputs.clone();
        invalid.x64_bundle = bad_resource;
        invalid.destination = root.join("BadResource.app");
        assert!(matches!(
            assemble_with_tool(&invalid, OverwritePolicy::Reject, &FakeLipo),
            Err(Error::UniversalMacOsMismatch { .. })
        ));

        let bad_abi = stage_slice(&root, "bad-abi", PackageArchitecture::X64, 3, "resource");
        invalid.x64_bundle = bad_abi;
        invalid.destination = root.join("BadAbi.app");
        assert!(matches!(
            assemble_with_tool(&invalid, OverwritePolicy::Reject, &FakeLipo),
            Err(Error::UniversalMacOsMismatch { .. })
        ));
        fs::remove_dir_all(root).expect("remove fixture");
    }

    fn stage_slice(
        root: &Path,
        name: &str,
        architecture: PackageArchitecture,
        core_abi: u32,
        resource: &str,
    ) -> PathBuf {
        let input = root.join(format!("{name}-input"));
        fs::create_dir_all(&input).expect("create slice input");
        let executable = input.join("sample");
        let mut macho = 0xfeedfacfu32.to_be_bytes().to_vec();
        macho.extend_from_slice(format!(" {name}").as_bytes());
        fs::write(&executable, macho).expect("write fake Mach-O");
        let resource_path = input.join("resource");
        fs::write(&resource_path, resource).expect("write resource");
        let destination = root.join(format!("{name}.app"));
        crate::stage_bundle(
            &PackagingInputs {
                destination: destination.clone(),
                package_record: PathBuf::from("Contents/Resources/effindom-package.json"),
                metadata: PackageMetadata {
                    application_identifier: "dev.effindom.sample".to_string(),
                    application_version: "1.2.3".to_string(),
                    operating_system: PackageOperatingSystem::MacOs,
                    architecture,
                    target_triple: match architecture {
                        PackageArchitecture::Arm64 => "aarch64-apple-darwin",
                        PackageArchitecture::X64 => "x86_64-apple-darwin",
                        PackageArchitecture::Universal => unreachable!(),
                    }
                    .to_string(),
                    build_mode: crate::PackageBuildMode::Release,
                    core_abi,
                    ui_abi: 1,
                },
                application_executable: BundleFile::new(executable, "Contents/MacOS/sample"),
                effindom_runtime_libraries: vec![],
                third_party_libraries: vec![],
                runtime_resources: vec![],
                application_resources: vec![BundleFile::new(
                    resource_path,
                    "Contents/Resources/resource.txt",
                )],
                metadata_artifacts: vec![],
            },
            OverwritePolicy::Reject,
        )
        .expect("stage slice");
        destination
    }
}
