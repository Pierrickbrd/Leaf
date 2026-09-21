// The two axes the bandeau offers, against a real HTTP boundary and no window.
//
// What is worth pinning here is not that `/filters` parses — `Api` is tested for that — but
// the rule the artifact states and the QML must never re-decide: an axis that does not cut
// the library in two is not offered at all.

#include "Filters.h"
#include "Shelf.h"
#include "Pretend.h"
#include "Server.h"
#include "Settings.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

QJsonArray counted(const QList<QPair<QString, int>> &values)
{
    QJsonArray facets;
    for (const auto &one : values) {
        facets << QJsonObject{{u"value"_s, one.first}, {u"count"_s, one.second}};
    }
    return facets;
}

QByteArray answer(const QJsonArray &readStatuses, const QJsonArray &media)
{
    return QJsonDocument(QJsonObject{
                             {u"readStatuses"_s, readStatuses},
                             {u"media"_s, media},
                         })
        .toJson(QJsonDocument::Compact);
}

QString labelOf(const QVariantList &pills, int at)
{
    return pills.at(at).toMap().value(u"label"_s).toString();
}

QString valueOf(const QVariantList &pills, int at)
{
    return pills.at(at).toMap().value(u"value"_s).toString();
}

int countOf(const QVariantList &pills, int at)
{
    return pills.at(at).toMap().value(u"count"_s).toInt();
}

} // namespace

class HoldsTheFilters : public QObject
{
    Q_OBJECT

    Pretend *m_pretend = nullptr;
    Settings *m_settings = nullptr;
    Server *m_server = nullptr;
    Filters *m_filters = nullptr;
    Shelf *m_shelf = nullptr;

    void settle()
    {
        for (int i = 0; i < 200 && m_filters->loading(); ++i)
            QTest::qWait(10);
    }

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void init()
    {
        m_pretend = new Pretend;
        QVERIFY(m_pretend->listen(QHostAddress::LocalHost));
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(m_pretend->serverPort()).toUtf8());
        qputenv("LEAF_KEY", QByteArrayLiteral("8f3a92c1d4e5b6a7"));

