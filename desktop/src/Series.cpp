#include "Series.h"

#include "Words.h"

#include <QDebug>
#include <QUrlQuery>
#include <QVariantMap>

using namespace Qt::StringLiterals;

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
    , m_description(new Description(this))
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

bool Series::webtoon() const
{
    return m_one && m_one->medium == Api::Medium::Webtoon;
}

QString Series::cover() const
{
    if (!m_one || !m_server)
        return {};
    // Resolved server-side — « one request, not one to find the first entry and another for
    // its cover ». And it is the edition's own: the Intégrale shows its first volume, not the
    // albums'.
    return m_server->address() + u"/series/"_s + Server::segment(m_one->id) + u"/cover"_s;
}

QVariantList Series::editions() const
{
    QVariantList said;
    if (m_siblings.size() < 2)
        return said;
    for (const Api::Series &one : m_siblings) {
        // The keys a tile is drawn from, which are the same here as in the universe block:
        // one shape, because a series is drawn the same way wherever it appears and two
        // shapes would be two delegates the day one of them gains a field.
        said.append(QVariantMap{
            {u"seriesId"_s, one.id},
            // An implicit edition has no name of its own — nothing would ever show it — so
            // the work's name stands in rather than a blank pill.
            {u"name"_s, one.edition.value_or(one.work)},
            {u"detail"_s, Words::holdings(one)},
            {u"cover"_s, m_server == nullptr
                             ? QString()
                             : m_server->address() + u"/series/"_s + Server::segment(one.id) + u"/cover"_s},
            {u"inProgress"_s, one.holding.readStatus == Api::ReadStatus::InProgress},
            {u"howFarRead"_s, Api::howFarRead(one)},
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

int Series::volumeCount() const
{
    return m_one.has_value() ? m_one->holding.ownedVolumes : 0;
}

int Series::arcCount() const
{
    return m_one.has_value() ? int(m_one->counts.arcs) : 0;
}

void Series::point(const QString &identifier)
{
    m_id = identifier;
    reload();
}

void Series::forget()
{
    // The generation moves, so an answer still on the wire for the series being left is
    // dropped rather than landing on the page after it has been let go.
    ++m_generation;
    if (m_id.isEmpty() && !m_one.has_value())
        return;
    m_id.clear();
    m_one.reset();
    m_siblings.clear();
    m_loading = false;
    m_trouble.clear();
    emit changed();
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

void Series::take(const Api::Series &fresh)
{
    bool moved = false;
    if (m_one && m_one->id == fresh.id) {
        m_one = fresh;
        moved = true;
    }
    for (Api::Series &one : m_siblings) {
        if (one.id == fresh.id) {
            one = fresh;
            moved = true;
        }
    }
    if (moved)
        emit changed();
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
