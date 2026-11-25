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
    BulkImport, CleanupRequest, ImportRequest, ManifestFile, ManifestSidecar, ReceiveError, Scope,
};
use leaf_server::api::intake::{Collision, Confidence, FileRequest, Intake, OnCollision};
use leaf_server::api::local_drop::{DropRequest, LocalDrop};
use leaf_server::api::records::{EntryPatch, Records, SeriesPatch};
use leaf_server::metadata::sidecars::{self, ArcJson, EntryJson, WorkJson};
use leaf_server::scan::scanner::Scanner;
use leaf_server::store::repository::Ceiling;
use leaf_server::store::schema::DEFAULT_PROFILE;
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
                title: Some(Some("BLEACH".into())),
                author: Some(Some("Tite Kubo".into())),
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
                title: Some(Some("BLEACH".into())),
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
                title: Some(Some("Rien".into())),
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
                publisher: Some(Some("Glénat".into())),
                volume_count: Some(Some(74)),
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
        parent: None,
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
                title: Some(Some("L'agonie du soleil".into())),
                isbn: Some(Some("978-2-7234-4234-9".into())),
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
    assert!(!world.library().join("Bleach/bleach_tome_003.cbz").exists());

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

    let landed = world.library().join("Bleach/bleach_tome_003.cbz");
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
                status: Some(Some("ongoing".into())),
                volume_count: Some(Some(2)),
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
                status: Some(Some("completed".into())),
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

#[cfg(unix)]
#[test]
fn a_link_in_the_drop_is_neither_listed_nor_followed() {
    // The drop moves what it takes (`consume`), so a link left in it named a file anywhere the
    // server could read, and took that file into the library. Only what lies in the folder is
    // the folder's.
    let world = World::new();
    let folder = world.dir.path().join("drop");
    std::fs::create_dir_all(&folder).unwrap();
    let elsewhere = world.dir.path().join("secret.cbz");
    archive(&elsewhere, None);
    std::os::unix::fs::symlink(&elsewhere, folder.join("Lien.cbz")).unwrap();

    let intake = Arc::new(Intake::new(&world.inbox(), Arc::clone(&world.db)));
    let drop = LocalDrop::new(Some(folder), intake);

    assert!(drop.list().files.is_empty());
    assert!(drop
        .receive(&DropRequest {
            name: "Lien.cbz".into(),
            consume: true,
        })
        .is_err());
    assert!(elsewhere.exists(), "what the link named was taken");
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
                rewrite: None,
                ..Default::default()
            })
            .collect(),
        scope,
        sidecars: Vec::new(),
        ..Default::default()
    }
}

/// Each open import is a folder a client fills, and the only limit was the size of one file:
/// sessions opened without end each held what the disk had left.
#[test]
fn imports_left_open_are_counted_and_the_next_one_is_refused() {
    let (_dir, bulk) = a_bulk();
    let request = manifest(&[("Tome 1.cbz", 10)], Scope::Addition);
    for _ in 0..leaf_server::api::bulk_import::MAX_OPEN_IMPORTS {
        bulk.open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
            .expect("opening");
    }

    let refused = bulk.open(&request, DEFAULT_PROFILE, &Ceiling::NONE);
    assert!(refused.is_err(), "a sixty-fifth import was opened");

    // And abandoning one gives its place back.
    let waiting = bulk.waiting().expect("listing");
    bulk.abandon(&waiting[0].id).expect("abandoning");
    assert!(bulk.open(&request, DEFAULT_PROFILE, &Ceiling::NONE).is_ok());
}

/// The count and the place taken were two steps, and openings made together all saw room for
/// the last place: the limit held for one client at a time and for no more than that.
#[test]
fn openings_made_together_do_not_share_the_last_places() {
    let (_dir, bulk) = a_bulk();
    let request = manifest(&[("Tome 1.cbz", 10)], Scope::Addition);
    let places = leaf_server::api::bulk_import::MAX_OPEN_IMPORTS;

    let opened = std::thread::scope(|scope| {
        let attempts: Vec<_> = (0..places + 16)
            .map(|_| scope.spawn(|| bulk.open(&request, DEFAULT_PROFILE, &Ceiling::NONE).is_ok()))
            .collect();
        attempts
            .into_iter()
            .map(|attempt| attempt.join().expect("an opening"))
            .filter(|opened| *opened)
            .count()
    });

    assert_eq!(
        places, opened,
        "{opened} imports opened for {places} places"
    );
}

/// Down the model and not across the disk. The walk meets sidecars in whatever order the
/// filesystem lists them — measured on a real folder, « édition, série, édition, univers »,
/// which is the four levels shuffled and unreadable as a list of what will happen.
#[test]
fn what_a_folder_would_create_is_ordered_the_way_the_model_is() {
    let (_dir, bulk) = a_bulk();
    let declare = |path: &str, json: &str| ManifestSidecar {
        path: path.to_string(),
        json: json.to_string(),
    };
    let mut request = manifest(&[], Scope::Addition);
    request.root = "Dragon Ball".to_string();
    // Handed over in the order a walk of that folder produces, which is not the model's.
    request.sidecars = vec![
        declare(
            "Dragon Ball/Perfect Edition/edition.json",
            r#"{"leaf":1,"name":"Perfect Edition"}"#,
        ),
        declare(
            "Dragon Ball/work.json",
            r#"{"leaf":1,"title":"Dragon Ball"}"#,
        ),
        declare(
            "Dragon Ball/Édition originale/edition.json",
            r#"{"leaf":1,"name":"Édition originale"}"#,
        ),
        declare("universe.json", r#"{"leaf":1,"name":"Dragon Ball"}"#),
    ];

    let opened = bulk
        .open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect("an import");

    let said: Vec<(&str, &str)> = opened
        .creates
        .iter()
        .map(|c| (c.kind.as_str(), c.name.as_str()))
        .collect();
    assert_eq!(
        vec![
            ("UNIVERSE", "Dragon Ball"),
            ("WORK", "Dragon Ball"),
            ("EDITION", "Perfect Edition"),
            ("EDITION", "Édition originale"),
        ],
        said,
        "universe, then its work, then that work's editions"
    );
}

/// An identity is stamped by the scan and belongs to the library: it never travels in an
/// import.
///
/// Measured: a folder prepared on a laptop and never scanned has no `id` in its
/// `work.json`, and copying that over the library's own made the field come back `null`.
/// The next scan mints a new one, the prune takes the rows it replaces, and
/// `ON DELETE CASCADE` takes every reading position of that series with them — the exact
/// loss `scan::identity` exists to prevent, walked in through the import door.
#[test]
fn an_import_never_takes_the_identity_off_what_is_already_there() {
    let (dir, bulk) = a_bulk();
    let target = dir.path().join("library").join("Death Note");
    std::fs::create_dir_all(&target).unwrap();
    std::fs::write(
        target.join("work.json"),
        br#"{"leaf":1,"title":"Death Note","id":"2f680fdf43a78c4e"}"#,
    )
    .unwrap();

    let mut request = manifest(&[], Scope::Addition);
    request.root = "Death Note".to_string();
    request.sidecars = vec![ManifestSidecar {
        path: "work.json".to_string(),
        json: r#"{"leaf":1,"title":"DEATH NOTE","genres":["thriller"]}"#.to_string(),
    }];
    let opened = bulk
        .open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect("an import");
    // Named, because writing over a declaration the library already holds is the reader's
    // decision now. What this test is about is what survives when they say yes.
    assert_eq!(
        vec!["work.json".to_string()],
        opened
            .declarations
            .iter()
            .map(|d| d.path.clone())
            .collect::<Vec<_>>()
    );
    bulk.commit(
        &opened.id,
        &[],
        &["work.json".to_string()],
        &[],
        DEFAULT_PROFILE,
    )
    .expect("a commit");

    let after: serde_json::Value =
        serde_json::from_slice(&std::fs::read(target.join("work.json")).unwrap()).unwrap();
    assert_eq!(
        "2f680fdf43a78c4e", after["id"],
        "the identity stays the one on disk"
    );
    // And everything else is what just arrived: the identity alone is held back.
    assert_eq!("DEATH NOTE", after["title"]);
    assert_eq!(serde_json::json!(["thriller"]), after["genres"]);
}

/// A declaration that cannot be read is refused at the announcement, before a byte moves.
///
/// Installing it over one the library can read would replace something useful with
/// something inert — the scan ignores it — and take the folder's identity with it. Skipping
/// it quietly at commit would be the other half of the same mistake: the reader would be
/// told the import worked, and the file would still be wrong.
#[test]
fn a_declaration_that_cannot_be_read_is_refused_before_anything_moves() {
    let (dir, bulk) = a_bulk();
    let target = dir.path().join("library").join("Death Note");
    std::fs::create_dir_all(&target).unwrap();
    std::fs::write(
        target.join("work.json"),
        br#"{"leaf":1,"title":"Death Note","id":"2f680fdf43a78c4e"}"#,
    )
    .unwrap();

    let mut request = manifest(&[], Scope::Addition);
    request.root = "Death Note".to_string();
    request.sidecars = vec![ManifestSidecar {
        path: "work.json".to_string(),
        // One trailing comma: what a hand-edited file becomes.
        json: r#"{"leaf":1,"title":"Death Note",}"#.to_string(),
    }];

    let refused = bulk
        .open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect_err("an unreadable declaration is refused");
    assert!(
        refused.to_string().contains("work.json"),
        "the refusal names the file to fix: {refused}"
    );

    // And the disk has not moved: that is the whole point of refusing at the announcement.
    let after: serde_json::Value =
        serde_json::from_slice(&std::fs::read(target.join("work.json")).unwrap()).unwrap();
    assert_eq!("2f680fdf43a78c4e", after["id"]);
}

/// A folder that was not there has no identity to keep: the declaration arrives as it came,
/// and the scan will stamp one onto it.
#[test]
fn a_declaration_landing_somewhere_new_arrives_as_it_came() {
    let (dir, bulk) = a_bulk();
    let mut request = manifest(&[], Scope::Addition);
    request.root = "Elfes".to_string();
    request.sidecars = vec![ManifestSidecar {
        path: "work.json".to_string(),
        json: r#"{"leaf":1,"title":"Elfes"}"#.to_string(),
    }];
    let opened = bulk
        .open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect("an import");
    bulk.commit(&opened.id, &[], &[], &[], DEFAULT_PROFILE)
        .expect("a commit");

    let landed = dir.path().join("library/Elfes/work.json");
    let after: serde_json::Value =
        serde_json::from_slice(&std::fs::read(&landed).unwrap()).unwrap();
    assert_eq!("Elfes", after["title"]);
    assert!(after["id"].is_null());
}

#[test]
fn an_import_asks_only_for_what_is_not_already_there() {
    let world = World::new();
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));
    let there = world.library().join("Terres d'Arran/Nains 01.cbz");
    std::fs::create_dir_all(there.parent().unwrap()).unwrap();
    std::fs::write(&there, vec![0u8; 10]).unwrap();

    let opened = bulk
        .open(
            &manifest(
                &[("Nains 01.cbz", 10), ("Nains 02.cbz", 4)],
                Scope::Addition,
            ),
            DEFAULT_PROFILE,
            &Ceiling::NONE,
        )
        .expect("opening");

    assert_eq!(vec!["Nains 02.cbz".to_string()], opened.to_send);
    assert_eq!(vec!["Nains 01.cbz".to_string()], opened.already_there);
    assert_eq!(4, opened.bytes_to_send);
    // Nains 02.cbz is new to the library — nothing sits at that path yet — so asking for it
    // is not a replacement.
    assert!(opened.replaces.is_empty(), "{:?}", opened.replaces);
}

/// A manifest can declare the same path the library already holds under another size — a
/// retouched volume sent again without a new name. `commit`'s installation is an ordinary
/// rename, which onto an existing path replaces it, and a reader deciding whether to import
/// has to be told that before it happens.
///
/// Measured: this file used to land in `to_send` and nowhere else — neither `already_there`
/// nor `creates` — so nothing said it would overwrite what is already on the disk.
#[test]
fn a_manifest_that_disagrees_with_what_is_already_home_is_named_as_a_replacement() {
    let world = World::new();
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));
    let there = world.library().join("Terres d'Arran/Nains 01.cbz");
    std::fs::create_dir_all(there.parent().unwrap()).unwrap();
    std::fs::write(&there, vec![0u8; 10]).unwrap();

    let opened = bulk
        .open(
            &manifest(&[("Nains 01.cbz", 4)], Scope::Addition),
            DEFAULT_PROFILE,
            &Ceiling::NONE,
        )
        .expect("opening");

    assert_eq!(vec!["Nains 01.cbz".to_string()], opened.to_send);
    assert!(
        opened.already_there.is_empty(),
        "{:?}",
        opened.already_there
    );
    assert_eq!(
        vec!["Nains 01.cbz".to_string()],
        opened
            .replaces
            .iter()
            .map(|r| r.path.clone())
            .collect::<Vec<_>>(),
        "a size that disagrees with what is already home is a replacement, not a plain addition"
    );
}

#[test]
fn a_transfer_resumes_at_the_byte_it_stopped_on() {
    let world = World::new();
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));
    let opened = bulk
        .open(
            &manifest(&[("Nains 01.cbz", 8)], Scope::Addition),
            DEFAULT_PROFILE,
            &Ceiling::NONE,
        )
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
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));
    let opened = bulk
        .open(
            &manifest(&[("Nains 01.cbz", 8)], Scope::Addition),
            DEFAULT_PROFILE,
            &Ceiling::NONE,
        )
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
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));
    let opened = bulk
        .open(
            &manifest(&[("Nains 01.cbz", 4), ("Nains 02.cbz", 4)], Scope::Addition),
            DEFAULT_PROFILE,
            &Ceiling::NONE,
        )
        .expect("opening");
    bulk.receive(&opened.id, "Nains 01.cbz", 0, b"abcd", CEILING)
        .expect("whole");
    bulk.receive(&opened.id, "Nains 02.cbz", 0, b"ab", CEILING)
        .expect("half");

    let result = bulk
        .commit(&opened.id, &[], &[], &[], DEFAULT_PROFILE)
        .expect("committing");

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
    let result = bulk
        .commit(&opened.id, &[], &[], &[], DEFAULT_PROFILE)
        .expect("committing again");
    assert_eq!(1, result.installed);
    assert!(result.pending.is_empty());
    assert!(
        !result.open,
        "everything went home, so there is nothing left to hold"
    );
    // Everything arrived: now it is gone.
    assert!(bulk.state(&opened.id).unwrap().is_none());
}

