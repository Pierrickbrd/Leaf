//! Reading the library off the disk and into the index.
//!
//! The index is derived, always: delete it and a scan rebuilds it. The one exception is
//! progress, which lives nowhere else — which is why nothing here ever touches that table.
//!
//! Two properties are worth stating before the code, because most of what follows exists to
//! keep them:
//!
//!  - **a scan is one transaction.** Either the library is coherent afterwards or nothing
//!    changed. Browsing during a scan shows what was there before, never half of what is
//!    being built.
//!  - **an unchanged file is not read.** Size and modification time decide, and when not one
//!    entry in an edition has moved, its chapters are not recomputed either. That is the
//!    difference between a rescan of 419 seconds and one of 1.5.

use std::collections::{HashMap, HashSet};
use std::path::{Path, PathBuf};

use anyhow::{Context, Result};
use sha2::{Digest, Sha256};

use crate::archive::cbz;
use crate::metadata::label;
use crate::metadata::legacy_comic_info::{self as comic_info, LegacyRead};
use crate::metadata::sidecars::{
    ArcJson, ChapterJson, EditionJson, EntryJson, UniverseJson, WorkJson,
};
use crate::scan::checks;
use crate::scan::covers;
use crate::scan::layout::{self, Kind};
use crate::scan::report::ScanReport;
use crate::store::text::search_key;
use crate::store::{Cx, Db};

pub const ENTRY_JSON: &str = "entry.json";

pub struct Scanner {
    db: std::sync::Arc<Db>,
    all_dimensions: bool,
}

/// What the scan met, so that what it did not meet can be removed.
#[derive(Default)]
struct Seen {
    universes: HashSet<String>,
    works: HashSet<String>,
    editions: HashSet<String>,
    entries: HashSet<String>,
}

/// The fields a work hands down to an edition that does not declare its own.
#[derive(Default, Clone)]
struct WorkDefaults {
    status: Option<String>,
    publisher: Option<String>,
    volume_count: Option<i32>,
    format: Option<String>,
    language: Option<String>,
    medium: Option<String>,
    reading_direction: Option<String>,
    chapter_label: Option<String>,
    arcs: Vec<ArcJson>,
    work: Option<WorkJson>,
    work_name: String,
    universe_name: Option<String>,
}

/// What the files turned out to know that `work.json` did not say.
#[derive(Default, Clone)]
struct Inherited {
    author: Option<String>,
    reading_direction: Option<String>,
    genres: Vec<String>,
}

/// One chapter, as it stands before its position on the edition's scale is known.
#[derive(Debug, Clone)]
pub struct ChapterDraft {
    pub raw: String,
    pub label: String,
    pub number: Option<f64>,
    pub title: Option<String>,
    pub kind: &'static str,
    pub start_page: Option<i32>,
    /// What it comes after, when it has no number of its own.
    pub after: Option<f64>,
    /// Which volume its content belongs to.
    pub volume: Option<f64>,
}

struct ReadEntry {
    id: String,
    path: PathBuf,
    kind: &'static str,
    size: i64,
    modified_at: i64,
    volume_number: Option<f64>,
    title: Option<String>,
    isbn: Option<String>,
    published_on: Option<String>,
    summary: Option<String>,
    chapters: Vec<ChapterDraft>,
    /// `None` when the entry was unchanged and therefore not opened.
    pages: Option<Vec<cbz::ArchivePage>>,
    unchanged: bool,
    file_order: usize,
    cover_file: Option<String>,
    legacy_arc: Option<String>,
    inherited: Option<Inherited>,
    /// What the file says about itself, kept so the checks can compare it with where it
    /// actually sits. Nothing downstream reads the index from it.
    declared: Option<EntryJson>,
}

impl ReadEntry {
    /// Where the entry sits in its edition: the **lowest** of its chapters' places,
    /// otherwise the volume number, otherwise the order the files came in.
    ///
    /// The lowest, not the first declared. Nothing requires the array in `entry.json` to be
    /// in order, and a file whose chapters read 41 then 40 begins at 40 — filed at 41, it
    /// would sit after an entry that starts at 40.5 while containing it. When the array is
    /// in order, which is every ordinary case, the two are the same value.
    fn sort_key(&self) -> f64 {
        self.chapters
            .iter()
            .filter_map(|c| c.number.or(c.after))
            .min_by(|a, b| a.partial_cmp(b).unwrap_or(std::cmp::Ordering::Equal))
            .or(self.volume_number)
            .unwrap_or(self.file_order as f64)
    }
}

impl Scanner {
    pub fn new(db: std::sync::Arc<Db>, all_dimensions: bool) -> Self {
        Scanner { db, all_dimensions }
    }

    /// The whole library, one transaction per shelf folder.
    ///
    /// Not one transaction for the lot. There is a single writer, so a scan that held it
    /// from beginning to end blocked **every** other write for as long as the scan took —
    /// measured at thirty seconds on a cold disk, during which a phone recording where it
    /// stopped reading simply waited. One transaction per top-level folder bounds that to
    /// one shelf.
    ///
    /// What it gives up is that a reader browsing during a scan can now see one folder
    /// updated and the next not yet. That is worth giving up: the index is derived from the
    /// files, a scan is idempotent, and the alternative was a real stall on the one write a
    /// reader makes constantly. It also means a scan that dies keeps what it had done rather
    /// than losing all of it.
    ///
    /// The prune is its own transaction at the end, because it needs the whole of `seen`.
    pub fn scan(&self, roots: &[PathBuf]) -> Result<ScanReport> {
        let mut report = ScanReport::default();
        // Built fresh every run. Kept between runs, a deletion became undetectable: the ids
        // met on the first scan stayed in the set, so pruning never removed anything the
        // second time round.
        let mut seen = Seen::default();

        let mut complete = true;
        for root in roots {
            tracing::info!(root = %root.display(), "Scanning");
            for folder in layout::sub_folders(root) {
                // One shelf that cannot be read must not stop the two hundred behind it.
                // A folder whose permissions changed, a mount that went away mid-scan: the
                // rest of the library is still there and still worth indexing.
                if let Err(e) = self.db.write(|cx| {
                    self.visit(
                        cx,
                        &folder,
                        None,
                        layout::MAX_SHELVES,
                        &mut seen,
                        &mut report,
                    )
                }) {
                    tracing::warn!(folder = %folder.display(), error = %e, "shelf not read");
                    report.errors.push(format!("{}: {e:#}", name_of(&folder)));
                    complete = false;
                }
            }
        }

        // Pruning removes whatever was not met. A shelf that failed was not met either, so
        // running it after a partial sweep would delete a series because its folder happened
        // to be unreadable for a moment — and progress is the one thing a rescan cannot
        // bring back.
        if complete {
            self.db.write(|cx| self.prune(cx, &seen))?;
        } else {
            tracing::warn!("part of the library could not be read: nothing removed from the index");
        }
        Ok(report)
    }

