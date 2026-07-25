use super::common::{
    copy_tree, create_dir_all, finalize_artifact, prepare_output, run_tool, unique_path,
    verified_record, DistributionArtifact,
};
use crate::{OverwritePolicy, PackageOperatingSystem, Result};
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DmgInputs {
    pub app_bundle: PathBuf,
    pub package_record: PathBuf,
    pub destination: PathBuf,
    pub volume_name: String,
    pub hdiutil: PathBuf,
}

pub fn create_dmg(inputs: &DmgInputs, overwrite: OverwritePolicy) -> Result<DistributionArtifact> {
    let record = verified_record(
        &inputs.app_bundle,
        &inputs.package_record,
        PackageOperatingSystem::MacOs,
        "DMG",
    )?;
    let destination = prepare_output(&inputs.destination, overwrite)?;
    let staging = unique_path(&destination, "dmg-staging")?;
    create_dir_all(&staging, "create DMG staging directory")?;
    let app_name =
        inputs
            .app_bundle
            .file_name()
            .ok_or_else(|| crate::Error::InvalidBundlePath {
                path: inputs.app_bundle.clone(),
                message: "must name a macOS app bundle",
            })?;
    let staged_app = staging.join(app_name);
    let temporary_base = unique_path(&destination, "dmg-output")?;
    let temporary_dmg = PathBuf::from(format!("{}.dmg", temporary_base.display()));
    let result = (|| {
        copy_tree(&inputs.app_bundle, &staged_app)?;
        run_tool(
            "hdiutil",
            "create DMG",
            &destination,
            Command::new(&inputs.hdiutil)
                .args([
                    "create", "-quiet", "-fs", "HFS+", "-format", "UDZO", "-volname",
                ])
                .arg(&inputs.volume_name)
                .arg("-srcfolder")
                .arg(&staging)
                .arg("-ov")
                .arg(&temporary_dmg),
        )?;
        verify_dmg(inputs, &temporary_dmg, app_name)?;
        fs::rename(&temporary_dmg, &destination).map_err(|source| crate::Error::ArchiveIo {
            operation: "publish DMG",
            path: destination.clone(),
            source,
        })?;
        finalize_artifact(
            destination,
            &inputs.app_bundle.join(&inputs.package_record),
            record,
        )
    })();
    let _ = fs::remove_dir_all(staging);
    if result.is_err() {
        let _ = fs::remove_file(temporary_dmg);
    }
    result
}

fn verify_dmg(inputs: &DmgInputs, dmg: &Path, app_name: &std::ffi::OsStr) -> Result<()> {
    let mount = unique_path(dmg, "dmg-mount")?;
    create_dir_all(&mount, "create DMG verification mount")?;
    run_tool(
        "hdiutil",
        "attach DMG for verification",
        dmg,
        Command::new(&inputs.hdiutil)
            .args(["attach", "-quiet", "-readonly", "-nobrowse", "-mountpoint"])
            .arg(&mount)
            .arg(dmg),
    )?;
    let verification = crate::verify_bundle(mount.join(app_name), &inputs.package_record);
    let detach = run_tool(
        "hdiutil",
        "detach verified DMG",
        dmg,
        Command::new(&inputs.hdiutil)
            .args(["detach", "-quiet"])
            .arg(&mount),
    );
    let _ = fs::remove_dir_all(&mount);
    verification?;
    detach?;
    Ok(())
}
