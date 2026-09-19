#include "Shelf.h"

#include "Rearrange.h"

#include "Words.h"

#include <QDebug>

#include <QJsonObject>
#include <QSet>
#include <QUrlQuery>
#include <QtGlobal>

using namespace Qt::StringLiterals;

namespace {

/// Asked for explicitly although it is also the contract's default, because a page size the
/// client did not choose is a page size no test here pins, and the server is free to move its
/// own default without telling anyone.
constexpr int Size = 100;

} // namespace

Shelf *Shelf::create(QQmlEngine *engine, QJSEngine *)
{
    auto *server = engine->singletonInstance<Server *>(qmlTypeId("Leaf", 1, 0, "Server"));
    if (!server) {
        // Not fatal here. A shelf with no server fills with nothing and says so on the screen,
        // which is a great deal more useful than a crash on the first tile.
        qWarning().noquote()
            << QStringLiteral("error resolving the Server singleton — the shelf will stay "
                              "empty");
    }
    return new Shelf(server);
}

Shelf::Shelf(Server *server, QObject *parent)
    : QAbstractListModel(parent)
    , m_server(server)
{
    // One shot per pause in the typing, and the question goes out once the pause is over.
    // `criteriaChanged` fires here rather than when the letter was pressed, so that whatever
    // follows the shelf — the rows a search draws above it — waits with it instead of asking
    // its own question per key.
    m_settling.setSingleShot(true);
    connect(&m_settling, &QTimer::timeout, this, [this] {
        emit criteriaChanged();
        reload();
    });
}

int Shelf::rowCount(const QModelIndex &parent) const
{
    // A list has its rows at the root and nowhere else. Without this a view walking the tree
    // would find the whole shelf again under every tile.
    return parent.isValid() ? 0 : int(m_held.size());
}

int Shelf::count() const
{
    return int(m_held.size());
}

int Shelf::total() const
{
    return m_total;
}

bool Shelf::loading() const
{
    return m_loading;
}

QString Shelf::trouble() const
{
    return m_trouble;
}

QVariant Shelf::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_held.size()) {
        return {};
    }

    const Api::Series &one = m_held.at(index.row());
    using enum Role;
    switch (static_cast<Role>(role)) {
    case SeriesId:
        return one.id;
    case Name:
        return one.name;
    case Work:
        return one.work;
    case Cover:
        // Whole, and straight into an `Image`. The key it needs is put on by `Covers`, the
        // engine's network manager factory, so the route is spelled once here instead of
        // being assembled out of `Settings.address` in every `.qml` that draws a tile.
        return m_server->address() + u"/series/"_s + one.id + u"/cover"_s;
    case Medium:
        // Absent stays absent. "Autre" is the answer for a word this client has not been
        // taught, not the answer for a medium nobody recorded.
        return one.medium ? Words::medium(*one.medium) : QString();
    case Volumes:
        return Words::volumes(one.holding.ownedVolumes, one.medium);
    case InProgress:
        // A tile draws a mark or draws nothing. Read and never-opened are the same answer
        // here — neither carries one — so this is a boolean and not three cases sent to QML.
        return one.holding.readStatus == Api::ReadStatus::InProgress;
    default:
        // Reached for `Qt::DisplayRole` and everything else a view asks about by habit, so it
        // is an ordinary answer rather than a case that should not happen.
        return {};
    }
}

QHash<int, QByteArray> Shelf::roleNames() const
{
    // `seriesId` and not `id`: `id` is QML's own word for a component's name, and a role
    // called that is a trap laid for whoever writes the delegate.
    QHash<int, QByteArray> named;
    using enum Role;
    named.insert(qToUnderlying(SeriesId), "seriesId");
    named.insert(qToUnderlying(Name), "name");
    named.insert(qToUnderlying(Work), "work");
    named.insert(qToUnderlying(Cover), "cover");
    named.insert(qToUnderlying(Medium), "medium");
    named.insert(qToUnderlying(Volumes), "volumes");
    named.insert(qToUnderlying(InProgress), "inProgress");
    return named;
}

bool Shelf::canFetchMore(const QModelIndex &parent) const
{
    // Never while a replacement is in flight: page one of the new criteria would land on
    // top of a shelf still holding the old ones, under the old page numbering.
    return !parent.isValid() && m_more && !m_loading && !m_replacing
           && m_held.size() < m_total;
}

