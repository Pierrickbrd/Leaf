//! What a reimport brings, matched to what the library holds.
//!
//! A file arriving for a volume the library holds lands on **that volume's file name**: an
//! entry is a file in an edition, so landing on the same name keeps the entry, and with it
//! every reader's place, the read marks and the date it arrived. That makes the match the
//! dangerous part. A wrong match is not a wrong label, it is the wrong volume overwritten —
//! so a volume is matched by its kind and its exact number, never by its name or its size,
//! and anything doubtful matches nothing: an addition is undone by deleting a file, a
//! replacement of the wrong volume is not undone at all.

use std::collections::{HashMap, HashSet};
use std::path::Path;

use anyhow::{anyhow, Result};
use serde::{Deserialize, Serialize};
use unicode_normalization::UnicodeNormalization;

use crate::api::intake::token;
use crate::api::invalid;
use crate::store::held::{Held, HeldChapter, Titled};
use crate::store::text::is_combining_mark;

/// What a reimport is aimed at. `SERIES` is an edition, as everywhere in the API.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Deserialize, Serialize)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum TargetKind {
    Entry,
    Series,
    Universe,
    Chapter,
    /// New editions of the series: every folder dropped is an edition of its own, filed beside
    /// the ones it holds. `id` is any edition of the series.
    SeriesNewEditions,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ImportTarget {
    pub kind: TargetKind,
    pub id: String,
}

/// The target as the answer names it, with the way down to it from its universe — the
/// rail draws one line per step, the target last.
#[derive(Debug, Clone, PartialEq, Eq, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct TargetPlace {
    pub kind: TargetKind,
    pub id: String,
    pub name: String,
    pub trail: Vec<TrailStep>,
}

/// One level from the universe down to the target.
#[derive(Debug, Clone, PartialEq, Eq, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct TrailStep {
    /// `UNIVERSE`, `WORK`, `SERIES`, `ENTRY` or `CHAPTER`.
    pub kind: String,
    pub id: String,
    pub name: String,
    /// What the level holds: works, editions, volumes, pages.
    pub count: u32,
}

/// What the client read in a file, after the corrections a person made: the number and kind
/// it will carry once installed, which is what it is matched on.
#[derive(Debug, Clone, Default, PartialEq, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct FoundInFile {
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub number: Option<f64>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub kind: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub title: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub series: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub pages: Option<u32>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub chapters: Vec<f64>,
    /// The number was read off the file's name, not declared in it nor confirmed by a
    /// person. Such a number is shown, never landed on.
    #[serde(default, skip_serializing_if = "std::ops::Not::not")]
    pub guessed: bool,
}

/// A range of pages, counted from 0 as `/pages/{number}` counts them.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Splice {
    pub from: u32,
    pub to: u32,
}

/// The longest range of pages a splice takes. Far past any printed volume — the thickest
/// manga omnibus runs to about 1 500 — and a bound on everything the announcement works out
/// from a number the client chose, before a file has arrived to say how many pages it has.
pub const MAX_SPLICE: u32 = 10_000;

/// What becomes of one chapter of the volume a splice rewrites.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum SpliceKind {
    /// Before the range, or after it when the range keeps its length: the same pages.
    Keep,
    /// The chapter aimed at: its pages are the ones taken.
    Replace,
    /// After the range: the same pages, moved by what the range gained or lost.
    Shift,
}

/// One chapter of the volume, where it is and where it will be — counted from 0, both ends
/// included.
#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ChapterSplice {
    pub id: String,
    pub number: Option<f64>,
    pub title: Option<String>,
    pub from: i64,
    pub to: i64,
    pub becomes_from: i64,
    pub becomes_to: i64,
    pub outcome: SpliceKind,
}

/// What a chapter reimport does to its volume, said before a byte moves.
#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SpliceOutcome {
    pub pages_before: i64,
    pub pages_after: i64,
    /// Every chapter that says where it starts, in reading order.
    pub chapters: Vec<ChapterSplice>,
    /// The asking profile's place in the volume, and where it will be.
    pub page: Option<i64>,
    pub page_becomes: Option<i64>,
}

/// The pages of the volume a splice replaces, both ends included: from the chapter's first
/// page to the page before the next chapter's, or to the last.
pub type Cut = (u32, u32);

