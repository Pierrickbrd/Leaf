#include "Bond.h"

#include "Api.h"
#include "Linking.h"
#include "Pinning.h"
#include "Server.h"
#include "Settings.h"
#include "Words.h"

#include <QClipboard>
#include <QDateTime>
#include <QDebug>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslError>
#include <QUrlQuery>

#include <utility>

using namespace Qt::StringLiterals;

namespace {

/// How long a new address may take to answer, for the reason `Linking` gives: an address
/// this device cannot route to is silent, not refused, and the system waits minutes.
constexpr int Patience = 15000;

/// Where a tried address says it presented a certificate this device will not take. On the
/// reply, as `Pinning` keeps its own mark.
constexpr const char *Refused = "leafBondRefused";

/// The loopback, the one place plain http is allowed: nothing on it leaves the machine.
bool loopback(const QString &host)
{
    return host == u"127.0.0.1"_s || host == u"::1"_s
           || host.compare(u"localhost"_s, Qt::CaseInsensitive) == 0;
}

/// How long a code lives, as the server makes it. Only a fallback: the code's own expiry is
/// what is counted down, unless this machine's clock disagrees with the server's so much that
/// a code just made reads as already gone, or as living longer than any code does.
constexpr qint64 CodeLifetime = 10 * 60 * 1000;

/// How often a relayed code is asked about. Two seconds: « PC du salon relié » arrives while
/// the person who linked it is still looking at this screen, and a code is asked about at most
/// three hundred times.
constexpr int FollowEvery = 2000;

/// How often the countdown looks at the clock. Under a second, so that a second is never shown
/// for nearly two, and the link goes when the code does rather than up to a second after.
constexpr int TickEvery = 250;

/// The certificate `reply` presented, as its fingerprint; empty when there was none.
QString presented(const QNetworkReply *reply)
{
    const QSslCertificate certificate = reply->sslConfiguration().peerCertificate();
    return certificate.isNull() ? QString() : Pinning::fingerprint(certificate);
}

/// The link another device opens: this device's own address and pin, around the server's
/// code. Written for `Link::read` to read back into that same address — the port left out
/// only where it reads a missing one as the same port.
QString linkTo(const QString &address, const QString &fingerprint, const QString &code)
{
    const QUrl at(address);
    const QString host = at.host();
    // QUrl hands an IPv6 host back without its brackets, and a link needs them.
    QString link = u"leaf://"_s + (host.contains(u':') ? u"[%1]"_s.arg(host) : host);
    // No port in a link means 443, except on the loopback, which is read as plain http.
    const int port = at.port();
    if (port != -1 && !(port == 443 && !loopback(host))) {
        link += u":"_s + QString::number(port);
    }
    link += u"/lier/"_s + code;
    if (!fingerprint.isEmpty()) {
        link += u"?empreinte="_s + fingerprint;
    }
    return link;
}

/// The path that follows or withdraws a code. Its id is the server's, and goes in encoded all
/// the same: a path is not the place to find out what an id may hold.
QString codePath(const QString &id)
{
    return u"/link/codes/"_s + QString::fromUtf8(QUrl::toPercentEncoding(id));
}

} // namespace

Bond::Bond(Settings *settings, Server *server, Linking *linking, QObject *parent)
    : QObject(parent), m_settings(settings), m_server(server), m_linking(linking)
{
    // The address, the name and the date are the settings' own; this only words them.
    connect(settings, &Settings::changed, this, &Bond::changed);
    // What `Settings` could not do — a name it could not write, an address the environment
    // overrides, a key the keyring kept — is said on the tab where it was asked for.
    connect(settings, &Settings::trouble, this, &Bond::say);
    if (server) {
        connect(server, &Server::keyUnknown, this, &Bond::revoked);
    }

    // A move is judged here and not by `Pinning::guard`, which refuses any certificate but
    // the pinned one. Moving may meet one of three: the pinned certificate (the same Leaf at
    // another address: kept), one an authority signed (a proxy now stands in front: the pin
    // goes, as it would have had the device been linked there), or another nobody signed —
    // which is refused inside the handshake, before the key is written.
    connect(&m_network, &QNetworkAccessManager::sslErrors, this,
            [this](QNetworkReply *reply, const QList<QSslError> &errors) {
                if (!m_pin.isEmpty() && presented(reply) == m_pin) {
                    reply->ignoreSslErrors(errors);
                    return;
                }
                reply->setProperty(Refused, true);
            });
    // Direct: the handshake is over and the request not yet written, so what is decided here
    // is what the key is sent under.
    connect(
        &m_network, &QNetworkAccessManager::encrypted, this,
        [this](QNetworkReply *reply) {
            m_newPin = !m_pin.isEmpty() && presented(reply) == m_pin ? m_pin : QString();
        },
        Qt::DirectConnection);

    m_relayClock.setInterval(TickEvery);
    connect(&m_relayClock, &QTimer::timeout, this, &Bond::tick);
    m_relayFollow.setInterval(FollowEvery);
    connect(&m_relayFollow, &QTimer::timeout, this, &Bond::follow);
}

