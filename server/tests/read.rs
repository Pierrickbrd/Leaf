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
                    "INSERT INTO work (id, universe_id, name, path, title, medium, author, status,
                                       reading_direction)
                     VALUES (?1, ?2, ?3, ?4, ?3, ?5, ?6, 'ongoing', 'RIGHT_TO_LEFT')",
                    (id, universe, name, format!("/library/{id}"), medium, author),
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
    assert!(fifteen <= 6, "listing took {fifteen} queries");
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
