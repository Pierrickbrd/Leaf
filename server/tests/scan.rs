//! Reading a library off the disk.
//!
//! Built on real archives rather than on rows inserted by hand: the scanner's whole job is
//! to turn files into rows, and a fixture that starts from rows would test nothing.

use std::io::Write;
use std::path::Path;
use std::sync::Arc;

mod common;
use common::writable;

use leaf_server::scan::scanner::Scanner;
use leaf_server::store::repository::Ceiling;
use leaf_server::store::Db;

fn jpeg(width: u32, height: u32) -> Vec<u8> {
    let mut buffer = image::RgbImage::new(width, height);
    for (x, y, pixel) in buffer.enumerate_pixels_mut() {
        *pixel = image::Rgb([(x % 256) as u8, (y % 256) as u8, 128]);
    }
    let mut out = std::io::Cursor::new(Vec::new());
    image::DynamicImage::ImageRgb8(buffer)
        .write_to(&mut out, image::ImageFormat::Jpeg)
        .unwrap();
    out.into_inner()
}

/// A CBZ with `pages` images and whatever sidecar is handed in.
fn archive(path: &Path, pages: usize, sidecar: Option<(&str, &str)>) {
    std::fs::create_dir_all(path.parent().unwrap()).unwrap();
    let file = std::fs::File::create(path).unwrap();
    let mut zip = zip::ZipWriter::new(file);
    let options = zip::write::SimpleFileOptions::default();
    for i in 0..pages {
        zip.start_file::<_, ()>(format!("{i:03}.jpg"), options)
            .unwrap();
        zip.write_all(&jpeg(100, 140)).unwrap();
    }
    if let Some((name, body)) = sidecar {
        zip.start_file::<_, ()>(name, options).unwrap();
        zip.write_all(body.as_bytes()).unwrap();
    }
    zip.finish().unwrap();
}

/// An archive holding several sidecars at once.
fn archive_with(path: &Path, sidecars: &[(&str, &str)]) {
    std::fs::create_dir_all(path.parent().unwrap()).unwrap();
    let mut zip = zip::ZipWriter::new(std::fs::File::create(path).unwrap());
    let options = zip::write::SimpleFileOptions::default();
    for i in 0..2 {
        zip.start_file::<_, ()>(format!("{i:03}.jpg"), options)
            .unwrap();
        zip.write_all(&jpeg(100, 140)).unwrap();
    }
    for (name, body) in sidecars {
        zip.start_file::<_, ()>(*name, options).unwrap();
        zip.write_all(body.as_bytes()).unwrap();
    }
    zip.finish().unwrap();
}

struct Library {
    dir: tempfile::TempDir,
    db: Arc<Db>,
}

impl Library {
    fn new() -> Self {
        let dir = tempfile::tempdir().unwrap();
        std::fs::create_dir_all(dir.path().join("library")).unwrap();
        let db = Db::open(&dir.path().join("index.sqlite")).unwrap();
        Library {
            dir,
            db: Arc::new(db),
        }
    }

    fn folder(&self, path: &str) -> std::path::PathBuf {
        let full = self.dir.path().join("library").join(path);
        std::fs::create_dir_all(&full).unwrap();
        full
    }

    fn write(&self, path: &str, body: &str) {
        let full = self.dir.path().join("library").join(path);
        std::fs::create_dir_all(full.parent().unwrap()).unwrap();
        std::fs::write(full, body).unwrap();
    }

    fn scan(&self) -> leaf_server::scan::report::ScanReport {
        Scanner::new(Arc::clone(&self.db), true)
            .scan(&[self.dir.path().join("library")])
            .expect("scanning")
    }

    fn count(&self, table: &str) -> i64 {
        self.db
            .read(|cx| {
                Ok(cx
                    .query_one(&format!("SELECT COUNT(*) FROM {table}"), [], |r| r.get(0))?
                    .unwrap_or(0))
            })
            .unwrap()
    }

    /// The first column of the first row, which may itself be null — so the answer is
    /// "no row" and "a null" collapsed into one, which is all these tests need.
    fn one<T: rusqlite::types::FromSql>(&self, sql: &str) -> Option<T> {
        self.db
            .read(|cx| cx.query_one(sql, [], |r| r.get::<_, Option<T>>(0)))
            .unwrap()
            .flatten()
    }

    fn all(&self, sql: &str) -> Vec<String> {
        self.db
            .read(|cx| cx.query(sql, [], |r| r.get::<_, String>(0)))
            .unwrap()
    }

    /// A reader stopped here, on every entry there is. Written into the table rather than
    /// through the route, because what is under test is what a **scan** does to it.
    fn stopped_at(&self, page: i64) {
        self.db
            .write(|cx| {
                cx.execute(
                    "INSERT INTO progress (entry_id, edition_id, page, finished, updated_at)
                     SELECT id, edition_id, ?1, 0, 1 FROM entry",
                    [page],
                )?;
                Ok(())
            })
            .unwrap();
    }

    /// Where the reader stopped, as the index holds it now.
    fn place(&self) -> Option<i64> {
        self.one::<i64>("SELECT page FROM progress")
    }

    /// The index as it was before an identity stopped being a path: every work, edition and
    /// entry filed under the hash of its own path, and the reading positions pointing at
    /// that.
    ///
    /// The fixture for the migration, and the only way to build one — the scanner cannot
    /// write the old shape any more. All three levels at once, because a real first scan on
    /// an unmigrated library re-keys the work, its editions and their entries together, in
    /// one transaction — not the entries alone. A fixture that only re-keyed entries left
    /// `progress.edition_id` pointing at the edition's *current* id, which a scan of the
    /// real shape does not: `reattach` carrying only `entry_id` and dropping `edition_id`
    /// left every assertion here green regardless, because the edition row the position
    /// pointed at was never the one the prune removed. Foreign keys deferred to the commit,
    /// because a parent is being re-keyed here before its children have caught up.
    fn as_it_was_before(&self) {
        self.db
            .write(|cx| {
                cx.run("PRAGMA defer_foreign_keys = ON")?;

                let works = cx.query("SELECT id, path FROM work", [], |r| {
                    Ok((r.get::<_, String>(0)?, r.get::<_, String>(1)?))
                })?;
                for (id, path) in works {
                    let was = leaf_server::scan::scanner::id_of(Path::new(&path), "");
                    for sql in [
                        "UPDATE work SET id = ?1 WHERE id = ?2",
                        "UPDATE edition SET work_id = ?1 WHERE work_id = ?2",
                        "UPDATE reading_order_step SET work_id = ?1 WHERE work_id = ?2",
                    ] {
                        cx.execute(sql, rusqlite::params![was, id])?;
                    }
                }

                // An implicit edition's path is its work's own folder, so the same formula
                // that names a folder-backed edition's old identity names this one too.
                let editions = cx.query("SELECT id, path FROM edition", [], |r| {
                    Ok((r.get::<_, String>(0)?, r.get::<_, String>(1)?))
                })?;
                for (id, path) in editions {
                    let was = leaf_server::scan::scanner::id_of(Path::new(&path), "edition");
                    for sql in [
                        "UPDATE edition SET id = ?1 WHERE id = ?2",
                        "UPDATE entry SET edition_id = ?1 WHERE edition_id = ?2",
                        "UPDATE arc SET edition_id = ?1 WHERE edition_id = ?2",
                        "UPDATE chapter SET edition_id = ?1 WHERE edition_id = ?2",
                        "UPDATE progress SET edition_id = ?1 WHERE edition_id = ?2",
                        "UPDATE reading_order_step SET edition_id = ?1 WHERE edition_id = ?2",
                    ] {
                        cx.execute(sql, rusqlite::params![was, id])?;
                    }
                }

                let entries = cx.query("SELECT id, file FROM entry", [], |r| {
                    Ok((r.get::<_, String>(0)?, r.get::<_, String>(1)?))
                })?;
                for (id, file) in entries {
                    let was = leaf_server::scan::scanner::id_of(Path::new(&file), "");
                    for sql in [
                        "UPDATE entry SET id = ?1 WHERE id = ?2",
                        "UPDATE progress SET entry_id = ?1 WHERE entry_id = ?2",
                        "UPDATE page SET entry_id = ?1 WHERE entry_id = ?2",
                        "UPDATE chapter SET entry_id = ?1 WHERE entry_id = ?2",
                    ] {
                        cx.execute(sql, rusqlite::params![was, id])?;
                    }
                }
                Ok(())
            })
            .unwrap();
    }
}

// ------------------------------------------------------------------ levels

#[test]
fn a_folder_of_archives_is_a_work_with_an_implicit_edition() {
    let library = Library::new();
    let bleach = library.folder("Bleach");
    archive(&bleach.join("Tome 1.cbz"), 3, None);
    archive(&bleach.join("Tome 2.cbz"), 3, None);

    let report = library.scan();

    assert_eq!(1, library.count("work"));
    assert_eq!(1, library.count("edition"));
    assert_eq!(2, library.count("entry"));
    assert_eq!(6, library.count("page"));
    // No sub-folder, no picker, nothing to declare: the edition has no name of its own.
    assert_eq!(None, library.one::<String>("SELECT name FROM edition"));
    assert_eq!(
        1,
        library.one::<i64>("SELECT implicit FROM edition").unwrap()
    );
    assert_eq!(2, report.entries);
}

/// The shape `oneshot.json` exists for, and the one no count could tell apart: a book
/// standing alone, beside another book standing alone.
///
/// Without the declaration both files are archives sitting in `BD/`, which makes `BD/` a
/// work and the two albums its volumes — a two-volume series named after a folder somebody
/// made to tidy up. Nothing on the disk says otherwise, which is exactly why somebody has
/// to say it.
#[test]
fn an_archive_that_declares_itself_a_whole_book_is_lifted_out_of_the_folder_it_sits_in() {
    let library = Library::new();
    let shelf = library.folder("BD");
    archive(
        &shelf.join("Le Combat Ordinaire.cbz"),
        3,
        Some((
            "oneshot.json",
            r#"{"leaf":1,"title":"Le Combat Ordinaire","authors":["Manu Larcenet"],
                "publisher":"Dargaud","medium":"bd"}"#,
        )),
    );
    archive(
        &shelf.join("Blast.cbz"),
        4,
        Some((
            "oneshot.json",
            r#"{"leaf":1,"title":"Blast","authors":["Manu Larcenet"]}"#,
        )),
    );

    library.scan();

    // Two works, two editions, two entries — and not one work holding two volumes.
    assert_eq!(2, library.count("work"));
    assert_eq!(2, library.count("edition"));
    assert_eq!(2, library.count("entry"));
    assert_eq!(
        2,
        library
            .one::<i64>("SELECT COUNT(*) FROM edition WHERE one_shot = 1")
            .unwrap()
    );
    // Each holds exactly one file, which is what lets the shelf offer it to the reader.
    assert_eq!(
        1,
        library
            .one::<i64>(
                "SELECT MAX((SELECT COUNT(*) FROM entry x WHERE x.edition_id = e.id))
                 FROM edition e"
            )
            .unwrap()
    );
    // The title comes from the file, not from the folder it was dropped in.
    let mut names = library.all("SELECT name FROM work ORDER BY name");
    names.sort();
    assert_eq!(vec!["Blast", "Le Combat Ordinaire"], names);
    // And the folder is not a series: nothing is recorded under its name.
    assert!(!names.contains(&"BD".to_string()));
}

/// The shelf is unmade, and so is what the scan had already said about it. It was recorded
/// as a work before its archives could speak, and its implicit edition was checked against a
/// `work.json` nobody was ever going to write: « Death Note - One-Shot/work.json: status,
/// volumeCount » was reported for a folder that is not a work.
#[test]
fn a_shelf_of_whole_books_is_not_reported_as_a_work_missing_its_sidecar() {
    let library = Library::new();
    let shelf = library.folder("BD");
    archive(
        &shelf.join("Blast.cbz"),
        4,
        Some((
            "oneshot.json",
            r#"{"leaf":1,"title":"Blast","authors":["Manu Larcenet"]}"#,
        )),
    );

    let report = library.scan();

    assert!(
        !report
            .missing_required
            .iter()
            .any(|line| line.starts_with("BD")),
        "{:?}",
        report.missing_required
    );
}

