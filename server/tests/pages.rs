//! Serving a page, and the cache that means it is only ever resized once.

use std::io::Write;
use std::path::Path;
use std::sync::Arc;

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
