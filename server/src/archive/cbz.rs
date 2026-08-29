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
        let (name, size, head) = {
            let mut entry = archive.by_index(i)?;
            if entry.is_dir() {
                continue;
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
            (entry.name().to_string(), entry.size(), head)
        };

        match crate::archive::images::media_type(&head) {
            None => {
                if size <= MAX_SIDECAR {
                    if size <= head.len() as u64 {
                        sidecars.push((name, head[..size as usize].to_vec()));
                    } else {
                        reread.push((i, name));
                    }
                }
            }
            Some(kind) => {
                // From the bytes already in hand, and back to the archive only when they
                // turned out not to be enough.
                let dimension = if measure_dimensions {
                    crate::archive::images::dimension(&head)
                } else {
                    None
                };
                let needs_more =
                    measure_dimensions && dimension.is_none() && size > head.len() as u64;
                pages.push(ArchivePage {
                    name: name.clone(),
                    media_type: kind.to_string(),
                    size: Some(size),
                    dimension,
                });
                if needs_more {
                    reread.push((i, name));
                }
            }
        }
    }

    // The few that the head did not settle, read in full — up to the ceiling.
    for (index, name) in reread {
        let mut bytes = Vec::new();
        {
            let entry = archive.by_index(index)?;
            OPENED.fetch_add(1, Ordering::Relaxed);
            entry.take(MAX_PAGE).read_to_end(&mut bytes)?;
        }
        match pages.iter_mut().find(|p| p.name == name) {
            Some(page) => page.dimension = crate::archive::images::dimension(&bytes),
            None => sidecars.push((name, bytes)),
        }
    }

    pages.sort_by(|a, b| {
        crate::archive::natural_order::compare(a.short_name(), b.short_name())
            .then_with(|| crate::archive::natural_order::compare(&a.name, &b.name))
    });

    let mut counts: std::collections::HashMap<&str, usize> = std::collections::HashMap::new();
    for page in &pages {
        *counts.entry(page.short_name()).or_default() += 1;
    }
    let mut duplicate_names: Vec<String> = counts
        .into_iter()
        .filter(|(_, n)| *n > 1)
        .map(|(name, _)| name.to_string())
        .collect();
    duplicate_names.sort();

    Ok(Content {
        pages,
        sidecars,
        duplicate_names,
    })
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
