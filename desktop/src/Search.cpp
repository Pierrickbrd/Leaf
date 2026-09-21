#include "Search.h"

#include "Rearrange.h"

#include "Shelf.h"
#include "Words.h"

#include <QDebug>
#include <QJsonArray>
#include <QUrlQuery>
#include <QVariantMap>
#include <QtGlobal>

#include <utility>

using namespace Qt::StringLiterals;

namespace {

constexpr int LegacySearchLimit = 200;
constexpr int PageSize = 50;
constexpr int PreviewRows = 4;

QStringList kept(const QStringList &asked)
{
    QStringList worth;
    for (const QString &one : asked) {
        if (!one.isEmpty())
            worth << one;
    }
    return worth;
}

QString mediumLabel(const QString &word)
{
    for (int raw = int(Api::Medium::Manga); raw <= int(Api::Medium::Other); ++raw) {
        const auto medium = static_cast<Api::Medium>(raw);
        if (Api::spell(medium) == word)
            return Words::medium(medium);
    }
    return word;
}

} // namespace

Search *Search::create(QQmlEngine *engine, QJSEngine *)
{
    auto *server = engine->singletonInstance<Server *>(qmlTypeId("Leaf", 1, 0, "Server"));
    auto *shelf = engine->singletonInstance<Shelf *>(qmlTypeId("Leaf", 1, 0, "Shelf"));
    if (!server || !shelf) {
        qWarning().noquote()
            << QStringLiteral("error resolving the search dependencies — file matches will "
                              "stay empty");
    }
    return new Search(server, shelf, nullptr);
}

Search::Search(Server *server, Shelf *shelf, QObject *parent)
    : QAbstractListModel(parent)
    , m_server(server)
    , m_shelf(shelf)
{
    if (!m_shelf)
        return;

    connect(m_shelf, &Shelf::criteriaChanged, this, &Search::followShelf);
    // The sort label in the bar is a view of Shelf state. It does not issue another search,
    // but its binding still needs to be told when that state changes.
    connect(m_shelf, &Shelf::changed, this, &Search::changed);
    followShelf();
}

int Search::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_files.size());
}

QVariant Search::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_files.size())
        return {};

    const Api::Hit &hit = m_files.at(index.row());
    using enum Role;
    switch (static_cast<Role>(role)) {
    case Kind:
        return Api::spell(hit.kind);
    case ResultId:
        return hit.id;
    case Label:
        return hit.label;
    case SeriesId:
        return hit.seriesId.value_or(QString());
    case SeriesName:
        return hit.seriesName.value_or(QString());
    case EntryId:
        // An ENTRY is its own thing to open. A CHAPTER points at the entry that contains it.
        return hit.entryId.value_or(hit.id);
    case Cover: {
        if (!m_server)
            return QString();
        const QString entry = hit.entryId.value_or(hit.id);
        return m_server->address() + u"/entries/"_s + entry + u"/cover"_s;
    }
    case Context:
        return Words::fileContext(hit);
    default:
        return {};
    }
}

QHash<int, QByteArray> Search::roleNames() const
{
    QHash<int, QByteArray> named;
    using enum Role;
    named.insert(qToUnderlying(Kind), "kind");
    named.insert(qToUnderlying(ResultId), "resultId");
    named.insert(qToUnderlying(Label), "label");
    named.insert(qToUnderlying(SeriesId), "seriesId");
    named.insert(qToUnderlying(SeriesName), "seriesName");
    named.insert(qToUnderlying(EntryId), "entryId");
    named.insert(qToUnderlying(Cover), "cover");
    named.insert(qToUnderlying(Context), "context");
    return named;
}

bool Search::canFetchMore(const QModelIndex &parent) const
{
    return !parent.isValid() && active() && !m_loading && m_more
        && m_received < m_wireTotal && m_files.size() < m_fileTotal;
}

void Search::fetchMore(const QModelIndex &parent)
{
    if (canFetchMore(parent))
        ask(true, m_next);
}

QString Search::heading() const
{
    return filesHeading();
}

QString Search::moreLabel() const
{
    return Words::seeTheOthers(remaining());
}

QString Search::overviewLabel() const
{
    return Words::overview();
}

QString Search::seriesHeading() const
{
    return Words::series(m_shelf ? m_shelf->total() : 0);
}

QString Search::filesHeading() const
{
    return Words::files(m_fileTotal);
}

QString Search::allSeriesLabel() const
{
    return Words::seeAllSeries(m_shelf ? m_shelf->total() : 0);
}

QString Search::allFilesLabel() const
{
    return Words::seeAllFiles(m_fileTotal);
}

QString Search::outsideFilters() const
{
    return m_outside > 0 ? Words::nothingHere(activeLabels(), m_outside) : QString();
}

QString Search::suggestion() const
{
    if (m_approximate.isEmpty())
        return {};
    return Words::didYouMean(m_approximate);
}

QString Search::placeholder() const
{
    return Words::searchHint();
}

QString Search::shortPlaceholder() const
{
    return Words::searchHintShort();
}

