//! Matching what a reimport brings to what the library holds, and announcing it.
//!
//! The matching first, as pure functions: the tables below are the library and the drop.
//! Then the announcement, on real archives in a real scanned library. A wrong answer here
//! is not a wrong label on a screen — the file lands on the name it is given, and landing
//! on the wrong volume's name replaces that volume.

use std::collections::HashSet;
use std::path::{Path, PathBuf};
use std::sync::Arc;

mod common;

use leaf_server::api::bulk_import::{
    ArchiveRewrite, BulkImport, ChapterSplice, Cut, ImportOpened, ImportRequest, ManifestFile,
    ManifestMember, ManifestSidecar, Matched, Scope, SpliceKind,
};
use leaf_server::api::progress::{Progress, ProgressPatch};
use leaf_server::api::reimport::{
    folded, free_name_among, match_volumes, FoundInFile, ImportTarget, Incoming, Outcome, Splice,
    TargetKind, VolumeMatch,
};
use leaf_server::api::routes::aimed_after_import;
use leaf_server::metadata::sidecars::EntryJson;
use leaf_server::scan::scanner::Scanner;
use leaf_server::store::held::Held;
use leaf_server::store::profiles::{NewProfile, Profiles, Rights};
use leaf_server::store::replaced::{self, Positions, Replaced};
use leaf_server::store::repository::Ceiling;
use leaf_server::store::repository::Repository;
use leaf_server::store::schema::DEFAULT_PROFILE;
use leaf_server::store::Db;

const FOLDER: &str = "/library/Blue Period";

fn folder() -> &'static Path {
    Path::new(FOLDER)
}

fn held(kind: &str, number: Option<f64>, file: &str, pages: u32) -> Held {
    Held {
        entry_id: format!("entry-{file}"),
        edition_id: "edition".to_string(),
        kind: kind.to_string(),
        number,
        file: PathBuf::from(FOLDER).join(file),
        size: 1000,
        pages,
        title: None,
        chapters: Vec::new(),
        page: None,
        finished: false,
        others: 0,
    }
}

fn volume(number: f64, file: &str) -> Held {
    held("VOLUME", Some(number), file, 180)
}

fn found(number: Option<f64>) -> FoundInFile {
    FoundInFile {
        number,
        ..FoundInFile::default()
    }
}

fn numbered(number: f64) -> FoundInFile {
    found(Some(number))
}

/// Every answer, for a drop the matching is expected to take.
fn match_all(
    held: &[Held],
    incoming: &[Incoming],
    folder: &Path,
    aimed: Option<&str>,
    series: &[&str],
) -> Vec<VolumeMatch> {
    match_volumes(held, incoming, folder, aimed, series).expect("a drop it takes")
}

fn arriving<'a>(path: &'a str, found: &'a FoundInFile) -> Incoming<'a> {
    Incoming {
        path,
        found: Some(found),
        identical_to: None,
    }
}

const BLUE_PERIOD: &[&str] = &["Blue Period"];

fn one(held: &[Held], incoming: &[Incoming]) -> VolumeMatch {
    let mut all = match_all(held, incoming, folder(), None, BLUE_PERIOD);
    assert_eq!(1, all.len(), "{all:?}");
    all.remove(0)
}

#[test]
fn a_volume_lands_on_the_name_of_the_one_it_replaces() {
    let tome = volume(4.0, "Tome 04.cbz");
    let four = numbered(4.0);
    let answer = one(&[tome], &[arriving("drop/blue_period_t04.cbz", &four)]);
    assert_eq!(Outcome::Replace, answer.outcome);
    assert_eq!(Some("Tome 04.cbz"), answer.lands.as_deref());
    assert_eq!(Some("entry-Tome 04.cbz"), answer.entry_id.as_deref());
    assert_eq!(
        "Tome 04.cbz",
        answer.present.expect("the volume it replaces").file
    );
    assert_eq!("drop/blue_period_t04.cbz", answer.path);
}

#[test]
fn a_loose_chapter_two_never_replaces_volume_two() {
    let tome = volume(2.0, "Tome 02.cbz");
    let chapter = FoundInFile {
        number: Some(2.0),
        kind: Some("CHAPTER".to_string()),
        ..FoundInFile::default()
    };
    let answer = one(&[tome], &[arriving("Chapitre 2.cbz", &chapter)]);
    assert_eq!(Outcome::Add, answer.outcome);
    assert_eq!(Some("Chapitre 2.cbz"), answer.lands.as_deref());
    assert_eq!(None, answer.entry_id);
    assert_eq!(None, answer.present);
}

#[test]
fn a_volume_two_never_replaces_a_loose_chapter_two() {
    let chapter = held("CHAPTER", Some(2.0), "Chapitre 2.cbz", 20);
    let two = numbered(2.0);
    let answer = one(&[chapter], &[arriving("Tome 2.cbz", &two)]);
    assert_eq!(Outcome::Add, answer.outcome);
    assert_eq!(None, answer.present);
}

#[test]
fn a_kind_the_library_does_not_hold_replaces_no_volume() {
    let tome = volume(2.0, "Tome 02.cbz");
    let bonus = FoundInFile {
        number: Some(2.0),
        kind: Some("BONUS".to_string()),
        ..FoundInFile::default()
    };
    let answer = one(&[tome], &[arriving("Bonus 2.cbz", &bonus)]);
    assert_eq!(Outcome::Add, answer.outcome);
    assert_eq!(None, answer.present);
}

#[test]
fn a_chapter_file_replaces_the_chapter_file_of_its_number() {
    let chapter = held("CHAPTER", Some(2.0), "Chapitre 2.cbz", 20);
    let two = FoundInFile {
        number: Some(2.0),
        kind: Some("chapter".to_string()),
        ..FoundInFile::default()
    };
    let answer = one(&[chapter], &[arriving("ch002.cbz", &two)]);
    assert_eq!(Outcome::Replace, answer.outcome);
    assert_eq!(Some("Chapitre 2.cbz"), answer.lands.as_deref());
}

#[test]
fn two_and_a_half_is_not_two() {
    let tome = volume(2.0, "Tome 02.cbz");
    let half = numbered(2.5);
    let answer = one(&[tome], &[arriving("Tome 2.5.cbz", &half)]);
    assert_eq!(Outcome::Add, answer.outcome);
    assert_eq!(Some("Tome 2.5.cbz"), answer.lands.as_deref());

    let half = volume(2.5, "Tome 2.5.cbz");
    let two = numbered(2.0);
    let answer = one(&[half], &[arriving("Tome 2.cbz", &two)]);
    assert_eq!(Outcome::Add, answer.outcome);
}

#[test]
fn two_files_at_one_number_replace_nothing() {
    let tome = volume(3.0, "Tome 03.cbz");
    let three = numbered(3.0);
    let all = match_all(
        &[tome],
        &[
            arriving("a/Tome 3.cbz", &three),
            arriving("b/t03.cbz", &three),
        ],
        folder(),
        None,
        BLUE_PERIOD,
    );
    assert_eq!(2, all.len());
    for answer in &all {
        assert_eq!(Outcome::Twice, answer.outcome, "{answer:?}");
        assert_eq!(None, answer.lands);
        assert_eq!(None, answer.entry_id);
    }
}

#[test]
fn two_files_at_one_number_say_twice_even_where_nothing_is_held() {
    let six = numbered(6.0);
    let all = match_all(
        &[],
        &[
            arriving("Tome 6.cbz", &six),
            arriving("Tome 6 bis.cbz", &six),
        ],
        folder(),
        None,
        BLUE_PERIOD,
    );
    assert!(all.iter().all(|m| m.outcome == Outcome::Twice), "{all:?}");
    assert!(all.iter().all(|m| m.lands.is_none()));
}

#[test]
fn a_volume_and_a_chapter_at_one_number_are_not_twice() {
    let volume_two = numbered(2.0);
    let chapter_two = FoundInFile {
        number: Some(2.0),
        kind: Some("CHAPTER".to_string()),
        ..FoundInFile::default()
    };
    let all = match_all(
        &[],
        &[
            arriving("Tome 2.cbz", &volume_two),
            arriving("Chapitre 2.cbz", &chapter_two),
        ],
        folder(),
        None,
        BLUE_PERIOD,
    );
    assert!(all.iter().all(|m| m.outcome == Outcome::Add), "{all:?}");
}

#[test]
fn a_file_without_a_number_lands_nowhere() {
    let tome = volume(1.0, "Tome 01.cbz");
    let none = found(None);
    let answer = one(std::slice::from_ref(&tome), &[arriving("Bonus.cbz", &none)]);
    assert_eq!(Outcome::Unnumbered, answer.outcome);
    assert_eq!(None, answer.lands);

    // Nothing read at all is no number either.
    let answer = one(
        &[tome],
        &[Incoming {
            path: "Tome 01.cbz",
            found: None,
            identical_to: None,
        }],
    );
    assert_eq!(Outcome::Unnumbered, answer.outcome);
    assert_eq!(None, answer.lands);
}

#[test]
fn a_number_that_is_no_number_lands_nowhere() {
    let tome = volume(1.0, "Tome 01.cbz");
    let nan = numbered(f64::NAN);
    let answer = one(&[tome], &[arriving("Tome 01.cbz", &nan)]);
    assert_eq!(Outcome::Unnumbered, answer.outcome);
    assert_eq!(None, answer.lands);
}

#[test]
fn the_same_pages_with_other_details_is_an_update() {
    let tome = volume(7.0, "Tome 07.cbz");
    let seven = FoundInFile {
        number: Some(7.0),
        pages: Some(180),
        title: Some("Le tableau".to_string()),
        ..FoundInFile::default()
    };
    let answer = one(&[tome], &[arriving("t07.cbz", &seven)]);
    assert_eq!(Outcome::Update, answer.outcome);
    assert_eq!(Some("Tome 07.cbz"), answer.lands.as_deref());
}

#[test]
fn other_pages_are_a_replacement() {
    let tome = volume(4.0, "Tome 04.cbz");
    let four = FoundInFile {
        number: Some(4.0),
        pages: Some(184),
        ..FoundInFile::default()
    };
    let answer = one(&[tome], &[arriving("t04.cbz", &four)]);
    assert_eq!(Outcome::Replace, answer.outcome);
}

#[test]
fn pages_nobody_counted_are_a_replacement_not_an_update() {
    let tome = volume(4.0, "Tome 04.cbz");
    let four = numbered(4.0);
    let answer = one(&[tome], &[arriving("t04.cbz", &four)]);
    assert_eq!(Outcome::Replace, answer.outcome);
}

#[test]
fn a_volume_already_home_is_the_same_whatever_its_name() {
    let tome = volume(5.0, "Tome 05.cbz");
    let five = FoundInFile {
        number: Some(5.0),
        pages: Some(12),
        ..FoundInFile::default()
    };
    let answer = one(
        &[tome],
        &[Incoming {
            path: "Blue Period - 05 (scan).cbz",
            found: Some(&five),
            identical_to: Some("entry-Tome 05.cbz"),
        }],
    );
    assert_eq!(Outcome::Same, answer.outcome);
    assert_eq!(Some("Tome 05.cbz"), answer.lands.as_deref());
}

#[test]
fn an_added_volume_whose_name_is_taken_gets_a_free_one() {
    let tome = volume(2.0, "Tome 5.cbz");
    let five = numbered(5.0);
    let answer = one(&[tome], &[arriving("Tome 5.cbz", &five)]);
    assert_eq!(Outcome::Add, answer.outcome);
    assert_eq!(Some("Tome 5 (2).cbz"), answer.lands.as_deref());
}

#[test]
fn two_additions_never_share_a_free_name() {
    let tome = volume(2.0, "Tome 5.cbz");
    let five = numbered(5.0);
    let six = numbered(6.0);
    // Announced in the reverse of their paths' order: names are handed out by path, so the
    // same drop gets the same names whatever order the client listed it in.
    let all = match_all(
        &[tome],
        &[
            arriving("b/Tome 5.cbz", &six),
            arriving("a/Tome 5.cbz", &five),
        ],
        folder(),
        None,
        BLUE_PERIOD,
    );
    assert_eq!("b/Tome 5.cbz", all[0].path);
    assert_eq!(Some("Tome 5 (3).cbz"), all[0].lands.as_deref());
    assert_eq!("a/Tome 5.cbz", all[1].path);
    assert_eq!(Some("Tome 5 (2).cbz"), all[1].lands.as_deref());
}

#[test]
fn an_addition_never_takes_the_name_a_replacement_lands_on() {
    // « Tome 3.cbz » arrives as volume 9, while another file replaces the volume 3 held
    // under that very name.
    let tome = volume(3.0, "Tome 3.cbz");
    let three = numbered(3.0);
    let nine = numbered(9.0);
    let all = match_all(
        &[tome],
        &[arriving("t03.cbz", &three), arriving("Tome 3.cbz", &nine)],
        folder(),
        None,
        BLUE_PERIOD,
    );
    assert_eq!(Some("Tome 3.cbz"), all[0].lands.as_deref());
    assert_eq!(Outcome::Replace, all[0].outcome);
    assert_eq!(Some("Tome 3 (2).cbz"), all[1].lands.as_deref());
    assert_eq!(Outcome::Add, all[1].outcome);
}

#[test]
fn every_landing_of_a_large_drop_is_its_own() {
    // Twelve held under « Tome NN.cbz », and twenty arriving, every one under the same
    // name, numbered 1 to 20: twelve replace, eight are added, and no two land together.
    let held: Vec<Held> = (1..=12)
        .map(|n| volume(f64::from(n), &format!("Tome {n:02}.cbz")))
        .collect();
    let numbers: Vec<FoundInFile> = (1..=20).map(|n| numbered(f64::from(n))).collect();
    let paths: Vec<String> = (1..=20).map(|n| format!("{n:02}/Tome 01.cbz")).collect();
    let incoming: Vec<Incoming> = paths
        .iter()
        .zip(&numbers)
        .map(|(path, found)| arriving(path, found))
        .collect();
    let all = match_all(&held, &incoming, folder(), None, BLUE_PERIOD);
    let landings: HashSet<&str> = all.iter().filter_map(|m| m.lands.as_deref()).collect();
    assert_eq!(20, landings.len(), "{all:?}");
    for (n, answer) in (1..=20).zip(&all) {
        if n <= 12 {
            assert_eq!(Outcome::Replace, answer.outcome);
            assert_eq!(Some(format!("Tome {n:02}.cbz")), answer.lands);
        } else {
            assert_eq!(Outcome::Add, answer.outcome);
            assert!(answer.lands.as_deref().unwrap().starts_with("Tome 01 ("));
        }
    }
}

#[test]
fn the_file_of_volume_five_on_volume_two_is_another_volume() {
    let two = volume(2.0, "Tome 02.cbz");
    let five = numbered(5.0);
    let all = match_all(
        &[two],
        &[arriving("Tome 05.cbz", &five)],
        folder(),
        Some("entry-Tome 02.cbz"),
        BLUE_PERIOD,
    );
    let answer = &all[0];
    assert_eq!(Outcome::OtherNumber, answer.outcome);
    assert_eq!(Some("entry-Tome 02.cbz"), answer.wanted.as_deref());
    assert_eq!(Some("Tome 05.cbz"), answer.lands.as_deref());
    assert_eq!(None, answer.present);
}

#[test]
fn the_file_of_the_volume_aimed_at_is_no_other_volume() {
    let two = volume(2.0, "Tome 02.cbz");
    let found = numbered(2.0);
    let all = match_all(
        &[two],
        &[arriving("t02.cbz", &found)],
        folder(),
        Some("entry-Tome 02.cbz"),
        BLUE_PERIOD,
    );
    assert_eq!(Outcome::Replace, all[0].outcome);
    assert_eq!(None, all[0].wanted);
}

#[test]
fn a_file_naming_another_series_is_said_louder() {
    let two = volume(2.0, "Tome 02.cbz");
    let giant = FoundInFile {
        number: Some(5.0),
        series: Some("Blue Giant".to_string()),
        ..FoundInFile::default()
    };
    // Louder than being another volume: the volume aimed at is 2, the file says 5, and it
    // is not even of this series.
    let all = match_all(
        std::slice::from_ref(&two),
        &[arriving("Blue Giant 05.cbz", &giant)],
        folder(),
        Some("entry-Tome 02.cbz"),
        BLUE_PERIOD,
    );
    assert_eq!(Outcome::OtherSeries, all[0].outcome);
    assert_eq!(Some("Blue Giant 05.cbz"), all[0].lands.as_deref());

    let shouted = FoundInFile {
        number: Some(2.0),
        series: Some("  BLUE   PÉRIOD ".to_string()),
        ..FoundInFile::default()
    };
    let answer = one(std::slice::from_ref(&two), &[arriving("t02.cbz", &shouted)]);
    assert_eq!(Outcome::Replace, answer.outcome);
}

#[test]
fn a_series_named_by_its_title_rather_than_its_folder_is_this_one() {
    let two = volume(2.0, "Tome 02.cbz");
    let titled = FoundInFile {
        number: Some(2.0),
        series: Some("ブルーピリオド".to_string()),
        ..FoundInFile::default()
    };
    let all = match_all(
        &[two],
        &[arriving("t02.cbz", &titled)],
        folder(),
        None,
        &["Blue Period", "ブルーピリオド"],
    );
    assert_eq!(Outcome::Replace, all[0].outcome);
}

#[test]
fn a_named_series_with_nothing_to_compare_it_with_is_another() {
    let giant = FoundInFile {
        number: Some(1.0),
        series: Some("Blue Giant".to_string()),
        ..FoundInFile::default()
    };
    let all = match_all(&[], &[arriving("t01.cbz", &giant)], folder(), None, &[]);
    assert_eq!(Outcome::OtherSeries, all[0].outcome);
}

#[test]
fn what_differs_is_named_field_by_field() {
    let mut tome = volume(4.0, "Tome 04.cbz");
    tome.title = Some("Le concours".to_string());
    tome.chapters = vec![13.0, 14.0, 15.0];

    let all_three = FoundInFile {
        number: Some(4.0),
        pages: Some(190),
        title: Some("Le Concours".to_string()),
        chapters: vec![13.0, 14.0, 15.0, 16.0],
        ..FoundInFile::default()
    };
    let answer = one(
        std::slice::from_ref(&tome),
        &[arriving("t04.cbz", &all_three)],
    );
    assert_eq!(vec!["pages", "title", "chapters"], answer.differs);
    assert_eq!(Some(190), answer.arriving.pages);
    assert_eq!(Some("Le Concours"), answer.arriving.title.as_deref());
    assert_eq!(vec![13.0, 14.0, 15.0, 16.0], answer.arriving.chapters);

    let alike = FoundInFile {
        number: Some(4.0),
        pages: Some(180),
        title: Some("Le concours".to_string()),
        chapters: vec![15.0, 13.0, 14.0],
        ..FoundInFile::default()
    };
    let answer = one(std::slice::from_ref(&tome), &[arriving("t04.cbz", &alike)]);
    assert!(answer.differs.is_empty(), "{:?}", answer.differs);

    // What the file does not say is not a difference.
    let silent = numbered(4.0);
    let answer = one(&[tome], &[arriving("t04.cbz", &silent)]);
    assert!(answer.differs.is_empty(), "{:?}", answer.differs);
}

#[test]
fn a_title_given_to_an_untitled_volume_differs() {
    let tome = volume(4.0, "Tome 04.cbz");
    let titled = FoundInFile {
        number: Some(4.0),
        title: Some("Le concours".to_string()),
        ..FoundInFile::default()
    };
    let answer = one(&[tome], &[arriving("t04.cbz", &titled)]);
    assert_eq!(vec!["title"], answer.differs);
}

#[test]
fn an_addition_has_nothing_to_differ_from() {
    let five = FoundInFile {
        number: Some(5.0),
        pages: Some(200),
        title: Some("Nouveau".to_string()),
        ..FoundInFile::default()
    };
    let answer = one(&[], &[arriving("t05.cbz", &five)]);
    assert_eq!(Outcome::Add, answer.outcome);
    assert!(answer.differs.is_empty());
}

