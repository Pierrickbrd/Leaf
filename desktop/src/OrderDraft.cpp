#include "OrderDraft.h"

#include "Words.h"

#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <memory>
#include <ranges>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

using Manifest::Level;

OrderDraft::Cost &spent()
{
    static OrderDraft::Cost total;
    return total;
}

QString parentOf(const QString &path)
{
    const qsizetype slash = path.lastIndexOf(u'/');
    return slash < 0 ? QString() : path.left(slash);
}

/// A folder's own name, which is what the scan matches a step's work and edition by.
QString folderOf(const QString &path)
{
    return path.mid(path.lastIndexOf(u'/') + 1);
}

QString numberOf(const std::optional<double> &number)
{
    return number ? Words::number(*number) : QString();
}

/// A number as typed, with the comma a French reader types: nothing for an empty field, and
/// false for a word.
bool parse(const QString &typed, std::optional<double> &out)
{
    const QString text = typed.trimmed();
    if (text.isEmpty()) {
        out.reset();
        return true;
    }
    bool ok = false;
    const double value = QString(text).replace(u',', u'.').toDouble(&ok);
    if (!ok || !std::isfinite(value))
        return false;
    out = value;
    return true;
}

/// What an inclusion writes to designate order `at`: the first of its id, its name and what
/// the scan calls it that the server resolves to it — or nothing, when none does. Its name
/// alone sent « Les Nains » to the order before it that wrote « cycle » as its id and « Les
/// Nains » as its name; and an order with neither was written as an empty step.
QString designation(const Orders::Plan &plan, const Orders::Order &order, int at)
{
    for (const QString &said : {order.id.trimmed(), order.name, Orders::declaredId(order, at)}) {
        if (!said.trimmed().isEmpty() && plan.designated(said) == at)
            return said;
    }
    return {};
}

QString nameOf(const Orders::Universe &read, int order)
{
    const Orders::Order &one = read.orders.at(order);
    return one.name.isEmpty() ? Orders::declaredId(one, order) : one.name;
}

/// What an inclusion reads, as said to the reader: the order it reaches, else what it wrote.
QString targetNameOf(const Orders::Universe &read, int target, const Orders::Step &step)
{
    return target >= 0 ? nameOf(read, target) : step.include;
}

/// An inclusion naming nothing asks for its order, rather than including « ».
QString labelOf(const Orders::Step &step, bool include, const QString &targetName)
{
    if (!include)
        return step.work;
    return targetName.trimmed().isEmpty() ? Words::chooseIncludedOrder()
                                          : Words::includeStep(targetName);
}

/// The steps an inclusion of order `target` reads, as its line lists them under it.
QVariantList inheritedOf(const QList<std::optional<QList<Orders::Step>>> &flats, int target)
{
    QVariantList list;
    if (target < 0 || !flats.at(target))
        return list;
    for (const Orders::Step &one : *flats.at(target)) {
        list << QVariantMap{{u"label"_s, one.work},
                            {u"unit"_s, Words::orderUnit(one.unit)},
                            {u"from"_s, numberOf(one.from)},
                            {u"to"_s, numberOf(one.to)}};
    }
    return list;
}

QVariantList tabsOf(const Orders::Universe &draft, const Orders::Universe &read,
                    const QList<std::optional<QList<Orders::Step>>> &flats, int chosen)
{
    QVariantList tabs;
    const auto count = int(draft.orders.size());
    for (int at = 0; at < count; ++at) {
        const QList<Orders::Step> &steps =
            flats.at(at) ? *flats.at(at) : draft.orders.at(at).steps;
        tabs << QVariantMap{{u"id"_s, Orders::declaredId(draft.orders.at(at), at)},
                            {u"name"_s, nameOf(read, at)},
                            {u"works"_s, Orders::worksIn(steps)},
                            {u"chosen"_s, at == chosen}};
    }
    return tabs;
}

/// Why the chosen order cannot include order `at`, or nothing when it can.
QString reasonOf(const Orders::Universe &read, int at, int chosen, bool loops, bool unreachable)
{
    if (unreachable)
        return Words::orderDisregarded(nameOf(read, at));
    if (!loops)
        return {};
    // The order itself is refused by the rule, which says it better than a sentence naming it
    // three times.
    if (at == chosen)
        return Words::loopExplained();
    return Words::includeWouldLoop(nameOf(read, chosen), nameOf(read, at));
}

