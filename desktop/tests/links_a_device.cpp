// A device linking itself from a `leaf://` link: read it, check the server, trade the code.
//
// Against a real TLS socket, for the reason `pins_a_certificate.cpp` gives: what has to be
// proved is what the server *heard* — the health check and nothing else before « Relier »,
// nothing at all from a server presenting the wrong certificate. A mock of the network would
// prove what the mock was told.
//
// The server listens on 127.0.0.2. A link to 127.0.0.1 means plain http — the one loopback
// exception the spec allows — while 127.0.0.2 is, for a link, any other machine: https, and
// held to the fingerprint the link carries. Linux answers the whole of 127/8 on the loopback,
// so no network is needed for it.

#include "Linking.h"
#include "Pinning.h"
#include "Settings.h"
#include "Words.h"

#include <QClipboard>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslServer>
#include <QSslSocket>
#include <QStandardPaths>
#include <QSysInfo>
#include <QFileInfo>
#include <QTest>

#include <qt6keychain/keychain.h>

#include <functional>

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

QByteArray reply(int status, const QByteArray &body)
{
    return "HTTP/1.1 " + QByteArray::number(status) + " .\r\nContent-Type: application/json\r\n"
           + "Content-Length: " + QByteArray::number(body.size())
           + "\r\nConnection: close\r\n\r\n" + body;
}

const QByteArray Healthy = R"({"status":"ok","api":1,"format":1,"library":11,"linked":true})";

/// `Pretend` over TLS: answers what `answerFor` says for each whole request, bodies included,
/// and remembers everything it heard. An empty answer holds the request without replying.
class Pretend : public QSslServer
{
public:
    QByteArray heard;
    std::function<QByteArray(const QByteArray &)> answerFor;

    explicit Pretend(const QString &name)
    {
        QSslConfiguration tls = QSslConfiguration::defaultConfiguration();
        tls.setLocalCertificate(certificate(name));
        tls.setPrivateKey(QSslKey(contents(name + u".key"_s), QSsl::Ec, QSsl::Pem));
        tls.setPeerVerifyMode(QSslSocket::VerifyNone);
        setSslConfiguration(tls);
        // What a Leaf server answers by default: its health, and nothing to a code.
        answerFor = [](const QByteArray &request) {
            return request.startsWith("GET /health ") ? reply(200, Healthy) : QByteArray();
        };
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
                        return;
                    }
                    socket->write(answer);
                    socket->flush();
                    socket->disconnectFromHost();
                });
            }
        });
    }

    bool heardAPost() const { return heard.contains("POST "); }
};

const QString Code = u"K7QF-2MXP"_s;

} // namespace

class LinksADevice : public QObject
{
    Q_OBJECT

private:
    Pretend *m_server = nullptr;
    Settings *m_settings = nullptr;
    Linking *m_linking = nullptr;

    /// Starts a server presenting `name` on 127.0.0.2.
    void serve(const QString &name)
    {
        m_server = new Pretend(name);
        QVERIFY(m_server->listen(QHostAddress(u"127.0.0.2"_s)));
    }

    /// A link to that server, carrying the fingerprint of `pinned`.
    QString linkTo(const QString &pinned = u"pinned"_s) const
    {
        return u"leaf://127.0.0.2:%1/lier/%2?empreinte=%3"_s.arg(m_server->serverPort())
            .arg(Code, Pinning::fingerprint(certificate(pinned)));
    }

    QString address() const { return u"https://127.0.0.2:%1"_s.arg(m_server->serverPort()); }

    /// Offers the link and waits for the check to end, whichever way it ends.
    void offerAndWait(const QString &link)
    {
        m_linking->offer(link);
        QTRY_VERIFY_WITH_TIMEOUT(m_linking->stage() != Linking::Stage::Checking, 5000);
    }

    /// The body of the last `POST /link` the server heard.
    QJsonObject sent() const
    {
        const QByteArray &heard = m_server->heard;
        const qsizetype post = heard.lastIndexOf("POST /link ");
        if (post < 0) {
            return {};
        }
        // Nothing follows it: the last thing any of these tests asks of the server is a code.
        return QJsonDocument::fromJson(heard.mid(heard.indexOf("\r\n\r\n", post) + 4))
            .object();
    }

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

    /// A fresh `Settings`, loaded, with nothing linked, and a `Linking` on it.
    void start()
    {
        m_settings = new Settings;
        QSignalSpy loaded(m_settings, &Settings::changed);
        QVERIFY(loaded.wait(5000));
        m_linking = new Linking(m_settings);
    }

