//! What each profile may do to the library, checked at the door of every route.
//!
//! The table in the design is the reference: « oui » means the server accepts, « non » means
//! it refuses. A device carried every right until profiles existed, because whoever linked
//! it could already import; a reader without a ticked box, or a child's tablet, now carries
//! exactly what its profile says, and an old key from `LEAF_KEYS` exactly what is written
//! beside it.

mod common;

use std::sync::Arc;

use axum::body::Body;
use axum::http::StatusCode;
use common::{archive_bytes, json_body, request, Server, ERASER, IMPORTER, READ_ONLY};
use leaf_server::metadata::sidecars::EntryJson;
use leaf_server::store::devices::Devices;
use leaf_server::store::profiles::{CodeScope, NewProfile, Profiles, Rights};

const NOW: i64 = 1_790_000_000_000;

/// « Petit » (8+) and « Grand » (16+), three volumes each: enough for a child to see one and
/// not the other, and for a delete to have something to take without emptying a series.
fn a_library() -> Server {
    let server = Server::new();
    let library = server.library();
    for (work, rating) in [("Petit", "8+"), ("Grand", "16+")] {
        let folder = library.join(work);
        std::fs::create_dir_all(&folder).unwrap();
        std::fs::write(
            folder.join("work.json"),
            format!(r#"{{"leaf":1,"title":"{work}","ageRating":"{rating}"}}"#),
        )
        .unwrap();
        for number in [1, 2, 3] {
            std::fs::write(
                folder.join(format!("Tome {number}.cbz")),
                archive_bytes(Some(&EntryJson {
                    leaf: Some(1),
                    work: Some(work.into()),
                    number: Some(f64::from(number)),
                    ..Default::default()
                })),
            )
            .unwrap();
        }
    }
    server.scan();
    server
}

/// One key per kind of profile, each on its own device.
struct Household {
    principal: String,
    /// A reader nobody ticked anything for.
    reader: String,
    /// A reader who may import and edit, and not delete.
    editor: String,
    /// A reader who may delete, and nothing else.
    eraser: String,
    /// Restricted to ten.
    child: String,
}

fn a_household(server: &Server) -> Household {
    let profiles = Profiles::new(Arc::clone(&server.db));
    profiles
        .set_code("2468", CodeScope::Restricted)
        .unwrap()
        .unwrap();
    let seated = |device: &str, name: &str, color: &str, restricted: bool, rights: Rights| {
        let profile = profiles
            .create(
                NewProfile {
                    name: name.into(),
                    color: color.into(),
                    restricted,
                    max_age: restricted.then_some(10),
                    rights,
                },
                NOW,
            )
            .unwrap()
            .unwrap();
        let key = server.linked_device(device);
        profiles
            .place(&device_id(server, device), &profile.id, false)
            .unwrap()
            .unwrap();
        key
    };
    Household {
        principal: server.linked_device("Portable"),
        reader: seated("Liseuse", "Camille", "bleu", false, Rights::default()),
        editor: seated(
            "Bureau",
            "Dominique",
            "ocre",
            false,
            Rights {
                import: true,
                edit: true,
                delete: false,
            },
        ),
        eraser: seated(
            "Grenier",
            "Eden",
            "prune",
            false,
            Rights {
                import: false,
                edit: false,
                delete: true,
            },
        ),
        child: seated("Tablette", "Léo", "corail", true, Rights::default()),
    }
}

fn device_id(server: &Server, name: &str) -> String {
    server
        .db
        .read(|cx| {
            cx.query_one("SELECT id FROM device WHERE name = ?1", [name], |r| {
                r.get::<_, String>(0)
            })
        })
        .unwrap()
        .expect("the device")
}

fn series_of(server: &Server, work: &str) -> String {
    server
        .db
        .read(|cx| {
            cx.query_one(
                "SELECT e.id FROM edition e JOIN work w ON w.id = e.work_id WHERE w.name = ?1",
                [work],
                |r| r.get::<_, String>(0),
            )
        })
        .unwrap()
        .unwrap_or_else(|| panic!("no series for {work}"))
}

/// The volume of a work at a position, first being 0.
fn entry_of(server: &Server, work: &str, nth: i64) -> String {
    let series = series_of(server, work);
    server
        .db
        .read(|cx| {
            cx.query_one(
                "SELECT id FROM entry WHERE edition_id = ?1 ORDER BY sort_key LIMIT 1 OFFSET ?2",
                rusqlite::params![series, nth],
                |r| r.get::<_, String>(0),
            )
        })
        .unwrap()
        .expect("an entry")
}

async fn call(
    server: &Server,
    method: &str,
    uri: &str,
    key: &str,
    body: Option<serde_json::Value>,
) -> (StatusCode, serde_json::Value) {
    let builder = request(method, uri, key);
    let request = match body {
        Some(body) => builder
            .header("Content-Type", "application/json")
            .body(json_body(body))
            .unwrap(),
        None => builder.body(Body::empty()).unwrap(),
    };
    server.send(request).await
}

async fn get(server: &Server, uri: &str, key: &str) -> (StatusCode, serde_json::Value) {
    call(server, "GET", uri, key, None).await
}

fn a_rating() -> Option<serde_json::Value> {
    Some(serde_json::json!({"ageRating": "6+"}))
}

// ------------------------------------------------------------------ the rights

#[tokio::test]
async fn a_reader_without_rights_only_reads() {
    let server = a_library();
    let household = a_household(&server);
    let petit = series_of(&server, "Petit");
    let entry = entry_of(&server, "Petit", 2);

    for (method, uri, body, right) in [
        ("GET", "/import".to_string(), None, "import"),
        ("PATCH", format!("/series/{petit}"), a_rating(), "edit"),
        ("DELETE", format!("/entries/{entry}"), None, "delete"),
    ] {
        let (status, said) = call(&server, method, &uri, &household.reader, body).await;
        assert_eq!(StatusCode::FORBIDDEN, status, "{method} {uri}: {said}");
        assert_eq!(
            format!("this profile does not carry the \"{right}\" right"),
            said["error"],
            "{method} {uri}"
        );
    }
    assert!(
        server.library().join("Petit/Tome 3.cbz").exists(),
        "a refused delete takes nothing"
    );

    for uri in [
        "/series".to_string(),
        format!("/series/{petit}"),
        format!("/series/{petit}/entries"),
        format!("/entries/{entry}/pages/0"),
        // Exporting is not a right to tick: a reader already sees every page.
        format!("/entries/{entry}/file"),
    ] {
        let (status, said) = get(&server, &uri, &household.reader).await;
        assert_eq!(StatusCode::OK, status, "{uri}: {said}");
    }
}

#[tokio::test]
async fn each_right_opens_only_its_own_routes() {
    let server = a_library();
    let household = a_household(&server);
    let petit = series_of(&server, "Petit");
    let entry = entry_of(&server, "Petit", 2);

    let (status, said) = get(&server, "/import", &household.editor).await;
    assert_eq!(StatusCode::OK, status, "import: {said}");
    let (status, said) = call(
        &server,
        "PATCH",
        &format!("/series/{petit}"),
        &household.editor,
        a_rating(),
    )
    .await;
    assert_eq!(StatusCode::OK, status, "edit: {said}");
    let (status, said) = call(
        &server,
        "DELETE",
        &format!("/entries/{entry}"),
        &household.editor,
        None,
    )
    .await;
    assert_eq!(StatusCode::FORBIDDEN, status, "delete: {said}");

    // And the other way round: deleting opens neither importing nor editing.
    let (status, _) = get(&server, "/import", &household.eraser).await;
    assert_eq!(StatusCode::FORBIDDEN, status);
    let (status, _) = call(
        &server,
        "PATCH",
        &format!("/series/{petit}"),
        &household.eraser,
        a_rating(),
    )
    .await;
    assert_eq!(StatusCode::FORBIDDEN, status);
    let (status, said) = call(
        &server,
        "DELETE",
        &format!("/entries/{entry}"),
        &household.eraser,
        None,
    )
    .await;
    assert!(status.is_success(), "delete: {status} {said}");
    assert!(!server.library().join("Petit/Tome 3.cbz").exists());
}

/// Scanning, cleaning up and linking belong to the principal alone: relinking a device or
/// renaming this one lives in the Devices tab, which nobody else is shown.
#[tokio::test]
async fn only_the_principal_scans_and_links() {
    let server = a_library();
    let household = a_household(&server);
    let routes = [
        ("POST", "/scan", None),
        (
            "POST",
            "/cleanup",
            Some(serde_json::json!({"root": "Petit", "files": []})),
        ),
        ("POST", "/link/codes", None),
        ("GET", "/link/codes/nothing", None),
        ("DELETE", "/link/codes/nothing", None),
        (
            "PATCH",
            "/device",
            Some(serde_json::json!({"name": "Autre"})),
        ),
        ("DELETE", "/device", None),
    ];
    for key in [
        &household.reader,
        &household.editor,
        &household.eraser,
        &household.child,
    ] {
        for (method, uri, body) in &routes {
            let (status, said) = call(&server, method, uri, key, body.clone()).await;
            assert_eq!(StatusCode::FORBIDDEN, status, "{method} {uri}: {said}");
            assert_eq!(
                "only the principal profile may do this", said["error"],
                "{method} {uri}"
            );
        }
    }
    assert_eq!(
        5,
        Devices::new(Arc::clone(&server.db))
            .list(NOW)
            .unwrap()
            .0
            .len(),
        "a refused unlink removes nobody"
    );

    let (status, said) = call(&server, "POST", "/scan", &household.principal, None).await;
    assert_eq!(StatusCode::ACCEPTED, status, "{said}");
    let (status, said) = call(
        &server,
        "PATCH",
        "/device",
        &household.principal,
        Some(serde_json::json!({"name": "Portable du salon"})),
    )
    .await;
    assert_eq!(StatusCode::OK, status, "{said}");
    // No relay is configured here: refused for that, and not for who asked.
    let (status, said) = call(&server, "POST", "/link/codes", &household.principal, None).await;
    assert_eq!(StatusCode::CONFLICT, status, "{said}");
}

#[tokio::test]
async fn a_restricted_profile_cannot_take_the_whole_file() {
    let server = a_library();
    let household = a_household(&server);
    let entry = entry_of(&server, "Petit", 0);

    let (status, said) = get(&server, &format!("/entries/{entry}/file"), &household.child).await;
    assert_eq!(StatusCode::FORBIDDEN, status, "{said}");
    // The same refusal for a volume above its age as for one it may read: a 404 here would
    // say which of the two it was.
    let hidden = entry_of(&server, "Grand", 0);
    assert_eq!(
        (status, said),
        get(
            &server,
            &format!("/entries/{hidden}/file"),
            &household.child
        )
        .await
    );

    let (status, _) = get(
        &server,
        &format!("/entries/{entry}/pages/0"),
        &household.child,
    )
    .await;
    assert_eq!(StatusCode::OK, status, "the pages one by one, still");
}

/// A restricted profile carries no right, so the door refuses before anything is read — and
/// the refusal carries nothing of the series, not even that it is there.
#[tokio::test]
async fn a_restricted_profile_cannot_edit_a_series_it_cannot_see() {
    let server = a_library();
    let household = a_household(&server);
    let grand = series_of(&server, "Grand");

    let (status, said) = call(
        &server,
        "PATCH",
        &format!("/series/{grand}"),
        &household.child,
        a_rating(),
    )
    .await;
    assert_eq!(StatusCode::FORBIDDEN, status, "{said}");
    assert_eq!(
        serde_json::json!({"error": "this profile does not carry the \"edit\" right"}),
        said
    );
    let (absent_status, absent) = call(
        &server,
        "PATCH",
        "/series/nothing",
        &household.child,
        a_rating(),
    )
    .await;
    assert_eq!((status, said), (absent_status, absent));
    let work = std::fs::read_to_string(server.library().join("Grand/work.json")).unwrap();
    assert!(work.contains("16+"), "nothing was written: {work}");
}

/// What a write route answers is read back as whoever wrote: a reader's edit comes back with
/// the reader's own place in the series, not the principal's.
#[tokio::test]
async fn an_edit_is_answered_as_the_profile_that_made_it() {
    let server = a_library();
    let household = a_household(&server);
    let petit = series_of(&server, "Petit");
    // The principal has finished every volume; the editor has opened none.
    server
        .db
        .write(|cx| {
            cx.execute(
                "INSERT INTO progress (profile_id, entry_id, edition_id, page, finished,
                                       times_finished, updated_at)
                 SELECT 'default', x.id, x.edition_id, 0, 1, 1, 1 FROM entry x
                  WHERE x.edition_id = ?1",
                [&petit],
            )
        })
        .unwrap();
    let (_, principal) = get(&server, &format!("/series/{petit}"), &household.principal).await;
    assert_eq!("READ", principal["readStatus"], "{principal}");

    let (status, edited) = call(
        &server,
        "PATCH",
        &format!("/series/{petit}"),
        &household.editor,
        a_rating(),
    )
    .await;
    assert_eq!(StatusCode::OK, status, "{edited}");
    assert!(
        edited.get("readStatus").is_none() || edited["readStatus"] == "UNREAD",
        "the editor's own status: {edited}"
    );
}

