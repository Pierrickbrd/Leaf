// What the client sends, and what it makes of what comes back.
//
// Against a server of forty lines rather than the real one: what is being checked is the
// client's own behaviour — that the key rides on every request, and that each way a request
// can fail becomes a sentence rather than a number.

#include "Server.h"
#include "Settings.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUrlQuery>

/// Answers once, with whatever it was told to, and remembers what it was asked.
class Pretend : public QTcpServer
{
    Q_OBJECT

public:
    QByteArray answer;
    QByteArray heard;

    /// Built rather than typed, because a hand-counted Content-Length is a way to fail a
    /// test for a reason that has nothing to do with what it is testing.
    void answers(int status, const QByteArray &body, const QByteArray &extra = {})
    {
        answer = "HTTP/1.1 " + QByteArray::number(status) + " .\r\n"
                 "Content-Type: application/json\r\n" + extra
                 + "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
    }

    void incomingConnection(qintptr handle) override
    {
        auto *socket = new QTcpSocket(this);
        socket->setSocketDescriptor(handle);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
            heard += socket->readAll();
            if (!heard.contains("\r\n\r\n")) {
                return;
            }
            socket->write(answer);
            socket->flush();
            socket->disconnectFromHost();
        });
    }
};

class TalksToTheServer : public QObject
{
    Q_OBJECT

private:
    Pretend *m_pretend = nullptr;
    Settings *m_settings = nullptr;
    Server *m_server = nullptr;

    Server::Answer ask(const QString &path = QStringLiteral("/health"),
                       const QUrlQuery &query = {})
    {
        Server::Answer got;
        bool done = false;
        m_server->get(path, query, [&](const Server::Answer &answer) {
            got = answer;
            done = true;
        });
        QTest::qWait(50);
        for (int i = 0; i < 100 && !done; ++i) {
            QTest::qWait(20);
        }
        return got;
    }

private slots:
    void init()
    {
        QStandardPaths::setTestModeEnabled(true);
        m_pretend = new Pretend;
        QVERIFY(m_pretend->listen(QHostAddress::LocalHost));
        m_settings = new Settings;
        // Wait for the keyring before anything else touches it. The application does the
        // same — `loaded` exists for exactly this.
        QSignalSpy loaded(m_settings, &Settings::changed);
        QVERIFY(loaded.wait(5000));
        m_settings->setAddress(
            QStringLiteral("http://127.0.0.1:%1").arg(m_pretend->serverPort()));
        m_settings->setKey(QStringLiteral("8f3a92c1d4e5b6a7"));
        m_server = new Server(m_settings);
        m_pretend->answers(200, "{}");
    }

    void cleanup()
    {
        delete m_server;
        delete m_settings;
        delete m_pretend;
    }

    void the_key_rides_on_every_request()
    {
        const auto got = ask(QStringLiteral("/series"));
        QVERIFY2(got.went(), qPrintable(got.trouble));
        QVERIFY2(m_pretend->heard.contains("X-Leaf-Key: 8f3a92c1d4e5b6a7"),
                 m_pretend->heard.constData());
        QVERIFY2(m_pretend->heard.startsWith("GET /series "), m_pretend->heard.constData());
    }

    /// A search term is the one thing a person types, so it is the one place every byte
    /// question meets at once: a letter Latin-1 has no room for, a curly apostrophe, an
    /// ampersand that cuts a query in half, and a space.
    void a_search_term_reaches_the_server_byte_for_byte()
    {
        const QString typed = QString::fromUtf8("Haikyū !! & l\u2019été");
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("q"), typed);

        const auto got = ask(QStringLiteral("/search"), query);
        QVERIFY2(got.went(), qPrintable(got.trouble));

        // Read back off the wire the way the server will read it, rather than by eye.
        const QByteArray line = m_pretend->heard.split(' ').value(1);
        const QUrl asSent = QUrl::fromEncoded(line);
        QCOMPARE(asSent.path(), QStringLiteral("/search"));
        QCOMPARE(QUrlQuery(asSent).queryItemValue(QStringLiteral("q"), QUrl::FullyDecoded),
                 typed);

