//! What a scan noticed but did not treat as a failure.
//!
//! The rule these guard: **describe what is absent, never invent a value for it.** A volume
//! with no number is a real thing — a one-shot, a hors-série, an artbook — and the answer is
//! to say so, not to make one up from the file name.

use leaf_server::metadata::sidecars::{ChapterJson, EditionJson, EntryJson, WorkJson};
use leaf_server::scan::checks;

fn sound_work() -> WorkJson {
    WorkJson {
        leaf: Some(1),
        title: Some("Death Note".into()),
        medium: Some("manga".into()),
        authors: vec!["Ōba".into()],
        artists: vec!["Obata".into()],
        status: Some("completed".into()),
        reading_direction: Some("RIGHT_TO_LEFT".into()),
        ..Default::default()
    }
}

fn only<T>(mut found: Vec<T>) -> T {
    assert_eq!(1, found.len(), "expected exactly one finding");
    found.remove(0)
}

#[test]
fn a_sound_work_raises_nothing() {
    assert!(checks::work("Death Note", Some(&sound_work())).is_empty());
}

#[test]
fn names_every_required_field_that_is_missing() {
    let found = only(checks::work(
        "Essai",
        Some(&WorkJson {
            leaf: Some(1),
            ..Default::default()
        }),
    ));
    for field in [
        "title",
        "medium",
        "authors",
        "artists",
        "status",
        "readingDirection",
    ] {
        assert!(found.contains(field), "{found} should name {field}");
    }
}

#[test]
fn an_absent_file_is_itself_the_problem() {
    assert!(only(checks::work("Essai", None)).contains("missing"));
}

#[test]
fn a_file_without_a_version_marker_is_flagged() {
    let work = WorkJson {
        leaf: None,
        ..sound_work()
    };
    assert!(only(checks::work("Essai", Some(&work))).contains("leaf"));
}

#[test]
fn a_file_from_a_later_format_is_flagged_too() {
    // Better to know you are reading something you do not fully understand.
    let work = WorkJson {
        leaf: Some(99),
        ..sound_work()
    };
    assert!(only(checks::work("Essai", Some(&work))).contains("format 99"));
}

#[test]
fn an_implicit_edition_is_looked_for_in_the_work_file() {
    let found = only(checks::edition("Bleach", None, true, Some(&sound_work())));
    assert!(found.contains("volumeCount"));
    assert!(found.contains("implicit edition"));
}

#[test]
fn an_edition_file_that_is_sound_raises_nothing() {
    let edition = EditionJson {
        leaf: Some(1),
        name: Some("Black Edition".into()),
        status: Some("completed".into()),
        volume_count: Some(7),
        ..Default::default()
    };
    assert!(checks::edition("Black Edition", Some(&edition), false, None).is_empty());
}

#[test]
fn a_file_claiming_another_work_is_caught() {
    let declared = EntryJson {
        work: Some("Autre chose".into()),
        ..Default::default()
    };
    let found = checks::identity("Tome 2.cbz", Some(&declared), "Essai", None, None);
    assert!(only(found).contains("claims work"));
}

#[test]
fn the_title_counts_as_a_match_not_only_the_folder_name() {
    let declared = EntryJson {
        work: Some("Death Note".into()),
        ..Default::default()
    };
    let found = checks::identity(
        "Tome 1.cbz",
        Some(&declared),
        "death-note",
        Some("Death Note"),
        None,
    );
    assert!(found.is_empty());
}

#[test]
fn an_edition_claimed_where_there_is_none_is_caught() {
    let declared = EntryJson {
        work: Some("Essai".into()),
        edition: Some("Fantôme".into()),
        ..Default::default()
    };
    let found = checks::identity("Tome 1.cbz", Some(&declared), "Essai", None, None);
    assert!(only(found).contains("only one"));
}

#[test]
fn an_edition_left_unsaid_where_there_is_one_is_caught() {
    let declared = EntryJson {
        work: Some("Death Note".into()),
        ..Default::default()
    };
    let found = checks::identity(
        "Tome 1.cbz",
        Some(&declared),
        "Death Note",
        None,
        Some("Black Edition"),
    );
    assert!(only(found).contains("claims no edition"));
}

