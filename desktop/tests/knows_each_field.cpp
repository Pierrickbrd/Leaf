// Where every value of an import comes from.
//
// Folders and archives are written here, byte by byte through `Zip::volume`, because what is
// worth covering is how two sources meet — an `entry.json` and a `ComicInfo.xml` in one
// archive, a `work.json` above an `edition.json`, two volumes that contradict each other —
// and a checked-in fixture says one of those at a time.

#include "Facts.h"
#include "Manifest.h"
#include "Orders.h"
#include "Shape.h"
#include "Words.h"
#include "Zip.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

void write(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
}

/// A ComicInfo.xml holding the tags given, in that order.
QByteArray comicInfo(const QList<std::pair<QByteArray, QByteArray>> &tags)
{
    QByteArray xml = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<ComicInfo>";
    for (const auto &[tag, text] : tags)
        xml += '<' + tag + '>' + text + "</" + tag + '>';
    return xml + "</ComicInfo>";
}

/// The sheets of the folder at `path`, read the way the import reads a dropped card: its
/// tree from `found`, what its files say from `of`.
QList<Facts::Sheet> sheetsOf(const QString &path)
{
    const QList<Manifest::Node> cards = Manifest::found(path);
    if (cards.size() != 1)
        return {};
    return Facts::read(cards.first(), Manifest::of(path, false));
}

// Not into a list that dies at the end of the statement: four tests kept such a pointer, read
// freed memory, and passed until `Field` grew a member and one of them crashed.
const Facts::Sheet *sheetAt(QList<Facts::Sheet> &&sheets, const QString &at) = delete;

const Facts::Sheet *sheetAt(const QList<Facts::Sheet> &sheets, const QString &at)
{
    for (const Facts::Sheet &sheet : sheets) {
        if (sheet.at == at)
            return &sheet;
    }
    return nullptr;
}

const Facts::Field *fieldOf(const Facts::Sheet *sheet, const QString &key)
{
    if (!sheet)
        return nullptr;
    for (const Facts::Field &field : sheet->fields) {
        if (field.key == key)
            return &field;
    }
    return nullptr;
}

/// What the import announces for the folder at `path`: its sheets, composed against the
/// sidecars the walk found.
Facts::Composed composedOf(const QString &path)
{
    return Facts::compose(sheetsOf(path), Manifest::of(path, false).sidecars);
}

// Nor into one that dies at the end of the statement, for the same reason: on Qt 6.4 a
// work.json read this way came back without the drawer it was composed with.
const Manifest::Sidecar *sidecarAt(Facts::Composed &&composed, const QString &path) = delete;

const Manifest::Sidecar *sidecarAt(const Facts::Composed &composed, const QString &path)
{
    for (const Manifest::Sidecar &one : composed.sidecars) {
        if (one.path == path)
            return &one;
    }
    return nullptr;
}

QJsonObject objectOf(const QByteArray &bytes)
{
    return QJsonDocument::fromJson(bytes).object();
}

/// The sheets of the folder at `path` with the roles given, on the card `found` makes of it.
QList<Facts::Sheet> sheetsOf(const QString &path, const Shape::Roles &roles)
{
    const QList<Manifest::Node> cards = Manifest::found(path);
    if (cards.size() != 1)
        return {};
    return Facts::read(cards.first(), Manifest::of(path, false), roles);
}

/// The sheets of the folder at `path` read the way the import reads a drop now: its shape
/// first, then the one card it makes, with the roles the shape gave it.
QList<Facts::Sheet> shapedSheetsOf(const QString &path, Shape::Roles *roles = nullptr)
{
    const Shape::Drop drop = Shape::read(path);
    const QStringList cards = Shape::cards(drop);
    if (cards != QStringList{QString()})
        return {};
    Shape::Roles given = Shape::rolesOf(drop, {});
    if (roles)
        *roles = given;
    return Facts::read(Shape::cardNode(drop, {}), Manifest::of(path, false), given);
}

const QString OneShotJson = u"oneshot.json"_s;

/// The roles the scan's own rule gives a card that declares or holds what it is: each folder
/// what its level says, each archive a volume — all Evident, nothing guessed, nothing chosen.
void evidentRoles(const Manifest::Node &node, Shape::Roles &into)
{
    const Shape::Role role = node.level == Manifest::Level::Universe ? Shape::Role::Universe
                             : node.level == Manifest::Level::Edition ? Shape::Role::Edition
                                                                      : Shape::Role::Work;
    into.insert(node.at, {role, Shape::Origin::Evident});
    for (const Manifest::Entry &file : node.files)
        into.insert(file.path, {Shape::Role::Volume, Shape::Origin::Evident});
    for (const Manifest::Node &child : node.children)
        evidentRoles(child, into);
}

/// Two readings of one folder that should be the same, compared on everything a sheet holds.
void sameSheets(const QList<Facts::Sheet> &one, const QList<Facts::Sheet> &other)
{
    QCOMPARE(one.size(), other.size());
    for (qsizetype i = 0; i < one.size(); ++i) {
        const Facts::Sheet &a = one.at(i);
        const Facts::Sheet &b = other.at(i);
        QCOMPARE(a.level, b.level);
        QCOMPARE(a.at, b.at);
        QCOMPARE(a.name, b.name);
        QCOMPARE(a.sidecar, b.sidecar);
        QCOMPARE(a.trouble, b.trouble);
        QCOMPARE(a.own, b.own);
        QCOMPARE(a.role, b.role);
        QCOMPARE(a.roleOrigin, b.roleOrigin);
        QCOMPARE(a.comicInfo.carried, b.comicInfo.carried);
        QCOMPARE(a.comicInfo.read, b.comicInfo.read);
        QCOMPARE(a.comicInfo.unreadable, b.comicInfo.unreadable);
        QCOMPARE(a.oneShot, b.oneShot);
        QCOMPARE(a.oneShotUnreadable, b.oneShotUnreadable);
        QCOMPARE(a.credits.credited.authors, b.credits.credited.authors);
        QCOMPARE(a.credits.credited.artists, b.credits.credited.artists);
        QCOMPARE(a.fields.size(), b.fields.size());
        for (qsizetype f = 0; f < a.fields.size(); ++f) {
            QCOMPARE(a.fields.at(f).key, b.fields.at(f).key);
            QCOMPARE(a.fields.at(f).value, b.fields.at(f).value);
            QCOMPARE(a.fields.at(f).origin, b.fields.at(f).origin);
            QCOMPARE(a.fields.at(f).weight, b.fields.at(f).weight);
            QCOMPARE(a.fields.at(f).from, b.fields.at(f).from);
            QCOMPARE(a.fields.at(f).doubt, b.fields.at(f).doubt);
        }
    }
}

void sameComposed(const Facts::Composed &one, const Facts::Composed &other)
{
    QCOMPARE(one.sidecars.size(), other.sidecars.size());
    for (qsizetype i = 0; i < one.sidecars.size(); ++i) {
        QCOMPARE(one.sidecars.at(i).path, other.sidecars.at(i).path);
        QCOMPARE(one.sidecars.at(i).json, other.sidecars.at(i).json);
    }
    QCOMPARE(one.rewrites.keys(), other.rewrites.keys());
    for (auto it = one.rewrites.cbegin(); it != one.rewrites.cend(); ++it) {
        QCOMPARE(it->entryJson, other.rewrites.value(it.key()).entryJson);
        QCOMPARE(it->oneShotJson, other.rewrites.value(it.key()).oneShotJson);
        QCOMPARE(it->remove, other.rewrites.value(it.key()).remove);
    }
    QCOMPARE(one.leftOut, other.leftOut);
}

} // namespace

class KnowsEachField : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_root;

    QString folder(const QString &name) const { return m_root.filePath(name); }