/// An image named for the series, sent with the volumes, is the series' cover: it lands in
/// the work's folder and the scan reads it from there (`cover.jpg` is one of the names it
/// takes for the whole series), with nothing to say about it in the announcement.
#[test]
fn a_cover_image_sent_with_the_volumes_becomes_the_series_cover() {
    let world = World::new();
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));
    let volume = {
        let held = world.dir.path().join("held.cbz");
        archive(&held, None);
        std::fs::read(held).unwrap()
    };
    let picture = jpeg();
    let opened = bulk
        .open(
            &manifest(
                &[
                    ("Nains 01.cbz", volume.len() as u64),
                    ("cover.jpg", picture.len() as u64),
                ],
                Scope::Addition,
            ),
            DEFAULT_PROFILE,
            &Ceiling::NONE,
        )
        .expect("opening");
    bulk.receive(&opened.id, "Nains 01.cbz", 0, &volume, CEILING)
        .expect("the volume");
    bulk.receive(&opened.id, "cover.jpg", 0, &picture, CEILING)
        .expect("the cover");
    bulk.commit(&opened.id, &[], &[], &[], DEFAULT_PROFILE)
        .expect("committing");
    assert!(world.library().join("Terres d'Arran/cover.jpg").exists());

    world.scan();
    let chosen: Option<String> = world.one("SELECT cover_file FROM edition");
    assert!(
        chosen.is_some_and(|file| file.ends_with("cover.jpg")),
        "the series' cover is the image that arrived with it"
    );
}

#[test]
fn a_file_that_travelled_wrong_is_left_aside_rather_than_installed() {
    let world = World::new();
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));
    let mut request = manifest(&[("Nains 01.cbz", 4)], Scope::Addition);
    request.files[0].checksum = Some("not the digest of abcd".into());
    let opened = bulk
        .open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect("opening");
    bulk.receive(&opened.id, "Nains 01.cbz", 0, b"abcd", CEILING)
        .expect("whole");

    let result = bulk
        .commit(&opened.id, &[], &[], &[], DEFAULT_PROFILE)
        .expect("committing");

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
    let opened = bulk
        .open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect("opening");
    bulk.receive(&opened.id, "Nains 01.cbz", 0, b"abcd", CEILING)
        .expect("whole");
    assert_eq!(
        1,
        bulk.commit(&opened.id, &[], &[], &[], DEFAULT_PROFILE)
            .expect("committing")
            .installed
    );
}

#[test]
fn an_addition_says_nothing_about_what_it_did_not_bring() {
    let world = World::new();
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));
    let stranger = world.library().join("Terres d'Arran/Elfes 01.cbz");
    std::fs::create_dir_all(stranger.parent().unwrap()).unwrap();
    std::fs::write(&stranger, b"xy").unwrap();

    let opened = bulk
        .open(
            &manifest(&[("Nains 01.cbz", 4)], Scope::Addition),
            DEFAULT_PROFILE,
            &Ceiling::NONE,
        )
        .expect("opening");
    bulk.receive(&opened.id, "Nains 01.cbz", 0, b"abcd", CEILING)
        .expect("whole");
    let result = bulk
        .commit(&opened.id, &[], &[], &[], DEFAULT_PROFILE)
        .expect("committing");

    // The desktop is a workbench, not a mirror: a series imported and then deleted locally
    // can no longer be announced in full.
    assert!(result.orphans.is_empty());
    assert!(stranger.exists());
}

#[test]
fn a_complete_manifest_names_the_orphans_and_deletes_nothing() {
    let world = World::new();
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));
    let stranger = world.library().join("Terres d'Arran/Elfes 01.cbz");
    std::fs::create_dir_all(stranger.parent().unwrap()).unwrap();
    std::fs::write(&stranger, b"xy").unwrap();

    let opened = bulk
        .open(
            &manifest(&[("Nains 01.cbz", 4)], Scope::Complete),
            DEFAULT_PROFILE,
            &Ceiling::NONE,
        )
        .expect("opening");
    bulk.receive(&opened.id, "Nains 01.cbz", 0, b"abcd", CEILING)
        .expect("whole");
    let result = bulk
        .commit(&opened.id, &[], &[], &[], DEFAULT_PROFILE)
        .expect("committing");

    assert_eq!(vec!["Elfes 01.cbz".to_string()], result.orphans);
    // Reported, and still there: a wrong manifest never destroys anything.
    assert!(stranger.exists());
}

#[test]
fn deletion_happens_only_on_an_explicit_by_name_order() {
    let world = World::new();
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));
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
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));
    let opened = bulk
        .open(
            &manifest(&[("Nains 01.cbz", 4)], Scope::Addition),
            DEFAULT_PROFILE,
            &Ceiling::NONE,
        )
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
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));
    let opened = bulk
        .open(
            &manifest(&[("Nains 01.cbz", 4096)], Scope::Addition),
            DEFAULT_PROFILE,
            &Ceiling::NONE,
        )
        .expect("opening");

    assert!(bulk
        .receive(&opened.id, "Nains 01.cbz", 0, &vec![0u8; 4096], 1024)
        .is_err());
}

#[test]
fn an_abandoned_import_is_gone() {
    let world = World::new();
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));
    let opened = bulk
        .open(
            &manifest(&[("Nains 01.cbz", 4)], Scope::Addition),
            DEFAULT_PROFILE,
            &Ceiling::NONE,
        )
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

    // A file that says nothing of its number, named like volume 1 — which is what comes out
    // of a downloads folder more often than not. It says nothing to build a name from, so
    // it keeps the one it had.
    let file = staged(
        &intake,
        "Tome 1.cbz",
        Some(&EntryJson {
            leaf: Some(1),
            work: Some("Bleach".into()),
            ..Default::default()
        }),
    );
    let proposal = intake.propose_for(&file).expect("proposing");
    assert!(
        matches!(proposal.confidence, Confidence::Certain),
        "no number is taken: the name is what meets"
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

    assert!(
        !collision.same_volume,
        "a file with no number is not volume 1"
    );
    assert_eq!(None, collision.arriving.number);
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
                title: Some(Some("Banana split".into())),
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
            number: Some(1.0),
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
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));
    let root = world.library().join("Terres d'Arran");
    std::fs::create_dir_all(root.join("Nains")).unwrap();
    std::fs::write(root.join("Nains/Tome 1.cbz"), b"abcd").unwrap();
    std::os::unix::fs::symlink(&root, root.join("Nains/retour")).unwrap();

    let opened = bulk
        .open(
            &manifest(&[("Nains/Tome 1.cbz", 4)], Scope::Complete),
            DEFAULT_PROFILE,
            &Ceiling::NONE,
        )
        .expect("opening");
    // A COMPLETE manifest sweeps the whole target to name what it did not bring, and
    // `is_dir()` follows symlinks: without this it would recurse until the stack gave out.
    let result = bulk
        .commit(&opened.id, &[], &[], &[], DEFAULT_PROFILE)
        .expect("committing");

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
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));
    assert!(bulk.waiting().unwrap().is_empty());

    let opened = bulk
        .open(
            &manifest(&[("Nains 01.cbz", 4), ("Nains 02.cbz", 4)], Scope::Addition),
            DEFAULT_PROFILE,
            &Ceiling::NONE,
        )
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
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));

    let of = |root: &str, count: usize| ImportRequest {
        root: root.into(),
        files: (1..=count)
            .map(|n| ManifestFile {
                path: format!("Tome {n:02}.cbz"),
                size: 4,
                checksum: None,
                rewrite: None,
                ..Default::default()
            })
            .collect(),
        scope: Scope::Addition,
        sidecars: Vec::new(),
        ..Default::default()
    };
    let bleach = bulk
        .open(&of("Bleach", 8), DEFAULT_PROFILE, &Ceiling::NONE)
        .expect("opening");
    let naruto = bulk
        .open(&of("Naruto", 8), DEFAULT_PROFILE, &Ceiling::NONE)
        .expect("opening");

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
    let stopped = bulk
        .commit(&bleach.id, &[], &[], &[], DEFAULT_PROFILE)
        .expect("committing");
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
    let finished = bulk
        .commit(&bleach.id, &[], &[], &[], DEFAULT_PROFILE)
        .expect("committing again");
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
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));
    let there = world.library().join("Terres d'Arran/Nains 01.cbz");
    std::fs::create_dir_all(there.parent().unwrap()).unwrap();
    std::fs::write(&there, b"Tome 1").unwrap();

    // Same length, different contents, and the sender says which contents it means.
    let digest = "6a5f9b6f8e2f4c3d";
    let mut request = manifest(&[("Nains 01.cbz", 6)], Scope::Addition);
    request.files[0].checksum = Some(digest.into());
    let opened = bulk
        .open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect("opening");
    assert_eq!(
        vec!["Nains 01.cbz".to_string()],
        opened.to_send,
        "the length matches and the contents do not"
    );

    // The real digest of what is there: nothing to send, and nothing read twice.
    let real = leaf_server::api::bulk_import::checksum(&there).expect("digesting");
    let mut request = manifest(&[("Nains 01.cbz", 6)], Scope::Addition);
    request.files[0].checksum = Some(real);
    let opened = bulk
        .open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect("opening");
    assert!(opened.to_send.is_empty(), "{:?}", opened.to_send);
    assert_eq!(vec!["Nains 01.cbz".to_string()], opened.already_there);

    // And with no checksum at all, the size is all there is to go on — said plainly rather
    // than pretended otherwise.
    let opened = bulk
        .open(
            &manifest(&[("Nains 01.cbz", 6)], Scope::Addition),
            DEFAULT_PROFILE,
            &Ceiling::NONE,
        )
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
        Arc::new(Db::open(&dir.path().join("index.sqlite")).unwrap()),
    );
    (dir, bulk)
}

#[test]
fn a_manifest_path_that_walks_upwards_is_refused_at_the_announcement() {
    use leaf_server::api::bulk_import::{ImportRequest, ManifestFile, Scope};

    // Accepted at the announcement and refused only when its first byte arrived, the
    // session it opened could then never be read back: listed nowhere, abandoned by no
    // one, its folder held in the inbox for good. A path outside the root is refused
    // before any session exists.
    let (dir, bulk) = a_bulk();
    let refused = bulk
        .open(
            &ImportRequest {
                root: "Bleach".into(),
                scope: Scope::Addition,
                files: vec![ManifestFile {
                    path: "../../evade.cbz".into(),
                    size: 2,
                    checksum: None,
                    rewrite: None,
                    ..Default::default()
                }],
                sidecars: Vec::new(),
                ..Default::default()
            },
            DEFAULT_PROFILE,
            &Ceiling::NONE,
        )
        .unwrap_err();

    assert!(
        refused
            .downcast_ref::<leaf_server::api::Invalid>()
            .is_some(),
        "{refused:?}"
    );
    assert_eq!(
        0,
        std::fs::read_dir(dir.path().join("inbox")).unwrap().count()
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
        .open(
            &ImportRequest {
                root: "Bleach".into(),
                scope: Scope::Addition,
                files: vec![ManifestFile {
                    path: "Tome 1.cbz".into(),
                    size: 9_000_000_000,
                    checksum: None,
                    rewrite: None,
                    ..Default::default()
                }],
                sidecars: Vec::new(),
                ..Default::default()
            },
            DEFAULT_PROFILE,
            &Ceiling::NONE,
        )
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
        .open(
            &ImportRequest {
                root: "Bleach".into(),
                scope: Scope::Addition,
                files: vec![ManifestFile {
                    path: "Tome 1.cbz".into(),
                    size: 2,
                    checksum: None,
                    rewrite: None,
                    ..Default::default()
                }],
                sidecars: Vec::new(),
                ..Default::default()
            },
            DEFAULT_PROFILE,
            &Ceiling::NONE,
        )
        .expect("opened");

    let state = bulk.state(&opened.id).unwrap().expect("a state");
    assert_eq!(state.missing.len(), 0, "{state:?}");
    // Nothing left to send: it is there, at the right length.
    let result = bulk
        .commit(&opened.id, &[], &[], &[], DEFAULT_PROFILE)
        .expect("committed");
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
        .open(
            &ImportRequest {
                root: "Bleach".into(),
                scope: Scope::Complete,
                files: vec![ManifestFile {
                    path: "Tome 1.cbz".into(),
                    size: 2,
                    checksum: None,
                    rewrite: None,
                    ..Default::default()
                }],
                sidecars: Vec::new(),
                ..Default::default()
            },
            DEFAULT_PROFILE,
            &Ceiling::NONE,
        )
        .expect("opened");

    let result = bulk
        .commit(&opened.id, &[], &[], &[], DEFAULT_PROFILE)
        .expect("committed");
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
        .open(
            &ImportRequest {
                root: "Bleach".into(),
                scope: Scope::Complete,
                files: Vec::new(),
                sidecars: Vec::new(),
                ..Default::default()
            },
            DEFAULT_PROFILE,
            &Ceiling::NONE,
        )
        .expect("opened");

    let result = bulk
        .commit(&opened.id, &[], &[], &[], DEFAULT_PROFILE)
        .expect("committed");
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
        .open(
            &ImportRequest {
                root: "Bleach".into(),
                scope: Scope::Complete,
                files: Vec::new(),
                sidecars: Vec::new(),
                ..Default::default()
            },
            DEFAULT_PROFILE,
            &Ceiling::NONE,
        )
        .expect("opened");

    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        std::fs::set_permissions(&target, std::fs::Permissions::from_mode(0o000)).unwrap();
        let result = bulk
            .commit(&opened.id, &[], &[], &[], DEFAULT_PROFILE)
            .expect("committed");
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
        Arc::new(Db::open(&dir.path().join("index.sqlite")).unwrap()),
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
        .open(
            &ImportRequest {
                root: "Bleach".into(),
                scope: Scope::Addition,
                files: vec![ManifestFile {
                    path: "Tome 1.cbz".into(),
                    size: 2,
                    checksum: Some("0".repeat(64)),
                    rewrite: None,
                    ..Default::default()
                }],
                sidecars: Vec::new(),
                ..Default::default()
            },
            DEFAULT_PROFILE,
            &Ceiling::NONE,
        )
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

    let result = bulk
        .commit(&opened.id, &[], &[], &[], DEFAULT_PROFILE)
        .expect("committed");
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
        .open(
            &ImportRequest {
                root: "Bleach".into(),
                scope: Scope::Addition,
                files: vec![ManifestFile {
                    path: "Extras/./Bonus.cbz".into(),
                    size: 2,
                    checksum: None,
                    rewrite: None,
                    ..Default::default()
                }],
                sidecars: Vec::new(),
                ..Default::default()
            },
            DEFAULT_PROFILE,
            &Ceiling::NONE,
        )
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

    bulk.open(
        &ImportRequest {
            root: "Bleach".into(),
            scope: Scope::Addition,
            files: vec![
                ManifestFile {
                    path: "Tome 1.cbz".into(),
                    size: 2,
                    checksum: None,
                    rewrite: None,
                    ..Default::default()
                },
                ManifestFile {
                    path: "Tome 2.cbz".into(),
                    size: 2,
                    checksum: None,
                    rewrite: None,
                    ..Default::default()
                },
            ],
            sidecars: Vec::new(),
            ..Default::default()
        },
        DEFAULT_PROFILE,
        &Ceiling::NONE,
    )
    .expect("opened");

    let waiting = bulk.waiting().unwrap();
    assert_eq!(waiting.len(), 1);
    assert_eq!(waiting[0].of, 2);
    assert_eq!(waiting[0].complete, 1, "the one already home is done");
}

// ——— What a folder would add: the diff the library is shown ————————————————

fn declaring(path: &str, json: &str) -> ManifestSidecar {
    ManifestSidecar {
        path: path.into(),
        json: json.into(),
    }
}

/// The sentence the dialog has to be able to write: *« créera l'univers Terres d'Arran, et
/// Elfes dedans »*. Answered before a byte moves, because a folder that went where it was
/// not meant to looks exactly like one that went where it was.
#[test]
fn opening_a_folder_says_what_the_library_would_gain() {
    let world = World::new();
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));

    let mut request = manifest(&[("Elfes/Tome 1.cbz", 10)], Scope::Addition);
    request.root = "Terres d'Arran".into();
    request.sidecars = vec![
        declaring("universe.json", r#"{"leaf":1,"name":"Terres d’Arran"}"#),
        declaring("Elfes/work.json", r#"{"leaf":1,"title":"Elfes"}"#),
    ];

    let said = bulk
        .open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect("an opening");
    let kinds: Vec<(&str, &str)> = said
        .creates
        .iter()
        .map(|c| (c.kind.as_str(), c.name.as_str()))
        .collect();
    assert_eq!(
        kinds,
        vec![("UNIVERSE", "Terres d’Arran"), ("WORK", "Elfes")]
    );
    // Named from the declaration and not from the folder, because the two are allowed to
    // differ and the declaration is the one that wins.
    assert_eq!(said.creates[1].at, "Elfes");
}

/// A folder already on disk is not a creation. Read off the disk and not the index on
/// purpose: a folder put there by hand and not yet scanned exists, and answering "no"
/// would make a second one beside it.
#[test]
fn a_folder_that_is_already_there_creates_nothing() {
    let world = World::new();
    std::fs::create_dir_all(world.library().join("Bleach")).unwrap();
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));

    let mut request = manifest(&[("Tome 3.cbz", 10)], Scope::Addition);
    request.root = "Bleach".into();
    request.sidecars = vec![declaring("work.json", r#"{"leaf":1,"title":"Bleach"}"#)];

    assert!(bulk
        .open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect("an opening")
        .creates
        .is_empty());
}

/// The edition of a series that exists: one creation, not two.
#[test]
fn a_new_edition_under_a_work_that_is_there_creates_only_the_edition() {
    let world = World::new();
    std::fs::create_dir_all(world.library().join("Parasite")).unwrap();
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));

    let mut request = manifest(&[("Deluxe/Tome 1.cbz", 10)], Scope::Addition);
    request.root = "Parasite".into();
    request.sidecars = vec![
        declaring("work.json", r#"{"leaf":1,"title":"Parasite"}"#),
        declaring(
            "Deluxe/edition.json",
            r#"{"leaf":1,"name":"Édition Deluxe"}"#,
        ),
    ];

    let said = bulk
        .open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect("an opening");
    assert_eq!(said.creates.len(), 1);
    assert_eq!(said.creates[0].kind, "EDITION");
    assert_eq!(said.creates[0].name, "Édition Deluxe");
}

