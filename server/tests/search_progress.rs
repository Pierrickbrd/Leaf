//! Search, and where the reader stands.
//!
//! The search index is seeded by hand: writing it is the scanner's job, and a search test
//! that needs a scanner fails for two reasons with only one of them its own.

use std::sync::Arc;

use leaf_server::api::dto::{SeriesFilter, SeriesSort};
use leaf_server::api::progress::{Progress, ProgressPatch};
use leaf_server::store::text::search_key;
use leaf_server::store::{Db, Repository};

struct Fixture {
    _dir: tempfile::TempDir,
    db: Arc<Db>,
}

impl Fixture {
    fn new() -> Self {
        let dir = tempfile::tempdir().expect("a directory");
        let db = Db::open(&dir.path().join("index.sqlite")).expect("opening");
        db.write(|cx| {
            cx.execute(
                "INSERT INTO work (id, name, path, author) VALUES ('w', 'L''Attaque des Titans', '/w', 'Isayama')",
                [],
            )?;
            cx.execute(
                "INSERT INTO work_genre (work_id, name, key) VALUES ('w', 'Action', 'action')",
                [],
            )?;
            cx.execute(
                "INSERT INTO edition (id, work_id, path, implicit) VALUES ('e', 'w', '/w/e', 1)",
                [],
            )?;
            cx.execute(
                "INSERT INTO work (id, name, path, author) VALUES ('w2', 'Erased', '/w2', 'Sanbe')",
                [],
            )?;
            cx.execute(
                "INSERT INTO edition (id, work_id, path, implicit) VALUES ('e2', 'w2', '/w2/e', 1)",
                [],
            )?;
            // A title in its own script. Under the old fold its search key was the empty
            // string, so it was in the library and unreachable at the same time.
            cx.execute(
                "INSERT INTO work (id, name, path, author) VALUES ('w3', 'ハイキュー!!', '/w3', 'Furudate')",
                [],
            )?;
            cx.execute(
                "INSERT INTO edition (id, work_id, path, implicit) VALUES ('e3', 'w3', '/w3/e', 1)",
                [],
            )?;
            for (id, edition, sort, pages) in
                [("v1", "e", 1.0, 190), ("v2", "e", 2.0, 190), ("x1", "e2", 1.0, 195)]
            {
                cx.execute(
                    "INSERT INTO entry (id, edition_id, type, file, size, modified_at, added_at,
                                        volume_number, sort_key, page_count)
                     VALUES (?1, ?2, 'VOLUME', ?3, 1, 1, 1, ?4, ?4, ?5)",
                    (id, edition, format!("/{id}.cbz"), sort, pages),
                )?;
            }
            // Two markers in volume one, so "which chapter is page 100 in" has an answer.
            cx.execute(
                "INSERT INTO chapter (id, edition_id, entry_id, raw, label, number, kind, position, start_page)
                 VALUES ('c1', 'e', 'v1', '1', 'Chapitre 1', 1.0, 'CHAPTER', 0, 0)",
                [],
            )?;
            cx.execute(
                "INSERT INTO chapter (id, edition_id, entry_id, raw, label, number, kind, position, start_page)
                 VALUES ('c2', 'e', 'v1', '2', 'Chapitre 2', 2.0, 'CHAPTER', 1, 60)",
                [],
            )?;
            // The index, as the scanner would have written it: folded on the way in.
            for (kind, reference, name, detail, edition, entry, label) in [
                ("EDITION", "e", "lattaque des titans", "isayama action", Some("e"), None, "L'Attaque des Titans"),
                ("ENTRY", "v1", "lattaque des titans tome 1", "", Some("e"), Some("v1"), "Tome 1"),
                ("CHAPTER", "c2", "chapitre 2 les titans", "", Some("e"), Some("v1"), "Chapitre 2"),
                ("EDITION", "e2", "erased", "sanbe", Some("e2"), None, "Erased"),
            ] {
                cx.execute(
                    "INSERT INTO search (name, detail, kind, ref, edition_id, entry_id, label)
                     VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)",
                    (name, detail, kind, reference, edition, entry, label),
                )?;
            }
            // Folded here by the same function the scanner uses, rather than typed out: the
            // whole point is that what goes in and what a query becomes are one rule.
            cx.execute(
                "INSERT INTO search (name, detail, kind, ref, edition_id, entry_id, label)
                 VALUES (?1, ?2, 'EDITION', 'e3', 'e3', NULL, ?3)",
                (search_key("ハイキュー!!"), search_key("Furudate"), "ハイキュー!!"),
            )?;
            Ok(())
        })
        .expect("seeding");
        Fixture {
            _dir: dir,
            db: Arc::new(db),
        }
    }

    fn find(&self, query: &str) -> Vec<String> {
        self.search(query, &[], &SeriesFilter::default())
            .into_iter()
            .map(|h| h.label)
            .collect()
    }

    fn search(
        &self,
        query: &str,
        kinds: &[String],
        filter: &SeriesFilter,
    ) -> Vec<leaf_server::api::dto::SearchHitDto> {
        Repository::new(&self.db)
            .search(query, 40, kinds, filter)
            .expect("searching")
    }
}

