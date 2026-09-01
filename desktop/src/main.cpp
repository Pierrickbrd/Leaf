// The Ubuntu client's entry point.
//
// What is left here is what cannot be anything but wiring: build the application, pick the
// style, build the engine, hand both to `Boot::run`, and hand control to the event loop.
// `Boot.h` carries the two decisions that used to live in this function — loading the fonts
// before any QML runs, and resolving the `Theme` singleton only after it does — because
// neither is reachable by a test once it is buried in `main`; see that file for why the order
// is not a preference.

#include "Boot.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

int main(int argc, char *argv[])
{
    QGuiApplication application(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("Leaf"));
    QGuiApplication::setApplicationName(QStringLiteral("Leaf"));

    // Basic, not Fusion and not the native style: the client draws its own palette — a
    // light one and a dark one, both decided — and a style that paints its own controls
    // would fight it every time. Basic gets out of the way.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QQmlApplicationEngine engine;
    Boot::run(engine, application);

    return QGuiApplication::exec();
}
