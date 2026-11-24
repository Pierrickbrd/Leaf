#include "Preferences.h"

#include "Widths.h"
#include "Words.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <QScopeGuard>
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

/// Words and not numbers, for the same reason as the rest of the file. A word this version
/// does not know falls back to the default instead of failing: the file may be hand-edited
/// or written by a later version.
QString spell(Preferences::PageLayout which)
{
    using enum Preferences::PageLayout;
    switch (which) {
    case Single:
        return u"single"_s;
    case Double:
        return u"double"_s;
    case Auto:
        break;
    }
    return u"auto"_s;
}

Preferences::PageLayout readLayout(const QString &word)
{
    using enum Preferences::PageLayout;
    for (const Preferences::PageLayout which : {Single, Double, Auto}) {
        if (spell(which) == word)
            return which;
    }
    return Auto;
}

QString spell(Preferences::Fit which)
{
    using enum Preferences::Fit;
    switch (which) {
    case Width:
        return u"width"_s;
    case Original:
        return u"original"_s;
    case Height:
        break;
    }
    return u"height"_s;
}

/// A width read back from the file: nothing that is not a positive number is one.
double aWidth(const QVariant &held)
{
    bool number = false;
    const double width = held.toDouble(&number);
    return number && std::isfinite(width) && width > 0 ? width : 0;
}

Preferences::Fit readFit(const QString &word)
{
    using enum Preferences::Fit;
    for (const Preferences::Fit which : {Height, Width, Original}) {
        if (spell(which) == word)
            return which;
    }
    return Height;
}

QString spell(Preferences::Backdrop which)
{
    using enum Preferences::Backdrop;
    switch (which) {
    case Grey:
        return u"grey"_s;
    case White:
        return u"white"_s;
    case Black:
        break;
    }
    return u"black"_s;
}

Preferences::Backdrop readBackdrop(const QString &word)
{
    using enum Preferences::Backdrop;
    for (const Preferences::Backdrop which : {Black, Grey, White}) {
        if (spell(which) == word)
            return which;
    }
    return Black;
}

QString spell(Preferences::Motion which)
{
    using enum Preferences::Motion;
    switch (which) {
    case Fade:
        return u"fade"_s;
    case Instant:
        return u"instant"_s;
    case Slide:
        break;
    }
    return u"slide"_s;
}

Preferences::Motion readMotion(const QString &word)
{
    using enum Preferences::Motion;
    for (const Preferences::Motion which : {Slide, Fade, Instant}) {
        if (spell(which) == word)
            return which;
    }
    return Slide;
}

QString spell(Preferences::StripControls which)
{
    using enum Preferences::StripControls;
    switch (which) {
    case OnTheWayUp:
        return u"up"_s;
    case OnClick:
        return u"click"_s;
    case Discreet:
        break;
    }
    return u"discreet"_s;
}

Preferences::StripControls readStripControls(const QString &word)
{
    using enum Preferences::StripControls;
    for (const Preferences::StripControls which : {Discreet, OnTheWayUp, OnClick}) {
        if (spell(which) == word)
            return which;
    }
    return Discreet;
}

/// One of the eleven steps, from what the file holds; anything else is `fallback`.
int aStep(const QVariant &held, int fallback)
{
    bool number = false;
    const int step = held.toInt(&number);
    return number ? std::clamp(step, 0, Preferences::LastStep) : fallback;
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
    m_pageLayout = readLayout(m_file.value(u"reader/layout"_s).toString());
    m_fit = readFit(m_file.value(u"reader/fit"_s).toString());
    m_backdrop = readBackdrop(m_file.value(u"reader/backdrop"_s).toString());
    m_motion = readMotion(m_file.value(u"reader/motion"_s).toString());
    m_stripSensitivity = aStep(m_file.value(u"strip/sensitivity"_s), MiddleStep);
    m_stripAutoSpeed = aStep(m_file.value(u"strip/autoSpeed"_s), AutoStep);
    m_stripControls = readStripControls(m_file.value(u"strip/controls"_s).toString());
    m_stripAtRest = m_file.value(u"strip/atRest"_s, false).toBool();
    m_corner = readCorner(m_file.value(u"corner"_s).toString());
}

