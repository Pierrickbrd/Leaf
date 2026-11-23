//! The bulk path: a whole folder at once.
//!
//! Used for the first seeding and for a complete series; the everyday path is one file at a
//! time, in [`crate::api::intake`].
//!
//! Three steps: announce a manifest, send what is missing, commit. Committing renames the
//! inbox into the library — hence the requirement that both sit on the same filesystem. A
//! rename is instant and atomic; copying nine gigabytes is neither.
//!
//! **Nothing here is destroyed without being named first.** What is left over after a
//! `COMPLETE` import is reported and shown to you by name rather than removed; what a
//! manifest would overwrite is reported the same way, in the same preflight, before a byte
//! moves — see [`ImportOpened::replaces`]. Measured: a manifest declaring `Bleach/Tome
//! 1.cbz` at another size than the one already in the library used to fall into neither
//! `already_there` nor `creates`. It went straight into `to_send`, unnamed anywhere else,
//! and `commit`'s installation is an ordinary rename — which onto an existing path replaces
//! it.

use std::collections::{BTreeMap, HashMap, HashSet};
use std::ffi::OsStr;
use std::io::{Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};

use anyhow::{Context, Result};

use crate::api::{absent, invalid};
use serde::{Deserialize, Serialize};

use crate::api::intake::token;
use crate::api::reimport::{
    chapter_cut, folded, match_volumes, moved_page, series_of_universe, splice_outcome,
    FoundInFile, HeldVolume, ImportTarget, Incoming, Outcome, SeriesIs, Splice, TargetKind,
    TargetPlace, TrailStep, VolumeMatch,
};
pub use crate::api::reimport::{ChapterSplice, Matched, SpliceKind, SpliceOutcome};
use crate::metadata::sidecars;
use crate::store::held::{self, Aim, EditionFolder, Held, Place, Titled};
use crate::store::origins::Origin;
use crate::store::replaced::{self, Positions, Replaced};
use crate::store::repository::Ceiling;
use crate::store::Db;

const MANIFEST: &str = "_manifest.json";

/// How many imports may be open at once. Each is a folder in the inbox that a client fills,
/// and the only limit was the size of one file: a profile allowed to import could open
/// sessions without end, each holding up to what the disk had left. Far more than a person
/// has waiting — a drop of fifty series is one session — and finite.
pub const MAX_OPEN_IMPORTS: usize = 64;

/// Where each announced file and declaration lands, beside the manifest that keeps the
/// paths as announced. See [`Landings`].
const LANDINGS: &str = "_landings.json";
/// Where a replaced file waits, under the inbox: `replaced/<session>/<landing>`. Not a
/// session's own folder, which the commit removes once it is done.
const REPLACED: &str = "replaced";
/// In a session, the replacements a commit has begun: see [`Settling`].
const SETTLING: &str = "_settling";

/// The folder of a session the announced files and declarations are written into, apart
/// from the session's own files. Beside them, a client sending « _landings.json » as one of
/// its files rewrote where the commit would install, and the commit installed there.
const RECEIVED: &str = "files";
/// Left in a session by the commit that installed its first file: what is done once per
/// import is not done again by the commit that resumes it.
const BEGAN: &str = "_began";

/// Largest announcement taken, in bytes. The files travel separately; this is their paths,
/// sizes and checksums and the declarations' text — thirty-two mebibytes is a library of
/// several hundred thousand volumes, and still a body a server can hold to parse.
pub const MAX_ANNOUNCEMENT_BYTES: usize = 32 * 1024 * 1024;

#[derive(Debug, Clone, Default, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ManifestFile {
    pub path: String,
    pub size: u64,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub checksum: Option<String>,
    /// Asks for the archive to be rewritten as it is installed. `size` and `checksum` stay
    /// those of the file as sent: they are what is compared and verified on arrival.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub rewrite: Option<ArchiveRewrite>,
    /// What the client read in the file. A file that carries it is matched by its number
    /// to the volumes of the edition it arrives in; one that does not lands at its path.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub found: Option<FoundInFile>,
    /// The pages of this file that replace a chapter. Only with a `CHAPTER` target.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub splice: Option<Splice>,
    /// What every member of the archive decompresses to, as the sender read it: the pages are
    /// among them, and the server keeps the ones it recognises as such. Only asked
    /// for with the checksum, and only looked at for a file that is rewritten or spliced:
    /// the file checksum cannot say what the file written here is made of.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub members: Option<Vec<ManifestMember>>,
}

/// One member of an archive as the sender hashed it.
#[derive(Debug, Clone, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ManifestMember {
    /// Its path inside the archive. Said in the refusal; never what pages are matched by,
    /// since a splice renames them.
    pub name: String,
    pub size: u64,
    /// SHA-256 of the decompressed member, lowercase hex.
    pub checksum: String,
}

/// What to change inside an archive on its way into the library.
#[derive(Debug, Clone, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ArchiveRewrite {
    /// The `entry.json` to put in the archive, as text.
    pub entry_json: String,
    /// Members to drop, by bare name: every member of that name goes, in any folder of the
    /// archive and whatever its case.
    pub remove: Vec<String>,
    /// The `oneshot.json` to put in the archive, as text: the archive becomes a book standing
    /// alone. Absent, it receives none and keeps the one it carries.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub one_shot_json: Option<String>,
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

#[derive(Debug, Clone, Default, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ImportRequest {
    /// The folder chosen in the application: "Bleach", "Terres d'Arran".
    pub root: String,
    pub files: Vec<ManifestFile>,
    #[serde(default)]
    pub scope: Scope,
    /// The `universe.json`, `work.json` and `edition.json` of every folder under the root,
    /// with their contents.
    ///
    /// They travel in the announcement rather than among the files, and for two reasons at
    /// once. They are what says where all this goes — a folder *is* its declaration, and no
    /// route creates a universe — so the server has to read them before it can say what it
    /// would create. And they are a few hundred bytes, so sending them here costs nothing
    /// and saves a round trip per folder.
    ///
    /// Written into the session on the spot, which is how they reach the library at commit
    /// without being announced twice.
    #[serde(default)]
    pub sidecars: Vec<ManifestSidecar>,
    /// What this import reimports: a volume, a series, a universe, a chapter. The folder
    /// dropped is laid on the target's folder whatever it is called on the sender's disk.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub target: Option<ImportTarget>,
    /// With `SERIES_NEW_EDITIONS` on a series that declares no edition: the name its current
    /// edition takes when it gets a folder of its own beside the new ones.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub current_edition_name: Option<String>,
}

/// Where an open session's files land, kept beside its manifest.
///
/// The manifest keeps every path as it was announced, because that is what the client
/// sends, resumes and names its decisions by. Where each one goes is decided once, at the
/// announcement, against what the library held then, and kept here so that the state and
/// the commit install what the announcement said rather than deciding again.
#[derive(Debug, Clone, Default, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
struct Landings {
    /// The library folder everything lands under.
    root: String,
    /// The folder under `root` the drop is laid on: empty unless a target sits deeper.
    #[serde(default, skip_serializing_if = "String::is_empty")]
    under: String,
    /// Announced path → where it lands, relative to `root`. A file absent here lands
    /// nowhere.
    files: BTreeMap<String, String>,
    sidecars: BTreeMap<String, String>,
    /// What each file compared with the library was found to be.
    #[serde(default, skip_serializing_if = "BTreeMap::is_empty")]
    outcomes: BTreeMap<String, Outcome>,
    /// The work a targeted reimport touches, relative to the library: what the rescan
    /// after the commit is aimed at, when the root is a universe it could not be aimed at.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    work_folder: Option<String>,
    /// Announced path → the pages of the volume it lands on that its splice replaces,
    /// decided at the announcement from the chapters the volume held then.
    #[serde(default, skip_serializing_if = "BTreeMap::is_empty")]
    cuts: BTreeMap<String, (u32, u32)>,
    /// Names of editions that cannot stand, said at the announcement: while any remains the
    /// commit installs nothing.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    blocked: Vec<String>,
}

impl Landings {
    /// A session opened before landings were kept: everything lands at its own path.
    fn as_announced(manifest: &ImportRequest) -> Landings {
        Landings {
            root: manifest.root.clone(),
            files: manifest
                .files
                .iter()
                .map(|f| (f.path.clone(), f.path.clone()))
                .collect(),
            sidecars: manifest
                .sidecars
                .iter()
                .map(|s| (s.path.clone(), s.path.clone()))
                .collect(),
            ..Landings::default()
        }
    }

    /// The file as it lands: the same announcement, at its landing path. `None` for one
    /// that lands nowhere.
    fn at_landing(&self, file: &ManifestFile) -> Option<ManifestFile> {
        let at = self.files.get(&file.path)?;
        Some(ManifestFile {
            path: at.clone(),
            ..file.clone()
        })
    }

    /// Whether these landings stay in `library`, under the manifest's own root, and speak
    /// only of what it announced.
    fn checked(&self, library: &Path, manifest: &ImportRequest) -> Result<()> {
        if self.root != manifest.root {
            anyhow::bail!("{:?} is not the root announced", self.root);
        }
        let base = library.join(&self.root);
        if !self.under.is_empty() {
            under(&base, &self.under)?;
        }
        let files: HashSet<&str> = manifest.files.iter().map(|f| f.path.as_str()).collect();
        let sidecars: HashSet<&str> = manifest.sidecars.iter().map(|s| s.path.as_str()).collect();
        for (announced, at) in &self.files {
            if !files.contains(announced.as_str()) {
                anyhow::bail!("{announced} was not announced");
            }
            under(&base, at)?;
        }
        for (announced, at) in &self.sidecars {
            if !sidecars.contains(announced.as_str()) {
                anyhow::bail!("{announced} was not announced");
            }
            under(&base, at)?;
        }
        if let Some(announced) = self.outcomes.keys().find(|p| !files.contains(p.as_str())) {
            anyhow::bail!("{announced} was not announced");
        }
        // Read back from the inbox, which more than the server can write: a cut for a file
        // that asks for no splice, or one backwards, is not one this server wrote.
        for (announced, cut) in &self.cuts {
            let spliced = manifest
                .files
                .iter()
                .any(|f| &f.path == announced && f.splice.is_some());
            if !spliced || cut.0 > cut.1 {
                anyhow::bail!("{announced} has no range of pages to replace");
            }
        }
        if let Some(work) = &self.work_folder {
            under(library, work)?;
        }
        Ok(())
    }

    fn outcome(&self, path: &str) -> Option<Outcome> {
        self.outcomes.get(path).copied()
    }

    fn sidecar(&self, path: &str) -> String {
        self.sidecars
            .get(path)
            .cloned()
            .unwrap_or_else(|| path.to_string())
    }
}

/// Where one file of a session lands, and who asked.
struct Landing<'a> {
    id: &'a str,
    session: &'a Path,
    root: &'a str,
    /// Relative to `root`.
    at: &'a str,
    /// The profile that asked to go back to the first page, when it asked.
    reset: Option<&'a str>,
    profile: &'a str,
    /// The chapter this file's pages replace in the volume it lands on, when they do.
    splice: Option<&'a SpliceAt>,
}

/// A chapter about to be replaced in the volume a file lands on: the volume's pages it
/// replaces, the file's pages that replace them, and the volume's `entry.json` with every
/// page it names moved to where that page will be.
struct SpliceAt {
    cut: (u32, u32),
    take: Splice,
    entry_json: Vec<u8>,
}

impl SpliceAt {
    /// The last page of the range once replaced.
    fn new_to(&self) -> u32 {
        self.cut.0.saturating_add(self.take.to - self.take.from)
    }
}

/// What became of one file at its landing.
enum Installed {
    Added,
    Replaced {
        entry_id: Option<String>,
        kept_at: String,
    },
    /// Not an archive that can be rewritten: dropped, to be sent again.
    Unreadable,
    /// The name it was to take was taken since the announcement.
    Taken,
    /// It would replace a volume the index does not hold, which no record could name.
    Unheld,
    /// The file in place is neither the one this session found there nor the one it
    /// brought: something else replaced the volume meanwhile, and nothing of it is touched.
    Conflict,
}

/// A replacement under way, written in the session before the rename and removed once the
/// index has heard of it. It is what a commit cut between the two leaves to finish — or,
/// abandoned, to undo.
#[derive(Debug, Serialize, Deserialize)]
struct Settling {
    /// The path the announcement gave the new file.
    announced: String,
    replaced: Replaced,
    positions: Positions,
    /// The file in place when the replacement began — the one linked aside.
    old: FileId,
    /// The file that takes its place; a rename carries this identity to the destination.
    new: FileId,
    /// What a rewritten file holds, for telling it at the destination once the identity
    /// says nothing: a rewrite shares no inode with anything left in the session.
    #[serde(default)]
    new_checksum: Option<String>,
}

/// What tells one file on the disk from another at a glance: where it lives, its size and
/// its last write. A pre-check and never a verdict: the date moves when a backup tool or a
/// `touch` passes, the device number when a disk is mounted again, and either made a
/// session's own file look like another's for good. [`same_file`] decides.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
struct FileId {
    dev: u64,
    ino: u64,
    size: u64,
    modified: i64,
}

fn identity_of(file: &Path) -> std::io::Result<FileId> {
    let meta = std::fs::metadata(file)?;
    let modified = meta
        .modified()
        .ok()
        .and_then(|t| t.duration_since(std::time::UNIX_EPOCH).ok())
        .map(|d| i64::try_from(d.as_nanos()).unwrap_or(i64::MAX))
        .unwrap_or(0);
    #[cfg(unix)]
    let (dev, ino) = {
        use std::os::unix::fs::MetadataExt;
        (meta.dev(), meta.ino())
    };
    #[cfg(not(unix))]
    let (dev, ino) = (0, 0);
    Ok(FileId {
        dev,
        ino,
        size: meta.len(),
        modified,
    })
}

/// Whether two names hold the same file. A hard link shares the identity; the copy
/// [`link_or_copy`] falls back to across two disks has its own inode and its own date,
/// so it is compared by size and then by its bytes.
fn same_file(a: &Path, b: &Path) -> bool {
    match (identity_of(a), identity_of(b)) {
        (Ok(x), Ok(y)) if x == y => true,
        (Ok(x), Ok(y)) if x.size == y.size => {
            matches!((checksum(a), checksum(b)), (Ok(x), Ok(y)) if x == y)
        }
        _ => false,
    }
}

/// A folder made if missing, synced with the one holding it, so the name survives the
/// power going: what is written in it next is only found again through it.
fn folder_made(dir: &Path) -> Result<()> {
    std::fs::create_dir_all(dir)?;
    sync_folder(dir)?;
    if let Some(parent) = dir.parent() {
        sync_folder(parent)?;
    }
    Ok(())
}

/// A folder's own entries written down: a rename or a link in it is only durable once its
/// folder is.
fn sync_folder(dir: &Path) -> Result<()> {
    #[cfg(unix)]
    std::fs::File::open(dir)?.sync_all()?;
    #[cfg(not(unix))]
    let _ = dir;
    Ok(())
}

/// The same bytes under a second name: a hard link, which costs nothing and cannot be
/// half done, or a copy written beside and linked when the two are not on one disk.
/// Never over a file already at `to`.
///
/// The copy is synced before it takes the name, as [`crate::store::files::write_whole`]
/// syncs a file before its rename, and the folder after, which `write_whole` does not: the
/// old volume's only other name is about to be given to the new one, and a copy still in
/// the page cache when the power goes is a copy that was never made. Its temporary name is
/// the destination's own, so a leftover is found and replaced by the next attempt.
fn link_or_copy(from: &Path, to: &Path) -> Result<()> {
    if std::fs::hard_link(from, to).is_err() {
        let name = to
            .file_name()
            .map(|n| n.to_string_lossy().into_owned())
            .unwrap_or_default();
        let part = to.with_file_name(format!("{name}.part"));
        let _ = std::fs::remove_file(&part);
        let copied = (|| -> Result<()> {
            let mut source = std::fs::File::open(from)?;
            let mut copy = std::fs::File::create(&part)?;
            std::io::copy(&mut source, &mut copy)?;
            copy.sync_data()?;
            std::fs::hard_link(&part, to)?;
            Ok(())
        })();
        let _ = std::fs::remove_file(&part);
        copied.with_context(|| format!("copying {} to {}", from.display(), to.display()))?;
    }
    if let Some(folder) = to.parent() {
        sync_folder(folder)?;
    }
    Ok(())
}

/// The name the version being installed is prepared under, beside its destination: one per
/// session, so a leftover of a commit cut short is that session's own, removed by the next
/// commit or by abandoning it — never a dead process's, left in the folder for good and
/// reported among its orphans.
fn staged_for(destination: &Path, id: &str) -> PathBuf {
    let name = destination
        .file_name()
        .map(|n| n.to_string_lossy().into_owned())
        .unwrap_or_default();
    destination.with_file_name(format!("{name}.{id}.leaf-import"))
}

/// Whether the destination holds the file this replacement brought. The identity first,
/// which answers at once when nothing touched it; then the received file while the session
/// still holds it — the file was linked from it, so they are one — or, for a rewrite, which
/// shares nothing with it, the bytes counted when it was made.
fn arrived(settling: &Settling, destination: &Path, source: Option<&Path>) -> bool {
    let Ok(now) = identity_of(destination) else {
        return false;
    };
    if now == settling.new {
        return true;
    }
    match &settling.new_checksum {
        Some(sum) => {
            now.size == settling.new.size && checksum(destination).ok().as_ref() == Some(sum)
        }
        None => source.is_some_and(|source| source.is_file() && same_file(source, destination)),
    }
}

/// What [`BulkImport::reconcile`] made of a replacement a commit was cut in.
enum Settled {
    Finished(ReplacedFile),
    Cancelled,
    Conflicted,
}

/// Where the old file of a landing waits, relative to the inbox.
fn kept_at_for(landing: &Landing<'_>) -> String {
    format!("{REPLACED}/{}/{}", landing.id, landing.at)
}

fn now_ms() -> i64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_millis() as i64)
        .unwrap_or(0)
}

/// What a file found to be one of these does: nothing, and it is never sent. An identical
/// file is already there; one without a number, or at a number another file of the drop
/// also claims, has nowhere it could go without guessing.
fn stays_out(outcome: Option<Outcome>) -> bool {
    matches!(
        outcome,
        Some(Outcome::Same | Outcome::Unnumbered | Outcome::Twice)
    )
}

/// Everything an announcement needs from the index, read before anything else of it.
struct Landed {
    needing: Vec<NeedsAName>,
    problems: Vec<NameProblem>,
    lands_as: Vec<FiledAs>,
    collisions: Vec<Collided>,
    reshelved: Vec<Reshelved>,
    root: String,
    under: String,
    /// Per announced file, where it lands relative to `root`.
    files: Vec<Option<String>>,
    outcomes: Vec<Option<Outcome>>,
    /// Per announced declaration, where it lands relative to `root`.
    sidecars: Vec<String>,
    origins: HashMap<String, Origin>,
    volumes: Vec<VolumeMatch>,
    absent: Vec<HeldVolume>,
    target: Option<TargetPlace>,
    work_folder: Option<String>,
    /// The works the library holds elsewhere that a folder of the drop is, by that folder
    /// relative to `root`.
    known: HashMap<String, Known>,
    absent_series: Vec<AbsentSeries>,
    resembles: Vec<Resemblance>,
    splice: Option<SpliceOutcome>,
    /// Per announced file that replaces a chapter, the pages of its volume it replaces.
    cuts: BTreeMap<String, (u32, u32)>,
}

/// One folder's declaration, as it stands on the sender's disk.
#[derive(Debug, Clone, Deserialize, Serialize)]
pub struct ManifestSidecar {
    /// Relative to the root, so the server knows which folder it describes.
    pub path: String,
    /// The bytes, unparsed. What this server does not understand it still installs: a field
    /// a later version writes must survive a trip through one that does not know it.
    pub json: String,
}