    /// One work, aimed at rather than swept for.
    ///
    /// Reading the whole library after one edited field is five seconds on six series and a
    /// minute on sixty.
    pub fn rescan_work(&self, work_folder: &Path) -> Result<ScanReport> {
        let mut report = ScanReport::default();
        let mut seen = Seen::default();
        let path = absolute(work_folder);

        // A universe read as a work files its works as editions. `can_be_aimed_at` is what
        // keeps that from happening, and this is the same rule stated where it belongs: on
        // the method that would do the damage.
        if layout::kind(work_folder) == (layout::Kind::Universe, true) {
            anyhow::bail!(
                "{} declares itself a universe: a rescan cannot be aimed at it",
                work_folder.display()
            );
        }

        self.db.write(|cx| {
            let universe_id: Option<String> = cx
                .query_one(
                    "SELECT universe_id FROM work WHERE path = ?1",
                    [path.as_str()],
                    |r| r.get::<_, Option<String>>(0),
                )?
                .flatten();

            if layout::holds_archives(work_folder) {
                let name = match &universe_id {
                    Some(id) => self.universe_name(cx, id)?,
                    None => None,
                };
                self.visit_work(
                    cx,
                    work_folder,
                    universe_id.as_deref(),
                    name.as_deref(),
                    &mut seen,
                    &mut report,
                )?;
                self.prune_within(cx, &path, &seen)
            } else if work_folder.exists() && layout::readable(work_folder).is_err() {
                // There, and shut. Not the same thing as gone, and the difference is a
                // series disappearing from the shelf because a permission changed.
                anyhow::bail!("{} cannot be listed", work_folder.display())
            } else {
                // The folder is gone, or holds nothing any more: drop what it left behind.
                cx.execute("DELETE FROM work WHERE path = ?1", [path.as_str()])?;
                Ok(())
            }
        })?;
        Ok(report)
    }

    // ------------------------------------------------------------------ levels

    fn visit(
        &self,
        cx: &Cx<'_>,
        folder: &Path,
        universe_id: Option<&str>,
        left: usize,
        seen: &mut Seen,
        report: &mut ScanReport,
    ) -> Result<()> {
        // Before anything reads it as empty. An unreadable folder and an empty one look
        // identical from `children`, and empty means "pruned from the index".
        layout::readable(folder).with_context(|| format!("listing {}", folder.display()))?;
        let (kind, _) = layout::kind(folder);

        if kind == Kind::Container {
            return self.visit_container(cx, folder, universe_id, left, seen, report);
        }

        // From here the model applies, and it is three floors deep: universe, work, edition.
        if !layout::holds_archives_within(folder, layout::MODEL_DEPTH) {
            // Deeper than the model has room for is worth saying, rather than being the same
            // silence arrived at by another route.
            if layout::holds_archives_within(folder, layout::MODEL_DEPTH + 3) {
                report.disregarded.push(format!(
                    "{}: its archives sit deeper than universe / work / edition, so nothing \
                     under it is read",
                    name_of(folder)
                ));
            }
            return Ok(());
        }

        if kind == Kind::Universe && universe_id.is_none() {
            return self.visit_universe(cx, folder, seen, report);
        }

        let name = match universe_id {
            Some(id) => self.universe_name(cx, id)?,
            None => None,
        };
        self.visit_work(cx, folder, universe_id, name.as_deref(), seen, report)
    }

    /// A shelf. Not a level of the model — a folder somebody made to tidy up, holding works
    /// and universes that each say what they are. Walked through, so that what is inside is
    /// judged on its own terms rather than becoming an edition of the tidying.
    fn visit_container(
        &self,
        cx: &Cx<'_>,
        folder: &Path,
        universe_id: Option<&str>,
        left: usize,
        seen: &mut Seen,
        report: &mut ScanReport,
    ) -> Result<()> {
        if left == 0 {
            report.disregarded.push(format!(
                "{}: folders nested past what is worth following, so nothing under it is \
                 read",
                name_of(folder)
            ));
            return Ok(());
        }
        for inside in layout::sub_folders(folder) {
            self.visit(cx, &inside, universe_id, left - 1, seen, report)?;
        }
        Ok(())
    }

    /// A universe and the works under it.
    fn visit_universe(
        &self,
        cx: &Cx<'_>,
        folder: &Path,
        seen: &mut Seen,
        report: &mut ScanReport,
    ) -> Result<()> {
        let id = self.record_universe(cx, folder, seen, report)?;
        let name = self.universe_name(cx, &id)?;
        for work in layout::sub_folders(folder) {
            // Universes do not nest: the model is universe, work, edition, and a fourth
            // level has nowhere to go. One below another is read as a work and its own
            // works become its editions — which is a defensible answer, and a baffling
            // one to meet without being told.
            if layout::kind(&work) == (layout::Kind::Universe, true) {
                report.disregarded.push(format!(
                    "{}/{}/universe.json — a universe cannot hold another, so this is \
                     read as a work of \"{}\"",
                    name_of(folder),
                    name_of(&work),
                    name_of(folder)
                ));
            }
            self.visit_work(cx, &work, Some(&id), name.as_deref(), seen, report)?;
        }
        Ok(())
    }