        m_settings = new Settings;
        m_server = new Server(m_settings);
        m_shelf = new Shelf(m_server);
        m_filters = new Filters(m_server, m_shelf);
    }

    void cleanup()
    {
        delete m_filters;
        delete m_shelf;
        delete m_server;
        delete m_settings;
        qunsetenv("LEAF_ADDRESS");
        qunsetenv("LEAF_KEY");
        delete m_pretend;
    }

    /// Two states and two media: both axes cut the library, both are offered, and each pill
    /// carries the contract's spelling to send back, the French to draw, and the count.
    void an_axis_that_cuts_the_library_is_offered_worded_and_counted()
    {
        m_pretend->answers(200, answer(counted({{u"UNREAD"_s, 12}, {u"IN_PROGRESS"_s, 3}}),
                                       counted({{u"manga"_s, 41}, {u"bd"_s, 12}})));
        m_filters->reload();
        settle();

        QVERIFY(m_pretend->heard.contains("GET /filters"));
        QVERIFY(m_filters->trouble().isEmpty());
        QCOMPARE(m_filters->readStatuses().size(), 2);
        QCOMPARE(valueOf(m_filters->readStatuses(), 0), u"UNREAD"_s);
        QCOMPARE(labelOf(m_filters->readStatuses(), 0), u"Non lues 12"_s);
        QCOMPARE(countOf(m_filters->readStatuses(), 0), 12);
        QCOMPARE(labelOf(m_filters->readStatuses(), 1), u"En cours 3"_s);

        QCOMPARE(m_filters->media().size(), 2);
        QCOMPARE(valueOf(m_filters->media(), 1), u"bd"_s);
        // The acronym keeps its case: this is the pill that would read "Bd" if anyone ever
        // reached for a capitalise-the-first-letter helper.
        QCOMPARE(labelOf(m_filters->media(), 1), u"BD 12"_s);
        QVERIFY(!m_filters->tooManyValues());
    }

    /// Left to right, the order `Words.h` writes down — and not the order the server happened
    /// to group its rows in, which would put "En cours" first on one library and second on the
    /// next. The answer below is deliberately out of order.
    void the_row_reads_in_the_order_the_client_decided()
    {
        m_pretend->answers(
            200, answer(counted({{u"READ"_s, 4}, {u"IN_PROGRESS"_s, 3}, {u"UNREAD"_s, 5}}),
                        counted({{u"comics"_s, 1}, {u"bd"_s, 3}, {u"manga"_s, 8}})));
        m_filters->reload();
        settle();

        QCOMPARE(labelOf(m_filters->readStatuses(), 0), u"Non lues 5"_s);
        QCOMPARE(labelOf(m_filters->readStatuses(), 1), u"En cours 3"_s);
        QCOMPARE(labelOf(m_filters->readStatuses(), 2), u"Terminées 4"_s);
        QCOMPARE(labelOf(m_filters->media(), 0), u"Manga 8"_s);
        QCOMPARE(labelOf(m_filters->media(), 1), u"BD 3"_s);
        QCOMPARE(labelOf(m_filters->media(), 2), u"Comics 1"_s);
    }

    /// The shelf this was first run against: six series, all manga, all unread. One value on
    /// each axis covers everything, so neither axis is offered and the row stays empty.
    void an_axis_with_one_value_filters_nothing_and_is_not_offered()
    {
        m_pretend->answers(200, answer(counted({{u"UNREAD"_s, 6}}), counted({{u"manga"_s, 6}})));
        m_filters->reload();
        settle();

        QVERIFY(m_filters->readStatuses().isEmpty());
        QVERIFY(m_filters->media().isEmpty());
        QVERIFY(m_filters->trouble().isEmpty());
        QVERIFY(!m_filters->tooManyValues());
    }

    /// Above four values the artifact asks for a button and a menu. Until that exists the
    /// axis goes, and `tooManyValues` says why — an empty row would otherwise read as a
    /// library with nothing to filter by.
    void an_axis_too_long_for_a_row_says_so_rather_than_going_quiet()
    {
        m_pretend->answers(200, answer(counted({{u"UNREAD"_s, 2}, {u"READ"_s, 2}}),
                                       counted({{u"manga"_s, 4},
                                                {u"bd"_s, 3},
                                                {u"comics"_s, 2},
                                                {u"manhwa"_s, 1},
                                                {u"webtoon"_s, 1}})));
        m_filters->reload();
        settle();

        QCOMPARE(m_filters->readStatuses().size(), 2);
        QVERIFY(m_filters->media().isEmpty());
        QVERIFY(m_filters->tooManyValues());
    }

    /// A word this client does not know is not a pill. The server may learn a medium first —
    /// a series carrying one still stands, deliberately — but a pill reading "Autre" beside
    /// real ones teaches nothing and filters on a spelling nobody here recognises.
    void a_value_with_no_word_for_it_is_not_drawn()
    {
        m_pretend->answers(
            200, answer(counted({{u"UNREAD"_s, 2}, {u"READ"_s, 2}, {u"SOMEDAY"_s, 1}}),
                        counted({{u"manga"_s, 2}, {u"bd"_s, 2}, {u"lightnovel"_s, 1}})));
        m_filters->reload();
        settle();

        QCOMPARE(m_filters->readStatuses().size(), 2);
        QCOMPARE(valueOf(m_filters->readStatuses(), 1), u"READ"_s);
        QCOMPARE(m_filters->media().size(), 2);
        QCOMPARE(valueOf(m_filters->media(), 1), u"bd"_s);
    }

    /// And when dropping the word nobody knows leaves a single value, the axis goes with it:
    /// the two rules meet, and the one about cutting the library wins.
    void a_lone_value_left_by_a_word_nobody_knows_takes_its_axis_with_it()
    {
        m_pretend->answers(200, answer(counted({{u"UNREAD"_s, 2}, {u"SOMEDAY"_s, 1}}),
                                       counted({{u"manga"_s, 2}, {u"lightnovel"_s, 1}})));
        m_filters->reload();
        settle();

        QVERIFY(m_filters->readStatuses().isEmpty());
        QVERIFY(m_filters->media().isEmpty());
        QVERIFY(!m_filters->tooManyValues());
    }

    void a_server_that_says_no_offers_nothing_and_says_why()
    {
        m_pretend->answers(503, QByteArrayLiteral("{}"));
        m_filters->reload();
        settle();

        QVERIFY(m_filters->readStatuses().isEmpty());
        QVERIFY(!m_filters->trouble().isEmpty());
    }

    void an_answer_that_is_not_an_object_is_refused()
    {
        m_pretend->answers(200, QByteArrayLiteral("[]"));
        m_filters->reload();
        settle();

        QVERIFY(m_filters->readStatuses().isEmpty());
        QCOMPARE(m_filters->trouble(), u"filters: expected an object"_s);
    }

    void no_server_is_an_explanation_instead_of_a_crash()
    {
        Filters orphan(nullptr);
        orphan.reload();

        QVERIFY(orphan.readStatuses().isEmpty());
        QVERIFY(!orphan.trouble().isEmpty());
        QVERIFY(!orphan.loading());
    }

    /// A row of pills drawn above a list of files counts files. It asks for them itself,
    /// following what is being searched for: a screen that has to remember to ask is a screen
    /// that will one day show the previous query's numbers, which is worse than none.
    void the_file_counts_follow_what_is_being_searched_for()
    {
        m_pretend->answers(200, answer(counted({{u"UNREAD"_s, 5}, {u"IN_PROGRESS"_s, 1}}),
                                       counted({{u"manga"_s, 4}, {u"bd"_s, 2}})));
        m_filters->reload();
        settle();
        QCOMPARE(countOf(m_filters->readStatuses(), 0), 5);
        // Nothing is being searched for, so no row sits above a list of files.
        QVERIFY(m_filters->fileReadStatuses().isEmpty());

        m_pretend->heard.clear();
        m_pretend->answers(200, answer(counted({{u"UNREAD"_s, 57}, {u"IN_PROGRESS"_s, 3}}),
                                       counted({{u"manga"_s, 60}})));
        m_shelf->searchFor(u"assassinat"_s);
        for (int i = 0; i < 300 && m_filters->fileReadStatuses().isEmpty(); ++i)
            QTest::qWait(10);

        // Counted over files and narrowed by the query, both said on the wire.
        QVERIFY(m_pretend->heard.contains("over=files"));
        QVERIFY(m_pretend->heard.contains("q=assassinat"));
        QCOMPARE(countOf(m_filters->fileReadStatuses(), 0), 57);
        QCOMPARE(countOf(m_filters->fileReadStatuses(), 1), 3);
        QCOMPARE(labelOf(m_filters->fileReadStatuses(), 0), u"Non lues 57"_s);

        // The series counts are untouched: the two answer different questions about
        // different populations, and one is not the other's replacement.
        QCOMPARE(countOf(m_filters->readStatuses(), 0), 5);

        // A single medium filters nothing, so that axis is not offered over files either.
        QVERIFY(m_filters->fileMedia().isEmpty());
        QCOMPARE(valueOf(m_filters->fileReadStatuses(), 0), u"UNREAD"_s);
    }

    /// And when the field is cleared there is no list of files, so the row that counted them
    /// goes with it rather than standing above nothing with the last query's numbers.
    void clearing_the_search_takes_the_file_counts_with_it()
    {
        m_pretend->answers(200, answer(counted({{u"UNREAD"_s, 57}, {u"IN_PROGRESS"_s, 3}}),
                                       counted({{u"manga"_s, 60}})));
        m_shelf->searchFor(u"assassinat"_s);
        for (int i = 0; i < 300 && m_filters->fileReadStatuses().isEmpty(); ++i)
            QTest::qWait(10);
        QVERIFY(!m_filters->fileReadStatuses().isEmpty());

        m_shelf->searchFor(QString());
        QVERIFY(m_filters->fileReadStatuses().isEmpty());
    }
};

QTEST_MAIN(HoldsTheFilters)
#include "holds_the_filters.moc"
