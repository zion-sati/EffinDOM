#![cfg(target_os = "macos")]

use effindom_native_packaging::{
    assemble_universal_macos_app, create_release_archive, stage_bundle, verify_bundle, BundleFile,
    OverwritePolicy, PackageArchitecture, PackageBuildMode, PackageMetadata,
    PackageOperatingSystem, PackagingInputs, ReleaseArchiveFormat, ReleaseArchiveSpec,
    UniversalMacOsInputs,
};
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::atomic::{AtomicU64, Ordering};

static NEXT_TEMP_ID: AtomicU64 = AtomicU64::new(1);
const PACKAGE_RECORD: &str = "Contents/Resources/effindom-package.json";

struct Fixture(PathBuf);

impl Fixture {
    fn new() -> Self {
        let root = std::env::temp_dir().join(format!(
            "effindom-real-universal-{}-{}",
            std::process::id(),
            NEXT_TEMP_ID.fetch_add(1, Ordering::Relaxed)
        ));
        fs::create_dir_all(&root).expect("create fixture");
        Self(root)
    }
}

impl Drop for Fixture {
    fn drop(&mut self) {
        fs::remove_dir_all(&self.0).expect("remove fixture");
    }
}

#[test]
fn real_lipo_merges_thin_macho_slices_and_archives_deterministically() {
    let fixture = Fixture::new();
    let source = fixture.0.join("sample.c");
    fs::write(&source, "int effindom_sample(void) { return 42; }\n").expect("write source");
    let arm64_object = fixture.0.join("sample-arm64.o");
    let x64_object = fixture.0.join("sample-x64.o");
    compile_object(&source, &arm64_object, "arm64");
    compile_object(&source, &x64_object, "x86_64");

    let resource = fixture.0.join("resource.txt");
    fs::write(&resource, "identical resource\n").expect("write resource");
    let arm64_bundle = stage_object_bundle(
        &fixture.0,
        "arm64",
        arm64_object,
        &resource,
        PackageArchitecture::Arm64,
    );
    let x64_bundle = stage_object_bundle(
        &fixture.0,
        "x64",
        x64_object,
        &resource,
        PackageArchitecture::X64,
    );
    let inputs = UniversalMacOsInputs {
        arm64_bundle,
        x64_bundle,
        destination: fixture.0.join("Sample.app"),
        package_record: PathBuf::from(PACKAGE_RECORD),
    };
    let universal = assemble_universal_macos_app(&inputs, OverwritePolicy::Reject)
        .expect("assemble real universal bundle");
    assert_eq!(
        lipo_architectures(&universal.root.join("Contents/MacOS/sample")),
        vec!["x86_64", "arm64"]
    );
    verify_bundle(&universal.root, PACKAGE_RECORD).expect("verify universal record");

    let spec = ReleaseArchiveSpec {
        archive_name: "sample-macos-universal.tar.zst".to_string(),
        format: ReleaseArchiveFormat::TarZstd,
        package_record: PathBuf::from(PACKAGE_RECORD),
    };
    let first = create_release_archive(
        &universal.root,
        fixture.0.join("first-archive"),
        &spec,
        OverwritePolicy::Reject,
    )
    .expect("archive universal bundle");
    let second = create_release_archive(
        &universal.root,
        fixture.0.join("second-archive"),
        &spec,
        OverwritePolicy::Reject,
    )
    .expect("archive universal bundle again");
    assert_eq!(
        fs::read(first.archive).unwrap(),
        fs::read(second.archive).unwrap()
    );
}

#[test]
fn configured_real_application_slices_merge_relocate_and_launch() {
    let Some(arm64) = std::env::var_os("EFFINDOM_MACOS_ARM64_APP") else {
        return;
    };
    let Some(x64) = std::env::var_os("EFFINDOM_MACOS_X64_APP") else {
        return;
    };
    let fixture = Fixture::new();
    let arm64_bundle = normalize_application_bundle(
        Path::new(&arm64),
        &fixture.0.join("arm64.app"),
        PackageArchitecture::Arm64,
    );
    let x64_bundle = normalize_application_bundle(
        Path::new(&x64),
        &fixture.0.join("x64.app"),
        PackageArchitecture::X64,
    );
    let inputs = UniversalMacOsInputs {
        arm64_bundle,
        x64_bundle,
        destination: fixture.0.join("universal/EffinDOM.app"),
        package_record: PathBuf::from(PACKAGE_RECORD),
    };
    fs::create_dir_all(inputs.destination.parent().unwrap()).expect("create universal parent");
    let artifact = assemble_universal_macos_app(&inputs, OverwritePolicy::Reject)
        .expect("assemble application bundles");
    assert!(artifact.merged_macho_files.len() >= 4);

    let moved = fixture.0.join("moved/EffinDOM.app");
    fs::create_dir_all(moved.parent().unwrap()).expect("create moved parent");
    fs::rename(&artifact.root, &moved).expect("move universal app");
    verify_bundle(&moved, PACKAGE_RECORD).expect("verify moved universal app");
    let executable = moved.join("Contents/MacOS/effindom_v2_macos_native");
    let screenshot = fixture.0.join("universal-app.png");
    let status = Command::new(executable)
        .args(["--hidden", "--screenshot"])
        .arg(&screenshot)
        .current_dir(&fixture.0)
        .status()
        .expect("launch universal app");
    assert!(status.success());
    assert!(fs::metadata(screenshot).expect("inspect screenshot").len() > 0);
}

