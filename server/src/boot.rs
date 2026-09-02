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
/// leaf-server scan  [roots…] [--no-dimensions]   analyse and exit, with a report
/// leaf-server serve [roots…] [--no-dimensions]   scan, then listen
/// ```
///
/// `serve` is the default, because a server started with no argument should serve.
///
/// Built only through [`Invocation::of`], which returns an [`Outcome`] rather than this
/// directly — a command or an option it does not recognise has to reach `main` as something
/// it can fail on, and `--help` is not a run at all. Before that existed, an unrecognised
/// argument was simply not there: `leaf-server --help` served the library, and
/// `leaf-server scan --help` scanned it, both times because the first argument became
/// `command` unchecked and every other `--flag` but one was dropped by a filter that never
/// said what it had dropped. Deployed for the first time on 2 September 2026, that swallowed
/// the same question twice in a row.
#[derive(Debug, PartialEq, Eq)]
pub struct Invocation {
    pub command: String,
    pub requested: Vec<PathBuf>,
    /// Measuring every page is most of a scan's cost, and the only thing that needs it is
    /// the dimensions in the index.
    pub all_dimensions: bool,
}

/// The three ways a command line can be answered: run something, print usage and stop, or
/// refuse. Only the last of those is an error — `Invocation::of` returns `Ok` for `--help`
/// too, because being asked how to run this is not a mistake.
#[derive(Debug)]
pub enum Outcome {
    /// Proceed with this invocation.
    Run(Invocation),
    /// `--help` was asked. Usage to print on stdout before exiting 0.
    Usage(&'static str),
}

/// The commands `Invocation::of` understands. Named once so the refusal that lists them and
/// the check that accepts them cannot drift apart.
const COMMANDS: [&str; 2] = ["scan", "serve"];

/// Printed for `--help`, and nowhere else — so the options this claims to accept and the
/// ones `Invocation::of` actually parses cannot quietly disagree.
const USAGE: &str = "\
Usage: leaf-server [scan|serve] [roots...] [--no-dimensions]

  scan             analyse the roots and exit, with a report
  serve            scan, then listen (the default when no command is given)

  roots            paths to walk; the configured library when none are given
  --no-dimensions  skip measuring each page's width and height
  --help           print this message and exit";

impl Invocation {
    /// Parses a command line into something `main` can run, print, or fail on.
    ///
    /// Pure on purpose: everything here is a `String` in and a value out, with no
    /// environment and no process to touch, which is the whole reason this lives in `boot`
    /// rather than in `main` — a test can hand it any argument list and read back exactly
    /// what a real invocation would have done with it.
    pub fn of<I, S>(args: I) -> Result<Outcome>
    where
        I: IntoIterator<Item = S>,
        S: Into<String>,
    {
        let args: Vec<String> = args.into_iter().map(Into::into).collect();

        // Checked before the command itself, and over the whole line rather than one
        // position: `leaf-server --help` and `leaf-server scan --help` both used to run
        // instead of answering, the first because `--help` was never compared against
        // anything and the second because it sat after the command word, where nothing
        // looked for it at all.
        if args.iter().any(|a| a == "--help") {
            return Ok(Outcome::Usage(USAGE));
        }

        let command = args.first().cloned().unwrap_or_else(|| "serve".into());
        if !COMMANDS.contains(&command.as_str()) {
            anyhow::bail!(
                "'{command}' is not a leaf-server command; the ones that exist are {}.",
                COMMANDS.join(" and ")
            );
        }

        let mut requested = Vec::new();
        let mut all_dimensions = true;
        for a in args.iter().skip(1) {
            if a == "--no-dimensions" {
                all_dimensions = false;
            } else if a.starts_with("--") {
                // Not filed as a root and not silently dropped either: the old filter kept
                // only `--no-dimensions` and let every other `--flag` — a typo among them —
                // pass through as though it meant nothing.
                anyhow::bail!("'{a}' is not a recognised option; run --help for usage.");
            } else {
                requested.push(PathBuf::from(a));
            }
        }

        Ok(Outcome::Run(Invocation {
            command,
            requested,
            all_dimensions,
        }))
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
