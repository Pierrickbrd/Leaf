//! The read side: what a filter means, what a shelf costs, and what crosses the wire.
//!
//! Seeded directly rather than by scanning folders: a repository test that needs a scanner
//! to run is a repository test that fails for two reasons, and only one of them is its own.

use std::sync::Arc;

use axum::body::Body;
use axum::http::{Request, StatusCode};
use http_body_util::BodyExt;
use leaf_server::api::dto::{SeriesFilter, SeriesSort};
use leaf_server::api::keys::Keys;
use leaf_server::api::routes::{router, AppState};
use leaf_server::store::text::search_key;
use leaf_server::store::{Db, Repository};
use tower::ServiceExt;

// ------------------------------------------------------------------ fixture

struct Library {
    _dir: tempfile::TempDir,
    db: Arc<Db>,
}

impl Library {
    /// A universe whose name none of its works repeats — the only way to tell whether the
    /// universe is really filterable or merely spelled inside the work's own title.
    fn new() -> Self {
        let dir = tempfile::tempdir().expect("a directory");
        let db = Db::open(&dir.path().join("index.sqlite")).expect("opening");

        db.write(|cx| {
            cx.execute(
                "INSERT INTO universe (id, name, path) VALUES ('u-arran', 'Terres d''Arran', '/u')",
                [],
            )?;
            Ok(())
        })
        .expect("the universe");

        let library = Library {
            _dir: dir,
            db: Arc::new(db),
        };
        library.work(
            "elfes",
            Some("u-arran"),
            "Elfes",
            "Jarry",
            "bd",
            &["Fantasy", "Aventure"],
        );
        library.work(
            "nains",
            Some("u-arran"),
            "Nains",
            "Jarry",
            "bd",
            &["Fantasy"],
        );
        library.work("death", None, "Death Note", "Ohba", "manga", &["Thriller"]);
        library.edition("e-elfes", "elfes", None, "ongoing", 3);
        library.edition("e-nains", "nains", None, "ongoing", 2);
        library.edition("e-death", "death", None, "completed", 2);
        library
    }

    fn work(
        &self,
        id: &str,
        universe: Option<&str>,
        name: &str,
        author: &str,
        medium: &str,
        genres: &[&str],
    ) {
        self.db
            .write(|cx| {
                cx.execute(
                    "INSERT INTO work (id, universe_id, name, path, title, medium, status,
                                       reading_direction)
                     VALUES (?1, ?2, ?3, ?4, ?3, ?5, 'ongoing', 'RIGHT_TO_LEFT')",
                    (id, universe, name, format!("/library/{id}"), medium),
                )?;
                cx.execute(
                    "INSERT INTO work_author (work_id, name, key) VALUES (?1, ?2, ?3)",
                    (id, author, leaf_server::store::text::search_key(author)),
                )?;
                for genre in genres {
                    cx.execute(
                        "INSERT INTO work_genre (work_id, name, key) VALUES (?1, ?2, ?3)",
                        (id, genre, leaf_server::store::text::search_key(genre)),
                    )?;
                }
                Ok(())
            })
            .expect("a work");
    }

    /// Publisher and language are fixed: no test varies them, and a fixture with a knob
    /// nobody turns is a fixture that reads as though it mattered.
    fn edition(&self, id: &str, work: &str, name: Option<&str>, status: &str, volumes: i64) {
        let (publisher, language) = ("Glénat", "fr");
        self.db
            .write(|cx| {
                cx.execute(
                    "INSERT INTO edition (id, work_id, name, path, implicit, publisher, language, status)
                     VALUES (?1, ?2, ?3, ?4, 1, ?5, ?6, ?7)",
                    (id, work, name, format!("/library/{work}/{id}"), publisher, language, status),
                )?;
                // The index, as the scanner would have written it: the work's name and its
                // author, folded by the same function a query is folded with. Without it a
                // search over this fixture finds nothing, and `?q=` would look broken in a
                // test where the only thing missing is the scanner nobody wants to run here.
                let (title, author): (String, String) = cx.query_one(
                    "SELECT w.name, COALESCE(a.name, '') FROM work w
                     LEFT JOIN work_author a ON a.work_id = w.id WHERE w.id = ?1",
                    (work,),
                    |r| Ok((r.get(0)?, r.get(1)?)),
                )?
                .expect("the work this edition belongs to");
                cx.execute(
                    "INSERT INTO search (name, detail, kind, ref, edition_id, entry_id, label)
                     VALUES (?1, ?2, 'EDITION', ?3, ?3, NULL, ?4)",
                    (search_key(&title), search_key(&author), id, &title),
                )?;
                for v in 1..=volumes {
                    cx.execute(
                        "INSERT INTO entry (id, edition_id, type, file, size, modified_at, added_at,
                                            volume_number, sort_key, page_count)
                         VALUES (?1, ?2, 'VOLUME', ?3, 1000, 1, 1, ?4, ?4, 10)",
                        (
                            format!("{id}-v{v}"),
                            id,
                            format!("/library/{work}/{id}/Tome {v}.cbz"),
                            v as f64,
                        ),
                    )?;
                }
                Ok(())
            })
            .expect("an edition");
    }

    fn repository(&self) -> Repository<'_> {
        Repository::new(&self.db)
    }

    fn names(&self, filter: &SeriesFilter) -> Vec<String> {
        let mut found: Vec<String> = self
            .repository()
            .series(filter, SeriesSort::Name, 0, 0)
            .expect("listing")
            .into_iter()
            .map(|s| s.work)
            .collect();
        found.sort();
        found
    }

    async fn get(&self, path: &str) -> (StatusCode, serde_json::Value) {
        self.get_with(path, Some("8f3a92c1d4e5b6a7")).await
    }

    async fn get_with(&self, path: &str, key: Option<&str>) -> (StatusCode, serde_json::Value) {
        let state = AppState::new(
            Arc::clone(&self.db),
            Keys::parse(Some("desktop:8f3a92c1d4e5b6a7:read,import")).expect("keys"),
        );
        let mut request = Request::builder().uri(path);
        if let Some(key) = key {
            request = request.header("X-Leaf-Key", key);
        }
        let response = router(state)
            .oneshot(request.body(Body::empty()).unwrap())
            .await
            .expect("a response");
        let status = response.status();
        let bytes = response
            .into_body()
            .collect()
            .await
            .expect("a body")
            .to_bytes();
        let json = if bytes.is_empty() {
            serde_json::Value::Null
        } else {
            serde_json::from_slice(&bytes).expect("json")
        };
        (status, json)
    }
}

