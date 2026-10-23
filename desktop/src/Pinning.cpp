#include "Pinning.h"

#include <QCryptographicHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslError>
#include <QStringList>
#include <QUrl>

#include <memory>
#include <utility>

namespace {

/// Where a refused reply says it was refused for its certificate. A property on the reply
/// rather than a set kept here: the reply is what every caller already holds, and it takes
/// the mark with it when it is deleted.
constexpr const char *Mismatch = "leafPinMismatch";

/// The pin a manager last sent under, held on the manager so that `renew` keeps one line at
/// each call site and no table of managers outlives one of them.
constexpr const char *Sent = "leafPinSent";

/// Where a reply says `renew` cancelled it, for the same reason `Mismatch` is on the reply.
constexpr const char *Renewed = "leafPinRenewed";

/// Whether `reply` is talking to the certificate `pin` names. The whole string, compared
/// whole — never a prefix, never « long enough ». Not in constant time: a fingerprint is
/// printed in a link and in the server's log, and is no secret to guess at.
bool presents(const QNetworkReply *reply, const QString &pin)
{
    const QSslCertificate presented = reply->sslConfiguration().peerCertificate();
    return !presented.isNull() && Pinning::fingerprint(presented) == pin;
}

/// Where a manager remembers the servers it refused under the pin it holds now, as
/// `host:port`. Cleared by `renew`, since a new pin judges every server afresh.
constexpr const char *RefusedAt = "leafPinRefusedAt";

QString authority(const QUrl &url)
{
    return url.host() + u':' + QString::number(url.port(443));
}

void refuse(QNetworkReply *reply)
{
    reply->setProperty(Mismatch, true);
    // On the manager too. Held to HTTP/1.1, Qt opens several connections to one server, and
    // when the first handshake is refused it fails the requests queued behind it with the
    // same error — without `sslErrors` for them, so they never pass through `guard`. Refused
    // all the same, and for the same certificate: they should say so.
    if (QNetworkAccessManager *manager = reply->manager()) {
        QStringList refused = manager->property(RefusedAt).toStringList();
        const QString at = authority(reply->url());
        if (!refused.contains(at)) {
            refused.append(at);
            manager->setProperty(RefusedAt, refused);
        }
    }
}

} // namespace

QString Pinning::fingerprint(const QSslCertificate &certificate)
{
    return QString::fromUtf8(
        QCryptographicHash::hash(certificate.toDer(), QCryptographicHash::Sha256).toHex());
}

void Pinning::guard(QNetworkAccessManager &manager, std::function<QString()> expected)
{
    // Shared by the two connections below, so `expected` is written once.
    const auto pin = std::make_shared<const std::function<QString()>>(std::move(expected));

    // The system objected — which a certificate Leaf generated always gives it reason to:
    // nobody signed it, and the name in it is not the address. Those objections are set
    // aside for the pinned certificate, and for nothing else. Left alone, the handshake
    // fails here, before the request is written.
    QObject::connect(&manager, &QNetworkAccessManager::sslErrors, &manager,
                     [pin](QNetworkReply *reply, const QList<QSslError> &errors) {
                         const QString wanted = (*pin)();
                         if (wanted.isEmpty()) {
                             return;
                         }
                         if (presents(reply, wanted)) {
                             reply->ignoreSslErrors(errors);
                             return;
                         }
                         refuse(reply);
                     });

    // The system did not object — and that is not enough. A certificate an authority signed
    // for this address would pass every check above without ever raising an error, and
    // stand in for the one this device was linked to. Qt's documentation names this signal
    // for pinning: the handshake is done, no request has been written, and a reply aborted
    // here sends nothing. Direct, because a queued abort would arrive after the key.
    QObject::connect(
        &manager, &QNetworkAccessManager::encrypted, &manager,
        [pin](QNetworkReply *reply) {
            const QString wanted = (*pin)();
            if (wanted.isEmpty() || presents(reply, wanted)) {
                return;
            }
            refuse(reply);
            reply->abort();
        },
        Qt::DirectConnection);
}

void Pinning::renew(QNetworkAccessManager &manager, const QString &pin)
{
    if (manager.property(Sent).toString() == pin) {
        return;
    }
    // Recorded first: an aborted reply finishes on the spot, and whoever it answers may send
    // again from inside this call — which must find the pin already renewed.
    manager.setProperty(Sent, pin);
    manager.setProperty(RefusedAt, QStringList());
    // A reply still riding a connection the cache is about to drop is orphaned by it: Qt
    // never finishes it, measured on 6.10 with the server's answer already sent, and a
    // screen waits on it for the rest of the session. Cancelled instead, it finishes at once
    // — refused, like everything this file cannot vouch for. Finished replies are skipped:
    // `Server` has them waiting on `deleteLater`.
    const QList<QNetworkReply *> replies = manager.findChildren<QNetworkReply *>();
    for (const QPointer<QNetworkReply> reply : replies) {
        if (reply && !reply->isFinished()) {
            reply->setProperty(Renewed, true);
            reply->abort();
        }
    }
    manager.clearConnectionCache();
}

bool Pinning::mismatched(const QNetworkReply *reply)
{
    if (reply == nullptr) {
        return false;
    }
    if (reply->property(Mismatch).toBool()) {
        return true;
    }
    // A request failed behind another's refused handshake: see `refuse`.
    const QNetworkAccessManager *manager = reply->manager();
    return reply->error() == QNetworkReply::SslHandshakeFailedError && manager != nullptr
           && manager->property(RefusedAt).toStringList().contains(authority(reply->url()));
}

bool Pinning::renewed(const QNetworkReply *reply)
{
    return reply != nullptr && reply->property(Renewed).toBool();
}

QNetworkRequest Pinning::request(const QUrl &url)
{
    return request(QNetworkRequest(url));
}

QNetworkRequest Pinning::request(QNetworkRequest from)
{
    from.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    return from;
}
