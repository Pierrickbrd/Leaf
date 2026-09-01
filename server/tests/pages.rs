//! Serving a page, and the cache that means it is only ever resized once.

use std::io::Write;
use std::path::Path;
use std::sync::Arc;

mod common;
use common::{read_only, writable};

use leaf_server::api::pages::Pages;
use leaf_server::store::Db;

/// A CBZ with pages of a known size, so "was it resized" has an answer.
fn archive(path: &Path, pages: &[(&str, u32, u32)]) {
    let file = std::fs::File::create(path).expect("creating the archive");
    let mut zip = zip::ZipWriter::new(file);
    for (name, width, height) in pages {
        zip.start_file::<_, ()>(*name, zip::write::SimpleFileOptions::default())
            .expect("an entry");
        zip.write_all(&jpeg(*width, *height)).expect("writing");
    }
    zip.finish().expect("closing");
}

fn jpeg(width: u32, height: u32) -> Vec<u8> {
    // Noise rather than flat colour: a page of one colour compresses to nothing, and then
    // "the resize came back bigger" is true for the wrong reason.
    let mut buffer = image::RgbImage::new(width, height);
    for (x, y, pixel) in buffer.enumerate_pixels_mut() {
        *pixel = image::Rgb([
            (x * 7 % 256) as u8,
            (y * 13 % 256) as u8,
            ((x ^ y) % 256) as u8,
        ]);
    }
    let mut out = std::io::Cursor::new(Vec::new());
    image::DynamicImage::ImageRgb8(buffer)
        .write_to(&mut out, image::ImageFormat::Jpeg)
        .expect("encoding");
    out.into_inner()
}

struct Fixture {
    dir: tempfile::TempDir,
    db: Arc<Db>,
}

impl Fixture {
    fn new() -> Self {
        let dir = tempfile::tempdir().expect("a directory");
        let cbz = dir.path().join("Tome 1.cbz");
        archive(
            &cbz,
            &[
                ("000.jpg", 1200, 1700),
                ("001.jpg", 2400, 1700),
                ("002.jpg", 300, 400),
            ],
        );

        let db = Db::open(&dir.path().join("index.sqlite")).expect("opening");
        db.write(|cx| {
            cx.execute("INSERT INTO work (id, name, path) VALUES ('w', 'Essai', '/w')", [])?;
            cx.execute(
                "INSERT INTO edition (id, work_id, path, implicit) VALUES ('e', 'w', '/w/e', 1)",
                [],
            )?;
            cx.execute(
                "INSERT INTO entry (id, edition_id, type, file, size, modified_at, added_at,
                                    volume_number, sort_key, page_count)
                 VALUES ('v1', 'e', 'VOLUME', ?1, 1, 1700000000000, 1, 1.0, 1.0, 3)",
                [cbz.to_string_lossy().to_string()],
            )?;
            for (number, name, w, h) in [
                (0, "000.jpg", 1200, 1700),
                (1, "001.jpg", 2400, 1700),
                (2, "002.jpg", 300, 400),
            ] {
                cx.execute(
                    "INSERT INTO page (entry_id, number, entry_name, media_type, width, height, size)
                     VALUES ('v1', ?1, ?2, 'image/jpeg', ?3, ?4, 1000)",
                    (number, name, w, h),
                )?;
            }
            Ok(())
        })
        .expect("seeding");

        Fixture {
            db: Arc::new(db),
            dir,
        }
    }

    fn pages(&self) -> Pages {
        self.pages_with_budget(64 * 1024 * 1024)
    }

    fn pages_with_budget(&self, budget: u64) -> Pages {
        let pages = Pages::new(
            Arc::clone(&self.db),
            self.dir.path().join("cache"),
            85,
            budget,
        );
        pages.prepare();
        pages
    }

    fn cache_files(&self) -> usize {
        fn count(dir: &Path) -> usize {
            std::fs::read_dir(dir)
                .map(|entries| {
                    entries
                        .flatten()
                        .map(|e| {
                            if e.path().is_dir() {
                                count(&e.path())
                            } else {
                                1
                            }
                        })
                        .sum()
                })
                .unwrap_or(0)
        }
        count(&self.dir.path().join("cache"))
    }
}

#[test]
fn without_a_width_the_original_bytes_go_back_untouched() {
    let f = Fixture::new();
    let served = f
        .pages()
        .page("v1", 0, None)
        .expect("serving")
        .expect("a page");

    assert_eq!("image/jpeg", served.media_type);
    // Byte for byte what is inside the archive.
    let inside = leaf_server::archive::cbz::extract(&f.dir.path().join("Tome 1.cbz"), "000.jpg")
        .expect("extracting")
        .expect("the entry");
    assert_eq!(inside, served.bytes);
    assert_eq!(
        0,
        f.cache_files(),
        "serving the source must not write a cache entry"
    );
}

