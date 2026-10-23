//! `leaf-server device add | list | revoke | enroll`: the linked devices, from the terminal.
//!
//! What they print is French, like the installer that runs them: the person reading it is
//! the one who installed Leaf, not someone reading a log.
//!
//! **A key is never shown on a screen.** `enroll` is the only command that has one in hand,
//! and it writes it to the output `main` hands in for the installer to capture, refusing when
//! that output is a terminal. Nothing here logs: no subscriber is installed on this path at all.

use std::io::Write;
use std::process::ExitCode;
use std::sync::Arc;

use anyhow::{bail, Context, Result};

use crate::api::keys::Keys;
use crate::boot::{is_loopback, tls_hosts, tls_paths, Device};
use crate::config::Config;
use crate::net::link::{targets, Link, Note, Target, Targets};
use crate::net::tls::own_fingerprint;
use crate::store::devices::{Creator, Devices, Refusal, CODE_LIFETIME};
use crate::store::Db;

use super::{become_owner, printable, Settings};

const UNREACHABLE: &str =
    "Cette adresse ne répond pas depuis le serveur : l'empreinte est celle du certificat de Leaf.";
/// The same answer when Leaf itself serves plain http on the loopback and has no certificate
/// to offer in its place.
const UNREACHABLE_NO_CERTIFICATE: &str =
    "Cette adresse ne répond pas depuis le serveur, et Leaf n'a pas de certificat à sa place : \
     le lien ne porte pas d'empreinte.";
const PRIVATE_AUTHORITY: &str = "Ce certificat n'est pas signé par une autorité publique : \
     quand il sera renouvelé, il faudra relier les appareils à nouveau.";

/// What is said of a certificate that is neither Leaf's nor publicly signed, before the
/// renewal sentence. The link pins it whatever it is — so if something sits between this
/// machine and the address, it is that something the new device will trust, and the only
/// one who can tell is the operator, by comparing the fingerprint with the proxy's own.
fn not_leafs(fingerprint: &str) -> String {
    format!(
        "Le certificat présenté à cette adresse n'est pas celui de Leaf. Son empreinte \
         SHA-256 est {fingerprint}. Vérifiez que c'est bien celle du certificat de votre \
         proxy avant de donner ce lien : sinon, l'appareil ferait confiance à ce qui \
         s'interpose."
    )
}
const CLOSES_THE_LIBRARY: &str = "Dès que cet appareil sera relié, la bibliothèque sera fermée : \
     ce qui la lisait sans clé devra se relier.";
const KEY_ON_A_TERMINAL: &str = "device enroll écrit la clé d'un appareil sur sa sortie, et \
     cette sortie est un terminal : une clé ne s'affiche jamais à l'écran. Redirigez-la vers \
     un fichier ou un programme.";

/// Where a command writes: the standard streams `main` hands in, and whether its output is a
/// terminal.
///
/// Handed in rather than printed to, because `main.rs` is the one file of the server allowed
/// to reach the console (`tools/nothing_talks_to_the_console.py`), and because a write that
/// fails — stdout closed under it — has to come back as an error: the printing macros panic there,
/// and for `enroll` that would leave a device linked whose key nobody received.
pub struct Console<'a> {
    pub out: &'a mut dyn Write,
    pub err: &'a mut dyn Write,
    /// Whether `out` is a terminal: `enroll` refuses to write a key to one, and the QR code
    /// is inverted for one.
    pub terminal: bool,
}

/// Runs one `device` command, already read by `Invocation::of`.
///
/// Settings first, then the account, then the index — in that order because the settings
/// say where the index is, and the account has to be the service's before the index is
/// opened (`cli::become_owner`).
pub fn run(asked: &Device, console: &mut Console) -> Result<ExitCode> {
    // Before anything is read or written: a refusal that comes after the device was made
    // would leave a device whose key nobody holds.
    if matches!(asked, Device::Enroll(_)) && console.terminal {
        writeln!(console.err, "{KEY_ON_A_TERMINAL}")?;
        return Ok(ExitCode::from(2));
    }

    let settings = Settings::read()?;
    let config = Config::read(|name| settings.get(name));
    become_owner(&config.db)?;

    match asked {
        Device::Add { address, qr } => add(
            &config,
            &settings,
            address.as_deref(),
            qr.as_deref(),
            console,
        ),
        Device::List => list(&config, &settings, console),
        Device::Revoke(name_or_code) => revoke(&config, name_or_code, console),
        Device::Enroll(name) => enroll(&config, name, console),
    }
}

fn now() -> i64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_millis() as i64)
        .unwrap_or_default()
}

fn devices(config: &Config) -> Result<Devices> {
    let db = Db::open(&config.db).context("ouverture de l'index")?;
    Ok(Devices::new(Arc::new(db)))
}

