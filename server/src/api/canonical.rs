//! The names an import lays its files and folders down under.
//!
//! A drop arrives under the names its sender gave it — « DB couleur 01.cbz » in
//! « zodiaque-vf » — and the library names a volume by what it *is*: series, edition, number.
//! Left alone, the same volume imported by two people landed under two names and a rescan
//! could not tell them apart. This is the one place that decides, once, at the announcement,
//! so that the state and the commit install what the announcement said.
//!
//! **A post-pass over the landings, not a rule of the matching.** Which files are new is only
//! known once each has been compared with the volumes the library holds; so the names are
//! given afterwards, to what is new, and to nothing else. A file that replaces a volume
//! lands on it, under the name that volume has, and one already identical lands nowhere.

use std::collections::{BTreeMap, HashSet};
use std::path::Path;

use super::bulk_import::{declares, is_archive_name, text_of, ImportRequest};
use super::reimport::Outcome;
use crate::store::naming::{file_name, folder_name, Parts};

/// A folder renamed on the way in: where the drop put it, where it lands.
pub type FolderMove = (String, String);

/// What naming did: the folders it renamed, in order, and the files whose names met, each
/// with the index of the one it met.
pub struct Named {
    pub moved: Vec<FolderMove>,
    pub collisions: Vec<(usize, usize)>,
}

/// What the library already knows of the series a drop is aimed at, which a name is made
/// from before anything the drop declares: a title, a universe, an edition name and a count
/// come from the index — as the rename engine reads them — and not from the folders the
/// sender happened to use. Absent for a drop aimed at nothing, which has only its own words.
pub struct Known {
    pub work: String,
    pub universe: Option<String>,
    /// The edition a drop aimed at a series lands in, when the series has more than one.
    pub edition: Option<String>,
    /// How many editions the series has once the drop is in, when the drop adds none itself.
    pub editions: i64,
    /// 1 when the series has an implicit edition, which has no folder to be counted by and
    /// is one more when editions are added beside it.
    pub held_implicit: i64,
}

/// Gives every new folder the real name its declaration states and every new, numbered file
/// its canonical name. Returns the folders it renamed, in the order it renamed them, so that
/// whatever else is keyed by folder follows.
///
/// Two files that come to one name are a collision: neither lands, and each is `Twice`, which
/// is what an import already says of two files carrying one number.
pub fn canonicalise(
    request: &ImportRequest,
    root: &str,
    base: &Path,
    known: Option<&Known>,
    files: &mut [Option<String>],
    sidecars: &mut [String],
    outcomes: &mut [Option<Outcome>],
) -> Named {
    // New folders are named by what they declare, whether the drop is aimed at nothing or
    // at new editions of a series; any other target lays the drop on folders that exist.
    let new_folders = matches!(
        request.target.as_ref().map(|t| t.kind),
        None | Some(crate::api::reimport::TargetKind::SeriesNewEditions)
    );
    let moved = if new_folders {
        rename_folders(request, base, files, sidecars)
    } else {
        Vec::new()
    };
    let collisions = name_files(request, root, base, known, files, sidecars, outcomes);
    Named { moved, collisions }
}

/// One of the three declarations a sidecar path is, and what it calls its folder.
struct Declared {
    folder: String,
    kind: &'static str,
    title: Option<String>,
}

fn declared(request: &ImportRequest, sidecars: &[String]) -> Vec<Declared> {
    request
        .sidecars
        .iter()
        .zip(sidecars)
        .filter_map(|(sidecar, at)| {
            let path = Path::new(at);
            let kind = declares(path.file_name()?.to_str()?)?;
            let folder = path.parent()?.to_string_lossy().into_owned();
            // An edition says its name in `name`; a work or a universe says its title.
            let key = if kind == "EDITION" { "name" } else { "title" };
            let title = text_of(sidecar, key).filter(|t| !t.trim().is_empty());
            Some(Declared {
                folder,
                kind,
                title,
            })
        })
        .collect()
}

fn under_prefix(path: &str, old: &str) -> Option<String> {
    path.strip_prefix(old)
        .filter(|rest| rest.is_empty() || rest.starts_with('/'))
        .map(str::to_string)
}

