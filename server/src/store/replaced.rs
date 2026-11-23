//! What a reimport replaced, and what became of the places people had reached in it.
//!
//! A replacement puts a new file where an old one was, under the old one's name, so the
//! entry keeps its identity and everything attached to it. The old file is set aside, and
//! nothing in the index says it ever existed: a person who finds a volume changed has no way
//! to learn what was replaced, where the old one is waiting, or why a place moved. This is
//! that record, written in the transaction that settles the places — one or the other, never
//! a replacement without its record or places moved without a trace.

use anyhow::Result;
use rusqlite::params;
use serde::{Deserialize, Serialize};

use super::db::Db;
use super::held::unsigned;

/// One file replaced by an import session.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Replaced {
    /// The session that did it; with `path`, what makes recording it twice one line.
    pub import_id: String,
    /// Where the new file landed, relative to the library.
    pub path: String,
    pub entry_id: String,
    /// Where the old file waits, relative to the inbox: `replaced/<session>/<landing>`.
    pub kept_at: String,
    pub replaced_at: i64,
    /// The profile that asked.
    pub profile_id: String,
    pub old_size: u64,
    pub new_size: u64,
    pub old_pages: Option<u32>,
    pub new_pages: Option<u32>,
}

/// What to do with the places readers had in the replaced volume. `last` is the last page
/// of the file that replaces it, counted from 0.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub enum Positions {
    /// Every place stays, brought back to `last` if it is past it.
    Kept { last: u32 },
    /// The asking profile goes back to the first page; the others stay, brought back to
    /// `last` if they are past it. Never another profile's place: they did not ask.
    Reset { profile: String, last: u32 },
    /// Pages `from..=old_to` became `from..=new_to`: a place after the range moves by the
    /// difference, one inside it stays if it still fits and is brought to its new end if not.
    Spliced {
        from: u32,
        old_to: u32,
        new_to: u32,
        last: u32,
    },
}

impl Positions {
    fn name(&self) -> &'static str {
        match self {
            Positions::Kept { .. } => "KEPT",
            Positions::Reset { .. } => "RESET",
            Positions::Spliced { .. } => "SPLICED",
        }
    }
}

/// Records the replacement and settles the places of its entry, in one transaction and a
/// number of statements that does not depend on how many people were reading.
///
/// `finished` and `times_finished` are never written here: what a person has read is not
/// undone by the file being better.
pub fn replace(db: &Db, done: &Replaced, positions: &Positions) -> Result<()> {
    let old_size = i64::try_from(done.old_size)?;
    let new_size = i64::try_from(done.new_size)?;
    db.write(|cx| {
        cx.execute(
            "INSERT OR REPLACE INTO replaced_file
                (import_id, path, entry_id, kept_at, replaced_at, profile_id, old_size,
                 new_size, old_pages, new_pages, positions)
             VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
            params![
                done.import_id,
                done.path,
                done.entry_id,
                done.kept_at,
                done.replaced_at,
                done.profile_id,
                old_size,
                new_size,
                done.old_pages,
                done.new_pages,
                positions.name()
            ],
        )?;
        match positions {
            Positions::Kept { last } => cx.execute(
                "UPDATE progress SET page = ?2 WHERE entry_id = ?1 AND page > ?2",
                params![done.entry_id, last],
            )?,
            Positions::Reset { profile, last } => cx.execute(
                "UPDATE progress
                 SET page = CASE WHEN profile_id = ?2 THEN 0 ELSE MIN(page, ?3) END
                 WHERE entry_id = ?1",
                params![done.entry_id, profile, last],
            )?,
            Positions::Spliced {
                from,
                old_to,
                new_to,
                last,
            } => cx.execute(
                "UPDATE progress
                 SET page = MIN(?5, CASE
                       WHEN page > ?3 THEN page + (?4 - ?3)
                       WHEN page >= ?2 THEN MIN(page, ?4)
                       ELSE page
                     END)
                 WHERE entry_id = ?1",
                params![done.entry_id, from, old_to, new_to, last],
            )?,
        };
        Ok(())
    })
}

/// What was replaced in an entry, oldest first. In one statement.
pub fn of_entry(db: &Db, entry_id: &str) -> Result<Vec<Replaced>> {
    db.read(|cx| {
        cx.query(
            "SELECT import_id, path, entry_id, kept_at, replaced_at, profile_id, old_size,
                    new_size, old_pages, new_pages
             FROM replaced_file WHERE entry_id = ? ORDER BY replaced_at, import_id, path",
            [entry_id],
            |r| {
                Ok(Replaced {
                    import_id: r.get(0)?,
                    path: r.get(1)?,
                    entry_id: r.get(2)?,
                    kept_at: r.get(3)?,
                    replaced_at: r.get(4)?,
                    profile_id: r.get(5)?,
                    old_size: unsigned(r.get::<_, i64>(6)?, 6)?,
                    new_size: unsigned(r.get::<_, i64>(7)?, 7)?,
                    old_pages: r
                        .get::<_, Option<i64>>(8)?
                        .map(|n| unsigned(n, 8))
                        .transpose()?,
                    new_pages: r
                        .get::<_, Option<i64>>(9)?
                        .map(|n| unsigned(n, 9))
                        .transpose()?,
                })
            },
        )
    })
}

/// The files one session set aside, as recorded. In one statement.
pub fn kept_by(db: &Db, import_id: &str) -> Result<Vec<String>> {
    db.read(|cx| {
        cx.query(
            "SELECT kept_at FROM replaced_file WHERE import_id = ?",
            [import_id],
            |r| r.get(0),
        )
    })
}

/// Whether a session already recorded a replacement at this path. In one statement.
pub fn recorded(db: &Db, import_id: &str, path: &str) -> Result<bool> {
    db.read(|cx| {
        cx.query_one(
            "SELECT 1 FROM replaced_file WHERE import_id = ?1 AND path = ?2",
            [import_id, path],
            |_| Ok(()),
        )
    })
    .map(|found| found.is_some())
}
