//! The age of a series, and the ceiling a restricted profile reads under.
//!
//! The anti-goal this file holds: a child never reaches a series above their age, by any
//! route. Every route that names a series, an entry, a chapter, an arc or a universe is
//! asked here as the restricted profile, and a hidden one must answer exactly what an id
//! that never existed answers — a refusal, or any other difference, would say it is there.

mod common;

use std::sync::Arc;

use axum::body::Body;
use axum::http::StatusCode;
use common::{archive_bytes, json_body, request, Server, IMPORTER};
use leaf_server::api::dto::{SeriesFilter, SeriesSort, SortDirection};
use leaf_server::metadata::age::age_of;
use leaf_server::metadata::sidecars::EntryJson;
use leaf_server::store::profiles::{CodeScope, NewProfile, Profiles, Rights};
use leaf_server::store::repository::Ceiling;
use leaf_server::store::{Db, Repository};

const NOW: i64 = 1_790_000_000_000;

// ------------------------------------------------------------------ the rule

#[test]
fn a_rating_reads_as_its_first_number() {
    assert_eq!(age_of("16+"), Some(16));
    assert_eq!(age_of("Mature 17+"), Some(17));
    assert_eq!(age_of("10 ans"), Some(10));
}

#[test]
fn the_usual_words_have_an_age() {
    assert_eq!(age_of("Everyone"), Some(0));
    assert_eq!(age_of("Teen"), Some(13));
    assert_eq!(age_of("T"), Some(13));
    assert_eq!(age_of("M"), Some(17));
    assert_eq!(age_of("Adults Only"), Some(18));
    assert_eq!(age_of("tout public"), Some(0));
    // Whatever their case: a sidecar is typed by a person.
    assert_eq!(age_of("TOUT PUBLIC"), Some(0));
    assert_eq!(age_of("mature"), Some(17));
}

#[test]
fn anything_else_has_none() {
    assert_eq!(age_of(""), None);
    assert_eq!(age_of("Rating Pending"), None);
    assert_eq!(age_of("?"), None);
}

// ------------------------------------------------------------------ the index

#[test]
fn existing_works_get_their_age_when_the_index_upgrades() {
    let dir = tempfile::tempdir().unwrap();
    let file = dir.path().join("index.sqlite");
    drop(Db::open(&file).unwrap());
    {
        // An index from before `work.age`: works with a rating and nothing beside it. Stamped
        // 29 rather than 30 so the profiles' migration runs again too, which its columns
        // already being there lets it do — the test is about 31, which then fills the ages.
        let conn = rusqlite::Connection::open(&file).unwrap();
        conn.execute_batch(
            "ALTER TABLE work DROP COLUMN age;
             INSERT INTO work (id, name, path, age_rating) VALUES
               ('douze', 'Douze', '/douze', '12+'),
               ('mot', 'Mot', '/mot', 'Teen'),
               ('rien', 'Rien', '/rien', NULL),
               ('flou', 'Flou', '/flou', 'Rating Pending');
             PRAGMA user_version = 29;",
        )
        .unwrap();
    }
    let db = Db::open(&file).unwrap();
    let ages: Vec<(String, Option<i64>)> = db
        .read(|cx| {
            cx.query("SELECT id, age FROM work ORDER BY id", [], |r| {
                Ok((r.get(0)?, r.get(1)?))
            })
        })
        .unwrap();
    assert_eq!(
        ages,
        vec![
            ("douze".to_string(), Some(12)),
            ("flou".to_string(), None),
            ("mot".to_string(), Some(13)),
            ("rien".to_string(), None),
        ]
    );
}

// ------------------------------------------------------------------ the library

