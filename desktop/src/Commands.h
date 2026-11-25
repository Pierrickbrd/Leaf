#pragma once

// What the three dots do, once something has been chosen from them.
//
// An object drawn somewhere must be commandable where it is drawn, so the same menu opens on
// a shelf tile, on the header of a series, on a line of its list and on a tile of its grid.
// One object behind all four: a command written twice is a command that will one day mean two
// different things depending on which screen asked for it.
//
// **Unread is a record forgotten, never a rewind.** `DELETE` takes the record away and the
// volume goes back to never opened. Moving it to page nought instead would leave « in
// progress, at page 0 », which is not the same fact and is not what anybody asked for.
//
// Deleting is not here: it is a question before it is a command, and that question is
// `Erasure`. Nor is what the editing windows read before they change anything, which is
// `Editing`: it only asks, and is never the command in flight.

#include "Api.h"
#include "Server.h"

#include <QObject>
#include <QQmlEngine>
#include <QSaveFile>
#include <QString>
#include <QVariantMap>

#include <memory>

class Commands : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// Something is on its way. One at a time is enough: these are gestures from a menu, and
    /// a reader does not open two menus at once.
    /// The direction the calling profile chose for the series last asked about, −1 for none.
    Q_PROPERTY(int direction READ direction NOTIFY directionChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    /// What refused, in the server's own words. Cleared by the next command.
    Q_PROPERTY(QString trouble READ trouble NOTIFY changed)
    /// The nine entries of the two menus, by name — `markSeriesRead`, `markSeriesUnread`,
    /// `reimportSeries`, `eraseSeries`, `markEntryRead`, `markEntryUnread`, `reimportEntry`,
    /// `saveACopy`, `eraseEntry`, `eraseEdition`, `editEdition`. One map rather than a dozen properties, and named rather than
    /// numbered: a `.qml` reading `command(3)` is a `.qml` that breaks silently the day an
    /// entry is inserted above it.
    Q_PROPERTY(QVariantMap words READ words CONSTANT)

public:
    explicit Commands(Server *server, QObject *parent = nullptr);

    static Commands *create(QQmlEngine *engine, QJSEngine *);

    int direction() const { return m_direction; }
    bool busy() const { return m_busy; }
    QString trouble() const { return m_trouble; }
    QVariantMap words() const;

    /// A whole edition, read or unread — one statement on the server and not one call per
    /// volume, which is what makes it a route rather than a loop here.
    Q_INVOKABLE void markSeries(const QString &seriesId, bool read);
    /// One file. The edition travels with it because what is marked is a volume *of* a
    /// series, and the screens that have to be told apart hold the series and not the file.
    /// Sends what the window changed — `fields`, an invalid value taking a field away — and
    /// says `edited` when the server kept it.
    /// A volume chosen as the series' cover travels in `fields` (`coverVolume`, `null` to take it
    /// away); `cover` is the choice of an image: `{ file }` sends the file at that URL, `{ clear: true }`
    /// takes the chosen image back, empty leaves the cover alone. After the fields, in the same
    /// call: two commands in flight would each cancel the other's answer.
    Q_INVOKABLE void editSeries(const QString &seriesId, const QVariantMap &fields,
                                const QVariantMap &cover = {});
    Q_INVOKABLE void editEntry(const QString &entryId, const QString &seriesId,
                               const QVariantMap &fields, const QVariantMap &cover = {});
    /// Reads which direction the caller chose for a series; `direction` follows.
    Q_INVOKABLE void askDirection(const QString &seriesId);
    /// Chooses the way a series is read, −1 to go back to what its files declare.
    Q_INVOKABLE void chooseDirection(const QString &seriesId, int direction);
    Q_INVOKABLE void markEntry(const QString &entryId, const QString &seriesId, bool read);
    /// Several at once: the editions `ids` when `seriesId` is empty, else volumes of that
    /// edition. One request each, the routes above in a loop — a route for a list would be
    /// one more thing for the server to keep in step, to save requests that go out together
    /// and that a selection of a few dozen makes cheap. Says `marked` once the last has
    /// answered: once for the edition's volumes, once per edition otherwise.
    Q_INVOKABLE void markMany(const QStringList &ids, const QString &seriesId, bool read);
    /// The archive itself, written where the reader chose. A series is not a file, so this
    /// exists for a volume alone.
    Q_INVOKABLE void saveACopy(const QString &entryId, const QUrl &where);
    /// Opens the folder a saved copy landed in, which is the one thing its bubble offers.
    /// Here rather than in the `.qml` that asks: taking a folder off a path is path work,
    /// and a `.qml` doing it with `lastIndexOf` gets a Windows path wrong the day there is
    /// one.
    Q_INVOKABLE void showTheFolder(const QString &path) const;

signals:
    void changed();
    /// A reading state moved. `entryId` is empty when a whole edition was marked, and what
    /// shows either has to ask again.
    void marked(const QString &seriesId, const QString &entryId);
    void directionChanged();
    /// A series or one of its files was rewritten; `entryId` is empty for the series.
    void edited(const QString &seriesId, const QString &entryId);
    /// Written to disk, at that path.
    void saved(const QString &where);

private:
    /// The fields, then the cover; said as one `edited` when both went.
    void change(const QString &path, const QVariantMap &fields, const QVariantMap &cover,
                const QString &seriesId, const QString &entryId);
    void sendCover(const QString &path, const QVariantMap &cover,
                   const std::function<void(const Server::Answer &)> &then);
    void took(const Server::Answer &answer, const QString &seriesId, const QString &entryId);
    /// The copy being written, beside where the reader chose, until it is whole.
    std::unique_ptr<QSaveFile> m_copy;
    bool m_copyFailed = false;

    Server *m_server;
    QString m_trouble;
    bool m_busy = false;
    int m_generation = 0;
    int m_direction = -1;
    /// Counts the questions and choices about a direction, so that only the latest answer
    /// lands: two in flight come back in the network's order.
    int m_askedDirection = 0;
    void tookDirection(const Api::Series &series);
};
