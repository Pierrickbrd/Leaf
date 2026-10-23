#include "Theme.h"

#include <QGuiApplication>
#include <QPalette>

namespace {
/// One token, both themes. Paired here rather than in two tables, so a value added to one
/// and forgotten in the other cannot compile.
///
/// The values are QRgb and not strings on purpose. `QString(const char16_t *)` does not
/// exist, and the obvious repair — the single-byte-encoding string view — is refused by
/// `tools/bytes_stay_utf8.py`, which bans that encoding's name anywhere under `desktop/`.
/// Hex integers read exactly like a colour swatch and allocate nothing.
QColor pick(bool dark, QRgb light, QRgb night)
{
    return QColor::fromRgb(dark ? night : light);
}

/// The sixteen profile tints, in palette order. The id is what the server stores, without
/// accent; the two values are the measured ones, light then dark.
struct Tint {
    const char *id;
    QRgb light;
    QRgb night;
};
constexpr Tint Tints[] = {
    {"emeraude", 0x0A6A55u, 0x2FB98Bu}, {"sapin", 0x2E5E2Au, 0x7CC46Fu},
    {"sauge", 0x4A6650u, 0x9DBEA2u},    {"olive", 0x5E6216u, 0xC2C55Au},
    {"ocre", 0x855A0Cu, 0xE5B04Au},     {"ambre", 0x8F4A0Au, 0xF0A050u},
    {"corail", 0xA23B2Au, 0xF2846Eu},   {"framboise", 0xA3264Au, 0xF07A95u},
    {"rose", 0xA33C7Au, 0xEB8CC4u},     {"prune", 0x6E2A63u, 0xD58BCBu},
    {"violet", 0x5B3A8Eu, 0xB49AEEu},   {"indigo", 0x3B4392u, 0x949DF0u},
    {"bleu", 0x1F5590u, 0x6FA8EEu},     {"azur", 0x0C6283u, 0x4FBCE4u},
    {"ardoise", 0x4A5768u, 0xA6B4C6u},  {"terre", 0x6B4E3Au, 0xC9A58Au},
};
constexpr int Slate = 14;
} // namespace

Theme::Theme(QObject *parent) : QObject(parent) {}

void Theme::setDark(bool dark)
{
    if (m_dark == dark)
        return;
    m_dark = dark;
    emit changed();
}

void Theme::follow(int appearance)
{
    // The numbers are `Preferences::Appearance`, passed as an int so that `Theme` — which
    // knows what a palette is and nothing else — does not have to include the object that
    // knows what a reader asked for.
    switch (appearance) {
    case 1:
        setDark(false);
        return;
    case 2:
        setDark(true);
        return;
    default:
        followSystem();
    }
}

qreal Theme::hairOf(QWindow *window) const
{
    const qreal ratio = window ? window->devicePixelRatio() : 1.0;
    return ratio > 0 ? 1.0 / ratio : 1.0;
}

void Theme::followSystem()
{
    // Lightness, not a colour comparison: a desktop may tint its window colour, and what
    // decides a theme is whether it is dark, not which hue it is dark in.
    //
    // What this depends on, because no test can see it: Qt only puts the desktop's colours
    // in this palette when a platform theme plugin is loaded — `qt6-gtk-platformtheme` on
    // Ubuntu. Without one, Qt answers with its own default, which is light, and the client
    // would come up light on a dark desktop with nothing to say about it. Both tests here
    // set the palette themselves, so neither would notice.
    //
    // Measured on the machine this is built for, 4 September 2026, GNOME set to prefer-dark:
    // the window colour comes back #323232, lightness 50 against the 128 threshold, and the
    // dark theme is chosen. The plugin is installed and this works.
    //
    // Qt 6.5 answers the question directly with QStyleHints::colorScheme, which reads the
    // XDG appearance portal and needs no plugin. When this moves past 6.4, prefer it — that
    // is the fix, not a D-Bus reader written here for a case this machine does not have.
    setDark(QGuiApplication::palette().color(QPalette::Window).lightness() < 128);
}

QColor Theme::paper() const        { return pick(m_dark, 0xE9E3D6u, 0x0C100Eu); }
QColor Theme::surface() const      { return pick(m_dark, 0xF0ECE3u, 0x141916u); }
QColor Theme::onBar() const        { return pick(m_dark, 0xEBE2D5u, 0x1B211Du); }
QColor Theme::onPaper() const      { return pick(m_dark, 0xDFD6C9u, 0x1B211Du); }
QColor Theme::rule() const         { return pick(m_dark, 0xC2BEB5u, 0x2A322Cu); }
QColor Theme::ink() const          { return pick(m_dark, 0x1A1D1Bu, 0xE7EBE7u); }
QColor Theme::inkSoft() const      { return pick(m_dark, 0x555C56u, 0x99A39Bu); }
QColor Theme::inkFaint() const     { return pick(m_dark, 0x666C63u, 0x6B756Du); }
QColor Theme::emerald() const      { return pick(m_dark, 0x0A6A55u, 0x2FB98Bu); }
QColor Theme::emeraldWash() const  { return pick(m_dark, 0xC9DCD3u, 0x12241Du); }
QColor Theme::onEmerald() const    { return pick(m_dark, 0xF0ECE3u, 0x08110Du); }
// Garnet, where this was amber. One alert colour and not two: a red beside an orange would
// be the « one colour per value » the architecture refuses for pills, and what changes here is
// the value, not the role. The eleven places that call these — the badge of a card that
// failed, « Abandonner », the notes of the settings screen, the two breakdown banners, the
// Attention tone of the import tree — do not move a line.
//
// The hollow of a hovered row decided the dark value: a deeper garnet fell to 4,44 there,
// under the floor. `keeps_its_contrast` holds the four figures of each theme.
QColor Theme::alert() const        { return pick(m_dark, 0x8E1F2Bu, 0xE0656Eu); }
QColor Theme::alertWash() const    { return pick(m_dark, 0xF2DCE0u, 0x2A1216u); }
QColor Theme::readerPaper() const  { return pick(m_dark, 0xDCD2BCu, 0x070908u); }

QVariantMap Theme::profileTints() const
{
    QVariantMap tints;
    for (const Tint &tint : Tints)
        tints.insert(QString::fromUtf8(tint.id), pick(m_dark, tint.light, tint.night));
    return tints;
}

QColor Theme::profileTintIn(const QVariantMap &tints, const QString &color) const
{
    const QVariant found = tints.value(color);
    if (found.isValid())
        return found.value<QColor>();
    return pick(m_dark, Tints[Slate].light, Tints[Slate].night);
}

// The initial is the surface tone of the theme it is in, the same inversion `onEmerald`
// makes: pale on the dark tints of the light theme, near-black on the bright ones of the
// dark theme. `keeps_its_contrast` holds all thirty-two pairs at 4,5:1.
QColor Theme::profileInk() const   { return pick(m_dark, 0xF0ECE3u, 0x0C100Eu); }

QStringList Theme::profileColors() const
{
    QStringList ids;
    for (const Tint &tint : Tints)
        ids << QString::fromUtf8(tint.id);
    return ids;
}