#[test]
fn an_entry_that_says_nothing_is_worse_than_none() {
    // It exists, so it silences the "no metadata" alert while providing nothing.
    assert!(checks::says_nothing(Some(&EntryJson {
        leaf: Some(1),
        ..Default::default()
    })));
    assert!(!checks::says_nothing(Some(&EntryJson {
        work: Some("Essai".into()),
        number: Some(1.0),
        ..Default::default()
    })));
    assert!(!checks::says_nothing(None));
}

#[test]
fn a_standalone_bonus_with_no_anchor_is_flagged() {
    let chapters = [ChapterJson {
        title: Some("Histoire parallèle".into()),
        ..Default::default()
    }];
    let found = checks::chapters("Bonus.cbz", &chapters, true);
    assert!(only(found).contains("neither number nor after"));
}

#[test]
fn inside_a_volume_an_unnumbered_chapter_needs_no_anchor() {
    let chapters = [ChapterJson {
        title: Some("Interlude".into()),
        ..Default::default()
    }];
    assert!(checks::chapters("Tome 1.cbz", &chapters, false).is_empty());
}

#[test]
fn a_volume_must_carry_a_number_or_an_anchor() {
    let bare = EntryJson {
        leaf: Some(1),
        work: Some("Essai".into()),
        ..Default::default()
    };
    assert!(only(checks::entry("Tome 1.cbz", Some(&bare), "VOLUME", false)).contains("number"));

    // A hors-série or an artbook has no volume number, and that is legitimate — as long as
    // something places it. What it may not do is leave its place to the file name.
    let anchored = EntryJson {
        chapters: vec![ChapterJson {
            title: Some("Hors-série".into()),
            after: Some(108.0),
            ..Default::default()
        }],
        ..bare
    };
    assert!(checks::entry("Hors-serie.cbz", Some(&anchored), "VOLUME", false).is_empty());
}

#[test]
fn an_edition_is_required_once_the_work_has_several() {
    let declared = EntryJson {
        leaf: Some(1),
        work: Some("Death Note".into()),
        number: Some(1.0),
        ..Default::default()
    };
    let with_several = checks::entry("T1.cbz", Some(&declared), "VOLUME", true);
    assert!(only(with_several).contains("edition"));
    assert!(checks::entry("T1.cbz", Some(&declared), "VOLUME", false).is_empty());
}

// ------------------------------------------------------- saying two things at once

use leaf_server::scan::checks::coherence;

fn declaring(entry: EntryJson) -> Vec<String> {
    coherence("Tome 1.cbz", Some(&entry), "VOLUME", 20, None)
}

#[test]
fn a_type_that_is_neither_is_said_rather_than_guessed_at() {
    let found = declaring(EntryJson {
        kind: "TOME".into(),
        ..Default::default()
    });
    assert!(only(found).contains("neither VOLUME nor CHAPTER"));

    // Lower case is not a typo, it is the same word. The reader takes it, so nothing is said.
    assert!(declaring(EntryJson {
        kind: "chapter".into(),
        ..Default::default()
    })
    .is_empty());
}

#[test]
fn a_volume_that_came_from_a_volume_is_not_a_thing() {
    let found = declaring(EntryJson {
        volume: Some(3.0),
        ..Default::default()
    });
    // That field says which volume a loose chapter came from.
    assert!(only(found).contains("loose chapter"));

    // On a chapter it is exactly what it is for.
    assert!(coherence(
        "Chapitre 686.cbz",
        Some(&EntryJson {
            kind: "CHAPTER".into(),
            volume: Some(70.0),
            ..Default::default()
        }),
        "CHAPTER",
        20,
        None
    )
    .is_empty());
}

#[test]
fn a_number_that_is_not_a_number_of_anything() {
    assert!(only(declaring(EntryJson {
        number: Some(-3.0),
        ..Default::default()
    }))
    .contains("-3"));
}

#[test]
fn a_marker_past_the_last_page_can_never_be_reached() {
    let entry = EntryJson {
        chapters: vec![ChapterJson {
            number: Some(21.0),
            start_page: Some(900),
            ..Default::default()
        }],
        ..Default::default()
    };
    assert!(only(declaring(entry)).contains("page 900 of 20"));
}

