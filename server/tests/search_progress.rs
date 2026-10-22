//! Search, and where the reader stands.
//!
//! The search index is seeded by hand: writing it is the scanner's job, and a search test
//! that needs a scanner fails for two reasons with only one of them its own.

use std::sync::Arc;

use leaf_server::api::dto::{SeriesFilter, SeriesSort};
use leaf_server::api::progress::{Progress, ProgressPatch};
use leaf_server::store::text::search_key;
use leaf_server::store::{Db, Repository};

mod common;
use axum::body::Body;
use axum::http::StatusCode;
use common::{a_volume, json_body, request, Server, READ_ONLY};

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
                "INSERT INTO work (id, name, path) VALUES ('w', 'L''Attaque des Titans', '/w')",
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
                "INSERT INTO work (id, name, path) VALUES ('w2', 'Erased', '/w2')",
                [],
            )?;
            cx.execute(
                "INSERT INTO edition (id, work_id, path, implicit) VALUES ('e2', 'w2', '/w2/e', 1)",
                [],
            )?;
            // A title in its own script. Under the old fold its search key was the empty
            // string, so it was in the library and unreachable at the same time.
            cx.execute(
                "INSERT INTO work (id, name, path) VALUES ('w3', 'ハイキュー!!', '/w3')",
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

#[test]
fn a_paged_search_counts_the_whole_answer_and_describes_its_files() {
    let f = Fixture::new();
    let repository = Repository::new(&f.db);
    let filter = SeriesFilter::default();

    let first = repository
        .search_page("titans", 1, 0, &[], &filter)
        .expect("the first search page");
    let second = repository
        .search_page("titans", 1, 1, &[], &filter)
        .expect("the second search page");

    assert_eq!(3, first.total, "one series, one volume and one chapter");
    assert_eq!(2, first.file_total, "only volume and chapter are files");
    assert_eq!(0, first.page);
    assert_eq!(1, first.size);
    assert_eq!(1, first.items.len());
    assert_eq!(1, second.items.len());
    assert_ne!(first.items[0].id, second.items[0].id);

    let all = repository
        .search_page("titans", 40, 0, &[], &filter)
        .expect("all the context");
    let volume = all
        .items
        .iter()
        .find(|hit| hit.kind == "ENTRY")
        .expect("the volume hit");
    assert_eq!(Some("VOLUME"), volume.entry_kind.as_deref());
    assert_eq!(Some(1.0), volume.entry_number);
    assert_eq!(Some(190), volume.entry_page_count);

    let chapter = all
        .items
        .iter()
        .find(|hit| hit.kind == "CHAPTER")
        .expect("the chapter hit");
    assert_eq!(Some("VOLUME"), chapter.entry_kind.as_deref());
    assert_eq!(Some(1.0), chapter.entry_number);
    assert_eq!(Some(2.0), chapter.chapter_number);
    assert_eq!(Some(190), chapter.entry_page_count);
}

/// The guess answers about the level that was asked for, or not at all.
///
/// It is series-only by construction: it reads what it compares, so what it reads has to
/// stay bounded by the shelf — two hundred rows rather than fifty thousand. Left as it is,
/// that answers with a series to a client that asked for chapters: and a client that asked
/// for chapters and got none wants "no chapters", not "here is a series you might have
/// meant".
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
            .record("default", "v1", &patch, 1_000)
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
fn two_profiles_keep_two_places_in_the_same_volume() {
    let f = Fixture::new();
    f.db.write(|cx| {
        cx.execute(
            "INSERT INTO profile (id, name, name_key, created_at) VALUES ('kid', 'Kid', 'kid', 1)",
            [],
        )
    })
    .unwrap();
    let progress = Progress::new(&f.db);
    for (profile, page) in [("default", 3), ("kid", 9)] {
        let patch = ProgressPatch {
            page: Some(page),
            ..Default::default()
        };
        progress.record(profile, "v1", &patch, 1_000).unwrap();
    }
    let pages: Vec<(String, i64)> =
        f.db.read(|cx| {
            cx.query(
                "SELECT profile_id, page FROM progress WHERE entry_id = 'v1' ORDER BY profile_id",
                [],
                |r| Ok((r.get(0)?, r.get(1)?)),
            )
        })
        .unwrap();
    assert_eq!(
        vec![("default".to_string(), 3), ("kid".to_string(), 9)],
        pages
    );
}

#[test]
fn a_position_that_does_not_exist_is_clamped_to_one_that_does() {
    let f = Fixture::new();
    let recorded = Progress::new(&f.db)
        .record(
            "default",
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
            "default",
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
            "default",
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
            "default",
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
            "default",
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
            "default",
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
            "default",
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
            "default",
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
            "default",
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
    // how long it is: two questions to choose the lines — what is open, and what follows
    // the furthest finished entry of every series at once — and the rest to dress them.
    assert_eq!(13, cost, "what to open next took {cost} queries");
}

/// Two more volumes in the first edition, so that « after the last one finished » and
/// « the first one never opened » can be told apart.
fn four_volumes(f: &Fixture) {
    f.db.write(|cx| {
        for (id, sort) in [("v3", 3.0), ("v4", 4.0)] {
            cx.execute(
                "INSERT INTO entry (id, edition_id, type, file, size, modified_at, added_at,
                                    volume_number, sort_key, page_count)
                 VALUES (?1, 'e', 'VOLUME', ?2, 1, 1, 1, ?3, ?3, 190)",
                (id, format!("/{id}.cbz"), sort),
            )?;
        }
        Ok(())
    })
    .expect("seeding");
}

/// What `up_next` offers for the first edition, as (entry, reason).
fn offered_in_e(progress: &Progress) -> Vec<(String, String)> {
    progress
        .up_next(20)
        .expect("up next")
        .into_iter()
        .filter(|n| n.series_id == "e")
        .map(|n| (n.entry.id, n.reason))
        .collect()
}

fn at_page(progress: &Progress, entry: &str, page: i64) {
    progress
        .record(
            "default",
            entry,
            &ProgressPatch {
                page: Some(page),
                ..Default::default()
            },
            1,
        )
        .expect("recording")
        .expect("the entry");
}

/// The test of 22/10/2026: volume three finished, volumes one and two never opened because
/// they were read elsewhere, and « à suivre » offered volume one — the first entry carrying
/// no progress, not the one that follows what was just read.
#[test]
fn what_follows_is_the_entry_after_the_last_one_finished_not_the_first_unread() {
    let f = Fixture::new();
    four_volumes(&f);
    let progress = Progress::new(&f.db);

    finish(&progress, "v3", 1_000);

    assert_eq!(
        vec![("v4".to_string(), "NEXT_UP".to_string())],
        offered_in_e(&progress)
    );
}

#[test]
fn a_series_finished_to_its_last_entry_offers_nothing_more() {
    let f = Fixture::new();
    four_volumes(&f);
    let progress = Progress::new(&f.db);

    finish(&progress, "v4", 1_000);

    assert!(
        offered_in_e(&progress).is_empty(),
        "nothing follows the last volume"
    );
}

/// Finished volumes in a row are walked past: the offer is the first unfinished entry after
/// the furthest one finished, however many finished ones lead up to it.
#[test]
fn finished_entries_in_a_row_are_walked_past_to_the_first_unfinished_one() {
    let f = Fixture::new();
    four_volumes(&f);
    let progress = Progress::new(&f.db);

    finish(&progress, "v1", 1_000);
    finish(&progress, "v2", 1_100);
    finish(&progress, "v3", 1_200);

    assert_eq!(
        vec![("v4".to_string(), "NEXT_UP".to_string())],
        offered_in_e(&progress)
    );
}

/// The entry that follows is already open: it is offered once, as what you are reading.
#[test]
fn what_follows_already_begun_is_offered_as_in_progress_and_only_so() {
    let f = Fixture::new();
    four_volumes(&f);
    let progress = Progress::new(&f.db);

    finish(&progress, "v1", 1_000);
    at_page(&progress, "v2", 40);

    assert_eq!(
        vec![("v2".to_string(), "IN_PROGRESS".to_string())],
        offered_in_e(&progress)
    );
}

/// Opened at its first page and closed again, it is still what follows: a record at page
/// nought is not a reason to skip it and offer the volume after.
#[test]
fn what_follows_opened_at_its_first_page_is_still_what_follows() {
    let f = Fixture::new();
    four_volumes(&f);
    let progress = Progress::new(&f.db);

    finish(&progress, "v1", 1_000);
    at_page(&progress, "v2", 0);

    assert_eq!(
        vec![("v2".to_string(), "NEXT_UP".to_string())],
        offered_in_e(&progress)
    );
}

#[test]
fn the_progress_of_a_series_is_read_once_not_once_per_volume() {
    let f = Fixture::new();
    let progress = Progress::new(&f.db);
    for entry in ["v1", "v2"] {
        progress
            .record(
                "default",
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
            "default",
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

/// The status of one edition as the shelf computes it.
fn read_status(f: &Fixture, edition: &str) -> String {
    Repository::new(&f.db)
        .series(&SeriesFilter::default(), SeriesSort::Name, 0, 0)
        .expect("listing")
        .into_iter()
        .find(|s| s.id == edition)
        .expect("the series")
        .read_status
}

fn finish(progress: &Progress, entry: &str, at: i64) {
    progress
        .record(
            "default",
            entry,
            &ProgressPatch {
                page: Some(189),
                finished: Some(true),
                ..Default::default()
            },
            at,
        )
        .expect("recording")
        .expect("the entry");
}

fn start_over(progress: &Progress, entry: &str, at: i64) {
    progress
        .record(
            "default",
            entry,
            &ProgressPatch {
                page: Some(0),
                finished: Some(false),
                rewind: true,
            },
            at,
        )
        .expect("recording")
        .expect("the entry");
}

/// `finished` and « have I read this » were the same column, and they are not the same
/// question. The rewind that starts a second reading clears the first, so the only mark a
/// finished volume left was erased by re-opening it.
#[test]
fn finishing_counts_the_endings_and_a_rewind_counts_none() {
    let f = Fixture::new();
    let progress = Progress::new(&f.db);

    finish(&progress, "v1", 1_000);
    assert_eq!(
        1,
        progress.of("v1").unwrap().expect("a record").times_finished
    );

    // Saying it a second time is the same ending: a client re-sending the last page of a
    // book it has already closed must not add a reading to the count.
    finish(&progress, "v1", 1_100);
    assert_eq!(
        1,
        progress.of("v1").unwrap().expect("a record").times_finished
    );

    let rewound = {
        start_over(&progress, "v1", 1_200);
        progress.of("v1").unwrap().expect("a record")
    };
    assert_eq!(
        1, rewound.times_finished,
        "starting over keeps what happened"
    );
    assert!(!rewound.finished, "and says where the reader is now");
    assert_eq!(0, rewound.page);

    finish(&progress, "v1", 1_300);
    assert_eq!(
        2,
        progress.of("v1").unwrap().expect("a record").times_finished
    );
}

fn rewind_to(
    progress: &Progress,
    entry: &str,
    page: i64,
) -> leaf_server::api::progress::ProgressDto {
    progress
        .record(
            "default",
            entry,
            &ProgressPatch {
                page: Some(page),
                rewind: true,
                ..Default::default()
            },
            2_000,
        )
        .expect("recording")
        .expect("the entry")
}

/// The contract says a rewind clears `finished`; the server kept it, so a volume re-opened
/// at its first page in the test of 22/10/2026 stayed « lu » until the client said
/// `finished: false` on its own.
#[test]
fn a_rewind_below_the_last_page_clears_finished_and_keeps_the_count() {
    let f = Fixture::new();
    let progress = Progress::new(&f.db);
    finish(&progress, "v1", 1_000);

    let rewound = rewind_to(&progress, "v1", 0);

    assert!(!rewound.finished, "a rewind says where the reader is now");
    assert_eq!(0, rewound.page);
    assert_eq!(1, rewound.times_finished, "and keeps what happened");
}

/// Rewinding to the last page is still being at the end: there is nothing to clear.
#[test]
fn a_rewind_to_the_last_page_leaves_it_finished() {
    let f = Fixture::new();
    let progress = Progress::new(&f.db);
    finish(&progress, "v1", 1_000);

    assert!(rewind_to(&progress, "v1", 189).finished);
}

/// What the patch says outright wins over what the rewind implies.
#[test]
fn a_rewind_that_says_finished_is_taken_at_its_word() {
    let f = Fixture::new();
    let progress = Progress::new(&f.db);
    finish(&progress, "v1", 1_000);

    let said = progress
        .record(
            "default",
            "v1",
            &ProgressPatch {
                page: Some(3),
                finished: Some(true),
                rewind: true,
            },
            2_000,
        )
        .expect("recording")
        .expect("the entry");

    assert!(said.finished);
    assert_eq!(1, said.times_finished, "the same ending, not a second one");
}

/// The defect the counter exists for: a series you had finished left the READ filter the
/// moment you opened one of its volumes again.
#[test]
fn a_series_being_reread_is_still_read() {
    let f = Fixture::new();
    let progress = Progress::new(&f.db);

    finish(&progress, "v1", 1_000);
    finish(&progress, "v2", 1_000);
    assert_eq!("READ", read_status(&f, "e"));

    start_over(&progress, "v1", 2_000);

    assert_eq!(
        "READ",
        read_status(&f, "e"),
        "having finished it is something that happened, and re-reading does not un-happen it"
    );
    // And the volume itself answers the other question, which is why both are kept.
    assert!(!progress.of("v1").unwrap().expect("a record").finished);
}

/// And not too loose in the other direction: the rule is every entry finished, not any.
#[test]
fn a_series_with_a_volume_never_finished_is_still_in_progress() {
    let f = Fixture::new();
    let progress = Progress::new(&f.db);

    finish(&progress, "v1", 1_000);
    assert_eq!("IN_PROGRESS", read_status(&f, "e"));
}

/// A volume finished once and rewound to page nought is not a series nobody started: the
/// UNREAD branch asks the same question as the READ one, and had to learn it too.
#[test]
fn a_series_entirely_rewound_is_not_unread_again() {
    let f = Fixture::new();
    let progress = Progress::new(&f.db);

    finish(&progress, "v1", 1_000);
    finish(&progress, "v2", 1_000);
    start_over(&progress, "v1", 2_000);
    start_over(&progress, "v2", 2_000);

    assert_eq!("READ", read_status(&f, "e"));
}

#[test]
fn marking_a_whole_series_read_fills_in_the_page_it_was_finished_at() {
    let f = Fixture::new();
    let progress = Progress::new(&f.db);

    let records = progress
        .record_series("default", "e", true, 5_000)
        .expect("recording")
        .expect("the series");

    assert_eq!(2, records.len());
    for one in &records {
        assert!(one.finished);
        assert_eq!(1, one.times_finished);
        // A volume finished at page nought is a contradiction, and a shelf would draw it as
        // an empty bar under the word « lu ».
        assert_eq!(189, one.page, "{one:?}");
    }
    assert_eq!("READ", read_status(&f, "e"));
}

/// The whole point of the route. Thirty volumes cost what two do, because it is one
/// statement over a `SELECT` and not one call per volume — the shape `store/db.rs` counts
/// statements to make visible.
#[test]
fn marking_a_whole_series_read_costs_the_same_whatever_it_holds() {
    let f = Fixture::new();
    let progress = Progress::new(&f.db);

    let before = f.db.statements();
    progress
        .record_series("default", "e", true, 5_000)
        .expect("recording");
    let two_volumes = f.db.statements() - before;

    f.db.write(|cx| {
        for n in 3..=32 {
            cx.execute(
                "INSERT INTO entry (id, edition_id, type, file, size, modified_at, added_at,
                                    volume_number, sort_key, page_count)
                 VALUES (?1, 'e', 'VOLUME', ?2, 1, 1, 1, ?3, ?3, 190)",
                (format!("v{n}"), format!("/v{n}.cbz"), n as f64),
            )?;
        }
        Ok(())
    })
    .expect("seeding thirty more");

    let before = f.db.statements();
    progress
        .record_series("default", "e", true, 6_000)
        .expect("recording");
    let thirty_two_volumes = f.db.statements() - before;

    assert_eq!(
        two_volumes, thirty_two_volumes,
        "marking a series read cost {thirty_two_volumes} statements for 32 volumes \
         against {two_volumes} for 2"
    );
}

/// Saying « not finished » about a book nobody opened is not a fact worth storing, and an
/// absent record already says it.
#[test]
fn marking_a_whole_series_unread_records_nothing_about_what_was_never_opened() {
    let f = Fixture::new();
    let progress = Progress::new(&f.db);

    let records = progress
        .record_series("default", "e", false, 5_000)
        .expect("recording")
        .expect("the series");

    assert!(records.is_empty());
    assert_eq!("UNREAD", read_status(&f, "e"));
}

/// Clearing the mark is not forgetting the position: one says « not finished », the other
/// says « never opened », and the menu offers the second.
#[test]
fn marking_a_read_series_unread_keeps_the_page_and_forgetting_it_does_not() {
    let f = Fixture::new();
    let progress = Progress::new(&f.db);

    progress
        .record_series("default", "e", true, 5_000)
        .expect("recording");
    let cleared = progress
        .record_series("default", "e", false, 6_000)
        .expect("recording")
        .expect("the series");
    assert_eq!(2, cleared.len());
    assert!(cleared.iter().all(|p| !p.finished));
    assert!(cleared.iter().all(|p| p.page == 189));
    // The endings are not undone by saying the book is not open at its last page.
    assert!(cleared.iter().all(|p| p.times_finished == 1));

    progress.forget_series("default", "e").expect("forgetting");
    assert!(progress.of_series("e").unwrap().is_empty());
    assert_eq!("UNREAD", read_status(&f, "e"));
}

/// The same contract as forgetting one: silent about whether there was anything to forget.
#[test]
fn forgetting_a_series_that_holds_nothing_is_not_a_failure() {
    let f = Fixture::new();
    Progress::new(&f.db)
        .forget_series("default", "e")
        .expect("forgetting");
    Progress::new(&f.db)
        .forget_series("default", "nowhere")
        .expect("forgetting");
}

#[test]
fn marking_a_series_that_is_not_there_records_nothing() {
    let f = Fixture::new();
    assert!(Progress::new(&f.db)
        .record_series("default", "nowhere", true, 5_000)
        .expect("recording")
        .is_none());
}

/// Progress belongs to the edition, and marking one whole does not reach into another.
#[test]
fn marking_a_series_read_leaves_the_other_editions_alone() {
    let f = Fixture::new();
    let progress = Progress::new(&f.db);

    progress
        .record_series("default", "e", true, 5_000)
        .expect("recording");

    assert!(progress.of_series("e2").unwrap().is_empty());
    assert_eq!("UNREAD", read_status(&f, "e2"));
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
        Progress::new(&db).forget("default", &entry).unwrap();
        let ahead = Arc::clone(&db);
        let behind = Arc::clone(&db);
        let (a, b) = (entry.clone(), entry.clone());

        let far = std::thread::spawn(move || {
            Progress::new(&ahead)
                .record(
                    "default",
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
                    "default",
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

/// The shelf says how far through a series a reader is, not only that they started.
///
/// `read_status` has three answers and « how far » is not one of them, so a tile drew a
/// full-width mark the moment a series was opened — the same emerald, the same four pixels
/// as the band's own progress bar above it, saying « started » where that one says « how
/// far ». Counted in the listing's own statement: a shelf of two hundred series must not be
/// two hundred more reads for one number.
#[test]
fn the_shelf_says_how_many_volumes_of_a_series_are_finished() {
    let f = Fixture::new();
    let progress = Progress::new(&f.db);

    let shelf = || {
        Repository::new(&f.db)
            .series(&SeriesFilter::default(), SeriesSort::Name, 0, 0)
            .expect("listing")
    };
    let far = |rows: &[leaf_server::api::dto::SeriesDto], id: &str| {
        rows.iter()
            .find(|s| s.id == id)
            .expect("a series")
            .read_entries
    };

    assert_eq!(0, far(&shelf(), "e"), "nothing read is nothing counted");

    // A page turned is started, and not finished.
    progress
        .record(
            "default",
            "v1",
            &ProgressPatch {
                page: Some(10),
                ..Default::default()
            },
            1,
        )
        .unwrap();
    assert_eq!(0, far(&shelf(), "e"), "opened is not finished");
    // Not nothing either: the bar used to jump a volume at a time and sit still in between,
    // which on a twenty-one volume series is most of the time.
    let part = |rows: &[leaf_server::api::dto::SeriesDto], id: &str| {
        rows.iter()
            .find(|s| s.id == id)
            .expect("a series")
            .part_read
    };
    assert!(
        part(&shelf(), "e") > 0.0,
        "a page turned counts for something"
    );

    progress
        .record(
            "default",
            "v1",
            &ProgressPatch {
                finished: Some(true),
                ..Default::default()
            },
            1,
        )
        .unwrap();
    assert_eq!(1, far(&shelf(), "e"));
    // And a volume finished stops being counted twice: it is whole, not whole plus a part.
    assert_eq!(0.0, part(&shelf(), "e"), "finished is not also partly read");

    // And it costs nothing per row: the same listing, the same statements.
    let before = f.db.statements();
    let rows = shelf();
    let cost = f.db.statements() - before;
    assert!(!rows.is_empty());
    assert!(
        cost < 12,
        "listing a shelf took {cost} statements for the count it carries"
    );
}

/// Forgetting is the reader's own act: a profile clearing its place in a series must not
/// clear the place another profile holds in it.
#[test]
fn forgetting_a_series_leaves_another_profiles_progress() {
    let f = Fixture::new();
    f.db.write(|cx| {
        cx.execute(
            "INSERT INTO profile (id, name, name_key, created_at) VALUES ('kid', 'Kid', 'kid', 1)",
            [],
        )
    })
    .unwrap();
    let progress = Progress::new(&f.db);
    progress.record_series("default", "e", true, 5_000).unwrap();
    progress.record_series("kid", "e", true, 5_000).unwrap();

    // Marking unread, forgetting one volume, forgetting the series: each under `default`.
    progress
        .record_series("default", "e", false, 6_000)
        .unwrap();
    progress.forget("default", "v1").unwrap();
    progress.forget_series("default", "e").unwrap();

    let rows: Vec<(String, String, bool)> =
        f.db.read(|cx| {
            cx.query(
                "SELECT profile_id, entry_id, finished FROM progress ORDER BY profile_id, entry_id",
                [],
                |r| Ok((r.get(0)?, r.get(1)?, r.get::<_, i64>(2)? == 1)),
            )
        })
        .unwrap();
    assert_eq!(
        vec![
            ("kid".to_string(), "v1".to_string(), true),
            ("kid".to_string(), "v2".to_string(), true),
        ],
        rows
    );
}
// ---------------------------------------------------------- the way a series is read

/// A series declaring `RIGHT_TO_LEFT`, as a person's library would have it.
fn a_right_to_left_series(server: &Server) -> String {
    a_volume(server);
    std::fs::write(
        server.library().join("Bleach/work.json"),
        br#"{"leaf":1,"title":"Bleach","readingDirection":"RIGHT_TO_LEFT"}"#,
    )
    .unwrap();
    server.scan();
    server.series()
}

async fn choose(
    server: &Server,
    series: &str,
    body: serde_json::Value,
) -> (StatusCode, serde_json::Value) {
    server
        .send(
            request(
                "PUT",
                &format!("/series/{series}/reading-direction"),
                READ_ONLY,
            )
            .header("content-type", "application/json")
            .body(json_body(body))
            .unwrap(),
        )
        .await
}

async fn the_series(server: &Server, series: &str) -> serde_json::Value {
    let (status, body) = server
        .send(
            request("GET", &format!("/series/{series}"), READ_ONLY)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::OK, status, "{body}");
    body
}

/// The reader's choice is not something the series says about itself, so it must not reach
/// `work.json`: a file that changed whenever somebody turned a page the other way would be
/// rewritten by a read-only key.
#[tokio::test]
async fn a_direction_chosen_for_a_series_comes_back_with_it_and_leaves_the_file_alone() {
    let server = Server::new();
    let series = a_right_to_left_series(&server);
    let sidecar = server.library().join("Bleach/work.json");
    let before = std::fs::read(&sidecar).unwrap();

    let (status, body) = choose(
        &server,
        &series,
        serde_json::json!({"readingDirection": "LEFT_TO_RIGHT"}),
    )
    .await;
    assert_eq!(StatusCode::OK, status, "{body}");
    assert_eq!(
        serde_json::json!("LEFT_TO_RIGHT"),
        body["readingDirectionChosen"]
    );

    let got = the_series(&server, &series).await;
    assert_eq!(serde_json::json!("RIGHT_TO_LEFT"), got["readingDirection"]);
    assert_eq!(
        serde_json::json!("LEFT_TO_RIGHT"),
        got["readingDirectionChosen"]
    );
    assert_eq!(before, std::fs::read(&sidecar).unwrap());
}

#[tokio::test]
async fn choosing_nothing_goes_back_to_what_the_series_declares() {
    let server = Server::new();
    let series = a_right_to_left_series(&server);
    choose(
        &server,
        &series,
        serde_json::json!({"readingDirection": "VERTICAL"}),
    )
    .await;

    let (status, body) = choose(
        &server,
        &series,
        serde_json::json!({"readingDirection": null}),
    )
    .await;
    assert_eq!(StatusCode::OK, status, "{body}");

    let got = the_series(&server, &series).await;
    assert!(got["readingDirectionChosen"].is_null(), "{got}");
    assert_eq!(serde_json::json!("RIGHT_TO_LEFT"), got["readingDirection"]);
    let rows: i64 = server
        .db
        .read(|cx| cx.query_one("SELECT COUNT(*) FROM reading_choice", [], |r| r.get(0)))
        .unwrap()
        .unwrap();
    assert_eq!(0, rows, "absence is what says \"by default\"");
}

/// The reason for a table of its own: the index is rebuilt from the files by a scan, and
/// a column in it would have forgotten the choice at the next one.
#[tokio::test]
async fn a_chosen_direction_survives_a_rescan() {
    let server = Server::new();
    let series = a_right_to_left_series(&server);
    choose(
        &server,
        &series,
        serde_json::json!({"readingDirection": "VERTICAL"}),
    )
    .await;

    server.scan();

    let got = the_series(&server, &series).await;
    assert_eq!(serde_json::json!("VERTICAL"), got["readingDirectionChosen"]);
}

#[tokio::test]
async fn a_word_the_contract_does_not_have_is_refused_by_name() {
    let server = Server::new();
    let series = a_right_to_left_series(&server);

    let (status, body) = choose(
        &server,
        &series,
        serde_json::json!({"readingDirection": "SIDEWAYS"}),
    )
    .await;
    assert_eq!(StatusCode::BAD_REQUEST, status, "{body}");
    let said = body["error"].as_str().unwrap_or_default();
    for word in ["SIDEWAYS", "LEFT_TO_RIGHT", "RIGHT_TO_LEFT", "VERTICAL"] {
        assert!(said.contains(word), "{said}");
    }
    assert!(the_series(&server, &series).await["readingDirectionChosen"].is_null());
}

/// An absent field is not `null`. `null` takes the choice away; a body that forgot the field
/// said nothing, and answering it 200 erased whatever the reader had chosen — the contract
/// marks the field required for exactly that reason.
#[tokio::test]
async fn a_body_without_the_field_is_refused_and_keeps_the_choice() {
    let server = Server::new();
    let series = a_right_to_left_series(&server);
    choose(
        &server,
        &series,
        serde_json::json!({"readingDirection": "VERTICAL"}),
    )
    .await;

    let (status, body) = choose(&server, &series, serde_json::json!({})).await;
    assert_eq!(StatusCode::BAD_REQUEST, status, "{body}");
    assert!(
        body["error"]
            .as_str()
            .unwrap_or_default()
            .contains("readingDirection"),
        "{body}"
    );
    assert_eq!(
        serde_json::json!("VERTICAL"),
        the_series(&server, &series).await["readingDirectionChosen"]
    );
}

/// The choice belongs to an edition: when the files of that edition leave the library, the
/// row goes with it rather than waiting for an edition of the same id to come back and
/// inherit a choice nobody made for it.
#[tokio::test]
async fn a_choice_goes_with_the_edition_it_was_made_for() {
    let server = Server::new();
    let series = a_right_to_left_series(&server);
    choose(
        &server,
        &series,
        serde_json::json!({"readingDirection": "VERTICAL"}),
    )
    .await;
    let rows = |server: &Server| -> i64 {
        server
            .db
            .read(|cx| cx.query_one("SELECT COUNT(*) FROM reading_choice", [], |r| r.get(0)))
            .unwrap()
            .unwrap()
    };
    assert_eq!(1, rows(&server));

    std::fs::remove_dir_all(server.library().join("Bleach")).unwrap();
    server.scan();

    assert_eq!(0, rows(&server), "the edition is gone, so is its choice");
}

#[tokio::test]
async fn choosing_for_no_series_is_a_404() {
    let server = Server::new();
    a_volume(&server);

    let (status, _) = choose(
        &server,
        "nothing-like-it",
        serde_json::json!({"readingDirection": "VERTICAL"}),
    )
    .await;
    assert_eq!(StatusCode::NOT_FOUND, status);
    let (status, _) = choose(
        &server,
        "nothing-like-it",
        serde_json::json!({"readingDirection": null}),
    )
    .await;
    assert_eq!(StatusCode::NOT_FOUND, status);
}

/// The choice rides the query that already builds the shelf, as a join. A lookup per
/// series would return the same list and ask a question for each of its rows.
#[test]
fn the_shelf_still_costs_what_it_cost() {
    let f = Fixture::new();
    let list = |f: &Fixture| {
        let before = f.db.statements();
        Repository::new(&f.db)
            .series(&SeriesFilter::default(), SeriesSort::Name, 0, 0)
            .unwrap();
        f.db.statements() - before
    };
    let without = list(&f);

    f.db.write(|cx| {
        for i in 0..12 {
            cx.execute(
                "INSERT INTO work (id, name, path) VALUES (?1, ?1, ?2)",
                (format!("cw{i}"), format!("/cw{i}")),
            )?;
            cx.execute(
                "INSERT INTO edition (id, work_id, path, implicit) VALUES (?1, ?2, ?3, 1)",
                (format!("ce{i}"), format!("cw{i}"), format!("/cw{i}/e")),
            )?;
            cx.execute(
                "INSERT INTO reading_choice (edition_id, reading_direction) VALUES (?1, 'VERTICAL')",
                [format!("ce{i}")],
            )?;
        }
        Ok(())
    })
    .unwrap();

    assert_eq!(
        without,
        list(&f),
        "a choice must not add a query per series"
    );
    assert!(Repository::new(&f.db)
        .choose_reading_direction("e", Some("VERTICAL"))
        .unwrap());
    assert_eq!(without, list(&f));
    assert!(!Repository::new(&f.db)
        .choose_reading_direction("nope", Some("VERTICAL"))
        .unwrap());
}

/// Writing the choice is a fixed handful of statements whatever the library holds: one to
/// ask whether the edition exists and one to write (or delete) the row. A write that
/// scanned or listed to find its edition would cost more on a large library and say nothing.
#[test]
fn choosing_a_direction_costs_two_statements_and_an_unknown_edition_costs_one() {
    let f = Fixture::new();
    let cost = |edition: &str, chosen: Option<&str>| {
        let before = f.db.statements();
        Repository::new(&f.db)
            .choose_reading_direction(edition, chosen)
            .unwrap();
        f.db.statements() - before
    };

    assert_eq!(2, cost("e", Some("VERTICAL")), "choosing");
    assert_eq!(2, cost("e", None), "choosing nothing");
    assert_eq!(1, cost("nope", Some("VERTICAL")), "an unknown edition");
}
