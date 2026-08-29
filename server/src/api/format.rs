//! The rules of the on-disk format, as data.
//!
//! Both clients have to tell somebody where to put their files and what may be written in
//! them, and neither can be trusted to remember: these rules changed three times in one
//! afternoon. A page written into an application drifts from the server the first time the
//! scanner learns something, and drifts silently, because nothing compares the two.
//!
//! So the server says them, the depth limits come from the constants the walk actually uses,
//! and **every rule here has a test that builds the shape it describes and checks the
//! outcome**. A rule that stops being true stops compiling or stops passing.

use serde::Serialize;

use crate::metadata::sidecars::FORMAT_VERSION;
use crate::scan::layout;

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Format {
    /// The version of the on-disk format this server reads and writes, and what a `leaf`
    /// marker in a sidecar is compared against.
    pub format: i32,
    pub sidecars: Vec<Sidecar>,
    pub folders: Vec<Rule>,
    pub limits: Limits,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Sidecar {
    /// The file's name, which is fixed.
    pub file: String,
    /// Where it goes.
    pub place: String,
    /// Every field it may carry, in the spelling it takes on disk.
    pub fields: Vec<String>,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Rule {
    /// What the folder holds.
    pub holds: String,
    /// What the library makes of it: UNIVERSE, WORK, or SHELF for a folder that is not a
    /// level at all.
    pub becomes: String,
    pub because: String,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Limits {
    /// Folders below a declared level: universe, work, edition. Deeper is not read.
    pub model_depth: usize,
    /// Folders that declare nothing may be stacked this many deep before the walk stops.
    pub max_shelves: usize,
}

pub fn describe() -> Format {
    Format {
        format: FORMAT_VERSION,
        sidecars: vec![
            Sidecar {
                file: layout::UNIVERSE_FILE.into(),
                place: "in the folder".into(),
                fields: fields_of(&filled::universe()),
            },
            Sidecar {
                file: layout::WORK_FILE.into(),
                place: "in the folder".into(),
                fields: fields_of(&filled::work()),
            },
            Sidecar {
                file: layout::EDITION_FILE.into(),
                place: "in the folder".into(),
                fields: fields_of(&filled::edition()),
            },
            Sidecar {
                file: crate::scan::scanner::ENTRY_JSON.into(),
                place: "inside the archive".into(),
                fields: fields_of(&filled::entry()),
            },
            Sidecar {
                file: "chapters[]".into(),
                place: "inside entry.json".into(),
                fields: fields_of(&filled::chapter()),
            },
            Sidecar {
                file: "arcs[]".into(),
                place: "inside work.json or edition.json".into(),
                fields: fields_of(&filled::arc()),
            },
        ],
        folders: vec![
            Rule {
                holds: format!("{}, and folders", layout::UNIVERSE_FILE),
                becomes: "UNIVERSE".into(),
                because: "it says it is one, and only that makes one".into(),
            },
            Rule {
                holds: format!("{}, and archives", layout::WORK_FILE),
                becomes: "WORK".into(),
                because: "the archives beside it are its edition, which has no name".into(),
            },
            Rule {
                holds: format!("{}, and folders of archives", layout::WORK_FILE),
                becomes: "WORK".into(),
                because: "each folder is one of its editions, named after the folder".into(),
            },
            Rule {
                holds: "archives, and no sidecar".into(),
                becomes: "WORK".into(),
                because: "nothing but a work puts volumes in a folder — evidence, not a guess"
                    .into(),
            },
            Rule {
                holds: "folders, and no sidecar".into(),
                becomes: "SHELF".into(),
                because: "a folder made to tidy up is not a level: it is walked through, and \
                          what is inside says what it is"
                    .into(),
            },
        ],
        limits: Limits {
            model_depth: layout::MODEL_DEPTH,
            max_shelves: layout::MAX_SHELVES,
        },
    }
}

/// The field names a sidecar serialises to, read off the type rather than typed out beside
/// it. A field renamed in the struct is renamed here.
fn fields_of<T: Serialize>(value: &T) -> Vec<String> {
    match serde_json::to_value(value) {
        Ok(serde_json::Value::Object(map)) => map.keys().cloned().collect(),
        _ => Vec::new(),
    }
}

/// One of each, with **every** field set.
///
/// Every field, because a sidecar skips what is empty: a default value serialises to `{}`
/// and would advertise a format with no fields at all.
///
/// And written as exhaustive literals — no `..Default::default()` anywhere below. That is
/// the point of them: a field added to a sidecar and forgotten here does not quietly go
/// missing from what the server tells the applications the format is. It fails to compile.
mod filled {
    use crate::metadata::sidecars::*;

    pub fn universe() -> UniverseJson {
        UniverseJson {
            leaf: Some(FORMAT_VERSION),
            name: Some(String::new()),
        }
    }

    pub fn work() -> WorkJson {
        WorkJson {
            leaf: Some(FORMAT_VERSION),
            title: Some(String::new()),
            medium: Some(String::new()),
            author: Some(String::new()),
            status: Some(String::new()),
            reading_direction: Some(String::new()),
            genres: vec![String::new()],
            summary: Some(String::new()),
            publisher: Some(String::new()),
            volume_count: Some(0),
            format: Some(String::new()),
            language: Some(String::new()),
            chapter_label: Some(String::new()),
            arcs: vec![arc()],
        }
    }

    pub fn edition() -> EditionJson {
        EditionJson {
            leaf: Some(FORMAT_VERSION),
            name: Some(String::new()),
            medium: Some(String::new()),
            publisher: Some(String::new()),
            reading_direction: Some(String::new()),
            status: Some(String::new()),
            volume_count: Some(0),
            format: Some(String::new()),
            language: Some(String::new()),
            chapter_label: Some(String::new()),
            arcs: vec![arc()],
        }
    }

    pub fn entry() -> EntryJson {
        EntryJson {
            leaf: Some(FORMAT_VERSION),
            work: Some(String::new()),
            edition: Some(String::new()),
            // Not the default, or `skip_serializing_if` drops it from the list.
            kind: "CHAPTER".to_string(),
            number: Some(0.0),
            id: Some(String::new()),
            title: Some(String::new()),
            isbn: Some(String::new()),
            published_on: Some(String::new()),
            summary: Some(String::new()),
            volume: Some(0.0),
            chapters: vec![chapter()],
        }
    }

    pub fn chapter() -> ChapterJson {
        ChapterJson {
            raw: Some(String::new()),
            number: Some(0.0),
            title: Some(String::new()),
            start_page: Some(0),
            after: Some(0.0),
            volume: Some(0.0),
            label: Some(String::new()),
        }
    }

    pub fn arc() -> ArcJson {
        ArcJson {
            name: String::new(),
            // Not CHAPTER, for the same reason.
            unit: "VOLUME".to_string(),
            from: 0.0,
            to: 0.0,
        }
    }
}