// -------------------------------------------------------------------- search

#[test]
fn every_term_has_to_land_and_a_prefix_counts() {
    let f = Fixture::new();
    assert!(!f.find("titans").is_empty());
    assert!(
        !f.find("tita").is_empty(),
        "a half-typed word already finds something"
    );
    // Two terms: both have to appear.
    assert!(!f.find("attaque titans").is_empty());
    assert!(
        f.find("attaque erased").is_empty(),
        "one term must not carry the other"
    );
}

#[test]
fn accents_case_and_apostrophes_are_folded_on_the_way_in() {
    let f = Fixture::new();
    // "L'Attaque" is reachable by typing "lattaque", which is what someone in a hurry
    // actually produces.
    for spelling in ["lattaque", "L'Attaque", "LATTAQUE", "l’attaque"] {
        assert!(!f.find(spelling).is_empty(), "typed {spelling}");
    }
}

#[test]
fn an_edition_wears_the_name_the_grid_gives_it() {
    let f = Fixture::new();
    let hits = f.search("titans", &["EDITION".into()], &SeriesFilter::default());
    assert_eq!(1, hits.len());
    // Not the bare folder name: a result and the thing it opens are never labelled
    // differently.
    assert_eq!("L'Attaque des Titans", hits[0].label);
    assert_eq!(
        Some("L'Attaque des Titans".to_string()),
        hits[0].series_name
    );
}

#[test]
fn the_client_can_ask_for_one_level_only() {
    let f = Fixture::new();
    // A search box wants series; "find the chapter called…" wants chapters.
    let chapters = f.search("titans", &["CHAPTER".into()], &SeriesFilter::default());
    assert_eq!(1, chapters.len());
    assert_eq!("CHAPTER", chapters[0].kind);
    assert_eq!("Chapitre 2", chapters[0].label);

    let editions = f.search("titans", &["EDITION".into()], &SeriesFilter::default());
    assert_eq!(1, editions.len());
    assert_eq!("EDITION", editions[0].kind);
}

/// The guess answers about the level that was asked for, or not at all.
///
/// It is series-only by construction: it reads what it compares, so what it reads has to
/// stay bounded by the shelf — two hundred rows rather than fifty thousand. That made it
/// answer with a series to a client that had asked for chapters, which the Kotlin did too.
/// Decided at the port: a client that asked for chapters and got none wants "no chapters",
/// not "here is a series you might have meant".
#[test]
fn the_guess_is_offered_only_when_a_series_was_asked_for() {
    let f = Fixture::new();

    let asked_for_chapters = f.search("erazed", &["CHAPTER".into()], &SeriesFilter::default());
    assert!(
        asked_for_chapters.is_empty(),
        "asked for chapters, and there are none"
    );

    // Asking for series, or asking for nothing in particular, still gets the guess.
    for kinds in [vec!["EDITION".to_string()], Vec::new()] {
        let hits = f.search("erazed", &kinds, &SeriesFilter::default());
        assert_eq!(1, hits.len(), "with kinds = {kinds:?}");
        assert_eq!("EDITION", hits[0].kind);
        assert!(hits[0].approximate);
    }
}

