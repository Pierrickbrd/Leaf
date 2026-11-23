#include "Facts.h"

#include "ComicInfo.h"
#include "Fold.h"
#include "Orders.h"
#include "Words.h"

#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <tuple>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

using Facts::Field;
using Facts::Origin;
using Facts::Sheet;
using Facts::Weight;
using Manifest::Level;

const QString EntryJson = u"entry.json"_s;
const QString ComicInfoXml = u"ComicInfo.xml"_s;
const QString OneShotJson = u"oneshot.json"_s;

/// What one archive says, read once: the walk already opened it, this only parses.
struct Reading {
    QString path;
    QString fileName;
    /// `entry.json`, parsed. Empty when the archive carries none.
    QJsonObject own;
    /// Whether it carries one at all: `{}` is an `entry.json` that says nothing, and is there.
    bool hasOwn = false;
    std::optional<ComicInfo::Fields> comic;
    bool carriesComicInfo = false;
    bool comicInfoUnreadable = false;
    /// The sentence over the sheet when the archive, or its `entry.json`, could not be read.
    QString trouble;
    /// `oneshot.json`, parsed, and whether it was one: an archive carrying `{}` is a book too.
    QJsonObject oneShot;
    bool book = false;
    bool oneShotUnreadable = false;
    int pages = -1;
};

/// Whether a JSON value says something. "", "  ", [] and {} say nothing — the server's
/// `checks::says_nothing` reads an empty `entry.json` the same way — and a key that says
/// nothing must not hide what ComicInfo says behind it.
bool says(const QJsonValue &value)
{
    switch (value.type()) {
    case QJsonValue::String:
        return !value.toString().trimmed().isEmpty();
    case QJsonValue::Array:
        return !value.toArray().isEmpty();
    case QJsonValue::Object:
        return !value.toObject().isEmpty();
    case QJsonValue::Double:
    case QJsonValue::Bool:
        return true;
    default:
        return false;
    }
}

QString sidecarName(Level level)
{
    switch (level) {
    case Level::Universe:
        return u"universe.json"_s;
    case Level::Edition:
        return u"edition.json"_s;
    default:
        return u"work.json"_s;
    }
}

bool isContainer(Level level)
{
    using enum Manifest::Level;
    return level == Universe || level == Work || level == Edition;
}

QString parentOf(const QString &path)
{
    const qsizetype slash = path.lastIndexOf(u'/');
    return slash < 0 ? QString() : path.left(slash);
}

QString joined(const QString &at, const QString &name)
{
    return at.isEmpty() ? name : at + u'/' + name;
}

/// The sidecar a container writes: the one on disk, path and case as they are, so a
/// `Work.json` is completed rather than shadowed by a second file the server would read
/// first; the canonical name beside the folder otherwise.
QString sidecarOf(const Manifest::Node &node, const Manifest::Folder &tree)
{
    const QString name = sidecarName(node.level);
    if (!node.declaration.isEmpty()) {
        for (const Manifest::Sidecar &one : tree.sidecars) {
            if (parentOf(one.path) == node.at &&
                QFileInfo(one.path).fileName().compare(name, Qt::CaseInsensitive) == 0)
                return one.path;
        }
    }
    return joined(node.at, name);
}

/// Whether the server's typed reading of `oneshot.json` takes this object — `OneShotJson` in
/// `metadata/sidecars.rs`, read by `sidecars::read`, which gives nothing at all for one key of
/// the wrong type. « Is a JSON object » was the test first, and `{"authors":"Jarry"}` was shown
/// as a book the scan then put in `disregarded`. A key it does not know is ignored, as there;
/// `null` is accepted where it holds an `Option` and refused where it holds a list.
bool serverReadsOneShot(const QJsonObject &object)
{
    static const QSet texts{u"title"_s,     u"medium"_s,  u"publisher"_s,
                              u"collection"_s, u"language"_s, u"readingDirection"_s,
                              u"summary"_s,   u"ageRating"_s, u"isbn"_s,
                              u"publishedOn"_s};
    static const QSet lists{u"authors"_s, u"artists"_s, u"genres"_s, u"tags"_s};
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        const QJsonValue value = it.value();
        if (texts.contains(it.key())) {
            if (!value.isNull() && !value.isString())
                return false;
        } else if (lists.contains(it.key())) {
            if (!value.isArray() ||
                !std::ranges::all_of(value.toArray(), [](const QJsonValue &one) {
                    return one.isString();
                }))
                return false;
        } else if (it.key() == u"leaf"_s) {
            // An `i32`. Qt keeps no difference between 1 and 1.0, which serde refuses for an
            // integer; the one case read differently here.
            using Range = std::numeric_limits<qint32>;
            const double number = value.toDouble();
            if (!value.isNull() &&
                (!value.isDouble() || number != std::trunc(number) || number < Range::min() ||
                 number > Range::max()))
                return false;
        } else if (it.key() == u"colour"_s && !value.isNull() && !value.isBool()) {
            return false;
        }
    }
    return true;
}

Reading readingOf(const Manifest::Entry &entry)
{
    Reading reading;
    reading.path = entry.path;
    reading.fileName = QFileInfo(entry.path).fileName();
    const Cbz::Members &members = entry.members;

    // An archive that does not open puts the same sentence in both members (`Cbz.h`): that,
    // and not a member that alone failed, is an archive nobody can read.
    if (!members.entry.opened() && members.entry.trouble == members.comicInfo.trouble) {
        reading.trouble = Words::archiveUnreadable(members.entry.trouble);
        return reading;
    }

    if (!members.entry.opened()) {
        reading.trouble = Words::sidecarUnreadable(EntryJson);
    } else if (!members.entry.sidecar.isEmpty()) {
        QJsonParseError error{};
        const QJsonDocument document = QJsonDocument::fromJson(members.entry.sidecar, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            reading.trouble = Words::sidecarUnreadable(EntryJson);
        } else {
            reading.own = document.object();
            reading.hasOwn = true;
        }
    }

    // The server's `sidecars::read` gives no book for a member it cannot parse, so neither
    // does this — and the flag keeps « Livre seul » from writing a new one over it.
    if (!members.oneShot.opened()) {
        reading.oneShotUnreadable = true;
    } else if (!members.oneShot.sidecar.isEmpty()) {
        QJsonParseError error{};
        const QJsonDocument document = QJsonDocument::fromJson(members.oneShot.sidecar, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject() ||
            !serverReadsOneShot(document.object())) {
            reading.oneShotUnreadable = true;
        } else {
            reading.oneShot = document.object();
            reading.book = true;
        }
    }

    reading.pages = members.pages;
    reading.carriesComicInfo = !members.comicInfo.opened() || !members.comicInfo.sidecar.isEmpty();
    reading.comicInfoUnreadable = !members.comicInfo.opened();
    if (members.comicInfo.opened() && !members.comicInfo.sidecar.isEmpty())
        reading.comic = ComicInfo::read(members.comicInfo.sidecar, &reading.comicInfoUnreadable);
    return reading;
}

