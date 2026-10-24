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
#include "CardCaptions.h"
#include "Commands.h"
#include "ImportCaptions.h"
#include "Imports.h"
#include "Preferences.h"
#include "Profiles.h"
#include "Reader.h"
#include "Toasts.h"
#include "Entries.h"
#include "Filters.h"
#include "Navigation.h"
#include "Series.h"
#include "Settings.h"
#include "Pretend.h"

#include <QDBusConnection>

#include <functional>
#include <memory>
#include "Shelf.h"
#include "Theme.h"
#include "Widths.h"
#include "Words.h"

#include <QAccessible>
#include <QBuffer>
#include <QClipboard>
#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPalette>
#include <QPointer>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTcpServer>
#include <QMutex>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QWheelEvent>
#include <QWindow>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <vector>

using Qt::Literals::StringLiterals::operator""_s;

namespace {
/// One notch of a wheel, sent to the window. `QTest::wheelEvent` does not exist in the Qt of
/// Ubuntu 24.04, where the client is built in CI and where its absence broke this file; the
/// event is built here and handed to the window, which handles it before this returns.
void wheel(QWindow *window, const QPoint &at, const QPoint &angle, const QPoint &pixels = {},
           Qt::KeyboardModifiers modifiers = Qt::NoModifier,
           Qt::ScrollPhase phase = Qt::NoScrollPhase)
{
    QWheelEvent event(QPointF(at), QPointF(window->mapToGlobal(at)), pixels, angle,
                      Qt::NoButton, modifiers, phase, false);
    QGuiApplication::sendEvent(window, &event);
}

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
             // Label and title, the two halves the band joins as the reader does. The title
             // is markup on purpose: it is the files' words, and must be shown as written.
             {u"chapter"_s,
              QJsonObject{{u"label"_s, u"Chap.\u00A098"_s}, {u"title"_s, u"<b>x</b> & y"_s}}},
         }},
    };
    return QJsonDocument(QJsonArray{row}).toJson(QJsonDocument::Compact);
}

/// `/filters`, with two values on each axis so that both are worth offering. The read counts
/// can be moved, for a test that marks something read and watches the row follow.
QByteArray someFilters(int unread = 12, int read = 4)
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
                    QJsonArray{counted(u"UNREAD"_s, unread), counted(u"READ"_s, read)}},
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

/// Whether Qt is complaining about the QML itself rather than about the world.
///
/// A binding loop is one of those. So is a `TypeError` — and that one is worse, because it
/// happens *when a gesture is made* and never at load: choosing an edition tore down the row
/// that was handling the tap, everything it looked up afterwards came back undefined, and
/// « Cannot call method 'close' of undefined » went into the terminal four times a click
/// while every one of these tests stayed green.
///
/// A missing cover or a refused connection are not that: this harness points the client at a
/// port that refuses on purpose, and the covers it asks for are never there.
bool complainsAboutTheQml(const QString &said)
{
    return said.contains(u"Binding loop"_s) || said.contains(u"TypeError"_s)
        || said.contains(u"ReferenceError"_s) || said.contains(u"is not a function"_s)
        || said.contains(u"Unable to assign"_s) || said.contains(u"Cannot assign"_s);
}

void watchForLoops(QtMsgType type, const QMessageLogContext &where, const QString &said)
{
    if (complainsAboutTheQml(said)) {
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

/// A bus that goes nowhere, and why every boot here is handed one.
///
/// `Boot::run` wires the real notifier, so a test that boots the window and raises anything
/// worth announcing lands it on the desktop of whoever ran `ctest`. A connection under a name
/// nobody registered is never connected, so the notifier is built, asked, and silent — which
/// is also what it does on a machine with no daemon, the case it exists to survive.
QDBusConnection nowhere()
{
    return QDBusConnection(u"leaf-tests-speak-to-nobody"_s);
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

QString textNamed(QQuickItem *root, const QString &name)
{
    QQuickItem *one = itemNamed(root, name);
    return one ? one->property("text").toString()
               : u"<aucun élément nommé "_s + name + u">"_s;
}

/// One profile as `GET /profiles` lists it.
QJsonObject aProfile(const QString &id, const QString &name, const QString &color,
                     const QString &kind, bool import = false, int maxAge = 0,
                     bool edit = false, bool erase = false)
{
    QJsonObject one{
        {u"id"_s, id},
        {u"name"_s, name},
        {u"color"_s, color},
        {u"kind"_s, kind},
        {u"rights"_s,
         QJsonObject{{u"import"_s, import}, {u"edit"_s, edit}, {u"delete"_s, erase}}},
    };
    if (maxAge > 0)
        one.insert(u"maxAge"_s, maxAge);
    return one;
}

/// Who is seated on this device, and among whom.
QByteArray aSeating(const QJsonArray &profiles, const QString &active, bool kept,
                    const QString &code = u"none"_s)
{
    return aReply(200, QByteArrayLiteral("application/json"),
                  QJsonDocument(QJsonObject{
                                    {u"profiles"_s, profiles},
                                    {u"active"_s, active},
                                    {u"kept"_s, kept},
                                    {u"choosing"_s, false},
                                    {u"held"_s, false},
                                    {u"code"_s, code},
                                })
                      .toJson(QJsonDocument::Compact));
}

/// The principal alone, free to import: what every test that reaches the import button now
/// has to be told, since the button waits for the server to say the profile may.
std::function<QByteArray(const QByteArray &)>
seatedAsPrincipal(std::function<QByteArray(const QByteArray &)> rest)
{
    const QByteArray seating = aSeating(
        {aProfile(u"p"_s, u"Principal"_s, u"emeraude"_s, u"principal"_s, true)}, u"p"_s,
        false);
    return [seating, rest](const QByteArray &request) {
        if (request.startsWith("GET /profiles "))
            return seating;
        return rest(request);
    };
}

/// The principal and a restricted profile, the restricted one reading and the code guarding
/// the way out: the seat every test of the code window starts from.
QByteArray aRestrictedSeat(const QString &active = u"l"_s)
{
    return aSeating({aProfile(u"p"_s, u"Principal"_s, u"emeraude"_s, u"principal"_s, true),
                     aProfile(u"l"_s, u"Enfant"_s, u"azur"_s, u"restricted"_s, false, 10)},
                    active, false, u"restricted"_s);
}

/// Typed as a keyboard would, one character at a time: `QTest::keyClicks` takes a widget,
/// and `keyClick` on a window takes a Latin-1 `char`, which an « é » is not.
void typeInto(QQuickWindow *window, const QString &text)
{
    for (const QChar one : text)
        QTest::sendKeyEvent(QTest::Click, window, Qt::Key_unknown, QString(one), Qt::NoModifier);
}

/// Rows and columns place their children at the next frame, not when one appears: a click
/// aimed before it lands where the button is about to stop being. Two frames, because the
/// threaded render loop may swap one it had already taken before the change.
bool laidOut(QQuickWindow *window)
{
    QSignalSpy drawn(window, &QQuickWindow::frameSwapped);
    for (int frame = 0; frame < 2; ++frame) {
        window->update();
        if (!drawn.wait())
            return false;
    }
    return true;
}

/// A reader, kept on this device, and a principal whose code also guards the way back to it
/// and the adding of a profile.
QByteArray aReaderSeatBehindTheCode()
{
    return aSeating({aProfile(u"p"_s, u"Principal"_s, u"emeraude"_s, u"principal"_s, true),
                     aProfile(u"r"_s, u"Lecteur"_s, u"corail"_s, u"reader"_s)},
                    u"r"_s, true, u"everything"_s);
}

/// From the bar to the code window: the square, « Qui lit ? », then a tile — the
/// principal's unless told otherwise.
/// Returns the window's body, shown — or null, for the caller to refuse.
void clickOn(QQuickWindow *window, QQuickItem *item);
QQuickItem *askedForTheCode(QQuickWindow *window, const QString &tile = u"who-reads-square-p"_s)
{
    auto *square = window->findChild<QQuickItem *>(u"profile-square"_s);
    auto *whoReads = window->findChild<QQuickItem *>(u"who-reads"_s);
    auto *dialog = window->findChild<QQuickItem *>(u"code-dialog-body"_s);
    if (!square || !whoReads || !dialog)
        return nullptr;
    if (!QTest::qWaitFor([&] {
            return square->isVisible()
                   && square->mapToScene(QPointF(0, 0)).x() > window->width() / 2;
        }))
        return nullptr;
    clickOn(window, square);
    if (!QTest::qWaitFor([&] { return whoReads->isVisible(); }))
        return nullptr;
    QQuickItem *chosen = itemNamed(whoReads, tile);
    if (!chosen)
        return nullptr;
    clickOn(window, chosen);
    if (!QTest::qWaitFor([&] { return dialog->isVisible(); }) || !laidOut(window))
        return nullptr;
    return dialog;
}

/// The JSON body of the first request that starts with `line` — `POST /profiles ` — as the
/// server would read it: by its Content-Length, since `heard` holds every request after it too.
QJsonObject bodySent(const QByteArray &heard, const QByteArray &line)
{
    const qsizetype start = heard.indexOf(line);
    const qsizetype end = start < 0 ? -1 : heard.indexOf("\r\n\r\n", start);
    if (end < 0)
        return {};
    const QByteArray head = heard.mid(start, end - start).toLower();
    const qsizetype length = head.indexOf("content-length: ");
    if (length < 0)
        return {};
    const int size = head.mid(length + 16).split('\r').constFirst().toInt();
    return QJsonDocument::fromJson(heard.mid(end + 4, size)).object();
}

/// The profile window, opened from « Qui lit ? »'s Add tile — or null, for the caller to
/// refuse. The principal reaches it directly; a reader behind the code goes through
/// `askedForTheCode` first.
QQuickItem *profileDialogFromAdd(QQuickWindow *window)
{
    auto *square = window->findChild<QQuickItem *>(u"profile-square"_s);
    auto *whoReads = window->findChild<QQuickItem *>(u"who-reads"_s);
    auto *dialog = window->findChild<QQuickItem *>(u"profile-dialog-body"_s);
    if (!square || !whoReads || !dialog)
        return nullptr;
    if (!QTest::qWaitFor([&] {
            return square->isVisible()
                   && square->mapToScene(QPointF(0, 0)).x() > window->width() / 2;
        }))
        return nullptr;
    clickOn(window, square);
    if (!QTest::qWaitFor([&] { return whoReads->isVisible(); }))
        return nullptr;
    QQuickItem *add = itemNamed(whoReads, u"who-reads-add"_s);
    if (!add)
        return nullptr;
    clickOn(window, add);
    if (!QTest::qWaitFor([&] { return dialog->isVisible(); }) || !laidOut(window))
        return nullptr;
    return dialog;
}

/// A profile as `Profiles.list` hands it to a screen, for the window hooks the settings call.
QVariantMap aListedProfile(const QString &id, const QString &name, const QString &color,
                           const QString &kind, bool import = false, int maxAge = 0)
{
    return {{u"id"_s, id}, {u"name"_s, name}, {u"color"_s, color}, {u"kind"_s, kind},
            {u"initial"_s, name.left(1)}, {u"maxAge"_s, maxAge},
            {u"rights"_s, QVariantMap{{u"import"_s, import}, {u"edit"_s, false},
                                      {u"erase"_s, false}}}};
}

/// The profile window, opened the way Réglages › Profils opens it — or null, for the caller
/// to refuse. Waits for the seat first: the window reads who the principal is.
QQuickItem *profileDialogFor(QQuickWindow *window, const QString &mode, const QVariantMap &profile)
{
    auto *square = window->findChild<QQuickItem *>(u"profile-square"_s);
    auto *dialog = window->findChild<QQuickItem *>(u"profile-dialog-body"_s);
    if (!square || !dialog || !QTest::qWaitFor([&] { return square->isVisible(); }))
        return nullptr;
    if (!QMetaObject::invokeMethod(window, "openProfileDialog", Q_ARG(QVariant, mode),
                                   Q_ARG(QVariant, profile), Q_ARG(QVariant, QString())))
        return nullptr;
    if (!QTest::qWaitFor([&] { return dialog->isVisible(); }) || !laidOut(window))
        return nullptr;
    return dialog;
}

/// The window `Boot::run` loaded, shown and active — or null, for the caller to refuse.
QQuickWindow *shown(QQmlApplicationEngine &engine)
{
    Boot::run(engine, *qGuiApp, nowhere());
    if (engine.rootObjects().size() != 1)
        return nullptr;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    if (!window || !QTest::qWaitForWindowExposed(window))
        return nullptr;
    window->requestActivate();
    return QTest::qWaitForWindowActive(window) ? window : nullptr;
}

/// The settings screen, opened on the tab `section` — or null, for the caller to refuse. Waits
/// for the seat first: which tabs there are depends on who reads.
QQuickItem *settingsOn(QQmlApplicationEngine &engine, QQuickWindow *window,
                       const QString &section)
{
    auto *square = window->findChild<QQuickItem *>(u"profile-square"_s);
    if (!square || !QTest::qWaitFor([&] { return square->isVisible(); }))
        return nullptr;
    auto *navigation =
        engine.singletonInstance<Navigation *>(qmlTypeId("Leaf", 1, 0, "Navigation"));
    if (!navigation
        || !navigation->open(Navigation::Destination::Settings, {{u"section"_s, section}}))
        return nullptr;
    QQuickItem *view = nullptr;
    if (!QTest::qWaitFor([&] {
            return (view = window->findChild<QQuickItem *>(u"settings-view"_s)) != nullptr;
        }))
        return nullptr;
    return laidOut(window) ? view : nullptr;
}

/// A click in the middle of an item, the way a hand makes one.
void clickOn(QQuickWindow *window, QQuickItem *item)
{
    const QPointF middle = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, middle.toPoint());
}

/// Whether the item of that name is there and shown — false, rather than a crash, when it
/// is not there at all.
bool shownNamed(QQuickItem *root, const QString &name)
{
    QQuickItem *one = itemNamed(root, name);
    return one && one->isVisible();
}

/// Whether the item of that name is there and hidden: an item that is missing is not hidden,
/// it is a test looking at the wrong name.
bool hiddenNamed(QQuickItem *root, const QString &name)
{
    QQuickItem *one = itemNamed(root, name);
    return one && !one->isVisible();
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

/// The animation that glides the shelf to where a key sent it, or null.
///
/// A key lands on a cover by animating `contentY` over 180 ms (`glide` in ShelfView.qml),
/// and the pills row follows `contentY`, so for those 180 ms every chip is moving.
/// `currentIndex` already holds its new value when the glide begins, so waiting on it waits
/// for nothing: on an idle machine 180 ms pass before the test aims, and under load (40 busy
/// loops on 32 cores) they do not — the click was computed from a chip's position mid-glide
/// and released where the chip no longer was, and `shelf->media()` stayed empty. It is found
/// by its name, not by its class: a first version took any NumberAnimation under the grid,
/// so a renamed glide, or a delegate's own animation, would have made the wait pass or stall
/// for the wrong reason. The caller fails when it is missing rather than call it stopped.
QObject *shelfGlide(QQuickItem *grid)
{
    return grid->findChild<QObject *>(u"shelf-glide"_s);
}

/// One series of one volume, as the reader asks for it: the volume, its 190 pages, no
/// chapter, a position at page 46, and every page a small image. `direction` is what the
/// series declares. The shelf, the band and the series' own list get what they need to
/// draw and nothing more. With `recorded`, a write moves the position the server answers
/// with afterwards — when it is answered, not when it arrives, so a test holding the write
/// back sees exactly what a slow server would serve meanwhile. With `chosen`, the reader's
/// own direction is kept there (empty is « Par défaut ») and served back on the series;
/// `refuseChoice` makes the server answer that write with a failure instead.
///
/// `extras` is what the volume carries beyond its number and its pages — fields, chapters —
/// and whether it was ever opened; the panel beside the pages draws all of it.
struct BookExtras {
    QJsonObject volume;
    QJsonArray chapters;
    bool unopened = false;
    /// The series holds a volume 13 after this one, which is what the end card offers.
    bool followed = false;
    /// What follows is a chapter, number 108, and not a volume.
    bool followedByChapter = false;
    /// The list of the series' entries is refused.
    bool entriesFail = false;
    /// The series holds a volume 11 of thirty pages before this one, never opened.
    bool preceded = false;
    /// The series' name, when not « Assassination Classroom »: a long one, to see it elided.
    QString seriesName;
};

QByteArray aBookAnswers(const QByteArray &request, const QString &direction,
                        int *recorded = nullptr, const QString &medium = u"manga"_s,
                        QString *chosen = nullptr, const bool *refuseChoice = nullptr,
                        const BookExtras *extras = nullptr)
{
    int fallback = 46;
    int &at = recorded ? *recorded : fallback;
    const QByteArray line = request.left(request.indexOf("\r\n"));
    const QString path = QString::fromUtf8(line.split(' ').value(1).split('?').first());
    const auto json = [](const QJsonValue &value) {
        return aReply(200, QByteArrayLiteral("application/json"),
                      value.isArray()
                          ? QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact)
                          : QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    };
    QJsonObject volume{{u"id"_s, u"e12"_s}, {u"type"_s, u"VOLUME"_s},
                       {u"number"_s, 12}, {u"title"_s, u"Le temps des examens"_s},
                       {u"pageCount"_s, 190}, {u"chapterCount"_s, 0},
                       {u"file"_s, u"T12.cbz"_s}, {u"size"_s, 1}};
    if (extras) {
        for (auto it = extras->volume.begin(); it != extras->volume.end(); ++it)
            volume[it.key()] = it.value();
    }

    const QJsonObject before{{u"id"_s, u"e11"_s}, {u"type"_s, u"VOLUME"_s},
                             {u"number"_s, 11}, {u"title"_s, u"Le temps des adieux"_s},
                             {u"pageCount"_s, 30}, {u"chapterCount"_s, 0},
                             {u"file"_s, u"T11.cbz"_s}, {u"size"_s, 1}};
    if (extras && extras->preceded && path.startsWith(u"/entries/e11"_s)) {
        if (path == u"/entries/e11"_s)
            return json(before);
        if (path == u"/entries/e11/pages"_s) {
            QJsonArray pages;
            for (int i = 0; i < 30; ++i)
                pages.append(QJsonObject{{u"number"_s, i},
                                         {u"name"_s, u"%1.png"_s.arg(i, 3, 10, QChar(u'0'))},
                                         {u"mediaType"_s, u"image/png"_s},
                                         {u"width"_s, 1000}, {u"height"_s, 1500}});
            return json(pages);
        }
        if (path == u"/entries/e11/chapters"_s)
            return json(QJsonArray{});
        if (path == u"/entries/e11/progress"_s && !line.startsWith("PATCH "))
            return "HTTP/1.1 204 .\r\nContent-Length: 0\r\n\r\n";
        if (path.startsWith(u"/entries/e11/pages/"_s))
            return aReply(200, QByteArrayLiteral("image/png"), aCover());
    }

    if (line.startsWith("PATCH ")) {
        at = QJsonDocument::fromJson(request.mid(request.indexOf("\r\n\r\n") + 4))
                 .object()
                 .value(u"page"_s)
                 .toInt(at);
        return json(QJsonObject{{u"entryId"_s, u"e12"_s}, {u"page"_s, at},
                                {u"pageCount"_s, 190}});
    }
    const QString seriesName = extras && !extras->seriesName.isEmpty()
                                   ? extras->seriesName
                                   : u"Assassination Classroom"_s;
    const auto theSeries = [&]() {
        QJsonObject one{{u"id"_s, u"s1"_s}, {u"workId"_s, u"w1"_s},
                        {u"name"_s, seriesName},
                        {u"work"_s, u"Assassination Classroom"_s},
                        {u"medium"_s, medium}, {u"readingDirection"_s, direction},
                        {u"ownedVolumes"_s, 1}, {u"entryCount"_s, 1},
                        {u"chapterCount"_s, 0}, {u"arcCount"_s, 0}};
        if (chosen && !chosen->isEmpty())
            one[u"readingDirectionChosen"_s] = *chosen;
        return one;
    };
    if (line.startsWith("PUT ") && path == u"/series/s1/reading-direction"_s) {
        if (refuseChoice && *refuseChoice)
            return "HTTP/1.1 500 .\r\nContent-Length: 0\r\n\r\n";
        if (chosen)
            *chosen = QJsonDocument::fromJson(request.mid(request.indexOf("\r\n\r\n") + 4))
                          .object()
                          .value(u"readingDirection"_s)
                          .toString();
        return json(theSeries());
    }
    if (path == u"/series/s1"_s)
        return json(theSeries());
    if (path == u"/series/s1/entries"_s && extras && extras->entriesFail)
        return "HTTP/1.1 500 .\r\nContent-Length: 0\r\n\r\n";
    if (path == u"/series/s1/entries"_s) {
        QJsonArray all{volume};
        if (extras && extras->preceded)
            all.prepend(before);
        if (extras && extras->followedByChapter)
            all.append(QJsonObject{{u"id"_s, u"e13"_s}, {u"type"_s, u"CHAPTER"_s},
                                   {u"number"_s, 108}, {u"title"_s, u"Suite"_s},
                                   {u"pageCount"_s, 20}, {u"chapterCount"_s, 0},
                                   {u"file"_s, u"C108.cbz"_s}, {u"size"_s, 1}});
        if (extras && extras->followed)
            all.append(QJsonObject{{u"id"_s, u"e13"_s}, {u"type"_s, u"VOLUME"_s},
                                   {u"number"_s, 13}, {u"title"_s, u"Le temps des retrouvailles"_s},
                                   {u"pageCount"_s, 192}, {u"chapterCount"_s, 0},
                                   {u"file"_s, u"T13.cbz"_s}, {u"size"_s, 1}});
        return json(all);
    }
    if (path == u"/series/s1/progress"_s)
        return json(QJsonArray{QJsonObject{{u"entryId"_s, u"e12"_s}, {u"page"_s, at},
                                           {u"pageCount"_s, 190}, {u"finished"_s, false}}});
    if (path == u"/entries/e12"_s)
        return json(volume);
    if (path == u"/entries/e12/pages"_s) {
        QJsonArray pages;
        for (int i = 0; i < 190; ++i)
            pages.append(QJsonObject{{u"number"_s, i},
                                     {u"name"_s, u"%1.png"_s.arg(i, 3, 10, QChar(u'0'))},
                                     {u"mediaType"_s, u"image/png"_s},
                                     {u"width"_s, 1000},
                                     // A webtoon ends on a sliver: too short to ever cross
                                     // the middle of the view, which is the case where only
                                     // the bottom of the strip can say it was read.
                                     {u"height"_s, medium == u"webtoon"_s && i == 189 ? 100
                                                                                      : 1500}});
        return json(pages);
    }
    if (path == u"/entries/e12/chapters"_s)
        return json(extras ? extras->chapters : QJsonArray{});
    if (path == u"/entries/e12/progress"_s && extras && extras->unopened)
        return "HTTP/1.1 204 .\r\nContent-Length: 0\r\n\r\n";
    if (path == u"/entries/e12/progress"_s)
        return json(QJsonObject{{u"entryId"_s, u"e12"_s}, {u"page"_s, 46},
                                {u"pageCount"_s, 190}, {u"finished"_s, false}});
    if (path.startsWith(u"/entries/e12/pages/"_s))
        return aReply(200, QByteArrayLiteral("image/png"), aCover());
    if (path == u"/entries/e13/cover"_s && extras && extras->followed)
        return aReply(200, QByteArrayLiteral("image/png"), aCover());
    if (path.startsWith(u"/next"_s) || path.endsWith(u"/cover"_s))
        return "HTTP/1.1 404 .\r\nContent-Length: 0\r\n\r\n";
    return json(QJsonObject{{u"items"_s, QJsonArray{}}, {u"total"_s, 0}, {u"page"_s, 0},
                            {u"size"_s, 0}});
}

/// One page at a time until the guard goes, then the layout there was. For the tests that go to
/// a page and read it back as the first on screen: in a pair, page 120 is the second of 119's.
/// They used to pass only when another test executable had left « Simple » in a preferences
/// file both read; each has its own file now (`CMakeLists.txt`), so each says what it needs.
[[nodiscard]] auto onePageAtATime(QQmlApplicationEngine &engine)
{
    auto *preferences =
        engine.singletonInstance<Preferences *>(qmlTypeId("Leaf", 1, 0, "Preferences"));
    const Preferences::PageLayout was = preferences->pageLayout();
    preferences->choosePageLayout(Preferences::PageLayout::Single);
    return qScopeGuard([preferences, was] { preferences->choosePageLayout(was); });
}

/// The reader, open on that book and done loading, from the series page so that leaving it
/// goes back there.
Reader *readerOpenOnTheBook(QQmlApplicationEngine &engine)
{
    auto *navigation =
        engine.singletonInstance<Navigation *>(qmlTypeId("Leaf", 1, 0, "Navigation"));
    auto *reader = engine.singletonInstance<Reader *>(qmlTypeId("Leaf", 1, 0, "Reader"));
    if (!navigation || !reader)
        return nullptr;
    navigation->open(Navigation::Destination::Series, {{u"series"_s, u"s1"_s}});
    navigation->open(Navigation::Destination::Reader,
                     {{u"series"_s, u"s1"_s}, {u"entry"_s, u"e12"_s}});
    return reader;
}

/// One frame drawn from now. Layouts — a row, a column — place what they hold when the
/// window is next polished, a frame after what changed them; a position read before that is
/// where things were, and a click aimed there misses.
bool aFrameDrawn(QQuickWindow *window)
{
    QSignalSpy swapped(window, &QQuickWindow::frameSwapped);
    window->update();
    return QTest::qWaitFor([&] { return swapped.count() > 0; });
}

/// The reader open on its last screen with the window active, and the card that the next
/// turn forward raises — null when none came.
QQuickItem *raiseTheEndCard(QQuickWindow *window, QQuickItem *view, Reader *reader)
{
    window->requestActivate();
    if (!QTest::qWaitForWindowActive(window))
        return nullptr;
    reader->goToPage(189);
    // The page drawn and the keys still the reader's, not only `Reader` moved: under load the
    // space bar was once sent while the window had handed its activation elsewhere, went to
    // nothing, and no card came.
    if (!QTest::qWaitFor([&] {
            QQuickItem *last = itemNamed(view, u"reader-page-189"_s);
            return reader->firstPage() == 189 && last && last->isVisible() && window->isActive()
                   && view->hasActiveFocus();
        }))
        return nullptr;
    QTest::keyClick(window, Qt::Key_Space);
    QQuickItem *card = nullptr;
    (void)QTest::qWaitFor([&] {
        card = itemNamed(view, u"reader-end"_s);
        return reader->atTheEnd() && card && card->isVisible();
    });
    // Its buttons placed: a click aimed at « Lire le tome 13 » the moment the card showed
    // once landed where the button had not yet been put, and nothing opened.
    if (!card || !card->isVisible() || !aFrameDrawn(window))
        return nullptr;
    return card;
}

/// The animation that carries the strip a screen down, or null — found by name, for the
/// reason `shelfGlide` is: a key lands by animating `contentY`, and a test that aims or reads
/// a position before it ends reads a strip in motion. The incident is that one's.
QObject *stripGlide(QQuickItem *strip)
{
    return strip->findChild<QObject *>(u"strip-glide"_s);
}

/// The page whose top is at or above `y` in the strip's content, or −1.
int stripPageAt(QQuickItem *strip, qreal y)
{
    int index = -1;
    QMetaObject::invokeMethod(strip, "indexAt", Q_RETURN_ARG(int, index),
                              Q_ARG(double, strip->width() / 2), Q_ARG(double, y));
    return index;
}

/// The centre of an item, in the window's coordinates: where a hand aims at it.
QPoint centreOf(QQuickItem *item)
{
    return item->mapToScene(QPointF(item->width() / 2.0, item->height() / 2.0)).toPoint();
}

/// The bars called — unless they are up already, as a volume opens with them — then the
/// reader's settings opened under `tune`; the card, once it is there. Null when either did
/// not happen.
QQuickItem *openTheSettings(QQuickWindow *window, QQuickItem *view)
{
    if (!view->property("barsShown").toBool())
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          view->mapToScene(QPointF(view->width() / 2.0, view->height() / 2.0))
                              .toPoint());
    QQuickItem *tune = itemNamed(view, u"reader-settings-button"_s);
    QQuickItem *card = itemNamed(view, u"reader-settings"_s);
    if (!tune || !card || !QTest::qWaitFor([&] { return tune->isVisible() && tune->opacity() > 0
                                                         && view->property("barsShown").toBool(); }))
        return nullptr;
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centreOf(tune));
    return QTest::qWaitFor([&] { return card->isVisible(); }) ? card : nullptr;
}

/// Where a click lands at that fraction of the reader's width, halfway down it.
QPoint across(QQuickItem *view, qreal fraction)
{
    return view->mapToScene(QPointF(view->width() * fraction, view->height() / 2.0)).toPoint();
}

/// The bars called, then the volume's panel opened from the book button; the panel, once it
/// is there. Null when either did not happen.
QQuickItem *openTheVolumePanel(QQuickWindow *window, QQuickItem *view)
{
    if (!view->property("barsShown").toBool())
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, across(view, 0.5));
    QQuickItem *book = itemNamed(view, u"reader-volume-button"_s);
    QQuickItem *panel = itemNamed(view, u"reader-volume"_s);
    if (!book || !panel || !QTest::qWaitFor([&] { return book->isVisible() && book->opacity() > 0
                                                          && view->property("barsShown").toBool(); }))
        return nullptr;
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centreOf(book));
    return QTest::qWaitFor([&] { return panel->isVisible(); }) ? panel : nullptr;
}

/// The bars up — called by a click in the middle unless they are already there — and the
/// named button in them once it can be clicked, or null.
QQuickItem *barsCalled(QQuickWindow *window, QQuickItem *view, const QString &name)
{
    if (!view->property("barsShown").toBool())
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, across(view, 0.5));
    QQuickItem *button = itemNamed(view, name);
    if (!button || !QTest::qWaitFor([&] {
            return view->property("barsShown").toBool() && button->isVisible()
                   && button->opacity() > 0;
        }))
        return nullptr;
    // Faded in all the way and placed: a click on a dock still rising lands on nothing.
    QQuickItem *dock = itemNamed(view, u"reader-dock"_s);
    if (dock && !QTest::qWaitFor([&] { return dock->opacity() >= 1; }))
        return nullptr;
    return aFrameDrawn(window) ? button : nullptr;
}

/// The bars a volume opens with sent away, as a turn sends them, and gone from the screen:
/// where a test that starts from a bare page starts. False when they did not go.
bool atRest(QQuickItem *view)
{
    QMetaObject::invokeMethod(view, "sendTheBarsAway");
    QQuickItem *bar = itemNamed(view, u"reader-bar"_s);
    QQuickItem *dock = itemNamed(view, u"reader-dock"_s);
    return bar && dock
           && QTest::qWaitFor([&] { return !bar->isVisible() && !dock->isVisible(); });
}

/// A line of the panel is there when an item of that name is drawn. A line that is not
/// drawn may be absent or merely hidden: both are « no line ».
bool drawn(QQuickItem *root, const QString &name)
{
    QQuickItem *one = itemNamed(root, name);
    return one && one->isVisible();
}
/// The view's zoom: 1 for the whole page, 2 for 200 %.
qreal zoomOf(QQuickItem *view)
{
    return view->property("zoom").toReal();
}

/// The book open in the reader with the window active and the keys the reader's: what every
/// test of the zoom starts from. Null when it did not get there.
QQuickItem *aReaderAtHand(QQmlApplicationEngine &engine, Reader *&reader)
{
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    if (!window || !QTest::qWaitForWindowExposed(window))
        return nullptr;
    reader = readerOpenOnTheBook(engine);
    QQuickItem *view = nullptr;
    if (!reader || !QTest::qWaitFor([&] {
            view = itemNamed(window->contentItem(), u"reader-view"_s);
            return view && !reader->loading() && reader->pageCount() == 190;
        }))
        return nullptr;
    // The pages laid out in their row, which is done a frame after they are made: a zoom
    // asked before that measured a row 0 pixels wide, and centred nothing.
    if (!reader->webtoon() && !QTest::qWaitFor([&] {
            QQuickItem *first = itemNamed(view, u"reader-page-%1"_s.arg(reader->firstPage()));
            return first && first->width() > 0 && first->parentItem()
                   && first->parentItem()->width() >= first->width();
        }))
        return nullptr;
    window->requestActivate();
    if (!QTest::qWaitFor([&] { return window->isActive() && view->hasActiveFocus(); }))
        return nullptr;
    if (!atRest(view))
        return nullptr;
    return aFrameDrawn(window) ? view : nullptr;
}

/// The windows the reader is tried in: the 1920 × 1200 and its neighbours, the usual
/// laptops, a short window, and one in the narrow band.
struct AWindow {
    int width;
    int height;
};
const AWindow theWindows[] = {{900, 600},   {1280, 720},  {1366, 768}, {1920, 1080},
                              {1920, 1200}, {2560, 1440}, {560, 700}};

/// Laid out for this window: resized, and the view at its width and height.
bool laidOutFor(QQuickItem *view, const AWindow &at)
{
    view->window()->resize(at.width, at.height);
    return QTest::qWaitFor(
        [&] { return view->width() == at.width && view->height() == at.height; });
}

/// Where an item is in the window, without what it is scaled or slid by while it fades.
QRectF placed(QQuickItem *item)
{
    return item->parentItem()->mapRectToScene(QRectF(item->position(), item->size()));
}

/// The size of a text's words.
int lettersOf(QQuickItem *text)
{
    return text->property("font").value<QFont>().pixelSize();
}

/// Every text drawn under `root`, shown or not.
void textsUnder(QQuickItem *root, QList<QQuickItem *> &all)
{
    if (root->inherits("QQuickText"))
        all << root;
    for (QQuickItem *child : root->childItems())
        textsUnder(child, all);
}

/// What is wrong with one of the reader's cards in each of `theWindows`, or nothing: `face`
/// as wide as drawn (`width`) scaled as a card of words is (`Scale::columnFor`, half the
/// window's growth since 23/10/2026, when a card grown at the full rate round words that no
/// longer grew was a band of empty paper), but never wider than `share` of
/// the window nor than the window less `margin`, and inside the window; its words `words` at
/// what `role` makes of their drawn `size`. In the test of 22/10/2026 on a full-screen
/// window of 1920, the dock had grown and the cards beside it had kept their drawn size.
/// Measured as laid out, open or not: their geometry is bound to the window, not to being shown.
QString aCardThatDoesNotFollow(QQuickItem *view, const QString &face, const QString &words,
                               qreal width, int size, int (*role)(int, qreal), qreal share,
                               int margin)
{
    QQuickItem *card = itemNamed(view, face);
    QQuickItem *text = itemNamed(view, words);
    if (!card || !text)
        return u"no %1 or no %2"_s.arg(face, words);
    for (const AWindow &at : theWindows) {
        if (!laidOutFor(view, at))
            return u"the view never reached %1 × %2"_s.arg(at.width).arg(at.height);
        const qreal factor = Widths::controlsFor(at.width);
        const qreal wide = std::min({qreal(Scale::columnFor(width, factor)), at.width * share,
                                     qreal(at.width - margin)});
        const int letters = role(size, factor);
        if (!QTest::qWaitFor([&] {
                return qAbs(card->width() - wide) < 0.01 && lettersOf(text) == letters;
            }))
            return u"at %1: %2 is %3 wide for %4, %5 at %6 px for %7"_s.arg(at.width)
                .arg(face)
                .arg(card->width())
                .arg(wide)
                .arg(words)
                .arg(lettersOf(text))
                .arg(letters);
        const QRectF window(0, 0, at.width, at.height);
        if (!window.contains(placed(card)))
            return u"at %1 × %2: %3 is outside the window"_s.arg(at.width).arg(at.height).arg(face);
    }
    return {};
}

/// The key pressed again each time the page has stopped gliding, until the sheet is at that
/// edge — `atXEnd`, `atYEnd` — and never once it is: one press more would be the turn the
/// caller is about to test. A press while the page still glides would aim from where it is
/// passing, not from where it is going.
bool pressedToTheEdge(QQuickWindow *window, QQuickItem *view, Qt::Key key, const char *edge)
{
    QQuickItem *sheet = itemNamed(view, u"reader-sheet"_s);
    QObject *glide = sheet ? sheet->findChild<QObject *>(u"reader-sheet-glide"_s) : nullptr;
    if (!glide)
        return false;
    return QTest::qWaitFor([&] {
        if (sheet->property(edge).toBool() && !glide->property("running").toBool())
            return true;
        if (!glide->property("running").toBool())
            QTest::keyClick(window, key);
        return false;
    }, 10000);
}

/// The page drawn under a point of the window, and where in that page the point falls, in
/// the page's own unzoomed pixels. Null when no page is there.
QQuickItem *pageUnder(QQuickItem *view, Reader *reader, QPointF scene, QPointF &inside)
{
    for (const int page : {reader->firstPage(), reader->secondPage()}) {
        QQuickItem *one = page >= 0 ? itemNamed(view, u"reader-page-%1"_s.arg(page)) : nullptr;
        if (!one)
            continue;
        const QPointF at = one->mapFromScene(scene);
        if (at.x() >= 0 && at.y() >= 0 && at.x() < one->width() && at.y() < one->height()) {
            inside = at;
            return one;
        }
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
        // Every test, not one: whichever screen a test drew, it drew it properly — and
        // whatever gesture it made, the QML answered it without an error nobody would have
        // read.
        const QStringList seen = loopsSoFar();
        QVERIFY2(seen.isEmpty(), qPrintable(seen.join(u"\n"_s)));
    }

    void booting_loads_exactly_one_window()
    {
        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());

        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QCOMPARE(window->title(), u"Leaf"_s);
    }

    void the_loader_opens_on_the_shelf_grid()
    {
        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());

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
        Boot::run(engine, *qGuiApp, nowhere());

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
        Boot::run(engine, *qGuiApp, nowhere());

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
        pretend.answerFor = seatedAsPrincipal(pretend.answerFor);
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());

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
        QVERIFY(mark->isVisible());
        QVERIFY(!itemNamed(reading, u"finished-dn"_s)->isVisible());

        auto *finished = itemNamed(grid, u"tile-ac"_s);
        QVERIFY(finished);
        auto *finishedMark = itemNamed(finished, u"in-progress-ac"_s);
        auto *finishedCover = itemNamed(finished, u"cover-frame-ac"_s);
        auto *finishedRing = itemNamed(finished, u"focus-ac"_s);
        QVERIFY(finishedMark);
        QVERIFY(finishedCover);
        QVERIFY(finishedRing);
        // Full, and checked: a finished series used to carry nothing at all, and read exactly
        // like the one beside it that nobody has opened.
        QVERIFY(finishedMark->isVisible());
        QCOMPARE(itemNamed(finished, u"read-so-far-ac"_s)->width(), finishedMark->width());
        QVERIFY(itemNamed(finished, u"finished-ac"_s)->isVisible());

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
        auto *importButton = window->findChild<QQuickItem *>(u"import-button"_s);
        auto *sortButton = window->findChild<QQuickItem *>(u"sort-button"_s);
        auto *settingsButton = window->findChild<QQuickItem *>(u"settings-button"_s);
        QVERIFY(searchField);
        QVERIFY(filterButton);
        QVERIFY(sortButton);
        QVERIFY(settingsButton);

        QTest::keyClick(window, Qt::Key_Right);
        QTRY_VERIFY(searchField->hasActiveFocus());
        QCOMPARE(grid->property("currentIndex").toInt(), -1);
        // The import took the slot the filter left when it went down to the pills.
        QTest::keyClick(window, Qt::Key_Tab);
        QTRY_VERIFY(importButton->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Tab);
        QTRY_VERIFY(sortButton->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Tab);
        QTRY_VERIFY(settingsButton->hasActiveFocus());
        // Who reads, last in the bar, after the gear.
        QTest::keyClick(window, Qt::Key_Tab);
        QTRY_VERIFY(window->findChild<QQuickItem *>(u"profile-square"_s)->hasActiveFocus());
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
    /// The series page, against the real engine: a header that words itself, and a list whose
    /// lines come from two answers married in C++.
    ///
    /// Headless tests settle what each model holds; this settles that the screen the reader
    /// opens is made of it. A `.qml` file that stopped binding a role would pass every one of
    /// them and draw nothing here.
    void a_series_opens_on_a_page_that_words_its_header_and_its_volumes()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));

        const QByteArray one = QJsonDocument(QJsonObject{
            {u"id"_s, u"albums"_s},
            {u"workId"_s, u"elfes"_s},
            {u"name"_s, u"Terres d'Arran · Elfes"_s},
            {u"universe"_s, u"Terres d'Arran"_s},
            {u"work"_s, u"Elfes"_s},
            {u"edition"_s, u"Albums"_s},
            {u"publisher"_s, u"Soleil"_s},
            {u"medium"_s, u"bd"_s},
            {u"ownedVolumes"_s, 2},
            {u"entryCount"_s, 2},
            {u"chapterCount"_s, 0},
            {u"arcCount"_s, 0},
            {u"summary"_s, u"Cinq peuples elfiques."_s},
            {u"genres"_s, QJsonArray{u"Fantasy"_s, u"Heroic Fantasy"_s}},
        }).toJson(QJsonDocument::Compact);

        const QByteArray files = QJsonDocument(QJsonArray{
            QJsonObject{{u"id"_s, u"v1"_s}, {u"type"_s, u"VOLUME"_s}, {u"number"_s, 1},
                        {u"title"_s, u"Le Crystal"_s}, {u"pageCount"_s, 54},
                        {u"chapterCount"_s, 0}, {u"file"_s, u"T1.cbz"_s}, {u"size"_s, 1}},
            QJsonObject{{u"id"_s, u"v2"_s}, {u"type"_s, u"VOLUME"_s}, {u"number"_s, 2},
                        {u"title"_s, u"L'Honneur"_s}, {u"pageCount"_s, 54},
                        {u"chapterCount"_s, 0}, {u"file"_s, u"T2.cbz"_s}, {u"size"_s, 1}},
        }).toJson(QJsonDocument::Compact);

        const QByteArray states = QJsonDocument(QJsonArray{
            QJsonObject{{u"entryId"_s, u"v1"_s}, {u"page"_s, 53}, {u"pageCount"_s, 54},
                        {u"finished"_s, true}},
        }).toJson(QJsonDocument::Compact);

        const QByteArray shelf = QJsonDocument(QJsonObject{
            {u"items"_s, QJsonArray{}}, {u"total"_s, 0}, {u"page"_s, 0}, {u"size"_s, 0},
        }).toJson(QJsonDocument::Compact);

        const auto reply = [](const QByteArray &body) {
            return "HTTP/1.1 200 .\r\nContent-Type: application/json\r\nContent-Length: "
                   + QByteArray::number(body.size()) + "\r\n\r\n" + body;
        };
        // Counted, for the assertion at the end of this test: the band that offers to resume
        // is drawn on the shelf *and* on this page, and only one of the two is built once.
        auto resumeAsked = std::make_shared<int>(0);
        pretend.answerFor = [=](const QByteArray &request) -> QByteArray {
            if (request.startsWith("GET /next")) {
                ++*resumeAsked;
                return "HTTP/1.1 404 .\r\nContent-Length: 0\r\n\r\n";
            }
            if (request.contains("/entries "))
                return reply(files);
            if (request.contains("/progress "))
                return reply(states);
            if (request.contains("/cover"))
                return "HTTP/1.1 404 .\r\nContent-Length: 0\r\n\r\n";
            if (request.startsWith("GET /series/"))
                return reply(one);
            return reply(shelf);
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());

        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));

        auto *navigation =
            engine.singletonInstance<Navigation *>(qmlTypeId("Leaf", 1, 0, "Navigation"));
        QVERIFY(navigation);
        QVERIFY(navigation->open(Navigation::Destination::Series,
                                 {{u"series"_s, u"albums"_s}}));

        QQuickItem *page = nullptr;
        QTRY_VERIFY((page = itemNamed(window->contentItem(), u"series-view"_s)));
        QVERIFY(itemNamed(page, u"series-header"_s));
        QVERIFY(itemNamed(page, u"series-tabs"_s));
        // The edition has a name, so the level below the work is drawn — and the universe
        // above it is a link and not a title.
        QVERIFY(itemNamed(page, u"series-universe"_s));

        // The shell points the page, and the list follows once the page knows its gaps.
        auto *page_ = engine.singletonInstance<Series *>(qmlTypeId("Leaf", 1, 0, "Series"));
        QVERIFY(page_);
        QTRY_VERIFY(page_->available());
        auto *volumes =
            engine.singletonInstance<Entries *>(qmlTypeId("Leaf", 1, 0, "Entries"));
        QVERIFY(volumes);
        QTRY_COMPARE(volumes->count(), 2);
        // The count is not the end of the loading. The files and what has been read of them
        // are two requests, and the rows are drawn the moment the first lands — carrying « non
        // lu », which is true until the second one does. Read at `count`, the mark was right
        // for as long as this page made three requests and wrong the day it made four.
        QTRY_VERIFY(!volumes->loading());

        // The shelf's band asked once, at startup. This page's band must not ask again: the
        // Loader rebuilds this screen every time a series is opened, so a band fetching on
        // creation fetches once per series opened — which is the trap `Main.qml` names for
        // the page itself, walked into by a band nobody counted as a page.
        QVERIFY2(*resumeAsked <= 1,
                 qPrintable(u"the resume state was asked for %1 times"_s.arg(*resumeAsked)));

        // Two answers, married: the first volume is finished and the second was never opened.
        QQuickItem *first = nullptr;
        QTRY_VERIFY((first = itemNamed(page, u"volume-v1"_s)));
        QVERIFY(itemNamed(page, u"volume-v2"_s));
        QCOMPARE(first->property("state").toInt(), 2);
        QCOMPARE(first->property("number").toString(), u"1"_s);
        QCOMPARE(first->property("title").toString(), u"Le Crystal"_s);
        QCOMPARE(first->property("pages").toString(), u"54 p."_s);

        // The page carries its own way out, and it is a button before it is a shortcut:
        // Escape works and always did, but a page whose only exit is a key nobody is told
        // about is a page with no exit. It names where it goes, not where you are.
        QQuickItem *back = nullptr;
        QTRY_VERIFY((back = itemNamed(page, u"series-back"_s)));
        QVERIFY(back->isVisible());
        QCOMPARE(back->property("label").toString(), navigation->backLabel());
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          back->mapToItem(window->contentItem(),
                                          QPointF(back->width() / 2.0, back->height() / 2.0))
                              .toPoint());
        QTRY_COMPARE(navigation->destination(), Navigation::Destination::Shelf);

        // And Escape leaves it too, whatever happened before it. The window has to hold the
        // keyboard for that: a shortcut nobody is focused on is a shortcut nobody fires.
        QVERIFY(navigation->open(Navigation::Destination::Series,
                                 {{u"series"_s, u"albums"_s}}));
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_COMPARE(navigation->destination(), Navigation::Destination::Shelf);

        // A genre goes where the universe goes: to the shelf, narrowed to it. Pressed in the
        // header — the description draws the same pill, out of sight behind its tab.
        auto *shelfModel = engine.singletonInstance<Shelf *>(qmlTypeId("Leaf", 1, 0, "Shelf"));
        QVERIFY(shelfModel);
        QVERIFY(navigation->open(Navigation::Destination::Series,
                                 {{u"series"_s, u"albums"_s}}));
        QQuickItem *header = nullptr;
        QQuickItem *genre = nullptr;
        QTRY_VERIFY((page = itemNamed(window->contentItem(), u"series-view"_s))
                    && (header = itemNamed(page, u"series-header"_s))
                    && (genre = itemNamed(header, u"genre-Heroic Fantasy"_s))
                    && genre->isVisible() && genre->width() > 0);
        pretend.heard.clear();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          genre->mapToItem(window->contentItem(),
                                           QPointF(genre->width() / 2.0, genre->height() / 2.0))
                              .toPoint());
        QTRY_COMPARE(navigation->destination(), Navigation::Destination::Shelf);
        QCOMPARE(shelfModel->narrowing().value(u"genre"_s).toStringList(),
                 QStringList{u"Heroic Fantasy"_s});
        QTRY_VERIFY(pretend.heard.contains("genre=Heroic%20Fantasy"));

        // The gear is the application's, not the shelf's: it stays on a series page and
        // takes you to the settings, and back from there is back to the series.
        QVERIFY(navigation->open(Navigation::Destination::Series,
                                 {{u"series"_s, u"albums"_s}}));
        auto *settings = window->findChild<QQuickItem *>(u"settings-button"_s);
        QVERIFY(settings);
        QTRY_VERIFY(settings->isVisible());
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          settings->mapToItem(window->contentItem(),
                                              QPointF(settings->width() / 2.0,
                                                      settings->height() / 2.0))
                              .toPoint());
        QTRY_COMPARE(navigation->destination(), Navigation::Destination::Settings);
        QTRY_VERIFY(!settings->isVisible());
        navigation->back();
        QTRY_COMPARE(navigation->destination(), Navigation::Destination::Series);
    }

    /// The two things this page does that no other screen does: it cuts its list where an arc
    /// begins, and its last tab is a way out of the series — the editions of the work, and the
    /// universe walked in the order a file declares.
    void a_series_cuts_its_list_at_an_arc_and_says_where_to_go_from_there()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));

        const auto aWork = [](const QString &id, const QString &edition, const QString &work,
                              int volumes) {
            return QJsonObject{{u"id"_s, id},
                               {u"workId"_s, u"w-"_s + work.toLower()},
                               {u"name"_s, u"Terres d'Arran · "_s + work},
                               {u"universe"_s, u"Terres d'Arran"_s},
                               {u"universeId"_s, u"u-arran"_s},
                               {u"work"_s, work},
                               {u"edition"_s, edition},
                               {u"medium"_s, u"bd"_s},
                               {u"ownedVolumes"_s, volumes},
                               {u"entryCount"_s, volumes},
                               {u"chapterCount"_s, 0},
                               {u"arcCount"_s, 1}};
        };
        const auto shelfOf = [](const QJsonArray &rows) {
            return QJsonDocument(QJsonObject{{u"items"_s, rows},
                                             {u"total"_s, rows.size()},
                                             {u"page"_s, 0},
                                             {u"size"_s, rows.size()}})
                .toJson(QJsonDocument::Compact);
        };

        const QByteArray one = QJsonDocument(aWork(u"albums"_s, u"Albums"_s, u"Elfes"_s, 2))
                                   .toJson(QJsonDocument::Compact);
        const QByteArray editions =
            shelfOf({aWork(u"albums"_s, u"Albums"_s, u"Elfes"_s, 2),
                     aWork(u"integrale"_s, u"Intégrale"_s, u"Elfes"_s, 1)});
        const QByteArray universe = shelfOf({aWork(u"albums"_s, u"Albums"_s, u"Elfes"_s, 2),
                                             aWork(u"nains"_s, QString(), u"Nains"_s, 26)});
        const QByteArray universes =
            QJsonDocument(QJsonArray{QJsonObject{{u"id"_s, u"u-arran"_s},
                                                 {u"name"_s, u"Terres d'Arran"_s},
                                                 {u"orderCount"_s, 1}}})
                .toJson(QJsonDocument::Compact);
        const auto aStep = [](const QString &work, const QString &seriesId, double from,
                              double to) {
            QJsonObject step{{u"workId"_s, u"w-"_s + work.toLower()}, {u"work"_s, work}};
            if (!seriesId.isEmpty()) {
                step[u"unit"_s] = u"VOLUME"_s;
                step[u"seriesId"_s] = seriesId;
                step[u"series"_s] = seriesId;
                step[u"from"_s] = from;
                step[u"to"_s] = to;
            }
            return step;
        };
        // The same work twice, which is the whole reason an order is not a sorted list.
        const QByteArray ways =
            QJsonDocument(QJsonArray{
                              QJsonObject{{u"id"_s, u"chrono"_s},
                                          {u"name"_s, u"Chronologique"_s},
                                          {u"default"_s, true},
                                          {u"steps"_s,
                                           QJsonArray{aStep(u"Elfes"_s, u"albums"_s, 1, 1),
                                                      aStep(u"Nains"_s, QString(), 0, 0),
                                                      aStep(u"Elfes"_s, u"albums"_s, 2, 2)}}}})
                .toJson(QJsonDocument::Compact);

        const QByteArray files = QJsonDocument(QJsonArray{
            QJsonObject{{u"id"_s, u"v1"_s}, {u"type"_s, u"VOLUME"_s}, {u"number"_s, 1},
                        {u"title"_s, u"Le Crystal"_s}, {u"pageCount"_s, 54},
                        {u"chapterCount"_s, 0}, {u"file"_s, u"T1.cbz"_s}, {u"size"_s, 1}},
            QJsonObject{{u"id"_s, u"v2"_s}, {u"type"_s, u"VOLUME"_s}, {u"number"_s, 2},
                        {u"title"_s, u"L'Honneur"_s}, {u"pageCount"_s, 54},
                        {u"chapterCount"_s, 0}, {u"file"_s, u"T2.cbz"_s}, {u"size"_s, 1}},
        }).toJson(QJsonDocument::Compact);
        // The second volume is open, which is what puts « ici » on the second step and not on
        // the first: two stretches of one work, one reader.
        const QByteArray states = QJsonDocument(QJsonArray{
            QJsonObject{{u"entryId"_s, u"v1"_s}, {u"page"_s, 54}, {u"pageCount"_s, 54},
                        {u"finished"_s, true}},
            QJsonObject{{u"entryId"_s, u"v2"_s}, {u"page"_s, 12}, {u"pageCount"_s, 54},
                        {u"finished"_s, false}},
        }).toJson(QJsonDocument::Compact);
        const QByteArray arcs = QJsonDocument(QJsonArray{
            QJsonObject{{u"id"_s, u"a2"_s}, {u"name"_s, u"La Guerre"_s},
                        {u"unit"_s, u"VOLUME"_s}, {u"from"_s, 2}, {u"to"_s, 2},
                        {u"position"_s, 1}},
        }).toJson(QJsonDocument::Compact);

        const auto reply = [](const QByteArray &body) {
            return "HTTP/1.1 200 .\r\nContent-Type: application/json\r\nContent-Length: "
                   + QByteArray::number(body.size()) + "\r\n\r\n" + body;
        };
        pretend.answerFor = [=](const QByteArray &request) -> QByteArray {
            if (request.contains("/entries "))
                return reply(files);
            if (request.contains("/progress "))
                return reply(states);
            if (request.contains("/arcs "))
                return reply(arcs);
            if (request.contains("/orders "))
                return reply(ways);
            if (request.contains("GET /universes"))
                return reply(universes);
            if (request.contains("/cover"))
                return "HTTP/1.1 404 .\r\nContent-Length: 0\r\n\r\n";
            if (request.contains("universe="))
                return reply(universe);
            if (request.startsWith("GET /series/"))
                return reply(one);
            return reply(editions);
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));

        auto *navigation =
            engine.singletonInstance<Navigation *>(qmlTypeId("Leaf", 1, 0, "Navigation"));
        QVERIFY(navigation);
        QVERIFY(navigation->open(Navigation::Destination::Series,
                                 {{u"series"_s, u"albums"_s}}));

        QQuickItem *page = nullptr;
        QTRY_VERIFY((page = itemNamed(window->contentItem(), u"series-view"_s)));

        // The list is cut where the arc begins: a separator between the two volumes, named
        // and carrying its range — and the volume it begins on is still drawn. Looked up
        // again at each try rather than held: every answer rebuilds the list, and a delegate
        // kept across one is a pointer to an item that has been destroyed.
        const auto separatorRange = [&] {
            const QQuickItem *row = itemNamed(page, u"arc-La Guerre"_s);
            return row == nullptr ? QString() : row->property("range").toString();
        };
        QTRY_COMPARE(separatorRange(), u"tome 2"_s);
        QVERIFY(itemNamed(page, u"volume-v1"_s));
        QVERIFY(itemNamed(page, u"volume-v2"_s));

        auto *page_ =
            engine.singletonInstance<Series *>(qmlTypeId("Leaf", 1, 0, "Series"));
        auto *volumes =
            engine.singletonInstance<Entries *>(qmlTypeId("Leaf", 1, 0, "Entries"));
        QVERIFY(page_);
        QVERIFY(volumes);

        // Changing edition, made as a reader makes it: the pill, then the other edition.
        // It is the one gesture that tears down the list it was made in — the row handling
        // the tap is rebuilt by what the tap does — and the menu stayed open over the
        // edition it had just switched to, with a `TypeError` per click in the terminal.
        //
        // Pressed once the line it sits on is laid out. The pill is drawn as soon as the
        // editions are known and the edition's own name, to its left, comes with the page a
        // moment later: on a loaded machine the name landed between measuring the pill and
        // pressing it, pushed it sixty pixels right, and the press fell beside it.
        QQuickItem *pill = nullptr;
        const auto laidOut = [&] {
            pill = itemNamed(page, u"edition-switch"_s);
            if (pill == nullptr || page_->loading() || page_->edition().isEmpty())
                return false;
            const QQuickItem *line = pill->parentItem();
            const QQuickItem *name = line->childItems().constFirst();
            return name->width() > 0
                   && qFuzzyCompare(pill->x(), name->width() + line->property("spacing").toReal());
        };
        QTRY_VERIFY(laidOut());
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          pill->mapToItem(window->contentItem(),
                                          QPointF(pill->width() / 2.0, pill->height() / 2.0))
                              .toPoint());

        QQuickItem *other = nullptr;
        QTRY_VERIFY((other = itemNamed(window->contentItem(), u"edition-integrale"_s)));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          other->mapToItem(window->contentItem(),
                                           QPointF(other->width() / 2.0,
                                                   other->height() / 2.0))
                              .toPoint());

        QTRY_COMPARE(page_->identifier(), u"integrale"_s);
        // And the menu is gone with it: if I want to change again, I will open it again.
        QTRY_VERIFY(!itemNamed(window->contentItem(), u"edition-integrale"_s));

        // Back to the edition this test is about, so what follows reads the list it served.
        QVERIFY(navigation->open(Navigation::Destination::Series,
                                 {{u"series"_s, u"albums"_s}}));
        QTRY_COMPARE(page_->identifier(), u"albums"_s);
        QTRY_VERIFY(!volumes->loading());

        // The last tab is reached by a click on it, like a reader reaches it.
        QQuickItem *tab = nullptr;
        QTRY_VERIFY((tab = itemNamed(page, u"series-tab-2"_s)));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          tab->mapToItem(window->contentItem(),
                                         QPointF(tab->width() / 2.0, tab->height() / 2.0))
                              .toPoint());
        QQuickItem *elsewhere = nullptr;
        QTRY_VERIFY((elsewhere = itemNamed(page, u"elsewhere-tab"_s)));
        QTRY_VERIFY(elsewhere->isVisible());

        // Both blocks are drawn, and neither is in the other's list.
        QQuickItem *editionsBlock = itemNamed(elsewhere, u"editions-block"_s);
        QQuickItem *universeBlock = itemNamed(elsewhere, u"universe-block"_s);
        QVERIFY(editionsBlock);
        QVERIFY(universeBlock);
        QTRY_VERIFY(itemNamed(editionsBlock, u"tile-integrale"_s));
        QTRY_VERIFY(itemNamed(universeBlock, u"tile-nains"_s));
        QVERIFY(!itemNamed(universeBlock, u"tile-integrale"_s));

        // A tile per step: the work being read is there twice, and « ici » is on the stretch
        // holding the volume that is open — the second, because the first is finished.
        //
        // Which step carries the mark, and -1 for none, -2 for two: the answer that draws the
        // steps and the answer that says which volume is open are not the same one, so the
        // tiles are there before the mark is, and a run that read them once would be right
        // only when the two landed in the order it happened to expect.
        const auto marked = [&] {
            const QList<QQuickItem *> steps = itemsNamed(universeBlock, u"tile-albums"_s);
            if (steps.size() != 2)
                return -3;
            int which = -1;
            for (int at = 0; at < steps.size(); ++at) {
                if (steps.at(at)->property("detail").toString().endsWith(u"· ici"_s))
                    which = which < 0 ? at : -2;
            }
            return which;
        };
        QTRY_COMPARE(marked(), 1);
    }

    void a_menu_offers_only_what_the_profile_may_do_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::addColumn<bool>("import");
        QTest::addColumn<bool>("edit");
        QTest::addColumn<bool>("erase");
        QTest::addColumn<QStringList>("offered");

        const QStringList all{u"read"_s, u"unread"_s, u"reimport"_s, u"copy"_s, u"erase"_s};
        QTest::newRow("the principal gets everything")
            << u"principal"_s << true << true << true << all;
        // A copy is not a right of its own: whoever is not restricted may take a file out.
        QTest::newRow("a reader without rights gets what reads and what copies")
            << u"reader"_s << false << false << false
            << QStringList{u"read"_s, u"unread"_s, u"copy"_s};
        QTest::newRow("a reader who may import and edit is still not offered erasing")
            << u"reader"_s << true << true << false
            << QStringList{u"read"_s, u"unread"_s, u"reimport"_s, u"copy"_s};
        QTest::newRow("a restricted profile gets only what reads")
            << u"restricted"_s << true << true << true
            << QStringList{u"read"_s, u"unread"_s};
    }

    /// The entries of a volume's menu are built in one place, and each answers to one right.
    /// There is no entry yet for « Modifier » or « Réorganiser », so `edit` is in the table
    /// to show that it adds nothing here, not because anything hangs on it.
    void a_menu_offers_only_what_the_profile_may_do()
    {
        QFETCH(QString, kind);
        QFETCH(bool, import);
        QFETCH(bool, edit);
        QFETCH(bool, erase);
        QFETCH(QStringList, offered);

        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        const auto reply = [](const QByteArray &body) {
            return "HTTP/1.1 200 .\r\nContent-Type: application/json\r\nContent-Length: "
                   + QByteArray::number(body.size()) + "\r\n\r\n" + body;
        };
        const QByteArray one = QJsonDocument(QJsonObject{
            {u"id"_s, u"albums"_s}, {u"workId"_s, u"elfes"_s}, {u"name"_s, u"Elfes"_s},
            {u"work"_s, u"Elfes"_s}, {u"edition"_s, u"Albums"_s}, {u"medium"_s, u"bd"_s},
            {u"ownedVolumes"_s, 1}, {u"entryCount"_s, 1}, {u"chapterCount"_s, 0},
            {u"arcCount"_s, 0},
        }).toJson(QJsonDocument::Compact);
        const QByteArray files = QJsonDocument(QJsonArray{QJsonObject{
            {u"id"_s, u"v23"_s}, {u"type"_s, u"VOLUME"_s}, {u"number"_s, 23},
            {u"title"_s, u"La Nuit des Sylvains"_s}, {u"pageCount"_s, 54},
            {u"chapterCount"_s, 0}, {u"file"_s, u"Tome 23.cbz"_s}, {u"size"_s, 48000000},
        }}).toJson(QJsonDocument::Compact);
        const QByteArray seat = aSeating(
            {aProfile(u"p"_s, u"Profil"_s, u"emeraude"_s, kind, import, 0, edit, erase)},
            u"p"_s, false);
        pretend.answerFor = [=](const QByteArray &request) -> QByteArray {
            if (request.startsWith("GET /profiles "))
                return seat;
            if (request.contains("/entries "))
                return reply(files);
            if (request.contains("/progress "))
                return reply(QByteArrayLiteral("[]"));
            if (request.contains("/cover"))
                return "HTTP/1.1 404 .\r\nContent-Length: 0\r\n\r\n";
            if (request.startsWith("GET /series/"))
                return reply(one);
            return reply(QByteArrayLiteral(R"({"items":[],"total":0,"page":0,"size":0})"));
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        QQuickWindow *window = shown(engine);
        QVERIFY(window);
        auto *profiles = engine.singletonInstance<Profiles *>(qmlTypeId("Leaf", 1, 0, "Profiles"));
        QVERIFY(profiles);
        QTRY_VERIFY(profiles->isPrincipal() || profiles->isRestricted() || profiles->canExport());

        auto *navigation =
            engine.singletonInstance<Navigation *>(qmlTypeId("Leaf", 1, 0, "Navigation"));
        QVERIFY(navigation);
        QVERIFY(navigation->open(Navigation::Destination::Series, {{u"series"_s, u"albums"_s}}));
        QQuickItem *page = nullptr;
        QTRY_VERIFY((page = itemNamed(window->contentItem(), u"series-view"_s)));
        QQuickItem *dots = nullptr;
        QTRY_VERIFY((dots = itemNamed(page, u"commands-v23"_s)));
        QVERIFY(QMetaObject::invokeMethod(dots, "show"));

        for (const QString &entry :
             {u"read"_s, u"unread"_s, u"reimport"_s, u"copy"_s, u"erase"_s}) {
            const QString name = u"command-"_s + entry + u"-v23"_s;
            QTRY_VERIFY2(itemNamed(window->contentItem(), name), qPrintable(name));
            QCOMPARE_EQ(shownNamed(window->contentItem(), name), offered.contains(entry));
        }
    }

    /// The list becomes a wall of covers, and the three dots reach the one confirmation in
    /// this client that unmakes a file. Both are gestures, so both are made here: a menu that
    /// opens and commands nothing, and a toggle that changes an icon and nothing else, are
    /// exactly what every headless test passes over.
    void the_grid_draws_covers_and_the_dots_reach_the_confirmation()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));

        const QByteArray one = QJsonDocument(QJsonObject{
            {u"id"_s, u"albums"_s},
            {u"workId"_s, u"elfes"_s},
            {u"name"_s, u"Terres d'Arran · Elfes"_s},
            {u"work"_s, u"Elfes"_s},
            {u"edition"_s, u"Albums"_s},
            {u"medium"_s, u"bd"_s},
            {u"ownedVolumes"_s, 3},
            {u"entryCount"_s, 3},
            {u"chapterCount"_s, 0},
            {u"arcCount"_s, 0},
        }).toJson(QJsonDocument::Compact);

        const auto aVolume = [](int number, const QString &title) {
            return QJsonObject{{u"id"_s, u"v%1"_s.arg(number)}, {u"type"_s, u"VOLUME"_s},
                               {u"number"_s, number},          {u"title"_s, title},
                               {u"pageCount"_s, 54},           {u"chapterCount"_s, 0},
                               {u"file"_s, u"Tome %1.cbz"_s.arg(number)},
                               {u"size"_s, 48000000}};
        };
        const QByteArray files = QJsonDocument(QJsonArray{
            aVolume(22, u"Les Portes de Nyn"_s), aVolume(23, u"La Nuit des Sylvains"_s),
            aVolume(24, u"Le Serment de Lanawyn"_s)}).toJson(QJsonDocument::Compact);
        const QByteArray shelf = QJsonDocument(QJsonObject{
            {u"items"_s, QJsonArray{}}, {u"total"_s, 0}, {u"page"_s, 0}, {u"size"_s, 0},
        }).toJson(QJsonDocument::Compact);

        const auto reply = [](const QByteArray &body) {
            return "HTTP/1.1 200 .\r\nContent-Type: application/json\r\nContent-Length: "
                   + QByteArray::number(body.size()) + "\r\n\r\n" + body;
        };
        // Seated as a principal who may erase: the menu no longer offers « Supprimer » to
        // a profile the server has not said may.
        const QByteArray seat = aSeating(
            {aProfile(u"p"_s, u"Principal"_s, u"emeraude"_s, u"principal"_s, true, 0, true,
                      true)},
            u"p"_s, false);
        pretend.answerFor = [=](const QByteArray &request) -> QByteArray {
            if (request.startsWith("GET /profiles "))
                return seat;
            if (request.contains("/entries "))
                return reply(files);
            if (request.contains("/progress "))
                return reply(QByteArrayLiteral("[]"));
            if (request.contains("/cover"))
                return "HTTP/1.1 404 .\r\nContent-Length: 0\r\n\r\n";
            if (request.startsWith("GET /series/"))
                return reply(one);
            return reply(shelf);
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));

        auto *navigation =
            engine.singletonInstance<Navigation *>(qmlTypeId("Leaf", 1, 0, "Navigation"));
        QVERIFY(navigation);
        QVERIFY(navigation->open(Navigation::Destination::Series,
                                 {{u"series"_s, u"albums"_s}}));

        QQuickItem *page = nullptr;
        QTRY_VERIFY((page = itemNamed(window->contentItem(), u"series-view"_s)));
        auto *volumes = engine.singletonInstance<Entries *>(qmlTypeId("Leaf", 1, 0, "Entries"));
        QVERIFY(volumes);
        QTRY_COMPARE(volumes->count(), 3);
        QTRY_VERIFY(!volumes->loading());
        QTRY_VERIFY(itemNamed(page, u"volume-v23"_s));

        // The toggle at the end of the tab bar draws covers where there were lines. It was
        // wired to the icon of its own button and to nothing else for one whole branch.
        auto *preferences =
            engine.singletonInstance<Preferences *>(qmlTypeId("Leaf", 1, 0, "Preferences"));
        QVERIFY(preferences);
        preferences->showVolumesAsGrid(true);
        QTRY_VERIFY(itemNamed(page, u"tile-volume-v23"_s));
        // And the line is gone with it: two repeaters over one model, one of them emptied,
        // rather than thirty delegates built for nobody.
        QVERIFY(!itemNamed(page, u"volume-v23"_s));

        preferences->showVolumesAsGrid(false);
        QTRY_VERIFY(itemNamed(page, u"volume-v23"_s));

        // The three dots of that line, pressed as a reader presses them — which begins by
        // moving onto the line, because nothing is drawn there until the pointer is over it.
        QQuickItem *dots = nullptr;
        QTRY_VERIFY((dots = itemNamed(page, u"commands-v23"_s)));
        // Opened the way a keyboard opens it. A synthetic pointer move does not make a row
        // believe it is hovered, and what is worth settling here is that the menu is built
        // and that what it commands reaches the model — not how a real mouse behaves.
        // The menu is built before the server has said who is seated, and offers nothing
        // that needs a right until it has.
        auto *seated = engine.singletonInstance<Profiles *>(qmlTypeId("Leaf", 1, 0, "Profiles"));
        QVERIFY(seated);
        QTRY_VERIFY(seated->canErase());
        QVERIFY(QMetaObject::invokeMethod(dots, "show"));

        QQuickItem *erase = nullptr;
        QTRY_VERIFY((erase = itemNamed(window->contentItem(), u"command-erase-v23"_s)));
        QVERIFY(laidOut(window));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          erase->mapToItem(window->contentItem(),
                                           QPointF(erase->width() / 2.0,
                                                   erase->height() / 2.0))
                              .toPoint());

        // And the confirmation says which volume, what it leaves behind, and that there is
        // no trash — the three things it exists to say.
        const auto questionOf = [&] {
            const QQuickItem *said = itemNamed(window->contentItem(), u"erase-question"_s);
            return said == nullptr ? QString() : said->property("text").toString();
        };
        QTRY_COMPARE(questionOf(), u"Supprimer le tome 23 ?"_s);
        QVERIFY(itemNamed(window->contentItem(), u"erase-leaves"_s)
                    ->property("text").toString().contains(u"rejoindra les manquants"_s));
        QVERIFY(itemNamed(window->contentItem(), u"erase-warning"_s)
                    ->property("text").toString().contains(u"corbeille"_s));
    }

    /// The third section of the settings, and a bubble drawn over whatever is on screen.
    /// Both are made of a model nobody can see from here — a switch bound to nothing looks
    /// exactly like a switch that is off, and a bubble that fails to build leaves the screen
    /// it was raised over looking perfectly well.
    void the_switches_turn_warnings_off_and_a_bubble_is_drawn_over_the_shelf()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        // Seated as the principal, who is told about every family: a profile that cannot
        // import is not offered a switch for it.
        pretend.answerFor = seatedAsPrincipal([](const QByteArray &) {
            const QByteArray body = R"({"items":[],"total":0,"page":0,"size":0})";
            return "HTTP/1.1 200 .\r\nContent-Type: application/json\r\nContent-Length: "
                   + QByteArray::number(body.size()) + "\r\n\r\n" + body;
        });
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));

        auto *preferences =
            engine.singletonInstance<Preferences *>(qmlTypeId("Leaf", 1, 0, "Preferences"));
        QVERIFY(preferences);
        using enum Preferences::Warns;
        for (const Preferences::Warns which : {Imports, Scans, Downloads, Failures})
            preferences->showBubble(which, true);

        auto *navigation =
            engine.singletonInstance<Navigation *>(qmlTypeId("Leaf", 1, 0, "Navigation"));
        QVERIFY(navigation);
        QVERIFY(navigation->open(Navigation::Destination::Settings, {}));

        QQuickItem *screen = nullptr;
        QTRY_VERIFY((screen = itemNamed(window->contentItem(), u"settings-view"_s)));
        // Its two cards are drawn only in their own section.
        screen->setProperty("section", u"notifications"_s);
        QQuickItem *lever = nullptr;
        QTRY_VERIFY((lever = itemNamed(screen, u"warns-scans-bubble"_s)));
        QTRY_VERIFY(itemNamed(screen, u"settings-corner"_s)->isVisible());

        // Pressed the way a keyboard presses it, because a switch is a control and not a
        // picture: the same press a reader makes with the space bar.
        lever->forceActiveFocus(Qt::TabFocusReason);
        const QQuickItem *ring = itemNamed(lever, u"warns-scans-bubble-focus"_s);
        QVERIFY(ring);
        QTRY_VERIFY(ring->isVisible());
        const QPointer<QQuickItem> pressed(lever);
        const QQuickItem *knob = itemNamed(lever, u"warns-scans-bubble-knob"_s);
        QVERIFY(knob);
        const qreal from = knob->x();
        QTest::keyClick(window, Qt::Key_Space);
        QTRY_VERIFY(!preferences->bubbles(Scans));
        // The switch pressed is the one that travels. The lines were once rebuilt on every
        // change, and the knob arrived in a new switch already at its other end: the
        // animation, whatever its length, had nothing left to play on.
        QVERIFY(pressed);
        QCOMPARE(itemNamed(screen, u"warns-scans-bubble"_s), pressed.data());
        QTRY_VERIFY(knob->x() < from);

        // And pressed the way a hand presses it, which is the gesture that stretches the knob.
        // The knob coming back from its stretch is a width that changes while it travels;
        // when the travel was computed from that width it sat still until the width had
        // settled, and stood out past the end of the track meanwhile. So: it is under way
        // while still longer than it rests, and it never leaves the track.
        QTRY_COMPARE(knob->x(), lever->property("inset").toReal());
        const qreal rests = knob->width();
        const QPoint centre = lever->mapToScene(QPointF(lever->width() / 2.0,
                                                        lever->height() / 2.0)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, centre);
        QTRY_VERIFY(knob->width() > rests + 2);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, centre);
        // The ring is the keyboard's, and the first click puts it away — even on a switch
        // that keeps the focus it had, which is the one click nothing in QML could hear.
        QVERIFY(lever->hasActiveFocus());
        QVERIFY(!ring->isVisible());
        bool leftWhileLong = false;
        for (int frame = 0; frame < 40; ++frame) {
            QVERIFY(knob->x() >= 1);
            QVERIFY(knob->x() + knob->width() <= lever->width() - 1);
            if (knob->width() > rests + 0.5 && knob->x() > from / 3)
                leftWhileLong = true;
            QTest::qWait(16);
        }
        QVERIFY(preferences->bubbles(Scans));
        QVERIFY(leftWhileLong);
        preferences->showBubble(Scans, false);

        // With nothing left to make a bubble, the card that says where they go is not drawn
        // at all: an empty heading is worse than an absent one.
        for (const Preferences::Warns which : {Imports, Downloads, Failures})
            preferences->showBubble(which, false);
        QTRY_VERIFY(!itemNamed(screen, u"settings-corner"_s)->isVisible());

        // And a bubble, raised over the shelf. It says its two lines and stays its ten seconds,
        // or until its cross.
        preferences->showBubble(Failures, true);
        QVERIFY(navigation->open(Navigation::Destination::Shelf, {}));
        auto *bubbles = engine.singletonInstance<Toasts *>(qmlTypeId("Leaf", 1, 0, "Toasts"));
        QVERIFY(bubbles);
        bubbles->scanFailed(u"le disque ne répond pas"_s);

        const auto headlineOf = [&] {
            const QQuickItem *said = itemNamed(window->contentItem(), u"toast-0-headline"_s);
            return said == nullptr ? QString() : said->property("text").toString();
        };
        QTRY_COMPARE(headlineOf(), Words::scanStopped());
        QTest::qWait(250);
        QCOMPARE(bubbles->count(), 1);

        // The cross is the way out before the time is up.
        QQuickItem *cross = nullptr;
        QTRY_VERIFY((cross = itemNamed(window->contentItem(), u"toast-0-close"_s)));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          cross->mapToItem(window->contentItem(),
                                           QPointF(cross->width() / 2.0,
                                                   cross->height() / 2.0))
                              .toPoint());
        QTRY_COMPARE(bubbles->count(), 0);
    }

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
        Boot::run(engine, *qGuiApp, nowhere());
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
        // Written by the files, so never read as markup: « <b>x</b> » stays seven characters.
        QCOMPARE(name->property("textFormat").toInt(), int(Qt::PlainText));
        QCOMPARE(where->property("textFormat").toInt(), int(Qt::PlainText));
        QCOMPARE(where->property("text").toString(),
                 u"Tome 12 · Page 48/190 · Chap.\u00A098\u00A0: <b>x</b> & y"_s);
        QCOMPARE(label->property("text").toString(), u"Reprendre"_s);
        QVERIFY(label->isVisible());
        QCOMPARE(cover->property("source").toString(),
                 u"http://127.0.0.1:%1/entries/volume-12/cover"_s.arg(pretend.serverPort()));

        // The display family, and larger than what is said under it: the name of the series
        // is the one thing read from across the room. The size itself is pinned so that a
        // page-wide change of scale is a decision and not a drift.
        const QFont nameFont = name->property("font").value<QFont>();
        QCOMPARE(nameFont.family(), Theme().displayFamily());
        QCOMPARE(nameFont.pixelSize(), 22);
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
        pretend.answerFor = seatedAsPrincipal(pretend.answerFor);
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
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
        auto *importButton = window->findChild<QQuickItem *>(u"import-button"_s);
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
        QTRY_VERIFY(importButton->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Tab);
        QTRY_VERIFY(sortButton->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Tab);
        QTRY_VERIFY(settingsButton->hasActiveFocus());
        // Who reads, last in the bar, after the gear.
        QTest::keyClick(window, Qt::Key_Tab);
        QTRY_VERIFY(window->findChild<QQuickItem *>(u"profile-square"_s)->hasActiveFocus());
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
        Boot::run(engine, *qGuiApp, nowhere());
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
        Boot::run(engine, *qGuiApp, nowhere());
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
        QByteArray filtersReply =
            aReply(200, QByteArrayLiteral("application/json"), someFilters());
        const QByteArray coverReply =
            aReply(200, QByteArrayLiteral("image/png"), aCover());
        pretend.answerFor = [pageReply, nextReply, &filtersReply,
                             coverReply](const QByteArray &request) {
            if (request.startsWith("GET /next"))
                return nextReply;
            if (request.startsWith("GET /filters"))
                return filtersReply;
            return request.startsWith("GET /series?") ? pageReply : coverReply;
        };
        pretend.answerFor = seatedAsPrincipal(pretend.answerFor);
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
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

        // A series marked read moves two numbers, and the chips carrying them change their
        // words where they stand. Each was rebuilt from nothing whenever any number moved —
        // the chip under the pointer replaced by another — and before that, nothing asked
        // for the numbers again at all.
        {
            const QPointer<QQuickItem> sameUnread(unread);
            const QPointer<QQuickItem> sameManga(manga);
            filtersReply = aReply(200, QByteArrayLiteral("application/json"), someFilters(11, 5));
            auto *filters =
                engine.singletonInstance<Filters *>(qmlTypeId("Leaf", 1, 0, "Filters"));
            QVERIFY(filters);
            filters->refresh();
            QTRY_COMPARE(said->property("text").toString(), u"Non lues 11"_s);
            QCOMPARE(itemNamed(pills, u"chip-READ-text"_s)->property("text").toString(),
                     u"Terminées 5"_s);
            QVERIFY(sameUnread);
            QVERIFY(sameManga);
            QCOMPARE(itemNamed(pills, u"chip-UNREAD"_s), sameUnread.data());
        }
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
        auto *importButton = window->findChild<QQuickItem *>(u"import-button"_s);
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
        QTRY_VERIFY(importButton->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Tab);
        QTRY_VERIFY(sortButton->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Tab);
        QTRY_VERIFY(settingsButton->hasActiveFocus());
        // Who reads, last in the bar, after the gear.
        QTest::keyClick(window, Qt::Key_Tab);
        QTRY_VERIFY(window->findChild<QQuickItem *>(u"profile-square"_s)->hasActiveFocus());
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
        QTRY_VERIFY(window->findChild<QQuickItem *>(u"profile-square"_s)->hasActiveFocus());

        searchField->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY(searchField->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Backtab);
        QTRY_COMPARE(grid->property("currentIndex").toInt(), 11);

        // Lighting one sends the contract's spelling to the shelf, and the shelf asks again.
        auto *shelf = engine.singletonInstance<Shelf *>(qmlTypeId("Leaf", 1, 0, "Shelf"));
        QVERIFY(shelf);
        // Backtab sent the shelf gliding to the last cover; the chip is aimed at only once
        // the row that carries it has stopped.
        QObject *glide = shelfGlide(grid);
        QVERIFY2(glide, "no object named shelf-glide under the grid: the wait would be empty");
        QTRY_VERIFY(!glide->property("running").toBool());
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

    /// The pointer emphasises; it does not scroll, and it does not share. A cover cut by the
    /// bottom edge pulled the shelf up under a pointer that was only passing, because hovering
    /// wrote the view's current item. And a chip the keyboard had been left on stayed ringed
    /// beside the hovered cover, so Enter opened the one nobody was looking at.
    void hovering_a_cover_neither_scrolls_the_shelf_nor_leaves_a_second_thing_lit()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        QJsonArray many;
        for (int i = 0; i < 40; ++i) {
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
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->requestActivate();
        QTRY_VERIFY(window->isActive());

        auto *pills = window->findChild<QQuickItem *>(u"filter-pills"_s);
        auto *grid = window->findChild<QQuickItem *>(u"shelf-grid"_s);
        QVERIFY(pills);
        QVERIFY(grid);
        QTRY_VERIFY(pills->isVisible());
        QTRY_COMPARE(grid->property("count").toInt(), 40);
        // After the list has settled: the shelf puts itself back at its beginning while a page
        // is landing, and that is not the pointer moving it.
        QTest::qWait(400);

        // A cover the bottom edge cuts through, hovered on the part of it that shows.
        const qreal gridBottom = grid->mapToItem(nullptr, QPointF(0, grid->height())).y();
        QQuickItem *cut = nullptr;
        for (int i = 0; i < 40 && !cut; ++i) {
            auto *frame = itemNamed(grid, u"cover-frame-s%1"_s.arg(i));
            if (!frame || !frame->isVisible())
                continue;
            const qreal top = frame->mapToItem(nullptr, QPointF(0, 0)).y();
            const qreal bottom = frame->mapToItem(nullptr, QPointF(0, frame->height())).y();
            if (top < gridBottom - 20 && bottom > gridBottom)
                cut = frame;
        }
        QVERIFY2(cut, "no cover is cut by the bottom edge: the window is too tall to test this");
        const qreal before = grid->property("contentY").toReal();
        const QPointF cutTop = cut->mapToItem(nullptr, QPointF(cut->width() / 2, 0));
        QTest::mouseMove(window, QPoint(int(cutTop.x()), int(cutTop.y()) + 10), 20);
        QTest::qWait(400);
        QCOMPARE(grid->property("contentY").toReal(), before);

        // The keyboard left on a chip, then a cover hovered: one thing lit, and it is the cover.
        auto *unread = itemNamed(pills, u"chip-UNREAD"_s);
        auto *unreadRing = itemNamed(pills, u"chip-UNREAD-focus"_s);
        auto *first = itemNamed(grid, u"cover-frame-s0"_s);
        auto *firstRing = itemNamed(grid, u"focus-s0"_s);
        QVERIFY(unread);
        QVERIFY(unreadRing);
        QVERIFY(first);
        QVERIFY(firstRing);
        QTest::mouseMove(window, QPoint(window->width() - 2, 2), 20);
        unread->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY(unreadRing->isVisible());

        const auto centreOf = [](QQuickItem *item) {
            return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
        };
        QTest::mouseMove(window, centreOf(first), 20);
        QTRY_VERIFY(firstRing->isVisible());
        QTRY_VERIFY(!unreadRing->isVisible());
        QVERIFY(unread->hasActiveFocus());

        // Leaving the cover does not bring the ring back on its own: nobody went back to the
        // chip by moving the mouse away from a cover.
        QTest::mouseMove(window, QPoint(window->width() - 2, 2), 20);
        QTRY_VERIFY(!firstRing->isVisible());
        QTest::qWait(200);
        QVERIFY(!unreadRing->isVisible());
        QVERIFY(unread->hasActiveFocus());

        // Enter chooses nothing it cannot be seen choosing: it hands the emphasis back to the
        // keyboard, where it was left. The next Enter is the chip's.
        QTest::mouseMove(window, centreOf(first), 20);
        QTRY_VERIFY(firstRing->isVisible());
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(unreadRing->isVisible());
        QTRY_VERIFY(!firstRing->isVisible());
        QVERIFY(unread->hasActiveFocus());
        QTest::qWait(200);
        QVERIFY(!pretend.heard.contains("read=UNREAD"));

        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(pretend.heard.contains("read=UNREAD"));

        // And a key that moves brings the ring back by moving it: hovered, left, then Right —
        // the next chip is lit, from where the keyboard had been left.
        auto *read = itemNamed(pills, u"chip-READ"_s);
        auto *readRing = itemNamed(pills, u"chip-READ-focus"_s);
        QVERIFY(read);
        QVERIFY(readRing);
        QTRY_VERIFY(unread->hasActiveFocus());
        // Found again: lighting the chip asked for the shelf anew, and its tiles were rebuilt.
        QTest::qWait(400);
        QTRY_VERIFY((first = itemNamed(grid, u"cover-frame-s0"_s)));
        QTRY_VERIFY((firstRing = itemNamed(grid, u"focus-s0"_s)));
        QTest::mouseMove(window, QPoint(window->width() - 2, 2), 20);
        QTest::mouseMove(window, centreOf(first), 20);
        QTRY_VERIFY(firstRing->isVisible());
        QTest::mouseMove(window, QPoint(window->width() - 2, 2), 20);
        QTRY_VERIFY(!firstRing->isVisible());
        QVERIFY(!unreadRing->isVisible());
        QTest::keyClick(window, Qt::Key_Right);
        QTRY_VERIFY(read->hasActiveFocus());
        QTRY_VERIFY(readRing->isVisible());
    }

    /// A click inside the open filter panel belongs to the panel. It went through to the
    /// cover drawn underneath, and the series opened over the panel somebody was using.
    void a_click_in_the_filter_panel_does_not_open_the_series_behind_it()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        QJsonArray many;
        for (int i = 0; i < 40; ++i) {
            many << aSeries(u"s%1"_s.arg(i), u"Série %1"_s.arg(i), 3, false);
        }
        const QByteArray pageReply =
            aReply(200, QByteArrayLiteral("application/json"), aPage(many));
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
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->requestActivate();
        QTRY_VERIFY(window->isActive());
        auto *navigation =
            engine.singletonInstance<Navigation *>(qmlTypeId("Leaf", 1, 0, "Navigation"));
        QVERIFY(navigation);
        auto *grid = window->findChild<QQuickItem *>(u"shelf-grid"_s);
        QVERIFY(grid);
        QTRY_COMPARE(grid->property("count").toInt(), 40);

        QQuickItem *button = nullptr;
        QTRY_VERIFY((button = reachableFilterButton(window)));
        auto *panel = button->findChild<QObject *>(u"filter-panel"_s);
        QVERIFY(panel);
        // Green while its panel is open, and only then.
        QVERIFY(!button->property("held").toBool());
        QVERIFY(QMetaObject::invokeMethod(panel, "open"));
        QTRY_VERIFY(panel->property("opened").toBool());
        QVERIFY(button->property("held").toBool());

        // A point of the panel's own surface with a cover underneath it: its padding, near
        // the bottom edge, where no control of the panel takes the press for itself.
        auto *content = panel->property("contentItem").value<QQuickItem *>();
        QVERIFY(content);
        QQuickItem *surface = content->parentItem();
        QVERIFY(surface);
        const QPointF inside =
            surface->mapToScene(QPointF(24, surface->height() - 4));
        bool coverUnder = false;
        for (int i = 0; i < 40 && !coverUnder; ++i) {
            auto *frame = itemNamed(grid, u"cover-frame-s%1"_s.arg(i));
            if (frame && frame->isVisible()
                && frame->contains(frame->mapFromScene(inside)))
                coverUnder = true;
        }
        QVERIFY2(coverUnder, "no cover under the panel: this would test nothing");

        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, inside.toPoint());
        QTest::qWait(300);
        QCOMPARE(navigation->destination(), Navigation::Destination::Shelf);
        QVERIFY(panel->property("opened").toBool());

        // And on what the panel draws, which is where a hand actually clicks: a heading that
        // folds its axis, and a value that narrows the shelf.
        QQuickItem *page = window->contentItem();
        QQuickItem *head = itemNamed(page, u"filter-axis-head-genre"_s);
        QVERIFY(head);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          head->mapToScene(QPointF(head->width() / 2, head->height() / 2))
                              .toPoint());
        QTest::qWait(300);
        QCOMPARE(navigation->destination(), Navigation::Destination::Shelf);

        QQuickItem *rows = itemNamed(page, u"filter-axis-rows-universe"_s);
        QVERIFY(rows);
        QQuickItem *value = nullptr;
        QTRY_VERIFY((value = [rows]() -> QQuickItem * {
            for (QQuickItem *one : rows->property("contentItem").value<QQuickItem *>()
                                       ->childItems()) {
                if (one->objectName().startsWith(u"filter-value-"_s) && one->isVisible())
                    return one;
            }
            return nullptr;
        }()));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          value->mapToScene(QPointF(value->width() / 2, value->height() / 2))
                              .toPoint());
        QTest::qWait(300);
        QCOMPARE(navigation->destination(), Navigation::Destination::Shelf);

        // In a short window the panel is shorter, and still hangs under its button. Bounded
        // by the window alone, it was pushed up until it fitted, over the button that shuts it.
        QVERIFY(QMetaObject::invokeMethod(panel, "close"));
        QTRY_VERIFY(!panel->property("visible").toBool());
        window->setWindowState(Qt::WindowNoState);
        window->resize(900, 400);
        QTRY_COMPARE(window->height(), 400);
        QTRY_VERIFY((button = reachableFilterButton(window)));
        QVERIFY(QMetaObject::invokeMethod(panel, "open"));
        QTRY_VERIFY(panel->property("opened").toBool());
        const qreal sheet = content->implicitHeight()
                            + 2 * panel->property("padding").toReal();
        QVERIFY2(panel->property("height").toReal() < sheet,
                 "the panel fits whole: this would test nothing");
        const qreal buttonFoot = button->mapToScene(QPointF(0, button->height())).y();
        const qreal panelTop = surface->mapToScene(QPointF(0, 0)).y();
        const qreal panelFoot = surface->mapToScene(QPointF(0, surface->height())).y();
        QVERIFY2(panelTop >= buttonFoot,
                 qPrintable(u"the panel starts at %1, over a button ending at %2"_s
                                .arg(panelTop).arg(buttonFoot)));
        QVERIFY2(panelFoot <= window->height(),
                 qPrintable(u"the panel ends at %1, past a window %2 tall"_s
                                .arg(panelFoot).arg(window->height())));
    }

    /// A series marked read from the shelf asks for that series and nothing else. Its tile
    /// fills and checks, and the two counts it moves move by one where they stand. The shelf
    /// used to ask for its first hundred series again, and the counts for nothing at all.
    void marking_a_series_read_asks_for_that_series_and_nothing_else()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        auto readNow = std::make_shared<bool>(false);
        QJsonObject koro = aSeries(u"kq"_s, u"Koro Quest"_s, 6, true);
        koro[u"readStatus"_s] = u"UNREAD"_s;
        QJsonObject parasite = aSeries(u"pa"_s, u"Parasite"_s, 8, true);
        parasite[u"readStatus"_s] = u"UNREAD"_s;
        const QJsonObject death = aSeries(u"dn"_s, u"Death Note"_s, 12, false);
        const auto reply = [](const QByteArray &body) {
            return "HTTP/1.1 200 .\r\nContent-Type: application/json\r\nContent-Length: "
                   + QByteArray::number(body.size()) + "\r\n\r\n" + body;
        };
        const auto json = [](const QJsonObject &one) {
            return QJsonDocument(one).toJson(QJsonDocument::Compact);
        };
        pretend.answerFor = [=](const QByteArray &request) -> QByteArray {
            if (request.startsWith("PATCH /series/kq/progress")) {
                *readNow = true;
                return reply("{}");
            }
            if (request.startsWith("GET /series/kq ")) {
                QJsonObject now = koro;
                now[u"readStatus"_s] = *readNow ? u"READ"_s : u"UNREAD"_s;
                now[u"readEntries"_s] = *readNow ? 6 : 0;
                return reply(json(now));
            }
            if (request.startsWith("GET /filters"))
                return reply(someFilters(2, 1));
            if (request.startsWith("GET /series?"))
                return reply(aPage({death, koro, parasite}));
            return "HTTP/1.1 404 .\r\nContent-Length: 0\r\n\r\n";
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *grid = window->findChild<QQuickItem *>(u"shelf-grid"_s);
        auto *pills = window->findChild<QQuickItem *>(u"filter-pills"_s);
        QVERIFY(grid);
        QVERIFY(pills);
        QTRY_COMPARE(grid->property("count").toInt(), 3);
        const auto said = [&](const QString &name) {
            const QQuickItem *text = itemNamed(pills, name);
            return text == nullptr ? QString() : text->property("text").toString();
        };
        QTRY_COMPARE(said(u"chip-READ-text"_s), u"Terminées 1"_s);
        QQuickItem *tile = nullptr;
        QTRY_VERIFY((tile = itemNamed(grid, u"tile-kq"_s)));
        QVERIFY(!itemNamed(tile, u"finished-kq"_s)->isVisible());

        pretend.heard.clear();
        auto *commands =
            engine.singletonInstance<Commands *>(qmlTypeId("Leaf", 1, 0, "Commands"));
        QVERIFY(commands);
        commands->markSeries(u"kq"_s, true);

        QTRY_VERIFY(itemNamed(tile, u"finished-kq"_s)->isVisible());
        QTRY_COMPARE(said(u"chip-READ-text"_s), u"Terminées 2"_s);
        QCOMPARE(said(u"chip-UNREAD-text"_s), u"Non lues 1"_s);
        // Long enough for anything else to have gone out.
        QTest::qWait(300);
        const QByteArray &heard = pretend.heard;
        QVERIFY(heard.contains("PATCH /series/kq/progress"));
        QCOMPARE(heard.count("GET /series/kq "), 1);
        QVERIFY2(!heard.contains("GET /series?"), heard.constData());
        QVERIFY2(!heard.contains("GET /filters"), heard.constData());
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
            QJsonObject hit{
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
            // The last is a chapter whose title is markup: the files' words, shown as written.
            if (number == 4) {
                hit[u"kind"_s] = u"CHAPTER"_s;
                hit[u"label"_s] = u"<b>x</b>"_s;
                hit[u"chapterLabel"_s] = u"Chap.\u00A031"_s;
                hit[u"chapterTitle"_s] = u"<b>x</b>"_s;
            }
            foundFiles << hit;
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
        Boot::run(engine, *qGuiApp, nowhere());
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

        // A chapter is named as the reader names it, and its title — markup here — is drawn
        // as the characters it is, on both lines of the row.
        QQuickItem *chapterRow = nullptr;
        QTRY_VERIFY((chapterRow = itemNamed(results, u"search-result-pd-v4"_s)));
        auto *chapterName = itemNamed(chapterRow, u"search-name-pd-v4"_s);
        auto *chapterContext = itemNamed(chapterRow, u"search-context-pd-v4"_s);
        QVERIFY(chapterName);
        QVERIFY(chapterContext);
        QCOMPARE(chapterName->property("text").toString(),
                 u"Chap.\u00A031\u00A0: <b>x</b>"_s);
        QCOMPARE(chapterName->property("textFormat").toInt(), int(Qt::PlainText));
        QCOMPARE(chapterContext->property("textFormat").toInt(), int(Qt::PlainText));

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
        Boot::run(engine, *qGuiApp, nowhere());
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
        Boot::run(engine, *qGuiApp, nowhere());
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
            // Gone, and not only closing. `opened` turns false when the exit transition
            // *starts*, and since Qt 6.10 an `open()` during those 80 ms is ignored: the next
            // option was asked for on a menu that never came back.
            QTRY_VERIFY(!menu->property("visible").toBool());
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
        Boot::run(engine, *qGuiApp, nowhere());
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
        Boot::run(engine, *qGuiApp, nowhere());
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
        Boot::run(engine, *qGuiApp, nowhere());
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
        Boot::run(engine, *qGuiApp, nowhere());
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
        // Seated as the principal, the one profile with a library tab to open.
        pretend.answerFor = seatedAsPrincipal([pageReply, emptyNext, healthReply, scanReply,
                                               filtersReply, coverReply](const QByteArray &request) {
            if (request.startsWith("GET /next"))
                return emptyNext;
            if (request.startsWith("GET /health"))
                return healthReply;
            if (request.startsWith("GET /scan"))
                return scanReply;
            if (request.startsWith("GET /filters"))
                return filtersReply;
            return request.startsWith("GET /series?") ? pageReply : coverReply;
        });
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
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

        // The scan lives behind the library's pill, and what it counted is said in French
        // here rather than arriving as a paragraph the vocabulary file could not reach.
        QTRY_VERIFY(itemNamed(page, u"settings-tab-library"_s));
        QVariant library;
        QVERIFY(QMetaObject::invokeMethod(view, "sectionIndex", Q_RETURN_ARG(QVariant, library),
                                          Q_ARG(QVariant, u"library"_s)));
        QVERIFY(QMetaObject::invokeMethod(itemNamed(page, u"settings-tabs"_s), "selected",
                                          Q_ARG(int, library.toInt())));
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
        Boot::run(engine, *qGuiApp, nowhere());
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
        Boot::run(engine, *qGuiApp, nowhere());
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
        Boot::run(engine, *qGuiApp, nowhere());
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
                     u"Tome 12 · Page 48/190 · Chap.\u00A098\u00A0: <b>x</b> & y"_s);

        window->setWidth(800);
        QTRY_COMPARE(where->property("text").toString(), u"T12 · Page 48/190"_s);
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
        Boot::run(engine, *qGuiApp, nowhere());

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
        Boot::run(engine, *qGuiApp, nowhere());

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
        Boot::run(engine, *qGuiApp, nowhere());

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

    /// No address: the shelf says so in its card, and the band above it said the same
    /// sentence again — seen twice on one screen in the test of 22/10/2026. Said once.
    void a_missing_address_is_said_once_and_not_twice()
    {
        qunsetenv("LEAF_ADDRESS");
        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());

        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *trouble = window->findChild<QQuickItem *>(u"shelf-trouble"_s);
        auto *said = window->findChild<QQuickItem *>(u"shelf-trouble-text"_s);
        auto *band = window->findChild<QQuickItem *>(u"resume-band"_s);
        QVERIFY(trouble);
        QVERIFY(said);
        QVERIFY(band);
        QTRY_VERIFY(trouble->isVisible());
        QCOMPARE(said->property("text").toString(),
                 Words::noAddress(Settings::configurationFile()));
        auto *echo = itemNamed(band, u"resume-trouble"_s);
        QVERIFY(echo);
        QVERIFY(!band->isVisible());
        QVERIFY(!echo->isVisible());
    }

    /// `Main.qml` has exactly one line that hands a value only QML knows to a C++ singleton:
    /// `Component.onCompleted: Widths.window = window.width`. Nothing in the C++ tests on
    /// `Widths` can see whether that binding still exists.
    void the_window_hands_its_own_width_to_widths()
    {
        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());

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
        Boot::run(engine, *qGuiApp, nowhere());

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
        Boot::run(engine, *qGuiApp, nowhere());
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

    /// The import's first phase, which is the only one a reader reaches without a server
    /// answering: the box, the two pickers, and the one answer that has to be given before
    /// anything is chosen.
    ///
    /// Checked here and not in `holds_the_imports` because the cost is the point: the
    /// checksums are computed while the manifest is built, so a reader who says it after
    /// dropping says it too late. That is a fact about where the checkbox sits on the
    /// screen, and only a test that looks at the screen can hold it.
    void what_is_verified_is_answered_before_anything_is_chosen()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answers(200, QByteArrayLiteral(R"({"items":[],"total":0})"));
        // Seated, so that the bar offers the import at all.
        pretend.answerFor = seatedAsPrincipal(
            [answer = pretend.answer](const QByteArray &) { return answer; });
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->requestActivate();
        QTRY_VERIFY(window->isActive());

        auto *imports =
            engine.singletonInstance<Imports *>(qmlTypeId("Leaf", 1, 0, "Imports"));
        auto *dialog = window->findChild<QObject *>(u"import-dialog"_s);
        auto *button = window->findChild<QQuickItem *>(u"import-button"_s);
        QVERIFY(imports);
        QVERIFY(dialog);
        QVERIFY(button);
        QVERIFY(!dialog->property("visible").toBool());
        // Offered once the server has said who is seated, and laid out in the row after that.
        QTRY_VERIFY(button->isVisible() && button->width() > 0);

        const QPointF at = button->mapToItem(nullptr, QPointF(button->width() / 2,
                                                              button->height() / 2));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, at.toPoint());
        QTRY_VERIFY(dialog->property("visible").toBool());

        auto *check = window->findChild<QQuickItem *>(u"import-verify"_s);
        auto *chooseFiles = window->findChild<QQuickItem *>(u"import-choose-files"_s);
        auto *chooseFolder = window->findChild<QQuickItem *>(u"import-choose-folder"_s);
        QVERIFY(check);
        QVERIFY(chooseFiles);
        QVERIFY(chooseFolder);
        QVERIFY(check->isVisible());
        QVERIFY(chooseFiles->isVisible());
        QVERIFY(chooseFolder->isVisible());

        // On by default, and off by one click — a verified transfer is what a reader gets
        // without knowing the option is there, and the cost is theirs to refuse.
        QVERIFY(imports->verifying());
        QVERIFY(check->property("checked").toBool());
        const QPointF box = check->mapToItem(nullptr, QPointF(8, 8));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, box.toPoint());
        QTRY_VERIFY(!imports->verifying());
        QVERIFY(!check->property("checked").toBool());
    }

    /// A folder's card shows the tree of what it holds — a root node named after the
    /// folder itself — and goes no further until somebody has said yes. The sentence a
    /// later task will place on that tree is checked directly here, since this step does
    /// not yet draw it. Creating a universe is the one thing a reader cannot undo by
    /// deleting a file.
    void a_folder_shows_its_tree_before_it_creates_anything()
    {
        QTemporaryDir library;
        QVERIFY(library.isValid());
        QVERIFY(QDir().mkpath(library.filePath(u"Koro"_s)));
        QFile volume(library.filePath(u"Koro/Tome 1.cbz"_s));
        QVERIFY(volume.open(QIODevice::WriteOnly));
        volume.write(QByteArray(64, 'a'));
        volume.close();

        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) -> QByteArray {
            if (request.startsWith("POST /import")) {
                return aReply(200, QByteArrayLiteral("application/json"),
                              QByteArrayLiteral(
                                  R"({"id":"imp_1","root":"Koro",)"
                                  R"("creates":[{"kind":"WORK","name":"Koro Quest",)"
                                  R"("at":""}],"toSend":["Tome 1.cbz"],)"
                                  R"("alreadyThere":[],"bytesToSend":64})"));
            }
            return aReply(200, QByteArrayLiteral("application/json"),
                          QByteArrayLiteral(R"({"items":[],"total":0})"));
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));

        auto *imports =
            engine.singletonInstance<Imports *>(qmlTypeId("Leaf", 1, 0, "Imports"));
        auto *importCaptions = engine.singletonInstance<ImportCaptions *>(
            qmlTypeId("Leaf", 1, 0, "ImportCaptions"));
        // Two caption objects and not one since the dialog's own words were told apart from
        // what a card says about itself, and the tree reads every one of its lines off this
        // second one: a `CardCaptions` the engine failed to resolve would leave the nodes
        // blank on screen while every C++ test on `Words` still passed.
        auto *cardCaptions = engine.singletonInstance<CardCaptions *>(
            qmlTypeId("Leaf", 1, 0, "CardCaptions"));
        auto *dialog = window->findChild<QObject *>(u"import-dialog"_s);
        QVERIFY(imports);
        QVERIFY(importCaptions);
        QVERIFY(cardCaptions);
        QVERIFY(dialog);
        dialog->setProperty("visible", true);
        QTRY_VERIFY(dialog->property("visible").toBool());

        // Through the dialog's own `take`, rather than by reaching past it: turning a URL
        // into a path is the step that decides whether the phase moves at all.
        const QVariantList dropped{QUrl::fromLocalFile(library.filePath(u"Koro"_s))};
        QVERIFY(QMetaObject::invokeMethod(dialog, "take",
                                          Q_ARG(QVariant, QVariant(dropped))));
        QTRY_COMPARE(dialog->property("phase").toInt(),
                     dialog->property("proposing").toInt());
        QTRY_COMPARE(imports->rowCount({}), 1);

        // Asked for until it is there: the model has its row before the view has built the
        // delegate that draws it. And walked down the view rather than asked of the window: a delegate has no QObject
        // parent — the delegate model owns it — so `findChild` goes straight past every
        // row the list ever draws and finds the list itself, looking like an empty queue.
        auto *rows = window->findChild<QQuickItem *>(u"import-rows"_s);
        QVERIFY(rows);

        // Looked up again on every poll rather than captured once and read back later: the
        // tree rebuilds its delegates whenever the row's `nodes` role changes, and a
        // `QQuickItem *` held across a further `QTRY_*` is exactly the SIGSEGV `textNamed`'s
        // own note below already names — this block used to hold `root`, `rootName` and
        // `accept` that way, and once the card's header stopped repeating the tree's own
        // name (fewer, differently-timed re-layouts of the same row) it started losing that
        // race on nearly every run instead of rarely.
        //
        // `accept` visible is waited for **before** either text is read, not after: it is
        // what guarantees the server's answer — `creates`, and so the state a node's own
        // line names — has actually landed, and reading a node's `state` any earlier is
        // reading it while it is still empty, which is a real failure and not a flake.
        QTRY_VERIFY(itemNamed(rows, u"import-row-Koro-node-root"_s)
                    && itemNamed(rows, u"import-row-Koro-node-root"_s)->isVisible());
        QTRY_VERIFY(itemNamed(rows, u"import-row-Koro-accept"_s)
                    && itemNamed(rows, u"import-row-Koro-accept"_s)->isVisible());
        QCOMPARE(textNamed(rows, u"import-row-Koro-node-root-name"_s), u"Koro"_s);

        // The card's own header does not say the name a second time: it is the tree's root
        // node, and the spec is explicit that no header repeats what that row already says.
        QQuickItem *header = itemNamed(rows, u"import-row-Koro-name"_s);
        QVERIFY2(!header || !header->isVisible(), "the folder's name is drawn twice");

        // The tree says what a node becomes, and not only what it is: the level, the
        // server's own answer and what it weighs, joined on one line under one separator.
        // `CardCaptions::nodeLine` is what does the joining — proved alone in
        // `writes_french.cpp` — and this is where it is proved to actually reach the screen,
        // which a model-only test cannot show.
        QCOMPARE(textNamed(rows, u"import-row-Koro-node-root-says"_s),
                 u"Série · sera créée · 1 tome · 64 o"_s);

        // Closing the window decides nothing, and this is asserted **while the card is
        // still being prepared** — which is the only moment it proves anything, since
        // `giveUpPreparing` never touches a transfer that has started. The cross used to
        // call it, so closing during a verification threw away the reading of a whole
        // library with nothing saying that was what it meant. Giving up is what
        // « Revenir en arrière » and « Abandonner » are for, and both say so.
        QQuickItem *cross = itemNamed(window->contentItem(), u"import-close"_s);
        QVERIFY(cross);
        QMetaObject::invokeMethod(cross, "triggered");
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QCOMPARE(imports->rowCount({}), 1);
        dialog->setProperty("visible", true);
        QTRY_VERIFY(dialog->property("visible").toBool());

        // And the card carries the one command that can stop what is moving. Asked on the
        // screen and not of the model: `Pause` is a `visible` binding, and a binding that
        // silently evaluates to false is exactly what no model test can see.
        imports->accept(0);
        imports->send();
        QTRY_COMPARE(imports->data(imports->index(0), int(Imports::Role::Stage_)).toInt(),
                     int(Imports::Stage::Sending));
        QTRY_VERIFY2(itemNamed(rows, u"import-row-Koro-pause"_s)
                         && itemNamed(rows, u"import-row-Koro-pause"_s)->isVisible(),
                     "nothing on the card could stop a transfer that had started");

        // The word is not in the QML: the elision in front of a vowel is not a rule a
        // binding could apply, and « créera le UNIVERSE » is what leaving it there looks
        // like.
        QCOMPARE(cardCaptions->willCreateLabel(u"UNIVERSE"_s, u"Terres d’Arran"_s),
                 u"créera l’univers « Terres d’Arran »"_s);

        // The path the server sent, cut down to the folder a reader recognises. It is the
        // one caption that does work of its own rather than handing `Words` its arguments,
        // and the cutting is the point: a card already crowded with words gains nothing
        // from « /srv/leaf/library/Mangas » where « Mangas » says it.
        QCOMPARE(cardCaptions->alreadyElsewhereLabel(u"Elfes"_s,
                                                     u"/srv/leaf/library/Mangas/Elfes"_s),
                 Words::alreadyElsewhere(u"Elfes"_s, u"Mangas"_s));
        // And the two a row asks about its own numbers, proved to reach `Words` at all:
        // they are what the stage badge and the concern list under a card are made of.
        QCOMPARE(cardCaptions->tryingAgainIn(8), Words::tryingAgainIn(8));
        QCOMPARE(cardCaptions->concern(u"page manquante"_s),
                 Words::concern(u"page manquante"_s));

        QQuickItem *accept = itemNamed(rows, u"import-row-Koro-accept"_s);
        QVERIFY(accept);
        const QPointF on = accept->mapToItem(nullptr, QPointF(accept->width() / 2,
                                                              accept->height() / 2));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, on.toPoint());
        // Accepting is the only answer this card asks for now that the scope question is
        // gone, so there is nothing left to check once the accept action itself is hidden.
        // Looked up fresh once more, for the same reason as above.
        QTRY_VERIFY(!itemNamed(rows, u"import-row-Koro-accept"_s)
                    || !itemNamed(rows, u"import-row-Koro-accept"_s)->isVisible());
    }

    /// A line of the series page opens the book where reading stopped, over the whole
    /// window: the application's bar goes, because a reader at rest sees nothing but pages.
    void a_volume_clicked_on_the_series_page_opens_the_reader_at_its_page()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *navigation =
            engine.singletonInstance<Navigation *>(qmlTypeId("Leaf", 1, 0, "Navigation"));
        auto *reader = engine.singletonInstance<Reader *>(qmlTypeId("Leaf", 1, 0, "Reader"));
        QVERIFY(navigation && reader);

        QVERIFY(navigation->open(Navigation::Destination::Series, {{u"series"_s, u"s1"_s}}));
        auto *volumes = engine.singletonInstance<Entries *>(qmlTypeId("Leaf", 1, 0, "Entries"));
        QTRY_VERIFY(volumes->count() == 1 && !volumes->loading());
        QQuickItem *row = nullptr;
        QTRY_VERIFY((row = itemNamed(window->contentItem(), u"volume-e12"_s))
                    && row->isVisible() && row->width() > 0);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          row->mapToScene(QPointF(row->width() / 2.0, row->height() / 2.0))
                              .toPoint());

        QTRY_COMPARE(navigation->destination(), Navigation::Destination::Reader);
        QCOMPARE(navigation->parameters().value(u"entry"_s).toString(), u"e12"_s);
        QQuickItem *view = nullptr;
        QTRY_VERIFY((view = itemNamed(window->contentItem(), u"reader-view"_s)));
        // Heard from the moment the view is made: the bars it opens with are sent away by this
        // timer, and what the test checks is that it did so, not that a page arrived within
        // its three seconds — under a loaded suite the page once took longer, and the bars
        // were already gone when they were looked for.
        QObject *welcome = view->findChild<QObject *>(u"reader-welcome"_s);
        QVERIFY(welcome);
        QSignalSpy welcomed(welcome, SIGNAL(triggered()));
        QTRY_VERIFY(!reader->loading() && reader->pageCount() == 190);
        // Page 46 is on screen, alone or in its pair.
        QVERIFY2(reader->firstPage() == 46 || reader->secondPage() == 46,
                 qPrintable(u"first %1, second %2"_s.arg(reader->firstPage())
                                .arg(reader->secondPage())));
        QQuickItem *page = nullptr;
        QTRY_VERIFY((page = itemNamed(view, u"reader-page-46"_s)) && page->isVisible()
                    && page->width() > 0);
        // The whole window is the reader's.
        QCOMPARE(view->height(), qreal(window->height()));
        QVERIFY(!window->findChild<QQuickItem *>(u"app-bar"_s)->isVisible());
        // A volume opens with its bars up, and after three seconds without input there is
        // nothing but the pages (the test of 22/10/2026 found a bare page, and nothing
        // saying the commands were a click away).
        QQuickItem *bar = itemNamed(view, u"reader-bar"_s);
        QQuickItem *dock = itemNamed(view, u"reader-dock"_s);
        QVERIFY(bar && dock);
        QCOMPARE(welcome->property("interval").toInt(), 3000);
        // Up while the welcome holds them, if it still does; whether it does depends on how long
        // the page took, which is not what is under test.
        if (welcomed.count() == 0) {
            QVERIFY(view->property("barsShown").toBool());
            QTRY_VERIFY((bar->isVisible() && dock->isVisible()) || welcomed.count() > 0);
        }
        QTRY_VERIFY_WITH_TIMEOUT(!bar->isVisible() && !dock->isVisible(), 8000);
        // Sent away by the welcome, which only runs for bars a volume opened with.
        QCOMPARE(welcomed.count(), 1);
        // The image is asked at the width the list was asked at.
        QTRY_VERIFY(pretend.heard.contains(
            "GET /entries/e12/pages/46?width=" + QByteArray::number(reader->askedWidth())));
    }

    /// In a manga the page read next lies to the left, so the left third goes forward —
    /// the physical side, not the word « next ».
    void in_a_manga_the_left_third_turns_forward()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        QString direction = u"RIGHT_TO_LEFT"_s;
        pretend.answerFor = [&direction](const QByteArray &request) {
            return aBookAnswers(request, direction);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        Reader *reader = readerOpenOnTheBook(engine);
        QVERIFY(reader);
        QQuickItem *view = nullptr;
        QTRY_VERIFY((view = itemNamed(window->contentItem(), u"reader-view"_s)));
        QTRY_VERIFY(!reader->loading() && reader->pageCount() == 190);

        const int before = reader->display();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, across(view, 0.10));
        QTRY_COMPARE(reader->display(), before + 1);
        // What lies ahead of the new screen is fetched before it is needed — each turn, and
        // not only once: the list is asked of `Reader` again whenever the screen changes.
        const QStringList ahead = reader->ahead(reader->askedWidth());
        QVERIFY(!ahead.isEmpty());
        for (const QString &one : ahead) {
            const QByteArray path = QUrl(one).path(QUrl::FullyEncoded).toUtf8() + "?width="
                                    + QByteArray::number(reader->askedWidth());
            QTRY_VERIFY2(pretend.heard.contains("GET " + path), path.constData());
        }
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, across(view, 0.90));
        QTRY_COMPARE(reader->display(), before);
        // And the arrow on the same side as the third.
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));
        QTest::keyClick(window, Qt::Key_Left);
        QTRY_COMPARE(reader->display(), before + 1);
        // The wheel goes down to go on, whatever the side.
        wheel(window, across(view, 0.5), QPoint(0, -120));
        QTRY_COMPARE(reader->display(), before + 2);

        // Left to right, the same third goes back.
        direction = u"LEFT_TO_RIGHT"_s;
        auto *navigation =
            engine.singletonInstance<Navigation *>(qmlTypeId("Leaf", 1, 0, "Navigation"));
        navigation->back();
        QTRY_COMPARE(navigation->destination(), Navigation::Destination::Series);
        navigation->open(Navigation::Destination::Reader,
                         {{u"series"_s, u"s1"_s}, {u"entry"_s, u"e12"_s}});
        QTRY_VERIFY((view = itemNamed(window->contentItem(), u"reader-view"_s)));
        QTRY_VERIFY(!reader->loading() && reader->direction() == int(Reader::Direction::LeftToRight));
        const int then = reader->display();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, across(view, 0.10));
        QTRY_COMPARE(reader->display(), then - 1);
    }

    /// A webtoon is one strip: the side thirds turn nothing, the wheel and the space bar move
    /// it, and the page it records is the one that crosses the middle — never a pair.
    void a_webtoon_scrolls_and_never_pairs_pages()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"VERTICAL"_s, nullptr, u"webtoon"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        Reader *reader = readerOpenOnTheBook(engine);
        QVERIFY(reader);
        QQuickItem *view = nullptr;
        QTRY_VERIFY((view = itemNamed(window->contentItem(), u"reader-view"_s)));
        QTRY_VERIFY(!reader->loading() && reader->pageCount() == 190);
        QVERIFY(reader->webtoon());
        QQuickItem *strip = nullptr;
        QTRY_VERIFY((strip = itemNamed(view, u"reader-strip"_s)) && strip->isVisible());
        QObject *glide = stripGlide(strip);
        QVERIFY2(glide, "no object named strip-glide under the strip: the wait would be empty");
        // The recorded page opens at the top of the view, one page and never two.
        QTRY_COMPARE(stripPageAt(strip, strip->property("contentY").toReal() + 1), 46);
        QCOMPARE(reader->firstPage(), 46);
        QCOMPARE(reader->secondPage(), -1);
        // No page lies to one side of the first: the pages run edge to edge, and the strip
        // is as wide as the window.
        QCOMPARE(strip->width(), view->width());
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));

        QVERIFY(atRest(view));
        // The thirds turn nothing in a strip.
        const qreal top = strip->property("contentY").toReal();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, across(view, 0.10));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, across(view, 0.90));
        QTest::qWait(100);
        QCOMPARE(reader->firstPage(), 46);
        QCOMPARE(strip->property("contentY").toReal(), top);
        QVERIFY(!view->property("barsShown").toBool());

        // The wheel moves it, and the page recorded follows what crosses the middle.
        wheel(window, across(view, 0.5), QPoint(0, -120));
        QTRY_VERIFY(strip->property("contentY").toReal() > top);
        QTRY_VERIFY(!strip->property("moving").toBool());
        const qreal wheeled = strip->property("contentY").toReal();
        QTRY_COMPARE(reader->firstPage(),
                     stripPageAt(strip, wheeled + strip->height() / 2));
        QCOMPARE(reader->secondPage(), -1);

        // The space bar moves it by most of a screen, and Shift+Space back by the same.
        QTest::keyClick(window, Qt::Key_Space);
        QTRY_VERIFY(strip->property("contentY").toReal() > wheeled + strip->height() * 0.8);
        QTRY_VERIFY(!glide->property("running").toBool());
        const qreal paged = strip->property("contentY").toReal();
        QVERIFY(paged < wheeled + strip->height());
        QTest::keyClick(window, Qt::Key_Space, Qt::ShiftModifier);
        QTRY_VERIFY(strip->property("contentY").toReal() < paged - strip->height() * 0.8);
        QTRY_VERIFY(!glide->property("running").toBool());
        QTRY_COMPARE(reader->firstPage(),
                     stripPageAt(strip, strip->property("contentY").toReal()
                                            + strip->height() / 2));

        // The middle still calls the bars.
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, across(view, 0.5));
        QTRY_VERIFY(view->property("barsShown").toBool());

        // And the scrub still goes where it is told: the strip follows the page it is sent
        // to, and does not send it back.
        reader->goToPage(120);
        QTRY_COMPARE(stripPageAt(strip, strip->property("contentY").toReal() + 1), 120);
        QTRY_VERIFY(!strip->property("moving").toBool());
        QCOMPARE(reader->firstPage(), 120);
    }

    /// The bottom of the strip is the last page shown, even when that page is a sliver that
    /// never crosses the middle: the volume is finished the moment its end is reached.
    void reaching_the_bottom_of_a_webtoon_says_it_is_finished()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"VERTICAL"_s, nullptr, u"webtoon"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        Reader *reader = readerOpenOnTheBook(engine);
        QVERIFY(reader);
        QQuickItem *view = nullptr;
        QTRY_VERIFY((view = itemNamed(window->contentItem(), u"reader-view"_s)));
        QTRY_VERIFY(!reader->loading() && reader->pageCount() == 190);
        QQuickItem *strip = nullptr;
        QTRY_VERIFY((strip = itemNamed(view, u"reader-strip"_s)) && strip->isVisible());
        QTRY_COMPARE(stripPageAt(strip, strip->property("contentY").toReal() + 1), 46);

        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));
        QTest::keyClick(window, Qt::Key_End);
        QTRY_VERIFY(strip->property("atYEnd").toBool());
        QTRY_COMPARE(reader->firstPage(), 189);
        QTRY_VERIFY2(pretend.heard.contains("\"finished\":true"), pretend.heard.constData());
    }

    /// Long names are elided where a line has no room for more, and wrapped where there is
    /// room below, and never cut: the owner asked on 22/10/2026 to see it with a long series
    /// and a long chapter, on a small window and on his own.
    void long_names_are_elided_or_wrapped_never_cut()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        BookExtras extras;
        extras.seriesName = u"Les Chroniques de la Lune Noire et de tout ce qui s’ensuivit "
                            u"au royaume de Lhynn quand l’Empereur Haghendorf revint d’exil, "
                            u"édition intégrale revue, corrigée et augmentée de ses annexes"_s;
        extras.volume = QJsonObject{
            {u"title"_s, u"Le temps des examens, des adieux et des retrouvailles au bord de "
                         u"la mer, quand tout le monde est parti"_s}};
        extras.chapters = QJsonArray{
            QJsonObject{{u"id"_s, u"c98"_s}, {u"label"_s, u"Chap.098"_s}, {u"number"_s, 98},
                        {u"title"_s, u"Où l’on apprend enfin pourquoi la classe entière avait "
                                     u"décidé de se taire pendant trois semaines, et ce que "
                                     u"le professeur fit de ce silence le jour de l’examen "
                                     u"blanc, sous la pluie, devant le lycée tout entier"_s},
                        {u"kind"_s, u"CHAPTER"_s}, {u"entryId"_s, u"e12"_s},
                        {u"startPage"_s, 0}},
        };
        pretend.answerFor = [&extras](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s, nullptr, u"manga"_s, nullptr,
                                nullptr, &extras);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();
        const auto putBack = qScopeGuard([&] { window->resize(1100, 760); });
        QTRY_VERIFY(reader->chapters().size() == 1);
        QQuickItem *panel = itemNamed(view, u"reader-volume"_s);
        QQuickItem *title = itemNamed(view, u"reader-title"_s);
        QQuickItem *where = itemNamed(view, u"reader-where"_s);
        QQuickItem *head = itemNamed(view, u"reader-volume-head"_s);
        QQuickItem *series = itemNamed(view, u"reader-volume-series"_s);
        QQuickItem *chapter = itemNamed(view, u"reader-volume-chapter-0-text"_s);
        QQuickItem *row = itemNamed(view, u"reader-volume-chapter-0"_s);
        QVERIFY(panel && title && where && head && series && chapter && row);
        QTRY_COMPARE(title->property("text").toString(), extras.seriesName);

        for (const AWindow &at : {AWindow{900, 600}, AWindow{1920, 1200}}) {
            QVERIFY(laidOutFor(view, at));
            const QString said = u"%1 × %2"_s.arg(at.width).arg(at.height);
            const QRectF drawn = panel->mapRectToScene(panel->boundingRect());
            // One line each, elided when it does not fit — which on the small window none of
            // them does: the bar's two lines and the card's title.
            for (QQuickItem *line : {title, where, head}) {
                const QString which = said + u": "_s + line->objectName();
                QTRY_VERIFY2(line->property("truncated").toBool()
                                 || line->implicitWidth() <= line->width(),
                             qPrintable(which));
                if (at.width == 900)
                    QVERIFY2(line->property("truncated").toBool(), qPrintable(which));
                QVERIFY2(placed(line).right() <= at.width, qPrintable(which));
            }
            // Wrapped where there is room below, inside the card and inside its row.
            QTRY_VERIFY2(series->property("lineCount").toInt() > 1, qPrintable(said));
            QVERIFY2(chapter->property("lineCount").toInt() > 1, qPrintable(said));
            QVERIFY2(placed(series).right() <= drawn.right() + 0.5, qPrintable(said));
            QVERIFY2(placed(chapter).right() <= drawn.right() + 0.5, qPrintable(said));
            QVERIFY2(row->height() >= chapter->height(), qPrintable(said));
        }
    }

    /// The end card follows the window as the dock does; see `aCardThatDoesNotFollow`.
    void the_end_card_follows_the_window_as_the_dock_does()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        const auto putBack = qScopeGuard([&] { view->window()->resize(1100, 760); });

        QString wrong = aCardThatDoesNotFollow(view, u"reader-end-card"_s, u"reader-end-done"_s,
                                               380, 14, Scale::bodyFor, 1, 32);
        QVERIFY2(wrong.isEmpty(), qPrintable(wrong));
        wrong = aCardThatDoesNotFollow(view, u"reader-end-card"_s, u"reader-end-title"_s, 380,
                                       20, Scale::titleFor, 1, 32);
        QVERIFY2(wrong.isEmpty(), qPrintable(wrong));
    }

    /// Past the last page the book is not cut off: a card says what was finished and offers
    /// what follows, over the last page, which stays there under a veil.
    void past_the_last_page_a_card_offers_the_next_volume()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        BookExtras extras;
        extras.followed = true;
        pretend.answerFor = [&extras](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s, nullptr, u"manga"_s, nullptr,
                                nullptr, &extras);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        Reader *reader = readerOpenOnTheBook(engine);
        QVERIFY(reader);
        QQuickItem *view = nullptr;
        QTRY_VERIFY((view = itemNamed(window->contentItem(), u"reader-view"_s)));
        QTRY_VERIFY(!reader->loading() && reader->pageCount() == 190);
        QQuickItem *panel = itemNamed(view, u"reader-volume"_s);
        QVERIFY(panel);
        QQuickItem *settings = itemNamed(view, u"reader-settings"_s);
        QVERIFY(settings);
        QQuickItem *hidden = itemNamed(view, u"reader-end"_s);
        QVERIFY(hidden);
        QVERIFY(!hidden->isVisible());
        // A panel left open is shut by the card: one thing at a time beside the pages.
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));
        QTest::keyClick(window, Qt::Key_I);
        QTRY_VERIFY(panel->isVisible());

        QQuickItem *card = raiseTheEndCard(window, view, reader);
        QVERIFY(card);
        QTRY_VERIFY(!panel->isVisible());
        QVERIFY(!settings->isVisible());
        QCOMPARE(textNamed(card, u"reader-end-done"_s), u"Tome 12 terminé"_s);
        QTRY_COMPARE(textNamed(card, u"reader-end-title"_s), u"Tome 13"_s);
        QCOMPARE(itemNamed(card, u"reader-end-go"_s)->property("label").toString(),
                 u"Lire le tome 13"_s);
        QVERIFY(itemNamed(card, u"reader-end-back"_s)->isVisible());
        QVERIFY(itemNamed(card, u"reader-end-stay"_s)->isVisible());
        // The cover was fetched ahead three pages from the end, and the card draws that one:
        // asked once. Fetched with another fill than the card's, it was asked twice.
        QTRY_VERIFY(pretend.heard.contains("GET /entries/e13/cover "));
        QVERIFY(aFrameDrawn(window));
        QCOMPARE(pretend.heard.count("GET /entries/e13/cover "), 1);
        QVERIFY(itemNamed(card, u"reader-end-title"_s)->isVisible());
        QVERIFY(itemNamed(card, u"reader-end-go"_s)->isVisible());
        // Centred on the pages, and still the last page behind it.
        QQuickItem *face = itemNamed(card, u"reader-end-card"_s);
        QVERIFY(face);
        QVERIFY(qAbs(face->mapToScene(QPointF(face->width() / 2, 0)).x() - view->width() / 2)
                <= 2);
        QVERIFY(qAbs(face->mapToScene(QPointF(0, face->height() / 2)).y() - view->height() / 2)
                <= 2);
        QCOMPARE(reader->firstPage(), 189);

        // « Rester sur cette page » closes it and keeps the page.
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          centreOf(itemNamed(card, u"reader-end-stay"_s)));
        QTRY_VERIFY(!card->isVisible());
        QVERIFY(!reader->atTheEnd());
        QCOMPARE(reader->firstPage(), 189);

        // The button opens what follows.
        QVERIFY(raiseTheEndCard(window, view, reader));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          centreOf(itemNamed(card, u"reader-end-go"_s)));
        QTRY_COMPARE(reader->entryId(), u"e13"_s);
    }

    /// A turn forward is what the button is: the same Space that brought the card opens
    /// the volume.
    void turning_again_opens_it()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        BookExtras extras;
        extras.followed = true;
        pretend.answerFor = [&extras](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s, nullptr, u"manga"_s, nullptr,
                                nullptr, &extras);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        Reader *reader = readerOpenOnTheBook(engine);
        QVERIFY(reader);
        QQuickItem *view = nullptr;
        QTRY_VERIFY((view = itemNamed(window->contentItem(), u"reader-view"_s)));
        QTRY_VERIFY(!reader->loading() && reader->pageCount() == 190);
        QQuickItem *card = raiseTheEndCard(window, view, reader);
        QVERIFY(card);
        // The way on arrives a moment after the card does.
        QTRY_VERIFY(!reader->next().isEmpty());

        QTest::keyClick(window, Qt::Key_Space);
        QTRY_COMPARE(reader->entryId(), u"e13"_s);
        QVERIFY(!reader->atTheEnd());
    }

    /// Enter on the end card is its button: what follows opens, as a turn forward would.
    void enter_on_the_end_card_opens_what_follows()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        BookExtras extras;
        extras.followed = true;
        pretend.answerFor = [&extras](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s, nullptr, u"manga"_s, nullptr,
                                nullptr, &extras);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        Reader *reader = readerOpenOnTheBook(engine);
        QVERIFY(reader);
        QQuickItem *view = nullptr;
        QTRY_VERIFY((view = itemNamed(window->contentItem(), u"reader-view"_s)));
        QTRY_VERIFY(!reader->loading() && reader->pageCount() == 190);
        QVERIFY(raiseTheEndCard(window, view, reader));
        QTRY_VERIFY(!reader->next().isEmpty());

        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(reader->entryId(), u"e13"_s);
        QVERIFY(!reader->atTheEnd());
    }

    /// The back button on the first page opens the volume before, at its last page, and in
    /// the first volume of the series going back does nothing. Tried by hand on 22/10/2026,
    /// the button did nothing on a first page.
    void back_from_the_first_page_opens_the_volume_before_at_its_end()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        BookExtras extras;
        extras.preceded = true;
        extras.unopened = true;
        pretend.answerFor = [&extras](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s, nullptr, u"manga"_s, nullptr,
                                nullptr, &extras);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();
        QCOMPARE(reader->firstPage(), 0);

        QQuickItem *back = barsCalled(window, view, u"reader-page-backward"_s);
        QVERIFY(back);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centreOf(back));
        QTRY_COMPARE(reader->entryId(), u"e11"_s);
        QTRY_VERIFY(!reader->loading() && reader->pageCount() == 30);
        QCOMPARE(reader->firstPage(), 29);
        QQuickItem *last = nullptr;
        QTRY_VERIFY((last = itemNamed(view, u"reader-page-29"_s)) && last->isVisible());

        // Tome 11 is the first: back from its first page stays there. In a manga, back is
        // the arrow on the right.
        reader->goToPage(0);
        QTRY_VERIFY(!reader->next().isEmpty());
        window->requestActivate();
        QVERIFY(QTest::qWaitFor([&] { return window->isActive() && view->hasActiveFocus(); }));
        QTest::keyClick(window, Qt::Key_Right);
        QVERIFY(aFrameDrawn(window));
        QCOMPARE(reader->entryId(), u"e11"_s);
        QCOMPARE(reader->firstPage(), 0);
        QVERIFY(!reader->loading());
    }

    void escape_closes_the_card_and_stays_on_the_last_page()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        BookExtras extras;
        extras.followed = true;
        pretend.answerFor = [&extras](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s, nullptr, u"manga"_s, nullptr,
                                nullptr, &extras);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *navigation =
            engine.singletonInstance<Navigation *>(qmlTypeId("Leaf", 1, 0, "Navigation"));
        Reader *reader = readerOpenOnTheBook(engine);
        QVERIFY(reader);
        QQuickItem *view = nullptr;
        QTRY_VERIFY((view = itemNamed(window->contentItem(), u"reader-view"_s)));
        QTRY_VERIFY(!reader->loading() && reader->pageCount() == 190);
        QQuickItem *card = raiseTheEndCard(window, view, reader);
        QVERIFY(card);

        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!card->isVisible());
        QTest::qWait(50);
        QCOMPARE(navigation->destination(), Navigation::Destination::Reader);
        QCOMPARE(reader->entryId(), u"e12"_s);
        QCOMPARE(reader->firstPage(), 189);

        // And the next Escape leaves the reader, as it always did.
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_COMPARE(navigation->destination(), Navigation::Destination::Series);
    }

    /// Nothing follows: the card says so and keeps the one way out, back to the series.
    void the_last_volume_offers_only_the_way_back()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *navigation =
            engine.singletonInstance<Navigation *>(qmlTypeId("Leaf", 1, 0, "Navigation"));
        Reader *reader = readerOpenOnTheBook(engine);
        QVERIFY(reader);
        QQuickItem *view = nullptr;
        QTRY_VERIFY((view = itemNamed(window->contentItem(), u"reader-view"_s)));
        QTRY_VERIFY(!reader->loading() && reader->pageCount() == 190);
        QQuickItem *card = raiseTheEndCard(window, view, reader);
        QVERIFY(card);

        QTRY_COMPARE(textNamed(card, u"reader-end-done"_s),
                     u"Tome 12 terminé"_s + QChar(0x00A0) + u"· dernier tome de la série"_s);
        QVERIFY(!itemNamed(card, u"reader-end-go"_s)->isVisible());
        QVERIFY(!itemNamed(card, u"reader-end-stay"_s)->isVisible());
        QVERIFY(!itemNamed(card, u"reader-end-title"_s)->isVisible());
        QQuickItem *back = itemNamed(card, u"reader-end-back"_s);
        QVERIFY(back->isVisible());

        // A turn forward has nothing to open, and the card stays.
        QTest::keyClick(window, Qt::Key_Space);
        QTest::qWait(50);
        QVERIFY(reader->atTheEnd());
        QCOMPARE(reader->entryId(), u"e12"_s);

        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centreOf(back));
        QTRY_COMPARE(navigation->destination(), Navigation::Destination::Series);
    }

    /// A webtoon is scrolled and not turned, yet it ends like any book: forward at the bottom
    /// of the strip raises the card instead of scrolling a strip that has no more to give.
    void the_bottom_of_a_webtoon_raises_the_card_on_the_next_forward()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        BookExtras extras;
        extras.followed = true;
        pretend.answerFor = [&extras](const QByteArray &request) {
            return aBookAnswers(request, u"VERTICAL"_s, nullptr, u"webtoon"_s, nullptr,
                                nullptr, &extras);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        Reader *reader = readerOpenOnTheBook(engine);
        QVERIFY(reader);
        QQuickItem *view = nullptr;
        QTRY_VERIFY((view = itemNamed(window->contentItem(), u"reader-view"_s)));
        QTRY_VERIFY(!reader->loading() && reader->pageCount() == 190);
        QQuickItem *strip = nullptr;
        QTRY_VERIFY((strip = itemNamed(view, u"reader-strip"_s)) && strip->isVisible());
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));

        QTest::keyClick(window, Qt::Key_End);
        QTRY_VERIFY(strip->property("atYEnd").toBool());
        QTRY_COMPARE(reader->firstPage(), 189);
        QVERIFY(!reader->atTheEnd());
        QQuickItem *card = itemNamed(view, u"reader-end"_s);
        QVERIFY(card);
        QVERIFY(!card->isVisible());

        QTest::keyClick(window, Qt::Key_Space);
        QTRY_VERIFY(reader->atTheEnd());
        QTRY_VERIFY(card->isVisible());
        QTRY_COMPARE(reader->next().value(u"entryId"_s).toString(), u"e13"_s);
        QTest::keyClick(window, Qt::Key_Space);
        QTRY_COMPARE(reader->entryId(), u"e13"_s);
    }

    /// A turn back with the card up puts it away in a book, on the same page.
    void turning_back_closes_the_card_in_a_book()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        BookExtras extras;
        extras.followed = true;
        pretend.answerFor = [&extras](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s, nullptr, u"manga"_s, nullptr,
                                nullptr, &extras);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        Reader *reader = readerOpenOnTheBook(engine);
        QVERIFY(reader);
        QQuickItem *view = nullptr;
        QTRY_VERIFY((view = itemNamed(window->contentItem(), u"reader-view"_s)));
        QTRY_VERIFY(!reader->loading() && reader->pageCount() == 190);
        QQuickItem *card = raiseTheEndCard(window, view, reader);
        QVERIFY(card);
        QTest::keyClick(window, Qt::Key_Space, Qt::ShiftModifier);
        QTRY_VERIFY(!card->isVisible());
        QCOMPARE(reader->firstPage(), 189);
        QCOMPARE(reader->entryId(), u"e12"_s);
    }

    /// And in a webtoon, whose turn back used to scroll the strip and leave the card up.
    void turning_back_closes_the_card_in_a_webtoon()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        BookExtras extras;
        extras.followed = true;
        pretend.answerFor = [&extras](const QByteArray &request) {
            return aBookAnswers(request, u"VERTICAL"_s, nullptr, u"webtoon"_s, nullptr,
                                nullptr, &extras);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        Reader *reader = readerOpenOnTheBook(engine);
        QVERIFY(reader);
        QQuickItem *view = nullptr;
        QTRY_VERIFY((view = itemNamed(window->contentItem(), u"reader-view"_s)));
        QTRY_VERIFY(!reader->loading() && reader->pageCount() == 190);
        QQuickItem *strip = nullptr;
        QTRY_VERIFY((strip = itemNamed(view, u"reader-strip"_s)) && strip->isVisible());
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));
        QTest::keyClick(window, Qt::Key_End);
        QTRY_VERIFY(strip->property("atYEnd").toBool());
        QTRY_COMPARE(reader->firstPage(), 189);
        QTest::keyClick(window, Qt::Key_Space);
        QTRY_VERIFY(reader->atTheEnd());
        QQuickItem *card = itemNamed(view, u"reader-end"_s);
        QTRY_VERIFY(card->isVisible());

        QTest::keyClick(window, Qt::Key_Space, Qt::ShiftModifier);
        QTRY_VERIFY(!card->isVisible());
        QVERIFY(strip->property("atYEnd").toBool());
        QCOMPARE(reader->firstPage(), 189);
    }

    /// While the rest of the series has not answered, nothing is known of what follows: the
    /// card says the volume is finished, never that it is the last, and offers no button.
    void the_card_does_not_claim_the_last_volume_while_the_way_on_is_out()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        BookExtras extras;
        extras.followed = true;
        pretend.answerFor = [&extras](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s, nullptr, u"manga"_s, nullptr,
                                nullptr, &extras);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        Reader *reader = readerOpenOnTheBook(engine);
        QVERIFY(reader);
        QQuickItem *view = nullptr;
        QTRY_VERIFY((view = itemNamed(window->contentItem(), u"reader-view"_s)));
        QTRY_VERIFY(!reader->loading() && reader->pageCount() == 190);
        pretend.holdBack = [](const QByteArray &request) {
            return request.startsWith("GET /series/s1/entries");
        };
        QQuickItem *card = raiseTheEndCard(window, view, reader);
        QVERIFY(card);
        QCOMPARE(textNamed(card, u"reader-end-done"_s), u"Tome 12 terminé"_s);
        QVERIFY(!itemNamed(card, u"reader-end-go"_s)->isVisible());
        QVERIFY(itemNamed(card, u"reader-end-back"_s)->isVisible());
        QVERIFY(itemNamed(card, u"reader-end-stay"_s)->isVisible());

        pretend.holdBack = nullptr;
        pretend.release();
        QTRY_VERIFY(itemNamed(card, u"reader-end-go"_s)->isVisible());
    }

    /// A way on that failed is not « nothing follows »: the card does not say last volume, and
    /// the next turn asks again, then the card offers the volume and the one after opens it.
    void a_failed_way_on_is_asked_again_by_the_next_turn()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        BookExtras extras;
        extras.followed = true;
        extras.entriesFail = true;
        // The rest of the series asked near the end of the volume, which is the reader's way
        // on: the series page under the reader asks for the same list, at page 46.
        Reader *reader = nullptr;
        int wayOnAsked = 0;
        pretend.answerFor = [&extras, &reader, &wayOnAsked](const QByteArray &request) {
            if (request.startsWith("GET /series/s1/entries ") && reader
                && reader->firstPage() > 180)
                ++wayOnAsked;
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s, nullptr, u"manga"_s, nullptr,
                                nullptr, &extras);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        reader = readerOpenOnTheBook(engine);
        QVERIFY(reader);
        QQuickItem *view = nullptr;
        QTRY_VERIFY((view = itemNamed(window->contentItem(), u"reader-view"_s)));
        QTRY_VERIFY(!reader->loading() && reader->pageCount() == 190);
        QQuickItem *card = raiseTheEndCard(window, view, reader);
        QVERIFY(card);
        // A turn asks again only once the refusal before it has landed, so the turns go on
        // until the server hears the question that many times. A fixed wait in place of this
        // was a guess at how long a refusal takes to come back, and a loaded machine guessed
        // longer.
        const auto askedAgain = [&](int times) {
            if (wayOnAsked >= times)
                return true;
            QTest::keyClick(window, Qt::Key_Space);
            return false;
        };
        QTRY_VERIFY(askedAgain(2));
        QCOMPARE(textNamed(card, u"reader-end-done"_s), u"Tome 12 terminé"_s);
        QVERIFY(!itemNamed(card, u"reader-end-go"_s)->isVisible());
        QVERIFY(itemNamed(card, u"reader-end-back"_s)->isVisible());
        QVERIFY(itemNamed(card, u"reader-end-stay"_s)->isVisible());

        extras.entriesFail = false;
        QTRY_VERIFY(askedAgain(3));
        QTRY_VERIFY(itemNamed(card, u"reader-end-go"_s)->isVisible());
        QVERIFY(card->isVisible());
        QCOMPARE(reader->entryId(), u"e12"_s);
        QTest::keyClick(window, Qt::Key_Space);
        QTRY_COMPARE(reader->entryId(), u"e13"_s);
    }

    /// What follows may be a chapter, and the button says so.
    void the_card_offers_a_chapter_as_a_chapter()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        BookExtras extras;
        extras.followedByChapter = true;
        pretend.answerFor = [&extras](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s, nullptr, u"manga"_s, nullptr,
                                nullptr, &extras);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        Reader *reader = readerOpenOnTheBook(engine);
        QVERIFY(reader);
        QQuickItem *view = nullptr;
        QTRY_VERIFY((view = itemNamed(window->contentItem(), u"reader-view"_s)));
        QTRY_VERIFY(!reader->loading() && reader->pageCount() == 190);
        QQuickItem *card = raiseTheEndCard(window, view, reader);
        QVERIFY(card);
        QTRY_COMPARE(itemNamed(card, u"reader-end-go"_s)->property("label").toString(),
                     u"Lire le chapitre 108"_s);
    }

    /// The bars a volume opens with leave on the first turn, without waiting their three
    /// seconds — and come back with the next volume opened.
    void the_bars_a_volume_opens_with_leave_on_the_first_turn()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        Reader *reader = readerOpenOnTheBook(engine);
        QVERIFY(reader);
        QQuickItem *view = nullptr;
        QTRY_VERIFY((view = itemNamed(window->contentItem(), u"reader-view"_s)));
        QTRY_VERIFY(!reader->loading() && reader->pageCount() == 190);
        QObject *welcome = view->findChild<QObject *>(u"reader-welcome"_s);
        QVERIFY(welcome);
        QVERIFY(view->property("barsShown").toBool());
        QVERIFY(welcome->property("running").toBool());
        window->requestActivate();
        QVERIFY(QTest::qWaitFor([&] { return window->isActive() && view->hasActiveFocus(); }));

        const int before = reader->display();
        QTest::keyClick(window, Qt::Key_Left);
        QTRY_COMPARE(reader->display(), before + 1);
        QVERIFY(!view->property("barsShown").toBool());
        QVERIFY(!welcome->property("running").toBool());

        // Opened again, the volume comes with its bars.
        reader->open(u"s1"_s, u"e12"_s);
        QTRY_VERIFY(!reader->loading());
        QTRY_VERIFY(view->property("barsShown").toBool());
        QVERIFY(welcome->property("running").toBool());
    }

    /// The dock, the bar and what the reader says in place of a book follow the window: tried
    /// by hand on 22/10/2026 in full screen on a window of 1920, the dock was a strip of specks
    /// at the foot of the page at its drawn size; on a small window its words kept that size
    /// over a page that had shrunk.
    void the_dock_follows_the_window_both_ways()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();
        const auto putBack = qScopeGuard([&] { window->resize(1100, 760); });
        QQuickItem *forward = barsCalled(window, view, u"reader-page-forward"_s);
        QVERIFY(forward);
        QQuickItem *dock = itemNamed(view, u"reader-dock"_s);
        QQuickItem *bar = itemNamed(view, u"reader-bar"_s);
        QQuickItem *number = itemNamed(view, u"reader-page-number"_s);
        QQuickItem *title = itemNamed(view, u"reader-title"_s);
        // What went wrong instead of the book, said where the bar and the dock are.
        QQuickItem *trouble = itemNamed(view, u"reader-trouble"_s);
        QVERIFY(number && title && trouble);
        const auto letters = [](QQuickItem *text) {
            return text->property("font").value<QFont>().pixelSize();
        };

        // Drawn for 1280, and exactly as drawn there.
        window->resize(1280, 800);
        QTRY_COMPARE(view->width(), 1280.0);
        QTRY_COMPARE(forward->width(), 26.0);
        QCOMPARE(bar->height(), 58.0);
        QCOMPARE(letters(number), 13);
        QCOMPARE(letters(title), 19);
        QCOMPARE(letters(trouble), 15);
        const qreal tall = dock->height();

        // Half as large again at 2560, and still inside the window, centred; its words at
        // their drawn size, which on 23/10/2026 at 1920 had grown with the rest and read too
        // big, and its glyphs at half the rate.
        window->resize(2560, 1440);
        QTRY_COMPARE(view->width(), 2560.0);
        QTRY_COMPARE(forward->width(), 39.0);
        QCOMPARE(forward->property("side").toInt(), 25);
        QCOMPARE(bar->height(), 87.0);
        QVERIFY(dock->height() >= tall * 1.2);
        QCOMPARE(letters(number), 13);
        QCOMPARE(letters(title), 19);
        QCOMPARE(letters(trouble), 15);
        QVERIFY(dock->width() <= view->width());
        QVERIFY(qAbs(dock->x() + dock->width() / 2 - view->width() / 2) <= 1);

        // Smaller on a small window, 0.85 at 900 and no further, and its words with it down
        // to their floors: a caption to 11, the title to 15, what is read to 13.
        window->resize(900, 700);
        QTRY_COMPARE(view->width(), 900.0);
        QTRY_COMPARE(forward->width(), 22.0);
        QCOMPARE(bar->height(), 49.0);
        QCOMPARE(letters(number), 11);
        QCOMPARE(letters(title), 15);
        QCOMPARE(letters(trouble), 13);
        QVERIFY(dock->width() <= view->width());

        window->resize(560, 700);
        QTRY_COMPARE(view->width(), 560.0);
        QCOMPARE(forward->width(), 22.0);
        QCOMPARE(letters(number), 11);
        QVERIFY(dock->x() >= 0 && dock->x() + dock->width() <= view->width());
    }

    /// The screen is 2560 × 1600 at a scale of 1.333, so the window Qt lays out in
    /// full screen is 1920 × 1200. In the test of 22/10/2026 the dock was a strip of specks
    /// there at its drawn size, and too large at half as large again: a quarter, now.
    void the_dock_is_a_quarter_larger_at_a_full_screen_of_1920_by_1200()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();
        const auto putBack = qScopeGuard([&] { window->resize(1100, 760); });
        QQuickItem *forward = barsCalled(window, view, u"reader-page-forward"_s);
        QVERIFY(forward);
        QQuickItem *dock = itemNamed(view, u"reader-dock"_s);
        QQuickItem *bar = itemNamed(view, u"reader-bar"_s);

        window->resize(1920, 1200);
        QTRY_COMPARE(view->width(), 1920.0);
        QTRY_COMPARE(forward->width(), 33.0);
        QCOMPARE(bar->height(), 73.0);
        QCOMPARE(dock->width(), 800.0);
        // On 23/10/2026 there its words and glyphs read too big and its buttons had gained no
        // room between them: the words at their drawn size, the glyphs an eighth larger, and
        // the gaps between a step and the progress bar twice the window's growth: 4 and 6
        // drawn, 6 and 9 here.
        QQuickItem *scrub = itemNamed(view, u"reader-scrub"_s);
        QVERIFY(scrub);
        const QRectF step = placed(forward);
        const QRectF progress = placed(scrub);
        QCOMPARE(std::max(step.left() - progress.right(), progress.left() - step.right()), 15.0);
        QCOMPARE(forward->property("side").toInt(), 23);
        QCOMPARE(itemNamed(view, u"reader-page-number"_s)->property("font").value<QFont>()
                     .pixelSize(), 13);
        QCOMPARE(itemNamed(view, u"reader-title"_s)->property("font").value<QFont>().pixelSize(),
                 19);
    }

    /// The settings card follows the window as the dock does; see `aCardThatDoesNotFollow`.
    void the_settings_card_follows_the_window_as_the_dock_does()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        const auto putBack = qScopeGuard([&] { view->window()->resize(1100, 760); });

        const QString wrong =
            aCardThatDoesNotFollow(view, u"reader-settings"_s, u"reader-settings-title"_s, 640,
                                   17, Scale::titleFor, 1, 24);
        QVERIFY2(wrong.isEmpty(), qPrintable(wrong));
        // Its pills with it, which the Settings screen draws at their own size: smaller on a
        // small window, never larger on a large one.
        QQuickItem *pill = itemNamed(view, u"reader-direction-0-text"_s);
        QVERIFY(pill);
        view->window()->resize(1920, 1200);
        QTRY_COMPARE(view->width(), 1920.0);
        QCOMPARE(pill->property("font").value<QFont>().pixelSize(), 14);
        view->window()->resize(900, 700);
        QTRY_COMPARE(pill->property("font").value<QFont>().pixelSize(), 13);
    }

    /// The volume's card follows the window as the dock does: its words by the dock's factor,
    /// its width by its share of the window up to its drawn width so grown.
    void the_volume_panel_follows_the_window_as_the_dock_does()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();
        const auto putBack = qScopeGuard([&] { window->resize(1100, 760); });
        QQuickItem *panel = itemNamed(view, u"reader-volume"_s);
        QVERIFY(panel);
        const auto letters = [view](const QString &name) {
            QQuickItem *text = itemNamed(view, name);
            return text ? text->property("font").value<QFont>().pixelSize() : -1;
        };

        // At 1280, as drawn.
        window->resize(1280, 800);
        QTRY_COMPARE(view->width(), 1280.0);
        QTRY_COMPARE(panel->width(), 460.8);
        QCOMPARE(letters(u"reader-volume-head"_s), 17);
        QCOMPARE(letters(u"reader-volume-summary"_s), 14);
        QCOMPARE(letters(u"reader-volume-series-name"_s), 13);

        // Wider at half the window's rate, its words at their drawn size (23/10/2026).
        window->resize(1920, 1200);
        QTRY_COMPARE(view->width(), 1920.0);
        QTRY_COMPARE(panel->width(), 540.0);
        QCOMPARE(letters(u"reader-volume-head"_s), 17);
        QCOMPARE(letters(u"reader-volume-summary"_s), 14);
        QCOMPARE(letters(u"reader-volume-series-name"_s), 13);

        window->resize(2560, 1600);
        QTRY_COMPARE(view->width(), 2560.0);
        QTRY_COMPARE(panel->width(), 600.0);
        QCOMPARE(letters(u"reader-volume-head"_s), 17);

        window->resize(900, 700);
        QTRY_COMPARE(view->width(), 900.0);
        QTRY_COMPARE(letters(u"reader-volume-head"_s), 15);
        QCOMPARE(letters(u"reader-volume-summary"_s), 13);
        // Never more than two fifths of a window that has the pages beside it.
        QVERIFY(panel->width() <= 900 * 0.4);

        window->resize(560, 700);
        QTRY_COMPARE(view->width(), 560.0);
        QTRY_VERIFY(panel->width() <= 560 - 2 * view->property("margin").toReal());
    }

    void a_click_in_the_middle_shows_the_bars_and_a_second_hides_them()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        Reader *reader = readerOpenOnTheBook(engine);
        QVERIFY(reader);
        QQuickItem *view = nullptr;
        QTRY_VERIFY((view = itemNamed(window->contentItem(), u"reader-view"_s)));
        QTRY_VERIFY(!reader->loading() && reader->pageCount() == 190);
        QQuickItem *bar = itemNamed(view, u"reader-bar"_s);
        QQuickItem *dock = itemNamed(view, u"reader-dock"_s);
        QVERIFY(bar && dock);
        // Up as the volume opens; a click in the middle sends them away, and leaving on
        // their own is no longer theirs to do.
        QTRY_VERIFY(bar->isVisible() && dock->isVisible());
        QTRY_COMPARE(bar->opacity(), 1.0);
        QVERIFY(aFrameDrawn(window));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, across(view, 0.5));
        QTRY_VERIFY(!bar->isVisible() && !dock->isVisible());
        QVERIFY(!view->findChild<QObject *>(u"reader-welcome"_s)->property("running").toBool());

        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, across(view, 0.5));
        QTRY_VERIFY(bar->isVisible() && dock->isVisible());
        QTRY_COMPARE(bar->opacity(), 1.0);
        // The bar is the application's height for this window — drawn for 1280, and the test
        // window is narrower — and the card floats clear of the edge.
        QCOMPARE(bar->height(), qRound(58 * Widths::controlsFor(int(view->width()))) * 1.0);
        QVERIFY(dock->y() + dock->height() < view->height());
        // The book's name and where in it, as the bar's two lines.
        QCOMPARE(textNamed(bar, u"reader-title"_s), u"Assassination Classroom"_s);
        QCOMPARE(textNamed(dock, u"reader-page-number"_s),
                 QString::number(reader->firstPage() + 1));
        // A click on the bar is the bar's: it does not reach the page under it.
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          bar->mapToScene(QPointF(bar->width() / 2.0, bar->height() / 2.0))
                              .toPoint());
        QTest::qWait(50);
        // The view's own state and not the bar's visibility, which a fade keeps true for a
        // moment whatever was decided.
        QVERIFY(view->property("barsShown").toBool());

        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, across(view, 0.5));
        QTRY_VERIFY(!bar->isVisible() && !dock->isVisible());

        // Turning a page sends them away too.
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, across(view, 0.5));
        QTRY_VERIFY(bar->isVisible());
        const int before = reader->display();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, across(view, 0.10));
        QTRY_COMPARE(reader->display(), before + 1);
        QTRY_VERIFY(!bar->isVisible() && !dock->isVisible());
    }

    /// Leaving straight after a turn: the volume's line shows the page just reached, once the
    /// server has kept it. Asked at the moment of leaving, the server could still answer with
    /// the page before — and nothing asked again after.
    void leaving_right_after_a_turn_shows_the_page_reached_once_it_is_kept()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        int recorded = 46;
        pretend.answerFor = [&recorded](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s, &recorded);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *navigation =
            engine.singletonInstance<Navigation *>(qmlTypeId("Leaf", 1, 0, "Navigation"));
        Reader *reader = readerOpenOnTheBook(engine);
        QVERIFY(reader);
        QTRY_VERIFY(itemNamed(window->contentItem(), u"reader-view"_s));
        QTRY_VERIFY(!reader->loading() && reader->pageCount() == 190);

        pretend.holdBack = [](const QByteArray &request) { return request.startsWith("PATCH "); };
        reader->forward();
        const int reached = std::max(reader->firstPage(), reader->secondPage());
        QVERIFY(reached > 46);
        navigation->back();
        QTRY_COMPARE(navigation->destination(), Navigation::Destination::Series);
        QTRY_VERIFY(pretend.heard.contains("PATCH /entries/e12/progress"));
        QTest::qWait(300);

        pretend.holdBack = nullptr;
        pretend.release();
        QQuickItem *row = nullptr;
        QTRY_VERIFY((row = itemNamed(window->contentItem(), u"volume-e12"_s)));
        QTRY_COMPARE(row->property("howFarRead").toReal(), reached / 190.0);
    }

    /// Escape takes away one thing at a time: the full screen first, and only then the
    /// reader — a reader who pressed it to get their desktop back did not mean to close
    /// the book.
    void escape_leaves_full_screen_before_it_leaves_the_reader()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *navigation =
            engine.singletonInstance<Navigation *>(qmlTypeId("Leaf", 1, 0, "Navigation"));
        Reader *reader = readerOpenOnTheBook(engine);
        QVERIFY(reader);
        QTRY_VERIFY(itemNamed(window->contentItem(), u"reader-view"_s));
        QTRY_VERIFY(!reader->loading() && reader->pageCount() == 190);
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));

        QTest::keyClick(window, Qt::Key_F);
        QTRY_COMPARE(window->visibility(), QWindow::FullScreen);

        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(window->visibility() != QWindow::FullScreen);
        QTest::qWait(50);
        QCOMPARE(navigation->destination(), Navigation::Destination::Reader);

        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_COMPARE(navigation->destination(), Navigation::Destination::Series);
    }

    /// A direction chosen in the card is the direction the next page is turned: a book that
    /// declared left to right, chosen right to left, goes forward by the left third. The card
    /// is under `tune`, which stays lit while it is open, and a click on the page outside it
    /// closes it and does nothing else.
    void choosing_a_direction_in_the_reader_turns_the_next_page_the_new_way()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        QString chosen;
        pretend.answerFor = [&chosen](const QByteArray &request) {
            return aBookAnswers(request, u"LEFT_TO_RIGHT"_s, nullptr, u"manga"_s, &chosen);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        Reader *reader = readerOpenOnTheBook(engine);
        QVERIFY(reader);
        QQuickItem *view = nullptr;
        QTRY_VERIFY((view = itemNamed(window->contentItem(), u"reader-view"_s)));
        QTRY_VERIFY(!reader->loading() && reader->pageCount() == 190);
        QVERIFY(!itemNamed(view, u"reader-settings"_s)->isVisible());

        QQuickItem *card = openTheSettings(window, view);
        QVERIFY(card);
        QCOMPARE(itemNamed(view, u"reader-settings-button"_s)->property("held").toBool(), true);
        // Under the button, inside the window.
        QQuickItem *tune = itemNamed(view, u"reader-settings-button"_s);
        QVERIFY(card->mapToScene(QPointF(0, 0)).y() >= tune->mapToScene(QPointF(0, 0)).y());
        QVERIFY(card->mapToScene(QPointF(card->width(), 0)).x() <= view->width());
        // The four directions are words alone, « Par défaut » lit, the line names the files'.
        QQuickItem *ltr = itemNamed(card, u"reader-direction-0"_s);
        QQuickItem *rtl = itemNamed(card, u"reader-direction-1"_s);
        QQuickItem *byDefault = itemNamed(card, u"reader-direction--1"_s);
        QVERIFY(ltr && rtl && byDefault && itemNamed(card, u"reader-direction-2"_s));
        QCOMPARE(textNamed(card, u"reader-direction-1-text"_s), u"De droite à gauche"_s);
        QVERIFY(byDefault->property("lit").toBool());
        QVERIFY(textNamed(card, u"reader-declares"_s).contains(u"gauche"_s));

        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centreOf(rtl));
        QTRY_COMPARE(reader->direction(), int(Reader::Direction::RightToLeft));
        QTRY_VERIFY(rtl->property("lit").toBool());
        QVERIFY(!byDefault->property("lit").toBool());
        QVERIFY(pretend.heard.contains("PUT /series/s1/reading-direction"));
        QCOMPARE(chosen, u"RIGHT_TO_LEFT"_s);

        // A click on the page outside the card shuts it and turns nothing.
        const int before = reader->display();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, across(view, 0.10));
        QTRY_VERIFY(!card->isVisible());
        QCOMPARE(itemNamed(view, u"reader-settings-button"_s)->property("held").toBool(), false);
        QCOMPARE(reader->display(), before);
        // The next one turns it the new way: the left third goes forward.
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, across(view, 0.10));
        QTRY_COMPARE(reader->display(), before + 1);
    }

    /// The card has a title and a cross, as the mockup draws it, and the cross closes it the
    /// way Escape does — without touching the page under it.
    void the_cross_on_the_settings_card_closes_it_and_leaves_the_page_alone()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        Reader *reader = readerOpenOnTheBook(engine);
        QVERIFY(reader);
        QQuickItem *view = nullptr;
        QTRY_VERIFY((view = itemNamed(window->contentItem(), u"reader-view"_s)));
        QTRY_VERIFY(!reader->loading() && reader->pageCount() == 190);
        QQuickItem *card = openTheSettings(window, view);
        QVERIFY(card);
        QCOMPARE(textNamed(card, u"reader-settings-title"_s), u"Lecture"_s);
        QQuickItem *cross = itemNamed(card, u"reader-settings-close"_s);
        QVERIFY(cross && cross->isVisible());

        const int before = reader->display();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centreOf(cross));
        QTRY_VERIFY(!card->isVisible());
        QCOMPARE(reader->display(), before);
        QVERIFY(view->property("barsShown").toBool());
    }

    /// Escape takes the settings card first, then the full screen, then the reader.
    void escape_closes_the_settings_card_before_the_full_screen_and_the_reader()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *navigation =
            engine.singletonInstance<Navigation *>(qmlTypeId("Leaf", 1, 0, "Navigation"));
        Reader *reader = readerOpenOnTheBook(engine);
        QVERIFY(reader);
        QQuickItem *view = nullptr;
        QTRY_VERIFY((view = itemNamed(window->contentItem(), u"reader-view"_s)));
        QTRY_VERIFY(!reader->loading() && reader->pageCount() == 190);
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));
        QTest::keyClick(window, Qt::Key_F);
        QTRY_COMPARE(window->visibility(), QWindow::FullScreen);
        QQuickItem *card = openTheSettings(window, view);
        QVERIFY(card);

        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!card->isVisible());
        QCOMPARE(window->visibility(), QWindow::FullScreen);
        QCOMPARE(navigation->destination(), Navigation::Destination::Reader);

        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(window->visibility() != QWindow::FullScreen);
        QCOMPARE(navigation->destination(), Navigation::Destination::Reader);

        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_COMPARE(navigation->destination(), Navigation::Destination::Series);
    }

    /// A refused choice leaves no pill lit for it: the pill that was lit stays, and the
    /// refusal is said. A pill lit on the click and put back on the answer would flicker a
    /// lie for as long as the server took.
    void a_direction_the_server_refused_leaves_the_previous_pill_lit()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        QString chosen;
        bool refuse = false;
        pretend.answerFor = [&chosen, &refuse](const QByteArray &request) {
            return aBookAnswers(request, u"LEFT_TO_RIGHT"_s, nullptr, u"manga"_s, &chosen,
                                &refuse);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        Reader *reader = readerOpenOnTheBook(engine);
        QVERIFY(reader);
        QQuickItem *view = nullptr;
        QTRY_VERIFY((view = itemNamed(window->contentItem(), u"reader-view"_s)));
        QTRY_VERIFY(!reader->loading() && reader->pageCount() == 190);
        QQuickItem *card = openTheSettings(window, view);
        QVERIFY(card);
        QQuickItem *rtl = itemNamed(card, u"reader-direction-1"_s);
        QQuickItem *vertical = itemNamed(card, u"reader-direction-2"_s);
        QVERIFY(rtl && vertical);

        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centreOf(rtl));
        QTRY_VERIFY(rtl->property("lit").toBool());

        refuse = true;
        QSignalSpy refused(reader, &Reader::refused);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centreOf(vertical));
        QTRY_COMPARE(refused.count(), 1);
        QTest::qWait(50);
        QVERIFY(!vertical->property("lit").toBool());
        QVERIFY(rtl->property("lit").toBool());
        QCOMPARE(reader->chosenDirection(), int(Reader::Direction::RightToLeft));
        QCOMPARE(reader->direction(), int(Reader::Direction::RightToLeft));
    }

    /// The book button lays a card over the right of the pages, which keep their width: it
    /// took the right half and shrank the pages into the left one until the test of
    /// 22/10/2026. Closing it — the cross, `i`, Escape, a click on the page — and Escape
    /// closes it before the reader.
    void the_book_button_lays_a_card_over_the_pages_and_leaves_their_width_alone()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *navigation =
            engine.singletonInstance<Navigation *>(qmlTypeId("Leaf", 1, 0, "Navigation"));
        Reader *reader = readerOpenOnTheBook(engine);
        QVERIFY(reader);
        QQuickItem *view = nullptr;
        QTRY_VERIFY((view = itemNamed(window->contentItem(), u"reader-view"_s)));
        QTRY_VERIFY(!reader->loading() && reader->pageCount() == 190);
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));
        QQuickItem *stage = itemNamed(view, u"reader-stage"_s);
        QVERIFY(stage);
        QQuickItem *panel = itemNamed(view, u"reader-volume"_s);
        QVERIFY(panel);
        QVERIFY(!panel->isVisible());
        const qreal whole = stage->width();
        const int askedAt = reader->pageWidth();
        QQuickItem *page = itemNamed(view, u"reader-page-%1"_s.arg(reader->firstPage()));
        QVERIFY(page);
        const qreal pageWide = page->width();

        QQuickItem *opened = openTheVolumePanel(window, view);
        QVERIFY(opened);
        QCOMPARE(itemNamed(view, u"reader-volume-button"_s)->property("held").toBool(), true);
        // Slid in and faded in, then still: on the right of the window, inside it.
        QTRY_COMPARE(panel->opacity(), 1.0);
        const qreal left = panel->mapToScene(QPointF(0, 0)).x();
        QVERIFY(left > view->width() / 2);
        QVERIFY(panel->mapToScene(QPointF(panel->width(), 0)).x() <= view->width());
        // Over the pages, which have not moved nor shrunk.
        QVERIFY(aFrameDrawn(window));
        QCOMPARE(stage->width(), whole);
        QCOMPARE(reader->pageWidth(), askedAt);
        QCOMPARE(page->width(), pageWide);

        // The cross closes it, fading, and the pages are as they were.
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          centreOf(itemNamed(panel, u"reader-volume-close"_s)));
        QTRY_VERIFY(!panel->isVisible());
        QCOMPARE(stage->width(), whole);
        QCOMPARE(itemNamed(view, u"reader-volume-button"_s)->property("held").toBool(), false);

        // `i` toggles it, with no bar called.
        QTest::keyClick(window, Qt::Key_I);
        QTRY_VERIFY(panel->isVisible());
        QTest::keyClick(window, Qt::Key_I);
        QTRY_VERIFY(!panel->isVisible());

        // A click on the page closes it and turns nothing.
        QTest::keyClick(window, Qt::Key_I);
        QTRY_VERIFY(panel->isVisible());
        const int before = reader->display();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, across(view, 0.10));
        QTRY_VERIFY(!panel->isVisible());
        QCOMPARE(reader->display(), before);

        // Escape closes it first and only then leaves the reader.
        QTest::keyClick(window, Qt::Key_I);
        QTRY_VERIFY(panel->isVisible());
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!panel->isVisible());
        QTest::qWait(50);
        QCOMPARE(navigation->destination(), Navigation::Destination::Reader);
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_COMPARE(navigation->destination(), Navigation::Destination::Series);
    }

    /// A webtoon the same: the strip keeps the width of the window under the card, and the
    /// page being read stays at its top.
    void a_webtoon_under_the_panel_keeps_its_width_and_the_page_being_read()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"VERTICAL"_s, nullptr, u"webtoon"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        Reader *reader = readerOpenOnTheBook(engine);
        QVERIFY(reader);
        QQuickItem *view = nullptr;
        QTRY_VERIFY((view = itemNamed(window->contentItem(), u"reader-view"_s)));
        QTRY_VERIFY(!reader->loading() && reader->pageCount() == 190);
        QQuickItem *strip = nullptr;
        QTRY_VERIFY((strip = itemNamed(view, u"reader-strip"_s)) && strip->isVisible());
        QTRY_COMPARE(stripPageAt(strip, strip->property("contentY").toReal() + 1), 46);
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));

        const qreal wide = strip->width();
        QCOMPARE(wide, view->width());
        QQuickItem *panel = openTheVolumePanel(window, view);
        QVERIFY(panel);
        QTRY_COMPARE(panel->opacity(), 1.0);
        QVERIFY(aFrameDrawn(window));
        QCOMPARE(strip->width(), wide);
        QTRY_COMPARE(stripPageAt(strip, strip->property("contentY").toReal() + 1), 46);
        QTRY_VERIFY(!strip->property("moving").toBool());
        QCOMPARE(reader->firstPage(), 46);
    }

    /// The chapters are listed, the one being read is lit, a click goes to its page — and a
    /// chapter that does not say where it begins is listed and goes nowhere.
    void a_chapter_clicked_in_the_panel_jumps_to_its_page()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        BookExtras extras;
        extras.chapters = QJsonArray{
            QJsonObject{{u"id"_s, u"c98"_s}, {u"label"_s, u"Chap.098"_s}, {u"number"_s, 98},
                        {u"title"_s, u"Le temps des examens"_s}, {u"kind"_s, u"CHAPTER"_s},
                        {u"entryId"_s, u"e12"_s}, {u"startPage"_s, 0}},
            QJsonObject{{u"id"_s, u"c99"_s}, {u"label"_s, u"Chap.099"_s}, {u"number"_s, 99},
                        {u"title"_s, u"Coup de sifflet"_s}, {u"kind"_s, u"CHAPTER"_s},
                        {u"entryId"_s, u"e12"_s}, {u"startPage"_s, 30}},
            QJsonObject{{u"id"_s, u"c100"_s}, {u"label"_s, u"Chap.100"_s}, {u"number"_s, 100},
                        {u"title"_s, u"Ridicule"_s}, {u"kind"_s, u"CHAPTER"_s},
                        {u"entryId"_s, u"e12"_s}},
            QJsonObject{{u"id"_s, u"c101"_s}, {u"label"_s, u"Chap.101"_s}, {u"number"_s, 101},
                        {u"title"_s, u"Le temps des remords"_s}, {u"kind"_s, u"CHAPTER"_s},
                        {u"entryId"_s, u"e12"_s}, {u"startPage"_s, 120}},
        };
        pretend.answerFor = [&extras](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s, nullptr, u"manga"_s, nullptr,
                                nullptr, &extras);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        const auto single = onePageAtATime(engine);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        Reader *reader = readerOpenOnTheBook(engine);
        QVERIFY(reader);
        QQuickItem *view = nullptr;
        QTRY_VERIFY((view = itemNamed(window->contentItem(), u"reader-view"_s)));
        QTRY_VERIFY(!reader->loading() && reader->pageCount() == 190);
        QQuickItem *panel = openTheVolumePanel(window, view);
        QVERIFY(panel);

        // « Chapitres 98 à 101 », four rows, the one holding page 46 lit.
        QCOMPARE(textNamed(panel, u"reader-volume-chapters"_s), u"98 à 101"_s);
        for (int i = 0; i < 4; ++i)
            QVERIFY(drawn(panel, u"reader-volume-chapter-%1"_s.arg(i)));
        QVERIFY(itemNamed(panel, u"reader-volume-chapter-1"_s)->property("current").toBool());
        QVERIFY(!itemNamed(panel, u"reader-volume-chapter-0"_s)->property("current").toBool());
        QVERIFY(textNamed(panel, u"reader-volume-chapter-2-text"_s).contains(u"Ridicule"_s));
        QCOMPARE(textNamed(panel, u"reader-volume-chapter-3-page"_s), u"p. 121"_s);
        QVERIFY(textNamed(panel, u"reader-volume-chapter-2-page"_s).isEmpty());

        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          centreOf(itemNamed(panel, u"reader-volume-chapter-3"_s)));
        QTRY_COMPARE(reader->firstPage(), 120);
        QTRY_VERIFY(itemNamed(panel, u"reader-volume-chapter-3"_s)->property("current").toBool());
        // The panel stays open while the reader reads on beside it.
        QVERIFY(panel->isVisible());

        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          centreOf(itemNamed(panel, u"reader-volume-chapter-2"_s)));
        QTest::qWait(100);
        QCOMPARE(reader->firstPage(), 120);

        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          centreOf(itemNamed(panel, u"reader-volume-chapter-0"_s)));
        QTRY_COMPARE(reader->firstPage(), 0);
    }

    /// The panel says what the volume has and nothing else: an ISBN is a line when there is
    /// one, and a label over nothing when there is not is the one thing it must never be.
    void a_volume_without_an_isbn_has_no_isbn_line()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        BookExtras extras;
        extras.volume = QJsonObject{{u"publishedOn"_s, u"2015-03-04"_s},
                                    {u"summary"_s, u"Les examens approchent."_s}};
        pretend.answerFor = [&extras](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s, nullptr, u"manga"_s, nullptr,
                                nullptr, &extras);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        const auto single = onePageAtATime(engine);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        Reader *reader = readerOpenOnTheBook(engine);
        QVERIFY(reader);
        QQuickItem *view = nullptr;
        QTRY_VERIFY((view = itemNamed(window->contentItem(), u"reader-view"_s)));
        QTRY_VERIFY(!reader->loading() && reader->pageCount() == 190);
        QQuickItem *panel = openTheVolumePanel(window, view);
        QVERIFY(panel);

        QVERIFY(!drawn(panel, u"reader-volume-isbn"_s));
        QVERIFY(!drawn(panel, u"reader-volume-isbn-name"_s));
        QVERIFY(!drawn(panel, u"reader-volume-isbn-key"_s));
        QCOMPARE(textNamed(panel, u"reader-volume-published"_s), u"4 mars 2015"_s);
        QCOMPARE(textNamed(panel, u"reader-volume-pages"_s), u"190"_s);
        QCOMPARE(textNamed(panel, u"reader-volume-series"_s), u"Assassination Classroom"_s);
        QCOMPARE(textNamed(panel, u"reader-volume-head"_s),
                 u"Tome 12 · Le temps des examens"_s);
        QCOMPARE(textNamed(panel, u"reader-volume-summary"_s), u"Les examens approchent."_s);
        QCOMPARE(textNamed(panel, u"reader-volume-read"_s), u"page 47 / 190"_s);
        // No chapters: no line for them and no list.
        QVERIFY(!drawn(panel, u"reader-volume-chapters"_s));
        QVERIFY(!drawn(panel, u"reader-volume-chapters-heading"_s));
    }

    /// What the server wrote is shown as written: a ComicInfo summary or title with markup in
    /// it is characters, not formatting.
    void markup_in_a_summary_or_a_title_is_shown_as_text()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        BookExtras extras;
        extras.volume = QJsonObject{{u"title"_s, u"Le <i>temps</i>"_s},
                                    {u"isbn"_s, u"<b>978</b>"_s},
                                    {u"summary"_s, u"<b>x</b> & <3"_s}};
        extras.chapters = QJsonArray{QJsonObject{
            {u"id"_s, u"c1"_s}, {u"label"_s, u"Chap.<b>1</b>"_s}, {u"number"_s, 1},
            {u"title"_s, u"A & <br>B"_s}, {u"kind"_s, u"CHAPTER"_s},
            {u"entryId"_s, u"e12"_s}, {u"startPage"_s, 0}}};
        pretend.answerFor = [&extras](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s, nullptr, u"manga"_s, nullptr,
                                nullptr, &extras);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        Reader *reader = readerOpenOnTheBook(engine);
        QVERIFY(reader);
        QQuickItem *view = nullptr;
        QTRY_VERIFY((view = itemNamed(window->contentItem(), u"reader-view"_s)));
        QTRY_VERIFY(!reader->loading() && reader->pageCount() == 190);
        QQuickItem *panel = openTheVolumePanel(window, view);
        QVERIFY(panel);
        for (const QString &name : {u"reader-volume-summary"_s, u"reader-volume-head"_s,
                                    u"reader-volume-isbn"_s}) {
            QQuickItem *one = itemNamed(panel, name);
            QVERIFY(one);
            QCOMPARE(one->property("textFormat").toInt(), int(Qt::PlainText));
        }
        // Not an ISBN, so no key: the line shows it as written.
        QVERIFY(!drawn(panel, u"reader-volume-isbn-key"_s));
        QCOMPARE(textNamed(panel, u"reader-volume-isbn"_s), u"<b>978</b>"_s);
        QCOMPARE(textNamed(panel, u"reader-volume-summary"_s), u"<b>x</b> & <3"_s);
        QVERIFY(textNamed(panel, u"reader-volume-head"_s).contains(u"<i>temps</i>"_s));
        QQuickItem *chapter = itemNamed(panel, u"reader-volume-chapter-0-text"_s);
        QVERIFY(chapter);
        QCOMPARE(chapter->property("textFormat").toInt(), int(Qt::PlainText));
        QVERIFY(textNamed(panel, u"reader-volume-chapter-0-text"_s)
                    .contains(u"Chap.<b>1</b>"_s));
        QVERIFY(textNamed(panel, u"reader-volume-chapter-0-text"_s).contains(u"A & <br>B"_s));
    }

    /// The card opens at its top, on what the volume is, with the chapter being read lit far
    /// down a list of forty and not scrolled to — opened on that chapter, it hid the cover and
    /// the facts it is opened for (the test of 22/10/2026). Once open, it follows the
    /// reader into the next chapter; closed and opened again, it is back at its top. A title
    /// that is only the volume's label again is not said twice over it.
    void the_panel_opens_at_its_top_and_follows_the_reader_once_open()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        BookExtras extras;
        extras.volume = QJsonObject{{u"title"_s, u"tome 12 "_s},
                                    {u"summary"_s, u"Les examens approchent."_s}};
        for (int i = 0; i < 40; ++i)
            extras.chapters.append(QJsonObject{
                {u"id"_s, u"c%1"_s.arg(i)}, {u"label"_s, u"Chap.%1"_s.arg(i)},
                {u"number"_s, i}, {u"kind"_s, u"CHAPTER"_s}, {u"entryId"_s, u"e12"_s},
                {u"startPage"_s, i * 4}});
        pretend.answerFor = [&extras](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s, nullptr, u"manga"_s, nullptr,
                                nullptr, &extras);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        Reader *reader = readerOpenOnTheBook(engine);
        QVERIFY(reader);
        QQuickItem *view = nullptr;
        QTRY_VERIFY((view = itemNamed(window->contentItem(), u"reader-view"_s)));
        QTRY_VERIFY(!reader->loading() && reader->pageCount() == 190);
        QQuickItem *panel = openTheVolumePanel(window, view);
        QVERIFY(panel);

        const auto inView = [&](int chapter) {
            QQuickItem *row = itemNamed(panel, u"reader-volume-chapter-%1"_s.arg(chapter));
            if (!row)
                return false;
            const qreal top = row->mapToScene(QPointF(0, 0)).y();
            return top >= panel->mapToScene(QPointF(0, 0)).y()
                   && top + row->height()
                          <= panel->mapToScene(QPointF(0, panel->height())).y() + 1;
        };
        QCOMPARE(textNamed(panel, u"reader-volume-head"_s), u"Tome 12"_s);
        QQuickItem *sheet = itemNamed(panel, u"reader-volume-sheet"_s);
        QVERIFY(sheet);
        // Page 46 is in chapter 11: lit, and left where it is, under the cover and the facts.
        QTRY_VERIFY(itemNamed(panel, u"reader-volume-chapter-11"_s));
        QTRY_VERIFY(itemNamed(panel, u"reader-volume-chapter-11"_s)->property("current").toBool());
        QTRY_VERIFY(sheet->property("contentHeight").toReal() > sheet->height());
        QVERIFY(aFrameDrawn(window));
        QTest::qWait(100);
        QCOMPARE(sheet->property("contentY").toReal(), 0.0);
        QVERIFY(!inView(37));
        // A turn within a chapter moves nothing but the lit row: the rows are the same items.
        QQuickItem *rowBefore = itemNamed(panel, u"reader-volume-chapter-11"_s);
        reader->goToPage(150);
        QTRY_VERIFY(inView(37));
        QVERIFY(sheet->property("contentY").toReal() > 0);
        QCOMPARE(itemNamed(panel, u"reader-volume-chapter-11"_s), rowBefore);
        QVERIFY(itemNamed(panel, u"reader-volume-chapter-37"_s)->property("current").toBool());

        // Closed, and opened again: at its top, the reader still in chapter 37.
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          centreOf(itemNamed(panel, u"reader-volume-close"_s)));
        QTRY_VERIFY(!panel->isVisible());
        QVERIFY(openTheVolumePanel(window, view));
        QVERIFY(aFrameDrawn(window));
        QCOMPARE(sheet->property("contentY").toReal(), 0.0);
        QVERIFY(itemNamed(panel, u"reader-volume-chapter-37"_s)->property("current").toBool());
    }

    /// An ISBN that does not add up — 978-2-505-06215 checks to 8, not 9 — is no barcode: it
    /// stays a line, in the same place as every other, shown as the file wrote it.
    void an_isbn_that_does_not_add_up_stays_a_line()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        BookExtras extras;
        extras.volume = QJsonObject{{u"isbn"_s, u"978-2-505-06215-9"_s}};
        pretend.answerFor = [&extras](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s, nullptr, u"manga"_s, nullptr,
                                nullptr, &extras);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        Reader *reader = readerOpenOnTheBook(engine);
        QVERIFY(reader);
        QQuickItem *view = nullptr;
        QTRY_VERIFY((view = itemNamed(window->contentItem(), u"reader-view"_s)));
        QTRY_VERIFY(!reader->loading() && reader->pageCount() == 190);
        QQuickItem *panel = openTheVolumePanel(window, view);
        QVERIFY(panel);
        QCOMPARE(textNamed(panel, u"reader-volume-isbn"_s), u"978-2-505-06215-9"_s);
        QVERIFY(drawn(panel, u"reader-volume-isbn-name"_s));
        QVERIFY(!drawn(panel, u"reader-volume-isbn-key"_s));
        QVERIFY(!drawn(panel, u"reader-volume-published"_s));
        QVERIFY(!drawn(panel, u"reader-volume-summary"_s));
    }

    /// An ISBN that adds up is the key the owner chose on 22/10/2026: bars and the number as
    /// the file hyphenated it, the last thing in the sheet after the chapters and scrolled
    /// with them — never over them, as when it stood at the foot of the card and cut the
    /// seventeenth chapter in half (the test of 22/10/2026) — and no line among the facts. A click copies the thirteen digits and a bubble says so;
    /// Enter does the same from the keyboard.
    void a_volume_with_an_isbn_shows_it_as_a_key_that_copies_it()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        BookExtras extras;
        extras.volume = QJsonObject{{u"isbn"_s, u"978-2-505-06533-3"_s},
                                    {u"summary"_s, u"Les examens approchent."_s}};
        // Long enough that the sheet scrolls: the key comes after its last row.
        for (int i = 0; i < 40; ++i)
            extras.chapters.append(QJsonObject{
                {u"id"_s, u"c%1"_s.arg(i)}, {u"label"_s, u"Chap.%1"_s.arg(i)},
                {u"number"_s, i}, {u"kind"_s, u"CHAPTER"_s}, {u"entryId"_s, u"e12"_s},
                {u"startPage"_s, i * 4}});
        pretend.answerFor = [&extras](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s, nullptr, u"manga"_s, nullptr,
                                nullptr, &extras);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        Reader *reader = readerOpenOnTheBook(engine);
        QVERIFY(reader);
        QQuickItem *view = nullptr;
        QTRY_VERIFY((view = itemNamed(window->contentItem(), u"reader-view"_s)));
        QTRY_VERIFY(!reader->loading() && reader->pageCount() == 190);
        QQuickItem *panel = openTheVolumePanel(window, view);
        QVERIFY(panel);
        QTRY_COMPARE(panel->opacity(), 1.0);

        QQuickItem *key = itemNamed(panel, u"reader-volume-isbn-key"_s);
        QQuickItem *sheet = itemNamed(panel, u"reader-volume-sheet"_s);
        QQuickItem *bars = itemNamed(panel, u"reader-volume-isbn-bars"_s);
        QVERIFY(key && sheet && bars);
        QVERIFY(key->isVisible());
        QVERIFY(!drawn(panel, u"reader-volume-isbn"_s));
        QVERIFY(!drawn(panel, u"reader-volume-isbn-name"_s));
        QCOMPARE(textNamed(panel, u"reader-volume-isbn-line-0"_s), u"978-2-505"_s);
        QCOMPARE(textNamed(panel, u"reader-volume-isbn-line-1"_s), u"06533-3"_s);
        QCOMPARE(textNamed(panel, u"reader-volume-isbn-label"_s), u"ISBN"_s);
        QCOMPARE(textNamed(panel, u"reader-volume-isbn-hint"_s), u"Cliquer pour copier"_s);
        // Dark bars on a lighter face in both themes, or the code reads inverted.
        auto *theme = engine.singletonInstance<Theme *>(qmlTypeId("Leaf", 1, 0, "Theme"));
        QVERIFY(theme);
        QQuickItem *face = itemNamed(panel, u"reader-volume-isbn-face"_s);
        QVERIFY(face);
        const bool wasDark = theme->dark();
        for (const bool dark : {false, true}) {
            theme->setDark(dark);
            QQuickItem *bar = nullptr;
            for (QQuickItem *one : bars->childItems())
                if (one->inherits("QQuickRectangle"))
                    bar = one;
            QVERIFY(bar);
            QTRY_VERIFY2(bar->property("color").value<QColor>().lightnessF()
                             < face->property("color").value<QColor>().lightnessF(),
                         dark ? "dark" : "light");
        }
        theme->setDark(wasDark);
        // Thirty bars, as every EAN-13 has; the Repeater that made them is an item too.
        const QList<QQuickItem *> drawnBars = bars->childItems();
        QCOMPARE(std::count_if(drawnBars.begin(), drawnBars.end(),
                               [](QQuickItem *one) { return one->inherits("QQuickRectangle"); }),
                 30);

        // The key is in the scrolled content, after the last chapter; the sheet runs to the
        // foot of the card. Scrolled to the end, the key is whole inside the sheet.
        QVERIFY(sheet->property("contentHeight").toReal() > sheet->height());
        auto *content = sheet->property("contentItem").value<QQuickItem *>();
        QVERIFY(content);
        QVERIFY(content->isAncestorOf(key));
        QQuickItem *last = itemNamed(panel, u"reader-volume-chapter-39"_s);
        QVERIFY(last);
        QVERIFY(key->mapToItem(content, QPointF(0, 0)).y()
                >= last->mapToItem(content, QPointF(0, last->height())).y());
        QVERIFY(qAbs(placed(panel).bottom() - panel->property("padding").toReal()
                     - placed(sheet).bottom())
                < 1);
        sheet->setProperty("contentY", sheet->property("contentHeight").toReal()
                                           - sheet->height());
        QVERIFY(aFrameDrawn(window));
        QVERIFY(placed(sheet).contains(placed(key)));
        QVERIFY(placed(key).left() > placed(panel).left());
        QVERIFY(placed(key).right() < placed(panel).right());

        auto *bubbles = engine.singletonInstance<Toasts *>(qmlTypeId("Leaf", 1, 0, "Toasts"));
        QVERIFY(bubbles);
        QClipboard *board = QGuiApplication::clipboard();
        board->setText(u"rien"_s);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centreOf(key));
        QTRY_COMPARE(board->text(), u"9782505065333"_s);
        QTRY_COMPARE(bubbles->count(), 1);
        QCOMPARE(bubbles->data(bubbles->index(0, 0), qToUnderlying(Toasts::Role::Headline))
                     .toString(),
                 u"ISBN copié"_s);

        board->setText(u"rien"_s);
        key->forceActiveFocus();
        QTRY_VERIFY(key->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(board->text(), u"9782505065333"_s);
        board->setText(u"rien"_s);
        QTest::keyClick(window, Qt::Key_Space);
        QTRY_COMPARE(board->text(), u"9782505065333"_s);
    }

    /// The key follows the window as the rest of the card does: its words at what their role
    /// makes of their drawn size, its bars a whole number of the screen's pixels per module,
    /// and inside the sheet — at 1280, where it is drawn, and at the 1920.
    void the_isbn_key_follows_the_window()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        BookExtras extras;
        extras.volume = QJsonObject{{u"isbn"_s, u"9782505065333"_s}};
        pretend.answerFor = [&extras](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s, nullptr, u"manga"_s, nullptr,
                                nullptr, &extras);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();
        const auto putBack = qScopeGuard([&] { window->resize(1100, 760); });
        QQuickItem *panel = itemNamed(view, u"reader-volume"_s);
        QQuickItem *key = itemNamed(view, u"reader-volume-isbn-key"_s);
        QQuickItem *sheet = itemNamed(view, u"reader-volume-sheet"_s);
        QQuickItem *bars = itemNamed(view, u"reader-volume-isbn-bars"_s);
        QQuickItem *number = itemNamed(view, u"reader-volume-isbn-number"_s);
        QQuickItem *label = itemNamed(view, u"reader-volume-isbn-label"_s);
        QQuickItem *line = itemNamed(view, u"reader-volume-isbn-line-0"_s);
        QVERIFY(panel && key && sheet && bars && number && label && line);
        auto *content = sheet->property("contentItem").value<QQuickItem *>();
        QVERIFY(content && content->isAncestorOf(key));
        // Bare digits: grouped, never hyphenated by guess.
        QCOMPARE(line->property("text").toString(), u"978 2505"_s);

        const qreal ratio = window->devicePixelRatio();
        for (const AWindow &at : {AWindow{1280, 800}, AWindow{1920, 1200}, AWindow{560, 700}}) {
            QVERIFY(laidOutFor(view, at));
            const QString where = u"%1 × %2"_s.arg(at.width).arg(at.height);
            const qreal factor = Widths::controlsFor(at.width);
            const int padding = Scale::sizeFor(18, factor);
            QTRY_VERIFY2(qAbs(key->width() - (panel->width() - 2 * padding)) < 0.01,
                         qPrintable(where));
            QCOMPARE(lettersOf(label), Scale::captionFor(11, factor));
            QCOMPARE(lettersOf(line), Scale::titleFor(19, factor));
            QCOMPARE(key->property("radius").toReal(), qreal(Scale::sizeFor(12, factor)));
            // A module in whole pixels of the screen, as drawn and scaled and rounded up, and
            // never under one.
            const qreal module = bars->width() / 95;
            QVERIFY2(module * ratio >= 1, qPrintable(where));
            QCOMPARE(module * ratio, std::round(module * ratio));
            if (at.width >= 1280)
                QCOMPARE(module, std::max(1.0, std::ceil(1.6 * factor * ratio)) / ratio);
            // The bars and the number inside the face, beside one another or one under the
            // other; and the sheet, scrolled to its end, holding the key whole.
            const QRectF face = placed(key);
            QVERIFY2(face.contains(placed(bars)), qPrintable(where));
            QVERIFY2(face.contains(placed(number)), qPrintable(where));
            QVERIFY2(!placed(bars).intersects(placed(number)), qPrintable(where));
            sheet->setProperty("contentY",
                               std::max(0.0, sheet->property("contentHeight").toReal()
                                                 - sheet->height()));
            QVERIFY2(placed(sheet).contains(placed(key)), qPrintable(where));
            sheet->setProperty("contentY", 0.0);
        }
    }

    /// A volume nobody has opened has no « Lu » line and no bar on its cover.
    void a_volume_never_opened_has_no_read_line()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        BookExtras extras;
        extras.unopened = true;
        pretend.answerFor = [&extras](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s, nullptr, u"manga"_s, nullptr,
                                nullptr, &extras);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        Reader *reader = readerOpenOnTheBook(engine);
        QVERIFY(reader);
        QQuickItem *view = nullptr;
        QTRY_VERIFY((view = itemNamed(window->contentItem(), u"reader-view"_s)));
        QTRY_VERIFY(!reader->loading() && reader->pageCount() == 190);
        QQuickItem *panel = openTheVolumePanel(window, view);
        QVERIFY(panel);
        QVERIFY(!drawn(panel, u"reader-volume-read"_s));
        QVERIFY(!drawn(panel, u"reader-volume-read-name"_s));
        QVERIFY(!drawn(panel, u"read-so-far-e12"_s));
        QVERIFY(!drawn(panel, u"reader-volume-read-bar"_s));
    }

    /// The settings card and the volume's panel are one or the other: opening either puts
    /// the other away, and the button of the one put away goes out.
    void the_settings_card_and_the_volume_panel_never_stand_together()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        Reader *reader = readerOpenOnTheBook(engine);
        QVERIFY(reader);
        QQuickItem *view = nullptr;
        QTRY_VERIFY((view = itemNamed(window->contentItem(), u"reader-view"_s)));
        QTRY_VERIFY(!reader->loading() && reader->pageCount() == 190);

        QQuickItem *panel = openTheVolumePanel(window, view);
        QVERIFY(panel);
        QQuickItem *card = itemNamed(view, u"reader-settings"_s);
        QVERIFY(card);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          centreOf(itemNamed(view, u"reader-settings-button"_s)));
        QTRY_VERIFY(card->isVisible());
        QTRY_VERIFY(!panel->isVisible());
        QCOMPARE(itemNamed(view, u"reader-volume-button"_s)->property("held").toBool(), false);

        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          centreOf(itemNamed(view, u"reader-volume-button"_s)));
        QTRY_VERIFY(panel->isVisible());
        QTRY_VERIFY(!card->isVisible());
        QCOMPARE(itemNamed(view, u"reader-settings-button"_s)->property("held").toBool(), false);
    }

    /// A strip has no sense and no pair: the card keeps only the fit, and of the fit only the
    /// two that mean something for an endless column — « Hauteur » would be the height of
    /// a thing with none. « Original » draws each page at the width of its file, centred.
    void a_webtoon_offers_only_the_fit()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"VERTICAL"_s, nullptr, u"webtoon"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *preferences =
            engine.singletonInstance<Preferences *>(qmlTypeId("Leaf", 1, 0, "Preferences"));
        QVERIFY(preferences);
        const auto putBack = qScopeGuard([&] {
            preferences->chooseFit(Preferences::Fit::Height);
        });
        preferences->chooseFit(Preferences::Fit::Height);
        Reader *reader = readerOpenOnTheBook(engine);
        QVERIFY(reader);
        QQuickItem *view = nullptr;
        QTRY_VERIFY((view = itemNamed(window->contentItem(), u"reader-view"_s)));
        QTRY_VERIFY(!reader->loading() && reader->pageCount() == 190);
        QQuickItem *strip = nullptr;
        QTRY_VERIFY((strip = itemNamed(view, u"reader-strip"_s)) && strip->isVisible());

        QQuickItem *card = openTheSettings(window, view);
        QVERIFY(card);
        const auto shown = [&](const QString &name) {
            QQuickItem *one = itemNamed(card, name);
            return one && one->isVisible();
        };
        QVERIFY(!shown(u"reader-direction"_s));
        QVERIFY(!shown(u"reader-direction-1"_s));
        QVERIFY(!shown(u"reader-layout"_s));
        QVERIFY(!shown(u"reader-declares"_s));
        QVERIFY(!shown(u"reader-fit-height"_s));
        QVERIFY(shown(u"reader-fit-width"_s));
        QVERIFY(shown(u"reader-fit-original"_s));
        // A fit chosen for books that means nothing here is shown as the one it behaves as.
        QVERIFY(itemNamed(card, u"reader-fit-width"_s)->property("lit").toBool());

        // « Largeur » is the strip as it was: every page the width of the window.
        QTRY_VERIFY(itemNamed(strip, u"strip-image-46"_s));
        QCOMPARE(itemNamed(strip, u"strip-image-46"_s)->width(), strip->width());

        // « Original » is the file's own width — 1000 in a window of 1100 — centred.
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          centreOf(itemNamed(card, u"reader-fit-original"_s)));
        QTRY_COMPARE(preferences->fit(), Preferences::Fit::Original);
        QQuickItem *image = itemNamed(strip, u"strip-image-46"_s);
        QVERIFY(image);
        QTRY_COMPARE(image->width(), 1000.0);
        QCOMPARE(image->x(), (strip->width() - 1000) / 2);
        QCOMPARE(image->height(), 1500.0);
        // A file wider than the window is held to the window, not cut by it.
        window->resize(800, window->height());
        QTRY_COMPARE(strip->width(), 800.0);
        QTRY_COMPARE(itemNamed(strip, u"strip-image-46"_s)->width(), 800.0);

        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          centreOf(itemNamed(card, u"reader-fit-width"_s)));
        QTRY_COMPARE(preferences->fit(), Preferences::Fit::Width);
        QTRY_COMPARE(itemNamed(strip, u"strip-image-46"_s)->width(), strip->width());
    }

    /// Narrowing the window re-measures every page, and a strip left alone stays at the same
    /// pixel offset while the pages above it get shorter: the page being read slid up out of
    /// the top. It is put back at the top, as the scrub puts it there.
    void narrowing_the_window_keeps_the_page_being_read_at_the_top_of_a_strip()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"VERTICAL"_s, nullptr, u"webtoon"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        Reader *reader = readerOpenOnTheBook(engine);
        QVERIFY(reader);
        QQuickItem *view = nullptr;
        QTRY_VERIFY((view = itemNamed(window->contentItem(), u"reader-view"_s)));
        QTRY_VERIFY(!reader->loading() && reader->pageCount() == 190);
        QQuickItem *strip = nullptr;
        QTRY_VERIFY((strip = itemNamed(view, u"reader-strip"_s)) && strip->isVisible());
        const auto putBack = qScopeGuard([&] { window->resize(1100, 760); });

        reader->goToPage(120);
        QTRY_COMPARE(stripPageAt(strip, strip->property("contentY").toReal() + 1), 120);
        QTRY_VERIFY(!strip->property("moving").toBool());
        // Part-way down the page, as a reader is: a list keeps the offset inside the page it
        // keeps on screen, which is not the page's top.
        wheel(window, across(view, 0.5), QPoint(0, -120));
        QTRY_VERIFY(!strip->property("moving").toBool());
        QQuickItem *page = itemNamed(strip, u"strip-page-120"_s);
        QVERIFY(page);
        const auto offsetInPage = [&] {
            return strip->property("contentY").toReal() - page->y();
        };
        QTRY_VERIFY(offsetInPage() > 50);
        QCOMPARE(reader->firstPage(), 120);

        window->resize(700, window->height());
        QTRY_COMPARE(strip->width(), 700.0);
        QTRY_COMPARE(offsetInPage(), 0.0);
        QCOMPARE(reader->firstPage(), 120);
    }

    /// Ctrl + zooms a step at a time, and a zoomed page is moved by the arrows rather than
    /// turned; Ctrl − steps back, Ctrl 0 goes straight back to the whole page, and whole, the
    /// arrow turns again (spec §09).
    void ctrl_plus_zooms_and_the_arrow_then_moves_the_page()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"LEFT_TO_RIGHT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();
        QQuickItem *sheet = itemNamed(view, u"reader-sheet"_s);
        QVERIFY(sheet);
        QCOMPARE(zoomOf(view), 1.0);
        const int before = reader->display();

        // Three steps, 150 then 200 then 300 %: a portrait page in a landscape window runs
        // off the sides only from there, and the arrows have somewhere to move it.
        QTest::keyClick(window, Qt::Key_Plus, Qt::ControlModifier);
        QTRY_COMPARE(zoomOf(view), 1.5);
        QTest::keyClick(window, Qt::Key_Plus, Qt::ControlModifier);
        QTRY_COMPARE(zoomOf(view), 2.0);
        QTest::keyClick(window, Qt::Key_Plus, Qt::ControlModifier);
        QTRY_COMPARE(zoomOf(view), 3.0);
        QTRY_VERIFY(sheet->property("contentWidth").toReal() > sheet->width());
        QTRY_VERIFY(sheet->property("contentHeight").toReal() > sheet->height());

        const qreal x = sheet->property("contentX").toReal();
        QTest::keyClick(window, Qt::Key_Right);
        QTRY_VERIFY(sheet->property("contentX").toReal() > x);
        const qreal y = sheet->property("contentY").toReal();
        QTest::keyClick(window, Qt::Key_Down);
        QTRY_VERIFY(sheet->property("contentY").toReal() > y);
        QCOMPARE(reader->display(), before);

        QTest::keyClick(window, Qt::Key_Minus, Qt::ControlModifier);
        QTRY_COMPARE(zoomOf(view), 2.0);
        QTest::keyClick(window, Qt::Key_0, Qt::ControlModifier);
        QTRY_COMPARE(zoomOf(view), 1.0);
        QCOMPARE(reader->display(), before);
        QTest::keyClick(window, Qt::Key_Right);
        QTRY_COMPARE(reader->display(), before + 1);
    }

    /// The arrow moves a zoomed page until its edge is in view, and the next press turns it;
    /// the space bar goes down the page the same way, and turns at its foot.
    void at_the_edge_of_a_zoomed_page_the_arrow_turns_it()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"LEFT_TO_RIGHT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();
        QQuickItem *sheet = itemNamed(view, u"reader-sheet"_s);
        QVERIFY(sheet);
        const int before = reader->display();

        for (int step = 0; step < 3; ++step)
            QTest::keyClick(window, Qt::Key_Plus, Qt::ControlModifier);
        QTRY_COMPARE(zoomOf(view), 3.0);
        QVERIFY(!sheet->property("atXEnd").toBool());
        QVERIFY(pressedToTheEdge(window, view, Qt::Key_Right, "atXEnd"));
        QCOMPARE(reader->display(), before);
        // A zoomed page turned does not slide: nothing moves while the page is enlarged.
        QObject *slide = view->findChild<QObject *>(u"reader-turn"_s);
        QObject *fade = view->findChild<QObject *>(u"reader-fade"_s);
        QVERIFY(slide && fade);
        QSignalSpy slid(slide, SIGNAL(started()));
        QSignalSpy faded(fade, SIGNAL(started()));
        QTest::keyClick(window, Qt::Key_Right);
        QTRY_COMPARE(reader->display(), before + 1);
        QVERIFY(aFrameDrawn(window));
        QCOMPARE(slid.count(), 0);
        QCOMPARE(faded.count(), 0);

        QTest::keyClick(window, Qt::Key_Plus, Qt::ControlModifier);
        QTRY_COMPARE(zoomOf(view), 1.5);
        QVERIFY(!sheet->property("atYEnd").toBool());
        QVERIFY(pressedToTheEdge(window, view, Qt::Key_Space, "atYEnd"));
        QCOMPARE(reader->display(), before + 1);
        QTest::keyClick(window, Qt::Key_Space);
        QTRY_COMPARE(reader->display(), before + 2);
    }

    /// A turn slides the screen that comes in from the side reading goes towards, and the one
    /// left out the other way: from the left going on in a manga, from the right in a book
    /// read left to right, and the reverse going back. Tried by hand on 22/10/2026, a page
    /// replaced in one frame read as a flash. The screen leaving is drawn from the image
    /// cache: its page is never asked of the server twice.
    void a_turn_slides_in_from_the_side_reading_goes_towards()
    {
        for (const QString &direction : {u"RIGHT_TO_LEFT"_s, u"LEFT_TO_RIGHT"_s}) {
            Pretend pretend;
            QVERIFY(pretend.listen(QHostAddress::LocalHost));
            pretend.answerFor = [direction](const QByteArray &request) {
                return aBookAnswers(request, direction);
            };
            qputenv("LEAF_ADDRESS",
                    u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

            QQmlApplicationEngine engine;
            Boot::run(engine, *qGuiApp, nowhere());
            Reader *reader = nullptr;
            QQuickItem *view = aReaderAtHand(engine, reader);
            QVERIFY(view);
            auto *window = view->window();
            const bool manga = direction == u"RIGHT_TO_LEFT"_s;
            QObject *slide = view->findChild<QObject *>(u"reader-turn"_s);
            QObject *in = view->findChild<QObject *>(u"reader-turn-in"_s);
            QObject *out = view->findChild<QObject *>(u"reader-turn-out"_s);
            QQuickItem *ghost = itemNamed(view, u"reader-ghost"_s);
            QObject *shift = view->findChild<QObject *>(u"reader-sheet-shift"_s);
            QVERIFY(slide && in && out && ghost && shift);
            QSignalSpy slid(slide, SIGNAL(started()));
            const int left = reader->firstPage();
            QTRY_VERIFY(itemNamed(view, u"reader-page-image-%1"_s.arg(left))
                            ->property("status").toInt() == 1);
            const QByteArray asked =
                "GET /entries/e12/pages/" + QByteArray::number(left) + "?";
            QCOMPARE(pretend.heard.count(asked), 1);

            // Going on: the arrow on the side the next page lies.
            const int before = reader->display();
            QTest::keyClick(window, manga ? Qt::Key_Left : Qt::Key_Right);
            QTRY_COMPARE(reader->display(), before + 1);
            QTRY_COMPARE(slid.count(), 1);
            QCOMPARE(in->property("property").toString(), u"x"_s);
            const qreal from = in->property("from").toReal();
            QVERIFY2(manga ? from < 0 : from > 0, qPrintable(QString::number(from)));
            QCOMPARE(in->property("to").toReal(), 0.0);
            QCOMPARE(out->property("to").toReal(), -from);
            // What leaves is the screen that was there.
            QCOMPARE(ghost->property("first").toInt(), left);
            QTRY_VERIFY(!slide->property("running").toBool());
            QCOMPARE(shift->property("x").toReal(), 0.0);
            QVERIFY(!ghost->isVisible());
            QCOMPARE(pretend.heard.count(asked), 1);
            // And the page turned to was fetched ahead once, and drawn from that.
            const QByteArray arrived =
                "GET /entries/e12/pages/" + QByteArray::number(reader->firstPage()) + "?";
            QCOMPARE(pretend.heard.count(arrived), 1);

            // Going back: the other side.
            QTest::keyClick(window, manga ? Qt::Key_Right : Qt::Key_Left);
            QTRY_COMPARE(reader->display(), before);
            QTRY_COMPARE(slid.count(), 2);
            const qreal back = in->property("from").toReal();
            QVERIFY2(manga ? back > 0 : back < 0, qPrintable(QString::number(back)));
            QTRY_VERIFY(!slide->property("running").toBool());
            QCOMPARE(pretend.heard.count(asked), 1);
        }
    }

    /// Turns made faster than they slide never queue: each ends the one running, and three
    /// quick turns land on the third screen with nothing left moving.
    void three_quick_turns_end_on_the_third_screen()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"LEFT_TO_RIGHT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();
        QObject *slide = view->findChild<QObject *>(u"reader-turn"_s);
        QObject *shift = view->findChild<QObject *>(u"reader-sheet-shift"_s);
        QQuickItem *ghost = itemNamed(view, u"reader-ghost"_s);
        QVERIFY(slide && shift && ghost);
        const int before = reader->display();

        for (int i = 0; i < 3; ++i)
            QTest::keyClick(window, Qt::Key_Right);
        QTRY_COMPARE(reader->display(), before + 3);
        QTRY_VERIFY(!slide->property("running").toBool()
                    && view->property("pending").toString().isEmpty());
        QCOMPARE(shift->property("x").toReal(), 0.0);
        QVERIFY(!ghost->isVisible());
        QQuickItem *page = itemNamed(view, u"reader-page-%1"_s.arg(reader->firstPage()));
        QVERIFY(page && page->isVisible());
        const QPointF at = page->mapToScene(QPointF(0, 0));
        QVERIFY(at.x() >= 0 && at.x() < view->width());
    }

    /// A jump — here the progress bar's — fades across and slides nothing.
    void a_jump_fades_and_slides_nothing()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"LEFT_TO_RIGHT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        QObject *slide = view->findChild<QObject *>(u"reader-turn"_s);
        QObject *fade = view->findChild<QObject *>(u"reader-fade"_s);
        QQuickItem *sheet = itemNamed(view, u"reader-sheet"_s);
        QVERIFY(slide && fade && sheet);
        QSignalSpy slid(slide, SIGNAL(started()));
        QSignalSpy faded(fade, SIGNAL(started()));

        reader->goToPage(100);
        QTRY_COMPARE(faded.count(), 1);
        QTRY_VERIFY(!fade->property("running").toBool());
        QCOMPARE(slid.count(), 0);
        QCOMPARE(sheet->opacity(), 1.0);
    }

    /// « Sur cet appareil » names what each of its rows chooses, under « Pages » and
    /// « Confort »: the critique of 22/10/2026 was four anonymous rows of pills. The
    /// names stand in one column with every row's pills starting at one edge; the pills are
    /// words alone, the paper's three showing the paper; and on a card too narrow for both
    /// side by side, each name goes above its pills.
    void the_desk_rows_are_named_beside_their_pills_and_above_them_when_narrow()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"LEFT_TO_RIGHT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *theme = engine.singletonInstance<Theme *>(qmlTypeId("Leaf", 1, 0, "Theme"));
        QVERIFY(theme);
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();
        const auto putBack = qScopeGuard([&] { window->resize(1100, 760); });
        window->resize(1280, 900);
        QTRY_COMPARE(view->width(), 1280.0);
        QQuickItem *card = openTheSettings(window, view);
        QVERIFY(card);

        using Group = Words::ReaderDeskGroup;
        using Row = Words::ReaderSetting;
        QCOMPARE(textNamed(card, u"reader-pages-heading"_s), Words::readerDeskGroup(Group::Pages));
        QCOMPARE(textNamed(card, u"reader-comfort-heading"_s),
                 Words::readerDeskGroup(Group::Comfort));
        const QList<std::pair<QString, Row>> rows = {{u"reader-layout"_s, Row::Layout},
                                                      {u"reader-fit"_s, Row::Fit},
                                                      {u"reader-backdrop"_s, Row::Backdrop},
                                                      {u"reader-motion"_s, Row::Motion}};
        for (const auto &[key, row] : rows)
            QCOMPARE(textNamed(card, key + u"-name"_s), Words::readerSetting(row));
        for (const QString &name : {u"reader-pages-heading"_s, u"reader-comfort-heading"_s,
                                    u"reader-layout-name"_s, u"reader-fit-name"_s,
                                    u"reader-backdrop-name"_s, u"reader-motion-name"_s}) {
            QQuickItem *text = itemNamed(card, name);
            QVERIFY2(text && text->isVisible() && text->width() > 0, qPrintable(name));
        }

        // Beside: each name left of its pills, and the pills of all four rows on one edge.
        const auto pillsOf = [&](const QString &key) {
            // The row's own `SettingsChoice`, and not the reader's paper of the same name.
            return itemNamed(itemNamed(card, key + u"-row"_s), key);
        };
        QVERIFY(!card->property("stacked").toBool());
        const qreal edge = placed(pillsOf(u"reader-layout"_s)).left();
        for (const auto &[key, row] : rows) {
            const QRectF name = placed(itemNamed(card, key + u"-name"_s));
            const QRectF pills = placed(pillsOf(key));
            QVERIFY2(name.right() <= pills.left(), qPrintable(key));
            QVERIFY2(qAbs(pills.left() - edge) < 0.5, qPrintable(key));
            QVERIFY2(name.top() < pills.bottom() && pills.top() < name.bottom(), qPrintable(key));
        }

        // Words alone: no picto in any of the twelve pills, and the paper's three show it.
        const QStringList pills = {u"reader-layout-single"_s,   u"reader-layout-double"_s,
                                   u"reader-layout-auto"_s,     u"reader-fit-height"_s,
                                   u"reader-fit-width"_s,       u"reader-fit-original"_s,
                                   u"reader-backdrop-black"_s,  u"reader-backdrop-grey"_s,
                                   u"reader-backdrop-white"_s,  u"reader-motion-slide"_s,
                                   u"reader-motion-fade"_s,     u"reader-motion-instant"_s};
        for (const QString &name : pills) {
            QQuickItem *pill = itemNamed(card, name);
            QVERIFY2(pill && pill->isVisible(), qPrintable(name));
            QVERIFY2(!pill->property("pictured").toBool(), qPrintable(name));
            for (const QQuickItem *inside : pill->childItems())
                QVERIFY2(!QString::fromUtf8(inside->metaObject()->className()).startsWith(u"Glyph")
                             || !inside->isVisible(),
                         qPrintable(name));
        }
        QCOMPARE(textNamed(card, u"reader-motion-instant-text"_s),
                 Words::readerMotion(Preferences::Motion::Instant));
        const QList<std::pair<QString, QColor>> papers = {
            {u"reader-backdrop-black-swatch"_s, theme->readerBlack()},
            {u"reader-backdrop-grey-swatch"_s, theme->readerGrey()},
            {u"reader-backdrop-white-swatch"_s, theme->readerWhite()}};
        for (const auto &[name, colour] : papers) {
            QQuickItem *swatch = itemNamed(card, name);
            QVERIFY2(swatch && swatch->isVisible(), qPrintable(name));
            QCOMPARE(swatch->property("color").value<QColor>(), colour);
        }
        QVERIFY(!itemNamed(card, u"reader-fit-height-swatch"_s)->isVisible());

        // Narrow: each name above its pills, both on the card's left edge. At 420 the three
        // pills of a row still stand beside a name; at 360 they no longer do.
        window->resize(420, 760);
        QTRY_COMPARE(view->width(), 420.0);
        QVERIFY(!card->property("stacked").toBool());
        window->resize(360, 760);
        QTRY_COMPARE(view->width(), 360.0);
        QTRY_VERIFY(card->property("stacked").toBool());
        for (const auto &[key, row] : rows) {
            const QRectF name = placed(itemNamed(card, key + u"-name"_s));
            const QRectF pills = placed(pillsOf(key));
            QVERIFY2(name.bottom() <= pills.top(), qPrintable(key));
            QVERIFY2(qAbs(name.left() - pills.left()) < 0.5, qPrintable(key));
            QVERIFY2(pills.right() <= placed(card).right(), qPrintable(key));
        }
    }

    /// The backdrop and the motion chosen on the card are this desk's: lit when the book is
    /// opened again, and read back from the file by a later run. « Blanc » paints what is
    /// behind the pages — sampled in the margin beside them — and not only a property.
    void the_backdrop_and_the_motion_are_kept_when_the_book_is_opened_again()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"LEFT_TO_RIGHT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *preferences =
            engine.singletonInstance<Preferences *>(qmlTypeId("Leaf", 1, 0, "Preferences"));
        QVERIFY(preferences);
        const auto putBack = qScopeGuard([&] {
            preferences->chooseBackdrop(Preferences::Backdrop::Black);
            preferences->chooseMotion(Preferences::Motion::Slide);
        });
        preferences->chooseBackdrop(Preferences::Backdrop::Black);
        preferences->chooseMotion(Preferences::Motion::Slide);
        auto *theme = engine.singletonInstance<Theme *>(qmlTypeId("Leaf", 1, 0, "Theme"));
        QVERIFY(theme);
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();
        const auto sampled = [window](const QPointF &point) {
            const QImage rendered = window->grabWindow();
            if (rendered.isNull())
                return QColor();
            const qreal xScale = rendered.width() / qreal(window->width());
            const qreal yScale = rendered.height() / qreal(window->height());
            return rendered.pixelColor(qRound(point.x() * xScale), qRound(point.y() * yScale));
        };
        // The margin beside the page, halfway down: nothing but the backdrop is drawn there.
        const QPointF margin = view->mapToScene(QPointF(1, view->height() / 2.0));

        // « Noir » is chosen for the pages, not for the application: black in the light theme
        // too, where the reader's paper used to be cream.
        const bool wasDark = theme->dark();
        const auto themeBack = qScopeGuard([&] { theme->setDark(wasDark); });
        theme->setDark(false);
        QTRY_COMPARE(sampled(margin), theme->readerBlack());
        QVERIFY2(sampled(margin).lightness() < 20, qPrintable(sampled(margin).name()));

        QQuickItem *card = openTheSettings(window, view);
        QVERIFY(card);
        QVERIFY(aFrameDrawn(window));
        // A short window scrolls the card's groups: a pill is brought into sight before it is
        // aimed at, or the click lands on whatever is drawn where it would be.
        const auto aimAt = [&](const QString &name) {
            QQuickItem *pill = itemNamed(card, name);
            auto *sheet = qobject_cast<QQuickItem *>(itemNamed(card, u"reader-settings-sheet"_s));
            if (!pill || !sheet)
                return QPoint(-1, -1);
            auto *content = sheet->property("contentItem").value<QQuickItem *>();
            const qreal top = pill->mapToItem(content, QPointF(0, 0)).y();
            const qreal most =
                std::max(0.0, sheet->property("contentHeight").toReal() - sheet->height());
            sheet->setProperty("contentY", std::clamp(top - 10.0, 0.0, most));
            (void)aFrameDrawn(window);
            return centreOf(pill);
        };
        using B = Preferences::Backdrop;
        using M = Preferences::Motion;
        QCOMPARE(textNamed(card, u"reader-backdrop-black-text"_s), Words::readerBackdrop(B::Black));
        QCOMPARE(textNamed(card, u"reader-backdrop-grey-text"_s),
                 Words::readerBackdrop(B::Grey));
        QCOMPARE(textNamed(card, u"reader-backdrop-white-text"_s), Words::readerBackdrop(B::White));
        QCOMPARE(textNamed(card, u"reader-motion-slide-text"_s), Words::readerMotion(M::Slide));
        QCOMPARE(textNamed(card, u"reader-motion-fade-text"_s), Words::readerMotion(M::Fade));
        QCOMPARE(textNamed(card, u"reader-motion-instant-text"_s),
                 Words::readerMotion(M::Instant));
        QVERIFY(itemNamed(card, u"reader-backdrop-black"_s)->property("lit").toBool());
        QVERIFY(itemNamed(card, u"reader-motion-slide"_s)->property("lit").toBool());

        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          aimAt(u"reader-backdrop-white"_s));
        QTRY_COMPARE(preferences->backdrop(), B::White);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          aimAt(u"reader-motion-instant"_s));
        QTRY_COMPARE(preferences->motion(), M::Instant);
        QQuickItem *backdrop = itemNamed(view, u"reader-backdrop"_s);
        QVERIFY(backdrop);
        QCOMPARE(backdrop->property("color").value<QColor>(), theme->readerWhite());

        // Painted: the margin beside the page, with the card and the bars gone.
        view->setProperty("settingsOpen", false);
        QVERIFY(atRest(view));
        QTRY_COMPARE(sampled(margin), theme->readerWhite());

        // The book opened again finds both as they were left.
        auto *navigation =
            engine.singletonInstance<Navigation *>(qmlTypeId("Leaf", 1, 0, "Navigation"));
        navigation->back();
        QTRY_COMPARE(navigation->destination(), Navigation::Destination::Series);
        navigation->open(Navigation::Destination::Reader,
                         {{u"series"_s, u"s1"_s}, {u"entry"_s, u"e12"_s}});
        QTRY_VERIFY((view = itemNamed(window->contentItem(), u"reader-view"_s)));
        QTRY_VERIFY(!reader->loading() && reader->pageCount() == 190);
        card = openTheSettings(window, view);
        QVERIFY(card);
        QVERIFY(itemNamed(card, u"reader-backdrop-white"_s)->property("lit").toBool());
        QVERIFY(!itemNamed(card, u"reader-backdrop-black"_s)->property("lit").toBool());
        QVERIFY(itemNamed(card, u"reader-motion-instant"_s)->property("lit").toBool());
        QCOMPARE(itemNamed(view, u"reader-backdrop"_s)->property("color").value<QColor>(),
                 theme->readerWhite());

        // And a later run reads them from the file.
        const Preferences later;
        QCOMPARE(later.backdrop(), B::White);
        QCOMPARE(later.motion(), M::Instant);
    }

    /// « Sans effet »: a turn puts the next page up with nothing running — no slide, no fade — and
    /// still never on bare paper: until its images are there the new screen is hidden and the
    /// old one stands.
    void with_no_motion_a_turn_changes_the_page_and_moves_nothing()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"LEFT_TO_RIGHT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *preferences =
            engine.singletonInstance<Preferences *>(qmlTypeId("Leaf", 1, 0, "Preferences"));
        QVERIFY(preferences);
        const auto putBack =
            qScopeGuard([&] { preferences->chooseMotion(Preferences::Motion::Slide); });
        preferences->chooseMotion(Preferences::Motion::Instant);
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();
        QObject *slide = view->findChild<QObject *>(u"reader-turn"_s);
        QObject *fade = view->findChild<QObject *>(u"reader-fade"_s);
        QQuickItem *sheet = itemNamed(view, u"reader-sheet"_s);
        QQuickItem *ghost = itemNamed(view, u"reader-ghost"_s);
        QVERIFY(slide && fade && sheet && ghost);
        QSignalSpy slid(slide, SIGNAL(started()));
        QSignalSpy faded(fade, SIGNAL(started()));

        const int before = reader->display();
        QTest::keyClick(window, Qt::Key_Right);
        QCOMPARE(reader->display(), before + 1);
        // Shown only with its images: either they are there, or the screen is hidden.
        QVERIFY(view->property("pagesReady").toBool() || sheet->opacity() == 0.0);
        QTRY_VERIFY(view->property("pending").toString().isEmpty());
        QCOMPARE(sheet->opacity(), 1.0);
        QVERIFY(!ghost->isVisible());
        QVERIFY(!slide->property("running").toBool());
        QVERIFY(!fade->property("running").toBool());
        QCOMPARE(slid.count(), 0);
        QCOMPARE(faded.count(), 0);
        QQuickItem *page = itemNamed(view, u"reader-page-%1"_s.arg(reader->firstPage()));
        QVERIFY(page && page->isVisible() && page->property("loaded").toBool());

        // A jump neither.
        reader->goToPage(100);
        QTRY_VERIFY(reader->firstPage() == 100 || reader->secondPage() == 100);
        QTRY_VERIFY(view->property("pending").toString().isEmpty());
        QCOMPARE(sheet->opacity(), 1.0);
        QCOMPARE(slid.count(), 0);
        QCOMPARE(faded.count(), 0);
    }

    /// « Fondu »: a turn fades across as a jump does, and slides nothing.
    void with_the_fade_chosen_a_turn_fades()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"LEFT_TO_RIGHT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *preferences =
            engine.singletonInstance<Preferences *>(qmlTypeId("Leaf", 1, 0, "Preferences"));
        QVERIFY(preferences);
        const auto putBack =
            qScopeGuard([&] { preferences->chooseMotion(Preferences::Motion::Slide); });
        preferences->chooseMotion(Preferences::Motion::Fade);
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();
        QObject *slide = view->findChild<QObject *>(u"reader-turn"_s);
        QObject *fade = view->findChild<QObject *>(u"reader-fade"_s);
        QQuickItem *sheet = itemNamed(view, u"reader-sheet"_s);
        QVERIFY(slide && fade && sheet);
        QSignalSpy slid(slide, SIGNAL(started()));
        QSignalSpy faded(fade, SIGNAL(started()));

        const int before = reader->display();
        QTest::keyClick(window, Qt::Key_Right);
        QTRY_COMPARE(reader->display(), before + 1);
        QTRY_COMPARE(faded.count(), 1);
        QTRY_VERIFY(!fade->property("running").toBool());
        QCOMPARE(slid.count(), 0);
        QCOMPARE(sheet->opacity(), 1.0);
    }

    /// However a page is left — here from the progress bar — the one that comes is seen whole,
    /// from its top (spec §09).
    void a_turned_page_starts_whole()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        const auto single = onePageAtATime(engine);
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();
        QQuickItem *sheet = itemNamed(view, u"reader-sheet"_s);
        QVERIFY(sheet);

        QTest::keyClick(window, Qt::Key_Plus, Qt::ControlModifier);
        QTest::keyClick(window, Qt::Key_Plus, Qt::ControlModifier);
        QTRY_COMPARE(zoomOf(view), 2.0);
        const qreal y = sheet->property("contentY").toReal();
        QTest::keyClick(window, Qt::Key_Down);
        QTRY_VERIFY(sheet->property("contentY").toReal() > y);

        reader->goToPage(120);
        QTRY_COMPARE(reader->firstPage(), 120);
        QTRY_COMPARE(zoomOf(view), 1.0);
        QTRY_COMPARE(sheet->property("contentY").toReal(), 0.0);
        QTRY_COMPARE(sheet->property("contentX").toReal(), 0.0);
        QCOMPARE(sheet->property("contentHeight").toReal(), sheet->height());
    }

    /// A double click zooms with the point it aimed at staying under the pointer, and a
    /// second one gives the whole page back. Neither calls the bars, which a single click in
    /// the middle does.
    void a_double_click_zooms_where_it_points()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();
        // Narrow enough for a page at 200 % to run off both sides, which is where the point
        // aimed at can be held under the pointer across as well as down.
        const QSize sized = window->size();
        const auto putBack = qScopeGuard([&] { window->resize(sized); });
        window->resize(600, 760);
        QQuickItem *sheet = itemNamed(view, u"reader-sheet"_s);
        QVERIFY(sheet);
        QTRY_VERIFY(sheet->width() < 600);
        QVERIFY(aFrameDrawn(window));
        const int before = reader->display();

        // Off the middle on both axes, so that a zoom about the centre would move the point.
        const QPointF aimed = view->mapToScene(QPointF(view->width() * 0.45,
                                                       view->height() * 0.3));
        QPointF was;
        QQuickItem *page = nullptr;
        // Once the pages are laid out: the row places them a frame after they are made.
        QTRY_VERIFY((page = pageUnder(view, reader, aimed, was)));
        // And once the narrower window has given the stage its narrower margin: a page whose
        // height is fitted starts at the stage's top.
        QTRY_COMPARE(page->mapToScene(QPointF()).y(), sheet->mapToScene(QPointF()).y());
        QVERIFY(pageUnder(view, reader, aimed, was) == page);
        QTest::mouseDClick(window, Qt::LeftButton, Qt::NoModifier, aimed.toPoint());
        QTRY_COMPARE(zoomOf(view), 2.0);
        const QPointF now = page->mapFromScene(aimed);
        QVERIFY(sheet->property("contentWidth").toReal() > sheet->width());
        QVERIFY(sheet->property("contentHeight").toReal() > sheet->height());
        QVERIFY2(qAbs(now.x() - was.x()) < 1.5 && qAbs(now.y() - was.y()) < 1.5,
                 qPrintable(u"aimed at %1,%2 of the page, and %3,%4 is under the pointer"_s
                                .arg(was.x()).arg(was.y()).arg(now.x()).arg(now.y())));
        QCOMPARE(reader->display(), before);
        QObject *later = view->findChild<QObject *>(u"reader-bars-later"_s);
        QVERIFY(later);
        QVERIFY(!later->property("running").toBool());
        QVERIFY(!view->property("barsShown").toBool());

        QTest::mouseDClick(window, Qt::LeftButton, Qt::NoModifier, aimed.toPoint());
        QTRY_COMPARE(zoomOf(view), 1.0);
        QVERIFY(!later->property("running").toBool());
        QVERIFY(!view->property("barsShown").toBool());
        QCOMPARE(reader->display(), before);
    }

    /// The level reached is said in the bar at the foot of the page for a second, and then
    /// nothing is (spec §09) — with a narrow space before the sign, which never breaks.
    void the_zoom_level_is_said_in_the_bottom_bar_for_a_second()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();
        QQuickItem *dock = itemNamed(view, u"reader-dock"_s);
        QQuickItem *level = itemNamed(view, u"reader-zoom-level"_s);
        QVERIFY(dock && level);
        QVERIFY(!level->isVisible());

        QTest::keyClick(window, Qt::Key_Plus, Qt::ControlModifier);
        QTest::keyClick(window, Qt::Key_Plus, Qt::ControlModifier);
        QElapsedTimer since;
        since.start();
        QTRY_VERIFY(level->isVisible());
        QCOMPARE(level->property("text").toString(), u"200 %"_s);
        QVERIFY(dock->isAncestorOf(level));
        QTRY_VERIFY_WITH_TIMEOUT(!level->isVisible(), 5000);
        QVERIFY2(since.elapsed() >= 900, qPrintable(QString::number(since.elapsed())));
        QVERIFY(!view->property("barsShown").toBool());
    }

    /// Under the end card, the settings card or the volume's panel a zoom key does nothing to
    /// the page: what is up is what the reader is looking at.
    void a_zoom_key_under_a_card_leaves_the_page_alone()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();

        // Escape after the key, and its effect awaited: keys are handled in the order they
        // come, so once the card is gone the zoom key before it has been handled too.
        QQuickItem *settings = openTheSettings(window, view);
        QVERIFY(settings);
        QTest::keyClick(window, Qt::Key_Plus, Qt::ControlModifier);
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!settings->isVisible());
        QCOMPARE(zoomOf(view), 1.0);

        QQuickItem *panel = openTheVolumePanel(window, view);
        QVERIFY(panel);
        QTest::keyClick(window, Qt::Key_Plus, Qt::ControlModifier);
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!panel->isVisible());
        QCOMPARE(zoomOf(view), 1.0);

        QQuickItem *card = raiseTheEndCard(window, view, reader);
        QVERIFY(card);
        QTest::keyClick(window, Qt::Key_Plus, Qt::ControlModifier);
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!reader->atTheEnd());
        QCOMPARE(zoomOf(view), 1.0);
    }

    /// Past the width it was fetched at, a zoomed page asks for its file as it is, once,
    /// rather than drawing the screen's copy larger and blurred.
    void a_page_zoomed_past_its_width_asks_for_its_file_whole()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();
        const QByteArray whole = "GET /entries/e12/pages/"
                                 + QByteArray::number(reader->firstPage()) + " HTTP";
        QTRY_VERIFY(pretend.heard.contains("GET /entries/e12/pages/"
                                           + QByteArray::number(reader->firstPage()) + "?width="));
        QVERIFY(!pretend.heard.contains(whole));

        for (int step = 0; step < 4; ++step)
            QTest::keyClick(window, Qt::Key_Plus, Qt::ControlModifier);
        QTRY_COMPARE(zoomOf(view), 4.0);
        QTRY_VERIFY2(pretend.heard.contains(whole), whole.constData());
    }

    /// The zoom stays out of a strip: Ctrl and the wheel scroll a webtoon as the wheel alone
    /// does, and nothing is enlarged.
    void ctrl_and_the_wheel_still_scroll_a_webtoon()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"VERTICAL"_s, nullptr, u"webtoon"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();
        QQuickItem *strip = nullptr;
        QTRY_VERIFY((strip = itemNamed(view, u"reader-strip"_s)) && strip->isVisible());
        QTRY_COMPARE(stripPageAt(strip, strip->property("contentY").toReal() + 1), 46);
        const qreal top = strip->property("contentY").toReal();

        wheel(window, across(view, 0.5), QPoint(0, -120), QPoint(0, 0),
                          Qt::ControlModifier);
        QTRY_VERIFY(strip->property("contentY").toReal() > top);
        QTest::keyClick(window, Qt::Key_Plus, Qt::ControlModifier);
        QTest::keyClick(window, Qt::Key_I);
        QTRY_VERIFY(view->property("volumeOpen").toBool());
        QCOMPARE(zoomOf(view), 1.0);
    }

    /// A double click on a third of a whole page is two turns and no zoom, deliberately:
    /// telling it from a click would hold back every turn by click for the double-click
    /// interval. Only the middle zooms.
    void a_double_click_on_a_third_turns_two_pages_and_zooms_nothing()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();
        const int before = reader->display();

        QTest::mouseDClick(window, Qt::LeftButton, Qt::NoModifier, across(view, 0.10));
        QTRY_COMPARE(reader->display(), before + 2);
        QCOMPARE(zoomOf(view), 1.0);
        QObject *later = view->findChild<QObject *>(u"reader-bars-later"_s);
        QVERIFY(later);
        QVERIFY(!later->property("running").toBool());
    }

    /// In a manga the page read next is on the left: ← moves a zoomed page towards its left
    /// edge, and at that edge turns forward, as the left third does.
    void in_a_manga_the_left_edge_of_a_zoomed_page_turns_it_forward()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();
        QQuickItem *sheet = itemNamed(view, u"reader-sheet"_s);
        QVERIFY(sheet);
        const int before = reader->display();

        for (int step = 0; step < 3; ++step)
            QTest::keyClick(window, Qt::Key_Plus, Qt::ControlModifier);
        QTRY_COMPARE(zoomOf(view), 3.0);
        const qreal x = sheet->property("contentX").toReal();
        QTest::keyClick(window, Qt::Key_Left);
        QTRY_VERIFY(sheet->property("contentX").toReal() < x);
        QCOMPARE(reader->display(), before);
        QVERIFY(pressedToTheEdge(window, view, Qt::Key_Left, "atXBeginning"));
        QCOMPARE(reader->display(), before);
        QTest::keyClick(window, Qt::Key_Left);
        QTRY_COMPARE(reader->display(), before + 1);

        // And → towards the right edge, which turns back, as the right third does.
        for (int step = 0; step < 3; ++step)
            QTest::keyClick(window, Qt::Key_Plus, Qt::ControlModifier);
        QTRY_COMPARE(zoomOf(view), 3.0);
        QVERIFY(!sheet->property("atXEnd").toBool());
        QVERIFY(pressedToTheEdge(window, view, Qt::Key_Right, "atXEnd"));
        QCOMPARE(reader->display(), before + 1);
        QTest::keyClick(window, Qt::Key_Right);
        QTRY_COMPARE(reader->display(), before);
    }

    /// Ctrl = zooms as Ctrl + does — the + needs Shift on many keyboards — and on a French
    /// one the key of 0 sends À: Ctrl À gives the whole page back as Ctrl 0 does.
    void ctrl_equals_zooms_and_ctrl_a_grave_gives_the_whole_page_back()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();

        QTest::keyClick(window, Qt::Key_Equal, Qt::ControlModifier);
        QTRY_COMPARE(zoomOf(view), 1.5);
        QTest::keyClick(window, Qt::Key_Agrave, Qt::ControlModifier);
        QTRY_COMPARE(zoomOf(view), 1.0);
    }

    /// F11 is the full screen every other application on the desk answers to, and the test
    /// of 22/10/2026 pressed it first: it does what `f` does, both ways.
    void f11_toggles_the_full_screen_as_f_does()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();

        QTest::keyClick(window, Qt::Key_F11);
        QTRY_COMPARE(window->visibility(), QWindow::FullScreen);
        QTest::keyClick(window, Qt::Key_F11);
        QTRY_VERIFY(window->visibility() != QWindow::FullScreen);
    }

    /// Backspace goes back a page, as the back button of the dock does — and from the first
    /// page to the end of the volume before, the way that button learned to.
    void backspace_turns_back_and_from_the_first_page_opens_the_volume_before()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        BookExtras extras;
        extras.preceded = true;
        extras.unopened = true;
        pretend.answerFor = [&extras](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s, nullptr, u"manga"_s, nullptr,
                                nullptr, &extras);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();

        reader->goToPage(10);
        const int there = reader->display();
        QVERIFY(there > 0);
        QTest::keyClick(window, Qt::Key_Backspace);
        QTRY_COMPARE(reader->display(), there - 1);

        reader->goToPage(0);
        QTRY_COMPARE(reader->display(), 0);
        QTest::keyClick(window, Qt::Key_Backspace);
        QTRY_COMPARE(reader->entryId(), u"e11"_s);
        QTRY_VERIFY(!reader->loading() && reader->pageCount() == 30);
        QCOMPARE(reader->firstPage(), 29);
    }

    /// Alt ← is the way back in every browser and file manager: it leaves the reader for the
    /// series, as the bar's arrow does, without peeling a layer at a time as Escape does.
    void alt_left_leaves_the_reader_for_the_series()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *navigation =
            engine.singletonInstance<Navigation *>(qmlTypeId("Leaf", 1, 0, "Navigation"));
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        const int before = reader->display();

        QTest::keyClick(view->window(), Qt::Key_Left, Qt::AltModifier);
        QTRY_COMPARE(navigation->destination(), Navigation::Destination::Series);
        QCOMPARE(reader->display(), before);
    }

    /// Ctrl W closes what is open, here the book, even with the volume's panel up over it.
    void ctrl_w_closes_the_reader_whatever_is_open_over_the_page()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *navigation =
            engine.singletonInstance<Navigation *>(qmlTypeId("Leaf", 1, 0, "Navigation"));
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();
        QTest::keyClick(window, Qt::Key_I);
        QTRY_VERIFY(view->property("volumeOpen").toBool());

        QTest::keyClick(window, Qt::Key_W, Qt::ControlModifier);
        QTRY_COMPARE(navigation->destination(), Navigation::Destination::Series);
    }

    /// + and − alone zoom as Ctrl + and Ctrl − do, from the row of figures and from the
    /// keypad alike; the keypad's with Ctrl too.
    void plus_and_minus_alone_and_on_the_keypad_zoom_as_ctrl_does()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();
        const int before = reader->display();

        QTest::keyClick(window, Qt::Key_Plus);
        QTRY_COMPARE(zoomOf(view), 1.5);
        QTest::keyClick(window, Qt::Key_Minus);
        QTRY_COMPARE(zoomOf(view), 1.0);
        QTest::keyClick(window, Qt::Key_Plus, Qt::KeypadModifier);
        QTRY_COMPARE(zoomOf(view), 1.5);
        QTest::keyClick(window, Qt::Key_Plus, Qt::ControlModifier | Qt::KeypadModifier);
        QTRY_COMPARE(zoomOf(view), 2.0);
        QTest::keyClick(window, Qt::Key_Minus, Qt::ControlModifier | Qt::KeypadModifier);
        QTRY_COMPARE(zoomOf(view), 1.5);
        QTest::keyClick(window, Qt::Key_Minus, Qt::KeypadModifier);
        QTRY_COMPARE(zoomOf(view), 1.0);
        QCOMPARE(reader->display(), before);
    }

    /// Ctrl ← and Ctrl → go to the ends of the book the way it lies: in a manga the last page
    /// is on the left, so Ctrl ← goes there, as ← goes on.
    void ctrl_arrows_go_to_the_ends_of_the_book_the_way_it_reads()
    {
        for (const QString &direction : {u"RIGHT_TO_LEFT"_s, u"LEFT_TO_RIGHT"_s}) {
            Pretend pretend;
            QVERIFY(pretend.listen(QHostAddress::LocalHost));
            pretend.answerFor = [direction](const QByteArray &request) {
                return aBookAnswers(request, direction);
            };
            qputenv("LEAF_ADDRESS",
                    u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

            QQmlApplicationEngine engine;
            Boot::run(engine, *qGuiApp, nowhere());
            Reader *reader = nullptr;
            QQuickItem *view = aReaderAtHand(engine, reader);
            QVERIFY(view);
            auto *window = view->window();
            const bool manga = direction == u"RIGHT_TO_LEFT"_s;

            reader->goToPage(40);
            QTRY_VERIFY(reader->display() > 0);
            QTest::keyClick(window, manga ? Qt::Key_Left : Qt::Key_Right, Qt::ControlModifier);
            QTRY_COMPARE(reader->display(), reader->displayCount() - 1);
            QVERIFY(!reader->atTheEnd());
            QTest::keyClick(window, manga ? Qt::Key_Right : Qt::Key_Left, Qt::ControlModifier);
            QTRY_COMPARE(reader->display(), 0);
        }
    }

    /// The keys that were there keep working beside the new ones: Page Down and Page Up turn,
    /// whole pages, both ways.
    void page_down_and_page_up_still_turn()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();
        const int before = reader->display();

        QTest::keyClick(window, Qt::Key_PageDown);
        QTRY_COMPARE(reader->display(), before + 1);
        QTest::keyClick(window, Qt::Key_PageUp);
        QTRY_COMPARE(reader->display(), before);
    }

    /// The card of the reader's keys opens from its button in the bar — lit while it is up —
    /// and from `?` and F1; each closes what it opened.
    void the_keyboard_button_question_mark_and_f1_open_the_shortcuts_card()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();
        QQuickItem *card = itemNamed(view, u"reader-shortcuts"_s);
        QVERIFY(card);
        QVERIFY(!card->isVisible());

        QQuickItem *button = barsCalled(window, view, u"reader-shortcuts-button"_s);
        QVERIFY(button);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centreOf(button));
        QTRY_VERIFY(card->isVisible());
        QTRY_COMPARE(card->opacity(), 1.0);
        QVERIFY(button->property("held").toBool());
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centreOf(button));
        QTRY_VERIFY(!card->isVisible());
        QVERIFY(!button->property("held").toBool());

        QTest::keyClick(window, Qt::Key_Question, Qt::ShiftModifier);
        QTRY_VERIFY(card->isVisible());
        QTest::keyClick(window, Qt::Key_Question, Qt::ShiftModifier);
        QTRY_VERIFY(!card->isVisible());
        QTest::keyClick(window, Qt::Key_F1);
        QTRY_VERIFY(card->isVisible());
        QTest::keyClick(window, Qt::Key_F1);
        QTRY_VERIFY(!card->isVisible());
    }

    /// Escape puts the card away and nothing else; the cross does the same; the reader
    /// stays open on the same page, and the keys of the page wait while the card is up.
    void escape_and_the_cross_put_the_shortcuts_card_away_and_nothing_else()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *navigation =
            engine.singletonInstance<Navigation *>(qmlTypeId("Leaf", 1, 0, "Navigation"));
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();
        QQuickItem *card = itemNamed(view, u"reader-shortcuts"_s);
        QVERIFY(card);
        const int before = reader->display();

        QTest::keyClick(window, Qt::Key_F1);
        QTRY_VERIFY(card->isVisible());
        QTest::keyClick(window, Qt::Key_Left);
        QTest::keyClick(window, Qt::Key_Space);
        QVERIFY(aFrameDrawn(window));
        QCOMPARE(reader->display(), before);
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!card->isVisible());
        QCOMPARE(navigation->destination(), Navigation::Destination::Reader);

        QTest::keyClick(window, Qt::Key_F1);
        QTRY_COMPARE(card->opacity(), 1.0);
        QQuickItem *cross = itemNamed(card, u"reader-shortcuts-close"_s);
        QVERIFY(cross);
        QVERIFY(aFrameDrawn(window));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centreOf(cross));
        QTRY_VERIFY(!card->isVisible());
        QCOMPARE(reader->display(), before);
        QCOMPARE(navigation->destination(), Navigation::Destination::Reader);
    }

    /// A click beside the card puts it away and turns nothing, even on a third of the page.
    void a_click_beside_the_shortcuts_card_puts_it_away_and_turns_nothing()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();
        QQuickItem *card = itemNamed(view, u"reader-shortcuts"_s);
        QQuickItem *face = itemNamed(view, u"reader-shortcuts-card"_s);
        QVERIFY(card && face);
        const int before = reader->display();

        QTest::keyClick(window, Qt::Key_F1);
        QTRY_COMPARE(card->opacity(), 1.0);
        QVERIFY(aFrameDrawn(window));
        // On the card: it stays.
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          face->mapToScene(QPointF(face->width() / 2.0, face->height() - 12))
                              .toPoint());
        QVERIFY(aFrameDrawn(window));
        QVERIFY(view->property("shortcutsOpen").toBool());
        // Beside it, low on the left third: it goes, and the page stays.
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          view->mapToScene(QPointF(8, view->height() - 8)).toPoint());
        QTRY_VERIFY(!card->isVisible());
        QCOMPARE(reader->display(), before);
    }

    /// One thing hung over the book at a time: the card shuts the volume's panel and the
    /// settings card, and either of them shuts it. Nothing opens it over the end card.
    void the_shortcuts_card_is_never_up_with_the_panel_the_settings_or_the_end_card()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        BookExtras extras;
        extras.followed = true;
        pretend.answerFor = [&extras](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s, nullptr, u"manga"_s, nullptr,
                                nullptr, &extras);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();
        const auto open = [view](const char *what) { return view->property(what).toBool(); };
        // The bars up first: a click in the middle under the card is a click on the card.
        QQuickItem *tune = barsCalled(window, view, u"reader-settings-button"_s);
        QVERIFY(tune);

        QTest::keyClick(window, Qt::Key_I);
        QTRY_VERIFY(open("volumeOpen"));
        QTest::keyClick(window, Qt::Key_F1);
        QTRY_VERIFY(open("shortcutsOpen"));
        QVERIFY(!open("volumeOpen"));

        // The bar's buttons answer through the veil.
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centreOf(tune));
        QTRY_VERIFY(open("settingsOpen"));
        QVERIFY(!open("shortcutsOpen"));
        QTest::keyClick(window, Qt::Key_F1);
        QTRY_VERIFY(open("shortcutsOpen"));
        QVERIFY(!open("settingsOpen"));
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!open("shortcutsOpen"));

        QVERIFY(raiseTheEndCard(window, view, reader));
        QTest::keyClick(window, Qt::Key_F1);
        QTest::keyClick(window, Qt::Key_Question, Qt::ShiftModifier);
        QVERIFY(aFrameDrawn(window));
        QVERIFY(!open("shortcutsOpen"));
        QVERIFY(reader->atTheEnd());
    }

    /// Every word on the card is `Words`': the title, the four groups in their order, each
    /// line, the keys — and the card is two columns in a wide window, one in a narrow one.
    void the_shortcuts_card_says_what_words_says_in_two_columns_or_one()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();
        const auto putBack = qScopeGuard([&] { window->resize(1100, 760); });
        QQuickItem *card = itemNamed(view, u"reader-shortcuts"_s);
        QVERIFY(card);
        QTest::keyClick(window, Qt::Key_F1);
        QTRY_COMPARE(card->opacity(), 1.0);

        QCOMPARE(textNamed(card, u"reader-shortcuts-title"_s), Words::readerShortcuts());
        QStringList said;
        for (QQuickItem *one : itemsNamed(card, u"reader-shortcut-does"_s))
            said << one->property("text").toString();
        using enum Words::ShortcutDoes;
        // A manga: ← goes on, and the arrows' lines say so.
        QCOMPARE(said, (QStringList{
                           Words::shortcutDoes(TurnByArrowRightToLeft),
                           Words::shortcutDoes(TurnBySpace),
                           Words::shortcutDoes(ToTheEndsRightToLeft), Words::shortcutDoes(BackAPage),
                           Words::shortcutDoes(FullScreen), Words::shortcutDoes(VolumeInfo),
                           Words::shortcutDoes(TheseShortcuts), Words::shortcutDoes(ZoomByKey),
                           Words::shortcutDoes(BackToWhole), Words::shortcutDoes(ZoomByWheel),
                           Words::shortcutDoes(ZoomWhereAimed), Words::shortcutDoes(Pinch),
                           Words::shortcutDoes(CloseWhatIsOpen),
                           Words::shortcutDoes(BackToSeries), Words::shortcutDoes(CloseReader)}));
        QStringList keys;
        for (QQuickItem *one : itemsNamed(card, u"keycap-text"_s)) {
            if (one->isVisible())
                keys << one->property("text").toString();
        }
        QVERIFY2(keys.contains(Words::keyName(Words::KeyName::Escape)), qPrintable(keys.join(u' ')));
        QVERIFY(keys.contains(Words::keyName(Words::KeyName::Shift)));
        QCOMPARE(keys.count(Words::keyName(Words::KeyName::Ctrl)), 5);

        // Navigate and Display on the left, Zoom and Leave on the right, side by side.
        QQuickItem *navigate = itemNamed(card, u"reader-shortcuts-navigate"_s);
        QQuickItem *display = itemNamed(card, u"reader-shortcuts-display"_s);
        QQuickItem *zoom = itemNamed(card, u"reader-shortcuts-zoom"_s);
        QVERIFY(navigate && display && zoom);
        const auto at = [](QQuickItem *item) { return item->mapToScene(QPointF(0, 0)); };
        QVERIFY(at(display).y() > at(navigate).y());
        QVERIFY(at(zoom).x() > at(navigate).x() + 200);
        QCOMPARE(at(zoom).y(), at(navigate).y());

        window->resize(560, 760);
        QTRY_COMPARE(view->width(), 560.0);
        QTRY_COMPARE(at(zoom).x(), at(navigate).x());
        QVERIFY(at(zoom).y() > at(display).y());
    }

    /// The shortcuts card follows the window as the dock does, its keys and its lines with
    /// it; see `aCardThatDoesNotFollow`. Below the wide band it is still one column.
    void the_shortcuts_card_follows_the_window_as_the_dock_does()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();
        const auto putBack = qScopeGuard([&] { window->resize(1100, 760); });

        struct Line {
            QString name;
            int size;
            int (*role)(int, qreal);
        };
        for (const Line &words : {Line{u"reader-shortcuts-title"_s, 28, Scale::titleFor},
                                  Line{u"reader-shortcut-does"_s, 15, Scale::bodyFor},
                                  Line{u"keycap-text"_s, 13, Scale::captionFor}}) {
            const QString wrong =
                aCardThatDoesNotFollow(view, u"reader-shortcuts-card"_s, words.name, 1080,
                                       words.size, words.role, 0.9, 32);
            QVERIFY2(wrong.isEmpty(), qPrintable(wrong));
        }

        // Two columns in the wide band however large, one below it however small.
        QQuickItem *grid = itemNamed(view, u"reader-shortcuts-grid"_s);
        QVERIFY(grid);
        window->resize(1920, 1200);
        QTRY_COMPARE(grid->property("columns").toInt(), 2);
        window->resize(1099, 700);
        QTRY_COMPARE(grid->property("columns").toInt(), 1);
        window->resize(560, 700);
        QTRY_COMPARE(view->width(), 560.0);
        QCOMPARE(grid->property("columns").toInt(), 1);
    }

    /// The reader in each of `theWindows`, as the owner asked on 22/10/2026 after trying it
    /// on a window of 1920: no text under its floor, nothing outside the window, and each
    /// layout switching where it should — the keys' card to one column, the volume's cover
    /// above its facts, the dock's chapter steps and the bar's keyboard gone in the narrow
    /// band, and a card taller than the window scrolling inside it.
    void the_reader_lays_itself_out_for_each_window()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        BookExtras extras;
        extras.chapters = QJsonArray{
            QJsonObject{{u"id"_s, u"c98"_s}, {u"label"_s, u"Chap.098"_s}, {u"number"_s, 98},
                        {u"title"_s, u"Le temps des examens"_s}, {u"kind"_s, u"CHAPTER"_s},
                        {u"entryId"_s, u"e12"_s}, {u"startPage"_s, 0}},
            QJsonObject{{u"id"_s, u"c99"_s}, {u"label"_s, u"Chap.099"_s}, {u"number"_s, 99},
                        {u"title"_s, u"Coup de sifflet"_s}, {u"kind"_s, u"CHAPTER"_s},
                        {u"entryId"_s, u"e12"_s}, {u"startPage"_s, 30}},
        };
        pretend.answerFor = [&extras](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s, nullptr, u"manga"_s, nullptr,
                                nullptr, &extras);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();
        const auto putBack = qScopeGuard([&] { window->resize(1100, 760); });
        QTRY_VERIFY(reader->chapters().size() == 2);
        // The bars up for good, so that what they show is shown: the ones a volume opens with
        // would leave after three seconds.
        view->findChild<QObject *>(u"reader-welcome"_s)->setProperty("running", false);
        view->setProperty("barsShown", true);
        QQuickItem *keys = itemNamed(view, u"reader-shortcuts-card"_s);
        QQuickItem *grid = itemNamed(view, u"reader-shortcuts-grid"_s);
        QQuickItem *sheet = itemNamed(view, u"reader-shortcuts-sheet"_s);
        QQuickItem *panel = itemNamed(view, u"reader-volume"_s);
        QQuickItem *cover = itemNamed(view, u"reader-volume-cover"_s);
        QQuickItem *facts = itemNamed(view, u"reader-volume-facts"_s);
        QQuickItem *settings = itemNamed(view, u"reader-settings"_s);
        QQuickItem *end = itemNamed(view, u"reader-end-card"_s);
        QQuickItem *dock = itemNamed(view, u"reader-dock"_s);
        QQuickItem *bar = itemNamed(view, u"reader-bar"_s);
        QQuickItem *chapter = itemNamed(view, u"reader-chapter-forward"_s);
        QQuickItem *keyboard = itemNamed(view, u"reader-shortcuts-button"_s);
        QVERIFY(keys && grid && sheet && panel && cover && facts && settings && end && dock
                && bar && chapter && keyboard);
        QList<QQuickItem *> texts;
        textsUnder(view, texts);
        QVERIFY(texts.size() > 40);
        // What is read rather than glanced at, which stops at 13.
        const QStringList read = {u"reader-shortcut-does"_s, u"reader-volume-summary"_s,
                                  u"reader-desk-note"_s, u"reader-trouble"_s,
                                  u"reader-end-done"_s, u"reader-volume-series"_s};

        for (const AWindow &at : theWindows) {
            QVERIFY(laidOutFor(view, at));
            const QString where = u"%1 × %2"_s.arg(at.width).arg(at.height);
            const QRectF whole(0, 0, at.width, at.height);
            for (QQuickItem *text : std::as_const(texts))
                QVERIFY2(lettersOf(text) >= Scale::CaptionFloor,
                         qPrintable(where + u": "_s + text->objectName()));
            for (const QString &name : read)
                QVERIFY2(lettersOf(itemNamed(view, name)) >= Scale::BodyFloor,
                         qPrintable(where + u": "_s + name));
            for (QQuickItem *card : {keys, panel, settings, end, dock, bar})
                QVERIFY2(whole.contains(placed(card)),
                         qPrintable(where + u": "_s + card->objectName()));

            const bool narrow = at.width < 600;
            QCOMPARE(grid->property("columns").toInt(), at.width >= 1100 ? 2 : 1);
            // A card taller than the window stops at it, and its lines scroll.
            const bool overflows = sheet->property("contentHeight").toReal() > sheet->height();
            QCOMPARE(sheet->property("interactive").toBool(), overflows);
            QVERIFY2(keys->height() <= at.height - 32, qPrintable(where));
            if (at.height <= 600)
                QVERIFY2(overflows, qPrintable(where));
            // The cover above the facts when the card is too narrow for both side by side.
            const bool stacked = at.width <= 900;
            // Placed by the grid at the next frame, not as the window changes.
            QTRY_COMPARE(placed(facts).top() >= placed(cover).bottom(), stacked);
            // Measured against the card as drawn, slide included: it is closed, so slid aside.
            QVERIFY(placed(facts).right()
                    <= panel->mapRectToScene(panel->boundingRect()).right() + 0.5);
            QCOMPARE(chapter->isVisible(), !narrow);
            QCOMPARE(keyboard->isVisible(), !narrow);
        }
    }

    /// The arrows' lines follow the book open: in the test of 22/10/2026 the card said
    /// « Page précédente, suivante » over ← → in a manga, where ← goes on. A book read left to
    /// right keeps the words the mockup approved.
    void the_shortcuts_card_words_the_arrows_the_way_the_book_reads()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"LEFT_TO_RIGHT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        QTRY_VERIFY(!view->property("rightToLeft").toBool());
        auto *window = view->window();
        QQuickItem *card = itemNamed(view, u"reader-shortcuts"_s);
        QVERIFY(card);
        QTest::keyClick(window, Qt::Key_F1);
        QTRY_COMPARE(card->opacity(), 1.0);

        QStringList said;
        for (QQuickItem *one : itemsNamed(card, u"reader-shortcut-does"_s))
            said << one->property("text").toString();
        QVERIFY(said.size() > 2);
        using enum Words::ShortcutDoes;
        QCOMPARE(said.at(0), Words::shortcutDoes(TurnByArrow));
        QCOMPARE(said.at(2), Words::shortcutDoes(ToTheEnds));
        QVERIFY(!said.contains(Words::shortcutDoes(ToTheEndsRightToLeft)));
    }

    /// At the width fit a page taller than the window is read down before it is turned: the
    /// space bar and ↓ go down it and turn at its foot, in a book read left to right.
    void at_the_width_fit_the_keys_read_down_a_tall_page_and_turn_at_its_foot()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"LEFT_TO_RIGHT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *preferences =
            engine.singletonInstance<Preferences *>(qmlTypeId("Leaf", 1, 0, "Preferences"));
        QVERIFY(preferences);
        const auto putBack = qScopeGuard([&] {
            preferences->chooseFit(Preferences::Fit::Height);
        });
        preferences->chooseFit(Preferences::Fit::Width);
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();
        QQuickItem *sheet = itemNamed(view, u"reader-sheet"_s);
        QVERIFY(sheet);
        QTRY_VERIFY(sheet->property("contentHeight").toReal() > sheet->height());
        QCOMPARE(zoomOf(view), 1.0);
        const int before = reader->display();

        QVERIFY(pressedToTheEdge(window, view, Qt::Key_Space, "atYEnd"));
        QCOMPARE(reader->display(), before);
        QTest::keyClick(window, Qt::Key_Space);
        QTRY_COMPARE(reader->display(), before + 1);
        // The page that comes starts at its top.
        QTRY_COMPARE(sheet->property("contentY").toReal(), 0.0);

        QVERIFY(pressedToTheEdge(window, view, Qt::Key_Down, "atYEnd"));
        QCOMPARE(reader->display(), before + 1);
        QTest::keyClick(window, Qt::Key_Down);
        QTRY_COMPARE(reader->display(), before + 2);
    }

    /// Going back from the top of a tall page lands at the foot of the one before, as reading
    /// back up does — not at its top, a screen away from the lines just left.
    void at_the_width_fit_a_turn_back_lands_at_the_foot_of_the_page_before()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"LEFT_TO_RIGHT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        const auto single = onePageAtATime(engine);
        auto *preferences =
            engine.singletonInstance<Preferences *>(qmlTypeId("Leaf", 1, 0, "Preferences"));
        QVERIFY(preferences);
        const auto putBack = qScopeGuard([&] {
            preferences->chooseFit(Preferences::Fit::Height);
        });
        preferences->chooseFit(Preferences::Fit::Width);
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();
        QQuickItem *sheet = itemNamed(view, u"reader-sheet"_s);
        QVERIFY(sheet);
        QTRY_VERIFY(sheet->property("contentHeight").toReal() > sheet->height());
        QTRY_VERIFY(sheet->property("atYBeginning").toBool());
        const int before = reader->display();

        QTest::keyClick(window, Qt::Key_Space, Qt::ShiftModifier);
        QTRY_COMPARE(reader->display(), before - 1);
        QTRY_VERIFY(sheet->property("contentHeight").toReal() > sheet->height());
        QTRY_VERIFY(sheet->property("atYEnd").toBool());
        QVERIFY(sheet->property("contentY").toReal() > 0);

        // A jump back from elsewhere — the progress bar — is not reading back up: the page
        // starts at its top.
        reader->goToPage(20);
        QTRY_COMPARE(reader->firstPage(), 20);
        QTRY_COMPARE(sheet->property("contentY").toReal(), 0.0);
    }

    /// The wheel scrolls a tall page at the width fit and turns it at the foot — with a notch
    /// of its own, never with the run of notches that reached the foot.
    void at_the_width_fit_the_wheel_turns_at_the_foot_but_not_with_the_scroll_that_reached_it()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"LEFT_TO_RIGHT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *preferences =
            engine.singletonInstance<Preferences *>(qmlTypeId("Leaf", 1, 0, "Preferences"));
        QVERIFY(preferences);
        const auto putBack = qScopeGuard([&] {
            preferences->chooseFit(Preferences::Fit::Height);
        });
        preferences->chooseFit(Preferences::Fit::Width);
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();
        QQuickItem *sheet = itemNamed(view, u"reader-sheet"_s);
        QVERIFY(sheet);
        QObject *rest = view->findChild<QObject *>(u"reader-wheel-rest"_s);
        QVERIFY(rest);
        QTRY_VERIFY(sheet->property("contentHeight").toReal() > sheet->height());
        const int before = reader->display();
        // The rest held for as long as the test needs it, so that no notch below depends
        // on arriving within 300 ms of the last scroll.
        rest->setProperty("interval", 3600000);

        // Down to the foot, a notch at a time; each is handled before `wheelEvent` returns.
        for (int notch = 0; notch < 100 && !sheet->property("atYEnd").toBool(); ++notch)
            wheel(window, across(view, 0.5), QPoint(0, -120));
        QVERIFY(sheet->property("atYEnd").toBool());
        QCOMPARE(reader->display(), before);
        QVERIFY(rest->property("running").toBool());
        // The run goes on past the foot, fresh from the scroll: no turn.
        for (int notch = 0; notch < 3; ++notch)
            wheel(window, across(view, 0.5), QPoint(0, -120));
        QCOMPARE(reader->display(), before);
        // Turned by a key while the rest still runs: the page that comes has no rest to
        // swallow its first notch, which would otherwise wait an hour here.
        QTest::keyClick(window, Qt::Key_Space);
        QTRY_COMPARE(reader->display(), before + 1);
        QVERIFY(!rest->property("running").toBool());
    }

    /// What a touchpad sends once the fingers are lifted, named as momentum, never turns a
    /// page at its foot, however long after the scroll it comes. Sent only once the rest is
    /// over, so the phase alone is what holds the page.
    void at_the_width_fit_the_momentum_of_a_touchpad_never_turns_the_page()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"LEFT_TO_RIGHT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *preferences =
            engine.singletonInstance<Preferences *>(qmlTypeId("Leaf", 1, 0, "Preferences"));
        QVERIFY(preferences);
        const auto putBack = qScopeGuard([&] {
            preferences->chooseFit(Preferences::Fit::Height);
        });
        preferences->chooseFit(Preferences::Fit::Width);
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();
        QQuickItem *sheet = itemNamed(view, u"reader-sheet"_s);
        QVERIFY(sheet);
        QObject *rest = view->findChild<QObject *>(u"reader-wheel-rest"_s);
        QVERIFY(rest);
        QTRY_VERIFY(sheet->property("contentHeight").toReal() > sheet->height());
        const int before = reader->display();
        for (int notch = 0; notch < 100 && !sheet->property("atYEnd").toBool(); ++notch)
            wheel(window, across(view, 0.5), QPoint(0, -120));
        QVERIFY(sheet->property("atYEnd").toBool());
        QTRY_VERIFY(!rest->property("running").toBool());
        for (int sliver = 0; sliver < 3; ++sliver)
            wheel(window, across(view, 0.5), QPoint(0, -120), QPoint(0, 0),
                              Qt::NoModifier, Qt::ScrollMomentum);
        QCOMPARE(reader->display(), before);
        QVERIFY(!rest->property("running").toBool());
        wheel(window, across(view, 0.5), QPoint(0, -120));
        QTRY_COMPARE(reader->display(), before + 1);
    }

    /// What the files say is text wherever the reader shows it: the bar at the top, the line
    /// under the progress bar and the bubble over it. Left to Qt's guess, « <b>x</b> & y »
    /// came out bold and without its tags in the bar, where the panel beside it already
    /// showed it as written.
    void a_chapter_title_with_markup_is_shown_as_written_in_the_bars()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        BookExtras extras;
        extras.chapters = QJsonArray{
            QJsonObject{{u"id"_s, u"c99"_s}, {u"label"_s, u"Chap.099"_s}, {u"number"_s, 99},
                        {u"title"_s, u"<b>x</b> & y"_s}, {u"kind"_s, u"CHAPTER"_s},
                        {u"entryId"_s, u"e12"_s}, {u"startPage"_s, 0}},
            QJsonObject{{u"id"_s, u"c100"_s}, {u"label"_s, u"Chap.100"_s}, {u"number"_s, 100},
                        {u"title"_s, u"Ridicule"_s}, {u"kind"_s, u"CHAPTER"_s},
                        {u"entryId"_s, u"e12"_s}, {u"startPage"_s, 150}}};
        pretend.answerFor = [&extras](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s, nullptr, u"manga"_s, nullptr,
                                nullptr, &extras);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        const auto single = onePageAtATime(engine);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        Reader *reader = readerOpenOnTheBook(engine);
        QVERIFY(reader);
        QQuickItem *view = nullptr;
        QTRY_VERIFY((view = itemNamed(window->contentItem(), u"reader-view"_s)));
        QTRY_VERIFY(!reader->loading() && reader->pageCount() == 190);
        QCOMPARE(reader->firstPage(), 46);

        for (const QString &name : {u"reader-title"_s, u"reader-where"_s, u"reader-trouble"_s}) {
            QQuickItem *one = itemNamed(view, name);
            QVERIFY2(one, qPrintable(name));
            QVERIFY2(one->property("textFormat").toInt() == int(Qt::PlainText),
                     qPrintable(name));
        }
        QVERIFY(textNamed(view, u"reader-where"_s).contains(u"<b>x</b> & y"_s));
        QObject *tip = view->findChild<QObject *>(u"reader-scrub-tip"_s);
        QVERIFY(tip);
        QCOMPARE(tip->property("textFormat").toInt(), int(Qt::PlainText));
    }

    /// The line under the progress bar (spec §02): the chapter being read, and where the
    /// volume ends — its last chapter, then « fin du tome » — both as the files spell them.
    void the_line_under_the_progress_bar_names_the_chapter_and_the_end()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        BookExtras extras;
        extras.chapters = QJsonArray{
            QJsonObject{{u"id"_s, u"c99"_s}, {u"label"_s, u"Chap.099"_s}, {u"number"_s, 99},
                        {u"title"_s, u"<b>x</b> & y"_s}, {u"kind"_s, u"CHAPTER"_s},
                        {u"entryId"_s, u"e12"_s}, {u"startPage"_s, 0}},
            QJsonObject{{u"id"_s, u"c100"_s}, {u"label"_s, u"Chap.100"_s}, {u"number"_s, 100},
                        {u"title"_s, u"Ridicule"_s}, {u"kind"_s, u"CHAPTER"_s},
                        {u"entryId"_s, u"e12"_s}, {u"startPage"_s, 150}},
            // Titled by its label, as the files of the test of 22/10/2026 were.
            QJsonObject{{u"id"_s, u"c13"_s}, {u"label"_s, u"#13"_s}, {u"number"_s, 101},
                        {u"title"_s, u"#13"_s}, {u"kind"_s, u"CHAPTER"_s},
                        {u"entryId"_s, u"e12"_s}, {u"startPage"_s, 170}}};
        pretend.answerFor = [&extras](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s, nullptr, u"manga"_s, nullptr,
                                nullptr, &extras);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        const auto single = onePageAtATime(engine);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        Reader *reader = readerOpenOnTheBook(engine);
        QVERIFY(reader);
        QQuickItem *view = nullptr;
        QTRY_VERIFY((view = itemNamed(window->contentItem(), u"reader-view"_s)));
        QTRY_VERIFY(!reader->loading() && reader->pageCount() == 190);
        QCOMPARE(reader->firstPage(), 46);

        for (const QString &name : {u"reader-scrub-chapter"_s, u"reader-scrub-title"_s,
                                    u"reader-scrub-end"_s}) {
            QQuickItem *one = itemNamed(view, name);
            QVERIFY2(one, qPrintable(name));
            QVERIFY2(one->property("textFormat").toInt() == int(Qt::PlainText),
                     qPrintable(name));
        }
        const QChar n = Words::Nbsp;
        QCOMPARE(textNamed(view, u"reader-scrub-chapter"_s), u"Chap.099"_s);
        QCOMPARE(textNamed(view, u"reader-scrub-title"_s), n + u": <b>x</b> & y"_s);
        QCOMPARE(textNamed(view, u"reader-scrub-end"_s), u"#13"_s + n + u"· fin du tome"_s);
        reader->goToPage(160);
        QTRY_COMPARE(textNamed(view, u"reader-scrub-chapter"_s), u"Chap.100"_s);
        QCOMPARE(textNamed(view, u"reader-scrub-title"_s), n + u": Ridicule"_s);

        // A title that only repeats its label is said once, on the line and in the bubble the
        // hand raises over the bar: both read « #13 · #13 » in the test of 22/10/2026.
        reader->goToPage(175);
        QTRY_COMPARE(textNamed(view, u"reader-scrub-chapter"_s), u"#13"_s);
        QCOMPARE(textNamed(view, u"reader-scrub-title"_s), QString());
        QObject *scrub = view->findChild<QObject *>(u"reader-scrub"_s);
        QVERIFY(scrub);
        QObject *tip = view->findChild<QObject *>(u"reader-scrub-tip"_s);
        QVERIFY(tip);
        scrub->setProperty("held", 179);
        QCOMPARE(tip->property("text").toString(), u"#13"_s + n + u"· page 180"_s);
        scrub->setProperty("held", 159);
        QCOMPARE(tip->property("text").toString(),
                 u"Chap.100"_s + n + u": Ridicule"_s + n + u"· page 160"_s);
        scrub->setProperty("held", -1);
    }

    /// The panel opened over the pages changes nothing under it and asks for nothing: the
    /// same image, the same file, never not there.
    void opening_the_panel_keeps_the_page_on_screen_and_asks_for_nothing()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();
        const int first = reader->firstPage();
        QQuickItem *picture = nullptr;
        QTRY_VERIFY((picture = itemNamed(view, u"reader-page-image-%1"_s.arg(first)))
                    && picture->property("status").toInt() == 1);
        const QUrl source = picture->property("source").toUrl();
        const QByteArray asked = "GET /entries/e12/pages/" + QByteArray::number(first) + "?";
        const qsizetype before = pretend.heard.count(asked);
        QCOMPARE(before, 1);
        QQuickItem *stage = itemNamed(view, u"reader-stage"_s);
        const qreal whole = stage->width();

        QQuickItem *panel = itemNamed(view, u"reader-volume"_s);
        QTest::keyClick(window, Qt::Key_I);
        QTRY_COMPARE(panel->opacity(), 1.0);
        QVERIFY(aFrameDrawn(window));
        QCOMPARE(stage->width(), whole);
        // The same item, the same file, never not there.
        QCOMPARE(itemNamed(view, u"reader-page-image-%1"_s.arg(first)), picture);
        QCOMPARE(picture->property("source").toUrl(), source);
        QCOMPARE(picture->property("status").toInt(), 1);
        QTest::keyClick(window, Qt::Key_I);
        QTRY_VERIFY(!panel->isVisible());
        QVERIFY(aFrameDrawn(window));
        QCOMPARE(stage->width(), whole);
        QCOMPARE(picture->property("source").toUrl(), source);
        QCOMPARE(picture->property("status").toInt(), 1);
        QCOMPARE(pretend.heard.count(asked), before);
    }

    /// A window made wider than the step asked at does ask again — the copy would be blurred
    /// otherwise — and the page drawn so far stays on screen until the new copy is there.
    void a_page_asked_again_wider_stays_on_screen_until_the_new_copy_comes()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();
        const auto putBack = qScopeGuard([&] { window->resize(1100, 760); });
        const int first = reader->firstPage();
        QQuickItem *picture = nullptr;
        QTRY_VERIFY((picture = itemNamed(view, u"reader-page-image-%1"_s.arg(first)))
                    && picture->property("status").toInt() == 1);
        const int narrow = reader->askedWidth();

        // The copy at the new width held back by the server, as a slow one would.
        pretend.holdBack = [](const QByteArray &request) {
            return request.startsWith("GET /entries/e12/pages/") && request.contains("?width=");
        };
        window->resize(1700, 760);
        QTRY_VERIFY(reader->askedWidth() > narrow);
        QQuickItem *kept = itemNamed(view, u"reader-page-before-%1"_s.arg(first));
        QVERIFY(kept);
        QTRY_COMPARE(picture->property("status").toInt(), 2);
        QVERIFY(kept->isVisible());
        QCOMPARE(kept->property("status").toInt(), 1);
        QVERIFY(itemNamed(view, u"reader-page-%1"_s.arg(first))->height() > 0);

        pretend.holdBack = nullptr;
        pretend.release();
        QTRY_COMPARE(picture->property("status").toInt(), 1);
        QVERIFY(!kept->isVisible());
    }

    /// The notch that raised the end card is not also the one that opens what follows. One
    /// notch raised it and the next, a hand still rolling, opened the next volume under a
    /// reader who had not seen the card; a touchpad's momentum did it on its own. Swallowed
    /// while the card is fresh (`wheelRest`, started when it rises) and whenever the platform
    /// names it momentum; a deliberate notch after that opens it, as a turn does.
    void the_wheel_that_raised_the_card_does_not_also_open_what_follows()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        BookExtras extras;
        extras.followed = true;
        pretend.answerFor = [&extras](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s, nullptr, u"manga"_s, nullptr,
                                nullptr, &extras);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();
        QObject *rest = view->findChild<QObject *>(u"reader-wheel-rest"_s);
        QVERIFY(rest);
        reader->goToPage(189);
        QTRY_VERIFY(!reader->next().isEmpty());
        QTRY_VERIFY(!rest->property("running").toBool());

        wheel(window, across(view, 0.5), QPoint(0, -120));
        QVERIFY(reader->atTheEnd());
        QVERIFY(rest->property("running").toBool());
        wheel(window, across(view, 0.5), QPoint(0, -120));
        wheel(window, across(view, 0.5), QPoint(0, -240));
        QCOMPARE(reader->entryId(), u"e12"_s);
        QVERIFY(reader->atTheEnd());

        QTRY_VERIFY(!rest->property("running").toBool());
        for (int sliver = 0; sliver < 3; ++sliver)
            wheel(window, across(view, 0.5), QPoint(0, -120), QPoint(0, 0),
                              Qt::NoModifier, Qt::ScrollMomentum);
        QCOMPARE(reader->entryId(), u"e12"_s);

        wheel(window, across(view, 0.5), QPoint(0, -120));
        QTRY_COMPARE(reader->entryId(), u"e13"_s);
    }

    /// At the foot of a webtoon the wheel goes on as the space bar does: the card, then what
    /// follows. Scrolling it did nothing, since there was no strip left to scroll, and the
    /// card was out of reach of the wheel.
    void at_the_foot_of_a_webtoon_the_wheel_raises_the_card_then_opens_what_follows()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        BookExtras extras;
        extras.followed = true;
        pretend.answerFor = [&extras](const QByteArray &request) {
            return aBookAnswers(request, u"VERTICAL"_s, nullptr, u"webtoon"_s, nullptr,
                                nullptr, &extras);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();
        QQuickItem *strip = nullptr;
        QTRY_VERIFY((strip = itemNamed(view, u"reader-strip"_s)) && strip->isVisible());
        QObject *rest = view->findChild<QObject *>(u"reader-wheel-rest"_s);
        QVERIFY(rest);

        QTest::keyClick(window, Qt::Key_End);
        QTRY_VERIFY(strip->property("atYEnd").toBool());
        QTRY_COMPARE(reader->firstPage(), 189);
        QTRY_VERIFY(!reader->next().isEmpty());
        QTRY_VERIFY(!rest->property("running").toBool());

        wheel(window, across(view, 0.5), QPoint(0, -120));
        QVERIFY(reader->atTheEnd());
        wheel(window, across(view, 0.5), QPoint(0, -120));
        QCOMPARE(reader->entryId(), u"e12"_s);
        QTRY_VERIFY(!rest->property("running").toBool());
        wheel(window, across(view, 0.5), QPoint(0, -120), QPoint(0, 0),
                          Qt::NoModifier, Qt::ScrollMomentum);
        QCOMPARE(reader->entryId(), u"e12"_s);
        wheel(window, across(view, 0.5), QPoint(0, -120));
        QTRY_COMPARE(reader->entryId(), u"e13"_s);
    }

    /// The bars a click in the middle was about to call stay away when a page is turned
    /// before they came: the turn is what sends them away, and they came anyway a moment
    /// later, over the page the reader had just turned to.
    void a_turn_right_after_a_click_in_the_middle_keeps_the_bars_away()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();
        QObject *later = view->findChild<QObject *>(u"reader-bars-later"_s);
        QVERIFY(later);
        const int before = reader->display();

        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, across(view, 0.5));
        QVERIFY(later->property("running").toBool());
        QTest::keyClick(window, Qt::Key_Space);
        QCOMPARE(reader->display(), before + 1);
        QTRY_VERIFY(!later->property("running").toBool());
        QVERIFY(aFrameDrawn(window));
        QVERIFY(!view->property("barsShown").toBool());
    }

    /// Nothing opens over the end card: the card is the one decision on the screen, and the
    /// volume's panel or the settings put beside it are a second one it hides.
    void nothing_opens_over_the_end_card()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        Reader *reader = readerOpenOnTheBook(engine);
        QVERIFY(reader);
        QQuickItem *view = nullptr;
        QTRY_VERIFY((view = itemNamed(window->contentItem(), u"reader-view"_s)));
        QTRY_VERIFY(!reader->loading() && reader->pageCount() == 190);
        QQuickItem *card = raiseTheEndCard(window, view, reader);
        QVERIFY(card);
        QQuickItem *panel = itemNamed(view, u"reader-volume"_s);
        QQuickItem *settings = itemNamed(view, u"reader-settings"_s);
        QVERIFY(panel && settings);

        QTest::keyClick(window, Qt::Key_I);
        QVERIFY(aFrameDrawn(window));
        QVERIFY(!view->property("volumeOpen").toBool());

        // The bars over the card, called by a click on the veil beside it.
        view->setProperty("barsShown", true);
        QQuickItem *book = itemNamed(view, u"reader-volume-button"_s);
        QQuickItem *tune = itemNamed(view, u"reader-settings-button"_s);
        QTRY_VERIFY(book->isVisible() && book->opacity() > 0.99);
        QVERIFY(aFrameDrawn(window));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centreOf(book));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centreOf(tune));
        QVERIFY(aFrameDrawn(window));
        QVERIFY(!view->property("volumeOpen").toBool());
        QVERIFY(!view->property("settingsOpen").toBool());
        QVERIFY(!panel->isVisible());
        QVERIFY(!settings->isVisible());
        QVERIFY(reader->atTheEnd());
    }

    /// The wheel over the settings card or the volume's panel is theirs: the page behind was
    /// turned by a hand that was only scrolling a list of chapters.
    void the_wheel_over_the_settings_or_the_panel_turns_nothing_behind()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();
        const int before = reader->display();

        QQuickItem *settings = openTheSettings(window, view);
        QVERIFY(settings);
        QVERIFY(aFrameDrawn(window));
        for (int notch = 0; notch < 3; ++notch)
            wheel(window, centreOf(settings), QPoint(0, -120));
        QVERIFY(aFrameDrawn(window));
        QCOMPARE(reader->display(), before);
        QVERIFY(settings->isVisible());

        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!settings->isVisible());
        QQuickItem *panel = openTheVolumePanel(window, view);
        QVERIFY(panel);
        QVERIFY(aFrameDrawn(window));
        for (int notch = 0; notch < 3; ++notch)
            wheel(window, centreOf(panel), QPoint(0, -120));
        QVERIFY(aFrameDrawn(window));
        QCOMPARE(reader->display(), before);
        QVERIFY(panel->isVisible());
    }

    /// A window closed less than a second after a page was turned waits for that page to be
    /// written. The write was left to the reader's destructor, which runs once the event loop
    /// has stopped: the request was queued and never left, and the page was lost — the
    /// abrupt exit spec §08 is written against.
    void closing_the_window_right_after_a_turn_waits_for_the_page_to_be_written()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();
        bool writtenWhenHidden = false;
        connect(window, &QWindow::visibleChanged, this, [&](bool visible) {
            if (!visible)
                writtenWhenHidden = pretend.heard.contains("PATCH /entries/e12/progress ");
        });

        QTest::keyClick(window, Qt::Key_Space);
        QVERIFY(!pretend.heard.contains("PATCH "));
        window->close();
        // Still there: the close waits for the write it just sent.
        QVERIFY(window->isVisible());
        QTRY_VERIFY(!window->isVisible());
        QVERIFY(writtenWhenHidden);
    }

    /// A server that does not answer does not hold the window open: the wait has a ceiling.
    void a_close_waiting_for_a_write_gives_up_after_a_moment()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [](const QByteArray &request) {
            return aBookAnswers(request, u"RIGHT_TO_LEFT"_s);
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        Reader *reader = nullptr;
        QQuickItem *view = aReaderAtHand(engine, reader);
        QVERIFY(view);
        auto *window = view->window();
        pretend.holdBack = [](const QByteArray &request) { return request.startsWith("PATCH "); };

        QTest::keyClick(window, Qt::Key_Space);
        window->close();
        QVERIFY(window->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(pretend.heard.contains("PATCH "), 1000);
        QTRY_VERIFY_WITH_TIMEOUT(!window->isVisible(), 3000);
        pretend.holdBack = nullptr;
        pretend.release();
    }

    /// The tile of a book that is a whole book on its own opens the reader on it — the third
    /// way in that spec §01 names — and every other tile its series page. It opened the
    /// series page for both, a page of one volume to click again.
    void a_book_on_its_own_opens_the_reader_from_its_tile()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        QJsonObject alone = aSeries(u"lco"_s, u"Le Combat ordinaire"_s, 1, true);
        alone[u"oneShotEntry"_s] = u"lco-1"_s;
        const QJsonObject death = aSeries(u"dn"_s, u"Death Note"_s, 12, true);
        pretend.answerFor = [=](const QByteArray &request) -> QByteArray {
            if (request.startsWith("GET /series?"))
                return aReply(200, QByteArrayLiteral("application/json"),
                              aPage({alone, death}));
            if (request.startsWith("GET /filters"))
                return aReply(200, QByteArrayLiteral("application/json"), someFilters());
            return "HTTP/1.1 404 .\r\nContent-Length: 0\r\n\r\n";
        };
        qputenv("LEAF_ADDRESS", u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *navigation =
            engine.singletonInstance<Navigation *>(qmlTypeId("Leaf", 1, 0, "Navigation"));
        auto *grid = window->findChild<QQuickItem *>(u"shelf-grid"_s);
        QVERIFY(grid);
        QTRY_COMPARE(grid->property("count").toInt(), 2);
        QQuickItem *tile = nullptr;
        QTRY_VERIFY((tile = itemNamed(grid, u"tile-lco"_s)) && tile->width() > 0);
        QVERIFY(aFrameDrawn(window));

        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          centreOf(itemNamed(tile, u"cover-frame-lco"_s)));
        QTRY_COMPARE(navigation->destination(), Navigation::Destination::Reader);
        QCOMPARE(navigation->parameters().value(u"series"_s).toString(), u"lco"_s);
        QCOMPARE(navigation->parameters().value(u"entry"_s).toString(), u"lco-1"_s);

        navigation->back();
        QTRY_COMPARE(navigation->destination(), Navigation::Destination::Shelf);
        QQuickItem *other = nullptr;
        QTRY_VERIFY((other = itemNamed(grid, u"tile-dn"_s)) && other->isVisible());
        QVERIFY(aFrameDrawn(window));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          centreOf(itemNamed(other, u"cover-frame-dn"_s)));
        QTRY_COMPARE(navigation->destination(), Navigation::Destination::Series);
        QCOMPARE(navigation->parameters().value(u"series"_s).toString(), u"dn"_s);
    }

    void theme_resolves_and_follows_the_desktop_palette()
    {
        const RestoresThePalette restoreOnExit;
        QPalette night;
        night.setColor(QPalette::Window, QColor(u"#101010"_s));
        QGuiApplication::setPalette(night);

        QQmlApplicationEngine engine;
        Boot::run(engine, *qGuiApp, nowhere());

        auto *theme = engine.singletonInstance<Theme *>(qmlTypeId("Leaf", 1, 0, "Theme"));
        QVERIFY(theme);
        QVERIFY(theme->dark());
    }

    /// The square is there with a single profile — it is the way to « Ajouter » — and it
    /// opens « Qui lit ? » over the whole window; the cross puts the shelf back, and the
    /// keyboard where it was.
    void the_square_opens_who_reads()
    {
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = seatedAsPrincipal([](const QByteArray &) {
            return aReply(200, QByteArrayLiteral("application/json"), aPage({}));
        });
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        QQuickWindow *window = shown(engine);
        QVERIFY(window);

        auto *square = window->findChild<QQuickItem *>(u"profile-square"_s);
        auto *whoReads = window->findChild<QQuickItem *>(u"who-reads"_s);
        QVERIFY(square);
        QVERIFY(whoReads);
        QTRY_VERIFY(square->isVisible());
        // Laid out at the far right once it appears, not where the row first put it.
        QTRY_VERIFY(square->mapToScene(QPointF(0, 0)).x() > window->width() / 2);
        QCOMPARE(textNamed(square, u"profile-square-face-initial"_s), u"P"_s);
        QVERIFY(shownNamed(square, u"profile-square-face-badge"_s));
        // One profile: nobody to choose between, so the launch does not ask.
        QVERIFY(!whoReads->isVisible());

        clickOn(window, square);
        QTRY_VERIFY(whoReads->isVisible());
        QCOMPARE(textNamed(whoReads, u"who-reads-title"_s), Words::whoReads());
        QVERIFY(itemNamed(whoReads, u"who-reads-tile-p"_s));
        QVERIFY(shownNamed(whoReads, u"who-reads-ring-p"_s));
        QVERIFY(shownNamed(whoReads, u"who-reads-add"_s));

        QVERIFY(shownNamed(whoReads, u"who-reads-close"_s));
        clickOn(window, itemNamed(whoReads, u"who-reads-close"_s));
        QTRY_VERIFY(!whoReads->isVisible());
        QTRY_VERIFY(square->hasActiveFocus());
    }

    /// The shelf, its field and its covers are still there under « Qui lit ? »: no key may
    /// reach them. Tab once walked into the hidden field, and Enter opened a series behind
    /// the screen.
    void who_reads_keeps_the_keyboard_to_itself()
    {
        const QByteArray seating = aSeating(
            {aProfile(u"p"_s, u"Principal"_s, u"emeraude"_s, u"principal"_s, true),
             aProfile(u"r"_s, u"Lecteur"_s, u"corail"_s, u"reader"_s)},
            u"p"_s, false);
        const QByteArray shelf =
            aReply(200, QByteArrayLiteral("application/json"),
                   aPage({aSeries(u"aa"_s, u"Derrière"_s, 3, true)}));
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [=](const QByteArray &request) {
            return request.startsWith("GET /profiles ") ? seating : shelf;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        QQuickWindow *window = shown(engine);
        QVERIFY(window);
        auto *whoReads = window->findChild<QQuickItem *>(u"who-reads"_s);
        auto *searchField = window->findChild<QQuickItem *>(u"search-field"_s);
        QVERIFY(whoReads);
        QVERIFY(searchField);
        QTRY_VERIFY(whoReads->isVisible());
        QTRY_VERIFY(whoReads->hasActiveFocus());

        const auto inside = [whoReads, window] {
            for (QQuickItem *at = window->activeFocusItem(); at; at = at->parentItem()) {
                if (at == whoReads)
                    return true;
            }
            return false;
        };
        for (int i = 0; i < 12; ++i) {
            QTest::keyClick(window, Qt::Key_Tab);
            QVERIFY2(inside(), qPrintable(u"Tab %1 left « Qui lit ? »"_s.arg(i + 1)));
        }
        QTest::keyClick(window, Qt::Key_Slash);
        QTest::qWait(50);
        QVERIFY(!searchField->hasActiveFocus());
        QVERIFY(inside());
    }

    /// Another profile sees another shelf: what the first one was shown — its tiles, their
    /// covers, its search, the page it had open — goes, and the wall is asked again for the
    /// second, once.
    void switching_profile_reloads_the_shelf_and_drops_the_covers()
    {
        const QJsonArray both{
            aProfile(u"p"_s, u"Principal"_s, u"emeraude"_s, u"principal"_s, true),
            aProfile(u"r"_s, u"Lecteur"_s, u"corail"_s, u"reader"_s),
        };
        const QByteArray before = aSeating(both, u"p"_s, true);
        const QByteArray after = aSeating(both, u"r"_s, false);
        const QByteArray firstShelf =
            aReply(200, QByteArrayLiteral("application/json"),
                   aPage({aSeries(u"aa"_s, u"Pour le principal"_s, 3, true)}));
        const QByteArray secondShelf =
            aReply(200, QByteArrayLiteral("application/json"),
                   aPage({aSeries(u"bb"_s, u"Pour le lecteur"_s, 2, false)}));
        const QByteArray cover = aReply(200, QByteArrayLiteral("image/png"), aCover());
        const QByteArray nothing = aReply(200, QByteArrayLiteral("application/json"), "[]");
        auto changed = std::make_shared<bool>(false);

        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [=](const QByteArray &request) -> QByteArray {
            if (request.startsWith("GET /profiles "))
                return *changed ? after : before;
            if (request.startsWith("PUT /profiles/active")) {
                *changed = true;
                return after;
            }
            if (request.startsWith("GET /series?"))
                return *changed ? secondShelf : firstShelf;
            if (request.contains("/cover"))
                return cover;
            return nothing;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        QQuickWindow *window = shown(engine);
        QVERIFY(window);
        auto *navigation =
            engine.singletonInstance<Navigation *>(qmlTypeId("Leaf", 1, 0, "Navigation"));
        auto *shelf = engine.singletonInstance<Shelf *>(qmlTypeId("Leaf", 1, 0, "Shelf"));
        QVERIFY(navigation);
        QVERIFY(shelf);

        auto *grid = window->findChild<QQuickItem *>(u"shelf-grid"_s);
        QVERIFY(grid);
        QQuickItem *found = nullptr;
        QTRY_VERIFY((found = itemNamed(grid, u"cover-aa"_s)));
        const QPointer<QQuickItem> oldCover(found);
        QTRY_COMPARE(oldCover->property("status").toInt(), 1);

        // A search under way and a series open: the change of profile has to undo both.
        shelf->searchFor(u"pour"_s);
        QTRY_VERIFY(pretend.heard.contains("pour"));
        QVERIFY(navigation->open(Navigation::Destination::Series, {{u"series"_s, u"aa"_s}}));
        auto *square = window->findChild<QQuickItem *>(u"profile-square"_s);
        QVERIFY(square);
        QTRY_VERIFY(square->isVisible());
        QTRY_VERIFY(square->mapToScene(QPointF(0, 0)).x() > window->width() / 2);
        clickOn(window, square);
        auto *whoReads = window->findChild<QQuickItem *>(u"who-reads"_s);
        QVERIFY(whoReads);
        QTRY_VERIFY(whoReads->isVisible());

        pretend.heard.clear();
        QQuickItem *reader = itemNamed(whoReads, u"who-reads-square-r"_s);
        QVERIFY(reader);
        clickOn(window, reader);

        QTRY_VERIFY(pretend.heard.contains("PUT /profiles/active"));
        QVERIFY(!pretend.heard.contains("\"code\""));
        QTRY_VERIFY(!whoReads->isVisible());
        QTRY_COMPARE(navigation->destination(), Navigation::Destination::Shelf);
        QCOMPARE(navigation->property("canGoBack").toBool(), false);
        QCOMPARE(shelf->query(), QString());
        QTRY_VERIFY(itemNamed(grid, u"tile-bb"_s));
        QVERIFY(!itemNamed(grid, u"tile-aa"_s));
        // The first profile's cover went with its tile rather than waiting on screen for the
        // second profile's answer.
        QVERIFY(oldCover.isNull());
        // Once: clearing the search and letting go of the wall were two requests, of which
        // the first was thrown away.
        QTest::qWait(400);
        QCOMPARE(pretend.heard.count("GET /series?"), 1);
        QCOMPARE(textNamed(square, u"profile-square-face-initial"_s), u"L"_s);
        QTRY_VERIFY(square->hasActiveFocus());
    }

    /// Ticking « Retenir » and choosing the profile already reading changes where this
    /// device opens, nothing else: no shelf is asked again.
    void keeping_your_own_profile_does_not_reload_the_shelf()
    {
        const QJsonArray both{
            aProfile(u"p"_s, u"Principal"_s, u"emeraude"_s, u"principal"_s, true),
            aProfile(u"r"_s, u"Lecteur"_s, u"corail"_s, u"reader"_s),
        };
        const QByteArray loose = aSeating(both, u"p"_s, false);
        const QByteArray kept = aSeating(both, u"p"_s, true);
        const QByteArray empty =
            aReply(200, QByteArrayLiteral("application/json"), aPage({}));
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [=](const QByteArray &request) {
            if (request.startsWith("GET /profiles "))
                return loose;
            if (request.startsWith("PUT /profiles/active"))
                return kept;
            return empty;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        QQuickWindow *window = shown(engine);
        QVERIFY(window);
        auto *whoReads = window->findChild<QQuickItem *>(u"who-reads"_s);
        QVERIFY(whoReads);
        QTRY_VERIFY(whoReads->isVisible());
        QQuickItem *keep = itemNamed(whoReads, u"who-reads-keep"_s);
        QQuickItem *mine = itemNamed(whoReads, u"who-reads-square-p"_s);
        QVERIFY(keep);
        QVERIFY(mine);
        clickOn(window, keep);
        QTRY_VERIFY(keep->property("checked").toBool());

        QTest::qWait(200);
        pretend.heard.clear();
        clickOn(window, mine);
        QTRY_VERIFY(pretend.heard.contains("PUT /profiles/active"));
        QVERIFY(pretend.heard.contains(R"("keep":true)"));
        QTRY_VERIFY(!whoReads->isVisible());
        QTest::qWait(300);
        QVERIFY(!pretend.heard.contains("GET /series?"));
    }

    /// Several profiles and none kept on this device: the launch asks who reads, before the
    /// shelf is anybody's.
    void who_reads_opens_at_start_when_several_profiles_and_none_kept()
    {
        const QByteArray seating = aSeating(
            {aProfile(u"p"_s, u"Principal"_s, u"emeraude"_s, u"principal"_s, true),
             aProfile(u"r"_s, u"Lecteur"_s, u"corail"_s, u"reader"_s)},
            u"p"_s, false);
        const QByteArray empty =
            aReply(200, QByteArrayLiteral("application/json"), aPage({}));
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [=](const QByteArray &request) {
            return request.startsWith("GET /profiles ") ? seating : empty;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        QQuickWindow *window = shown(engine);
        QVERIFY(window);
        auto *whoReads = window->findChild<QQuickItem *>(u"who-reads"_s);
        QVERIFY(whoReads);
        QTRY_VERIFY(whoReads->isVisible());
        QVERIFY(itemNamed(whoReads, u"who-reads-tile-p"_s));
        QVERIFY(itemNamed(whoReads, u"who-reads-tile-r"_s));
        QVERIFY(shownNamed(whoReads, u"who-reads-keep"_s));
        QVERIFY(shownNamed(whoReads, u"who-reads-manage"_s));
        QVERIFY(shownNamed(whoReads, u"who-reads-close"_s));
    }

    /// A restricted profile leaves only with the code: no tile that adds, no cross, no
    /// Escape, no settings — the sentence that says why, and the way back to its shelf.
    void a_restricted_profile_sees_neither_add_nor_close()
    {
        const QByteArray seating = aSeating(
            {aProfile(u"p"_s, u"Principal"_s, u"emeraude"_s, u"principal"_s, true),
             aProfile(u"l"_s, u"Enfant"_s, u"azur"_s, u"restricted"_s, false, 10)},
            u"l"_s, false, u"restricted"_s);
        const QByteArray empty =
            aReply(200, QByteArrayLiteral("application/json"), aPage({}));
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [=](const QByteArray &request) {
            return request.startsWith("GET /profiles ") ? seating : empty;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        QQuickWindow *window = shown(engine);
        QVERIFY(window);
        auto *square = window->findChild<QQuickItem *>(u"profile-square"_s);
        auto *whoReads = window->findChild<QQuickItem *>(u"who-reads"_s);
        QVERIFY(square);
        QVERIFY(whoReads);
        QTRY_VERIFY(square->isVisible());
        QTRY_VERIFY(square->mapToScene(QPointF(0, 0)).x() > window->width() / 2);
        // Not asked at launch: a restricted device opens on its profile, kept or not.
        QVERIFY(!whoReads->isVisible());
        QVERIFY(shownNamed(square, u"profile-square-face-badge"_s));

        clickOn(window, square);
        QTRY_VERIFY(whoReads->isVisible());
        QVERIFY(hiddenNamed(whoReads, u"who-reads-add"_s));
        QVERIFY(hiddenNamed(whoReads, u"who-reads-close"_s));
        QVERIFY(hiddenNamed(whoReads, u"who-reads-manage"_s));
        QVERIFY(hiddenNamed(whoReads, u"who-reads-keep"_s));
        QVERIFY(shownNamed(whoReads, u"who-reads-needs-code"_s));
        QCOMPARE(textNamed(whoReads, u"who-reads-needs-code"_s), Words::switchNeedsCode());
        QVERIFY(shownNamed(whoReads, u"who-reads-back"_s));

        // Choosing the principal goes to the code, never straight to the server.
        QQuickItem *principal = itemNamed(whoReads, u"who-reads-square-p"_s);
        QVERIFY(principal);
        pretend.heard.clear();
        clickOn(window, principal);
        QTest::qWait(200);
        QVERIFY(!pretend.heard.contains("PUT /profiles/active"));

        QTest::keyClick(window, Qt::Key_Escape);
        QTest::qWait(50);
        QVERIFY(whoReads->isVisible());

        clickOn(window, itemNamed(whoReads, u"who-reads-back"_s));
        QTRY_VERIFY(!whoReads->isVisible());
    }

    /// Without the right, the bar does not offer the import: the server would refuse it.
    void a_reader_does_not_see_import()
    {
        const QByteArray seating = aSeating(
            {aProfile(u"p"_s, u"Principal"_s, u"emeraude"_s, u"principal"_s, true),
             aProfile(u"r"_s, u"Lecteur"_s, u"corail"_s, u"reader"_s)},
            u"r"_s, true);
        const QByteArray empty =
            aReply(200, QByteArrayLiteral("application/json"), aPage({}));
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [=](const QByteArray &request) {
            return request.startsWith("GET /profiles ") ? seating : empty;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        QQuickWindow *window = shown(engine);
        QVERIFY(window);
        auto *square = window->findChild<QQuickItem *>(u"profile-square"_s);
        auto *importButton = window->findChild<QQuickItem *>(u"import-button"_s);
        auto *sortButton = window->findChild<QQuickItem *>(u"sort-button"_s);
        QVERIFY(square);
        QVERIFY(importButton);
        QVERIFY(sortButton);
        QTRY_VERIFY(square->isVisible());
        QCOMPARE(textNamed(square, u"profile-square-face-initial"_s), u"L"_s);
        // A reader carries no badge: neither crowned nor locked.
        QVERIFY(hiddenNamed(square, u"profile-square-face-badge"_s));
        QVERIFY(sortButton->isVisible());
        QVERIFY(!importButton->isVisible());
    }

    /// Eight rounds, always: how many are filled says how much was typed, never how long the
    /// code is — and nothing past eight is taken, since no code is longer.
    void the_code_dialog_shows_eight_rounds_whatever_the_length()
    {
        const QByteArray before = aRestrictedSeat();
        const QByteArray after = aRestrictedSeat(u"p"_s);
        const QByteArray empty =
            aReply(200, QByteArrayLiteral("application/json"), aPage({}));
        auto changed = std::make_shared<bool>(false);
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [=](const QByteArray &request) {
            if (request.startsWith("PUT /profiles/active")) {
                *changed = true;
                return after;
            }
            if (request.startsWith("GET /profiles "))
                return *changed ? after : before;
            return empty;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        QQuickWindow *window = shown(engine);
        QVERIFY(window);
        QQuickItem *dialog = askedForTheCode(window);
        QVERIFY(dialog);
        QCOMPARE(textNamed(dialog, u"code-title"_s), Words::switchToProfile(u"Principal"_s));
        QCOMPARE(textNamed(dialog, u"code-sentence"_s), Words::leaveNeedsCode());
        QVERIFY(shownNamed(dialog, u"code-from"_s));
        QVERIFY(shownNamed(dialog, u"code-to"_s));

        const auto filled = [dialog] {
            int count = 0;
            for (int i = 0; i < 8; ++i) {
                QQuickItem *round = itemNamed(dialog, u"code-round-%1"_s.arg(i));
                if (!round)
                    return -1;
                count += round->property("filled").toBool() ? 1 : 0;
            }
            return itemNamed(dialog, u"code-round-8"_s) ? -1 : count;
        };
        QCOMPARE(filled(), 0);
        typeInto(window, u"ab"_s);
        QTRY_COMPARE(filled(), 2);
        typeInto(window, u"cdéfghijk"_s);
        QTRY_COMPARE(filled(), 8);

        pretend.heard.clear();
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(pretend.heard.contains("PUT /profiles/active"));
        QVERIFY(pretend.heard.contains(u"\"code\":\"abcdéfgh\""_s.toUtf8()));
        QTRY_VERIFY(!dialog->isVisible());
        QTRY_VERIFY(!window->findChild<QQuickItem *>(u"who-reads"_s)->isVisible());
    }

    /// A wrong code empties the rounds and says so, and the window stays for another try.
    void a_wrong_code_clears_the_rounds_and_says_so()
    {
        const QByteArray seat = aRestrictedSeat();
        const QByteArray refused =
            aReply(403, QByteArrayLiteral("application/json"), R"({"error":"code"})");
        const QByteArray empty =
            aReply(200, QByteArrayLiteral("application/json"), aPage({}));
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [=](const QByteArray &request) {
            if (request.startsWith("PUT /profiles/active"))
                return refused;
            return request.startsWith("GET /profiles ") ? seat : empty;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        QQuickWindow *window = shown(engine);
        QVERIFY(window);
        QQuickItem *dialog = askedForTheCode(window);
        QVERIFY(dialog);
        QVERIFY(hiddenNamed(dialog, u"code-trouble"_s));
        typeInto(window, u"faux"_s);
        clickOn(window, itemNamed(dialog, u"code-confirm"_s));

        QTRY_VERIFY(shownNamed(dialog, u"code-trouble"_s));
        QCOMPARE(textNamed(dialog, u"code-trouble"_s), Words::wrongCode());
        QCOMPARE(textNamed(dialog, u"code-field"_s), QString());
        QVERIFY(!itemNamed(dialog, u"code-round-0"_s)->property("filled").toBool());
        QVERIFY(dialog->isVisible());
    }

    /// « Code oublié ? » takes the recovery code and a new code, twice, and says where the code
    /// is replaced when there is no recovery code, as a command that can be selected and
    /// copied; « Retour » brings the rounds back.
    void a_forgotten_code_takes_the_recovery_code_and_shows_the_server_command()
    {
        const QByteArray seat = aRestrictedSeat();
        const QByteArray empty =
            aReply(200, QByteArrayLiteral("application/json"), aPage({}));
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [=](const QByteArray &request) {
            return request.startsWith("GET /profiles ") ? seat : empty;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        QQuickWindow *window = shown(engine);
        QVERIFY(window);
        QQuickItem *dialog = askedForTheCode(window);
        QVERIFY(dialog);
        QVERIFY(hiddenNamed(dialog, u"code-command"_s));

        clickOn(window, itemNamed(dialog, u"code-forgot"_s));
        QTRY_VERIFY(shownNamed(dialog, u"code-command"_s));
        QCOMPARE(textNamed(dialog, u"code-command"_s), Words::resetCodeCommand());
        QCOMPARE(itemNamed(dialog, u"code-command"_s)->property("selectByMouse").toBool(), true);
        QCOMPARE(textNamed(dialog, u"code-sentence"_s), Words::recoverExplained());
        QVERIFY(shownNamed(dialog, u"code-recovery-field"_s));
        QVERIFY(shownNamed(dialog, u"code-new-field"_s));
        QVERIFY(shownNamed(dialog, u"code-new-again-field"_s));
        QVERIFY(shownNamed(dialog, u"code-replace"_s));
        QCOMPARE(textNamed(dialog, u"code-no-recovery"_s), Words::noRecoveryCommand());
        QVERIFY(hiddenNamed(dialog, u"code-rounds"_s));
        QVERIFY(hiddenNamed(dialog, u"code-confirm"_s));

        QVERIFY(laidOut(window));
        clickOn(window, itemNamed(dialog, u"code-back"_s));
        QTRY_VERIFY(shownNamed(dialog, u"code-rounds"_s));
        QVERIFY(hiddenNamed(dialog, u"code-command"_s));
    }

    /// Cancelling the code from a restricted profile goes back to « Qui lit ? », still locked:
    /// the shelf is reached by its own button there, never by backing out of the code.
    void cancelling_the_code_from_a_restricted_profile_keeps_who_reads_open()
    {
        const QByteArray seat = aRestrictedSeat();
        const QByteArray empty =
            aReply(200, QByteArrayLiteral("application/json"), aPage({}));
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [=](const QByteArray &request) {
            return request.startsWith("GET /profiles ") ? seat : empty;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        QQuickWindow *window = shown(engine);
        QVERIFY(window);
        QQuickItem *dialog = askedForTheCode(window);
        QVERIFY(dialog);
        typeInto(window, u"abc"_s);
        pretend.heard.clear();
        clickOn(window, itemNamed(dialog, u"code-cancel"_s));

        QTRY_VERIFY(!dialog->isVisible());
        auto *whoReads = window->findChild<QQuickItem *>(u"who-reads"_s);
        QTest::qWait(100);
        QVERIFY(whoReads->isVisible());
        QVERIFY(shownNamed(whoReads, u"who-reads-needs-code"_s));
        QVERIFY(!pretend.heard.contains("PUT /profiles/active"));
        // What was typed went with the window.
        QCOMPARE(textNamed(dialog, u"code-field"_s), QString());
    }

    /// From a profile free to leave, towards the principal its code protects: nothing is
    /// being left behind a lock, so the window only asks for the code.
    void a_reader_going_to_the_protected_principal_is_asked_the_code()
    {
        const QByteArray seat = aReaderSeatBehindTheCode();
        const QByteArray empty =
            aReply(200, QByteArrayLiteral("application/json"), aPage({}));
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [=](const QByteArray &request) {
            return request.startsWith("GET /profiles ") ? seat : empty;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        QQuickWindow *window = shown(engine);
        QVERIFY(window);
        QQuickItem *dialog = askedForTheCode(window);
        QVERIFY(dialog);
        QCOMPARE(textNamed(dialog, u"code-sentence"_s), Words::enterCode());
    }

    /// A device whose reader was deleted must choose, and sits on the principal only to hold
    /// its key. Read as the principal, touching the protected principal's tile sent the switch
    /// without the code, and the server's refusal had no window to be said in: nothing
    /// happened at all.
    void a_device_that_must_choose_reaches_the_protected_principal_through_the_code()
    {
        const QByteArray seat = aReply(
            200, QByteArrayLiteral("application/json"),
            QJsonDocument(
                QJsonObject{
                    {u"profiles"_s,
                     QJsonArray{aProfile(u"p"_s, u"Principal"_s, u"emeraude"_s,
                                         u"principal"_s, true),
                                aProfile(u"r"_s, u"Lecteur"_s, u"corail"_s, u"reader"_s)}},
                    {u"active"_s, u"p"_s},
                    {u"kept"_s, false},
                    {u"choosing"_s, true},
                    {u"held"_s, false},
                    {u"code"_s, u"everything"_s},
                })
                .toJson(QJsonDocument::Compact));
        const QByteArray empty =
            aReply(200, QByteArrayLiteral("application/json"), aPage({}));
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        const QByteArray seated = aSeating(
            {aProfile(u"p"_s, u"Principal"_s, u"emeraude"_s, u"principal"_s, true),
             aProfile(u"r"_s, u"Lecteur"_s, u"corail"_s, u"reader"_s)},
            u"p"_s, false, u"everything"_s);
        pretend.answerFor = [=](const QByteArray &request) {
            if (request.startsWith("GET /profiles "))
                return seat;
            return request.startsWith("PUT /profiles/active") ? seated : empty;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        QQuickWindow *window = shown(engine);
        QVERIFY(window);
        auto *whoReads = window->findChild<QQuickItem *>(u"who-reads"_s);
        auto *popup = window->findChild<QObject *>(u"code-dialog"_s);
        auto *dialog = window->findChild<QQuickItem *>(u"code-dialog-body"_s);
        QVERIFY(whoReads);
        QVERIFY(popup);
        QVERIFY(dialog);
        QTRY_VERIFY(whoReads->isVisible());
        QVERIFY(laidOut(window));
        pretend.heard.clear();
        clickOn(window, itemNamed(whoReads, u"who-reads-square-p"_s));
        QTRY_VERIFY(dialog->isVisible());
        QCOMPARE(popup->property("mode").toString(), u"switch"_s);
        QVERIFY(!pretend.heard.contains("PUT /profiles/active"));

        // Adding is the principal's power, and this device is not the principal's: the door
        // is the code, not the principal's whole form.
        QVERIFY(laidOut(window));
        clickOn(window, itemNamed(dialog, u"code-cancel"_s));
        QTRY_VERIFY(!dialog->isVisible());
        QMetaObject::invokeMethod(whoReads, "add");
        QTRY_VERIFY(dialog->isVisible());
        QCOMPARE(popup->property("mode").toString(), u"add"_s);
        QVERIFY(!window->findChild<QQuickItem *>(u"profile-dialog-body"_s)->isVisible());

        // And the code, once typed, seats it there: the principal it already sat on is still
        // a profile chosen, so both windows go.
        QVERIFY(laidOut(window));
        clickOn(window, itemNamed(dialog, u"code-cancel"_s));
        QTRY_VERIFY(!dialog->isVisible());
        QVERIFY(laidOut(window));
        clickOn(window, itemNamed(whoReads, u"who-reads-square-p"_s));
        QTRY_VERIFY(dialog->isVisible());
        typeInto(window, u"secret"_s);
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(pretend.heard.contains(R"("code":"secret")"));
        QTRY_VERIFY(!dialog->isVisible());
        QTRY_VERIFY(!whoReads->isVisible());
    }

    /// The principal read on the family laptop without « Retenir » and closed Leaf; whoever
    /// opens it next finds « Qui lit ? » on the principal. With the code guarding the way back,
    /// the cross, Escape or the principal's own tile left anyone on the principal without it.
    /// Now nothing leaves but a profile, and the principal's tile asks the code. Later in the
    /// session the screen closes freely, but the device still does not retain the principal:
    /// its tile asks the code again, and a reader stays free.
    void a_principal_left_unkept_behind_the_code_is_asked_it_at_launch()
    {
        const QByteArray loose = aSeating(
            {aProfile(u"p"_s, u"Principal"_s, u"emeraude"_s, u"principal"_s, true),
             aProfile(u"r"_s, u"Lecteur"_s, u"corail"_s, u"reader"_s)},
            u"p"_s, false, u"everything"_s);
        const QByteArray empty =
            aReply(200, QByteArrayLiteral("application/json"), aPage({}));
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [=](const QByteArray &request) {
            return request.startsWith("GET /profiles ") || request.startsWith("PUT /profiles/active")
                       ? loose
                       : empty;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        QQuickWindow *window = shown(engine);
        QVERIFY(window);
        auto *whoReads = window->findChild<QQuickItem *>(u"who-reads"_s);
        auto *popup = window->findChild<QObject *>(u"code-dialog"_s);
        auto *dialog = window->findChild<QQuickItem *>(u"code-dialog-body"_s);
        auto *square = window->findChild<QQuickItem *>(u"profile-square"_s);
        QVERIFY(whoReads);
        QVERIFY(popup);
        QVERIFY(dialog);
        QVERIFY(square);
        QTRY_VERIFY(whoReads->isVisible());
        QVERIFY(hiddenNamed(whoReads, u"who-reads-close"_s));
        QVERIFY(hiddenNamed(whoReads, u"who-reads-add"_s));
        QVERIFY(hiddenNamed(whoReads, u"who-reads-manage"_s));
        QTest::keyClick(window, Qt::Key_Escape);
        QTest::qWait(50);
        QVERIFY(whoReads->isVisible());

        QVERIFY(laidOut(window));
        pretend.heard.clear();
        clickOn(window, itemNamed(whoReads, u"who-reads-square-p"_s));
        QTRY_VERIFY(dialog->isVisible());
        QCOMPARE(popup->property("mode").toString(), u"switch"_s);
        QVERIFY(!pretend.heard.contains("PUT /profiles/active"));
        typeInto(window, u"secret"_s);
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(pretend.heard.contains(R"("code":"secret")"));
        QTRY_VERIFY(!dialog->isVisible());
        QTRY_VERIFY(!whoReads->isVisible());

        // Opened from the square later, the screen is the principal's to leave.
        QVERIFY(laidOut(window));
        clickOn(window, square);
        QTRY_VERIFY(whoReads->isVisible());
        QVERIFY(shownNamed(whoReads, u"who-reads-close"_s));
        QVERIFY(shownNamed(whoReads, u"who-reads-add"_s));
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!whoReads->isVisible());

        // But a seat not retained is not a way round the code: the server asks it to stay.
        QVERIFY(laidOut(window));
        clickOn(window, square);
        QTRY_VERIFY(whoReads->isVisible());
        QVERIFY(laidOut(window));
        pretend.heard.clear();
        clickOn(window, itemNamed(whoReads, u"who-reads-square-p"_s));
        QTRY_VERIFY(dialog->isVisible());
        QVERIFY(!pretend.heard.contains("PUT /profiles/active"));
        QVERIFY(laidOut(window));
        clickOn(window, itemNamed(dialog, u"code-cancel"_s));
        QTRY_VERIFY(!dialog->isVisible());
        QVERIFY(laidOut(window));
        clickOn(window, itemNamed(whoReads, u"who-reads-square-r"_s));
        QTRY_VERIFY(pretend.heard.contains("PUT /profiles/active"));
        QVERIFY(!pretend.heard.contains(R"("code")"));
        QVERIFY(!dialog->isVisible());
    }

    /// A device that keeps the principal opens on it, code or no code — the principal chose
    /// it there — and « Qui lit ? » opened from the square is the principal's to leave.
    void a_principal_kept_here_opens_on_it_and_leaves_who_reads_freely()
    {
        const QByteArray kept = aSeating(
            {aProfile(u"p"_s, u"Principal"_s, u"emeraude"_s, u"principal"_s, true),
             aProfile(u"r"_s, u"Lecteur"_s, u"corail"_s, u"reader"_s)},
            u"p"_s, true, u"everything"_s);
        const QByteArray empty =
            aReply(200, QByteArrayLiteral("application/json"), aPage({}));
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [=](const QByteArray &request) {
            return request.startsWith("GET /profiles ") ? kept : empty;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        QQuickWindow *window = shown(engine);
        QVERIFY(window);
        auto *whoReads = window->findChild<QQuickItem *>(u"who-reads"_s);
        auto *dialog = window->findChild<QQuickItem *>(u"code-dialog-body"_s);
        auto *square = window->findChild<QQuickItem *>(u"profile-square"_s);
        QVERIFY(whoReads);
        QVERIFY(dialog);
        QVERIFY(square);
        QTRY_VERIFY(square->isVisible()
                    && square->mapToScene(QPointF(0, 0)).x() > window->width() / 2);
        QTest::qWait(100);
        QVERIFY(!whoReads->isVisible());

        clickOn(window, square);
        QTRY_VERIFY(whoReads->isVisible());
        QVERIFY(shownNamed(whoReads, u"who-reads-close"_s));
        QVERIFY(laidOut(window));
        clickOn(window, itemNamed(whoReads, u"who-reads-square-p"_s));
        QTRY_VERIFY(!whoReads->isVisible());
        QVERIFY(!dialog->isVisible());
    }

    /// Ten wrong codes and the server pauses: said as tries from this device, the way the
    /// link says it, and not as the wrong keys `Server` would word a 429 as.
    void too_many_wrong_codes_say_how_long_to_wait()
    {
        const QByteArray seat = aRestrictedSeat();
        // `aReply` with the one header it does not write.
        const QByteArray paused =
            aReply(429, QByteArrayLiteral("application/json"), R"({"error":"slow down"})")
                .replace("HTTP/1.1 429 .\r\n", "HTTP/1.1 429 .\r\nRetry-After: 30\r\n");
        const QByteArray empty =
            aReply(200, QByteArrayLiteral("application/json"), aPage({}));
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [=](const QByteArray &request) {
            if (request.startsWith("PUT /profiles/active"))
                return paused;
            return request.startsWith("GET /profiles ") ? seat : empty;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        QQuickWindow *window = shown(engine);
        QVERIFY(window);
        QQuickItem *dialog = askedForTheCode(window);
        QVERIFY(dialog);
        typeInto(window, u"abcd"_s);
        QTest::keyClick(window, Qt::Key_Return);

        QTRY_VERIFY(shownNamed(dialog, u"code-trouble"_s));
        QCOMPARE(textNamed(dialog, u"code-trouble"_s), Words::tooManyTries(u"30"_s));
        QVERIFY(dialog->isVisible());
    }

    /// Adding from another profile: the code is only collected here, for the profile window
    /// to send with the profile it creates — the server is not asked anything yet.
    void adding_hands_the_code_on_without_asking_the_server()
    {
        const QByteArray seat = aReaderSeatBehindTheCode();
        const QByteArray empty =
            aReply(200, QByteArrayLiteral("application/json"), aPage({}));
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [=](const QByteArray &request) {
            return request.startsWith("GET /profiles ") ? seat : empty;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        QQuickWindow *window = shown(engine);
        QVERIFY(window);
        auto *popup = window->findChild<QObject *>(u"code-dialog"_s);
        QVERIFY(popup);
        QSignalSpy typed(popup, SIGNAL(codeTyped(QString)));
        QQuickItem *dialog = askedForTheCode(window, u"who-reads-add"_s);
        QVERIFY(dialog);
        QCOMPARE(textNamed(dialog, u"code-title"_s), Words::addAProfile());
        QCOMPARE(textNamed(dialog, u"code-sentence"_s), Words::addNeedsCode());
        QVERIFY(shownNamed(dialog, u"code-from"_s));
        QVERIFY(hiddenNamed(dialog, u"code-to"_s));

        pretend.heard.clear();
        typeInto(window, u"secret"_s);
        QTest::keyClick(window, Qt::Key_Return);

        QTRY_COMPARE(typed.count(), 1);
        QCOMPARE(typed.first().first().toString(), u"secret"_s);
        QTRY_VERIFY(!dialog->isVisible());
        QTest::qWait(200);
        QVERIFY(!pretend.heard.contains("PUT "));
        QVERIFY(!pretend.heard.contains("POST "));
    }

    /// The principal's window offers the kind and the rights, none ticked for a new profile,
    /// and sends what was ticked. A refusal is said in the window, which stays open.
    void adding_a_reader_from_the_principal_sends_the_checked_rights()
    {
        const QByteArray seat = aSeating(
            {aProfile(u"p"_s, u"Principal"_s, u"emeraude"_s, u"principal"_s, true)}, u"p"_s,
            true);
        const QByteArray empty =
            aReply(200, QByteArrayLiteral("application/json"), aPage({}));
        const QByteArray taken = aReply(409, QByteArrayLiteral("application/json"),
                                        QByteArrayLiteral(R"({"error":"name taken"})"));
        const QByteArray made = aReply(201, QByteArrayLiteral("application/json"),
                                       QByteArrayLiteral("{}"));
        int posts = 0;
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [&](const QByteArray &request) {
            if (request.startsWith("GET /profiles "))
                return seat;
            if (request.startsWith("POST /profiles "))
                return ++posts == 1 ? taken : made;
            return empty;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        QQuickWindow *window = shown(engine);
        QVERIFY(window);
        QQuickItem *dialog = profileDialogFromAdd(window);
        QVERIFY(dialog);
        QCOMPARE(textNamed(dialog, u"profile-title"_s), Words::newProfile());
        QVERIFY(shownNamed(dialog, u"profile-kind"_s));
        QVERIFY(shownNamed(dialog, u"profile-may-import"_s));
        QVERIFY(hiddenNamed(dialog, u"profile-code"_s));
        for (const QString &right : {u"import"_s, u"edit"_s, u"erase"_s})
            QVERIFY(!itemNamed(dialog, u"profile-may-"_s + right)->property("checked").toBool());
        // The principal's colour is taken: the new profile starts on the next one.
        QVERIFY(!itemNamed(dialog, u"profile-colour-emeraude"_s)->isEnabled());

        typeInto(window, u"Inès"_s);
        clickOn(window, itemNamed(dialog, u"profile-may-edit"_s));
        QCOMPARE(textNamed(dialog, u"profile-confirm-text"_s), u"Ajouter Inès"_s);
        pretend.heard.clear();
        clickOn(window, itemNamed(dialog, u"profile-confirm"_s));
        QTRY_VERIFY(shownNamed(dialog, u"profile-trouble"_s));
        QVERIFY(dialog->isVisible());

        pretend.heard.clear();
        clickOn(window, itemNamed(dialog, u"profile-confirm"_s));
        QTRY_VERIFY(!dialog->isVisible());
        const QJsonObject sent = bodySent(pretend.heard, "POST /profiles ");
        QCOMPARE(sent.value(u"name"_s).toString(), u"Inès"_s);
        QCOMPARE(sent.value(u"kind"_s).toString(), u"reader"_s);
        QCOMPARE(sent.value(u"color"_s).toString(), u"sapin"_s);
        QCOMPARE(sent.value(u"rights"_s).toObject(),
                 (QJsonObject{{u"import"_s, false}, {u"edit"_s, true}, {u"delete"_s, false}}));
        QVERIFY(!sent.contains(u"code"_s));
        QVERIFY(!sent.contains(u"principalCode"_s));
        // The lists refresh by themselves: the window asked again once the profile was made.
        QTRY_VERIFY(pretend.heard.contains("GET /profiles "));
    }

    /// The first restricted profile makes the code in the same window, typed twice.
    void adding_a_restricted_profile_with_no_code_sends_code_and_code_again()
    {
        const QByteArray seat = aSeating(
            {aProfile(u"p"_s, u"Principal"_s, u"emeraude"_s, u"principal"_s, true)}, u"p"_s,
            true);
        const QByteArray empty =
            aReply(200, QByteArrayLiteral("application/json"), aPage({}));
        const QByteArray made = aReply(201, QByteArrayLiteral("application/json"),
                                       QByteArrayLiteral("{}"));
        const QByteArray recovery = aReply(
            200, QByteArrayLiteral("application/json"),
            QByteArrayLiteral(R"({"recoveryCode":"K7QM-2XWD-HP9T-C4VN"})"));
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [=](const QByteArray &request) {
            if (request.startsWith("GET /profiles "))
                return seat;
            if (request.startsWith("POST /profiles/code/recovery "))
                return recovery;
            return request.startsWith("POST /profiles ") ? made : empty;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        QQuickWindow *window = shown(engine);
        QVERIFY(window);
        QQuickItem *dialog = profileDialogFromAdd(window);
        QVERIFY(dialog);
        typeInto(window, u"Léo"_s);
        clickOn(window, itemNamed(dialog, u"profile-kind-lock"_s));
        QTRY_VERIFY(shownNamed(dialog, u"profile-code"_s));
        QVERIFY(laidOut(window));
        QVERIFY(hiddenNamed(dialog, u"profile-may-import"_s));
        QCOMPARE(textNamed(dialog, u"profile-age"_s), Words::yearsOld(10));
        clickOn(window, itemNamed(dialog, u"profile-age-more"_s));
        QCOMPARE(textNamed(dialog, u"profile-age"_s), Words::yearsOld(11));
        // Not ready until the code is typed twice.
        QVERIFY(!itemNamed(dialog, u"profile-confirm"_s)->property("ready").toBool());

        clickOn(window, itemNamed(dialog, u"profile-code"_s));
        typeInto(window, u"1234"_s);
        clickOn(window, itemNamed(dialog, u"profile-code-again"_s));
        typeInto(window, u"1235"_s);
        pretend.heard.clear();
        clickOn(window, itemNamed(dialog, u"profile-confirm"_s));
        QTRY_VERIFY(!dialog->isVisible());
        const QJsonObject sent = bodySent(pretend.heard, "POST /profiles ");
        QCOMPARE(sent.value(u"kind"_s).toString(), u"restricted"_s);
        QCOMPARE(sent.value(u"maxAge"_s).toInt(), 11);
        QCOMPARE(sent.value(u"code"_s).toString(), u"1234"_s);
        QCOMPARE(sent.value(u"codeAgain"_s).toString(), u"1235"_s);
        QVERIFY(!sent.contains(u"rights"_s));
        // Nothing typed outlives the window.
        QCOMPARE(itemNamed(dialog, u"profile-code"_s)->property("text").toString(), QString());
    }

    /// From a reader, the window is a name and a colour; the code typed before it goes with
    /// the request, and not one moment longer.
    void a_readers_new_profile_shows_neither_kind_nor_rights_and_sends_the_principal_code()
    {
        const QByteArray seat = aReaderSeatBehindTheCode();
        const QByteArray empty =
            aReply(200, QByteArrayLiteral("application/json"), aPage({}));
        const QByteArray made = aReply(201, QByteArrayLiteral("application/json"),
                                       QByteArrayLiteral("{}"));
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [=](const QByteArray &request) {
            if (request.startsWith("GET /profiles "))
                return seat;
            return request.startsWith("POST /profiles ") ? made : empty;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        QQuickWindow *window = shown(engine);
        QVERIFY(window);
        QVERIFY(askedForTheCode(window, u"who-reads-add"_s));
        typeInto(window, u"secret"_s);
        QTest::keyClick(window, Qt::Key_Return);

        auto *popup = window->findChild<QObject *>(u"profile-dialog"_s);
        auto *dialog = window->findChild<QQuickItem *>(u"profile-dialog-body"_s);
        QVERIFY(popup);
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->isVisible());
        QVERIFY(laidOut(window));
        QVERIFY(hiddenNamed(dialog, u"profile-kind"_s));
        QVERIFY(hiddenNamed(dialog, u"profile-may-import"_s));
        QVERIFY(shownNamed(dialog, u"profile-manages"_s));
        QCOMPARE(textNamed(dialog, u"profile-manages"_s), Words::principalManages(u"Principal"_s));

        typeInto(window, u"Hugo"_s);
        pretend.heard.clear();
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(!dialog->isVisible());
        const QJsonObject sent = bodySent(pretend.heard, "POST /profiles ");
        QCOMPARE(sent.value(u"name"_s).toString(), u"Hugo"_s);
        QCOMPARE(sent.value(u"kind"_s).toString(), u"reader"_s);
        QCOMPARE(sent.value(u"principalCode"_s).toString(), u"secret"_s);
        QVERIFY(!sent.contains(u"rights"_s));
        QCOMPARE(popup->property("principalCode").toString(), QString());
    }

    /// Nobody could leave a restricted profile without the code, so the switch that removes
    /// it is locked while one exists, and says which.
    void the_code_switch_is_locked_while_a_restricted_profile_exists()
    {
        const QByteArray seat = aRestrictedSeat(u"p"_s);
        const QByteArray empty =
            aReply(200, QByteArrayLiteral("application/json"), aPage({}));
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [=](const QByteArray &request) {
            return request.startsWith("GET /profiles ") ? seat : empty;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        QQuickWindow *window = shown(engine);
        QVERIFY(window);
        auto *square = window->findChild<QQuickItem *>(u"profile-square"_s);
        QVERIFY(square);
        QTRY_VERIFY(square->isVisible());
        const QVariantMap me{{u"id"_s, u"p"_s}, {u"name"_s, u"Principal"_s},
                             {u"color"_s, u"emeraude"_s}, {u"kind"_s, u"principal"_s},
                             {u"initial"_s, u"P"_s},
                             {u"rights"_s, QVariantMap{{u"import"_s, true}, {u"edit"_s, false},
                                                       {u"erase"_s, false}}}};
        QVERIFY(QMetaObject::invokeMethod(window, "openProfileDialog",
                                          Q_ARG(QVariant, u"self"_s), Q_ARG(QVariant, me),
                                          Q_ARG(QVariant, QString())));
        auto *dialog = window->findChild<QQuickItem *>(u"profile-dialog-body"_s);
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->isVisible());
        QVERIFY(laidOut(window));
        QCOMPARE(textNamed(dialog, u"profile-title"_s), Words::yourProfile());
        QQuickItem *lever = itemNamed(dialog, u"profile-code-switch"_s);
        QVERIFY(lever);
        QVERIFY(lever->property("on").toBool());
        QVERIFY(!lever->isEnabled());
        QCOMPARE(textNamed(dialog, u"profile-code-hint"_s), Words::codeHintHeld(u"Enfant"_s));
        QVERIFY(hiddenNamed(dialog, u"profile-kind"_s));

        clickOn(window, lever);
        QTest::qWait(100);
        QVERIFY(lever->property("on").toBool());
    }

    /// Deleting is asked first, says what it erases, and only then goes to the server.
    void deleting_a_profile_asks_for_confirmation_and_sends_delete()
    {
        const QByteArray seat = aSeating(
            {aProfile(u"p"_s, u"Principal"_s, u"emeraude"_s, u"principal"_s, true),
             aProfile(u"r"_s, u"Lecteur"_s, u"corail"_s, u"reader"_s)},
            u"p"_s, true);
        const QByteArray empty =
            aReply(200, QByteArrayLiteral("application/json"), aPage({}));
        const QByteArray gone = aReply(204, QByteArrayLiteral("application/json"), {});
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [=](const QByteArray &request) {
            if (request.startsWith("GET /profiles "))
                return seat;
            return request.startsWith("DELETE ") ? gone : empty;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        QQuickWindow *window = shown(engine);
        QVERIFY(window);
        const QVariantMap reader{{u"id"_s, u"r"_s}, {u"name"_s, u"Lecteur"_s}};
        pretend.heard.clear();
        QVERIFY(QMetaObject::invokeMethod(window, "removeProfile", Q_ARG(QVariant, reader)));
        auto *dialog = window->findChild<QQuickItem *>(u"profile-removal-body"_s);
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->isVisible());
        QVERIFY(laidOut(window));
        QCOMPARE(textNamed(dialog, u"profile-removal-title"_s), Words::eraseProfile(u"Lecteur"_s));
        QTest::qWait(100);
        QVERIFY(!pretend.heard.contains("DELETE "));

        clickOn(window, itemNamed(dialog, u"profile-removal-confirm"_s));
        QTRY_VERIFY(!dialog->isVisible());
        QVERIFY(pretend.heard.contains("DELETE /profiles/r "));
    }

    /// « Cet appareil » left the library tab for the devices tab, whole: the name, the date it
    /// was linked, linking another device and unlinking. The address stays with the library.
    void the_device_block_moved_to_devices()
    {
        const QByteArray seat = aSeating(
            {aProfile(u"p"_s, u"Principal"_s, u"emeraude"_s, u"principal"_s, true)}, u"p"_s,
            false);
        const QByteArray empty =
            aReply(200, QByteArrayLiteral("application/json"), aPage({}));
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [=](const QByteArray &request) {
            return request.startsWith("GET /profiles ") ? seat : empty;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        QQuickWindow *window = shown(engine);
        QVERIFY(window);
        QQuickItem *view = settingsOn(engine, window, u"library"_s);
        QVERIFY(view);
        QTRY_VERIFY(shownNamed(view, u"bond-address"_s));
        QVERIFY(shownNamed(view, u"bond-move"_s));
        for (const QString &moved : {u"bond-name"_s, u"bond-unlink"_s, u"bond-rename-field"_s})
            QVERIFY2(!shownNamed(view, moved), qPrintable(moved));

        view->setProperty("section", u"devices"_s);
        QTRY_VERIFY(shownNamed(view, u"bond-name"_s));
        QVERIFY(shownNamed(view, u"settings-this-device"_s));
        QVERIFY(!shownNamed(view, u"bond-address"_s));
    }

    /// A reader manages no one else: their own row, the pencil that opens « Votre profil », and
    /// who to ask for the rest — never the others' rows, the bins or the rights.
    void a_reader_sees_only_their_own_profile()
    {
        const QByteArray seat = aSeating(
            {aProfile(u"p"_s, u"Camille"_s, u"emeraude"_s, u"principal"_s, true),
             aProfile(u"r"_s, u"Lecteur"_s, u"corail"_s, u"reader"_s),
             aProfile(u"l"_s, u"Enfant"_s, u"azur"_s, u"restricted"_s, false, 10)},
            u"r"_s, true);
        const QByteArray empty =
            aReply(200, QByteArrayLiteral("application/json"), aPage({}));
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [=](const QByteArray &request) {
            return request.startsWith("GET /profiles ") ? seat : empty;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        QQuickWindow *window = shown(engine);
        QVERIFY(window);
        QQuickItem *view = settingsOn(engine, window, u"profiles"_s);
        QVERIFY(view);
        QTRY_VERIFY(shownNamed(view, u"profile-row-r"_s));
        QVERIFY(!itemNamed(view, u"profile-row-p"_s));
        QVERIFY(!itemNamed(view, u"profile-row-l"_s));
        QVERIFY(!shownNamed(view, u"profile-row-r-remove"_s));
        QVERIFY(!shownNamed(view, u"profiles-add"_s));
        QVERIFY(!shownNamed(view, u"settings-rights"_s));
        QCOMPARE(textNamed(view, u"profiles-managed"_s),
                 Words::managedByPrincipal(u"Camille"_s));
        QCOMPARE(textNamed(view, u"profile-row-r-note"_s),
                 Words::profileLine(Api::Kind::Reader, std::nullopt,
                                    {Words::youMark(), Words::retainedHere()}));
        // Nor the tabs of what a reader does not manage.
        QVERIFY(itemNamed(view, u"settings-tab-profiles"_s));
        QVERIFY(!itemNamed(view, u"settings-tab-devices"_s));
        QVERIFY(!itemNamed(view, u"settings-tab-library"_s));
    }

    /// A right is given the moment its box is ticked, the other two sent as they stand.
    void a_rights_checkbox_sends_the_right_at_once()
    {
        const QByteArray seat = aSeating(
            {aProfile(u"p"_s, u"Principal"_s, u"emeraude"_s, u"principal"_s, true),
             aProfile(u"r"_s, u"Lecteur"_s, u"corail"_s, u"reader"_s, false, 0, true)},
            u"p"_s, true);
        const QByteArray empty =
            aReply(200, QByteArrayLiteral("application/json"), aPage({}));
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [=](const QByteArray &request) {
            return request.startsWith("GET /profiles ") ? seat : empty;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        QQuickWindow *window = shown(engine);
        QVERIFY(window);
        QQuickItem *view = settingsOn(engine, window, u"profiles"_s);
        QVERIFY(view);
        QQuickItem *box = nullptr;
        QTRY_VERIFY((box = itemNamed(view, u"rights-r-import"_s)) && box->isVisible());
        QVERIFY(laidOut(window));
        pretend.heard.clear();
        clickOn(window, box);
        QTRY_VERIFY(pretend.heard.contains("PATCH /profiles/r "));
        const QJsonObject rights =
            bodySent(pretend.heard, "PATCH /profiles/r ").value(u"rights"_s).toObject();
        QCOMPARE(rights.value(u"import"_s).toBool(), true);
        QCOMPARE(rights.value(u"edit"_s).toBool(), true);
        QCOMPARE(rights.value(u"delete"_s).toBool(), false);
    }

    /// Removing a device cuts its key with the answer, so it is asked first.
    void removing_a_device_asks_then_sends_delete()
    {
        const QByteArray seat = aSeating(
            {aProfile(u"p"_s, u"Principal"_s, u"emeraude"_s, u"principal"_s, true)}, u"p"_s,
            false);
        const QByteArray devices = aReply(
            200, QByteArrayLiteral("application/json"),
            QJsonDocument(QJsonObject{
                              {u"devices"_s,
                               QJsonArray{QJsonObject{{u"id"_s, u"d1"_s},
                                                      {u"name"_s, u"Ordinateur"_s},
                                                      {u"profile"_s, u"p"_s},
                                                      {u"kept"_s, false},
                                                      {u"here"_s, true}},
                                          QJsonObject{{u"id"_s, u"d2"_s},
                                                      {u"name"_s, u"Tablette"_s},
                                                      {u"profile"_s, u"p"_s},
                                                      {u"kept"_s, true},
                                                      {u"here"_s, false}}}}})
                .toJson(QJsonDocument::Compact));
        const QByteArray empty =
            aReply(200, QByteArrayLiteral("application/json"), aPage({}));
        const QByteArray gone = aReply(204, QByteArrayLiteral("application/json"), {});
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [=](const QByteArray &request) {
            if (request.startsWith("GET /profiles "))
                return seat;
            if (request.startsWith("GET /devices "))
                return devices;
            return request.startsWith("DELETE ") ? gone : empty;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        QQuickWindow *window = shown(engine);
        QVERIFY(window);
        QQuickItem *view = settingsOn(engine, window, u"devices"_s);
        QVERIFY(view);
        QQuickItem *bin = nullptr;
        QTRY_VERIFY((bin = itemNamed(view, u"device-row-d2-remove"_s)) && bin->isVisible());
        QCOMPARE(textNamed(view, u"device-row-d2-opens"_s), Words::opensOn(u"Principal"_s));
        QVERIFY(!itemNamed(view, u"device-row-d1"_s));
        QVERIFY(laidOut(window));
        pretend.heard.clear();
        clickOn(window, bin);
        auto *dialog = window->findChild<QQuickItem *>(u"device-removal-body"_s);
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->isVisible());
        QVERIFY(laidOut(window));
        QCOMPARE(textNamed(dialog, u"device-removal-title"_s),
                 Words::removeDeviceQuestion(u"Tablette"_s));
        QTest::qWait(100);
        QVERIFY(!pretend.heard.contains("DELETE "));

        clickOn(window, itemNamed(dialog, u"device-removal-confirm"_s));
        QTRY_VERIFY(!dialog->isVisible());
        QTRY_VERIFY(pretend.heard.contains("DELETE /devices/d2 "));
    }

    /// A name is typed on another device, by anyone with a profile: « <b>x</b> » is shown as
    /// typed. Qt's default reads anything that looks like markup as markup, so the brackets
    /// vanished and the name came out bold — and an `<img>` would have been fetched.
    void a_name_that_looks_like_markup_is_shown_as_typed()
    {
        const QString marked = u"<b>x</b>"_s;
        const QByteArray seat = aSeating(
            {aProfile(u"p"_s, u"Principal"_s, u"emeraude"_s, u"principal"_s, true),
             aProfile(u"r"_s, marked, u"corail"_s, u"reader"_s)},
            u"p"_s, true);
        const QByteArray devices = aReply(
            200, QByteArrayLiteral("application/json"),
            QJsonDocument(QJsonObject{
                              {u"devices"_s,
                               QJsonArray{QJsonObject{{u"id"_s, u"d2"_s},
                                                      {u"name"_s, u"<i>y</i>"_s},
                                                      {u"profile"_s, u"r"_s},
                                                      {u"kept"_s, true},
                                                      {u"here"_s, false}}}}})
                .toJson(QJsonDocument::Compact));
        const QByteArray empty =
            aReply(200, QByteArrayLiteral("application/json"), aPage({}));
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [=](const QByteArray &request) {
            if (request.startsWith("GET /profiles "))
                return seat;
            return request.startsWith("GET /devices ") ? devices : empty;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        QQuickWindow *window = shown(engine);
        QVERIFY(window);
        QQuickItem *view = settingsOn(engine, window, u"profiles"_s);
        QVERIFY(view);
        // Asked of the item rather than of its pixels: the default format decides per string,
        // so only the format set on the item says every name will be read as typed.
        const auto plain = [](QQuickItem *text) {
            return text && text->property("textFormat").toInt() == 0 /* Text.PlainText */;
        };
        QQuickItem *name = nullptr;
        QTRY_VERIFY((name = itemNamed(view, u"profile-row-r-name"_s)));
        QVERIFY(plain(name));
        QCOMPARE(name->property("text").toString(), marked);
        QVERIFY(plain(itemNamed(view, u"profile-row-r-note"_s)));

        view->setProperty("section", u"devices"_s);
        QQuickItem *device = nullptr;
        QTRY_VERIFY((device = itemNamed(view, u"device-row-d2-name"_s)));
        QVERIFY(plain(device));
        QCOMPARE(device->property("text").toString(), u"<i>y</i>"_s);
        QVERIFY(laidOut(window));
        clickOn(window, itemNamed(view, u"device-row-d2-remove"_s));
        auto *dialog = window->findChild<QQuickItem *>(u"device-removal-body"_s);
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->isVisible());
        QVERIFY(plain(itemNamed(dialog, u"device-removal-title"_s)));

        // « Qui lit ? » is the screen every profile sees, a child's included: the name a
        // reader gave itself must not draw there, nor in any window that says it.
        auto *whoReads = window->findChild<QQuickItem *>(u"who-reads"_s);
        QVERIFY(whoReads);
        QQuickItem *tile = itemNamed(whoReads, u"who-reads-name-r"_s);
        QVERIFY(plain(tile));
        QCOMPARE(tile->property("text").toString(), marked);
        const std::pair<QString, QString> sayings[] = {
            {u"code-dialog-body"_s, u"code-title"_s},
            {u"profile-removal-body"_s, u"profile-removal-title"_s},
            {u"profile-removal-body"_s, u"profile-removal-confirm-text"_s},
            {u"profile-dialog-body"_s, u"profile-title"_s},
            {u"profile-dialog-body"_s, u"profile-manages"_s},
            {u"profile-dialog-body"_s, u"profile-code-hint"_s},
            {u"profile-dialog-body"_s, u"profile-confirm-text"_s},
        };
        for (const auto &[body, name] : sayings) {
            auto *holder = window->findChild<QQuickItem *>(body);
            QVERIFY2(holder, qPrintable(body));
            QVERIFY2(plain(itemNamed(holder, name)), qPrintable(name));
        }
    }

    /// The principal changing a reader sends its kind and what goes with it: the rights for a
    /// reader, the age for a restricted profile — never both.
    void editing_a_profile_sends_its_kind_and_its_rights_or_its_age()
    {
        const QByteArray seat = aSeating(
            {aProfile(u"p"_s, u"Principal"_s, u"emeraude"_s, u"principal"_s, true),
             aProfile(u"r"_s, u"Lecteur"_s, u"corail"_s, u"reader"_s)},
            u"p"_s, true, u"restricted"_s);
        const QByteArray empty =
            aReply(200, QByteArrayLiteral("application/json"), aPage({}));
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [=](const QByteArray &request) {
            return request.startsWith("GET /profiles ") ? seat : empty;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        QQuickWindow *window = shown(engine);
        QVERIFY(window);
        const QVariantMap reader = aListedProfile(u"r"_s, u"Lecteur"_s, u"corail"_s, u"reader"_s);
        QQuickItem *dialog = profileDialogFor(window, u"edit"_s, reader);
        QVERIFY(dialog);
        QCOMPARE(textNamed(dialog, u"profile-title"_s), Words::editProfile(u"Lecteur"_s));
        QCOMPARE(textNamed(dialog, u"profile-confirm-text"_s), Words::saveProfile());
        // Its own colour stays its own to keep.
        QVERIFY(itemNamed(dialog, u"profile-colour-corail"_s)->isEnabled());
        clickOn(window, itemNamed(dialog, u"profile-may-import"_s));
        pretend.heard.clear();
        clickOn(window, itemNamed(dialog, u"profile-confirm"_s));
        QTRY_VERIFY(!dialog->isVisible());
        QJsonObject sent = bodySent(pretend.heard, "PATCH /profiles/r ");
        QCOMPARE(sent.value(u"kind"_s).toString(), u"reader"_s);
        QCOMPARE(sent.value(u"rights"_s).toObject(),
                 (QJsonObject{{u"import"_s, true}, {u"edit"_s, false}, {u"delete"_s, false}}));
        QVERIFY(!sent.contains(u"maxAge"_s));

        dialog = profileDialogFor(window, u"edit"_s, reader);
        QVERIFY(dialog);
        clickOn(window, itemNamed(dialog, u"profile-kind-lock"_s));
        QTRY_VERIFY(shownNamed(dialog, u"profile-age-less"_s));
        QVERIFY(laidOut(window));
        // A code exists: restricting asks for none.
        QVERIFY(hiddenNamed(dialog, u"profile-code"_s));
        clickOn(window, itemNamed(dialog, u"profile-age-less"_s));
        pretend.heard.clear();
        clickOn(window, itemNamed(dialog, u"profile-confirm"_s));
        QTRY_VERIFY(!dialog->isVisible());
        sent = bodySent(pretend.heard, "PATCH /profiles/r ");
        QCOMPARE(sent.value(u"kind"_s).toString(), u"restricted"_s);
        QCOMPARE(sent.value(u"maxAge"_s).toInt(), 9);
        QVERIFY(!sent.contains(u"rights"_s));
    }

    /// The code is required from the first restricted profile, made or turned: restricting a
    /// reader when there is no code makes it in the same window, before the change.
    void restricting_a_reader_with_no_code_makes_the_code_first()
    {
        const QByteArray seat = aSeating(
            {aProfile(u"p"_s, u"Principal"_s, u"emeraude"_s, u"principal"_s, true),
             aProfile(u"r"_s, u"Lecteur"_s, u"corail"_s, u"reader"_s)},
            u"p"_s, true);
        const QByteArray empty =
            aReply(200, QByteArrayLiteral("application/json"), aPage({}));
        const QByteArray set = aReply(204, QByteArrayLiteral("application/json"), {});
        const QByteArray recovery = aReply(
            200, QByteArrayLiteral("application/json"),
            QByteArrayLiteral(R"({"recoveryCode":"K7QM-2XWD-HP9T-C4VN"})"));
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [=](const QByteArray &request) {
            if (request.startsWith("GET /profiles "))
                return seat;
            if (request.startsWith("POST /profiles/code/recovery "))
                return recovery;
            return request.startsWith("PUT /profiles/code ") ? set : empty;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        QQuickWindow *window = shown(engine);
        QVERIFY(window);
        QQuickItem *dialog = profileDialogFor(
            window, u"edit"_s, aListedProfile(u"r"_s, u"Lecteur"_s, u"corail"_s, u"reader"_s));
        QVERIFY(dialog);
        QVERIFY(hiddenNamed(dialog, u"profile-code"_s));
        clickOn(window, itemNamed(dialog, u"profile-kind-lock"_s));
        QTRY_VERIFY(shownNamed(dialog, u"profile-code"_s));
        QVERIFY(laidOut(window));
        QQuickItem *confirm = itemNamed(dialog, u"profile-confirm"_s);
        clickOn(window, itemNamed(dialog, u"profile-code"_s));
        typeInto(window, u"1234"_s);
        // Half a code is not a code.
        QVERIFY(!confirm->property("ready").toBool());
        clickOn(window, itemNamed(dialog, u"profile-code-again"_s));
        typeInto(window, u"1234"_s);
        QVERIFY(confirm->property("ready").toBool());
        pretend.heard.clear();
        clickOn(window, confirm);
        QTRY_VERIFY(!dialog->isVisible());
        const qsizetype code = pretend.heard.indexOf("PUT /profiles/code ");
        const qsizetype change = pretend.heard.indexOf("PATCH /profiles/r ");
        QVERIFY(code >= 0);
        QVERIFY(change > code);
        const QJsonObject made = bodySent(pretend.heard, "PUT /profiles/code ");
        QCOMPARE(made, (QJsonObject{{u"code"_s, u"1234"_s}, {u"codeAgain"_s, u"1234"_s},
                                    {u"scope"_s, u"restricted"_s}}));
        QCOMPARE(bodySent(pretend.heard, "PATCH /profiles/r ").value(u"kind"_s).toString(),
                 u"restricted"_s);
    }

    /// The principal's own window sends the change of name first, then the code with what it
    /// is for — one after the other, so that a refusal of the first is not lost.
    void saving_your_own_profile_sends_the_change_then_the_code()
    {
        const QByteArray seat = aSeating(
            {aProfile(u"p"_s, u"Principal"_s, u"emeraude"_s, u"principal"_s, true)}, u"p"_s,
            true, u"restricted"_s);
        const QByteArray empty =
            aReply(200, QByteArrayLiteral("application/json"), aPage({}));
        const QByteArray set = aReply(204, QByteArrayLiteral("application/json"), {});
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [=](const QByteArray &request) {
            if (request.startsWith("GET /profiles "))
                return seat;
            return request.startsWith("PUT /profiles/code ") ? set : empty;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        QQuickWindow *window = shown(engine);
        QVERIFY(window);
        QQuickItem *dialog = profileDialogFor(
            window, u"self"_s,
            aListedProfile(u"p"_s, u"Principal"_s, u"emeraude"_s, u"principal"_s, true));
        QVERIFY(dialog);
        typeInto(window, u"e"_s);
        QQuickItem *confirm = itemNamed(dialog, u"profile-confirm"_s);
        QVERIFY(confirm->property("ready").toBool());
        // A code typed once, even where none is needed, is half a code.
        clickOn(window, itemNamed(dialog, u"profile-code"_s));
        typeInto(window, u"abcd"_s);
        QVERIFY(!confirm->property("ready").toBool());
        // Changing what the code is for means typing it again.
        clickOn(window, itemNamed(dialog, u"profile-code-scope-crown"_s));
        QTRY_VERIFY(!confirm->property("ready").toBool());
        clickOn(window, itemNamed(dialog, u"profile-code-again"_s));
        typeInto(window, u"abcd"_s);
        // Replacing a code that stands means proving one holds it.
        QVERIFY(!confirm->property("ready").toBool());
        QVERIFY(laidOut(window));
        clickOn(window, itemNamed(dialog, u"profile-code-current"_s));
        typeInto(window, u"vieux"_s);
        QTRY_VERIFY(confirm->property("ready").toBool());
        pretend.heard.clear();
        clickOn(window, confirm);
        QTRY_VERIFY(!dialog->isVisible());
        QVERIFY(pretend.heard.contains("\"currentCode\":\"vieux\""));
        const qsizetype change = pretend.heard.indexOf("PATCH /profiles/p ");
        const qsizetype code = pretend.heard.indexOf("PUT /profiles/code ");
        QVERIFY(change >= 0);
        QVERIFY(code > change);
        QCOMPARE(bodySent(pretend.heard, "PATCH /profiles/p ").value(u"name"_s).toString(),
                 u"Principale"_s);
        QCOMPARE(bodySent(pretend.heard, "PUT /profiles/code ").value(u"scope"_s).toString(),
                 u"everything"_s);
    }

    /// With no restricted profile, the code can go: the switch off sends the removal, and
    /// nothing else when nothing else changed.
    void turning_the_code_off_removes_it()
    {
        const QByteArray seat = aSeating(
            {aProfile(u"p"_s, u"Principal"_s, u"emeraude"_s, u"principal"_s, true)}, u"p"_s,
            true, u"restricted"_s);
        const QByteArray empty =
            aReply(200, QByteArrayLiteral("application/json"), aPage({}));
        const QByteArray gone = aReply(204, QByteArrayLiteral("application/json"), {});
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [=](const QByteArray &request) {
            if (request.startsWith("GET /profiles "))
                return seat;
            return request.startsWith("DELETE ") ? gone : empty;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        QQuickWindow *window = shown(engine);
        QVERIFY(window);
        QQuickItem *dialog = profileDialogFor(
            window, u"self"_s,
            aListedProfile(u"p"_s, u"Principal"_s, u"emeraude"_s, u"principal"_s, true));
        QVERIFY(dialog);
        QQuickItem *lever = itemNamed(dialog, u"profile-code-switch"_s);
        QVERIFY(lever->isEnabled());
        QCOMPARE(textNamed(dialog, u"profile-code-hint"_s), Words::codeHintFree());
        clickOn(window, lever);
        QTRY_VERIFY(!lever->property("on").toBool());
        QVERIFY(laidOut(window));
        // Removing a code asks for the code, as changing it does.
        QQuickItem *confirm = itemNamed(dialog, u"profile-confirm"_s);
        QVERIFY(!confirm->property("ready").toBool());
        clickOn(window, itemNamed(dialog, u"profile-code-current"_s));
        typeInto(window, u"vieux"_s);
        QVERIFY(confirm->property("ready").toBool());
        pretend.heard.clear();
        clickOn(window, confirm);
        QTRY_VERIFY(!dialog->isVisible());
        QVERIFY(pretend.heard.contains("DELETE /profiles/code "));
        QVERIFY(pretend.heard.contains("\"currentCode\":\"vieux\""));
        QVERIFY(!pretend.heard.contains("PATCH "));
    }

    /// A refused deletion says why where it was asked, and the profile stays.
    void a_refused_deletion_is_said_in_its_window()
    {
        const QByteArray seat = aSeating(
            {aProfile(u"p"_s, u"Principal"_s, u"emeraude"_s, u"principal"_s, true),
             aProfile(u"r"_s, u"Lecteur"_s, u"corail"_s, u"reader"_s)},
            u"p"_s, true);
        const QByteArray empty =
            aReply(200, QByteArrayLiteral("application/json"), aPage({}));
        const QByteArray refused = aReply(409, QByteArrayLiteral("application/json"),
                                          QByteArrayLiteral(R"({"error":"refused"})"));
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [=](const QByteArray &request) {
            if (request.startsWith("GET /profiles "))
                return seat;
            return request.startsWith("DELETE ") ? refused : empty;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        QQuickWindow *window = shown(engine);
        QVERIFY(window);
        QVERIFY(QMetaObject::invokeMethod(
            window, "removeProfile",
            Q_ARG(QVariant, aListedProfile(u"r"_s, u"Lecteur"_s, u"corail"_s, u"reader"_s))));
        auto *dialog = window->findChild<QQuickItem *>(u"profile-removal-body"_s);
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->isVisible());
        QVERIFY(laidOut(window));
        QVERIFY(hiddenNamed(dialog, u"profile-removal-trouble"_s));
        clickOn(window, itemNamed(dialog, u"profile-removal-confirm"_s));
        QTRY_VERIFY(shownNamed(dialog, u"profile-removal-trouble"_s));
        QVERIFY(!textNamed(dialog, u"profile-removal-trouble"_s).isEmpty());
        QVERIFY(dialog->isVisible());
    }

    /// The swatches answer the keyboard: Tab reaches the chosen one, the arrows go from one
    /// free colour to the next over the taken ones, Space or Enter picks. Each is named by its
    /// palette name, and a taken one by its owner too.
    void a_colour_is_chosen_from_the_keyboard()
    {
        const QByteArray seat = aSeating(
            {aProfile(u"p"_s, u"Principal"_s, u"emeraude"_s, u"principal"_s, true),
             aProfile(u"r"_s, u"Lecteur"_s, u"sauge"_s, u"reader"_s)},
            u"p"_s, true);
        const QByteArray empty =
            aReply(200, QByteArrayLiteral("application/json"), aPage({}));
        Pretend pretend;
        QVERIFY(pretend.listen(QHostAddress::LocalHost));
        pretend.answerFor = [=](const QByteArray &request) {
            return request.startsWith("GET /profiles ") ? seat : empty;
        };
        qputenv("LEAF_ADDRESS",
                u"http://127.0.0.1:%1"_s.arg(pretend.serverPort()).toUtf8());

        QQmlApplicationEngine engine;
        QQuickWindow *window = shown(engine);
        QVERIFY(window);
        QQuickItem *dialog = profileDialogFromAdd(window);
        QVERIFY(dialog);
        auto *popup = window->findChild<QObject *>(u"profile-dialog"_s);
        QVERIFY(popup);
        QCOMPARE(popup->property("color").toString(), u"sapin"_s);
        QQuickItem *sapin = itemNamed(dialog, u"profile-colour-sapin"_s);
        QQuickItem *olive = itemNamed(dialog, u"profile-colour-olive"_s);
        QQuickItem *bleu = itemNamed(dialog, u"profile-colour-bleu"_s);
        QVERIFY(sapin && olive && bleu);

        QAccessibleInterface *named =
            QAccessible::queryAccessibleInterface(itemNamed(dialog, u"profile-colour-emeraude"_s));
        QVERIFY(named);
        QCOMPARE(named->text(QAccessible::Name), Words::colourName(u"emeraude"_s, u"Principal"_s));
        QCOMPARE(QAccessible::queryAccessibleInterface(sapin)->text(QAccessible::Name),
                 u"Sapin"_s);

        QTest::keyClick(window, Qt::Key_Tab);
        QTRY_VERIFY(sapin->hasActiveFocus());
        QVERIFY(shownNamed(sapin, u"profile-colour-sapin-focus"_s));
        // Émeraude is taken and first: nowhere to go on the left.
        QTest::keyClick(window, Qt::Key_Left);
        QVERIFY(sapin->hasActiveFocus());
        // Sauge is taken: the right arrow goes over it.
        QTest::keyClick(window, Qt::Key_Right);
        QVERIFY(olive->hasActiveFocus());
        QCOMPARE(popup->property("color").toString(), u"sapin"_s);
        QTest::keyClick(window, Qt::Key_Space);
        QCOMPARE(popup->property("color").toString(), u"olive"_s);
        QTest::keyClick(window, Qt::Key_Down);
        QVERIFY(itemNamed(dialog, u"profile-colour-indigo"_s)->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Right);
        QVERIFY(bleu->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_Return);
        QCOMPARE(popup->property("color").toString(), u"bleu"_s);
        QVERIFY(dialog->isVisible());
    }
};

namespace {
/// Puts this process on a display of its own before any Qt object exists, or refuses to run.
///
/// Twice on 22/10/2026 this test drew windows on the desktop of the person who ran it. First a
/// bare `ctest` connected to the desktop's own X display and covered their work with empty
/// blue « Leaf » windows; then, with registration fixed, a direct run of the binary did it
/// again, and so did `xvfb-run -a build/crosses-the-seam` without `QT_QPA_PLATFORM=xcb`:
/// `WAYLAND_DISPLAY` is set in a GNOME session, so Qt chose the Wayland platform and ignored
/// Xvfb altogether. The test also moves the cursor and takes window activation, which are the
/// display server's and not the test's. A registration in CMake protects `ctest` and nothing
/// else, so the protection lives here, in the one place every way of starting the binary goes
/// through.
///
/// Without the marker the binary re-executes itself under `xvfb-run -a` with the platform
/// forced to xcb and the Wayland socket removed; with it, it is that re-executed process and
/// runs the tests. A missing `xvfb-run` is a failure naming the package, never a fall-back to
/// the real display. `execv` replaces the process, so the exit code is the test's own.
constexpr const char *ownDisplay = "LEAF_SEAM_OWN_DISPLAY";

void useXcbOnly()
{
    qputenv("QT_QPA_PLATFORM", "xcb");
    qunsetenv("WAYLAND_DISPLAY");
}

/// Returns only when it could not re-execute; the caller then exits non-zero.
int reexecuteOnItsOwnDisplay(int argc, char *argv[])
{
    const QString xvfb = QStandardPaths::findExecutable(u"xvfb-run"_s);
    if (xvfb.isEmpty()) {
        fprintf(stderr, "crosses-the-seam needs xvfb-run (package xvfb) to run on a display of "
                        "its own, and will not use the real one\n");
        return 1;
    }
    qputenv(ownDisplay, "1");
    useXcbOnly();
    const QByteArray tool = QFile::encodeName(xvfb);
    std::vector<char *> args;
    args.push_back(const_cast<char *>(tool.constData()));
    args.push_back(const_cast<char *>("-a"));
    for (int i = 0; i < argc; ++i)
        args.push_back(argv[i]);
    args.push_back(nullptr);
    execv(tool.constData(), args.data());
    fprintf(stderr, "crosses-the-seam could not start xvfb-run: %s\n", strerror(errno));
    return 1;
}
} // namespace

int main(int argc, char *argv[])
{
    if (!qEnvironmentVariableIsSet(ownDisplay))
        return reexecuteOnItsOwnDisplay(argc, argv);
    // Defensive: whoever sets the marker by hand still never reaches the real display.
    useXcbOnly();

    // What QTEST_MAIN expands to for a Gui-only target, written out so that nothing runs
    // before the check above.
    QGuiApplication app(argc, argv);
    app.setAttribute(Qt::AA_Use96Dpi, true);
    CrossesTheSeam tc;
    QTest::setMainSourcePath(__FILE__);
    return QTest::qExec(&tc, argc, argv);
}
#include "crosses_the_seam.moc"
