//! The harness the route tests share: a real library on disk, a real index, a real router.
//!
//! One copy, because two would drift — and a fixture that drifts makes two test files
//! disagree about what the server does without either of them being wrong.

#![allow(dead_code)]

use std::io::Write;
use std::sync::Arc;

use axum::body::Body;
use axum::http::{Request, StatusCode};
use http_body_util::BodyExt;
use leaf_server::api::keys::Keys;
use leaf_server::api::routes::{router, AppState};
use leaf_server::metadata::sidecars::{self, EntryJson};
use leaf_server::scan::scanner::Scanner;
use leaf_server::store::Db;
use tower::ServiceExt;

pub const READ_ONLY: &str = "1111111111111111";
pub const IMPORTER: &str = "8f3a92c1d4e5b6a7";

pub fn keys() -> Keys {
    Keys::parse(Some(
        "phone:1111111111111111:read  desktop:8f3a92c1d4e5b6a7:read,import",
    ))
    .expect("keys")
}

pub fn jpeg() -> Vec<u8> {
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

pub fn archive_bytes(entry: Option<&EntryJson>) -> Vec<u8> {
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

pub struct Server {
    pub dir: tempfile::TempDir,
    pub db: Arc<Db>,
    pub drop: Option<std::path::PathBuf>,
    pub trust_proxy: bool,
}

/// Turns the server's own logging on, once per test binary.
///
/// Without a subscriber every `tracing::warn!` short-circuits before it evaluates its
/// arguments, so the lines that say what went wrong are never run — and the one thing a
/// test of an error path should be sure of is that the report of it works.
pub fn logging() {
    static ONCE: std::sync::Once = std::sync::Once::new();
    ONCE.call_once(|| {
        let _ = tracing_subscriber::fmt()
            .with_test_writer()
            .with_max_level(tracing::Level::DEBUG)
            .try_init();
    });
}

impl Server {
    pub fn new() -> Self {
        logging();
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

    pub fn library(&self) -> std::path::PathBuf {
        self.dir.path().join("library")
    }

    pub fn state(&self) -> AppState {
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

    pub fn scan(&self) {
        Scanner::new(Arc::clone(&self.db), true)
            .scan(&[self.library()])
            .expect("scanning");
    }

    pub fn series(&self) -> String {
        self.db
            .read(|cx| cx.query_one("SELECT id FROM edition", [], |r| r.get::<_, String>(0)))
            .unwrap()
            .expect("a series")
    }

    pub async fn send(&self, request: Request<Body>) -> (StatusCode, serde_json::Value) {
        self.send_to(self.state(), request).await
    }

    /// Through one router, so that state a test wants to survive between calls — the
    /// throttle, which counts — actually does.
    pub async fn send_to(
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

pub fn request(method: &str, uri: &str, key: &str) -> axum::http::request::Builder {
    Request::builder()
        .method(method)
        .uri(uri)
        .header("X-Leaf-Key", key)
}

pub fn json_body(value: serde_json::Value) -> Body {
    Body::from(serde_json::to_vec(&value).unwrap())
}

/// A work whose edition has a folder and a file of its own — the other half of the model,
/// where the edition is declared rather than implied by the volumes sitting beside work.json.
pub fn a_named_edition(server: &Server) {
    let folder = server.library().join("Bleach/Perfect Edition");
    std::fs::create_dir_all(&folder).unwrap();
    std::fs::write(
        server.library().join("Bleach/work.json"),
        br#"{"leaf":1,"title":"Bleach"}"#,
    )
    .unwrap();
    std::fs::write(
        folder.join("edition.json"),
        br#"{"leaf":1,"name":"Perfect Edition"}"#,
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
}

pub fn a_volume(server: &Server) {
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

impl Server {
    /// The ETag one route hands out, for the test that asks for it again.
    pub async fn tagged(&self, uri: &str) -> Option<String> {
        let response = router(self.state())
            .oneshot(request("GET", uri, READ_ONLY).body(Body::empty()).unwrap())
            .await
            .expect("a response");
        response
            .headers()
            .get(axum::http::header::ETAG)
            .and_then(|v| v.to_str().ok())
            .map(str::to_string)
    }

    /// One entry's id, for the routes that take one.
    pub fn entry(&self) -> String {
        self.db
            .read(|cx| cx.query_one("SELECT id FROM entry", [], |r| r.get::<_, String>(0)))
            .unwrap()
            .expect("an entry")
    }
}
