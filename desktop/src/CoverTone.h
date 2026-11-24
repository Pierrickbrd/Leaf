#pragma once

// Whether the number a borrowed cover carries is written light or dark.
//
// A volume with no cover of its own shows its series' and writes its number over the foot of
// it, on a veil (decision of 24/11/2026). White on that veil is right over almost every
// illustration and wrong over a pale one: the veil darkens the foot by half at the height of
// the number, and half of a white sky is a grey white letters vanish into. So the choice is
// made from the picture itself, as it is drawn — the band the number sits on, veiled — and
// not from a rule about covers.
//
// One per series page and not one per tile: every borrowed cover of an edition is the same
// picture, the series', so a hundred and twenty chapters are measured once.

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QUrl>

class QNetworkReply;

class CoverTone : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    /// The cover to measure. Empty measures nothing and says dark.
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    /// Whether the band under the number is light enough, veiled, that a dark number reads
    /// better than a white one.
    Q_PROPERTY(bool light READ light NOTIFY lightChanged)

public:
    explicit CoverTone(QObject *parent = nullptr);
    ~CoverTone() override;

    QUrl source() const { return m_source; }
    void setSource(const QUrl &source);
    bool light() const { return m_light; }

    /// The decision, on a picture already in hand: the rows from three quarters of the way
    /// down to the foot, each darkened by the veil as it is drawn there, and the contrast a
    /// white and a dark number would each have against what is left. Public because it is
    /// the whole of what is worth testing; the rest is a download.
    static bool lightUnderVeil(const QImage &cover);

signals:
    void sourceChanged();
    void lightChanged();

private:
    void measured(QNetworkReply *reply);
    void setLight(bool light);

    QUrl m_source;
    bool m_light = false;
    QPointer<QNetworkReply> m_reply;
};
