//! Progress, and what to read next.
//!
//! It belongs to the edition: reading chapter 100 in the Perfect Edition does not mark it
//! read in the original. Editions do not even hold the same bonus chapters, so a shared
//! numbering would be wrong from the first one.
//!
//! This is also the first data that exists nowhere but in the index. Everything else can be
//! rebuilt by rescanning the files; this cannot — which is the whole reason migrations
//! matter at all.

use anyhow::Result;
use rusqlite::types::Value;
use serde::{Deserialize, Serialize};

use crate::api::dto::{ChapterDto, EntryDto};
use crate::store::repository::marks;
use crate::store::{Db, Repository};

/// Well under SQLite's parameter ceiling, and above any list of entries a screen shows.
const CHUNK: usize = 400;

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ProgressDto {
    pub entry_id: String,
    pub page: i64,
    pub page_count: i64,
    pub finished: bool,
    pub updated_at: i64,
    /// Where that page falls in the chapters. Derived from the markers rather than stored:
    /// two records of the same fact would eventually disagree, and this one costs a lookup.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub chapter: Option<ChapterDto>,
}

#[derive(Debug, Default, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ProgressPatch {
    pub page: Option<i64>,
    pub finished: Option<bool>,
    /// Progress never moves backwards on its own. That is the rule that makes the offline
    /// queue safe: a phone replaying yesterday's positions cannot undo today's reading. But
    /// re-reading a volume is a normal thing to do, so it stays possible — explicitly.
    #[serde(default)]
    pub rewind: bool,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct UpNextDto {
    pub series_id: String,
    pub series_name: String,
    pub entry: EntryDto,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub progress: Option<ProgressDto>,
    /// IN_PROGRESS or NEXT_UP — the client groups on this, and they are not the same offer.
    pub reason: String,
}

pub struct Progress<'a> {
    db: &'a Db,
}

#[derive(Debug, Clone)]
struct Next {
    edition_id: String,
    entry_id: String,
    reason: &'static str,
}

impl<'a> Progress<'a> {
    pub fn new(db: &'a Db) -> Self {
        Progress { db }
    }

    pub fn of(&self, entry_id: &str) -> Result<Option<ProgressDto>> {
        Ok(self
            .of_many(std::slice::from_ref(&entry_id.to_string()))?
            .remove(entry_id))
    }

    /// Where the reader stands in several entries at once, markers included.
    ///
    /// Asked for one entry at a time, this was two queries per row and a third for the
    /// chapters — which is how "what to open next" came to cost a hundred and forty
    /// questions to answer with twenty lines.
    pub fn of_many(
        &self,
        entry_ids: &[String],
    ) -> Result<std::collections::HashMap<String, ProgressDto>> {
        if entry_ids.is_empty() {
            return Ok(Default::default());
        }
        let mut unique: Vec<String> = entry_ids.to_vec();
        unique.sort();
        unique.dedup();

        let chapters = Repository::new(self.db).chapters_of_entries(&unique)?;

        let mut found = std::collections::HashMap::new();
        for part in unique.chunks(CHUNK) {
            let sql = format!(
                "SELECT p.entry_id, p.page, p.finished, p.updated_at, e.page_count
                 FROM progress p JOIN entry e ON e.id = p.entry_id
                 WHERE p.entry_id IN ({})",
                marks(part.len())
            );
            let args: Vec<Value> = part.iter().map(|i| Value::Text(i.clone())).collect();
            let rows = self.db.read(|cx| {
                cx.query(&sql, rusqlite::params_from_iter(args.iter()), |r| {
                    Ok((
                        r.get::<_, String>("entry_id")?,
                        r.get::<_, i64>("page")?,
                        r.get::<_, i64>("page_count")?,
                        r.get::<_, i64>("finished")? == 1,
                        r.get::<_, i64>("updated_at")?,
                    ))
                })
            })?;
            for (entry_id, page, page_count, finished, updated_at) in rows {
                let none = Vec::new();
                let chapter = chapter_at_page(chapters.get(&entry_id).unwrap_or(&none), page);
                found.insert(
                    entry_id.clone(),
                    ProgressDto {
                        entry_id,
                        page,
                        page_count,
                        finished,
                        updated_at,
                        chapter,
                    },
                );
            }
        }
        Ok(found)
    }

