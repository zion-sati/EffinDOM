use effindom_native_packaging::{
    create_native_runtime_artifact, extract_native_runtime_artifact, Error,
    NativeRuntimeArtifactInput, NativeRuntimeArtifactRequest, NativeRuntimeFileRole,
    NativeRuntimeMinimumOs, NativeRuntimeTarget, OverwritePolicy,
    NATIVE_RUNTIME_MANIFEST_SCHEMA_VERSION,
};
use std::fs;
use std::path::PathBuf;
use std::process::Command;
use std::sync::atomic::{AtomicU64, Ordering};

static NEXT_ID: AtomicU64 = AtomicU64::new(1);
const COMMIT: &str = "0123456789abcdef0123456789abcdef01234567";

struct Fixture {
    root: PathBuf,
}

impl Fixture {
    fn new() -> Self {
        let root = std::env::temp_dir().join(format!(
            "effindom-runtime-artifact-{}-{}",
            std::process::id(),
            NEXT_ID.fetch_add(1, Ordering::Relaxed)
        ));
        fs::create_dir_all(root.join("inputs")).unwrap();
        fs::write(root.join("inputs/host.a"), b"host library").unwrap();
        fs::write(root.join("inputs/font.ttf"), b"font").unwrap();
        Self { root }
    }

    fn request(
        &self,
        target: NativeRuntimeTarget,
        destination: &str,
    ) -> NativeRuntimeArtifactRequest {
        NativeRuntimeArtifactRequest {
            schema_version: NATIVE_RUNTIME_MANIFEST_SCHEMA_VERSION,
            source_commit: COMMIT.to_string(),
            target,
            core_abi: 2,
            ui_abi: 1,
            minimum_os: NativeRuntimeMinimumOs {
                family: match target {
                    NativeRuntimeTarget::WindowsArm64 | NativeRuntimeTarget::WindowsX64 => {
                        "windows"
                    }
                    NativeRuntimeTarget::MacosArm64 | NativeRuntimeTarget::MacosX64 => "macos",
                    _ => "glibc",
                }
                .to_string(),
                version: "1.0".to_string(),
            },
            destination: self.root.join(destination),
            files: vec![
                NativeRuntimeArtifactInput {
                    source: self.root.join("inputs/host.a"),
                    path: "sdk/lib/host.a".to_string(),
                    role: NativeRuntimeFileRole::HostLibrary,
                    executable: false,
                },
                NativeRuntimeArtifactInput {
                    source: self.root.join("inputs/font.ttf"),
                    path: "runtime/assets/fonts/font.ttf".to_string(),
                    role: NativeRuntimeFileRole::RuntimeAsset,
                    executable: false,
                },
            ],
        }
    }
}

impl Drop for Fixture {
    fn drop(&mut self) {
        fs::remove_dir_all(&self.root).unwrap();
    }
}

#[test]
fn creates_deterministic_tar_and_zip_artifacts_and_reverifies_extraction() {
    for target in [
        NativeRuntimeTarget::MacosArm64,
        NativeRuntimeTarget::WindowsX64,
    ] {
        let fixture = Fixture::new();
        let first = create_native_runtime_artifact(
            &fixture.request(target, "first"),
            OverwritePolicy::Reject,
        )
        .unwrap();
        let second = create_native_runtime_artifact(
            &fixture.request(target, "second"),
            OverwritePolicy::Reject,
        )
        .unwrap();
        assert_eq!(
            fs::read(&first.archive).unwrap(),
            fs::read(&second.archive).unwrap()
        );
        assert_eq!(first.artifact, second.artifact);

        let extracted = fixture.root.join("relocated/runtime");
        fs::create_dir_all(extracted.parent().unwrap()).unwrap();
        let manifest = extract_native_runtime_artifact(
            &first.root,
            &extracted,
            COMMIT,
            &first.artifact,
            OverwritePolicy::Reject,
        )
        .unwrap();
        assert_eq!(manifest.files, first.artifact.files);
        assert_eq!(
            fs::read(extracted.join("sdk/lib/host.a")).unwrap(),
            b"host library"
        );
    }
}

#[test]
fn rejects_corruption_unsafe_paths_duplicates_and_unrecorded_payloads() {
    let fixture = Fixture::new();
    let output = create_native_runtime_artifact(
        &fixture.request(NativeRuntimeTarget::LinuxX64, "release"),
        OverwritePolicy::Reject,
    )
    .unwrap();
    fs::write(&output.archive, b"corrupt").unwrap();
    assert!(matches!(
        extract_native_runtime_artifact(
            &output.root,
            fixture.root.join("corrupt"),
            COMMIT,
            &output.artifact,
            OverwritePolicy::Reject,
        ),
        Err(Error::ArchiveChecksumMismatch { .. })
    ));

    let mut request = fixture.request(NativeRuntimeTarget::LinuxX64, "unsafe");
    request.files[0].path = "../escape".to_string();
    assert!(matches!(
        create_native_runtime_artifact(&request, OverwritePolicy::Reject),
        Err(Error::InvalidBundlePath { .. })
    ));

    let mut request = fixture.request(NativeRuntimeTarget::LinuxX64, "duplicate");
    request.files[1].path = request.files[0].path.clone();
    assert!(matches!(
        create_native_runtime_artifact(&request, OverwritePolicy::Reject),
        Err(Error::DuplicateBundlePath(_))
    ));
}

#[test]
fn cli_creates_the_same_verified_runtime_artifact() {
    let fixture = Fixture::new();
    let request = fixture.request(NativeRuntimeTarget::LinuxArm64, "cli-release");
    let request_path = fixture.root.join("request.json");
    fs::write(&request_path, serde_json::to_vec_pretty(&request).unwrap()).unwrap();

    let output = Command::new(env!("CARGO_BIN_EXE_effindom-native-packager"))
        .args(["create-runtime-artifact", request_path.to_str().unwrap()])
        .output()
        .unwrap();
    assert!(
        output.status.success(),
        "packager failed: {}",
        String::from_utf8_lossy(&output.stderr)
    );
    let created: serde_json::Value = serde_json::from_slice(&output.stdout).unwrap();
    assert_eq!(
        created["artifact"]["target"],
        serde_json::Value::String("linux-arm64".to_string())
    );
    assert!(fixture
        .root
        .join("cli-release/effindom-native-linux-arm64.tar.zst")
        .is_file());
}