#[test]
fn a_volume_held_twice_at_one_number_is_replaced_by_neither() {
    let first = volume(8.0, "Tome 08.cbz");
    let second = volume(8.0, "Tome 08 (2).cbz");
    let eight = numbered(8.0);
    let answer = one(&[first, second], &[arriving("t08.cbz", &eight)]);
    assert_eq!(Outcome::Add, answer.outcome);
    assert_eq!(None, answer.entry_id);
    assert_eq!(Some("t08.cbz"), answer.lands.as_deref());
}

#[test]
fn a_free_name_is_the_one_wanted_when_nothing_holds_it() {
    let taken = HashSet::new();
    assert_eq!(
        "Tome 5.cbz",
        free_name_among(folder(), "Tome 5.cbz", &taken)
    );
}

#[test]
fn a_free_name_steps_past_every_name_taken() {
    let taken: HashSet<String> = ["Tome 5.cbz", "Tome 5 (2).cbz", "Tome 5 (3).cbz"]
        .into_iter()
        .map(String::from)
        .collect();
    assert_eq!(
        "Tome 5 (4).cbz",
        free_name_among(folder(), "Tome 5.cbz", &taken)
    );
}

#[test]
fn a_free_name_steps_past_a_file_on_disk_the_list_does_not_know() {
    let dir = tempfile::tempdir().unwrap();
    std::fs::write(dir.path().join("Tome 5.cbz"), b"x").unwrap();
    let taken: HashSet<String> = std::iter::once("Tome 5 (2).cbz".to_string()).collect();
    assert_eq!(
        "Tome 5 (3).cbz",
        free_name_among(dir.path(), "Tome 5.cbz", &taken)
    );
}

#[test]
fn a_free_name_without_an_extension_keeps_none() {
    let taken: HashSet<String> = std::iter::once("Notes".to_string()).collect();
    assert_eq!("Notes (2)", free_name_among(folder(), "Notes", &taken));
}

#[test]
fn folding_forgets_case_accents_and_extra_spaces() {
    for (written, plain) in [
        ("Blue Period", "blue period"),
        ("  BLUE   PÉRIOD ", "blue period"),
        ("Éclat\tde\nlune", "eclat de lune"),
        ("ＢＬＵＥ　ＰＥＲＩＯＤ", "blue period"),
        ("L\u{2019}Attaque des Titans", "l'attaque des titans"),
    ] {
        assert_eq!(plain, folded(written), "{written:?}");
    }
    assert_ne!(folded("Blue Period"), folded("Blue Giant"));
}

#[test]
fn folding_keeps_the_punctuation_that_tells_two_series_apart() {
    // Each pair is two series, or two spellings a scanlation and a publisher do not share;
    // a fold that dropped punctuation made each pair one name and let one series' volume
    // into the other without a word.
    for (one, other) in [
        ("Re:Zero", "Re Zero"),
        ("Gintama'", "Gintama"),
        ("Durarara!!", "Durarara"),
        ("Blue Period.", "Blue Period"),
    ] {
        assert_ne!(folded(one), folded(other), "{one:?} against {other:?}");
    }
}

#[test]
fn folding_keeps_the_marks_that_make_another_kana() {
    // バ, パ and ハ are three letters, not one letter with accents: the voicing marks are
    // part of the word, and « ブルーピリオド » folded without them is another title.
    let (ba, pa, ha) = (folded("バ"), folded("パ"), folded("ハ"));
    assert_ne!(ba, pa);
    assert_ne!(ba, ha);
    assert_ne!(pa, ha);
    assert_eq!("ブルーピリオド", folded("ブルーピリオド"));
    // Written decomposed, as some file systems hand names over, it is still the same title.
    assert_eq!(
        folded("ブルーピリオド"),
        folded("フ\u{3099}ルーヒ\u{309A}リオト\u{3099}")
    );
    assert_ne!(folded("ブルーピリオド"), folded("ブルージャイアント"));
}

fn identical<'a>(path: &'a str, found: &'a FoundInFile, entry: &'a str) -> Incoming<'a> {
    Incoming {
        path,
        found: Some(found),
        identical_to: Some(entry),
    }
}

#[test]
fn an_identical_file_whose_number_matches_nothing_is_not_added_beside_itself() {
    // The file is volume 5 byte for byte, but what was read in it says 9: paired by number
    // it was an addition, and volume 5 would have been in the library twice, the second
    // copy as « Tome 05 (2).cbz ».
    let tome = volume(5.0, "Tome 05.cbz");
    let nine = numbered(9.0);
    let answer = one(
        &[tome],
        &[identical("Tome 05.cbz", &nine, "entry-Tome 05.cbz")],
    );
    assert_eq!(Outcome::Same, answer.outcome);
    assert_eq!(Some("Tome 05.cbz"), answer.lands.as_deref());
    assert_eq!(Some("entry-Tome 05.cbz"), answer.entry_id.as_deref());
}

#[test]
fn an_identical_file_is_the_same_even_aimed_at_another_volume() {
    let two = volume(2.0, "Tome 02.cbz");
    let five = volume(5.0, "Tome 05.cbz");
    let found = numbered(5.0);
    let all = match_all(
        &[two, five],
        &[identical("t05.cbz", &found, "entry-Tome 05.cbz")],
        folder(),
        Some("entry-Tome 02.cbz"),
        BLUE_PERIOD,
    );
    assert_eq!(Outcome::Same, all[0].outcome);
    assert_eq!(None, all[0].wanted);
}

#[test]
fn an_identical_file_is_the_same_whatever_series_it_names() {
    // A volume converted from a ComicInfo that names the series as another catalogue
    // spells it: it is the file that is there, and nothing is to be installed.
    let tome = volume(3.0, "Tome 03.cbz");
    let found = FoundInFile {
        number: Some(3.0),
        series: Some("Blue Period (2020)".to_string()),
        ..FoundInFile::default()
    };
    let answer = one(
        &[tome],
        &[identical("t03.cbz", &found, "entry-Tome 03.cbz")],
    );
    assert_eq!(Outcome::Same, answer.outcome);
}

#[test]
fn an_identical_file_of_a_volume_held_elsewhere_lands_nowhere() {
    let found = numbered(3.0);
    let answer = one(&[], &[identical("t03.cbz", &found, "entry-elsewhere")]);
    assert_eq!(Outcome::Same, answer.outcome);
    assert_eq!(None, answer.lands);
    assert_eq!(Some("entry-elsewhere"), answer.entry_id.as_deref());
}

#[test]
fn an_identical_file_leaves_another_of_its_number_in_doubt() {
    // The drop holds volume 3 as it is and another file numbered 3: which of the two is
    // the volume is a question, not a replacement.
    let tome = volume(3.0, "Tome 03.cbz");
    let three = numbered(3.0);
    let all = match_all(
        &[tome],
        &[
            identical("Tome 03.cbz", &three, "entry-Tome 03.cbz"),
            arriving("other/t03.cbz", &three),
        ],
        folder(),
        None,
        BLUE_PERIOD,
    );
    assert_eq!(Outcome::Same, all[0].outcome);
    assert_eq!(Outcome::Twice, all[1].outcome);
    assert_eq!(None, all[1].lands);
}

#[test]
fn a_held_file_outside_its_folder_is_refused_rather_than_landed_at_the_root() {
    // Landing on its bare name put the replacement beside the edition's other files, left
    // the old one where it was, and lost every reader's place with the entry.
    let mut stray = volume(4.0, "Tome 04.cbz");
    stray.file = PathBuf::from("/elsewhere/Tome 04.cbz");
    let four = numbered(4.0);
    let refused = match_volumes(
        &[stray],
        &[arriving("t04.cbz", &four)],
        folder(),
        None,
        BLUE_PERIOD,
    );
    assert!(refused.is_err());
}

#[test]
fn a_held_file_in_a_subfolder_is_replaced_where_it_is() {
    let tome = volume(3.0, "Extra/Tome 03.cbz");
    let three = numbered(3.0);
    let answer = one(&[tome], &[arriving("t03.cbz", &three)]);
    assert_eq!(Outcome::Replace, answer.outcome);
    assert_eq!(Some("Extra/Tome 03.cbz"), answer.lands.as_deref());
    assert_eq!("Extra/Tome 03.cbz", answer.present.unwrap().file);
}

#[test]
fn a_volume_held_twice_is_replaced_where_the_reimport_aims() {
    let first = volume(8.0, "Tome 08.cbz");
    let second = volume(8.0, "Tome 08 (2).cbz");
    let eight = numbered(8.0);
    let all = match_all(
        &[first, second],
        &[arriving("t08.cbz", &eight)],
        folder(),
        Some("entry-Tome 08 (2).cbz"),
        BLUE_PERIOD,
    );
    assert_eq!(Outcome::Replace, all[0].outcome);
    assert_eq!(Some("Tome 08 (2).cbz"), all[0].lands.as_deref());
    assert_eq!(None, all[0].wanted);
}

#[test]
fn two_files_at_a_number_held_twice_replace_nothing() {
    let first = volume(8.0, "Tome 08.cbz");
    let second = volume(8.0, "Tome 08 (2).cbz");
    let eight = numbered(8.0);
    let all = match_all(
        &[first, second],
        &[arriving("a/t08.cbz", &eight), arriving("b/t08.cbz", &eight)],
        folder(),
        None,
        BLUE_PERIOD,
    );
    assert!(all.iter().all(|m| m.outcome == Outcome::Twice), "{all:?}");
    assert!(all.iter().all(|m| m.lands.is_none()));
}

#[test]
fn an_empty_kind_replaces_no_volume() {
    let tome = volume(2.0, "Tome 02.cbz");
    let blank = FoundInFile {
        number: Some(2.0),
        kind: Some(String::new()),
        ..FoundInFile::default()
    };
    let answer = one(&[tome], &[arriving("t02.cbz", &blank)]);
    assert_eq!(Outcome::Add, answer.outcome);
    assert_eq!(None, answer.present);
}

#[test]
fn a_series_name_with_nothing_in_it_says_no_other_series() {
    let tome = volume(2.0, "Tome 02.cbz");
    for said in ["", "   ", "\t\n"] {
        let found = FoundInFile {
            number: Some(2.0),
            series: Some(said.to_string()),
            ..FoundInFile::default()
        };
        let answer = one(std::slice::from_ref(&tome), &[arriving("t02.cbz", &found)]);
        assert_eq!(Outcome::Replace, answer.outcome, "{said:?}");
    }
}

#[test]
fn a_number_guessed_from_a_name_lands_nowhere_until_confirmed() {
    // « Tome 2 - Chapitre 14.cbz » read off its name is volume 2 or chapter 14; landing on
    // volume 2 on that guess would overwrite it.
    let tome = volume(2.0, "Tome 02.cbz");
    let guessed = FoundInFile {
        number: Some(2.0),
        guessed: true,
        ..FoundInFile::default()
    };
    let answer = one(&[tome], &[arriving("Tome 2 - Chapitre 14.cbz", &guessed)]);
    assert_eq!(Outcome::Unnumbered, answer.outcome);
    assert_eq!(None, answer.lands);
    assert_eq!(None, answer.present);
    assert_eq!(Some(2.0), answer.number);
}

#[test]
fn a_name_taken_in_another_case_is_taken() {
    // On a disk that does not tell case apart, « Tome 5.cbz » beside « tome 5.cbz » is the
    // same file, and the addition was written over the volume.
    let tome = volume(2.0, "tome 5.cbz");
    let five = numbered(5.0);
    let answer = one(&[tome], &[arriving("Tome 5.cbz", &five)]);
    assert_eq!(Some("Tome 5 (2).cbz"), answer.lands.as_deref());
}

#[test]
fn a_name_taken_in_another_unicode_form_is_taken() {
    // « Étoile » composed and « E » followed by its accent are one name to a person and
    // to some file systems, and two strings to a comparison.
    let tome = volume(2.0, "\u{c9}toile.cbz");
    let five = numbered(5.0);
    let answer = one(&[tome], &[arriving("E\u{301}toile.cbz", &five)]);
    assert_eq!(Some("E\u{301}toile (2).cbz"), answer.lands.as_deref());
}

#[test]
fn a_free_name_steps_past_a_file_on_disk_in_another_case() {
    let dir = tempfile::tempdir().unwrap();
    std::fs::write(dir.path().join("tome 5.cbz"), b"x").unwrap();
    assert_eq!(
        "Tome 5 (2).cbz",
        free_name_among(dir.path(), "Tome 5.cbz", &HashSet::new())
    );
}

#[test]
fn a_path_without_a_file_name_is_refused() {
    let five = numbered(5.0);
    for path in ["", "..", "a/.."] {
        let refused = match_volumes(&[], &[arriving(path, &five)], folder(), None, BLUE_PERIOD)
            .expect_err(path);
        assert!(
            refused
                .downcast_ref::<leaf_server::api::Invalid>()
                .is_some(),
            "{path:?}: {refused}"
        );
    }
}

#[test]
fn what_is_not_known_is_left_out_of_the_answer_rather_than_sent_as_null() {
    let none = found(None);
    let answer = one(&[], &[arriving("Bonus.cbz", &none)]);
    let json = serde_json::to_value(&answer).unwrap();
    let object = json.as_object().unwrap();
    for absent in ["lands", "number", "entryId", "wanted", "present"] {
        assert!(!object.contains_key(absent), "{absent} in {json}");
    }
    assert_eq!("UNNUMBERED", object["outcome"]);
    let arriving = object["arriving"].as_object().unwrap();
    assert!(!arriving.contains_key("pages") && !arriving.contains_key("title"));
}

#[test]
fn a_found_file_says_whether_its_number_was_guessed() {
    let read: FoundInFile = serde_json::from_str(r#"{"number": 2, "guessed": true}"#).unwrap();
    assert!(read.guessed);
    let read: FoundInFile = serde_json::from_str(r#"{"number": 2}"#).unwrap();
    assert!(!read.guessed);
}

#[test]
fn a_copy_of_another_volume_puts_no_doubt_on_the_number_it_announces() {
    // The drop holds volume 5 byte for byte, its metadata saying 9, and a real new volume
    // 9. Counted under the 9 it announced, the copy made the real one « twice » and kept
    // it from replacing the volume 9 held.
    let five = volume(5.0, "Tome 05.cbz");
    let nine = volume(9.0, "Tome 09.cbz");
    let says_nine = numbered(9.0);
    let all = match_all(
        &[five, nine],
        &[
            identical("copy/Tome 05.cbz", &says_nine, "entry-Tome 05.cbz"),
            arriving("new/t09.cbz", &says_nine),
        ],
        folder(),
        None,
        BLUE_PERIOD,
    );
    assert_eq!(Outcome::Same, all[0].outcome);
    assert_eq!(Outcome::Replace, all[1].outcome, "{:?}", all[1]);
    assert_eq!(Some("Tome 09.cbz"), all[1].lands.as_deref());
}

#[test]
fn a_copy_of_a_volume_held_elsewhere_puts_no_doubt_on_any_number() {
    let nine = volume(9.0, "Tome 09.cbz");
    let says_nine = numbered(9.0);
    let all = match_all(
        &[nine],
        &[
            identical("copy.cbz", &says_nine, "entry-elsewhere"),
            arriving("t09.cbz", &says_nine),
        ],
        folder(),
        None,
        BLUE_PERIOD,
    );
    assert_eq!(Outcome::Same, all[0].outcome);
    assert_eq!(Outcome::Replace, all[1].outcome, "{:?}", all[1]);
}

#[test]
fn a_spacing_voicing_mark_is_folded_onto_its_kana() {
    // NFKC turns ゛ written on its own into a space and the combining mark: « ハ゛ » folded
    // to « ハ ゙ », a stray space and a mark on nothing, and was not « バ ».
    assert_eq!(folded("バ"), folded("ハ\u{309B}"));
    assert_eq!(folded("パ"), folded("ハ\u{309C}"));
    assert_eq!("ハ", folded("\u{309B}ハ"));
}

#[test]
fn a_dotted_capital_i_folds_to_a_plain_i() {
    // « İ » lowercases to « i » and a combining dot: whatever order the fold does its
    // steps in, the dot must not survive it.
    assert_eq!(folded("Istanbul"), folded("İstanbul"));
}

// ------------------------------------------------------------------ announcing a reimport

const SENT_CEILING: u64 = 64 * 1024 * 1024;

struct Library {
    dir: tempfile::TempDir,
    db: Arc<Db>,
}

impl Library {
    fn new() -> Self {
        let dir = tempfile::tempdir().unwrap();
        std::fs::create_dir_all(dir.path().join("library")).unwrap();
        std::fs::create_dir_all(dir.path().join("inbox")).unwrap();
        let db = Arc::new(Db::open(&dir.path().join("index.sqlite")).unwrap());
        Library { dir, db }
    }

    fn root(&self) -> PathBuf {
        self.dir.path().join("library")
    }

    fn bulk(&self) -> BulkImport {
        BulkImport::new(
            &self.dir.path().join("inbox"),
            &self.root(),
            Arc::clone(&self.db),
        )
    }

    fn scan(&self) {
        Scanner::new(Arc::clone(&self.db), true)
            .scan(&[self.root()])
            .expect("scanning");
    }

    fn id(&self, sql: &str) -> String {
        self.db
            .read(|cx| cx.query_one(sql, [], |r| r.get::<_, String>(0)))
            .unwrap()
            .expect(sql)
    }

    /// « Blue Period », twelve volumes of two pages each, scanned.
    fn blue_period() -> (Library, String) {
        let library = Library::new();
        let folder = library.root().join("Blue Period");
        std::fs::create_dir_all(&folder).unwrap();
        for n in 1..=12 {
            std::fs::write(
                folder.join(format!("Tome {n:02}.cbz")),
                volume_bytes(n, 2, None),
            )
            .unwrap();
        }
        library.scan();
        let series = library.id("SELECT id FROM edition");
        (library, series)
    }

    /// A universe of two series, « Elfes » and « Mages », one volume each, scanned.
    fn terres_d_arran() -> (Library, String) {
        let library = Library::new();
        let universe = library.root().join("Terres d'Arran");
        std::fs::create_dir_all(&universe).unwrap();
        std::fs::write(
            universe.join("universe.json"),
            r#"{"leaf":1,"name":"Terres d'Arran"}"#,
        )
        .unwrap();
        for series in ["Elfes", "Mages"] {
            std::fs::create_dir_all(universe.join(series)).unwrap();
            std::fs::write(
                universe.join(series).join("Tome 01.cbz"),
                volume_bytes(1, 2, None),
            )
            .unwrap();
        }
        library.scan();
        let id = library.id("SELECT id FROM universe");
        (library, id)
    }

    fn session(&self, id: &str) -> PathBuf {
        self.dir.path().join("inbox").join(id)
    }

    fn entry(&self, number: u32) -> String {
        self.id(&format!(
            "SELECT id FROM entry WHERE volume_number = {number}"
        ))
    }

    fn open(&self, request: &ImportRequest) -> anyhow::Result<ImportOpened> {
        self.bulk().open(request, DEFAULT_PROFILE, &Ceiling::NONE)
    }
}

fn volume_bytes(number: u32, pages: u8, title: Option<&str>) -> Vec<u8> {
    common::archive_of_pages(
        &EntryJson {
            leaf: Some(1),
            number: Some(f64::from(number)),
            title: title.map(str::to_string),
            ..Default::default()
        },
        pages,
    )
}

fn file(path: &str, bytes: &[u8], number: u32, pages: u32) -> ManifestFile {
    ManifestFile {
        path: path.to_string(),
        size: bytes.len() as u64,
        found: Some(FoundInFile {
            number: Some(f64::from(number)),
            pages: Some(pages),
            ..FoundInFile::default()
        }),
        ..ManifestFile::default()
    }
}

fn aimed(kind: TargetKind, id: &str, root: &str, files: Vec<ManifestFile>) -> ImportRequest {
    ImportRequest {
        root: root.to_string(),
        files,
        scope: Scope::Addition,
        target: Some(ImportTarget {
            kind,
            id: id.to_string(),
        }),
        ..ImportRequest::default()
    }
}

fn outcome_of<'a>(opened: &'a ImportOpened, path: &str) -> &'a VolumeMatch {
    opened
        .volumes
        .iter()
        .find(|v| v.path == path)
        .unwrap_or_else(|| panic!("{path} in {:?}", opened.volumes))
}

