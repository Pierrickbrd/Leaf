//! The schema, deliberately hand-written in plain SQL: ten tables justify neither code
//! generation nor a query builder.
//!
//! Nothing here requires chapters to exist: a series may be flat volumes. Chapter markers
//! and arcs are tables that stay empty, not columns that must be filled.
//!
//! Ported statement for statement from the Kotlin. The SQL is the part of that server
//! worth keeping unchanged — it is where the model lives, and it has tests behind it.

/// Run in order on every start. `IF NOT EXISTS` throughout, so this is idempotent and a
/// fresh database is created already current.
pub const SCHEMA: &[&str] = &[
    r#"
    CREATE TABLE IF NOT EXISTS universe (
      id    TEXT PRIMARY KEY,
      name  TEXT NOT NULL,
      path  TEXT NOT NULL UNIQUE
    )
    "#,
    r#"
    CREATE TABLE IF NOT EXISTS work (
      id                TEXT PRIMARY KEY,
      universe_id       TEXT REFERENCES universe(id) ON DELETE SET NULL,
      name              TEXT NOT NULL,
      path              TEXT NOT NULL UNIQUE,
      title             TEXT,
      medium            TEXT,
      author            TEXT,
      status            TEXT,
      reading_direction TEXT,
      summary           TEXT
    )
    "#,
    // One row per genre, and the only record of one. A comma-joined column beside this
    // table showed well and filtered badly, and the two drifted exactly where you would
    // expect: a genre inherited from a ComicInfo reached the column and not the table.
    r#"
    CREATE TABLE IF NOT EXISTS work_genre (
      work_id TEXT NOT NULL REFERENCES work(id) ON DELETE CASCADE,
      name    TEXT NOT NULL,
      key     TEXT NOT NULL,
      PRIMARY KEY (work_id, key)
    )
    "#,
    "CREATE INDEX IF NOT EXISTS work_genre_key ON work_genre(key)",
    // Where a search row lives, so removing it is a lookup rather than a scan of the index.
    r#"
    CREATE TABLE IF NOT EXISTS search_ref (
      kind   TEXT NOT NULL,
      ref    TEXT NOT NULL,
      row_id INTEGER NOT NULL,
      digest TEXT,
      PRIMARY KEY (kind, ref)
    )
    "#,
    r#"
    CREATE TABLE IF NOT EXISTS edition (
      id           TEXT PRIMARY KEY,
      work_id      TEXT NOT NULL REFERENCES work(id) ON DELETE CASCADE,
      name         TEXT,
      path         TEXT NOT NULL UNIQUE,
      implicit     INTEGER NOT NULL DEFAULT 0,
      publisher    TEXT,
      status       TEXT,
      medium       TEXT,
      cover_file   TEXT,
      reading_direction TEXT,
      volume_count INTEGER,
      format       TEXT,
      language     TEXT
    )
    "#,
    // A range, not a list: four Haikyū volumes belong to two arcs, because an arc does
    // not end where a volume ends.
    r#"
    CREATE TABLE IF NOT EXISTS arc (
      id          TEXT PRIMARY KEY,
      edition_id  TEXT NOT NULL REFERENCES edition(id) ON DELETE CASCADE,
      name        TEXT NOT NULL,
      unit        TEXT NOT NULL CHECK (unit IN ('CHAPTER','VOLUME')),
      from_number REAL NOT NULL,
      to_number   REAL NOT NULL,
      position    INTEGER NOT NULL
    )
    "#,
    r#"
    CREATE TABLE IF NOT EXISTS entry (
      id            TEXT PRIMARY KEY,
      edition_id    TEXT NOT NULL REFERENCES edition(id) ON DELETE CASCADE,
      type          TEXT NOT NULL CHECK (type IN ('VOLUME','CHAPTER')),
      file          TEXT NOT NULL UNIQUE,
      size          INTEGER NOT NULL,
      modified_at   INTEGER NOT NULL,
      added_at      INTEGER,
      cover_file    TEXT,
      volume_number REAL,
      title         TEXT,
      sort_key      REAL,
      page_count    INTEGER NOT NULL DEFAULT 0,
      isbn          TEXT,
      published_on  TEXT,
      summary       TEXT
    )
    "#,
    // A number is unique within an edition, whether it comes from a CHAPTER entry or
    // from a marker inside a volume. SQLite treats NULLs as distinct, so unnumbered
    // bonuses coexist without colliding.
    r#"
    CREATE TABLE IF NOT EXISTS chapter (
      id          TEXT PRIMARY KEY,
      edition_id  TEXT NOT NULL REFERENCES edition(id) ON DELETE CASCADE,
      entry_id    TEXT NOT NULL REFERENCES entry(id) ON DELETE CASCADE,
      raw         TEXT NOT NULL,
      label       TEXT NOT NULL,
      number      REAL,
      title       TEXT,
      kind        TEXT NOT NULL CHECK (kind IN ('CHAPTER','BONUS')),
      position    INTEGER NOT NULL,
      start_page  INTEGER,
      volume      REAL
    )
    "#,
    "CREATE UNIQUE INDEX IF NOT EXISTS ix_chapter_number ON chapter(edition_id, number)",
    "CREATE INDEX IF NOT EXISTS ix_arc_edition ON arc(edition_id, position)",
    "CREATE INDEX IF NOT EXISTS ix_chapter_entry ON chapter(entry_id, position)",
    // A foreign key with no index behind it makes SQLite scan the child table for every
    // parent row deleted, and both of these are followed on every aimed rescan as well.
    "CREATE INDEX IF NOT EXISTS ix_edition_work ON edition(work_id)",
    "CREATE INDEX IF NOT EXISTS ix_work_universe ON work(universe_id)",
    "CREATE INDEX IF NOT EXISTS ix_entry_edition ON entry(edition_id, sort_key)",
    r#"
    CREATE TABLE IF NOT EXISTS page (
      entry_id    TEXT NOT NULL REFERENCES entry(id) ON DELETE CASCADE,
      number      INTEGER NOT NULL,
      entry_name  TEXT NOT NULL,
      media_type  TEXT NOT NULL,
      width       INTEGER,
      height      INTEGER,
      size        INTEGER,
      PRIMARY KEY (entry_id, number)
    )
    "#,
    // Progress belongs to the edition: reading chapter 100 in the Perfect Edition does
    // not mark it read in the original.
    r#"
    CREATE TABLE IF NOT EXISTS progress (
      entry_id    TEXT PRIMARY KEY REFERENCES entry(id) ON DELETE CASCADE,
      edition_id  TEXT NOT NULL REFERENCES edition(id) ON DELETE CASCADE,
      page        INTEGER NOT NULL,
      finished    INTEGER NOT NULL DEFAULT 0,
      updated_at  INTEGER NOT NULL
    )
    "#,
    // Keyed by entry, but read by edition: the read status of a shelf asks "what has this
    // edition got" twice for every tile.
    "CREATE INDEX IF NOT EXISTS ix_progress_edition ON progress(edition_id, finished)",
];