QVariantList targetsOf(const Orders::Universe &draft, const Orders::Universe &read,
                       const QList<bool> &loops, const QStringList &designations, int chosen)
{
    QVariantList targets;
    const auto count = int(draft.orders.size());
    for (int at = 0; at < count; ++at) {
        const bool loop = loops.at(at);
        const bool unreachable = designations.at(at).isEmpty();
        targets << QVariantMap{{u"id"_s, Orders::declaredId(draft.orders.at(at), at)},
                               {u"name"_s, nameOf(read, at)},
                               {u"possible"_s, !loop && !unreachable},
                               {u"reason"_s, reasonOf(read, at, chosen, loop, unreachable)}};
    }
    return targets;
}

/// The orders that read `chosen`, through any number of inclusions, and `chosen` itself:
/// including any of them in it would bring it back to itself. Walked once, back along the
/// inclusions, and not asked of each order in turn.
QList<bool> readingBack(const QList<QList<int>> &adjacent, int count, int chosen)
{
    QList<bool> loops(count, false);
    if (chosen < 0)
        return loops;
    QList<QList<int>> readers(count);
    for (int from = 0; from < adjacent.size(); ++from) {
        for (const int to : adjacent.at(from)) {
            if (to >= 0 && to < count)
                readers[to] << from;
        }
    }
    QList waiting{chosen};
    loops[chosen] = true;
    while (!waiting.isEmpty()) {
        for (const int reader : readers.at(waiting.takeLast())) {
            if (!loops.at(reader)) {
                loops[reader] = true;
                waiting << reader;
            }
        }
    }
    return loops;
}

/// The works of the universe at `at` and the editions each one reads, by folder: an archive
/// lying in the work's own folder is its implicit edition, and one more to choose between.
void readWorks(const QList<Facts::Sheet> &sheets, const QString &at, QStringList &folders,
               QHash<QString, QStringList> &editions, QHash<QString, int> &editionCount)
{
    using enum Manifest::Level;
    QHash<QString, QString> works;
    for (const Facts::Sheet &one : sheets) {
        if (one.level == Work && parentOf(one.at) == at) {
            folders << folderOf(one.at);
            works.insert(one.at, folderOf(one.at));
        }
    }
    QSet<QString> implicit;
    for (const Facts::Sheet &one : sheets) {
        const auto work = works.constFind(parentOf(one.at));
        if (work == works.cend())
            continue;
        if (one.level == Edition) {
            editions[*work] << folderOf(one.at);
            ++editionCount[*work];
        } else if (one.level == Volume || one.level == Chapter) {
            implicit.insert(*work);
        }
    }
    for (const QString &work : std::as_const(implicit))
        ++editionCount[work];
}

} // namespace

OrderDraft::Cost OrderDraft::cost()
{
    return spent();
}

const Facts::Field *OrderDraft::said(const Facts::Sheet &sheet, const QString &key)
{
    const auto found = std::ranges::find(sheet.fields, key, &Facts::Field::key);
    return found == sheet.fields.end() || found->origin == Facts::Origin::Missing
               ? nullptr
               : std::to_address(found);
}

OrderDraft::OrderDraft(std::function<void()> changed)
    : m_changed(std::move(changed))
{
}

void OrderDraft::read(const QList<Facts::Sheet> &sheets, const Facts::Sheet &universe,
                      const QString &orderId)
{
    QJsonObject told;
    if (const Facts::Field *orders = said(universe, u"orders"_s))
        told.insert(u"orders"_s, orders->value);
    if (const Facts::Field *chosen = said(universe, u"defaultOrder"_s))
        told.insert(u"defaultOrder"_s, chosen->value);
    m_read = Orders::read(told);
    const Facts::Field *name = said(universe, u"name"_s);
    m_universeName = name && !name->value.toString().isEmpty() ? name->value.toString()
                                                               : universe.name;
    m_pastWhenRead = Orders::pastCeilings(m_read);
    for (const Orders::Order &order : std::as_const(m_read.orders)) {
        Order draft;
        for (const Orders::Step &step : order.steps)
            draft.steps << Draft{step, false, {}};
        m_orders << draft;
        m_edited << false;
    }
    readWorks(sheets, universe.at, m_works, m_editions, m_editionCount);

    m_chosen = indexOf(orderId);
    if (m_chosen < 0 && !m_orders.isEmpty())
        m_chosen = 0;
    if (m_chosen >= 0) {
        propose(m_chosen);
        m_selected = firstToLookAt(m_chosen);
    }
    touched();
}

