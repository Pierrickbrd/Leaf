#pragma once

// What a CBZ says about itself, read without unpacking it.
//
// The import asks the server where a file belongs **before** sending it, and the server
// answers from three things: the name, the size, and the `entry.json` inside the archive.
// The first two are free. This is the third.
//
// **It never touches an image.** A zip carries a catalogue at its end listing every member
// with its name, its size and where it sits; finding one small JSON member means reading
// that catalogue and inflating a few kilobytes. A volume is two hundred pages and two
// hundred megabytes, and none of it is read.
//
// It is deliberately the only thing this knows how to do. Counting pages was in the first
// draft, until `Intake::propose` turned out not to use a count — it only reaches the
// `concerns`, which the server fills from the file once it holds it. Counting here would
// have meant reproducing the server's idea of what an image is, which it decides from the
// leading bytes of each member rather than from an extension. Two classifiers that have to
// agree are two classifiers that drift.

#include <QByteArray>
#include <QList>
#include <QString>

namespace Cbz {

/// The ceiling on a member read whole, matching the server's own: past it, a file claiming
/// to be a sidecar is something else, and an archive can name a member any size it likes.
constexpr qint64 MostSidecarBytes = 4 * 1024 * 1024;

/// What was found, or why nothing was.
///
/// The two failures are not the same and the caller acts differently on each: an archive
/// that opened and holds no sidecar is an ordinary file from somewhere else, and it is
/// offered anyway. One that could not be opened at all is offered too — the spec says so —
/// but the screen says the proposal will be weaker, and it can only say that if it knows.
struct Found {
    /// The bytes of `entry.json`, or empty when the archive holds none.
    QByteArray sidecar;
    /// Empty when the archive was read. A sentence in French otherwise.
    QString trouble;

    bool opened() const { return trouble.isEmpty(); }
};

/// What one member of an archive decompresses to: its path inside the archive, its length
/// once decompressed and the SHA-256 of those bytes, lowercase hex.
///
/// Hashed on the content and not on what is stored: the server rewrites an archive by
/// copying its members raw and may rename them, so the content is the only thing that
/// is still the same afterwards.
struct Digest {
    QString name;
    qint64 size = 0;
    QString checksum;
};

/// The three members an archive may carry its description in, each with its own outcome.
///
/// They are read together because the import wants all three for every file of a folder, and
/// an archive's catalogue costs an open and a read each time it is asked for.
/// `Found::sidecar` holds the bytes of the member in any of the fields.
struct Members {
    Found entry;
    Found comicInfo;
    /// `oneshot.json`: the archive saying it is a whole book and not a volume of something.
    Found oneShot;
    /// The members whose extension is an image (`jpg jpeg png webp gif avif`), counted in
    /// the same pass over the catalogue that finds the three above: a reimport compares
    /// page counts, and a second open per archive would cost 115 000 seeks over a library.
    /// -1 when the archive does not open.
    int pages = -1;
    /// Every member that is not a folder, hashed — only when asked for, and empty when it
    /// was not or when one member could not be read: a list with a hole in it would make
    /// the server refuse a volume that is whole, so there is none.
    QList<Digest> digests = {};
    /// Why `digests` is empty when they were asked for and could not be had — a sentence in
    /// French — and empty otherwise. The sender is told, because a volume announced without
    /// them is checked as a file and no further, and nothing else would say so.
    QString digestTrouble;
};

/// Reads the archive at `path` once and returns its `entry.json`, its `ComicInfo.xml` and
/// its `oneshot.json`; with `withDigests`, in the same pass over the catalogue, the digest
/// of every member too. The members are inflated then, which is the minutes-long part of
/// the walk, and asked for only by the sender who verifies each volume.
///
/// Each member is matched on its **last path segment**, without case — `entry.json`,
/// `ENTRY.JSON`, `some/folder/Entry.Json` — which is what the server matches on. An archive
/// holding several takes the first the catalogue lists, as the server does. A member too
/// large or compressed in a way this does not read troubles only itself; an archive that
/// does not open puts the same sentence in all three.
Members membersOf(const QString &path, bool withDigests = false);

/// Reads the archive at `path` and returns its `entry.json` alone: `membersOf(path).entry`.
Found sidecarOf(const QString &path);

} // namespace Cbz
