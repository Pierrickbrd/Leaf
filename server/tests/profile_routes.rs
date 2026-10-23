//! The routes of profiles, the code, reading preferences and devices, one test per row of the
//! design's two tables « Les profils » and « Les appareils et le reste ».
//!
//! « Oui » means the server accepts, « non » that it refuses: the client hiding a button is
//! the other half of each row, and not what is tested here.

mod common;

use std::sync::Arc;

use axum::body::Body;
use axum::http::{Request, StatusCode};
use common::{json_body, request, Server, IMPORTER, READ_ONLY};
use http_body_util::BodyExt;
use leaf_server::api::routes::{router, AppState};
use leaf_server::store::profiles::{CodeScope, NewProfile, Profiles, Rights};
use serde_json::{json, Value};
use tower::ServiceExt;

const NOW: i64 = 1_790_000_000_000;
const CODE: &str = "2468";

/// A principal on « Portable », a reader without rights on « Liseuse », a child restricted to
/// ten on « Tablette », and the code that lets the child out.
struct Household {
    server: Server,
    /// One state for the whole test, so the brakes count across requests as they do in the
    /// server.
    state: AppState,
    principal: String,
    reader: String,
    child: String,
    camille: String,
    leo: String,
}

impl Household {
    fn new(scope: CodeScope) -> Self {
        let server = Server::new();
        let profiles = Profiles::new(Arc::clone(&server.db));
        profiles.set_code(CODE, scope).unwrap().unwrap();
        let made = |name: &str, color: &str, restricted: bool| {
            profiles
                .create(
                    NewProfile {
                        name: name.into(),
                        color: color.into(),
                        restricted,
                        max_age: restricted.then_some(10),
                        rights: Rights::default(),
                    },
                    NOW,
                )
                .unwrap()
                .unwrap()
                .id
        };
        let camille = made("Camille", "bleu", false);
        let leo = made("Léo", "corail", true);
        let principal = server.linked_device("Portable");
        let reader = server.linked_device("Liseuse");
        let child = server.linked_device("Tablette");
        profiles
            .place(&device_id(&server, "Liseuse"), &camille, false)
            .unwrap()
            .unwrap();
        profiles
            .place(&device_id(&server, "Tablette"), &leo, false)
            .unwrap()
            .unwrap();
        let state = server.state();
        Household {
            server,
            state,
            principal,
            reader,
            child,
            camille,
            leo,
        }
    }

    fn profiles(&self) -> Profiles {
        Profiles::new(Arc::clone(&self.server.db))
    }

    async fn call(
        &self,
        method: &str,
        uri: &str,
        key: &str,
        body: Option<Value>,
    ) -> (StatusCode, Value) {
        let (status, _, said) = self.raw(method, uri, key, body).await;
        (status, said)
    }

    async fn raw(
        &self,
        method: &str,
        uri: &str,
        key: &str,
        body: Option<Value>,
    ) -> (StatusCode, axum::http::HeaderMap, Value) {
        let builder = request(method, uri, key);
        let request: Request<Body> = match body {
            Some(body) => builder
                .header("Content-Type", "application/json")
                .body(json_body(body))
                .unwrap(),
            None => builder.body(Body::empty()).unwrap(),
        };
        let response = router(self.state.clone())
            .oneshot(request)
            .await
            .expect("a response");
        let status = response.status();
        let headers = response.headers().clone();
        let bytes = response.into_body().collect().await.unwrap().to_bytes();
        let said = if bytes.is_empty() {
            Value::Null
        } else {
            serde_json::from_slice(&bytes).unwrap_or(Value::Null)
        };
        (status, headers, said)
    }

    async fn get(&self, uri: &str, key: &str) -> (StatusCode, Value) {
        self.call("GET", uri, key, None).await
    }

    async fn switch(&self, key: &str, to: &str, code: Option<&str>) -> (StatusCode, Value) {
        let mut body = json!({"id": to});
        if let Some(code) = code {
            body["code"] = json!(code);
        }
        self.call("PUT", "/profiles/active", key, Some(body)).await
    }

