//! TLS, for the case where the server is exposed without anything in front of it.
//!
//! The recommended path is still a reverse proxy or `tailscale serve`: they hold a
//! certificate a browser already trusts, and Leaf then speaks plain HTTP on the loopback.
//! But "recommended" is not "enforced", and a port opened without a proxy would send the
//! API key in clear on every single request. So the server can do it itself.
//!
//! A self-signed certificate is generated on first start and kept. Nothing on the internet
//! trusts it, which is fine here: the clients are yours, and they pin the fingerprint
//! printed below rather than trusting a public authority. That is a stronger bond than a
//! public certificate, not a weaker one — it names exactly one server.
//!
//! **Two PEM files, and no keystore.** A keystore is a format one ecosystem reads, protected
//! by a password that guards the file rather than the wire. On Unix a private key is guarded by its mode, so the key is written `0600` and
//! there is no password to configure, lose, or leave in an environment variable.

use std::path::{Path, PathBuf};

use anyhow::{Context, Result};
use axum_server::tls_rustls::RustlsConfig;
use sha2::{Digest, Sha256};

pub struct Tls {
    pub config: RustlsConfig,
    /// SHA-256 over the certificate, colon-separated — what a client pins.
    pub fingerprint: String,
    pub certificate: PathBuf,
}

impl Tls {
    /// Loads the pair at [`certificate`] and [`key`], generating a self-signed one if the
    /// certificate is not there yet.
    pub async fn of(certificate: &Path, key: &Path, hosts: &[String]) -> Result<Tls> {
        // rustls 0.23 asks for a provider by name rather than picking one, and asks once per
        // process. Installing it here rather than in `main` keeps TLS the only thing that
        // knows rustls exists.
        let _ = rustls::crypto::ring::default_provider().install_default();

        if !certificate.exists() {
            tracing::info!(
                path = %certificate.display(),
                hosts = hosts.join(", "),
                "no certificate — generating a self-signed one"
            );
            generate(certificate, key, hosts)?;
        }

        // On every start, not only the one that generates. Generating is the rare path: the
        // usual one is a pair already on disk — left at 0644 by an earlier version, restored
        // from a backup, or copied by hand — and it never reached `write_private` at all. A
        // mode offered in place of a keystore's password, kept only on first boot, is not
        // kept.
        close_private(key)?;

        let certificate_pem = std::fs::read(certificate)
            .with_context(|| format!("reading {}", certificate.display()))?;
        let key_pem = std::fs::read(key).with_context(|| format!("reading {}", key.display()))?;

        let fingerprint = fingerprint(&certificate_pem)?;
        tracing::info!("TLS on, certificate fingerprint (pin this in the applications):");
        tracing::info!("  {fingerprint}");

        Ok(Tls {
            config: RustlsConfig::from_pem(certificate_pem, key_pem)
                .await
                .context("the certificate and the key do not make a usable pair")?,
            fingerprint,
            certificate: certificate.to_path_buf(),
        })
    }
}

fn generate(certificate: &Path, key: &Path, hosts: &[String]) -> Result<()> {
    let mut params = rcgen::CertificateParams::new(hosts.to_vec())
        .context("the host names are not usable in a certificate")?;
    // A wide fixed window rather than "ten years from today". This certificate names one
    // server on one network and is trusted by its fingerprint; the day it expires, every
    // client has to be re-pinned by hand — which is the cost the pinning exists to avoid.
    params.not_before = rcgen::date_time_ymd(2020, 1, 1);
    params.not_after = rcgen::date_time_ymd(2045, 1, 1);
    params
        .distinguished_name
        .push(rcgen::DnType::CommonName, "leaf");

    let pair = rcgen::KeyPair::generate().context("generating a key")?;
    let signed = params.self_signed(&pair).context("signing")?;

    if let Some(parent) = certificate.parent() {
        std::fs::create_dir_all(parent)?;
    }
    if let Some(parent) = key.parent() {
        std::fs::create_dir_all(parent)?;
    }
    std::fs::write(certificate, signed.pem())?;
    write_private(key, pair.serialize_pem().as_bytes())?;
    Ok(())
}

