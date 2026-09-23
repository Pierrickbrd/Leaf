#include "Elsewhere.h"

#include "Words.h"

#include <QDebug>
#include <QJsonArray>
#include <QUrlQuery>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <memory>

using namespace Qt::StringLiterals;

Elsewhere *Elsewhere::create(QQmlEngine *engine, QJSEngine *)
{
    auto *server = engine->singletonInstance<Server *>(qmlTypeId("Leaf", 1, 0, "Server"));
    if (!server) {
        qWarning().noquote()
            << QStringLiteral("error resolving the Server singleton — a series will offer "
                              "nowhere to go next");
    }
    return new Elsewhere(server);
}

Elsewhere::Elsewhere(Server *server, QObject *parent)
    : QObject(parent)
    , m_server(server)
{
}

QVariantList Elsewhere::orders() const
{
    QVariantList said;
    for (const Api::ReadingOrder &one : m_orders)
        said.append(QVariantMap{{u"identifier"_s, one.id},
                                {u"name"_s, one.name},
                                {u"chosen"_s, one.id == m_chosen}});
    return said;
}

QString Elsewhere::chosenName() const
{
    const Api::ReadingOrder *walk = chosen();
    return walk == nullptr ? QString() : walk->name;
}

const Api::ReadingOrder *Elsewhere::chosen() const
{
    const auto found = std::ranges::find(m_orders, m_chosen, &Api::ReadingOrder::id);
    return found == m_orders.cend() ? nullptr : std::to_address(found);
}

QVariantMap Elsewhere::tileOf(const Api::Series *one, const QString &name,
                              const QString &detail, bool here) const
{
    // The same keys whatever the tile stands for, so the block does not change component when
    // a way through is chosen. A step names its work and its edition in clear — « so a step
    // can be drawn without a request of its own » — but a cover does not travel that way, and
    // comes from the universe answer this block already holds.
    const QString identifier = one == nullptr ? QString() : one->id;
    return {{u"seriesId"_s, identifier},
            {u"name"_s, name},
            {u"detail"_s, !detail.isEmpty() || one == nullptr
                              ? detail
                              : Words::volumes(one->holding.ownedVolumes, one->medium)},
            {u"cover"_s, identifier.isEmpty() || m_server == nullptr
                             ? QString()
                             : m_server->address() + u"/series/"_s + identifier + u"/cover"_s},
            {u"inProgress"_s,
             one != nullptr && one->holding.readStatus == Api::ReadStatus::InProgress},
            {u"howFarRead"_s, one == nullptr ? 0.0 : Api::howFarRead(*one)},
            {u"here"_s, here}};
}

const Api::Series *Elsewhere::seriesOf(const Api::ReadingStep &step) const
{
    // A volume step names the edition it counts in, « because volumes 1 to 7 is not the same
    // content in an edition of 42 and one of 34 ». A chapter step names none, and the work's
    // first edition is the one to draw.
    const auto found = step.seriesId.has_value()
                           ? std::ranges::find(m_siblings, *step.seriesId, &Api::Series::id)
                           : std::ranges::find(m_siblings, step.workId, &Api::Series::workId);
    return found == m_siblings.cend() ? nullptr : std::to_address(found);
}

qsizetype Elsewhere::hereStep(const Api::ReadingOrder &way) const
{
    qsizetype first = -1;
    for (qsizetype at = 0; at < way.steps.size(); ++at) {
        const Api::ReadingStep &step = way.steps.at(at);
        if (step.seriesId.value_or(QString()) != m_seriesId || m_seriesId.isEmpty())
            continue;
        if (first < 0)
            first = at;
        // The mark follows where the reader stands: when a work comes round twice, « ici » is
        // the stretch holding the volume being read and not both of them. The walk then says
        // something nothing else on the page does — where one is in the universe.
        if (m_at.has_value() && step.from.has_value() && step.to.has_value()
            && *m_at >= *step.from && *m_at <= *step.to)
            return at;
    }
    return first;
}

QVariantList Elsewhere::tiles() const
{
    QVariantList said;
    const Api::ReadingOrder *walk = chosen();
    if (walk == nullptr) {
        for (const Api::Series &one : m_siblings) {
            if (one.id == m_seriesId)
                continue;
            said.append(tileOf(&one, one.work, QString(), false));
        }
        return said;
    }

    // A tile per step. The same work may come round again, with a different stretch under it,
    // and that is the whole reason an order is not a sorted list of series.
    const qsizetype mark = hereStep(*walk);
    for (qsizetype at = 0; at < walk->steps.size(); ++at) {
        const Api::ReadingStep &step = walk->steps.at(at);
        said.append(tileOf(seriesOf(step), step.work, Words::stepRange(step), at == mark));
    }
    return said;
}

