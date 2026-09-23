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
    /// Which of the orders below a reader is offered first, by `id`. Optional: a universe
    /// with one order does not need to say which.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub default_order: Option<String>,
    /// Several named ways through the same universe. Kept in the order they are written:
    /// the file is the author's, and the first one is the one they put first.
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub orders: Vec<OrderJson>,
}

/// One named way through a universe.
///
/// Steps and not a flat list of editions: the same work appears more than once in most
/// reading orders worth writing down — « work A, part 1; work B; work A, part 2 » — and a
/// list of editions cannot say that.
#[derive(Debug, Clone, Default, Deserialize, Serialize)]
#[serde(default, rename_all = "camelCase")]
pub struct OrderJson {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub id: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub name: Option<String>,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub steps: Vec<StepJson>,
}

/// A stretch of one work, read at this point of the order.
///
/// `work` is the work folder's path relative to the universe, never its display title: a
/// title is edited in a sidecar and an order written against it would break silently, where
/// a folder that moves is a folder somebody moved.
#[derive(Debug, Clone, Default, Deserialize, Serialize)]
#[serde(default, rename_all = "camelCase")]
pub struct StepJson {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub work: Option<String>,
    /// `CHAPTER` or `VOLUME`. Absent means the whole work, which is the common case and the
    /// one worth writing shortest.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub unit: Option<String>,
    /// Required of a `VOLUME` step and refused on a `CHAPTER` one: « volumes 1 to 7 »
    /// describes different content in a 42-volume edition and a 34-volume one, while a
    /// chapter number identifies the same story across both.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub edition: Option<String>,
    /// Inclusive, and open at either end: no `from` means the beginning, no `to` means the
    /// current end and whatever is added after it.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub from: Option<f64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub to: Option<f64>,
}

/// The two units a step can be measured in. A third would be a third kind of range, so this
/// is an enumeration and not a free string — unlike a status, which a library may spell in
/// a way this server has not been taught.
pub const UNITS: [&str; 2] = ["CHAPTER", "VOLUME"];

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

/// A book that is a whole book: an album that is its own work, its own edition and its own
/// entry at once.
///
/// **Inside the CBZ, beside `entry.json`, and its presence is the declaration.** No count
/// separates an album standing alone from a series still running of which you happen to own
/// one volume — they are the same rows. Only somebody saying so tells them apart, which is
/// what every other level of this format already works by.
///
/// It carries what `work.json`, `edition.json` and `entry.json` carry together, because a
/// one-shot has none of the three above it to hold them. That is the same reasoning the
/// format already applies one level up, where a single-edition work keeps its edition fields
/// in `work.json` for want of an edition folder.
///
/// An archive carrying this is lifted out of the work whose folder it sits in: two albums
/// dropped side by side in `BD/` are two books, and not a two-volume series called `BD`.
#[derive(Debug, Clone, Default, Deserialize, Serialize)]
#[serde(default, rename_all = "camelCase")]
pub struct OneShotJson {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub leaf: Option<i32>,
    /// What the book is called. Absent falls back to the file's own name, the way a work
    /// falls back to its folder's.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub title: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub medium: Option<String>,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub authors: Vec<String>,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub artists: Vec<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub publisher: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub collection: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub language: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub reading_direction: Option<String>,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub genres: Vec<String>,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub tags: Vec<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub summary: Option<String>,
    /// A free string, never an enum: "16+" at Kana, "T" elsewhere.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub age_rating: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub colour: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub isbn: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub published_on: Option<String>,
}

/// There is no `status` and no `volumeCount` here, and that is the point: a book that is
/// whole has nothing to announce and nothing left to wait for. A one-shot is `completed`
/// with one volume by being a one-shot, so asking a file to repeat it would be asking for a
/// fact that can disagree with itself.
impl OneShotJson {
    /// The writers, and nothing legacy to resolve: this file was born after `authors`.
    pub fn authors(&self) -> Vec<String> {
        self.authors.clone()
    }
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
///
/// For a file this server owns end to end — an `entry.json` it wrote itself into an archive.
/// A sidecar a person may have edited goes through [`Document`] instead, which is the whole
/// point of that type.
pub fn write<T: Serialize>(value: &T) -> Result<Vec<u8>, serde_json::Error> {
    serde_json::to_vec_pretty(value)
}

/// A sidecar as what it is on disk: a JSON document, of which this server understands some
/// fields and a library may hold others.
///
/// **Serialising a type back over a file loses every field that type does not know.**
/// Measured on a `work.json` carrying one hand-written key:
///
/// ```text
/// before  {"leaf":1,"title":"Death Note","myField":"kept?","status":"completed"}
/// after   {"leaf":1,"title":"Death Note","status":"completed"}
/// ```
///
/// Which is not a risk for later: `PATCH /series/{id}` went through that path, so correcting
/// a title from a client quietly threw away anything a human — or a later version of Leaf —
/// had put beside it in the same file.
///
/// So a sidecar is edited as a document: the typed view is read out of it, changed, and
/// written back into it. What this version has never heard of is copied through untouched,
/// and stays where its author put it.
#[derive(Debug, Clone, Default)]
pub struct Document(serde_json::Map<String, serde_json::Value>);

impl Document {
    /// What a file holds. Anything that is not a JSON object — an array, a number, a stray
    /// comma — is an empty document, which is exactly what [`read`] already made of it.
    ///
    /// The right read for a **reader**: [`Document::read`]'s callers default whatever is
    /// missing, and unreadable bytes are nothing worse than bytes with nothing in them. The
    /// wrong one for a **writer** — see [`Document::parse`], which is what a caller about to
    /// write has to ask instead.
    pub fn of(bytes: &[u8]) -> Self {
        match serde_json::from_slice(bytes) {
            Ok(serde_json::Value::Object(map)) => Self(map),
            _ => Self::default(),
        }
    }

