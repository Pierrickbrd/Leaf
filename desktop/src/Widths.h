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
#include <QPointer>
#include <QQmlEngine>
#include <QString>

class Widths;

/// How large the person at this desktop asked text to be: 1 as drawn, 1.25 a quarter larger.
///
/// A source handed to `Widths::follow`, and not something `Widths` looks up itself, for the
/// reason `Config::read` takes its lookup as an argument: the application hands it the
/// desktop's own setting (`DesktopText`), a test hands it a number it can change. Nothing in
/// `Widths` knows what D-Bus is.
class SystemText : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    /// The factor as the desktop says it, unbounded: `Widths` bounds it.
    virtual qreal factor() const = 0;

signals:
    /// The setting moved while the application was open.
    void changed();
};

/// The size of everything a screen draws, for this window: `Widths.scale` in QML.
///
/// One factor (`Widths::controlsFor`) sets four rules, one per kind of thing drawn:
///
/// - **Lengths** (`size`) follow it both ways — `size(26)` is a button drawn at 26 for a window
///   of 1280 — so that a card, its paddings and the bars grow and breathe on a large window.
/// - **The box a word stands in** (`fit`) — a button's height, a pill's side padding — follows
///   the word, not the lengths: a control drawn 36 high round 14 pixels of text is 2.6 words
///   high at every window, and a larger one does not make it 3.2. Only what frames a word
///   takes this law; a card, a bar and the page's own gaps are surfaces, and stay lengths.
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
/// **One word grows** (`display`): the word that is the subject of a whole screen — a series'
/// name over its page, the one sentence of an empty screen. A title of 36 over a page of 1100
/// reads as a title; the same over a page of 1888 reads as a caption. It grows as an icon
/// does, at half the rate, and never past a quarter more than drawn. The owner allowed it on
/// 23/10/2026 for three named places — the series' name, the screen of an unlinked
/// application, the empty shelf — and no others.
///
/// **Words follow the desktop's text size** (`Widths::systemText`), and so do the cards that
/// hold them (`column`): someone who asked GNOME for text a quarter larger gets words a
/// quarter larger, live, and the window's floors apply after, so a small window shrinks them
/// no further than a caption can bear. The owner asked for it on 23/10/2026, once it was
/// measured that Qt 6 carries none of GNOME's text-scaling factor to an application — the
/// logical DPI stays 96, and every `pixelSize` ignores the system font anyway. The device-pixel
/// ratio is not in it: a screen at 200 % is already doubled by Qt, and counting it here would
/// draw every word twice too big.
///
/// An object behind a property, and not invokables on `Widths`: a binding re-reads what a
/// call returns only when a property it read notifies, and `scale` notifies with the window
/// and with the text size. Written `Widths.scale.body(14)`, the size follows a window dragged
/// wider; written `Widths.body(14)`, it would be computed once and never again.
class Scale : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS

