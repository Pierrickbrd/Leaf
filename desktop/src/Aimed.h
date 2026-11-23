#pragma once

// The checking frame of a card laid on something the library already holds.
//
// A drop that aims at nothing is read as a folder to file: containers, files, who said what.
// One aimed at a volume or a series is read otherwise — what it replaces, adds and leaves —
// and what the server found each file to be (`Api::VolumeMatch`) is the whole of the list.
// Pure on purpose, like `Facts`: a card and what it composes to in, the frame's values out,
// so `Review` only chooses between this and the frame it always had.

#include "Card.h"
#include "Facts.h"

#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace Aimed {

/// The groups of the list, in the order of `Review::Filter`.
enum class Group { All = 0, ToReview = 1, Ready = 2, Identical = 3 };

/// Beyond this many pages gained or lost, the reader is asked what becomes of the place it
/// has in the volume: a few pages is a corrected scan, many is another book.
constexpr int MostPagesStill = 5;

/// Whether the card is laid on a volume (`ENTRY`, `CHAPTER`) and not on a series.
bool onAVolume(const Card &card);

QString title(const Card &card);
QString subtitle(const Card &card);
QVariantList rail(const Card &card);
QString railNote(const Card &card);

/// The editions the import adds that the server asks a name for — the series' current one
/// first — each `{path, label, name, problem}`, `problem` being what is wrong with the name
/// as typed. Empty for any other import.
QVariantList editionRows(const Card &card);
QString note(const Card &card);
QString listTitle(const Card &card);

/// The four filters and what each counts.
QVariantList counts(const Card &card, int lit);

/// Header lines and file lines, in the shape `Review::rows` gives its own.
QVariantList rows(const Card &card, int filter, bool showIdentical, const QString &focus,
                  const QString &searchKey);

/// The sheet of the line `id`, or of the target when `id` names no line.
QVariantMap sheet(const Card &card, const Facts::Composed &composed, const QString &id);

QString footer(const Card &card);
QString importLabel(const Card &card);
/// What « Appliquer » would do: the lines ticked, the additions, the values kept.
int changes(const Card &card);

/// Whether « Appliquer » has something to do: a change, or the one decision a file that is
/// another volume asks.
bool anything(const Card &card);

/// The number a file must be given to be the volume aimed at, and the one it reads of itself.
std::optional<double> aimedNumber(const Card &card);
std::optional<double> ownNumber(const Card &card, const QString &path);

/// The work a series line of an universe's drop would be moved by, if it is one that would
/// move; empty otherwise.
QString moveWorkId(const Card &card, const QString &id);

/// Whether `id` is another chapter of the volume the card is aimed at a chapter of.
bool isChapterOfAimedVolume(const Card &card, const QString &id);

} // namespace Aimed
