//! The read side of the API.
//!
//! A "series" in API terms is an edition — the level that carries chapters and progress.
//! Work and universe are display groupings only; they surface inside the composed name.
//!
//! The SQL is ported from the Kotlin as it stands. It is the part of that server most worth
//! keeping unchanged: it is where the model lives, it has cost tests behind it, and every
//! shape in it was arrived at by fixing something.

use anyhow::Result;
use rusqlite::types::Value;
use rusqlite::Row;

use crate::api::dto::{
    ArcDto, ChapterDto, EntryDto, FacetDto, FacetsDto, PageDto, SearchHitDto, SeriesDto,
    SeriesFilter, SeriesSort, UNREAD,
};
use crate::store::text::{composed_name, gaps, nearest, search_key, tolerance};
use crate::store::{Cx, Db};

/// Where a reader stands in a series, worked out from the progress kept per volume.
///
/// Nothing started is UNREAD; every volume finished is READ; anything between is
/// IN_PROGRESS. Written once and used three times — in the list, in the filter and in the
/// counts — so those three can never tell different stories about the same shelf.
///
/// A volume counts as started once a page has been turned or it has been finished: opening
/// a book and closing it on page zero is not reading it.
const READ_STATUS: &str = "CASE
    WHEN (SELECT COUNT(*) FROM progress p
          WHERE p.edition_id = e.id AND (p.finished = 1 OR p.page > 0)) = 0 THEN 'UNREAD'
    WHEN (SELECT COUNT(*) FROM progress p WHERE p.edition_id = e.id AND p.finished = 1)
         >= (SELECT COUNT(*) FROM entry x WHERE x.edition_id = e.id) THEN 'READ'
    ELSE 'IN_PROGRESS'
  END";

/// Well under SQLite's parameter ceiling, and above any page the API hands out.
const CHUNK: usize = 400;

pub struct Repository<'a> {
    db: &'a Db,
}

/// A WHERE clause and the values it binds, built once and used by both the page and its
/// total — so the two cannot disagree about what they are counting.
struct Where {
    sql: String,
    args: Vec<Value>,
}

impl<'a> Repository<'a> {
    pub fn new(db: &'a Db) -> Self {
        Repository { db }
    }

    fn build_where(filter: &SeriesFilter) -> Where {
        let mut clauses: Vec<String> = Vec::new();
        let mut args: Vec<Value> = Vec::new();

        let mut keep = |column: &str, values: &[String]| {
            if values.is_empty() {
                return;
            }
            clauses.push(format!(
                "{column} COLLATE NOCASE IN ({})",
                marks(values.len())
            ));
            args.extend(values.iter().map(|v| Value::Text(v.clone())));
        };

        keep("e.id", &filter.ids);
        // The editions of one work — how "the other editions of this one" is asked for.
        keep("w.id", &filter.works);
        keep("u.name", &filter.universes);
        keep("w.author", &filter.authors);
        keep("COALESCE(e.medium, w.medium)", &filter.media);
        keep("COALESCE(e.status, w.status)", &filter.statuses);
        keep("e.language", &filter.languages);
        keep(READ_STATUS, &filter.read_statuses);
        keep("e.publisher", &filter.publishers);

        if !filter.genres.is_empty() {
            // Folded, because a genre is typed by a human somewhere: "Shônen" and "shonen"
            // are the same filter.
            clauses.push(format!(
                "EXISTS (SELECT 1 FROM work_genre g WHERE g.work_id = w.id AND g.key IN ({}))",
                marks(filter.genres.len())
            ));
            args.extend(filter.genres.iter().map(|g| Value::Text(search_key(g))));
        }

        Where {
            sql: if clauses.is_empty() {
                String::new()
            } else {
                format!("WHERE {}", clauses.join(" AND "))
            },
            args,
        }
    }

