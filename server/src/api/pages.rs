//! Serving a page, and the cache that means it is only ever resized once.
//!
//! Four decisions carry this file, and each of them was arrived at by measuring:
//!
//!  - **the cache is consulted before the archive is opened.** It used to be consulted
//!    inside the resize, which meant a hit still paid for unzipping and reading a megabyte
//!    of page — the whole cost the cache exists to avoid. Everything the decision needs, the
//!    width and height of the source included, is already in the index.
//!  - **resizing is refused when its benefit can be ruled out beforehand.** Decoding a
//!    1700-wide WebP, scaling it to 1600 and re-encoding costs some seven hundred
//!    milliseconds and gives back a file the same size.
//!  - **a spread gets twice the width.** `?width` is the width you want *per page*; a page
//!    wider than it is tall holds two, and each half would otherwise come back at half the
//!    resolution of a single page.
//!  - **the answer's tag is known before anything is read.** It is built from the key, the
//!    file's modification time and the width, so a client revalidating what it holds gets
//!    its 304 without the archive being opened — and a page whose resize turned out larger
//!    than the source is remembered as such, rather than decoded again on every request.

use std::collections::HashSet;
use std::io::Cursor;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::mpsc::{sync_channel, SyncSender};
use std::sync::Arc;
use std::sync::{Mutex, OnceLock};

use anyhow::Result;
use fast_image_resize::images::Image as FirImage;
use fast_image_resize::{PixelType, ResizeAlg, ResizeOptions, Resizer};
use sha2::{Digest, Sha256};

use super::flight::{Flights, Takeoff, PATIENCE};
use crate::archive::cbz;
use crate::store::Db;

/// Above this, a "width" is a client bug rather than a request.
const MAX_WIDTH: u32 = 4096;

/// What a cover is asked for at when nobody says. Small: it is a tile on a shelf.
///
/// A step of `WIDTH_STEP`, and said as one: it was 300, which is served at 400 since widths
/// went by steps, and a constant that is never the width served misleads whoever reads it.
const COVER_WIDTH: u32 = 400;

/// What a resized page is encoded as.
///
/// WebP, unless the page is too tall for one: a WebP counts its rows in fourteen bits, and
/// a stitched webtoon strip shrunk to a reading width is routinely past 16 383 of them.
/// Sent as the original instead, a strip of 20 000 rows is exactly the waste the resize
/// exists to remove — so it goes out as a JPEG, which counts them in sixteen.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum Encoding {
    WebP,
    Jpeg,
}

impl Encoding {
    fn media_type(self) -> &'static str {
        match self {
            Encoding::WebP => "image/webp",
            Encoding::Jpeg => "image/jpeg",
        }
    }

    fn extension(self) -> &'static str {
        match self {
            Encoding::WebP => "webp",
            Encoding::Jpeg => "jpg",
        }
    }

    /// The most rows or columns the format can hold.
    fn max_side(self) -> u32 {
        match self {
            Encoding::WebP => 16383,
            Encoding::Jpeg => u32::from(u16::MAX),
        }
    }

    /// The format to shrink a page into, given the rows it comes out at: nothing when no
    /// format can hold them, or there are none.
    fn for_rows(rows: f64) -> Option<Encoding> {
        if !(1.0..=f64::from(Encoding::Jpeg.max_side())).contains(&rows) {
            None
        } else if rows <= f64::from(Encoding::WebP.max_side()) {
            Some(Encoding::WebP)
        } else {
            Some(Encoding::Jpeg)
        }
    }

    /// The quality this format is asked for, from the one configured for WebP.
    ///
    /// The dial is calibrated on WebP: measured on the real library, WebP at 75 has the
    /// fidelity of JPEG at 85. Ten points up is the same dial read for a JPEG.
    fn quality(self, configured: u8) -> u8 {
        match self {
            Encoding::WebP => configured,
            Encoding::Jpeg => configured.saturating_add(10).min(100),
        }
    }

    /// What goes into the tag of a page encoded this way.
    fn token(self, configured: u8) -> String {
        match self {
            Encoding::WebP => format!("webp-m4-q{}", self.quality(configured)),
            Encoding::Jpeg => format!("jpeg-q{}", self.quality(configured)),
        }
    }
}

/// Widths are served by steps of this many pixels, rounded up.
///
/// Every width a client may ask for is a decode, an entry in the cache and, for a page not
/// worth resizing, a marker — and any key that can read can ask for all 4096 of them on
/// every page. Rounded up to a step, a page has at most 21 entries whatever it is asked, and
/// two windows a few pixels apart share one. Up rather than to the nearest, so a client
/// never gets fewer pixels than it asked for; the same steps the desktop client already
/// asks by, so its requests land on a step rather than between two.
const WIDTH_STEP: u32 = 200;

/// How many pages are prepared ahead of the one being read.
const AHEAD: i64 = 4;

/// How many cover widths are remembered for warming.
const WIDTHS_REMEMBERED: usize = 2;

/// Below this ratio the resize gives back a file no smaller than the source, so it is not
/// done at all: 1600 out of a 1700-wide page is work for nothing.
const WORTH_RESIZING: f64 = 0.8;

/// What crosses the wire, and the tag that lets a client keep it.
pub struct ServedImage {
    pub bytes: Vec<u8>,
    pub media_type: String,
    pub tag: String,
}

/// What a request comes to once the tag the client already holds has been looked at.
pub enum Served {
    Image(ServedImage),
    /// The client holds this tag already, and nothing was read to say so.
    Unchanged(String),
}

impl Served {
    /// The image, or nothing when the answer was that the client already has it.
    pub fn into_image(self) -> Option<ServedImage> {
        match self {
            Served::Image(image) => Some(image),
            Served::Unchanged(_) => None,
        }
    }
}

