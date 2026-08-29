//! The bulk path: a whole folder at once.
//!
//! Used for the first seeding and for a complete series; the everyday path is one file at a
//! time, in [`crate::api::intake`].
//!
//! Three steps: announce a manifest, send what is missing, commit. Committing renames the
//! inbox into the library — hence the requirement that both sit on the same filesystem. A
//! rename is instant and atomic; copying nine gigabytes is neither.
//!
//! **Nothing is ever deleted here.** What is left over is reported and shown to you by name
//! before you decide, so a wrong manifest cannot destroy anything.

use std::io::{Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};

use anyhow::{Context, Result};

use crate::api::{absent, invalid};
use serde::{Deserialize, Serialize};

use crate::api::intake::token;

const MANIFEST: &str = "_manifest.json";

#[derive(Debug, Clone, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ManifestFile {
    pub path: String,
    pub size: u64,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub checksum: Option<String>,
}

/// What a manifest claims to cover.
///
/// The desktop is a workbench, not a mirror: a series imported and then deleted locally can
/// no longer be announced in full. So a manifest never means "here is everything that
/// exists" unless it says so explicitly.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, Deserialize, Serialize)]
pub enum Scope {
    /// I am bringing these files, I say nothing about the rest.
    #[default]
    #[serde(rename = "ADDITION")]
    Addition,
    /// Here is the whole series: whatever is not in it is reported as an orphan.
    #[serde(rename = "COMPLETE")]
    Complete,
}

