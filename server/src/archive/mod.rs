//! Reading what is inside a CBZ.
//!
//! A CBZ is a renamed zip — there is no standard behind the extension. So anything can be
//! dropped inside: that is already what ComicInfo.xml does, and what entry.json does.
//! Readers filter on images and ignore the rest.

pub mod cbz;
pub mod cbz_writer;
pub mod images;
pub mod natural_order;