pub struct Pages {
    db: Arc<Db>,
    /// The pages currently claimed for preparation, so the same one is not prepared twice.
    in_flight: Mutex<HashSet<String>>,
    /// Set once warming has been started. Absent in a test that does not want threads.
    warm: OnceLock<SyncSender<Warm>>,
    cache_root: PathBuf,
    quality: u8,
    max_cache_bytes: u64,
    written_since_check: AtomicU64,
    check_every: u64,
    /// The cover widths a client has asked for, most recent first, kept to a handful.
    ///
    /// Bounded on purpose: a client that sweeps through widths must not turn this into a
    /// list of everything it ever tried, and then warm all of it.
    seen_widths: Mutex<Vec<u32>>,
    /// How many times a page has been decoded to be shrunk.
    ///
    /// The same reason `Cx` counts queries: the defects that hurt here do not fail anything.
    /// A page the encoder will refuse comes back correct either way — as the original, which
    /// is the right answer — and the only difference between deciding that from the index and
    /// deciding it after decoding the whole strip is 116 ms of blocking pool per request,
    /// repeated for ever because that path stores nothing. Nothing observable moves, so
    /// nothing but a count can hold it still.
    ///
    /// On the instance and not in a `static`, unlike `cbz::OPENED`: tests in one binary share
    /// a process, and a global count is one a test can only print. Each `Pages` has its own.
    decoded: AtomicU64,
    /// The resizes in progress, by tag, so the same one is never done twice at once.
    flights: Flights,
}

impl Pages {
    pub fn new(db: Arc<Db>, cache_root: PathBuf, quality: u8, max_cache_bytes: u64) -> Self {
        // Sweeping on every write would stat the whole cache for every page; sweeping too
        // rarely lets it overshoot. An eighth of the budget is the compromise, floored so a
        // small budget still gets swept and capped so a large one does not wait for ever.
        //
        // The floor used to be 4 MB, which silently restored the old behaviour under a
        // 32 MB budget — found by the test that was written to guard the fix.
        let check_every = (max_cache_bytes / 8).clamp(64 * 1024, 256 * 1024 * 1024);
        Pages {
            db,
            in_flight: Mutex::new(HashSet::new()),
            warm: OnceLock::new(),
            cache_root,
            quality,
            max_cache_bytes,
            written_since_check: AtomicU64::new(0),
            check_every,
            seen_widths: Mutex::new(Vec::new()),
            decoded: AtomicU64::new(0),
            flights: Flights::default(),
        }
    }

    /// How many pages this instance has decoded in order to shrink them.
    pub fn decodes(&self) -> u64 {
        self.decoded.load(Ordering::Relaxed)
    }

    pub fn prepare(&self) {
        if let Err(e) = std::fs::create_dir_all(&self.cache_root) {
            tracing::warn!(cache = %self.cache_root.display(), error = %e, "cache unavailable");
        }
    }

    /// A page, at the width asked for. Without a width the original bytes go back untouched.
    pub fn page(
        &self,
        entry_id: &str,
        number: i64,
        width: Option<u32>,
    ) -> Result<Option<ServedImage>> {
        Ok(self
            .page_or_unchanged(entry_id, number, width, None)?
            .and_then(Served::into_image))
    }

    /// A page, or word that the client already holds it when `held` is its tag.
    pub fn page_or_unchanged(
        &self,
        entry_id: &str,
        number: i64,
        width: Option<u32>,
        held: Option<&str>,
    ) -> Result<Option<Served>> {
        self.page_for(entry_id, number, width, held, false)
    }

    /// A page for the warming, which skips one somebody is already making rather than
    /// waiting for it: it has other pages to prepare.
    fn warm_page(&self, entry_id: &str, number: i64, width: u32) -> Result<()> {
        self.page_for(entry_id, number, Some(width), None, true)
            .map(|_| ())
    }

    fn page_for(
        &self,
        entry_id: &str,
        number: i64,
        width: Option<u32>,
        held: Option<&str>,
        warming: bool,
    ) -> Result<Option<Served>> {
        let width = width.map(stepped);
        let row = self.db.read(|cx| {
            cx.query_one(
                "SELECT e.file, e.modified_at, p.entry_name, p.media_type, p.width, p.height
                 FROM page p JOIN entry e ON e.id = p.entry_id
                 WHERE p.entry_id = ?1 AND p.number = ?2",
                (entry_id, number),
                |r| {
                    Ok(Source {
                        file: r.get(0)?,
                        modified_at: r.get::<_, i64>(1)?,
                        entry_name: r.get(2)?,
                        media_type: r.get(3)?,
                        width: r.get(4)?,
                        height: r.get(5)?,
                    })
                },
            )
        })?;
        let Some(source) = row else { return Ok(None) };
        self.serve(
            &source,
            width,
            &format!("{entry_id}/{number}"),
            held,
            warming,
        )
    }

    /// The cover of an entry. A thumbnail is a page asked for narrow; there is no separate
    /// thing.
    pub fn cover(&self, entry_id: &str, width: Option<u32>) -> Result<Option<ServedImage>> {
        Ok(self
            .cover_or_unchanged(entry_id, width, None)?
            .and_then(Served::into_image))
    }

    pub fn cover_or_unchanged(
        &self,
        entry_id: &str,
        width: Option<u32>,
        held: Option<&str>,
    ) -> Result<Option<Served>> {
        // A cover chosen on disk wins over page zero — that is what "cover.jpg beside the
        // volumes" is for.
        let chosen: Option<String> = self.db.read(|cx| {
            cx.query_one(
                "SELECT cover_file FROM entry WHERE id = ?1",
                [entry_id],
                |r| r.get::<_, Option<String>>(0),
            )
            .map(Option::flatten)
        })?;
        let requested = tile_width(width);
        if let Some(path) = chosen {
            return self.serve_file(Path::new(&path), requested, held);
        }
        // Not a read: a cover is a tile on a shelf, and nobody has opened the volume.
        self.page_or_unchanged(entry_id, 0, Some(requested), held)
    }

    /// The tile of a grid: one request, not one to find the first entry and another for its
    /// cover.
    pub fn series_cover(
        &self,
        edition_id: &str,
        width: Option<u32>,
    ) -> Result<Option<ServedImage>> {
        Ok(self
            .series_cover_or_unchanged(edition_id, width, None)?
            .and_then(Served::into_image))
    }

