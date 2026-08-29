//! The everyday path: one file at a time.
//!
//! The desktop is only a workbench — you pull a volume down, retouch it, send it back, and
//! the file says for itself what it belongs to. **Nothing is filed without your say-so**: the
//! server proposes a destination and waits.
//!
//! The proposal carries how sure it is and why, so a client can decide whether to ask. Four
//! answers, and the difference between them is the whole point: a file that names an entry
//! of this library is a round trip, a file that names a work is a new volume, a file that
//! names nothing is a question.

use std::path::{Path, PathBuf};

use anyhow::Result;
use serde::{Deserialize, Serialize};

use crate::api::records::{read_entry_json, Records};
use crate::api::{absent, invalid};
use crate::metadata::sidecars::EntryJson;
use crate::store::Db;

/// How sure the server is about the file just handed to it.
#[derive(Debug, Clone, Copy, Serialize)]
pub enum Confidence {
    /// One series matches and the number is free.
    #[serde(rename = "CERTAIN")]
    Certain,
    /// An entry already occupies that place: this is probably a replacement.
    #[serde(rename = "REPLACEMENT")]
    Replacement,
    /// Several series match — the client has to ask.
    #[serde(rename = "AMBIGUOUS")]
    Ambiguous,
    /// Nothing matches: the file does not say where it comes from.
    #[serde(rename = "UNKNOWN")]
    Unknown,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SeriesCandidate {
    pub series_id: String,
    pub name: String,
}

#[derive(Debug, Clone, Default, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct FileReading {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub work: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub edition: Option<String>,
    #[serde(rename = "type", skip_serializing_if = "Option::is_none")]
    pub kind: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub number: Option<f64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub title: Option<String>,
    #[serde(skip_serializing_if = "is_zero")]
    pub chapter_count: usize,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Proposal {
    pub received: String,
    pub name: String,
    pub size: u64,
    pub read: FileReading,
    pub confidence: Confidence,
    pub reason: String,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub candidates: Vec<SeriesCandidate>,
    /// The entry this file would replace, if any.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub replaces: Option<String>,
    /// What the file says about itself that does not hold together.
    ///
    /// The same reading a scan does, run on the file in your hand rather than on a library
    /// you are not looking at. A scan reports it into a list nobody reads until something
    /// is already wrong; here it arrives at the one moment a person is looking straight at
    /// the file and can still say no.
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub concerns: Vec<String>,
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct FileRequest {
    pub series_id: String,
    #[serde(default)]
    pub replaces_entry_id: Option<String>,
    /// What to do when a file of that name is already in the series.
    ///
    /// Absent means "ask me", and asking is what the server does: it refuses and describes
    /// what it found. A name is not an identity, and the one thing that must never happen
    /// quietly is a volume being written over.
    #[serde(default)]
    pub on_collision: Option<OnCollision>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Deserialize, Serialize)]
pub enum OnCollision {
    /// Write over the file already there.
    #[serde(rename = "REPLACE")]
    Replace,
    /// Keep both. The arriving one is filed under a free name.
    #[serde(rename = "RENAME")]
    Rename,
}

/// A file of that name is already in the series, and nobody has said which one wins.
///
/// Carries what each of the two says about itself, so the question put to a person is about
/// the volumes rather than about the file names — which is the only level at which it can
/// be answered.
#[derive(Debug, Clone, Serialize, thiserror::Error)]
#[serde(rename_all = "camelCase")]
#[error("a file named \"{would_become}\" is already there")]
pub struct Collision {
    /// The file already at that name.
    pub path: String,
    /// The entry it is, when the index knows it. Absent for a file dropped into the folder
    /// and not yet scanned.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub entry_id: Option<String>,
    /// What the file already there says about itself.
    pub occupies: FileReading,
    /// What the arriving one says.
    pub arriving: FileReading,
    /// Whether the two describe the same volume — see [`same_volume`]. True is the case
    /// where REPLACE is the sensible answer; false is where RENAME is.
    pub same_volume: bool,
    /// The declared fields the two agree on, **`title` included**.
    ///
    /// The title cannot identify — a volume corrected locally to fix its title and then
    /// brought back is the same volume, and treating it as another would file a duplicate.
    /// But two files that agree on nothing except their title agreeing exactly is worth
    /// seeing before deciding, so it is reported rather than judged.
    pub agrees: Vec<String>,
    /// The same bytes on both sides. Settles it: whatever the sidecars say, there is
    /// nothing to lose by replacing and nothing to gain by keeping two.
    pub identical: bool,
    /// The name it would take under RENAME.
    pub would_become: String,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Filed {
    pub entry_id: String,
    pub path: String,
    pub replacement: bool,
    /// Filed under a name of its own because that one was taken.
    #[serde(skip_serializing_if = "std::ops::Not::not")]
    pub renamed: bool,
    /// Filled in when the declared count moved, or should have.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub note: Option<String>,
}

/// How a staged file got here, which decides what abandoning it costs.
///
/// Written into the folder's own name, so it survives a restart without a file beside the
/// one being staged — and `file` finds that one by looking for the only file there is.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
pub enum Origin {
    /// Sent over the wire. Whoever sent it still has it.
    #[serde(rename = "UPLOAD")]
    Upload,
    /// Taken from the shared folder, and taken *out* of it: with `consume`, which is the
    /// default, this staged copy is the only one left.
    #[serde(rename = "DROP")]
    Drop,
}

impl Origin {
    fn prefix(self) -> &'static str {
        match self {
            Origin::Upload => "rcv_",
            Origin::Drop => "drp_",
        }
    }

