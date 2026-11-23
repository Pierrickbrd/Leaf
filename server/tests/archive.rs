//! Reading one archive: what is a page, what is a sidecar, and what is neither.
//!
//! A CBZ is a renamed zip and holds whatever somebody put in it. What matters is that the
//! decisions are made from the first few kilobytes — a scan of a real library is made of
//! seeks, and reading every member whole to find out what it is doubles the cost of one.

use std::io::Write;
use std::path::Path;

use leaf_server::archive::{cbz, cbz_writer};

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
fn the_sidecars_of_one_archive_are_kept_up_to_a_total_and_no_further() {
    // Each of these is under the size of one sidecar, so each is read; twelve of them are not
    // metadata but a way to put gigabytes in memory, a deflated zero costing a thousandth of
    // what it fills. What is kept stops at a total, whatever the number of members.
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("Tome 1.cbz");
    let names: Vec<String> = (0..12).map(|i| format!("extra-{i}.bin")).collect();
    let mut members: Vec<(&str, Vec<u8>)> = vec![("000.jpg", jpeg(60, 90))];
    for name in &names {
        members.push((name.as_str(), vec![0u8; 3 * 1024 * 1024]));
    }
    archive(&path, &members);

    let content = cbz::read(&path, true).unwrap();
    assert_eq!(content.pages.len(), 1);
    let kept: usize = content.sidecars.iter().map(|(_, bytes)| bytes.len()).sum();
    assert!(
        kept <= 16 * 1024 * 1024,
        "{kept} bytes of sidecars kept, past the total allowed"
    );
    assert!(
        !content.sidecars.is_empty(),
        "the first ones are still read"
    );
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

/// Two deflated pages and a ComicInfo.xml: deflated, because a stored page would come out
/// byte-identical even from a rewrite that re-encoded it.
fn volume(path: &Path, extra: &[(&str, &[u8])]) {
    let mut zip = zip::ZipWriter::new(std::fs::File::create(path).unwrap());
    let options = zip::write::SimpleFileOptions::default()
        .compression_method(zip::CompressionMethod::Deflated);
    let page = "0123456789".repeat(500);
    for (name, bytes) in [
        ("1.jpg", page.as_bytes()),
        ("2.jpg", page.as_bytes()),
        ("ComicInfo.xml", &b"<ComicInfo/>"[..]),
    ]
    .into_iter()
    .chain(extra.iter().copied())
    {
        zip.start_file::<_, ()>(name, options).unwrap();
        zip.write_all(bytes).unwrap();
    }
    zip.finish().unwrap();
}

fn names(path: &Path) -> Vec<String> {
    let zip = zip::ZipArchive::new(std::fs::File::open(path).unwrap()).unwrap();
    zip.file_names().map(String::from).collect()
}

#[test]
fn an_archive_loses_its_comic_info_and_gains_an_entry_json_in_one_pass() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("Tome 1.cbz");
    volume(&path, &[]);

    cbz_writer::rewrite(&path, &[("entry.json", b"{}")], &["ComicInfo.xml"]).unwrap();

    assert_eq!(names(&path), ["1.jpg", "2.jpg", "entry.json"]);
    assert_eq!(
        cbz::extract(&path, "entry.json").unwrap().as_deref(),
        Some(&b"{}"[..])
    );
}

#[test]
fn no_page_is_recompressed_by_a_rewrite() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("Tome 1.cbz");
    volume(&path, &[]);
    let raw = |path: &Path| -> Vec<Vec<u8>> {
        let mut zip = zip::ZipArchive::new(std::fs::File::open(path).unwrap()).unwrap();
        (0..2)
            .map(|i| {
                let mut bytes = Vec::new();
                std::io::Read::read_to_end(&mut zip.by_index_raw(i).unwrap(), &mut bytes).unwrap();
                bytes
            })
            .collect()
    };
    let before = raw(&path);

    cbz_writer::rewrite(&path, &[("entry.json", b"{}")], &["ComicInfo.xml"]).unwrap();

    assert_eq!(raw(&path), before);
}

#[test]
fn a_comic_info_in_a_subfolder_or_in_other_case_is_removed_too() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("Tome 1.cbz");
    volume(&path, &[("x/comicinfo.XML", b"<ComicInfo/>")]);

    cbz_writer::rewrite(&path, &[], &["ComicInfo.xml"]).unwrap();

    assert_eq!(names(&path), ["1.jpg", "2.jpg"]);
}

#[test]
fn an_entry_json_already_there_is_replaced_not_doubled() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("Tome 1.cbz");
    volume(&path, &[("entry.json", b"{\"old\":1}")]);

    cbz_writer::rewrite(&path, &[("entry.json", b"{\"new\":2}")], &[]).unwrap();

    assert_eq!(
        names(&path).iter().filter(|n| *n == "entry.json").count(),
        1
    );
    assert_eq!(
        cbz::extract(&path, "entry.json").unwrap().as_deref(),
        Some(&b"{\"new\":2}"[..])
    );
}

