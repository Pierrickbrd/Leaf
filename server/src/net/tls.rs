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
//! **The keystore became two PEM files.** The Kotlin kept a JKS, which is a Java format and
//! nothing else reads it, and protected it with a password that guarded the file rather than
//! the wire. On Unix a private key is guarded by its mode, so the key is written `0600` and
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
    file.write_all(bytes)?;
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
