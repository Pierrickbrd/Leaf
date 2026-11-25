//! What the library holds, read for a reimport: the target it is aimed at, the volumes
//! that target holds, the editions that sit at some folders, and every work.
//!
//! A reimport compares what arrives with what is there, and replaces by landing on the
//! name of the file it replaces. Everything it decides rests on these reads being right and
//! being cheap — a volume lost here is a volume replaced by the wrong file, and a read that
//! asks one question per volume makes a 200-volume series take 200 of them. So each is one
//! statement whatever the size, which the tests count.

use std::collections::HashMap;
use std::path::PathBuf;

use anyhow::Result;
use rusqlite::{params_from_iter, Row};

use super::db::Db;
use super::repository::Ceiling;
use crate::scan::scanner::id_of;

/// SQLite refuses more bound parameters than its limit (32 766 on current builds); well
/// under it, as `origins_of` does for the same reason.
const CHUNK: usize = 30_000;

/// What a reimport is aimed at, by the id the API already gives it.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Aim {
    Entry(String),
    Series(String),
    Universe(String),
    Chapter(String),
}

/// One level of the way from a universe down to the target, as the rail draws it.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Step {
    /// `UNIVERSE`, `WORK`, `SERIES`, `ENTRY` or `CHAPTER`.
    pub kind: &'static str,
    pub id: String,
    pub name: String,
    /// What the level holds: the works of a universe, the series of a work, the volumes of
    /// a series, the pages of a volume. A chapter holds none.
    pub count: u32,
}

/// A chapter as the index holds it.
#[derive(Debug, Clone, PartialEq)]
pub struct HeldChapter {
    pub id: String,
    pub number: Option<f64>,
    pub title: Option<String>,
    pub start_page: Option<i64>,
}

/// Where a reimport lands, and what it is compared with.
#[derive(Debug, Clone, PartialEq)]
pub struct Place {
    /// The folder the drop is laid on.
    pub folder: PathBuf,
    /// What a rescan has to aim at; none for a universe, which is no work.
    pub work_folder: Option<PathBuf>,
    /// The editions the arrivals are compared with.
    pub editions: Vec<String>,
    /// For a universe, where each of those editions is and what it is called: a drop's
    /// series finds its edition by folder or by name, and nothing else says either.
    pub edition_folders: Vec<EditionFolder>,
    pub work_id: Option<String>,
    pub universe_id: Option<String>,
    /// For a volume or a chapter.
    pub entry_id: Option<String>,
    pub chapter: Option<HeldChapter>,
    /// From the universe down to the target.
    pub trail: Vec<Step>,
}

/// One edition of a universe, as a drop's folder is compared with it.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct EditionFolder {
    pub id: String,
    pub work_id: String,
    pub path: PathBuf,
    /// What its `edition.json` calls it; none for an implicit edition, which is its work.
    pub name: Option<String>,
    pub implicit: bool,
}

/// A volume or a chapter-file the library holds, and what this profile has done in it.
#[derive(Debug, Clone, PartialEq)]
pub struct Held {
    pub entry_id: String,
    pub edition_id: String,
    pub kind: String,
    pub number: Option<f64>,
    pub file: PathBuf,
    pub size: u64,
    pub pages: u32,
    pub title: Option<String>,
    /// The numbers of the chapters it holds; a bonus has none to list.
    pub chapters: Vec<f64>,
    /// The asking profile's place in it.
    pub page: Option<i64>,
    pub finished: bool,
    /// How many other profiles have a place in it: what a replacement would disturb.
    pub others: u32,
}

/// A work, with what is needed to tell it from another of the same title.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Titled {
    pub work_id: String,
    /// Its edition, in the API's word for it; the implicit one when there is one.
    pub series_id: String,
    pub name: String,
    pub title: Option<String>,
    pub path: PathBuf,
    pub universe_id: Option<String>,
    /// The volumes its series holds, to say what a drop that leaves it out leaves.
    pub volumes: u32,
    /// Whether its folder carries an identity of its own. One that has only the identity its
    /// place gives it is not the same work once moved.
    pub identity_declared: bool,
}

