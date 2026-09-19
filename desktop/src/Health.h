#pragma once

// What the server is, and whether it answers.
//
// Asked once when the settings screen opens, and again when somebody asks. Not polled: a
// server's version does not change while you look at it, and a client that keeps asking is
// a client that keeps the disk awake for nothing.
//
// `reachable` and `configured` are not the same question, and the screen has to tell them
// apart: no address at all is a setup that was never finished, an address that does not
// answer is a server that is down or a tunnel that is closed. One is fixed by reading the
// instructions, the other by looking at the machine.

#include "Api.h"
#include "Server.h"

#include <QObject>
#include <QQmlEngine>
#include <QString>

class Health : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool asking READ asking NOTIFY changed)
    /// True once an answer came back and could be read. False before the first question,
    /// and false again after one that failed.
    Q_PROPERTY(bool reachable READ reachable NOTIFY changed)
    Q_PROPERTY(QString trouble READ trouble NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(int api READ api NOTIFY changed)
    Q_PROPERTY(int format READ format NOTIFY changed)
    /// How many series it holds. The one number that says the library is really there: a
    /// server answering perfectly over an unmounted disk answers zero.
    Q_PROPERTY(int library READ library NOTIFY changed)
    Q_PROPERTY(bool localDrop READ localDrop NOTIFY changed)

    /// The section's words. Carried by the object that owns the subject, the way `Search`
    /// carries the bar's: French lives in `Words`, and a screen never spells it.
    Q_PROPERTY(QString title READ title CONSTANT)
    Q_PROPERTY(QString answering READ answering NOTIFY changed)
    /// « Bibliothèque connectée ». What a reader needs of a server; the address and the
    /// version are a deployment's business.
    Q_PROPERTY(QString connected READ connected NOTIFY changed)
    Q_PROPERTY(QString versions READ versions NOTIFY changed)
    Q_PROPERTY(QString holds READ holds NOTIFY changed)
    Q_PROPERTY(QString sharedFolder READ sharedFolder NOTIFY changed)

public:
    explicit Health(Server *server, QObject *parent = nullptr);

    static Health *create(QQmlEngine *engine, QJSEngine *);

    bool asking() const { return m_asking; }
    bool reachable() const { return m_reachable; }
    QString trouble() const { return m_trouble; }
    QString status() const { return m_said.status; }
    int api() const { return m_said.api; }
    int format() const { return m_said.format; }
    int library() const { return m_said.library; }
    bool localDrop() const { return m_said.localDrop; }
    QString title() const;
    QString answering() const;
    QString connected() const;
    QString versions() const;
    QString holds() const;
    QString sharedFolder() const;

    Q_INVOKABLE void ask();

signals:
    void changed();

private:
    void took(const Server::Answer &answer);

    Server *m_server;
    Api::Health m_said;
    bool m_asking = false;
    bool m_reachable = false;
    QString m_trouble;
    int m_generation = 0;
};
