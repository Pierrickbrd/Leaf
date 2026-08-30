//! The files that describe what is on the disk.
//!
//! Four of them, one per level, and each one optional. A library with none of them still
//! reads: the folder names carry the structure, and everything below is what the files add
//! when they are there.
//!
//! `serde` is asked to be lenient about what it does not know: a sidecar written by a later
//! version of Leaf must not stop this one from reading a library.

use serde::{Deserialize, Serialize};

/// The version of the on-disk format this server reads and writes.
pub const FORMAT_VERSION: i32 = 1;

#[derive(Debug, Clone, Default, Deserialize, Serialize)]
#[serde(default, rename_all = "camelCase")]
pub struct WorkJson {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub leaf: Option<i32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub title: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub medium: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub author: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub status: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub reading_direction: Option<String>,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub genres: Vec<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub summary: Option<String>,
    // A single-edition work has no edition folder to hold these: they live here and act as
    // defaults for its editions.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub publisher: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub volume_count: Option<i32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub format: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub language: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub chapter_label: Option<String>,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub arcs: Vec<ArcJson>,
}

#[derive(Debug, Clone, Default, Deserialize, Serialize)]
#[serde(default, rename_all = "camelCase")]
pub struct UniverseJson {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub leaf: Option<i32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub name: Option<String>,
}

#[derive(Debug, Clone, Default, Deserialize, Serialize)]
#[serde(default, rename_all = "camelCase")]
pub struct EditionJson {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub leaf: Option<i32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub name: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub medium: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub publisher: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub reading_direction: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub status: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub volume_count: Option<i32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub format: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub language: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub chapter_label: Option<String>,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub arcs: Vec<ArcJson>,
}

/// A range, not a list: four Haikyū volumes belong to two arcs, because an arc does not end
/// where a volume ends.
#[derive(Debug, Clone, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ArcJson {
    pub name: String,
    #[serde(default = "chapter_unit", skip_serializing_if = "is_chapter")]
    pub unit: String,
    pub from: f64,
    pub to: f64,
}

fn chapter_unit() -> String {
    "CHAPTER".to_string()
}

fn is_chapter(unit: &str) -> bool {
    unit == "CHAPTER"
}

/// Inside the CBZ, in place of ComicInfo.xml. One file name for both materialisations: a
/// volume and a loose chapter are the same kind of thing to a reader, and the type field is
/// what tells them apart.
#[derive(Debug, Clone, Deserialize, Serialize)]
#[serde(default, rename_all = "camelCase")]
pub struct EntryJson {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub leaf: Option<i32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub work: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub edition: Option<String>,
    #[serde(
        rename = "type",
        default = "volume_type",
        skip_serializing_if = "is_volume"
    )]
    pub kind: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub number: Option<f64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub id: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub title: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub isbn: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub published_on: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub summary: Option<String>,
    /// Which volume a loose chapter came from.
    ///
    /// A story can reach you as volumes, as loose chapters, or as both in turn. Saying which
    /// volume a loose chapter came from is what keeps the server from reporting that volume
    /// as missing: it does not have the file, but it has the content, and the content is
    /// what you would be missing.
    ///
    /// Not to be confused with the entry's own `number`: on a CHAPTER entry that one is the
    /// chapter's number.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub volume: Option<f64>,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub chapters: Vec<ChapterJson>,
}

fn volume_type() -> String {
    "VOLUME".to_string()
}

fn is_volume(kind: &str) -> bool {
    kind == "VOLUME"
}

/// Written by hand rather than derived, so that `EntryJson::default()` and the entry serde
/// builds from `{}` are the same value. A derived `Default` would leave `type` empty, and
/// the sidecar that a stamp writes onto a file with no metadata would then claim the entry
/// is of no kind at all.
impl Default for EntryJson {
    fn default() -> Self {
        EntryJson {
            leaf: None,
            work: None,
            edition: None,
            kind: volume_type(),
            number: None,
            id: None,
            title: None,
            isbn: None,
            published_on: None,
            summary: None,
            volume: None,
            chapters: Vec::new(),
        }
    }
}

#[derive(Debug, Clone, Default, Deserialize, Serialize)]
#[serde(default, rename_all = "camelCase")]
pub struct ChapterJson {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub raw: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub number: Option<f64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub title: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub start_page: Option<i32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub after: Option<f64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub volume: Option<f64>,
    /// Escapes the edition's pattern for this one chapter. Three cases, and this single
    /// field covers them: absent, the pattern composes it; a string, that string is used;
    /// **an empty string, nothing is displayed and the title stands alone** — which is what
    /// a specially-named bonus wants.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub label: Option<String>,
}

/// Reads a sidecar, tolerating anything it does not understand.
///
/// A file that cannot be parsed is not a reason to fail a scan: it is one folder described
/// less well than it could be, and the report says so. Refusing to index a library because
/// one JSON file has a trailing comma would be the wrong trade every time.
pub fn read<T: serde::de::DeserializeOwned>(bytes: &[u8]) -> Option<T> {
    serde_json::from_slice(bytes).ok()
}

/// Writes one back, in the shape the format documents.
pub fn write<T: Serialize>(value: &T) -> Result<Vec<u8>, serde_json::Error> {
    serde_json::to_vec_pretty(value)
}