/// The declarations reach the library with the files. Without this a series lands without
/// the thing that says what it is, and the scan behind it classifies a folder of archives
/// as nothing at all.
#[test]
fn the_declarations_are_installed_along_with_the_volumes() {
    let world = World::new();
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));

    let mut request = manifest(&[("Tome 1.cbz", 4)], Scope::Addition);
    request.root = "Koro".into();
    request.sidecars = vec![declaring("work.json", r#"{"leaf":1,"title":"Koro Quest"}"#)];

    let opened = bulk
        .open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect("an opening");
    // Announced as missing, so it is sent — the sidecar was not, and does not need to be.
    assert_eq!(opened.to_send, vec!["Tome 1.cbz".to_string()]);
    {
        let mut file = leaf_server::api::bulk_import::open_at(
            &world
                .inbox()
                .join(&opened.id)
                .join("files")
                .join("Tome 1.cbz"),
            0,
        )
        .unwrap();
        use std::io::Write;
        file.write_all(b"abcd").unwrap();
    }

    let result = bulk
        .commit(&opened.id, &[], &[], &[], DEFAULT_PROFILE)
        .expect("a commit");
    assert_eq!(result.installed, 1);
    let landed = world.library().join("Koro").join("work.json");
    assert!(landed.exists(), "the declaration did not travel");
    assert!(std::fs::read_to_string(landed)
        .unwrap()
        .contains("Koro Quest"));
}

/// A declaration this server does not understand is still installed, unparsed. A field a
/// later version writes has to survive a trip through one that does not know it.
#[test]
fn a_declaration_carrying_an_unknown_field_keeps_it() {
    let world = World::new();
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));

    let mut request = manifest(&[], Scope::Addition);
    request.root = "Demain".into();
    request.sidecars = vec![declaring(
        "work.json",
        r#"{"leaf":1,"title":"Demain","champDeDemain":"à garder"}"#,
    )];

    let opened = bulk
        .open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect("an opening");
    bulk.commit(&opened.id, &[], &[], &[], DEFAULT_PROFILE)
        .expect("a commit");
    let landed = std::fs::read_to_string(world.library().join("Demain").join("work.json")).unwrap();
    assert!(landed.contains("champDeDemain"), "{landed}");
}

/// A declaration with no path of its own is a malformed announcement, and says so.
///
/// It used to reach `under` a few statements later, which has nothing to resolve and fails
/// as an error rather than as a refusal — so a 400's worth of wrong request came back a
/// 500's worth of wrong server.
#[test]
fn a_declaration_without_a_path_is_refused_rather_than_breaking() {
    let (_dir, bulk) = a_bulk();
    let mut request = manifest(&[("Nains 01.cbz", 4)], Scope::Addition);
    request.sidecars = vec![ManifestSidecar {
        path: String::new(),
        json: r#"{"leaf":1}"#.into(),
    }];

    let refused = bulk
        .open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect_err("a declaration with no path");

    assert!(
        refused.to_string().contains("without the path"),
        "{refused}"
    );
}

