#include "Review.h"

#include "Aimed.h"
#include "Api.h"
#include "Card.h"
#include "Carrier.h"
#include "Facts.h"
#include "Fold.h"
#include "Imports.h"
#include "Orders.h"
#include "Sagas.h"
#include "Words.h"

#include <QDebug>
#include <QFileInfo>
#include <QUrl>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlEngine>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

using Facts::Field;
using Facts::Sheet;
using Manifest::Level;
using Filter = Review::Filter;

// `Review::Origin` is `Facts::Origin` said to QML: a value inserted in one and not the other
// would dress a guess as said.
static_assert(std::to_underlying(Review::Origin::Said) == std::to_underlying(Facts::Origin::Said));
static_assert(std::to_underlying(Review::Origin::Taken) == std::to_underlying(Facts::Origin::Taken));
static_assert(std::to_underlying(Review::Origin::Guessed) == std::to_underlying(Facts::Origin::Guessed));
static_assert(std::to_underlying(Review::Origin::Missing) == std::to_underlying(Facts::Origin::Missing));
// And `Review::Filter` is `Words::Reviewed`, for the label of each filter and each group.
static_assert(std::to_underlying(Review::Filter::All) == std::to_underlying(Words::Reviewed::All));
static_assert(std::to_underlying(Review::Filter::ToReview) == std::to_underlying(Words::Reviewed::ToReview));
static_assert(std::to_underlying(Review::Filter::Ready) == std::to_underlying(Words::Reviewed::Ready));
static_assert(std::to_underlying(Review::Filter::Identical) == std::to_underlying(Words::Reviewed::Identical));

/// `Review::cost`. Not atomic, and not to be: the frame is built on the thread that draws it
/// and on no other, so a plain count is exact there and nowhere else is it read.
Review::Cost &spent()
{
    static Review::Cost total;
    return total;
}

/// The rail's first line, the whole apport; not `Filter::All`, which is a tab.
const QString WholeNode = u"all"_s;
const QString Loose = u"loose"_s;
const QString ComicInfoXml = u"ComicInfo.xml"_s;

/// One thing of the frame: a sheet, and the card it belongs to.
struct Item {
    const Card *card = nullptr;
    const Sheet *sheet = nullptr;
};

/// A book is a file too: left out, a book at a universe's root was a container of nothing,
/// listed nowhere and counted identical while its `oneshot.json` was on its way.
bool isFile(Level level)
{
    using enum Manifest::Level;
    return level == Volume || level == Chapter || level == Book;
}

/// Whether `path` is `at` or lies under it. `""` is a card's own root, and holds everything.
bool under(const QString &path, const QString &at)
{
    return at.isEmpty() || path == at || path.startsWith(at + u'/');
}

QString parentOf(const QString &path)
{
    const qsizetype slash = path.lastIndexOf(u'/');
    return slash < 0 ? QString() : path.left(slash);
}

QString idOf(const Item &item)
{
    return QString::number(item.card->token) + u'/' + item.sheet->at;
}

const Field *fieldIn(const Sheet &sheet, const QString &key)
{
    const auto found = std::ranges::find(sheet.fields, key, &Field::key);
    return found == sheet.fields.end() ? nullptr : std::to_address(found);
}

/// The fields that hold a list of words, given as words and never typed with commas.
bool isListKey(const QString &key)
{
    return key == u"authors"_s || key == u"artists"_s || key == u"genres"_s || key == u"tags"_s;
}

/// The words a list field holds, trimmed, nothing when it is missing.
QStringList wordsOf(const Field *field)
{
    QStringList out;
    if (!field || field->origin == Facts::Origin::Missing)
        return out;
    for (const QJsonValue &one : field->value.toArray()) {
        if (const QString word = one.toString().trimmed(); !word.isEmpty())
            out << word;
    }
    return out;
}

/// The name a container goes by: what its sheet says it is called, its folder's otherwise.
QString nameOf(const Sheet &sheet)
{
    // A book has no number to be called by, and a title of its own.
    if (isFile(sheet.level) && sheet.level != Level::Book) {
        const Field *number = fieldIn(sheet, u"number"_s);
        const bool numbered = number && number->origin != Facts::Origin::Missing;
        const QString called =
            Words::fileTitle(sheet.level,
                             numbered ? std::optional(number->value.toDouble()) : std::nullopt,
                             sheet.name);
        // « Tome 1 · Son Gokû et ses amis »: the title when the volume has one.
        if (const Field *title = fieldIn(sheet, u"title"_s);
            title && title->origin != Facts::Origin::Missing
            && !title->value.toString().trimmed().isEmpty())
            return Words::titled(called, title->value.toString().trimmed());
        return called;
    }
    for (const QString &key : {u"title"_s, u"name"_s}) {
        if (const Field *field = fieldIn(sheet, key);
            field && field->origin != Facts::Origin::Missing && !field->value.toString().isEmpty())
            return field->value.toString();
    }
    return sheet.name;
}

/// The cards the frame shows: folders whose walk has ended. A file dropped alone keeps its
/// own road (`/preflight`) and has no sheet to show.
QList<const Card *> cardsOf(const Imports *imports)
{
    QList<const Card *> out;
    if (!imports)
        return out;
    for (const Card &card : imports->cards()) {
        if (card.folder && !card.road.sheets.isEmpty())
            out << &card;
    }
    return out;
}

bool held(const Card &card, const QString &path)
{
    return card.road.alreadyThere.contains(path);
}

/// The files of `card` under the container at `at`, as the walk listed them.
QList<const Manifest::Entry *> filesUnder(const Card &card, const QString &at)
{
    QList<const Manifest::Entry *> out;
    for (const Manifest::Entry &file : card.road.tree.files) {
        ++spent().entries;
        if (under(file.path, at))
            out << &file;
    }
    return out;
}

/// Identical, to review, or ready — the three groups, and what the filters count.
///
/// Identical is what reaches the library not at all (`Carrier::travels`): a file the server
/// already holds byte for byte, a container nothing of which travels, itself or beneath it —
/// a series held whole whose edition the server creates is not « rien à faire ». To review
/// is a sheet that travels and stops the step (`Facts::needsReview`), its own or one beneath
/// it. Ready is the rest. The same predicate as `Imports::confirm`'s: read apart, they
/// disagreed on a series held whole whose `work.json` would be written.
/// The look-alike the library holds of the series at `at` (relative to the card), when the
/// reader has not said a second one is wanted.
const Api::Resemblance *resemblanceAt(const Card &card, const QString &at)
{
    if (card.road.target)
        return nullptr;
    for (const Api::Resemblance &one : card.road.resembles) {
        if (one.at == at && !card.road.second.contains(at))
            return &one;
    }
    return nullptr;
}

/// Where the server says the files under a sheet will be filed — « Série › Édition » — or
/// nothing when it says none (an older server, or a sheet that carries no file).
QString filedFolderOf(const Card &card, const Sheet &sheet)
{
    for (const Api::Landing &one : card.road.landsAs) {
        if (sheet.at.isEmpty() || one.path == sheet.at || one.path.startsWith(sheet.at + u'/'))
            return one.folder;
    }
    return {};
}

Filter groupOf(const Item &item)
{
    using enum Review::Filter;
    const Card &card = *item.card;
    const Sheet &sheet = *item.sheet;
    // A card refused is read first, before what travels: nothing of it travels any more, and
    // read by that alone a series the library held was « Identique », one whose announcement
    // was refused « Prête » — both folded among what is fine, and neither ever going.
    if (card.stage == Card::Stage::Failed)
        return ToReview;
    if (isFile(sheet.level)) {
        if (!Carrier::travels(card, sheet))
            return Identical;
        return Facts::needsReview(sheet) ? ToReview : Ready;
    }
    if (resemblanceAt(card, sheet.at))
        return ToReview;
    bool goes = false;
    bool reviewing = false;
    for (const Sheet &one : card.road.sheets) {
        if (!under(one.at, sheet.at) || !Carrier::travels(card, one))
            continue;
        goes = true;
        reviewing = reviewing || Facts::needsReview(one);
    }
    if (!goes)
        return Identical;
    return reviewing ? ToReview : Ready;
}

/// The series of a card: those under it when it is a universe, the card itself otherwise —
/// an edition dropped on its own included, which the tree then shows as a series.
QList<Item> seriesOf(const Card &card)
{
    QList<Item> out;
    if (const Sheet &root = card.road.sheets.constFirst(); root.level != Level::Universe)
        return {Item{&card, &root}};
    for (const Sheet &sheet : card.road.sheets) {
        if (sheet.level == Level::Work)
            out << Item{&card, &sheet};
    }
    return out;
}

/// The editions a series declares. An implicit edition has no folder and no sheet, and is
/// the series itself: drawing a node for it would be a second line saying the same thing.
QList<Item> editionsOf(const Item &series)
{
    QList<Item> out;
    for (const Sheet &sheet : series.card->road.sheets) {
        if (sheet.level == Level::Edition && &sheet != series.sheet
            && parentOf(sheet.at) == series.sheet->at)
            out << Item{series.card, &sheet};
    }
    return out;
}

QList<Item> filesOf(const Item &container)
{
    QList<Item> out;
    for (const Sheet &sheet : container.card->road.sheets) {
        // Never the container itself: a sheet that is a file by its level would otherwise
        // appear among its own lines and count once more in « Tout ».
        if (&sheet != container.sheet && isFile(sheet.level)
            && under(sheet.at, container.sheet->at))
            out << Item{container.card, &sheet};
    }
    return out;
}

/// What a container weighs, or a file — the size of each file read once per list.
///
/// A file was weighed by walking every file of its card, so a list of a thousand chapters
/// read a million and a half entries each time it was built, and it is built on every
/// change of the queue.
class Weights
{
public:
    qint64 of(const Item &item)
    {
        if (!isFile(item.sheet->level)) {
            qint64 total = 0;
            for (const Manifest::Entry *file : filesUnder(*item.card, item.sheet->at))
                total += file->size;
            return total;
        }
        QHash<QString, qint64> &sizes = m_sizes[item.card];
        if (sizes.isEmpty()) {
            for (const Manifest::Entry &file : item.card->road.tree.files) {
                ++spent().entries;
                sizes.insert(file.path, file.size);
            }
        }
        return sizes.value(item.sheet->at);
    }

private:
    QHash<const Card *, QHash<QString, qint64>> m_sizes;
};

/// What a line is worth a word for: the one field it lacks or doubts, by its name, or
/// « Manquant » when there are several. Nothing otherwise.
///
/// It used to say where its values were read — « Écrit », « Repris », « Deviné » — on every
/// line, and the one line that lacked a drawer wore a chip like the forty others.
std::pair<QString, Facts::Origin> chipOf(const Sheet &sheet)
{
    using enum Facts::Origin;
    QList<const Field *> asking;
    for (const Field &field : sheet.fields)
        if (Facts::lacks(sheet, field) || !field.doubt.isEmpty())
            asking << &field;
    if (asking.isEmpty())
        return {QString(), Said};
    if (asking.size() == 1)
        return {Words::fieldLabel(asking.constFirst()->key, sheet.level), Missing};
    return {Words::missing(), Missing};
}

/// What a field shows inside itself: the value as a reader writes it.
QString textOf(const Field &field)
{
    const QJsonValue &value = field.value;
    if (field.key == u"readingDirection"_s) {
        using enum Api::ReadingDirection;
        for (const Api::ReadingDirection one : {LeftToRight, RightToLeft, Vertical}) {
            if (Api::spell(one) == value.toString())
                return Words::readingDirection(one);
        }
    }
    if (field.key == u"status"_s && value.isString())
        return Words::editionStatus(value.toString());
    if (value.isDouble())
        return Words::number(value.toDouble());
    if (value.isArray()) {
        QStringList parts;
        for (const QJsonValue &one : value.toArray())
            parts << (one.isObject() ? one.toObject().value(u"raw"_s).toString() : one.toString());
        // A comma, and not a « · »: a list of names, as a sentence writes one.
        return parts.join(u", "_s);
    }
    return value.toString();
}

/// What a field shows that it does not hold: a volume's credit its series gives it, an
/// edition's value its work gives it. Drawn greyed, and never written as the sheet's own.
struct InheritedValue {
    bool held = false;
    QString text;
    QStringList words;
};

InheritedValue inheritedOf(const Sheet &sheet, const Field &field)
{
    // `fallback` is set on a volume or a chapter alone (`weighCredits`), and stands for the
    // field only while the volume names nobody: ComicInfo fills the hole before it.
    if (const bool credit = field.key == u"authors"_s || field.key == u"artists"_s;
        credit && field.origin == Facts::Origin::Missing) {
        const QStringList &from =
            field.key == u"authors"_s ? sheet.credits.fallback.authors
                                      : sheet.credits.fallback.artists;
        if (!from.isEmpty())
            return {true, from.join(u", "_s), from};
    }
    // An edition's field read in `work.json` because its own sidecar said nothing: the sheet
    // showed it as the edition's, and nothing said that it would follow the series.
    if (sheet.level == Level::Edition && !field.edited
            && field.origin != Facts::Origin::Missing && field.from == u"work.json"_s)
        return {true, textOf(field), {}};
    // And the same value once the edition's own is emptied: the server reads it again.
    if (field.origin == Facts::Origin::Missing && !field.fromWork.isNull()) {
        Field given = field;
        given.value = field.fromWork;
        return {true, textOf(given), {}};
    }
    // A series that names no form for its chapters has the server's: an empty box said
    // nothing of what every chapter without a title would be called.
    if (field.key == u"chapterLabel"_s && field.origin == Facts::Origin::Missing)
        return {true, Words::defaultChapterLabel(), {}};
    return {};
}

/// The line under a series' Scénario and Dessin, what they are the default of, and under its
/// chapter label, how a form is written.
QString hintOf(const Sheet &sheet, const Field &field)
{
    if (sheet.level == Level::Work && field.key == u"chapterLabel"_s)
        return Words::chapterLabelHint();
    if (sheet.level != Level::Work || (field.key != u"authors"_s && field.key != u"artists"_s))
        return {};
    const std::optional<Api::Medium> medium =
        sheet.medium.isEmpty() ? std::nullopt : std::optional(Api::medium(sheet.medium));
    return Words::creditDefault(field.key == u"artists"_s, medium);
}

/// What the box of a field is: the sheet draws a summary as a text area, a date as a date, a
/// number as a short numeric line and the chapters as the list they are — all of them were
/// the same one-line box, which cut a summary at its first line and wrote the chapters out as
/// « , , , , , » since a chapter has a title and no `raw`.
QString kindOf(const QString &key)
{
    if (key == u"summary"_s)
        return u"long"_s;
    if (key == u"publishedOn"_s)
        return u"date"_s;
    if (key == u"number"_s || key == u"volumeCount"_s)
        return u"number"_s;
    if (key == u"chapters"_s)
        return u"chapters"_s;
    return u"line"_s;
}

/// One entry per chapter — its number, its title and the page it starts on — for the sheet to
/// draw as the list it is.
QVariantList chapterLines(const Field &field)
{
    QVariantList lines;
    for (const QJsonValue &one : field.value.toArray()) {
        const QJsonObject chapter = one.toObject();
        const QString number = chapter.value(u"number"_s).isDouble()
                                   ? Words::number(chapter.value(u"number"_s).toDouble())
                                   : QString();
        const QString title = chapter.value(u"title"_s).toString().trimmed();
        const QString page = chapter.value(u"startPage"_s).isDouble()
                                 ? Words::pageNumbered(chapter.value(u"startPage"_s).toInt())
                                 : QString();
        if (!number.isEmpty() || !title.isEmpty() || !page.isEmpty())
            lines << QVariantMap{{u"number"_s, number}, {u"title"_s, title}, {u"page"_s, page}};
    }
    return lines;
}

/// The edition a volume of `work` is in: the `at` of the nearest edition folder above it, or the
/// series' own `at` for a volume of the edition the series holds without declaring one.
QString editionOf(const Sheet &work, const QList<Sheet> &sheets, const Sheet &volume)
{
    QString nearest = work.at;
    for (const Sheet &sheet : sheets)
        if (sheet.level == Level::Edition && volume.at.startsWith(sheet.at + u'/')
            && sheet.at.size() > nearest.size())
            nearest = sheet.at;
    return nearest;
}

bool isVolumeOf(const Sheet &work, const Sheet &sheet)
{
    return sheet.level == Level::Volume
           && (work.at.isEmpty() || sheet.at.startsWith(work.at + u'/'));
}

/// The chapters a sheet's `entry.json` lists, in its order: their numbers and pages.
QList<Facts::Sheet::ChapterStart> chapterStartsOf(const Sheet &sheet)
{
    QList<Facts::Sheet::ChapterStart> out;
    const Field *chapters = fieldIn(sheet, u"chapters"_s);
    if (!chapters || chapters->origin == Facts::Origin::Missing)
        return out;
    for (const QJsonValue &one : chapters->value.toArray()) {
        const QJsonObject held = one.toObject();
        if (!held.value(u"number"_s).isDouble())
            continue;
        out << Facts::Sheet::ChapterStart{held.value(u"number"_s).toDouble(),
                                          held.value(u"startPage"_s).isDouble()
                                              ? std::optional(held.value(u"startPage"_s).toInt())
                                              : std::nullopt};
    }
    return out;
}

/// Where the arcs of `work` counted in chapters cross the chapters of `volume`, for its window:
/// one running when the volume opens, said with its span and the volumes of the edition that
/// hold it, and one opening at a chapter of it, said with that chapter's page. `at` is the
/// line of the volume's list the band goes over.
QVariantList arcsCrossing(const Sheet &work, const QList<Sheet> &sheets, const Sheet &volume)
{
    QVariantList out;
    const QList<Facts::Sheet::ChapterStart> held = chapterStartsOf(volume);
    const Field *arcs = fieldIn(work, u"arcs"_s);
    if (held.isEmpty() || !arcs || arcs->origin == Facts::Origin::Missing)
        return out;
    const auto [lowest, highest] = std::ranges::minmax(held, {}, &Facts::Sheet::ChapterStart::number);
    const QString edition = editionOf(work, sheets, volume);
    for (const QJsonValue &one : arcs->value.toArray()) {
        const QJsonObject arc = one.toObject();
        if (arc.value(u"unit"_s).toString() != u"CHAPTER"_s)
            continue;
        const double from = arc.value(u"from"_s).toDouble();
        const double to = arc.value(u"to"_s).toDouble();
        if (to < lowest.number || from > highest.number)
            continue;
        const bool starts = from >= lowest.number;
        QVariantMap band{{u"name"_s, arc.value(u"name"_s).toString()},
                         {u"starts"_s, starts},
                         {u"state"_s, Words::chaptersArcState(starts)}};
        if (starts) {
            const auto opening = std::ranges::find_if(
                held, [from](const Facts::Sheet::ChapterStart &one) { return one.number >= from; });
            band.insert(u"at"_s, int(opening - held.begin()));
            band.insert(u"said"_s, Words::chaptersArcStarts(from, opening->number == from
                                                                      ? opening->page
                                                                      : std::nullopt));
        } else {
            const auto holds = [&](const Sheet &sheet) {
                return isVolumeOf(work, sheet) && editionOf(work, sheets, sheet) == edition
                       && std::ranges::any_of(chapterStartsOf(sheet),
                                              [&](const Facts::Sheet::ChapterStart &chapter) {
                                                  return chapter.number >= from
                                                         && chapter.number <= to;
                                              });
            };
            band.insert(u"at"_s, 0);
            band.insert(u"said"_s, Words::chaptersArcRuns(from, to,
                                                          int(std::ranges::count_if(sheets, holds))));
        }
        out << band;
    }
    std::ranges::stable_sort(out, {}, [](const QVariant &one) { return one.toMap().value(u"at"_s).toInt(); });
    return out;
}

