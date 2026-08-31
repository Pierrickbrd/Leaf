#pragma once

// Which screen we are on, and how one comes back.
//
// In C++ rather than a QML StackView, for the reason the whole block is arranged this way:
// every test here runs without a window, so state held in QML is state `ctest` cannot see. A
// regression where Escape stopped going back would pass continuous integration green.
//
// The map is fixed, not incidental:
//
//   Shelf — the home. Series → Reader hang off it. Health and Settings are dead ends one
//   comes back from. There is no "library" destination, because the home is the library.

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QVariantMap>
#include <QList>

class Navigation : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(Destination destination READ destination NOTIFY changed)
    Q_PROPERTY(QVariantMap parameters READ parameters NOTIFY changed)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY changed)
    /// « Étagère », « Série », … — `Words::destination(destination())`, kept on this
    /// singleton rather than a second one, the way `Theme` carries the two font families
    /// rather than exposing `Fonts` itself.
    Q_PROPERTY(QString label READ label NOTIFY changed)

public:
    enum class Destination { Shelf, Series, Reader, Health, Settings };
    Q_ENUM(Destination)

    explicit Navigation(QObject *parent = nullptr);

    Destination destination() const { return m_stack.last().where; }
    QVariantMap parameters() const { return m_stack.last().with; }
    bool canGoBack() const { return m_stack.size() > 1; }
    QString label() const;

    /// False, and nothing changed, when what is asked makes no sense — a series with no
    /// identifier, a destination outside the enumeration. It neither throws nor shows: an
    /// empty screen is a costlier lie than a refusal.
    Q_INVOKABLE bool open(Destination where, const QVariantMap &with = {});

    /// Pops. At the root it does nothing, and says nothing.
    Q_INVOKABLE void back();

signals:
    void changed();

private:
    struct Step {
        Destination where;
        QVariantMap with;
    };

    QList<Step> m_stack;
};
