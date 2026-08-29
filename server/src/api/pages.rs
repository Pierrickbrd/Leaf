//! Serving a page, and the cache that means it is only ever resized once.
//!
//! Three decisions carry this file, and each of them was arrived at by measuring:
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

use crate::archive::cbz;
use crate::store::Db;

/// Above this, a "width" is a client bug rather than a request.
const MAX_WIDTH: u32 = 4096;

/// What a cover is asked for at when nobody says. Small: it is a tile on a shelf.
const COVER_WIDTH: u32 = 300;

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
        }
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
        self.serve(&source, width, &format!("{entry_id}/{number}"))
    }

    /// The cover of an entry. A thumbnail is a page asked for narrow; there is no separate
    /// thing.
    pub fn cover(&self, entry_id: &str, width: Option<u32>) -> Result<Option<ServedImage>> {
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
        if let Some(path) = chosen {
            return self.serve_file(Path::new(&path), width.unwrap_or(COVER_WIDTH));
        }
        // Not a read: a cover is a tile on a shelf, and nobody has opened the volume.
        self.page(entry_id, 0, Some(width.unwrap_or(COVER_WIDTH)))
    }

    /// The tile of a grid: one request, not one to find the first entry and another for its
    /// cover.
    pub fn series_cover(
        &self,
        edition_id: &str,
        width: Option<u32>,
    ) -> Result<Option<ServedImage>> {
        if let Some(width) = width {
            self.remember_width(width);
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
            return self.serve_file(Path::new(&path), width.unwrap_or(COVER_WIDTH));
        }
        let first: Option<String> = self.db.read(|cx| {
            cx.query_one(
                "SELECT id FROM entry WHERE edition_id = ?1 ORDER BY sort_key, file LIMIT 1",
                [edition_id],
                |r| r.get::<_, String>(0),
            )
        })?;
        match first {
            Some(entry) => self.cover(&entry, width),
            None => Ok(None),
        }
    }

    /// An image sitting on the disk rather than inside an archive.
    ///
    /// Keyed on its own path: it belongs to no entry in particular — an edition cover is one
    /// file speaking for a whole series — and the path is what identifies it.
    fn serve_file(&self, path: &Path, requested: u32) -> Result<Option<ServedImage>> {
        if !path.is_file() {
            return Ok(None);
        }
        let Ok(bytes) = std::fs::read(path) else {
            return Ok(None);
        };
        let key = path.to_string_lossy().to_string();
        let modified = std::fs::metadata(path)
            .and_then(|m| m.modified())
            .ok()
            .and_then(|t| t.duration_since(std::time::UNIX_EPOCH).ok())
            .map(|d| d.as_millis() as i64)
            .unwrap_or(0);
        let media_type = crate::archive::images::media_type(&bytes)
            .unwrap_or("application/octet-stream")
            .to_string();
        let dimensions = crate::archive::images::dimension(&bytes);

        Ok(Some(self.shrink(
            bytes,
            &media_type,
            dimensions.map(|(w, _)| w as i64),
            dimensions.map(|(_, h)| h as i64),
            requested,
            &key,
            modified,
        )))
    }

    fn serve(
        &self,
        source: &Source,
        requested: Option<u32>,
        key: &str,
    ) -> Result<Option<ServedImage>> {
        let Some(requested) = requested else {
            let Some(bytes) = cbz::extract(Path::new(&source.file), &source.entry_name)? else {
                return Ok(None);
            };
            return Ok(Some(ServedImage {
                tag: self.tag(key, source.modified_at, None),
                bytes,
                media_type: source.media_type.clone(),
            }));
        };

        // Consulted before the archive is opened. Everything the decision needs is in the
        // index already.
        let plan = self.plan(source.width, source.height, requested);
        if plan.worth_resizing {
            if let Some(hit) = self.cached(&self.tag(key, source.modified_at, Some(requested))) {
                return Ok(Some(hit));
            }
        }

        let Some(bytes) = cbz::extract(Path::new(&source.file), &source.entry_name)? else {
            return Ok(None);
        };
        Ok(Some(self.shrink(
            bytes,
            &source.media_type,
            source.width,
            source.height,
            requested,
            key,
            source.modified_at,
        )))
    }

    /// What a request for a width actually means for this page, decided from the index alone.
    fn plan(&self, source_width: Option<i64>, source_height: Option<i64>, requested: u32) -> Plan {
        let spread = matches!((source_width, source_height), (Some(w), Some(h)) if w > h);
        let target = if spread {
            requested.saturating_mul(2)
        } else {
            requested
        }
        .clamp(1, MAX_WIDTH);
        Plan {
            target,
            worth_resizing: match source_width {
                None => true,
                Some(w) => f64::from(target) < w as f64 * WORTH_RESIZING,
            },
        }
    }

    /// Decide the width, resize, and refuse to hand back something worse than the source.
    #[allow(clippy::too_many_arguments)]
    fn shrink(
        &self,
        source: Vec<u8>,
        source_media_type: &str,
        source_width: Option<i64>,
        source_height: Option<i64>,
        requested: u32,
        key: &str,
        modified_at: i64,
    ) -> ServedImage {
        let decided = self.plan(source_width, source_height, requested);
        let plain = |bytes: Vec<u8>| ServedImage {
            tag: self.tag(key, modified_at, None),
            bytes,
            media_type: source_media_type.to_string(),
        };
        if !decided.worth_resizing {
            return plain(source);
        }

        // Keyed on what the caller asked for, not on the width we end up using: doubling a
        // spread is an internal decision, and the answer is deterministic either way.
        let etag = self.tag(key, modified_at, Some(requested));
        if let Some(hit) = self.cached(&etag) {
            return hit;
        }

        let resized = match resize(&source, decided.target, self.quality) {
            Ok(bytes) => bytes,
            Err(e) => {
                // A codec we cannot read is not a reason to fail: serve the original.
                tracing::warn!(key, error = %e, "could not resize");
                return plain(source);
            }
        };

        // A WebP source can re-encode to a *larger* JPEG. Serving that would be absurd, so
        // the original goes back instead. The width still had a point: what matters on a
        // phone is the decoded bitmap — 1695×2101 costs 14 MB of memory, 1080 wide costs 6.
        if resized.len() >= source.len() {
            return plain(source);
        }

        self.store(&etag, &resized);
        ServedImage {
            bytes: resized,
            media_type: "image/jpeg".to_string(),
            tag: etag,
        }
    }

    /// A resized page already on disk, or nothing.
    ///
    /// Reading touches the file, so what goes first when the cache is trimmed is what has
    /// not been opened for longest — not what happened to be written first.
    fn cached(&self, etag: &str) -> Option<ServedImage> {
        let path = self.cache_path(etag);
        let bytes = std::fs::read(&path).ok()?;
        let _ = touch(&path);
        Some(ServedImage {
            bytes,
            media_type: "image/jpeg".to_string(),
            tag: etag.to_string(),
        })
    }

    fn store(&self, etag: &str, bytes: &[u8]) {
        let path = self.cache_path(etag);
        let written = path
            .parent()
            .map(std::fs::create_dir_all)
            .transpose()
            .and_then(|_| crate::store::files::write_whole(&path, bytes));
        if let Err(e) = written {
            tracing::warn!(cache = %path.display(), error = %e, "cache not written");
            return;
        }
        let so_far = self
            .written_since_check
            .fetch_add(bytes.len() as u64, Ordering::Relaxed)
            + bytes.len() as u64;
        if so_far > self.check_every {
            self.written_since_check.store(0, Ordering::Relaxed);
            super::cache_budget::enforce(&self.cache_root, self.max_cache_bytes);
        }
    }

    /// The tag carries the file's modification time, so a retouched volume invalidates its
    /// cached pages on its own — no sweep to run, no stale image to explain.
    fn tag(&self, key: &str, modified_at: i64, width: Option<u32>) -> String {
        let width = width
            .map(|w| w.to_string())
            .unwrap_or_else(|| "src".to_string());
        let digest = Sha256::digest(format!("{key}/{modified_at}/{width}").as_bytes());
        digest.iter().map(|b| format!("{b:02x}")).collect()
    }

    /// Two levels of fan-out: a single folder of a hundred thousand files ages badly.
    fn cache_path(&self, tag: &str) -> PathBuf {
        self.cache_root.join(&tag[..2]).join(format!("{tag}.jpg"))
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
                    let prepared = match task.number {
                        Some(number) => pages.page(&task.id, number, Some(task.width)),
                        None => pages.series_cover(&task.id, Some(task.width)),
                    };
                    if let Err(e) = prepared {
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
        let Some(sender) = self.warm.get() else {
            return;
        };
        for step in 1..=AHEAD {
            let next = number + step;
            let key = format!("{entry_id}/{next}@{width}");
            if !self.claim(&key) {
                continue;
            }
            let task = Warm {
                key: key.clone(),
                id: entry_id.to_string(),
                number: Some(next),
                width,
            };
            // A full queue means we are behind the reader. What is dropped is the newest
            // request — the page furthest ahead — because the oldest is the one about to be
            // turned to.
            //
            // And a dropped task has to give its claim back. In Kotlin that needed a custom
            // rejection handler, because the stock policies drop in silence and the page
            // stayed marked in flight for ever: one stutter, at the same page, every time
            // it was read. Here the value comes back in the error, so releasing it is the
            // only thing there is to do with it.
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
    /// From the second page, not the first: the client asks for page 0 the instant it has
    /// the list, so preparing it here only means doing it twice at once.
    pub fn warm_opening(&self, entry_id: &str, width: Option<u32>) {
        if let Some(width) = width {
            self.warm_ahead(entry_id, 0, width);
        }
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
    /// An entry for a page, an edition for a shelf tile.
    id: String,
    /// `None` for a series cover: there is no page number to speak of.
    number: Option<i64>,
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
    worth_resizing: bool,
}

/// Resize on width, aspect ratio preserved, never upscaled. The output is JPEG: it is the
/// only format every reader and every browser writes and reads without a plugin, and a
/// downscaled page has already lost the detail a lossless format would protect.
///
/// The scaling itself is SIMD and fast — 1.9 ms on a real page, against 31 ms for the
/// scalar implementation in `image` and 9.6 ms for Java's Graphics2D.
///
/// **And the whole path is still four times slower than the Kotlin's**: 116 ms against
/// 31 ms per page, measured through both servers on the same archive. The reason is not
/// the resize, it is the decode. Thumbnailator asks libjpeg to decode at a *reduced* scale
/// — the DCT can produce a half- or quarter-size image for a fraction of the work — so the
/// JVM never materialises the full bitmap it is about to shrink. Neither `image` nor
/// `zune-jpeg` exposes that, so this decodes 1936×1400 in full and then throws most of it
/// away.
///
/// Recorded rather than papered over. It only bites on a cache miss, and the warm path is
/// the one that runs: 9 ms against 12. Closing it would mean `mozjpeg` or `turbojpeg`,
/// which is a C dependency, which is the property this build chose not to have.
/// The pixels, with any transparency laid over white.
///
/// `to_rgb8` **drops** the alpha channel rather than compositing it, and a PNG saved with a
/// transparent background holds black under the clear part — so a page came back with ink
/// where a reader expects paper. There is nowhere for transparency to go in a JPEG, and the
/// question is only what it becomes: white, because these are pages.
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

fn resize(source: &[u8], width: u32, quality: u8) -> Result<Vec<u8>> {
    let decoded = image::ImageReader::new(Cursor::new(source))
        .with_guessed_format()?
        .decode()?;
    let (w0, h0) = (decoded.width(), decoded.height());
    let decoded = flattened(decoded);
    if width >= w0 {
        // Never upscaled: asking for 4000 on a 1500-wide scan gets the original back rather
        // than a blurred enlargement.
        anyhow::bail!("not worth resizing");
    }
    let height = ((h0 as f64) * (f64::from(width) / f64::from(w0))).round() as u32;

    let from = FirImage::from_vec_u8(w0, h0, decoded.into_raw(), PixelType::U8x3)?;
    let mut into = FirImage::new(width, height, PixelType::U8x3);
    Resizer::new().resize(
        &from,
        &mut into,
        &ResizeOptions::new().resize_alg(ResizeAlg::Convolution(
            fast_image_resize::FilterType::Lanczos3,
        )),
    )?;

    // `jpeg-encoder` rather than the one in `image`, because encoding is where this path
    // spends its time. Broken down on a 1936×1400 page: 0.1 ms to pull it out of the
    // archive, 7.9 to decode it, 4.8 to shrink it, and **18.4 to encode it again** — nearly
    // sixty per cent of the work, in the step nobody looks at.
    //
    // This one does it in 6.3, in Rust with SIMD, for 260 KB against 267 and an average
    // error against the source pixels of 2.31 against 2.33. Faster, slightly smaller, no
    // less faithful, and still nothing to install in the container.
    let mut out = Vec::new();
    jpeg_encoder::Encoder::new(&mut out, quality)
        .encode(
            into.buffer(),
            width as u16,
            height as u16,
            jpeg_encoder::ColorType::Rgb,
        )
        .map_err(|e| anyhow::anyhow!("encoding a page: {e}"))?;
    Ok(out)
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
