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
#include <QQmlEngine>
#include <QScopedPointer>
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

/// A selection, by the contract's axis names. Written once here so a test says what it is
/// narrowing by rather than relying on the order of two lists.
QVariantMap by(const QStringList &read, const QStringList &media = {})
{
    QVariantMap narrowing;
    if (!read.isEmpty())
        narrowing.insert(u"read"_s, read);
    if (!media.isEmpty())
        narrowing.insert(u"medium"_s, media);
    return narrowing;
}

constexpr int role(Shelf::Role value)
{
    return static_cast<int>(value);
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
    ///
    /// Two loops and not one. A change of criteria settles for 200 ms before the question
    /// goes out, so waiting only *while* the shelf is loading returns before anything has
    /// been asked — and every assertion about what reached the server then read an empty
    /// `heard`. The first loop gives the settling twice its time to fire; a test that
    /// expects nothing to go out pays that wait and nothing else.
    void settle()
    {
        for (int i = 0; i < 40 && !m_shelf->loading(); ++i) {
            QTest::qWait(10);
        }
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
        QCOMPARE(m_shelf->data(m_shelf->index(0), role(Shelf::Role::Name)).toString(),
                 u"Death Note"_s);
    }

    void a_row_it_no_longer_holds_and_a_role_it_does_not_offer_say_nothing()
    {
        // A reset makes every index a view held beforehand stale, while views also ask for
        // `DisplayRole` by habit. Neither is exceptional and neither is one of this model's
        // rows or roles, so both answers are deliberately empty.
        m_pretend->answers(200, aPage({aSeries(u"dn"_s, u"Death Note"_s),
                                       aSeries(u"ac"_s, u"Assassination Classroom"_s)},
                                      3));
        m_shelf->reload();
        settle();

        const QModelIndex first = m_shelf->index(0);
        const QModelIndex stale = m_shelf->index(1);
        QVERIFY(first.isValid());
        QVERIFY(stale.isValid());
        QCOMPARE(m_shelf->rowCount(first), 0);
        QVERIFY(m_shelf->canFetchMore({}));
        QVERIFY(!m_shelf->canFetchMore(first));
        m_pretend->heard.clear();
        m_shelf->fetchMore(first);
        QCOMPARE(requests(), 0);
        QVERIFY(!m_shelf->data(first, Qt::DisplayRole).isValid());
        QVERIFY(!m_shelf->data({}, role(Shelf::Role::Name)).isValid());

        m_pretend->answers(200, aPage({aSeries(u"pa"_s, u"Parasite"_s)}, 1));
        m_shelf->reload();
        settle();

        QVERIFY(stale.isValid());
        QVERIFY(!m_shelf->data(stale, role(Shelf::Role::Name)).isValid());
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

    /// What a row of pills sends. The contract's own spellings — UNREAD, manga — and its own
    /// rule: repeating a parameter widens the choice, so two lit pills on one axis go out as
    /// two `read=`, and one on each axis narrows.
    void lit_pills_go_out_as_the_contract_spells_them()
    {
        m_pretend->heard.clear();
        m_shelf->filterBy(by({u"UNREAD"_s, u"IN_PROGRESS"_s}, {u"manga"_s}));
        settle();

        QVERIFY(m_pretend->heard.contains("read=UNREAD"));
        QVERIFY(m_pretend->heard.contains("read=IN_PROGRESS"));
        QVERIFY(m_pretend->heard.contains("medium=manga"));
        QVERIFY(m_pretend->heard.contains("page=0"));
        QCOMPARE(m_shelf->readStatuses(), QStringList({u"UNREAD"_s, u"IN_PROGRESS"_s}));
        QCOMPARE(m_shelf->media(), QStringList({u"manga"_s}));
    }

    /// A changed filter starts the shelf again rather than appending to what is already
    /// held: the rows behind it answered a different question.
    void a_changed_filter_forgets_the_page_it_was_on()
    {
        m_pretend->answers(200, aPage({aSeries(u"dn"_s, u"Death Note"_s)}, 3));
        m_shelf->reload();
        settle();
        m_shelf->fetchMore({});
        settle();
        QCOMPARE(m_shelf->rowCount(), 2);

        m_pretend->heard.clear();
        m_pretend->answers(200, aPage({aSeries(u"ac"_s, u"Assassination Classroom"_s)}, 1));
        m_shelf->filterBy(by({u"READ"_s}, {}));
        settle();

        QVERIFY(m_pretend->heard.contains("page=0"));
        QCOMPARE(m_shelf->rowCount(), 1);
        QCOMPARE(m_shelf->data(m_shelf->index(0), role(Shelf::Role::Name)).toString(),
                 u"Assassination Classroom"_s);
    }

    /// A pill lit again by a stray binding must not cost a page.
    void the_same_filter_asked_twice_goes_out_once()
    {
        m_shelf->filterBy(by({u"READ"_s}, {}));
        settle();

        m_pretend->heard.clear();
        m_shelf->filterBy(by({u"READ"_s}, {}));
        settle();
        QCOMPARE(requests(), 0);
    }

    /// The contract drops blank values rather than filtering on the empty string. Dropped
    /// here too, or `readStatuses` would report a pill nobody can see lit.
    void a_blank_value_is_not_a_filter()
    {
        m_shelf->filterBy(by({u"READ"_s, QString()}, {QString()}));
        settle();

        QCOMPARE(m_shelf->readStatuses(), QStringList({u"READ"_s}));
        QVERIFY(m_shelf->media().isEmpty());
        QVERIFY(!m_pretend->heard.contains("medium="));
    }

    /// The order goes out with every page, default included: an order the client did not
    /// choose is an order no test here pins, and the server is free to move its own default.
    void it_asks_for_the_order_it_was_given()
    {
        m_shelf->reload();
        settle();
        QVERIFY(m_pretend->heard.contains("sort=name"));

        m_pretend->heard.clear();
        m_shelf->sortBy(u"added"_s);
        settle();
        QVERIFY(m_pretend->heard.contains("sort=added"));
        QVERIFY(m_pretend->heard.contains("page=0"));
        QCOMPARE(m_shelf->sort(), u"added"_s);
    }

    /// The same fallback as the server's, and reported as such: a shelf saying it is sorted
    /// by something it did not ask for is a bar naming an order nobody is looking at.
    void an_order_nobody_knows_is_the_name_order()
    {
        // From somewhere else, so that coming back to `name` is a change and can be seen on
        // the wire: asked from `name`, an unknown word changes nothing and asks nothing, which
        // is the other half of the same rule.
        m_shelf->sortBy(u"volumes"_s);
        settle();
        m_pretend->heard.clear();

        m_shelf->sortBy(u"par la couleur de la tranche"_s);
        settle();

        QCOMPARE(m_shelf->sort(), u"name"_s);
        QVERIFY(m_pretend->heard.contains("sort=name"));

        // And from `name`, where the fallback lands on the order already in force, it is
        // still not the second click that reverses one: it changes nothing and asks nothing.
        m_pretend->heard.clear();
        m_shelf->sortBy(u"par la couleur de la tranche"_s);
        settle();
        QCOMPARE(requests(), 0);
        QVERIFY(!m_shelf->sortReversed());
    }

    /// The four criteria are four menu entries and the reversal is none: asking again for the
    /// order in force turns it round. A fifth entry under them read as a fifth criterion.
    void the_same_order_asked_twice_turns_it_round()
    {
        m_shelf->sortBy(u"volumes"_s);
        settle();
        QVERIFY(!m_shelf->sortReversed());
        QVERIFY(m_pretend->heard.contains("direction=desc"));

        m_pretend->heard.clear();
        m_shelf->sortBy(u"volumes"_s);
        settle();
        QCOMPARE(requests(), 1);
        QVERIFY(m_shelf->sortReversed());
        QVERIFY(m_pretend->heard.contains("sort=volumes"));
        QVERIFY(m_pretend->heard.contains("direction=asc"));

        // And a third time comes back, rather than staying reversed for good.
        m_pretend->heard.clear();
        m_shelf->sortBy(u"volumes"_s);
        settle();
        QVERIFY(!m_shelf->sortReversed());
        QVERIFY(m_pretend->heard.contains("direction=desc"));
    }

    /// A reader clicking a pill or an order must not watch the shelf empty itself and fill
    /// again. What is on screen answers the previous question until there is an answer to
    /// the new one, and the series both questions hold keep their rows: a reset rebuilds
    /// every tile, which is the flicker this exists to prevent.
    void changing_the_criteria_keeps_the_shelf_until_the_answer_arrives()
    {
        m_pretend->answers(200, aPage({aSeries(u"a"_s, u"Akira"_s),
                                       aSeries(u"b"_s, u"Berserk"_s),
                                       aSeries(u"c"_s, u"Blame"_s)},
                                      3));
        m_shelf->reload();
        settle();
        QCOMPARE(m_shelf->count(), 3);

        QSignalSpy reset(m_shelf, &QAbstractItemModel::modelAboutToBeReset);
        QSignalSpy removed(m_shelf, &QAbstractItemModel::rowsRemoved);
        QSignalSpy inserted(m_shelf, &QAbstractItemModel::rowsInserted);

        // Berserk and Blame survive the pill; Akira does not, and Dorohedoro joins them.
        m_pretend->answers(200, aPage({aSeries(u"b"_s, u"Berserk"_s),
                                       aSeries(u"c"_s, u"Blame"_s),
                                       aSeries(u"d"_s, u"Dorohedoro"_s)},
                                      3));
        m_shelf->filterBy(by({u"UNREAD"_s}, {}));

        // Chosen, and the shelf has not moved: not while the criteria are settling, and not
        // while the question that follows is in flight. The reader sees the answer to the
        // previous question until there is an answer to this one.
        QCOMPARE(m_shelf->count(), 3);
        QCOMPARE(m_shelf->data(m_shelf->index(0), role(Shelf::Role::Name)).toString(),
                 u"Akira"_s);
        for (int i = 0; i < 40 && !m_shelf->loading(); ++i)
            QTest::qWait(10);
        QVERIFY2(m_shelf->loading(), "the settling never asked");
        QCOMPARE(m_shelf->count(), 3);
        QCOMPARE(m_shelf->data(m_shelf->index(0), role(Shelf::Role::Name)).toString(),
                 u"Akira"_s);

        settle();

        QCOMPARE(m_shelf->count(), 3);
        QCOMPARE(m_shelf->data(m_shelf->index(0), role(Shelf::Role::Name)).toString(),
                 u"Berserk"_s);
        QCOMPARE(m_shelf->data(m_shelf->index(2), role(Shelf::Role::Name)).toString(),
                 u"Dorohedoro"_s);

        // One row left and one row arrived, and the model was never reset: the two that
        // stayed were never removed, so their tiles were never rebuilt.
        QCOMPARE(reset.count(), 0);
        QCOMPARE(removed.count(), 1);
        QCOMPARE(removed.constFirst().at(1).toInt(), 0);
        QCOMPARE(removed.constFirst().at(2).toInt(), 0);
        QCOMPARE(inserted.count(), 1);
        QCOMPARE(inserted.constFirst().at(1).toInt(), 2);
    }

    /// The same series in another order is the whole of what reversing one does, and it is
    /// the case the row-by-row replacement can silently get wrong: nothing is added, nothing
    /// is removed, and a replacement that only ever inserts and deletes would show no change
    /// at all while the label above it said the order had turned round.
    void the_same_series_in_another_order_are_reordered_and_not_rebuilt()
    {
        m_pretend->answers(200, aPage({aSeries(u"a"_s, u"Akira"_s),
                                       aSeries(u"b"_s, u"Berserk"_s),
                                       aSeries(u"c"_s, u"Blame"_s)},
                                      3));
        m_shelf->reload();
        settle();

        QSignalSpy reset(m_shelf, &QAbstractItemModel::modelAboutToBeReset);
        QSignalSpy moved(m_shelf, &QAbstractItemModel::rowsMoved);

        // Z → A: the very thing a second click on « Nom » asks the server for.
        m_pretend->answers(200, aPage({aSeries(u"c"_s, u"Blame"_s),
                                       aSeries(u"b"_s, u"Berserk"_s),
                                       aSeries(u"a"_s, u"Akira"_s)},
                                      3));
        m_shelf->sortBy(u"name"_s);
        settle();

        QCOMPARE(m_shelf->count(), 3);
        QCOMPARE(m_shelf->data(m_shelf->index(0), role(Shelf::Role::Name)).toString(),
                 u"Blame"_s);
        QCOMPARE(m_shelf->data(m_shelf->index(1), role(Shelf::Role::Name)).toString(),
                 u"Berserk"_s);
        QCOMPARE(m_shelf->data(m_shelf->index(2), role(Shelf::Role::Name)).toString(),
                 u"Akira"_s);
        QCOMPARE(reset.count(), 0);
        QVERIFY2(moved.count() > 0, "the rows were never moved");

        // And the direction that was asked for is the one that went out.
        QVERIFY(m_pretend->heard.contains("direction=desc"));
    }

    /// And when the new criteria hold nothing, the shelf does empty — the emptiness is the
    /// answer, not a gap on the way to one.
    void criteria_that_hold_nothing_empty_the_shelf()
    {
        m_pretend->answers(200, aPage({aSeries(u"a"_s, u"Akira"_s)}, 1));
        m_shelf->reload();
        settle();
        QCOMPARE(m_shelf->count(), 1);

        m_pretend->answers(200, aPage({}, 0));
        m_shelf->filterBy(by({u"READ"_s}, {}));
        settle();
        QCOMPARE(m_shelf->count(), 0);
        QCOMPARE(m_shelf->total(), 0);
    }

    /// A shelf that could not be replaced keeps what it was showing. An empty grid would say
    /// the library holds nothing under these criteria, which is not what happened.
    void a_replacement_that_fails_leaves_the_shelf_alone_and_says_why()
    {
        m_pretend->answers(200, aPage({aSeries(u"a"_s, u"Akira"_s)}, 1));
        m_shelf->reload();
        settle();
        QCOMPARE(m_shelf->count(), 1);

        m_pretend->answers(500, QByteArrayLiteral("{}"));
        m_shelf->filterBy(by({u"UNREAD"_s}, {}));
        settle();

        QCOMPARE(m_shelf->count(), 1);
        QVERIFY(!m_shelf->trouble().isEmpty());
        QVERIFY(!m_shelf->loading());
    }

    /// Only `name` counts up by nature. The direction the shelf sends is the criterion's own
    /// unless it was turned round, so a reversal never means the same word twice.
    void each_criterion_has_its_own_familiar_direction()
    {
        // Read rather than asked for: a shelf opens on `name`, so asking for `name` here
        // would be the second click on it and would turn round what it was meant to observe.
        QCOMPARE(m_shelf->sort(), u"name"_s);
        QCOMPARE(m_shelf->sortDirection(), u"asc"_s);

        m_shelf->sortBy(u"added"_s);
        settle();
        QCOMPARE(m_shelf->sortDirection(), u"desc"_s);
        m_shelf->sortBy(u"added"_s);
        settle();
        QCOMPARE(m_shelf->sortDirection(), u"asc"_s);
    }

    /// A reversal belongs to the criterion that was reversed, and stays with it. Carried to
    /// the next one it would have a reader leave « Nom · Z → A », choose « Ajout » and get the
    /// oldest first, having asked for nothing; forgotten, it would lose what they had set.
    void every_criterion_remembers_the_way_it_was_left()
    {
        m_shelf->sortBy(u"name"_s);
        settle();
        QVERIFY(m_shelf->sortReversed());
        QCOMPARE(m_shelf->sortDirection(), u"desc"_s);

        // Another criterion, untouched so far: its own familiar direction, not the alphabet's.
        m_shelf->sortBy(u"added"_s);
        settle();
        QVERIFY(!m_shelf->sortReversed());
        QCOMPARE(m_shelf->sortDirection(), u"desc"_s);

        m_shelf->sortBy(u"added"_s);
        settle();
        QCOMPARE(m_shelf->sortDirection(), u"asc"_s);

        // And back to the alphabet, which is still where it was left.
        m_shelf->sortBy(u"name"_s);
        settle();
        QVERIFY(m_shelf->sortReversed());
        QCOMPARE(m_shelf->sortDirection(), u"desc"_s);

        // As is the one in between, when it is come back to in its turn.
        m_shelf->sortBy(u"added"_s);
        settle();
        QVERIFY(m_shelf->sortReversed());
        QCOMPARE(m_shelf->sortDirection(), u"asc"_s);
    }

    /// What was typed goes out with the chips, not instead of them: the contract runs a
    /// search inside what is showing, and a lit chip is a statement about what that is.
    void what_was_typed_goes_out_beside_the_lit_chips()
    {
        m_shelf->filterBy(by({u"UNREAD"_s}, {}));
        settle();

        m_pretend->heard.clear();
        m_shelf->searchFor(u"  assassinat  "_s);
        // Waited for rather than settled: what is asked waits for the typing to stop, so
        // there is nothing in flight to settle until it does.
        QTRY_VERIFY(m_pretend->heard.contains("q=assassinat"));
        settle();

        QVERIFY(m_pretend->heard.contains("read=UNREAD"));
        QVERIFY(m_pretend->heard.contains("page=0"));
        QCOMPARE(m_shelf->query(), u"assassinat"_s);
    }

    /// A cleared field is the whole shelf again, and says so by sending nothing at all.
    void a_cleared_field_is_not_a_search()
    {
        m_shelf->searchFor(u"assassinat"_s);
        settle();

        m_pretend->heard.clear();
        m_shelf->searchFor(u"   "_s);
        // Clearing does not wait: the request is already in flight when this returns.
        QVERIFY(m_shelf->loading());
        settle();

        QVERIFY(m_shelf->query().isEmpty());
        QVERIFY(!m_pretend->heard.contains("q="));
        QVERIFY(m_pretend->heard.contains("page=0"));
    }

    void the_same_query_typed_twice_goes_out_once()
    {
        m_shelf->searchFor(u"parasite"_s);
        QTRY_VERIFY(m_pretend->heard.contains("q=parasite"));
        settle();

        m_pretend->heard.clear();
        m_shelf->searchFor(u"parasite"_s);
        QTest::qWait(300);
        QCOMPARE(requests(), 0);
    }

    /// What is typed is kept at once and asked for once the typing stops. On five hundred
    /// series a key at a time is a page of a hundred rows per letter, and every one of them
    /// but the last is read by nobody.
    void what_is_typed_is_kept_at_once_and_asked_for_once_it_stops()
    {
        m_pretend->heard.clear();
        m_shelf->searchFor(u"a"_s);

        // The field is not a key behind: the text is already the shelf's.
        QCOMPARE(m_shelf->query(), u"a"_s);
        QCOMPARE(requests(), 0);

        QTRY_VERIFY(m_pretend->heard.contains("q=a"));
    }

    /// A word typed in one go is one question, not one per letter.
    void a_burst_of_keys_is_a_single_question()
    {
        m_pretend->heard.clear();
        for (const QString &sofar : {u"p"_s, u"pa"_s, u"par"_s, u"para"_s, u"paras"_s})
            m_shelf->searchFor(sofar);

        QTRY_VERIFY(m_pretend->heard.contains("q=paras"));
        settle();
        QCOMPARE(requests(), 1);
        QVERIFY2(!m_pretend->heard.contains("q=p&"), "no question for a half-typed word");
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
        QCOMPARE(m_shelf->data(m_shelf->index(1), role(Shelf::Role::Name)).toString(),
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
        QCOMPARE(m_shelf->data(first, role(Shelf::Role::Medium)).toString(), u"Manga"_s);
        QCOMPARE(m_shelf->data(first, role(Shelf::Role::Volumes)).toString(), u"21 tomes"_s);
        // Whole, and ready for an `Image`: the key it needs is put on by `Covers`, so no
        // `.qml` has to splice `Settings.address` onto a path.
        QCOMPARE(m_shelf->data(first, role(Shelf::Role::Cover)).toString(),
                 m_settings->address() + u"/series/dn/cover"_s);
    }

    void only_a_series_being_read_is_marked()
    {
        // §01: an emerald bar means in progress, nothing means never opened, and finished
        // carries no mark either — so two of the three answers are the same answer here.
        QJsonObject reading = aSeries(u"dn"_s, u"Death Note"_s);
        reading[u"readStatus"_s] = u"IN_PROGRESS"_s;
        QJsonObject finished = aSeries(u"ac"_s, u"Assassination Classroom"_s);
        finished[u"readStatus"_s] = u"READ"_s;
        m_pretend->answers(200, aPage({reading, finished, aSeries(u"pa"_s, u"Parasite"_s)}, 3));
        m_shelf->reload();
        settle();

        QVERIFY(m_shelf->data(m_shelf->index(0), role(Shelf::Role::InProgress)).toBool());
        QVERIFY(!m_shelf->data(m_shelf->index(1), role(Shelf::Role::InProgress)).toBool());
        QVERIFY(!m_shelf->data(m_shelf->index(2), role(Shelf::Role::InProgress)).toBool());
    }

    void a_shelf_factory_with_no_server_says_so_rather_than_reaching_through_nothing()
    {
        // A test executable has no registered Leaf module, which is exactly the broken-build
        // state this factory guards. It must still hand QML a model that can explain why it
        // is empty instead of handing the engine a null object or crashing on its first row.
        QQmlEngine engine;
        QTest::ignoreMessage(
            QtWarningMsg,
            "error resolving the Server singleton — the shelf will stay empty");
        QScopedPointer<Shelf> orphan(Shelf::create(&engine, nullptr));
        QVERIFY(orphan);
        orphan->reload();

        QCOMPARE(orphan->rowCount(), 0);
        QVERIFY(!orphan->trouble().isEmpty());
        QVERIFY(!orphan->loading());
    }

    void a_medium_the_server_did_not_give_is_left_unsaid()
    {
        // Absent is not "Autre": the shelf has nothing to say about a medium nobody recorded,
        // and a tile labelled with a guess is worse than one labelled with nothing.
        m_pretend->answers(200, aPage({aSeries(u"dn"_s, u"Death Note"_s)}, 1));
        m_shelf->reload();
        settle();

        QVERIFY(m_shelf->data(m_shelf->index(0), role(Shelf::Role::Medium)).toString().isEmpty());
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
