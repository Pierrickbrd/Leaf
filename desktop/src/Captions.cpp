#include "Captions.h"

#include "Api.h"
#include "Search.h"
#include "Shelf.h"
#include "Words.h"

#include <QDebug>
#include <QQmlEngine>
#include <QVariantMap>

#include <algorithm>
#include <optional>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

/// A medium's word from the contract's spelling, by walking the enumeration rather than
/// keeping a second table that could disagree with `Api::spell`.
QString mediumLabel(const QString &word)
{
    for (auto raw = std::to_underlying(Api::Medium::Manga);
         raw <= std::to_underlying(Api::Medium::Other); ++raw) {
        const auto medium = static_cast<Api::Medium>(raw);
        if (Api::spell(medium) == word)
            return Words::medium(medium);
    }
    return word;
}

} // namespace

Captions *Captions::create(QQmlEngine *engine, QJSEngine *)
{
    auto *search = engine->singletonInstance<Search *>(qmlTypeId("Leaf", 1, 0, "Search"));
    auto *shelf = engine->singletonInstance<Shelf *>(qmlTypeId("Leaf", 1, 0, "Shelf"));
    if (!search || !shelf) {
        qWarning().noquote()
            << QStringLiteral("error resolving the Search or Shelf singleton — the bar will "
                              "have no words");
    }
    return new Captions(search, shelf);
}

Captions::Captions(Search *search, Shelf *shelf, QObject *parent)
    : QObject(parent)
    , m_search(search)
    , m_shelf(shelf)
{
    // Both, because a caption quotes both: « 6 séries » is the shelf's count and
    // « 12 fichiers » is the search's, and they change on different answers.
    if (m_search)
        connect(m_search, &Search::changed, this, &Captions::changed);
    if (m_shelf)
        connect(m_shelf, &Shelf::changed, this, &Captions::changed);
}

QString Captions::heading() const
{
    return filesHeading();
}

QString Captions::moreLabel() const
{
    return Words::seeTheOthers(m_search ? m_search->remaining() : 0);
}

QString Captions::overviewLabel() const
{
    return Words::overview();
}

QString Captions::seriesHeading() const
{
    return Words::series(m_shelf ? m_shelf->total() : 0);
}

QString Captions::filesHeading() const
{
    return Words::files(m_search ? m_search->fileTotal() : 0);
}

QString Captions::allSeriesLabel() const
{
    return Words::seeAllSeries(m_shelf ? m_shelf->total() : 0);
}

QString Captions::allFilesLabel() const
{
    return Words::seeAllFiles(m_search ? m_search->fileTotal() : 0);
}

QString Captions::outsideFilters() const
{
    const Search::Aside aside = m_search ? m_search->aside() : Search::Aside{};
    return aside.outside > 0 ? Words::nothingHere(activeLabels(), aside.outside) : QString();
}

QString Captions::suggestion() const
{
    const Search::Aside aside = m_search ? m_search->aside() : Search::Aside{};
    if (aside.approximate.isEmpty())
        return {};
    return Words::didYouMean(aside.approximate);
}

bool Captions::filtered() const
{
    if (!m_shelf)
        return false;
    const QVariantMap narrowing = m_shelf->narrowing();
    return std::ranges::any_of(narrowing, [](const QVariant &values) {
        return !values.toStringList().isEmpty();
    });
}

QString Captions::emptyHeadline() const
{
    return filtered() ? Words::nothingLeftHeadline() : Words::emptyShelfHeadline();
}

QString Captions::emptySaid() const
{
    return filtered() ? Words::nothingLeft() : Words::emptyShelf();
}

QString Captions::troubleHeadline() const
{
    return Words::shelfTroubleHeadline();
}

QString Captions::noSeriesLabel() const
{
    return Words::noSeriesByThatName();
}

QString Captions::nothingFoundLabel() const
{
    return Words::nothingFound();
}

QStringList Captions::narrowedBy(const QString &axis) const
{
    return m_search ? m_search->narrowing().value(axis).toStringList() : QStringList();
}

QStringList Captions::activeLabels() const
{
    QStringList labels;
    for (const QString &word : narrowedBy(u"read"_s)) {
        const std::optional<Api::ReadStatus> status = Api::readStatus(word);
        labels << (status ? Words::readStatus(*status) : word);
    }
    for (const QString &word : narrowedBy(u"medium"_s))
        labels << mediumLabel(word);
    // The axes the row never draws say themselves: a name is already a word.
    for (const QString &axis : {u"universe"_s, u"genre"_s, u"author"_s, u"publisher"_s})
        labels << narrowedBy(axis);
    return labels;
}
