#include "Link.h"

#include <QRegularExpression>
#include <QUrlQuery>

using namespace Qt::StringLiterals;

namespace {

// The code's alphabet leaves out I, L, O, 0 and 1: letters a person would mistake for one
// another when reading a code off one screen onto another.
const QRegularExpression &codeShape()
{
    static const QRegularExpression shape(u"^[ABCDEFGHJKMNPQRSTUVWXYZ23456789]{8}$"_s);
    return shape;
}

const QRegularExpression &fingerprintShape()
{
    static const QRegularExpression shape(u"^[0-9a-f]{64}$"_s);
    return shape;
}

bool loopback(const QString &host)
{
    return host == u"127.0.0.1"_s || host == u"localhost"_s || host == u"::1"_s;
}

} // namespace

std::optional<Link> Link::read(const QString &typed)
{
    // Whitespace anywhere, not only at the ends: a link wrapped by a mail client or a chat
    // arrives with a line break in the middle, and no part of a valid link holds a space.
    QString text = typed;
    text.remove(QRegularExpression(u"\\s"_s));

    const QUrl url(text, QUrl::StrictMode);
    if (!url.isValid() || url.scheme() != u"leaf"_s)
        return std::nullopt;
    if (url.host().isEmpty() || !url.userInfo().isEmpty() || url.hasFragment())
        return std::nullopt;

    // `/lier/CODE`, with a slash after it tolerated.
    QString path = url.path();
    if (path.endsWith(u'/'))
        path.chop(1);
    static const QString prefix = u"/lier/"_s;
    if (!path.startsWith(prefix))
        return std::nullopt;
    const QString code = path.mid(prefix.size()).remove(u'-').toUpper();
    if (!codeShape().match(code).hasMatch())
        return std::nullopt;

    QString fingerprint;
    const QUrlQuery query(url);
    if (query.hasQueryItem(u"empreinte"_s)) {
        fingerprint = query.queryItemValue(u"empreinte"_s).toLower();
        if (!fingerprintShape().match(fingerprint).hasMatch())
            return std::nullopt;
    }

    const QString host = url.host();
    QUrl address;
    address.setScheme(loopback(host) ? u"http"_s : u"https"_s);
    // QUrl hands an IPv6 host back without its brackets and wants them to write one.
    address.setHost(host.contains(u':') ? u"[%1]"_s.arg(host) : host);
    if (url.port() != -1 && !(url.port() == 443 && !loopback(host)))
        address.setPort(url.port());
    if (!address.isValid())
        return std::nullopt;

    return Link{address, code, fingerprint};
}
