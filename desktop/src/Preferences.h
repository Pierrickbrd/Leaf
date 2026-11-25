#pragma once

// What this reader prefers, and the one thing in Leaf that is written.
//
// `Settings.h` says read, never written, and that rule stands — it is about the
// **deployment**: where a server lives and how to prove who you are are configured by
// environment variables at both ends, and a client that grew a connection console would be
// managing something that is not its subject.
//
// A preference is not that. How this application looks on this machine is nobody's business
// but the person looking at it, and it has nowhere else to live. So it is written, and
// written apart: `leaf.conf` is the deployment's file, refused when anybody but its owner
// can read it, and a preference has no reason to sit in a file with that rule on it.

#include "Spreads.h"

#include <QHash>
#include <QObject>
#include <QQmlEngine>
#include <QSettings>
#include <QSizeF>
#include <QUrl>
#include <QString>
#include <QVariantList>

class Preferences : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// What the reader asked for, which is not what is being drawn: `System` is a choice,
    /// and the palette it resolves to is the desktop's business.
    Q_PROPERTY(Appearance appearance READ appearance NOTIFY changed)
    Q_PROPERTY(QString appearanceTitle READ appearanceTitle CONSTANT)
    Q_PROPERTY(QString readerDefaultsTitle READ readerDefaultsTitle CONSTANT)
    /// `[{ value, label, icon }]`, in the order they are offered.
    Q_PROPERTY(QVariantList appearances READ appearances CONSTANT)

    /// Where the file and folder pickers open, which is where they were last used.
    ///
    /// A library lives in one place and a reader imports from it over and over. Opening on
    /// the home folder every time meant walking the same four levels down before every
    /// single import. Empty until a picker has been used once, and the picker then keeps
    /// whatever default the desktop gives it.
    Q_PROPERTY(QUrl lastPlace READ lastPlace NOTIFY changed)

    /// Whether the volumes of a series are shown as covers rather than as lines.
    ///
    /// Kept **once and not per series**: it is a reader's habit, not a state of one page.
    /// Switching to the grid on Elfes and opening Nains gives a grid — the alternative is a
    /// setting that answers differently depending on where you last were, which is a setting
    /// nobody can predict. Lines are the default: they say the whole title, the pages and the
    /// state on one row, where a grid shows covers and cuts long titles.
    Q_PROPERTY(bool volumesAsGrid READ volumesAsGrid WRITE showVolumesAsGrid NOTIFY changed)

    /// What warns, and by which of the two channels. `[{ name, label, detail, bubble,
    /// desktop }]`, in the order the section draws them — a list and not four cards, because
    /// four lines and two columns of switches fit in one card where four cards of three pills
    /// would fill a section to say the same thing.
    Q_PROPERTY(QVariantList warnings READ warnings NOTIFY changed)

    /// Where the bubbles appear: one of six zones of the screen. Not drawn at all when
    /// nothing makes a bubble — an empty heading is worse than an absent one.
    /// The two cards of the section and the two columns of its list, said once here so that
    /// no `.qml` writes a word of French.
    Q_PROPERTY(QString warningsTitle READ warningsTitle CONSTANT)
    Q_PROPERTY(QString cornerTitle READ cornerTitle CONSTANT)
    Q_PROPERTY(QString eventColumn READ eventColumn CONSTANT)
    Q_PROPERTY(QString bubbleColumn READ bubbleColumn CONSTANT)
    Q_PROPERTY(QString desktopColumn READ desktopColumn CONSTANT)

    Q_PROPERTY(Corner corner READ corner NOTIFY changed)
    Q_PROPERTY(QString cornerLabel READ cornerLabel NOTIFY changed)
    Q_PROPERTY(bool anythingBubbles READ anythingBubbles NOTIFY changed)

    /// How the reader lays pages out and what it fits them to. Kept on this machine and, once a
    /// profile is reading, on the server under that profile (see `synced`).
    Q_PROPERTY(PageLayout pageLayout READ pageLayout NOTIFY changed)
    Q_PROPERTY(Fit fit READ fit NOTIFY changed)
    /// What is behind the pages, and how a page that comes takes the place of the one that
    /// goes. The choice of 22/10/2026: a dark paper tires some eyes in a lit room, and
    /// a slide on every page is too much for some readers, so both are a reader's to say, on
    /// this desk like the layout.
    Q_PROPERTY(Backdrop backdrop READ backdrop NOTIFY changed)
    Q_PROPERTY(Motion motion READ motion NOTIFY changed)
    /// A webtoon's reading, on this desk and for every webtoon (the card says so): how far a
    /// notch of the wheel or a stroke of the pad moves the strip, and how fast its A goes —
    /// eleven steps each, 0 to 10, the choice of 24/11/2026 (between ten and fifteen, and no
    /// values running to infinity) — then when its bars go and come back.
    Q_PROPERTY(int stripSensitivity READ stripSensitivity NOTIFY changed)
    Q_PROPERTY(int stripAutoSpeed READ stripAutoSpeed NOTIFY changed)
    Q_PROPERTY(StripControls stripControls READ stripControls NOTIFY changed)
    /// The bars come back after five seconds without scrolling as well — with `Discreet` or
    /// `OnTheWayUp`; with `OnClick` scrolling never sends them away to begin with.
    Q_PROPERTY(bool stripAtRest READ stripAtRest NOTIFY changed)