    /// How many series a filter matches, without building a single one of them.
    ///
    /// What lets a client show a page and still know what it has not asked for yet.
    pub fn count_series(&self, filter: &SeriesFilter) -> Result<i64> {
        let w = Self::build_where(filter);
        let sql = format!(
            "SELECT COUNT(*) FROM edition e
             JOIN work w ON w.id = e.work_id
             LEFT JOIN universe u ON u.id = w.universe_id
             {}",
            w.sql
        );
        self.db.read(|cx| {
            Ok(cx
                .query_one(&sql, rusqlite::params_from_iter(w.args.iter()), |r| {
                    r.get::<_, i64>(0)
                })?
                .unwrap_or(0))
        })
    }

    /// The series, optionally narrowed, ordered and paged.
    ///
    /// Filtering happens here rather than in the client because the client would otherwise
    /// have to hold the whole library to filter it — which works at six series and stops
    /// working long before the library stops growing.
    ///
    /// `limit` of 0 asks for every match; anything else bounds what crosses the wire.
    pub fn series(
        &self,
        filter: &SeriesFilter,
        sort: SeriesSort,
        limit: i64,
        offset: i64,
    ) -> Result<Vec<SeriesDto>> {
        let w = Self::build_where(filter);
        let mut args = w.args;
        let window = if limit <= 0 {
            String::new()
        } else {
            args.push(Value::Integer(limit));
            args.push(Value::Integer(offset.max(0)));
            "LIMIT ? OFFSET ?".to_string()
        };

        let sql = format!(
            "SELECT e.id, e.name AS edition, COALESCE(e.status, w.status) AS status, e.volume_count,
                    e.publisher, e.language,
                    w.id AS work_id, w.name AS work,
                    w.author, COALESCE(e.medium, w.medium) AS medium,
                    COALESCE(e.reading_direction, w.reading_direction) AS reading_direction,
                    u.name AS universe,
                    {READ_STATUS} AS read_status,
                    (SELECT COUNT(*) FROM entry   x WHERE x.edition_id = e.id) AS entry_count,
                    (SELECT COUNT(*) FROM chapter c WHERE c.edition_id = e.id) AS chapter_count,
                    (SELECT COUNT(*) FROM arc     a WHERE a.edition_id = e.id) AS arc_count,
                    (SELECT MIN(x.added_at) FROM entry x WHERE x.edition_id = e.id) AS added_at,
                    (SELECT MAX(x.added_at) FROM entry x WHERE x.edition_id = e.id) AS last_added_at
             FROM edition e
             JOIN work w ON w.id = e.work_id
             LEFT JOIN universe u ON u.id = w.universe_id
             {}
             ORDER BY {}
             {window}",
            w.sql,
            sort.sql()
        );

        let rows = self.db.read(|cx| {
            cx.query(&sql, rusqlite::params_from_iter(args.iter()), |r| {
                let universe: Option<String> = r.get("universe")?;
                let work: String = r.get("work")?;
                let edition: Option<String> = r.get("edition")?;
                // The four collection facts are filled in below, once, for the whole page.
                Ok(SeriesDto {
                    id: r.get("id")?,
                    work_id: r.get("work_id")?,
                    name: composed_name(universe.as_deref(), &work, edition.as_deref()),
                    universe,
                    work,
                    edition,
                    author: r.get("author")?,
                    medium: r.get("medium")?,
                    reading_direction: r.get("reading_direction")?,
                    status: r.get("status")?,
                    read_status: r.get("read_status")?,
                    declared_volumes: r.get("volume_count")?,
                    owned_volumes: 0,
                    missing_volumes: Vec::new(),
                    missing_chapters: Vec::new(),
                    entry_count: r.get("entry_count")?,
                    chapter_count: r.get("chapter_count")?,
                    arc_count: r.get("arc_count")?,
                    genres: Vec::new(),
                    publisher: r.get("publisher")?,
                    language: r.get("language")?,
                    added_at: r.get("added_at")?,
                    last_added_at: r.get("last_added_at")?,
                })
            })
        })?;

        if rows.is_empty() {
            return Ok(rows);
        }

        // Read in one go, after the rows and bounded by them: asking per series turned a
        // list of two hundred into two hundred and one queries, and reading the whole
        // chapter table to answer about one series was the same defect wearing the other
        // hat.
        let edition_ids: Vec<String> = rows.iter().map(|s| s.id.clone()).collect();
        let mut work_ids: Vec<String> = rows.iter().map(|s| s.work_id.clone()).collect();
        work_ids.sort();
        work_ids.dedup();

        let volumes = self.all_volume_numbers(&edition_ids)?;
        let claimed = self.claimed_volumes(&edition_ids)?;
        let chapter_gaps = self.missing_chapters(&edition_ids)?;
        let genres = self.genres_of(&work_ids)?;

        Ok(rows
            .into_iter()
            .map(|mut s| {
                let owned = volumes.get(&s.id).cloned().unwrap_or_default();
                let none = Vec::new();
                s.missing_volumes = gaps(
                    &owned,
                    s.declared_volumes,
                    claimed.get(&s.id).unwrap_or(&none),
                );
                s.owned_volumes = owned.len() as i64;
                s.missing_chapters = chapter_gaps.get(&s.id).cloned().unwrap_or_default();
                s.genres = genres.get(&s.work_id).cloned().unwrap_or_default();
                s
            })
            .collect())
    }

