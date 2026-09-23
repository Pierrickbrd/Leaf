#include "Entries.h"

#include "Words.h"

#include <QDebug>
#include <QJsonArray>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

/// « 54 p. » — what a file weighs in pages, and nothing at nought: a volume whose page count
/// was never read says so by saying nothing rather than by claiming none.
QString pagesOf(int pages)
{
    return pages > 0 ? u"%1 p."_s.arg(pages) : QString();
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

QVariant Entries::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_shown.size())
        return {};
    const Line &line = m_shown.at(index.row());
    using enum Role;

    // A gap answers for itself: no identifier, no pages, no weight. Nothing can be done to a
    // file that is not there, and a delegate reading an empty id draws no menu.
    if (line.missingNumber.has_value()) {
        switch (Role(role)) {
        case Number:
            return Words::number(*line.missingNumber);
        case Title:
            return Words::missingLabel(1);
        case State_:
            return QVariant::fromValue(State::Missing);
        default:
            return {};
        }
    }

    using enum State;
    switch (Role(role)) {
    case EntryId:
        return line.file.id;
    case Number:
        return line.file.number.has_value() ? Words::number(*line.file.number) : QString();
    case Title:
        return line.file.title.value_or(QString());
    case Pages:
        return pagesOf(line.file.pageCount);
    case Weight:
        return QVariant::fromValue(line.file.size);
    case State_: {
        if (!line.read.has_value())
            return QVariant::fromValue(NeverRead);
        // `finished` says where the reader stands now; a volume being read again is in
        // progress, whatever it has been before.
        if (line.read->finished)
            return QVariant::fromValue(Read);
        return QVariant::fromValue(line.read->page > 0 ? InProgress : NeverRead);
    }
    case HowFarRead: {
        if (!line.read.has_value() || line.read->pageCount <= 0)
            return 0.0;
        if (line.read->finished)
            return 1.0;
        return std::clamp(qreal(line.read->page) / qreal(line.read->pageCount), qreal(0),
                          qreal(1));
    }
    case TimesFinished:
        return line.read.has_value() ? Words::timesFinished(line.read->timesFinished)
                                     : QString();
    }
    return {};
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
    return named;
}

void Entries::point(const QString &seriesId, const QVariantList &missing)
{
    m_id = seriesId;
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
        fresh.append(Line{*read.value, std::nullopt, std::nullopt});
    }

    // The gaps, woven in where their numbers put them. In reading order like everything else,
    // because a hole between the sixth and the eighth is where the seventh would have been.
    for (const double number : m_missing) {
        Api::Entry nothing;
        nothing.number = number;
        Line gap{nothing, std::nullopt, number};
        const auto at = std::ranges::find_if(fresh, [number](const Line &line) {
            return line.file.number.has_value() && *line.file.number > number;
        });
        fresh.insert(at, gap);
    }

    m_all = fresh;
    m_trouble.clear();
    rebuild();

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

    for (Line &line : m_all) {
        if (line.missingNumber.has_value())
            continue;
        const auto found = where.constFind(line.file.id);
        if (found != where.constEnd())
            line.read = *found;
    }
    rebuild();
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
