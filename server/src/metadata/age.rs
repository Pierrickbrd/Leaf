//! The age a rating stands for.
//!
//! `ageRating` is a free string, and stays one: "16+" at Kana, "T" at a comics publisher,
//! « 10 ans » on a French cover. A restricted profile needs a number to compare with its own,
//! so the number is read out of the string once, at the scan, and kept beside it — the
//! string is what a person wrote and what the screen shows, the number is what a filter can
//! put in a `WHERE`.
//!
//! **Nothing recognised is no age at all, never zero.** A series nobody classified is hidden
//! from a restricted profile, because the one mistake this exists to prevent is a child
//! opening something an adult never looked at. Guessing low on a word not in the list below
//! would be exactly that mistake, made quietly.

use crate::store::text::search_key;

/// The words publishers use instead of a number, folded the way `search_key` folds, with the
/// age each one means. « Tout public » is the French cover's « Everyone ».
const WORDS: [(&str, u8); 7] = [
    ("everyone", 0),
    ("tout public", 0),
    ("teen", 13),
    ("t", 13),
    ("mature", 17),
    ("m", 17),
    ("adults only", 18),
];

/// The age a rating stands for: its first number — « Mature 17+ » is 17, not « mature » —
/// otherwise one of the usual words, whatever their case, otherwise none.
pub fn age_of(rating: &str) -> Option<u8> {
    let digits: String = rating
        .chars()
        .skip_while(|c| !c.is_ascii_digit())
        .take_while(char::is_ascii_digit)
        .collect();
    if !digits.is_empty() {
        // A number too large to be an age is not one: « 2024 » is a year somebody typed in
        // the wrong field, and reading it as « 255 and over » would hide the series for a
        // reason nobody could find.
        return digits.parse().ok();
    }
    let folded = search_key(rating);
    WORDS
        .iter()
        .find(|(word, _)| *word == folded)
        .map(|(_, age)| *age)
}
