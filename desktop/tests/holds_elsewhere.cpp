// The universe a series belongs to, and the named ways through it.

#include "Elsewhere.h"
#include "Pretend.h"
#include "SeriesCaptions.h"
#include "Server.h"
#include "Settings.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

#include <limits>

using namespace Qt::StringLiterals;

namespace {

QJsonObject aSeries(const QString &id, const QString &work, int volumes)
{
    return {{u"id"_s, id},
            {u"workId"_s, u"w-"_s + id},
            {u"name"_s, work},
            {u"work"_s, work},
            {u"medium"_s, u"bd"_s},
            {u"ownedVolumes"_s, volumes},
            {u"entryCount"_s, volumes},
            {u"chapterCount"_s, 0},
            {u"arcCount"_s, 0}};
}

QJsonObject aStep(const QString &work, const QString &seriesId = {},
                  std::optional<double> from = std::nullopt,
                  std::optional<double> to = std::nullopt)
{
    QJsonObject step{{u"workId"_s, u"w-"_s + work.toLower()}, {u"work"_s, work}};
    if (!seriesId.isEmpty()) {
        step[u"unit"_s] = u"VOLUME"_s;
        step[u"seriesId"_s] = seriesId;
        step[u"series"_s] = seriesId;
    }
    if (from.has_value()) {
        step[u"from"_s] = *from;
        step[u"to"_s] = *to;
    }
    return step;
}

QJsonObject aWay(const QString &id, const QString &name, const QJsonArray &steps,
                 bool byDefault = false)
{
    QJsonObject way{{u"id"_s, id}, {u"name"_s, name}, {u"steps"_s, steps}};
    if (byDefault)
        way[u"default"_s] = true;
    return way;
}

QByteArray shelfOf(const QJsonArray &rows)
{
    return QJsonDocument(QJsonObject{{u"items"_s, rows},
                                     {u"total"_s, rows.size()},
                                     {u"page"_s, 0},
                                     {u"size"_s, rows.size()}})
        .toJson(QJsonDocument::Compact);
}

QString nameAt(const QVariantList &tiles, int row)
{
    return tiles.at(row).toMap().value(u"name"_s).toString();
}

QString detailAt(const QVariantList &tiles, int row)
{
    return tiles.at(row).toMap().value(u"detail"_s).toString();
}

bool hereAt(const QVariantList &tiles, int row)
{
    return tiles.at(row).toMap().value(u"here"_s).toBool();
}

QByteArray answering(const QByteArray &body)
{
    return "HTTP/1.1 200 .\r\nContent-Type: application/json\r\nContent-Length: "
           + QByteArray::number(body.size()) + "\r\n\r\n" + body;
}

} // namespace

class HoldsElsewhere : public QObject
{
    Q_OBJECT

    Pretend *m_pretend = nullptr;
    Settings *m_settings = nullptr;
    Server *m_server = nullptr;
    Elsewhere *m_block = nullptr;

    void settle()
    {
        for (int i = 0; i < 300 && m_block->loading(); ++i)
            QTest::qWait(10);
        QTest::qWait(60);
    }

    /// The three answers a pointed block asks for. `declared` is what `/universes` says the
    /// universe holds, which defaults to what is served: a test that wants the guard tested
    /// says so by making the two disagree.
    void serve(const QByteArray &siblings, const QJsonArray &ways, int declared = -1)
    {
        const QByteArray walked = QJsonDocument(ways).toJson(QJsonDocument::Compact);
        const QByteArray universes =
            QJsonDocument(QJsonArray{QJsonObject{
                              {u"id"_s, u"u-arran"_s},
                              {u"name"_s, u"Terres d'Arran"_s},
                              {u"orderCount"_s, declared >= 0 ? declared : int(ways.size())}}})
                .toJson(QJsonDocument::Compact);
        m_pretend->answerFor = [siblings, walked, universes](const QByteArray &request) {
            // The ways first: their route is under `/universes` too, and the list would
            // answer for both.
            if (request.contains("/orders "))
                return answering(walked);
            return answering(request.contains("GET /universes") ? universes : siblings);
        };
    }

private slots:
    void init()
    {
        QStandardPaths::setTestModeEnabled(true);
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
        m_block = new Elsewhere(m_server);
        serve(shelfOf({aSeries(u"elfes"_s, u"Elfes"_s, 29),
                       aSeries(u"nains"_s, u"Nains"_s, 26)}),
              QJsonArray{});
    }

