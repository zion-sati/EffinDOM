use effindom_native_packaging::{
    create_native_runtime_artifact, decode_native_runtime_release_manifest,
    encode_native_runtime_release_manifest, extract_native_runtime_artifact, NativeRuntimeArtifact,
    NativeRuntimeArtifactRequest, OverwritePolicy,
};
use std::fs;

fn main() {
    if let Err(error) = run() {
        eprintln!("{error}");
        std::process::exit(1);
    }
}

fn run() -> Result<(), Box<dyn std::error::Error>> {
    let mut arguments = std::env::args_os().skip(1);
    let command = arguments.next().and_then(|value| value.into_string().ok());
    match command.as_deref() {
        Some("create-runtime-artifact") => {
            let request_path = required_path(&mut arguments)?;
            ensure_finished(arguments)?;
            let request: NativeRuntimeArtifactRequest =
                serde_json::from_slice(&fs::read(&request_path)?)?;
            let output = create_native_runtime_artifact(&request, OverwritePolicy::Reject)?;
            println!("{}", serde_json::to_string_pretty(&output)?);
        }
        Some("create-release-manifest") => {
            let request_path = required_path(&mut arguments)?;
            let output_path = required_path(&mut arguments)?;
            ensure_finished(arguments)?;
            let manifest = decode_native_runtime_release_manifest(&fs::read(request_path)?)?;
            fs::write(
                output_path,
                encode_native_runtime_release_manifest(&manifest)?,
            )?;
        }
        Some("verify-runtime-artifact") => {
            let artifact_root = required_path(&mut arguments)?;
            let descriptor_path = required_path(&mut arguments)?;
            let source_commit = arguments
                .next()
                .and_then(|value| value.into_string().ok())
                .ok_or_else(|| usage())?;
            let destination = required_path(&mut arguments)?;
            ensure_finished(arguments)?;
            let descriptor: NativeRuntimeArtifact =
                serde_json::from_slice(&fs::read(descriptor_path)?)?;
            extract_native_runtime_artifact(
                artifact_root,
                destination,
                &source_commit,
                &descriptor,
                OverwritePolicy::Reject,
            )?;
        }
        _ => return Err(usage().into()),
    }
    Ok(())
}

fn required_path(
    arguments: &mut impl Iterator<Item = std::ffi::OsString>,
) -> Result<std::path::PathBuf, Box<dyn std::error::Error>> {
    arguments
        .next()
        .map(std::path::PathBuf::from)
        .ok_or_else(|| usage().into())
}

fn ensure_finished(
    mut arguments: impl Iterator<Item = std::ffi::OsString>,
) -> Result<(), Box<dyn std::error::Error>> {
    if arguments.next().is_some() {
        Err(usage().into())
    } else {
        Ok(())
    }
}

fn usage() -> &'static str {
    "usage:\n  effindom-native-packager create-runtime-artifact <request.json>\n  effindom-native-packager verify-runtime-artifact <artifact-root> <descriptor.json> <source-commit> <destination>\n  effindom-native-packager create-release-manifest <request.json> <output.json>"
}
