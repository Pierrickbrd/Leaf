// The Ubuntu client's entry point.
//
// Four things happen here in an order that matters: the four font faces are registered, the
// QML is loaded, the Theme singleton is resolved by type id, and it is told to follow the
// system palette. The fonts have to be in the font database before any QML runs, or a label
// asking for "Barlow Condensed" draws in Noto Sans and nothing says why. The QML has to load
// before the singleton lookup, because `qt_add_qml_module`'s registration for "Leaf" is lazy —
// it is recorded, not run, until something actually imports "Leaf" — so a `qmlTypeId` called
// first would find the module looking checked-and-empty to every import after it. Each step
// that can fail warns instead of leaving the failure invisible, because a window that opens
// with the wrong fonts or the wrong palette still opens, and nothing else would notice.

#include "Fonts.h"
#include "Theme.h"

#include <QDebug>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

using Qt::Literals::StringLiterals::operator""_s;

int main(int argc, char *argv[])
{
    QGuiApplication application(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("Leaf"));
    QGuiApplication::setApplicationName(QStringLiteral("Leaf"));

    // Basic, not Fusion and not the native style: the client draws its own palette — a
    // light one and a dark one, both decided — and a style that paints its own controls
    // would fight it every time. Basic gets out of the way.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // Before any QML: a family the database does not know resolves to a fallback, and the
    // window would come up in Noto Sans with nothing said about it.
    //
    // The stream form, not qWarning("…"): the printf form takes a narrow literal, which
    // would be the one string in this client not written u"…"_s, and its em dash would then
    // ride on whatever charset the compiler narrows to. bytes_stay_utf8.py would not have
    // caught it — it greps for one encoding's name, never for a missing _s.
    //
    // Worded to say "failed to load": `tests/opens.sh` greps its captured log for exactly that
    // phrase, so a face that stops registering fails the smoke test instead of only degrading
    // silently to a fallback nobody is watching for.
    if (!Fonts::load())
        qWarning().noquote()
            << u"a font failed to load — the interface will draw in a fallback"_s;

    QQmlApplicationEngine engine;
    // A window that fails to load must not leave a process running with nothing on screen.
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &application,
        []() { QCoreApplication::exit(1); }, Qt::QueuedConnection);

    // `loadFromModule("Leaf", "Main")` says this better, and arrived in Qt 6.5. Ubuntu 24.04
    // ships 6.4, so the module's own resource path is spelled out instead — it is the same
    // path that call would have resolved to.
    //
    // The registration `qt_add_qml_module` generates for a "Leaf" built into this very
    // binary is lazy: it is recorded, not run, until something actually imports "Leaf" — so
    // `qmlTypeId("Leaf", …)` called before this line finds nothing and, worse, leaves the
    // module looking checked-and-empty to every import after it. `engine.load(…)` is
    // synchronous for a qrc path — it returns only once Main.qml, which imports "Leaf", has
    // been parsed and its objects constructed — so the load has to come first.
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Leaf/Main.qml")));

    // The palette is read once here rather than watched: Qt 6.4 has no colorScheme signal to
    // watch, and a desktop that changes theme mid-session is a restart away from being right.
    //
    // `singletonInstance(uri, typeName)` would say this in one line and arrived in Qt 6.5 —
    // the same 6.5 that has `loadFromModule`, above, for the same reason. The type id is what
    // 6.4 offers, and by this point the module is registered and the id resolves. Nothing has
    // painted yet — the event loop has not started — so `followSystem()` still lands before
    // anything is shown, even though it now runs after `load(…)` rather than before it.
    // Worded to say "error", for the same reason as the font warning above: nothing else
    // covers this one. embeds-its-fonts would catch a font that stopped registering; nothing
    // catches a Theme that stopped resolving except this line landing in the log `opens.sh`
    // greps — a URI rename, a version bump or a registration-ordering change would otherwise
    // ship the light palette on a dark desktop with every test still green.
    if (Theme *theme = engine.singletonInstance<Theme *>(qmlTypeId("Leaf", 1, 0, "Theme")))
        theme->followSystem();
    else
        qWarning().noquote()
            << u"error resolving the Theme singleton — the interface will stay in its light "
               u"palette"_s;

    return QGuiApplication::exec();
}
