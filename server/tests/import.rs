//! Editing records, taking a file in, and the bulk path.
//!
//! Built on real archives on a real disk: what these check is that the bytes end up in the
//! right place and that the index is never written directly — every edit goes into a sidecar
//! and comes back through a scan.

use std::io::{Read, Write};
use std::path::Path;
use std::sync::Arc;

mod common;
use common::writable;

use leaf_server::api::bulk_import::{
    BulkImport, CleanupRequest, ImportRequest, ManifestFile, ReceiveError, Scope,
};
use leaf_server::api::intake::{Collision, Confidence, FileRequest, Intake, OnCollision};
use leaf_server::api::local_drop::{DropRequest, LocalDrop};
use leaf_server::api::records::{EntryPatch, Records, SeriesPatch};
use leaf_server::metadata::sidecars::{self, ArcJson, EntryJson, WorkJson};
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

fn archive(path: &Path, sidecar: Option<&EntryJson>) {
    std::fs::create_dir_all(path.parent().unwrap()).unwrap();
    let mut zip = zip::ZipWriter::new(std::fs::File::create(path).unwrap());
    let options = zip::write::SimpleFileOptions::default();
    for i in 0..2 {
        zip.start_file::<_, ()>(format!("{i:03}.jpg"), options)
            .unwrap();
        zip.write_all(&jpeg()).unwrap();
    }
    if let Some(entry) = sidecar {
        zip.start_file::<_, ()>("entry.json", options).unwrap();
        zip.write_all(&sidecars::write(entry).unwrap()).unwrap();
    }
    zip.finish().unwrap();
}

/// Reads a named entry back out of a CBZ.
fn inside(path: &Path, name: &str) -> Option<Vec<u8>> {
    let mut zip = zip::ZipArchive::new(std::fs::File::open(path).ok()?).ok()?;
    let mut found = zip.by_name(name).ok()?;
    let mut bytes = Vec::new();
    found.read_to_end(&mut bytes).ok()?;
    Some(bytes)
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

    fn inbox(&self) -> std::path::PathBuf {
        self.dir.path().join("inbox")
    }

    fn scan(&self) {
        Scanner::new(Arc::clone(&self.db), true)
            .scan(&[self.library()])
            .expect("scanning");
    }

    fn one<T: rusqlite::types::FromSql>(&self, sql: &str) -> Option<T> {
        self.db
            .read(|cx| cx.query_one(sql, [], |r| r.get::<_, Option<T>>(0)))
            .unwrap()
            .flatten()
    }

    /// A work of two volumes, scanned.
    fn with_bleach(&self) -> String {
        let folder = self.library().join("Bleach");
        for number in 1..=2 {
            archive(
                &folder.join(format!("Tome {number}.cbz")),
                Some(&EntryJson {
                    leaf: Some(1),
                    work: Some("Bleach".into()),
                    number: Some(number as f64),
                    ..Default::default()
                }),
            );
        }
        self.scan();
        self.one::<String>("SELECT id FROM edition").unwrap()
    }
}

// ------------------------------------------------------------------ records

#[test]
fn an_edit_lands_on_the_disk_and_never_in_the_index() {
    let world = World::new();
    let series = world.with_bleach();

    let patched = Records::new(&world.db)
        .patch_series(
            &series,
            &SeriesPatch {
                title: Some("BLEACH".into()),
                author: Some("Tite Kubo".into()),
                ..Default::default()
            },
        )
        .expect("patching");

    assert!(patched);
    // On the disk, in work.json — which is the whole point.
    let written: WorkJson =
        sidecars::read(&std::fs::read(world.library().join("Bleach/work.json")).unwrap()).unwrap();
    assert_eq!(Some("BLEACH".to_string()), written.title);
    assert_eq!(Some("Tite Kubo".to_string()), written.author);
    // And not in the index: nothing has been rescanned yet.
    assert_eq!(None, world.one::<String>("SELECT title FROM work"));

    world.scan();
    assert_eq!(
        Some("BLEACH".to_string()),
        world.one::<String>("SELECT title FROM work")
    );
}

#[test]
fn a_sidecar_carries_what_was_said_and_nothing_else() {
    let world = World::new();
    let series = world.with_bleach();

    Records::new(&world.db)
        .patch_series(
            &series,
            &SeriesPatch {
                title: Some("BLEACH".into()),
                ..Default::default()
            },
        )
        .expect("patching");

    // These files are read and edited by hand. One touched field must not write out
    // fourteen nulls beside it, which is what writing every field at its default would do.
    let written = std::fs::read_to_string(world.library().join("Bleach/work.json")).unwrap();
    assert!(
        !written.contains("null"),
        "no field written for want of a value:\n{written}"
    );
    assert!(!written.contains("[]"), "no empty list either:\n{written}");
    assert!(written.contains("\"title\": \"BLEACH\""));
    assert!(written.contains("\"leaf\": 1"));
}

#[test]
fn an_unknown_series_is_said_so_rather_than_written_somewhere() {
    let world = World::new();
    world.with_bleach();
    let patched = Records::new(&world.db)
        .patch_series(
            "not-a-series",
            &SeriesPatch {
                title: Some("Rien".into()),
                ..Default::default()
            },
        )
        .expect("patching");
    assert!(!patched);
}

#[test]
fn an_implicit_editions_fields_go_down_into_the_work_file() {
    let world = World::new();
    let series = world.with_bleach();

    Records::new(&world.db)
        .patch_series(
            &series,
            &SeriesPatch {
                publisher: Some("Glénat".into()),
                volume_count: Some(74),
                ..Default::default()
            },
        )
        .expect("patching");

    // An edition.json dropped beside the volumes would flip how the folder is classified:
    // it would stop being a work and become an edition.
    assert!(!world.library().join("Bleach/edition.json").exists());
    let written: WorkJson =
        sidecars::read(&std::fs::read(world.library().join("Bleach/work.json")).unwrap()).unwrap();
    assert_eq!(Some("Glénat".to_string()), written.publisher);
    assert_eq!(Some(74), written.volume_count);
}

#[test]
fn arcs_are_replaced_whole() {
    let world = World::new();
    let series = world.with_bleach();
    let arcs = vec![ArcJson {
        name: "Soul Society".into(),
        unit: "CHAPTER".into(),
        from: 1.0,
        to: 183.0,
    }];

    assert!(Records::new(&world.db).set_arcs(&series, arcs).unwrap());

    world.scan();
    assert_eq!(
        Some("Soul Society".to_string()),
        world.one::<String>("SELECT name FROM arc")
    );
}

#[test]
fn an_entry_edit_is_written_inside_the_volume_without_re_encoding_a_page() {
    let world = World::new();
    world.with_bleach();
    let entry: String = world
        .one("SELECT id FROM entry ORDER BY volume_number LIMIT 1")
        .unwrap();
    let file = world.library().join("Bleach/Tome 1.cbz");
    let pages_before = inside(&file, "000.jpg").unwrap();

    let patched = Records::new(&world.db)
        .patch_entry(
            &entry,
            &EntryPatch {
                title: Some("L'agonie du soleil".into()),
                isbn: Some("978-2-7234-4234-9".into()),
                ..Default::default()
            },
        )
        .expect("patching");

    assert!(patched);
    let written: EntryJson = sidecars::read(&inside(&file, "entry.json").unwrap()).unwrap();
    assert_eq!(Some("L'agonie du soleil".to_string()), written.title);
    assert_eq!(Some(1.0), written.number, "the number it already had stays");
    // Byte for byte: the pages are copied still compressed, never decoded.
    assert_eq!(pages_before, inside(&file, "000.jpg").unwrap());
}

