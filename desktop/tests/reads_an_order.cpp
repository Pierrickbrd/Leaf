// The reading orders of a universe.json, read the way the server reads them.
//
// The rules are those of `server/src/scan/orders.rs`: an included order is read in the place
// of the step that names it, a loop refuses every inclusion that closes it, and an order is
// held to the steps the server keeps. What is worth covering here is where the client and the
// server could part: an inclusion the client let through that the announcement then refuses,
// and an order the client wrote back that no longer said what its author had written.

#include "Fold.h"
#include "Orders.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

QJsonObject step(const QString &work, double from = -1, double to = -1, const QString &unit = {})
{
    QJsonObject one{{u"work"_s, work}};
    if (!unit.isEmpty())
        one.insert(u"unit"_s, unit);
    if (from >= 0)
        one.insert(u"from"_s, from);
    if (to >= 0)
        one.insert(u"to"_s, to);
    return one;
}

QJsonObject include(const QString &name)
{
    return QJsonObject{{u"include"_s, name}};
}

QJsonObject order(const QString &id, const QString &name, const QList<QJsonObject> &steps)
{
    QJsonObject one;
    if (!id.isEmpty())
        one.insert(u"id"_s, id);
    if (!name.isEmpty())
        one.insert(u"name"_s, name);
    QJsonArray written;
    for (const QJsonObject &each : steps)
        written << each;
    one.insert(u"steps"_s, written);
    return one;
}

Orders::Universe universe(const QList<QJsonObject> &orders)
{
    QJsonArray all;
    for (const QJsonObject &each : orders)
        all << each;
    return Orders::read(QJsonObject{{u"orders"_s, all}});
}

/// « Tout Aquilon » reads the whole of the saga, « Terres d’Arran » only the three series that
/// happen there, and the first has them written out again between two stretches of Lanfeust.
QList<QJsonObject> aquilon(const QList<QJsonObject> &everything)
{
    return {order(u"tout-aquilon"_s, u"Tout Aquilon"_s, everything),
            order(u"terres-d-arran"_s, u"Terres d'Arran"_s,
                  {step(u"Elfes"_s, 1, 4), step(u"Nains"_s, 1, 3), step(u"Mages"_s, 1, 3)})};
}

QList<QJsonObject> writtenAgain()
{
    return aquilon({step(u"Lanfeust"_s, 1, 4), step(u"Elfes"_s, 1, 4), step(u"Nains"_s, 1, 3),
                    step(u"Mages"_s, 1, 3), step(u"Lanfeust"_s, 5, 8),
                    step(u"Trolls"_s, 1, 12, u"CHAPTER"_s), step(u"Elfes"_s, 5, 6)});
}

QList<QJsonObject> included()
{
    return aquilon({step(u"Lanfeust"_s, 1, 4), include(u"terres-d-arran"_s),
                    step(u"Lanfeust"_s, 5, 8), step(u"Trolls"_s, 1, 12, u"CHAPTER"_s),
                    step(u"Elfes"_s, 5, 6)});
}

} // namespace

class ReadsAnOrder : public QObject {
    Q_OBJECT
private slots:
    /// Three steps written out again, in one piece, are the whole of another order: what a
    /// file written before inclusions existed looks like, and what the editor proposes to
    /// replace by an inclusion.
    void the_whole_of_another_order_written_again_is_a_copy()
    {
        const Orders::Universe u = universe(writtenAgain());
        const QList<Orders::Copy> copies = Orders::copies(u);
        QCOMPARE(copies.size(), 1);
        QCOMPARE(copies.constFirst().order, 0);
        QCOMPARE(copies.constFirst().from, 1);
        QCOMPARE(copies.constFirst().count, 3);
        QCOMPARE(copies.constFirst().of, 1);
        QCOMPARE(Orders::works(u, 0), 5);
        QCOMPARE(Orders::works(u, 1), 3);
    }