public:
    static constexpr int CaptionFloor = 11;
    static constexpr int BodyFloor = 13;
    static constexpr int TitleFloor = 15;
    /// The most `display` ever makes of a word, against what it was drawn at.
    static constexpr qreal DisplayCeiling = 1.25;

    explicit Scale(const Widths *widths, QObject *parent = nullptr);

    /// A length drawn at `drawn` for a window of 1280: a width, a padding, a radius.
    Q_INVOKABLE int size(qreal drawn) const;
    /// The box a word stands in — a button's height, its side padding — so that a control
    /// keeps the proportions it was drawn with: it follows the word and not the window, which
    /// is to say down with a small window and with the desktop's text size, never up with a
    /// large one. A box that grew as a length while its word stayed put turned a pill drawn
    /// 36 high round 14 pixels of text into one 45 high at 1920.
    Q_INVOKABLE int fit(qreal drawn) const;
    /// An icon's side, a key's face: half the rate of a length above 1, the same under it.
    Q_INVOKABLE int icon(qreal drawn) const;
    /// The width of something that holds words — a card, a column of names: as `icon`, and
    /// as large again as the desktop makes its text.
    Q_INVOKABLE int column(qreal drawn) const;
    /// The gap between the buttons and groups of a bar: twice the rate of a length above 1.
    Q_INVOKABLE int room(qreal drawn) const;
    /// A letter spacing drawn at `drawn`: it belongs to the words, so it never grows with the
    /// window either, and follows the desktop's text size as they do.
    Q_INVOKABLE qreal letters(qreal drawn) const;
    /// A caption, a label, a key: the small words beside something. Never under 11, never
    /// over its drawn size times the desktop's text size.
    Q_INVOKABLE int caption(int drawn) const;
    /// Words that are read rather than glanced at. Never under 13, never over its drawn size
    /// times the desktop's text size.
    Q_INVOKABLE int body(int drawn) const;
    /// A card's or a bar's title: twice as fast as the rest under 1, never under 15, never
    /// over its drawn size times the desktop's text size.
    Q_INVOKABLE int title(int drawn) const;
    /// The subject of a whole screen: as `title` under 1, half the rate of a length above it,
    /// never over a quarter more than drawn — all of it times the desktop's text size.
    Q_INVOKABLE int display(int drawn) const;

    static int sizeFor(qreal drawn, qreal factor);
    static int fitFor(qreal drawn, qreal factor) { return fitFor(drawn, factor, 1.0); }
    static int fitFor(qreal drawn, qreal factor, qreal text);
    static int iconFor(qreal drawn, qreal factor);
    static int columnFor(qreal drawn, qreal factor) { return columnFor(drawn, factor, 1.0); }
    static int columnFor(qreal drawn, qreal factor, qreal text);
    static int roomFor(qreal drawn, qreal factor);
    static qreal lettersFor(qreal drawn, qreal factor) { return lettersFor(drawn, factor, 1.0); }
    static qreal lettersFor(qreal drawn, qreal factor, qreal text);
    // Two arguments as well as three, and not a defaulted third: the seam test hands these
    // around as `int (*)(int, qreal)`, and a default argument is not part of a function's type.
    static int captionFor(int drawn, qreal factor) { return captionFor(drawn, factor, 1.0); }
    static int captionFor(int drawn, qreal factor, qreal text);
    static int bodyFor(int drawn, qreal factor) { return bodyFor(drawn, factor, 1.0); }
    static int bodyFor(int drawn, qreal factor, qreal text);
    static int titleFor(int drawn, qreal factor) { return titleFor(drawn, factor, 1.0); }
    static int titleFor(int drawn, qreal factor, qreal text);
    static int displayFor(int drawn, qreal factor) { return displayFor(drawn, factor, 1.0); }
    static int displayFor(int drawn, qreal factor, qreal text);

private:
    const Widths *m_widths;
};

/// The space between things, by what it separates: `Widths.space` in QML.
///
/// On 23/10/2026 the owner set the shelf beside the search and found that the « Reprendre »
/// card and the « Aperçu – Séries – Fichiers » pills did not stand the same distance under
/// the bar. Measured, the first thing on a page stood 16, 14, 12, 11 or 9 pixels under it
/// depending on the screen — three of those literals that did not follow the window, so that
/// at 1920 the order of the gaps reversed — and the search page alone drew four left edges.
/// Each screen placed its own first block, and nothing said « a page starts here ».
///
/// So a gap is named by its role, once, and each role is one law of `Scale`: `room` for the
/// gaps that should take what a wider window gives, `size` for the paddings inside a surface,
/// which grow with the surface and not faster. `PageColumn`, `PageStrip` and `SectionHeading`
/// read these; a screen reads the containers, not the numbers.
///
/// Properties, and not invokables as on `Scale`: there is nothing to pass, and a property
/// notifies — `changed` follows `Widths::scaleChanged` — so `Widths.space.top` in a binding
/// follows a window dragged wider.
///
/// The roles, smallest to largest, are an order the tests hold: a title and its subtitle
/// (`tight`) are closer than two pills (`pill`), than a glyph and its word (`label`), than two
/// lines of a card (`row`), than two blocks of a column (`block`), than the page's edge
/// (`gutter`), than two titled sections (`section`).
class Space : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS

    /// The paper left and right of every page: the shelf's 16, as room.
    Q_PROPERTY(int gutter READ gutter NOTIFY changed)
    /// From the bottom of the bar to the first thing on a page, and from the last thing to
    /// the bottom of what scrolls. The same as `gutter`, so a page's corner is square.
    Q_PROPERTY(int top READ top NOTIFY changed)
    /// Between two titled sections: the settings' 28 between cards, now a role.
    Q_PROPERTY(int section READ section NOTIFY changed)
    /// Between two blocks of one column, and from a button to the group it leads: the shelf's
    /// gap between covers, which was already the 14 under the band.
    Q_PROPERTY(int block READ block NOTIFY changed)
    /// From a section's heading to what it heads.
    Q_PROPERTY(int heading READ heading NOTIFY changed)
    /// Inside a card, a dialog, a bubble: a padding, so a length.
    Q_PROPERTY(int card READ card NOTIFY changed)
    /// Inside a menu, a panel, a tooltip, and from a trigger to what it opens.
    Q_PROPERTY(int compact READ compact NOTIFY changed)
    /// Between the lines stacked in a card.
    Q_PROPERTY(int row READ row NOTIFY changed)
    /// From a glyph to its word inside one control.
    Q_PROPERTY(int label READ label NOTIFY changed)
    /// From one pill to the next in a row of pills.
    Q_PROPERTY(int pill READ pill NOTIFY changed)
    /// Between a title and the line glued under it.
    Q_PROPERTY(int tight READ tight NOTIFY changed)
    /// The widest a page of reading gets — the series, the files found, the settings —
    /// laid on the left at `gutter` and not centred: at 1920 a centred page moved everything
    /// 317 pixels right of the shelf it was opened from, and left the back button alone in
    /// the corner (spacing audit, 23/10/2026). A grid stays full width: its columns fill it.
    Q_PROPERTY(int column READ column NOTIFY changed)

