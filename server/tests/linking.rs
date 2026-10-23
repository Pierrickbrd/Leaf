//! The door: who a request is, once devices hold keys of their own beside the old
//! `LEAF_KEYS`, and when a library with neither is open or closed — and the routes by which
//! a device gets its key, renames itself, and leaves.

mod common;

use std::sync::Arc;

use axum::body::Body;
use axum::http::{Request, StatusCode};
use common::{a_volume, json_body, request, Server, READ_ONLY};
use leaf_server::api::keys::Keys;
use leaf_server::api::routes::AppState;
use leaf_server::net::link::Reach;
use leaf_server::net::tls::Tls;
use leaf_server::store::devices::{Creator, Devices, CODE_LIFETIME};
use leaf_server::store::Db;

/// A state with no `LEAF_KEYS` at all, the way a fresh installation starts.
fn keyless(server: &Server) -> AppState {
    AppState::new(Arc::clone(&server.db), Keys::default())
}

/// Reached by other devices through a tunnel's address, declared as `LEAF_PUBLIC_URL`: plain
/// http on the loopback, so nothing is asked of the network to make a link to it.
fn through_a_tunnel(state: AppState) -> AppState {
    settled(
        state,
        Reach {
            public_url: Some("http://127.0.0.1:9099".into()),
            ..Reach::listening_on("127.0.0.1", 8081, None)
        },
    )
}

/// What `main` does once at start: the reach settled into the one relay the routes keep.
fn settled(state: AppState, reach: Reach) -> AppState {
    state.relaying(reach.settle())
}

fn anonymous(uri: &str) -> Request<Body> {
    Request::builder().uri(uri).body(Body::empty()).unwrap()
}

#[tokio::test]
async fn a_library_with_no_key_and_no_device_is_open_on_the_loopback() {
    let server = Server::new();
    let (status, _) = server.send_to(keyless(&server), anonymous("/series")).await;
    assert_eq!(StatusCode::OK, status);
}

#[tokio::test]
async fn linking_the_first_device_closes_a_library_that_was_open() {
    let server = Server::new();
    server.linked_device("Liseuse");
    let (status, _) = server.send_to(keyless(&server), anonymous("/series")).await;
    assert_eq!(StatusCode::FORBIDDEN, status);
}

#[tokio::test]
async fn revoking_the_only_device_leaves_the_library_closed_for_good() {
    let server = Server::new();
    let key = server.linked_device("Téléphone perdu");
    Devices::new(Arc::clone(&server.db))
        .revoke("Téléphone perdu")
        .unwrap()
        .expect("revoked");

    let (status, _) = server.send_to(keyless(&server), anonymous("/series")).await;
    assert_eq!(StatusCode::FORBIDDEN, status, "no key");
    let (status, body) = server
        .send_to(
            keyless(&server),
            request("GET", "/series", &key).body(Body::empty()).unwrap(),
        )
        .await;
    assert_eq!(StatusCode::FORBIDDEN, status, "the revoked key: {body}");

    // Still reachable the ways a new device needs: the doorbell and the exchange.
    let (status, body) = server.send_to(keyless(&server), anonymous("/health")).await;
    assert_eq!(StatusCode::OK, status);
    assert_eq!(serde_json::json!(true), body["linked"], "{body}");
    let code = a_terminal_code(&server);
    let (status, body) = server
        .send_to(keyless(&server), exchange(&code, "Liseuse"))
        .await;
    assert_eq!(StatusCode::CREATED, status, "{body}");
}

#[tokio::test]
async fn an_unknown_key_is_refused_even_by_a_library_never_linked() {
    let server = Server::new();
    let (status, body) = server
        .send_to(
            keyless(&server),
            request("GET", "/series", "a-key-nobody-gave")
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::FORBIDDEN, status);
    assert_eq!("unknown key", body["error"].as_str().unwrap_or_default());
}

#[tokio::test]
async fn off_the_loopback_a_library_with_no_device_is_closed_but_answers_health() {
    let server = Server::new();
    let state = keyless(&server).bound_to("0.0.0.0");

    let (status, _) = server.send_to(state.clone(), anonymous("/series")).await;
    assert_eq!(StatusCode::FORBIDDEN, status);
    let (status, _) = server.send_to(state, anonymous("/health")).await;
    assert_eq!(StatusCode::OK, status);
}