#[test]
fn removing_a_name_that_is_not_there_is_not_an_error() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("Tome 1.cbz");
    volume(&path, &[]);

    cbz_writer::rewrite(&path, &[], &["entry.json"]).unwrap();

    assert_eq!(names(&path), ["1.jpg", "2.jpg", "ComicInfo.xml"]);
}

#[test]
fn two_members_are_added_in_one_pass_and_each_replaces_its_namesake() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("Tome 1.cbz");
    volume(&path, &[("oneshot.json", b"{\"title\":\"old\"}")]);

    cbz_writer::rewrite(
        &path,
        &[
            ("entry.json", b"{}"),
            ("oneshot.json", b"{\"title\":\"X\"}"),
        ],
        &["ComicInfo.xml"],
    )
    .unwrap();

    assert_eq!(
        names(&path),
        ["1.jpg", "2.jpg", "entry.json", "oneshot.json"]
    );
    assert_eq!(
        cbz::extract(&path, "oneshot.json").unwrap().as_deref(),
        Some(&b"{\"title\":\"X\"}"[..])
    );
}

#[test]
fn adding_the_same_name_twice_is_refused_and_leaves_the_archive_as_it_was() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("Tome 1.cbz");
    volume(&path, &[]);
    let before = std::fs::read(&path).unwrap();

    let refused = cbz_writer::rewrite(
        &path,
        &[("entry.json", b"{}"), ("ENTRY.json", b"{\"a\":1}")],
        &[],
    );

    assert!(refused.is_err());
    assert_eq!(std::fs::read(&path).unwrap(), before);
}

// ------------------------------------------------------------------ splicing a chapter in

/// A deflated archive of `count` pages named `001.jpg` and on, each its own image, plus
/// whatever `extra` holds.
fn pages(path: &Path, count: u32, width: u32, extra: &[(&str, &[u8])]) {
    let mut zip = zip::ZipWriter::new(std::fs::File::create(path).unwrap());
    let options = zip::write::SimpleFileOptions::default()
        .compression_method(zip::CompressionMethod::Deflated);
    for n in 1..=count {
        zip.start_file::<_, ()>(format!("{n:03}.jpg"), options)
            .unwrap();
        zip.write_all(&jpeg(width + n, 90)).unwrap();
    }
    for (name, bytes) in extra {
        zip.start_file::<_, ()>(*name, options).unwrap();
        zip.write_all(bytes).unwrap();
    }
    zip.finish().unwrap();
}

/// Every member's compressed bytes, by name.
fn raw_members(path: &Path) -> std::collections::HashMap<String, Vec<u8>> {
    let mut zip = zip::ZipArchive::new(std::fs::File::open(path).unwrap()).unwrap();
    (0..zip.len())
        .map(|i| {
            let mut member = zip.by_index_raw(i).unwrap();
            let mut bytes = Vec::new();
            std::io::Read::read_to_end(&mut member, &mut bytes).unwrap();
            (member.name().to_string(), bytes)
        })
        .collect()
}

#[test]
fn a_splice_takes_the_source_pages_in_the_target_order_and_recompresses_none() {
    let dir = tempfile::tempdir().unwrap();
    let target = dir.path().join("Tome 03.cbz");
    let source = dir.path().join("source.cbz");
    let destination = dir.path().join("Tome 03.cbz.new");
    pages(&target, 10, 100, &[("entry.json", b"{\"old\":true}")]);
    pages(&source, 12, 300, &[("ComicInfo.xml", b"<ComicInfo/>")]);

    cbz_writer::splice_into(
        &target,
        &source,
        &destination,
        (3, 5),
        (3, 7),
        b"{\"new\":1}",
    )
    .unwrap();

    let read = cbz::read(&destination, false).unwrap();
    let names: Vec<&str> = read.pages.iter().map(|p| p.name.as_str()).collect();
    let wanted: Vec<String> = (1..=12).map(|n| format!("{n:04}.jpg")).collect();
    assert_eq!(wanted, names);
    // Target pages 0–2, source pages 3–7, target pages 6–9: by their original names.
    let origin: Vec<(&Path, String)> = (1..=3)
        .map(|n| (target.as_path(), format!("{n:03}.jpg")))
        .chain((4..=8).map(|n| (source.as_path(), format!("{n:03}.jpg"))))
        .chain((7..=10).map(|n| (target.as_path(), format!("{n:03}.jpg"))))
        .collect();
    let out = raw_members(&destination);
    let from_target = raw_members(&target);
    let from_source = raw_members(&source);
    for (wanted, (archive, name)) in wanted.iter().zip(&origin) {
        let held = if *archive == target.as_path() {
            &from_target
        } else {
            &from_source
        };
        assert_eq!(held[name], out[wanted], "{wanted} is {name}, byte for byte");
    }
    assert_eq!(
        Some(&b"{\"new\":1}"[..]),
        cbz::extract(&destination, "entry.json").unwrap().as_deref()
    );
    assert!(
        read.sidecar("ComicInfo.xml").is_none(),
        "nothing of the source but its pages"
    );
    assert_eq!(1, read.sidecars.len(), "{:?}", read.sidecars);
}

