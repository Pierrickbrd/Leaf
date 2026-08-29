// The Ubuntu client.
//
// Nothing here yet but a window: the point of this first pass is to find out whether Qt 6
// with QML is pleasant to write, which is the one thing about the choice that no amount of
// reasoning could settle.

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

int main(int argc, char *argv[])
{
    QGuiApplication application(argc, argv);
    application.setOrganizationName(QStringLiteral("Leaf"));
    application.setApplicationName(QStringLiteral("Leaf"));

    // Basic, not Fusion and not the native style: the client draws its own palette — a
    // light one and a dark one, both decided — and a style that paints its own controls
    // would fight it every time. Basic gets out of the way.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QQmlApplicationEngine engine;
    // A window that fails to load must not leave a process running with nothing on screen.
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &application,
        []() { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    // `loadFromModule("Leaf", "Main")` says this better, and arrived in Qt 6.5. Ubuntu 24.04
    // ships 6.4, so the module's own resource path is spelled out instead — it is the same
    // path that call would have resolved to.
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Leaf/Main.qml")));

    return application.exec();
}