public:
    explicit Space(const Widths *widths, QObject *parent = nullptr);

    int gutter() const;
    int top() const;
    int section() const;
    int block() const;
    int heading() const;
    int card() const;
    int compact() const;
    int row() const;
    int label() const;
    int pill() const;
    int tight() const;
    int column() const;

    /// The drawn values at 1280, which each role grows or shrinks by its law.
    static constexpr int Gutter = 16;
    static constexpr int Top = 16;
    static constexpr int Section = 28;
    static constexpr int Block = 14;
    static constexpr int Heading = 8;
    static constexpr int Card = 16;
    static constexpr int Compact = 8;
    static constexpr int Row = 10;
    static constexpr int Label = 8;
    static constexpr int Pill = 6;
    static constexpr int Tight = 2;
    static constexpr int Column = 1100;

signals:
    void changed();

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
    /// The window's height, fed from `Main.qml` as `window` is, for the screens whose fixed
    /// headers ate a window of 560 (`short`).
    Q_PROPERTY(int height READ height WRITE setHeight NOTIFY heightChanged)
    /// Under 700 high: a banner of 120, a series header of 208 and a bar of 58 left a window
    /// of 560 no room for a single volume in the audit of 23/10/2026.
    Q_PROPERTY(bool short READ isShort NOTIFY heightChanged)
    Q_PROPERTY(Band band READ band NOTIFY changed)
    /// The shelf changes from four to five columns inside the medium band, so this follows
    /// every window change rather than only a band crossing.
    Q_PROPERTY(int shelfColumns READ shelfColumns NOTIFY windowChanged)
    /// The paper either side of the shelf and between its covers: `room(16)` and `room(14)`,
    /// so that a larger window gives its width to the space between covers, as the bar does.
    /// The same numbers as `space.gutter` and `space.block`, read under the shelf's name by
    /// the screens the spacing pass has not reached yet; they go once nothing reads them.
    Q_PROPERTY(int shelfMargin READ shelfMargin NOTIFY windowChanged)
    Q_PROPERTY(int shelfGap READ shelfGap NOTIFY windowChanged)
    /// How much larger or smaller than drawn everything the reader lays over its pages is —
    /// its bar, its dock and its cards — for this window: 0.85 up to 900 pixels, 1 at 1280,
    /// 1.5 from 2560, linear between. A 2560 display scaled by 1.333 is a window of 1920 and
    /// gets 1.25: in the test of 22/10/2026 there, 1 left the dock a strip of specks
    /// and 1.5 made it too large. What each kind of thing draws makes of it is `Scale`'s:
    /// lengths follow it, words only under 1 (23/10/2026), icons and cards at half its rate.
    Q_PROPERTY(qreal controls READ controls NOTIFY windowChanged)
    /// The desktop's text size, bounded to [0.8, 2] — see `Scale` and `follow`.
    Q_PROPERTY(qreal systemText READ systemText NOTIFY systemTextChanged)
    /// Lengths and words for this window, with their floors — see `Scale`. What a screen
    /// that follows the window reads, rather than multiplying `controls` itself.
    Q_PROPERTY(Scale *scale READ scale NOTIFY scaleChanged)
    /// The gaps between things, by role — see `Space`.
    Q_PROPERTY(Space *space READ space CONSTANT)
    /// « Large », « Moyenne », « Étroite » — `Words::band(band())`, on this singleton for the
    /// same reason `Navigation::label` is on that one rather than on a second singleton.
    Q_PROPERTY(QString bandLabel READ bandLabel NOTIFY changed)
    /// The smallest window `Main.qml` lets anyone make — see `MinimumWindowWidth`.
    Q_PROPERTY(int minimumWindowWidth READ minimumWindowWidth CONSTANT)
    Q_PROPERTY(int minimumWindowHeight READ minimumWindowHeight CONSTANT)

