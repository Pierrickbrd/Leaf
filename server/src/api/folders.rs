//! The folders a series sits in on the disk, with the sidecar each one carries.
//!
//! A volume alone says nothing of what it belongs to: the universe, the work and the edition
//! are folders with a file each, and they decide what a folder *is* — `Dragon Ball/{Perfect,
//! Original}` and `Terres d'Arran/{Elfes, Mages}` have the same shape and opposite meanings.
//! A copy of the volumes without them is a copy of the shape and not of the meaning, so what
//! leaves the library for an export has to carry them.
//!
//! Read-only, and only what is there: a sidecar that does not exist is not made up from the
//! index, because the index is derived from the files and not the other way round.

use anyhow::Result;
use serde::Serialize;
use std::path::{Path, PathBuf};

use crate::scan::covers;
use crate::store::repository::Ceiling;
use crate::store::Db;

/// One folder of the chain, outermost first.
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct FolderDto {
    /// From the outermost folder of the chain, `/` between names: where it goes in a copy.
    pub path: String,
    pub level: &'static str,
    /// The file this level carries, absent when the folder has none.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub sidecar: Option<&'static str>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub content: Option<String>,
}

/// One file the series holds on the disk, as a copy has to carry it.
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct FileDto {
    /// Relative to the parent of the chain's outermost folder, `/` between names.
    pub path: String,
    pub kind: &'static str,
    /// The volume it is, or the one it sits beside; absent for the series' own images.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub entry_id: Option<String>,
    pub size: u64,
}

#[derive(Debug, Clone, Serialize)]
pub struct Manifest {
    pub folders: Vec<FolderDto>,
    pub files: Vec<FileDto>,
}

/// A listed file with the place it is read from, which the listing does not show.
pub struct Located {
    pub file: FileDto,
    pub disk: PathBuf,
}

pub struct Folders<'a> {
    db: &'a Db,
}

impl<'a> Folders<'a> {
    pub fn new(db: &'a Db) -> Self {
        Folders { db }
    }

    /// What an edition holds on the disk: the chain above it and its files.
    ///
    /// `None` when the series is not there for this caller, which is what a hidden one
    /// answers too.
    pub fn of(&self, edition_id: &str, ceiling: &Ceiling) -> Result<Option<Manifest>> {
        Ok(self
            .listed(edition_id, ceiling)?
            .map(|(folders, files)| Manifest {
                folders,
                files: files.into_iter().map(|l| l.file).collect(),
            }))
    }

    /// The file at `index` of that same listing: what `GET .../files/{index}` serves, so the
    /// number a client read in the manifest is the number it asks for.
    pub fn nth(
        &self,
        edition_id: &str,
        index: usize,
        ceiling: &Ceiling,
    ) -> Result<Option<Located>> {
        Ok(self
            .listed(edition_id, ceiling)?
            .and_then(|(_, files)| files.into_iter().nth(index)))
    }

