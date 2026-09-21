#pragma once

// The one offer the shelf puts above its grid.
//
// `/next` returns an ordered list, but the artifact draws one full-width card rather than a
// carousel. This object therefore asks for exactly one row, keeps it, and words every string
// the QML displays. Like `Shelf`, it takes a `Server *` in tests and is a singleton only in
// the application.

#include "Api.h"
#include "Server.h"

#include <QObject>
#include <QQmlEngine>
#include <QString>

#include <optional>

class Resume : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(QString trouble READ trouble NOTIFY changed)
    Q_PROPERTY(QString seriesId READ seriesId NOTIFY changed)
    Q_PROPERTY(QString seriesName READ seriesName NOTIFY changed)
    Q_PROPERTY(QString entryId READ entryId NOTIFY changed)
    Q_PROPERTY(QString cover READ cover NOTIFY changed)
    Q_PROPERTY(QString where READ where NOTIFY changed)
    Q_PROPERTY(QString whereShort READ whereShort NOTIFY changed)
    Q_PROPERTY(QString action READ action NOTIFY changed)
    Q_PROPERTY(bool hasProgress READ hasProgress NOTIFY changed)
    Q_PROPERTY(qreal progress READ progress NOTIFY changed)

public:
    explicit Resume(Server *server, QObject *parent = nullptr);

    static Resume *create(QQmlEngine *engine, QJSEngine *);

    bool available() const { return m_card.has_value(); }
    bool loading() const { return m_loading; }
    QString trouble() const { return m_trouble; }
    QString seriesId() const;
    QString seriesName() const;
    QString entryId() const;
    QString cover() const;
    QString where() const;
    QString whereShort() const;
    QString action() const;
    bool hasProgress() const;
    qreal progress() const;

    Q_INVOKABLE void reload();

signals:
    void changed();

private:
    void took(const Server::Answer &answer);

    Server *m_server;
    std::optional<Api::UpNext> m_card;
    bool m_loading = false;
    QString m_trouble;
    int m_generation = 0;
};