    void cleanup()
    {
        delete m_block;
        delete m_server;
        delete m_settings;
        delete m_pretend;
    }

    /// A series in no universe has nowhere to walk to, and the tab says so by being absent.
    void a_series_in_no_universe_asks_nothing()
    {
        m_block->point(QString(), QString(), u"elfes"_s);
        QVERIFY(!m_block->loading());
        QVERIFY(m_block->tiles().isEmpty());
        QVERIFY(m_pretend->heard.isEmpty());
    }

    /// Declaring no way through is the ordinary case, and the contract says so twice.
    void a_universe_that_declares_no_way_is_a_flat_list()
    {
        m_block->point(u"u-arran"_s, u"Terres d'Arran"_s, u"elfes"_s);
        settle();

        // Every match and not a page of them: a universe drawn short is not a shorter
        // universe, it is a wrong one.
        QVERIFY(m_pretend->heard.contains("GET /series?universe=Terres%20d'Arran&size=0"));
        QVERIFY(!m_pretend->heard.contains("/orders "));
        QVERIFY(m_block->orders().isEmpty());
        // The one being read is not offered as somewhere to go.
        QCOMPARE(m_block->tiles().size(), 1);
        QCOMPARE(nameAt(m_block->tiles(), 0), u"Nains"_s);
        QCOMPARE(detailAt(m_block->tiles(), 0), u"26 albums"_s);
        QVERIFY(m_block->outside().isEmpty());
    }

    /// The guard the contract publishes `orderCount` for. Nothing else in this client reads
    /// `/universes`, so a count read wrong here is a request per series page for an answer
    /// that is empty on most libraries.
    void a_universe_saying_it_declares_none_is_never_asked_for_its_ways()
    {
        serve(shelfOf({aSeries(u"elfes"_s, u"Elfes"_s, 29)}),
              QJsonArray{aWay(u"chrono"_s, u"Chronologique"_s, {aStep(u"Nains"_s)})}, 0);
        m_block->point(u"u-arran"_s, u"Terres d'Arran"_s, u"elfes"_s);
        settle();

        QVERIFY(m_pretend->heard.contains("GET /universes "));
        QVERIFY(!m_pretend->heard.contains("/orders "));
        QVERIFY(m_block->orders().isEmpty());
    }

    /// A universe the shelf named before the field existed: its series are still drawn, and
    /// nothing is asked about ways that cannot be addressed.
    void a_universe_with_no_identifier_is_drawn_and_never_walked()
    {
        m_block->point(QString(), u"Terres d'Arran"_s, u"elfes"_s);
        settle();

        QCOMPARE(m_block->tiles().size(), 1);
        QVERIFY(!m_pretend->heard.contains("GET /universes"));
        QVERIFY(m_block->orders().isEmpty());
    }

