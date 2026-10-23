//! SQLite over rusqlite, with no layer on top. The SQL stays readable, there is no
//! generation step, and it removes one learning axis from a project whose real subject is
//! the model.
//!
//! # Why the database is not async
//!
//! SQLite is a synchronous library. There is no async SQLite: every crate that offers one
//! runs the real calls on a pool of blocking threads behind an async façade.
//!
//! The obvious way to express "the transaction currently open" is a `ThreadLocal` flag,
//! routing reads to the writer's connection while a transaction is open — the scanner reads
//! back what it has just written. In async Rust a task changes thread at every `.await`, so
//! that mechanism is not merely awkward, it is wrong.
//!
//! So the database stays out of the runtime. Handlers are async; every call in here is
//! made from inside [`tokio::task::spawn_blocking`], and the transaction is a **value**
//! that is handed to the closure. The routing disappears: reads made during a write go
//! through the same [`Cx`] because it is the parameter they were given.
//!
//! One thread hop per request. At three clients that is noise, and it buys a rule the
//! compiler enforces rather than one arrived at after an edit landing during a scan was
//! silently rolled back, and a scan was visible half-done.

use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Mutex;

use anyhow::{bail, Context, Result};
use rusqlite::{Connection, OpenFlags, Params, Row, Transaction};

use super::schema::{
    last_migration_of_old_count, schema_version, LEAF_APPLICATION_ID, MIGRATIONS, SCHEMA,
    SEARCH_SCHEMA,
};
use super::text::search_key;

/// How many reader connections are kept alive between calls. Opening one is not free, and
/// a handful covers a household: the writer is the thing that serialises, not these.
const READERS: usize = 8;

/// A connection, and the counter that watches what is asked of it.
///
/// Everything the repository does goes through here rather than touching a [`Connection`],
/// so that a test can state a **cost** rather than a speed. The defects that hurt most as a
/// library grows do not fail anything — a list that asks one question per series still
/// returns the right list, it just asks two hundred and one questions to do it, and nothing
/// notices until the library is big enough to make it hurt.
pub struct Cx<'a> {
    conn: &'a Connection,
    counter: &'a AtomicU64,
}

