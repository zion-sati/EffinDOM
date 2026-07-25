use crate::icon_encoding::encode_png;
use crate::{Error, IconArtifact, IconRasterSet, Result};
use std::path::PathBuf;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct NativeVersion {
    pub major: u16,
    pub minor: u16,
    pub patch: u16,
    pub build: u16,
}

impl NativeVersion {
    pub const fn new(major: u16, minor: u16, patch: u16, build: u16) -> Self {
        Self {
            major,
            minor,
            patch,
            build,
        }
    }

    fn dotted(self) -> String {
        format!("{}.{}.{}", self.major, self.minor, self.patch)
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ApplicationMetadata {
    pub executable_name: String,
    pub display_version: String,
    pub native_version: NativeVersion,
    pub caption: String,
    pub identifier: String,
}

impl ApplicationMetadata {
    pub fn new(
        executable_name: impl Into<String>,
        display_version: impl Into<String>,
        native_version: NativeVersion,
        caption: impl Into<String>,
        identifier: impl Into<String>,
    ) -> Result<Self> {
        let metadata = Self {
            executable_name: executable_name.into(),
            display_version: display_version.into(),
            native_version,
            caption: caption.into(),
            identifier: identifier.into(),
        };
        validate_metadata(&metadata)?;
        Ok(metadata)
    }
}

pub fn encode_application_icon_png(rasters: &IconRasterSet) -> Result<IconArtifact> {
    let raster = rasters.get(256).ok_or(Error::MissingIconRaster(256))?;
    Ok(IconArtifact {
        relative_path: PathBuf::from("app/application-icon.png"),
        bytes: encode_png(raster)?,
    })
}

pub fn encode_macos_info_plist(
    metadata: &ApplicationMetadata,
    minimum_version: Option<&str>,
) -> Result<IconArtifact> {
    validate_metadata(metadata)?;
    if minimum_version.is_some_and(|version| version.trim().is_empty()) {
        return Err(invalid_metadata(
            "macos.minimum-version",
            "must not be empty",
        ));
    }
    let mut entries = vec![
        plist_entry("CFBundleDevelopmentRegion", "en"),
        plist_entry("CFBundleDisplayName", &metadata.caption),
        plist_entry("CFBundleExecutable", &metadata.executable_name),
        plist_entry("CFBundleIconFile", "application.icns"),
        plist_entry("CFBundleIdentifier", &metadata.identifier),
        plist_entry("CFBundleInfoDictionaryVersion", "6.0"),
        plist_entry("CFBundleName", &metadata.caption),
        plist_entry("CFBundlePackageType", "APPL"),
        plist_entry(
            "CFBundleShortVersionString",
            &metadata.native_version.dotted(),
        ),
        plist_entry("CFBundleVersion", &metadata.native_version.dotted()),
    ];
    if let Some(version) = minimum_version {
        entries.push(plist_entry("LSMinimumSystemVersion", version));
    }
    let xml = format!(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n<plist version=\"1.0\">\n<dict>\n{}</dict>\n</plist>\n",
        entries.concat()
    );
    Ok(IconArtifact {
        relative_path: PathBuf::from("Info.plist"),
        bytes: xml.into_bytes(),
    })
}

pub fn encode_windows_resource_script(
    metadata: &ApplicationMetadata,
    publisher: Option<&str>,
) -> Result<IconArtifact> {
    validate_metadata(metadata)?;
    let version = metadata.native_version;
    let mut strings = vec![
        ("FileDescription", metadata.caption.as_str()),
        ("FileVersion", metadata.display_version.as_str()),
        ("InternalName", metadata.executable_name.as_str()),
        ("OriginalFilename", metadata.executable_name.as_str()),
        ("ProductName", metadata.caption.as_str()),
        ("ProductVersion", metadata.display_version.as_str()),
    ];
    if let Some(publisher) = publisher {
        if publisher.trim().is_empty() {
            return Err(invalid_metadata("windows.publisher", "must not be empty"));
        }
        strings.push(("CompanyName", publisher));
    }
    let values = strings
        .into_iter()
        .map(|(key, value)| format!("      VALUE \"{key}\", \"{}\\0\"\n", rc_escape(value)))
        .collect::<String>();
    let script = format!(
        "#pragma code_page(65001)\n1 ICON \"application.ico\"\n1 VERSIONINFO\n FILEVERSION {},{},{},{}\n PRODUCTVERSION {},{},{},{}\n FILEFLAGSMASK 0x3fL\n FILEFLAGS 0x0L\n FILEOS 0x40004L\n FILETYPE 0x1L\n FILESUBTYPE 0x0L\nBEGIN\n  BLOCK \"StringFileInfo\"\n  BEGIN\n    BLOCK \"040904B0\"\n    BEGIN\n{}    END\n  END\n  BLOCK \"VarFileInfo\"\n  BEGIN\n    VALUE \"Translation\", 0x0409, 1200\n  END\nEND\n",
        version.major, version.minor, version.patch, version.build,
        version.major, version.minor, version.patch, version.build, values
    );
    Ok(IconArtifact {
        relative_path: PathBuf::from("application.rc"),
        bytes: script.into_bytes(),
    })
}

pub fn encode_linux_desktop_entry(
    metadata: &ApplicationMetadata,
    categories: &[String],
) -> Result<IconArtifact> {
    validate_metadata(metadata)?;
    if categories.iter().any(|category| {
        category.is_empty()
            || !category
                .bytes()
                .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'-' | b'_'))
    }) {
        return Err(invalid_metadata(
            "linux.categories",
            "entries must use ASCII letters, digits, hyphens, or underscores",
        ));
    }
    let categories_line = if categories.is_empty() {
        String::new()
    } else {
        format!("Categories={};\n", categories.join(";"))
    };
    let desktop = format!(
        "[Desktop Entry]\nType=Application\nVersion=1.0\nName={}\nComment={}\nExec={}\nIcon={}\nTerminal=false\n{}",
        desktop_escape(&metadata.caption), desktop_escape(&metadata.caption),
        metadata.executable_name, metadata.executable_name, categories_line
    );
    Ok(IconArtifact {
        relative_path: PathBuf::from(format!("{}.desktop", metadata.executable_name)),
        bytes: desktop.into_bytes(),
    })
}