#[derive(Debug, Clone, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ImportRequest {
    /// The folder chosen in the application: "Bleach", "Terres d'Arran".
    pub root: String,
    pub files: Vec<ManifestFile>,
    #[serde(default)]
    pub scope: Scope,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ImportOpened {
    pub id: String,
    pub root: String,
    pub to_send: Vec<String>,
    pub already_there: Vec<String>,
    pub bytes_to_send: u64,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ImportState {
    pub id: String,
    pub root: String,
    /// How much of each in-flight file the server holds: enough to resume.
    pub received: std::collections::BTreeMap<String, u64>,
    pub missing: Vec<String>,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ImportResult {
    pub root: String,
    pub installed: usize,
    /// On the server, absent from your manifest. Never deleted automatically.
    pub orphans: Vec<String>,
    /// Arrived complete and did not match the checksum announced for it. Left in the inbox
    /// rather than installed: a volume that travelled wrong is worse than one that did not
    /// travel, because nothing afterwards would tell you.
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub corrupt: Vec<String>,
    /// Announced, and not here in full. Nothing is wrong with them — they are the ones the
    /// transfer had not reached when it stopped.
    ///
    /// This is what makes stopping in the middle a decision rather than an accident: commit
    /// installs every volume that arrived whole, and says which are still to come.
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub pending: Vec<String>,
    /// The session is still there. Send what is pending against the same id and commit
    /// again; the bytes already transferred are not sent twice.
    pub open: bool,
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct CleanupRequest {
    pub root: String,
    pub files: Vec<String>,
}

/// The offset a client asked to write at is past what the server holds.
#[derive(Debug)]
pub struct BadOffset {
    pub received: u64,
}

/// A session left open, and what it is holding.
#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Open {
    pub id: String,
    pub root: String,
    /// How many of the manifest's files are here in full.
    pub complete: usize,
    pub of: usize,
    /// What the session holds on the disk. The inbox sits on the library's own filesystem,
    /// so this is the library's own space.
    pub bytes: u64,
    /// When it last changed, in milliseconds. What matters is how long it has sat there.
    pub last_touched_at: i64,
}

pub struct BulkImport {
    inbox: PathBuf,
    library: PathBuf,
}

impl BulkImport {
    pub fn new(inbox: &Path, library: &Path) -> Self {
        BulkImport {
            inbox: inbox.to_path_buf(),
            library: library.to_path_buf(),
        }
    }

    pub fn open(&self, request: &ImportRequest) -> Result<ImportOpened> {
        let root = plain_name(&request.root)?;
        let id = format!("imp_{}", token());
        let session = self.inbox.join(&id);
        std::fs::create_dir_all(&session)?;

        let target = self.library.join(&root);
        let missing: Vec<&ManifestFile> = request
            .files
            .iter()
            .filter(|f| !already_home(&target, f))
            .collect();

        let stored = ImportRequest {
            root: root.clone(),
            files: request.files.clone(),
            scope: request.scope,
        };
        std::fs::write(session.join(MANIFEST), serde_json::to_vec_pretty(&stored)?)?;
        tracing::info!(
            id,
            root,
            to_send = missing.len(),
            of = request.files.len(),
            "import opened"
        );

        let to_send: Vec<String> = missing.iter().map(|f| f.path.clone()).collect();
        Ok(ImportOpened {
            bytes_to_send: missing.iter().map(|f| f.size).sum(),
            already_there: request
                .files
                .iter()
                .map(|f| f.path.clone())
                .filter(|p| !to_send.contains(p))
                .collect(),
            to_send,
            id,
            root,
        })
    }

    pub fn state(&self, id: &str) -> Result<Option<ImportState>> {
        let Some(session) = self.session(id)? else {
            return Ok(None);
        };
        let Some(manifest) = self.manifest(&session) else {
            return Ok(None);
        };
        let target = self.library.join(&manifest.root);

        let mut received = std::collections::BTreeMap::new();
        let mut missing = Vec::new();
        for f in &manifest.files {
            // Already in the library counts as held. Without this, a commit that installed
            // half a manifest and kept the session for the rest reported the installed half
            // as missing, and the client sent it all over again.
            if already_home(&target, f) {
                received.insert(f.path.clone(), f.size);
                continue;
            }
            let held = under(&session, &f.path)
                .ok()
                .and_then(|p| std::fs::metadata(p).ok())
                .filter(|m| m.is_file())
                .map(|m| m.len());
            if let Some(held) = held {
                received.insert(f.path.clone(), held);
            }
            if held != Some(f.size) {
                missing.push(f.path.clone());
            }
        }
        Ok(Some(ImportState {
            missing,
            id: id.to_string(),
            root: manifest.root,
            received,
        }))
    }

    /// Resolves and checks where a chunk should land, writing nothing.
    ///
    /// Separate from the writing because the route streams the body straight to the disk:
    /// a volume is a hundred and thirty megabytes and a bulk import sends them back to
    /// back, so nothing may be held in memory on the way past.
    pub fn writing_at(
        &self,
        id: &str,
        path: &str,
        from: u64,
        max_bytes: u64,
    ) -> std::result::Result<PathBuf, ReceiveError> {
        let session = self
            .session(id)
            .map_err(ReceiveError::Other)?
            .ok_or_else(|| ReceiveError::Unknown(id.to_string()))?;
        let target = under(&session, path).map_err(ReceiveError::Other)?;
        if let Some(parent) = target.parent() {
            std::fs::create_dir_all(parent).map_err(|e| ReceiveError::Other(e.into()))?;
        }

        let already_there = std::fs::metadata(&target).map(|m| m.len()).unwrap_or(0);
        if from > already_there {
            return Err(ReceiveError::BadOffset(BadOffset {
                received: already_there,
            }));
        }
        // The same ceiling the one-file path has. This is the route that carries the
        // gigabytes, and it was the one without a limit: an unbounded write fills a disk
        // with no bug involved, only patience. Counted from the offset, so resuming at 95 %
        // is not mistaken for a fresh file of that size.
        if from >= max_bytes {
            return Err(ReceiveError::Other(over(max_bytes)));
        }
        Ok(target)
    }

    /// Writing at a given offset: a break at 95 % of a 130 MB volume resumes at the byte,
    /// not at zero. The client asks for the state first to know where to pick up.
    pub fn receive(
        &self,
        id: &str,
        path: &str,
        from: u64,
        body: &[u8],
        max_bytes: u64,
    ) -> std::result::Result<u64, ReceiveError> {
        let target = self.writing_at(id, path, from, max_bytes)?;
        if from + body.len() as u64 > max_bytes {
            return Err(ReceiveError::Other(over(max_bytes)));
        }
        let mut file = open_at(&target, from).map_err(|e| ReceiveError::Other(e.into()))?;
        file.write_all(body)
            .map_err(|e| ReceiveError::Other(e.into()))?;
        file.flush().map_err(|e| ReceiveError::Other(e.into()))?;
        file.metadata()
            .map(|m| m.len())
            .map_err(|e| ReceiveError::Other(e.into()))
    }

    /// Where a manifest root lands, resolved exactly as [`BulkImport::commit`] resolves it.
    pub fn target_of(&self, root: &str) -> PathBuf {
        self.library.join(root)
    }

    pub fn commit(&self, id: &str) -> Result<ImportResult> {
        let session = self
            .session(id)?
            .ok_or_else(|| absent(format!("unknown import: {id}")))?;
        let manifest = self
            .manifest(&session)
            .ok_or_else(|| absent(format!("unknown import: {id}")))?;
        let target = self.library.join(&manifest.root);

        let mut installed = 0usize;
        let mut pending = Vec::new();
        let mut corrupt = Vec::new();
        for f in &manifest.files {
            // A commit that installed part of a manifest keeps the session for the rest, so
            // a second one meets files that are already home. They are done, not missing.
            if already_home(&target, f) {
                continue;
            }
            let source = under(&session, &f.path)?;
            let complete = std::fs::metadata(&source)
                .map(|m| m.is_file() && m.len() == f.size)
                .unwrap_or(false);
            if !complete {
                pending.push(f.path.clone());
                continue;
            }
            // Only when the client announced one. It costs a full read of the file, which
            // on nine gigabytes is not free, so it is the sender's call — but a checksum
            // that is sent and never compared is worse than none, because it reads like a
            // guarantee.
            if let Some(announced) = &f.checksum {
                match checksum(&source) {
                    Ok(found) if found.eq_ignore_ascii_case(announced) => {}
                    Ok(found) => {
                        tracing::warn!(file = %f.path, announced, found, "checksum does not match");
                        // Dropped, not kept. The bytes are known to be wrong, so keeping
                        // them helps nobody — and `state` reports a file of the right size
                        // as held, which would have told the client there was nothing left
                        // to send while every commit went on refusing it.
                        let _ = std::fs::remove_file(&source);
                        corrupt.push(f.path.clone());
                        continue;
                    }
                    Err(e) => {
                        tracing::warn!(file = %f.path, error = %e, "could not read to check");
                        let _ = std::fs::remove_file(&source);
                        corrupt.push(f.path.clone());
                        continue;
                    }
                }
            }
            let destination = under(&target, &f.path)?;
            if let Some(parent) = destination.parent() {
                std::fs::create_dir_all(parent)?;
            }
            crate::api::intake::move_or_copy(&source, &destination)?;
            installed += 1;
        }

        let expected: std::collections::HashSet<&str> =
            manifest.files.iter().map(|f| f.path.as_str()).collect();
        let mut orphans: Vec<String> = if manifest.scope == Scope::Complete && target.exists() {
            walk(&target, &target)
                .into_iter()
                .filter(|p| !expected.contains(p.as_str()))
                .collect()
        } else {
            Vec::new()
        };
        orphans.sort();

        // Anything left aside stays where it is, so the client can send it again against
        // the same session rather than starting the whole folder over.
        let open = !pending.is_empty() || !corrupt.is_empty();
        if !open {
            let _ = std::fs::remove_dir_all(&session);
        }
        tracing::info!(
            id,
            installed,
            pending = pending.len(),
            orphans = orphans.len(),
            "import committed"
        );

        pending.sort();
        Ok(ImportResult {
            root: manifest.root,
            installed,
            orphans,
            corrupt,
            pending,
            open,
        })
    }

    /// Every session opened and not yet finished.
    ///
    /// Nothing sweeps these, and nothing could: a session is a partial transfer, and what it
    /// holds is exactly what you would not have to send again. That value decays — an import
    /// abandoned six months ago will not be resumed — but deciding when it has decayed is
    /// not the server's to decide, and this server has never removed a file nobody named.
    ///
    /// So they are listed. Without this there was no way to reach one at all: every route
    /// takes an id, and a desktop that crashed mid-import took the only copy of it with it.
    pub fn waiting(&self) -> Result<Vec<Open>> {
        let mut out = Vec::new();
        let Ok(folders) = std::fs::read_dir(&self.inbox) else {
            return Ok(out);
        };
        for folder in folders.flatten() {
            let id = folder.file_name().to_string_lossy().to_string();
            if !id.starts_with("imp_") {
                continue;
            }
            let Some(manifest) = self.manifest(&folder.path()) else {
                continue;
            };
            let target = self.library.join(&manifest.root);
            let (mut bytes, mut complete, mut touched) = (0u64, 0usize, 0i64);
            for f in &manifest.files {
                if already_home(&target, f) {
                    complete += 1;
                    continue;
                }
                let Ok(held) = under(&folder.path(), &f.path) else {
                    continue;
                };
                if let Ok(meta) = std::fs::metadata(&held) {
                    bytes += meta.len();
                    touched = touched.max(crate::api::intake::modified_at(&meta));
                    if meta.len() == f.size {
                        complete += 1;
                    }
                }
            }
            if touched == 0 {
                touched = std::fs::metadata(folder.path())
                    .map(|m| crate::api::intake::modified_at(&m))
                    .unwrap_or(0);
            }
            out.push(Open {
                id,
                of: manifest.files.len(),
                root: manifest.root,
                complete,
                bytes,
                last_touched_at: touched,
            });
        }
        out.sort_by_key(|o| o.last_touched_at);
        Ok(out)
    }

    pub fn abandon(&self, id: &str) -> Result<()> {
        if let Some(session) = self.session(id)? {
            let _ = std::fs::remove_dir_all(session);
        }
        Ok(())
    }

    /// Deletion on an explicit, file-by-file order, and never otherwise.
    ///
    /// It is the only route that removes anything from the library, and it never infers
    /// what to remove.
    pub fn cleanup(&self, request: &CleanupRequest) -> Result<Vec<String>> {
        let target = self.library.join(plain_name(&request.root)?);
        let mut removed = Vec::new();
        for path in &request.files {
            let file = under(&target, path)?;
            if file.is_file() && std::fs::remove_file(&file).is_ok() {
                tracing::info!(file = %file.display(), "removed on request");
                removed.push(path.clone());
            }
        }
        Ok(removed)
    }

    fn session(&self, id: &str) -> Result<Option<PathBuf>> {
        if !id.starts_with("imp_") || !id.chars().all(|c| c.is_ascii_alphanumeric() || c == '_') {
            return Err(invalid("invalid id"));
        }
        let folder = self.inbox.join(id);
        Ok(folder.exists().then_some(folder))
    }

    fn manifest(&self, session: &Path) -> Option<ImportRequest> {
        let bytes = std::fs::read(session.join(MANIFEST)).ok()?;
        serde_json::from_slice(&bytes).ok()
    }
}

/// Opens a file positioned at [`from`], truncating only when the write starts at zero.
pub fn open_at(target: &Path, from: u64) -> std::io::Result<std::fs::File> {
    let mut file = std::fs::OpenOptions::new()
        .create(true)
        .truncate(from == 0)
        .write(true)
        .open(target)?;
    file.seek(SeekFrom::Start(from))?;
    Ok(file)
}

pub fn over(max_bytes: u64) -> anyhow::Error {
    invalid(format!(
        "file larger than the {} MB limit",
        max_bytes / (1024 * 1024)
    ))
}

#[derive(Debug)]
pub enum ReceiveError {
    Unknown(String),
    BadOffset(BadOffset),
    Other(anyhow::Error),
}

/// Whether the library already holds this file.
///
/// The size, and then the checksum **when the manifest declares one**. Without it only the
/// size is compared, and a change that keeps the size is invisible: correcting a title in an
/// `entry.json` from "Tome 1" to "Tome 2" leaves the archive exactly as long as it was, and
/// the import would answer "already there" and never ask for it.
///
/// Declaring a checksum is therefore the way to say "compare the contents, not the length" —
/// and it costs a full read of both sides, which is why it is the sender's call rather than
/// something done to every file of every import.
fn already_home(target: &Path, file: &ManifestFile) -> bool {
    let Ok(path) = under(target, &file.path) else {
        return false;
    };
    if !std::fs::metadata(&path).is_ok_and(|m| m.is_file() && m.len() == file.size) {
        return false;
    }
    match &file.checksum {
        None => true,
        Some(announced) => checksum(&path).is_ok_and(|found| found.eq_ignore_ascii_case(announced)),
    }
}

/// A path arrives over the network: it must stay under its root.
///
/// Without this a "../../etc" would write wherever it liked on the server.
fn under(base: &Path, relative: &str) -> Result<PathBuf> {
    let resolved = normalise(&base.join(relative));
    let root = normalise(base);
    if !resolved.starts_with(&root) {
        return Err(invalid(format!("path outside its root: {relative}")));
    }
    Ok(resolved)
}

/// Resolves `.` and `..` textually, without touching the filesystem — the path may not
/// exist yet, which is the whole point of checking it before writing.
fn normalise(path: &Path) -> PathBuf {
    let mut out = PathBuf::new();
    for part in path.components() {
        match part {
            std::path::Component::ParentDir => {
                out.pop();
            }
            std::path::Component::CurDir => {}
            other => out.push(other.as_os_str()),
        }
    }
    out
}

fn plain_name(root: &str) -> Result<String> {
    let name = root.trim().trim_matches('/').to_string();
    if name.is_empty() || name.contains("..") {
        return Err(invalid(format!("invalid root: {root}")));
    }
    Ok(name)
}

/// How deep the orphan sweep will go before deciding a library is not shaped like one.
///
/// A symlink to a parent makes an ordinary walk recurse until the stack gives out, and
/// `is_dir()` follows symlinks. Nothing legitimate is more than three folders down —
/// universe, work, edition — so this is generous and still finite.
const MAX_DEPTH: usize = 8;

fn walk(dir: &Path, base: &Path) -> Vec<String> {
    walk_to(dir, base, MAX_DEPTH)
}

fn walk_to(dir: &Path, base: &Path, left: usize) -> Vec<String> {
    let mut out = Vec::new();
    if left == 0 {
        tracing::warn!(dir = %dir.display(), "too deep to sweep for orphans — a loop?");
        return out;
    }
    let Ok(entries) = std::fs::read_dir(dir) else {
        return out;
    };
    for entry in entries.flatten() {
        let path = entry.path();
        // The entry's own kind, not what it points at: a symlink to a folder is not
        // descended into, so a link back to a parent is a leaf rather than a loop.
        let Ok(kind) = entry.file_type() else {
            continue;
        };
        if kind.is_symlink() {
            continue;
        }
        if kind.is_dir() {
            out.extend(walk_to(&path, base, left - 1));
        } else if let Ok(relative) = path.strip_prefix(base) {
            out.push(relative.to_string_lossy().to_string());
        }
    }
    out
}

/// Only used when the application asks for a checksum comparison.
pub fn checksum(file: &Path) -> Result<String> {
    use sha2::{Digest, Sha256};
    let mut hasher = Sha256::new();
    let mut source =
        std::fs::File::open(file).with_context(|| format!("opening {}", file.display()))?;
    let mut buffer = vec![0u8; 1 << 16];
    loop {
        let read = source.read(&mut buffer)?;
        if read == 0 {
            break;
        }
        hasher.update(&buffer[..read]);
    }
    Ok(hasher
        .finalize()
        .iter()
        .map(|b| format!("{b:02x}"))
        .collect())
}