fn rename_folders(
    request: &ImportRequest,
    base: &Path,
    files: &mut [Option<String>],
    sidecars: &mut [String],
) -> Vec<FolderMove> {
    let mut done: HashSet<String> = HashSet::new();
    let mut moved: Vec<FolderMove> = Vec::new();
    loop {
        let mut shallowest: Option<(String, String)> = None;
        for d in declared(request, sidecars) {
            let (Some(title), true) = (&d.title, !d.folder.is_empty()) else {
                continue;
            };
            if done.contains(&d.folder) {
                continue;
            }
            let (parent, name) = match d.folder.rsplit_once('/') {
                Some((parent, name)) => (format!("{parent}/"), name),
                None => (String::new(), d.folder.as_str()),
            };
            let wanted = folder_name(title);
            let target = format!("{parent}{wanted}");
            let depth = d.folder.matches('/').count();
            let better = shallowest
                .as_ref()
                .is_none_or(|(old, _)| depth < old.matches('/').count());
            // A folder already on the disk is the library's, not the drop's: it keeps the
            // name it has. A target name already used by another folder of the drop would
            // fold two series into one.
            let taken = base.join(&target).exists()
                || sidecars
                    .iter()
                    .chain(files.iter().flatten())
                    .any(|p| under_prefix(p, &target).is_some());
            if name != wanted && !base.join(&d.folder).exists() && !taken {
                if better {
                    shallowest = Some((d.folder.clone(), target));
                }
            } else {
                done.insert(d.folder.clone());
            }
        }
        let Some((old, new)) = shallowest else {
            return moved;
        };
        for at in files.iter_mut().flatten().chain(sidecars.iter_mut()) {
            if let Some(rest) = under_prefix(at, &old) {
                *at = format!("{new}{rest}");
            }
        }
        done.insert(new.clone());
        moved.push((old, new));
    }
}

fn name_files(
    request: &ImportRequest,
    root: &str,
    base: &Path,
    known: Option<&Known>,
    files: &mut [Option<String>],
    sidecars: &[String],
    outcomes: &mut [Option<Outcome>],
) -> Vec<(usize, usize)> {
    let held_implicit = known.map_or(0, |k| k.held_implicit);
    let adds_editions = matches!(
        request.target.as_ref().map(|t| t.kind),
        Some(crate::api::reimport::TargetKind::SeriesNewEditions)
    );
    let declarations = declared(request, sidecars);
    let mut proposed: Vec<(usize, String)> = Vec::new();
    for (i, f) in request.files.iter().enumerate() {
        let new = matches!(outcomes[i], None | Some(Outcome::Add));
        let (Some(at), true, true) = (files[i].as_deref(), new, is_archive_name(&f.path)) else {
            continue;
        };
        // The file that replaces a chapter lands in the volume it rewrites: that volume has
        // its name already.
        if f.splice.is_some() {
            continue;
        }
        let Some(found) = &f.found else { continue };
        let number = found.number.filter(|_| !found.guessed);
        if number.is_none() && found.title.is_none() {
            continue;
        }
        let (folder, announced) = at.rsplit_once('/').unwrap_or(("", at));
        let chain: Vec<&str> = std::iter::once("")
            .chain(
                folder
                    .match_indices('/')
                    .map(|(k, _)| &folder[..k])
                    .chain((!folder.is_empty()).then_some(folder)),
            )
            .collect();
        let at_kind = |prefix: &str, kind: &str| {
            declarations
                .iter()
                .find(|d| d.folder == prefix && d.kind == kind)
        };
        let name_of = |prefix: &str| -> String {
            let basename = if prefix.is_empty() {
                root
            } else {
                prefix.rsplit_once('/').map_or(prefix, |(_, n)| n)
            };
            declarations
                .iter()
                .find(|d| d.folder == prefix && d.kind != "EDITION")
                .and_then(|d| d.title.clone())
                .unwrap_or_else(|| basename.to_string())
        };
        let edition_at = chain.iter().rev().find(|p| at_kind(p, "EDITION").is_some());
        let work_at = chain
            .iter()
            .find(|p| at_kind(p, "WORK").is_some())
            .or_else(|| chain.iter().rev().find(|p| Some(*p) != edition_at))
            .copied()
            .unwrap_or("");
        let universe_at = chain.iter().find(|p| at_kind(p, "UNIVERSE").is_some());
        let work = known.map_or_else(|| name_of(work_at), |k| k.work.clone());
        let universe = match known {
            Some(k) => k.universe.clone(),
            None => universe_at.filter(|p| **p != work_at).map(|p| name_of(p)),
        };
        let edition = match known {
            // Laid on an edition the series holds: that edition's own name.
            Some(k) if !adds_editions => k.edition.clone(),
            _ => edition_at.map(|p| {
                at_kind(p, "EDITION")
                    .and_then(|d| d.title.clone())
                    .unwrap_or_else(|| p.rsplit_once('/').map_or(*p, |(_, n)| n).to_string())
            }),
        };
        let editions = match known {
            Some(k) if !adds_editions => k.editions,
            _ => edition_at.map_or(1, |_| {
                editions_of(&declarations, base, work_at) + held_implicit
            }),
        };
        let canonical = file_name(&Parts {
            universe: universe.as_deref(),
            work: &work,
            edition: edition.as_deref(),
            editions,
            chapter: found
                .kind
                .as_deref()
                .is_some_and(|k| k.eq_ignore_ascii_case("chapter")),
            number,
            title: found.title.as_deref(),
            file: &as_cbz(announced),
        });
        let landing = if folder.is_empty() {
            canonical
        } else {
            format!("{folder}/{canonical}")
        };
        // A name the library already holds is a volume it has: that is the matching's to
        // decide, and renaming onto it would hide it.
        if landing != at && !base.join(&landing).exists() {
            proposed.push((i, landing));
        }
    }

    settle(&proposed, files, outcomes)
}

