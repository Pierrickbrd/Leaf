//! What comes back for a page that is not a JPEG.
//!
//! The library holds whatever a packer put in it, and the resize path has exactly one output
//! format. What each input becomes is therefore worth stating rather than discovering.

use std::io::Write;
use std::sync::Arc;

use leaf_server::api::pages::Pages;
use leaf_server::scan::scanner::Scanner;
use leaf_server::store::Db;

/// The bar pattern, plus a vertical rule every 300 pixels.
fn opaque(x: u32, y: u32) -> image::Rgb<u8> {
    if y % 120 < 30 || x % 300 < 6 {
        image::Rgb([20, 20, 20])
    } else {
        image::Rgb([250, 250, 250])
    }
}

/// The left third transparent, and black underneath it — which is what a PNG saved with a
/// transparent background actually holds.
fn translucent(x: u32, y: u32, width: u32) -> image::Rgba<u8> {
    if x < width / 3 {
        image::Rgba([0, 0, 0, 0])
    } else if y % 120 < 30 {
        image::Rgba([20, 20, 20, 255])
    } else {
        image::Rgba([250, 250, 250, 255])
    }
}

/// A page with a plain white background and a black bar, in whatever format is asked for.
fn drawn(format: image::ImageFormat, alpha: bool) -> Vec<u8> {
    let (w, h) = (900u32, 1200u32);
    let mut out = std::io::Cursor::new(Vec::new());
    let page = if alpha {
        image::DynamicImage::ImageRgba8(image::RgbaImage::from_fn(w, h, |x, y| {
            translucent(x, y, w)
        }))
    } else {
        image::DynamicImage::ImageRgb8(image::RgbImage::from_fn(w, h, opaque))
    };
    page.write_to(&mut out, format).unwrap();
    out.into_inner()
}

struct One {
    _dir: tempfile::TempDir,
    pages: Arc<Pages>,
    entry: String,
}

fn served(name: &str, bytes: &[u8]) -> One {
    let dir = tempfile::tempdir().unwrap();
    let folder = dir.path().join("library/Essai");
    std::fs::create_dir_all(&folder).unwrap();
    let mut zip = zip::ZipWriter::new(std::fs::File::create(folder.join("Tome 1.cbz")).unwrap());
    zip.start_file::<_, ()>(
        name,
        zip::write::SimpleFileOptions::default().compression_method(zip::CompressionMethod::Stored),
    )
    .unwrap();
    zip.write_all(bytes).unwrap();
    zip.finish().unwrap();

    let db = Arc::new(Db::open(&dir.path().join("index.sqlite")).unwrap());
    Scanner::new(Arc::clone(&db), true)
        .scan(&[dir.path().join("library")])
        .unwrap();
    let entry = db
        .read(|cx| cx.query_one("SELECT id FROM entry", [], |r| r.get::<_, String>(0)))
        .unwrap()
        .expect("an entry");
    let pages = Arc::new(Pages::new(db, dir.path().join("cache"), 85, 64 << 20));
    pages.prepare();
    One {
        _dir: dir,
        pages,
        entry,
    }
}

#[test]
fn every_format_the_scanner_reads_can_also_be_served_and_shrunk() {
    for (name, format) in [
        ("000.png", image::ImageFormat::Png),
        ("000.webp", image::ImageFormat::WebP),
        ("000.gif", image::ImageFormat::Gif),
        ("000.bmp", image::ImageFormat::Bmp),
        ("000.jpg", image::ImageFormat::Jpeg),
    ] {
        let source = drawn(format, false);
        let one = served(name, &source);

        // Without a width the bytes go back exactly as they are, in their own format.
        let whole = one
            .pages
            .page(&one.entry, 0, None)
            .unwrap()
            .expect("a page");
        assert_eq!(source, whole.bytes, "{name} must go back untouched");

        // With one, it is decoded, shrunk, and re-encoded — as a JPEG, whatever it was.
        let small = one
            .pages
            .page(&one.entry, 0, Some(300))
            .unwrap()
            .expect("a page");
        let back = image::load_from_memory(&small.bytes).unwrap();
        // Either it was shrunk, or the shrinking would have cost more bytes than it saved
        // and the original went back instead — which is a decision, and the only two
        // outcomes there are.
        let shrunk = back.width() == 300;
        assert!(
            shrunk || small.bytes == source,
            "{name}: neither shrunk nor left alone — {} wide, {} bytes",
            back.width(),
            small.bytes.len()
        );
        assert!(
            small.bytes.len() <= source.len(),
            "{name}: asking for less gave back more"
        );
        println!(
            "  {name:10} {:>6} Ko {:>4}px  →  {:>6} Ko {:>4}px  {:11} {}",
            source.len() / 1024,
            900,
            small.bytes.len() / 1024,
            back.width(),
            small.media_type,
            if shrunk {
                "réduit"
            } else {
                "source rendue telle quelle"
            }
        );
    }
}

/// Transparency has nowhere to go in a JPEG, and dropping it is not the same as flattening it.
///
/// A PNG saved with a transparent background holds black under the clear part, so the page
/// came back with ink where a reader expects paper.
#[test]
fn a_transparent_page_does_not_come_back_with_black_where_it_was_clear() {
    let source = drawn(image::ImageFormat::Png, true);
    let one = served("000.png", &source);

    let small = one
        .pages
        .page(&one.entry, 0, Some(300))
        .unwrap()
        .expect("a page");
    let back = image::load_from_memory(&small.bytes).unwrap().into_rgb8();

    // The left third was transparent over black. A reader expects paper there, not ink.
    let corner = back.get_pixel(10, 10);
    assert!(
        corner.0.iter().all(|c| *c > 240),
        "the clear third came back as {:?} — dropping alpha is not flattening it",
        corner.0
    );
    // And the drawing on the opaque side is still there, so nothing was flattened that
    // should not have been.
    let ink = (0..300)
        .map(|y| back.get_pixel(200, y))
        .filter(|p| p.0[0] < 80)
        .count();
    assert!(
        ink > 20,
        "the ink went with the transparency: {ink} dark pixels"
    );
}