/// Two universes and a loose work, scanned from the disk so the scanner is what fills the
/// ages: « Petit » at 8+, « Grand » at 16+ beside it in « Monde », « Ombre » at « Mature 17+ »
/// alone in « Nuit », and « Brut », which nobody rated.
///
/// The hidden ones carry what the visible ones do not — an author, a genre, a universe of
/// their own, an order — so that each of those leaking would show.
fn a_library() -> Server {
    let server = Server::new();
    let library = server.library();
    let write = |path: &str, text: &str| {
        let file = library.join(path);
        std::fs::create_dir_all(file.parent().unwrap()).unwrap();
        std::fs::write(file, text).unwrap();
    };
    write(
        "Monde/universe.json",
        r#"{"leaf":1,"name":"Monde","orders":[
             {"id":"lecture","name":"Lecture","steps":[{"work":"Petit"},{"work":"Grand"}]},
             {"id":"frissons","name":"Frissons","steps":[{"work":"Grand"}]}]}"#,
    );
    write(
        "Monde/Petit/work.json",
        r#"{"leaf":1,"title":"Petit","ageRating":"8+","authors":["Alice"],"genres":["Conte"]}"#,
    );
    write(
        "Monde/Grand/work.json",
        r#"{"leaf":1,"title":"Grand","ageRating":"16+","authors":["Bastien"],"genres":["Horreur"]}"#,
    );
    write("Nuit/universe.json", r#"{"leaf":1,"name":"Nuit"}"#);
    write(
        "Nuit/Ombre/work.json",
        r#"{"leaf":1,"title":"Ombre","ageRating":"Mature 17+","authors":["Bastien"]}"#,
    );
    write(
        "Brut/work.json",
        r#"{"leaf":1,"title":"Brut","authors":["Chloé"]}"#,
    );
    for (folder, work) in [
        ("Monde/Petit", "Petit"),
        ("Monde/Grand", "Grand"),
        ("Nuit/Ombre", "Ombre"),
        ("Brut", "Brut"),
    ] {
        for number in [1, 2] {
            std::fs::write(
                library.join(folder).join(format!("Tome {number}.cbz")),
                archive_bytes(Some(&EntryJson {
                    leaf: Some(1),
                    work: Some(work.into()),
                    number: Some(f64::from(number)),
                    ..Default::default()
                })),
            )
            .unwrap();
        }
    }
    server.scan();
    server
}

/// The edition of a work, by the work's name.
fn series_of(server: &Server, work: &str) -> String {
    server
        .db
        .read(|cx| {
            cx.query_one(
                "SELECT e.id FROM edition e JOIN work w ON w.id = e.work_id WHERE w.name = ?1",
                [work],
                |r| r.get::<_, String>(0),
            )
        })
        .unwrap()
        .unwrap_or_else(|| panic!("no series for {work}"))
}

/// Its first volume.
fn entry_of(server: &Server, work: &str) -> String {
    let series = series_of(server, work);
    server
        .db
        .read(|cx| {
            cx.query_one(
                "SELECT id FROM entry WHERE edition_id = ?1 ORDER BY sort_key LIMIT 1",
                [&series],
                |r| r.get::<_, String>(0),
            )
        })
        .unwrap()
        .expect("an entry")
}

fn universe_of(server: &Server, name: &str) -> String {
    server
        .db
        .read(|cx| {
            cx.query_one("SELECT id FROM universe WHERE name = ?1", [name], |r| {
                r.get::<_, String>(0)
            })
        })
        .unwrap()
        .unwrap_or_else(|| panic!("no universe {name}"))
}

/// A device linked and seated on a restricted profile of ten, and its key.
fn a_child_of_ten(server: &Server) -> String {
    let profiles = Profiles::new(Arc::clone(&server.db));
    profiles
        .set_code("2468", CodeScope::Restricted)
        .unwrap()
        .unwrap();
    let child = profiles
        .create(
            NewProfile {
                name: "Léo".into(),
                color: "corail".into(),
                restricted: true,
                max_age: Some(10),
                rights: Rights::default(),
            },
            NOW,
        )
        .unwrap()
        .unwrap();
    let key = server.linked_device("Tablette");
    let device = device_id(server, "Tablette");
    profiles.place(&device, &child.id, false).unwrap().unwrap();
    key
}

/// A device linked by the terminal, which sits on the principal.
fn the_principal(server: &Server) -> String {
    server.linked_device("Portable")
}

fn device_id(server: &Server, name: &str) -> String {
    server
        .db
        .read(|cx| {
            cx.query_one("SELECT id FROM device WHERE name = ?1", [name], |r| {
                r.get::<_, String>(0)
            })
        })
        .unwrap()
        .expect("the device")
}

async fn get(server: &Server, uri: &str, key: &str) -> (StatusCode, serde_json::Value) {
    server
        .send(request("GET", uri, key).body(Body::empty()).unwrap())
        .await
}

