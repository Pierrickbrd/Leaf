//! Renaming what is on the disk, worked out before anything is touched.
//!
//! A name in the library is made of things that change: a series is retitled, a volume is
//! renumbered, a second edition arrives and the edition starts to be said. Each of those
//! changes the canonical name of files and folders ([`super::naming`]), and the files have to
//! follow — without losing the reading position of a single volume, and without ever writing
//! over a file.
//!
//! This module is the first half: [`plan_work`] reads the index and the disk and says what
//! would move, or why nothing can. It writes nothing and moves nothing. A plan is all or
//! nothing: every collision is looked for before the first move, because a series that went
//! half way to two editions is a series nobody can read the folder of.

use std::collections::{HashMap, HashSet};
use std::path::{Path, PathBuf};

use anyhow::{Context, Result};

use super::db::Cx;
use super::naming::{file_name, folder_name, Parts};
use crate::metadata::sidecars::{Document, FORMAT_VERSION};
use crate::scan::{identity, layout};

/// The three kinds of folder a plan renames.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum FolderKind {
    Universe,
    Work,
    Edition,
}

impl FolderKind {
    fn sidecar(self) -> &'static str {
        match self {
            FolderKind::Universe => layout::UNIVERSE_FILE,
            FolderKind::Work => layout::WORK_FILE,
            FolderKind::Edition => layout::EDITION_FILE,
        }
    }
}

/// One thing to move. In a plan the folders come first, from the top down, and the files
/// after: a file's `from` is therefore where it is **once the folders above it have moved**.
#[derive(Debug, Clone, PartialEq)]
pub enum Move {
    File {
        entry_id: String,
        from: PathBuf,
        to: PathBuf,
    },
    Folder {
        kind: FolderKind,
        id: String,
        from: PathBuf,
        to: PathBuf,
    },
}

/// A sidecar to write whole **before** the first move, at the place it is now.
#[derive(Debug, Clone, PartialEq)]
pub struct SidecarRewrite {
    pub path: PathBuf,
    pub content: Vec<u8>,
    /// Set when the only thing this write is for is to make the folder say its identity: it
    /// is then applied to the sidecar as it is **when the step runs**, not to the copy the
    /// plan read. Between the two a patch may have written the title, and a whole file put
    /// back over it takes the patch with it.
    pub stamp: Option<String>,
}

/// What would be done: sidecars first, then `moves` in order.
#[derive(Debug, Clone, Default, PartialEq)]
pub struct Plan {
    pub rewrites: Vec<SidecarRewrite>,
    pub moves: Vec<Move>,
    /// The folders of the works touched, as they will be once the plan is done: what the
    /// rescan after it is aimed at.
    pub works: Vec<PathBuf>,
}

/// Why nothing is moved.
#[derive(Debug, Clone, PartialEq)]
pub enum Refusal {
    /// Where two things would land on one name, or a file that is not this library's is
    /// already there. Every such path, so that it can be fixed in one go.
    Collision(Vec<PathBuf>),
    /// The disk will not take a rename.
    ReadOnly,
    /// A folder whose identity is its path (nothing could be written in it) would change
    /// identity by being renamed, and take its readers' positions with it.
    CannotStampIdentity(PathBuf),
    /// Two editions of one series that would go by the same name, which two folders cannot.
    SameEditionName(String),
}

/// The new values of what enters a name, for the plan to work out the names they give. What
/// is not here is read from the index as it is.
#[derive(Debug, Clone, Default)]
pub struct Overrides {
    /// The series' title. Renames the series folder.
    pub work_name: Option<String>,
    /// A new name for an edition, by edition id. Renames its folder when it has one.
    pub edition_names: HashMap<String, String>,
    /// A volume's number or title once changed, by entry id.
    pub numbers: HashMap<String, Option<f64>>,
    pub titles: HashMap<String, Option<String>>,
    /// How many editions the series holds once done, when that is not the count it has.
    pub editions: Option<i64>,
    /// The universe the series will be in: `Some(None)` out of any, `Some(Some(name))` into
    /// one. Its name enters the name of every volume.
    pub universe: Option<Option<String>>,
    /// The series has one edition with no folder of its own, and is about to have two: the
    /// one it has gets a folder with this name, its own `edition.json`, and the publication
    /// fields `work.json` held for it.
    pub home_for_implicit: Option<String>,
    /// Volumes to give their canonical name though nothing that composes it changed: a file
    /// an import has just put over a volume lands on its row, under the name that volume
    /// had, and is named like any other from there.
    pub normalise: HashSet<String>,
}

/// What `work.json` says about the printing rather than the story: when a second edition
/// arrives these go with the first one, because every one of them has an everyday
/// counter-example — a colour first printing, a French reading direction flipped, another
/// publisher, a language — and an inherited default that is wrong is wrong without anything
/// saying so, where an empty field shows.
const PUBLICATION: [&str; 10] = [
    "publisher",
    "collection",
    "language",
    "readingDirection",
    "status",
    "volumeCount",
    "format",
    "colour",
    "chapterLabel",
    "coverVolume",
];

struct WorkRow {
    id: String,
    name: String,
    path: PathBuf,
    universe: Option<String>,
    universe_path: Option<PathBuf>,
    editions: i64,
}