/// Volumes 1 to 10 as they are, under other names, except 4 (another page) and 7 (the same
/// pages, another title); then 13 and 14, which are new. 11 and 12 are not brought.
fn the_drop() -> Vec<(String, Vec<u8>, u32, u32)> {
    let mut drop = Vec::new();
    for n in 1..=10u32 {
        let (bytes, pages) = match n {
            4 => (volume_bytes(4, 3, None), 3),
            7 => (volume_bytes(7, 2, Some("Le tableau")), 2),
            _ => (volume_bytes(n, 2, None), 2),
        };
        drop.push((format!("blue_period_{n:02}.cbz"), bytes, n, pages));
    }
    for n in 13..=14u32 {
        drop.push((
            format!("blue_period_{n:02}.cbz"),
            volume_bytes(n, 2, None),
            n,
            2,
        ));
    }
    drop
}

fn files_of(drop: &[(String, Vec<u8>, u32, u32)]) -> Vec<ManifestFile> {
    drop.iter()
        .map(|(path, bytes, n, pages)| file(path, bytes, *n, *pages))
        .collect()
}

#[test]
fn a_series_reimport_names_what_is_added_replaced_and_absent() {
    let (library, series) = Library::blue_period();
    let drop = the_drop();

    let opened = library
        .open(&aimed(
            TargetKind::Series,
            &series,
            "Blue Period",
            files_of(&drop),
        ))
        .expect("opening");

    let count = |outcome: Outcome| {
        opened
            .volumes
            .iter()
            .filter(|v| v.outcome == outcome)
            .count()
    };
    assert_eq!(8, count(Outcome::Same), "{:?}", opened.volumes);
    assert_eq!(
        Outcome::Replace,
        outcome_of(&opened, "blue_period_04.cbz").outcome
    );
    assert_eq!(
        Outcome::Update,
        outcome_of(&opened, "blue_period_07.cbz").outcome
    );
    assert_eq!(
        Outcome::Add,
        outcome_of(&opened, "blue_period_13.cbz").outcome
    );
    assert_eq!(
        Outcome::Add,
        outcome_of(&opened, "blue_period_14.cbz").outcome
    );
    let absent: Vec<Option<f64>> = opened.absent.iter().map(|a| a.number).collect();
    assert_eq!(vec![Some(11.0), Some(12.0)], absent);
    assert_eq!(
        vec![
            "blue_period_04.cbz",
            "blue_period_07.cbz",
            "blue_period_13.cbz",
            "blue_period_14.cbz"
        ],
        opened.to_send
    );
    assert_eq!(8, opened.already_there.len());
    assert!(opened.creates.is_empty(), "{:?}", opened.creates);
}

#[test]
fn a_reimport_announced_under_another_folder_name_lands_in_the_target() {
    let (library, series) = Library::blue_period();
    let drop = the_drop();

    let opened = library
        .open(&aimed(
            TargetKind::Series,
            &series,
            "Blue Period (scan)",
            files_of(&drop),
        ))
        .expect("opening");

    assert_eq!("Blue Period", opened.root);
    assert_eq!(
        Some("Tome 04.cbz"),
        outcome_of(&opened, "blue_period_04.cbz").lands.as_deref()
    );
    // What would be written over is named by the path the client announced.
    let replaced: Vec<&str> = opened.replaces.iter().map(|r| r.path.as_str()).collect();
    assert_eq!(vec!["blue_period_04.cbz", "blue_period_07.cbz"], replaced);
}

#[test]
fn an_identical_file_under_another_name_is_not_sent() {
    let (library, series) = Library::blue_period();
    let three = volume_bytes(3, 2, None);

    let opened = library
        .open(&aimed(
            TargetKind::Series,
            &series,
            "Blue Period",
            vec![file("bp-03.cbz", &three, 3, 2)],
        ))
        .expect("opening");

    assert_eq!(vec!["bp-03.cbz".to_string()], opened.already_there);
    assert!(opened.to_send.is_empty(), "{:?}", opened.to_send);
    assert!(opened.replaces.is_empty(), "{:?}", opened.replaces);
    assert_eq!(Outcome::Same, opened.volumes[0].outcome);
}

#[test]
fn an_identical_file_whose_number_was_read_wrong_is_still_not_sent() {
    let (library, series) = Library::blue_period();
    let three = volume_bytes(3, 2, None);
    // Only its checksum can say so: without one, the same length is all there is.
    let mut misread = file("bp-03.cbz", &three, 9, 2);
    misread.checksum = Some(checksum_of(&three));

    let opened = library
        .open(&aimed(
            TargetKind::Series,
            &series,
            "Blue Period",
            vec![misread],
        ))
        .expect("opening");

    assert_eq!(Outcome::Same, opened.volumes[0].outcome);
    assert!(opened.to_send.is_empty(), "{:?}", opened.to_send);
}

fn a_komga_volume() -> Vec<u8> {
    use std::io::Write;
    let mut zip = zip::ZipWriter::new(std::io::Cursor::new(Vec::new()));
    let options = zip::write::SimpleFileOptions::default();
    for i in 0..2 {
        zip.start_file::<_, ()>(format!("{i:03}.jpg"), options)
            .unwrap();
        zip.write_all(&common::jpeg()).unwrap();
    }
    zip.start_file::<_, ()>("ComicInfo.xml", options).unwrap();
    zip.write_all(b"<ComicInfo><Number>2</Number></ComicInfo>")
        .unwrap();
    zip.finish().unwrap().into_inner()
}

fn the_conversion() -> ArchiveRewrite {
    ArchiveRewrite {
        entry_json: r#"{"leaf":1,"number":2}"#.to_string(),
        remove: vec!["ComicInfo.xml".to_string()],
        one_shot_json: None,
    }
}

#[test]
fn a_converted_volume_met_again_through_a_target_is_the_same() {
    let library = Library::new();
    let bytes = a_komga_volume();
    let bulk = library.bulk();
    let mut first = file("Nains 02.cbz", &bytes, 2, 2);
    first.rewrite = Some(the_conversion());
    let opened = library
        .open(&ImportRequest {
            root: "Terres d'Arran".to_string(),
            files: vec![first],
            ..ImportRequest::default()
        })
        .unwrap();
    bulk.receive(&opened.id, "Nains 02.cbz", 0, &bytes, SENT_CEILING)
        .unwrap();
    assert_eq!(
        1,
        bulk.commit(&opened.id, &[], &[], &[], DEFAULT_PROFILE)
            .unwrap()
            .installed
    );
    library.scan();
    let series = library.id("SELECT id FROM edition");

    // The file the library holds is the rewritten one, another length than this.
    let mut again = file("Komga - Nains 02.cbz", &bytes, 2, 2);
    again.rewrite = Some(the_conversion());
    let opened = library
        .open(&aimed(TargetKind::Series, &series, "Nains", vec![again]))
        .unwrap();

    assert_eq!(
        Outcome::Same,
        opened.volumes[0].outcome,
        "{:?}",
        opened.volumes
    );
    assert!(opened.to_send.is_empty(), "{:?}", opened.to_send);
    assert!(opened.replaces.is_empty(), "{:?}", opened.replaces);
}

#[test]
fn a_folder_dropped_without_target_onto_a_series_it_holds_makes_no_duplicate() {
    let (library, _) = Library::blue_period();
    let drop = the_drop();
    let files: Vec<ManifestFile> = files_of(&drop[..10]);

    let opened = library
        .open(&ImportRequest {
            root: "Blue Period".to_string(),
            files,
            ..ImportRequest::default()
        })
        .expect("opening");

    assert_eq!(10, opened.volumes.len());
    for volume in &opened.volumes {
        assert!(
            matches!(
                volume.outcome,
                Outcome::Same | Outcome::Replace | Outcome::Update
            ),
            "{volume:?}"
        );
        assert!(volume.lands.as_deref().unwrap().starts_with("Tome "));
    }
    assert!(opened.absent.is_empty(), "absent is said only for a target");
    assert!(opened.target.is_none());
}

#[test]
fn a_target_the_profile_cannot_see_is_unknown() {
    let (library, series) = Library::blue_period();
    let request = aimed(TargetKind::Series, &series, "Blue Period", Vec::new());

    // Nobody rated it, so a child of twelve does not see it.
    let refused = library
        .bulk()
        .open(&request, DEFAULT_PROFILE, &Ceiling(Some(12)))
        .expect_err("hidden");
    assert!(
        refused.downcast_ref::<leaf_server::api::Absent>().is_some(),
        "{refused}"
    );

    let nothing = aimed(
        TargetKind::Series,
        "no-such-series",
        "Blue Period",
        Vec::new(),
    );
    let refused = library.open(&nothing).expect_err("unknown");
    assert!(refused.downcast_ref::<leaf_server::api::Absent>().is_some());
}

fn is_refused(result: anyhow::Result<ImportOpened>) -> bool {
    result
        .err()
        .is_some_and(|e| e.downcast_ref::<leaf_server::api::Invalid>().is_some())
}

#[test]
fn a_volume_target_takes_one_file_and_one_only() {
    let (library, _) = Library::blue_period();
    let two = library.entry(2);
    let bytes = volume_bytes(2, 2, None);

    let both = vec![file("a.cbz", &bytes, 2, 2), file("b.cbz", &bytes, 2, 2)];
    assert!(is_refused(library.open(&aimed(
        TargetKind::Entry,
        &two,
        "x",
        both
    ))));
    assert!(is_refused(library.open(&aimed(
        TargetKind::Entry,
        &two,
        "x",
        Vec::new()
    ))));
    // A cover beside it is not a second volume.
    let mut with_cover = vec![file("a.cbz", &bytes, 2, 2)];
    with_cover.push(ManifestFile {
        path: "cover.jpg".to_string(),
        size: 4,
        ..ManifestFile::default()
    });
    assert!(library
        .open(&aimed(TargetKind::Entry, &two, "x", with_cover))
        .is_ok());
}

#[test]
fn a_declaration_that_is_not_the_targets_own_is_refused() {
    let (library, series) = Library::blue_period();
    let declaring = |name: &str| {
        let mut request = aimed(TargetKind::Series, &series, "Blue Period", Vec::new());
        request.sidecars = vec![ManifestSidecar {
            path: name.to_string(),
            json: r#"{"leaf":1}"#.to_string(),
        }];
        library.open(&request)
    };
    // The series is its work's only edition: its folder is the work's, and says so with a
    // work.json, never an edition.json or a universe.json.
    assert!(is_refused(declaring("edition.json")));
    assert!(is_refused(declaring("universe.json")));
    assert!(declaring("work.json").is_ok());
    // Below the root, a declaration describes a folder of the drop, not the target.
    assert!(declaring("Extras/edition.json").is_ok());
}

#[test]
fn a_range_of_pages_without_a_chapter_aimed_at_is_refused() {
    let (library, series) = Library::blue_period();
    let bytes = volume_bytes(2, 2, None);
    let mut spliced = file("t02.cbz", &bytes, 2, 2);
    spliced.splice = Some(Splice { from: 0, to: 1 });
    assert!(is_refused(library.open(&aimed(
        TargetKind::Series,
        &series,
        "Blue Period",
        vec![spliced.clone()]
    ))));
    assert!(is_refused(library.open(&ImportRequest {
        root: "Blue Period".to_string(),
        files: vec![spliced],
        ..ImportRequest::default()
    })));
}

#[test]
fn a_volume_target_answers_the_file_of_another_volume() {
    let (library, _) = Library::blue_period();
    std::fs::remove_file(library.root().join("Blue Period/Tome 05.cbz")).unwrap();
    library.scan();
    let two = library.entry(2);
    let five = volume_bytes(5, 2, None);

    let opened = library
        .open(&aimed(
            TargetKind::Entry,
            &two,
            "Tome 05",
            vec![file("Tome 05.cbz", &five, 5, 2)],
        ))
        .expect("opening");

    let answer = &opened.volumes[0];
    assert_eq!(Outcome::OtherNumber, answer.outcome);
    assert_eq!(Some(two.as_str()), answer.wanted.as_deref());
    assert_eq!(None, answer.present);
    let target = opened.target.expect("the target, named");
    assert_eq!(2, target.trail.len(), "{:?}", target.trail);
    assert_eq!(two, target.id);
    // The volume aimed at and not met is what stays.
    let absent: Vec<&str> = opened.absent.iter().map(|a| a.entry_id.as_str()).collect();
    assert_eq!(vec![two.as_str()], absent);
}

/// What a series reimport asks: where the target is, what it holds, the origins of
/// what it holds and of every landing, and what names them — four statements, for one file as
/// for a hundred.
#[test]
fn opening_a_reimport_costs_the_same_for_one_file_as_for_a_hundred() {
    let (library, series) = Library::blue_period();
    let bytes = volume_bytes(1, 2, None);
    let cost = |count: u32| {
        let files: Vec<ManifestFile> = (1..=count)
            .map(|n| file(&format!("t{n:03}.cbz"), &bytes, n, 2))
            .collect();
        let before = library.db.statements();
        library
            .open(&aimed(TargetKind::Series, &series, "Blue Period", files))
            .unwrap();
        library.db.statements() - before
    };
    assert_eq!(4, cost(1));
    assert_eq!(4, cost(100));
}

/// Without a target: the editions the folders are, what they hold, the origins — three.
#[test]
fn opening_a_drop_onto_a_series_costs_the_same_for_one_file_as_for_a_hundred() {
    let (library, _) = Library::blue_period();
    let bytes = volume_bytes(1, 2, None);
    let cost = |count: u32| {
        let files: Vec<ManifestFile> = (1..=count)
            .map(|n| file(&format!("t{n:03}.cbz"), &bytes, n, 2))
            .collect();
        let before = library.db.statements();
        library
            .open(&ImportRequest {
                root: "Blue Period".to_string(),
                files,
                ..ImportRequest::default()
            })
            .unwrap();
        library.db.statements() - before
    };
    assert_eq!(3, cost(1));
    assert_eq!(3, cost(100));
}

/// A universe: where it is with its editions, every work, what they hold, the origins.
#[test]
fn opening_a_universe_reimport_costs_the_same_for_one_file_as_for_a_hundred() {
    let (library, universe) = Library::terres_d_arran();
    let bytes = volume_bytes(1, 2, None);
    let cost = |count: u32| {
        let files: Vec<ManifestFile> = (1..=count)
            .map(|n| file(&format!("Elfes/t{n:03}.cbz"), &bytes, n, 2))
            .collect();
        let before = library.db.statements();
        library
            .open(&aimed(TargetKind::Universe, &universe, "Arran", files))
            .unwrap();
        library.db.statements() - before
    };
    assert_eq!(4, cost(1));
    assert_eq!(4, cost(100));
}

#[test]
fn an_addition_through_a_target_is_installed_in_the_target_folder() {
    let (library, series) = Library::blue_period();
    let bulk = library.bulk();
    let thirteen = volume_bytes(13, 2, None);
    let three = volume_bytes(3, 2, None);

    let opened = library
        .open(&aimed(
            TargetKind::Series,
            &series,
            "Blue Period (scan)",
            vec![
                file("bp 13.cbz", &thirteen, 13, 2),
                file("bp 03.cbz", &three, 3, 2),
            ],
        ))
        .unwrap();
    let state = bulk.state(&opened.id).unwrap().unwrap();
    assert_eq!(vec!["bp 13.cbz".to_string()], state.missing);
    bulk.receive(&opened.id, "bp 13.cbz", 0, &thirteen, SENT_CEILING)
        .unwrap();
    let result = bulk
        .commit(&opened.id, &[], &[], &[], DEFAULT_PROFILE)
        .unwrap();

    assert_eq!("Blue Period", result.root);
    assert_eq!(1, result.installed);
    assert!(result.pending.is_empty(), "{:?}", result.pending);
    assert!(library
        .root()
        .join("Blue Period/blue-period_tome_013.cbz")
        .is_file());
    assert!(!library.root().join("Blue Period (scan)").exists());
    // The identical one was neither sent nor written anywhere.
    assert!(!library.root().join("Blue Period/bp 03.cbz").exists());
}

#[test]
fn a_file_that_lands_nowhere_is_neither_asked_for_nor_installed() {
    let (library, series) = Library::blue_period();
    let bulk = library.bulk();
    let bytes = volume_bytes(20, 2, None);
    let mut guessed = file("Tome 20.cbz", &bytes, 20, 2);
    guessed.found.as_mut().unwrap().guessed = true;

    let opened = library
        .open(&aimed(
            TargetKind::Series,
            &series,
            "Blue Period",
            vec![guessed],
        ))
        .unwrap();

    assert_eq!(Outcome::Unnumbered, opened.volumes[0].outcome);
    assert!(opened.to_send.is_empty() && opened.already_there.is_empty());
    assert!(bulk.state(&opened.id).unwrap().unwrap().missing.is_empty());
    bulk.receive(&opened.id, "Tome 20.cbz", 0, &bytes, SENT_CEILING)
        .unwrap();
    let result = bulk
        .commit(&opened.id, &[], &[], &[], DEFAULT_PROFILE)
        .unwrap();
    assert_eq!(0, result.installed);
    assert_eq!(vec!["Tome 20.cbz".to_string()], result.pending);
    assert!(!library.root().join("Blue Period/Tome 20.cbz").exists());
}

fn checksum_of(bytes: &[u8]) -> String {
    use sha2::{Digest, Sha256};
    Sha256::digest(bytes)
        .iter()
        .map(|b| format!("{b:02x}"))
        .collect()
}

// ------------------------------------------------------------------ what a session trusts

/// Opened, with one file sent whole: `(library, bulk, id, bytes)`.
fn a_session_with_one_file() -> (Library, BulkImport, String, Vec<u8>) {
    let library = Library::new();
    let bulk = library.bulk();
    let bytes = volume_bytes(1, 2, None);
    let opened = library
        .open(&ImportRequest {
            root: "Essai".to_string(),
            files: vec![ManifestFile {
                path: "a.cbz".to_string(),
                size: bytes.len() as u64,
                ..ManifestFile::default()
            }],
            ..ImportRequest::default()
        })
        .unwrap();
    bulk.receive(&opened.id, "a.cbz", 0, &bytes, SENT_CEILING)
        .unwrap();
    (library, bulk, opened.id, bytes)
}

#[test]
fn a_write_at_a_path_the_announcement_did_not_name_is_refused() {
    // The session's own files sat beside the received ones, so writing « _landings.json »
    // rewrote where the commit would install — anywhere on the disk.
    let (_library, bulk, id, _) = a_session_with_one_file();
    for name in ["_landings.json", "_manifest.json", "b.cbz", "files/a.cbz"] {
        assert!(
            bulk.receive(&id, name, 0, b"{}", SENT_CEILING).is_err(),
            "{name} was written"
        );
    }
}

#[test]
fn a_declaration_cannot_be_announced_over_the_sessions_own_files() {
    let library = Library::new();
    let bulk = library.bulk();
    let bytes = volume_bytes(1, 2, None);
    let opened = library
        .open(&ImportRequest {
            root: "Essai".to_string(),
            files: vec![ManifestFile {
                path: "a.cbz".to_string(),
                size: bytes.len() as u64,
                ..ManifestFile::default()
            }],
            sidecars: vec![ManifestSidecar {
                path: "_landings.json".to_string(),
                json: r#"{"root":"/tmp","files":{"a.cbz":"elsewhere.cbz"}}"#.to_string(),
            }],
            ..ImportRequest::default()
        })
        .unwrap();
    bulk.receive(&opened.id, "a.cbz", 0, &bytes, SENT_CEILING)
        .unwrap();
    let result = bulk
        .commit(&opened.id, &[], &[], &[], DEFAULT_PROFILE)
        .unwrap();
    assert_eq!("Essai", result.root);
    assert!(library.root().join("Essai/a.cbz").is_file());
}

