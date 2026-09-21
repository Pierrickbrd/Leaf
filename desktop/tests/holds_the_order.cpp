// Turning one list of rows into another, as a decision about two lists of names.
//
// Headless and without Qt's models on purpose: the plan is the part that is easy to get
// wrong, and the part a model test cannot show you. A replacement that only ever inserts and
// deletes passes every test about *what* a shelf holds and still leaves a reordered shelf
// looking exactly as it did — which is the defect that started this file.

#include "Rearrange.h"

#include <QStringList>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

/// The plan, applied. What a model does with its own rows, done here to a list of names, so
/// a test can say what the reader would end up looking at.
QStringList applied(const QStringList &held, const QStringList &fresh)
{
    QStringList rows = held;
    for (const Rearrange::Step &step : Rearrange::plan(held, fresh)) {
        switch (step.kind) {
        case Rearrange::Step::Kind::Remove:
            rows.remove(step.first, step.last - step.first + 1);
            break;
        case Rearrange::Step::Kind::Move:
            rows.move(step.from, step.to);
            break;
        case Rearrange::Step::Kind::Insert:
            rows.insert(step.to, fresh.at(step.to));
            break;
        }
    }
    return rows;
}

int howMany(const QStringList &held, const QStringList &fresh, Rearrange::Step::Kind kind)
{
    int found = 0;
    for (const Rearrange::Step &step : Rearrange::plan(held, fresh)) {
        if (step.kind == kind)
            ++found;
    }
    return found;
}

} // namespace

class HoldsTheOrder : public QObject
{
    Q_OBJECT

private slots:
    /// Whatever the two lists are, applying the plan to one gives the other. Everything below
    /// is about *how* — this is the promise the rest refines.
    void the_plan_always_ends_on_the_list_that_was_asked_for()
    {
        const QList<QPair<QStringList, QStringList>> cases{
            {{}, {}},
            {{}, {u"a"_s, u"b"_s}},
            {{u"a"_s, u"b"_s}, {}},
            {{u"a"_s, u"b"_s, u"c"_s}, {u"c"_s, u"b"_s, u"a"_s}},
            {{u"a"_s, u"b"_s, u"c"_s}, {u"b"_s, u"c"_s, u"d"_s}},
            {{u"a"_s, u"b"_s, u"c"_s, u"d"_s}, {u"d"_s, u"a"_s}},
            {{u"a"_s}, {u"b"_s, u"a"_s, u"c"_s}},
            {{u"a"_s, u"b"_s, u"c"_s, u"d"_s, u"e"_s}, {u"e"_s, u"c"_s, u"a"_s}},
        };
        for (const auto &one : cases) {
            QCOMPARE(applied(one.first, one.second), one.second);
        }
    }

    /// The case a plan built only out of insertions and deletions gets wrong in the one way
    /// nobody sees: the right rows, in the right order, and every delegate rebuilt — so a
    /// shelf that turned round looks like a shelf that ignored the click.
    void the_same_rows_in_another_order_are_moved_and_never_rebuilt()
    {
        const QStringList held{u"a"_s, u"b"_s, u"c"_s};
        const QStringList reversed{u"c"_s, u"b"_s, u"a"_s};

        QCOMPARE(applied(held, reversed), reversed);
        QCOMPARE(howMany(held, reversed, Rearrange::Step::Kind::Remove), 0);
        QCOMPARE(howMany(held, reversed, Rearrange::Step::Kind::Insert), 0);
        QVERIFY(howMany(held, reversed, Rearrange::Step::Kind::Move) > 0);
    }

    /// A row already where it belongs is not touched at all. This is the whole value of the
    /// exercise: clicking a pill leaves the series that pill was never going to remove.
    void a_list_that_did_not_change_is_left_completely_alone()
    {
        const QStringList same{u"a"_s, u"b"_s, u"c"_s};
        QCOMPARE(Rearrange::plan(same, same).size(), 0);
    }

    /// Removals come out in runs. A filter dropping half a large shelf one row at a time is
    /// hundreds of signals, and every one of them makes a view re-lay out what is left.
    void rows_that_go_together_go_in_one_step()
    {
        const QStringList held{u"a"_s, u"b"_s, u"c"_s, u"d"_s, u"e"_s};
        const QStringList kept{u"a"_s, u"e"_s};

        QCOMPARE(applied(held, kept), kept);
        QCOMPARE(howMany(held, kept, Rearrange::Step::Kind::Remove), 1);
    }

    /// And separate runs stay separate, rather than one removal swallowing what sits between.
    void rows_with_a_survivor_between_them_go_separately()
    {
        const QStringList held{u"a"_s, u"b"_s, u"c"_s, u"d"_s, u"e"_s};
        const QStringList kept{u"c"_s};

        QCOMPARE(applied(held, kept), kept);
        QCOMPARE(howMany(held, kept, Rearrange::Step::Kind::Remove), 2);
    }

    void everything_new_is_inserted_and_everything_gone_is_removed()
    {
        const QStringList held{u"a"_s, u"b"_s};
        const QStringList fresh{u"c"_s, u"d"_s};

        QCOMPARE(applied(held, fresh), fresh);
        QCOMPARE(howMany(held, fresh, Rearrange::Step::Kind::Move), 0);
    }

    /// An id twice in one answer is a server doing something it should not. The plan still
    /// ends on a list the model can hold rather than one whose count disagrees with its rows:
    /// a short list is recoverable, a model that lies about its own size is not.
    void an_id_the_server_repeated_does_not_leave_a_model_disagreeing_with_itself()
    {
        const QStringList held{u"a"_s, u"b"_s};
        const QStringList repeated{u"a"_s, u"a"_s};

        const QStringList ended = applied(held, repeated);
        QVERIFY2(ended.size() <= repeated.size(), "more rows than were asked for");
        for (const QString &one : ended)
            QVERIFY(repeated.contains(one));
    }
};

QTEST_MAIN(HoldsTheOrder)
#include "holds_the_order.moc"