/// Where page `page` of the volume goes once `cut` becomes `cut.0..=new_to`: moved by the
/// difference after the range, kept inside it while it still fits and brought to its new
/// end if not. The rule `replaced::Positions::Spliced` applies to readers' places, so a
/// chapter start, the cover and a reader's place all move the same way.
pub fn moved_page(page: i64, cut: Cut, new_to: i64) -> i64 {
    let (from, old_to) = (i64::from(cut.0), i64::from(cut.1));
    if page > old_to {
        page + (new_to - old_to)
    } else if page >= from {
        page.min(new_to)
    } else {
        page
    }
}

/// The pages of a volume of `pages` pages that the chapter at `aimed` covers, from the
/// first page of each chapter in reading order (`starts`): from its own to the page before
/// the next one's, or to the last.
///
/// Read the same way from the index at the announcement and from the volume's own
/// `entry.json` at the commit, so the two can be compared: a volume replaced and rescanned
/// in between, whose chapters now start two pages later, would otherwise have lost two
/// pages of the chapter before.
///
/// Refused when a chapter that does not say its first page follows the one aimed at before
/// the next that does: its pages are somewhere in the range, and replacing the range would
/// replace them with it — chapter 6.5 gone with chapter 6, which nobody asked for.
pub fn chapter_cut(starts: &[Option<i64>], aimed: usize, pages: u32) -> Result<Cut> {
    let start = starts
        .get(aimed)
        .copied()
        .flatten()
        .and_then(|s| u32::try_from(s).ok())
        .filter(|s| *s < pages)
        .ok_or_else(|| invalid("the chapter aimed at does not say which page it starts on"))?;
    let within = |s: &i64| u32::try_from(*s).ok().filter(|s| *s < pages);
    for following in &starts[aimed + 1..] {
        match following.as_ref().and_then(within) {
            Some(next) if next > start => break,
            Some(_) => {}
            None => {
                return Err(invalid(
                    "a chapter that does not say its first page follows the one aimed at, \
                     and would be replaced with it",
                ))
            }
        }
    }
    let end = starts
        .iter()
        .flatten()
        .filter_map(within)
        .filter(|s| *s > start)
        .min()
        .map_or(pages - 1, |next| next - 1);
    Ok((start, end))
}

/// The range a splice of `take` into the chapter `aimed` replaces, and what it does to each
/// chapter of a volume of `pages` pages; `page` is the asking profile's place in it.
///
/// Refused — an `invalid`, before a session exists — for a range backwards or longer than
/// [`MAX_SPLICE`], and wherever [`chapter_cut`] refuses: a splice there would be a guess at
/// which pages are the chapter's.
pub fn splice_outcome(
    chapters: &[HeldChapter],
    pages: u32,
    aimed: &str,
    take: Splice,
    page: Option<i64>,
) -> Result<(Cut, SpliceOutcome)> {
    if take.from > take.to || take.to - take.from >= MAX_SPLICE {
        return Err(invalid(format!(
            "pages {}–{} are not a range a chapter is taken from",
            take.from, take.to
        )));
    }
    let starts: Vec<(&HeldChapter, u32)> = chapters
        .iter()
        .filter_map(|c| {
            let start = u32::try_from(c.start_page?).ok()?;
            (start < pages).then_some((c, start))
        })
        .collect();
    let end_of = |start: u32| -> u32 {
        starts
            .iter()
            .map(|(_, s)| *s)
            .filter(|s| *s > start)
            .min()
            .map_or(pages - 1, |next| next - 1)
    };
    let index = chapters
        .iter()
        .position(|c| c.id == aimed)
        .ok_or_else(|| invalid("the chapter aimed at is not in its volume"))?;
    let all: Vec<Option<i64>> = chapters.iter().map(|c| c.start_page).collect();
    let cut = chapter_cut(&all, index, pages)?;
    let new_to = i64::from(cut.0) + i64::from(take.to - take.from);
    let gained = new_to - i64::from(cut.1);
    let last = i64::from(pages) - 1 + gained;
    let chapters = starts
        .iter()
        .map(|(c, s)| {
            let (from, to) = (i64::from(*s), i64::from(end_of(*s)));
            // A bonus filed on the chapter's first page is in the range too.
            let inside = *s >= cut.0 && *s <= cut.1;
            let outcome = if inside {
                SpliceKind::Replace
            } else if *s > cut.1 && gained != 0 {
                SpliceKind::Shift
            } else {
                SpliceKind::Keep
            };
            ChapterSplice {
                id: c.id.clone(),
                number: c.number,
                title: c.title.clone(),
                from,
                to,
                becomes_from: moved_page(from, cut, new_to),
                becomes_to: if inside {
                    new_to
                } else {
                    moved_page(to, cut, new_to)
                },
                outcome,
            }
        })
        .collect();
    Ok((
        cut,
        SpliceOutcome {
            pages_before: i64::from(pages),
            pages_after: last + 1,
            chapters,
            page,
            page_becomes: page.map(|p| moved_page(p, cut, new_to).min(last)),
        },
    ))
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Deserialize, Serialize)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum Outcome {
    /// Already there, byte for byte, whatever its name: nothing moves. `lands` names where
    /// it already is, and is no instruction: nothing is sent, renamed, set aside or
    /// recorded for it.
    Same,
    /// Lands on the volume of its number, with other pages.
    Replace,
    /// Lands on the volume of its number with the same count of pages: only what it says
    /// changes.
    Update,
    /// No volume holds its number.
    Add,
    /// Not the volume the reimport is aimed at.
    OtherNumber,
    /// Names a series that is not the one aimed at; louder than any other answer.
    OtherSeries,
    /// Carries no number, or only one guessed from its name, so lands nowhere.
    Unnumbered,
    /// Another file of the drop carries the same kind and number; neither lands.
    Twice,
}