    pub fn series_cover_or_unchanged(
        &self,
        edition_id: &str,
        width: Option<u32>,
        held: Option<&str>,
    ) -> Result<Option<Served>> {
        let requested = stepped(tile_width(width));
        // Remembered at the width it is served at, never at the one that was typed. A `0`
        // remembered here is a width nothing is ever stored under: it pushed the two real
        // widths out of the list — `WIDTHS_REMEMBERED` is two — and the sweep after the next
        // scan then read every cover in the library, decided each one was not worth
        // resizing, stored nothing, and reported itself done. A shelf as cold as before, and
        // a full pass over the library to leave it that way.
        if width.is_some() {
            self.remember_width(requested);
        }
        let chosen: Option<String> = self.db.read(|cx| {
            cx.query_one(
                "SELECT cover_file FROM edition WHERE id = ?1",
                [edition_id],
                |r| r.get::<_, Option<String>>(0),
            )
            .map(Option::flatten)
        })?;
        if let Some(path) = chosen {
            return self.serve_file(Path::new(&path), requested, held);
        }
        let first: Option<String> = self.db.read(|cx| {
            cx.query_one(
                "SELECT id FROM entry WHERE edition_id = ?1 ORDER BY sort_key, file LIMIT 1",
                [edition_id],
                |r| r.get::<_, String>(0),
            )
        })?;
        match first {
            Some(entry) => self.cover_or_unchanged(&entry, Some(requested), held),
            None => Ok(None),
        }
    }

    /// An image sitting on the disk rather than inside an archive.
    ///
    /// Keyed on its own path: it belongs to no entry in particular — an edition cover is one
    /// file speaking for a whole series — and the path is what identifies it.
    ///
    /// The index holds no dimensions for such a file, so whether it is worth resizing is only
    /// known once it has been read — and is remembered from then on like a resize that came
    /// out larger, so that the next request for it reads nothing it does not have to.
    fn serve_file(
        &self,
        path: &Path,
        requested: u32,
        held: Option<&str>,
    ) -> Result<Option<Served>> {
        let requested = stepped(requested);
        let Ok(meta) = std::fs::metadata(path) else {
            return Ok(None);
        };
        if !meta.is_file() {
            return Ok(None);
        }
        let key = path.to_string_lossy().to_string();
        let modified = meta
            .modified()
            .ok()
            .and_then(|t| t.duration_since(std::time::UNIX_EPOCH).ok())
            .map(|d| d.as_millis() as i64)
            .unwrap_or(0);
        let original = self.tag(&key, modified, None);
        // WebP, decided before the file is read: its dimensions are not in the index, and the
        // tag has to be known before anything is read. A cover too tall for a WebP at a tile's
        // width is not a cover anyone has, so it is simply served as it is — see below.
        let etag = self.tag(&key, modified, Some((requested, Encoding::WebP)));
        let not_worth = self.known_not_worth(&etag);
        if let Some(unchanged) = unchanged(held, if not_worth { &original } else { &etag }) {
            return Ok(Some(unchanged));
        }
        if !not_worth {
            if let Some(hit) = self.cached(&etag, Encoding::WebP) {
                return Ok(Some(Served::Image(hit)));
            }
        }
        // The shelf sweep and a reader looking at the shelf ask for the same tiles: one makes
        // a tile, the other waits for it. See `serve`.
        let flight = if not_worth {
            None
        } else {
            match self.flights.take_off(&etag, Some(PATIENCE)) {
                Takeoff::Mine(flight) => Some(flight),
                Takeoff::Landed => {
                    if let Some(hit) = self.cached(&etag, Encoding::WebP) {
                        return Ok(Some(Served::Image(hit)));
                    }
                    if self.known_not_worth(&etag) {
                        None
                    } else {
                        self.flights.try_take_off(&etag)
                    }
                }
                Takeoff::TimedOut | Takeoff::Busy => None,
            }
        };
        let not_worth = not_worth || (flight.is_none() && self.known_not_worth(&etag));

        let Ok(bytes) = std::fs::read(path) else {
            return Ok(None);
        };
        let media_type = crate::archive::images::media_type(&bytes)
            .unwrap_or("application/octet-stream")
            .to_string();
        let plain = |bytes: Vec<u8>| ServedImage {
            tag: original.clone(),
            bytes,
            media_type: media_type.clone(),
        };
        if not_worth {
            return Ok(Some(Served::Image(plain(bytes))));
        }
        let dimensions = crate::archive::images::dimension(&bytes);
        let plan = self.plan(
            dimensions.map(|(w, _)| w as i64),
            dimensions.map(|(_, h)| h as i64),
            requested,
            lossless(&media_type) || lossless_webp(&bytes),
        );
        if !plan.worth_resizing || plan.encoding != Encoding::WebP {
            self.remember_not_worth(&etag);
            if let Some(unchanged) = unchanged(held, &original) {
                return Ok(Some(unchanged));
            }
            return Ok(Some(Served::Image(plain(bytes))));
        }
        let served = self.shrink(bytes, &plan, &etag, &key, plain);
        Ok(Some(match unchanged(held, &served.tag) {
            Some(unchanged) => unchanged,
            None => Served::Image(served),
        }))
    }

