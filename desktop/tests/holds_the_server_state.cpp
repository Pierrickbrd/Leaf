// What the settings screen knows: what the server is, and where its scan is.
//
// Headless, against `Pretend`. Both models exist apart from any screen — a scan outlives
// the page that started it — so both are testable without one, which is the point.

#include "Health.h"
#include "Pretend.h"
#include "Scan.h"
#include "Server.h"
#include "Settings.h"
#include "Words.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

QJsonObject aReport()
{
    return QJsonObject{{u"counts"_s,
                        QJsonObject{{u"universes"_s, 1},
                                    {u"works"_s, 5},
                                    {u"editions"_s, 6},
                                    {u"entries"_s, 59},
                                    {u"chapters"_s, 546},
                                    {u"pages"_s, 0},
                                    {u"reanalysed"_s, 0}}},
                       {u"findings"_s,
                        QJsonArray{QJsonObject{{u"kind"_s, u"DISREGARDED"_s},
                                               {u"total"_s, 1},
                                               {u"items"_s, QJsonArray{u"Akira — rien"_s}}}}}};
}

QByteArray asJson(const QJsonObject &body)
{
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

} // namespace

class HoldsTheServerState : public QObject
{
    Q_OBJECT

    Pretend *m_pretend = nullptr;
    Settings *m_settings = nullptr;
    Server *m_server = nullptr;

    /// Long enough to outlast the model's own interval between two questions, because that
    /// interval is what half of these tests are about. Three hundred turns of ten
    /// milliseconds stopped exactly on it, and the wait lost the race it was watching.
    void settle(const std::function<bool()> &until)
    {
        for (int i = 0; i < 800 && !until(); ++i)
            QTest::qWait(10);
    }

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void init()
    {
        qunsetenv("LEAF_ADDRESS");
        qunsetenv("LEAF_KEY");
        m_pretend = new Pretend;
        QVERIFY(m_pretend->listen(QHostAddress::LocalHost));
        m_settings = new Settings;
        QSignalSpy loaded(m_settings, &Settings::changed);
        QVERIFY(loaded.wait(5000));
        m_settings->setAddress(
            QStringLiteral("http://127.0.0.1:%1").arg(m_pretend->serverPort()));
        m_settings->setKey(QStringLiteral("8f3a92c1d4e5b6a7"));
        m_server = new Server(m_settings);
    }

    void cleanup()
    {
        delete m_server;
        delete m_settings;
        delete m_pretend;
    }

    void missing_configuration_is_reported_without_starting_a_request()
    {
        Health health(nullptr);
        QSignalSpy healthChanged(&health, &Health::changed);
        health.ask();
        QCOMPARE(healthChanged.size(), 1);
        QVERIFY(!health.asking());
        QVERIFY(!health.reachable());
        QCOMPARE(health.trouble(), Words::notSetUp(Words::Asking::State));
        QCOMPARE(health.answering(), Words::answering(false));
        QCOMPARE(health.connected(), Words::connected(false));
        Scan scan(nullptr);
        QSignalSpy scanChanged(&scan, &Scan::changed);
        QCOMPARE(scan.stateLabel(), Words::scanState(Words::Scanning::Unknown));
        QVERIFY(scan.withoutStartPage().isEmpty());
        scan.ask();
        QCOMPARE(scanChanged.size(), 1);
        QVERIFY(!scan.asking());
        QCOMPARE(scan.trouble(), Words::notSetUp(Words::Asking::State));
        scan.start();
        QCOMPARE(scanChanged.size(), 1);
        QCOMPARE(scan.state(), Scan::State::Unknown);
        QCOMPARE(scan.title(), Words::theScan());
        QCOMPARE(scan.foundTitle(), Words::whatItFound());
        QCOMPARE(scan.startLabel(), Words::startAScan());
        QCOMPARE(scan.lastScanLabel(), Words::lastScan(0));
    }

    void malformed_health_preserves_the_previous_information()
    {
        Health health(m_server);
        m_pretend->answers(200, asJson({{u"status"_s, u"ok"_s}, {u"api"_s, 1},
                                        {u"format"_s, 1}, {u"library"_s, 6}}));
        health.ask();
        QTRY_VERIFY(health.reachable());
        m_pretend->answers(200, QByteArrayLiteral("{}"));
        health.ask();
        QVERIFY(health.asking());
        QTRY_VERIFY(!health.asking());
        QVERIFY(!health.reachable());
        QVERIFY(!health.trouble().isEmpty());
        QCOMPARE(health.library(), 6);
        QCOMPARE(health.status(), u"ok"_s);
    }

