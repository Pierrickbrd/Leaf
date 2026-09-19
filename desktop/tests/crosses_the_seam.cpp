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
#include "Preferences.h"
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
#include <QMutex>
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

/// The one `/next` row the artifact draws: volume 12, page 47 of 190, inside chapter 98.
QByteArray anOffer(const QString &reason = u"IN_PROGRESS"_s)
{
    const QJsonObject row{
        {u"seriesId"_s, u"ac"_s},
        {u"seriesName"_s, u"Assassination Classroom"_s},
        {u"reason"_s, reason},
        {u"entry"_s,
         QJsonObject{
             {u"id"_s, u"volume-12"_s},
             {u"type"_s, u"VOLUME"_s},
             {u"number"_s, 12.0},
             {u"pageCount"_s, 190},
         }},
        {u"progress"_s,
         QJsonObject{
             {u"page"_s, 47},
             {u"pageCount"_s, 190},
             {u"chapter"_s, QJsonObject{{u"label"_s, u"Chapitre 98"_s}}},
         }},
    };
    return QJsonDocument(QJsonArray{row}).toJson(QJsonDocument::Compact);
}

/// `/filters`, with two values on each axis so that both are worth offering.
QByteArray someFilters()
{
    const auto counted = [](const QString &value, int count) {
        return QJsonObject{{u"value"_s, value}, {u"count"_s, count}};
    };
    // Two axes the row can draw, and four it cannot — one of them long enough that the panel
    // folds it and gives it a field of its own.
    QJsonArray manyAuthors;
    for (int i = 1; i <= 14; ++i)
        manyAuthors << counted(u"Auteur %1"_s.arg(i, 2, 10, QChar(u'0')), 15 - i);
    return QJsonDocument(
               QJsonObject{
                   {u"readStatuses"_s,
                    QJsonArray{counted(u"UNREAD"_s, 12), counted(u"READ"_s, 4)}},
                   {u"media"_s, QJsonArray{counted(u"manga"_s, 8), counted(u"bd"_s, 3)}},
                   {u"genres"_s,
                    QJsonArray{counted(u"Horreur"_s, 5), counted(u"Aventure"_s, 3)}},
                   {u"universes"_s,
                    QJsonArray{counted(u"Parasite"_s, 3), counted(u"Terres d’Arran"_s, 2)}},
                   {u"languages"_s, QJsonArray{counted(u"fr"_s, 9), counted(u"ja"_s, 2)}},
                   {u"authors"_s, manyAuthors},
               })
        .toJson(QJsonDocument::Compact);
}