    /// The tag is decided before anything is read, and compared with the one the client holds
    /// before the archive is opened.
    ///
    /// Measured on a real library over a 4.5 ms network: a revalidation of a page whose
    /// resize came out larger took 194 ms to say 304, because the tag was compared only once
    /// the page had been extracted, decoded, resized and re-encoded — all of it to be thrown
    /// away. The tag never needed any of that: it is the key, the file's modification time and
    /// the width.
    fn serve(
        &self,
        source: &Source,
        requested: Option<u32>,
        key: &str,
        held: Option<&str>,
        warming: bool,
    ) -> Result<Option<Served>> {
        let original = self.tag(key, source.modified_at, None);
        let Some(requested) = requested else {
            return self.extracted(source, original, held);
        };

        // Consulted before the archive is opened. Everything the decision needs is in the
        // index already.
        let plan = self.plan(
            source.width,
            source.height,
            requested,
            lossless(&source.media_type),
        );
        if !plan.worth_resizing {
            return self.extracted(source, original, held);
        }
        let etag = self.tag(key, source.modified_at, Some((requested, plan.encoding)));
        if self.known_not_worth(&etag) {
            return self.extracted(source, original, held);
        }
        if let Some(unchanged) = unchanged(held, &etag) {
            return Ok(Some(unchanged));
        }
        if let Some(hit) = self.cached(&etag, plan.encoding) {
            return Ok(Some(Served::Image(hit)));
        }

        // Whoever is already making this page makes it for everyone: a reader asking for the
        // page the warming is on waits for it and reads it from the cache, rather than
        // decoding and encoding it a second time beside it.
        let _flight = match self.flights.take_off(&etag, (!warming).then_some(PATIENCE)) {
            Takeoff::Mine(flight) => Some(flight),
            Takeoff::Busy => return Ok(None),
            Takeoff::TimedOut => None,
            Takeoff::Landed => {
                if self.known_not_worth(&etag) {
                    return self.extracted(source, original, held);
                }
                if let Some(hit) = self.cached(&etag, plan.encoding) {
                    return Ok(Some(Served::Image(hit)));
                }
                // It landed with nothing to show — a panic, a cache that could not be
                // written. The work is ours, then.
                self.flights.try_take_off(&etag)
            }
        };

        let Some(bytes) = cbz::extract(Path::new(&source.file), &source.entry_name)? else {
            return Ok(None);
        };
        let plain = |bytes: Vec<u8>| ServedImage {
            tag: original.clone(),
            bytes,
            media_type: source.media_type.clone(),
        };
        let served = self.shrink(bytes, &plan, &etag, key, plain);
        // A revalidation of a tag the client could only have got from an earlier answer that
        // was the original: the resize has just been found not worth it, and that is what it
        // holds.
        Ok(Some(match unchanged(held, &served.tag) {
            Some(unchanged) => unchanged,
            None => Served::Image(served),
        }))
    }

    /// The member itself, unless the client already holds it under `tag`.
    fn extracted(
        &self,
        source: &Source,
        tag: String,
        held: Option<&str>,
    ) -> Result<Option<Served>> {
        if let Some(unchanged) = unchanged(held, &tag) {
            return Ok(Some(unchanged));
        }
        let Some(bytes) = cbz::extract(Path::new(&source.file), &source.entry_name)? else {
            return Ok(None);
        };
        Ok(Some(Served::Image(ServedImage {
            tag,
            bytes,
            media_type: source.media_type.clone(),
        })))
    }

    /// What a request for a width actually means for this page, decided from the index alone.
    ///
    /// `lossless` says the source is stored without loss, and then a width close to its own,
    /// or past it, still goes through the encoder — at its own width rather than larger. A
    /// PNG scan is 3 to 8 MB where a lossy WebP of it is a tenth of that: serving it as it is
    /// because 1600 is close to 1920 sends the whole of it and has the client decode the
    /// whole of it, to show the same page. A lossy source is a different case: shrinking it
    /// by a fifth re-encodes losses already made, for bytes it mostly does not save.
    fn plan(
        &self,
        source_width: Option<i64>,
        source_height: Option<i64>,
        requested: u32,
        lossless: bool,
    ) -> Plan {
        let spread = matches!((source_width, source_height), (Some(w), Some(h)) if w > h);
        let target = if spread {
            requested.saturating_mul(2)
        } else {
            requested
        }
        .clamp(1, MAX_WIDTH);
        if lossless && requested > 0 {
            if let Some(w) = source_width.and_then(|w| u32::try_from(w).ok()) {
                let target = target.min(w);
                let encoding = encoding_for(target, i64::from(w), source_height);
                return Plan {
                    target,
                    worth_resizing: encoding.is_some(),
                    at_own_width: true,
                    encoding: encoding.unwrap_or(Encoding::WebP),
                };
            }
        }
        let encoding = source_width.map_or(Some(Encoding::WebP), |w| {
            encoding_for(target, w, source_height)
        });
        Plan {
            target,
            at_own_width: lossless,
            encoding: encoding.unwrap_or(Encoding::WebP),
            // A request for no columns at all is not a request for a width, and the clamp
            // above made it into one: `?width=0` came out as "shrink this page to a single
            // column", which on a spread is a JPEG one pixel wide. Being smaller than the
            // source was the only test it had to pass, so it went into the cache under the
            // ETag for the width that was asked for and came back out for a year behind
            // `max-age=31536000`. The ceiling had a guard; the floor invented a value.
            worth_resizing: requested > 0
                && match source_width {
                    None => true,
                    Some(w) => f64::from(target) < w as f64 * WORTH_RESIZING && encoding.is_some(),
                },
        }
    }

    /// Resize, keep the result under `etag`, and refuse to hand back something worse than the
    /// source — in which case `plain` makes the original into the answer.
    ///
    /// Keyed on what the caller asked for, not on the width we end up using: doubling a
    /// spread is an internal decision, and the answer is deterministic either way.
    fn shrink(
        &self,
        source: Vec<u8>,
        plan: &Plan,
        etag: &str,
        key: &str,
        plain: impl Fn(Vec<u8>) -> ServedImage,
    ) -> ServedImage {
        // Counted here rather than inside `resize`, because this is the line that commits to
        // the decode: everything above decided whether to reach it.
        self.decoded.fetch_add(1, Ordering::Relaxed);
        let resized = match resize(
            &source,
            plan.target,
            plan.encoding,
            plan.encoding.quality(self.quality),
            plan.at_own_width,
        ) {
            Ok(bytes) => bytes,
            Err(e) => {
                // A codec we cannot read is not a reason to fail: serve the original. And
                // remember it: the same bytes will fail the same way next time.
                tracing::warn!(key, error = %e, "could not resize");
                self.remember_not_worth(etag);
                return plain(source);
            }
        };

        // A resize can come out larger than its source — a third to a half of the WebP pages
        // of the real library did, when this encoded JPEG. Serving that would be absurd, so
        // the original goes back instead. The width still had a point: what matters on a
        // phone is the decoded bitmap — 1695×2101 costs 14 MB of memory, 1080 wide costs 6.
        //
        // **And the decision is kept.** It used to be thrown away with the image, so every
        // later request for that page at that width decoded, resized and re-encoded it again:
        // 130 to 230 ms on every request, against 10 to 15 for a page in the cache — measured
        // on four of the first twelve pages of a volume of WebP. The marker sits in the cache
        // under the same tag, so it survives a restart, is trimmed with the pages, and is
        // orphaned by a new modification time like they are.
        if resized.len() >= source.len() {
            self.remember_not_worth(etag);
            return plain(source);
        }

        self.store(etag, plan.encoding, &resized);
        ServedImage {
            bytes: resized,
            media_type: plan.encoding.media_type().to_string(),
            tag: etag.to_string(),
        }
    }

