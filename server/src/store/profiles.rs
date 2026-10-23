//! Profiles: who reads, what each may do to the library, and the one code that guards the
//! restricted ones.
//!
//! The kind of a profile is **derived**, never stored: the row whose id is
//! [`DEFAULT_PROFILE`] is the principal, a row with `restricted = 1` is restricted, any other
//! is a reader. A `kind` column would be a second statement of what those two already say,
//! and the day they disagreed there would be a principal that could be deleted.
//!
//! The code lives in `household`, once, because a household has one: readers and restricted
//! profiles do not have their own.

use std::sync::Arc;

use anyhow::{anyhow, Context, Result};
use argon2::password_hash::{PasswordHash, PasswordHasher, PasswordVerifier, SaltString};
use argon2::Argon2;
use rusqlite::Row;

use super::db::{Cx, Db};
use super::devices::name_key;
use super::schema::DEFAULT_PROFILE;
use super::text::search_key;

/// The sixteen colours of the palette, stored without accents and in the palette's order. A
/// profile owns one, and the order is the one the picker draws.
pub const COLORS: [&str; 16] = [
    "emeraude",
    "sapin",
    "sauge",
    "olive",
    "ocre",
    "ambre",
    "corail",
    "framboise",
    "rose",
    "prune",
    "violet",
    "indigo",
    "bleu",
    "azur",
    "ardoise",
    "terre",
];

/// The ages a restricted profile may be set to. Under three nothing in a library is rated,
/// and eighteen is the rating that hides everything a restriction exists to hide.
const AGES: std::ops::RangeInclusive<u8> = 3..=17;

/// In characters and not bytes, as for a device name: « é€ » is five bytes and two characters,
/// and a code typed on a French keyboard would otherwise be held to a shorter length than
/// the one the screen says.
const CODE_LENGTH: std::ops::RangeInclusive<usize> = 4..=8;

pub struct Profiles {
    db: Arc<Db>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Kind {
    Principal,
    Reader,
    Restricted,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct Rights {
    pub import: bool,
    pub edit: bool,
    pub delete: bool,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Profile {
    pub id: String,
    pub name: String,
    pub color: String,
    pub kind: Kind,
    /// Only a restricted profile has one.
    pub max_age: Option<u8>,
    /// All true for the principal, all false for a restricted profile, whatever the
    /// columns say: the table of rights is the reference, not what was once stored.
    pub rights: Rights,
}

/// What the code is for, beyond leaving a restricted profile, which it always is.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CodeScope {
    Restricted,
    /// Also to go back to the principal, and to add a reader from another profile.
    Everything,
}

#[derive(Debug, Clone)]
pub struct NewProfile {
    pub name: String,
    pub color: String,
    pub restricted: bool,
    pub max_age: Option<u8>,
    pub rights: Rights,
}

#[derive(Debug, Clone, Default)]
pub struct ProfilePatch {
    pub name: Option<String>,
    pub color: Option<String>,
    pub restricted: Option<bool>,
    pub max_age: Option<u8>,
    pub rights: Option<Rights>,
}

/// The answers that are the caller's to fix, as opposed to the errors that are the server's.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ProfileRefusal {
    NameTaken,
    ColorTaken,
    UnknownColor,
    BadAge,
    NotFound,
    PrincipalIsFixed,
    /// A restricted profile cannot exist without the code that lets it out.
    CodeRequired,
    /// The code cannot go while a profile is restricted.
    CodeStillNeeded,
    BadCode,
    /// Empty, or only whitespace.
    BadName,
}

/// The answers to a recovery that are the caller's to fix.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RecoveryRefusal {
    /// Not the recovery code, or none was ever drawn: the two are not told apart.
    WrongRecovery,
    BadCode,
}

/// Where a device sits: the profile it reads as, and the three flags that say how it got
/// there and what leaving it costs.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Seat {
    pub profile_id: String,
    pub kept: bool,
    pub choosing: bool,
    pub held: bool,
}