#[test]
fn a_search_runs_inside_the_shelfs_filters() {
    let f = Fixture::new();
    let action = SeriesFilter {
        genres: vec!["Action".into()],
        ..Default::default()
    };
    assert!(!f.search("titans", &[], &action).is_empty());

    // A lit chip is a statement about what you are looking at: a search that ignored it
    // would hand back series the shelf behind it is hiding.
    let elsewhere = SeriesFilter {
        genres: vec!["Romance".into()],
        ..Default::default()
    };
    assert!(f.search("titans", &[], &elsewhere).is_empty());
}

#[test]
fn a_near_miss_comes_back_marked_as_a_guess() {
    let f = Fixture::new();
    let hits = f.search("erazed", &[], &SeriesFilter::default());

    assert_eq!(1, hits.len());
    assert_eq!("Erased", hits[0].label);
    // It has to reach the screen as a guess. An approximate hit shown like an exact one is
    // a search that invents answers, which costs more trust than finding nothing.
    assert!(hits[0].approximate, "a guess must say so");
    // And a hit that worked must not be second-guessed.
    assert!(!f.search("erased", &[], &SeriesFilter::default())[0].approximate);
}

#[test]
fn a_guess_is_only_ever_offered_about_a_series() {
    let f = Fixture::new();
    // "chapitre" is in the index as a CHAPTER row; a typo on it finds nothing, because the
    // fallback reads only editions — it has no index behind it, so what it reads has to
    // stay bounded by the shelf.
    let hits = f.search("chapitrf", &[], &SeriesFilter::default());
    assert!(hits.iter().all(|h| h.kind == "EDITION"), "{hits:?}");
}

#[test]
fn a_short_word_is_not_forgiven_and_an_empty_one_asks_nothing() {
    let f = Fixture::new();
    // Three letters, one edit: a dozen unrelated words. A confident wrong guess is worse
    // than none.
    assert!(f.find("xyz").is_empty());
    assert!(f.find("").is_empty());
    assert!(f.find("   ").is_empty());
}

// ------------------------------------------------------------------ progress

#[test]
fn progress_never_moves_backwards_on_its_own() {
    let f = Fixture::new();
    let progress = Progress::new(&f.db);

    let at = |p: &leaf_server::api::progress::ProgressDto| p.page;
    let set = |patch: ProgressPatch| {
        progress
            .record("v1", &patch, 1_000)
            .expect("recording")
            .expect("the entry")
    };

    assert_eq!(
        40,
        at(&set(ProgressPatch {
            page: Some(40),
            ..Default::default()
        }))
    );
    // That is the rule that makes an offline queue safe: a phone replaying yesterday's
    // positions cannot undo today's reading.
    assert_eq!(
        40,
        at(&set(ProgressPatch {
            page: Some(5),
            ..Default::default()
        }))
    );
    // Re-reading is a normal thing to do, so it stays possible — by saying so.
    assert_eq!(
        5,
        at(&set(ProgressPatch {
            page: Some(5),
            rewind: true,
            ..Default::default()
        }))
    );
}

#[test]
fn a_position_that_does_not_exist_is_clamped_to_one_that_does() {
    let f = Fixture::new();
    let recorded = Progress::new(&f.db)
        .record(
            "v1",
            &ProgressPatch {
                page: Some(9_999),
                ..Default::default()
            },
            1,
        )
        .expect("recording")
        .expect("the entry");
    // 190 pages, so the last one is 189. A client with the wrong idea of how long a volume
    // is cannot store a position the volume has not got.
    assert_eq!(189, recorded.page);
}

