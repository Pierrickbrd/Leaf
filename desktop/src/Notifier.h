#pragma once

// The same event, escalated to the desktop.
//
// `org.freedesktop.Notifications` over D-Bus, which costs no new dependency: qtkeychain
// already brings the bus in for the keyring, and `QT_DBUS_LIB` is in the compile flags of
// every target here. Asked for explicitly all the same — a link that happens to work through
// somebody else's dependency is a link that breaks the day they drop it.
//
// **Nothing breaks when there is no daemon.** A desktop with no notification service, a
// remote session, a container: the call fails and the application knows no more about it than
// that. It is a courtesy, not a feature anything depends on.
//
// It decides nothing. Whether an event deserves the desktop is `Toasts`'s business — what
// the reader asked for, and whether the window has the focus — and this puts on the bus what
// it is handed.

#include <QDBusConnection>
#include <QDBusMessage>
#include <QObject>
#include <QString>

class Notifier : public QObject
{
    Q_OBJECT

public:
    /// The bus it speaks on, **given rather than read** — the same care `Toasts` takes with
    /// the window's own state, and for a sharper reason. Read from inside, a test that
    /// exercises this class speaks on the real one: `warns_once` put « Scan terminé · 21
    /// séries » on the desktop of whoever ran `ctest`, nine times in forty minutes on the
    /// machine it was written on, and the only thing that looked wrong was the server.
    explicit Notifier(QDBusConnection bus = QDBusConnection::sessionBus(),
                      QObject *parent = nullptr);

    /// The call the specification asks for, composed and not sent.
    ///
    /// Apart from `show` so a test can read the eight arguments without a daemon to read them
    /// off. That order is the half of this class that can be wrong — a headline and a detail
    /// the wrong way round is a notification that says nothing — where « it reached the bus »
    /// is the half no test should be settling on somebody's desktop.
    static QDBusMessage announcement(const QString &headline, const QString &detail);

    /// Puts one on the bus. The application's own name and icon ride with it, because a
    /// notification nobody can attribute is a notification nobody trusts.
    void show(const QString &headline, const QString &detail);

    /// Whether the bus answered at all when this was built. False on a machine with no
    /// session bus, and then `show` does nothing rather than failing once per event.
    bool reachable() const { return m_reachable; }

private:
    QDBusConnection m_bus;
    bool m_reachable = false;
};
