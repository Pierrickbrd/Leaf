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
    /// The former singular, kept so a file written before `authors` existed keeps reading
    /// the same author it always did — see [`WorkJson::authors`].
    #[serde(skip_serializing_if = "Option::is_none")]
    pub author: Option<String>,
    /// The writers. Several populate: *Les Terres d'Arran* carries five of them, *Blake et
    /// Mortimer* six.
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub authors: Vec<String>,
    /// The illustrators — penciller, inker and cover artist in one, because in the 25
    /// series this was measured against the three are the same person without exception.
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub artists: Vec<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub status: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub reading_direction: Option<String>,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub genres: Vec<String>,
    /// Beside `genres`, never folded into them: measured, *Les Terres d'Arran* carries one
    /// genre and seven tags with nothing in common between the two lists.
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub tags: Vec<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub summary: Option<String>,
    /// A free string, never an enum: "16+" at Kana, "T" elsewhere — the vocabulary is a
    /// publisher's, not this format's to constrain.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub age_rating: Option<String>,
    // A single-edition work has no edition folder to hold these: they live here and act as
    // defaults for its editions.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub publisher: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub collection: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub colour: Option<bool>,
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

impl WorkJson {
    /// The writers, resolved. `authors` when it says something; otherwise a one-element list
    /// built from the legacy singular `author`, so a file written before `authors` existed
    /// is read exactly as it always was — one name, the name it already had.
    pub fn authors(&self) -> Vec<String> {
        if !self.authors.is_empty() {
            self.authors.clone()
        } else {
            self.author.iter().cloned().collect()
        }
    }
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
    /// The publisher's imprint — "Dark Kana" — a sibling of `publisher`, not a replacement
    /// for it.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub collection: Option<String>,
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
    /// Positive form, never `blackAndWhite`: a page carries colour or it does not, and a
    /// double negative is not a fact worth asking a file to hold.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub colour: Option<bool>,
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

/// The two units an arc can be counted in, and nothing else.
///
/// The index has the last word — `CHECK (unit IN ('CHAPTER','VOLUME'))` — and it is a bad
/// place to find out: the insert fails inside the transaction that holds a whole shelf, so
/// one word in one edition.json stopped that shelf from ever being indexed again, and with
/// it the prune that only runs after a complete sweep. Asked here instead, where a file
/// saying something else can be reported rather than obeyed.
///
/// Case is not vocabulary: `volume` is the same unit as `VOLUME`, spelled by a person.
pub fn arc_unit(spelled: &str) -> Option<&'static str> {
    match spelled.trim().to_uppercase().as_str() {
        "CHAPTER" => Some("CHAPTER"),
        "VOLUME" => Some("VOLUME"),
        _ => None,
    }
}

/// The words the contract has for a medium, a status and a reading direction.
///
/// Public so that a refusal can name them: a caller told only that its word was wrong has to
/// go and find the contract, and a message that lists the vocabulary is the contract at the
/// one moment it is being read.
pub const MEDIA: [&str; 8] = [
    "manga", "bd", "comics", "manhwa", "manhua", "webtoon", "artbook", "other",
];
pub const STATUSES: [&str; 2] = ["ongoing", "completed"];
pub const READING_DIRECTIONS: [&str; 3] = ["LEFT_TO_RIGHT", "RIGHT_TO_LEFT", "VERTICAL"];

/// The same rule as [`arc_unit`], for the three enums beside it: **what may be written is
/// what may be served.**
///
/// A patch wrote these three straight into a sidecar, and the scanner then indexed whatever
/// word it found. `{"status": "hiatus"}` was answered 200, went into work.json, and came
/// back out of `GET /series` against an enum of two words — with `intake` reading it as "not
/// ongoing" and filing an extra volume as though the series were finished, and a client that
/// maps a word it does not know to nothing showing the field empty, which reads as an edit
/// that did nothing at all.
///
/// Case is not vocabulary, as above: `Manga` is somebody meaning `manga`, and what goes into
/// the file is the contract's spelling either way.
pub fn medium(spelled: &str) -> Option<&'static str> {
    one_of(spelled, &MEDIA)
}

pub fn status(spelled: &str) -> Option<&'static str> {
    one_of(spelled, &STATUSES)
}

pub fn reading_direction(spelled: &str) -> Option<&'static str> {
    one_of(spelled, &READING_DIRECTIONS)
}

fn one_of(spelled: &str, vocabulary: &[&'static str]) -> Option<&'static str> {
    let spelled = spelled.trim();
    vocabulary
        .iter()
        .copied()
        .find(|word| word.eq_ignore_ascii_case(spelled))
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