/// Refuses an announcement carrying a declaration that is not a JSON object.
///
/// **Refused here, before a byte moves, and not skipped at commit.** A declaration that
/// cannot be read carries no metadata — the scan ignores it — so installing it over one the
/// library can read replaces something useful with something inert, and takes the folder's
/// identity with it. Skipping it silently at commit would be the other half of the same
/// mistake: the reader would be told the import worked and the file would still be wrong.
///
/// The scan tolerates a malformed sidecar, and that is not a contradiction. Reading a
/// library that already exists must keep working; writing a file into one is a different
/// act, and a server that knows the bytes are unreadable has no business writing them over
/// bytes that are not.
fn readable_declarations(sidecars: &[ManifestSidecar]) -> Result<()> {
    for sidecar in sidecars {
        // Named before it is parsed: a declaration with no path of its own reached `under`
        // a few lines later, which has nothing to resolve and fails as an error rather than
        // as a refusal — a malformed announcement answered 500 where it is a 400. What the
        // caller sent is wrong, not what the server did with it.
        if sidecar.path.trim().is_empty() {
            return Err(invalid(
                "a declaration arrived without the path it belongs to".to_string(),
            ));
        }
        let an_object = serde_json::from_str::<serde_json::Value>(&sidecar.json)
            .map(|v| v.is_object())
            .unwrap_or(false);
        if !an_object {
            return Err(invalid(format!(
                "{} is not a readable declaration — fix it before importing the folder",
                sidecar.path
            )));
        }
    }
    Ok(())
}

/// Refuses, before a byte moves, a rewrite the commit could not honour.
///
/// Checked here and not at the commit for the reason `readable_declarations` is: by then
/// the archive has travelled, and a refusal would cost the whole transfer. A name to remove
/// holding a separator or `..` is refused because a member is named, never located — the
/// rewrite has no business reaching a path. A name is matched at any depth and without
/// case, so it already reaches every member of that name.
fn readable_rewrites(files: &[ManifestFile]) -> Result<()> {
    for file in files {
        let Some(rewrite) = &file.rewrite else {
            continue;
        };
        let lower = file.path.to_lowercase();
        if !(lower.ends_with(".cbz") || lower.ends_with(".zip")) {
            return Err(invalid(format!(
                "{} is not an archive, so it cannot be rewritten",
                file.path
            )));
        }
        let an_object = |text: &str| {
            serde_json::from_str::<serde_json::Value>(text)
                .map(|v| v.is_object())
                .unwrap_or(false)
        };
        if !an_object(&rewrite.entry_json) {
            return Err(invalid(format!(
                "the entry.json to put in {} is not a JSON object",
                file.path
            )));
        }
        if rewrite
            .one_shot_json
            .as_deref()
            .is_some_and(|t| !an_object(t))
        {
            return Err(invalid(format!(
                "the oneshot.json to put in {} is not a JSON object",
                file.path
            )));
        }
        // An empty name names nothing: removed at any depth by its name, it matches no
        // member, and a request to remove nothing is a mistake of the sender's.
        if rewrite.remove.iter().any(|n| n.trim().is_empty()) {
            return Err(invalid(format!(
                "a name to remove from {} is empty",
                file.path
            )));
        }
        if let Some(name) = rewrite
            .remove
            .iter()
            .find(|n| n.contains('/') || n.contains('\\') || n.contains(".."))
        {
            return Err(invalid(format!(
                "{} cannot be removed from {}: a member is named, not located",
                name, file.path
            )));
        }
    }
    Ok(())
}

/// Refuses, before a byte moves, an announcement whose declarations contradict each other.
///
/// Two declarations in one folder say it is two things at once, and the scan would pick one
/// without telling anyone. Nor can one sit inside another that leaves it no room: see the
/// check below. That is a mistake of the sender, found here for the reason
/// `readable_declarations` is: afterwards the files have travelled.
///
/// A universe holding archives directly used to be refused too, as a volume placed on a
/// shelf of works. That turned away a universe's book standing alone, whose `oneshot.json`
/// is inside the archive, where an announcement cannot show it. The scan opens the archive:
/// it files the book, and reports the archives there that declare nothing.
fn coherent_declarations(sidecars: &[ManifestSidecar]) -> Result<()> {
    let folder_of = |path: &str| -> String {
        Path::new(path)
            .parent()
            .map(|p| p.to_string_lossy().into_owned())
            .unwrap_or_default()
    };
    let named = |folder: &str| -> String {
        if folder.is_empty() {
            "the root folder".to_string()
        } else {
            folder.to_string()
        }
    };

    // Ordered by path: when several nestings are wrong the message names the first one in
    // path order, the same on every run, instead of whichever a hash happened to visit first.
    let mut declared: BTreeMap<String, &'static str> = BTreeMap::new();
    for sidecar in sidecars {
        let Some(kind) = declared_by(Path::new(&sidecar.path)) else {
            continue;
        };
        let folder = folder_of(&sidecar.path);
        if declared.insert(folder.clone(), kind).is_some() {
            return Err(invalid(format!(
                "{} holds more than one declaration — a folder is a universe, a work or an \
                 edition, not several at once",
                named(&folder)
            )));
        }
    }

    // Nesting is checked here and not at the scan, which reads a universe inside a universe
    // as a series and says so afterwards (`a_universe_inside_a_universe_is_read_as_a_work_
    // and_the_report_says_so`). An announcement that writes it is the sender's mistake, and
    // this finds it before the files travel. Only the folders the announcement declares can
    // be compared: an edition whose series is already on the disk is not judged from here.
    //
    // The nearest declaring ancestor is enough: whatever is above it was checked against it
    // when it was itself the folder looked at.
    for (folder, kind) in &declared {
        let above = Path::new(folder.as_str())
            .ancestors()
            .skip(1)
            .map(|a| a.to_string_lossy().into_owned())
            .find_map(|a| declared.get(&a).map(|k| (a, *k)));
        let Some((outer, outer_kind)) = above else {
            continue;
        };
        // An edition straight under a universe passes: this compares only what the
        // announcement declares, and whether a work stands between them is for the scan, which
        // sees the files and reports what it cannot place instead of guessing.
        let fits = match outer_kind {
            "EDITION" => false,
            "WORK" => *kind == "EDITION",
            _ => *kind != "UNIVERSE",
        };
        if !fits {
            return Err(invalid(format!(
                "{} declares a {} inside {}, which declares a {}: the model has no room for it",
                named(folder),
                kind.to_lowercase(),
                named(&outer),
                outer_kind.to_lowercase()
            )));
        }
    }
    Ok(())
}

/// Refuses, before a byte moves, a `universe.json` whose reading orders cannot all be read:
/// an inclusion naming no order of the file, one carrying a work or a range beside it, or
/// one that closes a loop.
///
/// Found here for the reason `coherent_declarations` is: the scan would leave the inclusion
/// out and say so, but only after the files have travelled, and the sender is the one who
/// can fix the file. The rules are `scan::orders`'s own, so the announcement and the scan
/// cannot disagree about what a loop is.
///
/// What is **not** refused: two orders under one identifier (the scan keeps the first and
/// says so, and refusing would turn away libraries the server reads), and a step naming a
/// work the announcement does not carry (it may be on the disk already, or come in another
/// announcement).
fn coherent_orders(sidecars: &[ManifestSidecar]) -> Result<()> {
    for sidecar in sidecars {
        if declared_by(Path::new(&sidecar.path)) != Some("UNIVERSE") {
            continue;
        }
        // A file that does not read as a universe has no orders the scan could resolve
        // either; `readable_declarations` already answered for what is not an object.
        let Some(universe) = crate::metadata::sidecars::read::<
            crate::metadata::sidecars::UniverseJson,
        >(sidecar.json.as_bytes()) else {
            continue;
        };
        if let Some(((order, step), why)) = crate::scan::orders::refused(&universe.orders)
            .into_iter()
            .next()
        {
            return Err(invalid(format!(
                "{} — order \"{}\", step {}: {why}",
                sidecar.path,
                crate::scan::orders::declared_id(&universe.orders[order], order),
                step + 1
            )));
        }
    }
    Ok(())
}

/// Writes one declaration into the library, **keeping the identity already there**.
///
/// An identity is stamped by the scan and belongs to the library; it never travels in an
/// import. A folder prepared on a laptop and never scanned has no `id` in its `work.json`,
/// and copying that file over the library's would take the identity off it — measured: the
/// field came back `null`. The next scan then mints a new one, the rows it replaces are
/// pruned, and `ON DELETE CASCADE` takes every reading position of that series with them.
/// Which is the exact loss `scan::identity` exists to prevent, walked in through the import
/// door.
///
/// Anything reaching here parses: [`readable_declarations`] refused the announcement
/// otherwise, before a byte of it moved.
fn install_sidecar(source: &Path, destination: &Path) -> Result<()> {
    let arriving = std::fs::read(source)?;
    let mut document = sidecars::Document::of(&arriving);

    if let Ok(here) = std::fs::read(destination) {
        if let Some(already) = sidecars::Document::of(&here).text(crate::scan::identity::FIELD) {
            document.set(
                crate::scan::identity::FIELD,
                serde_json::Value::String(already.to_string()),
            );
        }
    }
    crate::store::files::write_whole(destination, &document.bytes()?)?;
    let _ = std::fs::remove_file(source);
    Ok(())
}

/// What a sidecar's own file name says it declares, or nothing when it declares nothing.
///
/// Written once because the same three steps — the name, its text, what it declares — were
/// spelled out at four call sites, and four copies of one expression is four places for it
/// to drift.
fn declared_by(at: &Path) -> Option<&'static str> {
    at.file_name().and_then(OsStr::to_str).and_then(declares)
}

/// How many folders down the root a declaration sits. An empty path is the root itself.
fn depth_of(at: &str) -> usize {
    if at.is_empty() {
        0
    } else {
        at.matches('/').count() + 1
    }
}

/// The names an import gives to new editions that cannot stand: one equal to an edition the
/// series holds, or to another in the same drop, however it is cased or accented. Two folders
/// cannot carry them, and « originale » beside « Originale » is one edition typed twice.
pub fn edition_name_problems(known: &[String], given: &[(String, String)]) -> Vec<NameProblem> {
    // Compared as the folders they make are: two long names that differ only past what a
    // folder name keeps are one folder.
    let key = |name: &str| crate::store::text::search_key(&crate::store::naming::folder_name(name));
    let mut problems = Vec::new();
    for (n, (path, name)) in given.iter().enumerate() {
        // A name that is nothing but dots, dashes or slashes makes the folder « Sans nom »:
        // it is no name.
        if crate::store::naming::folder_name(name) == "Sans nom"
            && crate::store::text::search_key(name) != crate::store::text::search_key("Sans nom")
        {
            problems.push(NameProblem {
                path: path.clone(),
                kind: "EMPTY",
                with: None,
            });
            continue;
        }
        let me = key(name);
        let twin = known
            .iter()
            .cloned()
            .chain(given[..n].iter().map(|(_, other)| other.clone()))
            .find(|other| key(other) == me);
        if let Some(with) = twin {
            problems.push(NameProblem {
                path: path.clone(),
                kind: "DUPLICATE",
                with: Some(with),
            });
        }
    }
    problems
}

/// Something the library does not hold yet, and would gain.
///
/// Answered before a byte moves, because "this will create a universe" is the one thing a
/// reader cannot undo by deleting a file afterwards — a folder that should have gone under
/// an existing shelf and made a second one beside it looks exactly like a folder that went
/// where it was meant to.
#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Creation {
    /// UNIVERSE, WORK or EDITION — which of the three declarations made it.
    pub kind: String,
    /// What it will be called, from the declaration itself and not from the folder name.
    pub name: String,
    /// Where under the root, so two works of the same name are told apart.
    pub at: String,
}

/// One declaration that would be written over another, and what the two disagree about.
///
/// The field names and not the values: a summary is four hundred words and a screen has one
/// line. « résumé, arcs » says what is at stake where the values could not fit — and it is
/// what a reader needs to decide, since the question is never « which of these two strings »
/// but « did I edit this here ».
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Declaration {
    pub path: String,
    /// What the library's own declaration calls itself — its `title`, or its `name`.
    pub present_name: Option<String>,
    /// The keys the two disagree about. Never `id`: an identity belongs to the library and
    /// is kept from its own copy whatever the arriving one says.
    pub differs: Vec<String>,
    /// What the library's own declaration holds under each of those keys; a key it does not
    /// carry is not here. « éditeur » says something differs, « Pika » says what would be
    /// lost — and a reader keeps a value they recognise, not a field name.
    pub present: serde_json::Map<String, serde_json::Value>,
}

/// One file that would land on another, and what the library already holds there.
///
/// A size alone says « different » without saying how. Measured on a real library: two
/// archives six hundred and eighty-two bytes apart, whose difference was entirely inside the
/// `entry.json` one of them carried — a title edited through the API months earlier. The only
/// way to find that out was to open both by hand, which is not a thing to ask of somebody
/// deciding whether to overwrite a volume.
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Replacement {
    pub path: String,
    /// What the arriving file weighs, as the manifest declares it.
    pub size: u64,
    /// What the library's own file weighs.
    pub present_size: u64,
    /// What the library's own file declares inside itself. `None` when it carries no
    /// `entry.json`, and also when `present_read` is false.
    pub present_title: Option<String>,
    pub present_number: Option<f64>,
    /// Whether the server opened the file it already holds. False past [`DEEPEST_READ`].
    pub present_read: bool,
}

/// A work the library already holds, that this folder declares somewhere else.
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Relocation {
    /// The identity it is already indexed under — which is what the move is aimed with.
    pub work_id: String,
    pub name: String,
    /// The folder it is in now, so a reader can see what is about to change.
    pub from: String,
    /// Where under the root it would go, the same way a `Creation` says it.
    pub at: String,
    /// How the folder was known to be this work: by the identity its `work.json` carries,
    /// or only by its title — which a reader confirms, since two series can share one.
    pub matched: Matched,
}

/// A work the library holds somewhere a drop declares one, as the index files it.
#[derive(Debug, Clone)]
struct Known {
    work_id: String,
    path: String,
    name: String,
    matched: Matched,
}

impl Known {
    fn of(work: &Titled, matched: Matched) -> Self {
        Known {
            work_id: work.work_id.clone(),
            path: work.path.to_string_lossy().into_owned(),
            name: work.name.clone(),
            matched,
        }
    }
}

/// A series of the universe aimed at that the drop does not bring. It stays: nothing is
/// removed by being left out, and saying so is what tells a reader the drop was not whole.
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct AbsentSeries {
    pub work_id: String,
    pub series_id: String,
    pub name: String,
    /// The volumes it holds.
    pub volumes: u32,
}

/// A series a drop would create whose title, folded, is one the library already holds.
///
/// « blue period » beside « Blue Period » is two folders on the disk and, almost always, one
/// series typed twice; once created, it is two series to merge by hand.
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Resemblance {
    /// Which folder of the drop, the same way a `Creation` says it.
    pub at: String,
    pub series_id: String,
    pub name: String,
    /// The folder the series it resembles is in.
    pub from: String,
}

/// Where one announced file will be filed, and under what name.
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct FiledAs {
    /// The path as announced.
    pub path: String,
    /// « Série › Édition », readable.
    pub folder: String,
    /// The name it will carry.
    pub file: String,
    /// The name it arrived with.
    pub was_called: String,
}

/// Two announced files that would be filed under one name: neither is.
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Collided {
    pub path: String,
    pub with: String,
}

/// What the commit of an import that adds editions needs to know before it lays them down.
#[derive(Debug, Clone)]
pub struct EditionsAdded {
    pub series: String,
    pub current_name: Option<String>,
    pub first_new: String,
}

/// An edition the drop adds that has no name yet: the client asks for one.
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct NeedsAName {
    /// The folder of the drop, `""` for files dropped loose, or `@current` for the edition
    /// the series holds now when it declares none.
    pub path: String,
    pub current: bool,
}

/// What is wrong with the names of the editions a drop adds: sending is refused until none is.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct NameProblem {
    pub path: String,
    /// `EMPTY` or `DUPLICATE`.
    pub kind: &'static str,
    /// The edition it would share a name with, for `DUPLICATE`.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub with: Option<String>,
}

/// Volumes the library holds that an import moves into another edition.
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Reshelved {
    pub edition: String,
    pub count: u32,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ImportOpened {
    /// Per file that is filed, where and under what name. Always present, possibly empty.
    pub lands_as: Vec<FiledAs>,
    /// Files whose canonical names meet; neither is filed.
    pub collisions: Vec<Collided>,
    /// Volumes already held that the import moves. Always present, possibly empty.
    pub reshelved: Vec<Reshelved>,
    /// Editions a `SERIES_NEW_EDITIONS` drop adds that have no name, and the current one of
    /// a series that declares none. Always present, possibly empty.
    pub editions_needing_a_name: Vec<NeedsAName>,
    /// Names that are empty or already taken, said before anything is sent; the commit
    /// refuses while any remains.
    pub problems: Vec<NameProblem>,
    pub id: String,
    pub root: String,
    /// What the library would gain. Empty when everything announced is already there under
    /// a name it knows.
    pub creates: Vec<Creation>,
    /// What it already has, somewhere else, and would file here instead.
    ///
    /// The sixth case of an import, and the one that needed a route: a universe arrives and
    /// one of the series it declares is already on the disk under another parent. It is a
    /// **move**, not a creation, and telling the two apart is only possible because a
    /// folder's identity now travels with it in its sidecar.
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub moves: Vec<Relocation>,
    /// What the import aims at, with the way down to it. Absent without a target.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub target: Option<TargetPlace>,
    /// What each file compared with the volumes of its edition is found to be, by the path
    /// it was announced under.
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub volumes: Vec<VolumeMatch>,
    /// What the target holds and the drop does not bring. It stays: nothing is removed by
    /// being left out. Only with a target.
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub absent: Vec<HeldVolume>,
    /// The series of the universe aimed at that the drop does not bring. Only with a
    /// universe target.
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub absent_series: Vec<AbsentSeries>,
    /// The series the drop would create that are named like one the library holds. Only
    /// without a target: a target already says which series the drop is.
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub resembles: Vec<Resemblance>,
    /// What replacing the chapter aimed at does to its volume: its pages before and after,
    /// each chapter's range now and then, the asking profile's place. Only with a chapter
    /// target.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub splice: Option<SpliceOutcome>,
    /// What this manifest would land on top of, one entry per path, with enough of what is
    /// already there to decide with.
    ///
    /// Named before anything moves, alongside `creates` and `already_there`: the third of
    /// what a preflight owes a reader — what will be created, what already exists, what
    /// would be replaced. Their paths are also in `to_send`, because installing the arriving
    /// bytes is still what a commit does with them; this list is what says that doing so
    /// overwrites something rather than adds it.
    ///
    /// **Nothing here is replaced unless the commit names it**, the same way nothing moves
    /// unless a commit names it. Six volumes that could be replaced are six decisions, and
    /// five of them may be exactly what their reader wants left alone.
    pub replaces: Vec<Replacement>,
    /// The declarations this manifest would rewrite, one entry per sidecar the library
    /// already holds whose content differs from the arriving one.
    ///
    /// A folder every volume of which the library already holds still carries its
    /// `work.json`, and installing it over a title or a summary edited through `PATCH` is a
    /// decision — not a side effect of dropping the folder a second time. Nothing here is
    /// written unless the commit names it.
    pub declarations: Vec<Declaration>,
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
    /// The works this commit filed under the folder it installed, by identity.
    ///
    /// Only the ones the request named. Moving what a reader did not ask to move is the one
    /// thing this route must never do on its own: it rearranges a library that was fine.
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub moved: Vec<String>,
    /// On the server, absent from your manifest. Never deleted automatically.
    pub orphans: Vec<String>,
    /// Arrived complete and cannot be installed as announced: the checksum does not match, a
    /// rewrite was asked of something that is not a readable archive, or the archive written
    /// is not made of the pages announced. Not installed, and
    /// the copy dropped so the session asks for it again: a volume that travelled wrong is
    /// worse than one that did not travel, because nothing afterwards would tell you.
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
    /// What this commit installed over a volume the library held, and where the old file
    /// waits. A replacement destroys nothing, but a person who finds a volume changed has
    /// to be able to find the one it was.
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub replaced: Vec<ReplacedFile>,
    /// The work a targeted reimport touched, for the rescan to be aimed at it rather than
    /// at a universe that cannot be aimed at. Not in the answer: it is the route's.
    #[serde(skip)]
    pub work_folder: Option<PathBuf>,
}

/// One volume a commit replaced.
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ReplacedFile {
    /// The path the announcement gave it.
    pub path: String,
    /// The entry it replaced the file of; absent for a file that is no volume — a cover,
    /// a page — set aside all the same.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub entry_id: Option<String>,
    /// Where the old file waits, relative to the inbox.
    pub kept_at: String,
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
    /// Read for one question only: does the library already hold the identity this folder
    /// declares? `would_create` reads the **disk** on purpose — what is there is what is
    /// there — but "is this work already somewhere else" is not a question a folder can
    /// answer about itself. See [`BulkImport::would_move`].
    db: std::sync::Arc<Db>,
    /// Where a commit stops on purpose; `None` but in a test. See [`Cut`].
    cut: Option<Cut>,
    /// Held by a commit and an abandon for all they do. Two sessions replacing one volume
    /// at once both linked the original aside and both renamed over it: the second put its
    /// file over the first's with no copy of it anywhere, and recorded the original's size
    /// as the one it replaced. One after the other, the second sets the first's file aside.
    commits: std::sync::Mutex<()>,
    /// Held by an opening from the count of the open imports to the manifest that makes the
    /// new one count: two openings at once both saw room for the last place and both took it.
    openings: std::sync::Mutex<()>,
}

/// A point where a test stops a commit, as a crash or a full disk would, to see what it
/// leaves. A replacement is four steps on two stores, the disk and the index, and no
/// transaction spans both: what each cut leaves behind is the whole of its safety, and
/// nothing short of stopping there shows it.
#[doc(hidden)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Cut {
    /// The old file linked aside, nothing yet written of the replacement.
    AfterLink,
    /// What the replacement will record written in the session.
    AfterSettling,
    /// Everything ready, the new file not yet renamed over the old.
    BeforeRename,
    /// The new file in place, nothing yet said of it in the index.
    AfterRename,
    /// Recorded and settled in the index, the session not yet told.
    AfterConsign,
}