    fn of(id: &str) -> Option<Origin> {
        match &id[..id.len().min(4)] {
            "rcv_" => Some(Origin::Upload),
            "drp_" => Some(Origin::Drop),
            _ => None,
        }
    }
}

/// A file waiting for a decision, and what is known about it without opening it.
#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Waiting {
    pub id: String,
    pub name: String,
    pub size: u64,
    /// When it last changed, in milliseconds. What matters is how long it has sat there.
    pub last_touched_at: i64,
    pub origin: Origin,
    /// True when this is the only copy: it was consumed from the shared folder, so
    /// abandoning it does not send you back to a file you still have.
    pub only_copy: bool,
}

pub struct Intake {
    received: PathBuf,
    db: std::sync::Arc<Db>,
}

/// Where a series lives, and what it says about its own length.
struct Series {
    path: PathBuf,
    status: Option<String>,
    volume_count: Option<i32>,
    edition_name: Option<String>,
    work_name: String,
}

impl Intake {
    pub fn new(inbox: &Path, db: std::sync::Arc<Db>) -> Self {
        Intake {
            received: inbox.join("received"),
            db,
        }
    }

    /// Where a file being taken in should be written.
    ///
    /// Public so the local drop can put a file there by renaming rather than by sending its
    /// bytes through the loopback.
    ///
    /// The folder is cleared when the returned value is dropped, unless [`Staging::keep`]
    /// says otherwise. See [`Staging`] for why that is a destructor and not an `if`.
    /// Staging for a file that arrived over the wire.
    pub fn staging_for(&self, name: &str) -> Result<Staging> {
        self.staging_from(name, Origin::Upload)
    }

    /// Staging, saying how the file got here.
    pub fn staging_from(&self, name: &str, origin: Origin) -> Result<Staging> {
        let plain = Path::new(name)
            .file_name()
            .map(|n| n.to_string_lossy().to_string())
            .unwrap_or_default();
        if plain.trim().is_empty() || plain.contains("..") {
            return Err(invalid(format!("invalid file name: {name}")));
        }
        let folder = self
            .received
            .join(format!("{}{}", origin.prefix(), token()));
        std::fs::create_dir_all(&folder)?;
        Ok(Staging {
            file: folder.join(plain),
            keep: false,
        })
    }