/// Every forged landing an attacker with the disk could write, and none is followed.
#[test]
fn landings_that_leave_the_library_are_refused_rather_than_followed() {
    let outside = tempfile::tempdir().unwrap();
    let absolute = outside.path().to_string_lossy().into_owned();
    for (root, landing) in [
        (absolute.as_str(), "a.cbz"),
        ("..", "a.cbz"),
        ("../outside", "a.cbz"),
        ("Essai", "../../outside.cbz"),
        ("Essai", absolute.as_str()),
    ] {
        let (library, bulk, id, _) = a_session_with_one_file();
        let forged = serde_json::json!({
            "root": root,
            "files": {"a.cbz": landing},
            "sidecars": {},
        });
        std::fs::write(
            library.session(&id).join("_landings.json"),
            forged.to_string(),
        )
        .unwrap();

        assert!(
            bulk.commit(&id, &[], &[], &[], DEFAULT_PROFILE).is_err(),
            "{root} / {landing}"
        );
        assert!(bulk.state(&id).is_err(), "{root} / {landing}");
        assert_eq!(0, std::fs::read_dir(outside.path()).unwrap().count());
        assert!(!library.dir.path().join("outside").exists());
        assert!(!library.dir.path().join("outside.cbz").exists());
    }
}

#[test]
fn landings_naming_a_file_the_announcement_did_not_list_are_refused() {
    let (library, bulk, id, _) = a_session_with_one_file();
    let forged = serde_json::json!({
        "root": "Essai",
        "files": {"a.cbz": "a.cbz", "b.cbz": "b.cbz"},
        "sidecars": {},
    });
    std::fs::write(
        library.session(&id).join("_landings.json"),
        forged.to_string(),
    )
    .unwrap();
    assert!(bulk.commit(&id, &[], &[], &[], DEFAULT_PROFILE).is_err());
}

#[test]
fn a_manifest_whose_root_leaves_the_library_is_refused() {
    let outside = tempfile::tempdir().unwrap();
    for root in [
        outside.path().to_string_lossy().into_owned(),
        "..".to_string(),
    ] {
        let (library, bulk, id, _) = a_session_with_one_file();
        let session = library.session(&id);
        std::fs::remove_file(session.join("_landings.json")).unwrap();
        let manifest = serde_json::json!({
            "root": root,
            "files": [{"path": "a.cbz", "size": 4}],
        });
        std::fs::write(session.join("_manifest.json"), manifest.to_string()).unwrap();

        assert!(
            bulk.commit(&id, &[], &[], &[], DEFAULT_PROFILE).is_err(),
            "{root}"
        );
        assert_eq!(0, std::fs::read_dir(outside.path()).unwrap().count());
    }
}

fn sessions_in(library: &Library) -> usize {
    std::fs::read_dir(library.dir.path().join("inbox"))
        .unwrap()
        .count()
}

fn plain(path: &str) -> ManifestFile {
    ManifestFile {
        path: path.to_string(),
        size: 4,
        ..ManifestFile::default()
    }
}

fn declaration(path: &str) -> ManifestSidecar {
    ManifestSidecar {
        path: path.to_string(),
        json: r#"{"leaf":1}"#.to_string(),
    }
}

/// Every announcement whose paths do not each name one place of their own under the root.
/// Accepted, each opened a session the commit then refused to read, which no listing
/// showed and nothing could abandon.
#[test]
fn an_announcement_whose_paths_do_not_each_name_one_place_is_refused_before_a_session() {
    let cases: Vec<(Vec<ManifestFile>, Vec<ManifestSidecar>)> = vec![
        (vec![plain("../../evade.cbz"), plain("ok.cbz")], vec![]),
        (vec![plain("../Essai/a.cbz")], vec![]),
        // Two names for one received file: the second overwrote the first.
        (vec![plain("a.cbz"), plain("x/../a.cbz")], vec![]),
        (vec![plain("a.cbz"), plain("a.cbz")], vec![]),
        (vec![plain("a.cbz"), plain("./a.cbz")], vec![]),
        (vec![plain("x/a.cbz"), plain("x//a.cbz")], vec![]),
        (vec![plain("x/..")], vec![]),
        (vec![plain("")], vec![]),
        (vec![plain(".")], vec![]),
        (vec![plain("x/")], vec![]),
        (vec![plain("/a.cbz")], vec![]),
        (vec![plain("a\0.cbz")], vec![]),
        // A file, and a folder of the same name: one of them cannot be written.
        (vec![plain("x"), plain("x/a.cbz")], vec![]),
        (vec![plain("x/a.cbz")], vec![declaration("x")]),
        // Resolved under the received files, it named the received files' own folder.
        (
            vec![plain("a.cbz")],
            vec![declaration("../files/work.json")],
        ),
        (vec![plain("a.cbz")], vec![declaration("../../work.json")]),
    ];
    for (files, sidecars) in cases {
        let library = Library::new();
        let what = format!(
            "{:?} {:?}",
            files.iter().map(|f| &f.path).collect::<Vec<_>>(),
            sidecars.iter().map(|s| &s.path).collect::<Vec<_>>()
        );
        let refused = library.open(&ImportRequest {
            root: "Essai".to_string(),
            files,
            sidecars,
            ..ImportRequest::default()
        });
        assert!(is_refused(refused), "{what}");
        assert_eq!(0, sessions_in(&library), "{what}");
    }
}

/// A declaration is read before it travels, and refused when it cannot be read. Announced
/// as a file instead, it travelled as bytes nobody read and was installed as they came.
#[test]
fn a_declaration_announced_as_a_file_is_refused() {
    for (files, sidecars) in [
        (vec![plain("work.json")], vec![declaration("work.json")]),
        (vec![plain("work.json"), plain("a.cbz")], vec![]),
        (vec![plain("Elfes/EDITION.JSON")], vec![]),
        (vec![plain("universe.json")], vec![]),
        // Not a declaration, but announced as both: one would overwrite the other.
        (vec![plain("cover.jpg")], vec![declaration("cover.jpg")]),
    ] {
        let library = Library::new();
        let what = format!("{:?}", files.iter().map(|f| &f.path).collect::<Vec<_>>());
        let refused = library.open(&ImportRequest {
            root: "Essai".to_string(),
            files,
            sidecars,
            ..ImportRequest::default()
        });
        assert!(is_refused(refused), "{what}");
        assert_eq!(0, sessions_in(&library), "{what}");
    }
}

/// A session an earlier server opened, or one written by hand, can still name paths this
/// one refuses. Read back, they are refused as they would be announced — said as the
/// caller's to fix, and the session can still be abandoned.
#[test]
fn a_session_naming_paths_no_announcement_may_name_is_refused_and_abandonable() {
    let (library, bulk, id, _) = a_session_with_one_file();
    let session = library.session(&id);
    std::fs::remove_file(session.join("_landings.json")).unwrap();
    let manifest = serde_json::json!({
        "root": "Essai",
        "files": [{"path": "../../evade.cbz", "size": 4}],
        "sidecars": [{
            "path": "../../Ailleurs/work.json",
            "json": r#"{"leaf":1,"id":"wrk_ailleurs"}"#,
        }],
    });
    std::fs::write(session.join("_manifest.json"), manifest.to_string()).unwrap();

    let refused = bulk
        .commit(&id, &[], &[], &[], DEFAULT_PROFILE)
        .unwrap_err();
    assert!(
        refused
            .downcast_ref::<leaf_server::api::Invalid>()
            .is_some(),
        "{refused:?}"
    );
    assert!(bulk.state(&id).is_err());
    assert!(bulk.place_for(&id, "wrk_ailleurs").is_err());
    assert!(!library.dir.path().join("evade.cbz").exists());
    assert!(!library.dir.path().join("Ailleurs").exists());

    bulk.abandon(&id).unwrap();
    assert!(!session.exists());
}

/// Its received files sat at the session's root, where this server no longer looks: the
/// state said every one missing, and a commit installed the declarations without them.
#[test]
fn a_session_kept_in_the_earlier_layout_is_refused_and_abandonable() {
    let (library, bulk, id, _) = a_session_with_one_file();
    let session = library.session(&id);
    std::fs::rename(session.join("files/a.cbz"), session.join("a.cbz")).unwrap();
    std::fs::remove_dir(session.join("files")).unwrap();

    assert!(bulk.state(&id).is_err());
    assert!(bulk.commit(&id, &[], &[], &[], DEFAULT_PROFILE).is_err());
    assert!(session.join("a.cbz").is_file());
    assert!(!library.root().join("Essai").exists());

    bulk.abandon(&id).unwrap();
    assert!(!session.exists());
}

/// Nothing received yet and nothing declared: the session is the new layout all the same.
#[test]
fn a_session_with_nothing_received_yet_is_read_back() {
    let library = Library::new();
    let bulk = library.bulk();
    let opened = library
        .open(&ImportRequest {
            root: "Essai".to_string(),
            files: vec![plain("a.cbz")],
            ..ImportRequest::default()
        })
        .unwrap();
    let state = bulk.state(&opened.id).unwrap().unwrap();
    assert_eq!(vec!["a.cbz".to_string()], state.missing);
}

// ------------------------------------------------------------------ installed or not

#[test]
fn a_named_replacement_is_installed_on_the_volume_it_replaces() {
    let (library, series) = Library::blue_period();
    let bulk = library.bulk();
    let four = volume_bytes(4, 3, None);
    let opened = library
        .open(&aimed(
            TargetKind::Series,
            &series,
            "Blue Period",
            vec![file("bp 04.cbz", &four, 4, 3)],
        ))
        .unwrap();
    bulk.receive(&opened.id, "bp 04.cbz", 0, &four, SENT_CEILING)
        .unwrap();

    let result = bulk
        .commit(
            &opened.id,
            &["bp 04.cbz".to_string()],
            &[],
            &[],
            DEFAULT_PROFILE,
        )
        .unwrap();

    assert_eq!(1, result.installed);
    assert_eq!(
        four,
        std::fs::read(library.root().join("Blue Period/Tome 04.cbz")).unwrap()
    );
    assert!(!library.root().join("Blue Period/bp 04.cbz").exists());
}

#[test]
fn an_unticked_replacement_lands_nowhere_and_stays_pending() {
    let (library, series) = Library::blue_period();
    let bulk = library.bulk();
    let four = volume_bytes(4, 3, None);
    let before = std::fs::read(library.root().join("Blue Period/Tome 04.cbz")).unwrap();
    let opened = library
        .open(&aimed(
            TargetKind::Series,
            &series,
            "Blue Period",
            vec![file("bp 04.cbz", &four, 4, 3)],
        ))
        .unwrap();
    bulk.receive(&opened.id, "bp 04.cbz", 0, &four, SENT_CEILING)
        .unwrap();

    let result = bulk
        .commit(&opened.id, &[], &[], &[], DEFAULT_PROFILE)
        .unwrap();

    assert_eq!(vec!["bp 04.cbz".to_string()], result.pending);
    assert_eq!(
        before,
        std::fs::read(library.root().join("Blue Period/Tome 04.cbz")).unwrap()
    );
    assert!(result.replaced.is_empty(), "{:?}", result.replaced);
    assert!(!library.dir.path().join("inbox/replaced").exists());
    assert!(replaced::of_entry(&library.db, &library.entry(4))
        .unwrap()
        .is_empty());
}

#[test]
fn an_identical_file_sent_anyway_is_never_installed() {
    let (library, series) = Library::blue_period();
    let bulk = library.bulk();
    let three = volume_bytes(3, 2, None);
    let opened = library
        .open(&aimed(
            TargetKind::Series,
            &series,
            "Blue Period",
            vec![file("bp 03.cbz", &three, 3, 2)],
        ))
        .unwrap();
    bulk.receive(&opened.id, "bp 03.cbz", 0, &three, SENT_CEILING)
        .unwrap();

    let result = bulk
        .commit(
            &opened.id,
            &["bp 03.cbz".to_string()],
            &[],
            &[],
            DEFAULT_PROFILE,
        )
        .unwrap();

    assert_eq!(0, result.installed);
    assert!(!library.root().join("Blue Period/bp 03.cbz").exists());
}

#[test]
fn the_file_of_another_volume_is_installed_only_when_named() {
    let (library, _) = Library::blue_period();
    let bulk = library.bulk();
    let two = library.entry(2);
    let five = volume_bytes(5, 3, None);
    let held_five = std::fs::read(library.root().join("Blue Period/Tome 05.cbz")).unwrap();
    let opened = library
        .open(&aimed(
            TargetKind::Entry,
            &two,
            "x",
            vec![file("t05.cbz", &five, 5, 3)],
        ))
        .unwrap();
    assert_eq!(Outcome::OtherNumber, opened.volumes[0].outcome);
    assert_eq!(Some("Tome 05.cbz"), opened.volumes[0].lands.as_deref());
    // Aimed at volume 2, it would land on volume 5: not a replacement anybody asked for.
    assert!(opened.replaces.is_empty(), "{:?}", opened.replaces);
    bulk.receive(&opened.id, "t05.cbz", 0, &five, SENT_CEILING)
        .unwrap();

    let result = bulk
        .commit(&opened.id, &[], &[], &[], DEFAULT_PROFILE)
        .unwrap();
    assert_eq!(vec!["t05.cbz".to_string()], result.pending);
    assert_eq!(
        held_five,
        std::fs::read(library.root().join("Blue Period/Tome 05.cbz")).unwrap()
    );

    let result = bulk
        .commit(
            &opened.id,
            &["t05.cbz".to_string()],
            &[],
            &[],
            DEFAULT_PROFILE,
        )
        .unwrap();
    assert_eq!(1, result.installed);
    assert_eq!(
        five,
        std::fs::read(library.root().join("Blue Period/Tome 05.cbz")).unwrap()
    );
}

#[test]
fn the_file_of_another_series_is_installed_only_when_named() {
    let (library, series) = Library::blue_period();
    let bulk = library.bulk();
    let giant = volume_bytes(13, 2, None);
    let mut named = file("Blue Giant 13.cbz", &giant, 13, 2);
    named.found.as_mut().unwrap().series = Some("Blue Giant".to_string());
    let opened = library
        .open(&aimed(
            TargetKind::Series,
            &series,
            "Blue Period",
            vec![named],
        ))
        .unwrap();
    assert_eq!(Outcome::OtherSeries, opened.volumes[0].outcome);
    bulk.receive(&opened.id, "Blue Giant 13.cbz", 0, &giant, SENT_CEILING)
        .unwrap();

    let result = bulk
        .commit(&opened.id, &[], &[], &[], DEFAULT_PROFILE)
        .unwrap();
    assert_eq!(vec!["Blue Giant 13.cbz".to_string()], result.pending);
    assert!(!library
        .root()
        .join("Blue Period/Blue Giant 13.cbz")
        .exists());
}

// ------------------------------------------------------------------ which series, which volume

#[test]
fn a_universe_reimport_knows_each_series_by_its_own_name() {
    let (library, universe) = Library::terres_d_arran();
    let bytes = volume_bytes(1, 3, None);
    let mut elfes = file("Elfes/Tome 01.cbz", &bytes, 1, 3);
    elfes.found.as_mut().unwrap().series = Some("Elfes".to_string());

    let opened = library
        .open(&aimed(
            TargetKind::Universe,
            &universe,
            "Arran",
            vec![elfes],
        ))
        .unwrap();

    assert_eq!(
        Outcome::Replace,
        opened.volumes[0].outcome,
        "{:?}",
        opened.volumes
    );
}

#[test]
fn a_volume_of_the_same_length_as_another_is_not_taken_for_it() {
    // Tome 10 and tome 11 weigh the same to the byte: two digits in their entry.json, the
    // same number of pages. Taken on its length, the new 11 was « already there ».
    let library = Library::new();
    let folder = library.root().join("Blue Period");
    std::fs::create_dir_all(&folder).unwrap();
    std::fs::write(folder.join("Tome 10.cbz"), volume_bytes(10, 2, None)).unwrap();
    library.scan();
    let series = library.id("SELECT id FROM edition");
    let eleven = volume_bytes(11, 2, None);
    assert_eq!(
        std::fs::metadata(folder.join("Tome 10.cbz")).unwrap().len(),
        eleven.len() as u64
    );

    let opened = library
        .open(&aimed(
            TargetKind::Series,
            &series,
            "Blue Period",
            vec![file("Tome 11.cbz", &eleven, 11, 2)],
        ))
        .unwrap();

    assert_eq!(Outcome::Add, opened.volumes[0].outcome);
    assert_eq!(vec!["Tome 11.cbz".to_string()], opened.to_send);
}

#[test]
fn a_folder_named_after_its_publisher_still_knows_its_series_title() {
    // « One Piece (Glénat) » on the disk, « One Piece » in the work.json and in the file.
    let library = Library::new();
    let folder = library.root().join("One Piece (Glénat)");
    std::fs::create_dir_all(&folder).unwrap();
    std::fs::write(
        folder.join("work.json"),
        r#"{"leaf":1,"title":"One Piece"}"#,
    )
    .unwrap();
    std::fs::write(folder.join("Tome 01.cbz"), volume_bytes(1, 2, None)).unwrap();
    library.scan();
    let bytes = volume_bytes(1, 3, None);
    let mut one = file("Tome 01 (v2).cbz", &bytes, 1, 3);
    one.found.as_mut().unwrap().series = Some("One Piece".to_string());

    let opened = library
        .open(&ImportRequest {
            root: "One Piece (Glénat)".to_string(),
            files: vec![one],
            ..ImportRequest::default()
        })
        .unwrap();

    assert_eq!(
        Outcome::Replace,
        opened.volumes[0].outcome,
        "{:?}",
        opened.volumes
    );
}

#[test]
fn a_drop_onto_a_series_hidden_from_the_profile_compares_nothing() {
    let (library, _) = Library::blue_period();
    let drop = the_drop();
    let opened = library
        .bulk()
        .open(
            &ImportRequest {
                root: "Blue Period".to_string(),
                files: files_of(&drop[..3]),
                ..ImportRequest::default()
            },
            DEFAULT_PROFILE,
            &Ceiling(Some(12)),
        )
        .unwrap();
    assert!(opened.volumes.is_empty(), "{:?}", opened.volumes);
}

#[test]
fn a_drop_that_climbs_into_another_series_is_refused() {
    let (library, _) = Library::blue_period();
    std::fs::create_dir_all(library.root().join("Essai")).unwrap();
    let bytes = volume_bytes(4, 3, None);
    let refused = library.open(&ImportRequest {
        root: "Essai".to_string(),
        files: vec![file("../Blue Period/Tome 04.cbz", &bytes, 4, 3)],
        ..ImportRequest::default()
    });
    assert!(is_refused(refused));
}

// ------------------------------------------------------------------ installed at the landing

const NOW: i64 = 1_790_000_000_000;

/// « Blue Period », five volumes, the fourth of 120 pages so a place deep in it is one.
fn a_long_fourth() -> (Library, String) {
    let library = Library::new();
    let folder = library.root().join("Blue Period");
    std::fs::create_dir_all(&folder).unwrap();
    for n in 1..=5 {
        let pages = if n == 4 { 120 } else { 2 };
        std::fs::write(
            folder.join(format!("Tome {n:02}.cbz")),
            volume_bytes(n, pages, None),
        )
        .unwrap();
    }
    library.scan();
    let series = library.id("SELECT id FROM edition");
    (library, series)
}

fn a_reader(library: &Library, name: &str, color: &str) -> String {
    Profiles::new(Arc::clone(&library.db))
        .create(
            NewProfile {
                name: name.to_string(),
                color: color.to_string(),
                restricted: false,
                max_age: None,
                rights: Rights::default(),
            },
            NOW,
        )
        .unwrap()
        .unwrap()
        .id
}

fn place(library: &Library, profile: &str, entry: &str, page: i64, finished: Option<bool>) {
    let rewind = finished == Some(false);
    Progress::new(&library.db)
        .record(
            profile,
            entry,
            &ProgressPatch {
                page: Some(page),
                finished,
                rewind,
            },
            NOW,
            &Ceiling::NONE,
        )
        .unwrap()
        .expect("a place");
}