struct EditionRow {
    id: String,
    name: Option<String>,
    path: PathBuf,
    implicit: bool,
    /// The file *is* the series: an album. Its identity is made from its path, so moving it
    /// is making a new series.
    one_shot: bool,
    entries: Vec<EntryRow>,
}

struct EntryRow {
    id: String,
    file: PathBuf,
    chapter: bool,
    number: Option<f64>,
    title: Option<String>,
}

/// The reading orders of a universe, with every step that named this series by its folder (or
/// this edition by its name) pointing at the new one. `None` when no step did, so that a
/// universe whose orders say nothing of the series is not rewritten for it.
///
/// Orders name nothing but `work` (the folder, relative to the universe) and `edition` (its
/// name): a rename of either is the only thing that can leave one pointing at a folder that
/// is not there any more.
fn orders_following(
    universe: &Path,
    work: (&str, &str),
    editions: &[(String, String)],
    first_edition: Option<&str>,
) -> Option<SidecarRewrite> {
    let file = universe.join(layout::UNIVERSE_FILE);
    let mut document = Document::parse(&std::fs::read(&file).ok()?).ok()?;
    let mut orders = document.value("orders")?.clone();
    let mut changed = false;
    for step in orders
        .as_array_mut()?
        .iter_mut()
        .filter_map(|order| order.get_mut("steps")?.as_array_mut())
        .flatten()
    {
        if step.get("work").and_then(|w| w.as_str()) != Some(work.0) {
            continue;
        }
        if work.0 != work.1 {
            step["work"] = serde_json::Value::String(work.1.to_string());
            changed = true;
        }
        let named = step
            .get("edition")
            .and_then(|e| e.as_str())
            .map(str::to_string);
        // A volume range names its edition once there are two to tell apart; with one the
        // field was useless and refused nothing. The edition it meant is the first one.
        if let (Some(first), None, Some("VOLUME")) = (
            first_edition,
            named.as_deref(),
            step.get("unit").and_then(|u| u.as_str()),
        ) {
            step["edition"] = serde_json::Value::String(first.to_string());
            changed = true;
            continue;
        }
        if let Some((_, to)) = editions
            .iter()
            .find(|(from, _)| named.as_deref() == Some(from))
        {
            step["edition"] = serde_json::Value::String(to.clone());
            changed = true;
        }
    }
    if !changed {
        return None;
    }
    document.set("orders", orders);
    Some(SidecarRewrite {
        path: file,
        content: document.bytes().ok()?,
        stamp: None,
    })
}

/// Two questions, whatever the length of the series: the work, and every edition with its
/// volumes. A question per volume would make renaming a long series a long wait.
fn load(cx: &Cx<'_>, work_id: &str) -> Result<(WorkRow, Vec<EditionRow>)> {
    let work = cx
        .query_one(
            "SELECT w.id, COALESCE(NULLIF(TRIM(w.title), ''), w.name), w.path, u.name, u.path,
                    (SELECT COUNT(*) FROM edition s WHERE s.work_id = w.id)
             FROM work w LEFT JOIN universe u ON u.id = w.universe_id
             WHERE w.id = ?1",
            [work_id],
            |r| {
                Ok(WorkRow {
                    id: r.get(0)?,
                    name: r.get(1)?,
                    path: PathBuf::from(r.get::<_, String>(2)?),
                    universe: r.get(3)?,
                    universe_path: r.get::<_, Option<String>>(4)?.map(PathBuf::from),
                    editions: r.get(5)?,
                })
            },
        )?
        .with_context(|| format!("no work {work_id}"))?;

    let rows = cx.query(
        "SELECT ed.id, ed.name, ed.path, ed.implicit, ed.one_shot,
                e.id, e.file, e.type, e.volume_number, e.title
         FROM edition ed LEFT JOIN entry e ON e.edition_id = ed.id
         WHERE ed.work_id = ?1 ORDER BY ed.id, e.volume_number, e.file",
        [work_id],
        |r| {
            Ok((
                r.get::<_, String>(0)?,
                r.get::<_, Option<String>>(1)?,
                r.get::<_, String>(2)?,
                r.get::<_, i64>(3)? == 1,
                r.get::<_, i64>(4)? == 1,
                r.get::<_, Option<String>>(5)?,
                r.get::<_, Option<String>>(6)?,
                r.get::<_, Option<String>>(7)?,
                r.get::<_, Option<f64>>(8)?,
                r.get::<_, Option<String>>(9)?,
            ))
        },
    )?;
    let mut editions: Vec<EditionRow> = Vec::new();
    for (id, name, path, implicit, one_shot, entry, file, kind, number, title) in rows {
        if editions.last().is_none_or(|e| e.id != id) {
            editions.push(EditionRow {
                id,
                name,
                path: PathBuf::from(path),
                implicit,
                one_shot,
                entries: Vec::new(),
            });
        }
        if let (Some(entry), Some(file)) = (entry, file) {
            if let Some(last) = editions.last_mut() {
                last.entries.push(EntryRow {
                    id: entry,
                    file: PathBuf::from(file),
                    chapter: kind.as_deref() == Some("CHAPTER"),
                    number,
                    title,
                });
            }
        }
    }
    Ok((work, editions))
}

