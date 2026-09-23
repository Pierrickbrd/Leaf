#include "Series.h"

#include "Words.h"

#include <QDebug>
#include <QJsonArray>
#include <QUrlQuery>
#include <QVariantMap>

using namespace Qt::StringLiterals;

namespace {

/// One `{label, value}` pair, or nothing when the value is not recorded. « Collection : — »
/// is a line that says a fact is missing; leaving it out says the same and takes no room.
void pair(QVariantList &into, Words::Fact which, const QString &value)
{
    if (value.trimmed().isEmpty())
        return;
    into.append(QVariantMap{{u"label"_s, Words::fact(which)}, {u"value"_s, value}});
}

/// The same, for the one line that is painted in the alert colour rather than in ink.
void alarming(QVariantList &into, const QString &label, const QString &value)
{
    if (value.isEmpty())
        return;
    into.append(QVariantMap{{u"label"_s, label}, {u"value"_s, value}, {u"alarming"_s, true}});
}

} // namespace

Series *Series::create(QQmlEngine *engine, QJSEngine *)
{
    auto *server = engine->singletonInstance<Server *>(qmlTypeId("Leaf", 1, 0, "Server"));
    if (!server) {
        qWarning().noquote()
            << QStringLiteral("error resolving the Server singleton — a series page will stay "
                              "empty");
    }
    return new Series(server);
}

Series::Series(Server *server, QObject *parent)
    : QObject(parent)
    , m_server(server)
{
}

QString Series::universe() const
{
    return m_one && m_one->universe ? *m_one->universe : QString();
}

QString Series::universeId() const
{
    return m_one && m_one->universeId ? *m_one->universeId : QString();
}

QString Series::work() const
{
    return m_one ? m_one->work : QString();
}

QString Series::edition() const
{
    return m_one && m_one->edition ? *m_one->edition : QString();
}

QString Series::cover() const
{
    if (!m_one || !m_server)
        return {};
    // Resolved server-side — « one request, not one to find the first entry and another for
    // its cover ». And it is the edition's own: the Intégrale shows its first volume, not the
    // albums'.
    return m_server->address() + u"/series/"_s + m_one->id + u"/cover"_s;
}

QString Series::makers() const
{
    return m_one ? Words::makers(*m_one) : QString();
}

QString Series::weights() const
{
    return m_one ? Words::weights(*m_one) : QString();
}

QStringList Series::genres() const
{
    if (!m_one)
        return {};
    // Genres and tags, in that order and never folded together: measured, a work carries one
    // genre and seven tags with nothing in common between the two lists.
    QStringList said = m_one->genres;
    said += m_one->tags;
    return said;
}

QString Series::summary() const
{
    return m_one && m_one->summary ? *m_one->summary : QString();
}

QVariantList Series::credits() const
{
    using enum Words::Fact;
    QVariantList said;
    if (!m_one)
        return said;
    const Api::Credits &by = m_one->credits;
    QStringList writers = by.authors;
    if (writers.isEmpty() && by.author.has_value())
        writers << *by.author;
    pair(said, Writers, writers.join(u", "_s));
    pair(said, Artists, by.artists.join(u", "_s));
    pair(said, Publisher, m_one->publication.publisher.value_or(QString()));
    pair(said, Collection, m_one->publication.collection.value_or(QString()));
    if (const auto &tag = m_one->publication.language; tag && !tag->isEmpty())
        pair(said, Language, Words::language(*tag));
    return said;
}

QVariantList Series::nature() const
{
    using enum Words::Fact;
    QVariantList said;
    if (!m_one)
        return said;
    if (m_one->medium.has_value())
        pair(said, Kind, Words::medium(*m_one->medium));
    if (m_one->readingDirection.has_value())
        pair(said, Direction, Words::readingDirection(*m_one->readingDirection));
    if (m_one->run.has_value()) {
        QString said_ =
            Words::editionStatus(*m_one->run == Api::Run::Completed ? u"completed"_s
                                                                    : u"ongoing"_s);
        // What the publisher announces, beside the state of the run — the two are one line
        // because « en cours » alone leaves « of how many? » unanswered.
        if (const auto declared = m_one->publication.declaredVolumes; declared.has_value())
            said_ += u" · "_s + Words::volumes(*declared, m_one->medium) + u" annoncés"_s;
        pair(said, Status, said_);
    }
    pair(said, Age, m_one->ageRating.value_or(QString()));
    if (m_one->colour.has_value())
        pair(said, Colour, Words::colour(*m_one->colour));
    return said;
}

