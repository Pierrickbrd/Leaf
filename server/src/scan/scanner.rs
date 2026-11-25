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
use crate::metadata::age::age_of;
use crate::metadata::label;
use crate::metadata::legacy_comic_info::{self as comic_info, LegacyRead};
use crate::metadata::sidecars::{
    self, ArcJson, ChapterJson, EditionJson, EntryJson, OneShotJson, StepJson, UniverseJson,
    WorkJson, UNITS,
};
use crate::scan::checks;
use crate::scan::covers;
use crate::scan::identity;
use crate::scan::layout::{self, Kind};
use crate::scan::orders;
use crate::scan::report::ScanReport;
use crate::store::text::{search_key, tag_case};
use crate::store::{Cx, Db};

pub const ENTRY_JSON: &str = "entry.json";

/// A book that stands alone, declaring itself so. Beside `entry.json`, inside the same
/// archive — see [`crate::metadata::sidecars::OneShotJson`] for why its presence is the
/// whole declaration.
pub const ONESHOT_JSON: &str = "oneshot.json";

pub struct Scanner {
    db: std::sync::Arc<Db>,
    all_dimensions: bool,
    /// Held for the whole of a scan and for the whole of a rename: the two read and move the
    /// same files, and a scan that sees a rename half done prunes the volume that is between
    /// its old name and its new one, reading position and all. See [`Scanner::exclusive`].
    exclusive: std::sync::Mutex<()>,
}

/// Proof that the scanners' lock is held, handed to the closure of [`Scanner::exclusive`].
/// What needs the lock already taken asks for it, so that it cannot be called without.
pub struct Held(());

/// What the scan met, so that what it did not meet can be removed.
#[derive(Default)]
struct Seen {
    universes: HashSet<String>,
    works: HashSet<String>,
    editions: HashSet<String>,
    entries: HashSet<String>,
    /// Which folder is already using which identifier. Two carrying the same one is a
    /// contradiction, and it happens by copying a folder with its sidecar — see
    /// [`identity::of`].
    claimed: identity::Claimed,
}

/// The edition one file is being read into: what identifies it, and how it labels a chapter.
///
/// Two arguments in a struct rather than beside each other, because the reader of one entry
/// takes six things already and the two that follow the edition belong together.
struct Within<'a> {
    edition_id: &'a str,
    chapter_pattern: Option<&'a str>,
}

/// The fields a work hands down to an edition that does not declare its own.
#[derive(Default, Clone)]
struct WorkDefaults {
    status: Option<String>,
    publisher: Option<String>,
    collection: Option<String>,
    volume_count: Option<i32>,
    format: Option<String>,
    language: Option<String>,
    medium: Option<String>,
    reading_direction: Option<String>,
    colour: Option<bool>,
    cover_volume: Option<f64>,
    chapter_label: Option<String>,
    arcs: Vec<ArcJson>,
    /// Who wrote and who drew, as `work.json` declares it: what a volume that says nothing
    /// is credited with.
    authors: Vec<String>,
    artists: Vec<String>,
    work: Option<WorkJson>,
    work_name: String,
    universe_name: Option<String>,
}

/// What the files turned out to know that `work.json` did not say.
#[derive(Default, Clone)]
struct Inherited {
    reading_direction: Option<String>,
    genres: Vec<String>,
}

/// What reading one edition leaves for the work above it to settle.
struct Visited {
    inherited: Inherited,
    lifted: Vec<ReadEntry>,
    credited: Credited,
}

/// The credits an edition's volumes carried, with the search entry that is still to be
/// written with them.
struct Credited {
    authors: Vec<String>,
    artists: Vec<String>,
    /// Whether every volume of the edition has a writer, and a drawer, once it has taken
    /// its series' default: one volume credited by nobody is what the work is reported for.
    every_authored: bool,
    every_drawn: bool,
    search: EditionSearch,
}

/// Who a volume is credited with once its series has had its say, and, field by field,
/// whether that came from the series rather than from the volume.
#[derive(Default, Clone)]
struct Credits {
    authors: Vec<String>,
    authors_inherited: bool,
    artists: Vec<String>,
    artists_inherited: bool,
}

impl Credits {
    /// What the volume says, else what its series declares — never the two together: a
    /// volume that names its writer replaces the series' writer, it does not join him.
    fn of(own: &(Vec<String>, Vec<String>), defaults: &WorkDefaults) -> Self {
        let (authors, authors_inherited) = inherit(&own.0, &defaults.authors);
        let (artists, artists_inherited) = inherit(&own.1, &defaults.artists);
        Credits {
            authors,
            authors_inherited,
            artists,
            artists_inherited,
        }
    }

    /// What the volume says and nothing else: a book standing alone has no series above it.
    fn own(own: &(Vec<String>, Vec<String>)) -> Self {
        Credits {
            authors: own.0.clone(),
            artists: own.1.clone(),
            ..Credits::default()
        }
    }
}

fn inherit(own: &[String], default: &[String]) -> (Vec<String>, bool) {
    if own.is_empty() && !default.is_empty() {
        (default.to_vec(), true)
    } else {
        (own.to_vec(), false)
    }
}

