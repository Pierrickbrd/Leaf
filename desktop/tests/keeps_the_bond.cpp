// The link to a library, once there is one: renamed, moved to another address, undone — and
// undone by the server, which is the case nobody on this device asked for.
//
// Against real sockets, for the reason `links_a_device.cpp` gives: what has to be proved is
// what a server *heard*. A move that sent the key to a server presenting another certificate
// has already lost it, whatever the screen said afterwards.
//
// The library listens on 127.0.0.2, in TLS, under the `pinned` certificate this device was
// linked to — see `links_a_device.cpp` for why that address and not 127.0.0.1. The one case
// where the environment sets the address uses plain http on 127.0.0.1 instead: an
// environment-configured client carries no pin, and a certificate nobody signed would never
// get as far as the refusal under test.

#include "Bond.h"
#include "Link.h"
#include "Linking.h"
#include "Pinning.h"
#include "Pretend.h"
#include "QrImage.h"
#include "Server.h"
#include "Settings.h"
#include "Words.h"

#include <QDir>
#include <QEventLoop>
#include <QDateTime>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPointer>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslServer>
#include <QSslSocket>
#include <QStandardPaths>
#include <QFileInfo>
#include <QTest>

#include <qrcodegen.hpp>
#include <qt6keychain/keychain.h>

#include <functional>
#include <utility>

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

QString pinOf(const QString &name)
{
    return Pinning::fingerprint(certificate(name));
}

QByteArray reply(int status, const QByteArray &body)
{
    return "HTTP/1.1 " + QByteArray::number(status) + " .\r\nContent-Type: application/json\r\n"
           + "Content-Length: " + QByteArray::number(body.size())
           + "\r\nConnection: close\r\n\r\n" + body;
}

const QByteArray Unknown = R"({"error":"unknown key"})";
const QByteArray NoSeries = R"({"items":[],"total":0})";
const QString Key = u"Zm9yLXRoZS1kZXZpY2Utb25seS0wMTIzNDU2Nzg5YWJjZGVm"_s;
const QByteArray Withdrawn = "HTTP/1.1 204 .\r\nConnection: close\r\n\r\n";

/// What `POST /link/codes` answers: a code living `lifetime` milliseconds from now.
QByteArray issued(qint64 lifetime = 600000)
{
    const qint64 expiresAt = QDateTime::currentMSecsSinceEpoch() + lifetime;
    return reply(201, R"({"id":"c-1","code":"K7QF-2MXP","expiresAt":)"
                          + QByteArray::number(expiresAt) + "}");
}

/// A Leaf server over TLS, as far as these tests need one: answers what `answerFor` says for
/// each whole request, bodies included, and remembers everything it heard. An empty answer
/// holds the request without replying — a command kept in flight.
class Https : public QSslServer
{
public:
    QByteArray heard;
    std::function<QByteArray(const QByteArray &)> answerFor = [](const QByteArray &) {
        return QByteArray();
    };

    explicit Https(const QString &name)
    {
        QSslConfiguration tls = QSslConfiguration::defaultConfiguration();
        tls.setLocalCertificate(certificate(name));
        tls.setPrivateKey(QSslKey(contents(name + u".key"_s), QSsl::Ec, QSsl::Pem));
        tls.setPeerVerifyMode(QSslSocket::VerifyNone);
        setSslConfiguration(tls);
        connect(this, &QSslServer::pendingConnectionAvailable, this, [this] {
            while (auto *socket = qobject_cast<QSslSocket *>(nextPendingConnection())) {
                connect(socket, &QSslSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QSslSocket::readyRead, this,
                        [this, socket, request = QByteArray{}]() mutable {
                    const QByteArray arrived = socket->readAll();
                    heard += arrived;
                    request += arrived;
                    const qsizetype end = request.indexOf("\r\n\r\n");
                    if (end < 0) {
                        return;
                    }
                    qsizetype length = 0;
                    for (const QByteArray &line : request.left(end).split('\n')) {
                        if (line.trimmed().toLower().startsWith("content-length:")) {
                            length = line.mid(line.indexOf(':') + 1).trimmed().toLongLong();
                        }
                    }
                    if (request.size() < end + 4 + length) {
                        return;
                    }
                    const QByteArray answer = answerFor(request);
                    request.clear();
                    if (answer.isEmpty()) {
                        held.append(socket);
                        return;
                    }
                    socket->write(answer);
                    socket->flush();
                    socket->disconnectFromHost();
                });
            }
        });
    }

