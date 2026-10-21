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
    Q_PROPERTY(QString bubbleColumn READ bubbleColumn CONSTANT)
    Q_PROPERTY(QString desktopColumn READ desktopColumn CONSTANT)

    Q_PROPERTY(Corner corner READ corner NOTIFY changed)
    Q_PROPERTY(QString cornerLabel READ cornerLabel NOTIFY changed)
    Q_PROPERTY(bool anythingBubbles READ anythingBubbles NOTIFY changed)

    /// How the reader lays pages out and what it fits them to, kept per device like the rest:
    /// a window and a screen are this machine's, not the library's.
    Q_PROPERTY(PageLayout pageLayout READ pageLayout NOTIFY changed)
    Q_PROPERTY(Fit fit READ fit NOTIFY changed)
    /// What is behind the pages, and how a page that comes takes the place of the one that
    /// goes. The owner's choice of 22/10/2026: a dark paper tires some eyes in a lit room, and
    /// a slide on every page is too much for some readers, so both are a reader's to say, on
    /// this desk like the layout.
    Q_PROPERTY(Backdrop backdrop READ backdrop NOTIFY changed)
    Q_PROPERTY(Motion motion READ motion NOTIFY changed)

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

    explicit Preferences(QObject *parent = nullptr);

    static Preferences *create(QQmlEngine *engine, QJSEngine *);

    Appearance appearance() const { return m_appearance; }
    QString appearanceTitle() const;
    QVariantList appearances() const;

    Q_INVOKABLE void chooseAppearance(Appearance wanted);

    PageLayout pageLayout() const { return m_pageLayout; }
    Q_INVOKABLE void choosePageLayout(PageLayout wanted);
    Fit fit() const { return m_fit; }
    Q_INVOKABLE void chooseFit(Fit wanted);
    Backdrop backdrop() const { return m_backdrop; }
    Q_INVOKABLE void chooseBackdrop(Backdrop wanted);
    Motion motion() const { return m_motion; }
    Q_INVOKABLE void chooseMotion(Motion wanted);
    /// What `Auto` resolves to for this window; a fixed choice ignores the window.
    Spreads::Layout layoutFor(QSizeF window) const;

    bool volumesAsGrid() const { return m_volumesAsGrid; }
    void showVolumesAsGrid(bool asGrid);

    QUrl lastPlace() const { return m_lastPlace; }
    /// Remembers where a picker was used. Takes the folder itself, or the folder a chosen
    /// file sits in — what a reader wants next time is the place, not the thing.
    Q_INVOKABLE void rememberPlace(const QUrl &place);

    QVariantList warnings() const;
    QString warningsTitle() const;
    QString cornerTitle() const;
    QString bubbleColumn() const;
    QString desktopColumn() const;
    /// Whether this family warns at all, and whether it escalates to the desktop. Asked by
    /// name so that the one place that decides is the one that holds the answer.
    bool bubbles(Warns which) const;
    bool reachesTheDesktop(Warns which) const;
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

private:
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
    /// Everything on, except the desktop for a download: a copy saved in two seconds wakes
    /// nobody. It is being pestered that makes somebody open this screen, not silence.
    QHash<Warns, bool> m_bubbles;
    QHash<Warns, bool> m_desktop;
    /// Where the desktop puts its own, so the place one has already learned to look — and
    /// the middle stays clear, which is where the eye goes on a wall of covers.
    Corner m_corner = Corner::BottomRight;
};
