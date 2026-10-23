#pragma once

// A `leaf://` link read into what a device needs to link itself: where the server is, the
// code to redeem, and the certificate to pin.
//
// The server composes these in `server/src/net/link.rs`; this is its reader. Pure parsing —
// no network, no screen — so that a pasted link and a clicked one go through the same door
// and the rules are tested without either.

#include <QString>
#include <QUrl>

#include <optional>

struct Link {
    /// `https://host[:port]`, or `http://` on the loopback; never a path. No port in the link
    /// means 443, which is how the server writes an https address on the default port.
    QUrl address;
    /// Eight characters of the code's alphabet, upper case, without a dash.
    QString code;
    /// 64 lowercase hex digits, or empty for a certificate no pin is needed for.
    QString fingerprint;

    /// Forgiving of what a copy brings along — spaces, a line break, a trailing slash, the
    /// dash a person reads a code with, any case — and nothing else: a link that is not
    /// exactly Leaf's is not read at all, rather than read into something nearby.
    static std::optional<Link> read(const QString &typed);
};