    /// Whether a resize at this tag has already been found to give back nothing smaller.
    ///
    /// Touched like a page read from the cache, so the trim takes the markers nobody asks for
    /// rather than the ones in use.
    fn known_not_worth(&self, etag: &str) -> bool {
        touch(&self.marker_path(etag)).is_ok()
    }

    fn remember_not_worth(&self, etag: &str) {
        let path = self.marker_path(etag);
        let written = path
            .parent()
            .map(std::fs::create_dir_all)
            .transpose()
            .and_then(|_| crate::store::files::write_whole(&path, &[]));
        if let Err(e) = written {
            tracing::warn!(cache = %path.display(), error = %e, "cache not written");
            return;
        }
        self.wrote(0);
    }

    /// A resized page already on disk, or nothing.
    ///
    /// Reading touches the file, so what goes first when the cache is trimmed is what has
    /// not been opened for longest — not what happened to be written first.
    fn cached(&self, etag: &str, encoding: Encoding) -> Option<ServedImage> {
        let path = self.cache_path(etag, encoding);
        let bytes = std::fs::read(&path).ok()?;
        let _ = touch(&path);
        Some(ServedImage {
            bytes,
            media_type: encoding.media_type().to_string(),
            tag: etag.to_string(),
        })
    }

    fn store(&self, etag: &str, encoding: Encoding, bytes: &[u8]) {
        let path = self.cache_path(etag, encoding);
        let written = path
            .parent()
            .map(std::fs::create_dir_all)
            .transpose()
            .and_then(|_| crate::store::files::write_whole(&path, bytes));
        if let Err(e) = written {
            tracing::warn!(cache = %path.display(), error = %e, "cache not written");
            return;
        }
        self.wrote(bytes.len() as u64);
    }

    /// Counts what was just written against the budget, at its cost on the disk, and trims
    /// the cache when enough has gone in since the last look.
    ///
    /// A marker goes through here like a page. Left out, it was the one write the budget
    /// never saw.
    fn wrote(&self, len: u64) {
        let cost = len.max(super::cache_budget::FLOOR);
        let so_far = self.written_since_check.fetch_add(cost, Ordering::Relaxed) + cost;
        if so_far > self.check_every {
            self.written_since_check.store(0, Ordering::Relaxed);
            super::cache_budget::enforce(&self.cache_root, self.max_cache_bytes);
        }
    }

    /// The tag carries the file's modification time, so a retouched volume invalidates its
    /// cached pages on its own — no sweep to run, no stale image to explain.
    ///
    /// A resized tag also names how it was encoded. The cache outlives a change of encoder:
    /// without it, a JPEG kept from before would be served under a WebP's media type, and a
    /// marker saying a page was not worth a JPEG would deny it a WebP that is half the size.
    /// With it, the old entries are simply never asked for again and age out under the
    /// budget. The quality is in it for the same reason: a page encoded at another one is
    /// another image.
    fn tag(&self, key: &str, modified_at: i64, resized: Option<(u32, Encoding)>) -> String {
        let width = resized
            .map(|(w, encoding)| format!("{w}/{}", encoding.token(self.quality)))
            .unwrap_or_else(|| "src".to_string());
        let digest = Sha256::digest(format!("{key}/{modified_at}/{width}").as_bytes());
        digest.iter().map(|b| format!("{b:02x}")).collect()
    }

    /// Two levels of fan-out: a single folder of a hundred thousand files ages badly.
    fn cache_path(&self, tag: &str, encoding: Encoding) -> PathBuf {
        self.cache_root
            .join(&tag[..2])
            .join(format!("{tag}.{}", encoding.extension()))
    }

    /// Beside the page it stands for, empty: its existence is the whole of what it says.
    fn marker_path(&self, tag: &str) -> PathBuf {
        self.cache_root
            .join(&tag[..2])
            .join(format!("{tag}.original"))
    }

    // --------------------------------------------------------------- warming

    /// Starts the threads that prepare pages ahead of the reader.
    ///
    /// Takes `Arc<Self>` because the workers call back into `page`. Weakly, so that
    /// dropping the last strong reference stops them rather than keeping the whole cache
    /// alive for the life of the process.
    ///
    /// Not started at all in tests that do not want threads: everything below is an
    /// optimisation, and the server answers correctly without it.
    pub fn start_warming(self: &Arc<Self>, workers: usize, queue: usize) {
        let (sender, receiver) = sync_channel::<Warm>(queue.max(1));
        if self.warm.set(sender).is_err() {
            return;
        }
        let receiver = Arc::new(Mutex::new(receiver));
        for _ in 0..workers.max(1) {
            let receiver = Arc::clone(&receiver);
            let pages = Arc::downgrade(self);
            std::thread::Builder::new()
                .name("leaf-warm".into())
                .spawn(move || loop {
                    let task = {
                        let Ok(guard) = receiver.lock() else { return };
                        guard.recv()
                    };
                    let Ok(task) = task else { return };
                    let Some(pages) = pages.upgrade() else { return };
                    // Given back by a destructor, so a panic in a decoder releases it too.
                    // Left to the line below, one panic both killed the worker and left the
                    // page marked in flight for ever — so it was never prepared again, and
                    // the same page stuttered on every read.
                    let _claim = Claim(&pages, &task.key);
                    if let Err(e) = pages.warm_page(&task.id, task.number, task.width) {
                        tracing::debug!(key = %task.key, error = %e, "could not warm");
                    }
                })
                .expect("spawning a warming thread");
        }
    }