#[test]
fn a_stamp_that_would_change_nothing_leaves_the_file_alone() {
    let world = World::new();
    world.with_bleach();
    let entry: String = world
        .one("SELECT id FROM entry ORDER BY volume_number LIMIT 1")
        .unwrap();
    let file = world.library().join("Bleach/Tome 1.cbz");

    let records = Records::new(&world.db);
    assert!(records.stamp_entry(&entry).unwrap());
    let after_first = std::fs::metadata(&file).unwrap().modified().unwrap();
    assert!(records.stamp_entry(&entry).unwrap());

    // Otherwise every download would modify the file and trigger a full reanalysis on the
    // next scan — of every volume anyone had ever read.
    assert_eq!(
        after_first,
        std::fs::metadata(&file).unwrap().modified().unwrap()
    );
}

// ------------------------------------------------------------------- intake

fn staged(intake: &Intake, name: &str, entry: Option<&EntryJson>) -> std::path::PathBuf {
    let target = intake.staging_for(name).expect("staging");
    archive(target.path(), entry);
    // The staging clears itself unless something claims it, which here is the test.
    target.keep()
}

#[test]
fn a_file_that_names_a_known_work_is_certain_of_where_it_goes() {
    let world = World::new();
    world.with_bleach();
    let intake = Intake::new(&world.inbox(), Arc::clone(&world.db));

    let file = staged(
        &intake,
        "Tome 3.cbz",
        Some(&EntryJson {
            leaf: Some(1),
            work: Some("Bleach".into()),
            number: Some(3.0),
            ..Default::default()
        }),
    );
    let proposal = intake.propose_for(&file).expect("proposing");

    assert!(matches!(proposal.confidence, Confidence::Certain));
    assert_eq!(1, proposal.candidates.len());
    assert_eq!(None, proposal.replaces);
}

#[test]
fn a_number_that_is_taken_makes_it_a_replacement() {
    let world = World::new();
    world.with_bleach();
    let intake = Intake::new(&world.inbox(), Arc::clone(&world.db));

    let file = staged(
        &intake,
        "Tome 2.cbz",
        Some(&EntryJson {
            leaf: Some(1),
            work: Some("Bleach".into()),
            number: Some(2.0),
            ..Default::default()
        }),
    );
    let proposal = intake.propose_for(&file).expect("proposing");

    assert!(matches!(proposal.confidence, Confidence::Replacement));
    assert!(proposal.replaces.is_some());
    assert!(proposal.reason.contains("already taken"));
}

#[test]
fn a_file_that_names_nothing_is_a_question_with_every_series_offered() {
    let world = World::new();
    world.with_bleach();
    let intake = Intake::new(&world.inbox(), Arc::clone(&world.db));

    let file = staged(&intake, "Inconnu.cbz", None);
    let proposal = intake.propose_for(&file).expect("proposing");

    assert!(matches!(proposal.confidence, Confidence::Unknown));
    assert_eq!(1, proposal.candidates.len(), "the shelf is offered instead");
}

#[test]
fn a_file_that_came_from_here_names_the_entry_it_replaces() {
    let world = World::new();
    world.with_bleach();
    let entry: String = world
        .one("SELECT id FROM entry ORDER BY volume_number LIMIT 1")
        .unwrap();
    let intake = Intake::new(&world.inbox(), Arc::clone(&world.db));

    // What a download stamps on its way out: the round trip in one field.
    let file = staged(
        &intake,
        "retouche.cbz",
        Some(&EntryJson {
            leaf: Some(1),
            id: Some(entry.clone()),
            work: Some("Bleach".into()),
            number: Some(1.0),
            ..Default::default()
        }),
    );
    let proposal = intake.propose_for(&file).expect("proposing");

    assert!(matches!(proposal.confidence, Confidence::Replacement));
    assert_eq!(Some(entry), proposal.replaces);
    assert!(proposal.reason.contains("came from this library"));
}

#[test]
fn nothing_moves_until_it_is_confirmed_and_then_it_lands_stamped() {
    let world = World::new();
    let series = world.with_bleach();
    let intake = Intake::new(&world.inbox(), Arc::clone(&world.db));

    let file = staged(
        &intake,
        "Tome 3.cbz",
        Some(&EntryJson {
            leaf: Some(1),
            work: Some("Bleach".into()),
            number: Some(3.0),
            ..Default::default()
        }),
    );
    let proposal = intake.propose_for(&file).expect("proposing");
    // Still in the inbox: a proposal moves nothing.
    assert!(file.exists());
    assert!(!world.library().join("Bleach/Tome 3.cbz").exists());

    let filed = intake
        .file(
            &proposal.received,
            &FileRequest {
                series_id: series,
                replaces_entry_id: None,
                on_collision: None,
            },
        )
        .expect("filing");

    let landed = world.library().join("Bleach/Tome 3.cbz");
    assert!(landed.exists());
    assert!(!file.exists(), "the staging folder is cleared");
    assert!(!filed.replacement);
    // It arrives knowing what it is, so it can find its way home if it leaves again.
    let written: EntryJson = sidecars::read(&inside(&landed, "entry.json").unwrap()).unwrap();
    assert_eq!(Some("Bleach".to_string()), written.work);
    assert_eq!(Some(filed.entry_id), written.id);
}

#[test]
fn a_replacement_lands_on_the_file_it_replaces() {
    let world = World::new();
    let series = world.with_bleach();
    let entry: String = world
        .one("SELECT id FROM entry ORDER BY volume_number LIMIT 1")
        .unwrap();
    let intake = Intake::new(&world.inbox(), Arc::clone(&world.db));

    let file = staged(
        &intake,
        "un-tout-autre-nom.cbz",
        Some(&EntryJson {
            leaf: Some(1),
            work: Some("Bleach".into()),
            number: Some(1.0),
            ..Default::default()
        }),
    );
    let proposal = intake.propose_for(&file).expect("proposing");
    let filed = intake
        .file(
            &proposal.received,
            &FileRequest {
                series_id: series,
                replaces_entry_id: Some(entry),
                on_collision: None,
            },
        )
        .expect("filing");

    assert!(filed.replacement);
    // The path it takes over, not a second file beside it under a new name.
    assert!(filed.path.ends_with("Tome 1.cbz"));
    assert!(!world
        .library()
        .join("Bleach/un-tout-autre-nom.cbz")
        .exists());
}

#[test]
fn an_ongoing_series_raises_its_own_count_and_a_finished_one_asks() {
    let world = World::new();
    let series = world.with_bleach();
    // Two volumes there, four declared, still running.
    Records::new(&world.db)
        .patch_series(
            &series,
            &SeriesPatch {
                status: Some("ongoing".into()),
                volume_count: Some(2),
                ..Default::default()
            },
        )
        .expect("patching");
    world.scan();

    let intake = Intake::new(&world.inbox(), Arc::clone(&world.db));
    let arriving = |number: f64| EntryJson {
        leaf: Some(1),
        work: Some("Bleach".into()),
        number: Some(number),
        ..Default::default()
    };
    let file = staged(&intake, "Tome 3.cbz", Some(&arriving(3.0)));
    let proposal = intake.propose_for(&file).expect("proposing");
    let filed = intake
        .file(
            &proposal.received,
            &FileRequest {
                series_id: series.clone(),
                replaces_entry_id: None,
                on_collision: None,
            },
        )
        .expect("filing");

    // You do not have to edit the record before adding the volume that just came out.
    assert!(filed.note.unwrap().contains("raised from 2 to 3"));
    let written: WorkJson =
        sidecars::read(&std::fs::read(world.library().join("Bleach/work.json")).unwrap()).unwrap();
    assert_eq!(Some(3), written.volume_count);

    // Finished, and an extra volume arrives: it is filed, and it is said out loud rather
    // than the number being quietly moved.
    Records::new(&world.db)
        .patch_series(
            &series,
            &SeriesPatch {
                status: Some("completed".into()),
                ..Default::default()
            },
        )
        .expect("patching");
    world.scan();
    let file = staged(&intake, "Tome 9.cbz", Some(&arriving(9.0)));
    let proposal = intake.propose_for(&file).expect("proposing");
    let filed = intake
        .file(
            &proposal.received,
            &FileRequest {
                series_id: series,
                replaces_entry_id: None,
                on_collision: None,
            },
        )
        .expect("filing");

    assert!(filed.note.unwrap().contains("worth checking"));
}

