#include "Carrier.h"

#include "Cbz.h"
#include "Facts.h"
#include "Imports.h"
#include "Words.h"

#include <QDir>
#include <QFutureWatcher>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPromise>
#include <QUrlQuery>
#include <QVariantMap>
#include <QtConcurrentRun>

#include <algorithm>
#include <array>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

/// What the announcement says a file is: its number, kind and title as composed, whatever
/// else it declares, and whether the number is only a guess from its name.
QJsonObject foundObject(const Facts::Found &found)
{
    QJsonObject said{{u"kind"_s, found.kind}};
    if (found.number.has_value()) {
        said.insert(u"number"_s, *found.number);
        if (found.guessed)
            said.insert(u"guessed"_s, true);
    }
    if (!found.title.isEmpty())
        said.insert(u"title"_s, found.title);
    if (!found.series.isEmpty())
        said.insert(u"series"_s, found.series);
    if (found.pages >= 0)
        said.insert(u"pages"_s, found.pages);
    if (!found.chapters.isEmpty()) {
        QJsonArray chapters;
        for (const double one : found.chapters)
            chapters << one;
        said.insert(u"chapters"_s, chapters);
    }
    return said;
}

/// The pages of the file that replace the chapter aimed at: the reader's own range, or what
/// the source's `entry.json` gives that chapter, or the whole file until the server has said
/// where the chapter sits.
Facts::ProposedSplice chapterRange(const Card::Road &road, const QString &path, int pages)
{
    const int last = std::max(0, pages - 1);
    if (road.splice)
        return {road.splice->from, road.splice->to, false};
    for (const Facts::Sheet &sheet : road.sheets) {
        if (sheet.at == path)
            return Facts::proposedSplice(sheet, road.chapterNumber, 0, last);
    }
    return {0, last, true};
}

/// The bytes of a sidecar, with the values the reader chose to keep put over the ones the
/// drop brings: what the library's own declaration holds under each such key, and no key at
/// all where it holds none — « non renseigné » is a fact, not an empty value.
QByteArray keptIn(const Card::Road &road, const Manifest::Sidecar &sidecar)
{
    // A series moved because its title matched is moved by its identity: the declaration it
    // travels with names the work the library files it under, which the title alone could
    // not make certain of.
    QByteArray json = sidecar.json;
    if (sidecar.path == u"work.json"_s || sidecar.path.endsWith(u"/work.json"_s)) {
        const QString folder = sidecar.path.left(std::max<qsizetype>(0, sidecar.path.size() - 10));
        for (const Api::Relocation &one : road.moves) {
            if (one.matched == u"TITLE"_s && road.filing.contains(one.workId)
                && one.at == folder.chopped(folder.endsWith(u'/') ? 1 : 0)) {
                QJsonObject object = QJsonDocument::fromJson(json).object();
                object.insert(u"id"_s, one.workId);
                json = QJsonDocument(object).toJson(QJsonDocument::Compact);
            }
        }
    }
    const QString prefix = sidecar.path + u'\n';
    const auto held = road.held.constFind(sidecar.path);
    if (held == road.held.cend())
        return json;
    QJsonObject object = QJsonDocument::fromJson(json).object();
    bool changed = false;
    for (const QString &one : road.keeping) {
        if (!one.startsWith(prefix))
            continue;
        if (const QString key = one.mid(prefix.size()); held->contains(key))
            object.insert(key, held->value(key));
        else
            object.remove(key);
        changed = true;
    }
    return changed ? QJsonDocument(object).toJson(QJsonDocument::Compact) : json;
}

/// One file of a folder as its announcement says it: its size and checksums, what was read of
/// it, the pages it gives a chapter aimed at and the rewrite it asks for.
QJsonObject announcedFile(const Manifest::Entry &entry, const Facts::Composed &composed,
                          const Card::Road &road)
{
    QJsonObject one{{u"path"_s, entry.path}, {u"size"_s, double(entry.size)}};
    if (!entry.checksum.isEmpty())
        one.insert(u"checksum"_s, entry.checksum);
    // What a rewrite or a splice at the other end is checked against; sent whether or not
    // this file will be rewritten, since that is decided there.
    if (!entry.members.digests.isEmpty()) {
        QJsonArray members;
        for (const Cbz::Digest &member : entry.members.digests)
            members.append(QJsonObject{{u"name"_s, member.name},
                                       {u"size"_s, double(member.size)},
                                       {u"checksum"_s, member.checksum}});
        one.insert(u"members"_s, members);
    }
    if (const auto found = composed.found.constFind(entry.path);
        found != composed.found.cend())
        one.insert(u"found"_s, foundObject(*found));
    // The pages of the file that replace the chapter aimed at; the whole file until a
    // reader narrows it.
    if (road.target && road.target->kind == u"CHAPTER"_s) {
        const Facts::ProposedSplice range =
            chapterRange(road, entry.path, composed.found.value(entry.path).pages);
        one.insert(u"splice"_s, QJsonObject{{u"from"_s, range.from}, {u"to"_s, range.to}});
    }
    if (const auto rewrite = composed.rewrites.constFind(entry.path);
        rewrite != composed.rewrites.cend()) {
        QJsonObject asked{{u"entryJson"_s, QString::fromUtf8(rewrite->entryJson)},
                          {u"remove"_s, QJsonArray::fromStringList(rewrite->remove)}};
        if (!rewrite->oneShotJson.isEmpty())
            asked.insert(u"oneShotJson"_s, QString::fromUtf8(rewrite->oneShotJson));
        one.insert(u"rewrite"_s, asked);
    }
    return one;
}

/// The sidecars composed, with the names typed for the editions an import adds written into
/// their `edition.json`, which is where the server reads a name from: a folder with none gets
/// one made.
QList<Manifest::Sidecar> withTypedNames(QList<Manifest::Sidecar> declared,
                                        const QHash<QString, QString> &names)
{
    for (auto typed = names.cbegin(); typed != names.cend(); ++typed) {
        if (typed.key() == u"@current"_s || typed.value().trimmed().isEmpty())
            continue;
        const QString at = typed.key().isEmpty() ? u"edition.json"_s
                                                  : typed.key() + u"/edition.json"_s;
        auto found = std::ranges::find_if(declared, [&at](const Manifest::Sidecar &one) {
            return one.path.compare(at, Qt::CaseInsensitive) == 0;
        });
        if (found == declared.end()) {
            declared.append(Manifest::Sidecar{at, QByteArray("{}")});
            found = std::prev(declared.end());
        }
        QJsonObject object = QJsonDocument::fromJson(found->json).object();
        object.insert(u"name"_s, typed.value().trimmed());
        found->json = QJsonDocument(object).toJson(QJsonDocument::Compact);
    }
    return declared;
}

