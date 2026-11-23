//! Rewriting a CBZ without touching the images: replacing or removing sidecars, and
//! replacing a range of its pages with another archive's.
//!
//! Entries are copied **as they are**, still compressed: not a single page is decompressed
//! or recompressed. That is what makes the operation instant on a 150 MB file, and what
//! guarantees the pixels served tomorrow are exactly today's.
//!
//! The new archive is written beside the old one and then replaces it in one move: a power
//! cut halfway through leaves the volume intact, not half rewritten.
//!
//! Beside, under a name of its own: two downloads of the same volume stamp it at the same
//! moment, and a temporary named only after its target would have both of them writing into
//! one file and then both renaming it over the volume. See `store::files::beside`.

use std::fs::File;
use std::io::Write;
use std::path::Path;

use anyhow::{Context, Result};

pub fn replace_sidecar(path: &Path, entry_name: &str, content: &[u8]) -> Result<()> {
    rewrite(path, &[(entry_name, content)], &[])
}

/// Whether `member` is `wanted` once any folder is dropped and the case ignored. Archives
/// in the wild carry `x/comicinfo.XML`, and one left behind would be read again as metadata.
fn is_named(member: &str, wanted: &str) -> bool {
    member
        .rsplit('/')
        .next()
        .is_some_and(|n| n.eq_ignore_ascii_case(wanted))
}

/// One pass over the archive: every member named in `remove` goes, each of `add` replaces any
/// member of its name and is written in the order given, the rest is copied raw. Removing and
/// adding in separate rewrites would copy a 150 MB volume twice for one change, and a book
/// that gets both `entry.json` and `oneshot.json` would copy it twice again.
pub fn rewrite(path: &Path, add: &[(&str, &[u8])], remove: &[&str]) -> Result<()> {
    let temporary = crate::store::files::beside(path, "leaf-tmp");
    rewrite_into(path, &temporary, add, remove)?;
    std::fs::rename(&temporary, path).with_context(|| format!("replacing {}", path.display()))?;
    tracing::info!(file = %path.display(), "rewrote archive");
    Ok(())
}

/// The same pass, written at `destination` and leaving `source` exactly as it was.
///
/// For a caller that must keep the original until the copy is home: an import rewriting the
/// file it received cannot lose it to a failure halfway, or the session can no longer say
/// what it holds. On failure `destination` is removed, so nothing half-written is left.
pub fn rewrite_into(
    source: &Path,
    destination: &Path,
    add: &[(&str, &[u8])],
    remove: &[&str],
) -> Result<()> {
    // Before anything is opened: two members of one name would leave an archive with two
    // `entry.json`, and which one a reader meets first is nobody's decision.
    for (i, (name, _)) in add.iter().enumerate() {
        if add[..i]
            .iter()
            .any(|(earlier, _)| earlier.eq_ignore_ascii_case(name))
        {
            anyhow::bail!("{name} is added twice");
        }
    }
    let outcome = (|| -> Result<()> {
        let from = File::open(source).with_context(|| format!("opening {}", source.display()))?;
        let mut archive = zip::ZipArchive::new(from)?;
        let mut out = zip::ZipWriter::new(File::create(destination)?);

        for i in 0..archive.len() {
            let entry = archive.by_index_raw(i)?;
            let name = entry.name();
            if add.iter().any(|(added, _)| is_named(name, added))
                || remove.iter().any(|gone| is_named(name, gone))
            {
                continue;
            }
            // Raw: the compressed bytes go across untouched, so a 150 MB volume is rewritten
            // in the time it takes to copy it and not a page is re-encoded.
            out.raw_copy_file(entry)?;
        }

        for (entry_name, content) in add {
            out.start_file::<_, ()>(entry_name, zip::write::SimpleFileOptions::default())?;
            out.write_all(content)?;
        }
        out.finish()?;
        Ok(())
    })();

    if outcome.is_err() {
        let _ = std::fs::remove_file(destination);
    }
    outcome
}

/// The context of a splice that failed on the archive it takes pages from, and not on the
/// volume or the disk. Only that failure says anything about the file received: taken for
/// every failure, a volume of the library that could not be read had the file sent deleted
/// from its session as if it were the broken one.
#[derive(Debug, Clone, Copy)]
pub struct FromSource;

impl std::fmt::Display for FromSource {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str("reading the archive the pages are taken from")
    }
}