#[test]
fn a_width_gives_back_something_smaller_and_keeps_it() {
    let f = Fixture::new();
    let pages = f.pages();

    let source = pages.page("v1", 0, None).unwrap().unwrap();
    let resized = pages.page("v1", 0, Some(400)).unwrap().unwrap();

    assert!(resized.bytes.len() < source.bytes.len());
    assert_eq!("image/jpeg", resized.media_type);
    assert_eq!(1, f.cache_files(), "the resize is kept");

    // And the second time it comes off the disk, tag and bytes unchanged.
    let again = pages.page("v1", 0, Some(400)).unwrap().unwrap();
    assert_eq!(resized.tag, again.tag);
    assert_eq!(resized.bytes, again.bytes);
    assert_eq!(1, f.cache_files(), "and not written twice");
}

#[test]
fn a_page_is_never_upscaled() {
    let f = Fixture::new();
    let pages = f.pages();
    let source = pages.page("v1", 2, None).unwrap().unwrap(); // 300 wide

    // Asking for 4000 on a 300-wide page returns the original rather than a blurred
    // enlargement — and writes nothing, because there is nothing to keep.
    let asked = pages.page("v1", 2, Some(4000)).unwrap().unwrap();
    assert_eq!(source.bytes, asked.bytes);
    assert_eq!(0, f.cache_files());
}

#[test]
fn a_resize_that_would_barely_help_is_not_done() {
    let f = Fixture::new();
    let pages = f.pages();
    let source = pages.page("v1", 0, None).unwrap().unwrap(); // 1200 wide

    // 1000 out of 1200 is above the ratio worth paying for: decoding, scaling and
    // re-encoding would cost several hundred milliseconds and give back a file the same
    // size.
    let asked = pages.page("v1", 0, Some(1000)).unwrap().unwrap();
    assert_eq!(source.bytes, asked.bytes, "1000/1200 is not worth the work");

    // 600 out of 1200 is.
    let smaller = pages.page("v1", 0, Some(600)).unwrap().unwrap();
    assert!(smaller.bytes.len() < source.bytes.len());
}

#[test]
fn a_spread_is_given_twice_the_width_it_asked_for() {
    let f = Fixture::new();
    let pages = f.pages();

    // Page 1 is 2400×1700: wider than tall, so it holds two pages side by side. ?width is
    // the width you want *per page* — each half would otherwise come back at half the
    // resolution of a single page, and unreadable the moment you zoom.
    let spread = pages.page("v1", 1, Some(600)).unwrap().unwrap();
    let single = pages.page("v1", 0, Some(600)).unwrap().unwrap();

    let width_of = |bytes: &[u8]| leaf_server::archive::images::dimension(bytes).unwrap().0;
    assert_eq!(
        1200,
        width_of(&spread.bytes),
        "a spread gets 600 per page, so 1200"
    );
    assert_eq!(600, width_of(&single.bytes));
}

#[test]
fn the_tag_changes_when_the_file_does() {
    let f = Fixture::new();
    let pages = f.pages();
    let before = pages.page("v1", 0, Some(400)).unwrap().unwrap().tag;

    // The tag carries the file's modification time, so a retouched volume invalidates its
    // cached pages on its own — no sweep to run, no stale image to explain.
    f.db.write(|cx| {
        cx.execute(
            "UPDATE entry SET modified_at = 1800000000000 WHERE id = 'v1'",
            [],
        )?;
        Ok(())
    })
    .unwrap();

    let after = pages.page("v1", 0, Some(400)).unwrap().unwrap().tag;
    assert_ne!(before, after);
}

#[test]
fn the_cache_is_trimmed_back_under_its_budget() {
    let f = Fixture::new();
    // A budget so small that the second page cannot join the first. The sweep runs after
    // enough has been written, and the floor on that threshold used to be 4 MB — which
    // silently disabled it under any small budget.
    let pages = f.pages_with_budget(80 * 1024);

    for width in [200, 300, 400, 500, 600, 700] {
        pages.page("v1", 0, Some(width)).unwrap();
    }

    let total: u64 = walk(&f.dir.path().join("cache"));
    assert!(
        total <= 80 * 1024,
        "the cache holds {total} bytes against a budget of {}",
        80 * 1024
    );
}

fn walk(dir: &Path) -> u64 {
    std::fs::read_dir(dir)
        .map(|entries| {
            entries
                .flatten()
                .map(|e| {
                    if e.path().is_dir() {
                        walk(&e.path())
                    } else {
                        e.metadata().map(|m| m.len()).unwrap_or(0)
                    }
                })
                .sum()
        })
        .unwrap_or(0)
}