QString Search::clearLabel() const
{
    return Words::clearTheSearch();
}

QString Search::filterLabel() const
{
    return Words::filter();
}

QString Search::settingsLabel() const
{
    return Words::destination(Navigation::Destination::Settings);
}

QString Search::noSeriesLabel() const
{
    return Words::noSeriesByThatName();
}

QString Search::clearFiltersLabel() const
{
    return Words::clearEveryFilter();
}

QString Search::nothingToFilterLabel() const
{
    return Words::nothingToFilter();
}

QString Search::noValueByThatNameLabel() const
{
    return Words::noValueByThatName();
}

QString Search::searchWithin(const QString &axisTitle) const
{
    return Words::searchWithin(axisTitle);
}

QString Search::sortValue() const
{
    const Api::Sort order = m_shelf ? Api::sort(m_shelf->sort()) : Api::Sort::Name;
    return Words::sortValue(order, m_shelf && m_shelf->sortReversed());
}

QString Search::sortLabel() const
{
    const Api::Sort order = m_shelf ? Api::sort(m_shelf->sort()) : Api::Sort::Name;
    return Words::labelled(u"Trier"_s,
                           Words::sortValue(order, m_shelf && m_shelf->sortReversed()));
}

QVariantList Search::sortOptions() const
{
    QVariantList options;
    using enum Api::Sort;
    for (const Api::Sort order : {Name, Added, Volumes, Read}) {
        options << QVariantMap{{u"value"_s, Api::spell(order)},
                               {u"label"_s, Words::sortOrder(order)}};
    }
    return options;
}

void Search::followShelf()
{
    if (m_shelf) {
        updateSearch(m_shelf->query(), m_shelf->narrowing(), m_shelf->sort(),
                     m_shelf->sortDirection());
    }
}

void Search::searchFor(const QString &query, const QStringList &readStatuses,
                       const QStringList &media)
{
    QVariantMap narrowing;
    if (!readStatuses.isEmpty())
        narrowing.insert(u"read"_s, readStatuses);
    if (!media.isEmpty())
        narrowing.insert(u"medium"_s, media);
    updateSearch(query, narrowing, m_sort, m_direction);
}

QStringList Search::narrowedBy(const QString &axis) const
{
    return m_narrowing.value(axis).toStringList();
}

void Search::updateSearch(const QString &query, const QVariantMap &narrowing,
                          const QString &sort, const QString &direction)
{
    const QString wanted = query.trimmed();
    QVariantMap asked;
    for (auto one = narrowing.constBegin(); one != narrowing.constEnd(); ++one) {
        const QStringList values = kept(one.value().toStringList());
        if (!values.isEmpty())
            asked.insert(one.key(), values);
    }
    const QString wantedSort = Api::spell(Api::sort(sort));
    const QString wantedDirection = direction == u"desc"_s ? u"desc"_s : u"asc"_s;
    if (wanted == m_query && asked == m_narrowing && wantedSort == m_sort
        && wantedDirection == m_direction) {
        return;
    }

    m_query = wanted;
    m_narrowing = asked;
    m_sort = wantedSort;
    m_direction = wantedDirection;
    ++m_generation;
    m_outside = 0;
    m_fileTotal = 0;
    m_wireTotal = 0;
    m_received = 0;
    m_next = 0;
    m_more = false;
    m_loading = false;
    m_approximate.clear();
    m_trouble.clear();

    if (m_query.isEmpty()) {
        // Nothing is being looked for, so there are no results — the emptiness is the answer
        // and not a gap on the way to one.
        replaceFiles({});
        emit changed();
        return;
    }

    // What is on screen answered the previous criteria and stays until there is an answer to
    // these. Emptying here put sixty lines out and a spinner in at every pill and every
    // order, including the lines the pill was never going to remove.
    ask(true, 0);
}

void Search::ask(bool filtered, int page)
{
    if (!m_server) {
        m_loading = false;
        m_trouble = tr("Leaf could not set itself up, so there is nothing to search.");
        emit changed();
        return;
    }

    m_loading = true;
    emit changed();

    QUrlQuery query;
    query.addQueryItem(u"q"_s, m_query);
    // A page is asked for by name, which is what opts into the envelope carrying the totals
    // a heading needs. A server that predates it ignores both and answers the bare list, and
    // the client counts what it was given rather than claim a number it does not have.
    query.addQueryItem(u"limit"_s, QString::number(LegacySearchLimit));
    query.addQueryItem(u"size"_s, QString::number(PageSize));
    query.addQueryItem(u"page"_s, QString::number(page));
    query.addQueryItem(u"sort"_s, m_sort);
    query.addQueryItem(u"direction"_s, m_direction);
    // Explicit rather than relying on today's empty-means-all default. These are the three
    // things this version knows how to interpret, and a future fourth must not silently join.
    for (const QString &kind : {u"EDITION"_s, u"ENTRY"_s, u"CHAPTER"_s})
        query.addQueryItem(u"kind"_s, kind);
    if (filtered) {
        // Every axis the panel can light, not two: a search runs inside what is showing.
        for (auto one = m_narrowing.constBegin(); one != m_narrowing.constEnd(); ++one) {
            for (const QString &value : one.value().toStringList())
                query.addQueryItem(one.key(), value);
        }
    }

    const int mine = m_generation;
    m_server->get(u"/search"_s, query, this,
                  [this, mine, filtered, page](const Server::Answer &answer) {
                      if (mine == m_generation)
                          took(filtered, page, answer);
                  });
}

