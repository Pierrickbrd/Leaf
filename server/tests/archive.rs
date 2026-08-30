//! Reading one archive: what is a page, what is a sidecar, and what is neither.
//!
//! A CBZ is a renamed zip and holds whatever somebody put in it. What matters is that the
//! decisions are made from the first few kilobytes — a scan of a real library is made of
//! seeks, and reading every member whole to find out what it is doubles the cost of one.

use std::io::Write;
use std::path::Path;

use leaf_server::archive::cbz;

fn jpeg(width: u32, height: u32) -> Vec<u8> {
    let mut buffer = image::RgbImage::new(width, height);
    for (x, y, pixel) in buffer.enumerate_pixels_mut() {
        *pixel = image::Rgb([(x % 256) as u8, (y % 256) as u8, 128]);
    }
    let mut out = std::io::Cursor::new(Vec::new());
    image::DynamicImage::ImageRgb8(buffer)
        .write_to(&mut out, image::ImageFormat::Jpeg)
        .unwrap();
    out.into_inner()
}

/// An archive holding exactly the members it is given, stored rather than deflated so a
/// member's declared size is the size the reader will meet.
fn archive(path: &Path, members: &[(&str, Vec<u8>)]) {
    std::fs::create_dir_all(path.parent().unwrap()).unwrap();
    let mut zip = zip::ZipWriter::new(std::fs::File::create(path).unwrap());
    let options =
        zip::write::SimpleFileOptions::default().compression_method(zip::CompressionMethod::Stored);
    for (name, bytes) in members {
        zip.start_file::<_, ()>(*name, options).unwrap();
        zip.write_all(bytes).unwrap();
    }
    zip.finish().unwrap();
}

#[test]
fn a_sidecar_bigger_than_the_head_is_read_a_second_time_and_read_whole() {
    // The head is four kilobytes. A work.json under it arrives with the first read; a real
    // ComicInfo with 2 677 chapters does not, and going back for it is the whole point of
    // there being a second pass at all.
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("Tome 1.cbz");
    let long = format!(
        "<?xml version=\"1.0\"?><ComicInfo><Notes>{}</Notes></ComicInfo>",
        "é".repeat(8000)
    );
    archive(
        &path,
        &[
            ("000.jpg", jpeg(60, 90)),
            ("ComicInfo.xml", long.clone().into_bytes()),
        ],
    );

    let content = cbz::read(&path, true).unwrap();
    assert_eq!(content.pages.len(), 1);
    assert_eq!(
        content.sidecar("ComicInfo.xml").map(<[u8]>::len),
        Some(long.len())
    );
}

#[test]
fn something_that_is_neither_a_page_nor_a_small_sidecar_is_left_alone() {
    // A stray video in a CBZ is not metadata, and going back for it would cost a seek and
    // four megabytes to learn nothing.
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("Tome 1.cbz");
    archive(
        &path,
        &[
            ("000.jpg", jpeg(60, 90)),
            ("bonus.bin", vec![0u8; 5 * 1024 * 1024]),
        ],
    );

    let content = cbz::read(&path, true).unwrap();
    assert_eq!(content.pages.len(), 1);
    assert!(content.sidecar("bonus.bin").is_none());
}

#[test]
fn a_folder_entry_is_not_a_member() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("Tome 1.cbz");
    let mut zip = zip::ZipWriter::new(std::fs::File::create(&path).unwrap());
    let options = zip::write::SimpleFileOptions::default();
    zip.add_directory::<_, ()>("Chapitre 1/", options).unwrap();
    zip.start_file::<_, ()>("Chapitre 1/000.jpg", options)
        .unwrap();
    zip.write_all(&jpeg(60, 90)).unwrap();
    zip.finish().unwrap();

    let content = cbz::read(&path, true).unwrap();
    assert_eq!(content.pages.len(), 1);
}

#[test]
fn pages_of_the_same_short_name_under_two_folders_are_named() {
    // Folders divide, they do not order: the pages sort on their short name, so two of them
    // sharing one is a reading order nothing can settle.
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("Tome 1.cbz");
    archive(
        &path,
        &[
            ("Chapitre 1/001.jpg", jpeg(60, 90)),
            ("Chapitre 2/001.jpg", jpeg(60, 90)),
            ("Chapitre 2/002.jpg", jpeg(60, 90)),
        ],
    );

    let content = cbz::read(&path, true).unwrap();
    assert_eq!(content.pages.len(), 3);
    assert_eq!(content.duplicate_names, vec!["001.jpg"]);
    // Ordered on the short name, folders ignored: 001, 001, then 002.
    assert_eq!(content.pages[2].short_name(), "002.jpg");
}