/// Ticks what `tookFolder` says a card aimed at something starts with.
void tickWhatTheTargetReplaces(Card::Road &road)
{
    for (const Api::VolumeMatch &one : road.volumes) {
        if (one.outcome == Api::Outcome::Replace || one.outcome == Api::Outcome::Update)
            road.replacing.insert(one.path);
    }
    for (const Api::Declaration &one : road.declarations)
        road.declaring.insert(one.path);
}

/// The fields `readSheets` stops asking of a card aimed at something the library holds.
void loosenWhatTheLibraryHolds(QList<Facts::Sheet> &sheets)
{
    for (Facts::Sheet &sheet : sheets) {
        for (Facts::Field &field : sheet.fields) {
            if (field.key == u"authors"_s || field.key == u"artists"_s
                || field.key == u"readingDirection"_s)
                field.weight = Facts::Weight::Optional;
        }
    }
}

} // namespace

namespace {

/// How much of a file goes up in one request.
///
/// Small enough that a progress bar moves and a pause is felt within a second on a slow
/// line; large enough that a two hundred megabyte volume is fifty requests and not five
/// thousand. Nothing is lost when one fails: the next attempt resumes at the byte.
constexpr qint64 ChunkBytes = 4 * 1024 * 1024;

/// How long to wait before trying again, and it grows. The server's own `Retry-After` is
/// already honoured by `Server` for a 429; this is for the cut cable, which nobody times.
int waitFor(int attempts)
{
    static constexpr std::array Steps{2, 5, 10, 30, 60};
    const std::size_t at = qMin(std::size_t(attempts), Steps.size() - 1);
    return Steps[at];
}

/// `Carrier::looked`. A plain count, as `Review::cost` is: `travels` is asked on the thread
/// that draws the frame and the queue, and on no other. It reads `Card::Road::travelling`
/// now and walks no file, so the count stays where it is; a walk put back in `travels` is
/// to count here, and the tests bounding it say so.
constexpr qint64 s_looked = 0;

/// `Card::Road::travelling` from the files the walk found and what the server holds of them,
/// each folder above a file that goes. Once per answer, where `travels` asked it of the
/// whole tree once per container and per rebuild.
void indexTravelling(Card::Road &road)
{
    road.travelling.clear();
    for (const Manifest::Entry &file : std::as_const(road.tree.files)) {
        if (road.alreadyThere.contains(file.path))
            continue;
        QString folder = file.path;
        do {
            const qsizetype slash = folder.lastIndexOf(u'/');
            folder = slash < 0 ? QString() : folder.left(slash);
            // Inserted already, every folder above it was too.
            if (road.travelling.contains(folder))
                break;
            road.travelling.insert(folder);
        } while (!folder.isEmpty());
    }
}

} // namespace

qint64 Cargo::looked()
{
    return s_looked;
}

Carrier::Carrier(Imports *of, Server *server)
    : QObject(of)
    , m_of(of)
    , m_server(server)
{
    // A card that failed is parked at `Paused` with seconds on it, and this is what counts
    // them down. One second, because that is the unit the card shows.
    m_retry.setInterval(1000);
    connect(&m_retry, &QTimer::timeout, this, [this] {
        bool any = false;
        for (int row = 0; row < m_of->m_rows.size(); ++row) {
            if (m_of->m_rows[row].retryIn <= 0)
                continue;
            --m_of->m_rows[row].retryIn;
            // The one role the card reads from this count: the whole `dataChanged` rebuilt the
            // tree of every folder card once a second for as long as it was paused.
            emit m_of->dataChanged(m_of->index(row), m_of->index(row),
                                   {std::to_underlying(Imports::Role::RetryIn)});
            emit m_of->changed();
            if (m_of->m_rows[row].retryIn > 0) {
                any = true;
                continue;
            }
            settle(row, Stage::Ready);
        }
        if (!any)
            m_retry.stop();
        pump();
    });
}

/// The row that holds the one slot, or -1 when nobody does.
int Carrier::holding() const
{
    for (int row = 0; row < m_of->m_rows.size(); ++row) {
        if (const Stage stage = m_of->m_rows.at(row).stage;
            stage == Stage::Sending || stage == Stage::Filing) {
            return row;
        }
    }
    return -1;
}

int Carrier::rowOf(const QString &path) const
{
    for (int row = 0; row < m_of->m_rows.size(); ++row) {
        if (m_of->m_rows.at(row).path == path)
            return row;
    }
    return -1;
}

int Carrier::rowOfToken(quint64 token) const
{
    for (int row = 0; row < m_of->m_rows.size(); ++row) {
        if (m_of->m_rows.at(row).token == token)
            return row;
    }
    return -1;
}

void Carrier::announce(int row)
{
    // A batch of corrections says it once, when it ends (`Imports::edit`).
    if (m_of->m_quiet)
        return;
    if (row >= 0 && row < m_of->m_rows.size()) {
        emit m_of->dataChanged(m_of->index(row), m_of->index(row));
    }
    emit m_of->changed();
}

void Carrier::settle(int row, Stage stage, const QString &trouble)
{
    using enum Card::Stage;

    if (row < 0 || row >= m_of->m_rows.size())
        return;
    m_of->m_rows[row].stage = stage;
    m_of->m_rows[row].trouble = stage == Failed ? Words::answerNotUnderstood(trouble) : trouble;
    // A card that failed is no reading of its drop any more, wherever it failed — a walk, an
    // announcement, a refused drop of its session: nothing can correct it, and left among
    // the drop's cards it refused every role changed beside it and kept the drop for ever.
    // Only a card that leaves one forgets drops: a refused card `addCard` poses had none,
    // and in the middle of a regroup the drop is briefly without cards on purpose.
    if (stage == Failed && std::exchange(m_of->m_rows[row].road.drop, 0) != 0)
        m_of->forgetDropsWithoutCards();
    announce(row);

    // The end of the road, and only the end: a card is settled into `Filing` and `Sending` on
    // its way there, and something that warned at each of those would warn four times about
    // one folder.
    if (stage == Filed || stage == Failed) {
        emit m_of->settled(stage == Filed, m_of->m_rows.at(row).name, trouble);
    }
}

void Carrier::appendRow(const QString &path, const QString &name, bool folder, qint64 size)
{
    Card one;
    one.token = m_nextToken++;
    one.path = path;
    one.name = name;
    one.folder = folder;
    one.size = size;
    m_of->beginInsertRows({}, int(m_of->m_rows.size()), int(m_of->m_rows.size()));
    m_of->m_rows.append(one);
    m_of->endInsertRows();
}

void Carrier::describeNext()
{
    if (m_describing)
        return;
    for (int row = 0; row < m_of->m_rows.size(); ++row) {
        if (m_of->m_rows.at(row).folder && !m_of->m_rows.at(row).road.described) {
            m_describing = true;
            describe(row);
            return;
        }
    }
}

