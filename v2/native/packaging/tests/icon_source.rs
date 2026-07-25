use effindom_native_packaging::{
    load_icon_source, Error, IconAlpha, IconQualityWarning, IconSourceFormat, CANONICAL_ICON_SIZES,
    MINIMUM_PNG_ICON_SIZE, RECOMMENDED_PNG_ICON_SIZE,
};
use image::{ImageBuffer, Rgba};
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};

static NEXT_TEMP_ID: AtomicU64 = AtomicU64::new(1);

struct TestDirectory(PathBuf);

impl TestDirectory {
    fn new() -> Self {
        let id = NEXT_TEMP_ID.fetch_add(1, Ordering::Relaxed);
        let path =
            std::env::temp_dir().join(format!("cargo-fui-icon-source-{}-{id}", std::process::id()));
        fs::create_dir_all(&path).expect("create icon test directory");
        Self(path)
    }

    fn join(&self, path: impl AsRef<Path>) -> PathBuf {
        self.0.join(path)
    }
}

impl Drop for TestDirectory {
    fn drop(&mut self) {
        fs::remove_dir_all(&self.0).expect("remove icon test directory");
    }
}

fn write_png(path: &Path, width: u32, height: u32, pixel: Rgba<u8>) {
    let image = ImageBuffer::from_pixel(width, height, pixel);
    image.save(path).expect("write PNG fixture");
}

#[test]
fn loads_transparent_png_and_generates_canonical_rgba_rasters() {
    let directory = TestDirectory::new();
    let path = directory.join("icon.png");
    let mut image: ImageBuffer<Rgba<u8>, Vec<u8>> = ImageBuffer::from_pixel(
        RECOMMENDED_PNG_ICON_SIZE,
        RECOMMENDED_PNG_ICON_SIZE,
        Rgba([20, 40, 60, 255]),
    );
    image.put_pixel(0, 0, Rgba([0, 0, 0, 0]));
    image.save(&path).expect("write PNG fixture");

    let source = load_icon_source(&path).expect("load PNG icon");
    assert_eq!(source.format(), IconSourceFormat::Png);
    assert_eq!(source.alpha(), IconAlpha::HasTransparency);
    assert_eq!(source.width(), RECOMMENDED_PNG_ICON_SIZE);
    assert!(source.quality_warnings().is_empty());
    let rasters = source
        .canonical_rasters()
        .expect("generate canonical rasters");
    for required_msix_size in [44, 50, 150] {
        assert!(
            rasters.get(required_msix_size).is_some(),
            "canonical rasters must include the {required_msix_size}px MSIX asset"
        );
    }
    assert_eq!(
        rasters
            .rasters
            .iter()
            .map(|raster| raster.size)
            .collect::<Vec<_>>(),
        CANONICAL_ICON_SIZES
    );
    for raster in &rasters.rasters {
        assert_eq!(raster.rgba8.len(), (raster.size * raster.size * 4) as usize);
    }
    assert_eq!(
        rasters.get(1024).and_then(|raster| raster.pixel(0, 0)),
        Some([0, 0, 0, 0])
    );
}

#[test]
fn preserves_opaque_png_color_and_orders_requested_sizes() {
    let directory = TestDirectory::new();
    let path = directory.join("icon.PNG");
    write_png(
        &path,
        MINIMUM_PNG_ICON_SIZE,
        MINIMUM_PNG_ICON_SIZE,
        Rgba([12, 34, 56, 255]),
    );
    let source = load_icon_source(&path).expect("load opaque PNG icon");
    assert_eq!(source.alpha(), IconAlpha::Opaque);
    let rasters = source.rasterize(&[256, 16, 64]).expect("rasterize PNG");
    assert_eq!(
        rasters
            .rasters
            .iter()
            .map(|raster| raster.size)
            .collect::<Vec<_>>(),
        vec![16, 64, 256]
    );
    assert_eq!(
        rasters.get(64).and_then(|raster| raster.pixel(20, 20)),
        Some([12, 34, 56, 255])
    );
}

