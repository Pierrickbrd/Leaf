#include "Carrier.h"

#include "Cbz.h"
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

#include <array>
#include <utility>

using namespace Qt::StringLiterals;

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

} // namespace

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
            announce(row);
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
    if (row >= 0 && row < m_of->m_rows.size()) {
        emit m_of->dataChanged(m_of->index(row), m_of->index(row));
    }
    emit m_of->changed();
}

void Carrier::settle(int row, Stage stage, const QString &trouble)
{
    if (row < 0 || row >= m_of->m_rows.size())
        return;
    m_of->m_rows[row].stage = stage;
    m_of->m_rows[row].trouble = trouble;
    announce(row);
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

    if (!verifying) {
        // Without a checksum the walk is a `stat` of every file, over before the pool has
        // even been asked twice — reporting progress on something that fast would only add
        // three thousand deliveries for a folder of chapters, never a count anyone reads.
        m_of->m_walkingToken = 0;
        watcher->setFuture(QtConcurrent::run(Manifest::of, path, false, Manifest::Progress{}));
        return;
    }

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
        [path, total](QPromise<Manifest::Folder> &promise) {
            promise.setProgressRange(0, int(total));
            promise.addResult(Manifest::of(path, true, [&promise](qint64 done) {
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
        m_of->m_rows[at].size = tree.bytes();
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

    QJsonArray files;
    for (const Manifest::Entry &entry : tree.files) {
        QJsonObject one{{u"path"_s, entry.path}, {u"size"_s, double(entry.size)}};
        if (!entry.checksum.isEmpty())
            one.insert(u"checksum"_s, entry.checksum);
        files.append(one);
    }
    QJsonArray sidecars;
    for (const Manifest::Sidecar &one : tree.sidecars) {
        sidecars.append(
            QJsonObject{{u"path"_s, one.path}, {u"json"_s, QString::fromUtf8(one.json)}});
    }

    // Always ADDITION: the scope question made sense only at the level that holds volumes,
    // and asking it of every edition of a ten-series universe was ten questions for a claim
    // one is rarely in a position to make. Naming a folder COMPLETE is a capability the
    // server keeps — nothing on this side asks for it any more.
    const QJsonObject body{{u"root"_s, tree.root},
                           {u"files"_s, files},
                           {u"sidecars"_s, sidecars},
                           {u"scope"_s, u"ADDITION"_s}};
    const QString path = m_of->m_rows.at(row).path;
    m_server->post(u"/import"_s, QJsonDocument(body).toJson(QJsonDocument::Compact), this,
                   [this, path](const Server::Answer &answer) { tookFolder(path, answer); });
}

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
    m_of->m_rows[row].road.toSend = read.value->toSend;
    m_of->m_rows[row].road.replaces = read.value->replaces;
    // Cleared with the announcement for `filing`'s own reason: a re-announced folder may
    // answer differently, and a tick against a volume it no longer names would overwrite
    // something for a reason nobody could see.
    m_of->m_rows[row].road.replacing.clear();
    m_of->m_rows[row].road.declarations = read.value->declarations;
    m_of->m_rows[row].road.declaring.clear();
    m_of->m_rows[row].road.alreadyThere = read.value->alreadyThere;
    m_of->m_rows[row].road.creates = read.value->creates;
    m_of->m_rows[row].road.moves = read.value->moves;
    // Cleared with the announcement, never carried across one: a re-announced folder may
    // answer differently, and a tick against a work it no longer names would move it for
    // no reason a reader could see.
    m_of->m_rows[row].road.filing.clear();
    m_of->m_rows[row].road.at = 0;
    m_of->m_rows[row].road.sentInFile = 0;
    // What is already there costs nothing and is not sent again, so the bar measures what
    // will actually travel rather than what the folder weighs.
    m_of->m_rows[row].size = read.value->bytesToSend;

    // Nothing to create and nothing to move is nothing to ask about. Creating a universe
    // is exactly the thing a reader cannot undo by deleting a file, and a series that moves
    // is one they would have to find again — so either stops and waits.
    const bool asks = !read.value->creates.isEmpty() || !read.value->moves.isEmpty();
    settle(row, asks ? Stage::Deciding : Stage::Ready);
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

void Carrier::pump()
{
    if (!m_server || !m_of->m_trouble.isEmpty() || holding() >= 0)
        return;

    for (int row = 0; row < m_of->m_rows.size(); ++row) {
        // A folder has no series to choose: it declares one. Asking it for a `chosen`
        // before letting it go left it Ready for ever, which looks exactly like a queue
        // that decided not to bother.
        if (m_of->m_rows.at(row).stage != Stage::Ready)
            continue;
        if (!m_of->m_rows.at(row).folder && m_of->m_rows.at(row).chosen.isEmpty())
            continue;
        // Left where it is. Putting the one in flight on top meant the list rearranged
        // itself under the reader's eyes every time a transfer ended — a card they were
        // looking at moved because a different one finished, and the order they dropped
        // the folders in was gone. The badge says which one is going; the list does not
        // have to.
        m_of->m_rows[row].stage = Stage::Sending;
        announce(row);
        sendMore(row);
        return;
    }

    // Nothing ready, and somebody stepped aside earlier: the slot is free and it is theirs
    // again. This is the rule nothing clicks — a paused file starts again by itself once
    // the one ahead of it is done.
    for (int row = 0; row < m_of->m_rows.size(); ++row) {
        if (m_of->m_rows.at(row).stage != Stage::Paused)
            continue;
        // Except one that is counting seconds. `retryLater` parks a failed transfer at
        // `Paused` too, and this loop could not tell that apart from a reader's own pause
        // without looking at `retryIn` — so it took the slot back in the same turn the
        // failure arrived. The countdown on the card was a lie every time, and against a
        // server answering 500 it was a hot loop: fail, resume, fail, as fast as the
        // answers came back, with `attempts` climbing and no wait ever served.
        if (m_of->m_rows.at(row).retryIn > 0)
            continue;
        settle(row, Stage::Ready);
        pump();
        return;
    }
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
                          retryLater(at);
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

    QFile file(one.path + u'/' + relative);
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
                          retryLater(at);
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
                          retryLater(at);
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
    const QByteArray body =
        QJsonDocument(QJsonObject{{u"move"_s, filing},
                        {u"replace"_s, replacing},
                        {u"declare"_s, declaring}})
            .toJson(QJsonDocument::Compact);
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

void Carrier::retryLater(int row)
{
    using enum Stage;
    if (row < 0 || row >= m_of->m_rows.size())
        return;
    // The key lost its right to import, or was refused: that is not this file's fault and
    // not something waiting will mend. Said once, and the queue stops.
    if (m_server && m_server->stopped()) {
        m_of->m_trouble = m_server->whyStopped();
        for (int at = 0; at < m_of->m_rows.size(); ++at) {
            if (m_of->m_rows.at(at).stage == Sending || m_of->m_rows.at(at).stage == Ready)
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
            if (answer.went())
                return;
            // Said on the one card it is true of, and nowhere else: abandoning it says
            // nothing about whether the rest of the queue can still send, so a refusal
            // comes back as this one failed row rather than a `trouble` that would stop
            // every other one too — `pump()` reads that property and nothing here touches
            // it. `m_server->stopped()`, in `retryLater`, is the case where the cause
            // really is shared — a key that lost its right to import, which nothing
            // sending will get past either — and that one still halts the whole queue;
            // this is not that.
            Card back = kept;
            back.stage = Stage::Failed;
            back.trouble = Words::couldNotCleanUp(kept.name);
            m_of->beginInsertRows({}, int(m_of->m_rows.size()), int(m_of->m_rows.size()));
            m_of->m_rows.append(back);
            m_of->endInsertRows();
            emit m_of->changed();
        });
    }

    emit m_of->changed();
    if (wasHolding)
        pump();
}