/// A page taller than a JPEG can be.
///
/// 65 535 rows is the format's own ceiling and a PNG has no such thing, so a stitched
/// webtoon strip goes past it as a matter of course. The encoder is told its dimensions as
/// `u16`: cast rather than checked, it was handed the whole bitmap and told it was
/// 13 107 rows tall, and what went into the cache — and out to the reader for a year behind
/// `max-age=31536000` — was the top sixth of the page.
#[test]
fn a_page_too_tall_for_a_jpeg_comes_back_whole_rather_than_cut() {
    let dir = tempfile::tempdir().expect("a directory");
    let cbz = dir.path().join("Tome 1.cbz");

    // 20 × 131 072: shrunk to twelve wide it is 78 643 rows, which is past what a JPEG can
    // say. Narrow on purpose — it is the height that has to be big, and this is 7.8 MB
    // rather than the gigabyte a realistic width would cost.
    let mut strip = image::RgbImage::new(20, 131_072);
    for (x, y, pixel) in strip.enumerate_pixels_mut() {
        *pixel = image::Rgb([(x * 7 % 256) as u8, (y % 256) as u8, ((x ^ y) % 256) as u8]);
    }
    let mut png = std::io::Cursor::new(Vec::new());
    image::DynamicImage::ImageRgb8(strip)
        .write_to(&mut png, image::ImageFormat::Png)
        .expect("encoding");
    let png = png.into_inner();

    let file = std::fs::File::create(&cbz).expect("creating the archive");
    let mut zip = zip::ZipWriter::new(file);
    zip.start_file::<_, ()>("000.png", zip::write::SimpleFileOptions::default())
        .expect("an entry");
    zip.write_all(&png).expect("writing");
    zip.finish().expect("closing");

    let db = Db::open(&dir.path().join("index.sqlite")).expect("opening");
    db.write(|cx| {
        cx.execute(
            "INSERT INTO work (id, name, path) VALUES ('w', 'Essai', '/w')",
            [],
        )?;
        cx.execute(
            "INSERT INTO edition (id, work_id, path, implicit) VALUES ('e', 'w', '/w/e', 1)",
            [],
        )?;
        cx.execute(
            "INSERT INTO entry (id, edition_id, type, file, size, modified_at, added_at,
                                volume_number, sort_key, page_count)
             VALUES ('v1', 'e', 'VOLUME', ?1, 1, 1700000000000, 1, 1.0, 1.0, 1)",
            [cbz.to_string_lossy().to_string()],
        )?;
        cx.execute(
            "INSERT INTO page (entry_id, number, entry_name, media_type, width, height, size)
             VALUES ('v1', 0, '000.png', 'image/png', 20, 131072, 1000)",
            [],
        )?;
        Ok(())
    })
    .expect("seeding");

    let pages = Pages::new(Arc::new(db), dir.path().join("cache"), 85, 64 * 1024 * 1024);
    pages.prepare();
    let served = pages
        .page("v1", 0, Some(12))
        .expect("serving")
        .expect("a page");

    // The original, whole, rather than a JPEG holding the top of it.
    assert_eq!("image/png", served.media_type);
    assert_eq!(png, served.bytes);

    // And it cost nothing to know: the index held 131 072 rows before the archive was
    // touched. Asked three times over, because the shape of the waste is that this path
    // stores nothing — so every reader used to pay the whole 7.8 MB decode again.
    for _ in 0..3 {
        assert_eq!(png, pages.page("v1", 0, Some(12)).unwrap().unwrap().bytes);
    }
    assert_eq!(
        0,
        pages.decodes(),
        "a strip no JPEG can hold is never decoded"
    );
}

/// A width of nought is not a width, and the clamp made it into one.
///
/// `?width=0` doubles to nought on a spread and is then clamped up to one, which reads as
/// "shrink this page to a single column" — and a JPEG one pixel wide is smaller than the
/// source, which was the only test standing between it and the cache. It went in under the
/// ETag for the width that was asked for and came back out behind `max-age=31536000` for a
/// year. The ceiling had a guard; the floor was a `clamp` inventing a value nobody sent.
#[test]
fn a_width_of_nothing_gives_the_page_back_rather_than_one_pixel_of_it() {
    let f = Fixture::new();
    let pages = f.pages();

    // Page 1 is the spread, which is where the doubling happens.
    let whole = pages.page("v1", 1, None).expect("serving").expect("a page");
    let served = pages
        .page("v1", 1, Some(0))
        .expect("serving")
        .expect("a page");

    assert_eq!(
        whole.bytes, served.bytes,
        "the page itself, not one pixel of it"
    );
    assert_eq!(
        0,
        f.cache_files(),
        "and nothing nobody asked for was kept for a year"
    );

    // A width that means something is still honoured, so this is not a way of refusing to
    // work.
    let smaller = pages.page("v1", 1, Some(600)).unwrap().unwrap();
    assert!(smaller.bytes.len() < whole.bytes.len());
}

/// The same nought, at the door a shelf actually knocks on.
///
/// `plan` refuses to shrink a page to no columns, which is right and is not enough: the cover
/// routes ask for `width.unwrap_or(COVER_WIDTH)`, and `unwrap_or` cannot tell `?width=0` from
/// no width at all. So the nought survived the floor, `plan` declined to resize it, and the
/// tile came back as the whole scan — a grid of five hundred of them fetching five hundred
/// full-resolution pages, none of it cached, every time the shelf was looked at.
#[test]
fn a_cover_asked_for_at_no_width_is_a_tile_and_not_the_whole_page() {
    let f = Fixture::new();
    let pages = f.pages();

    let tile = pages.cover("v1", None).unwrap().expect("a cover");
    let nought = pages.cover("v1", Some(0)).unwrap().expect("a cover");
    let whole = pages.page("v1", 0, None).unwrap().expect("a page");

    // The same answer, down to the tag: a cover asked for at no width is a cover.
    assert_eq!(tile.tag, nought.tag);
    assert_eq!(tile.bytes.len(), nought.bytes.len());
    assert!(
        nought.bytes.len() < whole.bytes.len() / 2,
        "a tile is {} bytes against a page of {}",
        nought.bytes.len(),
        whole.bytes.len()
    );
}