    /// The chain above an edition — its universe if it has one, its work, then itself — and
    /// the files below it.
    ///
    /// An edition that has no folder of its own — its volumes sit in the work's — is not a
    /// level of the chain. A one-shot is neither a work folder nor an edition folder: both
    /// of its paths are the archive itself, and reading `<Album.cbz>/work.json` answered 500
    /// for every book of a universe. Its chain is its universe, if any.
    fn listed(
        &self,
        edition_id: &str,
        ceiling: &Ceiling,
    ) -> Result<Option<(Vec<FolderDto>, Vec<Located>)>> {
        let sql = format!(
            "SELECT u.path, w.path, ed.path, ed.implicit, ed.one_shot
             FROM edition ed
             JOIN work w ON w.id = ed.work_id
             LEFT JOIN universe u ON u.id = w.universe_id
             WHERE ed.id = ?1{}",
            ceiling.and_work("w")
        );
        let found = self.db.read(|cx| {
            cx.query_one(&sql, [edition_id], |r| {
                Ok((
                    r.get::<_, Option<String>>(0)?,
                    r.get::<_, String>(1)?,
                    r.get::<_, String>(2)?,
                    r.get::<_, i64>(3)? != 0,
                    r.get::<_, i64>(4)? != 0,
                ))
            })
        })?;
        let Some((universe, work, edition, implicit, one_shot)) = found else {
            return Ok(None);
        };

        let mut chain = Vec::new();
        let mut above = String::new();
        let mut last: Option<PathBuf> = None;
        let mut level = |disk: &str, level: &'static str, sidecar: &'static str| -> Result<()> {
            let name = Path::new(disk)
                .file_name()
                .map(|n| n.to_string_lossy().to_string())
                .unwrap_or_default();
            above = if above.is_empty() {
                name
            } else {
                format!("{above}/{name}")
            };
            let content = read(&Path::new(disk).join(sidecar))?;
            chain.push(FolderDto {
                path: above.clone(),
                level,
                sidecar: content.as_ref().map(|_| sidecar),
                content,
            });
            last = Some(PathBuf::from(disk));
            Ok(())
        };
        if let Some(universe) = &universe {
            level(universe, "UNIVERSE", "universe.json")?;
        }
        if !one_shot {
            level(&work, "WORK", "work.json")?;
            if !implicit && edition != work {
                level(&edition, "EDITION", "edition.json")?;
            }
        }

        // Where the series' own images are: its folder, which for an implicit edition is the
        // work's. A one-shot has none, its "folder" being a file.
        let own_folder = if one_shot {
            None
        } else if implicit {
            Some(PathBuf::from(&work))
        } else {
            Some(PathBuf::from(&edition))
        };
        let archives: Vec<(String, String)> = self.db.read(|cx| {
            cx.query(
                "SELECT id, file FROM entry WHERE edition_id = ?1 ORDER BY sort_key, file",
                [edition_id],
                |r| Ok((r.get::<_, String>(0)?, r.get::<_, String>(1)?)),
            )
        })?;

        let name_in_copy = |disk: &Path| -> String {
            let below = last
                .as_deref()
                .or_else(|| disk.parent())
                .and_then(|base| disk.strip_prefix(base).ok())
                .map(|p| p.to_string_lossy().replace('\\', "/"))
                .unwrap_or_else(|| {
                    disk.file_name()
                        .map(|n| n.to_string_lossy().to_string())
                        .unwrap_or_default()
                });
            if last.is_some() {
                format!("{above}/{below}")
            } else {
                below
            }
        };
        let listed = |kind: &'static str,
                      disk: PathBuf,
                      entry_id: Option<&str>,
                      files: &mut Vec<Located>| {
            // A file the index remembers and the disk no longer has is not part of a copy.
            if let Ok(facts) = std::fs::metadata(&disk) {
                files.push(Located {
                    file: FileDto {
                        path: name_in_copy(&disk),
                        kind,
                        entry_id: entry_id.map(str::to_string),
                        size: facts.len(),
                    },
                    disk,
                });
            }
        };
        let mut files = Vec::new();
        for (id, file) in &archives {
            let file = PathBuf::from(file);
            listed("VOLUME", file.clone(), Some(id), &mut files);
            for image in covers::all_beside_archive(&file) {
                listed("IMAGE", image, Some(id), &mut files);
            }
        }
        if let Some(folder) = own_folder {
            for image in covers::all_in_folder(&folder) {
                listed("IMAGE", image, None, &mut files);
            }
        }
        Ok(Some((chain, files)))
    }
}

/// The text of a sidecar, `None` when there is none. Anything else going wrong is an error:
/// a sidecar the disk refuses to give is not a sidecar that is not there.
fn read(file: &Path) -> Result<Option<String>> {
    match std::fs::read_to_string(file) {
        Ok(text) => Ok(Some(text)),
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => Ok(None),
        Err(e) => Err(e.into()),
    }
}
