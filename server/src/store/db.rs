//! SQLite over rusqlite, with no layer on top. The SQL stays readable, there is no
//! generation step, and it removes one learning axis from a project whose real subject is
//! the model.
//!
//! # Why the database is not async
//!
//! SQLite is a synchronous library. There is no async SQLite: every crate that offers one
//! runs the real calls on a pool of blocking threads behind an async façade.
//!
//! The Kotlin server expressed "the transaction currently open" with the **thread** — a
//! `ThreadLocal` flag routing reads to the writer's connection while a transaction was
//! open, because the scanner reads back what it has just written. In async Rust a task
//! changes thread at every `.await`, so that mechanism is not merely awkward, it is wrong.
//!
//! So the database stays out of the runtime. Handlers are async; every call in here is
//! made from inside [`tokio::task::spawn_blocking`], and the transaction is a **value**
//! that is handed to the closure. The routing disappears: reads made during a write go
//! through the same [`Cx`] because it is the parameter they were given.
//!
//! One thread hop per request. At three clients that is noise, and it buys a rule the
//! compiler enforces — the shape the Kotlin arrived at only after a bug where an edit
//! landing during a scan was silently rolled back, and a scan was visible half-done.

use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Mutex;

use anyhow::{Context, Result};
use rusqlite::{Connection, OpenFlags, Params, Row, Transaction};

use super::schema::{schema_version, MIGRATIONS, SCHEMA, SEARCH_SCHEMA};

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

impl Db {
    pub fn open(path: &Path) -> Result<Self> {
        if let Some(parent) = path.parent() {
            std::fs::create_dir_all(parent)
                .with_context(|| format!("creating {}", parent.display()))?;
        }

        let writer =
            Connection::open(path).with_context(|| format!("opening {}", path.display()))?;
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
    fn prepare(&self) -> Result<()> {
        let existed = self.version()? > 0 || self.table_count()? > 0;

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
            Ok(())
        })?;

        self.migrate(!existed)
    }

    /// Applies what this database has not seen, in order, and stops on the first failure.
    ///
    /// Swallowing errors was the real danger of the first version of this in Kotlin: a
    /// migration that failed for a genuine reason looked exactly like one that had already
    /// run, and the server started on a half-migrated schema without a word.
    fn migrate(&self, already_current: bool) -> Result<()> {
        if already_current {
            self.set_version(schema_version())?;
            return Ok(());
        }
        let from = self.version()?;
        if from >= schema_version() {
            return Ok(());
        }
        for (i, sql) in MIGRATIONS.iter().enumerate().skip(from as usize) {
            let step = i + 1;
            let outcome = self.write(|cx| cx.run(sql));
            match outcome {
                Ok(()) => {}
                Err(e) if already_satisfied(sql, &e) => {}
                Err(e) => {
                    return Err(e.context(format!("migration {step} failed: {}", first_line(sql))))
                }
            }
            self.set_version(step as i32)?;
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
        Ok(conn)
    }

    fn give_back(&self, conn: Connection) {
        if let Ok(mut pool) = self.readers.lock() {
            if pool.len() < READERS {
                pool.push(conn);
            }
        }
    }

    fn version(&self) -> Result<i32> {
        self.read(|cx| {
            Ok(cx
                .query_one("PRAGMA user_version", [], |r| r.get::<_, i32>(0))?
                .unwrap_or(0))
        })
    }

    fn set_version(&self, value: i32) -> Result<()> {
        // A pragma takes no parameters, so the value is formatted in — it is an i32 we
        // produced ourselves, never anything that came off the wire.
        self.write(|cx| cx.run(&format!("PRAGMA user_version = {value}")))
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
/// Two shapes qualify, and both are the step having nothing left to do: adding a column
/// that is there, dropping one that is not. Anything else has to be seen.
fn already_satisfied(sql: &str, error: &anyhow::Error) -> bool {
    let statement = sql.to_lowercase();
    let message = format!("{error:#}").to_lowercase();
    (statement.contains("add column") && message.contains("duplicate column"))
        || (statement.contains("drop column") && message.contains("no such column"))
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
