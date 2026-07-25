use crate::{verify_bundle, Error, OverwritePolicy, PackageRecord, Result};
use sha2::{Digest, Sha256};
use std::collections::BTreeSet;
use std::fs::{self, File};
use std::io::{self, Read};
use std::path::{Component, Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};

const PACKAGE_RECORD_SIDECAR: &str = "effindom-package.json";
static NEXT_ARCHIVE_ID: AtomicU64 = AtomicU64::new(1);

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ReleaseArchiveFormat {
    TarZstd,
    Zip,
}

impl ReleaseArchiveFormat {
    fn extension(self) -> &'static str {
        match self {
            Self::TarZstd => ".tar.zst",
            Self::Zip => ".zip",
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ReleaseArchiveSpec {
    pub archive_name: String,
    pub format: ReleaseArchiveFormat,
    pub package_record: PathBuf,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ReleaseArchiveArtifact {
    pub root: PathBuf,
    pub archive: PathBuf,
    pub checksum: PathBuf,
    pub package_record: PathBuf,
    pub archive_sha256: String,
    pub record: PackageRecord,
}

pub fn create_release_archive(
    bundle_root: impl AsRef<Path>,
    destination: impl AsRef<Path>,
    spec: &ReleaseArchiveSpec,
    overwrite: OverwritePolicy,
) -> Result<ReleaseArchiveArtifact> {
    validate_spec(spec)?;
    let bundle_root = canonical_directory(bundle_root.as_ref(), "resolve bundle root")?;
    let destination = absolute_output(destination.as_ref())?;
    if destination.starts_with(&bundle_root) || bundle_root.starts_with(&destination) {
        return Err(Error::SourceOutputOverlap {
            source: bundle_root,
            destination,
        });
    }
    if destination.exists() && overwrite == OverwritePolicy::Reject {
        return Err(Error::ArchiveOutputExists(destination));
    }
    let record = verify_bundle(&bundle_root, &spec.package_record)?;
    let staging = unique_sibling(&destination, "archive-staging")?;
    create_dir_all(&staging, "create archive staging directory")?;

    let result = (|| {
        let archive = staging.join(&spec.archive_name);
        match spec.format {
            ReleaseArchiveFormat::TarZstd => encode_tar_zstd(&bundle_root, &archive)?,
            ReleaseArchiveFormat::Zip => encode_zip(&bundle_root, &archive)?,
        }
        let archive_sha256 = sha256_file(&archive)?;
        let checksum = staging.join(format!("{}.sha256", spec.archive_name));
        fs::write(
            &checksum,
            format!("{archive_sha256}  {}\n", spec.archive_name),
        )
        .map_err(|source| archive_io("write checksum sidecar", &checksum, source))?;
        let package_record = staging.join(PACKAGE_RECORD_SIDECAR);
        fs::copy(bundle_root.join(&spec.package_record), &package_record)
            .map_err(|source| archive_io("copy package record sidecar", &package_record, source))?;
        publish_directory(&staging, &destination, overwrite)?;
        Ok(ReleaseArchiveArtifact {
            archive: destination.join(&spec.archive_name),
            checksum: destination.join(format!("{}.sha256", spec.archive_name)),
            package_record: destination.join(PACKAGE_RECORD_SIDECAR),
            root: destination,
            archive_sha256,
            record,
        })
    })();
    if result.is_err() {
        let _ = fs::remove_dir_all(staging);
    }
    result
}

pub fn extract_release_archive(
    artifact_root: impl AsRef<Path>,
    destination: impl AsRef<Path>,
    spec: &ReleaseArchiveSpec,
    overwrite: OverwritePolicy,
) -> Result<PackageRecord> {
    validate_spec(spec)?;
    let artifact_root = canonical_directory(artifact_root.as_ref(), "resolve archive root")?;
    let destination = absolute_output(destination.as_ref())?;
    if destination.exists() && overwrite == OverwritePolicy::Reject {
        return Err(Error::BundleOutputExists(destination));
    }
    let archive = artifact_root.join(&spec.archive_name);
    let checksum = artifact_root.join(format!("{}.sha256", spec.archive_name));
    let expected = read_checksum(&checksum, &spec.archive_name)?;
    let actual = sha256_file(&archive)?;
    if actual != expected {
        return Err(Error::ArchiveChecksumMismatch {
            path: archive,
            expected,
            actual,
        });
    }

    let staging = unique_sibling(&destination, "extract-staging")?;
    create_dir_all(&staging, "create extraction staging directory")?;
    let result = (|| {
        match spec.format {
            ReleaseArchiveFormat::TarZstd => decode_tar_zstd(&archive, &staging)?,
            ReleaseArchiveFormat::Zip => decode_zip(&archive, &staging)?,
        }
        let embedded_record = staging.join(&spec.package_record);
        let sidecar_record = artifact_root.join(PACKAGE_RECORD_SIDECAR);
        let embedded = fs::read(&embedded_record).map_err(|source| {
            archive_io("read extracted package record", &embedded_record, source)
        })?;
        let sidecar = fs::read(&sidecar_record)
            .map_err(|source| archive_io("read package record sidecar", &sidecar_record, source))?;
        if embedded != sidecar {
            return Err(Error::ArchivePackageRecordMismatch(sidecar_record));
        }
        let record = verify_bundle(&staging, &spec.package_record)?;
        publish_directory(&staging, &destination, overwrite)?;
        Ok(record)
    })();
    if result.is_err() {
        let _ = fs::remove_dir_all(staging);
    }
    result
}

fn validate_spec(spec: &ReleaseArchiveSpec) -> Result<()> {
    let path = Path::new(&spec.archive_name);
    if path.file_name().and_then(|name| name.to_str()) != Some(spec.archive_name.as_str())
        || path.components().count() != 1
        || !spec.archive_name.ends_with(spec.format.extension())
    {
        return Err(Error::InvalidArchiveName(path.to_path_buf()));
    }
    validate_relative_path(&spec.package_record)?;
    Ok(())
}

#[derive(Debug)]
struct ArchiveEntry {
    relative: PathBuf,
    source: PathBuf,
    kind: ArchiveEntryKind,
    mode: u32,
}

#[derive(Debug)]
enum ArchiveEntryKind {
    Directory,
    File,
    Symlink(PathBuf),
}

fn collect_entries(root: &Path) -> Result<Vec<ArchiveEntry>> {
    let mut entries = Vec::new();
    collect_directory(root, root, &mut entries)?;
    entries.sort_by_key(|entry| path_key(&entry.relative));
    Ok(entries)
}

fn collect_directory(root: &Path, directory: &Path, entries: &mut Vec<ArchiveEntry>) -> Result<()> {
    let mut children = fs::read_dir(directory)
        .map_err(|source| archive_io("read bundle directory", directory, source))?
        .collect::<std::result::Result<Vec<_>, _>>()
        .map_err(|source| archive_io("read bundle directory entry", directory, source))?;
    children.sort_by_key(|entry| entry.file_name());
    for child in children {
        let source = child.path();
        let relative = source
            .strip_prefix(root)
            .expect("collected entry must remain under root")
            .to_path_buf();
        validate_relative_path(&relative)?;
        let metadata = fs::symlink_metadata(&source)
            .map_err(|error| archive_io("inspect bundle entry", &source, error))?;
        if metadata.file_type().is_symlink() {
            let target = fs::read_link(&source)
                .map_err(|error| archive_io("read bundle symlink", &source, error))?;
            validate_link_target(&relative, &target)?;
            entries.push(ArchiveEntry {
                relative,
                source,
                kind: ArchiveEntryKind::Symlink(target),
                mode: 0o777,
            });
        } else if metadata.is_dir() {
            entries.push(ArchiveEntry {
                relative: relative.clone(),
                source: source.clone(),
                kind: ArchiveEntryKind::Directory,
                mode: unix_mode(&metadata, 0o755),
            });
            collect_directory(root, &source, entries)?;
        } else if metadata.is_file() {
            entries.push(ArchiveEntry {
                relative,
                source,
                kind: ArchiveEntryKind::File,
                mode: unix_mode(&metadata, 0o644),
            });
        } else {
            return Err(Error::UnsupportedArchiveEntry(relative));
        }
    }
    Ok(())
}

pub(crate) fn encode_tar_zstd(root: &Path, destination: &Path) -> Result<()> {
    let file = File::create(destination)
        .map_err(|source| archive_io("create tar.zst", destination, source))?;
    let encoder = zstd::Encoder::new(file, 19).map_err(|source| Error::ArchiveEncoding {
        format: "tar.zst",
        message: source.to_string(),
    })?;
    let mut tar = tar::Builder::new(encoder);
    tar.mode(tar::HeaderMode::Deterministic);
    for entry in collect_entries(root)? {
        let mut header = tar::Header::new_gnu();
        header.set_uid(0);
        header.set_gid(0);
        header.set_mtime(0);
        header.set_mode(entry.mode);
        match entry.kind {
            ArchiveEntryKind::Directory => {
                header.set_entry_type(tar::EntryType::Directory);
                header.set_size(0);
                header.set_cksum();
                tar.append_data(&mut header, &entry.relative, io::empty())
                    .map_err(|source| Error::ArchiveEncoding {
                        format: "tar.zst",
                        message: source.to_string(),
                    })?;
            }
            ArchiveEntryKind::File => {
                let mut file = File::open(&entry.source)
                    .map_err(|source| archive_io("open bundle file", &entry.source, source))?;
                header.set_entry_type(tar::EntryType::Regular);
                header.set_size(
                    file.metadata()
                        .map_err(|source| archive_io("inspect bundle file", &entry.source, source))?
                        .len(),
                );
                header.set_cksum();
                tar.append_data(&mut header, &entry.relative, &mut file)
                    .map_err(|source| Error::ArchiveEncoding {
                        format: "tar.zst",
                        message: source.to_string(),
                    })?;
            }
            ArchiveEntryKind::Symlink(target) => {
                header.set_entry_type(tar::EntryType::Symlink);
                header.set_size(0);
                header
                    .set_link_name(&target)
                    .map_err(|source| Error::ArchiveEncoding {
                        format: "tar.zst",
                        message: source.to_string(),
                    })?;
                header.set_cksum();
                tar.append_data(&mut header, &entry.relative, io::empty())
                    .map_err(|source| Error::ArchiveEncoding {
                        format: "tar.zst",
                        message: source.to_string(),
                    })?;
            }
        }
    }
    let encoder = tar.into_inner().map_err(|source| Error::ArchiveEncoding {
        format: "tar.zst",
        message: source.to_string(),
    })?;
    encoder.finish().map_err(|source| Error::ArchiveEncoding {
        format: "tar.zst",
        message: source.to_string(),
    })?;
    Ok(())
}

pub(crate) fn encode_zip(root: &Path, destination: &Path) -> Result<()> {
    use zip::write::SimpleFileOptions;
    let file = File::create(destination)
        .map_err(|source| archive_io("create zip", destination, source))?;
    let mut writer = zip::ZipWriter::new(file);
    for entry in collect_entries(root)? {
        let name = path_key(&entry.relative);
        let options = SimpleFileOptions::default()
            .compression_method(zip::CompressionMethod::Deflated)
            .last_modified_time(zip::DateTime::default())
            .unix_permissions(entry.mode);
        match entry.kind {
            ArchiveEntryKind::Directory => writer
                .add_directory(format!("{name}/"), options)
                .map_err(zip_encoding)?,
            ArchiveEntryKind::File => {
                writer.start_file(name, options).map_err(zip_encoding)?;
                let mut source = File::open(&entry.source)
                    .map_err(|error| archive_io("open bundle file", &entry.source, error))?;
                io::copy(&mut source, &mut writer).map_err(|error| Error::ArchiveEncoding {
                    format: "zip",
                    message: error.to_string(),
                })?;
            }
            ArchiveEntryKind::Symlink(_) => {
                return Err(Error::UnsupportedArchiveEntry(entry.relative));
            }
        }
    }
    writer.finish().map_err(zip_encoding)?;
    Ok(())
}

pub(crate) fn decode_tar_zstd(archive: &Path, destination: &Path) -> Result<()> {
    let file = File::open(archive).map_err(|source| archive_io("open tar.zst", archive, source))?;
    let decoder = zstd::Decoder::new(file).map_err(|source| Error::ArchiveDecoding {
        format: "tar.zst",
        message: source.to_string(),
    })?;
    let mut tar = tar::Archive::new(decoder);
    let entries = tar.entries().map_err(|source| Error::ArchiveDecoding {
        format: "tar.zst",
        message: source.to_string(),
    })?;
    let mut seen = BTreeSet::new();
    for entry in entries {
        let mut entry = entry.map_err(|source| Error::ArchiveDecoding {
            format: "tar.zst",
            message: source.to_string(),
        })?;
        let relative = entry
            .path()
            .map_err(|source| Error::ArchiveDecoding {
                format: "tar.zst",
                message: source.to_string(),
            })?
            .into_owned();
        validate_relative_path(&relative)?;
        if !seen.insert(path_key(&relative)) {
            return Err(Error::DuplicateBundlePath(relative));
        }
        let target = destination.join(&relative);
        let kind = entry.header().entry_type();
        if kind.is_dir() {
            create_dir_all(&target, "create extracted directory")?;
        } else if kind.is_file() {
            create_parent(&target)?;
            let mut output = File::create(&target)
                .map_err(|source| archive_io("create extracted file", &target, source))?;
            io::copy(&mut entry, &mut output).map_err(|source| Error::ArchiveDecoding {
                format: "tar.zst",
                message: source.to_string(),
            })?;
            set_unix_mode(&target, entry.header().mode().unwrap_or(0o644))?;
        } else if kind.is_symlink() {
            let link = entry
                .link_name()
                .map_err(|source| Error::ArchiveDecoding {
                    format: "tar.zst",
                    message: source.to_string(),
                })?
                .ok_or_else(|| Error::UnsupportedArchiveEntry(relative.clone()))?
                .into_owned();
            validate_link_target(&relative, &link)?;
            create_parent(&target)?;
            create_symlink(&link, &target)?;
        } else {
            return Err(Error::UnsupportedArchiveEntry(relative));
        }
    }
    Ok(())
}

pub(crate) fn decode_zip(archive: &Path, destination: &Path) -> Result<()> {
    let file = File::open(archive).map_err(|source| archive_io("open zip", archive, source))?;
    let mut zip = zip::ZipArchive::new(file).map_err(zip_decoding)?;
    let mut seen = BTreeSet::new();
    for index in 0..zip.len() {
        let mut entry = zip.by_index(index).map_err(zip_decoding)?;
        let relative = entry
            .enclosed_name()
            .ok_or_else(|| Error::UnsupportedArchiveEntry(PathBuf::from(entry.name())))?
            .to_path_buf();
        validate_relative_path(&relative)?;
        if !seen.insert(path_key(&relative)) {
            return Err(Error::DuplicateBundlePath(relative));
        }
        let target = destination.join(&relative);
        if entry.is_dir() {
            create_dir_all(&target, "create extracted directory")?;
        } else {
            create_parent(&target)?;
            let mut output = File::create(&target)
                .map_err(|source| archive_io("create extracted file", &target, source))?;
            io::copy(&mut entry, &mut output).map_err(|source| Error::ArchiveDecoding {
                format: "zip",
                message: source.to_string(),
            })?;
            set_unix_mode(&target, entry.unix_mode().unwrap_or(0o644))?;
        }
    }
    Ok(())
}

fn read_checksum(path: &Path, archive_name: &str) -> Result<String> {
    let value = fs::read_to_string(path)
        .map_err(|source| archive_io("read checksum sidecar", path, source))?;
    let expected_suffix = format!("  {archive_name}\n");
    let digest = value
        .strip_suffix(&expected_suffix)
        .filter(|digest| digest.len() == 64 && digest.bytes().all(|byte| byte.is_ascii_hexdigit()))
        .ok_or_else(|| Error::ArchiveDecoding {
            format: "checksum sidecar",
            message: format!("{} is malformed", path.display()),
        })?;
    Ok(digest.to_ascii_lowercase())
}

pub(crate) fn publish_directory(
    staging: &Path,
    destination: &Path,
    overwrite: OverwritePolicy,
) -> Result<()> {
    if overwrite == OverwritePolicy::Replace && destination.exists() {
        let backup = unique_sibling(destination, "archive-backup")?;
        fs::rename(destination, &backup)
            .map_err(|source| archive_io("back up archive output", destination, source))?;
        if let Err(source) = fs::rename(staging, destination) {
            fs::rename(&backup, destination)
                .map_err(|rollback| archive_io("restore archive output", &backup, rollback))?;
            return Err(archive_io("publish archive output", destination, source));
        }
        fs::remove_dir_all(&backup)
            .map_err(|source| archive_io("remove archive backup", &backup, source))?;
    } else {
        fs::rename(staging, destination)
            .map_err(|source| archive_io("publish archive output", destination, source))?;
    }
    Ok(())
}

fn canonical_directory(path: &Path, operation: &'static str) -> Result<PathBuf> {
    let canonical = fs::canonicalize(path).map_err(|source| archive_io(operation, path, source))?;
    if !canonical.is_dir() {
        return Err(Error::UnsupportedArchiveEntry(path.to_path_buf()));
    }
    Ok(canonical)
}

pub(crate) fn absolute_output(path: &Path) -> Result<PathBuf> {
    let name = path.file_name().ok_or_else(|| Error::InvalidBundlePath {
        path: path.to_path_buf(),
        message: "must name an output directory",
    })?;
    let parent = path.parent().unwrap_or_else(|| Path::new("."));
    let parent = fs::canonicalize(parent)
        .map_err(|source| archive_io("resolve archive output parent", parent, source))?;
    Ok(parent.join(name))
}

pub(crate) fn validate_relative_path(path: &Path) -> Result<()> {
    if path.as_os_str().is_empty() || path.is_absolute() {
        return Err(Error::InvalidBundlePath {
            path: path.to_path_buf(),
            message: "must be a non-empty relative path",
        });
    }
    if path
        .components()
        .all(|component| matches!(component, Component::Normal(value) if value.to_str().is_some()))
    {
        Ok(())
    } else {
        Err(Error::InvalidBundlePath {
            path: path.to_path_buf(),
            message: "must contain only UTF-8 normal path components",
        })
    }
}

fn validate_link_target(entry: &Path, target: &Path) -> Result<()> {
    if target.as_os_str().is_empty() || target.is_absolute() {
        return Err(Error::UnsupportedArchiveEntry(entry.to_path_buf()));
    }
    let mut depth = entry
        .parent()
        .map_or(0usize, |parent| parent.components().count());
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

fn path_key(path: &Path) -> String {
    path.components()
        .filter_map(|component| match component {
            Component::Normal(value) => value.to_str(),
            _ => None,
        })
        .collect::<Vec<_>>()
        .join("/")
}

fn create_parent(path: &Path) -> Result<()> {
    let parent = path.parent().ok_or_else(|| Error::InvalidBundlePath {
        path: path.to_path_buf(),
        message: "must have a parent directory",
    })?;
    create_dir_all(parent, "create extracted parent directory")
}

pub(crate) fn create_dir_all(path: &Path, operation: &'static str) -> Result<()> {
    fs::create_dir_all(path).map_err(|source| archive_io(operation, path, source))
}

pub(crate) fn unique_sibling(destination: &Path, kind: &str) -> Result<PathBuf> {
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
        let id = NEXT_ARCHIVE_ID.fetch_add(1, Ordering::Relaxed);
        let candidate = parent.join(format!(
            ".{name}.effindom-{kind}-{}-{id}",
            std::process::id()
        ));
        if !candidate.exists() {
            return Ok(candidate);
        }
    }
}

pub(crate) fn sha256_file(path: &Path) -> Result<String> {
    let mut file =
        File::open(path).map_err(|source| archive_io("open for hashing", path, source))?;
    let mut digest = Sha256::new();
    let mut buffer = [0u8; 64 * 1024];
    loop {
        let count = file
            .read(&mut buffer)
            .map_err(|source| archive_io("hash", path, source))?;
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

fn archive_io(operation: &'static str, path: &Path, source: io::Error) -> Error {
    Error::ArchiveIo {
        operation,
        path: path.to_path_buf(),
        source,
    }
}

fn zip_encoding(source: zip::result::ZipError) -> Error {
    Error::ArchiveEncoding {
        format: "zip",
        message: source.to_string(),
    }
}

fn zip_decoding(source: zip::result::ZipError) -> Error {
    Error::ArchiveDecoding {
        format: "zip",
        message: source.to_string(),
    }
}

#[cfg(unix)]
fn unix_mode(metadata: &fs::Metadata, _fallback: u32) -> u32 {
    use std::os::unix::fs::PermissionsExt;
    metadata.permissions().mode() & 0o777
}

#[cfg(not(unix))]
fn unix_mode(_metadata: &fs::Metadata, fallback: u32) -> u32 {
    fallback
}

#[cfg(unix)]
fn set_unix_mode(path: &Path, mode: u32) -> Result<()> {
    use std::os::unix::fs::PermissionsExt;
    fs::set_permissions(path, fs::Permissions::from_mode(mode & 0o777))
        .map_err(|source| archive_io("set extracted permissions", path, source))
}

#[cfg(not(unix))]
fn set_unix_mode(_path: &Path, _mode: u32) -> Result<()> {
    Ok(())
}

#[cfg(unix)]
fn create_symlink(target: &Path, link: &Path) -> Result<()> {
    std::os::unix::fs::symlink(target, link)
        .map_err(|source| archive_io("create extracted symlink", link, source))
}

#[cfg(not(unix))]
fn create_symlink(_target: &Path, link: &Path) -> Result<()> {
    Err(Error::UnsupportedArchiveEntry(link.to_path_buf()))
}
