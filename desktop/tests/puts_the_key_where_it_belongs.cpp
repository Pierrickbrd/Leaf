// Where the key goes when QML does the fetching, and — the point of the file — where it does
// not.
//
// `Server` puts the key on what it asks for itself. A cover is not one of those: it is an
// `Image`, fetched by the engine's own network manager, which knows only a URL. `Covers` puts
// the key on those too, and the whole risk of doing so is putting it on the wrong ones — a
// key sent to whatever host a page happens to name is a key given away.

#include "Covers.h"
#include "Pretend.h"
#include "Server.h"
#include "Settings.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <QUrl>
#include <QtGlobal>

using namespace Qt::StringLiterals;

class PutsTheKeyWhereItBelongs : public QObject
{
    Q_OBJECT

private:
    Pretend *m_leaf = nullptr;
    Pretend *m_elsewhere = nullptr;
    Settings *m_settings = nullptr;
    Covers *m_covers = nullptr;
    QNetworkAccessManager *m_network = nullptr;

    /// Fetches once and hands back what the server heard, headers and all.
    QByteArray fetch(Pretend *from, const QString &path)
    {
        from->heard.clear();
        const QUrl url(QStringLiteral("http://127.0.0.1:%1%2").arg(from->serverPort()).arg(path));
        QScopedPointer<QNetworkReply> reply(m_network->get(QNetworkRequest(url)));
        QSignalSpy done(reply.data(), &QNetworkReply::finished);
        done.wait(5000);
        return from->heard;
    }

private slots:
    void init()
    {
        QStandardPaths::setTestModeEnabled(true);
        qunsetenv("LEAF_ADDRESS");
        qunsetenv("LEAF_KEY");
        m_leaf = new Pretend;
        m_elsewhere = new Pretend;
        QVERIFY(m_leaf->listen(QHostAddress::LocalHost));
        QVERIFY(m_elsewhere->listen(QHostAddress::LocalHost));
        m_leaf->answers(200, "{}");
        m_elsewhere->answers(200, "{}");

        m_settings = new Settings;
        QSignalSpy loaded(m_settings, &Settings::changed);
        QVERIFY(loaded.wait(5000));
        m_settings->setAddress(QStringLiteral("http://127.0.0.1:%1").arg(m_leaf->serverPort()));
        m_settings->setKey(QStringLiteral("8f3a92c1d4e5b6a7"));

        m_covers = new Covers(m_settings);
        m_network = m_covers->create(this);
    }

    void cleanup()
    {
        delete m_network;
        delete m_covers;
        delete m_settings;
        delete m_elsewhere;
        delete m_leaf;
    }

    void a_cover_from_the_configured_server_carries_the_key()
    {
        const QByteArray heard = fetch(m_leaf, u"/series/dn/cover"_s);

        QVERIFY(heard.contains(Server::KeyHeader));
        QVERIFY(heard.contains("8f3a92c1d4e5b6a7"));
    }

    void a_request_to_anywhere_else_carries_nothing()
    {
        // The one that matters. An `Image` in a delegate takes whatever string the model gave
        // it, and a model reading a field off a server is one bad answer away from naming
        // somebody else's host.
        const QByteArray heard = fetch(m_elsewhere, u"/series/dn/cover"_s);

        QVERIFY(!heard.contains(Server::KeyHeader));
        QVERIFY(!heard.contains("8f3a92c1d4e5b6a7"));
    }

    void the_key_that_rides_is_the_one_in_force_now()
    {
        // Asked of the settings per request rather than held by the manager: the keyring
        // answers after the run has begun, and the settings screen can change the key again
        // afterwards. A manager built at the first cover would carry the old one all session.
        m_settings->setKey(QStringLiteral("0000111122223333"));

        const QByteArray heard = fetch(m_leaf, u"/series/dn/cover"_s);

        QVERIFY(heard.contains("0000111122223333"));
        QVERIFY(!heard.contains("8f3a92c1d4e5b6a7"));
    }

    void a_manager_that_outlives_its_factory_carries_no_key()
    {
        // QQmlEngine owns the manager but not its factory. The application keeps the factory
        // longer, yet a teardown in the opposite order must still become harmless rather
        // than reading a deleted Settings pointer or disclosing the key.
        delete m_covers;
        m_covers = nullptr;

        const QByteArray heard = fetch(m_leaf, u"/series/dn/cover"_s);

        QVERIFY(!heard.contains(Server::KeyHeader));
        QVERIFY(!heard.contains("8f3a92c1d4e5b6a7"));
    }

    void a_factory_that_cannot_resolve_settings_says_so_and_returns_none()
    {
        QQmlEngine engine;
        Covers unresolved(&engine);
        QTest::ignoreMessage(
            QtWarningMsg,
            "error resolving the Settings singleton — covers will be asked for without a key");

        QVERIFY(!unresolved.settings());

        Covers unconfigured(static_cast<Settings *>(nullptr));
        QVERIFY(!unconfigured.settings());
    }

    void two_addresses_are_the_same_server_or_they_are_not_data()
    {
        QTest::addColumn<QString>("one");
        QTest::addColumn<QString>("other");
        QTest::addColumn<bool>("same");

        QTest::newRow("the same, written the same")
            << u"https://leaf.local/series/x/cover"_s << u"https://leaf.local"_s << true;
        QTest::newRow("the port a scheme implies, written out")
            << u"https://leaf.local:443/series/x/cover"_s << u"https://leaf.local"_s << true;
        QTest::newRow("a host is a host whatever its case")
            << u"https://Leaf.Local/series/x/cover"_s << u"https://leaf.local"_s << true;
        QTest::newRow("another port is another server")
            << u"https://leaf.local:8443/series/x/cover"_s << u"https://leaf.local"_s << false;
        QTest::newRow("another host is another server")
            << u"https://elsewhere.local/series/x/cover"_s << u"https://leaf.local"_s << false;
        QTest::newRow("plain where the server is not")
            << u"http://leaf.local/series/x/cover"_s << u"https://leaf.local"_s << false;
        // Where the engine spends most of its fetching: the module's own resources, and the
        // empty source of a tile whose row has not arrived.
        QTest::newRow("a resource is nobody's server")
            << u"qrc:/qt/qml/Leaf/Main.qml"_s << u"https://leaf.local"_s << false;
        QTest::newRow("nothing configured is not a match for nothing asked")
            << u"qrc:/qt/qml/Leaf/Main.qml"_s << QString() << false;
        QTest::newRow("nothing configured is not the server an image named")
            << u"https://leaf.local/series/x/cover"_s << QString() << false;
        QTest::newRow("an unusual scheme gets no invented port")
            << u"ftp://leaf.local/series/x/cover"_s << u"ftp://leaf.local"_s << true;
    }

    void two_addresses_are_the_same_server_or_they_are_not()
    {
        QFETCH(QString, one);
        QFETCH(QString, other);
        QFETCH(bool, same);

        QCOMPARE(Covers::sameServer(QUrl(one), QUrl(Server::tidy(other))), same);
    }
};

QTEST_MAIN(PutsTheKeyWhereItBelongs)
#include "puts_the_key_where_it_belongs.moc"
