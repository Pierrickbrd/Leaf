//! Reading what the files say about themselves.

use leaf_server::archive::natural_order;
use leaf_server::metadata::legacy_comic_info as comic_info;
use leaf_server::metadata::sidecars::{self, EntryJson, WorkJson};

// ----------------------------------------------------------- natural order

#[test]
fn numbers_are_compared_as_numbers() {
    use std::cmp::Ordering::*;
    // Plain alphabetical order would put 10.jpg before 2.jpg.
    assert_eq!(Less, natural_order::compare("2.jpg", "10.jpg"));
    assert_eq!(Greater, natural_order::compare("100.jpg", "99.jpg"));
    // 007 and 7 are the same number.
    assert_eq!(Equal, natural_order::compare("007.jpg", "7.jpg"));
    assert_eq!(Less, natural_order::compare("page-2", "page-10"));
    assert_eq!(Less, natural_order::compare("a", "b"));
    assert_eq!(Equal, natural_order::compare("A", "a"));
}

#[test]
fn a_page_order_is_what_a_reader_would_expect() {
    let mut names = vec!["10.jpg", "9.jpg", "1.jpg", "002.jpg", "cover.jpg", "11.jpg"];
    names.sort_by(|a, b| natural_order::compare(a, b));
    assert_eq!(
        vec!["1.jpg", "002.jpg", "9.jpg", "10.jpg", "11.jpg", "cover.jpg"],
        names
    );
}

// ------------------------------------------------------------- ComicInfo

const REAL: &str = r#"<?xml version="1.0"?>
<ComicInfo xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
  <Series>Bleach</Series>
  <Number>28</Number>
  <Count>74</Count>
  <Title>Baron&#8217;s Lecture Full Course</Title>
  <Writer>Tite Kubo</Writer>
  <Publisher>Glénat</Publisher>
  <Genre>Action, Fantastique, Aventure</Genre>
  <LanguageISO>fr</LanguageISO>
  <Manga>YesAndRightToLeft</Manga>
  <StoryArc>Arc des Arrancars</StoryArc>
  <Year>2008</Year><Month>3</Month>
  <Chapters>
    <Chapter><Name>Chap.243 : The Knuckle &amp; The Arrow</Name></Chapter>
    <Chapter><Name>Chap.244 : Born In The Dark</Name></Chapter>
  </Chapters>
</ComicInfo>"#;

#[test]
fn reads_what_the_real_library_actually_contains() {
    let read = comic_info::read(REAL.as_bytes()).expect("a readable ComicInfo");

    assert_eq!(Some("Bleach".to_string()), read.series);
    assert_eq!(Some(28.0), read.entry.number);
    assert_eq!(Some(74), read.volume_count);
    assert_eq!(Some("Glénat".to_string()), read.publisher);
    assert_eq!(vec!["Action", "Fantastique", "Aventure"], read.genres);
    assert_eq!(Some("RIGHT_TO_LEFT".to_string()), read.reading_direction);
    assert_eq!(Some("Arc des Arrancars".to_string()), read.arc);
    assert_eq!(Some("2008-3".to_string()), read.entry.published_on);
}

#[test]
fn an_entity_is_a_character_and_not_a_gap() {
    let read = comic_info::read(REAL.as_bytes()).unwrap();
    // quick-xml hands an entity over as an event of its own, between the two halves of the
    // text it sits in. Ignoring it swallowed the character silently — and trimming each
    // half ate the spaces around it. Both were caught by reading the real archives.
    assert_eq!(
        Some("Chap.243 : The Knuckle & The Arrow".to_string()),
        read.entry.chapters[0].raw
    );
    // A numeric reference too: the title carries a typographic apostrophe as &#8217;.
    assert_eq!(
        Some("Baron’s Lecture Full Course".to_string()),
        read.entry.title
    );
}

