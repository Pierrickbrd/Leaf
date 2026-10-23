#pragma once

// A link drawn as a QR code, for « Relier un autre appareil »: a phone's camera reads it
// off this screen, and nobody types a code.
//
// The encoding is Nayuki's `qrcodegen` (third_party/qrcodegen), not written here: a QR code
// is Reed-Solomon codewords, interleaved blocks and eight masks scored against each other,
// and a mistake in any of them is a picture no camera reads — found by somebody holding a
// phone up to the screen, never by a test of this client. What is written here is what goes
// in and how it is drawn.
//
// Always dark on light, whatever the theme: a scanner looks for dark modules on a light
// ground, and many read an inverted code poorly or not at all.

#include <QList>
#include <QQuickPaintedItem>
#include <QString>

namespace QrImage {

/// The light border a scanner finds the code by, in modules. Four is the standard's.
inline constexpr int QuietZone = 4;

/// The code's squares, row after row, without the quiet zone.
struct Modules {
    /// Modules per side; 0 when there was nothing to draw, or too much to fit in a code.
    int size = 0;
    QList<bool> dark;

    bool at(int x, int y) const { return dark.at(y * size + x); }
};

/// `text`'s UTF-8 bytes, at the medium level of correction: a quarter of a code may be
/// smudged, glared or folded and it still reads, and a link of a hundred-odd characters
/// still fits a code a phone resolves from across a desk.
Modules modules(const QString &text);

} // namespace QrImage

/// What QML places: a square that draws `text` as a QR code, quiet zone included.
class QrCode : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)

public:
    explicit QrCode(QQuickItem *parent = nullptr);

    QString text() const { return m_text; }
    void setText(const QString &text);

    void paint(QPainter *painter) override;

signals:
    void textChanged();

private:
    QString m_text;
    QrImage::Modules m_modules;
};