/// And a nought is not a width worth remembering for the sweep.
///
/// `seen_widths` holds two, so a nought counted as a width evicts a real one — and
/// `warm_covers` then walks every edition in the library, reads every cover in full, decides
/// each one is not worth resizing, stores nothing and reports itself done. A full pass over
/// the library to leave the shelf exactly as cold as it was.
#[test]
fn a_nought_does_not_take_a_real_width_s_place_in_the_sweep() {
    let f = Fixture::new();
    let pages = Arc::new(f.pages());

    pages.series_cover("e", Some(300)).unwrap().expect("a tile");
    pages.series_cover("e", Some(600)).unwrap().expect("a tile");
    pages.series_cover("e", Some(0)).unwrap().expect("a tile");
    pages.series_cover("e", Some(0)).unwrap().expect("a tile");

    // Emptied, so that what appears below can only have come from the sweep.
    std::fs::remove_dir_all(f.dir.path().join("cache")).unwrap();
    pages.prepare();
    assert_eq!(0, f.cache_files());

    pages.warm_covers();

    let deadline = std::time::Instant::now() + std::time::Duration::from_secs(30);
    while f.cache_files() < 2 {
        assert!(
            std::time::Instant::now() < deadline,
            "the sweep prepared {} of the two widths a client asked for",
            f.cache_files()
        );
        std::thread::sleep(std::time::Duration::from_millis(20));
    }
}

/// And nothing is read ahead at a width nothing is stored under.
///
/// Reading a volume with `?width=0` queued the four pages after each one, and each of those
/// opened the archive, read the member and stored nothing — four extractions per page turn,
/// for ever, because that path has nothing to cache.
#[test]
fn no_pages_are_prepared_ahead_at_a_width_of_nothing() {
    let f = Fixture::new();
    let pages = Arc::new(f.pages());
    pages.start_warming(2, 8);

    pages.warm_ahead("v1", 0, 0);
    // Read at once, and not after a sleep: a claim is taken in this thread before the task is
    // sent, and given back once a worker is done with it — so waiting is waiting for the
    // evidence to be tidied away.
    assert_eq!(
        0,
        pages.pending(),
        "four pages were queued to be prepared at nothing"
    );

    std::thread::sleep(std::time::Duration::from_millis(200));
    assert_eq!(0, f.cache_files(), "and nothing came of them, as ever");
}

/// The other end of the same arithmetic, which costs a decode rather than a wrong picture.
///
/// A page far wider than tall shrinks to no rows at all: 120 rows of a 4000-wide panorama,
/// asked for two columns, round to none. The scaler refuses that, so what came back was
/// always the page — but only after the member had been decoded in full on the blocking
/// pool, and `shrink` stores nothing on that path, so every reader paid the decode again on
/// every request. Both numbers are in the index, and `plan` now answers from them.
///
/// Nothing observable moves between the two, which is what makes this the kind of defect
/// that survives a suite. So the count is the assertion, the way `Cx` counts queries.
#[test]
fn a_width_that_leaves_no_rows_at_all_is_decided_before_the_decode() {
    let dir = tempfile::tempdir().expect("a directory");
    let cbz = dir.path().join("Tome 1.cbz");
    archive(&cbz, &[("000.jpg", 4000, 120)]);

    let db = Db::open(&dir.path().join("index.sqlite")).expect("opening");
    db.write(|cx| {
        cx.execute(
            "INSERT INTO work (id, name, path) VALUES ('w', 'Essai', '/w')",
            [],
        )?;
        cx.execute(
            "INSERT INTO edition (id, work_id, path, implicit) VALUES ('e', 'w', '/w/e', 1)",
            [],
        )?;
        cx.execute(
            "INSERT INTO entry (id, edition_id, type, file, size, modified_at, added_at,
                                volume_number, sort_key, page_count)
             VALUES ('v1', 'e', 'VOLUME', ?1, 1, 1700000000000, 1, 1.0, 1.0, 1)",
            [cbz.to_string_lossy().to_string()],
        )?;
        cx.execute(
            "INSERT INTO page (entry_id, number, entry_name, media_type, width, height, size)
             VALUES ('v1', 0, '000.jpg', 'image/jpeg', 4000, 120, 1000)",
            [],
        )?;
        Ok(())
    })
    .expect("seeding");

    let cache = dir.path().join("cache");
    let pages = Pages::new(Arc::new(db), cache.clone(), 85, 64 * 1024 * 1024);
    pages.prepare();
    let whole = pages.page("v1", 0, None).unwrap().unwrap();

    let before = pages.decodes();
    for _ in 0..3 {
        let served = pages.page("v1", 0, Some(1)).unwrap().unwrap();
        assert_eq!(whole.bytes, served.bytes);
    }
    assert_eq!(
        before,
        pages.decodes(),
        "three readers, three full decodes of a page that was never going to shrink"
    );
    assert!(!cache.join("v1").exists(), "and nothing was kept");

    // A width this page has the rows for still resizes, so the decision is about this page
    // rather than about giving up on narrow ones — and it costs the one decode it should.
    let smaller = pages.page("v1", 0, Some(1000)).unwrap().unwrap();
    assert!(smaller.bytes.len() < whole.bytes.len());
    assert_eq!(before + 1, pages.decodes());
}