impl BulkImport {
    pub fn new(inbox: &Path, library: &Path, db: std::sync::Arc<Db>) -> Self {
        BulkImport {
            inbox: inbox.to_path_buf(),
            library: library.to_path_buf(),
            db,
            cut: None,
            commits: std::sync::Mutex::new(()),
            openings: std::sync::Mutex::new(()),
        }
    }

    #[doc(hidden)]
    pub fn cut_at(mut self, cut: Cut) -> Self {
        self.cut = Some(cut);
        self
    }

    fn stop_at(&self, cut: Cut) -> Result<()> {
        if self.cut == Some(cut) {
            anyhow::bail!("stopped on purpose: {cut:?}");
        }
        Ok(())
    }

    /// The origins of every file of an announcement, in one query.
    fn origins_of<'a>(
        &self,
        root: &str,
        files: impl IntoIterator<Item = &'a ManifestFile>,
    ) -> Result<HashMap<String, Origin>> {
        let keys: Vec<String> = files
            .into_iter()
            .map(|f| origin_key(root, &f.path))
            .collect();
        self.origins_at(&keys)
    }

    fn origins_at(&self, keys: &[String]) -> Result<HashMap<String, Origin>> {
        // SQLite refuses more bound parameters than its limit (32 766 on current builds),
        // and an announcement within `MAX_ANNOUNCEMENT_BYTES` can name many times that.
        let mut found = HashMap::new();
        for chunk in keys.chunks(30_000) {
            found.extend(crate::store::origins::known(&self.db, chunk)?);
        }
        Ok(found)
    }

    /// Opens a session for `request`, read by `profile` under `ceiling`.
    ///
    /// With a target, the folder dropped is laid on the target's folder, and each archive
    /// lands on the volume of its number **under the name of that volume's file**. An entry
    /// is a file in an edition — its identity is the edition and the file name — so landing
    /// on the same name keeps the entry, and with it every profile's place in it, its read
    /// marks, the date it arrived and the count of volumes owned. Landing beside it under
    /// the arriving name made a second entry, and the rescan that found the old file gone
    /// pruned the first with all of that attached.
    pub fn open(
        &self,
        request: &ImportRequest,
        profile: &str,
        ceiling: &Ceiling,
    ) -> Result<ImportOpened> {
        let _one_at_a_time = self
            .openings
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        let id = format!("imp_{}", token());
        let session = self.inbox.join(&id);
        let opened = self.open_in(request, profile, ceiling, id, &session);
        if opened.is_err() {
            // What was made before the refusal is not an import: left, it sat in the inbox
            // for ever, and counted against the places there are.
            let _ = std::fs::remove_dir_all(&session);
        }
        opened
    }

    fn open_in(
        &self,
        request: &ImportRequest,
        profile: &str,
        ceiling: &Ceiling,
        id: String,
        session: &Path,
    ) -> Result<ImportOpened> {
        readable_declarations(&request.sidecars)?;
        announced_paths(request)?;
        readable_rewrites(&request.files)?;
        coherent_declarations(&request.sidecars)?;
        coherent_orders(&request.sidecars)?;
        // Counted as `waiting` lists them, a folder holding a manifest: every one counted is
        // one a person can see and abandon.
        let open_now = std::fs::read_dir(&self.inbox)
            .into_iter()
            .flatten()
            .flatten()
            .filter(|folder| {
                folder.file_name().to_string_lossy().starts_with("imp_")
                    && folder.path().join(MANIFEST).is_file()
            })
            .count();
        if open_now >= MAX_OPEN_IMPORTS {
            return Err(invalid(format!(
                "{open_now} imports are already open: finish or abandon some before opening another"
            )));
        }
        let landed = self.landed(request, profile, ceiling)?;
        let root = landed.root.clone();
        // Made now, empty or not: its absence is what tells a session of the earlier
        // layout, whose files sat at its root, from one where nothing has arrived yet.
        std::fs::create_dir_all(session.join(RECEIVED))?;

        let target = self.library.join(&root);
        let at_landing: Vec<Option<ManifestFile>> = request
            .files
            .iter()
            .zip(&landed.files)
            .map(|(f, at)| {
                at.as_ref().map(|at| ManifestFile {
                    path: at.clone(),
                    ..f.clone()
                })
            })
            .collect();
        // Home: identical to the volume it is, or already at its landing. Sent: the rest,
        // except what lands nowhere — those wait for a number, and sending them would be
        // asking for bytes nothing will install.
        // A file whose pages replace a chapter is never home: the volume it lands on is the
        // one it rewrites, and the same bytes there say nothing of the range asked for.
        let home: Vec<bool> = at_landing
            .iter()
            .zip(&landed.outcomes)
            .zip(&request.files)
            .map(|((at, outcome), f)| {
                f.splice.is_none()
                    && (*outcome == Some(Outcome::Same)
                        || (!stays_out(*outcome)
                            && at.as_ref().is_some_and(|at| {
                                already_home(
                                    &target,
                                    at,
                                    &origin_key(&root, &at.path),
                                    &landed.origins,
                                )
                            })))
            })
            .collect();
        let sent = |i: usize| !home[i] && !stays_out(landed.outcomes[i]);

        // Written where the files will be, so the commit's rename carries them home with
        // everything else. Without this a series would land without the declaration that
        // says what it is, and the scan behind it would classify a folder of archives as
        // nothing at all.
        for sidecar in &request.sidecars {
            let at = under(&session.join(RECEIVED), &sidecar.path)?;
            if let Some(folder) = at.parent() {
                std::fs::create_dir_all(folder)?;
            }
            std::fs::write(&at, sidecar.json.as_bytes())?;
        }
        // Every list below is worked out where things land and answered by the path the
        // client announced: it is the only one it knows them by.
        let landed_sidecars: Vec<ManifestSidecar> = request
            .sidecars
            .iter()
            .zip(&landed.sidecars)
            .map(|(s, at)| ManifestSidecar {
                path: at.clone(),
                json: s.json.clone(),
            })
            .collect();
        let sidecar_back: HashMap<&str, &str> = landed
            .sidecars
            .iter()
            .map(String::as_str)
            .zip(request.sidecars.iter().map(|s| s.path.as_str()))
            .collect();
        let file_back: HashMap<&str, &str> = landed
            .files
            .iter()
            .zip(&request.files)
            .filter_map(|(at, f)| at.as_deref().map(|at| (at, f.path.as_str())))
            .collect();
        let in_drop = |at: String| -> String {
            if landed.under.is_empty() {
                return at;
            }
            match Path::new(&at).strip_prefix(&landed.under) {
                Ok(rest) => rest.to_string_lossy().into_owned(),
                Err(_) => at,
            }
        };

        let known = &landed.known;
        let creates = self
            .would_create(&target, &landed_sidecars, known)
            .into_iter()
            .map(|c| Creation {
                at: in_drop(c.at),
                ..c
            })
            .collect();
        let moves = would_move(&target, known)
            .into_iter()
            .map(|m| Relocation {
                at: in_drop(m.at),
                ..m
            })
            .collect();
        // Another volume or another series lands on what it lands on only if asked: it is
        // said in `volumes`, and offering it here as one more replacement would offer to
        // overwrite volume 5 with a file brought for volume 2.
        let compared: Vec<ManifestFile> = at_landing
            .iter()
            .zip(&landed.outcomes)
            .filter(|(_, outcome)| {
                !stays_out(**outcome)
                    && !matches!(outcome, Some(Outcome::OtherNumber | Outcome::OtherSeries))
            })
            .filter_map(|(at, _)| at.clone())
            .collect();
        let replaces = would_replace(&target, &root, &compared, &landed.origins)
            .into_iter()
            .map(|r| Replacement {
                path: file_back
                    .get(r.path.as_str())
                    .map(|p| p.to_string())
                    .unwrap_or(r.path.clone()),
                ..r
            })
            .collect();
        let declarations = would_redeclare(&target, &landed_sidecars)
            .into_iter()
            .map(|d| Declaration {
                path: sidecar_back
                    .get(d.path.as_str())
                    .map(|p| p.to_string())
                    .unwrap_or(d.path.clone()),
                ..d
            })
            .collect();

        let stored = ImportRequest {
            root: root.clone(),
            files: request.files.clone(),
            scope: request.scope,
            sidecars: request.sidecars.clone(),
            target: request.target.clone(),
            current_edition_name: request.current_edition_name.clone(),
        };
        let landings = Landings {
            root: root.clone(),
            under: landed.under.clone(),
            files: request
                .files
                .iter()
                .zip(&landed.files)
                .filter_map(|(f, at)| at.clone().map(|at| (f.path.clone(), at)))
                .collect(),
            sidecars: request
                .sidecars
                .iter()
                .zip(&landed.sidecars)
                .map(|(s, at)| (s.path.clone(), at.clone()))
                .collect(),
            outcomes: request
                .files
                .iter()
                .zip(&landed.outcomes)
                .filter_map(|(f, o)| o.map(|o| (f.path.clone(), o)))
                .collect(),
            work_folder: landed.work_folder.clone(),
            cuts: landed.cuts.clone(),
            blocked: landed
                .problems
                .iter()
                .map(|p| format!("{}: {}", p.kind.to_lowercase(), p.path))
                .collect(),
        };
        std::fs::write(
            session.join(LANDINGS),
            serde_json::to_vec_pretty(&landings)?,
        )?;
        std::fs::write(session.join(MANIFEST), serde_json::to_vec_pretty(&stored)?)?;

        let to_send: Vec<String> = (0..request.files.len())
            .filter(|&i| sent(i))
            .map(|i| request.files[i].path.clone())
            .collect();
        tracing::info!(
            id,
            root,
            to_send = to_send.len(),
            of = request.files.len(),
            "import opened"
        );
        Ok(ImportOpened {
            lands_as: landed.lands_as,
            collisions: landed.collisions,
            reshelved: landed.reshelved,
            editions_needing_a_name: landed.needing,
            problems: landed.problems,
            creates,
            moves,
            replaces,
            declarations,
            bytes_to_send: (0..request.files.len())
                .filter(|&i| sent(i))
                .map(|i| request.files[i].size)
                .sum(),
            already_there: (0..request.files.len())
                .filter(|&i| home[i])
                .map(|i| request.files[i].path.clone())
                .collect(),
            to_send,
            target: landed.target,
            volumes: landed.volumes,
            absent: landed.absent,
            absent_series: landed.absent_series,
            resembles: landed.resembles,
            splice: landed.splice,
            id,
            root,
        })
    }

    /// Where every file of `request` lands and what it is to what the library holds, with
    /// every read of the index the announcement needs — a fixed number of them, whatever
    /// the number of files.
    fn landed(&self, request: &ImportRequest, profile: &str, ceiling: &Ceiling) -> Result<Landed> {
        let kind = request.target.as_ref().map(|t| t.kind);
        refuse_misshapen(request, kind)?;

        let place = self.target_place(request, ceiling)?;
        if let (Some(TargetKind::SeriesNewEditions), Some((_, place))) = (kind, &place) {
            self.refuse_one_shot(place)?;
        }
        let (root, under_root) = match &place {
            None => (plain_name(&request.root)?, String::new()),
            // New editions are laid beside the ones the series holds: on its work's folder.
            Some((target, place)) if target.kind == TargetKind::SeriesNewEditions => {
                self.root_of(place.work_folder.as_deref().unwrap_or(&place.folder))?
            }
            Some((_, place)) => self.root_of(&place.folder)?,
        };
        if let Some((target, place)) = &place {
            refuse_foreign_declarations(request, target, place)?;
        }
        // A file or declaration dropped loose is an edition of its own, in a folder named
        // as the edition is: it is never laid on the folder of the edition already there.
        let loose_edition = (kind == Some(TargetKind::SeriesNewEditions)).then(|| {
            request
                .sidecars
                .iter()
                .find(|s| s.path.eq_ignore_ascii_case("edition.json"))
                .and_then(|s| text_of(s, "name"))
                .map(|name| crate::store::naming::folder_name(&name))
                .unwrap_or_else(|| "Sans nom".to_string())
        });
        let joined = |path: &str| joined_path(loose_edition.as_deref(), &under_root, path);
        let base = self.library.join(&root);
        let paths = request.files.iter().map(|f| &f.path);
        for path in paths.chain(request.sidecars.iter().map(|s| &s.path)) {
            under(&base, &joined(path))?;
        }
        let files: Vec<Option<String>> = request
            .files
            .iter()
            .map(|f| Some(joined(&f.path)))
            .collect();
        let mut outcomes: Vec<Option<Outcome>> = vec![None; request.files.len()];
        let sidecars: Vec<String> = request.sidecars.iter().map(|s| joined(&s.path)).collect();

        // Every work, read once, and only when a folder of the drop could be one the library
        // holds elsewhere: a series of a universe, a series created without a target, or a
        // declared identity on a folder not there yet. Each of those used to be its own read
        // or none at all, and a drop of fifty series is fifty folders asking the same thing.
        let universe = place
            .as_ref()
            .filter(|(target, _)| target.kind == TargetKind::Universe)
            .map(|(_, place)| place);
        let fresh = if place.is_none() {
            new_series(&base, &root, request)
        } else {
            Vec::new()
        };
        let identified = |sidecars: &[String]| -> Vec<(String, String)> {
            request
                .sidecars
                .iter()
                .zip(sidecars)
                .filter(|(_, at)| declared_by(Path::new(at)) == Some("WORK"))
                .filter_map(|(s, at)| {
                    let folder = Path::new(at).parent().unwrap_or(Path::new(""));
                    let id = declared_identity(s)?;
                    (!base.join(folder).exists()).then(|| (folder.to_string_lossy().into(), id))
                })
                .collect()
        };
        let works: Vec<Titled> =
            if universe.is_some() || !fresh.is_empty() || !identified(&sidecars).is_empty() {
                held::works(&self.db, ceiling)?
            } else {
                Vec::new()
            };
        let mut laying = Laying {
            compared: vec![None; request.files.len()],
            files,
            sidecars,
            ..Laying::default()
        };

        // Which edition each file is compared with. A volume or a series target compares
        // every archive with the edition aimed at; a universe, each archive with the edition
        // of the work its folder is found to be; no target, each archive with the edition its
        // folder is, when it is one — the same matching, so a folder dropped onto a series it
        // holds replaces its volumes rather than doubling them. Without a target only a file
        // that says what it is takes part: one that carries nothing read lands at its path,
        // as an import always did.
        match &place {
            Some((target, place))
                if matches!(target.kind, TargetKind::Entry | TargetKind::Series) =>
            {
                laying.compare_with_target(request, place);
            }
            // The pages of a chapter come from the file sent, and land in the volume that
            // holds the chapter: the file is installed nowhere of its own, the volume is
            // rewritten with them in place of the chapter's.
            Some((target, place)) if target.kind == TargetKind::Chapter => {
                let volume = place
                    .entry_id
                    .as_deref()
                    .map(|entry| held::chaptered_volume(&self.db, entry, profile))
                    .transpose()?
                    .flatten()
                    .ok_or_else(|| absent(format!("unknown target: {}", target.id)))?;
                let inside = normalise(&volume.file)
                    .strip_prefix(normalise(&base))
                    .map(|p| p.to_string_lossy().into_owned())
                    .map_err(|_| invalid("the volume is not under its root"))?;
                under(&base, &inside)?;
                laying.splice_into(request, &target.id, &volume, &inside)?;
            }
            // Each folder of the drop is one series, matched with a work the way its name and
            // its `work.json` say; matched by folder name alone, « DBGT » holding « Dragon
            // Ball GT » was a second series beside the first.
            Some((target, place)) if target.kind == TargetKind::Universe => {
                laying.compare_in_universe(request, place, &base, &works, &joined)?;
            }
            _ => laying.compare_by_folder(&self.db, ceiling, request, &base)?,
        }
        let Laying {
            mut files,
            mut sidecars,
            compared,
            names_of,
            mut known,
            brought,
            cuts,
            splice,
        } = laying;

        // Two paths announced apart can land on one once a series is found to be another
        // folder: « DBGT/work.json » and « Dragon Ball GT/work.json » are both the work's
        // own, and installed in turn the second wrote over the first without a word.
        // Checked where they land, which is where the two meet.
        refuse_shared_landings(request, &files, &sidecars)?;

        // A work declared under an identity the library files elsewhere, on a folder not
        // there yet: a move, whatever the target.
        adopt_identities(identified(&sidecars), &works, &mut known);
        let resembles: Vec<Resemblance> = fresh
            .iter()
            .filter(|(folder, _)| !known.contains_key(folder))
            .flat_map(|(folder, title)| {
                let wanted = folded(title);
                works
                    .iter()
                    .filter(move |w| folded(w.title.as_deref().unwrap_or(&w.name)) == wanted)
                    .map(move |w| Resemblance {
                        at: folder.clone(),
                        series_id: w.series_id.clone(),
                        name: w.title.clone().unwrap_or_else(|| w.name.clone()),
                        from: w.path.to_string_lossy().into_owned(),
                    })
            })
            .collect();
        let absent_series: Vec<AbsentSeries> = universe
            .map(|place| {
                works
                    .iter()
                    .filter(|w| w.universe_id == place.universe_id)
                    .filter(|w| !brought.contains(&w.work_id))
                    .map(|w| AbsentSeries {
                        work_id: w.work_id.clone(),
                        series_id: w.series_id.clone(),
                        name: w.title.clone().unwrap_or_else(|| w.name.clone()),
                        volumes: w.volumes,
                    })
                    .collect()
            })
            .unwrap_or_default();

        let mut editions: Vec<String> = compared
            .iter()
            .flatten()
            .map(|(edition, _)| edition.clone())
            .collect();
        if let Some((target, place)) = &place {
            // A series is compared with what it holds even when nothing of it arrives:
            // what is absent is the answer then.
            if target.kind == TargetKind::Series {
                editions.extend(place.editions.iter().cloned());
            }
        }
        editions.sort();
        editions.dedup();
        let held = if editions.is_empty() {
            Vec::new()
        } else {
            held::held_in(&self.db, &editions, profile)?
        };

        // The origins of what is held, to know a converted volume when its original comes
        // back, and of every landing as announced — read together, in one query.
        let mut keys: Vec<String> = held
            .iter()
            .filter_map(|h| origin_key_of(&self.library, &h.file))
            .collect();
        keys.extend(files.iter().flatten().map(|at| origin_key(&root, at)));
        let origins = self.origins_at(&keys)?;

        let aimed = place
            .as_ref()
            .and_then(|(target, place)| match target.kind {
                TargetKind::Entry => place.entry_id.clone(),
                _ => None,
            });
        let mut volumes: Vec<(usize, VolumeMatch)> = Vec::new();
        for edition in &editions {
            let members: Vec<usize> = (0..request.files.len())
                .filter(|&i| compared[i].as_ref().is_some_and(|(e, _)| e == edition))
                .collect();
            let Some(folder) = members
                .first()
                .and_then(|&i| compared[i].as_ref().map(|(_, folder)| folder.clone()))
            else {
                continue;
            };
            let holding: Vec<Held> = held
                .iter()
                .filter(|h| &h.edition_id == edition)
                .cloned()
                .collect();
            let identical: Vec<Option<String>> = members
                .iter()
                .map(|&i| {
                    let landing = files[i].as_ref().map(|at| normalise(&base.join(at)));
                    identical_to(
                        &request.files[i],
                        landing.as_deref(),
                        &holding,
                        &self.library,
                        &origins,
                    )
                })
                .collect();
            let incoming: Vec<Incoming> = members
                .iter()
                .zip(&identical)
                .map(|(&i, same)| Incoming {
                    path: &request.files[i].path,
                    found: request.files[i].found.as_ref(),
                    identical_to: same.as_deref(),
                })
                .collect();
            let names: Vec<&str> = names_of
                .get(edition)
                .map(|names| names.iter().map(String::as_str).collect())
                .unwrap_or_default();
            let answers = match_volumes(&holding, &incoming, &folder, aimed.as_deref(), &names)?;
            let inside = normalise(&folder)
                .strip_prefix(normalise(&base))
                .map(Path::to_path_buf)
                .map_err(|_| invalid(format!("{} is not under {root}", folder.display())))?;
            for (&i, answer) in members.iter().zip(answers) {
                files[i] = answer
                    .lands
                    .as_ref()
                    .map(|lands| inside.join(lands).to_string_lossy().into_owned());
                outcomes[i] = Some(answer.outcome);
                volumes.push((i, answer));
            }
        }
        volumes.sort_by_key(|(i, _)| *i);
        let volumes: Vec<VolumeMatch> = volumes.into_iter().map(|(_, v)| v).collect();

        // Each compared edition's folder, which a held file is named relative to.
        let mut folders: HashMap<&str, &Path> = compared
            .iter()
            .flatten()
            .map(|(edition, folder)| (edition.as_str(), folder.as_path()))
            .collect();
        if let Some((_, place)) = &place {
            if let [edition] = place.editions.as_slice() {
                folders
                    .entry(edition.as_str())
                    .or_insert(place.folder.as_path());
            }
        }
        let touched: HashSet<&str> = volumes
            .iter()
            .filter_map(|v| v.entry_id.as_deref())
            .collect();
        let absent: Vec<HeldVolume> = match &place {
            None => Vec::new(),
            Some((target, place)) => absent_from(&held, &touched, target.kind, place, &folders),
        };

        let target = place.as_ref().map(|(target, place)| TargetPlace {
            kind: target.kind,
            id: target.id.clone(),
            name: place
                .trail
                .last()
                .map(|step| step.name.clone())
                .unwrap_or_default(),
            trail: place
                .trail
                .iter()
                .map(|step| TrailStep {
                    kind: step.kind.to_string(),
                    id: step.id.clone(),
                    name: step.name.clone(),
                    count: step.count,
                })
                .collect(),
        });
        let work_folder = place.as_ref().and_then(|(_, place)| {
            place
                .work_folder
                .as_ref()
                .and_then(|w| origin_key_of(&self.library, w))
        });
        // Names given last, to what is new: every key above was worked out from the paths as
        // announced, and the folders renamed here are renamed in them too.
        let series_known = match &place {
            Some((target, place)) if place.work_id.is_some() => self.known_series(target, place)?,
            _ => None,
        };
        let named = super::canonical::canonicalise(
            request,
            &root,
            &base,
            series_known.as_ref(),
            &mut files,
            &mut sidecars,
            &mut outcomes,
        );
        let follow = |at: &str| followed(&named.moved, at);
        // What the answer says of a volume is where it lands: the name given just above.
        let mut volumes = volumes;
        land_as_named(&mut volumes, request, &files);
        let known: HashMap<String, Known> =
            known.into_iter().map(|(at, k)| (follow(&at), k)).collect();
        let resembles: Vec<Resemblance> = resembles
            .into_iter()
            .map(|r| Resemblance {
                at: follow(&r.at),
                ..r
            })
            .collect();
        let lands_as: Vec<FiledAs> = request
            .files
            .iter()
            .zip(&files)
            .filter_map(|(f, at)| {
                let at = at.as_deref()?;
                let (folder, file) = at.rsplit_once('/').unwrap_or(("", at));
                let (_, was_called) = f.path.rsplit_once('/').unwrap_or(("", &f.path));
                let readable = std::iter::once(root.as_str())
                    .chain(folder.split('/').filter(|part| !part.is_empty()))
                    .collect::<Vec<_>>()
                    .join(" › ");
                Some(FiledAs {
                    path: f.path.clone(),
                    folder: readable,
                    file: file.to_string(),
                    was_called: was_called.to_string(),
                })
            })
            .collect();
        let collisions: Vec<Collided> = named
            .collisions
            .iter()
            .map(|&(i, j)| Collided {
                path: request.files[i].path.clone(),
                with: request.files[j].path.clone(),
            })
            .collect();
        let (needing, problems) = match &place {
            Some((target, place)) if target.kind == TargetKind::SeriesNewEditions => {
                self.new_edition_names(request, place)?
            }
            _ => (Vec::new(), Vec::new()),
        };
        let reshelved = match (&place, &series_known) {
            (Some((target, _)), Some(known))
                if target.kind == TargetKind::SeriesNewEditions && known.editions == 1 =>
            {
                self.reshelved_by(request, target, known)?
            }
            _ => Vec::new(),
        };
        Ok(Landed {
            needing,
            problems,
            lands_as,
            collisions,
            reshelved,
            root,
            under: under_root,
            files,
            outcomes,
            sidecars,
            origins,
            volumes,
            absent,
            target,
            work_folder,
            known,
            absent_series,
            resembles,
            splice,
            cuts,
        })
    }

    /// The place the request's target names, with the target; none without a target.
    fn target_place<'r>(
        &self,
        request: &'r ImportRequest,
        ceiling: &Ceiling,
    ) -> Result<Option<(&'r ImportTarget, Place)>> {
        let Some(target) = &request.target else {
            return Ok(None);
        };
        let aim = match target.kind {
            TargetKind::Entry => Aim::Entry(target.id.clone()),
            TargetKind::Series | TargetKind::SeriesNewEditions => Aim::Series(target.id.clone()),
            TargetKind::Universe => Aim::Universe(target.id.clone()),
            TargetKind::Chapter => Aim::Chapter(target.id.clone()),
        };
        // Unknown and hidden alike: a series a child may not see is not there for
        // that child, as `/series/{id}` answers.
        let place = held::place_of(&self.db, &aim, ceiling)?
            .ok_or_else(|| absent(format!("unknown target: {}", target.id)))?;
        Ok(Some((target, place)))
    }

    /// Refuses new editions for a series that is a one-shot.
    fn refuse_one_shot(&self, place: &Place) -> Result<()> {
        let work = place.work_id.clone().unwrap_or_default();
        let one_shots = self
            .db
            .read(|cx| {
                cx.query_one(
                    "SELECT COUNT(*) FROM edition WHERE work_id = ?1 AND one_shot = 1",
                    [work.as_str()],
                    |r| r.get::<_, i64>(0),
                )
            })?
            .unwrap_or(0);
        if one_shots > 0 {
            return Err(invalid("a one-shot is not given editions"));
        }
        Ok(())
    }

    /// What the index says of the series a drop is aimed at, which its volumes are named
    /// from: the title the rename engine uses, the universe, the edition aimed at and how
    /// many the series has. `None` for a target that is not a series.
    fn known_series(
        &self,
        target: &ImportTarget,
        place: &Place,
    ) -> Result<Option<super::canonical::Known>> {
        if !matches!(
            target.kind,
            TargetKind::Series | TargetKind::SeriesNewEditions
        ) {
            return Ok(None);
        }
        let work = place.work_id.clone().unwrap_or_default();
        let aimed = target.id.clone();
        // One statement for all of it: it is asked once per announcement, whatever the drop.
        self.db.read(|cx| {
            cx.query_one(
                "SELECT COALESCE(NULLIF(TRIM(w.title), ''), w.name), u.name,
                        (SELECT COUNT(*) FROM edition WHERE work_id = w.id),
                        (SELECT COUNT(*) FROM edition WHERE work_id = w.id AND implicit = 1),
                        (SELECT name FROM edition WHERE id = ?2)
                 FROM work w LEFT JOIN universe u ON u.id = w.universe_id WHERE w.id = ?1",
                [work.as_str(), aimed.as_str()],
                |r| {
                    let editions: i64 = r.get(2)?;
                    Ok(super::canonical::Known {
                        work: r.get(0)?,
                        universe: r.get(1)?,
                        edition: if editions > 1 { r.get(4)? } else { None },
                        editions,
                        held_implicit: r.get(3)?,
                    })
                },
            )
        })
    }

    /// The volumes a series with one edition has, which adding a second moves into a folder
    /// of their own under the names that say the edition. Said only once the edition they
    /// are in has a name, since without one there is nothing to say they go into.
    fn reshelved_by(
        &self,
        request: &ImportRequest,
        target: &ImportTarget,
        known: &super::canonical::Known,
    ) -> Result<Vec<Reshelved>> {
        let aimed = target.id.clone();
        let (held_name, count) = self
            .db
            .read(|cx| {
                cx.query_one(
                    "SELECT name, (SELECT COUNT(*) FROM entry WHERE edition_id = ?1)
                     FROM edition WHERE id = ?1",
                    [aimed.as_str()],
                    |r| Ok((r.get::<_, Option<String>>(0)?, r.get::<_, i64>(1)?)),
                )
            })?
            .unwrap_or((None, 0));
        let name = request
            .current_edition_name
            .as_deref()
            .map(str::trim)
            .filter(|n| !n.is_empty())
            .map(str::to_string)
            .or(held_name.filter(|n| !n.trim().is_empty()));
        Ok(match name {
            Some(name) if count > 0 => vec![Reshelved {
                edition: format!("{} › {}", known.work, name),
                count: count as u32,
            }],
            _ => Vec::new(),
        })
    }

    /// The editions a drop adds to a series, which of them have no name, and what is wrong
    /// with the names. Compared as a search is, without case or accents.
    fn new_edition_names(
        &self,
        request: &ImportRequest,
        place: &Place,
    ) -> Result<(Vec<NeedsAName>, Vec<NameProblem>)> {
        let work = place.work_id.clone().unwrap_or_default();
        let held: Vec<(Option<String>, bool)> = self.db.read(|cx| {
            cx.query(
                "SELECT name, implicit FROM edition WHERE work_id = ?1",
                [work.as_str()],
                |r| Ok((r.get::<_, Option<String>>(0)?, r.get::<_, i64>(1)? == 1)),
            )
        })?;
        let mut needing = Vec::new();
        let mut given: Vec<(String, String)> = Vec::new();
        // The edition the series holds now, when it has no name of its own to keep.
        let current = request
            .current_edition_name
            .as_deref()
            .map(str::trim)
            .filter(|n| !n.is_empty());
        let mut known: Vec<String> = held.iter().filter_map(|(n, _)| n.clone()).collect();
        if held.iter().any(|(_, implicit)| *implicit) {
            match current {
                Some(name) => known.push(name.to_string()),
                None => needing.push(NeedsAName {
                    path: "@current".into(),
                    current: true,
                }),
            }
        }
        let mut folders = first_folders(request);
        if request.files.iter().any(|f| !f.path.contains('/')) {
            folders.insert(0, String::new());
        }
        for folder in folders {
            let sidecar = if folder.is_empty() {
                "edition.json".to_string()
            } else {
                format!("{folder}/edition.json")
            };
            let name = request
                .sidecars
                .iter()
                .find(|s| s.path.eq_ignore_ascii_case(&sidecar))
                .and_then(|s| text_of(s, "name"))
                .filter(|n| !n.trim().is_empty());
            match name {
                Some(name) => given.push((folder, name)),
                None => needing.push(NeedsAName {
                    path: folder,
                    current: false,
                }),
            }
        }
        let mut problems: Vec<NameProblem> = needing
            .iter()
            .map(|n| NameProblem {
                path: n.path.clone(),
                kind: "EMPTY",
                with: None,
            })
            .collect();
        problems.extend(edition_name_problems(&known, &given));
        // A folder that is on the disk already is not a new edition's, whatever the names
        // say: its volumes would be poured into what it holds. Compared as a search is.
        let (root, under) = self.root_of(place.work_folder.as_deref().unwrap_or(&place.folder))?;
        let beside = self.library.join(root).join(under);
        let there: Vec<String> = std::fs::read_dir(&beside)
            .into_iter()
            .flatten()
            .flatten()
            .filter(|e| e.path().is_dir())
            .map(|e| e.file_name().to_string_lossy().into_owned())
            .collect();
        for (path, name) in &given {
            let wanted = crate::store::naming::folder_name(name);
            let key = crate::store::text::search_key(&wanted);
            if problems.iter().any(|p| &p.path == path) {
                continue;
            }
            if there
                .iter()
                .any(|d| crate::store::text::search_key(d) == key)
            {
                problems.push(NameProblem {
                    path: path.clone(),
                    kind: "DUPLICATE",
                    with: Some(wanted),
                });
            }
        }
        Ok((needing, problems))
    }

    /// The library folder a target sits in, and the way from it down to the target.
    fn root_of(&self, folder: &Path) -> Result<(String, String)> {
        let inside = normalise(folder)
            .strip_prefix(normalise(&self.library))
            .map(Path::to_path_buf)
            .map_err(|_| invalid("the target is not in this library"))?;
        let mut parts = inside
            .components()
            .map(|c| c.as_os_str().to_string_lossy().into_owned());
        let root = parts
            .next()
            .ok_or_else(|| invalid("the target is the library itself"))?;
        Ok((root, parts.collect::<Vec<_>>().join("/")))
    }

    /// What the library does not hold yet, from the declarations alone.
    ///
    /// Read off the **disk** and not the index, deliberately: a folder that exists is what
    /// the scanner derives a universe from, and asking the index instead would answer "no"
    /// for a folder put there by hand and not yet scanned — then create a second one beside
    /// it.
    fn would_create(
        &self,
        target: &Path,
        sidecars: &[ManifestSidecar],
        known: &HashMap<String, Known>,
    ) -> Vec<Creation> {
        let mut out = Vec::new();
        for sidecar in sidecars {
            let at = Path::new(&sidecar.path);
            let Some(kind) = declared_by(at) else {
                continue;
            };
            let folder = at.parent().unwrap_or(Path::new(""));
            if target.join(folder).exists() {
                continue;
            }
            // A work the library already holds elsewhere is a move, not a creation — saying
            // both would have the dialog announce the same folder twice, under two verbs.
            if kind == "WORK" && known.contains_key(folder.to_string_lossy().as_ref()) {
                continue;
            }
            let named: Option<String> = serde_json::from_str::<serde_json::Value>(&sidecar.json)
                .ok()
                .and_then(|v| {
                    // `name` for a universe and an edition, `title` for a work — the format's
                    // own words, and neither is a fallback for the other.
                    v.get("name")
                        .or_else(|| v.get("title"))
                        .and_then(|n| n.as_str().map(str::to_string))
                });
            out.push(Creation {
                kind: kind.to_string(),
                name: named.unwrap_or_else(|| {
                    folder
                        .file_name()
                        .and_then(OsStr::to_str)
                        .unwrap_or_default()
                        .to_string()
                }),
                at: folder.to_string_lossy().to_string(),
            });
        }
        // Down the model, not across the disk. The walk meets sidecars in whatever order the
        // filesystem lists them — measured on a real folder, that read « édition, série,
        // édition, univers », which is the model's four levels shuffled. Depth first, then
        // the path, so a reader gets universe, work, its editions, in that order and once.
        out.sort_by(|a, b| {
            depth_of(&a.at)
                .cmp(&depth_of(&b.at))
                .then_with(|| a.at.cmp(&b.at))
        });
        out
    }

    /// Where this import's manifest puts a work it did not bring the files of.
    ///
    /// The whole destination, name included: a folder can be declared under a name it does
    /// not currently have, and a move that kept the old one would file it somewhere nobody
    /// named. `None` when this import declares nothing under that identity.
    pub fn place_for(&self, id: &str, work_id: &str) -> Result<Option<PathBuf>> {
        let Some(session) = self.session(id)? else {
            return Ok(None);
        };
        let Some(manifest) = self.manifest(&session)? else {
            return Ok(None);
        };
        let landings = self.landings(&session, &manifest)?;
        let target = self.library.join(&landings.root);
        for sidecar in &manifest.sidecars {
            let at = under(&target, &landings.sidecar(&sidecar.path))?;
            if declared_by(&at) != Some("WORK") {
                continue;
            }
            let carried = serde_json::from_str::<serde_json::Value>(&sidecar.json)
                .ok()
                .and_then(|v| {
                    v.get(crate::scan::identity::FIELD)
                        .and_then(|i| i.as_str().map(str::to_string))
                });
            if carried.as_deref() == Some(work_id) {
                return Ok(Some(at.parent().unwrap_or(&target).to_path_buf()));
            }
        }
        Ok(None)
    }

    pub fn state(&self, id: &str) -> Result<Option<ImportState>> {
        let Some(session) = self.session(id)? else {
            return Ok(None);
        };
        let Some(manifest) = self.manifest(&session)? else {
            return Ok(None);
        };
        let landings = self.landings(&session, &manifest)?;
        let target = self.library.join(&landings.root);
        let origins = self.origins_landed(&manifest, &landings)?;

        let mut received = std::collections::BTreeMap::new();
        let mut missing = Vec::new();
        for f in &manifest.files {
            match landings.outcome(&f.path) {
                Some(Outcome::Same) => {
                    received.insert(f.path.clone(), f.size);
                    continue;
                }
                // Nothing installs it, so nothing is asked for.
                outcome if stays_out(outcome) => continue,
                _ => {}
            }
            // Already in the library counts as held. Without this, a commit that installed
            // half a manifest and kept the session for the rest reported the installed half
            // as missing, and the client sent it all over again.
            if let Some(at) = landings.at_landing(f) {
                if already_home(
                    &target,
                    &at,
                    &origin_key(&landings.root, &at.path),
                    &origins,
                ) {
                    received.insert(f.path.clone(), f.size);
                    continue;
                }
            }
            let held = under(&session.join(RECEIVED), &f.path)
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
            root: landings.root,
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
        let manifest = self
            .manifest(&session)
            .map_err(ReceiveError::Other)?
            .ok_or_else(|| ReceiveError::Unknown(id.to_string()))?;
        // Only what the announcement listed is received. Anything else is a file nobody
        // will install, at best, and at worst one of the session's own.
        if !manifest.files.iter().any(|f| f.path == path) {
            return Err(ReceiveError::Other(invalid(format!(
                "{path} is a path the announcement did not list"
            ))));
        }
        let target = under(&session.join(RECEIVED), path).map_err(ReceiveError::Other)?;
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

    /// What an import that adds editions to a series says of the series' current edition:
    /// the series, the name its current edition takes, and the name of the first new one.
    /// `None` for any other import.
    pub fn editions_added(&self, id: &str) -> Result<Option<EditionsAdded>> {
        let Some(session) = self.session(id)? else {
            return Ok(None);
        };
        let Some(manifest) = self.manifest(&session)? else {
            return Ok(None);
        };
        let Some(target) = manifest
            .target
            .as_ref()
            .filter(|t| t.kind == TargetKind::SeriesNewEditions)
        else {
            return Ok(None);
        };
        // Before anyone moves the current edition: a commit refused for a missing or
        // taken name would otherwise leave that edition renamed for nothing.
        let landings = self.landings(&session, &manifest)?;
        if !landings.blocked.is_empty() {
            return Err(invalid(format!(
                "an edition has no name or one already taken, so nothing was installed: {}",
                landings.blocked.join(", ")
            )));
        }
        let first_new = first_folders(&manifest)
            .into_iter()
            .chain(
                manifest
                    .files
                    .iter()
                    .any(|f| !f.path.contains('/'))
                    .then(String::new),
            )
            .find_map(|folder| {
                let at = if folder.is_empty() {
                    "edition.json".to_string()
                } else {
                    format!("{folder}/edition.json")
                };
                manifest
                    .sidecars
                    .iter()
                    .find(|s| s.path.eq_ignore_ascii_case(&at))
                    .and_then(|s| text_of(s, "name"))
            })
            .unwrap_or_default();
        Ok(Some(EditionsAdded {
            series: target.id.clone(),
            current_name: manifest.current_edition_name.clone(),
            first_new,
        }))
    }

    /// The folder imports land in, which is what an origin key is relative to.
    pub fn library(&self) -> &Path {
        &self.library
    }

    /// Where a manifest root lands, resolved exactly as [`BulkImport::commit`] resolves it.
    pub fn target_of(&self, root: &str) -> PathBuf {
        self.library.join(root)
    }

    /// Installs what arrived, and lands on an existing file only where `replacing` names it.
    ///
    /// `replacing` and `declaring` are to `replaces` and `declarations` what a commit's
    /// `move` list is to `moves`: the decisions
    /// a reader made in front of the announcement. A file that arrived whole, would land on
    /// something, and is not named here is left in the inbox and reported in `pending` — the
    /// same as one that never finished arriving. Refused is not the same as missing, but
    /// both are things still sitting there, and neither is silently skipped.
    pub fn commit(
        &self,
        id: &str,
        replacing: &[String],
        declaring: &[String],
        resetting: &[String],
        profile: &str,
    ) -> Result<ImportResult> {
        self.commit_with(id, replacing, declaring, resetting, profile, &mut || Ok(()))
    }

    /// [`BulkImport::commit`], with something to do once the commit knows it is about to
    /// install a first file and not before: under the same lock, after every check that
    /// could refuse it, and once per import — a commit resumed after a cut finds the mark
    /// the first one left and does not do it again. The move of a series' current edition
    /// into its own folder is that something: done earlier, a commit refused afterwards
    /// left a series renamed for an import that never happened.
    pub fn commit_with(
        &self,
        id: &str,
        replacing: &[String],
        declaring: &[String],
        resetting: &[String],
        profile: &str,
        before_first: &mut dyn FnMut() -> Result<()>,
    ) -> Result<ImportResult> {
        let _one_at_a_time = self
            .commits
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        let session = self
            .session(id)?
            .ok_or_else(|| absent(format!("unknown import: {id}")))?;
        let manifest = self
            .manifest(&session)?
            .ok_or_else(|| absent(format!("unknown import: {id}")))?;
        let landings = self.landings(&session, &manifest)?;
        if !landings.blocked.is_empty() {
            return Err(invalid(format!(
                "an edition has no name or one already taken, so nothing was installed: {}",
                landings.blocked.join(", ")
            )));
        }
        let target = self.library.join(&landings.root);
        // Read once, like `open` does, rather than once per file: a converted volume met
        // again is done, and that is only known from here.
        let origins = self.origins_landed(&manifest, &landings)?;

        let mut installed = 0usize;
        let mut began = false;
        let mut pending = Vec::new();
        let mut corrupt = Vec::new();
        // What a commit cut short had put in place and not yet recorded: this one says it.
        let (mut replaced, finished, conflicted) = split_settled(self.reconcile(id, &session)?);
        for f in &manifest.files {
            // Done by a commit cut short and finished above: whatever is at the volume now
            // came after, and is not this session's to replace again.
            if finished.contains(&f.path) {
                remove_received(&session, &f.path);
                continue;
            }
            // Another file took the volume between a cut and now: it is not this session's
            // to replace, and what it set aside stays.
            if conflicted.contains(&f.path) {
                pending.push(f.path.clone());
                continue;
            }
            let outcome = landings.outcome(&f.path);
            // Identical to what is there: nothing is installed, renamed or set aside for
            // it, whatever its landing says.
            if outcome == Some(Outcome::Same) {
                continue;
            }
            // Waiting for a number, or for the volume it is to be said: it stays.
            let Some(at) = landings.at_landing(f).filter(|_| !stays_out(outcome)) else {
                pending.push(f.path.clone());
                continue;
            };
            // A commit that installed part of a manifest keeps the session for the rest, so
            // a second one meets files that are already home. They are done, not missing.
            let key = origin_key(&landings.root, &at.path);
            // Never for a splice: the volume it lands on is the one it rewrites.
            if f.splice.is_none() && already_home(&target, &at, &key, &origins) {
                continue;
            }
            let source = under(&session.join(RECEIVED), &f.path)?;
            if let Err(aside) = received_whole(f, &source) {
                aside.put(&f.path, &mut pending, &mut corrupt);
                continue;
            }
            let destination = under(&target, &at.path)?;
            if waits_for_a_word(&destination, outcome, replacing, &f.path) {
                pending.push(f.path.clone());
                continue;
            }
            // Checked here, with the file arrived, and refused before anything is touched: a
            // range the announcement could only take the client's word for.
            let spliced = match self.spliced(f, &landings, &source, &destination)? {
                Ok(spliced) => spliced,
                Err(aside) => {
                    aside.put(&f.path, &mut pending, &mut corrupt);
                    continue;
                }
            };
            if !began {
                self.begin(&session, &manifest, &landings, &target, before_first)?;
                began = true;
            }
            parent_created(&destination)?;
            let landing = Landing {
                id,
                session: &session,
                root: &landings.root,
                at: &at.path,
                reset: resetting
                    .iter()
                    .any(|named| named == &f.path)
                    .then_some(profile),
                profile,
                splice: spliced.as_ref(),
            };
            match self.install(f, &source, &destination, &landing, key)? {
                Installed::Added => {}
                Installed::Replaced { entry_id, kept_at } => replaced.push(ReplacedFile {
                    path: f.path.clone(),
                    entry_id,
                    kept_at,
                }),
                Installed::Unreadable => {
                    corrupt.push(f.path.clone());
                    continue;
                }
                // Its name was free at the announcement and is not any more.
                Installed::Taken | Installed::Unheld | Installed::Conflict => {
                    pending.push(f.path.clone());
                    continue;
                }
            }
            installed += 1;
        }

        // The declarations, which are not in `files` and never were: they arrived in the
        // announcement, a few hundred bytes each, and they are what says what all this is.
        // Installed after the volumes and not counted among them — a folder is not "one
        // more file imported" because it finally said its own name.
        install_sidecars(&manifest, &landings, &session, &target, declaring)?;

        let expected: HashSet<&str> = landings
            .files
            .values()
            .chain(landings.sidecars.values())
            .map(String::as_str)
            .collect();
        // Under the folder the drop was laid on, not the whole of the root: an edition's
        // siblings are not what the drop claims to cover.
        let orphans = orphans_of(&manifest, &target.join(&landings.under), &target, &expected);

        let work_folder = landings
            .work_folder
            .as_deref()
            .map(|w| under(&self.library, w))
            .transpose()?;
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
            root: landings.root,
            installed,
            // Filled by the route, which is where a move happens: the folder has to be in
            // the index before anything is filed under it, and the index only learns of it
            // from the rescan that follows this.
            moved: Vec::new(),
            orphans,
            corrupt,
            pending,
            open,
            replaced,
            work_folder,
        })
    }

    /// The range of pages a file replaces a chapter with, checked against the volume it
    /// lands on; none for a file that is no splice.
    fn spliced(
        &self,
        f: &ManifestFile,
        landings: &Landings,
        source: &Path,
        destination: &Path,
    ) -> Result<std::result::Result<Option<SpliceAt>, Aside>> {
        let Some(take) = f.splice else {
            return Ok(Ok(None));
        };
        let cut =
            landings.cuts.get(&f.path).copied().ok_or_else(|| {
                invalid(format!("{}: no chapter was found for its pages", f.path))
            })?;
        if !destination.is_file() {
            return Ok(Err(Aside::Pending));
        }
        Ok(self
            .splice_at(source, destination, cut, take)?
            .map(Some)
            .ok_or(Aside::Corrupt))
    }

    /// What is done once, before the first file of an import is installed: the mark
    /// says a commit resumed after a cut has done it already.
    fn begin(
        &self,
        session: &Path,
        manifest: &ImportRequest,
        landings: &Landings,
        target: &Path,
        before_first: &mut dyn FnMut() -> Result<()>,
    ) -> Result<()> {
        let mark = session.join(BEGAN);
        if !mark.exists() {
            self.edition_folders_are_new(manifest, landings, target)?;
            before_first()?;
            std::fs::write(&mark, b"")?;
        }
        Ok(())
    }

    /// Refuses a commit that would pour new editions into a folder that is there already:
    /// another import, or a person, made it since the announcement said it was free.
    fn edition_folders_are_new(
        &self,
        manifest: &ImportRequest,
        landings: &Landings,
        target: &Path,
    ) -> Result<()> {
        if manifest.target.as_ref().map(|t| t.kind) != Some(TargetKind::SeriesNewEditions) {
            return Ok(());
        }
        let beside = target.join(&landings.under);
        for landed in landings.files.values() {
            let below = landed
                .strip_prefix(&landings.under)
                .map(|rest| rest.trim_start_matches('/'))
                .unwrap_or(landed);
            let Some((folder, _)) = below.split_once('/') else {
                continue;
            };
            if beside.join(folder).exists() {
                return Err(invalid(format!(
                    "the folder of an edition already exists, so nothing was installed: {folder}"
                )));
            }
        }
        Ok(())
    }

    /// Installs one file at its landing, in an order where nothing is lost and nothing
    /// erased whatever step a crash or a full disk stops it at.
    ///
    /// **Replacing in place is what keeps a reader's place.** The entry is the edition and
    /// the file name, so the new file takes the old one's name; a rescan that met the
    /// edition without its file in between would prune the entry, and `ON DELETE CASCADE`
    /// would take every profile's progress with it (`scan::identity`). So the old file is
    /// never removed and the new one never written over it — a rename swaps them:
    ///
    /// 1. the version to install is prepared **beside** the destination, rewritten or
    ///    linked from the inbox, which is never written to, and synced. Copied over the old
    ///    one in place, as it once was when a rename failed, a volume cut halfway was a
    ///    volume lost;
    /// 2. it is read — an archive that does not open is refused before anything else is
    ///    touched — the old file is **linked** under `replaced/<id>/`, the same bytes under
    ///    a second name, and what the replacement will record is written in the session
    ///    with the identity of both files. A link already there from a commit cut short is
    ///    used only if it is still the file in place;
    /// 3. the rename, and the folder synced;
    /// 4. the replacement recorded and the places in it settled, in one transaction, once:
    ///    replayed after a cut, it moves no place a reader has moved since.
    ///
    /// **The places move only once the new file is in place.** Settled before the rename,
    /// a commit cut between the two and then abandoned left the old volume where it was,
    /// a record saying it was replaced, and every reader's place reset or clamped to a
    /// file that never arrived. Cut before the rename now, nothing has moved and the
    /// abandon removes the link; cut after it, the session still says what to record, and
    /// the next commit or the abandon records it (see [`BulkImport::reconcile`]).
    ///
    /// An addition is 1 and 3 only, and 3 then refuses to write over a file it finds:
    /// the name was free at the announcement, and another import or a person in a file
    /// manager may have taken it since.
    fn install(
        &self,
        f: &ManifestFile,
        source: &Path,
        destination: &Path,
        landing: &Landing<'_>,
        key: String,
    ) -> Result<Installed> {
        let staged = staged_for(destination, landing.id);
        // A leftover of this name is this session's own, from a commit cut short.
        let _ = std::fs::remove_file(&staged);
        if let Some(refused) = self.prepare(f, source, &staged, destination, landing.splice)? {
            return Ok(refused);
        }
        if let Some(wrong) = members_differ(f, source, &staged, destination, landing.splice)? {
            // Dropped like a file whose checksum is wrong, and for the same reason: what was
            // about to be installed is not what was sent, and nothing afterwards would say so.
            tracing::warn!(file = %f.path, "{wrong}");
            let _ = std::fs::remove_file(&staged);
            let _ = std::fs::remove_file(source);
            return Ok(Installed::Unreadable);
        }
        let placed = (|| -> Result<Installed> {
            // The received bytes, or the rewrite made of them, on the disk before they are
            // given the volume's name.
            std::fs::File::open(&staged)?.sync_data()?;
            let settling = if destination.is_file() {
                match self.set_aside(f, destination, &staged, landing)? {
                    Ok(settling) => settling,
                    Err(refused) => return Ok(refused),
                }
            } else {
                None
            };
            // Recorded before the move: recording twice is harmless, a volume home with no
            // origin is asked for at every import.
            if let Some(change) = &f.rewrite {
                let origin = Origin {
                    path: key,
                    kept_size: std::fs::metadata(&staged)?.len(),
                    origin_size: f.size,
                    origin_checksum: f.checksum.clone(),
                    rewrite: Some(fingerprint(change)),
                };
                crate::store::origins::record(&self.db, &[origin])?;
            }
            if !destination.is_file() {
                return match std::fs::hard_link(&staged, destination) {
                    Ok(()) => Ok(Installed::Added),
                    // A file took the name: a decision nobody made, so it waits. Anything
                    // else there — a folder — is a disk to fix, not a name to choose.
                    Err(e)
                        if e.kind() == std::io::ErrorKind::AlreadyExists
                            && destination.is_file() =>
                    {
                        Ok(Installed::Taken)
                    }
                    Err(e) => {
                        Err(e).with_context(|| format!("installing {}", destination.display()))
                    }
                };
            }
            self.stop_at(Cut::BeforeRename)?;
            std::fs::rename(&staged, destination)
                .with_context(|| format!("installing {}", destination.display()))?;
            if let Some(folder) = destination.parent() {
                sync_folder(folder)?;
            }
            self.stop_at(Cut::AfterRename)?;
            match settling {
                Some((at, settling)) => {
                    let entry_id = Some(settling.replaced.entry_id.clone());
                    let kept_at = settling.replaced.kept_at.clone();
                    self.settle(&at, &settling)?;
                    Ok(Installed::Replaced { entry_id, kept_at })
                }
                None => Ok(Installed::Replaced {
                    entry_id: None,
                    kept_at: kept_at_for(landing),
                }),
            }
        })();
        // Gone whatever happened: renamed into place, linked there, or not wanted.
        let _ = std::fs::remove_file(&staged);
        let done = placed?;
        if matches!(done, Installed::Added | Installed::Replaced { .. }) {
            let _ = std::fs::remove_file(source);
        }
        Ok(done)
    }

    /// Step 1 of [`BulkImport::install`]: `staged` made from `source`, rewritten if asked.
    /// `Some` when what arrived is refused: not an archive that can be rewritten, dropped
    /// like a wrong checksum for the reason [`checksum_holds`] gives.
    /// A splice is made from the volume in place and the file received, beside the volume,
    /// so the volume itself is only ever renamed over, as any replacement is.
    fn prepare(
        &self,
        f: &ManifestFile,
        source: &Path,
        staged: &Path,
        destination: &Path,
        splice: Option<&SpliceAt>,
    ) -> Result<Option<Installed>> {
        let made = if let Some(splice) = splice {
            let wide = |(a, b): (u32, u32)| (a as usize, b as usize);
            crate::archive::cbz_writer::splice_into(
                destination,
                source,
                staged,
                wide(splice.cut),
                wide((splice.take.from, splice.take.to)),
                &splice.entry_json,
            )
        } else {
            let Some(change) = &f.rewrite else {
                link_or_copy(source, staged)?;
                return Ok(None);
            };
            let remove: Vec<&str> = change.remove.iter().map(String::as_str).collect();
            let mut members = vec![(
                crate::scan::scanner::ENTRY_JSON,
                change.entry_json.as_bytes(),
            )];
            if let Some(book) = &change.one_shot_json {
                members.push((crate::scan::scanner::ONESHOT_JSON, book.as_bytes()));
            }
            crate::archive::cbz_writer::rewrite_into(source, staged, &members, &remove)
        };
        match made {
            Ok(()) => Ok(None),
            Err(e) => {
                let _ = std::fs::remove_file(staged);
                // A splice reads the volume too, and a volume that fails is no reason to
                // drop the file sent: it stays in the session, and the commit says why.
                if splice.is_some()
                    && e.downcast_ref::<crate::archive::cbz_writer::FromSource>()
                        .is_none()
                {
                    return Err(e).with_context(|| {
                        format!(
                            "the volume {} could not be rewritten; the file sent stays in the session",
                            destination.display()
                        )
                    });
                }
                if !crate::archive::cbz_writer::unreadable(&e) {
                    return Err(e);
                }
                tracing::warn!(file = %source.display(), error = %e, "not an archive that can be rewritten");
                let _ = std::fs::remove_file(source);
                Ok(Some(Installed::Unreadable))
            }
        }
    }

    /// Step 2 of [`BulkImport::install`]: the new version read, the old file linked aside
    /// and what the replacement will record written in the session. `Err` carries a
    /// refusal, decided before anything is linked: an archive that does not open, or a
    /// volume the index does not hold. `Ok(None)` for a file that is no volume — a cover,
    /// a page — set aside all the same, with no entry to record it against.
    #[allow(clippy::type_complexity)]
    fn set_aside(
        &self,
        f: &ManifestFile,
        old: &Path,
        new: &Path,
        landing: &Landing<'_>,
    ) -> Result<std::result::Result<Option<(PathBuf, Settling)>, Installed>> {
        let archive = is_archive_name(landing.at);
        let mut recorded = None;
        if archive {
            let pages = match crate::archive::cbz::read(new, false) {
                Ok(content) => u32::try_from(content.pages.len())?,
                Err(e) if crate::archive::cbz_writer::unreadable(&e) => {
                    tracing::warn!(file = %f.path, error = %e, "not an archive that can be read");
                    let _ = std::fs::remove_file(new);
                    if let Ok(source) = under(&landing.session.join(RECEIVED), &f.path) {
                        let _ = std::fs::remove_file(source);
                    }
                    return Ok(Err(Installed::Unreadable));
                }
                Err(e) => return Err(e),
            };
            // Replaced, a volume the index does not hold would be a replacement nothing
            // records: no entry to name, no places to settle. It waits for the scan.
            let Some((entry_id, old_pages)) = self.entry_at(old)? else {
                return Ok(Err(Installed::Unheld));
            };
            recorded = Some((entry_id, old_pages, pages));
        }
        let kept_at = kept_at_for(landing);
        let kept = under(&self.inbox.join(REPLACED).join(landing.id), landing.at)?;
        if kept.is_file() {
            // Left by a commit cut short, and only of use if it is still the file in place.
            // Anything else there is the only copy of a file another hand replaced since.
            if !same_file(&kept, old) {
                tracing::warn!(
                    file = %old.display(),
                    "the volume is not the one this import set aside; left as it is"
                );
                return Ok(Err(Installed::Conflict));
            }
        } else {
            if let Some(parent) = kept.parent() {
                folder_made(parent)?;
            }
            link_or_copy(old, &kept)?;
        }
        self.stop_at(Cut::AfterLink)?;
        let Some((entry_id, old_pages, pages)) = recorded else {
            return Ok(Ok(None));
        };
        let last = pages.saturating_sub(1);
        let positions = match (landing.reset, landing.splice) {
            (Some(profile), _) => Positions::Reset {
                profile: profile.to_string(),
                last,
            },
            (None, Some(splice)) => Positions::Spliced {
                from: splice.cut.0,
                old_to: splice.cut.1,
                new_to: splice.new_to(),
                last,
            },
            (None, None) => Positions::Kept { last },
        };
        let settling = Settling {
            announced: f.path.clone(),
            replaced: Replaced {
                import_id: landing.id.to_string(),
                path: format!("{}/{}", landing.root, landing.at),
                entry_id,
                kept_at,
                replaced_at: now_ms(),
                profile_id: landing.profile.to_string(),
                old_size: std::fs::metadata(old)?.len(),
                new_size: std::fs::metadata(new)?.len(),
                old_pages: u32::try_from(old_pages).ok(),
                new_pages: Some(pages),
            },
            positions,
            old: identity_of(old)?,
            new: identity_of(new)?,
            new_checksum: if f.rewrite.is_some() || landing.splice.is_some() {
                Some(checksum(new)?)
            } else {
                None
            },
        };
        let at = under(
            &landing.session.join(SETTLING),
            &format!("{}.json", landing.at),
        )?;
        if let Some(parent) = at.parent() {
            folder_made(parent)?;
        }
        crate::store::files::write_whole(&at, &serde_json::to_vec(&settling)?)?;
        if let Some(parent) = at.parent() {
            sync_folder(parent)?;
        }
        self.stop_at(Cut::AfterSettling)?;
        Ok(Ok(Some((at, settling))))
    }

    /// What replacing the chapter `cut` of the volume at `destination` with the pages `take`
    /// of `source` needs, read from the two files themselves. `None` for a file received
    /// that is no archive; an `invalid` for a range outside the pages it has, or a volume
    /// that no longer has the chapter's, or declares nothing to move.
    fn splice_at(
        &self,
        source: &Path,
        destination: &Path,
        cut: (u32, u32),
        take: Splice,
    ) -> Result<Option<SpliceAt>> {
        let brought = match crate::archive::cbz::read(source, false) {
            Ok(content) => content.pages.len(),
            Err(e) if crate::archive::cbz_writer::unreadable(&e) => return Ok(None),
            Err(e) => return Err(e),
        };
        if take.from > take.to || take.to as usize >= brought {
            return Err(invalid(format!(
                "pages {}–{} are not pages of the file sent, which has {brought}",
                take.from, take.to
            )));
        }
        let volume = crate::archive::cbz::read(destination, false)
            .with_context(|| format!("the volume {} cannot be read", destination.display()))?;
        // Found where the scan finds it, under any folder of the archive: looked for at the
        // root only, an `entry.json` filed in a folder was a volume declaring nothing.
        let declared = volume
            .sidecar(crate::scan::scanner::ENTRY_JSON)
            .ok_or_else(|| invalid("the volume declares no chapter whose pages could move"))?
            .to_vec();
        // The cut was decided at the announcement, from the chapters the index held then.
        // Read again from the volume as it is now: replaced and rescanned in between, with
        // its chapters two pages further on, the old cut replaced two pages of the chapter
        // before along with the one asked for.
        let pages = u32::try_from(volume.pages.len())?;
        let starts = chapter_starts(&declared);
        let now = starts
            .iter()
            .position(|s| *s == Some(i64::from(cut.0)))
            .map(|aimed| chapter_cut(&starts, aimed, pages))
            .transpose()?;
        if now != Some(cut) {
            return Err(invalid(
                "the volume's chapters are no longer where they were at the announcement",
            ));
        }
        let mut splice = SpliceAt {
            cut,
            take,
            entry_json: Vec::new(),
        };
        splice.entry_json = pages_moved(&declared, cut, i64::from(splice.new_to()))?;
        Ok(Some(splice))
    }

    /// Step 4 of [`BulkImport::install`]: recorded and settled, then the session forgets it.
    ///
    /// Once. Replayed after a cut between the two, a reset sent back to the first page a
    /// reader who had read on since; a line already there means it was done.
    fn settle(&self, at: &Path, settling: &Settling) -> Result<()> {
        let done = &settling.replaced;
        if !replaced::recorded(&self.db, &done.import_id, &done.path)? {
            replaced::replace(&self.db, done, &settling.positions)?;
        }
        self.stop_at(Cut::AfterConsign)?;
        let _ = std::fs::remove_file(at);
        Ok(())
    }

    /// The replacements a commit of this session was cut in the middle of, each decided by
    /// what the index and the disk hold now:
    ///
    /// - **already recorded** — the cut came after the record: it is done, whatever has
    ///   happened to the volume since;
    /// - **the new file at the destination** — the rename happened: recorded and settled;
    /// - **the old file** — it did not: nothing happened that the index has to hear of, and
    ///   the link set aside and the file prepared beside go;
    /// - **anything else** — another session or another hand replaced the volume meanwhile.
    ///   Read as « the rename did not happen », this once let a resume rename its file over
    ///   the other's, and an abandon erase the only copy of the original. Nothing is touched:
    ///   the landing stays pending, the copy set aside stays, and the session can still be
    ///   abandoned.
    ///
    /// The files are compared by [`same_file`], never by their identity alone. A file left
    /// in the session that cannot be read is passed over, said in the log, and does not stop
    /// the others; the index refusing a record is an error, so whatever called this can be
    /// tried again with the session still there.
    ///
    /// The session's own files are written by the server and by whoever can write the
    /// inbox. What is checked as they are read back is where they point: the destination
    /// under the library, the copy set aside under this session's own folder of them.
    fn reconcile(&self, id: &str, session: &Path) -> Result<Vec<(String, Settled)>> {
        let folder = session.join(SETTLING);
        let mut out = Vec::new();
        for at in walk(&folder, &folder) {
            let read = under(&folder, &at).and_then(|file| {
                let (settling, kept, destination) = self.settling_at(id, &file)?;
                Ok((file, settling, kept, destination))
            });
            let (file, settling, kept, destination) = match read {
                Ok(read) => read,
                Err(e) => {
                    tracing::warn!(id, at, error = %e, "a replacement under way that cannot be read; passed over");
                    continue;
                }
            };
            let done = ReplacedFile {
                path: settling.announced.clone(),
                entry_id: Some(settling.replaced.entry_id.clone()),
                kept_at: settling.replaced.kept_at.clone(),
            };
            let source = under(&session.join(RECEIVED), &settling.announced).ok();
            let settled = if replaced::recorded(&self.db, id, &settling.replaced.path)? {
                let _ = std::fs::remove_file(&file);
                Settled::Finished(done)
            } else if arrived(&settling, &destination, source.as_deref()) {
                self.settle(&file, &settling)?;
                Settled::Finished(done)
            } else if same_file(&kept, &destination) {
                let _ = std::fs::remove_file(&kept);
                let _ = std::fs::remove_file(staged_for(&destination, id));
                let _ = std::fs::remove_file(&file);
                Settled::Cancelled
            } else {
                tracing::warn!(
                    id,
                    file = %destination.display(),
                    kept = %kept.display(),
                    "another file replaced the volume while this import was cut short; \
                     nothing touched, the copy set aside kept"
                );
                Settled::Conflicted
            };
            out.push((settling.announced, settled));
        }
        Ok(out)
    }

    /// One replacement under way read back, with the copy it set aside and its destination.
    fn settling_at(&self, id: &str, file: &Path) -> Result<(Settling, PathBuf, PathBuf)> {
        let settling: Settling = serde_json::from_slice(&std::fs::read(file)?)?;
        let own = format!("{REPLACED}/{id}/");
        let inside = settling
            .replaced
            .kept_at
            .strip_prefix(&own)
            .filter(|_| settling.replaced.import_id == id)
            .ok_or_else(|| anyhow::anyhow!("it speaks of another session's replacement"))?;
        let kept = under(&self.inbox.join(REPLACED).join(id), inside)?;
        let destination = under(&self.library, &settling.replaced.path)?;
        Ok((settling, kept, destination))
    }

    /// The entry whose file `file` is, and its page count — found by the file itself
    /// rather than by its spelling. The index holds the path the library was scanned
    /// under; reached through a link, the same file has another, and a comparison of text
    /// found nothing: the old file set aside, nothing recorded, nobody's place settled.
    /// One statement: the entries of that file name, then the one that is the same file.
    fn entry_at(&self, file: &Path) -> Result<Option<(String, i64)>> {
        let Some(name) = file.file_name().map(|n| n.to_string_lossy().into_owned()) else {
            return Ok(None);
        };
        let wanted = std::fs::canonicalize(file)?;
        let suffix = format!("/{name}");
        let named: Vec<(String, i64, String)> = self.db.read(|cx| {
            cx.query(
                "SELECT id, page_count, file FROM entry
                 WHERE substr(file, -length(?1)) = ?1",
                [&suffix],
                |r| Ok((r.get(0)?, r.get(1)?, r.get(2)?)),
            )
        })?;
        Ok(named
            .into_iter()
            .find(|(_, _, at)| std::fs::canonicalize(at).ok().as_ref() == Some(&wanted))
            .map(|(id, pages, _)| (id, pages)))
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
            if let Some(open) = self.open_session(&folder.path(), id) {
                out.push(open);
            }
        }
        out.sort_by_key(|o| o.last_touched_at);
        Ok(out)
    }

    /// What one session folder holds, or nothing when it holds no manifest — a folder
    /// under the inbox that is not an import is not an error, it is not ours.
    fn open_session(&self, folder: &Path, id: String) -> Option<Open> {
        // A session whose own files do not hold is not listed as one: listing it would show
        // where it claims to install as though the server meant to.
        let read = self.manifest(folder).and_then(|manifest| {
            let manifest = manifest.ok_or_else(|| anyhow::anyhow!("no manifest"))?;
            let landings = self.landings(folder, &manifest)?;
            Ok((manifest, landings))
        });
        let (manifest, landings) = match read {
            Ok(read) => read,
            Err(e) => {
                if folder.join(MANIFEST).exists() {
                    tracing::warn!(id, error = %e, "a waiting import whose session cannot be trusted");
                }
                return None;
            }
        };
        let target = self.library.join(&landings.root);
        // A listing that cannot read the origins shows a converted volume as unfinished,
        // which is a wrong number; it is not a reason to hide the whole session. Said, so
        // the wrong number has a cause somebody can find.
        let origins = self
            .origins_landed(&manifest, &landings)
            .unwrap_or_else(|e| {
                tracing::warn!(id, error = %e, "could not read the origins of a waiting import");
                HashMap::new()
            });
        let (mut bytes, mut complete, mut touched) = (0u64, 0usize, 0i64);
        for f in &manifest.files {
            let outcome = landings.outcome(&f.path);
            let home = outcome == Some(Outcome::Same)
                || landings.at_landing(f).is_some_and(|at| {
                    !stays_out(outcome)
                        && already_home(
                            &target,
                            &at,
                            &origin_key(&landings.root, &at.path),
                            &origins,
                        )
                });
            if home {
                complete += 1;
                continue;
            }
            let Ok(meta) = under(&folder.join(RECEIVED), &f.path)
                .and_then(|held| Ok(std::fs::metadata(held)?))
            else {
                continue;
            };
            bytes += meta.len();
            touched = touched.max(crate::api::intake::modified_at(&meta));
            if meta.len() == f.size {
                complete += 1;
            }
        }
        // Nothing has arrived yet, so the folder's own age is all there is to sort on.
        if touched == 0 {
            touched = std::fs::metadata(folder)
                .map(|m| crate::api::intake::modified_at(&m))
                .unwrap_or(0);
        }
        Some(Open {
            id,
            of: manifest.files.len(),
            root: landings.root,
            complete,
            bytes,
            last_touched_at: touched,
        })
    }

    /// The session removed, after what a commit of it was cut in the middle of is finished
    /// or undone (see [`BulkImport::reconcile`]).
    ///
    /// A copy set aside that nothing records goes only when it is the file still in place —
    /// a second name for the volume, nothing more. Any other is the only copy of something
    /// and stays under `replaced/`. A session whose own files cannot be read is removed all
    /// the same, `replaced/` left as it is: an import nobody can finish must still be one
    /// somebody can drop. The index refusing what the abandon has to record is another
    /// matter: the session stays, so the abandon can be tried again — gone, it took with it
    /// the only thing that knew a volume had been replaced and whose places to settle.
    pub fn abandon(&self, id: &str) -> Result<()> {
        let _one_at_a_time = self
            .commits
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        let Some(session) = self.session(id)? else {
            return Ok(());
        };
        self.sweep(id, &session)?;
        let _ = std::fs::remove_dir_all(session);
        Ok(())
    }

    /// What [`BulkImport::abandon`] tidies before the session goes.
    fn sweep(&self, id: &str, session: &Path) -> Result<()> {
        self.reconcile(id, session)?;
        let read = self.manifest(session).and_then(|manifest| {
            let manifest = manifest.ok_or_else(|| anyhow::anyhow!("no manifest"))?;
            self.landings(session, &manifest)
        });
        let landings = match read {
            Ok(landings) => landings,
            Err(e) => {
                tracing::warn!(id, error = %e, "abandoned with its copies set aside left as they are");
                return Ok(());
            }
        };
        let target = self.library.join(&landings.root);
        let recorded: HashSet<String> = replaced::kept_by(&self.db, id)?.into_iter().collect();
        let aside = self.inbox.join(REPLACED).join(id);
        for at in walk(&aside, &aside) {
            if recorded.contains(&format!("{REPLACED}/{id}/{at}")) {
                continue;
            }
            let kept = under(&aside, &at)?;
            if same_file(&kept, &under(&target, &at)?) {
                let _ = std::fs::remove_file(kept);
            }
        }
        if walk(&aside, &aside).is_empty() {
            let _ = std::fs::remove_dir_all(&aside);
        }
        for at in landings.files.values() {
            let _ = std::fs::remove_file(staged_for(&under(&target, at)?, id));
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

    /// The session's manifest; `None` when it has none, which is not one of ours.
    ///
    /// Its root is checked again as it is read, as it was when it was announced: the file is
    /// on the disk between the two, and a root that is no longer one folder of the library
    /// is refused rather than installed under.
    fn manifest(&self, session: &Path) -> Result<Option<ImportRequest>> {
        let Ok(bytes) = std::fs::read(session.join(MANIFEST)) else {
            return Ok(None);
        };
        let Ok(manifest) = serde_json::from_slice::<ImportRequest>(&bytes) else {
            return Ok(None);
        };
        if plain_name(&manifest.root).ok().as_deref() != Some(manifest.root.as_str()) {
            anyhow::bail!(
                "the session's manifest names {:?}, which is not one folder of the library",
                manifest.root
            );
        }
        announced_paths(&manifest)?;
        Ok(Some(manifest))
    }

    /// Where the session's files land, as its announcement decided — checked as it is read,
    /// never trusted: every landing must still be under the library folder the manifest
    /// names, and name only what the manifest announced. One that does not is refused, and
    /// the session with it; falling back to anything else would be installing somewhere
    /// nobody decided.
    fn landings(&self, session: &Path, manifest: &ImportRequest) -> Result<Landings> {
        // Its files sat at the session's root, where nothing looks any more: read as it is,
        // the state said each one missing and a commit installed the declarations alone.
        if !session.join(RECEIVED).is_dir() {
            return Err(invalid(
                "this import was opened by an earlier server, which kept its files \
                 elsewhere: abandon it and import again",
            ));
        }
        let Ok(bytes) = std::fs::read(session.join(LANDINGS)) else {
            // A session opened before landings were kept. One with a target was not, and
            // its landings are not to be guessed back from the paths.
            if manifest.target.is_some() {
                anyhow::bail!("the session's landings are missing");
            }
            return Ok(Landings::as_announced(manifest));
        };
        let landings: Landings =
            serde_json::from_slice(&bytes).context("the session's landings cannot be read")?;
        landings
            .checked(&self.library, manifest)
            .context("the session's landings are refused")?;
        Ok(landings)
    }

    /// The origins of the files of a session, keyed where they land.
    fn origins_landed(
        &self,
        manifest: &ImportRequest,
        landings: &Landings,
    ) -> Result<HashMap<String, Origin>> {
        let landed: Vec<ManifestFile> = manifest
            .files
            .iter()
            .filter_map(|f| landings.at_landing(f))
            .collect();
        self.origins_of(&landings.root, &landed)
    }
}

/// Every path an announcement names, files and declarations alike, is one place of its own
/// under the root — checked before a session exists, and again as one is read back.
///
/// Accepted as it came, `../../evade.cbz` opened a session the commit then refused to read:
/// listed by nothing, abandoned by no one, its folder held in the inbox for good. And
/// `x/../a.cbz` beside `a.cbz` were two names for one received file, the second written
/// over the first. So a path is read as [`plain_path`] reads it, and it is no other's, nor
/// the folder of another.
///
/// A declaration announced as a file is refused too: as a file it travelled as bytes
/// nobody read, and `readable_declarations` was there to refuse exactly those.
fn announced_paths(request: &ImportRequest) -> Result<()> {
    let files = request.files.iter().map(|f| (f.path.as_str(), true));
    let mut seen = HashSet::new();
    let mut plain = Vec::new();
    for (path, is_file) in files.chain(request.sidecars.iter().map(|s| (s.path.as_str(), false))) {
        let Some(at) = plain_path(path) else {
            return Err(invalid(format!("{path:?} is not a path under the root")));
        };
        if is_file && declared_by(Path::new(&at)).is_some() {
            return Err(invalid(format!(
                "{path} is a declaration: it is announced with the declarations, not sent as a file"
            )));
        }
        // The library holds volumes and the covers beside them. A `.html`, a script or a binary
        // announced as a file would be installed among them and sit in the tree for ever, read
        // by nothing.
        if is_file && !is_archive_name(&at) && !is_cover_name(&at) {
            return Err(invalid(format!(
                "{path} is neither a volume (.cbz, .zip) nor an image: nothing else is received as a file"
            )));
        }
        if !seen.insert(at.clone()) {
            return Err(invalid(format!("{path} is announced twice")));
        }
        plain.push(at);
    }
    for path in &plain {
        let mut folders = path.match_indices('/').map(|(at, _)| &path[..at]);
        if let Some(folder) = folders.find(|f| seen.contains(*f)) {
            return Err(invalid(format!(
                "{folder} is announced as a file and as the folder of {path}"
            )));
        }
    }
    Ok(())
}

/// The path an announced one names, resolved by itself — `None` when it names nothing, or
/// climbs above where it starts at any point.
///
/// Resolved by itself rather than under a base, because the same path is read under two:
/// the received files and the root. `../Essai/a.cbz` stayed under `Essai` and left the
/// received files, so the session it opened could receive nothing. `Extras/./Bonus.cbz` is
/// `Extras/Bonus.cbz`, as it always was.
fn plain_path(path: &str) -> Option<String> {
    if path.starts_with('/') || path.ends_with('/') || path.contains('\0') {
        return None;
    }
    let mut parts = Vec::new();
    for part in path.split('/') {
        match part {
            "" | "." => {}
            ".." => {
                parts.pop()?;
            }
            part => parts.push(part),
        }
    }
    (!parts.is_empty()).then(|| parts.join("/"))
}

/// Opens a file positioned at [`from`], truncating only when the write starts at zero.
/// Which of the three declarations a file name is, if it is one. Without case, because a
/// library carried across a filesystem that does not care comes back with `Work.json`.
pub(crate) fn declares(name: &str) -> Option<&'static str> {
    for (file, kind) in [
        (crate::scan::layout::UNIVERSE_FILE, "UNIVERSE"),
        (crate::scan::layout::WORK_FILE, "WORK"),
        (crate::scan::layout::EDITION_FILE, "EDITION"),
    ] {
        if name.eq_ignore_ascii_case(file) {
            return Some(kind);
        }
    }
    None
}

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
///
/// A file the server rewrote on arrival has another size than the one announced, so the
/// comparison above would call it changed and ask for it at every import. Its origin says
/// what it was made from: when the file is still as it was kept and the announcement is the
/// file it came from, it is home. The origin's checksum is compared, not the file's — the
/// bytes on the disk are not those the manifest speaks of.
fn already_home(
    target: &Path,
    file: &ManifestFile,
    key: &str,
    origins: &HashMap<String, Origin>,
) -> bool {
    let Ok(path) = under(target, &file.path) else {
        return false;
    };
    let Ok(present) = std::fs::metadata(&path) else {
        return false;
    };
    if !present.is_file() {
        return false;
    }
    if present.len() == file.size {
        return match &file.checksum {
            None => true,
            Some(announced) => {
                checksum(&path).is_ok_and(|found| found.eq_ignore_ascii_case(announced))
            }
        };
    }
    origins.get(key).is_some_and(|o| {
        present.len() == o.kept_size
            && o.origin_size == file.size
            && o.rewrite == file.rewrite.as_ref().map(fingerprint)
            && match (&file.checksum, &o.origin_checksum) {
                (None, _) => true,
                (Some(announced), Some(kept)) => kept.eq_ignore_ascii_case(announced),
                (Some(_), None) => false,
            }
    })
}

