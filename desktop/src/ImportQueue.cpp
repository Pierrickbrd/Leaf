#include "ImportQueue.h"

#include "Card.h"
#include "Carrier.h"
#include "Imports.h"

#include <QDebug>
#include <QQmlEngine>

#include <algorithm>

using Stage = Card::Stage;

namespace {

/// Starts the card at `row`, still at « Reviewing », as `startAll` does: held back, it waits;
/// corrected, it is announced again first.
void startReviewed(Carrier &carrier, Card &card, int row)
{
    using enum Card::Stage;
    if (Carrier::heldBack(card))
        return;
    card.road.confirmed = true;
    card.road.released = true;
    if (card.road.edited) {
        carrier.reannounce(row);
        return;
    }
    carrier.settle(row, card.road.moves.isEmpty() || card.road.target ? Ready : Deciding);
    carrier.release(row, false);
}

} // namespace

ImportQueue *ImportQueue::create(QQmlEngine *engine, QJSEngine *)
{
    auto *imports = engine->singletonInstance<Imports *>(qmlTypeId("Leaf", 1, 0, "Imports"));
    if (!imports) {
        qWarning().noquote()
            << QStringLiteral("error resolving the Imports singleton — nothing dropped will "
                              "be sent");
    }
    return new ImportQueue(imports);
}

ImportQueue::ImportQueue(Imports *imports, QObject *parent)
    : QObject(parent)
    // Never null, for `ImportTally`'s reason: a list of its own, which nothing is ever on.
    , m_imports(imports ? imports : new Imports(nullptr, this))
{
}

void ImportQueue::confirm()
{
    using enum Stage;
    QList<Card> &rows = m_imports->m_rows;
    Carrier &carrier = *m_imports->m_carrier;
    // Settling and announcing again change a card, never how many there are.
    const auto count = int(rows.size());
    for (int row = 0; row < count; ++row) {
        Card &card = rows[row];
        // Blocked by what will travel only, the same filter that stopped the card: a
        // duplicate number on two files the library already holds is not sent, and holding
        // the card for it would hold it for ever.
        if (card.stage != Reviewing || Carrier::heldBack(card))
            continue;
        card.road.confirmed = true;
        card.road.released = true;
        // A correction has to reach the server, whose session was announced without it.
        if (card.road.edited) {
            carrier.reannounce(row);
            continue;
        }
        // A drop aimed at something already held settled its moves in the review, tick or
        // no tick: asking again would leave it at « À vous de dire » with no screen to answer.
        carrier.settle(row, card.road.moves.isEmpty() || card.road.target ? Ready : Deciding);
    }
    send();
}

void ImportQueue::prepare()
{
    using enum Stage;
    const QList<Card> &rows = m_imports->m_rows;
    Carrier &carrier = *m_imports->m_carrier;
    for (int row = 0; row < rows.size(); ++row) {
        const Card &card = rows.at(row);
        if (card.stage != Reviewing || card.road.edited || Carrier::heldBack(card))
            continue;
        carrier.settle(row, card.road.moves.isEmpty() || card.road.target ? Ready : Deciding);
    }
}

void ImportQueue::clearFiled()
{
    QList<Card> &rows = m_imports->m_rows;
    bool any = false;
    for (int row = int(rows.size()) - 1; row >= 0; --row) {
        if (rows.at(row).stage != Stage::Filed)
            continue;
        m_imports->beginRemoveRows({}, row, row);
        rows.remove(row);
        m_imports->endRemoveRows();
        any = true;
    }
    if (!any)
        return;
    m_imports->forgetDropsWithoutCards();
    emit m_imports->changed();
}

void ImportQueue::giveUpPreparing()
{
    using enum Stage;
    const QList<Card> &rows = m_imports->m_rows;
    Carrier &carrier = *m_imports->m_carrier;
    for (int row = int(rows.size()) - 1; row >= 0; --row) {
        if (const Stage stage = rows.at(row).stage;
            stage == Asking || stage == Deciding || stage == Ready || stage == Reviewing) {
            carrier.abandon(row);
        }
    }
    m_imports->forgetDropsWithoutCards();
}

void ImportQueue::send()
{
    QList<Card> &rows = m_imports->m_rows;
    Carrier &carrier = *m_imports->m_carrier;
    // What would do nothing goes first, and the server is told so its session is cleaned
    // up. Leaving them in a list of transfers is one more card to read for nothing, and
    // their inbox folders are the same leak abandoning one closes — measured on a real
    // server, twenty-six sessions nobody would ever claim.
    for (int row = rows.size() - 1; row >= 0; --row) {
        if (rows.at(row).stage == Stage::Ready && rows.at(row).nothingToDo())
            carrier.abandon(row);
    }
    m_imports->forgetDropsWithoutCards();
    // What is let go is agreed to, whether it starts now or waits its turn behind the one
    // in flight: counted again, the next « Importer » said it would send it, and corrected
    // while it waited, it went back to `Reviewing` with nothing offering to send it again
    // (`Carrier::correctable`).
    for (Card &card : rows) {
        if (card.stage == Stage::Ready && (card.folder || !card.chosen.isEmpty())) {
            card.road.confirmed = true;
            card.road.released = true;
        }
        // Still read: the press covers it too, once it is (`Carrier::tookFolder`).
        if (card.folder && card.stage == Stage::Asking)
            card.road.awaited = true;
    }
    carrier.pump();
}

