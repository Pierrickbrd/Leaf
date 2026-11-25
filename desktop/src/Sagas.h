#pragma once

#include <QVariantList>

/// A series' arcs as the window « Arcs de la série » draws them: one list, its sagas as
/// separators between the arcs they hold.
///
/// The file nests them — an arc names its saga in `parent`, and the saga is an entry of its
/// own with a range — and the window drew that nesting as a tree whose saga carried a range
/// typed by hand, which said 2–194 while its arcs said 2–23 and 136–194. Read as separators,
/// a saga's range is its arcs', the order is the chapters', and moving where a saga starts
/// is moving one line. Pure functions over the shapes `Review` hands QML, so they are tested
/// without a window.
///
/// A row is a map with a `kind`:
/// - `"arc"` — `name, unit, from, to, falls`;
/// - `"saga"` — `name, count, unit, from, to`, the range and count those of the arcs under it
///   down to the next separator;
/// - `"none"` — « Aucune saga »: the arcs under it belong to no saga;
/// - `"gap"` — `from, to`, chapters no arc covers between two arcs of chapters.
namespace Sagas {

/// The file's list (`{name, parent, unit, from, to, falls}`, as `Review` puts it in
/// `shape.arcs`) as rows. Arcs sorted by unit — chapters first — then range and name; an
/// entry other arcs name as `parent` becomes the separator before the first of them; an arc
/// with no saga after one that has one opens a `"none"`; a saga interrupted in the chapters
/// is a separator again, under the same name, where it resumes.
QVariantList rowsOf(const QVariantList &arcs);

/// The rows back as the file's list, in the shape `ReviewEdits::setArcs` takes. Gaps are dropped;
/// an arc's `parent` is the saga above it, and nothing under a `"none"` or before the first
/// separator. Each saga is written once, ahead of its first arc, its range that of all its
/// arcs. Empty when a separator has no name, an arc lacks a bound, or a saga holds no arc or
/// mixes chapters and volumes — the format refuses all of them.
QVariantList arcsOf(const QVariantList &rows);

/// The arcs sorted again, each separator kept just before the arc it preceded, and the sagas'
/// counts and ranges and the gaps worked out again: an arc whose start changed may land under
/// another separator, which is what moving it means. An arc whose start is not typed yet keeps
/// its place. Extra keys on a row (the window's `uid`) are kept by every edit.
QVariantList regapped(const QVariantList &rows);

/// The separator at `at` moved one arc up (`delta` -1) or down (+1). Nothing at either end,
/// and a separator never passes another.
QVariantList moved(const QVariantList &rows, int at, int delta);

/// The separator at `at` placed just before the row `arc`: « Commencer à l’arc… », and a drag.
QVariantList startedAt(const QVariantList &rows, int at, int arc);

/// The row at `at` taken out. A separator's arcs fall under the separator above, by
/// construction.
QVariantList removed(const QVariantList &rows, int at);

} // namespace Sagas
