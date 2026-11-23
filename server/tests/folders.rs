//! The folders a series sits in, with the sidecar of each: what an export carries besides the
//! volumes, so that a copy has the meaning of the library and not only its shape.

use axum::body::Body;
use axum::http::StatusCode;
use http_body_util::BodyExt;
use leaf_server::api::routes::router;
use leaf_server::metadata::sidecars::EntryJson;
use std::io::Write;
use tower::ServiceExt;

mod common;
use common::{a_volume, archive_bytes, request, Server, READ_ONLY};

async fn folders_of(server: &Server, series: &str) -> (StatusCode, serde_json::Value) {
    server
        .send(
            request("GET", &format!("/series/{series}/folders"), READ_ONLY)
                .body(Body::empty())
                .unwrap(),
        )
        .await
}

/// Dragon Ball and Terres d'Arran have the same shape on disk and opposite meanings; only the
/// sidecars say which is which. An export that left them behind would copy the shape.
#[tokio::test]
async fn a_series_in_a_universe_comes_with_its_universe_its_work_and_its_edition() {
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

    let (status, body) = folders_of(&server, &server.series()).await;

    assert_eq!(StatusCode::OK, status);
    let chain: Vec<(&str, &str, &str)> = body["folders"]
        .as_array()
        .unwrap()
        .iter()
        .map(|f| {
            (
                f["path"].as_str().unwrap(),
                f["level"].as_str().unwrap(),
                f["sidecar"].as_str().unwrap(),
            )
        })
        .collect();
    assert_eq!(
        vec![
            ("Mangas", "UNIVERSE", "universe.json"),
            ("Mangas/Bleach", "WORK", "work.json"),
            ("Mangas/Bleach/Perfect", "EDITION", "edition.json"),
        ],
        chain,
        "outermost first, the way they are nested on the disk"
    );
    let edition: serde_json::Value =
        serde_json::from_str(body["folders"][2]["content"].as_str().unwrap()).unwrap();
    assert_eq!(serde_json::json!("Perfect"), edition["name"]);
}

/// A work with one edition that has no folder of its own keeps its volumes in the work's: the
/// edition is not a level of the chain, and a copy must not invent a folder for it.
#[tokio::test]
async fn an_edition_with_no_folder_of_its_own_is_not_a_level() {
    let server = Server::new();
    a_volume(&server);

    let (status, body) = folders_of(&server, &server.series()).await;

    assert_eq!(StatusCode::OK, status);
    let levels: Vec<&str> = body["folders"]
        .as_array()
        .unwrap()
        .iter()
        .map(|f| f["level"].as_str().unwrap())
        .collect();
    assert_eq!(vec!["WORK"], levels);
    assert_eq!(serde_json::json!("Bleach"), body["folders"][0]["path"]);
}

#[tokio::test]
async fn the_folders_of_something_that_is_not_there_are_a_404() {
    let server = Server::new();

    let (status, _) = folders_of(&server, "nothing-like-it").await;

    assert_eq!(StatusCode::NOT_FOUND, status);
}

