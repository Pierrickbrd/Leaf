#include "Entries.h"

#include "Words.h"

#include <QDebug>
#include <QJsonArray>

#include <algorithm>
#include <limits>

using namespace Qt::StringLiterals;

namespace {

/// « 54 p. » — what a file weighs in pages, and nothing at nought: a volume whose page count
/// was never read says so by saying nothing rather than by claiming none.
QString pagesOf(int pages)
{
    return pages > 0 ? u"%1 p."_s.arg(pages) : QString();
}

/// The separator an arc begins on: its name, its range in its own unit, and how deep it sits.
QVariant separatorRow(const Api::Arc &arc, int depth, Entries::Role role)
{
    using enum Entries::Role;
    switch (role) {
    case Title:
        return arc.name;
    case Detail:
        return Words::arcRange(arc.unit, arc.from, arc.to);
    case Kind_:
        return QVariant::fromValue(Entries::Kind::Arc);
    case Depth:
        return depth;
    default:
        return {};
    }
}

/// One of the two stretches of chapters either side of a frontier inside a volume.
QVariant stretchRow(const QString &range, Entries::Role role)
{
    using enum Entries::Role;
    switch (role) {
    case Detail:
        return range;
    case Kind_:
        return QVariant::fromValue(Entries::Kind::Range);
    default:
        return {};
    }
}

/// A hole where a volume is not: a number, a word, and nothing that can be opened.
QVariant gapRow(double number, Entries::Role role)
{
    using enum Entries::Role;
    switch (role) {
    case Number:
        return Words::number(number);
    case Title:
        return Words::missingLabel(1);
    case State_:
        return QVariant::fromValue(Entries::State::Missing);
    case Kind_:
        return QVariant::fromValue(Entries::Kind::Gap);
    default:
        return {};
    }
}

/// A file, and the record that may or may not stand beside it.
QVariant fileRow(const Api::Entry &file, const std::optional<Api::Progress> &read,
                 Entries::Role role)
{
    using enum Entries::Role;
    using enum Entries::State;
    switch (role) {
    case EntryId:
        return file.id;
    case Number:
        return file.number.has_value() ? Words::number(*file.number) : QString();
    case Title:
        return file.title.value_or(QString());
    case Pages:
        return pagesOf(file.pageCount);
    case Weight:
        return QVariant::fromValue(file.size);
    case State_:
        if (!read.has_value())
            return QVariant::fromValue(NeverRead);
        // `finished` says where the reader stands now; a volume being read again is in
        // progress, whatever it has been before.
        if (read->finished)
            return QVariant::fromValue(Read);
        return QVariant::fromValue(read->page > 0 ? InProgress : NeverRead);
    case HowFarRead:
        if (!read.has_value() || read->pageCount <= 0)
            return 0.0;
        if (read->finished)
            return 1.0;
        return std::clamp(qreal(read->page) / qreal(read->pageCount), qreal(0), qreal(1));
    case TimesFinished:
        return read.has_value() ? Words::timesFinished(read->timesFinished) : QString();
    case Kind_:
        return QVariant::fromValue(Entries::Kind::File);
    case Detail:
    case Depth:
        return {};
    }
    return {};
}

} // namespace

Entries *Entries::create(QQmlEngine *engine, QJSEngine *)
{
    auto *server = engine->singletonInstance<Server *>(qmlTypeId("Leaf", 1, 0, "Server"));
    if (!server) {
        qWarning().noquote()
            << QStringLiteral("error resolving the Server singleton — the volumes of a series "
                              "will stay empty");
    }
    return new Entries(server);
}

Entries::Entries(Server *server, QObject *parent)
    : QAbstractListModel(parent)
    , m_server(server)
{
}

int Entries::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_shown.size());
}

double Entries::reading() const
{
    // A record exists only for a file somebody opened, so an unfinished record *is* the
    // volume being read. The first, because a reader is in one place: two open volumes is a
    // library where one was left behind, and the earlier one is where the story stands.
    for (const Line &line : m_files) {
        if (line.read.has_value() && !line.read->finished && line.file.number.has_value())
            return *line.file.number;
    }
    return std::numeric_limits<double>::quiet_NaN();
}

QVariant Entries::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_shown.size())
        return {};
    const Line &line = m_shown.at(index.row());
    const auto which = Role(role);

    // Four kinds of row and only one of them is a file. A separator, a stretch of chapters
    // and a gap each answer for themselves: none has an identifier, a weight or a mark, and
    // a delegate reading an empty identifier draws no menu on any of them. One function per
    // kind rather than four switches in a row, which is one function nobody could read.
    if (line.arc.has_value())
        return separatorRow(*line.arc, line.depth, which);
    if (!line.range.isEmpty())
        return stretchRow(line.range, which);
    if (line.missingNumber.has_value())
        return gapRow(*line.missingNumber, which);
    return fileRow(line.file, line.read, which);
}

