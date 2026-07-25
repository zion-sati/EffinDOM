use std::fmt;
use std::path::PathBuf;

pub type Result<T> = std::result::Result<T, Error>;

#[derive(Debug)]
pub enum Error {
    ReadFile {
        path: PathBuf,
        source: std::io::Error,
    },
    InvalidIconPath(PathBuf),
    DecodeIcon {
        path: PathBuf,
        message: String,
    },
    InvalidIconDimensions {
        path: PathBuf,
        width: u32,
        height: u32,
    },
    PngIconTooSmall {
        path: PathBuf,
        width: u32,
        height: u32,
        minimum: u32,
    },
    InvisibleIcon(PathBuf),
    InvalidIconRasterSize(u32),
    DuplicateIconRasterSize(u32),
    MissingIconRaster(u32),
    InvalidIconRasterData {
        size: u32,
        expected: usize,
        actual: usize,
    },
    InvalidIconName(String),
    EncodeIcon {
        format: &'static str,
        message: String,
    },
    InvalidApplicationMetadata {
        field: &'static str,
        message: String,
    },
    InvalidBundlePath {
        path: PathBuf,
        message: &'static str,
    },
    MissingBundleInput(PathBuf),
    UnsupportedBundleInput(PathBuf),
    DuplicateBundlePath(PathBuf),
    SourceOutputOverlap {
        source: PathBuf,
        destination: PathBuf,
    },
    BundleOutputExists(PathBuf),
    BundleIo {
        operation: &'static str,
        path: PathBuf,
        source: std::io::Error,
    },
    PublishBundle {
        from: PathBuf,
        to: PathBuf,
        source: std::io::Error,
    },
    RollbackBundle {
        backup: PathBuf,
        destination: PathBuf,
        source: std::io::Error,
    },
    SerializePackageRecord(serde_json::Error),
    ParsePackageRecord {
        path: PathBuf,
        source: serde_json::Error,
    },
    UnsupportedPackageRecordSchema(u32),
    PackageRecordArtifactMissing(PathBuf),
    PackageRecordLengthMismatch {
        path: PathBuf,
        expected: u64,
        actual: u64,
    },
    PackageRecordChecksumMismatch {
        path: PathBuf,
        expected: String,
        actual: String,
    },
    InvalidArchiveName(PathBuf),
    ArchiveOutputExists(PathBuf),
    ArchiveIo {
        operation: &'static str,
        path: PathBuf,
        source: std::io::Error,
    },
    ArchiveEncoding {
        format: &'static str,
        message: String,
    },
    ArchiveDecoding {
        format: &'static str,
        message: String,
    },
    ArchiveChecksumMismatch {
        path: PathBuf,
        expected: String,
        actual: String,
    },
    ArchivePackageRecordMismatch(PathBuf),
    UnsupportedArchiveEntry(PathBuf),
    InvalidUniversalMacOsSlice {
        path: PathBuf,
        message: String,
    },
    UniversalMacOsMismatch {
        field: String,
        arm64: String,
        x64: String,
    },
    UniversalMacOsTool {
        operation: &'static str,
        path: PathBuf,
        message: String,
    },
    DistributionTargetMismatch {
        format: &'static str,
        actual: String,
    },
    DistributionTool {
        tool: &'static str,
        operation: &'static str,
        path: PathBuf,
        message: String,
    },
    ParseNativeRuntimeManifest(serde_json::Error),
    SerializeNativeRuntimeManifest(serde_json::Error),
    InvalidNativeRuntimeManifest(String),
}