/// A place in each first volume, under `profile`, so `/next` has something to offer in
/// every series to that profile — progress is a profile's own, and `/next` reads only it.
fn somewhere_in_every_series(server: &Server, profile: &str) {
    server
        .db
        .write(|cx| {
            cx.execute(
                "INSERT INTO progress (profile_id, entry_id, edition_id, page, finished,
                                       times_finished, updated_at)
                 SELECT ?1, x.id, x.edition_id, 3, 0, 0, 1 FROM entry x
                  WHERE x.volume_number = 1",
                [profile],
            )
        })
        .unwrap();
}

/// The profile the child's tablet sits on.
fn the_child(server: &Server) -> String {
    server
        .db
        .read(|cx| {
            cx.query_one("SELECT id FROM profile WHERE name = 'Léo'", [], |r| {
                r.get::<_, String>(0)
            })
        })
        .unwrap()
        .expect("the child's profile")
}

fn names(list: &serde_json::Value, field: &str) -> Vec<String> {
    list.as_array()
        .unwrap_or_else(|| panic!("a list: {list}"))
        .iter()
        .map(|item| item[field].as_str().unwrap_or_default().to_string())
        .collect()
}

// ------------------------------------------------------------------ the lists

#[tokio::test]
async fn a_restricted_profile_lists_only_what_its_age_allows() {
    let server = a_library();
    let child = a_child_of_ten(&server);
    somewhere_in_every_series(&server, &the_child(&server));

    let (status, shelf) = get(&server, "/series", &child).await;
    assert_eq!(StatusCode::OK, status, "{shelf}");
    assert_eq!(names(&shelf["items"], "work"), vec!["Petit"], "{shelf}");
    assert_eq!(1, shelf["total"], "{shelf}");

    // A search inside the shelf, and the search itself, whatever was typed.
    let (_, searched) = get(&server, "/series?q=grand", &child).await;
    assert_eq!(0, searched["total"], "{searched}");
    for query in ["grand", "ombre", "tome", "bastien", "grnd"] {
        let (status, hits) = get(&server, &format!("/search?q={query}"), &child).await;
        assert_eq!(StatusCode::OK, status, "{hits}");
        let petit = series_of(&server, "Petit");
        for hit in hits.as_array().unwrap() {
            assert_eq!(petit, hit["seriesId"], "« {query} » found {hit}");
        }
        let (_, page) = get(&server, &format!("/search?q={query}&page=0"), &child).await;
        for hit in page["items"].as_array().unwrap() {
            assert_eq!(petit, hit["seriesId"], "« {query} », paged, found {hit}");
        }
    }

    // A universe appears if something in it can be seen, and counts only that.
    let (status, universes) = get(&server, "/universes", &child).await;
    assert_eq!(StatusCode::OK, status);
    assert_eq!(names(&universes, "name"), vec!["Monde"], "{universes}");
    assert_eq!(1, universes[0]["orderCount"], "{universes}");
    let monde = universe_of(&server, "Monde");
    let (status, orders) = get(&server, &format!("/universes/{monde}/orders"), &child).await;
    assert_eq!(StatusCode::OK, status, "{orders}");
    assert_eq!(names(&orders, "id"), vec!["lecture"], "{orders}");
    assert_eq!(
        names(&orders[0]["steps"], "work"),
        vec!["Petit"],
        "{orders}"
    );

    // No value only a hidden series carries.
    let (status, filters) = get(&server, "/filters", &child).await;
    assert_eq!(StatusCode::OK, status);
    assert_eq!(
        names(&filters["authors"], "value"),
        vec!["Alice"],
        "{filters}"
    );
    assert_eq!(
        names(&filters["genres"], "value"),
        vec!["Conte"],
        "{filters}"
    );
    assert_eq!(
        names(&filters["universes"], "value"),
        vec!["Monde"],
        "{filters}"
    );
    assert_eq!(1, filters["universes"][0]["count"], "{filters}");
    let (_, files) = get(&server, "/filters?over=files", &child).await;
    assert_eq!(names(&files["authors"], "value"), vec!["Alice"], "{files}");
    assert_eq!(
        names(&files["universes"], "value"),
        vec!["Monde"],
        "{files}"
    );
    assert_eq!(2, files["universes"][0]["count"], "two volumes: {files}");

    let (status, next) = get(&server, "/next", &child).await;
    assert_eq!(StatusCode::OK, status);
    assert_eq!(
        names(&next, "seriesId"),
        vec![series_of(&server, "Petit")],
        "{next}"
    );
}

