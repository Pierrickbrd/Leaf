// The shelf: what it holds, when it asks for more, and what it says when the answer does not
// come.
//
// Headless, against `Pretend`. Everything here is the model's own behaviour — none of it
// needs a window, a grid, or a real server, which is the whole reason the model is C++ and
// not QML.

#include "Pretend.h"
#include "Server.h"
#include "Settings.h"
#include "Shelf.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaEnum>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <QtGlobal>

using namespace Qt::StringLiterals;

namespace {

/// One series carrying every field the contract requires and nothing else, so a test that
/// adds a field is a test saying that field matters to it.
QJsonObject aSeries(const QString &id, const QString &name)
{
    return QJsonObject{
        {u"id"_s, id},         {u"workId"_s, u"w-"_s + id}, {u"name"_s, name},
        {u"work"_s, name},     {u"entryCount"_s, 12},       {u"chapterCount"_s, 0},
        {u"arcCount"_s, 0},
    };
}

/// A page as the server sends it: what this page carries, and how many there are behind it.
QByteArray aPage(const QJsonArray &items, int total, int page = 0)
{
    return QJsonDocument(QJsonObject{
                             {u"items"_s, items},
                             {u"total"_s, total},
                             {u"page"_s, page},
                             {u"size"_s, 100},
                         })
        .toJson(QJsonDocument::Compact);
}

} // namespace

class HoldsAShelf : public QObject
{
    Q_OBJECT

private:
    Pretend *m_pretend = nullptr;
    Settings *m_settings = nullptr;
    Server *m_server = nullptr;
    Shelf *m_shelf = nullptr;

    /// Spins until the shelf has stopped waiting, rather than for a fixed time: a sleep long
    /// enough on this machine is a test that fails on a slower one for a reason that has
    /// nothing to do with the code.
    void settle()
    {
        for (int i = 0; i < 200 && m_shelf->loading(); ++i) {
            QTest::qWait(10);
        }
    }

    /// How many requests reached the server. One `GET` line per request, and `heard`
    /// accumulates, so counting them is counting requests.
    int requests() const { return int(m_pretend->heard.count("GET ")); }

private slots:
    void init()
    {
        QStandardPaths::setTestModeEnabled(true);
        // Both cleared before any `Settings` exists: `load()` reads the environment first and,
        // finding either, never asks the keyring — so a machine set up to run the client for
        // real would otherwise decide what these tests exercise.
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
        m_shelf = new Shelf(m_server);
        m_pretend->answers(200, aPage({}, 0));
    }

    void cleanup()
    {
        delete m_shelf;
        delete m_server;
        delete m_settings;
        delete m_pretend;
    }

    void a_new_shelf_holds_nothing_and_has_asked_for_nothing()
    {
        // A view showing no rows asks for none, so `fetchMore` can never be what starts a
        // shelf. Nothing goes out until something says to.
        QCOMPARE(m_shelf->rowCount(), 0);
        QCOMPARE(requests(), 0);
        QVERIFY(!m_shelf->loading());
    }

    void the_first_page_fills_the_shelf()
    {
        m_pretend->answers(200, aPage({aSeries(u"dn"_s, u"Death Note"_s),
                                       aSeries(u"ac"_s, u"Assassination Classroom"_s)},
                                      2));
        m_shelf->reload();
        settle();

        QCOMPARE(m_shelf->rowCount(), 2);
        QCOMPARE(m_shelf->total(), 2);
        QVERIFY(m_shelf->trouble().isEmpty());
        QCOMPARE(m_shelf->data(m_shelf->index(0), Shelf::NameRole).toString(),
                 u"Death Note"_s);
    }

    void it_asks_for_the_page_it_wants_and_the_size_it_chose()
    {
        // The contract's default size is 100 and its ceiling 500. Sent anyway: a page size
        // the client did not choose is a page size no test here pins, and the server is free
        // to move its own default.
        m_shelf->reload();
        settle();

        QVERIFY(m_pretend->heard.contains("page=0"));
        QVERIFY(m_pretend->heard.contains("size=100"));
    }

