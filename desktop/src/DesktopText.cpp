#include "DesktopText.h"

#include <QDBusArgument>
#include <QDBusMessage>
#include <QEvent>
#include <QFont>
#include <QGuiApplication>

using Qt::Literals::StringLiterals::operator""_s;

namespace {

const auto Portal = u"org.freedesktop.portal.Desktop"_s;
const auto PortalPath = u"/org/freedesktop/portal/desktop"_s;
const auto Settings = u"org.freedesktop.portal.Settings"_s;
const auto Interface = u"org.gnome.desktop.interface"_s;
const auto Factor = u"text-scaling-factor"_s;

/// Long enough for a portal that is running to answer, short enough that a session whose
/// portal hangs costs a start half a second rather than D-Bus's default of twenty-five.
constexpr int PortalWait = 500;

} // namespace

DesktopText::DesktopText(QDBusConnection bus, const QByteArray &desktop, QObject *parent)
    : SystemText(parent)
{
    // Asked and waited for, once, before the window is drawn: answered later, the first
    // frame would be laid out at 1 and every word would jump under the reader's eyes.
    QDBusMessage ask = QDBusMessage::createMethodCall(Portal, PortalPath, Settings, u"ReadOne"_s);
    ask << Interface << Factor;
    const QDBusMessage answer = bus.call(ask, QDBus::Block, PortalWait);
    const qreal said = answer.type() == QDBusMessage::ReplyMessage && !answer.arguments().isEmpty()
        ? numberIn(answer.arguments().constFirst())
        : 0;

    if (said > 0) {
        m_factor = said;
        bus.connect(Portal, PortalPath, Settings, u"SettingChanged"_s, this,
                    SLOT(settingChanged(QString, QString, QDBusVariant)));
        return;
    }
    if (desktop.toUpper().contains("KDE"))
        followTheFont();
}

qreal DesktopText::numberIn(const QVariant &answer)
{
    if (answer.canConvert<QDBusVariant>())
        return numberIn(answer.value<QDBusVariant>().variant());
    if (answer.canConvert<QDBusArgument>()) {
        // A variant inside a variant, as the older `Read` and some portals answer, arrives
        // as an argument still to be read rather than as a value.
        const auto argument = answer.value<QDBusArgument>();
        if (argument.currentType() == QDBusArgument::VariantType)
            return numberIn(qdbus_cast<QDBusVariant>(argument).variant());
        return 0;
    }
    bool number = false;
    const qreal factor = answer.toDouble(&number);
    return number ? factor : 0;
}

void DesktopText::settingChanged(const QString &space, const QString &key,
                                 const QDBusVariant &value)
{
    if (space != Interface || key != Factor)
        return;
    if (const qreal factor = numberIn(value.variant()); factor > 0)
        take(factor);
}

void DesktopText::followTheFont()
{
    if (!qGuiApp)
        return;
    m_followsTheFont = true;
    readTheFont();
    // The event, and not `QGuiApplication::fontChanged`, which Qt 6.10 deprecates in its
    // favour; the event has been there since Qt 4, so 6.4 has it too.
    qGuiApp->installEventFilter(this);
}

void DesktopText::readTheFont()
{
    const qreal points = QGuiApplication::font().pointSizeF();
    take(points > 0 ? points / PlasmaPoints : 1.0);
}

bool DesktopText::eventFilter(QObject *watched, QEvent *event)
{
    if (m_followsTheFont && watched == qGuiApp && event->type() == QEvent::ApplicationFontChange)
        readTheFont();
    return SystemText::eventFilter(watched, event);
}

void DesktopText::take(qreal factor)
{
    if (qFuzzyCompare(factor, m_factor))
        return;
    m_factor = factor;
    emit changed();
}
