//! Reading what is inside a CBZ.
//!
//! A CBZ is a renamed zip — there is no standard behind the extension. So anything can be
//! dropped inside: that is already what ComicInfo.xml does, and what entry.json does.
//! Readers filter on images and ignore the rest.

pub mod cbz;
pub mod cbz_writer;
pub mod images;
pub mod natural_order;

/// Whether an archive opens on page 1, whatever its name's width: `001.jpg`, `01.webp` and
/// `1.png` all say « no cover here ». A library's pages are numbered from 0, the cover, or
/// from 1 when the file has none, and a file from elsewhere counts the same way with fewer
/// zeros — a webtoon's `01.webp` opened on its first page of reading, and kept it as its cover
/// while its series held one. Any name that is not a number says nothing, and the first page
/// stays the cover.
///
/// Here and not beside the route that serves covers: the list of a series says the same
/// thing of each volume (`borrowedCover`), and two copies of a rule about names drift.
pub fn opens_without_cover(first: Option<&str>) -> bool {
    first.and_then(page_number) == Some(1)
}

/// The number a page is named by — `000.jpg` and `0.jpg` are 0, `01.webp` is 1 — or `None`
/// when its name is not one.
pub fn page_number(name: &str) -> Option<u64> {
    let stem = page_stem(name)?;
    if stem.is_empty() || !stem.bytes().all(|b| b.is_ascii_digit()) {
        return None;
    }
    stem.parse().ok()
}

/// A page's name without its folder or its extension: `sub/000.jpg` is `000`.
pub fn page_stem(name: &str) -> Option<&str> {
    name.rsplit('/').next().and_then(|n| n.split('.').next())
}
