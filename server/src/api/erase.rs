//! Taking things out of the library, which means taking them off the disk.
//!
//! **There is no trash in this model.** The library *is* the files; the index is rebuilt by
//! scanning them. So a deletion here is not a row disappearing with the file waiting
//! somewhere to be restored — the file goes, and no rescan brings it back. These are the two
//! most dangerous routes this server has, and everything in this module is written for that.
//!
//! Three rules hold it together:
//!
//!  - **The disk first, the row second.** If the file refuses to go, the index has not lied
//!    about it. The other order leaves a library claiming to have lost something it still
//!    holds, which a rescan would then put back — a deletion that undoes itself is worse
//!    than one that fails.
//!  - **Never outside a root.** An id resolves to a path, and a path is checked against the
//!    library roots before anything is unlinked. `relocate` already refuses to move a work
//!    outside them for the same reason, with the same test.
//!  - **What was not asked for stays.** The folder, its `work.json`, its `edition.json`, a
//!    cover dropped beside a volume: those are files somebody wrote, not files somebody
//!    asked to erase. An empty folder is visible and can be swept up; a `work.json` that
//!    went with the archives is gone for good. Re-importing into the same folder picks the
//!    metadata back up, which is the same decision read from the other side.

use anyhow::Result;
use serde::Serialize;
use std::path::{Path, PathBuf};

use crate::store::{Cx, Db};

/// What a deletion took, and what it could not.
#[derive(Debug, Default, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Erased {
    pub files: i64,
    pub bytes: i64,
    /// The files that would not go, by name. A deletion that half succeeded cannot be
    /// undone, so it is reported rather than rolled back: the caller is told exactly what is
    /// still there instead of being left to guess from a count.
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub refused: Vec<String>,
}

pub struct Erase<'a> {
    db: &'a Db,
    roots: &'a [PathBuf],
}

/// One file of the library, as the index holds it.
struct Held {
    id: String,
    file: PathBuf,
    size: i64,
}

impl<'a> Erase<'a> {
    pub fn new(db: &'a Db, roots: &'a [PathBuf]) -> Self {
        Erase { db, roots }
    }

    /// Removes one entry: its file, then its row.
    ///
    /// `None` when no entry has that id — the same answer as asking for it, and what makes
    /// deleting twice a 404 rather than a silent success.
    pub fn entry(&self, entry_id: &str) -> Result<Option<Erased>> {
        let held = self
            .db
            .read(|cx| Self::entries_of(cx, "e.id = ?1", entry_id))?;
        if held.is_empty() {
            return Ok(None);
        }
        Ok(Some(self.take(&held)?))
    }

    /// Removes a whole edition: every file it holds, then the rows that held them.
    ///
    /// The edition survives a file that would not go, and so does the work above it. An
    /// edition row with no entries would be a series the shelf draws and nothing opens.
    pub fn series(&self, edition_id: &str) -> Result<Option<Erased>> {
        let known: Option<String> = self.db.read(|cx| {
            cx.query_one("SELECT id FROM edition WHERE id = ?1", [edition_id], |r| {
                r.get(0)
            })
        })?;
        if known.is_none() {
            return Ok(None);
        }

        let held = self
            .db
            .read(|cx| Self::entries_of(cx, "e.edition_id = ?1", edition_id))?;
        let erased = self.take(&held)?;

        self.db.write(|cx| {
            let left: i64 = cx
                .query_one(
                    "SELECT COUNT(*) FROM entry WHERE edition_id = ?1",
                    [edition_id],
                    |r| r.get(0),
                )?
                .unwrap_or(0);
            if left > 0 {
                return Ok(());
            }
            let work: Option<String> = cx.query_one(
                "SELECT work_id FROM edition WHERE id = ?1",
                [edition_id],
                |r| r.get(0),
            )?;
            cx.execute("DELETE FROM edition WHERE id = ?1", [edition_id])?;
            // A work is the story, and a story with no edition left is nothing the shelf can
            // show. It goes with the last of them rather than lingering as a row no query
            // returns.
            if let Some(work) = work {
                let editions: i64 = cx
                    .query_one(
                        "SELECT COUNT(*) FROM edition WHERE work_id = ?1",
                        [work.as_str()],
                        |r| r.get(0),
                    )?
                    .unwrap_or(0);
                if editions == 0 {
                    cx.execute("DELETE FROM work WHERE id = ?1", [work.as_str()])?;
                }
            }
            Ok(())
        })?;

        Ok(Some(erased))
    }

    /// The files a clause selects, with what the index believes they weigh.
    fn entries_of(cx: &Cx<'_>, clause: &str, id: &str) -> Result<Vec<Held>> {
        cx.query(
            &format!("SELECT e.id, e.file, e.size FROM entry e WHERE {clause} ORDER BY e.id"),
            [id],
            |r| {
                Ok(Held {
                    id: r.get(0)?,
                    file: PathBuf::from(r.get::<_, String>(1)?),
                    size: r.get(2)?,
                })
            },
        )
    }

    /// Unlinks each file and drops the row that held it, one at a time.
    ///
    /// One at a time on purpose: a batch that failed halfway would leave the caller with a
    /// count and no way to know which half. Here every row that goes has had its file go
    /// first, and everything else is named in `refused`.
    fn take(&self, held: &[Held]) -> Result<Erased> {
        let mut erased = Erased::default();
        for one in held {
            let name = name_of(&one.file);
            if !self.within_a_root(&one.file) {
                // Not a failure to report as a stubborn file: an id that resolves outside
                // the library is the index disagreeing with the disk, and unlinking on that
                // basis is how a server deletes somebody's home directory.
                tracing::error!(file = %one.file.display(), "refusing to erase outside the library");
                erased.refused.push(name);
                continue;
            }
            match std::fs::remove_file(&one.file) {
                Ok(()) => {}
                // Already gone is the state that was asked for. The row still has to follow,
                // or the library would go on showing a book that is not there.
                Err(e) if e.kind() == std::io::ErrorKind::NotFound => {}
                Err(e) => {
                    tracing::warn!(file = %one.file.display(), error = %e, "could not erase");
                    erased.refused.push(name);
                    continue;
                }
            }
            self.db.write(|cx| {
                cx.execute("DELETE FROM entry WHERE id = ?1", [one.id.as_str()])?;
                Ok(())
            })?;
            erased.files += 1;
            erased.bytes += one.size;
        }
        Ok(erased)
    }

    /// Whether a path lies under one of the roots this server was told to serve.
    fn within_a_root(&self, file: &Path) -> bool {
        self.roots.iter().any(|root| file.starts_with(root))
    }
}

fn name_of(path: &Path) -> String {
    path.file_name()
        .map(|n| n.to_string_lossy().to_string())
        .unwrap_or_else(|| path.to_string_lossy().to_string())
}
