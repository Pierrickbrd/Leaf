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
        .send(request("GET", uri, READ_ONLY).body(Body::empty()).unwrap())
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

/// Both numbers come off the query string, and the offset is their product.
///
/// It overflowed: a panic in a debug build, taking the handler's task and the connection
/// with it, and in a release build a wrap to a negative offset that the repository clamped
/// back to zero — so a request for a page nothing could hold was answered, politely, with
/// the first one.
#[tokio::test]
async fn a_page_number_nothing_could_hold_is_an_empty_page_and_not_a_panic() {
    let (server, _, _) = a_library().await;
    let (status, body) = get(&server, "/series?page=9223372036854775807&size=500").await;

    assert_eq!(StatusCode::OK, status);
    assert_eq!(0, body["items"].as_array().expect("a list").len(), "{body}");
    assert_eq!(1, body["total"], "and it still says how many there are");
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
    assert!(
        body["error"].is_string() || body["message"].is_string(),
        "{body}"
    );
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
    assert_eq!(
        get(&server, "/entries/nope/cover").await.0,
        StatusCode::NOT_FOUND
    );
    assert_eq!(
        get(&server, "/series/nope/cover").await.0,
        StatusCode::NOT_FOUND
    );
}

#[tokio::test]
async fn the_original_file_comes_back_stamped_with_its_identity() {
    let (server, _, entry) = a_library().await;
    let (status, _) = get(&server, &format!("/entries/{entry}/file")).await;
    assert_eq!(status, StatusCode::OK);
    assert_eq!(
        get(&server, "/entries/nope/file").await.0,
        StatusCode::NOT_FOUND
    );
}

/// A row naming something that is not a file.
///
/// Opening a folder succeeds on Linux; reading one does not. Read whole, that failed before a
/// byte left and came back as a clean 500. Streamed, the answer had already gone out 200 with
/// the folder's own size as `Content-Length`, and the first read returned EISDIR — a
/// truncated download with no error document, which is the one shape a client cannot tell
/// from a connection that dropped.
#[tokio::test]
async fn an_entry_naming_a_folder_fails_before_it_answers_rather_than_mid_stream() {
    let server = Server::new();
    let folder = server.library().join("Bleach");
    std::fs::create_dir_all(&folder).unwrap();
    let file = folder.join("Tome 1.cbz");
    std::fs::write(&file, archive_bytes(None)).unwrap();
    server.scan();
    let entry = server.entry();

    // A volume unpacked in place, or a path a move rewrote. The row still names it.
    std::fs::remove_file(&file).unwrap();
    std::fs::create_dir(&file).unwrap();

    let (status, body) = get(&server, &format!("/entries/{entry}/file")).await;
    assert_eq!(StatusCode::INTERNAL_SERVER_ERROR, status, "{body}");
    assert_eq!(body["error"], "internal error");
}

/// And a row naming nothing at all, which is a different thing.
///
/// A volume taken off the disk between two scans is a thing that is not there — exactly what
/// the line above it in the same handler answers 404 for when the index has never heard of
/// the id. It came back as `internal error` instead: a client that says "the server is
/// broken" about a library that had merely moved a file, and an error-level line in the log
/// for something entirely routine.
#[tokio::test]
async fn an_entry_whose_file_has_gone_is_a_404_and_not_a_server_failure() {
    let server = Server::new();
    let folder = server.library().join("Bleach");
    std::fs::create_dir_all(&folder).unwrap();
    let file = folder.join("Tome 1.cbz");
    std::fs::write(&file, archive_bytes(None)).unwrap();
    server.scan();
    let entry = server.entry();

    // Deleted after the scan, so the index still holds the row that names it.
    std::fs::remove_file(&file).unwrap();

    let (status, body) = get(&server, &format!("/entries/{entry}/file")).await;
    assert_eq!(StatusCode::NOT_FOUND, status, "{body}");
    assert_ne!(body["error"], "internal error", "{body}");
}

