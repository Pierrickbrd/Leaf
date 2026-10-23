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
    /// SHA-256 over the certificate, colon-separated — the form the log has always printed,
    /// and the one a person compares by eye.
    pub fingerprint: String,
    /// The same digest as a link carries it: 64 lowercase hex digits, no colons.
    pub fingerprint_hex: String,
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

        made_if_absent(certificate, key, hosts)?;

        // On every start, not only the one that generates. Generating is the rare path: the
        // usual one is a pair already on disk — left at 0644 by an earlier version, restored
        // from a backup, or copied by hand — and it never reached `write_private` at all. A
        // mode offered in place of a keystore's password, kept only on first boot, is not
        // kept.
        close_private(key)?;

        let certificate_pem = tokio::fs::read(certificate)
            .await
            .with_context(|| format!("reading {}", certificate.display()))?;
        let key_pem = tokio::fs::read(key)
            .await
            .with_context(|| format!("reading {}", key.display()))?;

        let digest = digest_of_first(&certificate_pem)?;
        let fingerprint = colon_separated(&digest);
        let fingerprint_hex = hex(&digest);
        tracing::info!("TLS on, certificate fingerprint (pin this in the applications):");
        tracing::info!("  {fingerprint}");
        tracing::info!("  {fingerprint_hex} (as a link carries it)");

        Ok(Tls {
            config: RustlsConfig::from_pem(certificate_pem, key_pem)
                .await
                .context("the certificate and the key do not make a usable pair")?,
            fingerprint,
            fingerprint_hex,
            certificate: certificate.to_path_buf(),
        })
    }
}

/// The fingerprint a link to this server carries, as 64 lowercase hex digits, read from the
/// certificate it serves — made now if the server has never started, by the same path
/// [`Tls::of`] takes, so that the link printed before the first start and the certificate
/// served after it are one certificate.
pub fn own_fingerprint(certificate: &Path, key: &Path, hosts: &[String]) -> Result<String> {
    made_if_absent(certificate, key, hosts)?;
    let pem =
        std::fs::read(certificate).with_context(|| format!("reading {}", certificate.display()))?;
    Ok(hex(&digest_of_first(&pem)?))
}

fn made_if_absent(certificate: &Path, key: &Path, hosts: &[String]) -> Result<()> {
    if !certificate.exists() {
        tracing::info!(
            path = %certificate.display(),
            hosts = hosts.join(", "),
            "no certificate — generating a self-signed one"
        );
        generate(certificate, key, hosts)?;
    }
    Ok(())
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
fn digest_of_first(pem: &[u8]) -> Result<[u8; 32]> {
    let mut reader = std::io::BufReader::new(pem);
    let first = rustls_pemfile::certs(&mut reader)
        .next()
        .context("no certificate in the file")?
        .context("unreadable certificate")?;
    Ok(Sha256::digest(&first).into())
}

fn colon_separated(digest: &[u8]) -> String {
    digest
        .iter()
        .map(|b| format!("{b:02X}"))
        .collect::<Vec<_>>()
        .join(":")
}

fn hex(digest: &[u8]) -> String {
    digest.iter().map(|b| format!("{b:02x}")).collect()
}

/// What an address answers with when asked for a certificate.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Presented {
    /// A certificate the system already trusts. A link to it carries no fingerprint: a
    /// public one is renewed every ninety days, and a pinned fingerprint would lock every
    /// client out at the first renewal.
    PublicAuthority,
    /// Anything else — Leaf's own certificate, a proxy passing it through, a private
    /// authority — by the fingerprint a link carries (64 lowercase hex digits).
    Pinned(String),
    /// Nothing could be read from it, and why.
    Unreachable(String),
}

/// How long one connection, or one read or write on it, may take before the address counts
/// as unreachable. A command waiting on an address that drops packets has to give up and say
/// so; five seconds is long for a server on the same network and short for a person.
const PATIENCE: std::time::Duration = std::time::Duration::from_secs(5);

/// Which certificate `host:port` presents.
///
/// Two handshakes, the first validated by the system's roots: when it succeeds there is
/// nothing to pin. When it fails on the certificate, the second accepts anything and reads
/// back what was presented — this is a question about the address, not a connection that
/// trusts it, and nothing is sent over either.
pub fn observe(host: &str, port: u16) -> Presented {
    // Handed its provider rather than leaning on the process default: the command that asks
    // this never builds a `Tls`, which is what installs one.
    let provider = std::sync::Arc::new(rustls::crypto::ring::default_provider());
    let host = host.trim_start_matches('[').trim_end_matches(']');

    let mut roots = rustls::RootCertStore::empty();
    // A store that partly failed to load still answers for the roots it did load; one that
    // loaded nothing makes every certificate « not public », which is the side that pins.
    roots.add_parsable_certificates(rustls_native_certs::load_native_certs().certs);
    let trusting = rustls::ClientConfig::builder_with_provider(std::sync::Arc::clone(&provider))
        .with_safe_default_protocol_versions()
        .map(|b| b.with_root_certificates(roots).with_no_client_auth());
    let trusting = match trusting {
        Ok(config) => config,
        Err(e) => return Presented::Unreachable(e.to_string()),
    };
    match handshake(host, port, trusting) {
        Ok(_) => return Presented::PublicAuthority,
        Err(Shake::Unreachable(why)) => return Presented::Unreachable(why),
        Err(Shake::Refused) => {}
    }

    let accepting = rustls::ClientConfig::builder_with_provider(std::sync::Arc::clone(&provider))
        .with_safe_default_protocol_versions()
        .map(|b| {
            b.dangerous()
                .with_custom_certificate_verifier(std::sync::Arc::new(AnyCertificate(provider)))
                .with_no_client_auth()
        });
    let accepting = match accepting {
        Ok(config) => config,
        Err(e) => return Presented::Unreachable(e.to_string()),
    };
    match handshake(host, port, accepting) {
        Ok(Some(certificate)) => Presented::Pinned(hex(&Sha256::digest(certificate.as_ref()))),
        Ok(None) => Presented::Unreachable(format!("{host}:{port} presented no certificate")),
        Err(Shake::Unreachable(why)) => Presented::Unreachable(why),
        Err(Shake::Refused) => Presented::Unreachable(format!(
            "{host}:{port} refused the handshake even with any certificate accepted"
        )),
    }
}