#[test]
fn an_upload_that_is_never_claimed_clears_itself() {
    let world = World::new();
    let intake = Intake::new(&world.inbox(), Arc::clone(&world.db));

    let folder = {
        let staging = intake.staging_for("interrompu.cbz").expect("staging");
        archive(staging.path(), None);
        staging.path().parent().unwrap().to_path_buf()
    };

    // A client that hangs up mid-upload leaves the handler's future dropped where it stood,
    // so no error branch runs — only a destructor does.
    assert!(
        !folder.exists(),
        "the inbox must not keep what nobody claimed"
    );
}

#[test]
fn an_abandoned_intake_leaves_nothing_behind() {
    let world = World::new();
    world.with_bleach();
    let intake = Intake::new(&world.inbox(), Arc::clone(&world.db));
    let file = staged(&intake, "Tome 9.cbz", None);
    let proposal = intake.propose_for(&file).expect("proposing");

    intake.abandon(&proposal.received).expect("abandoning");

    assert!(!file.exists());
    assert!(intake
        .file(
            &proposal.received,
            &FileRequest {
                series_id: "whatever".into(),
                replaces_entry_id: None,
                on_collision: None,
            },
        )
        .is_err());
}

#[test]
fn a_name_that_climbs_out_of_the_inbox_is_cut_back_to_its_last_segment() {
    let world = World::new();
    let intake = Intake::new(&world.inbox(), Arc::clone(&world.db));

    // Not refused: reduced. A client that sends a path gets its file name, and the file
    // lands in the inbox like any other — never at the path it asked for.
    let staged = intake.staging_for("../../etc/passwd").expect("staging");
    assert!(staged.path().starts_with(world.inbox()));
    assert_eq!(Some("passwd".as_ref()), staged.path().file_name());

    // What is left of ".." once the last segment is taken is nothing at all.
    assert!(intake.staging_for("..").is_err());
    assert!(intake.staging_for("   ").is_err());
}

#[test]
fn an_upload_over_the_ceiling_is_refused() {
    let world = World::new();
    let intake = Intake::new(&world.inbox(), Arc::clone(&world.db));
    // An unbounded upload is a way to fill a disk that needs no bug at all, only patience.
    assert!(intake.receive("gros.cbz", &vec![0u8; 4096], 1024).is_err());
}

// --------------------------------------------------------------- local drop

#[test]
fn the_drop_hands_a_file_to_the_ordinary_intake() {
    let world = World::new();
    world.with_bleach();
    let folder = world.dir.path().join("drop");
    std::fs::create_dir_all(&folder).unwrap();
    archive(
        &folder.join("Tome 4.cbz"),
        Some(&EntryJson {
            leaf: Some(1),
            work: Some("Bleach".into()),
            number: Some(4.0),
            ..Default::default()
        }),
    );

    let intake = Arc::new(Intake::new(&world.inbox(), Arc::clone(&world.db)));
    let drop = LocalDrop::new(Some(folder.clone()), Arc::clone(&intake));

    let listing = drop.list();
    assert!(listing.enabled);
    assert_eq!(1, listing.files.len());
    assert_eq!("Tome 4.cbz", listing.files[0].name);

    let proposal = drop
        .receive(&DropRequest {
            name: "Tome 4.cbz".into(),
            consume: true,
        })
        .expect("taking it in");

    assert!(matches!(proposal.confidence, Confidence::Certain));
    // Consumed where it lay: that is the whole point of the short path.
    assert!(!folder.join("Tome 4.cbz").exists());
}

#[test]
fn the_drop_can_be_asked_to_leave_your_copy_alone() {
    let world = World::new();
    world.with_bleach();
    let folder = world.dir.path().join("drop");
    std::fs::create_dir_all(&folder).unwrap();
    archive(&folder.join("Copie.cbz"), None);

    let intake = Arc::new(Intake::new(&world.inbox(), Arc::clone(&world.db)));
    let drop = LocalDrop::new(Some(folder.clone()), intake);
    drop.receive(&DropRequest {
        name: "Copie.cbz".into(),
        consume: false,
    })
    .expect("taking it in");

    assert!(folder.join("Copie.cbz").exists());
}

#[test]
fn a_drop_that_is_not_configured_says_so_rather_than_pretending() {
    let world = World::new();
    let intake = Arc::new(Intake::new(&world.inbox(), Arc::clone(&world.db)));
    let drop = LocalDrop::new(None, intake);
    assert!(!drop.enabled());
    assert!(!drop.list().enabled);
    assert!(drop
        .receive(&DropRequest {
            name: "Quoi.cbz".into(),
            consume: true,
        })
        .is_err());
}

#[test]
fn a_drop_name_that_climbs_out_of_the_folder_is_refused() {
    let world = World::new();
    let folder = world.dir.path().join("drop");
    std::fs::create_dir_all(&folder).unwrap();
    let intake = Arc::new(Intake::new(&world.inbox(), Arc::clone(&world.db)));
    let drop = LocalDrop::new(Some(folder), intake);
    assert!(drop
        .receive(&DropRequest {
            name: "../library/Bleach/Tome 1.cbz".into(),
            consume: true,
        })
        .is_err());
}

// -------------------------------------------------------------- bulk import

const CEILING: u64 = 64 * 1024 * 1024;

fn manifest(files: &[(&str, u64)], scope: Scope) -> ImportRequest {
    ImportRequest {
        root: "Terres d'Arran".into(),
        files: files
            .iter()
            .map(|(path, size)| ManifestFile {
                path: (*path).into(),
                size: *size,
                checksum: None,
            })
            .collect(),
        scope,
    }
}

#[test]
fn an_import_asks_only_for_what_is_not_already_there() {
    let world = World::new();
    let bulk = BulkImport::new(&world.inbox(), &world.library());
    let there = world.library().join("Terres d'Arran/Nains 01.cbz");
    std::fs::create_dir_all(there.parent().unwrap()).unwrap();
    std::fs::write(&there, vec![0u8; 10]).unwrap();

    let opened = bulk
        .open(&manifest(
            &[("Nains 01.cbz", 10), ("Nains 02.cbz", 4)],
            Scope::Addition,
        ))
        .expect("opening");

    assert_eq!(vec!["Nains 02.cbz".to_string()], opened.to_send);
    assert_eq!(vec!["Nains 01.cbz".to_string()], opened.already_there);
    assert_eq!(4, opened.bytes_to_send);
}

#[test]
fn a_transfer_resumes_at_the_byte_it_stopped_on() {
    let world = World::new();
    let bulk = BulkImport::new(&world.inbox(), &world.library());
    let opened = bulk
        .open(&manifest(&[("Nains 01.cbz", 8)], Scope::Addition))
        .expect("opening");

    let held = bulk
        .receive(&opened.id, "Nains 01.cbz", 0, b"abcd", CEILING)
        .expect("first half");
    assert_eq!(4, held);

    // The client asks for the state to know where to pick up.
    let state = bulk.state(&opened.id).expect("state").expect("still open");
    assert_eq!(Some(&4), state.received.get("Nains 01.cbz"));
    assert_eq!(vec!["Nains 01.cbz".to_string()], state.missing);

    let held = bulk
        .receive(&opened.id, "Nains 01.cbz", 4, b"efgh", CEILING)
        .expect("second half");
    assert_eq!(8, held);
    assert!(bulk.state(&opened.id).unwrap().unwrap().missing.is_empty());
}

