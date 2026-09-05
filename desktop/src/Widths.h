#pragma once

// The three widths, and the shelf columns they imply, in the one place that declares them.
//
// Not `width < …` scattered through every delegate: the grid and the reader have to agree on
// where the break falls, and two copies of a number agree only until one of them is edited.
//
// Where the two numbers come from. Below 1100 a two-page spread gives each half under 400 px
// and stops being readable, so the reader falls back to a single page — which is why this
// break belongs to more than the grid. Below 600 the search field can no longer share the bar
// with anything and takes the screen.
//
// That is the reasoning that chose them. The grid now reads its band and column count here;
// the reader will later read the same band rather than grow a second set of thresholds.

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
    /// The shelf changes from four to five columns inside the medium band, so this follows
    /// every window change rather than only a band crossing.
    Q_PROPERTY(int shelfColumns READ shelfColumns NOTIFY windowChanged)
    Q_PROPERTY(int shelfMargin READ shelfMargin CONSTANT)
    Q_PROPERTY(int shelfGap READ shelfGap CONSTANT)
    Q_PROPERTY(int minimumCoverWidth READ minimumCoverWidth CONSTANT)
    /// « Large », « Moyenne », « Étroite » — `Words::band(band())`, on this singleton for the
    /// same reason `Navigation::label` is on that one rather than on a second singleton.
    Q_PROPERTY(QString bandLabel READ bandLabel NOTIFY changed)

public:
    enum class Band { Narrow, Medium, Wide };
    Q_ENUM(Band)

    static constexpr int ShelfMargin = 16;
    static constexpr int ShelfGap = 14;
    static constexpr int MinimumCoverWidth = 140;

    explicit Widths(QObject *parent = nullptr);

    int window() const { return m_window; }
    void setWindow(int width);

    Band band() const { return bandFor(m_window); }
    QString bandLabel() const;
    int shelfColumns() const { return shelfColumnsFor(m_window); }
    int shelfMargin() const { return ShelfMargin; }
    int shelfGap() const { return ShelfGap; }
    int minimumCoverWidth() const { return MinimumCoverWidth; }

    static Band bandFor(int width);
    static int shelfColumnsFor(int width);

signals:
    /// Emitted on every real change of the stored width.
    void windowChanged();

    /// Emitted when the *band* changes, not when the window does. A drag across a hundred
    /// pixels inside one band would otherwise rebind every screen a hundred times.
    void changed();

private:
    int m_window = 0;
};
