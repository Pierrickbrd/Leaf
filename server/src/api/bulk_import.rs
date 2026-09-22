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

use std::collections::HashMap;
use std::ffi::OsStr;
use std::io::{Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};

use anyhow::{Context, Result};

use crate::api::{absent, invalid};
use serde::{Deserialize, Serialize};

use crate::api::intake::token;
use crate::metadata::sidecars;
use crate::store::Db;

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
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ImportOpened {
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
    /// Read for one question only: does the library already hold the identity this folder
    /// declares? `would_create` reads the **disk** on purpose — what is there is what is
    /// there — but "is this work already somewhere else" is not a question a folder can
    /// answer about itself. See [`BulkImport::would_move`].
    db: std::sync::Arc<Db>,
}

impl BulkImport {
    pub fn new(inbox: &Path, library: &Path, db: std::sync::Arc<Db>) -> Self {
        BulkImport {
            inbox: inbox.to_path_buf(),
            library: library.to_path_buf(),
            db,
        }
    }

    pub fn open(&self, request: &ImportRequest) -> Result<ImportOpened> {
        let root = plain_name(&request.root)?;
        readable_declarations(&request.sidecars)?;
        let id = format!("imp_{}", token());
        let session = self.inbox.join(&id);
        std::fs::create_dir_all(&session)?;

        let target = self.library.join(&root);
        let missing: Vec<&ManifestFile> = request
            .files
            .iter()
            .filter(|f| !already_home(&target, f))
            .collect();

        // Written where the files will be, so the commit's rename carries them home with
        // everything else. Without this a series would land without the declaration that
        // says what it is, and the scan behind it would classify a folder of archives as
        // nothing at all.
        for sidecar in &request.sidecars {
            let at = under(&session, &sidecar.path)?;
            if let Some(folder) = at.parent() {
                std::fs::create_dir_all(folder)?;
            }
            std::fs::write(&at, sidecar.json.as_bytes())?;
        }
        // Asked once, read by both lists below. Each used to ask for itself, one `db.read`
        // per declared work: a universe of fifty series cost a hundred reads where fifty
        // answer both questions.
        let known = self.works_known_elsewhere(&request.sidecars);
        let creates = self.would_create(&target, &request.sidecars, &known);
        let moves = self.would_move(&target, &request.sidecars, &known);
        let replaces = would_replace(&target, &request.files);
        let declarations = would_redeclare(&target, &request.sidecars);

        let stored = ImportRequest {
            root: root.clone(),
            files: request.files.clone(),
            scope: request.scope,
            sidecars: request.sidecars.clone(),
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
            creates,
            moves,
            replaces,
            declarations,
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
        known: &HashMap<String, (String, String, String)>,
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
            if kind == "WORK" && known.contains_key(&sidecar.path) {
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

    /// What this folder declares that the library already holds somewhere else.
    ///
    /// Only works: a universe or an edition arriving under a new parent is a structure being
    /// rearranged around them, and moving those would rearrange far more than a reader asked
    /// for. A series filed under its universe is the one case the import actually meets.
    fn would_move(
        &self,
        target: &Path,
        sidecars: &[ManifestSidecar],
        known: &HashMap<String, (String, String, String)>,
    ) -> Vec<Relocation> {
        let mut out = Vec::new();
        for sidecar in sidecars {
            let at = Path::new(&sidecar.path);
            if declared_by(at) != Some("WORK") {
                continue;
            }
            let folder = at.parent().unwrap_or(Path::new(""));
            let destination = target.join(folder);
            if destination.exists() {
                continue;
            }
            let Some((work_id, path, name)) = known.get(&sidecar.path).cloned() else {
                continue;
            };
            if Path::new(&path) == destination {
                continue;
            }
            out.push(Relocation {
                work_id,
                name,
                from: path,
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

    /// The work the index already files under the identity this sidecar carries.
    ///
    /// A sidecar without one says nothing: it has never been scanned by any Leaf, so there
    /// is nothing anywhere to move.
    /// The works the index already files under the identities these sidecars carry, by the
    /// path of the sidecar that named each one.
    ///
    /// One read for the whole manifest, and one statement inside it. `would_create` and
    /// `would_move` each used to ask per declared work, in a `db.read` of its own: fifty
    /// series announced cost a hundred reads where fifty answer both questions. That is the
    /// N+1 `store/db.rs` counts statements to make visible — it fails nothing, it only asks
    /// the same question twice and gets slower as a library grows.
    fn works_known_elsewhere(
        &self,
        sidecars: &[ManifestSidecar],
    ) -> HashMap<String, (String, String, String)> {
        let asked: Vec<(String, String)> = sidecars
            .iter()
            .filter(|s| declared_by(Path::new(&s.path)) == Some("WORK"))
            .filter_map(|s| declared_identity(s).map(|id| (s.path.clone(), id)))
            .collect();
        if asked.is_empty() {
            return HashMap::new();
        }
        // Built from the count, never from the values: the ids are bound as parameters, the
        // statement only says how many there are.
        let holes = std::iter::repeat_n("?", asked.len())
            .collect::<Vec<_>>()
            .join(",");
        let ids: Vec<&str> = asked.iter().map(|(_, id)| id.as_str()).collect();
        let found = self
            .db
            .read(|cx| {
                cx.query(
                    &format!("SELECT id, path, name FROM work WHERE id IN ({holes})"),
                    rusqlite::params_from_iter(ids.iter()),
                    |r| {
                        Ok((
                            r.get::<_, String>(0)?,
                            r.get::<_, String>(1)?,
                            r.get::<_, String>(2)?,
                        ))
                    },
                )
            })
            .unwrap_or_default();
        let by_id: HashMap<&str, &(String, String, String)> =
            found.iter().map(|row| (row.0.as_str(), row)).collect();
        asked
            .into_iter()
            .filter_map(|(path, id)| {
                by_id
                    .get(id.as_str())
                    .map(|row| (path, (row.0.clone(), row.1.clone(), row.2.clone())))
            })
            .collect()
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
        let Some(manifest) = self.manifest(&session) else {
            return Ok(None);
        };
        let target = self.library.join(&manifest.root);
        for sidecar in &manifest.sidecars {
            let at = Path::new(&sidecar.path);
            if declared_by(at) != Some("WORK") {
                continue;
            }
            let carried = serde_json::from_str::<serde_json::Value>(&sidecar.json)
                .ok()
                .and_then(|v| {
                    v.get(crate::scan::identity::FIELD)
                        .and_then(|i| i.as_str().map(str::to_string))
                });
            if carried.as_deref() == Some(work_id) {
                return Ok(Some(target.join(at.parent().unwrap_or(Path::new("")))));
            }
        }
        Ok(None)
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
    ) -> Result<ImportResult> {
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
            if !checksum_holds(&source, f.checksum.as_deref(), &f.path) {
                corrupt.push(f.path.clone());
                continue;
            }
            let destination = under(&target, &f.path)?;
            // Landing on something is a decision, and it is the reader's. Nothing in the
            // session is thrown away by refusing: the bytes stay where they are, and a
            // later commit naming this path installs them.
            if destination.is_file() && !replacing.iter().any(|named| named == &f.path) {
                pending.push(f.path.clone());
                continue;
            }
            if let Some(parent) = destination.parent() {
                std::fs::create_dir_all(parent)?;
            }
            crate::api::intake::move_or_copy(&source, &destination)?;
            installed += 1;
        }

        // The declarations, which are not in `files` and never were: they arrived in the
        // announcement, a few hundred bytes each, and they are what says what all this is.
        // Installed after the volumes and not counted among them — a folder is not "one
        // more file imported" because it finally said its own name.
        for sidecar in &manifest.sidecars {
            let source = under(&session, &sidecar.path)?;
            if !source.is_file() {
                continue;
            }
            let destination = under(&target, &sidecar.path)?;
            // Writing over a declaration the library already holds is a decision, and it is
            // the reader's — the same as landing on a volume. One with no counterpart goes
            // in whatever the request says: it is a creation, and `creates` announced it.
            if destination.is_file() && !declaring.iter().any(|named| named == &sidecar.path) {
                continue;
            }
            if let Some(parent) = destination.parent() {
                std::fs::create_dir_all(parent)?;
            }
            install_sidecar(&source, &destination)?;
        }

        let expected: std::collections::HashSet<&str> = manifest
            .files
            .iter()
            .map(|f| f.path.as_str())
            .chain(manifest.sidecars.iter().map(|s| s.path.as_str()))
            .collect();
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
            // Filled by the route, which is where a move happens: the folder has to be in
            // the index before anything is filed under it, and the index only learns of it
            // from the rescan that follows this.
            moved: Vec::new(),
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
        let manifest = self.manifest(folder)?;
        let target = self.library.join(&manifest.root);
        let (mut bytes, mut complete, mut touched) = (0u64, 0usize, 0i64);
        for f in &manifest.files {
            if already_home(&target, f) {
                complete += 1;
                continue;
            }
            let Ok(meta) = under(folder, &f.path).and_then(|held| Ok(std::fs::metadata(held)?))
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
            root: manifest.root,
            complete,
            bytes,
            last_touched_at: touched,
        })
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
/// Which of the three declarations a file name is, if it is one. Without case, because a
/// library carried across a filesystem that does not care comes back with `Work.json`.
fn declares(name: &str) -> Option<&'static str> {
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

/// What this manifest would overwrite: a path already a file in the library, under this
/// root, that is not [`already_home`] — so `commit`'s rename would land on top of it.
///
/// Read at the same moment as `would_create` and `would_move`: nothing has moved yet, so
/// what a reader is told here is still true when they decide. A path new to the library —
/// `under` resolves it but nothing is there yet — is not a replacement, whatever else it is;
/// only an existing file, about to be landed on, counts.
fn would_replace(target: &Path, files: &[ManifestFile]) -> Vec<Replacement> {
    let mut out: Vec<Replacement> = Vec::new();
    for file in files {
        if already_home(target, file) {
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
        out.push(Declaration {
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