/// The editions of `work` that hold volumes, in the order of the tree: the columns its arcs are
/// placed in. The edition the series holds without declaring one carries the series' title.
QVariantList arcsEditions(const Sheet &work, const QList<Sheet> &sheets)
{
    QVariantList out;
    QSet<QString> seen;
    for (const Sheet &sheet : sheets) {
        if (!isVolumeOf(work, sheet))
            continue;
        const QString at = editionOf(work, sheets, sheet);
        if (seen.contains(at))
            continue;
        seen.insert(at);
        QString name = work.name;
        if (at != work.at) {
            const auto edition = std::ranges::find(sheets, at, &Sheet::at);
            name = edition->name;
        } else if (const Field *title = fieldIn(work, u"title"_s);
                   title && title->origin != Facts::Origin::Missing && !textOf(*title).isEmpty()) {
            name = textOf(*title);
        }
        out << QVariantMap{{u"at"_s, at}, {u"name"_s, name}};
    }
    return out;
}

/// Where a chapter falls: the volume holding it, and the page it starts on.
struct Place {
    QString volume;
    std::optional<int> page;
};

/// Where `chapter` falls among the volumes of the edition `edition` of `work`, from what their
/// `entry.json` lists; nothing when no volume lists it.
std::optional<Place> placeOfChapter(const Sheet &work, const QString &edition,
                                    const QList<Sheet> &sheets, int chapter)
{
    for (const Sheet &sheet : sheets) {
        const Field *chapters = fieldIn(sheet, u"chapters"_s);
        if (!isVolumeOf(work, sheet) || editionOf(work, sheets, sheet) != edition || !chapters
            || chapters->origin == Facts::Origin::Missing)
            continue;
        for (const QJsonValue &one : chapters->value.toArray()) {
            const QJsonObject held = one.toObject();
            if (held.value(u"number"_s).toDouble(-1) != chapter)
                continue;
            const Field *number = fieldIn(sheet, u"number"_s);
            const QString volume =
                Words::fileTitle(sheet.level,
                                 number && number->origin != Facts::Origin::Missing
                                     ? std::optional(number->value.toDouble()) : std::nullopt,
                                 sheet.name);
            return Place{volume.toLower(), held.value(u"startPage"_s).isDouble()
                                               ? std::optional(held.value(u"startPage"_s).toInt())
                                               : std::nullopt};
        }
    }
    return std::nullopt;
}

/// Where the chapters of an arc fall among the volumes of one edition of the series, from what
/// their `entry.json` lists: the volume holding the chapter it opens on and the one holding the
/// chapter it ends on, each with the page that chapter starts on. Looked for in every volume of
/// the series, the first found was given, and a series of two editions said one edition's page
/// under the other's volume. Empty when no volume lists them — the volumes of a library read
/// without sidecars say nothing of chapters, and a guess here would be a place that is not there.
QString arcFalls(const Sheet &work, const QString &edition, const QList<Sheet> &sheets,
                 const QVariantMap &arc)
{
    const int from = arc.value(u"from"_s).toInt();
    const int to = arc.value(u"to"_s).toInt();
    if (arc.value(u"unit"_s).toString() == u"VOLUME"_s)
        return Words::arcsFallsInVolumes(from, to);
    const std::optional<Place> opens = placeOfChapter(work, edition, sheets, from);
    const std::optional<Place> ends = placeOfChapter(work, edition, sheets, to);
    if (!opens || !ends)
        return {};
    return Words::arcsFallsInChapters(opens->volume, opens->page, to, ends->volume, ends->page);
}

QVariantList choicesOf(const QString &key)
{
    QVariantList out;
    if (key == u"readingDirection"_s) {
        using enum Api::ReadingDirection;
        for (const Api::ReadingDirection one : {LeftToRight, RightToLeft, Vertical})
            out << QVariantMap{{u"value"_s, Api::spell(one)},
                               {u"label"_s, Words::readingDirection(one)}};
    } else if (key == u"status"_s) {
        // The contract's four words, and `editionStatus`'s French for them — « En parution »
        // and not « En cours », which already says how far *you* have read.
        for (const QString &one : {u"ongoing"_s, u"completed"_s, u"hiatus"_s, u"cancelled"_s})
            out << QVariantMap{{u"value"_s, one}, {u"label"_s, Words::editionStatus(one)}};
    }
    return out;
}

/// Where a field said in `sheet` is said. An edited field is written into this sheet's own
/// file, and said there from now on — not in the ComicInfo.xml or the name it replaced.
QString whereSaid(const Sheet &sheet, const Field &field)
{
    if (!field.edited)
        return field.from;
    return isFile(sheet.level) ? u"entry.json"_s : QFileInfo(sheet.sidecar).fileName();
}

/// Where most of a sheet's values came from — said first on a tie, then taken.
QString subtitleOf(const Sheet &sheet)
{
    // What it is, and not where its values came from: « Tome » over a file the scan will not
    // read as one named it for what it would never be.
    if (sheet.role == Shape::Role::Unread)
        return Words::anArchive();
    using enum Facts::Origin;
    int said = 0;
    int taken = 0;
    int guessed = 0;
    QHash<QString, int> saidIn;
    for (const Field &field : sheet.fields) {
        if (field.origin == Said) {
            ++said;
            ++saidIn[whereSaid(sheet, field)];
        } else if (field.origin == Taken) {
            ++taken;
        } else if (field.origin == Guessed) {
            ++guessed;
        }
    }
    if (said > 0 && said >= taken && said >= guessed) {
        QString from;
        int most = 0;
        for (auto it = saidIn.cbegin(); it != saidIn.cend(); ++it) {
            if (it.value() > most || (it.value() == most && it.key() < from)) {
                from = it.key();
                most = it.value();
            }
        }
        return Words::sheetSubtitle(sheet.level, from, false);
    }
    if (taken > 0 && taken >= guessed)
        return Words::sheetSubtitle(sheet.level, ComicInfoXml, false);
    if (guessed > 0)
        return Words::sheetSubtitle(sheet.level, sheet.name, true);
    return Words::sheetSubtitle(sheet.level, {}, false);
}

/// Whether an item's own file reaches the library (`Carrier::travels`) — what is counted in
/// the footer and opened on: a series whose files are all there but whose `work.json`
/// would be written, and not one whose edition alone is.
bool doing(const Item &item)
{
    return Carrier::travels(*item.card, *item.sheet);
}

/// Whether anything of an item reaches the library, itself or beneath it — the groups'
/// predicate, so that a card whose only news is an edition created is still something to
/// import.
bool anythingGoes(const Item &item)
{
    // A card refused is to review and goes nowhere: counted as going, a frame left with
    // nothing else offered « Importer » over nothing, and no way to close it.
    return item.card->stage != Card::Stage::Failed && groupOf(item) != Filter::Identical;
}

/// Whether a line can be ticked: a series, not a universe (its series are ticked from its
/// node), that goes somewhere and can still be corrected — not an identical one, nothing
/// refused, nothing already let go. A value applied to any other would be written nowhere.
bool tickable(const Item &item)
{
    return item.sheet && item.sheet->level == Level::Work && item.card->folder
           && Carrier::correctable(*item.card) && anythingGoes(item);
}

/// Whether « Importer » sends `card` now: what `Imports::confirm` lets go and `Imports::send`
/// then starts — the whole queue, a file dropped alone included, and never what already
/// went or was already let go to wait its turn. Counted over the frame's folders alone, the
/// button said « Importer 6 tomes » for a second folder of three dropped while the first
/// was on its way, and « Fermer » over a file that it then sent.
bool goesNow(const Card &card)
{
    using enum Card::Stage;
    if (card.road.confirmed)
        return false;
    if (!card.folder)
        return card.stage == Ready && !card.chosen.isEmpty();
    if (card.road.sheets.isEmpty() || (card.stage != Ready && card.stage != Reviewing))
        return false;
    return anythingGoes(Item{&card, &card.road.sheets.constFirst()}) && !Carrier::heldBack(card);
}

/// The volumes « Importer » sends now: a folder's files the library does not hold, and one
/// for each file dropped alone.
///
/// Counted on the sheets and not on the walk's files: an archive no one will read is never
/// announced (`Facts::Composed::leftOut`), and counted from the walk the button said
/// « Importer 2 tomes » over a universe that sent one.
int travelling(const Imports *imports)
{
    int total = 0;
    if (!imports)
        return total;
    for (const Card &card : imports->cards()) {
        if (!goesNow(card))
            continue;
        total += card.folder ? int(std::ranges::count_if(card.road.sheets,
                                                         [&card](const Sheet &sheet) {
                                                             return isFile(sheet.level)
                                                                    && sheet.role
                                                                           != Shape::Role::Unread
                                                                    && !held(card, sheet.at);
                                                         }))
                             : 1;
    }
    return total;
}

/// Finds the item an id names, or nothing.
Item itemOf(const QList<const Card *> &cards, const QString &id)
{
    const qsizetype slash = id.indexOf(u'/');
    if (slash < 0)
        return {};
    bool ok = false;
    const quint64 token = id.left(slash).toULongLong(&ok);
    if (!ok)
        return {};
    const QString at = id.mid(slash + 1);
    for (const Card *card : cards) {
        if (card->token != token)
            continue;
        for (const Sheet &sheet : card->road.sheets) {
            if (sheet.at == at)
                return Item{card, &sheet};
        }
    }
    return {};
}

bool isUniverseCard(const Card &card)
{
    return card.road.sheets.constFirst().level == Level::Universe;
}

/// Whether an item is drawn in the tree: a universe, a series, a declared edition.
bool inTheRail(const Item &item)
{
    if (!item.sheet || isFile(item.sheet->level))
        return false;
    if (item.sheet->at.isEmpty())
        return true;
    return item.sheet->level == Level::Work || item.sheet->level == Level::Edition;
}

// ——— Roles, as the shape read them ——————————————————————————————————————————————————

const QString ShelfIds = u"drop/"_s;

/// Where a card's `at` sits in its drop: the card is rooted at `within`.
QString inTheDrop(const QString &within, const QString &at)
{
    if (within.isEmpty())
        return at;
    return at.isEmpty() ? within : within + u'/' + at;
}

QString shelfIdOf(quint64 drop, const QString &at)
{
    return ShelfIds + QString::number(drop) + u'/' + at;
}

/// The drop and the place a shelf's id names, or nothing for an id that is no shelf's.
std::optional<std::pair<quint64, QString>> shelfOf(const QString &id)
{
    if (!id.startsWith(ShelfIds))
        return std::nullopt;
    const QString rest = id.mid(ShelfIds.size());
    const qsizetype slash = rest.indexOf(u'/');
    bool ok = false;
    const quint64 drop = slash < 0 ? 0 : rest.left(slash).toULongLong(&ok);
    if (!ok)
        return std::nullopt;
    return std::pair{drop, rest.mid(slash + 1)};
}

const Manifest::Node *nodeIn(const Manifest::Node &folder, const QString &at)
{
    if (folder.at == at)
        return &folder;
    for (const Manifest::Node &child : folder.children) {
        if (at == child.at || at.startsWith(child.at + u'/'))
            return nodeIn(child, at);
    }
    return nullptr;
}

/// The drop a card was read out of, as it stands now (`Imports::dropOf`).
const Shape::Drop *dropOfCard(const Imports *imports, const Card &card)
{
    return imports ? imports->dropOf(card.road.drop) : nullptr;
}

/// Whether a card sits under a shelf of its drop rather than being the folder dropped.
bool shelved(const Imports *imports, const Card &card)
{
    return !card.road.within.isEmpty() && dropOfCard(imports, card);
}

/// The shelves the frame draws for one drop: every folder above one of its cards, the
/// dropped folder included. A shelf inside a card is no node — it is out of the model there,
/// and nothing of it is read.
QSet<QString> shelvesOf(const QList<const Card *> &cards, quint64 drop)
{
    QSet<QString> out;
    for (const Card *card : cards) {
        ++spent().looks;
        if (card->road.drop != drop || card->road.within.isEmpty())
            continue;
        for (QString at = parentOf(card->road.within);; at = parentOf(at)) {
            out.insert(at);
            if (at.isEmpty())
                break;
        }
    }
    return out;
}

/// What the frame says of a role: its label, how surely it is known, and whether it is a
/// proposal — neither said by a file nor what the scan reads anyway.
struct RoleSaid {
    int role = -1;
    QString label;
    Review::Origin tone = Review::Origin::Said;
    bool proposed = false;
    /// The confidence a line's chip says, when the role is still a guess to confirm.
    std::optional<Shape::Confidence> asked;
    bool nameMissing = false;
};

/// How sure the reading that gave `at` its role is: its own suggestion's, or that of the
/// folder above whose reading decided it — an edition of a series « à confirmer » is as
/// unsure as that series. A folder decided by a file or a click asks nothing, nor does
/// anything read from it.
Shape::Confidence confidenceAt(const Shape::Drop &drop, const QString &at)
{
    for (QString where = at;; where = parentOf(where)) {
        ++spent().looks;
        if (const Shape::Origin origin = drop.roles.value(where).origin;
            origin == Shape::Origin::Said || origin == Shape::Origin::Chosen)
            return Shape::Confidence::Certain;
        if (const auto said = drop.suggestions.constFind(where); said != drop.suggestions.cend())
            return said->confidence;
        if (where.isEmpty())
            return Shape::Confidence::Certain;
    }
}

/// The tones are the palette's three: Said for a file's word, Guessed for what asks nothing
/// — « probable », the scan's own rule, a role chosen — and Missing for what still asks.
/// A role chosen is not dressed as said: nothing on the disk says it until it is written.
RoleSaid roleSaid(Shape::Role role, Shape::Origin origin, Shape::Confidence confidence,
                  bool nameMissing)
{
    using Tone = Review::Origin;
    RoleSaid said;
    said.role = std::to_underlying(role);
    said.label = Words::role(role);
    said.nameMissing = nameMissing;
    if (role == Shape::Role::Unread) {
        said.tone = Tone::Missing;
        said.proposed = origin != Shape::Origin::Said;
        return said;
    }
    switch (origin) {
    case Shape::Origin::Said:
        said.tone = Tone::Said;
        break;
    case Shape::Origin::Evident:
        said.tone = nameMissing ? Tone::Missing : Tone::Guessed;
        break;
    case Shape::Origin::Guessed:
        said.proposed = confidence != Shape::Confidence::Certain;
        if (said.proposed)
            said.asked = confidence;
        said.tone = nameMissing || confidence == Shape::Confidence::ToConfirm ? Tone::Missing
                                                                            : Tone::Guessed;
        break;
    case Shape::Origin::Chosen:
        said.proposed = true;
        said.tone = nameMissing ? Tone::Missing : Tone::Guessed;
        break;
    }
    return said;
}

/// A series called « Vrac » whose title nobody has typed yet.
bool nameMissing(const Card &card, const Sheet &sheet)
{
    if (!card.road.untitled.contains(sheet.at))
        return false;
    const Field *title = fieldIn(sheet, u"title"_s);
    return title && title->origin == Facts::Origin::Missing;
}

RoleSaid roleOfSheet(const Imports *imports, const Card &card, const Sheet &sheet)
{
    const Shape::Drop *drop = dropOfCard(imports, card);
    const Shape::Confidence confidence =
        drop ? confidenceAt(*drop, inTheDrop(card.road.within, sheet.at))
             : Shape::Confidence::Certain;
    return roleSaid(sheet.role, sheet.roleOrigin, confidence, nameMissing(card, sheet));
}

RoleSaid roleOfShelf(const Shape::Drop &drop, const QString &at)
{
    const Shape::Choice choice = drop.roles.value(at);
    return roleSaid(choice.role, choice.origin, confidenceAt(drop, at), false);
}

/// An archive sitting at a universe's root, where the scan reads books and nothing else.
bool atAUniverseRoot(const Item &item)
{
    return isFile(item.sheet->level) && !item.sheet->at.contains(u'/')
           && isUniverseCard(*item.card);
}

QList<Item> rootArchivesOf(const Card &card)
{
    QList<Item> out;
    if (!isUniverseCard(card))
        return out;
    for (const Sheet &sheet : card.road.sheets) {
        if (atAUniverseRoot(Item{&card, &sheet}))
            out << Item{&card, &sheet};
    }
    return out;
}

/// Whether a card's own line says its reading rather than its values: a role still a guess
/// to confirm, or a series whose name is still to give.
bool asksItsReading(const RoleSaid &said)
{
    return said.asked.has_value() || said.nameMissing;
}

/// Whether « Livre seul » cannot be written on this archive, and why: its `oneshot.json` did
/// not read, or the archive itself did not — `Facts` reads no book out of either.
QString bookRefused(const Sheet &sheet)
{
    if (!isFile(sheet.level))
        return {};
    if (sheet.oneShotUnreadable)
        return Words::oneShotUnreadableReason();
    return sheet.trouble;
}

/// The role menu of the folder or archive at `at` in `drop`, `sheet` its own when it has one
/// — what `ReviewEdits::roleOptions` hands the screen, and what the tree's `changeable` is read
/// from, so the two never disagree.
QVariantList optionsAt(const Shape::Drop &drop, const QString &at, const Sheet *sheet)
{
    const Shape::Role current = sheet ? sheet->role : drop.roles.value(at).role;
    const QString refused = sheet ? bookRefused(*sheet) : QString();
    QVariantList out;
    for (const Shape::Option &option : Shape::options(drop, at)) {
        bool possible = option.possible;
        QString detail = option.detail;
        // `Shape` opens no archive, so it cannot know that this one's `oneshot.json` did not
        // read: written over, it would be replaced by a book nobody read.
        if (option.role == Shape::Role::Book && !refused.isEmpty()) {
            possible = false;
            detail = refused;
        }
        out << QVariantMap{{u"role"_s, std::to_underlying(option.role)},
                           {u"label"_s, Words::roleOption(option.role)},
                           {u"detail"_s, detail},
                           {u"possible"_s, possible},
                           {u"chosen"_s, option.role == current}};
    }
    return out;
}

/// `options` with each role the cards cannot take now greyed and saying so: offered, it was a
/// click that changed nothing and said nothing, while a card it touches was announced again.
QVariantList waiting(QVariantList options, const Imports *imports, quint64 drop,
                     const QString &at)
{
    if (!imports)
        return options;
    for (QVariant &one : options) {
        QVariantMap option = one.toMap();
        if (!option.value(u"possible"_s).toBool() || option.value(u"chosen"_s).toBool())
            continue;
        if (imports->roleWaits(drop, at, Shape::Role(option.value(u"role"_s).toInt()))) {
            option.insert(u"possible"_s, false);
            option.insert(u"detail"_s, Words::roleWaitsForTheCards());
            one = option;
        }
    }
    return options;
}

/// What a line's chip says of the roles at its place: whether another one can be chosen —
/// the ▾ — and, for a screen reader, the greyed ones with their reasons.
struct RoleChange {
    bool changeable = false;
    QString greyed;
};