    QString address() const { return u"https://127.0.0.2:%1"_s.arg(serverPort()); }

    /// Answers, late, every request held so far.
    void release(const QByteArray &answer)
    {
        const QList<QPointer<QSslSocket>> waiting = std::exchange(held, {});
        for (const QPointer<QSslSocket> &socket : waiting) {
            if (socket) {
                socket->write(answer);
                socket->flush();
                socket->disconnectFromHost();
            }
        }
    }

private:
    QList<QPointer<QSslSocket>> held;
};

} // namespace

class KeepsTheBond : public QObject
{
    Q_OBJECT

private:
    Https *m_library = nullptr;
    Settings *m_settings = nullptr;
    Server *m_server = nullptr;
    Linking *m_linking = nullptr;
    Bond *m_bond = nullptr;

    static void forgetTheKeyring()
    {
        // The keyring outlives the process, so a run inherits whatever the run before it left.
        QKeychain::DeletePasswordJob forget(u"Leaf-under-test"_s);
        forget.setAutoDelete(false);
        forget.setKey(u"server-key"_s);
        QEventLoop waiting;
        connect(&forget, &QKeychain::Job::finished, &waiting, &QEventLoop::quit);
        forget.start();
        waiting.exec();
    }

    static Https *serve(const QString &name)
    {
        auto *library = new Https(name);
        if (!library->listen(QHostAddress(u"127.0.0.2"_s))) {
            delete library;
            return nullptr;
        }
        return library;
    }

    /// Loaded settings, and everything this tab is built from on top of them.
    void build()
    {
        m_settings = new Settings;
        QSignalSpy loaded(m_settings, &Settings::changed);
        QVERIFY(loaded.wait(5000));
        m_server = new Server(m_settings);
        m_linking = new Linking(m_settings);
        m_bond = new Bond(m_settings, m_server, m_linking);
    }

    /// A device linked to a library presenting `pinned`, under the name « Bureau » — as the
    /// linking screen would have left it.
    void linked()
    {
        m_library = serve(u"pinned"_s);
        QVERIFY(m_library);
        build();
        m_settings->remember(QUrl(m_library->address()), pinOf(u"pinned"_s), Key, u"Bureau"_s,
                             QDateTime::currentDateTimeUtc());
        QTRY_VERIFY_WITH_TIMEOUT(m_settings->configured(), 5000);
    }

    /// The JSON body of the last `verb` request a server heard.
    static QJsonObject lastBody(const QByteArray &heard, const QByteArray &verb)
    {
        const qsizetype at = heard.lastIndexOf(verb + ' ');
        if (at < 0) {
            return {};
        }
        return QJsonDocument::fromJson(heard.mid(heard.indexOf("\r\n\r\n", at) + 4)).object();
    }

    /// Asks the server for something, the way any screen would.
    void ask(const QString &path)
    {
        m_server->get(path, this, [](const Server::Answer &) {});
    }

private slots:
    void init()
    {
        QStandardPaths::setTestModeEnabled(true);
        QDir(QFileInfo(Settings::configurationFile()).absolutePath())
            .removeRecursively();
        qunsetenv("LEAF_ADDRESS");
        qunsetenv("LEAF_KEY");
        forgetTheKeyring();
    }

    void cleanup()
    {
        delete m_bond;
        m_bond = nullptr;
        delete m_linking;
        m_linking = nullptr;
        delete m_server;
        m_server = nullptr;
        delete m_settings;
        m_settings = nullptr;
        delete m_library;
        m_library = nullptr;
        qunsetenv("LEAF_ADDRESS");
        qunsetenv("LEAF_KEY");
        forgetTheKeyring();
    }

    void renaming_stores_the_name_the_server_answered()
    {
        linked();
        m_library->answerFor = [](const QByteArray &request) {
            return request.startsWith("PATCH /device ")
                       ? reply(200, R"({"id":"d-1","name":"Liseuse"})")
                       : QByteArray();
        };
        m_bond->rename(u"  liseuse "_s);
        // The server's spelling, not the one typed: it is the name every other device sees.
        QTRY_COMPARE_WITH_TIMEOUT(m_settings->deviceName(), u"Liseuse"_s, 5000);
        QCOMPARE(lastBody(m_library->heard, "PATCH").value(u"name"_s).toString(),
                 u"liseuse"_s);
        QVERIFY(m_library->heard.contains("X-Leaf-Key: " + Key.toUtf8()));
        QVERIFY(m_bond->sentence().isEmpty());

        // Written, and not only held: the next run reads it back.
        Settings again;
        QSignalSpy loaded(&again, &Settings::changed);
        QVERIFY(loaded.wait(5000));
        QCOMPARE(again.deviceName(), u"Liseuse"_s);
    }

