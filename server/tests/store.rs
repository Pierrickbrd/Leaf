//! The store: what a transaction is, and what a write that meets one is worth.
//!
//! These are the oracle. The server is not "done" when it compiles, it is done when it
//! answers the way these describe.

use std::sync::{Arc, Barrier};
use std::time::Duration;

use leaf_server::store::origins::{self, Origin};
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

fn tables(path: &std::path::Path) -> Vec<String> {
    let conn = rusqlite::Connection::open(path).expect("opening");
    let mut st = conn
        .prepare("SELECT name FROM sqlite_master WHERE type = 'table'")
        .expect("preparing");
    let rows = st
        .query_map([], |r| r.get::<_, String>(0))
        .expect("querying");
    rows.map(|r| r.expect("a table name")).collect()
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
    // The migration that replaces work.author with work_author tolerates the same thing:
    // a database old enough to predate this feature entirely has nothing to carry over,
    // and that is not a failure either.
    assert!(!columns(&file, "work").contains(&"author".to_string()));
    assert!(tables(&file).contains(&"work_author".to_string()));
    assert!(tables(&file).contains(&"work_artist".to_string()));
    assert!(tables(&file).contains(&"work_tag".to_string()));
}

/// A library that had already finished volumes must go on having finished them.
///
/// `times_finished` is backfilled from `finished` for the same reason `added_at` is: a
/// column added later leaves everything before it blank, and here that blank would mean
/// « never read » about every book the reader had actually finished. Progress lives nowhere
/// but the index, so no rescan brings it back.
#[test]
fn a_library_that_already_finished_volumes_keeps_having_finished_them() {
    let dir = temp();
    let file = dir.path().join("read.sqlite");
    {
        // A database at version 21: everything before this migration has run, and progress
        // holds one volume finished and one merely started. Wound back from a current one
        // so the volumes exist: the rebuild of `progress` that came with profiles copies
        // rows under their foreign keys, and a position for a volume that is not there is
        // not one a real index can hold.
        drop(Db::open(&file).expect("creating"));
        let conn = rusqlite::Connection::open(&file).expect("opening");
        conn.execute_batch(
            "DROP TABLE link_code; DROP TABLE device; DROP TABLE progress; DROP TABLE profile;
             CREATE TABLE progress (entry_id TEXT PRIMARY KEY, edition_id TEXT NOT NULL,
                                    page INTEGER NOT NULL,
                                    finished INTEGER NOT NULL DEFAULT 0,
                                    updated_at INTEGER NOT NULL);
             INSERT INTO work (id, name, path) VALUES ('w', 'Essai', '/essai');
             INSERT INTO edition (id, work_id, path, implicit) VALUES ('e', 'w', '/essai', 1);
             INSERT INTO entry (id, edition_id, type, file, size, modified_at)
               VALUES ('done', 'e', 'VOLUME', '/essai/1.cbz', 1, 1);
             INSERT INTO entry (id, edition_id, type, file, size, modified_at)
               VALUES ('midway', 'e', 'VOLUME', '/essai/2.cbz', 1, 1);
             INSERT INTO progress (entry_id, edition_id, page, finished, updated_at)
               VALUES ('done', 'e', 189, 1, 1);
             INSERT INTO progress (entry_id, edition_id, page, finished, updated_at)
               VALUES ('midway', 'e', 40, 0, 1);
             PRAGMA user_version = 21;",
        )
        .expect("building the old schema");
    }

    drop(Db::open(&file).expect("migrating"));

    let conn = rusqlite::Connection::open(&file).expect("opening");
    let counted = |entry: &str| -> i64 {
        conn.query_row(
            "SELECT times_finished FROM progress WHERE entry_id = ?1",
            [entry],
            |r| r.get(0),
        )
        .expect("the row")
    };
    assert_eq!(
        1,
        counted("done"),
        "a volume already finished had been read once"
    );
    assert_eq!(0, counted("midway"), "and one merely started had not");
}

/// The migration this feature actually depends on: not a fresh database, one that already
/// held real names in the column being replaced.
///
/// `work.author` was part of the schema from the very first version, so an existing library
/// has it populated exactly like this — a name for the work that had one, nothing for the
/// one that never carried it. The migration must carry that name into `work_author` before
/// dropping the column, or upgrading a real library would quietly forget every author it
/// already knew.
#[test]
fn an_existing_authors_column_is_carried_into_its_own_table_and_nothing_is_lost() {
    let dir = temp();
    let file = dir.path().join("migrating.sqlite");
    {
        // A database at version 13: every migration before this feature has already run,
        // author is still a column on work, and it holds a real name.
        let conn = rusqlite::Connection::open(&file).expect("opening");
        conn.execute_batch(
            "CREATE TABLE work (id TEXT PRIMARY KEY, universe_id TEXT, name TEXT NOT NULL,
                                 path TEXT NOT NULL UNIQUE, title TEXT, medium TEXT,
                                 author TEXT, status TEXT, reading_direction TEXT,
                                 summary TEXT);
             CREATE TABLE edition (id TEXT PRIMARY KEY, work_id TEXT NOT NULL, name TEXT,
                                    path TEXT NOT NULL UNIQUE,
                                    implicit INTEGER NOT NULL DEFAULT 0);
             INSERT INTO work (id, name, path, author)
               VALUES ('death', 'Death Note', '/death', 'Ōba');
             INSERT INTO work (id, name, path, author)
               VALUES ('arran', 'Elfes', '/arran', NULL);
             PRAGMA user_version = 13;",
        )
        .expect("building a database at version 13");
    }

    let db = Db::open(&file).expect("migrating an existing database");

    let names: Vec<String> = db
        .read(|cx| {
            cx.query(
                "SELECT name FROM work_author WHERE work_id = 'death'",
                [],
                |r| r.get::<_, String>(0),
            )
        })
        .expect("reading work_author");
    assert_eq!(
        vec!["Ōba".to_string()],
        names,
        "the name a real library already held must not be lost"
    );
    assert!(
        !columns(&file, "work").contains(&"author".to_string()),
        "one record of the fact, never two"
    );

    // A work that never had an author gets no row, and the migration does not fail over it.
    let none: Vec<String> = db
        .read(|cx| {
            cx.query(
                "SELECT name FROM work_author WHERE work_id = 'arran'",
                [],
                |r| r.get::<_, String>(0),
            )
        })
        .expect("reading work_author");
    assert!(none.is_empty());
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

// ------------------------------------------------- what the version counts

use leaf_server::store::schema::{
    last_migration_of_old_count, schema_version, LEAF_APPLICATION_ID, MIGRATIONS,
};

fn application_id(path: &std::path::Path) -> i32 {
    let conn = rusqlite::Connection::open(path).expect("opening");
    conn.query_row("PRAGMA application_id", [], |r| r.get(0))
        .expect("reading the application id")
}

/// One position, as every column of it reads: a migration that loses a column loses part
/// of somebody's place.
type Position = (String, String, String, i64, i64, i64, i64);

fn positions(path: &std::path::Path) -> Vec<Position> {
    let conn = rusqlite::Connection::open(path).expect("opening");
    let mut st = conn
        .prepare(
            "SELECT profile_id, entry_id, edition_id, page, finished, times_finished, updated_at
               FROM progress ORDER BY profile_id, entry_id",
        )
        .expect("preparing");
    let rows = st
        .query_map([], |r| {
            Ok((
                r.get(0)?,
                r.get(1)?,
                r.get(2)?,
                r.get(3)?,
                r.get(4)?,
                r.get(5)?,
                r.get(6)?,
            ))
        })
        .expect("querying");
    rows.map(|r| r.expect("a position")).collect()
}

/// The positions [`an_old_scheme_index_at`] plants, as they must read once migrated: every
/// one of them handed to the default profile, nothing else about it changed.
fn planted_positions() -> Vec<Position> {
    (0..6)
        .map(|i| {
            (
                "default".to_string(),
                format!("x{i}"),
                "e".to_string(),
                10 * i + 3,
                i % 2,
                // Read twice, some of them: a count above one is the thing the backfill of
                // migration 22 would flatten if it ran again unguarded.
                if i % 2 == 1 { 2 } else { 0 },
                1_700_000_000_000 + i,
            )
        })
        .collect()
}