RoleChange changeOf(const QVariantList &options)
{
    ++spent().looks;
    RoleChange out;
    QList<std::pair<QString, QString>> greyed;
    for (const QVariant &one : options) {
        const QVariantMap option = one.toMap();
        if (!option.value(u"possible"_s).toBool())
            greyed.append({option.value(u"label"_s).toString(),
                           option.value(u"detail"_s).toString()});
        else if (!option.value(u"chosen"_s).toBool())
            out.changeable = true;
    }
    out.greyed = Words::rolesGreyed(greyed);
    return out;
}

/// The frame's shelf at `at` of `drop`, when the id names one it draws.
bool isFrameShelf(const QList<const Card *> &cards, const Imports *imports, quint64 drop,
                  const QString &at)
{
    const Shape::Drop *kept = imports ? imports->dropOf(drop) : nullptr;
    return kept && kept->roles.value(at).role == Shape::Role::Shelf
           && shelvesOf(cards, drop).contains(at);
}

/// Whether one sheet holds `key` (`Fold::searchKey` of what was typed): in its name, its
/// folder's name, its title or one of its authors — folded the same way, so « Simón » is
/// found by « simon » and « MONSTER » finds « Monster ». Read and never written: a search
/// changes what is shown, not what any card says.
bool holds(const Item &item, const QString &key)
{
    ++spent().entries;
    const Sheet &sheet = *item.sheet;
    const QString folder =
        sheet.at.isEmpty() ? QFileInfo(item.card->path).fileName() : QFileInfo(sheet.at).fileName();
    QStringList said{nameOf(sheet), folder};
    if (const Field *title = fieldIn(sheet, u"title"_s);
        title && title->origin != Facts::Origin::Missing)
        said << title->value.toString();
    said << wordsOf(fieldIn(sheet, u"authors"_s));
    return std::ranges::any_of(said, [&key](const QString &one) {
        return Fold::searchKey(one).contains(key);
    });
}

/// Whether a line answers the search. A universe's line answers when one of its series
/// does: it is how the whole drop reaches them, and hidden it hid the series searched for.
bool answers(const Item &item, const QString &key)
{
    if (key.isEmpty() || holds(item, key))
        return true;
    if (item.sheet->level != Level::Universe)
        return false;
    return std::ranges::any_of(seriesOf(*item.card),
                               [&key](const Item &series) { return holds(series, key); });
}

/// The lines of `node` that answer the search `key`: what the list and its counters are
/// made of. The tree is not: it is how the drop is found again.
QList<Item> contentOf(const QList<const Card *> &cards, const QString &node,
                      const Imports *imports);
QList<Item> shownOf(const QList<const Card *> &cards, const QString &node,
                    const Imports *imports, const QString &key)
{
    QList<Item> items = contentOf(cards, node, imports);
    if (!key.isEmpty())
        items.removeIf([&key](const Item &item) { return !answers(item, key); });
    return items;
}

/// What the list of `frame` shows: its node's content, kept to its search.
QList<Item> shownIn(const Review::Frame &frame)
{
    return shownOf(cardsOf(frame.imports), frame.node, frame.imports, frame.searchKey);
}

/// The cards under the shelf at `at` of `drop`.
QList<Item> cardsOnShelf(const QList<const Card *> &cards, quint64 drop, const QString &at)
{
    QList<Item> out;
    for (const Card *card : cards) {
        const QString &within = card->road.within;
        if (card->road.drop == drop && !within.isEmpty()
            && (at.isEmpty() || within.startsWith(at + u'/')))
            out << Item{card, &card->road.sheets.constFirst()};
    }
    return out;
}

/// The lines the chosen node holds: a universe its archives and its series, a series or an
/// edition its files, a shelf its cards, the whole drop and « Sans univers » their cards.
QList<Item> contentOf(const QList<const Card *> &cards, const QString &node,
                      const Imports *imports)
{
    QList<Item> out;
    if (node == WholeNode || node == Loose) {
        for (const Card *card : cards) {
            if (node == WholeNode || (!isUniverseCard(*card) && !shelved(imports, *card)))
                out << Item{card, &card->road.sheets.constFirst()};
        }
        return out;
    }
    if (const auto shelf = shelfOf(node))
        return cardsOnShelf(cards, shelf->first, shelf->second);
    const Item item = itemOf(cards, node);
    if (!item.sheet)
        return out;
    if (item.sheet->level == Level::Universe)
        return rootArchivesOf(*item.card) + seriesOf(*item.card);
    // A series lists its editions and not their volumes put end to end: 76 lines of two editions
    // could not be told apart, and the edition is what has an editor of its own. One goes down
    // into an edition for its volumes.
    if (item.sheet->level == Level::Work && !editionsOf(item).isEmpty()) {
        for (const Sheet &sheet : item.card->road.sheets) {
            if (sheet.level == Level::Edition && under(sheet.at, item.sheet->at))
                out << Item{item.card, &sheet};
        }
        return out;
    }
    return filesOf(item);
}

/// What a card read as a whole holds, folder by folder — for the line of a card whose
/// reading is asked: its counts are what tells two editions from two series at a glance.
QString heldBy(const Card &card)
{
    QList<std::pair<QString, qint64>> folders;
    for (const Manifest::Node &child : card.road.node.children) {
        const qint64 volumes = child.volumes();
        spent().entries += volumes;
        folders << std::pair{child.name, volumes};
    }
    spent().entries += card.road.node.files.size();
    return Words::foldersHeld(folders, card.road.node.files.size());
}

/// A declaration over one the library holds is written only when its box is ticked: the
/// count said `work.json` for a series whose box nobody had ticked, and the import wrote
/// none.
QSet<QString> untickedOf(const Card &card)
{
    QSet<QString> unticked;
    for (const Api::Declaration &one : card.road.declarations) {
        if (!card.road.declaring.contains(one.path))
            unticked.insert(one.path);
    }
    return unticked;
}

/// Whether a rewrite whose `entry.json` is `entryJson` writes one: not when it is the one
/// `sheet` already carries, sent back as it was.
bool rewritesEntry(const QByteArray &entryJson, const Sheet *sheet)
{
    if (entryJson.isEmpty())
        return false;
    if (!sheet || sheet->own.isEmpty())
        return true;
    return entryJson != QJsonDocument(sheet->own).toJson(QJsonDocument::Compact);
}

/// Where a sidecar's name stands in the model's order, top to bottom; after all of them for
/// any other name.
qsizetype rankOf(const QString &name)
{
    static const QStringList Order{u"universe.json"_s, u"work.json"_s, u"edition.json"_s,
                                   u"entry.json"_s, u"oneshot.json"_s};
    const qsizetype at = Order.indexOf(name);
    return at < 0 ? Order.size() : at;
}

/// « Ce qui sera écrit » under the folder at `at` of a card, counted by file name: every
/// sidecar created or changed, the `entry.json` and `oneshot.json` of every rewrite of a
/// file the library does not hold. An `entry.json` sent back as it was is not counted.
QVariantList writesOf(const Card &card, const Facts::Composed &composed, const QString &at)
{
    QHash<QString, QByteArray> before;
    for (const Manifest::Sidecar &one : card.road.tree.sidecars)
        before.insert(one.path, one.json);
    const QSet<QString> unticked = untickedOf(card);
    QHash<QString, int> counts;
    for (const Manifest::Sidecar &one : composed.sidecars) {
        if (!under(parentOf(one.path), at) || unticked.contains(one.path))
            continue;
        if (const auto was = before.constFind(one.path);
            was != before.cend() && *was == one.json)
            continue;
        ++counts[QFileInfo(one.path).fileName()];
    }
    QHash<QString, const Sheet *> sheets;
    for (const Sheet &sheet : card.road.sheets)
        sheets.insert(sheet.at, &sheet);
    for (auto it = composed.rewrites.cbegin(); it != composed.rewrites.cend(); ++it) {
        if (held(card, it.key()) || !under(it.key(), at))
            continue;
        if (rewritesEntry(it->entryJson, sheets.value(it.key())))
            ++counts[u"entry.json"_s];
        if (!it->oneShotJson.isEmpty())
            ++counts[u"oneshot.json"_s];
    }
    // The model's order, top to bottom, then whatever else in the order of its name.
    QStringList names = counts.keys();
    std::ranges::sort(names, [](const QString &one, const QString &other) {
        const qsizetype a = rankOf(one);
        if (const qsizetype b = rankOf(other); a != b)
            return a < b;
        return one < other;
    });
    QVariantList out;
    for (const QString &name : names)
        out << QVariantMap{{u"name"_s, name},
                           {u"count"_s, counts.value(name)},
                           {u"text"_s, Words::writtenTimes(name, counts.value(name))}};
    return out;
}

/// The readings of the folder at `at` of `drop` and why the first, into `sheet`. A reading
/// whose role the menu greys where the folder now stands is left out: offering it would
/// offer a click `Imports::setReading` refuses.
void readingsInto(QVariantMap &sheet, const Shape::Drop &drop, const QString &at,
                  bool nameToGive)
{
    const auto said = drop.suggestions.constFind(at);
    if (said == drop.suggestions.cend())
        return;
    const QList<Shape::Option> options = Shape::options(drop, at);
    QList<int> shown;
    for (int index = 0; index < said->readings.size(); ++index) {
        const Shape::Role role = said->readings.at(index).roles.value(at);
        if (std::ranges::any_of(options, [role](const Shape::Option &one) {
                return one.role == role && one.possible;
            }))
            shown << index;
    }
    QVariantList readings;
    for (const int index : std::as_const(shown)) {
        const Shape::Reading &reading = said->readings.at(index);
        // The folder's own role, and only it: a reading counts the roles below as it read
        // them, and one chosen down there since made every reading of the shelf above look
        // not in force.
        const bool chosen = drop.roles.value(at).role == reading.roles.value(at);
        // The first reading is the one proposed. A name to give is said before « Plus
        // probable »: whichever reading is confirmed, the name is still missing.
        QString badge;
        // Its look, as a number the screen dresses: a name to give is a fault to mend, in
        // the alert's tokens; « Plus probable » is a guess, in a guess's. Told apart here and
        // not by comparing French words in the QML.
        Review::Origin badgeTone = Review::Origin::Guessed;
        if (index == 0 && nameToGive) {
            badge = Words::nameToGive();
            badgeTone = Review::Origin::Missing;
        } else if (index == 0 && shown.size() >= 2) {
            badge = Words::mostLikely();
        }
        readings << QVariantMap{
            {u"index"_s, index},
            {u"label"_s, Words::readingName(reading.roles.value(at), reading.universes,
                                            reading.works, reading.editions)},
            {u"summary"_s, Words::readingSummary(reading.universes, reading.works,
                                                 reading.editions, reading.volumes)},
            {u"chosen"_s, chosen},
            {u"badge"_s, badge},
            {u"badgeTone"_s, std::to_underlying(badgeTone)}};
    }
    QVariantList clues;
    for (const Shape::Clue &clue : said->clues)
        clues << QVariantMap{{u"forIt"_s, clue.forIt}, {u"text"_s, clue.text}};
    sheet.insert(u"readings"_s, readings);
    sheet.insert(u"clues"_s, clues);
    QString cluesTitle;
    if (!clues.isEmpty())
        cluesTitle = shown.size() >= 2 ? Words::whyTheFirst() : Words::why();
    sheet.insert(u"cluesTitle"_s, cluesTitle);
}

/// Where an archive at a universe's root can go: a book, or a volume of a series — greyed,
/// since filing it means moving it.
QVariantList placeOf(const Shape::Drop &drop, const QString &at, const Sheet &sheet)
{
    using enum Shape::Role;
    const QList<Shape::Option> options = Shape::options(drop, at);
    QVariantList out;
    for (const Shape::Role role : {Book, Volume}) {
        const auto option = std::ranges::find(options, role, &Shape::Option::role);
        bool possible = option != options.cend() && option->possible;
        QString detail = option != options.cend() ? option->detail : QString();
        if (role == Book) {
            if (const QString refused = bookRefused(sheet); !refused.isEmpty()) {
                possible = false;
                detail = refused;
            } else if (possible && sheet.roleOrigin != Shape::Origin::Said) {
                detail = Words::declareABookDetail();
            }
        }
        out << QVariantMap{{u"role"_s, std::to_underlying(role)},
                           {u"label"_s, role == Book ? Words::declareABook()
                                                                  : Words::fileInASeries()},
                           {u"detail"_s, detail},
                           {u"chosen"_s, sheet.role == role},
                           {u"possible"_s, possible}};
    }
    return out;
}

/// Whether any role the frame draws is a proposal: its folders' — shelves above the cards
/// included — and, unless `foldersOnly`, its archives at a universe's root.
bool anyProposed(const QList<const Card *> &cards, const Imports *imports, bool foldersOnly)
{
    QSet<quint64> drops;
    for (const Card *card : cards) {
        for (const Sheet &sheet : card->road.sheets) {
            if (const Item item{card, &sheet};
                isFile(sheet.level) && (foldersOnly || !atAUniverseRoot(item)))
                continue;
            if (roleOfSheet(imports, *card, sheet).proposed)
                return true;
        }
        if (shelved(imports, *card))
            drops.insert(card->road.drop);
    }
    for (const quint64 drop : std::as_const(drops)) {
        const Shape::Drop *kept = imports->dropOf(drop);
        for (const QString &at : shelvesOf(cards, drop)) {
            if (roleOfShelf(*kept, at).proposed)
                return true;
        }
    }
    return false;
}

/// The fields whose words the field `key` offers. Whoever writes may draw, and the other way
/// round: « Toriyama » typed under the drawing was offered nothing, having been met only under
/// the writing.
QStringList wordKeysOf(const QString &key)
{
    const bool credit = key == u"authors"_s || key == u"artists"_s;
    return credit ? QStringList{u"authors"_s, u"artists"_s} : QStringList{key};
}

/// Counts the words `sheet` holds under `keys`, folded, into `counts`; a word met for the
/// first time joins `met`, and `spelling` keeps how it was spelt there.
void meetIn(const Sheet &sheet, const QStringList &keys, QHash<QString, int> &counts,
            QHash<QString, QString> &spelling, QStringList &met)
{
    for (const QString &each : keys) {
        for (const QString &word : wordsOf(fieldIn(sheet, each))) {
            const QString folded = Fold::searchKey(word);
            if (!counts.contains(folded)) {
                met << folded;
                spelling.insert(folded, word);
            }
            ++counts[folded];
        }
    }
}

/// The words the field `key` holds anywhere in the frame, the most frequent first and each
/// under the spelling met first: what a list field offers, so that a name typed once on a
/// volume is not typed again, differently, on the next.
///
/// Not kept: every sheet of the frame is walked again for each list field of the sheet shown,
/// four walks of the whole queue each time a sheet is drawn, counted in `Cost::vocabulary`.
///
/// Then the names the library already holds (`library`, `ReviewEdits::setLibraryWords`), each not
/// met in the frame: a series imported among ten others by the same author offered nothing
/// of him, and a name retyped with a typo is a second author.
QStringList vocabularyOf(const QList<const Card *> &cards, const QString &key,
                         const QVariantMap &library)
{
    QHash<QString, int> counts;
    QHash<QString, QString> spelling;
    QStringList met;
    const QStringList keys = wordKeysOf(key);
    for (const Card *card : cards) {
        for (const Sheet &sheet : card->road.sheets) {
            ++spent().vocabulary;
            meetIn(sheet, keys, counts, spelling, met);
        }
    }
    std::ranges::stable_sort(met, [&counts](const QString &one, const QString &other) {
        return counts.value(one) > counts.value(other);
    });
    QStringList out;
    for (const QString &one : std::as_const(met))
        out << spelling.value(one);
    for (const QString &each : keys) {
        for (const QString &word : library.value(each).toStringList()) {
            const QString folded = Fold::searchKey(word);
            if (!counts.contains(folded)) {
                counts.insert(folded, 0);
                out << word;
            }
        }
    }
    return out;
}

/// The reading orders a universe's sheet says, as the server will read them.
std::optional<Orders::Universe> ordersOf(const Sheet &sheet)
{
    const Field *orders = fieldIn(sheet, u"orders"_s);
    if (sheet.level != Level::Universe || !orders || orders->origin == Facts::Origin::Missing)
        return std::nullopt;
    QJsonObject said{{u"orders"_s, orders->value}};
    if (const Field *chosen = fieldIn(sheet, u"defaultOrder"_s);
        chosen && chosen->origin != Facts::Origin::Missing)
        said.insert(u"defaultOrder"_s, chosen->value);
    return Orders::read(said);
}

/// The orders of a universe another one reads: through one of its inclusions, or by writing
/// its steps again.
QSet<int> readByOthers(const Orders::Universe &universe, const Orders::Plan &plan,
                       const QList<Orders::Copy> &copies)
{
    QSet<int> read;
    for (int at = 0; at < universe.orders.size(); ++at) {
        for (int step = 0; step < universe.orders.at(at).steps.size(); ++step) {
            if (const int target = plan.target(at, step); target >= 0 && target != at)
                read.insert(target);
        }
    }
    for (const Orders::Copy &copy : copies) {
        if (copy.of != copy.order)
            read.insert(copy.of);
    }
    return read;
}

/// `{id, name, line, isDefault, subset}` for each order of a universe, and the note saying
/// that one of them writes again what another holds.
std::pair<QVariantList, QString> ordersSaid(const Orders::Universe &universe)
{
    const qint64 before = Orders::cost().looks;
    const Orders::Plan plan(universe);
    const QList<Orders::Copy> copies = Orders::copies(universe);
    const QSet<int> read = readByOthers(universe, plan, copies);
    const auto nameOf = [&universe](int at) {
        const Orders::Order &order = universe.orders.at(at);
        return order.name.isEmpty() ? Orders::declaredId(order, at) : order.name;
    };
    // Flattened as the scan flattens them, in turn and from one allowance: an order the
    // server keeps without steps is counted on the steps it writes, and costs no more.
    Orders::Budget budget;
    QVariantList out;
    for (int at = 0; at < universe.orders.size(); ++at) {
        const Orders::Order &order = universe.orders.at(at);
        const auto written = int(order.steps.size());
        // An order the scan disregards for its id is not flattened there, and costs the
        // others nothing of their allowance.
        const std::optional<QList<Orders::Step>> flat =
            at < Orders::MostOrders && !plan.disregarded(at) ? plan.flatten(at, &budget)
                                                              : std::nullopt;
        const int steps = flat ? int(flat->size()) : written;
        const auto includes = int(std::ranges::count_if(
            order.steps, [](const Orders::Step &step) { return !step.include.isEmpty(); }));
        // What the order does not write itself: the steps its inclusions read in place, or,
        // with none, the steps it writes again from another order.
        int copied = std::max(0, steps - (written - includes));
        if (includes == 0) {
            copied = 0;
            for (const Orders::Copy &copy : copies)
                copied += copy.order == at ? copy.count : 0;
        }
        const QString id = Orders::declaredId(order, at);
        out << QVariantMap{
            {u"id"_s, id},
            {u"name"_s, nameOf(at)},
            {u"line"_s, Words::orderLine(Orders::worksIn(flat.value_or(order.steps)), steps,
                                         copied, includes > 0)},
            {u"isDefault"_s, !universe.defaultOrder.isEmpty() && universe.defaultOrder == id},
            {u"subset"_s, read.contains(at)}};
    }
    spent().orders += Orders::cost().looks - before;
    const QString note = copies.isEmpty()
        ? QString()
        : Words::ordersCopied(nameOf(copies.constFirst().of), nameOf(copies.constFirst().order));
    return {out, note};
}