/// An album inside a series it belongs to stays a volume of it. The declaration is what
/// lifts a file out, so an artbook you want filed with the work is filed with it by saying
/// nothing.
#[test]
fn a_volume_that_declares_nothing_stays_where_it_sits() {
    let library = Library::new();
    let naruto = library.folder("Naruto");
    archive(&naruto.join("Tome 1.cbz"), 2, None);
    archive(
        &naruto.join("Artbook.cbz"),
        2,
        Some(("oneshot.json", r#"{"leaf":1,"title":"Naruto Artbook"}"#)),
    );

    library.scan();

    // Two works: Naruto with its volume, and the artbook on its own.
    assert_eq!(2, library.count("work"));
    assert_eq!(
        1,
        library
            .one::<i64>("SELECT COUNT(*) FROM edition WHERE one_shot = 1")
            .unwrap()
    );
    assert_eq!(
        1,
        library
            .one::<i64>(
                "SELECT COUNT(*) FROM entry WHERE edition_id IN
                 (SELECT id FROM edition WHERE one_shot = 0)"
            )
            .unwrap()
    );
}

const BLAST: &str = r#"{"leaf":1,"title":"Blast","authors":["Manu Larcenet"]}"#;

/// A book of a universe that is no series of it had to be put in a folder of its own, made
/// for nothing but holding it: dropped beside the universe's works, it was not read at all,
/// and nothing said so.
#[test]
fn a_whole_book_sitting_directly_in_a_universe_folder_is_a_book_of_that_universe() {
    let library = Library::new();
    library.write("Larcenet/universe.json", r#"{"leaf":1,"name":"Larcenet"}"#);
    let universe = library.folder("Larcenet");
    archive(
        &universe.join("Blast.cbz"),
        3,
        Some(("oneshot.json", BLAST)),
    );

    let first = library.scan();

    assert_eq!(1, library.count("work"));
    assert_eq!(1, library.count("entry"));
    assert_eq!(
        Some(1),
        library.one::<i64>("SELECT one_shot FROM edition"),
        "its own edition, as a book lifted out of a work folder has"
    );
    assert_eq!(
        library.one::<String>("SELECT id FROM universe"),
        library.one::<String>("SELECT universe_id FROM work"),
        "the book belongs to the universe it sits in"
    );
    assert_eq!(
        Some("Blast".to_string()),
        library.one::<String>("SELECT title FROM work")
    );
    assert!(first.disregarded.is_empty(), "{:?}", first.disregarded);

    // And a second scan finds it where the first left it, reading position and all.
    let ids = library.all("SELECT id FROM entry");
    library.stopped_at(2);
    library.scan();
    assert_eq!(ids, library.all("SELECT id FROM entry"));
    assert_eq!(1, library.count("work"));
    assert_eq!(1, library.count("edition"));
    assert_eq!(Some(2), library.place());
}

/// A volume dropped in a universe belongs to no work, and guessing one would be inventing a
/// series. It is still not read — but it used to be skipped in silence, the archive on the
/// disk and nowhere in the library.
#[test]
fn an_archive_in_a_universe_folder_that_is_not_a_whole_book_is_reported_not_read() {
    let library = Library::new();
    library.write("Larcenet/universe.json", r#"{"leaf":1,"name":"Larcenet"}"#);
    let universe = library.folder("Larcenet");
    archive(&universe.join("Loose.cbz"), 2, None);
    archive(&universe.join("Retour à la terre/Tome 1.cbz"), 2, None);

    let report = library.scan();

    assert_eq!(1, library.count("entry"), "only the volume of the work");
    assert!(
        report
            .disregarded
            .iter()
            .any(|d| d.contains("Larcenet/Loose.cbz") && d.contains("oneshot.json")),
        "{:?}",
        report.disregarded
    );
    // Reported once, as what it is, and not also as a volume that lacks its metadata.
    assert!(
        !report
            .entries_without_metadata
            .contains(&"Loose.cbz".to_string()),
        "{:?}",
        report.entries_without_metadata
    );
}

#[test]
fn a_whole_book_moved_out_of_its_universe_folder_leaves_the_universe() {
    let library = Library::new();
    library.write("Larcenet/universe.json", r#"{"leaf":1,"name":"Larcenet"}"#);
    let universe = library.folder("Larcenet");
    archive(
        &universe.join("Blast.cbz"),
        3,
        Some(("oneshot.json", BLAST)),
    );
    library.scan();

    let shelf = library.folder("BD");
    std::fs::rename(universe.join("Blast.cbz"), shelf.join("Blast.cbz")).unwrap();
    library.scan();

    assert_eq!(1, library.count("work"));
    assert_eq!(1, library.count("edition"));
    assert_eq!(1, library.count("entry"));
    assert_eq!(None, library.one::<String>("SELECT universe_id FROM work"));
}

#[test]
fn a_book_in_a_universe_folder_that_stops_declaring_itself_whole_is_no_longer_filed() {
    let library = Library::new();
    library.write("Larcenet/universe.json", r#"{"leaf":1,"name":"Larcenet"}"#);
    let universe = library.folder("Larcenet");
    archive(
        &universe.join("Blast.cbz"),
        3,
        Some(("oneshot.json", BLAST)),
    );
    archive(&universe.join("Retour à la terre/Tome 1.cbz"), 2, None);
    library.scan();
    assert_eq!(2, library.count("work"));

    archive(&universe.join("Blast.cbz"), 3, None);
    let report = library.scan();

    assert_eq!(1, library.count("work"));
    assert_eq!(1, library.count("edition"));
    assert_eq!(1, library.count("entry"));
    assert!(
        report
            .disregarded
            .iter()
            .any(|d| d.contains("Larcenet/Blast.cbz")),
        "{:?}",
        report.disregarded
    );
}

#[test]
fn a_folder_of_folders_is_a_universe_unless_it_says_otherwise() {
    let library = Library::new();
    // The shape that no heuristic can read: a universe of two works, or one work in two
    // editions, and nothing on the disk says which.
    let arran = library.folder("Terres d'Arran");
    archive(&arran.join("Elfes/Tome 1.cbz"), 2, None);
    archive(&arran.join("Nains/Tome 1.cbz"), 2, None);

    library.scan();

    // So it is neither. The folder declares nothing, so it is a shelf: walked through, and
    // Elfes and Nains are judged on their own — two works, each holding volumes, which is
    // the only thing anything here actually says.
    assert_eq!(0, library.count("universe"));
    assert_eq!(2, library.count("work"));
    assert_eq!(
        vec![None::<String>],
        library
            .db
            .read(
                |cx| cx.query("SELECT DISTINCT universe_id FROM work", [], |r| r
                    .get::<_, Option<String>>(0))
            )
            .unwrap()
    );

    // Declared a work, it becomes one — with two editions.
    library.write(
        "Terres d'Arran/work.json",
        r#"{"leaf":1,"title":"Terres d'Arran"}"#,
    );
    library.scan();
    assert_eq!(0, library.count("universe"));
    assert_eq!(1, library.count("work"));
    assert_eq!(2, library.count("edition"));
}

// ----------------------------------------------------------------- chapters

#[test]
fn chapters_are_read_from_the_sidecar_and_numbered_on_one_scale() {
    let library = Library::new();
    let folder = library.folder("Haikyu");
    library.write(
        "Haikyu/work.json",
        r#"{"leaf":1,"title":"Haikyū","chapterLabel":"Chap.{n:000}"}"#,
    );
    archive(
        &folder.join("Tome 1.cbz"),
        4,
        Some((
            "entry.json",
            r#"{"leaf":1,"work":"Haikyū","type":"VOLUME","number":1,
                "chapters":[{"raw":"1","title":"Fin et commencement","startPage":0},
                            {"raw":"2","title":"Le roi","startPage":50},
                            {"raw":"Bonus","title":"Note de l'auteur","after":2}]}"#,
        )),
    );

    library.scan();

    assert_eq!(3, library.count("chapter"));
    // The pattern composes what the file did not spell out.
    assert_eq!(
        vec![
            "Chap.001".to_string(),
            "Chap.002".to_string(),
            "Bonus".to_string()
        ],
        library.all("SELECT label FROM chapter ORDER BY position")
    );
    // A bonus with no number of its own is not adrift: it sits after what preceded it.
    assert_eq!(
        vec![
            "CHAPTER".to_string(),
            "CHAPTER".to_string(),
            "BONUS".to_string()
        ],
        library.all("SELECT kind FROM chapter ORDER BY position")
    );
    assert_eq!(
        None,
        library.one::<f64>("SELECT number FROM chapter WHERE kind = 'BONUS'")
    );
}

#[test]
fn a_chapter_that_arrived_on_its_own_says_which_volume_it_came_from() {
    let library = Library::new();
    let folder = library.folder("One Piece");
    library.write(
        "One Piece/work.json",
        r#"{"leaf":1,"title":"One Piece","volumeCount":3}"#,
    );
    archive(
        &folder.join("Tome 1.cbz"),
        2,
        Some(("entry.json", r#"{"number":1}"#)),
    );
    archive(
        &folder.join("Tome 3.cbz"),
        2,
        Some(("entry.json", r#"{"number":3}"#)),
    );
    // Volume 2 is not on disk — but its chapters are, and they say so.
    archive(
        &folder.join("Chapitre 12.cbz"),
        2,
        Some((
            "entry.json",
            r#"{"type":"CHAPTER","number":12,"volume":2,"chapters":[{"raw":"12","volume":2}]}"#,
        )),
    );

    library.scan();

    assert_eq!(
        Some(2.0),
        library.one::<f64>("SELECT volume FROM chapter WHERE number = 12")
    );
    // And the series does not report volume 2 as missing: it does not have the file, but it
    // has the content, and the content is what you would be missing.
    let series = leaf_server::store::Repository::new(&library.db)
        .series(
            &leaf_server::api::dto::SeriesFilter::default(),
            leaf_server::api::dto::SeriesSort::Name,
            0,
            0,
            "default",
            &Ceiling::NONE,
        )
        .unwrap();
    assert!(
        series[0].missing_volumes.is_empty(),
        "volume 2 arrived as chapters: {:?}",
        series[0].missing_volumes
    );
}

#[test]
fn a_number_claimed_twice_is_reported_and_only_the_first_keeps_it() {
    let library = Library::new();
    let folder = library.folder("Doublons");
    archive(
        &folder.join("Tome 1.cbz"),
        2,
        Some((
            "entry.json",
            r#"{"number":1,"chapters":[{"raw":"5","title":"Premier"},{"raw":"5","title":"Second"}]}"#,
        )),
    );

    let report = library.scan();

    assert_eq!(
        1,
        report.duplicate_numbers.len(),
        "{:?}",
        report.duplicate_numbers
    );
    // A number is unique within an edition: the second loses it rather than colliding.
    let numbered: i64 = library
        .one("SELECT COUNT(*) FROM chapter WHERE number IS NOT NULL")
        .unwrap();
    assert_eq!(1, numbered);
    assert_eq!(2, library.count("chapter"));
}

// ---------------------------------------------------------------- the index

#[test]
fn what_the_scan_did_not_meet_is_removed_and_stops_being_findable() {
    let library = Library::new();
    let folder = library.folder("Bleach");
    archive(&folder.join("Tome 1.cbz"), 2, None);
    archive(&folder.join("Tome 2.cbz"), 2, None);
    library.scan();
    assert_eq!(2, library.count("entry"));

    std::fs::remove_file(folder.join("Tome 2.cbz")).unwrap();
    library.scan();

    assert_eq!(1, library.count("entry"));
    assert_eq!(2, library.count("page"));
    // The search index goes with it. Pruned before the tables were cleaned, it found no
    // orphan — and a deleted series stayed findable until the scan after the one that
    // removed it.
    let orphans: i64 = library
        .one(
            "SELECT COUNT(*) FROM search_ref
             WHERE kind = 'ENTRY' AND ref NOT IN (SELECT id FROM entry)",
        )
        .unwrap();
    assert_eq!(0, orphans);
    assert_eq!(library.count("search"), library.count("search_ref"));
}

#[test]
fn a_whole_library_removed_leaves_nothing_behind() {
    let library = Library::new();
    let folder = library.folder("Bleach");
    archive(&folder.join("Tome 1.cbz"), 2, None);
    library.scan();

    std::fs::remove_dir_all(&folder).unwrap();
    library.scan();

    for table in [
        "universe", "work", "edition", "entry", "page", "chapter", "search",
    ] {
        assert_eq!(0, library.count(table), "{table} still holds rows");
    }
}

// ------------------------------------------------------------- incremental

#[test]
fn an_unchanged_library_is_not_read_again() {
    let library = Library::new();
    let folder = library.folder("Bleach");
    for volume in 1..=3 {
        archive(&folder.join(format!("Tome {volume}.cbz")), 4, None);
    }

    let first = library.scan();
    assert_eq!(3, first.reanalysed);

    let before = library.db.statements();
    let second = library.scan();
    let cost = library.db.statements() - before;

    // Size and modification time decide. Not one archive is opened.
    assert_eq!(0, second.reanalysed);
    // And the chapters are not rewritten either: when no entry has moved and none has been
    // added or removed, the computation lands on exactly what is already stored.
    assert_eq!(
        3, second.entries,
        "the report describes the library, not the work done"
    );
    assert!(cost < 200, "an unchanged rescan took {cost} statements");
}

#[test]
fn a_touched_file_is_read_again_and_nothing_else_is() {
    let library = Library::new();
    let folder = library.folder("Bleach");
    for volume in 1..=3 {
        archive(&folder.join(format!("Tome {volume}.cbz")), 4, None);
    }
    library.scan();

    // Rewritten with a different number of pages, so the change is visible in the rows.
    archive(&folder.join("Tome 2.cbz"), 7, None);
    let report = library.scan();

    assert_eq!(1, report.reanalysed, "only the file that moved");
    assert_eq!(4 + 7 + 4, library.count("page"));
}

// ------------------------------------------------------------------ covers

#[test]
fn a_cover_dropped_beside_a_volume_is_picked_up() {
    let library = Library::new();
    let folder = library.folder("Bleach");
    archive(&folder.join("Tome 1.cbz"), 2, None);
    std::fs::write(folder.join("Tome 1.jpg"), jpeg(60, 90)).unwrap();
    std::fs::write(folder.join("cover.png"), jpeg(60, 90)).unwrap();

    library.scan();

    // Page zero is right most of the time and wrong often enough to matter, and overriding
    // it must not mean editing the archive.
    assert!(library
        .one::<String>("SELECT cover_file FROM entry")
        .unwrap()
        .ends_with("Tome 1.jpg"));
    // And one named for the folder speaks for the whole series.
    assert!(library
        .one::<String>("SELECT cover_file FROM edition")
        .unwrap()
        .ends_with("cover.png"));
}

/// A panic in the scan thread must not make the server unable to scan ever again.
#[test]
fn a_scan_that_panics_gives_the_runner_back() {
    use std::sync::Arc;
    let runner = Arc::new(leaf_server::scan::runner::ScanRunner::default());

    assert!(runner.start("boom", || panic!("something in the scanner")));
    for _ in 0..200 {
        if runner.status().state != "RUNNING" {
            break;
        }
        std::thread::sleep(std::time::Duration::from_millis(10));
    }

    assert!(
        runner.start("after", || Ok(Default::default())),
        "the runner is stuck: nothing can ever scan again"
    );
}

/// One shelf that cannot be read must not cost the others, nor cost anyone their index.
#[test]
#[cfg(unix)]
fn an_unreadable_shelf_is_reported_and_the_rest_still_scans() {
    use std::os::unix::fs::PermissionsExt;

    let library = Library::new();
    let bleach = library.folder("Bleach");
    archive(&bleach.join("Tome 1.cbz"), 3, None);
    let naruto = library.folder("Naruto");
    archive(&naruto.join("Tome 1.cbz"), 3, None);
    library.scan();
    assert_eq!(2, library.count("work"));

    // Naruto becomes unreadable — a permission that changed, a mount that went away.
    std::fs::set_permissions(&naruto, std::fs::Permissions::from_mode(0o000)).unwrap();
    let report = library.scan();
    writable(&naruto);

    // Bleach was still read...
    assert!(library
        .all("SELECT name FROM work")
        .contains(&"Bleach".to_string()));
    // ...and Naruto was not quietly deleted from the index because a folder was shut for a
    // moment. Progress is the one thing a rescan cannot bring back.
    assert_eq!(2, library.count("work"), "{}", report.summary());
}

/// Every shape a folder can take, and what the scanner makes of it.
///
/// A census rather than an assertion about one case: the classification is three rules deep
/// and the interesting part is where they meet. Printed as well as checked, so the table in
/// the documentation is read off a run rather than written from memory.
#[test]
fn what_the_scanner_makes_of_every_shape() {
    let library = Library::new();

    // 1. Archives sitting right here, nothing declared.
    archive(&library.folder("Bleach").join("Tome 1.cbz"), 2, None);

    // 2. A work that says so, with its archives directly.
    let naruto = library.folder("Naruto");
    archive(&naruto.join("Tome 1.cbz"), 2, None);
    library.write("Naruto/work.json", r#"{"leaf":1,"title":"Naruto"}"#);

    // 3. A work that says so, holding edition folders.
    let dn = library.folder("Death Note");
    library.write("Death Note/work.json", r#"{"leaf":1,"title":"Death Note"}"#);
    archive(&dn.join("Originale/Tome 1.cbz"), 2, None);
    archive(&dn.join("Black Edition/Tome 1.cbz"), 2, None);

    // 4. Sub-folders holding archives, and nothing saying what this is.
    let shelf = library.folder("Rayonnage");
    archive(&shelf.join("Berserk/Tome 1.cbz"), 2, None);
    archive(&shelf.join("Vagabond/Tome 1.cbz"), 2, None);

    // 5. A universe that says so.
    let arran = library.folder("Terres d'Arran");
    library.write(
        "Terres d'Arran/universe.json",
        r#"{"leaf":1,"name":"Terres d'Arran"}"#,
    );
    archive(&arran.join("Nains/Tome 1.cbz"), 2, None);
    archive(&arran.join("Elfes/Tome 1.cbz"), 2, None);

    // 6. An edition file at the top, where nothing is above it to be its work.
    let solo = library.folder("Perfect Edition");
    library.write(
        "Perfect Edition/edition.json",
        r#"{"leaf":1,"name":"Perfect"}"#,
    );
    archive(&solo.join("Tome 1.cbz"), 2, None);

    // 7. A universe below another. Reachable only because a sibling brings archives within
    //    two folders of the top — on its own, case 8 swallows it.
    let nested = library.folder("Bibliotheque");
    archive(&nested.join("Akira/Tome 1.cbz"), 2, None);
    library.write(
        "Bibliotheque/Marvel/universe.json",
        r#"{"leaf":1,"name":"Marvel"}"#,
    );
    archive(&nested.join("Marvel/Spider-Man/Tome 1.cbz"), 2, None);

    // 8. The full depth of the model: universe, work, edition, files.
    let mangas = library.folder("Mangas");
    library.write("Mangas/universe.json", r#"{"leaf":1,"name":"Mangas"}"#);
    library.write(
        "Mangas/Dragon Ball/work.json",
        r#"{"leaf":1,"title":"Dragon Ball"}"#,
    );
    library.write(
        "Mangas/Dragon Ball/Perfect Edition/edition.json",
        r#"{"leaf":1,"name":"Perfect Edition"}"#,
    );
    archive(
        &mangas.join("Dragon Ball/Perfect Edition/Tome 1.cbz"),
        2,
        None,
    );

    // 8b. Shelves stacked on shelves. None of them declares anything, so none of them is a
    //     level: the walk goes through all three and finds the work at the bottom.
    let deep = library.folder("Trop profond");
    archive(&deep.join("a/b/Aria/Tome 1.cbz"), 2, None);

    // 9. Nothing at all.
    library.folder("Vide");

    let report = library.scan();

    let universes = library.all("SELECT name FROM universe ORDER BY name");
    let works = library.all(
        "SELECT COALESCE(u.name, '—') || ' / ' || w.name FROM work w
         LEFT JOIN universe u ON u.id = w.universe_id ORDER BY 1",
    );
    let editions = library.all(
        "SELECT w.name || ' / ' || COALESCE(e.name, '(implicit)') FROM edition e
         JOIN work w ON w.id = e.work_id ORDER BY 1",
    );

    println!("\nuniverses:");
    for u in &universes {
        println!("   {u}");
    }
    println!("works  (universe / work):");
    for w in &works {
        println!("   {w}");
    }
    println!("editions  (work / edition):");
    for e in &editions {
        println!("   {e}");
    }
    println!("disregarded: {:?}", report.disregarded);

    // A universe is a folder that says it is one. Nothing else is.
    assert_eq!(
        vec![
            "Mangas".to_string(),
            "Marvel".into(),
            "Terres d'Arran".into()
        ],
        universes
    );

    // `Bibliotheque` and `Rayonnage` declare nothing, so they are shelves: walked through,
    // and what is inside is judged on its own terms. Marvel says it is a universe and is
    // one — it is not flattened into a work of the folder that happened to hold it.
    assert!(
        works.contains(&"Marvel / Spider-Man".to_string()),
        "{works:?}"
    );
    assert!(works.contains(&"— / Akira".to_string()), "{works:?}");
    assert!(works.contains(&"— / Berserk".to_string()), "{works:?}");

    // A universe.json below a shelf is honoured — the shelf was never a level, so nothing
    // is nested and nothing is disregarded. What is still disregarded is the edition name on
    // a folder whose archives sit beside it, because an implicit edition has no name.
    assert!(
        report
            .disregarded
            .iter()
            .any(|d| d.contains("Perfect Edition")),
        "{:?}",
        report.disregarded
    );

    // Out of reach: nothing shallower than three folders down is ever met.
    assert!(
        !works.iter().any(|w| w.contains("Trop profond")),
        "{works:?}"
    );
    assert!(!works.iter().any(|w| w.contains("Vide")));
}

/// What lands in the index, for each shape of thing you can import.
///
/// A matrix rather than a case: the import moves files to paths and the scanner decides
/// what they are, so "what happens when I import X" is only answerable by putting X on the
/// disk and looking.
#[test]
fn what_happens_for_each_shape_of_import() {
    let library = Library::new();

    // What is already there: a work with its volumes sitting directly in it, and a universe.
    archive(&library.folder("Bleach").join("Tome 1.cbz"), 2, None);
    library.write("Bleach/work.json", r#"{"leaf":1,"title":"Bleach"}"#);
    library.write(
        "Terres d'Arran/universe.json",
        r#"{"leaf":1,"name":"Terres d'Arran"}"#,
    );
    archive(
        &library.folder("Terres d'Arran/Nains").join("Tome 1.cbz"),
        2,
        None,
    );

    // 1. Volumes added to an existing work.
    archive(&library.folder("Bleach").join("Tome 2.cbz"), 2, None);

    // 2. A NEW EDITION as a sub-folder of a work whose volumes sit directly in it.
    archive(
        &library.folder("Bleach/Perfect Edition").join("Tome 1.cbz"),
        2,
        None,
    );

    // 3. A new work into an existing universe.
    archive(
        &library.folder("Terres d'Arran/Elfes").join("Tome 1.cbz"),
        2,
        None,
    );
    library.write(
        "Terres d'Arran/Elfes/work.json",
        r#"{"leaf":1,"title":"Elfes"}"#,
    );

    // 4. A whole new universe.
    library.write("Marvel/universe.json", r#"{"leaf":1,"name":"Marvel"}"#);
    archive(
        &library.folder("Marvel/Spider-Man").join("Tome 1.cbz"),
        2,
        None,
    );

    // 5. A new work on its own, with no sidecar at all.
    archive(&library.folder("Naruto").join("Tome 1.cbz"), 2, None);

    // 6. A work whose editions are all in sub-folders — the shape that works.
    library.write("Death Note/work.json", r#"{"leaf":1,"title":"Death Note"}"#);
    archive(
        &library.folder("Death Note/Originale").join("Tome 1.cbz"),
        2,
        None,
    );
    archive(
        &library.folder("Death Note/Black").join("Tome 1.cbz"),
        2,
        None,
    );

    let report = library.scan();

    let landed = library.all(
        "SELECT COALESCE(u.name || ' / ', '') || w.name || ' / '
                || COALESCE(e.name, '(implicit)') || '  →  ' || COUNT(x.id) || ' entrée(s)'
         FROM edition e
         JOIN work w ON w.id = e.work_id
         LEFT JOIN universe u ON u.id = w.universe_id
         LEFT JOIN entry x ON x.edition_id = e.id
         GROUP BY e.id ORDER BY 1",
    );
    println!("\nce qui a atterri:");
    for row in &landed {
        println!("   {row}");
    }
    println!("signalé: {:?}", report.disregarded);

    // A work can hold both: the volumes you already had, and the edition you bought after.
    // Taken as proof there were no edition folders, the archives made that folder invisible —
    // files on the disk, nothing in the library, and no word said.
    assert!(
        landed.contains(&"Bleach / (implicit)  →  2 entrée(s)".to_string()),
        "{landed:#?}"
    );
    assert!(
        landed.contains(&"Bleach / Perfect Edition  →  1 entrée(s)".to_string()),
        "{landed:#?}"
    );

    // Everything else lands where it should, at the level it should.
    for expected in [
        "Death Note / Black",
        "Death Note / Originale",
        "Marvel / Spider-Man / (implicit)",
        "Naruto / (implicit)",
        "Terres d'Arran / Elfes / (implicit)",
        "Terres d'Arran / Nains / (implicit)",
    ] {
        assert!(
            landed.iter().any(|l| l.starts_with(expected)),
            "{expected}: {landed:#?}"
        );
    }
    assert_eq!(8, landed.len(), "{landed:#?}");
}

/// An entry says which edition it is in only when there is a choice to make.
#[test]
fn only_a_named_edition_asks_an_entry_to_name_it() {
    let library = Library::new();
    // A work with its own volumes and one bought edition beside them.
    let entry = r#"{"leaf":1,"work":"Bleach","number":1}"#;
    archive(
        &library.folder("Bleach").join("Tome 1.cbz"),
        2,
        Some(("entry.json", entry)),
    );
    archive(
        &library
            .folder("Bleach/Perfect Edition")
            .join("Deluxe 1.cbz"),
        2,
        Some(("entry.json", entry)),
    );

    let report = library.scan();

    // The one in the folder is asked: it could have been in either, and it said neither.
    assert!(
        report
            .missing_required
            .contains(&"Deluxe 1.cbz: edition".to_string()),
        "{:?}",
        report.missing_required
    );
    // The one sitting in the work folder is not: the implicit edition has no name, so there
    // is nothing it could have declared.
    assert!(
        !report
            .missing_required
            .iter()
            .any(|m| m.starts_with("Tome 1.cbz")),
        "{:?}",
        report.missing_required
    );
}

/// A shelf is not a level, so a universe below one is still a universe.
///
/// The shape a person actually builds: a folder to tidy up, holding whatever they own.
#[test]
fn a_shelf_is_walked_through_and_what_is_inside_says_what_it_is() {
    let library = Library::new();
    let mangas = library.folder("Mangas");

    // A work with a named edition.
    library.write(
        "Mangas/Dragon Ball/work.json",
        r#"{"leaf":1,"title":"Dragon Ball"}"#,
    );
    archive(
        &mangas.join("Dragon Ball/Perfect Edition/Tome 1.cbz"),
        2,
        None,
    );
    // A work with its volumes beside it, declaring nothing: the archives are the evidence.
    archive(&mangas.join("Bleach/Tome 1.cbz"), 2, None);
    // And a universe that says so.
    library.write(
        "Mangas/Terres d'Arran/universe.json",
        r#"{"leaf":1,"name":"Terres d'Arran"}"#,
    );
    archive(&mangas.join("Terres d'Arran/Nains/Tome 1.cbz"), 2, None);

    library.scan();

    // Mangas is nowhere: it declared nothing, so it is nothing.
    assert_eq!(
        vec!["Terres d'Arran".to_string()],
        library.all("SELECT name FROM universe")
    );
    assert_eq!(
        vec!["Bleach".to_string(), "Dragon Ball".into(), "Nains".into()],
        library.all("SELECT name FROM work ORDER BY name")
    );
    assert_eq!(
        vec!["Nains".to_string()],
        library.all(
            "SELECT w.name FROM work w JOIN universe u ON u.id = w.universe_id ORDER BY w.name"
        ),
        "only the work under the universe that declared itself belongs to one"
    );
    assert_eq!(
        vec!["Perfect Edition".to_string()],
        library.all("SELECT name FROM edition WHERE name IS NOT NULL")
    );
}

/// Shelves are free, but not infinite: a link back to a parent is a folder like any other.
#[test]
fn shelves_stop_being_followed_before_the_stack_gives_out() {
    let library = Library::new();
    let mut path = String::from("Profond");
    for i in 0..12 {
        path.push_str(&format!("/n{i}"));
    }
    archive(&library.folder(&path).join("Tome 1.cbz"), 2, None);

    let report = library.scan();

    assert_eq!(0, library.count("work"), "past the bound, nothing is read");
    assert!(
        report.disregarded.iter().any(|d| d.contains("nested past")),
        "and it is said rather than silent: {:?}",
        report.disregarded
    );
}

// -------------------------------------------------- the rules the server hands out

/// Every rule `GET /format` serves, built on a disk and checked.
///
/// The point of serving them is that an application never has to remember them. The point
/// of this is that the server never gets to say something it does not do — these rules
/// changed three times in one afternoon, and a list nobody executes is a list that rots.
#[test]
fn every_rule_the_server_serves_is_a_rule_it_follows() {
    let format = leaf_server::api::format::describe();

    for rule in &format.folders {
        let library = Library::new();
        let folder = library.folder("Sujet");
        match rule.holds.as_str() {
            "universe.json, and folders" => {
                library.write("Sujet/universe.json", r#"{"leaf":1,"name":"Sujet"}"#);
                archive(&folder.join("Dedans/Tome 1.cbz"), 2, None);
            }
            "work.json, and archives" => {
                library.write("Sujet/work.json", r#"{"leaf":1,"title":"Sujet"}"#);
                archive(&folder.join("Tome 1.cbz"), 2, None);
            }
            "work.json, and folders of archives" => {
                library.write("Sujet/work.json", r#"{"leaf":1,"title":"Sujet"}"#);
                archive(&folder.join("Deluxe/Tome 1.cbz"), 2, None);
            }
            "archives, and no sidecar" => {
                archive(&folder.join("Tome 1.cbz"), 2, None);
            }
            "folders, and no sidecar" => {
                archive(&folder.join("Dedans/Tome 1.cbz"), 2, None);
            }
            other => panic!("a rule is served that this test cannot build: {other:?}"),
        }

        library.scan();
        let universes = library.all("SELECT name FROM universe");
        let works = library.all("SELECT name FROM work ORDER BY name");
        let editions = library.all("SELECT name FROM edition WHERE name IS NOT NULL");

        match rule.becomes.as_str() {
            "UNIVERSE" => {
                assert_eq!(vec!["Sujet".to_string()], universes, "{}", rule.holds);
                assert_eq!(vec!["Dedans".to_string()], works, "{}", rule.holds);
            }
            "WORK" => {
                assert!(universes.is_empty(), "{}: {universes:?}", rule.holds);
                assert_eq!(vec!["Sujet".to_string()], works, "{}", rule.holds);
                // The reason given is checked too, not just the verdict.
                if rule.because.contains("named after the folder") {
                    assert_eq!(vec!["Deluxe".to_string()], editions, "{}", rule.holds);
                }
                if rule.because.contains("has no name") {
                    assert!(editions.is_empty(), "{}: {editions:?}", rule.holds);
                }
            }
            // Not a level at all: walked through, and what is inside says what it is.
            "SHELF" => {
                assert!(universes.is_empty(), "{}: {universes:?}", rule.holds);
                assert_eq!(vec!["Dedans".to_string()], works, "{}", rule.holds);
            }
            other => panic!("a verdict is served that this test cannot check: {other:?}"),
        }
    }

    // And the depths it advertises are the ones the walk uses, rather than a copy of them.
    assert_eq!(
        leaf_server::scan::layout::MODEL_DEPTH,
        format.limits.model_depth
    );
    assert_eq!(
        leaf_server::scan::layout::MAX_SHELVES,
        format.limits.max_shelves
    );
}

/// The fields it advertises are the fields a sidecar accepts.
#[test]
fn every_field_the_server_advertises_is_one_a_sidecar_reads_back() {
    use leaf_server::metadata::sidecars;

    for sidecar in leaf_server::api::format::describe().sidecars {
        assert!(
            !sidecar.fields.is_empty(),
            "{} advertises nothing",
            sidecar.file
        );

        // A document made of exactly the advertised names must survive a round trip through
        // the type: parsed, written back, and still carrying all of them. A name that no
        // longer exists is dropped on the way out and caught here.
        let document: serde_json::Value = sidecar
            .fields
            .iter()
            .map(|f| (f.clone(), placeholder(f)))
            .collect::<serde_json::Map<_, _>>()
            .into();
        let bytes = serde_json::to_vec(&document).unwrap();

        let back: serde_json::Value = match sidecar.file.as_str() {
            "universe.json" => written(sidecars::read::<sidecars::UniverseJson>(&bytes)),
            "work.json" => written(sidecars::read::<sidecars::WorkJson>(&bytes)),
            "edition.json" => written(sidecars::read::<sidecars::EditionJson>(&bytes)),
            "entry.json" => written(sidecars::read::<sidecars::EntryJson>(&bytes)),
            "oneshot.json" => written(sidecars::read::<sidecars::OneShotJson>(&bytes)),
            "chapters[]" => written(sidecars::read::<sidecars::ChapterJson>(&bytes)),
            "arcs[]" => written(sidecars::read::<sidecars::ArcJson>(&bytes)),
            other => panic!("a sidecar is advertised that this test does not know: {other}"),
        };

        let kept: Vec<String> = back
            .as_object()
            .expect("an object")
            .keys()
            .cloned()
            .collect();
        assert_eq!(
            sidecar.fields, kept,
            "{} loses a field it advertises",
            sidecar.file
        );
    }
}

/// Something each field will accept: they are strings, numbers, or lists of either.
fn placeholder(field: &str) -> serde_json::Value {
    match field {
        "leaf" | "startPage" | "volumeCount" | "coverPage" => serde_json::json!(1),
        "number" | "volume" | "after" | "from" | "to" | "coverVolume" => serde_json::json!(1.0),
        "genres" | "authors" | "artists" | "tags" => serde_json::json!([""]),
        "colour" => serde_json::json!(true),
        "arcs" => serde_json::json!([{"name": "", "unit": "VOLUME", "from": 1.0, "to": 1.0}]),
        "orders" => serde_json::json!([{"id": "", "name": "", "steps": [
            {"work": "", "unit": "VOLUME", "edition": "", "from": 1.0, "to": 1.0}]}]),
        "chapters" => serde_json::json!([{"raw": "", "number": 1.0, "title": "", "startPage": 1,
                                          "after": 1.0, "volume": 1.0, "label": ""}]),
        // Not the value serde would skip, or the field vanishes on the way out.
        "type" => serde_json::json!("CHAPTER"),
        "unit" => serde_json::json!("VOLUME"),
        _ => serde_json::json!(""),
    }
}

fn written<T: serde::Serialize>(parsed: Option<T>) -> serde_json::Value {
    serde_json::to_value(parsed.expect("the document parses")).expect("it writes back")
}

// ------------------------------------------------------------- what is refused

/// A ComicInfo.xml with whatever tags are handed in.
fn comic_info(tags: &[(&str, &str)]) -> String {
    let inside: String = tags
        .iter()
        .map(|(name, value)| format!("<{name}>{value}</{name}>"))
        .collect();
    format!("<?xml version=\"1.0\"?><ComicInfo>{inside}</ComicInfo>")
}

/// A unit the index refuses used to reach it anyway, from inside the transaction that holds
/// a whole shelf: the insert failed, the shelf was rolled back and never indexed again, and
/// a scan that is not complete prunes nothing — so a deletion three shelves away went unseen
/// too. One word, in one file, written by hand.
#[test]
fn an_arc_counted_in_something_that_is_not_a_unit_leaves_the_shelf_standing() {
    let library = Library::new();
    let bleach = library.folder("Bleach");
    archive(&bleach.join("Tome 1.cbz"), 2, None);
    library.write(
        "Bleach/work.json",
        r#"{"leaf":1,"title":"Bleach","arcs":[
             {"name":"Soul Society","unit":"tome","from":1,"to":2},
             {"name":"Arrancar","unit":"volume","from":3,"to":4}]}"#,
    );

    let report = library.scan();

    // The shelf is indexed, the volume is there, and the arc that could be read is too.
    assert_eq!(1, library.count("work"));
    assert_eq!(1, library.count("entry"));
    assert!(report.errors.is_empty(), "{:?}", report.errors);
    assert_eq!(
        vec!["Arrancar".to_string()],
        library.all("SELECT name FROM arc")
    );
    // The one it could not read is said out loud rather than guessed at: "tome" is somebody
    // meaning VOLUME, and a scan that invents that value is a scan that stops describing the
    // disk.
    assert!(
        report
            .disregarded
            .iter()
            .any(|said| said.contains("Soul Society") && said.contains("tome")),
        "{:?}",
        report.disregarded
    );

    // And the arc that was kept takes the place the one before it did not. `enumerate` handed
    // out an index per arc *declared*, so a disregarded one left position 0 and the id ending
    // `-arc-0` belonging to nothing. The order still came out right, which is why this went
    // unseen — but the column stopped meaning "the nth arc of this edition", and a count read
    // off the highest position is short by however many were left out.
    assert_eq!(Some(0), library.one::<i64>("SELECT position FROM arc"));
    assert!(
        library.all("SELECT id FROM arc")[0].ends_with("-arc-0"),
        "{:?}",
        library.all("SELECT id FROM arc")
    );
}

/// And the report names the file the arcs were actually read from, once.
///
/// The list and the name travelled apart: `declared` fell back to the work's when the edition
/// declared none, and `where_` was the edition folder either way. A `tome` typed in a work's
/// work.json was reported against one edition folder and then the other — twice, each time
/// naming an edition.json with no `arcs` in it at all. A report that sends somebody to the
/// wrong file is worse than one that says nothing.
/// A saga is an arc holding arcs, and nothing in two ranges says so.
///
/// A range of 1 to 100 with two ranges at 42 to 68 and 69 to 95 inside it — the shape a
/// long serial takes, with the names borrowed from One Piece rather than its figures, which
/// this file has no way to check. The containment is a coincidence of numbers until
/// somebody writes down that it is not: the format already lets arcs overlap for reasons
/// that have nothing to do with nesting, so reading a hierarchy out of the ranges would
/// invent one.
#[test]
fn a_saga_holds_the_arcs_a_file_says_it_holds() {
    let library = Library::new();
    library.write(
        "One Piece/work.json",
        r#"{"leaf":1,"title":"One Piece","arcs":[
             {"name":"East Blue","from":1,"to":100},
             {"name":"Baratié","parent":"East Blue","from":42,"to":68},
             {"name":"Arlong Park","parent":"East Blue","from":69,"to":95}]}"#,
    );
    archive(&library.folder("One Piece").join("Tome 1.cbz"), 2, None);

    let report = library.scan();

    assert_eq!(3, library.count("arc"));
    assert!(report.disregarded.is_empty(), "{:?}", report.disregarded);
    // The saga sits inside nothing, and the two arcs sit inside it — by its id and not by
    // its numbers.
    assert_eq!(
        vec!["Arlong Park", "Baratié"],
        library.all(
            "SELECT a.name FROM arc a JOIN arc p ON p.id = a.parent_id
             WHERE p.name = 'East Blue' ORDER BY a.name"
        )
    );
    assert_eq!(
        1,
        library
            .one::<i64>("SELECT COUNT(*) FROM arc WHERE parent_id IS NULL")
            .unwrap()
    );
}

/// The parent is resolved after every arc has an id, so an arc may name one declared below
/// it. Asking a file to put its sagas first would be asking the order of the lines to carry
/// a meaning the sidecar exists to state.
#[test]
fn an_arc_may_name_a_saga_declared_after_it() {
    let library = Library::new();
    library.write(
        "One Piece/work.json",
        r#"{"leaf":1,"title":"One Piece","arcs":[
             {"name":"Baratié","parent":"East Blue","from":42,"to":68},
             {"name":"East Blue","from":1,"to":100}]}"#,
    );
    archive(&library.folder("One Piece").join("Tome 1.cbz"), 2, None);

    library.scan();

    assert_eq!(
        vec!["Baratié"],
        library.all(
            "SELECT a.name FROM arc a JOIN arc p ON p.id = a.parent_id WHERE p.name = 'East Blue'"
        )
    );
}

/// Three ways a file can say something the index must not claim. Each costs the link and
/// never the arc: losing the range as well would turn one wrong word into a missing arc.
#[test]
fn a_parent_that_names_nothing_a_self_and_a_loop_are_all_refused() {
    for (arcs, expected) in [
        (
            r#"[{"name":"Baratié","parent":"East Blue","from":42,"to":68}]"#,
            "is not an arc of this edition",
        ),
        (
            r#"[{"name":"Baratié","parent":"Baratié","from":42,"to":68}]"#,
            "sits inside itself",
        ),
        (
            r#"[{"name":"A","parent":"B","from":1,"to":9},
                {"name":"B","parent":"A","from":2,"to":8}]"#,
            "come back round",
        ),
    ] {
        let library = Library::new();
        library.write(
            "One Piece/work.json",
            &format!(r#"{{"leaf":1,"title":"One Piece","arcs":{arcs}}}"#),
        );
        archive(&library.folder("One Piece").join("Tome 1.cbz"), 2, None);

        let report = library.scan();

        assert!(
            report.disregarded.iter().any(|l| l.contains(expected)),
            "expected {expected:?} in {:?}",
            report.disregarded
        );
        // The arcs are all there; only the links went.
        assert_eq!(
            0,
            library
                .one::<i64>("SELECT COUNT(*) FROM arc WHERE parent_id IS NOT NULL")
                .unwrap()
        );
        assert!(library.count("arc") > 0);
    }
}

/// Two arcs of one name leave a child unable to say which it belongs to. The first keeps
/// the name, and the file is told — a silent choice between two right-looking answers is
/// how an index comes to claim something nobody wrote.
#[test]
fn two_arcs_sharing_a_name_are_said_out_loud() {
    let library = Library::new();
    library.write(
        "One Piece/work.json",
        r#"{"leaf":1,"title":"One Piece","arcs":[
             {"name":"East Blue","from":1,"to":50},
             {"name":"East Blue","from":51,"to":100},
             {"name":"Baratié","parent":"East Blue","from":42,"to":68}]}"#,
    );
    archive(&library.folder("One Piece").join("Tome 1.cbz"), 2, None);

    let report = library.scan();

    assert!(
        report
            .disregarded
            .iter()
            .any(|l| l.contains("two arcs are called")),
        "{:?}",
        report.disregarded
    );
    // The first keeps it, so the child hangs off the range starting at 1.
    assert_eq!(
        1,
        library
            .one::<i64>(
                "SELECT CAST(p.from_number AS INT) FROM arc a JOIN arc p ON p.id = a.parent_id
                 WHERE a.name = 'Baratié'"
            )
            .unwrap()
    );
}

#[test]
fn an_arc_a_work_declares_is_reported_against_the_work_and_said_once() {
    let library = Library::new();
    library.write(
        "Bleach/work.json",
        r#"{"leaf":1,"title":"Bleach","arcs":[
             {"name":"Soul Society","unit":"tome","from":1,"to":2}]}"#,
    );
    archive(
        &library.folder("Bleach/Perfect Edition").join("Tome 1.cbz"),
        2,
        None,
    );
    archive(
        &library
            .folder("Bleach/Édition originale")
            .join("Tome 1.cbz"),
        2,
        None,
    );

    let report = library.scan();
    assert_eq!(2, library.count("edition"));

    let said: Vec<&String> = report
        .disregarded
        .iter()
        .filter(|line| line.contains("Soul Society"))
        .collect();
    assert_eq!(1, said.len(), "{:?}", report.disregarded);
    assert!(
        said[0].starts_with("Bleach:"),
        "the file to open is the one the arcs are in: {}",
        said[0]
    );
}

#[test]
fn archives_deeper_than_the_model_has_room_for_are_said_out_loud() {
    // Universe, work, edition is three floors. A fourth has nowhere to go, and a folder
    // whose archives sit below it must not be the same silence as an empty one.
    // It has to be a folder the model applies to — a shelf is simply walked through — so it
    // declares itself a work, and then keeps its archives four floors down.
    let library = Library::new();
    library.write("Bleach/work.json", r#"{"leaf":1,"title":"Bleach"}"#);
    archive(
        &library
            .folder("Bleach/Édition/Cycle/Partie/Encore")
            .join("Tome 1.cbz"),
        2,
        None,
    );
    let report = library.scan();

    assert_eq!(library.count("entry"), 0);
    assert!(
        report
            .disregarded
            .iter()
            .any(|line| line.contains("deeper than universe / work / edition")),
        "{:?}",
        report.disregarded
    );
}

#[test]
fn a_universe_inside_a_universe_is_read_as_a_work_and_the_report_says_so() {
    // Universes do not nest: the model is universe, work, edition, and a fourth level has
    // nowhere to go. Reading the inner one as a work is defensible, and baffling to meet
    // without being told.
    let library = Library::new();
    library.write(
        "Terres d'Arran/universe.json",
        r#"{"leaf":1,"name":"Terres d'Arran"}"#,
    );
    library.write(
        "Terres d'Arran/Elfes/universe.json",
        r#"{"leaf":1,"name":"Elfes"}"#,
    );
    archive(
        &library
            .folder("Terres d'Arran/Elfes/Le Crystal")
            .join("Tome 1.cbz"),
        2,
        None,
    );
    let report = library.scan();

    assert_eq!(library.count("universe"), 1);
    assert!(
        report
            .disregarded
            .iter()
            .any(|line| line.contains("a universe cannot hold another")),
        "{:?}",
        report.disregarded
    );
}

// ------------------------------------------------ what the legacy metadata fills

#[test]
fn comic_info_answers_for_a_work_that_declares_nothing() {
    // The files first, and the legacy metadata only where work.json is silent — so a
    // library nobody has annotated still shows an author and reads the right way round.
    let library = Library::new();
    archive(
        &library.folder("Bleach").join("Tome 1.cbz"),
        2,
        Some((
            "ComicInfo.xml",
            &comic_info(&[
                ("Writer", "Tite Kubo"),
                ("Manga", "YesAndRightToLeft"),
                ("Genre", "Shonen, Action"),
            ]),
        )),
    );
    library.scan();

    assert_eq!(
        library.all("SELECT name FROM work_author"),
        vec!["Tite Kubo"]
    );
    assert_eq!(
        library.one::<String>("SELECT reading_direction FROM work"),
        Some("RIGHT_TO_LEFT".to_string())
    );
    // Into the genre table, so they can be filtered on — not into a column of their own,
    // which showed them and made them unfilterable.
    let genres = library.all("SELECT name FROM work_genre ORDER BY name");
    assert!(genres.contains(&"Action".to_string()), "{genres:?}");
    assert!(genres.contains(&"Shonen".to_string()), "{genres:?}");
}

/// A tag is shown with a capital first and the rest as the sidecar wrote it, and « horreur »
/// beside « HORREUR » is one tag: the key ignores case, only the spelling shown is chosen.
#[test]
fn a_tag_is_shown_with_a_first_capital_and_counted_once_whatever_its_case() {
    let library = Library::new();
    library.write(
        "Bleach/work.json",
        r#"{"leaf":1,"title":"Bleach","tags":["banane","BD","horreur","HORREUR","éNIGME"]}"#,
    );
    archive(&library.folder("Bleach").join("Tome 1.cbz"), 2, None);
    library.scan();

    assert_eq!(
        library.all("SELECT name FROM work_tag ORDER BY name"),
        vec!["BD", "Banane", "Horreur", "ÉNIGME"]
    );
}

/// A row written in capitals by an index built before the spelling changed is put right by
/// the next scan, with no migration: the rows are rebuilt from the sidecars every time.
#[test]
fn a_scan_rewrites_tags_an_older_index_stored_in_capitals() {
    let library = Library::new();
    library.write(
        "Bleach/work.json",
        r#"{"leaf":1,"title":"Bleach","tags":["Combustion humaine"]}"#,
    );
    archive(&library.folder("Bleach").join("Tome 1.cbz"), 2, None);
    library.scan();
    library
        .db
        .write(|cx| {
            cx.execute("UPDATE work_tag SET name = UPPER(name)", [])?;
            Ok(())
        })
        .unwrap();
    assert_eq!(
        library.all("SELECT name FROM work_tag"),
        vec!["COMBUSTION HUMAINE"]
    );

    library.scan();

    assert_eq!(
        library.all("SELECT name FROM work_tag"),
        vec!["Combustion humaine"]
    );
}

/// Genres only: a volume's ComicInfo still speaks for that volume's credits before the series
/// does, on a library never imported — see the test of that name below.
#[test]
fn what_the_work_declares_wins_over_what_comic_info_says() {
    let library = Library::new();
    library.write(
        "Bleach/work.json",
        r#"{"leaf":1,"title":"Bleach","author":"Kubo","genres":["Shonen"]}"#,
    );
    archive(
        &library.folder("Bleach").join("Tome 1.cbz"),
        2,
        Some((
            "ComicInfo.xml",
            &comic_info(&[("Writer", "Quelqu'un d'autre"), ("Genre", "Romance")]),
        )),
    );
    library.scan();

    assert_eq!(library.all("SELECT name FROM work_genre"), vec!["Shonen"]);
}

/// The concrete failure this whole feature exists to fix: work.json used to have only a
/// singular `author`, so an illustrator credited nowhere else — Obata drew every page of
/// Death Note, and wrote none of it — could not be searched for at all.
#[test]
fn an_illustrator_credited_nowhere_else_is_still_searchable() {
    let library = Library::new();
    library.write(
        "Death Note/work.json",
        r#"{"leaf":1,"title":"Death Note","medium":"manga","authors":["Ōba"],
            "artists":["Obata"],"status":"completed","readingDirection":"RIGHT_TO_LEFT"}"#,
    );
    archive(&library.folder("Death Note").join("Tome 1.cbz"), 2, None);
    library.scan();

    let hits = leaf_server::store::Repository::new(&library.db)
        .search(
            "Obata",
            10,
            &[],
            &leaf_server::api::dto::SeriesFilter::default(),
            "default",
            &Ceiling::NONE,
        )
        .expect("searching");
    assert!(
        !hits.is_empty(),
        "searching the illustrator's name must find the series"
    );
}

// ------------------------------------------------------- credits, volume by volume

/// A volume carrying its own `entry.json` credits.
fn credited_volume(library: &Library, at: &str, authors: &str, artists: &str) {
    let (folder, file) = at.rsplit_once('/').unwrap();
    archive(
        &library.folder(folder).join(file),
        2,
        Some((
            "entry.json",
            &format!(r#"{{"leaf":1,"authors":{authors},"artists":{artists}}}"#),
        )),
    );
}

/// The first volume that said a `Writer` named the whole series, so a series whose script
/// changed hands at volume 2 was credited to the first writer only — and an illustrator was
/// never credited at all.
#[test]
fn a_series_whose_volumes_credit_different_people_is_credited_to_all_of_them() {
    let library = Library::new();
    library.write("Lanfeust/work.json", r#"{"leaf":1,"title":"Lanfeust"}"#);
    credited_volume(&library, "Lanfeust/Tome 1.cbz", r#"["A"]"#, r#"["B"]"#);
    credited_volume(&library, "Lanfeust/Tome 2.cbz", r#"["C"]"#, r#"["D"]"#);
    library.scan();

    assert_eq!(
        vec!["A", "C"],
        library.all("SELECT name FROM work_author ORDER BY rowid")
    );
    assert_eq!(
        vec!["B", "D"],
        library.all("SELECT name FROM work_artist ORDER BY rowid")
    );
}

/// `work.json` used to replace what the volumes said. It is now what a volume that says
/// nothing is credited with, so a default no volume falls back on credits nobody.
#[test]
fn a_default_every_volume_overrides_is_not_among_the_series_credits() {
    let library = Library::new();
    library.write(
        "Lanfeust/work.json",
        r#"{"leaf":1,"title":"Lanfeust","authors":["Collectif"]}"#,
    );
    credited_volume(&library, "Lanfeust/Tome 1.cbz", r#"["A"]"#, r#"["B"]"#);
    credited_volume(&library, "Lanfeust/Tome 2.cbz", r#"["C"]"#, r#"["D"]"#);
    library.scan();

    assert_eq!(
        vec!["A", "C"],
        library.all("SELECT name FROM work_author ORDER BY rowid")
    );
    assert_eq!(
        vec!["B", "D"],
        library.all("SELECT name FROM work_artist ORDER BY rowid")
    );
}

/// The singular `author` of the first format is a default like `authors`.
#[test]
fn the_legacy_singular_author_is_the_default_a_silent_volume_inherits() {
    let library = Library::new();
    library.write(
        "Lanfeust/work.json",
        r#"{"leaf":1,"title":"Lanfeust","author":"Arleston"}"#,
    );
    archive(&library.folder("Lanfeust").join("Tome 1.cbz"), 2, None);
    library.scan();

    assert_eq!(
        vec!["Arleston"],
        library.all("SELECT name FROM work_author")
    );
    assert_eq!(
        vec!["Arleston"],
        library.all("SELECT name FROM entry_author")
    );
}

#[test]
fn a_volume_without_entry_json_is_credited_from_its_comicinfo_writer_and_penciller() {
    let library = Library::new();
    archive(
        &library.folder("Lanfeust").join("Tome 1.cbz"),
        2,
        Some((
            "ComicInfo.xml",
            &comic_info(&[("Writer", "Arleston"), ("Penciller", "Tarquin")]),
        )),
    );
    library.scan();

    assert_eq!(
        vec!["Arleston"],
        library.all("SELECT name FROM work_author")
    );
    assert_eq!(vec!["Tarquin"], library.all("SELECT name FROM work_artist"));
}

/// « Arleston » in one volume and « ARLESTON » in the next are one person; counted twice the
/// series showed two authors and a search listed it twice.
#[test]
fn the_same_person_spelled_twice_is_credited_once() {
    let library = Library::new();
    library.write("Lanfeust/work.json", r#"{"leaf":1,"title":"Lanfeust"}"#);
    credited_volume(&library, "Lanfeust/Tome 1.cbz", r#"["Arleston"]"#, r#"[]"#);
    credited_volume(&library, "Lanfeust/Tome 2.cbz", r#"["ARLESTON"]"#, r#"[]"#);
    library.scan();

    assert_eq!(
        vec!["Arleston"],
        library.all("SELECT name FROM work_author")
    );
}

// ------------------------------------------- credits a series hands down to its volumes

/// The names a volume is credited with, as the index holds them, in the order they were met.
fn credited(library: &Library, table: &str, file: &str) -> Vec<String> {
    library.all(&format!(
        "SELECT c.name FROM {table} c JOIN entry x ON x.id = c.entry_id
         WHERE x.file LIKE '%/{file}' ORDER BY c.rowid"
    ))
}

fn inherited(library: &Library, column: &str, file: &str) -> i64 {
    library
        .one::<i64>(&format!(
            "SELECT {column} FROM entry WHERE file LIKE '%/{file}'"
        ))
        .expect("the volume is indexed")
}

/// One Piece: « Oda » typed once in `work.json` left a hundred volumes credited with nobody,
/// because a credit went up from the volumes and never came down.
#[test]
fn a_volume_that_credits_nobody_is_credited_with_its_series_default() {
    let library = Library::new();
    library.write(
        "One Piece/work.json",
        r#"{"leaf":1,"title":"One Piece","authors":["Eiichirō Oda"],"artists":["Eiichirō Oda"]}"#,
    );
    archive(&library.folder("One Piece").join("Tome 01.cbz"), 2, None);
    archive(&library.folder("One Piece").join("Tome 02.cbz"), 2, None);
    library.scan();

    for file in ["Tome 01.cbz", "Tome 02.cbz"] {
        assert_eq!(
            vec!["Eiichirō Oda"],
            credited(&library, "entry_author", file)
        );
        assert_eq!(
            vec!["Eiichirō Oda"],
            credited(&library, "entry_artist", file)
        );
        assert_eq!(1, inherited(&library, "authors_inherited", file));
        assert_eq!(1, inherited(&library, "artists_inherited", file));
    }
}

/// Lucky Luke: the script changed hands, the drawing did not. A volume naming its writer
/// replaces the writer it would have inherited and keeps the drawer — field by field.
fn lucky_luke(library: &Library) {
    library.write(
        "Lucky Luke/work.json",
        r#"{"leaf":1,"title":"Lucky Luke","authors":["René Goscinny"],"artists":["Morris"]}"#,
    );
    archive(&library.folder("Lucky Luke").join("Tome 01.cbz"), 2, None);
    archive(
        &library.folder("Lucky Luke").join("Tome 02.cbz"),
        2,
        Some(("entry.json", r#"{"leaf":1,"authors":["Jul"]}"#)),
    );
}

#[test]
fn a_volume_that_names_its_writer_keeps_the_series_drawer() {
    let library = Library::new();
    lucky_luke(&library);
    library.scan();

    assert_eq!(
        vec!["Jul"],
        credited(&library, "entry_author", "Tome 02.cbz")
    );
    assert_eq!(0, inherited(&library, "authors_inherited", "Tome 02.cbz"));
    assert_eq!(
        vec!["Morris"],
        credited(&library, "entry_artist", "Tome 02.cbz")
    );
    assert_eq!(1, inherited(&library, "artists_inherited", "Tome 02.cbz"));
}

/// `work.json` naming someone used to replace the union outright, and the series hid every
/// writer who took over.
#[test]
fn the_series_is_credited_with_the_union_of_what_its_volumes_are_credited_with() {
    let library = Library::new();
    lucky_luke(&library);
    library.scan();

    assert_eq!(
        vec!["René Goscinny", "Jul"],
        library.all("SELECT name FROM work_author ORDER BY rowid")
    );
    assert_eq!(vec!["Morris"], library.all("SELECT name FROM work_artist"));
}

#[test]
fn a_series_whose_volumes_all_speak_for_themselves_is_credited_with_them() {
    let library = Library::new();
    library.write("Duos/work.json", r#"{"leaf":1,"title":"Duos"}"#);
    credited_volume(&library, "Duos/Tome 1.cbz", r#"["A"]"#, r#"["B"]"#);
    credited_volume(&library, "Duos/Tome 2.cbz", r#"["C"]"#, r#"["D"]"#);
    library.scan();

    assert_eq!(vec!["A"], credited(&library, "entry_author", "Tome 1.cbz"));
    assert_eq!(vec!["D"], credited(&library, "entry_artist", "Tome 2.cbz"));
    assert_eq!(0, inherited(&library, "authors_inherited", "Tome 1.cbz"));
    assert_eq!(
        vec!["A", "C"],
        library.all("SELECT name FROM work_author ORDER BY rowid")
    );
}

/// A library never passed through the import still has its ComicInfo, and the volume's own
/// file says more about the volume than the series does.
#[test]
fn a_comicinfo_still_comes_before_the_series_default_on_a_library_never_imported() {
    let library = Library::new();
    library.write(
        "Lanfeust/work.json",
        r#"{"leaf":1,"title":"Lanfeust","authors":["A"]}"#,
    );
    archive(
        &library.folder("Lanfeust").join("Tome 1.cbz"),
        2,
        Some(("ComicInfo.xml", &comic_info(&[("Writer", "B")]))),
    );
    library.scan();

    assert_eq!(vec!["B"], credited(&library, "entry_author", "Tome 1.cbz"));
    assert_eq!(0, inherited(&library, "authors_inherited", "Tome 1.cbz"));
}

/// Not one volume moves when only `work.json` changes, and an edition whose files are all
/// as they were is skipped wholesale — which must not skip what its volumes inherit.
#[test]
fn a_default_changed_in_work_json_recredits_volumes_that_did_not_move() {
    let library = Library::new();
    library.write(
        "One Piece/work.json",
        r#"{"leaf":1,"title":"One Piece","authors":["Oda"]}"#,
    );
    archive(&library.folder("One Piece").join("Tome 01.cbz"), 2, None);
    library.scan();

    // Edited in place, so the identity the scan stamped into it stays.
    let path = library.folder("One Piece").join("work.json");
    let stamped = std::fs::read_to_string(&path).unwrap();
    std::fs::write(&path, stamped.replace("\"Oda\"", "\"Eiichirō Oda\"")).unwrap();
    library.scan();

    assert_eq!(1, library.count("entry"));
    assert_eq!(
        vec!["Eiichirō Oda"],
        credited(&library, "entry_author", "Tome 01.cbz")
    );
}

/// Bleach bought twice: the old volumes at the root of the series, the Perfect Edition in a
/// folder beside them. Both read the same `work.json`.
#[test]
fn volumes_beside_and_below_a_series_inherit_the_same_default() {
    let library = Library::new();
    library.write(
        "Bleach/work.json",
        r#"{"leaf":1,"title":"Bleach","authors":["Tite Kubo"]}"#,
    );
    archive(&library.folder("Bleach").join("Tome 01.cbz"), 2, None);
    archive(
        &library.folder("Bleach/Perfect").join("Perfect 01.cbz"),
        2,
        None,
    );
    library.scan();

    assert_eq!(
        vec!["Tite Kubo"],
        credited(&library, "entry_author", "Tome 01.cbz")
    );
    assert_eq!(
        vec!["Tite Kubo"],
        credited(&library, "entry_author", "Perfect 01.cbz")
    );
    assert_eq!(
        vec!["Tite Kubo"],
        library.all("SELECT name FROM work_author")
    );
}

/// A whole book dropped in a series folder is its own work: it was never one of the series'
/// volumes, so the series' writer is not its writer.
#[test]
fn a_book_standing_alone_in_a_series_folder_does_not_inherit_its_default() {
    let library = Library::new();
    library.write(
        "Lucky Luke/work.json",
        r#"{"leaf":1,"title":"Lucky Luke","authors":["Morris"]}"#,
    );
    archive(&library.folder("Lucky Luke").join("Tome 01.cbz"), 2, None);
    archive(
        &library.folder("Lucky Luke").join("Hommage.cbz"),
        2,
        Some(("oneshot.json", r#"{"leaf":1,"title":"Hommage"}"#)),
    );
    library.scan();

    assert!(credited(&library, "entry_author", "Hommage.cbz").is_empty());
    assert!(library
        .all(
            "SELECT a.name FROM work_author a JOIN work w ON w.id = a.work_id
             WHERE w.name = 'Hommage'"
        )
        .is_empty());
}

/// A volume was found by its title and nothing else: the people who made it were indexed on
/// its edition only.
#[test]
fn a_volume_is_found_by_the_name_it_is_credited_with() {
    let library = Library::new();
    library.write(
        "One Piece/work.json",
        r#"{"leaf":1,"title":"One Piece","authors":["Oda"]}"#,
    );
    archive(&library.folder("One Piece").join("Tome 01.cbz"), 2, None);
    library.scan();

    let hits = leaf_server::store::Repository::new(&library.db)
        .search(
            "Oda",
            10,
            &["ENTRY".to_string()],
            &leaf_server::api::dto::SeriesFilter::default(),
            "default",
            &Ceiling::NONE,
        )
        .expect("searching");
    assert_eq!(
        1,
        hits.len(),
        "{:?}",
        hits.iter().map(|h| &h.label).collect::<Vec<_>>()
    );
    assert_eq!("ENTRY", hits[0].kind);
}

/// A series of a hundred volumes inheriting two names would rewrite four hundred rows on
/// every scan if credits were rewritten whole: a rescan of what did not change costs the
/// same whether the volumes inherit names or have none.
#[test]
fn crediting_an_unchanged_edition_costs_nothing_per_volume() {
    let rescan = |work: &str, volumes: usize| {
        let library = Library::new();
        library.write("Série/work.json", work);
        for n in 1..=volumes {
            archive(
                &library.folder("Série").join(format!("Tome {n}.cbz")),
                2,
                None,
            );
        }
        library.scan();
        let before = library.db.statements();
        library.scan();
        library.db.statements() - before
    };
    let named = r#"{"leaf":1,"title":"Série","authors":["A","B"],"artists":["C"]}"#;
    let silent = r#"{"leaf":1,"title":"Série"}"#;
    assert_eq!(
        rescan(named, 6) - rescan(named, 3),
        rescan(silent, 6) - rescan(silent, 3)
    );
}

/// One volume credited and its neighbour credited by nobody: the series was not reported,
/// because somebody somewhere was credited.
#[test]
fn a_series_with_one_volume_credited_by_nobody_is_reported() {
    let library = Library::new();
    library.write("Duos/work.json", r#"{"leaf":1,"title":"Duos"}"#);
    credited_volume(&library, "Duos/Tome 1.cbz", r#"["A"]"#, r#"["B"]"#);
    archive(&library.folder("Duos").join("Tome 2.cbz"), 2, None);
    let report = library.scan();

    let said = report
        .missing_required
        .iter()
        .find(|m| m.contains("Duos/work.json"))
        .expect("the series is reported");
    assert!(said.contains("authors"), "{said}");
    assert!(said.contains("artists"), "{said}");
}

/// Obata drew one volume of a collective series and is credited nowhere in `work.json`:
/// searching his name found nothing.
#[test]
fn a_search_for_the_artist_of_one_volume_finds_the_series() {
    let library = Library::new();
    library.write("Death Note/work.json", r#"{"leaf":1,"title":"Death Note"}"#);
    credited_volume(
        &library,
        "Death Note/Tome 1.cbz",
        r#"["Ōba"]"#,
        r#"["Obata"]"#,
    );
    library.scan();

    let hits = leaf_server::store::Repository::new(&library.db)
        .search(
            "Obata",
            10,
            &[],
            &leaf_server::api::dto::SeriesFilter::default(),
            "default",
            &Ceiling::NONE,
        )
        .expect("searching");
    assert!(
        !hits.is_empty(),
        "the artist of a volume must find its series"
    );
}

/// An album lifted out of a folder is its own work: its credits are its own, and crediting
/// the series it stood beside would give a collection the author of one of its neighbours.
#[test]
fn a_book_lifted_out_of_a_folder_does_not_credit_the_series() {
    let library = Library::new();
    library.write("Lanfeust/work.json", r#"{"leaf":1,"title":"Lanfeust"}"#);
    credited_volume(&library, "Lanfeust/Tome 1.cbz", r#"["A"]"#, r#"[]"#);
    archive_with(
        &library.folder("Lanfeust").join("Hors-série.cbz"),
        &[
            ("oneshot.json", r#"{"leaf":1,"title":"Hors-série"}"#),
            (
                "entry.json",
                r#"{"leaf":1,"authors":["Ailleurs"],"artists":["Autre"]}"#,
            ),
        ],
    );
    library.scan();

    assert_eq!(
        vec!["A"],
        library.all(
            "SELECT a.name FROM work_author a JOIN work w ON w.id = a.work_id
             WHERE w.title = 'Lanfeust'"
        )
    );
    assert!(library
        .all("SELECT name FROM work_artist")
        .iter()
        .all(|name| name != "Autre"));
}

/// ComicInfo writes several people in one field, as it does for genres.
#[test]
fn a_comicinfo_writer_naming_two_people_credits_both() {
    let library = Library::new();
    library.write("Lanfeust/work.json", r#"{"leaf":1,"title":"Lanfeust"}"#);
    for (volume, writer) in [(1, "Arleston"), (2, "Arleston, Tarquin")] {
        archive(
            &library
                .folder("Lanfeust")
                .join(format!("Tome {volume}.cbz")),
            2,
            Some(("ComicInfo.xml", &comic_info(&[("Writer", writer)]))),
        );
    }
    library.scan();

    assert_eq!(
        vec!["Arleston", "Tarquin"],
        library.all("SELECT name FROM work_author ORDER BY rowid")
    );
}

/// A shelf of books standing alone is not a work, and its `work.json` is not missing: it
/// was reported before the files were read, and the work was unmade afterwards.
#[test]
fn a_shelf_of_standalone_books_does_not_report_a_missing_work_json() {
    let library = Library::new();
    for name in ["Blast", "Le Combat Ordinaire"] {
        archive(
            &library.folder("BD").join(format!("{name}.cbz")),
            2,
            Some(("oneshot.json", r#"{"leaf":1,"title":"Un livre"}"#)),
        );
    }
    let report = library.scan();
    assert!(
        !report
            .missing_required
            .iter()
            .any(|line| line.contains("BD/work.json missing")),
        "{:?}",
        report.missing_required
    );
}

/// The work.json check asked for authors the volumes already carried, and reported them
/// missing for every series whose credits were written volume by volume.
#[test]
fn credits_found_in_the_volumes_are_not_reported_missing() {
    let library = Library::new();
    library.write(
        "Lanfeust/work.json",
        r#"{"leaf":1,"title":"Lanfeust","medium":"manga","status":"completed",
            "readingDirection":"LEFT_TO_RIGHT"}"#,
    );
    credited_volume(&library, "Lanfeust/Tome 1.cbz", r#"["A"]"#, r#"["B"]"#);
    let report = library.scan();

    assert!(
        !report
            .missing_required
            .iter()
            .any(|line| line.contains("authors") || line.contains("artists")),
        "{:?}",
        report.missing_required
    );
}

#[test]
fn an_arc_repeated_in_every_volume_becomes_one_range_of_volumes() {
    // ComicInfo has nowhere to say where an arc ends, so it repeats the name inside every
    // volume it covers. The range is the span of the volumes that carried it.
    let library = Library::new();
    let bleach = library.folder("Bleach");
    for volume in 1..=3 {
        let arc = if volume == 3 {
            "Soul Society"
        } else {
            "Agent of the Shinigami"
        };
        archive(
            &bleach.join(format!("Tome {volume}.cbz")),
            2,
            Some((
                "ComicInfo.xml",
                &comic_info(&[("Number", &volume.to_string()), ("StoryArc", arc)]),
            )),
        );
    }
    library.scan();

    let arcs = library.all("SELECT name FROM arc ORDER BY position");
    assert_eq!(arcs, vec!["Agent of the Shinigami", "Soul Society"]);
    assert_eq!(
        library.one::<f64>("SELECT to_number FROM arc WHERE name = 'Agent of the Shinigami'"),
        Some(2.0)
    );
    assert_eq!(
        library.one::<String>("SELECT unit FROM arc WHERE name = 'Soul Society'"),
        Some("VOLUME".to_string())
    );
}

/// One volume of Solo Leveling whose chapters say what `chapters` says, under an edition
/// that declares `edition` when it is given.
fn named(edition: Option<&str>, chapters: &str) -> Library {
    let library = Library::new();
    if let Some(edition) = edition {
        library.write("Solo Leveling/edition.json", edition);
    }
    archive(
        &library.folder("Solo Leveling").join("Tome 1.cbz"),
        3,
        Some((
            "entry.json",
            &format!(r#"{{"leaf":1,"work":"Solo Leveling","number":1,"chapters":{chapters}}}"#),
        )),
    );
    library.scan();
    library
}

#[test]
fn a_numbered_chapter_nothing_names_is_chap_and_its_number() {
    // A file called « #13 » showed « #13 » in every place a chapter is named. What the file
    // said stays in `raw`; what is drawn is the default pattern.
    let library = named(None, r##"[{"raw":"#13 : Le réveil"},{"number":56.5}]"##);

    assert_eq!(
        vec!["Chap.\u{a0}13".to_string(), "Chap.\u{a0}56.5".to_string()],
        library.all("SELECT label FROM chapter ORDER BY position")
    );
    assert_eq!(
        Some("#13 : Le réveil".to_string()),
        library.one::<String>("SELECT raw FROM chapter WHERE number = 13")
    );
    assert_eq!(
        Some("Le réveil".to_string()),
        library.one::<String>("SELECT title FROM chapter WHERE number = 13")
    );
}

#[test]
fn the_edition_pattern_names_a_chapter_before_the_default_does() {
    let library = named(
        Some(r#"{"leaf":1,"chapterLabel":"Level.{n:00}"}"#),
        r##"[{"raw":"#1 : Nom"}]"##,
    );
    assert_eq!(
        Some("Level.01".to_string()),
        library.one::<String>("SELECT label FROM chapter")
    );
}

#[test]
fn a_chapter_label_wins_over_every_pattern() {
    let library = named(
        Some(r#"{"leaf":1,"chapterLabel":"Level.{n:00}"}"#),
        r#"[{"number":1,"label":"Prologue"}]"#,
    );
    assert_eq!(
        Some("Prologue".to_string()),
        library.one::<String>("SELECT label FROM chapter")
    );
}

#[test]
fn an_empty_chapter_label_leaves_the_title_alone_even_when_numbered() {
    let library = named(None, r#"[{"number":4,"label":"","title":"Épilogue"}]"#);
    assert_eq!(
        Some(String::new()),
        library.one::<String>("SELECT label FROM chapter")
    );
}

#[test]
fn an_unnumbered_bonus_keeps_the_label_its_file_gave_it() {
    let library = named(
        None,
        r#"[{"number":1},{"raw":"Bonus : Note de l'auteur","after":1}]"#,
    );
    assert_eq!(
        Some("Bonus".to_string()),
        library.one::<String>("SELECT label FROM chapter WHERE kind = 'BONUS'")
    );
}

#[test]
fn a_rescan_renames_a_chapter_its_files_have_not_moved_since() {
    // Labels are computed at scan, and a scan does not recompute the chapters of an edition
    // whose files have not moved. A library indexed while the file name still named the
    // chapter kept « #13 » through every rescan after the default changed.
    let library = named(None, r##"[{"raw":"#13 : Le réveil"}]"##);
    library
        .db
        .write(|cx| cx.execute("UPDATE chapter SET label = '#13'", []))
        .unwrap();

    let report = library.scan();

    assert_eq!(0, report.reanalysed, "not one archive is opened for it");
    assert_eq!(
        Some("Chap.\u{a0}13".to_string()),
        library.one::<String>("SELECT label FROM chapter")
    );
}

#[test]
fn a_standalone_chapter_file_is_described_by_its_own_name() {
    // Its file name is all there is: no entry.json, and the whole file is the chapter.
    let library = Library::new();
    let bleach = library.folder("Bleach");
    archive(&bleach.join("Tome 1.cbz"), 3, None);
    archive(&bleach.join("Chapitre 45.5 - Un bonus.cbz"), 2, None);
    library.scan();

    assert_eq!(
        library.one::<String>("SELECT type FROM entry WHERE file LIKE '%45.5%'"),
        Some("CHAPTER".to_string())
    );
    // It occupies a number in the edition either way, which is what lets a 45.5 read
    // between 45 and 46.
    let labels = library.all("SELECT label FROM chapter ORDER BY position");
    assert!(labels.iter().any(|l| l.contains("45.5")), "{labels:?}");
}

#[test]
fn a_chapter_that_says_nothing_at_all_is_skipped_and_said_out_loud() {
    // No label, no title, no number: there is nothing to draw and nothing to order it by,
    // so it is dropped rather than shown as a blank row.
    let library = Library::new();
    archive(
        &library.folder("Bleach").join("Tome 1.cbz"),
        3,
        Some((
            "entry.json",
            r#"{"leaf":1,"work":"Bleach","number":1,"chapters":[{},{"number":2,"title":"Deux"}]}"#,
        )),
    );
    let report = library.scan();

    assert_eq!(library.count("chapter"), 1);
    assert!(
        report
            .errors
            .iter()
            .any(|line| line.contains("no label, title or number")),
        "{:?}",
        report.errors
    );
}

#[test]
fn a_chapter_with_no_raw_gets_one_composed_from_what_it_does_have() {
    // `raw` is what the file said; when nothing said anything, the label and the title are
    // put back together the way a reader would have written them.
    let library = Library::new();
    library.write(
        "Bleach/edition.json",
        r#"{"leaf":1,"chapterLabel":"Chapitre {n:000}"}"#,
    );
    archive(
        &library.folder("Bleach").join("Tome 1.cbz"),
        3,
        Some((
            "entry.json",
            r#"{"leaf":1,"work":"Bleach","number":1,"chapters":[{"number":7,"title":"Ennui"}]}"#,
        )),
    );
    library.scan();

    assert_eq!(
        library.one::<String>("SELECT raw FROM chapter"),
        Some("Chapitre 007 : Ennui".to_string())
    );
}

#[test]
fn two_pages_of_one_name_inside_a_volume_reach_the_report() {
    let library = Library::new();
    let path = library.folder("Bleach").join("Tome 1.cbz");
    std::fs::create_dir_all(path.parent().unwrap()).unwrap();
    let mut zip = zip::ZipWriter::new(std::fs::File::create(&path).unwrap());
    let options = zip::write::SimpleFileOptions::default();
    for folder in ["Chapitre 1", "Chapitre 2"] {
        zip.start_file::<_, ()>(format!("{folder}/001.jpg"), options)
            .unwrap();
        zip.write_all(&jpeg(100, 140)).unwrap();
    }
    zip.finish().unwrap();

    let report = library.scan();
    assert!(
        report
            .duplicate_page_names
            .iter()
            .any(|line| line.contains("001.jpg")),
        "{:?}",
        report.duplicate_page_names
    );
}

// ------------------------------------------------------- aiming at one work

#[test]
fn a_rescan_aimed_at_a_universe_is_refused_rather_than_filing_its_works_as_editions() {
    // A universe read as a work turns its works into editions of itself. The rule is stated
    // on the method that would do the damage, and not only at the door that calls it.
    let library = Library::new();
    library.write(
        "Terres d'Arran/universe.json",
        r#"{"leaf":1,"name":"Terres d'Arran"}"#,
    );
    archive(
        &library.folder("Terres d'Arran/Elfes").join("Tome 1.cbz"),
        2,
        None,
    );
    library.scan();

    let refused = Scanner::new(Arc::clone(&library.db), true)
        .rescan_work(&library.dir.path().join("library/Terres d'Arran"))
        .unwrap_err()
        .to_string();
    assert!(refused.contains("declares itself a universe"), "{refused}");
}

#[test]
fn a_work_inside_a_universe_keeps_it_when_only_that_work_is_read_again() {
    let library = Library::new();
    library.write(
        "Terres d'Arran/universe.json",
        r#"{"leaf":1,"name":"Terres d'Arran"}"#,
    );
    let elfes = library.folder("Terres d'Arran/Elfes");
    archive(&elfes.join("Tome 1.cbz"), 2, None);
    library.scan();
    assert_eq!(library.count("universe"), 1);

    Scanner::new(Arc::clone(&library.db), true)
        .rescan_work(&elfes)
        .expect("aiming at the work");

    // Still under its universe, and still one work: a targeted read must not orphan it.
    assert_eq!(library.count("work"), 1);
    assert!(library
        .one::<String>("SELECT universe_id FROM work")
        .is_some());
}

#[test]
fn a_work_whose_folder_has_gone_is_dropped_when_it_is_read_again() {
    // The folder is gone, or holds nothing any more: what it left behind goes with it.
    let library = Library::new();
    let bleach = library.folder("Bleach");
    archive(&bleach.join("Tome 1.cbz"), 2, None);
    library.scan();
    assert_eq!(library.count("work"), 1);

    std::fs::remove_dir_all(&bleach).unwrap();
    Scanner::new(Arc::clone(&library.db), true)
        .rescan_work(&bleach)
        .expect("aiming at what is no longer there");

    assert_eq!(library.count("work"), 0);
    assert_eq!(library.count("entry"), 0);
}

#[test]
fn a_folder_that_is_there_and_shut_is_not_the_same_as_one_that_is_gone() {
    // A series disappearing from the shelf because a permission changed is the failure this
    // refuses: shut is refused loudly, gone is pruned quietly.
    let library = Library::new();
    let bleach = library.folder("Bleach");
    archive(&bleach.join("Tome 1.cbz"), 2, None);
    library.scan();

    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        std::fs::set_permissions(&bleach, std::fs::Permissions::from_mode(0o000)).unwrap();
        let refused = Scanner::new(Arc::clone(&library.db), true)
            .rescan_work(&bleach)
            .unwrap_err()
            .to_string();
        assert!(refused.contains("cannot be listed"), "{refused}");
    }
    writable(&bleach);
    // And the work is still there: nothing was pruned on the strength of a closed door.
    assert_eq!(library.count("work"), 1);
}

#[test]
fn a_folder_that_holds_no_archive_is_read_as_nothing_at_all() {
    let library = Library::new();
    let empty = library.folder("Bleach");
    library.scan();
    assert_eq!(library.count("work"), 0);

    Scanner::new(Arc::clone(&library.db), true)
        .rescan_work(&empty)
        .expect("aiming at an empty folder");
    assert_eq!(library.count("work"), 0);
}

// ------------------------------------------------------------ what is pruned

#[test]
fn a_volume_taken_off_the_disk_is_taken_out_of_the_index_when_the_work_is_read_again() {
    let library = Library::new();
    let bleach = library.folder("Bleach");
    archive(&bleach.join("Tome 1.cbz"), 2, None);
    archive(&bleach.join("Tome 2.cbz"), 2, None);
    library.scan();
    assert_eq!(library.count("entry"), 2);

    std::fs::remove_file(bleach.join("Tome 2.cbz")).unwrap();
    Scanner::new(Arc::clone(&library.db), true)
        .rescan_work(&bleach)
        .expect("aimed");

    assert_eq!(library.count("entry"), 1);
    // And what pointed at it in the search index went with it.
    assert!(library
        .all("SELECT id FROM entry")
        .iter()
        .all(|id| !id.is_empty()));
}

#[test]
fn an_edition_writes_its_own_summary_and_leaves_the_works_alone() {
    // The column is new, and the answer coalesces it over the work's — so an edition whose
    // sidecar says nothing has to reach the table as NULL and not as an empty string, which
    // would win the coalesce and describe the edition with nothing at all.
    let library = Library::new();
    library.write(
        "Bleach/work.json",
        r#"{"leaf":1,"title":"Bleach","summary":"Un lycéen voit les morts."}"#,
    );
    library.write(
        "Bleach/Perfect Edition/edition.json",
        r#"{"leaf":1,"name":"Perfect Edition","summary":"Les 74 tomes en 40, au grand format."}"#,
    );
    library.write("Bleach/Poche/edition.json", r#"{"leaf":1,"name":"Poche"}"#);
    for edition in ["Perfect Edition", "Poche"] {
        archive(
            &library
                .folder(&format!("Bleach/{edition}"))
                .join("Tome 1.cbz"),
            2,
            None,
        );
    }
    library.scan();

    assert_eq!(
        library.one::<String>("SELECT summary FROM work"),
        Some("Un lycéen voit les morts.".to_string())
    );
    assert_eq!(
        library.one::<String>("SELECT summary FROM edition WHERE name = 'Perfect Edition'"),
        Some("Les 74 tomes en 40, au grand format.".to_string())
    );
    // Counted rather than read: `one` hands back `None` for a row that is missing and for a
    // column that is NULL alike, so reading the Poche row could not tell « no summary » from
    // « no edition » — and the second would pass this test while meaning the scan lost it.
    assert_eq!(library.count("edition"), 2);
    assert_eq!(
        library.one::<i64>("SELECT COUNT(*) FROM edition WHERE summary IS NOT NULL"),
        Some(1),
        "an edition that says nothing about itself holds no summary at all"
    );
}

#[test]
fn an_edition_folder_that_has_gone_takes_its_edition_with_it() {
    let library = Library::new();
    library.write("Bleach/work.json", r#"{"leaf":1,"title":"Bleach"}"#);
    for edition in ["Perfect Edition", "Poche"] {
        library.write(
            &format!("Bleach/{edition}/edition.json"),
            &format!(r#"{{"leaf":1,"name":"{edition}"}}"#),
        );
        archive(
            &library
                .folder(&format!("Bleach/{edition}"))
                .join("Tome 1.cbz"),
            2,
            None,
        );
    }
    library.scan();
    assert_eq!(library.count("edition"), 2);

    std::fs::remove_dir_all(library.dir.path().join("library/Bleach/Poche")).unwrap();
    Scanner::new(Arc::clone(&library.db), true)
        .rescan_work(&library.dir.path().join("library/Bleach"))
        .expect("aimed");

    assert_eq!(library.count("edition"), 1);
    assert_eq!(library.count("entry"), 1);
}

#[test]
fn pruning_a_work_that_was_never_recorded_is_nothing_to_do() {
    let library = Library::new();
    let bleach = library.folder("Bleach");
    archive(&bleach.join("Tome 1.cbz"), 2, None);
    // Never scanned, so nothing is in the index to prune — and aiming at it is not an error.
    Scanner::new(Arc::clone(&library.db), true)
        .rescan_work(&bleach)
        .expect("aimed at a work the index has never seen");
    assert_eq!(library.count("work"), 1);
}

#[test]
fn an_archive_that_cannot_be_read_is_reported_and_the_rest_of_the_folder_is_read() {
    // One bad file must not cost the other nine: the scan says what it could not read and
    // carries on.
    let library = Library::new();
    let bleach = library.folder("Bleach");
    archive(&bleach.join("Tome 1.cbz"), 2, None);
    std::fs::write(bleach.join("Tome 2.cbz"), b"not a zip at all").unwrap();

    let report = library.scan();
    assert_eq!(library.count("entry"), 1);
    assert!(
        report.errors.iter().any(|e| e.contains("Tome 2.cbz")),
        "{:?}",
        report.errors
    );
}

#[test]
fn a_half_number_keeps_its_half_wherever_it_is_written_down() {
    // 45.5 reads between 45 and 46, and says so in every place a number is spelled out.
    let library = Library::new();
    archive(
        &library.folder("Bleach").join("Tome 1.cbz"),
        3,
        Some((
            "entry.json",
            r#"{"leaf":1,"work":"Bleach","number":1,"chapters":[
                 {"number":45.5,"title":"Un bonus"},{"number":45.5,"title":"Un autre"}]}"#,
        )),
    );
    let report = library.scan();
    assert!(
        report.duplicate_numbers.iter().any(|d| d.contains("45.5")),
        "{:?}",
        report.duplicate_numbers
    );
}

#[test]
fn a_scan_that_measures_nothing_records_the_pages_without_their_size() {
    // Measuring every page is most of a scan's cost. Without it the pages are still there,
    // still in order, and their dimensions are null — which a client has to read as "I do
    // not know" rather than as zero.
    let library = Library::new();
    archive(&library.folder("Bleach").join("Tome 1.cbz"), 3, None);

    Scanner::new(Arc::clone(&library.db), false)
        .scan(&[library.dir.path().join("library")])
        .expect("scanning");

    assert_eq!(library.count("page"), 3);
    let sized: i64 = library
        .db
        .read(|cx| {
            Ok(cx
                .query_one(
                    "SELECT COUNT(*) FROM page WHERE width IS NOT NULL",
                    [],
                    |r| r.get(0),
                )?
                .unwrap_or(0))
        })
        .unwrap();
    // The cover is measured whatever happens — the shelf needs it — and the rest are not.
    assert!(
        sized <= 1,
        "{sized} pages measured when none were asked for"
    );
}

#[test]
fn an_arc_over_half_numbers_keeps_its_halves_in_the_report() {
    let library = Library::new();
    library.write(
        "Bleach/edition.json",
        r#"{"leaf":1,"arcs":[{"name":"Un cycle","unit":"CHAPTER","from":45.5,"to":108.5}]}"#,
    );
    archive(&library.folder("Bleach").join("Tome 1.cbz"), 2, None);
    let report = library.scan();
    let said = report.summary();
    assert!(said.contains("45.5") || library.count("arc") == 1, "{said}");
}

#[test]
fn a_work_read_again_inside_its_universe_keeps_the_universe_in_its_composed_name() {
    // The universe surfaces inside the name a shelf draws, so a work read on its own has to
    // find it again rather than losing it.
    let library = Library::new();
    library.write(
        "Terres d'Arran/universe.json",
        r#"{"leaf":1,"name":"Terres d'Arran"}"#,
    );
    let elfes = library.folder("Terres d'Arran/Elfes");
    archive(&elfes.join("Tome 1.cbz"), 2, None);
    library.scan();

    // A second full scan: the universe is already in the index, so the work is visited with
    // it rather than alongside it.
    library.scan();
    assert_eq!(library.count("universe"), 1);
    assert_eq!(library.count("work"), 1);
}

#[test]
fn an_arc_over_half_volumes_keeps_its_halves_in_what_is_reported() {
    // ComicInfo repeats the arc name in every volume it covers, and a volume can carry a
    // half number — a side story reading between two. The range says 45.5, not 45.
    let library = Library::new();
    let bleach = library.folder("Bleach");
    for (name, number) in [("Tome 1.cbz", "1"), ("Tome 45.5.cbz", "45.5")] {
        archive(
            &bleach.join(name),
            2,
            Some((
                "ComicInfo.xml",
                &comic_info(&[("Number", number), ("StoryArc", "Un cycle")]),
            )),
        );
    }
    let report = library.scan();
    assert!(
        report.derived_arcs.iter().any(|a| a.contains("45.5")),
        "{:?}",
        report.derived_arcs
    );
}

#[test]
fn a_universe_holding_a_folder_with_no_archive_in_it_records_no_work() {
    // A folder under a universe that holds nothing is not a work with no volumes: it is not
    // a work. Recording it would put an empty row on the shelf that nothing can ever fill.
    let library = Library::new();
    library.write(
        "Terres d'Arran/universe.json",
        r#"{"leaf":1,"name":"Terres d'Arran"}"#,
    );
    library.folder("Terres d'Arran/Rien du tout");
    archive(
        &library.folder("Terres d'Arran/Elfes").join("Tome 1.cbz"),
        2,
        None,
    );
    library.scan();

    assert_eq!(library.count("work"), 1);
    assert_eq!(library.all("SELECT name FROM work"), vec!["Elfes"]);
}

// ------------------------------------------------------- the ways through a universe

/// A universe with two works and one order over both of them, which is the shape almost
/// every declaration takes: read this, then that.
fn an_universe_with_two_works(library: &Library, universe: &str) {
    for (work, volumes) in [("Series A", 2), ("Series B", 1)] {
        let folder = library.folder(&format!("{universe}/{work}"));
        for v in 1..=volumes {
            archive(&folder.join(format!("Tome {v}.cbz")), 3, None);
        }
    }
}

#[test]
fn an_order_becomes_steps_in_the_order_they_were_written() {
    let library = Library::new();
    an_universe_with_two_works(&library, "Arran");
    library.write(
        "Arran/universe.json",
        r#"{
          "leaf": 1, "name": "Arran", "defaultOrder": "recommended",
          "orders": [
            { "id": "recommended", "name": "Ordre conseillé", "steps": [
                { "work": "Series A", "unit": "CHAPTER", "from": 1, "to": 120 },
                { "work": "Series B" },
                { "work": "Series A", "unit": "CHAPTER", "from": 121 }
            ]},
            { "id": "publication", "name": "Ordre de parution", "steps": [
                { "work": "Series B" }
            ]}
          ]
        }"#,
    );

    let report = library.scan();
    assert!(report.disregarded.is_empty(), "{:?}", report.disregarded);
    assert_eq!(2, library.count("reading_order"));
    assert_eq!(4, library.count("reading_order_step"));

    // Written first, offered first — and the one named by `defaultOrder` is the default,
    // which is a different question from being first.
    assert_eq!(
        vec!["recommended".to_string(), "publication".to_string()],
        library.all("SELECT declared_id FROM reading_order ORDER BY position")
    );
    assert_eq!(
        Some("recommended".to_string()),
        library.one("SELECT declared_id FROM reading_order WHERE is_default = 1")
    );

    // The same work twice, with another between: the whole reason a step is not an edition.
    assert_eq!(
        vec![
            "Series A".to_string(),
            "Series B".to_string(),
            "Series A".to_string()
        ],
        library.all(
            "SELECT w.name FROM reading_order_step s
             JOIN work w ON w.id = s.work_id
             JOIN reading_order o ON o.id = s.order_id
             WHERE o.declared_id = 'recommended' ORDER BY s.position"
        )
    );

    // A step with no range is the whole work, and says so by holding no bounds at all
    // rather than by holding bounds nobody can tell from real ones.
    assert_eq!(
        Some(1i64),
        library.one(
            "SELECT COUNT(*) FROM reading_order_step s
             JOIN reading_order o ON o.id = s.order_id
             WHERE o.declared_id = 'recommended'
               AND s.unit IS NULL AND s.from_number IS NULL AND s.to_number IS NULL"
        )
    );
    // And an open end is an absent `to`, not a large number standing in for one.
    assert_eq!(
        Some(121.0f64),
        library.one(
            "SELECT from_number FROM reading_order_step
             WHERE to_number IS NULL AND from_number IS NOT NULL"
        )
    );
}

/// The names of the works of one order, in the order they are read.
fn works_of_order(library: &Library, declared: &str) -> Vec<String> {
    library.all(&format!(
        "SELECT w.name FROM reading_order_step s
         JOIN work w ON w.id = s.work_id
         JOIN reading_order o ON o.id = s.order_id
         WHERE o.declared_id = '{declared}' ORDER BY s.position"
    ))
}

/// « Tout l'univers » was written by copying the steps of a shorter order into it, and the
/// copy went stale the first time the shorter one changed.
#[test]
fn an_included_order_is_read_in_its_place() {
    let library = Library::new();
    an_universe_with_two_works(&library, "Arran");
    archive(
        &library.folder("Arran/Series C").join("Tome 1.cbz"),
        3,
        None,
    );
    library.write(
        "Arran/universe.json",
        r#"{"leaf": 1, "orders": [
            {"id": "whole", "steps": [
                {"work": "Series A", "unit": "CHAPTER", "from": 1, "to": 4},
                {"include": "part"},
                {"work": "Series A", "unit": "CHAPTER", "from": 5, "to": 8}
            ]},
            {"id": "part", "steps": [{"work": "Series B"}, {"work": "Series C"}]}
        ]}"#,
    );

    let report = library.scan();
    assert!(report.disregarded.is_empty(), "{:?}", report.disregarded);
    assert_eq!(
        vec!["Series A", "Series B", "Series C", "Series A"],
        works_of_order(&library, "whole")
    );
    assert_eq!(2, works_of_order(&library, "part").len());
}

#[test]
fn an_order_without_an_id_is_included_by_its_name() {
    let library = Library::new();
    an_universe_with_two_works(&library, "Arran");
    library.write(
        "Arran/universe.json",
        r#"{"leaf": 1, "orders": [
            {"id": "whole", "steps": [{"include": "Series B order"}, {"work": "Series A"}]},
            {"name": "Series B order", "steps": [{"work": "Series B"}]}
        ]}"#,
    );

    let report = library.scan();
    assert!(report.disregarded.is_empty(), "{:?}", report.disregarded);
    assert_eq!(
        vec!["Series B", "Series A"],
        works_of_order(&library, "whole")
    );
}

/// Reading a loop from whichever end one enters it gave `a` half of `b` and `b` half of
/// `a`: the same file answering one question twice, differently.
#[test]
fn an_order_that_includes_itself_through_another_is_refused_in_both() {
    let library = Library::new();
    an_universe_with_two_works(&library, "Arran");
    library.write(
        "Arran/universe.json",
        r#"{"leaf": 1, "orders": [
            {"id": "a", "steps": [{"work": "Series A"}, {"include": "b"}]},
            {"id": "b", "steps": [{"include": "a"}, {"work": "Series B"}]}
        ]}"#,
    );

    let report = library.scan();
    assert_eq!(vec!["Series A"], works_of_order(&library, "a"));
    assert_eq!(vec!["Series B"], works_of_order(&library, "b"));
    assert_eq!(2, report.disregarded.len(), "{:?}", report.disregarded);
    assert!(report
        .disregarded
        .iter()
        .any(|one| one.contains("order \"a\", step 2") && one.contains("loop")));
    assert!(report
        .disregarded
        .iter()
        .any(|one| one.contains("order \"b\", step 1") && one.contains("loop")));
}

#[test]
fn an_include_naming_no_order_is_left_out_and_said() {
    let library = Library::new();
    an_universe_with_two_works(&library, "Arran");
    library.write(
        "Arran/universe.json",
        r#"{"leaf": 1, "orders": [
            {"id": "a", "steps": [{"work": "Series A"}, {"include": "nowhere"}, {"work": "Series B"}]}
        ]}"#,
    );

    let report = library.scan();
    assert_eq!(vec!["Series A", "Series B"], works_of_order(&library, "a"));
    assert_eq!(1, report.disregarded.len(), "{:?}", report.disregarded);
    assert!(report.disregarded[0].contains("step 2"));
    assert!(report.disregarded[0].contains("nowhere"));
}

#[test]
fn an_include_that_also_names_a_work_is_refused() {
    let library = Library::new();
    an_universe_with_two_works(&library, "Arran");
    library.write(
        "Arran/universe.json",
        r#"{"leaf": 1, "orders": [
            {"id": "a", "steps": [{"include": "b", "work": "Series A"}]},
            {"id": "b", "steps": [{"work": "Series B"}]}
        ]}"#,
    );

    let report = library.scan();
    assert!(works_of_order(&library, "a").is_empty());
    assert_eq!(vec!["Series B"], works_of_order(&library, "b"));
    assert_eq!(1, report.disregarded.len(), "{:?}", report.disregarded);
    assert!(report.disregarded[0].contains("carries nothing else"));
}

/// A step pointing at a work the universe does not hold was said once by the order that
/// wrote it, then again by every order that included that one.
#[test]
fn a_broken_step_of_an_included_order_is_reported_once() {
    let library = Library::new();
    an_universe_with_two_works(&library, "Arran");
    library.write(
        "Arran/universe.json",
        r#"{"leaf": 1, "orders": [
            {"id": "whole", "steps": [{"include": "part"}, {"work": "Series A"}]},
            {"id": "again", "steps": [{"include": "part"}]},
            {"id": "part", "steps": [{"work": "Series Z"}, {"work": "Series B"}]}
        ]}"#,
    );

    let report = library.scan();
    assert_eq!(
        vec!["Series B", "Series A"],
        works_of_order(&library, "whole")
    );
    assert_eq!(1, report.disregarded.len(), "{:?}", report.disregarded);
    assert!(report.disregarded[0].contains("order \"part\", step 1"));
}

/// Fourteen orders each reading the next twice are 8 192 steps from a file of a few
/// hundred bytes; the database must not be the thing that finds out.
#[test]
fn an_include_that_doubles_at_every_level_stops_at_the_ceiling() {
    let library = Library::new();
    an_universe_with_two_works(&library, "Arran");
    let mut orders = Vec::new();
    for level in 0..14 {
        let steps = if level == 13 {
            r#"{"work": "Series A"}"#.to_string()
        } else {
            let next = format!(r#"{{"include": "level-{}"}}"#, level + 1);
            format!("{next}, {next}")
        };
        orders.push(format!(r#"{{"id": "level-{level}", "steps": [{steps}]}}"#));
    }
    library.write(
        "Arran/universe.json",
        &format!(r#"{{"leaf": 1, "orders": [{}]}}"#, orders.join(",")),
    );

    let report = library.scan();
    assert!(works_of_order(&library, "level-0").is_empty());
    assert_eq!(4096, works_of_order(&library, "level-1").len());
    let most: i64 = library
        .one("SELECT MAX(n) FROM (SELECT COUNT(*) AS n FROM reading_order_step GROUP BY order_id)")
        .unwrap();
    assert!(most <= 4096, "{most}");
    assert!(
        report
            .contradictions
            .iter()
            .any(|one| one.contains("level-0") && one.contains("4096")),
        "{:?}",
        report.contradictions
    );
}

/// A chain of ten thousand orders, each including the next, was ten thousand frames of
/// recursion for a file that fits in a few hundred kilobytes.
#[test]
fn a_ten_thousand_deep_include_chain_does_not_crash_and_is_reported() {
    let library = Library::new();
    an_universe_with_two_works(&library, "Arran");
    let depth = 10_000;
    let orders: Vec<String> = (0..depth)
        .map(|level| {
            let step = if level + 1 == depth {
                r#"{"work": "Series A"}"#.to_string()
            } else {
                format!(r#"{{"include": "o{}"}}"#, level + 1)
            };
            format!(r#"{{"id": "o{level}", "steps": [{step}]}}"#)
        })
        .collect();
    library.write(
        "Arran/universe.json",
        &format!(r#"{{"leaf": 1, "orders": [{}]}}"#, orders.join(",")),
    );

    let report = library.scan();
    assert!(report.disregarded.is_empty(), "{:?}", report.disregarded);
    assert_eq!(depth as i64, library.count("reading_order"));
    assert!(
        library.count("reading_order_step") <= 16_384,
        "{}",
        library.count("reading_order_step")
    );
    // Said once for the universe, and once for its order count: not once per order cut.
    assert_eq!(
        2,
        report.contradictions.len(),
        "{:?}",
        report.contradictions
    );
    assert!(report
        .contradictions
        .iter()
        .any(|one| one.contains("together")));
    assert!(report
        .contradictions
        .iter()
        .any(|one| one.contains("1024 orders")));
}

/// Two hundred orders each including the one before, plus a step of their own, are each
/// under the ceiling of one order and twenty thousand rows together.
#[test]
fn two_hundred_orders_each_including_the_previous_stay_within_the_universe_budget() {
    let library = Library::new();
    an_universe_with_two_works(&library, "Arran");
    let orders: Vec<String> = (0..200)
        .map(|level| {
            let mut steps = Vec::new();
            if level > 0 {
                steps.push(format!(r#"{{"include": "o{}"}}"#, level - 1));
            }
            steps.push(r#"{"work": "Series B"}"#.to_string());
            format!(r#"{{"id": "o{level}", "steps": [{}]}}"#, steps.join(","))
        })
        .collect();
    library.write(
        "Arran/universe.json",
        &format!(r#"{{"leaf": 1, "orders": [{}]}}"#, orders.join(",")),
    );

    let report = library.scan();
    let rows = library.count("reading_order_step");
    assert!(rows <= 16_384, "{rows}");
    assert_eq!(
        1,
        report.contradictions.len(),
        "{:?}",
        report.contradictions
    );
    assert!(works_of_order(&library, "o199").is_empty());
    // What fits is complete: the first orders are whole.
    assert_eq!(60, works_of_order(&library, "o59").len());
}

/// Forty thousand orders in a file of a few megabytes: each was compared with the ones
/// before it, and each got its rows. The excess is kept without steps, and said once.
#[test]
fn a_universe_with_a_great_many_orders_keeps_the_excess_without_steps_and_says_so_once() {
    let library = Library::new();
    an_universe_with_two_works(&library, "Arran");
    let count = 20_000;
    let orders: Vec<String> = (0..count)
        .map(|n| format!(r#"{{"id":"o{n}","steps":[{{"work":"Series A"}}]}}"#))
        .collect();
    library.write(
        "Arran/universe.json",
        &format!(r#"{{"leaf": 1, "orders": [{}]}}"#, orders.join(",")),
    );

    let report = library.scan();
    assert_eq!(count as i64, library.count("reading_order"));
    assert_eq!(
        leaf_server::scan::orders::MOST_ORDERS as i64,
        library.count("reading_order_step")
    );
    assert_eq!(
        1,
        report.contradictions.len(),
        "{:?}",
        report.contradictions
    );
    assert!(report.contradictions[0].contains("orders"));
}

/// The scan drops the second of two orders under one identifier; an include by name must
/// not read the order the scan dropped.
#[test]
fn an_include_cannot_reach_an_order_dropped_for_sharing_an_identifier() {
    let library = Library::new();
    an_universe_with_two_works(&library, "Arran");
    library.write(
        "Arran/universe.json",
        r#"{"leaf": 1, "orders": [
            {"id": "x", "name": "Alpha", "steps": [{"work": "Series A"}]},
            {"id": "x", "name": "Beta", "steps": [{"work": "Series B"}]},
            {"id": "whole", "steps": [{"include": "Beta"}]}
        ]}"#,
    );

    let report = library.scan();
    assert!(works_of_order(&library, "whole").is_empty());
    assert!(
        report.disregarded.iter().any(|one| one.contains("Beta")),
        "{:?}",
        report.disregarded
    );
}

/// Volume ranges name their edition and chapter ranges must not, because « volumes 1 to 7 »
/// is different content in a 42-volume edition and a 34-volume one.
#[test]
fn a_volume_range_names_its_edition_and_a_chapter_range_may_not() {
    let library = Library::new();
    let deluxe = library.folder("Arran/Series A/Deluxe");
    let original = library.folder("Arran/Series A/Original");
    for folder in [&deluxe, &original] {
        archive(&folder.join("Tome 1.cbz"), 3, None);
        archive(&folder.join("Tome 2.cbz"), 3, None);
    }
    library.write(
        "Arran/universe.json",
        r#"{
          "leaf": 1, "orders": [
            { "id": "main", "steps": [
                { "work": "Series A", "unit": "VOLUME", "edition": "Deluxe",
                  "from": 1, "to": 2 },
                { "work": "Series A", "unit": "VOLUME", "from": 1, "to": 2 },
                { "work": "Series A", "unit": "CHAPTER", "edition": "Deluxe", "from": 1 }
            ]}
          ]
        }"#,
    );

    let report = library.scan();
    assert_eq!(1, library.count("reading_order_step"), "{report:?}");
    assert_eq!(
        Some("Deluxe".to_string()),
        library.one(
            "SELECT e.name FROM reading_order_step s
             JOIN edition e ON e.id = s.edition_id"
        )
    );

    // Both refusals are said out loud, and each says which order and which step.
    assert_eq!(2, report.disregarded.len(), "{:?}", report.disregarded);
    assert!(
        report.disregarded[0].contains("step 2"),
        "{:?}",
        report.disregarded
    );
    assert!(
        report.disregarded[0].contains("VOLUME range must name its edition"),
        "{:?}",
        report.disregarded
    );
    assert!(
        report.disregarded[1].contains("step 3"),
        "{:?}",
        report.disregarded
    );
}

/// « Lanfeust de Troy · Tomes 1 à 4 » was refused for not naming an edition, when the work
/// has one and the edition beside its archives has no name to write.
#[test]
fn a_volume_range_needs_no_edition_when_the_work_has_only_one() {
    let library = Library::new();
    let folder = library.folder("Arran/Lanfeust de Troy");
    for v in 1..=4 {
        archive(&folder.join(format!("Tome {v}.cbz")), 3, None);
    }
    library.write(
        "Arran/universe.json",
        r#"{"leaf": 1, "orders": [{"id": "main", "steps": [
            {"work": "Lanfeust de Troy", "unit": "VOLUME", "from": 1, "to": 4}
        ]}]}"#,
    );

    let report = library.scan();
    assert!(report.disregarded.is_empty(), "{:?}", report.disregarded);
    assert_eq!(1, library.count("reading_order_step"));
    assert_eq!(
        library.one::<String>("SELECT id FROM edition"),
        library.one::<String>("SELECT edition_id FROM reading_order_step")
    );
}

/// The ambiguity the rule protects exists from two editions on: a file that was fine with
/// one must say which the day a second arrives, and not silently pick one of them.
#[test]
fn a_volume_range_without_edition_is_refused_again_once_a_second_edition_arrives() {
    let library = Library::new();
    let folder = library.folder("Arran/Lanfeust de Troy");
    archive(&folder.join("Tome 1.cbz"), 3, None);
    library.write(
        "Arran/universe.json",
        r#"{"leaf": 1, "orders": [{"id": "main", "steps": [
            {"work": "Lanfeust de Troy", "unit": "VOLUME", "from": 1, "to": 4}
        ]}]}"#,
    );
    assert!(library.scan().disregarded.is_empty());
    assert_eq!(1, library.count("reading_order_step"));

    // The archives stay where they are and ONE edition folder joins them: two editions.
    archive(&folder.join("Deluxe").join("Tome 1.cbz"), 3, None);
    let report = library.scan();
    assert_eq!(0, library.count("reading_order_step"));
    assert_eq!(1, report.disregarded.len(), "{:?}", report.disregarded);
    assert!(report.disregarded[0].contains("VOLUME range must name its edition"));
}

/// Moving the archives into an edition folder leaves the work with one edition. The row of
/// the implicit edition is only pruned after the orders are written, and was counted as a
/// second edition: the step vanished until the following scan.
#[test]
fn a_volume_range_follows_the_archives_into_an_edition_folder_at_once() {
    let library = Library::new();
    let folder = library.folder("Arran/Lanfeust de Troy");
    archive(&folder.join("Tome 1.cbz"), 3, None);
    library.write(
        "Arran/universe.json",
        r#"{"leaf": 1, "orders": [{"id": "main", "steps": [
            {"work": "Lanfeust de Troy", "unit": "VOLUME", "from": 1, "to": 4}
        ]}]}"#,
    );
    assert!(library.scan().disregarded.is_empty());

    std::fs::remove_file(folder.join("Tome 1.cbz")).unwrap();
    archive(&folder.join("Deluxe").join("Tome 1.cbz"), 3, None);
    let report = library.scan();
    assert!(report.disregarded.is_empty(), "{:?}", report.disregarded);
    assert_eq!(1, library.count("reading_order_step"));
    assert_eq!(
        Some("Deluxe".to_string()),
        library
            .one("SELECT e.name FROM reading_order_step s JOIN edition e ON e.id = s.edition_id")
    );
}

/// Finding the only edition costs one question per such step and none for any other step:
/// a lookup that ran for every step would cost a large universe a question per step it
/// never needed to ask.
#[test]
fn the_edition_of_a_volume_range_costs_one_question_per_such_step() {
    let cost = |step: &str| {
        let library = Library::new();
        let folder = library.folder("Arran/Lanfeust de Troy");
        archive(&folder.join("Tome 1.cbz"), 3, None);
        let steps = [step; 5].join(",");
        library.write(
            "Arran/universe.json",
            &format!(r#"{{"leaf": 1, "orders": [{{"id": "main", "steps": [{steps}]}}]}}"#),
        );
        let before = library.db.statements();
        library.scan();
        library.db.statements() - before
    };
    let whole = cost(r#"{"work": "Lanfeust de Troy"}"#);
    let ranged = cost(r#"{"work": "Lanfeust de Troy", "unit": "VOLUME", "from": 1}"#);
    assert_eq!(5, ranged - whole);
}

/// A step naming something the universe does not hold is reported and skipped; the steps
/// around it are indexed, and so is everything else in the library.
#[test]
fn a_step_pointing_at_nothing_is_reported_and_the_rest_is_indexed() {
    let library = Library::new();
    an_universe_with_two_works(&library, "Arran");
    library.write(
        "Arran/universe.json",
        r#"{
          "leaf": 1, "defaultOrder": "nowhere", "orders": [
            { "id": "main", "steps": [
                { "work": "Series A" },
                { "work": "Series Z" },
                { "unit": "CHAPTER", "from": 1 },
                { "work": "Series B", "unit": "TOME" },
                { "work": "Series B", "from": 9, "to": 2 },
                { "work": "Series B" }
            ]}
          ]
        }"#,
    );

    let report = library.scan();

    // Two works and their volumes are there whatever the order says.
    assert_eq!(2, library.count("work"));
    assert_eq!(3, library.count("entry"));

    // Two steps survive, and their positions are consecutive: a dropped step leaves no hole
    // for a reader to walk into.
    assert_eq!(2, library.count("reading_order_step"));
    assert_eq!(
        vec!["0".to_string(), "1".to_string()],
        library.all("SELECT CAST(position AS TEXT) FROM reading_order_step ORDER BY position")
    );

    assert_eq!(4, report.disregarded.len(), "{:?}", report.disregarded);
    assert!(report.disregarded[0].contains("\"Series Z\" is not a work"));
    assert!(report.disregarded[1].contains("no work named"));
    assert!(report.disregarded[2].contains("is not a unit"));
    assert!(report.disregarded[3].contains("backwards"));

    // And a default naming an order nobody wrote is a contradiction, not a silence.
    assert!(
        report
            .contradictions
            .iter()
            .any(|one| one.contains("defaultOrder")),
        "{:?}",
        report.contradictions
    );
}

/// Rewritten whole at every scan, because `universe.json` is the truth and these rows are
/// only what a query can reach. Reconciled instead, a removed step would have survived.
#[test]
fn a_rescan_rewrites_the_orders_rather_than_adding_to_them() {
    let library = Library::new();
    an_universe_with_two_works(&library, "Arran");
    library.write(
        "Arran/universe.json",
        r#"{"leaf": 1, "orders": [{ "id": "main", "steps": [
            { "work": "Series A" }, { "work": "Series B" }
        ]}]}"#,
    );
    library.scan();
    assert_eq!(2, library.count("reading_order_step"));

    library.write(
        "Arran/universe.json",
        r#"{"leaf": 1, "orders": [{ "id": "main", "steps": [{ "work": "Series B" }]}]}"#,
    );
    library.scan();
    assert_eq!(1, library.count("reading_order"));
    assert_eq!(1, library.count("reading_order_step"));

    // And a file that stops declaring orders stops having them.
    library.write("Arran/universe.json", r#"{"leaf": 1}"#);
    library.scan();
    assert_eq!(0, library.count("reading_order"));
    assert_eq!(0, library.count("reading_order_step"));
}

/// An universe that goes away takes its orders with it, through the foreign key rather than
/// through a second pass somebody has to remember to write.
#[test]
fn removing_an_universe_removes_the_ways_through_it() {
    let library = Library::new();
    an_universe_with_two_works(&library, "Arran");
    library.write(
        "Arran/universe.json",
        r#"{"leaf": 1, "orders": [{ "id": "main", "steps": [{ "work": "Series A" }]}]}"#,
    );
    library.scan();
    assert_eq!(1, library.count("reading_order_step"));

    std::fs::remove_dir_all(library.dir.path().join("library").join("Arran")).unwrap();
    library.scan();
    assert_eq!(0, library.count("universe"));
    assert_eq!(0, library.count("reading_order"));
    assert_eq!(0, library.count("reading_order_step"));
}

#[test]
fn unnamed_and_duplicate_orders_keep_stable_identifiers_and_report_rejected_steps() {
    let library = Library::new();
    an_universe_with_two_works(&library, "Arran");
    library.write(
        "Arran/universe.json",
        r#"{"leaf": 1, "orders": [
            {"name": "Épopée", "steps": [{"work": "Series A"}]},
            {"id": " ", "name": " ", "steps": [{"work": "Series B"}]},
            {"id": "epopee", "steps": [{"work": "Series B"}]},
            {"id": "missing-edition", "steps": [
                {"work": "Series A", "unit": "VOLUME", "edition": "Absent"}
            ]}
        ]}"#,
    );
    let report = library.scan();
    assert_eq!(library.count("reading_order"), 3);
    assert_eq!(library.count("reading_order_step"), 2);
    assert_eq!(
        library.all("SELECT declared_id FROM reading_order ORDER BY position"),
        vec!["epopee", "2", "missing-edition"]
    );
    assert_eq!(
        library.all("SELECT name FROM reading_order ORDER BY position"),
        vec!["Épopée", "2", "missing-edition"]
    );
    assert!(report
        .contradictions
        .iter()
        .any(|line| line.contains("two orders")));
    assert!(report
        .disregarded
        .iter()
        .any(|line| line.contains("not an edition")));
}

#[test]
fn a_universe_with_unreadable_metadata_still_indexes_its_works_without_orders() {
    let library = Library::new();
    an_universe_with_two_works(&library, "Arran");
    library.write("Arran/universe.json", "{broken");
    let report = library.scan();
    assert_eq!(library.count("universe"), 1);
    assert_eq!(library.count("work"), 2);
    assert_eq!(library.count("reading_order"), 0);
    assert_eq!(report.universes, 1);
    assert_eq!(
        library.one::<String>("SELECT name FROM universe"),
        Some("Arran".into())
    );
}

// ----------------------------------------------------------------- identity
//
// What a folder is called stopped being what identifies it. The tests below are the reason
// that changed and the shape of what replaced it — see `scan::identity`.

/// The defect, and the only test here that would have failed before any of this existed.
///
/// Measured: a volume read to page 42, the folder renamed, the library rescanned, and the
/// place gone. Progress is the one thing a scan does not rebuild, and renaming a folder is
/// less effort than any of the accidents the server already guards against.
#[test]
fn renaming_a_folder_keeps_the_place_a_reader_stopped_at() {
    let library = Library::new();
    archive(&library.folder("Death Note").join("Tome 1.cbz"), 3, None);
    library.scan();
    library.stopped_at(42);

    std::fs::rename(
        library.folder("Death Note"),
        library.folder("").join("Death Note (VF)"),
    )
    .unwrap();
    library.scan();

    assert_eq!(1, library.count("entry"));
    assert_eq!(Some(42), library.place());
    // And the row knows where the folder went. It did not have to before — a folder that
    // moved got a new row — and every route reaching a work by its path would now reach a
    // folder that is not there.
    assert_eq!(
        Some(library.folder("Death Note (VF)").display().to_string()),
        library.one::<String>("SELECT path FROM work")
    );
    assert!(library
        .one::<String>("SELECT file FROM entry")
        .unwrap()
        .contains("Death Note (VF)"));
}

/// The identity is carried by every level, so renaming the folder above works too — the
/// series below it keeps both its own identifier and its reader's place.
#[test]
fn renaming_a_universe_keeps_it_too() {
    let library = Library::new();
    library.write("Arran/universe.json", r#"{"leaf":1,"name":"Arran"}"#);
    archive(&library.folder("Arran/Elfes").join("Tome 1.cbz"), 3, None);
    library.scan();
    library.stopped_at(12);
    let universe = library.one::<String>("SELECT id FROM universe").unwrap();

    std::fs::rename(
        library.folder("Arran"),
        library.folder("").join("Terres d’Arran"),
    )
    .unwrap();
    library.scan();

    assert_eq!(1, library.count("universe"));
    assert_eq!(
        Some(universe),
        library.one::<String>("SELECT id FROM universe"),
        "the universe is the same one, under another name"
    );
    assert_eq!(
        Some(library.folder("Terres d’Arran").display().to_string()),
        library.one::<String>("SELECT path FROM universe")
    );
    assert_eq!(Some(12), library.place());
}

/// The gesture the import asked for, and the reason this document came before it: a work
/// filed under a universe is a work that moved, and moving it used to cost its reader's
/// place.
#[test]
fn moving_a_work_into_a_universe_keeps_it() {
    let library = Library::new();
    archive(&library.folder("Elfes").join("Tome 1.cbz"), 3, None);
    library.scan();
    library.stopped_at(7);
    let work = library.one::<String>("SELECT id FROM work").unwrap();

    library.write("Arran/universe.json", r#"{"leaf":1,"name":"Arran"}"#);
    std::fs::rename(
        library.folder("Elfes"),
        library.folder("Arran").join("Elfes"),
    )
    .unwrap();
    library.scan();

    assert_eq!(1, library.count("work"));
    assert_eq!(Some(work), library.one::<String>("SELECT id FROM work"));
    assert_eq!(Some(7), library.place());
    // And it is a work *of* that universe now, at its new place.
    assert_eq!(
        library.one::<String>("SELECT id FROM universe"),
        library.one::<String>("SELECT universe_id FROM work")
    );
    assert_eq!(
        Some(library.folder("Arran/Elfes").display().to_string()),
        library.one::<String>("SELECT path FROM work")
    );
}

/// A file renamed *and* changed has nothing left to be recognised by: it is another file, and
/// its position is counted as lost rather than hushed.
#[test]
fn a_volume_renamed_and_rewritten_loses_its_place_and_says_so() {
    let library = Library::new();
    let folder = library.folder("Bleach");
    archive(&folder.join("Tome 1.cbz"), 3, None);
    library.scan();
    library.stopped_at(30);

    std::fs::remove_file(folder.join("Tome 1.cbz")).unwrap();
    archive(&folder.join("Tome 01.cbz"), 5, None);
    let report = library.scan();

    assert_eq!(1, library.count("entry"));
    assert_eq!(None, library.place());
    assert_eq!(1, report.progress_lost, "and it is counted, not hushed");
}

/// The stamping: a folder that declares nothing is given an identifier, in a file beside it,
/// once. Twice would mean a library that changes identity every time it is read.
#[test]
fn a_folder_that_declares_nothing_is_given_an_identifier_once() {
    let library = Library::new();
    archive(&library.folder("Bleach").join("Tome 1.cbz"), 3, None);
    library.scan();

    let sidecar = library.folder("Bleach").join("work.json");
    let written: serde_json::Value =
        serde_json::from_slice(&std::fs::read(&sidecar).unwrap()).unwrap();
    let id = written["id"].as_str().expect("an identifier").to_string();
    assert_eq!(16, id.len());
    assert_eq!(
        Some(id.clone()),
        library.one::<String>("SELECT id FROM work")
    );

    library.scan();
    let again: serde_json::Value =
        serde_json::from_slice(&std::fs::read(&sidecar).unwrap()).unwrap();
    assert_eq!(id, again["id"].as_str().unwrap());
    assert_eq!(Some(id), library.one::<String>("SELECT id FROM work"));
}

/// Stamped, never corrected. A sidecar that carries one keeps it whatever it looks like:
/// another installation of Leaf may have written it, and rewriting it would part a library
/// from its own reading positions.
#[test]
fn an_identifier_already_there_is_never_rewritten() {
    let library = Library::new();
    archive(&library.folder("Bleach").join("Tome 1.cbz"), 3, None);
    library.write(
        "Bleach/work.json",
        r#"{"leaf":1,"title":"Bleach","id":"une-autre-installation"}"#,
    );
    library.scan();

    assert_eq!(
        Some("une-autre-installation".to_string()),
        library.one::<String>("SELECT id FROM work")
    );
    let written: serde_json::Value =
        serde_json::from_slice(&std::fs::read(library.folder("Bleach").join("work.json")).unwrap())
            .unwrap();
    assert_eq!("une-autre-installation", written["id"]);
}

/// A `work.json` that cannot be parsed is left exactly as it was on disk. `identity::of`
/// reads it looking for an identifier and used to stamp one on top of it regardless of what
/// it found there, because a parse failure and an empty file looked the same to it.
///
/// Measured: a hand-written `work.json` with a trailing comma — title, authors and genres
/// all present — came back as `{"id":"…","leaf":1}` with every one of those gone, and
/// neither `disregarded` nor `contradictions` said a word. This is the counterpart of
/// `an_identifier_already_there_is_never_rewritten`, on the branch that cannot be read at
/// all rather than the one that disagrees.
#[test]
fn a_sidecar_that_cannot_be_parsed_is_never_overwritten() {
    let library = Library::new();
    archive(&library.folder("Bleach").join("Tome 1.cbz"), 3, None);
    let malformed = r#"{"leaf":1,"title":"Bleach","authors":["Tite Kubo"],"genres":["shonen"],}"#;
    library.write("Bleach/work.json", malformed);

    let report = library.scan();

    assert_eq!(
        malformed,
        std::fs::read_to_string(library.folder("Bleach").join("work.json")).unwrap(),
        "unreadable, so untouched"
    );
    assert_eq!(1, library.count("work"));
    assert!(
        report
            .disregarded
            .iter()
            .any(|one| one.contains("work.json")),
        "{:?}",
        report.disregarded
    );
}

/// Two folders carrying the same identifier is a contradiction, not a choice — and it
/// happens by copying a folder with its sidecar, which is the most ordinary gesture there
/// is. The second one met keeps its path as its identity, and both are named.
#[test]
fn two_folders_carrying_the_same_identifier_are_both_named() {
    let library = Library::new();
    archive(&library.folder("Bleach").join("Tome 1.cbz"), 3, None);
    archive(
        &library.folder("Bleach (copie)").join("Tome 1.cbz"),
        3,
        None,
    );
    for folder in ["Bleach", "Bleach (copie)"] {
        library.write(
            &format!("{folder}/work.json"),
            r#"{"leaf":1,"id":"le-meme-des-deux-cotes"}"#,
        );
    }
    let report = library.scan();

    // Both indexed, because refusing would index the library by halves over a duplicate
    // nobody has seen.
    assert_eq!(2, library.count("work"));
    assert_eq!(
        1,
        library
            .all("SELECT id FROM work")
            .iter()
            .filter(|id| *id == "le-meme-des-deux-cotes")
            .count()
    );
    let said = report.contradictions.join(" ");
    assert!(said.contains("Bleach"), "{:?}", report.contradictions);
    assert!(
        said.contains("le-meme-des-deux-cotes"),
        "{:?}",
        report.contradictions
    );
}

/// A library mounted read-only has to stay readable. The folder keeps the identity it had,
/// it is indexed like any other, and it is said once.
#[cfg(unix)]
#[test]
fn a_folder_that_cannot_be_written_to_is_indexed_anyway_and_said() {
    use std::os::unix::fs::PermissionsExt;

    let library = Library::new();
    let folder = library.folder("Bleach");
    archive(&folder.join("Tome 1.cbz"), 3, None);
    std::fs::set_permissions(&folder, std::fs::Permissions::from_mode(0o555)).unwrap();

    let report = library.scan();
    writable(&folder);

    assert_eq!(1, library.count("work"));
    assert_eq!(1, library.count("entry"));
    assert!(!folder.join("work.json").exists());
    assert!(
        report
            .disregarded
            .iter()
            .any(|one| one.contains("work.json")),
        "{:?}",
        report.disregarded
    );
}

/// An edition implied by its volumes has no folder of its own and so nowhere to keep an
/// identifier. It takes its work's, which a rename no longer changes.
#[test]
fn an_implicit_edition_follows_its_work() {
    let library = Library::new();
    archive(&library.folder("Bleach").join("Tome 1.cbz"), 3, None);
    library.scan();
    let edition = library.one::<String>("SELECT id FROM edition").unwrap();
    assert!(!library.folder("Bleach").join("edition.json").exists());

    std::fs::rename(
        library.folder("Bleach"),
        library.folder("").join("Bleach (VF)"),
    )
    .unwrap();
    library.scan();

    assert_eq!(
        Some(edition),
        library.one::<String>("SELECT id FROM edition")
    );
}

/// The migration, which is the only place in this change where doing nothing would have been
/// better than doing it late: at the first scan after the update every identifier changes at
/// once, and the prune would take every reading position with the rows it replaces.
#[test]
fn the_first_scan_carries_the_places_readers_stopped_at() {
    let library = Library::new();
    archive(&library.folder("Bleach").join("Tome 1.cbz"), 3, None);
    library.scan();
    library.stopped_at(42);
    library.as_it_was_before();

    let report = library.scan();

    assert_eq!(1, report.progress_carried);
    assert_eq!(0, report.progress_lost);
    assert_eq!(Some(42), library.place());
    // And onto the identity the scan gives it now, not left dangling beside it.
    assert_eq!(
        library.one::<String>("SELECT id FROM entry"),
        library.one::<String>("SELECT entry_id FROM progress")
    );
}

/// And a second one has nothing left to carry. Without that the number in the report would
/// be noise, and a scan that keeps rewriting the same rows is a scan doing work twice.
#[test]
fn a_second_scan_has_nothing_left_to_carry() {
    let library = Library::new();
    archive(&library.folder("Bleach").join("Tome 1.cbz"), 3, None);
    library.scan();
    library.stopped_at(42);
    library.as_it_was_before();
    library.scan();

    let report = library.scan();

    assert_eq!(0, report.progress_carried);
    assert_eq!(0, report.progress_lost);
    assert_eq!(Some(42), library.place());
}

/// 190 volumes renamed to fix their titles: a file is « this file, in this edition », so each
/// was a new entry and the prune took its position with the old one. Found again by its size,
/// which a rename does not change.
#[test]
fn a_volume_renamed_keeps_the_place_its_reader_stopped_at() {
    let library = Library::new();
    archive(&library.folder("Bleach").join("Tome 1.cbz"), 3, None);
    library.scan();
    library.stopped_at(42);

    std::fs::rename(
        library.folder("Bleach").join("Tome 1.cbz"),
        library.folder("Bleach").join("Bleach - T01.cbz"),
    )
    .unwrap();
    let report = library.scan();

    assert_eq!(1, library.count("entry"));
    assert_eq!(1, report.progress_carried);
    assert_eq!(0, report.progress_lost);
    assert_eq!(Some(42), library.place());
}

/// Two volumes of the same size renamed together cannot be told apart, and a position carried
/// onto the wrong volume is worse than one reported lost.
#[test]
fn two_volumes_of_one_size_renamed_together_are_not_guessed() {
    let library = Library::new();
    archive(&library.folder("Bleach").join("Tome 1.cbz"), 3, None);
    archive(&library.folder("Bleach").join("Tome 2.cbz"), 3, None);
    library.scan();
    library.stopped_at(42);

    for (from, to) in [("Tome 1.cbz", "A.cbz"), ("Tome 2.cbz", "B.cbz")] {
        std::fs::rename(
            library.folder("Bleach").join(from),
            library.folder("Bleach").join(to),
        )
        .unwrap();
    }
    let report = library.scan();

    assert_eq!(0, report.progress_carried);
    assert_eq!(None, library.place());
}

/// A field nobody here understands survives the stamping — the whole reason a sidecar is
/// edited as a document and never as a type.
#[test]
fn a_field_nobody_here_understands_survives_the_stamping() {
    let library = Library::new();
    archive(&library.folder("Bleach").join("Tome 1.cbz"), 3, None);
    library.write(
        "Bleach/work.json",
        r#"{"leaf":1,"title":"Bleach","monChamp":"gardé ?"}"#,
    );
    library.scan();

    let written: serde_json::Value =
        serde_json::from_slice(&std::fs::read(library.folder("Bleach").join("work.json")).unwrap())
            .unwrap();
    assert_eq!("gardé ?", written["monChamp"]);
    assert_eq!("Bleach", written["title"]);
    assert!(written["id"].is_string());
}

/// The prune fills its temporary table once, and the counting and the deleting both read
/// that one filling.
///
/// A cost, not a speed. The counting used to build `temp.kept`, drop it, and let the delete
/// build the identical set a statement later: two inserts per entry where one does, inside
/// the single transaction the whole prune runs in. On the 57 686 entries `scanner.rs` cites
/// that is 115 372 inserts for 57 686 entries' worth of work, and nothing failed — which is
/// exactly the kind of defect this repository counts statements to see.
#[test]
fn pruning_fills_its_temporary_table_once_and_not_twice() {
    let library = Library::new();
    let folder = library.folder("Bleach");
    for volume in 1..=12 {
        archive(&folder.join(format!("Tome {volume}.cbz")), 4, None);
    }
    library.scan();

    // One volume goes, so the prune has something to remove and cannot take a short way out.
    std::fs::remove_file(folder.join("Tome 12.cbz")).unwrap();

    let before = library.db.statements();
    let second = library.scan();
    let cost = library.db.statements() - before;

    assert_eq!(11, second.entries);
    // Measured both ways, which is the only reason this bound means anything: 91 statements
    // as it stands, 105 with the second filling put back. A bound that both shapes passed
    // would attest nothing at all.
    assert!(
        cost < 100,
        "a prune of eleven kept entries took {cost} statements"
    );
}

/// A rescan aimed at one work counts what it cost a reader, the same as a full sweep does.
///
/// `progress_lost` was filled by `prune` alone, and `prune` runs only at the end of a
/// complete sweep. Every route that touches one work — a commit, a file landing, a patch, a
/// move — aims a rescan instead, and every reading position those dropped went in silence:
/// the report said nought because it had never looked, which reads exactly like nothing
/// having been lost.
#[test]
fn a_rescan_aimed_at_one_work_counts_the_positions_it_drops() {
    let library = Library::new();
    let folder = library.folder("Bleach");
    archive(&folder.join("Tome 1.cbz"), 3, None);
    library.scan();
    library.stopped_at(30);

    std::fs::remove_file(folder.join("Tome 1.cbz")).unwrap();
    let report = Scanner::new(Arc::clone(&library.db), true)
        .rescan_work(&folder)
        .expect("aiming at a work whose only volume is gone");

    assert_eq!(None, library.place());
    assert_eq!(1, report.progress_lost, "and it is counted, not hushed");
}

/// An `id` that is there but is not a string is left exactly as it is.
///
/// `Document::text` answers `None` for it precisely as it does for a field that is not
/// there at all, so the stamping took it for absent and wrote over it — the one case
/// "stamped, never corrected" did not cover, and the one where correcting is least
/// defensible: a field this version does not understand may be one a later version does, or
/// one another installation wrote.
#[test]
fn an_identity_that_is_not_a_string_is_left_alone_and_said() {
    let library = Library::new();
    let folder = library.folder("Bleach");
    archive(&folder.join("Tome 1.cbz"), 2, None);
    std::fs::write(folder.join("work.json"), br#"{"leaf":1,"id":42}"#).unwrap();

    let report = library.scan();

    assert_eq!(
        r#"{"leaf":1,"id":42}"#,
        std::fs::read_to_string(folder.join("work.json")).unwrap(),
        "the file is untouched"
    );
    assert!(
        report
            .contradictions
            .iter()
            .any(|said| said.contains("not a string")),
        "{:?}",
        report.contradictions
    );
}

/// Reading the credits of an edition's volumes one volume at a time would make the list of a
/// hundred-volume series a hundred questions longer than the list of a two-volume one.
#[test]
fn the_entries_of_an_edition_bring_their_credits_in_a_fixed_number_of_questions() {
    let cost = |volumes: usize| {
        let library = Library::new();
        library.write(
            "Série/work.json",
            r#"{"leaf":1,"title":"Série","authors":["A"],"artists":["B"]}"#,
        );
        for n in 1..=volumes {
            archive(
                &library.folder("Série").join(format!("Tome {n}.cbz")),
                2,
                None,
            );
        }
        library.scan();
        let edition = library.one::<String>("SELECT id FROM edition").unwrap();
        let repository = leaf_server::store::Repository::new(&library.db);
        let before = library.db.statements();
        let entries = repository
            .entries(&edition, &Ceiling::NONE)
            .unwrap()
            .expect("the series");
        assert!(entries.iter().all(|e| e.authors == vec!["A".to_string()]));
        library.db.statements() - before
    };
    assert_eq!(cost(2), cost(20));
}

/// The server renames a file and moves `entry.file` in the same step, so the scan that comes
/// next finds the row **by its file**. Before this the identity was « this file, in this
/// edition » and a row found under another name was carried over as `stale`: it worked, but
/// only because the position was moved, and every table hanging off the old id (bookmarks,
/// chosen cover, pages) had to be carried too.
#[test]
fn a_volume_found_by_its_file_keeps_its_identity_whatever_its_name_would_give() {
    let library = Library::new();
    archive(&library.folder("Bleach").join("Tome 1.cbz"), 3, None);
    library.scan();
    library.stopped_at(42);
    let before: String = library.one("SELECT id FROM entry").unwrap();

    let renamed = library.folder("Bleach").join("bleach_tome-001.cbz");
    std::fs::rename(library.folder("Bleach").join("Tome 1.cbz"), &renamed).unwrap();
    library
        .db
        .write(|cx| {
            cx.execute(
                "UPDATE entry SET file = ?1",
                [renamed.to_string_lossy().as_ref()],
            )?;
            Ok(())
        })
        .unwrap();
    let report = library.scan();

    assert_eq!(1, library.count("entry"));
    assert_eq!(Some(before), library.one::<String>("SELECT id FROM entry"));
    assert_eq!(Some(42), library.place());
    assert_eq!(0, report.progress_lost);
}

#[test]
fn a_new_volume_is_given_a_drawn_identity_that_a_second_scan_keeps() {
    let library = Library::new();
    archive(&library.folder("Bleach").join("Tome 1.cbz"), 3, None);
    library.scan();
    let first: String = library.one("SELECT id FROM entry").unwrap();
    let edition: String = library.one("SELECT edition_id FROM entry").unwrap();

    assert_ne!(
        first,
        leaf_server::scan::identity::under(&edition, "Tome 1.cbz"),
        "an identity drawn once, not derived from the name"
    );
    library.scan();
    assert_eq!(Some(first), library.one::<String>("SELECT id FROM entry"));
}

#[test]
fn existing_volumes_keep_the_identity_they_have() {
    let library = Library::new();
    archive(&library.folder("Bleach").join("Tome 1.cbz"), 3, None);
    library.scan();
    let edition: String = library.one("SELECT edition_id FROM entry").unwrap();
    let old = leaf_server::scan::identity::under(&edition, "Tome 1.cbz");
    library
        .db
        .write(|cx| {
            // A row filed under the identity the name gives, as every library indexed before
            // identities were drawn holds it: the children follow the key.
            cx.execute("PRAGMA defer_foreign_keys = ON", [])?;
            cx.execute("UPDATE entry SET id = ?1", [old.as_str()])?;
            for child in ["entry_author", "entry_artist", "chapter", "page"] {
                cx.execute(&format!("UPDATE {child} SET entry_id = ?1"), [old.as_str()])?;
            }
            Ok(())
        })
        .unwrap();
    library.scan();

    assert_eq!(1, library.count("entry"));
    assert_eq!(Some(old), library.one::<String>("SELECT id FROM entry"));
}

/// A rename moves files and rewrites sidecars, and a scan reading the same folders at that
/// moment sees half of it: a volume gone from its old place and not yet at its new one is a
/// volume to prune, with its reading position. The two never run together.
#[test]
fn a_scan_waits_for_the_rename_that_is_running_and_two_renames_do_not_overlap() {
    use std::sync::atomic::{AtomicBool, Ordering};
    use std::sync::mpsc::channel;
    use std::time::Duration;

    let library = Library::new();
    archive(&library.folder("Bleach").join("Tome 1.cbz"), 3, None);
    let root = library.dir.path().join("library");
    let scanner = Arc::new(Scanner::new(Arc::clone(&library.db), true));

    let (entered_tx, entered) = channel();
    let (release, release_rx) = channel::<()>();
    let holder = {
        let scanner = Arc::clone(&scanner);
        std::thread::spawn(move || {
            scanner.exclusive(|_held| {
                entered_tx.send(()).unwrap();
                release_rx.recv().unwrap();
            })
        })
    };
    entered.recv().unwrap();

    let scanned = Arc::new(AtomicBool::new(false));
    let second_rename = Arc::new(AtomicBool::new(false));
    let waiting_scan = {
        let (scanner, scanned, root) = (Arc::clone(&scanner), Arc::clone(&scanned), root.clone());
        std::thread::spawn(move || {
            scanner.scan(&[root]).unwrap();
            scanned.store(true, Ordering::SeqCst);
        })
    };
    let waiting_rename = {
        let (scanner, flag) = (Arc::clone(&scanner), Arc::clone(&second_rename));
        std::thread::spawn(move || scanner.exclusive(|_held| flag.store(true, Ordering::SeqCst)))
    };

    std::thread::sleep(Duration::from_millis(300));
    assert!(
        !scanned.load(Ordering::SeqCst),
        "the scan ran during a rename"
    );
    assert!(
        !second_rename.load(Ordering::SeqCst),
        "two renames overlapped"
    );

    release.send(()).unwrap();
    holder.join().unwrap();
    waiting_scan.join().unwrap();
    waiting_rename.join().unwrap();
    assert!(scanned.load(Ordering::SeqCst) && second_rename.load(Ordering::SeqCst));
    assert_eq!(1, library.count("entry"));
}

/// A folder renamed by hand moves every volume's `file` at once, and the row is no longer
/// found by it. Two volumes of one size cannot be told apart by their size — but they can by
/// their name, which the rename of a folder leaves alone. Before identities were drawn this
/// was exact, the identity being the name: it must not become a guess.
#[test]
fn a_folder_renamed_by_hand_keeps_the_places_even_when_volumes_are_of_one_size() {
    let library = Library::new();
    archive(&library.folder("Bleach").join("Tome 1.cbz"), 3, None);
    archive(&library.folder("Bleach").join("Tome 2.cbz"), 3, None);
    library.scan();
    library.stopped_at(42);
    std::fs::rename(
        library.dir.path().join("library/Bleach"),
        library.dir.path().join("library/Bleach (VF)"),
    )
    .unwrap();
    let report = library.scan();

    assert_eq!(2, library.count("entry"));
    assert_eq!(0, report.progress_lost);
    assert_eq!(2, report.progress_carried);
    assert_eq!(2, library.count("progress"));
}
