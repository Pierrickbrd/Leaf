#include "Filters.h"

#include "Shelf.h"
#include "Words.h"

#include <QDebug>
#include <QJsonObject>
#include <QUrlQuery>
#include <QVariantMap>

#include <optional>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

/// One pill, ready to draw and ready to send back: the contract's spelling, the French, and
/// the count the server arrived at.
QVariantMap pill(const QString &value, const QString &label, int count)
{
    return {
        {u"value"_s, value},
        {u"label"_s, label},
        {u"count"_s, count},
    };
}

/// How many times a value was counted, or nothing when the server did not offer it. `/filters`
/// never offers a value matching nothing, so an absent one is a value this library does not
/// have rather than a zero worth drawing.
std::optional<int> counted(const QList<Api::Facet> &facets, const QString &value)
{
    for (const Api::Facet &one : facets) {
        if (one.value == value)
            return one.count;
    }
    return std::nullopt;
}

/// The order is the client's, not the server's. « Non lues, En cours, Terminées » is written
/// down in `Words.h` as the order these read left to right, and a row that came out as the
/// server happened to group its rows would put "En cours" first on one library and second on
/// the next. Walking the enumeration also drops, for free, any value this client has no word
/// for: the server may learn one before this client does.
QVariantList readStatusPills(const QList<Api::Facet> &facets)
{
    using enum Api::ReadStatus;

    QVariantList pills;
    for (const Api::ReadStatus status : {Unread, InProgress, Read}) {
        const QString value = Api::spell(status);
        if (const auto count = counted(facets, value); count.has_value())
            pills << pill(value, Words::pill(Words::readStatus(status), *count), *count);
    }
    return pills;
}

/// « Manga, BD, Comics » and then the rest, in the contract's own order — the same reason.
QVariantList mediumPills(const QList<Api::Facet> &facets)
{
    using enum Api::Medium;

    QVariantList pills;
    for (const Api::Medium medium :
         {Manga, Bd, Comics, Manhwa, Manhua, Webtoon, Artbook, Other}) {
        const QString value = Api::spell(medium);
        if (const auto count = counted(facets, value); count.has_value())
            pills << pill(value, Words::pill(Words::medium(medium), *count), *count);
    }
    return pills;
}

/// A value shown as it stands: a name typed by whoever made the files. Nothing to translate
/// and nothing to fold — « Glénat » is « Glénat ».
QVariantList verbatimPills(const QList<Api::Facet> &facets)
{
    QVariantList pills;
    for (const Api::Facet &one : facets) {
        if (one.value.trimmed().isEmpty())
            continue;
        pills << pill(one.value, Words::pill(one.value, one.count), one.count);
    }
    return pills;
}

/// Takes the wording as a template parameter rather than a function pointer: the two
/// callers each hand it a different `Words` function, and a pointer to one of them is a
/// call the compiler cannot see through.
template <typename Say>
QVariantList worded(const QList<Api::Facet> &facets, Say say)
{
    QVariantList pills;
    for (const Api::Facet &one : facets) {
        if (one.value.trimmed().isEmpty())
            continue;
        pills << pill(one.value, Words::pill(say(one.value), one.count), one.count);
    }
    return pills;
}

/// Every axis, in the order the panel draws them: what a reader reaches for first, then the
/// names. An axis holding a single value covers the whole library and narrows nothing, so it
/// is left out — the same rule the row follows, for the same reason.
QVariantList axesOf(const Api::Facets &facets)
{
    const QList<QPair<QString, QVariantList>> all{
        {u"read"_s, readStatusPills(facets.readStatuses)},
        {u"medium"_s, mediumPills(facets.media)},
        {u"universe"_s, verbatimPills(facets.universes)},
        {u"genre"_s, verbatimPills(facets.genres)},
        {u"author"_s, verbatimPills(facets.authors)},
        {u"publisher"_s, verbatimPills(facets.publishers)},
        {u"language"_s, worded(facets.languages, Words::language)},
        {u"status"_s, worded(facets.statuses, Words::editionStatus)},
    };

    QVariantList kept;
    for (const auto &[axis, pills] : all) {
        if (pills.size() < Filters::Fewest)
            continue;
        kept << QVariantMap{{u"axis"_s, axis},
                            {u"title"_s, Words::axis(axis)},
                            {u"values"_s, pills}};
    }
    return kept;
}

} // namespace