/// Where `path` is once the folder moves listed have been made, in order.
fn rebase(path: &Path, folders: &[(PathBuf, PathBuf)]) -> PathBuf {
    let mut now = path.to_path_buf();
    for (from, to) in folders {
        if let Ok(rest) = now.strip_prefix(from) {
            now = to.join(rest);
        }
    }
    now
}

/// The inverse of [`rebase`]: where a path of the finished library is on the disk today.
fn unrebase(path: &Path, folders: &[(PathBuf, PathBuf)]) -> PathBuf {
    let mut now = path.to_path_buf();
    for (from, to) in folders.iter().rev() {
        if let Ok(rest) = now.strip_prefix(to) {
            now = from.join(rest);
        }
    }
    now
}

/// Whether a file can be made in `dir`, which is the only honest answer to « will this disk
/// take a rename »: a read-only mount, a folder owned by someone else and a full disk all
/// answer it the same way, and the permission bits say none of them.
fn writable(dir: &Path) -> bool {
    let probe = super::files::beside(&dir.join(".leaf-probe"), "part");
    let made = std::fs::File::create(&probe).is_ok();
    let _ = std::fs::remove_file(&probe);
    made
}

/// What it takes for a renamed folder to keep its identity: the sidecar must say it. A folder
/// whose id is only its path (a sidecar that could not be written, an id another folder was
/// already using) would be a new folder under its new name.
fn stamp(kind: FolderKind, id: &str, folder: &Path) -> Result<Option<SidecarRewrite>, PathBuf> {
    let file = folder.join(kind.sidecar());
    let mut document = match std::fs::read(&file) {
        Ok(bytes) if bytes.is_empty() => Document::default(),
        Ok(bytes) => Document::parse(&bytes).map_err(|_| file.clone())?,
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => Document::default(),
        Err(_) => return Err(file),
    };
    if document.text(identity::FIELD) == Some(id) {
        return Ok(None);
    }
    if !writable(folder) {
        return Err(file);
    }
    document.set(identity::FIELD, serde_json::Value::String(id.to_string()));
    if !document.has("leaf") {
        document.set("leaf", serde_json::Value::from(FORMAT_VERSION));
    }
    let content = document.bytes().map_err(|_| file.clone())?;
    Ok(Some(SidecarRewrite {
        path: file,
        content,
        stamp: Some(id.to_string()),
    }))
}

/// Puts file moves in an order in which none lands on a file that has not left yet. Two
/// volumes that swap names go through a temporary one beside them.
fn sequence(wanted: Vec<(String, PathBuf, PathBuf)>) -> Vec<Move> {
    struct Item {
        entry_id: String,
        from: PathBuf,
        to: PathBuf,
        done: bool,
    }
    let mut items: Vec<Item> = wanted
        .into_iter()
        .map(|(entry_id, from, to)| Item {
            entry_id,
            from,
            to,
            done: false,
        })
        .collect();
    let mut out = Vec::with_capacity(items.len());
    let mut temporary = 0usize;
    let mut next = 0usize;
    loop {
        // Who still sits where each move wants to go.
        let sources: HashMap<PathBuf, usize> = items
            .iter()
            .enumerate()
            .filter(|(_, i)| !i.done)
            .map(|(n, i)| (i.from.clone(), n))
            .collect();
        let mut moved = false;
        for item in items.iter_mut() {
            if item.done || sources.contains_key(&item.to) {
                continue;
            }
            item.done = true;
            out.push(Move::File {
                entry_id: item.entry_id.clone(),
                from: item.from.clone(),
                to: item.to.clone(),
            });
            moved = true;
        }
        if moved {
            continue;
        }
        // Everything left is in a cycle: send one aside and let the rest through.
        while next < items.len() && items[next].done {
            next += 1;
        }
        if next >= items.len() {
            return out;
        }
        let aside = items[next]
            .from
            .with_file_name(format!(".leaf-rename-{temporary}"));
        temporary += 1;
        out.push(Move::File {
            entry_id: items[next].entry_id.clone(),
            from: items[next].from.clone(),
            to: aside.clone(),
        });
        items[next].from = aside;
    }
}

