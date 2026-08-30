//! What the server says about a file offered to it, before anything is filed.
//!
//! Nothing is written into the library until a client confirms, so the whole value of this
//! step is the sentence it comes back with: which series, how sure, and what the file says
//! about itself that does not hold together. A confident wrong answer here is a volume
//! written over.

use std::io::Write;
use std::sync::Arc;

use leaf_server::api::intake::{Confidence, Intake};
use leaf_server::metadata::sidecars::{self, EntryJson};
use leaf_server::scan::scanner::Scanner;
use leaf_server::store::Db;

fn jpeg() -> Vec<u8> {
    let mut buffer = image::RgbImage::new(60, 90);
    for (x, y, pixel) in buffer.enumerate_pixels_mut() {
        *pixel = image::Rgb([(x % 256) as u8, (y % 256) as u8, 128]);
    }
    let mut out = std::io::Cursor::new(Vec::new());
    image::DynamicImage::ImageRgb8(buffer)
        .write_to(&mut out, image::ImageFormat::Jpeg)
        .unwrap();
    out.into_inner()
}

fn archive_bytes(entry: Option<&EntryJson>) -> Vec<u8> {
    let mut zip = zip::ZipWriter::new(std::io::Cursor::new(Vec::new()));
    let options = zip::write::SimpleFileOptions::default();
    zip.start_file::<_, ()>("000.jpg", options).unwrap();
    zip.write_all(&jpeg()).unwrap();
    if let Some(entry) = entry {
        zip.start_file::<_, ()>("entry.json", options).unwrap();
        zip.write_all(&sidecars::write(entry).unwrap()).unwrap();
    }
    zip.finish().unwrap().into_inner()
}

struct World {
    dir: tempfile::TempDir,
    db: Arc<Db>,
}

impl World {
    fn new() -> Self {
        let dir = tempfile::tempdir().unwrap();
        std::fs::create_dir_all(dir.path().join("library")).unwrap();
        std::fs::create_dir_all(dir.path().join("inbox")).unwrap();
        let db = Arc::new(Db::open(&dir.path().join("index.sqlite")).unwrap());
        World { dir, db }
    }

    fn library(&self) -> std::path::PathBuf {
        self.dir.path().join("library")
    }

    /// A volume of `work`, numbered, inside `folder` under the library.
    fn volume(&self, folder: &str, name: &str, work: &str, number: f64) {
        let at = self.library().join(folder);
        std::fs::create_dir_all(&at).unwrap();
        std::fs::write(
            at.join(name),
            archive_bytes(Some(&EntryJson {
                leaf: Some(1),
                work: Some(work.into()),
                number: Some(number),
                ..Default::default()
            })),
        )
        .unwrap();
    }

    fn scan(&self) {
        Scanner::new(Arc::clone(&self.db), true)
            .scan(&[self.library()])
            .unwrap();
    }

    fn intake(&self) -> Intake {
        Intake::new(&self.dir.path().join("inbox"), Arc::clone(&self.db))
    }

    /// A file offered to the server, and what it says back.
    fn offer(&self, name: &str, entry: Option<&EntryJson>) -> leaf_server::api::intake::Proposal {
        self.intake()
            .receive(name, &archive_bytes(entry), 8 * 1024 * 1024)
            .expect("a proposal")
    }
}

#[test]
fn a_volume_of_a_work_the_library_knows_and_a_number_that_is_free() {
    let world = World::new();
    world.volume("Bleach", "Tome 1.cbz", "Bleach", 1.0);
    world.scan();

    let said = world.offer(
        "Tome 2.cbz",
        Some(&EntryJson {
            leaf: Some(1),
            work: Some("Bleach".into()),
            number: Some(2.0),
            ..Default::default()
        }),
    );

    assert_eq!(said.confidence, Confidence::Certain);
    assert!(said.reason.contains("Bleach"), "{}", said.reason);
    assert_eq!(said.candidates.len(), 1);
    assert!(said.replaces.is_none());
}

#[test]
fn a_number_already_taken_reads_as_a_replacement_and_names_what_it_would_replace() {
    // The one thing that must never happen quietly is a volume being written over.
    let world = World::new();
    world.volume("Bleach", "Tome 1.cbz", "Bleach", 1.0);
    world.scan();

    let said = world.offer(
        "Tome 1 (rescan).cbz",
        Some(&EntryJson {
            leaf: Some(1),
            work: Some("Bleach".into()),
            number: Some(1.0),
            ..Default::default()
        }),
    );

    assert_eq!(said.confidence, Confidence::Replacement);
    assert!(said.replaces.is_some(), "{said:?}");
}

