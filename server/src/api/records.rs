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
use crate::store::text::tag_case;
use crate::store::Db;

/// A field a patch may leave alone (absent), set (a value) or take away (`null`).
///
/// `Option<Option<T>>` and not `Option<T>`: serde reads an absent field and a `null` as the
/// same `None`, so a field once written could be changed and never emptied — a wrong ISBN
/// stayed on the volume until somebody edited the archive by hand.
pub type Clear<T> = Option<Option<T>>;

fn clearable<'de, D, T>(from: D) -> Result<Clear<T>, D::Error>
where
    D: serde::Deserializer<'de>,
    T: Deserialize<'de>,
{
    Option::<T>::deserialize(from).map(Some)
}

/// What a field becomes: itself when the patch did not mention it, whatever the patch said
/// otherwise — including nothing at all.
fn keep<T: Clone>(given: &Clear<T>, current: Option<T>) -> Option<T> {
    match given {
        None => current,
        Some(value) => value.clone(),
    }
}

#[derive(Debug, Default, Deserialize)]
#[serde(default, rename_all = "camelCase")]
pub struct SeriesPatch {
    pub name: Option<String>,
    #[serde(deserialize_with = "clearable")]
    pub title: Clear<String>,
    #[serde(deserialize_with = "clearable")]
    pub medium: Clear<String>,
    /// The former singular. `authors` is what a client should send; this stays readable for
    /// one that has not been rebuilt.
    #[serde(deserialize_with = "clearable")]
    pub author: Clear<String>,
    pub authors: Option<Vec<String>>,
    pub artists: Option<Vec<String>>,
    #[serde(deserialize_with = "clearable")]
    pub status: Clear<String>,
    #[serde(deserialize_with = "clearable")]
    pub reading_direction: Clear<String>,
    pub genres: Option<Vec<String>>,
    pub tags: Option<Vec<String>>,
    #[serde(deserialize_with = "clearable")]
    pub age_rating: Clear<String>,
    #[serde(deserialize_with = "clearable")]
    pub summary: Clear<String>,
    #[serde(deserialize_with = "clearable")]
    pub publisher: Clear<String>,
    #[serde(deserialize_with = "clearable")]
    pub collection: Clear<String>,
    #[serde(deserialize_with = "clearable")]
    pub volume_count: Clear<i32>,
    #[serde(deserialize_with = "clearable")]
    pub format: Clear<String>,
    #[serde(deserialize_with = "clearable")]
    pub language: Clear<String>,
    #[serde(deserialize_with = "clearable")]
    pub colour: Clear<bool>,
    /// The number of the volume whose cover is the series'; `null` gives the choice back to
    /// `cover.jpg`, then to the first volume.
    #[serde(deserialize_with = "clearable")]
    pub cover_volume: Clear<f64>,
    /// What *this printing* is, in its own `edition.json`; `null` hands the description back
    /// to the work's. An edition with no folder of its own has none to keep it in.
    #[serde(deserialize_with = "clearable")]
    pub edition_summary: Clear<String>,
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
            || self.cover_volume.is_some()
            || self.edition_summary.is_some()
    }
}

