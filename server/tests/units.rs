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

// ------------------------------------------------------------------ the throttle

#[test]
fn ten_wrong_keys_from_one_address_close_the_door_for_a_while() {
    use leaf_server::api::throttle::Throttle;
    use std::time::Duration;

    let throttle = Throttle::new(3, Duration::from_secs(300), Duration::from_secs(900));
    assert!(throttle.blocked_for("10.0.0.1").is_none());

    for _ in 0..3 {
        throttle.record_failure("10.0.0.1");
    }
    let left = throttle.blocked_for("10.0.0.1").expect("refused");
    assert!(left.as_secs() > 800, "{left:?}");
    // And only that address: one device getting it wrong must not lock out the household.
    assert!(throttle.blocked_for("10.0.0.2").is_none());
}

#[test]
fn a_key_that_works_clears_the_slate() {
    use leaf_server::api::throttle::Throttle;
    use std::time::Duration;

    let throttle = Throttle::new(3, Duration::from_secs(300), Duration::from_secs(900));
    throttle.record_failure("10.0.0.1");
    throttle.record_failure("10.0.0.1");
    throttle.record_success("10.0.0.1");
    // Two more would have been three without the clearing.
    throttle.record_failure("10.0.0.1");
    throttle.record_failure("10.0.0.1");
    assert!(throttle.blocked_for("10.0.0.1").is_none());
}

#[test]
fn too_many_addresses_presenting_wrong_keys_and_the_oldest_are_forgotten() {
    // The table is bounded: an attacker rotating source addresses must not be able to make
    // this server hold one record per address it has ever seen.
    use leaf_server::api::throttle::Throttle;
    use std::time::Duration;

    let throttle = Throttle::new(2, Duration::from_secs(300), Duration::from_secs(900));
    // One that is actually blocked, put in first so it is the oldest of all.
    throttle.record_failure("10.0.0.1");
    throttle.record_failure("10.0.0.1");
    assert!(throttle.blocked_for("10.0.0.1").is_some());

    for n in 0..1200u32 {
        throttle.record_failure(&format!("192.168.{}.{}", n / 256, n % 256));
    }

    // Bounded, whatever arrives.
    assert!(throttle.remembered() <= 1024, "{}", throttle.remembered());
    // And a block is the thing actually being enforced, so it outlives a bare count: the
    // address that was refused is still refused.
    assert!(throttle.blocked_for("10.0.0.1").is_some());
}

#[test]
fn failures_age_out_so_a_device_that_gets_it_wrong_once_a_week_never_accumulates() {
    use leaf_server::api::throttle::Throttle;
    use std::time::Duration;

    // A window of milliseconds rather than minutes: the rule is the same one, and the test
    // waits for it rather than for five minutes.
    let throttle = Throttle::new(3, Duration::from_millis(60), Duration::from_secs(900));
    throttle.record_failure("10.0.0.1");
    throttle.record_failure("10.0.0.1");
    std::thread::sleep(Duration::from_millis(120));

    // Two more. Four failures in all, and still not refused: the first two fell out of the
    // window before the last two arrived.
    throttle.record_failure("10.0.0.1");
    throttle.record_failure("10.0.0.1");
    assert!(throttle.blocked_for("10.0.0.1").is_none());
}

#[test]
fn a_table_full_of_addresses_nothing_is_held_against_empties_itself() {
    // The cheap half of making room: everything in there had aged out, so forgetting the
    // oldest by hand is not needed.
    use leaf_server::api::throttle::Throttle;
    use std::time::Duration;

    let throttle = Throttle::new(10, Duration::from_millis(40), Duration::from_secs(1));
    for n in 0..1024u32 {
        throttle.record_failure(&format!("192.168.{}.{}", n / 256, n % 256));
    }
    assert_eq!(throttle.remembered(), 1024);

    std::thread::sleep(Duration::from_millis(80));
    throttle.record_failure("10.0.0.9");
    assert!(
        throttle.remembered() < 100,
        "everything that aged out should be gone, not just the oldest half: {}",
        throttle.remembered()
    );
}

// -------------------------------------------------------------- folding a title

#[test]
fn what_separates_and_what_joins_inside_a_search_key() {
    use leaf_server::store::text::search_key;
    // A separator writes one space, and only between two things — never before the first.
    assert_eq!(search_key("  ---  Bleach  ---  "), "bleach");
    assert_eq!(search_key("Death & Strawberry"), "death strawberry");
    // The ligatures NFD does not take apart, because they are letters and not a mark.
    assert_eq!(search_key("Ørsted"), "orsted");
    assert_eq!(search_key("Cœur Æther Straße"), "coeur aether strasse");
}