    void scrolling_past_the_first_page_asks_for_the_second()
    {
        m_pretend->answers(200, aPage({aSeries(u"dn"_s, u"Death Note"_s)}, 3));
        m_shelf->reload();
        settle();
        QVERIFY(m_shelf->canFetchMore({}));

        m_pretend->heard.clear();
        m_pretend->answers(200, aPage({aSeries(u"ac"_s, u"Assassination Classroom"_s)}, 3, 1));
        m_shelf->fetchMore({});
        settle();

        QVERIFY(m_pretend->heard.contains("page=1"));
        QCOMPARE(m_shelf->rowCount(), 2);
        QCOMPARE(m_shelf->data(m_shelf->index(1), Shelf::NameRole).toString(),
                 u"Assassination Classroom"_s);
    }

    void a_shelf_that_holds_everything_asks_for_no_more()
    {
        m_pretend->answers(200, aPage({aSeries(u"dn"_s, u"Death Note"_s)}, 1));
        m_shelf->reload();
        settle();

        QVERIFY(!m_shelf->canFetchMore({}));
    }

    void a_page_that_comes_back_empty_ends_the_shelf_whatever_the_total_says()
    {
        // A total that disagrees with what arrives is the shape of an endless loop: the view
        // asks, nothing comes, the count still falls short, the view asks again. What
        // actually arrived is what the shelf believes.
        m_pretend->answers(200, aPage({aSeries(u"dn"_s, u"Death Note"_s)}, 9));
        m_shelf->reload();
        settle();
        QVERIFY(m_shelf->canFetchMore({}));

        m_pretend->answers(200, aPage({}, 9, 1));
        m_shelf->fetchMore({});
        settle();

        QCOMPARE(m_shelf->rowCount(), 1);
        QVERIFY(!m_shelf->canFetchMore({}));
    }

    void a_second_request_does_not_go_out_while_the_first_is_still_in_flight()
    {
        m_pretend->answers(200, aPage({aSeries(u"dn"_s, u"Death Note"_s)}, 9));
        m_shelf->reload();
        settle();
        QVERIFY(m_shelf->canFetchMore({}));

        // Both calls in one turn of the loop, and from a shelf holding one of nine — so
        // `canFetchMore` is still true when the second arrives and only the shelf's own
        // knowledge that it is already waiting can stop it. Asserted after a `reload` rather
        // than during one, where `total` is back at zero and the second call is refused for a
        // reason that has nothing to do with the request in flight.
        m_pretend->heard.clear();
        m_pretend->answers(200, aPage({aSeries(u"ac"_s, u"Assassination Classroom"_s)}, 9, 1));
        m_shelf->fetchMore({});
        m_shelf->fetchMore({});
        settle();

        QCOMPARE(requests(), 1);
        QCOMPARE(m_shelf->rowCount(), 2);
    }

    void a_server_that_says_no_leaves_the_shelf_empty_and_says_why()
    {
        m_pretend->answers(500, "{}");
        m_shelf->reload();
        settle();

        QCOMPARE(m_shelf->rowCount(), 0);
        QVERIFY(!m_shelf->trouble().isEmpty());
        QVERIFY(!m_shelf->loading());
    }

    void a_series_the_contract_refuses_takes_its_page_with_it_and_says_which()
    {
        // `Api::page` refuses the whole page over one broken row, and this pins that rather
        // than wishing otherwise: a shelf holding the good half of a page it cannot read is a
        // shelf that scrolls to a gap nobody can explain.
        QJsonObject nameless = aSeries(u"ac"_s, u"Assassination Classroom"_s);
        nameless.remove(u"name"_s);
        m_pretend->answers(200, aPage({aSeries(u"dn"_s, u"Death Note"_s), nameless}, 2));
        m_shelf->reload();
        settle();

        QCOMPARE(m_shelf->rowCount(), 0);
        QVERIFY(m_shelf->trouble().contains(u"items[1]"_s));
    }