    void a_taken_name_leaves_the_old_one()
    {
        linked();
        m_library->answerFor = [](const QByteArray &request) {
            return request.startsWith("PATCH /device ")
                       ? reply(409, R"({"error":"name","name":"Liseuse"})")
                       : QByteArray();
        };
        m_bond->rename(u"liseuse"_s);
        QTRY_COMPARE_WITH_TIMEOUT(m_bond->sentence(), Words::nameTaken(u"Liseuse"_s), 5000);
        QCOMPARE(m_settings->deviceName(), u"Bureau"_s);
    }

    void moving_keeps_the_key_and_saves_only_once_it_is_accepted()
    {
        linked();
        Https *elsewhere = serve(u"pinned"_s);
        QVERIFY(elsewhere);
        elsewhere->answerFor = [](const QByteArray &request) {
            return request.startsWith("GET /series?limit=1 ") ? reply(200, NoSeries)
                                                                : QByteArray();
        };
        const QString before = m_settings->address();
        QSignalSpy moved(m_bond, &Bond::moved);

        m_bond->move(elsewhere->address());
        // Nothing written while the new address has not answered.
        QCOMPARE(m_settings->address(), before);

        QTRY_COMPARE_WITH_TIMEOUT(m_settings->address(), elsewhere->address(), 5000);
        QCOMPARE(moved.count(), 1);
        // The key was tried there, and it is still the same key — a key is known only to its
        // library, so finding it accepted is what says this is the same one.
        QVERIFY(elsewhere->heard.contains("X-Leaf-Key: " + Key.toUtf8()));
        QCOMPARE(m_settings->key(), Key);
        QCOMPARE(m_settings->fingerprint(), pinOf(u"pinned"_s));
        QVERIFY(m_bond->sentence().isEmpty());
        delete elsewhere;
    }

    void moving_to_a_server_that_does_not_know_the_key_saves_nothing()
    {
        linked();
        Https *elsewhere = serve(u"pinned"_s);
        QVERIFY(elsewhere);
        elsewhere->answerFor = [](const QByteArray &) { return reply(403, Unknown); };
        const QString before = m_settings->address();
        QSignalSpy unlinked(m_bond, &Bond::unlinked);

        m_bond->move(elsewhere->address());
        QTRY_COMPARE_WITH_TIMEOUT(m_bond->sentence(), Words::keyUnknownThere(), 5000);
        QCOMPARE(m_settings->address(), before);
        // A key refused by *another* address says nothing about this device's own library:
        // it stays linked to it.
        QVERIFY(m_settings->configured());
        QCOMPARE(unlinked.count(), 0);
        delete elsewhere;
    }

    void moving_to_another_self_signed_certificate_is_refused()
    {
        linked();
        Https *elsewhere = serve(u"other"_s);
        QVERIFY(elsewhere);
        const QString before = m_settings->address();

        m_bond->move(elsewhere->address());
        QTRY_COMPARE_WITH_TIMEOUT(m_bond->sentence(), Words::certificateMismatch(), 5000);
        QCOMPARE(m_settings->address(), before);
        QCOMPARE(m_settings->fingerprint(), pinOf(u"pinned"_s));
        // Refused inside the handshake: the key never left.
        QTest::qWait(100);
        QVERIFY(elsewhere->heard.isEmpty());
        delete elsewhere;
    }

    void moving_to_plain_http_on_another_machine_is_refused()
    {
        linked();
        const QString before = m_settings->address();
        m_bond->move(u"http://192.0.2.7:8081"_s);
        QCOMPARE(m_bond->sentence(), Words::httpsElsewhere());
        QCOMPARE(m_settings->address(), before);
    }

