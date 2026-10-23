//! Leaf — a reading server for a comics library.
//!
//! Wiring, and only wiring. Everything this used to decide for itself now lives in
//! `boot`, where the tests can reach it: what stays here is installing the log subscriber,
//! opening the index, binding a socket and waiting for a signal — the three or four things
//! that are not a decision at all.

use std::process::ExitCode;
use std::sync::Arc;

use anyhow::{Context, Result};
use tracing_subscriber::EnvFilter;

use leaf_server::api::keys::Keys;
use leaf_server::api::pages::Pages;
use leaf_server::api::routes::{router, AppState};
use leaf_server::boot::{
    cache_ceiling, jpeg_quality, split_volumes, tls_hosts, tls_paths, Invocation, Outcome,
};
use leaf_server::config::Config;
use leaf_server::net::link::Reach;
use leaf_server::net::tls::Tls;
use leaf_server::scan::scanner::Scanner;
use leaf_server::store::Db;

fn main() -> Result<ExitCode> {
    // `--help` and an argument this cannot read are both settled here, before `Config`
    // touches the environment or `Db::open` touches the disk — a question about how to run
    // this, or a typo in how it was asked to, must not start anything on its way to an
    // answer.
    let arguments: Vec<String> = std::env::args().skip(1).collect();
    if arguments.as_slice() == ["--version"] {
        println!("leaf-server {}", env!("LEAF_VERSION"));
        return Ok(ExitCode::SUCCESS);
    }

    let asked = match Invocation::of(arguments)? {
        Outcome::Usage(usage) => {
            println!("{usage}");
            return Ok(ExitCode::SUCCESS);
        }
        Outcome::Run(it) => it,
    };

    // Before the runtime and before the log subscriber. Before the runtime, because a
    // `device` command may give up root (`cli::become_owner`), and an account is changed for
    // the whole process only while it is the one thread there is. Before the subscriber,
    // because nothing on that path is to be logged: one of its commands holds a device key.
    if let Some(device) = &asked.device {
        use std::io::IsTerminal;
        let terminal = std::io::stdout().is_terminal();
        let (mut out, mut err) = (std::io::stdout().lock(), std::io::stderr().lock());
        let mut console = leaf_server::cli::device::Console {
            out: &mut out,
            err: &mut err,
            terminal,
        };
        return leaf_server::cli::device::run(device, &mut console);
    }

    tracing_subscriber::fmt()
        .with_env_filter(
            EnvFilter::try_from_env("LEAF_LOG").unwrap_or_else(|_| EnvFilter::new("info")),
        )
        .with_target(false)
        .init();

    tokio::runtime::Builder::new_multi_thread()
        .enable_all()
        .build()
        .context("starting the runtime")?
        .block_on(serve(asked))
        .map(|()| ExitCode::SUCCESS)
}