void Carrier::describe(int row)
{
    if (row < 0 || row >= m_of->m_rows.size())
        return;
    const QString path = m_of->m_rows.at(row).path;
    // Carried into every callback below instead of `path`: a folder abandoned and
    // redropped while this very walk is still on the pool gives the new row the old
    // row's path, and a lookup by path would hand this walk's answer to a folder it was
    // never reading. `token` cannot collide — `m_nextToken` only grows.
    const quint64 token = m_of->m_rows.at(row).token;
    const bool verifying = m_of->m_verifying;

    // Parented, so shutting the application down takes the watcher with it. The walk itself
    // carries on for as long as it takes and touches nothing here — it has a copy of the
    // path and nothing else — and its answer is simply never delivered.
    auto *watcher = new QFutureWatcher<Manifest::Folder>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, token] {
        watcher->deleteLater();
        described(token, watcher->result());
    });

    // In both modes, and not only when hashing: the walk used to skip its count without a
    // checksum because it was then a `stat` of every file, over before a second report could
    // matter. It now opens every archive to read what it says about itself — the checking
    // step's whole source — which on a real series is long enough to look stuck without one.
    m_of->m_rows[row].road.hashing = verifying;
    m_of->m_walkingToken = token;
    // `progressValueChanged` is `QFutureWatcherBase`'s own signal, marshaled onto this
    // object's thread exactly the way `finished` above already is — the pool thread only
    // ever writes into the `QPromise`, which is Qt's own thread-safe channel for exactly
    // this, and never touches `this`. That is what let the previous round's raw
    // `QMetaObject::invokeMethod` from the pool crash the suite the moment a test deleted
    // this object while a walk was still running, and what let the destructor written to
    // stop it block this thread for the rest of that walk instead — the freeze this commit
    // exists to remove, moved onto the way out rather than gone. Nothing here needs either:
    // the connection made below dies with `watcher`, a child of `this`, the ordinary way
    // any other signal connection would.
    connect(watcher, &QFutureWatcherBase::progressValueChanged, this,
            [this, token](int done) { hashedSoFar(token, done); });

    const qint64 total = m_of->m_rows.at(row).road.node.volumes();
    watcher->setFuture(QtConcurrent::run(
        [path, total, verifying](QPromise<Manifest::Folder> &promise) {
            promise.setProgressRange(0, int(total));
            promise.addResult(QFileInfo(path).isFile()
                                  ? Manifest::single(path, verifying)
                                  : Manifest::of(path, verifying, [&promise](qint64 done) {
                                        promise.setProgressValue(int(done));
                                    }));
        }));
}

void Carrier::hashedSoFar(quint64 token, qint64 done)
{
    // By token and not by path — see `describe()` — for the same reason `described()`
    // looks up by token: the row may have been abandoned, or replaced by a redrop sharing
    // its old path, while the walk it came from was still running.
    const int at = rowOfToken(token);
    if (at < 0)
        return;
    m_of->m_rows[at].road.hashed = done;
    // Only the one role, not `announce()`'s full `dataChanged`: that would invalidate every
    // role the card reads, and `Role::Nodes` rebuilds the whole flattened tree from scratch
    // — turning the count that was meant to prove the wait is alive into rebuilding an
    // unfolded tree once per volume, quadratic in exactly the folder size this exists to
    // reassure about.
    emit m_of->dataChanged(m_of->index(at), m_of->index(at),
                           {std::to_underlying(Imports::Role::Checking)});
}

void Carrier::described(quint64 token, const Manifest::Folder &tree)
{
    // Cleared here rather than left for the next `describe()` to overwrite: between this
    // walk ending and the next one starting, nothing is on the pool, and the model's
    // `m_walkingToken` saying otherwise would show a stale count against whichever row
    // happens to be walking next.
    if (m_of->m_walkingToken == token)
        m_of->m_walkingToken = 0;

    // By token, not by path: rows can be abandoned while a folder is being read, and a
    // folder redropped at the same path while that walk is still running would otherwise
    // receive this answer instead of the one it is actually waiting on.
    const int at = rowOfToken(token);
    if (at < 0) {
        m_describing = false;
        describeNext();
        return;
    }
    // Whatever the walk found, it is over: from here the card is waiting on the server and
    // not on the disk, and until this flag existed nothing on screen said so.
    m_of->m_rows[at].road.checked = true;
    m_of->m_rows[at].road.described = true;
    if (!tree.read()) {
        settle(at, Stage::Failed, tree.trouble);
    } else {
        m_of->m_rows[at].road.tree = tree;
        // Before the server answers nothing is held, and every folder holding a file goes.
        indexTravelling(m_of->m_rows[at].road);
        m_of->m_rows[at].size = tree.bytes();
        // Read here, from the node the shape gave the card and the walk that just opened every
        // archive: the announcement composes from it, and the checking step shows it.
        readSheets(m_of->m_rows[at]);
        // Said now, not at the server's answer: the frame opened while fifty folders are read
        // takes each one into its tree and its count as soon as it is walked.
        announce(at);
        announceFolder(at, tree);
    }
    // The next one in the queue, and not before: two disk walks at once would get in each
    // other's way, and the first card would be ready no sooner for it.
    m_describing = false;
    describeNext();
}

