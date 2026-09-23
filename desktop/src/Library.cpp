#include "Library.h"

#include "Elsewhere.h"
#include "Search.h"
#include "Series.h"
#include "Shelf.h"

#include <QDebug>

using Qt::Literals::StringLiterals::operator""_s;

Library *Library::create(QQmlEngine *engine, QJSEngine *)
{
    auto *server = engine->singletonInstance<Server *>(qmlTypeId("Leaf", 1, 0, "Server"));
    if (!server) {
        qWarning().noquote()
            << QStringLiteral("error resolving the Server singleton — a series changed will "
                              "not be shown changed");
    }
    auto *library = new Library(server);
    // Everything that draws a series, told what became of it.
    if (auto *shelf = engine->singletonInstance<Shelf *>(qmlTypeId("Leaf", 1, 0, "Shelf"))) {
        connect(library, &Library::fresh, shelf, &Shelf::take);
        connect(library, &Library::gone, shelf, &Shelf::forget);
    }
    if (auto *page = engine->singletonInstance<Series *>(qmlTypeId("Leaf", 1, 0, "Series")))
        connect(library, &Library::fresh, page, &Series::take);
    if (auto *universe =
            engine->singletonInstance<Elsewhere *>(qmlTypeId("Leaf", 1, 0, "Elsewhere")))
        connect(library, &Library::fresh, universe, &Elsewhere::take);
    if (auto *found = engine->singletonInstance<Search *>(qmlTypeId("Leaf", 1, 0, "Search"))) {
        connect(library, &Library::gone, found,
                [found](const QString &seriesId) { found->forget(seriesId, QString()); });
        connect(library, &Library::fileGone, found, &Search::forget);
    }
    return library;
}

Library::Library(Server *server, QObject *parent)
    : QObject(parent)
    , m_server(server)
{
}

void Library::touched(const QString &seriesId)
{
    if (!m_server || seriesId.isEmpty())
        return;
    const int mine = ++m_asked[seriesId];
    m_server->get(u"/series/"_s + seriesId, this,
                  [this, seriesId, mine](const Server::Answer &answer) {
                      if (m_asked.value(seriesId) != mine)
                          return;
                      if (answer.status == 404) {
                          emit gone(seriesId);
                          return;
                      }
                      // A refusal changes nothing drawn: what is on screen stays as it was,
                      // which is the last thing the server said.
                      if (!answer.went() || !answer.body.isObject())
                          return;
                      const Api::Read<Api::Series> read = Api::series(answer.body.object());
                      if (read.ok())
                          emit fresh(*read.value);
                  });
}

void Library::erased(const QString &seriesId, const QString &entryId)
{
    if (entryId.isEmpty()) {
        emit gone(seriesId);
        return;
    }
    emit fileGone(seriesId, entryId);
    touched(seriesId);
}
