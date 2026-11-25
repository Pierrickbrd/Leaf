//! Finding a cover that was put on the disk rather than taken from page zero.
//!
//! Page zero is right most of the time and wrong often enough to matter — a colour insert, a
//! scanlation credit — and overriding it must not mean editing the archive.

use std::path::{Path, PathBuf};

pub const EXTENSIONS: [&str; 6] = ["png", "jpeg", "jpg", "webp", "gif", "tbn"];
const SERIES_NAMES: [&str; 5] = ["cover", "default", "folder", "poster", "series"];

/// What the server calls a cover it wrote itself, as opposed to one somebody put there.
///
/// A name of its own, and not `cover.jpg`: an image a person dropped in the folder is theirs,
/// and the server must be able to take back what it wrote without ever guessing which of the
/// two files it is looking at. It comes **first** in the search below, because it is the
/// later and deliberate choice, made in a window, where the hand-placed file may be old.
pub const OWN: &str = "leaf-cover";

/// An image named like the archive, sitting beside it: `Tome 4.cbz` and `Tome 4.jpg`.
pub fn beside_archive(archive: &Path) -> Option<PathBuf> {
    let folder = archive.parent()?;
    let base = archive.file_stem()?.to_string_lossy().to_lowercase();
    let own = format!("{base}.{OWN}");
    let found = entries(folder);
    [own, base].into_iter().find_map(|wanted| {
        found
            .iter()
            .find(|p| stem(p).is_some_and(|s| s.to_lowercase() == wanted) && has_image_extension(p))
            .cloned()
    })
}

/// A cover named for the folder, which speaks for the whole series.
pub fn in_folder(folder: &Path) -> Option<PathBuf> {
    let found = entries(folder);
    let named = |wanted: &dyn Fn(&str) -> bool| {
        found
            .iter()
            .find(|p| stem(p).is_some_and(|s| wanted(&s.to_lowercase())) && has_image_extension(p))
            .cloned()
    };
    named(&|s| s == OWN).or_else(|| named(&|s| SERIES_NAMES.contains(&s)))
}

/// Every image `beside_archive` would take for this archive, not only the one it prefers: a
/// copy that kept one of two would silently drop the other.
pub fn all_beside_archive(archive: &Path) -> Vec<PathBuf> {
    let Some(folder) = archive.parent() else {
        return Vec::new();
    };
    let Some(base) = archive
        .file_stem()
        .map(|s| s.to_string_lossy().to_lowercase())
    else {
        return Vec::new();
    };
    let own = format!("{base}.{OWN}");
    entries(folder)
        .into_iter()
        .filter(|p| {
            stem(p).is_some_and(|s| {
                let s = s.to_lowercase();
                s == base || s == own
            }) && has_image_extension(p)
        })
        .collect()
}

/// Every image `in_folder` would take for this folder, for the same reason.
pub fn all_in_folder(folder: &Path) -> Vec<PathBuf> {
    entries(folder)
        .into_iter()
        .filter(|p| {
            stem(p).is_some_and(|s| {
                let s = s.to_lowercase();
                s == OWN || SERIES_NAMES.contains(&s.as_str())
            }) && has_image_extension(p)
        })
        .collect()
}

/// Whether a cover recorded in the index is one the server wrote, which is what lets a client
/// offer to take it back and not offer it for a `cover.jpg` somebody placed.
pub fn is_own(recorded: &str) -> bool {
    Path::new(recorded)
        .file_stem()
        .map(|s| s.to_string_lossy().to_lowercase())
        .is_some_and(|s| s == OWN || s.ends_with(&format!(".{OWN}")))
}

/// The files the server itself wrote as the cover of `folder` — or, when `place` is an
/// archive, of that archive. Whatever the extension: a choice replaced by one in another
/// format must not leave the first behind.
pub fn own_files(place: &Path, for_archive: bool) -> Vec<PathBuf> {
    let (folder, wanted) = if for_archive {
        let Some(folder) = place.parent() else {
            return Vec::new();
        };
        let base = place
            .file_stem()
            .map(|s| s.to_string_lossy().to_lowercase())
            .unwrap_or_default();
        (folder, format!("{base}.{OWN}"))
    } else {
        (place, OWN.to_string())
    };
    entries(folder)
        .into_iter()
        .filter(|p| stem(p).is_some_and(|s| s.to_lowercase() == wanted) && has_image_extension(p))
        .collect()
}

/// Where the server writes the cover it was given: `leaf-cover.png` in a folder,
/// `Tome 4.leaf-cover.png` beside `Tome 4.cbz`.
pub fn own_path(place: &Path, for_archive: bool, extension: &str) -> Option<PathBuf> {
    if for_archive {
        let stem = place.file_stem()?.to_string_lossy();
        Some(place.with_file_name(format!("{stem}.{OWN}.{extension}")))
    } else {
        Some(place.join(format!("{OWN}.{extension}")))
    }
}

fn entries(folder: &Path) -> Vec<PathBuf> {
    let mut found: Vec<PathBuf> = std::fs::read_dir(folder)
        .map(|e| {
            e.flatten()
                .map(|e| e.path())
                .filter(|p| p.is_file())
                .collect()
        })
        .unwrap_or_default();
    // Sorted, so that two candidates always resolve the same way rather than by whatever
    // order the filesystem happened to hand back.
    found.sort();
    found
}

fn stem(path: &Path) -> Option<String> {
    path.file_stem().map(|s| s.to_string_lossy().to_string())
}

fn has_image_extension(path: &Path) -> bool {
    path.extension()
        .map(|e| e.to_string_lossy().to_lowercase())
        .is_some_and(|e| EXTENSIONS.contains(&e.as_str()))
}