/// What renaming a series would do, or why it cannot.
///
/// **A file is renamed when what its name is made of changes**, not whenever it differs from
/// the canonical name: a library imported before names were canonical keeps the names it has
/// until one of its volumes is touched, and then only the ones that are. Folders the same
/// way: the series folder is renamed when the title changes, to the name the new title gives.
pub fn plan_work(
    cx: &Cx<'_>,
    work_id: &str,
    wanted: &Overrides,
) -> Result<std::result::Result<Plan, Refusal>> {
    let (work, editions) = load(cx, work_id)?;
    if editions.iter().any(|e| e.one_shot) {
        return Ok(Ok(Plan::default()));
    }
    let mut folders: Vec<(PathBuf, PathBuf)> = Vec::new();
    let mut moves: Vec<Move> = Vec::new();
    let mut rewrites: Vec<SidecarRewrite> = Vec::new();

    // Folders, from the top down. The sidecar to stamp is read where the folder is now.
    if let Err(file) = move_folders(
        &work,
        &editions,
        wanted,
        &mut folders,
        &mut moves,
        &mut rewrites,
    ) {
        return Ok(Err(Refusal::CannotStampIdentity(file)));
    }

    // A series with one edition and no folder for it, about to have two: that edition is
    // given a folder, and what describes the printing moves into it.
    let has_implicit = editions.iter().any(|e| e.implicit);
    let home: Option<(&EditionRow, PathBuf)> = match (&wanted.home_for_implicit, has_implicit) {
        (Some(name), true) => editions
            .iter()
            .find(|e| e.implicit)
            .map(|e| (e, rebase(&work.path, &folders).join(folder_name(name)))),
        _ => None,
    };
    if let (Some(name), Some((edition, dir))) = (&wanted.home_for_implicit, &home) {
        match move_printing(&work.path, name, &edition.id, dir, &folders) {
            Ok(both) => rewrites.extend(both),
            Err(file) => return Ok(Err(Refusal::CannotStampIdentity(file))),
        }
    }

    // The orders of the universe that name this folder or an edition of it.
    if let Some(rewrite) = orders_rewrite(&work, &editions, wanted, &folders, has_implicit) {
        rewrites.push(rewrite);
    }

    // Files: the ones whose name is made of something that changed.
    let files = files_to_move(&work, &editions, wanted, home.as_ref(), &folders);

    // Everything that would land on a name that is taken: twice by this plan, or by a file
    // this plan does not move. Looked for before the first move.
    let collisions = collisions(&files, &moves, &folders, home.as_ref().map(|(_, dir)| dir));
    if !collisions.is_empty() {
        return Ok(Err(Refusal::Collision(collisions)));
    }

    let files = sequence(files);
    if !first_move_is_writable(moves.first().or(files.first()), &folders) {
        return Ok(Err(Refusal::ReadOnly));
    }
    moves.extend(files);
    let works = if moves.is_empty() {
        Vec::new()
    } else {
        vec![rebase(&work.path, &folders)]
    };
    Ok(Ok(Plan {
        rewrites,
        moves,
        works,
    }))
}

/// The series folder and the edition folders the wanted names rename, each with the
/// sidecar that stamps its identity; the sidecar that could not be read otherwise.
fn move_folders(
    work: &WorkRow,
    editions: &[EditionRow],
    wanted: &Overrides,
    folders: &mut Vec<(PathBuf, PathBuf)>,
    moves: &mut Vec<Move>,
    rewrites: &mut Vec<SidecarRewrite>,
) -> std::result::Result<(), PathBuf> {
    let mut renamed: Vec<(FolderKind, String, PathBuf, PathBuf)> = Vec::new();
    if let Some(title) = &wanted.work_name {
        if folder_name(title) != folder_name(&work.name) {
            let to = work.path.with_file_name(folder_name(title));
            if to != work.path {
                renamed.push((FolderKind::Work, work.id.clone(), work.path.clone(), to));
            }
        }
    }
    for (kind, id, from, to) in renamed {
        let before = unrebase(&from, folders);
        match stamp(kind, &id, &before) {
            Ok(Some(rewrite)) => rewrites.push(rewrite),
            Ok(None) => {}
            Err(file) => return Err(file),
        }
        folders.push((from.clone(), to.clone()));
        moves.push(Move::Folder { kind, id, from, to });
    }
    for edition in editions.iter().filter(|e| !e.implicit) {
        let Some(name) = wanted.edition_names.get(&edition.id) else {
            continue;
        };
        let was = edition.name.as_deref().unwrap_or_default();
        if folder_name(name) == folder_name(was) {
            continue;
        }
        let from = rebase(&edition.path, folders);
        let to = from.with_file_name(folder_name(name));
        if to == from {
            continue;
        }
        let before = unrebase(&from, folders);
        match stamp(FolderKind::Edition, &edition.id, &before) {
            Ok(Some(rewrite)) => rewrites.push(rewrite),
            Ok(None) => {}
            Err(file) => return Err(file),
        }
        folders.push((from.clone(), to.clone()));
        moves.push(Move::Folder {
            kind: FolderKind::Edition,
            id: edition.id.clone(),
            from,
            to,
        });
    }
    Ok(())
}

/// Whether the folder the first move leaves can be written to, or there is no move.
fn first_move_is_writable(first: Option<&Move>, folders: &[(PathBuf, PathBuf)]) -> bool {
    let Some(first) = first else {
        return true;
    };
    let at = match first {
        Move::Folder { from, .. } => unrebase(from, folders),
        Move::File { from, .. } => unrebase(from, folders),
    };
    at.parent().is_none_or(writable)
}