#[test]
fn a_collection_with_no_whole_number_in_it_has_no_gaps_to_report() {
    use leaf_server::store::text::gaps;
    // 45.5 and 108.5 are chapters between volumes, not volumes. With nothing whole to count
    // from, "which volumes are missing" has no answer worth inventing.
    assert!(gaps(&[45.5, 108.5], None, &[]).is_empty());
    assert!(gaps(&[], Some(4), &[]).is_empty());
}

#[test]
fn a_word_nobody_typed_is_near_nothing() {
    use leaf_server::store::text::{nearest, tolerance};
    // A term short enough to allow no mistakes at all: anything that is not a prefix of a
    // candidate is simply not that candidate.
    assert_eq!(tolerance("ab"), 0);
    assert_eq!(nearest("ab", ["abricot", "zz"].into_iter()), Some(0));
    assert_eq!(nearest("qq", ["abricot", "zz"].into_iter()), None);
}

// -------------------------------------------------------------------- the keys

#[test]
fn a_secret_too_short_to_be_one_stops_the_server_rather_than_being_ignored() {
    use leaf_server::api::keys::Keys;
    // A short secret is not a key, it is a password somebody will guess. The throttle slows
    // an attacker down; it does not make this safe.
    let refused = Keys::parse(Some("phone:a:read")).unwrap_err().to_string();
    assert!(refused.contains("characters"), "{refused}");
}

#[test]
fn a_key_with_no_right_anyone_recognises_gets_the_read_one() {
    use leaf_server::api::keys::Keys;
    let keys = Keys::parse(Some("phone:1111111111111111:écrire,tout")).unwrap();
    let key = keys.recognise(Some("1111111111111111")).expect("the key");
    assert_eq!(key.permissions.len(), 1);
}

#[test]
fn no_secret_at_all_is_not_a_key() {
    use leaf_server::api::keys::Keys;
    let keys = Keys::parse(Some("phone:1111111111111111:read")).unwrap();
    assert!(keys.recognise(None).is_none());
    assert!(keys.recognise(Some("   ")).is_none());
    assert!(keys.recognise(Some("2222222222222222")).is_none());
}

// ------------------------------------------------------------------ the report

#[test]
fn a_report_with_more_than_sixteen_of_one_thing_says_how_many_more() {
    use leaf_server::scan::report::ScanReport;
    // A scan of a real library finds hundreds. A report nobody can read to the end is a
    // report nobody reads at all.
    let report = ScanReport {
        errors: (1..=20).map(|n| format!("erreur {n}")).collect(),
        chapters_without_start_page: vec!["Tome 1 → Chapitre 1".into()],
        ..Default::default()
    };

    let said = report.summary();
    assert!(said.contains("Errors (20)"), "{said}");
    assert!(said.contains("… and 4 more"), "{said}");
    assert!(said.contains("Chapters without a start page: 1"), "{said}");
}

// ------------------------------------------------------------------- the cache

#[test]
fn a_cache_with_no_budget_or_no_folder_is_left_alone() {
    use leaf_server::api::cache_budget::enforce;
    let dir = tempfile::tempdir().unwrap();
    std::fs::write(dir.path().join("a"), vec![0u8; 1024]).unwrap();

    enforce(dir.path(), 0);
    enforce(Path::new("/no/such/cache"), 4096);
    // Nothing was asked for, so nothing was taken.
    assert!(dir.path().join("a").is_file());
}

#[test]
fn a_cache_already_under_its_ceiling_keeps_everything() {
    use leaf_server::api::cache_budget::enforce;
    let dir = tempfile::tempdir().unwrap();
    std::fs::write(dir.path().join("a"), vec![0u8; 100]).unwrap();
    enforce(dir.path(), 1024 * 1024);
    assert!(dir.path().join("a").is_file());
}

#[test]
fn a_cache_that_cannot_be_read_is_said_and_left() {
    use leaf_server::api::cache_budget::enforce;
    let dir = tempfile::tempdir().unwrap();
    let cache = dir.path().join("cache");
    std::fs::create_dir(&cache).unwrap();
    read_only(&cache);
    // It exists and cannot be walked: said out loud rather than treated as empty, which
    // would look exactly like a cache that fits.
    enforce(&cache, 1);
    writable(&cache);
}

// ------------------------------------------------------------ which chapter

#[test]
fn a_volume_with_one_marker_and_no_start_page_answers_that_marker() {
    use leaf_server::api::dto::ChapterDto;
    use leaf_server::api::progress::chapter_at_page;

    let one = ChapterDto {
        id: "c1".into(),
        raw: "Chapitre 45.5".into(),
        label: "Chapitre 45.5".into(),
        number: Some(45.5),
        title: None,
        kind: "CHAPTER".into(),
        position: 1,
        start_page: None,
        entry_id: "v1".into(),
    };
    // A standalone chapter entry is one marker with no start page: it is the answer, on
    // every page of it. Treating the missing start page as zero used to answer "chapter 1"
    // confidently and wrongly.
    assert_eq!(
        chapter_at_page(std::slice::from_ref(&one), 0).map(|c| c.id),
        Some("c1".into())
    );
    assert_eq!(chapter_at_page(&[one], 40).map(|c| c.id), Some("c1".into()));
    assert!(chapter_at_page(&[], 0).is_none());
}