QVariantList Series::holding() const
{
    using enum Words::Fact;
    QVariantList said;
    if (!m_one)
        return said;
    const Api::Holding &has = m_one->holding;
    const int ceiling = m_one->publication.declaredVolumes.value_or(0);
    pair(said, Held, Words::heldOutOf(has.ownedVolumes, ceiling));
    // The only place that says a volume is missing, and the only line painted in alert.
    alarming(said, Words::missingLabel(int(has.missingVolumes.size())),
             Words::missingVolumes(has.missingVolumes));
    if (has.readEntries > 0 || has.partRead > 0.0) {
        const bool open = has.partRead > 0.0;
        pair(said, Read,
             Words::readEntries(has.readEntries, open, has.readEntries + 1));
    }
    if (has.addedAt.has_value())
        pair(said, FirstReceived, Words::moment(*has.addedAt));
    if (has.lastAddedAt.has_value())
        pair(said, LastReceived, Words::moment(*has.lastAddedAt));
    return said;
}

QVariantList Series::editions() const
{
    QVariantList said;
    if (m_siblings.size() < 2)
        return said;
    for (const Api::Series &one : m_siblings) {
        said.append(QVariantMap{
            {u"identifier"_s, one.id},
            // An implicit edition has no name of its own — nothing would ever show it — so
            // the work's name stands in rather than a blank pill.
            {u"name"_s, one.edition.value_or(one.work)},
            {u"count"_s, Words::volumes(one.holding.ownedVolumes, one.medium)},
            {u"here"_s, one.id == m_id},
        });
    }
    return said;
}

QString Series::editionsLabel() const
{
    return Words::editions(int(m_siblings.size()));
}

QString Series::oneShotEntry() const
{
    return m_one && m_one->oneShotEntry ? *m_one->oneShotEntry : QString();
}

QVariantList Series::missingVolumes() const
{
    QVariantList said;
    if (!m_one)
        return said;
    for (const double one : m_one->holding.missingVolumes)
        said.append(one);
    return said;
}

void Series::point(const QString &identifier)
{
    m_id = identifier;
    reload();
}

void Series::reload()
{
    ++m_generation;
    m_trouble.clear();

    if (m_id.isEmpty()) {
        m_one.reset();
        m_siblings.clear();
        m_loading = false;
        emit changed();
        return;
    }
    if (!m_server) {
        m_loading = false;
        m_trouble = Words::notSetUp(Words::Asking::Shelf);
        emit changed();
        return;
    }

    m_loading = true;
    // Nothing is cleared here: what is on screen stays until the answer arrives. Emptying to
    // wait is the blank screen the artifact refuses, and the page has nothing to gain by it.
    emit changed();

    const int mine = m_generation;
    m_server->get(u"/series/"_s + m_id, this, [this, mine](const Server::Answer &answer) {
        if (mine == m_generation)
            took(answer);
    });
}

void Series::took(const Server::Answer &answer)
{
    m_loading = false;

    if (!answer.went()) {
        m_trouble = answer.trouble;
        emit changed();
        return;
    }
    if (!answer.body.isObject()) {
        m_trouble = QStringLiteral("series: expected an object");
        emit changed();
        return;
    }

    const Api::Read<Api::Series> read = Api::series(answer.body.object());
    if (!read.ok()) {
        m_trouble = read.trouble;
        emit changed();
        return;
    }

    m_one = *read.value;
    m_trouble.clear();
    emit changed();

    // The siblings follow, and only then: the work is what they are asked for, and it is this
    // answer that carries it. A page that asked for both at once would ask the second with an
    // identifier it does not have yet.
    const int mine = m_generation;
    QUrlQuery query;
    query.addQueryItem(u"work"_s, m_one->workId);
    m_server->get(u"/series"_s, query, this, [this, mine](const Server::Answer &sibling) {
        if (mine == m_generation)
            tookSiblings(sibling);
    });
}

void Series::tookSiblings(const Server::Answer &answer)
{
    // A refusal here costs the switcher and nothing else: the page is drawn, and offering no
    // other edition is exactly what a work with one edition looks like. Reporting it in
    // `trouble` would put a banner over a page that is perfectly readable.
    if (!answer.went() || !answer.body.isObject())
        return;

    const Api::Read<Api::Page> read = Api::page(answer.body.object());
    if (!read.ok())
        return;

    m_siblings = read.value->items;
    emit changed();
}
