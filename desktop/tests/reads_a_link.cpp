// The one door a link comes through, pasted or clicked: what it reads into, and what it
// refuses to read into something nearby.

#include "Link.h"

#include <QTest>

using namespace Qt::StringLiterals;

namespace {
const QString print = u"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"_s;
}

class ReadsALink : public QObject {
    Q_OBJECT

private slots:
    void a_link_reads_into_an_address_a_code_and_a_fingerprint_data()
    {
        QTest::addColumn<QString>("typed");
        QTest::addColumn<QString>("address");
        QTest::addColumn<QString>("code");
        QTest::addColumn<QString>("fingerprint");

        QTest::newRow("an address, a port and a pin")
            << u"leaf://192.168.1.20:8081/lier/ABCDEFGH?empreinte="_s + print
            << u"https://192.168.1.20:8081"_s << u"ABCDEFGH"_s << print;
        QTest::newRow("a name and no pin")
            << u"leaf://leaf.example.net:9000/lier/ABCDEFGH"_s
            << u"https://leaf.example.net:9000"_s << u"ABCDEFGH"_s << QString();
        QTest::newRow("a pin in capitals reads as lowercase")
            << u"leaf://h/lier/ABCDEFGH?empreinte="_s + print.toUpper()
            << u"https://h"_s << u"ABCDEFGH"_s << print;
        QTest::newRow("an unknown parameter is ignored")
            << u"leaf://h/lier/ABCDEFGH?autre=1&empreinte="_s + print
            << u"https://h"_s << u"ABCDEFGH"_s << print;
    }
    void a_link_reads_into_an_address_a_code_and_a_fingerprint()
    {
        QFETCH(QString, typed);
        QFETCH(QString, address);
        QFETCH(QString, code);
        QFETCH(QString, fingerprint);
        const auto link = Link::read(typed);
        QVERIFY(link.has_value());
        QCOMPARE(link->address.toString(), address);
        QCOMPARE(link->code, code);
        QCOMPARE(link->fingerprint, fingerprint);
    }

    void no_port_means_443()
    {
        const auto link = Link::read(u"leaf://leaf.example.net/lier/ABCDEFGH"_s);
        QVERIFY(link.has_value());
        QCOMPARE(link->address.toString(), u"https://leaf.example.net"_s);
        QCOMPARE(link->address.port(443), 443);
        // An explicit 443 is the same address, written the way the server never writes it.
        const auto explicitPort = Link::read(u"leaf://leaf.example.net:443/lier/ABCDEFGH"_s);
        QVERIFY(explicitPort.has_value());
        QCOMPARE(explicitPort->address, link->address);
    }

    void the_loopback_is_read_as_plain_http_data()
    {
        QTest::addColumn<QString>("host");
        QTest::addColumn<QString>("address");
        QTest::newRow("ipv4") << u"127.0.0.1:8081"_s << u"http://127.0.0.1:8081"_s;
        QTest::newRow("name") << u"localhost:8081"_s << u"http://localhost:8081"_s;
        QTest::newRow("ipv6") << u"[::1]:8081"_s << u"http://[::1]:8081"_s;
    }
    void the_loopback_is_read_as_plain_http()
    {
        QFETCH(QString, host);
        QFETCH(QString, address);
        const auto link = Link::read(u"leaf://"_s + host + u"/lier/ABCDEFGH"_s);
        QVERIFY(link.has_value());
        QCOMPARE(link->address.toString(), address);
    }

    void spaces_newlines_case_and_a_trailing_slash_are_forgiven()
    {
        for (const QString &typed : {u"  leaf://h/lier/abcd-efgh/\n"_s,
                                     u"\tLEAF://H/lier/ABCD-EFGH\r\n"_s,
                                     u"leaf://h/lier/ABCD\n-EFGH"_s,
                                     u"leaf://h/lier/AbCdEfGh/"_s}) {
            const auto link = Link::read(typed);
            QVERIFY2(link.has_value(), qPrintable(typed));
            QCOMPARE(link->address.toString(), u"https://h"_s);
            QCOMPARE(link->code, u"ABCDEFGH"_s);
        }
    }

    void an_ipv6_address_keeps_its_brackets()
    {
        const auto link = Link::read(u"leaf://[fe80::1]:8081/lier/ABCDEFGH"_s);
        QVERIFY(link.has_value());
        QCOMPARE(link->address.toString(), u"https://[fe80::1]:8081"_s);
        QCOMPARE(link->address.port(), 8081);
    }

    void anything_else_is_not_a_link_data()
    {
        QTest::addColumn<QString>("typed");
        QTest::newRow("empty") << QString();
        QTest::newRow("blank") << u"  \n"_s;
        QTest::newRow("https, not leaf") << u"https://h/lier/ABCDEFGH"_s;
        QTest::newRow("another path") << u"leaf://h/autre/ABCDEFGH"_s;
        QTest::newRow("no path") << u"leaf://h"_s;
        QTest::newRow("a code too short") << u"leaf://h/lier/ABC"_s;
        QTest::newRow("a code too long") << u"leaf://h/lier/ABCDEFGHJ"_s;
        QTest::newRow("a character outside the alphabet") << u"leaf://h/lier/ABCDEFG0"_s;
        QTest::newRow("a pin that is not hex") << u"leaf://h/lier/ABCDEFGH?empreinte=zz"_s;
        QTest::newRow("a pin too short") << u"leaf://h/lier/ABCDEFGH?empreinte=abcd"_s;
        QTest::newRow("a pin with colons")
            << u"leaf://h/lier/ABCDEFGH?empreinte=01:23:45"_s;
        QTest::newRow("an empty pin") << u"leaf://h/lier/ABCDEFGH?empreinte="_s;
        QTest::newRow("no host") << u"leaf:///lier/ABCDEFGH"_s;
        QTest::newRow("a login in front of the host") << u"leaf://a:b@h/lier/ABCDEFGH"_s;
        QTest::newRow("a code on its own") << u"ABCD-EFGH"_s;
    }
    void anything_else_is_not_a_link()
    {
        QFETCH(QString, typed);
        QVERIFY(!Link::read(typed).has_value());
    }
};

QTEST_APPLESS_MAIN(ReadsALink)
#include "reads_a_link.moc"
