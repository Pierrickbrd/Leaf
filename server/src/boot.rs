//! What the server settles before it listens.
//!
//! Every one of these was a line inside `main`, where no test could reach it — and each is a
//! decision, not wiring: which roots to walk, whether a key is required to bind here, what
//! ceiling the cache gets, which hosts a certificate must name. `lib.rs` says the behaviour
//! tests are the oracle and that anything they cannot see is not covered; a hundred and
//! seventy lines of `main` were exactly that.
//!
//! What stays in the binary is what cannot be anything but wiring: installing the log
//! subscriber, binding a socket, waiting for a signal.

use std::path::{Path, PathBuf};

use anyhow::Result;

use crate::api::keys::Keys;

/// What the command line asked for.
///
/// ```text
/// leaf scan  [roots…] [--no-dimensions]   analyse and exit, with a report
/// leaf serve [roots…]                     scan, then listen
/// ```
///
/// `serve` is the default, because a server started with no argument should serve.
#[derive(Debug, PartialEq, Eq)]
pub struct Invocation {
    pub command: String,
    pub requested: Vec<PathBuf>,
    /// Measuring every page is most of a scan's cost, and the only thing that needs it is
    /// the dimensions in the index.
    pub all_dimensions: bool,
}

impl Invocation {
    pub fn of<I, S>(args: I) -> Self
    where
        I: IntoIterator<Item = S>,
        S: Into<String>,
    {
        let args: Vec<String> = args.into_iter().map(Into::into).collect();
        Invocation {
            command: args.first().cloned().unwrap_or_else(|| "serve".into()),
            requested: args
                .iter()
                .skip(1)
                .filter(|a| !a.starts_with("--"))
                .map(PathBuf::from)
                .collect(),
            all_dimensions: !args.iter().any(|a| a == "--no-dimensions"),
        }
    }

    pub fn is_scan(&self) -> bool {
        self.command == "scan"
    }

    /// The roots to walk: the ones named, or the configured library when none were.
    pub fn roots(&self, library: &Path) -> Vec<PathBuf> {
        if self.requested.is_empty() {
            vec![library.to_path_buf()]
        } else {
            self.requested.clone()
        }
    }
}

/// Refuses a server bound past the loopback with no key configured.
///
/// Refused rather than warned about. A server bound to the whole network with no key is not
/// a risky configuration, it is an open library — and a line in a log nobody is reading at
/// that moment protects nothing.
pub fn refuse_an_open_library(host: &str, keys: &Keys) -> Result<()> {
    if host != "127.0.0.1" && host != "localhost" && keys.open() {
        anyhow::bail!(
            "LEAF_HOST is {host} and no key is configured: anyone who can reach this port \
             would read the library. Set LEAF_KEYS, or bind to 127.0.0.1."
        );
    }
    Ok(())
}

/// The JPEG quality for resized pages, as the encoder counts it.
///
/// Clamped rather than refused: the setting is a dial, and a dial turned past its end should
/// stop at the end. Below 0.4 the artefacts are visible on line art; above 1.0 there is
/// nothing left to ask for.
pub fn jpeg_quality(set: Option<&str>) -> u8 {
    set.and_then(|v| v.parse::<f32>().ok())
        .map(|q| (q.clamp(0.4, 1.0) * 100.0) as u8)
        .unwrap_or(85)
}

/// The ceiling on the page cache, in bytes.
pub fn cache_ceiling(set: Option<&str>) -> u64 {
    set.and_then(|v| v.parse::<u64>().ok()).unwrap_or(4096) * 1024 * 1024
}

/// The hosts a self-signed certificate has to name.
///
/// The address it is bound to, unless a list was given: a certificate that names nothing the
/// client typed fails hostname verification, and the commonest case is that there is only
/// one name and it is the one in `LEAF_HOST`.
pub fn tls_hosts(configured: &[String], host: &str) -> Vec<String> {
    if configured.is_empty() {
        vec![host.to_string()]
    } else {
        configured.to_vec()
    }
}

/// Whether the inbox and the library are known to sit on different volumes.
///
/// Getting this wrong breaks nothing immediately, but makes every import slow and
/// non-atomic: committing is a rename, and a rename across two volumes is a copy. `false`
/// when either cannot be read — a folder that is not there yet is not a wrong answer, it is
/// no answer, and warning about one would be noise at every first start.
pub fn split_volumes(library: &Path, inbox: &Path) -> bool {
    let device = |path: &Path| {
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
    matches!((device(library), device(inbox)), (Some(a), Some(b)) if a != b)
}