void Carrier::announceFolder(int row, const Manifest::Folder &tree)
{
    if (!m_server || row < 0 || row >= m_of->m_rows.size())
        return;

    // What was read, composed, and not the sidecars as the disk holds them: a Komga folder
    // announced with its own bytes alone reached the server with no `work.json` and no
    // `entry.json`, and was filed under its folder's name with no numbers at all. The
    // archives are not touched here — the server rewrites them as they are installed.
    const Facts::Composed composed =
        Facts::compose(m_of->m_rows.at(row).road.sheets, tree.sidecars);
    QJsonArray files;
    for (const Manifest::Entry &entry : tree.files) {
        // An archive no one will read stays here: installed, it would sit on the library's
        // disk in `disregarded`, where nothing ever reads it again.
        if (composed.leftOut.contains(entry.path))
            continue;
        files.append(announcedFile(entry, composed, m_of->m_rows.at(row).road));
    }
    // The series' cover travels with its volumes: path, size and checksum, nothing a volume
    // would be asked.
    // The one a folder holds gives way to the one the reader chose for that folder: two
    // covers in a folder would leave the server to pick between them.
    const auto announcedCover = [&files](const Manifest::Entry &cover) {
        QJsonObject one{{u"path"_s, cover.path}, {u"size"_s, double(cover.size)}};
        if (!cover.checksum.isEmpty())
            one.insert(u"checksum"_s, cover.checksum);
        files.append(one);
    };
    const Card::Road &chosen = m_of->m_rows.at(row).road;
    for (const Manifest::Entry &cover : tree.covers) {
        const qsizetype slash = cover.path.lastIndexOf(u'/');
        if (!chosen.picked.contains(slash < 0 ? QString() : cover.path.left(slash)))
            announcedCover(cover);
    }
    for (const Card::CoverPick &one : chosen.picked)
        announcedCover(one.entry);
    m_of->m_rows[row].road.coversSent = m_of->m_rows.at(row).coversSaid();
    QJsonArray sidecars;
    const Card::Road &aimed = m_of->m_rows.at(row).road;
    // The names typed for the editions an import adds are written into their `edition.json`,
    // which is where the server reads a name from: a folder with none gets one made.
    m_of->m_rows[row].road.namesSent = aimed.editionNames;
    const QList<Manifest::Sidecar> declared = withTypedNames(composed.sidecars, aimed.editionNames);
    for (const Manifest::Sidecar &one : declared) {
        // A declaration the reader did not write, at the root of a drop laid on a volume or
        // an edition, would be installed over the target's own: the server refuses one that
        // is not of the target's kind, and what was only guessed from a name is not worth
        // that refusal.
        // Except the `edition.json` the reader's typed names made: loose files have no folder
        // to hold it, and it is exactly what the server asked for.
        const bool typed = one.path == u"edition.json"_s && aimed.editionNames.contains(QString())
                           && !aimed.editionNames.value(QString()).trimmed().isEmpty();
        // New editions are editions: the server refuses a work or a universe among them, and
        // a one the reader never wrote is only the client's reading of the folder.
        if (const bool ofAnotherLevel =
                aimed.target && aimed.target->kind == u"SERIES_NEW_EDITIONS"_s
                && (one.path == u"work.json"_s || one.path.endsWith(u"/work.json"_s)
                    || one.path == u"universe.json"_s || one.path.endsWith(u"/universe.json"_s))
                && std::ranges::none_of(tree.sidecars, [&](const Manifest::Sidecar &said) {
                       return said.path == one.path;
                   });
            ofAnotherLevel)
            continue;
        if (aimed.target && aimed.target->kind != u"UNIVERSE"_s && !one.path.contains(u'/')
            && !typed
            && std::ranges::none_of(tree.sidecars, [&](const Manifest::Sidecar &said) {
                   return said.path == one.path;
               }))
            continue;
        sidecars.append(QJsonObject{{u"path"_s, one.path},
                                    {u"json"_s, QString::fromUtf8(keptIn(aimed, one))}});
    }

    // Always ADDITION: the scope question made sense only at the level that holds volumes,
    // and asking it of every edition of a ten-series universe was ten questions for a claim
    // one is rarely in a position to make. Naming a folder COMPLETE is a capability the
    // server keeps — nothing on this side asks for it any more.
    QJsonObject body{{u"root"_s, tree.root},
                     {u"files"_s, files},
                     {u"sidecars"_s, sidecars},
                     {u"scope"_s, u"ADDITION"_s}};
    if (const QString current = aimed.editionNames.value(u"@current"_s).trimmed();
        !current.isEmpty())
        body.insert(u"currentEditionName"_s, current);
    if (aimed.target) {
        body.insert(u"target"_s, QJsonObject{{u"kind"_s, aimed.target->kind},
                                             {u"id"_s, aimed.target->id}});
    }
    const QString path = m_of->m_rows.at(row).path;
    m_server->post(u"/import"_s, QJsonDocument(body).toJson(QJsonDocument::Compact), this,
                   [this, path](const Server::Answer &answer) { tookFolder(path, answer); });
}

namespace {

/// What the server's answer says of the road a folder travels, copied onto it: the steps of
/// `tookFolder` read as steps, and none of the fields they are made of.
void takeRoadFrom(Card::Road &road, const Api::Opened &opened)
{
    road.toSend = opened.toSend;
    road.replaces = opened.replaces;
    // Cleared with the announcement for `filing`'s own reason: a re-announced folder may
    // answer differently, and a tick against a volume it no longer names would overwrite
    // something for a reason nobody could see.
    road.replacing.clear();
    road.declarations = opened.declarations;
    road.declaring.clear();
    road.alreadyThere = QSet<QString>(opened.alreadyThere.cbegin(), opened.alreadyThere.cend());
    indexTravelling(road);
    road.creates = opened.creates;
    road.moves = opened.moves;
    road.volumes = opened.volumes;
    road.absent = opened.absent;
    road.absentSeries = opened.absentSeries;
    road.resembles = opened.resembles;
    road.landsAs = opened.landsAs;
    road.collisions = opened.collisions;
    road.reshelved = opened.reshelved;
    road.editionsNeedingAName = opened.editionsNeedingAName;
    for (const Api::NeedsAName &one : road.editionsNeedingAName) {
        if (!road.editionOrder.contains(one.path))
            road.editionOrder << one.path;
    }
    road.problems = opened.problems;
}

/// A chapter the source did not place was announced at the pages of the whole file; now that
/// the server has said where the chapter sits, the proposal is its own pages, and the answer
/// is asked for again once with them. True when it was, and the folder must be announced again.
bool proposedItsOwnPages(Card::Road &answered)
{
    if (!answered.target || answered.target->kind != u"CHAPTER"_s || answered.splice
        || !answered.spliced || answered.sheets.isEmpty())
        return false;
    const Facts::Sheet &source = answered.sheets.constFirst();
    const Facts::ProposedSplice proposal = chapterRange(answered, source.at, source.contents.pages);
    const auto aimed = std::ranges::find(answered.spliced->chapters, u"REPLACE"_s,
                                         &Api::ChapterSplice::outcome);
    if (!proposal.guessed || aimed == answered.spliced->chapters.cend())
        return false;
    answered.splice = Card::Road::Splice{aimed->from, aimed->to};
    return true;
}

} // namespace