#[test]
fn an_offset_past_what_the_server_holds_says_what_it_holds() {
    let world = World::new();
    let bulk = BulkImport::new(&world.inbox(), &world.library());
    let opened = bulk
        .open(&manifest(&[("Nains 01.cbz", 8)], Scope::Addition))
        .expect("opening");
    bulk.receive(&opened.id, "Nains 01.cbz", 0, b"abcd", CEILING)
        .expect("first half");

    match bulk.receive(&opened.id, "Nains 01.cbz", 7, b"h", CEILING) {
        Err(ReceiveError::BadOffset(offset)) => assert_eq!(4, offset.received),
        _ => panic!("an impossible offset must say where to resume"),
    }
}

#[test]
fn committing_installs_what_arrived_and_leaves_what_did_not() {
    let world = World::new();
    let bulk = BulkImport::new(&world.inbox(), &world.library());
    let opened = bulk
        .open(&manifest(
            &[("Nains 01.cbz", 4), ("Nains 02.cbz", 4)],
            Scope::Addition,
        ))
        .expect("opening");
    bulk.receive(&opened.id, "Nains 01.cbz", 0, b"abcd", CEILING)
        .expect("whole");
    bulk.receive(&opened.id, "Nains 02.cbz", 0, b"ab", CEILING)
        .expect("half");

    let result = bulk.commit(&opened.id).expect("committing");

    assert_eq!(1, result.installed);
    assert!(world.library().join("Terres d'Arran/Nains 01.cbz").exists());
    assert!(!world.library().join("Terres d'Arran/Nains 02.cbz").exists());
    // Stopping in the middle is a decision, not an accident: what arrived whole is in the
    // library, and what is still to come is named rather than merely absent.
    assert_eq!(vec!["Nains 02.cbz".to_string()], result.pending);
    assert!(result.open, "and the session is there to finish against");

    // The session stays, because something was left aside: the half that did arrive can be
    // finished against it rather than the whole folder being sent again.
    let state = bulk.state(&opened.id).unwrap().expect("still open");
    assert_eq!(vec!["Nains 02.cbz".to_string()], state.missing);
    bulk.receive(&opened.id, "Nains 02.cbz", 2, b"cd", CEILING)
        .expect("the rest");
    let result = bulk.commit(&opened.id).expect("committing again");
    assert_eq!(1, result.installed);
    assert!(result.pending.is_empty());
    assert!(
        !result.open,
        "everything went home, so there is nothing left to hold"
    );
    // Everything arrived: now it is gone.
    assert!(bulk.state(&opened.id).unwrap().is_none());
}

#[test]
fn a_file_that_travelled_wrong_is_left_aside_rather_than_installed() {
    let world = World::new();
    let bulk = BulkImport::new(&world.inbox(), &world.library());
    let mut request = manifest(&[("Nains 01.cbz", 4)], Scope::Addition);
    request.files[0].checksum = Some("not the digest of abcd".into());
    let opened = bulk.open(&request).expect("opening");
    bulk.receive(&opened.id, "Nains 01.cbz", 0, b"abcd", CEILING)
        .expect("whole");

    let result = bulk.commit(&opened.id).expect("committing");

    // A volume that arrived wrong is worse than one that did not arrive, because nothing
    // afterwards would tell you.
    assert_eq!(0, result.installed);
    assert_eq!(vec!["Nains 01.cbz".to_string()], result.corrupt);
    assert!(!world.library().join("Terres d'Arran/Nains 01.cbz").exists());

    // And the bad bytes are gone, so the client is told to send them again rather than
    // being told there is nothing left to send while every commit goes on refusing it.
    let state = bulk.state(&opened.id).unwrap().expect("still open");
    assert_eq!(vec!["Nains 01.cbz".to_string()], state.missing);

    // Announce the right one, and the same bytes go in.
    let mut request = manifest(&[("Nains 01.cbz", 4)], Scope::Addition);
    request.files[0].checksum =
        Some("88d4266fd4e6338d13b845fcf289579d209c897823b9217da3e161936f031589".into());
    let opened = bulk.open(&request).expect("opening");
    bulk.receive(&opened.id, "Nains 01.cbz", 0, b"abcd", CEILING)
        .expect("whole");
    assert_eq!(1, bulk.commit(&opened.id).expect("committing").installed);
}

#[test]
fn an_addition_says_nothing_about_what_it_did_not_bring() {
    let world = World::new();
    let bulk = BulkImport::new(&world.inbox(), &world.library());
    let stranger = world.library().join("Terres d'Arran/Elfes 01.cbz");
    std::fs::create_dir_all(stranger.parent().unwrap()).unwrap();
    std::fs::write(&stranger, b"xy").unwrap();

    let opened = bulk
        .open(&manifest(&[("Nains 01.cbz", 4)], Scope::Addition))
        .expect("opening");
    bulk.receive(&opened.id, "Nains 01.cbz", 0, b"abcd", CEILING)
        .expect("whole");
    let result = bulk.commit(&opened.id).expect("committing");

    // The desktop is a workbench, not a mirror: a series imported and then deleted locally
    // can no longer be announced in full.
    assert!(result.orphans.is_empty());
    assert!(stranger.exists());
}

#[test]
fn a_complete_manifest_names_the_orphans_and_deletes_nothing() {
    let world = World::new();
    let bulk = BulkImport::new(&world.inbox(), &world.library());
    let stranger = world.library().join("Terres d'Arran/Elfes 01.cbz");
    std::fs::create_dir_all(stranger.parent().unwrap()).unwrap();
    std::fs::write(&stranger, b"xy").unwrap();

    let opened = bulk
        .open(&manifest(&[("Nains 01.cbz", 4)], Scope::Complete))
        .expect("opening");
    bulk.receive(&opened.id, "Nains 01.cbz", 0, b"abcd", CEILING)
        .expect("whole");
    let result = bulk.commit(&opened.id).expect("committing");

    assert_eq!(vec!["Elfes 01.cbz".to_string()], result.orphans);
    // Reported, and still there: a wrong manifest never destroys anything.
    assert!(stranger.exists());
}

#[test]
fn deletion_happens_only_on_an_explicit_by_name_order() {
    let world = World::new();
    let bulk = BulkImport::new(&world.inbox(), &world.library());
    let file = world.library().join("Terres d'Arran/Elfes 01.cbz");
    std::fs::create_dir_all(file.parent().unwrap()).unwrap();
    std::fs::write(&file, b"xy").unwrap();

    let removed = bulk
        .cleanup(&CleanupRequest {
            root: "Terres d'Arran".into(),
            files: vec!["Elfes 01.cbz".into(), "Jamais vu.cbz".into()],
        })
        .expect("cleaning");

    assert_eq!(vec!["Elfes 01.cbz".to_string()], removed);
    assert!(!file.exists());
}

#[test]
fn a_path_that_climbs_out_of_its_root_is_refused() {
    let world = World::new();
    let bulk = BulkImport::new(&world.inbox(), &world.library());
    let opened = bulk
        .open(&manifest(&[("Nains 01.cbz", 4)], Scope::Addition))
        .expect("opening");

    // Without this a "../../etc" would write wherever it liked on the server.
    assert!(bulk
        .receive(&opened.id, "../../escaped.cbz", 0, b"abcd", CEILING)
        .is_err());
    assert!(bulk
        .cleanup(&CleanupRequest {
            root: "../library".into(),
            files: vec!["whatever".into()],
        })
        .is_err());
    assert!(bulk.state("../../etc").is_err());
}

#[test]
fn a_chunk_over_the_ceiling_is_refused() {
    let world = World::new();
    let bulk = BulkImport::new(&world.inbox(), &world.library());
    let opened = bulk
        .open(&manifest(&[("Nains 01.cbz", 4096)], Scope::Addition))
        .expect("opening");

    assert!(bulk
        .receive(&opened.id, "Nains 01.cbz", 0, &vec![0u8; 4096], 1024)
        .is_err());
}

