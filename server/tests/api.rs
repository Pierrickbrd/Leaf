//! The routes, answered through the real router.
//!
//! Not a mock: the request goes through axum, the handler runs, the database is a real
//! SQLite file. What these check is the shape the contract promises — including the
//! spelling of the field names, which is what a client generated from the contract will
//! be looking for.

use std::sync::Arc;

use axum::body::Body;
use axum::http::{Request, StatusCode};
use http_body_util::BodyExt;
use leaf_server::api::keys::Keys;
use leaf_server::api::routes::{router, AppState};
use leaf_server::store::Db;
use tower::ServiceExt;

async fn call(db: Arc<Db>, path: &str) -> (StatusCode, serde_json::Value) {
    let app = router(AppState::new(db, Keys::default()));
    let response = app
        .oneshot(Request::builder().uri(path).body(Body::empty()).unwrap())
        .await
        .expect("a response");
    let status = response.status();
    let bytes = response
        .into_body()
        .collect()
        .await
        .expect("a body")
        .to_bytes();
    let json = if bytes.is_empty() {
        serde_json::Value::Null
    } else {
        serde_json::from_slice(&bytes).expect("json")
    };
    (status, json)
}

#[tokio::test]
async fn health_answers_the_shape_the_contract_promises() {
    let dir = tempfile::tempdir().expect("a directory");
    let db = Arc::new(Db::open(&dir.path().join("index.sqlite")).expect("opening"));

    let (status, body) = call(Arc::clone(&db), "/health").await;

    assert_eq!(StatusCode::OK, status);
    assert_eq!("ok", body["status"]);
    assert_eq!(1, body["api"]);
    assert_eq!(1, body["format"]);
    assert_eq!(0, body["library"]);
    // Skipped at its default, exactly as kotlinx.serialization did with encodeDefaults =
    // false. A client reading it as absent-means-false is reading it correctly.
    assert!(
        body.get("localDrop").is_none(),
        "a default must not cross the wire"
    );
}

#[tokio::test]
async fn health_counts_the_library_rather_than_building_it() {
    let dir = tempfile::tempdir().expect("a directory");
    let db = Arc::new(Db::open(&dir.path().join("index.sqlite")).expect("opening"));
    db.write(|cx| {
        cx.execute(
            "INSERT INTO work (id, name, path) VALUES ('w', 'Essai', '/w')",
            [],
        )?;
        for (id, path) in [("e1", "/w/a"), ("e2", "/w/b")] {
            cx.execute(
                "INSERT INTO edition (id, work_id, path, implicit) VALUES (?1, 'w', ?2, 0)",
                (id, path),
            )?;
        }
        Ok(())
    })
    .expect("seeding");

    let before = db.statements();
    let (_, body) = call(Arc::clone(&db), "/health").await;

    assert_eq!(2, body["library"]);
    // One question, whatever the library holds. Building every series to read the length
    // of the list was a real defect, on the one route that answers without a key.
    assert_eq!(1, db.statements() - before, "/health must cost one query");
}

/// A key whose secret is not ASCII must not take the server down at startup.
#[test]
fn a_malformed_key_is_reported_rather_than_sliced_mid_character() {
    // `&line[..12]` lands inside the last é, and a slice panic is a poor way to report a
    // typo in a configuration file.
    let refused = Keys::parse(Some("désorganisé"));
    assert!(
        refused.is_err(),
        "a configuration of nothing but typos is a fault"
    );
    assert!(format!("{:#}", refused.unwrap_err()).contains("not one usable key"));

    // A malformed line beside a good one is ignored, and the good one stands.
    let keys = Keys::parse(Some("désorganisé  phone:1111111111111111:read")).expect("keys");
    assert!(!keys.open());
    assert!(keys.recognise(Some("1111111111111111")).is_some());
}

/// The record of an address must not outlive the reason for keeping it.
#[test]
fn the_throttle_forgets_the_addresses_it_no_longer_has_anything_against() {
    use leaf_server::api::throttle::Throttle;
    use std::time::Duration;

    // Behind a trusted proxy the address is a header the caller writes. Remembering every
    // distinct value for ever would turn the defence into a way of filling the memory.
    let throttle = Throttle::new(10, Duration::from_secs(300), Duration::from_secs(900));
    for i in 0..20_000 {
        throttle.record_failure(&format!("10.{}.{}.{}", i / 65536, (i / 256) % 256, i % 256));
    }

    // Every one of those is fresh, which is exactly the moment the list has to stop
    // growing — sweeping the stale ones alone would have bounded nothing.
    assert!(
        throttle.remembered() <= 1024,
        "it kept {} addresses out of twenty thousand",
        throttle.remembered()
    );

    // And what it is actually enforcing survives the tidying.
    let blocked = "192.168.1.1";
    for _ in 0..10 {
        throttle.record_failure(blocked);
    }
    for i in 20_000..40_000 {
        throttle.record_failure(&format!("10.{}.{}.{}", i / 65536, (i / 256) % 256, i % 256));
    }
    assert!(
        throttle.blocked_for(blocked).is_some(),
        "a blocked address was forgotten to make room for the noise blocking it"
    );
}
