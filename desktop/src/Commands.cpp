#include "Commands.h"

#include "Words.h"

#include <QDebug>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

#include <memory>
#include <utility>

#include "Api.h"

using namespace Qt::StringLiterals;


Commands *Commands::create(QQmlEngine *engine, QJSEngine *)
{
    auto *server = engine->singletonInstance<Server *>(qmlTypeId("Leaf", 1, 0, "Server"));
    if (!server) {
        qWarning().noquote()
            << QStringLiteral("error resolving the Server singleton — the menus will open "
                              "and command nothing");
    }
    return new Commands(server);
}

Commands::Commands(Server *server, QObject *parent)
    : QObject(parent)
    , m_server(server)
{
}

QVariantMap Commands::words() const
{
    using enum Words::Command;
    return {{u"markSeriesRead"_s, Words::command(MarkSeriesRead)},
            {u"markSeriesUnread"_s, Words::command(MarkSeriesUnread)},
            {u"reimportSeries"_s, Words::command(ReimportSeries)},
            {u"addEditions"_s, Words::command(AddEditions)},
            {u"eraseSeries"_s, Words::command(EraseSeries)},
            {u"markEntryRead"_s, Words::command(MarkEntryRead)},
            {u"markEntryUnread"_s, Words::command(MarkEntryUnread)},
            {u"reimportEntry"_s, Words::command(ReimportEntry)},
            {u"saveACopy"_s, Words::command(SaveACopy)},
            {u"eraseEntry"_s, Words::command(EraseEntry)},
            {u"showInfo"_s, Words::command(ShowInfo)},
            {u"chooseDirection"_s, Words::command(ChooseDirection)},
            {u"editSeries"_s, Words::command(EditSeries)},
            {u"editEntry"_s, Words::command(EditEntry)},
            {u"eraseEdition"_s, Words::command(EraseEdition)},
            {u"editEdition"_s, Words::command(EditEdition)},
            {u"rescan"_s, Words::command(Rescan)},
            {u"export"_s, Words::command(Export)},
            {u"exportWork"_s, Words::command(ExportWork)},
            {u"exportUniverse"_s, Words::command(ExportUniverse)},
            {u"replaceChapter"_s, Words::command(ReplaceChapter)},
            {u"closeInfo"_s, Words::closeThePanel()}};
}

void Commands::markSeries(const QString &seriesId, bool read)
{
    if (seriesId.isEmpty() || m_server == nullptr)
        return;

    ++m_generation;
    const int mine = m_generation;
    m_busy = true;
    m_trouble.clear();
    emit changed();

    const QString path = u"/series/"_s + seriesId + u"/progress"_s;
    const auto answered = [this, mine, seriesId](const Server::Answer &answer) {
        if (mine == m_generation)
            took(answer, seriesId, QString());
    };
    if (!read) {
        // Forgotten rather than moved: a series marked unread is a series nobody has opened,
        // and thirty records saying « not finished, page 0 » say something else.
        m_server->remove(path, this, answered);
        return;
    }
    m_server->patch(path, QJsonDocument(QJsonObject{{u"finished"_s, true}}).toJson(
                              QJsonDocument::Compact),
                    this, answered);
}

void Commands::markEntry(const QString &entryId, const QString &seriesId, bool read)
{
    if (entryId.isEmpty() || m_server == nullptr)
        return;

    ++m_generation;
    const int mine = m_generation;
    m_busy = true;
    m_trouble.clear();
    emit changed();

    const QString path = u"/entries/"_s + entryId + u"/progress"_s;
    const auto answered = [this, mine, seriesId, entryId](const Server::Answer &answer) {
        if (mine == m_generation)
            took(answer, seriesId, entryId);
    };
    if (!read) {
        m_server->remove(path, this, answered);
        return;
    }
    // `finished: true` fills the page in as well, which the contract is explicit about: a
    // volume finished at page nought is a contradiction a shelf would draw as an empty bar
    // under the word « lu ».
    m_server->patch(path, QJsonDocument(QJsonObject{{u"finished"_s, true}}).toJson(
                              QJsonDocument::Compact),
                    this, answered);
}

