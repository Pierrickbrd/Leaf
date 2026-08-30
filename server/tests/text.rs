//! The pure functions: folding a title for search, and finding the holes in a collection.
//!
//! SQLite folds nothing on its own, and a French library makes that a daily problem rather
//! than an edge case.

use leaf_server::store::text::{composed_name, distance, gaps, nearest, search_key, tolerance};

// ------------------------------------------------------------------- folding

#[test]
fn folds_accents() {
    assert_eq!("la rose ecarlate", search_key("La Rose écarlate"));
    assert_eq!("haikyu", search_key("Haikyū !!"));
    assert_eq!("dans une meme arene", search_key("Dans une même arène"));
}

#[test]
fn drops_punctuation_and_case() {
    assert_eq!("lattaque des titans", search_key("L'Attaque des Titans"));
    assert_eq!("terres darran", search_key("Terres d'Arran"));
}

/// A title in its own script used to fold to nothing at all, which finds nothing and is
/// indistinguishable from a search nobody typed.
#[test]
fn keeps_letters_of_any_script() {
    assert_eq!("ハイキュー", search_key("ハイキュー!!"));
    assert_eq!("灌籃高手", search_key("灌籃高手"));
    assert_eq!("الرحلة", search_key("الرحلة"));
    assert_eq!("привет мир", search_key("Привет, Мир"));
    assert_eq!("γεια", search_key("ΓΕΙΑ"));
}

/// Kept as letters, not kept as anything. An emoji is a symbol, and a title is no easier to
/// find for carrying one.
#[test]
fn drops_what_is_not_a_letter() {
    assert_eq!("saga", search_key("Saga 🚀"));
    assert_eq!("blame", search_key("Blame!"));
}

/// The half of the old rule that had to survive: an accent still comes off, so what somebody
/// types on a keyboard without one still meets the title that has one.
#[test]
fn an_accent_still_comes_off_a_letter_that_has_one() {
    assert_eq!("haikyu", search_key("Haikyū"));
    assert_eq!("ete", search_key("été"));
    // The same word typed the other way round — e then a combining accent, which is what
    // some keyboards produce. Both fold to the same key or the search is a coin toss.
    assert_eq!(search_key("été"), search_key("e\u{301}te\u{301}"));
}

/// Ligatures are letters, so the general rule would keep them whole. They are folded first
/// on purpose: "ae" is what somebody types.
#[test]
fn ligatures_are_still_taken_apart() {
    assert_eq!("aeon", search_key("Æon"));
    assert_eq!("oeuvre", search_key("Œuvre"));
    assert_eq!("strasse", search_key("Straße"));
}

#[test]
fn keeps_digits() {
    assert_eq!(
        "chap 099 coup de sifflet",
        search_key("Chap.099 : Coup de sifflet")
    );
}

// ---------------------------------------------------------------------- gaps

#[test]
fn names_what_is_missing_up_to_what_is_announced() {
    assert_eq!(
        vec![8.0, 9.0],
        gaps(&[1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0], Some(9), &[])
    );
}

#[test]
fn finds_holes_in_the_middle() {
    assert_eq!(vec![3.0, 5.0], gaps(&[1.0, 2.0, 4.0, 6.0], Some(6), &[]));
}

#[test]
fn without_a_declared_count_it_only_knows_the_inside() {
    // Beyond the highest volume owned, nothing is known: claiming a gap would be a guess.
    assert_eq!(vec![3.0], gaps(&[1.0, 2.0, 4.0], None, &[]));
}

/// Both ends of this range are written by a person: a `volumeCount` in a work.json, and a
/// volume number read off a file name — `label` takes any run of digits it finds. A typo at
/// either end used to build every number in between, in a blocking task, on every GET
/// /series that included the row.
///
/// And then refusing to answer at all was its own wrong answer: an empty list does not
/// reach the client as "we cannot say", it is dropped by `dto` and arrives as the absent
/// field a *complete* collection sends. What the typo costs is the typo, not the volumes.
#[test]
fn a_count_nothing_could_have_published_is_disregarded_and_the_collection_still_answers() {
    // Volume 2 is missing whatever the work.json claims was published.
    assert_eq!(vec![2.0], gaps(&[1.0, 3.0], Some(2_000_000_000), &[]));
    // The same from the other end: one file whose name reads as volume 999999999 does not
    // get to set a ceiling for the three volumes it sits beside.
    assert_eq!(vec![2.0, 3.0], gaps(&[1.0, 4.0, 999_999_999.0], None, &[]));
    // A collection anybody could actually own still answers, either side of the edge.
    assert_eq!(vec![2.0], gaps(&[1.0, 3.0], Some(3), &[]));
    assert_eq!(9_999, gaps(&[1.0], Some(10_000), &[]).len());
    // Past it, the count is dropped and only what is held speaks — here, nothing missing.
    assert!(gaps(&[1.0], Some(10_002), &[]).is_empty());

    // And the low end, which looked harmless and was the worse of the two: a count below the
    // volumes actually held gave an empty range, `dto` drops an empty list, and an absent
    // `missingVolumes` is what a *complete* collection sends. So a nonsense number came back
    // as "nothing to find here" — the one answer this function must never give — while the
    // gaps between the volumes on the disk were sitting right there.
    assert_eq!(vec![2.0], gaps(&[1.0, 3.0], Some(0), &[]));
    assert_eq!(vec![2.0], gaps(&[1.0, 3.0], Some(-4), &[]));
    assert_eq!(vec![6.0], gaps(&[5.0, 7.0], Some(3), &[]));
}