/// The three enum fields of a patch, checked against the contract's vocabulary.
///
/// Together rather than as three loose `Option<String>` because at a call site they are one
/// interchangeable type: a medium handed to the parameter meant for a status compiles, and
/// writes a medium into the status of a sidecar. Named fields cost nothing and that mistake
/// cannot be made.
struct Spelled {
    medium: Clear<String>,
    status: Clear<String>,
    reading_direction: Clear<String>,
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
) -> Result<Option<(sidecars::Document, WorkJson)>> {
    if !patch.touches_work() && !gets_the_edition {
        return Ok(None);
    }
    let document = current(file)?;
    let mut it: WorkJson = document.read();
    it.leaf = Some(FORMAT_VERSION);
    if patch.touches_work() {
        it.title = keep(&patch.title, it.title);
        it.medium = keep(&words.medium, it.medium);
        it.author = keep(&patch.author, it.author);
        if let Some(authors) = &patch.authors {
            it.authors = authors.clone();
        }
        if let Some(artists) = &patch.artists {
            it.artists = artists.clone();
        }
        it.status = keep(&words.status, it.status);
        it.reading_direction = keep(&words.reading_direction, it.reading_direction);
        if let Some(genres) = &patch.genres {
            it.genres = genres.clone();
        }
        if let Some(tags) = &patch.tags {
            // In capitals in the file itself, and once each: the sidecar is the truth, and
            // a spelling the index folds but the file keeps would come back on a rebuild.
            let mut seen = std::collections::HashSet::new();
            it.tags = tags
                .iter()
                .map(|t| tag_case(t))
                .filter(|t| !t.is_empty() && seen.insert(crate::store::text::search_key(t)))
                .collect();
        }
        it.age_rating = keep(&patch.age_rating, it.age_rating);
        it.summary = keep(&patch.summary, it.summary);
    }
    // An implicit edition has no folder of its own: its fields go down into work.json.
    // Dropping an edition.json there would flip how the folder is classified — it would stop
    // being a work and become an edition.
    if gets_the_edition {
        it.publisher = keep(&patch.publisher, it.publisher);
        it.collection = keep(&patch.collection, it.collection);
        it.volume_count = keep(&patch.volume_count, it.volume_count);
        it.format = keep(&patch.format, it.format);
        it.language = keep(&patch.language, it.language);
        it.status = keep(&words.status, it.status);
        it.colour = keep(&patch.colour, it.colour);
        it.cover_volume = keep(&patch.cover_volume, it.cover_volume);
    }
    Ok(Some((document, it)))
}

/// What `edition.json` is to become, or `None` when the patch does not reach it — including
/// the implicit edition, which has no folder to keep one in.
fn edition_document(
    file: &Path,
    patch: &SeriesPatch,
    words: &Spelled,
    implicit: bool,
) -> Result<Option<(sidecars::Document, EditionJson)>> {
    if implicit || !patch.touches_edition() {
        return Ok(None);
    }
    let document = current(file)?;
    let mut it: EditionJson = document.read();
    it.leaf = Some(FORMAT_VERSION);
    it.name = patch.name.clone().or(it.name);
    it.publisher = keep(&patch.publisher, it.publisher);
    it.collection = keep(&patch.collection, it.collection);
    it.status = keep(&words.status, it.status);
    it.volume_count = keep(&patch.volume_count, it.volume_count);
    it.format = keep(&patch.format, it.format);
    it.language = keep(&patch.language, it.language);
    it.colour = keep(&patch.colour, it.colour);
    it.cover_volume = keep(&patch.cover_volume, it.cover_volume);
    it.summary = keep(&patch.edition_summary, it.summary);
    Ok(Some((document, it)))
}

#[derive(Debug, Default, Deserialize)]
#[serde(default, rename_all = "camelCase")]
pub struct EntryPatch {
    /// Never taken away: a volume without a number is not one the edition can place.
    pub number: Option<f64>,
    #[serde(deserialize_with = "clearable")]
    pub title: Clear<String>,
    #[serde(deserialize_with = "clearable")]
    pub isbn: Clear<String>,
    #[serde(deserialize_with = "clearable")]
    pub published_on: Clear<String>,
    #[serde(deserialize_with = "clearable")]
    pub summary: Clear<String>,
    /// The page shown as the cover, from 0 like `/pages/{number}`; `null` goes back to an
    /// image beside the archive, otherwise page 0.
    #[serde(deserialize_with = "clearable")]
    pub cover_page: Clear<i32>,
    /// Who wrote this volume. Absent leaves it; a list replaces the volume's own; an empty
    /// list takes the volume's own away, and its series' default speaks for it again.
    pub authors: Option<Vec<String>>,
    /// Who drew it, on the same terms.
    pub artists: Option<Vec<String>>,
}

