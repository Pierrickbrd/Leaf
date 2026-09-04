#include "Shelf.h"

#include "Words.h"

#include <QJsonObject>
#include <QUrlQuery>

using namespace Qt::StringLiterals;

namespace {

/// Asked for explicitly although it is also the contract's default, because a page size the
/// client did not choose is a page size no test here pins, and the server is free to move its
/// own default without telling anyone.
constexpr int Size = 100;

} // namespace

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

QVariant Shelf::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_held.size()) {
        return {};
    }

    const Api::Series &one = m_held.at(index.row());
    switch (role) {
    case SeriesIdRole:
        return one.id;
    case NameRole:
        return one.name;
    case WorkRole:
        return one.work;
    case CoverRole:
        // A **path**, not something QML can hand to an `Image`: the key rides as an
        // `X-Leaf-Key` header and a plain `source:` would send none, so whatever turns this
        // into pixels supplies it. Spelled once here rather than in every `.qml` showing a
        // tile.
        return u"/series/"_s + one.id + u"/cover"_s;
    case MediumRole:
        // Absent stays absent. "Autre" is the answer for a word this client has not been
        // taught, not the answer for a medium nobody recorded.
        return one.medium ? Words::medium(*one.medium) : QString();
    case VolumesRole:
        return Words::volumes(one.holding.ownedVolumes, one.medium);
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
    return {
        {SeriesIdRole, "seriesId"}, {NameRole, "name"},     {WorkRole, "work"},
        {CoverRole, "cover"},       {MediumRole, "medium"}, {VolumesRole, "volumes"},
    };
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
        const int first = int(m_held.size());
        beginInsertRows({}, first, first + int(some.items.size()) - 1);
        m_held.append(some.items);
        endInsertRows();
    }
    emit changed();
}
