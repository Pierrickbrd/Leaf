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
    /// The former singular. `authors` is what a client should send; this stays readable for
    /// one that has not been rebuilt.
    pub author: Option<String>,
    pub authors: Option<Vec<String>>,
    pub artists: Option<Vec<String>>,
    pub status: Option<String>,
    pub reading_direction: Option<String>,
    pub genres: Option<Vec<String>>,
    pub tags: Option<Vec<String>>,
    pub age_rating: Option<String>,
    pub summary: Option<String>,
    pub publisher: Option<String>,
    pub collection: Option<String>,
    pub volume_count: Option<i32>,
    pub format: Option<String>,
    pub language: Option<String>,
    pub colour: Option<bool>,
}

impl SeriesPatch {
    /// What describes the work goes into work.json, whichever edition was targeted.
    fn touches_work(&self) -> bool {
        self.title.is_some()
            || self.medium.is_some()
            || self.author.is_some()
            || self.authors.is_some()
            || self.artists.is_some()
            || self.status.is_some()
            || self.reading_direction.is_some()
            || self.genres.is_some()
            || self.tags.is_some()
            || self.age_rating.is_some()
            || self.summary.is_some()
    }

    fn touches_edition(&self) -> bool {
        self.name.is_some()
            || self.publisher.is_some()
            || self.collection.is_some()
            || self.format.is_some()
            || self.language.is_some()
            || self.status.is_some()
            || self.volume_count.is_some()
            || self.colour.is_some()
    }
}

/// The three enum fields of a patch, checked against the contract's vocabulary.
///
/// Together rather than as three loose `Option<String>` because at a call site they are one
/// interchangeable type: a medium handed to the parameter meant for a status compiles, and
/// writes a medium into the status of a sidecar. Named fields cost nothing and that mistake
/// cannot be made.
struct Spelled {
    medium: Option<String>,
    status: Option<String>,
    reading_direction: Option<String>,
}

impl Spelled {
    /// Refused in the caller's own answer, rather than written down and met again by the
    /// scanner — which is what `set_arcs` does for units, and what this route did not do for
    /// the three enums beside them. See [`sidecars::medium`] for what a word the contract has
    /// not got costs once it is on the disk. Read before anything is written, so a patch
    /// carrying a good field and a bad one does not land half of itself.
    fn of(patch: &SeriesPatch) -> Result<Self> {
        Ok(Spelled {
            medium: spelled(
                &patch.medium,
                sidecars::medium,
                "a medium",
                &sidecars::MEDIA,
            )?,
            status: spelled(
                &patch.status,
                sidecars::status,
                "a status",
                &sidecars::STATUSES,
            )?,
            reading_direction: spelled(
                &patch.reading_direction,
                sidecars::reading_direction,
                "a reading direction",
                &sidecars::READING_DIRECTIONS,
            )?,
        })
    }
}

/// What `work.json` is to become, or `None` when nothing in the patch reaches it.
///
/// Built and returned rather than written, so that [`Records::patch_series`] stays the one
/// place that decides the order the disk is touched in.
fn work_document(
    file: &Path,
    patch: &SeriesPatch,
    words: &Spelled,
    gets_the_edition: bool,
) -> Result<Option<WorkJson>> {
    if !patch.touches_work() && !gets_the_edition {
        return Ok(None);
    }
    let mut it: WorkJson = current(file)?;
    it.leaf = Some(FORMAT_VERSION);
    if patch.touches_work() {
        it.title = patch.title.clone().or(it.title);
        it.medium = words.medium.clone().or(it.medium);
        it.author = patch.author.clone().or(it.author);
        if let Some(authors) = &patch.authors {
            it.authors = authors.clone();
        }
        if let Some(artists) = &patch.artists {
            it.artists = artists.clone();
        }
        it.status = words.status.clone().or(it.status);
        it.reading_direction = words.reading_direction.clone().or(it.reading_direction);
        if let Some(genres) = &patch.genres {
            it.genres = genres.clone();
        }
        if let Some(tags) = &patch.tags {
            it.tags = tags.clone();
        }
        it.age_rating = patch.age_rating.clone().or(it.age_rating);
        it.summary = patch.summary.clone().or(it.summary);
    }
    // An implicit edition has no folder of its own: its fields go down into work.json.
    // Dropping an edition.json there would flip how the folder is classified — it would stop
    // being a work and become an edition.
    if gets_the_edition {
        it.publisher = patch.publisher.clone().or(it.publisher);
        it.collection = patch.collection.clone().or(it.collection);
        it.volume_count = patch.volume_count.or(it.volume_count);
        it.format = patch.format.clone().or(it.format);
        it.language = patch.language.clone().or(it.language);
        it.status = words.status.clone().or(it.status);
        it.colour = patch.colour.or(it.colour);
    }
    Ok(Some(it))
}