#[test]
fn an_abandoned_import_is_gone() {
    let world = World::new();
    let bulk = BulkImport::new(&world.inbox(), &world.library());
    let opened = bulk
        .open(&manifest(&[("Nains 01.cbz", 4)], Scope::Addition))
        .expect("opening");
    bulk.receive(&opened.id, "Nains 01.cbz", 0, b"abcd", CEILING)
        .expect("whole");

    bulk.abandon(&opened.id).expect("abandoning");

    assert!(bulk.state(&opened.id).unwrap().is_none());
    assert!(!world.library().join("Terres d'Arran/Nains 01.cbz").exists());
}

/// Two different volumes can be called the same thing.
///
/// Which one wins is not the server's to decide, and it is not the file name's either. The
/// server compares what the two files say about themselves and hands the question back.
#[test]
fn a_name_already_taken_is_a_question_and_not_a_silent_rename() {
    let world = World::new();
    let series = world.with_bleach();
    let existing = world.library().join("Bleach/Tome 1.cbz");
    let before = std::fs::read(&existing).unwrap().len();
    let intake = Intake::new(&world.inbox(), Arc::clone(&world.db));

    // Volume 9, in a file that happens to be named like volume 1 — which is what comes out
    // of a downloads folder more often than not.
    let file = staged(
        &intake,
        "Tome 1.cbz",
        Some(&EntryJson {
            leaf: Some(1),
            work: Some("Bleach".into()),
            number: Some(9.0),
            title: Some("Fourteen Days of Bleach".into()),
            ..Default::default()
        }),
    );
    let proposal = intake.propose_for(&file).expect("proposing");
    assert!(
        matches!(proposal.confidence, Confidence::Certain),
        "9 is free: the number says nothing about the collision"
    );

    let refused = intake
        .file(
            &proposal.received,
            &FileRequest {
                series_id: series.clone(),
                replaces_entry_id: None,
                on_collision: None,
            },
        )
        .expect_err("nobody has said which one wins");
    let collision = refused
        .downcast_ref::<Collision>()
        .expect("it says what it found");

    assert!(!collision.same_volume, "volume 9 is not volume 1");
    assert_eq!(Some(9.0), collision.arriving.number);
    assert_eq!(Some(1.0), collision.occupies.number);
    assert_eq!(
        vec!["work".to_string(), "type".to_string()],
        collision.agrees
    );
    assert!(!collision.identical);
    assert_eq!("Tome 1 (2).cbz", collision.would_become);
    assert!(collision.entry_id.is_some(), "the index knows the occupant");
    // And nothing has moved.
    assert_eq!(before, std::fs::read(&existing).unwrap().len());

    let filed = intake
        .file(
            &proposal.received,
            &FileRequest {
                series_id: series,
                replaces_entry_id: None,
                on_collision: Some(OnCollision::Rename),
            },
        )
        .expect("filing");

    assert!(filed.renamed);
    assert!(filed.path.ends_with("Tome 1 (2).cbz"), "{}", filed.path);
    assert_eq!(before, std::fs::read(&existing).unwrap().len());
}

/// A volume corrected locally and brought back is the same volume.
///
/// This is the case that keeps the title out of the identity test: the desktop is a
/// workbench, and fixing a title is one of the things it is for. Called another volume, it
/// would file a duplicate beside the one it was meant to correct.
#[test]
fn a_retouched_volume_is_still_that_volume_however_its_title_reads_now() {
    let world = World::new();
    let series = world.with_bleach();
    let intake = Intake::new(&world.inbox(), Arc::clone(&world.db));

    let file = staged(
        &intake,
        "Tome 1.cbz",
        Some(&EntryJson {
            leaf: Some(1),
            work: Some("Bleach".into()),
            number: Some(1.0),
            title: Some("L'agonie du soleil".into()),
            summary: Some("Ajouté après coup.".into()),
            ..Default::default()
        }),
    );
    let proposal = intake.propose_for(&file).expect("proposing");
    let refused = intake
        .file(
            &proposal.received,
            &FileRequest {
                series_id: series.clone(),
                replaces_entry_id: None,
                on_collision: None,
            },
        )
        .expect_err("still a question — it is a destruction either way");
    let collision = refused.downcast_ref::<Collision>().expect("described");

    assert!(
        collision.same_volume,
        "a title added where there was none does not make it another volume"
    );

    let filed = intake
        .file(
            &proposal.received,
            &FileRequest {
                series_id: series,
                replaces_entry_id: None,
                on_collision: Some(OnCollision::Replace),
            },
        )
        .expect("filing");

    assert!(!filed.renamed);
    assert!(
        filed.replacement,
        "a file was written over, whichever way that was decided"
    );
    assert!(filed.path.ends_with("Bleach/Tome 1.cbz"), "{}", filed.path);
    world.scan();
    assert_eq!(
        Some("L'agonie du soleil".to_string()),
        world.one::<String>("SELECT title FROM entry WHERE volume_number = 1"),
        "the corrected file is the one in the library now"
    );
}

/// Two volumes agreeing on their title and on nothing else.
#[test]
fn a_shared_title_is_reported_and_never_taken_for_an_identity() {
    let world = World::new();
    let series = world.with_bleach();
    let intake = Intake::new(&world.inbox(), Arc::clone(&world.db));

    // The occupant gets a title...
    Records::new(&world.db)
        .patch_entry(
            &world
                .one::<String>("SELECT id FROM entry WHERE volume_number = 1")
                .unwrap(),
            &EntryPatch {
                title: Some("Banana split".into()),
                ..Default::default()
            },
        )
        .expect("patching");

    // ...and something from another work arrives under the same name with the same title.
    let file = staged(
        &intake,
        "Tome 1.cbz",
        Some(&EntryJson {
            leaf: Some(1),
            work: Some("Naruto".into()),
            number: Some(4.0),
            title: Some("Banana split".into()),
            ..Default::default()
        }),
    );
    let proposal = intake.propose_for(&file).expect("proposing");
    let refused = intake
        .file(
            &proposal.received,
            &FileRequest {
                series_id: series,
                replaces_entry_id: None,
                on_collision: None,
            },
        )
        .expect_err("a question");
    let collision = refused.downcast_ref::<Collision>().expect("described");

    assert!(
        !collision.same_volume,
        "the title cannot make them one volume"
    );
    assert!(
        collision.agrees.contains(&"title".to_string()),
        "but it is worth seeing before deciding: {:?}",
        collision.agrees
    );
}

/// The same bytes on both sides settles it.
#[test]
fn two_files_holding_the_same_bytes_are_said_to_be_identical() {
    let world = World::new();
    let series = world.with_bleach();
    let intake = Intake::new(&world.inbox(), Arc::clone(&world.db));

    let staging = intake.staging_for("Tome 1.cbz").expect("staging");
    std::fs::copy(world.library().join("Bleach/Tome 1.cbz"), staging.path()).unwrap();
    let file = staging.keep();

    let proposal = intake.propose_for(&file).expect("proposing");
    let refused = intake
        .file(
            &proposal.received,
            &FileRequest {
                series_id: series,
                replaces_entry_id: None,
                on_collision: None,
            },
        )
        .expect_err("a question");
    let collision = refused.downcast_ref::<Collision>().expect("described");

    // Whatever the sidecars say, there is nothing to lose by replacing and nothing to gain
    // by keeping two.
    assert!(collision.identical);
    assert!(collision.same_volume);
}

/// The entry named must belong to the series named.
#[test]
fn a_replacement_cannot_point_at_another_series_file() {
    let world = World::new();
    let series = world.with_bleach();
    // A second series, and one of its volumes as the thing to overwrite.
    let other = world.library().join("Naruto");
    archive(
        &other.join("Tome 1.cbz"),
        Some(&EntryJson {
            leaf: Some(1),
            work: Some("Naruto".into()),
            number: Some(1.0),
            ..Default::default()
        }),
    );
    world.scan();
    let elsewhere: String = world
        .one(
            "SELECT x.id FROM entry x JOIN edition e ON e.id = x.edition_id
              JOIN work w ON w.id = e.work_id WHERE w.name = 'Naruto'",
        )
        .unwrap();
    let victim = world.library().join("Naruto/Tome 1.cbz");
    let before = std::fs::read(&victim).unwrap().len();

    let intake = Intake::new(&world.inbox(), Arc::clone(&world.db));
    let file = staged(&intake, "Tome 1.cbz", None);
    let proposal = intake.propose_for(&file).expect("proposing");

    // Series Bleach, entry from Naruto: the same destruction the naming rule prevents,
    // arriving by another door.
    let refused = intake.file(
        &proposal.received,
        &FileRequest {
            series_id: series,
            replaces_entry_id: Some(elsewhere),
            on_collision: None,
        },
    );

    assert!(refused.is_err());
    assert_eq!(before, std::fs::read(&victim).unwrap().len());
}

