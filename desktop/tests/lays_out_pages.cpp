// How a volume's pages are shared out between screens: alone, or two by two around the doubles.

#include "Spreads.h"

#include <QTest>

#include <set>

using namespace Qt::StringLiterals;

namespace {

QList<Api::PageInfo> pagesOf(int count, const std::set<int> &spreads = {})
{
    QList<Api::PageInfo> pages;
    for (int i = 0; i < count; ++i) {
        Api::PageInfo page;
        // From 0, as the server numbers a page and as `Spreads` answers with indexes.
        page.number = i;
        page.spread = spreads.count(i) > 0;
        pages.append(page);
    }
    return pages;
}

// "0 | 1 2 | 3" so that a failure prints the whole layout and not one index of it.
QString shown(const QList<Spreads::Display> &displays)
{
    QStringList screens;
    for (const auto &display : displays)
        screens << (display.second ? QString::number(display.first) + u' ' + QString::number(*display.second)
                                   : QString::number(display.first));
    return screens.join(u" | ");
}

} // namespace

class LaysOutPages : public QObject
{
    Q_OBJECT
private slots:
    void single_shows_every_page_alone()
    {
        QCOMPARE(shown(Spreads::lay(pagesOf(5), Spreads::Layout::Single)), u"0 | 1 | 2 | 3 | 4"_s);
    }

    void single_ignores_spreads()
    {
        QCOMPARE(shown(Spreads::lay(pagesOf(4, {2}), Spreads::Layout::Single)), u"0 | 1 | 2 | 3"_s);
    }

    void the_cover_is_alone_and_pairs_start_at_the_second_page()
    {
        QCOMPARE(shown(Spreads::lay(pagesOf(6), Spreads::Layout::Double)), u"0 | 1 2 | 3 4 | 5"_s);
    }

    void a_spread_stands_alone_and_pairs_start_again_after_it()
    {
        QCOMPARE(shown(Spreads::lay(pagesOf(7, {3}), Spreads::Layout::Double)),
                 u"0 | 1 2 | 3 | 4 5 | 6"_s);
    }

    void a_page_left_alone_before_a_spread_is_shown_alone()
    {
        QCOMPARE(shown(Spreads::lay(pagesOf(5, {2}), Spreads::Layout::Double)), u"0 | 1 | 2 | 3 4"_s);
    }

    void a_cover_that_is_a_spread_is_still_alone()
    {
        QCOMPARE(shown(Spreads::lay(pagesOf(4, {0}), Spreads::Layout::Double)), u"0 | 1 2 | 3"_s);
    }

    void no_pages_make_no_displays()
    {
        QVERIFY(Spreads::lay({}, Spreads::Layout::Double).isEmpty());
        QVERIFY(Spreads::lay({}, Spreads::Layout::Single).isEmpty());
    }

    void the_page_recorded_for_a_pair_is_its_second()
    {
        QCOMPARE(Spreads::lastPageOf({1, 2}), 2);
        QCOMPARE(Spreads::lastPageOf({4, std::nullopt}), 4);
    }

    void a_page_is_found_in_the_display_that_holds_it()
    {
        const auto displays = Spreads::lay(pagesOf(6), Spreads::Layout::Double);
        QCOMPARE(Spreads::displayOf(displays, 0), 0);
        QCOMPARE(Spreads::displayOf(displays, 1), 1);
        QCOMPARE(Spreads::displayOf(displays, 2), 1);
        QCOMPARE(Spreads::displayOf(displays, 5), 3);
    }

    void a_page_outside_the_volume_belongs_to_no_display()
    {
        const auto displays = Spreads::lay(pagesOf(6), Spreads::Layout::Double);
        QCOMPARE(Spreads::displayOf(displays, 6), -1);
        QCOMPARE(Spreads::displayOf(displays, -1), -1);
        QCOMPARE(Spreads::displayOf({}, 0), -1);
    }

    // 190 pages and 14 doubles, the volume of Haikyu that made the rule matter.
    void every_page_belongs_to_exactly_one_display()
    {
        std::set<int> spreads;
        for (int i = 0; i < 14; ++i)
            spreads.insert(7 + i * 13);
        const auto pages = pagesOf(190, spreads);

        for (const auto layout : {Spreads::Layout::Single, Spreads::Layout::Double}) {
            const auto displays = Spreads::lay(pages, layout);
            QList<int> seen(190, 0);
            for (const auto &display : displays) {
                ++seen[display.first];
                if (display.second)
                    ++seen[*display.second];
                if (display.second) {
                    QCOMPARE(*display.second, display.first + 1);
                    QVERIFY(!pages[display.first].spread && !pages[*display.second].spread);
                }
            }
            for (int page = 0; page < 190; ++page) {
                QCOMPARE(seen[page], 1);
                QVERIFY(Spreads::displayOf(displays, page) >= 0);
            }
        }
    }
};

QTEST_MAIN(LaysOutPages)
#include "lays_out_pages.moc"