/// The edition about to be given a folder takes what describes the printing out of the
/// series' sidecar into its own: both files, or the one that could not be read.
fn move_printing(
    work_path: &Path,
    name: &str,
    edition_id: &str,
    dir: &Path,
    folders: &[(PathBuf, PathBuf)],
) -> std::result::Result<[SidecarRewrite; 2], PathBuf> {
    let work_file = work_path.join(layout::WORK_FILE);
    let mut story = match std::fs::read(&work_file) {
        Ok(bytes) => match Document::parse(&bytes) {
            Ok(it) => it,
            Err(_) => return Err(work_file),
        },
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => Document::default(),
        Err(_) => return Err(work_file),
    };
    let mut printing = Document::default();
    printing.set("leaf", serde_json::Value::from(FORMAT_VERSION));
    printing.set(
        identity::FIELD,
        serde_json::Value::String(edition_id.to_string()),
    );
    printing.set("name", serde_json::Value::String(name.to_string()));
    for key in PUBLICATION {
        if let Some(value) = story.remove(key) {
            printing.set(key, value);
        }
    }
    let (Ok(printing), Ok(story)) = (printing.bytes(), story.bytes()) else {
        return Err(work_file);
    };
    // The edition's file first: a cut between the two leaves the fields in both places,
    // which costs nothing, where the other order would leave them in neither.
    Ok([
        SidecarRewrite {
            path: unrebase(&dir.join(layout::EDITION_FILE), folders),
            content: printing,
            stamp: None,
        },
        SidecarRewrite {
            path: work_file,
            content: story,
            stamp: None,
        },
    ])
}

/// The universe's orders, rewritten to follow the series folder and its editions' folders,
/// when a universe holds the series and an order names one of them.
fn orders_rewrite(
    work: &WorkRow,
    editions: &[EditionRow],
    wanted: &Overrides,
    folders: &[(PathBuf, PathBuf)],
    has_implicit: bool,
) -> Option<SidecarRewrite> {
    let universe = work.universe_path.as_ref()?;
    let relative = |path: &Path| {
        path.strip_prefix(universe)
            .ok()
            .map(|p| p.to_string_lossy().to_string())
    };
    let renamed_editions: Vec<(String, String)> = editions
        .iter()
        .filter(|e| !e.implicit)
        .filter_map(|e| {
            // Orders name an edition by its folder, which is not its `name`: « Édition
            // française » sits in `VF`, and « Perfect/Kanzenban » in `Perfect-Kanzenban`.
            let to = folder_name(wanted.edition_names.get(&e.id)?);
            let from = e.path.file_name()?.to_string_lossy().to_string();
            Some((from, to))
        })
        .filter(|(from, to)| from != to)
        .collect();
    let new_work = rebase(&work.path, folders);
    let (Some(old), Some(new)) = (relative(&work.path), relative(&new_work)) else {
        return None;
    };
    orders_following(
        universe,
        (&old, &new),
        &renamed_editions,
        wanted
            .home_for_implicit
            .as_deref()
            .filter(|_| has_implicit)
            .map(folder_name)
            .as_deref(),
    )
}

/// The files whose name is made of something that changed, each with where it is once the
/// folders have moved and where it goes.
fn files_to_move(
    work: &WorkRow,
    editions: &[EditionRow],
    wanted: &Overrides,
    home: Option<&(&EditionRow, PathBuf)>,
    folders: &[(PathBuf, PathBuf)],
) -> Vec<(String, PathBuf, PathBuf)> {
    let editions_after = wanted.editions.unwrap_or(work.editions);
    let work_after = wanted.work_name.as_deref().unwrap_or(&work.name);
    let universe_after = match &wanted.universe {
        Some(it) => it.as_deref(),
        None => work.universe.as_deref(),
    };
    let mut files: Vec<(String, PathBuf, PathBuf)> = Vec::new();
    for edition in editions {
        let homed = home.filter(|(it, _)| it.id == edition.id);
        let edition_after = homed
            .and(wanted.home_for_implicit.as_deref())
            .or_else(|| wanted.edition_names.get(&edition.id).map(String::as_str))
            .or(edition.name.as_deref());
        for entry in &edition.entries {
            let on_disk = entry
                .file
                .file_name()
                .map(|n| n.to_string_lossy().to_string())
                .unwrap_or_default();
            let number_after = wanted
                .numbers
                .get(&entry.id)
                .copied()
                .unwrap_or(entry.number);
            let title_after = wanted
                .titles
                .get(&entry.id)
                .cloned()
                .unwrap_or_else(|| entry.title.clone());
            let before = file_name(&Parts {
                universe: work.universe.as_deref(),
                work: &work.name,
                edition: edition.name.as_deref(),
                editions: work.editions,
                chapter: entry.chapter,
                number: entry.number,
                title: entry.title.as_deref(),
                file: &on_disk,
            });
            let after = file_name(&Parts {
                universe: universe_after,
                work: work_after,
                edition: edition_after,
                editions: editions_after,
                chapter: entry.chapter,
                number: number_after,
                title: title_after.as_deref(),
                file: &on_disk,
            });
            if before == after && homed.is_none() && !wanted.normalise.contains(&entry.id) {
                continue;
            }
            let here = rebase(&entry.file, folders);
            let to = match homed {
                // Into the folder the edition has just been given, keeping any folder a
                // volume sat in under the series.
                Some((_, dir)) => {
                    let work_dir = rebase(&work.path, folders);
                    let inside = here.strip_prefix(&work_dir).unwrap_or(Path::new(&on_disk));
                    dir.join(inside.with_file_name(&after))
                }
                None => here.with_file_name(after),
            };
            if to != here {
                files.push((entry.id.clone(), here, to));
            }
        }
    }
    files
}