fn filter_of(field: &str, values: &[&str]) -> SeriesFilter {
    let values: Vec<String> = values.iter().map(ToString::to_string).collect();
    let mut f = SeriesFilter::default();
    match field {
        "author" => f.authors = values,
        "genre" => f.genres = values,
        "medium" => f.media = values,
        "universe" => f.universes = values,
        "publisher" => f.publishers = values,
        "status" => f.statuses = values,
        "work" => f.works = values,
        other => panic!("no such field: {other}"),
    }
    f
}

// ------------------------------------------------------------------- filters

#[test]
fn one_value_keeps_what_carries_it() {
    let library = Library::new();
    assert_eq!(
        vec!["Elfes", "Nains"],
        library.names(&filter_of("author", &["Jarry"]))
    );
    assert_eq!(
        vec!["Death Note"],
        library.names(&filter_of("medium", &["manga"]))
    );
    assert_eq!(
        vec!["Elfes", "Nains"],
        library.names(&filter_of("universe", &["Terres d'Arran"]))
    );
}

#[test]
fn repeating_a_parameter_widens_and_naming_another_narrows() {
    let library = Library::new();
    // Two authors: either of them.
    assert_eq!(
        vec!["Death Note", "Elfes", "Nains"],
        library.names(&filter_of("author", &["Jarry", "Ohba"]))
    );

    // An author and a genre: both. That is what a row of filter chips does, and what
    // anyone expects it to do.
    let mut narrow = filter_of("author", &["Jarry"]);
    narrow.genres = vec!["Aventure".into()];
    assert_eq!(vec!["Elfes"], library.names(&narrow));
}

#[test]
fn a_genre_is_matched_folded() {
    let library = Library::new();
    // A genre is typed by a human somewhere: "Fantasy", "fantasy" and "FANTASY" are one
    // filter, and the folded key is what the table is indexed on.
    for spelling in ["Fantasy", "fantasy", "FANTASY"] {
        assert_eq!(
            vec!["Elfes", "Nains"],
            library.names(&filter_of("genre", &[spelling])),
            "spelled {spelling}"
        );
    }
}

#[test]
fn the_other_editions_of_a_work_are_asked_for_by_the_work() {
    let library = Library::new();
    // Two editions of one story, cut differently — the case the section exists for.
    library.edition(
        "e-death-black",
        "death",
        Some("Black Edition"),
        "completed",
        1,
    );

    let siblings = library
        .repository()
        .series(&filter_of("work", &["death"]), SeriesSort::Name, 0, 0)
        .expect("listing");

    assert_eq!(2, siblings.len());
    assert!(siblings.iter().all(|s| s.work_id == "death"));
    // And the name says which is which, without repeating the work twice.
    let mut names: Vec<&str> = siblings.iter().map(|s| s.name.as_str()).collect();
    names.sort();
    assert_eq!(vec!["Death Note", "Death Note · Black Edition"], names);
}

#[test]
fn a_universe_the_work_already_names_is_not_repeated() {
    let library = Library::new();
    let elfes = library
        .repository()
        .one_series("e-elfes")
        .expect("reading")
        .expect("there");
    assert_eq!("Terres d'Arran · Elfes", elfes.name);
}

/// The name is what a screen shows; the id is what `/universes/{id}/orders` is addressed by.
///
/// A client holding only the name had to fetch every universe in the library and match
/// strings to find one — a whole request to answer a question the series already knew, and
/// a match that goes wrong the day somebody renames a folder.
#[test]
fn a_series_carries_the_id_of_its_universe_and_not_only_its_name() {
    let library = Library::new();
    let one = |id: &str| {
        library
            .repository()
            .one_series(id)
            .expect("reading")
            .expect("there")
    };

    let elfes = one("e-elfes");
    assert_eq!(Some("Terres d'Arran".to_string()), elfes.universe);
    assert_eq!(Some("u-arran".to_string()), elfes.universe_id);

    // And a work in no universe carries neither, rather than an empty string for one.
    let death = one("e-death");
    assert!(death.universe.is_none());
    assert!(death.universe_id.is_none());
}

// ----------------------------------------------------------------- the facets

