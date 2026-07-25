use crate::{verify_bundle, Error, OverwritePolicy, PackageOperatingSystem, PackageRecord, Result};
use sha2::{Digest, Sha256};
use std::fs::{self, File};
use std::io::Read;
use std::path::{Component, Path, PathBuf};
use std::process::{Command, Output};
use std::sync::atomic::{AtomicU64, Ordering};

static NEXT_DISTRIBUTION_ID: AtomicU64 = AtomicU64::new(1);

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DistributionArtifact {
    pub path: PathBuf,
    pub checksum: PathBuf,
    pub package_record: PathBuf,
    pub sha256: String,
    pub record: PackageRecord,
}

pub(crate) fn verified_record(
    root: &Path,
    package_record: &Path,
    expected: PackageOperatingSystem,
    format: &'static str,
) -> Result<PackageRecord> {
    let record = verify_bundle(root, package_record)?;
    if record.metadata.operating_system != expected {
        return Err(Error::DistributionTargetMismatch {
            format,
            actual: format!(
                "{:?}/{:?}",
                record.metadata.operating_system, record.metadata.architecture
            ),
        });
    }
    Ok(record)
}

pub(crate) fn finalize_artifact(
    path: PathBuf,
    source_record: &Path,
    record: PackageRecord,
) -> Result<DistributionArtifact> {
    let sha256 = sha256_file(&path)?;
    let file_name = path
        .file_name()
        .and_then(|value| value.to_str())
        .ok_or_else(|| Error::InvalidBundlePath {
            path: path.clone(),
            message: "must have a UTF-8 file name",
        })?;
    let checksum = path.with_file_name(format!("{file_name}.sha256"));
    fs::write(&checksum, format!("{sha256}  {file_name}\n"))
        .map_err(|source| io_error("write distribution checksum", &checksum, source))?;
    let package_record = path.with_file_name(format!("{file_name}.effindom-package.json"));
    fs::copy(source_record, &package_record)
        .map_err(|source| io_error("copy distribution package record", &package_record, source))?;
    Ok(DistributionArtifact {
        path,
        checksum,
        package_record,
        sha256,
        record,
    })
}

pub(crate) fn prepare_output(path: &Path, overwrite: OverwritePolicy) -> Result<PathBuf> {
    let name = path.file_name().ok_or_else(|| Error::InvalidBundlePath {
        path: path.to_path_buf(),
        message: "must name a distribution artifact",
    })?;
    let parent = path.parent().unwrap_or_else(|| Path::new("."));
    fs::create_dir_all(parent)
        .map_err(|source| io_error("create distribution output parent", parent, source))?;
    let parent = fs::canonicalize(parent)
        .map_err(|source| io_error("resolve distribution output parent", parent, source))?;
    let absolute = parent.join(name);
    if absolute.exists() {
        if overwrite == OverwritePolicy::Reject {
            return Err(Error::BundleOutputExists(absolute));
        }
        fs::remove_file(&absolute).map_err(|source| {
            io_error("remove replaced distribution artifact", &absolute, source)
        })?;
    }
    Ok(absolute)
}

pub(crate) fn copy_tree(source: &Path, destination: &Path) -> Result<()> {
    let source = fs::canonicalize(source)
        .map_err(|error| io_error("resolve distribution payload", source, error))?;
    create_dir_all(destination, "create distribution payload root")?;
    copy_directory(&source, &source, destination)
}

fn copy_directory(root: &Path, directory: &Path, destination: &Path) -> Result<()> {
    let mut entries = fs::read_dir(directory)
        .map_err(|source| io_error("read distribution payload directory", directory, source))?
        .collect::<std::result::Result<Vec<_>, _>>()
        .map_err(|source| io_error("read distribution payload entry", directory, source))?;
    entries.sort_by_key(|entry| entry.file_name());
    for entry in entries {
        let source = entry.path();
        let relative = source.strip_prefix(root).expect("entry remains under root");
        validate_relative(relative)?;
        let target = destination.join(relative);
        let metadata = fs::symlink_metadata(&source)
            .map_err(|error| io_error("inspect distribution payload", &source, error))?;
        if metadata.file_type().is_symlink() {
            let link = fs::read_link(&source)
                .map_err(|error| io_error("read distribution symlink", &source, error))?;
            validate_link(relative, &link)?;
            create_parent(&target)?;
            create_symlink(&link, &target)?;
        } else if metadata.is_dir() {
            create_dir_all(&target, "create distribution payload directory")?;
            copy_directory(root, &source, destination)?;
        } else if metadata.is_file() {
            create_parent(&target)?;
            fs::copy(&source, &target)
                .map_err(|error| io_error("copy distribution payload", &source, error))?;
        } else {
            return Err(Error::UnsupportedArchiveEntry(relative.to_path_buf()));
        }
    }
    Ok(())
}

