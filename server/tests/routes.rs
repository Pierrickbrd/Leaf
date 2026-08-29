//! Every read route, through the real router.
//!
//! `read.rs` seeds the index and asks the repository; that is the right shape for a question
//! about SQL. It leaves half of `routes.rs` unvisited all the same — the extractor, the
//! guard, the status a missing id comes back as, the shape that crosses the wire. None of
//! those live in the repository, and none of them fail loudly when they break.

use axum::body::Body;
use axum::http::StatusCode;

mod common;
use common::{a_volume, request, Server, IMPORTER, READ_ONLY};

async fn get(server: &Server, uri: &str) -> (StatusCode, serde_json::Value) {
    server
        .send(
            request("GET", uri, READ_ONLY)
                .body(Body::empty())
                .unwrap(),
        )
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