#[test]
fn a_failed_splice_leaves_no_half_written_archive() {
    let dir = tempfile::tempdir().unwrap();
    let target = dir.path().join("Tome 03.cbz");
    let source = dir.path().join("source.cbz");
    let destination = dir.path().join("Tome 03.cbz.new");
    pages(&target, 10, 100, &[]);
    std::fs::write(&source, b"not an archive").unwrap();
    let before = std::fs::read(&target).unwrap();

    assert!(
        cbz_writer::splice_into(&target, &source, &destination, (3, 5), (3, 7), b"{}").is_err()
    );
    assert!(!destination.exists());
    assert_eq!(before, std::fs::read(&target).unwrap());

    // A range past the source's last page is refused the same way, nothing written.
    pages(&source, 4, 300, &[]);
    assert!(
        cbz_writer::splice_into(&target, &source, &destination, (3, 5), (2, 9), b"{}").is_err()
    );
    assert!(!destination.exists());
    // And one past the target's.
    assert!(
        cbz_writer::splice_into(&target, &source, &destination, (8, 12), (0, 1), b"{}").is_err()
    );
    assert!(!destination.exists());
}

/// An empty `0003.jpg` at the root is no page, so it was kept — under the name the third
/// page takes, and the zip refused a second member of that name, failing the whole splice.
#[test]
fn a_member_bearing_a_page_new_name_does_not_fail_the_splice() {
    let dir = tempfile::tempdir().unwrap();
    let target = dir.path().join("Tome 03.cbz");
    let source = dir.path().join("source.cbz");
    let destination = dir.path().join("Tome 03.cbz.new");
    pages(&target, 10, 100, &[("0003.jpg", b"")]);
    pages(&source, 12, 300, &[]);

    cbz_writer::splice_into(&target, &source, &destination, (3, 5), (3, 7), b"{}").unwrap();

    let read = cbz::read(&destination, false).unwrap();
    assert_eq!(12, read.pages.len());
    assert_eq!(
        names(&destination).len(),
        13,
        "twelve pages and the entry.json"
    );
}

/// Only a failure on the file the pages come from says the file sent is broken: the
/// volume's own failing had the file sent dropped from its session as if it were.
#[test]
fn a_splice_says_when_the_file_it_takes_pages_from_is_the_broken_one() {
    let dir = tempfile::tempdir().unwrap();
    let target = dir.path().join("Tome 03.cbz");
    let source = dir.path().join("source.cbz");
    let destination = dir.path().join("Tome 03.cbz.new");
    pages(&target, 10, 100, &[]);
    pages(&source, 12, 300, &[]);
    let whole = std::fs::read(&target).unwrap();
    std::fs::write(&target, &whole[..whole.len() / 2]).unwrap();

    let failed = cbz_writer::splice_into(&target, &source, &destination, (3, 5), (3, 7), b"{}")
        .expect_err("a truncated volume");
    assert!(failed.downcast_ref::<cbz_writer::FromSource>().is_none());

    std::fs::write(&target, &whole).unwrap();
    std::fs::write(&source, b"not an archive").unwrap();
    let failed = cbz_writer::splice_into(&target, &source, &destination, (3, 5), (3, 7), b"{}")
        .expect_err("a file sent that is no archive");
    assert!(failed.downcast_ref::<cbz_writer::FromSource>().is_some());
    assert!(cbz_writer::unreadable(&failed));
}

/// What a page decompresses to is what tells it from another once a rewrite or a splice has
/// renamed it; a sidecar is no page, and a deflated page is hashed as its content and not as
/// the bytes stored.
#[test]
fn page_digests_follow_the_content_of_pages_and_ignore_the_rest() {
    let dir = tempfile::tempdir().unwrap();
    let one = dir.path().join("one.cbz");
    let two = dir.path().join("two.cbz");
    let first = jpeg(20, 30);
    let second = jpeg(21, 31);
    archive(
        &one,
        &[
            ("001.jpg", first.clone()),
            ("002.jpg", second.clone()),
            ("entry.json", b"{}".to_vec()),
        ],
    );
    // The same pages, swapped and under other names, one of them deflated this time.
    let mut zip = zip::ZipWriter::new(std::fs::File::create(&two).unwrap());
    zip.start_file::<_, ()>("b/0001.jpg", zip::write::SimpleFileOptions::default())
        .unwrap();
    zip.write_all(&second).unwrap();
    zip.start_file::<_, ()>("a.jpg", zip::write::SimpleFileOptions::default())
        .unwrap();
    zip.write_all(&first).unwrap();
    zip.finish().unwrap();

    let sums = |path: &Path| -> Vec<String> {
        let mut sums: Vec<String> = cbz::page_digests(path)
            .unwrap()
            .into_iter()
            .map(|p| p.checksum)
            .collect();
        sums.sort();
        sums
    };

    assert_eq!(2, sums(&one).len(), "the entry.json is no page");
    assert_eq!(sums(&one), sums(&two));
}