void OrderDraft::clear()
{
    m_universeName.clear();
    m_read = {};
    m_orders.clear();
    m_edited.clear();
    m_pastWhenRead = false;
    m_chosen = -1;
    m_selected = -1;
    m_refusedAt = -1;
    m_refusal.clear();
    m_works.clear();
    m_editions.clear();
    m_editionCount.clear();
    touched();
}

Orders::Universe OrderDraft::universe() const
{
    Orders::Universe out = m_read;
    for (int at = 0; at < out.orders.size(); ++at) {
        Orders::Order &order = out.orders[at];
        order.steps.clear();
        for (const Draft &draft : m_orders.at(at).steps)
            order.steps << draft.step;
        order.edited = m_edited.at(at);
    }
    return out;
}

int OrderDraft::indexOf(const QString &orderId) const
{
    for (int at = 0; at < m_read.orders.size(); ++at) {
        if (Orders::declaredId(m_read.orders.at(at), at) == orderId)
            return at;
    }
    return -1;
}

int OrderDraft::firstToLookAt(int order) const
{
    // The proposal first: it is the one step nobody wrote, and the one saving would decide.
    const QList<Draft> &steps = m_orders.at(order).steps;
    if (const auto proposed = std::ranges::find_if(steps, &Draft::proposed);
        proposed != steps.end())
        return int(proposed - steps.begin());
    return steps.isEmpty() ? -1 : 0;
}

void OrderDraft::propose(int order)
{
    if (m_orders.at(order).proposed)
        return;
    m_orders[order].proposed = true;
    const qint64 before = Orders::cost().looks;
    const Orders::Universe draft = universe();
    const Orders::Plan plan(draft);
    QList<Orders::Copy> copies;
    for (const Orders::Copy &copy : Orders::copies(draft)) {
        if (copy.order == order && !Orders::wouldLoop(draft, order, copy.of)
            && !designation(plan, draft.orders.at(copy.of), copy.of).isEmpty())
            copies << copy;
    }
    spent().looks += Orders::cost().looks - before;
    // The longest first, and none over another: two copies overlapping are two ways of
    // writing the same steps, and only one inclusion can stand in their place.
    std::ranges::stable_sort(copies, std::ranges::greater{}, &Orders::Copy::count);
    QList<Orders::Copy> kept;
    for (const Orders::Copy &copy : std::as_const(copies)) {
        if (std::ranges::none_of(kept, [&copy](const Orders::Copy &other) {
                return copy.from < other.from + other.count && other.from < copy.from + copy.count;
            }))
            kept << copy;
    }
    // From the last one back, so that each `from` still says where its steps are.
    std::ranges::sort(kept, std::ranges::greater{}, &Orders::Copy::from);
    QList<Draft> &steps = m_orders[order].steps;
    for (const Orders::Copy &copy : std::as_const(kept)) {
        Draft include;
        include.step.include = designation(plan, draft.orders.at(copy.of), copy.of);
        include.proposed = true;
        for (int k = 0; k < copy.count; ++k)
            include.replaced << steps.at(copy.from + k).step;
        steps.remove(copy.from, copy.count);
        steps.insert(copy.from, include);
    }
    if (!kept.isEmpty())
        m_edited[order] = true;
}