#[test]
fn the_tag_nothing_else_reads_carries_the_chapters() {
    let read = comic_info::read(REAL.as_bytes()).unwrap();
    // Non-standard, and yet it holds 2 677 chapters in the real library. No start page is
    // declared anywhere: that is the data to create.
    assert_eq!(2, read.entry.chapters.len());
    assert!(read.entry.chapters.iter().all(|c| c.start_page.is_none()));
}

#[test]
fn a_chapter_name_does_not_become_the_volume_title() {
    let read = comic_info::read(REAL.as_bytes()).unwrap();
    // Treating the document as flat would let the last <Name> overwrite <Title>.
    assert_eq!(
        Some("Baron’s Lecture Full Course".to_string()),
        read.entry.title
    );
}

#[test]
fn nothing_readable_is_nothing_rather_than_a_failure() {
    assert!(comic_info::read(b"not xml at all").is_none());
    assert!(comic_info::read(b"").is_none());
    // A malformed document is one file described less well, not a scan that fails.
    assert!(comic_info::read(b"<ComicInfo><Series>Bleach").is_none());
}

// -------------------------------------------------------------- sidecars

#[test]
fn a_sidecar_from_a_later_version_still_reads() {
    // A field this build has never heard of must not stop it indexing the library.
    let json = br#"{ "leaf": 1, "title": "Bleach", "author": "Kubo",
                     "somethingFromTheFuture": { "deeply": ["nested"] } }"#;
    let work: WorkJson = sidecars::read(json).expect("a readable sidecar");
    assert_eq!(Some("Bleach".to_string()), work.title);
    assert_eq!(Some("Kubo".to_string()), work.author);
}

#[test]
fn the_legacy_singular_author_reads_as_one_element_of_authors() {
    // "author" is what a file already on disk carries; "authors" is what a new one writes.
    // A file written before "authors" existed must keep reading the same name it always did.
    let json = br#"{ "leaf": 1, "title": "Bleach", "author": "Kubo" }"#;
    let work: WorkJson = sidecars::read(json).expect("a readable sidecar");
    assert_eq!(vec!["Kubo".to_string()], work.authors());
}

#[test]
fn authors_wins_over_the_legacy_singular_when_both_are_there() {
    let json = br#"{ "leaf": 1, "title": "Bleach", "author": "Kubo",
                     "authors": ["Kubo", "Quelqu'un d'autre"] }"#;
    let work: WorkJson = sidecars::read(json).expect("a readable sidecar");
    assert_eq!(
        vec!["Kubo".to_string(), "Quelqu'un d'autre".to_string()],
        work.authors()
    );
}

#[test]
fn an_entry_declares_the_volume_its_chapters_came_from() {
    let json = br#"{ "leaf": 1, "work": "Bleach", "type": "CHAPTER", "number": 685,
                     "volume": 9,
                     "chapters": [ { "raw": "685", "volume": 9 } ] }"#;
    let entry: EntryJson = sidecars::read(json).expect("a readable entry");
    assert_eq!("CHAPTER", entry.kind);
    assert_eq!(Some(685.0), entry.number);
    // Not the entry's own number: this is which volume the content belongs to, and it is
    // what keeps that volume from being reported missing.
    assert_eq!(Some(9.0), entry.volume);
    assert_eq!(Some(9.0), entry.chapters[0].volume);
}

#[test]
fn an_entry_with_nothing_declared_is_a_volume() {
    let entry: EntryJson = sidecars::read(b"{}").expect("readable");
    assert_eq!("VOLUME", entry.kind);
    assert!(entry.chapters.is_empty());
}

#[test]
fn an_empty_label_means_the_title_stands_alone() {
    // Three cases in one field: absent, the pattern composes it; a string, that string is
    // used; an empty string, nothing is displayed.
    let json = br#"{ "chapters": [ { "raw": "x", "label": "" }, { "raw": "y" } ] }"#;
    let entry: EntryJson = sidecars::read(json).unwrap();
    assert_eq!(Some(String::new()), entry.chapters[0].label);
    assert_eq!(None, entry.chapters[1].label);
}
