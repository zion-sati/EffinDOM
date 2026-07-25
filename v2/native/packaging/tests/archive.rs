use effindom_native_packaging::{
    create_release_archive, extract_release_archive, stage_bundle, verify_bundle, BundleFile,
    Error, OverwritePolicy, PackageArchitecture, PackageBuildMode, PackageMetadata,
    PackageOperatingSystem, PackagingInputs, ReleaseArchiveFormat, ReleaseArchiveSpec,
};
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};

static NEXT_TEMP_ID: AtomicU64 = AtomicU64::new(1);

struct Fixture {
    root: PathBuf,
    bundle: PathBuf,
}

impl Fixture {
    fn new() -> Self {
        let root = std::env::temp_dir().join(format!(
            "effindom-release-archive-{}-{}",
            std::process::id(),
            NEXT_TEMP_ID.fetch_add(1, Ordering::Relaxed)
        ));
        fs::create_dir_all(root.join("inputs")).expect("create fixture");
        let executable = root.join("inputs/sample-app");
        fs::write(&executable, "application").expect("write executable");
        set_executable(&executable);
        let runtime = root.join("inputs/runtime");
        fs::write(&runtime, "runtime").expect("write runtime");
        let icon = root.join("inputs/icon");
        fs::write(&icon, "icon").expect("write icon");
        let bundle = root.join("bundle");
        stage_bundle(
            &PackagingInputs {
                destination: bundle.clone(),
                package_record: PathBuf::from("share/effindom-package.json"),
                metadata: PackageMetadata {
                    application_identifier: "dev.effindom.sample".to_string(),
                    application_version: "1.2.3".to_string(),
                    operating_system: PackageOperatingSystem::Linux,
                    architecture: PackageArchitecture::X64,
                    target_triple: "x86_64-unknown-linux-gnu".to_string(),
                    build_mode: PackageBuildMode::Release,
                    core_abi: 2,
                    ui_abi: 1,
                },
                application_executable: BundleFile::new(executable, "bin/sample-app"),
                effindom_runtime_libraries: vec![BundleFile::new(runtime, "lib/libeffindom.so")],
                third_party_libraries: vec![],
                runtime_resources: vec![],
                application_resources: vec![BundleFile::new(icon, "share/app/icon.png")],
                metadata_artifacts: vec![],
            },
            OverwritePolicy::Reject,
        )
        .expect("stage fixture bundle");
        Self { root, bundle }
    }

    fn spec(format: ReleaseArchiveFormat) -> ReleaseArchiveSpec {
        ReleaseArchiveSpec {
            archive_name: match format {
                ReleaseArchiveFormat::TarZstd => "sample-app.tar.zst",
                ReleaseArchiveFormat::Zip => "sample-app.zip",
            }
            .to_string(),
            format,
            package_record: PathBuf::from("share/effindom-package.json"),
        }
    }
}

impl Drop for Fixture {
    fn drop(&mut self) {
        fs::remove_dir_all(&self.root).expect("remove fixture");
    }
}

#[test]
fn creates_byte_identical_archives_and_sidecars() {
    for format in [ReleaseArchiveFormat::TarZstd, ReleaseArchiveFormat::Zip] {
        let fixture = Fixture::new();
        let spec = Fixture::spec(format);
        let first = create_release_archive(
            &fixture.bundle,
            fixture.root.join("first"),
            &spec,
            OverwritePolicy::Reject,
        )
        .expect("create first archive");
        let second = create_release_archive(
            &fixture.bundle,
            fixture.root.join("second"),
            &spec,
            OverwritePolicy::Reject,
        )
        .expect("create second archive");

        assert_eq!(
            fs::read(&first.archive).unwrap(),
            fs::read(&second.archive).unwrap()
        );
        assert_eq!(
            fs::read(&first.checksum).unwrap(),
            fs::read(&second.checksum).unwrap()
        );
        assert_eq!(
            fs::read(&first.package_record).unwrap(),
            fs::read(&second.package_record).unwrap()
        );
        assert_eq!(first.archive_sha256, second.archive_sha256);
    }
}

#[test]
fn extracts_to_a_new_parent_and_reverifies_the_bundle() {
    for format in [ReleaseArchiveFormat::TarZstd, ReleaseArchiveFormat::Zip] {
        let fixture = Fixture::new();
        let spec = Fixture::spec(format);
        let artifact = create_release_archive(
            &fixture.bundle,
            fixture.root.join("release"),
            &spec,
            OverwritePolicy::Reject,
        )
        .expect("create release archive");
        fs::remove_dir_all(&fixture.bundle).expect("remove source bundle");
        let extracted = fixture.root.join("unrelated/relocated");
        fs::create_dir_all(extracted.parent().unwrap()).expect("create unrelated parent");
        let record =
            extract_release_archive(&artifact.root, &extracted, &spec, OverwritePolicy::Reject)
                .expect("extract and verify");
        assert_eq!(
            verify_bundle(&extracted, &spec.package_record).expect("verify extracted bundle"),
            record
        );
        assert_eq!(
            fs::read(extracted.join("bin/sample-app")).unwrap(),
            b"application"
        );
        assert_executable(&extracted.join("bin/sample-app"), format);
    }
}

