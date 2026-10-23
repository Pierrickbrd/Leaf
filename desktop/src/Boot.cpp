#include "Boot.h"

#include "Covers.h"
#include "Doorbell.h"
#include "Fonts.h"
#include "Linking.h"
#include "Notifier.h"
#include "Preferences.h"
#include "Theme.h"
#include "Toasts.h"

#include <QCoreApplication>
#include <QDebug>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QUrl>
#include <utility>

using Qt::Literals::StringLiterals::operator""_s;

namespace {

/// Every press in the window, told to the navigation cursor before anything under it hears
/// it: the focus ring is the keyboard's, and a click is the hand saying it is not using the
/// keyboard. Watched here and not in QML because QML only hears a press on an item that lets
/// it through — a switch clicked after Tab took the press, kept its focus, changed nothing
/// QML could see, and stayed ringed.
class Presses : public QObject
{
public:
    Presses(QObject *cursor, QObject *parent) : QObject(parent), m_cursor(cursor) {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::TouchBegin:
            m_cursor->setProperty("pointerDown", true);
            m_cursor->setProperty("pointerTookOver", true);
            break;
        case QEvent::MouseButtonRelease:
        case QEvent::TouchEnd:
            // After the release has been delivered and not before: a control that takes the
            // focus on release does it while the hand is still the reason.
            QMetaObject::invokeMethod(
                m_cursor, [cursor = m_cursor] { cursor->setProperty("pointerDown", false); },
                Qt::QueuedConnection);
            break;
        default:
            break;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QObject *m_cursor;
};

} // namespace

namespace Boot {

void run(QQmlApplicationEngine &engine, const QGuiApplication &application,
         QDBusConnection notifications, Doorbell *doorbell, const QString &link)
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

    // Before the load, and not after. Every cover on the shelf is an `Image` fetched by the
    // engine's own network manager, which would send no key and be refused; this puts one on.
    //
    // It takes the engine rather than the settings for the reason this whole file exists: at
    // this point "Leaf" is not a resolvable module yet, so there is no `Settings` singleton to
    // hand over. The factory resolves it the first time the engine asks for a manager, which is
    // after the load. Installing it after the load instead would work today, but only because
    // nothing on screen fetches anything before the event loop runs — a fact about today's QML,
    // not a rule the next screen would know it was breaking.
    //
    // Owned by the application: the engine takes no ownership of a factory and wants one that
    // outlives it, and `application` arrives here as a const reference — deliberately, it is
    // only a connection context — so the pointer `qApp` is what can be a parent.
    engine.setNetworkAccessManagerFactory(new Covers(&engine, qApp));

    // A window that fails to load must not leave a process running with nothing on screen.
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &application,
        []() { QCoreApplication::exit(1); }, Qt::QueuedConnection);

    // `loadFromModule("Leaf", "Main")` says this better, and arrived in Qt 6.5. Ubuntu 24.04
    // ships 6.4, so the module's own resource path is spelled out instead — it is the same
    // path that call would have resolved to.
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Leaf/Main.qml")));

    if (!engine.rootObjects().isEmpty()) {
        if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst())) {
            if (QObject *cursor = window->findChild<QObject *>(u"navigation-cursor"_s))
                window->installEventFilter(new Presses(cursor, window));
        }
    }

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
    if (Theme *theme = engine.singletonInstance<Theme *>(qmlTypeId("Leaf", 1, 0, "Theme"))) {
        // What the reader asked for wins over what the desktop says, and `System` is one of
        // the things they can ask for — so the preference is consulted first and decides
        // whether the palette is read at all.
        const auto *wanted = engine.singletonInstance<Preferences *>(
            qmlTypeId("Leaf", 1, 0, "Preferences"));
        theme->follow(wanted ? std::to_underlying(wanted->appearance()) : 0);
        if (wanted) {
            QObject::connect(wanted, &Preferences::appearanceChosen, theme,
                             [theme](Preferences::Appearance chosen) {
                                 theme->follow(std::to_underlying(chosen));
                             });
        }
    } else
        qWarning().noquote()
            << u"error resolving the Theme singleton — the interface will stay in its light "
               u"palette"_s;

    // A clicked link, from the command line or from a launch that found this one already
    // there. Resolved after the load for the reason the `Theme` is: the module does not exist
    // before it. Whatever it carries is untrusted and goes through `Linking`, which reads it
    // like a pasted one and never sends anything by itself.
    if (doorbell || !link.isEmpty()) {
        if (auto *linking =
                engine.singletonInstance<Linking *>(qmlTypeId("Leaf", 1, 0, "Linking"))) {
            if (!link.isEmpty())
                linking->arrive(link);
            if (doorbell) {
                QObject::connect(
                    doorbell, &Doorbell::rang, linking, [&engine, linking](const QString &heard) {
                        linking->arrive(heard);
                        if (engine.rootObjects().isEmpty())
                            return;
                        if (auto *window = qobject_cast<QQuickWindow *>(
                                engine.rootObjects().constFirst())) {
                            window->show();
                            window->raise();
                            window->requestActivate();
                        }
                    });
            }
        }
    }

    // What escalates to the desktop, put on the bus it was handed. The notifier is owned by
    // the engine so it lives exactly as long as what raises the events, and it is connected
    // here rather than held by `Toasts`: what a bubble is has nothing to do with what D-Bus
    // is, and a model that opened a session bus could not be built in a test.
    if (const auto *toasts =
            engine.singletonInstance<Toasts *>(qmlTypeId("Leaf", 1, 0, "Toasts"))) {
        QObject::connect(toasts, &Toasts::escalated,
                         new Notifier(std::move(notifications), &engine), &Notifier::show);
    }
}

} // namespace Boot