async fn file_of(
    server: &Server,
    series: &str,
    index: usize,
) -> (StatusCode, Option<String>, Vec<u8>) {
    let response = router(server.state())
        .oneshot(
            request("GET", &format!("/series/{series}/files/{index}"), READ_ONLY)
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .expect("a response");
    let status = response.status();
    let length = response
        .headers()
        .get(axum::http::header::CONTENT_LENGTH)
        .map(|v| v.to_str().unwrap().to_string());
    let bytes = response.into_body().collect().await.unwrap().to_bytes();
    (status, length, bytes.to_vec())
}

fn a_numbered_volume(path: &std::path::Path, work: &str, number: f64) {
    std::fs::write(
        path,
        archive_bytes(Some(&EntryJson {
            leaf: Some(1),
            work: Some(work.into()),
            number: Some(number),
            ..Default::default()
        })),
    )
    .unwrap();
}

fn paths(body: &serde_json::Value) -> Vec<(String, String, Option<String>)> {
    body["files"]
        .as_array()
        .unwrap()
        .iter()
        .map(|f| {
            (
                f["path"].as_str().unwrap().to_string(),
                f["kind"].as_str().unwrap().to_string(),
                f["entryId"].as_str().map(str::to_string),
            )
        })
        .collect()
}

/// A copy that left the images behind would come back from a re-scan with page zero as the
/// cover wherever somebody had chosen another; the volumes go first, in reading order, each
/// followed by its own image, and the series' own images close the list.
#[tokio::test]
async fn a_series_lists_its_volumes_then_their_images_then_its_own_images() {
    let server = Server::new();
    let folder = server.library().join("Bleach");
    std::fs::create_dir_all(&folder).unwrap();
    // Written out of reading order: the order of the list is the position, not the disk's.
    a_numbered_volume(&folder.join("Tome 2.cbz"), "Bleach", 2.0);
    a_numbered_volume(&folder.join("Tome 1.cbz"), "Bleach", 1.0);
    std::fs::write(folder.join("Tome 1.jpg"), b"one").unwrap();
    std::fs::write(folder.join("Tome 2.leaf-cover.png"), b"two!").unwrap();
    std::fs::write(folder.join("cover.jpg"), b"series").unwrap();
    std::fs::write(folder.join("notes.txt"), b"not a cover").unwrap();
    server.scan();

    let (status, body) = folders_of(&server, &server.series()).await;

    assert_eq!(StatusCode::OK, status);
    let listed = paths(&body);
    let shape: Vec<(&str, &str, bool)> = listed
        .iter()
        .map(|(p, k, e)| (p.as_str(), k.as_str(), e.is_some()))
        .collect();
    assert_eq!(
        vec![
            ("Bleach/Tome 1.cbz", "VOLUME", true),
            ("Bleach/Tome 1.jpg", "IMAGE", true),
            ("Bleach/Tome 2.cbz", "VOLUME", true),
            ("Bleach/Tome 2.leaf-cover.png", "IMAGE", true),
            ("Bleach/cover.jpg", "IMAGE", false),
        ],
        shape
    );
    assert_eq!(
        listed[0].2, listed[1].2,
        "the image carries its volume's id"
    );
    assert_eq!(serde_json::json!(3), body["files"][1]["size"]);
    let on_disk = std::fs::metadata(folder.join("Tome 1.cbz")).unwrap().len();
    assert_eq!(serde_json::json!(on_disk), body["files"][0]["size"]);
}

/// Its archive is its work and its edition at once, so the chain read `<Album.cbz>/work.json`
/// and answered 500 for every book in a universe.
#[tokio::test]
async fn a_one_shot_in_a_universe_has_the_universe_as_its_only_folder_and_its_archive_under_it() {
    let server = Server::new();
    let universe = server.library().join("Larcenet");
    std::fs::create_dir_all(&universe).unwrap();
    std::fs::write(
        universe.join("universe.json"),
        r#"{"leaf":1,"name":"Larcenet"}"#,
    )
    .unwrap();
    a_one_shot(&universe.join("Blast.cbz"));
    server.scan();

    let (status, body) = folders_of(&server, &server.series()).await;

    assert_eq!(StatusCode::OK, status);
    let levels: Vec<(&str, &str)> = body["folders"]
        .as_array()
        .unwrap()
        .iter()
        .map(|f| (f["path"].as_str().unwrap(), f["level"].as_str().unwrap()))
        .collect();
    assert_eq!(vec![("Larcenet", "UNIVERSE")], levels);
    assert_eq!(
        vec![("Larcenet/Blast.cbz".to_string(), "VOLUME".to_string())],
        paths(&body)
            .into_iter()
            .map(|(p, k, _)| (p, k))
            .collect::<Vec<_>>()
    );
}

#[tokio::test]
async fn a_one_shot_outside_any_universe_has_no_folder_and_sits_at_the_root() {
    let server = Server::new();
    // On a shelf, which is walked through and never recorded: nothing to copy but the book.
    std::fs::create_dir_all(server.library().join("BD")).unwrap();
    a_one_shot(&server.library().join("BD/Blast.cbz"));
    server.scan();

    let (status, body) = folders_of(&server, &server.series()).await;

    assert_eq!(StatusCode::OK, status);
    assert!(body["folders"].as_array().unwrap().is_empty());
    assert_eq!("Blast.cbz", body["files"][0]["path"]);
}

fn a_one_shot(path: &std::path::Path) {
    let mut zip = zip::ZipWriter::new(std::fs::File::create(path).unwrap());
    let options = zip::write::SimpleFileOptions::default();
    zip.start_file::<_, ()>("000.jpg", options).unwrap();
    zip.write_all(&common::jpeg()).unwrap();
    zip.start_file::<_, ()>("oneshot.json", options).unwrap();
    zip.write_all(br#"{"leaf":1,"title":"Blast"}"#).unwrap();
    zip.finish().unwrap();
}

/// A work with a folder of volumes of its own and a sub-folder that is another edition: the
/// implicit edition's images are the work folder's, and the other edition's belong to it.
#[tokio::test]
async fn an_implicit_edition_does_not_list_another_editions_files() {
    let server = Server::new();
    let work = server.library().join("Bleach");
    let other = work.join("Perfect");
    std::fs::create_dir_all(&other).unwrap();
    std::fs::write(work.join("work.json"), r#"{"leaf":1,"title":"Bleach"}"#).unwrap();
    a_numbered_volume(&work.join("Tome 1.cbz"), "Bleach", 1.0);
    a_numbered_volume(&other.join("Tome 1.cbz"), "Bleach", 1.0);
    std::fs::write(work.join("cover.jpg"), b"work").unwrap();
    std::fs::write(other.join("cover.jpg"), b"perfect").unwrap();
    server.scan();
    let implicit: String = server
        .db
        .read(|cx| {
            cx.query_one("SELECT id FROM edition WHERE implicit = 1", [], |r| {
                r.get(0)
            })
        })
        .unwrap()
        .expect("an implicit edition");

    let (status, body) = folders_of(&server, &implicit).await;

    assert_eq!(StatusCode::OK, status);
    let listed: Vec<String> = paths(&body).into_iter().map(|(p, _, _)| p).collect();
    assert_eq!(
        vec!["Bleach/Tome 1.cbz", "Bleach/cover.jpg"],
        listed,
        "nothing from Bleach/Perfect"
    );
}

#[tokio::test]
async fn a_file_of_the_list_is_served_with_its_bytes_and_its_length() {
    let server = Server::new();
    let folder = server.library().join("Bleach");
    std::fs::create_dir_all(&folder).unwrap();
    a_numbered_volume(&folder.join("Tome 1.cbz"), "Bleach", 1.0);
    std::fs::write(folder.join("cover.jpg"), b"series").unwrap();
    server.scan();
    let series = server.series();

    let (status, length, bytes) = file_of(&server, &series, 1).await;
    assert_eq!(StatusCode::OK, status);
    assert_eq!(Some("6".to_string()), length);
    assert_eq!(b"series".to_vec(), bytes);

    let (status, length, bytes) = file_of(&server, &series, 0).await;
    assert_eq!(StatusCode::OK, status);
    assert_eq!(Some(bytes.len().to_string()), length);
    assert_eq!(b"PK", &bytes[..2], "the archive, whole");
}

#[tokio::test]
async fn an_index_past_the_end_of_the_list_is_a_404() {
    let server = Server::new();
    a_volume(&server);
    let series = server.series();

    let (status, _, _) = file_of(&server, &series, 1).await;
    assert_eq!(StatusCode::NOT_FOUND, status);

    let (status, _, _) = file_of(&server, "nothing-like-it", 0).await;
    assert_eq!(StatusCode::NOT_FOUND, status);
}
