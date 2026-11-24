//! The routes that change something, answered through the real router.
//!
//! What these check is the guard and the shape: a write route without the import right must
//! be refused, an upload must reach the disk without being held whole in memory, and a
//! broken transfer must be told exactly where to resume.

use std::path::Path;

use axum::body::Body;
use axum::http::{Request, StatusCode};
use leaf_server::api::routes::{can_be_aimed_at, AppState};
use leaf_server::metadata::sidecars::{self, ChapterJson, EntryJson};

mod common;
use common::{a_volume, archive_bytes, json_body, request, Server, ERASER, IMPORTER, READ_ONLY};

// ------------------------------------------------------------------- erasing

/// Erasing is not importing. A key that fills a shelf has no business emptying it unless
/// somebody said so, and every key written before this right existed carries `read,import`.
#[tokio::test]
async fn the_import_right_does_not_carry_the_right_to_erase() {
    let server = Server::new();
    a_volume(&server);
    let series = server.series();
    let entry = server.entry();

    for uri in [format!("/series/{series}"), format!("/entries/{entry}")] {
        let (status, _) = server
            .send(
                request("DELETE", &uri, IMPORTER)
                    .body(Body::empty())
                    .unwrap(),
            )
            .await;
        assert_eq!(StatusCode::FORBIDDEN, status, "{uri}");
    }

    // And the file is still there, which is the assertion that matters.
    assert!(server.library().join("Bleach/Tome 1.cbz").exists());
}