#[test]
fn the_menu_only_offers_what_returns_something() {
    let library = Library::new();
    let facets = library.repository().facets().expect("facets");

    let authors: Vec<(&str, i64)> = facets
        .authors
        .iter()
        .map(|f| (f.value.as_str(), f.count))
        .collect();
    assert_eq!(vec![("Jarry", 2), ("Ohba", 1)], authors);

    // Every value the menu offers must match something, or the chip is a dead end.
    for author in &facets.authors {
        assert!(
            !library
                .names(&filter_of("author", &[&author.value]))
                .is_empty(),
            "the menu offered {} and the filter found nothing",
            author.value
        );
    }
    for genre in &facets.genres {
        assert!(!library
            .names(&filter_of("genre", &[&genre.value]))
            .is_empty());
    }
}

// -------------------------------------------------------------------- volumes

#[test]
fn a_series_counts_what_it_owns_and_names_what_it_misses() {
    let library = Library::new();
    library
        .db
        .write(|cx| {
            // Three volumes declared, the second missing from disk.
            cx.execute(
                "UPDATE edition SET volume_count = 3 WHERE id = 'e-nains'",
                [],
            )?;
            cx.execute("DELETE FROM entry WHERE id = 'e-nains-v2'", [])?;
            cx.execute(
                "INSERT INTO entry (id, edition_id, type, file, size, modified_at, added_at,
                                    volume_number, sort_key, page_count)
                 VALUES ('e-nains-v3', 'e-nains', 'VOLUME', '/n/t3.cbz', 1, 1, 1, 3.0, 3.0, 10)",
                [],
            )?;
            Ok(())
        })
        .expect("seeding");

    let nains = library
        .repository()
        .one_series("e-nains")
        .expect("reading")
        .expect("there");
    assert_eq!(2, nains.owned_volumes);
    assert_eq!(vec![2.0], nains.missing_volumes);
}

#[test]
fn a_volume_whose_chapters_are_here_is_not_reported_missing() {
    let library = Library::new();
    library.db
        .write(|cx| {
            cx.execute("UPDATE edition SET volume_count = 3 WHERE id = 'e-nains'", [])?;
            cx.execute(
                "INSERT INTO entry (id, edition_id, type, file, size, modified_at, added_at,
                                    volume_number, sort_key, page_count)
                 VALUES ('e-nains-c', 'e-nains', 'CHAPTER', '/n/c.cbz', 1, 1, 1, NULL, 2.5, 10)",
                [],
            )?;
            // The chapter says which volume it came from. That volume is no longer missing:
            // you hold its content under another name.
            cx.execute(
                "INSERT INTO chapter (id, edition_id, entry_id, raw, label, number, kind, position, volume)
                 VALUES ('c1', 'e-nains', 'e-nains-c', '12', 'Chapitre 12', 12.0, 'CHAPTER', 0, 3.0)",
                [],
            )?;
            Ok(())
        })
        .expect("seeding");

    let nains = library
        .repository()
        .one_series("e-nains")
        .expect("reading")
        .expect("there");
    assert!(
        nains.missing_volumes.is_empty(),
        "volume 3 arrived as chapters and must not be called missing: {:?}",
        nains.missing_volumes
    );
}

// ---------------------------------------------------------------------- cost

fn cost(db: &Db, f: impl FnOnce()) -> u64 {
    let before = db.statements();
    f();
    db.statements() - before
}

#[test]
fn listing_the_series_does_not_ask_one_question_per_series() {
    let library = Library::new();
    let three = cost(&library.db, || {
        library
            .repository()
            .series(&SeriesFilter::default(), SeriesSort::Name, 0, 0)
            .unwrap();
    });

    for i in 0..12 {
        library.work(
            &format!("w{i}"),
            None,
            &format!("Série {i}"),
            "Auteur",
            "manga",
            &["Aventure"],
        );
        library.edition(&format!("e{i}"), &format!("w{i}"), None, "ongoing", 2);
    }

    let fifteen = cost(&library.db, || {
        library
            .repository()
            .series(&SeriesFilter::default(), SeriesSort::Name, 0, 0)
            .unwrap();
    });

    // Five times the shelf, the same number of questions. A list that asks one per series
    // returns the right list and asks two hundred and one questions to do it.
    assert_eq!(
        three, fifteen,
        "the cost of a shelf must not follow its length"
    );
    // One query for the page itself, and one each for volumes, claimed volumes, missing
    // chapters, genres, authors, artists and tags — eight queries whatever the shelf holds,
    // never one of them per series.
    assert!(fifteen <= 8, "listing took {fifteen} queries");
}

#[test]
fn one_series_does_not_cost_the_whole_library() {
    let library = Library::new();
    for i in 0..20 {
        library.work(
            &format!("w{i}"),
            None,
            &format!("Série {i}"),
            "Auteur",
            "manga",
            &[],
        );
        library.edition(&format!("e{i}"), &format!("w{i}"), None, "ongoing", 3);
    }

    let whole = cost(&library.db, || {
        library
            .repository()
            .series(&SeriesFilter::default(), SeriesSort::Name, 0, 0)
            .unwrap();
    });
    let one = cost(&library.db, || {
        library.repository().one_series("e-elfes").unwrap();
    });

    assert!(
        one <= whole,
        "one series took {one} queries against {whole} for everything"
    );
}

// -------------------------------------------------------------------- routes

#[tokio::test]
async fn the_shelf_answers_a_page_and_a_total() {
    let library = Library::new();
    let (status, body) = library.get("/series?size=2").await;

    assert_eq!(StatusCode::OK, status);
    assert_eq!(2, body["items"].as_array().unwrap().len());
    assert_eq!(
        3, body["total"],
        "the total counts matches, not what was returned"
    );
    assert_eq!(2, body["size"]);
}