    /// One series, asked for by id.
    ///
    /// Reached through the filter rather than by building every series and throwing all but
    /// one away — which is what this used to do, so opening a single series cost exactly as
    /// much as listing the whole library.
    pub fn one_series(&self, id: &str) -> Result<Option<SeriesDto>> {
        Ok(self
            .series(&SeriesFilter::of(id), SeriesSort::Name, 0, 0)?
            .into_iter()
            .next())
    }

    /// The editions a filter admits — the set a search is then confined to.
    pub fn editions_matching(&self, filter: &SeriesFilter) -> Result<Vec<String>> {
        let w = Self::build_where(filter);
        let sql = format!(
            "SELECT e.id FROM edition e
             JOIN work w ON w.id = e.work_id
             LEFT JOIN universe u ON u.id = w.universe_id
             {}",
            w.sql
        );
        self.db.read(|cx| {
            cx.query(&sql, rusqlite::params_from_iter(w.args.iter()), |r| {
                r.get::<_, String>(0)
            })
        })
    }

    /// The volumes loose chapters say they came from, per edition.
    fn claimed_volumes(&self, ids: &[String]) -> Result<Grouped<f64>> {
        self.grouped(ids, |cx, part| {
            cx.query(
                &format!(
                    "SELECT DISTINCT edition_id, volume FROM chapter
                     WHERE volume IS NOT NULL AND edition_id IN ({})",
                    marks(part.len())
                ),
                rusqlite::params_from_iter(part.iter()),
                |r| Ok((r.get::<_, String>(0)?, r.get::<_, f64>(1)?)),
            )
        })
    }

    /// The chapters missing from the sequence, per edition.
    ///
    /// Once a volume can arrive as loose chapters, a gap has two granularities. A volume is
    /// only absent when neither it nor its chapters are here; and the chapters count
    /// themselves — 683, 684, 685, 687 says the 686 is missing, and says it without knowing
    /// anything about volumes.
    ///
    /// A false gap here is worth having: it means a volume you own declares no chapter
    /// markers, which is something to fix rather than to hide.
    fn missing_chapters(&self, ids: &[String]) -> Result<Grouped<f64>> {
        let numbers = self.grouped(ids, |cx, part| {
            cx.query(
                &format!(
                    "SELECT edition_id, number FROM chapter
                     WHERE number IS NOT NULL AND edition_id IN ({})",
                    marks(part.len())
                ),
                rusqlite::params_from_iter(part.iter()),
                |r| Ok((r.get::<_, String>(0)?, r.get::<_, f64>(1)?)),
            )
        })?;
        Ok(numbers
            .into_iter()
            .map(|(id, values)| {
                let missing = gaps(&values, None, &[]);
                (id, missing)
            })
            .collect())
    }

