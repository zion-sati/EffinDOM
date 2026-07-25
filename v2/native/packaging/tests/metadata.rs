use effindom_native_packaging::{
    encode_application_icon_png, encode_linux_desktop_entry, encode_macos_info_plist,
    encode_windows_resource_script, ApplicationMetadata, Error, IconAlpha, IconRaster,
    IconRasterSet, IconSourceFormat, NativeVersion,
};
use image::GenericImageView;
use std::fs;
use std::process::Command;
use std::sync::atomic::{AtomicU64, Ordering};

#[cfg(windows)]
use effindom_native_packaging::{encode_windows_ico, CANONICAL_ICON_SIZES};

static NEXT_TEMP_ID: AtomicU64 = AtomicU64::new(1);

fn metadata() -> ApplicationMetadata {
    ApplicationMetadata::new(
        "sample-app",
        "1.2.3-alpha.4",
        NativeVersion::new(1, 2, 3, 4),
        "Sample & <Tools>",
        "dev.effindom.sample",
    )
    .expect("valid metadata")
}

#[test]
fn macos_plist_contains_bundle_identity_icon_and_escaped_caption() {
    let artifact = encode_macos_info_plist(&metadata(), Some("13.0")).expect("encode plist");
    let plist = String::from_utf8(artifact.bytes).expect("UTF-8 plist");
    assert_eq!(artifact.relative_path.to_string_lossy(), "Info.plist");
    assert!(plist.contains("<key>CFBundleIconFile</key>\n  <string>application.icns</string>"));
    assert!(plist.contains("<string>dev.effindom.sample</string>"));
    assert!(plist.contains("<string>Sample &amp; &lt;Tools&gt;</string>"));
    assert!(plist.contains("<key>CFBundleVersion</key>\n  <string>1.2.3</string>"));
    assert!(plist.contains("<key>LSMinimumSystemVersion</key>\n  <string>13.0</string>"));
}

#[test]
fn windows_resource_contains_icon_version_and_utf8_string_table() {
    let artifact =
        encode_windows_resource_script(&metadata(), Some("EffinDOM Labs")).expect("encode RC");
    let resource = String::from_utf8(artifact.bytes).expect("UTF-8 RC");
    assert_eq!(artifact.relative_path.to_string_lossy(), "application.rc");
    assert!(resource.contains("1 ICON \"application.ico\""));
    assert!(resource.contains("FILEVERSION 1,2,3,4"));
    assert!(resource.contains("VALUE \"FileVersion\", \"1.2.3-alpha.4\\0\""));
    assert!(resource.contains("VALUE \"CompanyName\", \"EffinDOM Labs\\0\""));
}

#[test]
fn linux_desktop_entry_uses_icon_name_and_categories() {
    let artifact =
        encode_linux_desktop_entry(&metadata(), &["Utility".into(), "Development".into()])
            .expect("encode desktop entry");
    let desktop = String::from_utf8(artifact.bytes).expect("UTF-8 desktop entry");
    assert_eq!(
        artifact.relative_path.to_string_lossy(),
        "sample-app.desktop"
    );
    assert!(desktop.contains("Name=Sample & <Tools>\n"));
    assert!(desktop.contains("Exec=sample-app\nIcon=sample-app\n"));
    assert!(desktop.contains("Categories=Utility;Development;\n"));
}

#[test]
fn canonical_runtime_icon_is_a_decodable_256_png() {
    let rasters = IconRasterSet {
        source_format: IconSourceFormat::Svg,
        source_width: 256,
        source_height: 256,
        alpha: IconAlpha::Opaque,
        rasters: vec![IconRaster {
            size: 256,
            rgba8: vec![0x7f; 256 * 256 * 4],
        }],
    };
    let artifact = encode_application_icon_png(&rasters).expect("encode application icon");
    assert_eq!(
        artifact.relative_path.to_string_lossy(),
        "app/application-icon.png"
    );
    assert_eq!(
        image::load_from_memory(&artifact.bytes)
            .expect("decode PNG")
            .dimensions(),
        (256, 256)
    );
}

