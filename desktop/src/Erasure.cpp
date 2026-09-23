#include "Erasure.h"

#include "Words.h"

#include <QDebug>
#include <QJsonArray>
#include <QUrlQuery>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

/// Every match, which the contract spells `0`. A deletion that announced twenty-nine files
/// and took thirty would be the worst kind of wrong on the one screen that cannot be undone.
constexpr const char *Everything = "0";

} // namespace

Erasure *Erasure::create(QQmlEngine *engine, QJSEngine *)
{
    auto *server = engine->singletonInstance<Server *>(qmlTypeId("Leaf", 1, 0, "Server"));
    if (!server) {
        qWarning().noquote()
            << QStringLiteral("error resolving the Server singleton — nothing will be "
                              "deleted, which is the safe way for this to fail");
    }
    return new Erasure(server);
}

Erasure::Erasure(Server *server, QObject *parent)
    : QObject(parent)
    , m_server(server)
{
}

QString Erasure::cancelLabel() const
{
    return Words::cancel();
}

QString Erasure::eraseLabel() const
{
    return Words::eraseButton();
}

const Api::Entry *Erasure::theEntry() const
{
    const auto found = std::ranges::find(m_files, m_entryId, &Api::Entry::id);
    return found == m_files.cend() ? nullptr : std::to_address(found);
}

QVariantMap Erasure::said() const
{
    if (!m_one.has_value())
        return {};
    return whole() ? saidOfEdition() : saidOfFile();
}

QVariantMap Erasure::saidOfEdition() const
{
    QVariantMap lines;
    const QString name = m_one->edition.value_or(m_one->work);
    qint64 bytes = 0;
    for (const Api::Entry &one : m_files)
        bytes += one.size;
    lines.insert(u"question"_s, Words::eraseSeriesQuestion(name));
    lines.insert(u"what"_s,
                 Words::whatAWholeEditionTakes(int(m_files.size()), bytes,
                                               m_one->holding.readEntries,
                                               m_one->holding.partRead > 0,
                                               m_one->holding.readEntries + 1));
    lines.insert(u"warning"_s, Words::noTrash(true));
    lines.insert(u"confirm"_s, Words::typeToConfirm(name));

    // Named only when there is one. « Supprimer Elfes » is ambiguous the day two editions
    // carry that name, and that is exactly the day nobody may be wrong.
    if (const auto other = std::ranges::find_if(m_siblings,
                                                [this](const Api::Series &one) {
                                                    return one.id != m_seriesId;
                                                });
        other != m_siblings.cend()) {
        lines.insert(u"untouched"_s,
                     Words::otherEditionUntouched(other->edition.value_or(other->work)));
    }
    return lines;
}

QVariantMap Erasure::saidOfFile() const
{
    QVariantMap lines;
    const Api::Entry *one = theEntry();
    if (one == nullptr)
        return lines;

    const double number = one->number.value_or(0);
    lines.insert(u"question"_s, Words::eraseEntryQuestion(number));
    lines.insert(u"what"_s,
                 Words::whatGoes(one->title.value_or(QString()), one->pageCount, one->size));
    lines.insert(u"file"_s, Words::whichFile(one->file));
    lines.insert(u"warning"_s, Words::noTrash(false));

    // Where it falls decides what is left behind, and the sentence follows the server's own
    // answer to the same question: a hole is reported only between the lowest and the highest
    // volume held, so erasing an end moves the end rather than leaving a gap.
    bool below = false;
    bool above = false;
    std::optional<double> nextHeld;
    for (const Api::Entry &file : m_files) {
        if (file.id == m_entryId || !file.number.has_value())
            continue;
        below = below || *file.number < number;
        if (*file.number > number) {
            above = true;
            if (!nextHeld.has_value() || *file.number < *nextHeld)
                nextHeld = file.number;
        }
    }
    lines.insert(u"leaves"_s,
                 Words::whatWouldRemain(int(m_files.size()) - 1, m_one->medium, number,
                                        below && above, nextHeld));
    return lines;
}

bool Erasure::ready() const
{
    if (loading() || m_going || !m_one.has_value())
        return false;
    if (!whole())
        return theEntry() != nullptr;
    // The name, typed. The only protection that works against a gesture made by reflex, and
    // it is compared as it was written — a reader who types the name types the name.
    return m_written == m_one->edition.value_or(m_one->work);
}

void Erasure::aboutEntry(const QString &entryId, const QString &seriesId)
{
    ask(seriesId);
    m_entryId = entryId;
    emit changed();
}

void Erasure::aboutSeries(const QString &seriesId)
{
    ask(seriesId);
    emit changed();
}