fn compile_object(source: &Path, output: &Path, architecture: &str) {
    let status = Command::new("/usr/bin/clang")
        .args(["-arch", architecture, "-c"])
        .arg(source)
        .arg("-o")
        .arg(output)
        .status()
        .expect("run clang");
    assert!(status.success());
}

fn stage_object_bundle(
    root: &Path,
    name: &str,
    object: PathBuf,
    resource: &Path,
    architecture: PackageArchitecture,
) -> PathBuf {
    let destination = root.join(format!("{name}.app"));
    stage_bundle(
        &PackagingInputs {
            destination: destination.clone(),
            package_record: PathBuf::from(PACKAGE_RECORD),
            metadata: metadata(architecture),
            application_executable: BundleFile::new(object, "Contents/MacOS/sample"),
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
    .expect("stage object bundle");
    destination
}

fn normalize_application_bundle(
    source: &Path,
    destination: &Path,
    architecture: PackageArchitecture,
) -> PathBuf {
    let executable_relative = PathBuf::from("Contents/MacOS/effindom_v2_macos_native");
    let mut runtime_libraries = Vec::new();
    let mut runtime_resources = Vec::new();
    let mut application_resources = Vec::new();
    let mut metadata_artifacts = Vec::new();
    collect_files(source, source, &mut |path, relative| {
        if relative == executable_relative || relative == Path::new(PACKAGE_RECORD) {
            return;
        }
        let file = BundleFile::new(path, relative.clone());
        if relative.starts_with("Contents/Frameworks") {
            runtime_libraries.push(file);
        } else if relative.starts_with("Contents/Resources/effindom") {
            runtime_resources.push(file);
        } else if relative.starts_with("Contents/Resources/app") {
            application_resources.push(file);
        } else {
            metadata_artifacts.push(file);
        }
    });
    stage_bundle(
        &PackagingInputs {
            destination: destination.to_path_buf(),
            package_record: PathBuf::from(PACKAGE_RECORD),
            metadata: metadata(architecture),
            application_executable: BundleFile::new(
                source.join(&executable_relative),
                executable_relative,
            ),
            effindom_runtime_libraries: runtime_libraries,
            third_party_libraries: vec![],
            runtime_resources,
            application_resources,
            metadata_artifacts,
        },
        OverwritePolicy::Reject,
    )
    .expect("normalize real application bundle");
    destination.to_path_buf()
}

fn collect_files(root: &Path, directory: &Path, visitor: &mut impl FnMut(PathBuf, PathBuf)) {
    let mut entries = fs::read_dir(directory)
        .expect("read application bundle")
        .map(|entry| entry.expect("read application entry"))
        .collect::<Vec<_>>();
    entries.sort_by_key(|entry| entry.file_name());
    for entry in entries {
        let path = entry.path();
        let metadata = fs::symlink_metadata(&path).expect("inspect application entry");
        if metadata.is_dir() {
            collect_files(root, &path, visitor);
        } else if metadata.is_file() {
            visitor(path.clone(), path.strip_prefix(root).unwrap().to_path_buf());
        }
    }
}

fn metadata(architecture: PackageArchitecture) -> PackageMetadata {
    PackageMetadata {
        application_identifier: "dev.effindom.native-demo".to_string(),
        application_version: "0.1.0".to_string(),
        operating_system: PackageOperatingSystem::MacOs,
        architecture,
        target_triple: match architecture {
            PackageArchitecture::Arm64 => "aarch64-apple-darwin",
            PackageArchitecture::X64 => "x86_64-apple-darwin",
            PackageArchitecture::Universal => unreachable!(),
        }
        .to_string(),
        build_mode: PackageBuildMode::Release,
        core_abi: 2,
        ui_abi: 1,
    }
}

fn lipo_architectures(path: &Path) -> Vec<String> {
    let output = Command::new("/usr/bin/lipo")
        .arg("-archs")
        .arg(path)
        .output()
        .expect("inspect universal output");
    assert!(output.status.success());
    let value = String::from_utf8(output.stdout).expect("decode lipo output");
    value.split_whitespace().map(str::to_string).collect()
}
