//! Reading ComicInfo.xml — a fallback, and a temporary one.
//!
//! The model does not rest on it: entry.json is authoritative as soon as it exists. But
//! **the real library is 100 % ComicInfo and 0 % entry.json today**, so this is not a
//! courtesy: it is the only reader for its 2 677 chapters, and it ports first-class.
//!
//! Seen on the real library: the format forces a chapter to wear a volume number
//! (Chapitre 686.5 declares `<Number>75</Number>`) and repeats the arc inside every volume,
//! which makes a mid-volume boundary impossible to express. Both reasons to leave it.
//!
//! quick-xml does not resolve external entities at all, so the secure-processing settings
//! the Kotlin had to ask for explicitly are the default here.

use quick_xml::events::Event;
use quick_xml::Reader;

use super::sidecars::{ChapterJson, EntryJson};

#[derive(Debug, Clone, Default)]
pub struct LegacyRead {
    pub entry: EntryJson,
    // What is series-scoped, and therefore copied into every single file.
    pub series: Option<String>,
    pub volume_count: Option<i32>,
    pub publisher: Option<String>,
    pub language: Option<String>,
    pub reading_direction: Option<String>,
    pub genres: Vec<String>,
    pub author: Option<String>,
    pub arc: Option<String>,
}

pub const ENTRY_NAME: &str = "ComicInfo.xml";

/// The five entities XML defines, plus numeric references.
///
/// Nothing else is resolved, and nothing external is fetched — a document that declares its
/// own entities gets them back verbatim rather than expanded, which is the safe half of what
/// the Kotlin had to ask `DocumentBuilderFactory` for explicitly.
fn resolve(name: &str) -> String {
    match name {
        "amp" => "&".into(),
        "lt" => "<".into(),
        "gt" => ">".into(),
        "quot" => "\"".into(),
        "apos" => "'".into(),
        _ => {
            let numeric = name
                .strip_prefix("#x")
                .or_else(|| name.strip_prefix("#X"))
                .and_then(|hex| u32::from_str_radix(hex, 16).ok())
                .or_else(|| name.strip_prefix('#').and_then(|d| d.parse().ok()))
                .and_then(char::from_u32);
            match numeric {
                Some(c) => c.to_string(),
                None => format!("&{name};"),
            }
        }
    }
}

/// Where a tag's text goes once it closes: a chapter's <Name> makes a chapter, a tag at the
/// top level becomes a field, and anything else is dropped. An empty value is not a value.
///
/// `or_insert` and not `insert`: the first spelling of a tag wins, which is the rule this
/// format needs for a document that repeats one.
fn record(
    path: &[String],
    tag: String,
    value: String,
    tags: &mut std::collections::HashMap<String, String>,
    chapters: &mut Vec<ChapterJson>,
) {
    if value.is_empty() {
        return;
    }
    if path.iter().any(|p| p == "Chapter") {
        // The tag nothing reads — non-standard, and yet it carries 2 677 chapters. No
        // start page is declared anywhere: that is the data to create.
        if tag == "Name" {
            chapters.push(ChapterJson {
                raw: Some(value),
                ..Default::default()
            });
        }
    } else if path.len() <= 1 {
        tags.entry(tag).or_insert(value);
    }
}

pub fn read(content: &[u8]) -> Option<LegacyRead> {
    let text = std::str::from_utf8(content).ok()?;
    let mut reader = Reader::from_str(text);
    // Not trimmed by the reader: an entity splits its text in two, and trimming each half
    // eats the space beside it. "The Knuckle & The Arrow" came through as
    // "The Knuckle&The Arrow". The assembled value is trimmed once, below.
    reader.config_mut().trim_text(false);

    // Flat tags, plus the one nested block that matters. The format has no nesting worth
    // tracking beyond <Chapters><Chapter><Name>, and treating it as flat would let a
    // chapter's <Name> overwrite the volume's <Title>.
    let mut tags: std::collections::HashMap<String, String> = std::collections::HashMap::new();
    let mut chapters: Vec<ChapterJson> = Vec::new();

    let mut path: Vec<String> = Vec::new();
    let mut current = String::new();

    loop {
        match reader.read_event() {
            Ok(Event::Start(e)) => {
                path.push(String::from_utf8_lossy(e.local_name().as_ref()).to_string());
                current.clear();
            }
            Ok(Event::Text(e)) => {
                current.push_str(&e.decode().unwrap_or_default());
            }
            // An entity arrives as an event of its own, between the two halves of the text
            // it sits in. Ignoring it silently swallowed the character: a real title in the
            // library is "Death&Strawberry", and it came through as "DeathStrawberry".
            Ok(Event::GeneralRef(e)) => {
                let name = e.decode().unwrap_or_default();
                current.push_str(&resolve(&name));
            }
            Ok(Event::End(_)) => {
                let Some(tag) = path.pop() else { break };
                let value = current.trim().to_string();
                current.clear();
                record(&path, tag, value, &mut tags, &mut chapters);
            }
            Ok(Event::Eof) => break,
            Err(_) => return None,
            _ => {}
        }
    }

    if tags.is_empty() && chapters.is_empty() {
        return None;
    }

    let get = |name: &str| tags.get(name).cloned().filter(|v| !v.trim().is_empty());
    let year = get("Year");
    let published_on = year.map(|y| {
        [Some(y), get("Month"), get("Day")]
            .into_iter()
            .flatten()
            .collect::<Vec<_>>()
            .join("-")
    });

    Some(LegacyRead {
        entry: EntryJson {
            number: get("Number").and_then(|n| n.replace(',', ".").parse().ok()),
            title: get("Title"),
            isbn: get("GTIN"),
            published_on,
            summary: get("Summary"),
            chapters,
            ..Default::default()
        },
        series: get("Series"),
        volume_count: get("Count").and_then(|c| c.parse().ok()),
        publisher: get("Publisher"),
        language: get("LanguageISO"),
        reading_direction: get("Manga")
            .filter(|m| m.contains("RightToLeft"))
            .map(|_| "RIGHT_TO_LEFT".to_string()),
        genres: get("Genre")
            .map(|g| {
                g.split(',')
                    .map(str::trim)
                    .filter(|p| !p.is_empty())
                    .map(str::to_string)
                    .collect()
            })
            .unwrap_or_default(),
        author: get("Writer"),
        arc: get("StoryArc"),
    })
}
