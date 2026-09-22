#pragma once

// What a folder says it holds, before a byte of it moves.
//
// The bulk path announces a manifest and the server answers what it is missing; this builds
// that manifest from a folder on disk. Like `Cbz`, it is the brick under everything else:
// no network, no interface, and provable on its own.
//
// **A folder is the declaration.** No route creates a universe, a work or an edition — a
// universe exists because a folder with a `universe.json` exists, and the scanner derives
// the rest from the disk. So the sidecars are gathered here beside the file list: they are
// what the destination is read from, and they are a few hundred bytes against gigabytes.
//
// Symlinks are leaves, never descended into. A link back to a parent is then a file this
// does not follow rather than a walk that never ends.

#include <QByteArray>
#include <QList>
#include <QString>

#include <functional>

namespace Manifest {

/// How deep the walk goes before it decides something is wrong.
///
/// A library is universe, work, edition, file — four. Sixteen leaves room for a way of
/// arranging shelves nobody has thought of yet, and still stops a loop that symlinks
/// somehow let through.
constexpr int DeepestFolders = 16;

/// How many shelves the walk follows before deciding a library is not one.
///
/// The same value as `layout::MAX_SHELVES` on the server, and that is the constant's
/// reason to exist: two walks that stop at different depths would contradict each other
/// on some twisted case, with nothing to say which one is right.
constexpr int DeepestShelves = 8;

/// One file, in the shape `POST /import` reads.
struct Entry {
    /// Relative to the root, with `/` whatever the platform writes. Never escapes it.
    QString path;
    qint64 size = 0;
    /// SHA-256, lowercase hex, and empty when it was not asked for.
    ///
    /// It costs a full read of every file, and it buys two things nothing else can: a file
    /// already in the library compared on its **contents** rather than on its length — a
    /// title corrected inside an `entry.json` leaves the archive exactly as long as it was
    /// — and what arrives verified before it is installed. A volume that travelled wrong is
    /// worse than one that did not travel, because nothing afterwards would say so.
    QString checksum;
    /// What the archive declares itself to be, read out of its own `entry.json`.
    ///
    /// Empty when nothing opened it — the instant tree opens no archive, and a drop without
    /// checksums opens none either. A title is worth a second open only where the whole file
    /// is being read anyway, which is the verification pass.
    QString title;
};

/// A folder's sidecar, kept as the bytes on disk rather than parsed.
///
/// Unparsed on purpose: the client has no business deciding what a `work.json` means, and a
/// field it does not understand is a field it must not drop on the way to a server that
/// might. The same rule the identity spec sets for writing them.
struct Sidecar {
    /// Relative to the root, so the server knows which folder it describes.
    QString path;
    QByteArray json;
};

/// A level of the model — and nothing else. A shelf is not one: it is walked through and
/// never kept, which is exactly what `scan/layout.rs` says.
///
/// `Chapter` sits between `Edition` and `Volume` because a chapter is not under a volume,
/// it stands beside it: `entry.type` is VOLUME or CHAPTER, never one nested in the other.
enum class Level { Universe, Work, Edition, Chapter, Volume };

/// A declared thing, and what it holds.
///
/// The tree exists **before** any checksum: finding is instant, hashing takes minutes,
/// and showing the structure in between is what tells waiting apart from a freeze.
struct Node {
    Level level = Level::Work;
    /// What the reader reads: the declared name if there is one, the folder's otherwise.
    QString name;
    /// Relative to the folder that was asked about — `Manifest::of`'s `path`, or
    /// `Manifest::found`'s. It is what the server receives in `sidecars[].path` and in
    /// `creates[].at`, so it is how its answer lands on the right node.
    ///
    /// For `found`, that means `""` only when the folder asked about was itself the thing
    /// found — a shelf's children answer with their own subpath instead, which is how a
    /// caller tells two series on one shelf apart. A caller that wants a card's own root at
    /// `""` regardless — `Imports::offer` is one — rebases it once the card is settled.
    QString at;
    /// The sidecar's bytes, as they are. Empty for an implicit edition and for a volume —
    /// neither one declares anything.
    QByteArray declaration;
    qint64 size = 0;
    QList<Node> children;
    /// The archives sitting directly in this node, never its children's.
    QList<Entry> files;

    /// How many volumes under this node, itself included.
    qint64 volumes() const;
};

/// What was found, or why nothing was.
struct Folder {
    QString root;
    QList<Entry> files;
    QList<Sidecar> sidecars;
    /// Empty when the folder was read.
    QString trouble;

    bool read() const { return trouble.isEmpty(); }
    qint64 bytes() const;
};

/// Called once per file the walk has read, with the running total — never a fraction,
/// because the caller already knows the folder's own volume count from `found()` and can
/// divide however it likes.
using Progress = std::function<void(qint64 done)>;

/// Walks `path` and describes it.
///
/// `withChecksums` is the sender's call and the caller's cost: true reads every byte of
/// every file once, here, before anything is sent. `onward`, when given, is called after
/// each file — the checksum is what makes this take minutes rather than seconds, and a
/// caller that says nothing about its progress for that long is a caller that looks stuck.
Folder of(const QString &path, bool withChecksums = true, const Progress &onward = {});

/// What a dropped folder declares, shelves walked through.
///
/// **No checksum is computed here.** That is what makes finding instant, and it is
/// deliberate: dropping a whole library hashed everything before showing anything, and
/// the window looked stuck long enough for the desktop to offer to kill it.
QList<Node> found(const QString &path);

/// The SHA-256 of one file, lowercase hex, or empty when it could not be read.
QString checksumOf(const QString &path);

/// Whether a file is a volume or a chapter, from its name alone.
///
/// The same rule the server falls back on — `scanner::entry_kind` — and deliberately only
/// the fallback: the declaration inside the archive would be exact, and reading it would
/// mean opening every zip of a dropped folder, which is the instant finding this tree was
/// built to have. The tree is a preview; the scan corrects it once the files have landed.
Level levelOf(const QString &fileName);

} // namespace Manifest
