//! The routes that change something, answered through the real router.
//!
//! What these check is the guard and the shape: a write route without the import right must
//! be refused, an upload must reach the disk without being held whole in memory, and a
//! broken transfer must be told exactly where to resume.

use std::path::Path;

use axum::body::Body;
use axum::http::{Request, StatusCode};
use leaf_server::api::routes::{can_be_aimed_at, AppState};
use leaf_server::metadata::sidecars::EntryJson;

mod common;
use common::{a_volume, archive_bytes, json_body, request, Server, IMPORTER, READ_ONLY};

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
            "/import".to_string(),
            json_body(serde_json::json!({"root": "x", "files": []})),
        ),
        (
            "POST",
            "/cleanup".to_string(),
            json_body(serde_json::json!({"root": "x", "files": []})),
        ),
        ("DELETE", "/import/imp_1".to_string(), Body::empty()),
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
    assert!(server.library().join("Bleach/work.json").exists());
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
/// enums beside them: `{"status": "hiatus"}` was answered 200, written into work.json, and
/// served straight back out of `GET /series` against an enum of two words. `intake` then read
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
        ("status", "hiatus"),
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
    assert!(!written.contains("hiatus"), "{written}");

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
    // The same shape as a permission or a device error, and reachable without either.
    let sidecar = server.library().join("Bleach/work.json");
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
