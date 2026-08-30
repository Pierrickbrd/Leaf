//! Every read route, through the real router.
//!
//! `read.rs` seeds the index and asks the repository; that is the right shape for a question
//! about SQL. It leaves half of `routes.rs` unvisited all the same — the extractor, the
//! guard, the status a missing id comes back as, the shape that crosses the wire. None of
//! those live in the repository, and none of them fail loudly when they break.

use axum::body::Body;
use axum::http::StatusCode;

mod common;
use common::{a_named_edition, a_volume, archive_bytes, request, Server, IMPORTER, READ_ONLY};

async fn get(server: &Server, uri: &str) -> (StatusCode, serde_json::Value) {
    server
        .send(
            request("GET", uri, READ_ONLY)
                .body(Body::empty())
                .unwrap(),
        )
        .await
}

/// The same, with the key that carries the import right: the import and intake listings
/// are the write side, and a read-only key has no business seeing what is in flight.
async fn as_importer(server: &Server, uri: &str) -> (StatusCode, serde_json::Value) {
    server
        .send(request("GET", uri, IMPORTER).body(Body::empty()).unwrap())
        .await
}

/// A library with one volume in it, scanned, and the two ids the routes take.
async fn a_library() -> (Server, String, String) {
    let server = Server::new();
    a_volume(&server);
    let series = server.series();
    let entry = server.entry();
    (server, series, entry)
}

// ------------------------------------------------------------------ the shelf

