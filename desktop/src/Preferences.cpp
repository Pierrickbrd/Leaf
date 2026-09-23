#include "Preferences.h"

#include "Words.h"

#include <algorithm>
#include <array>

#include <QVariantMap>
#include <memory>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

/// Written as a word and never as a number: a file holding `2` is a file nobody can read,
/// and an enumeration that gains a value in the middle would silently move everyone's
/// choice.
QString spell(Preferences::Appearance which)
{
    using enum Preferences::Appearance;

    switch (which) {
    case Light:
        return u"light"_s;
    case Dark:
        return u"dark"_s;
    case System:
        break;
    }
    return u"system"_s;
}

Preferences::Appearance read(const QString &word)
{
    using enum Preferences::Appearance;

    if (word == u"light"_s)
        return Light;
    if (word == u"dark"_s)
        return Dark;
    // A word this version does not know is the desktop's choice, which is the answer that
    // is right whatever the word meant.
    return System;
}

/// The word a family is written under in the file. Written out rather than numbered: a file
/// holding `warns/2=false` is a file nobody can read, and the order of an enum is not a
/// promise anybody made to a settings file.
QString spell(Preferences::Warns which)
{
    using enum Preferences::Warns;
    switch (which) {
    case Imports:
        return u"imports"_s;
    case Scans:
        return u"scans"_s;
    case Downloads:
        return u"downloads"_s;
    case Failures:
        break;
    }
    return u"failures"_s;
}

QString spell(Preferences::Corner where)
{
    using enum Preferences::Corner;
    switch (where) {
    case TopLeft:
        return u"topLeft"_s;
    case Top:
        return u"top"_s;
    case TopRight:
        return u"topRight"_s;
    case BottomLeft:
        return u"bottomLeft"_s;
    case Bottom:
        return u"bottom"_s;
    case BottomRight:
        break;
    }
    return u"bottomRight"_s;
}

/// A word this client has not been taught is the bottom right, which is where the desktop
/// puts its own — the same rule the appearance reads its file by.
Preferences::Corner readCorner(const QString &word)
{
    using enum Preferences::Corner;
    for (const Preferences::Corner where :
         {TopLeft, Top, TopRight, BottomLeft, Bottom, BottomRight}) {
        if (spell(where) == word)
            return where;
    }
    return BottomRight;
}

} // namespace

Preferences *Preferences::create(QQmlEngine *, QJSEngine *)
{
    // Owned until the handoff, then the engine owns it: `QML_SINGLETON` takes the pointer
    // and deletes it with the engine. Written this way so that a throw between here and
    // the return could not leak it.
    return std::make_unique<Preferences>().release();
}

Preferences::Preferences(QObject *parent)
    : QObject(parent)
{
    m_appearance = read(m_file.value(u"appearance"_s).toString());
    m_lastPlace = QUrl(m_file.value(u"lastPlace"_s).toString());
    m_volumesAsGrid = m_file.value(u"volumesAsGrid"_s, false).toBool();

    // Everything on at the start, except the desktop for a download: a copy saved in two
    // seconds wakes nobody. It is being pestered that makes somebody open that screen, not
    // silence — so the defaults are noisy and the reader turns off what they do not want.
    using enum Warns;
    for (const Warns which : {Imports, Scans, Downloads, Failures}) {
        m_bubbles.insert(which, m_file.value(u"bubbles/"_s + spell(which), true).toBool());
        m_desktop.insert(which, m_file.value(u"desktop/"_s + spell(which),
                                             which != Downloads).toBool());
    }
    m_corner = readCorner(m_file.value(u"corner"_s).toString());
}

QVariantList Preferences::warnings() const
{
    using enum Warns;
    QVariantList said;
    for (const Warns which : {Imports, Scans, Downloads, Failures}) {
        said.append(QVariantMap{{u"name"_s, spell(which)},
                                {u"value"_s, std::to_underlying(which)},
                                {u"label"_s, Words::warns(which)},
                                {u"detail"_s, Words::warnsAbout(which)},
                                {u"bubble"_s, bubbles(which)},
                                {u"desktop"_s, reachesTheDesktop(which)}});
    }
    return said;
}

QString Preferences::warningsTitle() const
{
    return Words::whatWarnsYou();
}

QString Preferences::cornerTitle() const
{
    return Words::whereBubblesAppear();
}

QString Preferences::bubbleColumn() const
{
    return Words::bubbleColumn();
}

QString Preferences::desktopColumn() const
{
    return Words::desktopColumn();
}

bool Preferences::bubbles(Warns which) const
{
    return m_bubbles.value(which, true);
}

bool Preferences::reachesTheDesktop(Warns which) const
{
    return m_desktop.value(which, true);
}

bool Preferences::anythingBubbles() const
{
    using enum Warns;
    return std::ranges::any_of(std::array{Imports, Scans, Downloads, Failures},
                               [this](const Warns which) { return bubbles(which); });
}

void Preferences::showBubble(Warns which, bool wanted)
{
    if (bubbles(which) == wanted)
        return;
    m_bubbles.insert(which, wanted);
    m_file.setValue(u"bubbles/"_s + spell(which), wanted);
    m_file.sync();
    emit changed();
}

void Preferences::reachTheDesktop(Warns which, bool wanted)
{
    if (reachesTheDesktop(which) == wanted)
        return;
    m_desktop.insert(which, wanted);
    m_file.setValue(u"desktop/"_s + spell(which), wanted);
    m_file.sync();
    emit changed();
}

QString Preferences::cornerLabel() const
{
    return Words::corner(m_corner);
}

void Preferences::putBubbles(Corner where)
{
    if (where == m_corner)
        return;
    m_corner = where;
    m_file.setValue(u"corner"_s, spell(where));
    m_file.sync();
    emit changed();
}

QString Preferences::appearanceTitle() const
{
    return Words::appearance();
}

QVariantList Preferences::appearances() const
{
    return {
        QVariantMap{{u"value"_s, std::to_underlying(Appearance::System)},
                    {u"label"_s, Words::appearanceChoice(Words::Looks::System)},
                    {u"icon"_s, u"contrast"_s}},
        QVariantMap{{u"value"_s, std::to_underlying(Appearance::Light)},
                    {u"label"_s, Words::appearanceChoice(Words::Looks::Light)},
                    {u"icon"_s, u"light_mode"_s}},
        QVariantMap{{u"value"_s, std::to_underlying(Appearance::Dark)},
                    {u"label"_s, Words::appearanceChoice(Words::Looks::Dark)},
                    {u"icon"_s, u"dark_mode"_s}},
    };
}

void Preferences::chooseAppearance(Appearance wanted)
{
    if (wanted == m_appearance)
        return;

    m_appearance = wanted;
    // Written at once and not on the way out: an application that is killed, or that
    // crashes, has still been told what somebody wanted.
    m_file.setValue(u"appearance"_s, spell(wanted));
    m_file.sync();
    emit changed();
    emit appearanceChosen(wanted);
}

void Preferences::rememberPlace(const QUrl &place)
{
    if (!place.isValid() || place.isEmpty() || place == m_lastPlace)
        return;
    m_lastPlace = place;
    m_file.setValue(u"lastPlace"_s, place.toString());
}

void Preferences::showVolumesAsGrid(bool asGrid)
{
    if (asGrid == m_volumesAsGrid)
        return;
    m_volumesAsGrid = asGrid;
    m_file.setValue(u"volumesAsGrid"_s, asGrid);
    m_file.sync();
    // Once. It was said before the write and again after it, which is two notifications for
    // one fact and one redraw of everything bound to this object for nothing.
    emit changed();
}