    fn all_volume_numbers(&self, ids: &[String]) -> Result<Grouped<f64>> {
        let mut found = self.grouped(ids, |cx, part| {
            cx.query(
                &format!(
                    "SELECT edition_id, volume_number FROM entry
                     WHERE type = 'VOLUME' AND volume_number IS NOT NULL AND edition_id IN ({})",
                    marks(part.len())
                ),
                rusqlite::params_from_iter(part.iter()),
                |r| Ok((r.get::<_, String>(0)?, r.get::<_, f64>(1)?)),
            )
        })?;
        for values in found.values_mut() {
            values.sort_by(|a, b| a.partial_cmp(b).expect("volume numbers are never NaN"));
        }
        Ok(found)
    }

    /// The genres of several works at once, from the table that also filters them.
    ///
    /// They used to be read from `work.genres`, a comma-joined copy kept beside this table:
    /// two records of one fact, and the copy was the one that showed. It went wrong exactly
    /// where you would expect — genres inherited from a ComicInfo reached the column and not
    /// the table, so they were displayed and could not be filtered on.
    ///
    /// Alphabetical, because a set has no order of its own once it is rows.
    fn genres_of(&self, work_ids: &[String]) -> Result<Grouped<String>> {
        self.grouped(work_ids, |cx, part| {
            cx.query(
                &format!(
                    "SELECT work_id, name FROM work_genre WHERE work_id IN ({}) ORDER BY name",
                    marks(part.len())
                ),
                rusqlite::params_from_iter(part.iter()),
                |r| Ok((r.get::<_, String>(0)?, r.get::<_, String>(1)?)),
            )
        })
    }

