#include "OrderEditor.h"

#include "Card.h"
#include "Carrier.h"
#include "Facts.h"
#include "Imports.h"

#include <QDebug>
#include <QJsonArray>

#include <algorithm>
#include <memory>

using namespace Qt::StringLiterals;

namespace {

using Manifest::Level;

/// The card `token` names in `imports`' queue, nothing once it has left it.
const Card *cardOf(const Imports *imports, quint64 token)
{
    if (!imports || token == 0)
        return nullptr;
    const auto found = std::ranges::find(imports->cards(), token, &Card::token);
    return found == imports->cards().end() ? nullptr : std::to_address(found);
}

} // namespace

OrderEditor *OrderEditor::create(QQmlEngine *engine, QJSEngine *)
{
    auto *imports = engine->singletonInstance<Imports *>(qmlTypeId("Leaf", 1, 0, "Imports"));
    if (!imports) {
        qWarning().noquote()
            << QStringLiteral("error resolving the Imports singleton — no order will open");
    }
    return new OrderEditor(imports);
}

OrderEditor::OrderEditor(Imports *imports, QObject *parent)
    : QObject(parent)
    , m_imports(imports)
    , m_draft([this] { emit changed(); })
{
    // Whether the card can still be corrected is the queue's, and moves with it: the button
    // drawn from `savable` would otherwise stay lit over a card that left.
    if (m_imports) {
        connect(m_imports, &Imports::changed, this, [this] {
            if (open())
                emit changed();
        });
    }
}

bool OrderEditor::openOn(const QString &universeId, const QString &orderId)
{
    const qsizetype slash = universeId.indexOf(u'/');
    bool ok = false;
    const quint64 token = universeId.left(slash).toULongLong(&ok);
    if (slash < 0 || !ok || !m_imports)
        return false;
    const QString at = universeId.mid(slash + 1);
    const auto found = std::ranges::find(m_imports->cards(), token, &Card::token);
    if (found == m_imports->cards().end() || !Carrier::correctable(*found))
        return false;
    const QList<Facts::Sheet> &sheets = found->road.sheets;
    const auto sheet = std::ranges::find_if(sheets, [&at](const Facts::Sheet &one) {
        return one.at == at && one.level == Level::Universe;
    });
    if (sheet == sheets.end())
        return false;

    close();
    m_token = token;
    m_at = at;
    m_draft.read(sheets, *sheet, orderId);
    return true;
}

void OrderEditor::close()
{
    m_token = 0;
    m_at.clear();
    m_draft.clear();
}

QVariantList OrderEditor::tabs() const
{
    return open() ? m_draft.drawn().tabs : QVariantList();
}

QVariantList OrderEditor::steps() const
{
    return open() ? m_draft.drawn().steps : QVariantList();
}

QVariantMap OrderEditor::step() const
{
    return open() ? m_draft.drawn().step : QVariantMap();
}

QString OrderEditor::footer() const
{
    return open() ? m_draft.drawn().footer : QString();
}

QVariantList OrderEditor::targets() const
{
    return open() ? m_draft.drawn().targets : QVariantList();
}

bool OrderEditor::savable() const
{
    const Card *on = cardOf(m_imports, m_token);
    return on && Carrier::correctable(*on) && m_draft.writable();
}

QString OrderEditor::saveLabel() const
{
    return m_draft.saveLabel();
}

bool OrderEditor::dirty() const
{
    return m_draft.dirty();
}

void OrderEditor::choose(const QString &orderId)
{
    if (open())
        m_draft.choose(orderId);
}

void OrderEditor::select(int at)
{
    if (open())
        m_draft.select(at);
}

void OrderEditor::addWork(const QString &work)
{
    if (open())
        m_draft.addWork(work);
}

bool OrderEditor::addInclude(const QString &orderId)
{
    return open() && m_draft.addInclude(orderId);
}

bool OrderEditor::setInclude(int at, const QString &orderId)
{
    return open() && m_draft.setInclude(at, orderId);
}

bool OrderEditor::setRange(int at, const QString &unit, const QString &edition,
                           const QString &from, const QString &to)
{
    return open() && m_draft.setRange(at, unit, edition, from, to);
}

void OrderEditor::move(int at, int to)
{
    if (open())
        m_draft.move(at, to);
}

void OrderEditor::remove(int at)
{
    if (open())
        m_draft.remove(at);
}

void OrderEditor::acceptProposal(int at)
{
    if (open())
        m_draft.acceptProposal(at);
}

QString OrderEditor::newOrder(const QString &name)
{
    return open() ? m_draft.newOrder(name) : QString();
}

bool OrderEditor::save()
{
    const Card *on = cardOf(m_imports, m_token);
    if (!open() || !savable())
        return false;
    if (dirty()) {
        const QJsonArray orders = Orders::written(m_draft.universe());
        // The array the sheet already holds is no correction, and `edit` refuses it as one.
        const auto sheet = std::ranges::find_if(on->road.sheets, [this](const Facts::Sheet &one) {
            return one.at == m_at && one.level == Level::Universe;
        });
        const Facts::Field *held = sheet == on->road.sheets.end()
                                       ? nullptr
                                       : OrderDraft::said(*sheet, u"orders"_s);
        if ((!held || held->value != QJsonValue(orders))
            && !m_imports->edit(m_token, m_at, u"orders"_s, orders))
            return false;
    }
    close();
    return true;
}
