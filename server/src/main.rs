//! Leaf — a reading server for a comics library.
//!
//! The port of the Kotlin server, written against `contract/openapi.yaml` and kept honest
//! by the behaviour tests that came with it. The Kotlin one keeps running until this one
//! passes them.

use std::sync::Arc;

use anyhow::{Context, Result};
use tracing_subscriber::EnvFilter;

use leaf_server::api::keys::Keys;
use leaf_server::api::pages::Pages;
use leaf_server::api::routes::{router, AppState};
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

    // Two modes, the same as the Kotlin:
    //   leaf scan  [roots…] [--no-dimensions]   analyse and exit, with a report
    //   leaf serve [roots…]                     scan then listen
    let args: Vec<String> = std::env::args().skip(1).collect();
    let command = args.first().cloned().unwrap_or_else(|| "serve".into());
    let options: Vec<&String> = args.iter().filter(|a| a.starts_with("--")).collect();
    let requested: Vec<std::path::PathBuf> = args
        .iter()
        .skip(1)
        .filter(|a| !a.starts_with("--"))
        .map(std::path::PathBuf::from)
        .collect();

    let config = Config::from_env();
    let keys = Keys::parse(std::env::var("LEAF_KEYS").ok().as_deref())?;

    // Refused rather than warned about. A server bound to the whole network with no key is
    // not a risky configuration, it is an open library — and a line in a log nobody is
    // reading at that moment protects nothing.
    if config.host != "127.0.0.1" && config.host != "localhost" && keys.open() {
        anyhow::bail!(
            "LEAF_HOST is {} and no key is configured: anyone who can reach this port would \
             read the library. Set LEAF_KEYS, or bind to 127.0.0.1.",
            config.host
        );
    }

    let db = Arc::new(Db::open(&config.db).context("opening the index")?);

    let roots: Vec<std::path::PathBuf> = if requested.is_empty() {
        vec![config.library.clone()]
    } else {
        requested
    };

    if command == "scan" {
        let started = std::time::Instant::now();
        let scanner = Scanner::new(
            Arc::clone(&db),
            !options.iter().any(|o| *o == "--no-dimensions"),
        );
        let report = scanner.scan(&roots)?;
        tracing::info!(seconds = started.elapsed().as_secs(), "Scan finished");
        println!();
        println!("{}", report.summary());
        return Ok(());
    }

    std::fs::create_dir_all(&config.inbox).context("creating the inbox")?;
    warn_if_split_volumes(&config.library, &config.inbox);
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
    let quality = std::env::var("LEAF_JPEG_QUALITY")
        .ok()
        .and_then(|v| v.parse::<f32>().ok())
        .map(|q| (q.clamp(0.4, 1.0) * 100.0) as u8)
        .unwrap_or(85);
    let cache_budget = std::env::var("LEAF_MAX_CACHE_MB")
        .ok()
        .and_then(|v| v.parse::<u64>().ok())
        .unwrap_or(4096)
        * 1024
        * 1024;
    let pages = Pages::new(Arc::clone(&db), config.cache.clone(), quality, cache_budget);

    let all_dimensions = !options.iter().any(|o| *o == "--no-dimensions");
    let state = AppState::with_pages(Arc::clone(&db), keys, pages)
        .with_library(roots.clone(), all_dimensions)
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
            let hosts = if config.tls_hosts.is_empty() {
                vec![config.host.clone()]
            } else {
                config.tls_hosts.clone()
            };
            let tls = Tls::of(certificate, key, &hosts).await?;
            tracing::info!("Listening on https://{address}");
            // The same graceful stop the plain path has. Without it a container stop cut
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

/// Answers the signal a container stop sends, so the writer finishes what it holds.
async fn shutdown() {
    let _ = tokio::signal::ctrl_c().await;
    tracing::info!("Shutting down");
}

/// Getting this wrong breaks nothing immediately, but makes every import slow and
/// non-atomic: committing is a rename, and a rename across two volumes is a copy.
fn warn_if_split_volumes(library: &std::path::Path, inbox: &std::path::Path) {
    let device = |path: &std::path::Path| {
        #[cfg(unix)]
        {
            use std::os::unix::fs::MetadataExt;
            std::fs::metadata(path).ok().map(|m| m.dev())
        }
        #[cfg(not(unix))]
        {
            let _ = path;
            None::<u64>
        }
    };
    if let (Some(a), Some(b)) = (device(library), device(inbox)) {
        if a != b {
            tracing::warn!(
                library = %library.display(),
                inbox = %inbox.display(),
                "the inbox and the library sit on two volumes: committing will copy instead of rename"
            );
        }
    }
}