/// Everything that would land on a name that is taken: twice by the plan, or by a file the
/// plan does not move.
fn collisions(
    files: &[(String, PathBuf, PathBuf)],
    moves: &[Move],
    folders: &[(PathBuf, PathBuf)],
    home_dir: Option<&PathBuf>,
) -> Vec<PathBuf> {
    let mut taken: HashMap<PathBuf, u32> = HashMap::new();
    for (_, _, to) in files {
        *taken.entry(to.clone()).or_default() += 1;
    }
    let leaving: HashSet<PathBuf> = files
        .iter()
        .map(|(_, from, _)| unrebase(from, folders))
        .collect();
    let mut collisions: Vec<PathBuf> = Vec::new();
    for (_, _, to) in files {
        let today = unrebase(to, folders);
        if taken[to] > 1 || (today.exists() && !leaving.contains(&today)) {
            collisions.push(to.clone());
        }
    }
    for (n, m) in moves.iter().enumerate() {
        if let Move::Folder { to, .. } = m {
            if unrebase(to, &folders[..n]).exists() {
                collisions.push(to.clone());
            }
        }
    }
    if let Some(dir) = home_dir {
        if unrebase(dir, folders).exists() {
            collisions.push(dir.clone());
        }
    }
    collisions.sort();
    collisions.dedup();
    collisions
}

// ------------------------------------------------------------------ doing it

/// What a start of the server found of the plans a cut had left.
#[derive(Debug, Default)]
pub struct Resumed {
    /// Plans taken to their end.
    pub finished: usize,
    /// Steps that found a state the plan did not expect, and were left alone. The plan
    /// stays in the journal.
    pub unexpected: Vec<String>,
}

fn kind_word(kind: FolderKind) -> &'static str {
    match kind {
        FolderKind::Universe => "UNIVERSE",
        FolderKind::Work => "WORK",
        FolderKind::Edition => "EDITION",
    }
}

fn now_ms() -> i64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_millis() as i64)
        .unwrap_or(0)
}

fn text(path: &Path) -> String {
    path.to_string_lossy().to_string()
}

/// Writes the whole plan into the journal in one transaction, and returns its number. Before
/// the first move: whatever happens next, the intention is on record.
fn record(db: &super::db::Db, plan: &Plan) -> Result<i64> {
    db.write(|cx| {
        cx.execute(
            "INSERT INTO rename_plan (created_at, works) VALUES (?1, ?2)",
            rusqlite::params![
                now_ms(),
                serde_json::to_string(&plan.works.iter().map(|w| text(w)).collect::<Vec<_>>())?
            ],
        )?;
        let id = cx
            .query_one("SELECT last_insert_rowid()", [], |r| r.get::<_, i64>(0))?
            .context("no plan number")?;
        let mut seq = 0i64;
        for rewrite in &plan.rewrites {
            cx.execute(
                "INSERT INTO rename_step (plan_id, seq, kind, subject, from_path, content)
                 VALUES (?1, ?2, 'REWRITE', ?3, ?4, ?5)",
                rusqlite::params![id, seq, rewrite.stamp, text(&rewrite.path), rewrite.content],
            )?;
            seq += 1;
        }
        for step in &plan.moves {
            match step {
                Move::Folder {
                    kind,
                    id: subject,
                    from,
                    to,
                } => cx.execute(
                    "INSERT INTO rename_step
                       (plan_id, seq, kind, folder_kind, subject, from_path, to_path)
                     VALUES (?1, ?2, 'FOLDER', ?3, ?4, ?5, ?6)",
                    rusqlite::params![id, seq, kind_word(*kind), subject, text(from), text(to)],
                )?,
                Move::File { entry_id, from, to } => cx.execute(
                    "INSERT INTO rename_step (plan_id, seq, kind, subject, from_path, to_path)
                     VALUES (?1, ?2, 'FILE', ?3, ?4, ?5)",
                    rusqlite::params![id, seq, entry_id, text(from), text(to)],
                )?,
            };
            seq += 1;
        }
        Ok(id)
    })
}

struct Step {
    seq: i64,
    kind: String,
    subject: Option<String>,
    from: PathBuf,
    to: Option<PathBuf>,
    content: Option<Vec<u8>>,
}

enum Outcome {
    Done,
    /// The state of the disk was none the plan expected: nothing was touched.
    Unexpected(String),
}

/// The index follows a folder that moved: everything filed under it is now filed under its
/// new name. In the same transaction as the step's being crossed off, so that the index and
/// the journal never disagree about whether it was made — and **before** any rescan, which
/// would meet the files at their new place with no row for them and give each a new
/// identity, reading position lost.
fn follow_folder(cx: &super::db::Cx<'_>, from: &Path, to: &Path) -> Result<()> {
    let (from, to) = (text(from), text(to));
    for (table, column) in [
        ("universe", "path"),
        ("work", "path"),
        ("edition", "path"),
        ("entry", "file"),
    ] {
        cx.execute(
            &format!(
                "UPDATE {table} SET {column} = ?2 || substr({column}, length(?1) + 1)
                 WHERE {column} = ?1 OR substr({column}, 1, length(?1) + 1) = ?1 || '/'"
            ),
            rusqlite::params![from, to],
        )?;
    }
    Ok(())
}

