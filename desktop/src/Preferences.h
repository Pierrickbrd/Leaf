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

#include <QHash>
#include <QObject>
#include <QQmlEngine>
#include <QSettings>
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

    explicit Preferences(QObject *parent = nullptr);

    static Preferences *create(QQmlEngine *engine, QJSEngine *);

    Appearance appearance() const { return m_appearance; }
    QString appearanceTitle() const;
    QVariantList appearances() const;

    Q_INVOKABLE void chooseAppearance(Appearance wanted);

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
    /// Everything on, except the desktop for a download: a copy saved in two seconds wakes
    /// nobody. It is being pestered that makes somebody open this screen, not silence.
    QHash<Warns, bool> m_bubbles;
    QHash<Warns, bool> m_desktop;
    /// Where the desktop puts its own, so the place one has already learned to look — and
    /// the middle stays clear, which is where the eye goes on a wall of covers.
    Corner m_corner = Corner::BottomRight;
};