/// The name of the file is the one thing about a download that a person sees afterwards.
///
/// An HTTP header value is Latin-1, so the UTF-8 of "Tome 1 — Été.cbz" written straight into
/// `filename="…"` is saved as "Tome 1 â€” Ã‰tÃ©.cbz"; a `"` in a name closes the quoted string
/// early and truncates the rest; a control byte makes the value unbuildable and turns the
/// download into a 500. A library in French is mostly accented file names.
#[tokio::test]
async fn a_downloaded_file_keeps_its_name_across_a_header_that_cannot_hold_it() {
    use http_body_util::BodyExt;
    use tower::ServiceExt;

    let server = Server::new();
    let folder = server.library().join("Bleach");
    std::fs::create_dir_all(&folder).unwrap();
    let file = folder.join("Tome 1 — L'\"Été\".cbz");
    std::fs::write(&file, archive_bytes(None)).unwrap();
    server.scan();
    let entry = server.entry();

    let response = leaf_server::api::routes::router(server.state())
        .oneshot(
            request("GET", &format!("/entries/{entry}/file"), READ_ONLY)
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .expect("a response");
    assert_eq!(StatusCode::OK, response.status());

    let said = response
        .headers()
        .get(axum::http::header::CONTENT_DISPOSITION)
        .expect("a disposition")
        .to_str()
        .expect("a header a client can read")
        .to_string();
    let length: u64 = response
        .headers()
        .get(axum::http::header::CONTENT_LENGTH)
        .expect("a length, or nothing can show how far along it is")
        .to_str()
        .unwrap()
        .parse()
        .unwrap();

    // The whole name, percent-encoded, which is what every browser reads first.
    assert!(
        said.contains("filename*=UTF-8''Tome%201%20%E2%80%94%20L%27%22%C3%89t%C3%A9%22.cbz"),
        "{said}"
    );
    // And a plain one behind it for whatever does not: nothing outside ASCII, and no quote
    // to end the string early.
    let plain = said
        .split("filename=\"")
        .nth(1)
        .and_then(|rest| rest.split('"').next())
        .expect("a plain name too");
    assert!(plain.is_ascii(), "{plain}");
    assert_eq!("Tome 1 _ L___t__.cbz", plain);

    // And the bytes themselves, streamed rather than held: what came back is the file, and
    // it said how big it was before it started.
    let bytes = response.into_body().collect().await.unwrap().to_bytes();
    let on_disk = std::fs::read(&file).unwrap();
    assert_eq!(on_disk.len() as u64, length);
    assert_eq!(on_disk, bytes);
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

/// The contract says CHAPTER or VOLUME and the index has a CHECK constraint saying the same.
/// Nothing between the two looked, so `"unit": "tome"` was written into edition.json with a
/// 200 — and from then on every scan of that shelf died on the insert, inside the
/// transaction that holds the whole shelf. The shelf stopped being indexed, and an
/// incomplete scan prunes nothing, so deletions anywhere else in the library went unseen.
#[tokio::test]
async fn an_arc_counted_in_something_that_is_not_a_unit_is_refused() {
    let (server, series, _) = a_library().await;
    let (status, body) = patch(
        &server,
        &format!("/series/{series}/arcs"),
        serde_json::json!([{"name": "Un cycle", "unit": "tome", "from": 1, "to": 2}]),
    )
    .await;

    assert_eq!(status, StatusCode::BAD_REQUEST, "{body}");
    assert!(
        body["error"].as_str().unwrap_or_default().contains("tome"),
        "the answer names the word it could not read: {body}"
    );

    // And nothing was written: the sidecar still holds no arc at all.
    let (status, listed) = get(&server, &format!("/series/{series}/arcs")).await;
    assert_eq!(status, StatusCode::OK);
    assert_eq!(0, listed.as_array().expect("a list").len(), "{listed}");
}

/// Case is not vocabulary, and the file ends up saying what the format says.
#[tokio::test]
async fn a_unit_spelled_by_a_person_is_the_same_unit() {
    let (server, series, _) = a_library().await;
    let (status, body) = patch(
        &server,
        &format!("/series/{series}/arcs"),
        serde_json::json!([{"name": "Un cycle", "unit": "volume", "from": 1, "to": 2}]),
    )
    .await;
    assert_eq!(status, StatusCode::OK, "{body}");

    let (_, listed) = get(&server, &format!("/series/{series}/arcs")).await;
    assert_eq!(listed[0]["unit"], "VOLUME");
}

#[tokio::test]
async fn editing_something_that_is_not_there_is_a_404_on_every_route_that_takes_an_id() {
    let (server, _, _) = a_library().await;
    for (uri, body) in [
        ("/series/nope", serde_json::json!({"summary": "…"})),
        ("/entries/nope", serde_json::json!({"title": "…"})),
        ("/series/nope/arcs", serde_json::json!([])),
        // With a body the route would refuse on its own merits. The id is what the answer
        // is about: reading the units first told a caller its shape had been read before its
        // id, which is a 400 where every other route here says 404 — and this loop only
        // passed because every body above happens to be one the route accepts.
        (
            "/series/nope/arcs",
            serde_json::json!([{"name": "Soul Society", "unit": "tome", "from": 1, "to": 20}]),
        ),
        (
            "/series/nope",
            serde_json::json!({"name": "Édition Deluxe"}),
        ),
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
            request(
                "PUT",
                "/import/imp_deadbeef/file?path=Tome+1.cbz&offset=0",
                IMPORTER,
            )
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

    let written =
        std::fs::read_to_string(server.library().join("Bleach/Perfect Edition/edition.json"))
            .unwrap();
    assert!(written.contains("Kana"), "{written}");
    // And the work's own file is left alone: it says nothing about a printing.
    let work = std::fs::read_to_string(server.library().join("Bleach/work.json")).unwrap();
    assert!(!work.contains("Kana"), "{work}");
}

/// A patch that reaches two files reaches both or neither.
///
/// `title` belongs to the work and `publisher` to the edition, and they were written in that
/// order: when reading the second failed — a mode, a symlink loop, a device error, the class
/// `merge` was taught to refuse rather than swallow — the first had already been rewritten.
/// A 500, the title on disk, the publisher not, and nothing in the answer saying which half
/// had landed. The guard on naming an implicit edition is the same rule for the one case that
/// had it.
#[tokio::test]
#[cfg(unix)]
async fn a_patch_that_cannot_finish_writes_nothing_at_all() {
    let server = Server::new();
    a_named_edition(&server);
    let series = server.series();

    let edition = server.library().join("Bleach/Perfect Edition/edition.json");
    std::fs::remove_file(&edition).unwrap();
    // A link to itself: the kernel answers ELOOP, which is neither "there" nor "not there".
    std::os::unix::fs::symlink("edition.json", &edition).unwrap();

    let (status, _) = patch(
        &server,
        &format!("/series/{series}"),
        serde_json::json!({"title": "Bleach — édition revue", "publisher": "Kana"}),
    )
    .await;
    assert_eq!(StatusCode::INTERNAL_SERVER_ERROR, status);

    let work = std::fs::read_to_string(server.library().join("Bleach/work.json")).unwrap();
    assert!(
        !work.contains("édition revue"),
        "the half that could be written must not have been: {work}"
    );
    assert!(
        std::fs::symlink_metadata(&edition).unwrap().is_symlink(),
        "and the file that could not be read is still there, untouched"
    );
}

/// An edition that is implied by its volumes has no file of its own to hold a name.
///
/// It was accepted anyway: `name` counts as touching the edition, so the implicit branch
/// ran, and that branch writes publisher, volume count, format, language and status — every
/// edition field except the one that was asked for. 200, the series unchanged, and nothing
/// saying the field had gone nowhere.
#[tokio::test]
async fn naming_an_edition_that_has_no_folder_is_refused_rather_than_ignored() {
    let (server, series, _) = a_library().await;
    let (status, body) = patch(
        &server,
        &format!("/series/{series}"),
        serde_json::json!({"name": "Édition Deluxe"}),
    )
    .await;

    assert_eq!(StatusCode::BAD_REQUEST, status, "{body}");
    assert!(
        body["error"]
            .as_str()
            .unwrap_or_default()
            .contains("no folder of its own"),
        "{body}"
    );
    assert!(
        !server.library().join("Bleach/work.json").exists(),
        "and nothing was written on the way to refusing"
    );
}

/// The other half: an edition that does have a folder takes the name in its own file.
#[tokio::test]
async fn naming_an_edition_that_has_a_folder_writes_it_there() {
    let server = Server::new();
    a_named_edition(&server);
    let series = server.series();

    let (status, body) = patch(
        &server,
        &format!("/series/{series}"),
        serde_json::json!({"name": "Édition Deluxe"}),
    )
    .await;
    assert_eq!(StatusCode::OK, status, "{body}");

    let written =
        std::fs::read_to_string(server.library().join("Bleach/Perfect Edition/edition.json"))
            .unwrap();
    assert!(written.contains("Édition Deluxe"), "{written}");
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

    let written =
        std::fs::read_to_string(server.library().join("Bleach/Perfect Edition/edition.json"))
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
        body["error"]
            .as_str()
            .unwrap_or_default()
            .contains("larger than"),
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

// ------------------------------------------------------------- the last corners

#[tokio::test]
async fn a_name_already_taken_comes_back_as_a_conflict_with_both_sides_of_it() {
    let (server, series, _) = a_library().await;
    let (_, offered) = server
        .send(
            request("POST", "/entries", IMPORTER)
                .header("Content-Type", "application/octet-stream")
                .header("X-Leaf-Name", "Tome 1.cbz")
                .body(Body::from(archive_bytes(None)))
                .unwrap(),
        )
        .await;
    let id = offered["received"].as_str().expect("an id").to_string();

    let (status, body) = server
        .send(
            request("POST", &format!("/intake/{id}/file"), IMPORTER)
                .header("Content-Type", "application/json")
                .body(Body::from(
                    serde_json::json!({"seriesId": series}).to_string(),
                ))
                .unwrap(),
        )
        .await;
    assert_eq!(status, StatusCode::CONFLICT, "{body}");
    // The question put to a person is about the volumes, not about the file names.
    assert!(body.is_object(), "{body}");
}

#[tokio::test]
async fn forgetting_progress_that_was_never_recorded_says_so_without_a_body() {
    let (server, _, entry) = a_library().await;
    let (status, _) = server
        .send(
            request("DELETE", &format!("/entries/{entry}/progress"), IMPORTER)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(status, StatusCode::NO_CONTENT);
}

#[tokio::test]
async fn asking_for_a_page_at_a_width_prepares_the_next_one() {
    // The reader is about to turn to it, and preparing it after the request is what makes
    // the second page arrive as fast as the first.
    let (server, _, entry) = a_library().await;
    let (status, _) = get(&server, &format!("/entries/{entry}/pages/0?width=400")).await;
    assert_eq!(status, StatusCode::OK);
}

#[tokio::test]
async fn a_second_scan_asked_for_while_one_is_running_does_not_start_another() {
    // One router, so the runner a scan is registered with survives between the calls.
    let (server, _, _) = a_library().await;
    let state = server.state();
    let mut answers = Vec::new();
    for _ in 0..4 {
        let (status, _) = server
            .send_to(
                state.clone(),
                request("POST", "/scan", IMPORTER)
                    .header("Content-Type", "application/json")
                    .body(Body::from("{}"))
                    .unwrap(),
            )
            .await;
        answers.push(status);
    }
    // Said rather than swallowed: a client that asked twice is told the second one did not
    // start, instead of being left to believe it did.
    assert!(answers.iter().any(StatusCode::is_success), "{answers:?}");
    assert!(
        answers.contains(&StatusCode::CONFLICT),
        "a scan asked for while one runs must say so: {answers:?}"
    );
    let (status, body) = server
        .send_to(
            state,
            request("GET", "/scan", READ_ONLY)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(status, StatusCode::OK, "{body}");
}

#[tokio::test]
async fn a_name_that_is_not_a_name_never_reaches_the_drop() {
    // The drop is a folder shared with an application on the same machine. What it takes is
    // a name: the path is taken off it first, and what is left has to still be one.
    let server = Server::new();
    a_volume(&server);
    let folder = server.dir.path().join("drop");
    std::fs::create_dir_all(&folder).unwrap();
    let mut server = server;
    server.drop = Some(folder);

    for name in ["", "   ", "..", "a/..", "..evade.cbz"] {
        let (status, body) = server
            .send(
                request("POST", "/drop", IMPORTER)
                    .header("Content-Type", "application/json")
                    .body(Body::from(serde_json::json!({"name": name}).to_string()))
                    .unwrap(),
            )
            .await;
        assert_eq!(status, StatusCode::BAD_REQUEST, "{name}: {body}");
    }
}

#[tokio::test]
async fn a_name_that_is_a_name_but_names_nothing_is_a_404() {
    let server = Server::new();
    a_volume(&server);
    let folder = server.dir.path().join("drop");
    std::fs::create_dir_all(&folder).unwrap();
    let mut server = server;
    server.drop = Some(folder);

    let (status, _) = server
        .send(
            request("POST", "/drop", IMPORTER)
                .header("Content-Type", "application/json")
                .body(Body::from(r#"{"name":"Tome 9.cbz"}"#))
                .unwrap(),
        )
        .await;
    assert_eq!(status, StatusCode::NOT_FOUND);
}

#[tokio::test]
async fn a_search_can_be_held_to_one_series() {
    // The hits are series, entries and chapters. Scoping them to one edition is what a
    // search inside a series is: the same index, a narrower question.
    let (server, series, _) = a_library().await;
    let (status, body) = get(&server, &format!("/search?q=a&series={series}")).await;
    assert_eq!(status, StatusCode::OK, "{body}");
    assert!(body.is_array(), "{body}");
}

#[tokio::test]
async fn a_volume_nobody_has_opened_answers_no_content_rather_than_an_empty_object() {
    // Never opened. Not an error, and not `{"page":0}` either, which a client would draw as
    // "you are on page one".
    let (server, _, entry) = a_library().await;
    let response = server
        .send(
            request("GET", &format!("/entries/{entry}/progress"), READ_ONLY)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(response.0, StatusCode::NO_CONTENT);
    assert_eq!(response.1, serde_json::Value::Null);
}

#[tokio::test]
async fn a_misspelled_search_inside_one_series_is_still_held_to_that_series() {
    // The approximate fallback reads what it compares, so what it reads has to stay bounded
    // by the shelf — and by the filter, when there is one.
    let (server, _, _) = a_library().await;
    let (status, body) = get(&server, "/search?q=Blaech&medium=manga").await;
    assert_eq!(status, StatusCode::OK, "{body}");
    assert!(body.is_array(), "{body}");
}

#[tokio::test]
async fn a_search_for_something_that_folds_to_nothing_finds_nothing() {
    // "!!!" is punctuation: it folds away entirely, and a needle that is empty matches
    // every title rather than none unless it is stopped here.
    let (server, _, _) = a_library().await;
    for q in ["!!!", "···", "%20"] {
        let (status, body) = get(&server, &format!("/search?q={q}")).await;
        assert_eq!(status, StatusCode::OK, "{q}: {body}");
        assert_eq!(body.as_array().map(Vec::len), Some(0), "{q}: {body}");
    }
}

#[tokio::test]
async fn a_series_takes_its_genres_whole_rather_than_one_at_a_time() {
    // A list replaces a list: adding "Action" and losing "Shonen" because the patch carried
    // one of them is the kind of edit nobody notices until the filter is wrong.
    let (server, series, _) = a_library().await;
    let (status, body) = patch(
        &server,
        &format!("/series/{series}"),
        serde_json::json!({"genres": ["Shonen", "Action"]}),
    )
    .await;
    assert_eq!(status, StatusCode::OK, "{body}");

    let (_, listed) = get(&server, &format!("/series/{series}")).await;
    let genres: Vec<String> = listed["genres"]
        .as_array()
        .map(|a| {
            a.iter()
                .filter_map(|g| g.as_str().map(str::to_string))
                .collect()
        })
        .unwrap_or_default();
    assert!(genres.contains(&"Action".to_string()), "{listed}");
    assert!(genres.contains(&"Shonen".to_string()), "{listed}");
}

#[tokio::test]
async fn an_edit_on_a_series_whose_folder_has_gone_is_a_404() {
    // The index still says where it is; the disk no longer agrees. What must not happen is
    // an edit written into a folder that is not there any more.
    let (server, series, entry) = a_library().await;
    std::fs::remove_dir_all(server.library().join("Bleach")).unwrap();

    for (uri, body) in [
        (
            format!("/series/{series}"),
            serde_json::json!({"summary": "…"}),
        ),
        (
            format!("/entries/{entry}"),
            serde_json::json!({"title": "…"}),
        ),
        (format!("/series/{series}/arcs"), serde_json::json!([])),
    ] {
        let (status, said) = patch(&server, &uri, body).await;
        assert_eq!(status, StatusCode::NOT_FOUND, "{uri}: {said}");
    }
}

#[tokio::test]
async fn a_chunk_bigger_than_what_is_left_of_the_ceiling_is_refused() {
    let (server, _, _) = a_library().await;
    let (_, opened) = server
        .send(
            request("POST", "/import", IMPORTER)
                .header("Content-Type", "application/json")
                .body(Body::from(
                    serde_json::json!({
                        "root": "Essai",
                        "files": [{"path": "Tome 1.cbz", "size": 99_999_999}]
                    })
                    .to_string(),
                ))
                .unwrap(),
        )
        .await;
    let id = opened["id"].as_str().expect("an id").to_string();

    let (status, body) = server
        .send(
            request(
                "PUT",
                &format!("/import/{id}/file?path=Tome+1.cbz&offset=0"),
                IMPORTER,
            )
            .header("Content-Type", "application/octet-stream")
            .body(Body::from(vec![0u8; 8 * 1024]))
            .unwrap(),
        )
        .await;
    // The harness sets the ceiling at four kilobytes.
    assert_eq!(status, StatusCode::BAD_REQUEST, "{body}");
}

#[tokio::test]
async fn an_index_that_cannot_be_read_answers_internal_error_and_says_no_more() {
    // An error message can carry a path, a query, a piece of the schema. What crosses the
    // wire is "internal error"; the detail is logged where only the machine's owner reads it.
    let server = Server::new();
    a_volume(&server);
    let index = server.dir.path().join("index.sqlite");

    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        // A fresh state, so the read has to open a connection it does not already hold.
        let state = server.state();
        std::fs::set_permissions(&index, std::fs::Permissions::from_mode(0o000)).unwrap();
        let (status, body) = server
            .send_to(
                state,
                request("GET", "/series", READ_ONLY)
                    .body(Body::empty())
                    .unwrap(),
            )
            .await;
        std::fs::set_permissions(&index, std::fs::Permissions::from_mode(0o644)).unwrap();

        if status == StatusCode::INTERNAL_SERVER_ERROR {
            assert_eq!(body["error"], "internal error");
            assert_eq!(body.as_object().map(|o| o.len()), Some(1), "{body}");
        }
    }
}

#[tokio::test]
async fn an_edit_on_a_series_the_scan_cannot_reach_falls_back_to_the_whole_library() {
    // A rescan is aimed at one work because reading the library after one edited field is a
    // minute on sixty series. When there is nothing to aim at, the sweep goes wide.
    let (server, _, entry) = a_library().await;
    let state = server.state();

    // Two edits in a row: the first starts the sweep, the second meets it already running
    // and says so rather than queueing behind it.
    for _ in 0..2 {
        let (status, _) = server
            .send_to(
                state.clone(),
                request("PATCH", &format!("/entries/{entry}"), IMPORTER)
                    .header("Content-Type", "application/json")
                    .body(Body::from(r#"{"title":"Un titre"}"#))
                    .unwrap(),
            )
            .await;
        assert_eq!(status, StatusCode::OK);
    }
}

#[tokio::test]
async fn an_upload_sent_in_pieces_is_written_as_it_arrives() {
    // The body is streamed rather than held: nine gigabytes must not be a nine-gigabyte
    // allocation, and an empty frame in the middle of it is not the end of the file.
    let (server, _, _) = a_library().await;
    let (_, opened) = server
        .send(
            request("POST", "/import", IMPORTER)
                .header("Content-Type", "application/json")
                .body(Body::from(
                    serde_json::json!({
                        "root": "Essai",
                        "files": [{"path": "Tome 1.cbz", "size": 6}]
                    })
                    .to_string(),
                ))
                .unwrap(),
        )
        .await;
    let id = opened["id"].as_str().expect("an id").to_string();

    let pieces: Vec<Result<axum::body::Bytes, std::io::Error>> = vec![
        Ok(axum::body::Bytes::from_static(b"abc")),
        Ok(axum::body::Bytes::new()),
        Ok(axum::body::Bytes::from_static(b"def")),
    ];
    let body = Body::from_stream(futures_util::stream::iter(pieces));
    let (status, said) = server
        .send(
            request(
                "PUT",
                &format!("/import/{id}/file?path=Tome+1.cbz&offset=0"),
                IMPORTER,
            )
            .header("Content-Type", "application/octet-stream")
            .body(body)
            .unwrap(),
        )
        .await;
    assert_eq!(status, StatusCode::OK, "{said}");
    assert_eq!(said["received"], 6);
}

#[tokio::test]
async fn a_volume_whose_archive_is_corrupt_answers_internal_error_and_says_no_more() {
    // The index says the page is there; the archive says otherwise. An error message can
    // carry a path, a query, a piece of the schema, so what crosses the wire is three words
    // and the rest goes to the log.
    let (server, _, entry) = a_library().await;
    let file: String = server
        .db
        .read(|cx| cx.query_one("SELECT file FROM entry", [], |r| r.get::<_, String>(0)))
        .unwrap()
        .expect("a file");
    std::fs::write(&file, b"not a zip at all, not any more").unwrap();

    let (status, body) = get(&server, &format!("/entries/{entry}/pages/0")).await;
    assert_eq!(status, StatusCode::INTERNAL_SERVER_ERROR, "{body}");
    assert_eq!(body["error"], "internal error");
    // Nothing else: no path, no query, no fragment of the schema.
    assert_eq!(body.as_object().map(|o| o.len()), Some(1), "{body}");
}

#[tokio::test]
async fn a_chunk_offered_at_the_wrong_offset_is_told_where_to_resume() {
    // A broken transfer must be told exactly where to start again, not asked to send nine
    // gigabytes a second time.
    let (server, _, _) = a_library().await;
    let (_, opened) = server
        .send(
            request("POST", "/import", IMPORTER)
                .header("Content-Type", "application/json")
                .body(Body::from(
                    serde_json::json!({
                        "root": "Essai",
                        "files": [{"path": "Tome 1.cbz", "size": 10}]
                    })
                    .to_string(),
                ))
                .unwrap(),
        )
        .await;
    let id = opened["id"].as_str().expect("an id").to_string();

    // Nothing has been received, so a range starting at five is not where this file is.
    // The offset is a Content-Range, the way a resumable upload spells it.
    let (status, body) = server
        .send(
            request(
                "PUT",
                &format!("/import/{id}/file?path=Tome+1.cbz"),
                IMPORTER,
            )
            .header("Content-Type", "application/octet-stream")
            .header("Content-Range", "bytes 5-6/10")
            .body(Body::from(vec![0u8; 2]))
            .unwrap(),
        )
        .await;
    assert_eq!(status, StatusCode::CONFLICT, "{body}");
    assert_eq!(body["received"], 0, "{body}");

    // "bytes */10" declares no start at all, so it is read as the beginning.
    let (status, body) = server
        .send(
            request(
                "PUT",
                &format!("/import/{id}/file?path=Tome+1.cbz"),
                IMPORTER,
            )
            .header("Content-Type", "application/octet-stream")
            .header("Content-Range", "bytes */10")
            .body(Body::from(vec![0u8; 2]))
            .unwrap(),
        )
        .await;
    assert_eq!(status, StatusCode::OK, "{body}");
    assert_eq!(body["received"], 2, "{body}");
}

#[tokio::test]
async fn a_chunk_for_a_file_the_manifest_never_mentioned_is_refused() {
    let (server, _, _) = a_library().await;
    let (_, opened) = server
        .send(
            request("POST", "/import", IMPORTER)
                .header("Content-Type", "application/json")
                .body(Body::from(
                    serde_json::json!({
                        "root": "Essai",
                        "files": [{"path": "Tome 1.cbz", "size": 2}]
                    })
                    .to_string(),
                ))
                .unwrap(),
        )
        .await;
    let id = opened["id"].as_str().expect("an id").to_string();

    let (status, body) = server
        .send(
            request(
                "PUT",
                &format!("/import/{id}/file?path=../evade.cbz"),
                IMPORTER,
            )
            .header("Content-Type", "application/octet-stream")
            .body(Body::from(vec![0u8; 2]))
            .unwrap(),
        )
        .await;
    assert_eq!(status, StatusCode::BAD_REQUEST, "{body}");
}