#[test]
fn two_editions_of_one_name_leave_the_choice_to_the_client() {
    // A name is not an identity. Two printings of Bleach are two places this could go, and
    // guessing between them is how a volume lands in the wrong one.
    let world = World::new();
    std::fs::write(
        {
            let at = world.library().join("Bleach");
            std::fs::create_dir_all(&at).unwrap();
            at.join("work.json")
        },
        br#"{"leaf":1,"title":"Bleach"}"#,
    )
    .unwrap();
    for edition in ["Perfect Edition", "Édition Originale"] {
        let at = world.library().join("Bleach").join(edition);
        std::fs::create_dir_all(&at).unwrap();
        std::fs::write(
            at.join("edition.json"),
            format!(r#"{{"leaf":1,"name":"{edition}"}}"#).into_bytes(),
        )
        .unwrap();
        std::fs::write(at.join("Tome 1.cbz"), archive_bytes(Some(&EntryJson {
            leaf: Some(1),
            work: Some("Bleach".into()),
            number: Some(1.0),
            ..Default::default()
        })))
        .unwrap();
    }
    world.scan();

    let said = world.offer(
        "Tome 2.cbz",
        Some(&EntryJson {
            leaf: Some(1),
            work: Some("Bleach".into()),
            number: Some(2.0),
            ..Default::default()
        }),
    );

    assert_eq!(said.confidence, Confidence::Ambiguous);
    assert!(said.reason.contains("2 editions"), "{}", said.reason);
    assert_eq!(said.candidates.len(), 2);
}

#[test]
fn a_file_that_says_nothing_about_itself_is_read_from_its_name() {
    let world = World::new();
    world.volume("Bleach", "Tome 1.cbz", "Bleach", 1.0);
    world.scan();

    let said = world.offer("Bleach - Tome 2.cbz", None);
    assert_eq!(said.name, "Bleach - Tome 2.cbz");
    assert!(said.size > 0);
}

#[test]
fn a_file_nothing_in_the_library_matches_is_said_to_be_unknown() {
    let world = World::new();
    world.volume("Bleach", "Tome 1.cbz", "Bleach", 1.0);
    world.scan();

    let said = world.offer(
        "Tome 1.cbz",
        Some(&EntryJson {
            leaf: Some(1),
            work: Some("Une série que personne n'a".into()),
            number: Some(1.0),
            ..Default::default()
        }),
    );
    assert_eq!(said.confidence, Confidence::Unknown);
    assert!(said.reason.contains("no series is named"), "{}", said.reason);
    // And the shelf is offered anyway: "I do not know" is more useful with a list beside
    // it than alone, and the client is the one that can choose.
    assert_eq!(said.candidates.len(), 1);
}

#[test]
fn what_the_file_says_about_itself_that_does_not_hold_together_comes_back_with_it() {
    // The same reading a scan does, run on the file in your hand rather than on a library
    // somebody else will have to fix later.
    let world = World::new();
    world.volume("Bleach", "Tome 1.cbz", "Bleach", 1.0);
    world.scan();

    let said = world.offer(
        "Tome 3.cbz",
        Some(&EntryJson {
            leaf: Some(1),
            work: Some("Bleach".into()),
            number: Some(3.0),
            kind: "TOME".into(),
            ..Default::default()
        }),
    );
    assert!(
        said.concerns.iter().any(|c| c.contains("neither VOLUME nor CHAPTER")),
        "{:?}",
        said.concerns
    );
}

#[test]
fn a_file_over_the_ceiling_never_reaches_the_inbox() {
    let world = World::new();
    let refused = world
        .intake()
        .receive("Énorme.cbz", &vec![0u8; 4096], 1024)
        .unwrap_err()
        .to_string();
    assert!(refused.contains("larger than"), "{refused}");
    assert_eq!(
        std::fs::read_dir(world.dir.path().join("inbox")).unwrap().count(),
        0,
        "nothing may be left behind"
    );
}

#[test]
fn what_is_waiting_can_be_listed_and_given_up_on() {
    let world = World::new();
    world.volume("Bleach", "Tome 1.cbz", "Bleach", 1.0);
    world.scan();

    let said = world.offer("Tome 2.cbz", None);
    let intake = world.intake();
    let waiting = intake.waiting().unwrap();
    assert_eq!(waiting.len(), 1);

    intake.abandon(&said.received).unwrap();
    assert!(intake.waiting().unwrap().is_empty());
}

#[test]
fn a_staging_that_is_never_kept_clears_itself() {
    // An interrupted connection must not leave half a file in the inbox: it says nothing,
    // and this path has no offset to resume from.
    let world = World::new();
    let intake = world.intake();
    {
        let staged = intake.staging_for("Tome 1.cbz").unwrap();
        std::fs::write(staged.path(), b"half a file").unwrap();
    }
    assert!(intake.waiting().unwrap().is_empty());
}


// -------------------------------------------------------------- filing the file

use leaf_server::api::intake::{FileRequest, OnCollision};

impl World {
    /// Offer a file, then file it into `series`, saying what to do about a name clash.
    fn file(
        &self,
        name: &str,
        series: &str,
        on_collision: Option<OnCollision>,
    ) -> Result<leaf_server::api::intake::Filed, anyhow::Error> {
        let said = self.offer(name, None);
        self.intake().file(
            &said.received,
            &FileRequest {
                series_id: series.to_string(),
                replaces_entry_id: None,
                on_collision,
            },
        )
    }
}

#[test]
fn a_name_already_in_the_series_is_refused_until_somebody_says_which_one_wins() {
    // The one thing that must never happen quietly is a volume being written over. A name is
    // not an identity, so the question goes back with what each of the two says about itself.
    let world = World::new();
    world.volume("Bleach", "Tome 1.cbz", "Bleach", 1.0);
    world.scan();
    let series: String = world
        .db
        .read(|cx| cx.query_one("SELECT id FROM edition", [], |r| r.get::<_, String>(0)))
        .unwrap()
        .unwrap();

    let refused = world.file("Tome 1.cbz", &series, None).unwrap_err();
    assert!(
        refused.downcast_ref::<leaf_server::api::intake::Collision>().is_some(),
        "{refused}"
    );
    // And nothing was written over.
    assert!(world.library().join("Bleach/Tome 1.cbz").is_file());
}

#[test]
fn told_to_keep_both_it_files_the_arriving_one_under_a_free_name() {
    let world = World::new();
    world.volume("Bleach", "Tome 1.cbz", "Bleach", 1.0);
    world.scan();
    let series: String = world
        .db
        .read(|cx| cx.query_one("SELECT id FROM edition", [], |r| r.get::<_, String>(0)))
        .unwrap()
        .unwrap();

    world
        .file("Tome 1.cbz", &series, Some(OnCollision::Rename))
        .expect("filed beside the other");
    assert!(world.library().join("Bleach/Tome 1.cbz").is_file());
    assert!(world.library().join("Bleach/Tome 1 (2).cbz").is_file());
}

#[test]
fn told_to_replace_it_writes_over_the_one_that_was_there() {
    let world = World::new();
    world.volume("Bleach", "Tome 1.cbz", "Bleach", 1.0);
    world.scan();
    let series: String = world
        .db
        .read(|cx| cx.query_one("SELECT id FROM edition", [], |r| r.get::<_, String>(0)))
        .unwrap()
        .unwrap();
    let before = std::fs::metadata(world.library().join("Bleach/Tome 1.cbz"))
        .unwrap()
        .len();

    world
        .file("Tome 1.cbz", &series, Some(OnCollision::Replace))
        .expect("filed over the other");
    // One file, not two.
    assert!(!world.library().join("Bleach/Tome 1 (2).cbz").exists());
    let after = std::fs::metadata(world.library().join("Bleach/Tome 1.cbz"))
        .unwrap()
        .len();
    assert!(after > 0 && before > 0);
}

#[test]
fn an_id_that_is_not_one_is_refused_before_anything_is_looked_up() {
    // The id names a folder under the inbox. Anything that is not one of ours is refused by
    // shape rather than by whether the folder happens to exist.
    let world = World::new();
    for id in ["../etc", "rcv_../..", "nothing", "rcv_a b"] {
        let refused = world
            .intake()
            .file(
                id,
                &FileRequest {
                    series_id: "e".into(),
                    replaces_entry_id: None,
                    on_collision: None,
                },
            )
            .unwrap_err()
            .to_string();
        assert!(refused.contains("invalid id") || refused.contains("unknown"), "{id}: {refused}");
    }
}