/// How an origin is keyed: where the file lands, relative to the library.
///
/// Resolved the way `under` resolves the landing place. The text as announced was the key,
/// and `Extras/./Bonus.cbz` and `Extras/Bonus.cbz` — one file on the disk — were two origins,
/// so a second import spelling the path the other way did not find the file it converted.
fn origin_key(root: &str, path: &str) -> String {
    normalise(&Path::new(root).join(path))
        .to_string_lossy()
        .into_owned()
}

/// The origin key of a file the library holds, from its path on the disk; `None` when it
/// does not sit under `library`, which no import put there.
///
/// Textual, like the scan, which records a file as its root joined to the walk: the
/// configured library is the root the scan walks, so the two spell the same prefix.
pub fn origin_key_of(library: &Path, file: &Path) -> Option<String> {
    normalise(file)
        .strip_prefix(normalise(library))
        .ok()
        .map(|relative| relative.to_string_lossy().into_owned())
}

/// What a rewrite was asked to do, as one digest: its `entry.json`, then the names it
/// removes, sorted — they are a set, and the same request listed in another order is the same
/// request. Serialised as JSON so no `entry.json` can end where a name begins.
///
/// The book, when there is one, is a third element; without one the digest is the one of the
/// two-element form, unchanged. Every origin recorded before `oneShotJson` existed holds
/// that digest, and a form that gained a `null` for everybody would have asked for each of
/// those files again at the next import.
fn fingerprint(rewrite: &ArchiveRewrite) -> String {
    use sha2::{Digest, Sha256};
    let mut remove: Vec<&str> = rewrite.remove.iter().map(String::as_str).collect();
    remove.sort_unstable();
    let asked = match &rewrite.one_shot_json {
        None => serde_json::json!([rewrite.entry_json, remove]),
        Some(book) => serde_json::json!([rewrite.entry_json, remove, book]),
    }
    .to_string();
    hex(&Sha256::digest(asked.as_bytes()))
}

