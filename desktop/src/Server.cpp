#include "Server.h"

#include "Settings.h"

#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

Server::Server(Settings *settings, QObject *parent) : QObject(parent), m_settings(settings)
{
    // A key that changes is a reason to try again, and the only one. Nothing else the client
    // could do would make a refusal into an acceptance.
    connect(settings, &Settings::changed, this, [this] { m_stopped.clear(); });
}

QString Server::tidy(const QString &address)
{
    QString out = address.trimmed();
    if (out.isEmpty()) {
        return out;
    }
    if (!out.contains(QStringLiteral("://"))) {
        // https, not http. A key travels on every single request, and a server reached
        // without a proxy in front speaks TLS itself — see the server's own net::tls.
        out.prepend(QStringLiteral("https://"));
    }
    while (out.endsWith(u'/')) {
        out.chop(1);
    }
    return out;
}

void Server::get(const QString &path, std::function<void(const Answer &)> then)
{
    get(path, QUrlQuery(), std::move(then));
}

void Server::get(const QString &path, const QUrlQuery &query,
                 std::function<void(const Answer &)> then)
{
    // A caller that built its own query string has already lost the ampersands. Saying so is
    // better than encoding it twice or sending it broken.
    if (path.contains(u'?')) {
        then({0, {}, tr("A query has to be given apart from the path, not spliced into it.")});
        return;
    }
    if (!m_stopped.isEmpty()) {
        then({0, {}, m_stopped});
        return;
    }
    if (m_notBefore.isValid() && QDateTime::currentDateTime() < m_notBefore) {
        then({0, {},
              tr("Waiting %1 more seconds before asking again.")
                  .arg(QDateTime::currentDateTime().secsTo(m_notBefore))});
        return;
    }

    const QString address = tidy(m_settings->address());
    if (address.isEmpty()) {
        then({0, {}, m_settings->missing()});
        return;
    }

    QUrl url(address + path);
    if (!query.isEmpty()) {
        url.setQuery(query);
    }
    QNetworkRequest request{url};
    request.setRawHeader("X-Leaf-Key", m_settings->key().toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, then = std::move(then)] {
        reply->deleteLater();
        const Answer answer = read(reply);
        if (answer.status == 403) {
            m_stopped = answer.trouble;
        } else if (answer.status == 429) {
            const int seconds = reply->rawHeader("Retry-After").toInt();
            m_notBefore = QDateTime::currentDateTime().addSecs(seconds > 0 ? seconds : 60);
        }
        then(answer);
    });
}

Server::Answer Server::read(QNetworkReply *reply) const
{
    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    // Nothing came back at all: the machine is off, the name does not resolve, the
    // certificate is not the one pinned. Said as what it is rather than as a code.
    if (status == 0) {
        return {0, {}, tr("The server could not be reached — %1").arg(reply->errorString())};
    }

    const QByteArray bytes = reply->readAll();
    QJsonParseError fault{};
    const QJsonDocument body = QJsonDocument::fromJson(bytes, &fault);

    if (status >= 200 && status < 300) {
        if (fault.error != QJsonParseError::NoError && !bytes.isEmpty()) {
            return {status, {}, tr("The server answered something this cannot read.")};
        }
        return {status, body, {}};
    }

    // The server says what went wrong in the body, and it is better wording than anything
    // invented here — it knows which of the three reasons a 403 had.
    const QString said = body.isObject()
                             ? body.object().value(QStringLiteral("error")).toString()
                             : QString();
    switch (status) {
    case 403:
        return {status, body,
                said.isEmpty() ? tr("The key was refused.")
                               : tr("The key was refused: %1").arg(said)};
    case 429:
        return {status, body,
                tr("Too many wrong keys have been tried. Wait %1 seconds.")
                    .arg(QString::fromUtf8(reply->rawHeader("Retry-After")))};
    case 404:
        return {status, body, said.isEmpty() ? tr("There is no such thing here.") : said};
    default:
        return {status, body,
                said.isEmpty()
                    ? tr("The server answered %1.").arg(status)
                    : tr("The server answered %1: %2").arg(status).arg(said)};
    }
}