/// An index as a release before this change left it, in the old numbering: `user_version`
/// counted entries of the list, and there was no marker.
///
/// It is the shape of the production index this change exists for — the schema of old main
/// (d73ae1c), `progress` keyed by entry alone and none of the tables profiles and devices
/// brought — built from a current index and wound back rather than from a vendored copy of
/// that old schema: every other table is the same, and these four are the difference, with
/// `reading_choice`, which old main never had either. With it, it is that index exactly, as
/// a branch build whose forty-first entry created the table left it.
fn an_old_scheme_index_at(
    entries: i32,
    with_reading_choice: bool,
) -> (tempfile::TempDir, std::path::PathBuf) {
    let dir = temp();
    let file = dir.path().join("old-scheme.sqlite");
    drop(Db::open(&file).expect("creating"));
    let conn = rusqlite::Connection::open(&file).expect("opening");
    conn.execute_batch(
        "DROP TABLE link_code; DROP TABLE device; DROP TABLE closed;
         DROP TABLE progress; DROP TABLE profile; DROP TABLE profile_reading_choice;
         CREATE TABLE progress (
           entry_id    TEXT PRIMARY KEY REFERENCES entry(id) ON DELETE CASCADE,
           edition_id  TEXT NOT NULL REFERENCES edition(id) ON DELETE CASCADE,
           page        INTEGER NOT NULL,
           finished    INTEGER NOT NULL DEFAULT 0,
           updated_at  INTEGER NOT NULL,
           times_finished INTEGER NOT NULL DEFAULT 0);
         CREATE INDEX ix_progress_edition ON progress(edition_id, finished);
         INSERT INTO work (id, name, path, age_rating) VALUES ('w', 'Essai', '/essai', '12+');
         INSERT INTO edition (id, work_id, path, implicit, collection, colour, one_shot, summary)
           VALUES ('e', 'w', '/essai', 1, 'Poche', 1, 0, 'Une édition');",
    )
    .expect("winding back");
    for (_, entry, edition, page, finished, times, updated) in planted_positions() {
        conn.execute(
            "INSERT INTO entry (id, edition_id, type, file, size, modified_at)
             VALUES (?1, ?2, 'VOLUME', '/essai/' || ?1 || '.cbz', 1, 1)",
            (&entry, &edition),
        )
        .expect("an entry");
        conn.execute(
            "INSERT INTO progress (entry_id, edition_id, page, finished, times_finished, updated_at)
             VALUES (?1, ?2, ?3, ?4, ?5, ?6)",
            (&entry, &edition, page, finished, times, updated),
        )
        .expect("a position");
    }
    if with_reading_choice {
        conn.execute_batch(
            "CREATE TABLE IF NOT EXISTS reading_choice (
               edition_id TEXT PRIMARY KEY REFERENCES edition(id) ON DELETE CASCADE,
               reading_direction TEXT NOT NULL);
             INSERT INTO reading_choice (edition_id, reading_direction) VALUES ('e', 'RTL');",
        )
        .expect("the branch build's table");
    }
    conn.execute_batch(&format!(
        "PRAGMA application_id = 0; PRAGMA user_version = {entries};"
    ))
    .expect("stamping in the old numbering");
    (dir, file)
}

fn assert_migrated_to_the_end_with_every_position(file: &std::path::Path) {
    assert_eq!(schema_version(), user_version(file));
    assert_eq!(LEAF_APPLICATION_ID, application_id(file));
    assert_eq!(planted_positions(), positions(file));
    assert!(tables(file).contains(&"closed".to_string()));
    assert!(tables(file).contains(&"device".to_string()));
}

/// The version is a migration number and the list has one entry per migration, so the two
/// can no longer be read apart: a person who read « 41 » on a production index looked for
/// migration 41, and there were thirty.
#[test]
fn the_version_is_the_number_of_the_last_migration() {
    assert_eq!(40, MIGRATIONS.len());
    assert_eq!(40, schema_version());
}

#[test]
fn a_fresh_database_is_stamped_with_the_marker_and_the_last_migration() {
    let dir = temp();
    let file = dir.path().join("fresh.sqlite");
    drop(Db::open(&file).expect("opening"));

    assert_eq!(40, user_version(&file));
    assert_eq!(LEAF_APPLICATION_ID, application_id(&file));
}

/// Frozen: how the old numbering maps onto migrations, pinned at the counts that matter —
/// the last entry of a migration is that migration, and any entry before it is the one
/// before.
#[test]
fn an_old_entry_count_names_the_last_migration_it_completed() {
    assert_eq!(Some(0), last_migration_of_old_count(0));
    assert_eq!(Some(1), last_migration_of_old_count(1));
    assert_eq!(Some(1), last_migration_of_old_count(2));
    assert_eq!(Some(2), last_migration_of_old_count(3));
    assert_eq!(Some(13), last_migration_of_old_count(20));
    assert_eq!(Some(13), last_migration_of_old_count(23));
    assert_eq!(Some(14), last_migration_of_old_count(24));
    assert_eq!(Some(25), last_migration_of_old_count(40));
    assert_eq!(Some(25), last_migration_of_old_count(41));
    assert_eq!(Some(26), last_migration_of_old_count(42));
    assert_eq!(Some(27), last_migration_of_old_count(45));
    assert_eq!(Some(28), last_migration_of_old_count(46));
    assert_eq!(Some(29), last_migration_of_old_count(47));
    // No release ever counted further: guessing what a 48 meant is not a migration.
    assert_eq!(None, last_migration_of_old_count(48));
    assert_eq!(None, last_migration_of_old_count(-1));
}

#[test]
fn an_old_scheme_index_at_40_is_brought_to_29_with_every_position_under_the_default_profile() {
    let (_dir, file) = an_old_scheme_index_at(40, false);

    drop(Db::open(&file).expect("migrating"));

    assert_migrated_to_the_end_with_every_position(&file);
}

/// The production index: 41 in the old numbering, its forty-first entry a branch build's
/// `reading_choice`. Read as entries of main, 41 falls inside migration 26, so 25 is the
/// last complete one and 26 to 29 run on a database that holds a table none of them knows;
/// 30, the reader's, then finds that table already there.
#[test]
fn the_production_index_at_41_keeps_every_position_and_its_reading_choice() {
    let (_dir, file) = an_old_scheme_index_at(41, true);

    drop(Db::open(&file).expect("migrating"));

    assert_migrated_to_the_end_with_every_position(&file);
    let conn = rusqlite::Connection::open(&file).expect("opening");
    let choice: String = conn
        .query_row(
            "SELECT reading_direction FROM profile_reading_choice
             WHERE profile_id = 'default' AND edition_id = 'e'",
            [],
            |r| r.get(0),
        )
        .expect("the reader's choice is still there");
    assert_eq!("RTL", choice);
}

/// Main as it was before this change: 47 entries, every migration run. Recognised as 29 and
/// left alone but for what came after it — migration 6 would empty the search index, and a
/// rebuilt `progress` would hand a second reader's places to the default profile.
#[test]
fn an_old_scheme_index_at_47_is_recognised_as_29_and_only_what_is_new_runs() {
    let dir = temp();
    let file = dir.path().join("main-as-it-was.sqlite");
    drop(Db::open(&file).expect("creating"));
    rusqlite::Connection::open(&file)
        .expect("opening")
        .execute_batch(
            "INSERT INTO work (id, name, path) VALUES ('w', 'Essai', '/essai');
             INSERT INTO edition (id, work_id, path, implicit) VALUES ('e', 'w', '/essai', 1);
             INSERT INTO entry (id, edition_id, type, file, size, modified_at)
               VALUES ('x', 'e', 'VOLUME', '/essai/1.cbz', 1, 1);
             INSERT INTO profile (id, name, name_key, created_at) VALUES ('p2', 'Léa', 'léa', 1);
             INSERT INTO progress (profile_id, entry_id, edition_id, page, finished, times_finished, updated_at)
               VALUES ('default', 'x', 'e', 12, 1, 3, 5), ('p2', 'x', 'e', 40, 0, 0, 6);
             INSERT INTO search_ref (kind, ref, row_id, digest) VALUES ('work', 'w', 1, 'd');
             PRAGMA application_id = 0; PRAGMA user_version = 47;",
        )
        .expect("winding the marker back");

    drop(Db::open(&file).expect("opening"));

    assert_eq!(schema_version(), user_version(&file));
    assert_eq!(LEAF_APPLICATION_ID, application_id(&file));
    assert_eq!(
        vec![
            (
                "default".to_string(),
                "x".to_string(),
                "e".to_string(),
                12,
                1,
                3,
                5
            ),
            (
                "p2".to_string(),
                "x".to_string(),
                "e".to_string(),
                40,
                0,
                0,
                6
            ),
        ],
        positions(&file)
    );
    let conn = rusqlite::Connection::open(&file).expect("opening");
    let kept: i64 = conn
        .query_row("SELECT COUNT(*) FROM search_ref", [], |r| r.get(0))
        .expect("counting");
    assert_eq!(1, kept, "migration 6 must not have run again");
}

/// A server older than its index runs on a schema it does not know: columns it never
/// writes, a table rebuilt under it. It used to start without a word.
#[test]
fn an_index_written_by_a_newer_leaf_is_refused() {
    let dir = temp();
    let file = dir.path().join("newer.sqlite");
    drop(Db::open(&file).expect("creating"));
    rusqlite::Connection::open(&file)
        .expect("opening")
        .execute_batch("PRAGMA user_version = 41;")
        .expect("pretending to be newer");

    let said = match Db::open(&file) {
        Ok(_) => panic!("a newer index must not be opened"),
        Err(e) => format!("{e:#}"),
    };
    assert!(said.contains("written by a newer Leaf"), "{said}");
    assert!(said.contains("must not run on it"), "{said}");
    assert_eq!(41, user_version(&file), "and it is left as it was found");
}

/// No release ever counted past 47 in the old numbering; a count beyond it comes from a
/// build this server cannot know the shape of, and is not guessed at.
#[test]
fn an_old_count_no_release_ever_wrote_is_refused() {
    let dir = temp();
    let file = dir.path().join("unknown.sqlite");
    drop(Db::open(&file).expect("creating"));
    rusqlite::Connection::open(&file)
        .expect("opening")
        .execute_batch("PRAGMA application_id = 0; PRAGMA user_version = 48;")
        .expect("an unknown count");

    assert!(Db::open(&file).is_err());
    assert_eq!(48, user_version(&file), "and it is left as it was found");
}

