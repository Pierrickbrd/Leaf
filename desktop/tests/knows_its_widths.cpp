// Where the three widths break.
//
// Declared in one place because the grid and the reader have to agree on where the break
// falls; asserted at the edges because a breakpoint is wrong by one pixel and never by a
// hundred.

#include "Widths.h"

#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QTest>

#include <cmath>
#include <memory>

using Qt::Literals::StringLiterals::operator""_s;

namespace {

/// A desktop whose text size the test sets, as someone moves the slider in its settings.
class AnyText : public SystemText
{
public:
    explicit AnyText(qreal factor) : m_factor(factor) {}
    qreal factor() const override { return m_factor; }
    void becomes(qreal factor)
    {
        m_factor = factor;
        emit changed();
    }

private:
    qreal m_factor;
};

} // namespace

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
        // The box a word stands in keeps the word's proportions: it never grows with the
        // window, however wide, and follows the desktop's text size as the word does. The
        // reader's pills stood 45 high round a body of 14 at 1920 because theirs was a length.
        for (const int window : {1280, 1920, 2560, 3840})
            QCOMPARE(Scale::fitFor(36, Widths::controlsFor(window)), 36);
        QCOMPARE(Scale::fitFor(36, 0.85), 31);
        QCOMPARE(Scale::fitFor(36, 1.25, 1.25), 45);
        QCOMPARE(Scale::fitFor(36, 0.85, 0.8), 24);
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

    /// The one word that grows, for the three places the owner allowed on 23/10/2026: as a
    /// title under 1, at half a length's rate above it, and never past a quarter more.
    void the_subject_of_a_screen_grows_at_half_the_rate_and_no_further_than_a_quarter()
    {
        QCOMPARE(Scale::displayFor(36, Widths::controlsFor(900)), 25);
        QCOMPARE(Scale::displayFor(36, Widths::controlsFor(1280)), 36);
        QCOMPARE(Scale::displayFor(36, Widths::controlsFor(1920)), 41);
        QCOMPARE(Scale::displayFor(36, Widths::controlsFor(2560)), 45);
        QCOMPARE(Scale::displayFor(30, Widths::controlsFor(1920)), 34);
        // A title's floor under 1, and a quarter more as the ceiling whatever the factor.
        QCOMPARE(Scale::displayFor(20, 0.85), 15);
        QCOMPARE(Scale::displayFor(36, 3.0), 45);

        Widths widths;
        widths.setWindow(1920);
        QCOMPARE(widths.scale()->display(36), 41);
        QCOMPARE(widths.scale()->title(36), 36);
    }

    /// Words follow the desktop's text size, asked for on 23/10/2026: the window's floors
    /// apply after it and are never scaled by it, and the ceiling is the drawn size times it.
    void words_follow_the_desktops_text_size_with_the_floors_after_it()
    {
        // Larger: as large again as asked at the drawn window, and no larger on a large one.
        QCOMPARE(Scale::bodyFor(14, 1.0, 1.25), 18);
        QCOMPARE(Scale::bodyFor(14, Widths::controlsFor(1920), 1.25), 18);
        QCOMPARE(Scale::captionFor(12, Widths::controlsFor(2560), 1.5), 18);
        QCOMPARE(Scale::titleFor(20, Widths::controlsFor(1920), 1.5), 30);
        // A small window still shrinks them, and the floor is the window's, not 1.25 × it.
        QCOMPARE(Scale::bodyFor(14, 0.85, 1.25), 15);
        QCOMPARE(Scale::captionFor(12, 0.85, 1.5), 15);
        QCOMPARE(Scale::titleFor(20, 0.85, 1.25), 18);
        QCOMPARE(Scale::titleFor(19, 0.85, 1.5), 20);
        // Smaller, asked for on purpose: the floor never lifts a word back over what was
        // asked, and the window shrinks it no further than the floor.
        QCOMPARE(Scale::bodyFor(14, 1.0, 0.8), 11);
        QCOMPARE(Scale::bodyFor(14, 0.85, 0.8), 11);
        QCOMPARE(Scale::captionFor(12, 0.85, 0.8), 10);
        QCOMPARE(Scale::bodyFor(20, 0.85, 0.8), 14);
        // The one word that grows, grows with it too.
        QCOMPARE(Scale::displayFor(36, Widths::controlsFor(1920), 1.5), 61);
        QCOMPARE(Scale::displayFor(36, Widths::controlsFor(2560), 2.0), 90);
        // Its spacing belongs to the words.
        QCOMPARE(Scale::lettersFor(2, 1.0, 1.5), 3.0);
        // Lengths, icons and gaps do not: a screen at 200 % is already doubled by Qt.
        Widths widths;
        AnyText text(1.5);
        widths.follow(&text);
        widths.setWindow(1280);
        QCOMPARE(widths.scale()->size(26), 26);
        QCOMPARE(widths.scale()->icon(20), 20);
        QCOMPARE(widths.scale()->room(14), 14);
        QCOMPARE(widths.scale()->body(14), 21);
    }

    /// A card of words widens with them: at 380 round words half as large again, every line
    /// of the reader's cards would wrap in two.
    void a_card_of_words_widens_with_the_desktops_text_size()
    {
        QCOMPARE(Scale::columnFor(640, Widths::controlsFor(1920), 1.5), 1080);
        QCOMPARE(Scale::columnFor(380, Widths::controlsFor(900), 0.8), 258);
        QCOMPARE(Scale::columnFor(640, Widths::controlsFor(1280), 1.25), 800);

        Widths widths;
        AnyText text(1.25);
        widths.follow(&text);
        widths.setWindow(1920);
        QCOMPARE(widths.scale()->column(640), 900);
    }

    /// A setting typed by hand is held to what a layout survives, and nonsense from the
    /// desktop is no setting at all.
    void the_desktops_text_size_is_held_between_08_and_2()
    {
        QCOMPARE(Widths::textFor(1.25), 1.25);
        QCOMPARE(Widths::textFor(0.5), 0.8);
        QCOMPARE(Widths::textFor(3.0), 2.0);
        QCOMPARE(Widths::textFor(0), 1.0);
        QCOMPARE(Widths::textFor(-2), 1.0);
        QCOMPARE(Widths::textFor(std::nan("")), 1.0);

        Widths widths;
        QCOMPARE(widths.systemText(), 1.0);
        AnyText text(4.0);
        widths.follow(&text);
        QCOMPARE(widths.systemText(), 2.0);
        widths.follow(nullptr);
        QCOMPARE(widths.systemText(), 1.0);
    }

    /// The setting moved while the application was open, and every `Widths.scale` binding
    /// takes it at once — through a real binding, since a binding re-reads a call only when
    /// a property it read says it changed.
    void a_text_size_changed_on_the_desktop_reaches_every_binding_live()
    {
        Widths widths;
        widths.setWindow(1280);
        AnyText text(1.0);
        widths.follow(&text);

        QQmlEngine engine;
        engine.rootContext()->setContextProperty(u"widths"_s, &widths);
        QQmlComponent component(&engine);
        component.setData("import QtQml\nQtObject {\n"
                          "    property int body: widths.scale.body(14)\n"
                          "    property int column: widths.scale.column(400)\n"
                          "}\n",
                          QUrl());
        std::unique_ptr<QObject> bound(component.create());
        QVERIFY2(bound, qPrintable(component.errorString()));
        QCOMPARE(bound->property("body").toInt(), 14);
        QCOMPARE(bound->property("column").toInt(), 400);

        QSignalSpy sized(&widths, &Widths::systemTextChanged);
        QSignalSpy scale(&widths, &Widths::scaleChanged);
        text.becomes(1.5);
        QCOMPARE(sized.count(), 1);
        QCOMPARE(scale.count(), 1);
        QCOMPARE(bound->property("body").toInt(), 21);
        QCOMPARE(bound->property("column").toInt(), 600);

        // The same size said again changes nothing and rebinds nothing.
        text.becomes(1.5);
        QCOMPARE(scale.count(), 1);
        // A window dragged wider still rebinds through the same signal.
        widths.setWindow(900);
        QCOMPARE(scale.count(), 2);
        QCOMPARE(bound->property("body").toInt(), 18);
        // A source let go of is no longer heard.
        widths.follow(nullptr);
        text.becomes(2.0);
        QCOMPARE(widths.systemText(), 1.0);
    }

    /// Each gap by its role, at the smallest window, the window it was drawn for and the
    /// owner's. The first thing on a page stood 16, 14, 12, 11 or 9 under the bar on
    /// 23/10/2026, three of them literals that stayed put while the others grew: one number
    /// per role, and one law per number, is what keeps them from drifting apart again.
    void the_space_between_things_is_named_by_its_role()
    {
        Widths widths;
        const Space *space = widths.space();

        widths.setWindow(Widths::MinimumWindowWidth);
        QCOMPARE(space->gutter(), 14);
        QCOMPARE(space->top(), 14);
        QCOMPARE(space->section(), 24);
        QCOMPARE(space->block(), 12);
        QCOMPARE(space->heading(), 7);
        QCOMPARE(space->card(), 14);
        QCOMPARE(space->compact(), 7);
        QCOMPARE(space->row(), 9);
        QCOMPARE(space->label(), 7);
        QCOMPARE(space->pill(), 5);
        QCOMPARE(space->tight(), 2);
        QCOMPARE(space->column(), 935);

        widths.setWindow(1280);
        QCOMPARE(space->gutter(), 16);
        QCOMPARE(space->top(), 16);
        QCOMPARE(space->section(), 28);
        QCOMPARE(space->block(), 14);
        QCOMPARE(space->heading(), 8);
        QCOMPARE(space->card(), 16);
        QCOMPARE(space->compact(), 8);
        QCOMPARE(space->row(), 10);
        QCOMPARE(space->label(), 8);
        QCOMPARE(space->pill(), 6);
        QCOMPARE(space->tight(), 2);
        QCOMPARE(space->column(), 1100);

        // Gaps at twice a length's rate, paddings at a length's, the column at half of it.
        widths.setWindow(1920);
        QCOMPARE(space->gutter(), 24);
        QCOMPARE(space->top(), 24);
        QCOMPARE(space->section(), 42);
        QCOMPARE(space->block(), 21);
        QCOMPARE(space->heading(), 12);
        QCOMPARE(space->card(), 20);
        QCOMPARE(space->compact(), 10);
        QCOMPARE(space->row(), 15);
        QCOMPARE(space->label(), 12);
        QCOMPARE(space->pill(), 9);
        QCOMPARE(space->tight(), 3);
        QCOMPARE(space->column(), 1238);

        // The shelf's two names are the same two numbers, until nothing reads them.
        QCOMPARE(widths.shelfMargin(), space->gutter());
        QCOMPARE(widths.shelfGap(), space->block());
    }

    /// The hierarchy the roles stand for holds at every window: a gap that overtook the one
    /// above it in the order would make two blocks read as closer than two lines of a card.
    void the_roles_keep_their_order_at_every_window()
    {
        Widths widths;
        const Space *space = widths.space();
        for (int window = Widths::MinimumWindowWidth; window <= 2560; window += 40) {
            widths.setWindow(window);
            const QList<int> ordered{space->tight(), space->pill(),  space->label(),
                                     space->row(),   space->block(), space->gutter(),
                                     space->section()};
            for (qsizetype at = 1; at < ordered.size(); ++at)
                QVERIFY2(ordered.at(at - 1) < ordered.at(at),
                         qPrintable(u"at %1, role %2 is not under role %3"_s.arg(window)
                                        .arg(at - 1)
                                        .arg(at)));
            QCOMPARE(space->top(), space->gutter());
            QVERIFY(space->heading() < space->block());
        }
    }

    /// Through a real binding, as `scale` is: a window dragged wider and a text size changed
    /// on the desktop both reach `Widths.space.…`, which a property only does if it notifies.
    void a_window_dragged_wider_reaches_every_space_binding()
    {
        Widths widths;
        widths.setWindow(1280);
        AnyText text(1.0);
        widths.follow(&text);

        QQmlEngine engine;
        engine.rootContext()->setContextProperty(u"widths"_s, &widths);
        QQmlComponent component(&engine);
        component.setData("import QtQml\nQtObject {\n"
                          "    property int top: widths.space.top\n"
                          "    property int column: widths.space.column\n"
                          "}\n",
                          QUrl());
        std::unique_ptr<QObject> bound(component.create());
        QVERIFY2(bound, qPrintable(component.errorString()));
        QCOMPARE(bound->property("top").toInt(), 16);
        QCOMPARE(bound->property("column").toInt(), 1100);

        QSignalSpy changed(widths.space(), &Space::changed);
        widths.setWindow(1920);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(bound->property("top").toInt(), 24);

        // The column holds words, so it widens with them; a gap does not.
        text.becomes(1.5);
        QCOMPARE(changed.count(), 2);
        QCOMPARE(bound->property("top").toInt(), 24);
        QCOMPARE(bound->property("column").toInt(), qRound(1100 * 1.125 * 1.5));
    }

    /// The height is fed as the width is, and a window under 700 high is short: a banner of
    /// 120, a header of 208 and a bar of 58 left no volume row on a window of 560.
    void the_height_is_kept_and_a_window_under_700_is_short()
    {
        Widths widths;
        QSignalSpy said(&widths, &Widths::heightChanged);
        widths.setHeight(760);
        QCOMPARE(widths.height(), 760);
        QVERIFY(!widths.isShort());
        widths.setHeight(700);
        QVERIFY(!widths.isShort());
        widths.setHeight(699);
        QVERIFY(widths.isShort());
        widths.setHeight(Widths::MinimumWindowHeight);
        QVERIFY(widths.isShort());
        widths.setHeight(Widths::MinimumWindowHeight);
        QCOMPARE(said.count(), 4);
        QCOMPARE(widths.minimumWindowWidth(), 800);
        QCOMPARE(widths.minimumWindowHeight(), 560);
    }

    /// The paper round the shelf is room: on a larger window it gives the width to the space
    /// between covers, as the bar does since 23/10/2026; the count is the pure rule's, fed
    /// that paper.
    void the_shelfs_paper_is_room_and_its_columns_are_counted_with_it()
    {
        QCOMPARE(Widths::shelfColumnsFor(800, 14, 12), 5);
        QCOMPARE(Widths::shelfColumnsFor(1100, 15, 13), 7);
        QCOMPARE(Widths::shelfColumnsFor(1000, 0, 0), 5);
        QCOMPARE(Widths::shelfColumnsFor(1300, -5, -5), 8);

        Widths widths;
        QSignalSpy said(&widths, &Widths::windowChanged);
        widths.setWindow(1280);
        QCOMPARE(widths.shelfMargin(), 16);
        QCOMPARE(widths.shelfGap(), 14);
        widths.setWindow(1920);
        QCOMPARE(widths.shelfMargin(), 24);
        QCOMPARE(widths.shelfGap(), 21);
        QCOMPARE(widths.shelfColumns(), 8);
        widths.setWindow(Widths::MinimumWindowWidth);
        QCOMPARE(widths.shelfMargin(), 14);
        QCOMPARE(widths.shelfGap(), 12);
        QCOMPARE(widths.shelfColumns(), 5);
        QCOMPARE(said.count(), 3);
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

// Guiless and not appless: the binding test needs an application for its engine.
QTEST_GUILESS_MAIN(KnowsItsWidths)
#include "knows_its_widths.moc"