/// `(page, finished, times_finished)` of one profile in one entry.
fn progress_of(library: &Library, profile: &str, entry: &str) -> (i64, bool, i64) {
    library
        .db
        .read(|cx| {
            cx.query_one(
                "SELECT page, finished, times_finished FROM progress
                 WHERE profile_id = ?1 AND entry_id = ?2",
                [profile, entry],
                |r| Ok((r.get(0)?, r.get(1)?, r.get(2)?)),
            )
        })
        .unwrap()
        .expect("a place")
}

fn number_of(library: &Library, sql: &str) -> i64 {
    library
        .db
        .read(|cx| cx.query_one(sql, [], |r| r.get::<_, Option<i64>>(0)))
        .unwrap()
        .flatten()
        .unwrap_or(-1)
}

/// The fourth volume brought again, 130 pages, announced and received whole.
fn a_new_fourth(library: &Library, series: &str) -> (BulkImport, String, Vec<u8>) {
    let bulk = library.bulk();
    let four = volume_bytes(4, 130, Some("Réédition"));
    let opened = library
        .open(&aimed(
            TargetKind::Series,
            series,
            "Blue Period",
            vec![file("bp 04.cbz", &four, 4, 130)],
        ))
        .unwrap();
    assert_eq!(Outcome::Replace, outcome_of(&opened, "bp 04.cbz").outcome);
    bulk.receive(&opened.id, "bp 04.cbz", 0, &four, SENT_CEILING)
        .unwrap();
    (bulk, opened.id, four)
}

fn fourth(library: &Library) -> PathBuf {
    library.root().join("Blue Period/Tome 04.cbz")
}

fn kept(library: &Library, id: &str) -> PathBuf {
    library
        .dir
        .path()
        .join("inbox/replaced")
        .join(id)
        .join("Tome 04.cbz")
}

fn replacing_four() -> Vec<String> {
    vec!["bp 04.cbz".to_string()]
}

#[test]
fn a_replaced_volume_keeps_its_reader_its_read_status_and_its_arrival_date() {
    let (library, series) = a_long_fourth();
    let entry = library.entry(4);
    let (ines, malo) = (
        a_reader(&library, "Inès", "sauge"),
        a_reader(&library, "Malo", "prune"),
    );
    place(&library, &ines, &entry, 112, None);
    place(&library, &malo, &entry, 119, Some(true));
    place(&library, &malo, &entry, 0, Some(false));
    place(&library, &malo, &entry, 119, Some(true));
    assert_eq!((119, true, 2), progress_of(&library, &malo, &entry));
    let arrived = number_of(
        &library,
        &format!("SELECT added_at FROM entry WHERE id = '{entry}'"),
    );
    let owned = owned_volumes(&library, &series);

    let (bulk, id, _) = a_new_fourth(&library, &series);
    let result = bulk
        .commit(&id, &replacing_four(), &[], &[], &ines)
        .unwrap();
    assert_eq!(1, result.installed);
    library.scan();

    assert_eq!(entry, library.entry(4));
    assert_eq!(
        130,
        number_of(
            &library,
            &format!("SELECT page_count FROM entry WHERE id = '{entry}'")
        )
    );
    assert_eq!((112, false, 0), progress_of(&library, &ines, &entry));
    assert_eq!((119, true, 2), progress_of(&library, &malo, &entry));
    assert_eq!(
        arrived,
        number_of(
            &library,
            &format!("SELECT added_at FROM entry WHERE id = '{entry}'")
        )
    );
    assert_eq!(5, number_of(&library, "SELECT COUNT(*) FROM entry"));
    assert_eq!(owned, owned_volumes(&library, &series));
}

#[test]
fn asking_for_a_reset_returns_only_this_profile_to_the_first_page() {
    let (library, series) = a_long_fourth();
    let entry = library.entry(4);
    let (ines, malo) = (
        a_reader(&library, "Inès", "sauge"),
        a_reader(&library, "Malo", "prune"),
    );
    place(&library, &ines, &entry, 112, None);
    place(&library, &malo, &entry, 50, None);

    let (bulk, id, _) = a_new_fourth(&library, &series);
    bulk.commit(&id, &replacing_four(), &[], &replacing_four(), &ines)
        .unwrap();
    library.scan();

    assert_eq!(0, progress_of(&library, &ines, &entry).0);
    assert_eq!(50, progress_of(&library, &malo, &entry).0);
}

#[test]
fn a_replaced_file_waits_aside_and_is_recorded() {
    let (library, series) = a_long_fourth();
    let entry = library.entry(4);
    let old = std::fs::read(fourth(&library)).unwrap();
    let (bulk, id, four) = a_new_fourth(&library, &series);

    let result = bulk
        .commit(&id, &replacing_four(), &[], &[], DEFAULT_PROFILE)
        .unwrap();

    assert_eq!(four, std::fs::read(fourth(&library)).unwrap());
    assert_eq!(old, std::fs::read(kept(&library, &id)).unwrap());
    let kept_at = format!("replaced/{id}/Tome 04.cbz");
    assert_eq!(1, result.replaced.len(), "{:?}", result.replaced);
    assert_eq!("bp 04.cbz", result.replaced[0].path);
    assert_eq!(Some(entry.clone()), result.replaced[0].entry_id);
    assert_eq!(kept_at, result.replaced[0].kept_at);

    let recorded = replaced::of_entry(&library.db, &entry).unwrap();
    assert_eq!(1, recorded.len(), "{recorded:?}");
    assert_eq!(kept_at, recorded[0].kept_at);
    assert_eq!("Blue Period/Tome 04.cbz", recorded[0].path);
    assert_eq!(DEFAULT_PROFILE, recorded[0].profile_id);
    assert_eq!(old.len() as u64, recorded[0].old_size);
    assert_eq!(four.len() as u64, recorded[0].new_size);
    assert_eq!(Some(120), recorded[0].old_pages);
    assert_eq!(Some(130), recorded[0].new_pages);
}

/// Cut after the old file was set aside and recorded, before the new one took its name.
#[test]
fn a_commit_cut_after_the_set_aside_finishes_on_the_next() {
    let (library, series) = a_long_fourth();
    let entry = library.entry(4);
    let old = std::fs::read(fourth(&library)).unwrap();
    let (bulk, id, four) = a_new_fourth(&library, &series);
    let aside = kept(&library, &id);
    std::fs::create_dir_all(aside.parent().unwrap()).unwrap();
    std::fs::hard_link(fourth(&library), &aside).unwrap();
    replaced::replace(
        &library.db,
        &Replaced {
            import_id: id.clone(),
            path: "Blue Period/Tome 04.cbz".to_string(),
            entry_id: entry.clone(),
            kept_at: format!("replaced/{id}/Tome 04.cbz"),
            replaced_at: NOW,
            profile_id: DEFAULT_PROFILE.to_string(),
            old_size: old.len() as u64,
            new_size: four.len() as u64,
            old_pages: Some(120),
            new_pages: Some(130),
        },
        &Positions::Kept { last: 129 },
    )
    .unwrap();

    let result = bulk
        .commit(&id, &replacing_four(), &[], &[], DEFAULT_PROFILE)
        .unwrap();

    assert_eq!(1, result.installed);
    assert_eq!(four, std::fs::read(fourth(&library)).unwrap());
    assert_eq!(old, std::fs::read(&aside).unwrap());
    assert_eq!(1, replaced::of_entry(&library.db, &entry).unwrap().len());
}

/// The file to install cannot even be prepared beside the old one: its folder is closed.
#[test]
fn a_commit_that_cannot_prepare_its_file_leaves_the_old_volume_whole() {
    use std::os::unix::fs::PermissionsExt;

    if as_root() {
        eprintln!("skipped: root writes into a folder whatever its permissions say");
        return;
    }
    let (library, series) = a_long_fourth();
    let entry = library.entry(4);
    let ines = a_reader(&library, "Inès", "sauge");
    place(&library, &ines, &entry, 112, None);
    let old = std::fs::read(fourth(&library)).unwrap();
    let (bulk, id, four) = a_new_fourth(&library, &series);
    let folder = library.root().join("Blue Period");
    std::fs::set_permissions(&folder, std::fs::Permissions::from_mode(0o555)).unwrap();

    let cut = bulk.commit(&id, &replacing_four(), &[], &replacing_four(), &ines);
    std::fs::set_permissions(&folder, std::fs::Permissions::from_mode(0o755)).unwrap();

    assert!(cut.is_err(), "{cut:?}");
    assert_eq!(old, std::fs::read(fourth(&library)).unwrap());
    assert_eq!(112, progress_of(&library, &ines, &entry).0);
    assert!(replaced::of_entry(&library.db, &entry).unwrap().is_empty());
    assert!(leaf_server::archive::cbz::read(&fourth(&library), false).is_ok());
    assert!(bulk.state(&id).unwrap().is_some());
    let beside: Vec<String> = std::fs::read_dir(&folder)
        .unwrap()
        .map(|e| e.unwrap().file_name().to_string_lossy().into_owned())
        .filter(|name| name.ends_with(".leaf-import") || name.ends_with(".part"))
        .collect();
    assert!(beside.is_empty(), "nothing left beside it: {beside:?}");

    let result = bulk
        .commit(&id, &replacing_four(), &[], &[], DEFAULT_PROFILE)
        .unwrap();
    assert_eq!(1, result.installed);
    assert_eq!(four, std::fs::read(fourth(&library)).unwrap());
    assert_eq!(old, std::fs::read(kept(&library, &id)).unwrap());
}

#[test]
fn the_same_commit_twice_installs_nothing_more() {
    let (library, series) = a_long_fourth();
    let entry = library.entry(4);
    let old = std::fs::read(fourth(&library)).unwrap();
    let bulk = library.bulk();
    let four = volume_bytes(4, 130, None);
    let six = volume_bytes(6, 2, None);
    // A sixth volume announced and never sent keeps the session open.
    let opened = library
        .open(&aimed(
            TargetKind::Series,
            &series,
            "Blue Period",
            vec![
                file("bp 04.cbz", &four, 4, 130),
                file("bp 06.cbz", &six, 6, 2),
            ],
        ))
        .unwrap();
    bulk.receive(&opened.id, "bp 04.cbz", 0, &four, SENT_CEILING)
        .unwrap();

    let first = bulk
        .commit(&opened.id, &replacing_four(), &[], &[], DEFAULT_PROFILE)
        .unwrap();
    let second = bulk
        .commit(&opened.id, &replacing_four(), &[], &[], DEFAULT_PROFILE)
        .unwrap();

    assert_eq!((1, 0), (first.installed, second.installed));
    assert!(second.open);
    assert!(second.replaced.is_empty(), "{:?}", second.replaced);
    assert_eq!(four, std::fs::read(fourth(&library)).unwrap());
    assert_eq!(old, std::fs::read(kept(&library, &opened.id)).unwrap());
    assert_eq!(1, replaced::of_entry(&library.db, &entry).unwrap().len());
}

#[test]
fn a_volume_absent_from_the_drop_is_still_there_after_commit() {
    let (library, series) = a_long_fourth();
    let (bulk, id, _) = a_new_fourth(&library, &series);
    let result = bulk
        .commit(&id, &replacing_four(), &[], &[], DEFAULT_PROFILE)
        .unwrap();
    library.scan();

    assert!(result.orphans.is_empty(), "{:?}", result.orphans);
    for n in [1, 2, 3, 5] {
        let tome = library.root().join(format!("Blue Period/Tome {n:02}.cbz"));
        assert!(tome.is_file(), "{}", tome.display());
    }
    assert_eq!(5, number_of(&library, "SELECT COUNT(*) FROM entry"));
}

#[test]
fn nothing_is_set_aside_or_recorded_for_an_identical_file() {
    let (library, series) = a_long_fourth();
    let bulk = library.bulk();
    let three = std::fs::read(library.root().join("Blue Period/Tome 03.cbz")).unwrap();
    let mut same = file("bp 03.cbz", &three, 3, 2);
    same.checksum = Some(checksum_of(&three));
    let opened = library
        .open(&aimed(
            TargetKind::Series,
            &series,
            "Blue Period",
            vec![same],
        ))
        .unwrap();
    assert_eq!(Outcome::Same, outcome_of(&opened, "bp 03.cbz").outcome);
    bulk.receive(&opened.id, "bp 03.cbz", 0, &three, SENT_CEILING)
        .unwrap();

    let result = bulk
        .commit(
            &opened.id,
            &["bp 03.cbz".to_string()],
            &[],
            &[],
            DEFAULT_PROFILE,
        )
        .unwrap();

    assert_eq!(0, result.installed);
    assert!(result.replaced.is_empty(), "{:?}", result.replaced);
    assert!(!library.dir.path().join("inbox/replaced").exists());
    assert!(replaced::of_entry(&library.db, &library.entry(3))
        .unwrap()
        .is_empty());
}

#[test]
fn the_rescan_after_a_series_reimport_is_aimed_at_its_work() {
    let (library, _) = Library::terres_d_arran();
    let elfes = library.id("SELECT id FROM edition WHERE path LIKE '%/Elfes'");
    let bulk = library.bulk();
    let one = volume_bytes(1, 3, None);
    let opened = library
        .open(&aimed(
            TargetKind::Series,
            &elfes,
            "Terres d'Arran",
            vec![file("e01.cbz", &one, 1, 3)],
        ))
        .unwrap();
    bulk.receive(&opened.id, "e01.cbz", 0, &one, SENT_CEILING)
        .unwrap();
    let result = bulk
        .commit(
            &opened.id,
            &["e01.cbz".to_string()],
            &[],
            &[],
            DEFAULT_PROFILE,
        )
        .unwrap();
    assert_eq!(1, result.installed);

    let aim = aimed_after_import(&bulk.target_of(&result.root), &result)
        .expect("a rescan aimed at the series");
    assert_eq!(library.root().join("Terres d'Arran/Elfes"), aim);
    let report = Scanner::new(Arc::clone(&library.db), true)
        .rescan_work(&aim)
        .unwrap();
    assert_eq!(1, report.entries);
}

/// Whether this test runs as root, which no folder permission stops.
fn as_root() -> bool {
    // SAFETY: geteuid has no preconditions and cannot fail.
    unsafe { libc::geteuid() == 0 }
}

fn owned_volumes(library: &Library, series: &str) -> i64 {
    Repository::new(&library.db)
        .one_series(series, DEFAULT_PROFILE, &Ceiling::NONE)
        .unwrap()
        .expect("the series")
        .owned_volumes
}

#[test]
fn a_place_past_the_end_of_a_shorter_file_comes_back_to_its_last_page() {
    let (library, series) = a_long_fourth();
    let entry = library.entry(4);
    let ines = a_reader(&library, "Inès", "sauge");
    place(&library, &ines, &entry, 112, None);
    let bulk = library.bulk();
    let short = volume_bytes(4, 100, None);
    let opened = library
        .open(&aimed(
            TargetKind::Series,
            &series,
            "Blue Period",
            vec![file("bp 04.cbz", &short, 4, 100)],
        ))
        .unwrap();
    bulk.receive(&opened.id, "bp 04.cbz", 0, &short, SENT_CEILING)
        .unwrap();

    bulk.commit(&opened.id, &replacing_four(), &[], &[], &ines)
        .unwrap();
    library.scan();

    assert_eq!(99, progress_of(&library, &ines, &entry).0);
}

/// A RAR named `.cbz`, ticked as a replacement: read only once the old file was set aside,
/// it stopped the whole commit, and every one after it.
#[test]
fn an_unreadable_replacement_is_corrupt_and_the_rest_of_the_drop_installs() {
    let (library, series) = a_long_fourth();
    let entry = library.entry(4);
    let bulk = library.bulk();
    let old = std::fs::read(fourth(&library)).unwrap();
    let rar = b"Rar!\x1a\x07\x00 and nothing a zip reader knows".to_vec();
    let six = volume_bytes(6, 2, None);
    let opened = library
        .open(&aimed(
            TargetKind::Series,
            &series,
            "Blue Period",
            vec![file("bp 04.cbz", &rar, 4, 3), file("bp 06.cbz", &six, 6, 2)],
        ))
        .unwrap();
    let lands = outcome_of(&opened, "bp 06.cbz")
        .lands
        .clone()
        .expect("a landing");
    bulk.receive(&opened.id, "bp 04.cbz", 0, &rar, SENT_CEILING)
        .unwrap();
    bulk.receive(&opened.id, "bp 06.cbz", 0, &six, SENT_CEILING)
        .unwrap();

    let result = bulk
        .commit(&opened.id, &replacing_four(), &[], &[], DEFAULT_PROFILE)
        .unwrap();

    assert_eq!(vec!["bp 04.cbz".to_string()], result.corrupt);
    assert_eq!(1, result.installed);
    assert!(library.root().join("Blue Period").join(&lands).is_file());
    assert_eq!(old, std::fs::read(fourth(&library)).unwrap());
    assert!(!kept(&library, &opened.id).exists());
    assert!(replaced::of_entry(&library.db, &entry).unwrap().is_empty());
}

/// Two sessions replacing one volume at once both linked the original aside, and the
/// second rename put its file over the first's with no copy of it anywhere.
#[test]
fn two_commits_replacing_one_volume_lose_neither() {
    for _ in 0..5 {
        let (library, series) = a_long_fourth();
        let entry = library.entry(4);
        let bulk = library.bulk();
        let old = std::fs::read(fourth(&library)).unwrap();
        let versions = [
            (volume_bytes(4, 130, Some("A")), 130),
            (volume_bytes(4, 140, Some("B")), 140),
        ];
        let ids: Vec<String> = versions
            .iter()
            .map(|(bytes, pages)| {
                let opened = library
                    .open(&aimed(
                        TargetKind::Series,
                        &series,
                        "Blue Period",
                        vec![file("bp 04.cbz", bytes, 4, *pages)],
                    ))
                    .unwrap();
                bulk.receive(&opened.id, "bp 04.cbz", 0, bytes, SENT_CEILING)
                    .unwrap();
                opened.id
            })
            .collect();

        let barrier = std::sync::Barrier::new(2);
        let results: Vec<_> = std::thread::scope(|scope| {
            let handles: Vec<_> = ids
                .iter()
                .map(|id| {
                    let (bulk, barrier) = (&bulk, &barrier);
                    scope.spawn(move || {
                        barrier.wait();
                        bulk.commit(id, &replacing_four(), &[], &[], DEFAULT_PROFILE)
                    })
                })
                .collect();
            handles.into_iter().map(|h| h.join().unwrap()).collect()
        });
        for result in &results {
            assert!(result.is_ok(), "{result:?}");
        }

        // Every version is somewhere: in place, set aside, or still in its session.
        let mut found = vec![std::fs::read(fourth(&library)).unwrap()];
        for id in &ids {
            let waiting = library.session(id).join("files/bp 04.cbz");
            for at in [kept(&library, id), waiting] {
                if let Ok(bytes) = std::fs::read(at) {
                    found.push(bytes);
                }
            }
        }
        for (n, version) in [&old, &versions[0].0, &versions[1].0].iter().enumerate() {
            assert!(found.contains(version), "version {n} is lost");
        }
        for row in replaced::of_entry(&library.db, &entry).unwrap() {
            let aside = library.dir.path().join("inbox").join(&row.kept_at);
            assert_eq!(std::fs::metadata(aside).unwrap().len(), row.old_size);
        }
    }
}

/// Cut between the old file set aside and the new one in place, then abandoned: the old
/// volume is still the volume, so nobody's place may have moved and nothing says it was
/// replaced.
#[test]
fn a_commit_cut_before_the_rename_and_abandoned_moves_no_one() {
    let (library, series) = a_long_fourth();
    let entry = library.entry(4);
    let ines = a_reader(&library, "Inès", "sauge");
    place(&library, &ines, &entry, 112, None);
    let old = std::fs::read(fourth(&library)).unwrap();
    let (_, id, _) = a_new_fourth(&library, &series);

    let cut = library.bulk().cut_at(Cut::BeforeRename);
    assert!(cut
        .commit(&id, &replacing_four(), &[], &replacing_four(), &ines)
        .is_err());
    library.bulk().abandon(&id).unwrap();

    assert_eq!(old, std::fs::read(fourth(&library)).unwrap());
    assert_eq!(112, progress_of(&library, &ines, &entry).0);
    assert!(replaced::of_entry(&library.db, &entry).unwrap().is_empty());
    assert!(!kept(&library, &id).exists());
    assert!(!library.session(&id).exists());
}

