use effindom_native_packaging::{
    encode_browser_favicon, encode_linux_hicolor, encode_macos_icns, encode_windows_ico, Error,
    IconAlpha, IconRaster, IconRasterSet, IconSourceFormat, CANONICAL_ICON_SIZES,
};
use image::GenericImageView;

fn raster_set() -> IconRasterSet {
    IconRasterSet {
        source_format: IconSourceFormat::Svg,
        source_width: 512,
        source_height: 512,
        alpha: IconAlpha::HasTransparency,
        rasters: CANONICAL_ICON_SIZES
            .iter()
            .map(|size| {
                let mut rgba8 = Vec::with_capacity((size * size * 4) as usize);
                for y in 0..*size {
                    for x in 0..*size {
                        rgba8.extend_from_slice(&[
                            x as u8,
                            y as u8,
                            size.wrapping_add(x + y) as u8,
                            if x == 0 && y == 0 { 0 } else { 255 },
                        ]);
                    }
                }
                IconRaster { size: *size, rgba8 }
            })
            .collect(),
    }
}

fn le_u16(bytes: &[u8], offset: usize) -> u16 {
    u16::from_le_bytes(bytes[offset..offset + 2].try_into().expect("u16 bytes"))
}

fn le_u32(bytes: &[u8], offset: usize) -> u32 {
    u32::from_le_bytes(bytes[offset..offset + 4].try_into().expect("u32 bytes"))
}

fn be_u32(bytes: &[u8], offset: usize) -> u32 {
    u32::from_be_bytes(bytes[offset..offset + 4].try_into().expect("u32 bytes"))
}

#[test]
fn windows_ico_has_dib_small_entries_and_png_256_entry() {
    let ico = encode_windows_ico(&raster_set()).expect("encode Windows ICO");
    assert_eq!(&ico[0..6], &[0, 0, 1, 0, 6, 0]);

    let expected_sizes = [16u32, 24, 32, 48, 64, 256];
    let mut expected_offset = 6 + expected_sizes.len() * 16;
    for (index, expected_size) in expected_sizes.iter().enumerate() {
        let entry = 6 + index * 16;
        let encoded_size = if *expected_size == 256 {
            0
        } else {
            *expected_size as u8
        };
        assert_eq!(ico[entry], encoded_size);
        assert_eq!(ico[entry + 1], encoded_size);
        assert_eq!(le_u16(&ico, entry + 4), 1);
        assert_eq!(le_u16(&ico, entry + 6), 32);
        let payload_length = le_u32(&ico, entry + 8) as usize;
        let payload_offset = le_u32(&ico, entry + 12) as usize;
        assert_eq!(payload_offset, expected_offset);
        let payload = &ico[payload_offset..payload_offset + payload_length];
        if *expected_size == 256 {
            assert_eq!(&payload[..8], b"\x89PNG\r\n\x1a\n");
            assert_eq!(
                image::load_from_memory(payload)
                    .expect("decode embedded PNG")
                    .dimensions(),
                (256, 256)
            );
        } else {
            assert_eq!(le_u32(payload, 0), 40);
            assert_eq!(le_u32(payload, 4), *expected_size);
            assert_eq!(le_u32(payload, 8), expected_size * 2);
            assert_eq!(le_u16(payload, 12), 1);
            assert_eq!(le_u16(payload, 14), 32);
        }
        expected_offset += payload_length;
    }
    assert_eq!(expected_offset, ico.len());
}

#[test]
fn dib_is_bottom_up_bgra_and_carries_a_transparency_mask() {
    let ico = encode_browser_favicon(&raster_set()).expect("encode favicon");
    let first_entry = 6;
    let payload_length = le_u32(&ico, first_entry + 8) as usize;
    let payload_offset = le_u32(&ico, first_entry + 12) as usize;
    let dib = &ico[payload_offset..payload_offset + payload_length];
    let size = 16usize;

    assert_eq!(&dib[40..44], &[31, 15, 0, 255]);
    let top_left = 40 + ((size - 1) * size * 4);
    assert_eq!(&dib[top_left..top_left + 4], &[16, 0, 0, 0]);
    let mask_start = 40 + size * size * 4;
    let mask_stride = 4;
    let top_mask_row = mask_start + (size - 1) * mask_stride;
    assert_eq!(dib[top_mask_row] & 0b1000_0000, 0b1000_0000);
}

#[test]
fn macos_icns_contains_standard_png_chunks_with_valid_lengths() {
    let icns = encode_macos_icns(&raster_set()).expect("encode macOS ICNS");
    assert_eq!(&icns[..4], b"icns");
    assert_eq!(be_u32(&icns, 4) as usize, icns.len());
    let expected = [
        (*b"icp4", 16u32),
        (*b"icp5", 32),
        (*b"icp6", 64),
        (*b"ic07", 128),
        (*b"ic08", 256),
        (*b"ic09", 512),
        (*b"ic10", 1024),
    ];
    let mut offset = 8usize;
    for (chunk_type, size) in expected {
        assert_eq!(icns[offset..offset + 4], chunk_type);
        let chunk_length = be_u32(&icns, offset + 4) as usize;
        let png = &icns[offset + 8..offset + chunk_length];
        assert_eq!(&png[..8], b"\x89PNG\r\n\x1a\n");
        assert_eq!(
            image::load_from_memory(png)
                .expect("decode ICNS PNG")
                .dimensions(),
            (size, size)
        );
        offset += chunk_length;
    }
    assert_eq!(offset, icns.len());
}

#[test]
fn linux_hicolor_artifacts_have_safe_paths_and_decodable_pngs() {
    let artifacts =
        encode_linux_hicolor(&raster_set(), "effindom-demo").expect("encode hicolor icons");
    let expected_sizes = [16u32, 24, 32, 48, 64, 128, 192, 256, 512];
    assert_eq!(artifacts.len(), expected_sizes.len());
    for (artifact, size) in artifacts.iter().zip(expected_sizes) {
        assert_eq!(
            artifact.relative_path.to_string_lossy(),
            format!("hicolor/{size}x{size}/apps/effindom-demo.png")
        );
        assert_eq!(
            image::load_from_memory(&artifact.bytes)
                .expect("decode hicolor PNG")
                .dimensions(),
            (size, size)
        );
    }
}

#[test]
fn favicon_contains_the_conservative_dib_size_set() {
    let favicon = encode_browser_favicon(&raster_set()).expect("encode favicon");
    assert_eq!(le_u16(&favicon, 4), 3);
    assert_eq!([favicon[6], favicon[22], favicon[38]], [16, 32, 48]);
    for index in 0..3 {
        let entry = 6 + index * 16;
        let offset = le_u32(&favicon, entry + 12) as usize;
        assert_eq!(le_u32(&favicon, offset), 40);
    }
}

#[test]
fn encoders_reject_missing_malformed_and_unsafe_inputs() {
    let mut missing = raster_set();
    missing.rasters.retain(|raster| raster.size != 256);
    assert!(matches!(
        encode_windows_ico(&missing),
        Err(Error::MissingIconRaster(256))
    ));

    let mut malformed = raster_set();
    malformed.rasters[0].rgba8.pop();
    assert!(matches!(
        encode_browser_favicon(&malformed),
        Err(Error::InvalidIconRasterData { size: 16, .. })
    ));

    assert!(matches!(
        encode_linux_hicolor(&raster_set(), "../escape"),
        Err(Error::InvalidIconName(_))
    ));
}