QVariantList Preferences::warnings() const
{
    using enum Warns;
    QVariantList said;
    for (const Warns which : {Imports, Scans, Downloads, Failures}) {
        if (!concerns(which))
            continue;
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

QString Preferences::eventColumn() const
{
    return Words::eventColumn();
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
    return concerns(which) && m_bubbles.value(which, true);
}

bool Preferences::reachesTheDesktop(Warns which) const
{
    return concerns(which) && m_desktop.value(which, true);
}

bool Preferences::concerns(Warns which) const
{
    using enum Warns;
    switch (which) {
    case Imports: return m_concernsImports;
    case Scans: return m_concernsScans;
    case Downloads: return m_concernsDownloads;
    case Failures: break;
    }
    return true;
}

void Preferences::concern(bool imports, bool scans, bool downloads)
{
    if (imports == m_concernsImports && scans == m_concernsScans
        && downloads == m_concernsDownloads)
        return;
    m_concernsImports = imports;
    m_concernsScans = scans;
    m_concernsDownloads = downloads;
    emit changed();
}

bool Preferences::anythingBubbles() const
{
    using enum Warns;
    return std::ranges::any_of(std::array{Imports, Scans, Downloads, Failures},
                               [this](const Warns which) { return bubbles(which); });
}

void Preferences::showBubble(Warns which, bool wanted)
{
    if (m_bubbles.value(which, true) == wanted)
        return;
    m_bubbles.insert(which, wanted);
    m_file.setValue(u"bubbles/"_s + spell(which), wanted);
    m_file.sync();
    emit changed();
}

void Preferences::reachTheDesktop(Warns which, bool wanted)
{
    if (m_desktop.value(which, true) == wanted)
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

QString Preferences::readerDefaultsTitle() const
{
    return Words::readerDefaults();
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

QVariantMap Preferences::windowFrame() const
{
    const int width = m_file.value(u"window/width"_s, 0).toInt();
    const int height = m_file.value(u"window/height"_s, 0).toInt();
    if (width <= 0 || height <= 0)
        return {};
    return {{u"width"_s, width},
            {u"height"_s, height},
            {u"maximized"_s, m_file.value(u"window/maximized"_s, false).toBool()}};
}

void Preferences::rememberWindow(int width, int height, bool maximized)
{
    // A maximised window keeps the size it had before, which is the one to come back to when
    // it is restored: the screen's own is no choice of the reader's.
    if (!maximized && width > 0 && height > 0) {
        m_file.setValue(u"window/width"_s, width);
        m_file.setValue(u"window/height"_s, height);
    }
    m_file.setValue(u"window/maximized"_s, maximized);
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
    announce();
}

void Preferences::choosePageLayout(PageLayout wanted)
{
    if (wanted == m_pageLayout)
        return;
    m_pageLayout = wanted;
    m_file.setValue(u"reader/layout"_s, spell(wanted));
    m_file.sync();
    emit changed();
    announce();
}

void Preferences::chooseFit(Fit wanted)
{
    if (wanted == m_fit)
        return;
    m_fit = wanted;
    m_file.setValue(u"reader/fit"_s, spell(wanted));
    m_file.sync();
    emit changed();
    announce();
}

Preferences::Strip Preferences::strip(const QString &series, const QString &entry) const
{
    const QString at = u"strips/"_s + series;
    return {aWidth(m_file.value(at + u"/width"_s)), aWidth(m_file.value(at + u"/base"_s)),
            aWidth(m_file.value(at + u"/chapters/"_s + entry))};
}

void Preferences::keepStrip(const QString &series, const QString &entry, const Strip &kept)
{
    if (series.isEmpty())
        return;
    const QString at = u"strips/"_s + series;
    m_file.setValue(at + u"/width"_s, kept.width);
    m_file.setValue(at + u"/base"_s, kept.base);
    if (kept.chapter > 0 && !entry.isEmpty())
        m_file.setValue(at + u"/chapters/"_s + entry, kept.chapter);
    else if (!entry.isEmpty())
        m_file.remove(at + u"/chapters/"_s + entry);
    m_file.sync();
}

void Preferences::chooseBackdrop(Backdrop wanted)
{
    if (wanted == m_backdrop)
        return;
    m_backdrop = wanted;
    m_file.setValue(u"reader/backdrop"_s, spell(wanted));
    m_file.sync();
    emit changed();
    announce();
}

void Preferences::chooseStripSensitivity(int step)
{
    const int kept = std::clamp(step, 0, LastStep);
    if (kept == m_stripSensitivity)
        return;
    m_stripSensitivity = kept;
    keepHere(u"strip/sensitivity"_s, kept);
}

void Preferences::chooseStripAutoSpeed(int step)
{
    const int kept = std::clamp(step, 0, LastStep);
    if (kept == m_stripAutoSpeed)
        return;
    m_stripAutoSpeed = kept;
    keepHere(u"strip/autoSpeed"_s, kept);
}

void Preferences::chooseStripControls(StripControls wanted)
{
    if (wanted == m_stripControls)
        return;
    m_stripControls = wanted;
    keepHere(u"strip/controls"_s, spell(wanted));
}

void Preferences::chooseStripAtRest(bool wanted)
{
    if (wanted == m_stripAtRest)
        return;
    m_stripAtRest = wanted;
    keepHere(u"strip/atRest"_s, wanted);
}

void Preferences::keepHere(const QString &key, const QVariant &value)
{
    // Not `announce`d: these follow the desk, not the profile.
    m_file.setValue(key, value);
    m_file.sync();
    emit changed();
}

void Preferences::chooseMotion(Motion wanted)
{
    if (wanted == m_motion)
        return;
    m_motion = wanted;
    m_file.setValue(u"reader/motion"_s, spell(wanted));
    m_file.sync();
    emit changed();
    announce();
}

QVariantMap Preferences::synced() const
{
    return {{u"layout"_s, spell(m_pageLayout)},
            {u"fit"_s, spell(m_fit)},
            {u"backdrop"_s, spell(m_backdrop)},
            {u"motion"_s, spell(m_motion)},
            {u"volumesAsGrid"_s, m_volumesAsGrid}};
}

void Preferences::announce()
{
    if (!m_adopting)
        emit syncedChanged(synced());
}

void Preferences::adopt(const QVariantMap &held)
{
    // The ordinary setters, with the echo switched off: one place writes each choice, and a
    // word this version does not know falls back to the default through the same readers the
    // file is read by.
    const QScopeGuard done([this] { m_adopting = false; });
    m_adopting = true;
    if (held.contains(u"layout"_s))
        choosePageLayout(readLayout(held.value(u"layout"_s).toString()));
    if (held.contains(u"fit"_s))
        chooseFit(readFit(held.value(u"fit"_s).toString()));
    if (held.contains(u"backdrop"_s))
        chooseBackdrop(readBackdrop(held.value(u"backdrop"_s).toString()));
    if (held.contains(u"motion"_s))
        chooseMotion(readMotion(held.value(u"motion"_s).toString()));
    // A flag is a flag: anything else is not an answer to this question.
    if (held.value(u"volumesAsGrid"_s).typeId() == QMetaType::Bool)
        showVolumesAsGrid(held.value(u"volumesAsGrid"_s).toBool());
}

Spreads::Layout Preferences::layoutFor(QSizeF window) const
{
    switch (m_pageLayout) {
    case PageLayout::Single:
        return Spreads::Layout::Single;
    case PageLayout::Double:
        return Spreads::Layout::Double;
    case PageLayout::Auto:
        break;
    }
    // Two pages beyond 1100 px when the window is wider than tall (spec §03). The width is
    // `Widths`' own break and not a second copy of it: below it each half of a pair is under
    // four hundred pixels. Wider than tall alone sent a 1000 × 700 window to two pages of
    // three hundred and fifty. Strictly wider: a square window has no room for a second page
    // beside the first.
    const bool wide = Widths::bandFor(int(window.width())) == Widths::Band::Wide;
    return wide && window.width() > window.height() ? Spreads::Layout::Double
                                                    : Spreads::Layout::Single;
}