/// Cut once the new file is in place, before the index heard of it: the next commit says
/// what was replaced and settles the places.
#[test]
fn a_commit_cut_after_the_rename_is_recorded_by_the_next() {
    let (library, series) = a_long_fourth();
    let entry = library.entry(4);
    let ines = a_reader(&library, "Inès", "sauge");
    place(&library, &ines, &entry, 112, None);
    let old = std::fs::read(fourth(&library)).unwrap();
    let (_, id, four) = a_new_fourth(&library, &series);

    let cut = library.bulk().cut_at(Cut::AfterRename);
    assert!(cut
        .commit(&id, &replacing_four(), &[], &replacing_four(), &ines)
        .is_err());
    let result = library
        .bulk()
        .commit(&id, &replacing_four(), &[], &replacing_four(), &ines)
        .unwrap();

    assert_eq!(four, std::fs::read(fourth(&library)).unwrap());
    assert_eq!(old, std::fs::read(kept(&library, &id)).unwrap());
    assert_eq!(1, result.replaced.len(), "{:?}", result.replaced);
    assert_eq!(1, replaced::of_entry(&library.db, &entry).unwrap().len());
    assert_eq!(0, progress_of(&library, &ines, &entry).0);
}

/// Cut once the new file is in place, then abandoned: the replacement happened, so it is
/// recorded and settled rather than forgotten with the session.
#[test]
fn a_commit_cut_after_the_rename_and_abandoned_is_still_recorded() {
    let (library, series) = a_long_fourth();
    let entry = library.entry(4);
    let ines = a_reader(&library, "Inès", "sauge");
    place(&library, &ines, &entry, 112, None);
    let old = std::fs::read(fourth(&library)).unwrap();
    let (_, id, four) = a_new_fourth(&library, &series);

    let cut = library.bulk().cut_at(Cut::AfterRename);
    assert!(cut
        .commit(&id, &replacing_four(), &[], &replacing_four(), &ines)
        .is_err());
    library.bulk().abandon(&id).unwrap();

    assert_eq!(four, std::fs::read(fourth(&library)).unwrap());
    assert_eq!(old, std::fs::read(kept(&library, &id)).unwrap());
    assert_eq!(1, replaced::of_entry(&library.db, &entry).unwrap().len());
    assert_eq!(0, progress_of(&library, &ines, &entry).0);
}

/// The library reached through a link: the index holds its files under the path it was
/// scanned by, and a comparison of text found no entry — the old file set aside, nothing
/// recorded, nobody's place settled.
#[test]
fn a_volume_reached_through_a_linked_library_is_recorded() {
    let (library, _) = a_long_fourth();
    let entry = library.entry(4);
    let linked = library.dir.path().join("linked");
    std::os::unix::fs::symlink(library.root(), &linked).unwrap();
    let bulk = BulkImport::new(
        &library.dir.path().join("inbox"),
        &linked,
        Arc::clone(&library.db),
    );
    let four = volume_bytes(4, 130, None);
    let opened = bulk
        .open(
            &ImportRequest {
                root: "Blue Period".to_string(),
                files: vec![ManifestFile {
                    path: "Tome 04.cbz".to_string(),
                    size: four.len() as u64,
                    ..ManifestFile::default()
                }],
                ..ImportRequest::default()
            },
            DEFAULT_PROFILE,
            &Ceiling::NONE,
        )
        .unwrap();
    bulk.receive(&opened.id, "Tome 04.cbz", 0, &four, SENT_CEILING)
        .unwrap();

    let result = bulk
        .commit(
            &opened.id,
            &["Tome 04.cbz".to_string()],
            &[],
            &[],
            DEFAULT_PROFILE,
        )
        .unwrap();

    assert_eq!(1, result.installed);
    assert_eq!(Some(entry.clone()), result.replaced[0].entry_id);
    assert_eq!(1, replaced::of_entry(&library.db, &entry).unwrap().len());
}

/// A volume on the disk the index has not read yet has no entry to record a replacement
/// against: replaced, it would be a replacement nothing says happened.
#[test]
fn a_volume_the_index_does_not_hold_is_not_replaced_unrecorded() {
    let (library, _) = a_long_fourth();
    let stray = library.root().join("Blue Period/Tome 13.cbz");
    let before = volume_bytes(13, 2, None);
    std::fs::write(&stray, &before).unwrap();
    let bulk = library.bulk();
    let after = volume_bytes(13, 3, None);
    let opened = library
        .open(&ImportRequest {
            root: "Blue Period".to_string(),
            files: vec![ManifestFile {
                path: "Tome 13.cbz".to_string(),
                size: after.len() as u64,
                ..ManifestFile::default()
            }],
            ..ImportRequest::default()
        })
        .unwrap();
    bulk.receive(&opened.id, "Tome 13.cbz", 0, &after, SENT_CEILING)
        .unwrap();

    let result = bulk
        .commit(
            &opened.id,
            &["Tome 13.cbz".to_string()],
            &[],
            &[],
            DEFAULT_PROFILE,
        )
        .unwrap();

    assert_eq!(vec!["Tome 13.cbz".to_string()], result.pending);
    assert_eq!(before, std::fs::read(&stray).unwrap());
    assert!(result.replaced.is_empty(), "{:?}", result.replaced);
    assert!(!library
        .dir
        .path()
        .join("inbox/replaced")
        .join(&opened.id)
        .exists());
}

// ------------------------------------------------------------------ another hand between two steps

/// Another session bringing the fourth volume again, announced and received whole.
fn another_fourth(library: &Library, series: &str, pages: u8) -> (String, Vec<u8>) {
    let bytes = volume_bytes(4, pages, Some("Encore"));
    let opened = library
        .open(&aimed(
            TargetKind::Series,
            series,
            "Blue Period",
            vec![file("bp 04.cbz", &bytes, 4, u32::from(pages))],
        ))
        .unwrap();
    library
        .bulk()
        .receive(&opened.id, "bp 04.cbz", 0, &bytes, SENT_CEILING)
        .unwrap();
    (opened.id, bytes)
}

/// Every file that holds a version of the fourth volume: in place, set aside by either
/// session, or still waiting in one.
fn every_fourth(library: &Library, ids: &[&str]) -> Vec<Vec<u8>> {
    let mut found = vec![std::fs::read(fourth(library)).unwrap()];
    for id in ids {
        let waiting = library.session(id).join("files/bp 04.cbz");
        for at in [kept(library, id), waiting] {
            if let Ok(bytes) = std::fs::read(at) {
                found.push(bytes);
            }
        }
    }
    found
}

fn nothing_lost(library: &Library, ids: &[&str], versions: &[&Vec<u8>]) {
    let found = every_fourth(library, ids);
    for (n, version) in versions.iter().enumerate() {
        assert!(found.contains(version), "version {n} is lost");
    }
    for row in replaced::of_entry(&library.db, &library.entry(4)).unwrap() {
        let aside = library.dir.path().join("inbox").join(&row.kept_at);
        assert_eq!(
            std::fs::metadata(&aside).unwrap().len(),
            row.old_size,
            "{row:?}"
        );
    }
}

fn cut(library: &Library, id: &str, at: Cut) {
    assert!(library
        .bulk()
        .cut_at(at)
        .commit(id, &replacing_four(), &[], &[], DEFAULT_PROFILE)
        .is_err());
}

fn commit_four(library: &Library, id: &str) -> leaf_server::api::bulk_import::ImportResult {
    library
        .bulk()
        .commit(id, &replacing_four(), &[], &[], DEFAULT_PROFILE)
        .unwrap()
}

/// The aside folder of the first session held the only copy of the original once the
/// second had replaced the first's file; abandoning the first erased it.
#[test]
fn abandoning_a_session_cut_after_its_rename_keeps_the_original_another_replaced() {
    let (library, series) = a_long_fourth();
    let old = std::fs::read(fourth(&library)).unwrap();
    let (_, first, a) = a_new_fourth(&library, &series);
    cut(&library, &first, Cut::AfterRename);
    let (second, b) = another_fourth(&library, &series, 140);
    assert_eq!(1, commit_four(&library, &second).installed);

    library.bulk().abandon(&first).unwrap();

    nothing_lost(&library, &[&first, &second], &[&old, &a, &b]);
}

/// Resumed after another session replaced the volume, the first renamed its file over the
/// second's, which nothing had set aside.
#[test]
fn a_session_cut_before_its_rename_resumed_after_another_replaced_the_volume_waits() {
    let (library, series) = a_long_fourth();
    let old = std::fs::read(fourth(&library)).unwrap();
    let (_, first, a) = a_new_fourth(&library, &series);
    cut(&library, &first, Cut::BeforeRename);
    let (second, b) = another_fourth(&library, &series, 140);
    assert_eq!(1, commit_four(&library, &second).installed);

    let resumed = commit_four(&library, &first);

    assert_eq!(vec!["bp 04.cbz".to_string()], resumed.pending);
    assert_eq!(b, std::fs::read(fourth(&library)).unwrap());
    nothing_lost(&library, &[&first, &second], &[&old, &a, &b]);
}

#[test]
fn a_session_cut_after_its_rename_resumed_after_another_replaced_the_volume_waits() {
    let (library, series) = a_long_fourth();
    let old = std::fs::read(fourth(&library)).unwrap();
    let (_, first, a) = a_new_fourth(&library, &series);
    cut(&library, &first, Cut::AfterRename);
    let (second, b) = another_fourth(&library, &series, 140);
    assert_eq!(1, commit_four(&library, &second).installed);

    let resumed = commit_four(&library, &first);

    assert_eq!(vec!["bp 04.cbz".to_string()], resumed.pending);
    assert_eq!(b, std::fs::read(fourth(&library)).unwrap());
    nothing_lost(&library, &[&first, &second], &[&old, &a, &b]);
    library.bulk().abandon(&first).unwrap();
    nothing_lost(&library, &[&first, &second], &[&old, &a, &b]);
}

/// A file put on the volume by hand between the cut and the resume is nobody's to replace.
#[test]
fn a_session_resumed_after_a_hand_replaced_the_volume_waits() {
    let (library, series) = a_long_fourth();
    let old = std::fs::read(fourth(&library)).unwrap();
    let (_, first, a) = a_new_fourth(&library, &series);
    cut(&library, &first, Cut::BeforeRename);
    let by_hand = volume_bytes(4, 7, Some("À la main"));
    let elsewhere = library.dir.path().join("by hand.cbz");
    std::fs::write(&elsewhere, &by_hand).unwrap();
    std::fs::rename(&elsewhere, fourth(&library)).unwrap();

    let resumed = commit_four(&library, &first);

    assert_eq!(vec!["bp 04.cbz".to_string()], resumed.pending);
    assert_eq!(by_hand, std::fs::read(fourth(&library)).unwrap());
    nothing_lost(&library, &[&first], &[&old, &a, &by_hand]);
}

/// Cut once the index was told, before the session forgot it: replayed, the reset sent
/// back to the first page a reader who had read on since.
#[test]
fn a_replacement_recorded_twice_moves_no_place_twice() {
    let (library, series) = a_long_fourth();
    let entry = library.entry(4);
    let ines = a_reader(&library, "Inès", "sauge");
    place(&library, &ines, &entry, 112, None);
    let (_, id, _) = a_new_fourth(&library, &series);
    assert!(library
        .bulk()
        .cut_at(Cut::AfterConsign)
        .commit(&id, &replacing_four(), &[], &replacing_four(), &ines)
        .is_err());
    assert_eq!(0, progress_of(&library, &ines, &entry).0);
    place(&library, &ines, &entry, 5, None);

    library
        .bulk()
        .commit(&id, &replacing_four(), &[], &replacing_four(), &ines)
        .unwrap();

    assert_eq!(5, progress_of(&library, &ines, &entry).0);
    assert_eq!(1, replaced::of_entry(&library.db, &entry).unwrap().len());
}

/// Cut with the old file linked aside and nothing written of it: the link is the file still
/// in place, so the next commit uses it.
#[test]
fn a_commit_cut_after_the_link_finishes_on_the_next() {
    let (library, series) = a_long_fourth();
    let old = std::fs::read(fourth(&library)).unwrap();
    let (_, id, four) = a_new_fourth(&library, &series);
    cut(&library, &id, Cut::AfterLink);

    let result = commit_four(&library, &id);

    assert_eq!(1, result.installed);
    assert_eq!(four, std::fs::read(fourth(&library)).unwrap());
    assert_eq!(old, std::fs::read(kept(&library, &id)).unwrap());
    assert_eq!(
        1,
        replaced::of_entry(&library.db, &library.entry(4))
            .unwrap()
            .len()
    );
}

#[test]
fn a_commit_cut_after_writing_what_it_will_record_and_abandoned_leaves_nothing() {
    let (library, series) = a_long_fourth();
    let old = std::fs::read(fourth(&library)).unwrap();
    let (_, id, _) = a_new_fourth(&library, &series);
    cut(&library, &id, Cut::AfterSettling);

    library.bulk().abandon(&id).unwrap();

    assert_eq!(old, std::fs::read(fourth(&library)).unwrap());
    assert!(replaced::of_entry(&library.db, &library.entry(4))
        .unwrap()
        .is_empty());
    assert!(!kept(&library, &id).exists());
    assert!(!library.session(&id).exists());
}

/// What a commit left to finish cannot be read: the session is dropped all the same, and
/// the volume is whole — the copy set aside goes only for being the file still in place.
#[test]
fn a_session_whose_replacement_under_way_cannot_be_read_can_still_be_abandoned() {
    let (library, series) = a_long_fourth();
    let old = std::fs::read(fourth(&library)).unwrap();
    let (_, id, _) = a_new_fourth(&library, &series);
    cut(&library, &id, Cut::AfterSettling);
    std::fs::write(
        library.session(&id).join("_settling/Tome 04.cbz.json"),
        b"not json",
    )
    .unwrap();

    library.bulk().abandon(&id).unwrap();

    assert!(!library.session(&id).exists());
    assert_eq!(old, std::fs::read(fourth(&library)).unwrap());
    if let Ok(aside) = std::fs::read(kept(&library, &id)) {
        assert_eq!(old, aside);
    }
}

/// The old file touched — a backup tool, a `touch` — between a cut and the resume. Its date
/// was part of the verdict, and the resume called its own volume another's, for good.
#[test]
fn a_volume_touched_after_a_cut_before_the_rename_is_still_this_sessions_to_replace() {
    let (library, series) = a_long_fourth();
    let old = std::fs::read(fourth(&library)).unwrap();
    let (_, id, four) = a_new_fourth(&library, &series);
    cut(&library, &id, Cut::AfterSettling);
    touch(&fourth(&library));

    let resumed = commit_four(&library, &id);

    assert!(resumed.pending.is_empty(), "{:?}", resumed.pending);
    assert_eq!(1, resumed.installed);
    assert_eq!(four, std::fs::read(fourth(&library)).unwrap());
    assert_eq!(old, std::fs::read(kept(&library, &id)).unwrap());
}

#[test]
fn a_volume_touched_after_a_cut_after_the_rename_is_still_recorded() {
    let (library, series) = a_long_fourth();
    let (_, id, four) = a_new_fourth(&library, &series);
    cut(&library, &id, Cut::AfterRename);
    touch(&fourth(&library));

    let resumed = commit_four(&library, &id);

    assert!(resumed.pending.is_empty(), "{:?}", resumed.pending);
    assert_eq!(four, std::fs::read(fourth(&library)).unwrap());
    assert_eq!(1, resumed.replaced.len(), "{:?}", resumed.replaced);
    assert_eq!(
        1,
        replaced::of_entry(&library.db, &library.entry(4))
            .unwrap()
            .len()
    );
}

fn touch(file: &Path) {
    let later = std::time::SystemTime::now() + std::time::Duration::from_secs(3600);
    std::fs::File::options()
        .write(true)
        .open(file)
        .unwrap()
        .set_modified(later)
        .unwrap();
}

/// Recorded before the cut, the replacement is done, whatever replaced the volume since.
#[test]
fn a_replacement_recorded_before_another_replaced_the_volume_is_done() {
    let (library, series) = a_long_fourth();
    let (_, first, _) = a_new_fourth(&library, &series);
    cut(&library, &first, Cut::AfterConsign);
    let (second, _) = another_fourth(&library, &series, 140);
    assert_eq!(1, commit_four(&library, &second).installed);

    let resumed = commit_four(&library, &first);

    assert!(resumed.pending.is_empty(), "{:?}", resumed.pending);
    assert!(!resumed.open);
}

/// Two sessions bringing the same file: the second to arrive found its own bytes in place.
#[test]
fn a_session_resumed_after_another_brought_the_same_file_is_not_in_conflict() {
    let (library, series) = a_long_fourth();
    let (_, first, four) = a_new_fourth(&library, &series);
    cut(&library, &first, Cut::BeforeRename);
    let second = library
        .open(&aimed(
            TargetKind::Series,
            &series,
            "Blue Period",
            vec![file("bp 04.cbz", &four, 4, 130)],
        ))
        .unwrap();
    library
        .bulk()
        .receive(&second.id, "bp 04.cbz", 0, &four, SENT_CEILING)
        .unwrap();
    assert_eq!(1, commit_four(&library, &second.id).installed);

    let resumed = commit_four(&library, &first);

    assert!(resumed.pending.is_empty(), "{:?}", resumed.pending);
    assert_eq!(four, std::fs::read(fourth(&library)).unwrap());
}

/// The index refusing the record while an abandon finishes a replacement: the abandon said
/// it worked and the session went, and with it the only thing that knew the volume had been
/// replaced and whose places to settle.
#[test]
fn an_abandon_the_index_refuses_keeps_the_session_to_try_again() {
    let (library, series) = a_long_fourth();
    let entry = library.entry(4);
    let ines = a_reader(&library, "Inès", "sauge");
    place(&library, &ines, &entry, 112, None);
    let (_, id, _) = a_new_fourth(&library, &series);
    assert!(library
        .bulk()
        .cut_at(Cut::AfterRename)
        .commit(&id, &replacing_four(), &[], &replacing_four(), &ines)
        .is_err());
    library
        .db
        .write(|cx| {
            cx.execute(
                "CREATE TRIGGER refuse BEFORE INSERT ON replaced_file
                 BEGIN SELECT RAISE(ABORT, 'refused'); END",
                [],
            )
        })
        .unwrap();

    assert!(library.bulk().abandon(&id).is_err());
    assert!(library.session(&id).exists());

    library
        .db
        .write(|cx| cx.execute("DROP TRIGGER refuse", []))
        .unwrap();
    library.bulk().abandon(&id).unwrap();

    assert!(!library.session(&id).exists());
    assert_eq!(1, replaced::of_entry(&library.db, &entry).unwrap().len());
    assert_eq!(0, progress_of(&library, &ines, &entry).0);
}

// ------------------------------------------------------------------ a universe, a series' details

/// A sidecar the library holds, written by hand where a scan will find it.
fn declared(library: &Library, at: &str, json: &str) {
    let path = library.root().join(at);
    std::fs::create_dir_all(path.parent().unwrap()).unwrap();
    std::fs::write(path, json).unwrap();
}

fn sidecar(path: &str, json: &str) -> ManifestSidecar {
    ManifestSidecar {
        path: path.to_string(),
        json: json.to_string(),
    }
}