    void malformed_scan_answers_are_rejected_and_can_be_retried()
    {
        Scan scan(m_server);
        m_pretend->answers(200, QByteArrayLiteral("{}"));
        scan.ask();
        QVERIFY(scan.asking());
        QTRY_VERIFY(!scan.asking());
        QVERIFY(!scan.trouble().isEmpty());
        QCOMPARE(scan.state(), Scan::State::Unknown);
        m_pretend->answers(200, asJson({{u"state"_s, u"PAUSED"_s}}));
        scan.ask();
        QTRY_VERIFY(!scan.asking());
        QCOMPARE(scan.state(), Scan::State::Other);
        QCOMPARE(scan.stateLabel(), Words::scanState(Words::Scanning::Other));
        QVERIFY(scan.trouble().isEmpty());
    }

    void starting_twice_before_the_answer_sends_only_one_request()
    {
        Scan scan(m_server);
        m_pretend->answers(500, QByteArrayLiteral("{}"));
        scan.start();
        scan.start();
        QTRY_VERIFY(!scan.trouble().isEmpty());
        QCOMPARE(m_pretend->heard.count("POST /scan"), 1);
        m_pretend->answers(200, asJson({{u"state"_s, u"RUNNING"_s}}));
        scan.start();
        QTRY_VERIFY(scan.running());
        QCOMPARE(m_pretend->heard.count("POST /scan"), 2);
        QVERIFY(scan.trouble().isEmpty());
    }

    void what_the_server_is_is_read_and_held()
    {
        Health health(m_server);
        m_pretend->answers(200, asJson({{u"status"_s, u"ok"_s},
                                        {u"api"_s, 1},
                                        {u"format"_s, 1},
                                        {u"library"_s, 6},
                                        {u"localDrop"_s, true}}));
        health.ask();
        settle([&health] { return health.reachable(); });

        QVERIFY(health.reachable());
        QCOMPARE(health.title(), Words::theServer());
        QCOMPARE(health.status(), u"ok"_s);
        QCOMPARE(health.format(), 1);
        QCOMPARE(health.answering(), Words::answering(true));
        QCOMPARE(health.connected(), Words::connected(true));
        QCOMPARE(health.versions(), Words::apiVersion(1, 1));
        QCOMPARE(health.holds(), Words::libraryHolds(6));
        QCOMPARE(health.sharedFolder(), Words::sharedFolder(true));
        QCOMPARE(health.api(), 1);
        QCOMPARE(health.library(), 6);
        QVERIFY(health.localDrop());
        QVERIFY(health.trouble().isEmpty());
    }

    /// A server that stops answering has not changed version. Blanking what is on screen
    /// would say it had, so what was known stays known and the trouble is said beside it.
    void a_server_that_stops_answering_keeps_what_it_already_said()
    {
        Health health(m_server);
        m_pretend->answers(200, asJson({{u"status"_s, u"ok"_s},
                                        {u"api"_s, 1},
                                        {u"format"_s, 1},
                                        {u"library"_s, 6}}));
        health.ask();
        settle([&health] { return health.reachable(); });
        QCOMPARE(health.library(), 6);

        m_pretend->answers(500, QByteArrayLiteral("{}"));
        health.ask();
        settle([&health] { return !health.reachable(); });

        QVERIFY(!health.reachable());
        QVERIFY(!health.trouble().isEmpty());
        QCOMPARE(health.library(), 6);
    }

    void a_scan_that_never_ran_says_so()
    {
        Scan scan(m_server);
        m_pretend->answers(200, asJson({{u"state"_s, u"IDLE"_s}}));
        scan.ask();
        settle([&scan] { return scan.state() != Scan::State::Unknown; });

        QCOMPARE(scan.state(), Scan::State::Idle);
        QCOMPARE(scan.stateLabel(), Words::scanState(Words::Scanning::Idle));
        QVERIFY(!scan.running());
        QCOMPARE(scan.startedAt(), 0);
        QVERIFY(scan.findings().isEmpty());
        // Nothing, and not « 0 séries ». A server that has not said how many series it
        // holds has not said there are none, and the screen must not read that from
        // silence — it did, against a server that predates the structured report.
        QCOMPARE(scan.counts(), QString());
        QCOMPARE(scan.reanalysed(), QString());
        QCOMPARE(scan.failure(), QString());
    }