Field fieldNamed(const QString &key, Weight weight = Weight::Optional)
{
    Field field;
    field.key = key;
    field.weight = weight;
    return field;
}

/// Said, when the Leaf sidecar has the key and it says something.
bool fromJson(Field &field, const QJsonObject &said, const QString &from)
{
    const QJsonValue value = said.value(field.key);
    if (!says(value))
        return false;
    field.value = value;
    field.origin = Origin::Said;
    field.from = from;
    return true;
}

using Pick = std::function<QJsonValue(const ComicInfo::Fields &)>;

/// Taken from the first file, in walk order, whose ComicInfo says it.
///
/// The format copies the series into every volume, and the copies drift — one volume of a
/// Komga series says Pika and the next Kana. The first is kept, since something has to be.
/// They used to be counted, and the field said « 3 tomes disent autre chose que Pika » —
/// a question about where a value was read, which is no longer asked: the field shows the
/// value, and whoever imports corrects it if it is wrong.
template <typename P>
bool fromComicInfo(Field &field, const QList<const Reading *> &files, const P &pick)
{
    QJsonValue kept;
    for (const Reading *file : files) {
        if (!file->comic)
            continue;
        if (const QJsonValue value = pick(*file->comic); says(value)) {
            kept = value;
            break;
        }
    }
    if (!says(kept))
        return false;
    field.value = kept;
    field.origin = Origin::Taken;
    field.from = ComicInfoXml;
    return true;
}

void guessed(Field &field, const QJsonValue &value, const QString &from)
{
    field.value = value;
    field.origin = Origin::Guessed;
    field.from = from;
}

QJsonValue text(const QString &value)
{
    return value.trimmed().isEmpty() ? QJsonValue() : QJsonValue(value);
}

QJsonValue list(const QStringList &values)
{
    return values.isEmpty() ? QJsonValue() : QJsonValue(QJsonArray::fromStringList(values));
}

/// The people a ComicInfo tag names: `Writer` and `Penciller` are lists cut on commas, as
/// `Genre` is and as the server cuts them, so "Arleston, Tarquin" is two people and not one
/// called by both names.
QJsonValue people(const QString &tag)
{
    QStringList names;
    for (const QString &piece : tag.split(u','))
        if (const QString name = piece.trimmed(); !name.isEmpty())
            names << name;
    return list(names);
}

/// The edition fields, in the order the sheet shows them, with what ComicInfo copies of each.
/// `status` has no ComicInfo tag the server reads, and is never guessed.
QList<std::pair<Field, Pick>> editionFields()
{
    return {
        {fieldNamed(u"publisher"_s), [](const ComicInfo::Fields &c) { return text(c.publisher); }},
        {fieldNamed(u"language"_s), [](const ComicInfo::Fields &c) { return text(c.language); }},
        {fieldNamed(u"readingDirection"_s, Weight::Required),
         [](const ComicInfo::Fields &c) { return text(c.readingDirection); }},
        {fieldNamed(u"status"_s), [](const ComicInfo::Fields &) { return QJsonValue(); }},
        {fieldNamed(u"volumeCount"_s),
         [](const ComicInfo::Fields &c) {
             return c.volumeCount.has_value() ? QJsonValue(*c.volumeCount) : QJsonValue();
         }},
    };
}

Shape::Role roleOf(Level level)
{
    switch (level) {
    case Level::Universe:
        return Shape::Role::Universe;
    case Level::Edition:
        return Shape::Role::Edition;
    case Level::Book:
        return Shape::Role::Book;
    case Level::Chapter:
    case Level::Volume:
        return Shape::Role::Volume;
    case Level::Work:
        break;
    }
    return Shape::Role::Work;
}

class Reader
{
public:
    Reader(const Manifest::Folder &tree, const Shape::Roles &roles) : m_tree(tree), m_roles(roles)
    {
        for (const Manifest::Entry &entry : tree.files)
            m_readings.try_emplace(entry.path, readingOf(entry));
    }

    QList<Sheet> sheetsOf(const Manifest::Node &card)
    {
        visit(card, nullptr);
        return m_sheets;
    }

private:
    const Manifest::Folder &m_tree;
    const Shape::Roles &m_roles;
    /// A map and not a hash: `filesUnder` keeps pointers into it while `readingAt` may still
    /// add a file the walk did not open, and a node-based map never moves what it holds.
    std::map<QString, Reading> m_readings;
    QList<Sheet> m_sheets;

    const Reading *readingAt(const QString &path)
    {
        auto found = m_readings.find(path);
        if (found == m_readings.end())
        {
            Reading unopened;
            unopened.path = path;
            unopened.fileName = QFileInfo(path).fileName();
            found = m_readings.try_emplace(path, unopened).first;
        }
        return &found->second;
    }

    /// The files a container's values come from, in walk order — sub-folders first, then its
    /// own files, as `Manifest` walks — and never past another series or universe.
    void filesUnder(const Manifest::Node &node, QList<const Reading *> &into, bool top = true)
    {
        if (!top && (node.level == Level::Universe || node.level == Level::Work))
            return;
        for (const Manifest::Node &child : node.children)
            filesUnder(child, into, false);
        // A book is lifted out of the series its folder is — the scan's `write_one_shot` —
        // so what its ComicInfo says is not the series'.
        for (const Manifest::Entry &file : node.files) {
            if (const Reading *reading = readingAt(file.path); !isBook(*reading))
                into << reading;
        }
    }

    /// Whether an archive is a whole book: its own `oneshot.json` says so, or somebody chose
    /// it and nothing stands in the way — an archive that did not open, or one carrying an
    /// `oneshot.json` nobody could read, which a new one would be written over.
    bool isBook(const Reading &file) const
    {
        if (file.book)
            return true;
        const auto chosen = m_roles.constFind(file.path);
        return chosen != m_roles.cend() && chosen->role == Shape::Role::Book &&
               file.trouble.isEmpty() && !file.oneShotUnreadable;
    }

    QString folderName(const Manifest::Node &node) const
    {
        return node.at.isEmpty() ? m_tree.root : node.at.section(u'/', -1);
    }

