#include "QrImage.h"

#include <QColor>
#include <QPainter>

#include <qrcodegen.hpp>

#include <cstdint>
#include <vector>

namespace {

// The design's two: the app's darkest green for the modules, and the paper of its light
// theme for the ground — as near to black on white as the palette goes, which is what a
// camera needs, without a white square glaring out of a dark window.
const QColor Ink(0x0C, 0x10, 0x0E);
const QColor Ground(0xF2, 0xF4, 0xF2);

} // namespace

QrImage::Modules QrImage::modules(const QString &text)
{
    if (text.isEmpty()) {
        return {};
    }
    const QByteArray bytes = text.toUtf8();
    try {
        // Bytes, not text: `encodeText` would pick a mode from what it is handed as a C
        // string, and a link is UTF-8 a phone must read back character for character.
        const qrcodegen::QrCode code = qrcodegen::QrCode::encodeBinary(
            std::vector<std::uint8_t>(bytes.begin(), bytes.end()),
            qrcodegen::QrCode::Ecc::MEDIUM);
        Modules drawn;
        drawn.size = code.getSize();
        drawn.dark.reserve(drawn.size * drawn.size);
        for (int y = 0; y < drawn.size; ++y) {
            for (int x = 0; x < drawn.size; ++x) {
                drawn.dark.append(code.getModule(x, y));
            }
        }
        return drawn;
    } catch (const qrcodegen::data_too_long &) {
        // Nearly three kilobytes: no link Leaf composes comes near it. Nothing is drawn
        // rather than a code that would read as something else.
        return {};
    }
}

QrCode::QrCode(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    // Squares and nothing else: smoothing would blur the edge a camera looks for.
    setAntialiasing(false);
}

void QrCode::setText(const QString &text)
{
    if (text == m_text) {
        return;
    }
    m_text = text;
    m_modules = QrImage::modules(text);
    emit textChanged();
    update();
}

void QrCode::paint(QPainter *painter)
{
    painter->fillRect(QRectF(0, 0, width(), height()), Ground);
    if (m_modules.size == 0) {
        return;
    }
    // Whole pixels a module, so that every module is the same width: a code drawn at
    // fractional sizes has columns a pixel apart in width, and some phones miss them.
    const int across = m_modules.size + 2 * QrImage::QuietZone;
    const int side = static_cast<int>(qMin(width(), height()));
    const int pixels = qMax(1, side / across);
    const int origin = (side - across * pixels) / 2 + QrImage::QuietZone * pixels;
    for (int y = 0; y < m_modules.size; ++y) {
        for (int x = 0; x < m_modules.size; ++x) {
            if (m_modules.at(x, y)) {
                painter->fillRect(origin + x * pixels, origin + y * pixels, pixels, pixels,
                                  Ink);
            }
        }
    }
}
