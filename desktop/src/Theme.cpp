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
} // namespace

Theme::Theme(QObject *parent) : QObject(parent) {}

void Theme::setDark(bool dark)
{
    if (m_dark == dark)
        return;
    m_dark = dark;
    emit changed();
}

void Theme::followSystem()
{
    // Lightness, not a colour comparison: a desktop may tint its window colour, and what
    // decides a theme is whether it is dark, not which hue it is dark in.
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
QColor Theme::alert() const        { return pick(m_dark, 0x96400Au, 0xF59E0Bu); }
QColor Theme::alertWash() const    { return pick(m_dark, 0xEBDCCCu, 0x2A1C08u); }
QColor Theme::readerPaper() const  { return pick(m_dark, 0xDCD2BCu, 0x070908u); }
