//! The schema, deliberately hand-written in plain SQL: fourteen tables justify neither code
//! generation nor a query builder.
//!
//! Nothing here requires chapters to exist: a series may be flat volumes. Chapter markers
//! and arcs are tables that stay empty, not columns that must be filled.
//!
//! The SQL is written out rather than generated: it is where the model lives, and it has
//! tests behind it.

/// The profile every position belonged to before profiles existed, and the one a device
/// reads under until it is given another.
pub const DEFAULT_PROFILE: &str = "default";

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
      status            TEXT,
      reading_direction TEXT,
      summary           TEXT,
      age_rating        TEXT
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
    // Shaped exactly like work_genre, for the same reason: a comma-joined `author` column
    // showed one name and matched none of the rest of it — "Obata" found nothing on Death
    // Note, whose art is entirely his. Two of these, one for the writer and one for the
    // illustrator, because 11 of the 25 series this was measured against have one of each
    // and the two are never the same table.
    r#"
    CREATE TABLE IF NOT EXISTS work_author (
      work_id TEXT NOT NULL REFERENCES work(id) ON DELETE CASCADE,
      name    TEXT NOT NULL,
      key     TEXT NOT NULL,
      PRIMARY KEY (work_id, key)
    )
    "#,
    "CREATE INDEX IF NOT EXISTS work_author_key ON work_author(key)",
    // The illustrator: penciller, inker and cover artist in one table rather than a
    // credits(role, name) list, because in all 25 series measured the three are one person
    // without exception — a general mechanism for a case that does not exist.
    r#"
    CREATE TABLE IF NOT EXISTS work_artist (
      work_id TEXT NOT NULL REFERENCES work(id) ON DELETE CASCADE,
      name    TEXT NOT NULL,
      key     TEXT NOT NULL,
      PRIMARY KEY (work_id, key)
    )
    "#,
    "CREATE INDEX IF NOT EXISTS work_artist_key ON work_artist(key)",
    // Beside work_genre, never merged into it: measured, Les Terres d'Arran carries one
    // genre and seven tags with nothing in common between the two lists.
    r#"
    CREATE TABLE IF NOT EXISTS work_tag (
      work_id TEXT NOT NULL REFERENCES work(id) ON DELETE CASCADE,
      name    TEXT NOT NULL,
      key     TEXT NOT NULL,
      PRIMARY KEY (work_id, key)
    )
    "#,
    "CREATE INDEX IF NOT EXISTS work_tag_key ON work_tag(key)",
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
      collection   TEXT,
      status       TEXT,
      medium       TEXT,
      cover_file   TEXT,
      reading_direction TEXT,
      volume_count INTEGER,
      format       TEXT,
      language     TEXT,
      colour       INTEGER,
      one_shot     INTEGER NOT NULL DEFAULT 0,
      summary      TEXT
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
      position    INTEGER NOT NULL,
      parent_id   TEXT REFERENCES arc(id) ON DELETE SET NULL
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
    // Who reads. Only the default row exists until profiles can be made; it is inserted
    // here as well as in migration 26 because a fresh database skips the migrations.
    r#"
    CREATE TABLE IF NOT EXISTS profile (
      id          TEXT PRIMARY KEY,
      name        TEXT NOT NULL,
      name_key    TEXT NOT NULL UNIQUE,
      restricted  INTEGER NOT NULL DEFAULT 0,
      max_age     INTEGER,
      code_digest BLOB,
      created_at  INTEGER NOT NULL
    )
    "#,
    "INSERT OR IGNORE INTO profile (id, name, name_key, restricted, max_age, code_digest, created_at)
     VALUES ('default', 'Par défaut', 'par défaut', 0, NULL, NULL, CAST(strftime('%s', 'now') AS INTEGER) * 1000)",
    // A linked device. Only the SHA-256 of its key is kept, so a copy of the index cannot
    // be used to read the library.
    r#"
    CREATE TABLE IF NOT EXISTS device (
      id         TEXT PRIMARY KEY,
      name       TEXT NOT NULL,
      name_key   TEXT NOT NULL UNIQUE,
      key_digest BLOB NOT NULL UNIQUE,
      linked_at  INTEGER NOT NULL,
      linked_by  TEXT NOT NULL,
      seen_at    INTEGER,
      profile_id TEXT NOT NULL DEFAULT 'default' REFERENCES profile(id)
    )
    "#,
    "CREATE INDEX IF NOT EXISTS ix_device_profile ON device(profile_id)",
    // Set by the first device ever linked and never cleared. Whether a keyless library on
    // the loopback is open cannot be read from `device`: revoking the last one would empty
    // it, open the library again, and let a key nobody holds any more read it. One row at
    // most, so the question is an `EXISTS`.
    r#"
    CREATE TABLE IF NOT EXISTS closed (
      id        INTEGER PRIMARY KEY CHECK (id = 1),
      closed_at INTEGER NOT NULL
    )
    "#,
    // A code waiting to be typed on another device. Its creator's own row goes with it, so
    // unlinking a device leaves no live code behind.
    r#"
    CREATE TABLE IF NOT EXISTS link_code (
      id             TEXT PRIMARY KEY,
      code_digest    BLOB NOT NULL UNIQUE,
      created_at     INTEGER NOT NULL,
      expires_at     INTEGER NOT NULL,
      created_by     TEXT NOT NULL,
      creator_device TEXT REFERENCES device(id) ON DELETE CASCADE,
      served_device  TEXT REFERENCES device(id) ON DELETE SET NULL
    )
    "#,
    // Progress belongs to the edition: reading chapter 100 in the Perfect Edition does
    // not mark it read in the original. And to whoever reads: two people in one household
    // do not share a place in the same volume.
    r#"
    CREATE TABLE IF NOT EXISTS progress (
      profile_id  TEXT NOT NULL DEFAULT 'default' REFERENCES profile(id) ON DELETE CASCADE,
      entry_id    TEXT NOT NULL REFERENCES entry(id) ON DELETE CASCADE,
      edition_id  TEXT NOT NULL REFERENCES edition(id) ON DELETE CASCADE,
      page        INTEGER NOT NULL,
      finished    INTEGER NOT NULL DEFAULT 0,
      times_finished INTEGER NOT NULL DEFAULT 0,
      updated_at  INTEGER NOT NULL,
      PRIMARY KEY (profile_id, entry_id)
    )
    "#,
    // Keyed by entry, but read by edition: the read status of a shelf asks "what has this
    // edition got" twice for every tile.
    "CREATE INDEX IF NOT EXISTS ix_progress_edition ON progress(edition_id, finished)",
    r#"
    CREATE TABLE IF NOT EXISTS reading_order (
      id          TEXT PRIMARY KEY,
      universe_id TEXT NOT NULL REFERENCES universe(id) ON DELETE CASCADE,
      declared_id TEXT NOT NULL,
      name        TEXT NOT NULL,
      position    INTEGER NOT NULL,
      is_default  INTEGER NOT NULL DEFAULT 0,
      UNIQUE (universe_id, declared_id)
    )
    "#,
    r#"
    CREATE TABLE IF NOT EXISTS reading_order_step (
      id         TEXT PRIMARY KEY,
      order_id   TEXT NOT NULL REFERENCES reading_order(id) ON DELETE CASCADE,
      position   INTEGER NOT NULL,
      work_id    TEXT NOT NULL REFERENCES work(id) ON DELETE CASCADE,
      unit       TEXT CHECK (unit IN ('CHAPTER','VOLUME')),
      edition_id TEXT REFERENCES edition(id) ON DELETE CASCADE,
      from_number REAL,
      to_number   REAL
    )
    "#,
    "CREATE INDEX IF NOT EXISTS ix_order_step ON reading_order_step(order_id, position)",
    "CREATE INDEX IF NOT EXISTS ix_order_universe ON reading_order(universe_id, position)",
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
/// **One entry is one migration**, its statements run in one transaction that also stamps
/// its number, so `user_version` is the number of the last migration applied and a crash
/// leaves a migration either whole or not begun. It used to count *entries*, and one
/// migration could span several: a production index at 41 was read by a person as
/// « migration 41 », when there were twenty-nine, and a crash between two entries of one
/// migration left it half applied — which is why 28 had already been folded into one.
///
/// Every migration must still survive being run again on a database that holds part or all
/// of it: an index from the old numbering is resumed at the last migration it completed,
/// and one it had begun runs again. `every_migration_survives_being_run_twice_in_a_row`
/// holds each one to that.
///
/// The index is rebuildable by rescanning, so the safe move for anything a migration
/// cannot express is still to delete the file and scan again. These exist so that stops
/// being necessary once progress is in there — that, a rescan cannot bring back.
///
/// **They are numbered by position and must never be reordered or removed**, because a
/// database on disk carries the number of the last one it ran.
pub const MIGRATIONS: &[&[&str]] = &[
    // 1 — an edition can read the other way round from its work.
    &[
        "ALTER TABLE edition ADD COLUMN reading_direction TEXT",
    ],
    // 2 — what the work is, so browsing can filter on it.
    &[
        "ALTER TABLE work ADD COLUMN medium TEXT",
        "ALTER TABLE edition ADD COLUMN medium TEXT",
    ],
    // 3 — when a volume joined the library. Set once and never again, because it cannot
    // be recovered afterwards: a column added later leaves everything before it blank
    // for ever.
    &[
        "ALTER TABLE entry ADD COLUMN added_at INTEGER",
        "UPDATE entry SET added_at = modified_at WHERE added_at IS NULL",
    ],
    // 4 — a cover chosen on disk rather than taken from page 0.
    &[
        "ALTER TABLE entry ADD COLUMN cover_file TEXT",
        "ALTER TABLE edition ADD COLUMN cover_file TEXT",
    ],
    // 5 — genres as rows, so the filter can name one exactly.
    &[
        r#"
        CREATE TABLE IF NOT EXISTS work_genre (
          work_id TEXT NOT NULL REFERENCES work(id) ON DELETE CASCADE,
          name    TEXT NOT NULL,
          key     TEXT NOT NULL,
          PRIMARY KEY (work_id, key)
        )
        "#,
        "CREATE INDEX IF NOT EXISTS work_genre_key ON work_genre(key)",
    ],
    // 6 — kind and ref are UNINDEXED columns of an FTS5 table, so deleting by them scanned
    // the whole index: reindexing n rows cost n² row visits. A plain table mapping
    // (kind, ref) to the FTS rowid turns each delete into a lookup.
    &[
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
    ],
    // 7 — what the row was written from, so a scan can tell that it has not changed.
    &[
        "ALTER TABLE search_ref ADD COLUMN digest TEXT",
    ],
    // 8 — which volume a loose chapter came from. A story reaches you as volumes, as loose
    // chapters, or as both in turn; without this the server calls a volume missing when it
    // holds the whole of its content under another name.
    &[
        "ALTER TABLE chapter ADD COLUMN volume REAL",
    ],
    // 9 — one record of a genre instead of two. work.genres held "a, b, c" for display
    // while work_genre held the rows the filter used, and the copy was the one that showed.
    &[
        "ALTER TABLE work DROP COLUMN genres",
    ],
    // 10 — the read status of a shelf queries progress by edition twice per tile.
    &[
        "CREATE INDEX IF NOT EXISTS ix_progress_edition ON progress(edition_id, finished)",
    ],
    // 11 — a column nothing ever wrote. It was there for a per-entry integrity check that
    // was never built; what actually needed checking was a file crossing the network, and
    // that is verified at the import instead, against the checksum the manifest announces.
    // A schema that claims a fact it does not hold is a schema that will be believed.
    &[
        "ALTER TABLE entry DROP COLUMN checksum",
    ],
    // 12 — arcs are read by edition on every series page, and there were none to read by.
    &[
        "CREATE INDEX IF NOT EXISTS ix_arc_edition ON arc(edition_id, position)",
    ],
    // 13 — the two foreign keys that had no index behind them, so deleting a work scanned
    // every edition and deleting a universe scanned every work.
    &[
        "CREATE INDEX IF NOT EXISTS ix_edition_work ON edition(work_id)",
        "CREATE INDEX IF NOT EXISTS ix_work_universe ON work(universe_id)",
    ],
    // 14 — one writer, tracked as its own fact instead of a free string sharing a column
    // with nothing else — the string a work.author held could never be searched a name at a
    // time, which is why "Obata" found nothing on Death Note even though he drew every page
    // of it. The table is created here as well as in SCHEMA (a no-op on a fresh database,
    // real work on one that predates it), and the column's content is carried into it before
    // the column itself goes: one record of the fact, never two, the way migration 9 did for
    // genres. The key is only lower-cased here rather than folded the way a scan folds it —
    // an old row is corrected the moment its work is scanned again, and nothing is lost
    // meanwhile.
    &[
        r#"
        CREATE TABLE IF NOT EXISTS work_author (
          work_id TEXT NOT NULL REFERENCES work(id) ON DELETE CASCADE,
          name    TEXT NOT NULL,
          key     TEXT NOT NULL,
          PRIMARY KEY (work_id, key)
        )
        "#,
        "CREATE INDEX IF NOT EXISTS work_author_key ON work_author(key)",
        r#"
        INSERT OR IGNORE INTO work_author (work_id, name, key)
        SELECT id, TRIM(author), LOWER(TRIM(author)) FROM work
        WHERE author IS NOT NULL AND TRIM(author) <> ''
        "#,
        "ALTER TABLE work DROP COLUMN author",
    ],
    // 15 — the illustrator, tracked apart from the writer for the first time: nothing before
    // this migration ever recorded one. Penciller, inker and cover artist are the same
    // person in all 25 series this was measured against, without one exception, so a table
    // of names serves where a credits(role, name) list would serve a case that does not
    // exist.
    &[
        r#"
        CREATE TABLE IF NOT EXISTS work_artist (
          work_id TEXT NOT NULL REFERENCES work(id) ON DELETE CASCADE,
          name    TEXT NOT NULL,
          key     TEXT NOT NULL,
          PRIMARY KEY (work_id, key)
        )
        "#,
        "CREATE INDEX IF NOT EXISTS work_artist_key ON work_artist(key)",
    ],
    // 16 — tags, beside genres and never folded into them: measured, Les Terres d'Arran
    // carries one genre and seven tags with nothing in common between the two lists.
    &[
        r#"
        CREATE TABLE IF NOT EXISTS work_tag (
          work_id TEXT NOT NULL REFERENCES work(id) ON DELETE CASCADE,
          name    TEXT NOT NULL,
          key     TEXT NOT NULL,
          PRIMARY KEY (work_id, key)
        )
        "#,
        "CREATE INDEX IF NOT EXISTS work_tag_key ON work_tag(key)",
    ],
    // 17 — an age rating, a free string and never an enum: "16+" at Kana, "T" elsewhere, and
    // neither is wrong.
    &[
        "ALTER TABLE work ADD COLUMN age_rating TEXT",
    ],
    // 18 — the publisher's imprint, a sibling of publisher rather than a replacement for it:
    // "Dark Kana" beside "Kana".
    &[
        "ALTER TABLE edition ADD COLUMN collection TEXT",
    ],
    // 19 — positive form, never blackAndWhite: a page carries colour or it does not.
    &[
        "ALTER TABLE edition ADD COLUMN colour INTEGER",
    ],
    // 20 — the named ways through a universe. Derived from `universe.json` at every scan and
    // rewritten whole, so nothing here is a source of truth: the file is.
    &[
        r#"
        CREATE TABLE IF NOT EXISTS reading_order (
          id          TEXT PRIMARY KEY,
          universe_id TEXT NOT NULL REFERENCES universe(id) ON DELETE CASCADE,
          declared_id TEXT NOT NULL,
          name        TEXT NOT NULL,
          position    INTEGER NOT NULL,
          is_default  INTEGER NOT NULL DEFAULT 0,
          UNIQUE (universe_id, declared_id)
        )
        "#,
    ],
    // 21 — one stretch of one work. `position` is explicit and not the row order: an order
    // is a sequence, and a sequence that depends on how rows happen to come back is a
    // sequence that changes the day an index does.
    //
    // `edition_id` is set on a VOLUME step and null on a CHAPTER one. The range bounds are
    // REAL because chapter numbers are: 12.5 is a chapter, and so is -1 for a prologue some
    // publishers number that way.
    &[
        r#"
        CREATE TABLE IF NOT EXISTS reading_order_step (
          id         TEXT PRIMARY KEY,
          order_id   TEXT NOT NULL REFERENCES reading_order(id) ON DELETE CASCADE,
          position   INTEGER NOT NULL,
          work_id    TEXT NOT NULL REFERENCES work(id) ON DELETE CASCADE,
          unit       TEXT CHECK (unit IN ('CHAPTER','VOLUME')),
          edition_id TEXT REFERENCES edition(id) ON DELETE CASCADE,
          from_number REAL,
          to_number   REAL
        )
        "#,
        "CREATE INDEX IF NOT EXISTS ix_order_step ON reading_order_step(order_id, position)",
        "CREATE INDEX IF NOT EXISTS ix_order_universe ON reading_order(universe_id, position)",
    ],
    // 22 — how many times an entry has been finished. `finished` says where the reader
    // stands now and cannot say whether they ever reached the end, which is why re-reading
    // a finished series made it look unfinished: the rewind that starts the second reading
    // clears the only mark the first one left.
    //
    // Backfilled from `finished` for the same reason as `added_at` above — a column added
    // later would claim every volume already read was never finished, and no rescan brings
    // that back.
    &[
        "ALTER TABLE progress ADD COLUMN times_finished INTEGER NOT NULL DEFAULT 0",
        // Only where nothing was counted yet: run again on an index that has gone on being
        // read, `= 1` alone flattened a volume read three times back to once.
        "UPDATE progress SET times_finished = 1 WHERE finished = 1 AND times_finished = 0",
    ],
    // 23 — a book that is a whole book. Declared by `oneshot.json` inside the archive and
    // never counted out of the shape: an album standing alone and a running series you own
    // one volume of are the same rows, and only somebody saying so tells them apart.
    &[
        "ALTER TABLE edition ADD COLUMN one_shot INTEGER NOT NULL DEFAULT 0",
    ],
    // 24 — the arc an arc sits inside. Declared in the sidecar by name and resolved here,
    // because two ranges containing one another are not a saga: the format lets arcs
    // overlap for reasons that have nothing to do with nesting.
    &[
        "ALTER TABLE arc ADD COLUMN parent_id TEXT REFERENCES arc(id) ON DELETE SET NULL",
    ],
    // 25 — what an edition says about itself. `edition.json` has declared a `summary` since
    // the format's first version and this server read it into a struct and dropped it on the
    // floor: no column, nothing written, nothing served. A format that promises a field and
    // silently loses it is worse than one that never offered it.
    //
    // It is a column of its own and not a second use of the work's, because the two are
    // different sentences: the work's says what the story is, and an edition's says what
    // *this printing* of it is — restored, coloured, three volumes in one.
    &[
        "ALTER TABLE edition ADD COLUMN summary TEXT",
    ],
    // 26 — who reads. Until now the server knew one reader, and `progress` was keyed by
    // entry alone. The default profile is the one every existing position is handed to.
    &[
        r#"
        CREATE TABLE IF NOT EXISTS profile (
          id          TEXT PRIMARY KEY,
          name        TEXT NOT NULL,
          name_key    TEXT NOT NULL UNIQUE,
          restricted  INTEGER NOT NULL DEFAULT 0,
          max_age     INTEGER,
          code_digest BLOB,
          created_at  INTEGER NOT NULL
        )
        "#,
        "INSERT OR IGNORE INTO profile (id, name, name_key, restricted, max_age, code_digest, created_at)
         VALUES ('default', 'Par défaut', 'par défaut', 0, NULL, NULL, CAST(strftime('%s', 'now') AS INTEGER) * 1000)",
    ],
    // 27 — the devices linked to this server, and the codes that link them. Created here
    // as well as in SCHEMA, which is a no-op on a fresh database.
    &[
        r#"
        CREATE TABLE IF NOT EXISTS device (
          id         TEXT PRIMARY KEY,
          name       TEXT NOT NULL,
          name_key   TEXT NOT NULL UNIQUE,
          key_digest BLOB NOT NULL UNIQUE,
          linked_at  INTEGER NOT NULL,
          linked_by  TEXT NOT NULL,
          seen_at    INTEGER,
          profile_id TEXT NOT NULL DEFAULT 'default' REFERENCES profile(id)
        )
        "#,
        "CREATE INDEX IF NOT EXISTS ix_device_profile ON device(profile_id)",
        r#"
        CREATE TABLE IF NOT EXISTS link_code (
          id             TEXT PRIMARY KEY,
          code_digest    BLOB NOT NULL UNIQUE,
          created_at     INTEGER NOT NULL,
          expires_at     INTEGER NOT NULL,
          created_by     TEXT NOT NULL,
          creator_device TEXT REFERENCES device(id) ON DELETE CASCADE,
          served_device  TEXT REFERENCES device(id) ON DELETE SET NULL
        )
        "#,
    ],
    // 28 — progress keyed by profile as well as entry. The key is part of the table, so it
    // is rebuilt rather than altered, and every row is handed to the default profile. These
    // rows are the one thing a rescan cannot bring back: a reader's places, kept nowhere
    // else, so the copy has to carry every column or it loses them.
    //
    // **One entry, run as one batch inside one transaction.** The migrator stamps the version
    // after each entry, so split in steps a crash between DROP and RENAME left `progress`
    // gone and the rows stranded in `progress_new`: the next start recreates an empty
    // `progress` from SCHEMA and the RENAME then fails for ever. Every migration is now one
    // entry and one transaction, which is the rule this one was the first to need.
    //
    // **Run again, it copies each row's own profile.** Selecting the constant `'default'`
    // handed a second reader's places to the default profile on a re-run, and failed on the
    // key wherever both had a place in the same volume. The owner is added first as a
    // column holding the default — the one error a re-run meets, a column already there, is
    // the one the migrator lets pass — so the copy reads `profile_id` whichever table it
    // finds. `progress_new` has no `IF NOT EXISTS` on purpose: a leftover one is where the
    // split version stranded the rows, and running over it would be the loss.
    &[
        "ALTER TABLE progress ADD COLUMN profile_id TEXT NOT NULL DEFAULT 'default'",
        r#"
        CREATE TABLE progress_new (
          profile_id  TEXT NOT NULL DEFAULT 'default' REFERENCES profile(id) ON DELETE CASCADE,
          entry_id    TEXT NOT NULL REFERENCES entry(id) ON DELETE CASCADE,
          edition_id  TEXT NOT NULL REFERENCES edition(id) ON DELETE CASCADE,
          page        INTEGER NOT NULL,
          finished    INTEGER NOT NULL DEFAULT 0,
          times_finished INTEGER NOT NULL DEFAULT 0,
          updated_at  INTEGER NOT NULL,
          PRIMARY KEY (profile_id, entry_id)
        )
        "#,
        r#"
        INSERT INTO progress_new
           (profile_id, entry_id, edition_id, page, finished, times_finished, updated_at)
         SELECT profile_id, entry_id, edition_id, page, finished, times_finished, updated_at
           FROM progress
        "#,
        "DROP TABLE progress",
        "ALTER TABLE progress_new RENAME TO progress",
        "CREATE INDEX IF NOT EXISTS ix_progress_edition ON progress(edition_id, finished)",
    ],
    // 29 — closing is for good. A database that already holds a device was closed the day
    // that device was linked, so it is stamped closed on the way in rather than found open
    // the day its last device is revoked.
    &[
        r#"
        CREATE TABLE IF NOT EXISTS closed (
          id        INTEGER PRIMARY KEY CHECK (id = 1),
          closed_at INTEGER NOT NULL
        )
        "#,
        r#"
        INSERT OR IGNORE INTO closed (id, closed_at)
         SELECT 1, MIN(linked_at) FROM device HAVING COUNT(*) > 0
        "#,
    ],
];

