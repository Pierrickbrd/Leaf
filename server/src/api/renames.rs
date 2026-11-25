//! What a route does about the names of files when it changes what they are made of.
//!
//! A series retitled, a volume renumbered: [`crate::store::renaming`] works out what that
//! moves and does it. This is the part that belongs to a route — what a patch says about
//! names, and how a refusal is told to the person who asked.

use anyhow::Result;

use crate::api::records::{EntryPatch, SeriesPatch};
use crate::scan::scanner::Scanner;
use crate::store::renaming::{self, Overrides, Plan, Refusal};
use crate::store::Db;

/// A refusal, in words: the answer of a `409`, and what to do about it.
pub fn refused(why: Refusal) -> anyhow::Error {
    match why {
        Refusal::Collision(paths) => crate::api::conflict(format!(
            "these files are in the way, and nothing was renamed: {}",
            paths
                .iter()
                .map(|p| p.display().to_string())
                .collect::<Vec<_>>()
                .join(", ")
        )),
        Refusal::ReadOnly => crate::api::conflict(
            "the library is read-only, so nothing could be renamed and nothing was changed",
        ),
        Refusal::SameEditionName(name) => crate::api::conflict(format!(
            "this series already has an edition called \"{name}\" — two editions cannot go by \
             the same name, and nothing was changed"
        )),
        Refusal::CannotStampIdentity(file) => crate::api::conflict(format!(
            "{} cannot be written, and a folder renamed without it would be taken for a new \
             one — nothing was renamed",
            file.display()
        )),
    }
}

/// What renaming a series' title or an edition's name would move. `None` when the patch names
/// neither.
pub fn for_series(db: &Db, edition_id: &str, patch: &SeriesPatch) -> Result<Option<Plan>> {
    let mut wanted = Overrides::default();
    // A cleared title hands the name back to the folder, whose name is already the one on
    // the disk: nothing to move.
    if let Some(Some(title)) = patch
        .title
        .as_ref()
        .filter(|t| t.as_ref().is_some_and(|t| !t.trim().is_empty()))
    {
        wanted.work_name = Some(title.clone());
    }
    if let Some(name) = &patch.name {
        wanted
            .edition_names
            .insert(edition_id.to_string(), name.clone());
    }
    if wanted.work_name.is_none() && wanted.edition_names.is_empty() {
        return Ok(None);
    }
    let Some(work) = db.read(|cx| {
        cx.query_one(
            "SELECT work_id FROM edition WHERE id = ?1",
            [edition_id],
            |r| r.get::<_, String>(0),
        )
    })?
    else {
        return Ok(None);
    };
    planned(db, &work, &wanted)
}

/// What renumbering or retitling a volume would move.
pub fn for_entry(db: &Db, entry_id: &str, patch: &EntryPatch) -> Result<Option<Plan>> {
    let mut wanted = Overrides::default();
    if let Some(number) = patch.number {
        wanted.numbers.insert(entry_id.to_string(), Some(number));
    }
    if let Some(title) = &patch.title {
        wanted.titles.insert(entry_id.to_string(), title.clone());
    }
    if wanted.numbers.is_empty() && wanted.titles.is_empty() {
        return Ok(None);
    }
    let Some(work) = db.read(|cx| {
        cx.query_one(
            "SELECT e.work_id FROM entry x JOIN edition e ON e.id = x.edition_id WHERE x.id = ?1",
            [entry_id],
            |r| r.get::<_, String>(0),
        )
    })?
    else {
        return Ok(None);
    };
    planned(db, &work, &wanted)
}

fn planned(db: &Db, work: &str, wanted: &Overrides) -> Result<Option<Plan>> {
    match db.read(|cx| renaming::plan_work(cx, work, wanted))? {
        Ok(plan) if plan.moves.is_empty() && plan.rewrites.is_empty() => Ok(None),
        Ok(plan) => Ok(Some(plan)),
        Err(why) => Err(refused(why)),
    }
}

/// Does what a plan says, when there is one.
pub fn apply(scanner: &Scanner, db: &Db, plan: Option<Plan>) -> Result<()> {
    match plan {
        Some(plan) => renaming::execute(scanner, db, &plan),
        None => Ok(()),
    }
}

