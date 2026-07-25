#![cfg(target_os = "linux")]

use effindom_native_packaging::{
    create_appimage, stage_bundle, AppImageInputs, ApplicationMetadata, BundleFile, IconAlpha,
    IconRaster, IconRasterSet, IconSourceFormat, NativeVersion, OverwritePolicy,
    PackageArchitecture, PackageBuildMode, PackageMetadata, PackageOperatingSystem,
    PackagingInputs,
};
use std::fs;
use std::os::unix::fs::PermissionsExt;
use std::path::PathBuf;
use std::process::Command;
use std::sync::atomic::{AtomicU64, Ordering};

static NEXT_TEMP_ID: AtomicU64 = AtomicU64::new(1);

#[test]
fn appimage_extracts_without_fuse_verifies_and_launches_offline() {
    let Some(appimagetool) = std::env::var_os("EFFINDOM_APPIMAGETOOL") else {
        eprintln!("skipped: set EFFINDOM_APPIMAGETOOL for native AppImage acceptance");
        return;
    };
    let (architecture, target_triple) = if cfg!(target_arch = "aarch64") {
        (PackageArchitecture::Arm64, "aarch64-unknown-linux-gnu")
    } else {
        (PackageArchitecture::X64, "x86_64-unknown-linux-gnu")
    };
    let root = std::env::temp_dir().join(format!(
        "effindom-appimage-test-{}-{}",
        std::process::id(),
        NEXT_TEMP_ID.fetch_add(1, Ordering::Relaxed)
    ));
    fs::create_dir_all(root.join("inputs")).expect("create fixture");
    let source = root.join("inputs/sample");
    fs::write(&source, "#!/bin/sh\nprintf 'launched' > \"$1\"\n").expect("write launcher");
    fs::set_permissions(&source, fs::Permissions::from_mode(0o755)).expect("set executable");
    let resource = root.join("inputs/resource.txt");
    fs::write(&resource, "resource").expect("write resource");
    let bundle = root.join("bundle");
    let package_record = PathBuf::from("share/effindom-package.json");
    stage_bundle(
        &PackagingInputs {
            destination: bundle.clone(),
            package_record: package_record.clone(),
            metadata: PackageMetadata {
                application_identifier: "dev.effindom.sample".to_string(),
                application_version: "1.0.0".to_string(),
                operating_system: PackageOperatingSystem::Linux,
                architecture,
                target_triple: target_triple.to_string(),
                build_mode: PackageBuildMode::Release,
                core_abi: 2,
                ui_abi: 1,
            },
            application_executable: BundleFile::new(source, "bin/sample"),
            effindom_runtime_libraries: vec![],
            third_party_libraries: vec![],
            runtime_resources: vec![],
            application_resources: vec![BundleFile::new(resource, "share/resource.txt")],
            metadata_artifacts: vec![],
        },
        OverwritePolicy::Reject,
    )
    .expect("stage Linux bundle");
    let artifact = create_appimage(
        &AppImageInputs {
            bundle_root: bundle,
            package_record,
            destination: root.join("Sample.AppImage"),
            executable: PathBuf::from("bin/sample"),
            metadata: ApplicationMetadata::new(
                "sample",
                "1.0.0",
                NativeVersion::new(1, 0, 0, 0),
                "EffinDOM Sample",
                "dev.effindom.sample",
            )
            .expect("metadata"),
            categories: vec!["Utility".to_string()],
            icons: opaque_icons(),
            appimagetool: PathBuf::from(appimagetool),
            unsquashfs: PathBuf::from("/usr/bin/unsquashfs"),
        },
        OverwritePolicy::Reject,
    )
    .expect("create and verify AppImage");
    assert!(artifact.path.is_file());
    assert!(artifact.checksum.is_file());
    assert!(artifact.package_record.is_file());

    let extracted = root.join("extracted");
    fs::create_dir_all(&extracted).expect("create extraction directory");
    let bytes = fs::read(&artifact.path).expect("read AppImage");
    let offset = bytes
        .windows(4)
        .rposition(|window| window == b"hsqs")
        .expect("SquashFS offset");
    let extract = Command::new("/usr/bin/unsquashfs")
        .args(["-f", "-d"])
        .arg(extracted.join("squashfs-root"))
        .arg("-o")
        .arg(offset.to_string())
        .arg(&artifact.path)
        .status()
        .expect("extract AppImage");
    assert!(extract.success());
    let marker = root.join("launch-marker");
    let launch = Command::new(extracted.join("squashfs-root/AppRun"))
        .arg(&marker)
        .env_clear()
        .env("PATH", "/usr/bin:/bin")
        .status()
        .expect("launch extracted AppImage");
    assert!(launch.success());
    assert_eq!(fs::read_to_string(marker).unwrap(), "launched");
    fs::remove_dir_all(root).expect("remove fixture");
}

fn opaque_icons() -> IconRasterSet {
    IconRasterSet {
        source_format: IconSourceFormat::Png,
        source_width: 256,
        source_height: 256,
        alpha: IconAlpha::Opaque,
        rasters: vec![IconRaster {
            size: 256,
            rgba8: vec![255; 256 * 256 * 4],
        }],
    }
}