#[test]
fn rejects_corrupt_incomplete_mismatched_and_invalid_artifacts() {
    let fixture = Fixture::new();
    let spec = Fixture::spec(ReleaseArchiveFormat::TarZstd);
    let artifact = create_release_archive(
        &fixture.bundle,
        fixture.root.join("release"),
        &spec,
        OverwritePolicy::Reject,
    )
    .expect("create release archive");

    fs::write(&artifact.archive, "corrupt").expect("corrupt archive");
    assert!(matches!(
        extract_release_archive(
            &artifact.root,
            fixture.root.join("corrupt-output"),
            &spec,
            OverwritePolicy::Reject
        ),
        Err(Error::ArchiveChecksumMismatch { .. })
    ));

    let replacement = create_release_archive(
        &fixture.bundle,
        fixture.root.join("release"),
        &spec,
        OverwritePolicy::Replace,
    )
    .expect("replace release archive");
    fs::remove_file(&replacement.checksum).expect("remove checksum");
    assert!(matches!(
        extract_release_archive(
            &replacement.root,
            fixture.root.join("missing-output"),
            &spec,
            OverwritePolicy::Reject
        ),
        Err(Error::ArchiveIo { .. })
    ));

    let replacement = create_release_archive(
        &fixture.bundle,
        fixture.root.join("release"),
        &spec,
        OverwritePolicy::Replace,
    )
    .expect("replace release archive again");
    fs::write(&replacement.package_record, "{}\n").expect("replace sidecar record");
    assert!(matches!(
        extract_release_archive(
            &replacement.root,
            fixture.root.join("mismatch-output"),
            &spec,
            OverwritePolicy::Reject
        ),
        Err(Error::ArchivePackageRecordMismatch(_))
    ));

    let invalid = ReleaseArchiveSpec {
        archive_name: "../escape.tar.zst".to_string(),
        ..spec
    };
    assert!(matches!(
        create_release_archive(
            &fixture.bundle,
            fixture.root.join("invalid"),
            &invalid,
            OverwritePolicy::Reject
        ),
        Err(Error::InvalidArchiveName(_))
    ));
}

#[cfg(unix)]
#[test]
fn tar_zstd_preserves_safe_relative_symlinks_and_rejects_escaping_links() {
    use std::os::unix::fs::symlink;

    let fixture = Fixture::new();
    symlink("libeffindom.so", fixture.bundle.join("lib/current.so")).expect("create safe link");
    let spec = Fixture::spec(ReleaseArchiveFormat::TarZstd);
    let artifact = create_release_archive(
        &fixture.bundle,
        fixture.root.join("safe-release"),
        &spec,
        OverwritePolicy::Reject,
    )
    .expect("archive safe link");
    let extracted = fixture.root.join("safe-extracted");
    extract_release_archive(&artifact.root, &extracted, &spec, OverwritePolicy::Reject)
        .expect("extract safe link");
    assert_eq!(
        fs::read_link(extracted.join("lib/current.so")).expect("read extracted link"),
        Path::new("libeffindom.so")
    );

    symlink("../../../outside", fixture.bundle.join("lib/escape.so"))
        .expect("create escaping link");
    assert!(matches!(
        create_release_archive(
            &fixture.bundle,
            fixture.root.join("unsafe-release"),
            &spec,
            OverwritePolicy::Reject
        ),
        Err(Error::UnsupportedArchiveEntry(_))
    ));
}

#[cfg(unix)]
fn set_executable(path: &Path) {
    use std::os::unix::fs::PermissionsExt;
    fs::set_permissions(path, fs::Permissions::from_mode(0o755)).expect("set executable mode");
}

#[cfg(not(unix))]
fn set_executable(_path: &Path) {}

#[cfg(unix)]
fn assert_executable(path: &Path, format: ReleaseArchiveFormat) {
    use std::os::unix::fs::PermissionsExt;
    if format == ReleaseArchiveFormat::TarZstd {
        assert_ne!(fs::metadata(path).unwrap().permissions().mode() & 0o111, 0);
    }
}

#[cfg(not(unix))]
fn assert_executable(_path: &Path, _format: ReleaseArchiveFormat) {}
