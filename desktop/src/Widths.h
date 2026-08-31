#pragma once

// The three widths, in the one place that declares them.
//
// Not `width < …` scattered through every delegate: the grid and the reader have to agree on
// where the break falls, and two copies of a number agree only until one of them is edited.
//
// Where the two numbers come from. Below 1100 a two-page spread gives each half under 400 px
// and stops being readable, so the reader falls back to a single page — which is why this
// break belongs to more than the grid. Below 600 the search field can no longer share the bar
// with anything and takes the screen.
//
// That is the reasoning that chose them, deliberately not a description of the grid and the
// reader: neither is written yet, so a line here claiming what they do could not be checked
// today, and would go quietly false the day they are written differently.

#include <QObject>
#include <QQmlEngine>
#include <QString>

class Widths : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// `windowChanged` fires on every real change of the stored width; `changed` fires only
    /// when the band it falls in crosses. A screen bound to `window` wants the first — the
    /// second would leave it reading a value frozen at the last band crossing.
    Q_PROPERTY(int window READ window WRITE setWindow NOTIFY windowChanged)
    Q_PROPERTY(Band band READ band NOTIFY changed)
    /// « Large », « Moyenne », « Étroite » — `Words::band(band())`, on this singleton for the
    /// same reason `Navigation::label` is on that one rather than on a second singleton.
    Q_PROPERTY(QString bandLabel READ bandLabel NOTIFY changed)

public:
    enum class Band { Narrow, Medium, Wide };
    Q_ENUM(Band)

    explicit Widths(QObject *parent = nullptr);

    int window() const { return m_window; }
    void setWindow(int width);

    Band band() const { return bandFor(m_window); }
    QString bandLabel() const;

    static Band bandFor(int width);

signals:
    /// Emitted on every real change of the stored width.
    void windowChanged();

    /// Emitted when the *band* changes, not when the window does. A drag across a hundred
    /// pixels inside one band would otherwise rebind every screen a hundred times.
    void changed();

private:
    int m_window = 0;
};