    /// What the server answers to a `POST /link` that goes through.
    static QByteArray linked(const QString &name)
    {
        return reply(201, R"({"key":"Zm9yLXRoZS1kZXZpY2Utb25seS0wMTIzNDU2Nzg5YWJjZGVm",)"
                          R"("device":{"id":"d-1","name":")"
                              + name.toUtf8() + R"("}})");
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
        delete m_linking;
        m_linking = nullptr;
        delete m_settings;
        m_settings = nullptr;
        delete m_server;
        m_server = nullptr;
        qunsetenv("LEAF_ADDRESS");
        forgetTheKeyring();
    }

    void a_link_is_checked_before_anything_is_sent()
    {
        serve(u"pinned"_s);
        start();
        offerAndWait(linkTo());
        QCOMPARE(m_linking->stage(), Linking::Stage::Found);
        // The health check, and nothing that could be taken from it: no code, no key, and
        // not the link itself, which only ever lives on this machine.
        QVERIFY(m_server->heard.startsWith("GET /health "));
        QVERIFY(!m_server->heardAPost());
        QVERIFY(!m_server->heard.contains("K7QF"));
        QVERIFY(!m_server->heard.contains("X-Leaf-Key"));
        QVERIFY(!m_settings->configured());
    }

    void a_found_library_offers_the_machine_name()
    {
        serve(u"pinned"_s);
        start();
        QCOMPARE(m_linking->name(), QSysInfo::machineHostName());
        offerAndWait(linkTo());
        QCOMPARE(m_linking->stage(), Linking::Stage::Found);
        QCOMPARE(m_linking->found(), Words::libraryFound(u"127.0.0.2"_s));
        QVERIFY(m_linking->canLink());
        // An empty name is not one the server would take, so the button waits for one.
        m_linking->setName(u"   "_s);
        QVERIFY(!m_linking->canLink());
    }

    void linking_sends_the_code_and_the_name_then_remembers_the_answer()
    {
        serve(u"pinned"_s);
        m_server->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /link ")) {
                return linked(u"Bureau"_s);
            }
            return reply(200, Healthy);
        };
        start();
        QSignalSpy done(m_linking, &Linking::linked);
        offerAndWait(linkTo());
        m_linking->setName(u"  Bureau "_s);
        m_linking->link();
        QTRY_VERIFY_WITH_TIMEOUT(m_settings->configured(), 5000);

        QCOMPARE(sent().value(u"code"_s).toString(), u"K7QF2MXP"_s);
        QCOMPARE(sent().value(u"name"_s).toString(), u"Bureau"_s);
        QCOMPARE(m_settings->address(), address());
        QCOMPARE(m_settings->key(), u"Zm9yLXRoZS1kZXZpY2Utb25seS0wMTIzNDU2Nzg5YWJjZGVm"_s);
        QCOMPARE(m_settings->fingerprint(), Pinning::fingerprint(certificate(u"pinned"_s)));
        QCOMPARE(m_settings->deviceName(), u"Bureau"_s);
        QVERIFY(m_settings->linkedAt().isValid());
        QCOMPARE(m_linking->stage(), Linking::Stage::Linked);
        QCOMPARE(done.count(), 1);
    }

    void a_server_with_another_certificate_stops_at_mismatch_and_sends_no_code()
    {
        serve(u"other"_s);
        start();
        offerAndWait(linkTo(u"pinned"_s));
        QCOMPARE(m_linking->stage(), Linking::Stage::Mismatch);
        QCOMPARE(m_linking->sentence(), Words::certificateMismatch());
        QVERIFY(!m_linking->canLink());

        // And no way past it: « Relier » does nothing from here.
        m_linking->link();
        QTest::qWait(200);
        QCOMPARE(m_linking->stage(), Linking::Stage::Mismatch);
        QVERIFY(m_server->heard.isEmpty());
    }

    void an_expired_code_says_so()
    {
        serve(u"pinned"_s);
        m_server->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /link ")) {
                return reply(410, R"({"error":"code"})");
            }
            return reply(200, Healthy);
        };
        start();
        offerAndWait(linkTo());
        m_linking->link();
        QTRY_COMPARE_WITH_TIMEOUT(m_linking->stage(), Linking::Stage::CodeNotValid, 5000);
        QCOMPARE(m_linking->sentence(), Words::linkNoLongerValid());
        QVERIFY(!m_linking->canLink());
        QVERIFY(!m_settings->configured());
    }

    void a_taken_name_says_so_and_keeps_the_link()
    {
        serve(u"pinned"_s);
        int posts = 0;
        m_server->answerFor = [&posts](const QByteArray &request) {
            if (!request.startsWith("POST /link ")) {
                return reply(200, Healthy);
            }
            ++posts;
            return posts == 1 ? reply(409, R"({"error":"name","name":"Bureau"})")
                              : linked(u"Bureau 2"_s);
        };
        start();
        offerAndWait(linkTo());
        m_linking->setName(u"bureau"_s);
        m_linking->link();
        QTRY_COMPARE_WITH_TIMEOUT(m_linking->stage(), Linking::Stage::NameTaken, 5000);
        // Named the way the device that holds it spells it.
        QCOMPARE(m_linking->sentence(), Words::nameTaken(u"Bureau"_s));
        QVERIFY(m_linking->canLink());

        // The code is still good: another name, the same link.
        m_linking->setName(u"Bureau 2"_s);
        m_linking->link();
        QTRY_VERIFY_WITH_TIMEOUT(m_settings->configured(), 5000);
        QCOMPARE(sent().value(u"code"_s).toString(), u"K7QF2MXP"_s);
        QCOMPARE(m_settings->deviceName(), u"Bureau 2"_s);
        QCOMPARE(m_linking->stage(), Linking::Stage::Linked);
    }

    void something_that_is_not_a_link_says_what_a_link_looks_like()
    {
        start();
        m_linking->offer(u"https://leaf.example/lier/K7QF-2MXP"_s);
        QCOMPARE(m_linking->stage(), Linking::Stage::NotALink);
        QCOMPARE(m_linking->sentence(), Words::notALink());
        QVERIFY(!m_linking->canLink());

        // An emptied field is not a mistake, and says nothing.
        m_linking->offer(u""_s);
        QCOMPARE(m_linking->stage(), Linking::Stage::Empty);
        QVERIFY(m_linking->sentence().isEmpty());
    }

    void a_link_on_the_clipboard_fills_the_field()
    {
        serve(u"pinned"_s);
        start();
        QGuiApplication::clipboard()->setText(u"une phrase copiée pour autre chose"_s);
        m_linking->lookAtTheClipboard();
        QVERIFY(m_linking->typed().isEmpty());
        QCOMPARE(m_linking->stage(), Linking::Stage::Empty);

        QGuiApplication::clipboard()->setText(linkTo());
        m_linking->lookAtTheClipboard();
        QCOMPARE(m_linking->typed(), linkTo());
        QTRY_COMPARE_WITH_TIMEOUT(m_linking->stage(), Linking::Stage::Found, 5000);
    }

    void a_link_that_arrives_from_outside_is_checked_and_never_sent()
    {
        serve(u"pinned"_s);
        // Before the settings have loaded, as a link given on the command line is: it waits
        // for them rather than taking a linked client for an unlinked one.
        m_settings = new Settings;
        m_linking = new Linking(m_settings);
        m_linking->arrive(linkTo());
        QCOMPARE(m_linking->stage(), Linking::Stage::Empty);

        QTRY_COMPARE_WITH_TIMEOUT(m_linking->stage(), Linking::Stage::Found, 5000);
        QCOMPARE(m_linking->typed(), linkTo());
        // The person has not pressed « Relier »: the server heard the check and no code.
        QVERIFY(m_server->heard.startsWith("GET /health "));
        QVERIFY(!m_server->heardAPost());
        QVERIFY(!m_settings->configured());
    }

    void a_link_that_arrives_at_a_linked_device_replaces_nothing_and_says_so()
    {
        serve(u"pinned"_s);
        qputenv("LEAF_ADDRESS", "http://127.0.0.1:9");
        qputenv("LEAF_KEY", "already-here");
        start();
        QVERIFY(m_settings->configured());
        QSignalSpy refused(m_linking, &Linking::refusedWhileLinked);
        m_linking->arrive(linkTo());
        QCOMPARE(refused.count(), 1);
        // The sentence is held for the settings to show, so that it does not depend on any
        // bubble the person may have switched off.
        QCOMPARE(m_linking->notice(), Words::alreadyLinked());
        m_linking->clearNotice();
        QVERIFY(m_linking->notice().isEmpty());
        QCOMPARE(m_linking->stage(), Linking::Stage::Empty);
        QTest::qWait(200);
        QVERIFY(m_server->heard.isEmpty());
    }

    void a_link_that_arrives_while_a_code_is_on_its_way_is_ignored()
    {
        serve(u"pinned"_s);
        m_server->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /link ")) {
                return QByteArray();
            }
            return reply(200, Healthy);
        };
        start();
        offerAndWait(linkTo());
        m_linking->link();
        QCOMPARE(m_linking->stage(), Linking::Stage::Sending);

        // Another click while the key is in flight must not change what it will be stored
        // for: the screen stays as it was.
        m_linking->arrive(linkTo(u"other"_s));
        QCOMPARE(m_linking->stage(), Linking::Stage::Sending);
        QCOMPARE(m_linking->typed(), linkTo());
    }

    void a_link_that_arrives_while_a_key_is_held_is_ignored()
    {
        qputenv("LEAF_ADDRESS", "https://elsewhere.example");
        serve(u"pinned"_s);
        m_server->answerFor = [](const QByteArray &request) {
            if (request.startsWith("POST /link ")) {
                return linked(u"Bureau"_s);
            }
            return reply(200, Healthy);
        };
        start();
        offerAndWait(linkTo());
        m_linking->link();
        QTRY_COMPARE_WITH_TIMEOUT(m_linking->sentence(), Words::settingsFromEnvironment(), 5000);

        m_linking->arrive(linkTo(u"other"_s));
        QCOMPARE(m_linking->typed(), linkTo());
        QCOMPARE(m_linking->stage(), Linking::Stage::Found);
    }

    void a_link_that_arrives_on_the_screen_replaces_the_one_there()
    {
        serve(u"pinned"_s);
        start();
        offerAndWait(linkTo());
        QCOMPARE(m_linking->stage(), Linking::Stage::Found);
        m_linking->arrive(u"pas un lien"_s);
        QCOMPARE(m_linking->typed(), u"pas un lien"_s);
        QCOMPARE(m_linking->stage(), Linking::Stage::NotALink);
    }

    void settings_that_find_no_keyring_are_still_loaded_so_a_link_is_not_left_waiting()
    {
        // Covered by the whole file run with no session bus (the CI case): `start()` waits
        // for `loaded`, and `a_link_that_arrives_from_outside…` waits on it too. Here, the
        // direct statement.
        start();
        QVERIFY(m_settings->loaded());
    }

    void a_new_link_while_one_is_checked_drops_the_first_without_a_failure()
    {
        // The first server hears the check and never answers; a second link arrives meanwhile,
        // with another pin. The first check is cancelled — by the pin change, among others —
        // and that cancel is not an answer: nothing may say the first address is unreachable.
        serve(u"pinned"_s);
        m_server->answerFor = [](const QByteArray &) { return QByteArray(); };
        start();
        m_linking->offer(linkTo(u"pinned"_s));
        QTRY_VERIFY_WITH_TIMEOUT(m_server->heard.contains("\r\n\r\n"), 5000);

        Pretend second(u"other"_s);
        QVERIFY(second.listen(QHostAddress(u"127.0.0.2"_s)));
        QList<Linking::Stage> seen;
        const QMetaObject::Connection watching = connect(
            m_linking, &Linking::changed, this, [&] { seen.append(m_linking->stage()); });
        m_linking->offer(u"leaf://127.0.0.2:%1/lier/%2?empreinte=%3"_s.arg(second.serverPort())
                             .arg(Code, Pinning::fingerprint(certificate(u"other"_s))));
        QTRY_COMPARE_WITH_TIMEOUT(m_linking->stage(), Linking::Stage::Found, 5000);
        QTest::qWait(100);
        disconnect(watching);
        QVERIFY(!seen.contains(Linking::Stage::Unreachable));
        QCOMPARE(m_linking->stage(), Linking::Stage::Found);
    }

    void settings_that_refuse_the_link_say_why_and_send_no_second_code()
    {
        // The environment names an address and no key: the screen opens, and Settings will
        // write nothing underneath an environment. Its sentence is the one shown, and a
        // second press tries the writing again rather than spending a code that is used.
        qputenv("LEAF_ADDRESS", "https://elsewhere.example");
        serve(u"pinned"_s);
        int posts = 0;
        m_server->answerFor = [&posts](const QByteArray &request) {
            if (request.startsWith("POST /link ")) {
                ++posts;
                return linked(u"Bureau"_s);
            }
            return reply(200, Healthy);
        };
        start();
        offerAndWait(linkTo());
        m_linking->link();
        QTRY_COMPARE_WITH_TIMEOUT(m_linking->sentence(), Words::settingsFromEnvironment(), 5000);
        QVERIFY(!m_settings->configured());
        QVERIFY(m_linking->stage() != Linking::Stage::Linked);
        QVERIFY(m_linking->canLink());

        m_linking->link();
        QTest::qWait(200);
        QCOMPARE(posts, 1);
        QCOMPARE(m_linking->sentence(), Words::settingsFromEnvironment());
    }
};

QTEST_MAIN(LinksADevice)
#include "links_a_device.moc"
