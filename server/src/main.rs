//! Leaf — a reading server for a comics library.
//!
//! Wiring, and only wiring. Everything this used to decide for itself now lives in
//! `boot`, where the tests can reach it: what stays here is installing the log subscriber,
//! opening the index, binding a socket and waiting for a signal — the three or four things
//! that are not a decision at all.

use std::sync::Arc;

use anyhow::{Context, Result};
use tracing_subscriber::EnvFilter;

use leaf_server::api::keys::Keys;
use leaf_server::api::pages::Pages;
use leaf_server::api::routes::{router, AppState};
use leaf_server::boot::{
    cache_ceiling, jpeg_quality, refuse_an_open_library, split_volumes, tls_hosts, Invocation,
};
use leaf_server::config::Config;
use leaf_server::net::tls::Tls;
use leaf_server::scan::scanner::Scanner;
use leaf_server::store::Db;

#[tokio::main]
async fn main() -> Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(
            EnvFilter::try_from_env("LEAF_LOG").unwrap_or_else(|_| EnvFilter::new("info")),
        )
        .with_target(false)
        .init();

    let asked = Invocation::of(std::env::args().skip(1));
    let config = Config::from_env();
    let keys = Keys::parse(std::env::var("LEAF_KEYS").ok().as_deref())?;
    refuse_an_open_library(&config.host, &keys)?;

    let db = Arc::new(Db::open(&config.db).context("opening the index")?);
    let roots = asked.roots(&config.library);

    if asked.is_scan() {
        let started = std::time::Instant::now();
        let scanner = Scanner::new(Arc::clone(&db), asked.all_dimensions);
        let report = scanner.scan(&roots)?;
        tracing::info!(seconds = started.elapsed().as_secs(), "Scan finished");
        println!();
        println!("{}", report.summary());
        return Ok(());
    }

    std::fs::create_dir_all(&config.inbox).context("creating the inbox")?;
    if split_volumes(&config.library, &config.inbox) {
        tracing::warn!(
            library = %config.library.display(),
            inbox = %config.inbox.display(),
            "the inbox and the library sit on two volumes: committing will copy instead of rename"
        );
    }
    if let Some(folder) = &config.drop {
        std::fs::create_dir_all(folder).context("creating the drop folder")?;
    }

    tracing::info!(library = %config.library.display(), "Library");
    tracing::info!(inbox = %config.inbox.display(), "Inbox");
    tracing::info!(cache = %config.cache.display(), "Cache");
    if let Some(folder) = &config.drop {
        tracing::info!(drop = %folder.display(), "Drop");
    }

    // The connect info is what the throttle counts against: without it every caller looks
    // like the same one, and one misconfigured device would lock out the household.
    let pages = Pages::new(
        Arc::clone(&db),
        config.cache.clone(),
        jpeg_quality(std::env::var("LEAF_JPEG_QUALITY").ok().as_deref()),
        cache_ceiling(std::env::var("LEAF_MAX_CACHE_MB").ok().as_deref()),
    );

    let state = AppState::with_pages(Arc::clone(&db), keys, pages)
        .with_library(roots.clone(), asked.all_dimensions)
        .trusting_proxy(config.trust_proxy)
        .with_import(
            &config.inbox,
            &config.library,
            config.drop.clone(),
            config.max_upload_bytes,
        );

    // Behind, so the server answers from its first second. A scan that dies with the
    // process costs nothing: the index is rebuildable, you simply run it again.
    if std::env::var("LEAF_NO_SCAN").is_err() {
        let scanner = Arc::clone(&state.scanner);
        let roots = Arc::clone(&state.roots);
        state
            .runner
            .start("Startup scan", move || scanner.scan(&roots));
    }

    let app = router(state).into_make_service_with_connect_info::<std::net::SocketAddr>();

    // TLS only when a certificate is named. The recommended path is still a reverse proxy
    // or `tailscale serve`, which hold a certificate a browser already trusts; this is for
    // the port opened with nothing in front of it, which would otherwise send the API key
    // in clear on every request.
    let address = format!("{}:{}", config.host, config.port);
    let address: std::net::SocketAddr = address
        .parse()
        .with_context(|| format!("{address} is not an address to bind"))?;
    match (&config.tls_certificate, &config.tls_key) {
        (Some(certificate), Some(key)) => {
            let tls = Tls::of(
                certificate,
                key,
                &tls_hosts(&config.tls_hosts, &config.host),
            )
            .await?;
            tracing::info!("Listening on https://{address}");
            // The same graceful stop the plain path has. Without it, stopping the unit cut
            // the process off mid-write while HTTP shut down politely — one behaviour for
            // one server, decided by which port it happened to be listening on.
            let handle = axum_server::Handle::new();
            let stopping = handle.clone();
            tokio::spawn(async move {
                shutdown().await;
                stopping.graceful_shutdown(Some(std::time::Duration::from_secs(10)));
            });
            axum_server::bind_rustls(address, tls.config)
                .handle(handle)
                .serve(app)
                .await
                .context("serving")
        }
        _ => {
            let listener = tokio::net::TcpListener::bind(address)
                .await
                .with_context(|| format!("binding {address}"))?;
            tracing::info!("Listening on http://{address}");
            axum::serve(listener, app)
                .with_graceful_shutdown(shutdown())
                .await
                .context("serving")
        }
    }
}

/// Answers the signal `systemctl stop` sends, so the writer finishes what it holds.
async fn shutdown() {
    let _ = tokio::signal::ctrl_c().await;
    tracing::info!("Shutting down");
}