const OrderDraft::State &OrderDraft::state() const
{
    if (m_state)
        return *m_state;
    ++spent().drafts;
    const qint64 before = Orders::cost().looks;
    State &out = m_state.emplace();
    out.draft = universe();
    const Orders::Plan &plan = out.plan.emplace(out.draft);
    const auto count = int(out.draft.orders.size());
    for (int at = 0; at < count; ++at)
        out.designations << designation(plan, out.draft.orders.at(at), at);

    // Flattened as the scan flattens them, in turn and from one allowance: past it, an order
    // is kept without steps, and the editor refuses to be what takes it there. Each inclusion
    // reads its order's flattening from here, and flattens nothing again.
    Orders::Budget budget;
    out.past = count > Orders::MostOrders;
    out.flats.resize(count);
    for (int at = 0; at < std::min(count, Orders::MostOrders); ++at) {
        // Disregarded by the scan for its id before it is flattened: it gets no steps there,
        // and takes none from the others' allowance.
        if (plan.disregarded(at))
            continue;
        out.flats[at] = plan.flatten(at, &budget);
        out.past = out.past || !out.flats.at(at);
    }

    out.loops = readingBack(plan.adjacent(), count, m_chosen);

    // What is written is what is weighed: an order left as it was read goes back as it came,
    // and any inclusion refused refuses the whole announcement.
    out.doubtful = !plan.refused().isEmpty();
    for (int order = 0; order < count && !out.doubtful; ++order) {
        if (!m_edited.at(order))
            continue;
        for (int at = 0; at < m_orders.at(order).steps.size() && !out.doubtful; ++at)
            out.doubtful = !doubtOf(out, order, at).isEmpty();
    }
    spent().looks += Orders::cost().looks - before;
    return out;
}

QString OrderDraft::doubtOf(const State &state, int order, int at) const
{
    const Orders::Plan &plan = *state.plan;
    const Orders::Step &step = m_orders.at(order).steps.at(at).step;
    if (const auto refused = plan.refused().constFind({order, at});
        refused != plan.refused().cend()) {
        if (refused->why == Orders::Refusal::ClosesLoop) {
            const int target = plan.designated(step.include);
            return Words::includeWouldLoop(nameOf(m_read, order),
                                           target >= 0 ? nameOf(m_read, target) : step.include);
        }
        return Words::orderRefused(nameOf(m_read, order), at + 1, refused->why, refused->target);
    }
    if (Orders::includes(step))
        return {};
    if (step.from.has_value() && step.to.has_value() && *step.from > *step.to)
        return Words::rangeBackwards();
    if (step.unit == u"CHAPTER"_s && !step.edition.isEmpty())
        return Words::chapterRangeNamesNoEdition();
    if (step.unit == u"VOLUME"_s && step.edition.isEmpty()
        && m_editionCount.value(step.work.trimmed()) >= 2)
        return Words::rangeNeedsEdition(step.work.trimmed());
    return {};
}

const OrderDraft::State &OrderDraft::drawn() const
{
    if (const State &core = state(); core.drawn)
        return core;
    State &out = *m_state;
    out.drawn = true;
    const Orders::Plan &plan = *out.plan;
    const auto &flats = out.flats;
    out.tabs = tabsOf(out.draft, m_read, flats, m_chosen);
    if (m_chosen < 0)
        return out;

    const QList<Draft> &steps = m_orders.at(m_chosen).steps;
    for (int at = 0; at < steps.size(); ++at) {
        const Draft &line = steps.at(at);
        const Orders::Step &step = line.step;
        const bool include = Orders::includes(step);
        const int target = include ? plan.target(m_chosen, at) : -1;
        const QString targetName = targetNameOf(m_read, target, step);
        out.steps << QVariantMap{
            {u"at"_s, at},
            {u"kind"_s, include ? u"include"_s : u"work"_s},
            {u"label"_s, labelOf(step, include, targetName)},
            {u"unit"_s, step.unit},
            {u"unitLabel"_s, Words::orderUnit(step.unit)},
            {u"edition"_s, step.edition},
            {u"from"_s, numberOf(step.from)},
            {u"to"_s, numberOf(step.to)},
            {u"proposed"_s, line.proposed},
            {u"absent"_s, !include && !m_works.contains(step.work.trimmed())},
            {u"inherited"_s, inheritedOf(flats, target)},
            {u"inheritedNote"_s, include ? Words::inheritedFrom(targetName) : QString()},
            {u"selected"_s, at == m_selected},
            {u"doubt"_s, at == m_refusedAt ? m_refusal : doubtOf(out, m_chosen, at)}};
    }
    const std::optional<QList<Orders::Step>> &flat = flats.at(m_chosen);
    const auto flatCount = int(flat ? flat->size() : steps.size());
    out.footer = Words::orderFooter(int(steps.size()), flatCount);
    out.targets = targetsOf(out.draft, m_read, out.loops, out.designations, m_chosen);
    if (m_selected < 0 || m_selected >= steps.size())
        return out;

    const Orders::Step &step = steps.at(m_selected).step;
    const bool include = Orders::includes(step);
    const int target = include ? plan.target(m_chosen, m_selected) : -1;
    const QString targetName = targetNameOf(m_read, target, step);
    QVariantList units;
    for (const QString &unit : {QString(), u"VOLUME"_s, u"CHAPTER"_s})
        units << QVariantMap{{u"value"_s, unit}, {u"label"_s, Words::orderUnit(unit)}};
    out.step = QVariantMap{
        {u"kind"_s, include ? u"include"_s : u"work"_s},
        {u"target"_s,
         target >= 0 ? Orders::declaredId(out.draft.orders.at(target), target) : step.include},
        {u"targets"_s, out.targets},
        {u"inheritedCount"_s, target >= 0 && flats.at(target) ? int(flats.at(target)->size()) : 0},
        {u"flatCount"_s, flatCount},
        {u"inheritedNote"_s, include ? Words::inheritedFrom(targetName) : QString()},
        {u"units"_s, units},
        {u"editions"_s, m_editions.value(step.work.trimmed())},
        {u"works"_s, m_works}};
    return out;
}

