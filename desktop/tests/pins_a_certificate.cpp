// Which certificate this client accepts, and — the point of the file — which it refuses.
//
// Against a real TLS socket rather than a pretend reply: the whole decision is made inside
// a handshake, and the one thing worth proving is that a refused handshake ends before the
// key has been written anywhere. A server that never hears a request is that proof.
//
// The two certificates in `data/` are self-signed, `CN=leaf`, and generated once by
// `openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -days 9000 -nodes
// -subj /CN=leaf`. `other.crt` also names `IP:127.0.0.1`, so that a test can make the
// system trust it and show that being trusted is not the same as being pinned.

#include "Covers.h"
#include "Pinning.h"
#include "Server.h"
#include "Settings.h"
#include "Words.h"

#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslServer>
#include <QSslSocket>
#include <QStandardPaths>
#include <QTest>
#include <QtGlobal>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

QByteArray contents(const QString &name)
{
    QFile file(QStringLiteral(LEAF_TEST_DATA "/") + name);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

QSslCertificate certificate(const QString &name)
{
    return QSslCertificate(contents(name + u".crt"_s), QSsl::Pem);
}

/// Answers `200 {}` over TLS with the certificate it is told to present, and remembers what
/// it was asked — which is nothing at all when the client walked away from the handshake.
///
/// `keepAlive` leaves the connection open after each answer, the way a real server does.
/// Closed every time, a test cannot see a client that sends its next request down a
/// connection whose certificate was checked against the pin it had before.
class Https : public QSslServer
{
public:
    QByteArray heard;
    /// Hears and never answers while true — a request held in flight for as long as a test
    /// needs it there.
    bool silent = false;

    explicit Https(const QString &name, bool keepAlive = false)
    {
        QSslConfiguration tls = QSslConfiguration::defaultConfiguration();
        tls.setLocalCertificate(certificate(name));
        tls.setPrivateKey(QSslKey(contents(name + u".key"_s), QSsl::Ec, QSsl::Pem));
        tls.setPeerVerifyMode(QSslSocket::VerifyNone);
        setSslConfiguration(tls);
        connect(this, &QSslServer::pendingConnectionAvailable, this, [this, keepAlive] {
            while (auto *socket = qobject_cast<QSslSocket *>(nextPendingConnection())) {
                connect(socket, &QSslSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QSslSocket::readyRead, this,
                        [this, socket, keepAlive, request = QByteArray{}]() mutable {
                    const QByteArray arrived = socket->readAll();
                    heard += arrived;
                    request += arrived;
                    // Bodiless requests only — a GET is all these tests send.
                    while (!silent && request.contains("\r\n\r\n")) {
                        request.remove(0, request.indexOf("\r\n\r\n") + 4);
                        socket->write(keepAlive
                                          ? "HTTP/1.1 200 .\r\nContent-Type: application/json\r\n"
                                            "Content-Length: 2\r\n\r\n{}"
                                          : "HTTP/1.1 200 .\r\nContent-Type: application/json\r\n"
                                            "Content-Length: 2\r\nConnection: close\r\n\r\n{}");
                    }
                    socket->flush();
                    if (!keepAlive) {
                        socket->disconnectFromHost();
                    }
                });
            }
        });
    }

    /// Offers HTTP/2 first in the handshake, the way the real server's ALPN does, and still
    /// speaks only HTTP/1.1: a client that took the offer sends a preface this cannot read,
    /// and never gets an answer.
    void offerHttp2()
    {
        QSslConfiguration tls = sslConfiguration();
        tls.setAllowedNextProtocols({"h2"_ba, "http/1.1"_ba});
        setSslConfiguration(tls);
    }
};

} // namespace

class PinsACertificate : public QObject
{
    Q_OBJECT

private:
    Https *m_https = nullptr;
    Settings *m_settings = nullptr;
    Server *m_server = nullptr;
    QSslConfiguration m_system;

    /// Starts a server presenting `name`, and points the settings at it.
    void serve(const QString &name, const QString &pin, bool keepAlive = false)
    {
        m_https = new Https(name, keepAlive);
        QVERIFY(m_https->listen(QHostAddress::LocalHost));
        m_settings->setAddress(u"https://127.0.0.1:%1"_s.arg(m_https->serverPort()));
        m_settings->setFingerprint(pin);
    }

