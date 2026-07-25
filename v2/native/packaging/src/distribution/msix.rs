use super::common::{
    copy_tree, create_dir_all, finalize_artifact, prepare_output, run_tool, unique_path,
    verified_record, xml_escape, DistributionArtifact,
};
use crate::icon_encoding::encode_png;
use crate::{
    ApplicationMetadata, Error, IconRasterSet, OverwritePolicy, PackageArchitecture,
    PackageOperatingSystem, Result,
};
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct MsixInputs {
    pub bundle_root: PathBuf,
    pub package_record: PathBuf,
    pub destination: PathBuf,
    pub executable: PathBuf,
    pub metadata: ApplicationMetadata,
    pub publisher: String,
    pub publisher_display_name: String,
    pub icons: IconRasterSet,
    pub makeappx: PathBuf,
}

pub fn create_msix(
    inputs: &MsixInputs,
    overwrite: OverwritePolicy,
) -> Result<DistributionArtifact> {
    let record = verified_record(
        &inputs.bundle_root,
        &inputs.package_record,
        PackageOperatingSystem::Windows,
        "MSIX",
    )?;
    let destination = prepare_output(&inputs.destination, overwrite)?;
    let layout = unique_path(&destination, "msix-layout")?;
    create_dir_all(&layout, "create MSIX layout")?;
    let temporary_msix = unique_path(&destination, "msix-output")?;
    let result = (|| {
        prepare_layout(inputs, &record, &layout)?;
        run_tool(
            "makeappx",
            "pack MSIX",
            &destination,
            Command::new(&inputs.makeappx)
                .args(["pack", "/o", "/d"])
                .arg(&layout)
                .arg("/p")
                .arg(&temporary_msix),
        )?;
        verify_msix(inputs, &temporary_msix)?;
        fs::rename(&temporary_msix, &destination).map_err(|source| Error::ArchiveIo {
            operation: "publish MSIX",
            path: destination.clone(),
            source,
        })?;
        finalize_artifact(
            destination,
            &inputs.bundle_root.join(&inputs.package_record),
            record,
        )
    })();
    let _ = fs::remove_dir_all(layout);
    if result.is_err() {
        let _ = fs::remove_file(temporary_msix);
    }
    result
}

fn prepare_layout(inputs: &MsixInputs, record: &crate::PackageRecord, layout: &Path) -> Result<()> {
    copy_tree(&inputs.bundle_root, layout)?;
    let assets = layout.join("Assets");
    create_dir_all(&assets, "create MSIX assets")?;
    for (size, name) in [
        (44, "Square44x44Logo.png"),
        (50, "StoreLogo.png"),
        (150, "Square150x150Logo.png"),
    ] {
        let raster = inputs
            .icons
            .get(size)
            .ok_or(Error::MissingIconRaster(size))?;
        let path = assets.join(name);
        fs::write(&path, encode_png(raster)?).map_err(|source| Error::ArchiveIo {
            operation: "write MSIX icon",
            path,
            source,
        })?;
    }
    let manifest = encode_manifest(inputs, record)?;
    let path = layout.join("AppxManifest.xml");
    fs::write(&path, manifest).map_err(|source| Error::ArchiveIo {
        operation: "write MSIX manifest",
        path,
        source,
    })?;
    Ok(())
}

