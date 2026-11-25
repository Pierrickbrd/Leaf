#include "Sagas.h"

#include <QHash>
#include <QSet>
#include <QString>
#include <QVariantMap>

#include <algorithm>
#include <optional>
#include <tuple>

using namespace Qt::Literals::StringLiterals;

namespace Sagas {

namespace {

QString kindOf(const QVariant &row)
{
    return row.toMap().value(u"kind"_s).toString();
}

bool isSeparator(const QVariant &row)
{
    const QString kind = kindOf(row);
    return kind == u"saga"_s || kind == u"none"_s;
}

bool isArc(const QVariant &row)
{
    return kindOf(row) == u"arc"_s;
}

/// A bound as the window holds it: a number, or the text of a box typed into — empty while
/// « + Un arc » has only just added the line.
std::optional<int> numberOf(const QVariantMap &row, const QString &key)
{
    bool ok = false;
    const QVariant value = row.value(key);
    const int number = value.typeId() == QMetaType::QString ? value.toString().trimmed().toInt(&ok)
                                                            : value.toInt(&ok);
    return ok ? std::optional(number) : std::nullopt;
}

bool isChapters(const QVariantMap &arc)
{
    return arc.value(u"unit"_s).toString() == u"CHAPTER"_s;
}

/// Chapters before volumes, then by range, then by name: the order the chapters are read in,
/// which is the order the sagas are lived in.
bool readsBefore(const QVariantMap &one, const QVariantMap &other)
{
    const auto key = [](const QVariantMap &arc) {
        return std::tuple(!isChapters(arc), arc.value(u"from"_s).toInt(),
                          arc.value(u"to"_s).toInt(), arc.value(u"name"_s).toString());
    };
    return key(one) < key(other);
}

QVariantMap arcRow(const QVariantMap &arc)
{
    return {{u"kind"_s, u"arc"_s},
            {u"name"_s, arc.value(u"name"_s).toString()},
            {u"unit"_s, arc.value(u"unit"_s).toString()},
            {u"from"_s, arc.value(u"from"_s).toInt()},
            {u"to"_s, arc.value(u"to"_s).toInt()},
            {u"falls"_s, arc.value(u"falls"_s).toList()}};
}

/// Gives the saga row at `at` the count of the arcs under it down to the next separator, and
/// the unit and range of those whose bounds are typed.
void summarise(QVariantMap &row, const QVariantList &kept, qsizetype at)
{
    int count = 0;
    int spanned = 0;
    int from = 0;
    int to = 0;
    QString unit;
    for (qsizetype below = at + 1; below < kept.size() && !isSeparator(kept.at(below));
         ++below) {
        const QVariantMap arc = kept.at(below).toMap();
        const std::optional<int> start = numberOf(arc, u"from"_s);
        if (const std::optional<int> end = numberOf(arc, u"to"_s);
            start.has_value() && end.has_value()) {
            from = spanned == 0 ? *start : std::min(from, *start);
            to = spanned == 0 ? *end : std::max(to, *end);
            if (spanned == 0)
                unit = arc.value(u"unit"_s).toString();
            ++spanned;
        }
        ++count;
    }
    row.insert(u"count"_s, count);
    row.insert(u"unit"_s, unit);
    row.insert(u"from"_s, from);
    row.insert(u"to"_s, to);
}

/// An arc of chapters from `from` to `to`: a gap at `afterArc` when the chapters `reach`es
/// stop short of it, and `reach` carried on to `to`.
void reachOn(QVariantList &out, qsizetype afterArc, std::optional<int> &reach, int from, int to)
{
    if (reach && from > *reach + 1)
        out.insert(afterArc, QVariantMap{{u"kind"_s, u"gap"_s},
                                         {u"from"_s, *reach + 1},
                                         {u"to"_s, from - 1}});
    reach = std::max(reach.value_or(to), to);
}

/// The rows in the order given, with what is worked out from them worked out again: each
/// saga's count, unit and range from the arcs under it down to the next separator, and a gap
/// wherever the chapters an arc opens on are past every chapter the arcs above it reach. A
/// gap sits right after the arc it follows, ahead of any separator, so a separator stays
/// against the arc it opens on.
QVariantList finished(const QVariantList &rows)
{
    QVariantList kept;
    for (const QVariant &row : rows)
        if (kindOf(row) != u"gap"_s)
            kept << row;

    QVariantList out;
    std::optional<int> reach;
    qsizetype afterArc = 0;
    for (qsizetype at = 0; at < kept.size(); ++at) {
        QVariantMap row = kept.at(at).toMap();
        const QString kind = row.value(u"kind"_s).toString();
        if (kind == u"saga"_s) {
            summarise(row, kept, at);
            out << row;
        } else if (kind == u"arc"_s) {
            const std::optional<int> from = numberOf(row, u"from"_s);
            if (const std::optional<int> to = numberOf(row, u"to"_s);
                isChapters(row) && from.has_value() && to.has_value())
                reachOn(out, afterArc, reach, *from, *to);
            out << row;
            afterArc = out.size();
        } else {
            out << row;
        }
    }
    return out;
}

/// The entry of each saga of `order`, its range spanning the arcs it holds; none at all when one
/// of them cannot be written.
std::optional<QHash<QString, QVariantMap>>
sagaEntries(const QStringList &order, const QHash<QString, QList<QVariantMap>> &members)
{
    QHash<QString, QVariantMap> entries;
    for (const QString &name : order) {
        const QList<QVariantMap> held = members.value(name);
        // The format has a saga of chapters or one of volumes, and no saga of nothing.
        if (held.isEmpty())
            return std::nullopt;
        const QString unit = held.constFirst().value(u"unit"_s).toString();
        int from = held.constFirst().value(u"from"_s).toInt();
        int to = held.constFirst().value(u"to"_s).toInt();
        for (const QVariantMap &arc : held) {
            if (arc.value(u"unit"_s).toString() != unit)
                return std::nullopt;
            from = std::min(from, arc.value(u"from"_s).toInt());
            to = std::max(to, arc.value(u"to"_s).toInt());
        }
        entries.insert(name, {{u"name"_s, name}, {u"parent"_s, QString()}, {u"unit"_s, unit},
                              {u"from"_s, from}, {u"to"_s, to}});
    }
    return entries;
}

} // namespace

QVariantList rowsOf(const QVariantList &arcs)
{
    QSet<QString> sagas;
    for (const QVariant &one : arcs)
        if (const QString parent = one.toMap().value(u"parent"_s).toString().trimmed();
            !parent.isEmpty())
            sagas.insert(parent);

    // A saga is an entry with no `parent` that others name. One with a `parent` is an arc,
    // whatever its name: Water Seven, arc of the saga Water Seven, was filtered out as the
    // saga and written back without.
    QList<QVariantMap> leaves;
    for (const QVariant &one : arcs) {
        const QVariantMap entry = one.toMap();
        if (!entry.value(u"parent"_s).toString().trimmed().isEmpty()
            || !sagas.contains(entry.value(u"name"_s).toString().trimmed()))
            leaves << entry;
    }
    std::ranges::stable_sort(leaves, readsBefore);

    QVariantList rows;
    QString current;
    for (const QVariantMap &arc : std::as_const(leaves)) {
        if (const QString parent = arc.value(u"parent"_s).toString().trimmed();
            parent != current) {
            rows << (parent.isEmpty() ? QVariantMap{{u"kind"_s, u"none"_s}}
                                      : QVariantMap{{u"kind"_s, u"saga"_s}, {u"name"_s, parent}});
            current = parent;
        }
        rows << arcRow(arc);
    }
    return finished(rows);
}

QVariantList arcsOf(const QVariantList &rows)
{
    QList<QVariantMap> arcs;
    QStringList order;
    QHash<QString, QList<QVariantMap>> members;
    QString current;
    for (const QVariant &one : rows) {
        const QVariantMap row = one.toMap();
        const QString kind = row.value(u"kind"_s).toString();
        if (kind == u"saga"_s) {
            current = row.value(u"name"_s).toString().trimmed();
            if (current.isEmpty())
                return {};
            if (!members.contains(current)) {
                members.insert(current, {});
                order << current;
            }
        } else if (kind == u"none"_s) {
            current.clear();
        } else if (kind == u"arc"_s) {
            const std::optional<int> from = numberOf(row, u"from"_s);
            const std::optional<int> to = numberOf(row, u"to"_s);
            if (!from.has_value() || !to.has_value())
                return {};
            const QVariantMap arc{{u"name"_s, row.value(u"name"_s).toString()},
                                  {u"parent"_s, current},
                                  {u"unit"_s, row.value(u"unit"_s).toString()},
                                  {u"from"_s, *from},
                                  {u"to"_s, *to}};
            arcs << arc;
            if (!current.isEmpty())
                members[current] << arc;
        }
    }

    const std::optional<QHash<QString, QVariantMap>> entries = sagaEntries(order, members);
    if (!entries.has_value())
        return {};

    QVariantList out;
    QSet<QString> written;
    for (const QVariantMap &arc : std::as_const(arcs)) {
        if (const QString parent = arc.value(u"parent"_s).toString();
            !parent.isEmpty() && !written.contains(parent)) {
            out << entries->value(parent);
            written.insert(parent);
        }
        out << arc;
    }
    return out;
}

QVariantList regapped(const QVariantList &rows)
{
    // Each separator goes with the arc it stood before; one with no arc after it stays last.
    QList<std::pair<QVariantMap, QVariantList>> arcs;
    QVariantList waiting;
    for (const QVariant &row : rows) {
        if (isSeparator(row)) {
            waiting << row;
        } else if (isArc(row)) {
            arcs.append({row.toMap(), waiting});
            waiting.clear();
        }
    }
    // A line whose start is not typed stays where it stands; the others are sorted among the
    // places they hold. Sorted with them, the line « + Un arc » had just added at the foot went
    // to the top while it was being typed in.
    QList<qsizetype> placed;
    QList<std::pair<QVariantMap, QVariantList>> sorted;
    for (qsizetype at = 0; at < arcs.size(); ++at)
        if (numberOf(arcs.at(at).first, u"from"_s).has_value()) {
            placed << at;
            sorted << arcs.at(at);
        }
    std::ranges::stable_sort(sorted, [](const auto &one, const auto &other) {
        return readsBefore(one.first, other.first);
    });
    for (qsizetype at = 0; at < placed.size(); ++at)
        arcs[placed.at(at)] = sorted.at(at);
    QVariantList out;
    for (const auto &[arc, separators] : std::as_const(arcs)) {
        out << separators;
        out << arc;
    }
    out << waiting;
    return finished(out);
}

QVariantList moved(const QVariantList &rows, int at, int delta)
{
    if (at < 0 || at >= rows.size() || !isSeparator(rows.at(at)) || (delta != 1 && delta != -1))
        return rows;
    for (qsizetype next = at + delta; next >= 0 && next < rows.size(); next += delta) {
        if (isSeparator(rows.at(next)))
            return rows;
        if (!isArc(rows.at(next)))
            continue;
        // Down, the arc moves up one as the separator is taken out, and the separator lands
        // after it; up, the arc stays where it is and the separator lands before it.
        QVariantList out = rows;
        out.insert(next, out.takeAt(at));
        return finished(out);
    }
    return rows;
}

QVariantList startedAt(const QVariantList &rows, int at, int arc)
{
    if (at < 0 || at >= rows.size() || arc < 0 || arc >= rows.size() || !isSeparator(rows.at(at))
        || !isArc(rows.at(arc)))
        return rows;
    QVariantList out = rows;
    const QVariant separator = out.takeAt(at);
    out.insert(arc > at ? arc - 1 : arc, separator);
    return finished(out);
}

QVariantList removed(const QVariantList &rows, int at)
{
    if (at < 0 || at >= rows.size())
        return rows;
    QVariantList out = rows;
    out.removeAt(at);
    return finished(out);
}

} // namespace Sagas
