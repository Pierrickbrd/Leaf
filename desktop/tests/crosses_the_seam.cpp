// The seam between the QML that draws the window and the C++ that wires it up.
//
// Everything else in this block tests the C++ side directly and headless: the Navigation and
// Widths tests never write `import Leaf`. `tests/opens.sh` proves
// the real binary comes up at all, but it runs under `timeout` and expects to be killed —
// gcov writes nothing on a kill, and the script asserts only a log line, never an object. This
// is the one test that calls `Boot::run` — the same function `main` calls — against a real
// `QQmlApplicationEngine`, and reads the singletons back afterwards to prove `import Leaf`
// really reaches these objects rather than stand-ins compiled beside them.

#include "Boot.h"
#include "Navigation.h"
#include "Pretend.h"
#include "Shelf.h"
#include "Theme.h"
#include "Widths.h"

#include <QAccessible>
#include <QBuffer>
#include <QColor>
#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPalette>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTcpServer>
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

QJsonObject aSeries(const QString &id, const QString &name, int volumes, bool inProgress)
{
    return {
        {u"id"_s, id},
        {u"workId"_s, u"work-"_s + id},
        {u"name"_s, name},
        {u"work"_s, name},
        {u"entryCount"_s, volumes},
        {u"chapterCount"_s, 0},
        {u"arcCount"_s, 0},
        {u"medium"_s, u"MANGA"_s},
        {u"ownedVolumes"_s, volumes},
        {u"readStatus"_s, inProgress ? u"IN_PROGRESS"_s : u"READ"_s},
    };
}

QByteArray aPage(const QJsonArray &items, int total = -1, int page = 0)
{
    return QJsonDocument(QJsonObject{
                             {u"items"_s, items},
                             {u"total"_s, total < 0 ? items.size() : total},
                             {u"page"_s, page},
                             {u"size"_s, 100},
                         })
        .toJson(QJsonDocument::Compact);
}

QByteArray aReply(int status, const QByteArray &contentType, const QByteArray &body)
{
    return "HTTP/1.1 " + QByteArray::number(status) + " .\r\n"
           "Content-Type: " + contentType + "\r\n"
           "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
}

QByteArray aCover()
{
    QImage image(2, 3, QImage::Format_RGB32);
    image.fill(QColor(u"#2B3550"_s));
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

QQuickItem *itemNamed(QQuickItem *root, const QString &name)
{
    if (root->objectName() == name)
        return root;
    for (QQuickItem *child : root->childItems()) {
        if (QQuickItem *found = itemNamed(child, name))
            return found;
    }
    return nullptr;
}
} // namespace