/// Why a handshake did not complete: the address could not be talked to at all, or it was
/// talked to and its certificate was not one the verifier accepts.
enum Shake {
    Unreachable(String),
    Refused,
}

/// One handshake and nothing after it; the certificate the server presented first.
fn handshake(
    host: &str,
    port: u16,
    config: rustls::ClientConfig,
) -> std::result::Result<Option<rustls::pki_types::CertificateDer<'static>>, Shake> {
    use std::net::ToSocketAddrs;

    let unreachable = |e: &dyn std::fmt::Display| Shake::Unreachable(format!("{host}:{port}: {e}"));
    let name =
        rustls::pki_types::ServerName::try_from(host.to_string()).map_err(|e| unreachable(&e))?;
    let address = (host, port)
        .to_socket_addrs()
        .map_err(|e| unreachable(&e))?
        .next()
        .ok_or_else(|| unreachable(&"the name resolves to no address"))?;
    let mut socket =
        std::net::TcpStream::connect_timeout(&address, PATIENCE).map_err(|e| unreachable(&e))?;
    socket
        .set_read_timeout(Some(PATIENCE))
        .and_then(|_| socket.set_write_timeout(Some(PATIENCE)))
        .map_err(|e| unreachable(&e))?;

    let mut connection = rustls::ClientConnection::new(std::sync::Arc::new(config), name)
        .map_err(|e| unreachable(&e))?;
    while connection.is_handshaking() {
        if let Err(e) = connection.complete_io(&mut socket) {
            // rustls hands its own refusals back wrapped in an I/O error of kind
            // `InvalidData`; a certificate it would not accept is the one refusal that
            // means « ask again, without trusting », and anything else is the address.
            let refused = e
                .get_ref()
                .and_then(|inner| inner.downcast_ref::<rustls::Error>())
                .is_some_and(|inner| matches!(inner, rustls::Error::InvalidCertificate(_)));
            return Err(if refused {
                Shake::Refused
            } else {
                unreachable(&e)
            });
        }
    }
    Ok(connection
        .peer_certificates()
        .and_then(|chain| chain.first())
        .map(|first| first.clone().into_owned()))
}

/// Accepts whatever certificate is presented, and still checks that the server holds its
/// key: a handshake signed by somebody else is not the certificate it claims to present.
#[derive(Debug)]
struct AnyCertificate(std::sync::Arc<rustls::crypto::CryptoProvider>);

impl rustls::client::danger::ServerCertVerifier for AnyCertificate {
    fn verify_server_cert(
        &self,
        _: &rustls::pki_types::CertificateDer<'_>,
        _: &[rustls::pki_types::CertificateDer<'_>],
        _: &rustls::pki_types::ServerName<'_>,
        _: &[u8],
        _: rustls::pki_types::UnixTime,
    ) -> std::result::Result<rustls::client::danger::ServerCertVerified, rustls::Error> {
        Ok(rustls::client::danger::ServerCertVerified::assertion())
    }

    fn verify_tls12_signature(
        &self,
        message: &[u8],
        certificate: &rustls::pki_types::CertificateDer<'_>,
        signed: &rustls::DigitallySignedStruct,
    ) -> std::result::Result<rustls::client::danger::HandshakeSignatureValid, rustls::Error> {
        rustls::crypto::verify_tls12_signature(
            message,
            certificate,
            signed,
            &self.0.signature_verification_algorithms,
        )
    }

    fn verify_tls13_signature(
        &self,
        message: &[u8],
        certificate: &rustls::pki_types::CertificateDer<'_>,
        signed: &rustls::DigitallySignedStruct,
    ) -> std::result::Result<rustls::client::danger::HandshakeSignatureValid, rustls::Error> {
        rustls::crypto::verify_tls13_signature(
            message,
            certificate,
            signed,
            &self.0.signature_verification_algorithms,
        )
    }

    fn supported_verify_schemes(&self) -> Vec<rustls::SignatureScheme> {
        self.0.signature_verification_algorithms.supported_schemes()
    }
}