fn cross_off(cx: &super::db::Cx<'_>, plan: i64, seq: i64) -> Result<()> {
    cx.execute(
        "UPDATE rename_step SET done = 1 WHERE plan_id = ?1 AND seq = ?2",
        rusqlite::params![plan, seq],
    )?;
    Ok(())
}

/// One step, judged by what the disk and the index hold now rather than by what the plan
/// remembers: made when the source is there and the destination is not; already made when
/// only the destination is; anything else touches nothing.
fn apply(db: &super::db::Db, plan: i64, step: &Step) -> Result<Outcome> {
    match step.kind.as_str() {
        "REWRITE" => {
            let mut content = step.content.clone().unwrap_or_default();
            // A stamp is merged into what is there now. Only a sidecar that is missing or
            // cannot be read is written whole: there is nothing in it to lose.
            if let Some(id) = &step.subject {
                if let Some(mut now) = std::fs::read(&step.from)
                    .ok()
                    .and_then(|bytes| Document::parse(&bytes).ok())
                {
                    now.set(identity::FIELD, serde_json::Value::String(id.clone()));
                    if !now.has("leaf") {
                        now.set("leaf", serde_json::Value::from(FORMAT_VERSION));
                    }
                    content = now.bytes()?;
                }
            }
            // A stamp is for a folder that is there. Made where it is not, it would write
            // into the mount point of a disk that is not mounted.
            if step.subject.is_some() && !step.from.parent().is_some_and(Path::is_dir) {
                return Ok(Outcome::Unexpected(format!(
                    "{} has no folder to say its identity in",
                    step.from.display()
                )));
            }
            if std::fs::read(&step.from).ok().as_deref() != Some(content.as_slice()) {
                if let Some(parent) = step.from.parent() {
                    std::fs::create_dir_all(parent)?;
                }
                super::files::write_whole(&step.from, &content)
                    .with_context(|| format!("writing {}", step.from.display()))?;
            }
            db.write(|cx| cross_off(cx, plan, step.seq))?;
            Ok(Outcome::Done)
        }
        "FOLDER" | "FILE" => {
            let to = step.to.as_deref().context("a move without a destination")?;
            let (from_here, to_here) = (step.from.exists(), to.exists());
            match (from_here, to_here) {
                (true, false) => {
                    if let Some(parent) = to.parent() {
                        std::fs::create_dir_all(parent)?;
                    }
                    std::fs::rename(&step.from, to).with_context(|| {
                        format!("moving {} to {}", step.from.display(), to.display())
                    })?;
                }
                (false, true) => {}
                (true, true) => {
                    return Ok(Outcome::Unexpected(format!(
                        "{} and {} are both there — nothing is written over",
                        step.from.display(),
                        to.display()
                    )))
                }
                (false, false) => {
                    return Ok(Outcome::Unexpected(format!(
                        "neither {} nor {} is there",
                        step.from.display(),
                        to.display()
                    )))
                }
            }
            db.write(|cx| {
                if step.kind == "FOLDER" {
                    follow_folder(cx, &step.from, to)?;
                } else if let Some(entry) = &step.subject {
                    cx.execute(
                        "UPDATE entry SET file = ?2 WHERE id = ?1",
                        rusqlite::params![entry, text(to)],
                    )?;
                }
                cross_off(cx, plan, step.seq)
            })?;
            Ok(Outcome::Done)
        }
        other => anyhow::bail!("a step of kind {other}"),
    }
}

/// Goes through what is left of a plan. `stop_after` is for a test that needs a cut.
fn run(
    scanner: &crate::scan::scanner::Scanner,
    held: &crate::scan::scanner::Held,
    db: &super::db::Db,
    plan: i64,
    stop_after: Option<usize>,
) -> Result<Vec<String>> {
    let steps = db.read(|cx| {
        cx.query(
            "SELECT seq, kind, subject, from_path, to_path, content FROM rename_step
             WHERE plan_id = ?1 AND done = 0 ORDER BY seq",
            [plan],
            |r| {
                Ok(Step {
                    seq: r.get(0)?,
                    kind: r.get(1)?,
                    subject: r.get(2)?,
                    from: PathBuf::from(r.get::<_, String>(3)?),
                    to: r.get::<_, Option<String>>(4)?.map(PathBuf::from),
                    content: r.get(5)?,
                })
            },
        )
    })?;
    for (made, step) in steps.iter().enumerate() {
        if stop_after.is_some_and(|n| made >= n) {
            return Ok(Vec::new());
        }
        if let Outcome::Unexpected(why) = apply(db, plan, step)? {
            return Ok(vec![why]);
        }
    }
    let works: Vec<String> = db
        .read(|cx| {
            cx.query_one("SELECT works FROM rename_plan WHERE id = ?1", [plan], |r| {
                r.get::<_, String>(0)
            })
        })?
        .and_then(|json| serde_json::from_str(&json).ok())
        .unwrap_or_default();
    db.write(|cx| {
        cx.execute(
            "UPDATE rename_plan SET done_at = ?2 WHERE id = ?1",
            rusqlite::params![plan, now_ms()],
        )?;
        Ok(())
    })?;
    // What the scan works out from paths — `cover_file`, the folders' own `path` — is
    // refreshed by reading the series again. A failure here is not the rename's: the files
    // are where they should be, and the next scan will do it.
    for work in works {
        if let Err(e) = scanner.rescan_work_held(held, Path::new(&work)) {
            tracing::warn!(work, error = %e, "could not rescan after a rename");
        }
    }
    Ok(Vec::new())
}

