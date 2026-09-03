//! What the API returns.
//!
//! One shape per thing `contract/openapi.yaml` names, and the field names match it exactly
//! — that file is what both clients generate from, so a name that drifts here is a bug in
//! the contract, not a detail.
//!
//! # Defaults do not cross the wire
//!
//! A field left at its default is simply absent. That is not a quirk to tidy up: it is the
//! wire format both clients are written against, and it keeps a shelf of two hundred tiles
//! from carrying two hundred `"missingVolumes": []`.
//!
//! Verified against a running server rather than assumed — an unread series omits
//! `readStatus` entirely, because "UNREAD" *is* the default.

use serde::Serialize;

/// The version of this API. Bumped whenever a change would break a client that has not
/// been rebuilt — a removed field, a renamed route, a changed meaning.
pub const API_VERSION: i32 = 1;

/// The version of the on-disk format this server reads and writes.
pub const FORMAT_VERSION: i32 = 1;

/// UNREAD, and the reason `read_status` is usually absent from a response.
pub const UNREAD: &str = "UNREAD";

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct HealthDto {
    pub status: &'static str,
    pub api: i32,
    pub format: i32,
    pub library: i64,
    #[serde(skip_serializing_if = "is_false")]
    pub local_drop: bool,
}

/// A series is an **edition**: the level that carries entries, chapters and progress.
/// `work_id` is what ties it to its siblings — the other editions of the same story.
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SeriesDto {
    pub id: String,
    pub work_id: String,
    /// Built from the levels that add something: "Terres d'Arran · Elfes".
    pub name: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub universe: Option<String>,
    pub work: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub edition: Option<String>,
    /// The former singular, kept for a client that has not been rebuilt: the writers
    /// joined by ", " when there is more than one, absent when there are none. `authors`
    /// carries the same names separately, and is what a new client should read.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub author: Option<String>,
    /// The writers. Several populate — *Les Terres d'Arran* carries five.
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub authors: Vec<String>,
    /// The illustrators — penciller, inker and cover artist in one.
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub artists: Vec<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub medium: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub reading_direction: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub status: Option<String>,
    /// Computed, never declared. Absent means UNREAD.
    #[serde(skip_serializing_if = "is_unread")]
    pub read_status: String,
    /// What exists out in the world. Declared, not counted.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub declared_volumes: Option<i32>,
    /// What you own. Counted. The two do not say the same thing.
    #[serde(skip_serializing_if = "is_zero")]
    pub owned_volumes: i64,
    /// The gaps in your collection. A volume whose chapters are here under another name is
    /// not one of them.
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub missing_volumes: Vec<f64>,
    /// The gaps in the story itself — the other granularity.
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub missing_chapters: Vec<f64>,
    pub entry_count: i64,
    pub chapter_count: i64,
    pub arc_count: i64,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub genres: Vec<String>,
    /// Beside `genres`, never folded into them.
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub tags: Vec<String>,
    /// A free string, never an enum: "16+" at Kana, "T" elsewhere.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub age_rating: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub publisher: Option<String>,
    /// The publisher's imprint, a sibling of `publisher`.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub collection: Option<String>,
    /// Positive form, never `blackAndWhite`.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub colour: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub language: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub added_at: Option<i64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub last_added_at: Option<i64>,
}

/// A page of series, and how many there are in total.
///
/// The total is the point: without it a client cannot tell a short page from the end of the
/// library, and would either stop early or ask forever.
#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SeriesPageDto {
    pub items: Vec<SeriesDto>,
    pub total: i64,
    pub page: i64,
    pub size: i64,
}

#[derive(Debug, Serialize)]
pub struct FacetDto {
    pub value: String,
    /// How many series carry it — a filter offering a choice that returns nothing is a bug.
    pub count: i64,
}

/// Every value you can actually filter on, with its weight.
#[derive(Debug, Default, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct FacetsDto {
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub read_statuses: Vec<FacetDto>,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub universes: Vec<FacetDto>,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub authors: Vec<FacetDto>,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub genres: Vec<FacetDto>,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub media: Vec<FacetDto>,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub statuses: Vec<FacetDto>,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub languages: Vec<FacetDto>,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub publishers: Vec<FacetDto>,
}

/// A file — a volume, or a chapter that arrived on its own.
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct EntryDto {
    pub id: String,
    #[serde(rename = "type")]
    pub kind: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub number: Option<f64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub title: Option<String>,
    /// What orders it in the edition: the first chapter's number, otherwise the volume
    /// number. Position belongs to the edition; number identifies across editions.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub sort_key: Option<f64>,
    pub page_count: i64,
    pub chapter_count: i64,
    /// The file name alone, never a path.
    pub file: String,
    pub size: i64,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub isbn: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub published_on: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub summary: Option<String>,
    /// Recorded on arrival and never again: a rescan rebuilds everything else, not this.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub added_at: Option<i64>,
    #[serde(skip_serializing_if = "is_false")]
    pub own_cover: bool,
}