/// Every migration may meet a database that already holds part or all of it: an old count
/// falling inside a migration runs that migration again, and so does a start that crashed
/// mid-way through one written before it became a single transaction. So each one is run
/// twice in a row, from an index as old as the first version, and the second run must
/// neither fail nor take anything from the positions.
#[test]
fn every_migration_survives_being_run_twice_in_a_row() {
    use leaf_server::store::db::apply_migration;
    use leaf_server::store::schema::{SCHEMA, SEARCH_SCHEMA};

    let dir = temp();
    let file = dir.path().join("twice.sqlite");
    let conn = rusqlite::Connection::open(&file).expect("opening");
    conn.execute_batch(
        "PRAGMA foreign_keys = ON;
         CREATE TABLE work (id TEXT PRIMARY KEY, universe_id TEXT, name TEXT NOT NULL,
                            path TEXT NOT NULL UNIQUE, author TEXT, genres TEXT);
         CREATE TABLE edition (id TEXT PRIMARY KEY, work_id TEXT NOT NULL, name TEXT,
                               path TEXT NOT NULL UNIQUE, implicit INTEGER NOT NULL DEFAULT 0);
         CREATE TABLE entry (id TEXT PRIMARY KEY, edition_id TEXT NOT NULL, type TEXT NOT NULL,
                             file TEXT NOT NULL UNIQUE, size INTEGER NOT NULL,
                             modified_at INTEGER NOT NULL, sort_key REAL,
                             checksum TEXT);
         CREATE TABLE progress (entry_id TEXT PRIMARY KEY REFERENCES entry(id),
                                edition_id TEXT NOT NULL REFERENCES edition(id),
                                page INTEGER NOT NULL, finished INTEGER NOT NULL DEFAULT 0,
                                updated_at INTEGER NOT NULL);
         INSERT INTO work (id, name, path, author) VALUES ('w', 'Essai', '/essai', 'Ōba');
         INSERT INTO edition (id, work_id, path, implicit) VALUES ('e', 'w', '/essai', 1);
         INSERT INTO entry (id, edition_id, type, file, size, modified_at)
           VALUES ('a', 'e', 'VOLUME', '/essai/1.cbz', 1, 1), ('b', 'e', 'VOLUME', '/essai/2.cbz', 1, 1);
         INSERT INTO progress (entry_id, edition_id, page, finished, updated_at)
           VALUES ('a', 'e', 189, 1, 5), ('b', 'e', 40, 0, 6);",
    )
    .expect("the first version's shape");
    // What a start does before migrating anything.
    for statement in SCHEMA.iter().chain(SEARCH_SCHEMA) {
        conn.execute_batch(statement).expect("the schema");
    }

    let has = |table: &str, column: &str| -> bool {
        conn.query_row(
            "SELECT COUNT(*) FROM pragma_table_info(?1) WHERE name = ?2",
            [table, column],
            |r| r.get::<_, i64>(0),
        )
        .expect("reading the shape")
            > 0
    };
    for number in 1..=schema_version() {
        apply_migration(&conn, number)
            .unwrap_or_else(|e| panic!("migration {number}, first run: {e:#}"));
        // Between the two runs, what a reader would do: finish a volume a second time, and
        // read under a second profile once there are profiles to read under.
        if has("progress", "times_finished") {
            conn.execute(
                "UPDATE progress SET times_finished = 3 WHERE entry_id = 'a'",
                [],
            )
            .expect("reading it again");
        }
        if has("progress", "profile_id") {
            conn.execute_batch(
                "INSERT OR IGNORE INTO profile (id, name, name_key, created_at)
                   VALUES ('p2', 'Léa', 'léa', 1);
                 INSERT OR IGNORE INTO progress
                   (profile_id, entry_id, edition_id, page, finished, times_finished, updated_at)
                   VALUES ('p2', 'a', 'e', 7, 0, 0, 9);",
            )
            .expect("a second reader");
        }
        apply_migration(&conn, number)
            .unwrap_or_else(|e| panic!("migration {number}, second run: {e:#}"));

        let page: i64 = conn
            .query_row(
                "SELECT page FROM progress WHERE entry_id = 'a' ORDER BY page DESC",
                [],
                |r| r.get(0),
            )
            .expect("the position is still there");
        assert_eq!(189, page, "after migration {number}");
        if has("progress", "times_finished") {
            let times: i64 = conn
                .query_row(
                    "SELECT MAX(times_finished) FROM progress WHERE entry_id = 'a'",
                    [],
                    |r| r.get(0),
                )
                .expect("the count");
            assert_eq!(3, times, "migration {number} flattened a count of readings");
        }
        if has("progress", "profile_id") {
            let second: i64 = conn
                .query_row(
                    "SELECT COUNT(*) FROM progress WHERE profile_id = 'p2'",
                    [],
                    |r| r.get(0),
                )
                .expect("the second reader");
            assert_eq!(1, second, "migration {number} took a second reader's place");
        }
    }
    let total: i64 = conn
        .query_row("SELECT COUNT(*) FROM progress", [], |r| r.get(0))
        .expect("counting");
    assert_eq!(3, total);
}

// ----------------------------------------------------------------- profiles

fn count(db: &Db, sql: &str) -> i64 {
    db.read(|cx| Ok(cx.query_one(sql, [], |r| r.get::<_, i64>(0))?.unwrap_or(0)))
        .expect("counting")
}

/// A database as the version before profiles left it: the progress table without its
/// owner, and none of the three tables that came with them. Built from a current one so
/// that every other table is real, then wound back.
fn an_index_at_the_version_before_profiles_with(rows: i64) -> (tempfile::TempDir, Db) {
    let (dir, file) = an_unmigrated_index_before_profiles_with(rows);
    let db = Db::open(&file).expect("migrating");
    (dir, db)
}

fn an_unmigrated_index_before_profiles_with(rows: i64) -> (tempfile::TempDir, std::path::PathBuf) {
    let dir = temp();
    let file = dir.path().join("before-profiles.sqlite");
    drop(Db::open(&file).expect("creating"));
    let conn = rusqlite::Connection::open(&file).expect("opening");
    conn.execute_batch(
        "DROP TABLE link_code; DROP TABLE device; DROP TABLE progress; DROP TABLE profile;
         CREATE TABLE progress (
           entry_id TEXT PRIMARY KEY REFERENCES entry(id) ON DELETE CASCADE,
           edition_id TEXT NOT NULL REFERENCES edition(id) ON DELETE CASCADE,
           page INTEGER NOT NULL, finished INTEGER NOT NULL DEFAULT 0,
           times_finished INTEGER NOT NULL DEFAULT 0, updated_at INTEGER NOT NULL);
         CREATE INDEX ix_progress_edition ON progress(edition_id, finished);
         INSERT INTO work (id, name, path) VALUES ('w', 'Essai', '/essai');
         INSERT INTO edition (id, work_id, path, implicit) VALUES ('e', 'w', '/essai', 1);",
    )
    .expect("winding back");
    for i in 0..rows {
        conn.execute(
            "INSERT INTO entry (id, edition_id, type, file, size, modified_at)
             VALUES (?1, 'e', 'VOLUME', ?2, 1, 1)",
            (format!("x{i}"), format!("/essai/{i}.cbz")),
        )
        .expect("an entry");
        conn.execute(
            "INSERT INTO progress (entry_id, edition_id, page, finished, times_finished, updated_at)
             VALUES (?1, 'e', ?2, 1, 1, 7)",
            (format!("x{i}"), i),
        )
        .expect("a position");
    }
    conn.execute_batch("PRAGMA user_version = 25;")
        .expect("stamping");
    (dir, file)
}

/// A rebuild that fails part-way must leave the positions where they were, and the old
/// table as it was: the migrator stamps the version per entry, so a rebuild split in steps
/// leaves `progress_new` behind, or worse, `progress` dropped with its rows stranded. A
/// position whose volume is gone makes the copy fail under the foreign keys.
#[test]
fn a_progress_rebuild_that_fails_late_loses_no_position() {
    let (_dir, file) = an_unmigrated_index_before_profiles_with(16);
    rusqlite::Connection::open(&file)
        .expect("opening")
        .execute_batch(
            "PRAGMA foreign_keys = OFF; INSERT INTO progress (entry_id, edition_id, page, updated_at) VALUES ('ghost', 'e', 1, 1);",
        )
        .expect("planting a position with no volume");

    assert!(Db::open(&file).is_err(), "the migration has to fail");

    let conn = rusqlite::Connection::open(&file).expect("opening");
    let kept: i64 = conn
        .query_row("SELECT COUNT(*) FROM progress", [], |r| r.get(0))
        .expect("the old table is still there");
    assert_eq!(17, kept);
    assert!(!tables(&file).contains(&"progress_new".to_string()));
    // Untouched, not half-done: the next start has to be able to run the rebuild again.
    assert!(!columns(&file, "progress").contains(&"profile_id".to_string()));
}

#[test]
fn a_fresh_index_holds_the_default_profile() {
    let dir = temp();
    let db = Db::open(&dir.path().join("fresh.sqlite")).expect("opening");
    let names: Vec<String> = db
        .read(|cx| cx.query("SELECT id FROM profile", [], |r| r.get(0)))
        .unwrap();
    assert_eq!(vec!["default".to_string()], names);
}

