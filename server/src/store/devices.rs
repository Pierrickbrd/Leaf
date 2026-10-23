//! Linked devices: a one-time code typed (or clicked) on the new device, exchanged once for
//! a key of its own, which the server keeps only as a SHA-256.
//!
//! One key per device rather than one shared secret in `LEAF_KEYS`: a lost phone is
//! removed by its name, at the next request, without editing a file as root, restarting,
//! or pasting a new secret into every other device.
//!
//! Every time here is in milliseconds since the epoch, like `progress.updated_at` and
//! `profile.created_at`.

use std::sync::Arc;

use anyhow::{anyhow, bail, Result};
use base64::engine::general_purpose::URL_SAFE_NO_PAD;
use base64::Engine;
use rusqlite::{params, Row};
use sha2::{Digest, Sha256};

use super::db::{Cx, Db};
use super::schema::DEFAULT_PROFILE;

/// How long a code can be typed: long enough to fetch the other device and find the
/// window, short enough that one read over a shoulder is worth little by the time it is
/// used.
pub const CODE_LIFETIME: i64 = 10 * 60 * 1000;

/// A device seen less than a minute ago is not written again. `recognise` runs on every
/// request; without this, reading a volume would be one write per page.
const SEEN_REFRESH: i64 = 60 * 1000;

/// Without the characters that read as one another on a screen or a sheet of paper: 0 and
/// O, 1, I and L. Thirty-one left, eight drawn: 31⁸ ≈ 8.5 × 10¹¹, for a code that lives ten
/// minutes and is used once.
const ALPHABET: &[u8; 31] = b"ABCDEFGHJKMNPQRSTUVWXYZ23456789";
const CODE_LENGTH: usize = 8;

/// The longest name a device may carry, in characters and not bytes — « é » is two bytes
/// and one letter, and a French name would otherwise be held to twenty.
const NAME_LIMIT: usize = 40;

/// What `linked_by` says when the code came from `leaf-server device add` or `enroll`.
const TERMINAL: &str = "terminal";

pub struct Devices {
    db: Arc<Db>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Device {
    pub id: String,
    pub name: String,
    pub linked_at: i64,
    /// Who made the code, as it was called at the time: « terminal », a device's name, or
    /// « ancienne clé NOM ». Text rather than a reference, so that revoking the device that
    /// linked this one does not erase the record of how it came in.
    pub linked_by: String,
    pub seen_at: Option<i64>,
    pub profile_id: String,
    /// Whether the device holds `profile_id` as its own: Leaf opens on it without asking
    /// who reads. Whoever reads cannot set it on a restricted profile (`switch` refuses), but
    /// it can be true of one: the principal may `place` a device on it, and restricting a
    /// reader keeps the devices that had retained it.
    pub kept: bool,
    /// The device must ask who reads before anything else: its profile was deleted under it.
    pub choosing: bool,
    /// The profile it sat on was restricted when it was deleted, so leaving the seat it was
    /// parked on asks for the code like leaving the restricted profile would have.
    pub held: bool,
}

#[derive(Clone)]
pub struct Issued {
    pub id: String,
    /// `ABCD-EFGH`. The only time it exists in clear: what is kept is its SHA-256.
    pub code: String,
    pub expires_at: i64,
}

#[derive(Debug, Clone)]
pub struct Pending {
    pub id: String,
    pub created_by: String,
    pub expires_at: i64,
}

/// A device that has just been linked, and its key — in clear this once, and never again.
pub struct Linked {
    pub device: Device,
    pub key: String,
}

/// Written by hand so that the secret is the one field never printed. A derived `Debug`
/// put the code in clear in any `{:?}` — a `tracing` field, an `expect` message, a test
/// failure — and a log is the place a secret outlives the minutes it was meant to live.
impl std::fmt::Debug for Issued {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Issued")
            .field("id", &self.id)
            .field("code", &"…")
            .field("expires_at", &self.expires_at)
            .finish()
    }
}

/// The same for the key, which unlike the code does not expire.
impl std::fmt::Debug for Linked {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Linked")
            .field("device", &self.device)
            .field("key", &"…")
            .finish()
    }
}