/// The answers to a change of profile that are the caller's to fix.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SwitchRefusal {
    NoSuchProfile,
    CodeNeeded,
    WrongCode,
    /// A restricted profile cannot be kept on a device by whoever reads there.
    NotYours,
    /// Only from [`Profiles::place`], which is handed a device by id from over the wire.
    NoSuchDevice,
}

const COLUMNS: &str = "id, name, color, restricted, max_age, can_import, can_edit, can_delete";

/// The same columns under a table alias, for a query that joins the profile to something
/// else — the device that recognises a key reads its profile in the same statement.
pub(crate) fn columns_of(alias: &str) -> String {
    super::devices::aliased(COLUMNS, alias)
}

fn profile_of(r: &Row<'_>) -> rusqlite::Result<Profile> {
    profile_at(r, 0)
}

/// A profile read from the row's columns starting at `first`, in the order of [`COLUMNS`].
pub(crate) fn profile_at(r: &Row<'_>, first: usize) -> rusqlite::Result<Profile> {
    let id: String = r.get(first)?;
    let restricted: bool = r.get(first + 3)?;
    let kind = if id == DEFAULT_PROFILE {
        Kind::Principal
    } else if restricted {
        Kind::Restricted
    } else {
        Kind::Reader
    };
    let rights = match kind {
        Kind::Principal => Rights {
            import: true,
            edit: true,
            delete: true,
        },
        Kind::Restricted => Rights::default(),
        Kind::Reader => Rights {
            import: r.get(first + 5)?,
            edit: r.get(first + 6)?,
            delete: r.get(first + 7)?,
        },
    };
    Ok(Profile {
        id,
        name: r.get(first + 1)?,
        color: r.get(first + 2)?,
        kind,
        max_age: if kind == Kind::Restricted {
            r.get::<_, Option<u8>>(first + 4)?
        } else {
            None
        },
        rights,
    })
}

fn find(cx: &Cx<'_>, id: &str) -> Result<Option<Profile>> {
    cx.query_one(
        &format!("SELECT {COLUMNS} FROM profile WHERE id = ?1"),
        [id],
        profile_of,
    )
}

/// The form a name is compared in. Accents are folded as well as case, so « Inès » and
/// « INES » are one name: two profiles a tablet's keyboard could not tell apart would be a
/// "Who reads?" screen where the wrong child is one tap away. A name with nothing to fold
/// into (only punctuation) falls back to the plain device form rather than to the empty
/// string, which every such name would share.
fn key_of(name: &str) -> String {
    let folded = search_key(name);
    if folded.is_empty() {
        name_key(name)
    } else {
        folded
    }
}

/// `None` for a name with nothing in it: a refusal for the caller, not an error, or a route
/// would answer a blank form field with a 500.
fn clean_name(name: &str) -> Option<String> {
    let name = name.split_whitespace().collect::<Vec<_>>().join(" ");
    (!name.is_empty()).then_some(name)
}

fn has_code(cx: &Cx<'_>) -> Result<bool> {
    Ok(cx
        .query_one(
            "SELECT code_phc IS NOT NULL FROM household WHERE id = 1",
            [],
            |r| r.get::<_, bool>(0),
        )?
        .unwrap_or(false))
}

fn new_id() -> Result<String> {
    let mut bytes = [0u8; 8];
    getrandom::fill(&mut bytes)
        .map_err(|e| anyhow!("drawing from the system's randomness: {e}"))?;
    Ok(bytes.iter().map(|b| format!("{b:02x}")).collect())
}

impl Profiles {
    pub fn new(db: Arc<Db>) -> Self {
        Profiles { db }
    }

    /// The principal first, then in the order they were made. One query: the screen that
    /// asks "who reads?" is opened on every launch.
    pub fn list(&self) -> Result<Vec<Profile>> {
        self.db.read(|cx| {
            cx.query(
                &format!(
                    "SELECT {COLUMNS} FROM profile
                     ORDER BY id <> ?1, created_at, rowid"
                ),
                [DEFAULT_PROFILE],
                profile_of,
            )
        })
    }

    pub fn get(&self, id: &str) -> Result<Option<Profile>> {
        self.db.read(|cx| find(cx, id))
    }

    pub fn create(&self, new: NewProfile, now: i64) -> Result<Result<Profile, ProfileRefusal>> {
        self.create_with_code(new, None, now)
    }