// -------------------------------------------------------- when a write cannot land

#[test]
fn a_write_that_cannot_be_renamed_into_place_leaves_nothing_beside_it() {
    // The bytes are written beside and then renamed, because a rename within a directory is
    // atomic. When the rename cannot happen the temporary must go with it, or the folder
    // fills with half-written files nobody will ever look at.
    use leaf_server::store::files::write_whole;
    let dir = tempfile::tempdir().unwrap();
    // The target is an existing, non-empty directory: writing beside it works, renaming a
    // file over it cannot.
    let occupied = dir.path().join("work.json");
    std::fs::create_dir(&occupied).unwrap();
    std::fs::write(occupied.join("inside"), b"x").unwrap();

    assert!(write_whole(&occupied, b"{}").is_err());
    let left: Vec<String> = std::fs::read_dir(dir.path())
        .unwrap()
        .flatten()
        .map(|e| e.file_name().to_string_lossy().to_string())
        .collect();
    assert_eq!(left, vec!["work.json".to_string()], "no .part left behind");
}

#[test]
fn a_sidecar_that_cannot_be_rewritten_leaves_the_archive_as_it_was() {
    // The whole archive is rewritten beside the original and swapped in. A failure halfway
    // through must take the half-written copy with it and leave the volume untouched.
    use leaf_server::archive::cbz_writer::replace_sidecar;
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("Tome 1.cbz");
    std::fs::write(&path, b"not a zip at all").unwrap();

    assert!(replace_sidecar(&path, "entry.json", b"{\"leaf\":1}").is_err());
    assert_eq!(std::fs::read(&path).unwrap(), b"not a zip at all");
    let left: Vec<String> = std::fs::read_dir(dir.path())
        .unwrap()
        .flatten()
        .map(|e| e.file_name().to_string_lossy().to_string())
        .collect();
    assert_eq!(
        left,
        vec!["Tome 1.cbz".to_string()],
        "no temporary left behind"
    );
}

#[test]
fn an_inbox_on_another_filesystem_copies_instead_of_renaming() {
    // Committing an import is a rename, instant and atomic. Across two volumes it becomes a
    // multi-gigabyte copy — which is why the server warns about it at startup, and why it
    // still has to work when somebody sets it up that way anyway.
    use leaf_server::api::intake::move_or_copy;

    let shm = std::path::Path::new("/dev/shm");
    if !shm.is_dir() {
        return; // no second filesystem to hand: the rename path is covered elsewhere
    }
    let source = shm.join(format!("leaf-test-{}.cbz", std::process::id()));
    std::fs::write(&source, b"nine gigabytes, in spirit").unwrap();

    let dir = tempfile::tempdir().unwrap();
    let target = dir.path().join("Tome 1.cbz");
    move_or_copy(&source, &target).expect("across two volumes");

    assert_eq!(
        std::fs::read(&target).unwrap(),
        b"nine gigabytes, in spirit"
    );
    assert!(
        !source.exists(),
        "the original goes, as a rename would have taken it"
    );
}

// ------------------------------------------------------------------ the runner

#[test]
fn a_scan_that_fails_says_why_rather_than_only_writing_it_down() {
    // A scan that quietly did nothing is worse than one that says why: the failure has to
    // reach /scan, not only the log.
    use leaf_server::scan::runner::ScanRunner;
    use std::sync::Arc;
    use std::time::{Duration, Instant};

    let runner = Arc::new(ScanRunner::default());
    assert!(runner.start("Essai", || {
        Err(anyhow::anyhow!("the library is not there"))
    }));

    let deadline = Instant::now() + Duration::from_secs(30);
    loop {
        let status = runner.status();
        if status.state == "DONE" {
            let said = status.summary.unwrap_or_default();
            assert!(said.contains("failed:"), "{said}");
            assert!(said.contains("the library is not there"), "{said}");
            return;
        }
        assert!(Instant::now() < deadline, "the scan never finished");
        std::thread::sleep(Duration::from_millis(20));
    }
}

