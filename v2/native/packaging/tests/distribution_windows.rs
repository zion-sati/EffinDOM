#![cfg(target_os = "windows")]

use effindom_native_packaging::{
    create_msix, stage_bundle, ApplicationMetadata, BundleFile, IconAlpha, IconRaster,
    IconRasterSet, IconSourceFormat, MsixInputs, NativeVersion, OverwritePolicy,
    PackageArchitecture, PackageBuildMode, PackageMetadata, PackageOperatingSystem,
    PackagingInputs,
};
use std::fs;
use std::path::PathBuf;
use std::process::Command;
use std::sync::atomic::{AtomicU64, Ordering};

static NEXT_TEMP_ID: AtomicU64 = AtomicU64::new(1);

#[test]
fn msix_packs_unpacks_verifies_and_launches_the_canonical_payload() {
    let Some(makeappx) = std::env::var_os("EFFINDOM_MAKEAPPX") else {
        eprintln!("skipped: set EFFINDOM_MAKEAPPX for native MSIX acceptance");
        return;
    };
    let architecture_name =
        std::env::var("EFFINDOM_TEST_PACKAGE_ARCHITECTURE").unwrap_or_else(|_| "x64".to_string());
    let (architecture, target_triple) = match architecture_name.as_str() {
        "arm64" => (PackageArchitecture::Arm64, "aarch64-pc-windows-msvc"),
        "x64" => (PackageArchitecture::X64, "x86_64-pc-windows-msvc"),
        value => panic!("unsupported test package architecture: {value}"),
    };
    let root = std::env::temp_dir().join(format!(
        "effindom-msix-test-{}-{}",
        std::process::id(),
        NEXT_TEMP_ID.fetch_add(1, Ordering::Relaxed)
    ));
    fs::create_dir_all(&root).expect("create fixture");
    let windows = PathBuf::from(std::env::var_os("WINDIR").expect("WINDIR"));
    let source_executable = windows.join("System32/where.exe");
    assert!(source_executable.is_file());
    let resource = root.join("resource.txt");
    fs::write(&resource, "resource").expect("write resource");
    let bundle = root.join("bundle");
    let package_record = PathBuf::from("effindom-package.json");
    stage_bundle(
        &PackagingInputs {
            destination: bundle.clone(),
            package_record: package_record.clone(),
            metadata: PackageMetadata {
                application_identifier: "dev.effindom.sample".to_string(),
                application_version: "1.0.0".to_string(),
                operating_system: PackageOperatingSystem::Windows,
                architecture,
                target_triple: target_triple.to_string(),
                build_mode: PackageBuildMode::Release,
                core_abi: 2,
                ui_abi: 1,
            },
            application_executable: BundleFile::new(source_executable, "sample.exe"),
            effindom_runtime_libraries: vec![],
            third_party_libraries: vec![],
            runtime_resources: vec![],
            application_resources: vec![BundleFile::new(resource, "resource.txt")],
            metadata_artifacts: vec![],
        },
        OverwritePolicy::Reject,
    )
    .expect("stage Windows bundle");
    let artifact = create_msix(
        &MsixInputs {
            bundle_root: bundle,
            package_record,
            destination: root.join("Sample.msix"),
            executable: PathBuf::from("sample.exe"),
            metadata: ApplicationMetadata::new(
                "sample",
                "1.0.0",
                NativeVersion::new(1, 0, 0, 0),
                "EffinDOM Sample",
                "dev.effindom.sample",
            )
            .expect("metadata"),
            publisher: "CN=EffinDOM Test".to_string(),
            publisher_display_name: "EffinDOM".to_string(),
            icons: opaque_icons(),
            makeappx: PathBuf::from(&makeappx),
        },
        OverwritePolicy::Reject,
    )
    .expect("create and verify MSIX");
    assert!(artifact.path.is_file());
    assert!(artifact.checksum.is_file());
    assert!(artifact.package_record.is_file());

    let extracted = root.join("unpacked");
    let unpack = Command::new(&makeappx)
        .args(["unpack", "/o", "/p"])
        .arg(&artifact.path)
        .arg("/d")
        .arg(&extracted)
        .status()
        .expect("unpack MSIX");
    assert!(unpack.success());
    let manifest = fs::read_to_string(extracted.join("AppxManifest.xml")).expect("manifest");
    assert!(manifest.contains(&format!("ProcessorArchitecture=\"{architecture_name}\"")));
    let launch = Command::new(extracted.join("sample.exe"))
        .arg("cmd.exe")
        .output()
        .expect("launch unpacked payload");
    assert!(launch.status.success());
    assert!(String::from_utf8_lossy(&launch.stdout)
        .to_ascii_lowercase()
        .contains("cmd.exe"));
    fs::remove_dir_all(root).expect("remove fixture");
}

fn opaque_icons() -> IconRasterSet {
    IconRasterSet {
        source_format: IconSourceFormat::Png,
        source_width: 256,
        source_height: 256,
        alpha: IconAlpha::Opaque,
        rasters: [44, 50, 150]
            .into_iter()
            .map(|size| IconRaster {
                size,
                rgba8: vec![255; size as usize * size as usize * 4],
            })
            .collect(),
    }
}