impl<'a> Cx<'a> {
    /// Every row the query returns, mapped.
    pub fn query<T, P, F>(&self, sql: &str, params: P, mut row: F) -> Result<Vec<T>>
    where
        P: Params,
        F: FnMut(&Row<'_>) -> rusqlite::Result<T>,
    {
        self.counter.fetch_add(1, Ordering::Relaxed);
        let mut statement = self
            .conn
            .prepare(sql)
            .with_context(|| format!("preparing: {}", first_line(sql)))?;
        let mapped = statement.query_map(params, |r| row(r))?;
        let mut out = Vec::new();
        for item in mapped {
            out.push(item?);
        }
        Ok(out)
    }

    /// The first row, or nothing. `Option` rather than an error: "no such series" is an
    /// answer, and the route above turns it into a 404.
    pub fn query_one<T, P, F>(&self, sql: &str, params: P, row: F) -> Result<Option<T>>
    where
        P: Params,
        F: FnMut(&Row<'_>) -> rusqlite::Result<T>,
    {
        Ok(self.query(sql, params, row)?.into_iter().next())
    }

    /// An INSERT, UPDATE or DELETE. Returns how many rows it touched.
    pub fn execute<P: Params>(&self, sql: &str, params: P) -> Result<usize> {
        self.counter.fetch_add(1, Ordering::Relaxed);
        self.conn
            .execute(sql, params)
            .with_context(|| format!("executing: {}", first_line(sql)))
    }

    /// A statement with nothing to bind and nothing to return — pragmas, DDL.
    pub fn run(&self, sql: &str) -> Result<()> {
        self.counter.fetch_add(1, Ordering::Relaxed);
        self.conn
            .execute_batch(sql)
            .with_context(|| format!("running: {}", first_line(sql)))
    }

    /// The underlying connection, for the rare thing [`Cx`] does not wrap. It is not
    /// counted, which is the reason to keep reaching for it rare.
    pub fn raw(&self) -> &Connection {
        self.conn
    }
}

/// The database. One writer, as many readers as the pool holds.
///
/// WAL is built for exactly this shape: one writer, any number of readers, each on a
/// consistent snapshot. So writes go through a lock, and reads go to a connection of their
/// own — which is what lets browsing during a scan show the library as it was, rather than
/// half-rebuilt.
pub struct Db {
    path: PathBuf,
    writer: Mutex<Connection>,
    readers: Mutex<Vec<Connection>>,
    statements: AtomicU64,
}

/// Alphabetical order for a library that is not written in ASCII.
///
/// SQLite ships `BINARY` and `NOCASE`, and `NOCASE` folds A–Z and nothing else — so « Élève »
/// sorted after « Zorro », and a French shelf ran A, B, … Z, À, Â, É. This folds both sides
/// the way the search index is folded: the accent goes onto its letter, the case goes away,
/// and an accented initial keeps its letter's place. Digits fold to themselves and so come
/// before every letter, which is where a title beginning with a number belongs.
///
/// Registered on every connection, reader and writer alike — a collation is a property of
/// the connection, and a query running on one that lacks it fails rather than sorting badly.
/// Used only in `ORDER BY`, never in an index, so no stored data depends on it and nothing
/// has to be migrated when the folding changes.
fn name_order(one: &str, other: &str) -> std::cmp::Ordering {
    // Folded first, then the raw text: two spellings that fold to the same key — « Elève »
    // and « Élève » — must still have a fixed order, or a page boundary can fall between
    // them and show one of them twice.
    search_key(one)
        .cmp(&search_key(other))
        .then_with(|| one.cmp(other))
}

fn with_collation(conn: Connection) -> Result<Connection> {
    conn.create_collation("LEAF", name_order)
        .context("registering the LEAF collation")?;
    Ok(conn)
}

impl Db {
    pub fn open(path: &Path) -> Result<Self> {
        if let Some(parent) = path.parent() {
            std::fs::create_dir_all(parent)
                .with_context(|| format!("creating {}", parent.display()))?;
        }

        let writer = with_collation(
            Connection::open(path).with_context(|| format!("opening {}", path.display()))?,
        )?;
        writer.execute_batch(
            "PRAGMA journal_mode = WAL;
             PRAGMA foreign_keys = ON;
             PRAGMA synchronous = NORMAL;
             -- In-process the Mutex below serialises writes, so this is for everything
             -- else that can hold the file for a moment: a WAL checkpoint, a `sqlite3`
             -- session left open, the other server during the port. Without it those come
             -- back as SQLITE_BUSY at once instead of being waited out.
             PRAGMA busy_timeout = 5000;",
        )?;

        let db = Db {
            path: path.to_path_buf(),
            writer: Mutex::new(writer),
            readers: Mutex::new(Vec::new()),
            statements: AtomicU64::new(0),
        };
        db.prepare()?;
        Ok(db)
    }

    /// Creates what is missing and applies what this database has not seen.
    ///
    /// What it has seen is decided before anything is written, so an index this server must
    /// not run on is refused as it was found.
    fn prepare(&self) -> Result<()> {
        let found = self.pragma("user_version")?;
        let marked = self.pragma("application_id")? == LEAF_APPLICATION_ID;
        let existed = found > 0 || self.table_count()? > 0;

        // The last migration this database has wholly applied, or nothing for a new one.
        let done = if !existed {
            None
        } else if marked {
            // A server older than its index used to start on it without a word, on a schema
            // it does not know: columns it never fills, a table rebuilt under it.
            if found > schema_version() {
                bail!(
                    "{} was written by a newer Leaf: its schema is at migration {found} and \
                     this server knows only up to {}, so it must not run on it — start the \
                     newer server again, or restore a copy of the index taken before it ran",
                    self.path.display(),
                    schema_version()
                );
            }
            Some(found)
        } else {
            // No marker: `user_version` counts entries of the old flat list, not migrations.
            let Some(last) = last_migration_of_old_count(found) else {
                bail!(
                    "{} counts {found} steps in the numbering Leaf used before its migrations \
                     were numbered, more than any release ever wrote: it comes from a build \
                     whose schema this server cannot know, so it must not run on it",
                    self.path.display()
                );
            };
            Some(last)
        };

        self.write(|cx| {
            for statement in SCHEMA {
                cx.run(statement)?;
            }
            // The search index used to be an ordinary table. Recreating it costs nothing —
            // it is rebuilt by the next scan — so the shape is checked rather than migrated.
            if search_is_not_fts(cx)? {
                cx.run("DROP TABLE IF EXISTS search")?;
            }
            for statement in SEARCH_SCHEMA {
                cx.run(statement)?;
            }
            // Stamped in the transaction that made the tables, so a crash cannot leave a new
            // database with its tables and no version — one the next start would take for an
            // old index and replay every migration on. An old-numbering index is restamped in
            // migrations here, and from then on the marker says how to read it.
            match done {
                None => stamp(cx, schema_version()),
                Some(last) if !marked => stamp(cx, last),
                Some(_) => Ok(()),
            }
        })?;

        match done {
            Some(last) => self.migrate(last),
            None => Ok(()),
        }
    }

    /// Applies what this database has not seen, in order, and stops on the first failure.
    ///
    /// Swallowing errors is the real danger here: a migration that failed for a genuine
    /// reason looks exactly like one that had already run, and the server would start on a
    /// half-migrated schema without a word.
    ///
    /// Each migration and the version that records it are one transaction: stamped apart,
    /// a crash between the two ran a migration the version did not admit to, and a crash
    /// between two entries of one migration left it half applied.
    fn migrate(&self, done: i32) -> Result<()> {
        for number in done + 1..=schema_version() {
            self.write(|cx| {
                apply(cx, number)?;
                cx.run(&format!("PRAGMA user_version = {number}"))
            })?;
        }
        Ok(())
    }

    /// Reads, on a connection of its own, on a snapshot the writer cannot disturb.
    ///
    /// Call it from inside `spawn_blocking`: it blocks, and says so.
    pub fn read<T, F>(&self, f: F) -> Result<T>
    where
        F: FnOnce(&Cx<'_>) -> Result<T>,
    {
        let conn = self.checkout()?;
        let result = f(&Cx {
            conn: &conn,
            counter: &self.statements,
        });
        self.give_back(conn);
        result
    }

    /// Writes, under the lock, in one transaction.
    ///
    /// Either everything the closure did lands, or none of it does — a full scan is one
    /// transaction, so the library is never coherent-in-parts. Reads made inside go
    /// through the same [`Cx`], so they see what has just been written; there is nothing
    /// to route, because the transaction is the argument.
    pub fn write<T, F>(&self, f: F) -> Result<T>
    where
        F: FnOnce(&Cx<'_>) -> Result<T>,
    {
        // Taken back rather than refused. A panic while this lock was held unwound through
        // the `Transaction` below, and a transaction rolls back when it is dropped — so the
        // connection is in a known state and the only thing poisoning would achieve is
        // turning one panic into a server that can never write again.
        let mut guard = self
            .writer
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        let tx: Transaction<'_> = guard.transaction()?;
        let result = f(&Cx {
            conn: &tx,
            counter: &self.statements,
        });
        match result {
            Ok(value) => {
                tx.commit()?;
                Ok(value)
            }
            Err(e) => {
                // Explicit, though the drop would roll back anyway: a rollback that happens
                // by accident of scope is a rollback nobody reads.
                tx.rollback()?;
                Err(e)
            }
        }
    }

    /// How many statements have been run since the process started.
    ///
    /// Here so that tests can hold on to a number that does not depend on the machine, the
    /// disk or the day.
    pub fn statements(&self) -> u64 {
        self.statements.load(Ordering::Relaxed)
    }

    fn checkout(&self) -> Result<Connection> {
        if let Some(conn) = self.readers.lock().ok().and_then(|mut p| p.pop()) {
            return Ok(conn);
        }
        let conn = Connection::open_with_flags(
            &self.path,
            OpenFlags::SQLITE_OPEN_READ_ONLY
                | OpenFlags::SQLITE_OPEN_URI
                | OpenFlags::SQLITE_OPEN_NO_MUTEX,
        )
        // No fallback to a writable connection. One that could write would be a second
        // writer outside the lock, which is the one thing this whole file is arranged to
        // prevent — and it would only ever open in the case where something is already
        // wrong with the file.
        .with_context(|| format!("opening a reader on {}", self.path.display()))?;
        // A reader never waits on the writer in WAL, but a checkpoint can hold the file for
        // a moment. Waiting briefly beats failing the request.
        conn.execute_batch("PRAGMA busy_timeout = 5000; PRAGMA foreign_keys = ON;")?;
        with_collation(conn)
    }

    fn give_back(&self, conn: Connection) {
        if let Ok(mut pool) = self.readers.lock() {
            if pool.len() < READERS {
                pool.push(conn);
            }
        }
    }

    /// `user_version` or `application_id`: a name from this file, never from the wire.
    fn pragma(&self, name: &str) -> Result<i32> {
        self.read(|cx| {
            Ok(cx
                .query_one(&format!("PRAGMA {name}"), [], |r| r.get::<_, i32>(0))?
                .unwrap_or(0))
        })
    }

    fn table_count(&self) -> Result<i64> {
        self.read(|cx| {
            Ok(cx
                .query_one(
                    "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table'",
                    [],
                    |r| r.get::<_, i64>(0),
                )?
                .unwrap_or(0))
        })
    }
}

/// Marks the database as numbering migrations, at `version`. A pragma takes no parameters,
/// so the values are formatted in — integers produced here, never anything off the wire.
fn stamp(cx: &Cx<'_>, version: i32) -> Result<()> {
    cx.run(&format!(
        "PRAGMA application_id = {LEAF_APPLICATION_ID}; PRAGMA user_version = {version};"
    ))
}

/// Runs migration `number`, statement by statement, inside the caller's transaction.
///
/// One at a time rather than as one `execute_batch`, because a batch stops at its first
/// error: a migration whose `ADD COLUMN` met a column already there skipped every statement
/// after it, and letting that error pass then called the migration done — migration 3 would
/// have added nothing and backfilled nothing. Each statement's own failure is weighed here,
/// and a statement after a tolerated one still runs.
fn apply(cx: &Cx<'_>, number: i32) -> Result<()> {
    let statements = usize::try_from(number)
        .ok()
        .and_then(|n| n.checked_sub(1))
        .and_then(|i| MIGRATIONS.get(i))
        .with_context(|| format!("there is no migration {number}"))?;
    for sql in *statements {
        match cx.run(sql) {
            Ok(()) => {}
            Err(e) if already_satisfied(sql, &e) => {}
            Err(e) => {
                return Err(e)
                    .with_context(|| format!("migration {number} failed: {}", first_line(sql)))
            }
        }
    }
    Ok(())
}

/// Runs migration `number` on a bare connection, in a transaction of its own, without
/// touching the version.
///
/// Public for one test: the one that runs every migration twice in a row on an index as old
/// as the first version, which [`Db::open`] would bring up to date before it could be
/// handed over.
pub fn apply_migration(conn: &Connection, number: i32) -> Result<()> {
    let tx = conn.unchecked_transaction()?;
    let counter = AtomicU64::new(0);
    apply(
        &Cx {
            conn: &tx,
            counter: &counter,
        },
        number,
    )?;
    tx.commit()?;
    Ok(())
}

fn search_is_not_fts(cx: &Cx<'_>) -> Result<bool> {
    let sql: Option<Option<String>> = cx.query_one(
        "SELECT sql FROM sqlite_master WHERE type = 'table' AND name = 'search'",
        [],
        |r| r.get::<_, Option<String>>(0),
    )?;
    Ok(match sql.flatten() {
        Some(text) => !text.to_lowercase().contains("fts5"),
        None => false,
    })
}

/// Whether the failure means the database is already in the state the step asks for.
///
/// Three shapes qualify, and all three are the step having nothing left to do: adding a
/// column that is there, dropping one that is not, or backfilling `work_author` from a
/// `work.author` column that is not there either — a database new enough to have never had
/// one has nothing to carry into the table that replaces it, which is the backfill's own
/// goal already met rather than something wrong. Anything else has to be seen.
fn already_satisfied(sql: &str, error: &anyhow::Error) -> bool {
    let statement = sql.to_lowercase();
    let message = format!("{error:#}").to_lowercase();
    (statement.contains("add column") && message.contains("duplicate column"))
        || (statement.contains("drop column") && message.contains("no such column"))
        || (statement.contains("into work_author") && message.contains("no such column"))
}

/// SQL is written across several lines here; an error message wants one.
fn first_line(sql: &str) -> String {
    sql.split_whitespace()
        .collect::<Vec<_>>()
        .join(" ")
        .chars()
        .take(90)
        .collect()
}