fn add(
    config: &Config,
    settings: &Settings,
    address: Option<&str>,
    qr: Option<&str>,
    console: &mut Console,
) -> Result<ExitCode> {
    // Every target, and its fingerprint, before the code: a code made for a link that then
    // cannot be printed is a code nobody can use, waiting ten minutes in `device list`.
    let configured = address.map(str::to_string).or(config.public_url.clone());
    let own = || leafs_fingerprint(config);
    let targets: Vec<Target> =
        match targets(configured.as_deref(), &config.host, config.port, &own)? {
            Targets::Found(found) => found,
            Targets::OnlyHere => {
                writeln!(
                    console.out,
                    "Leaf n'écoute que sur cette machine ({}) : aucun autre appareil ne peut le \
                     joindre par cette adresse. Indiquez l'adresse publique dans LEAF_PUBLIC_URL \
                     (/etc/leaf/server.env), ou passez --address URL pour cette fois.",
                    config.host
                )?;
                return Ok(ExitCode::from(2));
            }
            Targets::NoneListening => bail!(
                "aucune adresse de cette machine ne répond à LEAF_HOST={} : indiquez \
                 l'adresse publique dans LEAF_PUBLIC_URL, ou passez --address URL.",
                config.host
            ),
        };

    let drawn = match qr {
        None => 0,
        Some(wanted) => targets
            .iter()
            .position(|t| {
                t.label
                    .as_deref()
                    .is_some_and(|l| l.eq_ignore_ascii_case(wanted))
            })
            .with_context(|| {
                let labels: Vec<&str> = targets.iter().filter_map(|t| t.label.as_deref()).collect();
                if labels.is_empty() {
                    format!("--qr {wanted} : il n'y a qu'un lien, sans étiquette")
                } else {
                    format!(
                        "--qr {wanted} : aucun lien ne porte cette étiquette ({})",
                        labels.join(", ")
                    )
                }
            })?,
    };

    let devices = devices(config)?;
    let open = is_loopback(&config.host)
        && Keys::parse(settings.get("LEAF_KEYS").as_deref())?.open()
        && !devices.closed()?;
    let issued = devices.issue(Creator::Terminal, now())?;

    let links: Vec<Link> = targets.iter().map(|t| t.link(&issued.code)).collect();
    for (target, link) in targets.iter().zip(&links) {
        match &target.label {
            Some(label) => writeln!(console.out, "Lien ({}) : {link}", printable(label))?,
            None => writeln!(console.out, "Lien : {link}")?,
        }
    }
    for target in &targets {
        for note in &target.notes {
            match note {
                Note::Unreachable => writeln!(console.out, "{UNREACHABLE}")?,
                Note::UnreachableNoCertificate => {
                    writeln!(console.out, "{UNREACHABLE_NO_CERTIFICATE}")?
                }
                Note::NotLeafs(fingerprint) => {
                    writeln!(console.out, "{}", not_leafs(fingerprint))?;
                    writeln!(console.out, "{PRIVATE_AUTHORITY}")?;
                }
            }
        }
    }
    writeln!(console.out)?;
    let drawing = qr_code(&links[drawn].to_string(), console.terminal)?;
    write!(console.out, "{drawing}")?;
    writeln!(console.out)?;
    writeln!(
        console.out,
        "Valable {} minutes, une seule fois.",
        CODE_LIFETIME / 60_000
    )?;
    if open {
        writeln!(console.out, "{CLOSES_THE_LIBRARY}")?;
    }
    Ok(ExitCode::SUCCESS)
}

/// The certificate Leaf serves with, made now if it has never started; `None` when it speaks
/// plain http on the loopback and has none.
fn leafs_fingerprint(config: &Config) -> Result<Option<String>> {
    tls_paths(config)
        .map(|(certificate, key)| {
            own_fingerprint(
                &certificate,
                &key,
                &tls_hosts(&config.tls_hosts, &config.host),
            )
        })
        .transpose()
}

/// The link as a QR code in text, two modules to a character cell, with the two-module
/// margin a reader needs.
///
/// Inverted on a terminal: most are light text on a dark background, and a reader looks for
/// dark modules on light. Not inverted when written elsewhere — a file or a page is read on
/// white.
fn qr_code(link: &str, terminal: bool) -> Result<String> {
    use qrcode::render::unicode::Dense1x2;
    use qrcode::render::Renderer;

    let code = qrcode::QrCode::new(link.as_bytes()).context("le lien ne tient pas en QR code")?;
    let colors = code.to_colors();
    let mut renderer = Renderer::<Dense1x2>::new(&colors, code.width(), 2);
    if terminal {
        renderer
            .dark_color(Dense1x2::Light)
            .light_color(Dense1x2::Dark);
    }
    Ok(renderer.build() + "\n")
}

