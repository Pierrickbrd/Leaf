//! Where a cold page actually spends its time.
//!
//! A cold page is one nobody has asked for at this width yet, so there is nothing in the
//! cache and the whole path runs: pull it out of the archive, decode it, shrink it, encode
//! it again. A warm one is a file read.
//!
//! This exists because the ledger carried a **wrong** explanation of that cost for weeks —
//! "it is the decode" — written from a plausible mechanism rather than a measurement, in a
//! document whose stated rule is to measure. Twenty minutes of benchmark said otherwise:
//! the encode is sixty per cent of it.
//!
//! Ignored by default: it measures rather than asserts, and what it prints is only worth
//! reading beside a change to this path.
//!
//!     cargo test --release --test cold_path -- --ignored --nocapture

use std::io::{Read, Write};
use std::time::Instant;

fn page(width: u32, height: u32) -> Vec<u8> {
    let mut buffer = image::RgbImage::new(width, height);
    for (x, y, pixel) in buffer.enumerate_pixels_mut() {
        let panel = x % 640 < 8 || y % 480 < 8;
        let stroke = ((x as i32 - y as i32) % 97).abs() < 3 && (x / 40 + y / 60) % 3 == 0;
        let tone = (x % 6 < 2 && y % 6 < 2) && (x / 300 + y / 400) % 2 == 0;
        *pixel = if panel || stroke {
            image::Rgb([20, 20, 20])
        } else if tone {
            image::Rgb([130, 130, 130])
        } else {
            image::Rgb([248, 248, 248])
        };
    }
    let mut out = std::io::Cursor::new(Vec::new());
    image::DynamicImage::ImageRgb8(buffer)
        .write_to(&mut out, image::ImageFormat::Jpeg)
        .unwrap();
    out.into_inner()
}

