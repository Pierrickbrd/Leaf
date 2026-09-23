#pragma once

// What happened to one series, told once to everything that draws it.
//
// A series marked read is on the shelf, on its own page, in the list of its editions and in
// the block of its universe, and each of those used to find out on its own — or not at all.
// The shelf asked for its first hundred series again for one tile that had changed, the page
// asked for itself and then for its siblings, and the universe never heard. Here the series is
// asked for once, and the one answer is handed to each of them; a deletion is handed on with
// no question at all, since the server has already said what is gone.
//
// **It reaches the models; they do not reach it.** They are connected to it where it is made,
// so a model built alone in a test needs nothing of it, and `take` and `forget` stay plain
// methods a test can call.

#include "Api.h"
#include "Server.h"

#include <QHash>
#include <QObject>
#include <QQmlEngine>
#include <QString>

class Library : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit Library(Server *server, QObject *parent = nullptr);

    static Library *create(QQmlEngine *engine, QJSEngine *);

    /// Something was done to this series — a mark, a volume taken off the disk. It is asked
    /// for once and everything drawing it follows. Gone by the time the answer comes, it is
    /// forgotten instead.
    Q_INVOKABLE void touched(const QString &seriesId);
    /// Taken off the disk: a whole series when `entryId` is empty, one file otherwise. A
    /// series is forgotten with no question; a file leaves its rows, and its series, which
    /// now holds one fewer, is asked for.
    Q_INVOKABLE void erased(const QString &seriesId, const QString &entryId);

signals:
    void fresh(const Api::Series &one);
    void gone(const QString &seriesId);
    void fileGone(const QString &seriesId, const QString &entryId);

private:
    Server *m_server;
    /// Per series, so that two marks in a row on one series keep only the later answer, and
    /// a mark on another does not cancel it.
    QHash<QString, int> m_asked;
};
