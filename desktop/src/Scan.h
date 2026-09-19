#pragma once

// Where the scan is, what the last one found, and how to start one.
//
// It lives beside the other singletons rather than inside the screen that shows it, because
// a scan outlives the screen: it runs on the server, and leaving the page does not stop it.
// A model owned by the view would stop asking the moment you went back to your shelf, and
// nothing would ever say the library had changed under you.
//
// **Asked at intervals only while it runs.** A scan takes minutes; asking every half second
// would keep a remote server's disk awake to learn nothing. Asking stops the moment the
// state comes back anything but RUNNING.

#include "Api.h"
#include "Server.h"

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QVariantList>

#include <optional>
#include <QTimer>

class Scan : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(State state READ state NOTIFY changed)
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(bool asking READ asking NOTIFY changed)
    Q_PROPERTY(QString trouble READ trouble NOTIFY changed)
    /// Milliseconds since the epoch, or zero when no scan has ever run. Worded by the
    /// screen, because a date is French and French lives in `Words`.
    Q_PROPERTY(qint64 startedAt READ startedAt NOTIFY changed)
    Q_PROPERTY(qint64 finishedAt READ finishedAt NOTIFY changed)
    /// What the last scan counted, worded here. « 6 séries, 59 tomes, 546 chapitres ».
    Q_PROPERTY(QString counts READ counts NOTIFY changed)
    Q_PROPERTY(QString reanalysed READ reanalysed NOTIFY changed)
    /// One entry per kind of thing found: `{ title, total, items, more }`. The scanner
    /// reports rather than guesses, and until this screen nothing could read what it said.
    Q_PROPERTY(QVariantList findings READ findings NOTIFY changed)
    Q_PROPERTY(QString withoutStartPage READ withoutStartPage NOTIFY changed)
    /// Set when the scan itself failed, and empty otherwise.
    Q_PROPERTY(QString failure READ failure NOTIFY changed)

    Q_PROPERTY(QString title READ title CONSTANT)
    Q_PROPERTY(QString foundTitle READ foundTitle CONSTANT)
    Q_PROPERTY(QString stateLabel READ stateLabel NOTIFY changed)
    Q_PROPERTY(QString lastScanLabel READ lastScanLabel NOTIFY changed)
    Q_PROPERTY(QString startLabel READ startLabel CONSTANT)

public:
    /// The client's own word for it, so QML never compares strings from the wire. `Other`
    /// is a state this client has not been taught — the screen can still say when the last
    /// scan ran.
    enum class State { Unknown, Idle, Running, Done, Other };
    Q_ENUM(State)

    explicit Scan(Server *server, QObject *parent = nullptr);

    static Scan *create(QQmlEngine *engine, QJSEngine *);

    State state() const { return m_state; }
    bool running() const { return m_state == State::Running; }
    bool asking() const { return m_asking; }
    QString trouble() const { return m_trouble; }
    qint64 startedAt() const { return m_startedAt; }
    qint64 finishedAt() const { return m_finishedAt; }
    QString counts() const;
    QString reanalysed() const;
    QVariantList findings() const;
    QString withoutStartPage() const;
    QString failure() const;
    QString title() const;
    QString foundTitle() const;
    QString stateLabel() const;
    QString lastScanLabel() const;
    QString startLabel() const;

    Q_INVOKABLE void ask();
    /// Starts one, unless one is already running: two scans over one library is work done
    /// twice and a report nobody can attribute.
    Q_INVOKABLE void start();

signals:
    void changed();
    /// A scan that was running has stopped. What the shelf listens to: the library has
    /// moved under it, and what it is showing answered the old one.
    void finished();

private:
    /// Long enough that a remote disk is left alone, short enough that nobody wonders.
    static constexpr int Between = 3000;

    void took(const Server::Answer &answer);

    Server *m_server;
    QTimer m_again;
    State m_state = State::Unknown;
    bool m_asking = false;
    bool m_starting = false;
    QString m_trouble;
    qint64 m_startedAt = 0;
    qint64 m_finishedAt = 0;
    /// Absent until a scan has reported. Not a report of zeros: a server that has not
    /// said how many series it holds has not said there are none, and « 0 séries » is a
    /// sentence nobody should read from silence.
    std::optional<Api::ScanFindings> m_found;
    int m_generation = 0;
};
