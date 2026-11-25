//! The name a volume goes by when it leaves the library, built from what the library knows.
//!
//! `[universe]_[series]_[edition]_[unit]_[number].ext`, every field absent when it has
//! nothing to say, `_` between fields and `-` inside them. The file on the disk is called
//! whatever it was imported as, and "Tome 3.cbz" downloaded twice is the same unhelpful name
//! twice; this one says which book it is wherever it lands.
//!
//! **Computed, never stored.** Renaming a file already indexed makes it a new entry and loses
//! its reading position, so the name is worked out each time it is asked for, from the state
//! of the library at that moment.

use super::text::search_key;

/// What a volume's name is made of.
pub struct Parts<'a> {
    pub universe: Option<&'a str>,
    pub work: &'a str,
    pub edition: Option<&'a str>,
    /// How many editions the work holds. The edition is only worth saying when there is
    /// another to tell it from.
    pub editions: i64,
    pub chapter: bool,
    pub number: Option<f64>,
    pub title: Option<&'a str>,
    /// The name on the disk: its extension is kept, and it stands in when nothing else can.
    pub file: &'a str,
}

/// Articles dropped from the start of a name, followed by a space. « i » is not one:
/// « I Am a Hero » is not « Am a Hero ». « de » and « du » are not either.
const ARTICLES: [&str; 20] = [
    "le", "la", "les", "un", "une", "des", "the", "a", "an", "el", "los", "las", "il", "lo", "gli",
    "der", "die", "das", "ein", "eine",
];

/// The widest a field gets, in bytes: three fields of a script written on three bytes a
/// letter must still fit the 255 a file name is allowed.
const FIELD: usize = 60;

fn cut(rest: &str) -> Option<&str> {
    (!rest.trim().is_empty()).then(|| rest.trim_start())
}

fn without_article(name: &str) -> &str {
    let name = name.trim();
    let lower = name.to_lowercase();
    for article in ARTICLES {
        if lower.starts_with(article) && lower[article.len()..].starts_with(' ') {
            if let Some(rest) = name.get(article.len()..).and_then(cut) {
                return rest;
            }
        }
    }
    for elision in ["l'", "l\u{2019}"] {
        if lower.starts_with(elision) {
            if let Some(rest) = name.get(elision.len()..).and_then(cut) {
                return rest;
            }
        }
    }
    name
}

fn slug(name: &str) -> String {
    // Apostrophes separate here, where a search key joins them: « Terres d'Arran » is
    // read as two words in a file name.
    let spaced = without_article(name).replace(['\'', '\u{2019}', '\u{02bc}'], " ");
    let words: Vec<String> = search_key(&spaced)
        .split_whitespace()
        .map(str::to_string)
        .collect();
    let mut out = String::new();
    for word in words {
        let next = if out.is_empty() { 0 } else { 1 } + word.len();
        if out.len() + next > FIELD {
            if out.is_empty() {
                out = word
                    .chars()
                    .scan(0, |n, c| {
                        *n += c.len_utf8();
                        (*n <= FIELD).then_some(c)
                    })
                    .collect();
            }
            break;
        }
        if !out.is_empty() {
            out.push('-');
        }
        out.push_str(&word);
    }
    out
}

/// Whether `inner` sits whole inside `outer`, word for word: « ham » is not in « hamlet ».
fn within(outer: &str, inner: &str) -> bool {
    let (o, i): (Vec<_>, Vec<_>) = (outer.split('-').collect(), inner.split('-').collect());
    !i.is_empty() && o.windows(i.len()).any(|w| w == i.as_slice())
}

fn number(value: f64, width: usize) -> String {
    let abs = value.abs();
    let whole = abs.trunc();
    let mut out = format!("{:0width$}", whole as u64);
    let fraction = format!("{:.3}", abs - whole);
    let fraction = fraction[1..].trim_end_matches('0');
    if fraction.len() > 1 {
        out.push_str(fraction);
    }
    if value < 0.0 {
        out.insert(0, 'm');
    }
    out
}

