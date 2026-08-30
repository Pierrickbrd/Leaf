//! Turning what a file says into what a reader sees.
//!
//! Including the shapes a real library turned out to contain — the ones nobody would have
//! thought to design for.

use leaf_server::metadata::label::{compose, parse};

#[test]
fn reads_the_common_form() {
    let read = parse("Chap.099 : Coup de sifflet");
    assert_eq!("Chap.099", read.label);
    assert_eq!(Some(99.0), read.number);
    assert_eq!(Some("Coup de sifflet".to_string()), read.title);
    assert_eq!("CHAPTER", read.kind);
}

#[test]
fn keeps_a_negative_number() {
    assert_eq!(
        Some(-108.0),
        parse("Chap.-108 : Turn Back the Pendulum").number
    );
}

#[test]
fn reads_a_decimal_number() {
    assert_eq!(Some(686.5), parse("Chapitre 686.5").number);
    assert_eq!(Some(45.5), parse("Chap.45,5 : Intermède").number);
}

#[test]
fn reads_shapes_it_was_never_taught() {
    assert_eq!(Some(46.0), parse("#46 : Vision").number);
    assert_eq!(Some(6.0), parse("Level 06 : Rencontre").number);
    assert_eq!(Some(6.0), parse("Z.6 : Retour").number);
}

#[test]
fn what_has_no_number_is_a_bonus() {
    let read = parse("Bonus : No Breaths From Hell");
    assert_eq!(None, read.number);
    assert_eq!("BONUS", read.kind);
    assert_eq!("Bonus", read.label);
    assert_eq!(Some("No Breaths From Hell".to_string()), read.title);
}

#[test]
fn a_number_in_the_title_is_not_the_chapter_number() {
    // The trap: looking for a number anywhere would make this chapter 0.
    let read = parse("Bonus : Chapitre 0");
    assert_eq!(None, read.number);
    assert_eq!("BONUS", read.kind);
}

#[test]
fn cuts_at_the_first_separator_not_the_last() {
    let read = parse("Chap.6 : micro : crack");
    assert_eq!("Chap.6", read.label);
    assert_eq!(Some("micro : crack".to_string()), read.title);
}

#[test]
fn keeps_the_raw_text_whatever_happens() {
    let raw = "Chap.099 : Coup de sifflet";
    assert_eq!(raw, parse(raw).raw);
    assert_eq!("n'importe quoi", parse("  n'importe quoi  ").raw);
}

#[test]
fn pads_the_integer_part_only() {
    assert_eq!(
        Some("Chap.099".to_string()),
        compose(Some("Chap.{n:000}"), Some(99.0))
    );
    assert_eq!(
        Some("Level.009.5".to_string()),
        compose(Some("Level.{n:000}"), Some(9.5))
    );
}

#[test]
fn renders_a_bare_placeholder_as_written() {
    assert_eq!(
        Some("Level.56.5".to_string()),
        compose(Some("Level.{n}"), Some(56.5))
    );
    assert_eq!(Some("#46".to_string()), compose(Some("#{n}"), Some(46.0)));
}

#[test]
fn keeps_the_sign_in_front_of_the_padding() {
    assert_eq!(
        Some("Chap.-108".to_string()),
        compose(Some("Chap.{n:000}"), Some(-108.0))
    );
}

#[test]
fn composes_nothing_without_a_pattern_or_without_a_number() {
    assert_eq!(None, compose(None, Some(99.0)));
    assert_eq!(None, compose(Some("Chap.{n}"), None));
    assert_eq!(None, compose(Some("   "), Some(99.0)));
}