#[tokio::test]
async fn health_answers_without_a_key_and_says_what_it_is() {
    let server = Server::new();
    let (status, body) = server
        .send(
            axum::http::Request::builder()
                .uri("/health")
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(status, StatusCode::OK);
    assert_eq!(body["status"], "ok");
    // What a client checks on connecting, so an old build says "update the application"
    // instead of failing obscurely three screens later.
    assert!(body["api"].is_number(), "{body}");
    assert!(body["format"].is_number(), "{body}");
    assert!(body["library"].is_number(), "{body}");
}

#[tokio::test]
async fn the_shelf_is_a_page_that_says_how_many_match() {
    let (server, _, _) = a_library().await;
    let (status, body) = get(&server, "/series").await;
    assert_eq!(status, StatusCode::OK);
    assert_eq!(body["total"], 1);
    assert!(body["items"].is_array());
    assert_eq!(body["items"][0]["name"], "Bleach");
}

#[tokio::test]
async fn the_filters_are_the_values_worth_offering() {
    let (server, _, _) = a_library().await;
    let (status, body) = get(&server, "/filters").await;
    assert_eq!(status, StatusCode::OK);
    assert!(body.is_object(), "{body}");
}

#[tokio::test]
async fn the_format_says_what_the_files_on_disk_are_written_for() {
    let (server, _, _) = a_library().await;
    let (status, body) = get(&server, "/format").await;
    assert_eq!(status, StatusCode::OK);
    assert!(body.is_object(), "{body}");
}

#[tokio::test]
async fn one_series_and_the_lists_hanging_off_it() {
    let (server, series, _) = a_library().await;

    let (status, body) = get(&server, &format!("/series/{series}")).await;
    assert_eq!(status, StatusCode::OK);
    assert_eq!(body["id"], series);

    for tail in ["entries", "chapters", "arcs"] {
        let (status, body) = get(&server, &format!("/series/{series}/{tail}")).await;
        assert_eq!(status, StatusCode::OK, "{tail}");
        assert!(body.is_array(), "{tail}: {body}");
    }
}

#[tokio::test]
async fn a_series_that_is_not_there_is_a_404_and_not_an_empty_one() {
    let (server, _, _) = a_library().await;
    let (status, body) = get(&server, "/series/nope").await;
    assert_eq!(status, StatusCode::NOT_FOUND);
    assert!(body["error"].is_string() || body["message"].is_string(), "{body}");
}

// ----------------------------------------------------------------- the entries

#[tokio::test]
async fn one_entry_and_what_is_inside_it() {
    let (server, _, entry) = a_library().await;

    let (status, body) = get(&server, &format!("/entries/{entry}")).await;
    assert_eq!(status, StatusCode::OK);
    assert_eq!(body["id"], entry);

    for tail in ["chapters", "pages"] {
        let (status, body) = get(&server, &format!("/entries/{entry}/{tail}")).await;
        assert_eq!(status, StatusCode::OK, "{tail}");
        assert!(body.is_array(), "{tail}: {body}");
    }
}

#[tokio::test]
async fn an_entry_that_is_not_there_is_a_404() {
    let (server, _, _) = a_library().await;
    assert_eq!(get(&server, "/entries/nope").await.0, StatusCode::NOT_FOUND);
}

#[tokio::test]
async fn asking_for_a_width_prepares_the_first_page_without_changing_the_answer() {
    // The width is a hint for the cache, not a filter: the list is the same list.
    let (server, _, entry) = a_library().await;
    let plain = get(&server, &format!("/entries/{entry}/pages")).await;
    let hinted = get(&server, &format!("/entries/{entry}/pages?width=600")).await;
    assert_eq!(plain.0, StatusCode::OK);
    assert_eq!(plain.1, hinted.1);
}

// -------------------------------------------------------------------- reading

#[tokio::test]
async fn a_page_comes_back_as_bytes_and_a_number_past_the_end_does_not() {
    let (server, _, entry) = a_library().await;
    let response = server
        .send(
            request("GET", &format!("/entries/{entry}/pages/0"), READ_ONLY)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(response.0, StatusCode::OK);

    let past = get(&server, &format!("/entries/{entry}/pages/900")).await;
    assert_eq!(past.0, StatusCode::NOT_FOUND);
}

#[tokio::test]
async fn a_cover_is_served_for_an_entry_and_for_a_series() {
    let (server, series, entry) = a_library().await;
    for uri in [
        format!("/entries/{entry}/cover"),
        format!("/series/{series}/cover"),
        format!("/series/{series}/cover?width=200"),
    ] {
        let (status, _) = get(&server, &uri).await;
        assert_eq!(status, StatusCode::OK, "{uri}");
    }
}

#[tokio::test]
async fn a_cover_for_something_that_is_not_there_is_a_404() {
    let (server, _, _) = a_library().await;
    assert_eq!(get(&server, "/entries/nope/cover").await.0, StatusCode::NOT_FOUND);
    assert_eq!(get(&server, "/series/nope/cover").await.0, StatusCode::NOT_FOUND);
}

#[tokio::test]
async fn the_original_file_comes_back_stamped_with_its_identity() {
    let (server, _, entry) = a_library().await;
    let (status, _) = get(&server, &format!("/entries/{entry}/file")).await;
    assert_eq!(status, StatusCode::OK);
    assert_eq!(get(&server, "/entries/nope/file").await.0, StatusCode::NOT_FOUND);
}

// ------------------------------------------------------------------- searching

#[tokio::test]
async fn search_ranks_and_takes_a_kind() {
    let (server, _, _) = a_library().await;
    let (status, body) = get(&server, "/search?q=ble").await;
    assert_eq!(status, StatusCode::OK);
    assert!(body.is_array(), "{body}");

    let (status, body) = get(&server, "/search?q=ble&kind=SERIES&limit=1").await;
    assert_eq!(status, StatusCode::OK);
    assert!(body.as_array().unwrap().len() <= 1, "{body}");
}

#[tokio::test]
async fn a_search_for_nothing_is_answered_rather_than_refused() {
    let (server, _, _) = a_library().await;
    let (status, body) = get(&server, "/search?q=zzzzzzzz").await;
    assert_eq!(status, StatusCode::OK);
    assert_eq!(body.as_array().map(Vec::len), Some(0), "{body}");
}

// ------------------------------------------------------------------- progress

#[tokio::test]
async fn up_next_is_answered_on_a_library_nobody_has_read_yet() {
    let (server, _, _) = a_library().await;
    let (status, _) = get(&server, "/next").await;
    assert_eq!(status, StatusCode::OK);
}

#[tokio::test]
async fn progress_is_recorded_read_back_and_forgotten() {
    let (server, series, entry) = a_library().await;

    let (status, recorded) = server
        .send(
            request("PATCH", &format!("/entries/{entry}/progress"), IMPORTER)
                .header("Content-Type", "application/json")
                .body(Body::from(r#"{"page":0}"#))
                .unwrap(),
        )
        .await;
    assert_eq!(status, StatusCode::OK);
    assert_eq!(recorded["entryId"], entry);

    let (status, body) = get(&server, &format!("/entries/{entry}/progress")).await;
    assert_eq!(status, StatusCode::OK);
    assert_eq!(body["page"], 0);
    assert_eq!(body["pageCount"], 1);
    // Being *on* the last page is not having finished it. Done is a thing the client says.
    assert_eq!(body["finished"], false, "{body}");

    let (status, done) = server
        .send(
            request("PATCH", &format!("/entries/{entry}/progress"), IMPORTER)
                .header("Content-Type", "application/json")
                .body(Body::from(r#"{"finished":true}"#))
                .unwrap(),
        )
        .await;
    assert_eq!(status, StatusCode::OK);
    assert_eq!(done["finished"], true, "{done}");

    // A series knows what has been read of it, which is what the shelf draws.
    let (status, _) = get(&server, &format!("/series/{series}/progress")).await;
    assert_eq!(status, StatusCode::OK);

    let (status, _) = server
        .send(
            request("DELETE", &format!("/entries/{entry}/progress"), IMPORTER)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert!(status.is_success(), "{status}");
}

#[tokio::test]
async fn progress_on_something_that_is_not_there_is_a_404() {
    let (server, _, _) = a_library().await;
    let (status, _) = server
        .send(
            request("PATCH", "/entries/nope/progress", IMPORTER)
                .header("Content-Type", "application/json")
                .body(Body::from(r#"{"page":1}"#))
                .unwrap(),
        )
        .await;
    assert_eq!(status, StatusCode::NOT_FOUND);
}

// ----------------------------------------------------------------- the scanner

#[tokio::test]
async fn the_scan_says_where_it_has_got_to_and_can_be_started() {
    let (server, _, _) = a_library().await;

    let (status, body) = get(&server, "/scan").await;
    assert_eq!(status, StatusCode::OK);
    assert!(body.is_object(), "{body}");

    let (status, _) = server
        .send(
            request("POST", "/scan", IMPORTER)
                .header("Content-Type", "application/json")
                .body(Body::from("{}"))
                .unwrap(),
        )
        .await;
    assert!(status.is_success(), "{status}");
}

#[tokio::test]
async fn starting_a_scan_needs_the_import_right() {
    let (server, _, _) = a_library().await;
    let (status, _) = server
        .send(
            request("POST", "/scan", READ_ONLY)
                .header("Content-Type", "application/json")
                .body(Body::from("{}"))
                .unwrap(),
        )
        .await;
    assert_eq!(status, StatusCode::FORBIDDEN);
}

// -------------------------------------------------------------------- the door

#[tokio::test]
async fn every_read_route_refuses_a_key_it_does_not_know() {
    let (server, series, entry) = a_library().await;
    for uri in [
        "/series".to_string(),
        "/filters".to_string(),
        "/search?q=a".to_string(),
        "/next".to_string(),
        format!("/series/{series}"),
        format!("/entries/{entry}"),
    ] {
        let (status, _) = server
            .send(
                request("GET", &uri, "0000000000000000")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await;
        // 403, as the contract declares: the key was read and refused, not missing.
        assert_eq!(status, StatusCode::FORBIDDEN, "{uri}");
    }
}

// ------------------------------------------------------------------- the edits

async fn patch(
    server: &Server,
    uri: &str,
    body: serde_json::Value,
) -> (StatusCode, serde_json::Value) {
    server
        .send(
            request("PATCH", uri, IMPORTER)
                .header("Content-Type", "application/json")
                .body(Body::from(serde_json::to_vec(&body).unwrap()))
                .unwrap(),
        )
        .await
}

#[tokio::test]
async fn an_entry_takes_an_edit_and_answers_with_what_it_now_is() {
    let (server, _, entry) = a_library().await;
    let (status, body) = patch(
        &server,
        &format!("/entries/{entry}"),
        serde_json::json!({"title": "Un titre"}),
    )
    .await;
    assert_eq!(status, StatusCode::OK);
    assert_eq!(body["title"], "Un titre");
}

#[tokio::test]
async fn the_arcs_of_a_series_are_replaced_whole() {
    // A range of chapters, never a list of volumes: an arc that covers volumes 1–4 and 4–6
    // would count volume 4 twice and put the boundary tens of pages out.
    let (server, series, _) = a_library().await;
    let (status, body) = patch(
        &server,
        &format!("/series/{series}/arcs"),
        serde_json::json!([{"name": "Un cycle", "unit": "CHAPTER", "from": 1, "to": 2}]),
    )
    .await;
    assert_eq!(status, StatusCode::OK, "{body}");

    let (status, listed) = get(&server, &format!("/series/{series}/arcs")).await;
    assert_eq!(status, StatusCode::OK);
    assert_eq!(listed[0]["name"], "Un cycle");
    assert_eq!(listed[0]["unit"], "CHAPTER");
}

#[tokio::test]
async fn editing_something_that_is_not_there_is_a_404_on_every_route_that_takes_an_id() {
    let (server, _, _) = a_library().await;
    for (uri, body) in [
        ("/series/nope", serde_json::json!({"summary": "…"})),
        ("/entries/nope", serde_json::json!({"title": "…"})),
        ("/series/nope/arcs", serde_json::json!([])),
    ] {
        let (status, _) = patch(&server, uri, body).await;
        assert_eq!(status, StatusCode::NOT_FOUND, "{uri}");
    }
}

// ------------------------------------------------------------------ the intake

#[tokio::test]
async fn a_file_offered_is_described_before_it_is_filed() {
    let (server, series, _) = a_library().await;

    // Offered: the server says where it would go and how sure it is.
    let (status, proposal) = server
        .send(
            request("POST", "/entries", IMPORTER)
                .header("Content-Type", "application/octet-stream")
                .header("X-Leaf-Name", "Tome 2.cbz")
                .body(Body::from(archive_bytes(None)))
                .unwrap(),
        )
        .await;
    assert_eq!(status, StatusCode::OK, "{proposal}");
    let offered = proposal["received"].as_str().expect("an id").to_string();

    // Nothing is in the library until it is confirmed.
    let (status, waiting) = as_importer(&server, "/intake").await;
    assert_eq!(status, StatusCode::OK);
    assert!(waiting.is_array(), "{waiting}");

    // Confirmed: only then is it filed.
    let (status, filed) = server
        .send(
            request("POST", &format!("/intake/{offered}/file"), IMPORTER)
                .header("Content-Type", "application/json")
                .body(Body::from(
                    serde_json::json!({"seriesId": series}).to_string(),
                ))
                .unwrap(),
        )
        .await;
    assert!(status.is_success(), "{status} {filed}");
}

#[tokio::test]
async fn a_file_offered_and_then_thought_better_of_is_abandoned() {
    let (server, _, _) = a_library().await;
    let (_, proposal) = server
        .send(
            request("POST", "/entries", IMPORTER)
                .header("Content-Type", "application/octet-stream")
                .header("X-Leaf-Name", "Tome 3.cbz")
                .body(Body::from(archive_bytes(None)))
                .unwrap(),
        )
        .await;
    let offered = proposal["received"].as_str().expect("an id").to_string();

    let (status, _) = server
        .send(
            request("DELETE", &format!("/intake/{offered}"), IMPORTER)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert!(status.is_success(), "{status}");
}

// ------------------------------------------------------------------ the import

#[tokio::test]
async fn an_import_is_opened_looked_at_and_given_up_on() {
    let (server, _, _) = a_library().await;

    let (status, opened) = server
        .send(
            request("POST", "/import", IMPORTER)
                .header("Content-Type", "application/json")
                .body(Body::from(
                    serde_json::json!({
                        "root": "Essai",
                        "files": [{"path": "Tome 1.cbz", "size": 4}]
                    })
                    .to_string(),
                ))
                .unwrap(),
        )
        .await;
    assert_eq!(status, StatusCode::OK, "{opened}");
    let id = opened["id"].as_str().expect("an id").to_string();

    let (status, state) = as_importer(&server, &format!("/import/{id}")).await;
    assert_eq!(status, StatusCode::OK, "{state}");

    let (status, listed) = as_importer(&server, "/import").await;
    assert_eq!(status, StatusCode::OK);
    assert!(listed.is_array(), "{listed}");

    let (status, _) = server
        .send(
            request("DELETE", &format!("/import/{id}"), IMPORTER)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(status, StatusCode::NO_CONTENT);
}

#[tokio::test]
async fn cleanup_removes_what_it_is_named_and_nothing_it_is_not() {
    // Deletion on an explicit, by-name order, never inferred from a manifest.
    let (server, _, _) = a_library().await;
    let (status, body) = server
        .send(
            request("POST", "/cleanup", IMPORTER)
                .header("Content-Type", "application/json")
                .body(Body::from(
                    serde_json::json!({"root": "Bleach", "files": []}).to_string(),
                ))
                .unwrap(),
        )
        .await;
    assert_eq!(status, StatusCode::OK, "{body}");
    // A list of what went, not a count: the client shows the names.
    assert_eq!(body["removed"], serde_json::json!([]));
    // Naming nothing removed nothing: the volume is still there.
    assert!(server.library().join("Bleach/Tome 1.cbz").is_file());
}

// -------------------------------------------------------------------- the etag

#[tokio::test]
async fn a_cover_already_held_comes_back_as_not_modified() {
    let (server, _, entry) = a_library().await;
    let response = server
        .send(
            request("GET", &format!("/entries/{entry}/cover"), READ_ONLY)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(response.0, StatusCode::OK);

    let tag = server
        .tagged(&format!("/entries/{entry}/cover"))
        .await
        .expect("an ETag");
    let (status, _) = server
        .send(
            request("GET", &format!("/entries/{entry}/cover"), READ_ONLY)
                .header("If-None-Match", &tag)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(status, StatusCode::NOT_MODIFIED);
}

// --------------------------------------------------------------- what is refused

#[tokio::test]
async fn a_page_number_that_is_not_a_number_is_a_400_and_says_which() {
    let (server, _, entry) = a_library().await;
    let (status, body) = get(&server, &format!("/entries/{entry}/pages/deux")).await;
    assert_eq!(status, StatusCode::BAD_REQUEST);
    assert_eq!(body["error"], "page number expected");
}

#[tokio::test]
async fn a_filter_this_server_has_never_heard_of_is_not_an_error() {
    // A client from a later version may send one, and a shelf is the right answer.
    let (server, _, _) = a_library().await;
    let (status, body) = get(&server, "/series?author=Kubo&nonsense=42&medium=manga").await;
    assert_eq!(status, StatusCode::OK, "{body}");
    assert!(body["items"].is_array());
}

#[tokio::test]
async fn every_filter_the_shelf_has_reaches_the_query() {
    let (server, _, _) = a_library().await;
    for one in [
        "author=Kubo",
        "genre=Shonen",
        "medium=manga",
        "status=ongoing",
        "language=fr",
        "publisher=Kana",
        "universe=Nulle+part",
        "read=UNREAD",
    ] {
        let (status, body) = get(&server, &format!("/series?{one}")).await;
        assert_eq!(status, StatusCode::OK, "{one}: {body}");
        // Every one of them narrows, so a filter matching nothing empties the shelf rather
        // than being quietly ignored.
        assert!(body["total"].is_number(), "{one}: {body}");
    }
}

#[tokio::test]
async fn a_file_sent_against_an_import_nobody_opened_is_a_404() {
    let (server, _, _) = a_library().await;
    let (status, body) = server
        .send(
            request("PUT", "/import/imp_deadbeef/file?path=Tome+1.cbz&offset=0", IMPORTER)
                .header("Content-Type", "application/octet-stream")
                .body(Body::from(vec![0u8; 4]))
                .unwrap(),
        )
        .await;
    assert_eq!(status, StatusCode::NOT_FOUND, "{body}");
}

#[tokio::test]
async fn an_unforeseen_failure_answers_with_nothing_and_logs_the_rest() {
    // An error message can carry a path, a query, a piece of the schema. What crosses the
    // wire is "internal error"; the detail stays server-side.
    let server = Server::new();
    // The index is opened and then taken away, so the next read fails for a reason no
    // handler has a case for.
    a_volume(&server);
    let series = server.series();
    std::fs::remove_dir_all(server.library()).unwrap();
    let (status, body) = server
        .send(
            request("GET", &format!("/entries/{series}/file"), READ_ONLY)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert!(
        status == StatusCode::NOT_FOUND || status == StatusCode::INTERNAL_SERVER_ERROR,
        "{status} {body}"
    );
    if status == StatusCode::INTERNAL_SERVER_ERROR {
        assert_eq!(body["error"], "internal error");
    }
}

// -------------------------------------------------- the edition that has a folder

#[tokio::test]
async fn an_edition_with_a_folder_of_its_own_takes_its_edit_in_its_own_file() {
    // The other half of the model: when the edition is declared rather than implied, what a
    // patch changes belongs in edition.json and not in the work's file.
    let server = Server::new();
    a_named_edition(&server);
    let series = server.series();

    let (status, body) = patch(
        &server,
        &format!("/series/{series}"),
        serde_json::json!({"publisher": "Kana", "language": "fr"}),
    )
    .await;
    assert_eq!(status, StatusCode::OK, "{body}");

    let written = std::fs::read_to_string(
        server.library().join("Bleach/Perfect Edition/edition.json"),
    )
    .unwrap();
    assert!(written.contains("Kana"), "{written}");
    // And the work's own file is left alone: it says nothing about a printing.
    let work = std::fs::read_to_string(server.library().join("Bleach/work.json")).unwrap();
    assert!(!work.contains("Kana"), "{work}");
}

#[tokio::test]
async fn the_arcs_of_a_named_edition_are_written_beside_it() {
    let server = Server::new();
    a_named_edition(&server);
    let series = server.series();

    let (status, body) = patch(
        &server,
        &format!("/series/{series}/arcs"),
        serde_json::json!([{"name": "Soul Society", "unit": "CHAPTER", "from": 1, "to": 20}]),
    )
    .await;
    assert_eq!(status, StatusCode::OK, "{body}");

    let written = std::fs::read_to_string(
        server.library().join("Bleach/Perfect Edition/edition.json"),
    )
    .unwrap();
    assert!(written.contains("Soul Society"), "{written}");
}

#[tokio::test]
async fn a_file_bigger_than_the_ceiling_is_refused_before_it_reaches_the_disk() {
    let (server, _, _) = a_library().await;
    let (status, body) = server
        .send(
            request("POST", "/entries", IMPORTER)
                .header("Content-Type", "application/octet-stream")
                .header("X-Leaf-Name", "Enorme.cbz")
                .body(Body::from(vec![0u8; 8 * 1024]))
                .unwrap(),
        )
        .await;
    // 400, as the contract declares: the request is the caller's mistake, and the harness
    // sets the ceiling at four kilobytes.
    assert_eq!(status, StatusCode::BAD_REQUEST, "{body}");
    assert!(
        body["error"].as_str().unwrap_or_default().contains("larger than"),
        "{body}"
    );
}

// ------------------------------------------------------------- names and secrets

#[tokio::test]
async fn a_file_name_with_an_accent_in_it_is_a_name_and_not_a_missing_header() {
    // A library in French is mostly accented file names. Read as visible ASCII, "Été" made
    // the header vanish — and the server answered that no name had been given at all.
    let (server, _, _) = a_library().await;
    for name in ["Tome 1 — L'Été.cbz", "ハイキュー 01.cbz", "Атака 1.cbz"] {
        let (status, body) = server
            .send(
                request("POST", "/entries", IMPORTER)
                    .header("Content-Type", "application/octet-stream")
                    .header("X-Leaf-Name", name)
                    .body(Body::from(archive_bytes(None)))
                    .unwrap(),
            )
            .await;
        assert_eq!(status, StatusCode::OK, "{name}: {body}");
        assert_eq!(body["name"], name, "{body}");
    }
}

#[tokio::test]
async fn a_key_with_an_accent_in_it_is_recognised() {
    // `Keys::parse` measures a secret in characters, so it accepts one. Read as visible
    // ASCII at the door, it could never be presented — configurable and unusable.
    use leaf_server::api::keys::Keys;
    use leaf_server::api::routes::{router, AppState};
    use tower::ServiceExt;

    let server = Server::new();
    a_volume(&server);
    let accented = "clé-très-secrète-ici";
    let state = AppState::new(
        std::sync::Arc::clone(&server.db),
        Keys::parse(Some(&format!("desktop:{accented}:read"))).unwrap(),
    )
    .with_library(vec![server.library()], true);

    let response = router(state)
        .oneshot(
            request("GET", "/series", accented)
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();
    assert_eq!(response.status(), StatusCode::OK);
}
