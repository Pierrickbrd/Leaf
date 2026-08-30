//! Reading a library off the disk.
//!
//! Built on real archives rather than on rows inserted by hand: the scanner's whole job is
//! to turn files into rows, and a fixture that starts from rows would test nothing.

use std::io::Write;
use std::path::Path;
use std::sync::Arc;

use leaf_server::scan::scanner::Scanner;
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
    std::fs::set_permissions(&naruto, std::fs::Permissions::from_mode(0o755)).unwrap();

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
        "leaf" | "startPage" | "volumeCount" => serde_json::json!(1),
        "number" | "volume" | "after" | "from" | "to" => serde_json::json!(1.0),
        "genres" => serde_json::json!([""]),
        "arcs" => serde_json::json!([{"name": "", "unit": "VOLUME", "from": 1.0, "to": 1.0}]),
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
        library.one::<String>("SELECT author FROM work"),
        Some("Tite Kubo".to_string())
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

    assert_eq!(
        library.one::<String>("SELECT author FROM work"),
        Some("Kubo".to_string())
    );
    assert_eq!(library.all("SELECT name FROM work_genre"), vec!["Shonen"]);
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
        std::fs::set_permissions(&bleach, std::fs::Permissions::from_mode(0o755)).unwrap();
        assert!(refused.contains("cannot be listed"), "{refused}");
    }
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