/// How many editions the work at `work_at` has: those the request declares beside it,
/// and those already on disk.
fn editions_of(declarations: &[Declared], base: &Path, work_at: &str) -> i64 {
    let mut names: HashSet<String> = declarations
        .iter()
        .filter(|d| d.kind == "EDITION")
        .filter(|d| d.folder.rsplit_once('/').map_or("", |(parent, _)| parent) == work_at)
        .map(|d| d.folder.clone())
        .collect();
    if let Ok(read) = std::fs::read_dir(base.join(work_at)) {
        // Only a folder that holds volumes is an edition: « Extras » is not.
        for entry in read
            .flatten()
            .filter(|e| e.path().is_dir() && !crate::scan::layout::archives(&e.path()).is_empty())
        {
            let name = entry.file_name().to_string_lossy().into_owned();
            names.insert(if work_at.is_empty() {
                name
            } else {
                format!("{work_at}/{name}")
            });
        }
    }
    names.len().max(1) as i64
}

/// Gives each proposed name to its file, unless another file, proposed or held, already
/// lands there: both are then reported, and the newcomer is refused as twice sent.
fn settle(
    proposed: &[(usize, String)],
    files: &mut [Option<String>],
    outcomes: &mut [Option<Outcome>],
) -> Vec<(usize, usize)> {
    let mut who: BTreeMap<String, Vec<usize>> = BTreeMap::new();
    for (i, landing) in proposed {
        who.entry(landing.to_lowercase()).or_default().push(*i);
    }
    let mut collisions = Vec::new();
    for (i, landing) in proposed {
        let meets = who[&landing.to_lowercase()]
            .iter()
            .copied()
            .find(|j| j != i)
            .or_else(|| {
                files.iter().enumerate().find_map(|(j, other)| {
                    (j != *i
                        && other
                            .as_deref()
                            .is_some_and(|o| o.eq_ignore_ascii_case(landing)))
                    .then_some(j)
                })
            });
        if let Some(j) = meets {
            files[*i] = None;
            outcomes[*i] = Some(Outcome::Twice);
            collisions.push((*i, j));
        } else {
            files[*i] = Some(landing.clone());
        }
    }
    collisions
}

/// A `.zip` is a `.cbz` under another name; the library says one.
fn as_cbz(name: &str) -> String {
    match name.rsplit_once('.') {
        Some((stem, ext)) if ext.eq_ignore_ascii_case("zip") => format!("{stem}.cbz"),
        _ => name.to_string(),
    }
}
