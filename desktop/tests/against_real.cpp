// The client against the real server, not a pretend one.
//
// `talks_to_the_server` checks the client's behaviour against forty lines of QTcpServer: it
// proves what was imagined about the protocol. This proves the imagining was right — the
// header the server actually reads, the shape it actually answers with, the wording it
// actually puts in a refusal.
//
// Kept out of the default build, because it needs a server running. Start one on the fixture
// and point it here:
//
//     python3 ../tools/fixture.py /tmp/leaf
//
// Each line stands alone on purpose: a trailing backslash would splice the next line into
// this comment, and `-Wcomment` is fatal here. So `export`, one line at a time.
//
//     export LEAF_LIBRARY=/tmp/leaf/library LEAF_INBOX=/tmp/leaf/inbox
//     export LEAF_CACHE=/tmp/leaf/cache LEAF_DB=/tmp/leaf/data/leaf.sqlite
//     export LEAF_PORT=8600 LEAF_KEYS="desktop:8f3a92c1d4e5b6a7:read,import"
//     leaf-server serve &
//
//     cmake --build build --target against-the-real-server
//     QT_QPA_PLATFORM=offscreen build/against-the-real-server
#include "Server.h"
#include "Settings.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

class AgainstTheRealServer : public QObject
{
    Q_OBJECT
private:
    Settings *m_settings = nullptr;
    Server *m_server = nullptr;
    Server::Answer ask(const QString &path)
    {
        Server::Answer got; bool done = false;
        m_server->get(path, this, [&](const Server::Answer &a) { got = a; done = true; });
        for (int i = 0; i < 200 && !done; ++i) QTest::qWait(20);
        return got;
    }
private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        m_settings = new Settings(this);
        QSignalSpy loaded(m_settings, &Settings::changed);
        QVERIFY(loaded.wait(5000));
        m_settings->setAddress(QStringLiteral("http://127.0.0.1:8600"));
        m_settings->setKey(QStringLiteral("8f3a92c1d4e5b6a7"));
        m_server = new Server(m_settings, this);
    }
    void health()
    {
        const auto got = ask(QStringLiteral("/health"));
        QVERIFY2(got.went(), qPrintable(got.trouble));
        QCOMPARE(got.body.object().value("status").toString(), QStringLiteral("ok"));
        qInfo() << "  /health ->" << got.body.toJson(QJsonDocument::Compact).constData();
    }
    void series()
    {
        const auto got = ask(QStringLiteral("/series"));
        QVERIFY2(got.went(), qPrintable(got.trouble));
        const auto items = got.body.object().value("items").toArray();
        QVERIFY(!items.isEmpty());
        for (const auto &item : items)
            qInfo().noquote() << "  " << item.toObject().value("name").toString();
    }
    void a_wrong_key_is_refused_by_the_real_thing()
    {
        m_settings->setKey(QStringLiteral("0000000000000000"));
        const auto got = ask(QStringLiteral("/series"));
        QVERIFY(!got.went());
        QCOMPARE(got.status, 403);
        qInfo().noquote() << "  refus :" << got.trouble;
        m_settings->setKey(QStringLiteral("8f3a92c1d4e5b6a7"));
    }

    /// The real counter, not an imagined one: ten wrong keys in five minutes and the address
    /// is shut out for fifteen. Twenty attempts through the client must not reach it.
    void the_client_cannot_lock_itself_out_of_the_real_server()
    {
        m_settings->setKey(QStringLiteral("0000000000000000"));
        for (int i = 0; i < 20; ++i) {
            const auto got = ask(QStringLiteral("/series"));
            QVERIFY(!got.went());
            QVERIFY2(got.status != 429, "the server started counting — the client retried");
        }
        // The good key works straight away, which it would not if the address were blocked.
        m_settings->setKey(QStringLiteral("8f3a92c1d4e5b6a7"));
        const auto got = ask(QStringLiteral("/series"));
        QVERIFY2(got.went(), qPrintable(got.trouble));
        qInfo().noquote() << "  vingt tentatives, et la bonne clé passe encore";
    }
};
QTEST_MAIN(AgainstTheRealServer)
#include "against_real.moc"