/// What the search index holds about an edition apart from its credits.
struct EditionSearch {
    id: String,
    names: Vec<String>,
    genres: Vec<String>,
    rest: Vec<String>,
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
    /// The identity the index still files this file under, when it is not this one.
    ///
    /// The whole of the migration, in one field. A library indexed before identities left
    /// the path holds every entry under the hash of its path; the reading position is filed
    /// against that, and `ON DELETE CASCADE` takes it away the moment the prune removes the
    /// row. So the row is found by the file it holds — which did not change — and the
    /// position is carried before anything is removed. See [`Scanner::reattach`].
    stale: Option<String>,
    file_order: usize,
    cover_file: Option<String>,
    /// A page of the archive that `entry.json` names as its cover, kept only when the archive
    /// holds it: a number past the end serves nothing, and the cover would be a 404.
    cover_page: Option<i32>,
    legacy_arc: Option<String>,
    inherited: Option<Inherited>,
    /// Who wrote and who drew **this** volume: its `entry.json` first, its ComicInfo's
    /// `Writer` and `Penciller` otherwise, field by field. The series is credited with the
    /// union of these over its volumes, see [`union`].
    credits: (Vec<String>, Vec<String>),
    /// [`ReadEntry::credits`] once the series has filled what the volume left empty — what
    /// the volume is recorded, shown and found under.
    effective: Credits,
    /// What the file says about itself, kept so the checks can compare it with where it
    /// actually sits. Nothing downstream reads the index from it.
    declared: Option<EntryJson>,
    /// Set when the archive declares itself a whole book. Such a file is not an entry of the
    /// edition it was read under: it is lifted out and filed as its own work, so two albums
    /// dropped side by side in one folder are two books rather than a two-volume series
    /// named after the folder.
    one_shot: Option<OneShotJson>,
    /// The row that already holds this file — its identity, size and modified time — kept
    /// because lifting an album changes the edition its identity is computed under, and
    /// `unchanged` and `stale` have to be worked out again against the new one. Asking the
    /// database a second time for an answer already in hand is the shape this file counts
    /// statements to avoid.
    held: Option<(String, i64, i64)>,
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
        Scanner {
            db,
            all_dimensions,
            exclusive: std::sync::Mutex::new(()),
        }
    }

    /// Runs `f` while no scan and no other `exclusive` runs, and keeps them out until it ends.
    ///
    /// Not re-entrant: `f` must not call [`Scanner::scan`] or [`Scanner::rescan_work`], which
    /// take the lock themselves and would wait for `f` forever. It receives a [`Held`] for
    /// [`Scanner::rescan_work_held`], which is the same rescan without taking the lock.
    pub fn exclusive<T>(&self, f: impl FnOnce(&Held) -> T) -> T {
        // A panic in a previous holder poisons the mutex but leaves nothing inconsistent in
        // it — it guards no data, only the right to go.
        let _lock = self
            .exclusive
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        f(&Held(()))
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
        self.exclusive(|_| self.scan_held(roots))
    }

    fn scan_held(&self, roots: &[PathBuf]) -> Result<ScanReport> {
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
            let mut lost = 0;
            self.db.write(|cx| self.prune(cx, &seen, &mut lost))?;
            report.progress_lost = lost;
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
        self.exclusive(|held| self.rescan_work_held(held, work_folder))
    }

    /// [`Scanner::rescan_work`] for a caller already inside [`Scanner::exclusive`].
    pub fn rescan_work_held(&self, _held: &Held, work_folder: &Path) -> Result<ScanReport> {
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
            // Which universe this folder is **in**, read from where it sits rather than from
            // the row that describes it. The row was the answer until an identity stopped
            // coming from a path: a work that moved keeps its row, so the row still names
            // the universe it left, and an aimed rescan would file it back there — or, going
            // the other way, leave it at the root it no longer sits in.
            let universe_id = self.universe_over(cx, work_folder)?;

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
                self.prune_within(cx, &path, &seen, &mut report.progress_lost)
            } else if work_folder.exists() && layout::readable(work_folder).is_err() {
                // There, and shut. Not the same thing as gone, and the difference is a
                // series disappearing from the shelf because a permission changed.
                anyhow::bail!("{} cannot be listed", work_folder.display())
            } else {
                // The folder is gone, or holds nothing any more: drop what it left behind.
                // Every position under it goes too, and is counted before it does — this is
                // the heaviest loss an aimed rescan can cause and it was the quietest.
                report.progress_lost += cx
                    .query_one(
                        "SELECT COUNT(*) FROM progress WHERE entry_id IN \
                         (SELECT entry.id FROM entry \
                          JOIN edition ON edition.id = entry.edition_id \
                          JOIN work ON work.id = edition.work_id WHERE work.path = ?1)",
                        [path.as_str()],
                        |r| r.get::<_, i64>(0),
                    )?
                    .unwrap_or(0) as u32;
                cx.execute("DELETE FROM work WHERE path = ?1", [path.as_str()])?;
                Ok(())
            }
        })?;
        Ok(report)
    }

    /// The universe a folder sits under, by walking up from it.
    ///
    /// Against the index rather than the disk, because the index is what has to agree: an
    /// ancestor that declares a universe was scanned, so it is in there. The walk stops at
    /// the first one found, and at `MAX_SHELVES` ancestors — above a library there is
    /// nothing to find and no reason to climb to the filesystem root looking for it.
    fn universe_over(&self, cx: &Cx<'_>, folder: &Path) -> Result<Option<String>> {
        let mut at = folder.to_path_buf();
        for _ in 0..layout::MAX_SHELVES {
            let Some(parent) = at.parent().map(Path::to_path_buf) else {
                return Ok(None);
            };
            let found: Option<String> = cx.query_one(
                "SELECT id FROM universe WHERE path = ?1",
                [absolute(&parent).as_str()],
                |r| r.get(0),
            )?;
            if found.is_some() {
                return Ok(found);
            }
            at = parent;
        }
        Ok(None)
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
        let (id, meta, stale_universe) = self.record_universe(cx, folder, seen, report)?;
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
        self.visit_books_of(cx, folder, &id, seen, report)?;
        // Last, and in the same transaction as the works it points at: an order names works
        // by folder, and a step cannot be resolved against a work nobody has recorded yet.
        self.record_orders(cx, folder, &id, meta.as_ref(), &seen.editions, report)?;
        // Only now: every work of this universe has had its chance to carry off whatever it
        // collided with — see `make_way`.
        self.let_go(cx, "universe", stale_universe.as_deref())?;
        Ok(())
    }

    /// The archives sitting directly in a universe folder.
    ///
    /// Walking only its sub-folders, the scan skipped them in silence: a universe's book that
    /// is no series of it had to be given a folder made for nothing but holding it, and one
    /// dropped beside the works was on the disk and nowhere in the library. A book declaring
    /// itself whole is filed exactly as one lifted out of a work folder — `read_entry`, then
    /// `write_one_shot` — under a provisional edition that only lasts until it gets its own.
    /// Anything else belongs to no work, and guessing one would be inventing a series, so it
    /// is reported rather than read.
    fn visit_books_of(
        &self,
        cx: &Cx<'_>,
        folder: &Path,
        universe_id: &str,
        seen: &mut Seen,
        report: &mut ScanReport,
    ) -> Result<()> {
        let provisional = identity::under(universe_id, "edition");
        let within = Within {
            edition_id: &provisional,
            chapter_pattern: None,
        };
        for (order, file) in layout::archives(folder).iter().enumerate() {
            // What reading the file said of it as a volume — no metadata, a contradiction — is
            // only true of a book. An archive that is not one is reported once, as that.
            let before = report.clone();
            match self.read_entry(cx, file, order, &within, seen, report) {
                Ok(Some(album)) => {
                    seen.entries.remove(&album.id);
                    if album.one_shot.is_some() {
                        self.write_one_shot(cx, album, Some(universe_id), seen, report)?;
                    } else {
                        *report = before;
                        report.disregarded.push(format!(
                            "{}/{}: an archive sitting in a universe belongs to no work — only \
                             a book declaring itself whole with oneshot.json can sit there, so \
                             it is not read",
                            name_of(folder),
                            name_of(file)
                        ));
                    }
                }
                Ok(None) => {}
                Err(e) => {
                    report.errors.push(format!("{}: {e}", name_of(file)));
                    tracing::warn!(file = %file.display(), error = %e, "unreadable entry");
                }
            }
        }
        Ok(())
    }

    /// The named ways through this universe, rewritten whole.
    ///
    /// Derived and never authoritative: `universe.json` is the truth, these rows are what a
    /// query can reach. So they are deleted and written again rather than reconciled — a
    /// half-updated order is an order that reads wrong, where a rewritten one cannot.
    fn record_orders(
        &self,
        cx: &Cx<'_>,
        folder: &Path,
        universe_id: &str,
        meta: Option<&UniverseJson>,
        editions: &HashSet<String>,
        report: &mut ScanReport,
    ) -> Result<()> {
        cx.execute(
            "DELETE FROM reading_order WHERE universe_id = ?1",
            [universe_id],
        )?;
        let Some(meta) = meta else {
            return Ok(());
        };
        if meta.orders.is_empty() {
            return Ok(());
        }

        let here = name_of(folder);
        let mut declared_ids: HashSet<String> = HashSet::new();
        let mut said_order_ceiling = false;
        let plan = orders::Plan::new(&meta.orders);
        let mut budget = orders::Budget::new();
        let mut said_universe_ceiling = false;
        for (at, order) in meta.orders.iter().enumerate() {
            let declared = orders::declared_id(order, at);
            if !declared_ids.insert(declared.clone()) {
                report.contradictions.push(format!(
                    "{here}/universe.json — two orders are both called \"{declared}\"; the \
                     second is disregarded"
                ));
                continue;
            }

            // Under its universe, never under the folder's path: on the scan that moves a
            // library off path identities the universe gets a new one, so the `DELETE … WHERE
            // universe_id` above misses the old rows, and an order rebuilt under the same
            // path-derived identifier collides with the one still there.
            let order_id = identity::under(universe_id, &declared);
            let name = order
                .name
                .clone()
                .filter(|one| !one.trim().is_empty())
                .unwrap_or_else(|| declared.clone());
            let is_default = meta.default_order.as_deref() == Some(declared.as_str());
            cx.execute(
                "INSERT INTO reading_order (id, universe_id, declared_id, name, position,
                                            is_default)
                 VALUES (?1,?2,?3,?4,?5,?6)",
                rusqlite::params![
                    order_id,
                    universe_id,
                    declared,
                    name,
                    at as i64,
                    i64::from(is_default)
                ],
            )?;

            if at >= orders::MOST_ORDERS {
                if !said_order_ceiling {
                    said_order_ceiling = true;
                    report.contradictions.push(format!(
                        "{here}/universe.json — it holds more than {} orders; from order \
                         \"{declared}\" on they are kept without steps",
                        orders::MOST_ORDERS
                    ));
                }
                continue;
            }
            // Said once, by the order that writes the inclusion; the steps it would have
            // brought are not read again for every order that reaches this one.
            for ((_, step_at), why) in plan.refused.range((at, 0)..=(at, usize::MAX)) {
                report.disregarded.push(format!(
                    "{here}/universe.json — order \"{declared}\", step {}: {why}",
                    step_at + 1
                ));
            }
            let flat = match plan.flatten(at, &mut budget) {
                Ok(flat) => flat,
                Err(over) => {
                    // Said once for the universe: past its ceiling every later order is
                    // cut the same way, and a line each would be the file's size again.
                    if over == orders::Over::Order {
                        report.contradictions.push(format!(
                            "{here}/universe.json — order \"{declared}\" is too large once \
                             its inclusions are read in place (more than {} steps written, or \
                             {} looked at); it is kept without steps",
                            orders::MOST_FLAT_STEPS,
                            orders::MOST_STEPS_LOOKED_AT
                        ));
                    } else if !said_universe_ceiling {
                        said_universe_ceiling = true;
                        report.contradictions.push(format!(
                            "{here}/universe.json — its orders together are too large once \
                             their inclusions are read in place (more than {} steps written, \
                             or {} looked at); order \"{declared}\" and each later one that \
                             no longer fits are kept without steps",
                            orders::MOST_UNIVERSE_STEPS,
                            orders::MOST_UNIVERSE_LOOKS
                        ));
                    }
                    continue;
                }
            };
            let mut position = 0i64;
            for flat_step in flat {
                let complaint =
                    self.record_step(cx, folder, &order_id, position, flat_step.step, editions)?;
                match (complaint, flat_step.own) {
                    (Some(why), Some(own)) => report.disregarded.push(format!(
                        "{here}/universe.json — order \"{declared}\", step {}: {why}",
                        own + 1
                    )),
                    // The order that wrote the step has already said what is wrong with it.
                    (Some(_), None) => {}
                    (None, _) => position += 1,
                }
            }
        }

        if let Some(wanted) = meta.default_order.as_deref() {
            if !declared_ids.contains(wanted) {
                report.contradictions.push(format!(
                    "{here}/universe.json — defaultOrder is \"{wanted}\" and no order is \
                     called that; the first one is offered instead"
                ));
            }
        }
        Ok(())
    }

    /// One step, or the reason it was left out. Returning the complaint rather than pushing
    /// it here keeps the wording of "which order, which step" in one place.
    fn record_step(
        &self,
        cx: &Cx<'_>,
        folder: &Path,
        order_id: &str,
        position: i64,
        step: &StepJson,
        editions: &HashSet<String>,
    ) -> Result<Option<String>> {
        let Some(work) = step
            .work
            .as_deref()
            .map(str::trim)
            .filter(|w| !w.is_empty())
        else {
            return Ok(Some("no work named".to_string()));
        };

        // By path and not by hashing the folder name: a path that is not in the database is
        // a work this universe does not hold, which is exactly what has to be reported.
        let work_path = absolute(&folder.join(work));
        let Some(work_id) = cx.query_one(
            "SELECT id FROM work WHERE path = ?1",
            [work_path.as_str()],
            |r| r.get::<_, String>(0),
        )?
        else {
            return Ok(Some(format!("\"{work}\" is not a work of this universe")));
        };

        let unit = step
            .unit
            .as_deref()
            .map(str::trim)
            .filter(|u| !u.is_empty());
        let unit = match unit {
            None => None,
            Some(word) => {
                let upper = word.to_uppercase();
                if !UNITS.contains(&upper.as_str()) {
                    return Ok(Some(format!(
                        "\"{word}\" is not a unit; it is CHAPTER or VOLUME"
                    )));
                }
                Some(upper)
            }
        };

        let named_edition = step
            .edition
            .as_deref()
            .map(str::trim)
            .filter(|e| !e.is_empty());
        let edition_id =
            match (unit.as_deref(), named_edition) {
                // A range of volumes describes different content in a 42-volume edition and a
                // 34-volume one, so with several editions it has to say which. With one there is
                // nothing to choose between, and the edition beside the archives has no name to
                // write: refusing there turned « Lanfeust de Troy, tomes 1 à 4 » down for a
                // work that has exactly one way to count its volumes.
                (Some("VOLUME"), None) => {
                    // Only the editions this scan met: the row of one whose archives moved
                    // into a folder is pruned after the orders are written, and counted here it
                    // made a work with one edition look like a work with two.
                    let mut held: Vec<String> = cx
                        .query(
                            "SELECT id FROM edition WHERE work_id = ?1 AND one_shot = 0",
                            [work_id.as_str()],
                            |r| r.get::<_, String>(0),
                        )?
                        .into_iter()
                        .filter(|id| editions.contains(id))
                        .collect();
                    match (held.pop(), held.pop()) {
                        (Some(only), None) => Some(only),
                        _ => return Ok(Some(
                            "a VOLUME range must name its edition, because volume numbers differ \
                             between editions of the same work"
                                .to_string(),
                        )),
                    }
                }
                (Some("CHAPTER"), Some(_)) => {
                    return Ok(Some(
                        "a CHAPTER range must not name an edition: a chapter number identifies \
                         the same story in every edition"
                            .to_string(),
                    ))
                }
                (_, None) => None,
                (_, Some(edition)) => {
                    let path = absolute(&folder.join(work).join(edition));
                    let found = cx.query_one(
                        "SELECT id FROM edition WHERE path = ?1 AND work_id = ?2",
                        rusqlite::params![path.as_str(), work_id.as_str()],
                        |r| r.get::<_, String>(0),
                    )?;
                    match found {
                        Some(id) => Some(id),
                        None => {
                            return Ok(Some(format!(
                                "\"{edition}\" is not an edition of \"{work}\""
                            )))
                        }
                    }
                }
            };

        if let (Some(from), Some(to)) = (step.from, step.to) {
            if from > to {
                return Ok(Some(format!(
                    "the range runs from {from} to {to}, which is backwards"
                )));
            }
        }

        cx.execute(
            "INSERT INTO reading_order_step (id, order_id, position, work_id, unit,
                                             edition_id, from_number, to_number)
             VALUES (?1,?2,?3,?4,?5,?6,?7,?8)",
            rusqlite::params![
                identity::under(order_id, &position.to_string()),
                order_id,
                position,
                work_id,
                unit,
                edition_id,
                step.from,
                step.to
            ],
        )?;
        Ok(None)
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
        let id = identity::of(
            folder,
            layout::WORK_FILE,
            &id_of(folder, ""),
            &mut seen.claimed,
            report,
        );
        let meta: Option<WorkJson> = read_json(folder, layout::WORK_FILE);
        seen.works.insert(id.clone());
        let stale_work = self.make_way(cx, "work", &id, &absolute(folder))?;

        cx.execute(
            "INSERT INTO work (id, universe_id, name, path, title, medium, status,
                               reading_direction, summary, age_rating, age,
                               declared_authors, declared_artists)
             VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13)
             ON CONFLICT(id) DO UPDATE SET
               -- The place follows the identity. It did not have to before: a folder
               -- that moved got a new identity, so it got a new row, and this one was
               -- pruned. Now the identity is in the sidecar and travels with the
               -- folder, so the row that survives is the one holding the old path —
               -- and every route that reaches a work by its path would reach nothing.
               path=excluded.path,
               universe_id=excluded.universe_id, name=excluded.name, title=excluded.title,
               medium=excluded.medium, status=excluded.status,
               reading_direction=excluded.reading_direction, summary=excluded.summary,
               age_rating=excluded.age_rating, age=excluded.age,
               declared_authors=excluded.declared_authors,
               declared_artists=excluded.declared_artists",
            rusqlite::params![
                id,
                universe_id,
                name_of(folder),
                absolute(folder),
                meta.as_ref().and_then(|m| m.title.clone()),
                meta.as_ref().and_then(|m| m.medium.clone()),
                meta.as_ref().and_then(|m| m.status.clone()),
                meta.as_ref().and_then(|m| m.reading_direction.clone()),
                meta.as_ref().and_then(|m| m.summary.clone()),
                meta.as_ref().and_then(|m| m.age_rating.clone()),
                // Beside the rating, from it, in the same statement: a work whose rating
                // changed and whose age did not would be shown to a child on the old one.
                meta.as_ref()
                    .and_then(|m| m.age_rating.as_deref())
                    .and_then(age_of),
                declared(&meta.as_ref().map(WorkJson::authors).unwrap_or_default()),
                declared(&meta.as_ref().map(|m| m.artists.clone()).unwrap_or_default()),
            ],
        )?;
        report.works += 1;
        // Said at this place in the report, but only once the volumes have been read: a series
        // whose credits are written volume by volume is not missing them.
        let work_report_at = report.missing_required.len();
        self.record_genres(
            cx,
            &id,
            &meta.as_ref().map(|m| m.genres.clone()).unwrap_or_default(),
        )?;
        self.record_tags(
            cx,
            &id,
            &meta.as_ref().map(|m| m.tags.clone()).unwrap_or_default(),
        )?;

        // A single-edition work has no edition folder: its edition fields live in work.json
        // and come down here as defaults.
        let defaults = WorkDefaults {
            status: meta.as_ref().and_then(|m| m.status.clone()),
            publisher: meta.as_ref().and_then(|m| m.publisher.clone()),
            collection: meta.as_ref().and_then(|m| m.collection.clone()),
            volume_count: meta.as_ref().and_then(|m| m.volume_count),
            format: meta.as_ref().and_then(|m| m.format.clone()),
            language: meta.as_ref().and_then(|m| m.language.clone()),
            medium: meta.as_ref().and_then(|m| m.medium.clone()),
            reading_direction: meta.as_ref().and_then(|m| m.reading_direction.clone()),
            colour: meta.as_ref().and_then(|m| m.colour),
            cover_volume: meta.as_ref().and_then(|m| m.cover_volume),
            chapter_label: meta.as_ref().and_then(|m| m.chapter_label.clone()),
            arcs: meta.as_ref().map(|m| m.arcs.clone()).unwrap_or_default(),
            authors: meta.as_ref().map(WorkJson::authors).unwrap_or_default(),
            artists: meta.as_ref().map(|m| m.artists.clone()).unwrap_or_default(),
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

        let said_before = report.missing_required.len();
        let mut inherited: Vec<Inherited> = Vec::new();
        let mut albums: Vec<ReadEntry> = Vec::new();
        let mut credited: Vec<Credited> = Vec::new();
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
            let visited = self.visit_edition(
                cx, folder, &id, true, &direct, &defaults, false, seen, report,
            )?;
            inherited.push(visited.inherited);
            albums.extend(visited.lifted);
            credited.push(visited.credited);
        }
        for edition in &edition_folders {
            let files = layout::archives(edition);
            let visited = self.visit_edition(
                cx, edition, &id, false, &files, &defaults, several, seen, report,
            )?;
            inherited.push(visited.inherited);
            albums.extend(visited.lifted);
            credited.push(visited.credited);
        }

        // Filed after the work they came out of, because their rows are made from what that
        // pass read.
        let said_by_visits = report.missing_required.len();
        let lifted_any = !albums.is_empty();
        for album in albums {
            self.write_one_shot(cx, album, universe_id, seen, report)?;
        }

        // A folder whose archives *all* declared themselves whole books is not a work: it is
        // a shelf somebody made to tidy up, and `BD/` holding two albums is two books rather
        // than a two-volume series named after a folder. It had to be recorded before they
        // could be read — the declaration is inside the files — so it is unmade here, now
        // that they have spoken. Only when something was lifted: a work that legitimately
        // holds nothing is a different fact, and `holds_archives` already turned it away.
        if lifted_any {
            let holds: i64 = cx
                .query_one(
                    "SELECT COUNT(*) FROM entry x JOIN edition d ON d.id = x.edition_id
                     WHERE d.work_id = ?1",
                    [id.as_str()],
                    |r| r.get(0),
                )?
                .unwrap_or(0);
            if holds == 0 {
                let emptied = cx.query(
                    "SELECT id FROM edition WHERE work_id = ?1",
                    [id.as_str()],
                    |r| r.get::<_, String>(0),
                )?;
                for edition in &emptied {
                    seen.editions.remove(edition);
                }
                report.editions = report.editions.saturating_sub(emptied.len() as u32);
                // The editions go with it, and so does anything hanging off them.
                cx.execute("DELETE FROM work WHERE id = ?1", [id.as_str()])?;
                seen.works.remove(&id);
                report.works = report.works.saturating_sub(1);
                // What the visits said about its editions was said about a work that is not one.
                report.missing_required.drain(said_before..said_by_visits);
                self.let_go(cx, "work", stale_work.as_deref())?;
                return Ok(());
            }
        }

        // Who wrote and who drew: everybody its volumes are credited with, once they have
        // taken the series' default. `work.json` naming someone used to replace this union
        // outright, and Lucky Luke credited to its first team hid every writer after them.
        // The default alone only for a series with no volume to credit. Recorded once, here,
        // because the editions' search entries below are made from the same lists and a
        // search must find what is recorded.
        let or_declared = |found: Vec<String>, declared: &[String]| {
            if found.is_empty() {
                declared.to_vec()
            } else {
                found
            }
        };
        let authors = or_declared(
            union(credited.iter().map(|c| c.authors.clone())),
            &defaults.authors,
        );
        let artists = or_declared(
            union(credited.iter().map(|c| c.artists.clone())),
            &defaults.artists,
        );
        self.record_authors(cx, &id, &authors)?;
        self.record_artists(cx, &id, &artists)?;
        let work_report = checks::work(
            &name_of(folder),
            meta.as_ref(),
            credited.iter().all(|c| c.every_authored),
            credited.iter().all(|c| c.every_drawn),
        );
        report
            .missing_required
            .splice(work_report_at..work_report_at, work_report);
        for found in credited {
            let mut extra = found.search.genres;
            extra.extend(authors.iter().cloned());
            extra.extend(artists.iter().cloned());
            extra.extend(found.search.rest);
            self.index(
                cx,
                "EDITION",
                &found.search.id,
                Some(&found.search.id),
                None,
                &found.search.names,
                &extra,
            )?;
        }

        // Whatever work.json does not say yet, we take from what is still in the files.
        if let Some(found) = inherited
            .into_iter()
            .find(|i| i.reading_direction.is_some() || !i.genres.is_empty())
        {
            cx.execute(
                "UPDATE work SET reading_direction = COALESCE(reading_direction, ?1) WHERE id = ?2",
                rusqlite::params![found.reading_direction, id],
            )?;
            // Genres go the same way as the rest — the files first, the legacy metadata
            // only when work.json is silent. They used to take a second route, into a
            // column of their own, and so were shown without ever being filterable.
            let declared_genres = meta.as_ref().map(|m| m.genres.clone()).unwrap_or_default();
            if declared_genres.is_empty() && !found.genres.is_empty() {
                self.record_genres(cx, &id, &found.genres)?;
            }
        }
        // Only now: every edition and entry this work holds has had its chance to carry its
        // reading position off whatever it collided with — see `make_way`.
        self.let_go(cx, "work", stale_work.as_deref())?;
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
    ) -> Result<Visited> {
        // An implicit edition has no folder of its own, so it has nowhere to keep an
        // identifier: it takes its work's, which a rename of the folder no longer changes.
        let id = if implicit {
            identity::under(work_id, "edition")
        } else {
            identity::of(
                folder,
                layout::EDITION_FILE,
                &id_of(folder, "edition"),
                &mut seen.claimed,
                report,
            )
        };
        let meta: Option<EditionJson> = read_json(folder, layout::EDITION_FILE);
        seen.editions.insert(id.clone());
        let stale_edition = self.make_way(cx, "edition", &id, &absolute(folder))?;

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
                                  cover_file, reading_direction, volume_count, format, language,
                                  collection, colour, summary, cover_volume)
             VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,?16,?17)
             ON CONFLICT(id) DO UPDATE SET
               path=excluded.path,
               work_id=excluded.work_id, name=excluded.name, implicit=excluded.implicit,
               publisher=excluded.publisher, status=excluded.status, medium=excluded.medium,
               cover_file=excluded.cover_file, reading_direction=excluded.reading_direction,
               volume_count=excluded.volume_count, format=excluded.format, language=excluded.language,
               collection=excluded.collection, colour=excluded.colour,
               summary=excluded.summary, cover_volume=excluded.cover_volume",
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
                or_default(
                    meta.as_ref().and_then(|m| m.collection.clone()),
                    &defaults.collection
                ),
                meta.as_ref().and_then(|m| m.colour).or(defaults.colour),
                // No default from the work: an edition that says nothing about itself falls
                // back to the work's summary at the moment it is read, not by having the
                // work's copied into its row. Copied, the day somebody edits the work the
                // editions would go on saying what it used to say.
                meta.as_ref().and_then(|m| m.summary.clone()),
                meta.as_ref().and_then(|m| m.cover_volume).or(defaults.cover_volume),
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
        // everything a reader half-remembers is indexed here — the work's title, its writers
        // and illustrators, the universe, the genres and tags, the summary. The name column
        // ranks tenfold; the rest sits in the low-weighted one, where a word met in a summary
        // never outranks the same word in a title.
        //
        // Authors, artists and tags join this the same way genres already did: as rows, not
        // as a column, so a name buried in a list of thirteen illustrators is still a name
        // this can find. Searching "Obata" and getting nothing on Death Note — its artist,
        // credited nowhere a search could reach — is the failure this exists to fix.
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
        let genres: Vec<String> = work.map(|w| w.genres.clone()).unwrap_or_default();
        let mut rest: Vec<String> = work.map(|w| w.tags.clone()).unwrap_or_default();
        rest.extend(
            [
                work.and_then(|w| w.summary.clone()),
                or_default(
                    meta.as_ref().and_then(|m| m.publisher.clone()),
                    &defaults.publisher,
                ),
            ]
            .into_iter()
            .flatten(),
        );
        // Not written yet: the people it names are the series' credits, which are known
        // only once every edition has been read. One write per edition, as before.
        let search = EditionSearch {
            id: id.clone(),
            names,
            genres,
            rest,
        };

        let pattern = meta
            .as_ref()
            .and_then(|m| m.chapter_label.clone())
            .or_else(|| defaults.chapter_label.clone());

        let mut sorted = files.to_vec();
        sorted.sort();
        let mut entries: Vec<ReadEntry> = Vec::new();
        for (order, file) in sorted.iter().enumerate() {
            let within = Within {
                edition_id: &id,
                chapter_pattern: pattern.as_deref(),
            };
            match self.read_entry(cx, file, order, &within, seen, report) {
                Ok(Some(entry)) => entries.push(entry),
                Ok(None) => {}
                Err(e) => {
                    report.errors.push(format!("{}: {e}", name_of(file)));
                    tracing::warn!(file = %file.display(), error = %e, "unreadable entry");
                }
            }
        }
        // A file that declares itself a whole book is not an entry of this edition. It is
        // taken out before anything is written or checked against a work it does not belong
        // to — its own title would be reported as naming the wrong series, and its page
        // count would be counted into a series it is not part of.
        let (lifted, mut entries): (Vec<ReadEntry>, Vec<ReadEntry>) =
            entries.into_iter().partition(|e| e.one_shot.is_some());
        // After the partition and not in `read_entry`: a book standing alone in the folder
        // never was one of the series' volumes, and the series' writer is not its writer.
        for e in &mut entries {
            e.effective = Credits::of(&e.credits, defaults);
        }
        for album in &lifted {
            // The identity `read_entry` gave it belongs to this edition, and it is about to
            // get one of its own. Left in, the prune would spare a row nobody wrote.
            seen.entries.remove(&album.id);
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
        // Outside the shortcut below, always: an edition whose files are all as they were
        // still changes credits when only `work.json` did.
        self.credit_entries(cx, &id, &entries)?;

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

        // Settled files are not yet settled chapters: what a chapter is called also comes
        // from the edition's pattern, and from the default one, neither of which is in the
        // archive. The default changed once — from what the file name held, « #13 », to
        // « Chap. 13 » — and a library whose files had not moved kept « #13 » through every
        // rescan. So the names stored are read back, in the one statement that used to only
        // count them, and the chapters are rewritten when one differs from what was drafted.
        let kept = if settled {
            Some(cx.query(
                "SELECT id, raw, label FROM chapter WHERE edition_id = ?1",
                [id.as_str()],
                |r| {
                    Ok((
                        r.get::<_, String>(0)?,
                        (r.get::<_, String>(1)?, r.get::<_, String>(2)?),
                    ))
                },
            )?)
        } else {
            None
        };
        match kept {
            Some(kept) if named_as_drafted(&kept, &entries) => {
                // Still counted, or the report would announce zero chapters for a library
                // that has fifty thousand — the summary describes the library, not the work
                // done.
                report.chapters += kept.len() as u32;
            }
            _ => self.write_chapters(cx, &id, &entries, report)?,
        }
        // The edition's own arcs, or the work's when it declares none: chosen here, so that
        // `write_arcs` is handed one list **and the file it came from**.
        //
        // The name travelled apart from the list and was the edition's either way, which is
        // the one thing it must not be: a `tome` typed in the work.json of Terres d'Arran was
        // reported against `Elfes`, then against `Mages` — twice, and each time naming an
        // edition.json that has no `arcs` in it at all. A report that sends somebody to the
        // wrong file is worse than one that says nothing.
        let edition_name = name_of(folder);
        let (declared, where_): (&[ArcJson], &str) = match meta.as_ref().map(|m| m.arcs.as_slice())
        {
            Some(arcs) if !arcs.is_empty() => (arcs, &edition_name),
            _ => (&defaults.arcs, &defaults.work_name),
        };
        self.write_arcs(cx, &id, where_, declared, &entries, report)?;

        // Only now: `write_entries` above is what gives every entry still on disk its
        // chance to carry its reading position off the id this edition collided with — see
        // `make_way`. An entry that did not get that chance because its file is gone goes
        // with this row, the same loss a complete sweep's prune would otherwise report.
        self.let_go(cx, "edition", stale_edition.as_deref())?;

        Ok(Visited {
            inherited: entries
                .iter()
                .find_map(|e| e.inherited.clone())
                .unwrap_or_default(),
            lifted,
            credited: Credited {
                authors: union(entries.iter().map(|e| e.effective.authors.clone())),
                artists: union(entries.iter().map(|e| e.effective.artists.clone())),
                every_authored: entries.iter().all(|e| !e.effective.authors.is_empty()),
                every_drawn: entries.iter().all(|e| !e.effective.artists.is_empty()),
                search,
            },
        })
    }

    /// Files an archive that declares itself a whole book: its own work, its own implicit
    /// edition, and the single entry it holds.
    ///
    /// Three rows for one file, and none of them ceremony. Where a reader stands is a
    /// position in an *entry* belonging to an *edition* — `progress` says so in its own
    /// foreign keys — and the shelf, the search index, the filters and every count speak
    /// edition. A book that were none of those would need a second road to each, and two
    /// roads to one place stop agreeing.
    ///
    /// The implicit edition is not invented here either: it already exists for a work with
    /// no edition folder. An album is that, one level further down.
    fn write_one_shot(
        &self,
        cx: &Cx<'_>,
        mut album: ReadEntry,
        universe_id: Option<&str>,
        seen: &mut Seen,
        report: &mut ScanReport,
    ) -> Result<()> {
        let Some(meta) = album.one_shot.clone() else {
            return Ok(());
        };
        let file = album.path.clone();
        let work_id = id_of(&file, "");
        let edition_id = identity::under(&work_id, "edition");

        // Re-keyed under the edition it is actually getting. `read_entry` could not know:
        // the declaration is inside the archive, and reading it twice to find out would be
        // a second open of every file in the library.
        album.id = identity::under(&edition_id, &name_of(&file));
        album.unchanged = album.held.as_ref().is_some_and(|(held, size, at)| {
            *held == album.id && *size == album.size && *at == album.modified_at
        });
        album.stale = album
            .held
            .clone()
            .map(|(held, _, _)| held)
            .filter(|held| *held != album.id);

        let title = meta
            .title
            .clone()
            .filter(|t| !t.trim().is_empty())
            .unwrap_or_else(|| name_of(&file));

        seen.works.insert(work_id.clone());
        let stale_work = self.make_way(cx, "work", &work_id, &absolute(&file))?;
        cx.execute(
            "INSERT INTO work (id, universe_id, name, path, title, medium, status,
                               reading_direction, summary, age_rating, age,
                               declared_authors, declared_artists)
             VALUES (?1,?2,?3,?4,?5,?6,'completed',?7,?8,?9,?10,?11,?12)
             ON CONFLICT(id) DO UPDATE SET
               path=excluded.path, universe_id=excluded.universe_id, name=excluded.name,
               title=excluded.title, medium=excluded.medium, status=excluded.status,
               reading_direction=excluded.reading_direction, summary=excluded.summary,
               age_rating=excluded.age_rating, age=excluded.age,
               declared_authors=excluded.declared_authors,
               declared_artists=excluded.declared_artists",
            rusqlite::params![
                work_id,
                universe_id,
                title,
                absolute(&file),
                title,
                meta.medium.clone(),
                meta.reading_direction.clone(),
                meta.summary.clone(),
                meta.age_rating.clone(),
                meta.age_rating.as_deref().and_then(age_of),
                // A book declares its own credits in `oneshot.json`, which is its `work.json`.
                declared(&meta.authors()),
                declared(&meta.artists),
            ],
        )?;
        report.works += 1;
        self.record_genres(cx, &work_id, &meta.genres)?;
        self.record_authors(cx, &work_id, &meta.authors())?;
        self.record_artists(cx, &work_id, &meta.artists)?;
        self.record_tags(cx, &work_id, &meta.tags)?;

        seen.editions.insert(edition_id.clone());
        let stale_edition = self.make_way(cx, "edition", &edition_id, &absolute(&file))?;
        cx.execute(
            "INSERT INTO edition (id, work_id, name, path, implicit, one_shot, publisher,
                                  status, medium, cover_file, reading_direction,
                                  volume_count, language, collection, colour)
             VALUES (?1,?2,NULL,?3,1,1,?4,'completed',?5,?6,?7,1,?8,?9,?10)
             ON CONFLICT(id) DO UPDATE SET
               path=excluded.path, work_id=excluded.work_id, implicit=excluded.implicit,
               one_shot=excluded.one_shot, publisher=excluded.publisher,
               status=excluded.status, medium=excluded.medium,
               cover_file=excluded.cover_file,
               reading_direction=excluded.reading_direction,
               volume_count=excluded.volume_count, language=excluded.language,
               collection=excluded.collection, colour=excluded.colour",
            rusqlite::params![
                edition_id,
                work_id,
                absolute(&file),
                meta.publisher.clone(),
                meta.medium.clone(),
                album.cover_file.clone(),
                meta.reading_direction.clone(),
                meta.language.clone(),
                meta.collection.clone(),
                meta.colour.map(i64::from),
            ],
        )?;
        report.editions += 1;

        let mut extra: Vec<String> = meta.genres.clone();
        extra.extend(meta.authors());
        extra.extend(meta.artists.clone());
        extra.extend(meta.tags.clone());
        extra.extend(
            [meta.summary.clone(), meta.publisher.clone()]
                .into_iter()
                .flatten(),
        );
        self.index(
            cx,
            "EDITION",
            &edition_id,
            Some(&edition_id),
            None,
            &[title],
            &extra,
        )?;

        seen.entries.insert(album.id.clone());
        // The book's own file credits it, and nothing above it does.
        album.effective = Credits::own(&(meta.authors(), meta.artists.clone()));
        let one = [album];
        self.write_entries(cx, &edition_id, &one, report)?;
        self.credit_entries(cx, &edition_id, &one)?;
        self.write_chapters(cx, &edition_id, &one, report)?;

        self.let_go(cx, "edition", stale_edition.as_deref())?;
        self.let_go(cx, "work", stale_work.as_deref())?;
        Ok(())
    }

    // ----------------------------------------------------------------- entries

    fn read_entry(
        &self,
        cx: &Cx<'_>,
        file: &Path,
        order: usize,
        within: &Within<'_>,
        seen: &mut Seen,
        report: &mut ScanReport,
    ) -> Result<Option<ReadEntry>> {
        let Within {
            edition_id,
            chapter_pattern,
        } = *within;
        let meta = std::fs::metadata(file)?;
        let size = meta.len() as i64;
        let modified_at = meta
            .modified()?
            .duration_since(std::time::UNIX_EPOCH)
            .map(|d| d.as_millis() as i64)
            .unwrap_or(0);

        // Asked by **file** and not by identity, which is what makes one query answer two
        // questions: whether this file changed since it was last read, and — when the row
        // holding it is filed under another identity — which identity that is. `file` is
        // UNIQUE, so there is at most one such row, and it is where this file's reading
        // position is.
        let row = cx.query_one(
            "SELECT id, size, modified_at, edition_id FROM entry WHERE file = ?1",
            [absolute(file).as_str()],
            |r| {
                Ok((
                    r.get::<_, String>(0)?,
                    r.get::<_, i64>(1)?,
                    r.get::<_, i64>(2)?,
                    r.get::<_, String>(3)?,
                ))
            },
        )?;
        let held = row.clone().map(|(id, size, at, _)| (id, size, at));

        // The row that holds this file **is** this volume, whatever `under(edition, name)`
        // would say of it now: the server renames a file and moves `entry.file` in the same
        // step, and a row found by its file keeps the identity it has. A file with no row is a
        // new volume and gets one drawn once (see [`identity::minted`]) — nothing is written
        // into an archive to hold it, and the name stops being what the volume is.
        //
        // **In the same edition only.** A row found in another edition is the case the
        // identity change of a folder produces — the first scan after the identifiers stopped
        // being paths re-keys every edition at once — and its reading position hangs off the
        // edition it was filed under: it is carried by `stale`, as it always was.
        let id = match &row {
            Some((id, _, _, held_edition)) if held_edition == edition_id => id.clone(),
            Some(_) => identity::under(edition_id, &name_of(file)),
            None => identity::minted(),
        };
        seen.entries.insert(id.clone());
        let unchanged = held
            .as_ref()
            .is_some_and(|(held, s, m)| *held == id && *s == size && *m == modified_at);
        let stale = held
            .clone()
            .map(|(held, _, _)| held)
            .filter(|held| *held != id);

        let content = cbz::read(file, self.all_dimensions && !unchanged)?;
        let one_shot: Option<OneShotJson> = content
            .sidecar(ONESHOT_JSON)
            .and_then(crate::metadata::sidecars::read);
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

        let cover_page = own
            .as_ref()
            .and_then(|o| o.cover_page)
            .filter(|p| (0..content.pages.len() as i32).contains(p));

        let credits = (
            credit(
                own.as_ref().map(|o| o.authors.as_slice()),
                legacy
                    .as_ref()
                    .map(|l| l.author.as_slice())
                    .unwrap_or_default(),
            ),
            credit(
                own.as_ref().map(|o| o.artists.as_slice()),
                legacy
                    .as_ref()
                    .map(|l| l.artist.as_slice())
                    .unwrap_or_default(),
            ),
        );

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
            stale,
            file_order: order,
            cover_file: covers::beside_archive(file).map(|p| p.to_string_lossy().to_string()),
            cover_page,
            declared: own.or_else(|| legacy.as_ref().map(|l| l.entry.clone())),
            one_shot,
            held,
            legacy_arc: legacy.as_ref().and_then(|l| l.arc.clone()),
            effective: Credits::own(&credits),
            credits,
            inherited: legacy.as_ref().map(|l| Inherited {
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
        //
        // A numbered chapter that nothing gives a pattern to takes the default one rather
        // than what its file name held: « #13 » named nothing, « Chap. 13 » does.
        let pattern = pattern
            .filter(|p| !p.trim().is_empty())
            .unwrap_or(label::DEFAULT_CHAPTER_LABEL);
        let composed = label::compose(Some(pattern), number);
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

    /// Clears the way for a row about to describe a place another row still describes, and
    /// returns that row's id so the caller can remove it once it is safe to.
    ///
    /// A folder changes level — `Elfes` holds volumes and is a work, then a `work.json`
    /// appears on the folder above it and `Elfes` becomes one of its editions. The two
    /// identities used to be the same hash of the same path; they are two different things
    /// now, and `path` is UNIQUE, so the insert fails. The commonest cause is not a folder
    /// changing level, though: it is the migration off path identities, where every row of
    /// an unmigrated library collides on exactly this path with the identity its sidecar
    /// now carries.
    ///
    /// **Parked here, deleted by [`Scanner::let_go`] — not "never deleted".** Deleting it on
    /// the spot would take its entries with it, and `ON DELETE CASCADE` would take their
    /// reading positions before the scan has had the chance to carry them off. So its path
    /// becomes its own id — sixteen hexadecimal characters, which no folder is — for exactly
    /// as long as it takes the caller to finish with what it held, and `let_go` removes it
    /// before this transaction commits.
    ///
    /// It used to stay parked instead, on the theory that "nothing meets it again": the
    /// prune that finally removed it ran only at the end of a *complete* sweep, and an aimed
    /// rescan (`prune_within`) never called it at all. Measured, on a library still indexed
    /// by path and rescanned one work at a time: the index held two works, two editions and
    /// two entries for one folder, one of the `work` rows carrying `path =
    /// "45fee9857437a8eb"`. `Intake::file` read that column as a real path, as every other
    /// caller does, and filed a volume into the server's own working directory.
    fn make_way(&self, cx: &Cx<'_>, table: &str, id: &str, path: &str) -> Result<Option<String>> {
        let stale: Option<String> = cx.query_one(
            &format!("SELECT id FROM {table} WHERE path = ?1 AND id <> ?2"),
            rusqlite::params![path, id],
            |r| r.get(0),
        )?;
        if let Some(stale_id) = &stale {
            cx.execute(
                &format!("UPDATE {table} SET path = id WHERE id = ?1"),
                [stale_id.as_str()],
            )?;
        }
        Ok(stale)
    }

    /// Removes a row [`Scanner::make_way`] parked, now that whatever it held has had its
    /// chance to move off it. `None` is the ordinary case — no collision, nothing to do.
    fn let_go(&self, cx: &Cx<'_>, table: &str, stale: Option<&str>) -> Result<()> {
        let Some(id) = stale else { return Ok(()) };
        cx.execute(&format!("DELETE FROM {table} WHERE id = ?1"), [id])?;
        Ok(())
    }

    /// Moves a reading position from an identity a file used to have onto the one it has
    /// now.
    ///
    /// **During the scan, not after it.** At the moment the scan meets a file it knows both
    /// identities — the one its path would give, which is the one the index holds, and the
    /// one it now has. After the scan there is nothing left to match them by: the prune has
    /// removed the old row and `ON DELETE CASCADE` has taken the position with it. This is
    /// the one place in this change where doing nothing would be better than doing it late.
    ///
    /// Idempotent: a second scan finds nothing filed under the old identity and moves
    /// nothing. `OR IGNORE` rather than `OR REPLACE`, because a position already filed under
    /// the new identity is the more recent of the two, and the old row is about to go.
    fn reattach(
        &self,
        cx: &Cx<'_>,
        was: &str,
        now: &str,
        edition_id: &str,
        report: &mut ScanReport,
    ) -> Result<()> {
        let moved = cx.execute(
            "UPDATE OR IGNORE progress SET entry_id = ?1, edition_id = ?2 WHERE entry_id = ?3",
            rusqlite::params![now, edition_id, was],
        )?;
        report.progress_carried += moved as u32;
        Ok(())
    }

    /// Which new entry is an old one under another file name, as `new id → old id`.
    ///
    /// An entry is « this file, in this edition » (see [`identity::under`]), so a CBZ renamed
    /// is a new entry and the prune would delete the old one with its reading position.
    /// Renaming 190 volumes to fix their titles would have taken 190 positions with it.
    ///
    /// Matched by the one thing a rename does not change, the size in bytes, and only when
    /// it is unambiguous on both sides: one row of this edition whose file is no longer
    /// listed, one new file of that size. Two volumes of the same size, renamed together, are
    /// left alone — a position carried onto the wrong volume is worse than one reported lost.
    fn renamed(
        &self,
        cx: &Cx<'_>,
        edition_id: &str,
        entries: &[ReadEntry],
    ) -> Result<HashMap<String, String>> {
        let listed: HashSet<&str> = entries.iter().map(|e| e.id.as_str()).collect();
        let held = cx.query(
            "SELECT id, size, file FROM entry WHERE edition_id = ?1",
            [edition_id],
            |r| {
                Ok((
                    r.get::<_, String>(0)?,
                    r.get::<_, i64>(1)?,
                    r.get::<_, String>(2)?,
                ))
            },
        )?;
        let known: HashSet<&str> = held.iter().map(|(id, _, _)| id.as_str()).collect();

        let base = |path: &str| path.rsplit('/').next().unwrap_or(path).to_string();
        let mut gone: Vec<(&str, i64, String)> = Vec::new();
        for (id, size, file) in &held {
            if !listed.contains(id.as_str()) {
                gone.push((id, *size, base(file)));
            }
        }
        let arrived: Vec<&ReadEntry> = entries
            .iter()
            .filter(|e| e.stale.is_none() && !known.contains(e.id.as_str()))
            .collect();

        // **By name first.** A folder renamed by hand moves every `file` at once and leaves
        // each volume's own name alone, so a row whose name is the name of exactly one new
        // file is that file — which no size can say of two volumes that weigh the same.
        let mut by_name: HashMap<String, (Vec<&str>, Vec<&str>)> = HashMap::new();
        for (id, _, name) in &gone {
            by_name.entry(name.clone()).or_default().0.push(id);
        }
        for e in &arrived {
            by_name
                .entry(base(&e.path.to_string_lossy()))
                .or_default()
                .1
                .push(&e.id);
        }
        let mut matched: HashMap<String, String> = HashMap::new();
        for (old, new) in by_name.values() {
            if let ([old], [new]) = (old.as_slice(), new.as_slice()) {
                matched.insert(new.to_string(), old.to_string());
            }
        }

        // **Then by size**, for a file renamed itself: only when it is unambiguous on both
        // sides, one row of this edition whose file is no longer listed, one new file of
        // that size.
        let taken: HashSet<&str> = matched.values().map(String::as_str).collect();
        let mut gone_by_size: HashMap<i64, Vec<&str>> = HashMap::new();
        for (id, size, _) in &gone {
            if !taken.contains(id) {
                gone_by_size.entry(*size).or_default().push(id);
            }
        }
        let mut arrived_by_size: HashMap<i64, Vec<&str>> = HashMap::new();
        for e in &arrived {
            if !matched.contains_key(e.id.as_str()) {
                arrived_by_size.entry(e.size).or_default().push(&e.id);
            }
        }
        for (size, new) in arrived_by_size {
            if let (&[new], Some(&[old])) =
                (new.as_slice(), gone_by_size.get(&size).map(Vec::as_slice))
            {
                matched.insert(new.to_string(), old.to_string());
            }
        }
        Ok(matched)
    }

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

        let renamed = self.renamed(cx, edition_id, entries)?;

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

            // The row that holds this file is about to be replaced by one under another
            // identity, and `file` is UNIQUE. Moved aside rather than deleted, because
            // deleting it takes the reading position with it before the line below has
            // carried it — see `make_way`.
            if e.stale.is_some() {
                cx.execute(
                    "UPDATE entry SET file = id WHERE file = ?1 AND id <> ?2",
                    rusqlite::params![absolute(&e.path), e.id],
                )?;
            }

            cx.execute(
                "INSERT INTO entry (id, edition_id, type, file, size, modified_at, added_at,
                                    cover_file, cover_page, volume_number, title, sort_key,
                                    page_count, isbn, published_on, summary,
                                    authors_inherited, artists_inherited)
                 VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,?16,?17,?18)
                 ON CONFLICT(id) DO UPDATE SET
                   -- The file follows the identity, for the reason `work` gives above: a
                   -- folder renamed keeps its entries, and they are still in it.
                   file=excluded.file,
                   edition_id=excluded.edition_id, type=excluded.type, size=excluded.size,
                   modified_at=excluded.modified_at,
                   -- Set once, on arrival, and never touched again: it is the one fact a
                   -- rescan could not reconstruct.
                   added_at=COALESCE(entry.added_at, excluded.added_at),
                   cover_file=excluded.cover_file, cover_page=excluded.cover_page,
                   volume_number=excluded.volume_number,
                   title=excluded.title, sort_key=excluded.sort_key,
                   page_count=excluded.page_count, isbn=excluded.isbn,
                   published_on=excluded.published_on, summary=excluded.summary,
                   authors_inherited=excluded.authors_inherited,
                   artists_inherited=excluded.artists_inherited",
                rusqlite::params![
                    e.id,
                    edition_id,
                    e.kind,
                    absolute(&e.path),
                    e.size,
                    e.modified_at,
                    arrived_at,
                    e.cover_file,
                    e.cover_page,
                    e.volume_number,
                    e.title,
                    e.sort_key(),
                    page_count,
                    e.isbn,
                    e.published_on,
                    e.summary,
                    e.effective.authors_inherited,
                    e.effective.artists_inherited,
                ],
            )?;
            report.entries += 1;

            // After the row exists and before the prune removes the old one: the position
            // points at an entry, so it cannot be moved onto one that is not there yet, and
            // what it is moved off is about to be deleted with the position still on it.
            if let Some(stale) = &e.stale {
                self.reattach(cx, stale, &e.id, edition_id, report)?;
            }
            if let Some(was) = renamed.get(&e.id) {
                self.reattach(cx, was, &e.id, edition_id, report)?;
                // The date it arrived is as much the old file's as its position is: a
                // renamed volume is not a new one, and `added_at` is the one fact a rescan
                // could not reconstruct.
                cx.execute(
                    "UPDATE entry SET added_at = COALESCE( \
                       (SELECT added_at FROM entry WHERE id = ?2), added_at) WHERE id = ?1",
                    rusqlite::params![e.id, was],
                )?;
            }

            let names: Vec<String> = [e.title.clone(), Some(stem_of(&e.path))]
                .into_iter()
                .flatten()
                .collect();
            // The people who made it, inherited ones included: a volume was found by its
            // title and nothing else, so « Oda » found One Piece and none of its volumes.
            let people: Vec<String> = e
                .effective
                .authors
                .iter()
                .chain(&e.effective.artists)
                .cloned()
                .collect();
            self.index(
                cx,
                "ENTRY",
                &e.id,
                Some(edition_id),
                Some(&e.id),
                &names,
                &people,
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

    /// Hangs each declared arc under the one it named, once they all have ids.
    ///
    /// A second pass and not a lookahead: an arc may name a parent declared after it, and
    /// asking a file to put its sagas first would be asking the shape to carry a meaning the
    /// sidecar is there to state.
    ///
    /// Three things are refused rather than obeyed, because each would make the index claim
    /// something nobody wrote: a parent that names no arc of this edition, an arc naming
    /// itself, and a chain that comes back round. The link goes, the arc stays — losing the
    /// arc as well would turn one wrong word into a missing range.
    fn link_sagas(
        &self,
        cx: &Cx<'_>,
        where_: &str,
        written: &[(String, ArcJson)],
        by_name: &HashMap<String, String>,
        report: &mut ScanReport,
    ) -> Result<()> {
        let mut parents: HashMap<String, String> = HashMap::new();
        for (id, arc) in written {
            let Some(named) = arc
                .parent
                .as_deref()
                .map(str::trim)
                .filter(|n| !n.is_empty())
            else {
                continue;
            };
            let Some(parent) = by_name.get(named) else {
                report.disregarded.push(format!(
                    "{where_}: arc \"{}\" says it sits inside \"{named}\", which is not an arc \
                     of this edition — the arc stands on its own",
                    arc.name
                ));
                continue;
            };
            if parent == id {
                report.disregarded.push(format!(
                    "{where_}: arc \"{}\" says it sits inside itself",
                    arc.name
                ));
                continue;
            }
            parents.insert(id.clone(), parent.clone());
        }

        // A chain that comes back round would have a client drawing one indentation for ever.
        // Walked from each arc, bounded by how many there are: past that, it has looped.
        let mut refused: Vec<String> = Vec::new();
        for (id, arc) in written {
            let mut at = id;
            for _ in 0..=written.len() {
                match parents.get(at) {
                    Some(next) if next == id => {
                        report.disregarded.push(format!(
                            "{where_}: arc \"{}\" and the arcs it sits inside come back round \
                             to it — the link was left out",
                            arc.name
                        ));
                        refused.push(id.clone());
                        break;
                    }
                    Some(next) => at = next,
                    None => break,
                }
            }
        }
        for id in refused {
            parents.remove(&id);
        }

        for (id, parent) in parents {
            cx.execute(
                "UPDATE arc SET parent_id = ?1 WHERE id = ?2",
                rusqlite::params![parent, id],
            )?;
        }
        Ok(())
    }

    fn write_arcs(
        &self,
        cx: &Cx<'_>,
        edition_id: &str,
        where_: &str,
        declared: &[ArcJson],
        entries: &[ReadEntry],
        report: &mut ScanReport,
    ) -> Result<()> {
        cx.execute("DELETE FROM arc WHERE edition_id = ?1", [edition_id])?;

        if !declared.is_empty() {
            // Counted on the way in, not read off `enumerate`. A disregarded arc consumed its
            // index too: three declared with the first left out gave positions 1 and 2 and
            // ids ending `-arc-1` and `-arc-2`, with 0 belonging to nothing. The order still
            // came out right, which is why it went unnoticed — but the column stopped meaning
            // "the nth arc of this edition", and a count taken from the highest one is short
            // by however many were left out.
            let mut kept = 0usize;
            // Name to id, for the parents to be resolved against once every arc has one. The
            // first of a repeated name wins and the repeat is reported: a file that names two
            // arcs the same has not said which one a child belongs to.
            let mut by_name: HashMap<String, String> = HashMap::new();
            let mut written: Vec<(String, ArcJson)> = Vec::new();
            for arc in declared {
                // A unit the index refuses used to be handed to it anyway: the insert failed,
                // and it failed inside the transaction that holds this whole shelf. One word
                // in one file stopped two hundred series from being indexed, and a scan that
                // is not complete prunes nothing, so a deletion anywhere else went unseen too.
                //
                // Read rather than obeyed, and said out loud rather than guessed at: "tome"
                // is somebody meaning VOLUME, but a scan that invents the value it wanted is
                // how a file stops describing what is on the disk.
                let Some(unit) = sidecars::arc_unit(&arc.unit) else {
                    let said = format!(
                        "{where_}: arc \"{}\" is counted in \"{}\", which is not a unit — \
                         CHAPTER or VOLUME, so this arc was left out",
                        arc.name, arc.unit
                    );
                    // Once for the file, not once per edition reading it. A work's arcs come
                    // down to every edition that declares none of its own, so one word in one
                    // work.json produced the same sentence for each of them — which reads as
                    // several problems in several files.
                    if !report.disregarded.contains(&said) {
                        report.disregarded.push(said);
                    }
                    continue;
                };
                cx.execute(
                    "INSERT INTO arc (id, edition_id, name, unit, from_number, to_number, position)
                     VALUES (?1,?2,?3,?4,?5,?6,?7)",
                    rusqlite::params![
                        format!("{edition_id}-arc-{kept}"),
                        edition_id,
                        arc.name,
                        unit,
                        arc.from,
                        arc.to,
                        kept as i64,
                    ],
                )?;
                if by_name.contains_key(&arc.name) {
                    report.disregarded.push(format!(
                        "{where_}: two arcs are called \"{}\" — an arc saying it sits inside \
                         that name cannot say which one, so the first keeps it",
                        arc.name
                    ));
                } else {
                    by_name.insert(arc.name.clone(), format!("{edition_id}-arc-{kept}"));
                }
                written.push((format!("{edition_id}-arc-{kept}"), arc.clone()));
                kept += 1;
            }
            self.link_sagas(cx, where_, &written, &by_name, report)?;
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
                plain(*from),
                plain(*to)
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

    /// The same rewrite-whole rule as genres, for the writers.
    fn record_authors(&self, cx: &Cx<'_>, work_id: &str, authors: &[String]) -> Result<()> {
        cx.execute("DELETE FROM work_author WHERE work_id = ?1", [work_id])?;
        for name in authors.iter().map(|a| a.trim()).filter(|a| !a.is_empty()) {
            cx.execute(
                "INSERT OR IGNORE INTO work_author (work_id, name, key) VALUES (?1,?2,?3)",
                rusqlite::params![work_id, name, search_key(name)],
            )?;
        }
        Ok(())
    }

    /// The same rewrite-whole rule again, for the illustrators.
    fn record_artists(&self, cx: &Cx<'_>, work_id: &str, artists: &[String]) -> Result<()> {
        cx.execute("DELETE FROM work_artist WHERE work_id = ?1", [work_id])?;
        for name in artists.iter().map(|a| a.trim()).filter(|a| !a.is_empty()) {
            cx.execute(
                "INSERT OR IGNORE INTO work_artist (work_id, name, key) VALUES (?1,?2,?3)",
                rusqlite::params![work_id, name, search_key(name)],
            )?;
        }
        Ok(())
    }

    /// Who each volume of an edition is credited with, written only where it changed.
    ///
    /// Not the rewrite-whole rule of the series' tables: that costs a statement per name per
    /// volume on every scan, and a series of a hundred volumes with two names each would
    /// rewrite four hundred rows to arrive where it started. So the edition's stored credits
    /// are read in one question, and a volume is rewritten only when its list differs.
    fn credit_entries(&self, cx: &Cx<'_>, edition_id: &str, entries: &[ReadEntry]) -> Result<()> {
        let rows = cx.query(
            "SELECT c.entry_id, 0, c.name, c.rowid FROM entry_author c
               JOIN entry x ON x.id = c.entry_id WHERE x.edition_id = ?1
             UNION ALL
             SELECT c.entry_id, 1, c.name, c.rowid FROM entry_artist c
               JOIN entry x ON x.id = c.entry_id WHERE x.edition_id = ?1",
            [edition_id],
            |r| {
                Ok((
                    r.get::<_, String>(0)?,
                    r.get::<_, i64>(1)?,
                    r.get::<_, String>(2)?,
                    r.get::<_, i64>(3)?,
                ))
            },
        )?;
        let mut stored: HashMap<(String, i64), Vec<(i64, String)>> = HashMap::new();
        for (entry, role, name, at) in rows {
            stored.entry((entry, role)).or_default().push((at, name));
        }
        for (role, table) in [(0, "entry_author"), (1, "entry_artist")] {
            for e in entries {
                let wanted = union([if role == 0 {
                    e.effective.authors.clone()
                } else {
                    e.effective.artists.clone()
                }]);
                let mut held = stored.remove(&(e.id.clone(), role)).unwrap_or_default();
                held.sort_by_key(|(at, _)| *at);
                if held.into_iter().map(|(_, n)| n).eq(wanted.iter().cloned()) {
                    continue;
                }
                cx.execute(
                    &format!("DELETE FROM {table} WHERE entry_id = ?1"),
                    [e.id.as_str()],
                )?;
                for name in &wanted {
                    cx.execute(
                        &format!(
                            "INSERT OR IGNORE INTO {table} (entry_id, name, key) VALUES (?1,?2,?3)"
                        ),
                        rusqlite::params![e.id, name, search_key(name)],
                    )?;
                }
            }
        }
        Ok(())
    }

    /// And again for the tags — beside genres, never merged into them.
    fn record_tags(&self, cx: &Cx<'_>, work_id: &str, tags: &[String]) -> Result<()> {
        cx.execute("DELETE FROM work_tag WHERE work_id = ?1", [work_id])?;
        for name in tags.iter().map(|t| tag_case(t)).filter(|t| !t.is_empty()) {
            cx.execute(
                "INSERT OR IGNORE INTO work_tag (work_id, name, key) VALUES (?1,?2,?3)",
                rusqlite::params![work_id, name, search_key(&name)],
            )?;
        }
        Ok(())
    }

    #[allow(clippy::type_complexity)]
    fn record_universe(
        &self,
        cx: &Cx<'_>,
        folder: &Path,
        seen: &mut Seen,
        report: &mut ScanReport,
    ) -> Result<(String, Option<UniverseJson>, Option<String>)> {
        let id = identity::of(
            folder,
            layout::UNIVERSE_FILE,
            &id_of(folder, "universe"),
            &mut seen.claimed,
            report,
        );
        // Read after the stamp, not before: the stamp writes the file, and a view taken
        // first would be a view of the version without it.
        let meta: Option<UniverseJson> = read_json(folder, layout::UNIVERSE_FILE);
        seen.universes.insert(id.clone());
        let stale = self.make_way(cx, "universe", &id, &absolute(folder))?;
        cx.execute(
            "INSERT INTO universe (id, name, path) VALUES (?1,?2,?3)
             ON CONFLICT(id) DO UPDATE SET name = excluded.name, path = excluded.path",
            rusqlite::params![
                id,
                meta.as_ref()
                    .and_then(|m| m.name.clone())
                    .unwrap_or_else(|| name_of(folder)),
                absolute(folder)
            ],
        )?;
        report.universes += 1;
        // Handed back rather than read again: the orders in it name works, and no work of
        // this universe is in the database yet. The stale id travels the same way, for
        // `visit_universe` to let go of once every work has had its turn — see `make_way`.
        Ok((id, meta, stale))
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
    fn prune(&self, cx: &Cx<'_>, seen: &Seen, lost: &mut u32) -> Result<()> {
        let fill = |keep: &HashSet<String>| -> Result<()> {
            cx.run("DROP TABLE IF EXISTS temp.kept")?;
            cx.run("CREATE TEMP TABLE kept (id TEXT PRIMARY KEY)")?;
            for id in keep {
                cx.execute("INSERT INTO temp.kept (id) VALUES (?1)", [id.as_str()])?;
            }
            Ok(())
        };
        let delete_missing = |table: &str| -> Result<()> {
            cx.execute(
                &format!("DELETE FROM {table} WHERE id NOT IN (SELECT id FROM temp.kept)"),
                [],
            )
            .map(|_| ())
        };
        let clean = |table: &str, keep: &HashSet<String>| -> Result<()> {
            if keep.is_empty() {
                cx.execute(&format!("DELETE FROM {table}"), [])?;
                return Ok(());
            }
            fill(keep)?;
            delete_missing(table)?;
            cx.run("DROP TABLE temp.kept")?;
            Ok(())
        };
        // Counted before the entries go, because `ON DELETE CASCADE` takes the positions
        // with them and there is nothing left to count afterwards. Lost either way — the
        // file is gone — but a reading position going quiet is the one thing a scan cannot
        // rebuild, so it is said rather than left to vanish.
        //
        // Counted and cleaned against **one** filling of `temp.kept`, not two. Counting used
        // to build the table, drop it, and let `clean` build the same set again a statement
        // later: on the 57 686 entries this module's own header cites, 115 372 inserts where
        // 57 686 do, inside the single transaction the whole prune runs in.
        if seen.entries.is_empty() {
            *lost = cx
                .query_one("SELECT COUNT(*) FROM progress", [], |r| r.get::<_, i64>(0))?
                .unwrap_or(0) as u32;
            cx.execute("DELETE FROM entry", [])?;
        } else {
            fill(&seen.entries)?;
            *lost = self.positions_about_to_go(cx)?;
            delete_missing("entry")?;
            cx.run("DROP TABLE temp.kept")?;
        }
        clean("edition", &seen.editions)?;
        clean("work", &seen.works)?;
        clean("universe", &seen.universes)?;
        // Last, and not first: it looks for search rows whose subject is gone, so the
        // subjects have to be gone already. Run before the cleaning, it read tables that
        // still held everything and found no orphan — a deleted series stayed findable until
        // the scan after the one that removed it.
        self.prune_search(cx)
    }

    /// How many reading positions point at an entry the scan did not meet.
    ///
    /// Through the same temporary table the prune uses, and for the same reason: an inlined
    /// `IN` clause of three thousand identifiers is a fifty-kilobyte statement.
    /// How many reading positions the entries about to be deleted carry, read from the
    /// `temp.kept` its caller has already filled.
    ///
    /// Takes no set of its own: it used to build `temp.kept`, count, and drop it, only for
    /// the delete a statement later to build the identical set again. The caller fills once
    /// and both read it.
    fn positions_about_to_go(&self, cx: &Cx<'_>) -> Result<u32> {
        Ok(cx
            .query_one(
                "SELECT COUNT(*) FROM progress WHERE entry_id NOT IN (SELECT id FROM temp.kept)",
                [],
                |r| r.get::<_, i64>(0),
            )?
            .unwrap_or(0) as u32)
    }

    fn prune_within(
        &self,
        cx: &Cx<'_>,
        work_path: &str,
        seen: &Seen,
        lost: &mut u32,
    ) -> Result<()> {
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
            // An edition goes with every entry under it, and every entry with its reading
            // position: counted here for the same reason as below, one query for the whole
            // edition rather than one per entry about to disappear with it.
            *lost += cx
                .query_one(
                    "SELECT COUNT(*) FROM progress WHERE entry_id IN \
                     (SELECT id FROM entry WHERE edition_id = ?1)",
                    [id.as_str()],
                    |r| r.get::<_, i64>(0),
                )?
                .unwrap_or(0) as u32;
            cx.execute("DELETE FROM edition WHERE id = ?1", [id.as_str()])?;
        }
        for edition in &seen.editions {
            let entries = cx.query(
                "SELECT id FROM entry WHERE edition_id = ?1",
                [edition.as_str()],
                |r| r.get::<_, String>(0),
            )?;
            for id in entries.iter().filter(|id| !seen.entries.contains(*id)) {
                // Counted before the delete, for `prune`'s own reason: `ON DELETE CASCADE`
                // takes the positions with the entry and there is nothing left to count
                // afterwards. `progress_lost` used to be filled by `prune` alone — which a
                // rescan aimed at one work never calls — so every position an aimed rescan
                // dropped was dropped in silence, and the report said nought because it had
                // not looked.
                *lost += cx
                    .query_one(
                        "SELECT COUNT(*) FROM progress WHERE entry_id = ?1",
                        [id.as_str()],
                        |r| r.get::<_, i64>(0),
                    )?
                    .unwrap_or(0) as u32;
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

/// One credit of a volume: what its `entry.json` lists, else the names its ComicInfo gives. Field by field, so a volume whose `entry.json` credits only the drawing still has
/// its writer read from ComicInfo.
fn credit(own: Option<&[String]>, legacy: &[String]) -> Vec<String> {
    match own {
        Some(listed) if !listed.is_empty() => listed.to_vec(),
        _ => legacy.to_vec(),
    }
}

/// A default as the `work` row keeps it: a JSON list, or nothing when nobody is declared.
fn declared(names: &[String]) -> Option<String> {
    (!names.is_empty()).then(|| serde_json::to_string(names).unwrap_or_default())
}

/// The people of several lists, once each: by `search_key`, under the spelling met first,
/// in the order they are met, which is file order (`Tome 10` before `Tome 2`).
///
/// What a series is credited with when its volumes are credited one by one. The rule it
/// replaces took the `Writer` of the first volume that had one for the whole series, so a
/// script that changed hands at volume 2 was credited to its first writer only, and an
/// illustrator was never credited at all — which is also what a search for him found.
fn union(lists: impl IntoIterator<Item = Vec<String>>) -> Vec<String> {
    let mut seen = std::collections::HashSet::new();
    let mut out = Vec::new();
    for name in lists.into_iter().flatten() {
        let name = name.trim();
        if !name.is_empty() && seen.insert(search_key(name)) {
            out.push(name.to_string());
        }
    }
    out
}

/// Short and content-addressed: two rows written from the same text share it.
fn digest_of(name: &str, detail: &str, label: &str) -> String {
    let digest = Sha256::digest(format!("{name}\u{0}{detail}\u{0}{label}").as_bytes());
    digest.iter().take(8).map(|b| format!("{b:02x}")).collect()
}

/// The identity a folder or a file has from its path alone.
///
/// **No longer what identifies a folder** — see [`super::identity`], which is where the
/// reading position stopped being destroyed by a `mv`. This is still what identifies one
/// whose sidecar cannot answer, and it is still the identity a folder *used* to have, which
/// is what the scan reattaches progress from on the first pass after the change.
///
/// Distinct per level, so a work and its implicit edition do not collide on one path.
pub fn id_of(path: &Path, suffix: &str) -> String {
    short(&format!("{}{suffix}", absolute(path)))
}

/// Sixteen hexadecimal characters of the SHA-256 of a string: the one shape an identifier
/// has in this server, whatever it was made from.
pub fn short(of: &str) -> String {
    Sha256::digest(of.as_bytes())
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

/// Whether the chapters stored for an edition carry the names its entries were just drafted
/// with — same rows, same `raw`, same `label` — keyed the way [`Scanner::write_chapters`]
/// writes them.
fn named_as_drafted(kept: &[(String, (String, String))], entries: &[ReadEntry]) -> bool {
    let kept: HashMap<&str, (&str, &str)> = kept
        .iter()
        .map(|(id, (raw, label))| (id.as_str(), (raw.as_str(), label.as_str())))
        .collect();
    let mut drafted = 0usize;
    for entry in entries {
        for (rank, chapter) in entry.chapters.iter().enumerate() {
            drafted += 1;
            let id = format!("{}-{rank}", entry.id);
            if kept.get(id.as_str()) != Some(&(chapter.raw.as_str(), chapter.label.as_str())) {
                return false;
            }
        }
    }
    drafted == kept.len()
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

/// A number written the way a reader writes it: « 7 » and not « 7.0 », « 7.5 » when it is.
fn plain(value: f64) -> String {
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
