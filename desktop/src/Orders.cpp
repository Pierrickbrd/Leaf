#include "Orders.h"

#include "Fold.h"
#include "Shape.h"

#include <QHash>
#include <QMap>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

using Orders::Order;
using Orders::Step;
using Orders::Universe;

Orders::Cost &spent()
{
    static Orders::Cost total;
    return total;
}

QString textOf(const QJsonObject &object, const QString &key)
{
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString() : QString();
}

std::optional<double> numberOf(const QJsonObject &object, const QString &key)
{
    const QJsonValue value = object.value(key);
    if (!value.isDouble())
        return std::nullopt;
    return value.toDouble();
}

/// Whether the step writes `key`, as serde reads an `Option`: a key set to `null` is a key
/// not written. `{"work":"X","include":null}` is an ordinary step on the server, and was
/// refused here as an inclusion that carries a work.
bool says(const Step &step, const QString &key)
{
    const QJsonValue value = step.raw.value(key);
    return !value.isUndefined() && !value.isNull();
}

bool carriesMore(const Step &step)
{
    for (const QString &key : {u"work"_s, u"unit"_s, u"edition"_s, u"from"_s, u"to"_s})
        if (says(step, key))
            return true;
    return !step.work.isEmpty() || !step.unit.isEmpty() || !step.edition.isEmpty() ||
           step.from.has_value() || step.to.has_value();
}

bool same(const Step &a, const Step &b)
{
    return a.work == b.work && a.unit == b.unit && a.edition == b.edition &&
           a.include == b.include && a.from == b.from && a.to == b.to;
}

/// Tarjan's bookkeeping, shared by the walks from every root.
struct Tarjan {
    static constexpr int unseen = -1;

    explicit Tarjan(const QList<QList<int>> &edges)
        : adjacent(edges), index(edges.size(), unseen), low(edges.size(), 0),
          component(edges.size(), unseen), onStack(edges.size(), false)
    {
    }

    const QList<QList<int>> &adjacent;
    QList<int> index;
    QList<int> low;
    QList<int> component;
    QList<bool> onStack;
    QList<int> open;
    int nextIndex = 0;
    int nextComponent = 0;

    void walkFrom(int root);
    void close(int node);
};

void Tarjan::walkFrom(int root)
{
    QList<std::pair<int, int>> work{{root, 0}};
    while (!work.isEmpty()) {
        const auto [node, child] = work.last();
        if (child == 0 && index[node] == unseen) {
            index[node] = nextIndex;
            low[node] = nextIndex;
            ++nextIndex;
            open << node;
            onStack[node] = true;
        }
        if (child < adjacent.at(node).size()) {
            const int to = adjacent.at(node).at(child);
            work.last().second++;
            if (index[to] == unseen)
                work.append({to, 0});
            else if (onStack[to])
                low[node] = std::min(low[node], index[to]);
            continue;
        }
        work.removeLast();
        if (!work.isEmpty())
            low[work.last().first] = std::min(low[work.last().first], low[node]);
        if (low[node] == index[node])
            close(node);
    }
}

void Tarjan::close(int node)
{
    while (!open.isEmpty()) {
        const int member = open.takeLast();
        onStack[member] = false;
        component[member] = nextComponent;
        if (member == node)
            break;
    }
    ++nextComponent;
}

/// Whether `mine` writes every step of `theirs` again from `from`, key by key.
bool writesAgain(const QList<Step> &mine, qsizetype from, const QList<Step> &theirs)
{
    for (qsizetype k = 0; k < theirs.size(); ++k) {
        ++spent().looks;
        if (!same(mine.at(from + k), theirs.at(k)))
            return false;
    }
    return true;
}

/// Adds to `found` each of `candidates` that order `at` writes again from `from`. One already
/// found keeps where it was found first.
void copiedFrom(const Universe &universe, int at, qsizetype from, const QList<int> &candidates,
                QMap<int, int> &found)
{
    const QList<Step> &mine = universe.orders.at(at).steps;
    for (const int of : candidates) {
        const QList<Step> &theirs = universe.orders.at(of).steps;
        if (of == at || found.contains(of) || from + theirs.size() > mine.size())
            continue;
        if (writesAgain(mine, from, theirs))
            found.insert(of, int(from));
    }
}

