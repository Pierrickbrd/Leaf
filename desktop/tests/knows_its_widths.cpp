// Where the three widths break.
//
// Declared in one place because the grid and the reader have to agree on where the break
// falls; asserted at the edges because a breakpoint is wrong by one pixel and never by a
// hundred.

#include "Widths.h"

#include <QSignalSpy>
#include <QTest>

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
};

QTEST_APPLESS_MAIN(KnowsItsWidths)
#include "knows_its_widths.moc"
