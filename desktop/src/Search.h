#pragma once

// The matches that are not shelf tiles.
//
// /series?q= owns the grid. /search owns everything that can be opened but is not a
// series: entries and chapters, drawn as rows above that same grid. Keeping the two apart is
// what stops a volume from pretending to be a series card while still letting one field move
// both lists together.
//
// The model holds the file pages. The screen decides whether it gives them four rows in the
// overview or a whole view of their own; changing presentation must never change what the
// model claims exists.

#include "Api.h"
#include "Server.h"

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class Shelf;

class Search final : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int total READ total NOTIFY changed)
    /// Every file the search found, and not only those in hand: what a heading counts. On a
    /// server that predates the page envelope it is what came back, which is all this client
    /// can honestly claim to know.
    Q_PROPERTY(int fileTotal READ fileTotal NOTIFY changed)
    Q_PROPERTY(int remaining READ remaining NOTIFY changed)
    Q_PROPERTY(bool expanded READ expanded NOTIFY changed)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(QString trouble READ trouble NOTIFY changed)
public:
    enum class Role {
        Kind = Qt::UserRole,
        ResultId,
        Label,
        SeriesId,
        SeriesName,
        EntryId,
        Cover,
        Context,
    };
    Q_ENUM(Role)

    explicit Search(Server *server, Shelf *shelf, QObject *parent);

    static Search *create(QQmlEngine *engine, QJSEngine *);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool canFetchMore(const QModelIndex &parent) const override;
    void fetchMore(const QModelIndex &parent) override;

    int count() const { return rowCount(); }
    int total() const { return m_fileTotal; }
    int fileTotal() const { return m_fileTotal; }
    int remaining() const { return m_fileTotal > count() ? m_fileTotal - count() : 0; }
    bool expanded() const { return !canFetchMore({}); }
    bool active() const { return !m_query.isEmpty(); }
    bool loading() const { return m_loading; }
    QString trouble() const { return m_trouble; }

    /// What the answer said besides its rows: how many matches the filters are keeping off
    /// the screen, and the name the server guessed at when nothing matched exactly. One
    /// accessor and not two, because `Captions` needs both to word either.
    struct Aside {
        int outside = 0;
        QString approximate;
    };
    Aside aside() const { return {m_outside, m_approximate}; }

    /// The selection the answer in hand was fetched under, by the contract's axis names.
    /// Read from here and not from the shelf: this is what the sentence describes, and a
    /// search can run with filters before any shelf exists — which is how every test of
    /// that sentence builds it.
    QVariantMap narrowing() const { return m_narrowing; }

    /// Named after the axis it narrows, so a reader cannot mistake it for the field that
    /// asks the server a question.

    /// Public for a headless test and for a future screen with its own shelf. In the
    /// application Shelf::criteriaChanged calls this with the one canonical selection.
    Q_INVOKABLE void searchFor(const QString &query, const QStringList &readStatuses,
                               const QStringList &media);
    Q_INVOKABLE void expand();
    Q_INVOKABLE void clearFilters();

signals:
    void changed();

private:
    struct Parsed {
        QList<Api::Hit> files;
        int exact = 0;
        int total = 0;
        int fileTotal = 0;
        int received = 0;
        int page = 0;
        int size = 0;
        QString approximate;
        QString trouble;
    };

    void followShelf();
    void updateSearch(const QString &query, const QVariantMap &narrowing,
                      const QString &sort, const QString &direction);
    void ask(bool filtered, int page);
    void took(bool filtered, int page, const Server::Answer &answer);
    Parsed parse(const Server::Answer &answer) const;
    void replaceFiles(QList<Api::Hit> files);
    void appendFiles(QList<Api::Hit> files);

    Server *m_server;
    Shelf *m_shelf;
    QString m_query;
    /// The whole selection, by the contract's axis names — the shelf's own map, mirrored so
    /// a search runs inside what is showing rather than beside it.
    QVariantMap m_narrowing;
    QString m_sort = Api::spell(Api::Sort::Name);
    QString m_direction = QStringLiteral("asc");
    QList<Api::Hit> m_files;
    int m_outside = 0;
    int m_fileTotal = 0;
    int m_wireTotal = 0;
    int m_received = 0;
    int m_next = 0;
    bool m_more = false;
    bool m_loading = false;
    QString m_approximate;
    QString m_trouble;
    int m_generation = 0;
};