#[tokio::test]
async fn a_series_above_its_age_is_not_there() {
    let server = a_library();
    let child = a_child_of_ten(&server);
    somewhere_in_every_series(&server, &the_child(&server));
    let series = series_of(&server, "Grand");
    let entry = entry_of(&server, "Grand");
    let nuit = universe_of(&server, "Nuit");

    // Each route on a hidden id, beside the same route on an id that never existed: the two
    // answers must be the same answer.
    let routes = [
        "/series/{s}",
        "/series/{s}/entries",
        "/series/{s}/chapters",
        "/series/{s}/arcs",
        "/series/{s}/cover",
        "/series/{s}/progress",
        "/entries/{e}",
        "/entries/{e}/chapters",
        "/entries/{e}/pages",
        "/entries/{e}/pages/1",
        "/entries/{e}/pages/0",
        "/entries/{e}/cover",
        "/entries/{e}/progress",
        "/universes/{u}/orders",
    ];
    for route in routes {
        let hidden = route
            .replace("{s}", &series)
            .replace("{e}", &entry)
            .replace("{u}", &nuit);
        let absent = route
            .replace("{s}", "nothing")
            .replace("{e}", "nothing")
            .replace("{u}", "nothing");
        let (status, body) = get(&server, &hidden, &child).await;
        assert_eq!(StatusCode::NOT_FOUND, status, "{hidden}: {body}");
        assert_eq!(
            get(&server, &absent, &child).await,
            (status, body),
            "{route}"
        );
    }

    // The whole file is refused to a restricted profile whatever the volume, so a hidden one
    // and one that never existed are refused alike.
    let (status, body) = get(&server, &format!("/entries/{entry}/file"), &child).await;
    assert_eq!(StatusCode::FORBIDDEN, status, "{body}");
    assert_eq!(
        get(&server, "/entries/nothing/file", &child).await,
        (status, body)
    );

    // Recording a place in it is reaching it too.
    for (uri, patch) in [
        (
            format!("/entries/{entry}/progress"),
            serde_json::json!({"page": 1}),
        ),
        (
            format!("/series/{series}/progress"),
            serde_json::json!({"finished": true}),
        ),
    ] {
        let (status, body) = server
            .send(
                request("PATCH", &uri, &child)
                    .header("Content-Type", "application/json")
                    .body(json_body(patch))
                    .unwrap(),
            )
            .await;
        assert_eq!(StatusCode::NOT_FOUND, status, "{uri}: {body}");
    }
    let finished: i64 = server
        .db
        .read(|cx| {
            cx.query_one(
                "SELECT COUNT(*) FROM progress WHERE edition_id = ?1 AND finished = 1",
                [&series],
                |r| r.get(0),
            )
        })
        .unwrap()
        .unwrap();
    assert_eq!(
        0, finished,
        "nothing was written into a series it cannot see"
    );
}

#[tokio::test]
async fn a_series_without_a_rating_is_hidden_from_a_restricted_profile() {
    let server = a_library();
    let child = a_child_of_ten(&server);
    let brut = series_of(&server, "Brut");

    let (_, shelf) = get(&server, "/series", &child).await;
    assert!(
        !names(&shelf["items"], "work").contains(&"Brut".to_string()),
        "{shelf}"
    );
    let (status, _) = get(&server, &format!("/series/{brut}"), &child).await;
    assert_eq!(StatusCode::NOT_FOUND, status);
    let (status, _) = get(&server, &format!("/series/{brut}/cover"), &child).await;
    assert_eq!(StatusCode::NOT_FOUND, status);
}