QHash<int, QByteArray> Entries::roleNames() const
{
    QHash<int, QByteArray> named;
    using enum Role;
    // `entryId` and not `id`, for the reason the shelf gives: `id` is QML's own word for a
    // component's name, and a role called that is a trap laid for whoever writes the delegate.
    named.insert(qToUnderlying(EntryId), "entryId");
    named.insert(qToUnderlying(Number), "number");
    named.insert(qToUnderlying(Title), "title");
    named.insert(qToUnderlying(Pages), "pages");
    named.insert(qToUnderlying(Weight), "weight");
    named.insert(qToUnderlying(State_), "state");
    named.insert(qToUnderlying(HowFarRead), "howFarRead");
    named.insert(qToUnderlying(TimesFinished), "timesFinished");
    named.insert(qToUnderlying(Kind_), "kind");
    named.insert(qToUnderlying(Detail), "detail");
    named.insert(qToUnderlying(Depth), "depth");
    return named;
}

void Entries::point(const QString &seriesId, const QVariantList &missing, int arcCount)
{
    m_id = seriesId;
    m_arcCount = arcCount;
    m_arcs.clear();
    m_missing.clear();
    m_missing.reserve(missing.size());
    for (const QVariant &one : missing)
        m_missing.append(one.toDouble());
    reload();
}

void Entries::reload()
{
    ++m_generation;
    m_trouble.clear();

    if (m_id.isEmpty() || !m_server) {
        beginResetModel();
        m_all.clear();
        m_shown.clear();
        endResetModel();
        m_loading = false;
        if (!m_server && !m_id.isEmpty())
            m_trouble = Words::notSetUp(Words::Asking::Shelf);
        emit changed();
        return;
    }

    m_loading = true;
    // Nothing is cleared: the list on screen stays until the new one arrives.
    emit changed();

    const int mine = m_generation;
    m_server->get(u"/series/"_s + m_id + u"/entries"_s, this,
                  [this, mine](const Server::Answer &answer) {
        if (mine == m_generation)
            tookEntries(answer);
    });
}

void Entries::tookEntries(const Server::Answer &answer)
{
    if (!answer.went()) {
        m_loading = false;
        m_trouble = answer.trouble;
        emit changed();
        return;
    }
    if (!answer.body.isArray()) {
        m_loading = false;
        m_trouble = QStringLiteral("entries: expected an array");
        emit changed();
        return;
    }

    QList<Line> fresh;
    for (const QJsonValue &one : answer.body.array()) {
        if (!one.isObject()) {
            m_loading = false;
            m_trouble = QStringLiteral("entries: expected an object");
            emit changed();
            return;
        }
        const Api::Read<Api::Entry> read = Api::entry(one.toObject());
        if (!read.ok()) {
            m_loading = false;
            m_trouble = read.trouble;
            emit changed();
            return;
        }
        Line file;
        file.file = *read.value;
        fresh.append(file);
    }

    // The gaps, woven in where their numbers put them. In reading order like everything else,
    // because a hole between the sixth and the eighth is where the seventh would have been.
    for (const double number : m_missing) {
        Line gap;
        gap.file.number = number;
        gap.missingNumber = number;
        const auto at = std::ranges::find_if(fresh, [number](const Line &line) {
            return line.file.number.has_value() && *line.file.number > number;
        });
        fresh.insert(at, gap);
    }

    m_files = fresh;
    m_trouble.clear();
    weaveArcs();

    // The reading states follow. A refusal here costs the marks and not the list: a page of
    // volumes with no ring on any line is still the page, and a banner over it would say the
    // series could not be read at all.
    const int mine = m_generation;
    m_server->get(u"/series/"_s + m_id + u"/progress"_s, this,
                  [this, mine](const Server::Answer &states) {
        if (mine == m_generation)
            tookProgress(states);
    });
}

void Entries::tookProgress(const Server::Answer &answer)
{
    m_loading = false;
    if (!answer.went() || !answer.body.isArray()) {
        emit changed();
        return;
    }

    QHash<QString, Api::Progress> where;
    for (const QJsonValue &one : answer.body.array()) {
        if (!one.isObject())
            continue;
        const Api::Read<Api::Progress> read = Api::progress(one.toObject());
        if (read.ok())
            where.insert(read.value->entryId, *read.value);
    }

    for (Line &line : m_files) {
        if (line.missingNumber.has_value())
            continue;
        const auto found = where.constFind(line.file.id);
        if (found != where.constEnd())
            line.read = *found;
    }
    weaveArcs();

    // The separators last, and only when there are any: a screen knows before it asks.
    if (m_arcCount <= 0 || !m_arcs.isEmpty())
        return;
    const int mine = m_generation;
    m_server->get(u"/series/"_s + m_id + u"/arcs"_s, this,
                  [this, mine](const Server::Answer &ranges) {
        if (mine == m_generation)
            tookArcs(ranges);
    });
}