    void visit(const Manifest::Node &node, const QJsonObject *work)
    {
        // Declared is said whatever the roles say; undeclared, the scan reads it by its files
        // (Evident) unless the shape or a click made it something.
        Shape::Choice choice{roleOf(node.level),
                             node.declared ? Shape::Origin::Said : Shape::Origin::Evident};
        if (!node.declared)
            choice = m_roles.value(node.at, choice);
        // A shelf is walked through and never kept (`scan/layout.rs`): no sheet, no sidecar,
        // and the archives in one are read by nobody.
        if (choice.role == Shape::Role::Shelf) {
            for (const Manifest::Node &child : node.children)
                visit(child, work);
            for (const Manifest::Entry &file : node.files)
                m_sheets << fileSheet(*readingAt(file.path), true);
            return;
        }

        // **The sidecar's name and the fields come from `node.level`, not from `choice`**: the
        // role gives the origin and nothing else. `Shape::cardNode` is what turns a role into a
        // level, so a role changed in the drop is read here only once `cardNode` has been
        // made again from it — reading the old node with the new roles mixes two readings.
        QJsonObject said;
        Sheet sheet;
        sheet.role = choice.role;
        sheet.roleOrigin = choice.origin;
        sheet.level = node.level;
        sheet.at = node.at;
        sheet.name = node.name;
        sheet.sidecar = sidecarOf(node, m_tree);
        if (!node.declaration.isEmpty()) {
            QJsonParseError error{};
            const QJsonDocument document = QJsonDocument::fromJson(node.declaration, &error);
            if (error.error != QJsonParseError::NoError || !document.isObject())
                sheet.trouble = Words::sidecarUnreadable(QFileInfo(sheet.sidecar).fileName());
            else
                said = document.object();
        }
        if (sheet.trouble.isEmpty())
            sheet.fields = containerFields(node, said, work);
        if (node.level == Level::Work)
            sheet.medium = said.value(u"medium"_s).toString();
        m_sheets << sheet;

        const QJsonObject *workBelow = node.level == Level::Work ? &said : work;
        for (const Manifest::Node &child : node.children)
            visit(child, workBelow);
        // `visit_books_of`: at a universe's root the scan reads books and nothing else.
        for (const Manifest::Entry &file : node.files)
            m_sheets << fileSheet(*readingAt(file.path), node.level == Level::Universe);
    }

    QList<Field> editionContainerFields(const Manifest::Node &node, const QJsonObject &said,
                                        const QJsonObject *work,
                                        const QList<const Reading *> &files) const
    {
        const QString from = sidecarName(node.level);
        QList<Field> fields;
        Field name = fieldNamed(u"name"_s);
        if (!fromJson(name, said, from))
            guessed(name, folderName(node), folderName(node));
        fields << name;
        // What `work.json` already says is the edition's default, and said: asked again,
        // the reader would type the same publisher once per edition.
        for (auto [field, pick] : editionFields()) {
            if (work && says(work->value(field.key)))
                field.fromWork = work->value(field.key);
            if (!fromJson(field, said, from) &&
                !(work && fromJson(field, *work, sidecarName(Level::Work))))
                fromComicInfo(field, files, pick);
            fields << field;
        }
        return fields;
    }

    QList<Field> containerFields(const Manifest::Node &node, const QJsonObject &said,
                                 const QJsonObject *work)
    {
        const QString from = sidecarName(node.level);
        QList<const Reading *> files;
        filesUnder(node, files);
        QList<Field> fields;

        if (node.level == Level::Universe) {
            Field name = fieldNamed(u"name"_s);
            if (!fromJson(name, said, from))
                guessed(name, folderName(node), folderName(node));
            // Said and never guessed: nothing in a folder or an archive says how a universe is
            // read, and an order made up would be written over the author's silence.
            Field orders = fieldNamed(u"orders"_s);
            fromJson(orders, said, from);
            Field defaultOrder = fieldNamed(u"defaultOrder"_s);
            fromJson(defaultOrder, said, from);
            return {name, orders, defaultOrder};
        }

        if (node.level == Level::Edition)
            return editionContainerFields(node, said, work, files);

        Field title = fieldNamed(u"title"_s, Weight::Required);
        if (!fromJson(title, said, from) &&
            !fromComicInfo(title, files,
                           [](const ComicInfo::Fields &c) { return text(c.series); }))
            guessed(title, folderName(node), folderName(node));
        fields << title;

        // Said and never taken: ComicInfo copies the writer of a volume into every volume, and
        // taking the first volume's for the series froze it on one name — the sheet shows the
        // union of the volumes (`Sheet::Credits::credited`) and writes none of it. The singular `author`
        // is what `work.json` said before `authors` existed, and the server still reads it as
        // a one-name list (`WorkJson::authors`): a file written then would otherwise show its
        // author missing.
        //
        // Important, and so blocking while nobody is named: by the series or by any of its
        // volumes (`lacks`). Left empty on a series whose volumes credit someone, the field is
        // not missing — the union stands for it.
        Field authors = fieldNamed(u"authors"_s, Weight::Required);
        if (!fromJson(authors, said, from)) {
            if (const QJsonValue one = said.value(u"author"_s); says(one)) {
                authors.value = QJsonArray{one};
                authors.origin = Origin::Said;
                authors.from = from;
            }
        }
        fields << authors;

        // The drawing, said as the writing is and for the same reason. `work.json` has carried
        // `artists` as long as `authors`, but the sheet offered no box for it: a series whose
        // volumes name no drawer had nowhere to be given one.
        Field artists = fieldNamed(u"artists"_s, Weight::Required);
        fromJson(artists, said, from);
        fields << artists;

        Field genres = fieldNamed(u"genres"_s);
        if (!fromJson(genres, said, from))
            fromComicInfo(genres, files, [](const ComicInfo::Fields &c) { return list(c.genres); });
        fields << genres;

        // No ComicInfo tag says them: a series is tagged by whoever wrote `work.json`.
        Field tags = fieldNamed(u"tags"_s);
        fromJson(tags, said, from);
        fields << tags;

        // How a chapter of this series is called — « Chap.{n:00} », « Page {n:000} »: the format
        // the series' chapters wear when they have no title of their own. Said in `work.json`
        // and nowhere else, and left empty — the volumes then say « Chapitre N » by default.
        Field chapterLabel = fieldNamed(u"chapterLabel"_s);
        fromJson(chapterLabel, said, from);
        fields << chapterLabel;

        // The arcs and sagas are the series': they run across volumes, and an edition holds
        // some of them. Said in `work.json`; an `edition.json` that carries them is a mistake
        // the server ignores and this sheet does not read.
        Field arcs = fieldNamed(u"arcs"_s);
        fromJson(arcs, said, from);
        fields << arcs;

        // An implicit edition has no folder to hold its fields, so `work.json` holds them —
        // the format's own rule for a single-edition work.
        if (const bool declaresEditions =
                std::ranges::any_of(node.children, [](const Manifest::Node &child) {
                    return child.level == Level::Edition;
                });
            !declaresEditions) {
            for (auto [field, pick] : editionFields()) {
                if (!fromJson(field, said, from))
                    fromComicInfo(field, files, pick);
                fields << field;
            }
        }
        return fields;
    }