    /// [`Profiles::create`], making the code in the same write when there is none yet — « Le
    /// code, à créer », the window that adds the first restricted profile. One write, so a
    /// profile refused (a name taken, a colour) leaves no code behind that nobody asked for,
    /// and a code is never stored for a profile that does not exist.
    ///
    /// The code is hashed before the write, never inside it, like [`Profiles::set_code`]. A
    /// code that is already there wins: the one given is ignored.
    pub fn create_with_code(
        &self,
        new: NewProfile,
        code: Option<&str>,
        now: i64,
    ) -> Result<Result<Profile, ProfileRefusal>> {
        let Some(name) = clean_name(&new.name) else {
            return Ok(Err(ProfileRefusal::BadName));
        };
        let phc = match code {
            Some(code) => match digest_of(code)? {
                Some(phc) => Some(phc),
                None => return Ok(Err(ProfileRefusal::BadCode)),
            },
            None => None,
        };
        let id = new_id()?;
        self.db.write(|cx| {
            if !COLORS.contains(&new.color.as_str()) {
                return Ok(Err(ProfileRefusal::UnknownColor));
            }
            let age = match (new.restricted, new.max_age) {
                (true, Some(age)) if AGES.contains(&age) => Some(age),
                (true, _) => return Ok(Err(ProfileRefusal::BadAge)),
                (false, Some(age)) if !AGES.contains(&age) => {
                    return Ok(Err(ProfileRefusal::BadAge))
                }
                (false, _) => None,
            };
            let code_to_make = match (new.restricted, &phc) {
                (true, phc) if !has_code(cx)? => match phc {
                    Some(phc) => Some(phc),
                    None => return Ok(Err(ProfileRefusal::CodeRequired)),
                },
                _ => None,
            };
            if name_is_taken(cx, &name, None)? {
                return Ok(Err(ProfileRefusal::NameTaken));
            }
            if color_is_taken(cx, &new.color, None)? {
                return Ok(Err(ProfileRefusal::ColorTaken));
            }
            // Last, once nothing can refuse: the transaction rolls back on an error, but a
            // refusal is an `Ok`, and a code written before one would stay.
            if let Some(phc) = code_to_make {
                cx.execute(
                    "UPDATE household SET code_phc = ?1, code_scope = 'restricted' WHERE id = 1",
                    [phc.as_str()],
                )?;
            }
            // A restricted profile is given no rights whatever was asked: the form may have
            // ticked boxes before the switch was thrown.
            let rights = if new.restricted {
                Rights::default()
            } else {
                new.rights
            };
            cx.execute(
                "INSERT INTO profile
                   (id, name, name_key, restricted, max_age, created_at,
                    color, can_import, can_edit, can_delete)
                 VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)",
                rusqlite::params![
                    id,
                    name,
                    key_of(&name),
                    new.restricted,
                    age,
                    now,
                    new.color,
                    rights.import,
                    rights.edit,
                    rights.delete
                ],
            )?;
            Ok(Ok(find(cx, &id)?.context("the profile just inserted")?))
        })
    }

    pub fn update(&self, id: &str, patch: ProfilePatch) -> Result<Result<Profile, ProfileRefusal>> {
        let name = match patch.name.as_deref().map(clean_name) {
            Some(None) => return Ok(Err(ProfileRefusal::BadName)),
            Some(some) => some,
            None => None,
        };
        self.db.write(|cx| {
            let Some(current) = find(cx, id)? else {
                return Ok(Err(ProfileRefusal::NotFound));
            };
            if current.kind == Kind::Principal && patch.restricted == Some(true) {
                return Ok(Err(ProfileRefusal::PrincipalIsFixed));
            }
            let color = patch.color.clone().unwrap_or_else(|| current.color.clone());
            if !COLORS.contains(&color.as_str()) {
                return Ok(Err(ProfileRefusal::UnknownColor));
            }
            let restricted = patch.restricted.unwrap_or(current.kind == Kind::Restricted);
            let age = match (restricted, patch.max_age.or(current.max_age)) {
                (true, Some(age)) if AGES.contains(&age) => Some(age),
                (true, _) => return Ok(Err(ProfileRefusal::BadAge)),
                (false, _) => {
                    if patch.max_age.is_some_and(|age| !AGES.contains(&age)) {
                        return Ok(Err(ProfileRefusal::BadAge));
                    }
                    None
                }
            };
            if restricted && current.kind != Kind::Restricted && !has_code(cx)? {
                return Ok(Err(ProfileRefusal::CodeRequired));
            }
            let name = name.unwrap_or_else(|| current.name.clone());
            if name_is_taken(cx, &name, Some(id))? {
                return Ok(Err(ProfileRefusal::NameTaken));
            }
            if color_is_taken(cx, &color, Some(id))? {
                return Ok(Err(ProfileRefusal::ColorTaken));
            }
            // Crossing the line either way clears the rights: a restricted profile has none,
            // and a profile released from it comes back with none until someone ticks them.
            let crossing = restricted != (current.kind == Kind::Restricted);
            let rights = if restricted || crossing {
                Rights::default()
            } else {
                patch.rights.unwrap_or(current.rights)
            };
            // The principal's own columns are never read for its rights, but they are not
            // left stale either.
            cx.execute(
                "UPDATE profile SET name = ?2, name_key = ?3, color = ?4, restricted = ?5,
                   max_age = ?6, can_import = ?7, can_edit = ?8, can_delete = ?9
                 WHERE id = ?1",
                rusqlite::params![
                    id,
                    name,
                    key_of(&name),
                    color,
                    restricted,
                    age,
                    rights.import,
                    rights.edit,
                    rights.delete
                ],
            )?;
            Ok(Ok(find(cx, id)?.context("the profile just updated")?))
        })
    }

    /// Removes the profile and, with it, its progress and preferences: the volumes stay.
    pub fn delete(&self, id: &str) -> Result<Result<(), ProfileRefusal>> {
        self.db.write(|cx| {
            if id == DEFAULT_PROFILE {
                return Ok(Err(ProfileRefusal::PrincipalIsFixed));
            }
            let Some(gone) = find(cx, id)? else {
                return Ok(Err(ProfileRefusal::NotFound));
            };
            // `device.profile_id` has no ON DELETE: without this the foreign key refuses the
            // delete. The device is put back on the principal only to hold the key, and in the
            // same statement told to choose again: between two writes it would otherwise be
            // served as the principal, which is the tablet of a child opening its parent's
            // library. A restricted profile's device is also held, so the choice asks the code.
            cx.execute(
                "UPDATE device SET profile_id = ?1, kept = 0, choosing = 1, held = held OR ?3
                 WHERE profile_id = ?2",
                rusqlite::params![DEFAULT_PROFILE, id, gone.kind == Kind::Restricted],
            )?;
            // Explicit although `progress` cascades: the cascade is a property of a table
            // this file does not own, and a rebuilt table that lost it would leave a
            // deleted child's places on the disk.
            cx.execute("DELETE FROM progress WHERE profile_id = ?1", [id])?;
            cx.execute("DELETE FROM profile WHERE id = ?1", [id])?;
            Ok(Ok(()))
        })
    }

    /// What the code is for, or `None` when there is no code.
    pub fn code(&self) -> Result<Option<CodeScope>> {
        self.db.read(|cx| {
            let row: Option<Option<String>> = cx.query_one(
                "SELECT CASE WHEN code_phc IS NULL THEN NULL ELSE code_scope END
                 FROM household WHERE id = 1",
                [],
                |r| r.get(0),
            )?;
            Ok(match row.flatten().as_deref() {
                Some("everything") => Some(CodeScope::Everything),
                Some(_) => Some(CodeScope::Restricted),
                None => None,
            })
        })
    }

    pub fn set_code(&self, code: &str, scope: CodeScope) -> Result<Result<(), ProfileRefusal>> {
        let Some(phc) = digest_of(code)? else {
            return Ok(Err(ProfileRefusal::BadCode));
        };
        let scope = match scope {
            CodeScope::Restricted => "restricted",
            CodeScope::Everything => "everything",
        };
        self.db.write(|cx| {
            cx.execute(
                "UPDATE household SET code_phc = ?1, code_scope = ?2 WHERE id = 1",
                [phc.as_str(), scope],
            )?;
            Ok(Ok(()))
        })
    }

    pub fn clear_code(&self) -> Result<Result<(), ProfileRefusal>> {
        self.db.write(|cx| {
            let restricted: i64 = cx
                .query_one(
                    "SELECT COUNT(*) FROM profile WHERE restricted = 1",
                    [],
                    |r| r.get(0),
                )?
                .unwrap_or(0);
            if restricted > 0 {
                return Ok(Err(ProfileRefusal::CodeStillNeeded));
            }
            // The recovery code goes with the code it recovers: kept, it would be a secret that
            // opens nothing, and the next code would inherit a way back its owner never saw.
            cx.execute(
                "UPDATE household SET code_phc = NULL, code_scope = NULL, recovery_phc = NULL
                 WHERE id = 1",
                [],
            )?;
            Ok(Ok(()))
        })
    }

    /// Revokes the recovery code, leaving the code. For a code replaced from the server's own
    /// terminal: whoever forgot it may have lost it to someone who also holds the recovery code,
    /// and a reset that leaves that one standing resets nothing.
    pub fn forget_recovery(&self) -> Result<()> {
        self.db.write(|cx| {
            cx.execute("UPDATE household SET recovery_phc = NULL WHERE id = 1", [])?;
            Ok(())
        })
    }

    /// Draws a recovery code, keeps its digest in place of any before it, and returns it — the
    /// one time it is ever in the clear. Only for a household that has a code: the recovery
    /// code is the way back to it.
    pub fn new_recovery(&self) -> Result<Result<String, ProfileRefusal>> {
        let phrase = new_recovery_phrase()?;
        let phc = hash_secret(&recovery_form(&phrase))?;
        self.db.write(|cx| {
            let changed = cx.execute(
                "UPDATE household SET recovery_phc = ?1 WHERE id = 1 AND code_phc IS NOT NULL",
                [phc.as_str()],
            )?;
            Ok(if changed == 0 {
                Err(ProfileRefusal::CodeRequired)
            } else {
                Ok(phrase)
            })
        })
    }

    /// Whether `recovery` is the household's recovery code. None set means nothing matches.
    /// Returns the digest it matched, so that the write can insist on it still being the one.
    fn recovery_matches(&self, recovery: &str) -> Result<Option<String>> {
        let phc: Option<Option<String>> = self.db.read(|cx| {
            cx.query_one("SELECT recovery_phc FROM household WHERE id = 1", [], |r| {
                r.get(0)
            })
        })?;
        let Some(phc) = phc.flatten() else {
            return Ok(None);
        };
        let parsed = PasswordHash::new(&phc)
            .map_err(|e| anyhow!("reading the stored recovery code: {e}"))?;
        let right = Argon2::default()
            .verify_password(recovery_form(recovery).as_bytes(), &parsed)
            .is_ok();
        Ok(right.then_some(phc))
    }

    /// Sets a new code for whoever holds the recovery code, keeping what the old one was for,
    /// and draws the recovery code that replaces the one just spent. Returns it, in the clear
    /// this once.
    ///
    /// Both digests are made before the write, as everywhere here, and the write insists that
    /// the recovery digest it checked is still the one stored: a principal who drew a new one
    /// between the two has revoked the old, and the old must not work for being a moment late.
    /// What is refused is said as one answer, whether the code was wrong or none was ever drawn.
    pub fn recover(&self, recovery: &str, code: &str) -> Result<Result<String, RecoveryRefusal>> {
        let Some(phc) = digest_of(code)? else {
            return Ok(Err(RecoveryRefusal::BadCode));
        };
        let Some(matched) = self.recovery_matches(recovery)? else {
            return Ok(Err(RecoveryRefusal::WrongRecovery));
        };
        let phrase = new_recovery_phrase()?;
        let next = hash_secret(&recovery_form(&phrase))?;
        self.db.write(|cx| {
            let changed = cx.execute(
                "UPDATE household SET code_phc = ?1, recovery_phc = ?2,
                        code_scope = COALESCE(code_scope, 'restricted')
                 WHERE id = 1 AND recovery_phc = ?3",
                [phc.as_str(), next.as_str(), matched.as_str()],
            )?;
            Ok(if changed == 0 {
                Err(RecoveryRefusal::WrongRecovery)
            } else {
                Ok(phrase)
            })
        })
    }

    /// Whether `code` is the code. No code means nothing matches, never that everything does.
    pub fn check_code(&self, code: &str) -> Result<bool> {
        let phc: Option<Option<String>> = self.db.read(|cx| {
            cx.query_one("SELECT code_phc FROM household WHERE id = 1", [], |r| {
                r.get(0)
            })
        })?;
        let Some(phc) = phc.flatten() else {
            return Ok(false);
        };
        let parsed =
            PasswordHash::new(&phc).map_err(|e| anyhow!("reading the stored code: {e}"))?;
        Ok(Argon2::default()
            .verify_password(code.as_bytes(), &parsed)
            .is_ok())
    }

    /// Where a device sits. An error for a device that does not exist: the caller was
    /// recognised by its key a moment ago, so its absence is a revocation in between.
    pub fn seat(&self, device_id: &str) -> Result<Seat> {
        self.db.read(|cx| seat_of(cx, device_id))
    }

    /// The device changes the profile it reads as, by the table of the design: leaving a
    /// restricted profile (or a seat it is held on) takes the code whatever the target;
    /// reaching the principal takes it when the code guards everything; the rest is free.
    ///
    /// The code is hashed **before** the write, never inside it: Argon2 is slow on purpose and
    /// the writer is one for the whole library. So what was true when it was checked may not
    /// be true when the write runs — a profile made restricted, a scope tightened — and the
    /// write asks again with what it finds, refusing with `CodeNeeded` if the code it was
    /// given no longer covers it. The refusal is the safe side of the race.
    pub fn switch(
        &self,
        device_id: &str,
        to: &str,
        code: Option<&str>,
        keep: bool,
    ) -> Result<Result<Seat, SwitchRefusal>> {
        Ok(self
            .switch_checked(device_id, to, code, keep)?
            .map(|(seat, _)| seat))
    }

    /// [`Profiles::switch`], saying also whether the code was checked and found right. The
    /// route clears the brake on wrong codes only then: a switch that needed no code, with
    /// some text in the field, would otherwise wipe nine wrong guesses at the principal and
    /// let a reader's tablet guess for ever, ten at a time.
    pub fn switch_checked(
        &self,
        device_id: &str,
        to: &str,
        code: Option<&str>,
        keep: bool,
    ) -> Result<Result<(Seat, bool), SwitchRefusal>> {
        let facts = self.db.read(|cx| facts_of(cx, device_id, to))?;
        let Some(facts) = facts else {
            return Ok(Err(SwitchRefusal::NoSuchProfile));
        };
        if keep && facts.target_restricted {
            return Ok(Err(SwitchRefusal::NotYours));
        }
        let mut verified = false;
        if facts.code_needed() {
            let Some(code) = code else {
                return Ok(Err(SwitchRefusal::CodeNeeded));
            };
            if !self.check_code(code)? {
                return Ok(Err(SwitchRefusal::WrongCode));
            }
            verified = true;
        }
        self.db.write(|cx| {
            // One transaction from here: two switches at once are two writes in turn, and the
            // seat is whichever came last, never a profile of one and the flags of the other.
            let Some(facts) = facts_of(cx, device_id, to)? else {
                return Ok(Err(SwitchRefusal::NoSuchProfile));
            };
            if keep && facts.target_restricted {
                return Ok(Err(SwitchRefusal::NotYours));
            }
            if facts.code_needed() && !verified {
                return Ok(Err(SwitchRefusal::CodeNeeded));
            }
            let seat = sit(cx, device_id, to, keep)?
                .ok_or_else(|| anyhow!("there is no device {device_id}"))?;
            Ok(Ok((seat, verified)))
        })
    }

    /// The principal seats another device, or this one: their decision, so no code and a
    /// restricted profile may be kept. It also ends a choice the device was waiting on.
    pub fn place(
        &self,
        device_id: &str,
        to: &str,
        kept: bool,
    ) -> Result<Result<Seat, SwitchRefusal>> {
        self.db.write(|cx| {
            if find(cx, to)?.is_none() {
                return Ok(Err(SwitchRefusal::NoSuchProfile));
            }
            // Told apart from a failure: the id came from a request, and a device removed a
            // moment earlier from another window is the caller's stale list, not a fault.
            Ok(sit(cx, device_id, to, kept)?.ok_or(SwitchRefusal::NoSuchDevice))
        })
    }

    /// The profile's reading preferences, an opaque object; `{}` for a profile that has none
    /// or does not exist.
    pub fn reading(&self, profile_id: &str) -> Result<serde_json::Value> {
        let text: Option<String> = self.db.read(|cx| {
            cx.query_one(
                "SELECT reading FROM profile WHERE id = ?1",
                [profile_id],
                |r| r.get(0),
            )
        })?;
        let Some(text) = text else {
            return Ok(serde_json::json!({}));
        };
        Ok(serde_json::from_str(&text).unwrap_or_else(|e| {
            // Said, not swallowed: preferences that vanish with no trace look like a bug in
            // the client.
            tracing::warn!(
                profile = profile_id,
                "unreadable reading preferences, using none: {e}"
            );
            serde_json::json!({})
        }))
    }

    pub fn set_reading(&self, profile_id: &str, value: &serde_json::Value) -> Result<()> {
        let text = serde_json::to_string(value)?;
        self.db.write(|cx| {
            let touched = cx.execute(
                "UPDATE profile SET reading = ?2 WHERE id = ?1",
                [profile_id, text.as_str()],
            )?;
            if touched == 0 {
                return Err(anyhow!("there is no profile {profile_id}"));
            }
            Ok(())
        })
    }
}

