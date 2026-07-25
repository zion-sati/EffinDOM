use effindom_native_packaging::{
    stage_bundle, verify_bundle, BundleFile, BundleFileRole, Error, OverwritePolicy,
    PackageArchitecture, PackageBuildMode, PackageMetadata, PackageOperatingSystem,
    PackagingInputs,
};
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::atomic::{AtomicU64, Ordering};

static NEXT_TEMP_ID: AtomicU64 = AtomicU64::new(1);

struct Fixture {
    root: PathBuf,
}

impl Fixture {
    fn new() -> Self {
        let root = std::env::temp_dir().join(format!(
            "effindom-native-bundle-{}-{}",
            std::process::id(),
            NEXT_TEMP_ID.fetch_add(1, Ordering::Relaxed)
        ));
        fs::create_dir_all(&root).expect("create fixture");
        Self { root }
    }

    fn file(&self, name: &str, contents: &str) -> PathBuf {
        let path = self.root.join(name);
        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent).expect("create fixture parent");
        }
        fs::write(&path, contents).expect("write fixture file");
        path
    }

    fn inputs(&self, destination: impl AsRef<Path>) -> PackagingInputs {
        PackagingInputs {
            destination: destination.as_ref().to_path_buf(),
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
            application_executable: BundleFile::new(
                self.file("inputs/app", "application"),
                "bin/sample-app",
            ),
            effindom_runtime_libraries: vec![
                BundleFile::new(
                    self.file("inputs/core-runtime", "core runtime"),
                    "lib/libeffindom_core.so",
                ),
                BundleFile::new(
                    self.file("inputs/ui-runtime", "ui runtime"),
                    "lib/libeffindom_ui.so",
                ),
            ],
            third_party_libraries: vec![BundleFile::new(
                self.file("inputs/sdl", "sdl"),
                "lib/libSDL3.so.0",
            )],
            runtime_resources: vec![BundleFile::new(
                self.file("inputs/icu", "icu"),
                "share/effindom/icudt.dat",
            )],
            application_resources: vec![BundleFile::new(
                self.file("inputs/icon", "icon"),
                "share/app/application-icon.png",
            )],
            metadata_artifacts: vec![BundleFile::new(
                self.file("inputs/desktop", "desktop"),
                "share/applications/sample-app.desktop",
            )],
        }
    }
}

impl Drop for Fixture {
    fn drop(&mut self) {
        fs::remove_dir_all(&self.root).expect("remove fixture");
    }
}

#[test]
fn stages_deterministic_payload_and_package_record() {
    let fixture = Fixture::new();
    let first_destination = fixture.root.join("first");
    let first = fixture.inputs(&first_destination);
    let first_record = stage_bundle(&first, OverwritePolicy::Reject).expect("stage first bundle");
    assert_eq!(first_record.schema_version, 2);
    assert_eq!(first_record.files.len(), 7);
    assert_eq!(
        first_record
            .files
            .iter()
            .map(|file| file.path.as_str())
            .collect::<Vec<_>>(),
        vec![
            "bin/sample-app",
            "lib/libSDL3.so.0",
            "lib/libeffindom_core.so",
            "lib/libeffindom_ui.so",
            "share/app/application-icon.png",
            "share/applications/sample-app.desktop",
            "share/effindom/icudt.dat",
        ]
    );
    assert_eq!(
        first_record.files[0].role,
        BundleFileRole::ApplicationExecutable
    );
    assert_eq!(first_record.files[0].byte_length, 11);
    assert_eq!(
        first_record.files[0].sha256,
        "1fe289205936c3fdb61158223892c7a8bee6ff4dfa085ea1c094ce0294e32114"
    );

    let second_destination = fixture.root.join("second");
    let second = fixture.inputs(&second_destination);
    let second_record =
        stage_bundle(&second, OverwritePolicy::Reject).expect("stage second bundle");
    assert_eq!(first_record, second_record);
    assert_eq!(
        fs::read(first_destination.join("share/effindom-package.json")).expect("read first record"),
        fs::read(second_destination.join("share/effindom-package.json"))
            .expect("read second record")
    );
}

#[test]
fn rejects_existing_output_without_mutating_it_and_replaces_explicitly() {
    let fixture = Fixture::new();
    let destination = fixture.root.join("bundle");
    fs::create_dir_all(&destination).expect("create existing output");
    fs::write(destination.join("marker"), "old").expect("write marker");
    let inputs = fixture.inputs(&destination);

    assert!(matches!(
        stage_bundle(&inputs, OverwritePolicy::Reject),
        Err(Error::BundleOutputExists(_))
    ));
    assert_eq!(
        fs::read_to_string(destination.join("marker")).expect("read marker"),
        "old"
    );
    stage_bundle(&inputs, OverwritePolicy::Replace).expect("replace bundle");
    assert!(!destination.join("marker").exists());
    assert!(destination.join("bin/sample-app").is_file());
}