    /// `unreadHere`: the folder holding it files nothing that is not a book.
    Sheet fileSheet(const Reading &file, bool unreadHere) const
    {
        const auto chosen = m_roles.constFind(file.path);
        const bool said = chosen != m_roles.cend();
        if (isBook(file))
            return bookSheet(file, file.book ? Shape::Origin::Said : chosen->origin);
        if (unreadHere || (said && chosen->role == Shape::Role::Unread)) {
            // Read by nobody, so nothing to ask of it but the role: the fields would be
            // questions about a file the library will never show.
            Sheet sheet = described(file, Level::Volume);
            sheet.role = Shape::Role::Unread;
            // Guessed unless somebody chose it, as the shape gives it: the reader has to see
            // what will not be read, and a « Non lu » chosen is not asked again.
            sheet.roleOrigin = said && chosen->role == Shape::Role::Unread
                                   ? chosen->origin
                                   : Shape::Origin::Guessed;
            return sheet;
        }
        Sheet sheet = volumeSheet(file);
        if (said && chosen->role == Shape::Role::Volume)
            sheet.roleOrigin = chosen->origin;
        return sheet;
    }

    /// What every archive's sheet carries whatever it turns out to be.
    static Sheet described(const Reading &file, Level level)
    {
        Sheet sheet;
        sheet.level = level;
        sheet.role = roleOf(level);
        sheet.at = file.path;
        sheet.name = file.fileName;
        sheet.sidecar = file.path;
        sheet.trouble = file.trouble;
        sheet.comicInfo.carried = file.carriesComicInfo;
        sheet.comicInfo.read = file.comic.has_value();
        sheet.comicInfo.unreadable = file.comicInfoUnreadable;
        sheet.own = file.own;
        sheet.hasOwn = file.hasOwn;
        sheet.oneShot = file.oneShot;
        sheet.oneShotUnreadable = file.oneShotUnreadable;
        sheet.contents.pages = file.pages;
        sheet.contents.series = file.own.value(u"work"_s).toString().trimmed();
        if (sheet.contents.series.isEmpty() && file.comic)
            sheet.contents.series = file.comic->series.trimmed();
        for (const QJsonValue &one : file.own.value(u"chapters"_s).toArray()) {
            if (one.toObject().value(u"number"_s).isDouble()) {
                sheet.contents.chapters << one.toObject().value(u"number"_s).toDouble();
                const QJsonValue page = one.toObject().value(u"startPage"_s);
                sheet.contents.starts << Sheet::ChapterStart{
                    one.toObject().value(u"number"_s).toDouble(),
                    page.isDouble() ? std::optional<int>(page.toInt()) : std::nullopt};
            }
        }
        return sheet;
    }

    /// A whole book: `oneshot.json` holds what a series, its edition and a volume would, and
    /// has no number — there is nothing it is the n-th of.
    static Sheet bookSheet(const Reading &file, Shape::Origin origin)
    {
        Sheet sheet = described(file, Level::Book);
        sheet.roleOrigin = origin;

        Field title = fieldNamed(u"title"_s, Weight::Required);
        if (!fromJson(title, file.oneShot, OneShotJson) &&
            !fromComicInfo(title, {&file},
                           [](const ComicInfo::Fields &c) { return text(c.title); }) &&
            !fromComicInfo(title, {&file},
                           [](const ComicInfo::Fields &c) { return text(c.series); }))
            guessed(title, QFileInfo(file.fileName).completeBaseName(), file.fileName);
        sheet.fields << title;

        const QList<std::tuple<QString, Weight, Pick>> rest{
            // A book is its own series: nobody else's volumes say who wrote and drew it, so its
            // credits block as a series' do when its volumes name nobody.
            {u"authors"_s, Weight::Required,
             [](const ComicInfo::Fields &c) { return people(c.author); }},
            {u"artists"_s, Weight::Required,
             [](const ComicInfo::Fields &c) { return people(c.artist); }},
            {u"publisher"_s, Weight::Optional,
             [](const ComicInfo::Fields &c) { return text(c.publisher); }},
            {u"language"_s, Weight::Optional,
             [](const ComicInfo::Fields &c) { return text(c.language); }},
            {u"readingDirection"_s, Weight::Required,
             [](const ComicInfo::Fields &c) { return text(c.readingDirection); }},
            {u"genres"_s, Weight::Optional,
             [](const ComicInfo::Fields &c) { return list(c.genres); }},
            {u"summary"_s, Weight::Optional,
             [](const ComicInfo::Fields &c) { return text(c.summary); }},
            {u"isbn"_s, Weight::Optional, [](const ComicInfo::Fields &c) { return text(c.isbn); }},
            {u"publishedOn"_s, Weight::Optional,
             [](const ComicInfo::Fields &c) { return text(c.publishedOn); }},
        };
        for (const auto &[key, weight, pick] : rest) {
            Field field = fieldNamed(key, weight);
            if (!fromJson(field, file.oneShot, OneShotJson))
                fromComicInfo(field, {&file}, pick);
            sheet.fields << field;
        }
        return sheet;
    }

    static Sheet volumeSheet(const Reading &file)
    {
        // A chapter when its `entry.json` says so, or when its name does — the server's
        // `entry_kind`, which reads both.
        const bool chapter =
            file.own.value(u"type"_s).toString().compare(u"CHAPTER"_s, Qt::CaseInsensitive) == 0 ||
            Manifest::levelOf(file.fileName) == Level::Chapter;
        Sheet sheet = described(file, chapter ? Level::Chapter : Level::Volume);

        Field number = fieldNamed(u"number"_s, Weight::Required);
        // A double only: a number written as a string is not the server's `number` either,
        // and taking it would announce a volume the server then files without one.
        if (const bool numberSaid = sheet.trouble.isEmpty() &&
                                    file.own.value(u"number"_s).isDouble() &&
                                    fromJson(number, file.own, EntryJson);
            !numberSaid) {
            const bool taken = !chapter && file.comic && file.comic->number.has_value() &&
                               fromComicInfo(number, {&file}, [](const ComicInfo::Fields &c) {
                                   return QJsonValue(*c.number);
                               });
            if (!taken) {
                if (const std::optional<double> fromName = Manifest::numberOf(file.fileName))
                    guessed(number, *fromName, file.fileName);
            }
        }
        sheet.fields << number;
        // Unreadable: a number from the name and nothing else, since nothing else was read.
        if (!sheet.trouble.isEmpty())
            return sheet;

        const QList<std::pair<QString, Pick>> rest{
            {u"title"_s, [](const ComicInfo::Fields &c) { return text(c.title); }},
            {u"authors"_s, [](const ComicInfo::Fields &c) { return people(c.author); }},
            {u"artists"_s, [](const ComicInfo::Fields &c) { return people(c.artist); }},
            {u"publishedOn"_s, [](const ComicInfo::Fields &c) { return text(c.publishedOn); }},
            {u"isbn"_s, [](const ComicInfo::Fields &c) { return text(c.isbn); }},
            {u"summary"_s, [](const ComicInfo::Fields &c) { return text(c.summary); }},
            {u"chapters"_s,
             [](const ComicInfo::Fields &c) {
                 QJsonArray chapters;
                 for (const QString &name : c.chapters)
                     chapters << QJsonObject{{u"raw"_s, name}};
                 return chapters.isEmpty() ? QJsonValue() : QJsonValue(chapters);
             }},
        };
        for (const auto &[key, pick] : rest) {
            // Who wrote and who drew are required of every volume: there is no volume nobody
            // made, and a library searched by name finds none it was not told. Its series
            // says them for it when it says nothing (`lacks`).
            const bool credit = key == u"authors"_s || key == u"artists"_s;
            Field field = fieldNamed(key, credit ? Weight::Required : Weight::Optional);
            if (!fromJson(field, file.own, EntryJson) && fromComicInfo(field, {&file}, pick)
                && credit)
                (key == u"authors"_s ? sheet.credits.comic.authors : sheet.credits.comic.artists) =
                    field.value.toVariant().toStringList();
            sheet.fields << field;
        }
        return sheet;
    }
};

