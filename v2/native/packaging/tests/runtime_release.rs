use effindom_native_packaging::{
    decode_native_runtime_bundle_manifest, decode_native_runtime_release_manifest,
    encode_native_runtime_bundle_manifest, encode_native_runtime_release_manifest, Error,
    NativeRuntimeArtifact, NativeRuntimeBundleManifest, NativeRuntimeFile, NativeRuntimeFileRole,
    NativeRuntimeMinimumOs, NativeRuntimeReleaseManifest, NativeRuntimeTarget,
    NATIVE_RUNTIME_MANIFEST_SCHEMA_VERSION, REQUIRED_NATIVE_RUNTIME_TARGETS,
};

const COMMIT: &str = "0123456789abcdef0123456789abcdef01234567";
const HASH: &str = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

fn minimum(target: NativeRuntimeTarget) -> NativeRuntimeMinimumOs {
    let (family, version) = match target {
        NativeRuntimeTarget::MacosArm64 | NativeRuntimeTarget::MacosX64 => ("macos", "13.0"),
        NativeRuntimeTarget::WindowsArm64 | NativeRuntimeTarget::WindowsX64 => {
            ("windows", "10.0.17763")
        }
        NativeRuntimeTarget::LinuxArm64 | NativeRuntimeTarget::LinuxX64 => ("glibc", "2.28"),
    };
    NativeRuntimeMinimumOs {
        family: family.to_string(),
        version: version.to_string(),
    }
}

fn files() -> Vec<NativeRuntimeFile> {
    vec![
        NativeRuntimeFile {
            path: "runtime/fonts/body.ttf".to_string(),
            bytes: 4,
            sha256: HASH.to_string(),
            role: NativeRuntimeFileRole::RuntimeAsset,
            executable: false,
        },
        NativeRuntimeFile {
            path: "tools/effindom-native-packager".to_string(),
            bytes: 8,
            sha256: HASH.to_string(),
            role: NativeRuntimeFileRole::Packager,
            executable: true,
        },
    ]
}

fn artifact(target: NativeRuntimeTarget) -> NativeRuntimeArtifact {
    NativeRuntimeArtifact {
        target,
        archive: target.archive_name(),
        archive_format: target.archive_format(),
        archive_bytes: 12,
        archive_sha256: HASH.to_string(),
        bundle_manifest_sha256: HASH.to_string(),
        core_abi: 2,
        ui_abi: 1,
        minimum_os: minimum(target),
        files: files(),
    }
}

fn release() -> NativeRuntimeReleaseManifest {
    NativeRuntimeReleaseManifest {
        schema_version: NATIVE_RUNTIME_MANIFEST_SCHEMA_VERSION,
        release: "0.2.0-alpha.1".to_string(),
        source_commit: COMMIT.to_string(),
        artifacts: REQUIRED_NATIVE_RUNTIME_TARGETS
            .into_iter()
            .rev()
            .map(artifact)
            .collect(),
    }
}

#[test]
fn release_manifest_round_trips_in_canonical_target_and_file_order() {
    let encoded = encode_native_runtime_release_manifest(&release()).expect("encode release");
    assert_eq!(encoded.last(), Some(&b'\n'));
    let decoded = decode_native_runtime_release_manifest(&encoded).expect("decode release");
    assert_eq!(decoded.release, "0.2.0-alpha.1");
    for target in REQUIRED_NATIVE_RUNTIME_TARGETS {
        assert_eq!(decoded.artifact(target).unwrap().target, target);
    }
    let text = String::from_utf8(encoded).unwrap();
    assert!(text.find("linux-arm64").unwrap() < text.find("macos-arm64").unwrap());
    assert!(text.find("runtime/fonts/body.ttf").unwrap() < text.find("tools/effindom").unwrap());
}

#[test]
fn bundle_manifest_round_trips_and_rejects_unknown_fields() {
    let manifest = NativeRuntimeBundleManifest {
        schema_version: NATIVE_RUNTIME_MANIFEST_SCHEMA_VERSION,
        source_commit: COMMIT.to_string(),
        target: NativeRuntimeTarget::MacosArm64,
        core_abi: 2,
        ui_abi: 1,
        minimum_os: minimum(NativeRuntimeTarget::MacosArm64),
        files: files(),
    };
    let encoded = encode_native_runtime_bundle_manifest(&manifest).expect("encode bundle");
    assert_eq!(
        decode_native_runtime_bundle_manifest(&encoded).expect("decode bundle"),
        manifest
    );
    let mut value: serde_json::Value = serde_json::from_slice(&encoded).unwrap();
    value["unexpected"] = serde_json::json!(true);
    assert!(matches!(
        decode_native_runtime_bundle_manifest(&serde_json::to_vec(&value).unwrap()),
        Err(Error::ParseNativeRuntimeManifest(_))
    ));
}

#[test]
fn release_manifest_rejects_incomplete_duplicate_and_malformed_contracts() {
    let mut manifest = release();
    manifest.artifacts.pop();
    assert!(matches!(
        manifest.validate(),
        Err(Error::InvalidNativeRuntimeManifest(_))
    ));

    let mut manifest = release();
    manifest.artifacts[1] = manifest.artifacts[0].clone();
    assert!(matches!(
        manifest.validate(),
        Err(Error::InvalidNativeRuntimeManifest(_))
    ));

    let mut manifest = release();
    manifest.source_commit = "ABC".to_string();
    assert!(matches!(
        manifest.validate(),
        Err(Error::InvalidNativeRuntimeManifest(_))
    ));

    let mut manifest = release();
    manifest.artifacts[0].archive_sha256 = "bad".to_string();
    assert!(matches!(
        manifest.validate(),
        Err(Error::InvalidNativeRuntimeManifest(_))
    ));

    let mut manifest = release();
    manifest.artifacts[0].files[0].path = "../escape".to_string();
    assert!(matches!(
        manifest.validate(),
        Err(Error::InvalidNativeRuntimeManifest(_))
    ));

    let mut manifest = release();
    manifest.artifacts[0].core_abi = 0;
    assert!(matches!(
        manifest.validate(),
        Err(Error::InvalidNativeRuntimeManifest(_))
    ));
}
