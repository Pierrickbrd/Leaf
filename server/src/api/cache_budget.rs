//! Keeping the cache of resized pages inside its budget.
//!
//! Removes the least recently *read* files until it fits again — read, not written, which is
//! why serving from the cache stamps the file. A cache that evicts by write order throws
//! away the page you are on.

use std::path::Path;

/// Walks the cache and deletes the coldest files until the total fits.
pub fn enforce(root: &Path, max_bytes: u64) {
    if max_bytes == 0 || !root.exists() {
        return;
    }
    let mut files = Vec::new();
    let mut total = 0u64;
    if let Err(e) = collect(root, &mut files, &mut total) {
        tracing::warn!(cache = %root.display(), error = %e, "cache not readable");
        return;
    }
    if total <= max_bytes {
        return;
    }

    // Coldest first.
    files.sort_by_key(|(_, _, read)| *read);

    let mut freed = 0u64;
    let mut removed = 0usize;
    for (path, size, _) in files {
        if total - freed <= max_bytes {
            break;
        }
        if std::fs::remove_file(&path).is_ok() {
            freed += size;
            removed += 1;
        }
    }
    tracing::info!(
        removed,
        freed_mb = freed / (1024 * 1024),
        "cache trimmed back under its budget"
    );
}

/// The smallest a file costs, whatever it holds: one block of the filesystem.
pub const FLOOR: u64 = 4096;

/// What a file in the cache really costs the disk — its allocated blocks, and never less
/// than one block.
///
/// Counted by its length, a page not worth resizing weighed nothing: its marker is empty.
/// Any key allowed to read could then ask every page at every width from 1 to 4096 and
/// leave a file behind each time, millions of them, under a budget that never moved — and
/// a marker is an inode and a block like any other file.
pub fn on_disk(meta: &std::fs::Metadata) -> u64 {
    #[cfg(unix)]
    let allocated = {
        use std::os::unix::fs::MetadataExt;
        meta.blocks().saturating_mul(512)
    };
    #[cfg(not(unix))]
    let allocated = 0;
    meta.len().max(allocated).max(FLOOR)
}

type Entry = (std::path::PathBuf, u64, std::time::SystemTime);

/// Anything that cannot be read is skipped rather than fatal.
///
/// The warming threads write into this cache while it is being walked, so a file can
/// disappear between `read_dir` and `metadata` for no reason worth reporting. Giving up on
/// the whole sweep because of one of them meant the cache stayed over its budget until the
/// next attempt happened to be luckier.
fn collect(dir: &Path, out: &mut Vec<Entry>, total: &mut u64) -> std::io::Result<()> {
    for entry in std::fs::read_dir(dir)?.flatten() {
        let path = entry.path();
        let Ok(meta) = entry.metadata() else { continue };
        if meta.is_dir() {
            let _ = collect(&path, out, total);
        } else if meta.is_file() {
            let cost = on_disk(&meta);
            *total += cost;
            out.push((path, cost, meta.modified().unwrap_or(std::time::UNIX_EPOCH)));
        }
    }
    Ok(())
}