    /// Two steps that only look alike are not a copy: a range one volume shorter is another
    /// reading, and proposing to replace it would change what is read.
    void steps_that_differ_by_a_range_are_not_a_copy()
    {
        QList<QJsonObject> orders = writtenAgain();
        QJsonObject shorter = orders[0];
        QJsonArray steps = shorter.value(u"steps"_s).toArray();
        steps[2] = step(u"Nains"_s, 1, 2);
        shorter.insert(u"steps"_s, steps);
        orders[0] = shorter;

        QVERIFY(Orders::copies(universe(orders)).isEmpty());
    }

    void an_included_order_is_flattened_in_its_place()
    {
        const Orders::Universe u = universe(included());
        const std::optional<QList<Orders::Step>> flat = Orders::flatten(u, 0);
        QVERIFY(flat.has_value());
        QStringList works;
        for (const Orders::Step &one : *flat)
            works << one.work;
        QCOMPARE(works, (QStringList{u"Lanfeust"_s, u"Elfes"_s, u"Nains"_s, u"Mages"_s,
                                     u"Lanfeust"_s, u"Trolls"_s, u"Elfes"_s}));
        QCOMPARE(flat->at(5).unit, u"CHAPTER"_s);
        QCOMPARE(Orders::works(u, 0), 5);
        QVERIFY(Orders::refused(u).isEmpty());
        QVERIFY(Orders::copies(u).isEmpty());
    }

    /// The unit is folded as the server folds it: « volume » written by hand is the VOLUME it
    /// reads, and compared against a range written « VOLUME » it is the same step.
    void a_unit_is_read_in_capitals_whatever_its_case_in_the_file()
    {
        const Orders::Universe u = universe(
            {order(u"a"_s, u"A"_s, {step(u"X"_s, 1, 2, u"volume"_s)})});
        QCOMPARE(u.orders.constFirst().steps.constFirst().unit, u"VOLUME"_s);
    }

    /// An order that wrote no `id` is designated by its name, folded the way the server folds
    /// it — case and accents gone, an apostrophe joining — and an `id` is looked at first.
    /// The apostrophe separated here: « terres d arran » was an order the client found and
    /// the server, which reads « terres darran », did not.
    void an_order_without_an_id_is_found_by_its_name()
    {
        const Orders::Universe u = universe({order({}, u"Terres d’Arran"_s, {step(u"Elfes"_s)}),
                                             order(u"autre"_s, u"Terres d'Arran"_s,
                                                   {step(u"Nains"_s)}),
                                             order({}, u"Été"_s, {step(u"Mages"_s)})});
        QCOMPARE(Orders::declaredId(u.orders.at(0), 0), u"terres darran"_s);
        QCOMPARE(Orders::declaredId(u.orders.at(1), 1), u"autre"_s);
        QCOMPARE(Orders::resolve(u, u"terres-d-arran"_s), -1);
        QCOMPARE(Orders::resolve(u, u"terres darran"_s), 0);
        QCOMPARE(Orders::resolve(u, u"TERRES D’ARRAN"_s), 0);
        QCOMPARE(Orders::resolve(u, u"autre"_s), 1);
        QCOMPARE(Orders::resolve(u, u"ete"_s), 2);
        QCOMPARE(Orders::resolve(u, u"inconnu"_s), -1);
        QCOMPARE(Orders::resolve(u, u"  "_s), -1);
    }