/// Strongly connected components, numbered: two orders share one exactly when each can read
/// the other. Tarjan's, kept off the call stack — a chain of ten thousand orders each
/// including the next is a file of a few hundred kilobytes.
QList<int> components(const QList<QList<int>> &adjacent)
{
    const auto n = int(adjacent.size());
    Tarjan tarjan(adjacent);
    for (int root = 0; root < n; ++root) {
        if (tarjan.index[root] == Tarjan::unseen)
            tarjan.walkFrom(root);
    }
    return tarjan.component;
}

Step stepOf(const QJsonObject &raw)
{
    Step step;
    step.raw = raw;
    step.work = textOf(raw, u"work"_s);
    step.unit = textOf(raw, u"unit"_s).trimmed().toUpper();
    step.edition = textOf(raw, u"edition"_s);
    step.include = textOf(raw, u"include"_s);
    step.from = numberOf(raw, u"from"_s);
    step.to = numberOf(raw, u"to"_s);
    return step;
}

/// Puts `text` under `key` unless the object already says it — in the case it was written in,
/// when `capitals` — and removes the key when there is nothing to say.
void setText(QJsonObject &object, const QString &key, const QString &text, bool capitals = false)
{
    if (text.isEmpty()) {
        object.remove(key);
        return;
    }
    if (const QJsonValue before = object.value(key);
        before.isString() &&
        (capitals ? before.toString().trimmed().toUpper() == text : before.toString() == text))
        return;
    object.insert(key, text);
}

void setNumber(QJsonObject &object, const QString &key, const std::optional<double> &number)
{
    if (!number.has_value()) {
        object.remove(key);
        return;
    }
    const QJsonValue before = object.value(key);
    if (!before.isDouble() || before.toDouble() != *number)
        object.insert(key, *number);
}

} // namespace

