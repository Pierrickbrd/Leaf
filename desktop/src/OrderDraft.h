#pragma once

// The draft of a universe's reading orders, as `OrderEditor` holds it while one is written.
//
// **The draft apart from the editor that shows it.** The editor is what QML calls, and what
// knows the card the draft came from and goes back to; this is what each change makes of the
// orders and what it refuses. As one class they were forty-six methods, and the draft needs
// neither a card nor a signal to be right: it is handed whom to tell when it changes, and the
// editor tells QML.
//
// **Refused here what the server refuses.** An inclusion that would loop, a range that runs
// backwards, a range of volumes that cannot say which edition it counts, a range of chapters
// that names one, orders past what a universe is read to: each is said on its step before it
// is sent, since sent, the announcement is a 400 for the whole card, or the scan leaves the
// step out without a word. Worked out on one `Orders::Plan` per change, as the scan works it.
//
// **What was read leaves as it came.** Every step keeps the object it was read from, and an
// order nobody touched is written back byte for byte (`Orders::written`): a note, a colour, a
// key a later server reads survive the editor. A step naming a work this drop does not hold
// is kept and said absent — the work may well be on the server's disk already.

#include "Facts.h"
#include "Orders.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <optional>

class OrderDraft final
{
public:
    /// What drawing the draft cost, for a test to assert on: the drafts worked out, and the
    /// steps looked at to work them out. Six properties read the same draft, and each working
    /// it out again read every order of the universe down to its last inclusion.
    struct Cost {
        qint64 drafts = 0;
        qint64 looks = 0;
    };
    static Cost cost();

    /// Everything the editor's properties read, worked out once per change: the draft, its
    /// plan and each order flattened first, which every change needs to know whether the
    /// server would cut it; the lists drawn after, and only once something reads them — a
    /// change undone for the ceiling drew 585 steps for nothing.
    struct State {
        Orders::Universe draft;
        /// On `draft`, which it refers to: the state is built in place and never moved.
        std::optional<Orders::Plan> plan;
        QList<std::optional<QList<Orders::Step>>> flats;
        /// A step of an order to be written is in doubt, or an inclusion would be refused.
        bool doubtful = false;
        bool past = false;
        /// By order: whether the chosen order including it would loop — it, or one reading it.
        QList<bool> loops;
        /// By order: what an inclusion of it writes, or nothing when nothing reaches it.
        QStringList designations;
        bool drawn = false;
        QVariantList tabs;
        QVariantList steps;
        QVariantList targets;
        QVariantMap step;
        QString footer;
    };

    /// The field `key` of `sheet`, nothing when it is missing.
    static const Facts::Field *said(const Facts::Sheet &sheet, const QString &key);

    /// `changed` is called at every change the draft makes of itself, refusals included.
    explicit OrderDraft(std::function<void()> changed);

    /// Reads the orders of `universe`, a sheet among `sheets`, chosen on `orderId` — its first
    /// order when no order is called that.
    void read(const QList<Facts::Sheet> &sheets, const Facts::Sheet &universe,
              const QString &orderId);
    /// Lets go of everything read.
    void clear();

    /// The state, with its lists drawn.
    const State &drawn() const;
    QString saveLabel() const;
    bool dirty() const;
    /// No step in doubt, and nothing past what the server reads that was not already.
    bool writable() const;
    QString universeName() const { return m_universeName; }
    QStringList works() const { return m_works; }
    /// The draft as `Orders` reads it.
    Orders::Universe universe() const;

    // The changes `OrderEditor` makes, as it documents them; it calls them only while open.
    void choose(const QString &orderId);
    void select(int at);
    void addWork(const QString &work);
    bool addInclude(const QString &orderId);
    bool setInclude(int at, const QString &orderId);
    bool setRange(int at, const QString &unit, const QString &edition, const QString &from,
                  const QString &to);
    void move(int at, int to);
    void remove(int at);
    void acceptProposal(int at);
    QString newOrder(const QString &name);

private:
    /// A step of the draft, and what an inclusion offered in its place stands for.
    struct Draft {
        Orders::Step step;
        bool proposed = false;
        QList<Orders::Step> replaced;
    };
    struct Order {
        QList<Draft> steps;
        /// Whether its copies were offered as inclusions: once, when it is first opened, so
        /// that a proposal refused does not come back on the next tab.
        bool proposed = false;
    };

    const State &state() const;
    /// Why step `at` of `order` would not be read as written, or nothing.
    QString doubtOf(const State &state, int order, int at) const;
    int indexOf(const QString &orderId) const;
    /// The step selected when an order is opened: its first proposal, or its first step.
    int firstToLookAt(int order) const;
    /// Offers each copy the order makes of another as an inclusion in its place.
    void propose(int order);
    /// Applies `apply` to the chosen order; undone, with the reason said, when it takes the
    /// orders past what the server reads of a universe and the file was not already.
    template <typename Apply>
    bool change(const Apply &apply);
    void refuse(int at, const QString &why);
    void touched();

    std::function<void()> m_changed;
    QString m_universeName;
    /// What was read, the orders' unknown keys and the universe's default among it.
    Orders::Universe m_read;
    QList<Order> m_orders;
    /// By order: whether it is written anew — changed, proposed an inclusion, or created here.
    /// An order that is not goes back to the file as it was read.
    QList<bool> m_edited;
    bool m_pastWhenRead = false;
    int m_chosen = -1;
    int m_selected = -1;
    /// A change refused, said on its step until the next change.
    int m_refusedAt = -1;
    QString m_refusal;
    /// The card's works by folder name, and how many editions each reads: the scan matches a
    /// step's work by its folder under the universe, and an edition by its folder under it.
    QStringList m_works;
    QHash<QString, QStringList> m_editions;
    QHash<QString, int> m_editionCount;
    mutable std::optional<State> m_state;
};