void Shelf::fetchMore(const QModelIndex &parent)
{
    if (canFetchMore(parent)) {
        ask(m_next);
    }
}

namespace {

QStringList kept(const QStringList &asked)
{
    // The contract drops blank values rather than reading them as a filter on the empty
    // string. Dropped here as well, so that what the shelf holds and what it sent are the
    // same list — otherwise `readStatuses` reports a pill nobody can see lit.
    QStringList worth;
    for (const QString &one : asked) {
        if (!one.isEmpty())
            worth << one;
    }
    return worth;
}

} // namespace

QStringList Shelf::chosen(const QString &axis) const
{
    return m_narrowing.value(axis).toStringList();
}

namespace {

/// The axes this client knows how to send, in the order the panel draws them. A key the
/// contract does not name is dropped rather than put on the wire: a query string the server
/// ignores narrows nothing, and the pills would then show a filter that does not filter.
const QStringList &axisNames()
{
    static const QStringList names{u"read"_s,      u"medium"_s,   u"universe"_s,
                                   u"genre"_s,     u"author"_s,   u"publisher"_s,
                                   u"language"_s,  u"status"_s};
    return names;
}

} // namespace

void Shelf::filterBy(const QVariantMap &narrowing)
{
    QVariantMap asked;
    for (const QString &axis : axisNames()) {
        const QStringList values = kept(narrowing.value(axis).toStringList());
        if (!values.isEmpty())
            asked.insert(axis, values);
    }
    if (asked == m_narrowing)
        return;

    m_narrowing = asked;
    // The field and the pills follow the choice at once; the question waits for the hand to
    // stop. Three genres ticked in a row are one request, not three of which two are thrown
    // away — the same settling the typing already uses, for the same reason.
    emit changed();
    m_settling.start(Settling);
}

void Shelf::sortBy(const QString &order)
{
    const QString asked = Api::spell(Api::sort(order));
    // A word this client does not know becomes `name`, the way it does on the server — but it
    // is not a second click on `name`, and must not turn the shelf round on its way through.
    const bool named = order.trimmed().compare(asked, Qt::CaseInsensitive) == 0;
    // Asking again for the order already in force is how the direction is reversed. It was
    // a fifth entry under the four criteria, which read as a fifth criterion and made the
    // menu answer two questions at once: the reversal belongs to the criterion it reverses.
    if (asked == m_sort) {
        if (!named)
            return;
        m_reversed[asked] = !m_reversed.value(asked, false);
    } else {
        // Each criterion keeps the direction it was left in, and begins in its familiar one.
        // Carrying the previous criterion's reversal over turned « Nom · Z → A » into dates
        // oldest first the moment the reader changed criterion, having asked for nothing.
        m_sort = asked;
    }
    emit changed();
    emit criteriaChanged();
    reload();
}

QString Shelf::sortDirection() const
{
    const bool naturallyAscending = Api::sort(m_sort) == Api::Sort::Name;
    const bool ascending = sortReversed() ? !naturallyAscending : naturallyAscending;
    return ascending ? QStringLiteral("asc") : QStringLiteral("desc");
}

void Shelf::searchFor(const QString &query)
{
    const QString asked = query.trimmed();
    if (asked == m_query)
        return;

    m_query = asked;
    // What was typed, straight away: the field, the cross that clears it and anything else
    // showing the text must not lag a key behind.
    emit changed();

    if (asked.isEmpty()) {
        // Clearing is not typing. The whole shelf comes back at once.
        m_settling.stop();
        emit criteriaChanged();
        reload();
        return;
    }

    m_settling.start(Settling);
}

void Shelf::reload()
{
    // The shelf is not emptied here. Clearing at the moment of asking put an empty grid and
    // a spinner between a reader's click on a pill and the answer to it — every series
    // vanished, including the ones the pill was never going to remove. What is on screen
    // stays until there is something to put in its place.
    ++m_generation;
    m_replacing = true;
    m_trouble.clear();
    ask(0);
}