    void unlinking_takes_the_key_off_the_server_then_forgets_it()
    {
        linked();
        m_library->answerFor = [](const QByteArray &request) {
            return request.startsWith("DELETE /device ")
                       ? QByteArray("HTTP/1.1 204 .\r\nConnection: close\r\n\r\n")
                       : QByteArray();
        };
        QSignalSpy unlinked(m_bond, &Bond::unlinked);
        m_bond->unlink();
        QTRY_COMPARE_WITH_TIMEOUT(unlinked.count(), 1, 5000);
        QVERIFY(m_library->heard.startsWith("DELETE /device "));
        QVERIFY(m_library->heard.contains("X-Leaf-Key: " + Key.toUtf8()));
        QVERIFY(!m_settings->configured());
        // The link screen opens empty, not on the link this device was made with.
        QCOMPARE(m_linking->stage(), Linking::Stage::Empty);
        QVERIFY(m_linking->sentence().isEmpty());
    }

    void unlinking_forgets_even_when_the_server_is_unreachable()
    {
        linked();
        // Gone: the address now answers nothing at all.
        delete m_library;
        m_library = nullptr;
        QSignalSpy unlinked(m_bond, &Bond::unlinked);

        m_bond->unlink();
        QTRY_COMPARE_WITH_TIMEOUT(unlinked.count(), 1, 5000);
        QVERIFY(!m_settings->configured());
        // Said, on the screen that opens: the key may still work on the server, and only the
        // person can go and remove it there.
        QCOMPARE(m_linking->sentence(), Words::unlinkedWithoutTheServer(u"Bureau"_s));

        // Forgotten on disk too.
        QTRY_VERIFY_WITH_TIMEOUT(m_settings->loaded(), 5000);
        Settings again;
        QSignalSpy loaded(&again, &Settings::changed);
        QVERIFY(loaded.wait(5000));
        QVERIFY(!again.configured());
        QVERIFY(again.address().isEmpty());
    }