/// Does the plan: journal first, then each step, then a rescan of the series touched. Under
/// the scanners' lock for the whole of it. A step that finds a file that is not this plan's
/// where it is going stops everything with an error and leaves the journal as it is.
pub fn execute(
    scanner: &crate::scan::scanner::Scanner,
    db: &super::db::Db,
    plan: &Plan,
) -> Result<()> {
    execute_until(scanner, db, plan, usize::MAX)
}

/// [`execute`] that stops after `steps` steps as a cut would: the journal is left as the
/// process dying there would leave it. For tests; `usize::MAX` is [`execute`].
#[doc(hidden)]
pub fn execute_until(
    scanner: &crate::scan::scanner::Scanner,
    db: &super::db::Db,
    plan: &Plan,
    steps: usize,
) -> Result<()> {
    if plan.moves.is_empty() && plan.rewrites.is_empty() {
        return Ok(());
    }
    scanner.exclusive(|held| {
        let id = record(db, plan)?;
        let stop = (steps != usize::MAX).then_some(steps);
        let stopped = run(scanner, held, db, id, stop)?;
        if let Some(why) = stopped.into_iter().next() {
            anyhow::bail!("{why}");
        }
        Ok(())
    })
}

/// [`execute`] for a caller that already holds the scanners' lock — one that moves a folder
/// around the plan and must keep the scanners out of the whole of it.
pub fn execute_held(
    scanner: &crate::scan::scanner::Scanner,
    held: &crate::scan::scanner::Held,
    db: &super::db::Db,
    plan: &Plan,
) -> Result<()> {
    if plan.moves.is_empty() && plan.rewrites.is_empty() {
        return Ok(());
    }
    let id = record(db, plan)?;
    if let Some(why) = run(scanner, held, db, id, None)?.into_iter().next() {
        anyhow::bail!("{why}");
    }
    Ok(())
}

/// Finishes the plans a cut left, at the start of the server. Each step is judged on what is
/// there now; one that finds a state it did not expect touches nothing and is reported.
pub fn resume(scanner: &crate::scan::scanner::Scanner, db: &super::db::Db) -> Result<Resumed> {
    scanner.exclusive(|held| {
        let plans: Vec<i64> = db.read(|cx| {
            cx.query(
                "SELECT id FROM rename_plan WHERE done_at IS NULL ORDER BY id",
                [],
                |r| r.get(0),
            )
        })?;
        let mut resumed = Resumed::default();
        for plan in plans {
            // A plan that cannot be finished — a disk not mounted, a folder not writable —
            // is reported and left in the journal for the next start; it must not be what
            // stops the server from starting, with no way to purge it but SQL.
            let unexpected = match run(scanner, held, db, plan, None) {
                Ok(unexpected) => unexpected,
                Err(e) => vec![format!("plan {plan}: {e:#}")],
            };
            if unexpected.is_empty() {
                resumed.finished += 1;
            } else {
                resumed.unexpected.extend(unexpected);
            }
        }
        Ok(resumed)
    })
}

/// The index follows a folder somebody else has just moved — a series filed into a universe by
/// [`crate::api::relocate`] — so that the volumes in it are still found by their files.
pub fn follow_moved_folder(db: &super::db::Db, from: &Path, to: &Path) -> Result<()> {
    db.write(|cx| follow_folder(cx, from, to))
}

/// What a series becoming two editions would do, or why it cannot.
///
/// The edition it has gets a folder at `first_edition_name` (when it has none — one that
/// already has a folder keeps its own name), its own `edition.json` carrying the identity the
/// edition already had and every field of publication, and its volumes move in under the
/// names that now say the edition. Two names that are one — compared as a search is, without
/// case or accents — are refused: two folders cannot carry them.
pub fn plan_second_edition(
    cx: &Cx<'_>,
    work_id: &str,
    first_edition_name: &str,
    new_edition_name: &str,
) -> Result<std::result::Result<Plan, Refusal>> {
    let held = cx.query(
        "SELECT name, implicit FROM edition WHERE work_id = ?1",
        [work_id],
        |r| Ok((r.get::<_, Option<String>>(0)?, r.get::<_, i64>(1)? == 1)),
    )?;
    let [(held_name, implicit)] = held.as_slice() else {
        anyhow::bail!(
            "a series that has {} editions is not becoming two",
            held.len()
        );
    };
    let first = if *implicit {
        first_edition_name
    } else {
        held_name.as_deref().unwrap_or(first_edition_name)
    };
    let new = new_edition_name.trim();
    if super::text::search_key(first) == super::text::search_key(new) {
        return Ok(Err(Refusal::SameEditionName(new.to_string())));
    }
    plan_work(
        cx,
        work_id,
        &Overrides {
            editions: Some(2),
            home_for_implicit: implicit.then(|| first_edition_name.trim().to_string()),
            ..Overrides::default()
        },
    )
}