#[test]
fn an_empty_collection_has_no_gaps() {
    assert!(gaps(&[], Some(47), &[]).is_empty());
}

#[test]
fn a_volume_whose_chapters_are_here_is_not_missing() {
    // Volumes 1 to 8 and 11 to 14 on disk out of 14 published: 9 and 10 are gaps.
    let owned: Vec<f64> = (1..=8).chain(11..=14).map(f64::from).collect();
    assert_eq!(vec![9.0, 10.0], gaps(&owned, Some(14), &[]));

    // The chapters of volume 9 arrived loose and say so. It is no longer missing — you
    // hold its content, and content is what you would actually be missing.
    assert_eq!(vec![10.0], gaps(&owned, Some(14), &[9.0]));
    assert!(gaps(&owned, Some(14), &[9.0, 10.0]).is_empty());
}

#[test]
fn a_claimed_volume_raises_the_ceiling_when_nothing_is_declared() {
    assert!(gaps(&[1.0, 2.0], None, &[]).is_empty());
    assert_eq!(vec![3.0], gaps(&[1.0, 2.0], None, &[4.0]));
}

#[test]
fn chapters_count_their_own_gaps() {
    // The other granularity, and it knows nothing about volumes.
    assert_eq!(vec![686.0], gaps(&[683.0, 684.0, 685.0, 687.0], None, &[]));
    // A bonus with no whole number of its own disturbs nothing.
    assert_eq!(
        vec![686.0],
        gaps(&[683.0, 685.5, 685.0, 684.0, 687.0], None, &[])
    );
}

// ------------------------------------------------------------- composed name

#[test]
fn shows_the_levels_that_exist() {
    assert_eq!("Bleach", composed_name(None, "Bleach", None));
    assert_eq!(
        "Dragon Ball · Perfect Edition",
        composed_name(None, "Dragon Ball", Some("Perfect Edition"))
    );
    assert_eq!(
        "Terres d'Arran · Elfes",
        composed_name(Some("Terres d'Arran"), "Elfes", None)
    );
}

#[test]
fn drops_a_universe_the_work_already_names() {
    assert_eq!(
        "Parasite · Édition Deluxe",
        composed_name(Some("Parasite"), "Parasite", Some("Édition Deluxe"))
    );
    assert_eq!(
        "Parasite",
        composed_name(Some("Parasite"), "Parasite", None)
    );
    // The spin-off says "Parasite" on its own: repeating it adds nothing.
    assert_eq!(
        "Parasite Reversi",
        composed_name(Some("Parasite"), "Parasite Reversi", None)
    );
}

#[test]
fn matches_whole_words_not_letters() {
    // "Arran" is inside "Arrandelle" as characters but is not one of its words.
    assert_eq!(
        "Arran · Arrandelle",
        composed_name(Some("Arran"), "Arrandelle", None)
    );
}

#[test]
fn ignores_accents_and_case_like_the_search_does() {
    assert_eq!(
        "Pokémon Écarlate",
        composed_name(Some("pokemon"), "Pokémon Écarlate", None)
    );
}

// --------------------------------------------------------------------- fuzzy

#[test]
fn distance_counts_the_edits() {
    assert_eq!(0, distance("oba", "oba", 2));
    assert_eq!(1, distance("oba", "ohba", 2));
    assert_eq!(1, distance("parasite", "parasit", 2));
    // One missing letter, so one edit — not two.
    assert_eq!(1, distance("assasination", "assassination", 3));
    // Two letters swapped costs two: this counts edits, and a swap is not one of them.
    assert_eq!(2, distance("elfes", "efles", 3));
}

#[test]
fn distance_gives_up_rather_than_finish_a_lost_cause() {
    // Anything past the bound comes back as "too far", not as a number nobody uses.
    assert!(distance("parasite", "koroquest", 2) > 2);
    assert!(distance("a", "abcdefghij", 3) > 3);
}

#[test]
fn short_words_are_not_forgiven() {
    // One edit on three letters reaches a dozen unrelated words: "oda" would suggest "ode",
    // "odo", "oga". A confident wrong guess is worse than none.
    assert_eq!(0, tolerance("oda"));
    assert_eq!(1, tolerance("elfes"));
    assert_eq!(2, tolerance("parasite"));
    assert_eq!(3, tolerance("assassination"));
}

#[test]
fn nearest_finds_the_closest_word_or_nothing() {
    let words = ["tsugumi", "oba", "death", "note", "thriller"];

    assert_eq!(Some(1), nearest("ohba", words));
    assert_eq!(
        Some(0),
        nearest("tsug", words),
        "a prefix is what the ordinary search does, not a miss"
    );
    assert_eq!(Some(1), nearest("thrilier", words));
    assert_eq!(None, nearest("parasite", words));
}
