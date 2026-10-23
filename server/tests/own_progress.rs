//! Progress is a profile's own: two readers in the same volume each find their own place,
//! their own badges and their own « what next », and never the other's.
//!
//! Every read of `progress` used to ignore `profile_id`. Harmless while one profile read;
//! with two, a parent who had finished a series made it « read » on the child's shelf, the
//! child's « Non lues » filter hid it, and `/next` offered one volume twice.

mod common;

use std::io::Write;
use std::sync::Arc;

use axum::body::Body;
use axum::http::StatusCode;
use common::{jpeg, json_body, request, Server};
use leaf_server::metadata::sidecars::{self, EntryJson};
use leaf_server::store::profiles::{NewProfile, Profiles, Rights};

const NOW: i64 = 1_790_000_000_000;

/// A volume of `pages` pages: enough of them for a place in the middle to be a place.
fn volume(work: &str, number: f64, pages: usize) -> Vec<u8> {
    let mut zip = zip::ZipWriter::new(std::io::Cursor::new(Vec::new()));
    let options = zip::write::SimpleFileOptions::default();
    for page in 0..pages {
        zip.start_file::<_, ()>(format!("{page:03}.jpg"), options)
            .unwrap();
        zip.write_all(&jpeg()).unwrap();
    }
    zip.start_file::<_, ()>("entry.json", options).unwrap();
    zip.write_all(
        &sidecars::write(&EntryJson {
            leaf: Some(1),
            work: Some(work.into()),
            number: Some(number),
            ..Default::default()
        })
        .unwrap(),
    )
    .unwrap();
    zip.finish().unwrap().into_inner()
}

/// One series of two volumes of five pages, a principal's laptop and a reader's tablet.
struct Household {
    server: Server,
    series: String,
    first: String,
    parent: String,
    reader: String,
}

fn a_household() -> Household {
    let server = Server::new();
    let folder = server.library().join("Bleach");
    std::fs::create_dir_all(&folder).unwrap();
    for number in [1.0, 2.0] {
        std::fs::write(
            folder.join(format!("Tome {number}.cbz")),
            volume("Bleach", number, 5),
        )
        .unwrap();
    }
    server.scan();

    let profiles = Profiles::new(Arc::clone(&server.db));
    let ines = profiles
        .create(
            NewProfile {
                name: "Inès".into(),
                color: "bleu".into(),
                restricted: false,
                max_age: None,
                rights: Rights::default(),
            },
            NOW,
        )
        .unwrap()
        .unwrap();
    let parent = server.linked_device("Portable");
    let reader = server.linked_device("Tablette");
    let tablet: String = server
        .db
        .read(|cx| {
            cx.query_one("SELECT id FROM device WHERE name = 'Tablette'", [], |r| {
                r.get(0)
            })
        })
        .unwrap()
        .unwrap();
    profiles.place(&tablet, &ines.id, false).unwrap().unwrap();

    let series = server.series();
    let first: String = server
        .db
        .read(|cx| {
            cx.query_one("SELECT id FROM entry ORDER BY sort_key LIMIT 1", [], |r| {
                r.get(0)
            })
        })
        .unwrap()
        .unwrap();
    Household {
        server,
        series,
        first,
        parent,
        reader,
    }
}

impl Household {
    async fn get(&self, uri: &str, key: &str) -> (StatusCode, serde_json::Value) {
        self.server
            .send(request("GET", uri, key).body(Body::empty()).unwrap())
            .await
    }

    async fn patch(
        &self,
        uri: &str,
        key: &str,
        body: serde_json::Value,
    ) -> (StatusCode, serde_json::Value) {
        self.server
            .send(
                request("PATCH", uri, key)
                    .header("Content-Type", "application/json")
                    .body(json_body(body))
                    .unwrap(),
            )
            .await
    }
}