/// A volume the library holds, as the answer shows it beside the file that would take its
/// place.
#[derive(Debug, Clone, PartialEq, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct HeldVolume {
    pub entry_id: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub number: Option<f64>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub title: Option<String>,
    /// Relative to the edition's folder.
    pub file: String,
    pub size: u64,
    pub pages: u32,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub chapters: Vec<f64>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub page: Option<i64>,
    pub finished: bool,
    pub others: u32,
}

/// What the file says of itself, for the comparison.
#[derive(Debug, Clone, Default, PartialEq, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Arriving {
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub pages: Option<u32>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub title: Option<String>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub chapters: Vec<f64>,
}

#[derive(Debug, Clone, PartialEq, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct VolumeMatch {
    /// The path as announced.
    pub path: String,
    /// Where it is installed, relative to the edition's folder; none where it lands nowhere.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub lands: Option<String>,
    pub outcome: Outcome,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub number: Option<f64>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub entry_id: Option<String>,
    /// The volume aimed at, when this file is another.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub wanted: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub present: Option<HeldVolume>,
    pub arriving: Arriving,
    /// `pages`, `title`, `chapters`: what the file declares otherwise than `present`.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub differs: Vec<String>,
}

#[derive(Debug, Clone, Copy)]
pub struct Incoming<'a> {
    pub path: &'a str,
    pub found: Option<&'a FoundInFile>,
    /// The entry this file already is, byte for byte, wherever and under whatever name it
    /// is held.
    pub identical_to: Option<&'a str>,
}