fn hex(digest: &[u8]) -> String {
    digest.iter().map(|b| format!("{b:02x}")).collect()
}

/// Whether a path names an image the scan reads as a cover, by its extension.
fn is_cover_name(path: &str) -> bool {
    Path::new(path)
        .extension()
        .and_then(std::ffi::OsStr::to_str)
        .is_some_and(|e| {
            crate::scan::covers::EXTENSIONS
                .iter()
                .any(|known| known.eq_ignore_ascii_case(e))
        })
}

/// Whether a path names an archive the scan reads as a volume, by its extension.
pub(crate) fn is_archive_name(path: &str) -> bool {
    let lower = path.to_lowercase();
    lower.ends_with(".cbz") || lower.ends_with(".zip")
}

/// Refuses a declaration at the root of the drop that is not the target's own.
///
/// The root of the drop is laid on the target's folder, so a declaration there is
/// installed over the target's: an `edition.json` dropped on a work's folder would turn
/// the work into an edition of nothing, a `work.json` on an edition's folder a work inside
/// a work. Below the root, a declaration describes a folder of the drop, which is checked
/// like any other announcement's.
fn own_declarations_only(
    sidecars: &[ManifestSidecar],
    kind: TargetKind,
    place: &Place,
) -> Result<()> {
    let own = match kind {
        TargetKind::Universe => "UNIVERSE",
        // A series that is its work's only edition has the work's folder, which declares a
        // work; an edition of its own declares an edition.
        _ if place.work_folder.as_deref() == Some(place.folder.as_path()) => "WORK",
        _ => "EDITION",
    };
    for sidecar in sidecars {
        let at = normalise(Path::new(&sidecar.path));
        let at_root = at.parent().is_none_or(|p| p.as_os_str().is_empty());
        if let Some(declared) = declared_by(&at).filter(|_| at_root) {
            if declared != own {
                return Err(invalid(format!(
                    "{} declares a {} on a folder that is a {}",
                    sidecar.path,
                    declared.to_lowercase(),
                    own.to_lowercase()
                )));
            }
        }
    }
    Ok(())
}