/// The code as it is kept: an Argon2 PHC string, default parameters, a random salt. `None`
/// for a code outside four to eight characters. Slow on purpose: never call it under the
/// writer.
fn digest_of(code: &str) -> Result<Option<String>> {
    if !CODE_LENGTH.contains(&code.chars().count()) {
        return Ok(None);
    }
    hash_secret(code).map(Some)
}

/// A secret as a slow digest with a salt of its own: the code's and the recovery code's.
fn hash_secret(secret: &str) -> Result<String> {
    let mut salt = [0u8; 16];
    getrandom::fill(&mut salt).map_err(|e| anyhow!("drawing from the system's randomness: {e}"))?;
    let salt = SaltString::encode_b64(&salt).map_err(|e| anyhow!("encoding a salt: {e}"))?;
    Ok(Argon2::default()
        .hash_password(secret.as_bytes(), &salt)
        .map_err(|e| anyhow!("hashing the code: {e}"))?
        .to_string())
}

/// Sixteen characters from an alphabet without the ones that are mistaken for another — no 0
/// and O, no 1 and I — which is eighty bits, written in four groups so that it can be copied
/// off a screen onto paper. A power of two long, so that a byte masked to five bits draws
/// each one equally often.
const RECOVERY_ALPHABET: &[u8; 32] = b"ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
const RECOVERY_LENGTH: usize = 16;

