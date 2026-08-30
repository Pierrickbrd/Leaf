//! Replacing a sidecar inside a CBZ without touching the images.
//!
//! Entries are copied **as they are**, still compressed: not a single page is decompressed
//! or recompressed. That is what makes the operation instant on a 150 MB file, and what
//! guarantees the pixels served tomorrow are exactly today's.
//!
//! The new archive is written beside the old one and then replaces it in one move: a power
//! cut halfway through leaves the volume intact, not half rewritten.

use std::fs::File;
use std::io::Write;
use std::path::Path;

use anyhow::{Context, Result};

pub fn replace_sidecar(path: &Path, entry_name: &str, content: &[u8]) -> Result<()> {
    let temporary = path.with_extension(format!(
        "{}.leaf-tmp",
        path.extension()
            .map(|e| e.to_string_lossy().to_string())
            .unwrap_or_default()
    ));

    let outcome = (|| -> Result<()> {
        let source = File::open(path).with_context(|| format!("opening {}", path.display()))?;
        let mut archive = zip::ZipArchive::new(source)?;
        let mut out = zip::ZipWriter::new(File::create(&temporary)?);

        for i in 0..archive.len() {
            let entry = archive.by_index_raw(i)?;
            let name = entry.name().to_string();
            if name
                .rsplit('/')
                .next()
                .is_some_and(|n| n.eq_ignore_ascii_case(entry_name))
            {
                continue;
            }
            // Raw: the compressed bytes go across untouched, so a 150 MB volume is rewritten
            // in the time it takes to copy it and not a page is re-encoded.
            out.raw_copy_file(entry)?;
        }

        out.start_file::<_, ()>(entry_name, zip::write::SimpleFileOptions::default())?;
        out.write_all(content)?;
        out.finish()?;
        Ok(())
    })();

    if let Err(e) = outcome {
        let _ = std::fs::remove_file(&temporary);
        return Err(e);
    }
    std::fs::rename(&temporary, path).with_context(|| format!("replacing {}", path.display()))?;
    tracing::info!(entry = entry_name, file = %path.display(), "rewrote sidecar");
    Ok(())
}
