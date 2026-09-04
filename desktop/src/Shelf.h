#pragma once

// The shelf: what this library holds, kept for a grid to draw.
//
// The first model this client has. `Server` knows how to ask, `Api` knows how to read the
// answer, and until now nothing kept one — so every pixel of the shelf would have been
// written against nothing. A `GridView` binds to a model or to no shelf at all.
//
// **It words its own rows.** `Api::Series` carries numbers and enumerations and says so:
// "whether it reads 21 tomes or 7 albums is the shelf's business". So a role hands QML a
// sentence out of `Words`, never a value to switch on, and no `.qml` file spells a French
// string or a medium's name.
//
// **A `Server *` through the constructor**, the way `Server` takes a `Settings *`: one
// instance in the application, and a test builds its own against a server of forty lines.
// Nothing here needs a window.
//
// One role is deliberately missing. A tile will want to show whether a series has been read,
// and `Words::readStatus` says « Terminées » — a filter pill's plural, wrong on a single
// tile. The role arrives when `Words` has the singular, rather than now with bad French or
// with a raw enumeration for QML to switch on.

#include "Api.h"
#include "Server.h"

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QString>

class Shelf : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int total READ total NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(QString trouble READ trouble NOTIFY changed)

public:
    /// What a tile shows, one role each. `Q_ENUM` so a test names them rather than counting
    /// from `Qt::UserRole` and hoping.
    enum Role {
        SeriesIdRole = Qt::UserRole,
        NameRole,
        WorkRole,
        CoverRole,
        MediumRole,
        VolumesRole,
    };
    Q_ENUM(Role)

    explicit Shelf(Server *server, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// Qt's own paging hooks rather than a `loadNextPage()` of this client's invention:
    /// `libQt6QmlModels` carries undefined references to both `QAbstractItemModel::fetchMore`
    /// and `::canFetchMore`, so the delegate model behind every `GridView` calls them itself
    /// as the view runs past the rows it has. A method of our own would be a second way to
    /// say the same thing, and QML would have to remember which one.
    ///
    /// What they cannot do is start. A view showing nothing asks for nothing, so the first
    /// page is `reload`'s job and never theirs.
    bool canFetchMore(const QModelIndex &parent) const override;
    void fetchMore(const QModelIndex &parent) override;

    int count() const { return int(m_held.size()); }
    /// How many there are behind the current answer, which is not how many are held.
    int total() const { return m_total; }
    bool loading() const { return m_loading; }
    /// Empty while nothing is wrong. The only thing a screen should ever show about a shelf
    /// that did not fill.
    QString trouble() const { return m_trouble; }

    /// Forget everything and ask again from the first page. Also the retry after a refusal,
    /// and later what a changed filter does.
    Q_INVOKABLE void reload();

signals:
    /// One signal for the four properties above. It fires on every transition of every one of
    /// them — a `NOTIFY` that only fires sometimes is a binding that is sometimes wrong.
    void changed();

private:
    void ask(int page);
    void took(int page, const Server::Answer &answer);

    Server *m_server;
    QList<Api::Series> m_held;
    int m_total = 0;
    /// The page to ask for next, counted here rather than read from the answer's echo: a
    /// server repeating the page it was given would otherwise fetch the same one for ever.
    int m_next = 0;
    /// Cleared by a page that arrives empty. A `total` disagreeing with what actually comes
    /// back is the shape of an endless loop — the view asks, nothing arrives, the count still
    /// falls short, the view asks again.
    bool m_more = true;
    bool m_loading = false;
    QString m_trouble;
    /// Which shelf an answer belongs to. `Server` has no cancel, so a request already out
    /// arrives whatever happens next; without this a `reload` during one would see the old
    /// page land on top of the new. Bumped by every `reload`, and an answer carrying a stale
    /// number is dropped unread.
    int m_generation = 0;
};
