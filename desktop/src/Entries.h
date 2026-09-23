#pragma once

// The files of one edition, as the volumes tab draws them.
//
// **It marries two answers.** `Entry` carries no reading state at all — the contract is plain
// about it — and `/series/{id}/progress` holds « one record per entry that has been opened ».
// So a line is an entry and the record that may or may not exist beside it, joined here: one
// request for thirty volumes, not thirty.
//
// **The missing volumes are rows too**, and they come from neither answer: they are gaps
// `holding.missingVolumes` reports, and the page hands them over because it already knows
// them. A gap has no file, so it has no identifier, no pages and no menu — the list draws it
// and nothing can be done to it.
//
// **Nothing is re-sorted.** The server answers « volumes and standalone chapters, mixed, in
// reading order »; sorting by file name is how « Tome 10 » lands before « Tome 2 ».

#include "Api.h"
#include "Server.h"

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QQmlEngine>
#include <QString>
#include <QVariantList>

class Entries final : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(QString trouble READ trouble NOTIFY changed)
    /// What is being looked for inside the list, or nothing. Local: the whole list is already
    /// in hand — `/series/{id}/entries` takes no parameter — so the answer arrives before the
    /// next keystroke.
    Q_PROPERTY(QString query READ query NOTIFY changed)
    /// The edition this list is showing, so the screen can tell whether it is already the one
    /// asked for. Without it the shell would re-point the list on every change of the page —
    /// including the one its own answer caused.
    Q_PROPERTY(QString pointedAt READ pointedAt NOTIFY changed)
    /// True when a search emptied the list, which is not the same as a series with no files.
    Q_PROPERTY(bool narrowedToNothing READ narrowedToNothing NOTIFY changed)

public:
    /// What a line shows. `State` and not three booleans: a line draws one mark, and three
    /// flags is three ways for a delegate to draw two.
    enum class State { NeverRead, InProgress, Read, Missing };
    Q_ENUM(State)

    enum class Role {
        EntryId = Qt::UserRole,
        Number,
        Title,
        Pages,
        Weight,
        State_,
        HowFarRead,
        TimesFinished,
    };
    Q_ENUM(Role)

    explicit Entries(Server *server, QObject *parent = nullptr);

    static Entries *create(QQmlEngine *engine, QJSEngine *);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_shown.size()); }
    bool loading() const { return m_loading; }
    QString trouble() const { return m_trouble; }
    QString query() const { return m_query; }
    QString pointedAt() const { return m_id; }
    bool narrowedToNothing() const { return m_shown.isEmpty() && !m_query.isEmpty(); }

    /// Points the list at an edition, with the gaps the page already knows about. Keeps what
    /// it holds until the answer arrives, like everything else on this screen.
    Q_INVOKABLE void point(const QString &seriesId, const QVariantList &missing = {});
    Q_INVOKABLE void reload();

    /// Narrows what is shown to the lines whose number or title match. Blank is not a search:
    /// a cleared field is the whole list back, the way a cleared chip is the whole shelf.
    Q_INVOKABLE void searchFor(const QString &query);

signals:
    void changed();

private:
    /// One line: a file, or a hole where one is not.
    struct Line {
        Api::Entry file;
        std::optional<Api::Progress> read;
        /// Set on a gap, which has no file behind it.
        std::optional<double> missingNumber;
    };

    void tookEntries(const Server::Answer &answer);
    void tookProgress(const Server::Answer &answer);
    void rebuild();

    Server *m_server;
    QString m_id;
    QList<double> m_missing;
    QList<Line> m_all;
    QList<Line> m_shown;
    QString m_query;
    bool m_loading = false;
    QString m_trouble;
    int m_generation = 0;
};