#[test]
fn the_chapter_is_derived_from_the_markers_not_stored() {
    let f = Fixture::new();
    let progress = Progress::new(&f.db);

    let early = progress
        .record(
            "v1",
            &ProgressPatch {
                page: Some(10),
                ..Default::default()
            },
            1,
        )
        .unwrap()
        .unwrap();
    assert_eq!(
        Some("Chapitre 1".to_string()),
        early.chapter.map(|c| c.label)
    );

    let later = progress
        .record(
            "v1",
            &ProgressPatch {
                page: Some(100),
                ..Default::default()
            },
            2,
        )
        .unwrap()
        .unwrap();
    assert_eq!(
        Some("Chapitre 2".to_string()),
        later.chapter.map(|c| c.label)
    );
}

#[test]
fn a_volume_with_no_start_pages_answers_that_it_does_not_know() {
    let f = Fixture::new();
    f.db.write(|cx| {
        // Every volume still described by a ComicInfo looks like this: markers, no start
        // pages. Treating a missing one as zero would answer "chapter 1" on every page of
        // the volume, confidently and wrongly.
        cx.execute(
            "UPDATE chapter SET start_page = NULL WHERE entry_id = 'v1'",
            [],
        )?;
        Ok(())
    })
    .unwrap();

    let recorded = Progress::new(&f.db)
        .record(
            "v1",
            &ProgressPatch {
                page: Some(100),
                ..Default::default()
            },
            1,
        )
        .unwrap()
        .unwrap();
    assert!(
        recorded.chapter.is_none(),
        "an unknown chapter must stay unknown"
    );
}

#[test]
fn an_unknown_entry_records_nothing() {
    let f = Fixture::new();
    let recorded = Progress::new(&f.db)
        .record(
            "nexistepas",
            &ProgressPatch {
                page: Some(1),
                ..Default::default()
            },
            1,
        )
        .expect("recording");
    assert!(recorded.is_none());
}

#[test]
fn what_to_open_next_is_next_for_you_and_not_a_catalogue() {
    let f = Fixture::new();
    let progress = Progress::new(&f.db);

    // Nothing opened: nothing to offer. A series you have never touched appears in neither
    // list.
    assert!(progress.up_next(20).expect("up next").is_empty());

    progress
        .record(
            "v1",
            &ProgressPatch {
                page: Some(40),
                ..Default::default()
            },
            1,
        )
        .unwrap();
    let started = progress.up_next(20).expect("up next");
    assert_eq!(1, started.len());
    assert_eq!("IN_PROGRESS", started[0].reason);
    assert_eq!("v1", started[0].entry.id);

    // Finished, so what follows it is the offer — and it is a different one.
    progress
        .record(
            "v1",
            &ProgressPatch {
                finished: Some(true),
                ..Default::default()
            },
            2,
        )
        .unwrap();
    let following = progress.up_next(20).expect("up next");
    assert_eq!(1, following.len());
    assert_eq!("NEXT_UP", following[0].reason);
    assert_eq!("v2", following[0].entry.id);
    assert!(
        following[0].progress.is_none(),
        "nothing has been read of it yet"
    );
}

#[test]
fn what_to_open_next_does_not_ask_one_question_per_line() {
    let f = Fixture::new();
    let progress = Progress::new(&f.db);
    progress
        .record(
            "v1",
            &ProgressPatch {
                finished: Some(true),
                ..Default::default()
            },
            1,
        )
        .unwrap();
    progress
        .record(
            "x1",
            &ProgressPatch {
                page: Some(3),
                ..Default::default()
            },
            1,
        )
        .unwrap();

    let before = f.db.statements();
    let rows = progress.up_next(20).expect("up next");
    let cost = f.db.statements() - before;

    assert_eq!(2, rows.len());
    // It used to build every line on its own — a series query, an entry query, a progress
    // query and a chapter query each. What it costs now is what the screen is made of, not
    // how long it is.
    assert!(cost < 15, "what to open next took {cost} queries");
}

