// The Ubuntu client's entry point.
//
// What is left here is what cannot be anything but wiring: build the application, pick the
// style, build the engine, hand both to `Boot::run`, and hand control to the event loop.
// `Boot.h` carries the two decisions that used to live in this function — loading the fonts
// before any QML runs, and resolving the `Theme` singleton only after it does — because
// neither is reachable by a test once it is buried in `main`; see that file for why the order
// is not a preference.

#include "Boot.h"
#include "Doorbell.h"

#include <QDBusConnection>
#include <QIcon>
#include <iostream>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

int main(int argc, char *argv[])
{
    if (argc == 2 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--version")) {
        std::cout << "leaf-desktop " LEAF_VERSION << '\n';
        return 0;
    }

    const bool checkStartup = argc == 2
        && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--check-startup");

    QGuiApplication application(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("Leaf"));
    QGuiApplication::setApplicationName(QStringLiteral("Leaf"));
    QGuiApplication::setApplicationVersion(QStringLiteral(LEAF_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral("leaf"));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/qt/qml/Leaf/assets/logos/leaf-dark.svg")));

    // Basic, not Fusion and not the native style: the client draws its own palette — a
    // light one and a dark one, both decided — and a style that paints its own controls
    // would fight it every time. Basic gets out of the way.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // A click on a `leaf://` link starts this program with the link as its argument. If a
    // Leaf is already open it is handed the link and this one leaves; otherwise this is the
    // first, and it opens on the link — and listens for the next.
    QString link;
    for (int i = 1; i < argc; ++i) {
        const QString argument = QString::fromLocal8Bit(argv[i]);
        if (argument.startsWith(QStringLiteral("leaf:"), Qt::CaseInsensitive)) {
            link = argument;
            break;
        }
    }
    if (!link.isEmpty() && Doorbell::ring(link)) {
        return 0;
    }
    Doorbell doorbell;
    // Not listened to under `--check-startup`: a smoke test must not take over the door of
    // the Leaf its runner has open.
    const bool listening = !checkStartup && doorbell.listen();

    QQmlApplicationEngine engine;
    Boot::run(engine, application, QDBusConnection::sessionBus(),
              listening ? &doorbell : nullptr, link);

    if (checkStartup) {
        return 0;
    }

    return QGuiApplication::exec();
}