pub struct Records<'a> {
    db: &'a Db,
    /// Where imports land, so a volume converted on its way in is followed when Leaf
    /// rewrites it. `None` for a caller that writes only files no import put there.
    library: Option<&'a Path>,
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
        Records { db, library: None }
    }

    /// The same, for a caller that may rewrite a volume an import converted: every route
    /// that edits or stamps an archive of the library says where the library is.
    pub fn in_library(self, library: &'a Path) -> Self {
        Records {
            library: Some(library),
            ..self
        }
    }

    /// Writes `entry.json` into a volume, and follows the origin it was converted from.
    ///
    /// Leaf's own rewrite changes the archive's size, and the next import compared it to the
    /// size it was kept at: a title corrected in the application read as an edit made by
    /// hand, and the import offered to overwrite it. A failure to follow is said and not
    /// raised — the volume is written, and a stale origin only makes the next import ask.
    fn write_entry_json(&self, file: &Path, entry: &EntryJson) -> Result<()> {
        let was = std::fs::metadata(file).map(|m| m.len()).ok();
        cbz_writer::replace_sidecar(file, ENTRY_JSON, &sidecars::write(entry)?)?;
        self.follow_origin(file, was);
        Ok(())
    }

    /// See [`Self::write_entry_json`]: `was` is the archive's size before Leaf rewrote it.
    fn follow_origin(&self, file: &Path, was: Option<u64>) {
        let (Some(library), Some(was)) = (self.library, was) else {
            return;
        };
        let Some(key) = crate::api::bulk_import::origin_key_of(library, file) else {
            return;
        };
        let followed = std::fs::metadata(file)
            .map_err(anyhow::Error::from)
            .and_then(|m| crate::store::origins::follow(self.db, &key, was, m.len()));
        if let Err(e) = followed {
            tracing::warn!(file = %file.display(), error = %e, "could not follow the origin");
        }
    }

    /// Makes `image` the volume's page `000`, the cover the format names: replacing the one
    /// it has, or — a volume numbered from `001` has none — adding it before the first page.
    ///
    /// Adding one moves every page a place down, and everything that counts pages moves with
    /// it **in the same operation**: each profile's place in the volume, and the first page of
    /// every chapter and the cover page `entry.json` names. A chosen `coverPage` is cleared,
    /// since it would go on outranking the page 0 just written. Chapters a volume only
    /// declares in its ComicInfo are moved too: they are read the way a patch reads them, and
    /// written into `entry.json`, which wins over the ComicInfo from then on.
    ///
    /// The new archive is written beside the old one and read back — its first page must be
    /// the image, and it must hold the pages expected — before it replaces the volume. The
    /// cover it replaces is **not kept**: the archive is the volume, and it holds the cover
    /// now. `false` when there is no such volume.
    pub fn set_cover_page(&self, entry_id: &str, extension: &str, image: &[u8]) -> Result<bool> {
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
        let held = cbz::read(&file, false)?.pages;
        let Some(first) = held.first() else {
            return Err(crate::api::invalid("the volume has no pages"));
        };
        // Where the page goes: over the one it replaces, in its folder and under the new
        // extension, or at the root of an archive that has none.
        let replaced = crate::archive::page_number(&first.name) == Some(0);
        let folder = if replaced {
            first.name.rsplit_once('/').map(|(f, _)| f)
        } else {
            None
        };
        // Named as its siblings are: a file numbered `01`, `02`… gets `00`, one numbered `1`,
        // `2`… gets `0`, and anything else the three digits a library writes.
        let width = crate::archive::page_stem(&first.name)
            .filter(|stem| crate::archive::page_number(stem).is_some())
            .map_or(3, str::len);
        let stem = format!("{:0width$}", 0);
        let name = match folder {
            Some(folder) => format!("{folder}/{stem}.{extension}"),
            None => format!("{stem}.{extension}"),
        };
        let shifted = !replaced;

        let mut current = read_entry_json(&file).unwrap_or_default();
        let changes_json = shifted || current.cover_page.is_some();
        let json = if changes_json {
            current.leaf = Some(FORMAT_VERSION);
            current.work = Some(where_.work_name);
            current.edition = where_.edition_name;
            current.id = Some(entry_id.to_string());
            current.cover_page = None;
            if shifted {
                for chapter in &mut current.chapters {
                    chapter.start_page = chapter.start_page.map(|p| p + 1);
                }
            }
            Some(sidecars::write(&current)?)
        } else {
            None
        };

        let mut add: Vec<(&str, &[u8])> = vec![(name.as_str(), image)];
        if let Some(json) = &json {
            add.push((ENTRY_JSON, json));
        }
        let remove: Vec<&str> = if replaced && first.name != name {
            vec![first.name.as_str()]
        } else {
            Vec::new()
        };
        let was = std::fs::metadata(&file).map(|m| m.len()).ok();
        let temporary = crate::store::files::beside(&file, "leaf-tmp");
        cbz_writer::rewrite_into(&file, &temporary, &add, &remove)?;
        // Read back before it replaces anything: a page that does not sort first would be a
        // cover nobody sees, and the pages shifted for it would be shifted for nothing.
        let written = cbz::read(&temporary, false).map(|c| c.pages);
        let expected = held.len() + usize::from(shifted);
        let sound = written.as_ref().is_ok_and(|pages| {
            pages.len() == expected && pages.first().is_some_and(|p| p.name == name)
        });
        if !sound {
            let _ = std::fs::remove_file(&temporary);
            return Err(crate::api::invalid(
                "the volume cannot be given that cover: its pages would not read in order",
            ));
        }
        std::fs::rename(&temporary, &file)
            .map_err(|e| anyhow!("replacing {}: {e}", file.display()))?;
        tracing::info!(file = %file.display(), "cover page written");
        if shifted {
            self.db.write(|cx| {
                cx.execute(
                    "UPDATE progress SET page = page + 1 WHERE entry_id = ?1",
                    [entry_id],
                )?;
                Ok(())
            })?;
        }
        self.follow_origin(&file, was);
        Ok(true)
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

        if where_.implicit && patch.edition_summary.is_some() {
            return Err(crate::api::invalid(
                "this edition has no folder of its own, so it has no description of its own — \
                 the work's is the one it shows",
            ));
        }

        let words = Spelled::of(patch)?;

        // Against the index, like a volume's cover page: a number the edition does not hold
        // would be written down, then ignored at every request for the cover.
        if let Some(Some(number)) = patch.cover_volume {
            let held = self.db.read(|cx| {
                cx.query_one(
                    "SELECT 1 FROM entry WHERE edition_id = ?1 AND volume_number = ?2",
                    rusqlite::params![edition_id, number],
                    |r| r.get::<_, i64>(0),
                )
            })?;
            if held.is_none() {
                return Err(crate::api::invalid(format!(
                    "the series has no volume {number}"
                )));
            }
        }

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

        if let Some((document, work)) = work {
            put(&work_file, &document, &work)?;
        }
        if let Some((document, edition)) = edition {
            put(&edition_file, &document, &edition)?;
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

        // Before the archive is touched. Two volumes of an edition holding the same number
        // are one volume twice, and every list built on `number` would show it.
        if let Some(number) = patch.number {
            let taken = self.db.read(|cx| {
                cx.query_one(
                    "SELECT 1 FROM entry WHERE edition_id = ?1 AND volume_number = ?2 AND id <> ?3",
                    rusqlite::params![edition_id, number, entry_id],
                    |r| r.get::<_, i64>(0),
                )
            })?;
            if taken.is_some() {
                return Err(crate::api::invalid(
                    "another file of this edition already has that number",
                ));
            }
        }

        // Against the index, which holds the pages the archive had when it was last read:
        // opening a second time to count them would cost an archive read for a number
        // already in hand.
        if let Some(Some(page)) = patch.cover_page {
            let held = self.db.read(|cx| {
                cx.query_one(
                    "SELECT 1 FROM page WHERE entry_id = ?1 AND number = ?2",
                    rusqlite::params![entry_id, page],
                    |r| r.get::<_, i64>(0),
                )
            })?;
            if held.is_none() {
                return Err(crate::api::invalid(format!(
                    "the volume has no page {page}; pages are counted from 0"
                )));
            }
        }

        let file = PathBuf::from(file);
        let mut current = read_entry_json(&file).unwrap_or_default();
        current.leaf = Some(FORMAT_VERSION);
        current.work = Some(where_.work_name);
        current.edition = where_.edition_name;
        current.id = Some(entry_id.to_string());
        current.number = patch.number.or(current.number);
        current.title = keep(&patch.title, current.title);
        current.isbn = keep(&patch.isbn, current.isbn);
        current.published_on = keep(&patch.published_on, current.published_on);
        current.summary = keep(&patch.summary, current.summary);
        current.cover_page = keep(&patch.cover_page, current.cover_page);
        if let Some(authors) = &patch.authors {
            current.authors = people(authors);
        }
        if let Some(artists) = &patch.artists {
            current.artists = people(artists);
        }

        self.write_entry_json(&file, &current)?;
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

        self.write_entry_json(file, &current)
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
    given: &Clear<String>,
    known: fn(&str) -> Option<&'static str>,
    what: &str,
    vocabulary: &[&str],
) -> Result<Clear<String>> {
    let Some(word) = given else { return Ok(None) };
    let Some(word) = word else {
        return Ok(Some(None));
    };
    match known(word) {
        Some(it) => Ok(Some(Some(it.to_string()))),
        None => Err(crate::api::invalid(format!(
            "\"{word}\" is not {what} — {}",
            vocabulary.join(", ")
        ))),
    }
}

/// What a file says about itself: entry.json if it has one, ComicInfo otherwise.
/// Names as a person typed them, without the blanks a form leaves behind: a name of spaces is
/// nobody, and writing it would credit the volume with nobody instead of letting its series
/// speak.
fn people(names: &[String]) -> Vec<String> {
    names
        .iter()
        .map(|n| n.trim().to_string())
        .filter(|n| !n.is_empty())
        .collect()
}

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
    let document = current(file)?;
    put(file, &document, &transform(document.read()))
}