QVariantList Elsewhere::outside() const
{
    const Api::ReadingOrder *walk = chosen();
    if (walk == nullptr)
        return {};

    // Named by the walk, at any of its steps: a work the way sends round twice is inside it
    // once, and this block is what is *outside*.
    const auto walked = [walk](const Api::Series &one) {
        return std::ranges::any_of(walk->steps, [&one](const Api::ReadingStep &step) {
            return step.workId == one.workId;
        });
    };

    QVariantList said;
    for (const Api::Series &one : m_siblings) {
        if (one.id == m_seriesId || walked(one))
            continue;
        said.append(tileOf(&one, one.work, QString(), false));
    }
    return said;
}

void Elsewhere::point(const QString &universeId, const QString &universeName,
                      const QString &seriesId)
{
    ++m_generation;
    m_universeId = universeId;
    m_universe = universeName;
    m_seriesId = seriesId;
    m_orders.clear();
    m_chosen.clear();
    m_at.reset();
    m_pending = 0;

    if (universeName.isEmpty() || m_server == nullptr) {
        m_siblings.clear();
        emit changed();
        return;
    }

    const int mine = m_generation;
    QUrlQuery query;
    query.addQueryItem(u"universe"_s, universeName);
    // Every match, which the contract spells `0`. A universe is a handful of series and the
    // block draws all of them or misleads: a walk missing its third step is not a shorter
    // walk, it is a wrong one.
    query.addQueryItem(u"size"_s, u"0"_s);
    ++m_pending;
    m_server->get(u"/series"_s, query, this, [this, mine](const Server::Answer &answer) {
        if (mine == m_generation)
            tookSiblings(answer);
    });

    // A universe with no identifier is one the shelf answered about before the field existed:
    // the block still draws its series, and nothing can be asked about its ways.
    if (universeId.isEmpty()) {
        emit changed();
        return;
    }
    ++m_pending;
    m_server->get(u"/universes"_s, this, [this, mine](const Server::Answer &all) {
        if (mine == m_generation)
            tookUniverses(all);
    });
    emit changed();
}

void Elsewhere::chooseOrder(const QString &identifier)
{
    if (identifier == m_chosen)
        return;
    const bool known = std::ranges::any_of(
        m_orders, [&identifier](const Api::ReadingOrder &one) { return one.id == identifier; });
    m_chosen = known ? identifier : QString();
    emit changed();
}

void Elsewhere::readAt(double number)
{
    const std::optional<double> fresh =
        std::isnan(number) ? std::optional<double>() : std::optional<double>(number);
    if (fresh == m_at)
        return;
    m_at = fresh;
    emit changed();
}

void Elsewhere::tookSiblings(const Server::Answer &answer)
{
    --m_pending;
    // A refusal costs the block and not the page: a universe that answers nothing looks
    // exactly like a series that belongs to none, and the tab says so by being absent.
    if (!answer.went() || !answer.body.isObject()) {
        emit changed();
        return;
    }
    if (const Api::Read<Api::Page> read = Api::page(answer.body.object()); read.ok())
        m_siblings = read.value->items;
    emit changed();
}

void Elsewhere::tookUniverses(const Server::Answer &answer)
{
    --m_pending;
    int declared = 0;
    if (answer.went() && answer.body.isArray()) {
        for (const QJsonValue &one : answer.body.array()) {
            if (const Api::Read<Api::Universe> read = Api::universe(one.toObject());
                read.ok() && read.value->id == m_universeId) {
                declared = read.value->orderCount;
                break;
            }
        }
    }
    // The guard the count is published for: declaring no way through is the ordinary case —
    // « usually zero » — and asking anyway would be a request per series page for an answer
    // that is empty on most libraries.
    if (declared <= 0) {
        emit changed();
        return;
    }

    ++m_pending;
    const int mine = m_generation;
    m_server->get(u"/universes/"_s + m_universeId + u"/orders"_s, this,
                  [this, mine](const Server::Answer &ways) {
        if (mine == m_generation)
            tookOrders(ways);
    });
}

void Elsewhere::tookOrders(const Server::Answer &answer)
{
    --m_pending;
    if (!answer.went() || !answer.body.isArray()) {
        emit changed();
        return;
    }

    for (const QJsonValue &one : answer.body.array()) {
        if (!one.isObject())
            continue;
        if (const Api::Read<Api::ReadingOrder> read = Api::readingOrder(one.toObject());
            read.ok())
            m_orders.append(*read.value);
    }
    // The one the file marks, and the first otherwise: a universe that declares a way means
    // it to be walked, and opening on none would hide what it went to the trouble of saying.
    if (const auto marked = std::ranges::find(m_orders, true, &Api::ReadingOrder::isDefault);
        marked != m_orders.cend())
        m_chosen = marked->id;
    else if (!m_orders.isEmpty())
        m_chosen = m_orders.constFirst().id;
    emit changed();
}
