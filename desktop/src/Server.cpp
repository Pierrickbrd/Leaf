#include "Server.h"

#include "Settings.h"

#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <utility>

namespace {
/// How many requests may be waiting on the keyring at once.
///
/// Generous, because the answer is "every screen a run opens with" and there is no honest
/// number for that. It is a ceiling on a caller that does not know it is waiting — one
/// retrying on a timer appends for ever, and the whole queue is then replayed at once the
/// moment the keyring answers — not a budget anybody is meant to spend.
constexpr int MostWaiting = 32;
} // namespace

Server::Server(Settings *settings, QObject *parent) : QObject(parent), m_settings(settings)
{
    // A key that changes is a reason to try again, and the only one. Nothing else the client
    // could do would make a refusal into an acceptance.
    connect(settings, &Settings::changed, this, [this] {
        m_stopped.clear();
        // And whatever was asked for before the settings had loaded is asked again. Taken
        // out of the list before it is walked: a request that still finds them unloaded goes
        // straight back in, and iterating a list being appended to does not end.
        const QList<Waiting> waiting = std::exchange(m_waiting, {});
        for (const Waiting &one : waiting) {
            // The screen that asked is gone, so there is nobody to answer. Silence is the
            // whole of it: `then` captured that screen, and calling it now reads freed
            // memory.
            if (!one.caller) {
                continue;
            }
            get(one.path, one.query, one.caller, one.then);
        }
    });
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

void Server::get(const QString &path, const QObject *caller,
                 std::function<void(const Answer &)> then)
{
    get(path, QUrlQuery(), caller, std::move(then));
}

void Server::get(const QString &path, const QUrlQuery &query, const QObject *caller,
                 std::function<void(const Answer &)> then)
{
    // Nothing given is this client itself: the answer then stands for as long as the thing
    // that would send it, which is what every caller had before there was anything to say.
    QPointer<const QObject> alive(caller != nullptr ? caller : this);

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

    // Nothing is decided while the keyring is still answering — see `Waiting`. Past this
    // line `missing()` speaks for certain, because it is silent only before `loaded`.
    if (!m_settings->loaded()) {
        if (m_waiting.size() >= MostWaiting) {
            // Said, and not held. A queue that quietly stops taking requests is a screen
            // waiting on an answer that was never going to come.
            then({0, {},
                  tr("Too many requests are already waiting for Leaf to open your library.")});
            return;
        }
        m_waiting.append({path, query, alive, std::move(then)});
        return;
    }

    const QString address = tidy(m_settings->address());
    if (address.isEmpty()) {
        // `trouble` empty is what `went()` reads as success, and nothing was sent at all: a
        // caller took a null document for an answer and showed an empty shelf as though the
        // server had said the library was empty, with nothing to display as an error. A
        // request that did not happen always says why — so the sentence below stands even
        // for the case `missing()` should have covered and did not.
        const QString said = m_settings->missing();
        then({0, {},
              said.isEmpty() ? tr("Leaf does not know where your library is.") : said});
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
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, alive, then = std::move(then)] {
                reply->deleteLater();
                const Answer answer = read(reply);
                if (answer.status == 403) {
                    m_stopped = answer.trouble;
                } else if (answer.status == 429) {
                    const int seconds = reply->rawHeader("Retry-After").toInt();
                    m_notBefore =
                        QDateTime::currentDateTime().addSecs(seconds > 0 ? seconds : 60);
                }
                // The two lines above are this client's own bookkeeping and stand whoever
                // asked: a refusal is a refusal, and a 429 is owed by every request after it.
                // The answer itself is the caller's, and there is nobody to hand it to.
                if (!alive) {
                    return;
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
