//! What the server settles before it listens.
//!
//! Every one of these was a line inside `main`, where no test could reach it — and each is a
//! decision, not wiring: which roots to walk, whether this address is served over https, what
//! ceiling the cache gets, which hosts a certificate must name. `lib.rs` says the behaviour
//! tests are the oracle and that anything they cannot see is not covered; a hundred and
//! seventy lines of `main` were exactly that.
//!
//! What stays in the binary is what cannot be anything but wiring: installing the log
//! subscriber, binding a socket, waiting for a signal.

use std::path::{Path, PathBuf};

use anyhow::Result;

use crate::config::Config;

/// What the command line asked for.
///
/// ```text
/// leaf-server scan  [roots…] [--no-dimensions]   analyse and exit, with a report
/// leaf-server serve [roots…] [--no-dimensions]   scan, then listen
/// leaf-server device add|list|revoke|enroll …    the linked devices, see [`Device`]
/// leaf-server profile reset-code                 replace a forgotten code
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
    /// What to do to the linked devices, for `device` and for no other command.
    pub device: Option<Device>,
    /// What to do to the profiles, for `profile` and for no other command.
    pub profile: Option<ProfileCommand>,
}

/// The words after `profile`.
#[derive(Debug, PartialEq, Eq)]
pub enum ProfileCommand {
    /// A new code, typed twice at the terminal, for a household that forgot it.
    ResetCode,
}

/// The words after `device`, read as strictly as the rest of the line: an option one
/// subcommand does not take is refused on it, not carried along unread.
#[derive(Debug, PartialEq, Eq)]
pub enum Device {
    /// A code, and a link for each address the server can be reached on.
    Add {
        /// Replaces `LEAF_PUBLIC_URL` and the listening addresses, this once.
        address: Option<String>,
        /// The label of the link whose QR code is drawn, when not the first one's.
        qr: Option<String>,
    },
    List,
    /// A device by its name, or a waiting code by the code itself.
    Revoke(String),
    /// A device linked on the spot, its key on stdout: for the installer, which links the
    /// client on the same machine without a network round trip.
    Enroll(String),
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
const COMMANDS: [&str; 4] = ["scan", "serve", "device", "profile"];

/// Printed for `--help`, and nowhere else — so the options this claims to accept and the
/// ones `Invocation::of` actually parses cannot quietly disagree.
const USAGE: &str = "\
Usage: leaf-server [scan|serve] [roots...] [--no-dimensions]
       leaf-server device add [--address URL] [--qr LABEL]
       leaf-server device list
       leaf-server device revoke NAME|CODE
       leaf-server device enroll NAME
       leaf-server profile reset-code

  scan             analyse the roots and exit, with a report
  serve            scan, then listen (the default when no command is given)

  roots            paths to walk; the configured library when none are given
  --no-dimensions  skip measuring each page's width and height

  device add       make a one-time code and print a leaf:// link and its QR code for
                   each address the server is reached on
    --address URL  link to this address instead, this once
    --qr LABEL     draw the QR code of the link with this label, not the first one
  device list      the linked devices, the keys in LEAF_KEYS and the waiting codes
  device revoke    remove a device by its name, or cancel a waiting code
  device enroll    link a device now and print its key on stdout, which must not be
                   a terminal (for the installer)

  profile reset-code
                   replace a forgotten code: asks twice, shows nothing, keeps what the
                   old code was for (everything, or only leaving a restricted profile)