void Commands::markMany(const QStringList &ids, const QString &seriesId, bool read)
{
    if (ids.isEmpty() || m_server == nullptr)
        return;

    ++m_generation;
    m_busy = true;
    m_trouble.clear();
    emit changed();

    // Shared by the answers, which come back in the network's order: the last to arrive is
    // the one that closes the batch, and the first refusal is the one that is said. Not
    // guarded by the generation: the requests are on their way, and dropping their answers
    // would leave tiles drawn with a state the server no longer has.
    struct Batch
    {
        int waiting;
        QString trouble;
        QStringList went;
    };
    const auto batch = std::make_shared<Batch>(Batch{static_cast<int>(ids.size()), {}, {}});
    const bool entries = !seriesId.isEmpty();
    for (const QString &id : ids) {
        const auto answered = [this, batch, id, seriesId, entries](const Server::Answer &answer) {
            if (answer.went())
                batch->went.append(id);
            else if (batch->trouble.isEmpty())
                batch->trouble = answer.trouble;
            if (--batch->waiting > 0)
                return;
            m_busy = false;
            m_trouble = batch->trouble;
            emit changed();
            if (entries && !batch->went.isEmpty())
                emit marked(seriesId, QString());
            else if (!entries)
                for (const QString &one : std::as_const(batch->went))
                    emit marked(one, QString());
        };
        const QString path = (entries ? u"/entries/"_s : u"/series/"_s) + id + u"/progress"_s;
        if (read) {
            m_server->patch(path, QJsonDocument(QJsonObject{{u"finished"_s, true}}).toJson(
                                      QJsonDocument::Compact),
                            this, answered);
        } else {
            m_server->remove(path, this, answered);
        }
    }
}

void Commands::took(const Server::Answer &answer, const QString &seriesId,
                    const QString &entryId)
{
    m_busy = false;
    m_trouble = answer.trouble;
    emit changed();
    if (answer.went())
        emit marked(seriesId, entryId);
}

void Commands::saveACopy(const QString &entryId, const QUrl &where)
{
    if (entryId.isEmpty() || m_server == nullptr || !where.isLocalFile())
        return;

    ++m_generation;
    const int mine = m_generation;
    m_busy = true;
    m_trouble.clear();
    emit changed();

    const QString path = where.toLocalFile();
    // To the disk as it comes, and not held whole: a volume is a hundred megabytes and more,
    // and held it was that much in memory, then the same again written. Atomic, so a copy that
    // stops half way leaves the reader's folder as it was — an archive that opens and is the
    // wrong length is worse than none.
    m_copy = std::make_unique<QSaveFile>(path);
    m_copyFailed = false;
    if (!m_copy->open(QIODevice::WriteOnly)) {
        m_copy.reset();
        m_busy = false;
        m_trouble = Words::couldNotWrite(path);
        emit changed();
        return;
    }
    m_server->download(
        u"/entries/"_s + entryId + u"/file"_s, this,
        [this, mine](const QByteArray &piece) {
            if (mine != m_generation || !m_copy)
                return false;
            if (m_copy->write(piece) != piece.size()) {
                m_copyFailed = true;
                return false;
            }
            return true;
        },
        [this, mine, path](const Server::Answer &answer) {
            if (mine != m_generation)
                return;
            m_busy = false;
            const bool wrote = m_copy && !m_copyFailed && answer.went() && m_copy->commit();
            m_copy.reset();
            if (!wrote) {
                m_trouble = !answer.went() ? answer.trouble : Words::couldNotWrite(path);
                emit changed();
                return;
            }
            emit changed();
            emit saved(path);
        });
}

void Commands::showTheFolder(const QString &path) const
{
    const QFileInfo about(path);
    if (!about.exists())
        return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(about.absolutePath()));
}

void Commands::askDirection(const QString &seriesId)
{
    if (seriesId.isEmpty() || m_server == nullptr)
        return;
    // The menu opens at once and the mark arrives with the answer: a choice shown unmarked
    // for a moment is better than a menu that waits on the network to open.
    const int mine = ++m_askedDirection;
    m_server->get(u"/series/"_s + seriesId, this, [this, mine](const Server::Answer &answer) {
        if (mine != m_askedDirection || !answer.went() || !answer.body.isObject())
            return;
        const Api::Read<Api::Series> read = Api::series(answer.body.object());
        if (read.ok())
            tookDirection(*read.value);
    });
}