namespace {

/// Whether a tile drawn from `before` would be drawn the same from `after`. Only what the
/// model exposes counts: a field no delegate reads cannot change what anybody sees.
bool showsTheSame(const Api::Series &before, const Api::Series &after)
{
    return before.name == after.name && before.work == after.work
           && before.medium == after.medium
           && before.holding.ownedVolumes == after.holding.ownedVolumes
           && before.holding.readStatus == after.holding.readStatus;
}

} // namespace

void Shelf::replaceWith(const QList<Api::Series> &fresh)
{
    // By id and row by row rather than a reset: a series the new criteria still hold is the
    // same tile, and it keeps its delegate, its cover and its place under the pointer. The
    // plan itself is `Rearrange`, shared with the search and tested on its own.
    QStringList held;
    held.reserve(int(m_held.size()));
    for (const Api::Series &one : m_held)
        held << one.id;
    QStringList wanted;
    wanted.reserve(fresh.size());
    for (const Api::Series &one : fresh)
        wanted << one.id;

    for (const Rearrange::Step &step : Rearrange::plan(held, wanted)) {
        using enum Rearrange::Step::Kind;

        switch (step.kind) {
        case Remove:
            beginRemoveRows({}, step.first, step.last);
            m_held.remove(step.first, step.last - step.first + 1);
            endRemoveRows();
            break;
        case Move:
            beginMoveRows({}, step.from, step.from, {}, step.to);
            m_held.move(step.from, step.to);
            endMoveRows();
            break;
        case Insert:
            beginInsertRows({}, step.to, step.to);
            m_held.insert(step.to, fresh.at(step.to));
            endInsertRows();
            break;
        }
    }

    // The rows are in the right places; their contents may still have moved — a volume
    // bought, a status read — so every row is refreshed and only the changed ones repainted.
    for (int at = 0; at < int(m_held.size()) && at < fresh.size(); ++at) {
        const bool repaint = !showsTheSame(m_held.at(at), fresh.at(at));
        m_held[at] = fresh.at(at);
        if (repaint)
            emit dataChanged(index(at), index(at));
    }
}

void Shelf::ask(int page)
{
    if (!m_server) {
        // Only reachable when the `Server` singleton did not resolve, which `create` has
        // already said out loud. Said again here, on the screen, because a log line is not
        // where anybody looks at an empty shelf.
        m_trouble = Words::notSetUp(Words::Asking::Shelf);
        emit changed();
        return;
    }

    m_loading = true;
    emit changed();

    QUrlQuery query;
    query.addQueryItem(u"page"_s, QString::number(page));
    query.addQueryItem(u"size"_s, QString::number(Size));
    query.addQueryItem(u"sort"_s, m_sort);
    query.addQueryItem(u"direction"_s, sortDirection());
    if (!m_query.isEmpty())
        query.addQueryItem(u"q"_s, m_query);
    // Repeated per value, which is how the contract widens a choice: two `read=` mean
    // either, and one on each axis narrows.
    for (const QString &axis : axisNames()) {
        for (const QString &one : chosen(axis))
            query.addQueryItem(axis, one);
    }

    const int mine = m_generation;
    m_server->get(u"/series"_s, query, this, [this, mine, page](const Server::Answer &answer) {
        if (mine == m_generation) {
            took(page, answer);
        }
    });
}

void Shelf::took(int page, const Server::Answer &answer)
{
    m_loading = false;

    if (!answer.went()) {
        // What is on screen was asked for under the previous criteria, and is now shown
        // beside the reason the new ones could not be fetched. An empty grid would say the
        // library holds nothing, which is a different and untrue statement.
        m_replacing = false;
        m_trouble = answer.trouble;
        emit changed();
        return;
    }

    const Api::Read<Api::Page> read = Api::page(answer.body.object());
    if (!read.ok()) {
        m_replacing = false;
        m_trouble = read.trouble;
        emit changed();
        return;
    }

    const Api::Page &some = *read.value;
    m_total = some.total;
    m_next = page + 1;
    m_more = !some.items.isEmpty();
    m_trouble.clear();

    if (m_replacing) {
        m_replacing = false;
        replaceWith(some.items);
    } else if (!some.items.isEmpty()) {
        const auto first = int(m_held.size());
        beginInsertRows({}, first, first + int(some.items.size()) - 1);
        m_held.append(some.items);
        endInsertRows();
    }
    emit changed();
}