void Carrier::tookFolder(const QString &path, const Server::Answer &answer)
{
    const int row = rowOf(path);
    if (row < 0)
        return;
    if (!answer.went()) {
        settle(row, Stage::Failed, answer.trouble);
        return;
    }
    const Api::Read<Api::Opened> read = Api::opened(answer.body.object());
    if (!read.ok()) {
        settle(row, Stage::Failed, read.trouble);
        return;
    }

    m_of->m_rows[row].id = read.value->id;
    m_of->remember(read.value->id);
    Card::Road &answered = m_of->m_rows[row].road;
    takeRoadFrom(answered, *read.value);
    // A name typed while this answer was awaited is not in it: the folder is announced again
    // with it, and what was typed is not lost.
    if (answered.editionNames != answered.namesSent
        || m_of->m_rows.at(row).coversSaid() != answered.coversSent) {
        reannounce(row);
        return;
    }
    answered.place = read.value->target;
    answered.spliced = read.value->splice;
    if (proposedItsOwnPages(answered)) {
        reannounce(row);
        return;
    }
    for (const Api::Declaration &one : read.value->declarations)
        answered.held.insert(one.path, one.present);
    // Aimed at something, a replacement is what was asked for: the volumes it matched and
    // the declarations that differ start ticked, and a series that is another never does.
    // A drop that aims at nothing ticks nothing — dropping a folder again is not a request
    // to overwrite what the library holds.
    if (answered.target)
        tickWhatTheTargetReplaces(answered);
    // Cleared with the announcement, never carried across one: a re-announced folder may
    // answer differently, and a tick against a work it no longer names would move it for
    // no reason a reader could see.
    // Kept for the moves the new answer still names: a move matched by title is announced
    // again once ticked, to carry the identity it is made by, and that answer must not undo
    // the tick it was asked for. One it no longer names is dropped.
    m_of->m_rows[row].road.filing.removeIf([&answered, &read](const QString &workId) {
        return !answered.target
               || std::ranges::none_of(read.value->moves, [&workId](const Api::Relocation &one) {
                      return one.workId == workId;
                  });
    });
    m_of->m_rows[row].road.at = 0;
    m_of->m_rows[row].road.sentInFile = 0;
    // What is already there costs nothing and is not sent again, so the bar measures what
    // will actually travel rather than what the folder weighs.
    m_of->m_rows[row].size = read.value->bytesToSend;

    Card &card = m_of->m_rows[row];
    // Stopped on what was guessed or is missing — unless « Importer » was already pressed
    // for this card, whose re-announcement only carries what was corrected. And only on
    // what will travel: a series the library holds whole sends nothing and writes nothing,
    // and checking values that go nowhere is a question with no consequence.
    if (!card.road.confirmed && (card.road.target || asksToBeChecked(card))) {
        settle(row, Stage::Reviewing);
        return;
    }
    // A creation no longer stops a card. It did, and a folder whose every value was said
    // still waited the moment its series was new — which every series is, once. The line
    // « sera créée » stays on the card, read before « Importer », and that press is the
    // yes. A series that would move is one its reader would have to find again: that one
    // still waits for its tick.
    const bool ready = read.value->moves.isEmpty() || card.road.target.has_value();
    // Read after « Importer » was pressed, complete, it goes in its turn: at fifty series
    // the walk outlasts the press, and the series read after it waited for a second one
    // nobody knew to make. What would do nothing is let go as the press lets go of it.
    if (ready && card.road.awaited && !card.road.confirmed) {
        if (card.nothingToDo()) {
            abandon(row);
            m_of->forgetDropsWithoutCards();
            return;
        }
        card.road.confirmed = true;
        card.road.released = true;
    }
    settle(row, ready ? Stage::Ready : Stage::Deciding);
    if (card.road.released)
        pump();
}

bool Cargo::travels(const Card &card, const Facts::Sheet &sheet)
{
    const QSet<QString> &held = card.road.alreadyThere;
    // A book is a file: read as a container, it held no file under its own path and was
    // « identical » in the frame — and held nothing back — while it was on its way.
    if (sheet.level == Manifest::Level::Volume || sheet.level == Manifest::Level::Chapter
        || sheet.level == Manifest::Level::Book)
        return !held.contains(sheet.at);
    // A container travels when one file under it does, and when its own sidecar is written
    // even with every file already there: the server creates it, or a reader ticked the
    // declaration that differs from the library's. Read without that, a series held whole
    // that the server would create was « nothing to import » in the frame and let go at
    // « Importer ».
    //
    // Its own, and not one beneath it: an edition created under a series held whole writes
    // `edition.json`, and the series' sheet stopped the card over a `work.json` nobody
    // writes. And ticked only: the boxes start empty and an unticked declaration is never
    // written, so a series held whole whose `work.json` differs stopped at « À vérifier »
    // over values that go nowhere.
    const auto folderOf = [](const QString &path) {
        const qsizetype slash = path.lastIndexOf(u'/');
        return slash < 0 ? QString() : path.left(slash);
    };
    return card.road.travelling.contains(sheet.at)
           || std::ranges::any_of(card.road.creates,
                                  [&](const Api::Creation &one) { return one.at == sheet.at; })
           || std::ranges::any_of(card.road.declarations, [&](const Api::Declaration &one) {
                  return folderOf(one.path) == sheet.at && card.road.declaring.contains(one.path);
              });
}

bool Cargo::startedSending(const Card &card)
{
    return card.sent > 0 || card.road.at > 0 || card.road.sentInFile > 0;
}

bool Cargo::heldBack(const Card &card)
{
    // A name the server asked for and has not been given holds the card back as a missing
    // required field does: it would refuse the commit, nothing installed.
    return !card.road.problems.isEmpty()
           || std::ranges::any_of(card.road.sheets, [&card](const Facts::Sheet &sheet) {
                  return travels(card, sheet) && Facts::blocks(sheet);
              });
}

bool Cargo::correctable(const Card &card)
{
    using enum Card::Stage;
    if (card.stage == Ready && card.road.confirmed)
        return false;
    return (card.stage == Reviewing || card.stage == Deciding || card.stage == Ready)
           && !startedSending(card);
}

void Cargo::readSheets(Card &card)
{
    Card::Road &road = card.road;
    road.sheets = Facts::read(road.node, road.tree, road.roles);
    // A series named « Vrac » has no title worth guessing: missing, it holds the card back
    // (`Facts::blocks`) until a name is typed, which the corrections below put back.
    for (Facts::Sheet &sheet : road.sheets) {
        if (sheet.level != Manifest::Level::Work || !road.untitled.contains(sheet.at))
            continue;
        for (Facts::Field &field : sheet.fields) {
            if (field.key == u"title"_s && field.origin == Facts::Origin::Guessed) {
                field.value = QJsonValue();
                field.origin = Facts::Origin::Missing;
            }
        }
    }
    // A card aimed at something the library holds asks nothing of who wrote and drew the
    // series and which way it reads: the library has them, and a file replacing a volume of
    // Dragon Ball was held back for a writer the series has had since it was imported. A
    // universe aimed at holds no series' fields, so what lies in it is asked as ever.
    if (road.target && road.target->kind != u"UNIVERSE"_s)
        loosenWhatTheLibraryHolds(road.sheets);
    // A correction whose field the new reading no longer has is forgotten rather than kept
    // for later: kept, a title typed on a series turned universe would come back on the day
    // it became a series again, a value nobody could see waiting.
    QList<Card::Road::Correction> kept;
    for (const Card::Road::Correction &one : road.corrections) {
        if (Facts::edit(road.sheets, one.at, one.key, one.value))
            kept << one;
    }
    road.corrections = kept;
}

bool Cargo::asksToBeChecked(const Card &card)
{
    // A series that looks like one held is asked about, unless a second one was chosen.
    if (!card.road.target
        && std::ranges::any_of(card.road.resembles, [&](const Api::Resemblance &one) {
               return !card.road.second.contains(one.at);
           }))
        return true;
    return std::ranges::any_of(card.road.sheets, [&](const Facts::Sheet &sheet) {
        return travels(card, sheet) && Facts::needsReview(sheet);
    });
}