void Commands::chooseDirection(const QString &seriesId, int direction)
{
    if (seriesId.isEmpty() || m_server == nullptr || direction < -1
        || direction > std::to_underlying(Api::ReadingDirection::Vertical))
        return;
    // `null` and not an absent field: « Par défaut » is taking the choice away.
    const QJsonValue said =
        direction < 0 ? QJsonValue(QJsonValue::Null)
                      : QJsonValue(Api::spell(Api::ReadingDirection(direction)));
    const int mine = ++m_askedDirection;
    m_server->set(u"/series/"_s + seriesId + u"/reading-direction"_s,
                  QJsonDocument(QJsonObject{{u"readingDirection"_s, said}})
                      .toJson(QJsonDocument::Compact),
                  this, [this, mine](const Server::Answer &answer) {
        if (mine != m_askedDirection)
            return;
        m_trouble = answer.trouble;
        if (answer.went() && answer.body.isObject()) {
            const Api::Read<Api::Series> read = Api::series(answer.body.object());
            if (read.ok())
                tookDirection(*read.value);
        }
        emit changed();
    });
}

void Commands::tookDirection(const Api::Series &series)
{
    m_direction = series.readingDirectionChosen ? std::to_underlying(*series.readingDirectionChosen) : -1;
    emit directionChanged();
}

void Commands::editSeries(const QString &seriesId, const QVariantMap &fields,
                          const QVariantMap &cover)
{
    if (seriesId.isEmpty() || m_server == nullptr)
        return;
    change(u"/series/"_s + seriesId, fields, cover, seriesId, QString());
}

void Commands::editEntry(const QString &entryId, const QString &seriesId,
                         const QVariantMap &fields, const QVariantMap &cover)
{
    if (entryId.isEmpty() || m_server == nullptr)
        return;
    change(u"/entries/"_s + entryId, fields, cover, seriesId, entryId);
}

void Commands::change(const QString &path, const QVariantMap &fields, const QVariantMap &cover,
                      const QString &seriesId, const QString &entryId)
{
    if (fields.isEmpty() && cover.isEmpty())
        return;
    ++m_generation;
    const int mine = m_generation;
    m_busy = true;
    m_trouble.clear();
    emit changed();
    // A page, a volume or an image chosen or taken back: every picture of a cover asks again, so
    // the new one shows without a restart. See `Server::coverChanged`.
    const bool coverMoves = !cover.isEmpty() || fields.contains(u"coverPage"_s)
                            || fields.contains(u"coverVolume"_s);
    const auto done = [this, mine, seriesId, entryId, coverMoves](const Server::Answer &answer) {
        if (mine != m_generation)
            return;
        m_busy = false;
        m_trouble = answer.trouble;
        emit changed();
        if (!answer.went())
            return;
        if (coverMoves)
            m_server->coverChanged();
        emit edited(seriesId, entryId);
    };
    if (fields.isEmpty()) {
        sendCover(path, cover, done);
        return;
    }
    m_server->patch(path,
                    QJsonDocument(QJsonObject::fromVariantMap(fields)).toJson(QJsonDocument::Compact),
                    this, [this, mine, path, cover, done](const Server::Answer &answer) {
        if (mine != m_generation)
            return;
        if (!answer.went() || cover.isEmpty())
            done(answer);
        else
            sendCover(path, cover, done);
    });
}

void Commands::sendCover(const QString &path, const QVariantMap &cover,
                         const std::function<void(const Server::Answer &)> &then)
{
    const QString route = path + u"/cover"_s;
    if (cover.value(u"clear"_s).toBool()) {
        m_server->remove(route, this, then);
        return;
    }
    // Read here, whole, and sent as bytes: the server is never told a path of this machine.
    // Bounded before it is read, as the server bounds it, so that a file of a gigabyte is
    // refused for what it is and not after it has been held in memory.
    constexpr qint64 Ceiling = 20 * 1024 * 1024;
    QFile file(QUrl(cover.value(u"file"_s).toString()).toLocalFile());
    if (file.size() > Ceiling) {
        then({0, {}, Words::editing().value(u"coverTooLarge"_s).toString()});
        return;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        then({0, {}, Words::editing().value(u"coverUnreadable"_s).toString()});
        return;
    }
    const QByteArray bytes = file.readAll();
    m_server->put(route, bytes, 0, bytes.size(), this, then);
}