    /// A name is folded as `search_key` folds it, the server's own cases: an apostrophe joins,
    /// a ligature is spelled out, any other mark separates, in any script.
    void a_name_is_folded_exactly_as_the_server_folds_it()
    {
        QCOMPARE(Fold::searchKey(u"La Rose écarlate"_s), u"la rose ecarlate"_s);
        QCOMPARE(Fold::searchKey(u"Haikyū !!"_s), u"haikyu"_s);
        QCOMPARE(Fold::searchKey(u"L'Attaque des Titans"_s), u"lattaque des titans"_s);
        QCOMPARE(Fold::searchKey(u"Terres d’Arran"_s), u"terres darran"_s);
        QCOMPARE(Fold::searchKey(u"Æon"_s), u"aeon"_s);
        QCOMPARE(Fold::searchKey(u"Œuvre"_s), u"oeuvre"_s);
        QCOMPARE(Fold::searchKey(u"Straße"_s), u"strasse"_s);
        QCOMPARE(Fold::searchKey(u"Ørsted"_s), u"orsted"_s);
        QCOMPARE(Fold::searchKey(u"Saga 🚀"_s), u"saga"_s);
        QCOMPARE(Fold::searchKey(u"Привет, Мир"_s), u"привет мир"_s);
        QCOMPARE(Fold::searchKey(u"ハイキュー!!"_s), u"ハイキュー"_s);
        QCOMPARE(Fold::searchKey(u"Chap.099 : Coup de sifflet"_s), u"chap 099 coup de sifflet"_s);
        QCOMPARE(Fold::searchKey(u"Jean-Claude Mézières"_s),
                 Fold::searchKey(u"Jean Claude Mezieres"_s));
        QCOMPARE(Fold::searchKey(u"e\u0301te\u0301"_s), u"ete"_s);
    }