    Server::Answer ask()
    {
        Server::Answer got;
        bool done = false;
        m_server->get(u"/health"_s, this, [&](const Server::Answer &answer) {
            got = answer;
            done = true;
        });
        // Waited on here and not asserted: a `QVERIFY` cannot return an `Answer`. An answer
        // that never came reads as status 0 with no sentence, which every caller rejects.
        for (int i = 0; i < 250 && !done; ++i) {
            QTest::qWait(20);
        }
        return got;
    }

    /// Whether the server never heard a byte of a request. Given time first: a refusal
    /// asserted the instant the client gave up would not see a request still on its way.
    bool nothingHeard()
    {
        QTest::qWait(200);
        return m_https->heard.isEmpty();
    }

private slots:
    void init()
    {
        QStandardPaths::setTestModeEnabled(true);
        qunsetenv("LEAF_ADDRESS");
        qunsetenv("LEAF_KEY");
        m_system = QSslConfiguration::defaultConfiguration();
        m_settings = new Settings;
        QSignalSpy loaded(m_settings, &Settings::changed);
        QVERIFY(loaded.wait(5000));
        m_settings->setKey(u"8f3a92c1d4e5b6a7"_s);
        m_server = new Server(m_settings);
    }

    void cleanup()
    {
        QSslConfiguration::setDefaultConfiguration(m_system);
        delete m_server;
        delete m_settings;
        delete m_https;
        m_https = nullptr;
    }

    void a_fingerprint_is_the_one_a_link_carries()
    {
        // Worked out by `openssl x509 -outform DER | sha256sum` on the file in `data/`: the
        // server's `fingerprint_hex` computes the same, and a client that wrote it with
        // colons or in capitals would refuse every link it was handed.
        QCOMPARE(Pinning::fingerprint(certificate(u"pinned"_s)),
                 u"88b4244071ee0cc6f0308bef972dcbc6da3b51a2c8f6afca5d7ec87862e146c2"_s);
    }

    void a_server_presenting_the_pinned_certificate_is_answered()
    {
        serve(u"pinned"_s, Pinning::fingerprint(certificate(u"pinned"_s)));
        const Server::Answer answer = ask();
        QVERIFY2(answer.went(), qPrintable(answer.trouble));
        QCOMPARE(answer.status, 200);
        QVERIFY(m_https->heard.contains("X-Leaf-Key: 8f3a92c1d4e5b6a7"));
    }

    void a_server_presenting_another_certificate_is_refused_with_its_sentence()
    {
        serve(u"other"_s, Pinning::fingerprint(certificate(u"pinned"_s)));
        const Server::Answer answer = ask();
        QCOMPARE(answer.status, 0);
        QCOMPARE(answer.trouble, Words::certificateMismatch());
        // Refused inside the handshake, so the key never left this machine.
        QVERIFY(nothingHeard());
    }

    void a_pin_is_compared_whole_and_never_by_its_beginning()
    {
        // The pinned fingerprint with its last digit changed: a comparison that stopped early
        // — on a prefix, or on the length — would let this through.
        QString almost = Pinning::fingerprint(certificate(u"pinned"_s));
        almost.back() = almost.back() == u'0' ? u'1' : u'0';
        serve(u"pinned"_s, almost);
        QCOMPARE(ask().trouble, Words::certificateMismatch());
        QVERIFY(nothingHeard());
    }

    void the_name_in_a_pinned_certificate_does_not_matter()
    {
        // The certificate says `leaf` and is reached at 127.0.0.1: the system's own check
        // fails on the name, and the pin is what is trusted instead — the spec's choice, so
        // that Leaf's certificate never has to be regenerated when its address changes.
        const QSslCertificate pinned = certificate(u"pinned"_s);
        const QList<QSslError> system = QSslCertificate::verify({pinned}, u"127.0.0.1"_s);
        QVERIFY(std::ranges::any_of(system, [](const QSslError &error) {
            return error.error() == QSslError::HostNameMismatch;
        }));

        serve(u"pinned"_s, Pinning::fingerprint(pinned));
        QVERIFY2(ask().went(), "a pinned certificate is refused over the name it carries");
    }

