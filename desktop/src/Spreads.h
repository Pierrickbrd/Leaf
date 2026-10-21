// Which pages share a screen. A pure function of the page list: the reader asks it again
// whenever the window changes shape, so it keeps nothing between two calls.

#pragma once

#include "Api.h"

#include <QList>

#include <optional>

namespace Spreads {

enum class Layout { Single, Double };

/// One screen. `first` is the page read first; which of the two sits on the left is the
/// reading direction's business, decided where it is drawn.
struct Display {
    int first = 0;
    std::optional<int> second;
};

/// Pages are indexes into `pages`, from 0. The cover is always alone, then pairs run
/// (1,2), (3,4)... A double page is alone and pairing starts again right after it; a page
/// left without a partner, before a double or at the end, is alone too.
QList<Display> lay(const QList<Api::PageInfo> &pages, Layout layout);

/// The display holding `page`, or -1 when no display does.
int displayOf(const QList<Display> &displays, int page);

/// The page to record as reached: the later of the two on screen.
int lastPageOf(const Display &display);

} // namespace Spreads