    /// Streams a file to disk under a ceiling, then proposes where it belongs.
    ///
    /// Nothing is streamed to disk without one: an unbounded upload is a way to fill the
    /// disk that needs no bug at all, only patience.
    pub fn receive(&self, name: &str, body: &[u8], max_bytes: u64) -> Result<Proposal> {
        if body.len() as u64 > max_bytes {
            return Err(crate::api::bulk_import::over(max_bytes));
        }
        let staged = self.staging_for(name)?;
        std::fs::write(staged.path(), body)?;
        let proposal = self.propose_for(staged.path())?;
        staged.keep();
        Ok(proposal)
    }

    /// Reads what a staged file says about itself and proposes where it belongs.
    pub fn propose_for(&self, file: &Path) -> Result<Proposal> {
        let id = file
            .parent()
            .and_then(|p| p.file_name())
            .map(|n| n.to_string_lossy().to_string())
            .unwrap_or_default();
        let read = read_entry_json(file);
        let reading = reading_of(read.as_ref());
        let size = std::fs::metadata(file).map(|m| m.len()).unwrap_or(0);
        let name = file
            .file_name()
            .unwrap_or_default()
            .to_string_lossy()
            .to_string();
        let from_name = crate::metadata::label::parse(
            file.file_stem()
                .unwrap_or_default()
                .to_string_lossy()
                .as_ref(),
        );
        let kind = if read
            .as_ref()
            .is_some_and(|r| r.kind.eq_ignore_ascii_case("CHAPTER"))
            || from_name.label.to_lowercase().contains("chap")
        {
            "CHAPTER"
        } else {
            "VOLUME"
        };
        let concerns = crate::scan::checks::coherence(
            &name,
            read.as_ref(),
            kind,
            crate::archive::cbz::read(file, false)
                .map(|c| c.pages.len() as i32)
                .unwrap_or(0),
            from_name.number,
        );
        tracing::info!(
            file = %file.file_name().unwrap_or_default().to_string_lossy(),
            size,
            work = reading.work.as_deref().unwrap_or("none"),
            "taken in"
        );
        let mut proposal = self.propose(
            &id,
            &name,
            size,
            reading,
            read.as_ref().and_then(|r| r.id.clone()).as_deref(),
        )?;
        proposal.concerns = concerns;
        Ok(proposal)
    }

    fn propose(
        &self,
        id: &str,
        name: &str,
        size: u64,
        read: FileReading,
        announced_id: Option<&str>,
    ) -> Result<Proposal> {
        let answer = |confidence, reason: String, candidates, replaces| Proposal {
            received: id.to_string(),
            name: name.to_string(),
            size,
            read: read.clone(),
            confidence,
            reason,
            candidates,
            replaces,
            concerns: Vec::new(),
        };

        // The file carries the id of a known entry: this is a round trip.
        if let Some(announced) = announced_id {
            let known: Option<String> = self.db.read(|cx| {
                cx.query_one(
                    "SELECT edition_id FROM entry WHERE id = ?1",
                    [announced],
                    |r| r.get::<_, String>(0),
                )
            })?;
            if let Some(edition_id) = known {
                return Ok(answer(
                    Confidence::Replacement,
                    "this file came from this library: it replaces the entry it names".into(),
                    vec![self.candidate(&edition_id)?],
                    Some(announced.to_string()),
                ));
            }
        }

        let Some(work) = read
            .work
            .as_deref()
            .map(str::trim)
            .filter(|w| !w.is_empty())
        else {
            return Ok(answer(
                Confidence::Unknown,
                "the file declares no work".into(),
                self.all_series()?,
                None,
            ));
        };

        let ids: Vec<String> = self.db.read(|cx| {
            cx.query(
                "SELECT e.id FROM edition e JOIN work w ON w.id = e.work_id
                 WHERE (w.name = ?1 COLLATE NOCASE OR w.title = ?1 COLLATE NOCASE)
                   AND (?2 IS NULL OR e.name = ?2 COLLATE NOCASE)",
                rusqlite::params![work, read.edition],
                |r| r.get::<_, String>(0),
            )
        })?;
        let candidates: Vec<SeriesCandidate> = ids
            .iter()
            .map(|id| self.candidate(id))
            .collect::<Result<_>>()?;

        Ok(match candidates.len() {
            0 => answer(
                Confidence::Unknown,
                format!("no series is named \"{work}\""),
                self.all_series()?,
                None,
            ),
            1 => {
                let series_id = candidates[0].series_id.clone();
                let occupant = match read.number {
                    Some(number) => self.entry_at_number(&series_id, number)?,
                    None => None,
                };
                match occupant {
                    Some(entry) => answer(
                        Confidence::Replacement,
                        format!(
                            "number {} is already taken in \"{}\"",
                            short(read.number.unwrap_or_default()),
                            candidates[0].name
                        ),
                        candidates,
                        Some(entry),
                    ),
                    None => answer(
                        Confidence::Certain,
                        format!("work \"{work}\" recognised, number free"),
                        candidates,
                        None,
                    ),
                }
            }
            n => answer(
                Confidence::Ambiguous,
                format!("{n} editions carry that name"),
                candidates,
                None,
            ),
        })
    }