#[tokio::test]
async fn the_principal_sees_everything_as_before() {
    let server = a_library();
    a_child_of_ten(&server);
    let principal = the_principal(&server);
    somewhere_in_every_series(&server, "default");

    let (_, shelf) = get(&server, "/series", &principal).await;
    assert_eq!(4, shelf["total"], "{shelf}");
    let (_, universes) = get(&server, "/universes", &principal).await;
    assert_eq!(names(&universes, "name"), vec!["Monde", "Nuit"]);
    assert_eq!(2, universes[0]["orderCount"]);
    let (_, filters) = get(&server, "/filters", &principal).await;
    assert!(names(&filters["authors"], "value").contains(&"Bastien".to_string()));
    let (_, next) = get(&server, "/next", &principal).await;
    assert_eq!(4, next.as_array().unwrap().len(), "{next}");

    let series = series_of(&server, "Grand");
    let entry = entry_of(&server, "Grand");
    for uri in [
        format!("/series/{series}"),
        format!("/series/{series}/entries"),
        format!("/series/{series}/chapters"),
        format!("/series/{series}/arcs"),
        format!("/series/{series}/cover"),
        format!("/series/{series}/progress"),
        format!("/entries/{entry}"),
        format!("/entries/{entry}/chapters"),
        format!("/entries/{entry}/pages"),
        format!("/entries/{entry}/pages/0"),
        format!("/entries/{entry}/cover"),
        format!("/entries/{entry}/file"),
        format!("/entries/{entry}/progress"),
        format!("/universes/{}/orders", universe_of(&server, "Nuit")),
    ] {
        let (status, body) = get(&server, &uri, &principal).await;
        assert_eq!(StatusCode::OK, status, "{uri}: {body}");
    }
    // An old key and an open library are the principal too.
    let (_, shelf) = get(&server, "/series", common::READ_ONLY).await;
    assert_eq!(4, shelf["total"], "{shelf}");
}

/// The ceiling is a column in the query, never a second question and never a pass over the
/// rows afterwards.
#[tokio::test]
async fn the_ceiling_adds_no_question_to_the_shelf() {
    let server = a_library();
    let child = a_child_of_ten(&server);
    let principal = the_principal(&server);
    // Once each first: a device's first request stamps when it was seen.
    get(&server, "/series", &child).await;
    get(&server, "/series", &principal).await;

    let before = server.db.statements();
    get(&server, "/series", &principal).await;
    let open = server.db.statements() - before;
    let before = server.db.statements();
    get(&server, "/series", &child).await;
    let restricted = server.db.statements() - before;
    assert_eq!(
        open, restricted,
        "{restricted} questions under a ceiling, {open} without"
    );

    let repository = Repository::new(&server.db);
    let cost = |ceiling: Ceiling| {
        let before = server.db.statements();
        repository
            .series_ordered(
                &SeriesFilter::default(),
                SeriesSort::Name,
                SortDirection::Ascending,
                0,
                0,
                "default",
                &ceiling,
            )
            .unwrap();
        repository
            .count_series(&SeriesFilter::default(), "default", &ceiling)
            .unwrap();
        server.db.statements() - before
    };
    assert_eq!(cost(Ceiling(None)), cost(Ceiling(Some(10))));
}

/// One question for each thing a hidden series could be reached through, whether it is
/// visible, hidden or not there at all: whether it exists is read in the same statement as
/// what it holds.
#[test]
fn reaching_a_series_costs_one_question_whatever_the_answer() {
    let server = a_library();
    let repository = Repository::new(&server.db);
    let under_ten = Ceiling(Some(10));
    for (series, entry) in [
        (series_of(&server, "Petit"), entry_of(&server, "Petit")),
        (series_of(&server, "Grand"), entry_of(&server, "Grand")),
        ("nothing".to_string(), "nothing".to_string()),
    ] {
        let cost = |f: &dyn Fn()| {
            let before = server.db.statements();
            f();
            server.db.statements() - before
        };
        assert_eq!(1, cost(&|| drop(repository.entries(&series, &under_ten))));
        assert_eq!(
            1,
            cost(&|| drop(repository.chapters_of_edition(&series, &under_ten)))
        );
        assert_eq!(1, cost(&|| drop(repository.arcs(&series, &under_ten))));
        assert_eq!(1, cost(&|| drop(repository.entry(&entry, &under_ten))));
        assert_eq!(
            1,
            cost(&|| drop(repository.chapters_of_entry(&entry, &under_ten)))
        );
        assert_eq!(1, cost(&|| drop(repository.pages(&entry, &under_ten))));
        assert_eq!(1, cost(&|| drop(repository.entry_path(&entry, &under_ten))));
    }
    assert!(repository
        .entries(&series_of(&server, "Grand"), &under_ten)
        .unwrap()
        .is_none());
    assert!(repository
        .entries(&series_of(&server, "Petit"), &under_ten)
        .unwrap()
        .is_some());
}

