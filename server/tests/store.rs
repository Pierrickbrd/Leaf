//! The store: what a transaction is, and what a write that meets one is worth.
//!
//! These are the oracle. The server is not "done" when it compiles, it is done when it
//! answers the way these describe.

use std::sync::{Arc, Barrier};
use std::time::Duration;

use leaf_server::store::Db;

fn temp() -> tempfile::TempDir {
    tempfile::tempdir().expect("a temporary directory")
}

fn universes(db: &Db) -> i64 {
    db.read(|cx| {
        Ok(cx
            .query_one("SELECT COUNT(*) FROM universe", [], |r| r.get::<_, i64>(0))?
            .unwrap_or(0))
    })
    .expect("counting universes")
}

fn user_version(path: &std::path::Path) -> i32 {
    let conn = rusqlite::Connection::open(path).expect("opening");
    conn.query_row("PRAGMA user_version", [], |r| r.get(0))
        .expect("reading the version")
}

fn columns(path: &std::path::Path, table: &str) -> Vec<String> {
    let conn = rusqlite::Connection::open(path).expect("opening");
    let mut st = conn
        .prepare(&format!("PRAGMA table_info({table})"))
        .expect("preparing");
    let rows = st
        .query_map([], |r| r.get::<_, String>(1))
        .expect("querying");
    rows.map(|r| r.expect("a column name")).collect()
}

// ---------------------------------------------------------------- migrations

/// The index is rebuildable by rescanning, so migrations matter for exactly one thing: the
/// progress that lives nowhere else. These check they run once, in order, and are not
/// confused with a database that was created already current.
#[test]
fn a_fresh_database_is_created_already_current() {
    let dir = temp();
    let file = dir.path().join("fresh.sqlite");
    drop(Db::open(&file).expect("opening"));

    // The CREATE statements already carry every column, so replaying the migrations on a
    // new database would be work at best and a lie at worst.
    assert_eq!(
        leaf_server::store::schema::schema_version(),
        user_version(&file)
    );
    assert!(columns(&file, "edition").contains(&"reading_direction".to_string()));
}

#[test]
fn an_older_database_is_brought_up_and_marked() {
    let dir = temp();
    let file = dir.path().join("old.sqlite");
    {
        // A database as it stood before the columns existed.
        let conn = rusqlite::Connection::open(&file).expect("opening");
        conn.execute_batch(
            // What the first version really had. The columns the migrations add are
            // missing; the ones that were there from the start — universe_id among them —
            // are not, because a database without those is not an old database, it is a
            // broken one, and the answer to that is to delete the file and scan again.
            "CREATE TABLE work (id TEXT PRIMARY KEY, universe_id TEXT,
                                name TEXT NOT NULL, path TEXT NOT NULL UNIQUE);
             CREATE TABLE edition (id TEXT PRIMARY KEY, work_id TEXT NOT NULL,
                                   name TEXT, path TEXT NOT NULL UNIQUE,
                                   implicit INTEGER NOT NULL DEFAULT 0);
             PRAGMA user_version = 0;",
        )
        .expect("building the old schema");
    }

    drop(Db::open(&file).expect("migrating"));

    assert_eq!(
        leaf_server::store::schema::schema_version(),
        user_version(&file)
    );
    assert!(columns(&file, "edition").contains(&"reading_direction".to_string()));
    // Migration 9 drops a column this old database never had. Tolerated, and only in that
    // exact shape — anything else has to be seen.
    assert!(!columns(&file, "work").contains(&"genres".to_string()));
}

#[test]
fn opening_twice_runs_nothing_a_second_time() {
    let dir = temp();
    let file = dir.path().join("twice.sqlite");
    drop(Db::open(&file).expect("first"));
    let after = user_version(&file);
    drop(Db::open(&file).expect("second"));

    assert_eq!(after, user_version(&file));
    assert_eq!(
        leaf_server::store::schema::schema_version(),
        user_version(&file)
    );
}