/// A request to rewrite something that is not an archive is a malformed announcement.
///
/// The rewrite opens the member list of a zip; asked of a `.jpg` it could only fail at the
/// commit, after the file had travelled. Refused at the announcement, with the path to fix,
/// and the inbox untouched.
#[test]
fn a_rewrite_on_something_that_is_not_an_archive_is_refused_before_a_byte_moves() {
    let (dir, bulk) = a_bulk();
    let mut request = manifest(&[("cover.jpg", 4)], Scope::Addition);
    request.files[0].rewrite = Some(a_rewrite(r#"{"leaf":1}"#, &[]));

    let refused = bulk
        .open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect_err("not an archive");

    assert!(refused.to_string().contains("cover.jpg"), "{refused}");
    assert_eq!(
        0,
        std::fs::read_dir(dir.path().join("inbox")).unwrap().count(),
        "no session was opened"
    );
}

#[test]
fn a_rewrite_whose_entry_json_is_not_an_object_is_refused() {
    let (_dir, bulk) = a_bulk();
    for not_an_object in ["[1]", r#""text""#, r#"{"leaf":1,}"#, ""] {
        let mut request = manifest(&[("Nains 01.cbz", 4)], Scope::Addition);
        request.files[0].rewrite = Some(a_rewrite(not_an_object, &[]));

        let refused = bulk
            .open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
            .expect_err(not_an_object);

        assert!(refused.to_string().contains("Nains 01.cbz"), "{refused}");
    }
}

/// A name to remove is a member of the archive, never a way out of it.
#[test]
fn a_rewrite_that_names_a_path_to_remove_is_refused() {
    let (_dir, bulk) = a_bulk();
    for climbing in ["../x", "a/b", r"a\b", ".."] {
        let mut request = manifest(&[("Nains 01.cbz", 4)], Scope::Addition);
        request.files[0].rewrite = Some(a_rewrite(r#"{"leaf":1}"#, &[climbing]));

        assert!(
            bulk.open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
                .is_err(),
            "{climbing} was accepted"
        );
    }
}

#[test]
fn an_announcement_without_rewrite_opens_as_before() {
    let (_dir, bulk) = a_bulk();
    let mut request = manifest(&[("Nains 01.cbz", 4), ("Nains 02.zip", 4)], Scope::Addition);
    request.files[1].rewrite = Some(a_rewrite(r#"{"leaf":1}"#, &["Thumbs.db"]));

    let opened = bulk
        .open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect("a valid rewrite opens");
    assert_eq!(2, opened.to_send.len());

    let plain: leaf_server::api::bulk_import::ManifestFile =
        serde_json::from_str(r#"{"path":"a.cbz","size":1}"#).unwrap();
    assert!(plain.rewrite.is_none());
}

/// A folder that is two things at once is an announcement that contradicts itself, and the
/// scan would pick one silently. Refused with the folder named, and the inbox untouched.
#[test]
fn two_declarations_in_one_folder_are_refused_naming_the_folder() {
    let (dir, bulk) = a_bulk();
    let mut request = manifest(&[("Elfes/Tome 1.cbz", 4)], Scope::Addition);
    request.sidecars = vec![
        declaring("Elfes/work.json", r#"{"leaf":1}"#),
        declaring("Elfes/edition.json", r#"{"leaf":1}"#),
    ];

    let refused = bulk
        .open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect_err("two declarations");

    assert!(refused.to_string().contains("Elfes"), "{refused}");
    assert_eq!(
        0,
        std::fs::read_dir(dir.path().join("inbox")).unwrap().count(),
        "no session was opened"
    );
}

/// An archive sitting directly in a universe is a book of it when it carries `oneshot.json`,
/// and that declaration is inside the archive, where the sender cannot read it from the
/// announcement. Refusing here turned away the one shape the scan knows how to file; the
/// scan is what opens the archive, and it reports the ones it cannot use.
#[test]
fn a_universe_holding_archives_directly_is_accepted_and_left_to_the_scan() {
    let (_dir, bulk) = a_bulk();
    let mut request = manifest(&[("Blast.cbz", 4)], Scope::Addition);
    request.sidecars = vec![declaring("universe.json", r#"{"leaf":1}"#)];

    bulk.open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect("a universe holding an archive opens");
}

#[test]
fn a_series_with_its_edition_folders_is_accepted() {
    let (_dir, bulk) = a_bulk();
    let mut request = manifest(
        &[("Perfect/Tome 1.cbz", 4), ("Original/Tome 1.cbz", 4)],
        Scope::Addition,
    );
    request.sidecars = vec![
        declaring("work.json", r#"{"leaf":1}"#),
        declaring("Perfect/edition.json", r#"{"leaf":1}"#),
        declaring("Original/edition.json", r#"{"leaf":1}"#),
    ];

    bulk.open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect("a coherent announcement opens");
}

#[test]
fn an_announcement_of_nothing_but_archives_is_accepted() {
    let (_dir, bulk) = a_bulk();
    let request = manifest(&[("Tome 1.cbz", 4), ("Tome 2.cbz", 4)], Scope::Addition);

    bulk.open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect("an implicit edition opens");
}

fn a_rewrite(entry_json: &str, remove: &[&str]) -> leaf_server::api::bulk_import::ArchiveRewrite {
    leaf_server::api::bulk_import::ArchiveRewrite {
        entry_json: entry_json.into(),
        remove: remove.iter().map(|n| (*n).to_string()).collect(),
        one_shot_json: None,
    }
}

/// A root that is one plain name is what this accepts, and a name holding two dots is one.
///
/// `contains("..")` refused `Terres d'Arran..2`, which is a folder somebody is allowed to
/// have, while a climb out of the library is a `..` *component* — which is what is refused
/// now, along with any separator at all.
#[test]
fn a_root_with_two_dots_in_its_name_is_not_a_climb_out_of_the_library() {
    let (_dir, bulk) = a_bulk();
    let mut request = manifest(&[("Nains 01.cbz", 4)], Scope::Addition);
    request.root = "Terres d'Arran..2".into();

    let opened = bulk
        .open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect("a name, not a climb");
    assert_eq!("Terres d'Arran..2", opened.root);

    let mut climbing = manifest(&[("Nains 01.cbz", 4)], Scope::Addition);
    climbing.root = "../etc".into();
    assert!(
        bulk.open(&climbing, DEFAULT_PROFILE, &Ceiling::NONE)
            .is_err(),
        "and a climb is still refused"
    );
}

/// Six volumes that could be replaced are six decisions, and a commit takes none of them.
///
/// The twin of `move`: nothing lands on a file the library already holds unless the commit
/// names that path. What is refused stays in the inbox and is reported in `pending` — not
/// installed, not thrown away, and above all not silently skipped, which would read exactly
/// like a transfer that worked.
#[test]
fn a_commit_replaces_only_the_paths_it_was_told_to() {
    // Volumes the index holds: one it did not would have no entry to record the
    // replacement against, and waits for the scan.
    let world = World::new();
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));
    let target = world.library().join("Terres d'Arran");
    std::fs::create_dir_all(&target).unwrap();
    let volume = |number: f64, pages: u8| {
        common::archive_of_pages(
            &EntryJson {
                leaf: Some(1),
                number: Some(number),
                ..Default::default()
            },
            pages,
        )
    };
    let (old_two, new_one, new_two) = (volume(2.0, 2), volume(1.0, 3), volume(2.0, 3));
    std::fs::write(target.join("Nains 01.cbz"), volume(1.0, 2)).unwrap();
    std::fs::write(target.join("Nains 02.cbz"), &old_two).unwrap();
    world.scan();

    let opened = bulk
        .open(
            &manifest(
                &[
                    ("Nains 01.cbz", new_one.len() as u64),
                    ("Nains 02.cbz", new_two.len() as u64),
                ],
                Scope::Addition,
            ),
            DEFAULT_PROFILE,
            &Ceiling::NONE,
        )
        .expect("opening");
    assert_eq!(2, opened.replaces.len(), "{:?}", opened.replaces);
    bulk.receive(&opened.id, "Nains 01.cbz", 0, &new_one, CEILING)
        .expect("the first");
    bulk.receive(&opened.id, "Nains 02.cbz", 0, &new_two, CEILING)
        .expect("the second");

    let result = bulk
        .commit(
            &opened.id,
            &["Nains 01.cbz".to_string()],
            &[],
            &[],
            DEFAULT_PROFILE,
        )
        .expect("committing");

    assert_eq!(1, result.installed);
    assert_eq!(
        new_one,
        std::fs::read(target.join("Nains 01.cbz")).unwrap(),
        "the one that was named is replaced"
    );
    assert_eq!(
        old_two,
        std::fs::read(target.join("Nains 02.cbz")).unwrap(),
        "and the one that was not is left exactly as it was"
    );
    assert_eq!(
        vec!["Nains 02.cbz".to_string()],
        result.pending,
        "refused is said, not silently skipped"
    );
    // And the bytes are still there to install with a later commit that does name it.
    assert!(result.open);
    let again = bulk
        .commit(
            &opened.id,
            &["Nains 02.cbz".to_string()],
            &[],
            &[],
            DEFAULT_PROFILE,
        )
        .expect("committing again");
    assert_eq!(1, again.installed);
    assert_eq!(new_two, std::fs::read(target.join("Nains 02.cbz")).unwrap());
}

/// What the library already holds is read out of its own archive, so a reader can tell two
/// files apart without opening both by hand.
///
/// Measured on a real library: two archives six hundred and eighty-two bytes apart, whose
/// whole difference was a title edited through the API months earlier. A size says
/// « different » and stops there.
#[test]
fn a_replacement_says_what_the_library_already_holds() {
    let (dir, bulk) = a_bulk();
    let target = dir.path().join("library").join("Terres d'Arran");
    std::fs::create_dir_all(&target).unwrap();
    archive(
        &target.join("Nains 01.cbz"),
        Some(&EntryJson {
            title: Some("Le vieux titre".to_string()),
            number: Some(1.0),
            ..EntryJson::default()
        }),
    );
    let held = std::fs::metadata(target.join("Nains 01.cbz"))
        .unwrap()
        .len();

    let opened = bulk
        .open(
            &manifest(&[("Nains 01.cbz", 4)], Scope::Addition),
            DEFAULT_PROFILE,
            &Ceiling::NONE,
        )
        .expect("opening");

    let said = opened.replaces.first().expect("a replacement");
    assert_eq!("Nains 01.cbz", said.path);
    assert_eq!(4, said.size, "what would arrive");
    assert_eq!(held, said.present_size, "what is there");
    assert_eq!(Some("Le vieux titre".to_string()), said.present_title);
    assert_eq!(Some(1.0), said.present_number);
    assert!(said.present_read, "and the server says it looked");
}

/// A declaration the library already holds is written over only when the commit names it.
///
/// `replace`'s twin one floor up. A series every volume of which the library already holds
/// still carries its `work.json`, and the commit installed it over whatever was there —
/// measured on a real library, over a summary and a set of arcs edited through `PATCH`
/// months earlier, while the card said « déjà là ».
#[test]
fn a_declaration_already_in_the_library_is_rewritten_only_when_named() {
    let (dir, bulk) = a_bulk();
    let target = dir.path().join("library").join("Terres d'Arran");
    std::fs::create_dir_all(&target).unwrap();
    std::fs::write(
        target.join("work.json"),
        br#"{"leaf":1,"title":"Nains","summary":"ce que j'ai ecrit","id":"aabbccddeeff0011"}"#,
    )
    .unwrap();

    let mut request = manifest(&[], Scope::Addition);
    request.sidecars = vec![ManifestSidecar {
        path: "work.json".to_string(),
        json: r#"{"leaf":1,"title":"Nains","summary":"ce que dit le fichier local"}"#.to_string(),
    }];

    let opened = bulk
        .open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect("opening");
    let said = opened.declarations.first().expect("a declaration");
    assert_eq!("work.json", said.path);
    assert_eq!(Some("Nains".to_string()), said.present_name);
    assert_eq!(vec!["summary".to_string()], said.differs, "and only that");

    bulk.commit(&opened.id, &[], &[], &[], DEFAULT_PROFILE)
        .expect("committing");
    let kept: serde_json::Value =
        serde_json::from_slice(&std::fs::read(target.join("work.json")).unwrap()).unwrap();
    assert_eq!(
        "ce que j'ai ecrit", kept["summary"],
        "nobody said to write over it"
    );

    // A second announcement, because the first commit closed its session: a refused
    // declaration leaves nothing waiting in the inbox, unlike a refused volume, so there is
    // nothing for the session to stay open for.
    let again = bulk
        .open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect("opening again");
    bulk.commit(
        &again.id,
        &[],
        &["work.json".to_string()],
        &[],
        DEFAULT_PROFILE,
    )
    .expect("committing again");
    let now: serde_json::Value =
        serde_json::from_slice(&std::fs::read(target.join("work.json")).unwrap()).unwrap();
    assert_eq!("ce que dit le fichier local", now["summary"]);
    assert_eq!(
        "aabbccddeeff0011", now["id"],
        "and the identity is still the library's own"
    );
}

// ----------------------------------------------------- a volume converted on arrival

const KOMGA_ENTRY: &str = r#"{"number":2,"title":"Le ciel"}"#;

/// A volume as another reader leaves it: two pages and a `ComicInfo.xml`, no `entry.json`.
fn a_komga_volume() -> Vec<u8> {
    let mut zip = zip::ZipWriter::new(std::io::Cursor::new(Vec::new()));
    let options = zip::write::SimpleFileOptions::default();
    for i in 0..2 {
        zip.start_file::<_, ()>(format!("{i:03}.jpg"), options)
            .unwrap();
        zip.write_all(&jpeg()).unwrap();
    }
    zip.start_file::<_, ()>("ComicInfo.xml", options).unwrap();
    zip.write_all(b"<ComicInfo><Number>9</Number></ComicInfo>")
        .unwrap();
    zip.finish().unwrap().into_inner()
}

/// Announced, received whole and committed once: the library path it lands on.
fn converted(world: &World, checksum: Option<String>, bytes: &[u8]) -> (ImportResultOf, String) {
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));
    let mut request = manifest(&[("Nains 02.cbz", bytes.len() as u64)], Scope::Addition);
    request.files[0].checksum = checksum;
    request.files[0].rewrite = Some(a_rewrite(KOMGA_ENTRY, &["ComicInfo.xml"]));
    let opened = bulk
        .open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect("opening");
    bulk.receive(&opened.id, "Nains 02.cbz", 0, bytes, CEILING)
        .expect("whole");
    let result = bulk
        .commit(&opened.id, &[], &[], &[], DEFAULT_PROFILE)
        .expect("committing");
    (result, opened.id)
}

type ImportResultOf = leaf_server::api::bulk_import::ImportResult;

#[test]
fn a_komga_volume_is_converted_when_it_is_installed() {
    let world = World::new();
    let bytes = a_komga_volume();

    let (result, _) = converted(&world, None, &bytes);

    assert_eq!(1, result.installed);
    let home = world.library().join("Terres d'Arran/Nains 02.cbz");
    assert!(
        inside(&home, "ComicInfo.xml").is_none(),
        "a ComicInfo left behind is read again as metadata"
    );
    let entry = inside(&home, "entry.json").expect("an entry.json");
    let number = serde_json::from_slice::<serde_json::Value>(&entry).unwrap()["number"].clone();
    assert_eq!(2, number);
    assert_eq!(Some(jpeg()), inside(&home, "000.jpg"));
    assert_eq!(Some(jpeg()), inside(&home, "001.jpg"));
}

#[test]
fn the_checksum_is_checked_against_the_bytes_that_arrived() {
    use sha2::{Digest, Sha256};
    let world = World::new();
    let bytes = a_komga_volume();
    let digest: String = Sha256::digest(&bytes)
        .iter()
        .map(|b| format!("{b:02x}"))
        .collect();

    let (result, _) = converted(&world, Some(digest), &bytes);

    assert!(result.corrupt.is_empty(), "{:?}", result.corrupt);
    assert_eq!(1, result.installed);
}

/// A copy that cannot be read as an archive is dropped, as one failing its checksum is.
/// Kept, it was the announced size, so `state` called it held and the client sent nothing,
/// while every commit set it aside again: a loop with no way out but abandoning the import.
#[test]
fn a_rewrite_on_something_that_is_not_a_zip_is_corrupt_and_its_copy_is_dropped() {
    let world = World::new();

    let (result, id) = converted(&world, None, b"not a zip at all");

    assert_eq!(vec!["Nains 02.cbz".to_string()], result.corrupt);
    assert_eq!(0, result.installed);
    assert!(!world.library().join("Terres d'Arran/Nains 02.cbz").exists());
    assert!(
        result.open,
        "the session stays, so the file can be sent again"
    );
    assert!(!world
        .inbox()
        .join(&id)
        .join("files")
        .join("Nains 02.cbz")
        .exists());
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));
    let state = bulk.state(&id).unwrap().expect("a session");
    assert_eq!(vec!["Nains 02.cbz".to_string()], state.missing);
}

/// Announced with a rewrite and received whole, not committed yet.
fn received_for_conversion(world: &World, bytes: &[u8]) -> (BulkImport, String) {
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));
    let mut request = manifest(&[("Nains 02.cbz", bytes.len() as u64)], Scope::Addition);
    request.files[0].rewrite = Some(a_rewrite(KOMGA_ENTRY, &["ComicInfo.xml"]));
    let opened = bulk
        .open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect("opening");
    bulk.receive(&opened.id, "Nains 02.cbz", 0, bytes, CEILING)
        .expect("whole");
    (bulk, opened.id)
}

/// The rewrite was made on the inbox copy itself, so a move that failed afterwards left a
/// converted copy at another size than announced: `state` asked for it again from an
/// offset past its end, and the commit called it pending for good.
#[test]
fn a_failure_between_the_rewrite_and_the_move_leaves_the_inbox_copy_as_it_was_announced() {
    let world = World::new();
    let bytes = a_komga_volume();
    let (bulk, id) = received_for_conversion(&world, &bytes);
    // A folder where the volume should land: the rewrite succeeds, the move cannot.
    let folder = world.library().join("Terres d'Arran");
    std::fs::create_dir_all(folder.join("Nains 02.cbz/inside")).unwrap();

    bulk.commit(&id, &[], &[], &[], DEFAULT_PROFILE)
        .expect_err("the move fails");

    let held = world.inbox().join(&id).join("files").join("Nains 02.cbz");
    assert_eq!(bytes.len() as u64, std::fs::metadata(&held).unwrap().len());
    assert_eq!(
        1,
        std::fs::read_dir(&folder).unwrap().count(),
        "and nothing half-made is left beside it"
    );

    std::fs::remove_dir_all(folder.join("Nains 02.cbz")).unwrap();
    let result = bulk
        .commit(&id, &[], &[], &[], DEFAULT_PROFILE)
        .expect("retrying");
    assert_eq!(1, result.installed);
    assert!(inside(&folder.join("Nains 02.cbz"), "entry.json").is_some());
}

/// A disk that refuses the rewrite says nothing about the file. Called corrupt, the copy
/// would be dropped and sent again, and refused again, for a fault that was never in it.
#[test]
fn a_disk_that_refuses_the_rewrite_is_an_error_and_not_a_corrupt_archive() {
    let world = World::new();
    let bytes = a_komga_volume();
    let (bulk, id) = received_for_conversion(&world, &bytes);
    let folder = world.library().join("Terres d'Arran");
    std::fs::create_dir_all(&folder).unwrap();
    common::read_only(&folder);

    let failed = bulk.commit(&id, &[], &[], &[], DEFAULT_PROFILE);
    writable(&folder);

    assert!(failed.is_err(), "{failed:?}");
    let held = world.inbox().join(&id).join("files").join("Nains 02.cbz");
    assert_eq!(bytes.len() as u64, std::fs::metadata(&held).unwrap().len());
    let result = bulk
        .commit(&id, &[], &[], &[], DEFAULT_PROFILE)
        .expect("once the disk takes it");
    assert_eq!(1, result.installed);
}

#[test]
fn the_scan_after_a_conversion_reads_the_declared_number_and_title() {
    let world = World::new();
    let bytes = a_komga_volume();
    converted(&world, None, &bytes);

    world.scan();

    assert_eq!(
        Some(2.0),
        world.one::<f64>("SELECT volume_number FROM entry")
    );
    assert_eq!(
        Some("Le ciel".to_string()),
        world.one::<String>("SELECT title FROM entry")
    );
}

#[test]
fn a_conversion_remembers_the_file_it_came_from() {
    let world = World::new();
    let bytes = a_komga_volume();
    converted(&world, None, &bytes);

    let kept = std::fs::metadata(world.library().join("Terres d'Arran/Nains 02.cbz"))
        .unwrap()
        .len();
    let known =
        leaf_server::store::origins::known(&world.db, &["Terres d'Arran/Nains 02.cbz".to_string()])
            .unwrap();
    let origin = &known["Terres d'Arran/Nains 02.cbz"];
    assert_eq!(kept, origin.kept_size);
    assert_eq!(bytes.len() as u64, origin.origin_size);
}

// ------------------------------------------------- a rewritten archive is still home

/// The announcement of `converted`, as the desktop would send it again.
fn announced_again(bytes: &[u8], checksum: Option<String>) -> ImportRequest {
    let mut request = manifest(&[("Nains 02.cbz", bytes.len() as u64)], Scope::Addition);
    request.files[0].checksum = checksum;
    request.files[0].rewrite = Some(a_rewrite(KOMGA_ENTRY, &["ComicInfo.xml"]));
    request
}

/// A rewritten archive is another length than the file announced, so it was asked for again
/// at every import, and the second send was converted once more for nothing.
#[test]
fn a_converted_volume_is_already_there_the_second_time() {
    let world = World::new();
    let bytes = a_komga_volume();
    converted(&world, None, &bytes);
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));

    let opened = bulk
        .open(
            &announced_again(&bytes, None),
            DEFAULT_PROFILE,
            &Ceiling::NONE,
        )
        .expect("opening");

    assert_eq!(vec!["Nains 02.cbz".to_string()], opened.already_there);
    assert!(opened.to_send.is_empty(), "{:?}", opened.to_send);
    assert!(opened.replaces.is_empty(), "{:?}", opened.replaces);
}

#[test]
fn a_converted_volume_is_not_pending_when_committed_again() {
    let world = World::new();
    let bytes = a_komga_volume();
    converted(&world, None, &bytes);
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));
    let opened = bulk
        .open(
            &announced_again(&bytes, None),
            DEFAULT_PROFILE,
            &Ceiling::NONE,
        )
        .expect("opening");

    let result = bulk
        .commit(&opened.id, &[], &[], &[], DEFAULT_PROFILE)
        .expect("committing");

    assert!(result.pending.is_empty(), "{:?}", result.pending);
    assert_eq!(0, result.installed);
}

#[test]
fn a_volume_edited_by_hand_afterwards_is_no_longer_taken_for_its_origin() {
    let world = World::new();
    let bytes = a_komga_volume();
    converted(&world, None, &bytes);
    let home = world.library().join("Terres d'Arran/Nains 02.cbz");
    let mut edited = std::fs::read(&home).unwrap();
    edited.extend_from_slice(b"padding");
    std::fs::write(&home, edited).unwrap();
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));

    let opened = bulk
        .open(
            &announced_again(&bytes, None),
            DEFAULT_PROFILE,
            &Ceiling::NONE,
        )
        .expect("opening");

    assert_eq!(vec!["Nains 02.cbz".to_string()], opened.to_send);
    assert_eq!(1, opened.replaces.len(), "{:?}", opened.replaces);
}