/// The rating is the files', so changing it is a `PATCH` like any other — and the age moves
/// with it, through the same rescan that reads the sidecar back.
#[tokio::test]
async fn rating_a_series_through_the_api_moves_it_across_the_ceiling() {
    let server = a_library();
    let child = a_child_of_ten(&server);
    let brut = series_of(&server, "Brut");
    let (status, _) = get(&server, &format!("/series/{brut}"), &child).await;
    assert_eq!(
        StatusCode::NOT_FOUND,
        status,
        "unrated, it is for nobody restricted"
    );

    let (status, body) = server
        .send(
            request("PATCH", &format!("/series/{brut}"), IMPORTER)
                .header("Content-Type", "application/json")
                .body(json_body(serde_json::json!({"ageRating": "6+"})))
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::OK, status, "{body}");
    let (status, _) = get(&server, &format!("/series/{brut}"), &child).await;
    assert_eq!(StatusCode::OK, status, "rated 6+, it is for ten");

    let grand = series_of(&server, "Grand");
    let (status, body) = server
        .send(
            request("PATCH", &format!("/series/{grand}"), IMPORTER)
                .header("Content-Type", "application/json")
                .body(json_body(
                    serde_json::json!({"ageRating": "Rating Pending"}),
                ))
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::OK, status, "{body}");
    let age: Option<i64> = server
        .db
        .read(|cx| {
            cx.query_one(
                "SELECT w.age FROM work w JOIN edition e ON e.work_id = w.id WHERE e.id = ?1",
                [&grand],
                |r| r.get(0),
            )
        })
        .unwrap()
        .unwrap();
    assert_eq!(None, age, "a rating that names no age leaves none");
}