#[test]
fn an_unknown_page_or_entry_is_nothing_rather_than_an_error() {
    let f = Fixture::new();
    let pages = f.pages();
    assert!(pages.page("v1", 99, Some(400)).unwrap().is_none());
    assert!(pages.page("nexistepas", 0, Some(400)).unwrap().is_none());
}

#[test]
fn a_cover_is_a_page_asked_for_narrow() {
    let f = Fixture::new();
    let pages = f.pages();

    let cover = pages.cover("v1", None).unwrap().unwrap();
    let explicit = pages.page("v1", 0, Some(300)).unwrap().unwrap();
    // There is no separate thing: the default cover is page zero at 300.
    assert_eq!(explicit.tag, cover.tag);
    assert_eq!(explicit.bytes, cover.bytes);
}

#[test]
fn a_series_cover_is_the_first_entrys() {
    let f = Fixture::new();
    let pages = f.pages();
    let series = pages.series_cover("e", None).unwrap().unwrap();
    let entry = pages.cover("v1", None).unwrap().unwrap();
    assert_eq!(entry.tag, series.tag);
}

#[test]
fn a_cover_chosen_on_disk_wins_over_page_zero() {
    let f = Fixture::new();
    let chosen = f.dir.path().join("cover.jpg");
    std::fs::write(&chosen, jpeg(800, 1200)).unwrap();
    f.db.write(|cx| {
        cx.execute(
            "UPDATE edition SET cover_file = ?1 WHERE id = 'e'",
            [chosen.to_string_lossy().to_string()],
        )?;
        Ok(())
    })
    .unwrap();

    let pages = f.pages();
    let series = pages.series_cover("e", None).unwrap().unwrap();
    let first = pages.cover("v1", None).unwrap().unwrap();
    // "cover.jpg beside the volumes" is what this is for: one file speaking for the whole
    // series, rather than whatever the first volume happens to open on.
    assert_ne!(first.tag, series.tag);
}

// ----------------------------------------------------------------- prefetch

#[test]
fn a_dropped_prefetch_gives_its_claim_back() {
    let f = Fixture::new();
    let pages = Arc::new(f.pages());
    // One worker, a queue of one: most of what is asked for cannot be taken, which is the
    // only way to reach the dropping path on purpose.
    pages.start_warming(1, 1);

    for round in 0..40 {
        pages.warm_ahead("v1", round, 400);
    }

    // A claim that is never given back means that page is never prepared again — one
    // stutter, at the same page, every time it is read. A queue that drops a task in
    // silence never runs the code that would have released it; here the dropped value comes
    // back in the error, so releasing it is the only thing there is to do with it.
    // A deadline rather than a count of naps, and a generous one: what is being waited on
    // is the one warm that was accepted actually preparing a page — decode, resize, encode
    // — which is real work on a machine that may be busy with other things. This asserts
    // liveness, not speed: a claim that leaks never comes back at all, so the only cost of
    // waiting longer than necessary is that the broken case takes longer to prove.
    let deadline = std::time::Instant::now() + std::time::Duration::from_secs(30);
    while pages.pending() > 0 && std::time::Instant::now() < deadline {
        std::thread::sleep(std::time::Duration::from_millis(50));
    }
    assert_eq!(0, pages.pending(), "a dropped prefetch kept its claim");
}

#[test]
fn preparing_a_page_does_not_ask_for_four_more() {
    let f = Fixture::new();
    let pages = Arc::new(f.pages());
    pages.start_warming(2, 8);

    pages.warm_ahead("v1", 0, 400);
    let mut waited = 0;
    while pages.pending() > 0 && waited < 100 {
        std::thread::sleep(std::time::Duration::from_millis(50));
        waited += 1;
    }

    // The preparation must not feed itself. It did once: every page prepared asked for the
    // four after it, which asked for four more, and fetching a grid of covers quietly
    // resized the opening pages of every volume in the library — six covers left nine
    // hundred files in the cache.
    //
    // Here it cannot: warming calls `page`, and `page` warms nothing. Only a route does,
    // and only when a width was asked for.
    assert!(
        f.cache_files() <= 3,
        "{} files in the cache from warming three pages",
        f.cache_files()
    );
}

