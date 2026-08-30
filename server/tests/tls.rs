//! Serving HTTPS with nothing in front.
//!
//! The clients pin the fingerprint rather than trusting a public authority, so the two
//! things that matter are that the pair survives a restart unchanged — a fingerprint that
//! moved would lock every client out — and that the key is not readable by the machine at
//! large.

use leaf_server::net::tls::Tls;

/// The server's own logging, on: without a subscriber every `tracing!` short-circuits
/// before it evaluates its arguments, so the line that announces a generated certificate
/// never runs.
fn logging() {
    static ONCE: std::sync::Once = std::sync::Once::new();
    ONCE.call_once(|| {
        let _ = tracing_subscriber::fmt()
            .with_test_writer()
            .with_max_level(tracing::Level::DEBUG)
            .try_init();
    });
}

#[tokio::test]
async fn a_pair_is_generated_once_and_then_kept() {
    logging();
    let dir = tempfile::tempdir().unwrap();
    let certificate = dir.path().join("tls/leaf.crt");
    let key = dir.path().join("tls/leaf.key");
    let hosts = vec!["leaf.local".to_string(), "127.0.0.1".to_string()];

    let first = Tls::of(&certificate, &key, &hosts)
        .await
        .expect("first start");
    assert!(certificate.exists() && key.exists());
    assert_eq!(
        95,
        first.fingerprint.len(),
        "a SHA-256 printed as 32 colon-separated bytes"
    );

    let again = Tls::of(&certificate, &key, &hosts).await.expect("restart");
    // A restart that generated a new certificate would silently lock out every client that
    // had pinned the old one.
    assert_eq!(first.fingerprint, again.fingerprint);
}

#[tokio::test]
#[cfg(unix)]
async fn the_key_is_readable_by_its_owner_and_nobody_else() {
    use std::os::unix::fs::PermissionsExt;

    let dir = tempfile::tempdir().unwrap();
    let certificate = dir.path().join("leaf.crt");
    let key = dir.path().join("leaf.key");
    Tls::of(&certificate, &key, &["leaf.local".to_string()])
        .await
        .expect("generating");

    // This is what replaced the keystore's password: on Unix a private key is guarded by
    // its mode, and there is then nothing to configure, lose, or leave in an environment
    // variable.
    let mode = std::fs::metadata(&key).unwrap().permissions().mode();
    assert_eq!(0o600, mode & 0o777, "the key must not be world-readable");
}

/// And a key that is already there is closed too.
///
/// `OpenOptions::mode` is honoured only when the file is created, so rewriting a key left at
/// 0644 by an earlier version, restored from a backup or made by hand kept it at 0644 — and
/// this module's whole argument for having no password is that the mode does the guarding.
/// Generation is reached whenever the certificate is missing, which is exactly when a key
/// can already be sitting there.
#[tokio::test]
#[cfg(unix)]
async fn a_key_left_open_by_something_else_is_closed_when_it_is_rewritten() {
    use std::os::unix::fs::PermissionsExt;

    let dir = tempfile::tempdir().unwrap();
    let certificate = dir.path().join("leaf.crt");
    let key = dir.path().join("leaf.key");
    std::fs::write(&key, b"whatever was here before").unwrap();
    std::fs::set_permissions(&key, std::fs::Permissions::from_mode(0o644)).unwrap();

    Tls::of(&certificate, &key, &["leaf.local".to_string()])
        .await
        .expect("generating");

    let mode = std::fs::metadata(&key).unwrap().permissions().mode();
    assert_eq!(0o600, mode & 0o777, "the key must not be world-readable");
}

/// And the far commoner case: a key sitting beside a certificate that is already there.
///
/// The repair above only ran on the path that generates, and generating happens once. Every
/// start after it takes the branch that reads the pair straight through, so a key left at
/// 0644 stayed at 0644 for the life of the deployment — and the module's whole argument for
/// having no password is that the mode does the guarding. It was true on first boot and
/// nowhere else.
#[tokio::test]
#[cfg(unix)]
async fn a_key_left_open_beside_a_certificate_that_exists_is_closed_on_the_way_in() {
    use std::os::unix::fs::PermissionsExt;

    let dir = tempfile::tempdir().unwrap();
    let certificate = dir.path().join("leaf.crt");
    let key = dir.path().join("leaf.key");
    let first = Tls::of(&certificate, &key, &["leaf.local".to_string()])
        .await
        .expect("generating");

    // What an earlier version left, or a restore, or a copy by hand. Both files are now
    // exactly where the server expects them, so nothing below generates anything.
    std::fs::set_permissions(&key, std::fs::Permissions::from_mode(0o644)).unwrap();

    let again = Tls::of(&certificate, &key, &["leaf.local".to_string()])
        .await
        .expect("restart");

    let mode = std::fs::metadata(&key).unwrap().permissions().mode();
    assert_eq!(0o600, mode & 0o777, "the key must not be world-readable");
    // And it is still the same pair: closing a key must never be a way of quietly issuing a
    // new one, which would lock out every client that had pinned the old fingerprint.
    assert_eq!(first.fingerprint, again.fingerprint);
}

/// A key closed tighter than this server writes is not a key that was left open.
///
/// The check asked whether the mode *was* 0600, which is not what the sentence above it
/// promises: what it promises is that nobody but the owner can reach the file. A key an
/// operator had deliberately made read-only failed that equality, so the server announced
/// that it had been readable by the machine at large — told somebody to issue a new one and
/// re-pin every client over nothing — and then chmod'ed it back to 0600, which on a 0400 key
/// hands back the write bit. A false alarm and a real loosening, from one comparison.
#[tokio::test]
#[cfg(unix)]
async fn a_key_shut_tighter_than_this_server_writes_is_left_exactly_as_it_is() {
    use std::os::unix::fs::PermissionsExt;

    let dir = tempfile::tempdir().unwrap();
    let certificate = dir.path().join("leaf.crt");
    let key = dir.path().join("leaf.key");
    let first = Tls::of(&certificate, &key, &["leaf.local".to_string()])
        .await
        .expect("generating");

    std::fs::set_permissions(&key, std::fs::Permissions::from_mode(0o400)).unwrap();

    let again = Tls::of(&certificate, &key, &["leaf.local".to_string()])
        .await
        .expect("restart");

    let mode = std::fs::metadata(&key).unwrap().permissions().mode();
    assert_eq!(
        0o400,
        mode & 0o777,
        "stricter than 0600 is not a mode to repair"
    );
    assert_eq!(first.fingerprint, again.fingerprint);
}

#[tokio::test]
async fn a_certificate_that_does_not_match_its_key_is_refused_at_startup() {
    let dir = tempfile::tempdir().unwrap();
    let certificate = dir.path().join("leaf.crt");
    let key = dir.path().join("leaf.key");
    Tls::of(&certificate, &key, &["leaf.local".to_string()])
        .await
        .expect("generating");

    // A second pair, and then the halves swapped: the kind of thing that happens when two
    // certificates are copied by hand.
    let other_certificate = dir.path().join("other.crt");
    let other_key = dir.path().join("other.key");
    Tls::of(&other_certificate, &other_key, &["leaf.local".to_string()])
        .await
        .expect("generating a second");
    std::fs::copy(&other_key, &key).unwrap();

    // Said at startup rather than at the first connection, which is where a mismatch would
    // otherwise show up: as a handshake failure on the client, with nothing in the log.
    assert!(Tls::of(&certificate, &key, &["leaf.local".to_string()])
        .await
        .is_err());
}
