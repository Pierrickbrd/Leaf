//! The two pure functions the whole model leans on: how a name is folded before it is
//! matched, and where the holes in a collection are.
//!
//! Both ported from the Kotlin unchanged. They have tests of their own because they are the
//! kind of code that is easy to get subtly wrong and impossible to notice: a fold that
//! keeps an accent finds nothing, a gap that counts wrong tells you to buy a volume you own.

use std::collections::BTreeSet;

use unicode_normalization::UnicodeNormalization;

/// Folds a string down to what a search should match on: no accents, no case, no
/// punctuation noise. "L'Attaque des Titans" and "lattaque des titans" become the same
/// thing, which is what someone typing on a phone actually produces.
///
/// What survives is a **letter or a digit in any script**, not just a Latin one. It was
/// `a-z0-9` once, on the reasoning that a comics library is named in Latin-1 plus a handful
/// of ligatures — and under that rule `\u{30cf}\u{30a4}\u{30ad}\u{30e5}\u{30fc}` folded to the empty string, which finds nothing and
/// is indistinguishable from a search nobody typed. A title in its own script was not an
/// edge case, it was unreachable.
///
/// The fold is still a fold: NFD takes the accents off before anything is kept, so
/// "Haiky\u{16b}" and "haikyu" still meet. What changed is that a character no accent can be
/// taken off is now kept rather than dropped.
///
/// One limit worth stating, since it is not this function's to fix: `unicode61` splits
/// tokens on non-alphanumerics, and Japanese and Chinese are written without spaces. So a
/// title in either is one token and matches from its beginning, not from its middle.
/// Searching inside one would want FTS5's trigram tokenizer, which is a different decision.
pub fn search_key(text: &str) -> String {
    let mut out = String::with_capacity(text.len());
    let mut pending_space = false;

    // Decomposed first, so an accent becomes a separate mark that can simply be dropped —
    // whatever letter it was sitting on. A hand-written table of the accented letters a
    // French library uses folded "Haikyū" to "haiky", which finds nothing and says nothing.
    for ch in text.nfd() {
        if is_combining_mark(ch) {
            continue;
        }
        match ch {
            // Apostrophes join rather than separate: "L'Attaque" has to be reachable by
            // typing "lattaque", which is what someone in a hurry actually produces.
            '\'' | '\u{2019}' | '\u{02bc}' | '`' => {}
            // The ligatures NFD does not take apart, because they are letters and not a
            // letter plus a mark. Before the general arm below, which would otherwise keep
            // them whole: they are alphabetic, and "ae" is what somebody types.
            'æ' | 'Æ' | 'œ' | 'Œ' | 'ß' | 'ø' | 'Ø' => {
                if pending_space && !out.is_empty() {
                    out.push(' ');
                }
                pending_space = false;
                out.push_str(match ch {
                    'æ' | 'Æ' => "ae",
                    'œ' | 'Œ' => "oe",
                    'ß' => "ss",
                    _ => "o",
                });
            }
            // A letter or a digit, in whatever script it is written. NFD has already taken
            // the accent off anything that had one, so what reaches here is the letter
            // itself — a Latin one, a kana, an ideograph, an Arabic letter alike.
            //
            // to_lowercase and not to_ascii_lowercase: outside ASCII the two disagree, and
            // one of them silently does nothing.
            ch if ch.is_alphanumeric() => {
                if pending_space && !out.is_empty() {
                    out.push(' ');
                }
                pending_space = false;
                out.extend(ch.to_lowercase());
            }
            _ => {
                if !out.is_empty() {
                    pending_space = true;
                }
            }
        }
    }
    out
}

/// The Unicode categories NFD leaves behind once a letter has been taken apart: Mn, Mc,
/// Me. Recognised by range rather than by pulling in a category table — every combining
/// mark lives in one of these blocks.
fn is_combining_mark(ch: char) -> bool {
    matches!(ch as u32,
        0x0300..=0x036F     // combining diacritical marks — the accents
        | 0x1AB0..=0x1AFF   // extended
        | 0x1DC0..=0x1DFF   // supplement
        | 0x20D0..=0x20FF   // for symbols
        | 0xFE20..=0xFE2F   // half marks
    )
}