/// What each file of a drop becomes, in the order the files were given.
///
/// `held` is what one edition holds, `folder` that edition's folder, which every `lands` is
/// relative to. `aimed` is the volume a reimport is aimed at, when it is aimed at one, and
/// `series` the names the aimed work goes by — its folder and its title.
///
/// Refused, as the announcement it comes from: a path with no file name, which has no name
/// to land under. An error: a held volume outside `folder`, which the index should never
/// say and which would have no name to be replaced at.
pub fn match_volumes(
    held: &[Held],
    incoming: &[Incoming],
    folder: &Path,
    aimed: Option<&str>,
    series: &[&str],
) -> Result<Vec<VolumeMatch>> {
    let wanted: Vec<String> = incoming
        .iter()
        .map(|file| {
            landing_name(file.path)
                .ok_or_else(|| invalid(format!("a path without a file name: {:?}", file.path)))
        })
        .collect::<Result<_>>()?;
    // The bare name was the fallback once: the replacement landed beside the edition's
    // files, the old one stayed where it was, and the entry — every reader's place in it —
    // went with the old one.
    let names: Vec<String> = held
        .iter()
        .map(|volume| {
            volume
                .file
                .strip_prefix(folder)
                .map(|relative| relative.to_string_lossy().to_string())
                .map_err(|_| {
                    anyhow!(
                        "{} is held in {} and lies outside it",
                        volume.file.display(),
                        folder.display()
                    )
                })
        })
        .collect::<Result<_>>()?;
    let by_entry: HashMap<&str, usize> = held
        .iter()
        .enumerate()
        .map(|(index, volume)| (volume.entry_id.as_str(), index))
        .collect();

    let keys: Vec<Option<Key>> = incoming
        .iter()
        .map(|file| {
            // A file identical to a held volume stands at that volume's number, whatever
            // it announces, and at none when it is held elsewhere. Counted under what it
            // announced, an exact copy of volume 5 saying 9 made a real volume 9 in the
            // same drop « twice », and kept it from replacing the 9 held.
            if let Some(entry) = file.identical_to {
                let volume = &held[*by_entry.get(entry)?];
                return Key::of(Some(&volume.kind), volume.number?);
            }
            let found = file.found.filter(|found| !found.guessed)?;
            Key::of(found.kind.as_deref(), found.number?)
        })
        .collect();
    let mut arrivals: HashMap<Key, usize> = HashMap::new();
    for key in keys.iter().flatten() {
        *arrivals.entry(*key).or_default() += 1;
    }
    let mut holding: HashMap<Key, Vec<usize>> = HashMap::new();
    for (index, volume) in held.iter().enumerate() {
        if let Some(key) = volume
            .number
            .and_then(|n| Key::of(Some(&volume.kind), n))
            .filter(|key| key.unit != Unit::Other)
        {
            holding.entry(key).or_default().push(index);
        }
    }
    // Two volumes held at one number are a library that already has a doubt in it, and
    // picking one of them would be a guess about which to overwrite — unless the reimport
    // is aimed at one of them, which is the person saying which. Otherwise neither is
    // replaced: the arrival is added beside them, which a person can undo.
    let paired = |key: &Key| -> Option<usize> {
        match holding.get(key)?.as_slice() {
            [only] => Some(*only),
            several => {
                let mut aimed_at = several
                    .iter()
                    .filter(|&&index| Some(held[index].entry_id.as_str()) == aimed);
                aimed_at
                    .next()
                    .filter(|_| aimed_at.next().is_none())
                    .copied()
            }
        }
    };

    // What each file is decided before any name is handed out, so that no addition is
    // given a name a replacement lands on.
    let decided: Vec<Decided> = incoming
        .iter()
        .zip(&keys)
        .map(|(file, key)| {
            // Before any pairing by number: a file identical to a held volume is that
            // volume whatever was read in it. Paired by number, the copy of volume 5 that
            // says 9 was an addition, and volume 5 was in the library twice.
            if let Some(entry) = file.identical_to {
                return Decided::Same(entry, by_entry.get(entry).copied());
            }
            let Some(key) = key else {
                return Decided::Nowhere(Outcome::Unnumbered);
            };
            if arrivals[key] > 1 {
                return Decided::Nowhere(Outcome::Twice);
            }
            match paired(key) {
                Some(index) => Decided::Onto(index),
                None => Decided::Added,
            }
        })
        .collect();

    let mut occupied = Occupied::of(folder, &names);
    let mut landings: Vec<Option<String>> = decided
        .iter()
        .map(|decided| match decided {
            Decided::Onto(index) | Decided::Same(_, Some(index)) => Some(names[*index].clone()),
            _ => None,
        })
        .collect();
    // By path, not by the order the client listed them in: the same drop announced twice
    // must be given the same names, or the second announcement would send a file to a name
    // the first one promised to another.
    let mut additions: Vec<usize> = (0..incoming.len())
        .filter(|&index| matches!(decided[index], Decided::Added))
        .collect();
    additions.sort_by_key(|&index| incoming[index].path);
    for index in additions {
        landings[index] = Some(occupied.take_free(&wanted[index]));
    }

    Ok(incoming
        .iter()
        .zip(decided)
        .zip(landings)
        .map(|((file, decided), lands)| {
            let found = file.found;
            let arriving = Arriving {
                pages: found.and_then(|f| f.pages),
                title: found.and_then(|f| f.title.clone()),
                chapters: found.map(|f| f.chapters.clone()).unwrap_or_default(),
            };
            let mut answer = VolumeMatch {
                path: file.path.to_string(),
                lands,
                outcome: Outcome::Add,
                number: found.and_then(|f| f.number).filter(|n| n.is_finite()),
                entry_id: None,
                wanted: None,
                present: None,
                arriving,
                differs: Vec::new(),
            };
            let compared = |answer: &mut VolumeMatch, index: usize| {
                answer.entry_id = Some(held[index].entry_id.clone());
                answer.differs = differs(&answer.arriving, &held[index]);
                answer.present = Some(present(&names[index], &held[index]));
            };
            match decided {
                // A file identical to what is there moves nothing whatever else it says.
                // Saying it is another volume or another series would offer to install what
                // is already installed.
                Decided::Same(entry, index) => {
                    answer.outcome = Outcome::Same;
                    match index {
                        Some(index) => compared(&mut answer, index),
                        None => answer.entry_id = Some(entry.to_string()),
                    }
                    return answer;
                }
                Decided::Nowhere(outcome) => {
                    answer.outcome = outcome;
                    return answer;
                }
                Decided::Onto(index) => {
                    compared(&mut answer, index);
                    answer.outcome = if answer.arriving.pages == Some(held[index].pages) {
                        Outcome::Update
                    } else {
                        // Pages nobody counted are not the same pages: an update keeps
                        // what is there and only changes what it says, which is a promise
                        // about the content that a missing count cannot make.
                        Outcome::Replace
                    };
                }
                Decided::Added => {}
            }
            if let Some(aimed) = aimed.filter(|&a| answer.entry_id.as_deref() != Some(a)) {
                answer.outcome = Outcome::OtherNumber;
                answer.wanted = Some(aimed.to_string());
            }
            if found.is_some_and(|f| of_another_series(f, series)) {
                answer.outcome = Outcome::OtherSeries;
            }
            answer
        })
        .collect())
}