/// A stored integer as the unsigned type it stands for. A value that does not fit is an
/// error and not a zero: a size or a page count read as 0 would pass for a file that was
/// never measured.
pub(super) fn unsigned<T: TryFrom<i64>>(value: i64, column: usize) -> rusqlite::Result<T> {
    T::try_from(value).map_err(|_| rusqlite::Error::IntegralValueOutOfRange(column, value))
}

fn count(r: &Row<'_>, column: &str) -> rusqlite::Result<u32> {
    unsigned(r.get::<_, i64>(column)?, 0)
}

/// The target, with the way down to it; `None` when it is not there **for this caller**,
/// which is what `/series/{id}` answers for a series a child may not see.
pub fn place_of(db: &Db, aim: &Aim, ceiling: &Ceiling) -> Result<Option<Place>> {
    match aim {
        Aim::Universe(id) => universe_place(db, id, ceiling),
        Aim::Series(id) => lower_place(db, id, ceiling, Level::Series),
        Aim::Entry(id) => lower_place(db, id, ceiling, Level::Entry),
        Aim::Chapter(id) => lower_place(db, id, ceiling, Level::Chapter),
    }
}

fn universe_place(db: &Db, id: &str, ceiling: &Ceiling) -> Result<Option<Place>> {
    // One row per edition of a work the caller may see, or one row of nothing for a
    // universe that holds none. Hidden works are left out of the join and of the count; and
    // a universe holding works but only hidden ones is not there at all, as it is not in the
    // list. One holding none is there for everyone, as it is without a ceiling: answering
    // otherwise to a child would say nothing about the library and everything about who asks.
    let visible = ceiling.and_work("uw");
    let hidden_only = if visible.is_empty() {
        String::new()
    } else {
        format!(
            " AND (NOT EXISTS (SELECT 1 FROM work aw WHERE aw.universe_id = u.id)
                   OR EXISTS (SELECT 1 FROM work uw WHERE uw.universe_id = u.id{visible}))"
        )
    };
    let sql = format!(
        "SELECT u.id AS u_id, u.name AS u_name, u.path AS u_path,
                (SELECT COUNT(*) FROM work cw WHERE cw.universe_id = u.id{}) AS u_works,
                ed.id AS ed_id, ed.work_id AS ed_work, ed.path AS ed_path, ed.name AS ed_name,
                ed.implicit AS ed_implicit
         FROM universe u
         LEFT JOIN work w ON w.universe_id = u.id{}
         LEFT JOIN edition ed ON ed.work_id = w.id
         WHERE u.id = ?1{hidden_only}
         ORDER BY w.path, ed.path",
        ceiling.and_work("cw"),
        ceiling.and_work("w"),
    );
    let rows = db.read(|cx| {
        cx.query(&sql, [id], |r| {
            Ok((
                r.get::<_, String>("u_id")?,
                r.get::<_, String>("u_name")?,
                r.get::<_, String>("u_path")?,
                count(r, "u_works")?,
                match r.get::<_, Option<String>>("ed_id")? {
                    None => None,
                    Some(id) => Some(EditionFolder {
                        id,
                        work_id: r.get("ed_work")?,
                        path: PathBuf::from(r.get::<_, String>("ed_path")?),
                        name: r.get("ed_name")?,
                        implicit: r.get::<_, i64>("ed_implicit")? != 0,
                    }),
                },
            ))
        })
    })?;
    let Some((universe, name, path, works, _)) = rows.first().cloned() else {
        return Ok(None);
    };
    let edition_folders: Vec<EditionFolder> = rows.into_iter().filter_map(|row| row.4).collect();
    Ok(Some(Place {
        folder: PathBuf::from(path),
        work_folder: None,
        editions: edition_folders.iter().map(|e| e.id.clone()).collect(),
        edition_folders,
        work_id: None,
        universe_id: Some(universe.clone()),
        entry_id: None,
        chapter: None,
        trail: vec![Step {
            kind: "UNIVERSE",
            id: universe,
            name,
            count: works,
        }],
    }))
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum Level {
    Series,
    Entry,
    Chapter,
}