/// Where the values of the frame come from, as the line under the title names it.
Words::Source sourceOf(const QList<const Card *> &cards)
{
    bool leaf = false;
    bool comicInfo = false;
    for (const Card *card : cards) {
        for (const Sheet &sheet : card->road.sheets) {
            // A file says Leaf when it carries its `entry.json` or its `oneshot.json`; a
            // folder, when something in its sidecar was said there and not typed here.
            if (isFile(sheet.level)) {
                leaf = leaf || !sheet.own.isEmpty() || !sheet.oneShot.isEmpty();
                comicInfo = comicInfo || sheet.comicInfo.carried;
            } else {
                leaf = leaf || std::ranges::any_of(sheet.fields, [](const Field &field) {
                    return field.origin == Facts::Origin::Said && !field.edited;
                });
            }
        }
    }
    if (leaf && comicInfo)
        return Words::Source::Several;
    if (leaf)
        return Words::Source::Leaf;
    if (comicInfo)
        return Words::Source::ComicInfo;
    return Words::Source::Nothing;
}

/// The cards of the tree, in its order, by token: shelves first, then universes, then the
/// series outside any.
QList<quint64> tokensInOrder(const QVariantList &whole)
{
    QList<quint64> out;
    for (const QVariant &one : whole) {
        const QString id = one.toMap().value(u"id"_s).toString();
        if (!id.endsWith(u'/') || id.count(u'/') != 1)
            continue;
        bool ok = false;
        if (const quint64 token = id.chopped(1).toULongLong(&ok); ok && !out.contains(token))
            out << token;
    }
    return out;
}

/// A sheet of `card` that goes and still asks something, its containers or its files: what
/// the frame opens on, and what « Revoir la suivante » goes to.
std::optional<Item> stopOf(const Card &card, bool files)
{
    // Let go, a card asks nothing any more: half an import confirmed, closed and opened
    // again, opened on a series already on its way rather than on what was left.
    if (!Carrier::correctable(card))
        return std::nullopt;
    for (const Sheet &sheet : card.road.sheets) {
        const Item item{&card, &sheet};
        if (isFile(sheet.level) == files && doing(item) && Facts::needsReview(sheet))
            return item;
    }
    return std::nullopt;
}

/// What a card asks first: its refusal, else a container's sheet, else a file's — one
/// value said for a series is said for every volume in it. Nothing when it asks nothing.
std::optional<Item> firstToReview(const Card &card)
{
    if (card.stage == Card::Stage::Failed)
        return Item{&card, &card.road.sheets.constFirst()};
    if (std::optional<Item> container = stopOf(card, false))
        return container;
    return stopOf(card, true);
}

/// The node and the sheet the frame opens on for `item`.
std::pair<QString, QString> placeOfReview(const Imports *imports, const Item &item)
{
    const Card &card = *item.card;
    const Sheet &sheet = *item.sheet;
    QString node = WholeNode;
    if (!isFile(sheet.level)) {
        node = inTheRail(item) ? idOf(item) : WholeNode;
        // A card whose own reading is asked opens among the cards of its shelf, its readings
        // side by side: opened on its files, the list showed volumes while the question was
        // what the folder is.
        if (sheet.at.isEmpty() && shelved(imports, card)
            && asksItsReading(roleOfSheet(imports, card, sheet)))
            node = shelfIdOf(card.road.drop, parentOf(card.road.within));
    } else {
        // The list is the series the file belongs to, and the sheet the file — or the
        // universe, for an archive at its root.
        if (atAUniverseRoot(item))
            node = idOf(Item{&card, &card.road.sheets.constFirst()});
        for (const Item &series : seriesOf(card)) {
            if (under(sheet.at, series.sheet->at))
                node = idOf(series);
        }
    }
    return {node, idOf(item)};
}

} // namespace

Review::Cost Review::cost()
{
    return spent();
}

Review *Review::create(QQmlEngine *engine, QJSEngine *)
{
    auto *imports = engine->singletonInstance<Imports *>(qmlTypeId("Leaf", 1, 0, "Imports"));
    if (!imports) {
        qWarning().noquote()
            << QStringLiteral("error resolving the Imports singleton — the checking frame will "
                              "show nothing");
    }
    return new Review(imports);
}

Review::Review(Imports *imports, QObject *parent)
    : QObject(parent)
    , m_view(new ReviewView(this, m_frame.get()))
    , m_ticks(new ReviewTicks(this, m_frame.get()))
    , m_edits(new ReviewEdits(this, m_frame.get()))
{
    m_frame->imports = imports;
    // First, so that whatever answers `changed` after it reads the frame anew — the parts
    // included, which say it after the frame has forgotten what it read.
    connect(this, &Review::changed, this, [this] {
        m_frame->stateCache.reset();
        m_frame->creditsCache.clear();
    });
    connect(this, &Review::changed, m_view, &ReviewView::changed);
    connect(this, &Review::changed, m_ticks, &ReviewTicks::changed);
    // Recomputed on both: `changed` is the queue's, and `dataChanged` is what a card alone
    // emits when one of its values moves without the queue's counts moving with it.
    if (!imports)
        return;
    connect(imports, &Imports::changed, this, [this] {
        // A doubt typed against a card that is gone would never be read again — a card
        // redropped has a new token — and would only grow the table.
        QSet<QString> alive;
        for (const Card &card : m_frame->imports->cards())
            alive.insert(QString::number(card.token));
        m_frame->doubts.removeIf([&alive](const auto &one) {
            return !alive.contains(one.key().section(u'/', 0, 0));
        });
        m_frame->ticked.removeIf(
            [&alive](const QString &id) { return !alive.contains(id.section(u'/', 0, 0)); });
        emit changed();
    });
    // Only the roles the frame reads. A chunk landing says `Sent` (two thousand times for a
    // 7.9 GiB folder), the hash tick `Checking`, the retry countdown `RetryIn` each second,
    // a finished file `Nodes`, the count of a line `Progress`: answered by rebuilding the tree, the list and a composition of
    // every card each time, the frame would have redone all of it per chunk. No roles means
    // every role.
    connect(imports, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex &, const QModelIndex &, const QList<int> &roles) {
                using enum Imports::Role;
                static const QSet Unread{std::to_underlying(Sent), std::to_underlying(Checking),
                                         std::to_underlying(RetryIn), std::to_underlying(Progress),
                                         std::to_underlying(Nodes)};
                if (roles.isEmpty() || std::ranges::any_of(roles, [](int role) {
                        return !Unread.contains(role);
                    }))
                    emit changed();
            });
}

ReviewView::ReviewView(Review *review, Review::Frame *frame)
    : QObject(review)
    , m_review(review)
    , m_frame(frame)
{
}

ReviewTicks::ReviewTicks(Review *review, Review::Frame *frame)
    : QObject(review)
    , m_review(review)
    , m_frame(frame)
{
}

ReviewEdits::ReviewEdits(Review *review, Review::Frame *frame)
    : QObject(review)
    , m_review(review)
    , m_frame(frame)
{
}

const Review::Frame::State &Review::Frame::state() const
{
    if (!stateCache) {
        ++spent().proposals;
        const QList<const Card *> cards = cardsOf(imports);
        State now;
        now.foldersProposed = anyProposed(cards, imports, true);
        now.anyProposed = now.foldersProposed || anyProposed(cards, imports, false);
        now.readings = imports ? imports->readings() : 0;
        for (const Card *card : cards) {
            if (!isUniverseCard(*card))
                continue;
            if (const auto orders = ordersOf(card->road.sheets.constFirst()))
                now.orders += int(orders->orders.size());
        }
        stateCache = now;
    }
    return *stateCache;
}

Facts::Composed Review::Frame::composedOf(const Card &card) const
{
    state();
    auto found = stateCache->composed.find(card.token);
    if (found == stateCache->composed.end()) {
        ++spent().composes;
        found = stateCache->composed.insert(card.token,
                                         Facts::compose(card.road.sheets, card.road.tree.sidecars));
    }
    return *found;
}

namespace {

/// The series a file at `path` is credited to: the nearest of `works` above it, or the card's
/// root.
QString ownerOf(const QSet<QString> &works, const QString &path)
{
    for (QString above = parentOf(path);; above = parentOf(above)) {
        if (works.contains(above) || above.isEmpty())
            return above;
    }
}

/// The authors and the artists `volume` credits, folded; each name `met` for the first time
/// joins `all`, as it is spelt there.
std::pair<QSet<QString>, QSet<QString>> creditedBy(const Sheet &volume, QSet<QString> &met,
                                                   QStringList &all)
{
    std::pair<QSet<QString>, QSet<QString>> said;
    for (const QString &which : {u"authors"_s, u"artists"_s}) {
        QSet<QString> &into = which == u"authors"_s ? said.first : said.second;
        for (const QString &name : Facts::creditedWith(volume, which)) {
            const QString folded = Fold::searchKey(name);
            into.insert(folded);
            if (!met.contains(folded)) {
                met.insert(folded);
                all << name;
            }
        }
    }
    return said;
}

} // namespace

Review::Frame::Credits Review::Frame::creditsOf(quint64 token, const QString &at) const
{
    const QString key = QString::number(token) + u'/' + at;
    if (const auto found = creditsCache.constFind(key); found != creditsCache.cend())
        return *found;
    Credits credits;
    const QList<const Card *> cards = cardsOf(imports);
    const auto card = std::ranges::find(cards, token, &Card::token);
    const Sheet *series = nullptr;
    if (card != cards.cend()) {
        const auto found = std::ranges::find((*card)->road.sheets, at, &Sheet::at);
        if (found != (*card)->road.sheets.cend() && found->level == Level::Work)
            series = std::to_address(found);
    }
    if (!series
        || (series->credits.credited.authors.isEmpty()
            && series->credits.credited.artists.isEmpty()))
        return *creditsCache.insert(key, credits);
    // `Facts::weighCredits`' rules: a volume or a chapter that is read, credited to the
    // nearest series above it.
    QSet<QString> works;
    for (const Sheet &one : (*card)->road.sheets) {
        if (one.level == Level::Work)
            works.insert(one.at);
    }
    QList<const Sheet *> volumes;
    for (const Sheet &one : (*card)->road.sheets) {
        ++spent().credits;
        if ((one.level == Level::Volume || one.level == Level::Chapter)
            && one.role != Shape::Role::Unread && ownerOf(works, one.at) == at)
            volumes << &one;
    }
    // In the server's order, so that a person named two ways keeps the spelling it keeps.
    std::ranges::stable_sort(volumes, [&at](const Sheet *one, const Sheet *other) {
        return Facts::readBefore(at, one->at, other->at);
    });
    QSet<QString> met;
    std::optional<std::pair<QSet<QString>, QSet<QString>>> first;
    for (const Sheet *volume : std::as_const(volumes)) {
        ++credits.volumes;
        const std::pair<QSet<QString>, QSet<QString>> said = creditedBy(*volume, met, credits.all);
        if (!first)
            first = said;
        else if (*first != said)
            credits.alike = false;
    }
    return *creditsCache.insert(key, credits);
}

std::optional<Review::Frame::Credits> Review::Frame::creditsShown() const
{
    const Item item = itemOf(cardsOf(imports), node);
    if (!item.sheet || item.sheet->level != Level::Work)
        return std::nullopt;
    Credits credits = creditsOf(item.card->token, item.sheet->at);
    if (credits.all.isEmpty() || credits.alike)
        return std::nullopt;
    return credits;
}

const Card *Review::Frame::aimed() const
{
    const QList<const Card *> cards = cardsOf(imports);
    return cards.size() == 1 && cards.constFirst()->road.target ? cards.constFirst() : nullptr;
}

QString Review::title() const
{
    const Card *card = m_frame->aimed();
    return card ? Aimed::title(*card) : QString();
}

bool ReviewEdits::choose(const QString &id, const QString &choice)
{
    const Card *card = m_frame->aimed();
    if (!card && m_frame->imports) {
        // A series the library already holds a look-alike of, met without a target: reimport
        // onto the one held, or create a second.
        const Item item = itemOf(cardsOf(m_frame->imports), id);
        const Api::Resemblance *like = item.sheet ? resemblanceAt(*item.card, item.sheet->at)
                                                  : nullptr;
        if (!like)
            return false;
        if (choice == u"onto"_s)
            return m_frame->imports->retarget(item.card->token, like->at, u"SERIES"_s,
                                              like->seriesId);
        if (choice == u"edition"_s)
            return m_frame->imports->retarget(item.card->token, like->at, u"SERIES_NEW_EDITIONS"_s,
                                       like->seriesId);
        if (choice != u"second"_s || !m_frame->imports->chooseSecond(item.card->token, like->at))
            return false;
        emit m_review->changed();
        return true;
    }
    if (!card || !m_frame->imports)
        return false;
    if (const QString work = Aimed::moveWorkId(*card, id); !work.isEmpty()) {
        if (choice != u"move"_s && choice != u"second"_s)
            return false;
        m_frame->imports->setFiling(int(card - &m_frame->imports->cards().constFirst()), work,
                             choice == u"move"_s);
        emit m_review->changed();
        return true;
    }
    std::optional<double> number;
    if (choice == u"aimed"_s)
        number = Aimed::aimedNumber(*card);
    else if (choice == u"own"_s)
        number = Aimed::ownNumber(*card, id);
    return number && m_frame->imports->edit(card->token, id, u"number"_s, *number);
}

bool ReviewEdits::setSwitch(const QString &id, bool on)
{
    const Card *card = m_frame->aimed();
    if (!card || !m_frame->imports)
        return false;
    m_frame->imports->setReplacing(int(card - &m_frame->imports->cards().constFirst()), id, on);
    emit m_review->changed();
    return true;
}

bool ReviewEdits::setSplice(const QString &id, int from, int to)
{
    const Card *card = m_frame->aimed();
    if (!card || !m_frame->imports)
        return false;
    Q_UNUSED(id)
    // Pages as the screen counts them, one more than the contract.
    const quint64 token = card->token;
    return m_frame->imports->setSplice(token, from - 1, to - 1)
           && m_frame->imports->askAgain(token);
}

bool ReviewEdits::setReset(const QString &id, bool reset)
{
    if (const Card *card = m_frame->aimed();
        !card || !m_frame->imports || !m_frame->imports->setResetting(card->token, id, reset))
        return false;
    emit m_review->changed();
    return true;
}

bool ReviewEdits::setKeep(const QString &id, const QString &key, bool keep)
{
    const Card *card = m_frame->aimed();
    return card && m_frame->imports && m_frame->imports->keep(card->token, id, key, keep);
}

QString Review::subtitle() const
{
    if (const Card *card = m_frame->aimed())
        return Aimed::subtitle(*card);
    const QList<const Card *> cards = cardsOf(m_frame->imports);
    if (cards.isEmpty())
        return {};
    int universes = 0;
    int series = 0;
    int files = 0;
    int atRoot = 0;
    QSet<quint64> drops;
    for (const Card *card : cards) {
        universes += isUniverseCard(*card) ? 1 : 0;
        series += int(seriesOf(*card).size());
        files += int(card->road.tree.files.size());
        atRoot += int(rootArchivesOf(*card).size());
        // A card of no drop is a folder of its own.
        drops.insert(card->road.drop != 0 ? card->road.drop : ~card->token);
    }
    const Words::Source source = sourceOf(cards);
    const QString summary = [&] {
        // While a folder's role is a guess, its universes and series are guesses too: the
        // line counts what was dropped, and leaves the rest to the tree.
        if (m_frame->state().foldersProposed)
            return Words::dropSummary(int(drops.size()), files, source);
        if (atRoot > 0)
            return Words::rootSummary(universes, series, atRoot);
        if (m_frame->state().orders > 0)
            return Words::ordersSummary(universes, series, m_frame->state().orders);
        return Words::apportSummary(universes, series, files, source);
    }();
    // Opened while fifty folders are walked, the frame says how far the walk has come; once
    // it is over, the count would answer a question nobody asks any more.
    if (const int toRead = m_frame->imports->foldersToRead(); toRead > 0) {
        const int read = m_frame->imports->foldersRead();
        return Words::whileRead(summary, read, read + toRead);
    }
    return summary;
}

namespace {

/// A node of the tree. `level` is the sign of the node's level, or -1 for what is no level of
/// the model: the whole drop, « Sans univers », a shelf.
struct RailNode {
    QString id;
    int level = -1;
    QString name;
    int depth = 0;
    qsizetype count = 0;
    bool expandable = false;
};

/// One line of the tree. `changeable`: whether another role can be chosen there, asked here
/// once per line rather than by each chip of the screen on every rebuild of the tree.
QVariantMap railLine(const RailNode &node, bool expanded, bool selected, const RoleSaid &role,
                     const RoleChange &change)
{
    return QVariantMap{{u"id"_s, node.id},
                       {u"level"_s, node.level},
                       {u"name"_s, node.name},
                       {u"depth"_s, node.depth},
                       {u"count"_s, int(node.count)},
                       {u"expandable"_s, node.expandable},
                       {u"expanded"_s, expanded},
                       {u"selected"_s, selected},
                       {u"role"_s, role.role},
                       {u"roleLabel"_s, role.label},
                       {u"roleTone"_s, std::to_underlying(role.tone)},
                       {u"changeable"_s, change.changeable},
                       {u"rolesGreyed"_s, change.greyed},
                       {u"roleSpoken"_s, role.label.isEmpty()
                                             ? QString()
                                             : Words::roleSpoken(node.name, role.label)},
                       // Drawn in the alert's tokens, as its chip: the scan will not read it.
                       {u"unread"_s, role.role == std::to_underlying(Shape::Role::Unread)}};
}

/// What the chip of a card's node says of its roles.
RoleChange changeAt(const Imports *imports, const Item &item)
{
    const Shape::Drop *drop = dropOfCard(imports, *item.card);
    if (!drop)
        return {};
    // Any role chosen inside a card changes that card: one that cannot be corrected
    // now takes none, which its menu says role by role (`waiting`) — and its chip
    // says the same, tried only then, since every other card takes them all.
    const QString at = inTheDrop(item.card->road.within, item.sheet->at);
    const QVariantList options = optionsAt(*drop, at, item.sheet);
    return changeOf(Carrier::correctable(*item.card)
                        ? options
                        : waiting(options, imports, item.card->road.drop, at));
}

/// The drops whose cards hang under a shelf, in the order of their cards.
QList<quint64> shelvedDrops(const Imports *imports, const QList<const Card *> &cards)
{
    QList<quint64> drops;
    for (const Card *card : cards) {
        if (shelved(imports, *card) && !drops.contains(card->road.drop))
            drops << card->road.drop;
    }
    return drops;
}

/// The cards of the drop `token` that sit under a shelf of it, by their place in it.
QHash<QString, const Card *> cardsWithin(const QList<const Card *> &cards, quint64 token)
{
    QHash<QString, const Card *> byWithin;
    for (const Card *card : cards) {
        if (card->road.drop == token && !card->road.within.isEmpty())
            byWithin.insert(card->road.within, card);
    }
    return byWithin;
}

/// Each card's series counted once, up its shelves: each shelf walking every card of the
/// drop was a hundred shelves reading a thousand cards a hundred times.
QHash<QString, int> seriesUnderShelves(const QHash<QString, const Card *> &byWithin)
{
    QHash<QString, int> seriesUnder;
    for (auto it = byWithin.cbegin(); it != byWithin.cend(); ++it) {
        const auto series = int(seriesOf(**it).size());
        for (QString at = parentOf(it.key());; at = parentOf(at)) {
            ++spent().looks;
            seriesUnder[at] += series;
            if (at.isEmpty())
                break;
        }
    }
    return seriesUnder;
}

/// The shelves of one drop, as the tree draws them.
struct Shelves {
    quint64 token = 0;
    const Shape::Drop *drop = nullptr;
    QSet<QString> at;
    QHash<QString, const Card *> byWithin;
    QHash<QString, int> seriesUnder;
    /// Whether a role can wait at all: with every card of the drop correctable, none does,
    /// and no shelf asks `roleWaits` of each of its roles.
    bool settled = false;
};

/// The tree as `Review::rail` draws it, line by line.
class Rail
{
public:
    Rail(const Imports *imports, const QSet<QString> &folded, const QString &node)
        : m_imports(imports)
        , m_folded(folded)
        , m_node(node)
    {
    }