private slots:
    /// A folder that already speaks Leaf has nothing to ask: stopping it on the checking
    /// step would make the step a wall every well-kept series has to climb for nothing.
    void a_complete_leaf_folder_says_everything_and_asks_nothing()
    {
        const QString at = folder(u"Blame"_s);
        write(at + u"/work.json"_s,
              R"({"title":"Blame!","readingDirection":"RIGHT_TO_LEFT","authors":["Tsutomu Nihei"],"artists":["Tsutomu Nihei"]})");
        write(at + u"/Tome 1.cbz"_s, Zip::volume(R"({"number":1})"));
        write(at + u"/Tome 2.cbz"_s, Zip::volume(R"({"number":2})"));

        const QList<Facts::Sheet> sheets = sheetsOf(at);
        QCOMPARE(sheets.size(), 3);
        const Facts::Sheet *series = sheetAt(sheets, u""_s);
        QVERIFY(series);
        QCOMPARE(series->level, Manifest::Level::Work);
        QCOMPARE(series->sidecar, u"work.json"_s);

        const Facts::Field *title = fieldOf(series, u"title"_s);
        QVERIFY(title);
        QCOMPARE(title->origin, Facts::Origin::Said);
        QCOMPARE(title->weight, Facts::Weight::Required);
        QCOMPARE(title->value.toString(), u"Blame!"_s);
        QCOMPARE(title->from, u"work.json"_s);
        const Facts::Field *direction = fieldOf(series, u"readingDirection"_s);
        QVERIFY(direction);
        QCOMPARE(direction->origin, Facts::Origin::Said);
        QCOMPARE(direction->weight, Facts::Weight::Required);

        for (const QString &file : {u"Tome 1.cbz"_s, u"Tome 2.cbz"_s}) {
            const Facts::Sheet *volume = sheetAt(sheets, file);
            QVERIFY2(volume, qPrintable(file));
            QCOMPARE(volume->level, Manifest::Level::Volume);
            QCOMPARE(volume->sidecar, file);
            const Facts::Field *number = fieldOf(volume, u"number"_s);
            QVERIFY(number);
            QCOMPARE(number->origin, Facts::Origin::Said);
            QCOMPARE(number->from, u"entry.json"_s);
        }
        for (const Facts::Sheet &sheet : sheets)
            QVERIFY2(!Facts::needsReview(sheet), qPrintable(sheet.at));
    }

    /// A Komga library is all ComicInfo and no sidecar. What every volume copies from its
    /// series is the series' — and the one thing ComicInfo never says reliably, the reading
    /// direction, is the one thing that stops it, because a manga read backwards is not
    /// noticed until page forty. The writer is not the series': it is the first volume's own,
    /// and copying it up froze a series on one name.
    void a_komga_series_is_taken_from_its_first_volume_and_its_author_goes_to_the_volume()
    {
        const QString at = folder(u"Komga"_s);
        write(at + u"/v01.cbz"_s,
              Zip::volume({}, comicInfo({{"Series", "Blame!"}, {"Number", "1"},
                                         {"Writer", "Tsutomu Nihei"},
                                         {"Penciller", "Tsutomu Nihei"}})));
        write(at + u"/v02.cbz"_s,
              Zip::volume({}, comicInfo({{"Series", "Blame!"}, {"Number", "2"},
                                         {"Writer", "Tsutomu Nihei"},
                                         {"Penciller", "Tsutomu Nihei"}})));

        QList<Facts::Sheet> sheets = sheetsOf(at);
        const Facts::Sheet *series = sheetAt(sheets, u""_s);
        QVERIFY(series);
        const Facts::Field *title = fieldOf(series, u"title"_s);
        QVERIFY(title);
        QCOMPARE(title->origin, Facts::Origin::Taken);
        QCOMPARE(title->from, u"ComicInfo.xml"_s);
        QCOMPARE(title->value.toString(), u"Blame!"_s);
        const Facts::Field *authors = fieldOf(series, u"authors"_s);
        QVERIFY(authors);
        QCOMPARE(authors->origin, Facts::Origin::Missing);
        QVERIFY(authors->value.isNull());
        QCOMPARE(series->credits.credited.authors, QStringList{u"Tsutomu Nihei"_s});
        const Facts::Field *writer = fieldOf(sheetAt(sheets, u"v01.cbz"_s), u"authors"_s);
        QVERIFY(writer);
        QCOMPARE(writer->origin, Facts::Origin::Taken);
        QCOMPARE(writer->value.toArray(), QJsonArray{u"Tsutomu Nihei"_s});
        const Facts::Field *direction = fieldOf(series, u"readingDirection"_s);
        QVERIFY(direction);
        QCOMPARE(direction->origin, Facts::Origin::Missing);
        QCOMPARE(direction->weight, Facts::Weight::Required);
        QVERIFY(direction->value.isNull());

        const Facts::Sheet *first = sheetAt(sheets, u"v01.cbz"_s);
        QVERIFY(first);
        QVERIFY(first->comicInfo.carried);
        QVERIFY(first->comicInfo.read);
        const Facts::Field *number = fieldOf(first, u"number"_s);
        QVERIFY(number);
        QCOMPARE(number->origin, Facts::Origin::Taken);
        QCOMPARE(number->value.toDouble(), 1.0);

        // The direction alone: once it is said, nothing else holds the series back.
        QVERIFY(Facts::needsReview(*series));
        QVERIFY(Facts::edit(sheets, u""_s, u"readingDirection"_s, u"RIGHT_TO_LEFT"_s));
        QVERIFY(!Facts::needsReview(*sheetAt(sheets, u""_s)));
    }

    /// `entry.json` is authoritative and ComicInfo only fills its holes — field by field,
    /// not file by file: an archive whose `entry.json` gives a number and no title would
    /// otherwise lose the title its ComicInfo carried.
    void what_entry_json_says_wins_field_by_field_over_comic_info()
    {
        const QString at = folder(u"Both"_s);
        write(at + u"/Tome 9.cbz"_s,
              Zip::volume(R"({"number":3})",
                          comicInfo({{"Number", "9"}, {"Title", "Le ciel"}})));

        const QList<Facts::Sheet> sheets = sheetsOf(at);
        const Facts::Sheet *volume = sheetAt(sheets, u"Tome 9.cbz"_s);
        const Facts::Field *number = fieldOf(volume, u"number"_s);
        QVERIFY(number);
        QCOMPARE(number->value.toDouble(), 3.0);
        QCOMPARE(number->origin, Facts::Origin::Said);
        const Facts::Field *title = fieldOf(volume, u"title"_s);
        QVERIFY(title);
        QCOMPARE(title->value.toString(), u"Le ciel"_s);
        QCOMPARE(title->origin, Facts::Origin::Taken);
        QCOMPARE(title->from, u"ComicInfo.xml"_s);
    }

    /// What the announcement carries for a file is what it will be once installed: a number
    /// corrected from 3 to 4 in the checking step is 4 for the server, which matches on it.
    void what_a_file_declares_is_what_it_will_carry()
    {
        const QString at = folder(u"Declared"_s);
        write(at + u"/Tome 3.cbz"_s,
              Zip::volume(R"({"number":3,"title":"Le ciel","work":"Declared","chapters":[{"number":1},{"number":2.5},{"raw":"x"}]})"));

        QList<Facts::Sheet> sheets = sheetsOf(at);
        QVERIFY(Facts::edit(sheets, u"Tome 3.cbz"_s, u"number"_s, 4));
        const Facts::Composed composed = Facts::compose(sheets, Manifest::of(at, false).sidecars);

        const Facts::Found found = composed.found.value(u"Tome 3.cbz"_s);
        QCOMPARE(found.number, std::optional<double>(4));
        QCOMPARE(found.kind, u"VOLUME"_s);
        QCOMPARE(found.title, u"Le ciel"_s);
        QCOMPARE(found.series, u"Declared"_s);
        QCOMPARE(found.chapters, QList<double>({1, 2.5}));
        QCOMPARE(found.pages, 1);
    }

    /// `Series` of a ComicInfo is the series' title when the archive's own `entry.json` says
    /// none: a Komga library names its series only there.
    void the_series_a_comic_info_names_travels_with_the_file()
    {
        const QString at = folder(u"Komga"_s);
        write(at + u"/Tome 1.cbz"_s,
              Zip::volume({}, comicInfo({{"Series", "Blue Period"}, {"Number", "1"}})));

        const Facts::Composed composed = composedOf(at);
        QCOMPARE(composed.found.value(u"Tome 1.cbz"_s).series, u"Blue Period"_s);
    }

    /// A file that says nothing still has a name, and the name gives a number — offered as a
    /// guess to confirm, never as a fact, and never as a reason to refuse the series.
    void a_bare_archive_gets_its_number_from_its_name_as_a_guess()
    {
        const QString at = folder(u"Bare"_s);
        write(at + u"/scan_02.cbz"_s, Zip::volume({}));

        const QList<Facts::Sheet> sheets = sheetsOf(at);
        const Facts::Sheet *volume = sheetAt(sheets, u"scan_02.cbz"_s);
        QVERIFY(volume);
        const Facts::Field *number = fieldOf(volume, u"number"_s);
        QVERIFY(number);
        QCOMPARE(number->value.toDouble(), 2.0);
        QCOMPARE(number->origin, Facts::Origin::Guessed);
        QCOMPARE(number->from, u"scan_02.cbz"_s);
        QVERIFY(!volume->comicInfo.carried);
        QVERIFY(Facts::needsReview(*volume));
        QVERIFY(!Facts::blocks(*volume));

        // The series' name is the folder's, guessed the same way.
        const Facts::Field *title = fieldOf(sheetAt(sheets, u""_s), u"title"_s);
        QVERIFY(title);
        QCOMPARE(title->value.toString(), u"Bare"_s);
        QCOMPARE(title->origin, Facts::Origin::Guessed);
        QCOMPARE(title->from, u"Bare"_s);
    }

    /// ComicInfo has no way to say a file is a chapter, so it pins the volume number on it:
    /// « Chapitre 686.5 » declares `<Number>75</Number>` in the real library. Taken, it
    /// would number the chapter 75 and collide with the chapter actually called 75.
    void a_chapter_file_ignores_the_volume_number_comic_info_pins_on_it()
    {
        const QString at = folder(u"Chapters"_s);
        write(at + u"/Chapitre 686.5.cbz"_s,
              Zip::volume({}, comicInfo({{"Number", "75"}, {"Title", "Le retour"}})));

        const QList<Facts::Sheet> sheets = sheetsOf(at);
        const Facts::Sheet *chapter = sheetAt(sheets, u"Chapitre 686.5.cbz"_s);
        QVERIFY(chapter);
        QCOMPARE(chapter->level, Manifest::Level::Chapter);
        const Facts::Field *number = fieldOf(chapter, u"number"_s);
        QVERIFY(number);
        QCOMPARE(number->value.toDouble(), 686.5);
        QCOMPARE(number->origin, Facts::Origin::Guessed);
        // The rest of what ComicInfo says still holds.
        QCOMPARE(fieldOf(chapter, u"title"_s)->origin, Facts::Origin::Taken);
    }

    /// Two files of one number in one edition: the server would keep one and the reader
    /// would never know which. Both lines say it, since either may be the wrong one.
    void two_volumes_of_one_number_block_their_series_and_say_so_on_both()
    {
        const QString at = folder(u"Twice"_s);
        write(at + u"/Tome 4.cbz"_s, Zip::volume(R"({"number":4})"));
        write(at + u"/Tome 4 - bis.cbz"_s, Zip::volume({}));

        const QList<Facts::Sheet> sheets = sheetsOf(at);
        for (const QString &file : {u"Tome 4.cbz"_s, u"Tome 4 - bis.cbz"_s}) {
            const Facts::Sheet *volume = sheetAt(sheets, file);
            QVERIFY2(volume, qPrintable(file));
            QCOMPARE(fieldOf(volume, u"number"_s)->doubt, Words::sameNumberTwice(4));
            QVERIFY(Facts::blocks(*volume));
            QVERIFY(Facts::needsReview(*volume));
        }
    }

    /// The format copies the series into every volume, and the copies drift. The first one
    /// is kept — something has to be. The field used to say the others disagreed, which was
    /// a question about where a value was read: it is shown, and corrected if wrong.
    void volumes_that_disagree_keep_the_first_value()
    {
        const QString at = folder(u"Disagree"_s);
        write(at + u"/Tome 1.cbz"_s,
              Zip::volume({}, comicInfo({{"Series", "Naruto"}, {"Number", "1"},
                                         {"Publisher", "Pika"}, {"Writer", "Kishimoto"},
                                         {"Penciller", "Kishimoto"},
                                         {"Manga", "YesAndRightToLeft"}})));
        write(at + u"/Tome 2.cbz"_s,
              Zip::volume({}, comicInfo({{"Series", "Naruto"}, {"Number", "2"},
                                         {"Publisher", "Kana"}, {"Writer", "Kishimoto"},
                                         {"Penciller", "Kishimoto"},
                                         {"Manga", "YesAndRightToLeft"}})));

        const QList<Facts::Sheet> sheets = sheetsOf(at);
        const Facts::Sheet *series = sheetAt(sheets, u""_s);
        const Facts::Field *publisher = fieldOf(series, u"publisher"_s);
        QVERIFY(publisher);
        QCOMPARE(publisher->value.toString(), u"Pika"_s);
        QCOMPARE(publisher->origin, Facts::Origin::Taken);
        QVERIFY(publisher->doubt.isEmpty());
        QVERIFY(!Facts::blocks(*series));
    }

    /// An archive that does not open is still offered — the spec says so — but with a
    /// number from its name and nothing else, and the sheet says the reading is weaker. No
    /// field of it is editable: there is no `entry.json` to write into a file nobody can open.
    void an_unreadable_archive_is_offered_with_a_weaker_reading_said()
    {
        const QString at = folder(u"Broken"_s);
        write(at + u"/Tome 3.cbz"_s, "ceci est du texte, pas une archive");

        QList<Facts::Sheet> sheets = sheetsOf(at);
        const Facts::Sheet *volume = sheetAt(sheets, u"Tome 3.cbz"_s);
        QVERIFY(volume);
        QVERIFY(!volume->trouble.isEmpty());
        QCOMPARE(volume->fields.size(), 1);
        const Facts::Field *number = fieldOf(volume, u"number"_s);
        QVERIFY(number);
        QCOMPARE(number->value.toDouble(), 3.0);
        QCOMPARE(number->origin, Facts::Origin::Guessed);
        QVERIFY(Facts::needsReview(*volume));
        QVERIFY(!Facts::blocks(*volume));
        QVERIFY(!Facts::edit(sheets, u"Tome 3.cbz"_s, u"number"_s, 4));
    }

    /// A work with declared editions keeps its edition fields in each
    /// `edition.json`, and what `work.json` already says is a default the edition inherits —
    /// said, not missing. Asked again, the reader would type « Glénat » for every edition.
    void an_edition_reads_what_its_work_already_says_as_said()
    {
        const QString at = folder(u"Dragon Ball"_s);
        write(at + u"/work.json"_s,
              R"({"title":"Dragon Ball","publisher":"Glénat","readingDirection":"RIGHT_TO_LEFT","authors":["Toriyama"],"artists":["Toriyama"]})");
        write(at + u"/Perfect/edition.json"_s, R"({"name":"Perfect"})");
        write(at + u"/Perfect/Tome 1.cbz"_s, Zip::volume(R"({"number":1})"));

        const QList<Facts::Sheet> sheets = sheetsOf(at);
        const Facts::Sheet *edition = sheetAt(sheets, u"Perfect"_s);
        QVERIFY(edition);
        QCOMPARE(edition->level, Manifest::Level::Edition);
        QCOMPARE(edition->sidecar, u"Perfect/edition.json"_s);
        QCOMPARE(fieldOf(edition, u"name"_s)->origin, Facts::Origin::Said);
        QCOMPARE(fieldOf(edition, u"name"_s)->from, u"edition.json"_s);
        const Facts::Field *publisher = fieldOf(edition, u"publisher"_s);
        QVERIFY(publisher);
        QCOMPARE(publisher->value.toString(), u"Glénat"_s);
        QCOMPARE(publisher->origin, Facts::Origin::Said);
        QCOMPARE(publisher->from, u"work.json"_s);
        QVERIFY(!Facts::needsReview(*edition));

        // The series carries no edition field of its own: they live in each edition.
        const Facts::Sheet *series = sheetAt(sheets, u""_s);
        QVERIFY(series);
        QVERIFY(!fieldOf(series, u"publisher"_s));
        QVERIFY(!fieldOf(series, u"readingDirection"_s));
        QVERIFY(!Facts::needsReview(*series));
    }

    /// A universe and a series declared before any volume arrived: two sheets to show, and
    /// nothing to stop on when they say all they need to.
    void a_folder_of_nothing_but_declarations_has_sheets_and_no_volume()
    {
        const QString at = folder(u"Arran"_s);
        write(at + u"/universe.json"_s, R"({"name":"Terres d’Arran"})");
        write(at + u"/Elfes/work.json"_s,
              R"({"title":"Elfes","readingDirection":"LEFT_TO_RIGHT","authors":["Istin"],"artists":["Nico"]})");

        const QList<Facts::Sheet> sheets = sheetsOf(at);
        QCOMPARE(sheets.size(), 2);
        QCOMPARE(sheets.at(0).level, Manifest::Level::Universe);
        QCOMPARE(sheets.at(0).sidecar, u"universe.json"_s);
        QCOMPARE(sheets.at(1).level, Manifest::Level::Work);
        QCOMPARE(sheets.at(1).sidecar, u"Elfes/work.json"_s);
        for (const Facts::Sheet &sheet : sheets)
            QVERIFY2(!Facts::needsReview(sheet), qPrintable(sheet.at));
    }

    /// Correcting a guess is saying it, and a corrected number is no longer the other
    /// file's: the duplicate it was part of goes away on both lines at once.
    void editing_a_guess_makes_it_said_and_editing_a_number_clears_a_duplicate()
    {
        const QString at = folder(u"Fix"_s);
        write(at + u"/Tome 2.cbz"_s, Zip::volume({}));
        write(at + u"/Tome 2 - bis.cbz"_s, Zip::volume({}));

        QList<Facts::Sheet> sheets = sheetsOf(at);
        QVERIFY(!fieldOf(sheetAt(sheets, u"Tome 2.cbz"_s), u"number"_s)->doubt.isEmpty());

        QVERIFY(Facts::edit(sheets, u"Tome 2 - bis.cbz"_s, u"number"_s, 3));
        const Facts::Field *edited = fieldOf(sheetAt(sheets, u"Tome 2 - bis.cbz"_s), u"number"_s);
        QCOMPARE(edited->value.toDouble(), 3.0);
        QCOMPARE(edited->origin, Facts::Origin::Said);
        QVERIFY(edited->edited);
        QVERIFY(edited->doubt.isEmpty());
        const Facts::Field *other = fieldOf(sheetAt(sheets, u"Tome 2.cbz"_s), u"number"_s);
        QVERIFY(other->doubt.isEmpty());
        QCOMPARE(other->origin, Facts::Origin::Guessed);
        QVERIFY(!other->edited);

        // Nothing to edit is said, not done.
        QVERIFY(!Facts::edit(sheets, u"Tome 9.cbz"_s, u"number"_s, 9));
        QVERIFY(!Facts::edit(sheets, u"Tome 2.cbz"_s, u"colour"_s, true));
    }

    /// The series' name is the one series field nobody can import without: emptied, it is
    /// missing, and missing it holds the series back.
    void clearing_the_series_name_blocks_the_series()
    {
        const QString at = folder(u"Named"_s);
        write(at + u"/work.json"_s, R"({"title":"Akira","readingDirection":"RIGHT_TO_LEFT","authors":["Otomo"],"artists":["Otomo"]})");
        write(at + u"/Tome 1.cbz"_s, Zip::volume(R"({"number":1})"));

        QList<Facts::Sheet> sheets = sheetsOf(at);
        QVERIFY(!Facts::blocks(*sheetAt(sheets, u""_s)));
        QVERIFY(Facts::edit(sheets, u""_s, u"title"_s, u"  "_s));
        const Facts::Sheet *series = sheetAt(sheets, u""_s);
        const Facts::Field *title = fieldOf(series, u"title"_s);
        QCOMPARE(title->origin, Facts::Origin::Missing);
        QVERIFY(title->value.isNull());
        QVERIFY(title->edited);
        QVERIFY(Facts::blocks(*series));
        QVERIFY(Facts::needsReview(*series));

        // A facultative field emptied is missing too, and holds nothing back.
        QVERIFY(Facts::edit(sheets, u"Tome 1.cbz"_s, u"title"_s, u"Néo-Tokyo"_s));
        QVERIFY(Facts::edit(sheets, u"Tome 1.cbz"_s, u"title"_s, u""_s));
        const Facts::Sheet *volume = sheetAt(sheets, u"Tome 1.cbz"_s);
        QCOMPARE(fieldOf(volume, u"title"_s)->origin, Facts::Origin::Missing);
        QVERIFY(!Facts::needsReview(*volume));
    }

    /// A key present and empty says nothing, so it must not hide what ComicInfo says behind
    /// it: a `"title":""` left by an editor would otherwise keep a volume untitled.
    void an_empty_key_in_a_leaf_json_falls_through_to_comic_info()
    {
        const QString at = folder(u"Hollow"_s);
        write(at + u"/Tome 1.cbz"_s,
              Zip::volume(R"({"number":1,"title":""})", comicInfo({{"Title", "Le ciel"}})));

        const QList<Facts::Sheet> sheets = sheetsOf(at);
        const Facts::Field *title = fieldOf(sheetAt(sheets, u"Tome 1.cbz"_s), u"title"_s);
        QVERIFY(title);
        QCOMPARE(title->origin, Facts::Origin::Taken);
        QCOMPARE(title->value.toString(), u"Le ciel"_s);
    }

    /// A file called « Special » is a chapter when its own `entry.json` says so, as the
    /// server reads it: going by the name alone filed it as a volume.
    void a_chapter_declared_in_its_entry_json_is_a_chapter()
    {
        const QString at = folder(u"Declared"_s);
        write(at + u"/Special.cbz"_s, Zip::volume(R"({"type":"chapter","number":5})"));

        const QList<Facts::Sheet> sheets = sheetsOf(at);
        const Facts::Sheet *file = sheetAt(sheets, u"Special.cbz"_s);
        QVERIFY(file);
        QCOMPARE(file->level, Manifest::Level::Chapter);
        QCOMPARE(fieldOf(file, u"number"_s)->origin, Facts::Origin::Said);
    }

    /// An edition declared by name only still gets its publisher from the volumes below it.
    void an_edition_takes_a_value_from_comic_info()
    {
        const QString at = folder(u"Editions"_s);
        write(at + u"/work.json"_s, R"({"title":"Dragon Ball"})");
        write(at + u"/Perfect/edition.json"_s, R"({"name":"Perfect"})");
        write(at + u"/Perfect/Tome 1.cbz"_s,
              Zip::volume(R"({"number":1})", comicInfo({{"Publisher", "Glénat"}})));

        const QList<Facts::Sheet> sheets = sheetsOf(at);
        const Facts::Field *publisher = fieldOf(sheetAt(sheets, u"Perfect"_s), u"publisher"_s);
        QVERIFY(publisher);
        QCOMPARE(publisher->origin, Facts::Origin::Taken);
        QCOMPARE(publisher->value.toString(), u"Glénat"_s);
    }

    /// A sidecar nothing was added to is not ours to reformat: the server keeps a fingerprint
    /// of what it was sent, and a file re-serialised for no reason changes it.
    void a_sidecar_nothing_was_added_to_travels_as_its_own_bytes()
    {
        const QString at = folder(u"Intact"_s);
        const QByteArray bytes =
            "{ \"myField\" : \"x\",\n  \"title\" : \"Blame!\",  \"leaf\":1 }\n";
        write(at + u"/work.json"_s, bytes);
        write(at + u"/Tome 1.cbz"_s, Zip::volume(R"({"number":1})"));

        const Facts::Composed composed = composedOf(at);
        QCOMPARE(composed.sidecars.size(), 1);
        QCOMPARE(composed.sidecars.first().path, u"work.json"_s);
        QCOMPARE(composed.sidecars.first().json, bytes);
        QVERIFY(composed.rewrites.isEmpty());
    }

    /// A key the client does not know is somebody else's: dropping `myField` on completion
    /// would erase data the reader typed themselves.
    void a_completed_sidecar_keeps_every_key_it_did_not_understand()
    {
        const QString at = folder(u"Completed"_s);
        write(at + u"/work.json"_s, R"({"leaf":1,"title":"X","myField":"kept"})");
        write(at + u"/Tome 1.cbz"_s,
              Zip::volume(R"({"number":1})", comicInfo({{"Manga", "YesAndRightToLeft"}})));

        const Facts::Composed composed = composedOf(at);
        const Manifest::Sidecar *work = sidecarAt(composed, u"work.json"_s);
        QVERIFY(work);
        const QJsonObject object = objectOf(work->json);
        QCOMPARE(object.value(u"myField"_s).toString(), u"kept"_s);
        QCOMPARE(object.value(u"title"_s).toString(), u"X"_s);
        QCOMPARE(object.value(u"readingDirection"_s).toString(), u"RIGHT_TO_LEFT"_s);
    }

    /// The singular `author` is a key like any other: completing the sidecar leaves what was
    /// there and migrates nothing, and the writer a volume's ComicInfo names is that volume's,
    /// so the `work.json` that said nothing of authors still says nothing of them.
    void the_singular_author_key_is_left_alone()
    {
        const QString at = folder(u"Singular"_s);
        write(at + u"/work.json"_s, R"({"title":"X","genres":["Seinen"],"author":"Jarry"})");
        write(at + u"/Tome 1.cbz"_s,
              Zip::volume(R"({"number":1})", comicInfo({{"Writer", "Nihei"}})));

        const Facts::Composed composed = composedOf(at);
        const Manifest::Sidecar *work = sidecarAt(composed, u"work.json"_s);
        QVERIFY(work);
        const QJsonObject object = objectOf(work->json);
        QCOMPARE(object.value(u"author"_s).toString(), u"Jarry"_s);
        QVERIFY(!object.contains(u"authors"_s));
        // The series names Jarry, so the volume is credited with him and its ComicInfo
        // naming Nihei fills no hole: nothing is written into the volume.
        QVERIFY(!objectOf(composed.rewrites.value(u"Tome 1.cbz"_s).entryJson)
                     .contains(u"authors"_s));
    }

    /// A Komga series is only ComicInfo: it leaves with a `work.json` made of it — the writer
    /// its volumes agree on included — and each volume with the order to convert, or it would
    /// reach the library as it came.
    void a_komga_series_gets_a_work_json_with_its_writer_and_each_volume_a_rewrite()
    {
        const QString at = folder(u"KomgaOut"_s);
        for (const int number : {1, 2}) {
            write(at + u"/v0"_s + QString::number(number) + u".cbz"_s,
                  Zip::volume({}, comicInfo({{"Series", "Blame!"}, {"Number", QByteArray::number(number)},
                                             {"Writer", "Nihei"}, {"Genre", "Action, SF"},
                                             {"Publisher", "Glénat"}, {"LanguageISO", "fr"},
                                             {"Count", "10"}})));
        }

        const Facts::Composed composed = composedOf(at);
        const Manifest::Sidecar *work = sidecarAt(composed, u"work.json"_s);
        QVERIFY(work);
        const QJsonObject object = objectOf(work->json);
        QCOMPARE(object.value(u"leaf"_s).toInt(), 1);
        QCOMPARE(object.value(u"title"_s).toString(), u"Blame!"_s);
        QCOMPARE(object.value(u"authors"_s).toArray(), QJsonArray{u"Nihei"_s});
        QCOMPARE(object.value(u"genres"_s).toArray(), (QJsonArray{u"Action"_s, u"SF"_s}));
        QCOMPARE(object.value(u"publisher"_s).toString(), u"Glénat"_s);
        QCOMPARE(object.value(u"language"_s).toString(), u"fr"_s);
        QCOMPARE(object.value(u"volumeCount"_s).toInt(), 10);
        QVERIFY(!object.contains(u"readingDirection"_s));

        QCOMPARE(composed.rewrites.size(), 2);
        for (const QString &file : {u"v01.cbz"_s, u"v02.cbz"_s}) {
            QVERIFY2(composed.rewrites.contains(file), qPrintable(file));
            const Facts::Rewrite &rewrite = composed.rewrites.value(file);
            QCOMPARE(rewrite.remove, QStringList{u"ComicInfo.xml"_s});
            QCOMPARE(objectOf(rewrite.entryJson).value(u"number"_s).toDouble(),
                     file == u"v01.cbz"_s ? 1.0 : 2.0);
            // Said once, by the series both volumes agree with.
            QVERIFY(!objectOf(rewrite.entryJson).contains(u"authors"_s));
        }
    }

    /// Both files and nothing to fill: its `entry.json` stays what it was, and the ComicInfo
    /// goes all the same — Leaf reads none after the import.
    void an_archive_with_both_files_and_no_gap_only_loses_its_comicinfo()
    {
        const QString at = folder(u"NoGap"_s);
        write(at + u"/Tome 1.cbz"_s,
              Zip::volume(R"({"number":1,"title":"A"})",
                          comicInfo({{"Number", "1"}, {"Title", "B"}})));

        const Facts::Composed composed = composedOf(at);
        QCOMPARE(composed.rewrites.size(), 1);
        const Facts::Rewrite rewrite = composed.rewrites.value(u"Tome 1.cbz"_s);
        QCOMPARE(objectOf(rewrite.entryJson),
                 (QJsonObject{{u"number"_s, 1}, {u"title"_s, u"A"_s}}));
        QCOMPARE(rewrite.remove, QStringList{u"ComicInfo.xml"_s});
    }

    /// `entry.json` is the archive's own: filling its title must neither lose the keys it
    /// carried nor leave a second description behind.
    void an_archive_with_both_files_and_a_gap_is_rewritten_once_keeping_its_entry_json()
    {
        const QString at = folder(u"Gap"_s);
        write(at + u"/Tome 1.cbz"_s,
              Zip::volume(R"({"leaf":1,"number":1,"myKey":"k"})",
                          comicInfo({{"Number", "1"}, {"Title", "Le ciel"}})));

        const Facts::Composed composed = composedOf(at);
        QCOMPARE(composed.rewrites.size(), 1);
        const Facts::Rewrite rewrite = composed.rewrites.value(u"Tome 1.cbz"_s);
        const QJsonObject object = objectOf(rewrite.entryJson);
        QCOMPARE(object.value(u"myKey"_s).toString(), u"k"_s);
        QCOMPARE(object.value(u"number"_s).toDouble(), 1.0);
        QCOMPARE(object.value(u"title"_s).toString(), u"Le ciel"_s);
        QCOMPARE(rewrite.remove, QStringList{u"ComicInfo.xml"_s});
    }

    /// An archive whose `entry.json` credits nobody and whose ComicInfo names a writer: the
    /// writer, whom the server reads from the ComicInfo in that case, is kept by a Leaf file
    /// — the series', which its one volume is the majority of — and the ComicInfo leaves.
    /// Before a volume carried its credits, such an archive kept both files.
    void an_entry_json_crediting_nobody_beside_a_comic_info_writer_leaves_it_to_the_series()
    {
        const QString at = folder(u"Writer Behind"_s);
        write(at + u"/Tome 1.cbz"_s,
              Zip::volume(R"({"number":1,"title":"A"})",
                          comicInfo({{"Number", "1"}, {"Title", "B"}, {"Writer", "Arleston"}})));

        const Facts::Composed composed = composedOf(at);
        QCOMPARE(composed.rewrites.size(), 1);
        const Facts::Rewrite rewrite = composed.rewrites.value(u"Tome 1.cbz"_s);
        const QJsonObject object = objectOf(rewrite.entryJson);
        QVERIFY(!object.contains(u"authors"_s));
        QCOMPARE(object.value(u"title"_s).toString(), u"A"_s);
        QCOMPARE(objectOf(sidecarAt(composed, u"work.json"_s)->json).value(u"authors"_s).toArray(),
                 QJsonArray{u"Arleston"_s});
        QCOMPARE(rewrite.remove, QStringList{u"ComicInfo.xml"_s});
    }

    /// An archive nobody could open would be filed under `corrupt` by the server if it were
    /// asked to rewrite it.
    void an_unreadable_archive_asks_for_no_rewrite()
    {
        const QString at = folder(u"BrokenOut"_s);
        write(at + u"/Tome 3.cbz"_s, "ceci est du texte, pas une archive");

        QVERIFY(composedOf(at).rewrites.isEmpty());
    }

    /// A ComicInfo that could not be read (here, one declaring an entity) leaves all the same:
    /// removing it needs no reading of it. It was kept, read by nobody, in every archive.
    void a_comic_info_that_could_not_be_read_leaves_all_the_same()
    {
        const QString at = folder(u"Entities"_s);
        write(at + u"/Tome 3.cbz"_s,
              Zip::volume({}, "<?xml version=\"1.0\"?><!DOCTYPE x [<!ENTITY a \"b\">]>"
                              "<ComicInfo><Number>3</Number></ComicInfo>"));

        const Facts::Composed composed = composedOf(at);
        QCOMPARE(composed.rewrites.size(), 1);
        const Facts::Rewrite rewrite = composed.rewrites.value(u"Tome 3.cbz"_s);
        QCOMPARE(objectOf(rewrite.entryJson).value(u"number"_s).toDouble(), 3.0);
        QCOMPARE(rewrite.remove, QStringList{u"ComicInfo.xml"_s});
        const QList<Facts::Sheet> sheets = sheetsOf(at);
        QVERIFY(sheetAt(sheets, u"Tome 3.cbz"_s)->comicInfo.unreadable);
    }

    /// A ComicInfo that opens and holds nothing worth taking — its `<Pages>` alone — is not
    /// unreadable: nothing is taken from it, and nobody is told it could not be read.
    void a_comic_info_that_says_nothing_is_not_unreadable()
    {
        const QString at = folder(u"Pages"_s);
        write(at + u"/Tome 1.cbz"_s,
              Zip::volume(R"({"number":1})",
                          "<ComicInfo><Pages><Page Image=\"0\"/></Pages></ComicInfo>"));

        const QList<Facts::Sheet> sheets = sheetsOf(at);
        const Facts::Sheet *volume = sheetAt(sheets, u"Tome 1.cbz"_s);
        QVERIFY(volume);
        QVERIFY(volume->comicInfo.carried);
        QVERIFY(!volume->comicInfo.read);
        QVERIFY(!volume->comicInfo.unreadable);
    }

    /// `Work.json` is the file the server reads: completing it into a `work.json` beside it
    /// would leave two declarations, and the first one read wins.
    void a_declaration_in_the_wrong_case_is_completed_where_it_is()
    {
        const QString at = folder(u"Cased"_s);
        write(at + u"/Work.json"_s, R"({"title":"X"})");
        write(at + u"/Tome 1.cbz"_s,
              Zip::volume(R"({"number":1})", comicInfo({{"Manga", "RightToLeft"}})));

        const Facts::Composed composed = composedOf(at);
        QCOMPARE(composed.sidecars.size(), 1);
        QCOMPARE(composed.sidecars.first().path, u"Work.json"_s);
        QCOMPARE(objectOf(composed.sidecars.first().json).value(u"readingDirection"_s).toString(),
                 u"RIGHT_TO_LEFT"_s);
    }

    /// The server remembers a rewrite by its fingerprint: the same reading must give the same
    /// bytes, or every announcement of an unchanged folder would be taken for a new one.
    void composing_twice_gives_the_same_bytes()
    {
        const QString at = folder(u"Twin"_s);
        write(at + u"/Tome 1.cbz"_s,
              Zip::volume({}, comicInfo({{"Series", "S"}, {"Number", "1"}, {"Title", "T"},
                                         {"Publisher", "P"}, {"Writer", "W"}})));

        const Facts::Composed first = composedOf(at);
        const Facts::Composed second = composedOf(at);
        QCOMPARE(first.sidecars.size(), second.sidecars.size());
        QCOMPARE(first.sidecars.first().json, second.sidecars.first().json);
        QCOMPARE(first.rewrites.value(u"Tome 1.cbz"_s).entryJson,
                 second.rewrites.value(u"Tome 1.cbz"_s).entryJson);
    }

    /// A correction typed on the screen replaces what the sidecar said, and an emptied field
    /// leaves the file: keeping the old value would undo the correction on arrival.
    void an_edited_value_wins_over_the_sidecar_it_came_from()
    {
        const QString at = folder(u"Edited"_s);
        write(at + u"/work.json"_s, R"({"title":"A","publisher":"Pika"})");
        write(at + u"/Tome 1.cbz"_s, Zip::volume(R"({"number":1})"));

        QList<Facts::Sheet> sheets = sheetsOf(at);
        QVERIFY(Facts::edit(sheets, u""_s, u"title"_s, u"B"_s));
        QVERIFY(Facts::edit(sheets, u""_s, u"publisher"_s, u""_s));
        const Facts::Composed composed =
            Facts::compose(sheets, Manifest::of(at, false).sidecars);
        const QJsonObject object = objectOf(sidecarAt(composed, u"work.json"_s)->json);
        QCOMPARE(object.value(u"title"_s).toString(), u"B"_s);
        QVERIFY(!object.contains(u"publisher"_s));
    }

    /// The value a field already holds, given again, is no correction: marked edited, it was
    /// a sheet re-announced and an archive rewritten for what it already said.
    void editing_a_field_to_what_it_holds_changes_nothing()
    {
        const QString at = folder(u"Same"_s);
        write(at + u"/work.json"_s, R"({"title":"Akira","readingDirection":"RIGHT_TO_LEFT","authors":["Otomo"],"artists":["Otomo"]})");
        write(at + u"/Tome 1.cbz"_s, Zip::volume(R"({"number":1})"));

        QList<Facts::Sheet> sheets = sheetsOf(at);
        QVERIFY(!Facts::edit(sheets, u""_s, u"title"_s, u"Akira"_s));
        QVERIFY(!fieldOf(sheetAt(sheets, u""_s), u"title"_s)->edited);
        QVERIFY(!Facts::edit(sheets, u"Tome 1.cbz"_s, u"number"_s, 1));
        QVERIFY(!fieldOf(sheetAt(sheets, u"Tome 1.cbz"_s), u"number"_s)->edited);
        // Nothing said stays nothing said.
        QVERIFY(!Facts::edit(sheets, u"Tome 1.cbz"_s, u"title"_s, u""_s));
        QVERIFY(!fieldOf(sheetAt(sheets, u"Tome 1.cbz"_s), u"title"_s)->edited);
        QVERIFY(Facts::edit(sheets, u""_s, u"title"_s, u"Akira!"_s));
    }

    /// A declaration that is not a JSON object is the server's to refuse, by name: sent as
    /// it is, never recomposed into something it was not.
    void a_container_sidecar_that_is_not_an_object_is_sent_as_it_is()
    {
        const QString at = folder(u"Listed"_s);
        write(at + u"/work.json"_s, "[1, 2]");
        write(at + u"/Tome 1.cbz"_s,
              Zip::volume(R"({"number":1})", comicInfo({{"Series", "Listed"}})));

        const Facts::Composed composed = composedOf(at);
        const Manifest::Sidecar *work = sidecarAt(composed, u"work.json"_s);
        QVERIFY(work);
        QCOMPARE(work->json, QByteArray("[1, 2]"));
    }

    /// A sidecar that is not there is created, even holding nothing but `{"leaf":1}`: a
    /// folder the library holds is described by a Leaf file of its own.
    void a_sidecar_that_would_say_nothing_is_created_all_the_same()
    {
        const QString at = folder(u"Nothing"_s);
        write(at + u"/Tome 1.cbz"_s, Zip::volume(R"({"number":1})"));

        QList<Facts::Sheet> sheets = sheetsOf(at);
        for (const Facts::Field &field : sheetAt(sheets, u""_s)->fields) {
            if (field.origin == Facts::Origin::Guessed || field.origin == Facts::Origin::Taken)
                QVERIFY(Facts::edit(sheets, u""_s, field.key, QJsonValue()));
        }
        const Facts::Composed composed =
            Facts::compose(sheets, Manifest::of(at, false).sidecars);
        const Manifest::Sidecar *work = sidecarAt(composed, u"work.json"_s);
        QVERIFY(work);
        QCOMPARE(work->json, QByteArray(R"({"leaf":1})"));
    }

    /// Edited and brought back to what it said, a sidecar keeps its own bytes: reserialised,
    /// it changes the fingerprint the server keeps of it, for nothing.
    void a_sidecar_edited_back_to_what_it_said_keeps_its_bytes()
    {
        const QString at = folder(u"Back"_s);
        const QByteArray bytes = "{ \"title\" : \"A\",\n  \"readingDirection\" : \"RIGHT_TO_LEFT\" }\n";
        write(at + u"/work.json"_s, bytes);
        write(at + u"/Tome 1.cbz"_s, Zip::volume(R"({"number":1})"));

        QList<Facts::Sheet> sheets = sheetsOf(at);
        QVERIFY(Facts::edit(sheets, u""_s, u"title"_s, u"B"_s));
        QVERIFY(Facts::edit(sheets, u""_s, u"title"_s, u"A"_s));
        QVERIFY(fieldOf(sheetAt(sheets, u""_s), u"title"_s)->edited);
        const Facts::Composed composed =
            Facts::compose(sheets, Manifest::of(at, false).sidecars);
        QCOMPARE(sidecarAt(composed, u"work.json"_s)->json, bytes);
    }

    /// A sidecar no sheet names is not ours to judge: it leaves as it is, after every one a
    /// sheet composed.
    void a_sidecar_no_sheet_names_is_announced_at_the_end()
    {
        const QString at = folder(u"Stray"_s);
        write(at + u"/work.json"_s, R"({"title":"Stray","readingDirection":"LEFT_TO_RIGHT"})");
        write(at + u"/Tome 1.cbz"_s, Zip::volume(R"({"number":1})"));

        QList<Manifest::Sidecar> said = Manifest::of(at, false).sidecars;
        const Manifest::Sidecar stray{u"Ailleurs/edition.json"_s, R"({ "name" : "x" })"};
        said.prepend(stray);
        const Facts::Composed composed = Facts::compose(sheetsOf(at), said);
        QVERIFY(!composed.sidecars.isEmpty());
        QCOMPARE(composed.sidecars.constLast().path, stray.path);
        QCOMPARE(composed.sidecars.constLast().json, stray.json);
        QCOMPARE(composed.sidecars.constFirst().path, u"work.json"_s);
    }

    /// The exact bytes of a composed sidecar: its keys sorted and nothing between them, so
    /// that one reading is always one set of bytes.
    void a_composed_sidecar_is_its_keys_sorted_and_compact()
    {
        const QString at = folder(u"Exact"_s);
        write(at + u"/Tome 1.cbz"_s,
              Zip::volume(R"({"number":1})",
                          comicInfo({{"Series", "Blame!"}, {"Writer", "Nihei"},
                                     {"Publisher", "Glénat"}})));

        const Facts::Composed composed = composedOf(at);
        const Manifest::Sidecar *work = sidecarAt(composed, u"work.json"_s);
        QVERIFY(work);
        QCOMPARE(work->json,
                 QByteArray(R"({"authors":["Nihei"],"leaf":1,"publisher":"Glénat","title":"Blame!"})"));
    }

    // ——— Les rôles ———————————————————————————————————————————————————————————————————

    /// An archive carrying `oneshot.json` is a whole book: the scan lifts it out of wherever
    /// it sits. Read as a volume, it asked for a number it does not have and held its
    /// universe back for it.
    void an_archive_carrying_oneshot_json_is_a_book_said()
    {
        const QString at = folder(u"Arran Livre"_s);
        write(at + u"/universe.json"_s, R"({"name":"Arran"})");
        write(at + u"/Le Dernier Elfe.cbz"_s,
              Zip::volume({}, {},
                          R"({"leaf":1,"title":"Le Dernier Elfe","readingDirection":"LEFT_TO_RIGHT","authors":["Istin"],"artists":["Nico"]})"));

        const QList<Facts::Sheet> sheets = sheetsOf(at);
        const Facts::Sheet *book = sheetAt(sheets, u"Le Dernier Elfe.cbz"_s);
        QVERIFY(book);
        QCOMPARE(book->level, Manifest::Level::Book);
        QCOMPARE(book->role, Shape::Role::Book);
        QCOMPARE(book->roleOrigin, Shape::Origin::Said);
        QVERIFY(!book->oneShotUnreadable);
        QCOMPARE(book->oneShot.value(u"title"_s).toString(), u"Le Dernier Elfe"_s);
        QVERIFY(!fieldOf(book, u"number"_s));
        const Facts::Field *title = fieldOf(book, u"title"_s);
        QVERIFY(title);
        QCOMPARE(title->origin, Facts::Origin::Said);
        QCOMPARE(title->from, OneShotJson);
        QCOMPARE(title->weight, Facts::Weight::Required);
        const Facts::Field *direction = fieldOf(book, u"readingDirection"_s);
        QVERIFY(direction);
        QCOMPARE(direction->origin, Facts::Origin::Said);
        QCOMPARE(direction->weight, Facts::Weight::Required);
        QVERIFY(!Facts::needsReview(*book));
        QVERIFY(!Facts::blocks(*book));

        const Facts::Composed composed =
            Facts::compose(sheets, Manifest::of(at, false).sidecars);
        QVERIFY(composed.rewrites.isEmpty());
        QVERIFY(composed.leftOut.isEmpty());
    }

    /// A book is named like the server names it when `oneshot.json` is silent, except that a
    /// ComicInfo saying it comes first: its `Title`, then its `Series`, then the file's name.
    void a_book_takes_its_title_from_comic_info_then_from_its_name()
    {
        const QString at = folder(u"Arran Titres"_s);
        write(at + u"/universe.json"_s, R"({"name":"Arran"})");
        write(at + u"/a.cbz"_s,
              Zip::volume({}, comicInfo({{"Title", "Le titre"}, {"Series", "La série"},
                                         {"Writer", "Istin"}}),
                          R"({"leaf":1})"));
        write(at + u"/b.cbz"_s, Zip::volume({}, comicInfo({{"Series", "La série"}}), R"({"leaf":1})"));
        write(at + u"/Le Souffle.cbz"_s, Zip::volume({}, {}, R"({"leaf":1})"));

        const QList<Facts::Sheet> sheets = sheetsOf(at);
        const Facts::Field *first = fieldOf(sheetAt(sheets, u"a.cbz"_s), u"title"_s);
        QVERIFY(first);
        QCOMPARE(first->value.toString(), u"Le titre"_s);
        QCOMPARE(first->origin, Facts::Origin::Taken);
        const Facts::Field *authors = fieldOf(sheetAt(sheets, u"a.cbz"_s), u"authors"_s);
        QVERIFY(authors);
        QCOMPARE(authors->value.toArray(), QJsonArray{u"Istin"_s});
        const Facts::Field *second = fieldOf(sheetAt(sheets, u"b.cbz"_s), u"title"_s);
        QVERIFY(second);
        QCOMPARE(second->value.toString(), u"La série"_s);
        QCOMPARE(second->origin, Facts::Origin::Taken);
        const Facts::Field *third = fieldOf(sheetAt(sheets, u"Le Souffle.cbz"_s), u"title"_s);
        QVERIFY(third);
        QCOMPARE(third->value.toString(), u"Le Souffle"_s);
        QCOMPARE(third->origin, Facts::Origin::Guessed);
        QCOMPARE(third->from, u"Le Souffle.cbz"_s);
    }

    /// `visit_books_of` reads nothing at a universe's root but a book: an archive there that
    /// does not declare itself one was sent, written to the disk, and read by nobody.
    void an_archive_at_a_universe_root_that_declares_nothing_is_left_out_and_said_so()
    {
        const QString at = folder(u"Arran Perdu"_s);
        write(at + u"/universe.json"_s, R"({"name":"Arran"})");
        write(at + u"/Perdu.cbz"_s, Zip::volume({}, comicInfo({{"Title", "Perdu"}, {"Number", "1"}})));

        const QList<Facts::Sheet> sheets = sheetsOf(at);
        const Facts::Sheet *lost = sheetAt(sheets, u"Perdu.cbz"_s);
        QVERIFY(lost);
        QCOMPARE(lost->role, Shape::Role::Unread);
        QVERIFY(lost->fields.isEmpty());
        QVERIFY(Facts::needsReview(*lost));
        QVERIFY(!Facts::blocks(*lost));

        const Facts::Composed composed =
            Facts::compose(sheets, Manifest::of(at, false).sidecars);
        QCOMPARE(composed.leftOut, QSet<QString>{u"Perdu.cbz"_s});
        QVERIFY(composed.rewrites.isEmpty());

        // Said « Non lu » by a click, it is somebody's decision: still left out, and the step
        // no longer stops on it — a chosen role is not asked again.
        const Shape::Roles chosen{{u"Perdu.cbz"_s, {Shape::Role::Unread, Shape::Origin::Chosen}}};
        const QList<Facts::Sheet> decided = sheetsOf(at, chosen);
        const Facts::Sheet *left = sheetAt(decided, u"Perdu.cbz"_s);
        QVERIFY(left);
        QCOMPARE(left->role, Shape::Role::Unread);
        QCOMPARE(left->roleOrigin, Shape::Origin::Chosen);
        QVERIFY(!Facts::needsReview(*left));
        QCOMPARE(Facts::compose(decided, Manifest::of(at, false).sidecars).leftOut,
                 QSet<QString>{u"Perdu.cbz"_s});
    }

    /// Choosing « Livre seul » is writing `oneshot.json`: without it the scan reads no book.
    /// The archive's `entry.json` is not the book's, and leaves as it was, keys and all.
    void choosing_book_writes_a_oneshot_json_and_keeps_the_entry_json()
    {
        const QString at = folder(u"Arran Choisi"_s);
        write(at + u"/universe.json"_s, R"({"name":"Arran"})");
        write(at + u"/Seul.cbz"_s,
              Zip::volume(R"({"leaf":1,"myKey":1})",
                          comicInfo({{"Title", "Seul au monde"}, {"Writer", "Jarry"}})));

        const Shape::Roles roles{{u"Seul.cbz"_s, {Shape::Role::Book, Shape::Origin::Chosen}}};
        const QList<Facts::Sheet> sheets = sheetsOf(at, roles);
        const Facts::Sheet *book = sheetAt(sheets, u"Seul.cbz"_s);
        QVERIFY(book);
        QCOMPARE(book->level, Manifest::Level::Book);
        QCOMPARE(book->role, Shape::Role::Book);
        QCOMPARE(book->roleOrigin, Shape::Origin::Chosen);

        const Facts::Composed composed =
            Facts::compose(sheets, Manifest::of(at, false).sidecars);
        QVERIFY(composed.leftOut.isEmpty());
        QVERIFY(composed.rewrites.contains(u"Seul.cbz"_s));
        const Facts::Rewrite rewrite = composed.rewrites.value(u"Seul.cbz"_s);
        QCOMPARE(rewrite.entryJson, QByteArray(R"({"leaf":1,"myKey":1})"));
        QCOMPARE(objectOf(rewrite.oneShotJson),
                 (QJsonObject{{u"leaf"_s, 1}, {u"title"_s, u"Seul au monde"_s},
                              {u"authors"_s, QJsonArray{u"Jarry"_s}}}));
        QCOMPARE(rewrite.remove, QStringList{u"ComicInfo.xml"_s});
    }

    /// A book whose `oneshot.json` says everything has nothing to gain from a second copy of
    /// itself: its rewrite only takes its ComicInfo away.
    void a_book_already_said_in_full_only_loses_its_comicinfo()
    {
        const QString at = folder(u"Arran Complet"_s);
        write(at + u"/universe.json"_s, R"({"name":"Arran"})");
        write(at + u"/Plein.cbz"_s,
              Zip::volume(R"({"leaf":1})", comicInfo({{"Title", "Autre chose"}}),
                          R"({"leaf":1,"title":"Plein","authors":["A"],"artists":["B"],)"
                          R"("publisher":"Soleil","language":"fr","readingDirection":"LEFT_TO_RIGHT","genres":["Fantasy"],)"
                          R"("summary":"S","isbn":"978","publishedOn":"2020-01-01"})"));

        const QList<Facts::Sheet> sheets = sheetsOf(at);
        const Facts::Sheet *full = sheetAt(sheets, u"Plein.cbz"_s);
        QVERIFY(full);
        QCOMPARE(full->role, Shape::Role::Book);
        for (const Facts::Field &field : full->fields)
            QCOMPARE(field.origin, Facts::Origin::Said);
        const Facts::Composed composed = Facts::compose(sheets, Manifest::of(at, false).sidecars);
        QCOMPARE(composed.rewrites.size(), 1);
        const Facts::Rewrite rewrite = composed.rewrites.value(u"Plein.cbz"_s);
        QVERIFY(rewrite.oneShotJson.isEmpty());
        QCOMPARE(objectOf(rewrite.entryJson), (QJsonObject{{u"leaf"_s, 1}}));
        QCOMPARE(rewrite.remove, QStringList{u"ComicInfo.xml"_s});
    }

    /// An `oneshot.json` that is not an object is no book to the server, so it is none here,
    /// and choosing « Livre seul » on it changes nothing: writing a new one would replace what
    /// nobody could read. A rewrite of the volume around it leaves it where it is.
    void an_unreadable_oneshot_json_is_never_written_over()
    {
        const QString root = folder(u"Arran Illisible"_s);
        write(root + u"/universe.json"_s, R"({"name":"Arran"})");
        write(root + u"/Cassé.cbz"_s, Zip::volume({}, comicInfo({{"Title", "Cassé"}}), "[1, 2]"));
        const Shape::Roles chosen{{u"Cassé.cbz"_s, {Shape::Role::Book, Shape::Origin::Chosen}}};
        const QList<Facts::Sheet> sheets = sheetsOf(root, chosen);
        const Facts::Sheet *broken = sheetAt(sheets, u"Cassé.cbz"_s);
        QVERIFY(broken);
        QVERIFY(broken->oneShotUnreadable);
        QCOMPARE(broken->role, Shape::Role::Unread);
        const Facts::Composed composed =
            Facts::compose(sheets, Manifest::of(root, false).sidecars);
        QVERIFY(composed.rewrites.isEmpty());
        QCOMPARE(composed.leftOut, QSet<QString>{u"Cassé.cbz"_s});

        // An object the server's typed reading refuses is no book either: `sidecars::read`
        // gives nothing for `{"authors":"Jarry"}`, and the archive lands in `disregarded`.
        const QString typed = folder(u"Arran Mal Typé"_s);
        write(typed + u"/universe.json"_s, R"({"name":"Arran"})");
        const QList<std::pair<QString, QByteArray>> mistyped{
            {u"auteurs.cbz"_s, R"({"authors":"Jarry"})"},
            {u"titre.cbz"_s, R"({"title":5})"},
            {u"leaf.cbz"_s, R"({"leaf":"1"})"},
            {u"demi.cbz"_s, R"({"leaf":1.5})"},
            {u"couleur.cbz"_s, R"({"colour":"oui"})"},
            {u"genres.cbz"_s, R"({"genres":["Fantasy",3]})"},
            {u"nul.cbz"_s, R"({"tags":null})"},
        };
        Shape::Roles books;
        for (const auto &[file, json] : mistyped) {
            write(typed + u'/' + file, Zip::volume({}, comicInfo({{"Title", "T"}}), json));
            books.insert(file, {Shape::Role::Book, Shape::Origin::Chosen});
        }
        // What the server does accept stays a book: null for a single value, a key it
        // does not know.
        write(typed + u"/bon.cbz"_s,
              Zip::volume({}, {}, R"({"leaf":1,"title":null,"colour":true,"myKey":[1]})"));
        const QList<Facts::Sheet> typedSheets = sheetsOf(typed, books);
        const Facts::Composed typedComposed =
            Facts::compose(typedSheets, Manifest::of(typed, false).sidecars);
        for (const auto &[file, json] : mistyped) {
            const Facts::Sheet *one = sheetAt(typedSheets, file);
            QVERIFY2(one, qPrintable(file));
            QVERIFY2(one->oneShotUnreadable, json.constData());
            QCOMPARE(one->role, Shape::Role::Unread);
            QVERIFY2(!typedComposed.rewrites.contains(file), qPrintable(file));
            QVERIFY2(typedComposed.leftOut.contains(file), qPrintable(file));
        }
        const Facts::Sheet *good = sheetAt(typedSheets, u"bon.cbz"_s);
        QVERIFY(good);
        QVERIFY(!good->oneShotUnreadable);
        QCOMPARE(good->role, Shape::Role::Book);

        const QString series = folder(u"Série Illisible"_s);
        write(series + u"/Tome 1.cbz"_s,
              Zip::volume({}, comicInfo({{"Number", "1"}, {"Title", "T"}}), "pas du JSON"));
        const Shape::Roles inSeries{{u"Tome 1.cbz"_s, {Shape::Role::Book, Shape::Origin::Chosen}}};
        const QList<Facts::Sheet> volumes = sheetsOf(series, inSeries);
        const Facts::Sheet *volume = sheetAt(volumes, u"Tome 1.cbz"_s);
        QVERIFY(volume);
        QVERIFY(volume->oneShotUnreadable);
        QCOMPARE(volume->role, Shape::Role::Volume);
        QCOMPARE(volume->level, Manifest::Level::Volume);
        const Facts::Composed rewritten =
            Facts::compose(volumes, Manifest::of(series, false).sidecars);
        QVERIFY(rewritten.rewrites.contains(u"Tome 1.cbz"_s));
        QVERIFY(rewritten.rewrites.value(u"Tome 1.cbz"_s).oneShotJson.isEmpty());
    }

    /// A role read from the shape is a guess like a name read from a folder: the step stops
    /// on it until somebody says it. Said by a click, it stops nothing more.
    void a_guessed_role_stops_the_step_and_a_chosen_one_does_not()
    {
        const QString at = folder(u"Dragon Ball Choisi"_s);
        for (const QString &edition : {u"Perfect"_s, u"Original"_s}) {
            for (const int number : {1, 2})
                write(at + u'/' + edition + u"/Tome "_s + QString::number(number) + u".cbz"_s,
                      Zip::volume({}, comicInfo({{"Series", "Dragon Ball"},
                                                 {"Number", QByteArray::number(number)},
                                                 {"Writer", "Toriyama"},
                                                 {"Penciller", "Toriyama"},
                                                 {"Manga", "YesAndRightToLeft"}})));
        }

        Shape::Roles roles;
        const QList<Facts::Sheet> guessed = shapedSheetsOf(at, &roles);
        const Facts::Sheet *work = sheetAt(guessed, u""_s);
        QVERIFY(work);
        QCOMPARE(work->role, Shape::Role::Work);
        QCOMPARE(work->roleOrigin, Shape::Origin::Guessed);
        const Facts::Field *title = fieldOf(work, u"title"_s);
        QVERIFY(title);
        QCOMPARE(title->origin, Facts::Origin::Taken);
        QVERIFY(Facts::needsReview(*work));
        QVERIFY(!Facts::blocks(*work));

        roles[u""_s].origin = Shape::Origin::Chosen;
        const Shape::Drop drop = Shape::read(at);
        const QList<Facts::Sheet> chosen =
            Facts::read(Shape::cardNode(drop, {}), Manifest::of(at, false), roles);
        QVERIFY(sheetAt(chosen, u""_s));
        QCOMPARE(sheetAt(chosen, u""_s)->roleOrigin, Shape::Origin::Chosen);
        QVERIFY(!Facts::needsReview(*sheetAt(chosen, u""_s)));
    }

    /// The scan reads a folder of folders as a shelf: a universe read from the shape exists
    /// only once a `universe.json` says it, so one is written even when the sheet adds
    /// nothing to it — the confirmation is the file.
    void a_folder_read_as_a_universe_by_its_shape_gets_a_universe_json()
    {
        const QString at = folder(u"Terres d'Arran"_s);
        for (int number = 1; number <= 3; ++number)
            write(at + u"/Elfes/Tome "_s + QString::number(number) + u".cbz"_s, Zip::volume({}));
        for (int number = 1; number <= 4; ++number)
            write(at + u"/Mages/Tome "_s + QString::number(number) + u".cbz"_s, Zip::volume({}));

        QList<Facts::Sheet> sheets = shapedSheetsOf(at);
        QVERIFY(!sheets.isEmpty());
        QCOMPARE(sheets.first().level, Manifest::Level::Universe);
        QCOMPARE(sheets.first().roleOrigin, Shape::Origin::Guessed);
        const QList<Manifest::Sidecar> said = Manifest::of(at, false).sidecars;
        const Facts::Composed composed = Facts::compose(sheets, said);
        const Manifest::Sidecar *universe = sidecarAt(composed, u"universe.json"_s);
        QVERIFY(universe);
        QCOMPARE(universe->json, QByteArray("{\"leaf\":1,\"name\":\"Terres d'Arran\"}"));
        for (const QString &series : {u"Elfes"_s, u"Mages"_s}) {
            const Manifest::Sidecar *work = sidecarAt(composed, series + u"/work.json"_s);
            QVERIFY2(work, qPrintable(series));
            QCOMPARE(objectOf(work->json).value(u"title"_s), series);
        }

        QVERIFY(Facts::edit(sheets, u""_s, u"name"_s, QJsonValue()));
        const Facts::Composed renamed = Facts::compose(sheets, said);
        const Manifest::Sidecar *bare = sidecarAt(renamed, u"universe.json"_s);
        QVERIFY(bare);
        QCOMPARE(bare->json, QByteArray(R"({"leaf":1})"));
    }

    /// Two editions read from the shape are one series: one `work.json` above them, one
    /// `edition.json` in each, and no universe nobody asked for.
    void a_series_in_two_editions_by_its_shape_writes_one_work_and_two_editions()
    {
        const QString at = folder(u"Dragon Ball"_s + u" Deux"_s);
        for (const QString &edition : {u"Perfect"_s, u"Original"_s}) {
            for (const int number : {1, 2})
                write(at + u'/' + edition + u"/Tome "_s + QString::number(number) + u".cbz"_s,
                      Zip::volume({}));
        }

        const Facts::Composed composed =
            Facts::compose(shapedSheetsOf(at), Manifest::of(at, false).sidecars);
        QStringList paths;
        for (const Manifest::Sidecar &one : composed.sidecars)
            paths << one.path;
        QCOMPARE(paths, (QStringList{u"work.json"_s, u"Original/edition.json"_s,
                                     u"Perfect/edition.json"_s}));
        QCOMPARE(objectOf(composed.sidecars.at(1).json).value(u"name"_s), u"Original"_s);
        QCOMPARE(objectOf(composed.sidecars.at(2).json).value(u"name"_s), u"Perfect"_s);
    }

    /// A series and its editions read from the shape exist for the scan only through their
    /// files: emptied of everything they said, `work.json` and each `edition.json` still
    /// leave, `{"leaf":1}` alone. Not written, the folder would go back to a shelf.
    void a_guessed_series_and_its_editions_are_written_even_saying_nothing()
    {
        const QString at = folder(u"Dragon Ball Vide"_s);
        for (const QString &edition : {u"Perfect"_s, u"Original"_s}) {
            for (const int number : {1, 2})
                write(at + u'/' + edition + u"/Tome "_s + QString::number(number) + u".cbz"_s,
                      Zip::volume({}));
        }

        QList<Facts::Sheet> sheets = shapedSheetsOf(at);
        const QStringList containers{u""_s, u"Original"_s, u"Perfect"_s};
        for (const QString &one : containers) {
            const Facts::Sheet *sheet = sheetAt(sheets, one);
            QVERIFY2(sheet, qPrintable(one));
            QCOMPARE(sheet->roleOrigin, Shape::Origin::Guessed);
            const QList<Facts::Field> fields = sheet->fields;
            for (const Facts::Field &field : fields) {
                if (field.origin != Facts::Origin::Missing)
                    QVERIFY(Facts::edit(sheets, one, field.key, QJsonValue()));
            }
        }
        const Facts::Composed composed =
            Facts::compose(sheets, Manifest::of(at, false).sidecars);
        for (const QString &path :
             {u"work.json"_s, u"Original/edition.json"_s, u"Perfect/edition.json"_s}) {
            const Manifest::Sidecar *written = sidecarAt(composed, path);
            QVERIFY2(written, qPrintable(path));
            QCOMPARE(written->json, QByteArray(R"({"leaf":1})"));
        }
    }

    // ——— Les crédits ————————————————————————————————————————————————————————————————

    /// A script that changes hands at volume 2 has two writers, and the first volume that
    /// named one used to name the series. Each volume says its own, from its `entry.json` or,
    /// failing that, from its ComicInfo — the same field by field rule as the rest.
    void each_volume_carries_its_own_credits()
    {
        const QString at = folder(u"Credits"_s);
        write(at + u"/Tome 1.cbz"_s,
              Zip::volume(R"({"number":1,"authors":["A"],"artists":["B"]})"));
        write(at + u"/Tome 4.cbz"_s,
              Zip::volume(R"({"number":4})", comicInfo({{"Writer", "A"}, {"Penciller", "F"}})));

        const QList<Facts::Sheet> sheets = sheetsOf(at);
        const Facts::Sheet *first = sheetAt(sheets, u"Tome 1.cbz"_s);
        const Facts::Field *writer = fieldOf(first, u"authors"_s);
        QVERIFY(writer);
        QCOMPARE(writer->origin, Facts::Origin::Said);
        QCOMPARE(writer->from, u"entry.json"_s);
        QCOMPARE(writer->value.toArray(), QJsonArray{u"A"_s});
        const Facts::Field *drawer = fieldOf(first, u"artists"_s);
        QVERIFY(drawer);
        QCOMPARE(drawer->origin, Facts::Origin::Said);
        QCOMPARE(drawer->value.toArray(), QJsonArray{u"B"_s});

        const Facts::Sheet *fourth = sheetAt(sheets, u"Tome 4.cbz"_s);
        writer = fieldOf(fourth, u"authors"_s);
        QVERIFY(writer);
        QCOMPARE(writer->origin, Facts::Origin::Taken);
        QCOMPARE(writer->from, u"ComicInfo.xml"_s);
        // Required of every volume now: its series says it for it when it says nothing.
        QCOMPARE(writer->weight, Facts::Weight::Required);
        QCOMPARE(writer->value.toArray(), QJsonArray{u"A"_s});
        drawer = fieldOf(fourth, u"artists"_s);
        QVERIFY(drawer);
        QCOMPARE(drawer->origin, Facts::Origin::Taken);
        QCOMPARE(drawer->from, u"ComicInfo.xml"_s);
        QCOMPARE(drawer->value.toArray(), QJsonArray{u"F"_s});

        // After the title, before the date: the order a sheet shows them in.
        QStringList keys;
        for (const Facts::Field &field : fourth->fields)
            keys << field.key;
        QCOMPARE(keys.mid(0, 4), (QStringList{u"number"_s, u"title"_s, u"authors"_s,
                                              u"artists"_s}));
    }

    /// `Writer` is a list, as `Genre` is: « Arleston, Tarquin » is two people, and the server
    /// splits it, so a client that kept it as one name announced a credit nobody is called.
    void a_comma_between_two_names_in_comic_info_is_two_people()
    {
        const QString at = folder(u"Two Names"_s);
        write(at + u"/Tome 1.cbz"_s,
              Zip::volume(R"({"number":1})", comicInfo({{"Writer", "Arleston, Tarquin"}})));

        const QList<Facts::Sheet> sheets = sheetsOf(at);
        const Facts::Field *writer = fieldOf(sheetAt(sheets, u"Tome 1.cbz"_s), u"authors"_s);
        QVERIFY(writer);
        QCOMPARE(writer->value.toArray(), (QJsonArray{u"Arleston"_s, u"Tarquin"_s}));
    }

    /// A series is whoever its volumes name, and it writes only what most of them agree on:
    /// three writers, one each, raise nothing; two volumes of three drawn by B, one of them
    /// read from its ComicInfo, make B the series' drawer.
    void a_series_is_credited_with_the_union_of_its_volumes_and_writes_what_most_agree_on()
    {
        const QString at = folder(u"Union"_s);
        write(at + u"/Tome 1.cbz"_s,
              Zip::volume(R"({"number":1,"authors":["A"],"artists":["B"]})"));
        write(at + u"/Tome 2.cbz"_s,
              Zip::volume(R"({"number":2,"authors":["C"],"artists":["D"]})"));
        write(at + u"/Tome 3.cbz"_s,
              Zip::volume(R"({"number":3})", comicInfo({{"Writer", "E"}, {"Penciller", "B"}})));

        const QList<Facts::Sheet> sheets = sheetsOf(at);
        const Facts::Sheet *series = sheetAt(sheets, u""_s);
        QVERIFY(series);
        QCOMPARE(series->credits.credited.authors, (QStringList{u"A"_s, u"C"_s, u"E"_s}));
        QCOMPARE(series->credits.credited.artists, (QStringList{u"B"_s, u"D"_s}));
        QCOMPARE(fieldOf(series, u"authors"_s)->origin, Facts::Origin::Missing);
        // The drawing has a box of its own on the series since a series nobody draws is held
        // back (`Facts::lacks`), and it stays empty for the same reason the writing does.
        QVERIFY(fieldOf(series, u"artists"_s));
        QCOMPARE(fieldOf(series, u"artists"_s)->origin, Facts::Origin::Missing);
        QVERIFY(!Facts::lacks(*series, *fieldOf(series, u"authors"_s)));
        QVERIFY(!Facts::lacks(*series, *fieldOf(series, u"artists"_s)));

        const Facts::Composed composed = composedOf(at);
        const Manifest::Sidecar *work = sidecarAt(composed, u"work.json"_s);
        QVERIFY(work);
        const QJsonObject object = objectOf(work->json);
        QVERIFY(!object.contains(u"authors"_s));
        QCOMPARE(object.value(u"artists"_s).toArray(), QJsonArray{u"B"_s});
    }

    /// The union follows the volumes as they are corrected: a name typed over a volume's
    /// credit is the series' too, and the one it replaced is gone from it.
    void the_union_follows_a_credit_corrected_in_a_volume()
    {
        const QString at = folder(u"Corrected"_s);
        write(at + u"/Tome 1.cbz"_s, Zip::volume(R"({"number":1,"authors":["A"]})"));
        write(at + u"/Tome 2.cbz"_s, Zip::volume(R"({"number":2,"authors":["C"]})"));

        QList<Facts::Sheet> sheets = sheetsOf(at);
        QVERIFY(Facts::edit(sheets, u"Tome 2.cbz"_s, u"authors"_s, QJsonArray{u"Z"_s}));
        QCOMPARE(sheetAt(sheets, u""_s)->credits.credited.authors, (QStringList{u"A"_s, u"Z"_s}));
    }

    /// What `work.json` says of the authors is the series' and wins over the volumes, as on
    /// the server — and the singular key it said before `authors` existed is still said.
    void credits_written_in_work_json_stay_said()
    {
        const QString at = folder(u"Said Credits"_s);
        write(at + u"/work.json"_s, R"({"title":"X","authors":["Collectif"]})");
        write(at + u"/Tome 1.cbz"_s, Zip::volume(R"({"number":1,"authors":["A"]})"));
        const QString old = folder(u"Said Singular"_s);
        write(old + u"/work.json"_s, R"({"title":"X","author":"Jarry"})");
        write(old + u"/Tome 1.cbz"_s, Zip::volume(R"({"number":1,"authors":["A"]})"));

        const QList<Facts::Sheet> sheets = sheetsOf(at);
        const QList<Facts::Sheet> oldSheets = sheetsOf(old);
        const Facts::Field *authors = fieldOf(sheetAt(sheets, u""_s), u"authors"_s);
        QVERIFY(authors);
        QCOMPARE(authors->origin, Facts::Origin::Said);
        QCOMPARE(authors->from, u"work.json"_s);
        QCOMPARE(authors->value.toArray(), QJsonArray{u"Collectif"_s});
        const Facts::Field *singular = fieldOf(sheetAt(oldSheets, u""_s), u"authors"_s);
        QVERIFY(singular);
        QCOMPARE(singular->origin, Facts::Origin::Said);
        QCOMPARE(singular->value.toArray(), QJsonArray{u"Jarry"_s});
        const Facts::Composed composed = composedOf(at);
        QCOMPARE(sidecarAt(composed, u"work.json"_s)->json,
                 QByteArray(R"({"title":"X","authors":["Collectif"]})"));
    }

    /// « Arleston », « ARLESTON » and « Arlestón » are one person typed three ways, and the
    /// server counts her once under the spelling it met first; a count of authors that
    /// doubled her would not match the one the library shows.
    void the_same_person_spelled_twice_is_counted_once()
    {
        const QString at = folder(u"Spelled"_s);
        write(at + u"/Tome 1.cbz"_s,
              Zip::volume(R"({"number":1,"authors":["Arleston","Jean Petit"]})"));
        write(at + u"/Tome 2.cbz"_s,
              Zip::volume(R"({"number":2,"authors":["ARLESTON","JEAN   PETIT"]})"));
        write(at + u"/Tome 3.cbz"_s,
              Zip::volume(R"({"number":3,"authors":["Arlestón","Autre"]})"));

        const QList<Facts::Sheet> sheets = sheetsOf(at);
        QCOMPARE(sheetAt(sheets, u""_s)->credits.credited.authors,
                 (QStringList{u"Arleston"_s, u"Jean Petit"_s, u"Autre"_s}));
    }

    /// One person is one as the server folds her name, not as this client folded it: a hyphen
    /// and an accent, an apostrophe, a ligature. Folded otherwise, « D’Aviau » and « DAviau »
    /// were two authors here and one in the library.
    void the_same_person_is_one_however_the_server_folds_her_name()
    {
        const QString at = folder(u"Folded"_s);
        write(at + u"/Tome 1.cbz"_s,
              // Not a raw string: moc reads an apostrophe in one as a character that never ends.
              Zip::volume("{\"number\":1,\"authors\":[\"Jean-Claude Mézières\",\"D'Aviau\","
                          "\"Cœur\"]}"));
        write(at + u"/Tome 2.cbz"_s,
              Zip::volume(R"({"number":2,"authors":["Jean Claude Mezieres","DAviau","Coeur"]})"));

        const QList<Facts::Sheet> sheets = sheetsOf(at);
        QCOMPARE(sheetAt(sheets, u""_s)->credits.credited.authors,
                 (QStringList{u"Jean-Claude Mézières"_s, u"D'Aviau"_s, u"Cœur"_s}));
    }

    /// The spelling kept is the one the server meets first, and it meets the files by their
    /// bytes: « Tome 10 » before « Tome 2 ». In the client's own order, numbers in order, the
    /// series was credited to « Arleston » and recorded as « ARLESTON ».
    void the_spelling_kept_is_the_one_the_server_reads_first()
    {
        const QString at = folder(u"Bytes"_s);
        write(at + u"/Tome 2.cbz"_s, Zip::volume(R"({"number":2,"authors":["Arleston"]})"));
        write(at + u"/Tome 10.cbz"_s, Zip::volume(R"({"number":10,"authors":["ARLESTON"]})"));

        const QList<Facts::Sheet> sheets = sheetsOf(at);
        QCOMPARE(sheetAt(sheets, u""_s)->credits.credited.authors, QStringList{u"ARLESTON"_s});
        QVERIFY(Facts::readBefore(u""_s, u"Tome 10.cbz"_s, u"Tome 2.cbz"_s));
        // Beside the series' folder first, then its edition folders.
        QVERIFY(Facts::readBefore(u"S"_s, u"S/Z.cbz"_s, u"S/A/Tome 1.cbz"_s));
        // Component by component: « A » before « A-B », though « - » is before « / ».
        QVERIFY(Facts::readBefore(u"S"_s, u"S/A/x.cbz"_s, u"S/A-B/x.cbz"_s));
    }

    /// A book lifted out of the folder is not one of its volumes: whoever wrote it is not
    /// the series' author.
    void a_book_does_not_credit_the_series_it_sits_in()
    {
        const QString at = folder(u"Lifted"_s);
        write(at + u"/Tome 1.cbz"_s, Zip::volume(R"({"number":1,"authors":["A"]})"));
        write(at + u"/Hors-serie.cbz"_s,
              Zip::volume(R"({"authors":["Ailleurs"]})", {}, R"({"leaf":1})"));

        const QList<Facts::Sheet> sheets = sheetsOf(at);
        QCOMPARE(sheetAt(sheets, u""_s)->credits.credited.authors, QStringList{u"A"_s});
    }

    /// A volume naming no drawer was a doubt among volumes that name one — « Aucun crédit ici »
    /// — weighed against its neighbours. It is now a fact: it takes its series' drawer, and
    /// there is nothing to ask.
    void a_volume_naming_no_drawer_takes_its_series_drawer_and_asks_nothing()
    {
        const QString at = folder(u"Missing Here"_s);
        write(at + u"/work.json"_s, R"({"title":"X","readingDirection":"LEFT_TO_RIGHT","authors":["Jarry"],"artists":["Jarry"]})");
        write(at + u"/Perfect/edition.json"_s, R"({"name":"Perfect"})");
        write(at + u"/Original/edition.json"_s, R"({"name":"Original"})");
        for (const int number : {1, 2, 3}) {
            const QByteArray entry = number == 3
                ? R"({"number":3,"authors":["A"]})"
                : "{\"number\":" + QByteArray::number(number) + R"(,"authors":["A"],"artists":["B"]})";
            write(at + u"/Perfect/Tome "_s + QString::number(number) + u".cbz"_s,
                  Zip::volume(entry));
        }
        // Nobody is credited in this edition: nothing there to be missing.
        write(at + u"/Original/Tome 1.cbz"_s, Zip::volume(R"({"number":1})"));

        QList<Facts::Sheet> sheets = sheetsOf(at);
        const Facts::Sheet *third = sheetAt(sheets, u"Perfect/Tome 3.cbz"_s);
        QVERIFY(third);
        QVERIFY(!Facts::needsReview(*third));
        QVERIFY(!Facts::blocks(*third));
        QVERIFY(fieldOf(third, u"artists"_s)->doubt.isEmpty());
        QCOMPARE(third->credits.fallback.artists, QStringList{u"Jarry"_s});
        QVERIFY(!Facts::needsReview(*sheetAt(sheets, u"Perfect/Tome 1.cbz"_s)));
        QVERIFY(!Facts::needsReview(*sheetAt(sheets, u"Original/Tome 1.cbz"_s)));

        // Said once, it is its own.
        QVERIFY(Facts::edit(sheets, u"Perfect/Tome 3.cbz"_s, u"artists"_s, QJsonArray{u"B"_s}));
        QVERIFY(!Facts::needsReview(*sheetAt(sheets, u"Perfect/Tome 3.cbz"_s)));
    }

    /// A library that credits nobody is the common one, and asking about every volume of it
    /// would make the step a wall.
    void a_series_where_no_volume_is_credited_asks_nothing()
    {
        const QString at = folder(u"Nobody"_s);
        write(at + u"/work.json"_s, R"({"title":"X","readingDirection":"LEFT_TO_RIGHT","authors":["Jarry"],"artists":["Jarry"]})");
        for (const int number : {1, 2})
            write(at + u"/Tome "_s + QString::number(number) + u".cbz"_s,
                  Zip::volume("{\"number\":" + QByteArray::number(number) + '}'));

        const QList<Facts::Sheet> sheets = sheetsOf(at);
        for (const Facts::Sheet &sheet : sheets) {
            QVERIFY2(!Facts::needsReview(sheet), qPrintable(sheet.at));
            for (const Facts::Field &field : sheet.fields)
                QVERIFY2(field.doubt.isEmpty(), qPrintable(sheet.at + u" "_s + field.key));
        }
        // Credited with the default its volumes inherit, as the server records it.
        QCOMPARE(sheetAt(sheets, u""_s)->credits.credited.authors, QStringList{u"Jarry"_s});
        QCOMPARE(sheetAt(sheets, u""_s)->credits.credited.artists, QStringList{u"Jarry"_s});
    }

    /// The three things a library is searched and read by: a series nobody is said to have
    /// written, drawn, or to be read in some direction goes nowhere until it is said. Each one
    /// alone holds it back, and each is counted for the footer.
    void a_series_with_no_writer_no_drawer_or_no_direction_is_held_back_for_each()
    {
        const QString direction = u"\"readingDirection\":\"RIGHT_TO_LEFT\""_s;
        const QString writer = u"\"authors\":[\"Toriyama\"]"_s;
        const QString drawer = u"\"artists\":[\"Toriyama\"]"_s;
        const QList<std::pair<QStringList, int>> cases{
            {{direction, drawer}, 1},          // no writer
            {{direction, writer}, 1},          // no drawer
            {{writer, drawer}, 1},             // no direction
            {{direction}, 2},                  // neither of the two
            {{}, 3},                           // nothing
            {{direction, writer, drawer}, 0},  // all three
        };
        int which = 0;
        for (const auto &[said, lacking] : cases) {
            const QString at = folder(u"Held "_s + QString::number(which++));
            write(at + u"/work.json"_s,
                  ("{\"title\":\"X\"" + (said.isEmpty() ? QString() : u","_s + said.join(u","_s)) + u"}"_s)
                      .toUtf8());
            write(at + u"/Tome 1.cbz"_s, Zip::volume(R"({"number":1})"));
            const QList<Facts::Sheet> sheets = sheetsOf(at);
            const Facts::Sheet *series = sheetAt(sheets, u""_s);
            QVERIFY(series);
            QCOMPARE(Facts::lacking(*series), lacking);
            QCOMPARE(Facts::blocks(*series), lacking > 0);
            QCOMPARE(Facts::needsReview(*series), lacking > 0);
        }
    }

    /// What the volumes say stands for themselves and no further: a series is not missing a
    /// writer every volume names, but one volume drawn by nobody — not by itself, not by its
    /// series — holds the series back, and the series is where it is put right.
    void one_volume_drawn_by_nobody_holds_its_series_back()
    {
        const QString at = folder(u"Volumes Say"_s);
        write(at + u"/work.json"_s, R"({"title":"X","readingDirection":"LEFT_TO_RIGHT"})");
        write(at + u"/Tome 1.cbz"_s,
              Zip::volume(R"({"number":1,"authors":["A"],"artists":["B"]})"));
        write(at + u"/Tome 2.cbz"_s, Zip::volume(R"({"number":2,"authors":["A"]})"));

        QList<Facts::Sheet> sheets = sheetsOf(at);
        const Facts::Sheet *series = sheetAt(sheets, u""_s);
        QCOMPARE(Facts::lacking(*series), 1);
        QCOMPARE(Facts::lackingFor(*series, u"artists"_s), 1);
        QCOMPARE(Facts::lackingFor(*series, u"authors"_s), 0);
        QVERIFY(Facts::blocks(*series));
        const Facts::Sheet *second = sheetAt(sheets, u"Tome 2.cbz"_s);
        QVERIFY(fieldOf(second, u"artists"_s)->doubt.isEmpty());
        QVERIFY(Facts::lacks(*second, *fieldOf(second, u"artists"_s)));
        // Counted on its series, where it is put right — not once more on the volume.
        QCOMPARE(Facts::lacking(*second), 0);

        // The other drawer corrected away: two volumes drawn by nobody are still one value.
        QVERIFY(Facts::edit(sheets, u"Tome 1.cbz"_s, u"artists"_s, QJsonValue()));
        QCOMPARE(Facts::lacking(*sheetAt(sheets, u""_s)), 1);
        QCOMPARE(Facts::lackingFor(*sheetAt(sheets, u""_s), u"artists"_s), 2);
        QVERIFY(Facts::blocks(*sheetAt(sheets, u""_s)));
    }

    /// Nothing anywhere says who wrote or drew these volumes: each lacks both, and the series
    /// holds them back.
    void a_volume_credited_by_nobody_where_the_series_says_nothing_lacks_both()
    {
        const QString at = folder(u"Bare"_s);
        write(at + u"/work.json"_s, R"({"title":"X","readingDirection":"LEFT_TO_RIGHT"})");
        for (const int number : {1, 2})
            write(at + u"/Tome "_s + QString::number(number) + u".cbz"_s,
                  Zip::volume("{\"number\":" + QByteArray::number(number) + '}'));

        const QList<Facts::Sheet> sheets = sheetsOf(at);
        for (const QString &file : {u"Tome 1.cbz"_s, u"Tome 2.cbz"_s}) {
            const Facts::Sheet *volume = sheetAt(sheets, file);
            QVERIFY(Facts::lacks(*volume, *fieldOf(volume, u"authors"_s)));
            QVERIFY(Facts::lacks(*volume, *fieldOf(volume, u"artists"_s)));
        }
        QVERIFY(Facts::blocks(*sheetAt(sheets, u""_s)));
    }

    /// One Piece: « Oda » typed once on the series releases every volume that named nobody.
    void a_series_default_releases_every_volume_at_once()
    {
        const QString at = folder(u"Released"_s);
        write(at + u"/work.json"_s, R"({"title":"X","readingDirection":"LEFT_TO_RIGHT"})");
        for (const int number : {1, 2})
            write(at + u"/Tome "_s + QString::number(number) + u".cbz"_s,
                  Zip::volume("{\"number\":" + QByteArray::number(number) + '}'));

        QList<Facts::Sheet> sheets = sheetsOf(at);
        QVERIFY(Facts::edit(sheets, u""_s, u"authors"_s, QJsonArray{u"Oda"_s}));
        QVERIFY(Facts::edit(sheets, u""_s, u"artists"_s, QJsonArray{u"Oda"_s}));
        for (const Facts::Sheet &sheet : sheets) {
            QVERIFY2(!Facts::blocks(sheet), qPrintable(sheet.at));
            for (const Facts::Field &field : sheet.fields)
                QVERIFY2(!Facts::lacks(sheet, field), qPrintable(sheet.at + u" "_s + field.key));
        }
        QCOMPARE(sheetAt(sheets, u"Tome 2.cbz"_s)->credits.fallback.authors, QStringList{u"Oda"_s});
    }

    /// One volume credited among forty-two: the footer said nothing was missing beside
    /// forty-one red volumes. It is one value per credit, on the series, and it names how
    /// many volumes wait for it.
    void one_volume_credited_among_forty_two_makes_one_value_missing_not_forty_one()
    {
        const QString at = folder(u"Forty Two"_s);
        write(at + u"/work.json"_s, R"({"title":"X","readingDirection":"LEFT_TO_RIGHT"})");
        write(at + u"/Tome 1.cbz"_s,
              Zip::volume(R"({"number":1,"authors":["A"],"artists":["B"]})"));
        for (int number = 2; number <= 42; ++number)
            write(at + u"/Tome "_s + QString::number(number) + u".cbz"_s,
                  Zip::volume("{\"number\":" + QByteArray::number(number) + '}'));

        const QList<Facts::Sheet> sheets = sheetsOf(at);
        const Facts::Sheet *series = sheetAt(sheets, u""_s);
        QCOMPARE(Facts::lacking(*series), 2);
        QCOMPARE(Facts::lackingFor(*series, u"authors"_s), 41);
        QCOMPARE(Facts::lackingFor(*series, u"artists"_s), 41);
        for (const Facts::Sheet &sheet : sheets) {
            if (sheet.at.isEmpty())
                continue;
            QCOMPARE(Facts::lacking(sheet), 0);
        }
    }

    /// A ComicInfo only fills a hole. One that names somebody else than the series' default is
    /// ignored: the volume inherits, nothing is in doubt, and nothing of it is written.
    void a_comicinfo_that_contradicts_the_series_default_is_ignored()
    {
        const QString at = folder(u"Ignored"_s);
        write(at + u"/work.json"_s,
              R"({"title":"X","readingDirection":"LEFT_TO_RIGHT","authors":["Oda"],"artists":["Oda"]})");
        write(at + u"/Tome 1.cbz"_s,
              Zip::volume(R"({"number":1})", comicInfo({{"Writer", "X"}})));

        const QList<Facts::Sheet> sheets = sheetsOf(at);
        const Facts::Field *writer = fieldOf(sheetAt(sheets, u"Tome 1.cbz"_s), u"authors"_s);
        QCOMPARE(writer->origin, Facts::Origin::Missing);
        QVERIFY(writer->doubt.isEmpty());
        const Facts::Composed composed = Facts::compose(sheets, Manifest::of(at, false).sidecars);
        for (auto it = composed.rewrites.cbegin(); it != composed.rewrites.cend(); ++it)
            QVERIFY2(!it.value().entryJson.contains("\"X\""), it.value().entryJson.constData());
    }

    /// Where no Leaf file says who wrote a volume, its ComicInfo does.
    void a_comicinfo_fills_what_no_json_says()
    {
        const QString at = folder(u"Filled"_s);
        write(at + u"/work.json"_s, R"({"title":"X","readingDirection":"LEFT_TO_RIGHT"})");
        write(at + u"/Tome 1.cbz"_s,
              Zip::volume(R"({"number":1})", comicInfo({{"Writer", "X"}})));

        const QList<Facts::Sheet> sheets = sheetsOf(at);
        const Facts::Field *writer = fieldOf(sheetAt(sheets, u"Tome 1.cbz"_s), u"authors"_s);
        QCOMPARE(writer->origin, Facts::Origin::Taken);
        QCOMPARE(writer->value.toArray(), QJsonArray{u"X"_s});
    }

    /// A number read off a file name was « Deviné » and asked to be looked at, on every
    /// volume of a library named « Tome 01 » to « Tome 76 ». The value is shown; that is all.
    void a_number_read_from_a_file_name_asks_for_no_review()
    {
        const QString at = folder(u"NamedOnly"_s);
        write(at + u"/work.json"_s,
              R"({"title":"X","readingDirection":"LEFT_TO_RIGHT","authors":["A"],"artists":["B"]})");
        write(at + u"/Tome 01.cbz"_s, Zip::volume(R"({"leaf":1})"));

        const QList<Facts::Sheet> sheets = sheetsOf(at);
        const Facts::Sheet *volume = sheetAt(sheets, u"Tome 01.cbz"_s);
        QCOMPARE(fieldOf(volume, u"number"_s)->origin, Facts::Origin::Guessed);
        for (const Facts::Sheet &sheet : sheets)
            QVERIFY2(!Facts::needsReview(sheet), qPrintable(sheet.at));
    }

    // ——— Where the import writes ——————————————————————————————————————————————

    /// The authors a volume's composed `entry.json` names, or nothing when it names none or
    /// the volume asks for no rewrite.
    static QJsonArray writtenAuthors(const Facts::Composed &composed, const QString &file)
    {
        return objectOf(composed.rewrites.value(file).entryJson).value(u"authors"_s).toArray();
    }

    /// A series with no writer of its own, and `writers` its volumes' ComicInfo names, one
    /// per volume, each volume carrying an `entry.json` with its number.
    QString aSeriesWrittenBy(const QString &name, const QList<QByteArray> &writers)
    {
        const QString at = folder(name);
        write(at + u"/work.json"_s, R"({"title":"X","readingDirection":"RIGHT_TO_LEFT"})");
        for (int one = 0; one < writers.size(); ++one)
            write(at + u"/Tome %1.cbz"_s.arg(one + 1),
                  Zip::volume(R"({"number":)" + QByteArray::number(one + 1) + '}',
                              comicInfo({{"Writer", writers.at(one)}})));
        return at;
    }

    /// Forty ComicInfo saying Oda were forty copies of one name in forty archives. The name
    /// the strict majority carries is the series' own, and only the two that differ say theirs.
    void forty_comicinfos_saying_oda_and_two_saying_otherwise_give_the_series_oda_and_two_exceptions()
    {
        QList<QByteArray> writers(40, "Oda");
        writers << "Autre" << "Autre";
        const Facts::Composed composed = composedOf(aSeriesWrittenBy(u"Forty"_s, writers));

        const Manifest::Sidecar *work = sidecarAt(composed, u"work.json"_s);
        QVERIFY(work);
        QCOMPARE(objectOf(work->json).value(u"authors"_s).toArray(), QJsonArray{u"Oda"_s});
        for (int one = 1; one <= 40; ++one)
            QVERIFY2(writtenAuthors(composed, u"Tome %1.cbz"_s.arg(one)).isEmpty(),
                     qPrintable(QString::number(one)));
        QCOMPARE(writtenAuthors(composed, u"Tome 41.cbz"_s), QJsonArray{u"Autre"_s});
        QCOMPARE(writtenAuthors(composed, u"Tome 42.cbz"_s), QJsonArray{u"Autre"_s});
    }

    /// Half is no majority: nothing is raised, and each volume says its own.
    void a_tie_raises_nothing_to_the_series()
    {
        QList<QByteArray> writers(21, "Oda");
        writers << QList<QByteArray>(21, "Autre");
        const Facts::Composed composed = composedOf(aSeriesWrittenBy(u"Tie"_s, writers));

        const Manifest::Sidecar *work = sidecarAt(composed, u"work.json"_s);
        QVERIFY(!work || !objectOf(work->json).contains(u"authors"_s));
        for (int one = 1; one <= 42; ++one)
            QCOMPARE(writtenAuthors(composed, u"Tome %1.cbz"_s.arg(one)),
                     QJsonArray{one <= 21 ? u"Oda"_s : u"Autre"_s});
    }

    /// Two spellings the search folds together are one person, as the server counts them:
    /// one majority, written once, as it was first met.
    void spellings_that_fold_together_count_as_one_majority()
    {
        const Facts::Composed composed =
            composedOf(aSeriesWrittenBy(u"Folded"_s, {"Arleston", "Arleston", "ARLESTON"}));

        const Manifest::Sidecar *work = sidecarAt(composed, u"work.json"_s);
        QVERIFY(work);
        QCOMPARE(objectOf(work->json).value(u"authors"_s).toArray(), QJsonArray{u"Arleston"_s});
        for (int one = 1; one <= 3; ++one)
            QVERIFY(writtenAuthors(composed, u"Tome %1.cbz"_s.arg(one)).isEmpty());
    }

    /// A book standing alone is no volume of the series it sits in: its writer counts in no
    /// majority, here two books against one volume.
    void a_book_standing_alone_counts_in_no_majority()
    {
        const QString at = aSeriesWrittenBy(u"Books"_s, {"A"});
        for (const QString &book : {u"Livre 1.cbz"_s, u"Livre 2.cbz"_s})
            write(at + u'/' + book, Zip::volume({}, comicInfo({{"Writer", "B"}}),
                                                R"({"title":"Livre"})"));
        const Facts::Composed composed = composedOf(at);

        const Manifest::Sidecar *work = sidecarAt(composed, u"work.json"_s);
        QVERIFY(work);
        QCOMPARE(objectOf(work->json).value(u"authors"_s).toArray(), QJsonArray{u"A"_s});
    }

    /// What an `entry.json` already says stays there: three volumes saying Oda are not moved
    /// into the series, since nothing was read or typed that needs a place.
    void a_value_already_in_an_entry_json_is_never_moved()
    {
        const QString at = folder(u"Said"_s);
        write(at + u"/work.json"_s, R"({"title":"X","readingDirection":"RIGHT_TO_LEFT"})");
        for (int one = 1; one <= 3; ++one)
            write(at + u"/Tome %1.cbz"_s.arg(one),
                  Zip::volume(R"({"number":)" + QByteArray::number(one)
                              + R"(,"authors":["Oda"],"artists":["Oda"]})"));
        const Facts::Composed composed = composedOf(at);

        const Manifest::Sidecar *work = sidecarAt(composed, u"work.json"_s);
        QVERIFY(work);
        QVERIFY(!objectOf(work->json).contains(u"authors"_s));
        for (int one = 1; one <= 3; ++one)
            QVERIFY(!composed.rewrites.contains(u"Tome %1.cbz"_s.arg(one)));
    }

    /// Volumes that already say Oda count in the majority that places one read from its
    /// ComicInfo: the series says Oda, the read one writes nothing, the others are not touched.
    void said_volumes_count_in_the_majority_that_places_a_taken_one()
    {
        const QString at = folder(u"SaidAndTaken"_s);
        write(at + u"/work.json"_s, R"({"title":"X","readingDirection":"RIGHT_TO_LEFT"})");
        for (int one = 1; one <= 2; ++one)
            write(at + u"/Tome %1.cbz"_s.arg(one),
                  Zip::volume(R"({"number":)" + QByteArray::number(one) + R"(,"authors":["Oda"]})"));
        write(at + u"/Tome 3.cbz"_s, Zip::volume(R"({"number":3})", comicInfo({{"Writer", "Oda"}})));
        const Facts::Composed composed = composedOf(at);

        const Manifest::Sidecar *work = sidecarAt(composed, u"work.json"_s);
        QVERIFY(work);
        QCOMPARE(objectOf(work->json).value(u"authors"_s).toArray(), QJsonArray{u"Oda"_s});
        QVERIFY(composed.rewrites.contains(u"Tome 3.cbz"_s));
        QVERIFY(writtenAuthors(composed, u"Tome 3.cbz"_s).isEmpty());
        QVERIFY(!composed.rewrites.contains(u"Tome 1.cbz"_s));
        QVERIFY(!composed.rewrites.contains(u"Tome 2.cbz"_s));
    }

    /// A volume nothing was read from used to leave without an `entry.json`, so the library
    /// held an archive no Leaf file described. It receives one, saying the little it is.
    void a_volume_with_nothing_taken_still_receives_its_entry_json()
    {
        const QString at = folder(u"NothingTaken"_s);
        write(at + u"/work.json"_s, R"({"title":"X","readingDirection":"RIGHT_TO_LEFT"})");
        write(at + u"/Hors-série.cbz"_s, Zip::volume({}));
        const Facts::Composed composed = composedOf(at);

        QVERIFY(composed.rewrites.contains(u"Hors-série.cbz"_s));
        QCOMPARE(objectOf(composed.rewrites.value(u"Hors-série.cbz"_s).entryJson)
                     .value(u"leaf"_s).toInt(),
                 1);
    }

    /// Every ComicInfo leaves the archive, the one nothing was taken from and the one that
    /// could not be read included: Leaf reads none of them after the import.
    void every_comicinfo_leaves_even_an_unreadable_one()
    {
        const QString at = folder(u"Leaves"_s);
        write(at + u"/work.json"_s,
              R"({"title":"X","readingDirection":"RIGHT_TO_LEFT","authors":["A"],"artists":["B"]})");
        write(at + u"/Tome 1.cbz"_s,
              Zip::volume(R"({"number":1})", comicInfo({{"Writer", "Autre"}})));
        write(at + u"/Tome 2.cbz"_s, Zip::volume(R"({"number":2})", "<ComicInfo><Writer>"));
        const Facts::Composed composed = composedOf(at);

        for (const QString &file : {u"Tome 1.cbz"_s, u"Tome 2.cbz"_s}) {
            QVERIFY2(composed.rewrites.value(file).remove.contains(u"ComicInfo.xml"_s),
                     qPrintable(file));
            // Nothing else changes: the volume's own `entry.json`, as it was.
            QCOMPARE(objectOf(composed.rewrites.value(file).entryJson).keys(), QStringList{u"number"_s});
        }
    }

    /// A Leaf file that is there and that nothing changed is not written again: the series'
    /// declaration travels as its own bytes, and a volume asks for no rewrite.
    void an_untouched_sidecar_is_not_sent_again()
    {
        const QString at = folder(u"Untouched"_s);
        const QByteArray declaration =
            R"({ "title" : "X", "readingDirection":"RIGHT_TO_LEFT","authors":["A"],"artists":["B"]})";
        write(at + u"/work.json"_s, declaration);
        write(at + u"/Tome 1.cbz"_s, Zip::volume(R"({"number":1})"));
        const Facts::Composed composed = composedOf(at);

        const Manifest::Sidecar *work = sidecarAt(composed, u"work.json"_s);
        QVERIFY(work);
        QCOMPARE(work->json, declaration);
        QVERIFY(!composed.rewrites.contains(u"Tome 1.cbz"_s));
    }

    /// Field by field: a volume naming only its writer takes its drawer from the series.
    void a_volume_naming_only_its_writer_takes_its_drawer_from_the_series()
    {
        const QString at = folder(u"Writer Only"_s);
        write(at + u"/work.json"_s,
              R"({"title":"X","readingDirection":"LEFT_TO_RIGHT","authors":["Goscinny"],"artists":["Morris"]})");
        write(at + u"/Tome 1.cbz"_s, Zip::volume(R"({"number":1,"authors":["Jul"]})"));

        const QList<Facts::Sheet> sheets = sheetsOf(at);
        const Facts::Sheet *volume = sheetAt(sheets, u"Tome 1.cbz"_s);
        QCOMPARE(fieldOf(volume, u"authors"_s)->value.toArray(), QJsonArray{u"Jul"_s});
        QCOMPARE(volume->credits.fallback.artists, QStringList{u"Morris"_s});
        QVERIFY(!Facts::lacks(*volume, *fieldOf(volume, u"artists"_s)));
        QVERIFY(!Facts::blocks(*sheetAt(sheets, u""_s)));
    }

    /// Typed on the series, a name is said and the series goes: the field the reader fills in
    /// on the checking step is the series' own, and it is written into its `work.json`.
    void a_writer_and_a_drawer_typed_on_the_series_release_it()
    {
        const QString at = folder(u"Typed"_s);
        write(at + u"/work.json"_s, R"({"title":"X"})");
        write(at + u"/Tome 1.cbz"_s, Zip::volume(R"({"number":1})"));

        QList<Facts::Sheet> sheets = sheetsOf(at);
        QCOMPARE(Facts::lacking(*sheetAt(sheets, u""_s)), 3);
        QVERIFY(Facts::edit(sheets, u""_s, u"readingDirection"_s, u"RIGHT_TO_LEFT"_s));
        QVERIFY(Facts::edit(sheets, u""_s, u"authors"_s, QJsonArray{u"Jarry"_s}));
        QVERIFY(Facts::blocks(*sheetAt(sheets, u""_s)));
        QVERIFY(Facts::edit(sheets, u""_s, u"artists"_s, QJsonArray{u"Nesmo"_s}));
        QCOMPARE(Facts::lacking(*sheetAt(sheets, u""_s)), 0);
        QVERIFY(!Facts::blocks(*sheetAt(sheets, u""_s)));

        const Facts::Composed composed =
            Facts::compose(sheets, Manifest::of(at, false).sidecars);
        const Manifest::Sidecar *work = sidecarAt(composed, u"work.json"_s);
        QVERIFY(work);
        QCOMPARE(objectOf(work->json).value(u"authors"_s).toArray(), QJsonArray{u"Jarry"_s});
        QCOMPARE(objectOf(work->json).value(u"artists"_s).toArray(), QJsonArray{u"Nesmo"_s});
    }

    /// A Komga volume's writer and penciller are kept when its ComicInfo goes: by the series,
    /// since its one volume is the majority, and not in an `entry.json` saying it again.
    void komga_credits_go_to_the_series_its_volumes_agree_on()
    {
        const QString at = folder(u"Komga Credits"_s);
        write(at + u"/v01.cbz"_s,
              Zip::volume({}, comicInfo({{"Series", "Blame!"}, {"Number", "1"},
                                         {"Writer", "Nihei"}, {"Penciller", "Nihei, Autre"}})));

        const Facts::Composed composed = composedOf(at);
        const Facts::Rewrite rewrite = composed.rewrites.value(u"v01.cbz"_s);
        const QJsonObject entry = objectOf(rewrite.entryJson);
        QVERIFY(!entry.contains(u"authors"_s));
        QVERIFY(!entry.contains(u"artists"_s));
        const QJsonObject work = objectOf(sidecarAt(composed, u"work.json"_s)->json);
        QCOMPARE(work.value(u"authors"_s).toArray(), QJsonArray{u"Nihei"_s});
        QCOMPARE(work.value(u"artists"_s).toArray(), (QJsonArray{u"Nihei"_s, u"Autre"_s}));
        QCOMPARE(rewrite.remove, QStringList{u"ComicInfo.xml"_s});
    }

    /// A book has a drawer as a volume does, and its ComicInfo's `Penciller` is read for it.
    void a_book_carries_its_artists_beside_its_authors()
    {
        const QString at = folder(u"Book Credits"_s);
        write(at + u"/universe.json"_s, R"({"name":"Arran"})");
        write(at + u"/a.cbz"_s,
              Zip::volume({}, comicInfo({{"Title", "T"}, {"Writer", "Istin"}, {"Penciller", "Nesmo"}}),
                          R"({"leaf":1})"));

        const QList<Facts::Sheet> sheets = sheetsOf(at);
        const Facts::Sheet *book = sheetAt(sheets, u"a.cbz"_s);
        QVERIFY(book);
        QStringList keys;
        for (const Facts::Field &field : book->fields)
            keys << field.key;
        QCOMPARE(keys.mid(0, 3), (QStringList{u"title"_s, u"authors"_s, u"artists"_s}));
        QCOMPARE(fieldOf(book, u"artists"_s)->value.toArray(), QJsonArray{u"Nesmo"_s});
    }

    /// The tags of a series are only ever what its `work.json` says: nothing in a ComicInfo
    /// names them, so there is nothing to take and nothing to guess. Edited, they are a list,
    /// and what the file held besides stays.
    void tags_said_in_work_json_are_kept_and_edited_as_a_list()
    {
        const QString said = folder(u"Tagged"_s);
        write(said + u"/work.json"_s,
              "{\"title\":\"X\",\"tags\":[\"Terres d'Arran\"],\"readingDirection\":\"LEFT_TO_RIGHT\"}");
        write(said + u"/Tome 1.cbz"_s, Zip::volume(R"({"number":1})"));
        const QList<Facts::Sheet> saidSheets = sheetsOf(said);
        const Facts::Field *tags = fieldOf(sheetAt(saidSheets, u""_s), u"tags"_s);
        QVERIFY(tags);
        QCOMPARE(tags->origin, Facts::Origin::Said);
        QCOMPARE(tags->weight, Facts::Weight::Optional);
        QCOMPARE(tags->value.toArray(), QJsonArray{u"Terres d'Arran"_s});

        const QString at = folder(u"Untagged"_s);
        write(at + u"/work.json"_s,
              R"({"title":"X","myField":"kept","readingDirection":"LEFT_TO_RIGHT"})");
        write(at + u"/Tome 1.cbz"_s, Zip::volume(R"({"number":1})"));
        QList<Facts::Sheet> sheets = sheetsOf(at);
        QCOMPARE(fieldOf(sheetAt(sheets, u""_s), u"tags"_s)->origin, Facts::Origin::Missing);
        QVERIFY(Facts::edit(sheets, u""_s, u"tags"_s, QJsonArray{u"Terres d'Arran"_s}));

        const Facts::Composed composed = Facts::compose(sheets, Manifest::of(at, false).sidecars);
        const QJsonObject object = objectOf(sidecarAt(composed, u"work.json"_s)->json);
        QCOMPARE(object.value(u"tags"_s).toArray(), QJsonArray{u"Terres d'Arran"_s});
        QCOMPARE(object.value(u"myField"_s).toString(), u"kept"_s);
        QCOMPARE(object.value(u"title"_s).toString(), u"X"_s);

        QStringList keys;
        for (const Facts::Field &field : sheetAt(sheets, u""_s)->fields)
            keys << field.key;
        QVERIFY(keys.indexOf(u"tags"_s) == keys.indexOf(u"genres"_s) + 1);
    }

    // ——— Les ordres de lecture ———————————————————————————————————————————————————————

    /// A universe that already speaks Leaf has orders to show and nothing to ask: they are said,
    /// never guessed, and a file nobody changed leaves with its own bytes — spacing, key order
    /// and the keys no version of Leaf knows included.
    void a_universe_json_with_orders_leaves_as_it_came_when_nothing_changes()
    {
        const QString at = folder(u"Arran Ordres"_s);
        const QByteArray bytes =
            "{ \"name\" : \"Terres d’Arran\",\n  \"myKey\" : 3,\n"
            "  \"defaultOrder\" : \"tout\",\n"
            "  \"orders\" : [ { \"id\" : \"tout\", \"note\" : \"x\", \"steps\" : "
            "[ { \"work\" : \"Elfes\", \"myStep\" : 1 } ] } ] }";
        write(at + u"/universe.json"_s, bytes);
        write(at + u"/Elfes/work.json"_s,
              R"({"title":"Elfes","readingDirection":"LEFT_TO_RIGHT","authors":["Istin"],"artists":["Nico"]})");

        const QList<Facts::Sheet> sheets = sheetsOf(at);
        const Facts::Sheet *universe = sheetAt(sheets, u""_s);
        const Facts::Field *orders = fieldOf(universe, u"orders"_s);
        QVERIFY(orders);
        QCOMPARE(orders->origin, Facts::Origin::Said);
        QCOMPARE(orders->weight, Facts::Weight::Optional);
        QCOMPARE(orders->from, u"universe.json"_s);
        QCOMPARE(orders->value.toArray().size(), 1);
        QCOMPARE(fieldOf(universe, u"defaultOrder"_s)->value.toString(), u"tout"_s);
        QVERIFY(!Facts::needsReview(*universe));

        const Facts::Composed composed = composedOf(at);
        QCOMPARE(sidecarAt(composed, u"universe.json"_s)->json, bytes);
    }

    /// Orders nobody wrote are not made up: a universe without any shows the field missing, and
    /// that asks nothing.
    void a_universe_without_orders_has_none_and_asks_nothing()
    {
        const QString at = folder(u"Arran Sans Ordre"_s);
        write(at + u"/universe.json"_s, R"({"name":"Arran"})");
        write(at + u"/Elfes/work.json"_s,
              R"({"title":"Elfes","readingDirection":"LEFT_TO_RIGHT","authors":["Istin"],"artists":["Nico"]})");

        const QList<Facts::Sheet> sheets = sheetsOf(at);
        const Facts::Sheet *universe = sheetAt(sheets, u""_s);
        QCOMPARE(fieldOf(universe, u"orders"_s)->origin, Facts::Origin::Missing);
        QCOMPARE(fieldOf(universe, u"defaultOrder"_s)->origin, Facts::Origin::Missing);
        QVERIFY(!Facts::needsReview(*universe));
        QVERIFY(!Facts::blocks(*universe));
    }

    /// The orders an editor hands back replace the array and nothing else of the file.
    void editing_the_orders_keeps_the_universe_s_other_keys()
    {
        const QString at = folder(u"Arran Edite"_s);
        write(at + u"/universe.json"_s, R"({"name":"Arran","myKey":"kept"})");
        write(at + u"/Elfes/work.json"_s,
              R"({"title":"Elfes","readingDirection":"LEFT_TO_RIGHT","authors":["Istin"],"artists":["Nico"]})");

        QList<Facts::Sheet> sheets = sheetsOf(at);
        const QJsonArray orders{QJsonObject{
            {u"id"_s, u"tout"_s},
            {u"steps"_s, QJsonArray{QJsonObject{{u"work"_s, u"Elfes"_s}}}}}};
        QVERIFY(Facts::edit(sheets, u""_s, u"orders"_s, orders));
        QVERIFY(Facts::edit(sheets, u""_s, u"defaultOrder"_s, u"tout"_s));

        const Facts::Composed composed = Facts::compose(sheets, Manifest::of(at, false).sidecars);
        const QJsonObject object = objectOf(sidecarAt(composed, u"universe.json"_s)->json);
        QCOMPARE(object.value(u"orders"_s).toArray(), orders);
        QCOMPARE(object.value(u"defaultOrder"_s).toString(), u"tout"_s);
        QCOMPARE(object.value(u"myKey"_s).toString(), u"kept"_s);
        QCOMPARE(object.value(u"name"_s).toString(), u"Arran"_s);
    }

    /// The announcement answers 400 to a universe whose inclusions name nothing, carry a range
    /// or close a loop: said on the sheet, and holding the card back, before anything is sent.
    void an_inclusion_the_announcement_would_refuse_blocks_the_universe_and_says_why()
    {
        const QString at = folder(u"Arran Boucle"_s);
        write(at + u"/universe.json"_s,
              R"({"name":"Arran","orders":[)"
              R"({"id":"a","steps":[{"include":"b"}]},)"
              R"({"id":"b","steps":[{"include":"a"}]}]})");
        write(at + u"/Elfes/work.json"_s,
              R"({"title":"Elfes","readingDirection":"LEFT_TO_RIGHT","authors":["Istin"],"artists":["Nico"]})");

        QList<Facts::Sheet> sheets = sheetsOf(at);
        const Facts::Sheet *universe = sheetAt(sheets, u""_s);
        QVERIFY(Facts::needsReview(*universe));
        QVERIFY(Facts::blocks(*universe));
        QCOMPARE(fieldOf(universe, u"orders"_s)->doubt,
                 Words::orderRefused(u"a"_s, 1, Orders::Refusal::ClosesLoop, u"b"_s));

        // Corrected, the doubt goes with the loop.
        QVERIFY(Facts::edit(sheets, u""_s, u"orders"_s,
                            QJsonArray{QJsonObject{{u"id"_s, u"a"_s}}}));
        universe = sheetAt(sheets, u""_s);
        QVERIFY(!Facts::blocks(*universe));
        QVERIFY(fieldOf(universe, u"orders"_s)->doubt.isEmpty());
    }

    /// An inclusion that names no order of the file is refused as well, by what it named.
    void an_inclusion_naming_no_order_is_refused_by_its_name()
    {
        const QString at = folder(u"Arran Nulle Part"_s);
        write(at + u"/universe.json"_s,
              R"({"name":"Arran","orders":[{"id":"a","steps":[{"work":"E"},{"include":"nowhere"}]}]})");
        write(at + u"/Elfes/work.json"_s,
              R"({"title":"Elfes","readingDirection":"LEFT_TO_RIGHT","authors":["Istin"],"artists":["Nico"]})");

        const QList<Facts::Sheet> sheets = sheetsOf(at);
        const Facts::Sheet *universe = sheetAt(sheets, u""_s);
        QVERIFY(Facts::blocks(*universe));
        QCOMPARE(fieldOf(universe, u"orders"_s)->doubt,
                 Words::orderRefused(u"a"_s, 2, Orders::Refusal::NamesNothing, u"nowhere"_s));
    }

    /// Past what the server reads is a reason to look and not a refusal: the scan keeps the
    /// rest and says so, so the card is not held back for it.
    void orders_past_the_ceiling_are_to_review_and_block_nothing()
    {
        const QString at = folder(u"Arran Trop"_s);
        QJsonArray orders;
        for (int rank = 0; rank < 14; ++rank) {
            const QString next = u"o"_s + QString::number(rank + 1);
            const QJsonObject include{{u"include"_s, next}};
            orders << QJsonObject{
                {u"id"_s, u"o"_s + QString::number(rank)},
                {u"steps"_s, rank == 13 ? QJsonArray{QJsonObject{{u"work"_s, u"X"_s}}}
                                        : QJsonArray{include, include}}};
        }
        write(at + u"/universe.json"_s,
              QJsonDocument(QJsonObject{{u"name"_s, u"Arran"_s}, {u"orders"_s, orders}}).toJson());
        write(at + u"/Elfes/work.json"_s,
              R"({"title":"Elfes","readingDirection":"LEFT_TO_RIGHT","authors":["Istin"],"artists":["Nico"]})");

        const QList<Facts::Sheet> sheets = sheetsOf(at);
        const Facts::Sheet *universe = sheetAt(sheets, u""_s);
        QVERIFY(Facts::needsReview(*universe));
        QVERIFY(!Facts::blocks(*universe));
        QCOMPARE(fieldOf(universe, u"orders"_s)->doubt, Words::ordersPastTheCeiling());
    }

    /// A series the scan already reads by its files gains nothing from its roles: the sheets
    /// and what they compose are those of before, with roles or without.
    void without_roles_the_sheets_are_what_they_were()
    {
        const QString complete = folder(u"Blame Roles"_s);
        write(complete + u"/work.json"_s,
              R"({"title":"Blame!","readingDirection":"RIGHT_TO_LEFT","authors":["Tsutomu Nihei"],"artists":["Tsutomu Nihei"]})");
        write(complete + u"/Tome 1.cbz"_s, Zip::volume(R"({"number":1})"));
        const QString komga = folder(u"Komga Roles"_s);
        for (const int number : {1, 2})
            write(komga + u"/v0"_s + QString::number(number) + u".cbz"_s,
                  Zip::volume({}, comicInfo({{"Series", "Blame!"},
                                             {"Number", QByteArray::number(number)}})));

        for (const QString &at : {complete, komga}) {
            const QList<Manifest::Node> cards = Manifest::found(at);
            QCOMPARE(cards.size(), 1);
            const Manifest::Folder tree = Manifest::of(at, false);
            const QList<Facts::Sheet> before = Facts::read(cards.first(), tree);
            Shape::Roles evident;
            evidentRoles(cards.first(), evident);
            sameSheets(before, Facts::read(cards.first(), tree, evident));
            sameSheets(before, shapedSheetsOf(at));
            sameComposed(Facts::compose(before, tree.sidecars),
                         Facts::compose(shapedSheetsOf(at), tree.sidecars));
        }
    }

    /// A source that says where each of its chapters starts gives the chapter aimed at the
    /// pages from its start to the page before the next one's, and the last one runs to the
    /// last page.
    void the_source_range_is_the_one_the_source_declares_for_that_chapter()
    {
        Facts::Sheet source;
        source.contents.pages = 200;
        source.contents.starts = {{5, 0}, {6, 62}, {7, 124}};

        const Facts::ProposedSplice middle = Facts::proposedSplice(source, 6.0, 10, 20);
        QCOMPARE(middle.from, 62);
        QCOMPARE(middle.to, 123);
        QVERIFY(!middle.guessed);

        const Facts::ProposedSplice last = Facts::proposedSplice(source, 7.0, 10, 20);
        QCOMPARE(last.from, 124);
        QCOMPARE(last.to, 199);
        QVERIFY(!last.guessed);
    }

    /// A chapter the source does not place — absent from its list, without a first page, or
    /// without a number to look for — is proposed at the pages it has now, and said to be a
    /// guess: nothing in the file says where it is.
    void a_chapter_missing_from_the_source_is_proposed_at_the_same_pages_and_guessed()
    {
        Facts::Sheet source;
        source.contents.pages = 200;
        source.contents.starts = {{5, 0}, {6, std::nullopt}};

        for (const std::optional<double> chapter :
             {std::optional<double>(9.0), std::optional<double>(6.0), std::optional<double>()}) {
            const Facts::ProposedSplice proposal = Facts::proposedSplice(source, chapter, 62, 120);
            QCOMPARE(proposal.from, 62);
            QCOMPARE(proposal.to, 120);
            QVERIFY(proposal.guessed);
        }
    }
};

QTEST_GUILESS_MAIN(KnowsEachField)
#include "knows_each_field.moc"