/// The shelf's own alphabet, which had the defect the search had: `w.name` ordered with
/// SQLite's default collation, so a series named « Élève » sorted after « Zorro ». Invisible
/// on a library whose titles all begin with an ASCII letter, and wrong the day one does not.
#[tokio::test]
async fn the_shelf_puts_an_accent_at_its_letter_and_a_digit_first() {
    let library = Library::new();
    library.work("eleve", None, "Élève", "Anon", "bd", &[]);
    library.work("zorro", None, "Zorro", "Anon", "bd", &[]);
    library.work("edena", None, "Edena", "Anon", "bd", &[]);
    library.work("ferme", None, "Ferme", "Anon", "bd", &[]);
    library.work("mille", None, "1984", "Anon", "bd", &[]);
    for id in ["eleve", "zorro", "edena", "ferme", "mille"] {
        library.edition(&format!("e-{id}"), id, None, "ongoing", 1);
    }

    let (status, body) = library.get("/series?sort=name&direction=asc&size=50").await;
    assert_eq!(StatusCode::OK, status);
    let found: Vec<String> = body["items"]
        .as_array()
        .unwrap()
        .iter()
        .map(|one| one["work"].as_str().unwrap().to_string())
        .collect();
    let at = |what: &str| found.iter().position(|one| one == what).expect(what);

    // A title beginning with a number comes before every letter. Few will, and the one that
    // does should not be filed under Z.
    assert_eq!(Some(&"1984".to_string()), found.first(), "{found:?}");

    // É sits with the E's, between Edena and Ferme — not after Zorro, which is where the
    // default collation put it.
    assert!(at("Edena") < at("Élève"), "{found:?}");
    assert!(at("Élève") < at("Ferme"), "{found:?}");
    assert!(at("Ferme") < at("Zorro"), "{found:?}");

    // And turned round it is the same alphabet read backwards — the whole shelf, not only
    // its ends, because nothing here ties and a reversal that is not exact hides a tie
    // whose two rows can then swap between two pages.
    let (_, down) = library
        .get("/series?sort=name&direction=desc&size=50")
        .await;
    let backwards: Vec<String> = down["items"]
        .as_array()
        .unwrap()
        .iter()
        .map(|one| one["work"].as_str().unwrap().to_string())
        .collect();
    assert_eq!(
        found.iter().rev().cloned().collect::<Vec<_>>(),
        backwards,
        "the reversed shelf is not the shelf reversed"
    );
    assert_eq!(Some(&"1984".to_string()), backwards.last(), "{backwards:?}");
}

/// An alphabet that runs A, B, … Z, À, Â is not an alphabet. `COLLATE NOCASE` knows only
/// ASCII, so ordering the search on the raw label put « À la verticale » and « Âmes sœurs »
/// *after* Z — and reversed, the list opened on them. On a French library that is a fifth of
/// the titles past the end of their own alphabet.
#[tokio::test]
async fn an_accented_title_sorts_with_its_letter_and_not_after_z() {
    let library = Library::new();
    library
        .db
        .write(|cx| {
            for (name, label) in [
                ("1984", "1984 chronique"),
                ("ames soeurs", "Âmes sœurs"),
                ("a la verticale", "À la verticale"),
                ("adultes", "Adultes"),
                ("zenith", "Zénith"),
                ("brouillard", "Brouillard"),
            ] {
                // One word they all carry, so the query settles which rows come back and
                // the assertions are about the order alone.
                cx.execute(
                    "INSERT INTO search (name, detail, kind, ref, edition_id, entry_id, label)
                     VALUES (?1, 'partout', 'ENTRY', ?2, 'e-elfes', 'e-elfes-v1', ?3)",
                    (name, label, label),
                )?;
            }
            Ok(())
        })
        .expect("the entries indexed");

    let labels = |body: &serde_json::Value| -> Vec<String> {
        body["items"]
            .as_array()
            .unwrap()
            .iter()
            .map(|one| one["label"].as_str().unwrap().to_string())
            .collect()
    };

    let (status, up) = library
        .get("/search?q=partout&size=50&sort=name&direction=asc&kind=ENTRY")
        .await;
    assert_eq!(StatusCode::OK, status);
    let found = labels(&up);
    assert_eq!(6, found.len(), "{found:?}");
    let at = |what: &str| found.iter().position(|one| one == what).expect(what);

    // The accents sit among the A's, where a reader looks for them, and every one of them
    // comes before the B. Where exactly they fall among the A's is the space's business —
    // « À la verticale » before « Adultes » is word-by-word order and is not the defect.
    assert!(at("À la verticale") < at("Brouillard"), "{found:?}");
    assert!(at("Âmes sœurs") < at("Brouillard"), "{found:?}");
    assert!(at("Adultes") < at("Brouillard"), "{found:?}");
    assert!(at("Brouillard") < at("Zénith"), "{found:?}");

    // And a number comes before every letter, wherever it is that the title starts with one.
    assert_eq!(
        Some(&"1984 chronique".to_string()),
        found.first(),
        "{found:?}"
    );

    // And turned round it opens on the Z, not on the accents.
    let (_, down) = library
        .get("/search?q=partout&size=50&sort=name&direction=desc&kind=ENTRY")
        .await;
    assert_eq!(
        Some(&"Zénith".to_string()),
        labels(&down).first(),
        "{:?}",
        labels(&down)
    );
}