    const QVariantList &lines() const { return m_out; }

    bool unfolded(const QString &id) const { return !m_folded.contains(id); }

    void line(const RailNode &node, const RoleSaid &role, const RoleChange &change = {})
    {
        m_out << railLine(node, node.expandable && !m_folded.contains(node.id), node.id == m_node,
                        role, change);
    }

    void addSeries(const Item &series, int depth)
    {
        const QList<Item> editions = editionsOf(series);
        addItem(series, depth, filesOf(series).size(), !editions.isEmpty());
        if (!unfolded(idOf(series)))
            return;
        for (const Item &edition : editions)
            addItem(edition, depth + 1, filesOf(edition).size(), false);
    }

    /// Its archives at its root first, as its list puts them: they are what it asks about.
    void addUniverse(const Card *card, int depth)
    {
        const Item universe{card, &card->road.sheets.constFirst()};
        const QList<Item> series = seriesOf(*card);
        const QList<Item> atRoot = rootArchivesOf(*card);
        addItem(universe, depth, series.size(), !series.isEmpty() || !atRoot.isEmpty());
        if (!unfolded(idOf(universe)))
            return;
        for (const Item &one : atRoot)
            addItem(one, depth + 1, 0, false);
        for (const Item &one : series)
            addSeries(one, depth + 1);
    }

    /// A card dropped inside a shelf hangs under it, in the order of the folders on disk,
    /// rather than under « Sans univers »: the shelf is where its role is read from.
    void addShelves(const QList<const Card *> &cards, quint64 token)
    {
        Shelves shelves{token, m_imports->dropOf(token), shelvesOf(cards, token),
                        cardsWithin(cards, token), {}, false};
        shelves.seriesUnder = seriesUnderShelves(shelves.byWithin);
        shelves.settled = std::ranges::all_of(m_imports->cards(), [token](const Card &one) {
            return !one.folder || one.road.drop != token || Carrier::correctable(one);
        });
        addShelf(shelves, shelves.drop->whole, 1);
    }

private:
    void addItem(const Item &item, int depth, qsizetype count, bool expandable)
    {
        line({idOf(item), std::to_underlying(item.sheet->level), nameOf(*item.sheet), depth, count,
              expandable},
             roleOfSheet(m_imports, *item.card, *item.sheet), changeAt(m_imports, item));
    }

    void addCard(const Card *card, int depth)
    {
        if (isUniverseCard(*card))
            addUniverse(card, depth);
        else
            addSeries(Item{card, &card->road.sheets.constFirst()}, depth);
    }

    void addShelf(const Shelves &shelves, const Manifest::Node &folder, int depth)
    {
        const QString id = shelfIdOf(shelves.token, folder.at);
        const QVariantList options = optionsAt(*shelves.drop, folder.at, nullptr);
        line({id, -1, folder.name, depth, shelves.seriesUnder.value(folder.at), true},
             roleOfShelf(*shelves.drop, folder.at),
             changeOf(shelves.settled ? options
                                      : waiting(options, m_imports, shelves.token, folder.at)));
        if (!unfolded(id))
            return;
        for (const Manifest::Node &child : folder.children) {
            if (const Card *card = shelves.byWithin.value(child.at))
                addCard(card, depth + 1);
            else if (shelves.at.contains(child.at))
                addShelf(shelves, child, depth + 1);
        }
    }

    const Imports *m_imports;
    QSet<QString> m_folded;
    QString m_node;
    QVariantList m_out;
};

} // namespace

QVariantList Review::rail() const
{
    if (const Card *card = m_frame->aimed())
        return Aimed::rail(*card);
    const QList<const Card *> cards = cardsOf(m_frame->imports);
    if (cards.isEmpty())
        return {};
    Rail tree(m_frame->imports, m_frame->folded, m_frame->node);

    int allSeries = 0;
    for (const Card *card : cards)
        allSeries += int(seriesOf(*card).size());
    tree.line({WholeNode, -1, Words::wholeApport(), 0, allSeries, true}, {});
    if (!tree.unfolded(WholeNode))
        return tree.lines();

    for (const quint64 token : shelvedDrops(m_frame->imports, cards))
        tree.addShelves(cards, token);

    QList<const Card *> loose;
    for (const Card *card : cards) {
        if (shelved(m_frame->imports, *card))
            continue;
        if (isUniverseCard(*card))
            tree.addUniverse(card, 1);
        else
            loose << card;
    }
    if (loose.isEmpty())
        return tree.lines();
    tree.line({Loose, -1, Words::withoutUniverse(), 1, loose.size(), true}, {});
    if (!tree.unfolded(Loose))
        return tree.lines();
    for (const Card *card : loose)
        tree.addSeries(Item{card, &card->road.sheets.constFirst()}, 2);
    return tree.lines();
}

namespace {

/// One line of the list, for `item` in `group`: `credits` when the series shown credits its
/// volumes apart, `checked` and `selected` as the frame has them.
QVariantMap listLine(const Imports *imports, const Item &item, Filter group, Weights &weights,
                     bool credits, bool checked, bool selected)
{
    auto [chip, said] = chipOf(*item.sheet);
    auto origin = std::to_underlying(said);
    QString detail;
    // A detail that says a file will not be read is said in the alert's ink: in the
    // quiet one of every other line, nothing told it from a size.
    bool alarming = false;
    const RoleSaid role = roleOfSheet(imports, *item.card, *item.sheet);
    if (item.card->stage == Card::Stage::Failed) {
        // Its values go nowhere: what the line says is why the card stays.
        chip = Words::failedChip();
        origin = std::to_underlying(Review::Origin::Missing);
        detail = item.card->trouble;
        alarming = true;
    } else if (item.sheet->role == Shape::Role::Unread) {
        // Its values are questions about a file the library will never show: what it is
        // asked is its role, and the line says so instead.
        chip = Words::role(Shape::Role::Unread);
        origin = std::to_underlying(Review::Origin::Missing);
        detail = Words::noRoleNotRead();
        alarming = true;
    } else if (item.sheet->at.isEmpty() && !isFile(item.sheet->level)
               && asksItsReading(role)) {
        chip = Words::readingChip(item.sheet->role,
                                  role.asked.value_or(Shape::Confidence::Certain),
                                  role.nameMissing);
        origin = std::to_underlying(role.tone);
        detail = heldBy(*item.card);
    } else if (const Api::Resemblance *like = resemblanceAt(*item.card, item.sheet->at)) {
        // A look-alike of a series held: asked about before anything is created.
        chip = Words::seriesExistsChip();
        origin = std::to_underlying(Review::Origin::Missing);
        detail = Words::alreadyInLibrary(like->name);
        alarming = true;
    } else if (item.sheet->level == Level::Work && !editionsOf(item).isEmpty()) {
        // How many editions the series has before how many volumes: 76 volumes read as one
        // long edition when they are two.
        detail = Words::seriesHolds(editionsOf(item).size(), filesOf(item).size(), weights.of(item));
    } else if (const QStringList authors = wordsOf(fieldIn(*item.sheet, u"authors"_s)),
               artists = wordsOf(fieldIn(*item.sheet, u"artists"_s));
               credits && isFile(item.sheet->level)
               && (!authors.isEmpty() || !artists.isEmpty())) {
        detail = Words::creditsLine(authors, artists);
    } else {
        detail = isFile(item.sheet->level)
            ? Words::fileDetail(item.sheet->name, weights.of(item))
            : Words::nodeHolds(filesOf(item).size(), weights.of(item));
    }
    return QVariantMap{{u"header"_s, false},
                       {u"id"_s, idOf(item)},
                       {u"name"_s, nameOf(*item.sheet)},
                       {u"detail"_s, detail},
                       {u"alarming"_s, alarming},
                       {u"chip"_s, chip},
                       {u"origin"_s, origin},
                       {u"group"_s, std::to_underlying(group)},
                       // A volume with a number that can be corrected is dragged to a place:
                       // its number follows.
                       {u"draggable"_s,
                        isFile(item.sheet->level) && Carrier::correctable(*item.card)
                            && item.sheet->trouble.isEmpty()
                            && [&] {
                                   const Field *n = fieldIn(*item.sheet, u"number"_s);
                                   return n && n->origin != Facts::Origin::Missing
                                          && n->value.isDouble();
                               }()},
                       // A card refused is taken away, never ticked to go.
                       {u"checkable"_s, tickable(item)},
                       {u"checked"_s, checked},
                       // The line whose sheet is shown, lit as the tree's node is.
                       {u"selected"_s, selected}};
}

/// The lines of `items` that fall in `group`, but a universe's archives at its root, which
/// stand in a group of their own.
QList<Item> inGroupOf(const QList<Item> &items, const QList<Filter> &groups, Filter group)
{
    QList<Item> inGroup;
    for (qsizetype at = 0; at < items.size(); ++at) {
        if (groups.at(at) == group && !atAUniverseRoot(items.at(at)))
            inGroup << items.at(at);
    }
    return inGroup;
}

/// The header over the `inGroup` lines of `group`, under the filter `filter`.
QVariantMap groupHeader(Filter group, const QList<Item> &inGroup, Filter filter,
                        bool showIdentical, Weights &weights)
{
    const auto which = Words::Reviewed(std::to_underlying(group));
    QVariantMap header{{u"header"_s, true}, {u"group"_s, std::to_underlying(group)}};
    if (group == Filter::Identical) {
        qint64 spared = 0;
        for (const Item &item : inGroup)
            spared += weights.of(item);
        header.insert(u"text"_s, Words::identicalGroup(int(inGroup.size()), spared));
        QString action;
        if (filter != Filter::Identical)
            action = showIdentical ? Words::hideIt() : Words::showIt();
        header.insert(u"action"_s, action);
    } else {
        header.insert(u"text"_s, Words::reviewGroup(which, int(inGroup.size())));
        header.insert(u"action"_s, QString());
    }
    return header;
}

/// A file's number, as the list sorts its volumes by; nothing for a container, or a file
/// without one.
std::optional<double> volumeNumberOf(const Item &item)
{
    if (!isFile(item.sheet->level))
        return std::nullopt;
    const Field *field = fieldIn(*item.sheet, u"number"_s);
    if (!field || field->origin == Facts::Origin::Missing || !field->value.isDouble())
        return std::nullopt;
    return field->value.toDouble();
}

/// Volumes are read in the order of their numbers: renumbered by a drag, they must stand
/// where the drag put them, and not where their files happen to sort. A file with no number
/// keeps its place after those that have one.
void sortByNumber(QList<Item> &items)
{
    std::ranges::stable_sort(items, [](const Item &one, const Item &other) {
        const auto first = volumeNumberOf(one);
        const auto second = volumeNumberOf(other);
        return first && second ? *first < *second : first.has_value() && !second.has_value();
    });
}

} // namespace

QVariantList Review::rows() const
{
    if (const Card *card = m_frame->aimed())
        return Aimed::rows(*card, std::to_underlying(m_frame->filter), m_frame->showIdentical,
                           m_frame->focus, m_frame->searchKey);
    ++spent().rows;
    const QList<Item> items = shownIn(*m_frame);
    QList<Filter> groups;
    groups.reserve(items.size());
    for (const Item &item : items)
        groups << groupOf(item);
    Weights weights;
    QVariantList out;
    // A series whose volumes are credited apart shows each volume's credits on its line:
    // the one place where « Auteur F drew only volume 4 » can be read without opening nine
    // sheets.
    const bool credits = m_frame->creditsShown().has_value();

    const auto lineOf = [&](const Item &item, Filter group) {
        return listLine(m_frame->imports, item, group, weights, credits,
                        m_frame->ticked.contains(idOf(item)), idOf(item) == m_frame->focus);
    };

    // A universe's archives at its root come first, in a group of their own: what the scan
    // makes of each is a question of its own, and folded among the series it was the line
    // nobody reached. Counted in the filters like any other line, and kept to the filter.
    QList<qsizetype> atRoot;
    for (qsizetype at = 0; at < items.size(); ++at) {
        if (atAUniverseRoot(items.at(at))
            && (m_frame->filter == Filter::All || m_frame->filter == groups.at(at)))
            atRoot << at;
    }
    if (!atRoot.isEmpty()) {
        // No group of the four: -1, as the tree's nodes that are no level.
        out << QVariantMap{{u"header"_s, true},
                           {u"group"_s, -1},
                           {u"text"_s, Words::atTheRoot(int(atRoot.size()))},
                           {u"action"_s, QString()}};
        for (const qsizetype at : std::as_const(atRoot))
            out << lineOf(items.at(at), groups.at(at));
    }

    // A universe lists its series once each, grouped by what is to be done about them like
    // any other list: grouped by tag, a series carrying five tags stood in the list five times,
    // each a line of the same series.
    for (const Filter group : {Filter::ToReview, Filter::Ready, Filter::Identical}) {
        if (m_frame->filter != Filter::All && m_frame->filter != group)
            continue;
        QList<Item> inGroup = inGroupOf(items, groups, group);
        if (inGroup.isEmpty())
            continue;
        // Folded unless asked for, or unless the filter asks for nothing else: « Identiques »
        // chosen and answered by one folded line would be a filter that shows nothing.
        const bool shown = group != Filter::Identical || m_frame->showIdentical
                           || m_frame->filter == Filter::Identical;
        out << groupHeader(group, inGroup, m_frame->filter, m_frame->showIdentical, weights);
        if (!shown)
            continue;
        sortByNumber(inGroup);
        for (const Item &item : inGroup)
            out << lineOf(item, group);
    }
    return out;
}

QVariantList Review::counts() const
{
    if (const Card *card = m_frame->aimed())
        return Aimed::counts(*card, std::to_underlying(m_frame->filter));
    using enum Filter;
    const QList<Item> items = shownIn(*m_frame);
    QVariantList out;
    for (const Filter which : {All, ToReview, Ready, Identical}) {
        const qsizetype count = which == All
            ? items.size()
            : std::ranges::count_if(items,
                                    [which](const Item &item) { return groupOf(item) == which; });
        out << QVariantMap{{u"label"_s, Words::reviewFilter(Words::Reviewed(std::to_underlying(which)))},
                           {u"count"_s, int(count)},
                           {u"lit"_s, which == m_frame->filter}};
    }
    return out;
}

void ReviewView::setSearch(const QString &text)
{
    m_frame->search = text;
    const QString key = Fold::searchKey(text);
    if (key == m_frame->searchKey)
        return;
    m_frame->searchKey = key;
    m_frame->untickAll();
    emit m_review->changed();
}

void ReviewView::setFilter(int filter)
{
    const auto chosen = Filter(std::clamp(filter, std::to_underlying(Filter::All),
                                          std::to_underlying(Filter::Identical)));
    if (chosen == m_frame->filter)
        return;
    m_frame->filter = chosen;
    // What a value is applied to is what is seen ticked: lines ticked under another filter
    // would be written out of sight.
    m_frame->untickAll();
    emit m_review->changed();
}

QString Review::listTitle() const
{
    if (const Card *card = m_frame->aimed())
        return Aimed::listTitle(*card);
    if (m_frame->node == WholeNode)
        return Words::wholeApport();
    if (m_frame->node == Loose)
        return Words::withoutUniverse();
    if (const auto shelf = shelfOf(m_frame->node)) {
        const Shape::Drop *drop =
            m_frame->imports ? m_frame->imports->dropOf(shelf->first) : nullptr;
        const Manifest::Node *folder = drop ? nodeIn(drop->whole, shelf->second) : nullptr;
        return folder ? Words::insideOf(folder->name) : QString();
    }
    const Item item = itemOf(cardsOf(m_frame->imports), m_frame->node);
    if (!item.sheet)
        return {};
    if (item.sheet->level == Level::Universe)
        return Words::seriesOfTheUniverse();
    if (m_frame->creditsShown())
        return Words::volumesCredited(nameOf(*item.sheet));
    return Words::volumesOf(nameOf(*item.sheet));
}

QString Review::railTitle() const
{
    if (m_frame->aimed())
        return Words::aimedRail();
    return m_frame->state().anyProposed ? Words::proposedApport() : Words::apport();
}

QString Review::railHint() const
{
    if (m_frame->aimed())
        return {};
    return m_frame->state().anyProposed ? Words::roleHint() : QString();
}

QVariantList Review::editionRows() const
{
    const Card *card = m_frame->aimed();
    return card ? Aimed::editionRows(*card) : QVariantList();
}

QString Review::editionNameHelp() const
{
    return Words::editionNameHelp();
}

bool ReviewEdits::setCover(const QString &at, const QString &fileUrl)
{
    const Card *card = m_frame->aimed();
    if (!card)
        card = itemOf(cardsOf(m_frame->imports), m_frame->focus).card;
    return card && m_frame->imports
           && m_frame->imports->setCover(card->token, at, QUrl(fileUrl).toLocalFile());
}

bool ReviewEdits::setEditionName(const QString &path, const QString &name)
{
    const Card *card = m_frame->aimed();
    return card && m_frame->imports && m_frame->imports->setEditionName(card->token, path, name);
}

QString Review::railNote() const
{
    if (const Card *card = m_frame->aimed())
        return Aimed::railNote(*card);
    const QList<const Card *> cards = cardsOf(m_frame->imports);
    if (cards.isEmpty())
        return {};
    // An archive at a universe's root that is not said a book is the one thing there the
    // scan would leave unread, and the rule is said once beside the filters.
    for (const Card *card : cards) {
        for (const Item &one : rootArchivesOf(*card)) {
            if (one.sheet->roleOrigin != Shape::Origin::Said)
                return Words::onlyABookAtTheRoot();
        }
    }
    if (sourceOf(cards) == Words::Source::Nothing && m_frame->state().foldersProposed)
        return Words::nothingSaid();
    int converted = 0;
    bool allSaid = true;
    for (const Card *card : cards) {
        const Facts::Composed composed = m_frame->composedOf(*card);
        for (auto it = composed.rewrites.cbegin(); it != composed.rewrites.cend(); ++it) {
            if (!held(*card, it.key()) && it.value().remove.contains(ComicInfoXml))
                ++converted;
        }
        allSaid = allSaid && std::ranges::none_of(card->road.sheets, [](const Sheet &sheet) {
            return Facts::needsReview(sheet)
                   || std::ranges::any_of(sheet.fields, [](const Field &field) {
                          return field.origin == Facts::Origin::Taken
                                 || field.origin == Facts::Origin::Guessed;
                      });
        });
    }
    if (converted > 0)
        return Words::comicInfoConverted(converted);
    return allSaid ? Words::everythingSaid() : QString();
}