#[test]
fn two_markers_at_one_page_leave_one_of_them_unreachable() {
    let at = |page| ChapterJson {
        number: Some(page as f64),
        start_page: Some(page),
        ..Default::default()
    };
    let entry = EntryJson {
        chapters: vec![at(2), at(2)],
        ..Default::default()
    };
    assert!(only(declaring(entry)).contains("only one of them"));
}

#[test]
fn a_number_and_an_anchor_on_one_chapter_is_one_too_many() {
    let entry = EntryJson {
        chapters: vec![ChapterJson {
            number: Some(50.0),
            after: Some(99.0),
            ..Default::default()
        }],
        ..Default::default()
    };
    // `after` places what has no number of its own; with a number it never applies.
    assert!(only(declaring(entry)).contains("the after is ignored"));
}

#[test]
fn a_number_disagreeing_with_the_file_name_is_worth_a_word() {
    let entry = EntryJson {
        number: Some(1.0),
        ..Default::default()
    };
    let found = coherence("Tome 7.cbz", Some(&entry), "VOLUME", 20, Some(7.0));
    assert!(only(found).contains("declares number 1"));

    // Agreeing, or the name saying nothing: silence.
    assert!(coherence("Tome 1.cbz", Some(&entry), "VOLUME", 20, Some(1.0)).is_empty());
    assert!(coherence("bidule.cbz", Some(&entry), "VOLUME", 20, None).is_empty());
}

#[test]
fn a_file_that_declares_nothing_contradicts_nothing() {
    assert!(coherence("Tome 1.cbz", None, "VOLUME", 20, Some(7.0)).is_empty());
}

#[test]
fn a_work_that_declares_itself_and_says_nothing_is_named_field_by_field() {
    use leaf_server::metadata::sidecars::WorkJson;
    let empty = WorkJson {
        leaf: Some(1),
        ..Default::default()
    };
    let said = leaf_server::scan::checks::work("Bleach", Some(&empty));
    let all = said.join(" ");
    for field in [
        "title",
        "medium",
        "authors",
        "artists",
        "status",
        "readingDirection",
    ] {
        assert!(all.contains(field), "{field} missing from: {all}");
    }
}

#[test]
fn an_edition_with_a_folder_and_no_name_is_named_as_missing_one() {
    use leaf_server::metadata::sidecars::EditionJson;
    // An edition that has a folder of its own has a name to declare in it; an implicit one
    // has neither, and its fields live in the work's file instead.
    let empty = EditionJson {
        leaf: Some(1),
        ..Default::default()
    };
    let said = leaf_server::scan::checks::edition("Bleach/Perfect", Some(&empty), false, None);
    assert!(said.join(" ").contains("name"), "{said:?}");
}

#[test]
fn an_entry_with_no_type_is_named_as_missing_one() {
    use leaf_server::metadata::sidecars::EntryJson;
    let declared = EntryJson {
        leaf: Some(1),
        work: Some("Bleach".into()),
        number: Some(1.0),
        kind: "   ".into(),
        ..Default::default()
    };
    let said = leaf_server::scan::checks::entry("Tome 1.cbz", Some(&declared), "VOLUME", false);
    assert!(said.join(" ").contains("type"), "{said:?}");
}

#[test]
fn an_entry_claiming_an_edition_it_does_not_sit_in_is_said_out_loud() {
    use leaf_server::metadata::sidecars::EntryJson;
    let declared = EntryJson {
        leaf: Some(1),
        work: Some("Bleach".into()),
        edition: Some("Perfect Edition".into()),
        number: Some(1.0),
        kind: "VOLUME".into(),
        ..Default::default()
    };
    let said = leaf_server::scan::checks::identity(
        "Tome 1.cbz",
        Some(&declared),
        "Bleach",
        None,
        Some("Édition Originale"),
    );
    let all = said.join(" ");
    assert!(all.contains("Perfect Edition"), "{all}");
    assert!(all.contains("Édition Originale"), "{all}");
}