#[test]
fn rejects_invalid_duplicate_missing_and_unsupported_inputs_before_staging() {
    let fixture = Fixture::new();

    let mut traversal = fixture.inputs(fixture.root.join("traversal"));
    traversal.application_resources[0].destination = PathBuf::from("../escape");
    assert!(matches!(
        stage_bundle(&traversal, OverwritePolicy::Reject),
        Err(Error::InvalidBundlePath { .. })
    ));
    assert!(!traversal.destination.exists());

    let mut duplicate = fixture.inputs(fixture.root.join("duplicate"));
    duplicate.third_party_libraries[0].destination = PathBuf::from("bin/sample-app");
    assert!(matches!(
        stage_bundle(&duplicate, OverwritePolicy::Reject),
        Err(Error::DuplicateBundlePath(_))
    ));
    assert!(!duplicate.destination.exists());

    let mut missing = fixture.inputs(fixture.root.join("missing"));
    missing.runtime_resources[0].source = fixture.root.join("not-there");
    assert!(matches!(
        stage_bundle(&missing, OverwritePolicy::Reject),
        Err(Error::MissingBundleInput(_))
    ));
    assert!(!missing.destination.exists());

    let directory = fixture.root.join("input-directory");
    fs::create_dir_all(&directory).expect("create unsupported input");
    let mut unsupported = fixture.inputs(fixture.root.join("unsupported"));
    unsupported.metadata_artifacts[0].source = directory;
    assert!(matches!(
        stage_bundle(&unsupported, OverwritePolicy::Reject),
        Err(Error::UnsupportedBundleInput(_))
    ));
    assert!(!unsupported.destination.exists());
}

#[test]
fn rejects_sources_inside_the_output_tree() {
    let fixture = Fixture::new();
    let destination = fixture.root.join("overlap");
    fs::create_dir_all(&destination).expect("create output");
    let source = destination.join("application");
    fs::write(&source, "application").expect("write overlapping input");
    let mut inputs = fixture.inputs(&destination);
    inputs.application_executable.source = source;

    assert!(matches!(
        stage_bundle(&inputs, OverwritePolicy::Replace),
        Err(Error::SourceOutputOverlap { .. })
    ));
    assert!(!destination.join("bin/sample-app").exists());
}

#[test]
fn verifies_a_relocated_bundle_and_reports_missing_or_corrupt_artifacts() {
    let fixture = Fixture::new();
    let original = fixture.root.join("original");
    let inputs = fixture.inputs(&original);
    let expected = stage_bundle(&inputs, OverwritePolicy::Reject).expect("stage bundle");
    let relocated = fixture.root.join("relocated-parent/bundle");
    fs::create_dir_all(relocated.parent().expect("relocated parent")).expect("create parent");
    fs::rename(&original, &relocated).expect("relocate bundle");

    assert_eq!(
        verify_bundle(&relocated, "share/effindom-package.json").expect("verify relocated"),
        expected
    );

    let runtime = relocated.join("lib/libeffindom_ui.so");
    fs::write(&runtime, "corrupt runtime").expect("corrupt runtime");
    assert!(matches!(
        verify_bundle(&relocated, "share/effindom-package.json"),
        Err(Error::PackageRecordLengthMismatch { .. })
            | Err(Error::PackageRecordChecksumMismatch { .. })
    ));

    fs::remove_file(runtime).expect("remove runtime");
    assert!(matches!(
        verify_bundle(&relocated, "share/effindom-package.json"),
        Err(Error::PackageRecordArtifactMissing(path))
            if path == Path::new("lib/libeffindom_ui.so")
    ));
}

#[test]
fn relocated_recorded_bundle_launches_without_source_tree() {
    if std::env::var_os("EFFINDOM_RELOCATED_BUNDLE_CHILD").is_some() {
        return;
    }

    let fixture = Fixture::new();
    let original = fixture.root.join("original");
    let mut inputs = fixture.inputs(&original);
    inputs.application_executable.source = std::env::current_exe().expect("current test binary");
    inputs.application_executable.destination = if cfg!(windows) {
        PathBuf::from("bin/sample-app.exe")
    } else {
        PathBuf::from("bin/sample-app")
    };

    let expected = stage_bundle(&inputs, OverwritePolicy::Reject).expect("stage runnable bundle");
    let relocated = fixture.root.join("unrelated-parent/application");
    fs::create_dir_all(relocated.parent().expect("relocated parent")).expect("create parent");
    fs::rename(&original, &relocated).expect("relocate runnable bundle");
    assert_eq!(
        verify_bundle(&relocated, "share/effindom-package.json")
            .expect("verify relocated runnable bundle"),
        expected
    );

    let executable = relocated.join(&inputs.application_executable.destination);
    let status = Command::new(executable)
        .args([
            "--exact",
            "relocated_recorded_bundle_launches_without_source_tree",
        ])
        .env("EFFINDOM_RELOCATED_BUNDLE_CHILD", "1")
        .current_dir(&fixture.root)
        .status()
        .expect("launch relocated recorded executable");
    assert!(status.success());
}