/// `scan` and `serve`: what `#[tokio::main]` used to wrap, unchanged.
async fn serve(asked: Invocation) -> Result<()> {
    let config = Config::from_env();
    // No refusal here any more for a host past the loopback with no key: the request guard
    // closes such a library instead (`AppState::bound_to`), answering only `/health` and the
    // code exchange — refusing to start left a server on a home network with no way to link
    // its first device.
    let keys = Keys::parse(std::env::var("LEAF_KEYS").ok().as_deref())?;

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

    // Here, at the top of the serve path, and not where it is waited on. Everything below
    // takes time — generating a certificate on first start most of all — and a signal that
    // arrives before the handler is registered is answered by the kernel's default
    // disposition, which is the death this exists to prevent.
    let stopping = Stopping::armed();

    tokio::fs::create_dir_all(&config.inbox)
        .await
        .context("creating the inbox")?;
    if split_volumes(&config.library, &config.inbox) {
        tracing::warn!(
            library = %config.library.display(),
            inbox = %config.inbox.display(),
            "the inbox and the library sit on two volumes: committing will copy instead of rename"
        );
    }
    if let Some(folder) = &config.drop {
        tokio::fs::create_dir_all(folder)
            .await
            .context("creating the drop folder")?;
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

    // Loaded before the state rather than where it is bound: the certificate's fingerprint is
    // what a device's relay pins in the link it hands another.
    let tls = match tls_paths(&config) {
        Some((certificate, key)) => Some(
            Tls::of(
                &certificate,
                &key,
                &tls_hosts(&config.tls_hosts, &config.host),
            )
            .await?,
        ),
        None => None,
    };
    let reach = Reach {
        public_url: config.public_url.clone(),
        ..Reach::listening_on(
            &config.host,
            config.port,
            tls.as_ref().map(|t| t.fingerprint_hex.clone()),
        )
    };

    // Once, here, and off the async threads: it asks LEAF_PUBLIC_URL which certificate it
    // presents, and the routes keep the answer.
    let relay = tokio::task::spawn_blocking(move || reach.settle())
        .await
        .context("settling where relayed links point")?;

    let state = AppState::with_pages(Arc::clone(&db), keys, pages)
        .relaying(relay)
        .with_library(roots.clone(), asked.all_dimensions)
        .trusting_proxy(config.trust_proxy)
        .bound_to(&config.host)
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

    // https whenever the server is reachable from anywhere but this machine, with no setting
    // to turn it off: a key sent in clear on a home network is anyone's who listens. Plain
    // http stays on the loopback, which is where a proxy or a tunnel beside it talks to it.
    let address = format!("{}:{}", config.host, config.port);
    let address: std::net::SocketAddr = address
        .parse()
        .with_context(|| format!("{address} is not an address to bind"))?;
    match tls {
        Some(tls) => {
            tracing::info!("Listening on https://{address}");
            // The same graceful stop the plain path has. Without it, stopping the unit cut
            // the process off mid-write while HTTP shut down politely — one behaviour for
            // one server, decided by which port it happened to be listening on.
            let handle = axum_server::Handle::new();
            let asked_to_stop = handle.clone();
            tokio::spawn(async move {
                stopping.wait().await;
                asked_to_stop.graceful_shutdown(Some(std::time::Duration::from_secs(10)));
            });
            axum_server::bind_rustls(address, tls.config)
                .handle(handle)
                .serve(app)
                .await
                .context("serving")
        }
        None => {
            let listener = tokio::net::TcpListener::bind(address)
                .await
                .with_context(|| format!("binding {address}"))?;
            tracing::info!("Listening on http://{address}");
            axum::serve(listener, app)
                .with_graceful_shutdown(stopping.wait())
                .await
                .context("serving")
        }
    }
}

/// Answers the two signals that ask a server to stop, so the writer finishes what it holds.
///
/// `ctrl_c` alone is `SIGINT`, which is what a terminal sends. `systemctl stop` sends
/// `SIGTERM` — the signal this was documented as answering, and the one nothing was
/// listening for: the default disposition killed the process where it stood, in the middle
/// of the write this exists to protect, and `SIGKILL` followed at `TimeoutStopSec`.
///
/// **Registering and waiting are two steps on purpose.** `tokio`'s handlers start listening
/// when their future is first polled, not when it is built, and on the TLS path that poll
/// happened inside a `tokio::spawn` issued after the certificate had been generated, signed
/// and written — the longest stretch of the whole start, on the one boot where those files
/// do not exist yet. A `systemctl stop` in that window met the default disposition, so the
/// fix for the missing handler had a hole of its own on the slowest path. [`Stopping::armed`]
/// is called before any of it; only the waiting is deferred.
///
/// **A stretch before this is still open, deliberately.** From `exec` to the call there is
/// the dynamic loader, the runtime, the configuration and `Db::open`, and a signal arriving
/// in there still meets the default disposition. Three things about it:
///
/// It cannot be armed any earlier than it is. Above the `scan` branch, `tokio` would take
/// the Ctrl-C that stops a scan and push it into a stream nobody polls — a scan of a large
/// library runs for minutes and could then not be interrupted at all, which is a worse thing
/// than the one being closed.
///
/// `Type=notify` does not close it either. `systemctl stop` on a unit that is still
/// activating sends `SIGTERM` like any other; what `READY=1` governs is when the unit counts
/// as started and when what depends on it may run.
///
/// And closing it properly means blocking both signals before the runtime has a thread to
/// inherit the mask — `pthread_sigmask` ahead of the runtime `main` now builds by hand, and
/// an unblock on each of the two paths — to protect a stretch where nothing is at stake: nothing has been written, no request is in
/// flight, the scan has not started, and SQLite rolls back a migration killed halfway. The
/// sentence at the top says what this is for, and it is the writer.
struct Stopping {
    #[cfg(unix)]
    interrupted: Option<tokio::signal::unix::Signal>,
    #[cfg(unix)]
    terminated: Option<tokio::signal::unix::Signal>,
}

impl Stopping {
    fn armed() -> Stopping {
        #[cfg(unix)]
        {
            use tokio::signal::unix::{signal, SignalKind};
            let arm = |kind: SignalKind, name: &str| match signal(kind) {
                Ok(it) => Some(it),
                Err(e) => {
                    tracing::warn!(error = %e, "cannot listen for {name}");
                    None
                }
            };
            let armed = Stopping {
                interrupted: arm(SignalKind::interrupt(), "SIGINT"),
                terminated: arm(SignalKind::terminate(), "SIGTERM"),
            };
            // Said by the registration itself, not by `main` on its behalf. `binary.rs` reads
            // the log to check this line comes *before* the certificate is generated, which
            // is the whole property; a line printed from somewhere else could be moved, or
            // left behind, without the handlers moving with it.
            //
            // And only when one of them actually registered. Printed unconditionally, it was
            // printed by a process where both `signal` calls had failed — fd exhaustion, a
            // seccomp profile — whose `wait()` is then a `pending()` that never returns, so
            // it never stops gracefully at all. The test greps for this sentence as its proof
            // the handlers are armed, and would have found it there and passed. A guard that
            // can see nothing is worth no more than no guard.
            if armed.interrupted.is_some() || armed.terminated.is_some() {
                tracing::info!("Stopping on SIGINT or SIGTERM when asked");
            }
            armed
        }
        #[cfg(not(unix))]
        Stopping {}
    }

    async fn wait(self) {
        #[cfg(unix)]
        {
            // Waiting on the other one rather than returning: a server that could not
            // register a handler must not shut down as though it had been asked to.
            async fn heard(it: Option<tokio::signal::unix::Signal>) {
                match it {
                    Some(mut it) => {
                        it.recv().await;
                    }
                    None => std::future::pending::<()>().await,
                }
            }
            tokio::select! {
                _ = heard(self.interrupted) => {}
                _ = heard(self.terminated) => {}
            }
        }
        #[cfg(not(unix))]
        {
            let _ = tokio::signal::ctrl_c().await;
        }
        tracing::info!("Shutting down");
    }
}