    /// Runs a lookup for a list of ids and groups what comes back by that id.
    ///
    /// In chunks, because the list is a page of results and a statement carries a bounded
    /// number of parameters — a page of five hundred is fine, "give me everything" is not.
    fn grouped<T, F>(&self, ids: &[String], query: F) -> Result<Grouped<T>>
    where
        F: Fn(&Cx<'_>, &[String]) -> Result<Vec<(String, T)>>,
    {
        if ids.is_empty() {
            return Ok(Grouped::new());
        }
        self.db.read(|cx| {
            let mut out: Grouped<T> = Grouped::new();
            for part in ids.chunks(CHUNK) {
                for (key, value) in query(cx, part)? {
                    out.entry(key).or_default().push(value);
                }
            }
            Ok(out)
        })
    }

    // ------------------------------------------------------------------ facets

    /// The values worth offering as filters, each with the number of series behind it.
    ///
    /// Counted by the database rather than by building every series and grouping them in
    /// memory: a filter menu is asked for on every visit to the browsing screen, and it has
    /// no business reading the whole library to say that four series are science fiction.
    ///
    /// The expressions are the ones [`Repository::series`] filters on, COALESCE included, so
    /// a value can never be offered that the filter would then fail to match.
    pub fn facets(&self) -> Result<FacetsDto> {
        Ok(FacetsDto {
            read_statuses: self.facet(READ_STATUS)?,
            universes: self.facet("u.name")?,
            authors: self.facet("w.author")?,
            genres: self.genre_facet()?,
            media: self.facet("COALESCE(e.medium, w.medium)")?,
            statuses: self.facet("COALESCE(e.status, w.status)")?,
            languages: self.facet("e.language")?,
            publishers: self.facet("e.publisher")?,
        })
    }

    fn facet(&self, expression: &str) -> Result<Vec<FacetDto>> {
        let sql = format!(
            "SELECT {expression} AS value, COUNT(*) AS n
             FROM edition e
             JOIN work w ON w.id = e.work_id
             LEFT JOIN universe u ON u.id = w.universe_id
             WHERE TRIM(COALESCE({expression}, '')) <> ''
             GROUP BY value
             ORDER BY n DESC, LOWER(value)"
        );
        self.db.read(|cx| {
            cx.query(&sql, [], |r| {
                Ok(FacetDto {
                    value: r.get::<_, String>(0)?.trim().to_string(),
                    count: r.get(1)?,
                })
            })
        })
    }

    /// Grouped on the folded key, shown under the spelling the files use.
    fn genre_facet(&self) -> Result<Vec<FacetDto>> {
        self.db.read(|cx| {
            cx.query(
                "SELECT MIN(g.name) AS value, COUNT(*) AS n
                 FROM work_genre g
                 JOIN edition e ON e.work_id = g.work_id
                 GROUP BY g.key
                 ORDER BY n DESC, LOWER(value)",
                [],
                |r| {
                    Ok(FacetDto {
                        value: r.get::<_, String>(0)?.trim().to_string(),
                        count: r.get(1)?,
                    })
                },
            )
        })
    }

    // ----------------------------------------------------------------- entries

    /// Ordered by sort key: the first chapter's number, otherwise the volume number.
    pub fn entries(&self, edition_id: &str) -> Result<Vec<EntryDto>> {
        self.db.read(|cx| {
            cx.query(
                "SELECT e.*, (SELECT COUNT(*) FROM chapter c WHERE c.entry_id = e.id) AS chapter_count
                 FROM entry e WHERE e.edition_id = ?1
                 ORDER BY e.sort_key, e.file",
                [edition_id],
                to_entry,
            )
        })
    }

    pub fn entry(&self, id: &str) -> Result<Option<EntryDto>> {
        self.db.read(|cx| {
            cx.query_one(
                "SELECT e.*, (SELECT COUNT(*) FROM chapter c WHERE c.entry_id = e.id) AS chapter_count
                 FROM entry e WHERE e.id = ?1",
                [id],
                to_entry,
            )
        })
    }

    /// A volume's markers, in reading order.
    pub fn chapters_of_entry(&self, entry_id: &str) -> Result<Vec<ChapterDto>> {
        self.db.read(|cx| {
            cx.query(
                "SELECT * FROM chapter WHERE entry_id = ?1 ORDER BY position",
                [entry_id],
                to_chapter,
            )
        })
    }

    /// The whole edition sequence, volumes and standalone chapters alike.
    pub fn chapters_of_edition(&self, edition_id: &str) -> Result<Vec<ChapterDto>> {
        self.db.read(|cx| {
            cx.query(
                "SELECT * FROM chapter WHERE edition_id = ?1 ORDER BY position",
                [edition_id],
                to_chapter,
            )
        })
    }

    pub fn arcs(&self, edition_id: &str) -> Result<Vec<ArcDto>> {
        self.db.read(|cx| {
            cx.query(
                "SELECT * FROM arc WHERE edition_id = ?1 ORDER BY position",
                [edition_id],
                |r| {
                    Ok(ArcDto {
                        id: r.get("id")?,
                        name: r.get("name")?,
                        unit: r.get("unit")?,
                        from: r.get("from_number")?,
                        to: r.get("to_number")?,
                        position: r.get("position")?,
                    })
                },
            )
        })
    }

    pub fn pages(&self, entry_id: &str) -> Result<Vec<PageDto>> {
        self.db.read(|cx| {
            cx.query(
                "SELECT * FROM page WHERE entry_id = ?1 ORDER BY number",
                [entry_id],
                |r| {
                    let width: Option<i64> = r.get("width")?;
                    let height: Option<i64> = r.get("height")?;
                    Ok(PageDto {
                        number: r.get("number")?,
                        name: r.get("entry_name")?,
                        media_type: r.get("media_type")?,
                        width,
                        height,
                        size: r.get("size")?,
                        spread: matches!((width, height), (Some(w), Some(h)) if w > h),
                    })
                },
            )
        })
    }

    /// The work folder a series belongs to, so a rescan can be aimed at it rather than at
    /// the whole library.
    pub fn work_folder_of_series(&self, edition_id: &str) -> Result<Option<String>> {
        self.db.read(|cx| {
            cx.query_one(
                "SELECT w.path FROM edition e JOIN work w ON w.id = e.work_id WHERE e.id = ?1",
                [edition_id],
                |r| r.get::<_, String>(0),
            )
        })
    }

    /// Same, from an entry.
    pub fn work_folder_of_entry(&self, entry_id: &str) -> Result<Option<String>> {
        self.db.read(|cx| {
            cx.query_one(
                "SELECT w.path FROM entry x
                 JOIN edition e ON e.id = x.edition_id
                 JOIN work w ON w.id = e.work_id
                 WHERE x.id = ?1",
                [entry_id],
                |r| r.get::<_, String>(0),
            )
        })
    }

    pub fn entry_path(&self, id: &str) -> Result<Option<String>> {
        self.db.read(|cx| {
            cx.query_one("SELECT file FROM entry WHERE id = ?1", [id], |r| {
                r.get::<_, String>(0)
            })
        })
    }

    /// Every series' display name, in one query.
    ///
    /// Read once by the search rather than inside the row mapper: as a computed property
    /// this ran the whole series query again for every single hit.
    pub fn series_names(&self) -> Result<std::collections::HashMap<String, String>> {
        self.db.read(|cx| {
            let rows = cx.query(
                "SELECT e.id, e.name AS edition, w.name AS work, u.name AS universe
                 FROM edition e
                 JOIN work w ON w.id = e.work_id
                 LEFT JOIN universe u ON u.id = w.universe_id",
                [],
                |r| {
                    let universe: Option<String> = r.get("universe")?;
                    let work: String = r.get("work")?;
                    let edition: Option<String> = r.get("edition")?;
                    Ok((
                        r.get::<_, String>("id")?,
                        composed_name(universe.as_deref(), &work, edition.as_deref()),
                    ))
                },
            )?;
            Ok(rows.into_iter().collect())
        })
    }
}

type Grouped<T> = std::collections::HashMap<String, Vec<T>>;

fn to_entry(r: &Row<'_>) -> rusqlite::Result<EntryDto> {
    let file: String = r.get("file")?;
    Ok(EntryDto {
        id: r.get("id")?,
        kind: r.get("type")?,
        number: r.get("volume_number")?,
        title: r.get("title")?,
        sort_key: r.get("sort_key")?,
        page_count: r.get("page_count")?,
        chapter_count: r.get("chapter_count")?,
        // The file name alone, never a path: the client has no use for where it sits on
        // this machine, and it is not a thing to hand out.
        file: file.rsplit('/').next().unwrap_or(&file).to_string(),
        size: r.get("size")?,
        isbn: r.get("isbn")?,
        published_on: r.get("published_on")?,
        summary: r.get("summary")?,
        added_at: r.get("added_at")?,
        own_cover: r.get::<_, Option<String>>("cover_file")?.is_some(),
    })
}

fn to_chapter(r: &Row<'_>) -> rusqlite::Result<ChapterDto> {
    Ok(ChapterDto {
        id: r.get("id")?,
        raw: r.get("raw")?,
        label: r.get("label")?,
        number: r.get("number")?,
        title: r.get("title")?,
        kind: r.get("kind")?,
        position: r.get("position")?,
        start_page: r.get("start_page")?,
        entry_id: r.get("entry_id")?,
    })
}

/// As many placeholders as there are values, which is the only safe way to write an IN.
pub fn marks(count: usize) -> String {
    std::iter::repeat_n("?", count)
        .collect::<Vec<_>>()
        .join(",")
}

/// Kept so a reader of `read_status` can compare against something named.
pub const UNREAD_STATUS: &str = UNREAD;

/// The three things a search can answer with. A universe and a work are not among them.
const KINDS: [&str; 3] = ["EDITION", "ENTRY", "CHAPTER"];

impl Repository<'_> {
    /// Find something by name.
    ///
    /// Every term has to appear, as a word or as the start of one, and results come back by
    /// relevance rather than by an arbitrary order.
    ///
    /// The terms are folded the same way the index was written — accents, case, apostrophes
    /// — so "ecarlate" reaches "écarlate" and "lattaque" reaches "L'Attaque". Each is turned
    /// into a prefix so that a half-typed word already finds something.
    ///
    /// Two things weigh on the ranking. A hit on a name counts far more than one buried in
    /// the rest of the text; and a level counts too, so that searching "Parasite" answers
    /// with the edition before the eleven chapters that happen to say the word. Relevance
    /// stays the primary signal — the level only tilts it.
    pub fn search(
        &self,
        query: &str,
        limit: i64,
        kinds: &[String],
        filter: &SeriesFilter,
    ) -> Result<Vec<SearchHitDto>> {
        let terms: Vec<String> = search_key(query)
            .split(' ')
            .filter(|t| !t.is_empty())
            .map(str::to_string)
            .collect();
        if terms.is_empty() {
            return Ok(Vec::new());
        }

        let expression = terms
            .iter()
            .map(|t| format!("\"{t}\"*"))
            .collect::<Vec<_>>()
            .join(" AND ");
        let wanted: Vec<String> = kinds
            .iter()
            .map(|k| k.to_uppercase())
            .filter(|k| KINDS.contains(&k.as_str()))
            .collect();

        // Every indexed row belongs to an edition — a work is not a result of its own — so
        // the filters apply by restricting which editions are allowed to answer.
        let within = if filter.is_empty() {
            None
        } else {
            let allowed = self.editions_matching(filter)?;
            if allowed.is_empty() {
                return Ok(Vec::new());
            }
            Some(allowed)
        };

        // Read once, here, and not inside the row mapper: as a computed property this ran
        // the whole series query again for every single hit.
        let names = self.series_names()?;

        let mut args: Vec<Value> = vec![Value::Text(expression)];
        let only_kinds = if wanted.is_empty() {
            String::new()
        } else {
            args.extend(wanted.iter().map(|k| Value::Text(k.clone())));
            format!("AND kind IN ({})", marks(wanted.len()))
        };
        let only_editions = match &within {
            None => String::new(),
            Some(ids) => {
                args.extend(ids.iter().map(|i| Value::Text(i.clone())));
                format!("AND edition_id IN ({})", marks(ids.len()))
            }
        };

        let sql = format!(
            "SELECT kind, ref, label, edition_id, entry_id
             FROM search
             WHERE search MATCH ? {only_kinds} {only_editions}
             ORDER BY bm25(search, 10.0, 1.0) * CASE kind
                        WHEN 'EDITION' THEN 3.0 WHEN 'ENTRY' THEN 1.5 ELSE 1.0
                      END
             LIMIT {}",
            limit.clamp(1, 200)
        );

        let hits = self.db.read(|cx| {
            cx.query(&sql, rusqlite::params_from_iter(args.iter()), |r| {
                let kind: String = r.get("kind")?;
                let reference: String = r.get("ref")?;
                let edition_id: Option<String> = r.get("edition_id")?;
                // An edition wears the name the grid gives it — "Parasite · Édition Deluxe",
                // not the bare "Édition Deluxe" its folder is called. Read from the same
                // place the tiles are, so a result and the thing it opens are never
                // labelled differently.
                let label = if kind == "EDITION" {
                    names.get(&reference).cloned()
                } else {
                    None
                };
                Ok(SearchHitDto {
                    label: label.unwrap_or(r.get("label")?),
                    kind,
                    id: reference,
                    series_name: edition_id.as_ref().and_then(|e| names.get(e).cloned()),
                    series_id: edition_id,
                    entry_id: r.get("entry_id")?,
                    approximate: false,
                })
            })
        })?;

        // A client that asked for chapters and got none wants "no chapters", not "here is
        // a series you might have meant". The guess is series-only by construction — it
        // reads what it compares, so what it reads has to stay bounded by the shelf — so it
        // is offered only when a series was among the things asked for.
        if hits.is_empty() && (wanted.is_empty() || wanted.iter().any(|k| k == "EDITION")) {
            return self.approximate(&terms, &names, limit, within.as_deref());
        }
        Ok(hits)
    }

