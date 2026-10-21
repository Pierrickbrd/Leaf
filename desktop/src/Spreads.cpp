#include "Spreads.h"

namespace Spreads {

QList<Display> lay(const QList<Api::PageInfo> &pages, Layout layout)
{
    QList<Display> displays;
    const int count = static_cast<int>(pages.size());
    displays.reserve(count);

    // The cover is printed on its own, so pairing from page 0 would put every pair across a
    // fold the book never had (spec 03). Single mode never pairs, spread or not.
    int page = 0;
    if (count > 0)
        displays.append({page++, std::nullopt});

    while (page < count) {
        const bool pairs = layout == Layout::Double && !pages[page].spread && page + 1 < count
                           && !pages[page + 1].spread;
        displays.append({page, pairs ? std::optional<int>(page + 1) : std::nullopt});
        page += pairs ? 2 : 1;
    }
    return displays;
}

int displayOf(const QList<Display> &displays, int page)
{
    for (int i = 0; i < displays.size(); ++i) {
        if (displays[i].first == page || displays[i].second == page)
            return i;
    }
    return -1;
}

int lastPageOf(const Display &display)
{
    return display.second.value_or(display.first);
}

} // namespace Spreads