    /// A step naming a work this library does not hold is drawn from what the step itself
    /// carries. The contract says an order is always walkable, so this is not a broken step —
    /// it is a work filed under another universe, and the walk still has to name it.
    void a_step_the_universe_answer_does_not_hold_is_drawn_from_the_step()
    {
        serve(shelfOf({aSeries(u"elfes"_s, u"Elfes"_s, 29)}),
              QJsonArray{aWay(u"chrono"_s, u"Chronologique"_s,
                              {aStep(u"Nains"_s), aStep(u"Elfes"_s, u"elfes"_s, 1, 29)})});
        m_block->point(u"u-arran"_s, u"Terres d'Arran"_s, u"elfes"_s);
        settle();

        const QVariantList steps = m_block->tiles();
        QCOMPARE(steps.size(), 2);
        QCOMPARE(nameAt(steps, 0), u"Nains"_s);
        // No count and no cover rather than a wrong one, and no identifier: there is nothing
        // to open, and a tile that opened nothing would be a tile that looks openable.
        QVERIFY(detailAt(steps, 0).isEmpty());
        QVERIFY(steps.at(0).toMap().value(u"cover"_s).toString().isEmpty());
        QVERIFY(steps.at(0).toMap().value(u"seriesId"_s).toString().isEmpty());
        QCOMPARE(steps.at(0).toMap().value(u"howFarRead"_s).toDouble(), 0.0);
    }

    /// The block *becomes* the way: a tile per step and not per series, because the same work
    /// may come round again — which is the whole reason an order is not a sorted list.
    void a_way_through_is_drawn_as_steps_and_a_work_may_come_round_twice()
    {
        serve(shelfOf({aSeries(u"elfes"_s, u"Elfes"_s, 29),
                       aSeries(u"nains"_s, u"Nains"_s, 26),
                       aSeries(u"orcs"_s, u"Orcs"_s, 22)}),
              QJsonArray{aWay(u"chrono"_s, u"Chronologique"_s,
                              {aStep(u"Nains"_s), aStep(u"Elfes"_s, u"elfes"_s, 1, 7),
                               aStep(u"Elfes"_s, u"elfes"_s, 8, 29)},
                              true)});
        m_block->point(u"u-arran"_s, u"Terres d'Arran"_s, u"elfes"_s);
        settle();

        QCOMPARE(m_block->orders().size(), 1);
        QCOMPARE(m_block->chosenOrder(), u"chrono"_s);
        QCOMPARE(m_block->chosenName(), u"Chronologique"_s);
        const QVariantList steps = m_block->tiles();
        QCOMPARE(steps.size(), 3);
        QCOMPARE(nameAt(steps, 0), u"Nains"_s);
        QCOMPARE(nameAt(steps, 1), u"Elfes"_s);
        QCOMPARE(nameAt(steps, 2), u"Elfes"_s);
        // A stretch says which volumes; a whole work falls back on the ordinary count, which
        // is what the series it stands for holds — and which cost no request of its own.
        QCOMPARE(detailAt(steps, 0), u"26 albums"_s);
        QCOMPARE(detailAt(steps, 1), u"tomes 1 à 7"_s);
        QCOMPARE(detailAt(steps, 2), u"tomes 8 à 29"_s);
        // The cover comes from the universe answer, because a step does not carry one.
        QVERIFY(steps.at(0).toMap().value(u"cover"_s).toString().endsWith(
            u"/series/nains/cover"_s));

        // Nothing is open, so the mark falls on the first stretch of the series being read:
        // a walk still has to say where it was entered.
        QVERIFY(hereAt(steps, 1));
        QVERIFY(!hereAt(steps, 2));

        // What the way does not name comes after, under its own heading.
        QCOMPARE(m_block->outside().size(), 1);
        QCOMPARE(nameAt(m_block->outside(), 0), u"Orcs"_s);
    }