    fn name_of(&self, id: &str) -> String {
        self.profiles().get(id).unwrap().expect("the profile").name
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

fn a_reader_named(name: &str, color: &str) -> Value {
    json!({"name": name, "color": color, "kind": "reader"})
}

fn code_error() -> Value {
    json!({"error": "code"})
}

// ---------------------------------------------------------- « Qui lit ? »

/// Ouvrir « Qui lit ? » : oui, oui, oui.
#[tokio::test]
async fn every_profile_opens_who_reads() {
    let home = Household::new(CodeScope::Restricted);
    for (key, active) in [
        (&home.principal, "default"),
        (&home.reader, home.camille.as_str()),
        (&home.child, home.leo.as_str()),
    ] {
        let (status, said) = home.get("/profiles", key).await;
        assert_eq!(StatusCode::OK, status, "{said}");
        assert_eq!(active, said["active"], "{said}");
        assert_eq!(false, said["kept"]);
        assert_eq!(false, said["choosing"]);
        assert_eq!(false, said["held"]);
        assert_eq!("restricted", said["code"]);
        let profiles = said["profiles"].as_array().expect("a list");
        assert_eq!(3, profiles.len(), "{said}");
        assert_eq!(
            json!({
                "id": "default", "name": "Principal", "color": "emeraude", "kind": "principal",
                "rights": {"import": true, "edit": true, "delete": true}
            }),
            profiles[0]
        );
        assert_eq!(
            json!({
                "id": home.leo, "name": "Léo", "color": "corail", "kind": "restricted",
                "maxAge": 10, "rights": {"import": false, "edit": false, "delete": false}
            }),
            profiles[2]
        );
    }
}

/// A device whose profile was deleted under it is refused everything but the two routes that
/// let it choose — and those two must answer it, or it is stuck.
#[tokio::test]
async fn profiles_answer_a_device_that_must_choose() {
    let home = Household::new(CodeScope::Restricted);
    let profiles = home.profiles();
    profiles.delete(&home.camille).unwrap().unwrap();
    profiles.delete(&home.leo).unwrap().unwrap();

    for (key, held) in [(&home.reader, false), (&home.child, true)] {
        let (status, said) = home.get("/profiles", key).await;
        assert_eq!(StatusCode::OK, status, "{said}");
        assert_eq!(true, said["choosing"], "{said}");
        assert_eq!(held, said["held"], "{said}");
        for (method, uri) in [
            ("GET", "/profile/reading"),
            ("GET", "/devices"),
            ("DELETE", "/profiles/code"),
        ] {
            let (status, said) = home.call(method, uri, key, None).await;
            assert_eq!(StatusCode::CONFLICT, status, "{method} {uri}: {said}");
            assert_eq!(json!({"error": "choose a profile"}), said);
        }
    }

    // The reader's device chooses freely; the child's, held, needs the code to leave.
    let (status, said) = home.switch(&home.reader, "default", None).await;
    assert_eq!(StatusCode::OK, status, "{said}");
    assert_eq!(false, said["choosing"]);
    let (status, said) = home.switch(&home.child, "default", None).await;
    assert_eq!(StatusCode::FORBIDDEN, status);
    assert_eq!(code_error(), said);
    let (status, said) = home.switch(&home.child, "default", Some(CODE)).await;
    assert_eq!(StatusCode::OK, status, "{said}");
    assert_eq!(false, said["held"]);
    let (status, _) = home.get("/profile/reading", &home.child).await;
    assert_eq!(StatusCode::OK, status, "seated again, it is served");
}

// ---------------------------------------------------------- changing profile

/// Passer à un lecteur ou à un restreint : libre, libre, code.
#[tokio::test]
async fn the_principal_and_a_reader_switch_to_a_reader_or_a_restricted_profile_freely() {
    let home = Household::new(CodeScope::Restricted);
    let (status, said) = home.switch(&home.principal, &home.camille, None).await;
    assert_eq!(StatusCode::OK, status, "{said}");
    assert_eq!(home.camille, said["active"]);
    let (status, said) = home.switch(&home.reader, &home.leo, None).await;
    assert_eq!(StatusCode::OK, status, "{said}");
    assert_eq!(home.leo, said["active"]);
}

#[tokio::test]
async fn a_restricted_profile_leaves_only_with_the_code() {
    let home = Household::new(CodeScope::Restricted);
    let (status, said) = home.switch(&home.child, &home.camille, None).await;
    assert_eq!(StatusCode::FORBIDDEN, status);
    assert_eq!(code_error(), said);
    let (status, said) = home.switch(&home.child, &home.camille, Some("1357")).await;
    assert_eq!(StatusCode::FORBIDDEN, status);
    assert_eq!(code_error(), said);
    let (status, said) = home.switch(&home.child, &home.camille, Some(CODE)).await;
    assert_eq!(StatusCode::OK, status, "{said}");
    assert_eq!(home.camille, said["active"]);
}

/// Passer au profil principal : libre ; code si le principal l'a choisi ; code.
#[tokio::test]
async fn a_reader_reaches_the_principal_with_the_code_only_when_it_guards_everything() {
    let home = Household::new(CodeScope::Restricted);
    let (status, said) = home.switch(&home.reader, "default", None).await;
    assert_eq!(StatusCode::OK, status, "{said}");

    let home = Household::new(CodeScope::Everything);
    let (status, said) = home.switch(&home.reader, "default", None).await;
    assert_eq!(StatusCode::FORBIDDEN, status);
    assert_eq!(code_error(), said);
    let (status, said) = home.switch(&home.reader, "default", Some(CODE)).await;
    assert_eq!(StatusCode::OK, status, "{said}");
    // The principal reaching itself again is free whatever the code guards — from a device
    // that retains it.
    home.profiles()
        .place(&device_id(&home.server, "Portable"), "default", true)
        .unwrap()
        .unwrap();
    let (status, said) = home.switch(&home.principal, "default", None).await;
    assert_eq!(StatusCode::OK, status, "{said}");
    // And a restricted profile, with one code and not two.
    let (status, said) = home.switch(&home.child, "default", Some(CODE)).await;
    assert_eq!(StatusCode::OK, status, "{said}");
}

/// A device left on the principal without « Retenir » reopened on it for anyone, and took any
/// code on the way back because none was checked.
#[tokio::test]
async fn an_unretained_principal_seat_is_refused_a_wrong_code() {
    let home = Household::new(CodeScope::Everything);
    let (status, said) = home.switch(&home.principal, "default", Some("1357")).await;
    assert_eq!(StatusCode::FORBIDDEN, status);
    assert_eq!(code_error(), said);
}

#[tokio::test]
async fn switching_to_a_profile_that_is_not_there_is_a_404() {
    let home = Household::new(CodeScope::Restricted);
    let (status, _) = home.switch(&home.principal, "nobody", None).await;
    assert_eq!(StatusCode::NOT_FOUND, status);
}

// ---------------------------------------------------------- the brake

/// [RF 4] Ten wrong codes and the route stops listening, even to the right one; the brake on
/// wrong keys is not the one that counts.
#[tokio::test]
async fn a_wrong_code_counts_towards_the_throttle() {
    let home = Household::new(CodeScope::Restricted);
    for _ in 0..10 {
        let (status, said) = home.switch(&home.child, &home.camille, Some("0000")).await;
        assert_eq!(StatusCode::FORBIDDEN, status, "{said}");
    }
    let (status, headers, _) = home
        .raw(
            "PUT",
            "/profiles/active",
            &home.child,
            Some(json!({"id": home.camille, "code": CODE})),
        )
        .await;
    assert_eq!(StatusCode::TOO_MANY_REQUESTS, status);
    assert!(headers.contains_key("retry-after"));
    // The same device still reads: the key is good, only the code is held back.
    let (status, _) = home.get("/profile/reading", &home.child).await;
    assert_eq!(StatusCode::OK, status);
    // And another device is not held back by this one's guesses.
    let (status, said) = home.switch(&home.reader, &home.leo, None).await;
    assert_eq!(StatusCode::OK, status, "{said}");
}

/// [RF 4] Guesses sent together are counted together: a check before Argon2 and a count
/// after let fifteen through at once, and a right one among them wiped the block.
#[tokio::test(flavor = "multi_thread", worker_threads = 4)]
async fn wrong_codes_sent_at_once_are_held_to_ten() {
    let home = Household::new(CodeScope::Restricted);
    let guesses = (0..15).map(|_| home.switch(&home.child, &home.camille, Some("0000")));
    let answers = futures_util::future::join_all(guesses).await;
    let refused = answers
        .iter()
        .filter(|(status, _)| *status == StatusCode::TOO_MANY_REQUESTS)
        .count();
    let judged = answers
        .iter()
        .filter(|(status, _)| *status == StatusCode::FORBIDDEN)
        .count();
    assert!(refused >= 5, "{refused} refused, {judged} judged");
    assert!(judged <= 10, "{judged} judged");
    // Blocked now: the right code is not heard, and does not lift the block either.
    for _ in 0..2 {
        let (status, said) = home.switch(&home.child, &home.camille, Some(CODE)).await;
        assert_eq!(StatusCode::TOO_MANY_REQUESTS, status, "{said}");
    }
    assert_eq!(
        home.leo,
        home.profiles()
            .seat(&device_id(&home.server, "Tablette"))
            .unwrap()
            .profile_id
    );
}

#[tokio::test]
async fn a_wrong_code_to_add_a_reader_counts_on_the_same_brake() {
    let home = Household::new(CodeScope::Everything);
    for _ in 0..10 {
        let mut body = a_reader_named("Anna", "rose");
        body["principalCode"] = json!("0000");
        let (status, said) = home
            .call("POST", "/profiles", &home.reader, Some(body))
            .await;
        assert_eq!(StatusCode::FORBIDDEN, status, "{said}");
    }
    let (status, _) = home.switch(&home.reader, "default", Some(CODE)).await;
    assert_eq!(StatusCode::TOO_MANY_REQUESTS, status);
}

#[tokio::test]
async fn the_right_code_clears_the_throttle_and_a_free_switch_does_not() {
    let home = Household::new(CodeScope::Everything);
    for _ in 0..9 {
        home.switch(&home.reader, "default", Some("0000")).await;
    }
    // A switch that needed no code, with text in the field: nothing was checked, so nothing
    // is forgiven.
    let (status, _) = home.switch(&home.reader, &home.leo, Some("0000")).await;
    assert_eq!(StatusCode::OK, status);
    home.profiles()
        .place(&device_id(&home.server, "Liseuse"), &home.camille, false)
        .unwrap()
        .unwrap();
    home.switch(&home.reader, "default", Some("0000")).await;
    let (status, _) = home.switch(&home.reader, "default", Some(CODE)).await;
    assert_eq!(
        StatusCode::TOO_MANY_REQUESTS,
        status,
        "the tenth wrong code"
    );

    let home = Household::new(CodeScope::Everything);
    for _ in 0..9 {
        home.switch(&home.reader, "default", Some("0000")).await;
    }
    let (status, _) = home.switch(&home.reader, "default", Some(CODE)).await;
    assert_eq!(StatusCode::OK, status);
    home.profiles()
        .place(&device_id(&home.server, "Liseuse"), &home.camille, false)
        .unwrap()
        .unwrap();
    for _ in 0..9 {
        let (status, _) = home.switch(&home.reader, "default", Some("0000")).await;
        assert_eq!(StatusCode::FORBIDDEN, status, "the slate was cleared");
    }
}

/// [R7, from the rights] A key that works asking for a right it has not got is guessing
/// nothing: it is refused for ever, and never throttled.
#[tokio::test]
async fn a_device_lacking_a_right_keeps_being_refused_and_never_throttled() {
    let home = Household::new(CodeScope::Restricted);
    for attempt in 0..12 {
        let (status, said) = home.get("/devices", &home.reader).await;
        assert_eq!(StatusCode::FORBIDDEN, status, "attempt {attempt}: {said}");
        let (status, said) = home.call("POST", "/scan", &home.child, None).await;
        assert_eq!(StatusCode::FORBIDDEN, status, "attempt {attempt}: {said}");
    }
}

// ---------------------------------------------------------- adding a profile

/// Ajouter un profil lecteur : oui ; oui, avec le code si le principal l'a choisi, et le
/// nouveau lecteur n'a aucun droit ; non.
#[tokio::test]
async fn the_principal_adds_a_reader_with_the_rights_it_ticks() {
    let home = Household::new(CodeScope::Everything);
    let mut body = a_reader_named("Anna", "rose");
    body["rights"] = json!({"import": true, "edit": false, "delete": true});
    let (status, said) = home
        .call("POST", "/profiles", &home.principal, Some(body))
        .await;
    assert_eq!(StatusCode::CREATED, status, "{said}");
    assert_eq!("Anna", said["name"]);
    assert_eq!("reader", said["kind"]);
    assert_eq!(
        json!({"import": true, "edit": false, "delete": true}),
        said["rights"]
    );
    assert!(said.get("maxAge").is_none(), "{said}");
}

#[tokio::test]
async fn a_reader_adds_a_reader_without_rights() {
    let home = Household::new(CodeScope::Restricted);
    let mut body = a_reader_named("Anna", "rose");
    body["rights"] = json!({"import": true, "edit": true, "delete": true});
    let (status, said) = home
        .call("POST", "/profiles", &home.reader, Some(body))
        .await;
    assert_eq!(StatusCode::CREATED, status, "{said}");
    assert_eq!(
        json!({"import": false, "edit": false, "delete": false}),
        said["rights"]
    );
    // After adding, the reader is still the reader: adding is not switching.
    let (_, who) = home.get("/profiles", &home.reader).await;
    assert_eq!(home.camille, who["active"]);
}

#[tokio::test]
async fn a_reader_adds_a_reader_with_the_code_when_the_principal_asks_for_it() {
    let home = Household::new(CodeScope::Everything);
    let (status, said) = home
        .call(
            "POST",
            "/profiles",
            &home.reader,
            Some(a_reader_named("Anna", "rose")),
        )
        .await;
    assert_eq!(StatusCode::FORBIDDEN, status);
    assert_eq!(code_error(), said);
    let mut body = a_reader_named("Anna", "rose");
    body["principalCode"] = json!("1357");
    let (status, said) = home
        .call("POST", "/profiles", &home.reader, Some(body))
        .await;
    assert_eq!(StatusCode::FORBIDDEN, status);
    assert_eq!(code_error(), said);
    let mut body = a_reader_named("Anna", "rose");
    body["principalCode"] = json!(CODE);
    let (status, said) = home
        .call("POST", "/profiles", &home.reader, Some(body))
        .await;
    assert_eq!(StatusCode::CREATED, status, "{said}");
    assert_eq!("reader", said["kind"]);
}

#[tokio::test]
async fn a_restricted_profile_adds_nobody() {
    let home = Household::new(CodeScope::Restricted);
    let (status, said) = home
        .call(
            "POST",
            "/profiles",
            &home.child,
            Some(a_reader_named("Anna", "rose")),
        )
        .await;
    assert_eq!(StatusCode::FORBIDDEN, status, "{said}");
    assert_eq!(3, home.profiles().list().unwrap().len(), "nobody added");
}

// ---------------------------------------------------------- restricting

/// Créer un profil restreint, restreindre un lecteur : oui, non, non.
#[tokio::test]
async fn the_principal_creates_a_restricted_profile_and_the_code_with_it() {
    let server = Server::new();
    let state = server.state();
    let principal = server.linked_device("Portable");
    let send = |method: &'static str, uri: &'static str, body: Value| {
        let state = state.clone();
        let principal = principal.clone();
        async move {
            let response = router(state)
                .oneshot(
                    request(method, uri, &principal)
                        .header("Content-Type", "application/json")
                        .body(json_body(body))
                        .unwrap(),
                )
                .await
                .unwrap();
            let status = response.status();
            let bytes = response.into_body().collect().await.unwrap().to_bytes();
            (
                status,
                serde_json::from_slice::<Value>(&bytes).unwrap_or(Value::Null),
            )
        }
    };
    let child = json!({"name": "Léo", "color": "corail", "kind": "restricted", "maxAge": 10});

    // No code yet, and none given: a restricted profile nobody could leave is refused.
    let (status, said) = send("POST", "/profiles", child.clone()).await;
    assert_eq!(StatusCode::CONFLICT, status, "{said}");
    assert_eq!(json!({"error": "code required"}), said);
    // Two that differ: a typing slip would lock the principal out.
    let mut body = child.clone();
    body["code"] = json!("2468");
    body["codeAgain"] = json!("2469");
    let (status, _) = send("POST", "/profiles", body).await;
    assert_eq!(StatusCode::BAD_REQUEST, status);
    assert_eq!(None, Profiles::new(Arc::clone(&server.db)).code().unwrap());

    let mut body = child.clone();
    body["code"] = json!("2468");
    body["codeAgain"] = json!("2468");
    let (status, said) = send("POST", "/profiles", body).await;
    assert_eq!(StatusCode::CREATED, status, "{said}");
    assert_eq!("restricted", said["kind"]);
    assert_eq!(10, said["maxAge"]);
    let profiles = Profiles::new(Arc::clone(&server.db));
    assert_eq!(Some(CodeScope::Restricted), profiles.code().unwrap());
    assert!(profiles.check_code("2468").unwrap());
}

#[tokio::test]
async fn a_name_taken_by_the_restricted_profile_takes_back_the_code_made_for_it() {
    let server = Server::new();
    let principal = server.linked_device("Portable");
    let body = json!({
        "name": "Principal", "color": "corail", "kind": "restricted", "maxAge": 10,
        "code": "2468", "codeAgain": "2468"
    });
    let (status, said) = server
        .send(
            request("POST", "/profiles", &principal)
                .header("Content-Type", "application/json")
                .body(json_body(body))
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::CONFLICT, status);
    assert_eq!(json!({"error": "name taken"}), said);
    assert_eq!(
        None,
        Profiles::new(Arc::clone(&server.db)).code().unwrap(),
        "a code made for a profile that was refused is not left behind"
    );
}

#[tokio::test]
async fn a_reader_cannot_restrict_anyone() {
    let home = Household::new(CodeScope::Restricted);
    let body = json!({"name": "Anna", "color": "rose", "kind": "restricted", "maxAge": 8});
    let (status, said) = home
        .call("POST", "/profiles", &home.reader, Some(body))
        .await;
    assert_eq!(StatusCode::FORBIDDEN, status, "{said}");
    let (status, said) = home
        .call(
            "PATCH",
            &format!("/profiles/{}", home.camille),
            &home.reader,
            Some(json!({"kind": "restricted", "maxAge": 8})),
        )
        .await;
    assert_eq!(StatusCode::FORBIDDEN, status, "{said}");
    let (status, said) = home
        .call(
            "PATCH",
            &format!("/profiles/{}", home.camille),
            &home.child,
            Some(json!({"kind": "restricted", "maxAge": 8})),
        )
        .await;
    assert_eq!(StatusCode::FORBIDDEN, status, "{said}");
    // Another reader than itself: the same refusal, and Anna is still a reader.
    let anna = home
        .profiles()
        .create(
            NewProfile {
                name: "Anna".into(),
                color: "rose".into(),
                restricted: false,
                max_age: None,
                rights: Rights::default(),
            },
            NOW,
        )
        .unwrap()
        .unwrap();
    let (status, said) = home
        .call(
            "PATCH",
            &format!("/profiles/{}", anna.id),
            &home.reader,
            Some(json!({"kind": "restricted", "maxAge": 8})),
        )
        .await;
    assert_eq!(StatusCode::FORBIDDEN, status, "{said}");
    assert_eq!(
        leaf_server::store::profiles::Kind::Reader,
        home.profiles().get(&anna.id).unwrap().unwrap().kind
    );
}

/// Restricting a reader needs the code as creating a restricted profile does; the edit window
/// has no code fields, so the client makes it first with `PUT /profiles/code`.
#[tokio::test]
async fn the_principal_restricting_a_reader_with_no_code_is_told_the_code_is_required() {
    let server = Server::new();
    let principal = server.linked_device("Portable");
    let camille = Profiles::new(Arc::clone(&server.db))
        .create(
            NewProfile {
                name: "Camille".into(),
                color: "bleu".into(),
                restricted: false,
                max_age: None,
                rights: Rights::default(),
            },
            NOW,
        )
        .unwrap()
        .unwrap();
    let (status, said) = server
        .send(
            request("PATCH", &format!("/profiles/{}", camille.id), &principal)
                .header("Content-Type", "application/json")
                .body(json_body(json!({"kind": "restricted", "maxAge": 10})))
                .unwrap(),
        )
        .await;
    assert_eq!(StatusCode::CONFLICT, status);
    assert_eq!(json!({"error": "code required"}), said);
}

#[tokio::test]
async fn an_age_that_is_not_a_small_whole_number_is_a_bad_age() {
    let home = Household::new(CodeScope::Restricted);
    for age in [json!(300), json!(-1), json!(3.5), json!(2)] {
        let body = json!({"name": "Anna", "color": "rose", "kind": "restricted", "maxAge": age});
        let (status, said) = home
            .call("POST", "/profiles", &home.principal, Some(body))
            .await;
        assert_eq!(StatusCode::BAD_REQUEST, status, "POST {age}: {said}");
        let (status, said) = home
            .call(
                "PATCH",
                &format!("/profiles/{}", home.leo),
                &home.principal,
                Some(json!({ "maxAge": age })),
            )
            .await;
        assert_eq!(StatusCode::BAD_REQUEST, status, "PATCH {age}: {said}");
    }
    assert_eq!(
        Some(10),
        home.profiles().get(&home.leo).unwrap().unwrap().max_age
    );
}

/// With a code already there, `code` and `codeAgain` are ignored — refusing them for not
/// matching would be refusing something nothing was going to use.
#[tokio::test]
async fn the_code_fields_are_ignored_once_there_is_a_code() {
    let home = Household::new(CodeScope::Restricted);
    let body = json!({
        "name": "Anna", "color": "rose", "kind": "restricted", "maxAge": 8,
        "code": "1111", "codeAgain": "2222"
    });
    let (status, said) = home
        .call("POST", "/profiles", &home.principal, Some(body))
        .await;
    assert_eq!(StatusCode::CREATED, status, "{said}");
    assert!(
        home.profiles().check_code(CODE).unwrap(),
        "the code is unchanged"
    );
}

// ---------------------------------------------------------- one's own name

/// Changer son nom et sa couleur : oui, oui, non.
#[tokio::test]
async fn a_reader_renames_and_recolours_itself() {
    let home = Household::new(CodeScope::Restricted);
    let (status, said) = home
        .call(
            "PATCH",
            &format!("/profiles/{}", home.camille),
            &home.reader,
            Some(json!({"name": "Cam", "color": "azur"})),
        )
        .await;
    assert_eq!(StatusCode::OK, status, "{said}");
    assert_eq!("Cam", said["name"]);
    assert_eq!("azur", said["color"]);
    let (status, said) = home
        .call(
            "PATCH",
            "/profiles/default",
            &home.principal,
            Some(json!({"name": "Maison"})),
        )
        .await;
    assert_eq!(StatusCode::OK, status, "{said}");
    assert_eq!("Maison", said["name"]);
}

#[tokio::test]
async fn a_reader_changes_nothing_else_of_its_own() {
    let home = Household::new(CodeScope::Restricted);
    let (status, said) = home
        .call(
            "PATCH",
            &format!("/profiles/{}", home.camille),
            &home.reader,
            Some(json!({"rights": {"import": true, "edit": true, "delete": true}})),
        )
        .await;
    assert_eq!(StatusCode::FORBIDDEN, status, "{said}");
    assert!(
        !home
            .profiles()
            .get(&home.camille)
            .unwrap()
            .unwrap()
            .rights
            .import
    );
}

#[tokio::test]
async fn a_restricted_profile_cannot_rename_itself() {
    let home = Household::new(CodeScope::Restricted);
    let (status, said) = home
        .call(
            "PATCH",
            &format!("/profiles/{}", home.leo),
            &home.child,
            Some(json!({"name": "Léon"})),
        )
        .await;
    assert_eq!(StatusCode::FORBIDDEN, status, "{said}");
    assert_eq!("Léo", home.name_of(&home.leo));
}

// ---------------------------------------------------------- another profile

/// Modifier ou supprimer un autre profil, cocher ses droits, fixer un âge : oui, non, non.
#[tokio::test]
async fn the_principal_edits_and_deletes_another_profile() {
    let home = Household::new(CodeScope::Restricted);
    let (status, said) = home
        .call(
            "PATCH",
            &format!("/profiles/{}", home.camille),
            &home.principal,
            Some(json!({"rights": {"import": true, "edit": true, "delete": false}})),
        )
        .await;
    assert_eq!(StatusCode::OK, status, "{said}");
    assert_eq!(
        json!({"import": true, "edit": true, "delete": false}),
        said["rights"]
    );
    let (status, said) = home
        .call(
            "PATCH",
            &format!("/profiles/{}", home.leo),
            &home.principal,
            Some(json!({"maxAge": 12})),
        )
        .await;
    assert_eq!(StatusCode::OK, status, "{said}");
    assert_eq!(12, said["maxAge"]);
    // Restricting a reader, with the code already there.
    let (status, said) = home
        .call(
            "PATCH",
            &format!("/profiles/{}", home.camille),
            &home.principal,
            Some(json!({"kind": "restricted", "maxAge": 14})),
        )
        .await;
    assert_eq!(StatusCode::OK, status, "{said}");
    assert_eq!("restricted", said["kind"]);

    let (status, _) = home
        .call(
            "DELETE",
            &format!("/profiles/{}", home.camille),
            &home.principal,
            None,
        )
        .await;
    assert_eq!(StatusCode::NO_CONTENT, status);
    assert!(home.profiles().get(&home.camille).unwrap().is_none());
}

#[tokio::test]
async fn the_principal_stays_the_principal() {
    let home = Household::new(CodeScope::Restricted);
    let (status, said) = home
        .call("DELETE", "/profiles/default", &home.principal, None)
        .await;
    assert_eq!(StatusCode::CONFLICT, status, "{said}");
    let (status, said) = home
        .call(
            "PATCH",
            "/profiles/default",
            &home.principal,
            Some(json!({"kind": "restricted", "maxAge": 10})),
        )
        .await;
    assert_eq!(StatusCode::CONFLICT, status, "{said}");
}

#[tokio::test]
async fn a_reader_neither_edits_nor_deletes_another_profile() {
    let home = Household::new(CodeScope::Restricted);
    for key in [&home.reader, &home.child] {
        let (status, said) = home
            .call(
                "PATCH",
                "/profiles/default",
                key,
                Some(json!({"name": "Moi"})),
            )
            .await;
        assert_eq!(StatusCode::FORBIDDEN, status, "{said}");
        let (status, said) = home
            .call("DELETE", &format!("/profiles/{}", home.leo), key, None)
            .await;
        assert_eq!(StatusCode::FORBIDDEN, status, "{said}");
    }
    let (status, said) = home
        .call(
            "PATCH",
            &format!("/profiles/{}", home.leo),
            &home.reader,
            Some(json!({"maxAge": 17})),
        )
        .await;
    assert_eq!(StatusCode::FORBIDDEN, status, "{said}");
    assert_eq!(
        Some(10),
        home.profiles().get(&home.leo).unwrap().unwrap().max_age
    );
}

#[tokio::test]
async fn an_unknown_profile_is_a_404() {
    let home = Household::new(CodeScope::Restricted);
    let (status, _) = home
        .call(
            "PATCH",
            "/profiles/nobody",
            &home.principal,
            Some(json!({"name": "X"})),
        )
        .await;
    assert_eq!(StatusCode::NOT_FOUND, status);
    let (status, _) = home
        .call("DELETE", "/profiles/nobody", &home.principal, None)
        .await;
    assert_eq!(StatusCode::NOT_FOUND, status);
}

#[tokio::test]
async fn names_and_colours_are_refused_with_their_reason() {
    let home = Household::new(CodeScope::Restricted);
    for (body, status, error) in [
        (
            a_reader_named("camille", "rose"),
            StatusCode::CONFLICT,
            Some("name taken"),
        ),
        (
            a_reader_named("Anna", "bleu"),
            StatusCode::CONFLICT,
            Some("colour taken"),
        ),
        (
            a_reader_named("Anna", "fuchsia"),
            StatusCode::BAD_REQUEST,
            None,
        ),
        (a_reader_named("   ", "rose"), StatusCode::BAD_REQUEST, None),
        (
            json!({"name": "Anna", "color": "rose", "kind": "restricted", "maxAge": 2}),
            StatusCode::BAD_REQUEST,
            None,
        ),
        (
            json!({"name": "Anna", "color": "rose", "kind": "principal"}),
            StatusCode::BAD_REQUEST,
            None,
        ),
    ] {
        let (got, said) = home
            .call("POST", "/profiles", &home.principal, Some(body.clone()))
            .await;
        assert_eq!(status, got, "{body}: {said}");
        if let Some(error) = error {
            assert_eq!(json!({ "error": error }), said, "{body}");
        }
    }
}

// ---------------------------------------------------------- the code

/// Créer, changer le code, choisir à quoi il sert : oui, non, non.
#[tokio::test]
async fn only_the_principal_sets_and_clears_the_code() {
    let home = Household::new(CodeScope::Restricted);
    let body = json!({"code": "13579", "codeAgain": "13579", "scope": "everything",
                      "currentCode": CODE});
    for key in [&home.reader, &home.child] {
        let (status, _) = home
            .call("PUT", "/profiles/code", key, Some(body.clone()))
            .await;
        assert_eq!(StatusCode::FORBIDDEN, status);
        let (status, _) = home.call("DELETE", "/profiles/code", key, None).await;
        assert_eq!(StatusCode::FORBIDDEN, status);
    }
    let (status, said) = home
        .call("PUT", "/profiles/code", &home.principal, Some(body))
        .await;
    assert_eq!(StatusCode::NO_CONTENT, status, "{said}");
    let profiles = home.profiles();
    assert_eq!(Some(CodeScope::Everything), profiles.code().unwrap());
    assert!(profiles.check_code("13579").unwrap());

    profiles.delete(&home.leo).unwrap().unwrap();
    let (status, _) = home
        .call(
            "DELETE",
            "/profiles/code",
            &home.principal,
            Some(json!({"currentCode": "13579"})),
        )
        .await;
    assert_eq!(StatusCode::NO_CONTENT, status);
    assert_eq!(None, profiles.code().unwrap());
    let (_, said) = home.get("/profiles", &home.principal).await;
    assert_eq!("none", said["code"]);
}

#[tokio::test]
async fn the_code_cannot_go_while_a_profile_is_restricted() {
    let home = Household::new(CodeScope::Restricted);
    let (status, said) = home
        .call(
            "DELETE",
            "/profiles/code",
            &home.principal,
            Some(json!({"currentCode": CODE})),
        )
        .await;
    assert_eq!(StatusCode::CONFLICT, status);
    assert_eq!(json!({"error": "a profile is restricted"}), said);
}

#[tokio::test]
async fn a_code_is_typed_twice_and_four_to_eight_characters_long() {
    let home = Household::new(CodeScope::Restricted);
    for body in [
        json!({"code": "1357", "codeAgain": "1358", "scope": "restricted", "currentCode": CODE}),
        json!({"code": "135", "codeAgain": "135", "scope": "restricted", "currentCode": CODE}),
        json!({"code": "123456789", "codeAgain": "123456789", "scope": "restricted",
               "currentCode": CODE}),
        json!({"code": "1357", "codeAgain": "1357", "scope": "sometimes", "currentCode": CODE}),
    ] {
        let (status, said) = home
            .call("PUT", "/profiles/code", &home.principal, Some(body.clone()))
            .await;
        assert_eq!(StatusCode::BAD_REQUEST, status, "{body}: {said}");
    }
    assert!(home.profiles().check_code(CODE).unwrap(), "unchanged");
}

/// Changing the code or taking it off asks the code it is now. A device that retains the
/// principal opens on it with no code, so being the principal proves nothing about knowing it:
/// whoever picked it up could otherwise replace the code and lock the household out.
#[tokio::test]
async fn changing_or_removing_the_code_asks_the_current_one() {
    let home = Household::new(CodeScope::Restricted);
    let profiles = home.profiles();
    let change = |current: serde_json::Value| {
        let mut body = json!({"code": "97531", "codeAgain": "97531", "scope": "restricted"});
        if !current.is_null() {
            body["currentCode"] = current;
        }
        body
    };
    for current in [json!(null), json!("0000"), json!("")] {
        let (status, said) = home
            .call(
                "PUT",
                "/profiles/code",
                &home.principal,
                Some(change(current.clone())),
            )
            .await;
        assert_eq!(StatusCode::FORBIDDEN, status, "{current}: {said}");
        assert_eq!(json!({"error": "code"}), said);
    }
    assert!(profiles.check_code(CODE).unwrap(), "nothing was changed");

    // Nor is the code taken off by whoever does not know it, with or without the body.
    for body in [None, Some(json!({})), Some(json!({"currentCode": "0000"}))] {
        let (status, said) = home
            .call("DELETE", "/profiles/code", &home.principal, body.clone())
            .await;
        assert_eq!(StatusCode::FORBIDDEN, status, "{body:?}: {said}");
    }
    assert_eq!(Some(CodeScope::Restricted), profiles.code().unwrap());

    let (status, said) = home
        .call(
            "PUT",
            "/profiles/code",
            &home.principal,
            Some(change(json!(CODE))),
        )
        .await;
    assert_eq!(StatusCode::NO_CONTENT, status, "{said}");
    assert!(profiles.check_code("97531").unwrap());
}

/// A guess at the current code is a guess at the lock like any other: it counts on the brake,
/// and ten stop the route listening, even to the right one.
#[tokio::test]
async fn a_wrong_current_code_counts_towards_the_throttle() {
    let home = Household::new(CodeScope::Restricted);
    let body = |current: &str| {
        json!({"code": "97531", "codeAgain": "97531", "scope": "restricted",
               "currentCode": current})
    };
    for _ in 0..10 {
        let (status, _) = home
            .call("PUT", "/profiles/code", &home.principal, Some(body("0000")))
            .await;
        assert_eq!(StatusCode::FORBIDDEN, status);
    }
    let (status, _, _) = home
        .raw("PUT", "/profiles/code", &home.principal, Some(body(CODE)))
        .await;
    assert_eq!(StatusCode::TOO_MANY_REQUESTS, status);
    assert!(home.profiles().check_code(CODE).unwrap());
}

/// With no code there is none to know: the first one is made with no current code, and removing
/// what is not there is as harmless as it was.
#[tokio::test]
async fn making_the_first_code_asks_no_current_one() {
    let home = Household::new(CodeScope::Restricted);
    let profiles = home.profiles();
    profiles.delete(&home.leo).unwrap().unwrap();
    profiles.clear_code().unwrap().unwrap();
    let (status, said) = home
        .call(
            "PUT",
            "/profiles/code",
            &home.principal,
            Some(json!({"code": "97531", "codeAgain": "97531", "scope": "restricted"})),
        )
        .await;
    assert_eq!(StatusCode::NO_CONTENT, status, "{said}");
    assert!(profiles.check_code("97531").unwrap());
    profiles.clear_code().unwrap().unwrap();
    let (status, _) = home
        .call("DELETE", "/profiles/code", &home.principal, None)
        .await;
    assert_eq!(StatusCode::NO_CONTENT, status);
}

// ---------------------------------------------------------- the recovery code

/// A recovery code is a code replaced without the old one, so drawing one asks the current code
/// like changing it does: a window left open must not hand the household's lock to a passer-by.
#[tokio::test]
async fn drawing_a_recovery_code_asks_the_current_code() {
    let home = Household::new(CodeScope::Restricted);
    for body in [None, Some(json!({"currentCode": "13579"}))] {
        let (status, _) = home
            .call("POST", "/profiles/code/recovery", &home.principal, body)
            .await;
        assert_eq!(StatusCode::FORBIDDEN, status);
    }
}

/// Whoever may change the code may draw the way back to it, and nobody else: a restricted
/// profile or a reader who learned it could set the code and lock the principal out.
#[tokio::test]
async fn only_the_principal_draws_a_recovery_code() {
    let home = Household::new(CodeScope::Restricted);
    for key in [&home.reader, &home.child] {
        let (status, _) = home
            .call("POST", "/profiles/code/recovery", key, None)
            .await;
        assert_eq!(StatusCode::FORBIDDEN, status);
    }
    let (status, said) = home
        .call(
            "POST",
            "/profiles/code/recovery",
            &home.principal,
            Some(json!({"currentCode": CODE})),
        )
        .await;
    assert_eq!(StatusCode::OK, status, "{said}");
    let code = said["recoveryCode"].as_str().expect("the code, once");
    let groups: Vec<&str> = code.split('-').collect();
    assert_eq!(4, groups.len(), "{code}");
    assert!(
        groups.iter().all(|g| g.len() == 4
            && g.chars()
                .all(|c| "ABCDEFGHJKLMNPQRSTUVWXYZ23456789".contains(c))),
        "{code}"
    );
}

/// The way back from a forgotten code, from a device that cannot even read yet: the code is
/// replaced, what it was for stays, and the recovery code is spent and replaced by another.
#[tokio::test]
async fn a_recovery_code_sets_a_new_code_and_is_spent() {
    let home = Household::new(CodeScope::Restricted);
    let (_, said) = home
        .call(
            "POST",
            "/profiles/code/recovery",
            &home.principal,
            Some(json!({"currentCode": CODE})),
        )
        .await;
    let recovery = said["recoveryCode"].as_str().unwrap().to_string();

    // Typed as a person copies it down: lower case, spaces for the dashes.
    let typed = recovery.to_lowercase().replace('-', " ");
    let (status, said) = home
        .call(
            "POST",
            "/profiles/code/recover",
            &home.child,
            Some(json!({"recoveryCode": typed, "code": "97531", "codeAgain": "97531"})),
        )
        .await;
    assert_eq!(StatusCode::OK, status, "{said}");
    let next = said["recoveryCode"].as_str().unwrap().to_string();
    assert_ne!(recovery, next);

    let profiles = home.profiles();
    assert!(profiles.check_code("97531").unwrap());
    assert!(!profiles.check_code(CODE).unwrap());
    assert_eq!(Some(CodeScope::Restricted), profiles.code().unwrap());

    let (status, _) = home
        .call(
            "POST",
            "/profiles/code/recover",
            &home.child,
            Some(json!({"recoveryCode": recovery, "code": "11111", "codeAgain": "11111"})),
        )
        .await;
    assert_eq!(
        StatusCode::FORBIDDEN,
        status,
        "a spent recovery code opens nothing"
    );
    assert!(profiles.check_code("97531").unwrap(), "and changes nothing");

    let (status, _) = home
        .call(
            "POST",
            "/profiles/code/recover",
            &home.child,
            Some(json!({"recoveryCode": next, "code": "22222", "codeAgain": "22222"})),
        )
        .await;
    assert_eq!(StatusCode::OK, status, "the new one works");
}

/// Drawing another revokes the one before it, so a recovery code that leaked is mended by
/// drawing again.
#[tokio::test]
async fn drawing_a_new_recovery_code_revokes_the_old_one() {
    let home = Household::new(CodeScope::Restricted);
    let (_, first) = home
        .call(
            "POST",
            "/profiles/code/recovery",
            &home.principal,
            Some(json!({"currentCode": CODE})),
        )
        .await;
    let (_, second) = home
        .call(
            "POST",
            "/profiles/code/recovery",
            &home.principal,
            Some(json!({"currentCode": CODE})),
        )
        .await;
    let body = |recovery: &serde_json::Value| json!({"recoveryCode": recovery, "code": "97531", "codeAgain": "97531"});
    let (status, _) = home
        .call(
            "POST",
            "/profiles/code/recover",
            &home.child,
            Some(body(&first["recoveryCode"])),
        )
        .await;
    assert_eq!(StatusCode::FORBIDDEN, status);
    let (status, said) = home
        .call(
            "POST",
            "/profiles/code/recover",
            &home.child,
            Some(body(&second["recoveryCode"])),
        )
        .await;
    assert_eq!(StatusCode::OK, status, "{said}");
}

/// A household that never drew one has none to guess, and answers as it does to a wrong one:
/// not telling the two apart is what keeps the answer from being worth probing.
#[tokio::test]
async fn recovering_with_none_drawn_is_refused_like_a_wrong_code() {
    let home = Household::new(CodeScope::Restricted);
    let (status, said) = home
        .call(
            "POST",
            "/profiles/code/recover",
            &home.child,
            Some(json!({"recoveryCode": "AAAA-AAAA-AAAA-AAAA", "code": "97531", "codeAgain": "97531"})),
        )
        .await;
    assert_eq!(StatusCode::FORBIDDEN, status, "{said}");
    assert_eq!(json!({"error": "code"}), said);
    assert!(home.profiles().check_code(CODE).unwrap());
}

/// A wrong recovery code is a guess at the lock that guards the code, so it counts on the same
/// brake: ten, and the route stops listening, even to the right one.
#[tokio::test]
async fn a_wrong_recovery_code_counts_towards_the_throttle() {
    let home = Household::new(CodeScope::Restricted);
    let (_, said) = home
        .call(
            "POST",
            "/profiles/code/recovery",
            &home.principal,
            Some(json!({"currentCode": CODE})),
        )
        .await;
    let recovery = said["recoveryCode"].clone();
    for _ in 0..10 {
        let (status, said) = home
            .call(
                "POST",
                "/profiles/code/recover",
                &home.child,
                Some(json!({"recoveryCode": "BBBB-BBBB-BBBB-BBBB", "code": "97531", "codeAgain": "97531"})),
            )
            .await;
        assert_eq!(StatusCode::FORBIDDEN, status, "{said}");
    }
    let (status, _, _) = home
        .raw(
            "POST",
            "/profiles/code/recover",
            &home.child,
            Some(json!({"recoveryCode": recovery, "code": "97531", "codeAgain": "97531"})),
        )
        .await;
    assert_eq!(StatusCode::TOO_MANY_REQUESTS, status);
    assert!(
        home.profiles().check_code(CODE).unwrap(),
        "nothing was changed"
    );
}

/// Typed twice and four to eight characters, as the code always is: refused before anything
/// is compared, and not counted against the brake.
#[tokio::test]
async fn a_new_code_by_recovery_is_typed_twice_and_of_the_right_length() {
    let home = Household::new(CodeScope::Restricted);
    let (_, said) = home
        .call(
            "POST",
            "/profiles/code/recovery",
            &home.principal,
            Some(json!({"currentCode": CODE})),
        )
        .await;
    for (code, again) in [
        ("97531", "97532"),
        ("975", "975"),
        ("123456789", "123456789"),
    ] {
        for _ in 0..4 {
            let (status, said) = home
                .call(
                    "POST",
                    "/profiles/code/recover",
                    &home.child,
                    Some(json!({"recoveryCode": said["recoveryCode"], "code": code, "codeAgain": again})),
                )
                .await;
            assert_eq!(StatusCode::BAD_REQUEST, status, "{code}: {said}");
        }
    }
    assert!(home.profiles().check_code(CODE).unwrap());
}

/// With the code gone there is nothing to recover: the recovery code goes with it, and the
/// next code does not inherit a way back its owner never saw.
#[tokio::test]
async fn removing_the_code_removes_its_recovery_code() {
    let home = Household::new(CodeScope::Restricted);
    let profiles = home.profiles();
    let recovery = profiles.new_recovery().unwrap().unwrap();
    profiles.delete(&home.leo).unwrap().unwrap();
    profiles.clear_code().unwrap().unwrap();
    assert_eq!(
        Err(leaf_server::store::profiles::ProfileRefusal::CodeRequired),
        profiles.new_recovery().unwrap(),
        "no code to recover"
    );
    profiles
        .set_code("97531", CodeScope::Restricted)
        .unwrap()
        .unwrap();
    assert_eq!(
        Err(leaf_server::store::profiles::RecoveryRefusal::WrongRecovery),
        profiles.recover(&recovery, "11111").unwrap(),
    );
}

// ---------------------------------------------------------- reading preferences

/// Ses préférences de lecture : oui, oui, oui — each profile its own.
#[tokio::test]
async fn each_profile_keeps_its_own_reading_preferences() {
    let home = Household::new(CodeScope::Restricted);
    let (status, said) = home.get("/profile/reading", &home.child).await;
    assert_eq!(StatusCode::OK, status);
    assert_eq!(json!({}), said);
    let (status, _) = home
        .call(
            "PUT",
            "/profile/reading",
            &home.child,
            Some(json!({"direction": "rtl"})),
        )
        .await;
    assert_eq!(StatusCode::NO_CONTENT, status);
    let (_, said) = home.get("/profile/reading", &home.child).await;
    assert_eq!(json!({"direction": "rtl"}), said);
    let (_, said) = home.get("/profile/reading", &home.reader).await;
    assert_eq!(json!({}), said, "the reader's are its own");
    let (status, _) = home
        .call("PUT", "/profile/reading", &home.reader, Some(json!([1, 2])))
        .await;
    assert_eq!(StatusCode::BAD_REQUEST, status, "an object, not anything");
}

// ---------------------------------------------------------- devices

/// Retenir son propre profil sur cet appareil : oui, oui, non.
#[tokio::test]
async fn a_reader_keeps_its_own_profile_on_its_device() {
    let home = Household::new(CodeScope::Restricted);
    for (key, id) in [
        (&home.principal, "default"),
        (&home.reader, home.camille.as_str()),
    ] {
        let (status, said) = home
            .call(
                "PUT",
                "/profiles/active",
                key,
                Some(json!({"id": id, "keep": true})),
            )
            .await;
        assert_eq!(StatusCode::OK, status, "{said}");
        assert_eq!(true, said["kept"]);
    }
}

#[tokio::test]
async fn a_restricted_profile_cannot_be_kept_by_who_reads() {
    let home = Household::new(CodeScope::Restricted);
    let (status, said) = home
        .call(
            "PUT",
            "/profiles/active",
            &home.reader,
            Some(json!({"id": home.leo, "keep": true})),
        )
        .await;
    assert_eq!(StatusCode::FORBIDDEN, status, "{said}");
    let (status, said) = home
        .call(
            "PUT",
            "/profiles/active",
            &home.child,
            Some(json!({"id": home.leo, "code": CODE, "keep": true})),
        )
        .await;
    assert_eq!(StatusCode::FORBIDDEN, status, "{said}");
}

/// Faire retenir un profil restreint par un appareil : oui, dans Appareils ; non ; non.
#[tokio::test]
async fn the_principal_has_a_device_keep_a_restricted_profile() {
    let home = Household::new(CodeScope::Restricted);
    let liseuse = device_id(&home.server, "Liseuse");
    let (status, said) = home
        .call(
            "PATCH",
            &format!("/devices/{liseuse}"),
            &home.principal,
            Some(json!({"profile": home.leo, "kept": true})),
        )
        .await;
    assert_eq!(StatusCode::OK, status, "{said}");
    assert_eq!(home.leo, said["profile"]);
    assert_eq!(true, said["kept"]);
    assert_eq!("Liseuse", said["name"]);
    assert_eq!(false, said["here"]);
    let (_, who) = home.get("/profiles", &home.reader).await;
    assert_eq!(home.leo, who["active"]);
    assert_eq!(true, who["kept"]);

    let (status, _) = home
        .call(
            "PATCH",
            "/devices/nothing",
            &home.principal,
            Some(json!({"profile": home.leo, "kept": true})),
        )
        .await;
    assert_eq!(StatusCode::NOT_FOUND, status);
    let (status, _) = home
        .call(
            "PATCH",
            &format!("/devices/{liseuse}"),
            &home.principal,
            Some(json!({"profile": "nobody", "kept": true})),
        )
        .await;
    assert_eq!(StatusCode::NOT_FOUND, status);
}

#[tokio::test]
async fn only_the_principal_places_a_device() {
    let home = Household::new(CodeScope::Restricted);
    let tablette = device_id(&home.server, "Tablette");
    for key in [&home.reader, &home.child] {
        let (status, said) = home
            .call(
                "PATCH",
                &format!("/devices/{tablette}"),
                key,
                Some(json!({"profile": "default", "kept": true})),
            )
            .await;
        assert_eq!(StatusCode::FORBIDDEN, status, "{said}");
    }
    assert_eq!(
        home.leo,
        home.profiles().seat(&tablette).unwrap().profile_id
    );
}

/// Relier un autre appareil, renommer ou délier cet appareil : oui, non, non.
#[tokio::test]
async fn only_the_principal_links_renames_or_unlinks() {
    let home = Household::new(CodeScope::Restricted);
    for key in [&home.reader, &home.child] {
        for (method, uri, body) in [
            ("POST", "/link/codes", None),
            ("PATCH", "/device", Some(json!({"name": "Autre"}))),
            ("DELETE", "/device", None),
        ] {
            let (status, said) = home.call(method, uri, key, body).await;
            assert_eq!(StatusCode::FORBIDDEN, status, "{method} {uri}: {said}");
        }
    }
}

/// Voir les autres appareils, leur profil retenu, en retirer un : oui, non, non.
#[tokio::test]
async fn the_principal_sees_every_device_and_which_is_here() {
    let home = Household::new(CodeScope::Restricted);
    home.get("/profiles", &home.child).await;
    let (status, said) = home.get("/devices", &home.principal).await;
    assert_eq!(StatusCode::OK, status, "{said}");
    let devices = said["devices"].as_array().expect("a list");
    assert_eq!(3, devices.len(), "{said}");
    let by_name = |name: &str| {
        devices
            .iter()
            .find(|d| d["name"] == name)
            .unwrap_or_else(|| panic!("{name} in {said}"))
            .clone()
    };
    let portable = by_name("Portable");
    assert_eq!(true, portable["here"]);
    assert_eq!("default", portable["profile"]);
    assert!(portable["seenAt"].is_i64(), "seen just now: {portable}");
    let tablette = by_name("Tablette");
    assert_eq!(false, tablette["here"]);
    assert_eq!(home.leo, tablette["profile"]);
    assert_eq!(false, tablette["kept"]);
    assert_eq!(device_id(&home.server, "Tablette"), tablette["id"]);
}

#[tokio::test]
async fn only_the_principal_sees_or_removes_the_devices() {
    let home = Household::new(CodeScope::Restricted);
    let portable = device_id(&home.server, "Portable");
    for key in [&home.reader, &home.child] {
        let (status, _) = home.get("/devices", key).await;
        assert_eq!(StatusCode::FORBIDDEN, status);
        let (status, _) = home
            .call("DELETE", &format!("/devices/{portable}"), key, None)
            .await;
        assert_eq!(StatusCode::FORBIDDEN, status);
    }
}

#[tokio::test]
async fn removing_a_device_cuts_its_key_at_once() {
    let home = Household::new(CodeScope::Restricted);
    let tablette = device_id(&home.server, "Tablette");
    let (status, _) = home.get("/profiles", &home.child).await;
    assert_eq!(StatusCode::OK, status);
    let (status, _) = home
        .call(
            "DELETE",
            &format!("/devices/{tablette}"),
            &home.principal,
            None,
        )
        .await;
    assert_eq!(StatusCode::NO_CONTENT, status);
    let (status, said) = home.get("/profiles", &home.child).await;
    assert_eq!(StatusCode::FORBIDDEN, status);
    assert_eq!(json!({"error": "unknown key"}), said);
    let (status, _) = home
        .call(
            "DELETE",
            &format!("/devices/{tablette}"),
            &home.principal,
            None,
        )
        .await;
    assert_eq!(StatusCode::NOT_FOUND, status);
}

// ---------------------------------------------------------- old keys

/// [RF 5] An old key is the principal on every profile route, and has no device to seat.
#[tokio::test]
async fn an_old_key_is_the_principal_on_the_profile_routes() {
    let home = Household::new(CodeScope::Restricted);
    let (status, said) = home.get("/profiles", READ_ONLY).await;
    assert_eq!(StatusCode::OK, status, "{said}");
    assert_eq!("default", said["active"]);
    assert_eq!(true, said["kept"]);
    assert_eq!(false, said["choosing"]);
    assert_eq!(false, said["held"]);

    let (status, said) = home.switch(IMPORTER, &home.camille, None).await;
    assert_eq!(StatusCode::CONFLICT, status);
    assert_eq!(json!({"error": "no device"}), said);

    let (status, said) = home.get("/devices", IMPORTER).await;
    assert_eq!(StatusCode::OK, status, "{said}");
    assert!(said["devices"]
        .as_array()
        .unwrap()
        .iter()
        .all(|d| d["here"] == false));
    let (status, _) = home.get("/devices", READ_ONLY).await;
    assert_eq!(
        StatusCode::FORBIDDEN,
        status,
        "the principal's routes ask import"
    );

    let (status, said) = home
        .call(
            "POST",
            "/profiles",
            IMPORTER,
            Some(a_reader_named("Anna", "rose")),
        )
        .await;
    assert_eq!(StatusCode::CREATED, status, "{said}");
    let (status, said) = home.get("/profile/reading", READ_ONLY).await;
    assert_eq!(StatusCode::OK, status, "{said}");
}
