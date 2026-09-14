#include "Covers.h"

#include "Server.h"
#include "Settings.h"

#include <QDebug>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QQmlEngine>

using namespace Qt::StringLiterals;

namespace {

/// The port a scheme means when nobody wrote one.
int usual(const QString &scheme)
{
    if (scheme == u"https"_s) {
        return 443;
    }
    if (scheme == u"http"_s) {
        return 80;
    }
    return -1;
}

/// The engine's network manager, with one thing added.
class Keyed : public QNetworkAccessManager
{
public:
    Keyed(Covers *covers, QObject *parent)
        : QNetworkAccessManager(parent)
        , m_covers(covers)
    {
    }

protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request,
                                 QIODevice *outgoing) override
    {
        QNetworkRequest carried(request);
        // Asked of the factory each time rather than held: the address and the key both change
        // — the keyring answers after the run has started, and the settings screen can change
        // them again — and a manager built at the first cover would carry whatever was true
        // then for the rest of the session.
        if (const Settings *settings = m_covers ? m_covers->settings() : nullptr) {
            if (Covers::sameServer(request.url(), QUrl(Server::tidy(settings->address())))) {
                carried.setRawHeader(Server::KeyHeader, settings->key().toUtf8());
            }
        }
        return QNetworkAccessManager::createRequest(operation, carried, outgoing);
    }

private:
    QPointer<Covers> m_covers;
};

} // namespace

Covers::Covers(QQmlEngine *engine, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
{
}

Covers::Covers(Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
}

QNetworkAccessManager *Covers::create(QObject *parent)
{
    return new Keyed(this, parent);
}

Settings *Covers::settings()
{
    if (!m_settings && m_engine) {
        m_settings =
            m_engine->singletonInstance<Settings *>(qmlTypeId("Leaf", 1, 0, "Settings"));
        if (!m_settings) {
            // Nothing else catches this. Every cover would come back 403 and the grid would
            // look like a library of broken files rather than a client that lost its key.
            qWarning().noquote()
                << QStringLiteral("error resolving the Settings singleton — covers will be "
                                  "asked for without a key");
        }
    }
    return m_settings;
}

bool Covers::sameServer(const QUrl &one, const QUrl &other)
{
    // No host is not a host that matches no host. An unconfigured client has an empty address,
    // and the engine fetches plenty that carries no host either — `qrc:`, `file:`, the empty
    // `source:` of a tile whose row has not arrived. Every one of those would be told the key.
    if (one.host().isEmpty() || other.host().isEmpty()) {
        return false;
    }
    // The port each scheme implies, filled in on both sides: otherwise `https://leaf.local`
    // and `https://leaf.local:443` are two different servers, and the key rides to one of them
    // and not the other for a reason nobody could see from either string.
    return one.scheme() == other.scheme()
        && one.host().compare(other.host(), Qt::CaseInsensitive) == 0
        && one.port(usual(one.scheme())) == other.port(usual(other.scheme()));
}