QString Review::note() const
{
    if (const Card *card = m_frame->aimed())
        return Aimed::note(*card);
    // Over cards rather than one series, what its identical lines spare: folded in their
    // group, fifteen series the library already holds said nothing of their weight.
    if (m_frame->node == WholeNode || m_frame->node == Loose || shelfOf(m_frame->node)) {
        int identical = 0;
        qint64 bytes = 0;
        Weights weights;
        const QList<const Card *> cards = cardsOf(m_frame->imports);
        for (const Item &item : contentOf(cards, m_frame->node, m_frame->imports)) {
            if (groupOf(item) != Filter::Identical)
                continue;
            ++identical;
            bytes += weights.of(item);
        }
        return identical > 0 ? Words::identicalSpared(identical, bytes) : QString();
    }
    const Item item = itemOf(cardsOf(m_frame->imports), m_frame->node);
    if (!item.sheet || item.sheet->level == Level::Universe)
        return {};
    const QList<Item> files = filesOf(item);
    const qsizetype there = std::ranges::count_if(files, [](const Item &one) {
        return held(*one.card, one.sheet->at);
    });
    if (there == 0)
        return {};
    return Words::alreadyHeld(nameOf(*item.sheet), int(there), int(files.size()));
}

namespace {

/// `sheet` with what a sheet holds of the shape. A folder read two ways is first of all
/// that: its values are the reading's, and saying where they came from before which reading
/// holds answered the second question first.
QVariantMap withShape(const QVariantMap &shape, QVariantMap sheet)
{
    if (const qsizetype readings = shape.value(u"readings"_s).toList().size(); readings >= 2)
        sheet.insert(u"subtitle"_s, Words::readingsPossible(int(readings)));
    sheet.insert(shape);
    return sheet;
}

/// The sheet of the shelf `shelf` names, `id`: its readings, and nothing to type.
QVariantMap shelfSheet(const Imports *imports, const std::pair<quint64, QString> &shelf,
                       QVariantMap shape, const QString &id)
{
    const Shape::Drop *drop = imports ? imports->dropOf(shelf.first) : nullptr;
    const Manifest::Node *folder = drop ? nodeIn(drop->whole, shelf.second) : nullptr;
    if (!folder)
        return {};
    readingsInto(shape, *drop, shelf.second, false);
    return withShape(shape, {{u"id"_s, id},
                             {u"title"_s, folder->name},
                             {u"subtitle"_s, QString()},
                             {u"editable"_s, false},
                             {u"fields"_s, QVariantList{}},
                             {u"notes"_s, QVariantList{}}});
}

/// The boxes of the sheet `id`, as the screen draws them. `doubts` is what could not be
/// read, by `id + '\n' + key`; `library` the names the library holds.
QVariantList fieldsOf(const Sheet &sheet, bool writable, const QHash<QString, QString> &doubts,
                      const QString &id, const QList<const Card *> &cards,
                      const QVariantMap &library)
{
    QVariantList fields;
    for (const Field &field : sheet.fields) {
        // The orders are a list of their own (`orders`), not a box to type JSON into.
        if (field.key == u"orders"_s || field.key == u"defaultOrder"_s)
            continue;
        // As the orders: a list of ranges is edited in a window of its own, not typed.
        if (field.key == u"arcs"_s)
            continue;
        const QString typed = doubts.value(id + u'\n' + field.key);
        const bool missing = field.origin == Facts::Origin::Missing;
        const InheritedValue inherited = inheritedOf(sheet, field);
        // An empty box with a value given from elsewhere shows that value, greyed, where it
        // said « Non renseigné »: the series' default form of a chapter's name was computed
        // and never drawn. A list draws its own (`inheritedWords`).
        const bool given = missing && inherited.held && !isListKey(field.key);
        QVariantMap shown{{u"key"_s, field.key},
                          {u"label"_s, Words::fieldLabel(field.key, sheet.level)},
                          {u"text"_s, missing ? QString() : textOf(field)},
                          // Whether it is red, and nothing of where it was read: a field
                          // shows what it holds.
                          {u"lacking"_s, Facts::lacks(sheet, field)},
                          {u"placeholder"_s, given ? inherited.text : Words::notFilledIn()},
                          {u"choices"_s, choicesOf(field.key)},
                          // Per field: a box that takes keystrokes `edit` then drops would
                          // be worse than one that says it cannot be typed into.
                          {u"editable"_s, writable && field.key != u"chapters"_s},
                          {u"doubt"_s, typed.isEmpty() ? field.doubt : typed}};
        shown.insert(u"kind"_s, kindOf(field.key));
        shown.insert(u"required"_s, field.weight == Facts::Weight::Required);
        shown.insert(u"inherited"_s, inherited.held);
        shown.insert(u"inheritedText"_s, inherited.text);
        shown.insert(u"inheritedWords"_s, inherited.words);
        shown.insert(u"hint"_s, hintOf(sheet, field));
        if (field.key == u"chapters"_s) {
            shown.insert(u"lines"_s, missing ? QVariantList() : chapterLines(field));
            shown.insert(u"text"_s, QString());
        }
        if (isListKey(field.key)) {
            shown.insert(u"words"_s, wordsOf(&field));
            shown.insert(u"vocabulary"_s, vocabularyOf(cards, field.key, library));
        }
        fields << shown;
    }
    return fields;
}

/// What a series says of `all` the people its volumes credit.
QVariantMap creditsSaid(const Sheet &sheet, const QStringList &all)
{
    const Field *authors = fieldIn(sheet, u"authors"_s);
    const bool written = authors && authors->origin != Facts::Origin::Missing;
    return QVariantMap{{u"computed"_s, Words::creditsListed(all)},
                       {u"all"_s, all},
                       {u"count"_s, int(all.size())},
                       {u"written"_s, written},
                       {u"writtenText"_s, written ? textOf(*authors) : QString()}};
}

/// The chapters a volume lists, as its window edits them, and the arcs of its series that
/// cross them. Nothing for any other sheet.
void chaptersInto(QVariantMap &shape, const Item &item, bool writable)
{
    const Sheet &sheet = *item.sheet;
    const Field *held = fieldIn(sheet, u"chapters"_s);
    if (!held || sheet.level != Level::Volume)
        return;
    QVariantList lines;
    const QJsonArray all = held->origin == Facts::Origin::Missing ? QJsonArray()
                                                                  : held->value.toArray();
    for (int at = 0; at < all.size(); ++at) {
        const QJsonObject chapter = all.at(at).toObject();
        lines << QVariantMap{{u"number"_s, chapter.value(u"number"_s).isDouble()
                                               ? Words::number(chapter.value(u"number"_s).toDouble())
                                               : QString()},
                             {u"title"_s, chapter.value(u"title"_s).toString()},
                             {u"page"_s, chapter.value(u"startPage"_s).isDouble()
                                             ? QString::number(chapter.value(u"startPage"_s).toInt())
                                             : QString()},
                             {u"source"_s, at}};
    }
    shape.insert(u"chaptersHeld"_s, lines);
    shape.insert(u"chaptersEditable"_s, writable);
    shape.insert(u"chaptersPages"_s, sheet.contents.pages);
    const auto work = std::ranges::find_if(item.card->road.sheets, [&](const Sheet &one) {
        return one.level == Level::Work && isVolumeOf(one, sheet);
    });
    if (work != item.card->road.sheets.end()) {
        shape.insert(u"chaptersArcs"_s, arcsCrossing(*work, item.card->road.sheets, sheet));
        shape.insert(u"chaptersWork"_s, QString::number(item.card->token) + u'/' + work->at);
    }
}

/// The arcs a series holds, each with where it falls in every edition. Nothing for any
/// other sheet.
void arcsInto(QVariantMap &shape, const Item &item, bool writable)
{
    const Sheet &sheet = *item.sheet;
    if (sheet.level != Level::Work)
        return;
    const QVariantList editions = arcsEditions(sheet, item.card->road.sheets);
    shape.insert(u"arcsEditions"_s, editions);
    QVariantList arcs;
    if (const Field *held = fieldIn(sheet, u"arcs"_s); held && held->origin != Facts::Origin::Missing) {
        for (const QJsonValue &one : held->value.toArray()) {
            const QJsonObject arc = one.toObject();
            QVariantMap said{{u"name"_s, arc.value(u"name"_s).toString()},
                             {u"parent"_s, arc.value(u"parent"_s).toString()},
                             {u"unit"_s, arc.value(u"unit"_s).toString()},
                             {u"from"_s, arc.value(u"from"_s).toInt()},
                             {u"to"_s, arc.value(u"to"_s).toInt()}};
            QVariantList falls;
            for (const QVariant &edition : editions)
                falls << arcFalls(sheet, edition.toMap().value(u"at"_s).toString(),
                                  item.card->road.sheets, said);
            said.insert(u"falls"_s, falls);
            arcs << said;
        }
    }
    shape.insert(u"arcs"_s, arcs);
    shape.insert(u"arcsEditable"_s, writable);
}

/// What is said over a sheet: why its card was refused, what is wrong with its file or its
/// orders, what it lacks, and where its names meet others.
QVariantList notesOf(const Item &item)
{
    const Sheet &sheet = *item.sheet;
    QVariantList notes;
    // A card refused says why first (`Review::sheet` offers to take it away).
    const bool failed = item.card->stage == Card::Stage::Failed;
    if (failed)
        notes << QVariantMap{{u"text"_s, item.card->trouble}, {u"alarming"_s, true}};
    if (!sheet.trouble.isEmpty() && !(failed && sheet.trouble == item.card->trouble))
        notes << QVariantMap{{u"text"_s, sheet.trouble}, {u"alarming"_s, true}};
    if (const Field *direction = fieldIn(sheet, u"readingDirection"_s);
        direction && direction->origin == Facts::Origin::Missing)
        notes << QVariantMap{{u"text"_s, Words::readingDirectionUnsaid()}, {u"alarming"_s, true}};
    // On the series only: its volumes lacking a credit are counted there, where one name
    // typed releases them all, and not said again on each of forty volumes.
    for (const QString &key : {u"authors"_s, u"artists"_s}) {
        if (const Field *credit = fieldIn(sheet, key);
            credit && sheet.level == Level::Work && Facts::lacks(sheet, *credit))
            notes << QVariantMap{{u"text"_s, Words::creditLacking(key, Facts::lackingFor(sheet, key))},
                                 {u"alarming"_s, true}};
    }
    if (sheet.comicInfo.unreadable && sheet.trouble.isEmpty())
        notes << QVariantMap{{u"text"_s, Words::comicInfoUnreadable()}, {u"alarming"_s, false}};
    // `Facts` reads it as the server will, no book, and the sheet has to say why « Livre
    // seul » is greyed on it: the flag was set and the sentence shown nowhere.
    // The orders are no field of the sheet, so what is wrong with them is said over it: an
    // inclusion the announcement refuses holds the whole card back.
    if (const Field *orders = fieldIn(sheet, u"orders"_s); orders && !orders->doubt.isEmpty())
        notes << QVariantMap{{u"text"_s, orders->doubt}, {u"alarming"_s, Facts::blocks(sheet)}};
    if (sheet.oneShotUnreadable && sheet.trouble.isEmpty())
        notes << QVariantMap{{u"text"_s, Words::oneShotUnreadable()}, {u"alarming"_s, false}};
    if (sheet.role == Shape::Role::Unread && atAUniverseRoot(item) && !failed)
        notes.prepend(QVariantMap{{u"text"_s, Words::unreadAtTheRoot()}, {u"alarming"_s, true}});
    for (const Api::NamesMeet &meet : item.card->road.collisions) {
        if (sheet.at.isEmpty() || meet.path.startsWith(sheet.at + u'/'))
            notes << QVariantMap{{u"text"_s, Words::namesMeet(meet.path, meet.with)},
                                 {u"alarming"_s, true}};
    }
    return notes;
}

/// Where the files of a series will be filed replaces where its values came from: the
/// second is of use to whoever edits a field, the first to whoever is about to send.
QString subtitleShown(const Card &card, const Sheet &sheet)
{
    const QString filedUnder =
        sheet.level == Level::Work && !filedFolderOf(card, sheet).isEmpty()
            ? Words::filedUnder(filedFolderOf(card, sheet))
            : QString();
    return filedUnder.isEmpty() ? subtitleOf(sheet) : filedUnder;
}

/// What is offered for a series the library holds a look-alike of.
QVariantList resemblanceChoices(const Api::Resemblance &like)
{
    QVariantList choices;
    // Only the folder announced can be reimported onto the one held: a folder inside a
    // bigger drop cannot be split from it.
    if (like.at.isEmpty()) {
        choices << QVariantMap{{u"id"_s, u"onto"_s},
                               {u"label"_s, Words::reimportOnto(like.name)},
                               {u"badge"_s, Words::recommended()},
                               {u"chosen"_s, false},
                               {u"detail"_s, QString()}};
    }
    // Beside « onto »: the folder is another edition of the series held, not its volumes
    // again, and not a series of its own.
    if (like.at.isEmpty()) {
        choices << QVariantMap{{u"id"_s, u"edition"_s},
                               {u"label"_s, Words::newEditionOf(like.name)},
                               {u"badge"_s, QString()},
                               {u"chosen"_s, false},
                               {u"detail"_s, Words::newEditionDetail()}};
    }
    choices << QVariantMap{{u"id"_s, u"second"_s},
                           {u"label"_s, Words::createSecondSeries()},
                           {u"badge"_s, QString()},
                           {u"chosen"_s, false},
                           {u"detail"_s, Words::existingStaysPut()}};
    return choices;
}

/// The sheet of what is not a card: the whole apport, or what sits under no universe. It
/// edits nothing and says nothing more than its name.
QVariantMap openingSheet(const QVariantMap &shape, const QString &focus)
{
    const QString title = focus == WholeNode ? Words::wholeApport() : Words::withoutUniverse();
    return withShape(shape, {{u"id"_s, focus},
                             {u"title"_s, title},
                             {u"subtitle"_s, QString()},
                             {u"editable"_s, false},
                             {u"fields"_s, QVariantList{}},
                             {u"notes"_s, QVariantList{}}});
}

/// The cover of a series or an edition: what the folder holds or the reader chose, and the
/// way to change it. A volume has its own, and a file no folder.
void coverInto(QVariantMap &out, const Item &item, const Sheet &sheet, bool writable)
{
    if (!item.card->folder || (sheet.level != Level::Work && sheet.level != Level::Edition))
        return;
    const QString shown = item.card->coverOf(sheet.at);
    out.insert(u"cover"_s,
               QVariantMap{{u"at"_s, sheet.at},
                           {u"url"_s, shown.isEmpty() ? QString()
                                                      : QUrl::fromLocalFile(shown).toString()},
                           {u"picked"_s, item.card->coverPicked(sheet.at)},
                           {u"editable"_s, writable}});
}

} // namespace

QVariantMap Review::sheet() const
{
    if (const Card *card = m_frame->aimed())
        return Aimed::sheet(*card, m_frame->composedOf(*card), m_frame->focus);
    // What a sheet holds of the shape: every key always there, empty where it says nothing,
    // so a screen binds to one shape of sheet.
    QVariantMap shape{{u"readings"_s, QVariantList{}}, {u"cluesTitle"_s, QString()},
                      {u"clues"_s, QVariantList{}},    {u"writes"_s, QVariantList{}},
                      {u"writesNote"_s, QString()},    {u"place"_s, QVariantList{}},
                      {u"credits"_s, QVariantMap{}},   {u"orders"_s, QVariantList{}},
                      {u"ordersNote"_s, QString()},    {u"ordersEditable"_s, false},
                      {u"removable"_s, false}};

    if (m_frame->focus == WholeNode || m_frame->focus == Loose)
        return openingSheet(shape, m_frame->focus);
    if (const auto shelf = shelfOf(m_frame->focus))
        return shelfSheet(m_frame->imports, *shelf, shape, m_frame->focus);
    const Item item = itemOf(cardsOf(m_frame->imports), m_frame->focus);
    if (!item.sheet)
        return {};
    const Sheet &sheet = *item.sheet;
    if (const Shape::Drop *drop = dropOfCard(m_frame->imports, *item.card)) {
        const QString at = inTheDrop(item.card->road.within, sheet.at);
        if (atAUniverseRoot(item)) {
            shape.insert(u"place"_s, placeOf(*drop, at, sheet));
        } else if (!isFile(sheet.level) && drop->suggestions.contains(at)) {
            readingsInto(shape, *drop, at, nameMissing(*item.card, sheet));
            const QVariantList writes =
                writesOf(*item.card, m_frame->composedOf(*item.card), sheet.at);
            shape.insert(u"writes"_s, writes);
            shape.insert(u"writesNote"_s, writes.isEmpty() ? QString() : Words::writesNote());
        }
    }

    // Nothing to write into a file nobody could open (`Sheet::trouble`).
    const bool writable = Carrier::correctable(*item.card) && sheet.trouble.isEmpty();
    const QVariantList fields =
        fieldsOf(sheet, writable, m_frame->doubts, m_frame->focus, cardsOf(m_frame->imports),
                 m_frame->libraryWords);

    if (sheet.level == Level::Work) {
        if (const Frame::Credits credits = m_frame->creditsOf(item.card->token, sheet.at);
            !credits.all.isEmpty())
            shape.insert(u"credits"_s, creditsSaid(sheet, credits.all));
    }
    chaptersInto(shape, item, writable);
    arcsInto(shape, item, writable);
    if (const std::optional<Orders::Universe> universe = ordersOf(sheet)) {
        const auto [orders, note] = ordersSaid(*universe);
        shape.insert(u"orders"_s, orders);
        shape.insert(u"ordersNote"_s, note);
        shape.insert(u"ordersEditable"_s, Carrier::correctable(*item.card));
    }

    // A card refused is offered to be taken away: nothing on it can be corrected, and left
    // there it would wait for ever under a frame that cannot send it.
    if (item.card->stage == Card::Stage::Failed)
        shape.insert(u"removable"_s, true);
    // Its id, for the field that hands a correction back to `edit`.
    QVariantMap out = withShape(shape, {{u"id"_s, m_frame->focus},
                                        {u"title"_s, nameOf(sheet)},
                                        {u"subtitle"_s, subtitleShown(*item.card, sheet)},
                                        {u"editable"_s, writable},
                                        {u"fields"_s, fields},
                                        {u"notes"_s, notesOf(item)}});
    coverInto(out, item, sheet, writable);
    if (const Api::Resemblance *like = resemblanceAt(*item.card, sheet.at)) {
        out.insert(u"alarming"_s, QVariantMap{{u"title"_s, Words::alreadyInLibrary(like->name)},
                                              {u"text"_s, QString()}});
        out.insert(u"choices"_s, resemblanceChoices(*like));
    }
    return out;
}