void Carrier::reannounce(int row)
{
    if (!m_server || row < 0 || row >= m_of->m_rows.size())
        return;
    Card &card = m_of->m_rows[row];
    // The server remembers the rewrites a session was announced with, and a correction is
    // a different rewrite: the first session goes, and the folder is announced again. The
    // id is let go as the request leaves, so an abandon in between does not drop the same
    // session twice and come back failed over a 404.
    // Kept whole, id included, for the one path where the server refuses to drop it: the
    // failed row that comes back still names the session, so abandoning it asks again.
    const Card kept = card;
    const QString id = std::exchange(card.id, QString());
    const quint64 token = card.token;
    card.road.edited = false;
    settle(row, Stage::Asking);
    m_server->remove(u"/import/"_s + id, this, [this, token, kept](const Server::Answer &answer) {
        const int at = rowOfToken(token);
        if (answer.went()) {
            m_of->forget(kept.id);
            if (at >= 0)
                announceFolder(at, m_of->m_rows.at(at).road.tree);
            return;
        }
        // Refused, as `abandon` reads a refusal: this card's own fact, never the queue's.
        // Abandoned meanwhile, the row is gone and `abandon` asked nothing — the id had
        // already been let go — so the refusal would otherwise be said to nobody.
        if (at < 0) {
            cameBack(kept);
            return;
        }
        m_of->m_rows[at].id = kept.id;
        settle(at, Stage::Failed, Words::couldNotCleanUp(kept.name));
    });
}

void Carrier::ask(int row)
{
    if (!m_server || row < 0 || row >= m_of->m_rows.size())
        return;
    const Card &one = m_of->m_rows.at(row);

    // Read here and not on a thread: the catalogue of a zip is a seek and a few kilobytes,
    // and a file that cannot be opened is answered by `Cbz` rather than by an exception.
    const Cbz::Found found = Cbz::sidecarOf(one.path);
    QJsonObject body{{u"name"_s, one.name}, {u"size"_s, double(one.size)}};
    if (!found.sidecar.isEmpty())
        body.insert(u"sidecar"_s, QString::fromUtf8(found.sidecar));

    const QString path = one.path;
    m_server->post(u"/preflight"_s, QJsonDocument(body).toJson(QJsonDocument::Compact), this,
                   [this, path](const Server::Answer &answer) { took(path, answer); });
}

void Carrier::took(const QString &path, const Server::Answer &answer)
{
    const int row = rowOf(path);
    if (row < 0)
        return;

    if (!answer.went()) {
        settle(row, Stage::Failed, answer.trouble);
        return;
    }
    const Api::Read<Api::Reserved> read = Api::reserved(answer.body.object());
    if (!read.ok()) {
        settle(row, Stage::Failed, read.trouble);
        return;
    }

    m_of->m_rows[row].id = read.value->id;
    m_of->m_rows[row].proposal = read.value->proposal;

    // Sure of itself and one series named: there is nothing to ask, and asking anyway would
    // be fifty clicks for a shelf that was never in doubt.
    if (const Api::Proposal &said = read.value->proposal;
        said.confidence == Api::Proposal::Confidence::Certain && !said.candidates.isEmpty()) {
        m_of->m_rows[row].chosen = said.candidates.constFirst().seriesId;
        settle(row, Stage::Ready);
        return;
    }
    settle(row, Stage::Deciding);
}

void Carrier::pump(int first)
{
    if (!m_server || !m_of->m_trouble.isEmpty() || holding() >= 0)
        return;

    // Only what its reader let go. The third step lists every card before any moves, and a
    // card the reader did not press is waiting for its button: the queue used to take any
    // card that was ready the moment the slot was free, so the line of a series nobody had
    // sent started when the one before it ended.
    const auto goes = [this](int row) {
        const Card &card = m_of->m_rows.at(row);
        return card.stage == Stage::Ready && card.road.released
               && (card.folder || !card.chosen.isEmpty());
    };
    int taken = -1;
    if (first >= 0 && first < m_of->m_rows.size() && goes(first)) {
        taken = first;
    } else {
        for (int row = 0; row < m_of->m_rows.size() && taken < 0; ++row) {
            // A folder has no series to choose: it declares one. Asking it for a `chosen`
            // before letting it go left it Ready for ever, which looks exactly like a queue
            // that decided not to bother.
            if (goes(row))
                taken = row;
        }
    }
    if (taken < 0)
        return;
    // Left where it is. Putting the one in flight on top meant the list rearranged itself
    // under the reader's eyes every time a transfer ended — a card they were looking at
    // moved because a different one finished, and the order they dropped the folders in was
    // gone. The badge says which one is going; the list does not have to.
    m_of->m_rows[taken].stage = Stage::Sending;
    announce(taken);
    sendMore(taken);
}

void Carrier::release(int row, bool now)
{
    if (row < 0 || row >= m_of->m_rows.size())
        return;
    using enum Card::Stage;
    if (Card &card = m_of->m_rows[row]; card.stage == Paused || card.stage == Failed) {
        card.retryIn = 0;
        card.attempts = 0;
        settle(row, Ready);
    }
    if (m_of->m_rows.at(row).stage != Ready)
        return;
    m_of->m_rows[row].road.released = true;
    // Asked to go now, a card in flight steps behind and waits its turn: it was not stopped by
    // anyone, so it is not paused, and nothing about it needs its reader a second time.
    if (const int held = holding(); now && held >= 0 && held != row) {
        m_of->m_rows[held].stage = Ready;
        announce(held);
    }
    pump(now ? row : -1);
}

void Carrier::restart(int row)
{
    if (!m_server || row < 0 || row >= m_of->m_rows.size())
        return;
    Card &card = m_of->m_rows[row];
    const bool wasHolding = holding() == row;
    // Back to nothing sent: what the server received of this card goes with its session. Kept
    // whole, as `reannounce` keeps it, for the one path where the server refuses to drop it.
    const Card kept = card;
    const QString id = std::exchange(card.id, QString());
    const quint64 token = card.token;
    card.sent = 0;
    card.road.at = 0;
    card.road.sentInFile = 0;
    card.road.released = false;
    card.road.edited = false;
    card.retryIn = 0;
    card.attempts = 0;
    // Asked of the server again, as a corrected card is: the answer decides what is left to
    // send, and a card whose bytes were all there may have nothing left. The reader's
    // agreement stays (`confirmed`), so the new answer does not stop on what was checked.
    settle(row, Stage::Asking);
    const QString route = (kept.folder ? u"/import/"_s : u"/intake/"_s) + id;
    m_server->remove(route, this, [this, token, kept](const Server::Answer &answer) {
        const int at = rowOfToken(token);
        if (answer.went()) {
            m_of->forget(kept.id);
            if (at < 0)
                return;
            if (kept.folder)
                announceFolder(at, m_of->m_rows.at(at).road.tree);
            else
                ask(at);
            return;
        }
        if (at < 0) {
            cameBack(kept);
            return;
        }
        m_of->m_rows[at].id = kept.id;
        settle(at, Stage::Failed, Words::couldNotCleanUp(kept.name));
    });
    if (wasHolding)
        pump();
}