    void reloading_forgets_what_it_held()
    {
        m_pretend->answers(200, aPage({aSeries(u"dn"_s, u"Death Note"_s),
                                       aSeries(u"ac"_s, u"Assassination Classroom"_s)},
                                      2));
        m_shelf->reload();
        settle();
        QCOMPARE(m_shelf->rowCount(), 2);

        m_pretend->answers(200, aPage({aSeries(u"pa"_s, u"Parasite"_s)}, 1));
        m_shelf->reload();
        settle();

        QCOMPARE(m_shelf->rowCount(), 1);
        QCOMPARE(m_shelf->total(), 1);
    }

    void an_answer_to_a_shelf_that_has_been_reloaded_is_dropped()
    {
        // `Server` has no cancel: a request already out arrives whatever happens next. Two
        // reloads without spinning the loop between them put two answers on the way, and the
        // shelf must end up holding one page — not the two stacked.
        m_pretend->answers(200, aPage({aSeries(u"dn"_s, u"Death Note"_s)}, 1));
        m_shelf->reload();
        m_shelf->reload();
        settle();
        // Long enough for a straggler: without the guard the first answer lands here and the
        // shelf holds two.
        QTest::qWait(200);

        QCOMPARE(requests(), 2);
        QCOMPARE(m_shelf->rowCount(), 1);
    }

    void a_tile_is_handed_words_and_not_values_to_switch_on()
    {
        QJsonObject rich = aSeries(u"dn"_s, u"Death Note"_s);
        rich[u"medium"_s] = u"MANGA"_s;
        rich[u"ownedVolumes"_s] = 21;
        m_pretend->answers(200, aPage({rich}, 1));
        m_shelf->reload();
        settle();

        const QModelIndex first = m_shelf->index(0);
        QCOMPARE(m_shelf->data(first, Shelf::MediumRole).toString(), u"Manga"_s);
        QCOMPARE(m_shelf->data(first, Shelf::VolumesRole).toString(), u"21 tomes"_s);
        QCOMPARE(m_shelf->data(first, Shelf::CoverRole).toString(), u"/series/dn/cover"_s);
    }

    void a_medium_the_server_did_not_give_is_left_unsaid()
    {
        // Absent is not "Autre": the shelf has nothing to say about a medium nobody recorded,
        // and a tile labelled with a guess is worse than one labelled with nothing.
        m_pretend->answers(200, aPage({aSeries(u"dn"_s, u"Death Note"_s)}, 1));
        m_shelf->reload();
        settle();

        QVERIFY(m_shelf->data(m_shelf->index(0), Shelf::MediumRole).toString().isEmpty());
    }

    void every_role_the_grid_binds_to_has_a_name()
    {
        // A role with no name is a role QML cannot reach, and the delegate that tried reads
        // `undefined` in silence.
        //
        // Walked from the enumeration rather than from a range this test spells out, so a
        // role added tomorrow is covered the day it is added and not the day somebody
        // remembers to extend the range. A loop over an empty enumeration checks nothing and
        // reports nothing either, so the count is asserted before the walk and again after.
        const QMetaEnum roles = QMetaEnum::fromType<Shelf::Role>();
        QVERIFY2(roles.keyCount() > 0, "this test is broken, not the shelf: Role reads empty");
        const QHash<int, QByteArray> named = m_shelf->roleNames();
        for (int i = 0; i < roles.keyCount(); ++i) {
            QVERIFY2(named.contains(roles.value(i)), roles.key(i));
        }
        // Neither more nor fewer: a name left behind by a role that was removed points QML at
        // a role nothing answers for.
        QCOMPARE(int(named.size()), roles.keyCount());
        // `id` is QML's own word for a component's name; a role called that is a trap laid
        // for whoever writes the delegate.
        QVERIFY(!named.values().contains(QByteArray("id")));
    }
};

QTEST_MAIN(HoldsAShelf)
#include "holds_a_shelf.moc"