/// The scan's findings name folders and series. A restricted profile is told whether a scan
/// runs, never what it found.
#[tokio::test]
async fn a_restricted_profile_is_not_told_what_a_scan_found() {
    let server = a_library();
    let child = a_child_of_ten(&server);
    let principal = the_principal(&server);
    let state = server.state();
    let started = state.runner.start("Scan", || {
        Ok(leaf_server::scan::report::ScanReport {
            missing_required: vec!["Nuit/Ombre : ageRating".into()],
            ..Default::default()
        })
    });
    assert!(started);
    while state.runner.status().state == "RUNNING" {
        tokio::time::sleep(std::time::Duration::from_millis(5)).await;
    }

    let (status, told) = server
        .send_to(
            state.clone(),
            request("GET", "/scan", &child).body(Body::empty()).unwrap(),
        )
        .await;
    assert_eq!(StatusCode::OK, status);
    assert_eq!("DONE", told["state"], "{told}");
    assert!(told.get("report").is_none(), "{told}");

    let (_, told) = server
        .send_to(
            state,
            request("GET", "/scan", &principal)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert!(told["report"].is_object(), "{told}");
}

/// A restricted profile deleted while its device sits on it leaves the device on the
/// principal, only to hold its key until somebody chooses. Until then it must not read as the
/// principal: the tablet of a child would open the whole library because a parent removed
/// the child's profile. Every route refuses it but `/health`, which counts nothing for it.
#[tokio::test]
async fn a_device_held_after_its_restricted_profile_is_deleted_sees_nothing_rated_above_nought() {
    let server = a_library();
    let child = a_child_of_ten(&server);
    let profiles = Profiles::new(Arc::clone(&server.db));
    let seat = profiles.seat(&device_id(&server, "Tablette")).unwrap();
    profiles.delete(&seat.profile_id).unwrap().unwrap();

    let (status, shelf) = get(&server, "/series", &child).await;
    assert_eq!(StatusCode::CONFLICT, status, "{shelf}");
    let (status, health) = get(&server, "/health", &child).await;
    assert_eq!(StatusCode::OK, status, "{health}");
    assert_eq!(0, health["library"], "{health}");
}

/// Another branch numbers a migration 30 of its own, and the numbers move when the two meet.
/// The backfill finds its migration by the statement it holds; this holds the constant that
/// says where that is to the truth, so a renumbering is a failure here and not a library
/// whose every age stayed empty.
#[test]
fn the_age_is_filled_after_the_migration_that_adds_it_and_no_other() {
    use leaf_server::store::schema::{adds_work_age, ADD_WORK_AGE, MIGRATIONS, WORK_AGE};
    let at = usize::try_from(WORK_AGE - 1).unwrap();
    assert!(
        MIGRATIONS[at]
            .iter()
            .any(|s| s.contains("ADD COLUMN age INTEGER")),
        "migration {WORK_AGE} no longer adds work.age: {:?}",
        MIGRATIONS[at]
    );
    let adding: Vec<i32> = (1..=MIGRATIONS.len() as i32)
        .filter(|n| adds_work_age(*n))
        .collect();
    assert_eq!(adding, vec![WORK_AGE], "{ADD_WORK_AGE}");
}

/// « N séries pour vous »: the count a restricted profile's connection line shows is its own
/// shelf, and asking it costs what it costs the principal.
#[tokio::test]
async fn health_counts_the_series_the_caller_may_see() {
    let server = a_library();
    let child = a_child_of_ten(&server);
    let principal = the_principal(&server);
    // Once each first: a device's first request stamps when it was seen.
    get(&server, "/health", &child).await;
    get(&server, "/health", &principal).await;

    let before = server.db.statements();
    let (_, told) = get(&server, "/health", &child).await;
    let restricted = server.db.statements() - before;
    assert_eq!(1, told["library"], "{told}");

    let before = server.db.statements();
    let (_, told) = get(&server, "/health", &principal).await;
    let open = server.db.statements() - before;
    assert_eq!(4, told["library"], "{told}");
    assert_eq!(open, restricted);

    // A stranger is told what it always was.
    let (_, told) = server
        .send(
            axum::http::Request::builder()
                .uri("/health")
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(4, told["library"], "{told}");
}

/// What the readers that gained a ceiling cost under one: the same statements as without,
/// whatever it hides.
#[test]
fn reading_under_a_ceiling_costs_what_it_costs_without() {
    use leaf_server::api::progress::Progress;
    let server = a_library();
    let child = {
        a_child_of_ten(&server);
        the_child(&server)
    };
    somewhere_in_every_series(&server, &child);
    let repository = Repository::new(&server.db);
    let progress = Progress::new(&server.db);
    let under_ten = Ceiling(Some(10));
    let cost = |f: &dyn Fn()| {
        let before = server.db.statements();
        f();
        server.db.statements() - before
    };
    let petit = series_of(&server, "Petit");
    let opened = entry_of(&server, "Petit");
    let monde = universe_of(&server, "Monde");

    assert_eq!(1, cost(&|| drop(repository.universes(&under_ten))));
    assert_eq!(
        2,
        cost(&|| drop(repository.orders_of_universe(&monde, &under_ten)))
    );
    // Hidden or absent: the first statement says so and the steps are never asked for.
    let nuit = universe_of(&server, "Nuit");
    assert_eq!(
        1,
        cost(&|| drop(repository.orders_of_universe(&nuit, &under_ten)))
    );

    // The place, then its chapters and the row itself.
    assert_eq!(
        3,
        cost(&|| drop(progress.of_entry(&opened, &child, &under_ten)))
    );
    assert_eq!(
        1,
        cost(&|| drop(progress.of_entry("nothing", &child, &under_ten)))
    );
    assert_eq!(
        3,
        cost(&|| drop(progress.of_series(&petit, &child, &under_ten)))
    );
    assert_eq!(
        1,
        cost(&|| drop(progress.of_series("nothing", &child, &under_ten)))
    );

    let next = cost(&|| drop(progress.up_next(20, &child, &under_ten)));
    let unlimited = cost(&|| drop(progress.up_next(20, &child, &Ceiling::NONE)));
    assert_eq!(
        unlimited, next,
        "{next} under a ceiling, {unlimited} without"
    );
    // Two to pick, eight for the series' rows and what they carry, one for the entries, two
    // for the places and their chapters: the shape of the answer, never one per line.
    assert_eq!(13, next, "/next took {next} questions");
}

// ------------------------------------------- a direction is a profile's own choice

async fn chooses_direction(
    server: &Server,
    series: &str,
    key: &str,
    word: serde_json::Value,
) -> (StatusCode, serde_json::Value) {
    server
        .send(
            request("PUT", &format!("/series/{series}/reading-direction"), key)
                .header("Content-Type", "application/json")
                .body(json_body(serde_json::json!({ "readingDirection": word })))
                .unwrap(),
        )
        .await
}

/// The way a series is read used to be one row per series, so a child turning a book the
/// other way turned it for everyone, and a reader of the family laptop found the page they
/// had left turned back by somebody else. Each profile has its own, and none touches another's.
#[tokio::test]
async fn a_direction_chosen_by_one_profile_is_not_seen_by_another() {
    let server = a_library();
    let child = a_child_of_ten(&server);
    let principal = the_principal(&server);
    let petit = series_of(&server, "Petit");

    let (status, body) =
        chooses_direction(&server, &petit, &child, serde_json::json!("VERTICAL")).await;
    assert_eq!(StatusCode::OK, status, "{body}");
    assert_eq!(
        serde_json::json!("VERTICAL"),
        body["readingDirectionChosen"]
    );

    // The principal sees what the files say, as though nobody had chosen.
    let (_, seen) = get(&server, &format!("/series/{petit}"), &principal).await;
    assert!(seen["readingDirectionChosen"].is_null(), "{seen}");

    // And choosing its own leaves the child's where it was.
    let (status, _) = chooses_direction(
        &server,
        &petit,
        &principal,
        serde_json::json!("RIGHT_TO_LEFT"),
    )
    .await;
    assert_eq!(StatusCode::OK, status);
    let (_, mine) = get(&server, &format!("/series/{petit}"), &principal).await;
    let (_, theirs) = get(&server, &format!("/series/{petit}"), &child).await;
    assert_eq!(
        serde_json::json!("RIGHT_TO_LEFT"),
        mine["readingDirectionChosen"]
    );
    assert_eq!(
        serde_json::json!("VERTICAL"),
        theirs["readingDirectionChosen"]
    );

    // The shelf says the same as the page of the series, per caller.
    let (_, shelf) = get(&server, "/series", &child).await;
    let on_shelf = shelf["items"]
        .as_array()
        .unwrap()
        .iter()
        .find(|s| s["id"] == serde_json::json!(petit))
        .expect("Petit on the child's shelf");
    assert_eq!(
        serde_json::json!("VERTICAL"),
        on_shelf["readingDirectionChosen"]
    );

    // Going back to the default removes the child's row and no other.
    let (status, _) = chooses_direction(&server, &petit, &child, serde_json::Value::Null).await;
    assert_eq!(StatusCode::OK, status);
    let rows: i64 = server
        .db
        .read(|cx| {
            cx.query_one("SELECT COUNT(*) FROM profile_reading_choice", [], |r| {
                r.get(0)
            })
        })
        .unwrap()
        .unwrap();
    assert_eq!(1, rows, "the principal's choice is still there");
}

/// A series above the profile's age is not there for it, to be read or to be given a way to
/// be read: the same 404 as for one that does not exist, and no row written.
#[tokio::test]
async fn a_series_above_the_age_cannot_be_given_a_direction() {
    let server = a_library();
    let child = a_child_of_ten(&server);
    let grand = series_of(&server, "Grand");

    let (status, _) =
        chooses_direction(&server, &grand, &child, serde_json::json!("VERTICAL")).await;
    assert_eq!(StatusCode::NOT_FOUND, status);
    let rows: i64 = server
        .db
        .read(|cx| {
            cx.query_one("SELECT COUNT(*) FROM profile_reading_choice", [], |r| {
                r.get(0)
            })
        })
        .unwrap()
        .unwrap();
    assert_eq!(0, rows);
}

/// What a profile chose goes with the profile: nothing is left pointing at a reader who is
/// no longer there, and a new profile never inherits a stranger's habits.
#[tokio::test]
async fn deleting_a_profile_takes_what_it_chose_with_it() {
    let server = a_library();
    let child = a_child_of_ten(&server);
    let petit = series_of(&server, "Petit");
    let (status, _) =
        chooses_direction(&server, &petit, &child, serde_json::json!("VERTICAL")).await;
    assert_eq!(StatusCode::OK, status);

    Profiles::new(Arc::clone(&server.db))
        .delete(&the_child(&server))
        .unwrap()
        .unwrap();

    let rows: i64 = server
        .db
        .read(|cx| {
            cx.query_one("SELECT COUNT(*) FROM profile_reading_choice", [], |r| {
                r.get(0)
            })
        })
        .unwrap()
        .unwrap();
    assert_eq!(0, rows);
}