class CrossesTheSeam : public QObject
{
    Q_OBJECT

private slots:
    /// `main` sets the style before it ever calls `Boot::run` — deliberately kept out of
    /// `Boot.h`, see its header comment — and the default style's `ApplicationWindow.qml`
    /// pulls in a `QtQuick.Window` plugin this machine does not package. Set once, here, the
    /// same way `main` sets it once, before the first `engine.load(...)` in the slots below.
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QQuickStyle::setStyle(u"Basic"_s);
    }

    void init()
    {
        // Never read the developer's real config or keyring when the shelf starts asking as
        // soon as its component completes. Port 1 refuses locally and deterministically.
        qputenv("LEAF_ADDRESS", QByteArrayLiteral("http://127.0.0.1:1"));
        qputenv("LEAF_KEY", QByteArrayLiteral("8f3a92c1d4e5b6a7"));
    }

    void cleanup()
    {
        qunsetenv("LEAF_ADDRESS");
        qunsetenv("LEAF_KEY");
    }

    void booting_loads_exactly_one_window()
    {
        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp);

        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QCOMPARE(window->title(), u"Leaf"_s);
    }

    void the_loader_opens_on_the_shelf_grid()
    {
        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp);

        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        auto *grid = window->findChild<QQuickItem *>(u"shelf-grid"_s);
        QVERIFY(grid);
        QCOMPARE(grid->property("columns").toInt(), 7);
    }

    /// The medium band changes from four columns to five without crossing one of its own
    /// edges. This reaches through the window, Widths and the QML binding in one assertion.
    void the_grid_follows_all_three_width_rules()
    {
        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp);

        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        auto *grid = window->findChild<QQuickItem *>(u"shelf-grid"_s);
        QVERIFY(grid);

        window->setWidth(800);
        QTRY_COMPARE(grid->property("columns").toInt(), 5);
        window->setWidth(700);
        QTRY_COMPARE(grid->property("columns").toInt(), 4);
        window->setWidth(500);
        QTRY_COMPARE(grid->property("columns").toInt(), 2);
    }

    /// The view, not hand-written visibility code, decides which covers live. One cell of
    /// cache is one extra row; beyond it the delegate — and its Image request — is gone.
    void the_grid_keeps_one_extra_row_and_takes_keyboard_focus()
    {
        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp);

        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        auto *grid = window->findChild<QQuickItem *>(u"shelf-grid"_s);
        QVERIFY(grid);

        const qreal cellHeight = grid->property("cellHeight").toReal();
        QVERIFY(cellHeight > 0);
        QCOMPARE(grid->property("cacheBuffer").toReal(), cellHeight);
        QVERIFY(!grid->property("reuseItems").toBool());

        grid->forceActiveFocus();
        QTRY_VERIFY(grid->hasActiveFocus());
    }

    void a_page_becomes_worded_tiles_with_the_mark_and_focus_the_artifact_draws()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        const QByteArray pageReply = aReply(
            200, QByteArrayLiteral("application/json"),
            aPage({
                aSeries(u"dn"_s, u"Death Note · Black Edition"_s, 7, true),
                aSeries(u"ac"_s, u"Assassination Classroom"_s, 21, false),
            }));
        const QByteArray coverBody = aCover();
        QVERIFY(!coverBody.isEmpty());
        const QByteArray coverReply =
            aReply(200, QByteArrayLiteral("image/png"), coverBody);
        pretend.answerFor = [pageReply, coverReply](const QByteArray &request) {
            return request.startsWith("GET /series?") ? pageReply : coverReply;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp);

        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        auto *shelf = engine.singletonInstance<Shelf *>(qmlTypeId("Leaf", 1, 0, "Shelf"));
        QVERIFY(shelf);
        QTRY_COMPARE(shelf->count(), 2);

        auto *grid = window->findChild<QQuickItem *>(u"shelf-grid"_s);
        QVERIFY(grid);
        QTRY_COMPARE(grid->property("count").toInt(), 2);

        QQuickItem *reading = nullptr;
        QTRY_VERIFY((reading = itemNamed(grid, u"tile-dn"_s)));
        auto *cover = itemNamed(reading, u"cover-frame-dn"_s);
        auto *title = itemNamed(reading, u"title-dn"_s);
        auto *volumes = itemNamed(reading, u"volumes-dn"_s);
        auto *mark = itemNamed(reading, u"in-progress-dn"_s);
        auto *ring = itemNamed(reading, u"focus-dn"_s);
        auto *shadow = itemNamed(reading, u"cover-shadow-dn"_s);
        auto *clipped = itemNamed(reading, u"clipped-cover-dn"_s);
        QVERIFY(cover);
        QVERIFY(title);
        QVERIFY(volumes);
        QVERIFY(mark);
        QVERIFY(ring);
        QVERIFY(shadow);
        QVERIFY(clipped);

        QVERIFY(cover->width() >= Widths::MinimumCoverWidth);
        QCOMPARE(cover->height(), cover->width() * 1.5);
        QCOMPARE(clipped->property("radius").toInt(), Theme().coverRadius());
        QCOMPARE(shadow->width(), cover->width() + 32);
        QCOMPARE(shadow->height(), cover->height() + 32);
        QVERIFY(shadow->property("source").toString().endsWith(
            u"assets/cover-shadow-light.png"_s));
        QCOMPARE(title->property("text").toString(), u"Death Note · Black Edition"_s);
        const QFont titleFont = title->property("font").value<QFont>();
        QCOMPARE(titleFont.family(), Theme().displayFamily());
        QCOMPARE(titleFont.pixelSize(), 14);
        QCOMPARE(titleFont.weight(), QFont::DemiBold);
        QCOMPARE(title->property("maximumLineCount").toInt(), 2);
        QCOMPARE(volumes->property("text").toString(), u"7 tomes"_s);
        const QFont volumesFont = volumes->property("font").value<QFont>();
        QCOMPARE(volumesFont.family(), Theme().textFamily());
        QCOMPARE(volumesFont.pixelSize(), 12);
        QVERIFY(mark->isVisible());

        auto *finished = itemNamed(grid, u"tile-ac"_s);
        QVERIFY(finished);
        auto *finishedMark = itemNamed(finished, u"in-progress-ac"_s);
        QVERIFY(finishedMark);
        QVERIFY(!finishedMark->isVisible());

        grid->forceActiveFocus();
        grid->setProperty("currentIndex", 0);
        QTRY_VERIFY(ring->isVisible());
        QTest::keyClick(window, Qt::Key_Right);
        QTRY_COMPARE(grid->property("currentIndex").toInt(), 1);
        QTRY_VERIFY(!ring->isVisible());
        QTest::keyClick(window, Qt::Key_Left);
        QTRY_COMPARE(grid->property("currentIndex").toInt(), 0);

        QAccessibleInterface *accessible = QAccessible::queryAccessibleInterface(reading);
        QVERIFY(accessible);
        QCOMPARE(accessible->text(QAccessible::Name), u"Death Note · Black Edition"_s);
        QCOMPARE(accessible->text(QAccessible::Description), u"7 tomes"_s);

        auto *theme = engine.singletonInstance<Theme *>(qmlTypeId("Leaf", 1, 0, "Theme"));
        QVERIFY(theme);
        theme->setDark(true);
        QTRY_VERIFY(shadow->property("source").toString().endsWith(
            u"assets/cover-shadow-dark.png"_s));
    }

    void reaching_the_last_row_asks_for_the_next_page()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));

        QJsonArray firstItems;
        for (int i = 0; i < 100; ++i) {
            const QString number = QString::number(i);
            // Pagination is what this test isolates. A shared URL lets QQuickPixmap coalesce
            // the visible covers instead of making the jump cancel dozens of unrelated ones.
            firstItems.append(aSeries(u"shared"_s, u"Série "_s + number, 1, false));
        }
        const QByteArray firstReply = aReply(
            200, QByteArrayLiteral("application/json"), aPage(firstItems, 101));
        const QByteArray nextReply = aReply(
            200, QByteArrayLiteral("application/json"),
            aPage({aSeries(u"last"_s, u"La dernière"_s, 1, false)}, 101, 1));
        const QByteArray coverReply =
            aReply(200, QByteArrayLiteral("image/png"), aCover());
        pretend.answerFor = [firstReply, nextReply,
                             coverReply](const QByteArray &request) {
            if (!request.startsWith("GET /series?"))
                return coverReply;
            return request.contains("page=0") ? firstReply : nextReply;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp);

        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        auto *shelf = engine.singletonInstance<Shelf *>(qmlTypeId("Leaf", 1, 0, "Shelf"));
        QVERIFY(shelf);
        QTRY_COMPARE(shelf->count(), 100);

        auto *grid = window->findChild<QQuickItem *>(u"shelf-grid"_s);
        QVERIFY(grid);
        grid->forceActiveFocus();
        grid->setProperty("currentIndex", 99);

        QTRY_VERIFY(grid->property("contentY").toReal() > 0);
        QTRY_COMPARE(shelf->count(), 101);
        QVERIFY(pretend.heard.contains("page=1"));
    }

    void the_first_load_is_visible_while_the_server_has_not_answered()
    {
        QTcpServer silent;
        QVERIFY(silent.listen(QHostAddress::LocalHost));
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(silent.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp);

        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        auto *waiting = window->findChild<QQuickItem *>(u"shelf-first-load"_s);
        QVERIFY(waiting);
        QTRY_VERIFY(waiting->isVisible());
    }

    void a_first_page_that_fails_is_said_in_the_empty_grid()
    {
        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp);

        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        auto *trouble = window->findChild<QQuickItem *>(u"shelf-trouble"_s);
        auto *said = window->findChild<QQuickItem *>(u"shelf-trouble-text"_s);
        QVERIFY(trouble);
        QVERIFY(said);
        QTRY_VERIFY(trouble->isVisible());
        QVERIFY(!said->property("text").toString().isEmpty());
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

    /// The shelf is still Navigation's initial destination, and its French name remains
    /// computed by the singleton itself through `Words::destination`.
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