#[tokio::test]
async fn every_spelling_of_the_loopback_is_the_loopback_and_nothing_else_is() {
    for host in ["127.0.0.1", "localhost", "::1"] {
        assert!(leaf_server::boot::is_loopback(host), "{host}");
    }
    for host in ["0.0.0.0", "192.168.1.10", "::", "leaf.example"] {
        assert!(!leaf_server::boot::is_loopback(host), "{host}");
    }
}

#[tokio::test]
async fn a_device_key_opens_every_route_including_import_and_delete() {
    let server = Server::new();
    let key = server.linked_device("PC du salon");
    let state = server.state();

    let (status, _) = server
        .send_to(
            state.clone(),
            request("GET", "/series", &key).body(Body::empty()).unwrap(),
        )
        .await;
    assert_eq!(StatusCode::OK, status);

    // Behind `Importer`.
    let (status, body) = server
        .send_to(
            state.clone(),
            request("POST", "/scan", &key).body(Body::empty()).unwrap(),
        )
        .await;
    assert_ne!(StatusCode::FORBIDDEN, status, "{body}");

    // Behind `Eraser`: a series that is not there is a 404, which is past the door.
    let (status, body) = server
        .send_to(
            state,
            request("DELETE", "/series/nope", &key)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::NOT_FOUND, status, "{body}");
}

#[tokio::test]
async fn an_old_key_keeps_exactly_the_rights_it_had() {
    let server = Server::new();
    server.linked_device("Liseuse");
    let state = server.state();

    let (status, _) = server
        .send_to(
            state.clone(),
            request("GET", "/series", READ_ONLY)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::OK, status);

    let (status, body) = server
        .send_to(
            state,
            request("POST", "/scan", READ_ONLY)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::FORBIDDEN, status);
    assert_eq!(
        "this key does not carry the \"import\" right",
        body["error"].as_str().unwrap_or_default(),
        "{body}"
    );
}

#[tokio::test]
async fn a_device_revoked_by_another_process_is_refused_at_its_next_request() {
    let server = Server::new();
    let key = server.linked_device("Téléphone perdu");
    let state = server.state();

    let (status, _) = server
        .send_to(
            state.clone(),
            request("GET", "/series", &key).body(Body::empty()).unwrap(),
        )
        .await;
    assert_eq!(StatusCode::OK, status);

    // `leaf-server device remove`, run in a terminal: its own connection to the same file.
    let elsewhere = Arc::new(Db::open(&server.dir.path().join("index.sqlite")).unwrap());
    Devices::new(elsewhere)
        .revoke("Téléphone perdu")
        .unwrap()
        .expect("revoked");

    let (status, _) = server
        .send_to(
            state,
            request("GET", "/series", &key).body(Body::empty()).unwrap(),
        )
        .await;
    assert_eq!(StatusCode::FORBIDDEN, status);
}

#[tokio::test]
async fn a_wrong_device_key_counts_against_the_throttle() {
    let server = Server::new();
    server.linked_device("Liseuse");
    let state = keyless(&server);

    // Shaped like a device key, so nothing but the lookup can tell it is wrong.
    let wrong = "A".repeat(43);
    let mut statuses = Vec::new();
    for _ in 0..11 {
        let (status, _) = server
            .send_to(
                state.clone(),
                request("GET", "/series", &wrong)
                    .body(Body::empty())
                    .unwrap(),
            )
            .await;
        statuses.push(status);
    }
    assert!(statuses[..10].iter().all(|s| *s == StatusCode::FORBIDDEN));
    assert_eq!(StatusCode::TOO_MANY_REQUESTS, statuses[10]);
}