impl Library {
    /// The universe « Dragon Ball », holding « Dragon Ball » (two volumes) and « Dragon Ball
    /// GT » (one); and « Dragon Ball Super », one volume, at the root, in no universe.
    fn dragon_ball() -> (Library, String) {
        let library = Library::new();
        declared(
            &library,
            "Dragon Ball/universe.json",
            r#"{"leaf":1,"name":"Dragon Ball"}"#,
        );
        for (folder, count) in [
            ("Dragon Ball/Dragon Ball", 2),
            ("Dragon Ball/Dragon Ball GT", 1),
            ("Dragon Ball Super", 1),
        ] {
            let at = library.root().join(folder);
            std::fs::create_dir_all(&at).unwrap();
            for n in 1..=count {
                std::fs::write(
                    at.join(format!("Tome {n:02}.cbz")),
                    volume_bytes(n, 2, None),
                )
                .unwrap();
            }
        }
        declared(
            &library,
            "Dragon Ball Super/work.json",
            r#"{"leaf":1,"title":"Dragon Ball Super"}"#,
        );
        library.scan();
        let id = library.id("SELECT id FROM universe");
        (library, id)
    }

    fn work_at(&self, folder: &str) -> String {
        self.id(&format!(
            "SELECT id FROM work WHERE path = '{}'",
            self.root().join(folder).display()
        ))
    }
}

/// The value the library holds is what a reader compares the arriving one with: « éditeur,
/// nombre de tomes » says two fields differ and not that the library says « Pika » where
/// the file says « Pika Édition », which is the whole decision.
#[test]
fn a_changed_declaration_says_what_the_library_holds_for_each_key() {
    let library = Library::new();
    declared(
        &library,
        "Blue Period/work.json",
        r#"{"leaf":1,"title":"Blue Period","publisher":"Pika"}"#,
    );
    let request = ImportRequest {
        root: "Blue Period".to_string(),
        sidecars: vec![sidecar(
            "work.json",
            r#"{"leaf":1,"title":"Blue Period","publisher":"Pika Édition","volumeCount":14}"#,
        )],
        ..ImportRequest::default()
    };

    let opened = library.open(&request).unwrap();

    let said = opened.declarations.first().expect("a declaration");
    assert_eq!(vec!["publisher", "volumeCount"], said.differs);
    let mut present = serde_json::Map::new();
    present.insert("publisher".to_string(), serde_json::json!("Pika"));
    assert_eq!(
        present, said.present,
        "a key the library lacks is not in it"
    );
}

fn dragon_ball_drop() -> Vec<ManifestFile> {
    let one = volume_bytes(1, 2, None);
    let two = volume_bytes(2, 2, None);
    vec![
        file("Dragon Ball/Tome 01.cbz", &one, 1, 2),
        file("Dragon Ball/Tome 02.cbz", &two, 2, 2),
        file("Dragon Ball Z/Tome 01.cbz", &one, 1, 2),
        file("Dragon Ball Super/Tome 01.cbz", &one, 1, 2),
    ]
}

#[test]
fn a_universe_reimport_names_its_series_added_moved_absent_and_identical() {
    let (library, universe) = Library::dragon_ball();
    let mut request = aimed(
        TargetKind::Universe,
        &universe,
        "Dragon Ball",
        dragon_ball_drop(),
    );
    request.sidecars = vec![
        sidecar(
            "Dragon Ball Z/work.json",
            r#"{"leaf":1,"title":"Dragon Ball Z"}"#,
        ),
        sidecar(
            "Dragon Ball Super/work.json",
            r#"{"leaf":1,"title":"Dragon Ball Super"}"#,
        ),
    ];

    let opened = library.open(&request).unwrap();

    assert_eq!(1, opened.moves.len(), "{:?}", opened.moves);
    let super_ = &opened.moves[0];
    assert_eq!(library.work_at("Dragon Ball Super"), super_.work_id);
    assert_eq!("Dragon Ball Super", super_.at);
    assert_eq!(Matched::Title, super_.matched);

    let created: Vec<(&str, &str)> = opened
        .creates
        .iter()
        .map(|c| (c.kind.as_str(), c.at.as_str()))
        .collect();
    assert_eq!(vec![("WORK", "Dragon Ball Z")], created);

    let absent: Vec<&str> = opened
        .absent_series
        .iter()
        .map(|s| s.name.as_str())
        .collect();
    assert_eq!(vec!["Dragon Ball GT"], absent);
    assert_eq!(1, opened.absent_series[0].volumes);
    assert_eq!(
        library.work_at("Dragon Ball/Dragon Ball GT"),
        opened.absent_series[0].work_id
    );

    let there: HashSet<&str> = opened.already_there.iter().map(String::as_str).collect();
    assert!(there.contains("Dragon Ball/Tome 01.cbz"), "{there:?}");
    assert!(there.contains("Dragon Ball/Tome 02.cbz"), "{there:?}");
    assert!(opened.resembles.is_empty(), "only without a target");
}

/// A volume added to a series is named as the rename engine names it, from the index — the
/// title the series declares — and not from the folder the series happens to sit in.
#[test]
fn a_volume_added_to_a_series_is_named_by_its_title_not_its_folder() {
    let library = Library::new();
    let folder = library.root().join("old-folder");
    std::fs::create_dir_all(&folder).unwrap();
    std::fs::write(folder.join("Tome 01.cbz"), volume_bytes(1, 2, None)).unwrap();
    declared(
        &library,
        "old-folder/work.json",
        r#"{"leaf":1,"title":"Real Title"}"#,
    );
    library.scan();
    let series = library.id("SELECT id FROM edition");
    let five = volume_bytes(5, 2, None);
    let request = aimed(
        TargetKind::Series,
        &series,
        "Whatever",
        vec![file("other/Tome 05.cbz", &five, 5, 2)],
    );

    let opened = library.open(&request).unwrap();

    let landed: Vec<_> = opened.lands_as.iter().map(|l| l.file.clone()).collect();
    assert_eq!(vec!["real-title_tome_005.cbz".to_string()], landed);
}

/// « DBGT » holding a `work.json` that calls itself « Dragon Ball GT »: the series the
/// universe holds under that title, and not a second one beside it.
#[test]
fn a_series_of_the_universe_dropped_under_another_folder_name_lands_in_its_own_folder() {
    let (library, universe) = Library::dragon_ball();
    let two = volume_bytes(2, 2, None);
    let mut request = aimed(
        TargetKind::Universe,
        &universe,
        "Dragon Ball",
        vec![file("DBGT/Tome 02.cbz", &two, 2, 2)],
    );
    request.sidecars = vec![sidecar(
        "DBGT/work.json",
        r#"{"leaf":1,"title":"Dragon Ball GT"}"#,
    )];

    let opened = library.open(&request).unwrap();

    assert!(opened.creates.is_empty(), "{:?}", opened.creates);
    assert!(opened.moves.is_empty(), "{:?}", opened.moves);
    let answer = outcome_of(&opened, "DBGT/Tome 02.cbz");
    assert_eq!(Outcome::Add, answer.outcome);
    assert!(opened
        .absent_series
        .iter()
        .all(|s| s.name != "Dragon Ball GT"));

    let bulk = library.bulk();
    bulk.receive(&opened.id, "DBGT/Tome 02.cbz", 0, &two, SENT_CEILING)
        .unwrap();
    bulk.commit(&opened.id, &[], &[], &[], DEFAULT_PROFILE)
        .unwrap();
    let gt = library.root().join("Dragon Ball/Dragon Ball GT");
    assert_eq!(
        two,
        std::fs::read(gt.join("dragon-ball-gt_tome_002.cbz")).unwrap()
    );
    assert!(!library.root().join("Dragon Ball/DBGT").exists());
}