    /// « ici » follows where the reader stands: a work that comes round twice carries the
    /// mark on the stretch holding the volume being read, and on that one only. The walk then
    /// says what nothing else on the page can — where one is in the universe.
    void the_mark_lands_on_the_stretch_holding_the_volume_being_read()
    {
        serve(shelfOf({aSeries(u"elfes"_s, u"Elfes"_s, 29)}),
              QJsonArray{aWay(u"chrono"_s, u"Chronologique"_s,
                              {aStep(u"Elfes"_s, u"elfes"_s, 1, 7), aStep(u"Nains"_s),
                               aStep(u"Elfes"_s, u"elfes"_s, 8, 29)})});
        m_block->point(u"u-arran"_s, u"Terres d'Arran"_s, u"elfes"_s);
        settle();
        QVERIFY(hereAt(m_block->tiles(), 0));

        m_block->readAt(12);
        QVERIFY(!hereAt(m_block->tiles(), 0));
        QVERIFY(hereAt(m_block->tiles(), 2));

        // A volume no stretch covers leaves the mark where the walk was entered rather than
        // taking it off the block altogether.
        m_block->readAt(std::numeric_limits<double>::quiet_NaN());
        QVERIFY(hereAt(m_block->tiles(), 0));

        // The same place twice is not news, and a block that redrew on every answer the list
        // of volumes gives would redraw on all of them.
        m_block->readAt(12);
        QSignalSpy moved(m_block, &Elsewhere::changed);
        m_block->readAt(12);
        QCOMPARE(moved.size(), 0);
    }

    /// A universe that declares a way means it to be walked; opening on none would hide what
    /// it went to the trouble of saying.
    void the_first_way_is_chosen_when_none_is_marked()
    {
        serve(shelfOf({aSeries(u"elfes"_s, u"Elfes"_s, 29)}),
              QJsonArray{aWay(u"lecture"_s, u"De lecture"_s, {aStep(u"Nains"_s)}),
                         aWay(u"chrono"_s, u"Chronologique"_s, {aStep(u"Elfes"_s)})});
        m_block->point(u"u-arran"_s, u"Terres d'Arran"_s, u"elfes"_s);
        settle();

        QCOMPARE(m_block->orders().size(), 2);
        QCOMPARE(m_block->chosenOrder(), u"lecture"_s);
        QVERIFY(m_block->orders().at(0).toMap().value(u"chosen"_s).toBool());
    }

    void choosing_another_way_redraws_the_block_and_an_unknown_one_flattens_it()
    {
        serve(shelfOf({aSeries(u"elfes"_s, u"Elfes"_s, 29),
                       aSeries(u"nains"_s, u"Nains"_s, 26)}),
              QJsonArray{aWay(u"a"_s, u"Une"_s, {aStep(u"Nains"_s)}),
                         aWay(u"b"_s, u"Deux"_s, {aStep(u"Elfes"_s)})});
        m_block->point(u"u-arran"_s, u"Terres d'Arran"_s, u"elfes"_s);
        settle();
        QCOMPARE(m_block->chosenOrder(), u"a"_s);

        m_block->chooseOrder(u"b"_s);
        QCOMPARE(m_block->chosenOrder(), u"b"_s);
        QCOMPARE(nameAt(m_block->tiles(), 0), u"Elfes"_s);

        // Asking again for the one already chosen changes nothing.
        QSignalSpy moved(m_block, &Elsewhere::changed);
        m_block->chooseOrder(u"b"_s);
        QCOMPARE(moved.size(), 0);

        // An identifier naming no way is none, and none is the flat list.
        m_block->chooseOrder(u"nowhere"_s);
        QVERIFY(m_block->chosenOrder().isEmpty());
        QVERIFY(m_block->chosenName().isEmpty());
        QCOMPARE(m_block->tiles().size(), 1);
        QCOMPARE(nameAt(m_block->tiles(), 0), u"Nains"_s);
        QVERIFY(m_block->outside().isEmpty());
    }

    /// A refusal costs the block and not the page. A universe answering nothing looks exactly
    /// like a series belonging to none.
    void a_refusal_of_the_universe_leaves_the_block_empty_and_quiet()
    {
        m_pretend->answerFor = nullptr;
        m_pretend->answers(500, QByteArrayLiteral("{}"));
        m_block->point(u"u-arran"_s, u"Terres d'Arran"_s, u"elfes"_s);
        settle();

        QVERIFY(!m_block->loading());
        QVERIFY(m_block->tiles().isEmpty());
        QVERIFY(m_block->orders().isEmpty());
    }