    fn visit_work(
        &self,
        cx: &Cx<'_>,
        folder: &Path,
        universe_id: Option<&str>,
        universe_name: Option<&str>,
        seen: &mut Seen,
        report: &mut ScanReport,
    ) -> Result<()> {
        layout::readable(folder).with_context(|| format!("listing {}", folder.display()))?;
        if !layout::holds_archives(folder) {
            return Ok(());
        }
        let meta: Option<WorkJson> = read_json(folder, layout::WORK_FILE);
        let id = id_of(folder, "");
        seen.works.insert(id.clone());

        cx.execute(
            "INSERT INTO work (id, universe_id, name, path, title, medium, author, status,
                               reading_direction, summary)
             VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10)
             ON CONFLICT(id) DO UPDATE SET
               universe_id=excluded.universe_id, name=excluded.name, title=excluded.title,
               medium=excluded.medium, author=excluded.author, status=excluded.status,
               reading_direction=excluded.reading_direction, summary=excluded.summary",
            rusqlite::params![
                id,
                universe_id,
                name_of(folder),
                absolute(folder),
                meta.as_ref().and_then(|m| m.title.clone()),
                meta.as_ref().and_then(|m| m.medium.clone()),
                meta.as_ref().and_then(|m| m.author.clone()),
                meta.as_ref().and_then(|m| m.status.clone()),
                meta.as_ref().and_then(|m| m.reading_direction.clone()),
                meta.as_ref().and_then(|m| m.summary.clone()),
            ],
        )?;
        report.works += 1;
        report
            .missing_required
            .extend(checks::work(&name_of(folder), meta.as_ref()));
        self.record_genres(
            cx,
            &id,
            &meta.as_ref().map(|m| m.genres.clone()).unwrap_or_default(),
        )?;

        // A single-edition work has no edition folder: its edition fields live in work.json
        // and come down here as defaults.
        let defaults = WorkDefaults {
            status: meta.as_ref().and_then(|m| m.status.clone()),
            publisher: meta.as_ref().and_then(|m| m.publisher.clone()),
            volume_count: meta.as_ref().and_then(|m| m.volume_count),
            format: meta.as_ref().and_then(|m| m.format.clone()),
            language: meta.as_ref().and_then(|m| m.language.clone()),
            medium: meta.as_ref().and_then(|m| m.medium.clone()),
            reading_direction: meta.as_ref().and_then(|m| m.reading_direction.clone()),
            chapter_label: meta.as_ref().and_then(|m| m.chapter_label.clone()),
            arcs: meta.as_ref().map(|m| m.arcs.clone()).unwrap_or_default(),
            work: meta.clone(),
            work_name: name_of(folder),
            universe_name: universe_name.map(str::to_string),
        };

        // Both, and not one or the other. Archives sitting in the work folder are its
        // implicit edition; sub-folders holding archives are named editions. A work can have
        // both — you own Bleach, you buy the Perfect Edition, you put it in a folder beside
        // the volumes you already had.
        //
        // Taking the archives as proof there were no edition folders made that folder
        // **invisible**: the files sat on the disk, the scan reported nothing, and the
        // library simply did not have them. Silently losing files is the worst answer
        // available, and it was the one given.
        let direct = layout::archives(folder);
        let edition_folders: Vec<PathBuf> = layout::sub_folders(folder)
            .into_iter()
            .filter(|f| !layout::archives(f).is_empty())
            .collect();
        // How many places an entry could be, which is what decides whether it has to say
        // which one it is in. The implicit edition is not one of them: it has no name, so
        // there is nothing an entry could declare.
        let several =
            edition_folders.len() > 1 || (!direct.is_empty() && !edition_folders.is_empty());

        let mut inherited: Vec<Inherited> = Vec::new();
        if !direct.is_empty() {
            // An edition.json here describes an edition that has no folder of its own, so it
            // has no name either — nothing would ever show it. The rest of the file is used;
            // the name is the part that goes, and now says so.
            if let Some(named) = read_json::<EditionJson>(folder, layout::EDITION_FILE)
                .and_then(|e| e.name)
                .filter(|n| !n.trim().is_empty())
            {
                report.disregarded.push(format!(
                    "{}/edition.json names \"{named}\", but its archives sit beside it: the \
                     edition is implicit and an implicit edition has no name",
                    name_of(folder)
                ));
            }
            inherited.push(self.visit_edition(
                cx, folder, &id, true, &direct, &defaults, false, seen, report,
            )?);
        }
        for edition in &edition_folders {
            let files = layout::archives(edition);
            inherited.push(self.visit_edition(
                cx, edition, &id, false, &files, &defaults, several, seen, report,
            )?);
        }

        // Whatever work.json does not say yet, we take from what is still in the files.
        let Some(found) = inherited
            .into_iter()
            .find(|i| i.author.is_some() || i.reading_direction.is_some() || !i.genres.is_empty())
        else {
            return Ok(());
        };
        cx.execute(
            "UPDATE work SET
               author = COALESCE(author, ?1), reading_direction = COALESCE(reading_direction, ?2)
             WHERE id = ?3",
            rusqlite::params![found.author, found.reading_direction, id],
        )?;
        // Genres go the same way as the rest — the files first, the legacy metadata only
        // when work.json is silent. They used to take a second route, into a column of their
        // own, and so were shown without ever being filterable.
        let declared_genres = meta.as_ref().map(|m| m.genres.clone()).unwrap_or_default();
        if declared_genres.is_empty() && !found.genres.is_empty() {
            self.record_genres(cx, &id, &found.genres)?;
        }
        Ok(())
    }

    #[allow(clippy::too_many_arguments)]
    fn visit_edition(
        &self,
        cx: &Cx<'_>,
        folder: &Path,
        work_id: &str,
        implicit: bool,
        files: &[PathBuf],
        defaults: &WorkDefaults,
        has_several_editions: bool,
        seen: &mut Seen,
        report: &mut ScanReport,
    ) -> Result<Inherited> {
        let meta: Option<EditionJson> = read_json(folder, layout::EDITION_FILE);
        let id = id_of(folder, "edition");
        seen.editions.insert(id.clone());

        let name = if implicit {
            None
        } else {
            Some(
                meta.as_ref()
                    .and_then(|m| m.name.clone())
                    .unwrap_or_else(|| name_of(folder)),
            )
        };
        let or_default =
            |own: Option<String>, fallback: &Option<String>| own.or_else(|| fallback.clone());

        cx.execute(
            "INSERT INTO edition (id, work_id, name, path, implicit, publisher, status, medium,
                                  cover_file, reading_direction, volume_count, format, language)
             VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13)
             ON CONFLICT(id) DO UPDATE SET
               work_id=excluded.work_id, name=excluded.name, implicit=excluded.implicit,
               publisher=excluded.publisher, status=excluded.status, medium=excluded.medium,
               cover_file=excluded.cover_file, reading_direction=excluded.reading_direction,
               volume_count=excluded.volume_count, format=excluded.format, language=excluded.language",
            rusqlite::params![
                id,
                work_id,
                name,
                absolute(folder),
                i64::from(implicit),
                or_default(meta.as_ref().and_then(|m| m.publisher.clone()), &defaults.publisher),
                or_default(meta.as_ref().and_then(|m| m.status.clone()), &defaults.status),
                or_default(meta.as_ref().and_then(|m| m.medium.clone()), &defaults.medium),
                covers::in_folder(folder).map(|p| p.to_string_lossy().to_string()),
                or_default(
                    meta.as_ref().and_then(|m| m.reading_direction.clone()),
                    &defaults.reading_direction
                ),
                meta.as_ref().and_then(|m| m.volume_count).or(defaults.volume_count),
                or_default(meta.as_ref().and_then(|m| m.format.clone()), &defaults.format),
                or_default(meta.as_ref().and_then(|m| m.language.clone()), &defaults.language),
            ],
        )?;
        report.editions += 1;
        report.missing_required.extend(checks::edition(
            &name_of(folder),
            meta.as_ref(),
            implicit,
            defaults.work.as_ref(),
        ));

        // Neither a universe nor a work is a result of its own: you read an edition, you
        // never read Terres d'Arran and you never read "Parasite" in the abstract. So
        // everything a reader half-remembers is indexed here — the work's title and author,
        // the universe, the genres, the summary. The name column ranks tenfold; the rest
        // sits in the low-weighted one, where a word met in a summary never outranks the
        // same word in a title.
        let work = defaults.work.as_ref();
        let names: Vec<String> = [
            work.and_then(|w| w.title.clone()),
            Some(defaults.work_name.clone()),
            meta.as_ref().and_then(|m| m.name.clone()),
            defaults.universe_name.clone(),
        ]
        .into_iter()
        .flatten()
        .collect();
        let mut extra: Vec<String> = work.map(|w| w.genres.clone()).unwrap_or_default();
        extra.extend(
            [
                work.and_then(|w| w.author.clone()),
                work.and_then(|w| w.summary.clone()),
                or_default(
                    meta.as_ref().and_then(|m| m.publisher.clone()),
                    &defaults.publisher,
                ),
            ]
            .into_iter()
            .flatten(),
        );
        self.index(cx, "EDITION", &id, Some(&id), None, &names, &extra)?;

        let pattern = meta
            .as_ref()
            .and_then(|m| m.chapter_label.clone())
            .or_else(|| defaults.chapter_label.clone());

        let mut sorted = files.to_vec();
        sorted.sort();
        let mut entries: Vec<ReadEntry> = Vec::new();
        for (order, file) in sorted.iter().enumerate() {
            match self.read_entry(cx, file, order, pattern.as_deref(), seen, report) {
                Ok(Some(entry)) => entries.push(entry),
                Ok(None) => {}
                Err(e) => {
                    report.errors.push(format!("{}: {e}", name_of(file)));
                    tracing::warn!(file = %file.display(), error = %e, "unreadable entry");
                }
            }
        }
        for e in &entries {
            let file = name_of(&e.path);
            report.missing_required.extend(checks::entry(
                &file,
                e.declared.as_ref(),
                e.kind,
                has_several_editions,
            ));
            report.identity_mismatch.extend(checks::identity(
                &file,
                e.declared.as_ref(),
                &defaults.work_name,
                defaults.work.as_ref().and_then(|w| w.title.as_deref()),
                name.as_deref(),
            ));
            report.missing_required.extend(checks::chapters(
                &file,
                e.declared
                    .as_ref()
                    .map(|d| d.chapters.as_slice())
                    .unwrap_or(&[]),
                e.kind == "CHAPTER",
            ));
        }

        self.write_entries(cx, &id, &entries, report)?;

        // Chapters are rewritten for a whole edition at once, because a chapter's position
        // depends on the entries around it. When not one entry has moved, and none has been
        // added or removed, that computation lands on exactly what is already stored — fifty
        // thousand rows deleted and written again to no effect, which was most of what a
        // scan of an unchanged library spent itself on.
        let stored: i64 = cx
            .query_one(
                "SELECT COUNT(*) FROM entry WHERE edition_id = ?1",
                [id.as_str()],
                |r| r.get(0),
            )?
            .unwrap_or(0);
        let settled = entries.iter().all(|e| e.unchanged) && stored == entries.len() as i64;

        if settled {
            // Still counted, or the report would announce zero chapters for a library that
            // has fifty thousand — the summary describes the library, not the work done.
            let count: i64 = cx
                .query_one(
                    "SELECT COUNT(*) FROM chapter WHERE edition_id = ?1",
                    [id.as_str()],
                    |r| r.get(0),
                )?
                .unwrap_or(0);
            report.chapters += count as u32;
        } else {
            self.write_chapters(cx, &id, &entries, report)?;
        }
        self.write_arcs(cx, &id, meta.as_ref(), &defaults.arcs, &entries, report)?;

        Ok(entries
            .iter()
            .find_map(|e| e.inherited.clone())
            .unwrap_or_default())
    }

    // ----------------------------------------------------------------- entries

    fn read_entry(
        &self,
        cx: &Cx<'_>,
        file: &Path,
        order: usize,
        chapter_pattern: Option<&str>,
        seen: &mut Seen,
        report: &mut ScanReport,
    ) -> Result<Option<ReadEntry>> {
        let id = id_of(file, "");
        seen.entries.insert(id.clone());

        let meta = std::fs::metadata(file)?;
        let size = meta.len() as i64;
        let modified_at = meta
            .modified()?
            .duration_since(std::time::UNIX_EPOCH)
            .map(|d| d.as_millis() as i64)
            .unwrap_or(0);

        let unchanged = cx
            .query_one(
                "SELECT 1 FROM entry WHERE id = ?1 AND size = ?2 AND modified_at = ?3",
                rusqlite::params![id, size, modified_at],
                |r| r.get::<_, i64>(0),
            )?
            .is_some();

        let content = cbz::read(file, self.all_dimensions && !unchanged)?;
        if !unchanged {
            report.reanalysed += 1;
        }
        for name in &content.duplicate_names {
            report
                .duplicate_page_names
                .push(format!("{}: {name}", name_of(file)));
        }

        // entry.json is authoritative. ComicInfo is only a fallback, and a temporary one.
        let legacy: Option<LegacyRead> = content
            .sidecar(comic_info::ENTRY_NAME)
            .and_then(comic_info::read);
        let own: Option<EntryJson> = content
            .sidecar(ENTRY_JSON)
            .and_then(crate::metadata::sidecars::read);
        let entry: Option<EntryJson> = own
            .clone()
            .or_else(|| legacy.as_ref().map(|l| l.entry.clone()));
        // An entry.json that exists and says nothing worth having counts as no metadata —
        // unless a ComicInfo is standing behind it, which is where the values then come from.
        if entry.is_none() || (legacy.is_none() && checks::says_nothing(own.as_ref())) {
            report.entries_without_metadata.push(name_of(file));
        }

        let from_name = label::parse(&stem_of(file));
        let kind = entry_kind(entry.as_ref(), &from_name.label);

        // For a chapter the file name wins: ComicInfo pins a volume number on it for lack of
        // any way to say otherwise — Chapitre 686.5 declares Number 75 there.
        let volume_number = if kind == "CHAPTER" {
            None
        } else {
            own.as_ref()
                .and_then(|o| o.number)
                .or_else(|| legacy.as_ref().and_then(|l| l.entry.number))
                .or(from_name.number)
        };

        let chapters = self.chapters_of(
            kind,
            entry.as_ref(),
            own.as_ref(),
            &from_name,
            chapter_pattern,
            report,
        );

        report.contradictions.extend(checks::coherence(
            &name_of(file),
            own.as_ref(),
            kind,
            content.pages.len() as i32,
            from_name.number,
        ));

        // Only on a volume: a chapter file is its own chapter, and starts at its first page.
        if kind == "VOLUME" {
            report.chapters_without_start_page.extend(
                chapters
                    .iter()
                    .filter(|c| c.start_page.is_none())
                    .map(|c| format!("{} → {}", name_of(file), c.label)),
            );
        }

        Ok(Some(ReadEntry {
            id,
            path: file.to_path_buf(),
            kind,
            size,
            modified_at,
            volume_number,
            title: entry
                .as_ref()
                .and_then(|e| e.title.clone())
                .or(from_name.title),
            isbn: entry.as_ref().and_then(|e| e.isbn.clone()),
            published_on: entry.as_ref().and_then(|e| e.published_on.clone()),
            summary: entry.as_ref().and_then(|e| e.summary.clone()),
            chapters,
            pages: if unchanged { None } else { Some(content.pages) },
            unchanged,
            file_order: order,
            cover_file: covers::beside_archive(file).map(|p| p.to_string_lossy().to_string()),
            declared: own.or_else(|| legacy.as_ref().map(|l| l.entry.clone())),
            legacy_arc: legacy.as_ref().and_then(|l| l.arc.clone()),
            inherited: legacy.as_ref().map(|l| Inherited {
                author: l.author.clone(),
                reading_direction: l.reading_direction.clone(),
                genres: l.genres.clone(),
            }),
        }))
    }

    /// Chains anchored chapters to the last numbered one before them.
    ///
    /// A bonus with no number of its own is not adrift: it sits after whatever preceded it,
    /// and that is enough to place it on the edition's single scale.
    /// The chapters this entry declares, drafted and ready to be written down.
    fn chapters_of(
        &self,
        kind: &str,
        entry: Option<&EntryJson>,
        own: Option<&EntryJson>,
        from_name: &label::ParsedLabel,
        chapter_pattern: Option<&str>,
        report: &mut ScanReport,
    ) -> Vec<ChapterDraft> {
        let entry_volume = entry.and_then(|e| e.volume);
        if kind == "CHAPTER" && entry.is_none_or(|e| e.chapters.is_empty()) {
            // The entry is the chapter and says nothing more: its file name describes it.
            // Declaring a single chapter in entry.json is the way to add an anchor or a
            // label — which is exactly what a standalone bonus file needs.
            return self
                .draft(
                    &ChapterJson {
                        raw: Some(from_name.raw.clone()),
                        number: own.and_then(|o| o.number).or(from_name.number),
                        title: from_name.title.clone(),
                        ..Default::default()
                    },
                    chapter_pattern,
                    None,
                    entry_volume,
                    report,
                )
                .into_iter()
                .collect();
        }
        self.anchor_chain(
            entry.map(|e| e.chapters.clone()).unwrap_or_default(),
            chapter_pattern,
            entry_volume,
            report,
        )
    }

    fn anchor_chain(
        &self,
        source: Vec<ChapterJson>,
        pattern: Option<&str>,
        entry_volume: Option<f64>,
        report: &mut ScanReport,
    ) -> Vec<ChapterDraft> {
        let mut last_number: Option<f64> = None;
        let mut out = Vec::new();
        for chapter in source {
            let Some(drafted) = self.draft(&chapter, pattern, last_number, entry_volume, report)
            else {
                continue;
            };
            if drafted.number.is_some() {
                last_number = drafted.number;
            }
            out.push(drafted);
        }
        out
    }

    fn draft(
        &self,
        c: &ChapterJson,
        pattern: Option<&str>,
        previous_number: Option<f64>,
        entry_volume: Option<f64>,
        report: &mut ScanReport,
    ) -> Option<ChapterDraft> {
        let parsed = c
            .raw
            .as_deref()
            .map(str::trim)
            .filter(|r| !r.is_empty())
            .map(label::parse);
        let number = c.number.or_else(|| parsed.as_ref().and_then(|p| p.number));
        let title = c
            .title
            .clone()
            .filter(|t| !t.trim().is_empty())
            .or_else(|| parsed.as_ref().and_then(|p| p.title.clone()));

        // An empty label is a decision, not an omission: it says "show the title alone",
        // which is what a specially-named bonus wants even when it carries a number.
        let composed = label::compose(pattern, number);
        let label_text = c
            .label
            .clone()
            .or(composed)
            .or_else(|| parsed.as_ref().map(|p| p.label.clone()))
            .unwrap_or_default();

        if label_text.trim().is_empty() && title.is_none() && number.is_none() {
            report
                .errors
                .push("a chapter with no label, title or number — skipped".to_string());
            return None;
        }

        let raw = c
            .raw
            .clone()
            .map(|r| r.trim().to_string())
            .filter(|r| !r.is_empty())
            .unwrap_or_else(|| {
                [
                    Some(label_text.clone()).filter(|l| !l.trim().is_empty()),
                    title.clone(),
                ]
                .into_iter()
                .flatten()
                .collect::<Vec<_>>()
                .join(" : ")
            });

        Some(ChapterDraft {
            raw,
            kind: if number.is_none() { "BONUS" } else { "CHAPTER" },
            label: label_text,
            number,
            title,
            start_page: c.start_page,
            after: c.after.or(if number.is_none() {
                previous_number
            } else {
                None
            }),
            volume: c.volume.or(entry_volume),
        })
    }

    // ----------------------------------------------------------------- writing

    fn write_entries(
        &self,
        cx: &Cx<'_>,
        edition_id: &str,
        entries: &[ReadEntry],
        report: &mut ScanReport,
    ) -> Result<()> {
        let arrived_at = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|d| d.as_millis() as i64)
            .unwrap_or(0);

        for e in entries {
            let page_count = match &e.pages {
                Some(pages) => pages.len() as i64,
                None => cx
                    .query_one(
                        "SELECT COUNT(*) FROM page WHERE entry_id = ?1",
                        [e.id.as_str()],
                        |r| r.get(0),
                    )?
                    .unwrap_or(0),
            };

            cx.execute(
                "INSERT INTO entry (id, edition_id, type, file, size, modified_at, added_at,
                                    cover_file, volume_number, title, sort_key, page_count,
                                    isbn, published_on, summary)
                 VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15)
                 ON CONFLICT(id) DO UPDATE SET
                   edition_id=excluded.edition_id, type=excluded.type, size=excluded.size,
                   modified_at=excluded.modified_at,
                   -- Set once, on arrival, and never touched again: it is the one fact a
                   -- rescan could not reconstruct.
                   added_at=COALESCE(entry.added_at, excluded.added_at),
                   cover_file=excluded.cover_file, volume_number=excluded.volume_number,
                   title=excluded.title, sort_key=excluded.sort_key,
                   page_count=excluded.page_count, isbn=excluded.isbn,
                   published_on=excluded.published_on, summary=excluded.summary",
                rusqlite::params![
                    e.id,
                    edition_id,
                    e.kind,
                    absolute(&e.path),
                    e.size,
                    e.modified_at,
                    arrived_at,
                    e.cover_file,
                    e.volume_number,
                    e.title,
                    e.sort_key(),
                    page_count,
                    e.isbn,
                    e.published_on,
                    e.summary,
                ],
            )?;
            report.entries += 1;

            let names: Vec<String> = [e.title.clone(), Some(stem_of(&e.path))]
                .into_iter()
                .flatten()
                .collect();
            self.index(
                cx,
                "ENTRY",
                &e.id,
                Some(edition_id),
                Some(&e.id),
                &names,
                &[],
            )?;

            let Some(pages) = &e.pages else { continue };
            cx.execute("DELETE FROM page WHERE entry_id = ?1", [e.id.as_str()])?;
            for (i, page) in pages.iter().enumerate() {
                let dimension = match page.dimension {
                    Some(d) => Some(d),
                    None if i == 0 => cbz::extract(&e.path, &page.name)
                        .ok()
                        .flatten()
                        .and_then(|bytes| crate::archive::images::dimension(&bytes)),
                    None => None,
                };
                cx.execute(
                    "INSERT INTO page (entry_id, number, entry_name, media_type, width, height, size)
                     VALUES (?1,?2,?3,?4,?5,?6,?7)",
                    rusqlite::params![
                        e.id,
                        i as i64,
                        page.name,
                        page.media_type,
                        dimension.map(|d| d.0 as i64),
                        dimension.map(|d| d.1 as i64),
                        page.size.map(|s| s as i64),
                    ],
                )?;
            }
            report.pages += pages.len() as u32;
        }
        Ok(())
    }

    fn write_chapters(
        &self,
        cx: &Cx<'_>,
        edition_id: &str,
        entries: &[ReadEntry],
        report: &mut ScanReport,
    ) -> Result<()> {
        cx.execute("DELETE FROM chapter WHERE edition_id = ?1", [edition_id])?;

        let mut flat: Vec<(&ReadEntry, &ChapterDraft, usize)> = entries
            .iter()
            .flat_map(|e| e.chapters.iter().enumerate().map(move |(i, c)| (e, c, i)))
            .collect();

        flat.sort_by(|a, b| {
            let scale = |(e, c, _): &(&ReadEntry, &ChapterDraft, usize)| {
                c.number.or(c.after).unwrap_or_else(|| e.sort_key())
            };
            // Where it sits on the edition's single scale; then, at the same spot, a
            // numbered chapter before what is anchored to it.
            scale(a)
                .partial_cmp(&scale(b))
                .unwrap_or(std::cmp::Ordering::Equal)
                .then_with(|| a.1.number.is_none().cmp(&b.1.number.is_none()))
                .then_with(|| {
                    a.0.sort_key()
                        .partial_cmp(&b.0.sort_key())
                        .unwrap_or(std::cmp::Ordering::Equal)
                })
                .then_with(|| a.2.cmp(&b.2))
        });

        let mut taken_by: HashMap<String, String> = HashMap::new();
        for (position, (entry, chapter, rank)) in flat.iter().enumerate() {
            let mut number = chapter.number;
            if let Some(value) = number {
                let key = format!("{value}");
                match taken_by.get(&key) {
                    Some(first) => {
                        report.duplicate_numbers.push(format!(
                            "{value}: \"{first}\" then \"{}\" ({})",
                            chapter.raw,
                            name_of(&entry.path)
                        ));
                        number = None;
                    }
                    None => {
                        taken_by.insert(key, chapter.raw.clone());
                    }
                }
            }
            let id = format!("{}-{rank}", entry.id);
            cx.execute(
                "INSERT INTO chapter (id, edition_id, entry_id, raw, label, number, title, kind,
                                      position, start_page, volume)
                 VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11)",
                rusqlite::params![
                    id,
                    edition_id,
                    entry.id,
                    chapter.raw,
                    chapter.label,
                    number,
                    chapter.title,
                    chapter.kind,
                    position as i64,
                    chapter.start_page,
                    chapter.volume,
                ],
            )?;
            let names: Vec<String> = [chapter.title.clone(), Some(chapter.raw.clone())]
                .into_iter()
                .flatten()
                .collect();
            self.index(
                cx,
                "CHAPTER",
                &id,
                Some(edition_id),
                Some(&entry.id),
                &names,
                &[],
            )?;
            report.chapters += 1;
        }
        Ok(())
    }

    fn write_arcs(
        &self,
        cx: &Cx<'_>,
        edition_id: &str,
        meta: Option<&EditionJson>,
        work_arcs: &[ArcJson],
        entries: &[ReadEntry],
        report: &mut ScanReport,
    ) -> Result<()> {
        cx.execute("DELETE FROM arc WHERE edition_id = ?1", [edition_id])?;

        let declared: &[ArcJson] = match meta.map(|m| m.arcs.as_slice()) {
            Some(arcs) if !arcs.is_empty() => arcs,
            _ => work_arcs,
        };
        if !declared.is_empty() {
            for (i, arc) in declared.iter().enumerate() {
                cx.execute(
                    "INSERT INTO arc (id, edition_id, name, unit, from_number, to_number, position)
                     VALUES (?1,?2,?3,?4,?5,?6,?7)",
                    rusqlite::params![
                        format!("{edition_id}-arc-{i}"),
                        edition_id,
                        arc.name,
                        arc.unit.to_uppercase(),
                        arc.from,
                        arc.to,
                        i as i64,
                    ],
                )?;
            }
            return Ok(());
        }

        // Fallback: StoryArc can only say the arc per volume, since it is copied into every
        // file. That is exactly the limit that makes an arc spill across two volumes there —
        // four Haikyū volumes belong to two arcs at once.
        let mut ranges: HashMap<String, (f64, f64)> = HashMap::new();
        for e in entries {
            let (Some(arc), Some(volume)) = (&e.legacy_arc, e.volume_number) else {
                continue;
            };
            ranges
                .entry(arc.clone())
                .and_modify(|(from, to)| {
                    *from = from.min(volume);
                    *to = to.max(volume);
                })
                .or_insert((volume, volume));
        }
        let mut derived: Vec<(String, f64, f64)> = ranges
            .into_iter()
            .map(|(name, (from, to))| (name, from, to))
            .collect();
        derived.sort_by(|a, b| a.1.partial_cmp(&b.1).unwrap_or(std::cmp::Ordering::Equal));

        for (i, (name, from, to)) in derived.iter().enumerate() {
            cx.execute(
                "INSERT INTO arc (id, edition_id, name, unit, from_number, to_number, position)
                 VALUES (?1,?2,?3,'VOLUME',?4,?5,?6)",
                rusqlite::params![
                    format!("{edition_id}-arc-{i}"),
                    edition_id,
                    name,
                    from,
                    to,
                    i as i64
                ],
            )?;
            report.derived_arcs.push(format!(
                "{name} (volumes {} to {})",
                short(*from),
                short(*to)
            ));
        }
        Ok(())
    }

    // ----------------------------------------------------------------- helpers

    /// Rewritten whole on every scan: the files say what the genres are, not the table.
    fn record_genres(&self, cx: &Cx<'_>, work_id: &str, genres: &[String]) -> Result<()> {
        cx.execute("DELETE FROM work_genre WHERE work_id = ?1", [work_id])?;
        for name in genres.iter().map(|g| g.trim()).filter(|g| !g.is_empty()) {
            cx.execute(
                "INSERT OR IGNORE INTO work_genre (work_id, name, key) VALUES (?1,?2,?3)",
                rusqlite::params![work_id, name, search_key(name)],
            )?;
        }
        Ok(())
    }

    fn record_universe(
        &self,
        cx: &Cx<'_>,
        folder: &Path,
        seen: &mut Seen,
        report: &mut ScanReport,
    ) -> Result<String> {
        let meta: Option<UniverseJson> = read_json(folder, layout::UNIVERSE_FILE);
        let id = id_of(folder, "universe");
        seen.universes.insert(id.clone());
        cx.execute(
            "INSERT INTO universe (id, name, path) VALUES (?1,?2,?3)
             ON CONFLICT(id) DO UPDATE SET name = excluded.name",
            rusqlite::params![
                id,
                meta.and_then(|m| m.name).unwrap_or_else(|| name_of(folder)),
                absolute(folder)
            ],
        )?;
        report.universes += 1;
        Ok(id)
    }

    fn universe_name(&self, cx: &Cx<'_>, id: &str) -> Result<Option<String>> {
        cx.query_one("SELECT name FROM universe WHERE id = ?1", [id], |r| {
            r.get::<_, String>(0)
        })
    }

    /// Removes what the scan did not meet.
    ///
    /// Through a temporary table rather than an inlined IN clause: at three thousand entries
    /// that clause would be a fifty-kilobyte statement, and SQLite has limits on how deep an
    /// expression may go. A table has none, and the query stops growing with the library.
    fn prune(&self, cx: &Cx<'_>, seen: &Seen) -> Result<()> {
        let clean = |table: &str, keep: &HashSet<String>| -> Result<()> {
            if keep.is_empty() {
                cx.execute(&format!("DELETE FROM {table}"), [])?;
                return Ok(());
            }
            cx.run("DROP TABLE IF EXISTS temp.kept")?;
            cx.run("CREATE TEMP TABLE kept (id TEXT PRIMARY KEY)")?;
            for id in keep {
                cx.execute("INSERT INTO temp.kept (id) VALUES (?1)", [id.as_str()])?;
            }
            cx.execute(
                &format!("DELETE FROM {table} WHERE id NOT IN (SELECT id FROM temp.kept)"),
                [],
            )?;
            cx.run("DROP TABLE temp.kept")?;
            Ok(())
        };
        clean("entry", &seen.entries)?;
        clean("edition", &seen.editions)?;
        clean("work", &seen.works)?;
        clean("universe", &seen.universes)?;
        // Last, and not first: it looks for search rows whose subject is gone, so the
        // subjects have to be gone already. Run before the cleaning, it read tables that
        // still held everything and found no orphan — a deleted series stayed findable until
        // the scan after the one that removed it.
        self.prune_search(cx)
    }

    fn prune_within(&self, cx: &Cx<'_>, work_path: &str, seen: &Seen) -> Result<()> {
        let Some(work_id) =
            cx.query_one("SELECT id FROM work WHERE path = ?1", [work_path], |r| {
                r.get::<_, String>(0)
            })?
        else {
            return Ok(());
        };

        let editions = cx.query(
            "SELECT id FROM edition WHERE work_id = ?1",
            [work_id.as_str()],
            |r| r.get::<_, String>(0),
        )?;
        for id in editions.iter().filter(|id| !seen.editions.contains(*id)) {
            cx.execute("DELETE FROM edition WHERE id = ?1", [id.as_str()])?;
        }
        for edition in &seen.editions {
            let entries = cx.query(
                "SELECT id FROM entry WHERE edition_id = ?1",
                [edition.as_str()],
                |r| r.get::<_, String>(0),
            )?;
            for id in entries.iter().filter(|id| !seen.entries.contains(*id)) {
                cx.execute("DELETE FROM entry WHERE id = ?1", [id.as_str()])?;
            }
        }
        self.prune_search(cx)
    }

    /// Through `search_ref`, which is an ordinary indexed table: the same delete written
    /// against the FTS columns would scan the whole index once per kind.
    fn prune_search(&self, cx: &Cx<'_>) -> Result<()> {
        let drop = |kind: &str, table: Option<&str>| -> Result<()> {
            let orphans = match table {
                Some(t) => format!("AND ref NOT IN (SELECT id FROM {t})"),
                None => String::new(),
            };
            cx.execute(
                &format!(
                    "DELETE FROM search WHERE rowid IN
                     (SELECT row_id FROM search_ref WHERE kind = ?1 {orphans})"
                ),
                [kind],
            )?;
            cx.execute(
                &format!("DELETE FROM search_ref WHERE kind = ?1 {orphans}"),
                [kind],
            )?;
            Ok(())
        };
        drop("ENTRY", Some("entry"))?;
        drop("CHAPTER", Some("chapter"))?;
        drop("EDITION", Some("edition"))?;
        // Written by earlier versions, back when a work was a result of its own.
        drop("WORK", None)
    }

    #[allow(clippy::too_many_arguments)]
    fn index(
        &self,
        cx: &Cx<'_>,
        kind: &str,
        id: &str,
        edition_id: Option<&str>,
        entry_id: Option<&str>,
        texts: &[String],
        extra: &[String],
    ) -> Result<()> {
        let Some(label) = texts.first().filter(|t| !t.trim().is_empty()) else {
            return Ok(());
        };
        let name = search_key(label);
        let detail = texts
            .iter()
            .skip(1)
            .chain(extra)
            .map(|t| search_key(t))
            .collect::<Vec<_>>()
            .join(" ");
        let digest = digest_of(&name, &detail, label);

        let existing: Option<(i64, Option<String>)> = cx.query_one(
            "SELECT row_id, digest FROM search_ref WHERE kind = ?1 AND ref = ?2",
            [kind, id],
            |r| Ok((r.get(0)?, r.get(1)?)),
        )?;

        // Identical text, identical row: there is nothing to write. Rewriting it anyway
        // meant every scan redid the whole index — four statements per row, 55 000 rows — to
        // arrive at exactly what was already there.
        if existing
            .as_ref()
            .is_some_and(|(_, stored)| stored.as_deref() == Some(digest.as_str()))
        {
            return Ok(());
        }
        // FTS5 has no upsert: the old row goes first. By rowid, never by kind and ref —
        // those are UNINDEXED columns, so matching on them reads the entire index, and
        // reindexing a whole library that way costs n² row visits.
        if let Some((row_id, _)) = existing {
            cx.execute("DELETE FROM search WHERE rowid = ?1", [row_id])?;
        }

        // The name is indexed apart from the rest so that a match on a title can be ranked
        // above one buried in a raw label.
        cx.execute(
            "INSERT INTO search (name, detail, kind, ref, edition_id, entry_id, label)
             VALUES (?1,?2,?3,?4,?5,?6,?7)",
            rusqlite::params![name, detail, kind, id, edition_id, entry_id, label],
        )?;
        cx.execute(
            "INSERT INTO search_ref (kind, ref, row_id, digest) VALUES (?1,?2,last_insert_rowid(),?3)
             ON CONFLICT(kind, ref) DO UPDATE SET row_id = excluded.row_id, digest = excluded.digest",
            rusqlite::params![kind, id, digest],
        )?;
        Ok(())
    }
}

