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
        // while the narrow band is always two and the wide one stops at eight: a wider
        // window gives each cover more room instead of turning the shelf into thumbnails.
        QCOMPARE(Widths::shelfColumnsFor(0), 2);
        QCOMPARE(Widths::shelfColumnsFor(599), 2);
        QCOMPARE(Widths::shelfColumnsFor(600), 4);
        QCOMPARE(Widths::shelfColumnsFor(787), 4);
        QCOMPARE(Widths::shelfColumnsFor(788), 5);
        QCOMPARE(Widths::shelfColumnsFor(1099), 5);
        QCOMPARE(Widths::shelfColumnsFor(1100), 7);
        QCOMPARE(Widths::shelfColumnsFor(1249), 7);
        QCOMPARE(Widths::shelfColumnsFor(1250), 8);
        QCOMPARE(Widths::shelfColumnsFor(4000), 8);
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
    /// A mark on a cover follows the cover, within bounds: a speck at 22 on a cover three
    /// hundred wide went unseen, and a sixth of the cover up to 40 hid too much of it.
    void a_mark_on_a_cover_follows_the_cover_between_two_bounds()
    {
        QCOMPARE(Widths::markOn(Widths::MinimumCoverWidth), 22);
        QCOMPARE(Widths::markOn(208), 26);
        QCOMPARE(Widths::markOn(300), 30);
        QCOMPARE(Widths::markOn(600), 30);
    }

    /// The reader's controls follow the window both ways, within bounds: tried by hand on
    /// 22/10/2026 on a window of 1920 — a 2560 screen scaled by 1.333 — the dock was a strip
    /// of specks at its drawn size and too large at half as large again; a quarter there. On
    /// a small window its words kept their drawn size beside a page that had shrunk.
    void the_controls_follow_the_window_between_085_and_15()
    {
        QCOMPARE(Widths::controlsFor(400), 0.85);
        QCOMPARE(Widths::controlsFor(900), 0.85);
        QCOMPARE(Widths::controlsFor(1090), 0.925);
        QCOMPARE(Widths::controlsFor(1280), 1.0);
        QCOMPARE(Widths::controlsFor(1920), 1.25);
        QCOMPARE(Widths::controlsFor(2560), 1.5);
        QCOMPARE(Widths::controlsFor(3840), 1.5);
        QCOMPARE(Widths::controlsFor(7680), 1.5);

        Widths widths;
        QSignalSpy said(&widths, &Widths::windowChanged);
        widths.setWindow(1920);
        QCOMPARE(widths.controls(), 1.25);
        QCOMPARE(said.count(), 1);
    }

    /// Lengths follow the factor; words follow it down within their floors and never up — the
    /// the test of 22/10/2026 asked for words that follow a small window and none
    /// illegible on it, and the one of 23/10/2026 at 1920 found every word grown with the
    /// window too big.
    void words_shrink_with_the_window_and_never_grow()
    {
        QCOMPARE(Scale::sizeFor(26, 1.0), 26);
        QCOMPARE(Scale::sizeFor(26, 1.25), 33);
        QCOMPARE(Scale::sizeFor(26, 0.85), 22);

        // At the drawn window, every word at its drawn size, whatever its floor.
        QCOMPARE(Scale::captionFor(10, 1.0), 10);
        QCOMPARE(Scale::bodyFor(12, 1.0), 12);
        QCOMPARE(Scale::titleFor(17, 1.0), 17);
        // At its drawn size still on a large window, where they used to grow by the factor.
        for (const int window : {1920, 2560, 3840}) {
            const qreal factor = Widths::controlsFor(window);
            QCOMPARE(Scale::captionFor(12, factor), 12);
            QCOMPARE(Scale::bodyFor(14, factor), 14);
            QCOMPARE(Scale::titleFor(20, factor), 20);
            QCOMPARE(Scale::lettersFor(1.4, factor), 1.4);
        }
        // Smaller on a small one, down to the floor and never under it.
        QCOMPARE(Scale::captionFor(13, 0.85), 11);
        QCOMPARE(Scale::captionFor(12, 0.85), 11);
        QCOMPARE(Scale::bodyFor(14, 0.85), 13);
        QCOMPARE(Scale::bodyFor(15, 0.85), 13);
        QCOMPARE(Scale::lettersFor(2, 0.85), 1.7);
        // A title gives twice as much, and stops at 15.
        QCOMPARE(Scale::titleFor(28, 0.85), 20);
        QCOMPARE(Scale::titleFor(19, 0.85), 15);
        QCOMPARE(Scale::titleFor(28, 0.925), 24);

        // Through the singleton's object, for its window, and told when it changes.
        Widths widths;
        widths.setWindow(1920);
        QCOMPARE(widths.scale()->size(640), 800);
        QCOMPARE(widths.scale()->body(14), 14);
        QCOMPARE(widths.scale()->caption(12), 12);
        QCOMPARE(widths.scale()->title(20), 20);
        QSignalSpy said(&widths, &Widths::windowChanged);
        widths.setWindow(900);
        QCOMPARE(said.count(), 1);
        QCOMPARE(widths.scale()->caption(12), 11);
        QCOMPARE(widths.scale()->body(14), 13);
        QCOMPARE(widths.scale()->title(17), 15);
    }

    /// Icons and the width of a card of words grow at half a length's rate, the gaps of a bar
    /// at twice it; under 1 all three shrink as a length does. On 23/10/2026 at 1920 the keys
    /// stood larger than the words they name, and the floating bar gained no room between its
    /// buttons that anybody could see.
    void icons_grow_at_half_the_rate_and_gaps_at_twice_it()
    {
        Widths widths;
        widths.setWindow(1920);
        QCOMPARE(widths.scale()->size(20), 25);
        QCOMPARE(widths.scale()->icon(20), 23);
        QCOMPARE(widths.scale()->column(640), 720);
        QCOMPARE(widths.scale()->room(4), 6);
        QCOMPARE(widths.scale()->room(14), 21);
        widths.setWindow(2560);
        QCOMPARE(widths.scale()->icon(20), 25);
        QCOMPARE(widths.scale()->room(4), 8);
        widths.setWindow(1280);
        QCOMPARE(widths.scale()->icon(20), 20);
        QCOMPARE(widths.scale()->room(9), 9);
        widths.setWindow(900);
        QCOMPARE(widths.scale()->icon(20), 17);
        QCOMPARE(widths.scale()->column(380), 323);
        QCOMPARE(widths.scale()->room(14), 12);
    }

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