    /// Prepares the next few pages while you read this one.
    ///
    /// Resizing costs several hundred milliseconds on first read and is then cached, which
    /// means reading straight through pays it on every single page — the one moment the
    /// cache never helps. Reading is the one access pattern that can be predicted, so the
    /// work moves off the critical path: by the time you turn the page it is already done.
    ///
    /// Only when a width was asked for, since serving the source costs nothing to begin
    /// with.
    pub fn warm_ahead(&self, entry_id: &str, number: i64, width: u32) {
        self.warm_pages(entry_id, number + 1..=number + AHEAD, width);
    }

    /// Queues these pages of one entry for preparation at this width.
    fn warm_pages(&self, entry_id: &str, numbers: std::ops::RangeInclusive<i64>, width: u32) {
        // And never at a width nothing is stored under. `?width=0` is served as the page
        // itself — `plan` refuses to shrink to no columns — so the four pages queued behind
        // it were four archives opened and four members read to store nothing at all, on
        // every page turn.
        if width == 0 {
            return;
        }
        // Claimed at the width it will be served at, so two windows a few pixels apart do
        // not prepare the same page twice.
        let width = stepped(width);
        let Some(sender) = self.warm.get() else {
            return;
        };
        for next in numbers {
            let key = format!("{entry_id}/{next}@{width}");
            if !self.claim(&key) {
                continue;
            }
            let task = Warm {
                key: key.clone(),
                id: entry_id.to_string(),
                number: next,
                width,
            };
            // A full queue means we are behind the reader. What is dropped is the newest
            // request — the page furthest ahead — because the oldest is the one about to be
            // turned to.
            //
            // And a dropped task has to give its claim back. A queue that drops in silence
            // would leave the page marked in flight for ever: one stutter, at the same page,
            // every time it is read. Here the value comes back in the error, so releasing it
            // is the only thing there is to do with it.
            if sender.try_send(task).is_err() {
                self.release(&key);
            }
        }
    }

    /// Prepares the opening pages of a volume that is about to be read.
    ///
    /// Read-ahead only starts once a first page has been asked for, so opening a volume
    /// paid full price for its first two or three pages while the pipeline filled — the one
    /// moment a reader is actually waiting. Listing the pages of a volume, with the width it
    /// will be read at, is the earliest unambiguous sign that it is about to be.
    ///
    /// From the page the reader lands on, that page included, and the four after it. It
    /// used to be pages 1 to 4 whatever the volume: never the page shown first, and on a
    /// volume resumed at page 57 nothing it would show at all. Measured on the real server,
    /// the list asked at 1200 and six seconds left to warm: page 0 still took 300 ms, pages 1
    /// to 4 took 8 to 15, and page 5 took 770 — every opening waited on its first page.
    pub fn warm_opening(&self, entry_id: &str, width: Option<u32>) {
        let Some(width) = width else { return };
        if width == 0 || self.warm.get().is_none() {
            return;
        }
        let landing = self.landing(entry_id);
        self.warm_pages(entry_id, landing..=landing + AHEAD, width);
    }

    /// The page a reader opening this entry lands on: where its progress stands, or the
    /// first page when it has none or was finished.
    ///
    /// Finished is the start, because that is what the client does with it: a finished volume
    /// opened again is a volume read again, and it opens on the cover. Progress is the
    /// library's, not a key's — keys are not people here — so the stored one is the only one
    /// there is. One query, and none at all without a warming queue to feed.
    fn landing(&self, entry_id: &str) -> i64 {
        self.db
            .read(|cx| {
                cx.query_one(
                    "SELECT page FROM progress WHERE entry_id = ?1 AND finished = 0",
                    [entry_id],
                    |r| r.get::<_, i64>(0),
                )
            })
            .ok()
            .flatten()
            .unwrap_or(0)
            .max(0)
    }

    /// Prepares the shelf after a scan, at the widths a client has actually asked for.
    ///
    /// A cover costs 168 ms to make and 6 ms afterwards, so the first look at a shelf of
    /// five hundred series is a quarter of a minute of tiles trickling in, and every look
    /// after it is instant. Building them during the scan is the other way round: it makes
    /// the scan as slow as the covers are numerous, and it needs a queue that survives a
    /// restart. Here the scan stays the fast thing and this runs after it, on the warming
    /// threads that already exist, on a library that is already browsable.
    ///
    /// Never at a guessed width: a width nobody requests is a cache entry nobody reads, and
    /// the client stays free to want whatever it wants without the server being told.
    pub fn warm_covers(self: &Arc<Self>) {
        let widths = self
            .seen_widths
            .lock()
            .map(|w| w.clone())
            .unwrap_or_default();
        if widths.is_empty() {
            return;
        }

        // A thread of its own rather than the read-ahead queue. That queue is four pages
        // deep per worker **on purpose** — when it fills, the page furthest ahead is
        // dropped, because the reader is about to turn to the nearest one. Handing it five
        // hundred covers meant sixteen were prepared and the rest thrown away in silence:
        // the sweep did nothing and said nothing.
        //
        // Sequential, and slow, and that is right: the library is already browsable, the
        // tiles fill in behind you, and nothing a reader asks for is ever queued behind a
        // shelf.
        let pages = Arc::downgrade(self);
        let started = std::thread::Builder::new()
            .name("leaf-covers".into())
            .spawn(move || {
                let Some(pages) = pages.upgrade() else { return };
                let editions: Vec<String> = pages
                    .db
                    .read(|cx| cx.query("SELECT id FROM edition", [], |r| r.get::<_, String>(0)))
                    .unwrap_or_default();
                let (mut done, wanted) = (0usize, editions.len() * widths.len());
                for id in editions {
                    for width in &widths {
                        let key = format!("cover:{id}@{width}");
                        // Claimed, so a request arriving for the same tile does not make it
                        // twice — and released whatever happens, or it is never made again.
                        if !pages.claim(&key) {
                            continue;
                        }
                        let released = Claim(&pages, &key);
                        if let Err(e) = pages.series_cover(&id, Some(*width)) {
                            tracing::debug!(key, error = %e, "could not warm");
                        }
                        drop(released);
                        done += 1;
                    }
                }
                tracing::info!(done, wanted, "shelf covers prepared");
            });
        if let Err(e) = started {
            tracing::warn!(error = %e, "no thread for the shelf covers");
        }
    }