#[test]
fn a_different_file_with_the_same_announced_size_is_not_the_origin() {
    let world = World::new();
    let bytes = a_komga_volume();
    use sha2::{Digest, Sha256};
    let digest: String = Sha256::digest(&bytes)
        .iter()
        .map(|b| format!("{b:02x}"))
        .collect();
    converted(&world, Some(digest.clone()), &bytes);
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));

    let other = "0".repeat(digest.len());
    let opened = bulk
        .open(
            &announced_again(&bytes, Some(other)),
            DEFAULT_PROFILE,
            &Ceiling::NONE,
        )
        .expect("opening");
    assert_eq!(vec!["Nains 02.cbz".to_string()], opened.to_send);

    // And the same digest in capitals is the same file.
    let opened = bulk
        .open(
            &announced_again(&bytes, Some(digest.to_uppercase())),
            DEFAULT_PROFILE,
            &Ceiling::NONE,
        )
        .expect("opening");
    assert!(opened.to_send.is_empty(), "{:?}", opened.to_send);
}

#[test]
fn opening_an_import_reads_the_origins_in_one_query() {
    let world = World::new();
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));
    let names: Vec<String> = (0..100).map(|i| format!("Nains {i:03}.cbz")).collect();
    let keys: Vec<String> = names
        .iter()
        .map(|n| format!("Terres d'Arran/{n}"))
        .collect();

    let before = world.db.statements();
    leaf_server::store::origins::known(&world.db, &keys).unwrap();
    assert_eq!(1, world.db.statements() - before);

    // And `open` pays the same for a hundred files as for one: nothing asks per file.
    let cost = |files: &[(&str, u64)]| {
        let before = world.db.statements();
        bulk.open(
            &manifest(files, Scope::Addition),
            DEFAULT_PROFILE,
            &Ceiling::NONE,
        )
        .unwrap();
        world.db.statements() - before
    };
    let many: Vec<(&str, u64)> = names.iter().map(|n| (n.as_str(), 4)).collect();
    assert_eq!(cost(&many[..1]), cost(&many));
}

/// The rewrite is part of what was asked for, not only the bytes it was asked of. A title
/// corrected in the desktop travels as another `entry.json` beside the same archive, and the
/// origin — sizes and checksum only — answered « already there », so the correction never
/// reached the library.
#[test]
fn a_changed_entry_json_is_sent_again_and_rewritten() {
    let world = World::new();
    let bytes = a_komga_volume();
    converted(&world, None, &bytes);
    // Read by the index, so the replacement has an entry to be recorded against.
    world.scan();
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));
    let mut corrected = announced_again(&bytes, None);
    corrected.files[0].rewrite = Some(a_rewrite(
        r#"{"number":2,"title":"Le ciel corrigé"}"#,
        &["ComicInfo.xml"],
    ));

    let opened = bulk
        .open(&corrected, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect("opening");
    assert_eq!(vec!["Nains 02.cbz".to_string()], opened.to_send);
    bulk.receive(&opened.id, "Nains 02.cbz", 0, &bytes, CEILING)
        .expect("whole");
    let result = bulk
        .commit(
            &opened.id,
            &["Nains 02.cbz".to_string()],
            &[],
            &[],
            DEFAULT_PROFILE,
        )
        .expect("committing");

    assert_eq!(1, result.installed);
    let home = world.library().join("Terres d'Arran/Nains 02.cbz");
    let entry: serde_json::Value =
        serde_json::from_slice(&inside(&home, "entry.json").unwrap()).unwrap();
    assert_eq!("Le ciel corrigé", entry["title"]);
    // And the corrected announcement is now the one the library holds.
    let again = bulk
        .open(&corrected, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect("opening again");
    assert!(again.to_send.is_empty(), "{:?}", again.to_send);
}

/// The same bytes announced with no rewrite would be installed as they came, ComicInfo and
/// all: that is not the archive the library converted, whatever their size.
#[test]
fn the_same_file_announced_without_its_rewrite_is_not_the_converted_one() {
    let world = World::new();
    let bytes = a_komga_volume();
    converted(&world, None, &bytes);
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));

    let opened = bulk
        .open(
            &manifest(&[("Nains 02.cbz", bytes.len() as u64)], Scope::Addition),
            DEFAULT_PROFILE,
            &Ceiling::NONE,
        )
        .expect("opening");

    assert_eq!(vec!["Nains 02.cbz".to_string()], opened.to_send);
}

/// The names to remove are a set: the same request listed in another order is the same
/// request, and was not sent again for it.
#[test]
fn the_names_to_remove_are_compared_whatever_their_order() {
    let world = World::new();
    let bytes = a_komga_volume();
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));
    let mut request = manifest(&[("Nains 02.cbz", bytes.len() as u64)], Scope::Addition);
    request.files[0].rewrite = Some(a_rewrite(KOMGA_ENTRY, &["ComicInfo.xml", "notes.txt"]));
    let opened = bulk
        .open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect("opening");
    bulk.receive(&opened.id, "Nains 02.cbz", 0, &bytes, CEILING)
        .expect("whole");
    bulk.commit(&opened.id, &[], &[], &[], DEFAULT_PROFILE)
        .expect("committing");

    request.files[0].rewrite = Some(a_rewrite(KOMGA_ENTRY, &["notes.txt", "ComicInfo.xml"]));
    let again = bulk
        .open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect("opening again");

    assert!(again.to_send.is_empty(), "{:?}", again.to_send);
}

/// Leaf's own edit of a converted volume is not a hand edit. A title corrected through
/// `PATCH` rewrites the archive and changes its size, and the next import took it for a file
/// changed behind Leaf's back and offered to overwrite the correction.
#[test]
fn a_converted_volume_edited_through_leaf_is_still_home() {
    let world = World::new();
    let bytes = a_komga_volume();
    converted(&world, None, &bytes);
    world.scan();
    let entry: String = world.one("SELECT id FROM entry").unwrap();
    let library = world.library();

    Records::new(&world.db)
        .in_library(&library)
        .patch_entry(
            &entry,
            &EntryPatch {
                title: Some(Some("Le ciel, corrigé ici".into())),
                ..Default::default()
            },
        )
        .expect("patching");
    let bulk = BulkImport::new(&world.inbox(), &library, Arc::clone(&world.db));
    let opened = bulk
        .open(
            &announced_again(&bytes, None),
            DEFAULT_PROFILE,
            &Ceiling::NONE,
        )
        .expect("opening");

    assert!(opened.to_send.is_empty(), "{:?}", opened.to_send);
    assert!(opened.replaces.is_empty(), "{:?}", opened.replaces);
}

/// The stamp a download leaves is Leaf's own write too, and the most frequent one: a
/// converted volume downloaded once was asked for at every import afterwards.
#[test]
fn a_converted_volume_stamped_on_its_way_out_is_still_home() {
    let world = World::new();
    let bytes = a_komga_volume();
    converted(&world, None, &bytes);
    world.scan();
    let entry: String = world.one("SELECT id FROM entry").unwrap();
    let library = world.library();
    let home = library.join("Terres d'Arran/Nains 02.cbz");
    let before = std::fs::metadata(&home).unwrap().len();

    Records::new(&world.db)
        .in_library(&library)
        .stamp_entry(&entry)
        .expect("stamping");
    assert_ne!(
        before,
        std::fs::metadata(&home).unwrap().len(),
        "it was stamped"
    );
    let bulk = BulkImport::new(&world.inbox(), &library, Arc::clone(&world.db));
    let opened = bulk
        .open(
            &announced_again(&bytes, None),
            DEFAULT_PROFILE,
            &Ceiling::NONE,
        )
        .expect("opening");

    assert!(opened.to_send.is_empty(), "{:?}", opened.to_send);
}

/// `Extras/./Nains 02.cbz` and `Extras/Nains 02.cbz` land on one file, so they are one
/// origin. The key was the text as announced, and a second import spelling the path the
/// other way did not find the file it had converted.
#[test]
fn an_origin_is_keyed_by_where_the_file_lands_not_by_how_its_path_was_spelled() {
    let world = World::new();
    let bytes = a_komga_volume();
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));
    let mut request = manifest(
        &[("Extras/./Nains 02.cbz", bytes.len() as u64)],
        Scope::Addition,
    );
    request.files[0].rewrite = Some(a_rewrite(KOMGA_ENTRY, &["ComicInfo.xml"]));
    let opened = bulk
        .open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect("opening");
    bulk.receive(&opened.id, "Extras/./Nains 02.cbz", 0, &bytes, CEILING)
        .expect("whole");
    bulk.commit(&opened.id, &[], &[], &[], DEFAULT_PROFILE)
        .expect("committing");

    request.files[0].path = "Extras/Nains 02.cbz".into();
    let again = bulk
        .open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect("opening again");

    assert!(again.to_send.is_empty(), "{:?}", again.to_send);
}

/// A member is removed by its name at any depth, so an empty name names nothing at all —
/// and a request that says « remove » and means nothing is a mistake of the sender's,
/// answered before the archive travels rather than ignored after.
#[test]
fn an_empty_name_to_remove_is_refused() {
    let (_dir, bulk) = a_bulk();
    for empty in ["", "   "] {
        let mut request = manifest(&[("Nains 01.cbz", 4)], Scope::Addition);
        request.files[0].rewrite = Some(a_rewrite(r#"{"leaf":1}"#, &[empty]));

        let refused = bulk
            .open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
            .expect_err("an empty name");

        assert!(refused.to_string().contains("Nains 01.cbz"), "{refused}");
    }
}

// ------------------------------------------- a book put in the archive on arrival

const BOOK_ENTRY: &str = r#"{"leaf":1}"#;
const BOOK: &str = r#"{"leaf":1,"title":"Le Combat"}"#;

/// A rewrite that turns the archive into a book standing alone.
fn a_book_rewrite(one_shot: &str) -> leaf_server::api::bulk_import::ArchiveRewrite {
    let mut rewrite = a_rewrite(BOOK_ENTRY, &["ComicInfo.xml"]);
    rewrite.one_shot_json = Some(one_shot.into());
    rewrite
}

/// A universe folder announced with one archive at its root, asked to become a book.
fn a_book_at_a_universe_root(bytes: &[u8], one_shot: &str) -> ImportRequest {
    let mut request = manifest(&[("Le Combat.cbz", bytes.len() as u64)], Scope::Addition);
    request.sidecars = vec![declaring(
        "universe.json",
        r#"{"leaf":1,"name":"Terres d'Arran"}"#,
    )];
    request.files[0].rewrite = Some(a_book_rewrite(one_shot));
    request
}

/// Announced, sent whole and committed: the world, the bulk and the library path it landed on.
fn a_book_arrived() -> (World, BulkImport, Vec<u8>) {
    let world = World::new();
    let bytes = a_komga_volume();
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));
    let opened = bulk
        .open(
            &a_book_at_a_universe_root(&bytes, BOOK),
            DEFAULT_PROFILE,
            &Ceiling::NONE,
        )
        .expect("opening");
    bulk.receive(&opened.id, "Le Combat.cbz", 0, &bytes, CEILING)
        .expect("whole");
    bulk.commit(&opened.id, &[], &[], &[], DEFAULT_PROFILE)
        .expect("committing");
    (world, bulk, bytes)
}

/// Without a member of its own the book was only a request: the archive reached the
/// library as an ordinary volume and the root of the universe disregarded it.
#[test]
fn a_book_asked_for_is_written_into_the_archive_on_arrival() {
    let (world, _, _) = a_book_arrived();

    let home = world.library().join("Terres d'Arran/Le Combat.cbz");
    assert_eq!(
        Some(BOOK.as_bytes().to_vec()),
        inside(&home, "oneshot.json")
    );
    assert!(inside(&home, "entry.json").is_some());
    assert!(inside(&home, "ComicInfo.xml").is_none());
    assert_eq!(Some(jpeg()), inside(&home, "000.jpg"));
    assert_eq!(Some(jpeg()), inside(&home, "001.jpg"));
}