    /// What you might have meant, when nothing matched.
    ///
    /// Only ever reached on an empty result, so a search that works pays nothing for this.
    /// The words in the index are read once and compared to yours with a bounded edit
    /// distance — enough to carry "Ohba" to "Ōba", which the ordinary search cannot do since
    /// folding an accent is not the same as forgiving a letter.
    ///
    /// Series only, whatever was asked for. Two reasons, and they point the same way: a
    /// guess is worth offering about the series you were looking for, not about the fiftieth
    /// chapter that nearly spells it — and it means comparing your words against two hundred
    /// rows rather than fifty thousand. This pass has no index behind it; it reads what it
    /// compares, so what it reads has to stay bounded by the shelf.
    ///
    /// The answers come back marked, and they must stay marked. Mixed in unlabelled, a guess
    /// reads exactly like a match, and a search that quietly invents plausible answers is
    /// worse than one that admits it found nothing.
    fn approximate(
        &self,
        terms: &[String],
        names: &std::collections::HashMap<String, String>,
        limit: i64,
        within: Option<&[String]>,
    ) -> Result<Vec<SearchHitDto>> {
        if terms.iter().all(|t| tolerance(t) == 0) {
            return Ok(Vec::new());
        }

        let mut args: Vec<Value> = Vec::new();
        let only_editions = match within {
            None => String::new(),
            Some(ids) => {
                args.extend(ids.iter().map(|i| Value::Text(i.clone())));
                format!("AND edition_id IN ({})", marks(ids.len()))
            }
        };
        let sql = format!(
            "SELECT kind, ref, label, edition_id, entry_id, name, detail
             FROM search WHERE kind = 'EDITION' {only_editions}"
        );

        let rows = self.db.read(|cx| {
            cx.query(&sql, rusqlite::params_from_iter(args.iter()), |r| {
                let name: Option<String> = r.get("name")?;
                let detail: Option<String> = r.get("detail")?;
                Ok((
                    SearchHitDto {
                        kind: r.get("kind")?,
                        id: r.get("ref")?,
                        label: r.get("label")?,
                        series_id: r.get("edition_id")?,
                        series_name: None,
                        entry_id: r.get("entry_id")?,
                        approximate: true,
                    },
                    format!(
                        "{} {}",
                        name.unwrap_or_default(),
                        detail.unwrap_or_default()
                    ),
                ))
            })
        })?;

        let mut scored: Vec<(SearchHitDto, usize)> = rows
            .into_iter()
            .filter_map(|(hit, words)| {
                let candidates: Vec<&str> = words.split(' ').filter(|w| !w.is_empty()).collect();
                // Every term has to land somewhere, or it is not the same query at all.
                let mut cost = 0;
                for term in terms {
                    cost += nearest(term, candidates.iter().copied())?;
                }
                Some((hit, cost))
            })
            .collect();

        scored.sort_by_key(|(_, cost)| *cost);
        Ok(scored
            .into_iter()
            .take(limit.clamp(1, 20) as usize)
            .map(|(mut hit, _)| {
                hit.label = names.get(&hit.id).cloned().unwrap_or(hit.label);
                hit.series_name = hit.series_id.as_ref().and_then(|e| names.get(e).cloned());
                hit
            })
            .collect())
    }
}