/// A series, a volume or a chapter: one statement joining the levels above it, each level
/// counting what it holds in a sub-select.
fn lower_place(db: &Db, id: &str, ceiling: &Ceiling, level: Level) -> Result<Option<Place>> {
    let (from, aimed) = match level {
        Level::Series => (
            "edition ed
             JOIN work w ON w.id = ed.work_id
             LEFT JOIN entry x ON 1 = 0
             LEFT JOIN chapter c ON 1 = 0",
            "ed.id = ?1",
        ),
        Level::Entry => (
            "entry x
             JOIN edition ed ON ed.id = x.edition_id
             JOIN work w ON w.id = ed.work_id
             LEFT JOIN chapter c ON 1 = 0",
            "x.id = ?1",
        ),
        Level::Chapter => (
            "chapter c
             JOIN entry x ON x.id = c.entry_id
             JOIN edition ed ON ed.id = x.edition_id
             JOIN work w ON w.id = ed.work_id",
            "c.id = ?1",
        ),
    };
    let sql = format!(
        "SELECT ed.id AS ed_id, ed.name AS ed_name, ed.path AS ed_path, ed.implicit AS implicit,
                w.id AS w_id, COALESCE(w.title, w.name) AS w_name, w.path AS w_path,
                w.universe_id AS u_id,
                (SELECT u.name FROM universe u WHERE u.id = w.universe_id) AS u_name,
                (SELECT COUNT(*) FROM work cw WHERE cw.universe_id = w.universe_id{}) AS u_works,
                (SELECT COUNT(*) FROM edition se WHERE se.work_id = w.id) AS w_editions,
                (SELECT COUNT(*) FROM entry sx WHERE sx.edition_id = ed.id) AS ed_entries,
                x.id AS x_id, x.file AS x_file, x.title AS x_title, x.page_count AS x_pages,
                c.id AS c_id, c.number AS c_number, c.title AS c_title, c.start_page AS c_start
         FROM {from}
         WHERE {aimed}{}",
        ceiling.and_work("cw"),
        ceiling.and_work("w"),
    );
    let found = db.read(|cx| {
        cx.query_one(&sql, [id], |r| {
            let implicit = r.get::<_, i64>("implicit")? != 0;
            let work_name: String = r.get("w_name")?;
            let mut trail = Vec::new();
            if let Some(universe) = r.get::<_, Option<String>>("u_id")? {
                trail.push(Step {
                    kind: "UNIVERSE",
                    id: universe,
                    name: r.get::<_, Option<String>>("u_name")?.unwrap_or_default(),
                    count: count(r, "u_works")?,
                });
            }
            // An implicit edition is its work under another name, and drawn twice it would
            // be the same folder on two lines of the rail.
            if !implicit {
                trail.push(Step {
                    kind: "WORK",
                    id: r.get("w_id")?,
                    name: work_name.clone(),
                    count: count(r, "w_editions")?,
                });
            }
            let edition: String = r.get("ed_id")?;
            trail.push(Step {
                kind: "SERIES",
                id: edition.clone(),
                name: r
                    .get::<_, Option<String>>("ed_name")?
                    .filter(|_| !implicit)
                    .unwrap_or(work_name),
                count: count(r, "ed_entries")?,
            });
            let entry_id = r.get::<_, Option<String>>("x_id")?;
            if let Some(entry) = &entry_id {
                let file: String = r.get("x_file")?;
                trail.push(Step {
                    kind: "ENTRY",
                    id: entry.clone(),
                    name: r.get::<_, Option<String>>("x_title")?.unwrap_or_else(|| {
                        std::path::Path::new(&file)
                            .file_stem()
                            .map(|s| s.to_string_lossy().to_string())
                            .unwrap_or(file)
                    }),
                    count: count(r, "x_pages")?,
                });
            }
            let chapter = match r.get::<_, Option<String>>("c_id")? {
                Some(chapter) => {
                    let held = HeldChapter {
                        id: chapter,
                        number: r.get("c_number")?,
                        title: r.get("c_title")?,
                        start_page: r.get("c_start")?,
                    };
                    trail.push(Step {
                        kind: "CHAPTER",
                        id: held.id.clone(),
                        name: held
                            .title
                            .clone()
                            .or_else(|| held.number.map(|n| n.to_string()))
                            .unwrap_or_default(),
                        count: 0,
                    });
                    Some(held)
                }
                None => None,
            };
            Ok(Place {
                folder: PathBuf::from(r.get::<_, String>("ed_path")?),
                work_folder: Some(PathBuf::from(r.get::<_, String>("w_path")?)),
                editions: vec![edition],
                edition_folders: Vec::new(),
                work_id: Some(r.get("w_id")?),
                universe_id: r.get("u_id")?,
                entry_id,
                chapter,
                trail,
            })
        })
    })?;
    Ok(found)
}

