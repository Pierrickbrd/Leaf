#include "ImportTally.h"

#include "Card.h"
#include "Carrier.h"
#include "Imports.h"
#include "Words.h"

#include <QDebug>
#include <QQmlEngine>

#include <algorithm>

using namespace Qt::StringLiterals;
using Lines::bytesToSend;
using Lines::filesToSend;

using Stage = Card::Stage;

namespace {

/// How many of the volumes a card sends have arrived: all of them once it is filed, the ones
/// before the one in flight while it moves.
int filesSent(const Card &card)
{
    if (card.stage == Card::Stage::Filed)
        return Lines::filesToSend(card);
    if (!card.folder)
        return 0;
    return std::min(card.road.at, Lines::filesToSend(card));
}

/// Whether a line has gone: let go, moving, stopped part-way, done or failed after leaving. A
/// line put back to zero has not.
bool hasGone(const Card &one)
{
    using enum Card::Stage;
    return one.road.released || one.stage == Sending || one.stage == Filing
           || one.stage == Filed || one.stage == Paused
           || (one.stage == Failed && one.road.confirmed);
}

} // namespace

ImportTally *ImportTally::create(QQmlEngine *engine, QJSEngine *)
{
    auto *imports = engine->singletonInstance<Imports *>(qmlTypeId("Leaf", 1, 0, "Imports"));
    if (!imports) {
        qWarning().noquote()
            << QStringLiteral("error resolving the Imports singleton — the import will count "
                              "an empty list");
    }
    return new ImportTally(imports);
}

ImportTally::ImportTally(Imports *imports, QObject *parent)
    : QObject(parent)
    // Never null, so that no count has to ask: a list of its own when there is none to read,
    // which counts nothing — what an import nobody can reach has to say.
    , m_imports(imports ? imports : new Imports(nullptr, this))
{
    // Every count is a sum over the cards, asked again whenever the list says it moved.
    connect(m_imports, &ImportRows::changed, this, &ImportTally::changed);
    connect(m_imports, &ImportRows::progressed, this, &ImportTally::progressed);
}

int ImportTally::inFlight() const
{
    const QList<Card> &rows = m_imports->m_rows;
    int total = 0;
    for (const Card &one : rows) {
        if (one.stage != Stage::Filed && one.stage != Stage::Failed)
            ++total;
    }
    return total;
}

int ImportTally::reading() const
{
    const QList<Card> &rows = m_imports->m_rows;
    const Carrier &carrier = *m_imports->m_carrier;
    // A folder queued behind another is not yet "being looked at" — it is waiting its turn,
    // which `Carrier::describeNext` serialises on purpose. Counting it here would make the
    // button wait on folders nobody has started walking, for as long as the queue is deep.
    int total = carrier.walkingAFolder() ? 1 : 0;
    for (const Card &one : rows) {
        if (one.stage != Stage::Asking)
            continue;
        // A file always counts: each one is a live request of its own. A folder counts once
        // its walk is done and it is only the server's answer being waited on — before that
        // it is either the one `m_describing` already counted, or still queued.
        if (!one.folder || one.road.described)
            ++total;
    }
    return total;
}

qint64 ImportTally::volumesChecked() const
{
    const QList<Card> &rows = m_imports->m_rows;
    qint64 total = 0;
    for (const Card &one : rows)
        total += one.progress(m_imports->m_walkingToken).value(u"done"_s).toLongLong();
    return total;
}

qint64 ImportTally::volumesToCheck() const
{
    const QList<Card> &rows = m_imports->m_rows;
    qint64 total = 0;
    for (const Card &one : rows)
        total += one.progress(m_imports->m_walkingToken).value(u"whole"_s).toLongLong();
    return total;
}

int ImportTally::listed() const
{
    const QList<Card> &rows = m_imports->m_rows;
    return int(std::ranges::count_if(rows, [](const Card &one) {
        return one.stage != Stage::Failed;
    }));
}

int ImportTally::toSendFiles() const
{
    const QList<Card> &rows = m_imports->m_rows;
    int files = 0;
    for (const Card &card : rows) {
        if (card.shown())
            files += filesToSend(card);
    }
    return files;
}

qint64 ImportTally::toSendBytes() const
{
    const QList<Card> &rows = m_imports->m_rows;
    qint64 bytes = 0;
    for (const Card &card : rows) {
        if (card.shown())
            bytes += bytesToSend(card);
    }
    return bytes;
}

bool ImportTally::started() const
{
    const QList<Card> &rows = m_imports->m_rows;
    return std::ranges::any_of(rows, hasGone);
}