/// A marker. It may live inside a volume or be an entry of its own — the numbering is the
/// same either way, which is the whole point of the model.
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ChapterDto {
    pub id: String,
    /// What the file actually said.
    pub raw: String,
    /// What to show.
    pub label: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub number: Option<f64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub title: Option<String>,
    pub kind: String,
    pub position: i64,
    /// Null is common and honest: ComicInfo has no such field, so 3 008 chapters here have
    /// none. A reader must treat it as "I do not know" rather than as zero.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub start_page: Option<i64>,
    pub entry_id: String,
}

/// A range, not a list: four volumes can belong to two arcs, because an arc does not end
/// where a volume ends.
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ArcDto {
    pub id: String,
    pub name: String,
    pub unit: String,
    pub from: f64,
    pub to: f64,
    pub position: i64,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PageDto {
    pub number: i64,
    /// The entry name inside the archive.
    pub name: String,
    pub media_type: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub width: Option<i64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub height: Option<i64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub size: Option<i64>,
    /// Wider than tall: two pages side by side. Fitted to screen width, each half comes out
    /// at half the resolution of a single page, and the reader needs to know.
    #[serde(skip_serializing_if = "is_false")]
    pub spread: bool,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SearchHitDto {
    /// EDITION, ENTRY or CHAPTER — the three things that can be opened. Neither a universe
    /// nor a work appears: both are searched through the editions that carry them.
    pub kind: String,
    pub id: String,
    pub label: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub series_id: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub series_name: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub entry_id: Option<String>,
    /// A guess, offered because nothing matched: "did you mean", not "here it is". It has
    /// to reach the screen as a guess — an approximate hit shown like an exact one costs
    /// more trust than finding nothing ever does.
    #[serde(skip_serializing_if = "is_false")]
    pub approximate: bool,
}

#[derive(Debug, Serialize)]
pub struct ErrorDto {
    pub error: String,
}

impl ErrorDto {
    pub fn new(message: impl Into<String>) -> Self {
        ErrorDto {
            error: message.into(),
        }
    }
}

// --------------------------------------------------------------- the filter

/// What to keep when listing series.
///
/// Several values for one field mean "any of them"; several fields mean "all of them".
/// Asking for two authors widens, asking for an author and a genre narrows — which is what
/// a row of filter chips does, and what anyone expects it to do.
#[derive(Debug, Default, Clone)]
pub struct SeriesFilter {
    /// Specific series, by id — how a single one is fetched without building them all.
    pub ids: Vec<String>,
    /// The works whose editions are wanted — how "the other editions of this one" is asked
    /// for. A work is never a result on its own, so it is a filter and not a level.
    pub works: Vec<String>,
    pub universes: Vec<String>,
    pub authors: Vec<String>,
    pub genres: Vec<String>,
    pub media: Vec<String>,
    pub statuses: Vec<String>,
    /// UNREAD, IN_PROGRESS, READ.
    pub read_statuses: Vec<String>,
    pub languages: Vec<String>,
    pub publishers: Vec<String>,
}

impl SeriesFilter {
    pub fn is_empty(&self) -> bool {
        self.ids.is_empty()
            && self.works.is_empty()
            && self.universes.is_empty()
            && self.authors.is_empty()
            && self.genres.is_empty()
            && self.media.is_empty()
            && self.statuses.is_empty()
            && self.read_statuses.is_empty()
            && self.languages.is_empty()
            && self.publishers.is_empty()
    }

    /// Just this one series, by id.
    pub fn of(id: impl Into<String>) -> Self {
        SeriesFilter {
            ids: vec![id.into()],
            ..Default::default()
        }
    }
}

/// How a list of series is ordered.
///
/// Ordering happens in SQL rather than after the fact, because a page of fifty out of a
/// thousand is only the right fifty if the database did the sorting.
#[derive(Debug, Default, Clone, Copy, PartialEq, Eq)]
pub enum SeriesSort {
    /// Universe, then work, then edition — how a shelf reads.
    #[default]
    Name,
    /// Newest arrivals first.
    Added,
    /// What last received a volume — a series you follow surfaces when it moves.
    Updated,
    /// The longest first.
    Volumes,
}

impl SeriesSort {
    /// Unknown values fall back to `Name` rather than failing: a client sending a sort this
    /// server has not heard of should get a shelf, not an error.
    pub fn of(value: Option<&str>) -> Self {
        match value
            .map(str::trim)
            .unwrap_or_default()
            .to_ascii_lowercase()
            .as_str()
        {
            "added" => SeriesSort::Added,
            "updated" => SeriesSort::Updated,
            "volumes" => SeriesSort::Volumes,
            _ => SeriesSort::Name,
        }
    }

    pub fn sql(self) -> &'static str {
        match self {
            SeriesSort::Name => "COALESCE(u.name, ''), w.name, COALESCE(e.name, '')",
            SeriesSort::Added => "added_at DESC, w.name",
            SeriesSort::Updated => "last_added_at DESC, w.name",
            SeriesSort::Volumes => "entry_count DESC, w.name",
        }
    }
}

fn is_false(value: &bool) -> bool {
    !*value
}

fn is_zero(value: &i64) -> bool {
    *value == 0
}

fn is_unread(value: &str) -> bool {
    value == UNREAD
}