/// Short and content-addressed: two rows written from the same text share it.
fn digest_of(name: &str, detail: &str, label: &str) -> String {
    let digest = Sha256::digest(format!("{name}\u{0}{detail}\u{0}{label}").as_bytes());
    digest.iter().take(8).map(|b| format!("{b:02x}")).collect()
}

/// The identity of a folder or a file: its path, and nothing else.
///
/// Stable across scans, so an entry keeps its progress when the library is reindexed, and
/// distinct per level, so a work and its implicit edition do not collide on one path.
pub fn id_of(path: &Path, suffix: &str) -> String {
    let digest = Sha256::digest(format!("{}{suffix}", absolute(path)).as_bytes());
    digest
        .iter()
        .map(|b| format!("{b:02x}"))
        .collect::<String>()
        .chars()
        .take(16)
        .collect()
}

fn absolute(path: &Path) -> String {
    std::path::absolute(path)
        .unwrap_or_else(|_| path.to_path_buf())
        .to_string_lossy()
        .to_string()
}

fn name_of(path: &Path) -> String {
    path.file_name()
        .map(|n| n.to_string_lossy().to_string())
        .unwrap_or_default()
}

fn stem_of(path: &Path) -> String {
    path.file_stem()
        .map(|n| n.to_string_lossy().to_string())
        .unwrap_or_default()
}

fn short(value: f64) -> String {
    if value.fract() == 0.0 {
        format!("{}", value as i64)
    } else {
        format!("{value}")
    }
}

fn read_json<T: serde::de::DeserializeOwned>(folder: &Path, name: &str) -> Option<T> {
    let bytes = std::fs::read(folder.join(name)).ok()?;
    crate::metadata::sidecars::read(&bytes)
}

/// VOLUME or CHAPTER, from what the entry declares and failing that from its file name.
///
/// Case-insensitive: "chapter" is unmistakably CHAPTER, and reading it as VOLUME because of
/// a lower-case c inverts what the file plainly says. Anything that is still neither is a
/// typo, and `checks::coherence` says so rather than guessing.
fn entry_kind(declared: Option<&EntryJson>, from_name: &str) -> &'static str {
    if declared.is_some_and(|e| e.kind.eq_ignore_ascii_case("CHAPTER"))
        || from_name.to_lowercase().contains("chap")
    {
        "CHAPTER"
    } else {
        "VOLUME"
    }
}