#[test]
fn the_progress_of_a_series_is_read_once_not_once_per_volume() {
    let f = Fixture::new();
    let progress = Progress::new(&f.db);
    for entry in ["v1", "v2"] {
        progress
            .record(
                entry,
                &ProgressPatch {
                    page: Some(10),
                    ..Default::default()
                },
                1,
            )
            .unwrap();
    }

    let before = f.db.statements();
    let all = progress.of_series("e").expect("of series");
    let cost = f.db.statements() - before;

    assert_eq!(2, all.len());
    assert!(cost <= 4, "the progress of 2 volumes took {cost} queries");
}

#[test]
fn progress_belongs_to_the_edition() {
    let f = Fixture::new();
    let progress = Progress::new(&f.db);
    progress
        .record(
            "v1",
            &ProgressPatch {
                page: Some(40),
                ..Default::default()
            },
            1,
        )
        .unwrap();

    // Reading in one edition says nothing about another, which is why there is no shared
    // numbering: they do not even hold the same bonus chapters.
    assert_eq!(1, progress.of_series("e").unwrap().len());
    assert!(progress.of_series("e2").unwrap().is_empty());

    // And the shelf sees it: one series in progress, one still unread.
    let shelf = Repository::new(&f.db)
        .series(&SeriesFilter::default(), SeriesSort::Name, 0, 0)
        .expect("listing");
    let statuses: Vec<(&str, &str)> = shelf
        .iter()
        .map(|s| (s.id.as_str(), s.read_status.as_str()))
        .collect();
    assert!(statuses.contains(&("e", "IN_PROGRESS")), "{statuses:?}");
    assert!(statuses.contains(&("e2", "UNREAD")), "{statuses:?}");
}

/// Progress never moves backwards — including when two positions arrive at once.
///
/// That rule is what makes the offline queue safe: a phone replaying yesterday's positions
/// cannot undo today's reading. Enforced by reading the current page and writing the larger
/// of the two, it held only as long as nothing else was writing between the two steps.
#[test]
fn two_positions_arriving_together_cannot_lose_the_further_one() {
    use std::sync::Arc;

    let f = Fixture::new();
    let entry = "v1".to_string();
    let db = Arc::clone(&f.db);

    for round in 0..200 {
        Progress::new(&db).forget(&entry).unwrap();
        let ahead = Arc::clone(&db);
        let behind = Arc::clone(&db);
        let (a, b) = (entry.clone(), entry.clone());

        let far = std::thread::spawn(move || {
            Progress::new(&ahead)
                .record(
                    &a,
                    &ProgressPatch {
                        page: Some(9),
                        ..Default::default()
                    },
                    100,
                )
                .unwrap()
        });
        let near = std::thread::spawn(move || {
            Progress::new(&behind)
                .record(
                    &b,
                    &ProgressPatch {
                        page: Some(2),
                        ..Default::default()
                    },
                    101,
                )
                .unwrap()
        });
        far.join().unwrap();
        near.join().unwrap();

        let landed = Progress::new(&db).of(&entry).unwrap().expect("a position");
        assert_eq!(
            9, landed.page,
            "round {round}: page 9 and page 2 arrived together and the far one was lost"
        );
    }
}

/// A series named in its own script is findable in it. It was not: the fold kept only Latin
/// letters, so both the indexed name and the query became empty and the search returned
/// nothing without anything looking wrong.
#[test]
fn a_title_in_its_own_script_is_findable() {
    let f = Fixture::new();
    assert_eq!(vec!["ハイキュー!!"], f.find("ハイキュー"));
    assert_eq!(vec!["ハイキュー!!"], f.find("ハイキュー!!"));
}

/// From its beginning, which is what the FTS5 prefix match gives. Not from its middle —
/// `unicode61` splits on non-alphanumerics and Japanese is written without spaces, so the
/// title is a single token. Stated as a test so the limit is recorded rather than discovered.
#[test]
fn found_from_its_beginning_and_not_from_its_middle() {
    let f = Fixture::new();
    assert_eq!(vec!["ハイキュー!!"], f.find("ハイキ"));
    assert!(f.find("キュー").is_empty());
}