Field *fieldIn(Sheet &sheet, const QString &key)
{
    for (Field &field : sheet.fields) {
        if (field.key == key)
            return &field;
    }
    return nullptr;
}

const Field *fieldIn(const Sheet &sheet, const QString &key)
{
    for (const Field &field : sheet.fields) {
        if (field.key == key)
            return &field;
    }
    return nullptr;
}

/// Two files of one edition at one number: the server would keep one, and nobody would
/// know which. An edition is a folder, so the files of one edition are the files of one
/// folder; a chapter and a volume of the same number are not a duplicate, since a chapter
/// stands beside the volumes and never in their order.
void weighNumbers(QList<Sheet> &sheets)
{
    std::map<std::tuple<QString, int, double>, QList<Field *>> claims;
    for (Sheet &sheet : sheets) {
        if (isContainer(sheet.level))
            continue;
        Field *number = fieldIn(sheet, u"number"_s);
        if (!number)
            continue;
        number->doubt.clear();
        if (number->origin != Origin::Missing)
            claims[{parentOf(sheet.at), std::to_underlying(sheet.level), number->value.toDouble()}]
                << number;
    }
    for (const auto &[claim, fields] : claims) {
        if (fields.size() < 2)
            continue;
        for (Field *field : fields)
            field->doubt = Words::sameNumberTwice(std::get<2>(claim));
    }
}

/// The names a list field holds, trimmed, empties dropped.
QStringList namesIn(const Field &field)
{
    QStringList names;
    for (const QJsonValue &one : field.value.toArray())
        if (const QString name = one.toString().trimmed(); !name.isEmpty())
            names << name;
    return names;
}

/// The nearest series above `at` among `series`, by path: a series nested in another keeps
/// its own volumes.
template <typename S>
S *nearestSeries(const std::map<QString, S *> &series, const QString &at)
{
    for (QString up = parentOf(at);; up = parentOf(up)) {
        if (const auto found = series.find(up); found != series.end())
            return found->second;
        if (up.isEmpty())
            return nullptr;
    }
}

/// Whether a sheet is one volume or chapter of a series, which credits it: a book is lifted
/// out of the series it sits in, and an archive nobody will read is no volume.
bool isCredited(const Sheet &sheet)
{
    return !isContainer(sheet.level) && sheet.level != Level::Book &&
           sheet.role != Shape::Role::Unread;
}

/// Lets ComicInfo fill `field`, the volume's `which` credit, when nothing else does.
///
/// ComicInfo only fills a hole: the volume's own word or its series' default (`names`) comes
/// first, and a ComicInfo saying somebody else is not taken, nor written, nor asked about.
/// Decided here and again on every edit, since the series' default is what decides and it
/// can be typed or emptied.
void fillFromComic(const Sheet &sheet, Field &field, int which, const QStringList &names)
{
    using enum Facts::Origin;
    if (field.edited || field.origin == Said)
        return;
    const QStringList &comic =
        which == 0 ? sheet.credits.comic.authors : sheet.credits.comic.artists;
    const bool fills = names.isEmpty() && !comic.isEmpty();
    field.value = fills ? QJsonValue(QJsonArray::fromStringList(comic)) : QJsonValue();
    field.origin = fills ? Taken : Missing;
    field.from = fills ? ComicInfoXml : QString();
}

/// What `owner` says of `sheet`'s credits (`Sheet::Credits::fallback`), what its ComicInfo fills of
/// them, and whether it lacks them, counted on `owner`.
void creditVolume(Sheet &sheet, Sheet &owner, const QStringList &keys)
{
    for (int which = 0; which < 2; ++which) {
        const Field *own = fieldIn(owner, keys.at(which));
        const QStringList names = own ? namesIn(*own) : QStringList();
        (which == 0 ? sheet.credits.fallback.authors : sheet.credits.fallback.artists) = names;
        Field *field = fieldIn(sheet, keys.at(which));
        if (!field)
            continue;
        fillFromComic(sheet, *field, which, names);
        if (lacks(sheet, *field))
            ++(which == 0 ? owner.credits.authorsLacking : owner.credits.artistsLacking);
    }
}

/// Adds to `into` who `ordered` are credited with for `credit`, once each by
/// `Fold::searchKey`, under the spelling met first.
void unite(QStringList &into, const QList<Sheet *> &ordered, const QString &credit)
{
    QSet<QString> counted;
    for (const Sheet *sheet : ordered) {
        for (const QString &name : creditedWith(*sheet, credit)) {
            if (const QString key = Fold::searchKey(name); !counted.contains(key)) {
                counted.insert(key);
                into << name;
            }
        }
    }
}

