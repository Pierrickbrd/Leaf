#pragma once

// The three widths, and the shelf columns they imply, in the one place that declares them.
//
// Not `width < …` scattered through every delegate: the grid and the reader have to agree on
// where the break falls, and two copies of a number agree only until one of them is edited.
//
// Where the two numbers come from. Below 1100 a two-page spread gives each half under 400 px
// and stops being readable, so the reader falls back to a single page — which is why this
// break belongs to more than the grid. Below 600 the search field can no longer share the bar
// with anything and takes the screen.
//
// That is the reasoning that chose them. The grid now reads its band and column count here;
// the reader will later read the same band rather than grow a second set of thresholds.

#include <QObject>
#include <QQmlEngine>
#include <QString>

class Widths;

/// The size of everything a screen draws, for this window: `Widths.scale` in QML.
///
/// One factor (`Widths::controlsFor`) sets four rules, one per kind of thing drawn:
///
/// - **Lengths** (`size`) follow it both ways — `size(26)` is a button drawn at 26 for a window
///   of 1280 — so that a card, its paddings and the bars grow and breathe on a large window.
/// - **Words** (`caption`, `body`, `title`) follow it down and never up. Under 1 they shrink
///   within a floor that depends on what they are, so that a small window shrinks the paper
///   round the words before the words: a caption never under 11 pixels, body text never under
///   13, and a title, which has room to give, shrinking twice as fast but never under 15. Above
///   1 they keep the size they were drawn at. They used to grow with the lengths, and on
///   23/10/2026, at 1920 on a screen of 3840 at 200 %, the owner found every card's words and
///   every key too big: a page of text that grows with the window is something no web page
///   does, and the room a larger window gives belongs to the space between things.
/// - **Icons and marks** (`icon`) — a glyph in a button, a key on the shortcuts card — grow at
///   half the rate above 1: they stand beside words that no longer grow, and at the full rate
///   a key was drawn larger than the word it names. Under 1 they shrink as lengths do.
/// - **Room** (`room`), the gaps between the buttons and groups of a bar, grows twice as fast
///   as lengths above 1: with the words fixed, what the bar gains in width is given to the
///   space between them, which is what the owner asked of the floating bar that day.
///
/// A card that holds words takes its width from `column`, half the rate above 1, as icons:
/// at the full rate a card of fixed words grew a band of empty paper down its right side.
///
/// An object behind a property, and not invokables on `Widths`: a binding re-reads what a
/// call returns only when a property it read notifies, and `scale` notifies with the window.
/// Written `Widths.scale.body(14)`, the size follows a window dragged wider; written
/// `Widths.body(14)`, it would be computed once and never again.
class Scale : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS

public:
    static constexpr int CaptionFloor = 11;
    static constexpr int BodyFloor = 13;
    static constexpr int TitleFloor = 15;

    explicit Scale(const Widths *widths, QObject *parent = nullptr);

    /// A length drawn at `drawn` for a window of 1280: a width, a padding, a radius.
    Q_INVOKABLE int size(qreal drawn) const;
    /// An icon's side, a key's face: half the rate of a length above 1, the same under it.
    Q_INVOKABLE int icon(qreal drawn) const;
    /// The width of something that holds words — a card, a column of names: as `icon`.
    Q_INVOKABLE int column(qreal drawn) const;
    /// The gap between the buttons and groups of a bar: twice the rate of a length above 1.
    Q_INVOKABLE int room(qreal drawn) const;
    /// A letter spacing drawn at `drawn`: it belongs to the words, so it never grows either.
    Q_INVOKABLE qreal letters(qreal drawn) const;
    /// A caption, a label, a key: the small words beside something. Never under 11, never
    /// over its drawn size.
    Q_INVOKABLE int caption(int drawn) const;
    /// Words that are read rather than glanced at. Never under 13, never over its drawn size.
    Q_INVOKABLE int body(int drawn) const;
    /// A card's or a bar's title: twice as fast as the rest under 1, never under 15, never
    /// over its drawn size.
    Q_INVOKABLE int title(int drawn) const;

    static int sizeFor(qreal drawn, qreal factor);
    static int iconFor(qreal drawn, qreal factor);
    static int columnFor(qreal drawn, qreal factor);
    static int roomFor(qreal drawn, qreal factor);
    static qreal lettersFor(qreal drawn, qreal factor);
    static int captionFor(int drawn, qreal factor);
    static int bodyFor(int drawn, qreal factor);
    static int titleFor(int drawn, qreal factor);