// ------------------------------------------------------------------ the seat

/// A device whose profile was deleted under it must say who reads before anything else: read
/// as the principal in the meantime, a child's tablet would open the whole library.
#[tokio::test]
async fn a_device_that_must_choose_is_refused_everything_else() {
    let server = a_library();
    let household = a_household(&server);
    let profiles = Profiles::new(Arc::clone(&server.db));
    let petit = series_of(&server, "Petit");
    for (key, device) in [
        (&household.reader, "Liseuse"),
        (&household.child, "Tablette"),
    ] {
        let seat = profiles.seat(&device_id(&server, device)).unwrap();
        profiles.delete(&seat.profile_id).unwrap().unwrap();

        for (method, uri, body) in [
            ("GET", "/series".to_string(), None),
            ("GET", format!("/series/{petit}"), None),
            ("PATCH", format!("/series/{petit}"), a_rating()),
            ("POST", "/scan".to_string(), None),
            (
                "PATCH",
                "/device".to_string(),
                Some(serde_json::json!({"name": "x"})),
            ),
        ] {
            let (status, said) = call(&server, method, &uri, key, body).await;
            assert_eq!(
                StatusCode::CONFLICT,
                status,
                "{device} {method} {uri}: {said}"
            );
            assert_eq!(
                serde_json::json!({"error": "choose a profile"}),
                said,
                "{device} {method} {uri}"
            );
        }
        // `/health` answers everyone: a device that must choose is simply recognised there.
        let (status, said) = get(&server, "/health", key).await;
        assert_eq!(StatusCode::OK, status, "{device}: {said}");
        assert!(said.get("relay").is_some(), "{device} recognised: {said}");
    }
}

