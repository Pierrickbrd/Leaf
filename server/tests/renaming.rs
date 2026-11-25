//! Renaming files and folders: what a plan says before it moves anything.

mod common;

use std::io::Write;
use std::path::{Path, PathBuf};
use std::sync::Arc;

use leaf_server::scan::scanner::Scanner;
use leaf_server::store::renaming::{
    execute, execute_until, plan_second_edition, plan_work, resume, FolderKind, Move, Overrides,
    Plan, Refusal,
};
use leaf_server::store::Db;

fn archive(path: &Path, number: u32) {
    std::fs::create_dir_all(path.parent().unwrap()).unwrap();
    let mut zip = zip::ZipWriter::new(std::fs::File::create(path).unwrap());
    let options = zip::write::SimpleFileOptions::default();
    zip.start_file::<_, ()>("000.jpg", options).unwrap();
    zip.write_all(&common::jpeg()).unwrap();
    zip.start_file::<_, ()>("entry.json", options).unwrap();
    zip.write_all(format!(r#"{{"number":{number}}}"#).as_bytes())
        .unwrap();
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
        let db = Arc::new(Db::open(&dir.path().join("index.sqlite")).unwrap());
        Library { dir, db }
    }

    fn path(&self, relative: &str) -> PathBuf {
        self.dir.path().join("library").join(relative)
    }

    fn volume(&self, relative: &str, number: u32) {
        archive(&self.path(relative), number);
    }

    fn write(&self, relative: &str, body: &str) {
        let path = self.path(relative);
        std::fs::create_dir_all(path.parent().unwrap()).unwrap();
        std::fs::write(path, body).unwrap();
    }

    fn scan(&self) {
        Scanner::new(Arc::clone(&self.db), true)
            .scan(&[self.dir.path().join("library")])
            .unwrap();
    }

    fn one(&self, sql: &str) -> String {
        self.db
            .read(|cx| cx.query_one(sql, [], |r| r.get::<_, String>(0)))
            .unwrap()
            .unwrap()
    }

    fn scanner(&self) -> Scanner {
        Scanner::new(Arc::clone(&self.db), true)
    }

    /// A reader stopped here, on every volume there is.
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

    fn files(&self) -> Vec<String> {
        let mut all: Vec<String> = self
            .db
            .read(|cx| cx.query("SELECT file FROM entry", [], |r| r.get::<_, String>(0)))
            .unwrap();
        all.sort();
        all
    }

    fn plan(&self, wanted: &Overrides) -> Result<Plan, Refusal> {
        let work = self.one("SELECT id FROM work");
        self.db
            .read(|cx| plan_work(cx, &work, wanted))
            .expect("planning")
    }
}

fn file_moves(plan: &Plan) -> Vec<(PathBuf, PathBuf)> {
    plan.moves
        .iter()
        .filter_map(|m| match m {
            Move::File { from, to, .. } => Some((from.clone(), to.clone())),
            _ => None,
        })
        .collect()
}

fn retitled(title: &str) -> Overrides {
    Overrides {
        work_name: Some(title.to_string()),
        ..Overrides::default()
    }
}

#[test]
fn a_series_renamed_moves_its_folder_and_its_files() {
    let library = Library::new();
    library.write("Bleach/work.json", r#"{"leaf":1,"title":"Bleach"}"#);
    library.volume("Bleach/Tome 1.cbz", 1);
    library.volume("Bleach/Tome 2.cbz", 2);
    library.scan();

    let plan = library.plan(&retitled("Bleach Deluxe")).expect("a plan");

    assert!(matches!(
        &plan.moves[0],
        Move::Folder { kind: FolderKind::Work, from, to, .. }
            if *from == library.path("Bleach") && *to == library.path("Bleach Deluxe")
    ));
    let mut files = file_moves(&plan);
    files.sort();
    assert_eq!(
        vec![
            (
                library.path("Bleach Deluxe/Tome 1.cbz"),
                library.path("Bleach Deluxe/bleach-deluxe_tome_001.cbz")
            ),
            (
                library.path("Bleach Deluxe/Tome 2.cbz"),
                library.path("Bleach Deluxe/bleach-deluxe_tome_002.cbz")
            ),
        ],
        files
    );
    assert_eq!(vec![library.path("Bleach Deluxe")], plan.works);
}

#[test]
fn a_volume_whose_name_is_made_of_nothing_that_changed_is_left_alone() {
    let library = Library::new();
    library.write("Bleach/work.json", r#"{"leaf":1,"title":"Bleach"}"#);
    library.volume("Bleach/Tome 1.cbz", 1);
    library.volume("Bleach/Tome 2.cbz", 2);
    library.scan();
    let first = library.one("SELECT id FROM entry WHERE volume_number = 1");

    let mut wanted = Overrides::default();
    wanted.numbers.insert(first, Some(7.0));
    let plan = library.plan(&wanted).expect("a plan");

    assert_eq!(
        vec![(
            library.path("Bleach/Tome 1.cbz"),
            library.path("Bleach/bleach_tome_007.cbz")
        )],
        file_moves(&plan)
    );
}

#[test]
fn a_plan_that_collides_moves_nothing() {
    let library = Library::new();
    library.write("Bleach/work.json", r#"{"leaf":1,"title":"Bleach"}"#);
    library.volume("Bleach/Tome 1.cbz", 1);
    library.scan();
    // Somebody else's file, where the first volume would go.
    library.write("Bleach/bleach-deluxe_tome_001.cbz", "not an archive");

    let refused = library.plan(&retitled("Bleach Deluxe")).unwrap_err();

    assert_eq!(
        Refusal::Collision(vec![
            library.path("Bleach Deluxe/bleach-deluxe_tome_001.cbz")
        ]),
        refused
    );
}

#[test]
fn a_folder_that_would_land_on_another_is_a_collision() {
    let library = Library::new();
    library.write("Bleach/work.json", r#"{"leaf":1,"title":"Bleach"}"#);
    library.volume("Bleach/Tome 1.cbz", 1);
    library.scan();
    std::fs::create_dir_all(library.path("Bleach Deluxe")).unwrap();

    let refused = library.plan(&retitled("Bleach Deluxe")).unwrap_err();

    assert_eq!(
        Refusal::Collision(vec![library.path("Bleach Deluxe")]),
        refused
    );
}

#[test]
fn two_volumes_swapping_names_are_planned_through_a_temporary_name() {
    let library = Library::new();
    library.write("Bleach/work.json", r#"{"leaf":1,"title":"Bleach"}"#);
    library.volume("Bleach/bleach_tome_001.cbz", 1);
    library.volume("Bleach/bleach_tome_002.cbz", 2);
    library.scan();
    let one = library.one("SELECT id FROM entry WHERE volume_number = 1");
    let two = library.one("SELECT id FROM entry WHERE volume_number = 2");

    let mut wanted = Overrides::default();
    wanted.numbers.insert(one, Some(2.0));
    wanted.numbers.insert(two, Some(1.0));
    let plan = library.plan(&wanted).expect("a plan");

    let files = file_moves(&plan);
    assert_eq!(3, files.len(), "two renames and one step aside: {files:?}");
    // Replayed on a set of names, no step lands on a name that is still taken.
    let mut disk: std::collections::HashSet<PathBuf> = [
        library.path("Bleach/bleach_tome_001.cbz"),
        library.path("Bleach/bleach_tome_002.cbz"),
    ]
    .into_iter()
    .collect();
    for (from, to) in files {
        assert!(
            disk.remove(&from),
            "{} is not there to move",
            from.display()
        );
        assert!(
            disk.insert(to.clone()),
            "{} would be overwritten",
            to.display()
        );
    }
    assert_eq!(2, disk.len());
}

#[test]
fn a_plan_stamps_the_identity_of_every_folder_it_renames() {
    let library = Library::new();
    library.write("Bleach/work.json", r#"{"leaf":1,"title":"Bleach"}"#);
    library.volume("Bleach/Tome 1.cbz", 1);
    library.scan();
    let id = library.one("SELECT id FROM work");
    // A folder whose identity is only the index's: its sidecar does not say it.
    library.write("Bleach/work.json", r#"{"leaf":1,"title":"Bleach"}"#);

    let plan = library.plan(&retitled("Bleach Deluxe")).expect("a plan");

    assert_eq!(1, plan.rewrites.len());
    // Written where the folder is now, before it moves.
    assert_eq!(library.path("Bleach/work.json"), plan.rewrites[0].path);
    let written = String::from_utf8(plan.rewrites[0].content.clone()).unwrap();
    assert!(written.contains(&id), "{written}");
    assert!(
        written.contains("Bleach"),
        "what the sidecar said stays: {written}"
    );
}

#[test]
fn a_folder_that_already_says_its_identity_needs_no_rewrite() {
    let library = Library::new();
    library.write("Bleach/work.json", r#"{"leaf":1,"title":"Bleach"}"#);
    library.volume("Bleach/Tome 1.cbz", 1);
    library.scan();

    let plan = library.plan(&retitled("Bleach Deluxe")).expect("a plan");

    assert!(plan.rewrites.is_empty());
}

#[test]
fn a_folder_whose_identity_cannot_be_written_refuses_the_plan() {
    let library = Library::new();
    library.write("Bleach/work.json", r#"{"leaf":1,"title":"Bleach"}"#);
    library.volume("Bleach/Tome 1.cbz", 1);
    library.scan();
    library.write("Bleach/work.json", r#"{"leaf":1,"title":"Bleach"}"#);
    common::read_only(&library.path("Bleach"));

    let refused = library.plan(&retitled("Bleach Deluxe"));
    common::writable(&library.path("Bleach"));

    assert_eq!(
        Refusal::CannotStampIdentity(library.path("Bleach/work.json")),
        refused.unwrap_err()
    );
}

#[test]
fn a_read_only_library_refuses_a_rename() {
    let library = Library::new();
    library.write("Bleach/work.json", r#"{"leaf":1,"title":"Bleach"}"#);
    library.volume("Bleach/Tome 1.cbz", 1);
    library.scan();
    common::read_only(&library.path("Bleach"));
    common::read_only(&library.path(""));

    let refused = library.plan(&retitled("Bleach Deluxe"));
    common::writable(&library.path(""));
    common::writable(&library.path("Bleach"));

    assert_eq!(Refusal::ReadOnly, refused.unwrap_err());
}

#[test]
fn planning_costs_two_queries_whatever_the_length_of_the_series() {
    let library = Library::new();
    library.write("Bleach/work.json", r#"{"leaf":1,"title":"Bleach"}"#);
    for n in 1..=40 {
        library.volume(&format!("Bleach/Tome {n}.cbz"), n);
    }
    library.scan();
    assert_eq!(
        40,
        library
            .one("SELECT CAST(COUNT(*) AS TEXT) FROM entry")
            .parse::<i64>()
            .unwrap()
    );

    let before = library.db.statements();
    let plan = library.plan(&retitled("Bleach Deluxe")).expect("a plan");
    let asked = library.db.statements() - before;

    assert_eq!(40, file_moves(&plan).len());
    // One of those is the test's own lookup of the work.
    assert_eq!(3, asked, "statements asked: {asked}");
}

fn bleach(library: &Library, volumes: u32) {
    library.write("Bleach/work.json", r#"{"leaf":1,"title":"Bleach"}"#);
    for n in 1..=volumes {
        library.volume(&format!("Bleach/Tome {n}.cbz"), n);
    }
    library.scan();
}

#[test]
fn a_plan_is_applied_and_the_index_follows() {
    let library = Library::new();
    bleach(&library, 2);
    let ids = library.one("SELECT group_concat(id) FROM (SELECT id FROM entry ORDER BY id)");
    let plan = library.plan(&retitled("Bleach Deluxe")).expect("a plan");

    execute(&library.scanner(), &library.db, &plan).expect("executing");

    assert!(library
        .path("Bleach Deluxe/bleach-deluxe_tome_001.cbz")
        .is_file());
    assert!(library
        .path("Bleach Deluxe/bleach-deluxe_tome_002.cbz")
        .is_file());
    assert!(!library.path("Bleach").exists());
    assert_eq!(
        vec![
            library
                .path("Bleach Deluxe/bleach-deluxe_tome_001.cbz")
                .to_string_lossy()
                .to_string(),
            library
                .path("Bleach Deluxe/bleach-deluxe_tome_002.cbz")
                .to_string_lossy()
                .to_string(),
        ],
        library.files()
    );
    // Same volumes, not new ones.
    assert_eq!(
        ids,
        library.one("SELECT group_concat(id) FROM (SELECT id FROM entry ORDER BY id)")
    );
    assert_eq!(
        "0",
        library.one("SELECT CAST(COUNT(*) AS TEXT) FROM rename_plan WHERE done_at IS NULL")
    );
}

#[test]
fn reading_progress_survives_a_rename() {
    let library = Library::new();
    bleach(&library, 2);
    library.stopped_at(42);
    let before = library.one(
        "SELECT group_concat(entry_id) FROM (SELECT entry_id FROM progress ORDER BY entry_id)",
    );
    let plan = library.plan(&retitled("Bleach Deluxe")).expect("a plan");

    execute(&library.scanner(), &library.db, &plan).expect("executing");

    assert_eq!(
        "2",
        library.one("SELECT CAST(COUNT(*) AS TEXT) FROM progress WHERE page = 42")
    );
    assert_eq!(
        before,
        library.one(
            "SELECT group_concat(entry_id) FROM (SELECT entry_id FROM progress ORDER BY entry_id)"
        )
    );
}

#[test]
fn a_cut_in_the_middle_of_a_plan_is_finished_at_start() {
    let library = Library::new();
    bleach(&library, 3);
    library.stopped_at(42);
    let plan = library.plan(&retitled("Bleach Deluxe")).expect("a plan");

    // Two steps made — the stamp has nothing to say, so the folder and one volume — and the
    // process gone.
    execute_until(&library.scanner(), &library.db, &plan, 2).expect("starting");
    assert!(library.path("Bleach Deluxe").is_dir());
    assert!(library.path("Bleach Deluxe/Tome 3.cbz").exists());

    let resumed = resume(&library.scanner(), &library.db).expect("resuming");

    assert_eq!(1, resumed.finished);
    assert!(resumed.unexpected.is_empty(), "{:?}", resumed.unexpected);
    for n in 1..=3 {
        assert!(library
            .path(&format!("Bleach Deluxe/bleach-deluxe_tome_00{n}.cbz"))
            .is_file());
    }
    assert_eq!(
        "3",
        library.one("SELECT CAST(COUNT(*) AS TEXT) FROM progress WHERE page = 42")
    );
    assert_eq!("3", library.one("SELECT CAST(COUNT(*) AS TEXT) FROM entry"));
    // And a second start has nothing left to do.
    assert_eq!(0, resume(&library.scanner(), &library.db).unwrap().finished);
}

#[test]
fn a_resume_does_not_touch_a_state_it_does_not_expect() {
    let library = Library::new();
    bleach(&library, 2);
    let plan = library.plan(&retitled("Bleach Deluxe")).expect("a plan");
    execute_until(&library.scanner(), &library.db, &plan, 1).expect("starting");
    // Somebody removed the folder the plan was moving: neither end is there.
    std::fs::remove_dir_all(library.path("Bleach Deluxe")).unwrap();

    let resumed = resume(&library.scanner(), &library.db).expect("resuming");

    assert_eq!(0, resumed.finished);
    assert_eq!(1, resumed.unexpected.len(), "{:?}", resumed.unexpected);
    assert!(!library.path("Bleach").exists() && !library.path("Bleach Deluxe").exists());
}

#[test]
fn a_foreign_file_met_at_the_destination_stops_the_plan_and_the_journal_resumes_it() {
    let library = Library::new();
    bleach(&library, 2);
    let plan = library.plan(&retitled("Bleach Deluxe")).expect("a plan");
    // After the plan, before the move: a file appears where volume 1 is going.
    std::fs::write(
        library.path("Bleach/bleach-deluxe_tome_001.cbz"),
        "not mine",
    )
    .unwrap();

    let stopped = execute(&library.scanner(), &library.db, &plan);

    assert!(stopped.is_err());
    // Nothing was written over.
    let kept =
        std::fs::read_to_string(library.path("Bleach Deluxe/bleach-deluxe_tome_001.cbz")).unwrap();
    assert_eq!("not mine", kept);
    assert!(library.path("Bleach Deluxe/Tome 1.cbz").is_file());

    std::fs::remove_file(library.path("Bleach Deluxe/bleach-deluxe_tome_001.cbz")).unwrap();
    let resumed = resume(&library.scanner(), &library.db).expect("resuming");

    assert_eq!(1, resumed.finished);
    assert!(library
        .path("Bleach Deluxe/bleach-deluxe_tome_001.cbz")
        .is_file());
}

/// A plan stopped half way leaves the folder moved and the files not: the index is made to say
/// what the disk holds, or the next plan on the series meets its own half-done one as a
/// collision until a scan.
#[test]
fn a_plan_stopped_half_way_leaves_the_index_saying_what_the_disk_holds() {
    let library = Library::new();
    bleach(&library, 2);
    let plan = library.plan(&retitled("Bleach Deluxe")).expect("a plan");
    std::fs::write(
        library.path("Bleach/bleach-deluxe_tome_001.cbz"),
        "not mine",
    )
    .unwrap();
    let scanner = library.scanner();

    let stopped = scanner.exclusive(|held| {
        leaf_server::store::renaming::execute_held(&scanner, held, &library.db, &plan)
    });

    assert!(stopped.is_err());
    assert!(
        library
            .one("SELECT path FROM work")
            .trim_end_matches('/')
            .ends_with("Bleach Deluxe"),
        "the work is where the disk has it"
    );
}

#[test]
fn a_series_renamed_rewrites_the_reading_orders_that_name_its_folder() {
    let library = Library::new();
    library.write(
        "Larcenet/universe.json",
        r#"{"leaf":1,"name":"Larcenet","orders":[
            {"id":"main","name":"Par ici","steps":[
              {"work":"Retour à la terre"},
              {"work":"Blast","unit":"VOLUME","edition":"Originale"},
              {"work":"Retour à la terre","unit":"CHAPTER","from":3}
            ]}]}"#,
    );
    library.write(
        "Larcenet/Retour à la terre/work.json",
        r#"{"leaf":1,"title":"Retour à la terre"}"#,
    );
    library.volume("Larcenet/Retour à la terre/Tome 1.cbz", 1);
    library.scan();

    let plan = library.plan(&retitled("La vraie terre")).expect("a plan");

    let rewrite = plan
        .rewrites
        .iter()
        .find(|r| r.path == library.path("Larcenet/universe.json"))
        .expect("the universe's orders are rewritten");
    let written: serde_json::Value = serde_json::from_slice(&rewrite.content).unwrap();
    let steps: Vec<&str> = written["orders"][0]["steps"]
        .as_array()
        .unwrap()
        .iter()
        .map(|s| s["work"].as_str().unwrap())
        .collect();
    // Every step that named the folder, and only those.
    assert_eq!(vec!["La vraie terre", "Blast", "La vraie terre"], steps);
    assert_eq!("Par ici", written["orders"][0]["name"]);
    assert_eq!("Larcenet", written["name"]);
}

#[test]
fn a_series_renamed_in_a_universe_with_no_order_about_it_rewrites_no_universe() {
    let library = Library::new();
    library.write(
        "Larcenet/universe.json",
        r#"{"leaf":1,"name":"Larcenet","orders":[{"id":"m","steps":[{"work":"Blast"}]}]}"#,
    );
    library.write(
        "Larcenet/Retour à la terre/work.json",
        r#"{"leaf":1,"title":"Retour à la terre"}"#,
    );
    library.volume("Larcenet/Retour à la terre/Tome 1.cbz", 1);
    library.scan();

    let plan = library.plan(&retitled("La vraie terre")).expect("a plan");

    assert!(plan
        .rewrites
        .iter()
        .all(|r| r.path != library.path("Larcenet/universe.json")));
}

const PUBLISHED: &str = r#"{"leaf":1,"title":"Bleach","authors":["Tite Kubo"],"genres":["Shonen"],
    "publisher":"Glénat","collection":"Shonen","language":"fr","readingDirection":"RIGHT_TO_LEFT",
    "status":"completed","volumeCount":74,"format":"poche","colour":false,"chapterLabel":"Chapitre",
    "coverVolume":1}"#;

fn json(path: &Path) -> serde_json::Value {
    serde_json::from_slice(&std::fs::read(path).unwrap()).unwrap()
}

fn second_edition(library: &Library, first: &str, new: &str) -> Result<Plan, Refusal> {
    let work = library.one("SELECT id FROM work");
    library
        .db
        .read(|cx| plan_second_edition(cx, &work, first, new))
        .expect("planning")
}

fn bleach_with_publication(library: &Library, volumes: u32) {
    library.write("Bleach/work.json", PUBLISHED);
    for n in 1..=volumes {
        library.volume(&format!("Bleach/Tome {n}.cbz"), n);
    }
    library.scan();
}

#[test]
fn a_second_edition_moves_the_files_under_the_first_edition_folder() {
    let library = Library::new();
    bleach_with_publication(&library, 3);
    library.stopped_at(42);
    let plan = second_edition(&library, "Originale", "Perfect").expect("a plan");

    execute(&library.scanner(), &library.db, &plan).expect("executing");

    for n in 1..=3 {
        assert!(
            library
                .path(&format!("Bleach/Originale/bleach_originale_tome_00{n}.cbz"))
                .is_file(),
            "volume {n}"
        );
    }
    assert!(!library.path("Bleach/Tome 1.cbz").exists());
    assert_eq!(
        "3",
        library.one("SELECT CAST(COUNT(*) AS TEXT) FROM progress WHERE page = 42")
    );
    assert_eq!("3", library.one("SELECT CAST(COUNT(*) AS TEXT) FROM entry"));
}

#[test]
fn the_implicit_edition_keeps_its_identity_in_its_new_edition_json() {
    let library = Library::new();
    bleach_with_publication(&library, 2);
    library.stopped_at(10);
    let edition = library.one("SELECT id FROM edition");
    let plan = second_edition(&library, "Originale", "Perfect").expect("a plan");

    execute(&library.scanner(), &library.db, &plan).expect("executing");

    assert_eq!(
        edition,
        json(&library.path("Bleach/Originale/edition.json"))["id"]
            .as_str()
            .unwrap()
    );
    // The reading positions still point at an edition that exists, and it is the same one.
    assert_eq!(edition, library.one("SELECT id FROM edition"));
    assert_eq!(
        "1",
        library.one("SELECT CAST(COUNT(*) AS TEXT) FROM edition")
    );
    assert_eq!(
        "2",
        library.one(
            "SELECT CAST(COUNT(*) AS TEXT) FROM progress p JOIN edition e ON e.id = p.edition_id"
        )
    );
    assert_eq!(
        library.path("Bleach/Originale").to_string_lossy(),
        library.one("SELECT path FROM edition")
    );
}

#[test]
fn work_json_keeps_the_story_and_edition_json_the_publication() {
    let library = Library::new();
    bleach_with_publication(&library, 1);
    let plan = second_edition(&library, "Originale", "Perfect").expect("a plan");

    execute(&library.scanner(), &library.db, &plan).expect("executing");

    let work = json(&library.path("Bleach/work.json"));
    let edition = json(&library.path("Bleach/Originale/edition.json"));
    for kept in ["title", "authors", "genres"] {
        assert!(work.get(kept).is_some(), "work.json lost {kept}");
    }
    for moved in [
        "publisher",
        "collection",
        "language",
        "readingDirection",
        "status",
        "volumeCount",
        "format",
        "colour",
        "chapterLabel",
        "coverVolume",
    ] {
        assert!(work.get(moved).is_none(), "work.json still says {moved}");
        assert!(edition.get(moved).is_some(), "edition.json lacks {moved}");
    }
    assert_eq!("Originale", edition["name"]);
    assert_eq!("Glénat", edition["publisher"]);
    assert_eq!(work["id"], json(&library.path("Bleach/work.json"))["id"]);
}

#[test]
fn a_reading_order_step_gets_the_name_of_the_first_edition() {
    let library = Library::new();
    library.write(
        "Larcenet/universe.json",
        r#"{"leaf":1,"name":"Larcenet","orders":[{"id":"m","steps":[
            {"work":"Blast"},
            {"work":"Bleach","unit":"VOLUME","from":1,"to":2},
            {"work":"Bleach","unit":"CHAPTER","from":3}]}]}"#,
    );
    library.write("Larcenet/Bleach/work.json", PUBLISHED);
    library.volume("Larcenet/Bleach/Tome 1.cbz", 1);
    library.scan();
    let plan = second_edition(&library, "Originale", "Perfect").expect("a plan");

    execute(&library.scanner(), &library.db, &plan).expect("executing");

    let steps = json(&library.path("Larcenet/universe.json"))["orders"][0]["steps"].clone();
    assert_eq!(
        serde_json::Value::Null,
        steps[0]["edition"],
        "another work's step"
    );
    assert_eq!("Originale", steps[1]["edition"], "{steps}");
    assert_eq!(
        serde_json::Value::Null,
        steps[2]["edition"],
        "a chapter step names none"
    );
}

#[test]
fn two_editions_with_the_same_name_are_refused() {
    let library = Library::new();
    bleach_with_publication(&library, 1);

    let refused = second_edition(&library, "Originale", "originale ").unwrap_err();

    assert_eq!(Refusal::SameEditionName("originale".to_string()), refused);
    assert!(library.path("Bleach/Tome 1.cbz").is_file(), "nothing moved");
}

#[test]
fn the_folder_of_the_first_edition_is_not_taken_by_a_stranger() {
    let library = Library::new();
    bleach_with_publication(&library, 1);
    std::fs::create_dir_all(library.path("Bleach/Originale")).unwrap();

    let refused = second_edition(&library, "Originale", "Perfect").unwrap_err();

    assert_eq!(
        Refusal::Collision(vec![library.path("Bleach/Originale")]),
        refused
    );
}

// ------------------------------------------------------------ the reviewer's findings

/// An album is its own work: the file *is* the series, and its identity is made from its
/// path. Moving the file made a new work, and the cascade took the old one's reading with it.
#[test]
fn a_one_shot_is_never_moved_because_it_is_its_own_series() {
    let library = Library::new();
    let at = library.path("BD/Blast.cbz");
    std::fs::create_dir_all(at.parent().unwrap()).unwrap();
    let mut zip = zip::ZipWriter::new(std::fs::File::create(&at).unwrap());
    let options = zip::write::SimpleFileOptions::default();
    zip.start_file::<_, ()>("000.jpg", options).unwrap();
    zip.write_all(&common::jpeg()).unwrap();
    zip.start_file::<_, ()>("oneshot.json", options).unwrap();
    zip.write_all(br#"{"leaf":1,"title":"Blast"}"#).unwrap();
    zip.start_file::<_, ()>("entry.json", options).unwrap();
    zip.write_all(br#"{"number":1}"#).unwrap();
    zip.finish().unwrap();
    library.scan();
    library.stopped_at(42);

    let plan = library
        .plan(&Overrides {
            numbers: [(library.one("SELECT id FROM entry"), Some(2.0))].into(),
            ..Overrides::default()
        })
        .expect("a plan");

    assert!(plan.moves.is_empty(), "{:?}", plan.moves);
    assert!(plan.rewrites.is_empty());
}

#[test]
fn a_title_that_did_not_change_renames_nothing() {
    let library = Library::new();
    library.write(
        "Bleach (2004) [FR]/work.json",
        r#"{"leaf":1,"title":"Bleach"}"#,
    );
    library.volume("Bleach (2004) [FR]/Tome 1.cbz", 1);
    library.scan();

    let plan = library.plan(&retitled("Bleach")).expect("a plan");

    assert!(plan.moves.is_empty(), "{:?}", plan.moves);
}

/// The plan is made before the patch is written and the identity stamped from what the
/// plan read: applied as a whole file it put the old title back over the new one.
#[test]
fn a_stamp_written_late_keeps_what_was_written_in_the_meantime() {
    let library = Library::new();
    library.write("Bleach/work.json", r#"{"leaf":1,"title":"Bleach"}"#);
    library.volume("Bleach/Tome 1.cbz", 1);
    library.scan();
    let work = library.one("SELECT id FROM work");
    // A folder copied with its sidecar: the id it carries is another folder's, or none.
    library.write("Bleach/work.json", r#"{"leaf":1,"title":"Bleach"}"#);
    let plan = library.plan(&retitled("Bleach (VO)")).expect("a plan");

    library.write(
        "Bleach/work.json",
        r#"{"leaf":1,"title":"Bleach (VO)","summary":"Résumé"}"#,
    );
    execute(&library.scanner(), &library.db, &plan).expect("executing");

    let written = json(&library.path("Bleach (VO)/work.json"));
    assert_eq!("Bleach (VO)", written["title"]);
    assert_eq!("Résumé", written["summary"]);
    assert_eq!(work, written["id"].as_str().unwrap());
}

/// Orders name an edition by its folder, and the folder is `folder_name` of the name.
#[test]
fn an_order_step_follows_the_folder_of_an_edition_and_not_its_name() {
    let library = Library::new();
    library.write(
        "Larcenet/universe.json",
        r#"{"leaf":1,"name":"Larcenet","orders":[{"id":"m","steps":[
            {"work":"Blast","unit":"VOLUME","edition":"VF"}]}]}"#,
    );
    library.write("Larcenet/Blast/work.json", r#"{"leaf":1,"title":"Blast"}"#);
    library.write(
        "Larcenet/Blast/VF/edition.json",
        r#"{"leaf":1,"name":"Édition française"}"#,
    );
    library.volume("Larcenet/Blast/VF/Tome 1.cbz", 1);
    library.write(
        "Larcenet/Blast/Originale/edition.json",
        r#"{"leaf":1,"name":"Originale"}"#,
    );
    library.volume("Larcenet/Blast/Originale/Tome 1.cbz", 1);
    library.scan();
    let edition = library.one("SELECT id FROM edition WHERE name = 'Édition française'");

    let plan = library
        .plan(&Overrides {
            edition_names: [(edition, "Perfect/Kanzenban".to_string())].into(),
            ..Overrides::default()
        })
        .expect("a plan");
    execute(&library.scanner(), &library.db, &plan).expect("executing");

    assert!(library.path("Larcenet/Blast/Perfect-Kanzenban").is_dir());
    let steps = json(&library.path("Larcenet/universe.json"))["orders"][0]["steps"].clone();
    assert_eq!("Perfect-Kanzenban", steps[0]["edition"], "{steps}");
}

/// A plan the disk cannot finish must not stop the server from starting.
#[test]
fn a_plan_that_cannot_be_finished_is_reported_and_does_not_stop_the_start() {
    let library = Library::new();
    library.write("Bleach/work.json", r#"{"leaf":1,"title":"Bleach"}"#);
    library.volume("Bleach/Tome 1.cbz", 1);
    library.scan();
    library.write("Bleach/work.json", r#"{"leaf":1,"title":"Bleach"}"#);
    let plan = library.plan(&retitled("Bleach (VO)")).expect("a plan");
    execute_until(&library.scanner(), &library.db, &plan, 0).expect("cut at once");
    // The disk the library sits on is gone: no folder to stamp.
    std::fs::remove_dir_all(library.path("Bleach")).unwrap();

    let resumed = resume(&library.scanner(), &library.db).expect("a start that goes on");

    assert_eq!(0, resumed.finished);
    assert!(!resumed.unexpected.is_empty());
    assert!(
        !library.path("Bleach").exists(),
        "nothing was made in its place"
    );
}