int ImportTally::sendingFiles() const
{
    const QList<Card> &rows = m_imports->m_rows;
    int files = 0;
    for (const Card &card : rows) {
        if (card.shown() && hasGone(card))
            files += filesToSend(card);
    }
    return files;
}

qint64 ImportTally::sendingBytes() const
{
    const QList<Card> &rows = m_imports->m_rows;
    qint64 bytes = 0;
    for (const Card &card : rows) {
        if (card.shown() && hasGone(card))
            bytes += bytesToSend(card);
    }
    return bytes;
}

int ImportTally::startable() const
{
    using enum Stage;
    const QList<Card> &rows = m_imports->m_rows;
    return int(std::ranges::count_if(rows, [](const Card &one) {
        switch (one.stage) {
        case Ready:
            return !one.road.released && (one.folder || !one.chosen.isEmpty());
        case Reviewing:
            return !Carrier::heldBack(one);
        case Paused:
            return true;
        case Failed:
            return one.road.confirmed && !one.id.isEmpty();
        default:
            return false;
        }
    }));
}

bool ImportTally::finished() const
{
    const QList<Card> &rows = m_imports->m_rows;
    bool any = false;
    for (const Card &one : rows) {
        if (!one.shown())
            continue;
        any = true;
        if (one.stage != Stage::Filed && one.stage != Stage::Failed)
            return false;
    }
    return any;
}

bool ImportTally::completed() const
{
    const QList<Card> &rows = m_imports->m_rows;
    bool any = false;
    for (const Card &one : rows) {
        if (!one.shown())
            continue;
        any = true;
        if (one.stage != Stage::Filed)
            return false;
    }
    return any;
}

int ImportTally::failedLines() const
{
    const QList<Card> &rows = m_imports->m_rows;
    return int(std::ranges::count_if(rows, [](const Card &one) {
        return one.stage == Stage::Failed && one.shown();
    }));
}

int ImportTally::sentFiles() const
{
    const QList<Card> &rows = m_imports->m_rows;
    int files = 0;
    for (const Card &one : rows) {
        if (one.shown())
            files += filesSent(one);
    }
    return files;
}

qint64 ImportTally::sentBytes() const
{
    const QList<Card> &rows = m_imports->m_rows;
    qint64 bytes = 0;
    for (const Card &one : rows) {
        if (one.shown())
            bytes += one.stage == Stage::Filed ? bytesToSend(one)
                                               : std::min(one.sent, bytesToSend(one));
    }
    return bytes;
}

int ImportTally::heldFiles() const
{
    const QList<Card> &rows = m_imports->m_rows;
    int files = 0;
    for (const Card &card : rows) {
        if (card.stage == Stage::Failed || !card.folder)
            continue;
        files += int(std::ranges::count_if(card.road.tree.files, [&card](const auto &file) {
            return card.road.alreadyThere.contains(file.path);
        }));
    }
    return files;
}

QString ImportTally::heldNote() const
{
    const QList<Card> &rows = m_imports->m_rows;
    for (const Card &card : rows) {
        if (card.stage == Stage::Failed || !card.folder || card.road.alreadyThere.isEmpty())
            continue;
        // The series it is aimed at, else the one it resembles, else its own name: the library
        // holds it under that name, and « 14 fichiers » named nobody.
        QString name = card.name;
        if (card.road.target)
            name = card.road.target->name;
        else if (!card.road.resembles.isEmpty())
            name = card.road.resembles.first().name;
        // Volumes, which are the archives: the sidecars beside them are not « tomes ».
        const auto isArchive = [](const QString &path) {
            return path.endsWith(u".cbz"_s, Qt::CaseInsensitive);
        };
        const auto held = int(std::ranges::count_if(card.road.alreadyThere, isArchive));
        const auto whole = int(std::ranges::count_if(card.road.tree.files, [&](const auto &file) {
            return isArchive(file.path);
        }));
        if (held == 0)
            continue;
        return Words::heldNamed(name, held, whole);
    }
    return {};
}

int ImportTally::launched() const
{
    using enum Stage;
    const QList<Card> &rows = m_imports->m_rows;
    return int(std::ranges::count_if(rows, [](const Card &one) {
        return one.road.confirmed || one.stage == Sending || one.stage == Filing
               || one.stage == Filed || one.stage == Paused;
    }));
}

int ImportTally::blocked() const
{
    const QList<Card> &rows = m_imports->m_rows;
    return int(std::ranges::count_if(rows, [](const Card &one) {
        return one.stage == Stage::Reviewing && Carrier::heldBack(one);
    }));
}

bool ImportTally::busy() const
{
    const Carrier &carrier = *m_imports->m_carrier;
    return carrier.holding() >= 0;
}