#[test]
fn a_cover_does_not_start_a_volume_warming() {
    let f = Fixture::new();
    let pages = Arc::new(f.pages());
    pages.start_warming(2, 8);

    pages.cover("v1", None).unwrap();
    std::thread::sleep(std::time::Duration::from_millis(200));

    assert_eq!(
        0,
        pages.pending(),
        "a cover is a tile, not a volume being opened"
    );
    assert_eq!(1, f.cache_files(), "only the cover itself");
}

/// An archive is not a trusted format: these files are downloaded.
#[test]
fn an_entry_that_claims_to_be_enormous_is_refused_rather_than_allocated() {
    use std::io::Write;

    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("bomb.cbz");
    {
        // A gigabyte of zeroes compresses to almost nothing, which is the whole trick.
        let mut zip = zip::ZipWriter::new(std::fs::File::create(&path).unwrap());
        zip.start_file::<_, ()>("000.jpg", zip::write::SimpleFileOptions::default())
            .unwrap();
        let block = vec![0u8; 1 << 20];
        for _ in 0..600 {
            zip.write_all(&block).unwrap();
        }
        zip.finish().unwrap();
    }
    assert!(
        std::fs::metadata(&path).unwrap().len() < 2 * 1024 * 1024,
        "the point is that it is small on disk"
    );

    // Said, not survived by luck: it stops at the ceiling and reports what it saw.
    let refused = leaf_server::archive::cbz::extract(&path, "000.jpg");
    let message = format!("{:#}", refused.expect_err("a bomb is an error"));
    assert!(message.contains("past the"), "{message}");
}

/// Two callers wanting the same uncached page must not hand a third a half-written one.
#[test]
fn a_page_being_cached_is_never_read_half_written() {
    use std::sync::atomic::{AtomicBool, Ordering};
    use std::sync::Arc;

    let dir = tempfile::tempdir().unwrap();
    let cache = dir.path().join("cache");
    std::fs::create_dir_all(&cache).unwrap();

    // Stand where `store` stands: write the same file over and over, and read it from
    // beside. A reader must see the whole thing or nothing — never a prefix.
    let path = cache.join("page.jpg");
    let bytes = vec![7u8; 512 * 1024];
    std::fs::write(&path, &bytes).unwrap();

    let stop = Arc::new(AtomicBool::new(false));
    let writing = {
        let (path, bytes, stop) = (path.clone(), bytes.clone(), Arc::clone(&stop));
        std::thread::spawn(move || {
            while !stop.load(Ordering::Relaxed) {
                leaf_server::store::files::write_whole(&path, &bytes).unwrap();
            }
        })
    };

    let mut torn = 0;
    for _ in 0..2000 {
        match std::fs::read(&path) {
            Ok(read) if read.len() != bytes.len() => torn += 1,
            _ => {}
        }
    }
    stop.store(true, Ordering::Relaxed);
    writing.join().unwrap();

    // With `fs::write` in that thread, the same probe saw 1959 of 2000 reads land on a
    // partial file. This is not a narrow window.
    assert_eq!(
        0, torn,
        "{torn} reads saw a partial file — and a partial image is served with an ETag and \
         a year of cache-control behind it"
    );
}

#[test]
fn the_shelf_covers_are_prepared_behind_the_reader() {
    // Sequential and slow on purpose: the library is already browsable, the tiles fill in
    // behind you, and nothing a reader asks for is ever queued behind a shelf.
    let f = Fixture::new();
    let pages = Arc::new(f.pages());

    // Never at a guessed width: a width nobody requests is a cache entry nobody reads. With
    // none seen yet there is nothing to prepare, and the sweep is a no-op.
    pages.warm_covers();
    assert_eq!(f.cache_files(), 0);

    // One request teaches it a width. The cache is then emptied, so what appears next can
    // only have come from the sweep.
    pages
        .series_cover("e", Some(300))
        .unwrap()
        .expect("a cover");
    std::fs::remove_dir_all(f.dir.path().join("cache")).unwrap();
    pages.prepare();
    assert_eq!(f.cache_files(), 0);
    pages.warm_covers();

    // A deadline rather than a count of naps: what is waited on is a real decode, resize
    // and encode, on a machine that may be busy with other things.
    let deadline = std::time::Instant::now() + std::time::Duration::from_secs(30);
    while f.cache_files() == 0 {
        assert!(
            std::time::Instant::now() < deadline,
            "the shelf covers were never prepared"
        );
        std::thread::sleep(std::time::Duration::from_millis(20));
    }
    assert!(pages.series_cover("e", Some(300)).unwrap().is_some());
}