public:
    enum class Appearance { System, Light, Dark };
    Q_ENUM(Appearance)

    /// The four families of event, and the one that crosses the other three.
    ///
    /// Eleven events, four lines: a switch cannot answer event by event, so it answers by
    /// family. `Failures` is deliberately not one family among four — turning « scans » off
    /// does not silence « a scan stopped », because one may want quiet about a subject's good
    /// news and noise about its bad.
    enum class Warns { Imports, Scans, Downloads, Failures };
    Q_ENUM(Warns)

    /// The six zones of the screen a bubble can be laid in.
    enum class Corner { TopLeft, Top, TopRight, BottomLeft, Bottom, BottomRight };
    Q_ENUM(Corner)

    /// `Auto` is two pages when the window is landscape and one otherwise. That is the plan
    /// author's own choice: the specification does not define "Auto".
    enum class PageLayout { Single, Double, Auto };
    Q_ENUM(PageLayout)

    enum class Fit { Height, Width, Original };
    Q_ENUM(Fit)

    /// `Theme.readerBlack`, `readerGrey` and `readerWhite`: colours for the pages, the same in
    /// both themes, so « Noir » is black even when the application is light.
    enum class Backdrop { Black, Grey, White };
    Q_ENUM(Backdrop)

    /// `Slide` is what there was: a turn slides, a jump fades. `Fade` fades both; `Instant`
    /// moves nothing, and still waits for the images, so a page is never put on bare paper.
    /// Not `None`, which X11's headers define as a macro.
    ///
    /// No default from the desktop's « reduce motion »: Qt 6.4 does not say it — no
    /// `QStyleHints` for it, and GNOME's `enable-animations` is a GSettings key Qt does not
    /// read — so `Slide`, and the reader who wants less says it here.
    enum class Motion { Slide, Fade, Instant };
    Q_ENUM(Motion)
    /// When a webtoon's bars go: going down, back at a click or Tab (`Discreet`); going down,
    /// back after a quarter of a screen going up as well (`OnTheWayUp`); never by scrolling
    /// (`OnClick`). « Always there » was refused on 24/11/2026.
    enum class StripControls { Discreet, OnTheWayUp, OnClick };
    Q_ENUM(StripControls)
    /// The middle of the eleven steps, and the A's third: slow enough to read at the first try.
    static constexpr int MiddleStep = 5;
    static constexpr int AutoStep = 3;
    static constexpr int LastStep = 10;

    explicit Preferences(QObject *parent = nullptr);

    static Preferences *create(QQmlEngine *engine, QJSEngine *);

    Appearance appearance() const { return m_appearance; }
    QString appearanceTitle() const;
    QString readerDefaultsTitle() const;
    QVariantList appearances() const;

    Q_INVOKABLE void chooseAppearance(Appearance wanted);

    PageLayout pageLayout() const { return m_pageLayout; }
    Q_INVOKABLE void choosePageLayout(PageLayout wanted);
    Fit fit() const { return m_fit; }
    Q_INVOKABLE void chooseFit(Fit wanted);
    /// What a webtoon's strip keeps on this device, in logical pixels, 0 for nothing kept: the
    /// width its chapters are drawn at, the width it started at (its 100 %), and the width
    /// one chapter keeps apart from the others. A width and not a factor: a series mixes
    /// chapters scanned at 720 and at 1080 pixels, and one factor drew them at two widths.
    /// Not synced — a width in pixels is this screen's, and another device's would be wrong.
    struct Strip
    {
        double width = 0;
        double base = 0;
        double chapter = 0;
    };
    Strip strip(const QString &series, const QString &entry) const;
    /// Writes all three; a `chapter` of 0 forgets the chapter's own.
    void keepStrip(const QString &series, const QString &entry, const Strip &kept);
    Backdrop backdrop() const { return m_backdrop; }
    Q_INVOKABLE void chooseBackdrop(Backdrop wanted);
    Motion motion() const { return m_motion; }
    Q_INVOKABLE void chooseMotion(Motion wanted);
    int stripSensitivity() const { return m_stripSensitivity; }
    Q_INVOKABLE void chooseStripSensitivity(int step);
    int stripAutoSpeed() const { return m_stripAutoSpeed; }
    Q_INVOKABLE void chooseStripAutoSpeed(int step);
    StripControls stripControls() const { return m_stripControls; }
    Q_INVOKABLE void chooseStripControls(StripControls wanted);
    bool stripAtRest() const { return m_stripAtRest; }
    Q_INVOKABLE void chooseStripAtRest(bool wanted);
    /// What `Auto` resolves to for this window; a fixed choice ignores the window.
    Spreads::Layout layoutFor(QSizeF window) const;

    bool volumesAsGrid() const { return m_volumesAsGrid; }
    void showVolumesAsGrid(bool asGrid);

    QUrl lastPlace() const { return m_lastPlace; }
    /// Remembers where a picker was used. Takes the folder itself, or the folder a chosen
    /// file sits in — what a reader wants next time is the place, not the thing.
    Q_INVOKABLE void rememberPlace(const QUrl &place);
    /// How the window stood when it was closed, `{width, height, maximized}`; empty before
    /// it ever was. Written at the close and not at every resize: a harness that resizes its
    /// window would otherwise hand the next window the size of the last.
    Q_INVOKABLE QVariantMap windowFrame() const;
    Q_INVOKABLE void rememberWindow(int width, int height, bool maximized);

    /// The few choices that follow the reader rather than the machine, as the words they are
    /// written under: `{layout, fit, backdrop, motion, volumesAsGrid}`. The window, the
    /// appearance (a lit room is this desk's), the bubbles and the picker's place stay here:
    /// they describe this machine and its desktop, and a profile that read on another one
    /// would be handed a window it does not fit.
    QVariantMap synced() const;
    /// Takes what the server holds for the profile now reading. A key that is missing keeps
    /// what this machine has — a profile that never chose is not reset to the defaults, which
    /// is also how an existing install keeps its choices the first time — and a key this
    /// version does not know is left alone. Said back to nobody: it is the server's own word.
    void adopt(const QVariantMap &held);

    QVariantList warnings() const;
    QString warningsTitle() const;
    QString cornerTitle() const;
    QString eventColumn() const;
    QString bubbleColumn() const;
    QString desktopColumn() const;
    /// Whether this family warns at all, and whether it escalates to the desktop. Asked by
    /// name so that the one place that decides is the one that holds the answer. A family
    /// that does not concern the profile neither bubbles nor reaches the desktop, whatever
    /// was switched while another profile read: `Toasts` asks only these two.
    bool bubbles(Warns which) const;
    bool reachesTheDesktop(Warns which) const;
    /// Which families concern the profile this device reads as; all of them until told. A
    /// profile that cannot import is not offered a switch for imports — it would be a switch
    /// wired to nothing it can ever do. `Boot` says it, from `Profiles`, so that this class
    /// does not depend on the one that knows the rights.
    void concern(bool imports, bool scans, bool downloads);
    Q_INVOKABLE void showBubble(Warns which, bool wanted);
    Q_INVOKABLE void reachTheDesktop(Warns which, bool wanted);

    Corner corner() const { return m_corner; }
    QString cornerLabel() const;
    bool anythingBubbles() const;
    Q_INVOKABLE void putBubbles(Corner where);