/// `target` with its pages `cut.0..=cut.1` replaced by `source`'s pages `take.0..=take.1`,
/// written at `destination`; both counted from 0 in reading order, as
/// [`crate::archive::cbz::read`] gives it. Neither archive is touched, and on failure
/// `destination` is removed.
///
/// Every page is copied raw, as a rewrite copies it: a chapter replaced is not a volume
/// re-encoded. What `target` holds besides its pages stays as it is, except `entry.json`,
/// which becomes `entry_json` — its chapters start elsewhere now. Nothing of `source` but
/// the pages taken: its own `entry.json` or ComicInfo describes another file.
///
/// **Every page is renamed** `0001.jpg` and on, wide enough for the whole volume and never
/// less than four digits, with its own extension in lower case. The reading order is the
/// pages' *names*, folders ignored (`cbz::read`): kept, the target's `063.jpg` and the
/// source's `063.jpg` would be two pages of one name in an undefined order, and the
/// source's `001.jpg`…`062.jpg` taken from a file numbered from 1 would sort among the
/// target's first chapter. Numbered in the order wanted, the order is the one written.
pub fn splice_into(
    target: &Path,
    source: &Path,
    destination: &Path,
    cut: (usize, usize),
    take: (usize, usize),
    entry_json: &[u8],
) -> Result<()> {
    let held = crate::archive::cbz::read(target, false)?.pages;
    let brought = crate::archive::cbz::read(source, false)
        .context(FromSource)?
        .pages;
    if cut.0 > cut.1 || cut.1 >= held.len() {
        anyhow::bail!(
            "pages {}–{} are not pages of {}, which has {}",
            cut.0,
            cut.1,
            target.display(),
            held.len()
        );
    }
    if take.0 > take.1 || take.1 >= brought.len() {
        anyhow::bail!(
            "pages {}–{} are not pages of {}, which has {}",
            take.0,
            take.1,
            source.display(),
            brought.len()
        );
    }
    // Which archive each page comes from, in the order it will be read.
    let order: Vec<(bool, &str)> = held[..cut.0]
        .iter()
        .map(|p| (true, p.name.as_str()))
        .chain(
            brought[take.0..=take.1]
                .iter()
                .map(|p| (false, p.name.as_str())),
        )
        .chain(held[cut.1 + 1..].iter().map(|p| (true, p.name.as_str())))
        .collect();
    let width = order.len().to_string().len().max(4);
    let pages: std::collections::HashSet<&str> = held.iter().map(|p| p.name.as_str()).collect();
    let renamed: Vec<String> = order
        .iter()
        .enumerate()
        .map(|(n, (_, name))| {
            let extension = Path::new(name)
                .extension()
                .map(|e| format!(".{}", e.to_string_lossy().to_lowercase()))
                .unwrap_or_default();
            format!("{:0width$}{extension}", n + 1)
        })
        .collect();
    let taken: std::collections::HashSet<&str> = renamed.iter().map(String::as_str).collect();

    let outcome = (|| -> Result<()> {
        let mut from_target = zip::ZipArchive::new(
            File::open(target).with_context(|| format!("opening {}", target.display()))?,
        )?;
        let mut from_source = File::open(source)
            .map_err(anyhow::Error::from)
            .and_then(|file| Ok(zip::ZipArchive::new(file)?))
            .context(FromSource)?;
        let mut out = zip::ZipWriter::new(File::create(destination)?);
        for ((own, name), renamed) in order.iter().zip(&renamed) {
            let member = if *own {
                from_target
                    .index_for_name(name)
                    .with_context(|| format!("{name} is listed and not there"))
                    .and_then(|i| Ok(from_target.by_index_raw(i)?))?
            } else {
                from_source
                    .index_for_name(name)
                    .with_context(|| format!("{name} is listed and not there"))
                    .and_then(|i| Ok(from_source.by_index_raw(i)?))
                    .context(FromSource)?
            };
            out.raw_copy_file_rename(member, renamed)?;
        }
        // Kept as they are — a ComicInfo included, whose `PageCount` still says what the
        // volume had before: the scan counts the pages it finds, and a ComicInfo is the
        // fallback this server reads, not one it writes. A member that is no page but bears
        // a page's new name — an empty `0100.jpg` left at the root — would have made two
        // members of one name and failed the whole splice; it goes, being nothing a reader
        // was shown.
        for i in 0..from_target.len() {
            let entry = from_target.by_index_raw(i)?;
            let name = entry.name();
            if pages.contains(name) || is_named(name, crate::scan::scanner::ENTRY_JSON) {
                continue;
            }
            if taken.contains(name) {
                tracing::warn!(
                    member = name,
                    "a member bearing a page's new name, left out"
                );
                continue;
            }
            out.raw_copy_file(entry)?;
        }
        out.start_file::<_, ()>(
            crate::scan::scanner::ENTRY_JSON,
            zip::write::SimpleFileOptions::default(),
        )?;
        out.write_all(entry_json)?;
        out.finish()?;
        Ok(())
    })();
    if outcome.is_err() {
        let _ = std::fs::remove_file(destination);
    }
    outcome
}

/// Whether a failed rewrite failed on the archive itself, rather than on the disk.
///
/// The two call for opposite answers: a file that is not a readable zip will never be one,
/// while a full or read-only disk says nothing about the file and the same rewrite works
/// once it is fixed. A zip cut short reads as an end of file met too early, and is the file's
/// fault too.
pub fn unreadable(error: &anyhow::Error) -> bool {
    match error.downcast_ref::<zip::result::ZipError>() {
        Some(zip::result::ZipError::Io(e)) => e.kind() == std::io::ErrorKind::UnexpectedEof,
        Some(_) => true,
        None => false,
    }
}