#[test]
fn metadata_rejects_unsafe_names_and_platform_values() {
    assert!(matches!(
        ApplicationMetadata::new(
            "../escape",
            "1.0.0",
            NativeVersion::new(1, 0, 0, 0),
            "Sample",
            "dev.effindom.sample"
        ),
        Err(Error::InvalidApplicationMetadata {
            field: "executable-name",
            ..
        })
    ));
    assert!(matches!(
        encode_linux_desktop_entry(&metadata(), &["Bad;Category".into()]),
        Err(Error::InvalidApplicationMetadata {
            field: "linux.categories",
            ..
        })
    ));
    assert!(matches!(
        encode_windows_resource_script(&metadata(), Some("")),
        Err(Error::InvalidApplicationMetadata {
            field: "windows.publisher",
            ..
        })
    ));
}

#[test]
fn linux_desktop_entry_passes_the_platform_validator_when_requested() {
    if std::env::var_os("EFFINDOM_VALIDATE_LINUX_DESKTOP").is_none() {
        return;
    }

    let artifact =
        encode_linux_desktop_entry(&metadata(), &["Utility".into(), "Development".into()])
            .expect("encode desktop entry");
    let id = NEXT_TEMP_ID.fetch_add(1, Ordering::Relaxed);
    let root = std::env::temp_dir().join(format!(
        "effindom-native-packaging-desktop-{}-{id}",
        std::process::id()
    ));
    fs::create_dir_all(&root).expect("create desktop validation directory");
    let path = root.join(artifact.relative_path);
    fs::write(&path, artifact.bytes).expect("write desktop entry");

    let output = Command::new("desktop-file-validate")
        .arg(&path)
        .output()
        .expect("run desktop-file-validate");
    fs::remove_dir_all(root).expect("remove desktop validation directory");
    assert!(
        output.status.success(),
        "desktop-file-validate failed: {}",
        String::from_utf8_lossy(&output.stderr)
    );
}

#[cfg(windows)]
#[test]
fn windows_resources_compile_with_the_platform_tool_when_requested() {
    if std::env::var_os("EFFINDOM_VALIDATE_WINDOWS_RESOURCES").is_none() {
        return;
    }

    let rasters = IconRasterSet {
        source_format: IconSourceFormat::Svg,
        source_width: 1024,
        source_height: 1024,
        alpha: IconAlpha::HasTransparency,
        rasters: CANONICAL_ICON_SIZES
            .iter()
            .map(|size| IconRaster {
                size: *size,
                rgba8: vec![0x7f; (*size * *size * 4) as usize],
            })
            .collect(),
    };
    let resource = encode_windows_resource_script(&metadata(), Some("EffinDOM Labs"))
        .expect("encode Windows resource script");
    let id = NEXT_TEMP_ID.fetch_add(1, Ordering::Relaxed);
    let root = std::env::temp_dir().join(format!(
        "effindom-native-packaging-windows-{}-{id}",
        std::process::id()
    ));
    fs::create_dir_all(&root).expect("create Windows resource validation directory");
    fs::write(
        root.join("application.ico"),
        encode_windows_ico(&rasters).expect("encode Windows icon"),
    )
    .expect("write Windows icon");
    fs::write(root.join(resource.relative_path), resource.bytes)
        .expect("write Windows resource script");

    let compiled = Command::new("rc.exe")
        .current_dir(&root)
        .args(["/nologo", "/fo", "application.res", "application.rc"])
        .output()
        .expect("run rc.exe");
    assert!(
        compiled.status.success(),
        "rc.exe rejected generated resources:\n{}{}",
        String::from_utf8_lossy(&compiled.stdout),
        String::from_utf8_lossy(&compiled.stderr)
    );
    assert!(
        fs::metadata(root.join("application.res"))
            .expect("read compiled resource")
            .len()
            > 0,
        "rc.exe produced an empty resource"
    );

    fs::write(root.join("application.ico"), b"not an icon").expect("write malformed Windows icon");
    let rejected = Command::new("rc.exe")
        .current_dir(&root)
        .args(["/nologo", "/fo", "invalid.res", "application.rc"])
        .output()
        .expect("run rc.exe against malformed icon");
    assert!(
        !rejected.status.success(),
        "rc.exe accepted a malformed icon"
    );
    assert!(
        !rejected.stdout.is_empty() || !rejected.stderr.is_empty(),
        "rc.exe rejected malformed input without a diagnostic"
    );

    fs::remove_dir_all(root).expect("remove Windows resource validation directory");
}
