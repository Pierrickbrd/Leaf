//! What a scan noticed but did not treat as a failure.
//!
//! None of this touches the index: a library with no metadata at all still reads, because
//! the folder names carry the structure. These are the things a person might want to fix,
//! collected so that the report can say them once instead of the server guessing quietly.
//!
//! The rule throughout: **describe what is absent, never invent a value for it.** A volume
//! with no number is a real thing — a one-shot, a hors-série, an artbook — and the answer is
//! to say so, not to make one up from the file name.

use crate::metadata::sidecars::{ChapterJson, EditionJson, EntryJson, WorkJson, FORMAT_VERSION};

pub fn work(folder: &str, meta: Option<&WorkJson>) -> Vec<String> {
    let Some(meta) = meta else {
        return vec![format!(
            "{folder}/work.json missing — title, status and reading direction unknown"
        )];
    };
    let mut missing = Vec::new();
    if blank(&meta.title) {
        missing.push("title");
    }
    if blank(&meta.medium) {
        missing.push("medium");
    }
    if blank(&meta.status) {
        missing.push("status");
    }
    if blank(&meta.reading_direction) {
        missing.push("readingDirection");
    }
    let where_ = format!("{folder}/work.json");
    let mut out = report(&where_, &missing);
    out.extend(version(&where_, meta.leaf));
    out
}

pub fn edition(
    folder: &str,
    meta: Option<&EditionJson>,
    implicit: bool,
    work_meta: Option<&WorkJson>,
) -> Vec<String> {
    if implicit {
        // An implicit edition has no folder of its own, so its fields live in work.json.
        let mut missing = Vec::new();
        let status = meta
            .and_then(|m| m.status.clone())
            .or_else(|| work_meta.and_then(|w| w.status.clone()));
        let volume_count = meta
            .and_then(|m| m.volume_count)
            .or_else(|| work_meta.and_then(|w| w.volume_count));
        if blank(&status) {
            missing.push("status");
        }
        if volume_count.is_none() {
            missing.push("volumeCount");
        }
        return report(&format!("{folder}/work.json (implicit edition)"), &missing);
    }

    let Some(meta) = meta else {
        return vec![format!(
            "{folder}/edition.json missing — name, status and volume count unknown"
        )];
    };
    let mut missing = Vec::new();
    if blank(&meta.name) {
        missing.push("name");
    }
    if blank(&meta.status) {
        missing.push("status");
    }
    if meta.volume_count.is_none() {
        missing.push("volumeCount");
    }
    let where_ = format!("{folder}/edition.json");
    let mut out = report(&where_, &missing);
    out.extend(version(&where_, meta.leaf));
    out
}

pub fn entry(
    file: &str,
    declared: Option<&EntryJson>,
    kind: &str,
    has_several_editions: bool,
) -> Vec<String> {
    let Some(declared) = declared else {
        return Vec::new();
    };
    let mut missing = Vec::new();
    if blank(&declared.work) {
        missing.push("work");
    }
    if declared.kind.trim().is_empty() {
        missing.push("type");
    }
    // A volume without a number is a real thing. What it may not do is leave its place to
    // the file name, so it has to be anchored instead: one or the other, never neither.
    let anchored = declared.chapters.iter().any(|c| c.after.is_some());
    if kind == "VOLUME" && declared.number.is_none() && !anchored {
        missing.push("number, or a chapter with after");
    }
    if has_several_editions && blank(&declared.edition) {
        missing.push("edition");
    }
    let mut out = report(file, &missing);
    out.extend(version(file, declared.leaf));
    out
}

/// Whether a file agrees with where it is filed.
///
/// A volume that claims to belong somewhere else is the shape a misplaced download takes,
/// and it is worth saying before the number it carries is trusted.
pub fn identity(
    file: &str,
    declared: Option<&EntryJson>,
    work_name: &str,
    work_title: Option<&str>,
    edition_name: Option<&str>,
) -> Vec<String> {
    let Some(declared) = declared else {
        return Vec::new();
    };
    let mut out = Vec::new();

    let claimed_work = declared
        .work
        .as_deref()
        .map(str::trim)
        .filter(|w| !w.is_empty());
    if let Some(claimed) = claimed_work {
        let matches_name = claimed.eq_ignore_ascii_case(work_name);
        let matches_title = work_title.is_some_and(|t| claimed.eq_ignore_ascii_case(t));
        if !matches_name && !matches_title {
            out.push(format!(
                "{file} claims work \"{claimed}\" but sits in \"{work_name}\""
            ));
        }
    }

    let claimed_edition = declared
        .edition
        .as_deref()
        .map(str::trim)
        .filter(|e| !e.is_empty());
    match (claimed_edition, edition_name) {
        (None, Some(name)) => out.push(format!(
            "{file} claims no edition although it sits in \"{name}\""
        )),
        (Some(claimed), None) => out.push(format!(
            "{file} claims edition \"{claimed}\" although the work has only one"
        )),
        (Some(claimed), Some(name)) if !claimed.eq_ignore_ascii_case(name) => out.push(format!(
            "{file} claims edition \"{claimed}\" but sits in \"{name}\""
        )),
        _ => {}
    }
    out
}

