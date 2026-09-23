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
    assert!(server
        .library()
        .join("Terres d’Arran/Bleach/Tome 1.cbz")
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
    assert!(server.library().join("Bleach/Tome 1.cbz").exists());
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
        .join("Terres d’Arran/Bleach/Tome 1.cbz")
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
        .join("Terres d’Arran/Bleach/Tome 1.cbz")
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
