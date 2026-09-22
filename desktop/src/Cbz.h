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

/// Reads the archive at `path` and returns its `entry.json`.
///
/// The member is matched on its **last path segment**, without case — `entry.json`,
/// `ENTRY.JSON`, `some/folder/Entry.Json` — which is what the server matches on. An archive
/// holding several takes the first the catalogue lists, as the server does.
Found sidecarOf(const QString &path);

} // namespace Cbz