/// The search index, as an FTS5 table.
///
/// FTS5 ships inside SQLite, so this costs no new dependency: BM25 ranking, prefix
/// queries, and `remove_diacritics 2` folding accents in the tokenizer itself. Two indexed
/// columns rather than one, so a hit on a title can outweigh a hit buried in a raw label.
pub const SEARCH_SCHEMA: &[&str] = &[r#"
    CREATE VIRTUAL TABLE IF NOT EXISTS search USING fts5(
      name,
      detail,
      kind UNINDEXED,
      ref UNINDEXED,
      edition_id UNINDEXED,
      entry_id UNINDEXED,
      label UNINDEXED,
      tokenize = "unicode61 remove_diacritics 2"
    )
    "#];

/// Changes applied to an existing database, in order, each exactly once.
///
/// SQLite keeps a counter for this — `PRAGMA user_version` — so nothing has to be guessed
/// from the schema. A fresh database is created at the current version and skips them all;
/// an existing one runs whatever it has not seen yet.
///
/// The index is rebuildable by rescanning, so the safe move for anything a migration
/// cannot express is still to delete the file and scan again. These exist so that stops
/// being necessary once progress is in there — that, a rescan cannot bring back.
///
/// **They are numbered by position and must never be reordered or removed**, because a
/// database on disk carries the count of how many it has already run.
pub const MIGRATIONS: &[&str] = &[
    // 1 — an edition can read the other way round from its work.
    "ALTER TABLE edition ADD COLUMN reading_direction TEXT",
    // 2 — what the work is, so browsing can filter on it.
    "ALTER TABLE work ADD COLUMN medium TEXT",
    "ALTER TABLE edition ADD COLUMN medium TEXT",
    // 3 — when a volume joined the library. Set once and never again, because it cannot
    // be recovered afterwards: a column added later leaves everything before it blank
    // for ever.
    "ALTER TABLE entry ADD COLUMN added_at INTEGER",
    "UPDATE entry SET added_at = modified_at WHERE added_at IS NULL",
    // 4 — a cover chosen on disk rather than taken from page 0.
    "ALTER TABLE entry ADD COLUMN cover_file TEXT",
    "ALTER TABLE edition ADD COLUMN cover_file TEXT",
    // 5 — genres as rows, so the filter can name one exactly.
    r#"
    CREATE TABLE IF NOT EXISTS work_genre (
      work_id TEXT NOT NULL REFERENCES work(id) ON DELETE CASCADE,
      name    TEXT NOT NULL,
      key     TEXT NOT NULL,
      PRIMARY KEY (work_id, key)
    )
    "#,
    "CREATE INDEX IF NOT EXISTS work_genre_key ON work_genre(key)",
    // 6 — kind and ref are UNINDEXED columns of an FTS5 table, so deleting by them scanned
    // the whole index: reindexing n rows cost n² row visits. A plain table mapping
    // (kind, ref) to the FTS rowid turns each delete into a lookup.
    r#"
    CREATE TABLE IF NOT EXISTS search_ref (
      kind   TEXT NOT NULL,
      ref    TEXT NOT NULL,
      row_id INTEGER NOT NULL,
      PRIMARY KEY (kind, ref)
    )
    "#,
    "DELETE FROM search",
    "DELETE FROM search_ref",
    // 7 — what the row was written from, so a scan can tell that it has not changed.
    "ALTER TABLE search_ref ADD COLUMN digest TEXT",
    // 8 — which volume a loose chapter came from. A story reaches you as volumes, as loose
    // chapters, or as both in turn; without this the server calls a volume missing when it
    // holds the whole of its content under another name.
    "ALTER TABLE chapter ADD COLUMN volume REAL",
    // 9 — one record of a genre instead of two. work.genres held "a, b, c" for display
    // while work_genre held the rows the filter used, and the copy was the one that showed.
    "ALTER TABLE work DROP COLUMN genres",
    // 10 — the read status of a shelf queries progress by edition twice per tile.
    "CREATE INDEX IF NOT EXISTS ix_progress_edition ON progress(edition_id, finished)",
    // 11 — a column nothing ever wrote. It was there for a per-entry integrity check that
    // was never built; what actually needed checking was a file crossing the network, and
    // that is verified at the import instead, against the checksum the manifest announces.
    // A schema that claims a fact it does not hold is a schema that will be believed.
    "ALTER TABLE entry DROP COLUMN checksum",
    // 12 — arcs are read by edition on every series page, and there were none to read by.
    "CREATE INDEX IF NOT EXISTS ix_arc_edition ON arc(edition_id, position)",
    // 13 — the two foreign keys that had no index behind them, so deleting a work scanned
    // every edition and deleting a universe scanned every work.
    "CREATE INDEX IF NOT EXISTS ix_edition_work ON edition(work_id)",
    "CREATE INDEX IF NOT EXISTS ix_work_universe ON work(universe_id)",
];

/// What a fresh database is stamped with. Deriving it from the list is what makes adding a
/// migration a one-line change that cannot be got wrong.
pub fn schema_version() -> i32 {
    MIGRATIONS.len() as i32
}