/// The file goes and the folder stays. `work.json`, `edition.json` and a cover dropped
/// beside a volume are files somebody wrote, not files somebody asked to erase — an empty
/// folder is visible and can be swept up, a lost `work.json` is not.
#[tokio::test]
async fn erasing_a_volume_takes_the_file_and_leaves_what_was_not_asked_for() {
    let server = Server::new();
    a_volume(&server);
    let folder = server.library().join("Bleach");
    std::fs::write(folder.join("work.json"), r#"{"leaf":1,"title":"Bleach"}"#).unwrap();
    std::fs::write(folder.join("Tome 1.jpg"), common::jpeg()).unwrap();
    let entry = server.entry();

    let (status, body) = server
        .send(
            request("DELETE", &format!("/entries/{entry}"), ERASER)
                .body(Body::empty())
                .unwrap(),
        )
        .await;

    assert_eq!(StatusCode::OK, status);
    assert_eq!(serde_json::json!(1), body["files"]);
    assert!(body["bytes"].as_i64().unwrap_or(0) > 0);
    assert!(body.get("refused").is_none(), "{body}");

    assert!(!folder.join("Tome 1.cbz").exists(), "the archive went");
    assert!(folder.join("work.json").exists(), "and nothing else did");
    assert!(folder.join("Tome 1.jpg").exists());
    assert!(folder.exists());
}

/// The numbering does not move, so a hole stays a hole.
///
/// `number` identifies and `position` orders: erasing the second of three leaves the third
/// as the third, and leaves the second among the missing for as long as the collection has
/// a hole in it. That is not the interface being careful — it is the model, and the reason
/// the confirmation can promise it.
#[tokio::test]
async fn erasing_a_volume_in_the_middle_leaves_a_gap_the_index_reports() {
    let server = Server::new();
    let folder = server.library().join("Bleach");
    std::fs::create_dir_all(&folder).unwrap();
    for number in 1..=3 {
        std::fs::write(
            folder.join(format!("Tome {number}.cbz")),
            archive_bytes(Some(&EntryJson {
                leaf: Some(1),
                work: Some("Bleach".into()),
                number: Some(f64::from(number)),
                ..Default::default()
            })),
        )
        .unwrap();
    }
    server.scan();
    let series = server.series();

    let second: String = server
        .state()
        .db
        .read(|cx| {
            cx.query_one("SELECT id FROM entry WHERE volume_number = 2.0", [], |r| {
                r.get(0)
            })
        })
        .unwrap()
        .expect("the second volume");

    let (status, _) = server
        .send(
            request("DELETE", &format!("/entries/{second}"), ERASER)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::OK, status);

    let (status, body) = server
        .send(
            request("GET", &format!("/series/{series}"), READ_ONLY)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::OK, status);
    assert_eq!(serde_json::json!([2.0]), body["missingVolumes"], "{body}");
    assert_eq!(serde_json::json!(2), body["entryCount"]);
}

/// Asking twice is a 404 the second time: the row is gone with the file, so the id names
/// nothing. A deletion that answered "done" about something it never saw would be a route
/// nobody could trust to have acted.
#[tokio::test]
async fn erasing_something_that_is_not_there_is_a_404() {
    let server = Server::new();
    a_volume(&server);
    let entry = server.entry();
    let uri = format!("/entries/{entry}");

    let (first, _) = server
        .send(request("DELETE", &uri, ERASER).body(Body::empty()).unwrap())
        .await;
    assert_eq!(StatusCode::OK, first);

    let (again, _) = server
        .send(request("DELETE", &uri, ERASER).body(Body::empty()).unwrap())
        .await;
    assert_eq!(StatusCode::NOT_FOUND, again);

    let (nowhere, _) = server
        .send(
            request("DELETE", "/series/nothing-like-it", ERASER)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::NOT_FOUND, nowhere);
}

/// A whole edition, and the work above it when that was its last one — a work with no
/// edition is a story the shelf cannot show.
#[tokio::test]
async fn erasing_a_series_takes_every_file_and_the_work_with_it() {
    let server = Server::new();
    a_volume(&server);
    let series = server.series();

    let (status, body) = server
        .send(
            request("DELETE", &format!("/series/{series}"), ERASER)
                .body(Body::empty())
                .unwrap(),
        )
        .await;

    assert_eq!(StatusCode::OK, status);
    assert_eq!(serde_json::json!(1), body["files"]);
    assert!(!server.library().join("Bleach/Tome 1.cbz").exists());

    // Nothing is left to draw, and the folder is still on the disk.
    let (status, _) = server
        .send(
            request("GET", &format!("/series/{series}"), READ_ONLY)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::NOT_FOUND, status);
    assert!(server.library().join("Bleach").exists());
}

/// A universe holds works and nothing else: with its last work gone it is a name that opens
/// onto nothing, which the list drew all the same. Its folder and `universe.json` stay on the
/// disk, as the work's do.
#[tokio::test]
async fn erasing_the_last_series_of_a_universe_takes_the_universe_off_the_list() {
    let server = Server::new();
    let folder = server.library().join("Mangas/Bleach");
    std::fs::create_dir_all(&folder).unwrap();
    std::fs::write(
        server.library().join("Mangas/universe.json"),
        r#"{"leaf":1,"name":"Mangas"}"#,
    )
    .unwrap();
    std::fs::write(
        folder.join("Tome 1.cbz"),
        archive_bytes(Some(&EntryJson {
            leaf: Some(1),
            work: Some("Bleach".into()),
            number: Some(1.0),
            ..Default::default()
        })),
    )
    .unwrap();
    server.scan();
    let series = server.series();
    let listed = || async {
        let (_, body) = server
            .send(
                request("GET", "/universes", READ_ONLY)
                    .body(Body::empty())
                    .unwrap(),
            )
            .await;
        body.as_array().map_or(0, Vec::len)
    };
    assert_eq!(1, listed().await, "the universe is there before");

    server
        .send(
            request("DELETE", &format!("/series/{series}"), ERASER)
                .body(Body::empty())
                .unwrap(),
        )
        .await;

    assert_eq!(0, listed().await, "and gone with its last work");
    assert!(server.library().join("Mangas/universe.json").exists());
}

/// The disk first and the row second. A file that will not go keeps its row, because the
/// other order leaves a library claiming to have lost something it still holds — and a
/// rescan would put it back, which is a deletion that undoes itself.
#[tokio::test]
async fn a_file_that_will_not_go_keeps_its_row_and_is_named() {
    let server = Server::new();
    a_volume(&server);
    let series = server.series();
    let folder = server.library().join("Bleach");

    common::read_only(&folder);
    let (status, body) = server
        .send(
            request("DELETE", &format!("/series/{series}"), ERASER)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    common::writable(&folder);

    assert_eq!(StatusCode::OK, status);
    assert_eq!(serde_json::json!(0), body["files"]);
    assert_eq!(
        serde_json::json!(["Tome 1.cbz"]),
        body["refused"],
        "the caller is told which file is still there"
    );
    assert!(folder.join("Tome 1.cbz").exists());

    // And the series is still a series: an edition holding nothing would be drawn by the
    // shelf and opened by nothing.
    let (status, _) = server
        .send(
            request("GET", &format!("/series/{series}"), READ_ONLY)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::OK, status);
}

/// An id that resolves outside the library is the index disagreeing with the disk, and
/// unlinking on that basis is how a server deletes somebody's home directory.
#[tokio::test]
async fn a_file_outside_the_library_is_refused_rather_than_unlinked() {
    let server = Server::new();
    a_volume(&server);
    let entry = server.entry();

    let elsewhere = server
        .library()
        .parent()
        .unwrap()
        .join("not-the-library.cbz");
    std::fs::write(&elsewhere, b"not yours").unwrap();
    server
        .state()
        .db
        .write(|cx| {
            cx.execute(
                "UPDATE entry SET file = ?1 WHERE id = ?2",
                rusqlite::params![elsewhere.to_string_lossy().to_string(), entry],
            )?;
            Ok(())
        })
        .unwrap();

    let (status, body) = server
        .send(
            request("DELETE", &format!("/entries/{entry}"), ERASER)
                .body(Body::empty())
                .unwrap(),
        )
        .await;

    assert_eq!(StatusCode::OK, status);
    assert_eq!(serde_json::json!(0), body["files"]);
    assert!(
        elsewhere.exists(),
        "a file outside the roots is not this server's to erase"
    );
}

// -------------------------------------------------------------------- guards

#[tokio::test]
async fn a_read_only_key_cannot_reach_a_write_route() {
    let server = Server::new();
    a_volume(&server);
    let series = server.series();

    for (method, uri, body) in [
        (
            "PATCH",
            format!("/series/{series}"),
            json_body(serde_json::json!({})),
        ),
        (
            "PATCH",
            format!("/series/{series}/arcs"),
            json_body(serde_json::json!([])),
        ),
        ("GET", "/drop".to_string(), Body::empty()),
        (
            "POST",
            "/works/any/move".to_string(),
            json_body(serde_json::json!({})),
        ),
        (
            "POST",
            "/import".to_string(),
            json_body(serde_json::json!({"root": "x", "files": []})),
        ),
        (
            "POST",
            "/cleanup".to_string(),
            json_body(serde_json::json!({"root": "x", "files": []})),
        ),
        ("DELETE", "/import/imp_1".to_string(), Body::empty()),
        ("POST", format!("/series/{series}/rescan"), Body::empty()),
        ("POST", "/entries/any/rescan".to_string(), Body::empty()),
    ] {
        let (status, _) = server
            .send(
                request(method, &uri, READ_ONLY)
                    .header("content-type", "application/json")
                    .body(body)
                    .unwrap(),
            )
            .await;
        assert_eq!(
            StatusCode::FORBIDDEN,
            status,
            "{method} {uri} must ask for the import right"
        );
    }
}

// ------------------------------------------------------------------- rescanning

/// Waits for the runner to be done with what it was given: a rescan answers at once and
/// reads behind, so what it found is only there afterwards.
async fn until_idle(state: &AppState) {
    while state.runner.status().state == "RUNNING" {
        tokio::time::sleep(std::time::Duration::from_millis(5)).await;
    }
}

/// A file changed under the server is read back by asking for that one work, and the answer
/// is the scan that began rather than the series: it reads behind, as `POST /scan` does.
#[tokio::test]
async fn a_series_rescanned_by_itself_reads_a_sidecar_somebody_wrote_by_hand() {
    let server = Server::new();
    a_volume(&server);
    let series = server.series();
    let state = server.state();
    std::fs::write(
        server.library().join("Bleach/work.json"),
        r#"{"leaf":1,"title":"Bleach","author":"Tite Kubo"}"#,
    )
    .unwrap();

    let (status, told) = server
        .send_to(
            state.clone(),
            request("POST", &format!("/series/{series}/rescan"), IMPORTER)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::ACCEPTED, status, "{told}");
    assert_eq!("RUNNING", told["state"], "{told}");
    until_idle(&state).await;

    // Read again by what it now is: a sidecar that did not exist before is stamped with an
    // identity, and the series has the one it carries from then on.
    let series = server.series();
    let (status, body) = server
        .send_to(
            state,
            request("GET", &format!("/series/{series}"), IMPORTER)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::OK, status);
    assert_eq!("Tite Kubo", body["author"], "{body}");
}

/// A volume is read again by reading its work, the smallest thing the scanner is aimed at.
#[tokio::test]
async fn a_volume_rescanned_by_itself_reads_the_work_it_belongs_to() {
    let server = Server::new();
    a_volume(&server);
    let entry = server.entry();
    let state = server.state();
    std::fs::write(
        server.library().join("Bleach/work.json"),
        r#"{"leaf":1,"title":"Bleach","author":"Tite Kubo"}"#,
    )
    .unwrap();

    let (status, told) = server
        .send_to(
            state.clone(),
            request("POST", &format!("/entries/{entry}/rescan"), IMPORTER)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::ACCEPTED, status, "{told}");
    until_idle(&state).await;

    let series = server.series();
    let (_, body) = server
        .send_to(
            state,
            request("GET", &format!("/series/{series}"), IMPORTER)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!("Tite Kubo", body["author"], "{body}");
}

/// Something that is not there is a 404, and a rescan that meets a scan already running is
/// told so with a 409 rather than queued behind it: both read the same disk.
#[tokio::test]
async fn rescanning_what_is_not_there_is_a_404_and_during_a_scan_a_409() {
    let server = Server::new();
    a_volume(&server);
    let series = server.series();
    let state = server.state();

    for uri in ["/series/nope/rescan", "/entries/nope/rescan"] {
        let (status, _) = server
            .send_to(
                state.clone(),
                request("POST", uri, IMPORTER).body(Body::empty()).unwrap(),
            )
            .await;
        assert_eq!(StatusCode::NOT_FOUND, status, "{uri}");
    }

    let started = state.runner.start("Scan", a_scan_that_lasts);
    assert!(started);
    let (status, _) = server
        .send_to(
            state.clone(),
            request("POST", &format!("/series/{series}/rescan"), IMPORTER)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::CONFLICT, status);
    until_idle(&state).await;
}

/// A scan long enough to be caught running. A plain function, not a closure inside the
/// test: the runner calls it on its own thread, where blocking is what a scan does, and a
/// sleep written inside an async body reads as one stalling the runtime.
fn a_scan_that_lasts() -> anyhow::Result<leaf_server::scan::report::ScanReport> {
    std::thread::sleep(std::time::Duration::from_millis(300));
    Ok(leaf_server::scan::report::ScanReport::default())
}

// ------------------------------------------------------------------- records

#[tokio::test]
async fn patching_a_series_writes_the_sidecar_and_answers_with_what_it_now_is() {
    let server = Server::new();
    a_volume(&server);
    let series = server.series();

    let (status, body) = server
        .send(
            request("PATCH", &format!("/series/{series}"), IMPORTER)
                .header("content-type", "application/json")
                .body(json_body(serde_json::json!({
                    "title": "BLEACH",
                    "author": "Tite Kubo",
                })))
                .unwrap(),
        )
        .await;

    assert_eq!(StatusCode::OK, status);
    // The rescan the route runs is what puts the edit back in the answer.
    assert_eq!("Tite Kubo", body["author"]);
    // The title changed, and the folder is named after it.
    assert!(server.library().join("BLEACH/work.json").exists());
}

/// A field can be taken away, and not only changed: `null` empties it, an absent field leaves
/// it alone. Read as the same thing, a wrong ISBN stayed on its volume until somebody edited
/// the archive by hand.
#[tokio::test]
async fn null_takes_a_field_away_and_an_absent_one_leaves_it() {
    let server = Server::new();
    a_volume(&server);
    let series = server.series();
    let uri = format!("/series/{series}");
    let patch = |body: serde_json::Value| {
        request("PATCH", &uri, IMPORTER)
            .header("content-type", "application/json")
            .body(json_body(body))
            .unwrap()
    };

    let (status, body) = server
        .send(patch(
            serde_json::json!({"ageRating": "16+", "summary": "Un carnet."}),
        ))
        .await;
    assert_eq!(StatusCode::OK, status, "{body}");
    assert_eq!(body["ageRating"], "16+");

    // Only the age rating is said, and it is said to be nothing.
    let (status, body) = server
        .send(patch(serde_json::json!({"ageRating": null})))
        .await;
    assert_eq!(StatusCode::OK, status, "{body}");
    assert!(body.get("ageRating").is_none_or(|v| v.is_null()), "{body}");
    assert_eq!(
        body["summary"], "Un carnet.",
        "what was not named stays: {body}"
    );

    // This edition is the work's folder itself: it has no description of its own to keep.
    let (status, _) = server
        .send(patch(serde_json::json!({"editionSummary": "x"})))
        .await;
    assert_eq!(StatusCode::BAD_REQUEST, status);
}

/// What the edition says of itself is a field of its own, and `null` hands the description
/// back to the work's rather than leaving the edition saying nothing.
#[tokio::test]
async fn an_editions_own_description_is_written_and_given_back_to_the_work() {
    let server = Server::new();
    let folder = server.library().join("Mangas/Bleach/Perfect");
    std::fs::create_dir_all(&folder).unwrap();
    std::fs::write(
        server.library().join("Mangas/universe.json"),
        r#"{"leaf":1,"name":"Mangas"}"#,
    )
    .unwrap();
    std::fs::write(
        server.library().join("Mangas/Bleach/work.json"),
        r#"{"leaf":1,"title":"Bleach"}"#,
    )
    .unwrap();
    std::fs::write(
        folder.join("edition.json"),
        r#"{"leaf":1,"name":"Perfect"}"#,
    )
    .unwrap();
    std::fs::write(
        folder.join("Tome 1.cbz"),
        archive_bytes(Some(&EntryJson {
            leaf: Some(1),
            work: Some("Bleach".into()),
            number: Some(1.0),
            ..Default::default()
        })),
    )
    .unwrap();
    server.scan();
    let series = server.series();
    let uri = format!("/series/{series}");
    let patch = |body: serde_json::Value| {
        request("PATCH", &uri, IMPORTER)
            .header("content-type", "application/json")
            .body(json_body(body))
            .unwrap()
    };

    let (_, body) = server
        .send(patch(serde_json::json!({"summary": "L'histoire."})))
        .await;
    assert_eq!(body["summary"], "L'histoire.");

    let (status, body) = server
        .send(patch(
            serde_json::json!({"editionSummary": "Réimpression soignée."}),
        ))
        .await;
    assert_eq!(StatusCode::OK, status, "{body}");
    assert_eq!(body["summary"], "Réimpression soignée.");
    assert_eq!(body["ownSummary"], "Réimpression soignée.");

    let (_, body) = server
        .send(patch(serde_json::json!({"editionSummary": null})))
        .await;
    assert_eq!(body["summary"], "L'histoire.", "{body}");
    assert!(body.get("ownSummary").is_none_or(|v| v.is_null()), "{body}");
}

/// Two files of an edition holding one number are one volume twice. Refused before the
/// archive is touched, as a 400 — a 409 here would be read as « choose a profile ».
#[tokio::test]
async fn a_number_another_volume_holds_is_refused_and_an_isbn_can_be_taken_away() {
    let server = Server::new();
    let folder = server.library().join("Bleach");
    std::fs::create_dir_all(&folder).unwrap();
    for number in 1..=2 {
        std::fs::write(
            folder.join(format!("Tome {number}.cbz")),
            archive_bytes(Some(&EntryJson {
                leaf: Some(1),
                work: Some("Bleach".into()),
                number: Some(f64::from(number)),
                isbn: Some("9782205059922".into()),
                ..Default::default()
            })),
        )
        .unwrap();
    }
    server.scan();
    let second: String = server
        .state()
        .db
        .read(|cx| {
            cx.query_one("SELECT id FROM entry WHERE volume_number = 2.0", [], |r| {
                r.get(0)
            })
        })
        .unwrap()
        .expect("the second volume");
    let uri = format!("/entries/{second}");
    let patch = |body: serde_json::Value| {
        request("PATCH", &uri, IMPORTER)
            .header("content-type", "application/json")
            .body(json_body(body))
            .unwrap()
    };

    let (status, body) = server.send(patch(serde_json::json!({"number": 1}))).await;
    assert_eq!(StatusCode::BAD_REQUEST, status, "{body}");

    let (status, body) = server.send(patch(serde_json::json!({"isbn": null}))).await;
    assert_eq!(StatusCode::OK, status, "{body}");
    assert!(body.get("isbn").is_none_or(|v| v.is_null()), "{body}");
}

/// A tag starts with a capital, keeps the rest as written, and is kept once whatever its case,
/// in the sidecar itself: « Horreur », « horreur » and « HORREUR » were three entries of one
/// filter, and the vocabulary suggested all three. The first spelling written is the one kept.
#[tokio::test]
async fn tags_start_with_a_capital_keep_the_rest_and_are_kept_once_each() {
    let server = Server::new();
    a_volume(&server);
    let series = server.series();

    let (status, body) = server
        .send(
            request("PATCH", &format!("/series/{series}"), IMPORTER)
                .header("content-type", "application/json")
                .body(json_body(serde_json::json!({
                    "tags": ["horreur", " HORREUR ", "éNIGME", "BD", "fire Force", ""],
                })))
                .unwrap(),
        )
        .await;

    assert_eq!(StatusCode::OK, status, "{body}");
    assert_eq!(
        body["tags"],
        serde_json::json!(["BD", "Fire Force", "Horreur", "ÉNIGME"])
    );
    let file = std::fs::read_to_string(server.library().join("Bleach/work.json")).unwrap();
    assert!(file.contains("\"Horreur\""), "the file says it too: {file}");
}

/// A field the server does not write disappears at the first edit — so every field added
/// alongside `authors` has to round-trip through a patch exactly like the ones already
/// there, and a later patch touching something else must not lose what an earlier one wrote.
#[tokio::test]
async fn the_new_fields_round_trip_through_a_patch_and_a_later_one_does_not_lose_them() {
    let server = Server::new();
    a_volume(&server);
    let series = server.series();

    let (status, body) = server
        .send(
            request("PATCH", &format!("/series/{series}"), IMPORTER)
                .header("content-type", "application/json")
                .body(json_body(serde_json::json!({
                    "authors": ["Tsugumi Ōba"],
                    "artists": ["Takeshi Obata"],
                    "tags": ["Enquête"],
                    "ageRating": "16+",
                    "collection": "Dark Kana",
                    "colour": false,
                })))
                .unwrap(),
        )
        .await;

    assert_eq!(StatusCode::OK, status, "{body}");
    assert_eq!(body["authors"], serde_json::json!(["Tsugumi Ōba"]));
    assert_eq!(body["artists"], serde_json::json!(["Takeshi Obata"]));
    assert_eq!(body["tags"], serde_json::json!(["Enquête"]));
    assert_eq!(body["ageRating"], "16+");
    assert_eq!(body["collection"], "Dark Kana");
    assert_eq!(body["colour"], false);

    // A second patch, touching only the summary — nothing said here about the fields the
    // first patch wrote.
    let (status, body) = server
        .send(
            request("PATCH", &format!("/series/{series}"), IMPORTER)
                .header("content-type", "application/json")
                .body(json_body(serde_json::json!({"summary": "Un carnet."})))
                .unwrap(),
        )
        .await;

    assert_eq!(StatusCode::OK, status, "{body}");
    assert_eq!(
        body["authors"],
        serde_json::json!(["Tsugumi Ōba"]),
        "the first patch's authors must survive the second: {body}"
    );
    assert_eq!(body["artists"], serde_json::json!(["Takeshi Obata"]));
    assert_eq!(body["tags"], serde_json::json!(["Enquête"]));
    assert_eq!(body["ageRating"], "16+");
    assert_eq!(body["collection"], "Dark Kana");
    assert_eq!(body["colour"], false);
}

/// A sidecar is a file a person edits, and a person puts things in it this server has never
/// heard of. Serialising a type back over it threw all of them away, silently, at the first
/// correction of a title — measured on a `work.json` carrying one hand-written key.
#[tokio::test]
async fn a_field_this_server_does_not_know_survives_a_patch() {
    let server = Server::new();
    a_volume(&server);
    let series = server.series();

    let sidecar = server.library().join("Bleach/work.json");
    // The id the first scan already stamped, kept — a person hand-editing a sidecar to add a
    // field this server has never heard of does not also delete the one field they cannot
    // read either. Dropping it here made the folder's identity look abandoned rather than
    // edited: the rescan this patch triggers minted a fresh one for the work, which minted a
    // fresh one for its implicit edition in turn, and let go of the row filed under the id
    // this request named — answering "unknown series" for an edit that had just landed.
    let work_id = serde_json::from_slice::<serde_json::Value>(&std::fs::read(&sidecar).unwrap())
        .unwrap()["id"]
        .as_str()
        .expect("the first scan stamped one")
        .to_string();
    std::fs::write(
        &sidecar,
        format!(
            r#"{{"leaf":1,"title":"Bleach","myField":"kept?","status":"ONGOING","id":"{work_id}"}}"#
        ),
    )
    .unwrap();

    let (status, body) = server
        .send(
            request("PATCH", &format!("/series/{series}"), IMPORTER)
                .header("content-type", "application/json")
                .body(json_body(serde_json::json!({"title": "BLEACH"})))
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::OK, status, "{body}");

    // Retitled, so the folder is named after the new title: the sidecar went with it.
    let sidecar = server.library().join("BLEACH/work.json");
    let written: serde_json::Value =
        serde_json::from_slice(&std::fs::read(&sidecar).unwrap()).unwrap();
    assert_eq!(
        "kept?", written["myField"],
        "the unknown field must survive the patch: {written}"
    );
    assert_eq!("BLEACH", written["title"]);
    assert_eq!("ONGOING", written["status"]);
    // And where its author put it: a write that sorts the keys is a diff across a whole
    // library for a change nobody made.
    let keys: Vec<&str> = written
        .as_object()
        .unwrap()
        .keys()
        .map(String::as_str)
        .collect();
    assert!(
        keys.starts_with(&["leaf", "title", "myField", "status"]),
        "the keys kept their order, whatever the scan appended after them: {keys:?}"
    );
}

/// Marking a whole series is part of reading, like recording a position, so a read-only key
/// may do it — otherwise closing a series you finished would need an import right.
///
/// And it is one call, not one per volume: the menu entry exists because the route does.
#[tokio::test]
async fn a_whole_series_is_marked_read_then_forgotten_through_the_router() {
    let server = Server::new();
    a_volume(&server);
    let series = server.series();
    let progress = format!("/series/{series}/progress");

    let (status, body) = server
        .send(
            request("PATCH", &progress, READ_ONLY)
                .header("content-type", "application/json")
                .body(json_body(serde_json::json!({"finished": true})))
                .unwrap(),
        )
        .await;

    assert_eq!(StatusCode::OK, status);
    let marked = body.as_array().expect("an array");
    assert_eq!(1, marked.len());
    assert_eq!(serde_json::json!(true), marked[0]["finished"]);
    assert_eq!(serde_json::json!(1), marked[0]["timesFinished"]);

    // Clearing the mark is not forgetting the position, so the record is still there.
    let (status, body) = server
        .send(
            request("PATCH", &progress, READ_ONLY)
                .header("content-type", "application/json")
                .body(json_body(serde_json::json!({"finished": false})))
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::OK, status);
    let cleared = body.as_array().expect("an array");
    assert_eq!(1, cleared.len());
    assert_eq!(serde_json::json!(false), cleared[0]["finished"]);
    // The ending is not undone by saying the book is no longer open at its last page.
    assert_eq!(serde_json::json!(1), cleared[0]["timesFinished"]);

    let (status, _) = server
        .send(
            request("DELETE", &progress, READ_ONLY)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::NO_CONTENT, status);

    let (status, body) = server
        .send(
            request("GET", &progress, READ_ONLY)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::OK, status);
    assert!(body.as_array().expect("an array").is_empty());
}

#[tokio::test]
async fn marking_a_series_that_is_not_there_is_a_404() {
    let server = Server::new();
    a_volume(&server);

    let (status, _) = server
        .send(
            request("PATCH", "/series/nothing-like-it/progress", READ_ONLY)
                .header("content-type", "application/json")
                .body(json_body(serde_json::json!({"finished": true})))
                .unwrap(),
        )
        .await;

    assert_eq!(StatusCode::NOT_FOUND, status);
}

#[tokio::test]
async fn patching_something_that_is_not_there_is_a_404() {
    let server = Server::new();
    a_volume(&server);

    let (status, _) = server
        .send(
            request("PATCH", "/entries/nothing-like-it", IMPORTER)
                .header("content-type", "application/json")
                .body(json_body(serde_json::json!({"title": "x"})))
                .unwrap(),
        )
        .await;

    assert_eq!(StatusCode::NOT_FOUND, status);
}

/// A word the contract has not got is refused here, and not met again by the scanner.
///
/// `set_arcs` learned this for units and this route kept doing the other thing for the three
/// enums beside them: `{"status": "paused"}` was answered 200, written into work.json, and
/// served straight back out of `GET /series` against an enum that did not hold it. `intake` then read
/// it as "not ongoing" and filed an extra volume as though the series were finished, and a
/// client that maps a word it does not know to nothing showed the field empty — an edit that
/// looked like it had done nothing at all.
#[tokio::test]
async fn a_word_the_contract_has_not_got_is_refused_rather_than_written_down() {
    let server = Server::new();
    a_volume(&server);
    let series = server.series();

    let patch = |body: serde_json::Value| {
        request("PATCH", &format!("/series/{series}"), IMPORTER)
            .header("content-type", "application/json")
            .body(json_body(body))
            .unwrap()
    };

    for (field, word) in [
        ("status", "paused"),
        ("medium", "graphic-novel"),
        ("readingDirection", "SIDEWAYS"),
    ] {
        let (status, body) = server.send(patch(serde_json::json!({field: word}))).await;
        assert_eq!(StatusCode::BAD_REQUEST, status, "{field}: {body}");
        // And the refusal names the vocabulary: a caller told only that its word was wrong
        // has to go and find the contract.
        let said = body["error"].as_str().unwrap_or_default();
        assert!(said.contains(word), "{field}: {said}");
    }

    // Nothing of any of it reached the disk.
    let work = server.library().join("Bleach/work.json");
    let written = std::fs::read_to_string(&work).unwrap_or_default();
    assert!(!written.contains("paused"), "{written}");

    // And the same field, spelled the way somebody would actually type it, still goes
    // through — in the contract's spelling, because the file says what the format says.
    let (status, body) = server
        .send(patch(serde_json::json!({"status": "Ongoing"})))
        .await;
    assert_eq!(StatusCode::OK, status, "{body}");
    let written = std::fs::read_to_string(&work).expect("a work.json");
    assert!(written.contains("\"status\": \"ongoing\""), "{written}");
}

// -------------------------------------------------------------------- intake

/// A sidecar that cannot be read is not a sidecar that is not there.
///
/// `fs::read(..).ok()` said the same thing about both, so a patch carrying one field started
/// from a default and wrote it over a work.json holding a title, an author, genres and arcs.
/// A file that cannot be read is still a file, and it is the one about to be replaced.
#[cfg(unix)]
#[tokio::test]
async fn a_sidecar_that_cannot_be_read_is_refused_rather_than_replaced() {
    let server = Server::new();
    a_volume(&server);
    let series = server.series();

    // A link to itself: the kernel answers ELOOP, which is neither "there" nor "not there".
    // The same shape as a permission or a device error, and reachable without either. In
    // place of the file the scan wrote there to carry the folder's identity — see
    // `scan::identity` — which is why this removes one before it links one.
    let sidecar = server.library().join("Bleach/work.json");
    std::fs::remove_file(&sidecar).ok();
    std::os::unix::fs::symlink("work.json", &sidecar).unwrap();

    let (status, _) = server
        .send(
            request("PATCH", &format!("/series/{series}"), IMPORTER)
                .header("content-type", "application/json")
                .body(json_body(serde_json::json!({"summary": "deux mots"})))
                .unwrap(),
        )
        .await;

    assert_eq!(StatusCode::INTERNAL_SERVER_ERROR, status);
    assert!(
        std::fs::symlink_metadata(&sidecar).unwrap().is_symlink(),
        "the file that could not be read must still be there, untouched"
    );
}

#[tokio::test]
async fn an_upload_without_a_name_is_the_callers_fault_not_the_servers() {
    let server = Server::new();
    let (status, body) = server
        .send(
            request("POST", "/entries", IMPORTER)
                .body(Body::from(archive_bytes(None)))
                .unwrap(),
        )
        .await;

    assert_eq!(StatusCode::BAD_REQUEST, status);
    assert!(body["error"].as_str().unwrap().contains("X-Leaf-Name"));
}

#[tokio::test]
async fn an_upload_lands_in_the_inbox_and_comes_back_as_a_proposal() {
    let server = Server::new();
    a_volume(&server);

    let (status, body) = server
        .send(
            request("POST", "/entries", IMPORTER)
                .header("X-Leaf-Name", "Tome 2.cbz")
                .body(Body::from(archive_bytes(Some(&EntryJson {
                    leaf: Some(1),
                    work: Some("Bleach".into()),
                    number: Some(2.0),
                    ..Default::default()
                }))))
                .unwrap(),
        )
        .await;

    assert_eq!(StatusCode::OK, status);
    assert_eq!("CERTAIN", body["confidence"]);
    assert_eq!("Tome 2.cbz", body["name"]);
    assert_eq!("Bleach", body["read"]["work"]);
    // Nothing has moved: a proposal waits for a confirmation.
    assert!(!server.library().join("Bleach/Tome 2.cbz").exists());
}

#[tokio::test]
async fn an_upload_over_the_ceiling_is_stopped_and_leaves_nothing_behind() {
    let server = Server::new();
    // The state above is built with a 4 KB ceiling, which this comfortably passes.
    let (status, body) = server
        .send(
            request("POST", "/entries", IMPORTER)
                .header("X-Leaf-Name", "gros.cbz")
                .body(Body::from(vec![0u8; 16 * 1024]))
                .unwrap(),
        )
        .await;

    assert_eq!(StatusCode::BAD_REQUEST, status);
    assert!(body["error"].as_str().unwrap().contains("limit"));
    // Half a file left in the inbox says nothing and cannot be resumed.
    let received = server.dir.path().join("inbox/received");
    let left = std::fs::read_dir(&received)
        .map(|d| d.flatten().count())
        .unwrap_or(0);
    assert_eq!(0, left, "the staging folder is cleared");
}

/// The whole road one dropped volume takes, through the router and not past it.
///
/// `proposals.rs` drives `Intake` directly and the bulk tests below drive a folder's road,
/// so the three handlers a single `.cbz` actually reaches — `/preflight`, `GET /intake/{id}`
/// and `PUT /intake/{id}/file` — were answered by no test at all. What that leaves unwatched
/// is not the reserving: it is the `Content-Range` a resume turns on, the number the server
/// sends back for a client to count against, and the 409 that says where to start again.
/// Every one of those can stop working without the file road failing loudly.
#[tokio::test]
async fn a_file_is_announced_then_sent_in_pieces_and_says_how_much_arrived() {
    let server = Server::new();
    a_volume(&server);
    let sidecar = serde_json::json!({"leaf": 1, "work": "Bleach", "number": 3.0});
    let bytes = archive_bytes(Some(&EntryJson {
        leaf: Some(1),
        work: Some("Bleach".into()),
        number: Some(3.0),
        ..Default::default()
    }));
    let whole = bytes.len() as u64;

    // The sidecar travels with the announcement rather than with the bytes, which is the
    // whole reason the proposal can be shown while the transfer is still running: the
    // client read it out of the archive before sending a byte.
    let (status, reserved) = server
        .send(
            request("POST", "/preflight", IMPORTER)
                .header("content-type", "application/json")
                .body(json_body(serde_json::json!({
                    "name": "Tome 3.cbz",
                    "size": whole,
                    "sidecar": sidecar.to_string(),
                })))
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::OK, status);
    let id = reserved["id"].as_str().expect("an id").to_string();
    // The place is held before a byte moves, which is the whole point of announcing: the
    // proposal is already there to be shown while the transfer runs.
    assert_eq!("Bleach", reserved["proposal"]["read"]["work"]);

    let (status, staged) = server
        .send(
            request("GET", &format!("/intake/{id}"), IMPORTER)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::OK, status);
    assert_eq!("Tome 3.cbz", staged["name"]);
    assert_eq!(whole, staged["size"].as_u64().unwrap());
    assert_eq!(0, staged["received"].as_u64().unwrap());

    let half = (whole / 2) as usize;
    let (status, sent) = server
        .send(
            request("PUT", &format!("/intake/{id}/file"), IMPORTER)
                .header("content-range", format!("bytes 0-{}/{whole}", half - 1))
                .body(Body::from(bytes[..half].to_vec()))
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::OK, status);
    assert_eq!(half as u64, sent["received"].as_u64().unwrap());

    // Asked between two pieces, which is the one moment the answer is worth anything: a
    // client that lost its connection has no other way to learn where to start again.
    let (_, staged) = server
        .send(
            request("GET", &format!("/intake/{id}"), IMPORTER)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(half as u64, staged["received"].as_u64().unwrap());

    let (status, _) = server
        .send(
            request("PUT", &format!("/intake/{id}/file"), IMPORTER)
                .header(
                    "content-range",
                    format!("bytes {half}-{}/{whole}", whole - 1),
                )
                .body(Body::from(bytes[half..].to_vec()))
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::OK, status);
    assert_eq!(
        whole,
        std::fs::read(
            server
                .dir
                .path()
                .join("inbox/received")
                .join(&id)
                .join("Tome 3.cbz")
        )
        .unwrap()
        .len() as u64,
        "the two pieces are the file, in order"
    );
}

/// A resume that starts past what the server holds is a 409 saying how much that is.
///
/// Not a 400 and not a silent hole: the client believed it had sent more than arrived, and
/// the only answer it can act on is the number. Writing at the offset it asked for would
/// leave a gap of zeroes inside a volume that looks complete afterwards — a corruption
/// nothing downstream would call one.
#[tokio::test]
async fn a_resume_past_what_arrived_says_where_to_start_again() {
    let server = Server::new();
    a_volume(&server);
    let bytes = archive_bytes(None);
    let whole = bytes.len() as u64;

    let (_, reserved) = server
        .send(
            request("POST", "/preflight", IMPORTER)
                .header("content-type", "application/json")
                .body(json_body(
                    serde_json::json!({"name": "Tome 4.cbz", "size": whole}),
                ))
                .unwrap(),
        )
        .await;
    let id = reserved["id"].as_str().expect("an id").to_string();

    let (status, body) = server
        .send(
            request("PUT", &format!("/intake/{id}/file"), IMPORTER)
                .header("content-range", format!("bytes 64-{}/{whole}", whole - 1))
                .body(Body::from(bytes))
                .unwrap(),
        )
        .await;

    assert_eq!(StatusCode::CONFLICT, status);
    assert_eq!(0, body["received"].as_u64().unwrap());
    assert!(body["error"].as_str().unwrap().contains("0 byte"), "{body}");
}

/// Bytes for a reservation nobody made are a 404, and asking about one is nothing found.
///
/// The id is the client's own memory of a transfer, and it outlives the server's: an
/// abandoned intake, a server restarted against an emptied inbox, a card resumed from a
/// session that was cleaned up. Answering anything but "there is no such thing" would have
/// the client go on sending a file into a folder that is not there.
#[tokio::test]
async fn an_intake_that_was_never_reserved_is_not_there_either_way() {
    let server = Server::new();

    let (status, _) = server
        .send(
            request("PUT", "/intake/rcv_nothing/file", IMPORTER)
                .body(Body::from(vec![0u8; 8]))
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::NOT_FOUND, status);

    let (status, _) = server
        .send(
            request("GET", "/intake/rcv_nothing", IMPORTER)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::NOT_FOUND, status);
}

// --------------------------------------------------------------- bulk import

#[tokio::test]
async fn a_transfer_resumes_where_the_server_says_it_stopped() {
    let server = Server::new();

    let (status, opened) = server
        .send(
            request("POST", "/import", IMPORTER)
                .header("content-type", "application/json")
                .body(json_body(serde_json::json!({
                    "root": "Bleach",
                    "files": [{"path": "Tome 1.cbz", "size": 8}],
                })))
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::OK, status);
    let id = opened["id"].as_str().expect("an id").to_string();
    assert_eq!(8, opened["bytesToSend"]);

    let (status, sent) = server
        .send(
            request(
                "PUT",
                &format!("/import/{id}/file?path=Tome%201.cbz"),
                IMPORTER,
            )
            .body(Body::from("abcd"))
            .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::OK, status);
    // A number, like every other count in this API.
    assert_eq!(4, sent["received"]);

    let (status, state) = server
        .send(
            request("GET", &format!("/import/{id}"), IMPORTER)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::OK, status);
    assert_eq!(4, state["received"]["Tome 1.cbz"]);

    let (status, _) = server
        .send(
            request(
                "PUT",
                &format!("/import/{id}/file?path=Tome%201.cbz"),
                IMPORTER,
            )
            .header("Content-Range", "bytes 4-7/8")
            .body(Body::from("efgh"))
            .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::OK, status);

    let (status, result) = server
        .send(
            request("POST", &format!("/import/{id}/commit"), IMPORTER)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::OK, status);
    assert_eq!(1, result["installed"]);
    assert_eq!(
        b"abcdefgh".to_vec(),
        std::fs::read(server.library().join("Bleach/Tome 1.cbz")).unwrap()
    );
}

#[tokio::test]
async fn an_impossible_offset_answers_409_carrying_what_the_server_holds() {
    let server = Server::new();
    let (_, opened) = server
        .send(
            request("POST", "/import", IMPORTER)
                .header("content-type", "application/json")
                .body(json_body(serde_json::json!({
                    "root": "Bleach",
                    "files": [{"path": "Tome 1.cbz", "size": 8}],
                })))
                .unwrap(),
        )
        .await;
    let id = opened["id"].as_str().unwrap().to_string();
    server
        .send(
            request(
                "PUT",
                &format!("/import/{id}/file?path=Tome%201.cbz"),
                IMPORTER,
            )
            .body(Body::from("abcd"))
            .unwrap(),
        )
        .await;

    let (status, body) = server
        .send(
            request(
                "PUT",
                &format!("/import/{id}/file?path=Tome%201.cbz"),
                IMPORTER,
            )
            .header("Content-Range", "bytes 7-7/8")
            .body(Body::from("h"))
            .unwrap(),
        )
        .await;

    assert_eq!(StatusCode::CONFLICT, status);
    // The client knows exactly where to resume without asking a second question.
    assert_eq!(4, body["received"]);
}

#[tokio::test]
async fn an_import_that_has_expired_is_a_404_not_a_bad_request() {
    let server = Server::new();
    let (status, _) = server
        .send(
            request("GET", "/import/imp_deadbeef", IMPORTER)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::NOT_FOUND, status);

    let (status, _) = server
        .send(
            request("POST", "/import/imp_deadbeef/commit", IMPORTER)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::NOT_FOUND, status);
}

#[tokio::test]
async fn an_id_that_is_not_an_id_is_a_bad_request() {
    let server = Server::new();
    let (status, _) = server
        .send(
            request("GET", "/import/..%2F..%2Fetc", IMPORTER)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::BAD_REQUEST, status);
}

/// An id is bytes until something has looked at it.
///
/// The check for a staged file's prefix sliced four **bytes** off the id, which lands in the
/// middle of a `€` and panics — and it ran before the line that refuses anything not ASCII,
/// so the guard never got its turn. The handler's task died and the connection was dropped,
/// on a route anybody holding a key can call.
#[tokio::test]
async fn an_id_that_is_not_ascii_is_refused_rather_than_fatal() {
    let server = Server::new();
    a_volume(&server);
    let series = server.series();

    let (status, _) = server
        .send(
            request("DELETE", "/intake/%C3%A9%E2%82%AC", IMPORTER)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::BAD_REQUEST, status, "abandoning it");

    // The other door to the same check, and the one that carries a body: filing a staged
    // file under an id nobody could have been given.
    let (status, _) = server
        .send(
            request("POST", "/intake/%C3%A9%E2%82%AC/file", IMPORTER)
                .header("content-type", "application/json")
                .body(json_body(serde_json::json!({"seriesId": series})))
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::BAD_REQUEST, status, "filing it");
}

#[tokio::test]
async fn a_file_of_an_import_needs_to_say_which_one() {
    let server = Server::new();
    let (_, opened) = server
        .send(
            request("POST", "/import", IMPORTER)
                .header("content-type", "application/json")
                .body(json_body(serde_json::json!({
                    "root": "Bleach",
                    "files": [{"path": "Tome 1.cbz", "size": 4}],
                })))
                .unwrap(),
        )
        .await;
    let id = opened["id"].as_str().unwrap().to_string();

    let (status, body) = server
        .send(
            request("PUT", &format!("/import/{id}/file"), IMPORTER)
                .body(Body::from("abcd"))
                .unwrap(),
        )
        .await;

    assert_eq!(StatusCode::BAD_REQUEST, status);
    assert!(body["error"].as_str().unwrap().contains("path"));
}

// ---------------------------------------------------------------- local drop

#[tokio::test]
async fn health_says_whether_the_short_path_exists() {
    let mut server = Server::new();
    let (_, body) = server
        .send(
            Request::builder()
                .uri("/health")
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    // Skipped at its default: a default does not cross the wire.
    assert!(body.get("localDrop").is_none());

    let folder = server.dir.path().join("drop");
    std::fs::create_dir_all(&folder).unwrap();
    server.drop = Some(folder);
    let (_, body) = server
        .send(
            Request::builder()
                .uri("/health")
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(true, body["localDrop"]);
}

#[tokio::test]
async fn the_drop_lists_what_is_waiting_and_takes_it_in() {
    let mut server = Server::new();
    a_volume(&server);
    let folder = server.dir.path().join("drop");
    std::fs::create_dir_all(&folder).unwrap();
    std::fs::write(
        folder.join("Tome 2.cbz"),
        archive_bytes(Some(&EntryJson {
            leaf: Some(1),
            work: Some("Bleach".into()),
            number: Some(2.0),
            ..Default::default()
        })),
    )
    .unwrap();
    server.drop = Some(folder.clone());

    let (status, listing) = server
        .send(
            request("GET", "/drop", IMPORTER)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::OK, status);
    assert_eq!("Tome 2.cbz", listing["files"][0]["name"]);

    let (status, proposal) = server
        .send(
            request("POST", "/drop", IMPORTER)
                .header("content-type", "application/json")
                .body(json_body(serde_json::json!({"name": "Tome 2.cbz"})))
                .unwrap(),
        )
        .await;

    assert_eq!(StatusCode::OK, status);
    assert_eq!("CERTAIN", proposal["confidence"]);
    // Consumed by default: nothing crossed the loopback and nothing was copied.
    assert!(!folder.join("Tome 2.cbz").exists());
}

// ------------------------------------------------------------ aiming a rescan

#[test]
fn a_rescan_is_aimed_at_a_work_and_never_at_a_universe() {
    let dir = tempfile::tempdir().unwrap();

    // Archives sitting right here: the work is this folder, with its implicit edition.
    let flat = dir.path().join("Bleach");
    std::fs::create_dir_all(&flat).unwrap();
    std::fs::write(flat.join("Tome 1.cbz"), archive_bytes(None)).unwrap();
    assert!(can_be_aimed_at(&flat));

    // A work that says so, holding edition folders.
    let with_editions = dir.path().join("Death Note");
    std::fs::create_dir_all(with_editions.join("Black Edition")).unwrap();
    std::fs::write(with_editions.join("work.json"), b"{\"leaf\":1}").unwrap();
    std::fs::write(
        with_editions.join("Black Edition/Tome 1.cbz"),
        archive_bytes(None),
    )
    .unwrap();
    assert!(can_be_aimed_at(&with_editions));

    // A universe. Aimed at, rescan_work would file it as a work and its works as editions:
    // the whole hierarchy off by one. And `root: "Terres d'Arran"` is how a new work joins
    // a universe, so this is an ordinary import target, not a corner.
    let universe = dir.path().join("Terres d'Arran");
    std::fs::create_dir_all(universe.join("Nains")).unwrap();
    std::fs::write(universe.join("universe.json"), b"{\"leaf\":1}").unwrap();
    std::fs::write(universe.join("Nains/Tome 1.cbz"), archive_bytes(None)).unwrap();
    assert!(!can_be_aimed_at(&universe));

    // Nothing under it at all, and nothing there at all.
    let empty = dir.path().join("Rien");
    std::fs::create_dir_all(empty.join("Non plus")).unwrap();
    assert!(!can_be_aimed_at(&empty));
    assert!(!can_be_aimed_at(Path::new("/nowhere/at/all")));
}

/// The same thing, through the route that meets it: an import into a universe.
#[tokio::test]
async fn importing_into_a_universe_leaves_its_shape_alone() {
    let server = Server::new();
    let universe = server.library().join("Terres d'Arran");
    std::fs::create_dir_all(universe.join("Nains")).unwrap();
    std::fs::write(
        universe.join("universe.json"),
        br#"{"leaf":1,"name":"Terres d'Arran"}"#,
    )
    .unwrap();
    std::fs::write(universe.join("Nains/Tome 1.cbz"), archive_bytes(None)).unwrap();
    server.scan();

    let shape = |what: &str| -> Vec<String> {
        server
            .db
            .read(|cx| cx.query(&format!("SELECT name FROM {what}"), [], |r| r.get(0)))
            .unwrap()
    };
    assert_eq!(vec!["Terres d'Arran".to_string()], shape("universe"));
    assert_eq!(vec!["Nains".to_string()], shape("work"));

    let volume = archive_bytes(None);
    let (_, opened) = server
        .send(
            request("POST", "/import", IMPORTER)
                .header("content-type", "application/json")
                .body(json_body(serde_json::json!({
                    "root": "Terres d'Arran",
                    "files": [{"path": "Nains/Tome 2.cbz", "size": volume.len()}],
                })))
                .unwrap(),
        )
        .await;
    let id = opened["id"].as_str().unwrap().to_string();
    server
        .send(
            request(
                "PUT",
                &format!("/import/{id}/file?path=Nains%2FTome%202.cbz"),
                IMPORTER,
            )
            .body(Body::from(volume))
            .unwrap(),
        )
        .await;
    let (status, _) = server
        .send(
            request("POST", &format!("/import/{id}/commit"), IMPORTER)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::OK, status);

    // The universe is still a universe and the work is still a work. Aimed at, the rescan
    // filed the universe as a work and Nains as one of its editions.
    assert_eq!(vec!["Terres d'Arran".to_string()], shape("universe"));
    assert_eq!(vec!["Nains".to_string()], shape("work"));
}

// ------------------------------------------------------------ behind a proxy

/// Wrong keys from one address, and whether the next caller pays for them.
async fn wrong_keys(
    server: &Server,
    state: &AppState,
    forwarded: Option<&str>,
    times: usize,
) -> StatusCode {
    let mut last = StatusCode::OK;
    for _ in 0..times {
        let mut request = Request::builder()
            .uri("/series")
            .header("X-Leaf-Key", "0000000000000000");
        if let Some(claimed) = forwarded {
            request = request.header("X-Forwarded-For", claimed);
        }
        let (status, _) = server
            .send_to(state.clone(), request.body(Body::empty()).unwrap())
            .await;
        last = status;
    }
    last
}

#[tokio::test]
async fn without_a_trusted_proxy_every_caller_is_the_same_one() {
    let server = Server::new();
    let state = server.state();

    // Nothing sets the header here, so a caller claiming an address must not be believed:
    // otherwise it would take one line of curl to walk past the throttle for ever.
    let status = wrong_keys(&server, &state, Some("10.0.0.99"), 12).await;
    assert_eq!(StatusCode::TOO_MANY_REQUESTS, status);
}

#[tokio::test]
async fn a_trusted_proxy_throttles_the_device_and_not_the_household() {
    let mut server = Server::new();
    server.trust_proxy = true;
    let state = server.state();

    // One misconfigured device gets itself blocked...
    let status = wrong_keys(&server, &state, Some("10.0.0.99"), 12).await;
    assert_eq!(StatusCode::TOO_MANY_REQUESTS, status);

    // ...and the phone next to it, behind the same proxy, is unaffected. Without the
    // header being read, both would look like the proxy and the whole house would be out.
    let (status, _) = server
        .send_to(
            state.clone(),
            Request::builder()
                .uri("/series")
                .header("X-Leaf-Key", IMPORTER)
                .header("X-Forwarded-For", "10.0.0.42")
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::OK, status);
}

/// The entry to believe is the last one, because it is the only one the proxy wrote.
///
/// `X-Forwarded-For` is a list the caller starts and every hop appends to. Counted from the
/// left, a caller sending a made-up address got a fresh identity on every request: ten wrong
/// keys never landed on one key, `blocked_for` never fired, and the throttle a key sits
/// behind was one header away from doing nothing at all.
#[tokio::test]
async fn a_caller_cannot_hand_itself_a_new_address_on_every_guess() {
    let mut server = Server::new();
    server.trust_proxy = true;
    let state = server.state();

    // What the proxy in front produces when the caller sends "1.2.3.4" itself: its own claim,
    // then the address the proxy actually accepted the connection from. A different claim
    // every time, and the same machine behind all of them.
    for guess in 0..12 {
        wrong_keys(
            &server,
            &state,
            Some(&format!("1.2.3.{guess}, 10.0.0.99")),
            1,
        )
        .await;
    }

    let (blocked, _) = server
        .send_to(
            state.clone(),
            Request::builder()
                .uri("/series")
                .header("X-Leaf-Key", "0000000000000000")
                .header("X-Forwarded-For", "5.6.7.8, 10.0.0.99")
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(
        StatusCode::TOO_MANY_REQUESTS,
        blocked,
        "twelve guesses from one machine are twelve guesses, whatever it called itself"
    );

    // And the device beside it, behind the same proxy, is still not paying for them.
    let (allowed, _) = server
        .send_to(
            state.clone(),
            Request::builder()
                .uri("/series")
                .header("X-Leaf-Key", IMPORTER)
                .header("X-Forwarded-For", "1.2.3.4, 10.0.0.42")
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::OK, allowed);
}

/// A byte no text can hold, in the half of the line the caller wrote.
///
/// hyper passes 0x80–0xFF through in a header value and `to_str` refuses the whole line for
/// them — and with a proxy that appends, the caller's own bytes are *in* that line. So one
/// byte anywhere before the last comma threw away the entry the proxy had written, and the
/// guess was counted against the proxy's socket address: the shared key every request
/// arriving without the header already lands on, health checks included. Read out of the
/// bytes, the trusted hop's entry stands whatever precedes it.
#[tokio::test]
async fn a_byte_no_text_can_hold_does_not_hand_the_guesses_to_the_household() {
    let mut server = Server::new();
    server.trust_proxy = true;
    let state = server.state();

    let unreadable = || {
        Request::builder()
            .uri("/series")
            .header("X-Leaf-Key", "0000000000000000")
            .header(
                "X-Forwarded-For",
                axum::http::HeaderValue::from_bytes(b"1.2.3.\xff, 10.0.0.99").unwrap(),
            )
            .body(Body::empty())
            .unwrap()
    };

    for _ in 0..12 {
        server.send_to(state.clone(), unreadable()).await;
    }

    // Twelve guesses from 10.0.0.99, whatever the caller wrote in front of it.
    let (blocked, _) = server
        .send_to(
            state.clone(),
            Request::builder()
                .uri("/series")
                .header("X-Leaf-Key", "0000000000000000")
                .header("X-Forwarded-For", "9.9.9.9, 10.0.0.99")
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::TOO_MANY_REQUESTS, blocked);

    // And the device beside it is not paying for them.
    let (allowed, _) = server
        .send_to(
            state.clone(),
            Request::builder()
                .uri("/series")
                .header("X-Leaf-Key", IMPORTER)
                .header("X-Forwarded-For", "1.2.3.4, 10.0.0.42")
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::OK, allowed);
}

/// And the last *header*, not merely the last entry of the first one.
///
/// A header may arrive more than once. Some proxies append to the list already there, others
/// add a second `X-Forwarded-For:` line of their own — and `HeaderMap::get` hands back the
/// first only, which against a caller who sent one itself is the caller's line. The rightmost
/// entry of a line the attacker wrote is still a value the attacker chose, so the guess the
/// test above closes was open again on every deployment whose proxy adds rather than appends.
#[tokio::test]
async fn a_line_the_caller_wrote_does_not_speak_over_the_one_the_proxy_added() {
    let mut server = Server::new();
    server.trust_proxy = true;
    let state = server.state();

    // Two header lines: the caller's own, with a fresh address per guess, and the proxy's
    // after it, naming the connection it actually accepted.
    let two_lines = |claimed: String, key: &'static str| {
        Request::builder()
            .uri("/series")
            .header("X-Leaf-Key", key)
            .header("X-Forwarded-For", claimed)
            .header("X-Forwarded-For", "10.0.0.99")
            .body(Body::empty())
            .unwrap()
    };

    for guess in 0..12 {
        server
            .send_to(
                state.clone(),
                two_lines(format!("1.2.3.{guess}"), "0000000000000000"),
            )
            .await;
    }

    let (blocked, _) = server
        .send_to(
            state.clone(),
            two_lines("5.6.7.8".to_string(), "0000000000000000"),
        )
        .await;
    assert_eq!(
        StatusCode::TOO_MANY_REQUESTS,
        blocked,
        "the proxy writes last, and a caller does not get to write after it"
    );
}

// --------------------------------------------------------- the intake's name

#[tokio::test]
async fn a_staged_file_is_reached_at_intake_and_not_at_entries() {
    let server = Server::new();
    a_volume(&server);

    let (_, proposal) = server
        .send(
            request("POST", "/entries", IMPORTER)
                .header("X-Leaf-Name", "Tome 2.cbz")
                .body(Body::from(archive_bytes(Some(&EntryJson {
                    leaf: Some(1),
                    work: Some("Bleach".into()),
                    number: Some(2.0),
                    ..Default::default()
                }))))
                .unwrap(),
        )
        .await;
    let id = proposal["received"].as_str().expect("an id").to_string();

    // The old spelling is gone rather than kept alongside: there is no client to break,
    // and two names for one thing is how one of them rots.
    let (status, _) = server
        .send(
            request("DELETE", &format!("/entries/received/{id}"), IMPORTER)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::NOT_FOUND, status);

    let (status, _) = server
        .send(
            request("DELETE", &format!("/intake/{id}"), IMPORTER)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::NO_CONTENT, status);
}

// ------------------------------------------------------------------- moving

/// The work whose folder a test is about to move, and the universe to move it into.
fn a_work_and_a_universe(server: &Server) -> (String, String) {
    a_volume(server);
    // The universe holds a series of its own, because a universe folder with nothing under
    // it is not recorded — the model is three floors deep and an empty one has no floor.
    let arran = server.library().join("Terres d’Arran");
    std::fs::create_dir_all(arran.join("Elfes")).unwrap();
    std::fs::write(
        arran.join("universe.json"),
        "{\"leaf\":1,\"name\":\"Terres d’Arran\"}".as_bytes(),
    )
    .unwrap();
    std::fs::write(arran.join("Elfes/Tome 1.cbz"), archive_bytes(None)).unwrap();
    server.scan();

    let work = server
        .db
        .read(|cx| {
            cx.query_one("SELECT id FROM work WHERE name = 'Bleach'", [], |r| {
                r.get::<_, String>(0)
            })
        })
        .unwrap()
        .expect("a work");
    let universe = server
        .db
        .read(|cx| cx.query_one("SELECT id FROM universe", [], |r| r.get::<_, String>(0)))
        .unwrap()
        .expect("a universe");
    (work, universe)
}

async fn move_it(
    server: &Server,
    work: &str,
    body: serde_json::Value,
) -> (StatusCode, serde_json::Value) {
    server
        .send(
            request("POST", &format!("/works/{work}/move"), IMPORTER)
                .header("content-type", "application/json")
                .body(json_body(body))
                .unwrap(),
        )
        .await
}

/// The sixth case of the import: a universe arrives, and one of its series is already
/// somewhere else on the disk. Filing it is a `rename` and a rescan — and it is only that
/// because an identity no longer comes from a path.
#[tokio::test]
async fn moving_a_work_into_a_universe_moves_the_folder_and_keeps_the_reading_position() {
    let server = Server::new();
    let (work, universe) = a_work_and_a_universe(&server);
    server
        .db
        .write(|cx| {
            cx.execute(
                "INSERT INTO progress (entry_id, edition_id, page, finished, updated_at)
                 SELECT id, edition_id, 42, 0, 1 FROM entry",
                [],
            )?;
            Ok(())
        })
        .unwrap();

    let (status, body) = move_it(&server, &work, serde_json::json!({"universeId": universe})).await;

    assert_eq!(StatusCode::OK, status, "{body}");
    assert_eq!(true, body["moved"]);
    assert!(
        body["path"]
            .as_str()
            .unwrap_or_default()
            .ends_with("Terres d’Arran/Bleach"),
        "{body}"
    );
    // The universe enters the name of a volume, so the file takes it.
    assert!(server
        .library()
        .join("Terres d’Arran/Bleach/terres-d-arran_bleach_tome_001.cbz")
        .exists());
    assert!(!server.library().join("Bleach").exists());

    // The index followed, and so did the place the reader stopped at — which is the whole
    // reason this route waited for the identity to leave the path.
    let (universe_of_work, page): (Option<String>, Option<i64>) = server
        .db
        .read(|cx| {
            Ok((
                cx.query_one(
                    "SELECT universe_id FROM work WHERE id = ?1",
                    [work.as_str()],
                    |r| r.get::<_, Option<String>>(0),
                )?
                .flatten(),
                cx.query_one("SELECT page FROM progress", [], |r| r.get::<_, i64>(0))?,
            ))
        })
        .unwrap();
    assert_eq!(Some(universe.clone()), universe_of_work);
    assert_eq!(Some(42), page);
}

/// A file in the way of a name the move would give is told before anything moves: the folder
/// that had already gone when the 409 came left the index naming the old universe, and the
/// answer said nothing had been changed.
#[tokio::test]
async fn a_move_whose_names_collide_is_refused_before_the_folder_moves() {
    let server = Server::new();
    let (work, universe) = a_work_and_a_universe(&server);
    std::fs::write(
        server
            .library()
            .join("Bleach/terres-d-arran_bleach_tome_001.cbz"),
        b"in the way",
    )
    .unwrap();

    let (status, body) = move_it(&server, &work, serde_json::json!({"universeId": universe})).await;

    assert_eq!(StatusCode::CONFLICT, status, "{body}");
    assert!(
        server.library().join("Bleach/Tome 1.cbz").exists()
            || server.library().join("Bleach").is_dir(),
        "the folder is where it was"
    );
    assert!(!server.library().join("Terres d’Arran/Bleach").exists());
}

/// And out again. A universe left is a destination like any other, and the root the work
/// already lives under is the only honest one — a server with two libraries must not move a
/// series from one to the other because a body said nothing.
#[tokio::test]
async fn a_work_with_no_universe_named_goes_back_to_the_root_it_came_from() {
    let server = Server::new();
    let (work, universe) = a_work_and_a_universe(&server);
    let (status, _) = move_it(&server, &work, serde_json::json!({"universeId": universe})).await;
    assert_eq!(StatusCode::OK, status);

    let (status, body) = move_it(&server, &work, serde_json::json!({})).await;

    assert_eq!(StatusCode::OK, status, "{body}");
    assert_eq!(true, body["moved"], "{body}");
    // Out of the universe, the volume loses it in its name.
    assert!(server.library().join("Bleach/bleach_tome_001.cbz").exists());
    let universe_of_work: Option<String> = server
        .db
        .read(|cx| {
            cx.query_one(
                "SELECT universe_id FROM work WHERE id = ?1",
                [work.as_str()],
                |r| r.get::<_, Option<String>>(0),
            )
        })
        .unwrap()
        .flatten();
    assert_eq!(None, universe_of_work);
}

/// Asking twice is what a retry looks like, so a work already where it is asked to go is
/// not a fault. It is said, though: a move that did nothing would otherwise answer exactly
/// like one that did.
#[tokio::test]
async fn a_work_already_where_it_is_asked_to_go_says_it_moved_nothing() {
    let server = Server::new();
    let (work, universe) = a_work_and_a_universe(&server);
    assert_eq!(
        StatusCode::OK,
        move_it(&server, &work, serde_json::json!({"universeId": universe}))
            .await
            .0
    );

    let (status, body) = move_it(&server, &work, serde_json::json!({"universeId": universe})).await;

    assert_eq!(StatusCode::OK, status, "{body}");
    assert_eq!(false, body["moved"], "{body}");
    assert!(server
        .library()
        .join("Terres d’Arran/Bleach/terres-d-arran_bleach_tome_001.cbz")
        .exists());
}

/// A folder of that name already at the destination is refused rather than resolved:
/// renaming one of the two is a decision about a library, not about a request.
#[tokio::test]
async fn a_name_already_taken_at_the_destination_is_refused_and_nothing_moves() {
    let server = Server::new();
    let (work, universe) = a_work_and_a_universe(&server);
    std::fs::create_dir_all(server.library().join("Terres d’Arran/Bleach")).unwrap();

    let (status, body) = move_it(&server, &work, serde_json::json!({"universeId": universe})).await;

    assert_eq!(StatusCode::BAD_REQUEST, status, "{body}");
    assert!(
        body["error"]
            .as_str()
            .unwrap_or_default()
            .contains("Bleach"),
        "{body}"
    );
    assert!(server.library().join("Bleach/Tome 1.cbz").exists());
}

/// A work that is not there, and a universe that is not there, are both 404 — and the
/// second says which of the two was missing, or a caller cannot tell a wrong id from a
/// wrong destination.
#[tokio::test]
async fn moving_something_that_is_not_there_is_a_404_that_says_which() {
    let server = Server::new();
    let (work, _) = a_work_and_a_universe(&server);

    let (status, body) = move_it(&server, "not-a-work", serde_json::json!({})).await;
    assert_eq!(StatusCode::NOT_FOUND, status);
    assert!(
        body["error"].as_str().unwrap_or_default().contains("work"),
        "{body}"
    );

    let (status, body) = move_it(
        &server,
        &work,
        serde_json::json!({"universeId": "not-a-universe"}),
    )
    .await;
    assert_eq!(StatusCode::NOT_FOUND, status);
    assert!(
        body["error"]
            .as_str()
            .unwrap_or_default()
            .contains("universe"),
        "{body}"
    );
}

/// The sixth case, end to end: a universe arrives and one of the series it declares is
/// already on the disk somewhere else. It is a **move** and not a creation, and telling the
/// two apart is only possible because the folder's identity now travels in its sidecar.
#[tokio::test]
async fn a_universe_that_declares_a_series_already_here_offers_to_file_it() {
    let server = Server::new();
    a_volume(&server);
    // What the scan put in Bleach's own sidecar. The client reads it off the disk and sends
    // it back in the announcement, which is how the server recognises the same work.
    let carried: serde_json::Value =
        serde_json::from_slice(&std::fs::read(server.library().join("Bleach/work.json")).unwrap())
            .unwrap();
    let work = carried["id"]
        .as_str()
        .expect("a stamped identity")
        .to_string();

    let announce = serde_json::json!({
        "root": "Terres d’Arran",
        "files": [],
        "sidecars": [
            {"path": "universe.json", "json": "{\"leaf\":1,\"name\":\"Terres d’Arran\"}"},
            {"path": "Bleach/work.json", "json": carried.to_string()},
        ],
    });
    let (status, body) = server
        .send(
            request("POST", "/import", IMPORTER)
                .header("content-type", "application/json")
                .body(json_body(announce))
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::OK, status, "{body}");

    let moves = body["moves"].as_array().expect("moves");
    assert_eq!(1, moves.len(), "{body}");
    assert_eq!(work, moves[0]["workId"]);
    assert_eq!("Bleach", moves[0]["name"]);
    assert!(
        moves[0]["from"].as_str().unwrap().ends_with("Bleach"),
        "{body}"
    );
    // And it is not announced twice, under two verbs.
    let creates = body["creates"].as_array().expect("creates");
    assert!(
        creates.iter().all(|one| one["kind"] != "WORK"),
        "a work the library already holds is a move, not a creation: {body}"
    );

    let import = body["id"].as_str().expect("an import").to_string();
    let (status, body) = server
        .send(
            request("POST", &format!("/import/{import}/commit"), IMPORTER)
                .header("content-type", "application/json")
                .body(json_body(serde_json::json!({"move": [work]})))
                .unwrap(),
        )
        .await;

    assert_eq!(StatusCode::OK, status, "{body}");
    assert_eq!(serde_json::json!([work]), body["moved"], "{body}");
    assert!(server
        .library()
        .join("Terres d’Arran/Bleach/Tome 1.cbz")
        .exists());
    assert!(!server.library().join("Bleach").exists());
}

/// And nothing moves unless it was asked for. A commit with no body is the ordinary one,
/// and rearranging a library that was fine is the one thing this must never do on its own.
#[tokio::test]
async fn a_commit_that_names_nothing_moves_nothing() {
    let server = Server::new();
    a_volume(&server);
    let carried = std::fs::read_to_string(server.library().join("Bleach/work.json")).unwrap();

    let (_, body) = server
        .send(
            request("POST", "/import", IMPORTER)
                .header("content-type", "application/json")
                .body(json_body(serde_json::json!({
                    "root": "Terres d’Arran",
                    "files": [],
                    "sidecars": [
                        {"path": "universe.json", "json": "{\"leaf\":1}"},
                        {"path": "Bleach/work.json", "json": carried},
                    ],
                })))
                .unwrap(),
        )
        .await;
    let import = body["id"].as_str().expect("an import").to_string();

    let (status, body) = server
        .send(
            request("POST", &format!("/import/{import}/commit"), IMPORTER)
                .body(Body::empty())
                .unwrap(),
        )
        .await;

    assert_eq!(StatusCode::OK, status, "{body}");
    assert!(body["moved"].is_null(), "{body}");
    assert!(server.library().join("Bleach/Tome 1.cbz").exists());
}

/// A move whose source has gone and whose destination already holds this very work is a
/// resumption, not a collision.
///
/// A commit that moved a work and left its session open rescans behind itself, so a client
/// sending the rest and committing again with the same `move` list arrives here with a path
/// read before the move. Refusing made that the answer of the whole commit — nothing
/// installed — for a second commit asking for exactly what the first had already done.
#[tokio::test]
async fn a_move_that_has_already_happened_is_a_resumption_and_not_a_collision() {
    let server = Server::new();
    let (work, universe) = a_work_and_a_universe(&server);
    assert_eq!(
        StatusCode::OK,
        move_it(&server, &work, serde_json::json!({"universeId": universe}))
            .await
            .0
    );
    // The index is put back where it was before the move, which is what a rescan running
    // behind a commit leaves for the next one to read.
    let back = server
        .library()
        .join("Bleach")
        .to_string_lossy()
        .to_string();
    server
        .db
        .write(|cx| {
            cx.execute(
                "UPDATE work SET path = ?1 WHERE id = ?2",
                rusqlite::params![back, work],
            )
        })
        .unwrap();

    let (status, body) = move_it(&server, &work, serde_json::json!({"universeId": universe})).await;

    assert_eq!(StatusCode::OK, status, "{body}");
    assert_eq!(false, body["moved"], "{body}");
    assert!(server
        .library()
        .join("Terres d’Arran/Bleach/terres-d-arran_bleach_tome_001.cbz")
        .exists());
}

/// And a folder that is not this work at the destination is still a collision: only the
/// identity in its own sidecar tells a move that has already happened from one that cannot.
#[tokio::test]
async fn another_folder_at_the_destination_is_still_refused() {
    let server = Server::new();
    let (work, universe) = a_work_and_a_universe(&server);
    std::fs::create_dir_all(server.library().join("Terres d’Arran/Bleach")).unwrap();
    std::fs::write(
        server.library().join("Terres d’Arran/Bleach/work.json"),
        r#"{"id":"0123456789abcdef","leaf":1}"#,
    )
    .unwrap();
    std::fs::remove_dir_all(server.library().join("Bleach")).unwrap();

    let (status, body) = move_it(&server, &work, serde_json::json!({"universeId": universe})).await;

    assert_eq!(StatusCode::BAD_REQUEST, status, "{body}");
}

/// A file larger than this server takes in one upload is refused at the announcement, not
/// at its last byte.
///
/// The ceiling is the same one the receiving stream enforces. Without this the client
/// announced the file, was given a place for it, sent it, and was refused at the end — a
/// whole transfer spent to learn something the first request already said.
#[tokio::test]
async fn a_file_over_the_ceiling_is_refused_before_a_place_is_held_for_it() {
    let server = Server::new();
    let (status, body) = server
        .send(
            request("POST", "/preflight", IMPORTER)
                .header("content-type", "application/json")
                .body(json_body(serde_json::json!({
                    "name": "Tome 1.cbz",
                    "size": 8_u64 * 1024 * 1024 * 1024,
                })))
                .unwrap(),
        )
        .await;

    assert_eq!(StatusCode::BAD_REQUEST, status, "{body}");
    assert!(
        body["error"]
            .as_str()
            .unwrap_or_default()
            .contains("one upload"),
        "{body}"
    );
    // The half this test's name promises and never checked: a refusal that still reserved a
    // folder would leak one per refused drop, and the inbox is where that shows.
    assert_eq!(
        0,
        std::fs::read_dir(server.dir.path().join("inbox/received"))
            .map(|d| d.flatten().count())
            .unwrap_or(0),
        "nothing may be held for a file that was refused"
    );
}

/// A work cannot be moved into a folder that sits inside it.
///
/// The rename would take the destination away with the source, and before the refusal an
/// empty folder was created inside the work first — so the failure arrived as an EINVAL
/// from the filesystem, after a directory nobody asked for. Untested until now: removing
/// the refusal left the whole suite green.
///
/// Asked of `Relocate` directly, because the route cannot reach it: a universe declared
/// inside a work's own folder is never recorded — universes do not nest, and the walk reads
/// that folder as one of the work's editions. The guard is for the day something else calls
/// this, and a guard nothing exercises is a guard nobody knows is broken.
#[test]
fn a_work_cannot_be_moved_into_a_folder_that_sits_inside_it() {
    let server = Server::new();
    a_volume(&server);
    server.scan();
    let work = server
        .db
        .read(|cx| {
            cx.query_one("SELECT id FROM work WHERE name = 'Bleach'", [], |r| {
                r.get::<_, String>(0)
            })
        })
        .unwrap()
        .expect("a work");

    let roots = vec![server.library()];
    let inside = server.library().join("Bleach/Dedans/Bleach");
    let refused = leaf_server::api::relocate::Relocate::new(&server.db, &roots)
        .work(&work, &inside)
        .expect_err("a destination inside the work being moved");

    assert!(refused.to_string().contains("sits inside it"), "{refused}");
    assert!(
        !server.library().join("Bleach/Dedans").exists(),
        "and nothing was created on the way to refusing"
    );
}

// ------------------------------------------------------------ the cover of a volume

/// Three pages, a volume of its own, scanned; and the ids a test asks about.
fn three_pages() -> (Server, String, String) {
    let server = Server::new();
    let folder = server.library().join("Bleach");
    std::fs::create_dir_all(&folder).unwrap();
    let declared = EntryJson {
        leaf: Some(1),
        work: Some("Bleach".into()),
        number: Some(1.0),
        ..Default::default()
    };
    std::fs::write(
        folder.join("Tome 1.cbz"),
        common::archive_of_pages(&declared, 3),
    )
    .unwrap();
    server.scan();
    let (series, entry) = (server.series(), server.entry());
    (server, series, entry)
}

async fn choose_cover(server: &Server, entry: &str, body: serde_json::Value) -> StatusCode {
    server
        .send(
            request("PATCH", &format!("/entries/{entry}"), IMPORTER)
                .header("Content-Type", "application/json")
                .body(json_body(body))
                .unwrap(),
        )
        .await
        .0
}

/// The tile a shelf shows for `uri`, as bytes, so that two covers are compared by what they
/// are and not by what they are called.
async fn cover(server: &Server, uri: &str) -> Vec<u8> {
    let (status, bytes, _) = server.bytes(uri, None).await;
    assert_eq!(StatusCode::OK, status, "{uri}");
    bytes
}

#[tokio::test]
async fn a_chosen_page_becomes_the_cover_and_null_gives_page_zero_back() {
    let (server, _, entry) = three_pages();
    let uri = format!("/entries/{entry}/cover");
    let before = cover(&server, &uri).await;

    let (status, body) = server
        .send(
            request("PATCH", &format!("/entries/{entry}"), IMPORTER)
                .header("Content-Type", "application/json")
                .body(json_body(serde_json::json!({"coverPage": 2})))
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::OK, status, "{body}");
    assert_eq!(
        2, body["coverPage"],
        "the answer says what it now is: {body}"
    );

    let chosen = cover(&server, &uri).await;
    assert_ne!(before, chosen, "page 2 is not page 0");
    // The cover is the page, asked for narrow: there is no separate thing.
    assert_eq!(
        chosen,
        cover(&server, &format!("/entries/{entry}/pages/2?width=400")).await
    );

    // And it is in the file, which is the truth: a rebuilt index reads it back.
    let declared = server
        .db
        .read(|cx| cx.query_one("SELECT file FROM entry", [], |r| r.get::<_, String>(0)))
        .unwrap()
        .unwrap();
    let declared = leaf_server::api::records::read_entry_json(Path::new(&declared)).unwrap();
    assert_eq!(Some(2), declared.cover_page);

    assert_eq!(
        StatusCode::OK,
        choose_cover(&server, &entry, serde_json::json!({"coverPage": null})).await
    );
    assert_eq!(before, cover(&server, &uri).await);
    let (_, shown) = server
        .send(
            request("GET", &format!("/entries/{entry}"), READ_ONLY)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert!(shown.get("coverPage").is_none(), "{shown}");
}

/// A patch that does not mention the cover leaves it where it was: `Clear` exists because
/// absent and `null` are not the same word.
#[tokio::test]
async fn an_edit_that_does_not_name_the_cover_leaves_the_chosen_page_alone() {
    let (server, _, entry) = three_pages();
    choose_cover(&server, &entry, serde_json::json!({"coverPage": 1})).await;
    choose_cover(&server, &entry, serde_json::json!({"title": "Un titre"})).await;
    let (_, shown) = server
        .send(
            request("GET", &format!("/entries/{entry}"), READ_ONLY)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(1, shown["coverPage"], "{shown}");
}

#[tokio::test]
async fn a_cover_page_that_the_archive_does_not_hold_is_a_400() {
    let (server, _, entry) = three_pages();
    let uri = format!("/entries/{entry}/cover");
    let before = cover(&server, &uri).await;

    for page in [3, 99, -1] {
        assert_eq!(
            StatusCode::BAD_REQUEST,
            choose_cover(&server, &entry, serde_json::json!({ "coverPage": page })).await,
            "page {page}"
        );
    }
    // Refused before anything was written: the cover is still the one it was.
    assert_eq!(before, cover(&server, &uri).await);
}

#[tokio::test]
async fn a_series_shows_the_cover_its_first_volume_chose() {
    let (server, series, entry) = three_pages();
    let shelf = format!("/series/{series}/cover");
    let before = cover(&server, &shelf).await;

    choose_cover(&server, &entry, serde_json::json!({"coverPage": 1})).await;
    let after = cover(&server, &shelf).await;
    assert_ne!(before, after);
    assert_eq!(
        after,
        cover(&server, &format!("/entries/{entry}/cover")).await
    );
}

async fn choose_series_cover(
    server: &Server,
    series: &str,
    body: serde_json::Value,
) -> (StatusCode, serde_json::Value) {
    server
        .send(
            request("PATCH", &format!("/series/{series}"), IMPORTER)
                .header("Content-Type", "application/json")
                .body(json_body(body))
                .unwrap(),
        )
        .await
}

/// Two volumes and a `cover.jpg` in the folder; the second volume shows its third page, so
/// that all three covers are told apart by their bytes.
fn two_volumes_and_a_folder_image() -> (Server, String, String) {
    let server = Server::new();
    let folder = server.library().join("Bleach");
    std::fs::create_dir_all(&folder).unwrap();
    for (n, page) in [(1.0, None), (2.0, Some(2))] {
        let declared = EntryJson {
            leaf: Some(1),
            work: Some("Bleach".into()),
            number: Some(n),
            cover_page: page,
            ..Default::default()
        };
        std::fs::write(
            folder.join(format!("Tome {n}.cbz")),
            common::archive_of_pages(&declared, 3),
        )
        .unwrap();
    }
    std::fs::write(folder.join("cover.jpg"), common::coloured_jpeg(200)).unwrap();
    server.scan();
    let series = server.series();
    let second: String = server
        .db
        .read(|cx| {
            cx.query_one("SELECT id FROM entry WHERE volume_number = 2", [], |r| {
                r.get(0)
            })
        })
        .unwrap()
        .expect("volume 2");
    (server, series, second)
}

#[tokio::test]
async fn a_volume_chosen_for_the_series_comes_before_the_image_in_its_folder() {
    let (server, series, second) = two_volumes_and_a_folder_image();
    let shelf = format!("/series/{series}/cover");
    let folder_image = cover(&server, &shelf).await;

    let (status, body) =
        choose_series_cover(&server, &series, serde_json::json!({"coverVolume": 2})).await;
    assert_eq!(StatusCode::OK, status);
    assert_eq!(serde_json::json!(2.0), body["coverVolume"]);
    let chosen = cover(&server, &shelf).await;
    assert_ne!(folder_image, chosen);
    assert_eq!(
        chosen,
        cover(&server, &format!("/entries/{second}/cover")).await
    );

    // Taken away, the folder's image speaks again.
    let (_, body) =
        choose_series_cover(&server, &series, serde_json::json!({"coverVolume": null})).await;
    assert!(body.get("coverVolume").is_none());
    assert_eq!(folder_image, cover(&server, &shelf).await);
}

#[tokio::test]
async fn a_volume_the_series_does_not_hold_cannot_be_its_cover() {
    let (server, series, _) = two_volumes_and_a_folder_image();
    let shelf = format!("/series/{series}/cover");
    let before = cover(&server, &shelf).await;

    let (status, _) =
        choose_series_cover(&server, &series, serde_json::json!({"coverVolume": 9})).await;
    assert_eq!(StatusCode::BAD_REQUEST, status);
    assert_eq!(before, cover(&server, &shelf).await);
}

/// The tag is what a client holds a tile by. If it did not change with the cover, the client
/// would be told `304` and go on showing the page it was showing.
#[tokio::test]
async fn a_cover_already_held_is_not_served_stale_once_it_changes() {
    let (server, _, entry) = three_pages();
    let uri = format!("/entries/{entry}/cover");
    let (_, _, held) = server.bytes(&uri, None).await;
    assert_eq!(
        StatusCode::NOT_MODIFIED,
        server.bytes(&uri, Some(&held)).await.0,
        "held, and unchanged"
    );

    choose_cover(&server, &entry, serde_json::json!({"coverPage": 2})).await;
    let (status, _, fresh) = server.bytes(&uri, Some(&held)).await;
    assert_eq!(
        StatusCode::OK,
        status,
        "the tile the client holds is no longer the cover"
    );
    assert_ne!(held, fresh);
}

/// Chosen in the archive, then beside it, then page zero.
#[tokio::test]
async fn a_chosen_page_wins_over_an_image_beside_the_archive_which_wins_over_page_zero() {
    let (server, _, entry) = three_pages();
    let uri = format!("/entries/{entry}/cover");
    let page_zero = cover(&server, &uri).await;

    std::fs::write(
        server.library().join("Bleach/Tome 1.jpg"),
        common::coloured_jpeg(7),
    )
    .unwrap();
    server.scan();
    let beside = cover(&server, &uri).await;
    assert_ne!(
        page_zero, beside,
        "the image beside the archive speaks first"
    );

    choose_cover(&server, &entry, serde_json::json!({"coverPage": 1})).await;
    let chosen = cover(&server, &uri).await;
    assert_ne!(beside, chosen);
    assert_eq!(
        chosen,
        cover(&server, &format!("/entries/{entry}/pages/1?width=400")).await
    );

    choose_cover(&server, &entry, serde_json::json!({"coverPage": null})).await;
    assert_eq!(beside, cover(&server, &uri).await);
}

// ----------------------------------------------------- a cover sent from the reader's disk

async fn send_cover(
    server: &Server,
    uri: &str,
    method: &str,
    key: &str,
    bytes: Vec<u8>,
) -> StatusCode {
    server
        .send(request(method, uri, key).body(Body::from(bytes)).unwrap())
        .await
        .0
}

async fn series_json(server: &Server, series: &str) -> serde_json::Value {
    server
        .send(
            request("GET", &format!("/series/{series}"), READ_ONLY)
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .1
}

fn a_png() -> Vec<u8> {
    let mut out = std::io::Cursor::new(Vec::new());
    image::DynamicImage::ImageRgb8(image::RgbImage::from_pixel(
        40,
        60,
        image::Rgb([200, 30, 30]),
    ))
    .write_to(&mut out, image::ImageFormat::Png)
    .unwrap();
    out.into_inner()
}

#[tokio::test]
async fn an_image_sent_for_a_series_becomes_its_cover_beside_a_folder_image_it_leaves_alone() {
    let (server, series, _) = three_pages();
    std::fs::write(
        server.library().join("Bleach/cover.jpg"),
        common::coloured_jpeg(200),
    )
    .unwrap();
    server.scan();
    let uri = format!("/series/{series}/cover");
    let before = cover(&server, &uri).await;

    let sent = send_cover(&server, &uri, "PUT", IMPORTER, a_png()).await;
    assert_eq!(StatusCode::NO_CONTENT, sent);
    assert_ne!(before, cover(&server, &uri).await, "visible without a scan");
    let folder = server.library().join("Bleach");
    assert!(folder.join("leaf-cover.png").exists());
    assert_eq!(
        Some(true),
        series_json(&server, &series).await["coverImage"].as_bool()
    );
    assert!(
        folder.join("cover.jpg").exists(),
        "a hand-placed file is never touched"
    );

    // Another format replaces the first, and leaves no second one behind.
    let sent = send_cover(&server, &uri, "PUT", IMPORTER, common::coloured_jpeg(3)).await;
    assert_eq!(StatusCode::NO_CONTENT, sent);
    assert!(folder.join("leaf-cover.jpg").exists());
    assert!(!folder.join("leaf-cover.png").exists());

    // Withdrawn: only what the server wrote goes, and the folder's image speaks again.
    let gone = send_cover(&server, &uri, "DELETE", IMPORTER, vec![]).await;
    assert_eq!(StatusCode::NO_CONTENT, gone);
    assert!(!folder.join("leaf-cover.jpg").exists());
    assert!(folder.join("cover.jpg").exists());
    assert_eq!(before, cover(&server, &uri).await);
    assert!(series_json(&server, &series)
        .await
        .get("coverImage")
        .is_none());
}

#[tokio::test]
async fn a_file_that_is_not_an_image_is_refused_and_nothing_is_written() {
    let (server, series, entry) = three_pages();
    for uri in [
        format!("/series/{series}/cover"),
        format!("/entries/{entry}/cover"),
    ] {
        let before = cover(&server, &uri).await;
        for bytes in [
            b"not an image at all".to_vec(),
            vec![],
            // A PNG signature and nothing behind it.
            vec![0x89, b'P', b'N', b'G', 0x0d, 0x0a, 0x1a, 0x0a, 0, 0, 0],
            // A format the decoder knows and the server does not accept as a cover.
            b"GIF89a\x01\x00\x01\x00\x00\x00\x00;".to_vec(),
        ] {
            let sent = send_cover(&server, &uri, "PUT", IMPORTER, bytes).await;
            assert_eq!(StatusCode::BAD_REQUEST, sent, "{uri}");
        }
        assert_eq!(before, cover(&server, &uri).await);
    }
    let folder = server.library().join("Bleach");
    let names: Vec<String> = std::fs::read_dir(folder)
        .unwrap()
        .map(|e| e.unwrap().file_name().to_string_lossy().to_string())
        .collect();
    assert!(names.iter().all(|n| !n.contains("leaf-cover")), "{names:?}");
}

#[tokio::test]
async fn an_image_larger_than_the_ceiling_is_refused() {
    let (server, series, _) = three_pages();
    let huge = vec![0xff; leaf_server::api::cover_choice::MAX_BYTES + 1];
    let sent = send_cover(
        &server,
        &format!("/series/{series}/cover"),
        "PUT",
        IMPORTER,
        huge,
    )
    .await;
    assert!(
        sent == StatusCode::PAYLOAD_TOO_LARGE || sent == StatusCode::BAD_REQUEST,
        "{sent}"
    );
}

#[tokio::test]
async fn choosing_a_cover_image_needs_the_edit_right() {
    let (server, series, entry) = three_pages();
    for uri in [
        format!("/series/{series}/cover"),
        format!("/entries/{entry}/cover"),
    ] {
        for method in ["PUT", "DELETE"] {
            let sent = send_cover(&server, &uri, method, READ_ONLY, a_png()).await;
            assert_eq!(StatusCode::FORBIDDEN, sent, "{method} {uri}");
        }
    }
    let folder = server.library().join("Bleach");
    assert!(!folder.join("leaf-cover.png").exists());
}

#[tokio::test]
async fn a_cover_sent_for_something_unknown_is_a_404() {
    let (server, _, _) = three_pages();
    for uri in ["/series/nope/cover", "/entries/nope/cover"] {
        assert_eq!(
            StatusCode::NOT_FOUND,
            send_cover(&server, uri, "PUT", IMPORTER, a_png()).await,
            "{uri}"
        );
        assert_eq!(
            StatusCode::NOT_FOUND,
            send_cover(&server, uri, "DELETE", IMPORTER, vec![]).await,
            "{uri}"
        );
    }
}

/// The cover of a volume is its page `000`: sending an image writes it into the archive,
/// and a page chosen before it no longer outranks it. `three_pages` numbers its pages from
/// `000`, so the page is replaced and nothing moves.
#[tokio::test]
async fn an_image_sent_for_a_volume_replaces_its_page_000_and_beats_a_page_chosen_before() {
    let (server, _, entry) = three_pages();
    let uri = format!("/entries/{entry}/cover");
    let default = cover(&server, &uri).await;

    choose_cover(&server, &entry, serde_json::json!({"coverPage": 2})).await;
    let page = cover(&server, &uri).await;
    assert_ne!(default, page);

    let sent = send_cover(&server, &uri, "PUT", IMPORTER, a_png()).await;
    assert_eq!(StatusCode::NO_CONTENT, sent);
    let image = cover(&server, &uri).await;
    assert_ne!(
        page, image,
        "the image sent later beats the page chosen earlier"
    );
    // In the archive, and not beside it: it travels with the volume.
    assert!(!server
        .library()
        .join("Bleach/Tome 1.leaf-cover.png")
        .exists());
    let held = leaf_server::archive::cbz::read(&server.library().join("Bleach/Tome 1.cbz"), false)
        .unwrap()
        .pages;
    assert_eq!(3, held.len());
    assert_eq!("000.png", held[0].name);

    // A page chosen after the image beats it: page, then image beside, then page zero.
    choose_cover(&server, &entry, serde_json::json!({"coverPage": 1})).await;
    assert_ne!(image, cover(&server, &uri).await);
}

/// A file numbered `01`, `02`… is numbered as a library numbers a dozen pages: its cover is
/// `00`, not the `000` of a file of hundreds, so that the names still read as one series and
/// the archive does not carry two widths of the same count.
#[tokio::test]
async fn a_cover_for_a_volume_numbered_from_01_is_named_00() {
    let server = Server::new();
    let folder = server.library().join("Webtoon");
    std::fs::create_dir_all(&folder).unwrap();
    let archive = folder.join("Chapitre 1.cbz");
    {
        let mut zip = zip::ZipWriter::new(std::fs::File::create(&archive).unwrap());
        let options = zip::write::SimpleFileOptions::default();
        for seed in 1..=2u8 {
            zip.start_file::<_, ()>(format!("{seed:02}.jpg"), options)
                .unwrap();
            std::io::Write::write_all(&mut zip, &common::coloured_jpeg(seed)).unwrap();
        }
        zip.finish().unwrap();
    }
    server.scan();
    let entry = server.entry();
    let uri = format!("/entries/{entry}/cover");

    assert_eq!(
        StatusCode::NO_CONTENT,
        send_cover(&server, &uri, "PUT", IMPORTER, a_png()).await
    );

    let held = leaf_server::archive::cbz::read(&archive, false)
        .unwrap()
        .pages;
    let names: Vec<&str> = held.iter().map(|p| p.name.as_str()).collect();
    assert_eq!(vec!["00.png", "01.jpg", "02.jpg"], names);
}

/// A volume numbered from `001` has no cover page: giving it one adds the page, and every
/// page after it is a place further down — the reader's own place, the first page of each
/// chapter, the cover page a profile had chosen. All of it moves in the one operation, or a
/// chapter would begin on the page before its own and a reader be put back a page.
#[tokio::test]
async fn a_cover_for_a_volume_numbered_from_001_adds_the_page_and_moves_what_counts_pages() {
    let server = Server::new();
    let folder = server.library().join("Webtoon");
    std::fs::create_dir_all(&folder).unwrap();
    let declared = EntryJson {
        leaf: Some(1),
        work: Some("Webtoon".into()),
        number: Some(1.0),
        chapters: vec![
            ChapterJson {
                number: Some(1.0),
                start_page: Some(0),
                ..Default::default()
            },
            ChapterJson {
                number: Some(2.0),
                start_page: Some(2),
                ..Default::default()
            },
        ],
        ..Default::default()
    };
    let archive = folder.join("Chapitre 1.cbz");
    {
        let mut zip = zip::ZipWriter::new(std::fs::File::create(&archive).unwrap());
        let options = zip::write::SimpleFileOptions::default();
        for seed in 1..=3u8 {
            zip.start_file::<_, ()>(format!("{seed:03}.jpg"), options)
                .unwrap();
            std::io::Write::write_all(&mut zip, &common::coloured_jpeg(seed)).unwrap();
        }
        zip.start_file::<_, ()>("entry.json", options).unwrap();
        std::io::Write::write_all(&mut zip, &sidecars::write(&declared).unwrap()).unwrap();
        zip.finish().unwrap();
    }
    server.scan();
    let entry = server.entry();
    let uri = format!("/entries/{entry}/cover");
    let (status, _) = server
        .send(
            request("PATCH", &format!("/entries/{entry}/progress"), IMPORTER)
                .header("Content-Type", "application/json")
                .body(json_body(serde_json::json!({"page": 2})))
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::OK, status);
    let borrowed = cover(&server, &uri).await;

    assert_eq!(
        StatusCode::NO_CONTENT,
        send_cover(&server, &uri, "PUT", IMPORTER, a_png()).await
    );

    let held = leaf_server::archive::cbz::read(&archive, false)
        .unwrap()
        .pages;
    let names: Vec<&str> = held.iter().map(|p| p.name.as_str()).collect();
    assert_eq!(vec!["000.png", "001.jpg", "002.jpg", "003.jpg"], names);
    let declared = leaf_server::api::records::read_entry_json(&archive).unwrap();
    let starts: Vec<Option<i32>> = declared.chapters.iter().map(|c| c.start_page).collect();
    assert_eq!(vec![Some(1), Some(3)], starts);
    assert_eq!(None, declared.cover_page);
    // The reader is still on the page they were on: it is a place further down.
    let (status, progress) = server
        .send(
            request("GET", &format!("/entries/{entry}/progress"), IMPORTER)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::OK, status);
    assert_eq!(3, progress["page"], "{progress}");
    assert_eq!(4, progress["pageCount"], "{progress}");
    // And the volume has a cover of its own now: not the series', not its first page.
    assert_ne!(borrowed, cover(&server, &uri).await);
    let (_, shown) = server
        .send(
            request("GET", &format!("/entries/{entry}"), IMPORTER)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert!(shown.get("borrowedCover").is_none(), "{shown}");
}

#[tokio::test]
async fn an_image_placed_beside_a_volume_by_hand_refuses_a_cover_that_it_would_hide() {
    let (server, _, entry) = three_pages();
    let beside = server.library().join("Bleach/Tome 1.jpg");
    std::fs::write(&beside, common::coloured_jpeg(2)).unwrap();
    server.scan();
    let uri = format!("/entries/{entry}/cover");
    let theirs = cover(&server, &uri).await;
    let archive = std::fs::read(server.library().join("Bleach/Tome 1.cbz")).unwrap();

    let sent = send_cover(&server, &uri, "PUT", IMPORTER, a_png()).await;
    assert_eq!(StatusCode::BAD_REQUEST, sent);
    // Neither theirs nor the archive was touched.
    assert!(beside.exists());
    assert_eq!(theirs, cover(&server, &uri).await);
    assert_eq!(
        archive,
        std::fs::read(server.library().join("Bleach/Tome 1.cbz")).unwrap()
    );
}

// ------------------------------------- a volume chosen, an image sent: the last choice wins

#[tokio::test]
async fn an_image_sent_after_a_volume_wins_and_a_volume_chosen_after_an_image_wins() {
    let (server, series, second) = two_volumes_and_a_folder_image();
    let uri = format!("/series/{series}/cover");
    let folder = server.library().join("Bleach");
    let folder_image = cover(&server, &uri).await;

    choose_series_cover(&server, &series, serde_json::json!({"coverVolume": 2})).await;
    let volume_two = cover(&server, &format!("/entries/{second}/cover")).await;
    assert_eq!(volume_two, cover(&server, &uri).await);

    // The image comes after: it is shown, and the volume chosen before is forgotten.
    let sent = send_cover(&server, &uri, "PUT", IMPORTER, a_png()).await;
    assert_eq!(StatusCode::NO_CONTENT, sent);
    let image = cover(&server, &uri).await;
    assert_ne!(volume_two, image);
    assert!(series_json(&server, &series)
        .await
        .get("coverVolume")
        .is_none());

    // The volume comes after: the image the server wrote is taken back, the hand-placed
    // `cover.jpg` is not, and the volume is what is shown.
    let (status, _) =
        choose_series_cover(&server, &series, serde_json::json!({"coverVolume": 2})).await;
    assert_eq!(StatusCode::OK, status);
    assert!(!folder.join("leaf-cover.png").exists());
    assert!(folder.join("cover.jpg").exists());
    assert_eq!(volume_two, cover(&server, &uri).await);
    assert!(series_json(&server, &series).await["coverImage"].is_null());

    // Withdrawn, the folder's own image speaks, not the first volume.
    choose_series_cover(&server, &series, serde_json::json!({"coverVolume": null})).await;
    assert_eq!(folder_image, cover(&server, &uri).await);
}

#[tokio::test]
async fn deleting_the_cover_of_a_series_forgets_the_volume_chosen_too() {
    let (server, series, _) = two_volumes_and_a_folder_image();
    let uri = format!("/series/{series}/cover");
    let folder_image = cover(&server, &uri).await;

    choose_series_cover(&server, &series, serde_json::json!({"coverVolume": 2})).await;
    assert_ne!(folder_image, cover(&server, &uri).await);

    let gone = send_cover(&server, &uri, "DELETE", IMPORTER, vec![]).await;
    assert_eq!(StatusCode::NO_CONTENT, gone);
    assert_eq!(folder_image, cover(&server, &uri).await);
    assert!(series_json(&server, &series)
        .await
        .get("coverVolume")
        .is_none());
}

#[tokio::test]
async fn choosing_a_volume_without_the_right_to_edit_is_a_403_and_changes_nothing() {
    let (server, series, _) = two_volumes_and_a_folder_image();
    let uri = format!("/series/{series}/cover");
    let before = cover(&server, &uri).await;

    let (status, _) = server
        .send(
            request("PATCH", &format!("/series/{series}"), READ_ONLY)
                .header("Content-Type", "application/json")
                .body(json_body(serde_json::json!({"coverVolume": 2})))
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::FORBIDDEN, status);
    assert_eq!(before, cover(&server, &uri).await);
}

#[tokio::test]
async fn an_unknown_volume_is_a_400_that_leaves_the_image_sent_before_in_place() {
    let (server, series, _) = two_volumes_and_a_folder_image();
    let uri = format!("/series/{series}/cover");
    send_cover(&server, &uri, "PUT", IMPORTER, a_png()).await;
    let before = cover(&server, &uri).await;

    let (status, _) =
        choose_series_cover(&server, &series, serde_json::json!({"coverVolume": 9})).await;
    assert_eq!(StatusCode::BAD_REQUEST, status);
    assert!(server.library().join("Bleach/leaf-cover.png").exists());
    assert_eq!(before, cover(&server, &uri).await);
}

// ------------------------------------------------- an archive converted on its way in

/// The archive at `Bleach/Tome 1.cbz` recorded as the conversion of an imported file, the
/// way a commit records it, and its size as kept.
fn converted_on_import(server: &Server) -> u64 {
    let kept = std::fs::metadata(server.library().join("Bleach/Tome 1.cbz"))
        .unwrap()
        .len();
    leaf_server::store::origins::record(
        &server.db,
        &[leaf_server::store::origins::Origin {
            path: "Bleach/Tome 1.cbz".into(),
            kept_size: kept,
            origin_size: kept + 100,
            origin_checksum: None,
            rewrite: Some("fingerprint".into()),
        }],
    )
    .unwrap();
    kept
}

fn kept_size(server: &Server) -> u64 {
    leaf_server::store::origins::known(&server.db, &["Bleach/Tome 1.cbz".to_string()]).unwrap()
        ["Bleach/Tome 1.cbz"]
        .kept_size
}

fn size_on_disk(server: &Server) -> u64 {
    std::fs::metadata(server.library().join("Bleach/Tome 1.cbz"))
        .unwrap()
        .len()
}

/// Each route that writes into a volume says where the volume sits, or the origin it was
/// converted from is not followed and the next import offers to undo Leaf's own edit.
#[tokio::test]
async fn an_edit_through_the_api_keeps_the_origin_of_a_converted_volume() {
    let server = Server::new();
    a_volume(&server);
    let entry = server.entry();
    let kept = converted_on_import(&server);

    let (status, _) = server
        .send(
            request("PATCH", &format!("/entries/{entry}"), IMPORTER)
                .header("Content-Type", "application/json")
                .body(json_body(
                    serde_json::json!({"title": "Un titre bien plus long qu'avant"}),
                ))
                .unwrap(),
        )
        .await;

    assert_eq!(StatusCode::OK, status);
    assert_ne!(kept, size_on_disk(&server), "the volume was rewritten");
    assert_eq!(size_on_disk(&server), kept_size(&server));
}

#[tokio::test]
async fn a_download_that_stamps_a_converted_volume_keeps_its_origin() {
    let server = Server::new();
    a_volume(&server);
    let entry = server.entry();
    let kept = converted_on_import(&server);

    let (status, _, _) = server.bytes(&format!("/entries/{entry}/file"), None).await;

    assert_eq!(StatusCode::OK, status);
    assert_ne!(kept, size_on_disk(&server), "the volume was stamped");
    assert_eq!(size_on_disk(&server), kept_size(&server));
}

/// Forgetting a chosen page is a rewrite of the volume too, made through the cover routes.
#[tokio::test]
async fn forgetting_the_page_of_a_converted_volume_keeps_its_origin() {
    let (server, _, entry) = three_pages();
    assert_eq!(
        StatusCode::OK,
        choose_cover(&server, &entry, serde_json::json!({"coverPage": 2})).await
    );
    let kept = converted_on_import(&server);

    let (status, _) = server
        .send(
            request("DELETE", &format!("/entries/{entry}/cover"), IMPORTER)
                .body(Body::empty())
                .unwrap(),
        )
        .await;

    assert!(status.is_success(), "{status}");
    assert_ne!(
        kept,
        size_on_disk(&server),
        "the page was forgotten in the volume"
    );
    assert_eq!(size_on_disk(&server), kept_size(&server));
}

// --------------------------------------------------------- the size of an announcement

/// A whole library announced at once is thousands of paths and every declaration's text,
/// and axum's own ceiling of 2 MB refused it with a 413 before anything read it. The route
/// takes 32 MiB, said in the contract.
#[tokio::test]
async fn an_announcement_of_three_megabytes_is_read() {
    let server = Server::new();
    let summary = "x".repeat(3 * 1024 * 1024);

    let (status, _) = server
        .send(
            request("POST", "/import", IMPORTER)
                .header("content-type", "application/json")
                .body(json_body(serde_json::json!({
                    "root": "Bleach",
                    "files": [],
                    "sidecars": [{
                        "path": "work.json",
                        "json": serde_json::json!({"title": "Bleach", "summary": summary})
                            .to_string(),
                    }],
                })))
                .unwrap(),
        )
        .await;

    assert_eq!(StatusCode::OK, status);
}

/// And past 32 MiB it is refused unread, which is what a ceiling is for.
#[tokio::test]
async fn an_announcement_past_thirty_two_mebibytes_is_too_large() {
    let server = Server::new();
    let body = vec![b' '; 32 * 1024 * 1024 + 1];

    let (status, _) = server
        .send(
            request("POST", "/import", IMPORTER)
                .header("content-type", "application/json")
                .body(Body::from(body))
                .unwrap(),
        )
        .await;

    assert_eq!(StatusCode::PAYLOAD_TOO_LARGE, status);
}

// ------------------------------------------------------------------ credits of a volume

/// Lucky Luke with one volume, credited by its series: Goscinny wrote, Morris drew.
fn one_inheriting_volume(server: &Server) -> String {
    let folder = server.library().join("Lucky Luke");
    std::fs::create_dir_all(&folder).unwrap();
    std::fs::write(
        folder.join("work.json"),
        r#"{"leaf":1,"title":"Lucky Luke","authors":["René Goscinny"],"artists":["Morris"]}"#,
    )
    .unwrap();
    std::fs::write(
        folder.join("Tome 1.cbz"),
        archive_bytes(Some(&EntryJson {
            leaf: Some(1),
            number: Some(1.0),
            ..Default::default()
        })),
    )
    .unwrap();
    server.scan();
    server.entry()
}

fn patch_of(id: &str, body: serde_json::Value) -> Request<Body> {
    request("PATCH", &format!("/entries/{id}"), IMPORTER)
        .header("content-type", "application/json")
        .body(json_body(body))
        .unwrap()
}

/// A volume whose script changed hands had no way to say so but rewriting its archive by
/// hand: the route wrote every field of `entry.json` except who made the volume.
#[tokio::test]
async fn patching_a_volume_s_writer_writes_it_into_its_entry_json() {
    let server = Server::new();
    let id = one_inheriting_volume(&server);

    let (status, body) = server
        .send(patch_of(&id, serde_json::json!({"authors": ["Jul"]})))
        .await;
    assert_eq!(StatusCode::OK, status, "{body}");
    assert_eq!(body["authors"], serde_json::json!(["Jul"]), "{body}");
    assert_eq!(body["authorsInherited"], false);
    // The drawer was not asked about, and stays the series' word.
    assert_eq!(body["artists"], serde_json::json!(["Morris"]));
    assert_eq!(body["artistsInherited"], true);

    let written =
        leaf_server::api::records::read_entry_json(&server.library().join("Lucky Luke/Tome 1.cbz"))
            .expect("an entry.json");
    assert_eq!(vec!["Jul".to_string()], written.authors);
    assert!(written.artists.is_empty(), "nothing written for the drawer");
}

/// An empty list is « this volume says nothing »: the key goes, and the series' default
/// speaks for it again.
#[tokio::test]
async fn patching_a_volume_s_writer_with_nothing_gives_it_back_to_its_series() {
    let server = Server::new();
    let id = one_inheriting_volume(&server);
    server
        .send(patch_of(&id, serde_json::json!({"authors": ["Jul"]})))
        .await;

    let (status, body) = server
        .send(patch_of(&id, serde_json::json!({"authors": []})))
        .await;
    assert_eq!(StatusCode::OK, status, "{body}");
    assert_eq!(
        body["authors"],
        serde_json::json!(["René Goscinny"]),
        "{body}"
    );
    assert_eq!(body["authorsInherited"], true);
    let written =
        leaf_server::api::records::read_entry_json(&server.library().join("Lucky Luke/Tome 1.cbz"))
            .expect("an entry.json");
    assert!(written.authors.is_empty());
}

// ------------------------------------------------- a name that changes renames the files

fn stopped_at(server: &Server, page: i64) {
    server
        .db
        .write(|cx| {
            cx.execute(
                "INSERT INTO progress (entry_id, edition_id, page, finished, updated_at)
                 SELECT id, edition_id, ?1, 0, 1 FROM entry",
                [page],
            )?;
            Ok(())
        })
        .unwrap();
}

async fn patch(
    server: &Server,
    uri: &str,
    body: serde_json::Value,
) -> (StatusCode, serde_json::Value) {
    server
        .send(
            request("PATCH", uri, IMPORTER)
                .header("content-type", "application/json")
                .body(json_body(body))
                .unwrap(),
        )
        .await
}

/// A series retitled is a series whose folder and files carry the old title. They are
/// renamed with it, and the volume read to page 42 is still read to page 42: the identity of
/// a volume is not its name.
#[tokio::test]
async fn renaming_a_series_renames_its_files_and_keeps_the_page() {
    let server = Server::new();
    a_volume(&server);
    stopped_at(&server, 42);
    let (series, entry) = (server.series(), server.entry());

    let (status, body) = patch(
        &server,
        &format!("/series/{series}"),
        serde_json::json!({"title": "Bleach Deluxe"}),
    )
    .await;

    assert_eq!(StatusCode::OK, status, "{body}");
    assert!(server
        .library()
        .join("Bleach Deluxe/bleach-deluxe_tome_001.cbz")
        .is_file());
    assert!(!server.library().join("Bleach").exists());
    assert_eq!(entry, server.entry(), "the same volume");
    let page: i64 = server
        .db
        .read(|cx| cx.query_one("SELECT page FROM progress", [], |r| r.get(0)))
        .unwrap()
        .expect("a position");
    assert_eq!(42, page);
}

#[tokio::test]
async fn patching_a_volume_number_renames_its_file() {
    let server = Server::new();
    a_volume(&server);
    stopped_at(&server, 7);
    let entry = server.entry();

    let (status, body) = patch(
        &server,
        &format!("/entries/{entry}"),
        serde_json::json!({"number": 7}),
    )
    .await;

    assert_eq!(StatusCode::OK, status, "{body}");
    assert!(server
        .library()
        .join("Bleach/bleach_tome_007.cbz")
        .is_file());
    assert!(!server.library().join("Bleach/Tome 1.cbz").exists());
    assert_eq!(entry, server.entry());
}

/// A file that is not the library's is where a volume would go. Nothing is written over it,
/// the answer names it, and the title has not changed either: a refusal leaves the disk as
/// it was, sidecar included.
#[tokio::test]
async fn a_name_that_collides_is_a_409_that_names_the_files() {
    let server = Server::new();
    a_volume(&server);
    std::fs::write(
        server.library().join("Bleach/bleach-deluxe_tome_001.cbz"),
        "somebody else's",
    )
    .unwrap();
    let series = server.series();

    let (status, body) = patch(
        &server,
        &format!("/series/{series}"),
        serde_json::json!({"title": "Bleach Deluxe"}),
    )
    .await;

    assert_eq!(StatusCode::CONFLICT, status, "{body}");
    assert!(
        body["error"]
            .as_str()
            .unwrap_or_default()
            .contains("bleach-deluxe_tome_001.cbz"),
        "{body}"
    );
    assert!(server.library().join("Bleach/Tome 1.cbz").is_file());
    assert_eq!(
        "somebody else's",
        std::fs::read_to_string(server.library().join("Bleach/bleach-deluxe_tome_001.cbz"))
            .unwrap()
    );
    let sidecar =
        std::fs::read_to_string(server.library().join("Bleach/work.json")).unwrap_or_default();
    assert!(!sidecar.contains("Deluxe"), "{sidecar}");
}

#[tokio::test]
async fn a_read_only_library_refuses_a_rename_and_writes_nothing() {
    let server = Server::new();
    a_volume(&server);
    let series = server.series();
    common::read_only(&server.library().join("Bleach"));
    common::read_only(&server.library());

    let (status, body) = patch(
        &server,
        &format!("/series/{series}"),
        serde_json::json!({"title": "Bleach Deluxe"}),
    )
    .await;
    common::writable(&server.library());
    common::writable(&server.library().join("Bleach"));

    assert_eq!(StatusCode::CONFLICT, status, "{body}");
    assert!(
        body["error"]
            .as_str()
            .unwrap_or_default()
            .contains("read-only"),
        "{body}"
    );
    assert!(server.library().join("Bleach/Tome 1.cbz").is_file());
    // The scan stamped an identity into `work.json` long before; what the refusal must not
    // do is put the title in it.
    let sidecar =
        std::fs::read_to_string(server.library().join("Bleach/work.json")).unwrap_or_default();
    assert!(!sidecar.contains("Deluxe"), "{sidecar}");
}
