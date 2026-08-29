#include "Api.h"

#include "Ascii.h"

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
    bool has(QLatin1StringView name) const
    {
        const QJsonValue value = m_from.value(name);
        return !value.isUndefined() && !value.isNull();
    }

    QString text(QLatin1StringView name)
    {
        const QJsonValue value = m_from.value(name);
        if (!value.isString())
            return complain(name, QStringLiteral("text"));
        return value.toString();
    }

    int whole(QLatin1StringView name)
    {
        const QJsonValue value = m_from.value(name);
        if (!value.isDouble()) {
            complain(name, QStringLiteral("a number"));
            return 0;
        }
        return value.toInt();
    }

    QJsonObject object(QLatin1StringView name)
    {
        const QJsonValue value = m_from.value(name);
        if (!value.isObject()) {
            complain(name, QStringLiteral("an object"));
            return {};
        }
        return value.toObject();
    }

    std::optional<QString> maybeText(QLatin1StringView name) const
    {
        if (!has(name))
            return std::nullopt;
        const QJsonValue value = m_from.value(name);
        return value.isString() ? std::optional<QString>(value.toString()) : std::nullopt;
    }

    std::optional<int> maybeWhole(QLatin1StringView name) const
    {
        if (!has(name))
            return std::nullopt;
        const QJsonValue value = m_from.value(name);
        return value.isDouble() ? std::optional<int>(value.toInt()) : std::nullopt;
    }

    std::optional<qint64> maybeBig(QLatin1StringView name) const
    {
        if (!has(name))
            return std::nullopt;
        const QJsonValue value = m_from.value(name);
        return value.isDouble() ? std::optional<qint64>(value.toInteger()) : std::nullopt;
    }

    std::optional<double> maybeReal(QLatin1StringView name) const
    {
        if (!has(name))
            return std::nullopt;
        const QJsonValue value = m_from.value(name);
        return value.isDouble() ? std::optional<double>(value.toDouble()) : std::nullopt;
    }

    /// An absent list and an empty list are the same thing, so neither is worth a complaint.
    QList<QString> words(QLatin1StringView name) const
    {
        QList<QString> all;
        for (const QJsonValue &value : m_from.value(name).toArray())
            if (value.isString())
                all.append(value.toString());
        return all;
    }

    QList<double> reals(QLatin1StringView name) const
    {
        QList<double> all;
        for (const QJsonValue &value : m_from.value(name).toArray())
            if (value.isDouble())
                all.append(value.toDouble());
        return all;
    }