/// The name a file of the drop lands under when it is added: its own. `None` for a path
/// that has none — empty, or ending in `..`.
pub fn landing_name(path: &str) -> Option<String> {
    Path::new(path)
        .file_name()
        .map(|name| name.to_string_lossy().to_string())
}

/// What one file was found to be, before names are handed out.
enum Decided<'a> {
    /// Identical to this entry, and where it is held in this edition, if it is.
    Same(&'a str, Option<usize>),
    /// Lands on this held volume.
    Onto(usize),
    /// Lands under a free name.
    Added,
    /// Lands nowhere.
    Nowhere(Outcome),
}

/// A volume's kind and exact number: what a match is made on.
///
/// The number is compared by its bits, so 2 is not 2.5 and is not « about two ». `-0` is
/// made `0` first, the one pair of equal numbers with different bits.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
struct Key {
    unit: Unit,
    bits: u64,
}

/// The two kinds the index holds, and whatever else a file may call itself.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
enum Unit {
    Volume,
    Chapter,
    /// A word the index does not hold — « BONUS », a typo, an empty string. Held volumes
    /// are only ever the other two, so it matches none of them and is added, rather than
    /// read as a volume and laid on volume 2.
    Other,
}

impl Key {
    /// `None` for a number that is no number: NaN equals nothing, and infinity is no
    /// volume.
    fn of(kind: Option<&str>, number: f64) -> Option<Key> {
        if !number.is_finite() {
            return None;
        }
        let number = if number == 0.0 { 0.0 } else { number };
        // A missing kind is a volume, which is what the index says of a file that does not
        // say. And a chapter-file numbered 2 is never volume 2: that one comparison is what
        // keeps a loose chapter from overwriting a whole volume.
        let unit = match kind.map(str::trim) {
            None => Unit::Volume,
            Some(k) if k.eq_ignore_ascii_case("VOLUME") => Unit::Volume,
            Some(k) if k.eq_ignore_ascii_case("CHAPTER") => Unit::Chapter,
            Some(_) => Unit::Other,
        };
        Some(Key {
            unit,
            bits: number.to_bits(),
        })
    }
}

fn present(file: &str, volume: &Held) -> HeldVolume {
    HeldVolume {
        entry_id: volume.entry_id.clone(),
        number: volume.number,
        title: volume.title.clone(),
        file: file.to_string(),
        size: volume.size,
        pages: volume.pages,
        chapters: volume.chapters.clone(),
        page: volume.page,
        finished: volume.finished,
        others: volume.others,
    }
}