#[test]
fn a_page_whose_bytes_no_codec_reads_comes_back_as_it_is() {
    // A codec we cannot read is not a reason to fail: the original is always right, it is
    // only bigger than it needed to be.
    let dir = tempfile::tempdir().expect("a directory");
    let cbz = dir.path().join("Tome 1.cbz");
    let file = std::fs::File::create(&cbz).unwrap();
    let mut zip = zip::ZipWriter::new(file);
    zip.start_file::<_, ()>("000.jpg", zip::write::SimpleFileOptions::default())
        .unwrap();
    zip.write_all(b"\xff\xd8\xff not a jpeg past the first three bytes")
        .unwrap();
    zip.finish().unwrap();

    let db = Db::open(&dir.path().join("index.sqlite")).unwrap();
    db.write(|cx| {
        cx.execute(
            "INSERT INTO work (id, name, path) VALUES ('w','Essai','/w')",
            [],
        )?;
        cx.execute(
            "INSERT INTO edition (id, work_id, path, implicit) VALUES ('e','w','/w/e',1)",
            [],
        )?;
        cx.execute(
            "INSERT INTO entry (id, edition_id, type, file, size, modified_at, added_at,
                                volume_number, sort_key, page_count)
             VALUES ('v1','e','VOLUME',?1,1,1700000000000,1,1.0,1.0,1)",
            [cbz.to_string_lossy().to_string()],
        )?;
        cx.execute(
            "INSERT INTO page (entry_id, number, entry_name, media_type, width, height, size)
             VALUES ('v1', 0, '000.jpg', 'image/jpeg', 1200, 1700, 1000)",
            [],
        )?;
        Ok(())
    })
    .unwrap();

    let pages = Pages::new(Arc::new(db), dir.path().join("cache"), 85, 64 * 1024 * 1024);
    pages.prepare();
    let served = pages.page("v1", 0, Some(200)).unwrap().expect("a page");
    assert!(
        served.bytes.starts_with(b"\xff\xd8\xff"),
        "the original bytes, untouched"
    );
}

#[test]
fn a_cover_chosen_on_disk_is_served_at_the_width_that_was_asked_for() {
    // The file beside the archive is a file, not a page: it goes through its own path, and
    // that path has to honour the width the shelf asked for like any other.
    let f = Fixture::new();
    let beside = f.dir.path().join("cover.jpg");
    std::fs::write(&beside, jpeg(1200, 1700)).unwrap();
    f.db.write(|cx| {
        cx.execute(
            "UPDATE entry SET cover_file = ?1 WHERE id = 'v1'",
            [beside.to_string_lossy().to_string()],
        )?;
        Ok(())
    })
    .unwrap();

    let pages = f.pages();
    let wide = pages.cover("v1", Some(600)).unwrap().expect("a cover");
    let narrow = pages.cover("v1", Some(200)).unwrap().expect("a cover");
    assert!(narrow.bytes.len() < wide.bytes.len());
}

#[test]
fn a_cover_whose_file_has_gone_is_nothing_rather_than_an_error() {
    // The index says where it is; the disk is the one that answers. A file removed between
    // the two is a tile that does not draw, not a shelf that fails.
    let f = Fixture::new();
    f.db.write(|cx| {
        cx.execute(
            "UPDATE entry SET cover_file = '/no/such/cover.jpg' WHERE id = 'v1'",
            [],
        )?;
        Ok(())
    })
    .unwrap();

    let pages = f.pages();
    assert!(pages.cover("v1", Some(300)).unwrap().is_none());
    assert!(pages.cover("v1", None).unwrap().is_none());
}

#[test]
fn a_page_the_index_names_and_the_archive_does_not_hold_is_nothing() {
    let f = Fixture::new();
    f.db.write(|cx| {
        cx.execute(
            "INSERT INTO page (entry_id, number, entry_name, media_type, width, height, size)
             VALUES ('v1', 9, 'jamais.jpg', 'image/jpeg', 100, 100, 10)",
            [],
        )?;
        Ok(())
    })
    .unwrap();

    let pages = f.pages();
    assert!(pages.page("v1", 9, Some(300)).unwrap().is_none());
    // And without a width either: the two go down different paths.
    assert!(pages.page("v1", 9, None).unwrap().is_none());
}

#[test]
fn a_page_whose_size_the_index_does_not_know_is_resized_anyway() {
    // Worth resizing is decided against the source width. With none recorded the answer is
    // yes: better a resize that saved nothing than a full page sent to a phone.
    let f = Fixture::new();
    f.db.write(|cx| {
        cx.execute("UPDATE page SET width = NULL WHERE number = 0", [])?;
        Ok(())
    })
    .unwrap();

    let pages = f.pages();
    let served = pages.page("v1", 0, Some(300)).unwrap().expect("a page");
    assert!(!served.bytes.is_empty());
}

#[test]
fn the_second_ask_for_one_page_at_one_width_comes_out_of_the_cache() {
    // The whole point of the cache: a page is resized once, and read many times.
    let f = Fixture::new();
    let pages = f.pages();
    let first = pages.page("v1", 0, Some(400)).unwrap().expect("a page");
    let files = f.cache_files();
    let second = pages.page("v1", 0, Some(400)).unwrap().expect("a page");

    assert_eq!(first.bytes, second.bytes);
    assert_eq!(first.tag, second.tag);
    assert_eq!(f.cache_files(), files, "nothing new was written");
}