#[test]
fn a_scan_asked_for_twice_only_starts_once() {
    use leaf_server::scan::runner::ScanRunner;
    use std::sync::Arc;

    let runner = Arc::new(ScanRunner::default());
    let (gate, wait) = std::sync::mpsc::channel::<()>();
    let started = runner.start("Long", move || {
        let _ = wait.recv();
        Ok(Default::default())
    });
    assert!(started);
    // While it runs, a second is refused rather than queued behind it.
    assert!(!runner.start("Encore", || Ok(Default::default())));
    let _ = gate.send(());
}

// -------------------------------------------------------- is it the same volume

#[test]
fn two_files_carrying_one_id_are_the_same_volume_whatever_else_they_say() {
    use leaf_server::api::intake::same_volume;
    use leaf_server::metadata::sidecars::EntryJson;

    let stamped = |id: &str, title: &str| EntryJson {
        leaf: Some(1),
        id: Some(id.into()),
        work: Some("Bleach".into()),
        title: Some(title.into()),
        ..Default::default()
    };
    // The stamp is the identity: it is put there so a file that leaves can find its way
    // home, and a title edited on the way out changes nothing about which volume it is.
    assert!(same_volume(
        Some(&stamped("v1", "Tome 1")),
        Some(&stamped("v1", "Autre"))
    ));
    assert!(!same_volume(
        Some(&stamped("v1", "Tome 1")),
        Some(&stamped("v2", "Tome 1"))
    ));
}

#[test]
fn a_file_that_names_no_work_cannot_be_shown_to_be_the_same_as_anything() {
    use leaf_server::api::intake::same_volume;
    use leaf_server::metadata::sidecars::EntryJson;

    // Silence is never a match. The answer to "I cannot tell" is to keep both, not to write
    // one over the other and find out afterwards.
    let quiet = EntryJson {
        leaf: Some(1),
        number: Some(1.0),
        ..Default::default()
    };
    let named = EntryJson {
        leaf: Some(1),
        work: Some("Bleach".into()),
        number: Some(1.0),
        ..Default::default()
    };
    assert!(!same_volume(Some(&quiet), Some(&named)));
    assert!(!same_volume(Some(&quiet), Some(&quiet)));
    assert!(!same_volume(None, Some(&named)));
    assert!(!same_volume(Some(&named), None));
}

// ---------------------------------------------------------------- odds and ends

#[test]
fn a_universe_whose_name_is_only_punctuation_hides_nothing() {
    use leaf_server::store::text::composed_name;
    // The needle folds to nothing. An empty needle would match every title rather than
    // none, so a universe called "···" must not swallow the whole shelf.
    assert_eq!(composed_name(Some("···"), "Bleach", None), "··· · Bleach");
    assert_eq!(
        composed_name(Some("Terres d'Arran"), "Elfes", None),
        "Terres d'Arran · Elfes"
    );
    // The universe is dropped when the work already repeats it.
    assert_eq!(
        composed_name(Some("Bleach"), "Bleach", Some("Poche")),
        "Bleach · Poche"
    );
}

#[test]
fn a_document_that_is_not_xml_at_all_is_nothing() {
    use leaf_server::metadata::legacy_comic_info::read;
    // Not "half a document": a prefix that parses and then stops is exactly what a sidecar
    // being written by an edit looks like from a scan reading it at the same moment.
    assert!(read(b"<ComicInfo><Title>Bleach</Title></Comic Info>").is_none());
    assert!(read(b"<<<>>>").is_none());
    assert!(read(b"<ComicInfo attr=unquoted></ComicInfo>").is_none());
}

#[test]
fn an_archive_that_cannot_be_opened_says_which_archive_and_which_member() {
    use leaf_server::archive::cbz::extract;
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("Tome 1.cbz");
    // A zip whose directory is corrupt: not "the member is not there", which is an answer,
    // but "this is not an archive", which is a failure worth naming.
    std::fs::write(&path, b"PK\x03\x04 and then nothing that follows").unwrap();
    let refused = extract(&path, "entry.json").unwrap_err().to_string();
    assert!(refused.contains("Tome 1.cbz"), "{refused}");
}

#[test]
fn a_cache_folder_that_cannot_be_walked_is_said_and_left_alone() {
    use leaf_server::api::cache_budget::enforce;
    let dir = tempfile::tempdir().unwrap();
    let cache = dir.path().join("cache");
    std::fs::create_dir(&cache).unwrap();
    std::fs::write(cache.join("a"), vec![0u8; 4096]).unwrap();
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        std::fs::set_permissions(&cache, std::fs::Permissions::from_mode(0o000)).unwrap();
        // It exists and cannot be read: said out loud rather than treated as empty, which
        // would look exactly like a cache that already fits.
        enforce(&cache, 1);
        std::fs::set_permissions(&cache, std::fs::Permissions::from_mode(0o755)).unwrap();
    }
    assert!(
        cache.join("a").is_file(),
        "nothing may be deleted on a guess"
    );
}