fn list(config: &Config, settings: &Settings, console: &mut Console) -> Result<ExitCode> {
    let now = now();
    let (devices, pending) = devices(config)?.list(now)?;
    let keys = Keys::parse(settings.get("LEAF_KEYS").as_deref())?;

    let never = || "—".to_string();
    let mut rows: Vec<[String; 4]> = vec![[
        "NOM".into(),
        "RELIÉ LE".into(),
        "PAR".into(),
        "VU LE".into(),
    ]];
    // Every text read from the index through `printable`: names come from the devices.
    rows.extend(devices.iter().map(|d| {
        [
            printable(&d.name),
            local_time(d.linked_at),
            printable(&d.linked_by),
            d.seen_at.map(local_time).unwrap_or_else(never),
        ]
    }));
    rows.extend(keys.iter().map(|k| {
        [
            printable(&k.name),
            never(),
            "ancienne clé de LEAF_KEYS".into(),
            never(),
        ]
    }));
    write!(console.out, "{}", table(&rows))?;

    if !pending.is_empty() {
        writeln!(console.out)?;
        writeln!(console.out, "CODES EN ATTENTE")?;
        for code in &pending {
            // Rounded up: « expire dans 0 min » for a code that still has fifty seconds
            // would send someone to make another one for nothing.
            let minutes = (code.expires_at - now + 59_999) / 60_000;
            writeln!(
                console.out,
                "{}  expire dans {minutes} min",
                printable(&code.created_by)
            )?;
        }
    }
    Ok(ExitCode::SUCCESS)
}

/// Columns padded to their widest cell, in characters: « RELIÉ » is six bytes and five
/// letters.
fn table(rows: &[[String; 4]]) -> String {
    let mut widths = [0usize; 4];
    for row in rows {
        for (width, cell) in widths.iter_mut().zip(row) {
            *width = (*width).max(cell.chars().count());
        }
    }
    let mut out = String::new();
    for row in rows {
        let mut line = String::new();
        for (i, (cell, width)) in row.iter().zip(widths).enumerate() {
            line.push_str(cell);
            if i + 1 < row.len() {
                line.push_str(&" ".repeat(width - cell.chars().count() + 2));
            }
        }
        out.push_str(line.trim_end());
        out.push('\n');
    }
    out
}

/// `JJ/MM/AAAA HH:MM` in the machine's own time zone, for a time in milliseconds.
#[cfg(unix)]
fn local_time(millis: i64) -> String {
    let seconds = (millis / 1000) as libc::time_t;
    // SAFETY: localtime_r writes into the `tm` it is handed and reads only `seconds`.
    let mut tm: libc::tm = unsafe { std::mem::zeroed() };
    if unsafe { libc::localtime_r(&seconds, &mut tm) }.is_null() {
        return "—".into();
    }
    format!(
        "{:02}/{:02}/{:04} {:02}:{:02}",
        tm.tm_mday,
        tm.tm_mon + 1,
        tm.tm_year + 1900,
        tm.tm_hour,
        tm.tm_min
    )
}

#[cfg(not(unix))]
fn local_time(millis: i64) -> String {
    format!("{millis}")
}

fn revoke(config: &Config, name_or_code: &str, console: &mut Console) -> Result<ExitCode> {
    match devices(config)?.revoke(name_or_code)? {
        Ok(removed) => {
            writeln!(console.out, "« {} » est retiré.", printable(&removed))?;
            Ok(ExitCode::SUCCESS)
        }
        Err(_) => {
            writeln!(
                console.err,
                "Aucun appareil ni code en attente ne répond à « {} ».",
                printable(name_or_code)
            )?;
            Ok(ExitCode::FAILURE)
        }
    }
}

/// Links a device named `name` and writes its key, alone, to `console.out`.
///
/// If the key cannot be written — the installer's pipe closed, a full disk under a
/// redirection — the device is removed again before the error is returned: a device whose
/// key nobody received is a door nobody can use and nobody thinks to close.
pub fn enroll(config: &Config, name: &str, console: &mut Console) -> Result<ExitCode> {
    let devices = devices(config)?;
    match devices.enroll(name, now())? {
        Ok(linked) => {
            let written =
                writeln!(console.out, "{}", linked.key).and_then(|()| console.out.flush());
            if let Err(e) = written {
                devices.unlink(&linked.device.id)?;
                return Err(e).context("écriture de la clé — l'appareil n'a pas été gardé");
            }
            Ok(ExitCode::SUCCESS)
        }
        Err(refusal) => {
            writeln!(console.err, "{}", refused(&refusal))?;
            Ok(ExitCode::FAILURE)
        }
    }
}

fn refused(refusal: &Refusal) -> String {
    match refusal {
        Refusal::NameTaken(holder) => {
            format!("Le nom « {} » est déjà pris.", printable(holder))
        }
        Refusal::NameEmpty => "Un appareil a besoin d'un nom.".into(),
        Refusal::NameTooLong => "Un nom d'appareil tient en 40 caractères au plus.".into(),
        Refusal::NameInvalid => {
            "Un nom d'appareil ne peut pas contenir de caractère de contrôle.".into()
        }
        Refusal::CodeNotValid | Refusal::NoSuchDevice => format!("{refusal:?}"),
    }
}
