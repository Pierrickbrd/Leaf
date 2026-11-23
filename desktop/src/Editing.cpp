#include "Editing.h"

#include "Api.h"
#include "Fold.h"
#include "Words.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>

#include <utility>

using namespace Qt::StringLiterals;

namespace {

/// One series as the editing window reads it, its pictures asked of `address`.
QVariantMap seriesFields(const Api::Series &one, const QString &address)
{
    const auto text = [](const std::optional<QString> &value) {
        return value.value_or(QString());
    };
    QVariantMap said{
        {u"id"_s, one.id},
        {u"cover"_s, address + u"/series/"_s + one.id + u"/cover"_s},
        {u"coverImage"_s, one.coverImage},
        // The number of the volume chosen as the cover; left out, so that the window
        // reads `undefined`, when none was: a volume can be numbered 0.
        {u"title"_s, one.work},
        {u"summary"_s, text(one.summary)},
        // Which of the two the summary is: an edition's own words are edited in its own
        // file, and the work's sentence is never copied into it.
        {u"ownSummary"_s, one.ownSummary.has_value()},
        // What `work.json` declares, never the union of the volumes the series is shown
        // with: saving the window wrote that union back as the default, and Jul, who wrote
        // three albums, became the writer of every album that names nobody.
        {u"authors"_s, QVariant::fromValue(one.declared.authors)},
        {u"artists"_s, QVariant::fromValue(one.declared.artists)},
        {u"authorsDefault"_s, Words::creditDefault(false, one.medium)},
        {u"artistsDefault"_s, Words::creditDefault(true, one.medium)},
        {u"genres"_s, QVariant::fromValue(one.genres)},
        {u"tags"_s, QVariant::fromValue(one.tags)},
        {u"publisher"_s, text(one.publication.publisher)},
        {u"collection"_s, text(one.publication.collection)},
        {u"language"_s, text(one.publication.language)},
        {u"ageRating"_s, text(one.ageRating)},
        {u"volumeCount"_s, one.publication.declaredVolumes.value_or(0)},
        // The run's own number (`Api::Run`), −1 not said: the window's choice speaks integers.
        {u"status"_s, one.run ? std::to_underlying(*one.run) : -1},
        {u"medium"_s, one.medium ? std::to_underlying(*one.medium) : -1}};
    if (one.coverVolume.has_value())
        said.insert(u"coverVolume"_s, *one.coverVolume);
    return said;
}

} // namespace

Editing *Editing::create(QQmlEngine *engine, QJSEngine *)
{
    auto *server = engine->singletonInstance<Server *>(qmlTypeId("Leaf", 1, 0, "Server"));
    if (!server) {
        qWarning().noquote()
            << QStringLiteral("error resolving the Server singleton — the editing windows will "
                              "open on nothing");
    }
    return new Editing(server);
}

Editing::Editing(Server *server, QObject *parent)
    : QObject(parent)
    , m_server(server)
{
}

QVariantMap Editing::words() const
{
    return Words::editing();
}

QString Editing::languageName(const QString &tag) const
{
    return Words::language(tag);
}

QString Editing::searchKey(const QString &text) const
{
    return Fold::searchKey(text);
}

QVariantList Editing::mediums() const
{
    using enum Api::Medium;
    QVariantList all{QVariantMap{{u"value"_s, -1}, {u"label"_s, Words::editing().value(u"unsaid"_s)}}};
    for (const Api::Medium one : {Manga, Bd, Comics, Manhwa, Manhua, Webtoon, Artbook, Other})
        all.append(QVariantMap{{u"value"_s, std::to_underlying(one)}, {u"label"_s, Words::medium(one)}});
    return all;
}

void Editing::loadSeries(const QString &seriesId)
{
    if (seriesId.isEmpty() || m_server == nullptr)
        return;
    m_server->get(u"/series/"_s + seriesId, this, [this](const Server::Answer &answer) {
        if (!answer.went() || !answer.body.isObject())
            return;
        const Api::Read<Api::Series> read = Api::series(answer.body.object());
        if (!read.ok())
            return;
        emit seriesLoaded(seriesFields(*read.value, m_server->address()));
    });
}

void Editing::loadVolumes(const QString &seriesId)
{
    if (seriesId.isEmpty() || m_server == nullptr)
        return;
    m_server->get(u"/series/"_s + seriesId + u"/entries"_s, this,
                  [this, seriesId](const Server::Answer &answer) {
        if (!answer.went() || !answer.body.isArray())
            return;
        QVariantList volumes;
        for (const QJsonValue &one : answer.body.array()) {
            const Api::Read<Api::Entry> read = Api::entry(one.toObject());
            // A chapter, or a file with no number: neither is what the server takes as a
            // series' cover volume, which is named by its number.
            if (!read.ok() || read.value->kind != Api::Entry::Kind::Volume
                    || !read.value->number.has_value())
                continue;
            volumes.append(QVariantMap{
                {u"number"_s, *read.value->number},
                {u"label"_s, Words::readerVolume(*read.value->number)},
                {u"cover"_s, m_server->address() + u"/entries/"_s + read.value->id + u"/cover"_s}});
        }
        emit volumesLoaded(seriesId, volumes);
    });
}

void Editing::loadVocabulary()
{
    if (m_server == nullptr)
        return;
    m_server->get(u"/filters"_s, this, [this](const Server::Answer &answer) {
        if (!answer.went() || !answer.body.isObject())
            return;
        const Api::Read<Api::Facets> read = Api::facets(answer.body.object());
        if (!read.ok())
            return;
        const auto names = [](const QList<Api::Facet> &facets) {
            QStringList all;
            for (const Api::Facet &one : facets)
                all << one.value;
            return all;
        };
        emit vocabularyLoaded({{u"authors"_s, names(read.value->authors)},
                               {u"artists"_s, names(read.value->artists)},
                               {u"genres"_s, names(read.value->genres)},
                               {u"tags"_s, names(read.value->tags)},
                               {u"publishers"_s, names(read.value->publishers)},
                               {u"collections"_s, names(read.value->collections)},
                               {u"languages"_s, names(read.value->languages)}});
    });
}