    fn remember_width(&self, width: u32) {
        let Ok(mut seen) = self.seen_widths.lock() else {
            return;
        };
        seen.retain(|w| *w != width);
        seen.insert(0, width);
        seen.truncate(WIDTHS_REMEMBERED);
    }

    /// How many pages are currently claimed for preparation.
    ///
    /// Exposed so a test can watch it come back to nothing. A claim that is never given
    /// back means that page is never prepared again.
    pub fn pending(&self) -> usize {
        self.in_flight.lock().map(|s| s.len()).unwrap_or(0)
    }

    fn claim(&self, key: &str) -> bool {
        self.in_flight
            .lock()
            .map(|mut s| s.insert(key.to_string()))
            .unwrap_or(false)
    }

    fn release(&self, key: &str) {
        if let Ok(mut s) = self.in_flight.lock() {
            s.remove(key);
        }
    }
}

/// Carries the claim it made, so a task that never runs can still give it back.
/// Holds a claim on a page while it is being prepared, and gives it back on the way out.
struct Claim<'a>(&'a Pages, &'a str);

impl Drop for Claim<'_> {
    fn drop(&mut self) {
        self.0.release(self.1);
    }
}

pub struct Warm {
    key: String,
    id: String,
    number: i64,
    width: u32,
}

struct Source {
    file: String,
    modified_at: i64,
    entry_name: String,
    media_type: String,
    width: Option<i64>,
    height: Option<i64>,
}

struct Plan {
    target: u32,
    /// What the page is shrunk into, decided from the rows it comes out at.
    encoding: Encoding,
    /// Whether to go through the encoder at all — resizing, or for a lossless source,
    /// re-encoding at its own width.
    worth_resizing: bool,
    /// Whether a target at or past the source's own width is encoded at that width rather
    /// than refused: true for a lossless source only.
    at_own_width: bool,
}

/// Whether a source of this media type is stored without loss, said by the index alone.
///
/// A WebP can be either, and the media type does not say which: `lossless_webp` reads it
/// from the bytes where they are already in hand.
fn lossless(media_type: &str) -> bool {
    matches!(media_type, "image/png" | "image/bmp" | "image/gif")
}

/// A WebP whose first chunk is `VP8L`, its lossless form.
fn lossless_webp(bytes: &[u8]) -> bool {
    bytes.len() >= 16
        && &bytes[..4] == b"RIFF"
        && &bytes[8..12] == b"WEBP"
        && &bytes[12..16] == b"VP8L"
}

/// The width a request is served at: up to the next step, never past the ceiling, and
/// nought left as nought — it is not a width, and `plan` refuses it as such.
fn stepped(width: u32) -> u32 {
    width
        .div_ceil(WIDTH_STEP)
        .saturating_mul(WIDTH_STEP)
        .min(MAX_WIDTH)
}

/// A 304 in waiting, when the client already holds this tag.
fn unchanged(held: Option<&str>, tag: &str) -> Option<Served> {
    (held == Some(tag)).then(|| Served::Unchanged(tag.to_string()))
}

/// What a cover is asked for at, given what the query said.
///
/// `unwrap_or` cannot tell `?width=0` from no width at all, and kept the nought. `plan` then
/// refuses to shrink to no columns — which is right — so the tile came back as the
/// full-resolution scan, several megabytes of it, and nothing was cached because that path
/// stores nothing: a grid of five hundred tiles fetched five hundred whole pages, every
/// time it was looked at. The ceiling on a width was checked at the door; the floor was not.
fn tile_width(width: Option<u32>) -> u32 {
    width.filter(|w| *w > 0).unwrap_or(COVER_WIDTH)
}

/// The format shrinking to this width can produce, answered from the index — or nothing,
/// when no format can hold the rows it comes out at.
///
/// `resize` refuses a page whose rows do not fit, and refusing is right — but refusing
/// *there* costs the decode first: the member's bytes are read into a full bitmap on the
/// blocking pool, and only then is the answer no. The extraction would have happened anyway,
/// since the original is what goes back; the decode is pure waste. Ten readers on one webtoon
/// strip is ten full decodes per page turn for a decision two columns of the index already
/// hold.
///
/// WebP when the index does not know, because then only `resize` can say.
fn encoding_for(target: u32, source_width: i64, source_height: Option<i64>) -> Option<Encoding> {
    let Some(h) = source_height else {
        return Some(Encoding::WebP);
    };
    if source_width <= 0 || h <= 0 {
        return Some(Encoding::WebP);
    }
    Encoding::for_rows((h as f64 * (f64::from(target) / source_width as f64)).round())
}

/// The pixels, with any transparency laid over white.
///
/// `to_rgb8` **drops** the alpha channel rather than compositing it, and a PNG saved with a
/// transparent background holds black under the clear part — so a page came back with ink
/// where a reader expects paper. A WebP could carry the transparency through, and still
/// does not: these are pages, a reader draws them on paper, and a clear page shown over a
/// dark theme is ink on black. White is what the clear part of a page means.
fn flattened(decoded: image::DynamicImage) -> image::RgbImage {
    if !decoded.color().has_alpha() {
        return decoded.to_rgb8();
    }
    let source = decoded.to_rgba8();
    let mut out = image::RgbImage::new(source.width(), source.height());
    for (to, from) in out.pixels_mut().zip(source.pixels()) {
        let a = u32::from(from.0[3]);
        let over = |c: u8| ((u32::from(c) * a + 255 * (255 - a)) / 255) as u8;
        *to = image::Rgb([over(from.0[0]), over(from.0[1]), over(from.0[2])]);
    }
    out
}