    pub fn of_series(&self, edition_id: &str) -> Result<Vec<ProgressDto>> {
        let entry_ids = self.db.read(|cx| {
            cx.query(
                "SELECT p.entry_id FROM progress p WHERE p.edition_id = ?1",
                [edition_id],
                |r| r.get::<_, String>(0),
            )
        })?;
        let found = self.of_many(&entry_ids)?;
        Ok(entry_ids
            .iter()
            .filter_map(|id| found.get(id).cloned())
            .collect())
    }

    /// Records a position, and refuses to lose one.
    ///
    /// The page is clamped to the entry's own page count, so a client with the wrong idea of
    /// how long a volume is cannot store a position that does not exist.
    ///
    /// **One statement, not read-then-write.** Two positions arriving at once — a phone
    /// emptying an offline queue does exactly that — both read the same current page and the
    /// later writer won, so recording page 10 and page 7 together could leave 7. The rule
    /// that progress never moves backwards is the rule the offline queue is built on; it
    /// cannot hold if it is enforced outside the transaction that applies it. `MAX` does it
    /// in SQLite, where the row is locked anyway.
    pub fn record(
        &self,
        entry_id: &str,
        patch: &ProgressPatch,
        now: i64,
    ) -> Result<Option<ProgressDto>> {
        let written = self.db.write(|cx| {
            let entry = cx.query_one(
                "SELECT page_count, edition_id FROM entry WHERE id = ?1",
                [entry_id],
                |r| Ok((r.get::<_, i64>(0)?, r.get::<_, String>(1)?)),
            )?;
            let Some((page_count, edition_id)) = entry else {
                return Ok(false);
            };
            let asked = patch.page.map(|p| p.clamp(0, (page_count - 1).max(0)));

            cx.execute(
                "INSERT INTO progress (entry_id, edition_id, page, finished, updated_at)
                 VALUES (?1, ?2, COALESCE(?3, 0), COALESCE(?4, 0), ?5)
                 ON CONFLICT(entry_id) DO UPDATE SET
                   page = CASE
                            WHEN ?6 THEN COALESCE(?3, progress.page)
                            ELSE MAX(progress.page, COALESCE(?3, progress.page))
                          END,
                   finished = COALESCE(?4, progress.finished),
                   updated_at = ?5",
                rusqlite::params![
                    entry_id,
                    edition_id,
                    asked,
                    patch.finished.map(i64::from),
                    now,
                    i64::from(patch.rewind),
                ],
            )?;
            Ok(true)
        })?;
        if !written {
            return Ok(None);
        }
        self.of(entry_id)
    }

    pub fn forget(&self, entry_id: &str) -> Result<()> {
        self.db.write(|cx| {
            cx.execute("DELETE FROM progress WHERE entry_id = ?1", [entry_id])?;
            Ok(())
        })
    }