#[test]
fn the_dimensions_are_left_unmeasured_when_they_are_not_asked_for() {
    // Most of a scan's cost, and only the index wants them.
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("Tome 1.cbz");
    archive(&path, &[("000.jpg", jpeg(60, 90))]);

    assert_eq!(
        cbz::read(&path, true).unwrap().pages[0].dimension,
        Some((60, 90))
    );
    assert_eq!(cbz::read(&path, false).unwrap().pages[0].dimension, None);
}

#[test]
fn one_member_is_pulled_out_by_name_and_a_name_that_is_not_there_is_not_an_error() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("Tome 1.cbz");
    archive(&path, &[("entry.json", b"{\"leaf\":1}".to_vec())]);

    assert_eq!(
        cbz::extract(&path, "entry.json").unwrap().as_deref(),
        Some(&b"{\"leaf\":1}"[..])
    );
    // Absent is an answer, not a failure: most archives carry no entry.json at all.
    assert!(cbz::extract(&path, "ComicInfo.xml").unwrap().is_none());
}

#[test]
fn an_archive_that_is_not_one_is_refused_by_name() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("Tome 1.cbz");
    std::fs::write(&path, b"not a zip at all").unwrap();

    let refused = cbz::read(&path, true).unwrap_err().to_string();
    assert!(refused.contains("Tome 1.cbz"), "{refused}");
    assert!(cbz::extract(&path, "entry.json").is_err());
}

#[test]
fn every_stream_opened_is_counted() {
    // The same idea as counting statements in the database: opening an entry is a seek, and
    // a seek is what a scan of a large library is actually made of.
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("Tome 1.cbz");
    archive(
        &path,
        &[("000.jpg", jpeg(60, 90)), ("001.jpg", jpeg(60, 90))],
    );

    let before = cbz::streams_opened();
    cbz::read(&path, true).unwrap();
    assert!(
        cbz::streams_opened() >= before + 2,
        "two members, so at least two streams"
    );
}

/// A JPEG whose dimensions sit past the first four kilobytes.
///
/// A real one: a scanner writes an EXIF thumbnail into an APP1 segment before the frame
/// header, so the size of the page is not in the bytes the reader took to recognise it.
fn jpeg_with_a_fat_header(width: u32, height: u32) -> Vec<u8> {
    let plain = jpeg(width, height);
    let mut out = plain[..2].to_vec(); // SOI
    let payload = vec![0x20u8; 6000];
    out.extend_from_slice(&[0xff, 0xe1]); // APP1
    out.extend_from_slice(&((payload.len() + 2) as u16).to_be_bytes());
    out.extend_from_slice(&payload);
    out.extend_from_slice(&plain[2..]);
    out
}

#[test]
fn a_page_the_head_could_not_measure_is_read_again_and_measured() {
    // The head settles most pages. The ones it does not are worth a second seek rather than
    // a page of unknown size in the index — which is what a client sizes its layout on.
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("Tome 1.cbz");
    let fat = jpeg_with_a_fat_header(600, 900);
    assert!(fat.len() > 4096);
    archive(&path, &[("000.jpg", fat), ("001.jpg", jpeg(60, 90))]);

    let content = cbz::read(&path, true).unwrap();
    assert_eq!(content.pages.len(), 2);
    assert_eq!(content.pages[0].dimension, Some((600, 900)));
    assert_eq!(content.pages[1].dimension, Some((60, 90)));
}

#[test]
fn a_page_nobody_asked_to_measure_is_not_read_a_second_time() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("Tome 1.cbz");
    archive(&path, &[("000.jpg", jpeg_with_a_fat_header(600, 900))]);

    let content = cbz::read(&path, false).unwrap();
    // Nothing to go back for: the second look only ever happens to measure a page, and
    // nobody asked. The count itself is not asserted here — `OPENED` is global to the
    // process and these tests run beside each other, so a delta measures the schedule.
    assert_eq!(content.pages[0].dimension, None);
    assert_eq!(
        cbz::read(&path, true).unwrap().pages[0].dimension,
        Some((600, 900))
    );
}
