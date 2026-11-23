#include "ImportOffers.h"

#include "Api.h"
#include "Card.h"
#include "Carrier.h"
#include "Imports.h"
#include "Manifest.h"

#include <QDebug>
#include <QFileInfo>
#include <QJsonArray>
#include <QQmlEngine>

#include <span>

using namespace Qt::StringLiterals;

using Stage = Card::Stage;

namespace {

/// Aims `road` at `target`, with the number the chapter aimed at holds when it is one.
void aim(Card::Road &road, const Card::Road::Target &target,
         const QHash<QString, double> &chapterNumbers)
{
    road.target = target;
    if (target.kind == u"CHAPTER"_s && chapterNumbers.contains(target.id))
        road.chapterNumber = chapterNumbers.value(target.id);
    // A universe aimed at is read as one; any other target leaves the root as the
    // shape reads it, since the declaration it would write is the target's own.
    if (target.kind == u"UNIVERSE"_s) {
        road.roles.insert(QString(),
                          Shape::Choice{Shape::Role::Universe, Shape::Origin::Chosen});
    }
}

} // namespace

ImportOffers *ImportOffers::create(QQmlEngine *engine, QJSEngine *)
{
    auto *imports = engine->singletonInstance<Imports *>(qmlTypeId("Leaf", 1, 0, "Imports"));
    if (!imports) {
        qWarning().noquote()
            << QStringLiteral("error resolving the Imports singleton — nothing can be "
                              "dropped");
    }
    return new ImportOffers(imports);
}

ImportOffers::ImportOffers(Imports *imports, QObject *parent)
    : QObject(parent)
    // Never null, for `ImportTally`'s reason: a list of its own, with no server to ask.
    , m_imports(imports ? imports : new Imports(nullptr, this))
{
}

void ImportOffers::offer(const QStringList &paths)
{
    const QList<Card> &rows = m_imports->m_rows;
    Carrier &carrier = *m_imports->m_carrier;
    for (const QString &path : paths) {
        const QFileInfo about(path);
        if (about.isFile()) {
            if (carrier.rowOf(path) >= 0)
                continue;
            carrier.appendRow(path, about.fileName(), false, about.size());
            carrier.ask(int(rows.size()) - 1);
            continue;
        }
        if (!about.isDir())
            continue;
        // One card per universe or series the shape reads, and the shelves walked through
        // rather than kept. Measured: dropping the folder that holds every series rendered
        // one card named after it — and the server installs into `library/<that name>`, so
        // accepting that screen would have put the whole library inside a folder of itself.
        // Read from the shape and not from the declarations alone: a shelf of folders that
        // say nothing gave one card per folder of archives, and a universe of two series was
        // two series filed under no universe.
        Shape::Drop drop = Shape::read(path);
        drop.token = m_imports->m_nextDrop++;
        bool kept = false;
        for (const QString &within : Shape::cards(drop))
            kept = m_imports->addCard(drop, within) || kept;
        // Kept for as long as a card reads it: the roles can change there later.
        if (kept)
            m_imports->m_drops.append(std::move(drop));
    }
    carrier.describeNext();
    emit m_imports->changed();
}

void ImportOffers::reimport(const QString &kind, const QString &id, const QString &name,
                            const QStringList &paths)
{
    QList<Card> &rows = m_imports->m_rows;
    Carrier &carrier = *m_imports->m_carrier;
    const Card::Road::Target target{kind, id, name};
    for (const QString &path : paths) {
        const QFileInfo about(path);
        const auto before = int(rows.size());
        if (about.isFile()) {
            if (carrier.rowOf(path) >= 0)
                continue;
            Manifest::Node node;
            node.name = about.completeBaseName();
            node.size = about.size();
            Manifest::Entry entry;
            entry.path = about.fileName();
            entry.size = about.size();
            node.files << entry;
            carrier.appendRow(path, node.name, true, node.size);
            rows.last().road.node = node;
        } else if (about.isDir()) {
            Shape::Drop drop = Shape::read(path);
            drop.token = m_imports->m_nextDrop++;
            bool kept = false;
            for (const QString &within : Shape::cards(drop))
                kept = m_imports->addCard(drop, within) || kept;
            if (kept)
                m_imports->m_drops.append(std::move(drop));
        } else {
            continue;
        }
        // The cards this path added, and only those: the ones before it were aimed already.
        QList<Card> &added = m_imports->m_rows;
        for (Card &card : std::span(added.data() + before, added.size() - before)) {
            // A card refused on sight aims at nothing: its sentence is the whole of it.
            if (card.stage == Stage::Failed)
                continue;
            aim(card.road, target, m_chapterNumbers);
        }
    }
    carrier.describeNext();
    emit m_imports->changed();
}

int ImportOffers::offerUrls(const QList<QUrl> &urls)
{
    const QList<Card> &rows = m_imports->m_rows;
    QStringList paths;
    for (const QUrl &one : urls) {
        // A URL that is not a local file is not a file at all as far as this is concerned.
        if (!one.isLocalFile())
            continue;
        paths << one.toLocalFile();
    }
    const auto before = int(rows.size());
    offer(paths);
    return int(rows.size()) - before;
}

int ImportOffers::reimportUrls(const QString &kind, const QString &id, const QString &name,
                               const QList<QUrl> &urls)
{
    const QList<Card> &rows = m_imports->m_rows;
    QStringList paths;
    for (const QUrl &one : urls) {
        if (one.isLocalFile())
            paths << one.toLocalFile();
    }
    const auto before = int(rows.size());
    reimport(kind, id, name, paths);
    return int(rows.size()) - before;
}

void ImportOffers::wantReimport(const QString &kind, const QString &id, const QString &name,
                                int volumes)
{
    if (!id.isEmpty())
        emit reimportWanted(kind, id, name, volumes);
}

void ImportOffers::aimAtChapterOf(const QString &entryId, const QString &name)
{
    Server *server = m_imports->m_server;
    if (!server || entryId.isEmpty())
        return;
    const QString path = u"/entries/"_s + QString::fromUtf8(QUrl::toPercentEncoding(entryId))
                         + u"/chapters"_s;
    server->get(path, this, [this, name](const Server::Answer &answer) {
        if (!answer.body.isArray())
            return;
        for (const QJsonValue &one : answer.body.array()) {
            const Api::Read<Api::Chapter> read = Api::chapter(one.toObject());
            if (!read.ok() || read.value->kind != Api::Chapter::Kind::Chapter)
                continue;
            if (read.value->number.has_value())
                m_chapterNumbers.insert(read.value->id, *read.value->number);
            emit chapterAimed(read.value->id, name);
            return;
        }
    });
}