Bond *Bond::create(QQmlEngine *engine, QJSEngine *)
{
    // Resolved here for the reason `Server::create` gives: a QML singleton is built with
    // nothing but the engine to go on.
    auto *settings =
        engine->singletonInstance<Settings *>(qmlTypeId("Leaf", 1, 0, "Settings"));
    auto *server = engine->singletonInstance<Server *>(qmlTypeId("Leaf", 1, 0, "Server"));
    auto *linking = engine->singletonInstance<Linking *>(qmlTypeId("Leaf", 1, 0, "Linking"));
    if (!settings || !server) {
        qWarning().noquote()
            << u"error resolving the Settings or Server singleton — the library tab is empty"_s;
        return nullptr;
    }
    return new Bond(settings, server, linking);
}

QString Bond::address() const
{
    return Server::tidy(m_settings->address());
}

QString Bond::deviceName() const
{
    return m_settings->deviceName();
}

QString Bond::linkedOn() const
{
    return Words::linkedOn(m_settings->linkedAt());
}

QString Bond::title() const
{
    return Words::linkedLibrary();
}

QString Bond::deviceLabel() const
{
    return Words::thisDevice();
}

QString Bond::linkedLabel() const
{
    return Words::linkedOnLabel();
}

QString Bond::renameLabel() const
{
    return Words::renameDevice();
}

QString Bond::moveLabel() const
{
    return Words::changeAddress();
}

QString Bond::movePlaceholder() const
{
    return Words::addressPlaceholder();
}

QString Bond::moveAction() const
{
    return Words::tryThisAddress();
}

QString Bond::unlinkLabel() const
{
    return Words::unlinkDevice();
}

QString Bond::unlinkQuestion() const
{
    return Words::unlinkQuestion();
}

QString Bond::unlinkWhat() const
{
    return Words::unlinkWhat(m_settings->deviceName(), QUrl(address()).host());
}

QString Bond::unlinkComeBack() const
{
    return Words::unlinkComeBack();
}

QString Bond::unlinkIfSilent() const
{
    return Words::unlinkIfSilent(m_settings->deviceName());
}

QString Bond::unlinkGo() const
{
    return Words::unlinkGo();
}

QString Bond::cancelLabel() const
{
    return Words::cancel();
}

QString Bond::relayTimeLeft() const
{
    return m_relaySeconds > 0 ? Words::relayTimeLeft(m_relaySeconds) : QString();
}

QString Bond::relayedSaid() const
{
    return m_relayed.isEmpty() ? QString() : Words::deviceRelayed(m_relayed);
}

QString Bond::relayLabel() const
{
    return Words::relayAnother();
}

QString Bond::relayIntro() const
{
    return Words::relayIntro();
}

QString Bond::copyLabel() const
{
    return Words::copyTheLink();
}

QString Bond::copiedLabel() const
{
    return Words::linkCopied();
}

QString Bond::anotherLinkLabel() const
{
    return Words::anotherLink();
}

QString Bond::closeLabel() const
{
    return Words::closeThePanel();
}