    /// Files a staged file where the client confirmed it belongs.
    ///
    /// Only here does anything move, and the move is a rename when the inbox and the library
    /// share a filesystem — which is the reason they are required to.
    pub fn file(&self, received_id: &str, request: &FileRequest) -> Result<Filed> {
        let folder = self
            .received_folder(received_id)?
            .ok_or_else(|| absent(format!("unknown intake: {received_id}")))?;
        let source = std::fs::read_dir(&folder)?
            .flatten()
            .map(|e| e.path())
            .find(|p| p.is_file())
            .ok_or_else(|| absent(format!("empty intake: {received_id}")))?;

        let series = self
            .db
            .read(|cx| {
                cx.query_one(
                    "SELECT e.path, e.status, e.volume_count, e.name AS edition_name,
                            w.name AS work_name
                     FROM edition e JOIN work w ON w.id = e.work_id WHERE e.id = ?1",
                    [request.series_id.as_str()],
                    |r| {
                        Ok(Series {
                            path: PathBuf::from(r.get::<_, String>(0)?),
                            status: r.get(1)?,
                            volume_count: r.get(2)?,
                            edition_name: r.get(3)?,
                            work_name: r.get(4)?,
                        })
                    },
                )
            })?
            .ok_or_else(|| absent(format!("unknown series: {}", request.series_id)))?;

        // The entry named must belong to the series named. Otherwise a request pairing
        // series A with an entry of series B writes over B's file and stamps it as A's —
        // the same destruction the naming rule below prevents, arriving by another door.
        let previous = match &request.replaces_entry_id {
            Some(id) => Some(
                self.entry_path_within(id, &request.series_id)?
                    .ok_or_else(|| {
                        invalid(format!(
                            "entry {id} does not belong to series {}",
                            request.series_id
                        ))
                    })?,
            ),
            None => None,
        };
        // True whichever way it was decided — by naming the entry, or by answering a
        // collision with REPLACE. Both write over a file that was there, and a client that
        // reads this field is asking "did something go away", not "how did I phrase it".
        let mut replacement = previous.is_some();
        // A name is not an identity. Two different volumes can be called "Tome 1.cbz" —
        // that is what comes out of a downloads folder more often than not — so the only
        // file this ever writes over is one somebody named: the entry to replace, or the
        // occupant of a name whose collision was answered.
        let mut renamed = false;
        let target = match previous {
            Some(path) => path,
            None => {
                let wanted = source
                    .file_name()
                    .unwrap_or_default()
                    .to_string_lossy()
                    .to_string();
                let occupied = series.path.join(&wanted);
                match (occupied.exists(), request.on_collision) {
                    (false, _) => occupied,
                    (true, Some(OnCollision::Replace)) => {
                        replacement = true;
                        occupied
                    }
                    (true, Some(OnCollision::Rename)) => {
                        renamed = true;
                        free_name(&series.path, &wanted)
                    }
                    // Nobody has said which one wins. Refused, with what each of the two
                    // says about itself, so the question can be put about the volumes
                    // rather than about the file names.
                    (true, None) => {
                        return Err(anyhow::Error::new(self.collision(
                            &occupied,
                            &source,
                            &series.path,
                            &wanted,
                        )?))
                    }
                }
            }
        };

        if let Some(parent) = target.parent() {
            std::fs::create_dir_all(parent)?;
        }
        move_or_copy(&source, &target)?;
        let _ = std::fs::remove_dir_all(&folder);

        let entry_id = crate::scan::scanner::id_of(&target, "");
        let read = read_entry_json(&target);
        let number = read.as_ref().and_then(|r| r.number);
        if let Err(e) = self.records().stamp(
            &target,
            &entry_id,
            &series.work_name,
            series.edition_name.as_deref(),
            number,
            read.as_ref().map(|r| r.kind.as_str()).unwrap_or("VOLUME"),
        ) {
            tracing::warn!(file = %target.display(), error = %e, "could not stamp on arrival");
        }

        let mut note = self.adjust_count(&request.series_id, &series, number)?;
        if renamed {
            let said = format!(
                "a file of that name was already there — filed as \"{}\"",
                target.file_name().unwrap_or_default().to_string_lossy()
            );
            note = Some(match note {
                Some(existing) => format!("{said}; {existing}"),
                None => said,
            });
        }
        tracing::info!(file = %target.display(), replacement, "filed");
        Ok(Filed {
            entry_id,
            path: target.to_string_lossy().to_string(),
            replacement,
            renamed,
            note,
        })
    }