fn encode_manifest(inputs: &MsixInputs, record: &crate::PackageRecord) -> Result<String> {
    let architecture = match record.metadata.architecture {
        PackageArchitecture::X64 => "x64",
        PackageArchitecture::Arm64 => "arm64",
        PackageArchitecture::Universal => {
            return Err(Error::DistributionTargetMismatch {
                format: "MSIX",
                actual: "Windows/universal".to_string(),
            });
        }
    };
    let version = inputs.metadata.native_version;
    let executable = inputs.executable.to_string_lossy().replace('/', "\\");
    Ok(format!(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<Package xmlns=\"http://schemas.microsoft.com/appx/manifest/foundation/windows10\" xmlns:uap=\"http://schemas.microsoft.com/appx/manifest/uap/windows10\" xmlns:rescap=\"http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities\" IgnorableNamespaces=\"uap rescap\">\n  <Identity Name=\"{}\" Publisher=\"{}\" Version=\"{}.{}.{}.{}\" ProcessorArchitecture=\"{}\" />\n  <Properties><DisplayName>{}</DisplayName><PublisherDisplayName>{}</PublisherDisplayName><Logo>Assets\\StoreLogo.png</Logo></Properties>\n  <Resources><Resource Language=\"en-us\" /></Resources>\n  <Dependencies><TargetDeviceFamily Name=\"Windows.Desktop\" MinVersion=\"10.0.17763.0\" MaxVersionTested=\"10.0.26100.0\" /></Dependencies>\n  <Applications><Application Id=\"App\" Executable=\"{}\" EntryPoint=\"Windows.FullTrustApplication\"><uap:VisualElements DisplayName=\"{}\" Description=\"{}\" BackgroundColor=\"transparent\" Square44x44Logo=\"Assets\\Square44x44Logo.png\" Square150x150Logo=\"Assets\\Square150x150Logo.png\" /></Application></Applications>\n  <Capabilities><rescap:Capability Name=\"runFullTrust\" /></Capabilities>\n</Package>\n",
        xml_escape(&inputs.metadata.identifier),
        xml_escape(&inputs.publisher),
        version.major,
        version.minor,
        version.patch,
        version.build,
        architecture,
        xml_escape(&inputs.metadata.caption),
        xml_escape(&inputs.publisher_display_name),
        xml_escape(&executable),
        xml_escape(&inputs.metadata.caption),
        xml_escape(&inputs.metadata.caption),
    ))
}

fn verify_msix(inputs: &MsixInputs, msix: &Path) -> Result<()> {
    let extracted = unique_path(msix, "msix-extracted")?;
    run_tool(
        "makeappx",
        "unpack MSIX for verification",
        msix,
        Command::new(&inputs.makeappx)
            .args(["unpack", "/o", "/p"])
            .arg(msix)
            .arg("/d")
            .arg(&extracted),
    )?;
    let result = crate::verify_bundle(&extracted, &inputs.package_record);
    let _ = fs::remove_dir_all(extracted);
    result.map(|_| ())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{NativeVersion, PackageBuildMode, PackageMetadata};

    #[test]
    fn manifest_contains_full_trust_identity_architecture_and_visual_metadata() {
        let inputs = MsixInputs {
            bundle_root: PathBuf::from("bundle"),
            package_record: PathBuf::from("effindom-package.json"),
            destination: PathBuf::from("sample.msix"),
            executable: PathBuf::from("sample.exe"),
            metadata: ApplicationMetadata::new(
                "sample",
                "1.2.3",
                NativeVersion::new(1, 2, 3, 4),
                "Sample & App",
                "dev.effindom.sample",
            )
            .unwrap(),
            publisher: "CN=EffinDOM Test".to_string(),
            publisher_display_name: "EffinDOM".to_string(),
            icons: empty_icons(),
            makeappx: PathBuf::from("makeappx.exe"),
        };
        let record = crate::PackageRecord {
            schema_version: 2,
            metadata: PackageMetadata {
                application_identifier: "dev.effindom.sample".to_string(),
                application_version: "1.2.3".to_string(),
                operating_system: PackageOperatingSystem::Windows,
                architecture: PackageArchitecture::Arm64,
                target_triple: "aarch64-pc-windows-msvc".to_string(),
                build_mode: PackageBuildMode::Release,
                core_abi: 2,
                ui_abi: 1,
            },
            files: vec![],
        };
        let value = encode_manifest(&inputs, &record).unwrap();
        assert!(value.contains("ProcessorArchitecture=\"arm64\""));
        assert!(value.contains("EntryPoint=\"Windows.FullTrustApplication\""));
        assert!(value.contains("DisplayName>Sample &amp; App</DisplayName>"));
        assert!(value.contains("runFullTrust"));
    }

    fn empty_icons() -> IconRasterSet {
        IconRasterSet {
            source_format: crate::IconSourceFormat::Png,
            source_width: 256,
            source_height: 256,
            alpha: crate::IconAlpha::Opaque,
            rasters: vec![],
        }
    }
}
