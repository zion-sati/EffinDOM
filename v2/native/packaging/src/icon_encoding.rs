use crate::{Error, IconRaster, IconRasterSet, Result};
use image::codecs::png::PngEncoder;
use image::{ExtendedColorType, ImageEncoder};
use std::path::PathBuf;

const WINDOWS_ICO_SIZES: &[u32] = &[16, 24, 32, 48, 64, 256];
const FAVICON_SIZES: &[u32] = &[16, 32, 48];
const LINUX_HICOLOR_SIZES: &[u32] = &[16, 24, 32, 48, 64, 128, 192, 256, 512];
const ICNS_CHUNKS: &[(u32, [u8; 4])] = &[
    (16, *b"icp4"),
    (32, *b"icp5"),
    (64, *b"icp6"),
    (128, *b"ic07"),
    (256, *b"ic08"),
    (512, *b"ic09"),
    (1024, *b"ic10"),
];

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct IconArtifact {
    pub relative_path: PathBuf,
    pub bytes: Vec<u8>,
}

pub fn encode_windows_ico(rasters: &IconRasterSet) -> Result<Vec<u8>> {
    encode_ico(rasters, WINDOWS_ICO_SIZES, |size| size == 256)
}

pub fn encode_browser_favicon(rasters: &IconRasterSet) -> Result<Vec<u8>> {
    encode_ico(rasters, FAVICON_SIZES, |_| false)
}

pub fn encode_macos_icns(rasters: &IconRasterSet) -> Result<Vec<u8>> {
    let mut chunks = Vec::with_capacity(ICNS_CHUNKS.len());
    let mut total_length = 8usize;
    for (size, chunk_type) in ICNS_CHUNKS {
        let png = encode_png(required_raster(rasters, *size)?)?;
        let chunk_length = 8usize
            .checked_add(png.len())
            .ok_or_else(|| encode_error("ICNS", "chunk length overflow"))?;
        total_length = total_length
            .checked_add(chunk_length)
            .ok_or_else(|| encode_error("ICNS", "container length overflow"))?;
        chunks.push((*chunk_type, chunk_length, png));
    }

    let mut output = Vec::with_capacity(total_length);
    output.extend_from_slice(b"icns");
    push_be_u32(&mut output, checked_u32(total_length, "ICNS container")?);
    for (chunk_type, chunk_length, png) in chunks {
        output.extend_from_slice(&chunk_type);
        push_be_u32(&mut output, checked_u32(chunk_length, "ICNS chunk")?);
        output.extend_from_slice(&png);
    }
    Ok(output)
}

pub fn encode_linux_hicolor(rasters: &IconRasterSet, icon_name: &str) -> Result<Vec<IconArtifact>> {
    validate_icon_name(icon_name)?;
    LINUX_HICOLOR_SIZES
        .iter()
        .map(|size| {
            let raster = required_raster(rasters, *size)?;
            Ok(IconArtifact {
                relative_path: PathBuf::from(format!("hicolor/{size}x{size}/apps/{icon_name}.png")),
                bytes: encode_png(raster)?,
            })
        })
        .collect()
}

fn encode_ico(
    rasters: &IconRasterSet,
    sizes: &[u32],
    use_png: impl Fn(u32) -> bool,
) -> Result<Vec<u8>> {
    let payloads = sizes
        .iter()
        .map(|size| {
            let raster = required_raster(rasters, *size)?;
            if use_png(*size) {
                encode_png(raster)
            } else {
                encode_dib(raster)
            }
        })
        .collect::<Result<Vec<_>>>()?;

    let directory_length = 6usize
        .checked_add(
            sizes
                .len()
                .checked_mul(16)
                .ok_or_else(|| encode_error("ICO", "directory length overflow"))?,
        )
        .ok_or_else(|| encode_error("ICO", "directory length overflow"))?;
    let total_length = payloads
        .iter()
        .try_fold(directory_length, |length, payload| {
            length
                .checked_add(payload.len())
                .ok_or_else(|| encode_error("ICO", "container length overflow"))
        })?;
    let mut output = Vec::with_capacity(total_length);
    push_le_u16(&mut output, 0);
    push_le_u16(&mut output, 1);
    push_le_u16(
        &mut output,
        u16::try_from(sizes.len()).map_err(|error| encode_error("ICO", error))?,
    );

    let mut offset = directory_length;
    for (size, payload) in sizes.iter().zip(&payloads) {
        output.push(if *size == 256 { 0 } else { *size as u8 });
        output.push(if *size == 256 { 0 } else { *size as u8 });
        output.push(0);
        output.push(0);
        push_le_u16(&mut output, 1);
        push_le_u16(&mut output, 32);
        push_le_u32(&mut output, checked_u32(payload.len(), "ICO payload")?);
        push_le_u32(&mut output, checked_u32(offset, "ICO payload offset")?);
        offset = offset
            .checked_add(payload.len())
            .ok_or_else(|| encode_error("ICO", "payload offset overflow"))?;
    }
    for payload in payloads {
        output.extend_from_slice(&payload);
    }
    Ok(output)
}

