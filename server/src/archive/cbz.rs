//! Getting one entry out of an archive.
//!
//! Listing and ordering the pages belongs to the scanner; this is what serving a page
//! needs, and nothing else.

use std::fs::File;
use std::io::Read;
use std::path::Path;
use std::sync::atomic::{AtomicU64, Ordering};

use anyhow::{Context, Result};

/// How many entry streams have been opened since the process started.
///
/// The same idea as counting statements in the database: opening an entry is a seek, and a
/// seek is what a scan of a large library is actually made of. Reading each page twice
/// returned exactly the same result as reading it once — it simply took 662 seconds instead
/// of 291, and no test could see the difference between them.
static OPENED: AtomicU64 = AtomicU64::new(0);

pub fn streams_opened() -> u64 {
    OPENED.load(Ordering::Relaxed)
}

/// How much of an entry is read to recognise it and measure it.
///
/// Sixteen bytes named the format, and the dimensions were then read from a second stream —
/// two seeks per page, 115 000 of them over a library of 57 686 pages, on a disk that gives
/// 38 MB/s and charges for every one. Measured over 2 769 real pages, 92 % have their
/// dimensions within the first kilobyte; the rest are JPEGs carrying an EXIF thumbnail, the
/// worst settled by 27 KB. Four kilobytes therefore answers nearly always in a single read,
/// and the few that need more are read again rather than guessed at.
const HEAD: usize = 4096;

/// Sidecars are few and small, so keeping them in memory is fine. Anything larger is not a
/// sidecar.
const MAX_SIDECAR: u64 = 4 * 1024 * 1024;

/// A ceiling on what one entry may put in memory.
///
/// A zip says how big each entry is *before* it is read, and that figure is written by
/// whoever made the file. `Vec::with_capacity(entry.size())` on an archive claiming a
/// hundred gigabytes asks the allocator for a hundred gigabytes, and Rust aborts the process
/// when that fails — no error, no log, the server simply gone.
///
/// A CBZ is not a trusted format and never was: these files are downloaded. Generous enough
/// that no real page comes near it — a 600 dpi A4 double-page spread is around forty
/// megabytes — and small enough that a bomb hits it instead of the machine.
const MAX_PAGE: u64 = 256 * 1024 * 1024;

/// What is inside an archive.
#[derive(Debug, Default)]
pub struct Content {
    pub pages: Vec<ArchivePage>,
    /// Whatever is not an image: ComicInfo.xml today, entry.json tomorrow.
    pub sidecars: Vec<(String, Vec<u8>)>,
    /// Two images sharing a name across folders: the global order is no longer defined.
    pub duplicate_names: Vec<String>,
}

impl Content {
    /// A sidecar by its bare name, whatever folder it was filed under.
    pub fn sidecar(&self, name: &str) -> Option<&[u8]> {
        self.sidecars
            .iter()
            .find(|(key, _)| {
                key.rsplit('/')
                    .next()
                    .is_some_and(|n| n.eq_ignore_ascii_case(name))
            })
            .map(|(_, bytes)| bytes.as_slice())
    }
}

#[derive(Debug, Clone)]
pub struct ArchivePage {
    /// Full path inside the archive, folders included.
    pub name: String,
    pub media_type: String,
    pub size: Option<u64>,
    pub dimension: Option<(u32, u32)>,
}

impl ArchivePage {
    /// The bare name, without its folder: that is what carries the order.
    pub fn short_name(&self) -> &str {
        self.name.rsplit('/').next().unwrap_or(&self.name)
    }
}

/// Everything inside an archive: the pages in reading order, and the sidecars.
///
/// Reading order follows the image **name**, not the folder tree. Pages may be filed into
/// chapter folders: those folders divide, they do not order. A 097.jpg under "Chapter 12"
/// reads before a 098.jpg under "Chapter 13", and the question of how to sort folders never
/// arises.
pub fn read(path: &Path, measure_dimensions: bool) -> Result<Content> {
    let file = File::open(path).with_context(|| format!("opening {}", path.display()))?;
    let mut archive =
        zip::ZipArchive::new(file).with_context(|| format!("reading {}", path.display()))?;

    let mut pages: Vec<ArchivePage> = Vec::new();
    let mut sidecars: Vec<(String, Vec<u8>)> = Vec::new();
    let mut reread: Vec<(usize, String)> = Vec::new();

    for i in 0..archive.len() {
        let Some((name, size, head)) = head_of(&mut archive, i)? else {
            continue;
        };
        match classify(&name, size, &head, measure_dimensions) {
            Member::Ignored => {}
            Member::Later => reread.push((i, name)),
            Member::Sidecar(bytes) => sidecars.push((name, bytes)),
            Member::Page { page, again } => {
                if again {
                    reread.push((i, name));
                }
                pages.push(page);
            }
        }
    }

    // The few that the head did not settle, read in full — up to the ceiling.
    for (index, name) in reread {
        let bytes = whole(&mut archive, index)?;
        match pages.iter_mut().find(|p| p.name == name) {
            Some(page) => page.dimension = crate::archive::images::dimension(&bytes),
            None => sidecars.push((name, bytes)),
        }
    }

    pages.sort_by(page_order);

    Ok(Content {
        duplicate_names: duplicated(&pages),
        pages,
        sidecars,
    })
}

