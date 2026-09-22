//! Moving a work into a universe, or out of one.
//!
//! A `rename` and a rescan, and nothing else — which it can only be because an identity no
//! longer comes from a path. Before [`crate::scan::identity`] this route would have destroyed
//! the reading position of everything it moved, so the two changes land in that order and on
//! one branch: shipping this first would have turned an accidental loss into one offered by
//! a button.
//!
//! **No route creates a universe.** A folder *is* the declaration, so this moves a work into
//! a universe that already exists, and refuses to invent one.

use std::path::{Path, PathBuf};

use anyhow::{Context, Result};
use serde::{Deserialize, Serialize};

use crate::store::Db;

/// Whether the folder already at a destination is this very work, read from the identity in
/// its own `work.json`.
///
/// The folder being occupied says nothing by itself: another work of the same name sitting
/// there is a collision to refuse, while this work already sitting there is a move that has
/// already happened. Only the sidecar tells the two apart, and a folder without one — or
/// with one that says nothing — is not this work as far as anybody can prove.
fn arrived(to: &Path, work_id: &str) -> bool {
    let Ok(bytes) = std::fs::read(to.join(crate::scan::layout::WORK_FILE)) else {
        return false;
    };
    crate::metadata::sidecars::Document::of(&bytes)
        .text(crate::scan::identity::FIELD)
        .is_some_and(|carried| carried == work_id)
}

/// Where a work is asked to go. An absent `universe_id` means out to the library root it
/// lives under — which is a destination like any other, and the only way back out.
#[derive(Debug, Default, Deserialize)]
#[serde(default, rename_all = "camelCase")]
pub struct MoveRequest {
    pub universe_id: Option<String>,
}

/// Where it ended up.
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Moved {
    pub work_id: String,
    /// The folder it is in now. Named because the answer is what a caller checks against,
    /// and a move that did nothing answers exactly like one that did.
    pub path: String,
    /// Whether anything was renamed. A work asked to go where it already is is not a fault
    /// — asking twice is how a retry looks — and saying so beats answering the same either
    /// way.
    pub moved: bool,
}

pub struct Relocate<'a> {
    db: &'a Db,
    roots: &'a [PathBuf],
}

impl<'a> Relocate<'a> {
    pub fn new(db: &'a Db, roots: &'a [PathBuf]) -> Self {
        Relocate { db, roots }
    }

    /// Moves the work's folder to `to`, which is the folder it will *be*, name included.
    ///
    /// The whole path and not a parent to join a name onto: an import can declare a work
    /// under a different name than the one it currently has, and joining the old name would
    /// quietly file it somewhere nobody named.
    ///
    /// `None` when there is no such work. The disk first and the index after: the files are
    /// the truth, so the rescan the caller runs reads back what this did rather than being
    /// told about it.
    pub fn work(&self, work_id: &str, to: &Path) -> Result<Option<Moved>> {
        let Some(from) = self.folder_of(work_id)? else {
            return Ok(None);
        };
        let to = to.to_path_buf();
        let name = to
            .file_name()
            .ok_or_else(|| anyhow::anyhow!("{} has no name", to.display()))?
            .to_os_string();
        if to == from {
            return Ok(Some(Moved {
                work_id: work_id.to_string(),
                path: to.to_string_lossy().to_string(),
                moved: false,
            }));
        }
        // A destination inside the work being moved would make the work its own parent, and
        // the rename would take the destination away with the source.
        if to.starts_with(&from) {
            return Err(crate::api::invalid(
                "a work cannot be moved into a folder that sits inside it",
            ));
        }
        if to.exists() {
            // Already arrived, and the index has not caught up: a commit that moved a work
            // and left its session open rescans *behind* itself, so a client sending the
            // rest and committing again with the same `move` list reached this with a path
            // read before the move. Refusing made that the answer of the whole commit —
            // nothing installed — for a second commit that asked for exactly what the first
            // had done. A move whose source is gone and whose destination holds this very
            // work is a resumption, and resumptions answer the way `to == from` already
            // does. Told apart by the identity in the sidecar and not by the folder being
            // occupied, because another work sitting there is a real collision.
            if !from.exists() && arrived(&to, work_id) {
                return Ok(Some(Moved {
                    work_id: work_id.to_string(),
                    path: to.to_string_lossy().to_string(),
                    moved: false,
                }));
            }
            return Err(crate::api::invalid(format!(
                "\"{}\" is already there — rename one of the two first",
                name.to_string_lossy()
            )));
        }

        if let Some(parent) = to.parent() {
            std::fs::create_dir_all(parent)
                .with_context(|| format!("making {}", parent.display()))?;
        }
        std::fs::rename(&from, &to)
            .with_context(|| format!("moving {} to {}", from.display(), to.display()))?;
        Ok(Some(Moved {
            work_id: work_id.to_string(),
            path: to.to_string_lossy().to_string(),
            moved: true,
        }))
    }

    fn folder_of(&self, work_id: &str) -> Result<Option<PathBuf>> {
        Ok(self
            .db
            .read(|cx| {
                cx.query_one("SELECT path FROM work WHERE id = ?1", [work_id], |r| {
                    r.get::<_, String>(0)
                })
            })?
            .map(PathBuf::from))
    }

    /// The folder a work is to end up in, from the universe a request named.
    ///
    /// Public because the route resolves it and [`Relocate::work`] takes the answer: one
    /// place turns "this universe" into a path, and the import turns its manifest into one
    /// the same way.
    pub fn into_universe(
        &self,
        work_id: &str,
        universe_id: Option<&str>,
    ) -> Result<Option<PathBuf>> {
        let Some(from) = self.folder_of(work_id)? else {
            return Ok(None);
        };
        let name = from
            .file_name()
            .ok_or_else(|| anyhow::anyhow!("{} has no name", from.display()))?
            .to_os_string();
        Ok(Some(self.destination(&from, universe_id)?.join(name)))
    }

    /// The folder the work is being moved into.
    fn destination(&self, from: &Path, into: Option<&str>) -> Result<PathBuf> {
        let Some(universe_id) = into else {
            // Out to the root this work already lives under, and never to "the first root":
            // a server with two libraries would quietly move a series from one to the other.
            return self
                .roots
                .iter()
                .find(|root| from.starts_with(root))
                .cloned()
                .ok_or_else(|| {
                    anyhow::anyhow!("{} is under none of the library roots", from.display())
                });
        };
        let found: Option<String> = self.db.read(|cx| {
            cx.query_one(
                "SELECT path FROM universe WHERE id = ?1",
                [universe_id],
                |r| r.get(0),
            )
        })?;
        found
            .map(PathBuf::from)
            .ok_or_else(|| crate::api::absent("unknown universe".to_string()))
    }
}
