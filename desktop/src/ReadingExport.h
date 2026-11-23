#pragma once

// The calling profile's reading as one JSON file, saved where the person chose.
//
// Off `Scan` for the reason `CachePurge.h` gives: the library tab draws a scan, an emptied
// cache and this, and one class holding all three was forty-nine methods (`cpp:S1448`) for
// three requests that share a screen and no state.

#include "Server.h"

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QUrl>

class ReadingExport final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString label READ label CONSTANT)
    Q_PROPERTY(QString explained READ explained CONSTANT)
    /// What the save dialog proposes as the file's name.
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(bool exporting READ exporting NOTIFY changed)
    /// Where the file went, or why it did not; empty before one.
    Q_PROPERTY(QString sentence READ sentence NOTIFY changed)
    Q_PROPERTY(bool failed READ failed NOTIFY changed)

public:
    explicit ReadingExport(Server *server, QObject *parent = nullptr);

    static ReadingExport *create(QQmlEngine *engine, QJSEngine *);

    QString label() const;
    QString explained() const;
    QString name() const;
    bool exporting() const { return m_exporting; }
    QString sentence() const { return m_sentence; }
    bool failed() const { return m_failed; }

    /// Asks for the file and writes it whole or not at all.
    Q_INVOKABLE void save(const QUrl &where);

signals:
    void changed();

private:
    Server *m_server;
    bool m_exporting = false;
    bool m_failed = false;
    QString m_sentence;
};