#[test]
fn progress_kept_before_profiles_existed_belongs_to_the_default_profile() {
    // Those rows are not rebuilt by a scan: losing them or leaving them ownerless would
    // cost every reader their place.
    let (_dir, db) = an_index_at_the_version_before_profiles_with(16);
    assert_eq!(
        16,
        count(
            &db,
            "SELECT COUNT(*) FROM progress WHERE profile_id = 'default'"
        )
    );
    assert_eq!(1, count(&db, "SELECT COUNT(*) FROM profile"));
    // The rebuilt table keeps what the old one carried, index included.
    assert_eq!(
        16,
        count(
            &db,
            "SELECT SUM(times_finished) FROM progress WHERE updated_at = 7"
        )
    );
    assert_eq!(
        1,
        count(
            &db,
            "SELECT COUNT(*) FROM sqlite_master WHERE name = 'ix_progress_edition'"
        )
    );
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
    use std::sync::atomic::{AtomicBool, Ordering};
    use std::sync::Arc;

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
    let over = Arc::new(AtomicBool::new(false));
    let raised = Arc::clone(&over);
    let scan = std::thread::spawn(move || {
        let report = leaf_server::scan::scanner::Scanner::new(scanning, true)
            .scan(&[library])
            .unwrap();
        raised.store(true, Ordering::SeqCst);
        report
    });

    // Counted, not timed. This asked how long the slowest write waited and refused past
    // 400 ms, which is a speed — the one thing `store/db.rs` says a test here must not
    // assert, and it failed under coverage on a shared runner for saying it: 1028 ms, on a
    // scan nothing had made slower. Measured on an idle laptop it was already reaching
    // 151 ms of a 270 ms scan, so the margin was never there to begin with.
    //
    // What the test is actually for survives without a clock: a scan that wrapped the whole
    // library in one transaction would let **one** write through — the first, which would
    // block until the scan committed and would find it over. Two turns are the least a scan
    // that lets go between shelves can show, and no single transaction can give them. It
    // asked for twelve, one per shelf, and read 11 on a loaded machine on 24/10/2026: twelve
    // was a count of how fast this scan ran, which is a speed after all.
    let mut turns = 0u32;
    while !over.load(Ordering::SeqCst) && turns < 500 {
        db.write(|cx| cx.execute("PRAGMA user_version = 10", []))
            .unwrap();
        turns += 1;
    }
    let report = scan.join().unwrap();

    assert_eq!(12, report.works, "the scan still did all of it");
    println!("short writes that got a turn while the scan ran: {turns}");
    assert!(
        turns >= 2,
        "only {turns} short write(s) got a turn across twelve shelves — the scan is holding \
         the writer for the whole library"
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
            Ok(Some(
                raw.query_row("PRAGMA user_version", [], |r| r.get(0))?,
            ))
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

#[test]
fn a_migration_that_fails_for_a_real_reason_stops_the_start() {
    // Swallowing it is the danger: a migration that failed for a genuine reason looks
    // exactly like one that had already run, and the server would come up on a half-migrated
    // schema without a word.
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("index.sqlite");
    {
        let db = Db::open(&path).unwrap();
        db.write(|cx| {
            // One entry, so the step that updates every one of them has something to touch.
            cx.execute(
                "INSERT INTO work (id, name, path) VALUES ('w','Essai','/w')",
                [],
            )?;
            cx.execute(
                "INSERT INTO edition (id, work_id, path, implicit) VALUES ('e','w','/w/e',1)",
                [],
            )?;
            cx.execute(
                "INSERT INTO entry (id, edition_id, type, file, size, modified_at, page_count)
                 VALUES ('v1','e','VOLUME','/w/e/Tome 1.cbz',1,1700000000000,1)",
                [],
            )?;
            cx.run(
                "CREATE TRIGGER refuse BEFORE UPDATE ON entry
                 BEGIN SELECT RAISE(ABORT, 'not on my watch'); END",
            )?;
            // Back to before migration 3, whose second statement updates every entry.
            cx.run("PRAGMA user_version = 2")?;
            Ok(())
        })
        .unwrap();
    }

    let said = match Db::open(&path) {
        Ok(_) => panic!("a failing migration must stop the start"),
        Err(e) => format!("{e:#}"),
    };
    assert!(said.contains("migration 3 failed"), "{said}");
    assert!(said.contains("not on my watch"), "{said}");
}

#[test]
fn a_column_a_migration_adds_twice_is_not_a_failure() {
    // The other half of the same rule: "already there" is what a re-run looks like, and it
    // is the one error worth swallowing.
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("index.sqlite");
    {
        let db = Db::open(&path).unwrap();
        db.write(|cx| cx.run("PRAGMA user_version = 0")).unwrap();
    }
    // Every ALTER TABLE ADD COLUMN now meets a column that is already there.
    Db::open(&path).expect("a second start must not be a failure");
}

/// An index that already held a device before closing was recorded was closed the day that
/// device was linked; it must not be found open the day its last device is revoked.
#[test]
fn an_index_that_already_held_a_device_is_stamped_closed_by_the_migration() {
    let dir = temp();
    let file = dir.path().join("before-closing.sqlite");
    drop(Db::open(&file).expect("creating"));
    rusqlite::Connection::open(&file)
        .expect("opening")
        .execute_batch(
            "DROP TABLE closed;
             INSERT INTO device (id, name, name_key, key_digest, linked_at, linked_by)
             VALUES ('d', 'PC du salon', 'pc du salon', x'00', 7, 'terminal');
             PRAGMA user_version = 28;",
        )
        .expect("winding back");

    let db = Db::open(&file).expect("migrating");
    let closed_at: Option<i64> = db
        .read(|cx| cx.query_one("SELECT closed_at FROM closed", [], |r| r.get(0)))
        .expect("reading");
    assert_eq!(Some(7), closed_at);
}

#[test]
fn an_index_with_no_device_is_not_stamped_closed_by_the_migration() {
    let dir = temp();
    let file = dir.path().join("never-linked.sqlite");
    drop(Db::open(&file).expect("creating"));
    rusqlite::Connection::open(&file)
        .expect("opening")
        .execute_batch("DROP TABLE closed; PRAGMA user_version = 28;")
        .expect("winding back");

    let db = Db::open(&file).expect("migrating");
    let rows: Option<i64> = db
        .read(|cx| cx.query_one("SELECT COUNT(*) FROM closed", [], |r| r.get(0)))
        .expect("reading");
    assert_eq!(Some(0), rows);
}

// ------------------------------------------------- the reader's migration, on production

/// The production index exactly as the reader's first build left it: the forty entries of
/// main at d73ae1c, then that build's `reading_choice` as the forty-first, stamped 41 in the
/// old numbering with no marker. Built from a current index wound back to that shape, the
/// way [`an_old_scheme_index_at`] builds one, then `reading_choice` created by the statement
/// that build ran, so the table is the one production holds and not the one this server's
/// schema would have made.
fn the_production_index_with_the_readers_first_build() -> (tempfile::TempDir, std::path::PathBuf) {
    let (dir, file) = an_old_scheme_index_at(41, false);
    rusqlite::Connection::open(&file)
        .expect("opening")
        .execute_batch(
            "CREATE TABLE IF NOT EXISTS reading_choice (
               edition_id        TEXT PRIMARY KEY REFERENCES edition(id) ON DELETE CASCADE,
               reading_direction TEXT NOT NULL
             );
             INSERT INTO reading_choice (edition_id, reading_direction) VALUES ('e', 'VERTICAL');",
        )
        .expect("the reader's first build");
    assert_eq!(41, user_version(&file));
    assert_eq!(0, application_id(&file));
    (dir, file)
}

/// Read as entries of main, 41 falls inside migration 26, so the start resumes after 25:
/// profiles, devices, progress keyed by profile and closing run as they would on any old
/// index, and the reader's own migration, the thirtieth, meets a table already there and
/// leaves it as it found it. Profiles and ages run after it: they were numbered 30 and 31 on
/// their own branch, and a database that already held the reader's table is the proof they
/// had to move.
#[test]
fn an_unmarked_index_at_41_holding_the_readers_table_is_brought_to_the_last_migration() {
    assert!(MIGRATIONS[29][0].contains("CREATE TABLE IF NOT EXISTS reading_choice"));
    let (_dir, file) = the_production_index_with_the_readers_first_build();

    let db = Db::open(&file).expect("migrating");

    assert_eq!(schema_version(), user_version(&file));
    assert_eq!(LEAF_APPLICATION_ID, application_id(&file));
    assert_eq!(
        planted_positions(),
        positions(&file),
        "every place kept before profiles belongs to the default profile, nothing else changed"
    );
    for table in [
        "profile",
        "device",
        "link_code",
        "closed",
        "profile_reading_choice",
    ] {
        assert!(tables(&file).contains(&table.to_string()), "{table}");
    }
    assert_eq!(
        1,
        count(
            &db,
            "SELECT COUNT(*) FROM profile_reading_choice WHERE profile_id = 'default' AND edition_id = 'e' AND reading_direction = 'VERTICAL'"
        ),
        "the reader's choice outlives the migrations that ran under it"
    );
}

fn origin(path: &str, kept: u64, announced: u64, checksum: Option<&str>) -> Origin {
    Origin {
        path: path.to_string(),
        kept_size: kept,
        origin_size: announced,
        origin_checksum: checksum.map(str::to_string),
        rewrite: Some("fingerprint".to_string()),
    }
}

/// A rewritten archive no longer has the size of the file that was announced; without this
/// memory it would be sent again at every re-import.
#[test]
fn an_origin_is_kept_and_found_by_its_path() {
    let dir = temp();
    let db = Db::open(&dir.path().join("origins.sqlite")).expect("opening");
    let kept = origin("Serie/T01.cbz", 900, 1200, Some("abc"));
    origins::record(&db, std::slice::from_ref(&kept)).expect("recording");

    let found = origins::known(&db, &["Serie/T01.cbz".to_string(), "ailleurs".to_string()])
        .expect("asking");
    assert_eq!(1, found.len());
    assert_eq!(Some(&kept), found.get("Serie/T01.cbz"));
}

/// The cost is the assertion, as for `Cx` elsewhere: a re-import asks about every file at once.
#[test]
fn finding_a_hundred_origins_costs_one_query() {
    let dir = temp();
    let db = Db::open(&dir.path().join("cost.sqlite")).expect("opening");
    let all: Vec<Origin> = (0..100)
        .map(|i| origin(&format!("S/T{i:03}.cbz"), 1, 2, None))
        .collect();
    origins::record(&db, &all).expect("recording");
    let paths: Vec<String> = all.iter().map(|o| o.path.clone()).collect();

    let before = db.statements();
    let found = origins::known(&db, &paths).expect("asking");
    assert_eq!(100, found.len());
    assert_eq!(1, db.statements() - before);
}

/// A file rewritten again from a newer origin must not leave the old size behind, or the
/// next import would take the new one for a change.
#[test]
fn recording_an_origin_twice_keeps_the_last() {
    let dir = temp();
    let db = Db::open(&dir.path().join("twice.sqlite")).expect("opening");
    origins::record(&db, &[origin("S/T01.cbz", 1, 2, Some("old"))]).expect("first");
    origins::record(&db, &[origin("S/T01.cbz", 3, 4, None)]).expect("second");

    let found = origins::known(&db, &["S/T01.cbz".to_string()]).expect("asking");
    assert_eq!(
        Some(&origin("S/T01.cbz", 3, 4, None)),
        found.get("S/T01.cbz")
    );
}

/// Nothing asked is nothing queried: an empty `IN ()` is not valid in every SQLite build.
#[test]
fn asking_for_no_origin_asks_nothing() {
    let dir = temp();
    let db = Db::open(&dir.path().join("none.sqlite")).expect("opening");
    let before = db.statements();
    assert!(origins::known(&db, &[]).expect("asking").is_empty());
    assert_eq!(0, db.statements() - before);
}

/// The rewrite asked for is kept with the origin: a corrected `entry.json` sent with the same
/// archive is another request, and an origin that forgot it answered « already there ».
#[test]
fn an_origin_keeps_the_rewrite_it_was_made_with() {
    let dir = temp();
    let db = Db::open(&dir.path().join("rewrite.sqlite")).expect("opening");
    let mut plain = origin("S/T01.cbz", 1, 2, None);
    plain.rewrite = None;
    origins::record(&db, &[origin("S/T02.cbz", 1, 2, None), plain.clone()]).expect("recording");

    let found =
        origins::known(&db, &["S/T01.cbz".to_string(), "S/T02.cbz".to_string()]).expect("asking");
    assert_eq!(Some("fingerprint"), found["S/T02.cbz"].rewrite.as_deref());
    assert_eq!(Some(&plain), found.get("S/T01.cbz"));
}

/// Leaf's own edit of a converted archive changes its size; without following it, the next
/// import took a title corrected in the application for a hand edit and offered to undo it.
/// One statement, whatever the edit.
#[test]
fn an_origin_follows_an_archive_leaf_rewrote_in_one_statement() {
    let dir = temp();
    let db = Db::open(&dir.path().join("follow.sqlite")).expect("opening");
    origins::record(&db, &[origin("S/T01.cbz", 900, 1200, None)]).expect("recording");

    let before = db.statements();
    assert!(origins::follow(&db, "S/T01.cbz", 900, 950).expect("following"));
    assert_eq!(1, db.statements() - before);

    let found = origins::known(&db, &["S/T01.cbz".to_string()]).expect("asking");
    assert_eq!(950, found["S/T01.cbz"].kept_size);
}

/// An archive that was no longer as it was kept had been changed by hand before Leaf touched
/// it, and following it would erase the only sign of that.
#[test]
fn an_origin_does_not_follow_a_file_that_was_no_longer_as_it_was_kept() {
    let dir = temp();
    let db = Db::open(&dir.path().join("hand.sqlite")).expect("opening");
    origins::record(&db, &[origin("S/T01.cbz", 900, 1200, None)]).expect("recording");

    assert!(!origins::follow(&db, "S/T01.cbz", 901, 950).expect("following"));
    assert!(!origins::follow(&db, "S/ailleurs.cbz", 900, 950).expect("following"));

    let found = origins::known(&db, &["S/T01.cbz".to_string()]).expect("asking");
    assert_eq!(900, found["S/T01.cbz"].kept_size);
}

// ------------------------------------------------------------------ what a reimport reads

mod held {
    use std::io::Write;
    use std::path::{Path, PathBuf};
    use std::sync::Arc;

    use leaf_server::scan::scanner::Scanner;
    use leaf_server::store::held::{self, Aim};
    use leaf_server::store::repository::Ceiling;
    use leaf_server::store::Db;

    fn jpeg() -> Vec<u8> {
        let mut out = std::io::Cursor::new(Vec::new());
        image::DynamicImage::ImageRgb8(image::RgbImage::new(8, 8))
            .write_to(&mut out, image::ImageFormat::Jpeg)
            .unwrap();
        out.into_inner()
    }

    fn archive(path: &Path, pages: usize, entry_json: &str) {
        std::fs::create_dir_all(path.parent().unwrap()).unwrap();
        let mut zip = zip::ZipWriter::new(std::fs::File::create(path).unwrap());
        let options = zip::write::SimpleFileOptions::default();
        let page = jpeg();
        for i in 0..pages {
            zip.start_file::<_, ()>(format!("{i:03}.jpg"), options)
                .unwrap();
            zip.write_all(&page).unwrap();
        }
        zip.start_file::<_, ()>("entry.json", options).unwrap();
        zip.write_all(entry_json.as_bytes()).unwrap();
        zip.finish().unwrap();
    }

    struct Library {
        dir: tempfile::TempDir,
        db: Arc<Db>,
    }

    impl Library {
        fn new() -> Self {
            let dir = super::temp();
            std::fs::create_dir_all(dir.path().join("library")).unwrap();
            let db = Arc::new(Db::open(&dir.path().join("index.sqlite")).unwrap());
            Library { dir, db }
        }

        fn at(&self, path: &str) -> PathBuf {
            self.dir.path().join("library").join(path)
        }

        fn write(&self, path: &str, body: &str) {
            let full = self.at(path);
            std::fs::create_dir_all(full.parent().unwrap()).unwrap();
            std::fs::write(full, body).unwrap();
        }

        fn volume(&self, path: &str, number: u32, pages: usize) {
            archive(
                &self.at(path),
                pages,
                &format!(r#"{{"leaf":1,"type":"VOLUME","number":{number}}}"#),
            );
        }

        fn scan(&self) {
            Scanner::new(Arc::clone(&self.db), true)
                .scan(&[self.dir.path().join("library")])
                .expect("scanning");
        }

        fn id(&self, sql: &str) -> String {
            self.db
                .read(|cx| cx.query_one(sql, [], |r| r.get::<_, String>(0)))
                .unwrap()
                .expect("a row")
        }

        fn second_profile(&self) {
            self.db
                .write(|cx| {
                    cx.execute(
                        "INSERT INTO profile (id, name, name_key, created_at)
                         VALUES ('leo', 'Léo', 'leo', 1)",
                        [],
                    )
                })
                .unwrap();
        }
    }

    /// A universe of one series of two volumes, the shape a rail has to draw in full.
    fn aquilon() -> Library {
        let library = Library::new();
        library.write(
            "Monde d'Aquilon/universe.json",
            r#"{"leaf":1,"name":"Monde d'Aquilon"}"#,
        );
        library.write(
            "Monde d'Aquilon/Les Gardiens/work.json",
            r#"{"leaf":1,"title":"Les Gardiens"}"#,
        );
        library.volume("Monde d'Aquilon/Les Gardiens/Tome 1.cbz", 1, 3);
        library.volume("Monde d'Aquilon/Les Gardiens/Tome 2.cbz", 2, 4);
        library.scan();
        library
    }

    #[test]
    fn a_volume_target_is_found_with_its_whole_trail_in_one_query() {
        let library = aquilon();
        let entry = library.id("SELECT id FROM entry WHERE volume_number = 2");

        let before = library.db.statements();
        let place = held::place_of(&library.db, &Aim::Entry(entry.clone()), &Ceiling::NONE)
            .expect("asking")
            .expect("the volume is there");
        assert_eq!(1, library.db.statements() - before);

        let kinds: Vec<&str> = place.trail.iter().map(|s| s.kind).collect();
        assert_eq!(vec!["UNIVERSE", "SERIES", "ENTRY"], kinds);
        assert_eq!("Monde d'Aquilon", place.trail[0].name);
        assert_eq!("Les Gardiens", place.trail[1].name);
        assert_eq!(2, place.trail[1].count, "the series holds two volumes");
        assert_eq!(Some(entry), place.entry_id);
        assert_eq!(library.at("Monde d'Aquilon/Les Gardiens"), place.folder);
        assert_eq!(
            Some(library.at("Monde d'Aquilon/Les Gardiens")),
            place.work_folder
        );
        assert_eq!(1, place.editions.len());
        assert!(place.work_id.is_some() && place.universe_id.is_some());
    }

    #[test]
    fn a_series_and_a_universe_are_found_by_their_own_id() {
        let library = aquilon();
        let series = library.id("SELECT id FROM edition");
        let universe = library.id("SELECT id FROM universe");

        let before = library.db.statements();
        let by_series = held::place_of(&library.db, &Aim::Series(series.clone()), &Ceiling::NONE)
            .unwrap()
            .expect("the series");
        assert_eq!(1, library.db.statements() - before);
        assert_eq!(vec![series], by_series.editions);
        assert_eq!(None, by_series.entry_id);

        let before = library.db.statements();
        let by_universe = held::place_of(&library.db, &Aim::Universe(universe), &Ceiling::NONE)
            .unwrap()
            .expect("the universe");
        assert_eq!(1, library.db.statements() - before);
        assert_eq!(library.at("Monde d'Aquilon"), by_universe.folder);
        assert_eq!(None, by_universe.work_folder);
        assert_eq!(by_series.editions, by_universe.editions);

        assert!(
            held::place_of(&library.db, &Aim::Series("nope".into()), &Ceiling::NONE)
                .unwrap()
                .is_none()
        );
    }

    #[test]
    fn a_series_hidden_from_a_profile_is_no_target_for_it() {
        let library = Library::new();
        library.write(
            "Adulte/work.json",
            r#"{"leaf":1,"title":"Adulte","ageRating":"16+"}"#,
        );
        library.volume("Adulte/Tome 1.cbz", 1, 2);
        library.scan();
        let series = library.id("SELECT id FROM edition");
        let entry = library.id("SELECT id FROM entry");

        let twelve = Ceiling(Some(12));
        assert!(
            held::place_of(&library.db, &Aim::Series(series.clone()), &twelve)
                .unwrap()
                .is_none()
        );
        assert!(held::place_of(&library.db, &Aim::Entry(entry), &twelve)
            .unwrap()
            .is_none());
        assert!(
            held::place_of(&library.db, &Aim::Series(series), &Ceiling(Some(16)))
                .unwrap()
                .is_some()
        );
    }

    #[test]
    fn a_universe_whose_works_are_all_hidden_is_no_target_for_a_child() {
        let library = Library::new();
        library.write("Mondes/universe.json", r#"{"leaf":1,"name":"Mondes"}"#);
        library.write(
            "Mondes/Adulte/work.json",
            r#"{"leaf":1,"title":"Adulte","ageRating":"16+"}"#,
        );
        library.volume("Mondes/Adulte/Tome 1.cbz", 1, 2);
        library.scan();
        let universe = library.id("SELECT id FROM universe");

        assert!(
            held::place_of(&library.db, &Aim::Universe(universe), &Ceiling(Some(12)))
                .unwrap()
                .is_none()
        );
    }

    #[test]
    fn an_empty_universe_is_a_target_for_everyone_alike() {
        let library = Library::new();
        library.write("Vide/universe.json", r#"{"leaf":1,"name":"Vide"}"#);
        library.volume("Vide/Seul/Tome 1.cbz", 1, 1);
        library.scan();
        let universe = library.id("SELECT id FROM universe");
        library
            .db
            .write(|cx| cx.execute("DELETE FROM work", []))
            .unwrap();

        for ceiling in [Ceiling::NONE, Ceiling(Some(12))] {
            let place =
                held::place_of(&library.db, &Aim::Universe(universe.clone()), &ceiling).unwrap();
            assert!(place.is_some_and(|p| p.editions.is_empty()), "{ceiling:?}");
        }
    }

    #[test]
    fn a_chapter_target_carries_its_start_page() {
        let library = Library::new();
        library.write("Haikyu/work.json", r#"{"leaf":1,"title":"Haikyū"}"#);
        archive(
            &library.at("Haikyu/Tome 1.cbz"),
            4,
            r#"{"leaf":1,"type":"VOLUME","number":1,
                "chapters":[{"raw":"1","title":"Le début","startPage":0},
                            {"raw":"2","title":"Le roi","startPage":2}]}"#,
        );
        library.scan();
        let chapter = library.id("SELECT id FROM chapter WHERE number = 2");

        let before = library.db.statements();
        let place = held::place_of(&library.db, &Aim::Chapter(chapter), &Ceiling::NONE)
            .unwrap()
            .expect("the chapter");
        assert_eq!(1, library.db.statements() - before);

        let held_chapter = place.chapter.expect("its row");
        assert_eq!(Some(2), held_chapter.start_page);
        assert_eq!(Some(2.0), held_chapter.number);
        assert_eq!(Some("Le roi".to_string()), held_chapter.title);
        assert!(place.entry_id.is_some(), "and the volume it lives in");
        assert_eq!(Some("CHAPTER"), place.trail.last().map(|s| s.kind));
    }

    #[test]
    fn what_an_edition_holds_is_read_in_one_query_whatever_its_size() {
        let library = Library::new();
        library.write("Longue/work.json", r#"{"leaf":1,"title":"Longue"}"#);
        for n in 1..=200 {
            library.volume(&format!("Longue/Tome {n}.cbz"), n, 1);
        }
        library.scan();
        library.second_profile();
        let edition = library.id("SELECT id FROM edition");
        let read = library.id("SELECT id FROM entry WHERE volume_number = 7");
        library
            .db
            .write(|cx| {
                cx.execute(
                    "INSERT INTO progress (profile_id, entry_id, edition_id, page, finished,
                                           times_finished, updated_at)
                     VALUES ('default', ?1, ?2, 0, 1, 2, 1), ('leo', ?1, ?2, 5, 0, 0, 1)",
                    [&read, &edition],
                )
            })
            .unwrap();

        let before = library.db.statements();
        let all = held::held_in(&library.db, &[edition], "default").expect("reading");
        assert_eq!(1, library.db.statements() - before);

        assert_eq!(200, all.len());
        let seventh = all.iter().find(|h| h.entry_id == read).unwrap();
        assert_eq!(Some(7.0), seventh.number);
        assert_eq!(Some(0), seventh.page);
        assert!(seventh.finished);
        assert_eq!(1, seventh.others, "the other profile has a place in it");
        assert_eq!(1, seventh.pages);
        assert_eq!("VOLUME", seventh.kind);
        assert!(seventh.file.ends_with("Tome 7.cbz"));
        let untouched = all.iter().find(|h| h.number == Some(8.0)).unwrap();
        assert_eq!(
            (None, false, 0),
            (untouched.page, untouched.finished, untouched.others)
        );
    }

    #[test]
    fn a_volume_lists_the_numbers_of_its_chapters() {
        let library = Library::new();
        library.write("Haikyu/work.json", r#"{"leaf":1,"title":"Haikyū"}"#);
        archive(
            &library.at("Haikyu/Tome 1.cbz"),
            4,
            r#"{"leaf":1,"type":"VOLUME","number":1,
                "chapters":[{"raw":"1","startPage":0},{"raw":"2.5","startPage":2},
                            {"raw":"Bonus","after":2}]}"#,
        );
        library.scan();
        let edition = library.id("SELECT id FROM edition");

        let all = held::held_in(&library.db, &[edition], "default").unwrap();

        let mut chapters = all[0].chapters.clone();
        chapters.sort_by(f64::total_cmp);
        assert_eq!(vec![1.0, 2.5], chapters, "a bonus has no number to list");
    }

    #[test]
    fn nothing_held_by_no_edition_costs_no_query() {
        let library = Library::new();
        let before = library.db.statements();
        assert!(held::held_in(&library.db, &[], "default")
            .unwrap()
            .is_empty());
        assert_eq!(0, library.db.statements() - before);
    }

    #[test]
    fn editions_are_found_by_their_folder_in_one_query() {
        let library = Library::new();
        for n in 0..49 {
            library.write(
                &format!("Serie {n}/work.json"),
                &format!(r#"{{"leaf":1,"title":"Serie {n}"}}"#),
            );
            library.volume(&format!("Serie {n}/Tome 1.cbz"), 1, 1);
        }
        library.scan();
        let mut folders: Vec<PathBuf> =
            (0..49).map(|n| library.at(&format!("Serie {n}"))).collect();
        folders.push(library.at("Inconnue"));

        let before = library.db.statements();
        let found = held::editions_at(&library.db, &folders, &Ceiling::NONE).expect("asking");
        assert_eq!(1, library.db.statements() - before);

        assert_eq!(49, found.len());
        assert!(!found.contains_key(&library.at("Inconnue")));
        let first = library.db.read(|cx| {
            cx.query_one(
                "SELECT id FROM edition WHERE path = ?1",
                [library.at("Serie 0").to_string_lossy().to_string()],
                |r| r.get::<_, String>(0),
            )
        });
        assert_eq!(
            first.unwrap().as_ref(),
            found.get(&library.at("Serie 0")).map(|at| &at.id)
        );
    }

    #[test]
    fn a_folder_name_with_a_quote_is_bound_not_spliced() {
        let library = Library::new();
        library.write("L'Arran/work.json", r#"{"leaf":1,"title":"L'Arran"}"#);
        library.volume("L'Arran/Tome 1.cbz", 1, 1);
        library.scan();

        let found = held::editions_at(
            &library.db,
            &[library.at("L'Arran"), library.at("x' OR '1'='1")],
            &Ceiling::NONE,
        )
        .unwrap();

        assert_eq!(1, found.len());
    }

    #[test]
    fn an_edition_names_its_work_and_hides_from_a_profile_that_may_not_see_it() {
        let library = Library::new();
        library.write(
            "One Piece (Glénat)/work.json",
            r#"{"leaf":1,"title":"One Piece"}"#,
        );
        library.volume("One Piece (Glénat)/Tome 1.cbz", 1, 1);
        library.scan();
        let folder = library.at("One Piece (Glénat)");

        let found =
            held::editions_at(&library.db, std::slice::from_ref(&folder), &Ceiling::NONE).unwrap();
        let at = found.get(&folder).expect("the edition");
        assert_eq!("One Piece (Glénat)", at.work_name);
        assert_eq!(Some("One Piece"), at.work_title.as_deref());

        // Nobody rated it: a child of twelve does not see it, so it is no edition to them.
        let hidden = held::editions_at(&library.db, &[folder], &Ceiling(Some(12))).unwrap();
        assert!(hidden.is_empty());
    }

    #[test]
    fn every_work_is_listed_in_one_query() {
        let library = aquilon();
        library.write("Seule/work.json", r#"{"leaf":1,"title":"Seule"}"#);
        library.volume("Seule/Tome 1.cbz", 1, 1);
        library.scan();

        let before = library.db.statements();
        let works = held::works(&library.db, &Ceiling::NONE).expect("listing");
        assert_eq!(1, library.db.statements() - before);

        assert_eq!(2, works.len());
        let gardiens = works.iter().find(|w| w.name == "Les Gardiens").unwrap();
        assert!(gardiens.universe_id.is_some());
        assert_eq!(library.at("Monde d'Aquilon/Les Gardiens"), gardiens.path);
        assert_eq!(Some("Les Gardiens".to_string()), gardiens.title);
        assert!(!gardiens.series_id.is_empty());
        assert_eq!(2, gardiens.volumes);
        let seule = works.iter().find(|w| w.name == "Seule").unwrap();
        assert_eq!(None, seule.universe_id);
        assert_eq!(1, seule.volumes);
    }

    /// Every work is what a reimport compares a new series with and names in what it
    /// answers: a hidden one listed there would name, to a child, a series the library does
    /// not show it.
    /// The volume a chapter is replaced in, and every chapter it holds in reading order, in
    /// one statement whatever their number: the announcement works out each one's new range.
    #[test]
    fn a_volume_is_read_with_its_chapters_in_one_query() {
        let library = Library::new();
        archive(
            &library.at("Seule/Tome 1.cbz"),
            12,
            r#"{"leaf":1,"type":"VOLUME","number":1,
                "chapters":[{"number":2,"startPage":5},{"number":1,"startPage":0}]}"#,
        );
        library.scan();
        let entry = library.id("SELECT id FROM entry");

        let before = library.db.statements();
        let volume = held::chaptered_volume(
            &library.db,
            &entry,
            leaf_server::store::schema::DEFAULT_PROFILE,
        )
        .unwrap()
        .expect("the volume");
        assert_eq!(1, library.db.statements() - before);
        assert_eq!(12, volume.pages);
        assert_eq!(library.at("Seule/Tome 1.cbz"), volume.file);
        assert_eq!(None, volume.page);
        let starts: Vec<Option<i64>> = volume.chapters.iter().map(|c| c.start_page).collect();
        assert_eq!(vec![Some(0), Some(5)], starts);
        assert!(held::chaptered_volume(
            &library.db,
            "nothing",
            leaf_server::store::schema::DEFAULT_PROFILE
        )
        .unwrap()
        .is_none());
    }

    #[test]
    fn a_hidden_work_is_not_listed() {
        let library = aquilon();
        library.write(
            "Adulte/work.json",
            r#"{"leaf":1,"title":"Adulte","ageRating":"16+"}"#,
        );
        library.volume("Adulte/Tome 1.cbz", 1, 2);
        library.scan();

        let before = library.db.statements();
        let works = held::works(&library.db, &Ceiling(Some(12))).unwrap();
        assert_eq!(1, library.db.statements() - before);
        let names: Vec<&str> = works.iter().map(|w| w.name.as_str()).collect();
        assert!(!names.contains(&"Adulte"), "{names:?}");
        assert_eq!(2, held::works(&library.db, &Ceiling::NONE).unwrap().len());
    }

    /// The folder and the name of every edition of a universe, in the one statement that
    /// finds it: a drop's series is compared with an edition it names by folder or by name,
    /// and a second read per series would cost as many statements as the drop has series.
    #[test]
    fn a_universe_target_names_the_folder_of_each_of_its_editions() {
        let library = aquilon();
        let universe = library.id("SELECT id FROM universe");

        let before = library.db.statements();
        let place = held::place_of(&library.db, &Aim::Universe(universe), &Ceiling::NONE)
            .unwrap()
            .unwrap();
        assert_eq!(1, library.db.statements() - before);
        assert_eq!(1, place.edition_folders.len());
        let gardiens = &place.edition_folders[0];
        assert_eq!(library.at("Monde d'Aquilon/Les Gardiens"), gardiens.path);
        assert!(gardiens.implicit);
        assert_eq!(place.editions, vec![gardiens.id.clone()]);
    }

    #[test]
    fn a_work_says_whether_its_folder_declared_an_identity() {
        let library = Library::new();
        library.write("Seule/work.json", r#"{"leaf":1,"title":"Seule"}"#);
        library.volume("Seule/Tome 1.cbz", 1, 1);
        library.scan();
        let works = held::works(&library.db, &Ceiling::NONE).unwrap();
        assert!(
            works[0].identity_declared,
            "the scan stamped one in the sidecar"
        );

        // The identity a folder has only because of where it is: what an unwritable disk
        // leaves, and what a folder copied without its sidecar gets.
        let path = library.at("Sans sidecar");
        let by_place = leaf_server::scan::scanner::id_of(&path, "");
        library
            .db
            .write(|cx| {
                cx.execute(
                    "INSERT INTO work (id, name, path) VALUES (?1, 'Sans sidecar', ?2)",
                    [&by_place, &path.to_string_lossy().to_string()],
                )?;
                cx.execute(
                    "INSERT INTO edition (id, work_id, path, implicit) VALUES ('e', ?1, ?2, 1)",
                    [&by_place, &path.to_string_lossy().to_string()],
                )
            })
            .unwrap();
        let works = held::works(&library.db, &Ceiling::NONE).unwrap();
        let bare = works.iter().find(|w| w.name == "Sans sidecar").unwrap();
        assert!(!bare.identity_declared);
    }
}

// ------------------------------------------------------------------ what a reimport replaced

mod replaced {
    use leaf_server::store::replaced::{self, Positions, Replaced};
    use leaf_server::store::Db;

    fn library() -> (tempfile::TempDir, Db) {
        let dir = super::temp();
        let db = Db::open(&dir.path().join("index.sqlite")).expect("opening");
        db.write(|cx| {
            cx.execute(
                "INSERT INTO work (id, name, path) VALUES ('w', 'W', '/w')",
                [],
            )?;
            cx.execute(
                "INSERT INTO edition (id, work_id, path, implicit) VALUES ('e', 'w', '/w', 1)",
                [],
            )?;
            for entry in ["t1", "t2"] {
                cx.execute(
                    "INSERT INTO entry (id, edition_id, type, file, size, modified_at)
                     VALUES (?1, 'e', 'VOLUME', ?2, 10, 1)",
                    [entry, &format!("/w/{entry}.cbz")],
                )?;
            }
            Ok(())
        })
        .expect("seeding");
        (dir, db)
    }

    /// Each profile of `pages` at its own page of `entry`, finished twice over.
    fn read_at(db: &Db, entry: &str, pages: &[(&str, i64)]) {
        db.write(|cx| {
            for (profile, page) in pages {
                cx.execute(
                    "INSERT OR IGNORE INTO profile (id, name, name_key, created_at)
                     VALUES (?1, ?1, ?1, 1)",
                    [profile],
                )?;
                cx.execute(
                    "INSERT INTO progress (profile_id, entry_id, edition_id, page, finished,
                                           times_finished, updated_at)
                     VALUES (?1, ?2, 'e', ?3, 1, 2, 7)",
                    rusqlite::params![profile, entry, page],
                )?;
            }
            Ok(())
        })
        .expect("reading");
    }

    fn pages_of(db: &Db, entry: &str) -> Vec<(String, i64)> {
        db.read(|cx| {
            cx.query(
                "SELECT profile_id, page FROM progress WHERE entry_id = ?1 ORDER BY profile_id",
                [entry],
                |r| Ok((r.get(0)?, r.get(1)?)),
            )
        })
        .expect("asking")
    }

    fn done(import: &str, path: &str, entry: &str) -> Replaced {
        Replaced {
            import_id: import.to_string(),
            path: path.to_string(),
            entry_id: entry.to_string(),
            kept_at: format!("replaced/{import}/{path}"),
            replaced_at: 1_790_000_000_000,
            profile_id: "default".to_string(),
            old_size: 1200,
            new_size: 1500,
            old_pages: Some(30),
            new_pages: None,
        }
    }

    #[test]
    fn a_replacement_is_recorded_and_found_by_its_entry() {
        let (_dir, db) = library();

        replaced::replace(
            &db,
            &done("imp_a", "w/t1.cbz", "t1"),
            &Positions::Kept { last: 29 },
        )
        .expect("replacing");

        let found = replaced::of_entry(&db, "t1").expect("asking");
        assert_eq!(vec![done("imp_a", "w/t1.cbz", "t1")], found);
        assert!(replaced::of_entry(&db, "t2").unwrap().is_empty());
    }

    #[test]
    fn a_replacement_outlives_the_entry_it_replaced() {
        let (_dir, db) = library();
        replaced::replace(
            &db,
            &done("imp_a", "w/t1.cbz", "t1"),
            &Positions::Kept { last: 29 },
        )
        .unwrap();

        db.write(|cx| cx.execute("DELETE FROM entry WHERE id = 't1'", []))
            .unwrap();

        assert_eq!(1, replaced::of_entry(&db, "t1").unwrap().len());
    }

    #[test]
    fn recording_the_same_replacement_twice_keeps_one_line() {
        let (_dir, db) = library();
        let first = done("imp_a", "w/t1.cbz", "t1");
        let mut again = first.clone();
        again.new_size = 1600;

        replaced::replace(&db, &first, &Positions::Kept { last: 29 }).unwrap();
        replaced::replace(&db, &again, &Positions::Kept { last: 29 }).unwrap();

        let found = replaced::of_entry(&db, "t1").unwrap();
        assert_eq!(
            1,
            found.len(),
            "a second commit of one session doubles nothing"
        );
        assert_eq!(1600, found[0].new_size);
    }

    #[test]
    fn two_sessions_replacing_one_volume_are_two_lines() {
        let (_dir, db) = library();

        replaced::replace(
            &db,
            &done("imp_a", "w/t1.cbz", "t1"),
            &Positions::Kept { last: 29 },
        )
        .unwrap();
        replaced::replace(
            &db,
            &done("imp_b", "w/t1.cbz", "t1"),
            &Positions::Kept { last: 29 },
        )
        .unwrap();

        assert_eq!(2, replaced::of_entry(&db, "t1").unwrap().len());
    }

    #[test]
    fn replacing_costs_the_same_statements_whatever_the_readers() {
        let (_dir, db) = library();
        read_at(&db, "t1", &[("default", 10)]);
        read_at(
            &db,
            "t2",
            &[
                ("default", 10),
                ("p1", 20),
                ("p2", 30),
                ("p3", 40),
                ("p4", 50),
            ],
        );
        let splice = Positions::Spliced {
            from: 5,
            old_to: 15,
            new_to: 20,
            last: 99,
        };

        let before = db.statements();
        replaced::replace(&db, &done("imp_a", "w/t1.cbz", "t1"), &splice).unwrap();
        let one = db.statements() - before;
        let before = db.statements();
        replaced::replace(&db, &done("imp_a", "w/t2.cbz", "t2"), &splice).unwrap();
        let five = db.statements() - before;

        // The record and the places: two statements for one reader or for five.
        assert_eq!(2, one);
        assert_eq!(2, five);
        let reset = Positions::Reset {
            profile: "p1".into(),
            last: 99,
        };
        let before = db.statements();
        replaced::replace(&db, &done("imp_b", "w/t2.cbz", "t2"), &reset).unwrap();
        let reset_cost = db.statements() - before;
        let kept = Positions::Kept { last: 99 };
        let before = db.statements();
        replaced::replace(&db, &done("imp_c", "w/t2.cbz", "t2"), &kept).unwrap();
        assert_eq!(reset_cost, db.statements() - before);
    }

    #[test]
    fn a_reset_returns_only_its_own_profile_to_the_first_page() {
        let (_dir, db) = library();
        read_at(&db, "t1", &[("default", 112), ("p1", 40)]);

        replaced::replace(
            &db,
            &done("imp_a", "w/t1.cbz", "t1"),
            &Positions::Reset {
                profile: "default".into(),
                last: 200,
            },
        )
        .unwrap();

        assert_eq!(
            vec![("default".to_string(), 0), ("p1".to_string(), 40)],
            pages_of(&db, "t1"),
            "the other reader keeps the page they were on"
        );
    }

    #[test]
    fn a_reset_still_brings_the_others_back_under_the_new_last_page() {
        let (_dir, db) = library();
        read_at(&db, "t1", &[("default", 112), ("p1", 90), ("p2", 20)]);

        replaced::replace(
            &db,
            &done("imp_a", "w/t1.cbz", "t1"),
            &Positions::Reset {
                profile: "default".into(),
                last: 50,
            },
        )
        .unwrap();

        assert_eq!(
            vec![
                ("default".to_string(), 0),
                ("p1".to_string(), 50),
                ("p2".to_string(), 20)
            ],
            pages_of(&db, "t1")
        );
    }

    #[test]
    fn no_replacement_ever_clears_what_was_finished() {
        for positions in [
            Positions::Kept { last: 3 },
            Positions::Reset {
                profile: "default".into(),
                last: 3,
            },
            Positions::Spliced {
                from: 0,
                old_to: 100,
                new_to: 2,
                last: 3,
            },
        ] {
            let (_dir, db) = library();
            read_at(&db, "t1", &[("default", 112), ("p1", 8)]);

            replaced::replace(&db, &done("imp_a", "w/t1.cbz", "t1"), &positions).unwrap();

            let state: Vec<(i64, i64)> = db
                .read(|cx| {
                    cx.query(
                        "SELECT finished, times_finished FROM progress ORDER BY profile_id",
                        [],
                        |r| Ok((r.get(0)?, r.get(1)?)),
                    )
                })
                .unwrap();
            assert_eq!(vec![(1, 2), (1, 2)], state, "{positions:?}");
        }
    }

    #[test]
    fn a_position_past_the_new_last_page_is_brought_back_to_it() {
        let (_dir, db) = library();
        read_at(&db, "t1", &[("default", 112), ("p1", 29), ("p2", 5)]);

        replaced::replace(
            &db,
            &done("imp_a", "w/t1.cbz", "t1"),
            &Positions::Kept { last: 29 },
        )
        .unwrap();

        assert_eq!(
            vec![
                ("default".to_string(), 29),
                ("p1".to_string(), 29),
                ("p2".to_string(), 5)
            ],
            pages_of(&db, "t1")
        );
    }

    #[test]
    fn a_replacement_leaves_the_other_volumes_places_alone() {
        let (_dir, db) = library();
        read_at(&db, "t1", &[("default", 112)]);
        read_at(&db, "t2", &[("default", 112)]);

        replaced::replace(
            &db,
            &done("imp_a", "w/t1.cbz", "t1"),
            &Positions::Kept { last: 3 },
        )
        .unwrap();

        assert_eq!(vec![("default".to_string(), 112)], pages_of(&db, "t2"));
    }

    #[test]
    fn a_splice_shifts_what_follows_and_keeps_what_still_fits() {
        let (_dir, db) = library();
        read_at(&db, "t1", &[("a", 50), ("b", 70), ("c", 120), ("d", 150)]);

        replaced::replace(
            &db,
            &done("imp_a", "w/t1.cbz", "t1"),
            &Positions::Spliced {
                from: 62,
                old_to: 120,
                new_to: 123,
                last: 400,
            },
        )
        .unwrap();

        let after: Vec<i64> = pages_of(&db, "t1").into_iter().map(|(_, p)| p).collect();
        assert_eq!(vec![50, 70, 120, 153], after);
    }

    #[test]
    fn a_splice_that_shortens_the_chapter_brings_its_readers_back_into_it() {
        let (_dir, db) = library();
        read_at(&db, "t1", &[("a", 100), ("b", 130), ("c", 61)]);

        replaced::replace(
            &db,
            &done("imp_a", "w/t1.cbz", "t1"),
            &Positions::Spliced {
                from: 62,
                old_to: 120,
                new_to: 90,
                last: 400,
            },
        )
        .unwrap();

        let after: Vec<i64> = pages_of(&db, "t1").into_iter().map(|(_, p)| p).collect();
        assert_eq!(
            vec![90, 100, 61],
            after,
            "inside: held at the new end; after: shifted back"
        );
    }

    #[test]
    fn a_size_no_column_can_hold_is_refused() {
        let (_dir, db) = library();
        read_at(&db, "t1", &[("default", 112)]);
        let mut bad = done("imp_a", "w/t1.cbz", "t1");
        // A size no integer column can hold: the call refuses before it writes anything.
        bad.old_size = u64::MAX;

        assert!(replaced::replace(&db, &bad, &Positions::Kept { last: 3 }).is_err());

        assert!(replaced::of_entry(&db, "t1").unwrap().is_empty());
        assert_eq!(vec![("default".to_string(), 112)], pages_of(&db, "t1"));
    }

    #[test]
    fn a_replacement_that_fails_after_its_record_leaves_no_record_and_no_moved_place() {
        let (_dir, db) = library();
        read_at(&db, "t1", &[("default", 112)]);
        // The record is written first; the places are settled after it. A refusal there must
        // take the record back with it, or the history would tell of a replacement whose
        // places never moved.
        db.write(|cx| {
            cx.run(
                "CREATE TRIGGER refuse BEFORE UPDATE ON progress
                 BEGIN SELECT RAISE(ABORT, 'refusé'); END",
            )
        })
        .unwrap();

        let failed = replaced::replace(
            &db,
            &done("imp_a", "w/t1.cbz", "t1"),
            &Positions::Kept { last: 3 },
        );

        assert!(failed.is_err());
        assert!(replaced::of_entry(&db, "t1").unwrap().is_empty());
        assert_eq!(vec![("default".to_string(), 112)], pages_of(&db, "t1"));
    }

    #[test]
    fn what_was_replaced_in_an_entry_is_read_in_one_query() {
        let (_dir, db) = library();
        for session in ["imp_a", "imp_b", "imp_c"] {
            replaced::replace(
                &db,
                &done(session, "w/t1.cbz", "t1"),
                &Positions::Kept { last: 3 },
            )
            .unwrap();
        }

        let before = db.statements();
        let found = replaced::of_entry(&db, "t1").unwrap();

        assert_eq!(3, found.len());
        assert_eq!(1, db.statements() - before);
    }
}
