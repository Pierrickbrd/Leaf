#include "Boot.h"

#include "Fonts.h"
#include "Theme.h"

#include <QCoreApplication>
#include <QDebug>
#include <QGuiApplication>
#include <QUrl>

using Qt::Literals::StringLiterals::operator""_s;

namespace Boot {

void run(QQmlApplicationEngine &engine, const QGuiApplication &application)
{
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

    // A window that fails to load must not leave a process running with nothing on screen.
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &application,
        []() { QCoreApplication::exit(1); }, Qt::QueuedConnection);

    // `loadFromModule("Leaf", "Main")` says this better, and arrived in Qt 6.5. Ubuntu 24.04
    // ships 6.4, so the module's own resource path is spelled out instead — it is the same
    // path that call would have resolved to.
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
}

} // namespace Boot
