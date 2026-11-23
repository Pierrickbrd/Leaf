//! The file an archive was rewritten from, so that an import recognises it again.
//!
//! A rewritten archive has another size and another checksum than the file announced for
//! it; compared to the manifest it looks changed, and would be sent at every re-import.

use std::collections::HashMap;

use anyhow::Result;
use rusqlite::params;

use super::db::Db;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Origin {
    /// Relative to the library: the root, then the file's path under it.
    pub path: String,
    /// The size of the archive as it was kept.
    pub kept_size: u64,
    /// The size and checksum of the file that was announced and rewritten.
    pub origin_size: u64,
    pub origin_checksum: Option<String>,
    /// What the rewrite was asked to do, as a digest of its `entry.json` and of the names it
    /// removed. The same bytes sent with a corrected `entry.json` are another request, and
    /// an origin keeping only sizes and checksum answered « already there » to it.
    pub rewrite: Option<String>,
}

/// Inserts or replaces, in one transaction: a file rewritten again has a new origin, and
/// the old one would make the next import see a change.
pub fn record(db: &Db, origins: &[Origin]) -> Result<()> {
    db.write(|cx| {
        for o in origins {
            cx.execute(
                "INSERT OR REPLACE INTO import_origin
                    (path, kept_size, origin_size, origin_checksum, rewrite)
                 VALUES (?, ?, ?, ?, ?)",
                params![
                    o.path,
                    i64::try_from(o.kept_size)?,
                    i64::try_from(o.origin_size)?,
                    o.origin_checksum,
                    o.rewrite
                ],
            )?;
        }
        Ok(())
    })
}

/// The origins of those paths that have one, in one query whatever their number.
pub fn known(db: &Db, paths: &[String]) -> Result<HashMap<String, Origin>> {
    if paths.is_empty() {
        return Ok(HashMap::new());
    }
    // Built from the count, never from the values: the paths are bound as parameters.
    let holes = std::iter::repeat_n("?", paths.len())
        .collect::<Vec<_>>()
        .join(",");
    let sql = format!(
        "SELECT path, kept_size, origin_size, origin_checksum, rewrite
         FROM import_origin WHERE path IN ({holes})"
    );
    let found = db.read(|cx| {
        cx.query(&sql, rusqlite::params_from_iter(paths.iter()), |r| {
            Ok(Origin {
                path: r.get(0)?,
                kept_size: r.get::<_, i64>(1)?.unsigned_abs(),
                origin_size: r.get::<_, i64>(2)?.unsigned_abs(),
                origin_checksum: r.get(3)?,
                rewrite: r.get(4)?,
            })
        })
    })?;
    Ok(found.into_iter().map(|o| (o.path.clone(), o)).collect())
}

/// Follows an archive Leaf rewrote itself — a title edited, a stamp on download — to its new
/// size, so the next import does not take Leaf's own edit for one made by hand.
///
/// Only when the file was still as it was kept (`was`): one that had already changed was
/// edited behind Leaf's back, and following it would erase the only sign of that. Returns
/// whether an origin was followed; a path that has none is not an error, it is most files.
pub fn follow(db: &Db, path: &str, was: u64, now: u64) -> Result<bool> {
    let changed = db.write(|cx| {
        cx.execute(
            "UPDATE import_origin SET kept_size = ? WHERE path = ? AND kept_size = ?",
            params![i64::try_from(now)?, path, i64::try_from(was)?],
        )
    })?;
    Ok(changed > 0)
}