/// The volume a chapter is replaced in: its file, its pages, the asking profile's place in
/// it, and every chapter it holds in reading order.
#[derive(Debug, Clone, PartialEq)]
pub struct ChapteredVolume {
    pub file: PathBuf,
    pub pages: u32,
    pub page: Option<i64>,
    pub chapters: Vec<HeldChapter>,
}

/// The volume `entry_id`, with its chapters, in one statement: one row per chapter.
pub fn chaptered_volume(db: &Db, entry_id: &str, profile: &str) -> Result<Option<ChapteredVolume>> {
    let rows = db.read(|cx| {
        cx.query(
            "SELECT x.file, x.page_count, p.page, c.id, c.number, c.title, c.start_page
             FROM entry x
             LEFT JOIN progress p ON p.entry_id = x.id AND p.profile_id = ?2
             LEFT JOIN chapter c ON c.entry_id = x.id
             WHERE x.id = ?1
             ORDER BY c.position, c.start_page",
            [entry_id, profile],
            |r| {
                let chapter = match r.get::<_, Option<String>>(3)? {
                    Some(id) => Some(HeldChapter {
                        id,
                        number: r.get(4)?,
                        title: r.get(5)?,
                        start_page: r.get(6)?,
                    }),
                    None => None,
                };
                Ok((
                    r.get::<_, String>(0)?,
                    unsigned::<u32>(r.get::<_, i64>(1)?, 1)?,
                    r.get::<_, Option<i64>>(2)?,
                    chapter,
                ))
            },
        )
    })?;
    let Some((file, pages, page, _)) = rows.first().cloned() else {
        return Ok(None);
    };
    Ok(Some(ChapteredVolume {
        file: PathBuf::from(file),
        pages,
        page,
        chapters: rows.into_iter().filter_map(|row| row.3).collect(),
    }))
}

/// Every volume and chapter-file of these editions, with this profile's place in each and
/// how many other profiles have one — in one statement per `CHUNK` editions, which is one.
pub fn held_in(db: &Db, editions: &[String], profile: &str) -> Result<Vec<Held>> {
    let mut all = Vec::new();
    for chunk in editions.chunks(CHUNK) {
        // Built from the count, never from the values: the ids are bound.
        let holes = (2..chunk.len() + 2)
            .map(|n| format!("?{n}"))
            .collect::<Vec<_>>()
            .join(",");
        let sql = format!(
            "SELECT x.id, x.edition_id, x.type, x.volume_number, x.file, x.size, x.page_count,
                    x.title,
                    (SELECT group_concat(ch.number) FROM chapter ch
                      WHERE ch.entry_id = x.id AND ch.number IS NOT NULL) AS chapters,
                    p.page, p.finished,
                    (SELECT COUNT(*) FROM progress o
                      WHERE o.entry_id = x.id AND o.profile_id <> ?1) AS others
             FROM entry x
             LEFT JOIN progress p ON p.entry_id = x.id AND p.profile_id = ?1
             WHERE x.edition_id IN ({holes})
             ORDER BY x.edition_id, x.sort_key, x.file"
        );
        let found = db.read(|cx| {
            cx.query(
                &sql,
                params_from_iter(std::iter::once(profile).chain(chunk.iter().map(String::as_str))),
                |r| {
                    Ok(Held {
                        entry_id: r.get(0)?,
                        edition_id: r.get(1)?,
                        kind: r.get(2)?,
                        number: r.get(3)?,
                        file: PathBuf::from(r.get::<_, String>(4)?),
                        size: unsigned(r.get::<_, i64>(5)?, 5)?,
                        pages: unsigned(r.get::<_, i64>(6)?, 6)?,
                        title: r.get(7)?,
                        chapters: r
                            .get::<_, Option<String>>(8)?
                            .map(|joined| {
                                joined
                                    .split(',')
                                    .filter_map(|n| n.parse::<f64>().ok())
                                    .collect()
                            })
                            .unwrap_or_default(),
                        page: r.get(9)?,
                        finished: r.get::<_, Option<i64>>(10)?.is_some_and(|f| f != 0),
                        others: unsigned(r.get::<_, i64>(11)?, 11)?,
                    })
                },
            )
        })?;
        all.extend(found);
    }
    Ok(all)
}