namespace Orders {

bool includes(const Step &step)
{
    return says(step, u"include"_s) || !step.include.isEmpty();
}

Cost cost()
{
    return spent();
}

Plan::Plan(const Universe &universe) : m_universe(universe)
{
    const qsizetype count = universe.orders.size();
    m_disregarded.resize(count);
    for (qsizetype at = 0; at < count; ++at) {
        const Order &order = universe.orders.at(at);
        const QString id = declaredId(order, int(at));
        // Dropped by the scan: an include must not reach it by its name either.
        if (m_byId.contains(id)) {
            m_disregarded[at] = true;
            continue;
        }
        m_byId.insert(id, int(at));
        if (const QString key = Fold::searchKey(order.name);
            !key.isEmpty() && !m_byName.contains(key))
            m_byName.insert(key, int(at));
    }
    m_targets.resize(count);
    for (qsizetype at = 0; at < count; ++at) {
        for (const Step &step : universe.orders.at(at).steps) {
            ++spent().looks;
            m_targets[at] << (includes(step) ? designated(step.include) : -1);
        }
    }
    refuse();
}

int Plan::designated(const QString &include) const
{
    const QString wanted = include.trimmed();
    if (wanted.isEmpty())
        return -1;
    if (const auto found = m_byId.constFind(wanted); found != m_byId.cend())
        return found.value();
    const QString key = Fold::searchKey(wanted);
    return key.isEmpty() ? -1 : m_byName.value(key, -1);
}

int Plan::target(int order, int step) const
{
    return includes(m_universe.orders.at(order).steps.at(step)) ? m_targets.at(order).at(step)
                                                                : -1;
}

std::optional<QList<Step>> Plan::flatten(int at, Budget *budget) const
{
    QList<Step> out;
    qsizetype lookedAt = 0;
    struct Frame {
        int order;
        qsizetype next;
    };
    QList<Frame> stack{{at, 0}};
    while (!stack.isEmpty()) {
        Frame &frame = stack.last();
        const QList<Step> &steps = m_universe.orders.at(frame.order).steps;
        if (frame.next >= steps.size()) {
            stack.removeLast();
            continue;
        }
        const int order = frame.order;
        const qsizetype stepAt = frame.next++;
        ++spent().looks;
        if (budget) {
            if (budget->lookedLeft <= 0) {
                budget->lookedLeft = 0;
                return std::nullopt;
            }
            --budget->lookedLeft;
        }
        if (++lookedAt > qsizetype(MostFlatSteps) * 16)
            return std::nullopt;
        if (m_refused.contains({order, int(stepAt)}))
            continue;
        const int target = m_targets.at(order).at(stepAt);
        if (target >= 0 && includes(steps.at(stepAt))) {
            stack.append({target, 0});
        } else {
            if (out.size() >= MostFlatSteps)
                return std::nullopt;
            out << steps.at(stepAt);
        }
    }
    // As the scan's `checked_sub`: an order that does not fit takes nothing from what is
    // left, and a smaller one after it may still.
    if (budget) {
        if (out.size() > budget->flatLeft)
            return std::nullopt;
        budget->flatLeft -= out.size();
    }
    return out;
}

void Plan::refuse()
{
    struct Edge {
        int from;
        int step;
        int to;
    };
    QList<Edge> edges;
    const qsizetype count = m_universe.orders.size();
    for (qsizetype at = 0; at < count; ++at) {
        const QList<Step> &steps = m_universe.orders.at(at).steps;
        for (qsizetype stepAt = 0; stepAt < steps.size(); ++stepAt) {
            const Step &step = steps.at(stepAt);
            if (!includes(step))
                continue;
            const std::pair<int, int> key{int(at), int(stepAt)};
            if (carriesMore(step))
                m_refused.insert(key, {key.first, key.second, Orders::Refusal::CarriesMore, {}});
            else if (const int target = m_targets.at(at).at(stepAt); target >= 0)
                edges << Edge{int(at), int(stepAt), target};
            else
                m_refused.insert(key, {key.first, key.second, Orders::Refusal::NamesNothing,
                                       step.include.trimmed()});
        }
    }
    m_adjacent.resize(count);
    for (const Edge &edge : edges)
        m_adjacent[edge.from] << edge.to;
    const QList<int> component = components(m_adjacent);
    for (const Edge &edge : edges) {
        if (component.at(edge.from) == component.at(edge.to))
            m_refused.insert({edge.from, edge.step},
                             {edge.from, edge.step, Orders::Refusal::ClosesLoop,
                              Orders::declaredId(m_universe.orders.at(edge.to), edge.to)});
    }
}

Universe read(const QJsonObject &universeJson)
{
    Universe universe;
    universe.defaultOrder = textOf(universeJson, u"defaultOrder"_s);
    for (const QJsonValue &one : universeJson.value(u"orders"_s).toArray()) {
        const QJsonObject raw = one.toObject();
        Order order;
        order.raw = raw;
        order.id = textOf(raw, u"id"_s);
        order.name = textOf(raw, u"name"_s);
        for (const QJsonValue &step : raw.value(u"steps"_s).toArray())
            order.steps << stepOf(step.toObject());
        universe.orders << order;
    }
    return universe;
}

QString declaredId(const Order &order, int at)
{
    if (const QString id = order.id.trimmed(); !id.isEmpty())
        return id;
    if (const QString key = Fold::searchKey(order.name); !key.isEmpty())
        return key;
    return QString::number(at + 1);
}

int resolve(const Universe &universe, const QString &include)
{
    return Plan(universe).designated(include);
}

QSet<std::pair<int, int>> loops(const Universe &universe)
{
    QSet<std::pair<int, int>> out;
    // Named, not a temporary in the loop's range: `refused()` hands back a reference into the
    // plan, which a range-for does not keep alive before C++23. Qt 6.4 with GCC 13 read freed
    // nodes and crashed `reads-an-order`; Qt 6.10 happened to leave them intact.
    const Plan plan(universe);
    for (const Refused &one : plan.refused())
        if (one.why == Refusal::ClosesLoop)
            out.insert({one.order, one.step});
    return out;
}

bool wouldLoop(const Universe &universe, int order, int target)
{
    if (order == target)
        return true;
    // Only the inclusions that can be honoured read anything: a refused one is skipped, so it
    // joins nothing. `target` reaching `order` is the loop that including it would close.
    const Plan plan(universe);
    QSet seen{target};
    QList pending{target};
    while (!pending.isEmpty()) {
        const int at = pending.takeLast();
        for (const int next : plan.adjacent().at(at)) {
            if (next == order)
                return true;
            if (!seen.contains(next)) {
                seen.insert(next);
                pending << next;
            }
        }
    }
    return false;
}

QList<Refused> refused(const Universe &universe)
{
    return Plan(universe).refused().values();
}

std::optional<QList<Step>> flatten(const Universe &universe, int order)
{
    return Plan(universe).flatten(order);
}

bool pastCeilings(const Universe &universe)
{
    const Plan plan(universe);
    qsizetype total = 0;
    for (int at = 0; at < universe.orders.size(); ++at) {
        // Skipped by the scan before anything else is asked of it, its rank included.
        if (plan.disregarded(at))
            continue;
        if (at >= MostOrders)
            return true;
        const std::optional<QList<Step>> flat = plan.flatten(at);
        if (!flat)
            return true;
        total += flat->size();
        if (total > MostUniverseSteps)
            return true;
    }
    return false;
}

QList<Copy> copies(const Universe &universe)
{
    // Found by their first step and not by comparing every pair: a thousand orders were a
    // million pairs, each slid along the other, on every change of the queue.
    const auto count = int(std::min<qsizetype>(universe.orders.size(), MostOrders));
    const auto keyOf = [](const Step &step) {
        return QStringList{step.work, step.unit, step.edition, step.include,
                           step.from.has_value() ? QString::number(*step.from, 'g', 17)
                                                 : QString(),
                           step.to.has_value() ? QString::number(*step.to, 'g', 17) : QString()}
            .join(u'\n');
    };
    QHash<QString, QList<int>> startingWith;
    for (int of = 0; of < count; ++of) {
        if (const QList<Step> &theirs = universe.orders.at(of).steps; !theirs.isEmpty())
            startingWith[keyOf(theirs.constFirst())] << of;
    }
    QList<Copy> out;
    for (int at = 0; at < count; ++at) {
        const QList<Step> &mine = universe.orders.at(at).steps;
        QMap<int, int> found;
        for (qsizetype from = 0; from < mine.size(); ++from) {
            ++spent().looks;
            const auto candidates = startingWith.constFind(keyOf(mine.at(from)));
            if (candidates == startingWith.cend())
                continue;
            copiedFrom(universe, at, from, *candidates, found);
        }
        // By the order copied, as the pairs were walked: the note names the first.
        for (auto one = found.cbegin(); one != found.cend(); ++one)
            out << Copy{at, one.value(), int(universe.orders.at(one.key()).steps.size()),
                        one.key()};
    }
    return out;
}

int worksIn(const QList<Step> &steps)
{
    QSet<QString> distinct;
    for (const Step &step : steps)
        if (!step.work.isEmpty())
            distinct.insert(step.work);
    return int(distinct.size());
}

int works(const Universe &universe, int order)
{
    const std::optional<QList<Step>> flat = flatten(universe, order);
    return worksIn(flat.value_or(universe.orders.at(order).steps));
}

QString newId(const Universe &universe, const QString &name)
{
    QStringList words;
    QString word;
    for (const QChar ch : Shape::folded(name)) {
        if (ch.isLetterOrNumber()) {
            word.append(ch);
        } else if (!word.isEmpty()) {
            words << word;
            word.clear();
        }
    }
    if (!word.isEmpty())
        words << word;
    const QString base = words.isEmpty() ? u"ordre"_s : words.join(u'-');
    QSet<QString> taken;
    for (int at = 0; at < universe.orders.size(); ++at)
        taken.insert(declaredId(universe.orders.at(at), at));
    QString candidate = base;
    int suffix = 2;
    while (taken.contains(candidate)) {
        candidate = base + u'-' + QString::number(suffix);
        ++suffix;
    }
    return candidate;
}

QJsonArray written(const Universe &universe)
{
    QJsonArray out;
    for (const Order &order : universe.orders) {
        if (!order.edited) {
            out << order.raw;
            continue;
        }
        QJsonObject object = order.raw;
        setText(object, u"id"_s, order.id);
        setText(object, u"name"_s, order.name);
        QJsonArray steps;
        for (const Step &step : order.steps) {
            QJsonObject one = step.raw;
            setText(one, u"work"_s, step.work);
            setText(one, u"unit"_s, step.unit, true);
            setText(one, u"edition"_s, step.edition);
            setText(one, u"include"_s, step.include);
            setNumber(one, u"from"_s, step.from);
            setNumber(one, u"to"_s, step.to);
            steps << one;
        }
        if (steps.isEmpty())
            object.remove(u"steps"_s);
        else
            object.insert(u"steps"_s, steps);
        out << object;
    }
    return out;
}

} // namespace Orders
