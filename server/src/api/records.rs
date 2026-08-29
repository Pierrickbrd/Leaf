//! Editing what the files say about themselves.
//!
//! **Nothing here writes to the index.** Every edit goes into a sidecar on the disk, and the
//! rescan that follows reads it back. That is the whole point of the model: the files are
//! the truth, the index is derived, and an edit that only reached the database would be lost
//! the next time the library was scanned.
//!
//! It also means an edit survives everything — a rebuilt index, a restored backup, another
//! reader entirely.

use std::path::{Path, PathBuf};

use anyhow::{anyhow, Result};
use serde::Deserialize;

use crate::archive::{cbz, cbz_writer};
use crate::metadata::legacy_comic_info;
use crate::metadata::sidecars::{self, ArcJson, EditionJson, EntryJson, WorkJson, FORMAT_VERSION};
use crate::scan::layout;
use crate::scan::scanner::ENTRY_JSON;
use crate::store::Db;

#[derive(Debug, Default, Deserialize)]
#[serde(default, rename_all = "camelCase")]
pub struct SeriesPatch {
    pub name: Option<String>,
    pub title: Option<String>,
    pub medium: Option<String>,
    pub author: Option<String>,
    pub status: Option<String>,
    pub reading_direction: Option<String>,
    pub genres: Option<Vec<String>>,
    pub summary: Option<String>,
    pub publisher: Option<String>,
    pub volume_count: Option<i32>,
    pub format: Option<String>,
    pub language: Option<String>,
}

impl SeriesPatch {
    /// What describes the work goes into work.json, whichever edition was targeted.
    fn touches_work(&self) -> bool {
        self.title.is_some()
            || self.medium.is_some()
            || self.author.is_some()
            || self.status.is_some()
            || self.reading_direction.is_some()
            || self.summary.is_some()
            || self.genres.is_some()
    }

    fn touches_edition(&self) -> bool {
        self.name.is_some()
            || self.publisher.is_some()
            || self.format.is_some()
            || self.language.is_some()
            || self.status.is_some()
            || self.volume_count.is_some()
    }
}

#[derive(Debug, Default, Deserialize)]
#[serde(default, rename_all = "camelCase")]
pub struct EntryPatch {
    pub number: Option<f64>,
    pub title: Option<String>,
    pub isbn: Option<String>,
    pub published_on: Option<String>,
    pub summary: Option<String>,
}

pub struct Records<'a> {
    db: &'a Db,
}

/// Where an edition's files live, and under what names.
struct Places {
    work: PathBuf,
    edition: PathBuf,
    implicit: bool,
    work_name: String,
    edition_name: Option<String>,
}

impl<'a> Records<'a> {
    pub fn new(db: &'a Db) -> Self {
        Records { db }
    }

    pub fn patch_series(&self, edition_id: &str, patch: &SeriesPatch) -> Result<bool> {
        let Some(where_) = self.places(edition_id)? else {
            return Ok(false);
        };

        if patch.touches_work() {
            merge::<WorkJson>(&where_.work.join(layout::WORK_FILE), |mut current| {
                current.leaf = Some(FORMAT_VERSION);
                current.title = patch.title.clone().or(current.title);
                current.medium = patch.medium.clone().or(current.medium);
                current.author = patch.author.clone().or(current.author);
                current.status = patch.status.clone().or(current.status);
                current.reading_direction = patch
                    .reading_direction
                    .clone()
                    .or(current.reading_direction);
                if let Some(genres) = &patch.genres {
                    current.genres = genres.clone();
                }
                current.summary = patch.summary.clone().or(current.summary);
                current
            })?;
        }

        if !patch.touches_edition() {
            return Ok(true);
        }

        if where_.implicit {
            // An implicit edition has no folder of its own: its fields go down into
            // work.json. Dropping an edition.json there would flip how the folder is
            // classified — it would stop being a work and become an edition.
            merge::<WorkJson>(&where_.work.join(layout::WORK_FILE), |mut current| {
                current.leaf = Some(FORMAT_VERSION);
                current.publisher = patch.publisher.clone().or(current.publisher);
                current.volume_count = patch.volume_count.or(current.volume_count);
                current.format = patch.format.clone().or(current.format);
                current.language = patch.language.clone().or(current.language);
                current.status = patch.status.clone().or(current.status);
                current
            })?;
        } else {
            merge::<EditionJson>(&where_.edition.join(layout::EDITION_FILE), |mut current| {
                current.leaf = Some(FORMAT_VERSION);
                current.name = patch.name.clone().or(current.name);
                current.publisher = patch.publisher.clone().or(current.publisher);
                current.status = patch.status.clone().or(current.status);
                current.volume_count = patch.volume_count.or(current.volume_count);
                current.format = patch.format.clone().or(current.format);
                current.language = patch.language.clone().or(current.language);
                current
            })?;
        }
        Ok(true)
    }

    pub fn set_arcs(&self, edition_id: &str, arcs: Vec<ArcJson>) -> Result<bool> {
        let Some(where_) = self.places(edition_id)? else {
            return Ok(false);
        };
        if where_.implicit {
            merge::<WorkJson>(&where_.work.join(layout::WORK_FILE), |mut current| {
                current.arcs = arcs.clone();
                current
            })?;
        } else {
            merge::<EditionJson>(&where_.edition.join(layout::EDITION_FILE), |mut current| {
                current.arcs = arcs.clone();
                current
            })?;
        }
        Ok(true)
    }