/// The same axes, with an author list the size a real library reaches. Kept apart from
/// `someFilters` so that every other test keeps the small one and stays quick.
QByteArray manyFilters(int authors)
{
    const auto counted = [](const QString &value, int count) {
        return QJsonObject{{u"value"_s, value}, {u"count"_s, count}};
    };
    QJsonArray names;
    for (int i = 1; i <= authors; ++i)
        names << counted(u"Auteur %1"_s.arg(i, 4, 10, QChar(u'0')), 1);
    return QJsonDocument(
               QJsonObject{
                   {u"readStatuses"_s,
                    QJsonArray{counted(u"UNREAD"_s, 12), counted(u"READ"_s, 4)}},
                   {u"authors"_s, names},
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

/// Every item under `root` carrying that name. `findChildren` misses them: a Repeater's
/// delegates are not QObject children of the item they are drawn in, which is the same
/// reason `itemNamed` walks the item tree rather than the object tree.
void itemsNamed(QQuickItem *root, const QString &name, QList<QQuickItem *> &into)
{
    if (root->objectName() == name)
        into.append(root);
    for (QQuickItem *child : root->childItems())
        itemsNamed(child, name, into);
}

QList<QQuickItem *> itemsNamed(QQuickItem *root, const QString &name)
{
    QList<QQuickItem *> all;
    itemsNamed(root, name, all);
    return all;
}

/// Every binding loop Qt reported while a test ran.
///
/// A loop is not an error to Qt: it warns, four times per layout, and carries on. Two of
/// them shipped in one day for exactly that reason — nothing read the warnings, and the
/// second was written by whoever had just fixed the first. `opens.sh` greps the launch for
/// them, but the launch only ever draws the shelf; a screen reached by a click is only
/// drawn here.
///
/// It is checked after **every** test and not in one of them, because the loop needs the
/// text to arrive *after* the first layout: a detail read from the environment is there
/// when the item is built and never loops, the same detail waited on from the server does.
/// Running one test alone left this guard silent and looking sound.
///
/// Behind a mutex, because `qInstallMessageHandler` documents that the handler must be
/// thread-safe and Qt takes it at its word: a socket warning arrives on whichever thread
/// the socket lives on. Appending to a plain `QStringList` from there while the test
/// thread cleared it corrupted the heap, and the run died with SIGSEGV at whatever it
/// touched next — four different tests over four runs on the integration machine, none of
/// them the one at fault, and never once on this one.
QMutex loopLock;
QStringList loops;
QtMessageHandler passItOn = nullptr;

void watchForLoops(QtMsgType type, const QMessageLogContext &where, const QString &said)
{
    if (said.contains(u"Binding loop"_s)) {
        const QMutexLocker held(&loopLock);
        loops.append(said);
    }
    if (passItOn)
        passItOn(type, where, said);
}

QStringList loopsSoFar()
{
    const QMutexLocker held(&loopLock);
    return loops;
}

void forgetLoops()
{
    const QMutexLocker held(&loopLock);
    loops.clear();
}

/// The text of a named item, or a sentence saying there was no such item.
///
/// `itemNamed(...)->property("text")` reads well and dereferences a null the moment the
/// item is not there. On a slower machine the filter panel's delegates were rebuilt
/// between two lines of one test — an axis found, its own tally gone — and the run ended
/// in SIGSEGV where a failed comparison would have named what was missing.
QString textNamed(QQuickItem *root, const QString &name);

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

QString textNamed(QQuickItem *root, const QString &name)
{
    QQuickItem *one = itemNamed(root, name);
    return one ? one->property("text").toString()
               : u"<aucun élément nommé "_s + name + u">"_s;
}

/// The filter button a hand can reach, and the panel hanging from it.
///
/// There are two rows of pills on the shelf — its own, and the one the search workspace
/// draws over its results — so there are two of each, and only one is ever on screen.
/// `findChild` hands back whichever comes first in the object tree, which was the button
/// inside the hidden row: a popup opened from an item in an invisible subtree.
QQuickItem *reachableFilterButton(QQuickWindow *window)
{
    QQuickItem *found = nullptr;
    for (QQuickItem *one : itemsNamed(window->contentItem(), u"filter-button"_s)) {
        if (one->isVisible())
            found = one;
    }
    return found;
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
        passItOn = qInstallMessageHandler(watchForLoops);
    }

    void cleanupTestCase() { qInstallMessageHandler(passItOn); }

    void init()
    {
        forgetLoops();
        // Never read the developer's real config or keyring when the shelf starts asking as
        // soon as its component completes. Port 1 refuses locally and deterministically.
        qputenv("LEAF_ADDRESS", QByteArrayLiteral("http://127.0.0.1:1"));
        qputenv("LEAF_KEY", QByteArrayLiteral("8f3a92c1d4e5b6a7"));
    }

    void cleanup()
    {
        qunsetenv("LEAF_ADDRESS");
        qunsetenv("LEAF_KEY");
        // Every test, not one: whichever screen a test drew, it drew it properly.
        const QStringList seen = loopsSoFar();
        QVERIFY2(seen.isEmpty(), qPrintable(seen.join(u"\n"_s)));
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
        const RestoresThePalette restoreOnExit;
        QPalette day = QGuiApplication::palette();
        day.setColor(QPalette::Window, Qt::white);
        QGuiApplication::setPalette(day);

        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        const QByteArray pageReply = aReply(
            200, QByteArrayLiteral("application/json"),
            aPage({
                aSeries(u"dn"_s, u"Death Note · Black Edition"_s, 7, true),
                aSeries(u"ac"_s, u"Assassination Classroom"_s, 21, false),
                aSeries(u"kq"_s, u"Koro Quest"_s, 5, false),
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
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));
        auto *shelf = engine.singletonInstance<Shelf *>(qmlTypeId("Leaf", 1, 0, "Shelf"));
        QVERIFY(shelf);
        QTRY_COMPARE(shelf->count(), 3);

        auto *grid = window->findChild<QQuickItem *>(u"shelf-grid"_s);
        QVERIFY(grid);
        QTRY_COMPARE(grid->property("count").toInt(), 3);
        QTRY_VERIFY(grid->property("activeFocusOnTab").toBool());

        QQuickItem *reading = nullptr;
        QTRY_VERIFY((reading = itemNamed(grid, u"tile-dn"_s)));
        auto *cover = itemNamed(reading, u"cover-frame-dn"_s);
        auto *title = itemNamed(reading, u"title-dn"_s);
        auto *volumes = itemNamed(reading, u"volumes-dn"_s);
        auto *mark = itemNamed(reading, u"in-progress-dn"_s);
        auto *ring = itemNamed(reading, u"focus-dn"_s);
        auto *shadow = itemNamed(reading, u"cover-shadow-dn"_s);
        auto *clipped = itemNamed(reading, u"clipped-cover-dn"_s);
        auto *coverImage = itemNamed(reading, u"cover-dn"_s);
        QVERIFY(cover);
        QVERIFY(title);
        QVERIFY(volumes);
        QVERIFY(mark);
        QVERIFY(ring);
        QVERIFY(shadow);
        QVERIFY(clipped);
        QVERIFY(coverImage);

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
        QCOMPARE(titleFont.pixelSize(), 16);
        QCOMPARE(titleFont.weight(), QFont::DemiBold);
        QCOMPARE(title->property("maximumLineCount").toInt(), 2);
        QCOMPARE(volumes->property("text").toString(), u"7 tomes"_s);
        const QFont volumesFont = volumes->property("font").value<QFont>();
        QCOMPARE(volumesFont.family(), Theme().textFamily());
        QCOMPARE(volumesFont.pixelSize(), 14);
        QCOMPARE(mark->opacity(), 1.0);

        auto *finished = itemNamed(grid, u"tile-ac"_s);
        QVERIFY(finished);
        auto *finishedMark = itemNamed(finished, u"in-progress-ac"_s);
        auto *finishedCover = itemNamed(finished, u"cover-frame-ac"_s);
        auto *finishedRing = itemNamed(finished, u"focus-ac"_s);
        QVERIFY(finishedMark);
        QVERIFY(finishedCover);
        QVERIFY(finishedRing);
        QCOMPARE(finishedMark->opacity(), 0.0);

        auto *queued = itemNamed(grid, u"tile-kq"_s);
        QVERIFY(queued);
        auto *queuedCover = itemNamed(queued, u"cover-frame-kq"_s);
        auto *queuedRing = itemNamed(queued, u"focus-kq"_s);
        QVERIFY(queuedCover);
        QVERIFY(queuedRing);

        QTRY_COMPARE(coverImage->property("status").toInt(), 1);
        const auto sampled = [window](const QPointF &point) {
            const QImage rendered = window->grabWindow();
            if (rendered.isNull())
                return QColor();
            const qreal xScale = rendered.width() / qreal(window->width());
            const qreal yScale = rendered.height() / qreal(window->height());
            return rendered.pixelColor(qRound(point.x() * xScale),
                                       qRound(point.y() * yScale));
        };
        const QPointF coverMiddlePoint =
            cover->mapToScene(QPointF(cover->width() / 2, cover->height() / 2));
        QTRY_COMPARE(sampled(coverMiddlePoint), QColor(u"#2B3550"_s));
        const QColor coverMiddle = sampled(coverMiddlePoint);
        QVERIFY(sampled(cover->mapToScene(QPointF(1, 1))) != coverMiddle);

        QTest::mouseMove(window, QPoint(window->width() - 2, window->height() - 2), 20);
        QTRY_VERIFY(!ring->isVisible());
        QTRY_VERIFY(!finishedRing->isVisible());
        const auto centreOf = [](QQuickItem *item) {
            return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
        };
        const auto visibleRingCount = [ring, finishedRing, queuedRing] {
            return int(ring->isVisible()) + int(finishedRing->isVisible())
                   + int(queuedRing->isVisible());
        };

        QTest::mouseMove(window, centreOf(cover), 20);
        QTRY_VERIFY(ring->isVisible());
        QTRY_COMPARE(visibleRingCount(), 1);

        // The pointer paints; it does not anchor, and it does not take the focus either —
        // hovering used to do both, so crossing the shelf with the mouse emptied the search
        // field of its focus in the middle of a word. The keyboard takes the shelf when it is
        // given it, and enters at the first cover even though the pointer is over that same
        // one. Then Right continues from where the keyboard stands.
        grid->forceActiveFocus(Qt::TabFocusReason);
        QTRY_COMPARE(grid->property("currentIndex").toInt(), 0);
        QTRY_VERIFY(ring->isVisible());
        QTest::keyClick(window, Qt::Key_Right);
        QTRY_COMPARE(grid->property("currentIndex").toInt(), 1);
        QTRY_VERIFY(finishedRing->isVisible());
        QTRY_COMPARE(visibleRingCount(), 1);

        // Moving the pointer takes the same cursor over; the next key starts there.
        QTest::mouseMove(window, centreOf(queuedCover), 20);
        QTRY_VERIFY(!finishedRing->isVisible());
        QTRY_VERIFY(queuedRing->isVisible());
        QTRY_COMPARE(visibleRingCount(), 1);

        // Leaving a temporary hover returns to the keyboard anchor. The pointer cannot
        // leave an invisible index behind from which a later key would unexpectedly start.
        QTest::mouseMove(window, QPoint(window->width() - 2, window->height() - 2), 20);
        QTRY_COMPARE(visibleRingCount(), 0);
        QTRY_COMPARE(grid->property("currentIndex").toInt(), 1);

        // And a key continues from where the keyboard stands, not from under the pointer: the
        // third cover is hovered, Left goes to the first — the one beside the second.
        QTest::mouseMove(window, centreOf(queuedCover), 20);
        QTRY_VERIFY(queuedRing->isVisible());
        QTest::keyClick(window, Qt::Key_Left);
        QTRY_COMPARE(grid->property("currentIndex").toInt(), 0);
        QTRY_VERIFY(ring->isVisible());
        QTRY_VERIFY(!queuedRing->isVisible());
        QTRY_COMPARE(visibleRingCount(), 1);

        // A click away clears both the visible emphasis and its stale position. The first
        // arrow and Tab then enter the application's focus order at its first participant.
        QTest::mouseMove(window, QPoint(window->width() - 2, window->height() - 2), 20);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          QPoint(window->width() - 2, window->height() - 2));
        QTRY_COMPARE(visibleRingCount(), 0);
        QCOMPARE(grid->property("currentIndex").toInt(), -1);

        auto *searchField = window->findChild<QQuickItem *>(u"search-field"_s);
        auto *filterButton = window->findChild<QQuickItem *>(u"filter-button"_s);
        auto *sortButton = window->findChild<QQuickItem *>(u"sort-button"_s);
        auto *settingsButton = window->findChild<QQuickItem *>(u"settings-button"_s);
        QVERIFY(searchField);
        QVERIFY(filterButton);
        QVERIFY(sortButton);
        QVERIFY(settingsButton);

        QTest::keyClick(window, Qt::Key_Right);
        QTRY_VERIFY(searchField->hasActiveFocus());
        QCOMPARE(grid->property("currentIndex").toInt(), -1);
        QTest::keyClick(window, Qt::Key_Tab);
        QTRY_VERIFY(sortButton->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Tab);
        QTRY_VERIFY(settingsButton->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Tab);
        QTRY_COMPARE(grid->property("currentIndex").toInt(), 0);
        QTRY_VERIFY(ring->isVisible());
        QTRY_COMPARE(visibleRingCount(), 1);

        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          QPoint(window->width() - 2, window->height() - 2));
        QTRY_COMPARE(visibleRingCount(), 0);
        QCOMPARE(grid->property("currentIndex").toInt(), -1);
        QTest::keyClick(window, Qt::Key_Tab);
        QTRY_VERIFY(searchField->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Tab);
        QTest::keyClick(window, Qt::Key_Tab);
        QTest::keyClick(window, Qt::Key_Tab);
        QTRY_COMPARE(grid->property("currentIndex").toInt(), 0);
        QTRY_VERIFY(ring->isVisible());
        QTRY_COMPARE(visibleRingCount(), 1);
        QTest::keyClick(window, Qt::Key_Tab);
        QTRY_COMPARE(grid->property("currentIndex").toInt(), 1);
        QTRY_VERIFY(!ring->isVisible());
        QTRY_VERIFY(finishedRing->isVisible());
        QTRY_COMPARE(visibleRingCount(), 1);

        QAccessibleInterface *accessible = QAccessible::queryAccessibleInterface(reading);
        QVERIFY(accessible);
        QCOMPARE(accessible->text(QAccessible::Name), u"Death Note · Black Edition"_s);
        QCOMPARE(accessible->text(QAccessible::Description), u"7 tomes"_s);
        auto *finishedAccessible = QAccessible::queryAccessibleInterface(finished);
        QVERIFY(finishedAccessible);
        QTRY_VERIFY(finishedAccessible->state().focusable);
        QTRY_VERIFY(finishedAccessible->state().focused);
        QVERIFY(!accessible->state().focused);

        // The bar is now the next page region. Crossing the grid boundary reaches its first
        // participant, and coming back enters the shelf through its last cover.
        QTest::keyClick(window, Qt::Key_Tab);
        QTRY_COMPARE(grid->property("currentIndex").toInt(), 2);
        QTest::keyClick(window, Qt::Key_Tab);
        QTRY_VERIFY(searchField->hasActiveFocus());
        QCOMPARE(grid->property("currentIndex").toInt(), -1);
        QTest::keyClick(window, Qt::Key_Backtab);
        QTRY_COMPARE(grid->property("currentIndex").toInt(), 2);
        QTest::keyClick(window, Qt::Key_Down);
        QTRY_VERIFY(searchField->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Up);
        QTRY_COMPARE(grid->property("currentIndex").toInt(), 2);

        // An item added to the tree is no longer part of the page's order: that order is
        // stated by the page — bar, band, row, covers, and round — rather than read from the
        // item tree, which cannot express it. What still holds is that a focus change caused
        // from outside the shelf clears the position the shelf was keeping.
        auto *page = window->findChild<QQuickItem *>(u"page-navigation-root"_s);
        auto *navigationStart = window->findChild<QQuickItem *>(u"navigation-start"_s);
        QVERIFY(page);
        QVERIFY(navigationStart);
        auto *elsewhere = new QQuickItem(page);
        elsewhere->setParentItem(page);
        elsewhere->setActiveFocusOnTab(true);
        elsewhere->setSize(QSizeF(1, 1));

        elsewhere->forceActiveFocus(Qt::MouseFocusReason);
        QTRY_COMPARE(visibleRingCount(), 0);
        QTRY_COMPARE(grid->property("currentIndex").toInt(), -1);

        // Once the page itself has been reset, direction no longer changes the entry point:
        // Left, Up and Backtab all restart at the first participant, just like Right and Tab.
        const QPoint blank(window->width() - 2, window->height() - 2);
        const auto resetAndPress =
            [window, grid, navigationStart, searchField, blank](int key) {
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, blank);
            QTRY_COMPARE(grid->property("currentIndex").toInt(), -1);
            QTRY_VERIFY(navigationStart->hasActiveFocus());
            QTest::keyClick(window, static_cast<Qt::Key>(key));
            QTRY_VERIFY(searchField->hasActiveFocus());
            QTRY_COMPARE(grid->property("currentIndex").toInt(), -1);
        };
        resetAndPress(Qt::Key_Left);
        resetAndPress(Qt::Key_Up);
        resetAndPress(Qt::Key_Backtab);

        auto *theme = engine.singletonInstance<Theme *>(qmlTypeId("Leaf", 1, 0, "Theme"));
        QVERIFY(theme);
        theme->setDark(true);
        QTRY_VERIFY(shadow->property("source").toString().endsWith(
            u"assets/cover-shadow-dark.png"_s));
    }

    /// The band the artifact puts above the grid, with the wording and the elevation it draws.
    /// Every string here was settled in C++ — this asserts the QML shows those and not others.
    void the_resume_band_draws_the_one_offer_above_the_grid()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        const QByteArray pageReply = aReply(
            200, QByteArrayLiteral("application/json"),
            aPage({aSeries(u"ac"_s, u"Assassination Classroom"_s, 21, true)}));
        const QByteArray nextReply =
            aReply(200, QByteArrayLiteral("application/json"), anOffer());
        const QByteArray coverReply =
            aReply(200, QByteArrayLiteral("image/png"), aCover());
        pretend.answerFor = [pageReply, nextReply, coverReply](const QByteArray &request) {
            if (request.startsWith("GET /next"))
                return nextReply;
            return request.startsWith("GET /series?") ? pageReply : coverReply;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));

        auto *band = window->findChild<QQuickItem *>(u"resume-band"_s);
        QVERIFY(band);
        QTRY_VERIFY(band->isVisible());

        auto *card = itemNamed(band, u"resume-card"_s);
        auto *name = itemNamed(band, u"resume-name"_s);
        auto *where = itemNamed(band, u"resume-where"_s);
        auto *action = itemNamed(band, u"resume-action"_s);
        auto *label = itemNamed(band, u"resume-action-text"_s);
        auto *track = itemNamed(band, u"resume-progress"_s);
        auto *fill = itemNamed(band, u"resume-progress-fill"_s);
        auto *cover = itemNamed(band, u"resume-cover"_s);
        auto *shadow = itemNamed(band, u"cover-shadow-resume"_s);
        QVERIFY(card);
        QVERIFY(name);
        QVERIFY(where);
        QVERIFY(action);
        QVERIFY(label);
        QVERIFY(track);
        QVERIFY(fill);
        QVERIFY(cover);
        QVERIFY(shadow);

        QTRY_COMPARE(name->property("text").toString(), u"Assassination Classroom"_s);
        QCOMPARE(where->property("text").toString(),
                 u"Tome 12 · Page 47/190 · Chapitre 98"_s);
        QCOMPARE(label->property("text").toString(), u"Reprendre"_s);
        QVERIFY(label->isVisible());
        QCOMPARE(cover->property("source").toString(),
                 u"http://127.0.0.1:%1/entries/volume-12/cover"_s.arg(pretend.serverPort()));

        const QFont nameFont = name->property("font").value<QFont>();
        QCOMPARE(nameFont.family(), Theme().displayFamily());
        QCOMPARE(nameFont.pixelSize(), 20);
        QCOMPARE(card->property("radius").toInt(), Theme().cardRadius());
        // A pill, and not the 12 px button radius the other two radii cover.
        QCOMPARE(action->property("radius").toDouble(), action->height() / 2);
        // The pill measures itself from the glyph and the word it was given. A width read
        // from a `childrenRect` that has collapsed to zero is a circle with a word spilling
        // out of it, and nothing else in this suite would notice.
        QVERIFY(action->width() > action->height());
        QVERIFY(label->x() + label->width() <= action->width());

        QVERIFY(track->isVisible());
        QCOMPARE(fill->width(), qRound(track->width() * 47.0 / 190.0));

        // Above the first row, and *in* the scroll rather than anchored over it: the band is
        // the view's header, so it is content, and a hundred pixels of scroll take it a
        // hundred pixels up. That is the artifact's rule — the band goes, the row stays.
        auto *grid = window->findChild<QQuickItem *>(u"shelf-grid"_s);
        auto *appBar = window->findChild<QQuickItem *>(u"app-bar"_s);
        QVERIFY(grid);
        QVERIFY(appBar);
        QTRY_COMPARE(grid->property("count").toInt(), 1);
        QQuickItem *tile = nullptr;
        QTRY_VERIFY((tile = itemNamed(grid, u"tile-ac"_s)));
        QVERIFY(band->height() > 0);
        QVERIFY(grid->property("headerItem").value<QQuickItem *>());
        QTRY_COMPARE(band->mapToItem(nullptr, QPointF(0, 0)).y(),
                     qreal(appBar->height()));
        QVERIFY(tile->mapToItem(nullptr, QPointF(0, 0)).y()
                >= appBar->height() + band->height());

        const qreal wasAt = grid->property("contentY").toReal();
        grid->setProperty("contentY", wasAt + 100);
        QCOMPARE(band->mapToItem(nullptr, QPointF(0, 0)).y(),
                 appBar->height() - 100.0);
    }

    /// Where the band stands in the reading order: before the grid, because it is above it,
    /// and the grid entered by its **first** cover. The walk inside the grid is the sibling
    /// test's sentence; this one is about the seam between the two, which is where the defect
    /// was. The button is in that chain deliberately before the reader it will open exists —
    /// a control put into the keyboard workflow "later" is a control nobody puts in.
    void the_band_comes_before_the_grid_in_the_reading_order()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        const QByteArray pageReply = aReply(
            200, QByteArrayLiteral("application/json"),
            aPage({
                aSeries(u"ac"_s, u"Assassination Classroom"_s, 21, true),
                aSeries(u"dn"_s, u"Death Note · Black Edition"_s, 7, false),
                aSeries(u"kq"_s, u"Koro Quest"_s, 5, false),
            }));
        const QByteArray nextReply =
            aReply(200, QByteArrayLiteral("application/json"), anOffer());
        const QByteArray coverReply =
            aReply(200, QByteArrayLiteral("image/png"), aCover());
        pretend.answerFor = [pageReply, nextReply, coverReply](const QByteArray &request) {
            if (request.startsWith("GET /next"))
                return nextReply;
            return request.startsWith("GET /series?") ? pageReply : coverReply;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->requestActivate();
        QTRY_VERIFY(window->isActive());

        auto *band = window->findChild<QQuickItem *>(u"resume-band"_s);
        auto *grid = window->findChild<QQuickItem *>(u"shelf-grid"_s);
        auto *navigationStart = window->findChild<QQuickItem *>(u"navigation-start"_s);
        auto *searchField = window->findChild<QQuickItem *>(u"search-field"_s);
        auto *filterButton = window->findChild<QQuickItem *>(u"filter-button"_s);
        auto *sortButton = window->findChild<QQuickItem *>(u"sort-button"_s);
        auto *settingsButton = window->findChild<QQuickItem *>(u"settings-button"_s);
        QVERIFY(band);
        QVERIFY(grid);
        QVERIFY(navigationStart);
        QVERIFY(searchField);
        QVERIFY(filterButton);
        QVERIFY(sortButton);
        QVERIFY(settingsButton);
        QTRY_VERIFY(band->isVisible());
        QTRY_COMPARE(grid->property("count").toInt(), 3);
        // Asked of the view each time rather than kept: a GridView may build its header
        // again, and a pointer taken once then names an item nobody is looking at.
        const auto bandButton = [grid]() -> QQuickItem * {
            auto *header = grid->property("headerItem").value<QQuickItem *>();
            return header ? header->property("button").value<QQuickItem *>() : nullptr;
        };
        const auto bandRing = [&bandButton]() -> QQuickItem * {
            auto *one = bandButton();
            return one ? itemNamed(one, u"resume-action-focus"_s) : nullptr;
        };
        auto *action = bandButton();
        auto *ring = itemNamed(band, u"resume-action-focus"_s);
        QVERIFY(action);
        QVERIFY(ring);
        // Out of Qt's own traversal on purpose: that traversal follows the item tree, and the
        // order this screen reads in — band, row, covers — cannot be written as a tree, the
        // band being inside the view and the row outside it. The screen drives it instead,
        // which is what the walk below asserts.
        QVERIFY(!action->property("activeFocusOnTab").toBool());
        QVERIFY(!ring->isVisible());

        // The pointer is parked above the band, to the right of its card, where no tile can
        // ever be: the cursor belongs to the X server and stays where the previous slot left
        // it, a window opening under it hovers whatever lands beneath, and a tile that hovers
        // takes the focus. Then the starting focus is set rather than clicked for — that a
        // click on bare paper resets the page is the sibling test's sentence, and borrowing
        // it here would only borrow its dependence on where the pointer happens to be.
        QTest::mouseMove(window, QPoint(window->width() - 2, 2));
        navigationStart->forceActiveFocus(Qt::MouseFocusReason);
        QTRY_VERIFY(navigationStart->hasActiveFocus());

        // The application bar is the page's first region. The band comes next, before the
        // grid, and its ring says where that shared navigation now stands.
        QTest::keyClick(window, Qt::Key_Tab);
        QTRY_VERIFY(searchField->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Tab);
        QTRY_VERIFY(sortButton->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Tab);
        QTRY_VERIFY(settingsButton->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Tab);
        QTRY_VERIFY(bandButton() && bandButton()->hasActiveFocus());
        QVERIFY(bandRing() && bandRing()->isVisible());

        // Down enters the grid by its first cover, not its last. The chain wraps — the button
        // is both the item after the grid and the item before it — and reading the direction
        // out of it landed on the last cover until the band was named outright.
        QTest::keyClick(window, Qt::Key_Down);
        QTRY_COMPARE(grid->property("currentIndex").toInt(), 0);
        QVERIFY(!bandRing() || !bandRing()->isVisible());

        // And the top row gives the focus back to the band rather than stopping dead.
        QTest::keyClick(window, Qt::Key_Up);
        QTRY_VERIFY(bandButton() && bandButton()->hasActiveFocus());
    }

    /// Hover and focus say different things, so they cannot look the same. The pill is filled
    /// with emerald already and cannot take a background the way an icon button will: it grows.
    void the_button_grows_under_the_pointer_and_shrinks_back()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        const QByteArray pageReply = aReply(
            200, QByteArrayLiteral("application/json"),
            aPage({aSeries(u"ac"_s, u"Assassination Classroom"_s, 21, true)}));
        const QByteArray nextReply =
            aReply(200, QByteArrayLiteral("application/json"), anOffer());
        const QByteArray coverReply =
            aReply(200, QByteArrayLiteral("image/png"), aCover());
        pretend.answerFor = [pageReply, nextReply, coverReply](const QByteArray &request) {
            if (request.startsWith("GET /next"))
                return nextReply;
            return request.startsWith("GET /series?") ? pageReply : coverReply;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));

        auto *band = window->findChild<QQuickItem *>(u"resume-band"_s);
        QVERIFY(band);
        QTRY_VERIFY(band->isVisible());
        auto *action = itemNamed(band, u"resume-action"_s);
        auto *ring = itemNamed(band, u"resume-action-focus"_s);
        QVERIFY(action);
        QVERIFY(ring);

        QCOMPARE(action->scale(), 1.0);
        QVERIFY(!action->property("hovered").toBool());

        const QPointF middle =
            action->mapToItem(nullptr, QPointF(action->width() / 2, action->height() / 2));
        // Twice, and the first one elsewhere in the window: a synthetic move that is the
        // pointer's *first* position in a window delivers no hover at all, and the test then
        // waits five seconds for a state nothing will ever set.
        QTest::mouseMove(window, QPoint(window->width() / 2, window->height() - 4));
        QTest::mouseMove(window, middle.toPoint());
        QTRY_VERIFY(action->property("hovered").toBool());
        QTRY_VERIFY(action->scale() > 1.0);
        // The pointer is not the keyboard: hovering lights no ring.
        QVERIFY(!ring->isVisible());

        QTest::mouseMove(window, QPoint(window->width() - 2, window->height() - 2));
        QTRY_VERIFY(!action->property("hovered").toBool());
        QTRY_COMPARE(action->scale(), 1.0);
    }

    /// The bar must not fall back to Basic's white overlays: its hover help and sort choices
    /// are Leaf surfaces, and the selected sort is the application's emerald radio mark.
    /// The mark itself is deliberately large enough to remain a brand, not a toolbar glyph.
    void the_bar_overlays_and_brand_belong_to_leaf()
    {
        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));

        auto *theme = engine.singletonInstance<Theme *>(
            qmlTypeId("Leaf", 1, 0, "Theme"));
        QVERIFY(theme);

        auto *mark = window->findChild<QQuickItem *>(u"leaf-mark"_s);
        auto *filterButton = window->findChild<QQuickItem *>(u"filter-button"_s);
        auto *sortButton = window->findChild<QQuickItem *>(u"sort-button"_s);
        QVERIFY(mark);
        QVERIFY(filterButton);
        QVERIFY(sortButton);
        // The mark is drawn small and the word carries the name: 36 against a 25-pixel word,
        // which is the balance asked for once both were on screen — a leaf that competes with
        // its own name reads as two logos.
        QCOMPARE(mark->width(), 36.0);
        QCOMPARE(mark->height(), 36.0);
        auto *word = window->findChild<QQuickItem *>(u"brand-word"_s);
        QVERIFY(word);
        QCOMPARE(word->property("font").value<QFont>().pixelSize(), 25);
        QVERIFY2(word->x() - (mark->x() + mark->width()) >= 10,
                 "the word is not crowded against the mark");
        QVERIFY(mark->property("source").toUrl().toString().contains(u"leaf-mark-"_s));

        // And the name is not crowded against the field either. The field is a pill whose
        // focus ring is drawn outside its edge, so at the row's spacing alone that ring came
        // to rest on the "f" of Leaf and the two read as one object.
        auto *field = window->findChild<QQuickItem *>(u"search-field"_s);
        QVERIFY(field);
        const qreal wordRight =
            word->mapToItem(window->contentItem(), QPointF(word->width(), 0)).x();
        const qreal fieldLeft = field->mapToItem(window->contentItem(), QPointF(0, 0)).x();
        QVERIFY2(fieldLeft - wordRight >= 30, "the field is crowded against the name");

        auto *tip = filterButton->findChild<QObject *>(u"filter-button-tooltip"_s);
        QVERIFY(tip);
        QCOMPARE(tip->property("delay").toInt(), 450);
        // Open the popup itself here. Hover delivery is a window-system concern and is
        // already exercised by the button test above; this slot owns the overlay's drawing.
        QVERIFY(QMetaObject::invokeMethod(tip, "open"));
        QTRY_VERIFY(tip->property("visible").toBool());

        auto *tipPanel = tip->findChild<QQuickItem *>(u"filter-button-tooltip-panel"_s);
        QVERIFY(tipPanel);
        QCOMPARE(tipPanel->property("color").value<QColor>(), theme->surface());
        QCOMPARE(tipPanel->property("radius").toReal(), 9.0);

        QVERIFY(QMetaObject::invokeMethod(tip, "close"));
        QTRY_VERIFY(!tip->property("visible").toBool());

        auto *menu = sortButton->findChild<QObject *>(u"sort-menu"_s);
        QVERIFY(menu);
        QVERIFY(QMetaObject::invokeMethod(menu, "open"));
        QTRY_VERIFY(menu->property("opened").toBool());

        auto *menuPanel = menu->findChild<QQuickItem *>(u"sort-menu-panel"_s);
        auto *selected = menu->findChild<QQuickItem *>(u"sort-option-name"_s);
        QVERIFY(menuPanel);
        QVERIFY(selected);
        QCOMPARE(menuPanel->property("color").value<QColor>(), theme->surface());
        QCOMPARE(menuPanel->property("radius").toReal(), qreal(theme->buttonRadius()));
        QVERIFY(selected->property("checked").toBool());

        auto *dot = itemNamed(selected, u"sort-option-name-dot"_s);
        QVERIFY(dot);
        QVERIFY(dot->isVisible());
        QCOMPARE(dot->property("color").value<QColor>(), theme->emerald());

        QVERIFY(QMetaObject::invokeMethod(menu, "close"));
        QTRY_VERIFY(!menu->property("opened").toBool());
    }

    /// The row that says what you are looking at: worded and counted in C++, in the order the
    /// client decided, and staying at the top while the band leaves underneath it. Lighting a
    /// pill sends the contract's own spelling back, which is the whole of what a chip does.
    void the_row_says_what_you_are_looking_at_and_stays_while_the_band_goes()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        QJsonArray many;
        for (int i = 0; i < 12; ++i) {
            many << aSeries(u"s%1"_s.arg(i), u"Série %1"_s.arg(i), 3, false);
        }
        const QByteArray pageReply =
            aReply(200, QByteArrayLiteral("application/json"), aPage(many));
        const QByteArray nextReply =
            aReply(200, QByteArrayLiteral("application/json"), anOffer());
        const QByteArray filtersReply =
            aReply(200, QByteArrayLiteral("application/json"), someFilters());
        const QByteArray coverReply =
            aReply(200, QByteArrayLiteral("image/png"), aCover());
        pretend.answerFor = [pageReply, nextReply, filtersReply,
                             coverReply](const QByteArray &request) {
            if (request.startsWith("GET /next"))
                return nextReply;
            if (request.startsWith("GET /filters"))
                return filtersReply;
            return request.startsWith("GET /series?") ? pageReply : coverReply;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));

        auto *pills = window->findChild<QQuickItem *>(u"filter-pills"_s);
        auto *band = window->findChild<QQuickItem *>(u"resume-band"_s);
        auto *grid = window->findChild<QQuickItem *>(u"shelf-grid"_s);
        QVERIFY(pills);
        QVERIFY(band);
        QVERIFY(grid);
        QTRY_VERIFY(pills->isVisible());
        QTRY_COMPARE(grid->property("count").toInt(), 12);

        auto *unread = itemNamed(pills, u"chip-UNREAD"_s);
        auto *read = itemNamed(pills, u"chip-READ"_s);
        auto *manga = itemNamed(pills, u"chip-manga"_s);
        auto *bd = itemNamed(pills, u"chip-bd"_s);
        auto *rule = itemNamed(pills, u"filter-separator"_s);
        QVERIFY(unread);
        QVERIFY(read);
        QVERIFY(manga);
        QVERIFY(bd);
        QVERIFY(rule);
        QVERIFY(rule->isVisible());

        // Worded, counted, and in the order `Words.h` writes down — read status first, then
        // the kind of book, each left to right.
        auto *said = itemNamed(pills, u"chip-UNREAD-text"_s);
        QVERIFY(said);
        QCOMPARE(said->property("text").toString(), u"Non lues 12"_s);
        QTRY_VERIFY(unread->x() < read->x());
        QTRY_VERIFY(read->x() < rule->x());
        QTRY_VERIFY(rule->x() < manga->x());
        QTRY_VERIFY(manga->x() < bd->x());

        // Below the band at rest, and above the first cover.
        QTRY_VERIFY(band->isVisible());
        const qreal bandBottom =
            band->mapToItem(nullptr, QPointF(0, band->height())).y();
        QTRY_COMPARE(pills->mapToItem(nullptr, QPointF(0, 0)).y(), bandBottom);

        // The order the eye reads, which no focus chain can produce on its own: the band is
        // inside the view and the row is outside it, so the chain puts the row after the
        // covers whichever way the two are declared. The screen says the order instead.
        auto *button = itemNamed(band, u"resume-action"_s);
        auto *navigationStart = window->findChild<QQuickItem *>(u"navigation-start"_s);
        auto *searchField = window->findChild<QQuickItem *>(u"search-field"_s);
        auto *filterButton = window->findChild<QQuickItem *>(u"filter-button"_s);
        auto *sortButton = window->findChild<QQuickItem *>(u"sort-button"_s);
        auto *settingsButton = window->findChild<QQuickItem *>(u"settings-button"_s);
        QVERIFY(button);
        QVERIFY(navigationStart);
        QVERIFY(searchField);
        QVERIFY(filterButton);
        QVERIFY(sortButton);
        QVERIFY(settingsButton);
        QTest::mouseMove(window, QPoint(window->width() - 2, 2));
        window->requestActivate();
        QTRY_VERIFY(window->isActive());
        navigationStart->forceActiveFocus(Qt::MouseFocusReason);
        QTRY_VERIFY(navigationStart->hasActiveFocus());

        QTest::keyClick(window, Qt::Key_Tab);
        QTRY_VERIFY(searchField->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Tab);
        QTRY_VERIFY(sortButton->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Tab);
        QTRY_VERIFY(settingsButton->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Tab);
        QTRY_VERIFY(button->hasActiveFocus());
        // The filter button is the head of this row now, before the pills it produces —
        // it used to be the bar's second stop, an inch from the field and a screen away
        // from what it narrows.
        QTest::keyClick(window, Qt::Key_Tab);
        QTRY_VERIFY(filterButton->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Tab);
        QTRY_VERIFY(unread->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Right);
        QTRY_VERIFY(read->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Down);
        QTRY_COMPARE(grid->property("currentIndex").toInt(), 0);

        // And back up through the same three, by the row's far end.
        QTest::keyClick(window, Qt::Key_Up);
        QTRY_VERIFY(bd->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Left);
        QTRY_VERIFY(manga->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Up);
        QTRY_VERIFY(button->hasActiveFocus());

        // And the order is a loop, closed at both ends. Above the band is the bar again, by
        // its far end; and backwards from the bar's first stop is the shelf's last cover.
        QTest::keyClick(window, Qt::Key_Up);
        QTRY_VERIFY(settingsButton->hasActiveFocus());

        searchField->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY(searchField->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Backtab);
        QTRY_COMPARE(grid->property("currentIndex").toInt(), 11);

        // Lighting one sends the contract's spelling to the shelf, and the shelf asks again.
        auto *shelf = engine.singletonInstance<Shelf *>(qmlTypeId("Leaf", 1, 0, "Shelf"));
        QVERIFY(shelf);
        const QPointF middle = bd->mapToItem(nullptr, QPointF(bd->width() / 2,
                                                             bd->height() / 2));
        QTest::mouseMove(window, QPoint(window->width() / 2, window->height() - 4));
        QTest::mouseMove(window, middle.toPoint());
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, middle.toPoint());
        QTRY_COMPARE(shelf->media(), QStringList({u"bd"_s}));
        QTRY_VERIFY(bd->property("lit").toBool());
        QVERIFY(!manga->property("lit").toBool());
        // Asked for, not just remembered: the property is set before the request goes out.
        QTRY_VERIFY(pretend.heard.contains("medium=bd"));

        // And the rule the artifact gives them: the band leaves with the scroll, the row does
        // not. Scrolled past the band's own height, the row is at the top and the band is gone.
        //
        // After the new list has settled, not during: a filter starts the shelf again, and a
        // new list is shown from its beginning — scrolling into one that is still arriving is
        // scrolling something that is about to be put back.
        QTest::qWait(400);
        QTest::mouseMove(window, QPoint(window->width() - 2, 2));
        grid->setProperty("contentY",
                          grid->property("contentY").toReal() + band->height() + 40);
        auto *appBar = window->findChild<QQuickItem *>(u"app-bar"_s);
        QVERIFY(appBar);
        QTRY_COMPARE(pills->mapToItem(nullptr, QPointF(0, 0)).y(), appBar->height());
        QVERIFY(band->mapToItem(nullptr, QPointF(0, band->height())).y()
                < appBar->height());
    }

    /// The field, end to end: what is typed reaches the shelf as a query and `/search` as a
    /// question, the files come back as lines above the grid, and the band goes — you are
    /// looking for something precise, and being told where you were is noise at that moment.
    /// Thirteen tests cover the model; this one covers that the screen is wired to it.
    void typing_in_the_bar_searches_the_shelf_and_lists_what_it_found()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        const QByteArray pageReply = aReply(
            200, QByteArrayLiteral("application/json"),
            aPage({aSeries(u"pd"_s, u"Parasite · Édition Deluxe"_s, 8, false)}, 6));
        const QByteArray nextReply =
            aReply(200, QByteArrayLiteral("application/json"), anOffer());
        const QByteArray filtersReply =
            aReply(200, QByteArrayLiteral("application/json"), someFilters());
        QJsonArray foundFiles;
        for (int number = 1; number <= 4; ++number) {
            foundFiles << QJsonObject{
                {u"kind"_s, u"ENTRY"_s},
                {u"id"_s, u"pd-v%1"_s.arg(number)},
                {u"label"_s, u"Tome %1"_s.arg(number)},
                {u"seriesId"_s, u"pd"_s},
                {u"seriesName"_s, u"Parasite · Édition Deluxe"_s},
                {u"entryId"_s, u"pd-v%1"_s.arg(number)},
                {u"entryKind"_s, u"VOLUME"_s},
                {u"entryNumber"_s, double(number)},
                {u"entryPageCount"_s, 190},
            };
        }
        const QByteArray hitsReply = aReply(
            200, QByteArrayLiteral("application/json"),
            QJsonDocument(QJsonObject{{u"items"_s, foundFiles},
                                      // Four are in hand; sixty is the exact tab and heading.
                                      {u"total"_s, 4},
                                      {u"fileTotal"_s, 60},
                                      {u"page"_s, 0},
                                      {u"size"_s, 50}})
                .toJson(QJsonDocument::Compact));
        const QByteArray coverReply =
            aReply(200, QByteArrayLiteral("image/png"), aCover());
        pretend.answerFor = [pageReply, nextReply, filtersReply, hitsReply,
                             coverReply](const QByteArray &request) {
            if (request.startsWith("GET /next"))
                return nextReply;
            if (request.startsWith("GET /filters"))
                return filtersReply;
            if (request.startsWith("GET /search"))
                return hitsReply;
            return request.startsWith("GET /series?") ? pageReply : coverReply;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->requestActivate();
        QTRY_VERIFY(window->isActive());

        auto *field = window->findChild<QQuickItem *>(u"search-field"_s);
        auto *band = window->findChild<QQuickItem *>(u"resume-band"_s);
        QVERIFY(field);
        QVERIFY(band);
        QTRY_VERIFY(band->isVisible());

        auto *shelf = engine.singletonInstance<Shelf *>(qmlTypeId("Leaf", 1, 0, "Shelf"));
        QVERIFY(shelf);

        QTest::mouseMove(window, QPoint(window->width() - 2, 2));
        field->forceActiveFocus(Qt::MouseFocusReason);
        QTRY_VERIFY(field->hasActiveFocus());
        pretend.heard.clear();
        // One letter, and the wiring is what this asserts — not how many keys make a word.
        // Eight of them would put eight searches and eight shelf reloads in flight at once,
        // and the forty-line server answers one request per connection: the last answer, the
        // one that matters, would be the one nobody ever sends. That the client asks again on
        // every keystroke is worth knowing, and is not this test's subject.
        QTest::keyClick(window, Qt::Key_P);

        QTRY_COMPARE(shelf->query(), u"p"_s);
        QTRY_VERIFY(pretend.heard.contains("q=p&"));
        QTRY_VERIFY(pretend.heard.contains("GET /search"));

        // Proposal one: series first in a bounded preview, then four detailed file lines.
        // Neither action expands those lines in place; each switches to a persistent scope.
        auto *workspace = window->findChild<QQuickItem *>(u"search-workspace"_s);
        auto *overview = window->findChild<QQuickItem *>(u"search-overview"_s);
        auto *results = window->findChild<QQuickItem *>(u"search-overview-files"_s);
        QVERIFY(workspace);
        QVERIFY(overview);
        QVERIFY(results);
        QTRY_VERIFY(workspace->isVisible());
        QTRY_VERIFY(overview->isVisible());

        auto *seriesHeading = itemNamed(workspace, u"overview-series-heading"_s);
        QVERIFY(seriesHeading);
        QTRY_COMPARE(seriesHeading->property("text").toString(), u"Séries · 6"_s);

        auto *scopeFilters = itemNamed(workspace, u"search-series-filters"_s);
        QVERIFY(scopeFilters);
        QVERIFY(!scopeFilters->isVisible());

        // A preview is exactly one row. At a two-column width, six matches therefore leave
        // one explicit route to the complete scope instead of growing a second row in place.
        const QSize originalSize = window->size();
        window->resize(580, originalSize.height());
        auto *seeAllSeries = itemNamed(workspace, u"see-all-series"_s);
        QVERIFY(seeAllSeries);
        QTRY_VERIFY(seeAllSeries->isVisible());
        QCOMPARE(seeAllSeries->property("label").toString(), u"Voir les 6 séries"_s);
        window->resize(originalSize);

        workspace->setProperty("mode", 1);
        QTRY_VERIFY(scopeFilters->isVisible());
        workspace->setProperty("mode", 0);
        QTRY_VERIFY(!scopeFilters->isVisible());

        QQuickItem *row = nullptr;
        QTRY_VERIFY((row = itemNamed(results, u"search-result-pd-v1"_s)));
        QVERIFY(row->isVisible());
        auto *context = itemNamed(row, u"search-context-pd-v1"_s);
        QVERIFY(context);
        QCOMPARE(context->property("text").toString(),
                 u"Parasite · Édition Deluxe · 190 pages"_s);

        auto *heading = itemNamed(workspace, u"search-files-heading"_s);
        QVERIFY(heading);
        QCOMPARE(heading->property("text").toString(), u"Fichiers · 60"_s);

        auto *overviewTab = itemNamed(workspace, u"search-tab-overview"_s);
        auto *seriesTab = itemNamed(workspace, u"search-tab-series"_s);
        auto *filesTab = itemNamed(workspace, u"search-tab-files"_s);
        QVERIFY(overviewTab);
        QVERIFY(seriesTab);
        QVERIFY(filesTab);
        QCOMPARE(itemNamed(filesTab, u"search-tab-files-text"_s)
                     ->property("text").toString(),
                 u"Fichiers · 60"_s);

        // Hover only paints. The field keeps receiving text until a click or a navigation key
        // deliberately moves the focus elsewhere.
        const QPoint filesTabCentre = filesTab
                                          ->mapToItem(window->contentItem(),
                                                      QPointF(filesTab->width() / 2.0,
                                                              filesTab->height() / 2.0))
                                          .toPoint();
        QTest::mouseMove(window, filesTabCentre, 20);
        QTRY_VERIFY(field->hasActiveFocus());

        // A keyboard arrival owns the emerald ring. A mouse click may focus the command so
        // keys keep working from there, but it must not leave a second outline around an
        // already selected pill.
        auto *overviewFocus = itemNamed(overviewTab, u"search-tab-overview-focus"_s);
        QVERIFY(overviewFocus);
        overviewTab->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY(overviewFocus->isVisible());
        field->forceActiveFocus(Qt::MouseFocusReason);
        QTRY_VERIFY(field->hasActiveFocus());
        const QPoint overviewTabCentre = overviewTab
                                             ->mapToItem(window->contentItem(),
                                                         QPointF(overviewTab->width() / 2.0,
                                                                 overviewTab->height() / 2.0))
                                             .toPoint();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, overviewTabCentre);
        QTRY_VERIFY(overviewTab->hasActiveFocus());
        QVERIFY(!overviewFocus->isVisible());
        QVERIFY(!field->hasActiveFocus());

        // Scrollable content recedes under a quiet paper veil at either edge instead of
        // ending on a hard cut. Each veil appears only when there is content beyond it.
        auto *topVeil = itemNamed(workspace, u"overview-scroll-veil-top"_s);
        auto *bottomVeil = itemNamed(workspace, u"overview-scroll-veil-bottom"_s);
        QVERIFY(topVeil);
        QVERIFY(bottomVeil);
        QTRY_VERIFY(bottomVeil->property("shown").toBool());
        overview->setProperty("contentY", 24.0);
        QTRY_VERIFY(topVeil->property("shown").toBool());
        overview->setProperty("contentY", 0.0);

        auto *seeAll = itemNamed(workspace, u"see-all-files"_s);
        QVERIFY(seeAll);
        QVERIFY(seeAll->isVisible());
        QVERIFY(QMetaObject::invokeMethod(seeAll, "triggered"));
        auto *allFiles = itemNamed(workspace, u"search-all-files"_s);
        QVERIFY(allFiles);
        QTRY_VERIFY(allFiles->isVisible());
        QVERIFY(!overview->isVisible());
        QTRY_VERIFY(scopeFilters->isVisible());

        // And the band goes: you are looking for something precise.
        QTRY_VERIFY(!band->isVisible());

        // Escape clears the field before it means anything else.
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(shelf->query().isEmpty());
        QTRY_VERIFY(band->isVisible());
    }

    /// An overview of a single section is that section, with a heading above it and one more
    /// step to reach it. When only files matched, their scope opens straight away — and the
    /// moment the reader names a scope themselves, later answers leave them where they are.
    void a_search_that_found_only_files_opens_their_scope()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        // No series bears the name, so the series scope has nothing and the overview would
        // be a heading, an emptiness, and a route to the one section that holds anything.
        const QByteArray emptyPage =
            aReply(200, QByteArrayLiteral("application/json"), aPage({}, 0));
        const QByteArray nextReply =
            aReply(200, QByteArrayLiteral("application/json"), anOffer());
        const QByteArray filtersReply =
            aReply(200, QByteArrayLiteral("application/json"), someFilters());
        QJsonArray foundFiles;
        for (int number = 1; number <= 3; ++number) {
            foundFiles << QJsonObject{
                {u"kind"_s, u"ENTRY"_s},
                {u"id"_s, u"pd-v%1"_s.arg(number)},
                {u"label"_s, u"Tome %1"_s.arg(number)},
                {u"seriesId"_s, u"pd"_s},
                {u"seriesName"_s, u"Parasite · Édition Deluxe"_s},
                {u"entryId"_s, u"pd-v%1"_s.arg(number)},
                {u"entryKind"_s, u"VOLUME"_s},
                {u"entryNumber"_s, double(number)},
                {u"entryPageCount"_s, 190},
            };
        }
        const QByteArray hitsReply =
            aReply(200, QByteArrayLiteral("application/json"),
                   QJsonDocument(QJsonObject{{u"items"_s, foundFiles},
                                             {u"total"_s, 3},
                                             {u"fileTotal"_s, 3},
                                             {u"page"_s, 0},
                                             {u"size"_s, 50}})
                       .toJson(QJsonDocument::Compact));
        const QByteArray coverReply =
            aReply(200, QByteArrayLiteral("image/png"), aCover());
        pretend.answerFor = [emptyPage, nextReply, filtersReply, hitsReply,
                             coverReply](const QByteArray &request) {
            if (request.startsWith("GET /next"))
                return nextReply;
            if (request.startsWith("GET /filters"))
                return filtersReply;
            if (request.startsWith("GET /search"))
                return hitsReply;
            return request.startsWith("GET /series?") ? emptyPage : coverReply;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->requestActivate();
        QTRY_VERIFY(window->isActive());

        auto *field = window->findChild<QQuickItem *>(u"search-field"_s);
        QVERIFY(field);
        QTest::mouseMove(window, QPoint(window->width() - 2, 2));
        field->forceActiveFocus(Qt::MouseFocusReason);
        QTRY_VERIFY(field->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_P);

        auto *workspace = window->findChild<QQuickItem *>(u"search-workspace"_s);
        QVERIFY(workspace);
        QTRY_VERIFY(workspace->isVisible());

        auto *allFiles = itemNamed(workspace, u"search-all-files"_s);
        auto *overview = window->findChild<QQuickItem *>(u"search-overview"_s);
        QVERIFY(allFiles);
        QVERIFY(overview);
        QTRY_COMPARE(workspace->property("mode").toInt(), 2);
        QTRY_VERIFY(allFiles->isVisible());
        QVERIFY(!overview->isVisible());

        // The tabs are still the way back, and taking one settles the scope: it is the
        // reader's choice from then on, not a shape the counts keep deciding.
        auto *overviewTab = itemNamed(workspace, u"search-tab-overview"_s);
        QVERIFY(overviewTab);
        QVERIFY(QMetaObject::invokeMethod(workspace, "selectMode", Q_ARG(QVariant, 0)));
        QTRY_VERIFY(overview->isVisible());
        QVERIFY(workspace->property("scopeChosen").toBool());
        QCOMPARE(workspace->property("mode").toInt(), 0);
    }

    /// The second click on the criterion in force is the only way to reverse an order, so it
    /// is clicked here rather than invoked: a signal emitted by hand would pass while the
    /// real press was being eaten by the menu.
    void clicking_the_order_in_force_a_second_time_reverses_it()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        const QByteArray pageReply = aReply(
            200, QByteArrayLiteral("application/json"),
            aPage({aSeries(u"ac"_s, u"Assassination Classroom"_s, 21, false)}));
        const QByteArray emptyNext =
            aReply(200, QByteArrayLiteral("application/json"), QByteArrayLiteral("[]"));
        const QByteArray coverReply =
            aReply(200, QByteArrayLiteral("image/png"), aCover());
        pretend.answerFor = [pageReply, emptyNext, coverReply](const QByteArray &request) {
            if (request.startsWith("GET /next"))
                return emptyNext;
            return request.startsWith("GET /series?") ? pageReply : coverReply;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->requestActivate();
        QTRY_VERIFY(window->isActive());

        auto *shelf = engine.singletonInstance<Shelf *>(qmlTypeId("Leaf", 1, 0, "Shelf"));
        QVERIFY(shelf);
        QCOMPARE(shelf->sort(), u"name"_s);
        QCOMPARE(shelf->sortDirection(), u"asc"_s);

        auto *sortButton = window->findChild<QQuickItem *>(u"sort-button"_s);
        QVERIFY(sortButton);
        auto *menu = sortButton->findChild<QObject *>(u"sort-menu"_s);
        QVERIFY(menu);

        const auto clickOption = [&](const QString &name) {
            QVERIFY(QMetaObject::invokeMethod(menu, "open"));
            QTRY_VERIFY(menu->property("opened").toBool());
            QQuickItem *option = nullptr;
            QTRY_VERIFY((option = window->findChild<QQuickItem *>(name)));
            QTRY_VERIFY(option->width() > 0);
            const QPoint centre =
                option->mapToScene(QPointF(option->width() / 2.0, option->height() / 2.0))
                    .toPoint();
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centre);
            QTRY_VERIFY(!menu->property("opened").toBool());
        };

        // A different criterion first: it is selected, in its own familiar direction.
        clickOption(u"sort-option-volumes"_s);
        QTRY_COMPARE(shelf->sort(), u"volumes"_s);
        QCOMPARE(shelf->sortDirection(), u"desc"_s);

        // And the same one again, which is the reversal.
        clickOption(u"sort-option-volumes"_s);
        QTRY_COMPARE(shelf->sortDirection(), u"asc"_s);
        QVERIFY(shelf->sortReversed());

        // A third time comes back rather than staying reversed for good.
        clickOption(u"sort-option-volumes"_s);
        QTRY_COMPARE(shelf->sortDirection(), u"desc"_s);
    }

    /// Against a server that honours the direction, the tiles on screen come back in the
    /// other order. Every other test of this stops at the model: a shelf whose rows were
    /// reordered and whose grid was not looks, to the person holding the mouse, exactly like
    /// a shelf that ignored the click.
    void reversing_the_order_reorders_the_tiles_on_screen()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        const QByteArray emptyNext =
            aReply(200, QByteArrayLiteral("application/json"), QByteArrayLiteral("[]"));
        const QByteArray coverReply =
            aReply(200, QByteArrayLiteral("image/png"), aCover());
        const QByteArray forwards = aReply(
            200, QByteArrayLiteral("application/json"),
            aPage({aSeries(u"ac"_s, u"Assassination Classroom"_s, 21, false),
                   aSeries(u"dn"_s, u"Death Note"_s, 6, false),
                   aSeries(u"kq"_s, u"Koro Quest"_s, 5, false)},
                  3));
        const QByteArray backwards = aReply(
            200, QByteArrayLiteral("application/json"),
            aPage({aSeries(u"kq"_s, u"Koro Quest"_s, 5, false),
                   aSeries(u"dn"_s, u"Death Note"_s, 6, false),
                   aSeries(u"ac"_s, u"Assassination Classroom"_s, 21, false)},
                  3));
        // The whole of what a server honouring `direction` does, and nothing else: the same
        // three series, the other way round.
        pretend.answerFor = [forwards, backwards, emptyNext,
                             coverReply](const QByteArray &request) {
            if (request.startsWith("GET /next"))
                return emptyNext;
            if (!request.startsWith("GET /series?"))
                return coverReply;
            return request.contains("direction=desc") ? backwards : forwards;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->requestActivate();
        QTRY_VERIFY(window->isActive());

        auto *grid = window->findChild<QQuickItem *>(u"shelf-grid"_s);
        QVERIFY(grid);
        QTRY_COMPARE(grid->property("count").toInt(), 3);

        // Where each tile actually sits, which is the only thing the reader can see. Read
        // from the scene rather than from the model: the question is whether the grid moved.
        const auto leftEdgeOf = [grid](const QString &id) {
            QQuickItem *tile = itemNamed(grid, u"tile-"_s + id);
            return tile ? tile->mapToScene(QPointF(0, 0)).x() : -1.0;
        };

        QTRY_VERIFY(leftEdgeOf(u"kq"_s) > 0);
        QVERIFY(leftEdgeOf(u"ac"_s) < leftEdgeOf(u"dn"_s));
        QVERIFY(leftEdgeOf(u"dn"_s) < leftEdgeOf(u"kq"_s));

        auto *shelf = engine.singletonInstance<Shelf *>(qmlTypeId("Leaf", 1, 0, "Shelf"));
        QVERIFY(shelf);
        // Already on `name`, so this is the second click: the reversal.
        shelf->sortBy(u"name"_s);
        QCOMPARE(shelf->sortDirection(), u"desc"_s);

        QTRY_VERIFY(leftEdgeOf(u"kq"_s) < leftEdgeOf(u"ac"_s));
        QVERIFY(leftEdgeOf(u"kq"_s) < leftEdgeOf(u"dn"_s));
        QVERIFY(leftEdgeOf(u"dn"_s) < leftEdgeOf(u"ac"_s));
    }

    /// The filter button opens every axis the library can be narrowed by — the two the row
    /// draws and the ones it cannot. Narrowing by one of those has to show somewhere: a
    /// shelf cut to three series with nothing on screen saying why looks broken.
    void the_filter_button_opens_every_axis_and_a_choice_shows_in_the_row()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        const QByteArray pageReply = aReply(
            200, QByteArrayLiteral("application/json"),
            aPage({aSeries(u"ac"_s, u"Assassination Classroom"_s, 21, false)}, 1));
        const QByteArray emptyNext =
            aReply(200, QByteArrayLiteral("application/json"), QByteArrayLiteral("[]"));
        const QByteArray filtersReply =
            aReply(200, QByteArrayLiteral("application/json"), someFilters());
        const QByteArray coverReply =
            aReply(200, QByteArrayLiteral("image/png"), aCover());
        pretend.answerFor = [pageReply, emptyNext, filtersReply,
                             coverReply](const QByteArray &request) {
            if (request.startsWith("GET /next"))
                return emptyNext;
            if (request.startsWith("GET /filters"))
                return filtersReply;
            return request.startsWith("GET /series?") ? pageReply : coverReply;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->requestActivate();
        QTRY_VERIFY(window->isActive());

        QQuickItem *button = nullptr;
        QTRY_VERIFY((button = reachableFilterButton(window)));
        auto *panel = button->findChild<QObject *>(u"filter-panel"_s);
        QVERIFY(panel);
        QVERIFY(!panel->property("opened").toBool());

        QVERIFY(QMetaObject::invokeMethod(panel, "open"));
        QTRY_VERIFY(panel->property("opened").toBool());

        // Four axes: genre, universe, language and author. The panel has the room the row
        // has not, so nothing is dropped for being long — and it leaves out the two the row
        // draws, which sit as pills an inch below the button and were offered twice.
        QTRY_COMPARE(panel->property("axes").toList().size(), 4);
        QQuickItem *page = window->contentItem();
        for (const QString &axis : {u"genre"_s, u"universe"_s, u"language"_s, u"author"_s}) {
            QVERIFY2(itemNamed(page, u"filter-axis-"_s + axis),
                     qPrintable(u"no axis "_s + axis));
        }
        for (const QString &axis : {u"read"_s, u"medium"_s}) {
            QVERIFY2(!itemNamed(page, u"filter-axis-"_s + axis),
                     qPrintable(u"the row already draws "_s + axis));
            QVERIFY2(itemNamed(page, u"filter-chips"_s), "no row of pills");
        }

        // A short axis is open; a long one is folded and carries a field of its own, so that
        // fourteen authors are something to search rather than something to hunt through.
        QQuickItem *genreBody = itemNamed(page, u"filter-axis-body-genre"_s);
        QQuickItem *authorBody = itemNamed(page, u"filter-axis-body-author"_s);
        QVERIFY(genreBody);
        QVERIFY(authorBody);
        QVERIFY(genreBody->isVisible());
        QVERIFY(!authorBody->isVisible());
        QVERIFY(itemNamed(page, u"filter-axis-search-author"_s));
        QQuickItem *genreField = itemNamed(page, u"filter-axis-search-genre"_s);
        QVERIFY(genreField);
        QVERIFY(!genreField->isVisible());

        // A language is a word, not a tag: the panel says « Français », not « fr ».
        auto *french = itemNamed(page, u"filter-value-text-fr"_s);
        QVERIFY(french);
        QCOMPARE(french->property("text").toString(), u"Français 9"_s);

        auto *shelf = engine.singletonInstance<Shelf *>(qmlTypeId("Leaf", 1, 0, "Shelf"));
        QVERIFY(shelf);
        pretend.heard.clear();
        QVERIFY(QMetaObject::invokeMethod(panel, "toggle", Q_ARG(QVariant, u"genre"_s),
                                          Q_ARG(QVariant, u"Horreur"_s)));

        // It goes out under the contract's own name, once the hand has stopped.
        QTRY_VERIFY(pretend.heard.contains("genre=Horreur"));
        QCOMPARE(shelf->narrowing().value(u"genre"_s).toStringList(),
                 QStringList({u"Horreur"_s}));

        // And it is on screen: the row says what you are looking at, whatever axis said it.
        auto *chip = itemNamed(page, u"chip-Horreur"_s);
        QVERIFY2(chip, "a filter in force with nothing on screen saying so");
        QTRY_VERIFY(chip->isVisible());
        QCOMPARE(chip->property("lit").toBool(), true);

        // The button counts every axis, not only the two the row draws — it sits at the
        // head of that row and has to say, while the panel is shut, that something is in
        // force on an axis no pill shows.
        auto *pills = itemNamed(page, u"filter-pills"_s);
        QVERIFY(pills);
        QCOMPARE(pills->property("litCount").toInt(), 1);

        // « Tout effacer » appears only once there is something to clear, and empties every
        // axis at once — the only way out of a selection spread across eight of them.
        auto *clear = itemNamed(page, u"clear-every-filter"_s);
        QVERIFY(clear);
        QVERIFY(clear->isVisible());
        shelf->filterBy({});
        QTRY_COMPARE(pills->property("litCount").toInt(), 0);
        QTRY_VERIFY(!clear->isVisible());
    }

    /// Three hundred authors is a list, not a wall three hundred items tall. The axis is as
    /// tall as five of them and scrolls; without that, one axis of a library this size made
    /// the panel nine thousand pixels long and built every row to draw the five on screen.
    void an_axis_of_three_hundred_values_stays_the_height_of_five()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        const QByteArray pageReply = aReply(
            200, QByteArrayLiteral("application/json"),
            aPage({aSeries(u"ac"_s, u"Assassination Classroom"_s, 21, false)}, 1));
        const QByteArray emptyNext =
            aReply(200, QByteArrayLiteral("application/json"), QByteArrayLiteral("[]"));
        const QByteArray filtersReply =
            aReply(200, QByteArrayLiteral("application/json"), manyFilters(300));
        const QByteArray coverReply =
            aReply(200, QByteArrayLiteral("image/png"), aCover());
        pretend.answerFor = [pageReply, emptyNext, filtersReply,
                             coverReply](const QByteArray &request) {
            if (request.startsWith("GET /next"))
                return emptyNext;
            if (request.startsWith("GET /filters"))
                return filtersReply;
            return request.startsWith("GET /series?") ? pageReply : coverReply;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->requestActivate();
        QTRY_VERIFY(window->isActive());

        QQuickItem *button = nullptr;
        QTRY_VERIFY((button = reachableFilterButton(window)));
        auto *panel = button->findChild<QObject *>(u"filter-panel"_s);
        QVERIFY(panel);
        QVERIFY(QMetaObject::invokeMethod(panel, "open"));
        QTRY_VERIFY(panel->property("opened").toBool());

        QQuickItem *page = window->contentItem();
        QQuickItem *axis = nullptr;
        QTRY_VERIFY((axis = itemNamed(page, u"filter-axis-author"_s)));
        // Folded, because it is long — and it says how many are behind it without opening.
        QCOMPARE(textNamed(page, u"filter-axis-tally-author"_s),
                 u"300"_s);
        QQuickItem *authorBody = itemNamed(page, u"filter-axis-body-author"_s);
        QVERIFY(authorBody);
        QVERIFY(!authorBody->isVisible());

        axis->setProperty("open", true);
        QQuickItem *rows = itemNamed(page, u"filter-axis-rows-author"_s);
        QVERIFY(rows);
        QTRY_COMPARE(rows->property("count").toInt(), 300);

        // Seven rows tall whatever the three hundred, and only a handful of them built.
        QCOMPARE(rows->height(), 5.0 * 30.0);
        QVERIFY2(itemsNamed(rows, u"filter-value-Auteur 0001"_s).size() == 1,
                 "the first row was not built");
        QVERIFY2(itemsNamed(rows, u"filter-value-Auteur 0300"_s).isEmpty(),
                 "a row three hundred places down was built to draw five");

        // And its own field narrows it, which is the way through a list this long.
        auto *field = itemNamed(page, u"filter-axis-search-author"_s);
        QVERIFY(field);
        QVERIFY(field->isVisible());
        axis->setProperty("looking", u"0042"_s);
        QTRY_COMPARE(rows->property("count").toInt(), 1);
        QCOMPARE(rows->height(), 30.0);
    }

    /// Clicking the button that opened a popup closes it. The press counted as outside, so
    /// the popup shut and the release opened it again — a button that looked dead.
    ///
    /// Both buttons, because the defect was found on one and fixed on one: the toggle lives
    /// in `BarButton` now, so a third button opening a third popup gets it without knowing.
    void a_button_that_opened_a_popup_closes_it()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        const QByteArray pageReply = aReply(
            200, QByteArrayLiteral("application/json"),
            aPage({aSeries(u"ac"_s, u"Assassination Classroom"_s, 21, false)}, 1));
        const QByteArray emptyNext =
            aReply(200, QByteArrayLiteral("application/json"), QByteArrayLiteral("[]"));
        const QByteArray filtersReply =
            aReply(200, QByteArrayLiteral("application/json"), someFilters());
        const QByteArray coverReply =
            aReply(200, QByteArrayLiteral("image/png"), aCover());
        pretend.answerFor = [pageReply, emptyNext, filtersReply,
                             coverReply](const QByteArray &request) {
            if (request.startsWith("GET /next"))
                return emptyNext;
            if (request.startsWith("GET /filters"))
                return filtersReply;
            return request.startsWith("GET /series?") ? pageReply : coverReply;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->requestActivate();
        QTRY_VERIFY(window->isActive());

        // Press and release apart, with the window drawing in between: a click delivered as
        // one event never let the popup see the press, which is exactly the half of the
        // sequence that used to close it behind the button's back.
        const auto click = [window](QQuickItem *what) {
            const QPoint centre =
                what->mapToItem(window->contentItem(),
                                QPointF(what->width() / 2.0, what->height() / 2.0))
                    .toPoint();
            QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, centre);
            QTest::qWait(60);
            QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, centre);
            QTest::qWait(60);
        };

        // The sort menu answers to the same rule, through the same button.
        auto *sortButton = window->findChild<QQuickItem *>(u"sort-button"_s);
        auto *sortMenu = window->findChild<QObject *>(u"sort-menu"_s);
        QVERIFY(sortButton);
        QVERIFY(sortMenu);
        click(sortButton);
        QTRY_VERIFY(sortMenu->property("opened").toBool());
        click(sortButton);
        QTRY_VERIFY2(!sortMenu->property("opened").toBool(),
                     "the sort menu reopened behind its own button");
        click(sortButton);
        QTRY_VERIFY(sortMenu->property("opened").toBool());
        QVERIFY(QMetaObject::invokeMethod(sortMenu, "close"));
        QTRY_VERIFY(!sortMenu->property("opened").toBool());

        QQuickItem *button = nullptr;
        QTRY_VERIFY((button = reachableFilterButton(window)));
        auto *panel = button->findChild<QObject *>(u"filter-panel"_s);
        QVERIFY(panel);
        const auto clickTheButton = [click, button] { click(button); };

        clickTheButton();
        QTRY_VERIFY(panel->property("opened").toBool());

        clickTheButton();
        QTRY_VERIFY2(!panel->property("opened").toBool(),
                     "the press closed it and the release opened it again");

        // And it still opens on the next one, rather than having learned to refuse.
        clickTheButton();
        QTRY_VERIFY(panel->property("opened").toBool());

        // The half of the gesture a synthetic click cannot deliver: on a real desktop the
        // press outside shuts the popup, and the release that follows lands on a button
        // that now finds it closed. Played out directly, because no `mouseClick` reproduces
        // it — press and release reach Qt as one event.
        QTRY_VERIFY(panel->property("opened").toBool());
        button->setProperty("popupWasOpen", true);
        QVERIFY(QMetaObject::invokeMethod(panel, "close"));
        QTRY_VERIFY(!panel->property("opened").toBool());
        QVERIFY(QMetaObject::invokeMethod(button, "press",
                                          Q_ARG(QVariant, QVariant(true))));
        QTest::qWait(80);
        QVERIFY2(!panel->property("opened").toBool(),
                 "the release reopened what the press had just closed");

        // And the next gesture, which starts with the panel closed, opens it.
        clickTheButton();
        QTRY_VERIFY(panel->property("opened").toBool());
    }

    /// The settings button finally goes somewhere. It was left unwired because the Loader
    /// was pinned to the shelf: opening a destination nothing draws changed the stack,
    /// showed the same page, and spent the next Escape popping what nobody had seen.
    void the_settings_button_opens_a_screen_and_escape_comes_back()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        const QByteArray pageReply = aReply(
            200, QByteArrayLiteral("application/json"),
            aPage({aSeries(u"ac"_s, u"Assassination Classroom"_s, 21, false)}, 1));
        const QByteArray emptyNext =
            aReply(200, QByteArrayLiteral("application/json"), QByteArrayLiteral("[]"));
        const QByteArray healthReply =
            aReply(200, QByteArrayLiteral("application/json"),
                   QJsonDocument(QJsonObject{{u"status"_s, u"ok"_s},
                                             {u"api"_s, 1},
                                             {u"format"_s, 1},
                                             {u"library"_s, 6},
                                             {u"localDrop"_s, true}})
                       .toJson(QJsonDocument::Compact));
        const QByteArray scanReply =
            aReply(200, QByteArrayLiteral("application/json"),
                   QJsonDocument(QJsonObject{
                       {u"state"_s, u"DONE"_s},
                       {u"finishedAt"_s, 1788463370000LL},
                       {u"report"_s,
                        QJsonObject{{u"counts"_s,
                                     QJsonObject{{u"universes"_s, 1},
                                                 {u"works"_s, 5},
                                                 {u"editions"_s, 6},
                                                 {u"entries"_s, 59},
                                                 {u"chapters"_s, 546},
                                                 {u"pages"_s, 0},
                                                 {u"reanalysed"_s, 0}}}}}})
                       .toJson(QJsonDocument::Compact));
        const QByteArray coverReply =
            aReply(200, QByteArrayLiteral("image/png"), aCover());
        // Answered here so the row of pills is drawn: the filter button lives at its head,
        // and this slot has to leave the shelf with the panel open.
        const QByteArray filtersReply =
            aReply(200, QByteArrayLiteral("application/json"), someFilters());
        pretend.answerFor = [pageReply, emptyNext, healthReply, scanReply, filtersReply,
                             coverReply](const QByteArray &request) {
            if (request.startsWith("GET /next"))
                return emptyNext;
            if (request.startsWith("GET /health"))
                return healthReply;
            if (request.startsWith("GET /scan"))
                return scanReply;
            if (request.startsWith("GET /filters"))
                return filtersReply;
            return request.startsWith("GET /series?") ? pageReply : coverReply;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->requestActivate();
        QTRY_VERIFY(window->isActive());
        QQuickItem *grid = window->findChild<QQuickItem *>(u"shelf-grid"_s);
        QVERIFY(grid);

        // Opened here to be found shut on the other side of the navigation: a popup lives
        // in the overlay and outlives the button it hangs from, so hiding the bar's row
        // left the filters over the settings, narrowing a shelf nobody could see.
        // By its own button, and there are two of them on this page: the shelf's row of
        // pills and the one the search workspace draws. Only the shelf's is on screen.
        QQuickItem *filterButton = nullptr;
        QTRY_VERIFY((filterButton = reachableFilterButton(window)));
        auto *filters = filterButton->findChild<QObject *>(u"filter-panel"_s);
        QVERIFY(filters);
        QVERIFY(QMetaObject::invokeMethod(filters, "open"));
        QTRY_VERIFY(filters->property("opened").toBool());

        auto *button = window->findChild<QQuickItem *>(u"settings-button"_s);
        QVERIFY(button);
        QVERIFY(QMetaObject::invokeMethod(button, "triggered"));
        QTRY_VERIFY(!filters->property("opened").toBool());
        // And waited out, not merely flagged shut. Without this the Escape below reached
        // the window while the panel was still winding down and was swallowed on roughly
        // one run in three — the page stayed on the settings, with an empty search field
        // and no shortcut fired. What exactly ate the key was not established; that it
        // stops once the popup is finished is.
        QTRY_VERIFY(!filters->property("visible").toBool());

        QQuickItem *view = nullptr;
        QTRY_VERIFY((view = window->findChild<QQuickItem *>(u"settings-view"_s)));
        // The shelf goes out of sight and not out of existence. Destroyed and rebuilt it
        // lost its scroll, decoded every cover again and flashed its skeleton on the way
        // back, over a page the reader had already read.
        QTRY_VERIFY(!grid->isVisible());
        QCOMPARE(window->findChild<QQuickItem *>(u"shelf-grid"_s), grid);

        // A state and not a table of facts: what a reader needs of a server is whether
        // their books are there. The address and the version are nowhere on this screen.
        QQuickItem *page = window->contentItem();
        QTRY_COMPARE(textNamed(page, u"connection-label"_s),
                     u"Bibliothèque connectée"_s);
        QCOMPARE(textNamed(page, u"connection-detail"_s),
                 u"6 séries"_s);
        QVERIFY2(!itemNamed(page, u"settings-versions-label"_s),
                 "the server's version is on a reader's settings screen");

        // The one preference Leaf has, and the three answers to it.
        QVERIFY(itemNamed(page, u"appearance-contrast"_s));
        QVERIFY(itemNamed(page, u"appearance-light_mode"_s));
        QVERIFY(itemNamed(page, u"appearance-dark_mode"_s));

        // And a way back that names where it goes.
        auto *back = window->findChild<QQuickItem *>(u"back-button"_s);
        QVERIFY(back);
        QTRY_VERIFY(back->isVisible());
        QVERIFY2(back->property("label").toString().contains(u"étagère"_s),
                 qPrintable(back->property("label").toString()));

        // The scan lives behind the second pill, and what it counted is said in French
        // here rather than arriving as a paragraph the vocabulary file could not reach.
        auto *libraryTab = itemNamed(page, u"settings-tab-library"_s);
        QVERIFY(libraryTab);
        QVERIFY(QMetaObject::invokeMethod(itemNamed(page, u"settings-tabs"_s), "selected",
                                          Q_ARG(int, 1)));
        QTRY_COMPARE(textNamed(page, u"settings-scan-counts-label"_s),
                     u"1 univers, 6 séries, 59 tomes, 546 chapitres"_s);

        // The button goes back to the page underneath — the same one, item for item, which
        // is the whole point of leaving it there.
        QVERIFY(QMetaObject::invokeMethod(back, "triggered"));
        QTRY_VERIFY(grid->isVisible());
        QCOMPARE(window->findChild<QQuickItem *>(u"shelf-grid"_s), grid);
        // And the way back went with the page it belonged to. Asserted by looking for it
        // again rather than by asking the pointer whether it is still shown: the button
        // moved out of the bar and into the settings' own tabs, so the Loader deletes it
        // on the way out — `back->isVisible()` here read freed memory, and did it for
        // however long the allocator left the bytes recognisable.
        QTRY_VERIFY(!window->findChild<QQuickItem *>(u"back-button"_s));

        // And Escape does the same, rather than being spent on a page nobody saw.
        QVERIFY(QMetaObject::invokeMethod(button, "triggered"));
        QTRY_VERIFY(window->findChild<QQuickItem *>(u"settings-view"_s));
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(grid->isVisible());
        QTRY_VERIFY(!window->findChild<QQuickItem *>(u"settings-view"_s));
    }

    /// A setup that was never finished says what is missing and where it goes — the one
    /// moment the technical detail helps rather than clutters.
    ///
    /// It is also the only test that draws those sentences, and a paragraph is where a
    /// binding loop hides: a wrapped text with nothing in it is never laid out, so a loop
    /// in it costs nothing until somebody's key is missing.
    void a_setup_that_is_not_finished_says_what_is_missing()
    {
        qputenv("LEAF_ADDRESS", QByteArrayLiteral("http://127.0.0.1:1"));
        qunsetenv("LEAF_KEY");

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->requestActivate();
        QTRY_VERIFY(window->isActive());

        auto *button = window->findChild<QQuickItem *>(u"settings-button"_s);
        QVERIFY(button);
        QVERIFY(QMetaObject::invokeMethod(button, "triggered"));
        QTRY_VERIFY(window->findChild<QQuickItem *>(u"settings-view"_s));

        QQuickItem *page = window->contentItem();
        QTRY_COMPARE(textNamed(page, u"connection-label"_s),
                     u"Pas de connexion"_s);

        // The sentence names the file it wants and the variable that would do instead.
        // `Settings` writes it: it is the one that knows which of the two is absent.
        auto *detail = itemNamed(page, u"connection-detail"_s);
        QVERIFY(detail);
        QTRY_VERIFY(!detail->property("text").toString().isEmpty());
        QVERIFY2(detail->property("text").toString().contains(u"LEAF_KEY"_s),
                 qPrintable(detail->property("text").toString()));
        QVERIFY(detail->isVisible());
        QVERIFY(detail->height() > 0);
    }

    /// Nothing started is not an empty card: the header has to give its height back, or the
    /// grid keeps a hole where the band would have been.
    void nothing_started_leaves_no_band_and_no_gap()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        const QByteArray pageReply = aReply(
            200, QByteArrayLiteral("application/json"),
            aPage({aSeries(u"ac"_s, u"Assassination Classroom"_s, 21, false)}));
        const QByteArray emptyNext =
            aReply(200, QByteArrayLiteral("application/json"), QByteArrayLiteral("[]"));
        const QByteArray coverReply =
            aReply(200, QByteArrayLiteral("image/png"), aCover());
        pretend.answerFor = [pageReply, emptyNext, coverReply](const QByteArray &request) {
            if (request.startsWith("GET /next"))
                return emptyNext;
            return request.startsWith("GET /series?") ? pageReply : coverReply;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));

        auto *band = window->findChild<QQuickItem *>(u"resume-band"_s);
        QVERIFY(band);
        QTRY_COMPARE(band->height(), 0.0);
        QVERIFY(!band->isVisible());

        auto *grid = window->findChild<QQuickItem *>(u"shelf-grid"_s);
        auto *appBar = window->findChild<QQuickItem *>(u"app-bar"_s);
        QVERIFY(grid);
        QVERIFY(appBar);
        QQuickItem *tile = nullptr;
        QTRY_VERIFY((tile = itemNamed(grid, u"tile-ac"_s)));
        QCOMPARE(tile->mapToItem(nullptr, QPointF(0, 0)).y(), qreal(appBar->height()));
    }

    /// Half a screen: the long line no longer fits beside the button, so the wording shortens
    /// and the button keeps its arrow and drops its word. The break is Widths', not this file's.
    void half_a_screen_shortens_the_line_and_keeps_only_the_arrow()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        const QByteArray pageReply = aReply(
            200, QByteArrayLiteral("application/json"),
            aPage({aSeries(u"ac"_s, u"Assassination Classroom"_s, 21, true)}));
        const QByteArray nextReply =
            aReply(200, QByteArrayLiteral("application/json"), anOffer());
        const QByteArray coverReply =
            aReply(200, QByteArrayLiteral("image/png"), aCover());
        pretend.answerFor = [pageReply, nextReply, coverReply](const QByteArray &request) {
            if (request.startsWith("GET /next"))
                return nextReply;
            return request.startsWith("GET /series?") ? pageReply : coverReply;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));

        auto *band = window->findChild<QQuickItem *>(u"resume-band"_s);
        QVERIFY(band);
        QTRY_VERIFY(band->isVisible());
        auto *where = itemNamed(band, u"resume-where"_s);
        auto *label = itemNamed(band, u"resume-action-text"_s);
        auto *action = itemNamed(band, u"resume-action"_s);
        QVERIFY(where);
        QVERIFY(label);
        QVERIFY(action);
        QTRY_COMPARE(where->property("text").toString(),
                     u"Tome 12 · Page 47/190 · Chapitre 98"_s);

        window->setWidth(800);
        QTRY_COMPARE(where->property("text").toString(), u"T12 · Page 47/190"_s);
        QVERIFY(!label->isVisible());
        QCOMPARE(action->width(), action->height());
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
        QTRY_VERIFY(grid->property("contentHeight").toReal() > grid->height());
        grid->setProperty("contentY",
                          grid->property("contentHeight").toReal() - grid->height());

        QTRY_VERIFY(grid->property("contentY").toReal() > 0);
        QTRY_COMPARE(shelf->count(), 101);
        QVERIFY(pretend.heard.contains("page=1"));
    }

    /// The shape of the answer, in the grid's own cells, rather than a spinner in the middle
    /// of an empty page: the first tile lands where its placeholder already was.
    void the_first_load_shows_the_shape_of_what_is_coming()
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
        // Exposed, unlike the spinner this replaced: a placeholder is a measurement, and an
        // unexposed window measures nothing, so the grid would report one cell of no size.
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *waiting = window->findChild<QQuickItem *>(u"shelf-skeleton"_s);
        QVERIFY(waiting);
        QTRY_VERIFY(waiting->isVisible());

        // Placeholders the size of the covers they stand in for, and more than one of them:
        // a single centred shape is a spinner wearing a rectangle.
        auto *grid = window->findChild<QQuickItem *>(u"shelf-grid"_s);
        QVERIFY(grid);
        // Re-read each time: the repeaters fill after the item is shown, so a list taken
        // once is a list taken too early.
        const auto placeholders = [waiting] {
            return itemsNamed(waiting, u"skeleton-cover"_s);
        };
        QTRY_VERIFY2(placeholders().size() > 1,
                     "one placeholder is not the shape of a shelf");
        QCOMPARE(placeholders().constFirst()->width(),
                 grid->property("coverWidth").toReal());
        QCOMPARE(placeholders().constFirst()->height(),
                 grid->property("coverHeight").toReal());
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
        auto *grid = window->findChild<QQuickItem *>(u"shelf-grid"_s);
        QVERIFY(trouble);
        QVERIFY(said);
        QVERIFY(grid);
        QTRY_VERIFY(trouble->isVisible());
        QVERIFY(!grid->property("activeFocusOnTab").toBool());
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
    void changing_appearance_updates_the_running_window()
    {
        const RestoresThePalette restoreOnExit;
        QPalette night;
        night.setColor(QPalette::Window, QColor(u"#101010"_s));
        QGuiApplication::setPalette(night);
        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp);
        auto *preferences = engine.singletonInstance<Preferences *>(
            qmlTypeId("Leaf", 1, 0, "Preferences"));
        auto *theme = engine.singletonInstance<Theme *>(qmlTypeId("Leaf", 1, 0, "Theme"));
        QVERIFY(preferences);
        QVERIFY(theme);
        preferences->chooseAppearance(Preferences::Appearance::Dark);
        QVERIFY(theme->dark());
        preferences->chooseAppearance(Preferences::Appearance::Light);
        QVERIFY(!theme->dark());
        preferences->chooseAppearance(Preferences::Appearance::System);
        QVERIFY(theme->dark());
    }

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
