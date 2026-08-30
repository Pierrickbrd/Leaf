//! Finding a cover that was put on the disk rather than taken from page zero.
//!
//! Page zero is right most of the time and wrong often enough to matter — a colour insert, a
//! scanlation credit — and overriding it must not mean editing the archive.

use std::path::{Path, PathBuf};

const EXTENSIONS: [&str; 6] = ["png", "jpeg", "jpg", "webp", "gif", "tbn"];
const SERIES_NAMES: [&str; 5] = ["cover", "default", "folder", "poster", "series"];

/// An image named like the archive, sitting beside it: `Tome 4.cbz` and `Tome 4.jpg`.
pub fn beside_archive(archive: &Path) -> Option<PathBuf> {
    let folder = archive.parent()?;
    let base = archive.file_stem()?.to_string_lossy().to_lowercase();
    entries(folder)
        .into_iter()
        .find(|p| stem(p).is_some_and(|s| s.to_lowercase() == base) && has_image_extension(p))
}

/// A cover named for the folder, which speaks for the whole series.
pub fn in_folder(folder: &Path) -> Option<PathBuf> {
    entries(folder).into_iter().find(|p| {
        stem(p).is_some_and(|s| SERIES_NAMES.contains(&s.to_lowercase().as_str()))
            && has_image_extension(p)
    })
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
