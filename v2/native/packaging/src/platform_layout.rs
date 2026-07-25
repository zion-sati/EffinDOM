use serde::Serialize;
use std::path::PathBuf;

#[derive(Clone, Copy, Debug, Serialize, PartialEq, Eq)]
#[serde(rename_all = "kebab-case")]
pub enum NativePlatform {
    MacOs,
    Windows,
    Linux,
}

#[derive(Clone, Debug, Serialize, PartialEq, Eq)]
#[serde(tag = "kind", content = "path", rename_all = "kebab-case")]
pub enum NativeLibrarySearch {
    LoaderRelative(String),
    ExecutableDirectory,
}

#[derive(Clone, Debug, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct NativePackageLayout {
    pub root: PathBuf,
    pub executable: PathBuf,
    pub runtime_libraries: PathBuf,
    pub runtime_resources: PathBuf,
    pub application_resources: PathBuf,
    pub package_record: PathBuf,
    pub library_search: NativeLibrarySearch,
}

impl NativePackageLayout {
    pub fn for_application(name: &str, platform: NativePlatform) -> Self {
        match platform {
            NativePlatform::MacOs => {
                let root = PathBuf::from(format!("{name}.app"));
                Self {
                    executable: root.join("Contents/MacOS").join(name),
                    runtime_libraries: root.join("Contents/Frameworks"),
                    runtime_resources: root.join("Contents/Resources/effindom"),
                    application_resources: root.join("Contents/Resources/app"),
                    package_record: root.join("Contents/Resources/effindom-package.json"),
                    library_search: NativeLibrarySearch::LoaderRelative(
                        "@loader_path/../Frameworks".to_owned(),
                    ),
                    root,
                }
            }
            NativePlatform::Windows => {
                let root = PathBuf::from(name);
                Self {
                    executable: root.join(format!("{name}.exe")),
                    runtime_libraries: root.clone(),
                    runtime_resources: root.join("assets/effindom"),
                    application_resources: root.join("assets/app"),
                    package_record: root.join("effindom-package.json"),
                    library_search: NativeLibrarySearch::ExecutableDirectory,
                    root,
                }
            }
            NativePlatform::Linux => {
                let root = PathBuf::from(name);
                Self {
                    executable: root.join("bin").join(name),
                    runtime_libraries: root.join("lib"),
                    runtime_resources: root.join("share/effindom"),
                    application_resources: root.join("share/app"),
                    package_record: root.join("share/effindom-package.json"),
                    library_search: NativeLibrarySearch::LoaderRelative(
                        "$ORIGIN/../lib".to_owned(),
                    ),
                    root,
                }
            }
        }
    }
}