/// A row of pills above a list of files counts files. Saying « Non lues 5 » over sixty
/// rows counts series, which is neither what the reader is looking at nor what the pill
/// beneath their finger would leave.
#[tokio::test]
async fn the_pills_above_a_list_of_files_count_files() {
    let library = Library::new();
    // Three editions, seven volumes between them. One volume of Elfes is finished and one is
    // open partway, which is three read statuses over files where the shelf sees two.
    library
        .db
        .write(|cx| {
            cx.execute(
                "INSERT INTO progress (entry_id, edition_id, page, finished, updated_at)
                 VALUES ('e-elfes-v1', 'e-elfes', 10, 1, 100)",
                [],
            )?;
            cx.execute(
                "INSERT INTO progress (entry_id, edition_id, page, finished, updated_at)
                 VALUES ('e-elfes-v2', 'e-elfes', 4, 0, 200)",
                [],
            )?;
            Ok(())
        })
        .expect("some reading");

    // The scanner indexes every entry it records; this fixture indexes only editions, and a
    // row of pills over files counts the very population the file list is drawn from — the
    // search index. Written here rather than in `edition`, which the other tests read.
    library
        .db
        .write(|cx| {
            for (edition, volumes) in [("e-elfes", 3), ("e-nains", 2), ("e-death", 2)] {
                for v in 1..=volumes {
                    cx.execute(
                        "INSERT INTO search (name, detail, kind, ref, edition_id, entry_id, label)
                         VALUES (?1, '', 'ENTRY', ?2, ?3, ?2, ?4)",
                        (
                            search_key(&format!("{edition} tome {v}")),
                            format!("{edition}-v{v}"),
                            edition,
                            format!("Tome {v}"),
                        ),
                    )?;
                }
            }
            Ok(())
        })
        .expect("the entries indexed");

    // An axis with nothing in it is absent from the answer rather than empty, so a missing
    // one is read as no values and not as a failure.
    let counted = |body: &serde_json::Value, axis: &str| -> Vec<(String, i64)> {
        body.get(axis)
            .and_then(|one| one.as_array())
            .map(|all| all.as_slice())
            .unwrap_or_default()
            .iter()
            .map(|one| {
                (
                    one["value"].as_str().unwrap().to_string(),
                    one["count"].as_i64().unwrap(),
                )
            })
            .collect()
    };

    let (status, files) = library.get("/filters?over=files").await;
    assert_eq!(StatusCode::OK, status);

    // Seven files: three of Elfes, two of Nains, two of Death Note. One read, one in
    // progress, five untouched — and the three add up to every file there is.
    let read = counted(&files, "readStatuses");
    let total: i64 = read.iter().map(|(_, n)| n).sum();
    assert_eq!(7, total, "{read:?}");
    assert_eq!(
        Some(&5),
        read.iter().find(|(v, _)| v == "UNREAD").map(|(_, n)| n)
    );
    assert_eq!(
        Some(&1),
        read.iter().find(|(v, _)| v == "READ").map(|(_, n)| n)
    );
    assert_eq!(
        Some(&1),
        read.iter()
            .find(|(v, _)| v == "IN_PROGRESS")
            .map(|(_, n)| n)
    );

    // The same axes over series count something else entirely, and still do.
    let (_, series) = library.get("/filters").await;
    let over_series: i64 = counted(&series, "readStatuses")
        .iter()
        .map(|(_, n)| n)
        .sum();
    assert_eq!(3, over_series, "the shelf counts editions, not files");

    // Narrowed by what is being searched for, because a count that ignores the query does
    // not describe the list it sits above.
    let (_, elves) = library.get("/filters?over=files&q=e-elfes").await;
    let narrowed: i64 = counted(&elves, "readStatuses").iter().map(|(_, n)| n).sum();
    assert!(narrowed < 7, "the query narrowed nothing: {narrowed}");
    assert!(narrowed > 0, "the query matched nothing");

    // And it is the *same* population, term for term. The two were written twice and
    // disagreed: the list matched each term as a prefix and found sixty files, while the
    // pills above it matched the whole query and counted eleven. A pill saying eleven over
    // sixty rows is a worse answer than a pill saying nothing.
    for typed in ["tome", "e-elf", "tome%201", ""] {
        let (_, pills) = library.get(&format!("/filters?over=files&q={typed}")).await;
        let (_, list) = library
            .get(&format!(
                "/search?q={typed}&size=200&kind=ENTRY&kind=CHAPTER"
            ))
            .await;
        let shown = list["fileTotal"].as_i64().unwrap_or_default();
        let above: i64 = counted(&pills, "readStatuses").iter().map(|(_, n)| n).sum();
        if typed.is_empty() {
            // Nothing typed is not a search; the row then counts every file there is.
            assert_eq!(7, above, "empty query");
            continue;
        }
        assert_eq!(
            shown, above,
            "« {typed} »: {shown} rows under {above} counted"
        );
    }

    // Every axis, not two: a panel offering eight of them opens in this scope too, and a
    // word shown there must carry the same number as the same word in the row above it.
    for axis in ["readStatuses", "media", "universes", "authors", "genres"] {
        assert!(!counted(&files, axis).is_empty(), "{axis} counted nothing");
    }

    // An author is counted once per file of their work, not once per work: seven files
    // across three editions, all by the same two writers of the fixture.
    let by_author: i64 = counted(&files, "authors").iter().map(|(_, n)| n).sum();
    assert_eq!(7, by_author, "{:?}", counted(&files, "authors"));

    // A genre is grouped on its folded key, so one spelling comes back per genre and the
    // count is files and not works.
    let fantasy = counted(&files, "genres")
        .into_iter()
        .find(|(v, _)| v == "Fantasy")
        .map(|(_, n)| n);
    assert_eq!(Some(5), fantasy, "Elfes' three files and Nains' two");

    // A universe covers only the works inside it: Death Note has none.
    assert_eq!(
        vec![("Terres d'Arran".to_string(), 5)],
        counted(&files, "universes")
    );
}