void Carrier::sendMore(int row)
{
    if (row < 0 || row >= m_of->m_rows.size())
        return;
    if (m_of->m_rows.at(row).folder) {
        sendMoreOfFolder(row);
        return;
    }
    const Card &one = m_of->m_rows.at(row);

    if (one.sent >= one.size) {
        confirm(row);
        return;
    }

    QFile file(one.path);
    if (!file.open(QIODevice::ReadOnly) || !file.seek(one.sent)) {
        settle(row, Stage::Failed, Words::couldNotBeRead(one.name));
        pump();
        return;
    }
    const QByteArray chunk = file.read(qMin(ChunkBytes, one.size - one.sent));
    const QString path = one.path;
    const qint64 from = one.sent;
    const qint64 whole = one.size;

    m_server->put(u"/intake/"_s + one.id + u"/file"_s, chunk, from, whole, this,
                  [this, path, chunk](const Server::Answer &answer) {
                      const int at = rowOf(path);
                      if (at < 0)
                          return;
                      // Stepped aside, or thrown out, while these bytes were in the air.
                      // Whatever arrived is kept — the next attempt resumes past it.
                      if (m_of->m_rows.at(at).stage != Stage::Sending)
                          return;
                      if (!answer.went()) {
                          retryLater(at, answer);
                          return;
                      }
                      m_of->m_rows[at].attempts = 0;
                      m_of->m_rows[at].sent += chunk.size();
                      announce(at);
                      sendMore(at);
                  });
}

void Carrier::sendMoreOfFolder(int row)
{
    if (row < 0 || row >= m_of->m_rows.size())
        return;
    const Card &one = m_of->m_rows.at(row);

    if (one.road.at >= one.road.toSend.size()) {
        commitFolder(row);
        return;
    }
    const QString relative = one.road.toSend.at(one.road.at);
    const qint64 whole = one.sizeOf(relative);
    // What the server asks for is read from the disk, so it must be what the walk found:
    // a path it names that was never in the folder — `../`, an absolute one — is not read.
    if (!one.walked(relative)) {
        settle(row, Stage::Failed, Words::couldNotBeRead(relative));
        pump();
        return;
    }

    // The folder the paths are relative to: a file dropped on a volume is its own tree
    // (`Manifest::single`), and its card's path is the file, which has nothing under it.
    const QFileInfo about(one.path);
    // An image the reader chose is read from where it is, which is not in the folder.
    const QString chosenAt = one.pickedSource(relative);
    const QString folder = about.isFile() ? about.absolutePath() : one.path;
    QFile file(!chosenAt.isEmpty() ? chosenAt : folder + u'/' + relative);
    if (!file.open(QIODevice::ReadOnly) || !file.seek(one.road.sentInFile)) {
        settle(row, Stage::Failed, Words::couldNotBeRead(relative));
        pump();
        return;
    }
    const QByteArray chunk = file.read(qMin(ChunkBytes, whole - one.road.sentInFile));
    if (chunk.isEmpty()) {
        // Nothing left of this one. On to the next, without a request that would carry no
        // bytes and no range.
        m_of->m_rows[row].road.at += 1;
        m_of->m_rows[row].road.sentInFile = 0;
        sendMoreOfFolder(row);
        return;
    }

    // From the moment a byte leaves, the session may hold some: an application quit during
    // the first request would otherwise find the session kept as empty at its next start and
    // delete a volume that had half arrived. A session that holds bytes is worth what it
    // holds and is never dropped on start.
    m_of->forget(one.id);

    const QString path = one.path;
    const qint64 from = one.road.sentInFile;
    QUrlQuery query;
    query.addQueryItem(u"path"_s, relative);
    m_server->put(u"/import/"_s + one.id + u"/file"_s, query, chunk, from, whole, this,
                  [this, path, went = chunk.size(), whole](const Server::Answer &answer) {
                      const int at = rowOf(path);
                      if (at < 0 || m_of->m_rows.at(at).stage != Stage::Sending)
                          return;
                      // The one answer that is not a failure and not a success: the server
                      // holds less than this client believed. It says how much, and the
                      // next attempt starts exactly there rather than at the beginning.
                      if (answer.status == 409) {
                          resumeFrom(at);
                          return;
                      }
                      if (!answer.went()) {
                          retryLater(at, answer);
                          return;
                      }
                      chunkLanded(at, went, whole);
                      sendMoreOfFolder(at);
                  });
}

void Carrier::chunkLanded(int row, qint64 went, qint64 whole)
{
    m_of->m_rows[row].attempts = 0;
    m_of->m_rows[row].sent += went;
    m_of->m_rows[row].road.sentInFile += went;
    // Only the roles this chunk actually changed, not `announce()`'s full `dataChanged` —
    // the same fix `2614544` made for the hash tick, left on this path: a 7.9 GiB folder is
    // about two thousand chunks, and an unscoped signal here invalidated `Role::Nodes` on
    // every one of them, rebuilding the whole flattened tree — `node.volumes()` walked
    // afresh per node — and handing the `Repeater` a new `QVariantList` that destroyed and
    // recreated every `ImportNode`, every chevron `Canvas`, every level `Image`, once per
    // chunk rather than once per volume.
    QList roles{std::to_underlying(Imports::Role::Sent)};
    if (m_of->m_rows.at(row).road.sentInFile >= whole) {
        m_of->m_rows[row].road.at += 1;
        m_of->m_rows[row].road.sentInFile = 0;
        // The tree's own state moves here, not on every chunk: `stateOfVolume` reads
        // `Road::at` to say which node is « envoi » rather than « à envoyer » or « rangé »,
        // and that only changes once a whole file is done.
        roles.append(std::to_underlying(Imports::Role::Nodes));
    }
    emit m_of->dataChanged(m_of->index(row), m_of->index(row), roles);
}

void Carrier::resumeFrom(int row)
{
    if (row < 0 || row >= m_of->m_rows.size())
        return;
    const QString path = m_of->m_rows.at(row).path;
    m_server->get(u"/import/"_s + m_of->m_rows.at(row).id, this,
                  [this, path](const Server::Answer &answer) {
                      const int at = rowOf(path);
                      if (at < 0)
                          return;
                      const Api::Read<Api::Session> read = Api::session(answer.body.object());
                      if (!answer.went() || !read.ok()) {
                          retryLater(at, answer);
                          return;
                      }
                      if (const Card &one = m_of->m_rows.at(at);
                          one.road.at < one.road.toSend.size()) {
                          m_of->m_rows[at].road.sentInFile =
                              read.value->received.value(one.road.toSend.at(one.road.at), 0);
                      }
                      announce(at);
                      sendMoreOfFolder(at);
                  });
}

