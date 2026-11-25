#pragma once

// The reading orders of a universe.json, read the way the server reads them.
//
// An order is a list of steps — a stretch of one work — and a step may instead name another
// order of the same file, to be read in its place. The rules are `server/src/scan/orders.rs`:
// the scan flattens with them and the announcement of an import refuses with them, so a client
// that resolved a name otherwise would draw one order and have the server keep another. Pure on
// purpose: JSON in, JSON out, no window and no network.
//
// **What is read is kept.** Every order and every step holds the object it was read from, and
// `written` gives an order nobody edited back as it came: the file is its author's, and
// recomposing an order from the keys this client understands loses whatever no version of Leaf
// knows yet — a note, a colour, a key a later server reads.

#include <QJsonArray>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QSet>
#include <QString>

#include <optional>
#include <utility>

namespace Orders {

/// What one order may hold once its inclusions are read in place. An order that includes the
/// next one twice, fourteen times over, is 8 192 steps from a file of a few hundred bytes.
constexpr int MostFlatSteps = 4096;
/// The orders of one universe that get steps; the ones past it are kept without any.
constexpr int MostOrders = 1024;
/// What all the orders of a universe add up to once flattened.
constexpr int MostUniverseSteps = MostFlatSteps * 4;
/// The steps looked at across a universe's orders, kept or not: a chain of ten thousand orders,
/// each reading the next, is fifty million visits when each order walks its own way down.
constexpr int MostUniverseLooks = MostFlatSteps * 16 * 4;

/// The steps looked at, by any order of any universe: a cost is a count, a test can assert it.
struct Cost {
    qint64 looks = 0;
};
Cost cost();

struct Step {
    /// The step as read: its unknown keys leave with it.
    QJsonObject raw;
    QString work;
    QString unit;
    QString edition;
    QString include;
    /// `unit` is "", "VOLUME" or "CHAPTER": the case of the file, folded as the server folds it.
    std::optional<double> from;
    std::optional<double> to;
};

struct Order {
    QJsonObject raw;
    QString id;
    QString name;
    QList<Step> steps;
    /// Whether anyone changed it. An order that was not is written back as it was read.
    bool edited = false;
};

struct Universe {
    QList<Order> orders;
    QString defaultOrder;
};

Universe read(const QJsonObject &universeJson);

/// What an order is called when something refers to it: its `id` if it wrote one, else its name
/// folded, else its rank. `at` counts from 0, the rank from 1.
QString declaredId(const Order &order, int at);

/// The order an inclusion designates: by `id` first, by name when no order has that `id`. -1 when
/// it designates nothing. An order whose `id` an earlier one already took is not designated —
/// the scan drops it, and going round by its name must not reach what the scan drops.
int resolve(const Universe &universe, const QString &include);

/// Every inclusion that closes a loop, as (order, step). Each inclusion of a loop is in it, not
/// only the last one met: which one closes it depends on where one enters, and refusing one
/// would show two answers to the same question.
QSet<std::pair<int, int>> loops(const Universe &universe);

/// Whether including `target` in `order` would close a loop — `target` reads `order` already, or
/// is it.
bool wouldLoop(const Universe &universe, int order, int target);

/// Why an inclusion cannot be honoured, as the announcement refuses it.
enum class Refusal { NamesNothing, CarriesMore, ClosesLoop };

struct Refused {
    int order = 0;
    int step = 0;
    Refusal why = Refusal::NamesNothing;
    /// What was named (`NamesNothing`), or the declared id of the order it reads (`ClosesLoop`).
    QString target;
};

/// Each inclusion the announcement would refuse, in order then step — the first is the one it
/// names. Sent anyway, the whole announcement is a 400 before the first byte.
QList<Refused> refused(const Universe &universe);

/// The steps of `order` with each inclusion replaced by the steps it designates and each refused
/// one left out. Nothing past `MostFlatSteps` of them, nor past sixteen times as many looked
/// at: those orders are cut by the server and the client counts no further.
std::optional<QList<Step>> flatten(const Universe &universe, int order);

/// Whether the server would keep less than the file says: more orders than it reads, an order
/// past its steps, or a universe past what its orders add up to. Not a refusal — the scan keeps
/// the rest and says so — but a reason to look.
bool pastCeilings(const Universe &universe);

/// `order` writes again, from `from` for `count` steps, every step of the order `of`, in one
/// piece and equal key by key.
struct Copy {
    int order = 0;
    int from = 0;
    int count = 0;
    int of = 0;
};
/// Among the first `MostOrders` orders only, those the server gives steps to.
QList<Copy> copies(const Universe &universe);

/// The distinct works an order reads, inclusions read in place. Counted on the steps written
/// when the order is past its ceiling.
int works(const Universe &universe, int order);

/// What is left of a universe's allowance while its orders are flattened one after the other,
/// as the scan shares it: past it, an order is kept without steps, and so is every later one
/// that no longer fits.
struct Budget {
    qsizetype flatLeft = MostUniverseSteps;
    qsizetype lookedLeft = MostUniverseLooks;
};

/// What the orders of a universe say about one another, worked out once: who designates whom
/// and which inclusions are refused. `server/src/scan/orders.rs`'s `Plan`. Built once for all
/// that is asked of one universe: built again for every inclusion and every order, a file of
/// ten thousand chained orders was a hundred million comparisons on each change of the queue.
/// `universe` must outlive it.
class Plan
{
public:
    explicit Plan(const Universe &universe);

    /// `resolve`, on this plan.
    int designated(const QString &include) const;
    /// The order step `step` of `order` includes, -1 when it includes none or names nothing.
    int target(int order, int step) const;
    /// Whether `order` bears the id of one written before it: the scan disregards it before it
    /// is flattened, so it gets no steps, costs the universe nothing, and nothing includes it.
    bool disregarded(int order) const { return m_disregarded.value(order, false); }
    const QMap<std::pair<int, int>, Refused> &refused() const { return m_refused; }
    /// Edges of the inclusions that are sound by themselves: from an order to the one it reads.
    const QList<QList<int>> &adjacent() const { return m_adjacent; }
    /// `flatten`, on this plan; with a `budget`, also within what is left of the universe's.
    std::optional<QList<Step>> flatten(int order, Budget *budget = nullptr) const;

private:
    const Universe &m_universe;
    QHash<QString, int> m_byId;
    QHash<QString, int> m_byName;
    QList<bool> m_disregarded;
    QList<QList<int>> m_targets;
    QList<QList<int>> m_adjacent;
    QMap<std::pair<int, int>, Refused> m_refused;

    void refuse();
};

/// A step that names an order. The key written is what counts, as on the server: an empty
/// `include` names nothing, and is refused for it — and is still an inclusion, to be finished
/// by choosing its order, and not a step of a work that names none.
bool includes(const Step &step);

/// The distinct works of steps already flattened.
int worksIn(const QList<Step> &steps);

/// An id for a new order: its name in lowercase letters and hyphens, no other order's.
QString newId(const Universe &universe, const QString &name);

/// The `orders` array to write: an order nobody edited as it was read, an edited one as the keys
/// it was read with, the known ones replaced by what they hold now.
QJsonArray written(const Universe &universe);

} // namespace Orders