        // And the bytes on the wire are UTF-8 percent-encoding, not Latin-1's.
        QVERIFY2(line.contains("Haiky%C5%AB"), line.constData());
        QVERIFY2(line.contains("%26"), line.constData());
    }

    /// The shape that cannot be encoded correctly is refused rather than sent broken.
    void a_query_spliced_into_the_path_is_refused()
    {
        const auto got = ask(QStringLiteral("/search?q=Berserk"));
        QVERIFY(!got.went());
        QVERIFY2(got.trouble.contains(QStringLiteral("apart from the path")),
                 qPrintable(got.trouble));
        QVERIFY2(m_pretend->heard.isEmpty(), m_pretend->heard.constData());
    }

    void what_comes_back_is_read_as_json()
    {
        m_pretend->answers(200, "{\"status\":\"ok\"}");
        const auto got = ask();
        QVERIFY2(got.went(), qPrintable(got.trouble));
        QCOMPARE(got.body.object().value(QStringLiteral("status")).toString(),
                 QStringLiteral("ok"));
    }

    void a_refused_key_says_what_the_server_said_about_it()
    {
        // The server knows which of the three reasons a 403 had, and its wording is better
        // than anything invented here.
        m_pretend->answers(403, "{\"error\":\"this key does not carry the import right\"}");
        const auto got = ask();
        QVERIFY(!got.went());
        QCOMPARE(got.status, 403);
        QVERIFY2(got.trouble.contains(QStringLiteral("import")), qPrintable(got.trouble));
    }

    void being_throttled_says_how_long_to_wait()
    {
        m_pretend->answers(429, {}, "Retry-After: 900\r\n");
        const auto got = ask();
        QVERIFY(!got.went());
        QVERIFY2(got.trouble.contains(QStringLiteral("900")), qPrintable(got.trouble));
    }

    void a_server_that_is_not_there_says_so_rather_than_nothing()
    {
        m_settings->setAddress(QStringLiteral("http://127.0.0.1:9"));
        const auto got = ask();
        QVERIFY(!got.went());
        QCOMPARE(got.status, 0);
        QVERIFY2(got.trouble.contains(QStringLiteral("could not be reached")),
                 qPrintable(got.trouble));
    }

    void nonsense_in_place_of_json_is_not_taken_for_an_answer()
    {
        m_pretend->answers(200, "not json at all");
        const auto got = ask();
        QVERIFY(!got.went());
        QVERIFY2(got.trouble.contains(QStringLiteral("cannot read")), qPrintable(got.trouble));
    }

    void an_address_typed_by_hand_is_filed_down_rather_than_refused()
    {
        // Nobody should be corrected for typing what they mean.
        QCOMPARE(Server::tidy(QStringLiteral("  leaf.local:8081/  ")),
                 QStringLiteral("https://leaf.local:8081"));
        QCOMPARE(Server::tidy(QStringLiteral("http://leaf.local:8081///")),
                 QStringLiteral("http://leaf.local:8081"));
        // A key travels on every request, so a bare name gets https and not http.
        QVERIFY(Server::tidy(QStringLiteral("leaf.local")).startsWith(QStringLiteral("https://")));
        QCOMPARE(Server::tidy(QStringLiteral("   ")), QString());
    }

    /// A refused key stops the client, and the server's own counter is why.
    ///
    /// Ten wrong keys in five minutes and the address is shut out for a quarter of an hour —
    /// not just for this application, for everything answering from it. A client that
    /// retried a 403 would do that to itself in a second, and a generic "try again" is
    /// exactly how it would happen.
    void a_refused_key_stops_the_client_rather_than_being_retried()
    {
        m_pretend->answers(403, "{\"error\":\"unknown key\"}");
        const auto first = ask();
        QVERIFY(!first.went());
        QVERIFY(m_server->stopped());

        const QByteArray afterOne = m_pretend->heard;
        for (int i = 0; i < 10; ++i) {
            const auto again = ask();
            QVERIFY(!again.went());
            QVERIFY2(again.trouble.contains(QStringLiteral("unknown key")),
                     qPrintable(again.trouble));
        }
        QCOMPARE(m_pretend->heard, afterOne);
        QVERIFY2(m_pretend->heard.count("GET ") == 1, m_pretend->heard.constData());
    }

    void a_key_that_changes_is_the_one_reason_to_try_again()
    {
        m_pretend->answers(403, "{\"error\":\"unknown key\"}");
        ask();
        QVERIFY(m_server->stopped());

        // Somebody edited the file, or the environment, and the application read it again.
        m_settings->setKey(QStringLiteral("1111111111111111"));
        QVERIFY(!m_server->stopped());
        m_pretend->answers(200, "{}");
        QVERIFY(ask().went());
    }

    void being_throttled_holds_off_until_it_is_allowed_again()
    {
        m_pretend->answers(429, {}, "Retry-After: 900\r\n");
        QVERIFY(!ask().went());

        const QByteArray afterOne = m_pretend->heard;
        const auto again = ask();
        QVERIFY(!again.went());
        QVERIFY2(again.trouble.contains(QStringLiteral("more seconds")),
                 qPrintable(again.trouble));
        QCOMPARE(m_pretend->heard, afterOne);
    }

    void with_no_address_at_all_nothing_is_attempted()
    {
        m_settings->setAddress(QString());
        const auto got = ask();
        QVERIFY(!got.went());
        // The words come from Settings, which knows where the address is meant to go.
        QVERIFY2(got.trouble.contains(QStringLiteral("LEAF_ADDRESS")), qPrintable(got.trouble));
    }
};

QTEST_MAIN(TalksToTheServer)
#include "talks_to_the_server.moc"