/// What a fresh database is stamped with, and the number of the last migration. Deriving it
/// from the list is what makes adding a migration a one-line change that cannot be got
/// wrong.
pub fn schema_version() -> i32 {
    MIGRATIONS.len() as i32
}

/// `PRAGMA application_id` of an index whose `user_version` numbers migrations: « Leaf » in
/// ASCII, 0x4C656166. It is what tells the two numberings apart — 25 meant « migration 25 »
/// to one and « 25 entries, half of migration 14 » to the other — so it is written in the
/// same transaction as the first version that means a migration, and never otherwise.
pub const LEAF_APPLICATION_ID: i32 = 0x4C65_6166;

/// How many entries each migration had in the old numbering, where `user_version` counted
/// entries of a flat list: migration 1 is the first number, migration 29 the last.
///
/// **Frozen. It must never change**, whatever the list above becomes: it describes indexes
/// already on disk, written by builds that no longer exist. Read entry by entry from
/// `server/src/store/schema.rs` in git — 40 entries for migrations 1 to 25 at d73ae1c, 47
/// for 1 to 29 at 4aa1c8b, and every commit of main in between a prefix of the same list.
/// No release ever counted past 47, so a higher count is refused rather than guessed at. A
/// build that went further in the old numbering, and whose index must still be read, gets
/// its own entries appended here, one term per migration, in the order it ran them; that is
/// the only edit this table may take.
const OLD_ENTRIES_PER_MIGRATION: [i32; 29] = [
    1, 2, 2, 2, 2, 3, 1, 1, 1, 1, 1, 1, 2, 4, 2, 2, 1, 1, 1, 1, 3, 2, 1, 1, 1, 2, 3, 1, 1,
];

/// The last migration an index in the old numbering had **wholly** applied, from its entry
/// count. A count falling inside a migration means that migration was begun and not
/// finished, so the one before is the last complete one and the begun one runs again.
///
/// `None` for a count no release ever wrote — negative, or past 47 — which is not guessed
/// at: a branch build counting further had a shape this table does not describe.
pub fn last_migration_of_old_count(entries: i32) -> Option<i32> {
    if entries < 0 {
        return None;
    }
    let mut counted = 0;
    let mut last = 0;
    for (number, size) in (1..).zip(OLD_ENTRIES_PER_MIGRATION) {
        if counted + size > entries {
            return Some(last);
        }
        counted += size;
        last = number;
    }
    (counted == entries).then_some(last)
}