impl fmt::Display for Error {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::ReadFile { path, source } => write!(formatter, "failed to read {}: {source}", path.display()),
            Self::InvalidIconPath(path) => write!(formatter, "application icon {} must be an SVG or PNG source file", path.display()),
            Self::DecodeIcon { path, message } => write!(formatter, "failed to decode icon {}: {message}", path.display()),
            Self::InvalidIconDimensions { path, width, height } => write!(formatter, "icon {} must be square and non-empty, but is {width}x{height}", path.display()),
            Self::PngIconTooSmall { path, width, height, minimum } => write!(formatter, "PNG icon {} is {width}x{height}; provide a square PNG of at least {minimum}x{minimum} or use SVG", path.display()),
            Self::InvisibleIcon(path) => write!(formatter, "icon {} is fully transparent and would be invisible", path.display()),
            Self::InvalidIconRasterSize(size) => write!(formatter, "icon raster size must be greater than zero, got {size}"),
            Self::DuplicateIconRasterSize(size) => write!(formatter, "icon raster size {size} was requested more than once"),
            Self::MissingIconRaster(size) => write!(formatter, "icon raster set is missing required {size}x{size} pixels"),
            Self::InvalidIconRasterData { size, expected, actual } => write!(formatter, "icon raster {size}x{size} has {actual} RGBA bytes; expected {expected}"),
            Self::InvalidIconName(name) => write!(formatter, "icon name {name:?} must use only ASCII letters, digits, dots, hyphens, or underscores"),
            Self::EncodeIcon { format, message } => write!(formatter, "failed to encode {format} icon: {message}"),
            Self::InvalidApplicationMetadata { field, message } => write!(formatter, "invalid application metadata {field}: {message}"),
            Self::InvalidBundlePath { path, message } => write!(formatter, "invalid bundle path {}: {message}", path.display()),
            Self::MissingBundleInput(path) => write!(formatter, "bundle input {} does not exist", path.display()),
            Self::UnsupportedBundleInput(path) => write!(formatter, "bundle input {} must be a regular file and must not be a symbolic link", path.display()),
            Self::DuplicateBundlePath(path) => write!(formatter, "bundle destination {} is assigned more than once", path.display()),
            Self::SourceOutputOverlap { source, destination } => write!(formatter, "bundle input {} overlaps output {}; choose an output outside every input path", source.display(), destination.display()),
            Self::BundleOutputExists(path) => write!(formatter, "bundle output {} already exists; enable overwrite explicitly to replace it", path.display()),
            Self::BundleIo { operation, path, source } => write!(formatter, "failed to {operation} {}: {source}", path.display()),
            Self::PublishBundle { from, to, source } => write!(formatter, "failed to publish staged bundle {} to {}: {source}", from.display(), to.display()),
            Self::RollbackBundle { backup, destination, source } => write!(formatter, "failed to restore previous bundle {} to {} after publication failed: {source}", backup.display(), destination.display()),
            Self::SerializePackageRecord(source) => write!(formatter, "failed to serialize native package record: {source}"),
            Self::ParsePackageRecord { path, source } => write!(formatter, "failed to parse native package record {}: {source}", path.display()),
            Self::UnsupportedPackageRecordSchema(version) => write!(formatter, "native package record schema {version} is unsupported; rebuild the package with this cargo-fui version"),
            Self::PackageRecordArtifactMissing(path) => write!(formatter, "packaged artifact {} is missing; rebuild the package from verified runtime inputs", path.display()),
            Self::PackageRecordLengthMismatch { path, expected, actual } => write!(formatter, "packaged artifact {} has {actual} bytes but the package record requires {expected}; rebuild the package", path.display()),
            Self::PackageRecordChecksumMismatch { path, expected, actual } => write!(formatter, "packaged artifact {} has SHA-256 {actual} but the package record requires {expected}; rebuild the package from verified inputs", path.display()),
            Self::InvalidArchiveName(path) => write!(formatter, "release archive name {} must be one UTF-8 file name with the extension required by its format", path.display()),
            Self::ArchiveOutputExists(path) => write!(formatter, "release archive output {} already exists; enable overwrite explicitly to replace it", path.display()),
            Self::ArchiveIo { operation, path, source } => write!(formatter, "failed to {operation} release archive artifact {}: {source}", path.display()),
            Self::ArchiveEncoding { format, message } => write!(formatter, "failed to encode deterministic {format} release archive: {message}"),
            Self::ArchiveDecoding { format, message } => write!(formatter, "failed to decode {format} release archive: {message}"),
            Self::ArchiveChecksumMismatch { path, expected, actual } => write!(formatter, "release archive {} has SHA-256 {actual} but its checksum sidecar requires {expected}; download or rebuild the complete artifact set", path.display()),
            Self::ArchivePackageRecordMismatch(path) => write!(formatter, "release archive package record {} does not match the record embedded in the extracted bundle", path.display()),
            Self::UnsupportedArchiveEntry(path) => write!(formatter, "release archive entry {} has an unsupported type or unsafe link target", path.display()),
            Self::InvalidUniversalMacOsSlice { path, message } => write!(formatter, "invalid universal macOS slice {}: {message}", path.display()),
            Self::UniversalMacOsMismatch { field, arm64, x64 } => write!(formatter, "universal macOS slice mismatch for {field}: ARM64 has {arm64:?}, x64 has {x64:?}"),
            Self::UniversalMacOsTool { operation, path, message } => write!(formatter, "failed to {operation} macOS Mach-O {}: {message}", path.display()),
            Self::DistributionTargetMismatch { format, actual } => write!(formatter, "cannot create {format} from package target {actual}"),
            Self::DistributionTool { tool, operation, path, message } => write!(formatter, "{tool} failed to {operation} {}: {message}", path.display()),
            Self::ParseNativeRuntimeManifest(source) => write!(formatter, "failed to parse native runtime manifest: {source}"),
            Self::SerializeNativeRuntimeManifest(source) => write!(formatter, "failed to serialize native runtime manifest: {source}"),
            Self::InvalidNativeRuntimeManifest(message) => write!(formatter, "invalid native runtime manifest: {message}"),
        }
    }
}

impl std::error::Error for Error {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            Self::ReadFile { source, .. } => Some(source),
            Self::BundleIo { source, .. }
            | Self::ArchiveIo { source, .. }
            | Self::PublishBundle { source, .. }
            | Self::RollbackBundle { source, .. } => Some(source),
            Self::SerializePackageRecord(source) => Some(source),
            Self::ParsePackageRecord { source, .. } => Some(source),
            Self::ParseNativeRuntimeManifest(source)
            | Self::SerializeNativeRuntimeManifest(source) => Some(source),
            _ => None,
        }
    }
}