fn validate_metadata(metadata: &ApplicationMetadata) -> Result<()> {
    if metadata.executable_name.is_empty()
        || !metadata
            .executable_name
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'-' | b'_'))
    {
        return Err(invalid_metadata(
            "executable-name",
            "must use ASCII letters, digits, hyphens, or underscores",
        ));
    }
    if metadata.display_version.trim().is_empty() {
        return Err(invalid_metadata("display-version", "must not be empty"));
    }
    if metadata.caption.trim().is_empty() {
        return Err(invalid_metadata("caption", "must not be empty"));
    }
    let parts = metadata.identifier.split('.').collect::<Vec<_>>();
    if parts.len() < 2
        || parts.iter().any(|part| {
            part.is_empty()
                || !part
                    .bytes()
                    .all(|byte| byte.is_ascii_alphanumeric() || byte == b'-')
        })
    {
        return Err(invalid_metadata(
            "identifier",
            "must contain at least two dot-separated ASCII components",
        ));
    }
    Ok(())
}

fn plist_entry(key: &str, value: &str) -> String {
    format!(
        "  <key>{}</key>\n  <string>{}</string>\n",
        xml_escape(key),
        xml_escape(value)
    )
}

fn xml_escape(value: &str) -> String {
    value
        .replace('&', "&amp;")
        .replace('<', "&lt;")
        .replace('>', "&gt;")
        .replace('"', "&quot;")
        .replace('\'', "&apos;")
}

fn rc_escape(value: &str) -> String {
    value.replace('\\', "\\\\").replace('"', "\\\"")
}

fn desktop_escape(value: &str) -> String {
    value
        .replace('\\', "\\\\")
        .replace('\n', "\\n")
        .replace('\r', "\\r")
        .replace('\t', "\\t")
}

fn invalid_metadata(field: &'static str, message: impl Into<String>) -> Error {
    Error::InvalidApplicationMetadata {
        field,
        message: message.into(),
    }
}
