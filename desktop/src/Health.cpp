#include "Health.h"

#include "Words.h"

#include <QDebug>

using namespace Qt::StringLiterals;

Health *Health::create(QQmlEngine *engine, QJSEngine *)
{
    auto *server = engine->singletonInstance<Server *>(qmlTypeId("Leaf", 1, 0, "Server"));
    if (!server) {
        qWarning().noquote()
            << QStringLiteral("error resolving the Server singleton — the settings screen "
                              "will say nothing about the server");
    }
    return new Health(server);
}

Health::Health(Server *server, QObject *parent)
    : QObject(parent)
    , m_server(server)
{
}

QString Health::title() const
{
    return Words::theServer();
}

QString Health::answering() const
{
    return Words::answering(m_reachable);
}

QString Health::connected() const
{
    return Words::connected(m_reachable);
}

QString Health::versions() const
{
    return Words::apiVersion(m_said.api, m_said.format);
}

QString Health::holds() const
{
    return Words::libraryHolds(m_said.library);
}

QString Health::sharedFolder() const
{
    return Words::sharedFolder(m_said.localDrop);
}

void Health::ask()
{
    ++m_generation;
    if (!m_server) {
        m_reachable = false;
        m_trouble = Words::notSetUp(Words::Asking::State);
        emit changed();
        return;
    }

    m_asking = true;
    emit changed();

    const int mine = m_generation;
    m_server->get(u"/health"_s, this, [this, mine](const Server::Answer &answer) {
        if (mine == m_generation)
            took(answer);
    });
}

void Health::took(const Server::Answer &answer)
{
    m_asking = false;
    // What was known stays known. A server that stops answering has not changed version,
    // and blanking the screen would say it had.
    if (!answer.went()) {
        m_reachable = false;
        m_trouble = answer.trouble;
        emit changed();
        return;
    }

    const Api::Read<Api::Health> read = Api::health(answer.body.object());
    if (!read.ok()) {
        m_reachable = false;
        m_trouble = read.trouble;
        emit changed();
        return;
    }

    m_said = *read.value;
    m_reachable = true;
    m_trouble.clear();
    emit changed();
}
