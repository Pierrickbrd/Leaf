//! The commands a person types on the server, beside the one that serves.
//!
//! They run under `sudo`, so two things come first, before any of them touches the index:
//! the settings the service runs with, read from its environment file, and the account the
//! service runs as, which the command becomes.

pub mod device;
pub mod env_file;
pub mod profile;

use std::collections::BTreeMap;
use std::path::Path;

use anyhow::{bail, Context, Result};

/// Where install.sh writes the service's settings.
pub const ENV_FILE: &str = "/etc/leaf/server.env";

/// The unit's `StateDirectory`, where install.sh puts the index.
pub const STATE_DIRECTORY: &str = "/var/lib/leaf";

/// The settings as the service sees them: its environment file, under the process's own
/// environment.
pub struct Settings {
    file: BTreeMap<String, String>,
}

impl Settings {
    /// Reads `$LEAF_ENV_FILE`, or [`ENV_FILE`], if it is there.
    ///
    /// Absent is fine — a server installed by hand is configured by its environment alone.
    /// Present and unreadable is not: it means the command was typed without sudo, and going
    /// on without the file would quietly open some other index than the service's.
    pub fn read() -> Result<Settings> {
        let path = std::env::var_os("LEAF_ENV_FILE")
            .map(std::path::PathBuf::from)
            .unwrap_or_else(|| ENV_FILE.into());
        let file = match std::fs::read_to_string(&path) {
            Ok(text) => env_file::read_env_file(&text),
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => BTreeMap::new(),
            Err(e) => {
                return Err(e).with_context(|| {
                    format!(
                        "lecture de {} — lancez la commande avec sudo",
                        path.display()
                    )
                })
            }
        };
        Ok(Settings { file })
    }

    /// The process's value when it has a non-empty one, the file's otherwise: what is typed
    /// on the command line wins over what is configured, as it would for any program.
    pub fn get(&self, name: &str) -> Option<String> {
        std::env::var(name)
            .ok()
            .filter(|v| !v.is_empty())
            .or_else(|| self.file.get(name).cloned())
    }
}

/// `text` with every control character written out as `\u{…}`, for a value read from the
/// index before it reaches a terminal.
///
/// Names are refused with a control character in them (`Refusal::NameInvalid`), and this is
/// the second wall: a row written before that rule existed, or by hand, would otherwise put
/// an escape sequence in front of the operator — one that can clear the screen, retitle the
/// window, or rewrite the lines above it to say something else.
pub fn printable(text: &str) -> String {
    text.chars()
        .map(|c| {
            if c.is_control() {
                c.escape_unicode().to_string()
            } else {
                c.to_string()
            }
        })
        .collect()
}

/// Which account opens the index.
#[derive(Debug, PartialEq, Eq)]
pub enum Account {
    /// The one running the command.
    AsItIs,
    /// The owner of the index's folder, which is the service's account.
    Become { uid: u32, gid: u32 },
}

/// The account to open an index whose folder is `folder`, owned by `owner` (uid, gid), from
/// a command running with effective uid `euid`.
///
/// Root becomes the owner. A folder root owns itself is refused, except the unit's
/// `StateDirectory`: systemd hands that one back to the service at its next start, files
/// below it included, so a write made there as root does not outlive the restart. Anywhere
/// else nothing would, and the service — which never runs as root — could no longer write
/// its own index. Anyone but root opens the index as itself: an installation by hand, or a
/// test.
pub fn account_for(euid: u32, folder: &Path, owner: (u32, u32)) -> Result<Account> {
    let (uid, gid) = owner;
    if euid != 0 {
        return Ok(Account::AsItIs);
    }
    if uid != 0 {
        return Ok(Account::Become { uid, gid });
    }
    if folder == Path::new(STATE_DIRECTORY) {
        return Ok(Account::AsItIs);
    }
    bail!(
        "{} appartient à root : la base serait écrite par root et le service ne pourrait \
         plus l'écrire. Donnez ce dossier au compte du service (chown), puis recommencez.",
        folder.display()
    )
}

/// Gives up root for the account that owns the index's folder, before the index is opened.
///
/// The incident this avoids: `sudo leaf-server device add` opening the index as root, which
/// creates `leaf.sqlite-wal` and `leaf.sqlite-shm` owned by root beside it — after which the
/// service, running as its own account, cannot open its index for writing and stops serving.
/// The certificate made for a link (`tls::own_fingerprint`) is written by the same account,
/// for the same reason.
///
/// Supplementary groups first and the user last: once the uid is given up, nothing else can
/// be. Called before any thread is started, so that the whole process changes account.
#[cfg(unix)]
pub fn become_owner(db: &Path) -> Result<()> {
    use std::os::unix::fs::MetadataExt;

    // SAFETY: geteuid has no preconditions and cannot fail.
    let euid = unsafe { libc::geteuid() };
    if euid != 0 {
        return Ok(());
    }
    let folder = match db.parent() {
        Some(parent) if !parent.as_os_str().is_empty() => parent,
        _ => Path::new("."),
    };
    let facts = std::fs::metadata(folder)
        .with_context(|| format!("lecture du propriétaire de {}", folder.display()))?;
    let Account::Become { uid, gid } = account_for(euid, folder, (facts.uid(), facts.gid()))?
    else {
        return Ok(());
    };
    // SAFETY: plain system calls on integers; setgroups is handed no list at all.
    let failed = unsafe {
        libc::setgroups(0, std::ptr::null()) != 0
            || libc::setgid(gid) != 0
            || libc::setuid(uid) != 0
    };
    if failed {
        return Err(std::io::Error::last_os_error())
            .with_context(|| format!("passage au compte {uid}:{gid}"));
    }
    // A uid that can be taken back was not given up.
    // SAFETY: as above.
    let regained = unsafe { libc::setuid(0) == 0 };
    if regained || unsafe { libc::geteuid() } != uid || unsafe { libc::getegid() } != gid {
        bail!("le compte {uid}:{gid} n'a pas pu être pris pour de bon");
    }
    Ok(())
}

#[cfg(not(unix))]
pub fn become_owner(_: &Path) -> Result<()> {
    Ok(())
}
