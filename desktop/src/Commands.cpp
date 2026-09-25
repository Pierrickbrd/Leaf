#include "Commands.h"

#include "Words.h"

#include <QDebug>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

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
            {u"eraseSeries"_s, Words::command(EraseSeries)},
            {u"markEntryRead"_s, Words::command(MarkEntryRead)},
            {u"markEntryUnread"_s, Words::command(MarkEntryUnread)},
            {u"reimportEntry"_s, Words::command(ReimportEntry)},
            {u"saveACopy"_s, Words::command(SaveACopy)},
            {u"eraseEntry"_s, Words::command(EraseEntry)}};
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
    m_server->getFile(u"/entries/"_s + entryId + u"/file"_s, this,
                      [this, mine, path](const Server::Answer &answer) {
        if (mine != m_generation)
            return;
        m_busy = false;
        if (!answer.went()) {
            m_trouble = answer.trouble;
            emit changed();
            return;
        }
        write(answer.file, path);
    });
}

void Commands::showTheFolder(const QString &path) const
{
    const QFileInfo about(path);
    if (!about.exists())
        return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(about.absolutePath()));
}

void Commands::write(const QByteArray &archive, const QString &path)
{
    // Whole or not at all. A half-written archive beside the library is worse than none: it
    // opens, it is the wrong length, and nothing about it says which.
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || out.write(archive) != archive.size() || !out.flush()) {
        m_trouble = Words::couldNotWrite(out.fileName());
        out.remove();
        emit changed();
        return;
    }
    out.close();
    emit changed();
    emit saved(path);
}
