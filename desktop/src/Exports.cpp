#include "Exports.h"

#include "Words.h"

#include <QDir>
#include <QJsonObject>
#include <QStorageInfo>
#include <QUrlQuery>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

/// The three answers an export waits for are read alike: one that did not go says why in its
/// own words, which are the ones the reader is shown.
template <typename T>
Api::Read<T> readAnswer(const Server::Answer &answer, Api::Read<T> (*parse)(const QJsonObject &))
{
    if (!answer.went())
        return {std::nullopt, answer.trouble};
    if (!answer.body.isObject())
        return {};
    return parse(answer.body.object());
}

/// What `/series` is asked for to export a whole work or universe: every match, which the
/// contract spells `0`.
QUrlQuery sharing(const Api::Series &one, bool wholeUniverse)
{
    QUrlQuery query;
    if (wholeUniverse)
        query.addQueryItem(u"universe"_s, *one.universe);
    else
        query.addQueryItem(u"work"_s, one.workId);
    query.addQueryItem(u"size"_s, u"0"_s);
    return query;
}

} // namespace

Exports::Exports(Server *server, std::function<qint64(const QString &)> freeSpace,
                 QObject *parent)
    : QObject(parent), m_server(server), m_freeSpace(std::move(freeSpace))
{
    if (!m_freeSpace) {
        m_freeSpace = [](const QString &folder) {
            return QStorageInfo(folder).bytesAvailable();
        };
    }
}

Exports *Exports::create(QQmlEngine *engine, QJSEngine *)
{
    auto *server = engine->singletonInstance<Server *>(qmlTypeId("Leaf", 1, 0, "Server"));
    auto *made = new Exports(server);
    QJSEngine::setObjectOwnership(made, QJSEngine::CppOwnership);
    return made;
}

QString Exports::label() const
{
    return m_name.isEmpty() ? QString() : Words::exportingNow(m_name);
}

double Exports::fraction() const
{
    return m_total > 0 ? std::clamp(double(m_sent) / double(m_total), 0.0, 1.0) : 0.0;
}

QString Exports::percent() const
{
    return Words::percentSent(fraction());
}

QVariantMap Exports::words() const
{
    return {{u"folderTitle"_s, Words::exportFolderTitle()},
            {u"conflictTitle"_s, Words::exportConflictTitle()},
            {u"replace"_s, Words::exportReplace()},
            {u"complete"_s, Words::exportComplete()},
            {u"cancel"_s, Words::cancel()}};
}

void Exports::exportSeries(const QStringList &seriesIds, const QUrl &into, const QString &name)
{
    if (seriesIds.isEmpty() || busy() || !into.isLocalFile())
        return;
    begin(into, name.isEmpty() ? Words::exportOfSeveral(int(seriesIds.size())) : name);
    ask(seriesIds);
}

void Exports::exportWork(const QString &seriesId, const QUrl &into)
{
    if (seriesId.isEmpty() || busy() || !into.isLocalFile())
        return;
    begin(into, {});
    resolve(seriesId, false);
}

void Exports::exportUniverse(const QString &seriesId, const QUrl &into)
{
    if (seriesId.isEmpty() || busy() || !into.isLocalFile())
        return;
    begin(into, {});
    resolve(seriesId, true);
}

void Exports::begin(const QUrl &into, const QString &name)
{
    ++m_generation;
    m_phase = Phase::Reading;
    m_name = name;
    m_plan = ExportPlan(QDir::cleanPath(into.toLocalFile()));
    m_question.clear();
    m_sent = 0;
    m_total = 0;
    m_trees.clear();
    m_at = 0;
    m_writeFailed = false;
    m_out.reset();
    emit changed();
}

void Exports::resolve(const QString &seriesId, bool wholeUniverse)
{
    const int mine = m_generation;
    const auto listed = [this, mine, seriesId](const Server::Answer &all) {
        if (mine != m_generation)
            return;
        const Api::Read<Api::Page> page = readAnswer(all, Api::page);
        if (!page.ok())
            return fail(page.trouble);
        QStringList ids;
        for (const Api::Series &one : page.value->items)
            ids << one.id;
        // The series asked about is in it, whatever the list says.
        if (ids.isEmpty())
            ids << seriesId;
        ask(ids);
    };
    m_server->get(u"/series/"_s + seriesId, this,
                  [this, mine, wholeUniverse, listed](const Server::Answer &answer) {
        if (mine != m_generation)
            return;
        const Api::Read<Api::Series> read = readAnswer(answer, Api::series);
        if (!read.ok())
            return fail(read.trouble);

        // The series' own name for a work, the universe's for a universe: what the bar says
        // and the bubble repeats, though none of the three screens knew it when it was asked.
        if (wholeUniverse && !read.value->universe)
            return fail(Words::nothingToExport());
        m_name = wholeUniverse ? *read.value->universe : read.value->work;
        emit changed();
        m_server->get(u"/series"_s, sharing(*read.value, wholeUniverse), this, listed);
    });
}