void ImportQueue::pause(int row)
{
    using enum Stage;
    QList<Card> &rows = m_imports->m_rows;
    Carrier &carrier = *m_imports->m_carrier;
    if (row < 0 || row >= rows.size())
        return;
    if (const Stage stage = rows.at(row).stage;
        stage != Sending && stage != Ready) {
        return;
    }

    // Not let go any more, whatever it was waiting for: a card that waited behind another and
    // was paused there must not set off when the slot frees.
    rows[row].road.released = false;
    carrier.settle(row, Paused);
    carrier.pump();
}

void ImportQueue::resume(int row)
{
    const QList<Card> &rows = m_imports->m_rows;
    Carrier &carrier = *m_imports->m_carrier;
    if (row < 0 || row >= rows.size() || rows.at(row).stage != Stage::Paused)
        return;
    // Takes the slot back, and whoever held it steps behind — "now it is that one that
    // waits". A transfer interrupted this way loses nothing: the server keeps what arrived,
    // and the next attempt starts past it.
    carrier.release(row, true);
}

void ImportQueue::start(int row)
{
    using enum Stage;
    QList<Card> &rows = m_imports->m_rows;
    Carrier &carrier = *m_imports->m_carrier;
    if (row < 0 || row >= rows.size())
        return;
    Card &card = rows[row];
    switch (card.stage) {
    case Paused:
    case Failed:
        // A card that failed before it was announced has nothing on the server to go on from,
        // and its reason stays where the first step wrote it.
        if (card.stage == Failed && (!card.road.confirmed || card.id.isEmpty()))
            return;
        carrier.release(row, true);
        return;
    case Reviewing:
        // The press that agrees to a card, for this one card: what `confirm` does for all.
        if (Carrier::heldBack(card))
            return;
        card.road.confirmed = true;
        card.road.released = true;
        if (card.road.edited) {
            carrier.reannounce(row);
            return;
        }
        carrier.settle(row, card.road.moves.isEmpty() || card.road.target ? Ready : Deciding);
        carrier.release(row, true);
        return;
    case Ready:
        if (card.road.released)
            return;
        // What would do nothing is not sent, and the server is told so its session is cleaned
        // up, as `send` does for all of them.
        if (card.nothingToDo()) {
            carrier.abandon(row);
            m_imports->forgetDropsWithoutCards();
            return;
        }
        if (!card.folder && card.chosen.isEmpty())
            return;
        card.road.confirmed = true;
        carrier.release(row, true);
        return;
    default:
        return;
    }
}

void ImportQueue::startAll()
{
    using enum Stage;
    QList<Card> &rows = m_imports->m_rows;
    Carrier &carrier = *m_imports->m_carrier;
    // Rows can leave the list as they are visited (a card that would do nothing), so the list
    // is walked from the end and each is asked by its token.
    QList<quint64> tokens;
    for (const Card &card : rows)
        tokens << card.token;
    for (const quint64 token : tokens) {
        const auto found = std::ranges::find(rows, token, &Card::token);
        if (found == rows.end())
            continue;
        const auto row = int(found - rows.begin());
        Card &card = rows[row];
        switch (card.stage) {
        case Ready:
            if (card.road.released || (!card.folder && card.chosen.isEmpty()))
                continue;
            if (card.nothingToDo()) {
                carrier.abandon(row);
                continue;
            }
            card.road.confirmed = true;
            carrier.release(row, false);
            break;
        case Reviewing:
            startReviewed(carrier, card, row);
            break;
        case Paused:
            carrier.release(row, false);
            break;
        case Failed:
            if (card.road.confirmed && !card.id.isEmpty())
                carrier.release(row, false);
            break;
        default:
            break;
        }
    }
    m_imports->forgetDropsWithoutCards();
}

void ImportQueue::cancel(int row)
{
    using enum Stage;
    const QList<Card> &rows = m_imports->m_rows;
    Carrier &carrier = *m_imports->m_carrier;
    if (row < 0 || row >= rows.size())
        return;
    // Only a card that was agreed to has anything to put back: one still to be checked has
    // sent nothing and holds no session worth dropping, and `abandon` is its way out.
    if (const Card &card = rows.at(row); !card.road.confirmed || card.stage == Filed
                                           || card.stage == Asking || card.stage == Deciding) {
        return;
    }
    carrier.restart(row);
}