/// Who each volume is credited with, and what its series is.
///
/// A volume that names nobody is credited with its series' `authors` and `artists`, as the
/// server credits it (`Sheet::Credits::fallback`); one that names nobody under a series that names
/// nobody lacks them, and that is a fact rather than a suspicion. It used to be weighed
/// against its neighbours: one volume credited made the forty-one others « Manquant », and
/// none credited asked nothing at all — the same absence, read two opposite ways. Now each
/// volume lacks or does not, and the series counts its volumes that do, because the series
/// is where one name typed puts them all right.
///
/// The union (`Sheet::Credits::credited`): the server counts a person once, by `search_key`, under
/// the spelling it met first in its own file order. « Arleston » and « ARLESTON » on two
/// volumes made two authors, and a count of twelve that was eleven; « D’Aviau » and
/// « DAviau », folded otherwise than the server folds them, still made two.
void weighCredits(QList<Sheet> &sheets)
{
    const QStringList keys{u"authors"_s, u"artists"_s};
    // Each volume is credited to the nearest series above it: a series nested in another
    // keeps its own volumes, and its parent's union is not made of them.
    std::map<QString, Sheet *> series;
    for (Sheet &sheet : sheets) {
        sheet.credits.fallback = {};
        if (sheet.level == Level::Work) {
            sheet.credits.credited = {};
            sheet.credits.authorsLacking = 0;
            sheet.credits.artistsLacking = 0;
            series.try_emplace(sheet.at, &sheet);
        }
    }
    QHash<Sheet *, QList<Sheet *>> volumes;
    for (Sheet &sheet : sheets) {
        if (!isCredited(sheet))
            continue;
        Sheet *owner = nearestSeries(series, sheet.at);
        if (!owner)
            continue;
        volumes[owner] << &sheet;
        creditVolume(sheet, *owner, keys);
    }
    for (auto owner = volumes.begin(); owner != volumes.end(); ++owner) {
        QList<Sheet *> &ordered = owner.value();
        std::ranges::stable_sort(ordered, [&owner](const Sheet *one, const Sheet *other) {
            return Facts::readBefore(owner.key()->at, one->at, other->at);
        });
        for (int which = 0; which < 2; ++which) {
            QStringList &into =
                which == 0 ? owner.key()->credits.credited.authors
                           : owner.key()->credits.credited.artists;
            unite(into, ordered, keys.at(which));
        }
    }
}

/// The orders of a universe sheet as the server will read them, nothing when it says none.
std::optional<Orders::Universe> ordersOf(const Sheet &sheet)
{
    if (sheet.level != Level::Universe)
        return std::nullopt;
    for (const Field &field : sheet.fields) {
        if (field.key == u"orders"_s && field.origin != Origin::Missing)
            return Orders::read(QJsonObject{{u"orders"_s, field.value}});
    }
    return std::nullopt;
}

/// What is wrong with the orders of a universe, said on their field. An inclusion the
/// announcement refuses (`blocks`) comes first, named by its order and step as it names them;
/// an order the server would cut comes after, as a reason to look.
void weighOrders(QList<Sheet> &sheets)
{
    for (Sheet &sheet : sheets) {
        const std::optional<Orders::Universe> universe = ordersOf(sheet);
        Field *field = fieldIn(sheet, u"orders"_s);
        if (!field)
            continue;
        field->doubt.clear();
        if (!universe)
            continue;
        if (const QList<Orders::Refused> refused = Orders::refused(*universe); !refused.isEmpty()) {
            const Orders::Refused &first = refused.constFirst();
            field->doubt = Words::orderRefused(
                Orders::declaredId(universe->orders.at(first.order), first.order), first.step + 1,
                first.why, first.target);
        } else if (Orders::pastCeilings(*universe)) {
            field->doubt = Words::ordersPastTheCeiling();
        }
    }
}

bool adds(const Field &field)
{
    return field.edited || field.origin == Origin::Taken || field.origin == Origin::Guessed;
}

/// Puts every field that adds something into `object`. An edited field that ended Missing
/// is removed; a Said one that was not edited is already where it came from.
void complete(QJsonObject &object, const Sheet &sheet)
{
    for (const Field &field : sheet.fields) {
        if (!adds(field))
            continue;
        // The singular `author` is read as the series' authors when `authors` says nothing
        // (`WorkJson::authors`): left behind, it came back the moment the written authors
        // were removed to go back to the volumes' union.
        if (field.key == u"authors"_s && sheet.level == Level::Work)
            object.remove(u"author"_s);
        if (field.origin == Origin::Missing)
            object.remove(field.key);
        else
            object.insert(field.key, field.value);
    }
}

/// A list of names as the server's search compares two: folded, order ignored.
QStringList folded(const QStringList &names)
{
    QStringList keys;
    for (const QString &name : names)
        keys << Fold::searchKey(name);
    keys.sort();
    return keys;
}

/// The list of names carried by more than half of `perVolume`, compared as `folded`, and
/// spelt as it was first met. Half is no majority, and a list naming nobody wins nothing.
std::optional<QStringList> majorityOf(const QList<QStringList> &perVolume)
{
    QHash<QStringList, int> counts;
    QHash<QStringList, QStringList> first;
    for (const QStringList &names : perVolume) {
        if (names.isEmpty())
            continue;
        const QStringList key = folded(names);
        // `contains` and `insert`, not `tryEmplace`: that is Qt 6.9, and the client is built
        // against the 6.4 of the distribution it ships on, where it does not compile.
        if (!first.contains(key))
            first.insert(key, names);
        if (++counts[key] * 2 > perVolume.size())
            return first.value(key);
    }
    return std::nullopt;
}

/// What a series' `work.json` is to say of its credits, by the series' path, and the keys
/// each volume leaves to its series, by the volume's.
struct Raised {
    QHash<QString, QJsonObject> into;
    QHash<QString, QSet<QString>> inherits;
};

/// Raises `key` of `owner`'s `volumes` into `raised`, when `owner` names nobody for it and
/// the strict majority of its volumes agree.
void raiseInto(Raised &raised, const Sheet &owner, const QList<const Sheet *> &volumes,
               const QString &key)
{
    if (const Field *own = fieldIn(owner, key);
        own && (own->origin != Origin::Missing || own->edited))
        return;
    // Read off a ComicInfo or typed here: what has no Leaf file saying it yet.
    const auto placed = [&key](const Sheet *sheet) {
        const Field *field = fieldIn(*sheet, key);
        return field && (field->origin == Origin::Taken || field->edited)
               && !namesIn(*field).isEmpty();
    };
    if (std::ranges::none_of(volumes, placed))
        return;
    // In the order the scan reads them, so the spelling kept is the first one met.
    QList<const Sheet *> ordered = volumes;
    std::ranges::stable_sort(ordered, [&owner](const Sheet *one, const Sheet *other) {
        return Facts::readBefore(owner.at, one->at, other->at);
    });
    QList<QStringList> perVolume;
    for (const Sheet *sheet : std::as_const(ordered)) {
        const Field *field = fieldIn(*sheet, key);
        perVolume << (field ? namesIn(*field) : QStringList());
    }
    const std::optional<QStringList> majority = majorityOf(perVolume);
    if (!majority)
        return;
    raised.into[owner.at].insert(key, QJsonArray::fromStringList(*majority));
    const QStringList winning = folded(*majority);
    for (int at = 0; at < ordered.size(); ++at) {
        if (placed(ordered.at(at)) && folded(perVolume.at(at)) == winning)
            raised.inherits[ordered.at(at)->at].insert(key);
    }
}