    void without_a_pin_a_self_signed_server_is_refused()
    {
        serve(u"pinned"_s, QString());
        const Server::Answer answer = ask();
        QCOMPARE(answer.status, 0);
        QVERIFY(!answer.went());
        // The system's refusal, not the pin's: there was nothing pinned to mismatch.
        QVERIFY(answer.trouble != Words::certificateMismatch());
        QVERIFY(nothingHeard());
    }

    void a_server_the_system_trusts_is_still_refused_when_another_is_pinned()
    {
        // `other` is made an authority here, and names 127.0.0.1: the system finds nothing
        // wrong with it and no TLS error is ever raised. A pin checked only where the system
        // complains would let it through — and so would let a certificate bought for the
        // address stand in for the one this device was linked to.
        QSslConfiguration trusting = QSslConfiguration::defaultConfiguration();
        trusting.addCaCertificate(certificate(u"other"_s));
        QSslConfiguration::setDefaultConfiguration(trusting);

        serve(u"other"_s, QString());
        QVERIFY2(ask().went(), "the test's own premise: unpinned, the system accepts it");
        m_https->heard.clear();

        m_settings->setFingerprint(Pinning::fingerprint(certificate(u"pinned"_s)));
        QCOMPARE(ask().trouble, Words::certificateMismatch());
        QVERIFY(nothingHeard());
    }

    void a_connection_kept_open_does_not_outlive_a_change_of_pin()
    {
        // Answered unpinned is not the premise — a self-signed server is refused then — so it
        // is answered under its own pin first, over a connection the server keeps open.
        serve(u"pinned"_s, Pinning::fingerprint(certificate(u"pinned"_s)), true);
        QVERIFY(ask().went());
        m_https->heard.clear();

        // Then the pin changes. Qt keeps that connection for the next request to the same
        // host and port, and a pin checked only at the handshake would never be asked again:
        // the key would ride to a server nobody compared with it.
        m_settings->setFingerprint(Pinning::fingerprint(certificate(u"other"_s)));
        QCOMPARE(ask().trouble, Words::certificateMismatch());
        QVERIFY(nothingHeard());
    }