/// What the file declares and the held volume says otherwise. A value the file does not
/// declare is no difference: it says nothing either way.
fn differs(arriving: &Arriving, volume: &Held) -> Vec<String> {
    let mut found = Vec::new();
    if arriving.pages.is_some_and(|p| p != volume.pages) {
        found.push("pages".to_string());
    }
    if arriving
        .title
        .as_ref()
        .is_some_and(|t| volume.title.as_ref() != Some(t))
    {
        found.push("title".to_string());
    }
    // Compared as sets: the index lists a volume's chapters in whatever order it stored
    // them, and the same chapters in another order are the same chapters.
    if !arriving.chapters.is_empty() && sorted(&arriving.chapters) != sorted(&volume.chapters) {
        found.push("chapters".to_string());
    }
    found
}

fn sorted(numbers: &[f64]) -> Vec<f64> {
    let mut numbers = numbers.to_vec();
    numbers.sort_by(f64::total_cmp);
    numbers
}

/// Whether the series a file names is none of the names the aimed work goes by.
///
/// With no name to compare it with, a file that names a series is of another one: it
/// installs only once someone says so, which costs a confirmation, where the other answer
/// could put another series' volume in this one.
fn of_another_series(found: &FoundInFile, series: &[&str]) -> bool {
    let Some(named) = found.series.as_deref().map(folded) else {
        return false;
    };
    // A name with nothing in it — spaces — says nothing.
    !named.is_empty() && series.iter().all(|name| folded(name) != named)
}

/// The names a folder holds, as a file system that does not tell case or Unicode forms
/// apart sees them: « tome 5.cbz » and « Tome 5.cbz » are one file there, and so are « É »
/// composed and « E » followed by its accent. The tree is moved between such disks.
struct Occupied {
    folder: std::path::PathBuf,
    keys: HashSet<String>,
}

impl Occupied {
    /// What `folder` holds on disk now, and `names`, promised to files on their way.
    fn of<'a>(folder: &Path, names: impl IntoIterator<Item = &'a String>) -> Occupied {
        let on_disk = std::fs::read_dir(folder)
            .into_iter()
            .flatten()
            .flatten()
            .map(|entry| name_key(&entry.file_name().to_string_lossy()));
        let mut keys: HashSet<String> = on_disk.collect();
        keys.extend(names.into_iter().map(|name| name_key(name)));
        Occupied {
            folder: folder.to_path_buf(),
            keys,
        }
    }

    fn holds(&self, name: &str) -> bool {
        self.keys.contains(&name_key(name)) || self.folder.join(name).exists()
    }

    /// A free name from `wanted`, and from now on not free.
    ///
    /// " (2)", " (3)" and so on, before the extension — the shape a browser uses, because it
    /// is the shape a person recognises. Bounded: past a hundred, something else is wrong
    /// and hunting for a hundred and first name is not the answer.
    fn take_free(&mut self, wanted: &str) -> String {
        let free = if self.holds(wanted) {
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
            (2..=100)
                .map(|n| format!("{stem} ({n}){extension}"))
                .find(|candidate| !self.holds(candidate))
                .unwrap_or_else(|| format!("{stem} ({}){extension}", token()))
        } else {
            wanted.to_string()
        };
        self.keys.insert(name_key(&free));
        free
    }
}

fn name_key(name: &str) -> String {
    name.nfc().collect::<String>().to_lowercase()
}

/// A name in `folder` that neither a file on disk nor any of `taken` holds, from `wanted` —
/// in any case and either Unicode form.
///
/// `taken` is what the disk does not know yet: the names other files of the same drop were
/// just promised, so two additions called « Tome 5.cbz » are not both told « Tome 5 (2).cbz ».
///
/// **A name free now is not a name free when the file is written.** This reads the disk at
/// the announcement, and another import, or a person in a file manager, can take the name
/// before the commit. Whatever installs at it must refuse to write over a file it finds
/// there — a rename that does not replace, or a hard link then an unlink — rather than trust
/// this answer.
pub fn free_name_among(folder: &Path, wanted: &str, taken: &HashSet<String>) -> String {
    Occupied::of(folder, taken).take_free(wanted)
}

/// How a folder of a drop was found to be a work the library holds.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum Matched {
    Identity,
    Title,
}