bool OrderDraft::writable() const
{
    return !state().doubtful && !(state().past && !m_pastWhenRead);
}

QString OrderDraft::saveLabel() const
{
    return Words::saveOrders(int(m_edited.count(true)));
}

bool OrderDraft::dirty() const
{
    return m_edited.contains(true);
}

void OrderDraft::touched()
{
    m_state.reset();
    m_changed();
}

void OrderDraft::refuse(int at, const QString &why)
{
    m_refusedAt = at;
    m_refusal = why;
    touched();
}

template <typename Apply>
bool OrderDraft::change(const Apply &apply)
{
    if (m_chosen < 0)
        return false;
    const QList<Draft> before = m_orders.at(m_chosen).steps;
    const bool wasEdited = m_edited.at(m_chosen);
    const int wasSelected = m_selected;
    apply(m_orders[m_chosen].steps);
    m_edited[m_chosen] = true;
    m_refusedAt = -1;
    m_refusal.clear();
    m_state.reset();
    // The server would cut what this change wrote: undone, rather than left for the scan to
    // drop without a word. A file already past is not made worse by being edited elsewhere.
    if (state().past && !m_pastWhenRead) {
        m_orders[m_chosen].steps = before;
        m_edited[m_chosen] = wasEdited;
        m_selected = wasSelected;
        refuse(m_selected, Words::orderPastTheCeiling());
        return false;
    }
    m_changed();
    return true;
}

void OrderDraft::choose(const QString &orderId)
{
    const int order = indexOf(orderId);
    if (order < 0)
        return;
    m_chosen = order;
    propose(order);
    m_selected = firstToLookAt(order);
    m_refusedAt = -1;
    m_refusal.clear();
    touched();
}

void OrderDraft::select(int at)
{
    if (m_chosen < 0 || at < 0 || at >= m_orders.at(m_chosen).steps.size())
        return;
    m_selected = at;
    touched();
}

void OrderDraft::addWork(const QString &work)
{
    const QString said = work.trimmed();
    if (said.isEmpty())
        return;
    change([this, &said](QList<Draft> &steps) {
        Draft draft;
        draft.step.work = said;
        steps << draft;
        m_selected = int(steps.size()) - 1;
    });
}

bool OrderDraft::addInclude(const QString &orderId)
{
    const int target = indexOf(orderId);
    if (target < 0 || state().loops.value(target, true))
        return false;
    // Offered greyed with the reason, as a loop is: nothing the server reads would reach it.
    const QString said = state().designations.value(target);
    if (said.isEmpty())
        return false;
    return change([this, &said](QList<Draft> &steps) {
        Draft draft;
        draft.step.include = said;
        steps << draft;
        m_selected = int(steps.size()) - 1;
    });
}

bool OrderDraft::setInclude(int at, const QString &orderId)
{
    const int target = indexOf(orderId);
    if (m_chosen < 0 || at < 0 || at >= m_orders.at(m_chosen).steps.size() || target < 0 || state().loops.value(target, true))
        return false;
    const QString said = state().designations.value(target);
    if (said.isEmpty()) {
        refuse(at, Words::orderDisregarded(nameOf(m_read, target)));
        return false;
    }
    return change([this, at, &said](QList<Draft> &steps) {
        // The object it was read from stays: what is neither a range nor a work leaves with it.
        // The keys of a range or a work go, as the plan reads the object: one left there kept
        // the inclusion refused for carrying more, once nothing else did.
        Orders::Step &step = steps[at].step;
        step.work.clear();
        step.unit.clear();
        step.edition.clear();
        step.from.reset();
        step.to.reset();
        for (const QString &key : {u"work"_s, u"unit"_s, u"edition"_s, u"from"_s, u"to"_s})
            step.raw.remove(key);
        step.include = said;
        steps[at].proposed = false;
        steps[at].replaced.clear();
        m_selected = at;
    });
}

