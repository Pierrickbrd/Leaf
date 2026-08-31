// Which screen we are on, and what is refused.
//
// It lives in C++ so this test can reach it: every test in this block runs without a window,
// and a StackView's state would be reachable by none of them. The same argument took `boot.rs`
// out of `main` on the server — what a test cannot see is not covered.

#include "Navigation.h"

#include <QSignalSpy>
#include <QTest>
#include <QVariantMap>

using Qt::Literals::StringLiterals::operator""_s;

class FindsItsWayBack : public QObject
{
    Q_OBJECT

private slots:
    void it_starts_on_the_shelf_which_is_the_home()
    {
        Navigation navigation;
        QCOMPARE(navigation.destination(), Navigation::Destination::Shelf);
        QVERIFY(!navigation.canGoBack());
    }

    void opening_a_series_then_going_back_returns_to_the_shelf()
    {
        Navigation navigation;
        QVERIFY(navigation.open(Navigation::Destination::Series,
                                {{u"series"_s, u"dragon-ball"_s}}));
        QCOMPARE(navigation.destination(), Navigation::Destination::Series);
        QCOMPARE(navigation.parameters().value(u"series"_s).toString(), u"dragon-ball"_s);
        QVERIFY(navigation.canGoBack());

        navigation.back();
        QCOMPARE(navigation.destination(), Navigation::Destination::Shelf);
        QVERIFY(!navigation.canGoBack());
    }

    /// Health and Settings are dead ends one comes back from — to wherever one was.
    void escape_on_a_dead_end_returns_where_one_came_from()
    {
        Navigation navigation;
        QVERIFY(navigation.open(Navigation::Destination::Series,
                                {{u"series"_s, u"berserk"_s}}));
        QVERIFY(navigation.open(Navigation::Destination::Settings));
        navigation.back();
        QCOMPARE(navigation.destination(), Navigation::Destination::Series);
        QCOMPARE(navigation.parameters().value(u"series"_s).toString(), u"berserk"_s);
    }

    void going_back_from_the_root_does_nothing_rather_than_emptying_the_screen()
    {
        Navigation navigation;
        QSignalSpy said(&navigation, &Navigation::changed);
        navigation.back();
        QCOMPARE(navigation.destination(), Navigation::Destination::Shelf);
        QCOMPARE(said.count(), 0);
    }

    void a_series_with_no_identifier_is_refused_rather_than_shown_empty()
    {
        Navigation navigation;
        QSignalSpy said(&navigation, &Navigation::changed);
        QVERIFY(!navigation.open(Navigation::Destination::Series));
        QVERIFY(!navigation.open(Navigation::Destination::Series, {{u"series"_s, u""_s}}));
        QCOMPARE(navigation.destination(), Navigation::Destination::Shelf);
        QCOMPARE(said.count(), 0);
    }

    void a_reader_with_no_entry_is_refused_too()
    {
        Navigation navigation;
        QVERIFY(!navigation.open(Navigation::Destination::Reader));
        QCOMPARE(navigation.destination(), Navigation::Destination::Shelf);
    }

    /// QML hands an int across; a value outside the enumeration must not become a screen.
    void a_destination_outside_the_enumeration_is_refused()
    {
        Navigation navigation;
        QVERIFY(!navigation.open(static_cast<Navigation::Destination>(42)));
        QCOMPARE(navigation.destination(), Navigation::Destination::Shelf);
    }

    /// The tests above all refuse against an empty stack, where "the parameters were
    /// preserved" and "the parameters happened to already be empty" look identical. `open`
    /// performs no mutation before either guard, so this is correct by construction — but
    /// nothing exercised it against a stack already carrying something to lose.
    void a_refusal_leaves_a_non_empty_stack_exactly_as_it_was()
    {
        Navigation navigation;
        QVERIFY(navigation.open(Navigation::Destination::Series,
                                {{u"series"_s, u"vinland-saga"_s}}));

        const Navigation::Destination destination = navigation.destination();
        const QVariantMap parameters = navigation.parameters();
        const bool canGoBack = navigation.canGoBack();

        QVERIFY(!navigation.open(Navigation::Destination::Series));
        QCOMPARE(navigation.destination(), destination);
        QCOMPARE(navigation.parameters(), parameters);
        QCOMPARE(navigation.canGoBack(), canGoBack);

        QVERIFY(!navigation.open(static_cast<Navigation::Destination>(42)));
        QCOMPARE(navigation.destination(), destination);
        QCOMPARE(navigation.parameters(), parameters);
        QCOMPARE(navigation.canGoBack(), canGoBack);
    }
};

QTEST_APPLESS_MAIN(FindsItsWayBack)
#include "finds_its_way_back.moc"
