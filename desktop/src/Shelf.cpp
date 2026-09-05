#include "Shelf.h"

#include "Words.h"

#include <QDebug>

#include <QJsonObject>
#include <QUrlQuery>

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
    switch (static_cast<Role>(role)) {
    case Role::SeriesId:
        return one.id;
    case Role::Name:
        return one.name;
    case Role::Work:
        return one.work;
    case Role::Cover:
        // Whole, and straight into an `Image`. The key it needs is put on by `Covers`, the
        // engine's network manager factory, so the route is spelled once here instead of
        // being assembled out of `Settings.address` in every `.qml` that draws a tile.
        return m_server->address() + u"/series/"_s + one.id + u"/cover"_s;
    case Role::Medium:
        // Absent stays absent. "Autre" is the answer for a word this client has not been
        // taught, not the answer for a medium nobody recorded.
        return one.medium ? Words::medium(*one.medium) : QString();
    case Role::Volumes:
        return Words::volumes(one.holding.ownedVolumes, one.medium);
    case Role::InProgress:
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
    named.insert(static_cast<int>(Role::SeriesId), "seriesId");
    named.insert(static_cast<int>(Role::Name), "name");
    named.insert(static_cast<int>(Role::Work), "work");
    named.insert(static_cast<int>(Role::Cover), "cover");
    named.insert(static_cast<int>(Role::Medium), "medium");
    named.insert(static_cast<int>(Role::Volumes), "volumes");
    named.insert(static_cast<int>(Role::InProgress), "inProgress");
    return named;
}

bool Shelf::canFetchMore(const QModelIndex &parent) const
{
    return !parent.isValid() && m_more && !m_loading && m_held.size() < m_total;
}

void Shelf::fetchMore(const QModelIndex &parent)
{
    if (canFetchMore(parent)) {
        ask(m_next);
    }
}

void Shelf::reload()
{
    ++m_generation;
    beginResetModel();
    m_held.clear();
    m_total = 0;
    m_next = 0;
    m_more = true;
    endResetModel();
    m_trouble.clear();
    ask(0);
}

void Shelf::ask(int page)
{
    if (!m_server) {
        // Only reachable when the `Server` singleton did not resolve, which `create` has
        // already said out loud. Said again here, on the screen, because a log line is not
        // where anybody looks at an empty shelf.
        m_trouble = tr("Leaf could not set itself up, so there is nothing to ask for.");
        emit changed();
        return;
    }

    m_loading = true;
    emit changed();

    QUrlQuery query;
    query.addQueryItem(u"page"_s, QString::number(page));
    query.addQueryItem(u"size"_s, QString::number(Size));

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
        m_trouble = answer.trouble;
        emit changed();
        return;
    }

    const Api::Read<Api::Page> read = Api::page(answer.body.object());
    if (!read.ok()) {
        m_trouble = read.trouble;
        emit changed();
        return;
    }

    const Api::Page &some = *read.value;
    m_total = some.total;
    m_next = page + 1;
    m_more = !some.items.isEmpty();
    m_trouble.clear();

    if (!some.items.isEmpty()) {
        const auto first = int(m_held.size());
        beginInsertRows({}, first, first + int(some.items.size()) - 1);
        m_held.append(some.items);
        endInsertRows();
    }
    emit changed();
}