fn new_recovery_phrase() -> Result<String> {
    let mut bytes = [0u8; RECOVERY_LENGTH];
    getrandom::fill(&mut bytes)
        .map_err(|e| anyhow!("drawing from the system's randomness: {e}"))?;
    let letters: Vec<char> = bytes
        .iter()
        .map(|b| char::from(RECOVERY_ALPHABET[usize::from(b & 31)]))
        .collect();
    Ok(letters
        .chunks(4)
        .map(|group| group.iter().collect::<String>())
        .collect::<Vec<_>>()
        .join("-"))
}

/// What is compared: the letters and digits, in capitals. The dashes are there to be read and
/// a space or a lower case is what a person copying it down adds.
fn recovery_form(typed: &str) -> String {
    typed
        .chars()
        .filter(char::is_ascii_alphanumeric)
        .map(|c| c.to_ascii_uppercase())
        .collect()
}

fn seat_of(cx: &Cx<'_>, device_id: &str) -> Result<Seat> {
    cx.query_one(
        "SELECT profile_id, kept, choosing, held FROM device WHERE id = ?1",
        [device_id],
        |r| {
            Ok(Seat {
                profile_id: r.get(0)?,
                kept: r.get(1)?,
                choosing: r.get(2)?,
                held: r.get(3)?,
            })
        },
    )?
    .ok_or_else(|| anyhow!("there is no device {device_id}"))
}