/// The ways through a universe, over the wire — and at a cost that does not grow with how
/// many of them there are.
#[tokio::test]
async fn the_ways_through_an_universe_are_answered_whole_and_in_two_questions() {
    let library = Library::new();
    // Two orders over the works the fixture already holds, written straight into the rows
    // the scanner would have derived: this asks what the route answers, not how it is filled.
    library
        .db
        .write(|cx| {
            for (id, declared, name, position, default) in [
                ("o-main", "main", "Ordre conseillé", 0, 1),
                ("o-pub", "publication", "Ordre de parution", 1, 0),
            ] {
                cx.execute(
                    "INSERT INTO reading_order (id, universe_id, declared_id, name, position,
                                                is_default)
                     VALUES (?1, 'u-arran', ?2, ?3, ?4, ?5)",
                    rusqlite::params![id, declared, name, position, default],
                )?;
            }
            // A whole work, then a volume range of one edition, then the first work again:
            // the interleaving a flat list of editions cannot express.
            for (id, order, position, work, unit, edition, from, to) in [
                ("s1", "o-main", 0, "elfes", None, None, None, None),
                (
                    "s2",
                    "o-main",
                    1,
                    "nains",
                    Some("VOLUME"),
                    Some("e-nains"),
                    Some(1.0),
                    Some(2.0),
                ),
                (
                    "s3",
                    "o-main",
                    2,
                    "elfes",
                    Some("CHAPTER"),
                    None,
                    Some(121.0),
                    None,
                ),
                ("s4", "o-pub", 0, "nains", None, None, None, None),
            ] {
                cx.execute(
                    "INSERT INTO reading_order_step (id, order_id, position, work_id, unit,
                                                     edition_id, from_number, to_number)
                     VALUES (?1,?2,?3,?4,?5,?6,?7,?8)",
                    rusqlite::params![id, order, position, work, unit, edition, from, to],
                )?;
            }
            Ok(())
        })
        .expect("some orders");

    let (status, universes) = library.get("/universes").await;
    assert_eq!(StatusCode::OK, status);
    assert_eq!(1, universes.as_array().unwrap().len());
    assert_eq!("Terres d'Arran", universes[0]["name"]);
    assert_eq!(2, universes[0]["orderCount"]);

    let (status, orders) = library.get("/universes/u-arran/orders").await;
    assert_eq!(StatusCode::OK, status);
    let all = orders.as_array().unwrap();
    assert_eq!(2, all.len());

    // Addressed by what the file called it, in the order the file wrote them.
    assert_eq!("main", all[0]["id"]);
    assert_eq!("Ordre conseillé", all[0]["name"]);
    assert_eq!(true, all[0]["default"]);
    // False is absent rather than written: a default nobody holds says nothing.
    assert!(all[1].get("default").is_none(), "{:?}", all[1]);

    let steps = all[0]["steps"].as_array().unwrap();
    assert_eq!(3, steps.len());
    // A whole work carries no unit and no bounds at all.
    assert!(steps[0].get("unit").is_none(), "{:?}", steps[0]);
    assert!(steps[0].get("from").is_none(), "{:?}", steps[0]);
    assert_eq!("Elfes", steps[0]["work"]);
    // A volume range says which edition it is counted in, under the name `/series` uses.
    assert_eq!("VOLUME", steps[1]["unit"]);
    assert_eq!("e-nains", steps[1]["seriesId"]);
    assert_eq!(1.0, steps[1]["from"]);
    assert_eq!(2.0, steps[1]["to"]);
    // And a chapter range names none, with an open end written as no `to`.
    assert_eq!("CHAPTER", steps[2]["unit"]);
    assert!(steps[2].get("seriesId").is_none(), "{:?}", steps[2]);
    assert_eq!(121.0, steps[2]["from"]);
    assert!(steps[2].get("to").is_none(), "{:?}", steps[2]);

    // Two questions for two orders and four steps — and still two for one order, so the
    // cost is the shape of the answer and not its size.
    let both = cost(&library.db, || {
        library
            .repository()
            .orders_of_universe("u-arran")
            .expect("orders");
    });
    assert_eq!(2, both, "{both} questions for two orders");

    library
        .db
        .write(|cx| {
            cx.execute("DELETE FROM reading_order WHERE id = 'o-pub'", [])?;
            Ok(())
        })
        .unwrap();
    let one = cost(&library.db, || {
        library
            .repository()
            .orders_of_universe("u-arran")
            .expect("orders");
    });
    assert_eq!(both, one, "one order cost {one} where two cost {both}");

    library
        .db
        .write(|cx| {
            cx.execute(
                "UPDATE edition SET name = 'Deluxe' WHERE id = 'e-nains'",
                [],
            )?;
            Ok(())
        })
        .unwrap();
    let (status, named) = library.get("/universes/u-arran/orders").await;
    assert_eq!(status, StatusCode::OK);
    assert_eq!(named[0]["steps"][1]["series"], "Deluxe");
}

