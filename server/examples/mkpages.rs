//! A CBZ of realistic pages, for comparing the two servers on one archive.
//!
//! Realistic in shape and in content: **portrait**, because a page wider than tall is read
//! as a double spread and given twice the width it asked for — which is correct, and made
//! the first run of this benchmark measure nothing at all. And line art over white rather
//! than noise, because noise is the worst case a JPEG can be handed and a page is the
//! opposite.
//!
//!     cargo run --release --example mkpages -- <folder>

fn main() {
    let folder = std::env::args()
        .nth(1)
        .expect("a folder to write the archive into");
    let (width, height) = (1400u32, 1936u32);

    let mut buffer = image::RgbImage::new(width, height);
    for (x, y, pixel) in buffer.enumerate_pixels_mut() {
        let panel = x % 640 < 8 || y % 480 < 8;
        let stroke = ((x as i32 - y as i32) % 97).abs() < 3 && (x / 40 + y / 60) % 3 == 0;
        let screentone = (x % 6 < 2 && y % 6 < 2) && (x / 300 + y / 400) % 2 == 0;
        *pixel = if panel || stroke {
            image::Rgb([20, 20, 20])
        } else if screentone {
            image::Rgb([130, 130, 130])
        } else {
            image::Rgb([248, 248, 248])
        };
    }

    let mut jpeg = std::io::Cursor::new(Vec::new());
    image::DynamicImage::ImageRgb8(buffer)
        .write_to(&mut jpeg, image::ImageFormat::Jpeg)
        .expect("encoding the page");
    let jpeg = jpeg.into_inner();

    let path = std::path::Path::new(&folder).join("Tome 1.cbz");
    let mut zip = zip::ZipWriter::new(std::fs::File::create(&path).expect("creating the archive"));
    for i in 0..40 {
        // Stored, as a real CBZ has it: a JPEG does not compress and every packer knows.
        zip.start_file::<_, ()>(
            format!("{i:03}.jpg"),
            zip::write::SimpleFileOptions::default()
                .compression_method(zip::CompressionMethod::Stored),
        )
        .expect("starting a page");
        use std::io::Write;
        zip.write_all(&jpeg).expect("writing a page");
    }
    zip.finish().expect("closing the archive");

    println!(
        "{} — 40 pages of {width}×{height}, {} MB",
        path.display(),
        jpeg.len() * 40 / 1024 / 1024
    );
}