    void a_refusal_of_the_ways_costs_the_ways_and_not_the_universe()
    {
        const QByteArray siblings = shelfOf({aSeries(u"elfes"_s, u"Elfes"_s, 29),
                                             aSeries(u"nains"_s, u"Nains"_s, 26)});
        const QByteArray universes = QJsonDocument(QJsonArray{
            QJsonObject{{u"id"_s, u"u-arran"_s},
                        {u"name"_s, u"Terres d'Arran"_s},
                        {u"orderCount"_s, 1}}}).toJson(QJsonDocument::Compact);
        m_pretend->answerFor = [siblings, universes](const QByteArray &request) -> QByteArray {
            if (request.contains("/orders "))
                return "HTTP/1.1 500 .\r\nContent-Length: 0\r\n\r\n";
            return answering(request.contains("GET /universes") ? universes : siblings);
        };
        m_block->point(u"u-arran"_s, u"Terres d'Arran"_s, u"elfes"_s);
        settle();

        QVERIFY(!m_block->loading());
        QCOMPARE(m_block->tiles().size(), 1);
        QVERIFY(m_block->orders().isEmpty());
    }

    void a_way_that_cannot_be_read_costs_itself_only()
    {
        serve(shelfOf({aSeries(u"elfes"_s, u"Elfes"_s, 29)}),
              QJsonArray{7, aWay(u"a"_s, u"Une"_s, {aStep(u"Nains"_s)})}, 2);
        m_block->point(u"u-arran"_s, u"Terres d'Arran"_s, u"elfes"_s);
        settle();

        QCOMPARE(m_block->orders().size(), 1);
        QCOMPARE(m_block->chosenOrder(), u"a"_s);
    }

    /// The headings of the tab are bound to the block, so the count beside the universe is
    /// the count the block is drawing — and it is absent while a walk names its own steps.
    void the_headings_count_what_the_block_draws()
    {
        SeriesCaptions words(nullptr, m_block);
        QCOMPARE(words.universeLine(), QString());

        serve(shelfOf({aSeries(u"elfes"_s, u"Elfes"_s, 29),
                       aSeries(u"nains"_s, u"Nains"_s, 26),
                       aSeries(u"orcs"_s, u"Orcs"_s, 22)}),
              QJsonArray{aWay(u"chrono"_s, u"Chronologique"_s, {aStep(u"Nains"_s)})}, 0);
        m_block->point(u"u-arran"_s, u"Terres d'Arran"_s, u"elfes"_s);
        settle();
        QCOMPARE(words.universeLine(), u"Terres d'Arran · 2 autres"_s);
        QCOMPARE(words.outsideLine(), QString());

        serve(shelfOf({aSeries(u"elfes"_s, u"Elfes"_s, 29),
                       aSeries(u"nains"_s, u"Nains"_s, 26),
                       aSeries(u"orcs"_s, u"Orcs"_s, 22)}),
              QJsonArray{aWay(u"chrono"_s, u"Chronologique"_s, {aStep(u"Nains"_s)})});
        m_block->point(u"u-arran"_s, u"Terres d'Arran"_s, u"elfes"_s);
        settle();
        QCOMPARE(words.universeLine(), u"Terres d'Arran"_s);
        QCOMPARE(words.outsideLine(), u"1 série"_s);
        // « 29 albums · ici » — the mark goes on the line the tile already has.
        QCOMPARE(words.hereToo(u"29 albums"_s), u"29 albums · ici"_s);
    }

    void a_block_with_no_server_offers_nothing()
    {
        Elsewhere orphan(nullptr);
        orphan.point(u"u"_s, u"Terres d'Arran"_s, u"elfes"_s);

        QVERIFY(!orphan.loading());
        QVERIFY(orphan.tiles().isEmpty());
        QCOMPARE(orphan.universe(), u"Terres d'Arran"_s);
    }
};

QTEST_MAIN(HoldsElsewhere)
#include "holds_elsewhere.moc"