void Bond::rename(const QString &name)
{
    if (m_busy || !m_server) {
        return;
    }
    const QString wanted = name.trimmed();
    if (wanted.isEmpty()) {
        say(Words::nameNotAccepted());
        return;
    }
    if (wanted == m_settings->deviceName()) {
        say({});
        return;
    }
    setBusy(true);
    const QByteArray body =
        QJsonDocument(QJsonObject{{u"name"_s, wanted}}).toJson(QJsonDocument::Compact);
    m_server->patch(u"/device"_s, body, this, [this, wanted](const Server::Answer &answer) {
        m_busy = false;
        if (answer.went()) {
            const Api::Read<Api::Device> said = Api::device(answer.body.object());
            if (!said.ok()) {
                say(Words::unreadableAnswer());
                return;
            }
            // Cleared first, so that a name the file would not take says so after it.
            m_sentence.clear();
            // The server's spelling, not the one typed: it is the one every device sees.
            m_settings->rename(said.value->name);
            emit changed();
            return;
        }
        const QJsonObject body = answer.body.object();
        if (answer.status == 409 && body.value(u"error"_s).toString() == u"name"_s) {
            say(Words::nameTaken(body.value(u"name"_s).toString(wanted)));
            return;
        }
        if (answer.status == 400) {
            say(Words::nameNotAccepted());
            return;
        }
        // Everything else, in the words `Server` found for it — a pin that changed under the
        // request included, which it says without blaming the server.
        say(answer.trouble);
    });
}

void Bond::move(const QString &typed)
{
    if (m_busy) {
        return;
    }
    // Refused before anything is sent: the environment would go on naming the old address,
    // and the key would have been shown to the new one for nothing.
    if (m_settings->storage() == Settings::Storage::Environment) {
        say(Words::settingsFromEnvironment());
        return;
    }
    const QString tidied = Server::tidy(typed);
    const QUrl address(tidied, QUrl::StrictMode);
    const QString scheme = address.scheme();
    if (tidied.isEmpty() || !address.isValid() || address.host().isEmpty()
        || (scheme != u"https"_s && scheme != u"http"_s) || !address.path().isEmpty()
        || address.hasQuery() || address.hasFragment() || !address.userInfo().isEmpty()) {
        say(Words::notAnAddress());
        return;
    }
    if (scheme == u"http"_s && !loopback(address.host())) {
        say(Words::httpsElsewhere());
        return;
    }

    m_pin = m_settings->fingerprint();
    // What is kept if the handshake never says otherwise. The pin itself for https, so that
    // a certificate nobody judged is never let off the pin; nothing for plain http, which
    // presents no certificate to pin.
    m_newPin = scheme == u"https"_s ? m_pin : QString();
    setBusy(true);
    say(Words::tryingTheAddress());

    drop();
    // A connection kept from an earlier try was judged then, and would carry the key without
    // a handshake to judge it now.
    m_network.clearConnectionCache();

    QUrl probe = address;
    probe.setPath(u"/series"_s);
    probe.setQuery(QUrlQuery{{u"limit"_s, u"1"_s}});
    QNetworkRequest request = Pinning::request(probe);
    request.setRawHeader(Server::KeyHeader, m_settings->key().toUtf8());
    // The key goes to the address typed and to no other, whatever that address answers.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(Patience);
    QNetworkReply *reply = m_network.get(request);
    m_trying = reply;
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, where = address.toString()] { tried(reply, where); });
}

void Bond::tried(QNetworkReply *reply, const QString &address)
{
    reply->deleteLater();
    if (reply != m_trying) {
        return;
    }
    m_trying = nullptr;
    m_busy = false;

    if (reply->property(Refused).toBool()
        || reply->error() == QNetworkReply::SslHandshakeFailedError) {
        say(Words::certificateMismatch());
        return;
    }
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    switch (status) {
    case 200:
        // Accepted: the same library. Written now, and only now.
        m_sentence.clear();
        m_settings->moveTo(QUrl(address), m_newPin);
        if (m_settings->address() != address) {
            // `Settings` refused, and `trouble` has said why.
            emit changed();
            return;
        }
        emit changed();
        emit moved();
        return;
    case 403:
        say(Words::keyUnknownThere());
        return;
    case 429:
        say(Words::tooManyWrongKeys(QString::fromUtf8(reply->rawHeader("Retry-After"))));
        return;
    case 0:
        say(Words::unreachable(reply->errorString()));
        return;
    default:
        say(Words::serverAnswered(
            status, QJsonDocument::fromJson(reply->readAll())
                        .object()
                        .value(u"error"_s)
                        .toString()));
        return;
    }
}

