#pragma once

// Where to go from a series that is not this series.
//
// Two blocks under one tab, never mixed: changing edition is the same book in another
// binding, walking the universe is other books. The editions belong to `Series`, which
// already holds them for its switcher; this holds the universe.
//
// **A universe may declare ways through it.** When it does, the block *becomes* that way:
// a tile per **step** and not per series, because a step is a stretch of one work and the
// same work may appear twice — « work A part 1, work B, work A part 2 » is the reason the
// model has steps at all. Sorting series would lose exactly that.
//
// Declaring none is the ordinary case, and then it is the flat list it always was.

#include "Api.h"
#include "Server.h"

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QVariantList>

#include <optional>

class Elsewhere : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(QString universe READ universe NOTIFY changed)
    /// The series this block is pointed at, so the shell can tell whether it is already the
    /// one asked for — without it, every change of the page would re-point the block,
    /// including the one the block's own answer caused.
    Q_PROPERTY(QString pointedAt READ pointedAt NOTIFY changed)
    /// The ways through this universe, when it declares any. Empty is the ordinary case.
    Q_PROPERTY(QVariantList orders READ orders NOTIFY changed)
    Q_PROPERTY(QString chosenOrder READ chosenOrder NOTIFY changed)
    /// The name of that way, which is what the chooser shows. Here rather than searched for
    /// in `orders` by a `.qml`, which would be a list walked in JavaScript on every redraw.
    Q_PROPERTY(QString chosenName READ chosenName NOTIFY changed)
    /// What the block draws: a tile per step when a way is chosen, a tile per series
    /// otherwise. Same shape either way, so the block does not change component.
    Q_PROPERTY(QVariantList tiles READ tiles NOTIFY changed)
    /// What a chosen way does not name. It comes after, under its own heading, rather than
    /// being hidden or slipped onto the end as though it were part of the walk.
    Q_PROPERTY(QVariantList outside READ outside NOTIFY changed)

public:
    explicit Elsewhere(Server *server, QObject *parent = nullptr);

    static Elsewhere *create(QQmlEngine *engine, QJSEngine *);

    bool loading() const { return m_pending > 0; }
    QString universe() const { return m_universe; }
    QString pointedAt() const { return m_seriesId; }
    QVariantList orders() const;
    QString chosenOrder() const { return m_chosen; }
    QString chosenName() const;
    QVariantList tiles() const;
    QVariantList outside() const;

    /// Points the block at the universe a series belongs to, by name because that is what
    /// `/series?universe=` takes, and by identifier because that is what the ways take.
    Q_INVOKABLE void point(const QString &universeId, const QString &universeName,
                           const QString &seriesId);
    /// Chooses a way through, or none. An unknown identifier is none, which is the flat list.
    Q_INVOKABLE void chooseOrder(const QString &identifier);
    /// Where the reader stands in the series being read, so that « ici » lands on the step
    /// that holds it rather than on every step of that work. NaN when nothing is open, and
    /// then the mark falls on the first — a walk still has to say where it was entered.
    Q_INVOKABLE void readAt(double number);

signals:
    void changed();

private:
    void tookSiblings(const Server::Answer &answer);
    /// Reads the one count the block came for, and asks for the ways only when it is not
    /// nought. Nothing else in the client reads `/universes`, so the guard the contract
    /// publishes `orderCount` for is held here or nowhere.
    void tookUniverses(const Server::Answer &answer);
    void tookOrders(const Server::Answer &answer);
    const Api::ReadingOrder *chosen() const;
    /// The series a step draws from, and nothing when the universe answer does not hold it.
    const Api::Series *seriesOf(const Api::ReadingStep &step) const;
    /// Which step carries « ici », or -1.
    qsizetype hereStep(const Api::ReadingOrder &way) const;
    QVariantMap tileOf(const Api::Series *one, const QString &name, const QString &detail,
                       bool here) const;

    Server *m_server;
    QString m_universeId;
    QString m_universe;
    QString m_seriesId;
    QList<Api::Series> m_siblings;
    QList<Api::ReadingOrder> m_orders;
    QString m_chosen;
    /// Absent rather than NaN: « no volume open » is a fact, and a fact that has to survive
    /// a comparison. Two NaNs are never equal, so a NaN held here would redraw the block on
    /// every answer the list of volumes gives.
    std::optional<double> m_at;
    /// How many answers are still to come. A count and not a flag, because the ways are asked
    /// for after the universes have answered: a block that said it had finished between the
    /// two would be drawn flat and then redrawn as a walk under the reader's eyes.
    int m_pending = 0;
    int m_generation = 0;
};