/// The key is readable by its owner and by nobody else, which is what replaced the
/// keystore's password.
///
/// `mode` on the open is honoured **only when the file is created**, so a key rewritten in
/// place kept whatever mode it already had — 0644 left by an earlier version, restored from
/// a backup, or made by hand, and the one sentence this module offers in place of a password
/// was quietly untrue. Set on the handle instead, before a single byte of key is in it: the
/// window where the file exists at the wrong mode is a window where it is still empty.
fn write_private(path: &Path, bytes: &[u8]) -> Result<()> {
    use std::io::Write;
    let mut options = std::fs::OpenOptions::new();
    options.write(true).create(true).truncate(true);
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        options.mode(0o600);
    }
    let mut file = options
        .open(path)
        .with_context(|| format!("writing {}", path.display()))?;
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        file.set_permissions(std::fs::Permissions::from_mode(0o600))
            .with_context(|| format!("closing {} to everybody else", path.display()))?;
    }
    file.write_all(bytes)?;
    Ok(())
}

/// Whether `mode` lets anyone but the file's owner reach it — a read, write or execute bit
/// set in the group or the other triad.
///
/// Named out of [`close_private`] because what that function asks is that nobody *but* the
/// owner can reach the key — not that the mode is the one this server happens to write.
/// `mode != 0o600` failed a key deliberately hardened to `0400`: it printed the sentence
/// below about a file nothing could read, told an operator to re-issue a key and re-pin every
/// client over it, and then chmod'ed the file to 0600 — which on that key *adds* the write
/// bit. The bits that matter are the other six, which is what this checks and nothing else.
#[cfg(unix)]
pub fn reachable_by_others(mode: u32) -> bool {
    mode & 0o077 != 0
}

/// The same rule as [`write_private`], for the key this server did not write.
///
/// Loud, because it is not a repair anybody asked for: a key that was readable by the rest
/// of the machine has to be assumed read, and closing it now does not unread it. The line in
/// the log is what tells somebody to issue a new one.
#[cfg(unix)]
fn close_private(path: &Path) -> Result<()> {
    use std::os::unix::fs::PermissionsExt;
    // Absent is not this function's business: the read just below says so, with the path in
    // it, and says it once.
    let Ok(facts) = std::fs::metadata(path) else {
        return Ok(());
    };
    let mode = facts.permissions().mode() & 0o777;
    if !reachable_by_others(mode) {
        return Ok(());
    }
    tracing::warn!(
        path = %path.display(),
        mode = format!("{mode:04o}"),
        "the private key was readable by more than its owner — closing it, and issue a new one"
    );
    // Said and carried on, never refused. The sentence above is what this function is for;
    // the chmod is the part that may not be ours to do. A key handed over by a secrets
    // manager or by a unit's `LoadCredential` is owned by root and group-readable by the
    // service user, so `set_permissions` returns EPERM — and a `?` here turned "issue a new
    // one" into a server that does not boot at all, on a deployment that served yesterday.
    if let Err(e) = std::fs::set_permissions(path, std::fs::Permissions::from_mode(0o600)) {
        tracing::warn!(
            path = %path.display(),
            error = %e,
            "and it could not be closed either — chmod 600 it by hand"
        );
    }
    Ok(())
}

#[cfg(not(unix))]
fn close_private(_: &Path) -> Result<()> {
    Ok(())
}

/// Over the DER, not over the PEM: the fingerprint a client computes from the certificate
/// it was handed on the wire is a digest of the DER, and anything else would never match.
fn fingerprint(pem: &[u8]) -> Result<String> {
    let mut reader = std::io::BufReader::new(pem);
    let first = rustls_pemfile::certs(&mut reader)
        .next()
        .context("no certificate in the file")?
        .context("unreadable certificate")?;
    let digest = Sha256::digest(&first);
    Ok(digest
        .iter()
        .map(|b| format!("{b:02X}"))
        .collect::<Vec<_>>()
        .join(":"))
}