void Bond::unlink()
{
    if (m_busy) {
        return;
    }
    // Refused before the server is told: a key removed there while the environment goes on
    // sending it would leave this device locked out and still configured.
    if (m_settings->storage() == Settings::Storage::Environment) {
        say(Words::settingsFromEnvironment());
        return;
    }
    if (!m_server) {
        forget({});
        return;
    }
    setBusy(true);
    // Read now: forgetting takes the name with it, and the sentence afterwards needs it.
    const QString name = m_settings->deviceName();
    m_server->remove(u"/device"_s, this, [this, name](const Server::Answer &answer) {
        m_busy = false;
        // Forgotten already, by `revoked`: the server answered this very request « unknown
        // key », and said so on the link screen. A second forgetting would wipe that.
        if (!m_settings->configured()) {
            emit changed();
            return;
        }
        // Forgotten whatever came back — the confirmation said so. But a server that did not
        // confirm, or was never asked because the client was holding back, may still take
        // the key: the link screen says so, with the command that removes it.
        const bool confirmed = answer.status >= 200 && answer.status < 300;
        // A 409 is the server saying it heard, and that this key is not a device's: it came
        // from `LEAF_KEYS`, which `device revoke` does not know. Only the file removes it.
        if (answer.status == 409) {
            forget(Words::unlinkedAnOldKey());
            return;
        }
        forget(confirmed ? QString() : Words::unlinkedWithoutTheServer(name));
    });
}

void Bond::clearSentence()
{
    if (m_busy) {
        return;
    }
    say({});
}

void Bond::revoked()
{
    // Already forgotten: the refusals of the other requests that were out at the same time.
    if (!m_settings->configured()) {
        return;
    }
    if (m_settings->storage() == Settings::Storage::Environment) {
        if (!m_toldTheEnvironment) {
            m_toldTheEnvironment = true;
            say(Words::revokedUnderEnvironment());
        }
        return;
    }
    drop();
    m_busy = false;
    forget(Words::deviceRevoked());
}

void Bond::drop()
{
    if (!m_trying) {
        return;
    }
    // Disconnected before it is aborted: an aborted reply finishes on the spot, and its
    // « Operation canceled » would read as the new address not answering.
    QNetworkReply *going = m_trying;
    m_trying = nullptr;
    going->disconnect(this);
    going->abort();
    going->deleteLater();
}

bool Bond::forget(const QString &why)
{
    m_settings->forget();
    if (m_settings->configured()) {
        emit changed();
        return false;
    }
    // Nothing withdrawn: the key that would ask is gone, and the code dies with its ten
    // minutes. Only the panel lets go of it.
    m_relayed.clear();
    endRelay({});
    m_sentence.clear();
    if (m_linking) {
        m_linking->startOver(why);
    }
    emit changed();
    emit unlinked();
    return true;
}

void Bond::say(const QString &sentence)
{
    m_sentence = sentence;
    emit changed();
}

void Bond::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    emit changed();
}


void Bond::relay()
{
    if (!m_server || !m_relayId.isEmpty()) {
        return;
    }
    // Reopened while the code asked for before is still on its way: that one will do, and is
    // shown rather than withdrawn.
    if (m_relayAsking) {
        m_relayWithdrawn = false;
        return;
    }
    m_relayed.clear();
    m_relayAsking = true;
    m_relayWithdrawn = false;
    endRelay({});
    m_server->post(u"/link/codes"_s, "{}", this, [this](const Server::Answer &answer) {
        m_relayAsking = false;
        if (!answer.went()) {
            // Closed meanwhile: there is nobody left to tell.
            if (!m_relayWithdrawn) {
                endRelay(answer.trouble);
            }
            return;
        }
        const Api::Read<Api::IssuedCode> read = Api::issuedCode(answer.body.object());
        if (!read.ok()) {
            if (!m_relayWithdrawn) {
                endRelay(Words::unreadableAnswer());
            }
            return;
        }
        issued(read.value->id, read.value->code, read.value->expiresAt);
    });
}

