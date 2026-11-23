//! A cover chosen from a file on the reader's own disk.
//!
//! The client sends the image; the server never receives a path. What it writes goes beside
//! what it covers, under the name [`covers::OWN`] — the one name the scan already looks for
//! first — so the choice is a file of the library like any other: it survives a rebuilt index,
//! travels with the folder, and needs no column of its own.
//!
//! Three rules:
//!
//!  - **Decoded, not trusted.** The bytes are decoded in full before anything is written. A
//!    name, a content type or a magic number can all say « PNG » over something else, and a
//!    cover that does not decode is a tile that stays blank for every reader until someone
//!    finds the file.
//!  - **Whole or not at all.** [`write_whole`](crate::store::files::write_whole): a reader of
//!    the shelf, or the scan, must never see a prefix of the image.
//!  - **Only what the server wrote is taken back.** A `cover.jpg` somebody put in the folder
//!    is never overwritten, never removed: it stays, loses to the chosen image while it
//!    exists, and is the cover again when the choice is withdrawn.

use std::io::Cursor;
use std::path::{Path, PathBuf};

use anyhow::Result;
use image::{ImageFormat, ImageReader};

use crate::api::records::{EntryPatch, Records, SeriesPatch};
use crate::scan::covers;
use crate::store::files::write_whole;
use crate::store::Db;

/// Largest image taken, in bytes. A cover is a tile on a shelf: this is far more than a
/// scan of a page needs and far less than what would make a request a way to fill the disk.
pub const MAX_BYTES: usize = 20 * 1024 * 1024;

/// Largest image taken, in pixels. Bytes alone do not bound the memory a decoder asks for:
/// a few kilobytes of PNG can describe a billion pixels.
const MAX_PIXELS: u64 = 64_000_000;

pub struct CoverChoice<'a> {
    db: &'a Db,
    /// Passed on to [`Records`], which follows a converted volume it rewrites.
    library: Option<&'a std::path::Path>,
}

/// Where a cover goes, as the index knows it.
struct Place {
    path: PathBuf,
    /// An archive (the cover goes beside it) or a folder (it goes inside).
    archive: bool,
}

impl<'a> CoverChoice<'a> {
    pub fn new(db: &'a Db) -> Self {
        CoverChoice { db, library: None }
    }

    /// Forgetting a chosen page rewrites the volume: see [`Records::in_library`].
    pub fn in_library(self, library: &'a std::path::Path) -> Self {
        CoverChoice {
            library: Some(library),
            ..self
        }
    }

    /// Makes `bytes` the cover of a series. `false` when there is no such series.
    pub fn set_series(&self, edition_id: &str, bytes: &[u8]) -> Result<bool> {
        let Some(place) = self.series_place(edition_id)? else {
            return Ok(false);
        };
        write_choice(&place, bytes)?;
        // The later choice wins: the volume chosen before would otherwise keep being served
        // after the image, were the order ever to change, and is stale either way.
        self.forget_volume(edition_id)?;
        Ok(true)
    }

    /// Withdraws the image the server wrote for a series, if any, and the volume chosen
    /// as well: back to the default is back to the default. `false` when unknown.
    pub fn clear_series(&self, edition_id: &str) -> Result<bool> {
        let Some(place) = self.series_place(edition_id)? else {
            return Ok(false);
        };
        remove_own(&place, None)?;
        self.forget_volume(edition_id)?;
        Ok(true)
    }

    /// A volume was chosen as the series' cover: the image the server wrote before is taken
    /// back, or it would go on outranking the choice just made. A `cover.jpg` placed by hand
    /// is not the server's to remove. `false` when unknown.
    pub fn yield_to_volume(&self, edition_id: &str) -> Result<bool> {
        let Some(place) = self.series_place(edition_id)? else {
            return Ok(false);
        };
        remove_own(&place, None)?;
        Ok(true)
    }

    fn forget_volume(&self, edition_id: &str) -> Result<()> {
        let chosen: Option<Option<f64>> = self.db.read(|cx| {
            cx.query_one(
                "SELECT cover_volume FROM edition WHERE id = ?1",
                [edition_id],
                |r| r.get::<_, Option<f64>>(0),
            )
        })?;
        // A sidecar is rewritten only when there is something to forget.
        if chosen.flatten().is_some() {
            Records::new(self.db).patch_series(
                edition_id,
                &SeriesPatch {
                    cover_volume: Some(None),
                    ..SeriesPatch::default()
                },
            )?;
        }
        Ok(())
    }