/// Who a series' volumes are credited with, raised into the series where most of them agree.
///
/// A hundred identical ComicInfo were a hundred copies of one name in a hundred archives:
/// the name the strict majority of the volumes carries becomes the series' own, and only the
/// volumes that differ say theirs. Only for a series that names nobody itself, and only once
/// something read or typed needs a place: what an `entry.json` already says is never moved,
/// though it counts in the majority.
Raised raise(const QList<Sheet> &sheets)
{
    std::map<QString, const Sheet *> series;
    for (const Sheet &sheet : sheets)
        if (sheet.level == Level::Work)
            series.try_emplace(sheet.at, &sheet);
    QHash<const Sheet *, QList<const Sheet *>> volumes;
    for (const Sheet &sheet : sheets)
        if (isCredited(sheet) && sheet.trouble.isEmpty())
            if (const Sheet *owner = nearestSeries(series, sheet.at))
                volumes[owner] << &sheet;

    Raised raised;
    for (auto owner = volumes.cbegin(); owner != volumes.cend(); ++owner) {
        for (const QString &key : {u"authors"_s, u"artists"_s})
            raiseInto(raised, *owner.key(), owner.value(), key);
    }
    return raised;
}

/// `QJsonObject` keeps its keys sorted, so one reading is always one set of bytes: the
/// server remembers a rewrite by what it asked for, and bytes that moved between two
/// announcements of the same folder would make it send the archive again.
QByteArray compact(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QJsonObject started()
{
    return QJsonObject{{u"leaf"_s, 1}};
}

} // namespace

