//! The small decisions, each asked directly.
//!
//! Everything here is a pure function with one job and a case somebody will meet: a padding
//! spec nobody defined, an entity no XML reader knows, a magic number this server refuses on
//! purpose. They are cheap to get wrong and invisible when they are.

use std::path::Path;

use leaf_server::api::dto::SeriesSort;
use leaf_server::archive::images::media_type;
use leaf_server::metadata::label::{compose, parse};
use leaf_server::metadata::legacy_comic_info;
use leaf_server::scan::layout::holds_archives_within;
use leaf_server::store::files::write_whole;

// ------------------------------------------------------------------- the images

/// Twelve bytes: four of length, `ftyp`, and the brand.
fn boxed(brand: &[u8]) -> Vec<u8> {
    let mut head = vec![0, 0, 0, 0x18];
    head.extend_from_slice(b"ftyp");
    head.extend_from_slice(brand);
    head.extend_from_slice(&[0u8; 8]);
    head
}

#[test]
fn the_formats_this_server_will_not_decode_are_still_recognised() {
    // Named rather than ignored: a page that is AVIF is a page, and the scan should say so
    // instead of reporting a volume with fewer pages than it has.
    assert_eq!(media_type(&boxed(b"avif")), Some("image/avif"));
    assert_eq!(media_type(&boxed(b"avis")), Some("image/avif"));
    for brand in [b"heic", b"heix", b"hevc", b"mif1"] {
        assert_eq!(media_type(&boxed(brand)), Some("image/heic"), "{brand:?}");
    }
}

#[test]
fn a_box_of_some_other_kind_is_not_an_image() {
    assert_eq!(media_type(&boxed(b"mp42")), None);
    assert_eq!(media_type(b"not long enough"), None);
    assert_eq!(media_type(&[]), None);
}

// ------------------------------------------------------------------- the layout

#[test]
fn a_search_with_no_levels_left_finds_nothing() {
    // The floor of the recursion, and the thing that stops a library of symlinks walking
    // for ever.
    let dir = tempfile::tempdir().unwrap();
    std::fs::write(dir.path().join("Tome 1.cbz"), b"pk").unwrap();
    assert!(holds_archives_within(dir.path(), 1));
    assert!(!holds_archives_within(dir.path(), 0));
}

// --------------------------------------------------------------------- the shelf

#[test]
fn every_way_of_ordering_a_shelf_names_a_column() {
    // Four orders, and each has to reach SQL as something the index can use.
    for sort in [
        SeriesSort::Name,
        SeriesSort::Added,
        SeriesSort::Updated,
        SeriesSort::Volumes,
    ] {
        let sql = sort.sql();
        assert!(!sql.is_empty(), "{sort:?}");
        assert!(sql.contains("w.name"), "{sort:?}: {sql}");
    }
    assert!(SeriesSort::Added.sql().starts_with("added_at DESC"));
    assert!(SeriesSort::Updated.sql().starts_with("last_added_at DESC"));
    assert!(SeriesSort::Volumes.sql().starts_with("entry_count DESC"));
}

// -------------------------------------------------------------------- the labels

#[test]
fn a_colon_with_no_space_before_it_still_separates() {
    // ": " on its own is what a colon usually gets — nobody writes " : " in a file name.
    let read = parse("Chapitre 12: L'Été");
    assert_eq!(read.label, "Chapitre 12");
    assert_eq!(read.title.as_deref(), Some("L'Été"));
}

#[test]
fn a_pattern_that_opens_a_placeholder_and_never_closes_it_stops_there() {
    // Written by hand into edition.json, so it will be wrong sooner or later. What it must
    // not do is loop, or swallow the rest of the pattern in silence.
    assert_eq!(
        compose(Some("Chapitre {n"), Some(7.0)).as_deref(),
        Some("Chapitre {n")
    );
    // And the part before it is written once, not twice.
    assert_eq!(
        compose(Some("Vol. {n:000} — Chapitre {n"), Some(7.0)).as_deref(),
        Some("Vol. 007 — Chapitre {n")
    );
}

#[test]
fn a_padding_nobody_defined_is_written_out_as_it_stands() {
    // `{n:xxx}` is not a padding. Left verbatim, so the mistake is visible in the label
    // rather than silently becoming a number nobody asked for.
    assert_eq!(
        compose(Some("Chapitre {n:xxx}"), Some(7.0)).as_deref(),
        Some("Chapitre {n:xxx}")
    );
    assert_eq!(
        compose(Some("Chapitre {n:000}"), Some(7.0)).as_deref(),
        Some("Chapitre 007")
    );
}

// ------------------------------------------------------------------ the ComicInfo

#[test]
fn an_entity_no_reader_knows_is_given_back_as_it_was_written() {
    // Nothing external is fetched and nothing is invented: a document that declares its own
    // entities gets them back verbatim rather than expanded.
    let read = legacy_comic_info::read(
        b"<?xml version=\"1.0\"?><ComicInfo><Title>Death &amp; &inconnu; Strawberry</Title></ComicInfo>",
    )
    .expect("a document");
    assert_eq!(
        read.entry.title.as_deref(),
        Some("Death & &inconnu; Strawberry")
    );
}

#[test]
fn a_document_that_is_not_xml_is_nothing_rather_than_half_of_something() {
    assert!(legacy_comic_info::read(b"<ComicInfo><Title>unclosed").is_none());
    assert!(legacy_comic_info::read(&[0xff, 0xfe, 0x00]).is_none());
}

// -------------------------------------------------------------- writing a file

#[test]
fn a_whole_file_is_written_beside_and_then_renamed_over() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("work.json");
    write_whole(&path, b"{\"leaf\":1}").unwrap();
    assert_eq!(std::fs::read(&path).unwrap(), b"{\"leaf\":1}");
    // And nothing is left beside it.
    let left: Vec<_> = std::fs::read_dir(dir.path())
        .unwrap()
        .flatten()
        .map(|e| e.file_name().to_string_lossy().to_string())
        .collect();
    assert_eq!(left, vec!["work.json".to_string()]);
}

#[test]
fn a_write_that_cannot_start_leaves_nothing_behind() {
    // A folder nobody may write in: the temporary is never created, so there is nothing to
    // clean up and the failure is the caller's to see.
    let dir = tempfile::tempdir().unwrap();
    let closed = dir.path().join("closed");
    std::fs::create_dir(&closed).unwrap();
    read_only(&closed);

    let refused = write_whole(&closed.join("work.json"), b"{}").unwrap_err();
    assert_eq!(refused.kind(), std::io::ErrorKind::PermissionDenied);

    writable(&closed);
    assert_eq!(std::fs::read_dir(&closed).unwrap().count(), 0);
}

fn read_only(path: &Path) {
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        std::fs::set_permissions(path, std::fs::Permissions::from_mode(0o555)).unwrap();
    }
}

fn writable(path: &Path) {
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        std::fs::set_permissions(path, std::fs::Permissions::from_mode(0o755)).unwrap();
    }
}