/// What the table of the design looks at, read together so the decision is made on one
/// moment's state.
struct Facts {
    /// The device sits on a restricted profile, or is held as if it did.
    leaving_restricted: bool,
    on_principal: bool,
    target_principal: bool,
    target_restricted: bool,
    code: Option<CodeScope>,
}

impl Facts {
    fn code_needed(&self) -> bool {
        // No code at all, nothing to ask: a device held after its restricted profile was
        // deleted must not be stuck for ever if the code was cleared since, which
        // `clear_code` allows once no profile is restricted.
        let Some(scope) = self.code else {
            return false;
        };
        self.leaving_restricted
            || (self.target_principal && scope == CodeScope::Everything && !self.on_principal)
    }
}

/// `None` when the target profile does not exist.
fn facts_of(cx: &Cx<'_>, device_id: &str, to: &str) -> Result<Option<Facts>> {
    let seat = seat_of(cx, device_id)?;
    let Some(target) = find(cx, to)? else {
        return Ok(None);
    };
    let here = find(cx, &seat.profile_id)?;
    let code = cx
        .query_one(
            "SELECT CASE WHEN code_phc IS NULL THEN NULL ELSE code_scope END
             FROM household WHERE id = 1",
            [],
            |r| r.get::<_, Option<String>>(0),
        )?
        .flatten();
    Ok(Some(Facts {
        leaving_restricted: seat.held || here.is_some_and(|p| p.kind == Kind::Restricted),
        // A device that must choose or is held only sits on `default` to hold its key: it is
        // not the principal's, and reaching the principal from there asks what it asks of
        // anyone else. Counting it as the principal let a deleted reader's device walk to the
        // principal without the code when the code guards everything. Nor is a seat on the
        // principal without « Retenir »: a device left there reopened on it for anyone, and the
        // code window accepted any code, because the server did not check one.
        on_principal: seat.profile_id == DEFAULT_PROFILE
            && seat.kept
            && !seat.choosing
            && !seat.held,
        target_principal: target.kind == Kind::Principal,
        target_restricted: target.kind == Kind::Restricted,
        code: code.map(|s| {
            if s == "everything" {
                CodeScope::Everything
            } else {
                CodeScope::Restricted
            }
        }),
    }))
}