/// The held volume this file already is, byte for byte, if it is one — under whatever
/// name it is held or announced.
///
/// The same test [`already_home`] makes of a path, made of every volume of the edition:
/// the same length and, when the announcement carries one, the same checksum; or, for a
/// volume the server converted on its way in, the origin it was made from. Two volumes it
/// could be are told apart by the number read in the file; past that it is none of them,
/// and the file is matched by its number like any other.
fn identical_to(
    file: &ManifestFile,
    landing: Option<&Path>,
    held: &[Held],
    library: &Path,
    origins: &HashMap<String, Origin>,
) -> Option<String> {
    let read = file.found.as_ref().and_then(|f| f.number);
    let candidates: Vec<&Held> = held
        .iter()
        .filter(|h| {
            let Some(key) = origin_key_of(library, &h.file) else {
                return false;
            };
            // Most volumes leave on their length, before anything is opened.
            let converted = origins
                .get(&key)
                .is_some_and(|o| o.origin_size == file.size);
            if h.size != file.size && !converted {
                return false;
            }
            let (Some(folder), Some(name)) = (h.file.parent(), h.file.file_name()) else {
                return false;
            };
            // A length says « the same » only of the volume the file says it is, or of the
            // one at its own path. Taken of the whole edition, the new volume 11 had the
            // length of the volume 10 held, was « already there », and was never sent.
            if file.checksum.is_none() {
                let same_number = read.is_some() && read == h.number;
                let same_path = landing.is_some_and(|at| at == normalise(&h.file));
                if !same_number && !same_path {
                    return false;
                }
            }
            let there = ManifestFile {
                path: name.to_string_lossy().into_owned(),
                ..file.clone()
            };
            already_home(folder, &there, &key, origins)
        })
        .collect();
    match candidates.as_slice() {
        [] => None,
        [only] => Some(only.entry_id.clone()),
        several => {
            let number = file.found.as_ref().and_then(|f| f.number);
            let mut by_number = several
                .iter()
                .filter(|h| h.number.is_some() && h.number == number);
            match (by_number.next(), by_number.next()) {
                (Some(one), None) => Some(one.entry_id.clone()),
                _ => None,
            }
        }
    }
}