private:
    const Widths *m_widths;
};

class Widths : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// `windowChanged` fires on every real change of the stored width; `changed` fires only
    /// when the band it falls in crosses. A screen bound to `window` wants the first — the
    /// second would leave it reading a value frozen at the last band crossing.
    Q_PROPERTY(int window READ window WRITE setWindow NOTIFY windowChanged)
    Q_PROPERTY(Band band READ band NOTIFY changed)
    /// The shelf changes from four to five columns inside the medium band, so this follows
    /// every window change rather than only a band crossing.
    Q_PROPERTY(int shelfColumns READ shelfColumns NOTIFY windowChanged)
    Q_PROPERTY(int shelfMargin READ shelfMargin CONSTANT)
    Q_PROPERTY(int shelfGap READ shelfGap CONSTANT)
    /// How much larger or smaller than drawn everything the reader lays over its pages is —
    /// its bar, its dock and its cards — for this window: 0.85 up to 900 pixels, 1 at 1280,
    /// 1.5 from 2560, linear between. A 2560 display scaled by 1.333 is a window of 1920 and
    /// gets 1.25: in the owner's test of 22/10/2026 there, 1 left the dock a strip of specks
    /// and 1.5 made it too large. What each kind of thing draws makes of it is `Scale`'s:
    /// lengths follow it, words only under 1 (23/10/2026), icons and cards at half its rate.
    Q_PROPERTY(qreal controls READ controls NOTIFY windowChanged)
    /// Lengths and words for this window, with their floors — see `Scale`. What a screen
    /// that follows the window reads, rather than multiplying `controls` itself.
    Q_PROPERTY(Scale *scale READ scale NOTIFY windowChanged)
    /// « Large », « Moyenne », « Étroite » — `Words::band(band())`, on this singleton for the
    /// same reason `Navigation::label` is on that one rather than on a second singleton.
    Q_PROPERTY(QString bandLabel READ bandLabel NOTIFY changed)

public:
    enum class Band { Narrow, Medium, Wide };
    Q_ENUM(Band)

    static constexpr int ShelfMargin = 16;
    static constexpr int ShelfGap = 14;
    static constexpr int MinimumCoverWidth = 140;
    static constexpr int MaximumShelfColumns = 8;

    explicit Widths(QObject *parent = nullptr);

    int window() const;
    void setWindow(int width);

    Band band() const { return bandFor(m_window); }
    QString bandLabel() const;
    int shelfColumns() const;
    int shelfMargin() const;
    int shelfGap() const;

    qreal controls() const { return controlsFor(m_window); }
    Scale *scale() const { return m_scale; }

    static Band bandFor(int width);
    static qreal controlsFor(int width);
    static int shelfColumnsFor(int width);

    /// The side of a round mark laid on a cover — the « … » in its top corner, the check in
    /// its bottom one — for a cover this wide. An eighth of it, between 22 and 30. Fixed at
    /// 22, the check on a cover three hundred wide was a speck nobody found; at a sixth with
    /// a ceiling of 40, the « … » covered more of the illustration than it had any reason to.
    /// One rule for both, so the two corners of one cover never disagree on what size a mark
    /// is.
    Q_INVOKABLE static int markOn(qreal coverWidth);

signals:
    /// Emitted on every real change of the stored width.
    void windowChanged();

    /// Emitted when the *band* changes, not when the window does. A drag across a hundred
    /// pixels inside one band would otherwise rebind every screen a hundred times.
    void changed();

private:
    int m_window = 0;
    Scale *m_scale;
};
