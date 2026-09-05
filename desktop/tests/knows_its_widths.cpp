// Where the three widths break.
//
// Declared in one place because the grid and the reader have to agree on where the break
// falls; asserted at the edges because a breakpoint is wrong by one pixel and never by a
// hundred.

#include "Widths.h"

#include <QSignalSpy>
#include <QTest>

using Qt::Literals::StringLiterals::operator""_s;

class KnowsItsWidths : public QObject
{
    Q_OBJECT

private slots:
    void the_edges_of_the_three_bands()
    {
        QCOMPARE(Widths::bandFor(0), Widths::Band::Narrow);
        QCOMPARE(Widths::bandFor(599), Widths::Band::Narrow);
        QCOMPARE(Widths::bandFor(600), Widths::Band::Medium);
        QCOMPARE(Widths::bandFor(1099), Widths::Band::Medium);
        QCOMPARE(Widths::bandFor(1100), Widths::Band::Wide);
        QCOMPARE(Widths::bandFor(4000), Widths::Band::Wide);
    }

    /// A window has no negative width, but an unset one arrives as zero or less.
    void a_width_below_zero_is_the_narrow_band_and_not_a_crash()
    {
        QCOMPARE(Widths::bandFor(-1), Widths::Band::Narrow);
    }

    void the_band_follows_the_window()
    {
        Widths widths;
        widths.setWindow(1200);
        QCOMPARE(widths.band(), Widths::Band::Wide);
        widths.setWindow(800);
        QCOMPARE(widths.band(), Widths::Band::Medium);
    }

    void the_shelf_columns_follow_the_three_width_rules()
    {
        // Section 09 gives the bands; the 140 px floor and the 14 px gutter decide the
        // changes *inside* them. The medium band is deliberately clamped to four then five,
        // while the narrow band is always two and the wide one keeps growing.
        QCOMPARE(Widths::shelfColumnsFor(0), 2);
        QCOMPARE(Widths::shelfColumnsFor(599), 2);
        QCOMPARE(Widths::shelfColumnsFor(600), 4);
        QCOMPARE(Widths::shelfColumnsFor(787), 4);
        QCOMPARE(Widths::shelfColumnsFor(788), 5);
        QCOMPARE(Widths::shelfColumnsFor(1099), 5);
        QCOMPARE(Widths::shelfColumnsFor(1100), 7);
        QCOMPARE(Widths::shelfColumnsFor(1249), 7);
        QCOMPARE(Widths::shelfColumnsFor(1250), 8);
    }

    void the_shelf_column_property_follows_a_resize_inside_one_band()
    {
        // Four to five happens without crossing a band, so `shelfColumns` must notify with
        // the window itself and never depend on the band-only `changed` signal.
        Widths widths;
        widths.setWindow(700);
        QCOMPARE(widths.shelfColumns(), 4);
        widths.setWindow(800);
        QCOMPARE(widths.shelfColumns(), 5);
        QCOMPARE(widths.band(), Widths::Band::Medium);
    }

    /// A resize that does not cross a break must not make every screen rebind — but the width
    /// itself must still be the one just set, not a value `setWindow` quietly declined to
    /// store while only the band's own signal stayed silent.
    void a_resize_inside_one_band_says_nothing()
    {
        Widths widths;
        widths.setWindow(1200);
        QSignalSpy said(&widths, &Widths::changed);
        widths.setWindow(1300);
        QCOMPARE(said.count(), 0);
        QCOMPARE(widths.window(), 1300);
        widths.setWindow(800);
        QCOMPARE(said.count(), 1);
    }

    /// `windowChanged` is `window`'s own NOTIFY signal — a screen bound to `Widths.window`
    /// relies on it firing every time the value does, band crossed or not.
    void window_changed_fires_on_every_real_change_of_the_width()
    {
        Widths widths;
        widths.setWindow(1200);
        QSignalSpy said(&widths, &Widths::windowChanged);
        widths.setWindow(1300);
        QCOMPARE(said.count(), 1);
        widths.setWindow(1300);
        QCOMPARE(said.count(), 1);
        widths.setWindow(800);
        QCOMPARE(said.count(), 2);
    }

    /// `bandLabel` is read only from QML today — the card's second line in `Main.qml` — so
    /// nothing here had called it before.
    void the_band_label_is_the_bands_own_name_in_french()
    {
        Widths widths;
        widths.setWindow(1200);
        QCOMPARE(widths.bandLabel(), u"Large"_s);
        widths.setWindow(800);
        QCOMPARE(widths.bandLabel(), u"Moyenne"_s);
        widths.setWindow(200);
        QCOMPARE(widths.bandLabel(), u"Étroite"_s);
    }
};

QTEST_APPLESS_MAIN(KnowsItsWidths)
#include "knows_its_widths.moc"
