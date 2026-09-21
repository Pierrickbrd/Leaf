#include "Rearrange.h"

#include <QSet>

namespace Rearrange {

QList<Step> plan(const QStringList &held, const QStringList &fresh)
{
    QList<Step> steps;
    QStringList work = held;

    // What the new page does not hold, taken out in runs: a filter that drops half a shelf
    // is then a handful of signals rather than one per tile.
    QSet<QString> staying(fresh.constBegin(), fresh.constEnd());
    for (int row = work.size() - 1; row >= 0; --row) {
        if (staying.contains(work.at(row)))
            continue;
        int first = row;
        while (first > 0 && !staying.contains(work.at(first - 1)))
            --first;
        steps.append(Step{Step::Kind::Remove, first, row, 0, 0});
        work.remove(first, row - first + 1);
        row = first;
    }

    // Then each position in turn. A row already in the right place is left alone, which is
    // the whole point: it keeps its delegate.
    for (int at = 0; at < fresh.size(); ++at) {
        int found = -1;
        for (int row = at; row < work.size(); ++row) {
            if (work.at(row) == fresh.at(at)) {
                found = row;
                break;
            }
        }

        if (found < 0) {
            steps.append(Step{Step::Kind::Insert, 0, 0, 0, at});
            work.insert(at, fresh.at(at));
            continue;
        }
        if (found != at) {
            steps.append(Step{Step::Kind::Move, 0, 0, found, at});
            work.move(found, at);
        }
    }

    // Nothing should be left over — everything outside `fresh` was removed above — unless an
    // id was repeated. Said in steps rather than assumed, so a server that ever does that
    // leaves a short list rather than a model whose count disagrees with its rows.
    if (work.size() > fresh.size())
        steps.append(Step{Step::Kind::Remove, int(fresh.size()), int(work.size()) - 1, 0, 0});

    return steps;
}

} // namespace Rearrange