#[tokio::test]
async fn reading_progress_writes_it_under_the_callers_profile() {
    let server = Server::new();
    a_volume(&server);
    let entry = server.entry();
    let key = server.linked_device("Liseuse");
    // Every device is on the default profile until profiles can be chosen; moving this one
    // by hand is what tells the caller's profile apart from the default one.
    server
        .db
        .write(|cx| {
            cx.execute(
                "INSERT INTO profile (id, name, name_key, created_at) VALUES ('kid', 'Kid', 'kid', 1)",
                [],
            )?;
            cx.execute("UPDATE device SET profile_id = 'kid'", [])
        })
        .unwrap();

    let (status, body) = server
        .send(
            request("PATCH", &format!("/entries/{entry}/progress"), &key)
                .header("Content-Type", "application/json")
                .body(Body::from(r#"{"page":0}"#))
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::OK, status, "{body}");

    let profiles: Vec<String> = server
        .db
        .read(|cx| {
            cx.query(
                "SELECT profile_id FROM progress WHERE entry_id = ?1",
                [&entry],
                |r| r.get(0),
            )
        })
        .unwrap();
    assert_eq!(vec!["kid".to_string()], profiles);
}

// ------------------------------------------------------------------ the routes

fn now() -> i64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap()
        .as_millis() as i64
}

/// A code the way `leaf-server device add` makes one, as the person would type it.
fn a_terminal_code(server: &Server) -> String {
    Devices::new(Arc::clone(&server.db))
        .issue(Creator::Terminal, now())
        .expect("a code")
        .code
}

fn exchange(code: &str, name: &str) -> Request<Body> {
    Request::builder()
        .method("POST")
        .uri("/link")
        .header("content-type", "application/json")
        .body(json_body(serde_json::json!({ "code": code, "name": name })))
        .unwrap()
}

fn with_body(method: &str, uri: &str, key: &str, body: serde_json::Value) -> Request<Body> {
    request(method, uri, key)
        .header("content-type", "application/json")
        .body(json_body(body))
        .unwrap()
}

fn empty(method: &str, uri: &str, key: &str) -> Request<Body> {
    request(method, uri, key).body(Body::empty()).unwrap()
}

#[tokio::test]
async fn exchanging_a_code_answers_the_key_and_the_registered_name() {
    let server = Server::new();
    let code = a_terminal_code(&server);

    // Typed the way a person types it: lower case, no dash, a space at the edge.
    let typed = format!(" {} ", code.replace('-', "").to_lowercase());
    let (status, body) = server
        .send_to(keyless(&server), exchange(&typed, "  Liseuse  "))
        .await;

    assert_eq!(StatusCode::CREATED, status, "{body}");
    assert_eq!(
        "Liseuse", body["device"]["name"],
        "the name as stored, trimmed"
    );
    assert_eq!(16, body["device"]["id"].as_str().unwrap().len());
    let key = body["key"].as_str().expect("a key");
    assert_eq!(43, key.len());

    // And the key is the one that now opens the door.
    let (status, _) = server
        .send_to(keyless(&server), empty("GET", "/series", key))
        .await;
    assert_eq!(StatusCode::OK, status);
}

#[tokio::test]
async fn an_expired_code_is_a_410_and_counts_against_the_throttle() {
    let server = Server::new();
    let long_ago = now() - CODE_LIFETIME - 1;
    let expired = Devices::new(Arc::clone(&server.db))
        .issue(Creator::Terminal, long_ago)
        .unwrap()
        .code;
    let state = keyless(&server);

    let (status, body) = server
        .send_to(state.clone(), exchange(&expired, "Liseuse"))
        .await;
    assert_eq!(StatusCode::GONE, status);
    assert_eq!("code", body["error"]);

    // Nine more wrong ones fill the allowance, and the eleventh is refused whatever it holds.
    for _ in 0..9 {
        let (status, _) = server
            .send_to(state.clone(), exchange("ABCD-EFGH", "Liseuse"))
            .await;
        assert_eq!(StatusCode::GONE, status);
    }
    let fresh = a_terminal_code(&server);
    let (status, _) = server.send_to(state, exchange(&fresh, "Liseuse")).await;
    assert_eq!(StatusCode::TOO_MANY_REQUESTS, status);
}

#[tokio::test]
async fn a_good_key_does_not_wipe_the_count_of_wrong_codes() {
    let server = Server::new();
    server.linked_device("Liseuse");
    let state = server.state();

    // One below the allowance of wrong codes...
    for _ in 0..9 {
        let (status, _) = server
            .send_to(state.clone(), exchange("ABCD-EFGH", "Liseuse"))
            .await;
        assert_eq!(StatusCode::GONE, status);
    }
    // ...then a request with a key that works, from the same address...
    let (status, _) = server
        .send_to(state.clone(), empty("GET", "/series", READ_ONLY))
        .await;
    assert_eq!(StatusCode::OK, status);

    // ...must not have given the guesses back: one more fills the allowance, and a code
    // that would have been right is refused.
    let (status, _) = server
        .send_to(state.clone(), exchange("ABCD-EFGH", "Liseuse"))
        .await;
    assert_eq!(StatusCode::GONE, status);
    let fresh = a_terminal_code(&server);
    let (status, _) = server.send_to(state, exchange(&fresh, "Tablette")).await;
    assert_eq!(StatusCode::TOO_MANY_REQUESTS, status);
}

#[tokio::test]
async fn an_empty_overlong_or_control_character_name_is_a_400_and_counts_against_the_throttle() {
    let server = Server::new();
    let state = keyless(&server);
    let code = a_terminal_code(&server);

    let (status, _) = server.send_to(state.clone(), exchange(&code, "   ")).await;
    assert_eq!(StatusCode::BAD_REQUEST, status);
    let (status, _) = server
        .send_to(state.clone(), exchange(&code, &"é".repeat(41)))
        .await;
    assert_eq!(StatusCode::BAD_REQUEST, status);
    let (status, _) = server
        .send_to(state.clone(), exchange(&code, "PC\u{1b}[2J"))
        .await;
    assert_eq!(StatusCode::BAD_REQUEST, status);

    for _ in 0..7 {
        server.send_to(state.clone(), exchange(&code, "")).await;
    }
    let (status, _) = server.send_to(state, exchange(&code, "Liseuse")).await;
    assert_eq!(StatusCode::TOO_MANY_REQUESTS, status);
}

#[tokio::test]
async fn a_taken_name_is_a_409_naming_it_and_leaves_the_code_usable() {
    let server = Server::new();
    server.linked_device("PC du salon");
    let code = a_terminal_code(&server);
    let state = keyless(&server);

    let (status, body) = server
        .send_to(state.clone(), exchange(&code, "pc  DU salon"))
        .await;
    assert_eq!(StatusCode::CONFLICT, status);
    assert_eq!("name", body["error"]);
    assert_eq!(
        "PC du salon", body["name"],
        "the name as its holder spells it"
    );

    let (status, _) = server.send_to(state, exchange(&code, "Liseuse")).await;
    assert_eq!(StatusCode::CREATED, status);
}

#[tokio::test]
async fn a_taken_name_does_not_count_against_the_throttle() {
    let server = Server::new();
    server.linked_device("PC du salon");
    let code = a_terminal_code(&server);
    let state = keyless(&server);

    for _ in 0..12 {
        let (status, _) = server
            .send_to(state.clone(), exchange(&code, "PC du salon"))
            .await;
        assert_eq!(StatusCode::CONFLICT, status);
    }
}

#[tokio::test]
async fn link_answers_without_a_key_even_when_the_library_is_closed() {
    let server = Server::new();
    server.linked_device("PC du salon");
    let code = a_terminal_code(&server);

    // Off the loopback, with a device linked and old keys configured: closed three ways.
    let state = server.state().bound_to("0.0.0.0");
    let (status, _) = server.send_to(state.clone(), anonymous("/series")).await;
    assert_eq!(StatusCode::FORBIDDEN, status);
    let (status, _) = server.send_to(state, exchange(&code, "Liseuse")).await;
    assert_eq!(StatusCode::CREATED, status);
}

#[tokio::test]
async fn an_open_library_cannot_issue_codes() {
    let server = Server::new();
    let (status, _) = server
        .send_to(keyless(&server), empty("POST", "/link/codes", ""))
        .await;
    assert_eq!(StatusCode::FORBIDDEN, status);
}

#[tokio::test]
async fn a_device_follows_the_code_it_issued_until_it_is_served() {
    let server = Server::new();
    let key = server.linked_device("PC du salon");
    let state = through_a_tunnel(server.state());

    let (status, issued) = server
        .send_to(state.clone(), empty("POST", "/link/codes", &key))
        .await;
    assert_eq!(StatusCode::CREATED, status, "{issued}");
    let code = issued["code"].as_str().unwrap();
    let id = issued["id"].as_str().unwrap();
    assert_eq!(9, code.len(), "XXXX-XXXX");
    assert!(issued["expiresAt"].as_i64().unwrap() > now());

    let uri = format!("/link/codes/{id}");
    let (status, body) = server
        .send_to(state.clone(), empty("GET", &uri, &key))
        .await;
    assert_eq!(StatusCode::OK, status);
    assert_eq!(serde_json::json!({ "state": "waiting" }), body);

    let (status, _) = server
        .send_to(state.clone(), exchange(code, "Liseuse"))
        .await;
    assert_eq!(StatusCode::CREATED, status);

    let (status, body) = server.send_to(state, empty("GET", &uri, &key)).await;
    assert_eq!(StatusCode::OK, status);
    assert_eq!(
        serde_json::json!({ "state": "served", "name": "Liseuse" }),
        body
    );
}

#[tokio::test]
async fn a_device_cancels_the_code_it_issued() {
    let server = Server::new();
    let key = server.linked_device("PC du salon");
    let state = through_a_tunnel(server.state());
    let (_, issued) = server
        .send_to(state.clone(), empty("POST", "/link/codes", &key))
        .await;
    let uri = format!("/link/codes/{}", issued["id"].as_str().unwrap());

    let (status, _) = server
        .send_to(state.clone(), empty("DELETE", &uri, &key))
        .await;
    assert_eq!(StatusCode::NO_CONTENT, status);
    let (status, _) = server
        .send_to(state.clone(), empty("GET", &uri, &key))
        .await;
    assert_eq!(StatusCode::NOT_FOUND, status);
    let (status, _) = server
        .send_to(state, exchange(issued["code"].as_str().unwrap(), "Liseuse"))
        .await;
    assert_eq!(StatusCode::GONE, status);
}

#[tokio::test]
async fn a_device_cannot_see_or_cancel_a_code_another_device_issued() {
    let server = Server::new();
    let first = server.linked_device("PC du salon");
    let second = server.linked_device("Liseuse");
    let state = through_a_tunnel(server.state());
    let (_, issued) = server
        .send_to(state.clone(), empty("POST", "/link/codes", &first))
        .await;
    let uri = format!("/link/codes/{}", issued["id"].as_str().unwrap());

    let (status, _) = server
        .send_to(state.clone(), empty("GET", &uri, &second))
        .await;
    assert_eq!(StatusCode::NOT_FOUND, status);
    let (status, _) = server
        .send_to(state.clone(), empty("DELETE", &uri, &second))
        .await;
    assert_eq!(StatusCode::NOT_FOUND, status);
    // Still there for its maker.
    let (status, _) = server.send_to(state, empty("GET", &uri, &first)).await;
    assert_eq!(StatusCode::OK, status);
}

#[tokio::test]
async fn renaming_answers_the_name_as_stored() {
    let server = Server::new();
    let key = server.linked_device("PC du salon");
    server.linked_device("Liseuse");
    let state = server.state();

    let (status, body) = server
        .send_to(
            state.clone(),
            with_body(
                "PATCH",
                "/device",
                &key,
                serde_json::json!({ "name": "  PC  Bureau " }),
            ),
        )
        .await;
    assert_eq!(StatusCode::OK, status, "{body}");
    assert_eq!("PC  Bureau", body["name"]);
    assert!(body["id"].is_string());

    let (status, body) = server
        .send_to(
            state.clone(),
            with_body(
                "PATCH",
                "/device",
                &key,
                serde_json::json!({ "name": "liseuse" }),
            ),
        )
        .await;
    assert_eq!(StatusCode::CONFLICT, status);
    assert_eq!("Liseuse", body["name"]);

    let (status, _) = server
        .send_to(
            state,
            with_body("PATCH", "/device", &key, serde_json::json!({ "name": "" })),
        )
        .await;
    assert_eq!(StatusCode::BAD_REQUEST, status);
}

#[tokio::test]
async fn unlinking_makes_the_key_useless_at_once() {
    let server = Server::new();
    let key = server.linked_device("PC du salon");
    server.linked_device("Liseuse");
    let state = server.state();

    let (status, _) = server
        .send_to(state.clone(), empty("DELETE", "/device", &key))
        .await;
    assert_eq!(StatusCode::NO_CONTENT, status);
    let (status, _) = server.send_to(state, empty("GET", "/series", &key)).await;
    assert_eq!(StatusCode::FORBIDDEN, status);
}

#[tokio::test]
async fn an_old_key_asked_to_rename_itself_is_sent_to_server_env() {
    let server = Server::new();
    let state = server.state();
    let sentence = "this key comes from LEAF_KEYS: rename or remove it in server.env";

    let (status, body) = server
        .send_to(
            state.clone(),
            with_body(
                "PATCH",
                "/device",
                READ_ONLY,
                serde_json::json!({ "name": "x" }),
            ),
        )
        .await;
    assert_eq!(StatusCode::CONFLICT, status);
    assert_eq!(sentence, body["error"]);

    for (method, uri) in [
        ("DELETE", "/device"),
        ("GET", "/link/codes/anything"),
        ("DELETE", "/link/codes/anything"),
    ] {
        let (status, body) = server
            .send_to(state.clone(), empty(method, uri, READ_ONLY))
            .await;
        assert_eq!(StatusCode::CONFLICT, status, "{method} {uri}");
        assert_eq!(sentence, body["error"]);
    }
}

#[tokio::test]
async fn an_old_key_with_every_right_may_issue_a_code() {
    let server = Server::new();
    let keys = Keys::parse(Some("admin:0123456789abcdef:read,import,delete")).unwrap();
    let state = through_a_tunnel(AppState::new(Arc::clone(&server.db), keys));
    let (status, body) = server
        .send_to(state, empty("POST", "/link/codes", "0123456789abcdef"))
        .await;
    assert_eq!(StatusCode::CREATED, status, "{body}");
    assert!(body["code"].is_string());
}

#[tokio::test]
async fn a_read_only_old_key_cannot_issue_a_code() {
    let server = Server::new();
    let (status, _) = server
        .send_to(server.state(), empty("POST", "/link/codes", READ_ONLY))
        .await;
    assert_eq!(StatusCode::FORBIDDEN, status);
    let rows = server
        .db
        .read(|cx| cx.query_one("SELECT COUNT(*) FROM link_code", [], |r| r.get::<_, i64>(0)))
        .unwrap();
    assert_eq!(Some(0), rows, "no code was made");
}

#[tokio::test]
async fn health_says_whether_the_library_is_linked() {
    let server = Server::new();
    let (_, body) = server.send_to(keyless(&server), anonymous("/health")).await;
    assert_eq!(
        serde_json::json!(false),
        body["linked"],
        "open on the loopback"
    );

    server.linked_device("Liseuse");
    let (_, body) = server.send_to(keyless(&server), anonymous("/health")).await;
    assert_eq!(serde_json::json!(true), body["linked"]);

    // Old keys alone close it too: nobody reaching it without one is a reader.
    let other = Server::new();
    let (_, body) = other.send_to(other.state(), anonymous("/health")).await;
    assert_eq!(serde_json::json!(true), body["linked"]);
}

// ------------------------------------------------------------- relaying

// A person on the machine that runs the server opened « Relier un autre appareil » and got
// a window explaining 127.0.0.1, tunnels and sudo: the client built the link from its own
// address, which is the loopback there — and through an SSH tunnel too, where it named the
// tunnel. The server is the one that knows which addresses another device can reach, so it
// builds the link, and says when there is none.

/// Leaf's own certificate, as a link carries its fingerprint.
const OWN: &str = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

/// A TLS server on 127.0.0.2 — https, since only 127.0.0.1, localhost and ::1 are the
/// loopback a link speaks http to — presenting a certificate generated in `dir`. Its
/// fingerprint, its port, and the handle that stops it.
async fn presenting(
    dir: &std::path::Path,
) -> (String, u16, axum_server::Handle<std::net::SocketAddr>) {
    let tls = Tls::of(
        &dir.join("leaf.crt"),
        &dir.join("leaf.key"),
        &["127.0.0.2".to_string()],
    )
    .await
    .expect("generating");
    let listener = std::net::TcpListener::bind("127.0.0.2:0").unwrap();
    let port = listener.local_addr().unwrap().port();
    listener.set_nonblocking(true).unwrap();
    let handle = axum_server::Handle::new();
    let server = axum_server::from_tcp_rustls(listener, tls.config.clone())
        .unwrap()
        .handle(handle.clone())
        .serve(axum::Router::new().into_make_service());
    tokio::spawn(server);
    (tls.fingerprint_hex, port, handle)
}

async fn settled_off_the_runtime(state: AppState, reach: Reach) -> AppState {
    // `settle` asks the address, which here is served by this same runtime.
    let relay = tokio::task::spawn_blocking(move || reach.settle())
        .await
        .unwrap();
    state.relaying(relay)
}

#[tokio::test(flavor = "multi_thread")]
async fn the_public_address_is_probed_once_at_start_and_its_answer_kept() {
    let server = Server::new();
    let key = server.linked_device("PC du salon");
    let dir = tempfile::tempdir().unwrap();
    let (own, port, handle) = presenting(dir.path()).await;
    let state = settled_off_the_runtime(
        server.state(),
        Reach {
            public_url: Some(format!("https://127.0.0.2:{port}")),
            ..Reach::listening_on("0.0.0.0", 8081, Some(own.clone()))
        },
    )
    .await;
    // Gone after the start: asked again, it would be unreachable and pin nothing.
    handle.shutdown();

    for _ in 0..2 {
        let (status, issued) = server
            .send_to(state.clone(), empty("POST", "/link/codes", &key))
            .await;
        assert_eq!(StatusCode::CREATED, status, "{issued}");
        let code = issued["code"].as_str().unwrap().replace('-', "");
        assert_eq!(
            format!("leaf://127.0.0.2:{port}/lier/{code}?empreinte={own}"),
            issued["link"]
        );
        assert_eq!(serde_json::json!(false), issued["localNetwork"]);
    }
}

#[tokio::test]
async fn a_public_address_the_server_cannot_verify_gets_a_link_that_pins_nothing() {
    // Leaf on the network, a proxy elsewhere holding a public certificate for the public
    // name, and no way from the server back to that name: pinning Leaf's own certificate
    // there gave every phone a certificate alarm for a certificate it never sees. With no
    // fingerprint the client trusts only what a public authority signed — safe whatever
    // answers there.
    let server = Server::new();
    let key = server.linked_device("PC du salon");
    // `.invalid` never resolves.
    let state = settled(
        server.state(),
        Reach {
            public_url: Some("https://leaf.invalid".into()),
            ..Reach::listening_on("127.0.0.1", 8081, Some(OWN.into()))
        },
    );

    let (status, issued) = server
        .send_to(state, empty("POST", "/link/codes", &key))
        .await;
    assert_eq!(StatusCode::CREATED, status, "{issued}");
    let code = issued["code"].as_str().unwrap().replace('-', "");
    assert_eq!(format!("leaf://leaf.invalid/lier/{code}"), issued["link"]);
    assert_eq!(serde_json::json!(false), issued["localNetwork"]);
}

#[tokio::test(flavor = "multi_thread")]
async fn a_public_address_presenting_a_certificate_not_leafs_gets_a_link_that_pins_nothing() {
    // Reached from inside, the public address may land on the router's own page: its
    // certificate is no proxy's, and a link must not pin it.
    let server = Server::new();
    let key = server.linked_device("PC du salon");
    let dir = tempfile::tempdir().unwrap();
    let (theirs, port, _handle) = presenting(dir.path()).await;
    assert_ne!(theirs, OWN);
    let state = settled_off_the_runtime(
        server.state(),
        Reach {
            public_url: Some(format!("https://127.0.0.2:{port}")),
            ..Reach::listening_on("0.0.0.0", 8081, Some(OWN.into()))
        },
    )
    .await;

    let (status, issued) = server
        .send_to(state, empty("POST", "/link/codes", &key))
        .await;
    assert_eq!(StatusCode::CREATED, status, "{issued}");
    let link = issued["link"].as_str().unwrap();
    assert!(!link.contains("empreinte"), "{link}");
}

#[tokio::test]
async fn a_tunnel_declared_as_the_public_address_is_the_address_in_the_link() {
    let server = Server::new();
    let key = server.linked_device("PC du salon");
    let (status, issued) = server
        .send_to(
            through_a_tunnel(server.state()),
            empty("POST", "/link/codes", &key),
        )
        .await;
    assert_eq!(StatusCode::CREATED, status, "{issued}");
    let code = issued["code"].as_str().unwrap().replace('-', "");
    assert_eq!(format!("leaf://127.0.0.1:9099/lier/{code}"), issued["link"]);
    assert_eq!(serde_json::json!(false), issued["localNetwork"]);
}

#[tokio::test]
async fn without_a_public_address_the_link_names_a_network_address_and_says_so() {
    let server = Server::new();
    let key = server.linked_device("PC du salon");
    let state = settled(
        server.state().bound_to("192.168.1.20"),
        Reach::listening_on("192.168.1.20", 8081, Some(OWN.into())),
    );

    let (status, issued) = server
        .send_to(state, empty("POST", "/link/codes", &key))
        .await;
    assert_eq!(StatusCode::CREATED, status, "{issued}");
    let code = issued["code"].as_str().unwrap().replace('-', "");
    assert_eq!(
        format!("leaf://192.168.1.20:8081/lier/{code}?empreinte={OWN}"),
        issued["link"]
    );
    assert_eq!(serde_json::json!(true), issued["localNetwork"]);
}

#[tokio::test]
async fn a_server_bound_to_a_public_address_does_not_call_it_a_local_network() {
    // The bound address is its own label then, and a label said « local network » for it.
    let server = Server::new();
    let key = server.linked_device("PC du salon");
    let state = settled(
        server.state().bound_to("203.0.113.7"),
        Reach::listening_on("203.0.113.7", 8081, Some(OWN.into())),
    );
    let (status, issued) = server
        .send_to(state, empty("POST", "/link/codes", &key))
        .await;
    assert_eq!(StatusCode::CREATED, status, "{issued}");
    assert_eq!(serde_json::json!(false), issued["localNetwork"]);
}

#[tokio::test]
async fn a_server_only_its_own_machine_reaches_makes_no_code_for_another_device() {
    let server = Server::new();
    let key = server.linked_device("PC du salon");
    let state = settled(server.state(), Reach::listening_on("127.0.0.1", 8081, None));

    let (status, body) = server
        .send_to(state, empty("POST", "/link/codes", &key))
        .await;
    assert_eq!(StatusCode::CONFLICT, status, "{body}");
    assert_eq!("unreachable", body["error"]);
    let rows = server
        .db
        .read(|cx| cx.query_one("SELECT COUNT(*) FROM link_code", [], |r| r.get::<_, i64>(0)))
        .unwrap();
    assert_eq!(
        Some(0),
        rows,
        "no code was made for a link nobody could follow"
    );
}

#[tokio::test]
async fn health_tells_a_linked_device_whether_another_one_can_be_linked() {
    let server = Server::new();
    let key = server.linked_device("PC du salon");
    let keyed = |key: &str| request("GET", "/health", key).body(Body::empty()).unwrap();

    let only_here = settled(server.state(), Reach::listening_on("127.0.0.1", 8081, None));
    let (_, body) = server.send_to(only_here, keyed(&key)).await;
    assert_eq!(serde_json::json!(false), body["relay"], "{body}");

    let (_, body) = server
        .send_to(through_a_tunnel(server.state()), keyed(&key))
        .await;
    assert_eq!(serde_json::json!(true), body["relay"], "{body}");

    let network = settled(
        server.state().bound_to("192.168.1.20"),
        Reach::listening_on("192.168.1.20", 8081, Some(OWN.into())),
    );
    let (_, body) = server.send_to(network, keyed(&key)).await;
    assert_eq!(serde_json::json!(true), body["relay"], "{body}");
}

#[tokio::test]
async fn health_without_a_key_says_no_more_than_it_did() {
    let server = Server::new();
    server.linked_device("PC du salon");
    let (status, body) = server
        .send_to(through_a_tunnel(server.state()), anonymous("/health"))
        .await;
    assert_eq!(StatusCode::OK, status);
    let mut fields: Vec<&str> = body
        .as_object()
        .unwrap()
        .keys()
        .map(String::as_str)
        .collect();
    fields.sort_unstable();
    assert_eq!(vec!["api", "format", "library", "linked", "status"], fields);
}

#[tokio::test]
async fn a_wrong_key_on_health_hears_nothing_more_and_counts_against_the_throttle() {
    let server = Server::new();
    server.linked_device("Liseuse");
    let state = through_a_tunnel(server.state());
    let wrong = "A".repeat(43);
    for _ in 0..10 {
        let (status, body) = server
            .send_to(
                state.clone(),
                request("GET", "/health", &wrong)
                    .body(Body::empty())
                    .unwrap(),
            )
            .await;
        assert_eq!(StatusCode::OK, status, "health still answers");
        assert!(body.get("relay").is_none(), "{body}");
    }
    let (status, _) = server
        .send_to(
            state,
            request("GET", "/series", &wrong)
                .body(Body::empty())
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::TOO_MANY_REQUESTS, status);
}
