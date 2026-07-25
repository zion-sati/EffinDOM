#![cfg(target_os = "macos")]

use effindom_native_packaging::{
    create_dmg, stage_bundle, BundleFile, DmgInputs, OverwritePolicy, PackageArchitecture,
    PackageBuildMode, PackageMetadata, PackageOperatingSystem, PackagingInputs,
};
use std::fs;
use std::os::unix::fs::PermissionsExt;
use std::path::PathBuf;
use std::process::Command;
use std::sync::atomic::{AtomicU64, Ordering};

static NEXT_TEMP_ID: AtomicU64 = AtomicU64::new(1);

#[test]
fn dmg_contains_verified_app_payload_and_launches_after_mount() {
    let root = std::env::temp_dir().join(format!(
        "effindom-dmg-test-{}-{}",
        std::process::id(),
        NEXT_TEMP_ID.fetch_add(1, Ordering::Relaxed)
    ));
    fs::create_dir_all(root.join("inputs")).expect("create fixture");
    let source = root.join("inputs/sample");
    fs::write(&source, "#!/bin/sh\nprintf 'launched' > \"$1\"\n").expect("write launcher");
    fs::set_permissions(&source, fs::Permissions::from_mode(0o755)).expect("set executable");
    let resource = root.join("inputs/resource.txt");
    fs::write(&resource, "resource").expect("write resource");
    let bundle = root.join("Sample.app");
    let package_record = PathBuf::from("Contents/Resources/effindom-package.json");
    stage_bundle(
        &PackagingInputs {
            destination: bundle.clone(),
            package_record: package_record.clone(),
            metadata: PackageMetadata {
                application_identifier: "dev.effindom.sample".to_string(),
                application_version: "1.0.0".to_string(),
                operating_system: PackageOperatingSystem::MacOs,
                architecture: PackageArchitecture::Arm64,
                target_triple: "aarch64-apple-darwin".to_string(),
                build_mode: PackageBuildMode::Release,
                core_abi: 2,
                ui_abi: 1,
            },
            application_executable: BundleFile::new(source, "Contents/MacOS/sample"),
            effindom_runtime_libraries: vec![],
            third_party_libraries: vec![],
            runtime_resources: vec![],
            application_resources: vec![BundleFile::new(
                resource,
                "Contents/Resources/resource.txt",
            )],
            metadata_artifacts: vec![],
        },
        OverwritePolicy::Reject,
    )
    .expect("stage app");
    let artifact = create_dmg(
        &DmgInputs {
            app_bundle: bundle,
            package_record,
            destination: root.join("Sample.dmg"),
            volume_name: "EffinDOM Sample".to_string(),
            hdiutil: PathBuf::from("/usr/bin/hdiutil"),
        },
        OverwritePolicy::Reject,
    )
    .expect("create and verify DMG");
    assert!(artifact.path.is_file());
    assert!(artifact.checksum.is_file());
    assert!(artifact.package_record.is_file());

    let mount = root.join("mounted");
    fs::create_dir_all(&mount).expect("create mountpoint");
    let attach = Command::new("/usr/bin/hdiutil")
        .args(["attach", "-quiet", "-readonly", "-nobrowse", "-mountpoint"])
        .arg(&mount)
        .arg(&artifact.path)
        .status()
        .expect("attach DMG");
    assert!(attach.success());
    let marker = root.join("launch-marker");
    let launch = Command::new(mount.join("Sample.app/Contents/MacOS/sample"))
        .arg(&marker)
        .status()
        .expect("launch mounted app");
    let detach = Command::new("/usr/bin/hdiutil")
        .args(["detach", "-quiet"])
        .arg(&mount)
        .status()
        .expect("detach DMG");
    assert!(launch.success());
    assert!(detach.success());
    assert_eq!(fs::read_to_string(marker).unwrap(), "launched");
    fs::remove_dir_all(root).expect("remove fixture");
}
