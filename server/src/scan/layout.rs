//! Recognising what a folder is.
//!
//! The hard part: "Dragon Ball/{Perfect Edition, Original Edition}" and
//! "Terres d'Arran/{Elfes, Mages}" have exactly the same shape on disk. One is a work in two
//! editions, the other a universe of two works, and no heuristic can tell them apart — that
//! is precisely what the level files are for.
//!
//! With no file, we settle on UNIVERSE. Not out of symmetry, but because the opposite
//! mistake is worse: taking Terres d'Arran for a work would make Elfes and Mages two
//! "editions" of the same story, and the edition picker would offer to switch between them
//! as though they were one narrative.

use std::path::{Path, PathBuf};

pub const UNIVERSE_FILE: &str = "universe.json";
pub const WORK_FILE: &str = "work.json";
pub const EDITION_FILE: &str = "edition.json";

/// A CBZ is a zip: the extension is a convention, not a format. Both are accepted.
const EXTENSIONS: [&str; 2] = ["cbz", "zip"];

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Kind {
    Universe,
    Work,
    Edition,
    /// None of the three: a folder that holds folders and says nothing.
    ///
    /// Not a level of the model — a shelf. `Mangas/` sitting above a hundred series is one,
    /// and so is any folder somebody made to tidy up. It is walked through rather than
    /// recorded, and what is inside is judged on its own terms.
    Container,
}

/// What a folder is, and whether it said so itself.
///
/// **The sidecars decide.** A `universe.json` makes a universe and nothing else does; the
/// one exception is archives, which are evidence rather than a guess — a folder holding
/// volumes is the work those volumes belong to, whether or not anybody wrote it down.
///
/// A folder that holds only folders and declares nothing used to be taken for a universe,
/// "the safer of the two mistakes", and reported as ambiguous. It was a guess, and it was
/// usually wrong: what people actually build is a shelf. Guessing is gone.
pub fn kind(folder: &Path) -> (Kind, bool) {
    let children = children(folder);
    let named = |name: &str| {
        children
            .iter()
            .any(|c| c.file_name().is_some_and(|n| n == name))
    };

    if named(UNIVERSE_FILE) {
        return (Kind::Universe, true);
    }
    if named(WORK_FILE) {
        return (Kind::Work, true);
    }
    if named(EDITION_FILE) {
        return (Kind::Edition, true);
    }
    // Archives sitting right here: the work is this folder, with its implicit edition.
    // Evidence, not a guess — nothing else puts volumes in a folder.
    if children.iter().any(|c| is_archive(c)) {
        return (Kind::Work, false);
    }
    // Folders, and nothing saying what this is. So it is not one of the three.
    (Kind::Container, false)
}

pub fn archives(folder: &Path) -> Vec<PathBuf> {
    let mut found: Vec<PathBuf> = children(folder)
        .into_iter()
        .filter(|c| is_archive(c))
        .collect();
    found.sort();
    found
}

pub fn sub_folders(folder: &Path) -> Vec<PathBuf> {
    let mut found: Vec<PathBuf> = children(folder)
        .into_iter()
        .filter(|c| {
            c.is_dir()
                && !c
                    .file_name()
                    .is_some_and(|n| n.to_string_lossy().starts_with('.'))
        })
        .collect();
    found.sort();
    found
}

/// Whether there is an archive here, or within `levels - 1` folders below.
///
/// `levels` is how many floors to look at, this one included: 1 is here, 2 is here and the
/// sub-folders, 3 reaches an edition folder inside a work inside a universe — which is as
/// deep as the model goes.
pub fn holds_archives_within(folder: &Path, levels: usize) -> bool {
    if levels == 0 {
        return false;
    }
    if !archives(folder).is_empty() {
        return true;
    }
    levels > 1
        && sub_folders(folder)
            .iter()
            .any(|f| holds_archives_within(f, levels - 1))
}

/// An archive here or one folder down: what a **work** can hold, its editions included.
pub fn holds_archives(folder: &Path) -> bool {
    holds_archives_within(folder, 2)
}

/// The whole model, from a classified folder: universe, work, edition, and the files in it.
pub const MODEL_DEPTH: usize = 3;

/// How many shelves deep the walk will follow before deciding a library is not one.
///
/// A shelf costs nothing and holds nothing, so a person may nest a few — `Mangas/Shonen/`.
/// Bounded all the same: `sub_folders` follows a symbolic link like any folder, and a link
/// to a parent would otherwise be walked until the stack gave out.
pub const MAX_SHELVES: usize = 8;

/// Whether the folder can actually be listed.
///
/// `children` cannot say: it swallows the error and answers "empty", which is the right
/// answer for a folder with nothing in it and a catastrophic one for a folder whose
/// permissions changed. Everything downstream treats empty as "this series is gone", and
/// gone means pruned from the index — progress and all, which a rescan cannot bring back.
pub fn readable(folder: &Path) -> std::io::Result<()> {
    std::fs::read_dir(folder).map(|_| ())
}

fn children(folder: &Path) -> Vec<PathBuf> {
    if !folder.is_dir() {
        return Vec::new();
    }
    std::fs::read_dir(folder)
        .map(|entries| entries.flatten().map(|e| e.path()).collect())
        .unwrap_or_default()
}

fn is_archive(path: &Path) -> bool {
    !path.is_dir()
        && path
            .extension()
            .map(|e| e.to_string_lossy().to_lowercase())
            .is_some_and(|e| EXTENSIONS.contains(&e.as_str()))
}