#[test]
#[cfg(unix)]
fn a_link_back_to_a_parent_is_a_leaf_and_not_a_loop() {
    let world = World::new();
    let bulk = BulkImport::new(&world.inbox(), &world.library());
    let root = world.library().join("Terres d'Arran");
    std::fs::create_dir_all(root.join("Nains")).unwrap();
    std::fs::write(root.join("Nains/Tome 1.cbz"), b"abcd").unwrap();
    std::os::unix::fs::symlink(&root, root.join("Nains/retour")).unwrap();

    let opened = bulk
        .open(&manifest(&[("Nains/Tome 1.cbz", 4)], Scope::Complete))
        .expect("opening");
    // A COMPLETE manifest sweeps the whole target to name what it did not bring, and
    // `is_dir()` follows symlinks: without this it would recurse until the stack gave out.
    let result = bulk.commit(&opened.id).expect("committing");

    assert!(result.orphans.is_empty(), "{:?}", result.orphans);
}

/// The scan's reading, run on the file in your hand.
#[test]
fn a_proposal_carries_what_the_file_says_that_does_not_hold_together() {
    let world = World::new();
    world.with_bleach();
    let intake = Intake::new(&world.inbox(), Arc::clone(&world.db));

    let file = staged(
        &intake,
        "Tome 7.cbz",
        Some(&EntryJson {
            leaf: Some(1),
            work: Some("Bleach".into()),
            number: Some(1.0),
            volume: Some(3.0),
            ..Default::default()
        }),
    );
    let proposal = intake.propose_for(&file).expect("proposing");

    // A scan reports this into a list nobody reads until something is already wrong. Here it
    // arrives at the one moment a person is looking straight at the file and can still say no.
    assert_eq!(2, proposal.concerns.len(), "{:?}", proposal.concerns);
    assert!(proposal
        .concerns
        .iter()
        .any(|c| c.contains("declares number 1") && c.contains("says 7")));
    assert!(proposal
        .concerns
        .iter()
        .any(|c| c.contains("loose chapter")));

    // A file that holds together says nothing.
    let sound = staged(
        &intake,
        "Tome 8.cbz",
        Some(&EntryJson {
            leaf: Some(1),
            work: Some("Bleach".into()),
            number: Some(8.0),
            ..Default::default()
        }),
    );
    assert!(intake.propose_for(&sound).unwrap().concerns.is_empty());
}

// ------------------------------------------------------- what is left waiting

/// Nothing sweeps the inbox, so what sits in it has to be reachable.
#[test]
fn a_proposal_nobody_answered_can_at_least_be_found() {
    let world = World::new();
    world.with_bleach();
    let intake = Intake::new(&world.inbox(), Arc::clone(&world.db));

    assert!(intake.waiting().unwrap().is_empty());

    // Offered, and then the window was closed on the modal.
    let file = staged(&intake, "Tome 9.cbz", None);
    let proposal = intake.propose_for(&file).expect("proposing");

    let waiting = intake.waiting().expect("listing");
    assert_eq!(1, waiting.len());
    assert_eq!(proposal.received, waiting[0].id);
    assert_eq!("Tome 9.cbz", waiting[0].name);
    assert!(waiting[0].size > 0);
    // Sent over the wire, so whoever sent it still has it.
    assert!(!waiting[0].only_copy);

    // And abandoning it is the same call it always was.
    intake.abandon(&proposal.received).expect("abandoning");
    assert!(intake.waiting().unwrap().is_empty());
}

/// A file consumed from the shared folder is the only copy, and says so.
#[test]
fn a_file_taken_out_of_the_drop_is_marked_as_the_only_copy() {
    let world = World::new();
    world.with_bleach();
    let folder = world.dir.path().join("drop");
    std::fs::create_dir_all(&folder).unwrap();
    for name in ["consomme.cbz", "copie.cbz"] {
        archive(&folder.join(name), None);
    }

    let intake = Arc::new(Intake::new(&world.inbox(), Arc::clone(&world.db)));
    let drop = LocalDrop::new(Some(folder.clone()), Arc::clone(&intake));
    drop.receive(&DropRequest {
        name: "consomme.cbz".into(),
        consume: true,
    })
    .expect("taking it in");
    drop.receive(&DropRequest {
        name: "copie.cbz".into(),
        consume: false,
    })
    .expect("taking it in");

    let waiting = intake.waiting().expect("listing");
    let of = |name: &str| {
        waiting
            .iter()
            .find(|w| w.name == name)
            .unwrap_or_else(|| panic!("{name} is not waiting: {waiting:?}"))
    };
    // Consumed means it left your folder: abandoning this one does not send you back to a
    // file you still have.
    assert!(of("consomme.cbz").only_copy);
    assert!(!folder.join("consomme.cbz").exists());
    assert!(!of("copie.cbz").only_copy);
    assert!(folder.join("copie.cbz").exists());
}

/// A session is a partial transfer: what it holds is what you would not have to send again.
#[test]
fn an_import_left_open_says_what_it_is_holding() {
    let world = World::new();
    let bulk = BulkImport::new(&world.inbox(), &world.library());
    assert!(bulk.waiting().unwrap().is_empty());

    let opened = bulk
        .open(&manifest(
            &[("Nains 01.cbz", 4), ("Nains 02.cbz", 4)],
            Scope::Addition,
        ))
        .expect("opening");
    bulk.receive(&opened.id, "Nains 01.cbz", 0, b"abcd", CEILING)
        .expect("one");
    bulk.receive(&opened.id, "Nains 02.cbz", 0, b"ab", CEILING)
        .expect("half of the other");

    let waiting = bulk.waiting().expect("listing");
    assert_eq!(1, waiting.len());
    assert_eq!(opened.id, waiting[0].id);
    assert_eq!("Terres d'Arran", waiting[0].root);
    assert_eq!((1, 2), (waiting[0].complete, waiting[0].of));
    assert_eq!(6, waiting[0].bytes, "four bytes and two");
    assert!(waiting[0].last_touched_at > 0);

    // Every other route takes an id. Without the listing, a desktop that crashed took the
    // only way of reaching this with it.
    bulk.abandon(&opened.id).expect("abandoning");
    assert!(bulk.waiting().unwrap().is_empty());
}