QString Review::footer() const
{
    if (!m_frame->imports)
        return {};
    if (m_frame->aimed())
        return Aimed::footer(*m_frame->aimed());
    int missing = 0;
    int rewritten = 0;
    int leftOut = 0;
    for (const Card *card : cardsOf(m_frame->imports)) {
        for (const Sheet &sheet : card->road.sheets) {
            if (!doing(Item{card, &sheet}))
                continue;
            // Only what blocks: a writer or a drawer a series' volumes name is not missing, and
            // « Rien ne manque » over a series that « Suivant » then refuses was a lie.
            missing += Facts::lacking(sheet);
        }
        const Facts::Composed composed = m_frame->composedOf(*card);
        for (auto it = composed.rewrites.cbegin(); it != composed.rewrites.cend(); ++it)
            rewritten += held(*card, it.key()) ? 0 : 1;
        leftOut += int(composed.leftOut.size());
    }
    // Who waits, and why, first: « Importer » sends the others without them, and at fifty
    // series a footer that only counted what was missing read as the reason nothing went.
    int blocked = 0;
    int reading = 0;
    int failed = 0;
    for (const Card &card : m_frame->imports->cards()) {
        if (!card.folder)
            continue;
        if (card.stage == Card::Stage::Failed)
            ++failed;
        else if (card.stage == Card::Stage::Asking)
            ++reading;
        else if (Carrier::correctable(card) && Carrier::heldBack(card))
            ++blocked;
    }
    const QString waiting = Words::waitingSeries(blocked, reading, failed);
    const auto after = [&waiting, leftOut](const QString &rest) {
        const QString said = Words::afterLeftOut(leftOut, rest);
        return waiting.isEmpty() ? said : waiting + u' ' + said;
    };
    // While a reading is left, « Importer » confirms it first: the footer says that, rather
    // than counts of values the reading may still change.
    if (const int readings = m_frame->state().readings; readings > 0)
        return after(Words::nothingSentBeforeReadings(readings));
    // A series credited apart counts what its volumes still lack and who it is credited to,
    // which is what that list is about.
    if (const std::optional<Frame::Credits> credits = m_frame->creditsShown()) {
        return after(Words::creditsFooter(int(credits->all.size()), missing));
    }
    return after(Words::reviewFooter(missing, rewritten));
}

QString Review::importLabel() const
{
    if (const Card *card = m_frame->aimed())
        return Aimed::importLabel(*card);
    if (nothingToImport())
        return Words::nothingToImport();
    if (const int readings = m_frame->state().readings; readings > 0)
        return Words::confirmReadings(readings);
    return Words::importVolumes(travelling(m_frame->imports));
}

bool Review::importable() const
{
    if (const Card *card = m_frame->aimed())
        return goesNow(*card) && Aimed::anything(*card);
    // What « Importer » would let go: a card stopped or ready, something of which is not
    // identical, that no sheet holds back — or a file dropped alone that is ready.
    return m_frame->imports && std::ranges::any_of(m_frame->imports->cards(), goesNow);
}

bool Review::nothingToImport() const
{
    if (const Card *card = m_frame->aimed())
        return !Aimed::anything(*card) && card->stage != Card::Stage::Asking;
    // Nothing of the frame becomes anything, and nothing else of the queue goes either:
    // `confirm` ends by starting the whole queue. Nor is anything still read or waiting
    // for its answer: « Fermer » over a drop half read let the rest go unseen.
    const QList<const Card *> cards = cardsOf(m_frame->imports);
    return !cards.isEmpty() && std::ranges::none_of(cards, [](const Card *card) {
        return anythingGoes(Item{card, &card->road.sheets.constFirst()});
    }) && std::ranges::none_of(m_frame->imports->cards(), goesNow)
           && std::ranges::none_of(m_frame->imports->cards(), [](const Card &card) {
                  return card.stage == Card::Stage::Asking;
              });
}

bool Review::sparesSomething() const
{
    // Identical is the library holding the file already (`Carrier::travels`): the cards'
    // button goes on to the frame for it, since a reader who dropped a held folder was
    // shown « Fermer » and never the frame that says so.
    return std::ranges::any_of(cardsOf(m_frame->imports), [](const Card *card) {
        return std::ranges::any_of(card->road.sheets, [card](const Sheet &sheet) {
            return isFile(sheet.level) && !Carrier::travels(*card, sheet);
        });
    });
}

void ReviewView::open()
{
    // In the tree's order, the one « Revoir la suivante » walks: in the order things were
    // dropped, a series without universe dropped first opened below a universe drawn above.
    QList<const Card *> cards;
    {
        const QList<const Card *> all = cardsOf(m_frame->imports);
        for (const quint64 token : tokensInOrder(wholeRail())) {
            if (const auto found = std::ranges::find(all, token, &Card::token);
                found != all.cend())
                cards << *found;
        }
    }
    m_frame->filter = Filter::All;
    m_frame->showIdentical = false;
    m_frame->node = WholeNode;
    m_frame->focus = WholeNode;
    // Opened on what is to review: a search left from before hid it.
    m_frame->search.clear();
    m_frame->searchKey.clear();
    m_frame->untickAll();

    // A card refused before anything else: it will never go, and opened below a series that
    // only needed a word, it was the line nobody reached. Then a container's sheet: one value
    // said for a series is said for every volume in it, and opening on a volume first would
    // show sheets that cannot fix it.
    std::optional<Item> first;
    for (const Card *card : cards) {
        if (card->stage == Card::Stage::Failed) {
            first = Item{card, &card->road.sheets.constFirst()};
            break;
        }
    }
    for (const bool files : {false, true}) {
        for (const Card *card : cards) {
            if (first)
                break;
            first = stopOf(*card, files);
        }
    }
    if (first) {
        std::tie(m_frame->node, m_frame->focus) = placeOfReview(m_frame->imports, *first);
        foldAround();
        emit m_review->changed();
        return;
    }
    QList<Item> series;
    for (const Card *card : cards)
        series << seriesOf(*card);
    if (series.size() == 1) {
        m_frame->node = idOf(series.constFirst());
        m_frame->focus = m_frame->node;
    }
    foldAround();
    emit m_review->changed();
}

QVariantList ReviewView::wholeRail()
{
    const QSet<QString> folded = std::exchange(m_frame->folded, {});
    QVariantList whole = m_review->rail();
    m_frame->folded = folded;
    return whole;
}

QSet<QString> ReviewView::shownBy(const QVariantList &whole) const
{
    // Each line's parent is the nearest line above it one level up.
    QList<qsizetype> parents(whole.size(), -1);
    QList<qsizetype> above;
    for (qsizetype at = 0; at < whole.size(); ++at) {
        const int depth = whole.at(at).toMap().value(u"depth"_s).toInt();
        while (above.size() > depth)
            above.removeLast();
        parents[at] = above.isEmpty() ? -1 : above.constLast();
        above.append(at);
    }
    QSet<QString> kept;
    for (qsizetype at = 0; at < whole.size(); ++at) {
        if (const QString id = whole.at(at).toMap().value(u"id"_s).toString();
            id != m_frame->node && id != m_frame->focus)
            continue;
        for (qsizetype up = at; up >= 0; up = parents.at(up))
            kept.insert(whole.at(up).toMap().value(u"id"_s).toString());
    }
    // And a node alone under its parent: folded, it is one line hiding all there is.
    QHash<qsizetype, int> children;
    for (const qsizetype parent : std::as_const(parents))
        ++children[parent];
    for (qsizetype at = 0; at < whole.size(); ++at) {
        if (children.value(parents.at(at)) == 1)
            kept.insert(whole.at(at).toMap().value(u"id"_s).toString());
    }
    return kept;
}

void ReviewView::foldAround()
{
    // Every node folded but what leads to what is opened, and a node alone at its depth:
    // fifty series unfolded were a wall of sixty lines over the one asking something, and
    // one universe folded hid the two series it was dropped for. A rule of shape, not of
    // number.
    const QVariantList whole = wholeRail();
    const QSet<QString> kept = shownBy(whole);
    m_frame->folded.clear();
    for (const QVariant &one : whole) {
        const QVariantMap line = one.toMap();
        const QString id = line.value(u"id"_s).toString();
        if (line.value(u"expandable"_s).toBool() && !kept.contains(id))
            m_frame->folded.insert(id);
    }
}


int Review::toReview() const
{
    const QList<const Card *> cards = cardsOf(m_frame->imports);
    return int(std::ranges::count_if(
        cards, [](const Card *card) { return firstToReview(*card).has_value(); }));
}

bool ReviewView::reviewNext()
{
    const QList<const Card *> cards = cardsOf(m_frame->imports);
    const QVariantList whole = wholeRail();
    const QList<quint64> order = tokensInOrder(whole);
    const auto cardOf = [&cards](quint64 token) {
        const auto found = std::ranges::find(cards, token, &Card::token);
        return found == cards.cend() ? nullptr : *found;
    };
    const Item shown = itemOf(cards, m_frame->focus);
    const qsizetype from = shown.card ? order.indexOf(shown.card->token) : -1;
    for (qsizetype step = 1; step <= order.size(); ++step) {
        // After the card shown, coming back round; from the first when none is.
        const qsizetype at = from < 0 ? step - 1 : (from + step) % order.size();
        const Card *card = cardOf(order.at(at));
        const std::optional<Item> first = card ? firstToReview(*card) : std::nullopt;
        if (!first)
            continue;
        const auto [node, focus] = placeOfReview(m_frame->imports, *first);
        if (node != m_frame->node)
            m_frame->untickAll();
        m_frame->node = node;
        m_frame->focus = focus;
        // What leads to it is unfolded, and nothing else is folded for it.
        for (const QString &id : shownBy(whole))
            m_frame->folded.remove(id);
        emit m_review->changed();
        return true;
    }
    return false;
}

bool ReviewView::showBlocked()
{
    const QList<const Card *> cards = cardsOf(m_frame->imports);
    const QVariantList whole = wholeRail();
    for (const quint64 token : tokensInOrder(whole)) {
        const auto found = std::ranges::find(cards, token, &Card::token);
        if (found == cards.cend())
            continue;
        const Card &card = **found;
        if (!Carrier::correctable(card) || !Carrier::heldBack(card))
            continue;
        // A container's sheet first, as `firstToReview` has it: one value said for a series is
        // said for every volume in it.
        const Sheet *chosen = nullptr;
        for (const Sheet &sheet : card.road.sheets) {
            if (!Carrier::travels(card, sheet) || !Facts::blocks(sheet))
                continue;
            if (!chosen || (isFile(chosen->level) && !isFile(sheet.level)))
                chosen = &sheet;
        }
        if (!chosen)
            continue;
        const auto [node, focus] = placeOfReview(m_frame->imports, Item{&card, chosen});
        if (node != m_frame->node)
            m_frame->untickAll();
        m_frame->node = node;
        m_frame->focus = focus;
        for (const QString &id : shownBy(whole))
            m_frame->folded.remove(id);
        emit m_review->changed();
        return true;
    }
    return false;
}

void ReviewView::select(const QString &id)
{
    // The lines of an aimed card are the server's comparisons, not the nodes of a tree: the
    // focus is the line, and the target is what no line names.
    if (const Card *card = m_frame->aimed()) {
        // Another chapter of the volume in the rail aims the card at that one.
        if (Aimed::isChapterOfAimedVolume(*card, id)) {
            m_frame->imports->retarget(card->token, QString(), u"CHAPTER"_s, id);
            return;
        }
        m_frame->focus = id;
        emit m_review->changed();
        return;
    }
    if (id == WholeNode || id == Loose) {
        m_frame->node = id;
        m_frame->focus = id;
        emit m_review->changed();
        return;
    }
    if (const auto shelf = shelfOf(id)) {
        if (!isFrameShelf(cardsOf(m_frame->imports), m_frame->imports, shelf->first, shelf->second))
            return;
        m_frame->node = id;
        m_frame->focus = id;
        emit m_review->changed();
        return;
    }
    const Item item = itemOf(cardsOf(m_frame->imports), id);
    if (!item.sheet)
        return;
    m_frame->grouped = false;
    // While lines are ticked, a click on one of them shows its sheet beside them: moving
    // into the series instead forgot every tick for a look at one.
    if (const bool aTickedLine =
            !m_frame->ticked.isEmpty()
            && std::ranges::any_of(shownIn(*m_frame),
                                   [&id](const Item &one) { return idOf(one) == id; });
        inTheRail(item) && !aTickedLine && m_frame->node != id) {
        m_frame->node = id;
        m_frame->untickAll();
    }
    m_frame->focus = id;
    emit m_review->changed();
}

void Review::Frame::untickAll()
{
    ticked.clear();
    grouped = false;
    outcome.clear();
}

int ReviewTicks::ticked() const
{
    const QList<const Card *> cards = cardsOf(m_frame->imports);
    return int(std::ranges::count_if(
        m_frame->ticked, [&cards](const QString &id) { return tickable(itemOf(cards, id)); }));
}

int ReviewTicks::tickState() const
{
    if (const int count = ticked(); count == 0)
        return 0;
    // Under the filter too, as « Tout » ticks: under « À revoir », every line ticked was
    // said to be some of them, for the ready series the filter hides.
    const QList<Item> shown = shownIn(*m_frame);
    const bool all = std::ranges::all_of(shown, [this](const Item &one) {
        return (m_frame->filter != Filter::All && groupOf(one) != m_frame->filter) || !tickable(one)
               || m_frame->ticked.contains(idOf(one));
    });
    return all ? 2 : 1;
}

QString ReviewTicks::tickedLabel() const
{
    return Words::tickedCount(ticked());
}

void ReviewTicks::tick(const QString &id, bool ticked)
{
    if (ticked && !tickable(itemOf(cardsOf(m_frame->imports), id)))
        return;
    const bool moved = ticked ? !m_frame->ticked.contains(id) : m_frame->ticked.contains(id);
    if (ticked)
        m_frame->ticked.insert(id);
    else
        m_frame->ticked.remove(id);
    if (!moved && m_frame->grouped == ticked)
        return;
    m_frame->grouped = ticked || !m_frame->ticked.isEmpty();
    m_frame->outcome.clear();
    emit m_review->changed();
}

void ReviewTicks::tickShown(bool ticked)
{
    m_frame->untickAll();
    if (ticked) {
        // The lines the list shows, and no other: those of the filter, and of the search.
        for (const Item &one : shownIn(*m_frame)) {
            if ((m_frame->filter == Filter::All || groupOf(one) == m_frame->filter) && tickable(one))
                m_frame->ticked.insert(idOf(one));
        }
        m_frame->grouped = !m_frame->ticked.isEmpty();
    }
    emit m_review->changed();
}

int ReviewTicks::tickRange(const QString &from, const QString &to)
{
    // In the order the list draws its lines, groups and all, which is the order Maj and an
    // arrow walk: in the order of the drop, a range would run across lines nobody passed.
    const QVariantList lines = m_review->rows();
    qsizetype first = -1;
    qsizetype last = -1;
    for (qsizetype at = 0; at < lines.size(); ++at) {
        const QString id = lines.at(at).toMap().value(u"id"_s).toString();
        if (!id.isEmpty() && (id == from || id == to)) {
            first = first < 0 ? at : first;
            last = at;
        }
    }
    int added = 0;
    for (qsizetype at = first; first >= 0 && at <= last; ++at) {
        const QVariantMap line = lines.at(at).toMap();
        const QString id = line.value(u"id"_s).toString();
        if (line.value(u"checkable"_s).toBool() && !m_frame->ticked.contains(id)) {
            m_frame->ticked.insert(id);
            ++added;
        }
    }
    if (added == 0)
        return 0;
    m_frame->grouped = true;
    m_frame->outcome.clear();
    emit m_review->changed();
    return added;
}

namespace {

/// What is applied to series together: what one series shares with the next. A title or a
/// number given to twenty series at once makes twenty series of one name.
const QStringList GroupedKeys{u"readingDirection"_s, u"language"_s, u"status"_s};

/// The languages offered: those the drop already says, in the order met, then the three
/// most read. Every tag there is would be a wall; the three alone left a series in German
/// no way to be ticked into German.
QStringList languagesOf(const QList<const Card *> &cards)
{
    QStringList out;
    for (const Card *card : cards) {
        for (const Sheet &sheet : card->road.sheets) {
            ++spent().vocabulary;
            const Field *field = fieldIn(sheet, u"language"_s);
            if (!field || field->origin == Facts::Origin::Missing)
                continue;
            if (const QString tag = field->value.toString().trimmed();
                !tag.isEmpty() && !out.contains(tag))
                out << tag;
        }
    }
    for (const QString &tag : {u"fr"_s, u"en"_s, u"ja"_s}) {
        if (!out.contains(tag))
            out << tag;
    }
    return out;
}

/// Appends to `edits` what writing `given` into the ticked `series` changes; whether it does.
bool editsOf(const Item &series, const QList<std::pair<QString, QString>> &given,
             QList<Imports::Edit> &edits)
{
    bool changes = false;
    // The series and what it holds that carries the field: its own sheet, or its
    // declared editions, each of which says its own direction.
    for (const Sheet &sheet : series.card->road.sheets) {
        ++spent().grouped;
        if (isFile(sheet.level) || !under(sheet.at, series.sheet->at))
            continue;
        for (const auto &[key, text] : given) {
            const Field *field = fieldIn(sheet, key);
            if (!field)
                continue;
            if (field->origin != Facts::Origin::Guessed && field->value.toString() == text)
                continue;
            edits.append({series.card->token, sheet.at, key, text});
            changes = true;
        }
    }
    return changes;
}

} // namespace

QVariantMap ReviewTicks::grouped() const
{
    const int count = ticked();
    QVariantList fields;
    for (const QString &key : GroupedKeys) {
        QVariantList choices{QVariantMap{{u"value"_s, QVariant()},
                                         {u"label"_s, Words::leaveUnchanged()}}};
        if (key == u"language"_s) {
            for (const QString &tag : languagesOf(cardsOf(m_frame->imports)))
                choices << QVariantMap{{u"value"_s, tag}, {u"label"_s, Words::language(tag)}};
        } else {
            choices << choicesOf(key);
        }
        fields << QVariantMap{{u"key"_s, key},
                              {u"label"_s, Words::fieldLabel(key, Level::Work)},
                              {u"choices"_s, choices}};
    }
    return {{u"title"_s, Words::forTicked(count)},
            {u"aside"_s, Words::groupedActions()},
            {u"fields"_s, fields},
            {u"applyLabel"_s, Words::applyToTicked(count)},
            {u"note"_s, Words::groupedNote()},
            {u"outcome"_s, m_frame->outcome}};
}

int ReviewTicks::applyToTicked(const QVariantMap &values)
{
    if (!m_frame->imports)
        return 0;
    // The values given, as given: a key nobody offers, an empty one or a choice that is
    // none of the offered ones is no value to write.
    QList<std::pair<QString, QString>> given;
    for (const QString &key : GroupedKeys) {
        const QVariant value = values.value(key);
        const QString text = value.isNull() ? QString() : value.toString().trimmed();
        if (text.isEmpty())
            continue;
        if (const QVariantList choices = choicesOf(key);
            !choices.isEmpty() && std::ranges::none_of(choices, [&text](const QVariant &one) {
                return one.toMap().value(u"value"_s).toString() == text;
            }))
            continue;
        given.append({key, text});
    }
    if (given.isEmpty())
        return 0;

    const QList<const Card *> cards = cardsOf(m_frame->imports);
    QList<Imports::Edit> edits;
    int changedSeries = 0;
    int same = 0;
    int refused = 0;
    QStringList ids(m_frame->ticked.cbegin(), m_frame->ticked.cend());
    ids.sort();
    for (const QString &id : std::as_const(ids)) {
        const Item series = itemOf(cards, id);
        if (!tickable(series)) {
            ++refused;
            continue;
        }
        editsOf(series, given, edits) ? ++changedSeries : ++same;
    }
    m_frame->outcome = Words::groupedOutcome(changedSeries, same, refused);
    // One batch: the frame hears of it once, not once per series.
    if (edits.isEmpty() || m_frame->imports->edit(edits) == 0)
        emit m_review->changed();
    return changedSeries;
}