bool OrderDraft::setRange(int at, const QString &unit, const QString &edition,
                           const QString &from, const QString &to)
{
    if (m_chosen < 0 || at < 0 || at >= m_orders.at(m_chosen).steps.size()
        || Orders::includes(m_orders.at(m_chosen).steps.at(at).step))
        return false;
    const QString code = unit.trimmed().toUpper();
    if (!code.isEmpty() && code != u"VOLUME"_s && code != u"CHAPTER"_s)
        return false;
    std::optional<double> first;
    std::optional<double> last;
    for (const auto &[typed, number] : {std::pair{&from, &first}, std::pair{&to, &last}}) {
        if (!parse(*typed, *number)) {
            refuse(at, Words::notANumber(typed->trimmed(), false));
            return false;
        }
    }
    // A whole work has no range: one left behind would be read as a range of the whole.
    if (code.isEmpty()) {
        first.reset();
        last.reset();
    }
    if (code == u"CHAPTER"_s && !edition.trimmed().isEmpty()) {
        refuse(at, Words::chapterRangeNamesNoEdition());
        return false;
    }
    if (first && last && *first > *last) {
        refuse(at, Words::rangeBackwards());
        return false;
    }
    return change([this, at, &code, &edition, &first, &last](QList<Draft> &steps) {
        Orders::Step &step = steps[at].step;
        step.unit = code;
        step.edition = edition.trimmed();
        step.from = first;
        step.to = last;
        m_selected = at;
    });
}

void OrderDraft::move(int at, int to)
{
    if (m_chosen < 0 || at == to)
        return;
    if (const qsizetype size = m_orders.at(m_chosen).steps.size();
        at < 0 || at >= size || to < 0 || to >= size)
        return;
    change([this, at, to](QList<Draft> &steps) {
        steps.move(at, to);
        m_selected = to;
    });
}

void OrderDraft::remove(int at)
{
    if (m_chosen < 0 || at < 0 || at >= m_orders.at(m_chosen).steps.size())
        return;
    change([this, at](QList<Draft> &steps) {
        if (const Draft gone = steps.takeAt(at); gone.proposed) {
            for (qsizetype k = 0; k < gone.replaced.size(); ++k)
                steps.insert(at + k, Draft{gone.replaced.at(k), false, {}});
        }
        m_selected = steps.isEmpty() ? -1 : int(std::min<qsizetype>(at, steps.size() - 1));
    });
}

void OrderDraft::acceptProposal(int at)
{
    if (m_chosen < 0 || at < 0 || at >= m_orders.at(m_chosen).steps.size()
        || !m_orders.at(m_chosen).steps.at(at).proposed)
        return;
    change([this, at](QList<Draft> &steps) {
        // The steps it stands for go, and with them any key no version of Leaf reads that they
        // carried: they are written in the order included, and are not written twice.
        steps[at].proposed = false;
        steps[at].replaced.clear();
        m_selected = at;
    });
}

QString OrderDraft::newOrder(const QString &name)
{
    const QString said = name.trimmed();
    if (said.isEmpty())
        return {};
    Orders::Order order;
    order.id = Orders::newId(universe(), said);
    order.name = said;
    order.raw = QJsonObject{{u"id"_s, order.id}, {u"name"_s, said}};
    m_read.orders << order;
    m_orders << Order{{}, true};
    m_edited << true;
    m_state.reset();
    if (state().past && !m_pastWhenRead) {
        m_read.orders.removeLast();
        m_orders.removeLast();
        m_edited.removeLast();
        refuse(m_selected, Words::orderPastTheCeiling());
        return {};
    }
    m_chosen = int(m_read.orders.size()) - 1;
    m_selected = -1;
    m_refusedAt = -1;
    m_refusal.clear();
    touched();
    return order.id;
}