/// Two series at once, and one of them stops in the middle of a volume.
///
/// The shape a real seeding takes. Sessions are independent, so one stopping says nothing
/// about the other, and the one that stopped offers both answers: install what arrived and
/// name the rest, or drop the lot.
#[test]
fn one_transfer_stopping_leaves_the_other_alone_and_offers_both_answers() {
    let world = World::new();
    let bulk = BulkImport::new(&world.inbox(), &world.library());

    let of = |root: &str, count: usize| ImportRequest {
        root: root.into(),
        files: (1..=count)
            .map(|n| ManifestFile {
                path: format!("Tome {n:02}.cbz"),
                size: 4,
                checksum: None,
            })
            .collect(),
        scope: Scope::Addition,
    };
    let bleach = bulk.open(&of("Bleach", 8)).expect("opening");
    let naruto = bulk.open(&of("Naruto", 8)).expect("opening");

    // Naruto arrives whole. Bleach stops halfway through its fifth volume.
    for n in 1..=8 {
        bulk.receive(&naruto.id, &format!("Tome {n:02}.cbz"), 0, b"abcd", CEILING)
            .expect("whole");
    }
    for n in 1..=4 {
        bulk.receive(&bleach.id, &format!("Tome {n:02}.cbz"), 0, b"abcd", CEILING)
            .expect("whole");
    }
    bulk.receive(&bleach.id, "Tome 05.cbz", 0, b"ab", CEILING)
        .expect("half");

    // Each session says only what it is holding.
    let open = bulk.waiting().expect("listing");
    let held = |root: &str| open.iter().find(|o| o.root == root).expect("open");
    assert_eq!((8, 8), (held("Naruto").complete, held("Naruto").of));
    assert_eq!((4, 8), (held("Bleach").complete, held("Bleach").of));

    // One: stop at the last whole volume. Four are in the library, four are named.
    let stopped = bulk.commit(&bleach.id).expect("committing");
    assert_eq!(4, stopped.installed);
    assert!(stopped.open);
    assert_eq!(
        vec![
            "Tome 05.cbz".to_string(),
            "Tome 06.cbz".into(),
            "Tome 07.cbz".into(),
            "Tome 08.cbz".into()
        ],
        stopped.pending
    );
    // The half that arrived is not sent again: the resume starts where it stopped.
    let state = bulk.state(&bleach.id).unwrap().expect("still open");
    assert_eq!(Some(&2), state.received.get("Tome 05.cbz"));
    bulk.receive(&bleach.id, "Tome 05.cbz", 2, b"cd", CEILING)
        .expect("the rest of it");
    for n in 6..=8 {
        bulk.receive(&bleach.id, &format!("Tome {n:02}.cbz"), 0, b"abcd", CEILING)
            .expect("whole");
    }
    let finished = bulk.commit(&bleach.id).expect("committing again");
    assert_eq!(4, finished.installed);
    assert!(!finished.open);

    // Two: drop the lot. Nothing of it reaches the library, and nothing of it is left.
    bulk.abandon(&naruto.id).expect("abandoning");
    assert!(bulk.waiting().unwrap().is_empty());
    assert!(!world.library().join("Naruto").exists());
    assert_eq!(
        8,
        std::fs::read_dir(world.library().join("Bleach"))
            .unwrap()
            .count()
    );
}

/// A file can change without changing length.
///
/// Correcting a title in an `entry.json` from "Tome 1" to "Tome 2" leaves the archive exactly
/// as long as it was. Compared on size alone, the import answers "already there" and never
/// asks for it — so the corrected volume never arrives, and nothing says why.
#[test]
fn a_changed_file_of_the_same_size_is_asked_for_when_a_checksum_says_so() {
    let world = World::new();
    let bulk = BulkImport::new(&world.inbox(), &world.library());
    let there = world.library().join("Terres d'Arran/Nains 01.cbz");
    std::fs::create_dir_all(there.parent().unwrap()).unwrap();
    std::fs::write(&there, b"Tome 1").unwrap();

    // Same length, different contents, and the sender says which contents it means.
    let digest = "6a5f9b6f8e2f4c3d";
    let mut request = manifest(&[("Nains 01.cbz", 6)], Scope::Addition);
    request.files[0].checksum = Some(digest.into());
    let opened = bulk.open(&request).expect("opening");
    assert_eq!(
        vec!["Nains 01.cbz".to_string()],
        opened.to_send,
        "the length matches and the contents do not"
    );

    // The real digest of what is there: nothing to send, and nothing read twice.
    let real = leaf_server::api::bulk_import::checksum(&there).expect("digesting");
    let mut request = manifest(&[("Nains 01.cbz", 6)], Scope::Addition);
    request.files[0].checksum = Some(real);
    let opened = bulk.open(&request).expect("opening");
    assert!(opened.to_send.is_empty(), "{:?}", opened.to_send);
    assert_eq!(vec!["Nains 01.cbz".to_string()], opened.already_there);

    // And with no checksum at all, the size is all there is to go on — said plainly rather
    // than pretended otherwise.
    let opened = bulk
        .open(&manifest(&[("Nains 01.cbz", 6)], Scope::Addition))
        .expect("opening");
    assert!(opened.to_send.is_empty());
}

// ------------------------------------------------------- the corners of a sweep

/// A bulk import against a library of its own, so the sweep has a whole tree to itself.
fn a_bulk() -> (tempfile::TempDir, leaf_server::api::bulk_import::BulkImport) {
    let dir = tempfile::tempdir().unwrap();
    std::fs::create_dir_all(dir.path().join("library")).unwrap();
    std::fs::create_dir_all(dir.path().join("inbox")).unwrap();
    let bulk = leaf_server::api::bulk_import::BulkImport::new(
        &dir.path().join("inbox"),
        &dir.path().join("library"),
    );
    (dir, bulk)
}

#[test]
fn a_manifest_path_that_walks_upwards_lands_inside_the_root_anyway() {
    use leaf_server::api::bulk_import::{ImportRequest, ManifestFile, Scope};

    let (dir, bulk) = a_bulk();
    let opened = bulk
        .open(&ImportRequest {
            root: "Bleach".into(),
            scope: Scope::Addition,
            files: vec![ManifestFile {
                path: "../../evade.cbz".into(),
                size: 2,
                checksum: None,
            }],
        })
        .expect("opened");

    // Refused outright rather than quietly flattened: a manifest that names a path outside
    // its own root is a manifest nobody should be acting on, whatever it would resolve to.
    let refused = bulk
        .writing_at(&opened.id, "../../evade.cbz", 0, 1024)
        .unwrap_err();
    assert!(
        format!("{refused:?}").contains("outside its root"),
        "{refused:?}"
    );
    assert!(!dir.path().parent().unwrap().join("evade.cbz").exists());
}

#[test]
fn a_write_starting_past_the_ceiling_is_refused_even_though_it_is_a_resume() {
    use leaf_server::api::bulk_import::{ImportRequest, ManifestFile, Scope};

    // Counted from the offset, so resuming at 95 % of a huge file is not mistaken for a
    // fresh one of that size — and is still refused when the offset alone is over.
    let (_dir, bulk) = a_bulk();
    let opened = bulk
        .open(&ImportRequest {
            root: "Bleach".into(),
            scope: Scope::Addition,
            files: vec![ManifestFile {
                path: "Tome 1.cbz".into(),
                size: 9_000_000_000,
                checksum: None,
            }],
        })
        .expect("opened");

    // Five bytes are already there, so the offset is the right one — and it is over a
    // ceiling of four.
    let target = bulk
        .writing_at(&opened.id, "Tome 1.cbz", 0, 1_000_000)
        .expect("a target");
    std::fs::create_dir_all(target.parent().unwrap()).unwrap();
    std::fs::write(&target, b"12345").unwrap();

    let refused = bulk.writing_at(&opened.id, "Tome 1.cbz", 5, 4).unwrap_err();
    assert!(
        format!("{refused:?}").contains("larger than"),
        "{refused:?}"
    );
}

#[test]
fn a_session_with_no_manifest_is_no_session() {
    let (dir, bulk) = a_bulk();
    // A folder under the inbox that is not an import is not an error: it is not ours.
    std::fs::create_dir_all(dir.path().join("inbox/imp_deadbeef")).unwrap();
    assert!(bulk.state("imp_deadbeef").unwrap().is_none());
    assert!(bulk.waiting().unwrap().is_empty());
}

