#pragma once

// Empties the server's disk cache of resized pages, from the library tab.
//
// It used to hang off `Scan`, which was the singleton already linked wherever that tab was
// tested, and `Scan` reached forty-nine methods holding a scan, this and the export of the
// reading — `cpp:S1448` on three chores that share a tab and nothing else: a scan is a state
// asked at intervals, this is one request and its answer. Never the library: the answer says
// how many files went, and the sentence under the button repeats it.

#include "Server.h"

#include <QObject>
#include <QQmlEngine>
#include <QString>

class CachePurge final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString title READ title CONSTANT)
    Q_PROPERTY(QString label READ label CONSTANT)
    Q_PROPERTY(QString explained READ explained CONSTANT)
    Q_PROPERTY(bool purging READ purging NOTIFY changed)
    /// What the last emptying removed, or why it did not; empty before one.
    Q_PROPERTY(QString sentence READ sentence NOTIFY changed)
    Q_PROPERTY(bool failed READ failed NOTIFY changed)

public:
    explicit CachePurge(Server *server, QObject *parent = nullptr);

    static CachePurge *create(QQmlEngine *engine, QJSEngine *);

    QString title() const;
    QString label() const;
    QString explained() const;
    bool purging() const { return m_purging; }
    QString sentence() const { return m_sentence; }
    bool failed() const { return m_failed; }

    /// Empties the cache, once at a time: a second press while the first is out would ask
    /// the same thing twice and say the second answer, « 0 fichier », over the first.
    Q_INVOKABLE void purge();

signals:
    void changed();

private:
    Server *m_server;
    bool m_purging = false;
    bool m_failed = false;
    QString m_sentence;
};