    /// What the two files at one name say about themselves.
    fn collision(
        &self,
        occupied: &Path,
        arriving: &Path,
        folder: &Path,
        wanted: &str,
    ) -> Result<Collision> {
        let there = read_entry_json(occupied);
        let coming = read_entry_json(arriving);
        let entry_id = self.db.read(|cx| {
            cx.query_one(
                "SELECT id FROM entry WHERE file = ?1",
                [occupied.to_string_lossy().as_ref()],
                |r| r.get::<_, String>(0),
            )
        })?;
        Ok(Collision {
            path: occupied.to_string_lossy().to_string(),
            entry_id,
            same_volume: same_volume(there.as_ref(), coming.as_ref()),
            agrees: agreements(there.as_ref(), coming.as_ref()),
            identical: identical(occupied, arriving),
            occupies: reading_of(there.as_ref()),
            arriving: reading_of(coming.as_ref()),
            would_become: free_name(folder, wanted)
                .file_name()
                .unwrap_or_default()
                .to_string_lossy()
                .to_string(),
        })
    }

    /// A volume arriving past the declared count.
    ///
    /// An ongoing series raises its own count — you do not have to edit the record before
    /// adding the volume that just came out. A completed series asks instead: an extra
    /// volume there is probably a mistake, and quietly moving the number would hide it.
    fn adjust_count(
        &self,
        series_id: &str,
        series: &Series,
        number: Option<f64>,
    ) -> Result<Option<String>> {
        let Some(number) = number.filter(|n| n.fract() == 0.0).map(|n| n as i32) else {
            return Ok(None);
        };
        let Some(declared) = series.volume_count.filter(|d| number > *d) else {
            return Ok(None);
        };
        if series.status.as_deref() == Some("ongoing") {
            self.records().patch_series(
                series_id,
                &crate::api::records::SeriesPatch {
                    volume_count: Some(number),
                    ..Default::default()
                },
            )?;
            Ok(Some(format!(
                "ongoing series: declared count raised from {declared} to {number}"
            )))
        } else {
            Ok(Some(format!(
                "volume {number} goes past the {declared} declared, and the series is not \
                 marked ongoing — worth checking"
            )))
        }
    }

