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
// `Erasure`.

#include "Server.h"

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QVariantMap>

class Commands : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// Something is on its way. One at a time is enough: these are gestures from a menu, and
    /// a reader does not open two menus at once.
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    /// What refused, in the server's own words. Cleared by the next command.
    Q_PROPERTY(QString trouble READ trouble NOTIFY changed)
    /// The nine entries of the two menus, by name — `markSeriesRead`, `markSeriesUnread`,
    /// `reimportSeries`, `eraseSeries`, `markEntryRead`, `markEntryUnread`, `reimportEntry`,
    /// `saveACopy`, `eraseEntry`. One map rather than nine properties, and named rather than
    /// numbered: a `.qml` reading `command(3)` is a `.qml` that breaks silently the day an
    /// entry is inserted above it.
    Q_PROPERTY(QVariantMap words READ words CONSTANT)

public:
    explicit Commands(Server *server, QObject *parent = nullptr);

    static Commands *create(QQmlEngine *engine, QJSEngine *);

    bool busy() const { return m_busy; }
    QString trouble() const { return m_trouble; }
    QVariantMap words() const;

    /// A whole edition, read or unread — one statement on the server and not one call per
    /// volume, which is what makes it a route rather than a loop here.
    Q_INVOKABLE void markSeries(const QString &seriesId, bool read);
    /// One file. The edition travels with it because what is marked is a volume *of* a
    /// series, and the screens that have to be told apart hold the series and not the file.
    Q_INVOKABLE void markEntry(const QString &entryId, const QString &seriesId, bool read);
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
    /// Written to disk, at that path.
    void saved(const QString &where);

private:
    void took(const Server::Answer &answer, const QString &seriesId, const QString &entryId);
    /// Writes the archive where the reader chose, or says why it could not. Apart from the
    /// answer it arrived in, because what to do with bytes is not what to do with a reply.
    void write(const QByteArray &archive, const QString &path);

    Server *m_server;
    QString m_trouble;
    bool m_busy = false;
    int m_generation = 0;
};