impl Repository<'_> {
    /// The markers of several entries at once — what "up next" needs and asked for row by
    /// row.
    pub fn chapters_of_entries(&self, entry_ids: &[String]) -> Result<Grouped<ChapterDto>> {
        self.grouped(entry_ids, |cx, part| {
            cx.query(
                &format!(
                    "SELECT * FROM chapter WHERE entry_id IN ({}) ORDER BY position",
                    marks(part.len())
                ),
                rusqlite::params_from_iter(part.iter()),
                |r| Ok((r.get::<_, String>("entry_id")?, to_chapter(r)?)),
            )
        })
    }

    /// Several entries at once, by id.
    pub fn entries_by_ids(
        &self,
        ids: &[String],
    ) -> Result<std::collections::HashMap<String, EntryDto>> {
        let found = self.grouped(ids, |cx, part| {
            cx.query(
                &format!(
                    "SELECT e.*, (SELECT COUNT(*) FROM chapter c WHERE c.entry_id = e.id) AS chapter_count
                     FROM entry e WHERE e.id IN ({})",
                    marks(part.len())
                ),
                rusqlite::params_from_iter(part.iter()),
                |r| Ok((r.get::<_, String>("id")?, to_entry(r)?)),
            )
        })?;
        Ok(found
            .into_iter()
            .filter_map(|(id, mut entries)| entries.pop().map(|e| (id, e)))
            .collect())
    }
}