/// Who asked for a code.
#[derive(Debug, Clone, Copy)]
pub enum Creator<'a> {
    Terminal,
    /// A linked device, by id.
    Device(&'a str),
    /// A key from `LEAF_KEYS`, by its name there.
    OldKey(&'a str),
}

/// The answers that are the caller's to fix, as opposed to the errors that are the
/// server's. Kept apart so the route can answer each with its own sentence.
#[derive(Debug, PartialEq, Eq)]
pub enum Refusal {
    /// Unknown, expired, or already served: the three are one answer, so that the response
    /// does not tell a guesser which codes exist.
    CodeNotValid,
    /// The name as the device that holds it spells it.
    NameTaken(String),
    NameEmpty,
    NameTooLong,
    /// A control character somewhere in it: a name is printed in the operator's terminal by
    /// `device list`, and an escape sequence there would rewrite that terminal.
    NameInvalid,
    NoSuchDevice,
}

/// The form a name is compared in: case folded and inner spaces collapsed, so that
/// « PC du salon » and « pc  du SALON » are the same name.
pub fn name_key(name: &str) -> String {
    name.split_whitespace()
        .collect::<Vec<_>>()
        .join(" ")
        .to_lowercase()
}

/// A code as typed, read whatever its case, dashes and spaces, back to `ABCD-EFGH` — or
/// `None` if it cannot be a code at all.
pub fn normalise_code(typed: &str) -> Option<String> {
    let letters: Vec<u8> = typed
        .chars()
        .filter(|c| !c.is_whitespace() && *c != '-')
        .map(|c| c.to_ascii_uppercase())
        .map(|c| u8::try_from(c).ok().filter(|b| ALPHABET.contains(b)))
        .collect::<Option<_>>()?;
    if letters.len() != CODE_LENGTH {
        return None;
    }
    let (head, tail) = letters.split_at(CODE_LENGTH / 2);
    Some(format!(
        "{}-{}",
        String::from_utf8_lossy(head),
        String::from_utf8_lossy(tail)
    ))
}

impl Devices {
    pub fn new(db: Arc<Db>) -> Self {
        Devices { db }
    }

    /// A new code, valid for [`CODE_LIFETIME`] from `now`.
    pub fn issue(&self, by: Creator, now: i64) -> Result<Issued> {
        self.db.write(|cx| {
            // Codes nobody typed are of no use to anyone once expired. Swept here, where
            // new ones are made, so the table holds what is live and not every code ever
            // printed. Served ones stay: they say to their creator who used them.
            cx.execute(
                "DELETE FROM link_code WHERE served_device IS NULL AND expires_at <= ?1",
                [now],
            )?;

            let (created_by, creator_device) = match by {
                Creator::Terminal => (TERMINAL.to_string(), None),
                Creator::OldKey(name) => (format!("ancienne clé {name}"), None),
                Creator::Device(id) => {
                    let name: Option<String> = cx.query_one(
                        "SELECT name FROM device WHERE id = ?1",
                        [id],
                        |r| r.get(0),
                    )?;
                    let name = name.ok_or_else(|| anyhow!("no device {id} to issue a code"))?;
                    (name, Some(id.to_string()))
                }
            };

            let code = loop {
                let code = draw_code()?;
                let taken = cx
                    .query_one(
                        "SELECT 1 FROM link_code WHERE code_digest = ?1",
                        [&digest(&code)[..]],
                        |r| r.get::<_, i64>(0),
                    )?
                    .is_some();
                if !taken {
                    break code;
                }
            };

            let id = new_id()?;
            let expires_at = now + CODE_LIFETIME;
            cx.execute(
                "INSERT INTO link_code (id, code_digest, created_at, expires_at, created_by, creator_device)
                 VALUES (?1, ?2, ?3, ?4, ?5, ?6)",
                params![id, &digest(&code)[..], now, expires_at, created_by, creator_device],
            )?;
            Ok(Issued {
                id,
                code,
                expires_at,
            })
        })
    }

    /// Links a device against a code, under the name it chose.
    pub fn exchange(&self, code: &str, name: &str, now: i64) -> Result<Result<Linked, Refusal>> {
        let Some(code) = normalise_code(code) else {
            return Ok(Err(Refusal::CodeNotValid));
        };
        let code_digest = digest(&code);
        self.db.write(|cx| {
            // The code is looked at before the name, so that someone without a valid code
            // cannot use this route to learn which names the library's devices go by.
            let live: Option<(String, String)> = cx.query_one(
                "SELECT id, created_by FROM link_code
                 WHERE code_digest = ?1 AND served_device IS NULL AND expires_at > ?2",
                params![&code_digest[..], now],
                |r| Ok((r.get(0)?, r.get(1)?)),
            )?;
            let Some((code_id, created_by)) = live else {
                return Ok(Err(Refusal::CodeNotValid));
            };
            // Before anything is written, so a name already taken leaves the code as valid
            // as it was: the person picks another name and types the same code.
            let name = match acceptable_name(cx, name, None)? {
                Ok(name) => name,
                Err(refusal) => return Ok(Err(refusal)),
            };

            let linked = insert_device(cx, &name, &created_by, now)?;
            // The statement that serves the code is also the one that checks it is still
            // unserved, in the same transaction as the device. So "used once" holds by the
            // database's own serialisation of writers — the in-process lock covers this
            // server, but `leaf-server device` is another process with its own connection.
            let served = cx.execute(
                "UPDATE link_code SET served_device = ?1
                 WHERE id = ?2 AND served_device IS NULL AND expires_at > ?3",
                params![linked.device.id, code_id, now],
            )?;
            if served != 1 {
                cx.execute("DELETE FROM device WHERE id = ?1", [&linked.device.id])?;
                return Ok(Err(Refusal::CodeNotValid));
            }
            close_for_good(cx, now)?;
            Ok(Ok(linked))
        })
    }

    /// Links a device with no code, from the server's own terminal — for a client that can
    /// be handed a key directly.
    pub fn enroll(&self, name: &str, now: i64) -> Result<Result<Linked, Refusal>> {
        self.db.write(|cx| {
            let name = match acceptable_name(cx, name, None)? {
                Ok(name) => name,
                Err(refusal) => return Ok(Err(refusal)),
            };
            let linked = insert_device(cx, &name, TERMINAL, now)?;
            close_for_good(cx, now)?;
            Ok(Ok(linked))
        })
    }

    /// The device a key belongs to, if it still belongs to one.
    ///
    /// Asked of the database on every request, so a revocation made by another process
    /// holds from the next one. The lookup is by the key's SHA-256 on an indexed column,
    /// compared by SQLite and not in constant time: the digest of 256 random bits cannot be
    /// guessed a byte at a time, because nobody can choose a key whose digest starts with
    /// the bytes they want.
    pub fn recognise(&self, key: &str, now: i64) -> Result<Option<Device>> {
        let key_digest = digest(key);
        let found = self.db.read(|cx| {
            cx.query_one(
                &format!("SELECT {DEVICE_COLUMNS} FROM device WHERE key_digest = ?1"),
                [&key_digest[..]],
                device_row,
            )
        })?;
        let Some(mut device) = found else {
            return Ok(None);
        };
        if device
            .seen_at
            .is_some_and(|seen| now - seen <= SEEN_REFRESH)
        {
            return Ok(Some(device));
        }
        let still_there = self.db.write(|cx| {
            cx.execute(
                "UPDATE device SET seen_at = ?1 WHERE id = ?2",
                params![now, device.id],
            )
        })?;
        // Revoked between the read and the write: the revocation wins.
        if still_there == 0 {
            return Ok(None);
        }
        device.seen_at = Some(now);
        Ok(Some(device))
    }

    /// Whether a device was ever linked — the moment a library stops being open, for good.
    /// Not whether one is linked now: revoking the last device must not open the library
    /// again to whoever still holds a key that was just taken away.
    pub fn closed(&self) -> Result<bool> {
        self.db.read(|cx| {
            cx.query_one("SELECT EXISTS (SELECT 1 FROM closed)", [], |r| r.get(0))
                .map(|e| e.unwrap_or(false))
        })
    }

    /// The linked devices, oldest first, and the codes still waiting at `now`.
    pub fn list(&self, now: i64) -> Result<(Vec<Device>, Vec<Pending>)> {
        self.db.read(|cx| {
            let devices = cx.query(
                &format!("SELECT {DEVICE_COLUMNS} FROM device ORDER BY linked_at, name_key"),
                [],
                device_row,
            )?;
            let pending = cx.query(
                "SELECT id, created_by, expires_at FROM link_code
                 WHERE served_device IS NULL AND expires_at > ?1
                 ORDER BY expires_at",
                [now],
                |r| {
                    Ok(Pending {
                        id: r.get(0)?,
                        created_by: r.get(1)?,
                        expires_at: r.get(2)?,
                    })
                },
            )?;
            Ok((devices, pending))
        })
    }

    /// Removes a device by its name, or cancels a waiting code by the code itself. Answers
    /// with what was removed, spelled as it was stored.
    pub fn revoke(&self, name_or_code: &str) -> Result<Result<String, Refusal>> {
        self.db.write(|cx| {
            let device: Option<(String, String)> = cx.query_one(
                "SELECT id, name FROM device WHERE name_key = ?1",
                [name_key(name_or_code)],
                |r| Ok((r.get(0)?, r.get(1)?)),
            )?;
            if let Some((id, name)) = device {
                remove_device(cx, &id)?;
                return Ok(Ok(name));
            }
            if let Some(code) = normalise_code(name_or_code) {
                let cancelled = cx.execute(
                    "DELETE FROM link_code WHERE code_digest = ?1 AND served_device IS NULL",
                    [&digest(&code)[..]],
                )?;
                if cancelled > 0 {
                    return Ok(Ok(code));
                }
            }
            Ok(Err(Refusal::NoSuchDevice))
        })
    }

    /// Removes a device by its id: the way a device leaves by itself. By id and not by name,
    /// since a name can change, and be taken by another, between the request and the delete.
    pub fn unlink(&self, device_id: &str) -> Result<bool> {
        self.db.write(|cx| {
            let exists = cx
                .query_one("SELECT 1 FROM device WHERE id = ?1", [device_id], |r| {
                    r.get::<_, i64>(0)
                })?
                .is_some();
            if exists {
                remove_device(cx, device_id)?;
            }
            Ok(exists)
        })
    }

    /// Gives a device another name, under the same rules as the one it linked with. Its own
    /// name in another case is not "taken".
    pub fn rename(&self, device_id: &str, name: &str) -> Result<Result<Device, Refusal>> {
        self.db.write(|cx| {
            let exists = cx
                .query_one("SELECT 1 FROM device WHERE id = ?1", [device_id], |r| {
                    r.get::<_, i64>(0)
                })?
                .is_some();
            if !exists {
                return Ok(Err(Refusal::NoSuchDevice));
            }
            let name = match acceptable_name(cx, name, Some(device_id))? {
                Ok(name) => name,
                Err(refusal) => return Ok(Err(refusal)),
            };
            cx.execute(
                "UPDATE device SET name = ?1, name_key = ?2 WHERE id = ?3",
                params![name, name_key(&name), device_id],
            )?;
            let device = cx.query_one(
                &format!("SELECT {DEVICE_COLUMNS} FROM device WHERE id = ?1"),
                [device_id],
                device_row,
            )?;
            match device {
                Some(device) => Ok(Ok(device)),
                None => bail!("device {device_id} vanished inside its own transaction"),
            }
        })
    }

    /// Cancels a waiting code, if the device asking is the one that made it.
    pub fn cancel(&self, code_id: &str, creator_device: &str) -> Result<bool> {
        self.db.write(|cx| {
            cx.execute(
                "DELETE FROM link_code
                 WHERE id = ?1 AND creator_device = ?2 AND served_device IS NULL",
                [code_id, creator_device],
            )
            .map(|n| n > 0)
        })
    }

    /// What became of a code this device made: `None` if it is not one of its codes, or if
    /// nobody used it in time; `Some(None)` while it waits; `Some(Some(name))` once the
    /// device called `name` has linked with it.
    pub fn served(
        &self,
        code_id: &str,
        creator_device: &str,
        now: i64,
    ) -> Result<Option<Option<String>>> {
        let found: Option<(i64, Option<String>)> = self.db.read(|cx| {
            cx.query_one(
                "SELECT link_code.expires_at, device.name FROM link_code
                 LEFT JOIN device ON device.id = link_code.served_device
                 WHERE link_code.id = ?1 AND link_code.creator_device = ?2",
                [code_id, creator_device],
                |r| Ok((r.get(0)?, r.get(1)?)),
            )
        })?;
        Ok(match found {
            None => None,
            // Served stays served after the ten minutes: a window that asks a moment late
            // still learns the device came in.
            Some((_, Some(name))) => Some(Some(name)),
            Some((expires_at, None)) if expires_at > now => Some(None),
            Some((_, None)) => None,
        })
    }
}

fn remove_device(cx: &Cx<'_>, id: &str) -> Result<()> {
    // The code this device was linked with goes first. `served_device` is
    // `ON DELETE SET NULL`, so deleting the device alone would put its code back
    // to waiting — valid again for whatever was left of its ten minutes.
    cx.execute("DELETE FROM link_code WHERE served_device = ?1", [id])?;
    // Its own waiting codes go with it, by `creator_device`'s cascade; the
    // devices it linked stay, since their keys are their own.
    cx.execute("DELETE FROM device WHERE id = ?1", [id])?;
    Ok(())
}

const DEVICE_COLUMNS: &str =
    "id, name, linked_at, linked_by, seen_at, profile_id, kept, choosing, held";

fn device_row(r: &Row<'_>) -> rusqlite::Result<Device> {
    Ok(Device {
        id: r.get(0)?,
        name: r.get(1)?,
        linked_at: r.get(2)?,
        linked_by: r.get(3)?,
        seen_at: r.get(4)?,
        profile_id: r.get(5)?,
        kept: r.get(6)?,
        choosing: r.get(7)?,
        held: r.get(8)?,
    })
}

/// The name as it will be stored — trimmed at the edges — or why it cannot be. `except` is
/// the device being renamed, whose own name does not count against it.
fn acceptable_name(
    cx: &Cx<'_>,
    name: &str,
    except: Option<&str>,
) -> Result<Result<String, Refusal>> {
    let trimmed = name.trim();
    if trimmed.is_empty() {
        return Ok(Err(Refusal::NameEmpty));
    }
    if trimmed.chars().count() > NAME_LIMIT {
        return Ok(Err(Refusal::NameTooLong));
    }
    // C0, DEL and C1 alike — ESC and CSI (U+009B) both open a sequence a terminal obeys.
    if trimmed.chars().any(char::is_control) {
        return Ok(Err(Refusal::NameInvalid));
    }
    let holder: Option<String> = cx.query_one(
        "SELECT name FROM device WHERE name_key = ?1 AND id IS NOT ?2",
        params![name_key(trimmed), except],
        |r| r.get(0),
    )?;
    Ok(match holder {
        Some(holder) => Err(Refusal::NameTaken(holder)),
        None => Ok(trimmed.to_string()),
    })
}

/// In the transaction that links the device, so a library is never seen with a device and
/// still open. Nothing removes the row — not `revoke`, not `unlink`: a door that some path
/// can reopen is a door someone finds a way to reopen.
fn close_for_good(cx: &Cx<'_>, now: i64) -> Result<()> {
    cx.execute(
        "INSERT OR IGNORE INTO closed (id, closed_at) VALUES (1, ?1)",
        params![now],
    )?;
    Ok(())
}

fn insert_device(cx: &Cx<'_>, name: &str, linked_by: &str, now: i64) -> Result<Linked> {
    let key = URL_SAFE_NO_PAD.encode(random::<32>()?);
    let id = new_id()?;
    cx.execute(
        "INSERT INTO device (id, name, name_key, key_digest, linked_at, linked_by, seen_at, profile_id)
         VALUES (?1, ?2, ?3, ?4, ?5, ?6, NULL, ?7)",
        params![id, name, name_key(name), &digest(&key)[..], now, linked_by, DEFAULT_PROFILE],
    )?;
    Ok(Linked {
        device: Device {
            id,
            name: name.to_string(),
            linked_at: now,
            linked_by: linked_by.to_string(),
            seen_at: None,
            profile_id: DEFAULT_PROFILE.to_string(),
            kept: false,
            choosing: false,
            held: false,
        },
        key,
    })
}

/// Eight characters of [`ALPHABET`], drawn without bias: a byte at or above 248 (8 × 31)
/// is thrown away rather than folded, or the first eight letters would come up more often.
fn draw_code() -> Result<String> {
    let mut letters = String::with_capacity(CODE_LENGTH);
    while letters.len() < CODE_LENGTH {
        for byte in random::<16>()? {
            let byte = usize::from(byte);
            if byte < ALPHABET.len() * 8 && letters.len() < CODE_LENGTH {
                letters.push(char::from(ALPHABET[byte % ALPHABET.len()]));
            }
        }
    }
    normalise_code(&letters).ok_or_else(|| anyhow!("drew a code outside the alphabet"))
}

/// Sixteen hexadecimal characters, the shape every other id in the index has. Random rather
/// than derived: a device has nothing stable to derive one from.
fn new_id() -> Result<String> {
    Ok(random::<8>()?.iter().map(|b| format!("{b:02x}")).collect())
}

fn random<const N: usize>() -> Result<[u8; N]> {
    let mut bytes = [0u8; N];
    getrandom::fill(&mut bytes)
        .map_err(|e| anyhow!("drawing from the system's randomness: {e}"))?;
    Ok(bytes)
}

fn digest(value: &str) -> [u8; 32] {
    Sha256::digest(value.as_bytes()).into()
}