#[test]
fn accepts_minimum_png_with_structured_upscaling_warning() {
    let directory = TestDirectory::new();
    let path = directory.join("minimum.png");
    write_png(
        &path,
        MINIMUM_PNG_ICON_SIZE,
        MINIMUM_PNG_ICON_SIZE,
        Rgba([12, 34, 56, 255]),
    );

    let source = load_icon_source(&path).expect("load minimum-size PNG icon");
    assert_eq!(
        source.quality_warnings(),
        &[IconQualityWarning::PngWillBeUpscaled {
            width: MINIMUM_PNG_ICON_SIZE,
            height: MINIMUM_PNG_ICON_SIZE,
            recommended: RECOMMENDED_PNG_ICON_SIZE,
        }]
    );
    assert!(source.canonical_rasters().is_ok());
}

#[test]
fn loads_square_svg_and_rasterizes_with_transparency() {
    let directory = TestDirectory::new();
    let path = directory.join("icon.svg");
    fs::write(
        &path,
        r##"<svg xmlns="http://www.w3.org/2000/svg" width="512" height="512" viewBox="0 0 512 512"><circle cx="256" cy="256" r="200" fill="#ef4444"/></svg>"##,
    )
    .expect("write SVG fixture");

    let source = load_icon_source(&path).expect("load SVG icon");
    assert_eq!(source.format(), IconSourceFormat::Svg);
    assert_eq!(source.alpha(), IconAlpha::HasTransparency);
    let rasters = source.rasterize(&[32, 128]).expect("rasterize SVG");
    assert_eq!(
        rasters.get(32).and_then(|raster| raster.pixel(16, 16)),
        Some([239, 68, 68, 255])
    );
    assert_eq!(
        rasters.get(32).and_then(|raster| raster.pixel(0, 0)),
        Some([0, 0, 0, 0])
    );
}

#[test]
fn rejects_invalid_png_dimensions_size_and_visibility() {
    let directory = TestDirectory::new();
    let non_square = directory.join("non-square.png");
    write_png(&non_square, 1024, 512, Rgba([1, 2, 3, 255]));
    assert!(matches!(
        load_icon_source(&non_square),
        Err(Error::InvalidIconDimensions { .. })
    ));

    let too_small = directory.join("small.png");
    write_png(&too_small, 128, 128, Rgba([1, 2, 3, 255]));
    assert!(matches!(
        load_icon_source(&too_small),
        Err(Error::PngIconTooSmall { .. })
    ));

    let invisible = directory.join("invisible.png");
    write_png(&invisible, 1024, 1024, Rgba([0, 0, 0, 0]));
    assert!(matches!(
        load_icon_source(&invisible),
        Err(Error::InvisibleIcon(_))
    ));
}

#[test]
fn rejects_malformed_and_non_square_svg_sources() {
    let directory = TestDirectory::new();
    let malformed = directory.join("malformed.svg");
    fs::write(&malformed, "<svg><broken>").expect("write malformed SVG");
    assert!(matches!(
        load_icon_source(&malformed),
        Err(Error::DecodeIcon { .. })
    ));

    let non_square = directory.join("non-square.svg");
    fs::write(
        &non_square,
        r#"<svg xmlns="http://www.w3.org/2000/svg" width="400" height="200" viewBox="0 0 400 200"><rect width="400" height="200" fill="red"/></svg>"#,
    )
    .expect("write non-square SVG");
    assert!(matches!(
        load_icon_source(&non_square),
        Err(Error::InvalidIconDimensions { .. })
    ));
}

#[test]
fn rejects_unsupported_sources_and_invalid_requested_sizes() {
    let directory = TestDirectory::new();
    let unsupported = directory.join("icon.jpg");
    fs::write(&unsupported, b"not an icon").expect("write unsupported fixture");
    assert!(matches!(
        load_icon_source(&unsupported),
        Err(Error::InvalidIconPath(_))
    ));

    let png = directory.join("icon.png");
    write_png(&png, 1024, 1024, Rgba([1, 2, 3, 255]));
    let source = load_icon_source(&png).expect("load PNG fixture");
    assert!(matches!(
        source.rasterize(&[0]),
        Err(Error::InvalidIconRasterSize(0))
    ));
    assert!(matches!(
        source.rasterize(&[32, 32]),
        Err(Error::DuplicateIconRasterSize(32))
    ));
}
