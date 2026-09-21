#pragma once

// Turning one list of rows into another without resetting the model.
//
// A reset is the easy way to replace a page and the wrong one: every delegate is destroyed
// and rebuilt, so the covers blink, the item under the pointer changes identity, and a shelf
// whose contents merely turned round looks as though it were fetched from nothing. Worse,
// it is indistinguishable on screen from a shelf that did not change at all.
//
// The plan is computed here rather than inside either model because it is a decision about
// two lists of names and nothing else — no Qt, no network, no page. Both the shelf and the
// search apply it to their own rows, and `holds_the_order.cpp` proves it against the cases
// that are easy to get wrong: a pure reordering, which adds and removes nothing.

#include <QList>
#include <QString>
#include <QStringList>

namespace Rearrange {

/// One row operation, in the vocabulary `QAbstractItemModel` already speaks. `Move` carries
/// a single row: moving runs would be fewer signals, and every run it could merge is a run
/// whose rows a view has to re-lay out anyway.
struct Step {
    enum class Kind { Remove, Move, Insert };

    Kind kind = Kind::Remove;
    /// Remove: the first and last row of the run. Move: the row, in `from`. Insert: `to`.
    int first = 0;
    int last = 0;
    int from = 0;
    int to = 0;
};

/// The steps that turn `held` into `fresh`, applied in order, each against the list as the
/// previous ones left it. Ids are expected to be unique within each list; a repeated id in
/// `fresh` is tolerated by leaving the surplus to the trailing removal rather than by
/// pretending a row can be in two places.
QList<Step> plan(const QStringList &held, const QStringList &fresh);

} // namespace Rearrange