    /// What a file holds, or why it could not be read as this format's shape at all.
    ///
    /// `Err` on anything [`Document::of`] would quietly turn into an empty document: invalid
    /// JSON, or valid JSON that is not an object. The distinction matters to exactly one
    /// caller — `scan::identity::of` — which stamps an identifier onto whatever this returns
    /// and writes it back. Fed [`Document::of`]'s emptiness, it wrote `{"id":…,"leaf":1}`
    /// over a hand-written `work.json` that had a trailing comma: title, authors, genres,
    /// gone, and the report said nothing, because emptiness read as "nothing was here" is
    /// indistinguishable from emptiness that means "this could not be read". `parse` keeps
    /// the two apart so the caller can refuse to write instead.
    pub fn parse(bytes: &[u8]) -> Result<Self, String> {
        match serde_json::from_slice::<serde_json::Value>(bytes) {
            Ok(serde_json::Value::Object(map)) => Ok(Self(map)),
            Ok(_) => Err("not a JSON object".to_string()),
            Err(e) => Err(e.to_string()),
        }
    }

    /// The typed view, defaulted wherever the document says nothing.
    pub fn read<T: Default + serde::de::DeserializeOwned>(&self) -> T {
        serde_json::from_value(serde_json::Value::Object(self.0.clone())).unwrap_or_default()
    }

    /// Every key it holds, in the order the file wrote them.
    pub fn keys(&self) -> Vec<String> {
        self.0.keys().cloned().collect()
    }

    /// What it holds under `key`, whatever the shape — for comparing two documents without
    /// a type standing between them, which is the only way to compare fields no type knows.
    pub fn value(&self, key: &str) -> Option<&serde_json::Value> {
        self.0.get(key)
    }

    /// Whether the document holds that key at all — asked of a key this server may not have
    /// a field for.
    pub fn has(&self, key: &str) -> bool {
        self.0.contains_key(key)
    }

    /// What the document holds under `key`, when it is a string. Asked of a key this server
    /// reads without owning a field for it.
    pub fn text(&self, key: &str) -> Option<&str> {
        self.0.get(key)?.as_str()
    }

    /// Sets one key, leaving the rest of the document alone. An existing key keeps its place
    /// in the file; a new one goes at the end.
    pub fn set(&mut self, key: &str, value: serde_json::Value) {
        self.0.insert(key.to_string(), value);
    }

    /// The document's own bytes, for a change made key by key rather than through a type.
    pub fn bytes(&self) -> Result<Vec<u8>, serde_json::Error> {
        serde_json::to_vec_pretty(&serde_json::Value::Object(self.0.clone()))
    }

    /// The bytes to write: this document with `value`'s fields set into it.
    ///
    /// A key `T` used to produce and no longer does is removed — a field cleared has to
    /// clear — and every key `T` knows nothing about is left exactly where it was found.
    ///
    /// Only what disappeared is removed, never all of `T`'s keys and then back: emptying the
    /// type's fields and appending them again pushes every key this server does not know to
    /// the front of the file, which is a diff on a line nobody touched.
    pub fn written<T>(&self, value: &T) -> Result<Vec<u8>, serde_json::Error>
    where
        T: Default + serde::de::DeserializeOwned + Serialize,
    {
        let now = fields_of(value)?;
        let mut whole = self.0.clone();
        for key in fields_of(&self.read::<T>())?.keys() {
            if !now.contains_key(key) {
                whole.shift_remove(key);
            }
        }
        for (key, one) in now {
            whole.insert(key, one);
        }
        serde_json::to_vec_pretty(&serde_json::Value::Object(whole))
    }
}

/// The keys a value serialises to, which is how the fields a type owns are known without
/// listing them anywhere. A type that is not an object owns none.
fn fields_of<T: Serialize>(
    value: &T,
) -> Result<serde_json::Map<String, serde_json::Value>, serde_json::Error> {
    match serde_json::to_value(value)? {
        serde_json::Value::Object(map) => Ok(map),
        _ => Ok(serde_json::Map::new()),
    }
}
