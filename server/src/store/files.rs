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
    // Named for the process and thread, so two writers do not share a temporary.
    let beside = path.with_extension(format!("{}.{:x}.part", std::process::id(), thread_number()));
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