/// An edition found at a folder, with the names its work goes by — what a file naming its
/// series is compared with. The folder alone says « One Piece (Glénat) » where the file
/// says « One Piece », and the file was taken for another series'.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct EditionAt {
    pub id: String,
    pub work_name: String,
    pub work_title: Option<String>,
}

/// The edition each of these folders is, for those that are one and that `ceiling` lets
/// the caller see — in one statement. The paths are bound: they are names a person chose,
/// and one holding a quote is ordinary.
///
/// A hidden edition is not one: an import compared with it would answer what it holds to
/// somebody the library does not show it to.
pub fn editions_at(
    db: &Db,
    folders: &[PathBuf],
    ceiling: &Ceiling,
) -> Result<HashMap<PathBuf, EditionAt>> {
    let mut found = HashMap::new();
    for chunk in folders.chunks(CHUNK) {
        let holes = std::iter::repeat_n("?", chunk.len())
            .collect::<Vec<_>>()
            .join(",");
        let sql = format!(
            "SELECT e.path, e.id, w.name, w.title
             FROM edition e JOIN work w ON w.id = e.work_id
             WHERE e.path IN ({holes}){}",
            ceiling.and_work("w")
        );
        let rows = db.read(|cx| {
            cx.query(
                &sql,
                params_from_iter(chunk.iter().map(|f| f.to_string_lossy().to_string())),
                |r| {
                    Ok((
                        r.get::<_, String>(0)?,
                        EditionAt {
                            id: r.get(1)?,
                            work_name: r.get(2)?,
                            work_title: r.get(3)?,
                        },
                    ))
                },
            )
        })?;
        found.extend(rows.into_iter().map(|(path, at)| (PathBuf::from(path), at)));
    }
    Ok(found)
}

/// Every work `ceiling` lets the caller see, in one statement. A work holding no edition is
/// not listed: there is no series to name for it.
///
/// Hidden works are left out because what reads this names them back: a series a drop
/// resembles, or one it would move, answered to a child, is a series the library does not
/// show that child.
pub fn works(db: &Db, ceiling: &Ceiling) -> Result<Vec<Titled>> {
    let sql = format!(
        "SELECT t.id, t.name, t.title, t.path, t.universe_id, t.series,
                (SELECT COUNT(*) FROM entry x
                  WHERE x.edition_id = t.series AND x.type = 'VOLUME') AS volumes
         FROM (SELECT w.id, w.name, w.title, w.path, w.universe_id,
                      (SELECT e.id FROM edition e WHERE e.work_id = w.id
                        ORDER BY e.implicit DESC, e.path LIMIT 1) AS series
               FROM work w
               WHERE 1 = 1{}) t
         ORDER BY t.path",
        ceiling.and_work("w")
    );
    let rows = db.read(|cx| {
        cx.query(&sql, [], |r| {
            Ok((
                r.get::<_, String>(0)?,
                r.get::<_, String>(1)?,
                r.get::<_, Option<String>>(2)?,
                r.get::<_, String>(3)?,
                r.get::<_, Option<String>>(4)?,
                r.get::<_, Option<String>>(5)?,
                unsigned::<u32>(r.get::<_, i64>(6)?, 6)?,
            ))
        })
    })?;
    Ok(rows
        .into_iter()
        .filter_map(|(id, name, title, path, universe_id, series, volumes)| {
            let path = PathBuf::from(path);
            Some(Titled {
                // Read from the id and not stored: a folder whose sidecar could not carry an
                // identity keeps the one its place gives it. The limit: an id the scan had
                // to replace because another folder claimed the same one is not the place's
                // either, so it reads as declared though it was not this folder's choice.
                identity_declared: id != id_of(&path, ""),
                work_id: id,
                series_id: series?,
                name,
                title,
                path,
                universe_id,
                volumes,
            })
        })
        .collect())
}