#[test]
fn a_book_dropped_at_a_universe_root_is_filed_in_that_universe_after_import() {
    let (world, _, _) = a_book_arrived();

    let report = Scanner::new(Arc::clone(&world.db), true)
        .scan(&[world.library()])
        .expect("scanning");

    assert_eq!(
        Some(1),
        world.one::<i64>("SELECT one_shot FROM edition"),
        "a book of its own"
    );
    assert_eq!(
        world.one::<String>("SELECT id FROM universe"),
        world.one::<String>("SELECT universe_id FROM work"),
        "of the universe whose root it was dropped at"
    );
    assert!(report.disregarded.is_empty(), "{:?}", report.disregarded);
}

#[test]
fn a_one_shot_json_that_is_not_an_object_is_refused_before_a_byte_moves() {
    let (dir, bulk) = a_bulk();
    for not_an_object in ["[1]", "not json", "\"text\""] {
        let request = a_book_at_a_universe_root(b"abcd", not_an_object);

        let refused = bulk
            .open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
            .expect_err("not an object");

        assert!(refused.to_string().contains("Le Combat.cbz"), "{refused}");
    }
    assert_eq!(
        0,
        std::fs::read_dir(dir.path().join("inbox")).unwrap().count(),
        "no session was opened"
    );
}

/// A book asked for without a `oneShotJson` in the announcement is a plain rewrite: the
/// `oneshot.json` the archive already carries is not removed and not replaced.
#[test]
fn an_announcement_without_a_one_shot_json_keeps_the_one_in_the_archive() {
    let world = World::new();
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));
    let carried = br#"{"leaf":1,"title":"Already inside"}"#;
    let mut zip = zip::ZipWriter::new(std::io::Cursor::new(Vec::new()));
    let options = zip::write::SimpleFileOptions::default();
    zip.start_file::<_, ()>("000.jpg", options).unwrap();
    zip.write_all(&jpeg()).unwrap();
    zip.start_file::<_, ()>("oneshot.json", options).unwrap();
    zip.write_all(carried).unwrap();
    let bytes = zip.finish().unwrap().into_inner();
    let mut request = a_book_at_a_universe_root(&bytes, BOOK);
    request.files[0].rewrite = Some(a_rewrite(BOOK_ENTRY, &["ComicInfo.xml"]));

    let opened = bulk
        .open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect("opening");
    bulk.receive(&opened.id, "Le Combat.cbz", 0, &bytes, CEILING)
        .expect("whole");
    bulk.commit(&opened.id, &[], &[], &[], DEFAULT_PROFILE)
        .expect("committing");

    let home = world.library().join("Terres d'Arran/Le Combat.cbz");
    assert_eq!(Some(carried.to_vec()), inside(&home, "oneshot.json"));
    assert!(inside(&home, "entry.json").is_some());
}

#[test]
fn the_same_book_announced_again_is_already_there() {
    let (_world, bulk, bytes) = a_book_arrived();

    let opened = bulk
        .open(
            &a_book_at_a_universe_root(&bytes, BOOK),
            DEFAULT_PROFILE,
            &Ceiling::NONE,
        )
        .expect("opening again");

    assert_eq!(vec!["Le Combat.cbz".to_string()], opened.already_there);
    assert!(opened.to_send.is_empty(), "{:?}", opened.to_send);
}

/// The book is part of what was asked for: a title corrected in the desktop never reached
/// the library when only the `entry.json` and the names were compared.
#[test]
fn asking_for_another_book_json_sends_the_file_again() {
    let (_world, bulk, bytes) = a_book_arrived();

    let opened = bulk
        .open(
            &a_book_at_a_universe_root(&bytes, r#"{"leaf":1,"title":"Le Combat ordinaire"}"#),
            DEFAULT_PROFILE,
            &Ceiling::NONE,
        )
        .expect("opening again");

    assert_eq!(vec!["Le Combat.cbz".to_string()], opened.to_send);
}

/// Origins recorded before books existed were fingerprinted as `[entry, remove]`. A
/// fingerprint that gained a third element for everybody would ask for every one of them
/// again at the next import.
#[test]
fn an_origin_recorded_before_books_existed_still_finds_its_file() {
    use sha2::{Digest, Sha256};
    let world = World::new();
    let bytes = a_komga_volume();
    let asked = serde_json::json!([KOMGA_ENTRY, ["ComicInfo.xml"]]).to_string();
    let digest: String = Sha256::digest(asked.as_bytes())
        .iter()
        .map(|b| format!("{b:02x}"))
        .collect();
    let home = world.library().join("Terres d'Arran/Nains 02.cbz");
    std::fs::create_dir_all(home.parent().unwrap()).unwrap();
    std::fs::write(&home, b"kept").unwrap();
    leaf_server::store::origins::record(
        &world.db,
        &[leaf_server::store::origins::Origin {
            path: "Terres d'Arran/Nains 02.cbz".into(),
            kept_size: 4,
            origin_size: bytes.len() as u64,
            origin_checksum: None,
            rewrite: Some(digest),
        }],
    )
    .unwrap();
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));

    let opened = bulk
        .open(
            &announced_again(&bytes, None),
            DEFAULT_PROFILE,
            &Ceiling::NONE,
        )
        .expect("opening");

    assert_eq!(vec!["Nains 02.cbz".to_string()], opened.already_there);
}

// ------------------------------------------- nestings the model has no room for

/// An announcement of one archive under each given folder, declaring what it is told to.
fn nested(declarations: &[&str]) -> ImportRequest {
    let mut request = manifest(&[("Elfes/Tome 1.cbz", 4)], Scope::Addition);
    request.sidecars = declarations
        .iter()
        .map(|path| declaring(path, r#"{"leaf":1}"#))
        .collect();
    request
}

/// The scan reads a universe inside a universe as a series and says so afterwards; an
/// announcement that writes it is the sender's mistake, and the files have not travelled yet.
#[test]
fn a_universe_inside_a_universe_is_refused_naming_both() {
    let (dir, bulk) = a_bulk();

    let refused = bulk
        .open(
            &nested(&["universe.json", "Terres d'Arran/universe.json"]),
            DEFAULT_PROFILE,
            &Ceiling::NONE,
        )
        .expect_err("a universe in a universe");

    let message = refused.to_string();
    assert!(message.contains("Terres d'Arran"), "{message}");
    assert!(message.contains("the root folder"), "{message}");
    assert_eq!(
        0,
        std::fs::read_dir(dir.path().join("inbox")).unwrap().count(),
        "no session was opened"
    );
}

#[test]
fn a_universe_inside_a_series_is_refused() {
    let (_dir, bulk) = a_bulk();

    let refused = bulk
        .open(
            &nested(&["Elfes/work.json", "Elfes/Cycle/universe.json"]),
            DEFAULT_PROFILE,
            &Ceiling::NONE,
        )
        .expect_err("a universe in a series");

    let message = refused.to_string();
    assert!(message.contains("Elfes/Cycle"), "{message}");
    assert!(message.contains("Elfes"), "{message}");
    assert!(message.contains("declares a universe inside"), "{message}");
}

#[test]
fn a_series_inside_a_series_is_refused() {
    let (_dir, bulk) = a_bulk();

    let refused = bulk
        .open(
            &nested(&["work.json", "Elfes/work.json"]),
            DEFAULT_PROFILE,
            &Ceiling::NONE,
        )
        .expect_err("a series in a series");

    let message = refused.to_string();
    assert!(message.contains("Elfes"), "{message}");
    assert!(message.contains("the root folder"), "{message}");
}

/// Several wrong nestings at once: the one named is the first in path order, on every run.
/// The declarations used to sit in a hash map, so which one was reported changed from one
/// process to the next.
#[test]
fn when_several_nestings_are_wrong_the_first_by_path_is_the_one_named() {
    for _ in 0..20 {
        let (_dir, bulk) = a_bulk();

        let refused = bulk
            .open(
                &nested(&[
                    "Zeta/work.json",
                    "Zeta/Cycle/universe.json",
                    "Alpha/work.json",
                    "Alpha/Cycle/universe.json",
                ]),
                DEFAULT_PROFILE,
                &Ceiling::NONE,
            )
            .expect_err("two wrong nestings");

        assert!(refused.to_string().contains("Alpha/Cycle"), "{refused}");
    }
}

/// The announcement is compared with itself and nothing else: an edition declared straight
/// under a universe, with no work between them, is not the sender's contradiction, so it is
/// accepted here and left to the scan, which reports what it cannot place.
#[test]
fn an_edition_declared_straight_under_a_universe_is_accepted() {
    let (_dir, bulk) = a_bulk();

    bulk.open(
        &nested(&["universe.json", "Elfes/edition.json"]),
        DEFAULT_PROFILE,
        &Ceiling::NONE,
    )
    .expect("a universe holding an edition");
}

#[test]
fn a_declaration_inside_an_edition_is_refused() {
    let (_dir, bulk) = a_bulk();
    for inner in ["work.json", "universe.json", "edition.json"] {
        bulk.open(
            &nested(&["Elfes/edition.json", &format!("Elfes/Perfect/{inner}")]),
            DEFAULT_PROFILE,
            &Ceiling::NONE,
        )
        .expect_err(inner);
    }
}

/// The shape the model is made of, and the one a too-eager check would break first.
#[test]
fn a_universe_of_series_with_their_editions_is_accepted() {
    let (_dir, bulk) = a_bulk();

    bulk.open(
        &nested(&[
            "universe.json",
            "Elfes/work.json",
            "Elfes/Perfect/edition.json",
        ]),
        DEFAULT_PROFILE,
        &Ceiling::NONE,
    )
    .expect("the model's own nesting");
}

/// Only what the announcement declares can be compared. An edition whose series is on the
/// disk already, announced alone, is what the server cannot judge from here.
#[test]
fn an_edition_whose_series_is_not_announced_is_still_accepted() {
    let (_dir, bulk) = a_bulk();

    bulk.open(
        &nested(&["Perfect/edition.json"]),
        DEFAULT_PROFILE,
        &Ceiling::NONE,
    )
    .expect("an edition alone");
}

// ------------------------------------------------- reading orders in an announcement

/// An announcement whose only declaration is the `universe.json` of a folder named `root`.
fn announcing_universe(root: &str, json: &str) -> ImportRequest {
    let mut request = manifest(&[], Scope::Addition);
    request.root = root.to_string();
    request.sidecars = vec![ManifestSidecar {
        path: "universe.json".to_string(),
        json: json.to_string(),
    }];
    request
}

fn no_session_was_opened(dir: &tempfile::TempDir) -> bool {
    std::fs::read_dir(dir.path().join("inbox")).unwrap().count() == 0
}

/// Found while the files are still on the sender's disk: a loop discovered by the scan
/// afterwards leaves every order of it without the inclusion, after the transfer was paid.
#[test]
fn an_order_that_loops_is_refused_before_a_byte_moves() {
    let (dir, bulk) = a_bulk();
    let request = announcing_universe(
        "Monde d'Aquilon",
        r#"{"leaf":1,"orders":[
            {"id":"a","steps":[{"work":"X"},{"include":"b"}]},
            {"id":"b","steps":[{"include":"a"}]}
        ]}"#,
    );

    let refused = bulk
        .open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect_err("a loop is refused")
        .to_string();
    assert!(refused.contains("universe.json"), "{refused}");
    assert!(
        refused.contains("\"a\"") && refused.contains("\"b\""),
        "{refused}"
    );
    assert!(refused.contains("loop"), "{refused}");
    assert!(no_session_was_opened(&dir));
}

#[test]
fn an_include_naming_no_order_of_the_file_is_refused() {
    let (dir, bulk) = a_bulk();
    let request = announcing_universe(
        "Monde d'Aquilon",
        r#"{"leaf":1,"orders":[{"id":"a","steps":[{"work":"X"},{"include":"nowhere"}]}]}"#,
    );

    let refused = bulk
        .open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect_err("refused")
        .to_string();
    assert!(refused.contains("universe.json"), "{refused}");
    assert!(refused.contains("order \"a\", step 2"), "{refused}");
    assert!(refused.contains("nowhere"), "{refused}");
    assert!(no_session_was_opened(&dir));
}

#[test]
fn an_include_step_carrying_a_range_is_refused() {
    let (dir, bulk) = a_bulk();
    let request = announcing_universe(
        "Monde d'Aquilon",
        r#"{"leaf":1,"orders":[
            {"id":"a","steps":[{"include":"b","from":2}]},
            {"id":"b","steps":[{"work":"X"}]}
        ]}"#,
    );

    let refused = bulk
        .open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect_err("refused")
        .to_string();
    assert!(refused.contains("order \"a\", step 1"), "{refused}");
    assert!(refused.contains("nothing else"), "{refused}");
    assert!(no_session_was_opened(&dir));
}

#[test]
fn orders_that_include_each_other_without_a_loop_are_accepted() {
    let (_dir, bulk) = a_bulk();
    let request = announcing_universe(
        "Monde d'Aquilon",
        r#"{"leaf":1,"name":"Monde d'Aquilon","orders":[
            {"id":"terres-d-arran","name":"Terres d'Arran","steps":[{"work":"Terres d'Arran"}]},
            {"id":"tout-aquilon","name":"Tout Aquilon","steps":[
                {"work":"Lanfeust de Troy"},{"include":"terres-d-arran"}
            ]}
        ]}"#,
    );
    bulk.open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect("orders that nest are fine");
}

/// The work may be on the disk already, or arrive in a later announcement of the same
/// universe; the announcement only sees what it carries.
#[test]
fn a_step_naming_a_work_not_announced_is_still_accepted() {
    let (_dir, bulk) = a_bulk();
    let request = announcing_universe(
        "Monde d'Aquilon",
        r#"{"leaf":1,"orders":[{"id":"a","steps":[{"work":"Not in this announcement"}]}]}"#,
    );
    bulk.open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect("an absent work is the scan's to say");
}

/// The scan keeps the first of two orders sharing an identifier and says so; refusing here
/// would turn away libraries the server reads.
#[test]
fn two_orders_with_one_identifier_are_still_accepted() {
    let (_dir, bulk) = a_bulk();
    let request = announcing_universe(
        "Monde d'Aquilon",
        r#"{"leaf":1,"orders":[
            {"id":"a","steps":[{"work":"X"}]},
            {"id":"a","steps":[{"work":"Y"}]}
        ]}"#,
    );
    bulk.open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect("a duplicate identifier is the scan's to say");
}

// ------------------------------------------------------------ canonical names on arrival