    fn records(&self) -> Records<'_> {
        Records::new(&self.db)
    }

    /// Every file staged and not yet decided on.
    ///
    /// Nothing sweeps these. A proposal that is made and never answered — you closed the
    /// window on the modal — leaves its file in the inbox for good, and the inbox is on the
    /// library's own filesystem, so those are the library's own bytes. Listing them is what
    /// makes that visible instead of a slow leak; deleting them is still yours to ask for,
    /// as everything that removes a file here is.
    pub fn waiting(&self) -> Result<Vec<Waiting>> {
        let mut out = Vec::new();
        let Ok(folders) = std::fs::read_dir(&self.received) else {
            return Ok(out);
        };
        for folder in folders.flatten() {
            let id = folder.file_name().to_string_lossy().to_string();
            let Some(origin) = Origin::of(&id) else {
                continue;
            };
            let Some(file) = std::fs::read_dir(folder.path())
                .ok()
                .and_then(|mut d| d.find_map(|e| e.ok().map(|e| e.path()).filter(|p| p.is_file())))
            else {
                continue;
            };
            let meta = std::fs::metadata(&file)?;
            out.push(Waiting {
                id,
                name: file
                    .file_name()
                    .unwrap_or_default()
                    .to_string_lossy()
                    .to_string(),
                size: meta.len(),
                last_touched_at: modified_at(&meta),
                origin,
                only_copy: origin == Origin::Drop,
            });
        }
        out.sort_by_key(|w| w.last_touched_at);
        Ok(out)
    }

    pub fn abandon(&self, received_id: &str) -> Result<()> {
        if let Some(folder) = self.received_folder(received_id)? {
            let _ = std::fs::remove_dir_all(folder);
        }
        Ok(())
    }

    fn candidate(&self, series_id: &str) -> Result<SeriesCandidate> {
        let found = self.db.read(|cx| {
            cx.query_one(
                "SELECT e.id, w.name AS work, e.name AS edition, u.name AS universe
                 FROM edition e JOIN work w ON w.id = e.work_id
                 LEFT JOIN universe u ON u.id = w.universe_id WHERE e.id = ?1",
                [series_id],
                |r| {
                    let parts: Vec<String> = [
                        r.get::<_, Option<String>>(3)?,
                        Some(r.get::<_, String>(1)?),
                        r.get::<_, Option<String>>(2)?,
                    ]
                    .into_iter()
                    .flatten()
                    .collect();
                    Ok(SeriesCandidate {
                        series_id: r.get(0)?,
                        name: parts.join(" · "),
                    })
                },
            )
        })?;
        Ok(found.unwrap_or_else(|| SeriesCandidate {
            series_id: series_id.to_string(),
            name: series_id.to_string(),
        }))
    }

    fn all_series(&self) -> Result<Vec<SeriesCandidate>> {
        let ids: Vec<String> = self
            .db
            .read(|cx| cx.query("SELECT id FROM edition", [], |r| r.get::<_, String>(0)))?;
        ids.iter().map(|id| self.candidate(id)).collect()
    }

    fn entry_at_number(&self, edition_id: &str, number: f64) -> Result<Option<String>> {
        self.db.read(|cx| {
            cx.query_one(
                "SELECT id FROM entry WHERE edition_id = ?1 AND volume_number = ?2",
                rusqlite::params![edition_id, number],
                |r| r.get::<_, String>(0),
            )
        })
    }

    fn entry_path_within(&self, entry_id: &str, edition_id: &str) -> Result<Option<PathBuf>> {
        Ok(self
            .db
            .read(|cx| {
                cx.query_one(
                    "SELECT file FROM entry WHERE id = ?1 AND edition_id = ?2",
                    [entry_id, edition_id],
                    |r| r.get::<_, String>(0),
                )
            })?
            .map(PathBuf::from))
    }

    fn received_folder(&self, id: &str) -> Result<Option<PathBuf>> {
        if Origin::of(id).is_none() || !id.chars().all(|c| c.is_ascii_alphanumeric() || c == '_') {
            return Err(invalid("invalid id"));
        }
        let folder = self.received.join(id);
        Ok(folder.exists().then_some(folder))
    }
}