/// The gaps in the collection: what is declared published and you do not have.
///
/// With no declared count we only report internal gaps — between the lowest and highest
/// volume owned — because beyond that we know nothing.
///
/// `claimed` are the volumes you do not have as files but whose chapters are here and say
/// so. A story reaches you as volumes, as loose chapters, or as both in turn. A volume
/// whose chapters are on your disk is not missing: you hold its content under another name,
/// and content is what you would actually be missing.
pub fn gaps(owned: &[f64], declared: Option<i32>, claimed: &[f64]) -> Vec<f64> {
    if owned.is_empty() {
        return Vec::new();
    }
    let whole: BTreeSet<i64> = owned
        .iter()
        .filter(|v| is_whole(**v))
        .map(|v| *v as i64)
        .collect();
    let Some(&lowest) = whole.iter().next() else {
        return Vec::new();
    };

    let mut held = whole.clone();
    held.extend(claimed.iter().filter(|v| is_whole(**v)).map(|v| *v as i64));

    let ceiling = match declared {
        Some(n) => n as i64,
        None => *held.iter().next_back().expect("held is not empty"),
    };

    (lowest..=ceiling)
        .filter(|n| !held.contains(n))
        .map(|n| n as f64)
        .collect()
}

fn is_whole(value: f64) -> bool {
    value.fract() == 0.0
}

/// How far apart two words are, counting insertions, deletions and substitutions.
///
/// Bounded: the moment every way of continuing costs more than `max`, it stops and says so
/// rather than finishing a computation whose answer is already "too far". That bound is
/// what makes it cheap enough to run against every indexed word.
pub fn distance(a: &str, b: &str, max: usize) -> usize {
    if a == b {
        return 0;
    }
    let a: Vec<char> = a.chars().collect();
    let b: Vec<char> = b.chars().collect();
    if a.len().abs_diff(b.len()) > max {
        return max + 1;
    }

    let mut previous: Vec<usize> = (0..=b.len()).collect();
    let mut current = vec![0usize; b.len() + 1];

    for i in 1..=a.len() {
        current[0] = i;
        let mut best = current[0];
        for j in 1..=b.len() {
            let cost = usize::from(a[i - 1] != b[j - 1]);
            current[j] = (current[j - 1] + 1)
                .min(previous[j] + 1)
                .min(previous[j - 1] + cost);
            best = best.min(current[j]);
        }
        // Every remaining path goes through this row: if all of it already costs too much,
        // nothing below can bring it back.
        if best > max {
            return max + 1;
        }
        std::mem::swap(&mut previous, &mut current);
    }
    previous[b.len()]
}

/// How wrong a word is allowed to be before it stops being the same word.
///
/// Short words get nothing: at three letters, one edit reaches a dozen unrelated words and
/// a suggestion that confident is worse than none. It loosens with length, because a long
/// word carries enough of itself to survive a slip.
pub fn tolerance(term: &str) -> usize {
    match term.chars().count() {
        0..=3 => 0,
        4..=5 => 1,
        6..=9 => 2,
        _ => 3,
    }
}

/// The display name, built from the levels that exist — and only from what adds something.
///
/// A universe already contained in the work's name says nothing twice. "Parasite ·
/// Parasite · Édition Deluxe" and "Parasite · Parasite Reversi" both repeat themselves;
/// "Parasite · Édition Deluxe" and "Parasite Reversi" carry the same facts and read.
pub fn composed_name(universe: Option<&str>, work: &str, edition: Option<&str>) -> String {
    let worth_saying = universe
        .filter(|u| !u.trim().is_empty() && !name_contains(work, u))
        .map(str::to_string);

    [
        worth_saying.as_deref(),
        Some(work),
        edition.filter(|e| !e.is_empty()),
    ]
    .into_iter()
    .flatten()
    .collect::<Vec<_>>()
    .join(" · ")
}

/// Whole words, folded — so "Terres d'Arran · Elfes" keeps its universe, and an "Arran"
/// universe would not vanish behind an unrelated "Arrandelle".
fn name_contains(whole: &str, part: &str) -> bool {
    let needle = search_key(part);
    if needle.is_empty() {
        return false;
    }
    let haystack = search_key(whole);
    haystack
        .split(' ')
        .collect::<Vec<_>>()
        .windows(needle.split(' ').count())
        .any(|w| w.join(" ") == needle)
}

/// The closest indexed word to a term, or nothing when none is close enough.
///
/// A prefix costs nothing: matching the beginning of a word is what the ordinary search
/// already does, so it is not a miss to be forgiven but a hit to be preferred.
pub fn nearest<'a, I>(term: &str, candidates: I) -> Option<usize>
where
    I: IntoIterator<Item = &'a str>,
{
    let max = tolerance(term);
    let mut best: Option<usize> = None;

    for candidate in candidates {
        if candidate.starts_with(term) {
            return Some(0);
        }
        if max == 0 {
            continue;
        }
        let d = distance(term, candidate, max);
        if d <= max && best.is_none_or(|b| d < b) {
            best = Some(d);
            // Nothing beats one edit except an exact prefix, and that returns above.
            if d == 1 {
                break;
            }
        }
    }
    best
}