/// What the sidecar holds now, whole, or an empty document when there is no file.
///
/// Split out of [`merge`] so a caller writing two files can read both first: an edit refused
/// halfway leaves one of them changed, and that is not a shape any answer can describe. See
/// `patch_series`.
///
/// The whole document and not a typed view of it, because the typed view is what threw away
/// every field this version has not been taught — see [`sidecars::Document`].
fn current(file: &Path) -> Result<sidecars::Document> {
    match std::fs::read(file) {
        Ok(bytes) => Ok(sidecars::Document::of(&bytes)),
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => Ok(sidecars::Document::default()),
        Err(e) => Err(anyhow::Error::new(e).context(format!("reading {}", file.display()))),
    }
}

fn put<T>(file: &Path, document: &sidecars::Document, value: &T) -> Result<()>
where
    T: Default + serde::de::DeserializeOwned + serde::Serialize,
{
    let parent = file
        .parent()
        .ok_or_else(|| anyhow!("{} has no folder", file.display()))?;
    std::fs::create_dir_all(parent)?;
    // Beside, then renamed: a scan reading this file mid-write would read a prefix,
    // fail to parse it, and report every field in it as missing.
    crate::store::files::write_whole(file, &document.written(value)?)?;
    tracing::info!(file = %file.display(), "record written");
    Ok(())
}