    /// Makes `bytes` the cover of a volume, and forgets a page chosen before: the later
    /// choice wins, and a page would otherwise keep winning over the image just given.
    pub fn set_entry(&self, entry_id: &str, bytes: &[u8]) -> Result<bool> {
        let Some(place) = self.entry_place(entry_id)? else {
            return Ok(false);
        };
        write_choice(&place, bytes)?;
        self.forget_page(entry_id)?;
        Ok(true)
    }

    /// Back to the default for a volume: the page chosen is forgotten and the image the
    /// server wrote is removed.
    pub fn clear_entry(&self, entry_id: &str) -> Result<bool> {
        let Some(place) = self.entry_place(entry_id)? else {
            return Ok(false);
        };
        remove_own(&place, None)?;
        self.forget_page(entry_id)?;
        Ok(true)
    }

    fn forget_page(&self, entry_id: &str) -> Result<()> {
        let chosen: Option<Option<i64>> = self.db.read(|cx| {
            cx.query_one(
                "SELECT cover_page FROM entry WHERE id = ?1",
                [entry_id],
                |r| r.get::<_, Option<i64>>(0),
            )
        })?;
        // Rewriting the volume's sidecar costs a pass over the archive: only when there is
        // a page to forget.
        if chosen.flatten().is_some() {
            let records = Records::new(self.db);
            let records = match self.library {
                Some(library) => records.in_library(library),
                None => records,
            };
            records.patch_entry(
                entry_id,
                &EntryPatch {
                    cover_page: Some(None),
                    ..EntryPatch::default()
                },
            )?;
        }
        Ok(())
    }

    /// A one-shot's edition lives in its file, and its cover beside the file; any other
    /// edition is a folder.
    fn series_place(&self, edition_id: &str) -> Result<Option<Place>> {
        let path: Option<String> = self.db.read(|cx| {
            cx.query_one(
                "SELECT path FROM edition WHERE id = ?1",
                [edition_id],
                |r| r.get::<_, String>(0),
            )
        })?;
        Ok(path.map(|p| {
            let path = PathBuf::from(p);
            let archive = path.is_file();
            Place { path, archive }
        }))
    }

    fn entry_place(&self, entry_id: &str) -> Result<Option<Place>> {
        let file: Option<String> = self.db.read(|cx| {
            cx.query_one("SELECT file FROM entry WHERE id = ?1", [entry_id], |r| {
                r.get::<_, String>(0)
            })
        })?;
        Ok(file.map(|f| Place {
            path: PathBuf::from(f),
            archive: true,
        }))
    }
}

/// Validates, writes, then takes away the same cover in any other format.
fn write_choice(place: &Place, bytes: &[u8]) -> Result<()> {
    let extension = validated(bytes)?;
    let Some(target) = covers::own_path(&place.path, place.archive, extension) else {
        return Err(crate::api::invalid("nowhere to put a cover"));
    };
    write_whole(&target, bytes)?;
    // After, not before: if the write failed, the previous choice is still there.
    remove_own(place, Some(&target))
}

fn remove_own(place: &Place, keep: Option<&Path>) -> Result<()> {
    for old in covers::own_files(&place.path, place.archive) {
        if Some(old.as_path()) != keep {
            std::fs::remove_file(old)?;
        }
    }
    Ok(())
}

/// The extension of an image the server accepts, or why it does not.
pub fn validated(bytes: &[u8]) -> Result<&'static str> {
    if bytes.is_empty() {
        return Err(crate::api::invalid("the image is empty"));
    }
    if bytes.len() > MAX_BYTES {
        return Err(crate::api::invalid(format!(
            "the image is larger than {} MB",
            MAX_BYTES / (1024 * 1024)
        )));
    }
    let (format, extension) = match image::guess_format(bytes) {
        Ok(ImageFormat::Jpeg) => (ImageFormat::Jpeg, "jpg"),
        Ok(ImageFormat::Png) => (ImageFormat::Png, "png"),
        Ok(ImageFormat::WebP) => (ImageFormat::WebP, "webp"),
        _ => return Err(crate::api::invalid("a cover is a jpeg, png or webp image")),
    };
    let reader = || ImageReader::with_format(Cursor::new(bytes), format);
    let (width, height) = reader()
        .into_dimensions()
        .map_err(|_| crate::api::invalid("the image cannot be read"))?;
    if width == 0 || height == 0 || u64::from(width) * u64::from(height) > MAX_PIXELS {
        return Err(crate::api::invalid("the image is too large to be a cover"));
    }
    reader()
        .decode()
        .map_err(|_| crate::api::invalid("the image cannot be read"))?;
    Ok(extension)
}