void Bond::issued(const QString &id, const QString &code, qint64 expiresAt)
{
    // The panel closed while the code was being made: it is withdrawn before anybody saw it.
    if (std::exchange(m_relayWithdrawn, false)) {
        withdraw(id);
        return;
    }
    m_relayId = id;
    m_relayLink = linkTo(Server::tidy(m_settings->address()), m_settings->fingerprint(), code);
    // Counted on this machine's steady clock from here on, so that the clock being set while
    // the panel is open does not stretch or cut the code short.
    qint64 left = expiresAt - QDateTime::currentMSecsSinceEpoch();
    if (left <= 0 || left > CodeLifetime) {
        left = CodeLifetime;
    }
    m_relayDeadline.setRemainingTime(left);
    m_relayClock.start();
    m_relayFollow.start();
    m_relaySeconds = 0;
    tick();
}

QString Bond::relayOnlyHere() const
{
    // Read from the link itself, not the settings: it is the link that goes to the other
    // device, and the sentence goes with it — shown while it is, gone when it is.
    if (m_relayLink.isEmpty()) {
        return {};
    }
    return loopback(QUrl(Server::tidy(m_settings->address())).host()) ? Words::relayOnlyHere()
                                                                        : QString();
}

void Bond::follow()
{
    if (!m_server || m_relayId.isEmpty() || m_relayFollowing) {
        return;
    }
    m_relayFollowing = true;
    const QString id = m_relayId;
    m_server->get(codePath(id), this, [this, id](const Server::Answer &answer) {
        m_relayFollowing = false;
        // Closed, used or run out while the question was out.
        if (id != m_relayId) {
            return;
        }
        if (answer.status == 200) {
            const Api::Read<Api::CodeState> read = Api::codeState(answer.body.object());
            // An answer this client cannot read would be the same answer every two seconds
            // for ten minutes: the asking stops, and says why. The link stays — it is good.
            if (!read.ok()) {
                m_relayFollow.stop();
                m_relaySentence = Words::unreadableAnswer();
                emit relayChanged();
                return;
            }
            if (read.value->state == Api::CodeState::State::Served) {
                m_relayed = read.value->name;
                endRelay({});
            }
            return;
        }
        // Gone on the server before this clock said so: run out, or revoked from its terminal.
        if (answer.status == 404) {
            endRelay(Words::relayRanOut());
            return;
        }
        // A key from `LEAF_KEYS` has no device to follow a code with. The link stays until it
        // runs out; it only cannot say who used it.
        if (answer.status == 409) {
            m_relayFollow.stop();
        }
        // Anything else — the server out of reach for a moment — is asked again next turn.
    });
}

void Bond::tick()
{
    if (m_relayId.isEmpty()) {
        return;
    }
    const qint64 left = m_relayDeadline.remainingTime();
    if (left <= 0) {
        endRelay(Words::relayRanOut());
        return;
    }
    const int seconds = static_cast<int>((left + 999) / 1000);
    if (seconds != m_relaySeconds) {
        m_relaySeconds = seconds;
        emit relayChanged();
    }
}

void Bond::stopRelay()
{
    if (m_relayAsking) {
        m_relayWithdrawn = true;
    }
    // Still waiting — not used, not run out: taken back, so that it cannot be used by whoever
    // happens upon the link afterwards.
    if (!m_relayId.isEmpty()) {
        withdraw(m_relayId);
    }
    m_relayed.clear();
    endRelay({});
}

void Bond::copyRelay() const
{
    if (m_relayLink.isEmpty()) {
        return;
    }
    QGuiApplication::clipboard()->setText(m_relayLink);
}

void Bond::withdraw(const QString &id)
{
    if (!m_server) {
        return;
    }
    m_server->remove(codePath(id), this, [](const Server::Answer &) {});
}

void Bond::endRelay(const QString &why)
{
    m_relayClock.stop();
    m_relayFollow.stop();
    m_relayId.clear();
    m_relayLink.clear();
    m_relaySeconds = 0;
    m_relaySentence = why;
    emit relayChanged();
}
