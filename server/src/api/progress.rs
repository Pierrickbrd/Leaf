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
use crate::store::repository::{marks, Ceiling};
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
    /// How many times this entry has been finished. `finished` says where the reader stands
    /// now; this says whether they ever reached the end, and the two stopped being the same
    /// question the day re-reading became possible. Absent at nought, like every other count
    /// in these answers.
    #[serde(skip_serializing_if = "crate::api::dto::is_zero")]
    pub times_finished: i64,
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
    /// re-reading a volume is a normal thing to do, so it stays possible — explicitly. Below
    /// the last page it clears `finished`, unless the patch says `finished` itself.
    #[serde(default)]
    pub rewind: bool,
}

/// What a whole series can be told about its progress.
///
/// Only `finished`: a page number is a position inside one file and says nothing across
/// thirty, and moving a whole series backwards is `DELETE` — forgetting it, which is a
/// different act from being somewhere else in it.
#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SeriesProgressPatch {
    pub finished: bool,
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

    pub fn of(
        &self,
        entry_id: &str,
        profile: &str,
        ceiling: &Ceiling,
    ) -> Result<Option<ProgressDto>> {
        Ok(self
            .of_many(
                std::slice::from_ref(&entry_id.to_string()),
                profile,
                ceiling,
            )?
            .remove(entry_id))
    }

    /// Where the reader stands in one entry — `None` when the entry is not there for this
    /// caller, `Some(None)` when it is and was never opened.
    ///
    /// The route answers 204 for the second and must answer 404 for the first: an entry above
    /// a restricted profile's age answering « never opened » would be saying it exists. One
    /// statement for both, the entry joined to its place.
    pub fn of_entry(
        &self,
        entry_id: &str,
        profile: &str,
        ceiling: &Ceiling,
    ) -> Result<Option<Option<ProgressDto>>> {
        let sql = format!(
            "SELECT p.entry_id FROM entry x
             LEFT JOIN progress p ON p.entry_id = x.id AND p.profile_id = ?2
             WHERE x.id = ?1{}",
            ceiling.and_edition("x.edition_id")
        );
        let opened: Option<Option<String>> = self
            .db
            .read(|cx| cx.query_one(&sql, [entry_id, profile], |r| r.get(0)))?;
        match opened {
            None => Ok(None),
            Some(None) => Ok(Some(None)),
            Some(Some(_)) => Ok(Some(self.of(entry_id, profile, ceiling)?)),
        }
    }

    /// Where the reader stands in several entries at once, markers included.
    ///
    /// Asked for one entry at a time, this was two queries per row and a third for the
    /// chapters — which is how "what to open next" came to cost a hundred and forty
    /// questions to answer with twenty lines.
    ///
    /// **One profile's places.** Read across the table, two readers in the same volume came
    /// back as two rows for one entry, and the map kept whichever was read last — a child's
    /// page 3 answered with a parent's page 140.
    pub fn of_many(
        &self,
        entry_ids: &[String],
        profile: &str,
        ceiling: &Ceiling,
    ) -> Result<std::collections::HashMap<String, ProgressDto>> {
        if entry_ids.is_empty() {
            return Ok(Default::default());
        }
        let mut unique: Vec<String> = entry_ids.to_vec();
        unique.sort();
        unique.dedup();

        let chapters = Repository::new(self.db).chapters_of_entries(&unique, ceiling)?;

        let visible = ceiling.and_edition("e.edition_id");
        let mut found = std::collections::HashMap::new();
        for part in unique.chunks(CHUNK) {
            let sql = format!(
                "SELECT p.entry_id, p.page, p.finished, p.times_finished, p.updated_at,
                        e.page_count
                 FROM progress p JOIN entry e ON e.id = p.entry_id
                 WHERE p.profile_id = ? AND p.entry_id IN ({}){visible}",
                marks(part.len())
            );
            let mut args: Vec<Value> = vec![Value::Text(profile.to_string())];
            args.extend(part.iter().map(|i| Value::Text(i.clone())));
            let rows = self.db.read(|cx| {
                cx.query(&sql, rusqlite::params_from_iter(args.iter()), |r| {
                    Ok((
                        r.get::<_, String>("entry_id")?,
                        r.get::<_, i64>("page")?,
                        r.get::<_, i64>("page_count")?,
                        r.get::<_, i64>("finished")? == 1,
                        r.get::<_, i64>("times_finished")?,
                        r.get::<_, i64>("updated_at")?,
                    ))
                })
            })?;
            for (entry_id, page, page_count, finished, times_finished, updated_at) in rows {
                let none = Vec::new();
                let chapter = chapter_at_page(chapters.get(&entry_id).unwrap_or(&none), page);
                found.insert(
                    entry_id.clone(),
                    ProgressDto {
                        entry_id,
                        page,
                        page_count,
                        finished,
                        times_finished,
                        updated_at,
                        chapter,
                    },
                );
            }
        }
        Ok(found)
    }

    /// Every place in a series — `None` when the series is not there for this caller, which
    /// the route turns into the 404 an unknown series gets. Whether it is there is asked in
    /// the statement that finds the places, the edition joined to them.
    pub fn of_series(
        &self,
        edition_id: &str,
        profile: &str,
        ceiling: &Ceiling,
    ) -> Result<Option<Vec<ProgressDto>>> {
        let sql = format!(
            "SELECT p.entry_id FROM edition ed
             JOIN work w ON w.id = ed.work_id
             LEFT JOIN progress p ON p.edition_id = ed.id AND p.profile_id = ?2
             WHERE ed.id = ?1{}",
            ceiling.and_work("w")
        );
        let rows: Vec<Option<String>> = self
            .db
            .read(|cx| cx.query(&sql, [edition_id, profile], |r| r.get(0)))?;
        if rows.is_empty() {
            return Ok(None);
        }
        let entry_ids: Vec<String> = rows.into_iter().flatten().collect();
        let found = self.of_many(&entry_ids, profile, ceiling)?;
        Ok(Some(
            entry_ids
                .iter()
                .filter_map(|id| found.get(id).cloned())
                .collect(),
        ))
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
        profile_id: &str,
        entry_id: &str,
        patch: &ProgressPatch,
        now: i64,
        ceiling: &Ceiling,
    ) -> Result<Option<ProgressDto>> {
        // A place in a series the caller may not see is not written: the entry is not there
        // for them, and the 404 says exactly what an unknown entry's does.
        let sql = format!(
            "SELECT x.page_count, x.edition_id FROM entry x WHERE x.id = ?1{}",
            ceiling.and_edition("x.edition_id")
        );
        let written = self.db.write(|cx| {
            let entry = cx.query_one(&sql, [entry_id], |r| {
                Ok((r.get::<_, i64>(0)?, r.get::<_, String>(1)?))
            })?;
            let Some((page_count, edition_id)) = entry else {
                return Ok(false);
            };
            let asked = patch.page.map(|p| p.clamp(0, (page_count - 1).max(0)));

            cx.execute(
                "INSERT INTO progress
                   (entry_id, edition_id, page, finished, times_finished, updated_at, profile_id)
                 VALUES (?1, ?2, COALESCE(?3, 0), COALESCE(?4, 0), COALESCE(?4, 0), ?5, ?7)
                 ON CONFLICT(profile_id, entry_id) DO UPDATE SET
                   page = CASE
                            WHEN ?6 THEN COALESCE(?3, progress.page)
                            ELSE MAX(progress.page, COALESCE(?3, progress.page))
                          END,
                   -- A rewind below the last page clears it, as the contract says: the
                   -- server kept it, and a volume re-opened at its first page in the test
                   -- of 22/10/2026 stayed « lu » unless the client cleared it itself. What
                   -- the patch says outright still wins over what the rewind implies.
                   finished = CASE
                                WHEN ?4 IS NOT NULL THEN ?4
                                WHEN ?6 AND COALESCE(?3, progress.page) < ?8 THEN 0
                                ELSE progress.finished
                              END,
                   -- Counted on the edge, and in this statement for the same reason the
                   -- page is: read-then-write would let two patches arriving together both
                   -- see an unfinished row and count the same ending twice.
                   times_finished = progress.times_finished
                     + CASE WHEN progress.finished = 0
                                 AND COALESCE(?4, progress.finished) = 1
                            THEN 1 ELSE 0 END,
                   updated_at = ?5",
                rusqlite::params![
                    entry_id,
                    edition_id,
                    asked,
                    patch.finished.map(i64::from),
                    now,
                    i64::from(patch.rewind),
                    profile_id,
                    (page_count - 1).max(0),
                ],
            )?;
            Ok(true)
        })?;
        if !written {
            return Ok(None);
        }
        // This profile's place, the one just written: read back across profiles, the
        // answer to a child's PATCH could carry a parent's page.
        self.of(entry_id, profile_id, ceiling)
    }

    /// Marks every entry of a series finished, or unfinished, in one statement.
    ///
    /// The point of the route is that it is **not** thirty `PATCH`es. A series of thirty
    /// volumes is one upsert over a `SELECT`, so `store/db.rs`'s statement counter sees the
    /// same number here whether the series holds three volumes or three hundred — which is
    /// what a test can hold it to.
    ///
    /// Marking as finished fills the page in too: a volume finished at page nought is a
    /// contradiction the shelf would draw as an empty bar over the word « lu ».
    pub fn record_series(
        &self,
        profile_id: &str,
        edition_id: &str,
        finished: bool,
        now: i64,
        ceiling: &Ceiling,
    ) -> Result<Option<Vec<ProgressDto>>> {
        let sql = format!(
            "SELECT 1 FROM edition ed WHERE ed.id = ?1{}",
            ceiling.and_edition("ed.id")
        );
        let known = self.db.write(|cx| {
            let found = cx.query_one(&sql, [edition_id], |_| Ok(()))?;
            if found.is_none() {
                return Ok(false);
            }
            if finished {
                cx.execute(
                    "INSERT INTO progress
                       (entry_id, edition_id, page, finished, times_finished, updated_at, profile_id)
                     SELECT x.id, x.edition_id, MAX(x.page_count - 1, 0), 1, 1, ?2, ?3
                       FROM entry x WHERE x.edition_id = ?1
                     ON CONFLICT(profile_id, entry_id) DO UPDATE SET
                       page = MAX(progress.page, excluded.page),
                       finished = 1,
                       times_finished = progress.times_finished
                         + CASE WHEN progress.finished = 0 THEN 1 ELSE 0 END,
                       updated_at = ?2",
                    rusqlite::params![edition_id, now, profile_id],
                )?;
            } else {
                // No row is created for a volume that was never opened: saying « not
                // finished » about a book nobody started is not a fact worth storing, and
                // an absent record is already that answer.
                cx.execute(
                    "UPDATE progress SET finished = 0, updated_at = ?2
                     WHERE edition_id = ?1 AND profile_id = ?3",
                    rusqlite::params![edition_id, now, profile_id],
                )?;
            }
            Ok(true)
        })?;
        if !known {
            return Ok(None);
        }
        self.of_series(edition_id, profile_id, ceiling)
    }

    /// Forgets one profile's place in an entry. Only that profile's: another reader's
    /// position in the same volume is theirs to forget.
    pub fn forget(&self, profile_id: &str, entry_id: &str) -> Result<()> {
        self.db.write(|cx| {
            cx.execute(
                "DELETE FROM progress WHERE entry_id = ?1 AND profile_id = ?2",
                [entry_id, profile_id],
            )?;
            Ok(())
        })
    }

    /// Forgets every position a profile holds in a series. One statement, and silent about
    /// whether there was anything to forget — the same contract as forgetting one.
    pub fn forget_series(&self, profile_id: &str, edition_id: &str) -> Result<()> {
        self.db.write(|cx| {
            cx.execute(
                "DELETE FROM progress WHERE edition_id = ?1 AND profile_id = ?2",
                [edition_id, profile_id],
            )?;
            Ok(())
        })
    }

    /// What to open next. Two very different things, deliberately kept apart:
    ///
    ///  - what you are in the middle of, most recent first;
    ///  - the entry that follows the furthest one you finished, in each series — nothing
    ///    when that was the last.
    ///
    /// A series you have never opened appears in neither: "next" means next for you, not a
    /// catalogue.
    ///
    /// Under a ceiling, a series above it is in neither — and is left out by the queries
    /// that pick, not after them, or twenty lines asked for would come back as fewer while
    /// more were there to offer.
    ///
    /// Both lists are the caller's profile's. Read across profiles, a volume two readers were
    /// in the middle of came back twice, and a series one of them finished offered its next
    /// volume to the other, who had never opened it.
    pub fn up_next(&self, limit: i64, profile: &str, ceiling: &Ceiling) -> Result<Vec<UpNextDto>> {
        let sql = format!(
            "SELECT p.entry_id, p.edition_id FROM progress p
             WHERE p.profile_id = ?2 AND p.finished = 0 AND p.page > 0{}
             ORDER BY p.updated_at DESC LIMIT ?1",
            ceiling.and_edition("p.edition_id")
        );
        let in_progress = self.db.read(|cx| {
            cx.query(&sql, rusqlite::params![limit, profile], |r| {
                Ok(Next {
                    edition_id: r.get(1)?,
                    entry_id: r.get(0)?,
                    reason: "IN_PROGRESS",
                })
            })
        })?;
        let busy: std::collections::HashSet<String> =
            in_progress.iter().map(|n| n.edition_id.clone()).collect();

        // For each series, the entry right after the furthest one finished, in the order the
        // series is read. It used to be the first entry carrying no progress at all, which
        // the test of 22/10/2026 caught: volume three finished, one and two read elsewhere
        // and never opened here, and « à suivre » offered volume one. The entry after the
        // furthest finished is unfinished by construction, so a run of finished volumes is
        // walked past without asking; a series finished to its last entry offers nothing.
        // One query for every series, rather than one more question per series. What was
        // finished is what *this profile* finished, and a series above its age is not asked.
        let sql = format!(
            "WITH ranked AS (
               SELECT e.edition_id, e.id, COALESCE(p.finished, 0) AS finished,
                      ROW_NUMBER() OVER (
                        PARTITION BY e.edition_id ORDER BY e.sort_key IS NULL, e.sort_key, e.file
                      ) AS rank
               FROM entry e
               LEFT JOIN progress p ON p.entry_id = e.id AND p.profile_id = ?1
               WHERE 1 = 1{}
             ),
             furthest AS (
               SELECT edition_id, MAX(rank) AS rank FROM ranked
               WHERE finished = 1 GROUP BY edition_id
             )
             SELECT r.edition_id, r.id FROM ranked r
             JOIN furthest f ON f.edition_id = r.edition_id AND r.rank = f.rank + 1",
            ceiling.and_edition("e.edition_id")
        );
        let next_up = self.db.read(|cx| {
            cx.query(&sql, [profile], |r| {
                Ok(Next {
                    edition_id: r.get(0)?,
                    entry_id: r.get(1)?,
                    reason: "NEXT_UP",
                })
            })
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
        self.rows(&picked, profile, ceiling)
    }

    /// Turns the chosen entries into what the screen shows, reading each kind of thing once.
    ///
    /// This used to build a row at a time: a series query, an entry query, a progress query
    /// and a chapter query, for every line. Twenty lines cost around a hundred and forty
    /// questions, and the count grew with the list rather than with the library — the same
    /// shape of defect the shelf had, on the screen that opens first.
    fn rows(&self, picked: &[Next], profile: &str, ceiling: &Ceiling) -> Result<Vec<UpNextDto>> {
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
                profile,
                ceiling,
            )?
            .into_iter()
            .map(|s| (s.id, s.name))
            .collect();

        let entry_ids: Vec<String> = picked.iter().map(|n| n.entry_id.clone()).collect();
        let entries = repository.entries_by_ids(&entry_ids, ceiling)?;
        let progress = self.of_many(&entry_ids, profile, ceiling)?;

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