private:
    QString complain(QLatin1StringView name, const QString &wanted)
    {
        if (m_trouble.isEmpty()) {
            const QJsonValue value = m_from.value(name);
            QString state = QStringLiteral("is not %1").arg(wanted);
            if (value.isUndefined())
                state = QStringLiteral("is missing");
            else if (value.isNull())
                state = QStringLiteral("is null");
            m_trouble = QStringLiteral("%1 %2").arg(QString(name), state);
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

QList<Api::Facet> facetsUnder(const QJsonObject &from, QLatin1StringView name)
{
    QList<Api::Facet> all;
    for (const QJsonValue &value : from.value(name).toArray()) {
        const QJsonObject one = value.toObject();
        const QJsonValue count = one.value("count"_ascii);
        if (one.value("value"_ascii).isString() && count.isDouble())
            all.append({one.value("value"_ascii).toString(), count.toInt()});
    }
    return all;
}

} // namespace

namespace Api {

Medium medium(const QString &word)
{
    using enum Medium;

    const QString plain = word.toLower();
    if (plain == "manga"_ascii)
        return Manga;
    if (plain == "bd"_ascii)
        return Bd;
    if (plain == "comics"_ascii)
        return Comics;
    if (plain == "manhwa"_ascii)
        return Manhwa;
    if (plain == "manhua"_ascii)
        return Manhua;
    if (plain == "webtoon"_ascii)
        return Webtoon;
    if (plain == "artbook"_ascii)
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

ReadStatus readStatus(const QString &word)
{
    using enum ReadStatus;

    if (word == "IN_PROGRESS"_ascii)
        return InProgress;
    if (word == "READ"_ascii)
        return Read;
    return Unread;
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

    one.id = field.text("id"_ascii);
    one.workId = field.text("workId"_ascii);
    one.name = field.text("name"_ascii);
    one.work = field.text("work"_ascii);
    one.entryCount = field.whole("entryCount"_ascii);
    one.chapterCount = field.whole("chapterCount"_ascii);
    one.arcCount = field.whole("arcCount"_ascii);
    if (field.broken())
        return refused<Series>(QStringLiteral("series"), field.trouble());

    one.universe = field.maybeText("universe"_ascii);
    one.edition = field.maybeText("edition"_ascii);
    one.author = field.maybeText("author"_ascii);
    one.publisher = field.maybeText("publisher"_ascii);
    one.language = field.maybeText("language"_ascii);
    one.declaredVolumes = field.maybeWhole("declaredVolumes"_ascii);
    one.addedAt = field.maybeBig("addedAt"_ascii);
    one.lastAddedAt = field.maybeBig("lastAddedAt"_ascii);
    one.ownedVolumes = field.maybeWhole("ownedVolumes"_ascii).value_or(0);
    one.genres = field.words("genres"_ascii);
    one.missingVolumes = field.reals("missingVolumes"_ascii);
    one.missingChapters = field.reals("missingChapters"_ascii);

    // Vocabulary the client may not know yet never refuses a row — see the note in Api.h.
    if (const auto word = field.maybeText("medium"_ascii))
        one.medium = medium(*word);
    if (const auto word = field.maybeText("readingDirection"_ascii)) {
        // Scoped to the block rather than the function: three names are worth shortening
        // here, and everything around them belongs to other enums.
        using enum ReadingDirection;
        if (*word == "RIGHT_TO_LEFT"_ascii)
            one.readingDirection = RightToLeft;
        else if (*word == "VERTICAL"_ascii)
            one.readingDirection = Vertical;
        else if (*word == "LEFT_TO_RIGHT"_ascii)
            one.readingDirection = LeftToRight;
    }
    if (const auto word = field.maybeText("status"_ascii))
        one.run = (*word == "completed"_ascii) ? Run::Completed : Run::Ongoing;
    if (const auto word = field.maybeText("readStatus"_ascii))
        one.readStatus = readStatus(*word);

    return {one, {}};
}

Read<Page> page(const QJsonObject &from)
{
    Fields field(from);
    Page some;
    some.total = field.whole("total"_ascii);
    some.page = field.whole("page"_ascii);
    some.size = field.whole("size"_ascii);
    if (field.broken())
        return refused<Page>(QStringLiteral("page"), field.trouble());

    const QJsonValue items = from.value("items"_ascii);
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
    all.readStatuses = facetsUnder(from, "readStatuses"_ascii);
    all.universes = facetsUnder(from, "universes"_ascii);
    all.authors = facetsUnder(from, "authors"_ascii);
    all.genres = facetsUnder(from, "genres"_ascii);
    all.media = facetsUnder(from, "media"_ascii);
    all.statuses = facetsUnder(from, "statuses"_ascii);
    all.languages = facetsUnder(from, "languages"_ascii);
    all.publishers = facetsUnder(from, "publishers"_ascii);
    return {all, {}};
}

Read<UpNext> upNext(const QJsonObject &from)
{
    Fields field(from);
    UpNext card;
    card.seriesId = field.text("seriesId"_ascii);
    card.seriesName = field.text("seriesName"_ascii);
    const QString reason = field.text("reason"_ascii);
    const QJsonObject entry = field.object("entry"_ascii);
    if (field.broken())
        return refused<UpNext>(QStringLiteral("upNext"), field.trouble());

    card.reason = (reason == "IN_PROGRESS"_ascii) ? UpNext::Reason::InProgress : UpNext::Reason::NextUp;

    Fields inside(entry);
    card.entryId = inside.text("id"_ascii);
    card.pageCount = inside.whole("pageCount"_ascii);
    const QString kind = inside.text("type"_ascii);
    if (inside.broken())
        return refused<UpNext>(QStringLiteral("upNext.entry"), inside.trouble());

    card.entryKind = (kind == "CHAPTER"_ascii) ? UpNext::Kind::Chapter : UpNext::Kind::Volume;
    card.entryNumber = inside.maybeReal("number"_ascii);
    card.entryTitle = inside.maybeText("title"_ascii);

    // Absent progress is not a fault: it is what "you have not started this one" looks like.
    if (const QJsonValue progress = from.value("progress"_ascii); progress.isObject()) {
        const QJsonObject where = progress.toObject();
        Fields at(where);
        card.page = at.maybeWhole("page"_ascii);
        if (const auto pages = at.maybeWhole("pageCount"_ascii); pages && *pages > 0)
            card.pageCount = *pages;
        const QJsonValue chapter = where.value("chapter"_ascii);
        if (chapter.isObject()) {
            // Named, not a temporary: Fields keeps a reference to what it reads.
            const QJsonObject inChapter = chapter.toObject();
            card.chapterLabel = Fields(inChapter).maybeText("label"_ascii);
        }
    }

    return {card, {}};
}

} // namespace Api