fn encode_dib(raster: &IconRaster) -> Result<Vec<u8>> {
    validate_raster(raster)?;
    let size = raster.size as usize;
    let xor_length = size
        .checked_mul(size)
        .and_then(|pixels| pixels.checked_mul(4))
        .ok_or_else(|| encode_error("DIB", "pixel length overflow"))?;
    let mask_stride = size
        .checked_add(31)
        .map(|bits| (bits / 32) * 4)
        .ok_or_else(|| encode_error("DIB", "mask stride overflow"))?;
    let mask_length = mask_stride
        .checked_mul(size)
        .ok_or_else(|| encode_error("DIB", "mask length overflow"))?;
    let capacity = 40usize
        .checked_add(xor_length)
        .and_then(|length| length.checked_add(mask_length))
        .ok_or_else(|| encode_error("DIB", "payload length overflow"))?;
    let mut output = Vec::with_capacity(capacity);

    push_le_u32(&mut output, 40);
    push_le_i32(&mut output, raster.size as i32);
    push_le_i32(&mut output, (raster.size * 2) as i32);
    push_le_u16(&mut output, 1);
    push_le_u16(&mut output, 32);
    push_le_u32(&mut output, 0);
    push_le_u32(&mut output, checked_u32(xor_length, "DIB pixels")?);
    push_le_i32(&mut output, 0);
    push_le_i32(&mut output, 0);
    push_le_u32(&mut output, 0);
    push_le_u32(&mut output, 0);

    for y in (0..size).rev() {
        for x in 0..size {
            let offset = (y * size + x) * 4;
            let rgba = &raster.rgba8[offset..offset + 4];
            output.extend_from_slice(&[rgba[2], rgba[1], rgba[0], rgba[3]]);
        }
    }

    let mut mask_row = vec![0u8; mask_stride];
    for y in (0..size).rev() {
        mask_row.fill(0);
        for x in 0..size {
            let alpha = raster.rgba8[(y * size + x) * 4 + 3];
            if alpha == 0 {
                mask_row[x / 8] |= 1 << (7 - (x % 8));
            }
        }
        output.extend_from_slice(&mask_row);
    }
    Ok(output)
}

pub(crate) fn encode_png(raster: &IconRaster) -> Result<Vec<u8>> {
    validate_raster(raster)?;
    let mut output = Vec::new();
    PngEncoder::new(&mut output)
        .write_image(
            &raster.rgba8,
            raster.size,
            raster.size,
            ExtendedColorType::Rgba8,
        )
        .map_err(|error| encode_error("PNG", error))?;
    Ok(output)
}

fn required_raster(rasters: &IconRasterSet, size: u32) -> Result<&IconRaster> {
    let raster = rasters.get(size).ok_or(Error::MissingIconRaster(size))?;
    validate_raster(raster)?;
    Ok(raster)
}

fn validate_raster(raster: &IconRaster) -> Result<()> {
    let expected = (raster.size as usize)
        .checked_mul(raster.size as usize)
        .and_then(|pixels| pixels.checked_mul(4))
        .ok_or_else(|| encode_error("icon raster", "byte length overflow"))?;
    if raster.rgba8.len() != expected {
        return Err(Error::InvalidIconRasterData {
            size: raster.size,
            expected,
            actual: raster.rgba8.len(),
        });
    }
    Ok(())
}

fn validate_icon_name(icon_name: &str) -> Result<()> {
    if icon_name.is_empty()
        || icon_name.starts_with('.')
        || !icon_name
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'.' | b'-' | b'_'))
    {
        return Err(Error::InvalidIconName(icon_name.to_owned()));
    }
    Ok(())
}

fn checked_u32(value: usize, context: &'static str) -> Result<u32> {
    u32::try_from(value).map_err(|error| encode_error(context, error))
}

fn encode_error(format: &'static str, message: impl std::fmt::Display) -> Error {
    Error::EncodeIcon {
        format,
        message: message.to_string(),
    }
}

fn push_le_u16(output: &mut Vec<u8>, value: u16) {
    output.extend_from_slice(&value.to_le_bytes());
}

fn push_le_u32(output: &mut Vec<u8>, value: u32) {
    output.extend_from_slice(&value.to_le_bytes());
}

fn push_le_i32(output: &mut Vec<u8>, value: i32) {
    output.extend_from_slice(&value.to_le_bytes());
}

fn push_be_u32(output: &mut Vec<u8>, value: u32) {
    output.extend_from_slice(&value.to_be_bytes());
}
