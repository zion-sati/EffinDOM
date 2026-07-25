use super::common::{
    copy_tree, create_dir_all, finalize_artifact, prepare_output, run_tool, unique_path,
    verified_record, write_executable, DistributionArtifact,
};
use crate::icon_encoding::encode_png;
use crate::{
    encode_linux_desktop_entry, ApplicationMetadata, Error, IconRasterSet, OverwritePolicy,
    PackageArchitecture, PackageOperatingSystem, Result,
};
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct AppImageInputs {
    pub bundle_root: PathBuf,
    pub package_record: PathBuf,
    pub destination: PathBuf,
    pub executable: PathBuf,
    pub metadata: ApplicationMetadata,
    pub categories: Vec<String>,
    pub icons: IconRasterSet,
    pub appimagetool: PathBuf,
    pub unsquashfs: PathBuf,
}

pub fn create_appimage(
    inputs: &AppImageInputs,
    overwrite: OverwritePolicy,
) -> Result<DistributionArtifact> {
    let record = verified_record(
        &inputs.bundle_root,
        &inputs.package_record,
        PackageOperatingSystem::Linux,
        "AppImage",
    )?;
    let architecture = match record.metadata.architecture {
        PackageArchitecture::X64 => "x86_64",
        PackageArchitecture::Arm64 => "aarch64",
        PackageArchitecture::Universal => {
            return Err(Error::DistributionTargetMismatch {
                format: "AppImage",
                actual: "Linux/universal".to_string(),
            });
        }
    };
    let destination = prepare_output(&inputs.destination, overwrite)?;
    let app_dir = unique_path(&destination, "AppDir")?;
    create_dir_all(&app_dir, "create AppDir")?;
    let temporary = unique_path(&destination, "appimage-output")?;
    let result = (|| {
        prepare_layout(inputs, &app_dir)?;
        run_tool(
            "appimagetool",
            "create AppImage",
            &destination,
            Command::new(&inputs.appimagetool)
                .arg("--no-appstream")
                .arg(&app_dir)
                .arg(&temporary)
                .env("ARCH", architecture),
        )?;
        verify_appimage(inputs, &temporary)?;
        fs::rename(&temporary, &destination).map_err(|source| Error::ArchiveIo {
            operation: "publish AppImage",
            path: destination.clone(),
            source,
        })?;
        finalize_artifact(
            destination,
            &inputs.bundle_root.join(&inputs.package_record),
            record,
        )
    })();
    let _ = fs::remove_dir_all(app_dir);
    if result.is_err() {
        let _ = fs::remove_file(temporary);
    }
    result
}

fn prepare_layout(inputs: &AppImageInputs, app_dir: &Path) -> Result<()> {
    let usr = app_dir.join("usr");
    copy_tree(&inputs.bundle_root, &usr)?;
    let desktop = encode_linux_desktop_entry(&inputs.metadata, &inputs.categories)?;
    let desktop_path = app_dir.join(desktop.relative_path);
    fs::write(&desktop_path, desktop.bytes).map_err(|source| Error::ArchiveIo {
        operation: "write AppImage desktop entry",
        path: desktop_path,
        source,
    })?;
    let icon = inputs.icons.get(256).ok_or(Error::MissingIconRaster(256))?;
    let icon_path = app_dir.join(format!("{}.png", inputs.metadata.executable_name));
    fs::write(&icon_path, encode_png(icon)?).map_err(|source| Error::ArchiveIo {
        operation: "write AppImage icon",
        path: icon_path,
        source,
    })?;
    let executable = inputs.executable.to_string_lossy();
    let app_run = format!(
        "#!/bin/sh\nset -eu\nHERE=$(CDPATH= cd -- \"$(dirname -- \"$0\")\" && pwd)\nexec \"$HERE/usr/{executable}\" \"$@\"\n"
    );
    write_executable(&app_dir.join("AppRun"), app_run.as_bytes())
}

fn verify_appimage(inputs: &AppImageInputs, appimage: &Path) -> Result<()> {
    let extraction_parent = unique_path(appimage, "appimage-extracted")?;
    let extracted = extraction_parent.join("squashfs-root");
    create_dir_all(&extraction_parent, "create AppImage extraction directory")?;
    let offset = squashfs_offset(appimage)?;
    run_tool(
        "unsquashfs",
        "extract AppImage without FUSE",
        appimage,
        Command::new(&inputs.unsquashfs)
            .args(["-f", "-d"])
            .arg(&extracted)
            .arg("-o")
            .arg(offset.to_string())
            .arg(appimage),
    )?;
    let result = crate::verify_bundle(extracted.join("usr"), &inputs.package_record);
    let _ = fs::remove_dir_all(extraction_parent);
    result.map(|_| ())
}

fn squashfs_offset(appimage: &Path) -> Result<usize> {
    let bytes = fs::read(appimage).map_err(|source| Error::ArchiveIo {
        operation: "read AppImage for SquashFS offset",
        path: appimage.to_path_buf(),
        source,
    })?;
    bytes
        .windows(4)
        .rposition(|window| window == b"hsqs")
        .ok_or_else(|| Error::DistributionTool {
            tool: "AppImage",
            operation: "locate embedded SquashFS filesystem",
            path: appimage.to_path_buf(),
            message: "SquashFS magic was not found".to_string(),
        })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{IconAlpha, IconRaster, IconSourceFormat, NativeVersion};

    #[test]
    fn app_run_uses_executable_relative_to_its_appdir() {
        let root =
            std::env::temp_dir().join(format!("effindom-appdir-test-{}", std::process::id()));
        let bundle = root.join("bundle");
        fs::create_dir_all(bundle.join("bin")).unwrap();
        fs::write(bundle.join("bin/sample"), "sample").unwrap();
        let inputs = AppImageInputs {
            bundle_root: bundle,
            package_record: PathBuf::from("effindom-package.json"),
            destination: root.join("sample.AppImage"),
            executable: PathBuf::from("bin/sample"),
            metadata: ApplicationMetadata::new(
                "sample",
                "1.0.0",
                NativeVersion::new(1, 0, 0, 0),
                "Sample",
                "dev.effindom.sample",
            )
            .unwrap(),
            categories: vec!["Utility".to_string()],
            icons: IconRasterSet {
                source_format: IconSourceFormat::Png,
                source_width: 256,
                source_height: 256,
                alpha: IconAlpha::Opaque,
                rasters: vec![IconRaster {
                    size: 256,
                    rgba8: vec![255; 256 * 256 * 4],
                }],
            },
            appimagetool: PathBuf::from("appimagetool"),
            unsquashfs: PathBuf::from("unsquashfs"),
        };
        let app_dir = root.join("AppDir");
        prepare_layout(&inputs, &app_dir).unwrap();
        let launcher = fs::read_to_string(app_dir.join("AppRun")).unwrap();
        assert!(launcher.contains("$HERE/usr/bin/sample"));
        assert!(app_dir.join("sample.desktop").is_file());
        assert!(app_dir.join("sample.png").is_file());
        fs::remove_dir_all(root).unwrap();
    }
}
