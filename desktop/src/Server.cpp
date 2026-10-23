#include "Server.h"

#include "Pinning.h"
#include "Settings.h"
#include "Words.h"

#include <QDebug>

#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
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
    // Asked at each handshake rather than copied now: the pin changes with the link, and a
    // manager holding the one it was built with would refuse the server it was just moved to.
    Pinning::guard(m_network, [settings] { return settings->fingerprint(); });

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
            send({one.verb, one.path, QUrlQuery(one.encodedQuery), one.body, one.range,
                  one.octets},
                 one.caller, one.then, one.reading);
        }
    });
}

Server *Server::create(QQmlEngine *engine, QJSEngine *)
{
    // Resolved here rather than handed in: a QML singleton is built by the engine with nothing
    // but the engine to go on. The type id resolves by now — QML asks for this the first time
    // a screen names it, long after the module was registered, which is the same reason `Boot`
    // reads the `Theme` back *after* `engine.load(...)` and not before.
    auto *settings =
        engine->singletonInstance<Settings *>(qmlTypeId("Leaf", 1, 0, "Settings"));
    if (!settings) {
        // Said out loud rather than crashed on: nothing covers a registration that stopped
        // resolving, and a client that cannot reach its own settings has nothing to ask
        // anybody. `Shelf` turns a missing server into a sentence on the screen.
        qWarning().noquote()
            << QStringLiteral("error resolving the Settings singleton — nothing can be asked "
                              "of the server");
        return nullptr;
    }
    return new Server(settings);
}