/// What filing a series into a universe, or out of one, does to the names of its volumes:
/// the universe enters them, or leaves them. Worked out **before** the folder moves, against
/// the folder where it is, so that a name in the way refuses the move rather than being
/// found after it.
pub fn before_move(db: &Db, work: &str, universe: Option<&str>) -> Result<Option<Plan>> {
    let name = match universe {
        Some(id) => db.read(|cx| {
            cx.query_one("SELECT name FROM universe WHERE id = ?1", [id], |r| {
                r.get::<_, String>(0)
            })
        })?,
        None => None,
    };
    let wanted = Overrides {
        universe: Some(name),
        ..Overrides::default()
    };
    planned(db, work, &wanted)
}

/// Does what a plan says from inside the lock the caller holds.
pub fn apply_held(
    scanner: &Scanner,
    held: &crate::scan::scanner::Held,
    db: &Db,
    plan: Option<&Plan>,
) -> Result<()> {
    match plan {
        Some(plan) => renaming::execute_held(scanner, held, db, plan),
        None => Ok(()),
    }
}
/// Names the volumes an import has just put over others as the library names a volume.
///
/// The replacement lands on the file it replaces, so that the row — and with it every
/// reader's place — is the same one; it is named afterwards, by the same plan any rename
/// goes through, so a name in the way stops it rather than being written over. A refusal is
/// not an error of the import: the volume is installed, and keeps the name it had.
pub fn after_replacement(
    scanner: &Scanner,
    db: &Db,
    replaced: &[crate::api::bulk_import::ReplacedFile],
) -> Result<()> {
    let mut by_work: std::collections::BTreeMap<String, Overrides> = Default::default();
    for entry in replaced.iter().filter_map(|r| r.entry_id.as_deref()) {
        let Some(work) = db.read(|cx| {
            cx.query_one(
                "SELECT e.work_id FROM entry x JOIN edition e ON e.id = x.edition_id
                 WHERE x.id = ?1",
                [entry],
                |r| r.get::<_, String>(0),
            )
        })?
        else {
            continue;
        };
        by_work
            .entry(work)
            .or_default()
            .normalise
            .insert(entry.to_string());
    }
    for (work, wanted) in by_work {
        match db.read(|cx| renaming::plan_work(cx, &work, &wanted))? {
            Ok(plan) => apply(scanner, db, Some(plan))?,
            Err(why) => tracing::warn!(?why, work, "a replaced volume keeps the name it had"),
        }
    }
    Ok(())
}

/// Commits an import. One that adds editions to a series that declares none first gives the
/// edition it holds a folder of its own, under the scanners' lock and before any new volume is
/// laid down, so that the series goes from one edition to several in one move: the reading of
/// every volume is kept, and the new editions land beside a folder that is already there.
pub fn commit_import(
    scanner: &Scanner,
    db: &Db,
    bulk: &crate::api::bulk_import::BulkImport,
    id: &str,
    commit: impl FnOnce(&mut dyn FnMut() -> Result<()>) -> Result<crate::api::bulk_import::ImportResult>,
) -> Result<crate::api::bulk_import::ImportResult> {
    let Some(added) = bulk.editions_added(id)? else {
        return commit(&mut || Ok(()));
    };
    scanner.exclusive(|held| {
        // Done by the commit itself, under its lock and once it is about to install a first
        // file: the series is not re-read from the index before, and nothing is moved for a
        // commit that ends up refusing.
        commit(&mut || {
            // A series with one edition, named or not, becomes a series with two: the
            // edition it holds gets its own folder and its volumes the names that say it.
            let single = db.read(|cx| {
                cx.query_one(
                    "SELECT w.id, ed.name FROM edition ed JOIN work w ON w.id = ed.work_id
                         WHERE ed.id = ?1
                           AND (SELECT COUNT(*) FROM edition x WHERE x.work_id = w.id) = 1",
                    [added.series.as_str()],
                    |r| Ok((r.get::<_, String>(0)?, r.get::<_, Option<String>>(1)?)),
                )
            })?;
            let Some((work, held_name)) = single else {
                return Ok(());
            };
            let current = added.current_name.clone().or(held_name).unwrap_or_default();
            let plan =
                db.read(|cx| renaming::plan_second_edition(cx, &work, &current, &added.first_new))?;
            match plan {
                Ok(plan) => renaming::execute_held(scanner, held, db, &plan),
                Err(why) => Err(refused(why)),
            }
        })
    })
}
