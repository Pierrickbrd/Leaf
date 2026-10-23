#pragma once

// Which certificate a server may present: the one this device was linked to, by its
// fingerprint.
//
// Leaf serves TLS itself with a certificate it generates, which no authority signed and
// which names no address anybody types. The system's own check refuses it, rightly, and a
// question « do you trust this certificate? » is answered yes without reading. So the link
// carries the certificate's fingerprint, and that fingerprint is the whole of the trust:
// the certificate presented is accepted when it is that one, whatever name it carries, and
// refused otherwise — even when the system would have accepted it.
//
// Both network managers of this client go through here: `Server`'s, and the one `Covers`
// hands the QML engine for the images it fetches. A cover is a request carrying the key
// like any other, and a pin that held for one manager and not the other would hand the key
// to whoever answered the covers.

#include <QNetworkRequest>
#include <QString>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QSslCertificate;

namespace Pinning {

/// The SHA-256 of the certificate's DER, as 64 lowercase hexadecimal digits with no colons
/// — the form a `leaf://` link carries and the server's `fingerprint_hex` writes.
QString fingerprint(const QSslCertificate &certificate);

/// Holds every request `manager` sends to the fingerprint `expected()` answers at that
/// moment.
///
/// A matching certificate is accepted whatever the system thinks of it; any other is
/// refused inside the handshake, before a byte of the request — the key with it — is
/// written, and the reply is marked so that `mismatched` says why it failed. An empty
/// fingerprint leaves the system's own validation in charge: a server behind a proxy with a
/// certificate from a public authority has nothing to pin, since that certificate changes
/// every ninety days.
///
/// `expected` is asked on whichever thread `manager` lives on, which for the QML engine's is
/// not this one: it must not reach into anything that thread does not own.
void guard(QNetworkAccessManager &manager, std::function<QString()> expected);

/// Drops the connections `manager` keeps open when `pin` is not the one it last sent under.
///
/// Called before every request, on `manager`'s own thread. `guard` judges a certificate at
/// the handshake, and Qt keeps a connection for the next request to the same host and port:
/// after the pin changes, that next request rides a connection judged against the old pin
/// and carries the key to a server nobody compared with the new one. Measured on Qt 6.10.
///
/// Replies still in flight on `manager` are cancelled: dropping their connection without
/// that leaves them never finishing. They finish at once with `OperationCanceledError`.
void renew(QNetworkAccessManager &manager, const QString &pin);

/// Whether `reply` failed because the server presented a certificate other than the pinned
/// one.
bool mismatched(const QNetworkReply *reply);

/// Whether `reply` was cancelled by `renew` — by a change of pin, and not by anything the
/// server did or said. Nobody answered it, so nobody should be shown it as an answer.
bool renewed(const QNetworkReply *reply);

/// A request to `url`, or `from` copied, held to HTTP/1.1. Every request this client sends is
/// built here, and nowhere else — a guard in `tools/` refuses a `QNetworkRequest` made
/// anywhere but this file.
///
/// The server offers HTTP/2 in its handshake, and Qt takes the offer by default. Then one
/// connection carries every request multiplexed, `encrypted` fires once for all of them, and
/// what `renew` relies on — a connection per host that `clearConnectionCache` drops and the
/// next request reopens through a handshake judged against the new pin — is a property
/// nobody has measured for HTTP/2. HTTP/1.1 is the case the tests prove; a library of covers
/// and pages loses nothing by it.
QNetworkRequest request(const QUrl &url);
QNetworkRequest request(QNetworkRequest from);

} // namespace Pinning
