#include "Api.h"

#include <QJsonArray>
#include <QJsonValue>

using Qt::Literals::StringLiterals::operator""_s;

namespace {

/// Reads fields out of one object and remembers the first it could not read.
///
/// The first, not all of them: a broken answer is broken, and a list of six complaints about
/// the same missing object tells a person nothing the first one did not.
class Fields
{
public:
    explicit Fields(const QJsonObject &from) : m_from(from) {}

    bool broken() const { return !m_trouble.isEmpty(); }
    QString trouble() const { return m_trouble; }

    /// Present and not null. A JSON `null` is the contract's way of saying "not recorded",
    /// so it counts as absent everywhere here.
    bool has(QStringView name) const
    {
        const QJsonValue value = m_from.value(name);
        return !value.isUndefined() && !value.isNull();
    }

    QString text(QStringView name)
    {
        const QJsonValue value = m_from.value(name);
        if (!value.isString())
            return complain(name, QStringLiteral("text"));
        return value.toString();
    }

    int whole(QStringView name)
    {
        const QJsonValue value = m_from.value(name);
        if (!value.isDouble()) {
            complain(name, QStringLiteral("a number"));
            return 0;
        }
        return value.toInt();
    }

    QJsonObject object(QStringView name)
    {
        const QJsonValue value = m_from.value(name);
        if (!value.isObject()) {
            complain(name, QStringLiteral("an object"));
            return {};
        }
        return value.toObject();
    }

    std::optional<QString> maybeText(QStringView name) const
    {
        if (!has(name))
            return std::nullopt;
        const QJsonValue value = m_from.value(name);
        return value.isString() ? std::optional<QString>(value.toString()) : std::nullopt;
    }

    std::optional<int> maybeWhole(QStringView name) const
    {
        if (!has(name))
            return std::nullopt;
        const QJsonValue value = m_from.value(name);
        return value.isDouble() ? std::optional<int>(value.toInt()) : std::nullopt;
    }

    std::optional<qint64> maybeBig(QStringView name) const
    {
        if (!has(name))
            return std::nullopt;
        const QJsonValue value = m_from.value(name);
        return value.isDouble() ? std::optional<qint64>(value.toInteger()) : std::nullopt;
    }

    std::optional<double> maybeReal(QStringView name) const
    {
        if (!has(name))
            return std::nullopt;
        const QJsonValue value = m_from.value(name);
        return value.isDouble() ? std::optional<double>(value.toDouble()) : std::nullopt;
    }

    std::optional<bool> maybeBool(QStringView name) const
    {
        if (!has(name))
            return std::nullopt;
        const QJsonValue value = m_from.value(name);
        return value.isBool() ? std::optional<bool>(value.toBool()) : std::nullopt;
    }

    /// An absent list and an empty list are the same thing, so neither is worth a complaint.
    QList<QString> words(QStringView name) const
    {
        QList<QString> all;
        for (const QJsonValue &value : m_from.value(name).toArray())
            if (value.isString())
                all.append(value.toString());
        return all;
    }

    QList<double> reals(QStringView name) const
    {
        QList<double> all;
        for (const QJsonValue &value : m_from.value(name).toArray())
            if (value.isDouble())
                all.append(value.toDouble());
        return all;
    }

private:
    QString complain(QStringView name, const QString &wanted)
    {
        if (m_trouble.isEmpty()) {
            const QJsonValue value = m_from.value(name);
            QString state = QStringLiteral("is not %1").arg(wanted);
            if (value.isUndefined())
                state = QStringLiteral("is missing");
            else if (value.isNull())
                state = QStringLiteral("is null");
            m_trouble = QStringLiteral("%1 %2").arg(name.toString(), state);
        }
        return {};
    }

    const QJsonObject &m_from;
    QString m_trouble;
};

template <typename T>
Api::Read<T> refused(const QString &what, const QString &trouble)
{
    return {std::nullopt, QStringLiteral("%1: %2").arg(what, trouble)};
}

QList<Api::Facet> facetsUnder(const QJsonObject &from, QStringView name)
{
    QList<Api::Facet> all;
    for (const QJsonValue &value : from.value(name).toArray()) {
        const QJsonObject one = value.toObject();
        const QJsonValue count = one.value(u"count"_s);
        if (one.value(u"value"_s).isString() && count.isDouble())
            all.append({one.value(u"value"_s).toString(), count.toInt()});
    }
    return all;
}

} // namespace