bool ReviewEdits::renumber(const QStringList &ids)
{
    if (!m_frame->imports || ids.size() < 2)
        return false;
    const QList<const Card *> cards = cardsOf(m_frame->imports);
    QList<Item> volumes;
    QList<double> numbers;
    for (const QString &id : ids) {
        const Item item = itemOf(cards, id);
        const Field *number = item.sheet ? fieldIn(*item.sheet, u"number"_s) : nullptr;
        if (!item.sheet || !isFile(item.sheet->level) || !Carrier::correctable(*item.card)
            || !item.sheet->trouble.isEmpty() || !number
            || number->origin == Facts::Origin::Missing || !number->value.isDouble())
            return false;
        volumes << item;
        numbers << number->value.toDouble();
    }
    std::ranges::sort(numbers);
    QList<Imports::Edit> edits;
    for (qsizetype at = 0; at < volumes.size(); ++at) {
        const Field *held = fieldIn(*volumes.at(at).sheet, u"number"_s);
        if (held->value.toDouble() != numbers.at(at))
            edits.append({volumes.at(at).card->token, volumes.at(at).sheet->at, u"number"_s,
                          numbers.at(at)});
    }
    if (edits.isEmpty() || m_frame->imports->edit(edits) == 0)
        emit m_review->changed();
    return !edits.isEmpty();
}

bool ReviewEdits::remove(const QString &id)
{
    const Item item = itemOf(cardsOf(m_frame->imports), id);
    if (!item.card || item.card->stage != Card::Stage::Failed)
        return false;
    const quint64 token = item.card->token;
    const auto row = std::ranges::find(m_frame->imports->cards(), token, &Card::token);
    // Back to the whole drop first: the card's own node and sheet name nothing once it goes.
    m_frame->node = WholeNode;
    m_frame->focus = WholeNode;
    m_frame->imports->abandon(int(row - m_frame->imports->cards().cbegin()));
    emit m_review->changed();
    return true;
}

void ReviewView::toggle(const QString &id)
{
    if (!m_frame->folded.remove(id))
        m_frame->folded.insert(id);
    emit m_review->changed();
}

void ReviewView::toggleIdentical()
{
    m_frame->showIdentical = !m_frame->showIdentical;
    emit m_review->changed();
}

namespace {

/// The volumes of `item`'s folder that have a number, in its order, as `ReviewEdits::renumber`
/// takes them once `item` is moved to the place of the one numbered `number` — nothing when
/// no other volume holds that number.
std::optional<QStringList> movedTo(const Item &item, double number)
{
    const auto folderOf = [](const QString &path) {
        const qsizetype slash = path.lastIndexOf(u'/');
        return slash < 0 ? QString() : path.left(slash);
    };
    QList<std::pair<double, const Facts::Sheet *>> run;
    for (const Facts::Sheet &sheet : item.card->road.sheets) {
        const Field *held = fieldIn(sheet, u"number"_s);
        if (isFile(sheet.level) && folderOf(sheet.at) == folderOf(item.sheet->at) && held
            && held->origin != Facts::Origin::Missing && held->value.isDouble())
            run.append({held->value.toDouble(), &sheet});
    }
    std::ranges::stable_sort(run, {}, &std::pair<double, const Facts::Sheet *>::first);
    const auto mine = std::ranges::find(run, item.sheet, &std::pair<double, const Facts::Sheet *>::second);
    const auto there = std::ranges::find(run, number,
                                         &std::pair<double, const Facts::Sheet *>::first);
    if (mine == run.end() || there == run.end() || there == mine)
        return std::nullopt;
    QStringList ids;
    for (const auto &[held, sheet] : run)
        ids << QString::number(item.card->token) + u'/' + sheet->at;
    const QString moved = ids.takeAt(int(mine - run.begin()));
    ids.insert(int(there - run.begin()), moved);
    return ids;
}

} // namespace

bool ReviewEdits::edit(const QString &id, const QString &key, const QString &text)
{
    if (!m_frame->imports)
        return false;
    const Item item = itemOf(cardsOf(m_frame->imports), id);
    if (!item.sheet)
        return false;
    const QString trimmed = text.trimmed();
    const QString doubtKey = id + u'\n' + key;
    QJsonValue value;

    const auto refuse = [&](bool whole) {
        m_frame->doubts.insert(doubtKey, Words::notANumber(trimmed, whole));
        emit m_review->changed();
        return false;
    };

    // A list is given as words (`editWords`): cut on commas, « Dupont, Jean » was two people.
    if (isListKey(key))
        return false;
    if (key == u"chapters"_s) {
        // The chapter editor is a later step: a line of text is not a list of chapters, and
        // taking it as one would write `"chapters": "…"` into an `entry.json`.
        return false;
    }
    if (trimmed.isEmpty()) {
        value = QJsonValue();
    } else if (key == u"number"_s) {
        // A comma is how a French reader writes a half; `toDouble` reads only a point.
        bool ok = false;
        const double number = QString(trimmed).replace(u',', u'.').toDouble(&ok);
        if (!ok || !std::isfinite(number))
            return refuse(false);
        value = number;
    } else if (key == u"volumeCount"_s) {
        bool ok = false;
        const int count = trimmed.toInt(&ok);
        if (!ok || count < 0)
            return refuse(true);
        value = count;
    } else if (const QVariantList choices = choicesOf(key); !choices.isEmpty()) {
        // A choice is one of its values, and nothing typed: « droite » written into a
        // `work.json` is a direction the server does not read.
        if (std::ranges::none_of(choices, [&trimmed](const QVariant &one) {
                return one.toMap().value(u"value"_s).toString() == trimmed;
            })) {
            m_frame->doubts.insert(doubtKey, Words::notAChoice(trimmed));
            emit m_review->changed();
            return false;
        }
        value = trimmed;
    } else {
        value = trimmed;
    }

    // A number another volume of the edition already holds is a move, as a drag is: the volume
    // takes the place of that one and every volume between slides by one. « Un autre tome a le
    // numéro 5 » refused what a reader plainly meant, and swapping the two was a third thing
    // that neither the drag nor the arrows did.
    if (key == u"number"_s && value.isDouble() && isFile(item.sheet->level)
        && Carrier::correctable(*item.card) && item.sheet->trouble.isEmpty()) {
        if (const std::optional<QStringList> ids = movedTo(item, value.toDouble())) {
            m_frame->doubts.remove(doubtKey);
            return renumber(*ids);
        }
    }

    const bool hadDoubt = m_frame->doubts.remove(doubtKey);
    const bool went = m_frame->imports->edit(item.card->token, item.sheet->at, key, value);
    if (hadDoubt && !went)
        emit m_review->changed();
    return went;
}

bool ReviewEdits::editWords(const QString &id, const QString &key, const QStringList &words)
{
    if (!m_frame->imports || !isListKey(key))
        return false;
    const Item item = itemOf(cardsOf(m_frame->imports), id);
    if (!item.sheet)
        return false;
    // Once each, as the server keeps a list: « arleston » beside « Arleston » is one person,
    // and the first spelling stays.
    QJsonArray list;
    QSet<QString> held;
    for (const QString &word : words) {
        const QString trimmed = word.trimmed();
        if (!trimmed.isEmpty() && !held.contains(Fold::searchKey(trimmed))) {
            held.insert(Fold::searchKey(trimmed));
            list << trimmed;
        }
    }
    const bool hadDoubt = m_frame->doubts.remove(id + u'\n' + key);
    const bool went = m_frame->imports->edit(item.card->token, item.sheet->at, key,
                                      list.isEmpty() ? QJsonValue() : QJsonValue(list));
    if (hadDoubt && !went)
        emit m_review->changed();
    return went;
}

QVariantList ReviewEdits::chaptersNumbered(const QVariantList &rows, const QString &first) const
{
    bool isNumber = false;
    const double from = QString(first).trimmed().replace(u',', u'.').toDouble(&isNumber);
    if (!isNumber)
        return rows;
    QVariantList out;
    for (const QVariant &one : rows) {
        QVariantMap row = one.toMap();
        row.insert(u"number"_s, Words::number(from + double(out.size())));
        out << row;
    }
    return out;
}

int ReviewEdits::chapterPlace(const QVariantList &rows, int at) const
{
    const auto pageOf = [&rows](qsizetype line) {
        bool isPage = false;
        const int page = rows.at(line).toMap().value(u"page"_s).toString().trimmed().toInt(&isPage);
        return isPage ? std::optional(page) : std::nullopt;
    };
    if (at < 0 || at >= rows.size() || !pageOf(at).has_value())
        return at;
    const int page = *pageOf(at);
    // Already between the nearest pages on either side: it stays, and does not jump over a
    // line still without a page for nothing.
    std::optional<int> before;
    std::optional<int> after;
    for (qsizetype line = at - 1; line >= 0; --line) {
        before = pageOf(line);
        if (before.has_value())
            break;
    }
    for (qsizetype line = at + 1; line < rows.size(); ++line) {
        after = pageOf(line);
        if (after.has_value())
            break;
    }
    if ((!before || *before <= page) && (!after || *after >= page))
        return at;
    // Counted among the other lines, as the list stands once the line is taken out of it.
    int place = 0;
    int others = 0;
    for (qsizetype line = 0; line < rows.size(); ++line) {
        if (line == at)
            continue;
        if (const std::optional<int> other = pageOf(line)) {
            if (*other > page)
                return others;
            place = others + 1;
        }
        ++others;
    }
    return place;
}

bool ReviewEdits::setChapters(const QString &id, const QVariantList &rows)
{
    if (!m_frame->imports)
        return false;
    const Item item = itemOf(cardsOf(m_frame->imports), id);
    if (!item.sheet || item.sheet->level != Level::Volume)
        return false;
    const Field *held = fieldIn(*item.sheet, u"chapters"_s);
    if (!held)
        return false;
    const QJsonArray before = held->origin == Facts::Origin::Missing ? QJsonArray()
                                                                     : held->value.toArray();
    QJsonArray list;
    QSet<double> numbers;
    for (const QVariant &one : rows) {
        const QVariantMap row = one.toMap();
        bool isNumber = false;
        const double number = row.value(u"number"_s).toString().trimmed().replace(u',', u'.')
                                  .toDouble(&isNumber);
        if (!isNumber || number < 0 || numbers.contains(number))
            return false;
        numbers.insert(number);
        const QString typedPage = row.value(u"page"_s).toString().trimmed();
        bool isPage = typedPage.isEmpty();
        const int page = typedPage.toInt(&isPage);
        if (!typedPage.isEmpty() && (!isPage || page < 0))
            return false;
        const int source = row.value(u"source"_s, -1).toInt();
        QJsonObject chapter = source >= 0 && source < before.size() ? before.at(source).toObject()
                                                                    : QJsonObject();
        chapter.insert(u"number"_s, number);
        if (const QString title = row.value(u"title"_s).toString().trimmed(); title.isEmpty())
            chapter.remove(u"title"_s);
        else
            chapter.insert(u"title"_s, title);
        if (typedPage.isEmpty())
            chapter.remove(u"startPage"_s);
        else
            chapter.insert(u"startPage"_s, page);
        list << chapter;
    }
    return m_frame->imports->edit(item.card->token, item.sheet->at, u"chapters"_s,
                           list.isEmpty() ? QJsonValue() : QJsonValue(list));
}

namespace {

/// The rows with what each says beside its boxes: a saga its arcs' count and span, a gap the
/// chapters it leaves. Worded here so that the window only draws them.
QVariantList worded(const QVariantList &rows)
{
    QVariantList out;
    for (const QVariant &one : rows) {
        QVariantMap row = one.toMap();
        if (const QString kind = row.value(u"kind"_s).toString(); kind == u"saga"_s)
            row.insert(u"said"_s, Words::arcsSagaSpan(row.value(u"count"_s).toInt(),
                                                      row.value(u"unit"_s).toString(),
                                                      row.value(u"from"_s).toInt(),
                                                      row.value(u"to"_s).toInt()));
        else if (kind == u"gap"_s)
            row.insert(u"said"_s, Words::arcsGap(row.value(u"from"_s).toInt(),
                                                 row.value(u"to"_s).toInt()));
        out << row;
    }
    return out;
}

} // namespace

QVariantList ReviewEdits::arcRows(const QVariantList &arcs) const
{
    return worded(Sagas::rowsOf(arcs));
}

QVariantList ReviewEdits::arcsFromRows(const QVariantList &rows) const
{
    return Sagas::arcsOf(rows);
}

QVariantList ReviewEdits::arcRowsEdited(const QVariantList &rows, const QString &how, int at,
                                   int arg) const
{
    if (how == u"move"_s)
        return worded(Sagas::moved(rows, at, arg));
    if (how == u"start"_s)
        return worded(Sagas::startedAt(rows, at, arg));
    if (how == u"remove"_s)
        return worded(Sagas::removed(rows, at));
    if (how == u"regap"_s)
        return worded(Sagas::regapped(rows));
    return rows;
}

bool ReviewEdits::setArcs(const QString &id, const QVariantList &arcs)
{
    if (!m_frame->imports)
        return false;
    const Item item = itemOf(cardsOf(m_frame->imports), id);
    if (!item.sheet || item.sheet->level != Level::Work)
        return false;
    // What the format refuses is refused here, before a card that could not be sent: a name
    // each, a range that runs forwards, and a saga that is a top-level arc of the same list
    // (the sidecar nests one level and the client draws one).
    QSet<QString> names;
    for (const QVariant &one : arcs) {
        const QString name = one.toMap().value(u"name"_s).toString().trimmed();
        if (name.isEmpty() || names.contains(name))
            return false;
        names.insert(name);
    }
    QJsonArray list;
    for (const QVariant &one : arcs) {
        const QVariantMap arc = one.toMap();
        const QString unit = arc.value(u"unit"_s).toString();
        const QString parent = arc.value(u"parent"_s).toString().trimmed();
        const int from = arc.value(u"from"_s).toInt();
        const int to = arc.value(u"to"_s).toInt();
        if ((unit != u"CHAPTER"_s && unit != u"VOLUME"_s) || from < 0 || to < from
            || (!parent.isEmpty() && !names.contains(parent))
            || parent == arc.value(u"name"_s).toString().trimmed())
            return false;
        QJsonObject object{{u"name"_s, arc.value(u"name"_s).toString().trimmed()},
                           {u"unit"_s, unit}, {u"from"_s, from}, {u"to"_s, to}};
        if (!parent.isEmpty())
            object.insert(u"parent"_s, parent);
        list << object;
    }
    return m_frame->imports->edit(item.card->token, item.sheet->at, u"arcs"_s,
                           list.isEmpty() ? QJsonValue() : QJsonValue(list));
}

namespace {

/// The drop and the place in it an id names, and the sheet when it is a card's.
struct Where {
    const Shape::Drop *drop = nullptr;
    quint64 token = 0;
    QString at;
    Item item;
};

Where whereOf(const Imports *imports, const QString &id)
{
    Where where;
    if (!imports)
        return where;
    if (const auto shelf = shelfOf(id)) {
        where.drop = imports->dropOf(shelf->first);
        where.token = shelf->first;
        where.at = shelf->second;
        return where;
    }
    where.item = itemOf(cardsOf(imports), id);
    if (!where.item.sheet)
        return where;
    where.drop = dropOfCard(imports, *where.item.card);
    where.token = where.item.card->road.drop;
    where.at = inTheDrop(where.item.card->road.within, where.item.sheet->at);
    return where;
}

/// Whether an id still names something the frame shows.
bool resolves(const Imports *imports, const QString &id)
{
    if (id == WholeNode || id == Loose)
        return true;
    const QList<const Card *> cards = cardsOf(imports);
    if (const auto shelf = shelfOf(id))
        return isFrameShelf(cards, imports, shelf->first, shelf->second);
    return itemOf(cards, id).sheet != nullptr;
}

/// The id of what stands at `at` of `drop` now: the card holding it, or the shelf it is.
QString idAt(const Imports *imports, quint64 drop, const QString &at)
{
    const QList<const Card *> cards = cardsOf(imports);
    for (const Card *card : cards) {
        const QString &within = card->road.within;
        if (card->road.drop != drop || !under(at, within))
            continue;
        const QString id = QString::number(card->token) + u'/'
                           + (within.isEmpty() ? at : at.mid(within.size() + 1));
        if (itemOf(cards, id).sheet)
            return id;
    }
    if (isFrameShelf(cards, imports, drop, at))
        return shelfIdOf(drop, at);
    return WholeNode;
}

} // namespace

QVariantList ReviewEdits::roleOptions(const QString &id) const
{
    const Where where = whereOf(m_frame->imports, id);
    if (!where.drop)
        return {};
    return waiting(optionsAt(*where.drop, where.at, where.item.sheet), m_frame->imports,
                   where.token, where.at);
}

bool ReviewEdits::setRole(const QString &id, int role)
{
    using enum Shape::Role;
    if (!m_frame->imports || role < std::to_underlying(Shelf) || role > std::to_underlying(Unread))
        return false;
    const Where where = whereOf(m_frame->imports, id);
    if (!where.drop)
        return false;
    if (Shape::Role(role) == Book && where.item.sheet
        && !bookRefused(*where.item.sheet).isEmpty())
        return false;
    // Copied: the change may forget the card the item points into.
    const quint64 token = where.token;
    const QString at = where.at;
    if (!m_frame->imports->setRole(token, at, Shape::Role(role)))
        return false;
    if (m_frame->refocus(token, at))
        emit m_review->changed();
    return true;
}

bool ReviewEdits::chooseReading(const QString &id, int index)
{
    if (!m_frame->imports)
        return false;
    const Where where = whereOf(m_frame->imports, id);
    if (!where.drop)
        return false;
    const quint64 token = where.token;
    const QString at = where.at;
    if (!m_frame->imports->setReading(token, at, index))
        return false;
    if (m_frame->refocus(token, at))
        emit m_review->changed();
    return true;
}

bool Review::Frame::refocus(quint64 drop, const QString &at)
{
    // A shelf turned universe abandons its cards and reads a new one, under a new token: the
    // node and the sheet that showed them would show nothing at all.
    const bool focusGone = !resolves(imports, focus);
    const bool nodeGone = !resolves(imports, node);
    if (!focusGone && !nodeGone)
        return false;
    if (focusGone)
        focus = idAt(imports, drop, at);
    if (nodeGone) {
        const Item item = itemOf(cardsOf(imports), focus);
        node = shelfOf(focus) || inTheRail(item) ? focus : WholeNode;
    }
    return true;
}

void ReviewEdits::setLibraryWords(const QVariantMap &words)
{
    if (words == m_frame->libraryWords)
        return;
    m_frame->libraryWords = words;
    emit m_review->changed();
}