// ------------------------------------------------------------------ old keys

/// A key from `LEAF_KEYS`, like an open library, is the principal for everything a profile
/// decides — every series, the principal's progress — and keeps exactly the rights written
/// beside it.
#[tokio::test]
async fn an_old_key_keeps_its_rights_and_reads_as_the_principal() {
    let server = a_library();
    a_household(&server);
    let petit = series_of(&server, "Petit");
    let entry = entry_of(&server, "Petit", 2);

    let (status, shelf) = get(&server, "/series", READ_ONLY).await;
    assert_eq!(StatusCode::OK, status);
    assert_eq!(2, shelf["total"], "every series, as the principal: {shelf}");
    let (status, _) = get(&server, &format!("/entries/{entry}/file"), READ_ONLY).await;
    assert_eq!(StatusCode::OK, status, "a read key exports");

    let (status, said) = call(
        &server,
        "PATCH",
        &format!("/series/{petit}"),
        READ_ONLY,
        a_rating(),
    )
    .await;
    assert_eq!(StatusCode::FORBIDDEN, status);
    assert_eq!(
        "this key does not carry the \"import\" right",
        said["error"]
    );

    // Editing has no key right of its own: an old key edits with the import right, as before.
    let (status, said) = call(
        &server,
        "PATCH",
        &format!("/series/{petit}"),
        IMPORTER,
        a_rating(),
    )
    .await;
    assert_eq!(StatusCode::OK, status, "{said}");
    let (status, said) = call(&server, "POST", "/scan", IMPORTER, None).await;
    assert_eq!(StatusCode::ACCEPTED, status, "{said}");
    let (status, said) = call(
        &server,
        "DELETE",
        &format!("/entries/{entry}"),
        IMPORTER,
        None,
    )
    .await;
    assert_eq!(StatusCode::FORBIDDEN, status);
    assert_eq!(
        "this key does not carry the \"delete\" right",
        said["error"]
    );

    let (status, said) = call(&server, "POST", "/scan", ERASER, None).await;
    assert_eq!(StatusCode::FORBIDDEN, status, "{said}");
    let (status, said) = call(
        &server,
        "DELETE",
        &format!("/entries/{entry}"),
        ERASER,
        None,
    )
    .await;
    assert!(status.is_success(), "{status} {said}");
}