QString Server::address() const
{
    return tidy(m_settings->address());
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

void Server::post(const QString &path, const QByteArray &body, const QObject *caller,
                  std::function<void(const Answer &)> then)
{
    send({"POST", path, QUrlQuery(), body, {}}, caller, std::move(then));
}

void Server::get(const QString &path, const QUrlQuery &query, const QObject *caller,
                 std::function<void(const Answer &)> then)
{
    send({"GET", path, query, {}, {}}, caller, std::move(then));
}

void Server::remove(const QString &path, const QObject *caller,
                    std::function<void(const Answer &)> then)
{
    send({"DELETE", path, QUrlQuery(), {}, {}}, caller, std::move(then));
}

void Server::remove(const QString &path, const QByteArray &body, const QObject *caller,
                    std::function<void(const Answer &)> then)
{
    send({"DELETE", path, QUrlQuery(), body, {}, false}, caller, std::move(then));
}

void Server::patch(const QString &path, const QByteArray &body, const QObject *caller,
                   std::function<void(const Answer &)> then)
{
    send({"PATCH", path, QUrlQuery(), body, {}}, caller, std::move(then));
}

void Server::getFile(const QString &path, const QObject *caller,
                     std::function<void(const Answer &)> then)
{
    send({"GET", path, QUrlQuery(), {}, {}}, caller, std::move(then), Reading::Bytes);
}

void Server::put(const QString &path, const QByteArray &body, qint64 from, qint64 whole,
                 const QObject *caller, std::function<void(const Answer &)> then)
{
    put(path, QUrlQuery(), body, from, whole, caller, std::move(then));
}

void Server::put(const QString &path, const QUrlQuery &query, const QByteArray &body,
                 qint64 from, qint64 whole, const QObject *caller,
                 std::function<void(const Answer &)> then)
{
    // `bytes 104857600-134217727/134217728` — inclusive at both ends, which is why the last
    // byte is one before the sum and not the sum. An empty body carries no range at all:
    // there is no such thing as a range of nothing, and the server reads its absence as
    // "from zero".
    QByteArray range;
    if (!body.isEmpty()) {
        range = "bytes " + QByteArray::number(from) + '-'
                + QByteArray::number(from + body.size() - 1) + '/'
                + QByteArray::number(whole);
    }
    send({"PUT", path, query, body, range, true}, caller, std::move(then));
}

void Server::set(const QString &path, const QByteArray &body, const QObject *caller,
                 std::function<void(const Answer &)> then)
{
    send({"PUT", path, QUrlQuery(), body, {}, false}, caller, std::move(then));
}

void Server::send(const Sending &what, const QObject *caller,
                  std::function<void(const Answer &)> then, Reading reading)
{
    // Nothing given is this client itself: the answer then stands for as long as the thing
    // that would send it, which is what every caller had before there was anything to say.
    QPointer<const QObject> alive(caller != nullptr ? caller : this);

    // A caller that built its own query string has already lost the ampersands. Saying so
    // is better than encoding it twice or sending it broken.
    if (what.path.contains(u'?')) {
        then({0, {}, Words::queryBelongsApart()});
        return;
    }
    if (!m_stopped.isEmpty()) {
        then({0, {}, m_stopped});
        return;
    }
    if (m_notBefore.isValid() && QDateTime::currentDateTime() < m_notBefore) {
        then({0, {},
              Words::waitingBeforeAsking(
                  int(QDateTime::currentDateTime().secsTo(m_notBefore)))});
        return;
    }

    // Nothing is decided while the keyring is still answering — see `Waiting`. Past this
    // line `missing()` speaks for certain, because it is silent only before `loaded`.
    if (!m_settings->loaded()) {
        if (m_waiting.size() >= MostWaiting) {
            // Said, and not held. A queue that quietly stops taking requests is a screen
            // waiting on an answer that was never going to come.
            then({0, {},
                  Words::tooManyWaiting()});
            return;
        }
        // Encoded here and parsed back on the way out — see `Waiting::encodedQuery` for why
        // it is not the `QUrlQuery` itself. `FullyEncoded` is the only form that survives the
        // round trip: a `PrettyDecoded` query hands its own ampersands back to the parser.
        m_waiting.append({what.verb, what.path, what.query.toString(QUrl::FullyEncoded),
                          what.body, what.range, what.octets, reading, alive,
                          std::move(then)});
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
              said.isEmpty() ? Words::noLibrary() : said});
        return;
    }

    QUrl url(address + what.path);
    if (!what.query.isEmpty()) {
        url.setQuery(what.query);
    }
    QNetworkRequest request = Pinning::request(url);
    request.setRawHeader(KeyHeader, m_settings->key().toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    // A body means something to send, and a server that reads a length. Set even when the
    // body is empty: `POST /scan` carries nothing, and a POST with no length at all is one
    // some proxies hold open waiting for it.
    if (what.verb != "GET") {
        // Part of a file is never JSON, and everything else this client sends is.
        request.setHeader(QNetworkRequest::ContentTypeHeader,
                          what.octets ? QByteArrayLiteral("application/octet-stream")
                                      : QByteArrayLiteral("application/json"));
    }
    if (!what.range.isEmpty()) {
        request.setRawHeader("Content-Range", what.range);
    }
    Pinning::renew(m_network, m_settings->fingerprint());
    QNetworkReply *reply =
        what.verb == "GET" ? m_network.get(request)
                           : m_network.sendCustomRequest(request, what.verb, what.body);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, what, alive, reading, key = m_settings->key(), sentTo = address,
             then = std::move(then)] {
                reply->deleteLater();
                // Cancelled because the pin changed — a device just linked, or moved — and
                // not answered: shown, it read « Le serveur est injoignable — Operation
                // canceled » about a server nobody had asked anything yet. A question is
                // asked again of whatever the settings now name. A turn later and not from
                // here: this runs inside `renew`, whose next step drops the connections, and
                // a request started now would be orphaned by it and never finish.
                //
                // Only a question. A command may already have reached the server before the
                // cancel, and sending it twice is not this file's call to make.
                if (Pinning::renewed(reply) && what.verb == "GET") {
                    QTimer::singleShot(0, this, [this, what, alive, reading, then] {
                        if (alive) {
                            send(what, alive.data(), then, reading);
                        }
                    });
                    return;
                }
                // A command is answered — but not as a failure of the server, which never
                // saw the cancel and may well have done what was asked. Said as what it is,
                // so the screen that sent it does not read « injoignable » about a library
                // that answers.
                if (Pinning::renewed(reply)) {
                    if (alive) {
                        then({0, {}, Words::cutShortByANewPin()});
                    }
                    return;
                }
                const Answer answer = read(reply, reading);
                // Only for the key still held, at the address still used: a refusal on its
                // way back for a key since replaced, or from the address a device has just
                // moved away from, says nothing about the link as it now stands.
                const bool current =
                    key == m_settings->key() && sentTo == tidy(m_settings->address());
                const bool keyRefused =
                    answer.status == 403
                    && answer.body.object().value(QStringLiteral("error")).toString()
                           == QStringLiteral("unknown key");
                // A device that must choose is refused by every route but two, and the refusal
                // is the same wherever it lands: said here, once, so that a screen which only
                // asked for a shelf does not have to know what to do with it.
                if (answer.status == 409 && current
                    && answer.body.object().value(QStringLiteral("error")).toString()
                           == QStringLiteral("choose a profile")) {
                    emit mustChoose();
                }
                // Only the key's own refusal stops the client — the server's answer to no key
                // or one it does not know, which it counts towards its throttle. A route that
                // refuses this key a right (a read-only key asking to import, or to make a
                // code) answers that request alone: the key still opens everything else.
                if (keyRefused && current) {
                    m_stopped = answer.trouble;
                    emit keyUnknown();
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

Server::Answer Server::read(QNetworkReply *reply, Reading reading) const
{
    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    // Refused for its certificate, which is not a server that is down: the server answered,
    // as somebody this device was not linked to. Said before anything else, because Qt's own
    // words for it — « the handshake failed » — read as a network hiccup worth retrying.
    if (Pinning::mismatched(reply)) {
        return {0, {}, Words::certificateMismatch()};
    }

    // Nothing came back at all: the machine is off, the name does not resolve, the system
    // refused a certificate nothing was pinned for. Said as what it is rather than as a code.
    if (status == 0) {
        return {0, {}, Words::unreachable(reply->errorString())};
    }

    const QByteArray bytes = reply->readAll();
    // An archive is not a document, and reading it as one would turn the one answer that
    // arrived whole into « the server said something this client cannot read ». A refusal
    // still carries its sentence in JSON, so only the successful answer is left alone.
    if (reading == Reading::Bytes && status >= 200 && status < 300) {
        return {status, {}, {}, bytes};
    }

    QJsonParseError fault{};
    const QJsonDocument body = QJsonDocument::fromJson(bytes, &fault);

    if (status >= 200 && status < 300) {
        if (fault.error != QJsonParseError::NoError && !bytes.isEmpty()) {
            return {status, {}, Words::unreadableAnswer()};
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
                Words::keyRefused(said)};
    case 429: {
        const QString seconds = QString::fromUtf8(reply->rawHeader("Retry-After"));
        return {status, body, Words::tooManyWrongKeys(seconds), {}, seconds};
    }
    case 404:
        return {status, body, said.isEmpty() ? Words::noSuchThing() : said};
    default:
        return {status, body,
                Words::serverAnswered(status, said)};
    }
}