/// A file the client read a number in, announced under `path`.
fn numbered(path: &str, number: f64) -> ManifestFile {
    ManifestFile {
        path: path.into(),
        size: common::archive_bytes(None).len() as u64,
        found: Some(leaf_server::api::reimport::FoundInFile {
            number: Some(number),
            ..Default::default()
        }),
        ..Default::default()
    }
}

fn declaration(path: &str, json: &str) -> ManifestSidecar {
    ManifestSidecar {
        path: path.into(),
        json: json.into(),
    }
}

/// Announced, received whole and committed: every file under the library, relative to it.
fn imported_files(request: &ImportRequest) -> Vec<String> {
    let (dir, bulk) = a_bulk();
    let opened = bulk
        .open(request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect("opening");
    for path in &opened.to_send {
        bulk.receive(&opened.id, path, 0, &common::archive_bytes(None), 1 << 30)
            .expect("receiving");
    }
    bulk.commit(&opened.id, &[], &[], &[], DEFAULT_PROFILE)
        .expect("committing");
    let library = dir.path().join("library");
    let mut out = Vec::new();
    let mut stack = vec![library.clone()];
    while let Some(folder) = stack.pop() {
        for entry in std::fs::read_dir(&folder).unwrap().flatten() {
            let path = entry.path();
            if path.is_dir() {
                stack.push(path);
            } else if path.extension().is_some_and(|e| e == "cbz") {
                out.push(
                    path.strip_prefix(&library)
                        .unwrap()
                        .to_string_lossy()
                        .into_owned(),
                );
            }
        }
    }
    out.sort();
    out
}

#[test]
fn an_imported_file_takes_its_canonical_name() {
    let request = ImportRequest {
        root: "Dragon Ball".into(),
        files: vec![numbered("DB couleur 01.cbz", 1.0)],
        scope: Scope::Addition,
        ..Default::default()
    };

    assert_eq!(
        vec!["Dragon Ball/dragon-ball_tome_001.cbz".to_string()],
        imported_files(&request)
    );
}

#[test]
fn an_imported_folder_takes_the_real_name_of_its_series() {
    let request = ImportRequest {
        root: "Chevaliers".into(),
        files: vec![numbered("zodiaque-vf/ch 12.cbz", 12.0)],
        sidecars: vec![declaration(
            "zodiaque-vf/work.json",
            r#"{"title":"Les Chevaliers du Zodiaque"}"#,
        )],
        scope: Scope::Addition,
        ..Default::default()
    };

    assert_eq!(
        vec![
            "Chevaliers/Les Chevaliers du Zodiaque/chevaliers-du-zodiaque_tome_012.cbz".to_string()
        ],
        imported_files(&request)
    );
}

#[test]
fn a_file_without_a_number_takes_what_it_was_called() {
    let request = ImportRequest {
        root: "Dragon Ball".into(),
        files: vec![ManifestFile {
            path: "Hors série.cbz".into(),
            size: common::archive_bytes(None).len() as u64,
            ..Default::default()
        }],
        scope: Scope::Addition,
        ..Default::default()
    };

    assert_eq!(
        vec!["Dragon Ball/Hors série.cbz".to_string()],
        imported_files(&request)
    );
}

#[test]
fn the_edition_is_said_only_when_the_series_has_two() {
    let editions = |names: &[&str]| -> Vec<String> {
        imported_files(&ImportRequest {
            root: "Dragon Ball".into(),
            files: names
                .iter()
                .map(|name| numbered(&format!("{name}/DB 01.cbz"), 1.0))
                .collect(),
            sidecars: names
                .iter()
                .map(|name| {
                    declaration(
                        &format!("{name}/edition.json"),
                        &format!(r#"{{"name":"{name}"}}"#),
                    )
                })
                .collect(),
            scope: Scope::Addition,
            ..Default::default()
        })
    };

    assert_eq!(
        vec!["Dragon Ball/Perfect/dragon-ball_tome_001.cbz".to_string()],
        editions(&["Perfect"])
    );
    assert_eq!(
        vec![
            "Dragon Ball/Originale/dragon-ball_originale_tome_001.cbz".to_string(),
            "Dragon Ball/Perfect/dragon-ball_perfect_tome_001.cbz".to_string(),
        ],
        editions(&["Perfect", "Originale"])
    );
}

#[test]
fn two_files_with_the_same_canonical_name_are_a_collision_and_neither_lands() {
    let request = ImportRequest {
        root: "Dragon Ball".into(),
        files: vec![numbered("a 01.cbz", 1.0), numbered("b 01.cbz", 1.0)],
        scope: Scope::Addition,
        ..Default::default()
    };

    assert!(imported_files(&request).is_empty());
}

#[test]
fn a_zip_lands_as_a_cbz() {
    let request = ImportRequest {
        root: "Dragon Ball".into(),
        files: vec![numbered("DB 02.zip", 2.0)],
        scope: Scope::Addition,
        ..Default::default()
    };

    assert_eq!(
        vec!["Dragon Ball/dragon-ball_tome_002.cbz".to_string()],
        imported_files(&request)
    );
}

#[test]
fn an_announcement_says_where_each_file_will_be_filed() {
    let (_dir, bulk) = a_bulk();
    let request = ImportRequest {
        root: "Dragon Ball".into(),
        files: vec![
            numbered("Perfect/DB couleur 01.cbz", 1.0),
            numbered("Perfect/DB couleur 02.cbz", 2.0),
            numbered("Perfect/copie 02.cbz", 2.0),
        ],
        sidecars: vec![declaration(
            "Perfect/edition.json",
            r#"{"title":"Perfect"}"#,
        )],
        scope: Scope::Addition,
        ..Default::default()
    };

    let opened = bulk
        .open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect("opening");
    let said = serde_json::to_value(&opened).unwrap();

    assert_eq!(
        serde_json::json!([{
            "path": "Perfect/DB couleur 01.cbz",
            "folder": "Dragon Ball › Perfect",
            "file": "dragon-ball_tome_001.cbz",
            "wasCalled": "DB couleur 01.cbz",
        }]),
        said["landsAs"]
    );
    assert_eq!(
        serde_json::json!([
            {"path": "Perfect/DB couleur 02.cbz", "with": "Perfect/copie 02.cbz"},
            {"path": "Perfect/copie 02.cbz", "with": "Perfect/DB couleur 02.cbz"},
        ]),
        said["collisions"]
    );
    assert_eq!(serde_json::json!([]), said["reshelved"]);
}

#[test]
fn the_three_doors_give_the_same_name_to_the_same_volume() {
    // The door of a staged file (`POST /entries` and `POST /drop` both end in it) ...
    let world = World::new();
    let series = world.with_bleach();
    let intake = Intake::new(&world.inbox(), Arc::clone(&world.db));
    let file = staged(
        &intake,
        "whatever it was called.cbz",
        Some(&EntryJson {
            leaf: Some(1),
            work: Some("Bleach".into()),
            number: Some(3.0),
            ..Default::default()
        }),
    );
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
    let by_the_door = Path::new(&filed.path).file_name().unwrap().to_owned();

    // ... and the bulk import's, for the same volume of the same series.
    let by_the_import = imported_files(&ImportRequest {
        root: "Bleach".into(),
        files: vec![numbered("other name 03.cbz", 3.0)],
        scope: Scope::Addition,
        ..Default::default()
    });

    assert_eq!(
        vec![format!("Bleach/{}", by_the_door.to_string_lossy())],
        by_the_import
    );
}

#[test]
fn a_staged_file_is_named_by_the_title_and_universe_of_its_series() {
    let world = World::new();
    let folder = world.library().join("Saga/old-folder");
    archive(
        &folder.join("Tome 1.cbz"),
        Some(&EntryJson {
            leaf: Some(1),
            work: Some("Real Title".into()),
            number: Some(1.0),
            ..Default::default()
        }),
    );
    std::fs::write(
        folder.join("work.json"),
        r#"{"leaf":1,"title":"Real Title"}"#,
    )
    .unwrap();
    std::fs::write(
        world.library().join("Saga/universe.json"),
        r#"{"leaf":1,"name":"Saga"}"#,
    )
    .unwrap();
    world.scan();
    let series = world.one::<String>("SELECT id FROM edition").unwrap();
    let intake = Intake::new(&world.inbox(), Arc::clone(&world.db));
    let file = staged(
        &intake,
        "x.cbz",
        Some(&EntryJson {
            leaf: Some(1),
            work: Some("Real Title".into()),
            number: Some(2.0),
            ..Default::default()
        }),
    );
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

    assert!(
        Path::new(&filed.path).ends_with("saga_real-title_tome_002.cbz"),
        "{}",
        filed.path
    );
}

#[test]
fn a_staged_file_without_a_number_is_filed_under_the_name_it_had() {
    let world = World::new();
    let series = world.with_bleach();
    let intake = Intake::new(&world.inbox(), Arc::clone(&world.db));
    let file = staged(&intake, "Hors série.cbz", None);
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

    assert!(Path::new(&filed.path).ends_with("Bleach/Hors série.cbz"));
}

// ------------------------------------------------------------------ adding editions

fn new_editions(
    world: &World,
    series: &str,
    files: Vec<ManifestFile>,
    sidecars: Vec<ManifestSidecar>,
    current: Option<&str>,
) -> ImportRequest {
    let _ = world;
    ImportRequest {
        root: "Ajout".into(),
        files,
        sidecars,
        scope: Scope::Addition,
        target: Some(leaf_server::api::reimport::ImportTarget {
            kind: leaf_server::api::reimport::TargetKind::SeriesNewEditions,
            id: series.to_string(),
        }),
        current_edition_name: current.map(str::to_string),
    }
}

fn opened_said(world: &World, request: &ImportRequest) -> serde_json::Value {
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));
    serde_json::to_value(
        bulk.open(request, DEFAULT_PROFILE, &Ceiling::NONE)
            .expect("opening"),
    )
    .unwrap()
}

#[test]
fn announcing_new_editions_names_the_ones_that_have_no_name() {
    let world = World::new();
    let series = world.with_bleach();
    let request = new_editions(
        &world,
        &series,
        vec![
            numbered("Deluxe/DB 01.cbz", 1.0),
            numbered("Originale/DB 01.cbz", 1.0),
        ],
        vec![declaration(
            "Originale/edition.json",
            r#"{"name":"Originale"}"#,
        )],
        None,
    );

    let said = opened_said(&world, &request);

    assert_eq!(
        serde_json::json!([
            {"path": "@current", "current": true},
            {"path": "Deluxe", "current": false},
        ]),
        said["editionsNeedingAName"]
    );
}

#[test]
fn an_edition_json_with_a_name_needs_no_name() {
    let world = World::new();
    let series = world.with_bleach();
    let request = new_editions(
        &world,
        &series,
        vec![numbered("Originale/DB 01.cbz", 1.0)],
        vec![declaration(
            "Originale/edition.json",
            r#"{"name":"Originale"}"#,
        )],
        Some("Première"),
    );

    let said = opened_said(&world, &request);

    assert_eq!(serde_json::json!([]), said["editionsNeedingAName"]);
    assert_eq!(serde_json::json!([]), said["problems"]);
}

#[test]
fn a_series_without_a_declared_edition_asks_a_name_for_its_current_one() {
    let world = World::new();
    let series = world.with_bleach();
    let request = new_editions(
        &world,
        &series,
        vec![numbered("Originale/DB 01.cbz", 1.0)],
        vec![declaration(
            "Originale/edition.json",
            r#"{"name":"Originale"}"#,
        )],
        None,
    );

    let said = opened_said(&world, &request);

    assert_eq!(
        serde_json::json!([{"path": "@current", "current": true}]),
        said["editionsNeedingAName"]
    );
}