    /// What to open next. Two very different things, deliberately kept apart:
    ///
    ///  - what you are in the middle of, most recent first;
    ///  - the entry that follows the last one you finished, in each series.
    ///
    /// A series you have never opened appears in neither: "next" means next for you, not a
    /// catalogue.
    pub fn up_next(&self, limit: i64) -> Result<Vec<UpNextDto>> {
        let in_progress = self.db.read(|cx| {
            cx.query(
                "SELECT p.entry_id, p.edition_id FROM progress p
                 WHERE p.finished = 0 AND p.page > 0
                 ORDER BY p.updated_at DESC LIMIT ?1",
                [limit],
                |r| {
                    Ok(Next {
                        edition_id: r.get(1)?,
                        entry_id: r.get(0)?,
                        reason: "IN_PROGRESS",
                    })
                },
            )
        })?;
        let busy: std::collections::HashSet<String> =
            in_progress.iter().map(|n| n.edition_id.clone()).collect();

        // For each series with something finished and nothing in progress, the first entry
        // that carries no progress at all. The entry is picked by the same query that finds
        // the series, rather than by one more question per series.
        let next_up = self.db.read(|cx| {
            cx.query(
                "SELECT edition_id, id FROM (
                   SELECT e.edition_id, e.id,
                          ROW_NUMBER() OVER (
                            PARTITION BY e.edition_id ORDER BY e.sort_key IS NULL, e.sort_key, e.file
                          ) AS rank
                   FROM entry e
                   LEFT JOIN progress p ON p.entry_id = e.id
                   WHERE p.entry_id IS NULL
                     AND EXISTS (SELECT 1 FROM progress q
                                 WHERE q.edition_id = e.edition_id AND q.finished = 1)
                 )
                 WHERE rank = 1",
                [],
                |r| {
                    Ok(Next {
                        edition_id: r.get(0)?,
                        entry_id: r.get(1)?,
                        reason: "NEXT_UP",
                    })
                },
            )
        })?;

        let picked: Vec<Next> = in_progress
            .into_iter()
            .chain(
                next_up
                    .into_iter()
                    .filter(|n| !busy.contains(&n.edition_id)),
            )
            .take(limit.max(0) as usize)
            .collect();
        self.rows(&picked)
    }

    /// Turns the chosen entries into what the screen shows, reading each kind of thing once.
    ///
    /// This used to build a row at a time: a series query, an entry query, a progress query
    /// and a chapter query, for every line. Twenty lines cost around a hundred and forty
    /// questions, and the count grew with the list rather than with the library — the same
    /// shape of defect the shelf had, on the screen that opens first.
    fn rows(&self, picked: &[Next]) -> Result<Vec<UpNextDto>> {
        if picked.is_empty() {
            return Ok(Vec::new());
        }
        let repository = Repository::new(self.db);

        let mut edition_ids: Vec<String> = picked.iter().map(|n| n.edition_id.clone()).collect();
        edition_ids.sort();
        edition_ids.dedup();
        let names: std::collections::HashMap<String, String> = repository
            .series(
                &crate::api::dto::SeriesFilter {
                    ids: edition_ids,
                    ..Default::default()
                },
                crate::api::dto::SeriesSort::Name,
                0,
                0,
            )?
            .into_iter()
            .map(|s| (s.id, s.name))
            .collect();

        let entry_ids: Vec<String> = picked.iter().map(|n| n.entry_id.clone()).collect();
        let entries = repository.entries_by_ids(&entry_ids)?;
        let progress = self.of_many(&entry_ids)?;

        Ok(picked
            .iter()
            .filter_map(|next| {
                Some(UpNextDto {
                    series_id: next.edition_id.clone(),
                    series_name: names.get(&next.edition_id)?.clone(),
                    entry: entries.get(&next.entry_id)?.clone(),
                    progress: progress.get(&next.entry_id).cloned(),
                    reason: next.reason.to_string(),
                })
            })
            .collect())
    }
}

/// The chapter a page falls into: the last marker at or before it.
///
/// Two cases answer nothing on purpose, because the honest answer is "I do not know":
///
///  - the volume declares chapters but none says where it starts, which is every volume
///    still described by a ComicInfo, since that format has no such field;
///  - the page comes before the first marker — a cover, a colour insert, a page of credits
///    — and belongs to no chapter at all.
///
/// Treating a missing start page as zero would answer "chapter 1" on every page of the
/// volume, confidently and wrongly. It did, twice, before this became its own function.
pub fn chapter_at_page(chapters: &[ChapterDto], page: i64) -> Option<ChapterDto> {
    if chapters.is_empty() {
        return None;
    }
    // A standalone chapter entry is one marker with no start page: it is the answer.
    if chapters.len() == 1 {
        return chapters.first().cloned();
    }
    chapters
        .iter()
        .filter(|c| c.start_page.is_some_and(|s| s <= page))
        .max_by_key(|c| c.start_page.expect("filtered on Some"))
        .cloned()
}
