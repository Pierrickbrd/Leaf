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

    /// A number that has to be there, and may be a half: an arc bound is 68 or 68.5, and a
    /// volume number is a half as often as not.
    double real(QStringView name)
    {
        const QJsonValue value = m_from.value(name);
        if (!value.isDouble()) {
            complain(name, QStringLiteral("a number"));
            return 0;
        }
        return value.toDouble();
    }

    /// A list that has to be there. `words` takes an absent one for an empty one, which is
    /// right for genres and wrong for the steps of an order: an order with no steps is not a
    /// way through anything.
    QJsonArray array(QStringView name)
    {
        const QJsonValue value = m_from.value(name);
        if (!value.isArray()) {
            complain(name, QStringLiteral("a list"));
            return {};
        }
        return value.toArray();
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

    /// Held by value, and that is not an oversight. A reference here dangles the moment
    /// somebody writes `Fields one(value.toObject())` — the temporary dies at the end of
    /// the declaration and the fields read as absent, which looks exactly like a server
    /// that left them out. `QJsonObject` is implicitly shared, so the copy is a pointer.
    const QJsonObject m_from;
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

Hit::Kind hitKind(const QString &word)
{
    using enum Hit::Kind;

    const QString plain = word.toUpper();
    if (plain == u"EDITION"_s)
        return Edition;
    if (plain == u"ENTRY"_s)
        return Entry;
    if (plain == u"CHAPTER"_s)
        return Chapter;
    return Other;
}

QString spell(Hit::Kind value)
{
    using enum Hit::Kind;

    switch (value) {
    case Edition:
        return QStringLiteral("EDITION");
    case Entry:
        return QStringLiteral("ENTRY");
    case Chapter:
        return QStringLiteral("CHAPTER");
    case Other:
        return {};
    }
    return {};
}

Sort sort(const QString &word)
{
    using enum Sort;

    const QString plain = word.toLower();
    if (plain == u"added"_s)
        return Added;
    if (plain == u"volumes"_s)
        return Volumes;
    if (plain == u"read"_s)
        return Read;
    return Name;
}

QString spell(Sort value)
{
    using enum Sort;

    switch (value) {
    case Added:
        return QStringLiteral("added");
    case Volumes:
        return QStringLiteral("volumes");
    case Read:
        return QStringLiteral("read");
    case Name:
        return QStringLiteral("name");
    }
    return QStringLiteral("name");
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

double howFarRead(const Series &one)
{
    const int whole = one.counts.entries;
    if (whole <= 0)
        return 0.0;
    // The finished volumes **and** the page somebody stopped on in the one they are in:
    // « tome 12, page 156 » is eleven whole and 0.82 of a twelfth. Counting only the whole
    // ones made the bar jump a volume at a time and sit still in between, which on a
    // twenty-one volume series is most of the time.
    const double read = one.holding.readEntries + one.holding.partRead;
    return qBound(0.0, read / whole, 1.0);
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
    one.universeId = field.maybeText(u"universeId"_s);
    one.oneShotEntry = field.maybeText(u"oneShotEntry"_s);
    one.summary = field.maybeText(u"summary"_s);
    one.edition = field.maybeText(u"edition"_s);
    one.credits.author = field.maybeText(u"author"_s);
    one.credits.authors = field.words(u"authors"_s);
    one.credits.artists = field.words(u"artists"_s);
    one.publication.publisher = field.maybeText(u"publisher"_s);
    one.publication.collection = field.maybeText(u"collection"_s);
    one.publication.language = field.maybeText(u"language"_s);
    one.publication.declaredVolumes = field.maybeWhole(u"declaredVolumes"_s);
    one.genres = field.words(u"genres"_s);
    one.tags = field.words(u"tags"_s);
    one.ageRating = field.maybeText(u"ageRating"_s);
    one.colour = field.maybeBool(u"colour"_s);

    one.holding.addedAt = field.maybeBig(u"addedAt"_s);
    one.holding.lastAddedAt = field.maybeBig(u"lastAddedAt"_s);
    one.holding.ownedVolumes = field.maybeWhole(u"ownedVolumes"_s).value_or(0);
    one.holding.readEntries = field.maybeWhole(u"readEntries"_s).value_or(0);
    one.holding.partRead = field.maybeReal(u"partRead"_s).value_or(0.0);
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

Read<Hit> hit(const QJsonObject &from)
{
    Fields field(from);
    Hit found;
    found.id = field.text(u"id"_s);
    found.label = field.text(u"label"_s);
    const QString kind = field.text(u"kind"_s);
    if (field.broken())
        return refused<Hit>(QStringLiteral("hit"), field.trouble());

    found.kind = hitKind(kind);
    found.seriesId = field.maybeText(u"seriesId"_s);
    found.seriesName = field.maybeText(u"seriesName"_s);
    found.entryId = field.maybeText(u"entryId"_s);
    if (const std::optional<QString> kindOfEntry = field.maybeText(u"entryKind"_s)) {
        found.entryKind = (*kindOfEntry == u"CHAPTER"_s) ? UpNext::Kind::Chapter
                                                         : UpNext::Kind::Volume;
    }
    found.entryNumber = field.maybeReal(u"entryNumber"_s);
    found.entryTitle = field.maybeText(u"entryTitle"_s);
    found.entryPageCount = field.maybeWhole(u"entryPageCount"_s);
    found.chapterNumber = field.maybeReal(u"chapterNumber"_s);
    found.chapterTitle = field.maybeText(u"chapterTitle"_s);
    // Absent is not approximate: the contract's default is false, and a client reading a
    // missing field as "maybe" would put a guess mark on every exact hit an older server sent.
    found.approximate = field.maybeBool(u"approximate"_s).value_or(false);

    return {found, {}};
}

namespace {

/// A file is an entry or a chapter. An edition is a shelf tile and is counted apart, which is
/// why a heading over the lines cannot simply say how many hits there were.
bool isFile(const Hit &one)
{
    return !one.approximate
        && (one.kind == Hit::Kind::Entry || one.kind == Hit::Kind::Chapter);
}

Read<Hits> fromRows(const QJsonArray &rows, Hits page)
{
    for (int i = 0; i < rows.size(); ++i) {
        const Read<Hit> read = hit(rows.at(i).toObject());
        if (!read.ok())
            return refused<Hits>(QStringLiteral("hits[%1]").arg(i), read.trouble);
        page.items << *read.value;
    }
    return {page, {}};
}

} // namespace

Read<Hits> hits(const QJsonDocument &from)
{
    Hits page;

    if (from.isArray()) {
        const QJsonArray rows = from.array();
        page.size = int(rows.size());
        const Read<Hits> read = fromRows(rows, page);
        if (!read.ok())
            return read;
        // Nothing else said how many there are, so what is in hand is all there is known to
        // be. A count invented larger than the list would put a "see the others" under lines
        // that are already all of them.
        Hits counted = *read.value;
        counted.total = int(counted.items.size());
        for (const Hit &one : counted.items) {
            if (isFile(one))
                ++counted.fileTotal;
        }
        return {counted, {}};
    }

    if (!from.isObject())
        return refused<Hits>(QStringLiteral("hits"), QStringLiteral("expected a list"));

    const QJsonObject envelope = from.object();
    Fields field(envelope);
    page.total = field.whole(u"total"_s);
    page.fileTotal = field.whole(u"fileTotal"_s);
    page.page = field.whole(u"page"_s);
    page.size = field.whole(u"size"_s);
    if (field.broken())
        return refused<Hits>(QStringLiteral("hits"), field.trouble());

    const QJsonValue items = envelope.value(u"items"_s);
    if (!items.isArray())
        return refused<Hits>(QStringLiteral("hits"), QStringLiteral("items: expected a list"));

    return fromRows(items.toArray(), page);
}

Read<Health> health(const QJsonObject &from)
{
    Fields field(from);
    Health said;

    said.status = field.text(u"status"_s);
    said.api = field.whole(u"api"_s);
    said.format = field.whole(u"format"_s);
    said.library = field.whole(u"library"_s);
    if (field.broken())
        return refused<Health>(QStringLiteral("health"), field.trouble());

    // Absent means no shared folder, which is the ordinary case for a server on another
    // machine — not a malformed answer.
    said.localDrop = field.maybeBool(u"localDrop"_s).value_or(false);
    return {said, {}};
}

Read<ScanStatus> scanStatus(const QJsonObject &from)
{
    Fields field(from);
    ScanStatus said;

    const QString state = field.text(u"state"_s);
    if (field.broken())
        return refused<ScanStatus>(QStringLiteral("scan"), field.trouble());

    // A word this client has not been taught leaves the state `Other` rather than refusing
    // the answer: the server may grow a fourth, and a screen that cannot name it can still
    // say when the last scan ran.
    using enum ScanStatus::State;
    if (state == u"IDLE"_s)
        said.state = Idle;
    else if (state == u"RUNNING"_s)
        said.state = Running;
    else if (state == u"DONE"_s)
        said.state = Done;

    said.startedAt = field.maybeBig(u"startedAt"_s);
    said.finishedAt = field.maybeBig(u"finishedAt"_s);

    if (field.has(u"report"_s)) {
        const QJsonObject wrote = from.value(u"report"_s).toObject();
        ScanFindings found;
        const QJsonObject counted = wrote.value(u"counts"_s).toObject();
        Fields count(counted);
        found.counts.universes = count.whole(u"universes"_s);
        found.counts.works = count.whole(u"works"_s);
        found.counts.editions = count.whole(u"editions"_s);
        found.counts.entries = count.whole(u"entries"_s);
        found.counts.chapters = count.whole(u"chapters"_s);
        found.counts.pages = count.whole(u"pages"_s);
        found.counts.reanalysed = count.whole(u"reanalysed"_s);
        found.counts.progressCarried = count.maybeWhole(u"progressCarried"_s).value_or(0);
        found.counts.progressLost = count.maybeWhole(u"progressLost"_s).value_or(0);
        if (count.broken())
            return refused<ScanStatus>(QStringLiteral("scan"), count.trouble());

        Fields outer(wrote);
        found.chaptersWithoutStartPage =
            outer.maybeWhole(u"chaptersWithoutStartPage"_s).value_or(0);
        found.failure = outer.maybeText(u"failure"_s).value_or(QString());

        const QJsonArray listed = wrote.value(u"findings"_s).toArray();
        for (const QJsonValue &one : listed) {
            const QJsonObject each = one.toObject();
            Fields at(each);
            Finding finding;
            finding.kind = at.text(u"kind"_s);
            finding.total = at.whole(u"total"_s);
            if (at.broken())
                return refused<ScanStatus>(QStringLiteral("scan"), at.trouble());
            finding.items = at.words(u"items"_s);
            found.findings.append(finding);
        }
        said.report = found;
    }
    return {said, {}};
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

Read<Entry> entry(const QJsonObject &from)
{
    Fields field(from);
    Entry one;
    one.id = field.text(u"id"_s);
    one.pageCount = field.whole(u"pageCount"_s);
    one.chapterCount = field.whole(u"chapterCount"_s);
    one.file = field.text(u"file"_s);
    const QString kind = field.text(u"type"_s);
    if (field.broken())
        return refused<Entry>(QStringLiteral("entry"), field.trouble());

    // An unfamiliar word makes a volume rather than refusing the file: the server may learn
    // a kind before this client does, and a volume is what all but loose chapters are.
    one.kind = (kind == u"CHAPTER"_s) ? Entry::Kind::Chapter : Entry::Kind::Volume;
    one.number = field.maybeReal(u"number"_s);
    one.title = field.maybeText(u"title"_s);
    one.sortKey = field.maybeReal(u"sortKey"_s);
    one.size = field.maybeWhole(u"size"_s).value_or(0);
    return {one, {}};
}

Read<Progress> progress(const QJsonObject &from)
{
    Fields field(from);
    Progress where;
    where.entryId = field.text(u"entryId"_s);
    where.page = field.whole(u"page"_s);
    where.pageCount = field.whole(u"pageCount"_s);
    if (field.broken())
        return refused<Progress>(QStringLiteral("progress"), field.trouble());

    where.finished = from.value(u"finished"_s).toBool(false);
    // Absent at nought, like every other count in these answers.
    where.timesFinished = field.maybeWhole(u"timesFinished"_s).value_or(0);
    return {where, {}};
}

Read<Arc> arc(const QJsonObject &from)
{
    Fields field(from);
    Arc one;
    one.id = field.text(u"id"_s);
    one.name = field.text(u"name"_s);
    one.from = field.real(u"from"_s);
    one.to = field.real(u"to"_s);
    one.position = field.whole(u"position"_s);
    const QString unit = field.text(u"unit"_s);
    if (field.broken())
        return refused<Arc>(QStringLiteral("arc"), field.trouble());

    // An unfamiliar unit makes a chapter range, which is what the format calls the ordinary
    // one. The arc still stands: a word this client has not been taught is not a reason to
    // drop a stretch of the story.
    one.unit = (unit == u"VOLUME"_s) ? Arc::Unit::Volume : Arc::Unit::Chapter;
    one.parentId = field.maybeText(u"parentId"_s);
    return {one, {}};
}

Read<ReadingStep> readingStep(const QJsonObject &from)
{
    Fields field(from);
    ReadingStep step;
    step.workId = field.text(u"workId"_s);
    step.work = field.text(u"work"_s);
    if (field.broken())
        return refused<ReadingStep>(QStringLiteral("readingStep"), field.trouble());

    if (const auto unit = field.maybeText(u"unit"_s); unit.has_value())
        step.unit = (*unit == u"VOLUME"_s) ? ReadingStep::Unit::Volume
                                           : ReadingStep::Unit::Chapter;
    step.seriesId = field.maybeText(u"seriesId"_s);
    step.series = field.maybeText(u"series"_s);
    step.from = field.maybeReal(u"from"_s);
    step.to = field.maybeReal(u"to"_s);
    return {step, {}};
}

Read<ReadingOrder> readingOrder(const QJsonObject &from)
{
    Fields field(from);
    ReadingOrder order;
    order.id = field.text(u"id"_s);
    order.name = field.text(u"name"_s);
    const QJsonArray steps = field.array(u"steps"_s);
    if (field.broken())
        return refused<ReadingOrder>(QStringLiteral("readingOrder"), field.trouble());

    order.isDefault = from.value(u"default"_s).toBool(false);
    for (const QJsonValue &one : steps) {
        if (!one.isObject())
            return refused<ReadingOrder>(QStringLiteral("readingOrder.steps"),
                                         QStringLiteral("expected an object"));
        const Read<ReadingStep> step = readingStep(one.toObject());
        if (!step.ok())
            return refused<ReadingOrder>(QStringLiteral("readingOrder.steps"), step.trouble);
        order.steps.append(*step.value);
    }
    return {order, {}};
}

Read<Universe> universe(const QJsonObject &from)
{
    Fields field(from);
    Universe one;
    one.id = field.text(u"id"_s);
    one.name = field.text(u"name"_s);
    one.orderCount = field.whole(u"orderCount"_s);
    if (field.broken())
        return refused<Universe>(QStringLiteral("universe"), field.trouble());
    return {one, {}};
}

// ——— L'import ———————————————————————————————————————————————————————————————

namespace {

/// What a file said about itself. Absent everywhere, because the file may say nothing —
/// and a file that says nothing is an ordinary file from elsewhere, not a broken answer.
FileReading readingIn(const QJsonObject &from)
{
    Fields field(from);
    FileReading said;
    said.work = field.maybeText(u"work"_s);
    said.edition = field.maybeText(u"edition"_s);
    said.kind = field.maybeText(u"type"_s);
    said.number = field.maybeReal(u"number"_s);
    said.title = field.maybeText(u"title"_s);
    said.chapterCount = field.maybeWhole(u"chapterCount"_s).value_or(0);
    return said;
}

Proposal::Confidence confidenceOf(const QString &word)
{
    using enum Proposal::Confidence;

    if (word == u"CERTAIN"_s)
        return Certain;
    if (word == u"REPLACEMENT"_s)
        return Replacement;
    if (word == u"AMBIGUOUS"_s)
        return Ambiguous;
    if (word == u"UNKNOWN"_s)
        return Unknown;
    // Vocabulary is loose, structure is strict: the item stands and is shown by its
    // reason. A file that vanished from a list for being described too well would be the
    // worse answer.
    return Other;
}

} // namespace

Read<Proposal> proposal(const QJsonObject &from)
{
    Fields field(from);
    Proposal said;

    said.received = field.text(u"received"_s);
    said.name = field.text(u"name"_s);
    said.size = field.maybeBig(u"size"_s).value_or(-1);
    said.confidence = confidenceOf(field.text(u"confidence"_s));
    said.reason = field.text(u"reason"_s);
    if (said.size < 0)
        return refused<Proposal>(QStringLiteral("proposal"), QStringLiteral("size is missing"));
    if (!field.has(u"read"_s))
        return refused<Proposal>(QStringLiteral("proposal"), QStringLiteral("read is missing"));
    if (field.broken())
        return refused<Proposal>(QStringLiteral("proposal"), field.trouble());

    said.read = readingIn(field.object(u"read"_s));
    for (const QJsonValue &value : from.value(u"candidates"_s).toArray()) {
        Fields one(value.toObject());
        Candidate candidate;
        candidate.seriesId = one.text(u"seriesId"_s);
        candidate.name = one.text(u"name"_s);
        if (one.broken())
            return refused<Proposal>(QStringLiteral("proposal"), one.trouble());
        said.candidates << candidate;
    }
    said.replaces = field.maybeText(u"replaces"_s);
    said.concerns = field.words(u"concerns"_s);
    return {said, {}};
}

Read<Reserved> reserved(const QJsonObject &from)
{
    Fields field(from);
    Reserved said;

    said.id = field.text(u"id"_s);
    if (field.broken())
        return refused<Reserved>(QStringLiteral("reserved"), field.trouble());
    if (!field.has(u"proposal"_s)) {
        return refused<Reserved>(QStringLiteral("reserved"),
                                 QStringLiteral("proposal is missing"));
    }

    const Read<Proposal> inside = proposal(field.object(u"proposal"_s));
    if (!inside.ok())
        return refused<Reserved>(QStringLiteral("reserved"), inside.trouble);
    said.proposal = *inside.value;
    return {said, {}};
}

Read<Staged> staged(const QJsonObject &from)
{
    Fields field(from);
    Staged said;

    said.id = field.text(u"id"_s);
    said.name = field.text(u"name"_s);
    said.size = field.maybeBig(u"size"_s).value_or(-1);
    said.received = field.maybeBig(u"received"_s).value_or(-1);
    if (said.size < 0 || said.received < 0) {
        return refused<Staged>(QStringLiteral("staged"),
                               QStringLiteral("size and received are both required"));
    }
    if (field.broken())
        return refused<Staged>(QStringLiteral("staged"), field.trouble());
    return {said, {}};
}

Read<Waiting> waiting(const QJsonObject &from)
{
    Fields field(from);
    Waiting said;

    said.id = field.text(u"id"_s);
    said.name = field.text(u"name"_s);
    said.origin = field.text(u"origin"_s);
    said.size = field.maybeBig(u"size"_s).value_or(-1);
    said.lastTouchedAt = field.maybeBig(u"lastTouchedAt"_s).value_or(-1);
    said.received = field.maybeBig(u"received"_s).value_or(-1);
    const std::optional<bool> only = field.maybeBool(u"onlyCopy"_s);
    if (said.size < 0 || said.lastTouchedAt < 0 || said.received < 0 || !only.has_value()) {
        return refused<Waiting>(QStringLiteral("waiting"),
                                QStringLiteral("a required field is missing"));
    }
    if (field.broken())
        return refused<Waiting>(QStringLiteral("waiting"), field.trouble());
    said.onlyCopy = *only;
    return {said, {}};
}

Read<Filed> filed(const QJsonObject &from)
{
    Fields field(from);
    Filed said;

    said.entryId = field.text(u"entryId"_s);
    said.path = field.text(u"path"_s);
    const std::optional<bool> over = field.maybeBool(u"replacement"_s);
    if (!over.has_value()) {
        return refused<Filed>(QStringLiteral("filed"),
                              QStringLiteral("replacement is missing"));
    }
    if (field.broken())
        return refused<Filed>(QStringLiteral("filed"), field.trouble());

    said.replacement = *over;
    // Defaulted in the contract, so a server that leaves it out means false.
    said.renamed = field.maybeBool(u"renamed"_s).value_or(false);
    said.note = field.maybeText(u"note"_s);
    return {said, {}};
}

Read<Collision> collision(const QJsonObject &from)
{
    Fields field(from);
    Collision said;

    said.path = field.text(u"path"_s);
    said.wouldBecome = field.text(u"wouldBecome"_s);
    const std::optional<bool> same = field.maybeBool(u"sameVolume"_s);
    const std::optional<bool> identical = field.maybeBool(u"identical"_s);
    if (!same.has_value() || !identical.has_value() || !field.has(u"occupies"_s)
        || !field.has(u"arriving"_s)) {
        return refused<Collision>(QStringLiteral("collision"),
                                  QStringLiteral("a required field is missing"));
    }
    if (field.broken())
        return refused<Collision>(QStringLiteral("collision"), field.trouble());

    said.entryId = field.maybeText(u"entryId"_s);
    said.occupies = readingIn(field.object(u"occupies"_s));
    said.arriving = readingIn(field.object(u"arriving"_s));
    said.sameVolume = *same;
    said.agrees = field.words(u"agrees"_s);
    said.identical = *identical;
    return {said, {}};
}

Read<Opened> opened(const QJsonObject &from)
{
    Fields field(from);
    Opened said;

    said.id = field.text(u"id"_s);
    said.root = field.text(u"root"_s);
    said.bytesToSend = field.maybeBig(u"bytesToSend"_s).value_or(-1);
    if (said.bytesToSend < 0) {
        return refused<Opened>(QStringLiteral("opened"),
                               QStringLiteral("bytesToSend is missing"));
    }
    if (field.broken())
        return refused<Opened>(QStringLiteral("opened"), field.trouble());

    for (const QJsonValue &value : from.value(u"creates"_s).toArray()) {
        const QJsonObject one = value.toObject();
        Fields made(one);
        Creation creation;
        creation.kind = made.text(u"kind"_s);
        creation.name = made.text(u"name"_s);
        creation.at = made.text(u"at"_s);
        if (made.broken())
            return refused<Opened>(QStringLiteral("opened"), made.trouble());
        said.creates << creation;
    }
    for (const QJsonValue &value : from.value(u"moves"_s).toArray()) {
        const QJsonObject one = value.toObject();
        Fields filed(one);
        Relocation relocation;
        relocation.workId = filed.text(u"workId"_s);
        relocation.name = filed.text(u"name"_s);
        relocation.from = filed.text(u"from"_s);
        relocation.at = filed.text(u"at"_s);
        if (filed.broken())
            return refused<Opened>(QStringLiteral("opened"), filed.trouble());
        said.moves << relocation;
    }
    for (const QJsonValue &value : from.value(u"replaces"_s).toArray()) {
        const QJsonObject one = value.toObject();
        Fields held(one);
        Replacement replacement;
        replacement.path = held.text(u"path"_s);
        replacement.size = held.maybeBig(u"size"_s).value_or(0);
        replacement.presentSize = held.maybeBig(u"presentSize"_s).value_or(0);
        replacement.presentTitle = one.value(u"presentTitle"_s).toString();
        if (one.value(u"presentNumber"_s).isDouble())
            replacement.presentNumber = one.value(u"presentNumber"_s).toDouble();
        replacement.presentRead = one.value(u"presentRead"_s).toBool();
        if (held.broken())
            return refused<Opened>(QStringLiteral("opened"), held.trouble());
        said.replaces << replacement;
    }
    for (const QJsonValue &value : from.value(u"declarations"_s).toArray()) {
        const QJsonObject one = value.toObject();
        Fields said_(one);
        Declaration declaration;
        declaration.path = said_.text(u"path"_s);
        declaration.presentName = one.value(u"presentName"_s).toString();
        declaration.differs = said_.words(u"differs"_s);
        if (said_.broken())
            return refused<Opened>(QStringLiteral("opened"), said_.trouble());
        said.declarations << declaration;
    }
    said.toSend = field.words(u"toSend"_s);
    said.alreadyThere = field.words(u"alreadyThere"_s);
    return {said, {}};
}

Read<Received> received(const QJsonObject &from)
{
    Fields field(from);
    Received said;

    said.path = field.text(u"path"_s);
    said.received = field.maybeBig(u"received"_s).value_or(-1);
    if (said.received < 0) {
        return refused<Received>(QStringLiteral("received"),
                                 QStringLiteral("received is missing"));
    }
    if (field.broken())
        return refused<Received>(QStringLiteral("received"), field.trouble());
    return {said, {}};
}

Read<BadOffset> badOffset(const QJsonObject &from)
{
    Fields field(from);
    BadOffset said;

    said.error = field.text(u"error"_s);
    said.received = field.maybeBig(u"received"_s).value_or(-1);
    if (said.received < 0) {
        return refused<BadOffset>(QStringLiteral("badOffset"),
                                  QStringLiteral("received is missing"));
    }
    if (field.broken())
        return refused<BadOffset>(QStringLiteral("badOffset"), field.trouble());
    return {said, {}};
}

Read<Session> session(const QJsonObject &from)
{
    Fields field(from);
    Session said;

    said.id = field.text(u"id"_s);
    said.root = field.text(u"root"_s);
    if (!field.has(u"received"_s)) {
        return refused<Session>(QStringLiteral("session"),
                                QStringLiteral("received is missing"));
    }
    if (field.broken())
        return refused<Session>(QStringLiteral("session"), field.trouble());

    const QJsonObject held = field.object(u"received"_s);
    for (auto one = held.constBegin(); one != held.constEnd(); ++one) {
        // A count that is not a number is not a count. Read as zero it would send a whole
        // volume again; refused, it says which path the server described badly.
        if (!one.value().isDouble()) {
            return refused<Session>(QStringLiteral("session"),
                                    one.key() + QStringLiteral(" is not a number"));
        }
        said.received.insert(one.key(), one.value().toInteger());
    }
    said.missing = field.words(u"missing"_s);
    return {said, {}};
}

Read<Installed> installed(const QJsonObject &from)
{
    Fields field(from);
    Installed said;

    said.root = field.text(u"root"_s);
    said.installed = field.whole(u"installed"_s);
    if (!field.has(u"orphans"_s)) {
        return refused<Installed>(QStringLiteral("installed"),
                                  QStringLiteral("orphans is missing"));
    }
    if (field.broken())
        return refused<Installed>(QStringLiteral("installed"), field.trouble());

    said.orphans = field.words(u"orphans"_s);
    said.corrupt = field.words(u"corrupt"_s);
    said.pending = field.words(u"pending"_s);
    // Defaulted in the contract: a server that leaves it out closed the session.
    said.open = field.maybeBool(u"open"_s).value_or(false);
    said.moved = field.words(u"moved"_s);
    return {said, {}};
}

} // namespace Api