    void unlinking_while_the_client_holds_back_says_the_server_was_not_told()
    {
        linked();
        // A 429: the client now waits before asking anything, the DELETE included.
        m_library->answerFor = [](const QByteArray &request) {
            return request.startsWith("GET ")
                       ? reply(429, R"({"error":"too many"})").replace(
                             "Connection: close", "Retry-After: 60\r\nConnection: close")
                       : QByteArray();
        };
        bool answered = false;
        m_server->get(u"/series"_s, this, [&answered](const Server::Answer &) {
            answered = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(answered, 5000);
        QSignalSpy unlinked(m_bond, &Bond::unlinked);

        m_bond->unlink();
        QTRY_COMPARE_WITH_TIMEOUT(unlinked.count(), 1, 5000);
        QTest::qWait(100);
        QVERIFY(!m_library->heard.contains("DELETE "));
        QVERIFY(!m_settings->configured());
        QCOMPARE(m_linking->sentence(), Words::unlinkedWithoutTheServer(u"Bureau"_s));
    }

    void an_unlink_the_server_refuses_as_unknown_is_forgotten_once()
    {
        linked();
        m_library->answerFor = [](const QByteArray &) { return reply(403, Unknown); };
        QSignalSpy unlinked(m_bond, &Bond::unlinked);

        m_bond->unlink();
        QTRY_COMPARE_WITH_TIMEOUT(unlinked.count(), 1, 5000);
        QTest::qWait(200);
        QCOMPARE(unlinked.count(), 1);
        // Already removed on the server: the sentence that says so, not wiped by a second
        // forgetting.
        QCOMPARE(m_linking->sentence(), Words::deviceRevoked());
    }

    void a_name_in_the_revoke_command_is_quoted_for_the_shell()
    {
        // Pasted into a root terminal: nothing in a name may be read by the shell.
        const QString name = u"a$(x)`y`\"b'c"_s;
        const QString quoted = u"'a$(x)`y`\"b'\\''c'"_s;
        QVERIFY(Words::unlinkIfSilent(name).contains(u"device revoke "_s + quoted + u"."_s));
        QVERIFY(Words::unlinkedWithoutTheServer(name).contains(u"device revoke "_s + quoted));
    }

    void the_commands_to_remove_a_device_by_hand_run_under_sudo()
    {
        // They read /etc/leaf/server.env, which only root may read: copied without `sudo`,
        // each one fails on the line it was pasted.
        for (const QString &name : {u"Bureau"_s, QString()}) {
            for (const QString &said :
                 {Words::unlinkIfSilent(name), Words::unlinkedWithoutTheServer(name)}) {
                QVERIFY2(said.contains(u"sudo leaf-server device revoke"_s), qPrintable(said));
                QVERIFY2(!said.contains(QRegularExpression(u"(?<!sudo )leaf-server device"_s)),
                         qPrintable(said));
            }
        }
    }

    void an_unlink_refused_for_a_key_from_leaf_keys_forgets_and_says_where_that_key_lives()
    {
        linked();
        // Linked with an old key, which has no device to remove: the server cannot do it, and
        // `device revoke` does not know that key either.
        m_library->answerFor = [](const QByteArray &request) {
            return request.startsWith("DELETE /device ")
                       ? reply(409, R"({"error":"this key comes from LEAF_KEYS: rename or remove it in server.env"})")
                       : QByteArray();
        };
        QSignalSpy unlinked(m_bond, &Bond::unlinked);

        m_bond->unlink();
        QTRY_COMPARE_WITH_TIMEOUT(unlinked.count(), 1, 5000);
        QVERIFY(!m_settings->configured());
        QCOMPARE(m_linking->sentence(), Words::unlinkedAnOldKey());
        QVERIFY(m_linking->sentence().contains(u"LEAF_KEYS"_s));
        QVERIFY(m_linking->sentence().contains(u"/etc/leaf/server.env"_s));
        QVERIFY(!m_linking->sentence().contains(u"device revoke"_s));
        QVERIFY(!m_linking->sentence().contains(Key));
    }

    void unlinking_under_the_environment_sends_nothing()
    {
        Pretend plain;
        QVERIFY(plain.listen(QHostAddress::LocalHost));
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(plain.serverPort()).toUtf8());
        qputenv("LEAF_KEY", Key.toUtf8());
        build();
        QSignalSpy unlinked(m_bond, &Bond::unlinked);

        m_bond->unlink();
        QCOMPARE(m_bond->sentence(), Words::settingsFromEnvironment());
        QTest::qWait(100);
        // Nothing reached the server: removing the key there while the environment goes on
        // sending it would leave this device locked out and still configured.
        QVERIFY(plain.heard.isEmpty());
        QCOMPARE(unlinked.count(), 0);
        QVERIFY(m_settings->configured());
    }

    void a_device_revoked_on_the_server_returns_to_the_link_screen_once()
    {
        linked();
        m_library->answerFor = [](const QByteArray &) { return reply(403, Unknown); };
        QSignalSpy unlinked(m_bond, &Bond::unlinked);

        // Three screens asking at once, as they do when the window opens: three refusals.
        ask(u"/series"_s);
        ask(u"/next"_s);
        ask(u"/health"_s);
        QTRY_COMPARE_WITH_TIMEOUT(unlinked.count(), 1, 5000);
        QTest::qWait(300);
        QCOMPARE(unlinked.count(), 1);

        QVERIFY(!m_settings->configured());
        QCOMPARE(m_linking->stage(), Linking::Stage::Empty);
        QCOMPARE(m_linking->sentence(), Words::deviceRevoked());
    }

    void a_refusal_from_the_address_left_behind_does_not_forget_the_move()
    {
        linked();
        Https *elsewhere = serve(u"pinned"_s);
        QVERIFY(elsewhere);
        elsewhere->answerFor = [](const QByteArray &request) {
            return request.startsWith("GET /series?limit=1 ") ? reply(200, NoSeries)
                                                                : QByteArray();
        };
        // Asked of the old address, and held there until after the move.
        ask(u"/series"_s);
        QTRY_VERIFY_WITH_TIMEOUT(m_library->heard.contains("GET /series "), 5000);
        QSignalSpy unlinked(m_bond, &Bond::unlinked);

        m_bond->move(elsewhere->address());
        QTRY_COMPARE_WITH_TIMEOUT(m_settings->address(), elsewhere->address(), 5000);
        m_library->release(reply(403, Unknown));
        QTest::qWait(300);

        QCOMPARE(unlinked.count(), 0);
        QVERIFY(m_settings->configured());
        QCOMPARE(m_settings->address(), elsewhere->address());
        // Nor does it stop the client at the address it now uses.
        QVERIFY(!m_server->stopped());
        delete elsewhere;
    }

    void a_key_set_in_the_environment_is_not_forgotten_on_a_refusal()
    {
        Pretend plain;
        plain.answers(403, Unknown);
        QVERIFY(plain.listen(QHostAddress::LocalHost));
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(plain.serverPort()).toUtf8());
        qputenv("LEAF_KEY", Key.toUtf8());
        build();
        QSignalSpy unlinked(m_bond, &Bond::unlinked);

        ask(u"/series"_s);
        ask(u"/next"_s);
        QTRY_COMPARE_WITH_TIMEOUT(m_bond->sentence(), Words::revokedUnderEnvironment(), 5000);
        QTest::qWait(200);
        QCOMPARE(unlinked.count(), 0);
        QVERIFY(m_settings->configured());
        QCOMPARE(m_settings->storage(), Settings::Storage::Environment);
    }