/// Why a file of a commit is left aside: waiting for something, or not what was sent.
enum Aside {
    Pending,
    Corrupt,
}

impl Aside {
    /// Files `path` in the list this reason belongs to.
    fn put(self, path: &str, pending: &mut Vec<String>, corrupt: &mut Vec<String>) {
        match self {
            Aside::Pending => pending.push(path.to_string()),
            Aside::Corrupt => corrupt.push(path.to_string()),
        }
    }
}

/// Whether the bytes received for `f` are all there, and the ones announced.
fn received_whole(f: &ManifestFile, source: &Path) -> std::result::Result<(), Aside> {
    let complete = std::fs::metadata(source)
        .map(|m| m.is_file() && m.len() == f.size)
        .unwrap_or(false);
    if !complete {
        return Err(Aside::Pending);
    }
    if !checksum_holds(source, f.checksum.as_deref(), &f.path) {
        return Err(Aside::Corrupt);
    }
    Ok(())
}

/// Landing on something is a decision, and it is the reader's. Nothing in the session is
/// thrown away by refusing: the bytes stay where they are, and a later commit naming this
/// path installs them. A file of another series, or of another volume than the one aimed
/// at, is one too, wherever it lands.
fn waits_for_a_word(
    destination: &Path,
    outcome: Option<Outcome>,
    replacing: &[String],
    path: &str,
) -> bool {
    let decided = destination.is_file()
        || matches!(outcome, Some(Outcome::OtherSeries | Outcome::OtherNumber));
    decided && !replacing.iter().any(|named| named == path)
}

/// What a commit cut short left: the replacements to record, the files they finished, and
/// the files another hand took since.
fn split_settled(
    settled: Vec<(String, Settled)>,
) -> (Vec<ReplacedFile>, HashSet<String>, HashSet<String>) {
    let mut replaced = Vec::new();
    let mut finished = HashSet::new();
    let mut conflicted = HashSet::new();
    for (announced, settled) in settled {
        match settled {
            Settled::Finished(done) => {
                replaced.push(done);
                finished.insert(announced);
            }
            Settled::Cancelled => {}
            Settled::Conflicted => {
                conflicted.insert(announced);
            }
        }
    }
    (replaced, finished, conflicted)
}

/// Removes what the session received for `path`, when the path is one it can hold.
fn remove_received(session: &Path, path: &str) {
    if let Ok(source) = under(&session.join(RECEIVED), path) {
        let _ = std::fs::remove_file(source);
    }
}

/// Creates the folder a file is about to land in.
fn parent_created(destination: &Path) -> Result<()> {
    if let Some(parent) = destination.parent() {
        std::fs::create_dir_all(parent)?;
    }
    Ok(())
}

/// Installs each declaration the session received, unless it would write over one the
/// library holds and the request does not name it.
fn install_sidecars(
    manifest: &ImportRequest,
    landings: &Landings,
    session: &Path,
    target: &Path,
    declaring: &[String],
) -> Result<()> {
    for sidecar in &manifest.sidecars {
        let source = under(&session.join(RECEIVED), &sidecar.path)?;
        if !source.is_file() {
            continue;
        }
        let destination = under(target, &landings.sidecar(&sidecar.path))?;
        // Writing over a declaration the library already holds is a decision, and it is
        // the reader's — the same as landing on a volume. One with no counterpart goes
        // in whatever the request says: it is a creation, and `creates` announced it.
        if destination.is_file() && !declaring.iter().any(|named| named == &sidecar.path) {
            continue;
        }
        parent_created(&destination)?;
        install_sidecar(&source, &destination)?;
    }
    Ok(())
}

/// The files under `covered` a complete drop does not hold, sorted; none for an addition.
fn orphans_of(
    manifest: &ImportRequest,
    covered: &Path,
    target: &Path,
    expected: &HashSet<&str>,
) -> Vec<String> {
    let mut orphans: Vec<String> = if manifest.scope == Scope::Complete && covered.exists() {
        walk(covered, target)
            .into_iter()
            .filter(|p| !expected.contains(p.as_str()))
            .collect()
    } else {
        Vec::new()
    };
    orphans.sort();
    orphans
}

/// Where each file and sidecar of a drop lands so far, and which held edition each file is
/// compared with: what the target's kind decides, filled in by one of the `compare_*` below.
#[derive(Default)]
struct Laying {
    files: Vec<Option<String>>,
    sidecars: Vec<String>,
    /// Which edition each file is compared with, and that edition's folder.
    compared: Vec<Option<(String, PathBuf)>>,
    /// The names each compared edition's work goes by, for a file that names its series.
    names_of: HashMap<String, Vec<String>>,
    known: HashMap<String, Known>,
    brought: HashSet<String>,
    cuts: BTreeMap<String, (u32, u32)>,
    splice: Option<SpliceOutcome>,
}

impl Laying {
    /// A volume or a series target: every archive is compared with the edition aimed at.
    fn compare_with_target(&mut self, request: &ImportRequest, place: &Place) {
        let edition = place.editions.first().cloned().unwrap_or_default();
        self.names_of.insert(edition.clone(), target_names(place));
        for (i, f) in request.files.iter().enumerate() {
            if is_archive_name(&f.path) {
                self.compared[i] = Some((edition.clone(), place.folder.clone()));
            }
        }
    }

    /// A chapter target: the range of pages each archive names replaces the chapter, in
    /// the volume at `inside`.
    fn splice_into(
        &mut self,
        request: &ImportRequest,
        chapter: &str,
        volume: &held::ChapteredVolume,
        inside: &str,
    ) -> Result<()> {
        for (i, f) in request.files.iter().enumerate() {
            let Some(take) = f.splice.filter(|_| is_archive_name(&f.path)) else {
                continue;
            };
            let (cut, outcome) =
                splice_outcome(&volume.chapters, volume.pages, chapter, take, volume.page)?;
            self.cuts.insert(f.path.clone(), cut);
            self.splice = Some(outcome);
            self.files[i] = Some(inside.to_string());
        }
        Ok(())
    }

    /// A universe target: each folder of the drop is one series, matched with a work.
    fn compare_in_universe(
        &mut self,
        request: &ImportRequest,
        place: &Place,
        base: &Path,
        works: &[Titled],
        joined: &dyn Fn(&str) -> String,
    ) -> Result<()> {
        let universe_id = place.universe_id.as_deref().unwrap_or_default();
        for name in first_folders(request) {
            let declared = request.sidecars.iter().find(|s| {
                let at = normalise(Path::new(&s.path));
                declared_by(&at) == Some("WORK") && at.parent() == Some(Path::new(&name))
            });
            let identity = declared.and_then(declared_identity);
            let title = declared.and_then(|s| text_of(s, "title"));
            match series_of_universe(
                &name,
                identity.as_deref(),
                title.as_deref(),
                universe_id,
                works,
            ) {
                SeriesIs::New => {}
                SeriesIs::Elsewhere(work, matched) => {
                    self.known.insert(joined(&name), Known::of(work, matched));
                }
                SeriesIs::Ours(work) => self.bring_ours(request, place, base, &name, work)?,
            }
        }
        Ok(())
    }

    /// The folder `name` of the drop is `work`, which the universe holds: its paths follow
    /// the folders the work already has, and its archives are compared with their editions.
    fn bring_ours(
        &mut self,
        request: &ImportRequest,
        place: &Place,
        base: &Path,
        name: &str,
        work: &Titled,
    ) -> Result<()> {
        self.brought.insert(work.work_id.clone());
        let editions: Vec<&EditionFolder> = place
            .edition_folders
            .iter()
            .filter(|e| e.work_id == work.work_id)
            .collect();
        let renames = renames_into(base, name, work, &editions, request)?;
        for (i, f) in request.files.iter().enumerate() {
            if let Some(at) = renamed(&f.path, &renames) {
                under(base, &at)?;
                if self.files[i].is_some() {
                    self.files[i] = Some(at);
                }
            }
        }
        for (j, s) in request.sidecars.iter().enumerate() {
            if let Some(at) = renamed(&s.path, &renames) {
                under(base, &at)?;
                self.sidecars[j] = at;
            }
        }
        for (i, f) in request.files.iter().enumerate() {
            if !is_archive_name(&f.path) || renamed(&f.path, &renames).is_none() {
                continue;
            }
            let Some(at) = &self.files[i] else {
                continue;
            };
            let Some(folder) = under(base, at)?.parent().map(Path::to_path_buf) else {
                continue;
            };
            let Some(edition) = editions.iter().find(|e| normalise(&e.path) == folder) else {
                continue;
            };
            self.names_of.entry(edition.id.clone()).or_insert_with(|| {
                edition_names(
                    &folder,
                    &held::EditionAt {
                        id: edition.id.clone(),
                        work_name: work.name.clone(),
                        work_title: work.title.clone(),
                    },
                )
            });
            self.compared[i] = Some((edition.id.clone(), folder));
        }
        Ok(())
    }

    /// No target: each archive that says what it is is compared with the edition its
    /// folder is, when it is one.
    fn compare_by_folder(
        &mut self,
        db: &Db,
        ceiling: &Ceiling,
        request: &ImportRequest,
        base: &Path,
    ) -> Result<()> {
        let candidates: Vec<usize> = request
            .files
            .iter()
            .enumerate()
            .filter(|(_, f)| is_archive_name(&f.path) && f.found.is_some())
            .map(|(i, _)| i)
            .collect();
        // A file compared with an edition must arrive in it: a path that climbs out
        // of the folder announced reached another series and answered as an error
        // of the server's, where it is the request's.
        let mut arrival: HashMap<usize, PathBuf> = HashMap::new();
        for &i in &candidates {
            if let Some(at) = &self.files[i] {
                if let Some(folder) = under(base, at)?.parent() {
                    arrival.insert(i, folder.to_path_buf());
                }
            }
        }
        let mut folders: Vec<PathBuf> = arrival.values().cloned().collect();
        folders.sort();
        folders.dedup();
        let editions = if folders.is_empty() {
            HashMap::new()
        } else {
            held::editions_at(db, &folders, ceiling)?
        };
        for (i, folder) in arrival {
            if let Some(edition) = editions.get(&folder) {
                self.names_of
                    .entry(edition.id.clone())
                    .or_insert_with(|| edition_names(&folder, edition));
                self.compared[i] = Some((edition.id.clone(), folder));
            }
        }
        Ok(())
    }
}

/// Refuses what the target's kind cannot take: a range of pages anywhere but on a chapter,
/// a chapter without one, a volume or a chapter from anything but one archive.
fn refuse_misshapen(request: &ImportRequest, kind: Option<TargetKind>) -> Result<()> {
    let archives = request
        .files
        .iter()
        .filter(|f| is_archive_name(&f.path))
        .count();
    if kind != Some(TargetKind::Chapter) {
        if let Some(f) = request.files.iter().find(|f| f.splice.is_some()) {
            return Err(invalid(format!(
                "{}: a range of pages replaces a chapter, and only a chapter target has one",
                f.path
            )));
        }
    }
    // A chapter is replaced from the pages named, and only from those: without a range
    // the server would be guessing which pages of the file are the chapter. The
    // volume's `entry.json` is the server's to write then — its chapters start
    // elsewhere — so a rewrite asked alongside would be a second author of it.
    if kind == Some(TargetKind::Chapter) {
        if let Some(f) = request
            .files
            .iter()
            .find(|f| is_archive_name(&f.path) && (f.splice.is_none() || f.rewrite.is_some()))
        {
            return Err(invalid(format!(
                "{}: a chapter is replaced from a range of pages, and with no rewrite",
                f.path
            )));
        }
    }
    // One volume is one file: two archives for it would be a choice the server would
    // make by itself, and no archive is nothing to reimport.
    if matches!(kind, Some(TargetKind::Entry | TargetKind::Chapter)) && archives != 1 {
        return Err(invalid(format!(
            "a volume or a chapter is reimported from one archive, not {archives}"
        )));
    }
    Ok(())
}

/// Refuses a sidecar that declares more than the target can hold.
fn refuse_foreign_declarations(
    request: &ImportRequest,
    target: &ImportTarget,
    place: &Place,
) -> Result<()> {
    if target.kind != TargetKind::SeriesNewEditions {
        own_declarations_only(&request.sidecars, target.kind, place)?;
    } else if let Some(bad) = request
        .sidecars
        .iter()
        .find(|s| matches!(declared_by(Path::new(&s.path)), Some("WORK" | "UNIVERSE")))
    {
        // An edition added is an edition: a work or a universe declared in it would
        // be a series inside the series.
        return Err(invalid(format!(
            "{}: new editions are declared by edition.json, not by a work or a universe",
            bad.path
        )));
    }
    Ok(())
}

/// A path as announced, under the folder of the loose edition when it is loose, and under
/// the root's own folder.
fn joined_path(loose_edition: Option<&str>, under_root: &str, path: &str) -> String {
    let path = match loose_edition {
        Some(folder) if !path.contains('/') => format!("{folder}/{path}"),
        _ => path.to_string(),
    };
    if under_root.is_empty() {
        path
    } else {
        format!("{under_root}/{path}")
    }
}

/// Refuses two paths announced apart that land at one place.
fn refuse_shared_landings(
    request: &ImportRequest,
    files: &[Option<String>],
    sidecars: &[String],
) -> Result<()> {
    let mut landing_of: HashMap<PathBuf, &str> = HashMap::new();
    let announced = request
        .files
        .iter()
        .map(|f| f.path.as_str())
        .zip(files.iter().map(Option::as_deref))
        .chain(
            request
                .sidecars
                .iter()
                .map(|s| s.path.as_str())
                .zip(sidecars.iter().map(|at| Some(at.as_str()))),
        );
    for (path, at) in announced {
        let Some(at) = at else {
            continue;
        };
        if let Some(first) = landing_of.insert(normalise(Path::new(at)), path) {
            return Err(invalid(format!(
                "{first} and {path} would both land at {at}"
            )));
        }
    }
    Ok(())
}

/// Each folder declaring the identity of a work the library holds, and not known yet, is
/// known as that work.
fn adopt_identities(
    identified: Vec<(String, String)>,
    works: &[Titled],
    known: &mut HashMap<String, Known>,
) {
    for (folder, id) in identified {
        if known.contains_key(&folder) {
            continue;
        }
        if let Some(work) = works.iter().find(|w| w.work_id == id) {
            known.insert(folder, Known::of(work, Matched::Identity));
        }
    }
}

/// What the target leaves of what it holds, each named relative to its edition's folder.
fn absent_from(
    held: &[Held],
    touched: &HashSet<&str>,
    kind: TargetKind,
    place: &Place,
    folders: &HashMap<&str, &Path>,
) -> Vec<HeldVolume> {
    held.iter()
        .filter(|h| !touched.contains(h.entry_id.as_str()))
        .filter(|h| match kind {
            // What a volume target leaves is that volume, not the eleven others.
            TargetKind::Entry | TargetKind::Chapter => {
                place.entry_id.as_deref() == Some(h.entry_id.as_str())
            }
            TargetKind::Series | TargetKind::Universe => true,
            TargetKind::SeriesNewEditions => false,
        })
        .map(|h| {
            let file = folders
                .get(h.edition_id.as_str())
                .and_then(|folder| {
                    normalise(&h.file)
                        .strip_prefix(normalise(folder))
                        .ok()
                        .map(Path::to_path_buf)
                })
                .or_else(|| h.file.file_name().map(PathBuf::from))
                .unwrap_or_else(|| h.file.clone());
            HeldVolume {
                entry_id: h.entry_id.clone(),
                number: h.number,
                title: h.title.clone(),
                file: file.to_string_lossy().into_owned(),
                size: h.size,
                pages: h.pages,
                chapters: h.chapters.clone(),
                page: h.page,
                finished: h.finished,
                others: h.others,
            }
        })
        .collect()
}

