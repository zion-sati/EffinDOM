use crate::{Error, Result};
use image::imageops::FilterType;
use image::{ImageFormat, RgbaImage};
use resvg::{tiny_skia, usvg};
use serde::Serialize;
use std::collections::BTreeSet;
use std::fs;
use std::path::{Path, PathBuf};

pub const MINIMUM_PNG_ICON_SIZE: u32 = 256;
pub const RECOMMENDED_PNG_ICON_SIZE: u32 = 1024;
pub const CANONICAL_ICON_SIZES: &[u32] = &[
    16, 24, 32, 44, 48, 50, 64, 128, 150, 180, 192, 256, 512, 1024,
];

#[derive(Clone, Copy, Debug, Serialize, PartialEq, Eq)]
#[serde(rename_all = "lowercase")]
pub enum IconSourceFormat {
    Svg,
    Png,
}

#[derive(Clone, Copy, Debug, Serialize, PartialEq, Eq)]
#[serde(rename_all = "kebab-case")]
pub enum IconAlpha {
    Opaque,
    HasTransparency,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum IconQualityWarning {
    PngWillBeUpscaled {
        width: u32,
        height: u32,
        recommended: u32,
    },
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct IconRaster {
    pub size: u32,
    pub rgba8: Vec<u8>,
}

impl IconRaster {
    pub fn pixel(&self, x: u32, y: u32) -> Option<[u8; 4]> {
        if x >= self.size || y >= self.size {
            return None;
        }
        let offset = ((y * self.size + x) * 4) as usize;
        Some(
            self.rgba8[offset..offset + 4]
                .try_into()
                .expect("RGBA pixel"),
        )
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct IconRasterSet {
    pub source_format: IconSourceFormat,
    pub source_width: u32,
    pub source_height: u32,
    pub alpha: IconAlpha,
    pub rasters: Vec<IconRaster>,
}

impl IconRasterSet {
    pub fn get(&self, size: u32) -> Option<&IconRaster> {
        self.rasters.iter().find(|raster| raster.size == size)
    }
}

#[derive(Clone)]
enum DecodedSource {
    Svg(Box<usvg::Tree>),
    Png(RgbaImage),
}

#[derive(Clone)]
pub struct IconSource {
    path: PathBuf,
    format: IconSourceFormat,
    width: u32,
    height: u32,
    alpha: IconAlpha,
    quality_warnings: Vec<IconQualityWarning>,
    decoded: DecodedSource,
}

impl IconSource {
    pub fn path(&self) -> &Path {
        &self.path
    }

    pub fn format(&self) -> IconSourceFormat {
        self.format
    }

    pub fn width(&self) -> u32 {
        self.width
    }

    pub fn height(&self) -> u32 {
        self.height
    }

    pub fn alpha(&self) -> IconAlpha {
        self.alpha
    }

    pub fn quality_warnings(&self) -> &[IconQualityWarning] {
        &self.quality_warnings
    }

    pub fn canonical_rasters(&self) -> Result<IconRasterSet> {
        self.rasterize(CANONICAL_ICON_SIZES)
    }

    pub fn rasterize(&self, requested_sizes: &[u32]) -> Result<IconRasterSet> {
        let sizes = validate_sizes(requested_sizes)?;
        let rasters = sizes
            .into_iter()
            .map(|size| self.render(size))
            .collect::<Result<Vec<_>>>()?;
        Ok(IconRasterSet {
            source_format: self.format,
            source_width: self.width,
            source_height: self.height,
            alpha: self.alpha,
            rasters,
        })
    }

    fn render(&self, size: u32) -> Result<IconRaster> {
        let rgba8 = match &self.decoded {
            DecodedSource::Png(image) => {
                if image.width() == size {
                    image.as_raw().clone()
                } else {
                    image::imageops::resize(image, size, size, FilterType::Lanczos3).into_raw()
                }
            }
            DecodedSource::Svg(tree) => render_svg(tree, size, &self.path)?,
        };
        Ok(IconRaster { size, rgba8 })
    }
}

pub fn load_icon_source(path: impl AsRef<Path>) -> Result<IconSource> {
    let path = path.as_ref();
    let extension = path
        .extension()
        .and_then(|extension| extension.to_str())
        .map(str::to_ascii_lowercase);
    match extension.as_deref() {
        Some("png") => load_png(path),
        Some("svg") => load_svg(path),
        _ => Err(Error::InvalidIconPath(path.to_path_buf())),
    }
}

fn load_png(path: &Path) -> Result<IconSource> {
    let bytes = read_source(path)?;
    let image = image::load_from_memory_with_format(&bytes, ImageFormat::Png)
        .map_err(|error| decode_error(path, error))?
        .into_rgba8();
    let width = image.width();
    let height = image.height();
    validate_square(path, width, height)?;
    if width < MINIMUM_PNG_ICON_SIZE {
        return Err(Error::PngIconTooSmall {
            path: path.to_path_buf(),
            width,
            height,
            minimum: MINIMUM_PNG_ICON_SIZE,
        });
    }
    let alpha = classify_alpha(path, image.as_raw())?;
    let quality_warnings = if width < RECOMMENDED_PNG_ICON_SIZE {
        vec![IconQualityWarning::PngWillBeUpscaled {
            width,
            height,
            recommended: RECOMMENDED_PNG_ICON_SIZE,
        }]
    } else {
        Vec::new()
    };
    Ok(IconSource {
        path: path.to_path_buf(),
        format: IconSourceFormat::Png,
        width,
        height,
        alpha,
        quality_warnings,
        decoded: DecodedSource::Png(image),
    })
}

fn load_svg(path: &Path) -> Result<IconSource> {
    let bytes = read_source(path)?;
    let options = usvg::Options::default();
    let tree = usvg::Tree::from_data(&bytes, &options).map_err(|error| Error::DecodeIcon {
        path: path.to_path_buf(),
        message: error.to_string(),
    })?;
    let size = tree.size();
    let width = size.width().round() as u32;
    let height = size.height().round() as u32;
    if size.width() <= 0.0 || size.height() <= 0.0 || (size.width() - size.height()).abs() > 0.01 {
        return Err(Error::InvalidIconDimensions {
            path: path.to_path_buf(),
            width,
            height,
        });
    }
    let probe = render_svg(&tree, 256, path)?;
    let alpha = classify_alpha(path, &probe)?;
    Ok(IconSource {
        path: path.to_path_buf(),
        format: IconSourceFormat::Svg,
        width,
        height,
        alpha,
        quality_warnings: Vec::new(),
        decoded: DecodedSource::Svg(Box::new(tree)),
    })
}

fn read_source(path: &Path) -> Result<Vec<u8>> {
    fs::read(path).map_err(|source| Error::ReadFile {
        path: path.to_path_buf(),
        source,
    })
}

fn render_svg(tree: &usvg::Tree, size: u32, path: &Path) -> Result<Vec<u8>> {
    let mut pixmap = tiny_skia::Pixmap::new(size, size).ok_or_else(|| Error::DecodeIcon {
        path: path.to_path_buf(),
        message: format!("could not allocate {size}x{size} raster"),
    })?;
    let source_size = tree.size();
    let transform = tiny_skia::Transform::from_scale(
        size as f32 / source_size.width(),
        size as f32 / source_size.height(),
    );
    resvg::render(tree, transform, &mut pixmap.as_mut());
    Ok(pixmap.take())
}

fn validate_square(path: &Path, width: u32, height: u32) -> Result<()> {
    if width == 0 || height == 0 || width != height {
        Err(Error::InvalidIconDimensions {
            path: path.to_path_buf(),
            width,
            height,
        })
    } else {
        Ok(())
    }
}

fn classify_alpha(path: &Path, rgba8: &[u8]) -> Result<IconAlpha> {
    let mut has_visible_pixel = false;
    let mut has_transparency = false;
    for alpha in rgba8.iter().skip(3).step_by(4) {
        has_visible_pixel |= *alpha != 0;
        has_transparency |= *alpha != 255;
    }
    if !has_visible_pixel {
        return Err(Error::InvisibleIcon(path.to_path_buf()));
    }
    Ok(if has_transparency {
        IconAlpha::HasTransparency
    } else {
        IconAlpha::Opaque
    })
}

fn validate_sizes(requested_sizes: &[u32]) -> Result<Vec<u32>> {
    let mut sizes = BTreeSet::new();
    for size in requested_sizes {
        if *size == 0 {
            return Err(Error::InvalidIconRasterSize(*size));
        }
        if !sizes.insert(*size) {
            return Err(Error::DuplicateIconRasterSize(*size));
        }
    }
    Ok(sizes.into_iter().collect())
}

fn decode_error(path: &Path, error: image::ImageError) -> Error {
    Error::DecodeIcon {
        path: path.to_path_buf(),
        message: error.to_string(),
    }
}
