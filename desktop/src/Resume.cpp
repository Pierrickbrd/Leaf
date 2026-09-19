#include "Resume.h"

#include "Words.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonValue>
#include <QUrlQuery>

#include <algorithm>

using namespace Qt::StringLiterals;

Resume *Resume::create(QQmlEngine *engine, QJSEngine *)
{
    auto *server = engine->singletonInstance<Server *>(qmlTypeId("Leaf", 1, 0, "Server"));
    if (!server) {
        qWarning().noquote()
            << QStringLiteral("error resolving the Server singleton — the resume band will "
                              "stay empty");
    }
    return new Resume(server);
}

Resume::Resume(Server *server, QObject *parent)
    : QObject(parent)
    , m_server(server)
{
}

QString Resume::seriesId() const
{
    return m_card ? m_card->seriesId : QString();
}

QString Resume::seriesName() const
{
    return m_card ? m_card->seriesName : QString();
}

QString Resume::entryId() const
{
    return m_card ? m_card->entryId : QString();
}

QString Resume::cover() const
{
    if (!m_card || !m_server)
        return {};
    // The entry's own cover, not the series'. A series' cover is its first volume's, so a
    // band offering tome 12 would show tome 1 — the one picture that says "you are not where
    // you think you are".
    return m_server->address() + u"/entries/"_s + m_card->entryId + u"/cover"_s;
}

QString Resume::where() const
{
    if (!m_card)
        return {};
    return Words::where(m_card->entryKind, m_card->entryNumber, m_card->page,
                        m_card->pageCount, m_card->chapterLabel);
}

QString Resume::whereShort() const
{
    if (!m_card)
        return {};
    return Words::whereShort(m_card->entryKind, m_card->entryNumber, m_card->page,
                             m_card->pageCount);
}

QString Resume::action() const
{
    return m_card ? Words::resumeAction(m_card->reason) : QString();
}

bool Resume::hasProgress() const
{
    return m_card && m_card->page.has_value() && m_card->pageCount > 0;
}

qreal Resume::progress() const
{
    if (!hasProgress())
        return 0.0;
    return std::clamp(qreal(*m_card->page) / qreal(m_card->pageCount), qreal(0), qreal(1));
}

void Resume::reload()
{
    ++m_generation;
    m_card.reset();
    m_trouble.clear();

    if (!m_server) {
        m_loading = false;
        m_trouble = Words::notSetUp(Words::Asking::Resume);
        emit changed();
        return;
    }

    m_loading = true;
    emit changed();

    QUrlQuery query;
    query.addQueryItem(u"limit"_s, u"1"_s);
    const int mine = m_generation;
    m_server->get(u"/next"_s, query, this, [this, mine](const Server::Answer &answer) {
        if (mine == m_generation)
            took(answer);
    });
}

void Resume::took(const Server::Answer &answer)
{
    m_loading = false;

    if (!answer.went()) {
        m_trouble = answer.trouble;
        emit changed();
        return;
    }

    if (!answer.body.isArray()) {
        m_trouble = QStringLiteral("upNext: expected an array");
        emit changed();
        return;
    }

    const QJsonArray offered = answer.body.array();
    if (offered.isEmpty()) {
        m_trouble.clear();
        emit changed();
        return;
    }
    if (!offered.first().isObject()) {
        m_trouble = QStringLiteral("upNext[0]: expected an object");
        emit changed();
        return;
    }

    const Api::Read<Api::UpNext> read = Api::upNext(offered.first().toObject());
    if (!read.ok()) {
        m_trouble = read.trouble;
        emit changed();
        return;
    }

    m_card = *read.value;
    m_trouble.clear();
    emit changed();
}