#[test]
fn data_survives_the_migration() {
    let dir = temp();
    let file = dir.path().join("data.sqlite");
    {
        let db = Db::open(&file).expect("opening");
        db.write(|cx| {
            cx.execute(
                "INSERT INTO work (id, name, path) VALUES (?1, ?2, ?3)",
                ("w", "Essai", "/tmp/essai"),
            )?;
            cx.execute(
                "INSERT INTO edition (id, work_id, name, path, implicit) VALUES (?1, ?2, ?3, ?4, ?5)",
                ("e", "w", None::<String>, "/tmp/essai", 1),
            )?;
            cx.execute(
                "INSERT INTO entry (id, edition_id, type, file, size, modified_at)
                 VALUES (?1, ?2, ?3, ?4, ?5, ?6)",
                ("x", "e", "VOLUME", "/tmp/essai/t1.cbz", 1_i64, 1_i64),
            )?;
            cx.execute(
                "INSERT INTO progress (entry_id, edition_id, page, finished, updated_at)
                 VALUES (?1, ?2, ?3, ?4, ?5)",
                ("x", "e", 42, 0, 1_i64),
            )?;
            Ok(())
        })
        .expect("writing");
    }

    let db = Db::open(&file).expect("reopening");
    // The one thing a rescan could not bring back.
    let page: i64 = db
        .read(|cx| {
            Ok(cx
                .query_one("SELECT page FROM progress WHERE entry_id = 'x'", [], |r| {
                    r.get::<_, i64>(0)
                })?
                .expect("the record"))
        })
        .expect("reading");
    assert_eq!(42, page);
}

// -------------------------------------------------------------- concurrency

/// What happens when a request arrives while a scan is running.
///
/// One shared connection would put the edit inside the scan's transaction, so a rollback
/// would take it along — silently, after the request had already answered 200. Here the
/// transaction is a value handed to a closure, so there is nothing for an edit to fall into
/// by accident.
#[test]
fn a_write_during_a_long_transaction_survives_that_transaction_failing() {
    let dir = temp();
    let db = Arc::new(Db::open(&dir.path().join("index.sqlite")).expect("opening"));
    db.write(|cx| {
        cx.execute(
            "INSERT INTO universe (id, name, path) VALUES ('u', 'Témoin', '/tmp/u')",
            [],
        )?;
        Ok(())
    })
    .expect("the witness");

    let scan = {
        let db = Arc::clone(&db);
        std::thread::spawn(move || {
            // A scan: a long transaction that then fails and rolls back.
            let _: anyhow::Result<()> = db.write(|cx| {
                cx.execute(
                    "INSERT INTO universe (id, name, path) VALUES ('halfway', 'À moitié', '/tmp/h')",
                    [],
                )?;
                std::thread::sleep(Duration::from_millis(200));
                anyhow::bail!("the scan failed halfway")
            });
        })
    };

    // A request arriving meanwhile: someone edits a series from the application. It waits
    // for the writer rather than joining the scan's transaction, which is the whole point.
    std::thread::sleep(Duration::from_millis(50));
    let edit = {
        let db = Arc::clone(&db);
        std::thread::spawn(move || {
            db.write(|cx| {
                cx.execute(
                    "INSERT INTO universe (id, name, path) VALUES ('v', 'Édité pendant le scan', '/tmp/v')",
                    [],
                )?;
                Ok(())
            })
            .expect("the edit")
        })
    };

    scan.join().expect("the scan thread");
    edit.join().expect("the edit thread");

    // The edit was answered 200 and must still be there; the scan's half-written row must
    // not be.
    assert_eq!(
        2,
        universes(&db),
        "an edit made during a scan must not be undone by that scan"
    );
}

#[test]
fn reads_answer_while_a_transaction_is_open() {
    let dir = temp();
    let db = Arc::new(Db::open(&dir.path().join("index.sqlite")).expect("opening"));
    db.write(|cx| {
        cx.execute(
            "INSERT INTO universe (id, name, path) VALUES ('u', 'Témoin', '/tmp/u')",
            [],
        )?;
        Ok(())
    })
    .expect("the witness");

    let inside = Arc::new(Barrier::new(2));
    let release = Arc::new(Barrier::new(2));

    let scan = {
        let (db, inside, release) = (Arc::clone(&db), Arc::clone(&inside), Arc::clone(&release));
        std::thread::spawn(move || {
            db.write(|cx| {
                cx.execute(
                    "INSERT INTO universe (id, name, path) VALUES ('w', 'En cours', '/tmp/w')",
                    [],
                )?;
                inside.wait();
                release.wait();
                Ok(())
            })
            .expect("the scan")
        })
    };

    inside.wait();
    // Browsing during a scan shows the library as it was, not half of the one being built.
    assert_eq!(
        1,
        universes(&db),
        "a scan in progress must not be visible half-done"
    );
    release.wait();
    scan.join().expect("the scan thread");
    assert_eq!(2, universes(&db));
}