Filters *Filters::create(QQmlEngine *engine, QJSEngine *)
{
    auto *server = engine->singletonInstance<Server *>(qmlTypeId("Leaf", 1, 0, "Server"));
    auto *shelf = engine->singletonInstance<Shelf *>(qmlTypeId("Leaf", 1, 0, "Shelf"));
    if (!server) {
        qWarning().noquote()
            << QStringLiteral("error resolving the Server singleton — no filter will be "
                              "offered");
    }
    return new Filters(server, shelf);
}

Filters::Filters(Server *server, Shelf *shelf, QObject *parent)
    : QObject(parent)
    , m_server(server)
    , m_shelf(shelf)
{
    // The file counts follow what is being searched for. Connected here rather than asked
    // for by the screen drawing the row: a count that silently describes the previous query
    // is worse than no count, and a screen can forget to ask.
    if (m_shelf) {
        connect(m_shelf, &Shelf::criteriaChanged, this, [this] { countFiles(); });
    }
}

void Filters::offer(QVariantList &held, QVariantList fresh)
{
    if (held == fresh)
        return;
    held = std::move(fresh);
    emit axesChanged();
}

void Filters::countFiles()
{
    ++m_fileGeneration;
    if (!m_server)
        return;

    const QString wanted = m_shelf ? m_shelf->query() : QString();
    if (wanted.isEmpty()) {
        // Nothing is being searched for, so there is no list of files for a row to sit above.
        m_fileReadStatuses.clear();
        m_fileMedia.clear();
        offer(m_fileAxes, {});
        emit changed();
        return;
    }

    QUrlQuery query;
    query.addQueryItem(u"over"_s, u"files"_s);
    query.addQueryItem(u"q"_s, wanted);

    const int mine = m_fileGeneration;
    m_server->get(u"/filters"_s, query, this, [this, mine](const Server::Answer &answer) {
        if (mine == m_fileGeneration)
            tookFiles(answer);
    });
}

void Filters::tookFiles(const Server::Answer &answer)
{
    if (!answer.went() || !answer.body.isObject())
        return;

    const Api::Read<Api::Facets> read = Api::facets(answer.body.object());
    if (!read.ok())
        return;

    // The same floor and ceiling as the series row: an axis holding one value filters
    // nothing, and past four the artifact asks for a menu, which is not drawn yet.
    const QVariantList statuses = readStatusPills(read.value->readStatuses);
    const QVariantList kinds = mediumPills(read.value->media);
    const auto offered = [](const QVariantList &pills) {
        return pills.size() >= Fewest && pills.size() <= Most;
    };
    m_fileReadStatuses = offered(statuses) ? statuses : QVariantList();
    m_fileMedia = offered(kinds) ? kinds : QVariantList();
    offer(m_fileAxes, axesOf(*read.value));
    emit changed();
}

void Filters::reload()
{
    ++m_generation;
    m_readStatuses.clear();
    m_media.clear();
    m_tooMany = false;
    m_trouble.clear();

    if (!m_server) {
        m_loading = false;
        m_trouble = Words::notSetUp(Words::Asking::Filters);
        emit changed();
        return;
    }

    m_loading = true;
    emit changed();

    const int mine = m_generation;
    m_server->get(u"/filters"_s, this, [this, mine](const Server::Answer &answer) {
        if (mine == m_generation)
            took(answer);
    });
}

void Filters::took(const Server::Answer &answer)
{
    m_loading = false;

    if (!answer.went()) {
        m_trouble = answer.trouble;
        emit changed();
        return;
    }

    if (!answer.body.isObject()) {
        m_trouble = QStringLiteral("filters: expected an object");
        emit changed();
        return;
    }

    const Api::Read<Api::Facets> read = Api::facets(answer.body.object());
    if (!read.ok()) {
        m_trouble = read.trouble;
        emit changed();
        return;
    }

    const QVariantList statuses = readStatusPills(read.value->readStatuses);
    const QVariantList kinds = mediumPills(read.value->media);

    // An axis is offered when it has something to say and a row can hold it. Below the floor
    // it cuts nothing; above the ceiling it is a menu, which is not drawn yet.
    const auto offered = [](const QVariantList &pills) {
        return pills.size() >= Fewest && pills.size() <= Most;
    };
    m_tooMany = statuses.size() > Most || kinds.size() > Most;
    m_readStatuses = offered(statuses) ? statuses : QVariantList();
    m_media = offered(kinds) ? kinds : QVariantList();
    // The panel has room the row has not, so the ceiling of four does not apply to it: a
    // list of thirty authors is a list, where a row of thirty pills is a wall.
    offer(m_axes, axesOf(*read.value));
    m_trouble.clear();
    emit changed();
}
