#pragma once

// What this desktop says about the size of text, for `Widths::follow`.
//
// Qt 6 carries none of it. Measured on 23/10/2026 on the owner's GNOME session: the logical
// DPI stays 96 with high-density scaling on, the default font stays 11 pt whatever GNOME's
// « Taille du texte » says, and `QT_FONT_DPI` becomes a device-pixel ratio that enlarges the
// whole window rather than its words. So the setting is read where the desktop keeps it.

#include "Widths.h"

#include <QByteArray>
#include <QDBusConnection>
#include <QDBusVariant>

/// The desktop's text size, read and followed live.
///
/// - **GNOME**, on Wayland and X11: `text-scaling-factor` of `org.gnome.desktop.interface`,
///   through the settings portal, which answers without GSettings and from inside a sandbox;
///   its `SettingChanged` signal makes a change in the desktop's settings land at once.
/// - **Plasma**, when the portal has no GNOME setting to give and the desktop says it is KDE:
///   Plasma has no text factor, and its « force font DPI » arrives as the platform font's
///   size, so the factor is that size against Plasma's default of 10 points, followed through
///   the application's font-change event. Not verified on a Plasma session: there was none to
///   try it on.
/// - **Anything else**: 1.
///
/// `bus` is the bus the desktop half speaks on, handed in as `Boot::run` hands it to the
/// notifier: a test that boots the window hands one that goes nowhere, and reads 1 rather than
/// the text size of whoever ran it. `desktop` is `XDG_CURRENT_DESKTOP`, handed in for the same
/// reason.
class DesktopText : public SystemText
{
    Q_OBJECT

public:
    /// Plasma's default font size, against which its platform font is a factor.
    static constexpr qreal PlasmaPoints = 10.0;

    DesktopText(QDBusConnection bus, const QByteArray &desktop, QObject *parent = nullptr);

    qreal factor() const override { return m_factor; }

    /// What a portal answer holds, unwrapped however many variants deep, or 0 when it holds
    /// no number.
    static qreal numberIn(const QVariant &answer);

private slots:
    void settingChanged(const QString &space, const QString &key, const QDBusVariant &value);

private:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void take(qreal factor);
    void followTheFont();
    void readTheFont();

    qreal m_factor = 1.0;
    bool m_followsTheFont = false;
};
