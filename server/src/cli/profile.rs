//! `leaf-server profile reset-code`: a forgotten code, replaced from the server's terminal.
//!
//! The code is the one thing a profile screen cannot give back: whoever forgot it cannot pass
//! the check that would let them change it. What is left is the machine itself, and whoever
//! can run a command on it under `sudo` already owns the index. So the reset lives here and
//! nowhere on the network.
//!
//! **The code is never echoed.** It is typed on a terminal that may be shared in a screen
//! recording or read over a shoulder, and it is asked twice because nothing on the screen
//! says which key was struck. Nothing here logs: no subscriber is installed on this path.

use std::io::{self, BufRead, Write};
use std::process::ExitCode;
use std::sync::Arc;

use anyhow::{bail, Context, Result};

use crate::config::Config;
use crate::store::profiles::{CodeScope, ProfileRefusal, Profiles};
use crate::store::Db;

use super::device::Console;
use super::{become_owner, Settings};

const ASK: &str = "Nouveau code : ";
const ASK_AGAIN: &str = "Une seconde fois : ";

/// Runs `profile reset-code`.
///
/// Settings, then the account, then the index, in the order `device` uses and for the same
/// reason: a write made as root leaves `leaf.sqlite-wal` owned by root, and the service could
/// no longer open its own index (`cli::become_owner`).
pub fn run(console: &mut Console) -> Result<ExitCode> {
    let settings = Settings::read()?;
    let config = Config::read(|name| settings.get(name));
    become_owner(&config.db)?;
    let db = Arc::new(Db::open(&config.db).context("ouverture de l'index")?);
    let said = reset_code(db, read_without_echo)?;
    writeln!(console.out, "{said}")?;
    Ok(ExitCode::SUCCESS)
}

/// Asks for the new code twice and sets it, keeping what the old one was for.
///
/// `read` is given the prompt and returns what was typed, so a test hands it answers and the
/// binary hands it [`read_without_echo`]. A refusal is an error: two different entries and a
/// length outside 4 to 8 both change nothing and exit non-zero, which is what a script that
/// ran this would need to see.
///
/// Scope: the existing one stays, because a household that kept its code for « everything »
/// has not asked to lose that by forgetting it. With no code at all there is nothing to keep,
/// and `Restricted` is the lesser of the two.
pub fn reset_code(db: Arc<Db>, mut read: impl FnMut(&str) -> io::Result<String>) -> Result<String> {
    let profiles = Profiles::new(db);
    let scope = profiles.code()?.unwrap_or(CodeScope::Restricted);

    let first = read(ASK).context("lecture du code")?;
    let second = read(ASK_AGAIN).context("lecture du code")?;
    if first != second {
        bail!("Les deux saisies ne sont pas identiques : le code n'a pas changé.");
    }
    let set = profiles.set_code(&first, scope);
    // The code is a secret: it does not outlive the call that needed it.
    drop(first);
    drop(second);
    match set? {
        Ok(()) => {
            profiles.forget_recovery()?;
            Ok(
                "Le code a été remplacé, et l'ancien code de secours ne marche plus : \
                tirez-en un nouveau depuis les réglages."
                    .to_string(),
            )
        }
        Err(ProfileRefusal::BadCode) => {
            bail!("Le code doit faire de 4 à 8 caractères : il n'a pas changé.")
        }
        // `set_code` answers nothing else; if it ever does, the sentence still says the one
        // thing the person needs, that nothing was written.
        Err(_) => bail!("Le code n'a pas été accepté : il n'a pas changé."),
    }
}

/// The terminal attributes to put back, readable from a signal handler, which can touch
/// nothing but statics. Written before the handlers are installed and read only by them: one
/// read at a time, in a process that is a single command.
static mut SAVED_TERMIOS: std::mem::MaybeUninit<libc::termios> = std::mem::MaybeUninit::uninit();