// ------------------------------------------------------------------- search

/// FTS5 has to be compiled into the bundled SQLite, or the search index is a table that
/// cannot be created — and nothing else would say so until the first scan.
#[test]
fn the_search_index_is_a_real_fts5_table() {
    let dir = temp();
    let db = Db::open(&dir.path().join("index.sqlite")).expect("opening");

    let sql: String = db
        .read(|cx| {
            Ok(cx
                .query_one(
                    "SELECT sql FROM sqlite_master WHERE name = 'search'",
                    [],
                    |r| r.get::<_, String>(0),
                )?
                .expect("the search table"))
        })
        .expect("reading");
    assert!(
        sql.to_lowercase().contains("fts5"),
        "search must be FTS5: {sql}"
    );

    // And it has to rank, which is the reason for choosing it: BM25 is what LIKE could not
    // do. Accents are folded by the tokenizer itself.
    db.write(|cx| {
        cx.execute(
            "INSERT INTO search (name, detail, kind, ref, edition_id, entry_id, label)
             VALUES ('L''Attaque des Titans', 'Isayama', 'EDITION', 'e1', 'e1', NULL, 'L''Attaque')",
            [],
        )?;
        Ok(())
    })
    .expect("indexing");

    let hits: Vec<String> = db
        .read(|cx| {
            cx.query(
                "SELECT ref FROM search WHERE search MATCH ? ORDER BY bm25(search, 10.0, 1.0)",
                ["\"titans\"*"],
                |r| r.get::<_, String>(0),
            )
        })
        .expect("searching");
    assert_eq!(vec!["e1".to_string()], hits);
}

// -------------------------------------------------------------------- cost

/// A count does not depend on the machine, the disk or the day, so a test can hold it.
#[test]
fn the_statement_counter_counts_what_is_asked() {
    let dir = temp();
    let db = Db::open(&dir.path().join("index.sqlite")).expect("opening");

    let before = db.statements();
    db.read(|cx| {
        cx.query_one("SELECT 1", [], |r| r.get::<_, i64>(0))?;
        cx.query_one("SELECT 2", [], |r| r.get::<_, i64>(0))?;
        Ok(())
    })
    .expect("reading");

    assert_eq!(2, db.statements() - before);
}

/// There is one writer, and it is a lock: two writes never interleave.
///
/// This is the property the whole of `store::db` is arranged around, and the reason the
/// scanner commits shelf by shelf rather than all at once.
#[test]
fn writes_take_their_turn_one_at_a_time() {
    use std::sync::Arc;
    use std::time::{Duration, Instant};

    let dir = tempfile::tempdir().unwrap();
    let db = Arc::new(Db::open(&dir.path().join("index.sqlite")).unwrap());

    let held = Arc::clone(&db);
    let long = std::thread::spawn(move || {
        held.write(|_| {
            std::thread::sleep(Duration::from_millis(600));
            Ok(())
        })
    });
    std::thread::sleep(Duration::from_millis(50));

    let started = Instant::now();
    db.write(|cx| cx.execute("PRAGMA user_version = 42", []))
        .unwrap();
    let waited = started.elapsed();
    long.join().unwrap().unwrap();

    println!(
        "a short write waited {} ms behind a 600 ms one",
        waited.as_millis()
    );
    assert!(waited.as_millis() > 400, "it really does queue behind it");
}