// ------------------------------------------------------------------ the cost

/// The profile is read with the key, in the same statement: a device's request costs the
/// question that recognises it and nothing more, whatever its profile.
#[tokio::test]
async fn recognising_a_device_and_its_profile_is_one_question() {
    let server = a_library();
    let household = a_household(&server);
    let devices = Devices::new(Arc::clone(&server.db));
    // Once each, so the minute-long `seen_at` stamp is behind every count below.
    for key in [&household.principal, &household.child] {
        get(&server, "/series", key).await;
    }

    let now = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap()
        .as_millis() as i64;
    let before = server.db.statements();
    let (device, profile) = devices
        .recognise(&household.child, now)
        .unwrap()
        .expect("recognised");
    assert_eq!(1, server.db.statements() - before);
    assert_eq!(device.name, "Tablette");
    assert_eq!(profile.name, "Léo");
    assert_eq!(profile.max_age, Some(10));

    let cost = |key: &'static str| {
        let server = &server;
        async move {
            let before = server.db.statements();
            let (status, _) = get(server, "/series", key).await;
            assert_eq!(StatusCode::OK, status);
            server.db.statements() - before
        }
    };
    let old_key = cost(READ_ONLY).await;
    let principal = {
        let before = server.db.statements();
        get(&server, "/series", &household.principal).await;
        server.db.statements() - before
    };
    let child = {
        let before = server.db.statements();
        get(&server, "/series", &household.child).await;
        server.db.statements() - before
    };
    assert_eq!(old_key + 1, principal, "the key, and its profile with it");
    assert_eq!(principal, child, "a ceiling costs no question of its own");
}