/// What `edition.json` is to become, or `None` when the patch does not reach it — including
/// the implicit edition, which has no folder to keep one in.
fn edition_document(
    file: &Path,
    patch: &SeriesPatch,
    words: &Spelled,
    implicit: bool,
) -> Result<Option<EditionJson>> {
    if implicit || !patch.touches_edition() {
        return Ok(None);
    }
    let mut it: EditionJson = current(file)?;
    it.leaf = Some(FORMAT_VERSION);
    it.name = patch.name.clone().or(it.name);
    it.publisher = patch.publisher.clone().or(it.publisher);
    it.collection = patch.collection.clone().or(it.collection);
    it.status = words.status.clone().or(it.status);
    it.volume_count = patch.volume_count.or(it.volume_count);
    it.format = patch.format.clone().or(it.format);
    it.language = patch.language.clone().or(it.language);
    it.colour = patch.colour.or(it.colour);
    Ok(Some(it))
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

        // An edition with no folder of its own has nowhere to keep a name, and used to be
        // told so by nobody: `touches_edition` counts `name`, so the implicit branch below
        // ran — and that branch writes every edition field except that one. The request was
        // answered 200 with the series unchanged, which is the shape of an afternoon spent
        // renaming something that never renamed.
        //
        // Before anything is written, not inside the branch: a patch carrying a title as
        // well would otherwise have had half of it applied.
        if where_.implicit && patch.name.is_some() {
            return Err(crate::api::invalid(
                "this edition has no folder of its own, so it has no name to hold — give it \
                 one by putting its volumes in a folder with an edition.json in it",
            ));
        }

        let words = Spelled::of(patch)?;

        let work_file = where_.work.join(layout::WORK_FILE);
        let edition_file = where_.edition.join(layout::EDITION_FILE);
        let work_gets_the_edition = where_.implicit && patch.touches_edition();

        // **Both files are read, and both documents built, before either is written.** The
        // guard above is the same rule for the one case that had it: a patch touching the
        // work and the edition at once used to write work.json first, and if reading
        // edition.json then failed — a mode, a symlink loop, a device error, exactly the
        // class `merge` was taught to refuse rather than swallow — the answer was a 500 with
        // half the patch on disk and nothing saying which half.
        //
        // Two files still cannot be written atomically without a journal, so a failure
        // *between* the two writes below splits the patch. What this closes is the far
        // likelier half: every way the request can be refused now happens with the disk
        // untouched.
        let work = work_document(&work_file, patch, &words, work_gets_the_edition)?;
        let edition = edition_document(&edition_file, patch, &words, where_.implicit)?;

        if let Some(work) = work {
            put(&work_file, &work)?;
        }
        if let Some(edition) = edition {
            put(&edition_file, &edition)?;
        }
        Ok(true)
    }

    pub fn set_arcs(&self, edition_id: &str, arcs: Vec<ArcJson>) -> Result<bool> {
        // The id first, and only then the body. Reading the units first answered a request
        // naming an edition that does not exist with a 400 about its units — telling a caller
        // its shape was read before its id, where every other route taking an id says 404.
        // `patch_series` puts its own guard after this call for the same reason.
        let Some(where_) = self.places(edition_id)? else {
            return Ok(false);
        };

        // Refused here, in the caller's own answer, rather than written down and met again
        // by the scanner — which meets it inside the transaction holding a whole shelf.
        let arcs = arcs
            .into_iter()
            .map(|mut arc| match sidecars::arc_unit(&arc.unit) {
                Some(unit) => {
                    // Written back in the contract's spelling, so the file says what the
                    // format says even when the request said `volume`.
                    arc.unit = unit.to_string();
                    Ok(arc)
                }
                None => Err(crate::api::invalid(format!(
                    "\"{}\" is not a unit an arc is counted in — CHAPTER or VOLUME",
                    arc.unit
                ))),
            })
            .collect::<Result<Vec<ArcJson>>>()?;

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

/// One field of a patch, in the contract's spelling, or a refusal that names the vocabulary.
///
/// The words are in the message because a caller told only that its word was wrong has to go
/// and find the contract; a refusal listing what was allowed *is* the contract, at the one
/// moment somebody is reading it.
fn spelled(
    given: &Option<String>,
    known: fn(&str) -> Option<&'static str>,
    what: &str,
    vocabulary: &[&str],
) -> Result<Option<String>> {
    let Some(word) = given else { return Ok(None) };
    match known(word) {
        Some(it) => Ok(Some(it.to_string())),
        None => Err(crate::api::invalid(format!(
            "\"{word}\" is not {what} — {}",
            vocabulary.join(", ")
        ))),
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
/// A file that cannot be **parsed** is replaced rather than refused: the alternative is an
/// edit that silently does nothing because of a stray comma somewhere in a file nobody is
/// looking at.
///
/// A file that cannot be **read** is a different thing, and used to be the same one. `.ok()`
/// swallowed a permission error and a disk error exactly as it swallowed "there is no such
/// file": the merge then started from a default, and a patch carrying one field replaced a
/// work.json holding twelve. Absent is a reason to write a new one; unreadable is a reason
/// to stop, because what cannot be read is still there and is about to be overwritten.
fn merge<T>(file: &Path, transform: impl FnOnce(T) -> T) -> Result<()>
where
    T: Default + serde::de::DeserializeOwned + serde::Serialize,
{
    put(file, &transform(current(file)?))
}

/// What the sidecar says now, or its default when there is none.
///
/// Split out of [`merge`] so a caller writing two files can read both first: an edit refused
/// halfway leaves one of them changed, and that is not a shape any answer can describe. See
/// `patch_series`.
fn current<T>(file: &Path) -> Result<T>
where
    T: Default + serde::de::DeserializeOwned,
{
    match std::fs::read(file) {
        Ok(bytes) => Ok(sidecars::read(&bytes).unwrap_or_default()),
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => Ok(T::default()),
        Err(e) => Err(anyhow::Error::new(e).context(format!("reading {}", file.display()))),
    }
}

fn put<T: serde::Serialize>(file: &Path, value: &T) -> Result<()> {
    let parent = file
        .parent()
        .ok_or_else(|| anyhow!("{} has no folder", file.display()))?;
    std::fs::create_dir_all(parent)?;
    // Beside, then renamed: a scan reading this file mid-write would read a prefix,
    // fail to parse it, and report every field in it as missing.
    crate::store::files::write_whole(file, &sidecars::write(value)?)?;
    tracing::info!(file = %file.display(), "record written");
    Ok(())
}