  The device and profile commands read /etc/leaf/server.env (or $LEAF_ENV_FILE)
  and, run as root, open the index as the account that owns its folder.

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
                "'{command}' is not a leaf-server command; the ones that exist are {} and {}.",
                COMMANDS[..COMMANDS.len() - 1].join(", "),
                COMMANDS[COMMANDS.len() - 1]
            );
        }
        if command == "device" {
            return Ok(Outcome::Run(Invocation {
                command,
                requested: Vec::new(),
                all_dimensions: true,
                device: Some(Device::of(&args[1..])?),
                profile: None,
            }));
        }
        if command == "profile" {
            return Ok(Outcome::Run(Invocation {
                command,
                requested: Vec::new(),
                all_dimensions: true,
                device: None,
                profile: Some(ProfileCommand::of(&args[1..])?),
            }));
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
            device: None,
            profile: None,
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

impl ProfileCommand {
    /// The words after `profile`.
    fn of(words: &[String]) -> Result<ProfileCommand> {
        match words {
            [] => anyhow::bail!("'profile' needs a subcommand: reset-code."),
            [what] if what == "reset-code" => Ok(ProfileCommand::ResetCode),
            [what, ..] if what == "reset-code" => {
                anyhow::bail!("'profile reset-code' takes no '{}'.", words[1])
            }
            [other, ..] => {
                anyhow::bail!("'{other}' is not something 'profile' does; it does reset-code.")
            }
        }
    }
}

impl Device {
    /// The words after `device`.
    fn of(words: &[String]) -> Result<Device> {
        const SUBCOMMANDS: &str = "add, list, revoke NAME|CODE and enroll NAME";
        let Some((what, rest)) = words.split_first() else {
            anyhow::bail!("'device' needs a subcommand: {SUBCOMMANDS}.");
        };
        // An option on a subcommand that takes none is named as such rather than read as a
        // device called « --qr ».
        let no_option = |rest: &[String]| -> Result<()> {
            match rest.iter().find(|a| a.starts_with("--")) {
                Some(a) => anyhow::bail!("'{a}' is not an option of 'device {what}'."),
                None => Ok(()),
            }
        };
        let one_name = |rest: &[String]| -> Result<String> {
            no_option(rest)?;
            match rest {
                [name] => Ok(name.clone()),
                [] => anyhow::bail!("'device {what}' needs a name."),
                [_, extra, ..] => anyhow::bail!(
                    "'device {what}' takes one name, and '{extra}' is a second one; \
                     quote a name that has spaces in it."
                ),
            }
        };
        match what.as_str() {
            "add" => {
                let (mut address, mut qr) = (None, None);
                let mut rest = rest.iter();
                while let Some(a) = rest.next() {
                    let slot = match a.as_str() {
                        "--address" => &mut address,
                        "--qr" => &mut qr,
                        _ if a.starts_with("--") => {
                            anyhow::bail!("'{a}' is not an option of 'device add'.")
                        }
                        _ => anyhow::bail!("'device add' takes no '{a}'; run --help for usage."),
                    };
                    match rest.next() {
                        Some(value) if !value.starts_with("--") => *slot = Some(value.clone()),
                        _ => anyhow::bail!("'{a}' needs a value; run --help for usage."),
                    }
                }
                Ok(Device::Add { address, qr })
            }
            "list" => {
                no_option(rest)?;
                match rest.first() {
                    Some(extra) => anyhow::bail!("'device list' takes no '{extra}'."),
                    None => Ok(Device::List),
                }
            }
            "revoke" => one_name(rest).map(Device::Revoke),
            "enroll" => one_name(rest).map(Device::Enroll),
            other => {
                anyhow::bail!("'{other}' is not something 'device' does; it does {SUBCOMMANDS}.")
            }
        }
    }
}

/// Whether a server bound to `host` can only be reached from the machine it runs on.
///
/// The three spellings and nothing else: a host this does not recognise is treated as the
/// network, which closes a library rather than opening one.
pub fn is_loopback(host: &str) -> bool {
    matches!(host, "127.0.0.1" | "::1") || host.eq_ignore_ascii_case("localhost")
}

/// The certificate and key to serve with, or `None` to speak plain http.
///
/// Plain http on the loopback only, and only when no certificate is named: nothing leaves the
/// machine there, and it is how a proxy or a tunnel beside the server talks to it. Anywhere
/// else it is https, with no setting to turn that off — the key travels on every request, and
/// in clear on a home network it is anyone's who listens. A certificate nobody named is made
/// and kept beside the index, which is the server's state folder already: a default anywhere
/// else would be one more folder an installer has to create and own.
pub fn tls_paths(config: &Config) -> Option<(PathBuf, PathBuf)> {
    if let (Some(certificate), Some(key)) = (&config.tls_certificate, &config.tls_key) {
        return Some((certificate.clone(), key.clone()));
    }
    if is_loopback(&config.host) {
        return None;
    }
    let folder = config
        .db
        .parent()
        .unwrap_or_else(|| Path::new("."))
        .join("tls");
    Some((folder.join("leaf.crt"), folder.join("leaf.key")))
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