    void a_rename_cut_short_by_a_change_of_pin_is_not_a_server_failure()
    {
        linked();
        // The rename is heard and never answered: in flight when the pin changes.
        QSignalSpy changed(m_bond, &Bond::changed);
        m_bond->rename(u"Liseuse"_s);
        QTRY_VERIFY_WITH_TIMEOUT(m_library->heard.contains("PATCH /device "), 5000);

        // What a move to a library behind an authority does: the same key, another pin. The
        // next request on the client's own connection drops the ones judged under the old one,
        // the rename with them.
        m_settings->moveTo(QUrl(m_library->address()), pinOf(u"other"_s));
        ask(u"/health"_s);

        QTRY_COMPARE_WITH_TIMEOUT(m_bond->sentence(), Words::cutShortByANewPin(), 5000);
        QVERIFY(!m_bond->sentence().contains(Words::unreachable({})));
        QCOMPARE(m_settings->deviceName(), u"Bureau"_s);
        QVERIFY(!m_bond->busy());
    }

    void relaying_from_the_loopback_says_the_link_works_only_here_and_still_shows_it()
    {
        // `install.sh all`, or an SSH tunnel: this device reaches the library at 127.0.0.1,
        // and so would a link built from that address — on whichever device opens it.
        Pretend plain;
        QVERIFY(plain.listen(QHostAddress::LocalHost));
        plain.answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /link/codes ")) {
                return issued();
            }
            return reply(200, R"({"state":"waiting"})");
        };
        build();
        m_settings->remember(QUrl(u"http://127.0.0.1:%1"_s.arg(plain.serverPort())), QString(),
                             Key, u"Bureau"_s, QDateTime::currentDateTimeUtc());
        QTRY_VERIFY_WITH_TIMEOUT(m_settings->configured(), 5000);

        m_bond->relay();
        QTRY_VERIFY_WITH_TIMEOUT(!m_bond->relayLink().isEmpty(), 5000);
        QVERIFY(m_bond->relayLink().startsWith(u"leaf://127.0.0.1:"_s));
        QCOMPARE(m_bond->relayOnlyHere(), Words::relayOnlyHere());
        QVERIFY(m_bond->relayOnlyHere().contains(u"sudo leaf-server device add"_s));
        QVERIFY(m_bond->relayOnlyHere().contains(u"LEAF_PUBLIC_URL"_s));

        // Gone with the link.
        m_bond->stopRelay();
        QVERIFY(m_bond->relayOnlyHere().isEmpty());
    }

    void relaying_shows_a_link_built_from_this_devices_own_address_and_pin()
    {
        linked();
        m_library->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /link/codes ")) {
                return issued();
            }
            return request.startsWith("GET /link/codes/c-1 ")
                       ? reply(200, R"({"state":"waiting"})")
                       : QByteArray();
        };
        m_bond->relay();
        QTRY_VERIFY_WITH_TIMEOUT(!m_bond->relayLink().isEmpty(), 5000);

        // Composed here, from what this device itself uses — the server only made the code.
        const QUrl address(m_library->address());
        QCOMPARE(m_bond->relayLink(), u"leaf://127.0.0.2:%1/lier/K7QF-2MXP?empreinte=%2"_s.arg(
                                          address.port())
                                          .arg(pinOf(u"pinned"_s)));
        // And read back by the link screen into this very library.
        const std::optional<Link> read = Link::read(m_bond->relayLink());
        QVERIFY(read);
        QCOMPARE(read->address.toString(), m_settings->address());
        QCOMPARE(read->fingerprint, m_settings->fingerprint());
        QCOMPARE(read->code, u"K7QF2MXP"_s);

        QVERIFY(m_bond->relaySeconds() > 590 && m_bond->relaySeconds() <= 600);
        QVERIFY(m_bond->relayed().isEmpty());
        // Another machine's address: the link works from anywhere that reaches it.
        QVERIFY(m_bond->relayOnlyHere().isEmpty());
        QVERIFY(m_library->heard.contains("X-Leaf-Key: " + Key.toUtf8()));
    }

    void the_relay_says_the_name_of_the_device_that_used_it()
    {
        linked();
        m_library->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /link/codes ")) {
                return issued();
            }
            if (request.startsWith("GET /link/codes/c-1 ")) {
                return reply(200, R"({"state":"served","name":"PC du salon"})");
            }
            return request.startsWith("DELETE ") ? Withdrawn : QByteArray();
        };
        m_bond->relay();
        QTRY_COMPARE_WITH_TIMEOUT(m_bond->relayed(), u"PC du salon"_s, 5000);
        // Spent: a link that can no longer link anything is not left on screen.
        QVERIFY(m_bond->relayLink().isEmpty());

        // Closed after it was used: there is nothing left on the server to withdraw.
        m_bond->stopRelay();
        QTest::qWait(100);
        QVERIFY(!m_library->heard.contains("DELETE "));
    }

    void closing_the_relay_before_it_is_used_cancels_the_code()
    {
        linked();
        m_library->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /link/codes ")) {
                return issued();
            }
            if (request.startsWith("GET /link/codes/c-1 ")) {
                return reply(200, R"({"state":"waiting"})");
            }
            return request.startsWith("DELETE /link/codes/c-1 ") ? Withdrawn : QByteArray();
        };
        m_bond->relay();
        QTRY_VERIFY_WITH_TIMEOUT(!m_bond->relayLink().isEmpty(), 5000);

        m_bond->stopRelay();
        QVERIFY(m_bond->relayLink().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(m_library->heard.contains("DELETE /link/codes/c-1 "), 5000);
    }

    void a_relay_closed_before_the_server_answered_cancels_the_code_it_gets()
    {
        linked();
        // The code is held on its way back: the panel closes while it is still being made.
        m_library->answerFor = [](const QByteArray &request) {
            return request.startsWith("DELETE /link/codes/c-1 ") ? Withdrawn : QByteArray();
        };
        m_bond->relay();
        QTRY_VERIFY_WITH_TIMEOUT(m_library->heard.contains("POST /link/codes "), 5000);
        m_bond->stopRelay();
        m_library->release(issued());

        // Never shown, and withdrawn at once: nobody is left looking at it.
        QTRY_VERIFY_WITH_TIMEOUT(m_library->heard.contains("DELETE /link/codes/c-1 "), 5000);
        QVERIFY(m_bond->relayLink().isEmpty());
    }

    void a_code_gone_from_the_server_takes_its_link_with_it()
    {
        linked();
        m_library->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /link/codes ")) {
                return issued();
            }
            return request.startsWith("GET /link/codes/c-1 ")
                       ? reply(404, R"({"error":"unknown code"})")
                       : QByteArray();
        };
        m_bond->relay();
        QTRY_COMPARE_WITH_TIMEOUT(m_bond->relaySentence(), Words::relayRanOut(), 5000);
        QVERIFY(m_bond->relayLink().isEmpty());
        QCOMPARE(m_bond->relaySeconds(), 0);
    }

    void a_key_with_no_device_to_follow_keeps_the_link_and_stops_asking()
    {
        linked();
        m_library->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /link/codes ")) {
                return issued();
            }
            return request.startsWith("GET /link/codes/c-1 ")
                       ? reply(409, R"({"error":"a key from LEAF_KEYS has no device"})")
                       : QByteArray();
        };
        m_bond->relay();
        QTRY_VERIFY_WITH_TIMEOUT(m_library->heard.contains("GET /link/codes/c-1 "), 5000);
        // Another turn of the clock: nothing more is asked.
        QTest::qWait(2500);
        QCOMPARE(m_library->heard.count("GET /link/codes/c-1 "), 1);
        QVERIFY(!m_bond->relayLink().isEmpty());
        QVERIFY(m_bond->relaySentence().isEmpty());
    }

    void a_code_state_this_client_cannot_read_stops_the_following_and_says_so()
    {
        linked();
        m_library->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /link/codes ")) {
                return issued();
            }
            return request.startsWith("GET /link/codes/c-1 ")
                       ? reply(200, R"({"state":"lost"})")
                       : QByteArray();
        };
        m_bond->relay();
        QTRY_COMPARE_WITH_TIMEOUT(m_bond->relaySentence(), Words::unreadableAnswer(), 5000);
        QTest::qWait(2500);
        // Not asked again for ten minutes in silence.
        QCOMPARE(m_library->heard.count("GET /link/codes/c-1 "), 1);
    }

    void a_relay_reopened_while_its_code_is_on_the_way_shows_that_code()
    {
        linked();
        m_library->answerFor = [](const QByteArray &request) {
            if (request.startsWith("GET /link/codes/c-1 ")) {
                return reply(200, R"({"state":"waiting"})");
            }
            return request.startsWith("DELETE ") ? Withdrawn : QByteArray();
        };
        m_bond->relay();
        QTRY_VERIFY_WITH_TIMEOUT(m_library->heard.contains("POST /link/codes "), 5000);
        // Closed and opened again before the server answered.
        m_bond->stopRelay();
        m_bond->relay();
        m_library->release(issued());

        QTRY_VERIFY_WITH_TIMEOUT(!m_bond->relayLink().isEmpty(), 5000);
        QCOMPARE(m_library->heard.count("POST /link/codes "), 1);
        QVERIFY(!m_library->heard.contains("DELETE "));
    }

    void a_relay_left_open_counts_down_to_nothing()
    {
        linked();
        m_library->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /link/codes ")) {
                return issued(2500);
            }
            return request.startsWith("GET /link/codes/c-1 ")
                       ? reply(200, R"({"state":"waiting"})")
                       : QByteArray();
        };
        m_bond->relay();
        QTRY_VERIFY_WITH_TIMEOUT(!m_bond->relayLink().isEmpty(), 5000);
        const int first = m_bond->relaySeconds();
        QVERIFY(first > 0 && first <= 3);

        QTRY_VERIFY_WITH_TIMEOUT(m_bond->relaySeconds() < first, 3000);
        QVERIFY(!m_bond->relayLink().isEmpty());
        // At nothing, the code is no good to anyone, and its link goes with it.
        QTRY_COMPARE_WITH_TIMEOUT(m_bond->relaySeconds(), 0, 5000);
        QVERIFY(m_bond->relayLink().isEmpty());
        QVERIFY(m_bond->relayed().isEmpty());
    }

    void a_qr_code_encodes_exactly_the_link()
    {
        const QString link = u"leaf://192.0.2.20:8081/lier/P3WN-8TQA?empreinte=%1"_s.arg(
            pinOf(u"pinned"_s));
        const QrImage::Modules drawn = QrImage::modules(link);
        // The bytes of the link and nothing else: not the code alone, not the link trimmed.
        const QByteArray bytes = link.toUtf8();
        const qrcodegen::QrCode expected = qrcodegen::QrCode::encodeBinary(
            std::vector<std::uint8_t>(bytes.begin(), bytes.end()), qrcodegen::QrCode::Ecc::MEDIUM);
        QCOMPARE(drawn.size, expected.getSize());
        for (int y = 0; y < drawn.size; ++y) {
            for (int x = 0; x < drawn.size; ++x) {
                QCOMPARE(drawn.at(x, y), expected.getModule(x, y));
            }
        }
        QVERIFY(QrImage::modules({}).size == 0);
    }

    void a_qr_code_is_drawn_module_for_module_inside_its_quiet_zone()
    {
        const QString link = u"leaf://leaf.example.net/lier/M4TZ-7BXC"_s;
        const QrImage::Modules modules = QrImage::modules(link);
        QVERIFY(modules.size > 0);
        // Six pixels a module, so that each one is sampled at its own centre.
        constexpr int Pixels = 6;
        const int side = (modules.size + 2 * QrImage::QuietZone) * Pixels;

        QrCode item;
        item.setText(link);
        item.setSize(QSizeF(side, side));
        QImage picture(side, side, QImage::Format_RGB32);
        picture.fill(Qt::magenta);
        QPainter painter(&picture);
        item.paint(&painter);
        painter.end();

        const auto darkAt = [&](int x, int y) {
            const QPoint centre((QrImage::QuietZone + x) * Pixels + Pixels / 2,
                                (QrImage::QuietZone + y) * Pixels + Pixels / 2);
            return qGray(picture.pixel(centre)) < 128;
        };
        for (int y = 0; y < modules.size; ++y) {
            for (int x = 0; x < modules.size; ++x) {
                QCOMPARE(darkAt(x, y), modules.at(x, y));
            }
        }
        // The quiet zone is light all round: a scanner finds the corners by it.
        QVERIFY(qGray(picture.pixel(1, 1)) > 200);
        QVERIFY(qGray(picture.pixel(side - 2, side - 2)) > 200);
    }
};

QTEST_MAIN(KeepsTheBond)
#include "keeps_the_bond.moc"
