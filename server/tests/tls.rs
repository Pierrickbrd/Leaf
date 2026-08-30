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