#[test]
fn the_warming_queue_is_set_up_once_and_a_second_time_changes_nothing() {
    // Everything it does is an optimisation, and the server answers correctly without it —
    // so asking twice is a no-op rather than a second set of threads.
    let f = Fixture::new();
    let pages = Arc::new(f.pages());
    pages.start_warming(1, 2);
    pages.start_warming(1, 2);
    assert_eq!(pages.pending(), 0);
}

#[test]
fn nothing_is_read_ahead_when_no_queue_was_ever_started() {
    let f = Fixture::new();
    let pages = f.pages();
    // Without a queue there is nowhere to put the work, and asking is not an error.
    pages.warm_ahead("v1", 0, 400);
    pages.warm_opening("v1", Some(400));
    // And without a width there is nothing to prepare: serving the source costs nothing.
    pages.warm_opening("v1", None);
    assert_eq!(pages.pending(), 0);
}

#[test]
fn a_cache_that_cannot_be_made_is_said_and_the_pages_still_serve() {
    // The cache is an optimisation. A server that cannot write one still answers, it just
    // resizes the same page every time.
    let dir = tempfile::tempdir().expect("a directory");
    let closed = dir.path().join("closed");
    std::fs::create_dir(&closed).unwrap();
    read_only(&closed);

    let f = Fixture::new();
    let pages = Pages::new(
        Arc::clone(&f.db),
        closed.join("cache"),
        85,
        64 * 1024 * 1024,
    );
    pages.prepare();
    let served = pages.page("v1", 0, Some(300)).unwrap().expect("a page");
    assert!(!served.bytes.is_empty());

    writable(&closed);
}

#[test]
fn a_page_the_index_thinks_is_wider_than_it_is_comes_back_untouched() {
    // The plan is made from what the index recorded. When that is stale — a volume replaced
    // by a smaller scan, say — the resize finds nothing to shrink and the original goes
    // back rather than a blurred enlargement.
    let f = Fixture::new();
    f.db.write(|cx| {
        cx.execute(
            "UPDATE page SET width = 4000, height = 6000 WHERE number = 2",
            [],
        )?;
        Ok(())
    })
    .unwrap();

    let pages = f.pages();
    // 002.jpg is really 300 wide. Asking for 900 looks worth doing and turns out not to be.
    let served = pages.page("v1", 2, Some(900)).unwrap().expect("a page");
    assert_eq!(served.bytes, {
        let plain = pages.page("v1", 2, None).unwrap().expect("a page");
        plain.bytes
    });
}

#[test]
fn a_cover_file_that_cannot_be_read_is_nothing_rather_than_an_error() {
    // It is there and shut. A tile that does not draw, not a shelf that fails.
    let f = Fixture::new();
    let closed = f.dir.path().join("closed");
    std::fs::create_dir(&closed).unwrap();
    let beside = closed.join("cover.jpg");
    std::fs::write(&beside, jpeg(300, 400)).unwrap();
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        std::fs::set_permissions(&beside, std::fs::Permissions::from_mode(0o000)).unwrap();
    }
    f.db.write(|cx| {
        cx.execute(
            "UPDATE entry SET cover_file = ?1 WHERE id = 'v1'",
            [beside.to_string_lossy().to_string()],
        )?;
        Ok(())
    })
    .unwrap();

    let pages = f.pages();
    assert!(pages.cover("v1", Some(300)).unwrap().is_none());
    // Put back readable: this fixture's directory outlives the test, and a file left at 0
    // would still be sitting there, wrongly, for whatever runs in it next.
    writable(&beside);
}

#[test]
fn a_shelf_tile_that_cannot_be_made_is_said_and_the_sweep_carries_on() {
    // One edition whose file has gone, one whose file is there: the sweep must not stop at
    // the first, or a single broken volume costs the whole shelf its tiles.
    let f = Fixture::new();
    f.db.write(|cx| {
        cx.execute(
            "INSERT INTO edition (id, work_id, path, implicit) VALUES ('e2','w','/w/e2',1)",
            [],
        )?;
        cx.execute(
            "INSERT INTO entry (id, edition_id, type, file, size, modified_at, added_at,
                                volume_number, sort_key, page_count)
             VALUES ('v2','e2','VOLUME','/no/such/Tome 1.cbz',1,1700000000000,1,1.0,1.0,1)",
            [],
        )?;
        Ok(())
    })
    .unwrap();

    let pages = Arc::new(f.pages());
    pages
        .series_cover("e", Some(300))
        .unwrap()
        .expect("a cover");
    std::fs::remove_dir_all(f.dir.path().join("cache")).unwrap();
    pages.prepare();
    pages.warm_covers();

    // The good one still gets made, whichever order they come in.
    let deadline = std::time::Instant::now() + std::time::Duration::from_secs(30);
    while f.cache_files() == 0 {
        assert!(
            std::time::Instant::now() < deadline,
            "the sweep stopped at the broken one"
        );
        std::thread::sleep(std::time::Duration::from_millis(20));
    }
}
