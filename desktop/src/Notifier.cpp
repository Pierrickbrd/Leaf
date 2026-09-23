#include "Notifier.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QVariantList>
#include <QVariantMap>

#include <utility>

using namespace Qt::StringLiterals;

Notifier::Notifier(QDBusConnection bus, QObject *parent)
    : QObject(parent)
    , m_bus(std::move(bus))
    , m_reachable(m_bus.isConnected())
{
}

QDBusMessage Notifier::announcement(const QString &headline, const QString &detail)
{
    QDBusMessage call = QDBusMessage::createMethodCall(
        u"org.freedesktop.Notifications"_s, u"/org/freedesktop/Notifications"_s,
        u"org.freedesktop.Notifications"_s, u"Notify"_s);
    // The eight arguments the specification asks for, in its order: who is speaking, which
    // notification to replace (nought, never one of ours — a bubble that replaced the last
    // one would lose what the last one said), the icon, the two lines, the actions, the
    // hints, and how long. Minus one is the daemon's own default: it knows what its reader
    // has asked for, and this does not.
    call.setArguments({u"Leaf"_s, uint(0), u"leaf"_s, headline, detail, QStringList(),
                       QVariantMap(), -1});
    return call;
}

void Notifier::show(const QString &headline, const QString &detail)
{
    if (!m_reachable || headline.isEmpty())
        return;

    // Sent and not waited for. An answer would be the notification's own id, which nothing
    // here has any use for, and waiting for it on a desktop whose daemon is slow would hold
    // the interface for as long as it took.
    m_bus.asyncCall(announcement(headline, detail));
}