#[test]
fn two_editions_of_the_same_name_are_refused_whatever_the_case() {
    let world = World::new();
    let series = world.with_bleach();
    let request = new_editions(
        &world,
        &series,
        vec![numbered("A/DB 01.cbz", 1.0), numbered("B/DB 01.cbz", 1.0)],
        vec![
            declaration("A/edition.json", r#"{"name":"Originale"}"#),
            declaration("B/edition.json", r#"{"name":"originale"}"#),
        ],
        Some("Première"),
    );

    let said = opened_said(&world, &request);

    assert_eq!(
        serde_json::json!([{"path": "B", "kind": "DUPLICATE", "with": "Originale"}]),
        said["problems"]
    );
}

#[test]
fn an_empty_name_is_refused() {
    let world = World::new();
    let series = world.with_bleach();
    let request = new_editions(
        &world,
        &series,
        vec![numbered("A/DB 01.cbz", 1.0)],
        vec![declaration("A/edition.json", r#"{"name":"  "}"#)],
        Some("Première"),
    );

    let said = opened_said(&world, &request);

    assert_eq!(
        serde_json::json!([{"path": "A", "kind": "EMPTY"}]),
        said["problems"]
    );
}

#[test]
fn a_name_equal_to_an_existing_edition_is_refused_and_a_commit_installs_nothing() {
    let world = World::new();
    let series = world.with_bleach();
    let request = new_editions(
        &world,
        &series,
        vec![numbered("A/DB 01.cbz", 1.0)],
        vec![declaration("A/edition.json", r#"{"name":"première"}"#)],
        Some("Première"),
    );
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));
    let opened = bulk
        .open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect("opening");
    bulk.receive(
        &opened.id,
        "A/DB 01.cbz",
        0,
        &common::archive_bytes(None),
        1 << 30,
    )
    .expect("receiving");

    let refused = bulk.commit(&opened.id, &[], &[], &[], DEFAULT_PROFILE);

    assert!(refused.is_err());
    assert!(!world.library().join("Bleach/première").exists());
}

fn committed_new_editions(world: &World, request: &ImportRequest, files: &[&str]) {
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));
    let opened = bulk
        .open(request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect("opening");
    for path in files {
        bulk.receive(&opened.id, path, 0, &common::archive_bytes(None), 1 << 30)
            .expect("receiving");
    }
    let scanner = Scanner::new(Arc::clone(&world.db), true);
    leaf_server::api::renames::commit_import(&scanner, &world.db, &bulk, &opened.id, |first| {
        bulk.commit_with(&opened.id, &[], &[], &[], DEFAULT_PROFILE, first)
    })
    .expect("committing");
    scanner
        .scan(&[world.library()])
        .expect("scanning after the commit");
}

#[test]
fn committing_new_editions_moves_the_current_edition_into_its_folder() {
    let world = World::new();
    let series = world.with_bleach();
    world
        .db
        .write(|cx| {
            cx.execute(
                "INSERT INTO progress (entry_id, edition_id, page, finished, updated_at)
                 SELECT id, edition_id, 42, 0, 1 FROM entry",
                [],
            )?;
            Ok(())
        })
        .unwrap();
    let entries: Vec<String> = world
        .db
        .read(|cx| {
            cx.query("SELECT id FROM entry ORDER BY volume_number", [], |r| {
                r.get(0)
            })
        })
        .unwrap();
    let request = new_editions(
        &world,
        &series,
        vec![numbered("Deluxe/DB 01.cbz", 1.0)],
        vec![declaration("Deluxe/edition.json", r#"{"name":"Deluxe"}"#)],
        Some("Originale"),
    );

    committed_new_editions(&world, &request, &["Deluxe/DB 01.cbz"]);

    let library = world.library();
    for n in 1..=2 {
        assert!(
            library
                .join(format!("Bleach/Originale/bleach_originale_tome_00{n}.cbz"))
                .exists(),
            "volume {n} follows its edition into its folder"
        );
    }
    assert!(library
        .join("Bleach/Deluxe/bleach_deluxe_tome_001.cbz")
        .exists());
    let kept: Vec<String> = world
        .db
        .read(|cx| {
            cx.query(
                "SELECT entry_id FROM progress WHERE page = 42 ORDER BY entry_id",
                [],
                |r| r.get(0),
            )
        })
        .unwrap();
    let mut wanted = entries.clone();
    wanted.sort();
    assert_eq!(wanted, kept, "nobody's place is lost");
    assert_eq!(2, world.one::<i64>("SELECT COUNT(*) FROM edition").unwrap());
}

#[test]
fn a_commit_that_adds_two_editions_makes_one_transition() {
    let world = World::new();
    let series = world.with_bleach();
    let request = new_editions(
        &world,
        &series,
        vec![numbered("A/DB 01.cbz", 1.0), numbered("B/DB 01.cbz", 1.0)],
        vec![
            declaration("A/edition.json", r#"{"name":"Alpha"}"#),
            declaration("B/edition.json", r#"{"name":"Beta"}"#),
        ],
        Some("Originale"),
    );

    committed_new_editions(&world, &request, &["A/DB 01.cbz", "B/DB 01.cbz"]);

    assert_eq!(3, world.one::<i64>("SELECT COUNT(*) FROM edition").unwrap());
    assert_eq!(
        1,
        world
            .one::<i64>("SELECT COUNT(*) FROM rename_plan")
            .unwrap(),
        "the first edition moved once"
    );
    let library = world.library();
    assert!(library
        .join("Bleach/Originale/bleach_originale_tome_001.cbz")
        .exists());
    assert!(library
        .join("Bleach/Alpha/bleach_alpha_tome_001.cbz")
        .exists());
    assert!(library
        .join("Bleach/Beta/bleach_beta_tome_001.cbz")
        .exists());
}

#[test]
fn a_blocked_commit_does_not_move_the_current_edition() {
    let world = World::new();
    let series = world.with_bleach();
    let request = new_editions(
        &world,
        &series,
        vec![numbered("Deluxe/DB 01.cbz", 1.0)],
        vec![],
        Some("Originale"),
    );
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));
    let opened = bulk
        .open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect("opening");
    bulk.receive(
        &opened.id,
        "Deluxe/DB 01.cbz",
        0,
        &common::archive_bytes(None),
        1 << 30,
    )
    .expect("receiving");
    let scanner = Scanner::new(Arc::clone(&world.db), true);

    let refused =
        leaf_server::api::renames::commit_import(&scanner, &world.db, &bulk, &opened.id, |first| {
            bulk.commit_with(&opened.id, &[], &[], &[], DEFAULT_PROFILE, first)
        });

    assert!(
        refused.is_err(),
        "an edition without a name installs nothing"
    );
    assert_eq!(
        0,
        world
            .one::<i64>("SELECT COUNT(*) FROM rename_plan")
            .unwrap(),
        "and the edition already there stays where it was"
    );
    assert!(!world.library().join("Bleach/Originale").exists());
}

/// An import that ends up installing nothing — here a file that never finished arriving —
/// has not made the series two editions: the one it holds stays where it is.
#[test]
fn a_commit_that_installs_nothing_does_not_move_the_current_edition() {
    let world = World::new();
    let series = world.with_bleach();
    let request = new_editions(
        &world,
        &series,
        vec![numbered("Deluxe/DB 01.cbz", 1.0)],
        vec![declaration("Deluxe/edition.json", r#"{"name":"Deluxe"}"#)],
        Some("Originale"),
    );
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));
    let opened = bulk
        .open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect("opening");
    let scanner = Scanner::new(Arc::clone(&world.db), true);

    let result =
        leaf_server::api::renames::commit_import(&scanner, &world.db, &bulk, &opened.id, |first| {
            bulk.commit_with(&opened.id, &[], &[], &[], DEFAULT_PROFILE, first)
        })
        .expect("committing");

    assert_eq!(0, result.installed);
    assert!(!world.library().join("Bleach/Originale").exists());
    assert!(world.library().join("Bleach/Tome 1.cbz").exists());
}

/// A folder for the edition that is on the disk already is not a new one: the volumes would
/// be poured into whatever it holds.
#[test]
fn a_new_edition_whose_folder_exists_is_a_name_already_taken() {
    let world = World::new();
    let series = world.with_bleach();
    std::fs::create_dir_all(world.library().join("Bleach/Deluxe")).unwrap();
    let request = new_editions(
        &world,
        &series,
        vec![numbered("Elsewhere/DB 01.cbz", 1.0)],
        vec![declaration(
            "Elsewhere/edition.json",
            r#"{"name":"Deluxe"}"#,
        )],
        Some("Originale"),
    );

    let said = opened_said(&world, &request);

    let problems = said["problems"].as_array().unwrap();
    assert_eq!(1, problems.len(), "{problems:?}");
    assert_eq!("DUPLICATE", problems[0]["kind"]);
}

/// The same, met at the commit: the folder was free at the announcement.
#[test]
fn a_folder_taken_since_the_announcement_refuses_the_commit() {
    let world = World::new();
    let series = world.with_bleach();
    let request = new_editions(
        &world,
        &series,
        vec![numbered("Deluxe/DB 01.cbz", 1.0)],
        vec![declaration("Deluxe/edition.json", r#"{"name":"Deluxe"}"#)],
        Some("Originale"),
    );
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));
    let opened = bulk
        .open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect("opening");
    bulk.receive(
        &opened.id,
        "Deluxe/DB 01.cbz",
        0,
        &common::archive_bytes(None),
        1 << 30,
    )
    .expect("receiving");
    std::fs::create_dir_all(world.library().join("Bleach/Deluxe")).unwrap();
    let scanner = Scanner::new(Arc::clone(&world.db), true);

    let refused =
        leaf_server::api::renames::commit_import(&scanner, &world.db, &bulk, &opened.id, |first| {
            bulk.commit_with(&opened.id, &[], &[], &[], DEFAULT_PROFILE, first)
        });

    assert!(refused.is_err());
    assert!(!world.library().join("Bleach/Originale").exists());
}

/// A work or a universe is not an edition: a series dropped into a series would be a
/// series inside it.
#[test]
fn adding_editions_refuses_a_declaration_of_a_work_or_a_universe() {
    let world = World::new();
    let series = world.with_bleach();
    for bad in ["Deluxe/work.json", "work.json", "Deluxe/universe.json"] {
        let request = new_editions(
            &world,
            &series,
            vec![numbered("Deluxe/DB 01.cbz", 1.0)],
            vec![
                declaration("Deluxe/edition.json", r#"{"name":"Deluxe"}"#),
                declaration(bad, r#"{"title":"Other"}"#),
            ],
            Some("Originale"),
        );
        let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));

        let refused = bulk.open(&request, DEFAULT_PROFILE, &Ceiling::NONE);

        assert!(refused.is_err(), "{bad} is not an edition's");
    }
}

/// A series whose only edition has a name of its own becomes two editions as well: its
/// volumes take the names that say which edition they are.
#[test]
fn a_named_single_edition_is_renamed_when_a_second_arrives() {
    let world = World::new();
    let folder = world.library().join("Bleach/Vieille");
    archive(
        &folder.join("Tome 1.cbz"),
        Some(&EntryJson {
            leaf: Some(1),
            work: Some("Bleach".into()),
            number: Some(1.0),
            ..Default::default()
        }),
    );
    std::fs::write(
        folder.join("edition.json"),
        r#"{"leaf":1,"name":"Originale"}"#,
    )
    .unwrap();
    std::fs::write(
        world.library().join("Bleach/work.json"),
        r#"{"leaf":1,"title":"Bleach"}"#,
    )
    .unwrap();
    world.scan();
    let series = world.one::<String>("SELECT id FROM edition").unwrap();
    let request = new_editions(
        &world,
        &series,
        vec![numbered("Deluxe/DB 01.cbz", 1.0)],
        vec![declaration("Deluxe/edition.json", r#"{"name":"Deluxe"}"#)],
        None,
    );

    committed_new_editions(&world, &request, &["Deluxe/DB 01.cbz"]);

    assert!(world
        .library()
        .join("Bleach/Vieille/bleach_originale_tome_001.cbz")
        .exists());
}

/// A name made of nothing a folder could carry is no name, and two names that make one
/// folder are one edition.
#[test]
fn a_name_that_makes_no_folder_or_the_same_one_is_a_problem() {
    use leaf_server::api::bulk_import::edition_name_problems;
    let long_a = format!("{}A", "x".repeat(300));
    let long_b = format!("{}B", "x".repeat(300));

    let empty = edition_name_problems(&[], &[("A".into(), "...".into())]);
    let same = edition_name_problems(&[], &[("A".into(), long_a), ("B".into(), long_b)]);

    assert_eq!("EMPTY", empty[0].kind);
    assert_eq!(1, same.len(), "{same:?}");
    assert_eq!("DUPLICATE", same[0].kind);
}

/// An album has no editions to add to.
#[test]
fn a_one_shot_is_not_given_editions() {
    let world = World::new();
    let folder = world.library().join("Album");
    archive(
        &folder.join("Album.cbz"),
        Some(&EntryJson {
            leaf: Some(1),
            work: Some("Album".into()),
            ..Default::default()
        }),
    );
    std::fs::write(
        folder.join("work.json"),
        r#"{"leaf":1,"title":"Album","oneShot":true}"#,
    )
    .unwrap();
    world.scan();
    let series = world.one::<String>("SELECT id FROM edition").unwrap();
    if world
        .one::<i64>("SELECT one_shot FROM edition")
        .unwrap_or(0)
        == 0
    {
        world
            .db
            .write(|cx| {
                cx.execute("UPDATE edition SET one_shot = 1", [])?;
                Ok(())
            })
            .unwrap();
    }
    let request = new_editions(
        &world,
        &series,
        vec![numbered("Deluxe/DB 01.cbz", 1.0)],
        vec![declaration("Deluxe/edition.json", r#"{"name":"Deluxe"}"#)],
        None,
    );
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));

    let refused = bulk.open(&request, DEFAULT_PROFILE, &Ceiling::NONE);

    assert!(refused.is_err());
}

/// What adding an edition does to the volumes already held is said before anything moves:
/// how many, and into which edition.
#[test]
fn announcing_a_second_edition_says_how_many_volumes_it_reshelves() {
    let world = World::new();
    let series = world.with_bleach();
    let request = new_editions(
        &world,
        &series,
        vec![numbered("Deluxe/DB 01.cbz", 1.0)],
        vec![declaration("Deluxe/edition.json", r#"{"name":"Deluxe"}"#)],
        Some("Originale"),
    );

    let said = opened_said(&world, &request);

    let reshelved = said["reshelved"].as_array().unwrap();
    assert_eq!(1, reshelved.len(), "{reshelved:?}");
    assert_eq!(2, reshelved[0]["count"]);
    assert_eq!("Bleach › Originale", reshelved[0]["edition"]);
}

/// Nothing is reshelved when the series has already got editions of its own.
#[test]
fn a_series_that_already_has_editions_reshelves_nothing() {
    let world = World::new();
    let folder = world.library().join("Bleach");
    for edition in ["Vieille", "Autre"] {
        archive(
            &folder.join(edition).join("Tome 1.cbz"),
            Some(&EntryJson {
                leaf: Some(1),
                work: Some("Bleach".into()),
                number: Some(1.0),
                ..Default::default()
            }),
        );
        std::fs::write(
            folder.join(edition).join("edition.json"),
            format!(r#"{{"leaf":1,"name":"{edition}"}}"#),
        )
        .unwrap();
    }
    std::fs::write(folder.join("work.json"), r#"{"leaf":1,"title":"Bleach"}"#).unwrap();
    world.scan();
    let series = world
        .one::<String>("SELECT id FROM edition ORDER BY name LIMIT 1")
        .unwrap();
    let request = new_editions(
        &world,
        &series,
        vec![numbered("Deluxe/DB 01.cbz", 1.0)],
        vec![declaration("Deluxe/edition.json", r#"{"name":"Deluxe"}"#)],
        None,
    );

    let said = opened_said(&world, &request);

    assert!(said["reshelved"].as_array().unwrap().is_empty());
}

#[test]
fn an_import_cancelled_before_commit_leaves_no_trace() {
    let world = World::new();
    let series = world.with_bleach();
    let request = new_editions(
        &world,
        &series,
        vec![numbered("Deluxe/DB 01.cbz", 1.0)],
        vec![declaration("Deluxe/edition.json", r#"{"name":"Deluxe"}"#)],
        Some("Originale"),
    );
    let bulk = BulkImport::new(&world.inbox(), &world.library(), Arc::clone(&world.db));
    let opened = bulk
        .open(&request, DEFAULT_PROFILE, &Ceiling::NONE)
        .expect("opening");

    bulk.abandon(&opened.id).expect("abandoning");

    assert!(world.library().join("Bleach/Tome 1.cbz").exists());
    assert!(!world.library().join("Bleach/Originale").exists());
    assert!(!world.library().join("Bleach/Deluxe").exists());
    assert_eq!(
        0,
        world
            .one::<i64>("SELECT COUNT(*) FROM rename_plan")
            .unwrap()
    );
}