signals:
    void changed();
    /// The choice moved. `Theme` listens: it is the one that knows what a palette is, and
    /// this is the one that knows what was asked for.
    void appearanceChosen(Appearance wanted);
    /// One of `synced()`'s choices was made here, by the reader, and the server should hear
    /// it. Not said for `adopt`: echoing the server's answer back to it is a write for nothing.
    void syncedChanged(const QVariantMap &kept);

private:
    void announce();
    bool m_adopting = false;
    /// Its own file, named here rather than in the constructor's list: one place says
    /// where Leaf's preferences live, and it is the line that declares the member.
    QSettings m_file{QSettings::IniFormat, QSettings::UserScope, QStringLiteral("Leaf"),
                     QStringLiteral("preferences")};
    Appearance m_appearance = Appearance::System;
    QUrl m_lastPlace;
    bool m_volumesAsGrid = false;
    PageLayout m_pageLayout = PageLayout::Auto;
    Fit m_fit = Fit::Height;
    Backdrop m_backdrop = Backdrop::Black;
    Motion m_motion = Motion::Slide;
    int m_stripSensitivity = MiddleStep;
    int m_stripAutoSpeed = AutoStep;
    StripControls m_stripControls = StripControls::Discreet;
    bool m_stripAtRest = false;
    /// Writes one of a webtoon's choices, which this desk keeps and the profile does not.
    void keepHere(const QString &key, const QVariant &value);
    /// Everything on, except the desktop for a download: a copy saved in two seconds wakes
    /// nobody. It is being pestered that makes somebody open this screen, not silence.
    QHash<Warns, bool> m_bubbles;
    QHash<Warns, bool> m_desktop;
    /// Failures concern everybody and are not a member.
    bool m_concernsImports = true;
    bool m_concernsScans = true;
    bool m_concernsDownloads = true;
    bool concerns(Warns which) const;
    /// Where the desktop puts its own, so the place one has already learned to look — and
    /// the middle stays clear, which is where the eye goes on a wall of covers.
    Corner m_corner = Corner::BottomRight;
};