#[test]
fn a_file_already_home_is_done_rather_than_missing() {
    use leaf_server::api::bulk_import::{ImportRequest, ManifestFile, Scope};

    // A commit that installed part of a manifest keeps the session for the rest, so a second
    // one meets files that are already home.
    let (dir, bulk) = a_bulk();
    let library = dir.path().join("library/Bleach");
    std::fs::create_dir_all(&library).unwrap();
    std::fs::write(library.join("Tome 1.cbz"), b"pk").unwrap();

    let opened = bulk
        .open(&ImportRequest {
            root: "Bleach".into(),
            scope: Scope::Addition,
            files: vec![ManifestFile {
                path: "Tome 1.cbz".into(),
                size: 2,
                checksum: None,
            }],
        })
        .expect("opened");

    let state = bulk.state(&opened.id).unwrap().expect("a state");
    assert_eq!(state.missing.len(), 0, "{state:?}");
    // Nothing left to send: it is there, at the right length.
    let result = bulk.commit(&opened.id).expect("committed");
    assert!(result.pending.is_empty(), "{result:?}");
}

#[test]
fn a_manifest_that_covers_the_whole_series_reports_what_it_does_not_mention() {
    use leaf_server::api::bulk_import::{ImportRequest, ManifestFile, Scope};

    // "Here is the whole series": whatever is not in it is reported as an orphan — named,
    // never removed, because deciding a file is unwanted is not the server's call.
    let (dir, bulk) = a_bulk();
    let target = dir.path().join("library/Bleach");
    std::fs::create_dir_all(target.join("Extras")).unwrap();
    std::fs::write(target.join("Tome 1.cbz"), b"pk").unwrap();
    std::fs::write(target.join("Extras/Bonus.cbz"), b"pk").unwrap();
    #[cfg(unix)]
    std::os::unix::fs::symlink(&target, target.join("retour")).unwrap();

    let opened = bulk
        .open(&ImportRequest {
            root: "Bleach".into(),
            scope: Scope::Complete,
            files: vec![ManifestFile {
                path: "Tome 1.cbz".into(),
                size: 2,
                checksum: None,
            }],
        })
        .expect("opened");

    let result = bulk.commit(&opened.id).expect("committed");
    assert_eq!(result.orphans, vec!["Extras/Bonus.cbz".to_string()]);
    // A symlink is a leaf and never a way back up: the sweep does not descend into it, so
    // a link to the folder it sits in is not an infinite tree of orphans.
    assert!(
        target.join("Extras/Bonus.cbz").is_file(),
        "nothing is removed"
    );
}

#[test]
fn a_tree_deeper_than_the_sweep_follows_stops_rather_than_walking_for_ever() {
    use leaf_server::api::bulk_import::{ImportRequest, Scope};

    let (dir, bulk) = a_bulk();
    let mut deep = dir.path().join("library/Bleach");
    for n in 0..12 {
        deep = deep.join(format!("{n}"));
    }
    std::fs::create_dir_all(&deep).unwrap();
    std::fs::write(deep.join("Trop loin.cbz"), b"pk").unwrap();

    let opened = bulk
        .open(&ImportRequest {
            root: "Bleach".into(),
            scope: Scope::Complete,
            files: Vec::new(),
        })
        .expect("opened");

    let result = bulk.commit(&opened.id).expect("committed");
    assert!(
        !result.orphans.iter().any(|o| o.contains("Trop loin")),
        "past the ceiling nothing is swept: {:?}",
        result.orphans
    );
}

#[test]
fn a_target_that_cannot_be_read_sweeps_to_nothing_rather_than_failing() {
    use leaf_server::api::bulk_import::{ImportRequest, Scope};

    let (dir, bulk) = a_bulk();
    let target = dir.path().join("library/Bleach");
    std::fs::create_dir_all(&target).unwrap();
    std::fs::write(target.join("Tome 1.cbz"), b"pk").unwrap();

    let opened = bulk
        .open(&ImportRequest {
            root: "Bleach".into(),
            scope: Scope::Complete,
            files: Vec::new(),
        })
        .expect("opened");

    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        std::fs::set_permissions(&target, std::fs::Permissions::from_mode(0o000)).unwrap();
        let result = bulk.commit(&opened.id).expect("committed");
        writable(&target);
        assert!(result.orphans.is_empty(), "{result:?}");
    }
}

#[test]
fn what_is_waiting_in_an_inbox_that_is_not_there_is_nothing() {
    let dir = tempfile::tempdir().unwrap();
    let bulk = leaf_server::api::bulk_import::BulkImport::new(
        &dir.path().join("no-such-inbox"),
        &dir.path().join("library"),
    );
    assert!(bulk.waiting().unwrap().is_empty());
}

#[test]
fn a_file_that_cannot_be_read_to_check_its_checksum_is_dropped_like_a_wrong_one() {
    use leaf_server::api::bulk_import::{ImportRequest, ManifestFile, Scope};

    // A checksum that is sent and never compared is worse than none, because it reads like
    // a guarantee. When the comparison itself cannot happen, the bytes are no more trusted
    // than bytes known to be wrong.
    let (dir, bulk) = a_bulk();
    let opened = bulk
        .open(&ImportRequest {
            root: "Bleach".into(),
            scope: Scope::Addition,
            files: vec![ManifestFile {
                path: "Tome 1.cbz".into(),
                size: 2,
                checksum: Some("0".repeat(64)),
            }],
        })
        .expect("opened");

    let target = bulk
        .writing_at(&opened.id, "Tome 1.cbz", 0, 1_000_000)
        .expect("a target");
    std::fs::create_dir_all(target.parent().unwrap()).unwrap();
    std::fs::write(&target, b"pk").unwrap();
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        std::fs::set_permissions(&target, std::fs::Permissions::from_mode(0o000)).unwrap();
    }

    let result = bulk.commit(&opened.id).expect("committed");
    assert_eq!(result.corrupt, vec!["Tome 1.cbz".to_string()], "{result:?}");
    assert_eq!(result.installed, 0);
    let _ = std::fs::remove_file(&target);
    let _ = dir;
}

#[test]
fn a_manifest_path_with_a_dot_in_the_middle_is_the_same_path_without_it() {
    use leaf_server::api::bulk_import::{ImportRequest, ManifestFile, Scope};

    let (_dir, bulk) = a_bulk();
    let opened = bulk
        .open(&ImportRequest {
            root: "Bleach".into(),
            scope: Scope::Addition,
            files: vec![ManifestFile {
                path: "Extras/./Bonus.cbz".into(),
                size: 2,
                checksum: None,
            }],
        })
        .expect("opened");

    let target = bulk
        .writing_at(&opened.id, "Extras/./Bonus.cbz", 0, 1_000_000)
        .expect("a target");
    assert!(target.ends_with("Extras/Bonus.cbz"), "{}", target.display());
}

#[test]
fn a_folder_under_the_inbox_that_is_not_an_import_is_walked_past() {
    let (dir, bulk) = a_bulk();
    std::fs::create_dir_all(dir.path().join("inbox/quelque-chose-d-autre")).unwrap();
    std::fs::write(dir.path().join("inbox/un-fichier"), b"x").unwrap();
    assert!(bulk.waiting().unwrap().is_empty());
}

#[test]
fn a_file_already_home_is_counted_as_done_in_what_is_waiting() {
    use leaf_server::api::bulk_import::{ImportRequest, ManifestFile, Scope};

    // A commit that installed part of a manifest keeps the session for the rest, so the
    // listing meets files that are already there. They are done, not missing.
    let (dir, bulk) = a_bulk();
    let library = dir.path().join("library/Bleach");
    std::fs::create_dir_all(&library).unwrap();
    std::fs::write(library.join("Tome 1.cbz"), b"pk").unwrap();

    bulk.open(&ImportRequest {
        root: "Bleach".into(),
        scope: Scope::Addition,
        files: vec![
            ManifestFile {
                path: "Tome 1.cbz".into(),
                size: 2,
                checksum: None,
            },
            ManifestFile {
                path: "Tome 2.cbz".into(),
                size: 2,
                checksum: None,
            },
        ],
    })
    .expect("opened");

    let waiting = bulk.waiting().unwrap();
    assert_eq!(waiting.len(), 1);
    assert_eq!(waiting[0].of, 2);
    assert_eq!(waiting[0].complete, 1, "the one already home is done");
}