void Exports::ask(const QStringList &seriesIds)
{
    const int mine = m_generation;
    m_ids = seriesIds;
    m_trees = QList<Api::Tree>(seriesIds.size());
    m_waiting = int(seriesIds.size());
    for (int at = 0; at < seriesIds.size(); ++at) {
        m_server->get(u"/series/"_s + seriesIds.at(at) + u"/folders"_s, this,
                      [this, mine, at](const Server::Answer &answer) {
            if (mine != m_generation)
                return;
            const Api::Read<Api::Tree> read = readAnswer(answer, Api::tree);
            if (!read.ok())
                return fail(read.trouble);
            m_trees[at] = *read.value;
            if (--m_waiting > 0)
                return;
            if (const QString why = m_plan.merge(m_trees, m_ids); !why.isEmpty())
                return fail(why);
            const QStringList there = m_plan.present();
            if (there.isEmpty())
                return write(false);
            m_phase = Phase::Asking;
            m_question = Words::exportConflict(int(there.size()), there.first());
            emit changed();
            emit conflict(int(there.size()), there.first());
        });
    }
}

void Exports::replace()
{
    if (m_phase == Phase::Asking)
        write(false);
}

void Exports::complete()
{
    if (m_phase == Phase::Asking)
        write(true);
}

void Exports::write(bool keepWhatIsThere)
{
    m_phase = Phase::Writing;
    m_question.clear();

    // « Compléter » is what is missing and nothing else: a file that is there is left as it
    // is, byte for byte, and is not counted in what has to fit.
    if (keepWhatIsThere)
        m_plan.leaveWhatIsThere();
    m_total = m_plan.size();
    emit changed();

    if (const qint64 free = m_freeSpace(m_plan.destination()); free < m_total)
        return fail(Words::notEnoughRoom(m_total, free));
    if (const QString why = m_plan.makeFolders(keepWhatIsThere); !why.isEmpty())
        return fail(why);
    next();
}

void Exports::next()
{
    if (m_at >= m_plan.jobs().size()) {
        m_phase = Phase::Idle;
        ++m_generation;
        emit changed();
        emit finished(m_plan.where(), m_plan.missing());
        return;
    }

    const ExportPlan::Job &job = m_plan.jobs().at(m_at);
    const QString path = m_plan.target(job.path);
    m_out = std::make_unique<QSaveFile>(path);
    m_writeFailed = false;
    if (!m_out->open(QIODevice::WriteOnly))
        return fail(Words::couldNotWrite(path));

    const int mine = m_generation;
    QPointer<Exports> self(this);
    m_server->download(
        u"/series/"_s + job.seriesId + u"/files/"_s + QString::number(job.index), this,
        [self, mine](const QByteArray &piece) {
            // Over, or stopped: the connection is dropped at the next piece.
            if (!self || mine != self->m_generation || !self->m_out)
                return false;
            if (self->m_out->write(piece) != piece.size()) {
                self->m_writeFailed = true;
                return false;
            }
            self->m_sent += piece.size();
            emit self->changed();
            return true;
        },
        [this, mine, path](const Server::Answer &answer) {
            if (mine != m_generation)
                return;
            if (m_writeFailed)
                return fail(Words::couldNotWrite(path));
            if (!answer.went())
                return fail(answer.trouble);
            if (!m_out->commit())
                return fail(Words::couldNotWrite(path));
            m_out.reset();
            ++m_at;
            next();
        });
}

void Exports::fail(const QString &why)
{
    // Never while idle, where `cancel` would say nothing: whatever fails is on its way, every
    // answer being checked against the generation that each end moves on.
    cancel();
    emit failed(why);
}

void Exports::cancel()
{
    if (m_phase == Phase::Idle)
        return;
    ++m_generation;
    // Nothing of the file being written stays: not the file, not the temporary one beside it.
    if (m_out)
        m_out->cancelWriting();
    m_out.reset();
    m_phase = Phase::Idle;
    m_question.clear();
    emit changed();
}