/// A file on its way in, and the folder it sits in until something claims it.
///
/// Cleared by its destructor rather than by an error branch, because the branch does not
/// always run: a client that hangs up mid-upload leaves the handler's future dropped at the
/// await it was sitting on, and nothing after that point executes. Only a destructor does.
/// Without this, every interrupted transfer left a folder — and a partial volume — in the
/// inbox for good.
pub struct Staging {
    file: PathBuf,
    keep: bool,
}

impl Staging {
    pub fn path(&self) -> &Path {
        &self.file
    }

    /// The file made it all the way to a proposal: it stays until it is filed or abandoned.
    pub fn keep(mut self) -> PathBuf {
        self.keep = true;
        self.file.clone()
    }
}

impl Drop for Staging {
    fn drop(&mut self) {
        if self.keep {
            return;
        }
        if let Some(folder) = self.file.parent() {
            let _ = std::fs::remove_dir_all(folder);
            tracing::info!(file = %self.file.display(), "upload interrupted, staging cleared");
        }
    }
}

/// What a file says about itself, in the shape that crosses the wire.
fn reading_of(read: Option<&EntryJson>) -> FileReading {
    FileReading {
        work: read.and_then(|r| r.work.clone()),
        edition: read.and_then(|r| r.edition.clone()),
        kind: read.map(|r| r.kind.clone()),
        number: read.and_then(|r| r.number),
        title: read.and_then(|r| r.title.clone()),
        chapter_count: read.map(|r| r.chapters.len()).unwrap_or(0),
    }
}

/// Whether two declarations describe the same volume.
///
/// **The fields that identify, and only those**: work, edition, type, number. Not the
/// title, not the summary, not the chapter markers — those are exactly what comes back
/// changed from a round trip, and a volume that has been retouched is still that volume.
///
/// An `id` settles it outright when both carry one: that is what a download stamps on its
/// way out, and two files carrying different ids are two entries whatever else they agree
/// on.
///
/// Silence is never a match. A file that says nothing about itself cannot be shown to be
/// the same as anything, and the answer to "I cannot tell" is to keep both — not to write
/// one over the other and find out afterwards.
pub fn same_volume(a: Option<&EntryJson>, b: Option<&EntryJson>) -> bool {
    let (Some(a), Some(b)) = (a, b) else {
        return false;
    };
    if let (Some(one), Some(other)) = (&a.id, &b.id) {
        return one == other;
    }
    let named = |value: &Option<String>| {
        value
            .as_deref()
            .map(str::trim)
            .filter(|v| !v.is_empty())
            .map(str::to_lowercase)
    };
    // A work is the least a file has to name to be recognisable at all.
    let (Some(work), Some(other_work)) = (named(&a.work), named(&b.work)) else {
        return false;
    };
    if work != other_work || named(&a.edition) != named(&b.edition) || a.kind != b.kind {
        return false;
    }
    // Two unnumbered volumes in one work are a hors-série and an artbook, not one volume
    // twice.
    matches!((a.number, b.number), (Some(one), Some(other)) if one == other)
}

/// The declared fields two files agree on, named.
///
/// Reported rather than weighed. [`same_volume`] answers the question the server can
/// answer; this is what a person needs in front of them to answer the one it cannot.
fn agreements(a: Option<&EntryJson>, b: Option<&EntryJson>) -> Vec<String> {
    let (Some(a), Some(b)) = (a, b) else {
        return Vec::new();
    };
    let same = |x: &Option<String>, y: &Option<String>| {
        let fold = |v: &Option<String>| {
            v.as_deref()
                .map(str::trim)
                .filter(|s| !s.is_empty())
                .map(str::to_lowercase)
        };
        matches!((fold(x), fold(y)), (Some(one), Some(other)) if one == other)
    };
    let mut out = Vec::new();
    if same(&a.work, &b.work) {
        out.push("work".into());
    }
    if same(&a.edition, &b.edition) {
        out.push("edition".into());
    }
    if a.kind == b.kind {
        out.push("type".into());
    }
    if matches!((a.number, b.number), (Some(one), Some(other)) if one == other) {
        out.push("number".into());
    }
    if same(&a.title, &b.title) {
        out.push("title".into());
    }
    if !a.chapters.is_empty() && a.chapters.len() == b.chapters.len() {
        out.push("chapterCount".into());
    }
    out
}

