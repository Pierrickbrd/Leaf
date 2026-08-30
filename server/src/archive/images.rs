//! Recognising an image, and measuring it without decoding it.

use std::io::Cursor;

/// Recognition by leading bytes.
///
/// A handful of signatures is enough to settle page / not-a-page, which is the only
/// question asked here — a general-purpose content detector would answer far more than
/// that, and everything here is already known to be an image inside a zip.
pub fn media_type(head: &[u8]) -> Option<&'static str> {
    const SIGNATURES: &[(&[u8], &str)] = &[
        (&[0xFF, 0xD8, 0xFF], "image/jpeg"),
        (
            &[0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A],
            "image/png",
        ),
        (b"GIF87a", "image/gif"),
        (b"GIF89a", "image/gif"),
        (b"BM", "image/bmp"),
    ];
    for (signature, kind) in SIGNATURES {
        if head.starts_with(signature) {
            return Some(kind);
        }
    }
    // RIFF and ISO-BMFF put their marker at an offset, so it has to be looked for there.
    if head.len() >= 12 {
        if head.starts_with(b"RIFF") && &head[8..12] == b"WEBP" {
            return Some("image/webp");
        }
        if &head[4..8] == b"ftyp" {
            return match &head[8..12] {
                b"avif" | b"avis" => Some("image/avif"),
                b"heic" | b"heix" | b"hevc" | b"mif1" => Some("image/heic"),
                _ => None,
            };
        }
    }
    None
}

/// Dimensions without decoding the image: the header is read and that is all.
pub fn dimension(bytes: &[u8]) -> Option<(u32, u32)> {
    image::ImageReader::new(Cursor::new(bytes))
        .with_guessed_format()
        .ok()?
        .into_dimensions()
        .ok()
}
