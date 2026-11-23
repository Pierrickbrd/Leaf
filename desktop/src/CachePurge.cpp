#include "CachePurge.h"

#include "Words.h"

#include <QDebug>
#include <QJsonObject>

using namespace Qt::StringLiterals;

CachePurge *CachePurge::create(QQmlEngine *engine, QJSEngine *)
{
    auto *server = engine->singletonInstance<Server *>(qmlTypeId("Leaf", 1, 0, "Server"));
    if (!server) {
        qWarning().noquote()
            << QStringLiteral("error resolving the Server singleton — the cache cannot be "
                              "emptied");
    }
    return new CachePurge(server);
}

CachePurge::CachePurge(Server *server, QObject *parent)
    : QObject(parent)
    , m_server(server)
{
}

QString CachePurge::title() const
{
    return Words::theCache();
}

QString CachePurge::label() const
{
    return Words::purgeTheCache();
}

QString CachePurge::explained() const
{
    return Words::purgeExplained();
}

void CachePurge::purge()
{
    if (m_purging || !m_server)
        return;
    m_purging = true;
    m_sentence.clear();
    m_failed = false;
    emit changed();
    m_server->remove(u"/cache"_s, this, [this](const Server::Answer &answer) {
        m_purging = false;
        const QJsonObject said = answer.body.object();
        if (!answer.went()) {
            m_failed = true;
            m_sentence = answer.trouble;
        } else if (!said.contains(u"files"_s) || !said.contains(u"bytes"_s)) {
            // An answer that does not count what it removed is not evidence it removed it.
            m_failed = true;
            m_sentence = Words::unreadableAnswer();
        } else {
            m_sentence = Words::cachePurged(said.value(u"files"_s).toInteger(),
                                            said.value(u"bytes"_s).toInteger());
        }
        emit changed();
    });
}