/// A universe that declares no order answers an empty list. Declaring none is the ordinary
/// case, and a 404 would say the universe does not exist.
#[tokio::test]
async fn an_universe_with_no_order_answers_an_empty_list() {
    let library = Library::new();
    let (status, orders) = library.get("/universes/u-arran/orders").await;
    assert_eq!(StatusCode::OK, status);
    assert!(orders.as_array().unwrap().is_empty());

    let (status, universes) = library.get("/universes").await;
    assert_eq!(StatusCode::OK, status);
    assert_eq!(0, universes[0]["orderCount"]);
}

/// Where the reader left off, across the whole shelf. No count and no arrival date stands in
/// for it, which is why it took the place of `updated` — an order that said only that a
/// series had received something, now what `added` itself means.
#[tokio::test]
async fn a_shelf_ordered_by_reading_puts_the_last_one_opened_first() {
    let library = Library::new();
    library
        .db
        .write(|cx| {
            for (entry, edition, at) in [
                ("e-death-v1", "e-death", 300),
                ("e-elfes-v1", "e-elfes", 100),
            ] {
                cx.execute(
                    "INSERT INTO progress (entry_id, edition_id, page, finished, updated_at)
                     VALUES (?1, ?2, 3, 0, ?3)",
                    (entry, edition, at),
                )?;
            }
            Ok(())
        })
        .expect("some reading");

    let names = |body: &serde_json::Value| -> Vec<String> {
        body["items"]
            .as_array()
            .unwrap()
            .iter()
            .map(|one| one["name"].as_str().unwrap().to_string())
            .collect()
    };

    let (status, body) = library.get("/series?sort=read").await;
    assert_eq!(StatusCode::OK, status);
    // Death Note was opened after Elfes. Nains was never opened, and sorts with the other
    // absences at the far end rather than ahead of everything that was read.
    assert_eq!(
        vec![
            "Death Note",
            "Terres d'Arran · Elfes",
            "Terres d'Arran · Nains"
        ],
        names(&body)
    );

    // Reversed, the oldest reading first — and the unread stays at the far end either way,
    // because "never opened" is not a date and does not belong at one end of a range.
    let (_, oldest) = library.get("/series?sort=read&direction=asc").await;
    assert_eq!(
        vec![
            "Terres d'Arran · Elfes",
            "Death Note",
            "Terres d'Arran · Nains"
        ],
        names(&oldest)
    );
}

/// Asked for over the wire and not only in the SQL the order builds: the direction has to
/// survive the query string, the handler and the repository, and nothing between `sql()` and
/// the answer was covered — a parameter read into a struct nobody passes on is invisible.
#[tokio::test]
async fn a_shelf_asked_for_in_reverse_comes_back_in_reverse() {
    let library = Library::new();
    let names = |body: &serde_json::Value| -> Vec<String> {
        body["items"]
            .as_array()
            .unwrap()
            .iter()
            .map(|one| one["name"].as_str().unwrap().to_string())
            .collect()
    };

    let counts = |body: &serde_json::Value| -> Vec<i64> {
        body["items"]
            .as_array()
            .unwrap()
            .iter()
            .map(|one| one["entryCount"].as_i64().unwrap())
            .collect()
    };

    let (status, most) = library.get("/series?sort=volumes&direction=desc").await;
    assert_eq!(StatusCode::OK, status);
    let (_, fewest) = library.get("/series?sort=volumes&direction=asc").await;
    assert_eq!(3, counts(&most).len());
    assert_ne!(counts(&most), counts(&fewest), "the direction was ignored");
    assert!(counts(&most).windows(2).all(|two| two[0] >= two[1]));
    assert!(counts(&fewest).windows(2).all(|two| two[0] <= two[1]));

    // Only the criterion turns round. Series tied on it stay in alphabetical order either
    // way, so the two shelves are not mirrors of one another and are not asserted to be:
    // a reader reversing a count has not asked for the names to run backwards as well.
    assert_eq!(vec!["Death Note", "Terres d'Arran · Nains"], {
        let mut tied = names(&fewest);
        tied.truncate(2);
        tied
    });

    // And the alphabet, whose familiar direction is the other one and which has no ties.
    let (_, forwards) = library.get("/series?sort=name&direction=asc").await;
    let (_, backwards) = library.get("/series?sort=name&direction=desc").await;
    assert_eq!(
        names(&forwards),
        names(&backwards).into_iter().rev().collect::<Vec<_>>()
    );

    // A criterion named with no direction keeps its own: `desc` for a count.
    let (_, natural) = library.get("/series?sort=volumes").await;
    assert_eq!(names(&most), names(&natural));
}

#[tokio::test]
async fn a_default_never_crosses_the_wire() {
    let library = Library::new();
    let (_, body) = library.get("/series").await;
    let first = &body["items"][0];

    // A default does not cross the wire: an unread series omits readStatus entirely,
    // because "UNREAD" is the default.
    assert!(
        first.get("readStatus").is_none(),
        "readStatus must be absent when UNREAD"
    );
    assert!(
        first.get("missingVolumes").is_none(),
        "an empty list is absent"
    );
    assert!(first.get("edition").is_none(), "a null is absent");
    assert!(
        first.get("entryCount").is_some(),
        "a field with no default is always there"
    );
}

#[tokio::test]
async fn the_filters_reach_the_query_string() {
    let library = Library::new();
    let (_, body) = library.get("/series?author=Jarry&genre=Aventure").await;
    let works: Vec<&str> = body["items"]
        .as_array()
        .unwrap()
        .iter()
        .map(|s| s["work"].as_str().unwrap())
        .collect();
    assert_eq!(vec!["Elfes"], works);
}