#[test]
#[ignore]
fn what_the_cold_path_is_made_of() {
    let dir = tempfile::tempdir().unwrap();
    let cbz = dir.path().join("Tome 1.cbz");
    let jpeg = page(1936, 1400);
    {
        let mut zip = zip::ZipWriter::new(std::fs::File::create(&cbz).unwrap());
        for i in 0..40 {
            // Stored, as a real CBZ has it: a JPEG does not compress and every packer knows.
            zip.start_file::<_, ()>(
                format!("{i:03}.jpg"),
                zip::write::SimpleFileOptions::default()
                    .compression_method(zip::CompressionMethod::Stored),
            )
            .unwrap();
            zip.write_all(&jpeg).unwrap();
        }
        zip.finish().unwrap();
    }
    println!(
        "\nune archive de 40 pages, {} Mo, pages stockées\n",
        std::fs::metadata(&cbz).unwrap().len() / 1024 / 1024
    );

    let timed = |label: &str, mut f: Box<dyn FnMut()>| {
        f();
        let started = Instant::now();
        for _ in 0..10 {
            f();
        }
        println!(
            "  {label:44} {:>5.1} ms",
            started.elapsed().as_secs_f64() * 100.0
        );
    };

    let path = cbz.clone();
    timed(
        "ouvrir l'archive et sortir la page",
        Box::new(move || {
            let file = std::fs::File::open(&path).unwrap();
            let mut archive = zip::ZipArchive::new(file).unwrap();
            let mut entry = archive.by_name("020.jpg").unwrap();
            let mut bytes = Vec::with_capacity(entry.size() as usize);
            entry.read_to_end(&mut bytes).unwrap();
            std::hint::black_box(bytes);
        }),
    );

    let bytes = jpeg.clone();
    timed(
        "décoder",
        Box::new(move || {
            std::hint::black_box(image::load_from_memory(&bytes).unwrap().into_rgb8());
        }),
    );

    let decoded = image::load_from_memory(&jpeg).unwrap().into_rgb8();
    let raw = decoded.as_raw().clone();
    timed(
        "réduire à 1080 (SIMD)",
        Box::new(move || {
            use fast_image_resize::images::Image as Fir;
            use fast_image_resize::{PixelType, ResizeAlg, ResizeOptions, Resizer};
            let source = Fir::from_vec_u8(1936, 1400, raw.clone(), PixelType::U8x3).unwrap();
            let mut target = Fir::new(1080, 781, PixelType::U8x3);
            Resizer::new()
                .resize(
                    &source,
                    &mut target,
                    &ResizeOptions::new().resize_alg(ResizeAlg::Convolution(
                        fast_image_resize::FilterType::Lanczos3,
                    )),
                )
                .unwrap();
            std::hint::black_box(target.into_vec());
        }),
    );

    let small = image::imageops::resize(&decoded, 1080, 781, image::imageops::FilterType::Nearest);
    timed(
        "ré-encoder, image (ce qui était utilisé)",
        Box::new(move || {
            let mut out = Vec::new();
            image::codecs::jpeg::JpegEncoder::new_with_quality(&mut out, 85)
                .encode(small.as_raw(), 1080, 781, image::ExtendedColorType::Rgb8)
                .unwrap();
            std::hint::black_box(out);
        }),
    );

    let small = image::imageops::resize(&decoded, 1080, 781, image::imageops::FilterType::Nearest);
    let raw2 = small.as_raw().clone();
    timed(
        "ré-encoder, jpeg-encoder 4:4:4 (utilisé)",
        Box::new(move || {
            let mut out = Vec::new();
            let mut e = jpeg_encoder::Encoder::new(&mut out, 85);
            e.set_sampling_factor(jpeg_encoder::SamplingFactor::R_4_4_4);
            e.encode(&raw2, 1080, 781, jpeg_encoder::ColorType::Rgb)
                .unwrap();
            std::hint::black_box(out);
        }),
    );
    let small = image::imageops::resize(&decoded, 1080, 781, image::imageops::FilterType::Nearest);
    let raw = small.as_raw().clone();
    timed(
        "ré-encoder, jpeg-encoder 4:2:0 (son défaut)",
        Box::new(move || {
            let mut out = Vec::new();
            jpeg_encoder::Encoder::new(&mut out, 85)
                .encode(&raw, 1080, 781, jpeg_encoder::ColorType::Rgb)
                .unwrap();
            std::hint::black_box(out);
        }),
    );

    // A faster encoder that gives back a bigger or uglier file is not faster at anything
    // that matters: the point of the resize is the bytes on the wire and the pixels on the
    // screen.
    let mut current = Vec::new();
    image::codecs::jpeg::JpegEncoder::new_with_quality(&mut current, 85)
        .encode(small.as_raw(), 1080, 781, image::ExtendedColorType::Rgb8)
        .unwrap();
    let mut other = Vec::new();
    jpeg_encoder::Encoder::new(&mut other, 85)
        .encode(small.as_raw(), 1080, 781, jpeg_encoder::ColorType::Rgb)
        .unwrap();
    assert!(
        other.len() < current.len() * 11 / 10,
        "the one that is used must not pay for its speed in bytes: {} against {}",
        other.len(),
        current.len()
    );

    let difference = |bytes: &[u8]| {
        let back = image::load_from_memory(bytes).unwrap().into_rgb8();
        assert_eq!((1080, 781), (back.width(), back.height()));
        let total: u64 = back
            .as_raw()
            .iter()
            .zip(small.as_raw().iter())
            .map(|(a, b)| a.abs_diff(*b) as u64)
            .sum();
        total as f64 / back.as_raw().len() as f64
    };
    println!(
        "\n  taille   image {:>5} Ko   jpeg-encoder {:>5} Ko",
        current.len() / 1024,
        other.len() / 1024
    );
    println!(
        "  écart moyen au pixel d'origine   image {:.2}   jpeg-encoder {:.2}",
        difference(&current),
        difference(&other)
    );
    // The same question on a photograph rather than line art. Line art is what a manga page
    // is; a colour BD page is closer to this, and an encoder that only wins on one of them
    // would be a bad trade.
    let mut noisy = image::RgbImage::new(1400, 1936);
    for (x, y, pixel) in noisy.enumerate_pixels_mut() {
        let n = ((x * 7 + y * 13) % 251) as u8;
        let m = (((x / 3) ^ (y / 5)) % 199) as u8;
        *pixel = image::Rgb([n, m, ((x + y) % 233) as u8]);
    }
    let small = image::imageops::resize(&noisy, 1080, 1493, image::imageops::FilterType::Nearest);
    let raw = small.as_raw().clone();
    let (w, h) = (1080u32, 1493u32);
    timed(
        "photographique — image",
        Box::new({
            let raw = raw.clone();
            move || {
                let mut out = Vec::new();
                image::codecs::jpeg::JpegEncoder::new_with_quality(&mut out, 85)
                    .encode(&raw, w, h, image::ExtendedColorType::Rgb8)
                    .unwrap();
                std::hint::black_box(out);
            }
        }),
    );
    timed(
        "photographique — jpeg-encoder",
        Box::new({
            let raw = raw.clone();
            move || {
                let mut out = Vec::new();
                jpeg_encoder::Encoder::new(&mut out, 85)
                    .encode(&raw, w as u16, h as u16, jpeg_encoder::ColorType::Rgb)
                    .unwrap();
                std::hint::black_box(out);
            }
        }),
    );
    let mut a = Vec::new();
    image::codecs::jpeg::JpegEncoder::new_with_quality(&mut a, 85)
        .encode(&raw, w, h, image::ExtendedColorType::Rgb8)
        .unwrap();
    let mut b = Vec::new();
    jpeg_encoder::Encoder::new(&mut b, 85)
        .encode(&raw, w as u16, h as u16, jpeg_encoder::ColorType::Rgb)
        .unwrap();
    // The size gap is not free: below quality 90 this encoder halves the colour resolution
    // in both directions — 4:2:0, what every camera and every website does — where `image`
    // keeps it whole. So the fidelity has to be looked at, and on the worst case there is:
    // pure chroma noise, which is the one thing 4:2:0 is bad at and no page ever contains.
    let error = |bytes: &[u8]| {
        let back = image::load_from_memory(bytes).unwrap().into_rgb8();
        let total: u64 = back
            .as_raw()
            .iter()
            .zip(raw.iter())
            .map(|(x, y)| x.abs_diff(*y) as u64)
            .sum();
        total as f64 / raw.len() as f64
    };
    let mut whole_chroma = Vec::new();
    let mut encoder = jpeg_encoder::Encoder::new(&mut whole_chroma, 85);
    encoder.set_sampling_factor(jpeg_encoder::SamplingFactor::R_4_4_4);
    encoder
        .encode(&raw, w as u16, h as u16, jpeg_encoder::ColorType::Rgb)
        .unwrap();
    println!("\n  sur du bruit chromatique — le pire cas pour un sous-échantillonnage :");
    println!(
        "    image        4:4:4  {:>5} Ko   écart {:.2}",
        a.len() / 1024,
        error(&a)
    );
    println!(
        "    jpeg-encoder 4:2:0  {:>5} Ko   écart {:.2}",
        b.len() / 1024,
        error(&b)
    );
    println!(
        "    jpeg-encoder 4:4:4  {:>5} Ko   écart {:.2}",
        whole_chroma.len() / 1024,
        error(&whole_chroma)
    );
    println!();
}