/// What one folder of a universe drop is to the library.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SeriesIs<'a> {
    /// A work of the universe aimed at: its volumes are compared with that work's.
    Ours(&'a Titled),
    /// A work the library holds outside it: a move, never a second copy.
    Elsewhere(&'a Titled, Matched),
    /// Nothing the library holds: a creation.
    New,
}

/// What the folder `name` of a drop onto the universe `universe` is, given the identity and
/// the title its `work.json` declares, among `works`.
///
/// **The identity first, wherever it points.** Two works can share a title — a universe's
/// « Dragon Ball » and another publisher's at the root — and the one a folder was scanned
/// as is the one it is. Then, inside the universe, the folder's own name, then its folded
/// title; then, outside any universe, the folded title. A title two works answer to decides
/// nothing: the folder is a creation, which a reader can still file by hand, where a wrong
/// match would have compared its volumes with another series' and replaced them.
pub fn series_of_universe<'a>(
    name: &str,
    identity: Option<&str>,
    title: Option<&str>,
    universe: &str,
    works: &'a [Titled],
) -> SeriesIs<'a> {
    let ours = |w: &&Titled| w.universe_id.as_deref() == Some(universe);
    if let Some(work) = identity.and_then(|id| works.iter().find(|w| w.work_id == id)) {
        return if ours(&work) {
            SeriesIs::Ours(work)
        } else {
            SeriesIs::Elsewhere(work, Matched::Identity)
        };
    }
    if let Some(work) = works
        .iter()
        .filter(ours)
        .find(|w| w.path.file_name().is_some_and(|n| n == name))
    {
        return SeriesIs::Ours(work);
    }
    let wanted = folded(title.unwrap_or(name));
    let titled = |w: &&Titled| folded(w.title.as_deref().unwrap_or(&w.name)) == wanted;
    let only = |mut found: Vec<&'a Titled>| (found.len() == 1).then(|| found.remove(0));
    if let Some(work) = only(works.iter().filter(ours).filter(titled).collect()) {
        return SeriesIs::Ours(work);
    }
    match only(
        works
            .iter()
            .filter(|w| w.universe_id.is_none())
            .filter(titled)
            .collect(),
    ) {
        Some(work) => SeriesIs::Elsewhere(work, Matched::Title),
        None => SeriesIs::New,
    }
}

/// A series' name as two spellings of it are compared: without case, accents, or the
/// difference between one space and several, and in its compatibility form, so that
/// « ＢＬＵＥ » typed full-width is « blue ».
///
/// **The punctuation stays.** This was the search's fold once, which drops it, and under it
/// « Re:Zero » was « Re Zero », « Gintama' » was « Gintama » and « Durarara!! » was
/// « Durarara » — each a different work, or a different catalogue's spelling, that a file
/// could then be installed under without a word. Only the typographic apostrophe is made
/// the plain one, since French writes « L’Attaque » and a file system « L'Attaque ».
///
/// **The kana voicing marks stay** too: バ, パ and ハ are three letters, not one with
/// accents, and the search's fold, which takes them off like an accent, would make two
/// titles one.
pub fn folded(text: &str) -> String {
    let compatible: String = text
        .chars()
        // Written on their own, ゛ and ゜ are what a keyboard without the voiced kana types
        // after the letter. NFKC makes each a space and the combining mark, which left
        // « ハ゛ » as « ハ ゙ » rather than « バ »: they are made the combining mark first.
        .map(|ch| match ch {
            '\u{309B}' => '\u{3099}',
            '\u{309C}' => '\u{309A}',
            other => other,
        })
        .collect::<String>()
        .nfkc()
        .map(|ch| match ch {
            '\u{2019}' | '\u{02bc}' => '\'',
            other => other,
        })
        .collect();
    // Lowercased before the accents come off: lowercasing can itself write a combining
    // mark — « İ » becomes « i » and a dot above — and only what is taken off afterwards is
    // sure to be gone.
    let lowered = compatible.to_lowercase();
    let bare: String = lowered
        .nfd()
        .filter(|&ch| !is_combining_mark(ch) || matches!(ch, '\u{3099}' | '\u{309A}'))
        .nfc()
        // A voicing mark NFC could not put on a kana sits on nothing, and says nothing.
        .filter(|&ch| !matches!(ch, '\u{3099}' | '\u{309A}'))
        .collect();
    bare.split_whitespace().collect::<Vec<_>>().join(" ")
}