/// Resize on width, aspect ratio preserved, never upscaled, and encode as `encoding`.
///
/// The scaling itself is SIMD and fast — 1.9 ms on a real page, against 31 ms for the
/// scalar implementation in `image`.
///
/// The decode is not: `image` decodes the source in full before it is shrunk — 69 ms for a
/// WebP page of the real library — where a decoder working at a reduced scale would not.
/// Recorded rather than papered over: it only bites on a cache miss.
fn resize(
    source: &[u8],
    width: u32,
    encoding: Encoding,
    quality: u8,
    at_own_width: bool,
) -> Result<Vec<u8>> {
    let decoded = image::ImageReader::new(Cursor::new(source))
        .with_guessed_format()?
        .decode()?;
    let (w0, h0) = (decoded.width(), decoded.height());
    // A lossless source is encoded at its own width rather than refused — see `plan`.
    let width = if at_own_width { width.min(w0) } else { width };
    if width > w0 || (width == w0 && !at_own_width) {
        // Never upscaled: asking for 4000 on a 1500-wide scan gets the original back rather
        // than a blurred enlargement. Before `flattened`, which walks every pixel of the
        // source to lay transparency over white — work that was being done and then dropped
        // on this line.
        anyhow::bail!("not worth resizing");
    }
    let height = ((h0 as f64) * (f64::from(width) / f64::from(w0))).round() as u32;

    // A WebP counts its rows and columns in fourteen bits, a JPEG in sixteen. Unchecked, a stitched webtoon
    // strip — routinely tens of thousands of rows, and a PNG has no such ceiling — is a
    // failed encode at best; when this was a JPEG and the count was cast to sixteen bits, it
    // was a cache entry holding the top sixth of the page, served for a year behind
    // `max-age=31536000`. Refusing here serves the original instead, which is what every
    // other codec we cannot shrink already gets.
    //
    // **Nought is the same arithmetic from the other end.** A page far wider than tall
    // shrinks to no rows at all, and nothing here would otherwise say an image of no pixels
    // is not an image — the day the scaler stops minding, an image of nothing is smaller
    // than the source and goes into the cache for a year like anything else.
    //
    // `plan` decides this from the index, before the decode. This stays for the pages it
    // cannot decide for: an index that never recorded the dimensions.
    let usable = |n: u32| (1..=encoding.max_side()).contains(&n);
    if !usable(width) || !usable(height) {
        anyhow::bail!("{encoding:?} cannot be {width}×{height}");
    }
    let decoded = flattened(decoded);
    if width == w0 {
        return encode(encoding, decoded.as_raw(), w0, h0, quality);
    }

    let from = FirImage::from_vec_u8(w0, h0, decoded.into_raw(), PixelType::U8x3)?;
    let mut into = FirImage::new(width, height, PixelType::U8x3);
    Resizer::new().resize(
        &from,
        &mut into,
        &ResizeOptions::new().resize_alg(ResizeAlg::Convolution(
            fast_image_resize::FilterType::Lanczos3,
        )),
    )?;
    encode(encoding, into.buffer(), width, height, quality)
}

fn encode(encoding: Encoding, rgb: &[u8], width: u32, height: u32, quality: u8) -> Result<Vec<u8>> {
    match encoding {
        Encoding::WebP => encode_webp(rgb, width, height, quality),
        Encoding::Jpeg => encode_jpeg(rgb, width, height, quality),
    }
}

/// A JPEG, for the pages too tall for a WebP.
///
/// `jpeg-encoder` rather than the encoder in `image`: on a 1936×1400 page it encodes in
/// 6.3 ms against 18.4, in Rust with SIMD, for the same size and the same fidelity.
fn encode_jpeg(rgb: &[u8], width: u32, height: u32, quality: u8) -> Result<Vec<u8>> {
    let side = |n: u32| u16::try_from(n).map_err(|_| anyhow::anyhow!("a JPEG cannot be {n} wide"));
    let mut out = Vec::new();
    jpeg_encoder::Encoder::new(&mut out, quality)
        .encode(
            rgb,
            side(width)?,
            side(height)?,
            jpeg_encoder::ColorType::Rgb,
        )
        .map_err(|e| anyhow::anyhow!("encoding a page: {e}"))?;
    Ok(out)
}

/// Lossy WebP, at `quality` and method 4.
///
/// Measured on 177 pages of the real library, against the JPEG at 85 this replaced: at
/// quality 75 the same PSNR to a tenth of a decibel and a higher SSIM, for **0.56 of the
/// bytes** in the median (0.41 to 0.63 from p10 to p90). And the resize that came out larger
/// than its WebP source — 32 % of the pages asked at 1000 wide, 54 % at 1200, every one of
/// them decoded and thrown away — comes out larger for none of them.
///
/// It costs time, once: 84 ms to encode a page 1000 wide and 116 at 1200 on the machine it
/// was measured on, against 8 and 12 for the JPEG, paid on the first request for a page at
/// a width and never again, since the result is cached. Method 1 would divide that by two
/// and a half for 15 % more bytes; method 4 is libwebp's own default.
fn encode_webp(rgb: &[u8], width: u32, height: u32, quality: u8) -> Result<Vec<u8>> {
    let mut config =
        webp::WebPConfig::new().map_err(|()| anyhow::anyhow!("no configuration for libwebp"))?;
    config.lossless = 0;
    config.quality = f32::from(quality);
    config.method = 4;
    webp::Encoder::from_rgb(rgb, width, height)
        .encode_advanced(&config)
        .map(|memory| memory.to_vec())
        .map_err(|e| anyhow::anyhow!("encoding a page: {e:?}"))
}

/// Stamps a file as read, so the cache evicts what has not been opened for longest rather
/// than what happened to be written first.
///
/// The modification time is set deliberately rather than left to the access time: `relatime`
/// and `noatime` are common enough that atime cannot be relied on, and a cache that evicts
/// by write order throws away the page you are reading right now.
fn touch(path: &Path) -> std::io::Result<()> {
    std::fs::OpenOptions::new()
        .write(true)
        .open(path)?
        .set_modified(std::time::SystemTime::now())
}
