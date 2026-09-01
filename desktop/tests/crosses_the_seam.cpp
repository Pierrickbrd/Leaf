// The seam between the QML that draws the window and the C++ that wires it up.
//
// Everything else in this block tests the C++ side directly and headless: eight tests on
// Navigation, six on Widths, none of them ever writes `import Leaf`. `tests/opens.sh` proves
// the real binary comes up at all, but it runs under `timeout` and expects to be killed —
// gcov writes nothing on a kill, and the script asserts only a log line, never an object. This
// is the one test that calls `Boot::run` — the same function `main` calls — against a real
// `QQmlApplicationEngine`, and reads the singletons back afterwards to prove `import Leaf`
// really reaches these objects rather than stand-ins compiled beside them.

#include "Boot.h"
#include "Navigation.h"
#include "Theme.h"
#include "Widths.h"

#include <QColor>
#include <QGuiApplication>
#include <QPalette>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTest>

using Qt::Literals::StringLiterals::operator""_s;

namespace {
/// Puts the process-wide palette back on scope exit, failed assertion included — see
/// `keeps_its_contrast.cpp`'s `RestoresThePalette`, which this copies rather than shares: two
/// four-line classes cost less than a header neither test otherwise needs.
class RestoresThePalette
{
public:
    RestoresThePalette() : m_was(QGuiApplication::palette()) {}
    ~RestoresThePalette() { QGuiApplication::setPalette(m_was); }

private:
    QPalette m_was;
};
} // namespace

class CrossesTheSeam : public QObject
{
    Q_OBJECT

private slots:
    /// `main` sets the style before it ever calls `Boot::run` — deliberately kept out of
    /// `Boot.h`, see its header comment — and the default style's `ApplicationWindow.qml`
    /// pulls in a `QtQuick.Window` plugin this machine does not package. Set once, here, the
    /// same way `main` sets it once, before the first `engine.load(...)` in the slots below.
    void initTestCase() { QQuickStyle::setStyle(u"Basic"_s); }

    void booting_loads_exactly_one_window()
    {
        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp);

        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QCOMPARE(window->title(), u"Leaf"_s);
    }

    /// `Main.qml` has exactly one line that hands a value only QML knows to a C++ singleton:
    /// `Component.onCompleted: Widths.window = window.width`. Nothing in the C++ tests on
    /// `Widths` can see whether that binding still exists.
    void the_window_hands_its_own_width_to_widths()
    {
        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp);

        auto *widths = engine.singletonInstance<Widths *>(qmlTypeId("Leaf", 1, 0, "Widths"));
        QVERIFY(widths);
        QCOMPARE(widths->window(), 1100);
        QCOMPARE(widths->band(), Widths::Band::Wide);
    }

    /// The label on the shell's card is `Navigation.label`, computed by the singleton itself
    /// through `Words::destination` — none of it is written in the `.qml` file.
    void navigation_starts_on_the_shelf_and_says_so_in_french()
    {
        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp);

        auto *navigation =
            engine.singletonInstance<Navigation *>(qmlTypeId("Leaf", 1, 0, "Navigation"));
        QVERIFY(navigation);
        QCOMPARE(navigation->destination(), Navigation::Destination::Shelf);
        QCOMPARE(navigation->label(), u"Étagère"_s);
    }

    /// `Boot::run` resolves `Theme` only after `engine.load(...)` finishes — the ordering
    /// `Boot.h` documents as the only one that can work, because the module's registration is
    /// lazy. If that ordering ever regressed, `qmlTypeId` would find nothing here and this
    /// singleton would come back null rather than merely light where it should be dark.
    void theme_resolves_and_follows_the_desktop_palette()
    {
        const RestoresThePalette restoreOnExit;
        QPalette night;
        night.setColor(QPalette::Window, QColor(u"#101010"_s));
        QGuiApplication::setPalette(night);

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp);

        auto *theme = engine.singletonInstance<Theme *>(qmlTypeId("Leaf", 1, 0, "Theme"));
        QVERIFY(theme);
        QVERIFY(theme->dark());
    }
};

QTEST_MAIN(CrossesTheSeam)
#include "crosses_the_seam.moc"