/// The shelf, reduced to what the search finds — the same folding as `/search`, so a
/// half-typed word already finds something and accents are not a trap.
#[tokio::test]
async fn a_query_reduces_the_shelf_to_what_it_finds() {
    let library = Library::new();
    let (status, body) = library.get("/series?q=elf").await;

    assert_eq!(StatusCode::OK, status);
    assert_eq!(1, body["total"]);
    assert_eq!("Elfes", body["items"][0]["work"]);
}

/// A search runs *inside* what is showing. A lit chip is a statement about what you are
/// looking at, so the two narrow each other rather than the last one winning.
#[tokio::test]
async fn a_query_runs_inside_the_chips_that_are_lit() {
    let library = Library::new();
    let (_, both) = library.get("/series?q=jarry&medium=bd").await;
    assert_eq!(2, both["total"], "Jarry drew both of the albums");

    let (_, none) = library.get("/series?q=jarry&medium=manga").await;
    assert_eq!(
        0, none["total"],
        "and none of the manga, whatever the query finds"
    );
}

/// The answer a client cannot work out for itself: nothing matching is an empty shelf, and
/// not the whole one. An empty set of ids means "no restriction" to the query builder.
#[tokio::test]
async fn a_query_nothing_matches_is_an_empty_shelf() {
    let library = Library::new();
    let (status, body) = library.get("/series?q=zzzz").await;

    assert_eq!(StatusCode::OK, status);
    assert_eq!(0, body["total"]);
    assert!(body["items"].as_array().unwrap().is_empty());
}

/// A cleared field is not a search. The client sends `q=` on its way back to the whole
/// shelf, exactly as it sends a cleared chip.
#[tokio::test]
async fn a_blank_query_is_the_whole_shelf() {
    let library = Library::new();
    let (_, body) = library.get("/series?q=%20").await;
    assert_eq!(3, body["total"]);
}

/// Relevance orders the search; the shelf keeps its own order. A grid that reshuffled itself
/// as you typed would be a different thing from the one you were reading a second ago.
#[tokio::test]
async fn a_query_does_not_take_over_the_order() {
    let library = Library::new();
    let (_, body) = library.get("/series?q=jarry&sort=name").await;
    let works: Vec<&str> = body["items"]
        .as_array()
        .unwrap()
        .iter()
        .map(|s| s["work"].as_str().unwrap())
        .collect();
    assert_eq!(vec!["Elfes", "Nains"], works);
}

#[tokio::test]
async fn an_unknown_series_is_a_404_carrying_the_documented_shape() {
    let library = Library::new();
    let (status, body) = library.get("/series/nexistepas").await;
    assert_eq!(StatusCode::NOT_FOUND, status);
    assert_eq!("unknown series", body["error"]);
}

#[tokio::test]
async fn a_read_route_needs_a_key_and_health_does_not() {
    let library = Library::new();

    let (status, body) = library.get_with("/series", None).await;
    assert_eq!(StatusCode::FORBIDDEN, status);
    assert_eq!("unknown key", body["error"]);

    let (status, _) = library.get_with("/series", Some("0000000000000000")).await;
    assert_eq!(StatusCode::FORBIDDEN, status);

    // The one route that answers without a key, which is what makes it a health check.
    let (status, body) = library.get_with("/health", None).await;
    assert_eq!(StatusCode::OK, status);
    assert_eq!(3, body["library"]);
}

#[test]
fn asking_for_the_chapters_of_no_series_at_all_asks_the_database_nothing() {
    // The shelf hands the repository whatever it drew. An empty page must not become a
    // query with an empty IN list, which SQLite answers slowly and pointlessly.
    let library = Library::new();
    let repository = Repository::new(&library.db);
    assert!(repository.chapters_of_entries(&[]).unwrap().is_empty());
}

#[tokio::test]
async fn a_misspelled_search_held_to_a_filter_stays_held_to_it() {
    // The approximate fallback reads what it compares rather than asking an index, so what
    // it reads has to stay bounded — by the shelf, and by the filter when there is one.
    let library = Library::new();
    // Nothing matches exactly, so it falls through to the guess — with the filter still on.
    let (status, body) = library
        .get("/search?q=Blaech&medium=manga&kind=SERIES")
        .await;
    assert_eq!(status, StatusCode::OK, "{body}");
    assert!(body.is_array(), "{body}");

    // And with a publisher instead, which narrows to a different set of editions.
    let (status, body) = library.get("/search?q=Blaech&publisher=Kana").await;
    assert_eq!(status, StatusCode::OK, "{body}");
}

#[tokio::test]
async fn explicitly_sorted_legacy_search_keeps_the_array_and_matches_paged_results() {
    let library = Library::new();
    for sort in ["name", "added", "read", "volumes"] {
        for direction in ["asc", "desc"] {
            let query = format!("/search?q=e&sort={sort}&direction={direction}");
            let (status, legacy) = library.get(&query).await;
            assert_eq!(status, StatusCode::OK);
            assert!(legacy.is_array());
            assert!(!legacy.as_array().unwrap().is_empty());
            let (status, paged) = library.get(&format!("{query}&page=0&size=40")).await;
            assert_eq!(status, StatusCode::OK);
            assert_eq!(legacy, paged["items"], "{sort} {direction}");
        }
    }
}