namespace Facts {

QList<Sheet> read(const Manifest::Node &card, const Manifest::Folder &tree,
                  const Shape::Roles &roles)
{
    QList<Sheet> sheets = Reader(tree, roles).sheetsOf(card);
    weighNumbers(sheets);
    weighCredits(sheets);
    weighOrders(sheets);
    return sheets;
}

ProposedSplice proposedSplice(const Sheet &source, std::optional<double> chapter, int from, int to)
{
    if (!chapter.has_value())
        return {from, to, true};
    for (qsizetype at = 0; at < source.contents.starts.size(); ++at) {
        const Sheet::ChapterStart &one = source.contents.starts.at(at);
        if (one.number != *chapter || !one.page.has_value())
            continue;
        int end = std::max(*one.page, source.contents.pages - 1);
        for (qsizetype next = at + 1; next < source.contents.starts.size(); ++next) {
            if (source.contents.starts.at(next).page.has_value()) {
                end = *source.contents.starts.at(next).page - 1;
                break;
            }
        }
        return {*one.page, std::max(*one.page, end), false};
    }
    return {from, to, true};
}

bool edit(QList<Sheet> &sheets, const QString &at, const QString &key, const QJsonValue &value)
{
    using enum Facts::Origin;
    for (Sheet &sheet : sheets) {
        if (sheet.at != at)
            continue;
        if (!sheet.trouble.isEmpty())
            return false;
        Field *field = fieldIn(sheet, key);
        if (!field)
            return false;
        const bool something = says(value);
        // What it already holds is no correction, except a guess: given again, it is said.
        // Taken for one, the same value retyped marked the field edited, re-announced the
        // card and asked for an archive to be rewritten into what it already said.
        if (const bool same = something ? field->origin != Missing && field->value == value
                                        : field->origin == Missing;
            same && field->origin != Guessed)
            return false;
        field->value = something ? value : QJsonValue();
        field->origin = something ? Said : Missing;
        field->edited = true;
        // Corrected is settled: the disagreement it carried was about the value it replaced.
        field->doubt.clear();
        weighNumbers(sheets);
        weighCredits(sheets);
        weighOrders(sheets);
        return true;
    }
    return false;
}

bool readBefore(const QString &series, const QString &a, const QString &b)
{
    const auto partsOf = [&series](const QString &path) {
        const QString below = series.isEmpty() ? path : path.mid(series.size() + 1);
        QList<QByteArray> parts;
        for (const QString &part : below.split(u'/'))
            parts << part.toUtf8();
        return parts;
    };
    const QList<QByteArray> one = partsOf(a);
    const QList<QByteArray> other = partsOf(b);
    // Beside the folder first: `layout::archives` before `layout::sub_folders`.
    if ((one.size() == 1) != (other.size() == 1))
        return one.size() == 1;
    // A path sorts by its components, and a component by its bytes: « A/x » before « A-B/x »,
    // which byte by byte on the whole path would be the other way.
    return std::ranges::lexicographical_compare(one, other);
}

QStringList creditedWith(const Sheet &volume, const QString &key)
{
    const Field *field = fieldIn(volume, key);
    if (const QStringList own =
            field && field->origin != Origin::Missing ? namesIn(*field) : QStringList();
        !own.isEmpty())
        return own;
    return key == u"authors"_s ? volume.credits.fallback.authors : volume.credits.fallback.artists;
}

bool lacks(const Sheet &sheet, const Field &field)
{
    if (field.weight != Weight::Required || field.origin != Origin::Missing)
        return false;
    const bool credit = field.key == u"authors"_s || field.key == u"artists"_s;
    // A series lacks a credit when one of its volumes does (`weighCredits`), and not merely
    // because it names nobody itself: volumes that all name their own owe it nothing.
    if (credit && sheet.level == Level::Work)
        return lackingFor(sheet, field.key) > 0;
    // A volume is credited by its series when it names nobody.
    if (credit && isCredited(sheet)) {
        const QStringList &said =
            field.key == u"authors"_s ? sheet.credits.fallback.authors
                                      : sheet.credits.fallback.artists;
        return said.isEmpty();
    }
    // An edition is given what it leaves empty by its series' `work.json`.
    return field.fromWork.isNull();
}

int lacking(const Sheet &sheet)
{
    return int(std::ranges::count_if(sheet.fields, [&sheet](const Field &field) {
        // Counted on the series, once: forty-one volumes drawn by nobody are one value to type.
        if (isCredited(sheet) && (field.key == u"authors"_s || field.key == u"artists"_s))
            return false;
        return lacks(sheet, field);
    }));
}

int lackingFor(const Sheet &series, const QString &key)
{
    if (key == u"authors"_s)
        return series.credits.authorsLacking;
    return key == u"artists"_s ? series.credits.artistsLacking : 0;
}

bool needsReview(const Sheet &sheet)
{
    // A role read from the shape is a guess like any other — an archive nobody will read
    // included, which is not announced. A role chosen is somebody's, and is not asked again.
    if (!sheet.trouble.isEmpty() || sheet.roleOrigin == Shape::Origin::Guessed)
        return true;
    return std::ranges::any_of(sheet.fields, [&sheet](const Field &field) {
        // A value read off a name is shown and not asked about: « Deviné » on the number of
        // every volume of « Tome 01 » to « Tome 76 » was seventy-six questions with one answer.
        return !field.doubt.isEmpty() || lacks(sheet, field);
    });
}

bool blocks(const Sheet &sheet)
{
    // A folder whose sidecar is not a JSON object is refused by the server, which names it;
    // letting the series through would only move the refusal to after the upload.
    if (isContainer(sheet.level) && !sheet.trouble.isEmpty())
        return true;
    // The announcement answers 400 to an inclusion that names nothing, carries a range or
    // closes a loop: sent anyway, nothing of the whole card would arrive.
    if (const std::optional<Orders::Universe> universe = ordersOf(sheet);
        universe && !Orders::refused(*universe).isEmpty())
        return true;
    return std::ranges::any_of(sheet.fields, [&sheet](const Field &field) {
        // A volume's credits hold back its series, which is first in the walk and where they
        // are put right, rather than each of forty volumes.
        if (isCredited(sheet) && (field.key == u"authors"_s || field.key == u"artists"_s))
            return false;
        return lacks(sheet, field) || (field.key == u"number"_s && !field.doubt.isEmpty());
    });
}

namespace {

/// What a file's sheet comes to once its corrections are in.
Found foundIn(const Sheet &sheet)
{
    Found found;
    found.kind = sheet.level == Manifest::Level::Chapter ? u"CHAPTER"_s : u"VOLUME"_s;
    for (const Field &field : sheet.fields) {
        if (field.origin == Origin::Missing)
            continue;
        if (field.key == u"number"_s && field.value.isDouble()) {
            found.number = field.value.toDouble();
            found.guessed = field.origin == Origin::Guessed;
        }
        else if (field.key == u"title"_s && field.value.isString())
            found.title = field.value.toString();
    }
    found.series = sheet.contents.series;
    found.pages = sheet.contents.pages;
    found.chapters = sheet.contents.chapters;
    return found;
}

/// The sidecar a container leaves with, `before` being the one at its path on disk (null for
/// none); nothing when it has none and adds nothing.
std::optional<Manifest::Sidecar> sidecarFor(const Sheet &sheet, const Manifest::Sidecar *before,
                                            const Raised &raised)
{
    // The scan reads a role from a sidecar and never from a shape: one guessed or
    // chosen exists only once its file does, so it is written even saying nothing.
    const bool confirms = sheet.roleOrigin == Shape::Origin::Guessed ||
                          sheet.roleOrigin == Shape::Origin::Chosen;
    const QJsonObject credits = raised.into.value(sheet.at);
    if (!sheet.trouble.isEmpty() ||
        (before && !confirms && credits.isEmpty()
         && !std::ranges::any_of(sheet.fields, adds))) {
        if (before)
            return *before;
        return std::nullopt;
    }
    QJsonObject object = before ? QJsonDocument::fromJson(before->json).object() : started();
    const QJsonObject untouched = object;
    complete(object, sheet);
    for (auto one = credits.begin(); one != credits.end(); ++one)
        object.insert(one.key(), one.value());
    // Reserialising a sidecar that gained nothing would change the bytes the server
    // compares for no reason.
    if (before && object == untouched)
        return *before;
    return Manifest::Sidecar{sheet.sidecar, compact(object)};
}

/// What an archive that travels is rewritten into; nothing when it asks for nothing.
std::optional<Rewrite> rewriteOf(const Sheet &sheet, const Raised &raised)
{
    Rewrite rewrite;
    const bool added = std::ranges::any_of(sheet.fields, adds);
    if (sheet.role == Shape::Role::Book) {
        // A book chosen has no `oneshot.json` yet, and without one the scan reads no book.
        // Its fields go there; its `entry.json` is not the book's and leaves as it was.
        // Its Leaf file is its `oneshot.json`: one said and complete asks for nothing but
        // its ComicInfo's leaving.
        const bool chosen = sheet.roleOrigin != Shape::Origin::Said;
        if (!chosen && !added && !sheet.comicInfo.carried)
            return std::nullopt;
        if (chosen || added) {
            QJsonObject book = chosen ? started() : sheet.oneShot;
            complete(book, sheet);
            rewrite.oneShotJson = compact(book);
        }
        rewrite.entryJson = compact(sheet.own.isEmpty() ? started() : sheet.own);
    } else {
        const bool changed = added || !sheet.hasOwn;
        if (!changed && !sheet.comicInfo.carried)
            return std::nullopt;
        QJsonObject object = sheet.own.isEmpty() ? started() : sheet.own;
        if (changed) {
            complete(object, sheet);
            // Said by its series now, which most of its volumes agree on.
            for (const QString &key : raised.inherits.value(sheet.at))
                object.remove(key);
        }
        rewrite.entryJson = compact(object);
    }
    // Read, empty of anything worth taking or unreadable, it goes all the same: removing
    // it needs no reading of it, and after the import Leaf reads its own files alone.
    if (sheet.comicInfo.carried)
        rewrite.remove << ComicInfoXml;
    return rewrite;
}

} // namespace

Composed compose(const QList<Sheet> &sheets, const QList<Manifest::Sidecar> &said)
{
    Composed composed;
    const Raised raised = raise(sheets);
    QSet<QString> met;
    const auto original = [&said](const QString &path) -> const Manifest::Sidecar * {
        for (const Manifest::Sidecar &one : said) {
            if (one.path == path)
                return &one;
        }
        return nullptr;
    };

    for (const Sheet &sheet : sheets) {
        if (isContainer(sheet.level)) {
            const Manifest::Sidecar *before = original(sheet.sidecar);
            if (before)
                met.insert(before->path);
            if (const std::optional<Manifest::Sidecar> leaving = sidecarFor(sheet, before, raised))
                composed.sidecars << *leaving;
            continue;
        }

        // `visit_books_of` puts it in `disregarded`: sent, it would sit on the disk read by
        // nobody. It stays on screen and leaves out of the announcement.
        if (sheet.role == Shape::Role::Unread) {
            composed.leftOut.insert(sheet.at);
            continue;
        }
        if (!sheet.trouble.isEmpty())
            continue;
        composed.found.insert(sheet.at, foundIn(sheet));
        if (const std::optional<Rewrite> rewrite = rewriteOf(sheet, raised))
            composed.rewrites.insert(sheet.at, *rewrite);
    }

    // What the card's sheets do not name is not ours to judge: it leaves as it is.
    for (const Manifest::Sidecar &one : said) {
        if (!met.contains(one.path))
            composed.sidecars << one;
    }
    return composed;
}

} // namespace Facts