pub fn chapters(file: &str, declared: &[ChapterJson], standalone: bool) -> Vec<String> {
    let mut out = Vec::new();
    for (i, c) in declared.iter().enumerate() {
        let name = c
            .title
            .clone()
            .or_else(|| c.raw.clone())
            .unwrap_or_else(|| format!("chapter {}", i + 1));
        if c.number.is_none() && blank(&c.title) && blank(&c.raw) {
            out.push(format!(
                "{file}: a chapter with no number, no title and no label"
            ));
        }
        // Inside a volume an unnumbered chapter follows the previous numbered one. On its
        // own it has nothing to follow, and the file name ends up deciding — which is not a
        // decision anyone made.
        if standalone && c.number.is_none() && c.after.is_none() {
            out.push(format!(
                "{file}: \"{name}\" has neither number nor after — its place will depend on the file name"
            ));
        }
    }
    out
}

/// Fields that are there and disagree — with each other, or with the file holding them.
///
/// Only ever read from `entry.json`, never from a ComicInfo fallback. ComicInfo has no way
/// to say "this is a chapter", so it pins a volume number on one anyway — `Chapitre 686.5`
/// declares `Number 75` there — and reporting that on every chapter of a real library would
/// bury everything else. What is checked here is what somebody wrote on purpose.
pub fn coherence(
    file: &str,
    declared: Option<&EntryJson>,
    kind: &str,
    page_count: i32,
    from_name: Option<f64>,
) -> Vec<String> {
    let Some(declared) = declared else {
        return Vec::new();
    };
    let mut out = Vec::new();

    // A type that is neither, however it was spelled. The reader falls back on the file
    // name, so what the file plainly meant to say is simply lost.
    let stated = declared.kind.trim();
    if !stated.is_empty()
        && !stated.eq_ignore_ascii_case("VOLUME")
        && !stated.eq_ignore_ascii_case("CHAPTER")
    {
        out.push(format!(
            "{file}: type is \"{stated}\", which is neither VOLUME nor CHAPTER — read as {kind}"
        ));
    }

    if let Some(number) = declared.number {
        if !number.is_finite() || number < 0.0 {
            out.push(format!("{file}: number is {number}"));
        }
    }

    // `volume` says which volume a loose chapter came from. On a volume it is the volume
    // itself, and a volume that came from another volume is not a thing.
    if kind == "VOLUME" {
        if let Some(from) = declared.volume {
            out.push(format!(
                "{file}: type is VOLUME and volume is {from} — that field says which volume a \
                 loose chapter came from, and is ignored here"
            ));
        }
    }

    // The number wins over the file name, which is the rule. Worth a word all the same when
    // somebody wrote both and they disagree: one of the two is a mistake.
    if let (Some(declared_number), Some(named)) = (declared.number, from_name) {
        if declared_number != named {
            out.push(format!(
                "{file}: declares number {declared_number}, and its own name says {named}"
            ));
        }
    }

    let mut starts: Vec<i32> = Vec::new();
    for c in &declared.chapters {
        let name = c
            .title
            .clone()
            .or_else(|| c.raw.clone())
            .or_else(|| c.number.map(|n| n.to_string()))
            .unwrap_or_else(|| "a chapter".to_string());

        // A marker past the last page is a marker nothing can ever reach.
        if let Some(start) = c.start_page {
            if page_count > 0 && start >= page_count {
                out.push(format!(
                    "{file}: \"{name}\" starts at page {start} of {page_count}"
                ));
            }
            if starts.contains(&start) {
                out.push(format!(
                    "{file}: \"{name}\" starts at page {start}, where another already does — \
                     only one of them can ever be reached"
                ));
            }
            starts.push(start);
        }

        // `after` places what has no number of its own. With a number it never applies.
        if c.number.is_some() && c.after.is_some() {
            out.push(format!(
                "{file}: \"{name}\" carries a number and an after — the number places it and \
                 the after is ignored"
            ));
        }
    }
    out
}

/// An entry.json that exists and says nothing worth having.
pub fn says_nothing(declared: Option<&EntryJson>) -> bool {
    declared.is_some_and(|d| {
        blank(&d.work) && d.number.is_none() && blank(&d.title) && d.chapters.is_empty()
    })
}

fn blank(value: &Option<String>) -> bool {
    value.as_deref().map(str::trim).unwrap_or("").is_empty()
}

fn report(where_: &str, missing: &[&str]) -> Vec<String> {
    if missing.is_empty() {
        Vec::new()
    } else {
        vec![format!("{where_}: {}", missing.join(", "))]
    }
}

/// A sidecar written for a format this server does not know is worth saying out loud rather
/// than reading optimistically and getting half of it.
fn version(where_: &str, leaf: Option<i32>) -> Vec<String> {
    match leaf {
        None => vec![format!("{where_}: no \"leaf\" version marker")],
        Some(v) if v > FORMAT_VERSION => vec![format!(
            "{where_}: written for format {v}, this server reads {FORMAT_VERSION}"
        )],
        _ => Vec::new(),
    }
}