Search::Parsed Search::parse(const Server::Answer &answer) const
{
    Parsed parsed;
    if (!answer.went()) {
        parsed.trouble = answer.trouble;
        return parsed;
    }
    const Api::Read<Api::Hits> page = Api::hits(answer.body);
    if (!page.ok()) {
        parsed.trouble = page.trouble;
        return parsed;
    }

    parsed.total = page.value->total;
    parsed.fileTotal = page.value->fileTotal;
    parsed.received = page.value->items.size();
    parsed.page = page.value->page;
    parsed.size = page.value->size;
    for (const Api::Hit &hit : page.value->items) {
        if (!hit.approximate)
            ++parsed.exact;
        if (hit.approximate && hit.kind == Api::Hit::Kind::Edition
            && parsed.approximate.isEmpty()) {
            parsed.approximate = hit.label;
        }
        if (!hit.approximate
            && (hit.kind == Api::Hit::Kind::Entry || hit.kind == Api::Hit::Kind::Chapter)) {
            parsed.files << hit;
        }
    }
    return parsed;
}

void Search::took(bool filtered, int page, const Server::Answer &answer)
{
    const Parsed parsed = parse(answer);
    if (!parsed.trouble.isEmpty()) {
        m_loading = false;
        m_more = false;
        m_trouble = parsed.trouble;
        emit changed();
        return;
    }

    if (filtered) {
        if (page == 0)
            replaceFiles(parsed.files);
        else
            appendFiles(parsed.files);
        m_fileTotal = parsed.fileTotal;
        m_wireTotal = parsed.total;
        m_received = page == 0 ? parsed.received : m_received + parsed.received;
        m_next = page + 1;
        m_more = parsed.received > 0 && m_received < m_wireTotal;
        m_approximate = parsed.approximate;
        m_trouble.clear();

        // An exact match behind the current chips needs no explanation. With no exact match,
        // the unfiltered answer distinguishes "nothing exists" from "your view hides it".
        if (parsed.exact == 0 && !m_narrowing.isEmpty()) {
            ask(false, 0);
            return;
        }

        // A page may contain only edition hits before the first file. Keep walking until the
        // four-line overview can tell the truth, or until the server says there is no more.
        if (m_more && m_fileTotal > m_files.size()
            && m_files.size() < qMin(PreviewRows, m_fileTotal)) {
            ask(true, m_next);
            return;
        }
    } else {
        m_outside = parsed.total;
        if (!parsed.approximate.isEmpty())
            m_approximate = parsed.approximate;
    }

    m_loading = false;
    m_trouble.clear();
    emit changed();
}

void Search::replaceFiles(QList<Api::Hit> files)
{
    // Row by row and by id, for the reason the shelf does it: a reset destroys every row and
    // builds it again, so a filter that changes four lines out of sixty made all sixty blink
    // — and a list that merely turned round looked exactly like one that had not moved.
    QStringList held;
    held.reserve(int(m_files.size()));
    for (const Api::Hit &one : m_files)
        held << one.id;
    QStringList wanted;
    wanted.reserve(files.size());
    for (const Api::Hit &one : files)
        wanted << one.id;

    for (const Rearrange::Step &step : Rearrange::plan(held, wanted)) {
        switch (step.kind) {
        case Rearrange::Step::Kind::Remove:
            beginRemoveRows({}, step.first, step.last);
            m_files.remove(step.first, step.last - step.first + 1);
            endRemoveRows();
            break;
        case Rearrange::Step::Kind::Move:
            beginMoveRows({}, step.from, step.from, {}, step.to);
            m_files.move(step.from, step.to);
            endMoveRows();
            break;
        case Rearrange::Step::Kind::Insert:
            beginInsertRows({}, step.to, step.to);
            m_files.insert(step.to, files.at(step.to));
            endInsertRows();
            break;
        }
    }

    for (int at = 0; at < int(m_files.size()) && at < files.size(); ++at) {
        m_files[at] = files.at(at);
        emit dataChanged(index(at), index(at));
    }
}

void Search::appendFiles(QList<Api::Hit> files)
{
    if (files.isEmpty())
        return;
    const int first = m_files.size();
    const int last = first + files.size() - 1;
    beginInsertRows({}, first, last);
    m_files.append(std::move(files));
    endInsertRows();
}

QStringList Search::activeLabels() const
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

void Search::expand()
{
    fetchMore({});
}

void Search::clearFilters()
{
    if (m_shelf)
        m_shelf->filterBy({});
}