    void a_request_in_flight_when_the_pin_changes_is_still_answered()
    {
        // Dropping the open connections used to orphan whatever was riding them: Qt never
        // finished that reply, and a screen waited on an answer for the rest of the session.
        serve(u"pinned"_s, Pinning::fingerprint(certificate(u"pinned"_s)), true);
        m_https->silent = true;
        bool held = false;
        Server::Answer heldAnswer;
        m_server->get(u"/health"_s, this, [&](const Server::Answer &answer) {
            heldAnswer = answer;
            held = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(m_https->heard.contains("\r\n\r\n"), 5000);
        m_https->heard.clear();

        m_settings->setFingerprint(Pinning::fingerprint(certificate(u"other"_s)));
        QCOMPARE(ask().trouble, Words::certificateMismatch());
        QVERIFY(nothingHeard());
        // Never left waiting: asked again under the new pin, and refused by it like any
        // other request — not « Operation canceled », which is nothing the server said.
        QTRY_VERIFY_WITH_TIMEOUT(held, 5000);
        QCOMPARE(heldAnswer.trouble, Words::certificateMismatch());
    }

    void a_question_cancelled_by_a_change_of_pin_is_asked_again_where_it_now_points()
    {
        // Linking writes a new address and a new pin at once. A question in flight to the old
        // one is cancelled by the change — and that cancel is not an answer anybody gave: a
        // screen shown « injoignable » for it would be showing a failure that never happened.
        // So it is asked again, a turn later, of the server the settings now name.
        serve(u"pinned"_s, Pinning::fingerprint(certificate(u"pinned"_s)), true);
        m_https->silent = true;
        bool held = false;
        Server::Answer heldAnswer;
        m_server->get(u"/health"_s, this, [&](const Server::Answer &answer) {
            heldAnswer = answer;
            held = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(m_https->heard.contains("\r\n\r\n"), 5000);

        Https moved(u"other"_s, true);
        QVERIFY(moved.listen(QHostAddress::LocalHost));
        m_settings->setAddress(u"https://127.0.0.1:%1"_s.arg(moved.serverPort()));
        m_settings->setFingerprint(Pinning::fingerprint(certificate(u"other"_s)));
        QVERIFY(ask().went());

        QTRY_VERIFY_WITH_TIMEOUT(held, 5000);
        QVERIFY2(heldAnswer.went(), qPrintable(heldAnswer.trouble));
        QCOMPARE(moved.heard.count("GET /health "), 2);
    }

    void a_cover_connection_kept_open_does_not_outlive_a_change_of_pin()
    {
        serve(u"pinned"_s, Pinning::fingerprint(certificate(u"pinned"_s)), true);
        Covers covers(m_settings);
        QScopedPointer<QNetworkAccessManager> network(covers.create(nullptr));
        const QUrl cover(u"https://127.0.0.1:%1/covers/1"_s.arg(m_https->serverPort()));
        {
            QScopedPointer<QNetworkReply> reply(network->get(QNetworkRequest(cover)));
            QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 5000);
            QCOMPARE(reply->error(), QNetworkReply::NoError);
        }

        m_https->heard.clear();
        m_settings->setFingerprint(Pinning::fingerprint(certificate(u"other"_s)));
        QScopedPointer<QNetworkReply> reply(network->get(QNetworkRequest(cover)));
        QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 5000);
        QVERIFY(Pinning::mismatched(reply.data()));
        QVERIFY(nothingHeard());
    }

    void a_server_offering_http2_is_still_spoken_to_in_http1()
    {
        serve(u"pinned"_s, Pinning::fingerprint(certificate(u"pinned"_s)));
        m_https->offerHttp2();
        const Server::Answer answer = ask();
        QVERIFY2(answer.went(), qPrintable(answer.trouble));
        QCOMPARE(answer.status, 200);
        QVERIFY(m_https->heard.startsWith("GET /health HTTP/1.1\r\n"));
    }

    void a_cover_from_a_server_offering_http2_is_still_asked_in_http1()
    {
        serve(u"pinned"_s, Pinning::fingerprint(certificate(u"pinned"_s)));
        m_https->offerHttp2();
        Covers covers(m_settings);
        QScopedPointer<QNetworkAccessManager> network(covers.create(nullptr));
        const QUrl cover(u"https://127.0.0.1:%1/covers/1"_s.arg(m_https->serverPort()));
        QScopedPointer<QNetworkReply> reply(network->get(QNetworkRequest(cover)));
        QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 5000);
        QCOMPARE(reply->error(), QNetworkReply::NoError);
        QVERIFY(m_https->heard.startsWith("GET /covers/1 HTTP/1.1\r\n"));
    }

    void every_request_this_client_builds_refuses_http2()
    {
        QCOMPARE(Pinning::request(QUrl(u"https://leaf.example/"_s))
                     .attribute(QNetworkRequest::Http2AllowedAttribute)
                     .toBool(),
                 false);
        QNetworkRequest handed(QUrl(u"https://leaf.example/covers/1"_s));
        handed.setRawHeader("X-Kept", "1");
        const QNetworkRequest carried = Pinning::request(handed);
        QCOMPARE(carried.attribute(QNetworkRequest::Http2AllowedAttribute).toBool(), false);
        QCOMPARE(carried.rawHeader("X-Kept"), "1"_ba);
    }

    void covers_are_held_to_the_same_pin()
    {
        serve(u"other"_s, Pinning::fingerprint(certificate(u"other"_s)));
        Covers covers(m_settings);
        QScopedPointer<QNetworkAccessManager> network(covers.create(nullptr));
        const QUrl cover(u"https://127.0.0.1:%1/covers/1"_s.arg(m_https->serverPort()));

        {
            QScopedPointer<QNetworkReply> reply(network->get(QNetworkRequest(cover)));
            QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 5000);
            QCOMPARE(reply->error(), QNetworkReply::NoError);
            QVERIFY(!Pinning::mismatched(reply.data()));
        }

        m_https->heard.clear();
        m_settings->setFingerprint(Pinning::fingerprint(certificate(u"pinned"_s)));
        QScopedPointer<QNetworkReply> reply(network->get(QNetworkRequest(cover)));
        QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 5000);
        QVERIFY(reply->error() != QNetworkReply::NoError);
        QVERIFY(Pinning::mismatched(reply.data()));
        QVERIFY(nothingHeard());
    }
};

QTEST_MAIN(PinsACertificate)
#include "pins_a_certificate.moc"