void Erasure::ask(const QString &seriesId)
{
    ++m_generation;
    m_seriesId = seriesId;
    m_entryId.clear();
    m_one.reset();
    m_files.clear();
    m_siblings.clear();
    m_written.clear();
    m_trouble.clear();
    m_going = false;
    m_pending = 0;
    m_asking = true;

    if (seriesId.isEmpty() || m_server == nullptr) {
        m_trouble = Words::nothingToErase();
        return;
    }

    const int mine = m_generation;
    m_pending = 2;
    m_server->get(u"/series/"_s + seriesId, this, [this, mine](const Server::Answer &answer) {
        if (mine == m_generation)
            tookSeries(answer);
    });
    m_server->get(u"/series/"_s + seriesId + u"/entries"_s, this,
                  [this, mine](const Server::Answer &answer) {
        if (mine == m_generation)
            tookEntries(answer);
    });
}

void Erasure::tookSeries(const Server::Answer &answer)
{
    --m_pending;
    if (!answer.went() || !answer.body.isObject()) {
        m_trouble = answer.went() ? Words::unreadableAnswer() : answer.trouble;
        emit changed();
        return;
    }
    const Api::Read<Api::Series> read = Api::series(answer.body.object());
    if (!read.ok()) {
        m_trouble = read.trouble;
        emit changed();
        return;
    }
    m_one = read.value;
    emit changed();

    // The other editions of the same *work*, which is a question that cannot be asked before
    // this answer: the work is what the two editions share and the series is what tells them
    // apart. It is one sentence of the modal, and that sentence is the difference between
    // deleting the edition somebody meant and the other one of the same name.
    if (!whole())
        return;
    ++m_pending;
    const int mine = m_generation;
    QUrlQuery query;
    query.addQueryItem(u"work"_s, m_one->workId);
    query.addQueryItem(u"size"_s, QString::fromUtf8(Everything));
    m_server->get(u"/series"_s, query, this, [this, mine](const Server::Answer &siblings) {
        if (mine == m_generation)
            tookSiblings(siblings);
    });
}

void Erasure::tookEntries(const Server::Answer &answer)
{
    --m_pending;
    if (!answer.went() || !answer.body.isArray()) {
        m_trouble = answer.went() ? Words::unreadableAnswer() : answer.trouble;
        emit changed();
        return;
    }
    for (const QJsonValue &one : answer.body.array()) {
        if (const Api::Read<Api::Entry> read = Api::entry(one.toObject()); read.ok())
            m_files.append(*read.value);
    }
    emit changed();
}

void Erasure::tookSiblings(const Server::Answer &answer)
{
    --m_pending;
    // A refusal here costs one sentence and not the confirmation: what it names is what is
    // *not* being deleted, and its absence hides nothing about what is.
    if (answer.went() && answer.body.isObject()) {
        if (const Api::Read<Api::Page> read = Api::page(answer.body.object()); read.ok())
            m_siblings = read.value->items;
    }
    emit changed();
}

void Erasure::typed(const QString &what)
{
    if (what == m_written)
        return;
    m_written = what;
    emit changed();
}

void Erasure::go()
{
    // Checked here and not only drawn: a dead button is one way in, and an invokable is
    // another. The one screen that unmakes files says no twice.
    if (!ready())
        return;

    m_going = true;
    m_trouble.clear();
    emit changed();

    const int mine = m_generation;
    const QString path = whole() ? u"/series/"_s + m_seriesId
                                 : u"/entries/"_s + m_entryId;
    m_server->remove(path, this, [this, mine](const Server::Answer &answer) {
        if (mine == m_generation)
            tookErasure(answer);
    });
}

void Erasure::tookErasure(const Server::Answer &answer)
{
    m_going = false;
    if (!answer.went() || !answer.body.isObject()) {
        m_trouble = answer.went() ? Words::unreadableAnswer() : answer.trouble;
        emit changed();
        return;
    }

    const Api::Read<Api::Erased> gone = Api::erased(answer.body.object());
    if (!gone.ok()) {
        m_trouble = gone.trouble;
        emit changed();
        return;
    }
    // A file that would not go keeps its row and keeps the edition alive with it, so the
    // modal stays up and names them rather than closing on a job half done.
    if (!gone.value->refused.isEmpty()) {
        m_trouble = Words::wouldNotGo(gone.value->refused);
        emit changed();
        return;
    }

    const QString series = m_seriesId;
    const QString entry = m_entryId;
    dismiss();
    emit erased(series, entry);
}

void Erasure::dismiss()
{
    ++m_generation;
    m_asking = false;
    m_going = false;
    m_pending = 0;
    m_entryId.clear();
    m_one.reset();
    m_files.clear();
    m_siblings.clear();
    m_written.clear();
    m_trouble.clear();
    emit changed();
}
