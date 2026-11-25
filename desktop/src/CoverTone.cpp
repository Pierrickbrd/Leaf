#include "CoverTone.h"

#include "Pinning.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QQmlEngine>

#include <algorithm>
#include <cmath>

namespace {

/// What `CoverVeil` draws under a number: black, from nothing at half the height to this at
/// the foot. The two are one decision and live in two languages; this is the copy.
constexpr double VeilAtTheFoot = 0.72;

/// The rows the number sits on, as fractions of the height: its top to just above the bar.
constexpr double BandFrom = 0.75;
constexpr double BandTo = 0.96;

/// The dark a number is written in when it is not white: `Theme.ink` in the light theme,
/// the one that reads on a picture whatever the theme around it.
constexpr double DarkInk = 0.0118;

double linear(double channel)
{
    return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
}

double luminance(double red, double green, double blue)
{
    return 0.2126 * linear(red) + 0.7152 * linear(green) + 0.0722 * linear(blue);
}

} // namespace

CoverTone::CoverTone(QObject *parent) : QObject(parent) {}

CoverTone::~CoverTone()
{
    if (m_reply)
        m_reply->abort();
}

void CoverTone::setSource(const QUrl &source)
{
    if (source == m_source)
        return;
    m_source = source;
    emit sourceChanged();
    if (m_reply)
        m_reply->abort();
    const QQmlEngine *engine = qmlEngine(this);
    if (m_source.isEmpty() || !engine || !engine->networkAccessManager()) {
        setLight(false);
        return;
    }
    // The engine's own manager: the one `Covers` puts the key on, and whose cache already
    // holds the picture the tiles asked for.
    m_reply = engine->networkAccessManager()->get(Pinning::request(m_source));
    QNetworkReply *reply = m_reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] { measured(reply); });
}

void CoverTone::measured(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply != m_reply)
        return;
    m_reply = nullptr;
    if (reply->error() != QNetworkReply::NoError) {
        setLight(false);
        return;
    }
    setLight(lightUnderVeil(QImage::fromData(reply->readAll())));
}

void CoverTone::setLight(bool light)
{
    if (light == m_light)
        return;
    m_light = light;
    emit lightChanged();
}

bool CoverTone::lightUnderVeil(const QImage &cover)
{
    if (cover.isNull())
        return false;
    // Small: a mean over a band needs no detail, and a cover arrives at 400 wide.
    const QImage small = cover.scaledToWidth(48, Qt::SmoothTransformation)
                             .convertToFormat(QImage::Format_RGB32);
    const int height = small.height();
    double total = 0;
    int counted = 0;
    for (auto y = int(height * BandFrom); y < int(std::ceil(height * BandTo)) && y < height; ++y) {
        const double down = (double(y) + 0.5) / height;
        const double veil = VeilAtTheFoot * std::clamp((down - 0.5) / 0.5, 0.0, 1.0);
        for (int x = 0; x < small.width(); ++x) {
            const QRgb pixel = small.pixel(x, y);
            const double left = 1.0 - veil;
            total += luminance(qRed(pixel) / 255.0 * left, qGreen(pixel) / 255.0 * left,
                               qBlue(pixel) / 255.0 * left);
            ++counted;
        }
    }
    if (counted == 0)
        return false;
    const double under = total / counted;
    const double white = 1.05 / (under + 0.05);
    const double dark = (under + 0.05) / (DarkInk + 0.05);
    return dark > white;
}