    /// The whole reason this model is not owned by a screen: it keeps asking while the scan
    /// runs, and says so once when it stops.
    void a_running_scan_is_followed_until_it_stops_and_then_says_so()
    {
        Scan scan(m_server);
        QSignalSpy finished(&scan, &Scan::finished);

        m_pretend->answers(200, asJson({{u"state"_s, u"RUNNING"_s},
                                        {u"startedAt"_s, 1788463365455LL}}));
        scan.ask();
        settle([&scan] { return scan.running(); });
        QVERIFY(scan.running());
        QCOMPARE(scan.stateLabel(), Words::scanState(Words::Scanning::Running));
        QCOMPARE(finished.count(), 0);

        m_pretend->answers(200, asJson({{u"state"_s, u"DONE"_s},
                                        {u"startedAt"_s, 1788463365455LL},
                                        {u"finishedAt"_s, 1788463370000LL},
                                        {u"report"_s, aReport()}}));
        // Nothing is asked again here: the model's own timer is what asks, which is what
        // makes it survive the screen that started it.
        settle([&scan] { return !scan.running(); });

        QCOMPARE(scan.state(), Scan::State::Done);
        QCOMPARE(scan.stateLabel(), Words::scanState(Words::Scanning::Done));
        QCOMPARE(scan.reanalysed(), Words::reanalysed(0));
        QCOMPARE(scan.withoutStartPage(), Words::withoutStartPage(0));
        QVERIFY(scan.failure().isEmpty());
        QCOMPARE(finished.count(), 1);
        // Counted by the server, worded here: the client says its own French, and the
        // scanner's paragraph never crosses the wire.
        QCOMPARE(scan.counts(), u"1 univers, 6 séries, 59 tomes, 546 chapitres"_s);
        QCOMPARE(scan.findings().size(), 1);
        QCOMPARE(scan.findings().constFirst().toMap().value(u"title"_s).toString(),
                 u"Lu, puis écarté"_s);
        QCOMPARE(scan.finishedAt(), 1788463370000LL);
    }

    /// And once it has stopped, it stops asking. A timer still firing against a finished
    /// scan is a client keeping a remote disk awake to learn nothing.
    void a_scan_that_is_not_running_is_not_asked_about_again()
    {
        Scan scan(m_server);
        m_pretend->answers(200, asJson({{u"state"_s, u"DONE"_s}}));
        scan.ask();
        settle([&scan] { return scan.state() == Scan::State::Done; });

        m_pretend->heard.clear();
        QTest::qWait(400);
        QCOMPARE(m_pretend->heard.count("GET /scan"), 0);
    }

    /// A server that is not answering will not answer the next one either. Asking stops
    /// rather than beating against a closed tunnel.
    void a_scan_that_cannot_be_asked_about_stops_asking()
    {
        Scan scan(m_server);
        m_pretend->answers(200, asJson({{u"state"_s, u"RUNNING"_s}}));
        scan.ask();
        settle([&scan] { return scan.running(); });

        m_pretend->answers(500, QByteArrayLiteral("{}"));
        settle([&scan] { return !scan.trouble().isEmpty(); });
        QVERIFY(!scan.trouble().isEmpty());

        m_pretend->heard.clear();
        QTest::qWait(400);
        QCOMPARE(m_pretend->heard.count("GET /scan"), 0);
    }

    void starting_a_scan_asks_the_server_to_start_one()
    {
        Scan scan(m_server);
        m_pretend->answers(200, asJson({{u"state"_s, u"RUNNING"_s}}));
        m_pretend->heard.clear();
        scan.start();
        settle([&scan] { return scan.running(); });

        QVERIFY(m_pretend->heard.contains("POST /scan"));
        QVERIFY(scan.running());
        QCOMPARE(scan.stateLabel(), Words::scanState(Words::Scanning::Running));
    }

    /// One library, one scan. The same work done twice produces a report nobody can
    /// attribute to either run.
    void a_scan_already_running_is_not_started_again()
    {
        Scan scan(m_server);
        m_pretend->answers(200, asJson({{u"state"_s, u"RUNNING"_s}}));
        scan.ask();
        settle([&scan] { return scan.running(); });

        m_pretend->heard.clear();
        scan.start();
        QTest::qWait(150);
        QCOMPARE(m_pretend->heard.count("POST /scan"), 0);
    }
};

QTEST_MAIN(HoldsTheServerState)
#include "holds_the_server_state.moc"
