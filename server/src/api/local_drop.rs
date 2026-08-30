//! The short path, for when the application and the server share a machine.
//!
//! Sending nine gigabytes through the loopback works — it is fast — but the server then
//! writes a second copy of bytes that were already on the disk, and the disk is the thing
//! that is actually scarce. A shared folder skips that entirely: the application puts the
//! file down, the server renames it into the library, and nothing is copied at all.
//!
//! It changes only how the bytes arrive. Everything after — reading entry.json, proposing a
//! destination, waiting for a confirmation — is the same path as an upload, because it is
//! the same code.
//!
//! The folder has to be inside the mount the server already holds, or a rename becomes a
//! copy again and the point is lost.

use std::path::{Path, PathBuf};

use anyhow::Result;

use crate::api::{absent, invalid};
use serde::{Deserialize, Serialize};

use crate::api::intake::{move_or_copy, Intake, Origin, Proposal};

#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct DropRequest {
    /// A plain file name, already sitting in the drop folder.
    pub name: String,
    /// Whether the file may be consumed where it lies. True moves it — instant on the same
    /// filesystem, and the point of the whole thing. False copies and leaves yours alone.
    #[serde(default = "yes")]
    pub consume: bool,
}

fn yes() -> bool {
    true
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DropListing {
    pub enabled: bool,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub files: Vec<DropFile>,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DropFile {
    pub name: String,
    pub size: u64,
}

pub struct LocalDrop {
    folder: Option<PathBuf>,
    intake: std::sync::Arc<Intake>,
}

impl LocalDrop {
    pub fn new(folder: Option<PathBuf>, intake: std::sync::Arc<Intake>) -> Self {
        LocalDrop { folder, intake }
    }

    pub fn enabled(&self) -> bool {
        self.folder.is_some()
    }

    pub fn list(&self) -> DropListing {
        let Some(here) = &self.folder else {
            return DropListing {
                enabled: false,
                files: Vec::new(),
            };
        };
        let mut files: Vec<DropFile> = std::fs::read_dir(here)
            .into_iter()
            .flatten()
            .flatten()
            .filter_map(|e| {
                let path = e.path();
                let meta = std::fs::metadata(&path).ok()?;
                let name = path.file_name()?.to_string_lossy().to_string();
                meta.is_file().then_some(DropFile {
                    name,
                    size: meta.len(),
                })
            })
            .collect();
        files.sort_by(|a, b| a.name.cmp(&b.name));
        DropListing {
            enabled: true,
            files,
        }
    }

    /// Takes a file from the drop and hands it to the ordinary intake, which then proposes
    /// where it should go. Nothing is filed here — that still needs a confirmation.
    pub fn receive(&self, request: &DropRequest) -> Result<Proposal> {
        let here = self
            .folder
            .as_ref()
            .ok_or_else(|| invalid("the local drop is not configured"))?;
        let name = Path::new(&request.name)
            .file_name()
            .map(|n| n.to_string_lossy().to_string())
            .unwrap_or_default();
        if name.trim().is_empty() || name.contains("..") {
            return Err(invalid(format!("invalid file name: {}", request.name)));
        }

        let source = here.join(&name);
        if !source.starts_with(here) {
            return Err(invalid(format!("file outside the drop: {}", request.name)));
        }
        if !source.is_file() {
            return Err(absent(format!("no such file in the drop: {name}")));
        }

        // Said, and remembered: with `consume` the file has left your folder, so the staged
        // copy is the only one. What is listed as waiting says so, because abandoning it is
        // not the same decision as abandoning something you still have.
        let origin = if request.consume {
            Origin::Drop
        } else {
            Origin::Upload
        };
        let staged = self.intake.staging_from(&name, origin)?;
        if request.consume {
            move_or_copy(&source, staged.path())?;
        } else {
            std::fs::copy(&source, staged.path())?;
        }

        tracing::info!(
            name,
            size = std::fs::metadata(staged.path())
                .map(|m| m.len())
                .unwrap_or(0),
            how = if request.consume { "moved" } else { "copied" },
            "taken from the drop"
        );
        let proposal = self.intake.propose_for(staged.path())?;
        staged.keep();
        Ok(proposal)
    }
}