/// A path as announced, once the folders the names moved are moved.
fn followed(moved: &[super::canonical::FolderMove], at: &str) -> String {
    moved.iter().fold(at.to_string(), |at, (old, new)| {
        match at.strip_prefix(old.as_str()) {
            Some(rest) if rest.is_empty() || rest.starts_with('/') => format!("{new}{rest}"),
            _ => at,
        }
    })
}

/// Each volume's landing takes the file name its file was given.
fn land_as_named(volumes: &mut [VolumeMatch], request: &ImportRequest, files: &[Option<String>]) {
    for v in volumes {
        let Some(i) = request.files.iter().position(|f| f.path == v.path) else {
            continue;
        };
        let (Some(lands), Some(at)) = (&v.lands, &files[i]) else {
            continue;
        };
        let name = at.rsplit_once('/').map_or(at.as_str(), |(_, n)| n);
        v.lands = Some(match lands.rsplit_once('/') {
            Some((folder, _)) => format!("{folder}/{name}"),
            None => name.to_string(),
        });
    }
}

/// The names a target's work goes by, which a file naming its series is compared with:
/// its folder and its title.
fn target_names(place: &Place) -> Vec<String> {
    let mut names: Vec<String> = place
        .work_folder
        .as_deref()
        .and_then(Path::file_name)
        .map(|n| n.to_string_lossy().into_owned())
        .into_iter()
        .collect();
    names.extend(
        place
            .trail
            .iter()
            .rev()
            .find(|step| step.kind == "WORK")
            .or_else(|| place.trail.iter().find(|step| step.kind == "SERIES"))
            .map(|step| step.name.clone()),
    );
    names
}

/// The folders right under the folder announced that hold something, each once — the series
/// of a universe drop. A file or a declaration at the top belongs to the universe itself.
fn first_folders(request: &ImportRequest) -> Vec<String> {
    let mut out: Vec<String> = request
        .files
        .iter()
        .map(|f| &f.path)
        .chain(request.sidecars.iter().map(|s| &s.path))
        .filter_map(|path| {
            let at = normalise(Path::new(path));
            let mut parts = at.components();
            let first = parts.next()?;
            parts.next()?;
            Some(first.as_os_str().to_string_lossy().into_owned())
        })
        .collect();
    out.sort();
    out.dedup();
    out
}

/// A text field of a declaration, when it carries one.
pub(crate) fn text_of(sidecar: &ManifestSidecar, key: &str) -> Option<String> {
    serde_json::from_str::<serde_json::Value>(&sidecar.json)
        .ok()?
        .get(key)?
        .as_str()
        .map(str::to_string)
}

/// Where the folder `name` of a drop goes once it is known to be `work`: the work's own
/// folder, and each edition folder inside it to the edition it names — by its folder, or
/// by the folded name of its `edition.json` when only one edition answers to it.
///
/// As pairs of the prefix announced and the one it lands under, relative to `base`: every
/// path is read from the index, none from the drop, so a folder name that climbs out of the
/// universe reaches nothing.
fn renames_into(
    base: &Path,
    name: &str,
    work: &Titled,
    editions: &[&EditionFolder],
    request: &ImportRequest,
) -> Result<Vec<(PathBuf, PathBuf)>> {
    let inside = |folder: &Path| -> Result<PathBuf> {
        normalise(folder)
            .strip_prefix(normalise(base))
            .map(Path::to_path_buf)
            .map_err(|_| invalid(format!("{} is not under its root", folder.display())))
    };
    let mut renames = vec![(PathBuf::from(name), inside(&work.path)?)];
    let mut inner: Vec<String> = request
        .files
        .iter()
        .map(|f| &f.path)
        .chain(request.sidecars.iter().map(|s| &s.path))
        .filter_map(|path| {
            let at = normalise(Path::new(path));
            let rest = at.strip_prefix(name).ok()?;
            let mut parts = rest.components();
            let first = parts.next()?;
            parts.next()?;
            Some(first.as_os_str().to_string_lossy().into_owned())
        })
        .collect();
    inner.sort();
    inner.dedup();
    for folder in inner {
        let held_there = normalise(&work.path.join(&folder));
        if editions.iter().any(|e| normalise(&e.path) == held_there) {
            continue;
        }
        let at = Path::new(name).join(&folder);
        let declared = request
            .sidecars
            .iter()
            .find(|s| {
                let path = normalise(Path::new(&s.path));
                declared_by(&path) == Some("EDITION") && path.parent() == Some(at.as_path())
            })
            .and_then(|s| text_of(s, "name"));
        let wanted = folded(declared.as_deref().unwrap_or(&folder));
        let named: Vec<&&EditionFolder> = editions
            .iter()
            .filter(|e| !e.implicit && e.name.as_deref().map(folded).as_ref() == Some(&wanted))
            .collect();
        if let [edition] = named.as_slice() {
            renames.push((at, inside(&edition.path)?));
        }
    }
    Ok(renames)
}

/// Where an announced path lands under the longest of `renames` it is inside, if any.
fn renamed(path: &str, renames: &[(PathBuf, PathBuf)]) -> Option<String> {
    let at = normalise(Path::new(path));
    renames
        .iter()
        .filter_map(|(from, to)| {
            let rest = at.strip_prefix(from).ok()?;
            Some((from.components().count(), to.join(rest)))
        })
        .max_by_key(|(depth, _)| *depth)
        .map(|(_, to)| to.to_string_lossy().into_owned())
}

/// The series a drop without a target would create — a folder that declares a work, or one
/// holding archives under no declaration — that are not on the disk yet, each with the
/// title it goes by: its `work.json`'s, or its folder's name.
fn new_series(base: &Path, root: &str, request: &ImportRequest) -> Vec<(String, String)> {
    let declared: Vec<(PathBuf, &'static str, &ManifestSidecar)> = request
        .sidecars
        .iter()
        .filter_map(|s| {
            let at = normalise(Path::new(&s.path));
            let kind = declared_by(&at)?;
            Some((at.parent().unwrap_or(Path::new("")).to_path_buf(), kind, s))
        })
        .collect();
    let mut folders: Vec<PathBuf> = declared
        .iter()
        .filter(|(_, kind, _)| *kind == "WORK")
        .map(|(folder, _, _)| folder.clone())
        .collect();
    for file in request.files.iter().filter(|f| is_archive_name(&f.path)) {
        let at = normalise(Path::new(&file.path));
        let folder = at.parent().unwrap_or(Path::new(""));
        let claimed = declared.iter().any(|(at, kind, _)| {
            (*kind == "WORK" && folder.starts_with(at)) || (*kind != "WORK" && at == folder)
        });
        if !claimed {
            folders.push(folder.to_path_buf());
        }
    }
    folders.sort();
    folders.dedup();
    folders
        .into_iter()
        .filter(|folder| !base.join(folder).exists())
        .map(|folder| {
            let title = declared
                .iter()
                .find(|(at, kind, _)| *kind == "WORK" && *at == folder)
                .and_then(|(_, _, s)| text_of(s, "title"))
                .or_else(|| folder.file_name().map(|n| n.to_string_lossy().into_owned()))
                .unwrap_or_else(|| root.to_string());
            (folder.to_string_lossy().into_owned(), title)
        })
        .collect()
}

/// The names an edition found at `folder` goes by: its folder, the one above it — an
/// edition of its own sits in its work's — and its work's name and title.
fn edition_names(folder: &Path, edition: &held::EditionAt) -> Vec<String> {
    let name_of = |p: &Path| p.file_name().map(|n| n.to_string_lossy().into_owned());
    let mut names: Vec<String> = name_of(folder).into_iter().collect();
    names.extend(folder.parent().and_then(name_of));
    names.push(edition.work_name.clone());
    names.extend(edition.work_title.clone());
    names
}

/// What this manifest would overwrite: a path already a file in the library, under this
/// root, that is not [`already_home`] — so `commit`'s rename would land on top of it.
///
/// Read at the same moment as `would_create` and `would_move`: nothing has moved yet, so
/// what a reader is told here is still true when they decide. A path new to the library —
/// `under` resolves it but nothing is there yet — is not a replacement, whatever else it is;
/// only an existing file, about to be landed on, counts.
fn would_replace(
    target: &Path,
    root: &str,
    files: &[ManifestFile],
    origins: &HashMap<String, Origin>,
) -> Vec<Replacement> {
    let mut out: Vec<Replacement> = Vec::new();
    for file in files {
        if already_home(target, file, &origin_key(root, &file.path), origins) {
            continue;
        }
        let Ok(path) = under(target, &file.path) else {
            continue;
        };
        let Ok(present) = std::fs::metadata(&path) else {
            continue;
        };
        if !present.is_file() {
            continue;
        }
        // Opened only while the ceiling allows. The read is one member out of an archive —
        // `extract` seeks to it rather than walking the pages — but a library re-dropped
        // whole names hundreds of these, and hundreds of opens is not an instant answer.
        let read = out.len() < DEEPEST_READ;
        let declared: Option<crate::metadata::sidecars::EntryJson> = read
            .then(|| crate::archive::cbz::extract(&path, crate::scan::scanner::ENTRY_JSON).ok())
            .flatten()
            .flatten()
            .and_then(|bytes| crate::metadata::sidecars::read(&bytes));
        out.push(Replacement {
            path: file.path.clone(),
            size: file.size,
            present_size: present.len(),
            present_title: declared.as_ref().and_then(|e| e.title.clone()),
            present_number: declared.as_ref().and_then(|e| e.number),
            present_read: read,
        });
    }
    out
}

/// What this folder declares that the library already holds somewhere else.
///
/// Only works: a universe or an edition arriving under a new parent is a structure being
/// rearranged around them, and moving those would rearrange far more than a reader asked
/// for. A series filed under its universe is the one case the import actually meets.
fn would_move(target: &Path, known: &HashMap<String, Known>) -> Vec<Relocation> {
    let mut out = Vec::new();
    for (folder, work) in known {
        let destination = target.join(folder);
        if destination.exists() || Path::new(&work.path) == destination {
            continue;
        }
        out.push(Relocation {
            work_id: work.work_id.clone(),
            name: work.name.clone(),
            from: work.path.clone(),
            at: folder.clone(),
            matched: work.matched,
        });
    }
    // Down the model, not across the disk, as `would_create` orders its own: depth first,
    // then the path, and the same answer for the same drop whatever order a map lists it in.
    out.sort_by(|a, b| {
        depth_of(&a.at)
            .cmp(&depth_of(&b.at))
            .then_with(|| a.at.cmp(&b.at))
    });
    out
}

/// What this manifest would write over: one entry per declaration the library already holds
/// at that path whose content differs from the arriving one.
///
/// Compared key by key and with `id` set aside, because the identity is kept from the
/// library's copy whatever the arriving one says — a folder prepared on a laptop carries no
/// identity at all, and reporting that as a difference would name every declaration of every
/// second import.
fn would_redeclare(target: &Path, sidecars: &[ManifestSidecar]) -> Vec<Declaration> {
    let mut out = Vec::new();
    for sidecar in sidecars {
        let Ok(path) = under(target, &sidecar.path) else {
            continue;
        };
        let Ok(here) = std::fs::read(&path) else {
            continue;
        };
        let present = sidecars::Document::of(&here);
        let arriving = sidecars::Document::of(sidecar.json.as_bytes());
        let mut differs: Vec<String> = Vec::new();
        for key in present.keys().into_iter().chain(arriving.keys()) {
            if key == crate::scan::identity::FIELD || differs.iter().any(|had| had == &key) {
                continue;
            }
            if present.value(&key) != arriving.value(&key) {
                differs.push(key);
            }
        }
        if differs.is_empty() {
            continue;
        }
        differs.sort();
        let held: serde_json::Map<String, serde_json::Value> = differs
            .iter()
            .filter_map(|key| present.value(key).map(|v| (key.clone(), v.clone())))
            .collect();
        out.push(Declaration {
            present: held,
            path: sidecar.path.clone(),
            present_name: present
                .text("title")
                .or_else(|| present.text("name"))
                .map(str::to_string),
            differs,
        });
    }
    out
}

/// The first page of each chapter an `entry.json` declares, in reading order: the order
/// the scan gives them their positions in — by number, or by the number a bonus is filed
/// after, a numbered chapter before what is anchored to it, then as declared.
fn chapter_starts(declared: &[u8]) -> Vec<Option<i64>> {
    let document = serde_json::from_slice::<serde_json::Value>(declared).unwrap_or_default();
    let volume = document.get("number").and_then(serde_json::Value::as_f64);
    let chapters = document
        .get("chapters")
        .and_then(|c| c.as_array())
        .cloned()
        .unwrap_or_default();
    let mut read: Vec<(f64, bool, usize, Option<i64>)> = chapters
        .iter()
        .enumerate()
        .map(|(i, c)| {
            let number = c.get("number").and_then(serde_json::Value::as_f64);
            let after = c.get("after").and_then(serde_json::Value::as_f64);
            let scale = number.or(after).or(volume).unwrap_or(0.0);
            let start = c.get("startPage").and_then(serde_json::Value::as_i64);
            (scale, number.is_none(), i, start)
        })
        .collect();
    read.sort_by(|a, b| {
        a.0.partial_cmp(&b.0)
            .unwrap_or(std::cmp::Ordering::Equal)
            .then_with(|| a.1.cmp(&b.1))
            .then_with(|| a.2.cmp(&b.2))
    });
    read.into_iter().map(|(_, _, _, start)| start).collect()
}

/// The volume's own `entry.json` with every page it names — each chapter's first page and
/// the cover — moved to where that page will be once `cut` becomes `cut.0..=new_to`.
///
/// Edited as JSON and not read into [`sidecars::EntryJson`]: a field this server does not
/// know must survive the trip, as it survives every other rewrite. Left alone, chapter 7
/// would start three pages early, inside the chapter replaced, and the cover would be a page
/// of another chapter.
fn pages_moved(declared: &[u8], cut: (u32, u32), new_to: i64) -> Result<Vec<u8>> {
    let mut document: serde_json::Value = serde_json::from_slice(declared)
        .map_err(|e| invalid(format!("the volume's entry.json cannot be read: {e}")))?;
    let object = document
        .as_object_mut()
        .ok_or_else(|| invalid("the volume's entry.json is not an object"))?;
    let moved = |value: &mut serde_json::Value| {
        if let Some(page) = value.as_i64() {
            *value = moved_page(page, cut, new_to).into();
        }
    };
    if let Some(cover) = object.get_mut("coverPage") {
        moved(cover);
    }
    if let Some(serde_json::Value::Array(chapters)) = object.get_mut("chapters") {
        for chapter in chapters {
            if let Some(start) = chapter.get_mut("startPage") {
                moved(start);
            }
        }
    }
    Ok(serde_json::to_vec(&document)?)
}

/// How many files a preflight opens to say what it already holds.
///
/// Past this the answer still names every replacement and still carries both sizes; what it
/// stops carrying is what the existing file declares, and `present_read` says so rather than
/// letting an absent title read as an archive that has none. A folder of thirty-four volumes
/// is read whole; a library re-dropped over itself is not, and the phase keeps the instant
/// answer it exists to give.
const DEEPEST_READ: usize = 32;

/// The identity a sidecar's own JSON carries, if it carries one at all.
///
/// A sidecar without one says nothing: it has never been scanned by any Leaf, so there is
/// nothing anywhere to move.
fn declared_identity(sidecar: &ManifestSidecar) -> Option<String> {
    Some(
        serde_json::from_str::<serde_json::Value>(&sidecar.json)
            .ok()?
            .get(crate::scan::identity::FIELD)?
            .as_str()?
            .to_string(),
    )
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
pub(crate) fn normalise(path: &Path) -> PathBuf {
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
    // One plain folder name: no separator left in it, and not a climb. Asked of the whole
    // string rather than of a substring — `contains("..")` refused `Terres d'Arran..2`,
    // which is a name, while letting nothing more through than this does.
    let climbs = name.split('/').any(|part| part == ".." || part == ".");
    if name.is_empty() || name.contains('/') || climbs {
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

/// What is wrong with the file written at `staged`, when it is not made of the pages it
/// should be; `None` when it is, and for a file announced without `members` or installed as it
/// arrived.
///
/// **The checksum stops at the door.** It proves the file reached the server; the server then
/// rewrites it, and the `new_checksum` it takes of the result is of its own work. Verified
/// against that, a page lost or damaged by the rewrite would verify perfectly. The pages are
/// the reference that survives it — the server never re-encodes one, and the rewrite and the
/// splice only change their names and what surrounds them.
///
/// Compared as a multiset of contents, never by name. What should be there is the pages of
/// the file received for a rewrite; for a splice, those of the volume outside the chapter cut
/// and those taken from the file received. The pages of the file received are first checked
/// against what the sender announced, so that the reference is the sender's and not the
/// server's own reading of it.
fn members_differ(
    f: &ManifestFile,
    source: &Path,
    staged: &Path,
    destination: &Path,
    splice: Option<&SpliceAt>,
) -> Result<Option<String>> {
    use crate::archive::cbz::{self, PageDigest};

    let Some(announced) = &f.members else {
        return Ok(None);
    };
    if f.rewrite.is_none() && splice.is_none() {
        return Ok(None);
    }
    let mut announced_sums: HashMap<String, usize> = HashMap::new();
    for page in announced {
        *announced_sums
            .entry(page.checksum.to_lowercase())
            .or_default() += 1;
    }
    let received = match cbz::page_digests(source) {
        Ok(received) => received,
        Err(e) => {
            return Ok(Some(format!(
                "{}: the file received cannot be read: {e:#}",
                f.path
            )))
        }
    };
    for page in &received {
        match announced_sums.get_mut(&page.checksum) {
            Some(left) if *left > 0 => *left -= 1,
            _ => {
                return Ok(Some(format!(
                    "{}: page {} is not the page announced",
                    f.path, page.name
                )))
            }
        }
    }

    let expected: Vec<PageDigest> = match splice {
        None => received,
        Some(splice) => {
            let named = |digests: Vec<PageDigest>| -> HashMap<String, String> {
                let mut by_name = HashMap::new();
                for page in digests {
                    by_name.entry(page.name).or_insert(page.checksum);
                }
                by_name
            };
            let held_sums = named(cbz::page_digests(destination)?);
            let brought_sums = named(received);
            let held = cbz::read(destination, false)?.pages;
            let brought = cbz::read(source, false)?.pages;
            let cut = (splice.cut.0 as usize, splice.cut.1 as usize);
            let kept = held[..cut.0]
                .iter()
                .chain(&held[cut.1 + 1..])
                .map(|p| (p, &held_sums));
            let taken = brought[splice.take.from as usize..=splice.take.to as usize]
                .iter()
                .map(|p| (p, &brought_sums));
            kept.chain(taken)
                .map(|(page, sums)| {
                    let checksum = sums
                        .get(&page.name)
                        .with_context(|| format!("{} is listed and not hashed", page.name))?;
                    Ok(PageDigest {
                        name: page.name.clone(),
                        checksum: checksum.clone(),
                    })
                })
                .collect::<Result<_>>()?
        }
    };

    let mut wanted: HashMap<String, Vec<String>> = HashMap::new();
    for page in expected {
        wanted.entry(page.checksum).or_default().push(page.name);
    }
    for page in cbz::page_digests(staged)? {
        if wanted.get_mut(&page.checksum).and_then(Vec::pop).is_none() {
            return Ok(Some(format!(
                "{}: page {} of the file written is not one that should be there",
                f.path, page.name
            )));
        }
    }
    if let Some(name) = wanted.values().find_map(|names| names.first()) {
        return Ok(Some(format!(
            "{}: page {name} is missing from the file written",
            f.path
        )));
    }
    Ok(None)
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
    Ok(hex(&hasher.finalize()))
}

/// Whether the file that arrived may be installed.
///
/// Checked only when the client announced a checksum. It costs a full read of the file,
/// which on nine gigabytes is not free, so it is the sender's call — but a checksum that is
/// sent and never compared is worse than none, because it reads like a guarantee.
///
/// A file that fails is dropped, not kept. The bytes are known to be wrong, so keeping them
/// helps nobody — and `state` reports a file of the right size as held, which would have
/// told the client there was nothing left to send while every commit went on refusing it.
fn checksum_holds(source: &Path, announced: Option<&str>, name: &str) -> bool {
    let Some(announced) = announced else {
        return true;
    };
    match checksum(source) {
        Ok(found) if found.eq_ignore_ascii_case(announced) => return true,
        Ok(found) => tracing::warn!(file = %name, announced, found, "checksum does not match"),
        Err(e) => tracing::warn!(file = %name, error = %e, "could not read to check"),
    }
    let _ = std::fs::remove_file(source);
    false
}