public:
    enum class Band { Narrow, Medium, Wide };
    Q_ENUM(Band)

    static constexpr int ShelfMargin = 16;
    static constexpr int ShelfGap = 14;
    static constexpr int MinimumCoverWidth = 140;
    static constexpr int MaximumShelfColumns = 8;
    /// The smallest window there is, decided by the owner on 23/10/2026: under 800 the series
    /// page, the reader's cards and the shelf's five covers have nowhere left to give, and a
    /// window of 560 high is still one a volume row fits under its header. Here and not only
    /// in `Main.qml` so the tests read the same number the window is held to.
    static constexpr int MinimumWindowWidth = 800;
    static constexpr int MinimumWindowHeight = 560;
    /// Where a window stops being `short`.
    static constexpr int ShortWindow = 700;
    /// The bounds on the desktop's text size: past them, a setting someone typed by hand
    /// breaks every layout rather than enlarging it.
    static constexpr qreal LeastText = 0.8;
    static constexpr qreal MostText = 2.0;

    explicit Widths(QObject *parent = nullptr);

    int window() const;
    void setWindow(int width);
    int height() const { return m_height; }
    void setHeight(int height);
    bool isShort() const { return m_height < ShortWindow; }

    Band band() const { return bandFor(m_window); }
    QString bandLabel() const;
    int shelfColumns() const;
    int shelfMargin() const;
    int shelfGap() const;
    int minimumWindowWidth() const { return MinimumWindowWidth; }
    int minimumWindowHeight() const { return MinimumWindowHeight; }

    qreal controls() const { return controlsFor(m_window); }
    qreal systemText() const { return m_systemText; }
    Scale *scale() const { return m_scale; }
    Space *space() const { return m_space; }

    /// Takes the desktop's text size from `source` now and every time it says it changed.
    /// Not owned. A later call replaces the earlier source.
    void follow(SystemText *source);

    static Band bandFor(int width);
    static qreal controlsFor(int width);
    /// What a desktop's factor becomes here: bounded, and 1 for anything that is not a number
    /// a person could have chosen.
    static qreal textFor(qreal factor);
    /// Pure, with the paper as arguments: `shelfColumns` hands it what `room` makes of them for
    /// this window; the defaults are the paper as drawn, at 1280.
    static int shelfColumnsFor(int width, int margin = ShelfMargin, int gap = ShelfGap);

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
    /// Emitted on every real change of the stored height.
    void heightChanged();
    /// Emitted when the desktop's text size, once bounded, actually moved.
    void systemTextChanged();
    /// Emitted with either of the two: what `Scale` returns depends on both.
    void scaleChanged();

    /// Emitted when the *band* changes, not when the window does. A drag across a hundred
    /// pixels inside one band would otherwise rebind every screen a hundred times.
    void changed();

private:
    void readText();

    int m_window = 0;
    int m_height = 0;
    qreal m_systemText = 1.0;
    QPointer<SystemText> m_source;
    QMetaObject::Connection m_following;
    Scale *m_scale;
    Space *m_space;
};