void Entries::tookArcs(const Server::Answer &answer)
{
    // A refusal costs the separators and nothing else. A list of volumes with no arc drawn
    // over it is still the list, and a banner would say the series could not be read.
    if (!answer.went() || !answer.body.isArray())
        return;

    for (const QJsonValue &one : answer.body.array()) {
        if (!one.isObject())
            continue;
        if (const Api::Read<Api::Arc> read = Api::arc(one.toObject()); read.ok())
            m_arcs.append(*read.value);
    }
    std::ranges::sort(m_arcs, {}, &Api::Arc::position);
    weaveArcs();
}

void Entries::weaveArcs()
{
    m_all = m_files;
    if (m_arcs.isEmpty()) {
        rebuild();
        return;
    }

    // A saga holds its arcs, and one level of indentation is drawn: past that the eye loses
    // the thread. The parent has to be one of this edition's arcs — the scan guarantees it,
    // and a link to nothing indents nothing.
    const auto declared = [this](const QString &id) {
        return std::ranges::any_of(m_arcs, [&id](const Api::Arc &one) { return one.id == id; });
    };

    for (const Api::Arc &arc : m_arcs) {
        Line separator;
        separator.arc = arc;
        separator.depth = arc.parentId.has_value() && declared(*arc.parentId) ? 1 : 0;

        const qsizetype at = beginsAt(arc);
        if (at < 0)
            continue;
        const std::optional<double> key = arc.unit == Api::Arc::Unit::Volume
                                              ? m_all.at(at).file.number
                                              : m_all.at(at).file.sortKey;
        // A chapter range beginning above this file's own first chapter runs *through* it.
        // Anything else begins at a line, and the separator goes above that line.
        if (arc.unit == Api::Arc::Unit::Volume || !key.has_value() || *key >= arc.from) {
            m_all.insert(at, separator);
            continue;
        }
        cutOpen(at, *key, arc, separator);
    }
    rebuild();
}

qsizetype Entries::beginsAt(const Api::Arc &arc) const
{
    // A volume range starts at the first file numbered that high; a chapter range starts
    // inside the last file whose own first chapter is below it — `sortKey` being « the first
    // chapter's number, otherwise the volume number ».
    const bool byVolume = arc.unit == Api::Arc::Unit::Volume;
    qsizetype at = -1;
    for (qsizetype i = 0; i < m_all.size(); ++i) {
        const Line &line = m_all.at(i);
        if (line.arc.has_value() || !line.range.isEmpty())
            continue;
        const std::optional<double> key = byVolume ? line.file.number : line.file.sortKey;
        if (!key.has_value())
            continue;
        if (byVolume && *key >= arc.from)
            return i;
        if (!byVolume && *key <= arc.from)
            at = i;
    }
    return at;
}

void Entries::cutOpen(qsizetype at, double first, const Api::Arc &arc, const Line &separator)
{
    // The frontier is inside a line, so it is inside the line that it is drawn: the file
    // keeps its place, and under it come the two stretches either side of the marker. Not
    // the chapters themselves — a range is not something one opens, and `startPage` is null
    // on most libraries.
    Line before;
    before.range = Words::chapterRange(first, arc.from - 1);
    Line after;
    const std::optional<double> next = nextKey(at);
    after.range = next.has_value() ? Words::chapterRange(arc.from, *next - 1)
                                   : Words::fromChapter(arc.from);
    m_all.insert(at + 1, after);
    m_all.insert(at + 1, separator);
    m_all.insert(at + 1, before);
}

/// The first chapter of the file after this one, which is what bounds the stretch above it.
/// The last file of an edition has no neighbour, and its range is written open.
std::optional<double> Entries::nextKey(qsizetype after) const
{
    for (qsizetype i = after + 1; i < m_all.size(); ++i) {
        const Line &line = m_all.at(i);
        if (line.arc.has_value() || !line.range.isEmpty() || line.missingNumber.has_value())
            continue;
        if (line.file.sortKey.has_value())
            return line.file.sortKey;
    }
    return std::nullopt;
}

void Entries::searchFor(const QString &query)
{
    const QString asked = query.trimmed();
    if (asked == m_query)
        return;
    m_query = asked;
    rebuild();
}

void Entries::rebuild()
{
    beginResetModel();
    if (m_query.isEmpty()) {
        m_shown = m_all;
    } else {
        m_shown.clear();
        for (const Line &line : m_all) {
            // Its number or its title — the two things written on the line. A gap is matched
            // on its number like any other: it is in the list, so it is searchable in it.
            const QString number = line.file.number.has_value()
                                       ? Words::number(*line.file.number)
                                       : QString();
            const QString title = line.file.title.value_or(QString());
            if (number.contains(m_query, Qt::CaseInsensitive)
                || title.contains(m_query, Qt::CaseInsensitive))
                m_shown.append(line);
        }
    }
    endResetModel();
    emit changed();
}