/// Two works called « Dragon Ball », one in the universe and one at the root: the identity
/// the drop carries says which, and the title it shares with the other decides nothing.
#[test]
fn a_series_matched_by_identity_is_never_matched_by_title_instead() {
    let (library, universe) = Library::dragon_ball();
    let kana = library.root().join("Dragon Ball (Kana)");
    std::fs::create_dir_all(&kana).unwrap();
    std::fs::write(kana.join("Tome 01.cbz"), volume_bytes(1, 2, None)).unwrap();
    declared(
        &library,
        "Dragon Ball (Kana)/work.json",
        r#"{"leaf":1,"title":"Dragon Ball"}"#,
    );
    library.scan();
    let id = library.work_at("Dragon Ball (Kana)");
    let one = volume_bytes(1, 2, None);
    let mut request = aimed(
        TargetKind::Universe,
        &universe,
        "Dragon Ball",
        vec![file("Kana/Tome 01.cbz", &one, 1, 2)],
    );
    request.sidecars = vec![sidecar(
        "Kana/work.json",
        &format!(r#"{{"leaf":1,"title":"Dragon Ball","id":"{id}"}}"#),
    )];

    let opened = library.open(&request).unwrap();

    assert_eq!(1, opened.moves.len(), "{:?}", opened.moves);
    assert_eq!(id, opened.moves[0].work_id);
    assert_eq!(Matched::Identity, opened.moves[0].matched);
    assert!(
        opened.volumes.is_empty(),
        "not compared with the universe's own « Dragon Ball »: {:?}",
        opened.volumes
    );
}

/// « blue period » beside « Blue Period » is two folders on the disk and, almost always, one
/// series typed twice. Said before a byte moves, because afterwards it is two series.
#[test]
fn a_new_series_named_like_one_the_library_holds_is_said_to_resemble_it() {
    let (library, series) = Library::blue_period();
    let thirteen = volume_bytes(13, 2, None);
    let dropped = |root: &str| ImportRequest {
        root: root.to_string(),
        files: vec![file("Tome 13.cbz", &thirteen, 13, 2)],
        ..ImportRequest::default()
    };

    let opened = library.open(&dropped("blue period")).unwrap();

    assert_eq!(1, opened.resembles.len(), "{:?}", opened.resembles);
    let like = &opened.resembles[0];
    assert_eq!("", like.at, "the folder announced itself");
    assert_eq!(series, like.series_id);
    assert_eq!("Blue Period", like.name);
    assert!(opened.absent_series.is_empty(), "only with a universe");

    let opened = library.open(&dropped("Blue Giant")).unwrap();
    assert!(opened.resembles.is_empty(), "{:?}", opened.resembles);
}

/// Where the universe is and what it holds, every work, what the compared editions hold,
/// the origins: four statements, whatever the number of series the drop brings.
#[test]
fn a_universe_reimport_costs_the_same_for_two_series_as_for_twenty() {
    let (library, universe) = Library::dragon_ball();
    let one = volume_bytes(1, 2, None);
    let cost = |series: usize| {
        // « Dragon Ball », compared volume by volume, and new series for the rest.
        let mut files = dragon_ball_drop();
        files.truncate(2);
        let mut sidecars = Vec::new();
        for n in 1..series {
            files.push(file(&format!("Série {n}/Tome 01.cbz"), &one, 1, 2));
            sidecars.push(sidecar(
                &format!("Série {n}/work.json"),
                &format!(r#"{{"leaf":1,"title":"Série {n}"}}"#),
            ));
        }
        let mut request = aimed(TargetKind::Universe, &universe, "Dragon Ball", files);
        request.sidecars = sidecars;
        let before = library.db.statements();
        library.open(&request).unwrap();
        library.db.statements() - before
    };
    assert_eq!(4, cost(2));
    assert_eq!(4, cost(20));
}

/// « DB Perfect » declaring itself « Perfect Edition »: the edition of « Dragon Ball » the
/// library holds under that name, in its own folder, and its volume compared with that
/// edition's rather than added beside the work's.
#[test]
fn an_edition_dropped_under_another_folder_name_is_found_by_the_name_it_declares() {
    let (library, universe) = Library::dragon_ball();
    let perfect = library.root().join("Dragon Ball/Dragon Ball/Perfect");
    std::fs::create_dir_all(&perfect).unwrap();
    std::fs::write(perfect.join("Tome 01.cbz"), volume_bytes(1, 3, None)).unwrap();
    declared(
        &library,
        "Dragon Ball/Dragon Ball/Perfect/edition.json",
        r#"{"leaf":1,"name":"Perfect Edition"}"#,
    );
    library.scan();
    let one = volume_bytes(1, 4, None);
    let mut request = aimed(
        TargetKind::Universe,
        &universe,
        "Dragon Ball",
        vec![file("Dragon Ball/DB Perfect/t1.cbz", &one, 1, 4)],
    );
    request.sidecars = vec![sidecar(
        "Dragon Ball/DB Perfect/edition.json",
        r#"{"leaf":1,"name":"perfect édition"}"#,
    )];

    let opened = library.open(&request).unwrap();

    let answer = outcome_of(&opened, "Dragon Ball/DB Perfect/t1.cbz");
    assert_eq!(Outcome::Replace, answer.outcome, "{:?}", opened.volumes);
    assert_eq!(Some("Tome 01.cbz"), answer.lands.as_deref());
    assert!(opened.creates.is_empty(), "{:?}", opened.creates);
}

/// A series the library hides from a child is not one a child's drop resembles: named back,
/// it would tell the child what the library does not show it.
#[test]
fn a_new_series_never_resembles_one_hidden_from_who_drops_it() {
    let library = Library::new();
    declared(
        &library,
        "Adulte/work.json",
        r#"{"leaf":1,"title":"Adulte","ageRating":"16+"}"#,
    );
    std::fs::write(
        library.root().join("Adulte/Tome 01.cbz"),
        volume_bytes(1, 2, None),
    )
    .unwrap();
    library.scan();
    let two = volume_bytes(2, 2, None);
    let request = ImportRequest {
        root: "adulte".to_string(),
        files: vec![file("Tome 02.cbz", &two, 2, 2)],
        ..ImportRequest::default()
    };

    let child = library
        .bulk()
        .open(&request, DEFAULT_PROFILE, &Ceiling(Some(12)))
        .unwrap();
    assert!(child.resembles.is_empty(), "{:?}", child.resembles);
    assert_eq!(1, library.open(&request).unwrap().resembles.len());
}

// ------------------------------------------------------------------ replacing a chapter

/// « Blue Period » tome 4, 188 pages: chapter 5 on pages 0–61, 6 on 62–120, 7 on 121–187.
fn a_chaptered_fourth() -> (Library, String) {
    let library = Library::new();
    let folder = library.root().join("Blue Period");
    std::fs::create_dir_all(&folder).unwrap();
    let chapter = |number: f64, start: i32| leaf_server::metadata::sidecars::ChapterJson {
        number: Some(number),
        start_page: Some(start),
        ..Default::default()
    };
    let entry = EntryJson {
        leaf: Some(1),
        number: Some(4.0),
        cover_page: Some(130),
        chapters: vec![chapter(5.0, 0), chapter(6.0, 62), chapter(7.0, 121)],
        ..Default::default()
    };
    std::fs::write(
        folder.join("Tome 04.cbz"),
        common::archive_of_pages(&entry, 188),
    )
    .unwrap();
    library.scan();
    let six = library.id("SELECT id FROM chapter WHERE number = 6");
    (library, six)
}

/// The file the pages of chapter 6 come from: 130 pages, of which 62–123 are the chapter.
fn a_better_scan() -> Vec<u8> {
    volume_bytes(4, 130, Some("meilleur scan"))
}

fn splicing(six: &str, bytes: &[u8], from: u32, to: u32) -> ImportRequest {
    let mut sent = file("bp 04 hq.cbz", bytes, 4, 130);
    sent.splice = Some(Splice { from, to });
    aimed(TargetKind::Chapter, six, "Blue Period", vec![sent])
}

fn chapter_named(opened: &ImportOpened, number: f64) -> &ChapterSplice {
    opened
        .splice
        .as_ref()
        .expect("a splice")
        .chapters
        .iter()
        .find(|c| c.number == Some(number))
        .unwrap_or_else(|| panic!("chapter {number}"))
}

#[test]
fn a_chapter_reimport_says_the_new_ranges_before_anything_moves() {
    let (library, six) = a_chaptered_fourth();
    let entry = library.entry(4);
    place(&library, DEFAULT_PROFILE, &entry, 150, None);
    let before = std::fs::read(fourth(&library)).unwrap();

    let opened = library
        .open(&splicing(&six, &a_better_scan(), 62, 123))
        .unwrap();

    let splice = opened.splice.as_ref().expect("a splice");
    assert_eq!(188, splice.pages_before);
    assert_eq!(191, splice.pages_after);
    let five = chapter_named(&opened, 5.0);
    assert_eq!((SpliceKind::Keep, 0, 61, 0, 61), shape(five));
    let six = chapter_named(&opened, 6.0);
    assert_eq!((SpliceKind::Replace, 62, 120, 62, 123), shape(six));
    let seven = chapter_named(&opened, 7.0);
    assert_eq!((SpliceKind::Shift, 121, 187, 124, 190), shape(seven));
    assert_eq!(Some(150), splice.page);
    assert_eq!(Some(153), splice.page_becomes);
    assert_eq!(vec!["bp 04 hq.cbz".to_string()], opened.to_send);
    assert_eq!(before, std::fs::read(fourth(&library)).unwrap());
}

fn shape(c: &ChapterSplice) -> (SpliceKind, i64, i64, i64, i64) {
    (c.outcome, c.from, c.to, c.becomes_from, c.becomes_to)
}

/// Where the place, the chapters and the origins are: three statements, the volume's 188
/// pages and three chapters asked in one.
#[test]
fn opening_a_chapter_reimport_costs_three_statements() {
    let (library, six) = a_chaptered_fourth();
    let before = library.db.statements();
    library
        .open(&splicing(&six, &a_better_scan(), 62, 123))
        .unwrap();
    assert_eq!(3, library.db.statements() - before);
}

#[test]
fn replacing_a_chapter_rewrites_the_volume_once_and_keeps_its_reader() {
    let (library, six) = a_chaptered_fourth();
    let entry = library.entry(4);
    let ines = a_reader(&library, "Inès", "sauge");
    let noe = a_reader(&library, "Noé", "prune");
    place(&library, &ines, &entry, 111, None);
    place(&library, &noe, &entry, 150, None);
    let old = std::fs::read(fourth(&library)).unwrap();
    let bytes = a_better_scan();
    let opened = library.open(&splicing(&six, &bytes, 62, 123)).unwrap();
    let bulk = library.bulk();
    bulk.receive(&opened.id, "bp 04 hq.cbz", 0, &bytes, SENT_CEILING)
        .unwrap();

    let done = bulk
        .commit(
            &opened.id,
            &["bp 04 hq.cbz".to_string()],
            &[],
            &[],
            DEFAULT_PROFILE,
        )
        .unwrap();
    library.scan();

    assert_eq!(1, done.installed, "{done:?}");
    assert_eq!(entry, library.entry(4), "the same entry");
    let pages: i64 = library
        .db
        .read(|cx| {
            cx.query_one(
                "SELECT page_count FROM entry WHERE id = ?1",
                [&entry],
                |r| r.get(0),
            )
        })
        .unwrap()
        .unwrap();
    assert_eq!(191, pages);
    let seven: i64 = library
        .db
        .read(|cx| {
            cx.query_one("SELECT start_page FROM chapter WHERE number = 7", [], |r| {
                r.get(0)
            })
        })
        .unwrap()
        .unwrap();
    assert_eq!(124, seven);
    assert_eq!(111, progress_of(&library, &ines, &entry).0);
    assert_eq!(153, progress_of(&library, &noe, &entry).0);
    let entry_json: EntryJson = serde_json::from_slice(
        &leaf_server::archive::cbz::extract(&fourth(&library), "entry.json")
            .unwrap()
            .unwrap(),
    )
    .unwrap();
    assert_eq!(Some(133), entry_json.cover_page, "after the chapter, moved");
    assert_eq!(old, std::fs::read(kept(&library, &opened.id)).unwrap());
    assert_eq!(1, replaced::of_entry(&library.db, &entry).unwrap().len());
}

#[test]
fn a_range_past_the_source_pages_is_refused_and_installs_nothing() {
    let (library, six) = a_chaptered_fourth();
    let old = std::fs::read(fourth(&library)).unwrap();
    let bytes = a_better_scan();
    let opened = library.open(&splicing(&six, &bytes, 62, 130)).unwrap();
    let bulk = library.bulk();
    bulk.receive(&opened.id, "bp 04 hq.cbz", 0, &bytes, SENT_CEILING)
        .unwrap();

    let refused = bulk.commit(
        &opened.id,
        &["bp 04 hq.cbz".to_string()],
        &[],
        &[],
        DEFAULT_PROFILE,
    );

    assert!(refused
        .err()
        .is_some_and(|e| e.downcast_ref::<leaf_server::api::Invalid>().is_some()));
    assert_eq!(old, std::fs::read(fourth(&library)).unwrap());
    assert!(library
        .session(&opened.id)
        .join("files/bp 04 hq.cbz")
        .is_file());
    assert!(!library
        .dir
        .path()
        .join("inbox/replaced")
        .join(&opened.id)
        .exists());
}

#[test]
fn a_splice_without_a_chapter_target_is_refused() {
    let (library, six) = a_chaptered_fourth();
    let entry = library.entry(4);
    let bytes = a_better_scan();
    let mut request = splicing(&six, &bytes, 62, 123);
    request.target = Some(ImportTarget {
        kind: TargetKind::Entry,
        id: entry,
    });
    assert!(is_refused(library.open(&request)));

    // Nor a chapter target without one, nor a range backwards or longer than any volume.
    let mut bare = splicing(&six, &bytes, 62, 123);
    bare.files[0].splice = None;
    assert!(is_refused(library.open(&bare)));
    assert!(is_refused(library.open(&splicing(&six, &bytes, 70, 62))));
    assert!(is_refused(library.open(&splicing(
        &six,
        &bytes,
        0,
        u32::MAX
    ))));
}

/// Cut after the spliced volume took its name and before the index heard of it: the next
/// commit finds the splice by what the file holds — it shares no inode with what was sent —
/// records it, and moves each place once.
#[test]
fn a_splice_cut_after_its_rename_is_recorded_once_by_the_next_commit() {
    let (library, six) = a_chaptered_fourth();
    let entry = library.entry(4);
    let noe = a_reader(&library, "Noé", "prune");
    place(&library, &noe, &entry, 150, None);
    let bytes = a_better_scan();
    let opened = library.open(&splicing(&six, &bytes, 62, 123)).unwrap();
    library
        .bulk()
        .receive(&opened.id, "bp 04 hq.cbz", 0, &bytes, SENT_CEILING)
        .unwrap();
    let named = ["bp 04 hq.cbz".to_string()];
    assert!(library
        .bulk()
        .cut_at(Cut::AfterRename)
        .commit(&opened.id, &named, &[], &[], DEFAULT_PROFILE)
        .is_err());
    assert_eq!(150, progress_of(&library, &noe, &entry).0, "not yet");

    let resumed = library
        .bulk()
        .commit(&opened.id, &named, &[], &[], DEFAULT_PROFILE)
        .unwrap();

    assert!(resumed.pending.is_empty(), "{:?}", resumed.pending);
    assert_eq!(1, resumed.replaced.len(), "{:?}", resumed.replaced);
    assert_eq!(153, progress_of(&library, &noe, &entry).0);
    assert_eq!(1, replaced::of_entry(&library.db, &entry).unwrap().len());
}

/// Opened while chapter 6 ran from page 62 to 120; the volume was then replaced by a
/// 190-page one whose chapters start at 0, 64 and 123, and rescanned. The cut kept from
/// the announcement replaced pages 62–120 of it: two pages of chapter 5 gone.
#[test]
fn a_chapter_whose_volume_changed_since_the_announcement_is_refused_and_installs_nothing() {
    let (library, six) = a_chaptered_fourth();
    let bytes = a_better_scan();
    let opened = library.open(&splicing(&six, &bytes, 62, 123)).unwrap();
    let chapter = |number: f64, start: i32| leaf_server::metadata::sidecars::ChapterJson {
        number: Some(number),
        start_page: Some(start),
        ..Default::default()
    };
    let moved = common::archive_of_pages(
        &EntryJson {
            leaf: Some(1),
            number: Some(4.0),
            chapters: vec![chapter(5.0, 0), chapter(6.0, 64), chapter(7.0, 123)],
            ..Default::default()
        },
        190,
    );
    std::fs::write(fourth(&library), &moved).unwrap();
    library.scan();
    let bulk = library.bulk();
    bulk.receive(&opened.id, "bp 04 hq.cbz", 0, &bytes, SENT_CEILING)
        .unwrap();

    let refused = bulk.commit(
        &opened.id,
        &["bp 04 hq.cbz".to_string()],
        &[],
        &[],
        DEFAULT_PROFILE,
    );

    assert!(refused
        .err()
        .is_some_and(|e| e.downcast_ref::<leaf_server::api::Invalid>().is_some()));
    assert_eq!(moved, std::fs::read(fourth(&library)).unwrap());
    assert!(library
        .session(&opened.id)
        .join("files/bp 04 hq.cbz")
        .is_file());
}

/// A volume of the library that cannot be read is the library's to mend: the file sent is
/// not the broken one, and stays in the session to be committed once it is.
#[test]
fn a_volume_that_cannot_be_rewritten_keeps_the_file_sent() {
    let (library, six) = a_chaptered_fourth();
    let bytes = a_better_scan();
    let opened = library.open(&splicing(&six, &bytes, 62, 123)).unwrap();
    let whole = std::fs::read(fourth(&library)).unwrap();
    std::fs::write(fourth(&library), &whole[..whole.len() / 2]).unwrap();
    let bulk = library.bulk();
    bulk.receive(&opened.id, "bp 04 hq.cbz", 0, &bytes, SENT_CEILING)
        .unwrap();

    assert!(bulk
        .commit(
            &opened.id,
            &["bp 04 hq.cbz".to_string()],
            &[],
            &[],
            DEFAULT_PROFILE,
        )
        .is_err());
    assert!(library
        .session(&opened.id)
        .join("files/bp 04 hq.cbz")
        .is_file());
}

/// The scan reads an `entry.json` filed in a folder of the archive; the splice looked for
/// it at the root only, and refused a volume whose chapters it had itself indexed.
#[test]
fn a_volume_whose_entry_json_is_in_a_folder_has_its_chapter_replaced() {
    let (library, six) = a_chaptered_fourth();
    let held = fourth(&library);
    let declared = leaf_server::archive::cbz::extract(&held, "entry.json")
        .unwrap()
        .unwrap();
    leaf_server::archive::cbz_writer::rewrite(
        &held,
        &[("meta/entry.json", &declared)],
        &["entry.json"],
    )
    .unwrap();
    library.scan();
    let six_again = library.id("SELECT id FROM chapter WHERE number = 6");
    assert_eq!(six, six_again);
    let bytes = a_better_scan();
    let opened = library.open(&splicing(&six, &bytes, 62, 123)).unwrap();
    let bulk = library.bulk();
    bulk.receive(&opened.id, "bp 04 hq.cbz", 0, &bytes, SENT_CEILING)
        .unwrap();

    let done = bulk
        .commit(
            &opened.id,
            &["bp 04 hq.cbz".to_string()],
            &[],
            &[],
            DEFAULT_PROFILE,
        )
        .unwrap();

    assert_eq!(1, done.installed, "{done:?}");
    let content = leaf_server::archive::cbz::read(&held, false).unwrap();
    assert_eq!(191, content.pages.len());
}

/// Chapter 6.5 says nothing of where it starts: its pages are among 62–120, and replacing
/// chapter 6 there replaced it too, without a word.
#[test]
fn a_chapter_followed_by_one_without_a_first_page_is_not_replaced() {
    let library = Library::new();
    let folder = library.root().join("Blue Period");
    std::fs::create_dir_all(&folder).unwrap();
    let chapter = |number: f64, start: Option<i32>| leaf_server::metadata::sidecars::ChapterJson {
        number: Some(number),
        start_page: start,
        ..Default::default()
    };
    let entry = EntryJson {
        leaf: Some(1),
        number: Some(4.0),
        chapters: vec![
            chapter(5.0, Some(0)),
            chapter(6.0, Some(62)),
            chapter(6.5, None),
            chapter(7.0, Some(121)),
        ],
        ..Default::default()
    };
    std::fs::write(
        folder.join("Tome 04.cbz"),
        common::archive_of_pages(&entry, 188),
    )
    .unwrap();
    library.scan();
    let six = library.id("SELECT id FROM chapter WHERE number = 6");
    let seven = library.id("SELECT id FROM chapter WHERE number = 7");

    assert!(is_refused(library.open(&splicing(
        &six,
        &a_better_scan(),
        62,
        123
    ))));
    assert!(library
        .open(&splicing(&seven, &a_better_scan(), 62, 123))
        .is_ok());
}

/// « DBGT » is found to be « Dragon Ball GT », and both folders of the drop declare it:
/// two `work.json` landing on one, the second written over the first without a word.
#[test]
fn two_declarations_that_land_on_one_path_are_refused_naming_both() {
    let (library, universe) = Library::dragon_ball();
    let mut request = aimed(TargetKind::Universe, &universe, "Dragon Ball", vec![]);
    request.sidecars = vec![
        sidecar("DBGT/work.json", r#"{"leaf":1,"title":"Dragon Ball GT"}"#),
        sidecar(
            "Dragon Ball GT/work.json",
            r#"{"leaf":1,"title":"Dragon Ball GT","summary":"autre"}"#,
        ),
    ];

    let refused = library.open(&request).expect_err("two landings on one");

    let said = refused.to_string();
    assert!(refused
        .downcast_ref::<leaf_server::api::Invalid>()
        .is_some());
    assert!(said.contains("DBGT/work.json"), "{said}");
    assert!(said.contains("Dragon Ball GT/work.json"), "{said}");
}

/// An empty `0100.jpg` at the root of the volume, which is no page: kept under the name
/// page 100 takes, it failed the splice as a duplicate name — and the failure, read as the
/// file sent being broken, had that file deleted from its session and called corrupt.
#[test]
fn a_volume_holding_a_member_named_like_a_new_page_has_its_chapter_replaced() {
    let (library, six) = a_chaptered_fourth();
    leaf_server::archive::cbz_writer::rewrite(&fourth(&library), &[("0100.jpg", b"")], &[])
        .unwrap();
    let bytes = a_better_scan();
    let opened = library.open(&splicing(&six, &bytes, 62, 123)).unwrap();
    let bulk = library.bulk();
    bulk.receive(&opened.id, "bp 04 hq.cbz", 0, &bytes, SENT_CEILING)
        .unwrap();

    let done = bulk
        .commit(
            &opened.id,
            &["bp 04 hq.cbz".to_string()],
            &[],
            &[],
            DEFAULT_PROFILE,
        )
        .unwrap();

    assert!(done.corrupt.is_empty(), "{:?}", done.corrupt);
    assert_eq!(1, done.installed, "{done:?}");
    let content = leaf_server::archive::cbz::read(&fourth(&library), false).unwrap();
    assert_eq!(191, content.pages.len());
}

// ------------------------------------------------------- pages checked after a rewrite

/// What a sender who asked for verification announces: every page of `bytes`, hashed on
/// its content.
fn announced_pages(library: &Library, bytes: &[u8]) -> Vec<ManifestMember> {
    let at = library.dir.path().join("announced.cbz");
    std::fs::write(&at, bytes).unwrap();
    leaf_server::archive::cbz::page_digests(&at)
        .unwrap()
        .into_iter()
        .map(|page| ManifestMember {
            name: page.name,
            size: 0,
            checksum: page.checksum,
        })
        .collect()
}

/// The same pages with one of them announced as another picture's.
fn with_page_altered(mut pages: Vec<ManifestMember>, which: usize) -> Vec<ManifestMember> {
    pages[which].checksum = "0".repeat(64);
    pages
}

/// Announces `bytes` to be converted, sends it and commits: what the commit made of it.
fn converting(
    library: &Library,
    bytes: &[u8],
    pages: Option<Vec<ManifestMember>>,
) -> (String, leaf_server::api::bulk_import::ImportResult) {
    let mut sent = file("Nains 02.cbz", bytes, 2, 2);
    sent.rewrite = Some(the_conversion());
    sent.members = pages;
    let opened = library
        .open(&ImportRequest {
            root: "Terres d'Arran".to_string(),
            files: vec![sent],
            ..ImportRequest::default()
        })
        .unwrap();
    let bulk = library.bulk();
    bulk.receive(&opened.id, "Nains 02.cbz", 0, bytes, SENT_CEILING)
        .unwrap();
    let done = bulk
        .commit(&opened.id, &[], &[], &[], DEFAULT_PROFILE)
        .unwrap();
    (opened.id, done)
}

#[test]
fn a_rewritten_volume_whose_pages_were_announced_is_installed() {
    let library = Library::new();
    let bytes = a_komga_volume();
    let pages = announced_pages(&library, &bytes);

    let (_, done) = converting(&library, &bytes, Some(pages));

    assert_eq!(1, done.installed, "{done:?}");
    assert!(done.corrupt.is_empty(), "{:?}", done.corrupt);
}

#[test]
fn a_rewritten_volume_with_a_page_other_than_announced_is_refused_and_installs_nothing() {
    let library = Library::new();
    let bytes = a_komga_volume();
    let pages = with_page_altered(announced_pages(&library, &bytes), 1);

    let (id, done) = converting(&library, &bytes, Some(pages));

    assert_eq!(0, done.installed, "{done:?}");
    assert_eq!(vec!["Nains 02.cbz".to_string()], done.corrupt);
    assert!(!library.root().join("Terres d'Arran/Nains 02.cbz").exists());
    assert!(
        !library.session(&id).join("files/Nains 02.cbz").exists(),
        "dropped, so that it is asked for again"
    );
}

#[test]
fn a_rewritten_volume_announced_without_pages_is_installed_as_before() {
    let library = Library::new();
    let bytes = a_komga_volume();

    let (_, done) = converting(&library, &bytes, None);

    assert_eq!(1, done.installed, "{done:?}");
    assert!(done.corrupt.is_empty(), "{:?}", done.corrupt);
}

/// Commits the better scan's pages 62–123 over chapter 6, announcing `pages`.
fn splicing_with(
    library: &Library,
    six: &str,
    bytes: &[u8],
    pages: Option<Vec<ManifestMember>>,
) -> (String, leaf_server::api::bulk_import::ImportResult) {
    let mut request = splicing(six, bytes, 62, 123);
    request.files[0].members = pages;
    let opened = library.open(&request).unwrap();
    let bulk = library.bulk();
    bulk.receive(&opened.id, "bp 04 hq.cbz", 0, bytes, SENT_CEILING)
        .unwrap();
    let done = bulk
        .commit(
            &opened.id,
            &["bp 04 hq.cbz".to_string()],
            &[],
            &[],
            DEFAULT_PROFILE,
        )
        .unwrap();
    (opened.id, done)
}

#[test]
fn a_spliced_volume_whose_pages_were_announced_is_installed_though_every_page_was_renamed() {
    let (library, six) = a_chaptered_fourth();
    let bytes = a_better_scan();
    let pages = announced_pages(&library, &bytes);

    let (_, done) = splicing_with(&library, &six, &bytes, Some(pages));

    assert_eq!(1, done.installed, "{done:?}");
    assert!(done.corrupt.is_empty(), "{:?}", done.corrupt);
    let names: Vec<String> = leaf_server::archive::cbz::read(&fourth(&library), false)
        .unwrap()
        .pages
        .into_iter()
        .map(|p| p.name)
        .collect();
    assert_eq!("0001.jpg", names[0], "renamed, not the 000.jpg it was");
    assert_eq!(191, names.len());
}

#[test]
fn a_splice_with_a_page_taken_other_than_announced_is_refused_and_installs_nothing() {
    let (library, six) = a_chaptered_fourth();
    let old = std::fs::read(fourth(&library)).unwrap();
    let bytes = a_better_scan();
    // Page 70 is inside the pages taken, 62 to 123.
    let pages = with_page_altered(announced_pages(&library, &bytes), 70);

    let (id, done) = splicing_with(&library, &six, &bytes, Some(pages));

    assert_eq!(0, done.installed, "{done:?}");
    assert_eq!(vec!["bp 04 hq.cbz".to_string()], done.corrupt);
    assert_eq!(old, std::fs::read(fourth(&library)).unwrap());
    assert!(!library.session(&id).join("files/bp 04 hq.cbz").exists());
}

#[test]
fn a_spliced_volume_announced_without_pages_is_installed_as_before() {
    let (library, six) = a_chaptered_fourth();
    let bytes = a_better_scan();

    let (_, done) = splicing_with(&library, &six, &bytes, None);

    assert_eq!(1, done.installed, "{done:?}");
}

#[test]
fn a_replaced_volume_lands_on_its_row_and_takes_the_canonical_name() {
    let (library, series) = a_long_fourth();
    let entry = library.entry(4);
    let reader = a_reader(&library, "Inès", "sauge");
    place(&library, &reader, &entry, 112, None);
    let (bulk, id, four) = a_new_fourth(&library, &series);

    let result = bulk
        .commit(&id, &replacing_four(), &[], &[], DEFAULT_PROFILE)
        .unwrap();
    assert_eq!(
        four,
        std::fs::read(fourth(&library)).unwrap(),
        "on its row first"
    );
    let scanner = Scanner::new(Arc::clone(&library.db), true);
    scanner.scan(&[library.root()]).unwrap();
    leaf_server::api::renames::after_replacement(&scanner, &library.db, &result.replaced).unwrap();

    assert!(!fourth(&library).exists());
    assert_eq!(
        four,
        std::fs::read(library.root().join("Blue Period/blue-period_tome_004.cbz")).unwrap()
    );
    assert_eq!(entry, library.entry(4), "the same row, so the same readers");
    assert_eq!(112, progress_of(&library, &reader, &entry).0);
    assert_eq!(5, number_of(&library, "SELECT COUNT(*) FROM entry"));
    // The volumes nobody replaced keep the names they had.
    assert!(library.root().join("Blue Period/Tome 05.cbz").exists());
}

#[test]
fn a_commit_cut_after_some_files_resumes_with_the_canonical_names() {
    let (library, series) = Library::blue_period();
    let thirteen = volume_bytes(13, 2, None);
    let opened = library
        .open(&aimed(
            TargetKind::Series,
            &series,
            "Blue Period (scan)",
            vec![file("bp 13.cbz", &thirteen, 13, 2)],
        ))
        .unwrap();
    library
        .bulk()
        .receive(&opened.id, "bp 13.cbz", 0, &thirteen, SENT_CEILING)
        .unwrap();

    // A cut after the file was put in place: the session says where it was to go, and that
    // is the name it was announced under, which is the one to find it by.
    let landings: serde_json::Value = serde_json::from_slice(
        &std::fs::read(library.session(&opened.id).join("_landings.json")).unwrap(),
    )
    .unwrap();
    let landing = landings["files"]["bp 13.cbz"].as_str().unwrap().to_string();
    assert_eq!(
        "blue-period_tome_013.cbz",
        landing.rsplit('/').next().unwrap()
    );
    std::fs::write(library.root().join(&landing), &thirteen).unwrap();
    let result = library
        .bulk()
        .commit(&opened.id, &[], &[], &[], DEFAULT_PROFILE)
        .unwrap();

    let folder = library.root().join("Blue Period");
    assert_eq!(
        thirteen,
        std::fs::read(folder.join("blue-period_tome_013.cbz")).unwrap()
    );
    assert!(
        !folder.join("bp 13.cbz").exists(),
        "no second copy under the name it arrived with"
    );
    assert!(result.pending.is_empty(), "{:?}", result.pending);
}

/// Whatever is announced as a file is installed in the library, among the volumes, and stays.
/// A volume and the image of a cover are what a drop holds; a page, a script or a binary is not.
#[test]
fn a_file_that_is_neither_a_volume_nor_an_image_is_refused_before_a_byte_moves() {
    let (library, series) = Library::blue_period();
    let bytes = volume_bytes(2, 2, None);
    for name in [
        "run.sh",
        "page.html",
        "tool.exe",
        "noextension",
        "x.cbz.php",
    ] {
        let mut files = vec![file("a.cbz", &bytes, 2, 2)];
        files.push(ManifestFile {
            path: name.to_string(),
            size: 4,
            ..ManifestFile::default()
        });
        let request = aimed(TargetKind::Series, &series, "Blue Period", files);
        assert!(is_refused(library.open(&request)), "{name} was accepted");
    }
}