/// The one statement that moves a device: profile and flags together, and the end of any
/// choice or hold, since a seat taken is a seat chosen. `None` when there is no such device.
fn sit(cx: &Cx<'_>, device_id: &str, to: &str, kept: bool) -> Result<Option<Seat>> {
    let touched = cx.execute(
        "UPDATE device SET profile_id = ?2, kept = ?3, choosing = 0, held = 0 WHERE id = ?1",
        rusqlite::params![device_id, to, kept],
    )?;
    if touched == 0 {
        return Ok(None);
    }
    Ok(Some(Seat {
        profile_id: to.to_string(),
        kept,
        choosing: false,
        held: false,
    }))
}

fn name_is_taken(cx: &Cx<'_>, name: &str, except: Option<&str>) -> Result<bool> {
    Ok(cx
        .query_one(
            "SELECT 1 FROM profile WHERE name_key = ?1 AND id <> ?2",
            [key_of(name).as_str(), except.unwrap_or("")],
            |_| Ok(()),
        )?
        .is_some())
}

fn color_is_taken(cx: &Cx<'_>, color: &str, except: Option<&str>) -> Result<bool> {
    Ok(cx
        .query_one(
            "SELECT 1 FROM profile WHERE color = ?1 AND id <> ?2",
            [color, except.unwrap_or("")],
            |_| Ok(()),
        )?
        .is_some())
}