namespace Api {

Medium medium(const QString &word)
{
    using enum Medium;

    const QString plain = word.toLower();
    if (plain == u"manga"_s)
        return Manga;
    if (plain == u"bd"_s)
        return Bd;
    if (plain == u"comics"_s)
        return Comics;
    if (plain == u"manhwa"_s)
        return Manhwa;
    if (plain == u"manhua"_s)
        return Manhua;
    if (plain == u"webtoon"_s)
        return Webtoon;
    if (plain == u"artbook"_s)
        return Artbook;
    return Other;
}

QString spell(Medium value)
{
    using enum Medium;

    switch (value) {
    case Manga:
        return QStringLiteral("manga");
    case Bd:
        return QStringLiteral("bd");
    case Comics:
        return QStringLiteral("comics");
    case Manhwa:
        return QStringLiteral("manhwa");
    case Manhua:
        return QStringLiteral("manhua");
    case Webtoon:
        return QStringLiteral("webtoon");
    case Artbook:
        return QStringLiteral("artbook");
    case Other:
        return QStringLiteral("other");
    }
    return QStringLiteral("other");
}

std::optional<ReadStatus> readStatus(const QString &word)
{
    using enum ReadStatus;

    if (word == u"IN_PROGRESS"_s)
        return InProgress;
    if (word == u"READ"_s)
        return Read;
    if (word == u"UNREAD"_s)
        return Unread;
    // A fourth word is not one of the three. Reported as Unread it became a claim about a
    // collection — a series read to the end, shown as untouched — where nothing was known.
    return std::nullopt;
}

QString spell(ReadStatus value)
{
    using enum ReadStatus;

    switch (value) {
    case InProgress:
        return QStringLiteral("IN_PROGRESS");
    case Read:
        return QStringLiteral("READ");
    case Unread:
        return QStringLiteral("UNREAD");
    }
    return QStringLiteral("UNREAD");
}

Read<Series> series(const QJsonObject &from)
{
    Fields field(from);
    Series one;

    one.id = field.text(u"id"_s);
    one.workId = field.text(u"workId"_s);
    one.name = field.text(u"name"_s);
    one.work = field.text(u"work"_s);
    one.counts.entries = field.whole(u"entryCount"_s);
    one.counts.chapters = field.whole(u"chapterCount"_s);
    one.counts.arcs = field.whole(u"arcCount"_s);
    if (field.broken())
        return refused<Series>(QStringLiteral("series"), field.trouble());

    one.universe = field.maybeText(u"universe"_s);
    one.edition = field.maybeText(u"edition"_s);
    one.credits.author = field.maybeText(u"author"_s);
    one.credits.authors = field.words(u"authors"_s);
    one.credits.artists = field.words(u"artists"_s);
    one.publisher = field.maybeText(u"publisher"_s);
    one.collection = field.maybeText(u"collection"_s);
    one.language = field.maybeText(u"language"_s);
    one.declaredVolumes = field.maybeWhole(u"declaredVolumes"_s);
    one.genres = field.words(u"genres"_s);
    one.tags = field.words(u"tags"_s);
    one.ageRating = field.maybeText(u"ageRating"_s);
    one.colour = field.maybeBool(u"colour"_s);

    one.holding.addedAt = field.maybeBig(u"addedAt"_s);
    one.holding.lastAddedAt = field.maybeBig(u"lastAddedAt"_s);
    one.holding.ownedVolumes = field.maybeWhole(u"ownedVolumes"_s).value_or(0);
    one.holding.missingVolumes = field.reals(u"missingVolumes"_s);
    one.holding.missingChapters = field.reals(u"missingChapters"_s);

    // Vocabulary the client may not know yet never refuses a row — see the note in Api.h.
    if (const auto word = field.maybeText(u"medium"_s))
        one.medium = medium(*word);
    if (const auto word = field.maybeText(u"readingDirection"_s)) {
        // Scoped to the block rather than the function: three names are worth shortening
        // here, and everything around them belongs to other enums.
        using enum ReadingDirection;
        if (*word == u"RIGHT_TO_LEFT"_s)
            one.readingDirection = RightToLeft;
        else if (*word == u"VERTICAL"_s)
            one.readingDirection = Vertical;
        else if (*word == u"LEFT_TO_RIGHT"_s)
            one.readingDirection = LeftToRight;
    }
    if (const auto word = field.maybeText(u"status"_s)) {
        // Two words today; a third — hiatus, cancelled — is a thing the server may learn
        // first. Everything that was not "completed" used to come out as Ongoing, so a
        // series that had stopped was shown as still running. Left absent instead, like the
        // reading direction two lines up.
        if (*word == u"completed"_s)
            one.run = Run::Completed;
        else if (*word == u"ongoing"_s)
            one.run = Run::Ongoing;
    }
    if (const auto word = field.maybeText(u"readStatus"_s))
        one.holding.readStatus = readStatus(*word);

    return {one, {}};
}

Read<Page> page(const QJsonObject &from)
{
    Fields field(from);
    Page some;
    some.total = field.whole(u"total"_s);
    some.page = field.whole(u"page"_s);
    some.size = field.whole(u"size"_s);
    if (field.broken())
        return refused<Page>(QStringLiteral("page"), field.trouble());

    const QJsonValue items = from.value(u"items"_s);
    if (!items.isArray())
        return refused<Page>(QStringLiteral("page"), QStringLiteral("items is not a list"));

    const QJsonArray all = items.toArray();
    for (int i = 0; i < all.size(); ++i) {
        const Read<Series> one = series(all.at(i).toObject());
        if (!one.ok())
            return refused<Page>(QStringLiteral("items[%1]").arg(i), one.trouble);
        some.items.append(*one.value);
    }
    return {some, {}};
}

Read<Facets> facets(const QJsonObject &from)
{
    Facets all;
    all.readStatuses = facetsUnder(from, u"readStatuses"_s);
    all.universes = facetsUnder(from, u"universes"_s);
    all.authors = facetsUnder(from, u"authors"_s);
    all.genres = facetsUnder(from, u"genres"_s);
    all.media = facetsUnder(from, u"media"_s);
    all.statuses = facetsUnder(from, u"statuses"_s);
    all.languages = facetsUnder(from, u"languages"_s);
    all.publishers = facetsUnder(from, u"publishers"_s);
    return {all, {}};
}

Read<UpNext> upNext(const QJsonObject &from)
{
    Fields field(from);
    UpNext card;
    card.seriesId = field.text(u"seriesId"_s);
    card.seriesName = field.text(u"seriesName"_s);
    const QString reason = field.text(u"reason"_s);
    const QJsonObject entry = field.object(u"entry"_s);
    if (field.broken())
        return refused<UpNext>(QStringLiteral("upNext"), field.trouble());

    card.reason = (reason == u"IN_PROGRESS"_s) ? UpNext::Reason::InProgress : UpNext::Reason::NextUp;

    Fields inside(entry);
    card.entryId = inside.text(u"id"_s);
    card.pageCount = inside.whole(u"pageCount"_s);
    const QString kind = inside.text(u"type"_s);
    if (inside.broken())
        return refused<UpNext>(QStringLiteral("upNext.entry"), inside.trouble());

    card.entryKind = (kind == u"CHAPTER"_s) ? UpNext::Kind::Chapter : UpNext::Kind::Volume;
    card.entryNumber = inside.maybeReal(u"number"_s);
    card.entryTitle = inside.maybeText(u"title"_s);

    // Absent progress is not a fault: it is what "you have not started this one" looks like.
    if (const QJsonValue progress = from.value(u"progress"_s); progress.isObject()) {
        const QJsonObject where = progress.toObject();
        Fields at(where);
        card.page = at.maybeWhole(u"page"_s);
        if (const auto pages = at.maybeWhole(u"pageCount"_s); pages && *pages > 0)
            card.pageCount = *pages;
        const QJsonValue chapter = where.value(u"chapter"_s);
        if (chapter.isObject()) {
            // Named, not a temporary: Fields keeps a reference to what it reads.
            const QJsonObject inChapter = chapter.toObject();
            card.chapterLabel = Fields(inChapter).maybeText(u"label"_s);
        }
    }

    return {card, {}};
}

} // namespace Api