/// What one member of the archive turned out to be, from its first bytes alone.
enum Member {
    /// A page. `again` when the head did not hold its dimensions and the rest of it might.
    Page { page: ArchivePage, again: bool },
    /// A sidecar, whole, because it was smaller than the head that was read.
    Sidecar(Vec<u8>),
    /// Worth a second seek: a sidecar too big to have arrived with the head, or a page the
    /// head could not measure.
    Later,
    /// Neither, and not worth another seek — a stray video in a CBZ is not metadata.
    Ignored,
}

fn classify(name: &str, size: u64, head: &[u8], measure_dimensions: bool) -> Member {
    let Some(kind) = crate::archive::images::media_type(head) else {
        if size > MAX_SIDECAR {
            return Member::Ignored;
        }
        return if size <= head.len() as u64 {
            Member::Sidecar(head[..size as usize].to_vec())
        } else {
            Member::Later
        };
    };
    // From the bytes already in hand, and back to the archive only when they turned out
    // not to be enough.
    let dimension = measure_dimensions
        .then(|| crate::archive::images::dimension(head))
        .flatten();
    Member::Page {
        again: measure_dimensions && dimension.is_none() && size > head.len() as u64,
        page: ArchivePage {
            name: name.to_string(),
            media_type: kind.to_string(),
            size: Some(size),
            dimension,
        },
    }
}

/// By page name, folders ignored — see the note on `read`. The full name breaks the tie, so
/// that two pages of the same short name still land in a fixed order.
fn page_order(a: &ArchivePage, b: &ArchivePage) -> std::cmp::Ordering {
    crate::archive::natural_order::compare(a.short_name(), b.short_name())
        .then_with(|| crate::archive::natural_order::compare(&a.name, &b.name))
}

/// One member read whole, up to the ceiling.
fn whole(archive: &mut zip::ZipArchive<File>, index: usize) -> Result<Vec<u8>> {
    let entry = archive.by_index(index)?;
    OPENED.fetch_add(1, Ordering::Relaxed);
    let mut bytes = Vec::new();
    entry.take(MAX_PAGE).read_to_end(&mut bytes)?;
    Ok(bytes)
}

/// The name, the declared size, and the first bytes of one member — enough to say what it
/// is without reading it whole. `None` for a directory entry, which holds nothing.
fn head_of(
    archive: &mut zip::ZipArchive<File>,
    i: usize,
) -> Result<Option<(String, u64, Vec<u8>)>> {
    let mut entry = archive.by_index(i)?;
    if entry.is_dir() {
        return Ok(None);
    }
    OPENED.fetch_add(1, Ordering::Relaxed);
    let mut head = vec![0u8; HEAD];
    let mut filled = 0;
    while filled < HEAD {
        match entry.read(&mut head[filled..])? {
            0 => break,
            n => filled += n,
        }
    }
    head.truncate(filled);
    Ok(Some((entry.name().to_string(), entry.size(), head)))
}

/// The short names carried by more than one page — the same 001.jpg under two chapter
/// folders. Sorted, so that a scan report reads the same way twice.
fn duplicated(pages: &[ArchivePage]) -> Vec<String> {
    let mut counts: std::collections::HashMap<&str, usize> = std::collections::HashMap::new();
    for page in pages {
        *counts.entry(page.short_name()).or_default() += 1;
    }
    let mut names: Vec<String> = counts
        .into_iter()
        .filter(|(_, n)| *n > 1)
        .map(|(name, _)| name.to_string())
        .collect();
    names.sort();
    names
}

/// One entry, by its full name inside the archive. `None` when it is not there.
pub fn extract(path: &Path, entry_name: &str) -> Result<Option<Vec<u8>>> {
    let file = File::open(path).with_context(|| format!("opening {}", path.display()))?;
    let mut archive =
        zip::ZipArchive::new(file).with_context(|| format!("reading {}", path.display()))?;

    OPENED.fetch_add(1, Ordering::Relaxed);
    let entry = match archive.by_name(entry_name) {
        Ok(entry) => entry,
        Err(zip::result::ZipError::FileNotFound) => return Ok(None),
        Err(e) => return Err(e).with_context(|| format!("{entry_name} in {}", path.display())),
    };

    // The declared size guides the allocation, it does not decide it: a header claiming
    // more than the ceiling gets the ceiling, and the read stops there too.
    let declared = entry.size();
    let mut bytes = Vec::with_capacity(declared.min(MAX_PAGE) as usize);
    entry
        .take(MAX_PAGE)
        .read_to_end(&mut bytes)
        .with_context(|| format!("reading {entry_name}"))?;
    if bytes.len() as u64 == MAX_PAGE && declared > MAX_PAGE {
        anyhow::bail!(
            "{entry_name} in {} claims {declared} bytes, past the {MAX_PAGE} allowed for one page",
            path.display()
        );
    }
    Ok(Some(bytes))
}