void Carrier::commitFolder(int row)
{
    if (row < 0 || row >= m_of->m_rows.size())
        return;
    m_of->m_rows[row].stage = Stage::Filing;
    announce(row);

    const QString path = m_of->m_rows.at(row).path;
    // The answers travel with the commit rather than as a call of their own: the folder is
    // filed under the one that is being installed, and until that install runs there is no
    // "here" to file anything under.
    QJsonArray filing;
    for (const Api::Relocation &one : m_of->m_rows.at(row).road.moves) {
        if (m_of->m_rows.at(row).road.filing.contains(one.workId))
            filing << one.workId;
    }
    // And what may land on something already there. Only what was ticked: the server
    // installs over nothing it was not told to, and a path left out comes back in `pending`
    // rather than being quietly skipped.
    QJsonArray replacing;
    for (const QString &landing : m_of->m_rows.at(row).road.replacing)
        replacing << landing;
    QJsonArray declaring;
    for (const QString &said : m_of->m_rows.at(row).road.declaring)
        declaring << said;
    QJsonObject asked{{u"move"_s, filing}, {u"replace"_s, replacing}, {u"declare"_s, declaring}};
    // Only when somebody asked: a place started again is a decision about one profile, and
    // an empty list would say the commit had been asked something.
    if (const auto &starting = m_of->m_rows.at(row).road.resetting; !starting.isEmpty()) {
        QJsonArray starts;
        for (const QString &one : starting)
            starts << one;
        asked.insert(u"reset"_s, starts);
    }
    const QByteArray body = QJsonDocument(asked).toJson(QJsonDocument::Compact);
    m_server->post(u"/import/"_s + m_of->m_rows.at(row).id + u"/commit"_s, body, this,
                   [this, path](const Server::Answer &answer) {
                       committed(rowOf(path), answer);
                   });
}

void Carrier::committed(int row, const Server::Answer &answer)
{
    if (row < 0 || row >= m_of->m_rows.size())
        return;
    if (!answer.went()) {
        settle(row, Stage::Failed, answer.trouble);
        pump();
        return;
    }
    const Api::Read<Api::Installed> read = Api::installed(answer.body.object());
    if (!read.ok()) {
        settle(row, Stage::Failed, read.trouble);
        pump();
        return;
    }
    m_of->m_rows[row].road.replaced = read.value->replaced;
    m_of->forget(m_of->m_rows.at(row).id);
    // A commit that could not install everything is not a failure and does not lose the
    // session: what is still coming is said out loud, and the row keeps the id it would be
    // sent against.
    settle(row, Stage::Filed,
           Words::whatLanded(read.value->installed, int(read.value->pending.size()),
                             int(read.value->corrupt.size()), int(read.value->orphans.size())));
    pump();
}

void Carrier::confirm(int row)
{
    if (row < 0 || row >= m_of->m_rows.size())
        return;
    m_of->m_rows[row].stage = Stage::Filing;
    announce(row);

    const QString path = m_of->m_rows.at(row).path;
    const QJsonObject body{{u"seriesId"_s, m_of->m_rows.at(row).chosen}};
    m_server->post(u"/intake/"_s + m_of->m_rows.at(row).id + u"/file"_s,
                   QJsonDocument(body).toJson(QJsonDocument::Compact), this,
                   [this, path](const Server::Answer &answer) {
                       const int at = rowOf(path);
                       if (at < 0)
                           return;
                       if (!answer.went()) {
                           settle(at, Stage::Failed, answer.trouble);
                           pump();
                           return;
                       }
                       settle(at, Stage::Filed);
                       pump();
                   });
}

void Carrier::retryLater(int row, const Server::Answer &answer)
{
    using enum Stage;
    if (row < 0 || row >= m_of->m_rows.size())
        return;
    // The key was refused, or lacks the right to import: that is not this file's fault and
    // not something waiting will mend. Said once, and the queue stops. A missing right no
    // longer stops `Server` — the key still reads — so it is read off the answer itself.
    const bool stopped = m_server && m_server->stopped();
    if (stopped || answer.status == 403) {
        m_of->m_trouble = stopped ? m_server->whyStopped() : answer.trouble;
        for (int at = 0; at < m_of->m_rows.size(); ++at) {
            if (m_of->m_rows.at(at).stage == Sending
                || (m_of->m_rows.at(at).stage == Ready && m_of->m_rows.at(at).road.released))
                settle(at, Failed);
        }
        emit m_of->changed();
        return;
    }

    // Read before it is raised, so the first wait is the first step. Raised first, the
    // table began at its second entry: the two seconds it opens with were never served to
    // anybody, and a cut cable waited five before it tried again.
    m_of->m_rows[row].retryIn = waitFor(m_of->m_rows.at(row).attempts);
    ++m_of->m_rows[row].attempts;
    m_of->m_rows[row].stage = Paused;
    announce(row);
    if (!m_retry.isActive())
        m_retry.start();
    // Somebody else may go while this one waits: the slot is not held by a file that is
    // counting seconds.
    pump();
}

void Carrier::cameBack(Card kept)
{
    kept.stage = Stage::Failed;
    kept.trouble = Words::couldNotCleanUp(kept.name);
    // Failed, so out of its drop: the rule `settle` keeps for every other failure.
    kept.road.drop = 0;
    m_of->beginInsertRows({}, int(m_of->m_rows.size()), int(m_of->m_rows.size()));
    m_of->m_rows.append(kept);
    m_of->endInsertRows();
    emit m_of->changed();
}

void Carrier::abandon(int row)
{
    if (row < 0 || row >= m_of->m_rows.size())
        return;
    // Kept whole, not just the id: the one path where the server refuses to let this go
    // reinserts exactly this — same tree, same name, everything a card needs to draw
    // itself — as the failed row it now is, rather than rebuilding one from whatever
    // scraps a callback captured.
    const Card kept = m_of->m_rows.at(row);
    const bool wasHolding = holding() == row;

    m_of->beginRemoveRows({}, row, row);
    m_of->m_rows.remove(row);
    m_of->endRemoveRows();

    // The server keeps what nobody named, so it has to be told. A file's session lives
    // under `/intake/{id}` and a folder's under `/import/{id}` — two different routes for
    // the two roads `Card::folder` already tells apart everywhere else in this file. Sending
    // a folder's id down `/intake/` instead refused every folder's cleanup —
    // `received_folder` on the server reads the id through `Origin::of` and never
    // recognises an `imp_…` one there — and the bytes it had already received stayed in
    // the inbox forever, one box per abandoned folder.
    if (m_server && !kept.id.isEmpty()) {
        const QString route = (kept.folder ? u"/import/"_s : u"/intake/"_s) + kept.id;
        m_server->remove(route, this, [this, kept](const Server::Answer &answer) {
            if (answer.went()) {
                m_of->forget(kept.id);
                return;
            }
            // Said on the one card it is true of, and nowhere else: abandoning it says
            // nothing about whether the rest of the queue can still send, so a refusal
            // comes back as this one failed row rather than a `trouble` that would stop
            // every other one too — `pump()` reads that property and nothing here touches
            // it. A refused key or a 403 on a transfer, in `retryLater`, is the case where
            // the cause really is shared — a key without the right to import, which nothing
            // sending will get past either — and that one still halts the whole queue;
            // this is not that.
            cameBack(kept);
        });
    }

    emit m_of->changed();
    if (wasHolding)
        pump();
}
