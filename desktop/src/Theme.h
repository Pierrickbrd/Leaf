#pragma once

// The two palettes, and which one is on.
//
// The values below are the ones whose contrast was measured — #E9E3D6 for paper, #0A6A55 for
// emerald. A close cousin of each circulates elsewhere as prose, #EDE7DA and #0B6B4F, close
// enough to pass at a glance and never measured. Recopying that pair over the measured ones
// is a regression that compiles.
//
// The cover shadow is painted by CoverShadow.qml from a nine-slice texture: every delegate
// shares two small decoded images instead of running a blur while the grid scrolls. Its values
// are the measured card elevation —
//   card: 0 1px 2px rgb(26 29 27 / .07), 0 8px 24px -6px rgb(26 29 27 / .14)
//         dark: 0 1px 2px rgb(0 0 0 / .55), 0 10px 28px -8px rgb(0 0 0 / .65)
// The bar's smaller shadow belongs to the bar itself, which is not drawn yet:
//   bar:  0 1px 1px rgb(26 29 27 / .05)   dark: 0 1px 1px rgb(0 0 0 / .4)
// Qt 6.4.2 has no MultiEffect, and a blur recomputed by every delegate of a scrolling grid is
// the cost the texture exists to avoid — two decoded images shared by all of them instead.
// (This used to add that CI had no Qt5Compat GraphicalEffects module at all. It has, since
// `qml6-module-qt5compat-graphicaleffects` joined the workflow's apt line, and the QML has
// been importing it for a while; the reason above is the one that still holds.) Elevation and
// tone stay coupled: a card carries a shadow, therefore its tone gap can stay small.

#include "Fonts.h"

#include <QColor>
#include <QObject>
#include <QQmlEngine>
#include <QString>

class Theme : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY changed)

    Q_PROPERTY(QColor paper READ paper NOTIFY changed)
    Q_PROPERTY(QColor surface READ surface NOTIFY changed)
    Q_PROPERTY(QColor onBar READ onBar NOTIFY changed)
    Q_PROPERTY(QColor onPaper READ onPaper NOTIFY changed)
    Q_PROPERTY(QColor rule READ rule NOTIFY changed)
    Q_PROPERTY(QColor ink READ ink NOTIFY changed)
    Q_PROPERTY(QColor inkSoft READ inkSoft NOTIFY changed)
    Q_PROPERTY(QColor inkFaint READ inkFaint NOTIFY changed)
    Q_PROPERTY(QColor emerald READ emerald NOTIFY changed)
    Q_PROPERTY(QColor emeraldWash READ emeraldWash NOTIFY changed)
    /// What is written on an emerald pill — the resume band's button. Emerald is dark in
    /// the light theme and bright in the dark one, so its label turns over with it; a
    /// single fixed tone is unreadable on one of the two.
    Q_PROPERTY(QColor onEmerald READ onEmerald NOTIFY changed)
    Q_PROPERTY(QColor alert READ alert NOTIFY changed)
    Q_PROPERTY(QColor alertWash READ alertWash NOTIFY changed)
    /// The reader has paper of its own, deeper than the application's: a white page needs
    /// 1.50:1 behind it to keep an edge, and an application background cannot go that dark.
    Q_PROPERTY(QColor readerPaper READ readerPaper NOTIFY changed)

    Q_PROPERTY(int cardRadius READ cardRadius CONSTANT)
    Q_PROPERTY(int coverRadius READ coverRadius CONSTANT)
    Q_PROPERTY(int buttonRadius READ buttonRadius CONSTANT)
    /// How far the focus ring stands off the cover. Wider in the dark, because a light gap
    /// reads wider than a dark one of the same size.
    Q_PROPERTY(int focusGap READ focusGap NOTIFY changed)

    /// The two families, reached from QML through Theme rather than a second singleton:
    /// `Fonts` is a C++ namespace, not a QML type, and a family is a token like a colour —
    /// it describes how the interface is painted.
    Q_PROPERTY(QString displayFamily READ displayFamily CONSTANT)
    Q_PROPERTY(QString textFamily READ textFamily CONSTANT)

public:
    explicit Theme(QObject *parent = nullptr);

    bool dark() const { return m_dark; }
    void setDark(bool dark);

    /// Qt 6.4 has no QStyleHints::colorScheme — it arrived in 6.5 — so the desktop's own
    /// window colour is what there is to read.
    Q_INVOKABLE void followSystem();
    /// What the reader asked for, when they asked for something. `System` hands the choice
    /// back to the desktop, which is what `followSystem` reads.
    Q_INVOKABLE void follow(int appearance);

    QColor paper() const;
    QColor surface() const;
    QColor onBar() const;
    QColor onPaper() const;
    QColor rule() const;
    QColor ink() const;
    QColor inkSoft() const;
    QColor inkFaint() const;
    QColor emerald() const;
    QColor emeraldWash() const;
    QColor onEmerald() const;
    QColor alert() const;
    QColor alertWash() const;
    QColor readerPaper() const;

    int cardRadius() const { return 18; }
    int coverRadius() const { return 12; }
    int buttonRadius() const { return 12; }
    int focusGap() const { return m_dark ? 3 : 2; }

    QString displayFamily() const { return Fonts::display(); }
    QString textFamily() const { return Fonts::text(); }

signals:
    void changed();

private:
    bool m_dark = false;
};
