#pragma once

// What this reader prefers, and the one thing in Leaf that is written.
//
// `Settings.h` says read, never written, and that rule stands — it is about the
// **deployment**: where a server lives and how to prove who you are are configured by
// environment variables at both ends, and a client that grew a connection console would be
// managing something that is not its subject.
//
// A preference is not that. How this application looks on this machine is nobody's business
// but the person looking at it, and it has nowhere else to live. So it is written, and
// written apart: `leaf.conf` is the deployment's file, refused when anybody but its owner
// can read it, and a preference has no reason to sit in a file with that rule on it.

#include <QObject>
#include <QQmlEngine>
#include <QSettings>
#include <QString>
#include <QVariantList>

class Preferences : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// What the reader asked for, which is not what is being drawn: `System` is a choice,
    /// and the palette it resolves to is the desktop's business.
    Q_PROPERTY(Appearance appearance READ appearance NOTIFY changed)
    Q_PROPERTY(QString appearanceTitle READ appearanceTitle CONSTANT)
    /// `[{ value, label, icon }]`, in the order they are offered.
    Q_PROPERTY(QVariantList appearances READ appearances CONSTANT)

public:
    enum class Appearance { System, Light, Dark };
    Q_ENUM(Appearance)

    explicit Preferences(QObject *parent = nullptr);

    static Preferences *create(QQmlEngine *engine, QJSEngine *);

    Appearance appearance() const { return m_appearance; }
    QString appearanceTitle() const;
    QVariantList appearances() const;

    Q_INVOKABLE void chooseAppearance(Appearance wanted);

signals:
    void changed();
    /// The choice moved. `Theme` listens: it is the one that knows what a palette is, and
    /// this is the one that knows what was asked for.
    void appearanceChosen(Appearance wanted);

private:
    /// Its own file, named here rather than in the constructor's list: one place says
    /// where Leaf's preferences live, and it is the line that declares the member.
    QSettings m_file{QSettings::IniFormat, QSettings::UserScope, QStringLiteral("Leaf"),
                     QStringLiteral("preferences")};
    Appearance m_appearance = Appearance::System;
};
