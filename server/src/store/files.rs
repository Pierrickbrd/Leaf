//! Writing a file that something else may be reading.

use std::path::Path;
use std::sync::atomic::{AtomicU64, Ordering};

/// Writes a file so that a reader sees all of it or none of it.
///
/// `fs::write` truncates and then writes, and both of the things this server writes can be
/// read while they are being written:
///
///  - a **cached page**, produced twice at once by a request that missed the cache and a
///    warming thread about to prepare the same page. A reader in that window got a
///    **prefix** — and a prefix of a JPEG is served with an ETag and `max-age=31536000`
///    behind it, so one broken image is cached by the client for a year.
///  - a **sidecar**, written by an edit while a scan is reading it. A prefix does not parse,
///    so the scan concludes the file says nothing and the report announces fields as missing
///    that were there a moment before.
///
/// Measured, not supposed: with `fs::write`, 1959 of 2000 reads beside a writing thread
/// landed on a partial file.
///
/// Beside, then renamed. A rename within a directory is atomic, so the name points at the
/// old bytes or the new ones and never at half of either — which also means the two must
/// sit in the same directory, and they do.
pub fn write_whole(path: &Path, bytes: &[u8]) -> std::io::Result<()> {
    use std::io::Write;
    let beside = beside(path, "part");
    let outcome = (|| {
        let mut file = std::fs::File::create(&beside)?;
        file.write_all(bytes)?;
        file.sync_data()
    })();
    if let Err(e) = outcome {
        let _ = std::fs::remove_file(&beside);
        return Err(e);
    }
    std::fs::rename(&beside, path).inspect_err(|_| {
        let _ = std::fs::remove_file(&beside);
    })
}

/// A name for a file being written beside `path`, in the same directory as it.
///
/// Named for the process **and the thread**, because two writers that share a temporary do
/// not queue: they interleave their bytes into the one file and then both rename it over
/// the target. On a sidecar that means a truncated record; on a 150 MB volume rewritten by
/// `archive::cbz_writer`, it means the volume.
///
/// The extension is kept in the middle of the name rather than replaced, so a leftover
/// still says which file it was going to become.
pub fn beside(path: &Path, suffix: &str) -> std::path::PathBuf {
    let kept = path
        .extension()
        .map(|e| format!("{}.", e.to_string_lossy()))
        .unwrap_or_default();
    path.with_extension(format!(
        "{kept}{}.{:x}.{suffix}",
        std::process::id(),
        thread_number()
    ))
}

/// Something stable and distinct per thread. `ThreadId` has no stable numeric form on
/// stable Rust, so it is counted here instead.
fn thread_number() -> u64 {
    use std::cell::Cell;
    static NEXT: AtomicU64 = AtomicU64::new(0);
    thread_local! {
        static MINE: Cell<u64> = const { Cell::new(u64::MAX) };
    }
    MINE.with(|mine| {
        if mine.get() == u64::MAX {
            mine.set(NEXT.fetch_add(1, Ordering::Relaxed));
        }
        mine.get()
    })
}