pub(crate) fn run_tool(
    tool_name: &'static str,
    operation: &'static str,
    subject: &Path,
    command: &mut Command,
) -> Result<Output> {
    let output = command.output().map_err(|source| Error::DistributionTool {
        tool: tool_name,
        operation,
        path: subject.to_path_buf(),
        message: source.to_string(),
    })?;
    if output.status.success() {
        Ok(output)
    } else {
        let stderr = String::from_utf8_lossy(&output.stderr).trim().to_string();
        let stdout = String::from_utf8_lossy(&output.stdout).trim().to_string();
        Err(Error::DistributionTool {
            tool: tool_name,
            operation,
            path: subject.to_path_buf(),
            message: if stderr.is_empty() { stdout } else { stderr },
        })
    }
}

pub(crate) fn unique_path(destination: &Path, kind: &str) -> Result<PathBuf> {
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
        let id = NEXT_DISTRIBUTION_ID.fetch_add(1, Ordering::Relaxed);
        let candidate = parent.join(format!(
            ".{name}.effindom-{kind}-{}-{id}",
            std::process::id()
        ));
        if !candidate.exists() {
            return Ok(candidate);
        }
    }
}

pub(crate) fn create_dir_all(path: &Path, operation: &'static str) -> Result<()> {
    fs::create_dir_all(path).map_err(|source| io_error(operation, path, source))
}

pub(crate) fn create_parent(path: &Path) -> Result<()> {
    let parent = path.parent().ok_or_else(|| Error::InvalidBundlePath {
        path: path.to_path_buf(),
        message: "must have a parent directory",
    })?;
    create_dir_all(parent, "create distribution payload parent")
}

pub(crate) fn write_executable(path: &Path, value: &[u8]) -> Result<()> {
    create_parent(path)?;
    fs::write(path, value)
        .map_err(|source| io_error("write distribution launcher", path, source))?;
    set_executable(path)
}

pub(crate) fn xml_escape(value: &str) -> String {
    value
        .replace('&', "&amp;")
        .replace('<', "&lt;")
        .replace('>', "&gt;")
        .replace('"', "&quot;")
        .replace('\'', "&apos;")
}

fn validate_relative(path: &Path) -> Result<()> {
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

fn validate_link(entry: &Path, target: &Path) -> Result<()> {
    if target.as_os_str().is_empty() || target.is_absolute() {
        return Err(Error::UnsupportedArchiveEntry(entry.to_path_buf()));
    }
    let mut depth = entry
        .parent()
        .map_or(0, |parent| parent.components().count());
    for component in target.components() {
        match component {
            Component::Normal(value) if value.to_str().is_some() => depth += 1,
            Component::ParentDir if depth > 0 => depth -= 1,
            Component::CurDir => {}
            _ => return Err(Error::UnsupportedArchiveEntry(entry.to_path_buf())),
        }
    }
    Ok(())
}

fn sha256_file(path: &Path) -> Result<String> {
    let mut file = File::open(path).map_err(|source| io_error("open for hashing", path, source))?;
    let mut digest = Sha256::new();
    let mut buffer = [0u8; 64 * 1024];
    loop {
        let count = file
            .read(&mut buffer)
            .map_err(|source| io_error("hash distribution artifact", path, source))?;
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

fn io_error(operation: &'static str, path: &Path, source: std::io::Error) -> Error {
    Error::ArchiveIo {
        operation,
        path: path.to_path_buf(),
        source,
    }
}

#[cfg(unix)]
fn create_symlink(target: &Path, link: &Path) -> Result<()> {
    std::os::unix::fs::symlink(target, link)
        .map_err(|source| io_error("create distribution symlink", link, source))
}

#[cfg(windows)]
fn create_symlink(target: &Path, link: &Path) -> Result<()> {
    std::os::windows::fs::symlink_file(target, link)
        .map_err(|source| io_error("create distribution symlink", link, source))
}

#[cfg(unix)]
fn set_executable(path: &Path) -> Result<()> {
    use std::os::unix::fs::PermissionsExt;
    fs::set_permissions(path, fs::Permissions::from_mode(0o755))
        .map_err(|source| io_error("set launcher executable", path, source))
}

#[cfg(windows)]
fn set_executable(_path: &Path) -> Result<()> {
    Ok(())
}
