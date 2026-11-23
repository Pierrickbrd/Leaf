#include "Aimed.h"

#include "Fold.h"
#include "Words.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;
using Api::Outcome;

namespace Aimed {

namespace {

// A group crosses to the screen as the number `Review::Filter` holds, and a filter comes back
// as one: the list is built of those numbers.
constexpr int All = std::to_underlying(Group::All);
constexpr int ToReview = std::to_underlying(Group::ToReview);
constexpr int Ready = std::to_underlying(Group::Ready);
constexpr int Identical = std::to_underlying(Group::Identical);

bool givenTheAimedNumber(const Card &card, const QString &path);

/// One line of the list: a file the server compared, or a volume the drop does not bring.
struct Line {
    QString id;
    const Api::VolumeMatch *match = nullptr;
    const Api::HeldVolume *absent = nullptr;
};

QString idOfAbsent(const Api::HeldVolume &held)
{
    return u"absent:"_s + held.entryId;
}

QList<Line> linesOf(const Card &card)
{
    QList<Line> out;
    for (const Api::VolumeMatch &one : card.road.volumes)
        out << Line{one.path, &one, nullptr};
    for (const Api::HeldVolume &one : card.road.absent)
        out << Line{idOfAbsent(one), nullptr, &one};
    return out;
}

/// `Same` is identical and `Add` needs nobody's eye; everything else — a replacement, a file
/// that is another volume or another series, a volume left behind — is for the reader.
int groupOf(const Line &line)
{
    if (!line.match)
        return ToReview;
    switch (line.match->outcome) {
    case Outcome::Same:
        return Identical;
    case Outcome::Add:
        return Ready;
    default:
        return ToReview;
    }
}

QString numberText(double number)
{
    return QString::number(number, 'g', 10).replace(u'.', u',');
}

QString numberText(const std::optional<double> &number)
{
    return number ? numberText(*number) : QString();
}

/// The step of the way down that is a work, an edition, or the target itself.
const Api::TrailStep *stepOf(const Card &card, const QString &kind)
{
    if (!card.road.place)
        return nullptr;
    for (const Api::TrailStep &one : card.road.place->trail) {
        if (one.kind == kind)
            return &one;
    }
    return nullptr;
}

bool onAChapter(const Card &card)
{
    return card.road.target && card.road.target->kind == u"CHAPTER"_s;
}

/// The volume aimed at — or, for a chapter, the one it is a chapter of.
QString aimedEntryId(const Card &card)
{
    if (!card.road.target)
        return {};
    if (onAChapter(card)) {
        const Api::TrailStep *volume = stepOf(card, u"ENTRY"_s);
        return volume ? volume->id : QString();
    }
    return card.road.target->id;
}

const Api::HeldVolume *aimedVolume(const Card &card)
{
    if (!card.road.target)
        return nullptr;
    const QString entry = aimedEntryId(card);
    for (const Api::VolumeMatch &one : card.road.volumes) {
        if (one.entryId == entry && one.present)
            return &*one.present;
    }
    for (const Api::HeldVolume &one : card.road.absent) {
        if (one.entryId == entry)
            return &one;
    }
    return nullptr;
}

QString volumeNumberText(const Card &card)
{
    if (onAChapter(card)) {
        if (const Api::TrailStep *volume = stepOf(card, u"ENTRY"_s))
            return volume->name;
    }
    if (const Api::HeldVolume *held = aimedVolume(card); held && held->number.has_value())
        return numberText(*held->number);
    // The last step is the volume itself, and its name is the best the answer gave.
    if (card.road.place && !card.road.place->trail.isEmpty())
        return card.road.place->trail.constLast().name;
    return card.road.target ? card.road.target->name : QString();
}

QString chipOf(const Line &line, int &tone)
{
    using Words::Aim;
    using enum Card::Tone;
    if (!line.match) {
        tone = std::to_underlying(Quiet);
        return Words::aimChip(Aim::Keep);
    }
    switch (line.match->outcome) {
    case Outcome::Replace:
        tone = std::to_underlying(Moving);
        return Words::aimChip(Aim::Replace);
    case Outcome::Update:
        tone = std::to_underlying(Moving);
        return Words::aimChip(Aim::Update);
    case Outcome::Add:
        tone = std::to_underlying(Ordinary);
        return Words::aimChip(Aim::Add);
    case Outcome::OtherNumber:
        tone = std::to_underlying(Attention);
        return Words::aimChip(Aim::OtherNumber);
    case Outcome::OtherSeries:
        tone = std::to_underlying(Attention);
        return Words::aimChip(Aim::OtherSeries);
    case Outcome::Unnumbered:
        tone = std::to_underlying(Attention);
        return Words::aimChip(Aim::Unnumbered);
    case Outcome::Twice:
        tone = std::to_underlying(Attention);
        return Words::aimChip(Aim::Twice);
    case Outcome::Same:
    case Outcome::Other:
        break;
    }
    tone = std::to_underlying(Quiet);
    return Words::aimChip(Aim::Same);
}

bool switchable(const Line &line)
{
    if (!line.match)
        return false;
    using enum Api::Outcome;
    const Outcome outcome = line.match->outcome;
    return outcome == Replace || outcome == Update || outcome == OtherSeries;
}

QString seriesSaid(const Card &card, const QString &path)
{
    // What the file names for itself sits in the sheet of its own, composed from its
    // `entry.json` or its ComicInfo.
    for (const Facts::Sheet &sheet : card.road.sheets) {
        if (sheet.at == path)
            return sheet.contents.series;
    }
    return {};
}

QString titleOf(const Line &line)
{
    if (line.absent)
        return Words::volumeNumbered(numberText(line.absent->number));
    std::optional<double> number = line.match->number;
    if (!number.has_value() && line.match->present)
        number = line.match->present->number;
    return number ? Words::volumeNumbered(numberText(*number)) : line.match->path;
}

QString detailOf(const Card &card, const Line &line)
{
    if (line.absent)
        return Words::absentFromTheDrop();
    const Api::VolumeMatch &match = *line.match;
    const int after = match.arriving.pages.value_or(-1);
    using enum Api::Outcome;
    switch (match.outcome) {
    case Same:
        return Words::sameAsHeld();
    case Add:
        return Words::newVolume(std::max(0, after));
    case OtherNumber:
        return Words::comicInfoSays(numberText(match.number), std::max(0, after));
    case OtherSeries:
        return Words::saysItBelongsTo(seriesSaid(card, match.path));
    case Unnumbered:
        return Words::withoutNumber();
    case Twice:
        return Words::numberTwice();
    case Replace:
    case Update:
    case Other:
        break;
    }
    QStringList parts;
    if (match.present && match.differs.contains(u"pages"_s) && after >= 0) {
        parts << Words::pagesGoFromTo(match.present->pages, after,
                                      match.present->page.has_value()
                                          && !card.road.resetting.contains(match.path));
    } else if (match.differs.contains(u"title"_s) && match.arriving.title) {
        parts << (match.present && !match.present->title.isEmpty()
                      ? Words::titleChanged(*match.arriving.title)
                      : Words::titleAdded(*match.arriving.title));
    }
    if (match.differs.contains(u"chapters"_s))
        parts << Words::chaptersChanged();
    return Words::joinDetails(parts);
}

QVariantMap lineOf(const Card &card, const Line &line, const QString &focus)
{
    int tone = 0;
    const QString chip = chipOf(line, tone);
    return QVariantMap{{u"header"_s, false},
                       {u"id"_s, line.id},
                       {u"title"_s, titleOf(line)},
                       // The key the frame's other lines are read by: one list, one delegate.
                       {u"name"_s, titleOf(line)},
                       {u"detail"_s, detailOf(card, line)},
                       {u"alarming"_s, false},
                       {u"chip"_s, chip},
                       {u"chipTone"_s, tone},
                       {u"origin"_s, tone == std::to_underlying(Card::Tone::Attention)
                                         ? std::to_underlying(Facts::Origin::Missing)
                                         : std::to_underlying(Facts::Origin::Said)},
                       {u"group"_s, groupOf(line)},
                       {u"switchable"_s, switchable(line)},
                       {u"on"_s, line.match && card.road.replacing.contains(line.match->path)},
                       {u"checkable"_s, false},
                       {u"checked"_s, false},
                       {u"selected"_s, line.id == focus}};
}

bool matches(const Line &line, const Card &card, const QString &searchKey)
{
    if (searchKey.isEmpty())
        return true;
    return Fold::searchKey(titleOf(line) + u' ' + detailOf(card, line)).contains(searchKey);
}

QList<Line> shown(const Card &card, const QString &searchKey)
{
    QList<Line> out;
    for (const Line &one : linesOf(card)) {
        if (matches(one, card, searchKey))
            out << one;
    }
    return out;
}

QString show(const QJsonValue &value)
{
    if (value.isString())
        return value.toString();
    if (value.isArray()) {
        QStringList words;
        for (const QJsonValue &one : value.toArray())
            words << show(one);
        return words.join(u", "_s);
    }
    if (value.isBool())
        return value.toBool() ? u"oui"_s : u"non"_s;
    if (value.isDouble())
        return numberText(value.toDouble());
    return {};
}

QVariantMap sideRow(const QString &label, const QString &value, bool differs)
{
    return QVariantMap{{u"label"_s, label}, {u"value"_s, value}, {u"differs"_s, differs}};
}

QString chaptersText(const QList<double> &chapters)
{
    if (chapters.isEmpty())
        return numberText(0.0);
    return Words::chaptersSpan(int(chapters.size()), numberText(chapters.constFirst()),
                               numberText(chapters.constLast()));
}

/// The volume a file would take the place of: the one the server matched it with, or, for a
/// file of another number laid on a volume and sent there anyway, the volume aimed at — which
/// the answer lists among the ones the drop does not bring, so the left side was drawn empty
/// over a replacement of 188 pages by 200.
std::optional<Api::HeldVolume> replacedBy(const Card &card, const Api::VolumeMatch &match)
{
    if (match.present)
        return match.present;
    if (!card.road.target || card.road.target->kind != u"ENTRY"_s)
        return std::nullopt;
    for (const Api::HeldVolume &one : card.road.absent) {
        if (one.entryId == card.road.target->id)
            return one;
    }
    return std::nullopt;
}

/// The two sides of a comparison between the volume held and the file that would take its
/// place; each value that differs is marked, and the ComicInfo that will be dropped is said.
QVariantMap compareOf(const Card &card, const Api::VolumeMatch &match)
{
    QVariantList left;
    QVariantList right;
    const bool pages = match.differs.contains(u"pages"_s);
    const bool chapters = match.differs.contains(u"chapters"_s);
    if (const auto present = replacedBy(card, match)) {
        left << sideRow(Words::pagesLabel(), QString::number(present->pages), pages);
        left << sideRow(Words::chaptersLabel(), chaptersText(present->chapters), chapters);
        if (present->page.has_value())
            left << sideRow(Words::progressLabel(), Words::pageNumbered(*present->page + 1),
                            false);
    }
    if (match.arriving.pages.has_value())
        right << sideRow(Words::pagesLabel(), QString::number(*match.arriving.pages), pages);
    right << sideRow(Words::chaptersLabel(), chaptersText(match.arriving.chapters), chapters);
    for (const Facts::Sheet &sheet : card.road.sheets) {
        if (sheet.at == match.path && sheet.comicInfo.carried)
            right << sideRow(Words::comicInfoLabel(), Words::comicInfoRemoved(), false);
    }
    return QVariantMap{
        {u"left"_s, QVariantMap{{u"title"_s, Words::heldSide()}, {u"rows"_s, left}}},
        {u"right"_s, QVariantMap{{u"title"_s, Words::arrivingSide()}, {u"rows"_s, right}}}};
}

QVariantMap progressOf(const Card &card, const Api::VolumeMatch &match)
{
    const auto present = replacedBy(card, match);
    const bool placed = present && present->page.has_value();
    const bool far = placed && match.arriving.pages.has_value()
                     && std::abs(present->pages - *match.arriving.pages) >= MostPagesStill;
    return QVariantMap{
        {u"text"_s, placed ? Words::progressStays(numberText(present->number)) : QString()},
        {u"ask"_s, far},
        {u"reset"_s, card.road.resetting.contains(match.path)},
        {u"keepLabel"_s, Words::keepProgress()},
        {u"resetLabel"_s, Words::resetProgress()}};
}

QVariantMap emptyProgress()
{
    return QVariantMap{{u"text"_s, QString()},
                       {u"ask"_s, false},
                       {u"reset"_s, false},
                       {u"keepLabel"_s, QString()},
                       {u"resetLabel"_s, QString()}};
}

/// A value of the declaration the drop would write, by sidecar and key.
QString arrivingValue(const Facts::Composed &composed, const QString &path, const QString &key)
{
    for (const Manifest::Sidecar &one : composed.sidecars) {
        if (one.path == path)
            return show(QJsonDocument::fromJson(one.json).object().value(key));
    }
    return {};
}

QVariantMap seriesSheet(const Card &card, const Facts::Composed &composed)
{
    QVariantList fields;
    int changed = 0;
    for (const Api::Declaration &declaration : card.road.declarations) {
        for (const QString &key : declaration.differs) {
            // The orders are a list of their own, not a value to put side by side: they are
            // said in the note.
            if (key == u"orders"_s || key == u"defaultOrder"_s)
                continue;
            const bool held = declaration.present.contains(key);
            changed += card.road.keeping.contains(declaration.path + u'\n' + key) ? 0 : 1;
            fields << QVariantMap{
                {u"id"_s, declaration.path},
                {u"key"_s, key},
                {u"label"_s, Words::fieldLabel(key, Manifest::Level::Work)},
                {u"present"_s, held ? show(declaration.present.value(key)) : Words::notDeclared()},
                {u"arriving"_s, arrivingValue(composed, declaration.path, key)},
                {u"keep"_s, card.road.keeping.contains(declaration.path + u'\n' + key)}};
        }
    }
    return QVariantMap{{u"id"_s, QString()},
                       {u"title"_s, title(card)},
                       {u"subtitle"_s, fields.isEmpty() ? subtitle(card)
                                                        : Words::infosChanged(changed)},
                       {u"fields"_s, fields}};
}

} // namespace

namespace {

// ——— A chapter aimed at: the pages of a source taken in place of its pages —————————————

/// The chapter the drop replaces, among the volume's: the one aimed at.
const Api::ChapterSplice *aimedChapter(const Card &card)
{
    if (!card.road.spliced || !card.road.target)
        return nullptr;
    for (const Api::ChapterSplice &one : card.road.spliced->chapters) {
        if (one.id == card.road.target->id)
            return &one;
    }
    for (const Api::ChapterSplice &one : card.road.spliced->chapters) {
        if (one.outcome == u"REPLACE"_s)
            return &one;
    }
    return nullptr;
}

QString chapterNameOf(const Api::ChapterSplice &chapter)
{
    return Words::chapterListed(numberText(chapter.number), chapter.title.value_or(QString()));
}

/// The pages of the source that become the chapter, from the reader's range or the whole
/// file, counted from 0.
Card::Road::Splice sourceRange(const Card &card)
{
    if (card.road.splice)
        return *card.road.splice;
    const int pages = card.road.sheets.isEmpty() ? 0 : card.road.sheets.constFirst().contents.pages;
    return {0, std::max(0, pages - 1)};
}

QVariantList chapterRows(const Card &card, const QString &focus)
{
    QVariantList out;
    if (!card.road.spliced)
        return out;
    out << QVariantMap{{u"header"_s, true},
                       {u"group"_s, ToReview},
                       {u"text"_s, Words::chaptersOfVolume(volumeNumberText(card))},
                       {u"action"_s, QString()}};
    for (const Api::ChapterSplice &one : card.road.spliced->chapters) {
        const bool replace = one.outcome == u"REPLACE"_s;
        const bool shift = one.outcome == u"SHIFT"_s;
        const QString name = chapterNameOf(one);
        QString chip = Words::chapterKept();
        Card::Tone tone = Card::Tone::Quiet;
        if (replace) {
            chip = Words::chapterReplaced();
            tone = Card::Tone::Moving;
        } else if (shift) {
            chip = Words::chapterShifted(std::abs(one.becomesFrom - one.from));
            tone = Card::Tone::Ordinary;
        }
        out << QVariantMap{
            {u"header"_s, false},
            {u"id"_s, one.id},
            {u"title"_s, name},
            {u"name"_s, name},
            {u"detail"_s, replace || shift
                              ? Words::pagesRangeChange(one.from + 1, one.to + 1,
                                                        one.becomesFrom + 1, one.becomesTo + 1)
                              : Words::pagesRange(one.from + 1, one.to + 1)},
            {u"alarming"_s, false},
            {u"chip"_s, chip},
            {u"chipTone"_s, std::to_underlying(tone)},
            {u"origin"_s, std::to_underlying(Facts::Origin::Said)},
            {u"group"_s, ToReview},
            {u"switchable"_s, false},
            {u"on"_s, false},
            {u"checkable"_s, false},
            {u"checked"_s, false},
            {u"selected"_s, focus.isEmpty() ? one.id == card.road.target->id : one.id == focus}};
    }
    return out;
}

/// What the splice does to the reader's place, as a sentence — or nothing, for a reader who
/// has none in the volume.
QString progressSentence(const Card &card, const Api::SpliceOutcome &spliced)
{
    if (!spliced.page.has_value() || !spliced.pageBecomes.has_value())
        return {};
    const Api::ChapterSplice *chapter = aimedChapter(card);
    if (const bool inside =
            chapter && *spliced.page >= chapter->from && *spliced.page <= chapter->to;
        inside && *spliced.page == *spliced.pageBecomes)
        return Words::spliceProgressKept(*spliced.page + 1, numberText(chapter->number));
    if (*spliced.page == *spliced.pageBecomes)
        return {};
    return Words::spliceProgressBrought(*spliced.page + 1, *spliced.pageBecomes + 1);
}

QVariantMap chapterSheet(const Card &card)
{
    const QString volume = volumeNumberText(card);
    QVariantMap out{{u"id"_s, QString()},
                    {u"title"_s, Words::replaceChapterTitle(volume)},
                    {u"subtitle"_s, subtitle(card)},
                    {u"fields"_s, QVariantList{}},
                    {u"compare"_s, QVariantMap{}},
                    {u"choices"_s, QVariantList{}},
                    {u"progress"_s, emptyProgress()}};
    const Api::ChapterSplice *chapter = aimedChapter(card);
    if (!chapter || !card.road.spliced)
        return out;
    const Api::SpliceOutcome &spliced = *card.road.spliced;
    const Card::Road::Splice taken = sourceRange(card);
    const int total = card.road.sheets.isEmpty() ? 0 : card.road.sheets.constFirst().contents.pages;
    // The pages a reader sees around the range: two before it, its first two, a gap, its last
    // two, one after. Page numbers are the ones on screen, counted from 1.
    QVariantList around;
    const auto page = [&](int at) {
        if (at < 0 || (total > 0 && at >= total))
            return;
        around << QVariantMap{{u"page"_s, at + 1},
                              {u"gap"_s, false},
                              {u"inside"_s, at >= taken.from && at <= taken.to}};
    };
    page(taken.from - 2);
    page(taken.from - 1);
    page(taken.from);
    if (taken.to - taken.from > 3) {
        page(taken.from + 1);
        around << QVariantMap{{u"page"_s, 0}, {u"gap"_s, true}, {u"inside"_s, true}};
        page(taken.to - 1);
    } else {
        for (int at = taken.from + 1; at < taken.to; ++at)
            page(at);
    }
    if (taken.to > taken.from)
        page(taken.to);
    page(taken.to + 1);
    QString note = Words::spliceVolumeNote(volume, spliced.pagesBefore, spliced.pagesAfter);
    if (const QString progress = progressSentence(card, spliced); !progress.isEmpty())
        note += u' ' + progress;
    out.insert(u"splice"_s,
               QVariantMap{{u"id"_s, chapter->id},
                           {u"name"_s, chapterNameOf(*chapter)},
                           {u"current"_s, QVariantMap{{u"from"_s, chapter->from + 1},
                                                      {u"to"_s, chapter->to + 1},
                                                      {u"total"_s, spliced.pagesBefore}}},
                           {u"source"_s, QVariantMap{{u"from"_s, taken.from + 1},
                                                     {u"to"_s, taken.to + 1},
                                                     {u"total"_s, total}}},
                           {u"extra"_s, Words::spliceExtra(spliced.pagesAfter - spliced.pagesBefore)},
                           {u"around"_s, around},
                           {u"note"_s, note}});
    return out;
}

// ——— A universe aimed at: its series, by what becomes of each —————————————————————————

bool onAUniverse(const Card &card)
{
    return card.road.target && card.road.target->kind == u"UNIVERSE"_s;
}

/// One series of an universe's drop: the folder of its files, what the server found of them,
/// and whether it moves, is created, or is held and absent from the drop.
struct SeriesLine {
    QString id;
    QString name;
    int held = 0;
    int arriving = 0;
    int changed = 0;
    int added = 0;
    const Api::Relocation *move = nullptr;
    const Api::Creation *create = nullptr;
    const Api::AbsentSeries *absent = nullptr;
};

QList<SeriesLine> seriesLines(const Card &card)
{
    QList<SeriesLine> out;
    const auto lineFor = [&out](const QString &name) -> SeriesLine & {
        for (SeriesLine &one : out) {
            if (!one.absent && one.name == name)
                return one;
        }
        out.append(SeriesLine{name, name});
        return out.last();
    };
    for (const Api::VolumeMatch &one : card.road.volumes) {
        if (!one.path.contains(u'/'))
            continue;
        SeriesLine &line = lineFor(one.path.section(u'/', 0, 0));
        ++line.arriving;
        if (one.outcome == Outcome::Add)
            ++line.added;
        else if (one.outcome == Outcome::Same)
            ++line.held;
        else if (one.outcome == Outcome::Replace || one.outcome == Outcome::Update)
            ++line.held, ++line.changed;
    }
    for (const Api::Relocation &one : card.road.moves)
        lineFor(one.at).move = &one;
    for (const Api::Creation &one : card.road.creates) {
        if (one.kind == u"WORK"_s && !one.at.isEmpty())
            lineFor(one.at).create = &one;
    }
    for (const Api::AbsentSeries &one : card.road.absentSeries) {
        SeriesLine line{u"absent:"_s + one.workId, one.name};
        line.absent = &one;
        out.append(line);
    }
    return out;
}

int seriesGroup(const SeriesLine &line)
{
    if (line.absent)
        return Identical;
    if (line.move)
        return ToReview;
    if (line.create)
        return Ready;
    if (line.changed > 0)
        return ToReview;
    return line.added > 0 ? Ready : Identical;
}

QString seriesDetail(const SeriesLine &line)
{
    if (line.absent)
        return Words::absentSeriesDetail(line.absent->volumes);
    if (line.move)
        return Words::moveDetail(line.move->from, line.held, line.arriving);
    if (line.create)
        return Words::newSeriesDetail(line.arriving);
    if (line.changed == 0 && line.added == 0)
        return Words::identicalSeriesDetail(line.arriving);
    return Words::changedSeriesDetail(line.changed, line.added);
}

QVariantMap seriesLineMap(const SeriesLine &line, const QString &focus)
{
    QString chip = Words::seriesChangedChip();
    Card::Tone tone = Card::Tone::Moving;
    if (line.absent) {
        chip = Words::seriesKeepChip();
        tone = Card::Tone::Quiet;
    } else if (line.move) {
        chip = Words::seriesMoveChip();
    } else if (line.create || (line.changed == 0 && line.added > 0)) {
        chip = Words::seriesNewChip();
        tone = Card::Tone::Ordinary;
    } else if (line.changed == 0) {
        chip = Words::seriesIdenticalChip();
        tone = Card::Tone::Quiet;
    }
    return QVariantMap{{u"header"_s, false},
                       {u"id"_s, line.id},
                       {u"title"_s, line.name},
                       {u"name"_s, line.name},
                       {u"detail"_s, seriesDetail(line)},
                       {u"alarming"_s, false},
                       {u"chip"_s, chip},
                       {u"chipTone"_s, std::to_underlying(tone)},
                       {u"origin"_s, std::to_underlying(Facts::Origin::Said)},
                       {u"group"_s, seriesGroup(line)},
                       {u"switchable"_s, false},
                       {u"on"_s, false},
                       {u"checkable"_s, false},
                       {u"checked"_s, false},
                       {u"selected"_s, line.id == focus}};
}

QVariantMap seriesSheetOf(const Card &card, const SeriesLine &line)
{
    QVariantMap out{{u"id"_s, line.id},
                    {u"title"_s, line.name},
                    {u"subtitle"_s, seriesDetail(line)},
                    {u"fields"_s, QVariantList{}},
                    {u"compare"_s, QVariantMap{}},
                    {u"alarming"_s, QVariantMap{}},
                    {u"choices"_s, QVariantList{}},
                    {u"progress"_s, emptyProgress()}};
    if (!line.move)
        return out;
    const bool filed = card.road.filing.contains(line.move->workId);
    out.insert(u"alarming"_s, QVariantMap{{u"title"_s, Words::existsAlreadyAt(line.move->from)},
                                          {u"text"_s, Words::moveItWithReadings()}});
    out.insert(u"choices"_s,
               QVariantList{QVariantMap{{u"id"_s, u"move"_s},
                                        {u"label"_s, Words::moveIntoUniverse()},
                                        {u"badge"_s, Words::recommended()},
                                        {u"chosen"_s, filed},
                                        {u"detail"_s, Words::moveKeepsDetail(line.held,
                                                                              line.added)}},
                            QVariantMap{{u"id"_s, u"second"_s},
                                        {u"label"_s, Words::createSecondSeries()},
                                        {u"badge"_s, QString()},
                                        {u"chosen"_s, !filed},
                                        {u"detail"_s, Words::existingStaysPut()}}});
    return out;
}

/// The line heading a group of `size` lines, and what its button says: the identical ones
/// fold, unless they are all the filter shows.
QVariantMap groupHeader(int group, int filter, bool showIdentical, int size)
{
    QVariantMap header{{u"header"_s, true}, {u"group"_s, group}};
    if (group != Identical) {
        header.insert(u"text"_s, Words::reviewGroup(Words::Reviewed(group), size));
        header.insert(u"action"_s, QString());
        return header;
    }
    header.insert(u"text"_s, Words::identicalNothingToDo(size));
    if (filter == Identical)
        header.insert(u"action"_s, QString());
    else
        header.insert(u"action"_s, showIdentical ? Words::hideIt() : Words::showIt());
    return header;
}

QVariantList universeRows(const Card &card, int filter, bool showIdentical, const QString &focus)
{
    QVariantList out;
    const QList<SeriesLine> lines = seriesLines(card);
    for (const int group : {ToReview, Ready, Identical}) {
        if (filter != All && filter != group)
            continue;
        QList<SeriesLine> inGroup;
        for (const SeriesLine &one : lines) {
            if (seriesGroup(one) == group)
                inGroup << one;
        }
        if (inGroup.isEmpty())
            continue;
        const bool open = group != Identical || showIdentical || filter == Identical;
        out << groupHeader(group, filter, showIdentical, int(inGroup.size()));
        if (!open)
            continue;
        for (const SeriesLine &one : inGroup)
            out << seriesLineMap(one, focus);
    }
    return out;
}

} // namespace

bool onAVolume(const Card &card)
{
    return card.road.target
           && (card.road.target->kind == u"ENTRY"_s || card.road.target->kind == u"CHAPTER"_s);
}

std::optional<double> aimedNumber(const Card &card)
{
    const Api::HeldVolume *held = aimedVolume(card);
    return held ? held->number : std::nullopt;
}

std::optional<double> ownNumber(const Card &card, const QString &path)
{
    for (const Api::VolumeMatch &one : card.road.volumes) {
        if (one.path == path)
            return one.number;
    }
    return std::nullopt;
}

QString title(const Card &card)
{
    if (card.road.target && card.road.target->kind == u"SERIES_NEW_EDITIONS"_s)
        return Words::addEditionsTitle(card.road.target->name);
    if (onAChapter(card))
        return Words::replaceChapterTitle(volumeNumberText(card));
    if (onAVolume(card))
        return Words::reimportVolume(volumeNumberText(card));
    if (const Api::TrailStep *work = stepOf(card, u"WORK"_s))
        return Words::reimportTitle(work->name);
    return Words::reimportTitle(card.road.target ? card.road.target->name : card.name);
}

QString subtitle(const Card &card)
{
    if (card.road.target && card.road.target->kind == u"SERIES_NEW_EDITIONS"_s)
        return Words::addEditionsSubtitle();
    if (onAUniverse(card)) {
        const Api::TrailStep *universe = stepOf(card, u"UNIVERSE"_s);
        int arriving = 0;
        for (const SeriesLine &one : seriesLines(card))
            arriving += one.absent ? 0 : 1;
        return Words::universeSubtitle(universe ? universe->count : 0, arriving);
    }
    const Api::TrailStep *work = stepOf(card, u"WORK"_s);
    const Api::TrailStep *edition = stepOf(card, u"SERIES"_s);
    if (onAVolume(card) && work && edition)
        return Words::reimportVolumeUnder(work->name, edition->name);
    if (card.road.target && card.road.target->kind == u"SERIES"_s && edition)
        return Words::reimportSeriesHolds(edition->name, edition->count);
    return {};
}

QVariantList rail(const Card &card)
{
    QVariantList out;
    if (!card.road.place)
        return out;
    // A chapter is drawn among the others of its volume, under it: the way down ends at the
    // volume, and the chapters are the leaves.
    const bool chapters = onAChapter(card) && card.road.spliced;
    const qsizetype last = card.road.place->trail.size() - (chapters ? 2 : 1);
    for (qsizetype at = 0; at <= last; ++at) {
        const Api::TrailStep &step = card.road.place->trail.at(at);
        out << QVariantMap{{u"id"_s, step.id},
                           {u"level"_s, -1},
                           {u"name"_s, step.name},
                           {u"depth"_s, int(at)},
                           {u"count"_s, step.count},
                           {u"expandable"_s, false},
                           {u"expanded"_s, false},
                           {u"selected"_s, !chapters && at == last},
                           {u"role"_s, std::to_underlying(Shape::Role::Shelf)},
                           {u"roleLabel"_s, QString()},
                           {u"roleTone"_s, std::to_underlying(Card::Tone::Quiet)},
                           {u"changeable"_s, false},
                           {u"rolesGreyed"_s, false},
                           {u"roleSpoken"_s, QString()},
                           {u"unread"_s, false}};
    }
    if (chapters) {
        for (const Api::ChapterSplice &one : card.road.spliced->chapters) {
            out << QVariantMap{{u"id"_s, one.id},
                               {u"level"_s, -1},
                               {u"name"_s, chapterNameOf(one)},
                               {u"depth"_s, int(last) + 1},
                               {u"count"_s, 0},
                               {u"expandable"_s, false},
                               {u"expanded"_s, false},
                               {u"selected"_s, one.id == card.road.target->id},
                               {u"role"_s, std::to_underlying(Shape::Role::Shelf)},
                               {u"roleLabel"_s, QString()},
                               {u"roleTone"_s, std::to_underlying(Card::Tone::Quiet)},
                               {u"changeable"_s, false},
                               {u"rolesGreyed"_s, false},
                               {u"roleSpoken"_s, QString()},
                               {u"unread"_s, false}};
        }
    }
    return out;
}

QVariantList editionRows(const Card &card)
{
    // One row for each path that was ever asked a name, in the order they appeared: a name
    // accepted is no longer asked for, and its row stays to be corrected.
    QVariantList out;
    for (const QString &path : card.road.editionOrder) {
        const auto asked = std::ranges::find(card.road.editionsNeedingAName, path,
                                             &Api::NeedsAName::path);
        const bool current = path == u"@current"_s
                             || (asked != card.road.editionsNeedingAName.cend() && asked->current);
        QString label;
        if (current)
            label = Words::currentEdition();
        else if (path.isEmpty())
            label = Words::unnamedEdition();
        else
            label = QFileInfo(path).fileName();
        QString problem;
        for (const Api::NameProblem &bad : card.road.problems) {
            if (bad.path != path)
                continue;
            problem = bad.kind == u"DUPLICATE"_s ? Words::editionNameTaken(bad.with)
                                                 : Words::editionNameMissing();
        }
        out.append(QVariantMap{{u"path"_s, path},
                               {u"label"_s, label},
                               {u"name"_s, card.road.editionNames.value(path)},
                               {u"problem"_s, problem}});
    }
    // A name refused for being taken, by an edition the server never asked a name of: its
    // row is made, so that it can be corrected.
    for (const Api::NameProblem &bad : card.road.problems) {
        if (const bool listed = std::ranges::any_of(out, [&bad](const QVariant &row) {
                return row.toMap().value(u"path"_s).toString() == bad.path;
            });
            listed || bad.kind != u"DUPLICATE"_s)
            continue;
        out.append(QVariantMap{{u"path"_s, bad.path},
                               {u"label"_s, bad.path.isEmpty() ? Words::unnamedEdition()
                                                               : QFileInfo(bad.path).fileName()},
                               {u"name"_s, card.road.editionNames.value(bad.path)},
                               {u"problem"_s, Words::editionNameTaken(bad.with)}});
    }
    return out;
}

QString railNote(const Card &card)
{
    if (onAUniverse(card))
        return Words::universeRailNote();
    if (onAVolume(card))
        return Words::aimedRailNoteVolume();
    return card.road.resetting.isEmpty() ? Words::aimedRailNoteSeries() : QString();
}

QString note(const Card &card)
{
    if (onAUniverse(card)) {
        const bool orders = std::ranges::any_of(
            card.road.declarations, [](const Api::Declaration &one) {
                return one.path == u"universe.json"_s && one.differs.contains(u"orders"_s);
            });
        return orders ? Words::universeOrdersNote() : QString();
    }
    if (onAChapter(card)) {
        return card.road.sheets.isEmpty()
            ? QString()
            : Words::spliceSource(QFileInfo(card.road.sheets.constFirst().at).fileName());
    }
    if (onAVolume(card))
        return Words::aimedVolumeNote(volumeNumberText(card));
    // Adding editions: what it does to the volumes already held, and what the new ones will
    // be called, two of them as examples.
    if (card.road.target && card.road.target->kind == u"SERIES_NEW_EDITIONS"_s) {
        QStringList parts;
        for (const Api::Reshelved &one : card.road.reshelved)
            parts << Words::reshelved(one.count, one.edition);
        constexpr int shown = 2;
        for (int i = 0; i < card.road.landsAs.size() && i < shown; ++i)
            parts << Words::renamedExample(card.road.landsAs.at(i).wasCalled,
                                           card.road.landsAs.at(i).file);
        if (card.road.landsAs.size() > shown)
            parts << Words::andOthers(int(card.road.landsAs.size()) - shown);
        if (!parts.isEmpty())
            return Words::joinDetails(parts);
    }
    int added = 0;
    int replaced = 0;
    for (const Api::VolumeMatch &one : card.road.volumes) {
        added += one.outcome == Outcome::Add ? 1 : 0;
        replaced += one.outcome == Outcome::Replace || one.outcome == Outcome::Update ? 1 : 0;
    }
    return Words::aimedChanges(added, replaced, int(card.road.absent.size()));
}

QString listTitle(const Card &card)
{
    if (onAUniverse(card))
        return Words::aimedListUniverse();
    if (onAChapter(card))
        return Words::chaptersOfVolume(volumeNumberText(card));
    return onAVolume(card) ? Words::aimedListVolume() : Words::aimedListSeries();
}

QVariantList counts(const Card &card, int lit)
{
    if (onAUniverse(card)) {
        const QList<SeriesLine> lines = seriesLines(card);
        QVariantList out;
        for (const int which : {All, ToReview, Ready, Identical}) {
            const qsizetype count = which == All
                ? lines.size()
                : std::ranges::count_if(lines, [which](const SeriesLine &one) {
                      return seriesGroup(one) == which;
                  });
            out << QVariantMap{{u"label"_s, Words::reviewFilter(Words::Reviewed(which))},
                               {u"count"_s, int(count)},
                               {u"lit"_s, which == lit}};
        }
        return out;
    }
    if (onAChapter(card)) {
        const int chapters = card.road.spliced ? int(card.road.spliced->chapters.size()) : 0;
        QVariantList out;
        for (const int which : {All, ToReview, Ready, Identical}) {
            out << QVariantMap{{u"label"_s, Words::reviewFilter(Words::Reviewed(which))},
                               {u"count"_s, which == All || which == ToReview ? chapters : 0},
                               {u"lit"_s, which == lit}};
        }
        return out;
    }
    const QList<Line> lines = linesOf(card);
    QVariantList out;
    for (const int which : {All, ToReview, Ready, Identical}) {
        const qsizetype count = which == All
            ? lines.size()
            : std::ranges::count_if(lines, [which](const Line &one) { return groupOf(one) == which; });
        out << QVariantMap{{u"label"_s, Words::reviewFilter(Words::Reviewed(which))},
                           {u"count"_s, int(count)},
                           {u"lit"_s, which == lit}};
    }
    return out;
}

QVariantList rows(const Card &card, int filter, bool showIdentical, const QString &focus,
                  const QString &searchKey)
{
    if (onAChapter(card))
        return chapterRows(card, focus);
    if (onAUniverse(card))
        return universeRows(card, filter, showIdentical, focus);
    const QList<Line> lines = shown(card, searchKey);
    QVariantList out;
    for (const int group : {ToReview, Ready, Identical}) {
        if (filter != All && filter != group)
            continue;
        QList<Line> inGroup;
        for (const Line &one : lines) {
            if (groupOf(one) == group)
                inGroup << one;
        }
        if (inGroup.isEmpty())
            continue;
        const bool open = group != Identical || showIdentical || filter == Identical;
        out << groupHeader(group, filter, showIdentical, int(inGroup.size()));
        if (!open)
            continue;
        for (const Line &one : inGroup)
            out << lineOf(card, one, focus);
    }
    return out;
}

static QVariantMap sheetOf(const Card &card, const Facts::Composed &composed, const QString &id)
{
    if (onAChapter(card))
        return chapterSheet(card);
    if (onAUniverse(card)) {
        for (const SeriesLine &one : seriesLines(card)) {
            if (one.id == id)
                return seriesSheetOf(card, one);
        }
        return seriesSheet(card, composed);
    }
    for (const Line &line : linesOf(card)) {
        if (line.id != id)
            continue;
        QVariantMap out{{u"id"_s, id},
                        {u"title"_s, titleOf(line)},
                        {u"subtitle"_s, QString()},
                        {u"compare"_s, QVariantMap{}},
                        {u"alarming"_s, QVariantMap{}},
                        {u"choices"_s, QVariantList{}},
                        {u"progress"_s, emptyProgress()}};
        if (line.absent) {
            out.insert(u"alarming"_s,
                       QVariantMap{{u"title"_s,
                                    Words::volumeNotInTheDrop(numberText(line.absent->number))},
                                   {u"text"_s, Words::volumeStaysInTheLibrary()}});
            return out;
        }
        const Api::VolumeMatch &match = *line.match;
        out.insert(u"compare"_s, compareOf(card, match));
        if (match.present)
            out.insert(u"progress"_s, progressOf(card, match));
        if (match.outcome == Outcome::OtherSeries) {
            out.insert(u"alarming"_s,
                       QVariantMap{{u"title"_s,
                                    Words::fileBelongsToAnotherSeries(seriesSaid(card, match.path))},
                                   {u"text"_s, Words::nothingWasModified()}});
        } else if (match.outcome == Outcome::OtherNumber) {
            const QString own = numberText(match.number);
            const QString aimed = volumeNumberText(card);
            const Api::HeldVolume *held = aimedVolume(card);
            out.insert(u"alarming"_s,
                       QVariantMap{{u"title"_s, Words::fileIsAnotherVolume(own, aimed)},
                                   {u"text"_s, Words::nothingWasModified()}});
            const QString ownDetail = match.entryId.isEmpty() || match.entryId == card.road.target->id
                ? Words::volumeIsAdded(own, aimed)
                : Words::volumeIsReplaced(own, aimed);
            QVariantList choices;
            choices << QVariantMap{{u"id"_s, u"own"_s},
                                   {u"label"_s, Words::filePutAtVolume(own)},
                                   {u"badge"_s, Words::recommended()},
                                   {u"chosen"_s, !givenTheAimedNumber(card, match.path)},
                                   {u"detail"_s, ownDetail}};
            choices << QVariantMap{{u"id"_s, u"aimed"_s},
                                   {u"label"_s, Words::replaceAnyway(aimed)},
                                   {u"badge"_s, QString()},
                                   {u"chosen"_s, givenTheAimedNumber(card, match.path)},
                                   {u"detail"_s, held && match.arriving.pages.has_value()
                                                     ? Words::pagesGoFromToSentence(
                                                           held->pages, *match.arriving.pages)
                                                     : QString()}};
            out.insert(u"choices"_s, choices);
        }
        return out;
    }
    return seriesSheet(card, composed);
}

QVariantMap sheet(const Card &card, const Facts::Composed &composed, const QString &id)
{
    QVariantMap out = sheetOf(card, composed, id);
    // What the library holds of what the sheet is about, for the window that corrects it:
    // a volume held (the line's, or the one aimed at) and the edition it is in. Nothing is
    // written to either by the import when they are identical, so this is the one way to
    // reach their information from the frame.
    const Api::TrailStep *edition = stepOf(card, u"SERIES"_s);
    QString seriesId = edition ? edition->id : QString();
    if (seriesId.isEmpty() && card.road.target && card.road.target->kind == u"SERIES"_s)
        seriesId = card.road.target->id;
    QString entryId;
    bool aLine = false;
    for (const Line &line : linesOf(card)) {
        if (line.id != id)
            continue;
        aLine = true;
        if (line.match && line.match->present)
            entryId = line.match->present->entryId;
    }
    // Not on a file's line — a file of another number is not the volume aimed at: the volume.
    if (entryId.isEmpty() && !aLine && onAVolume(card) && !onAChapter(card))
        entryId = card.road.target->id;
    if (!seriesId.isEmpty() && !onAUniverse(card)) {
        out.insert(u"editSeriesId"_s, seriesId);
        out.insert(u"editEntryId"_s, entryId);
    }
    return out;
}

QString footer(const Card &card)
{
    if (onAChapter(card))
        return Words::spliceFooter(volumeNumberText(card));
    if (onAUniverse(card))
        return Words::universeFooter();
    return Words::aimedFooter();
}

namespace {

/// The series of an universe's drop that change: each move ticked, and each one created.
int seriesChanges(const Card &card)
{
    int series = 0;
    for (const Api::Relocation &one : card.road.moves)
        series += card.road.filing.contains(one.workId) ? 1 : 0;
    for (const Api::Creation &one : card.road.creates)
        series += one.kind == u"WORK"_s ? 1 : 0;
    return series;
}

} // namespace

int changes(const Card &card)
{
    if (onAChapter(card))
        return card.road.spliced ? 1 : 0;
    int count = onAUniverse(card) ? seriesChanges(card) : 0;
    // The volumes of a series the drop creates are that creation, not a change of their own.
    QSet<QString> created;
    if (onAUniverse(card)) {
        for (const Api::Creation &one : card.road.creates)
            created.insert(one.at);
    }
    for (const Api::VolumeMatch &one : card.road.volumes) {
        if (created.contains(one.path.section(u'/', 0, 0)))
            continue;
        if (one.outcome == Outcome::Add
            || (switchable(Line{one.path, &one, nullptr}) && card.road.replacing.contains(one.path)))
            ++count;
    }
    // And each value of a declaration that differs, until the reader keeps the library's.
    for (const Api::Declaration &declaration : card.road.declarations) {
        if (!card.road.declaring.contains(declaration.path))
            continue;
        for (const QString &key : declaration.differs)
            count += card.road.keeping.contains(declaration.path + u'\n' + key) ? 0 : 1;
    }
    return count;
}

namespace {

/// A volume aimed at that receives another volume's file has one decision, and the label of
/// « Appliquer » is that decision's.
/// Whether the file at `path` now carries the number of the volume aimed at.
bool givenTheAimedNumber(const Card &card, const QString &path)
{
    const std::optional<double> aimed = aimedNumber(card);
    for (const Facts::Sheet &sheet : card.road.sheets) {
        if (sheet.at != path)
            continue;
        for (const Facts::Field &field : sheet.fields) {
            if (field.key == u"number"_s)
                return aimed && field.value.isDouble() && field.value.toDouble() == *aimed;
        }
    }
    return false;
}

bool askedAboutOneFile(const Card &card)
{
    return onAVolume(card) && card.road.volumes.size() == 1
           && card.road.volumes.constFirst().outcome == Outcome::OtherNumber;
}

} // namespace

bool anything(const Card &card)
{
    return changes(card) > 0 || askedAboutOneFile(card);
}

QString importLabel(const Card &card)
{
    if (onAChapter(card))
        return card.road.spliced ? Words::replaceChapterLabel() : Words::nothingToImport();
    if (askedAboutOneFile(card)) {
        const Api::VolumeMatch &only = card.road.volumes.constFirst();
        return givenTheAimedNumber(card, only.path)
            ? Words::replaceAnyway(volumeNumberText(card))
            : Words::filePutAtVolume(numberText(only.number));
    }
    const int count = changes(card);
    return count > 0 ? Words::applyChanges(count) : Words::nothingToImport();
}

QString moveWorkId(const Card &card, const QString &id)
{
    if (!onAUniverse(card))
        return {};
    for (const Api::Relocation &one : card.road.moves) {
        if (one.at == id)
            return one.workId;
    }
    return {};
}

bool isChapterOfAimedVolume(const Card &card, const QString &id)
{
    if (!onAChapter(card) || !card.road.spliced || id == card.road.target->id)
        return false;
    return std::ranges::any_of(card.road.spliced->chapters,
                               [&id](const Api::ChapterSplice &one) { return one.id == id; });
}

} // namespace Aimed