/// Whether the two files hold the same bytes.
///
/// The size first, which settles almost every case for nothing; the contents only when the
/// sizes match. Two full reads of a volume is a second on an SSD, spent at the one moment
/// someone is about to be asked a question — which is a good moment to spend it.
fn identical(a: &Path, b: &Path) -> bool {
    let size = |p: &Path| std::fs::metadata(p).ok().map(|m| m.len());
    if size(a) != size(b) || size(a).is_none() {
        return false;
    }
    match (digest(a), digest(b)) {
        (Ok(one), Ok(other)) => one == other,
        _ => false,
    }
}

fn digest(file: &Path) -> std::io::Result<[u8; 32]> {
    use sha2::{Digest, Sha256};
    use std::io::Read;
    let mut hasher = Sha256::new();
    let mut source = std::fs::File::open(file)?;
    let mut buffer = vec![0u8; 1 << 16];
    loop {
        let read = source.read(&mut buffer)?;
        if read == 0 {
            break;
        }
        hasher.update(&buffer[..read]);
    }
    Ok(hasher.finalize().into())
}

/// A path in `folder` that no file occupies, starting from `wanted`.
///
/// " (2)", " (3)" and so on, before the extension — the shape a browser uses, because it is
/// the shape a person recognises. Bounded: past a hundred, something else is wrong and
/// hunting for a hundred and first name is not the answer.
fn free_name(folder: &Path, wanted: &str) -> PathBuf {
    let first = folder.join(wanted);
    if !first.exists() {
        return first;
    }
    let path = Path::new(wanted);
    let stem = path
        .file_stem()
        .unwrap_or_default()
        .to_string_lossy()
        .to_string();
    let extension = path
        .extension()
        .map(|e| format!(".{}", e.to_string_lossy()))
        .unwrap_or_default();
    for n in 2..=100 {
        let candidate = folder.join(format!("{stem} ({n}){extension}"));
        if !candidate.exists() {
            return candidate;
        }
    }
    folder.join(format!("{stem} ({}){extension}", token()))
}

/// A rename when both sides share a filesystem, a copy when they do not.
///
/// The copy is the price of getting the inbox wrong, and it is worth saying so: nine
/// gigabytes moved instantly, or nine gigabytes written twice.
pub fn move_or_copy(source: &Path, target: &Path) -> Result<()> {
    if std::fs::rename(source, target).is_ok() {
        return Ok(());
    }
    tracing::warn!("the inbox is not on the library's filesystem — copying instead of renaming");
    std::fs::copy(source, target)?;
    let _ = std::fs::remove_file(source);
    Ok(())
}

/// A file's modification time, in milliseconds.
pub fn modified_at(meta: &std::fs::Metadata) -> i64 {
    meta.modified()
        .ok()
        .and_then(|t| t.duration_since(std::time::UNIX_EPOCH).ok())
        .map(|d| d.as_millis() as i64)
        .unwrap_or(0)
}

pub fn token() -> String {
    // Not a secret, but it must not collide: two uploads landing in the same folder would
    // file each other's bytes.
    let mut bytes = [0u8; 8];
    getrandom(&mut bytes);
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

fn getrandom(buffer: &mut [u8]) {
    use std::io::Read;
    std::fs::File::open("/dev/urandom")
        .and_then(|mut f| f.read_exact(buffer))
        .expect("the system random source");
}

fn short(value: f64) -> String {
    if value.fract() == 0.0 {
        format!("{}", value as i64)
    } else {
        format!("{value}")
    }
}

fn is_zero(value: &usize) -> bool {
    *value == 0
}
