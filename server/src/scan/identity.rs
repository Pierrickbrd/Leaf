//! Where an identifier comes from, and why it stopped being the path.
//!
//! `scanner::id_of` hashes the absolute path, so a folder renamed is a folder with new
//! identifiers: the scan inserts the new rows, [`super::scanner::Scanner::prune`] deletes
//! the old, and `progress.entry_id … ON DELETE CASCADE` takes the reading position with
//! them. Measured — a volume read to page 42, `Death Note` renamed `Death Note (VF)`,
//! rescanned:
//!
//! ```text
//! AFTER THE RENAME — entries 1  editions 1  progress 0
//! ```
//!
//! Progress is the only thing a scan does not rebuild; `lib.rs` and the systemd unit both
//! say so, and the unit carries `RequiresMountsFor` for exactly that reason. A disk that
//! failed to mount was covered. A folder somebody renamed was not, and it takes less effort
//! than a disk: fixing a typo, adding « (VF) », filing two series under a universe.
//!
//! So the identity moves into the folder. A sidecar carries it, and a `mv` carries the
//! sidecar — which also means a folder copied without its sidecars is a new folder, and
//! that is the trade this makes on purpose.

use std::collections::HashMap;
use std::hash::{BuildHasher, RandomState};
use std::path::{Path, PathBuf};

use crate::metadata::sidecars::{Document, FORMAT_VERSION};
use crate::scan::report::ScanReport;

/// The key a sidecar keeps its identifier under.
pub const FIELD: &str = "id";

/// Which folder is already using which identifier, for the length of one scan.
pub type Claimed = HashMap<String, PathBuf>;

/// The identifier of a folder: the one its sidecar carries, or one stamped there now.
///
/// `path_id` is what the folder used to be called, and is still what it is called whenever
/// the sidecar cannot answer — an unwritable disk, or an identifier another folder is
/// already using. Neither refuses the folder: a library indexed by halves because of one
/// duplicate is worse than a library one folder of which is identified the old way.
pub fn of(
    folder: &Path,
    sidecar: &str,
    path_id: &str,
    claimed: &mut Claimed,
    report: &mut ScanReport,
) -> String {
    let file = folder.join(sidecar);
    let document = match std::fs::read(&file) {
        // Empty is nothing, same as absent: there is nothing here to lose by stamping it.
        Ok(bytes) if bytes.is_empty() => Document::default(),
        // Anything else has to parse as this format's shape before an identifier is stamped
        // onto it and the result written back. Before this branch existed the scan called
        // `Document::of`, which turns a parse failure into the same empty document a missing
        // file produces — so `identity::of` stamped an id on that emptiness and
        // `write_whole` wrote it over the file, and a `work.json` with one trailing comma
        // came back `{"id":…,"leaf":1}`: title, authors, genres gone, and neither
        // `disregarded` nor `contradictions` said a word. A scan that only reads tolerates
        // this; a scan about to write cannot — a server that knows the bytes are unreadable
        // has no business writing them over bytes that are not.
        Ok(bytes) => match Document::parse(&bytes) {
            Ok(document) => document,
            Err(reason) => {
                report.disregarded.push(format!(
                    "{}: {reason} — this folder is identified by its path, as before",
                    file.display()
                ));
                return settle(path_id.to_string(), folder, claimed);
            }
        },
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => Document::default(),
        Err(e) => {
            report.disregarded.push(format!(
                "{}: {e} — this folder is identified by its path, as before",
                file.display()
            ));
            return settle(path_id.to_string(), folder, claimed);
        }
    };

    // **Never corrected, only stamped.** A sidecar that already carries one keeps it,
    // whatever it looks like: another installation of Leaf may have written it, and
    // rewriting it would part a library from its own reading positions.
    if let Some(already) = document.text(FIELD).map(str::to_owned) {
        if !already.is_empty() {
            return agreed(already, folder, path_id, claimed, report);
        }
    }
    // Present, and not a string. `text` answers `None` for it exactly as it does for a field
    // that is not there, so the stamping below would overwrite it — the one case "stamped,
    // never corrected" did not cover, and the one where correcting is least defensible: a
    // field this server does not understand may be one a later version does, or one another
    // installation wrote. Left alone, said once, and the folder keeps the identity it had.
    if document.has(FIELD) && document.text(FIELD).is_none() {
        report.contradictions.push(format!(
            "{}: its `{FIELD}` is not a string — this folder is identified by its path, and \
             the field is left as it is",
            file.display()
        ));
        return settle(path_id.to_string(), folder, claimed);
    }

    let minted = minted();
    let mut document = document;
    document.set(FIELD, serde_json::Value::String(minted.clone()));
    // A file being created from nothing still has to be one this format describes.
    if !document.has("leaf") {
        document.set("leaf", serde_json::Value::from(FORMAT_VERSION));
    }

    match document
        .bytes()
        .map_err(anyhow::Error::from)
        .and_then(|bytes| Ok(crate::store::files::write_whole(&file, &bytes)?))
    {
        Ok(()) => settle(minted, folder, claimed),
        Err(e) => {
            // A library mounted read-only has to stay readable. It is said once, and the
            // folder keeps the identity it had.
            report.disregarded.push(format!(
                "{}: {e} — this folder is identified by its path, as before",
                file.display()
            ));
            settle(path_id.to_string(), folder, claimed)
        }
    }
}

/// An identifier a folder already carried, checked against the ones already met.
///
/// Two folders carrying the same one is a contradiction rather than a choice, and it happens
/// by copying a folder with its sidecar — the most ordinary gesture there is. The second one
/// met keeps its path as its identity and both are named, because refusing would index the
/// library by halves over a duplicate nobody has seen.
fn agreed(
    id: String,
    folder: &Path,
    path_id: &str,
    claimed: &mut Claimed,
    report: &mut ScanReport,
) -> String {
    if let Some(other) = claimed.get(&id) {
        if other != folder {
            report.contradictions.push(format!(
                "{} and {} both carry the identifier {id} — the second is identified by its \
                 path, as before",
                other.display(),
                folder.display()
            ));
            return settle(path_id.to_string(), folder, claimed);
        }
    }
    settle(id, folder, claimed)
}

fn settle(id: String, folder: &Path, claimed: &mut Claimed) -> String {
    claimed.insert(id.clone(), folder.to_path_buf());
    id
}

/// Sixteen hexadecimal characters, the same shape the path ones have always had — so nothing
/// downstream learns a second format.
///
/// Drawn rather than derived. Deriving from a path is the very thing being abandoned, and
/// deriving from a name would collide two works called the same thing in two universes.
pub fn minted() -> String {
    // `RandomState` is seeded by the operating system, and a fresh one per call is what
    // makes this a draw rather than a sequence.
    let now = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_nanos())
        .unwrap_or_default();
    format!("{:016x}", RandomState::new().hash_one(now))
}

/// The identifier of something that has no sidecar to carry one: an implicit edition, an
/// entry inside an archive.
///
/// **Nothing is written into an archive.** An identifier per entry, stamped into each CBZ's
/// `entry.json`, would mean reopening and rewriting thousands of archives — hours, and every
/// byte of the library touched for one field. So an entry is « this file, in this edition »,
/// which no rename of a folder above it changes.
///
/// A file renamed is therefore another entry, and that is the meaning intended: following a
/// file through its rename would mean identifying it by its contents, and the same volume
/// scanned twice is not byte for byte the same file.
pub fn under(parent: &str, leaf: &str) -> String {
    super::scanner::short(&format!("{parent}/{leaf}"))
}