    pub fn patch_entry(&self, entry_id: &str, patch: &EntryPatch) -> Result<bool> {
        let row = self.db.read(|cx| {
            cx.query_one(
                "SELECT file, edition_id FROM entry WHERE id = ?1",
                [entry_id],
                |r| Ok((r.get::<_, String>(0)?, r.get::<_, String>(1)?)),
            )
        })?;
        let Some((file, edition_id)) = row else {
            return Ok(false);
        };
        let Some(where_) = self.places(&edition_id)? else {
            return Ok(false);
        };

        let file = PathBuf::from(file);
        let mut current = read_entry_json(&file).unwrap_or_default();
        current.leaf = Some(FORMAT_VERSION);
        current.work = Some(where_.work_name);
        current.edition = where_.edition_name;
        current.id = Some(entry_id.to_string());
        current.number = patch.number.or(current.number);
        current.title = patch.title.clone().or(current.title);
        current.isbn = patch.isbn.clone().or(current.isbn);
        current.published_on = patch.published_on.clone().or(current.published_on);
        current.summary = patch.summary.clone().or(current.summary);

        cbz_writer::replace_sidecar(&file, ENTRY_JSON, &sidecars::write(&current)?)?;
        Ok(true)
    }

    /// Writes a volume's identity into the volume itself, so it can find its way home.
    ///
    /// This is what makes the round trip work: a file that leaves stamped with its work,
    /// edition and number can be handed back months later and filed without being asked
    /// where it belongs.
    pub fn stamp_entry(&self, entry_id: &str) -> Result<bool> {
        let row = self.db.read(|cx| {
            cx.query_one(
                "SELECT file, edition_id, volume_number, type FROM entry WHERE id = ?1",
                [entry_id],
                |r| {
                    Ok((
                        r.get::<_, String>(0)?,
                        r.get::<_, String>(1)?,
                        r.get::<_, Option<f64>>(2)?,
                        r.get::<_, String>(3)?,
                    ))
                },
            )
        })?;
        let Some((file, edition_id, number, kind)) = row else {
            return Ok(false);
        };
        let Some(where_) = self.places(&edition_id)? else {
            return Ok(false);
        };

        if let Err(e) = self.stamp(
            Path::new(&file),
            entry_id,
            &where_.work_name,
            where_.edition_name.as_deref(),
            number,
            &kind,
        ) {
            // A file that cannot be stamped is still a file worth serving: the download
            // goes ahead, it just arrives without its identity.
            tracing::warn!(file, error = %e, "could not stamp");
        }
        Ok(true)
    }

    pub fn stamp(
        &self,
        file: &Path,
        entry_id: &str,
        work_name: &str,
        edition_name: Option<&str>,
        number: Option<f64>,
        kind: &str,
    ) -> Result<()> {
        let mut current = read_entry_json(file).unwrap_or_default();
        // Already up to date: do not rewrite, or every download would modify the file and
        // trigger a full reanalysis on the next scan.
        if current.leaf == Some(FORMAT_VERSION)
            && current.id.as_deref() == Some(entry_id)
            && current.work.as_deref() == Some(work_name)
            && current.edition.as_deref() == edition_name
        {
            return Ok(());
        }
        current.leaf = Some(FORMAT_VERSION);
        current.work = Some(work_name.to_string());
        current.edition = edition_name.map(str::to_string);
        current.kind = kind.to_string();
        current.number = number.or(current.number);
        current.id = Some(entry_id.to_string());

        cbz_writer::replace_sidecar(file, ENTRY_JSON, &sidecars::write(&current)?)
    }

    fn places(&self, edition_id: &str) -> Result<Option<Places>> {
        self.db.read(|cx| {
            cx.query_one(
                "SELECT e.path AS edition, e.implicit, e.name AS edition_name,
                        w.path AS work, w.name AS work_name
                 FROM edition e JOIN work w ON w.id = e.work_id WHERE e.id = ?1",
                [edition_id],
                |r| {
                    Ok(Places {
                        edition: PathBuf::from(r.get::<_, String>(0)?),
                        implicit: r.get::<_, i64>(1)? == 1,
                        edition_name: r.get(2)?,
                        work: PathBuf::from(r.get::<_, String>(3)?),
                        work_name: r.get(4)?,
                    })
                },
            )
        })
    }
}

/// What a file says about itself: entry.json if it has one, ComicInfo otherwise.
pub fn read_entry_json(file: &Path) -> Option<EntryJson> {
    let content = cbz::read(file, false).ok()?;
    if let Some(own) = content.sidecar(ENTRY_JSON).and_then(sidecars::read) {
        return Some(own);
    }
    content
        .sidecar(legacy_comic_info::ENTRY_NAME)
        .and_then(legacy_comic_info::read)
        .map(|l| l.entry)
}

/// Reads a sidecar, changes it, writes it back.
///
/// A file that cannot be parsed is replaced rather than refused: the alternative is an edit
/// that silently does nothing because of a stray comma somewhere in a file nobody is
/// looking at.
fn merge<T>(file: &Path, transform: impl FnOnce(T) -> T) -> Result<()>
where
    T: Default + serde::de::DeserializeOwned + serde::Serialize,
{
    let current: T = std::fs::read(file)
        .ok()
        .and_then(|bytes| sidecars::read(&bytes))
        .unwrap_or_default();
    let parent = file
        .parent()
        .ok_or_else(|| anyhow!("{} has no folder", file.display()))?;
    std::fs::create_dir_all(parent)?;
    // Beside, then renamed: a scan reading this file mid-write would read a prefix,
    // fail to parse it, and report every field in it as missing.
    crate::store::files::write_whole(file, &sidecars::write(&transform(current))?)?;
    tracing::info!(file = %file.display(), "record written");
    Ok(())
}