/// One transaction per shelf folder, not one for the library.
///
/// A single writer means a scan that held it from beginning to end blocked every other
/// write for as long as the scan took. What a phone does constantly is record where it
/// stopped reading, and that is a write.
#[test]
fn a_scan_does_not_hold_the_writer_for_the_whole_library() {
    use std::io::Write;
    use std::sync::Arc;
    use std::time::Instant;

    let dir = tempfile::tempdir().unwrap();
    let library = dir.path().join("library");
    // Twelve shelves, each holding one archive that has to be opened and read.
    for shelf in 0..12 {
        let folder = library.join(format!("Oeuvre {shelf}"));
        std::fs::create_dir_all(&folder).unwrap();
        let mut zip =
            zip::ZipWriter::new(std::fs::File::create(folder.join("Tome 1.cbz")).unwrap());
        for page in 0..40 {
            zip.start_file::<_, ()>(
                format!("{page:03}.jpg"),
                zip::write::SimpleFileOptions::default(),
            )
            .unwrap();
            zip.write_all(&vec![0u8; 4096]).unwrap();
        }
        zip.finish().unwrap();
    }

    let db = Arc::new(Db::open(&dir.path().join("index.sqlite")).unwrap());
    let scanning = Arc::clone(&db);
    let scan = std::thread::spawn(move || {
        leaf_server::scan::scanner::Scanner::new(scanning, true)
            .scan(&[library])
            .unwrap()
    });

    // Whatever the scan is doing, a one-statement write gets a turn between two shelves.
    let mut worst = std::time::Duration::ZERO;
    for _ in 0..40 {
        let started = Instant::now();
        db.write(|cx| cx.execute("PRAGMA user_version = 10", []))
            .unwrap();
        worst = worst.max(started.elapsed());
    }
    let report = scan.join().unwrap();

    assert_eq!(12, report.works, "the scan still did all of it");
    println!("the longest a short write waited: {} ms", worst.as_millis());
    assert!(
        worst.as_millis() < 400,
        "a write waited {} ms — the scan is holding the writer too long",
        worst.as_millis()
    );
}

/// An old database that already ran everything up to 10, brought forward.
#[test]
fn dropping_the_unused_column_and_indexing_the_arcs_reaches_an_old_database() {
    let dir = tempfile::tempdir().unwrap();
    let file = dir.path().join("index.sqlite");
    // The real schema, wound back to where a database that had run everything up to 10
    // would be: the column still there, the index not yet.
    drop(Db::open(&file).unwrap());
    {
        let old = rusqlite::Connection::open(&file).unwrap();
        old.execute_batch(
            "ALTER TABLE entry ADD COLUMN checksum TEXT;
             DROP INDEX IF EXISTS ix_arc_edition;
             PRAGMA user_version = 10;",
        )
        .unwrap();
    }

    let db = Db::open(&file).unwrap();

    let columns: Vec<String> = db
        .read(|cx| {
            cx.query("SELECT name FROM pragma_table_info('entry')", [], |r| {
                r.get(0)
            })
        })
        .unwrap();
    assert!(
        !columns.iter().any(|c| c == "checksum"),
        "a column nothing wrote is gone: {columns:?}"
    );
    let indices: Vec<String> = db
        .read(|cx| {
            cx.query(
                "SELECT name FROM sqlite_master WHERE type = 'index' AND tbl_name = 'arc'",
                [],
                |r| r.get(0),
            )
        })
        .unwrap();
    assert!(indices.iter().any(|i| i == "ix_arc_edition"), "{indices:?}");
}

#[test]
fn the_connection_underneath_is_reachable_for_the_rare_thing_the_wrapper_does_not_do() {
    // It is not counted, which is the reason to keep reaching for it rare — but a backup or
    // a pragma nothing else needs has to be able to get at it.
    let dir = tempfile::tempdir().unwrap();
    let db = Db::open(&dir.path().join("index.sqlite")).unwrap();
    let version: i64 = db
        .read(|cx| {
            let raw = cx.raw();
            Ok(Some(raw.query_row("PRAGMA user_version", [], |r| r.get(0))?))
        })
        .unwrap()
        .unwrap();
    assert!(version >= 0);
}

#[test]
fn a_search_index_of_the_old_shape_is_thrown_away_and_made_again() {
    // It used to be an ordinary table. Recreating it costs nothing — the next scan rebuilds
    // it — so the shape is checked rather than migrated.
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("index.sqlite");
    {
        let db = Db::open(&path).unwrap();
        db.write(|cx| {
            cx.run("DROP TABLE IF EXISTS search")?;
            cx.run("CREATE TABLE search (id TEXT, label TEXT)")?;
            Ok(())
        })
        .unwrap();
    }

    let db = Db::open(&path).unwrap();
    let sql: Option<String> = db
        .read(|cx| {
            cx.query_one(
                "SELECT sql FROM sqlite_master WHERE name = 'search'",
                [],
                |r| r.get::<_, Option<String>>(0),
            )
            .map(Option::flatten)
        })
        .unwrap();
    assert!(
        sql.unwrap_or_default().to_lowercase().contains("fts5"),
        "the old shape must be replaced, not kept"
    );
}
