//! The routes that change something, answered through the real router.
//!
//! What these check is the guard and the shape: a write route without the import right must
//! be refused, an upload must reach the disk without being held whole in memory, and a
//! broken transfer must be told exactly where to resume.

use std::io::Write;
use std::path::Path;
use std::sync::Arc;

use axum::body::Body;
use axum::http::{Request, StatusCode};
use http_body_util::BodyExt;
use leaf_server::api::keys::Keys;
use leaf_server::api::routes::{can_be_aimed_at, router, AppState};
use leaf_server::metadata::sidecars::{self, EntryJson};
use leaf_server::scan::scanner::Scanner;
use leaf_server::store::Db;
use tower::ServiceExt;

const READ_ONLY: &str = "1111111111111111";
const IMPORTER: &str = "8f3a92c1d4e5b6a7";

fn keys() -> Keys {
    Keys::parse(Some(
        "phone:1111111111111111:read  desktop:8f3a92c1d4e5b6a7:read,import",
    ))
    .expect("keys")
}

fn jpeg() -> Vec<u8> {
    let mut buffer = image::RgbImage::new(60, 90);
    for (x, y, pixel) in buffer.enumerate_pixels_mut() {
        *pixel = image::Rgb([(x % 256) as u8, (y % 256) as u8, 128]);
    }
    let mut out = std::io::Cursor::new(Vec::new());
    image::DynamicImage::ImageRgb8(buffer)
        .write_to(&mut out, image::ImageFormat::Jpeg)
        .unwrap();
    out.into_inner()
}

fn archive_bytes(entry: Option<&EntryJson>) -> Vec<u8> {
    let mut zip = zip::ZipWriter::new(std::io::Cursor::new(Vec::new()));
    let options = zip::write::SimpleFileOptions::default();
    zip.start_file::<_, ()>("000.jpg", options).unwrap();
    zip.write_all(&jpeg()).unwrap();
    if let Some(entry) = entry {
        zip.start_file::<_, ()>("entry.json", options).unwrap();
        zip.write_all(&sidecars::write(entry).unwrap()).unwrap();
    }
    zip.finish().unwrap().into_inner()
}

struct Server {
    dir: tempfile::TempDir,
    db: Arc<Db>,
    drop: Option<std::path::PathBuf>,
    trust_proxy: bool,
}

impl Server {
    fn new() -> Self {
        let dir = tempfile::tempdir().unwrap();
        std::fs::create_dir_all(dir.path().join("library")).unwrap();
        std::fs::create_dir_all(dir.path().join("inbox")).unwrap();
        let db = Arc::new(Db::open(&dir.path().join("index.sqlite")).unwrap());
        Server {
            dir,
            db,
            drop: None,
            trust_proxy: false,
        }
    }

    fn library(&self) -> std::path::PathBuf {
        self.dir.path().join("library")
    }

    fn state(&self) -> AppState {
        AppState::new(Arc::clone(&self.db), keys())
            .with_library(vec![self.library()], true)
            .trusting_proxy(self.trust_proxy)
            .with_import(
                &self.dir.path().join("inbox"),
                &self.library(),
                self.drop.clone(),
                4 * 1024,
            )
    }

    fn scan(&self) {
        Scanner::new(Arc::clone(&self.db), true)
            .scan(&[self.library()])
            .expect("scanning");
    }

    fn series(&self) -> String {
        self.db
            .read(|cx| cx.query_one("SELECT id FROM edition", [], |r| r.get::<_, String>(0)))
            .unwrap()
            .expect("a series")
    }

    async fn send(&self, request: Request<Body>) -> (StatusCode, serde_json::Value) {
        self.send_to(self.state(), request).await
    }

    /// Through one router, so that state a test wants to survive between calls — the
    /// throttle, which counts — actually does.
    async fn send_to(
        &self,
        state: AppState,
        request: Request<Body>,
    ) -> (StatusCode, serde_json::Value) {
        let response = router(state).oneshot(request).await.expect("a response");
        let status = response.status();
        let bytes = response.into_body().collect().await.unwrap().to_bytes();
        let json = if bytes.is_empty() {
            serde_json::Value::Null
        } else {
            serde_json::from_slice(&bytes).unwrap_or(serde_json::Value::Null)
        };
        (status, json)
    }
}

fn request(method: &str, uri: &str, key: &str) -> axum::http::request::Builder {
    Request::builder()
        .method(method)
        .uri(uri)
        .header("X-Leaf-Key", key)
}

fn json_body(value: serde_json::Value) -> Body {
    Body::from(serde_json::to_vec(&value).unwrap())
}

fn a_volume(server: &Server) {
    let folder = server.library().join("Bleach");
    std::fs::create_dir_all(&folder).unwrap();
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

// -------------------------------------------------------------------- intake

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
    // A number, like every other count in this API. The Kotlin spelled it as text.
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
    // Skipped at its default, as kotlinx.serialization did with encodeDefaults = false.
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

#[tokio::test]
async fn the_leftmost_forwarded_address_is_the_client() {
    let mut server = Server::new();
    server.trust_proxy = true;
    let state = server.state();

    // "client, proxy1, proxy2" — the client is the one the first proxy wrote down.
    wrong_keys(&server, &state, Some("10.0.0.99, 172.17.0.1"), 12).await;

    let (blocked, _) = server
        .send_to(
            state.clone(),
            Request::builder()
                .uri("/series")
                .header("X-Leaf-Key", "0000000000000000")
                .header("X-Forwarded-For", "10.0.0.99, 172.17.0.5")
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(
        StatusCode::TOO_MANY_REQUESTS,
        blocked,
        "the same client through a different proxy is still that client"
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