pub fn file_name(parts: &Parts<'_>) -> String {
    let mut universe = parts.universe.map(slug).filter(|u| !u.is_empty());
    let series = Some(slug(parts.work)).filter(|s| !s.is_empty());
    let mut kept_series = series.clone();
    if let (Some(u), Some(s)) = (&universe, &series) {
        if u == s {
            // The series is the universe's own name: said once.
            kept_series = None;
        } else if within(s, u) {
            // The series already says the universe; dropping the series instead would give
            // « Naruto » and « Naruto Shippuden » the same name.
            universe = None;
        }
    }
    let edition = (parts.editions >= 2)
        .then(|| parts.edition.map(slug))
        .flatten()
        .filter(|e| !e.is_empty() && Some(e) != series.as_ref());

    let ext = parts
        .file
        .rsplit_once('.')
        .map(|(_, e)| e.to_lowercase())
        .filter(|e| !e.is_empty() && e.len() <= 5 && e.chars().all(|c| c.is_ascii_alphanumeric()))
        .unwrap_or_else(|| "cbz".to_string());

    let tail = match parts.number.filter(|n| n.is_finite()) {
        Some(n) => vec![
            if parts.chapter { "chapitre" } else { "tome" }.to_string(),
            number(n, if parts.chapter { 4 } else { 3 }),
        ],
        // Nothing to count by: what the book is called, or failing that, what it was.
        None => {
            let stem = parts.file.rsplit_once('.').map_or(parts.file, |(s, _)| s);
            let one = parts
                .title
                .map(slug)
                .filter(|t| !t.is_empty())
                .unwrap_or_else(|| slug(stem));
            vec![if one.is_empty() {
                "volume".to_string()
            } else {
                one
            }]
        }
    };

    let fields: Vec<String> = [universe, kept_series, edition]
        .into_iter()
        .flatten()
        .chain(tail)
        .collect();
    format!("{}.{ext}", fields.join("_"))
}

/// The widest a folder name gets, in bytes. Narrower than the 255 a file system allows:
/// the folder is one segment among three (universe, series, edition) of a path that also has
/// to hold a file name, and a path is limited too.
const FOLDER: usize = 120;

/// Names a Windows file system refuses whatever their extension, and which a library copied
/// to a USB key would otherwise carry there.
const RESERVED: [&str; 22] = [
    "con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8",
    "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9",
];

/// What a folder of the library is called: the real name of the universe, the series or the
/// edition, accents and capitals kept — unlike a file name, which is a slug, a folder is
/// what a person sees when they open the library in a file manager.
///
/// What a file system refuses (`/ \ : * ? " < > |`, controls) becomes `-`, so « Fate/Zero »
/// is `Fate-Zero` and not a folder `Fate` holding a folder `Zero`. Dots, spaces and dashes
/// at either end go: `..` must never be a name, and Windows drops a trailing dot silently,
/// which would make two names one. A name that comes to nothing is `Sans nom`, never an
/// empty segment.
pub fn folder_name(name: &str) -> String {
    let replaced: String = name
        .chars()
        .map(|c| {
            if c.is_whitespace() {
                ' '
            } else if c.is_control() || "/\\:*?\"<>|".contains(c) {
                '-'
            } else {
                c
            }
        })
        .collect();
    let folded = replaced.split_whitespace().collect::<Vec<_>>().join(" ");
    let trimmed = folded.trim_matches(['.', '-', ' ']);
    let mut out = String::new();
    for c in trimmed.chars() {
        if out.len() + c.len_utf8() > FOLDER {
            break;
        }
        out.push(c);
    }
    let out = out.trim_end_matches(['.', '-', ' ']);
    if out.is_empty() {
        return "Sans nom".to_string();
    }
    let stem = out.split('.').next().unwrap_or(out).to_lowercase();
    if RESERVED.contains(&stem.as_str()) {
        format!("{out}_")
    } else {
        out.to_string()
    }
}
