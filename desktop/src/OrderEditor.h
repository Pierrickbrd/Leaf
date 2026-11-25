#pragma once

// The draft of a universe's reading orders, while one of them is written.
//
// **A draft, and only while it is open.** It is read from the card's sheet at every `openOn`
// and let go at `close` or `save`: kept beyond, it would be a second truth beside the card,
// and the first list of this client that kept one drew a card as it was two answers ago
// (`Review.h`). Nothing reaches the card until `save`, which hands the whole `orders` array to
// `Imports::edit` — the one road a correction takes, refused once the card is on its way.
//
// **Refused here what the server refuses.** An inclusion that would loop, a range that runs
// backwards, a range of volumes that cannot say which edition it counts, a range of chapters
// that names one, orders past what a universe is read to: each is said on its step before it
// is sent. What each change makes of the orders, and what it refuses, is `OrderDraft`'s: this
// is what QML calls, and what knows the card the draft came from and goes back to.

#include "OrderDraft.h"

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class Imports;
class QJSEngine;

class OrderEditor final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool open READ open NOTIFY changed)
    /// `[{id, name, works, chosen}]`, one per order of the universe, in the file's order.
    Q_PROPERTY(QVariantList tabs READ tabs NOTIFY changed)
    /// The steps of the order chosen: `[{at, kind, label, unit, unitLabel, edition, from, to,
    /// proposed, absent, inherited, inheritedNote, selected, doubt}]`. `kind` is "work" or
    /// "include"; `inherited` the steps an inclusion reads, `[{label, unit, from, to}]`.
    Q_PROPERTY(QVariantList steps READ steps NOTIFY changed)
    /// The step selected: `{kind, target, targets, inheritedCount, flatCount, inheritedNote,
    /// units, editions, works}`; `targets` are `[{id, name, possible, reason}]`, every order it
    /// may include, greyed with the reason where including it would loop. Empty when none is.
    Q_PROPERTY(QVariantMap step READ step NOTIFY changed)
    /// « 5 étapes écrites · 7 une fois aplati ».
    Q_PROPERTY(QString footer READ footer NOTIFY changed)
    /// « Enregistrer l’ordre » / « Enregistrer les ordres ».
    Q_PROPERTY(QString saveLabel READ saveLabel NOTIFY changed)
    /// Whether saving would write anything: a proposal still there counts, saving accepts it.
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    /// Whether `save` would take the draft: no step in doubt, nothing past what the server
    /// reads, a card that can still be corrected. The button is drawn from it.
    Q_PROPERTY(bool savable READ savable NOTIFY changed)
    /// The universe's name, over its orders.
    Q_PROPERTY(QString universeName READ universeName NOTIFY changed)
    /// The works of the drop, by folder, in the order of the tree: what a step may read.
    Q_PROPERTY(QStringList works READ works NOTIFY changed)
    /// Every order the chosen one may include, `[{id, name, possible, reason}]`, as `step`'s
    /// `targets` — and there whatever step is selected, for the way to add an inclusion.
    Q_PROPERTY(QVariantList targets READ targets NOTIFY changed)

public:
    explicit OrderEditor(Imports *imports, QObject *parent = nullptr);

    static OrderEditor *create(QQmlEngine *engine, QJSEngine *);

    bool open() const { return m_token != 0; }
    QVariantList tabs() const;
    QVariantList steps() const;
    QVariantMap step() const;
    QString footer() const;
    QString saveLabel() const;
    bool dirty() const;
    bool savable() const;
    QString universeName() const { return m_draft.universeName(); }
    QStringList works() const { return m_draft.works(); }
    QVariantList targets() const;

    /// Opens the orders of the universe sheet `universeId` (`Review`'s `"<token>/<at>"`) on
    /// `orderId`, its first order when no order is called that. False, and nothing opened,
    /// when the sheet is no universe or its card can no longer be corrected.
    Q_INVOKABLE bool openOn(const QString &universeId, const QString &orderId);
    /// Another tab: the draft of the one left is kept until `close` or `save`.
    Q_INVOKABLE void choose(const QString &orderId);
    Q_INVOKABLE void select(int at);
    Q_INVOKABLE void addWork(const QString &work);
    /// False, and nothing changed, when including `orderId` would loop or name nothing.
    Q_INVOKABLE bool addInclude(const QString &orderId);
    Q_INVOKABLE bool setInclude(int at, const QString &orderId);
    /// `unit` is "", "VOLUME" or "CHAPTER"; `from` and `to` as typed, empty for none. False,
    /// and the step as it was with the reason on it, for a range the server would not read.
    Q_INVOKABLE bool setRange(int at, const QString &unit, const QString &edition,
                              const QString &from, const QString &to);
    Q_INVOKABLE void move(int at, int to);
    /// A proposal removed gives back the steps it stood for: refusing it is keeping them.
    Q_INVOKABLE void remove(int at);
    Q_INVOKABLE void acceptProposal(int at);
    /// An order of that name, with an id of its own, chosen; its id.
    Q_INVOKABLE QString newOrder(const QString &name);
    /// Writes the orders into the card, and closes. False, and still open, while a step is
    /// in doubt or the card can no longer be corrected.
    Q_INVOKABLE bool save();
    /// « Retour »: the draft is let go, and the card is as it was.
    Q_INVOKABLE void close();

signals:
    void changed();

private:
    Imports *m_imports;
    quint64 m_token = 0;
    QString m_at;
    OrderDraft m_draft;
};