#[tokio::test]
async fn two_profiles_in_the_same_volume_each_read_their_own_place_and_status() {
    let home = a_household();
    let place = format!("/entries/{}/progress", home.first);

    // The parent finishes the whole series; the reader opens the first volume at page 1.
    let (status, body) = home
        .patch(
            &format!("/series/{}/progress", home.series),
            &home.parent,
            serde_json::json!({"finished": true}),
        )
        .await;
    assert_eq!(StatusCode::OK, status, "{body}");
    let (status, body) = home
        .patch(&place, &home.reader, serde_json::json!({"page": 1}))
        .await;
    assert_eq!(StatusCode::OK, status, "{body}");

    let (_, parents) = home.get(&place, &home.parent).await;
    assert_eq!(4, parents["page"], "{parents}");
    assert_eq!(true, parents["finished"], "{parents}");
    let (_, readers) = home.get(&place, &home.reader).await;
    assert_eq!(1, readers["page"], "{readers}");
    assert_eq!(false, readers["finished"], "{readers}");

    // The badges on the shelf, and the filter behind « Non lues » and « Lues ».
    let (_, shelf) = home.get("/series", &home.parent).await;
    assert_eq!("READ", shelf["items"][0]["readStatus"], "{shelf}");
    let (_, shelf) = home.get("/series", &home.reader).await;
    assert_eq!("IN_PROGRESS", shelf["items"][0]["readStatus"], "{shelf}");
    assert!(shelf["items"][0].get("readEntries").is_none(), "{shelf}");
    let (_, read) = home.get("/series?read=READ", &home.reader).await;
    assert_eq!(
        0, read["total"],
        "the parent's reading is not the reader's: {read}"
    );
    let (_, read) = home.get("/series?read=READ", &home.parent).await;
    assert_eq!(1, read["total"], "{read}");

    // And the series' own list of places.
    let (_, places) = home
        .get(&format!("/series/{}/progress", home.series), &home.reader)
        .await;
    assert_eq!(1, places.as_array().unwrap().len(), "{places}");
    assert_eq!(1, places[0]["page"], "{places}");
}

#[tokio::test]
async fn what_next_offers_each_profile_its_own_volume_once() {
    let home = a_household();
    let place = format!("/entries/{}/progress", home.first);
    home.patch(&place, &home.parent, serde_json::json!({"page": 3}))
        .await;
    home.patch(&place, &home.reader, serde_json::json!({"page": 1}))
        .await;

    for (key, page) in [(&home.parent, 3), (&home.reader, 1)] {
        let (status, next) = home.get("/next", key).await;
        assert_eq!(StatusCode::OK, status);
        let rows = next.as_array().unwrap();
        assert_eq!(1, rows.len(), "one volume, offered once: {next}");
        assert_eq!(home.first, rows[0]["entry"]["id"], "{next}");
        assert_eq!(page, rows[0]["progress"]["page"], "{next}");
    }

    // One profile finishing a volume offers the next one to that profile only.
    home.patch(&place, &home.parent, serde_json::json!({"finished": true}))
        .await;
    let (_, next) = home.get("/next", &home.parent).await;
    assert_eq!("NEXT_UP", next[0]["reason"], "{next}");
    let (_, next) = home.get("/next", &home.reader).await;
    assert_eq!(1, next.as_array().unwrap().len(), "{next}");
    assert_eq!("IN_PROGRESS", next[0]["reason"], "{next}");
}

#[tokio::test]
async fn a_profile_recording_its_place_is_answered_with_its_own_position() {
    let home = a_household();
    let place = format!("/entries/{}/progress", home.first);
    let (_, parents) = home
        .patch(&place, &home.parent, serde_json::json!({"page": 3}))
        .await;
    assert_eq!(3, parents["page"], "{parents}");

    let (status, readers) = home
        .patch(&place, &home.reader, serde_json::json!({"page": 1}))
        .await;
    assert_eq!(StatusCode::OK, status, "{readers}");
    assert_eq!(1, readers["page"], "never the parent's page 3: {readers}");

    let (_, marked) = home
        .patch(
            &format!("/series/{}/progress", home.series),
            &home.reader,
            serde_json::json!({"finished": false}),
        )
        .await;
    let places = marked.as_array().unwrap();
    assert_eq!(1, places.len(), "the reader's own place, alone: {marked}");
    assert_eq!(1, places[0]["page"], "{marked}");
}