    /// `"include": null` is no inclusion, as serde reads an absent key: the step is the work
    /// it names. Read as an inclusion carrying a work, it was refused, and the announcement
    /// with it.
    void a_step_whose_include_is_null_is_an_ordinary_step()
    {
        const Orders::Universe u = Orders::read(
            QJsonDocument::fromJson(
                R"({"orders":[{"id":"a","steps":[{"work":"X","include":null},
                   {"include":"b","work":null}]},{"id":"b","steps":[{"work":"Y"}]}]})")
                .object());
        QVERIFY(Orders::refused(u).isEmpty());
        const std::optional<QList<Orders::Step>> flat = Orders::flatten(u, 0);
        QVERIFY(flat.has_value());
        QCOMPARE(flat->size(), 2);
        QCOMPARE(flat->at(0).work, u"X"_s);
        QCOMPARE(flat->at(1).work, u"Y"_s);
    }

    /// An order with neither id nor name is its rank, as the server counts it.
    void an_order_with_neither_id_nor_name_is_its_rank()
    {
        const Orders::Universe u = universe({order({}, {}, {step(u"X"_s)})});
        QCOMPARE(Orders::declaredId(u.orders.constFirst(), 0), u"1"_s);
        QCOMPARE(Orders::resolve(u, u"1"_s), 0);
    }

    void including_an_order_that_includes_this_one_would_loop()
    {
        const Orders::Universe u = universe(included());
        // « Tout Aquilon » includes « Terres d’Arran »: including it back would close the loop.
        QVERIFY(Orders::wouldLoop(u, 1, 0));
        QVERIFY(!Orders::wouldLoop(u, 0, 1));
        QVERIFY(Orders::wouldLoop(u, 0, 0));
    }

    /// Which inclusion of a loop closes it depends on where one enters, so the server refuses
    /// each of them: a client that marked only the last one would show a half-read order the
    /// server reads another way.
    void a_loop_already_written_is_found_in_every_order_it_passes()
    {
        const Orders::Universe u = universe({order(u"a"_s, {}, {step(u"X"_s), include(u"b"_s)}),
                                             order(u"b"_s, {}, {include(u"c"_s)}),
                                             order(u"c"_s, {}, {include(u"a"_s), step(u"Y"_s)}),
                                             order(u"d"_s, {}, {include(u"a"_s)})});
        const QSet<std::pair<int, int>> loops = Orders::loops(u);
        QCOMPARE(loops, (QSet<std::pair<int, int>>{{0, 1}, {1, 0}, {2, 0}}));
        const QList<Orders::Refused> refused = Orders::refused(u);
        QCOMPARE(refused.size(), 3);
        for (const Orders::Refused &one : refused)
            QCOMPARE(one.why, Orders::Refusal::ClosesLoop);
        // What a loop refuses is skipped when read: the rest of each order still reads.
        QCOMPARE(Orders::flatten(u, 0)->size(), 1);
        QCOMPARE(Orders::flatten(u, 2)->size(), 1);
        // And an order that merely includes the loop reads what is left of it.
        QCOMPARE(Orders::flatten(u, 3)->size(), 1);
    }

    /// An order that includes itself is a loop of one.
    void an_order_that_includes_itself_is_a_loop()
    {
        const Orders::Universe u = universe({order(u"a"_s, {}, {include(u"a"_s)})});
        QCOMPARE(Orders::loops(u), (QSet<std::pair<int, int>>{{0, 0}}));
    }

    /// The announcement refuses these before a byte moves; the client says so first.
    void an_inclusion_naming_nothing_or_carrying_a_range_is_refused()
    {
        QJsonObject carrying = include(u"b"_s);
        carrying.insert(u"from"_s, 2);
        const Orders::Universe u = universe({order(u"a"_s, {}, {include(u"nowhere"_s), carrying}),
                                             order(u"b"_s, {}, {step(u"X"_s)})});
        const QList<Orders::Refused> refused = Orders::refused(u);
        QCOMPARE(refused.size(), 2);
        QCOMPARE(refused.at(0).step, 0);
        QCOMPARE(refused.at(0).why, Orders::Refusal::NamesNothing);
        QCOMPARE(refused.at(0).target, u"nowhere"_s);
        QCOMPARE(refused.at(1).step, 1);
        QCOMPARE(refused.at(1).why, Orders::Refusal::CarriesMore);
    }

    /// Fourteen orders each including the next twice are 8 192 steps from a few hundred
    /// bytes; the server stops an order at 4 096 and the client counts no further.
    void flattening_stops_at_the_ceiling()
    {
        QList<QJsonObject> orders;
        for (int at = 0; at < 14; ++at) {
            const QString next = u"o"_s + QString::number(at + 1);
            orders << (at == 13 ? order(u"o13"_s, {}, {step(u"X"_s)})
                                : order(u"o"_s + QString::number(at), {},
                                        {include(next), include(next)}));
        }
        const Orders::Universe u = universe(orders);
        QVERIFY(!Orders::flatten(u, 0).has_value());
        QVERIFY(Orders::flatten(u, 1).has_value());
        QCOMPARE(Orders::flatten(u, 1)->size(), Orders::MostFlatSteps);
        QCOMPARE(Orders::flatten(u, 2)->size(), 2048);
        QVERIFY(Orders::pastCeilings(u));
    }

    /// Together the orders of a universe are held to four orders' worth: two hundred orders
    /// each just under the ceiling are not four hundred thousand rows.
    void a_universe_past_its_steps_or_its_orders_is_said()
    {
        QList<QJsonObject> steps;
        for (int at = 0; at < Orders::MostFlatSteps; ++at)
            steps << step(u"X"_s);
        QList<QJsonObject> orders;
        for (int at = 0; at < 5; ++at)
            orders << order(u"o"_s + QString::number(at), {}, steps);
        QVERIFY(Orders::pastCeilings(universe(orders)));
        QVERIFY(!Orders::pastCeilings(universe(orders.mid(0, 4))));

        QList<QJsonObject> many;
        for (int at = 0; at <= Orders::MostOrders; ++at)
            many << order(u"o"_s + QString::number(at), {}, {step(u"X"_s)});
        QVERIFY(Orders::pastCeilings(universe(many)));
        QVERIFY(!Orders::pastCeilings(universe(many.mid(0, Orders::MostOrders))));
    }

    /// An order bearing the id of one written before it is disregarded by the scan before it
    /// is flattened, and costs the universe nothing: counted here, four orders under their
    /// ceiling and a fifth the server never reads were said to be past it.
    void an_order_disregarded_for_its_id_costs_the_universe_nothing()
    {
        QList<QJsonObject> steps;
        for (int at = 0; at < Orders::MostFlatSteps; ++at)
            steps << step(u"X"_s);
        QList<QJsonObject> orders;
        for (int at = 0; at < 4; ++at)
            orders << order(u"o"_s + QString::number(at), {}, steps);
        orders << order(u"o0"_s, u"Doublon"_s, steps);
        const Orders::Universe u = universe(orders);
        QVERIFY(!Orders::pastCeilings(u));
        const Orders::Plan plan(u);
        QVERIFY(plan.disregarded(4));
        QVERIFY(!plan.disregarded(0));
        QCOMPARE(plan.designated(u"o0"_s), 0);
    }

    /// The file is its author's: an order nobody touched goes back as it was read, keys no
    /// version of Leaf knows included, and not as a recomposition of what was understood.
    void an_order_left_alone_is_written_back_as_it_was_read()
    {
        QJsonObject first = order(u"a"_s, u"A"_s, {step(u"X"_s, 1, 2)});
        first.insert(u"note"_s, u"à garder"_s);
        QJsonArray steps = first.value(u"steps"_s).toArray();
        QJsonObject withKey = steps.at(0).toObject();
        withKey.insert(u"myKey"_s, 7);
        withKey.insert(u"unit"_s, u"volume"_s);
        steps[0] = withKey;
        first.insert(u"steps"_s, steps);
        const QJsonArray original{first, order(u"b"_s, {}, {step(u"Y"_s)})};

        const Orders::Universe u = Orders::read(QJsonObject{{u"orders"_s, original}});
        QCOMPARE(Orders::written(u), original);
    }

    /// An order that was edited keeps what it carried beyond what the editor knows, down to
    /// a step's own keys and the case it wrote its unit in.
    void an_edited_step_keeps_its_unknown_keys()
    {
        QJsonObject first = order(u"a"_s, u"A"_s, {step(u"X"_s, 1, 2, u"volume"_s)});
        first.insert(u"note"_s, u"à garder"_s);
        QJsonArray steps = first.value(u"steps"_s).toArray();
        QJsonObject withKey = steps.at(0).toObject();
        withKey.insert(u"myKey"_s, 7);
        steps[0] = withKey;
        first.insert(u"steps"_s, steps);
        Orders::Universe u = Orders::read(QJsonObject{{u"orders"_s, QJsonArray{first}}});

        u.orders[0].steps[0].to = 5;
        u.orders[0].steps[0].from.reset();
        u.orders[0].name = u"Autre nom"_s;
        u.orders[0].edited = true;

        const QJsonObject back = Orders::written(u).at(0).toObject();
        QCOMPARE(back.value(u"note"_s).toString(), u"à garder"_s);
        QCOMPARE(back.value(u"id"_s).toString(), u"a"_s);
        QCOMPARE(back.value(u"name"_s).toString(), u"Autre nom"_s);
        const QJsonObject one = back.value(u"steps"_s).toArray().at(0).toObject();
        QCOMPARE(one.value(u"myKey"_s).toInt(), 7);
        QCOMPARE(one.value(u"to"_s).toDouble(), 5.0);
        QVERIFY(!one.contains(u"from"_s));
        QCOMPARE(one.value(u"unit"_s).toString(), u"volume"_s);
        QCOMPARE(one.value(u"work"_s).toString(), u"X"_s);
    }

    void a_new_order_gets_an_id_no_other_order_has()
    {
        const Orders::Universe u = universe(aquilon({step(u"Lanfeust"_s)}));
        QCOMPARE(Orders::newId(u, u"Terres d'Arran"_s), u"terres-d-arran-2"_s);
        QCOMPARE(Orders::newId(u, u"Été à Troy"_s), u"ete-a-troy"_s);
        QCOMPARE(Orders::newId(u, u"  "_s), u"ordre"_s);
    }
};

QTEST_GUILESS_MAIN(ReadsAnOrder)
#include "reads_an_order.moc"
