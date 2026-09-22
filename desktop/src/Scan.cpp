#include "Scan.h"

#include "Words.h"

#include <QDebug>

using namespace Qt::StringLiterals;

Scan *Scan::create(QQmlEngine *engine, QJSEngine *)
{
    auto *server = engine->singletonInstance<Server *>(qmlTypeId("Leaf", 1, 0, "Server"));
    if (!server) {
        qWarning().noquote()
            << QStringLiteral("error resolving the Server singleton — no scan can be "
                              "started or followed");
    }
    return new Scan(server);
}

Scan::Scan(Server *server, QObject *parent)
    : QObject(parent)
    , m_server(server)
{
    m_again.setSingleShot(true);
    connect(&m_again, &QTimer::timeout, this, &Scan::ask);
}

QString Scan::title() const
{
    return Words::theScan();
}

QString Scan::foundTitle() const
{
    return Words::whatItFound();
}

QString Scan::stateLabel() const
{
    switch (m_state) {
    case State::Idle:
        return Words::scanState(Words::Scanning::Idle);
    case State::Running:
        return Words::scanState(Words::Scanning::Running);
    case State::Done:
        return Words::scanState(Words::Scanning::Done);
    case State::Other:
        return Words::scanState(Words::Scanning::Other);
    case State::Unknown:
        break;
    }
    return Words::scanState(Words::Scanning::Unknown);
}

QString Scan::lastScanLabel() const
{
    return Words::lastScan(m_finishedAt);
}

QString Scan::startLabel() const
{
    return Words::startAScan();
}

QString Scan::counts() const
{
    return m_found ? Words::scanCounts(m_found->counts) : QString();
}

QString Scan::reanalysed() const
{
    return m_found ? Words::reanalysed(m_found->counts.reanalysed) : QString();
}

QString Scan::placesCarried() const
{
    return m_found ? Words::placesCarried(m_found->counts.progressCarried,
                                          m_found->counts.progressLost)
                   : QString();
}

QString Scan::withoutStartPage() const
{
    return m_found ? Words::withoutStartPage(m_found->chaptersWithoutStartPage) : QString();
}

QString Scan::failure() const
{
    return m_found ? m_found->failure : QString();
}

QVariantList Scan::findings() const
{
    QVariantList all;
    if (!m_found)
        return all;
    for (const Api::Finding &one : m_found->findings) {
        // A kind this client has no word for keeps its items and loses only its heading:
        // the scan found something either way, and hiding it would be the worse answer.
        all << QVariantMap{
            {u"title"_s, Words::finding(one.kind)},
            {u"total"_s, one.total},
            {u"items"_s, QVariant::fromValue(one.items)},
            {u"more"_s, Words::andMore(one.total - int(one.items.size()))},
        };
    }
    return all;
}

void Scan::ask()
{
    ++m_generation;
    if (!m_server) {
        m_trouble = Words::notSetUp(Words::Asking::State);
        emit changed();
        return;
    }

    m_asking = true;
    emit changed();

    const int mine = m_generation;
    m_server->get(u"/scan"_s, this, [this, mine](const Server::Answer &answer) {
        if (mine == m_generation)
            took(answer);
    });
}

void Scan::start()
{
    // Two scans over one library is the same work done twice and a report nobody can
    // attribute to either. The button is not the only way in — the server may be scanning
    // because somebody asked it elsewhere.
    if (m_starting || running() || !m_server)
        return;

    m_starting = true;
    emit changed();
    m_server->post(u"/scan"_s, {}, this, [this](const Server::Answer &answer) {
        m_starting = false;
        // The answer to a start is the state it left the scan in, read like any other.
        took(answer);
    });
}

void Scan::took(const Server::Answer &answer)
{
    m_asking = false;

    if (!answer.went()) {
        // Asking stops. A server that is not answering will not answer the next one either,
        // and a timer that keeps firing against a closed tunnel is a client nobody can put
        // down.
        m_again.stop();
        m_trouble = answer.trouble;
        emit changed();
        return;
    }

    const Api::Read<Api::ScanStatus> read = Api::scanStatus(answer.body.object());
    if (!read.ok()) {
        m_again.stop();
        m_trouble = read.trouble;
        emit changed();
        return;
    }

    const bool wasRunning = running();
    using enum Api::ScanStatus::State;
    switch (read.value->state) {
    case Idle:
        m_state = State::Idle;
        break;
    case Running:
        m_state = State::Running;
        break;
    case Done:
        m_state = State::Done;
        break;
    case Other:
        m_state = State::Other;
        break;
    }
    m_startedAt = read.value->startedAt.value_or(0);
    m_finishedAt = read.value->finishedAt.value_or(0);
    m_found = read.value->report;
    m_trouble.clear();

    // Only while it runs. Every other state is a state that will not change on its own.
    if (running())
        m_again.start(Between);
    else
        m_again.stop();

    emit changed();
    if (wasRunning && !running())
        emit finished();
}