/// Restores the terminal and dies of the signal it was sent.
///
/// The incident this answers: Ctrl-C or `kill` during the prompt ends the process before any
/// `Drop` runs, and the shell that was typed in is left with echo off — nothing it is typed
/// shows, until `reset` is typed blind. `tcsetattr`, `signal` and `raise` are
/// async-signal-safe; the default action is put back first so the exit status is still the
/// signal's, which is what a script that interrupted this would look for.
extern "C" fn restore_and_die(signal: libc::c_int) {
    // SAFETY: SAVED_TERMIOS was written before this handler was installed; the calls below
    // are on integers and that value only.
    unsafe {
        let saved = std::ptr::addr_of!(SAVED_TERMIOS) as *const libc::termios;
        libc::tcsetattr(0, libc::TCSADRAIN, saved);
        libc::signal(signal, libc::SIG_DFL);
        libc::raise(signal);
    }
}

const SIGNALS: [libc::c_int; 3] = [libc::SIGINT, libc::SIGTERM, libc::SIGHUP];

/// Puts the terminal's echo back on every way out of the read: normally and on an error by
/// `Drop`, on Ctrl-C, SIGTERM and SIGHUP by [`restore_and_die`], which `Drop` cannot do
/// because a process killed by a signal runs no destructor. Only SIGKILL leaves the echo off,
/// and nothing can answer that.
struct Echo {
    saved: libc::termios,
    previous: Vec<(libc::c_int, libc::sigaction)>,
}

impl Echo {
    /// Turns echo off on stdin, or `None` when stdin is not a terminal (a pipe, a test) and
    /// there is nothing to turn off.
    fn off() -> Option<Echo> {
        // SAFETY: plain calls on fd 0 with a zeroed termios that tcgetattr fills in; the
        // static is written once here, before any handler that reads it exists.
        unsafe {
            if libc::isatty(0) != 1 {
                return None;
            }
            let mut saved: libc::termios = std::mem::zeroed();
            if libc::tcgetattr(0, &mut saved) != 0 {
                return None;
            }
            std::ptr::addr_of_mut!(SAVED_TERMIOS).write(std::mem::MaybeUninit::new(saved));

            let mut action: libc::sigaction = std::mem::zeroed();
            action.sa_sigaction = restore_and_die as extern "C" fn(libc::c_int) as usize;
            libc::sigemptyset(&mut action.sa_mask);
            let mut previous = Vec::new();
            for signal in SIGNALS {
                let mut old: libc::sigaction = std::mem::zeroed();
                if libc::sigaction(signal, &action, &mut old) == 0 {
                    previous.push((signal, old));
                }
            }

            let mut silent = saved;
            silent.c_lflag &= !libc::ECHO;
            let guard = Echo { saved, previous };
            if libc::tcsetattr(0, libc::TCSADRAIN, &silent) != 0 {
                return None; // dropped: handlers put back
            }
            Some(guard)
        }
    }
}

impl Drop for Echo {
    fn drop(&mut self) {
        // SAFETY: restores the attributes and the handlers read in `off`. Not TCSAFLUSH:
        // that would throw away what the person typed ahead of the prompt.
        unsafe {
            libc::tcsetattr(0, libc::TCSADRAIN, &self.saved);
            for (signal, old) in &self.previous {
                libc::sigaction(*signal, old, std::ptr::null_mut());
            }
        }
    }
}

/// One line from stdin after `prompt` on stderr, without echo when stdin is a terminal.
///
/// The prompt goes to stderr so that a pipe on stdout still carries only the answer.
pub fn read_without_echo(prompt: &str) -> io::Result<String> {
    let mut err = io::stderr();
    err.write_all(prompt.as_bytes())?;
    err.flush()?;
    let guard = Echo::off();
    let mut line = String::new();
    let read = io::stdin().lock().read_line(&mut line);
    // The newline the person typed was not echoed either.
    if guard.is_some() {
        err.write_all(b"\n")?;
    }
    drop(guard);
    if read? == 0 {
        return Err(io::ErrorKind::UnexpectedEof.into());
    }
    Ok(line.trim_end_matches(['\n', '\r']).to_string())
}
