#pragma once

// Where every value of an import comes from.
//
// The checking step shows, for each universe, series, edition and file of a dropped folder,
// what it is called and who said so. Four origins, and they are the whole point:
//
//   · **Said** — written in a Leaf sidecar (`universe.json`, `work.json`, `edition.json`, the
//     archive's `entry.json`). Nothing to ask.
//   · **Taken** — read from a `ComicInfo.xml`. Nothing to ask either: the server converts it.
//   · **Guessed** — deduced from a file or folder name. A confirmation.
//   · **Missing** — nobody said it. A value, or nothing when the field is facultative.
//
// Pure on purpose: no network and no window, folders and archives in, sheets out. The
// screen, the composition of the sidecars that leave, and the announcement are elsewhere;
// they all read what this decided, so this is the one place an origin is decided.
//
// The order a source wins in is the server's (`scan/scanner.rs`): `entry.json` is
// authoritative and ComicInfo only fills its holes, field by field. One exception the
// server makes too: a chapter file's number comes from its name and never from ComicInfo,
// which pins a volume number on it for want of any way to say otherwise — « Chapitre
// 686.5 » declares `<Number>75</Number>` in the real library.

#include "Manifest.h"
#include "Shape.h"

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <optional>

namespace Facts {

/// Dit, Repris, Deviné, Manquant.
enum class Origin { Said, Taken, Guessed, Missing };

/// What a missing value costs: `Required` holds its series back — it is shown missing, stops
/// the step and blocks « Suivant » until it is said (`lacks`). Those are the fields a library
/// cannot do without or reads wrongly without: a name, a number, the reading direction (a
/// series read the wrong way round is not noticed until page forty), and who wrote and drew
/// it. `Optional` is shown missing and stops nothing — otherwise no real folder would ever
/// pass.
enum class Weight { Required, Optional };

struct Field {
    /// The JSON key it is written under: "title", "number"…
    QString key;
    /// Null when Missing.
    QJsonValue value;
    Origin origin = Origin::Missing;
    Weight weight = Weight::Optional;
    /// Where it was read: "work.json", "ComicInfo.xml", the file's or the folder's name.
    QString from;
    /// A sentence from `Words`, empty when nothing is in doubt.
    QString doubt;
    bool edited = false;
    /// On an edition, what its series' `work.json` says of this field: what the server reads
    /// while the edition says nothing, kept apart so that emptying the field gives it back.
    /// Without it an emptied reading direction was « Manquant » and held the import back.
    QJsonValue fromWork;
};

/// Who a series is credited with: the people its volumes name, once each.
struct Credited {
    QStringList authors;
    QStringList artists;
};

struct Sheet {
    Manifest::Level level = Manifest::Level::Work;
    /// A container's `at`, a file's path — both relative to the card.
    QString at;
    QString name;
    /// The sidecar it writes: an existing one's path kept as it is (case included),
    /// "work.json" / "…/edition.json" otherwise; the archive's own path for a file.
    QString sidecar;
    /// In the order the sheet shows them.
    QList<Field> fields;
    /// An archive or a sidecar that could not be read, as a sentence from `Words`. A sheet
    /// carrying one has nothing editable: there is nothing to write into a file nobody could
    /// open, and a sidecar that is not a JSON object is never recomposed.
    QString trouble;
    /// What became of the archive's ComicInfo.xml, three answers that only mean anything
    /// together: a ComicInfo unread is kept, one read is converted, one unreadable is said.
    struct ComicInfoFile {
        /// The archive holds one, and whether something was read out of it. Unread, it stays
        /// in the archive: nothing is removed that was not read.
        bool carried = false;
        bool read = false;
        /// And whether it could not be read at all — not one that opens and says nothing worth
        /// taking, which is no fault of anybody's and was announced « illisible » all the same.
        bool unreadable = false;
    };
    ComicInfoFile comicInfo;
    /// A file's own `entry.json`, parsed: what a rewrite completes rather than replaces.
    QJsonObject own;
    /// Whether the file carries one at all. A volume that carries none receives one, even
    /// saying nothing: the library then holds no archive no Leaf file describes.
    bool hasOwn = false;
    /// What the sheet is in the model, and who said so. A folder's comes from the roles
    /// `read` was given, its level otherwise; an archive's from its `oneshot.json`, its place
    /// and the roles, in that order — the archive is the one thing the shape never opened.
    Shape::Role role = Shape::Role::Work;
    Shape::Origin roleOrigin = Shape::Origin::Evident;
    /// The archive's `oneshot.json`, parsed: what a book's rewrite completes. Empty when it
    /// carries none.
    QJsonObject oneShot;
    /// It carries one that is not a JSON object, holds a key of a type the server's reading
    /// refuses, or could not be read out of it. Read as
    /// the server reads it, no book, and never written over: « Livre seul » is greyed on it.
    bool oneShotUnreadable = false;
    /// On a series: the `medium` its `work.json` says, empty when it says none — what the words
    /// under its credits count its volumes in, albums or tomes.
    QString medium;
    /// Who the sheet is credited with, as the volumes, the series and ComicInfo each say it,
    /// and what is still missing of it: weighed together (`weighCredits`), each one is read
    /// only beside the others.
    struct Credits {
        /// On a series' sheet: the union of who its volumes are credited with, each its own names
        /// or else the series' default (`creditedWith`; books left out, since a book lifted out of
        /// the folder is nobody's volume), in the server's file order (`readBefore`), one entry per
        /// person by `Fold::searchKey`, under the spelling met first. What the server records for
        /// the series. Follows the volumes' edits.
        Credited credited;
        /// On a volume or a chapter: its nearest series' `authors` and `artists` as they stand,
        /// typed ones included — who it is credited with when it names nobody. Empty elsewhere.
        Credited fallback;
        /// On a volume or a chapter: what its ComicInfo names, kept aside. ComicInfo only fills a
        /// hole, so whether it fills this one depends on its series, which is weighed after it.
        Credited comic;
        /// On a series: how many of its volumes are credited with no writer, and with no drawer,
        /// once the series has had its say. What `lacks` and `lackingFor` read; zero elsewhere.
        int authorsLacking = 0;
        int artistsLacking = 0;
    };
    Credits credits;
    /// Where each of an archive's chapters says it starts (counted from 0), when it says.
    struct ChapterStart {
        double number = 0;
        std::optional<int> page;
    };
    /// What a reimport compares an archive by, read together and only ever read together.
    struct Contents {
        /// Its image count; -1 where it was not read.
        int pages = -1;
        /// The series it names: `work` in its `entry.json`, `Series` in its ComicInfo.
        QString series;
        /// The numbers of the chapters its `entry.json` lists.
        QList<double> chapters;
        QList<ChapterStart> starts;
    };
    Contents contents;
};

/// The sheets of one card, in walk order: each container, then what it holds — sub-folders
/// first, then its files, the order `Manifest` walks in. `card` is rebased so that its own
/// `at` is "", and `tree` is `Manifest::of` on the same folder: the paths of one are the
/// paths of the other.
///
/// `roles` are `Shape::rolesOf` the same card. Without them, every folder is what its level
/// says and every archive what its place and its `oneshot.json` say — the sheets of a series
/// the scan already reads by its files are the same either way. A folder whose role is Shelf
/// has no sheet: nothing in it is read, and nothing is written for it.
QList<Sheet> read(const Manifest::Node &card, const Manifest::Folder &tree,
                  const Shape::Roles &roles = {});

/// The pages of `source` that replace chapter `chapter` of a volume: the ones its own
/// `entry.json` gives that chapter, from where it starts to the page before the next one that
/// says where it starts, or to its last. A chapter the source does not place is proposed at
/// the pages it has now, `from` to `to`, and said to be guessed.
struct ProposedSplice {
    int from = 0;
    int to = 0;
    bool guessed = false;
};
ProposedSplice proposedSplice(const Sheet &source, std::optional<double> chapter, int from, int to);

/// Puts `value` in the field `key` of the sheet at `at`: the value replaces, the field is
/// Said and edited; an empty value leaves it Missing. The duplicate numbers of the whole
/// card are then weighed again. False when `at` or `key` names nothing editable, and when
/// the field already holds that value — a guess excepted, which given again is said.
bool edit(QList<Sheet> &sheets, const QString &at, const QString &key, const QJsonValue &value);

/// Whether the checking step stops on this sheet: a guess, a required field it lacks,
/// a doubt, something unreadable, a role read from the shape, or an archive nobody will read.
bool needsReview(const Sheet &sheet);

/// Who a volume or a chapter is credited with for `key` (`authors`, `artists`): its own
/// names, else its series' default (`Sheet::Credits::fallback`) — what the server records, and what a
/// series' union is made of. Without the default, a series of Morris whose one volume said
/// « Jul » was credited with Jul alone.
QStringList creditedWith(const Sheet &volume, const QString &key);

/// Whether `field` of `sheet` is a required one nobody has said. A volume's `authors` and
/// `artists` are said by its series when it names nobody (`Sheet::Credits::fallback`); a series lacks
/// its own exactly when one of its volumes lacks them, which is where one name typed puts
/// every volume right.
bool lacks(const Sheet &sheet, const Field &field);

/// How many fields of the sheet it `lacks` — what the footer counts. A volume's credits are
/// counted on its series, once, and not once per volume: forty-one volumes drawn by nobody are
/// one value to type.
int lacking(const Sheet &sheet);

/// How many volumes of `series` lack `key` (`authors` or `artists`).
int lackingFor(const Sheet &series, const QString &key);

/// Whether this sheet holds its series back: a required field it lacks, a number another
/// file of its edition also claims, or a folder whose sidecar could not be read.
bool blocks(const Sheet &sheet);

/// What an archive is rewritten into: the `entry.json` to put in it, the `oneshot.json` to
/// put beside it (empty for none, and then the archive keeps the one it carries) and the
/// members to drop.
struct Rewrite {
    QByteArray entryJson;
    QStringList remove;
    QByteArray oneShotJson;
};

/// What the card announces: every sidecar that leaves, in walk order, and the rewrite of
/// each archive that asks for one, by its path.
/// What a file will be once installed, as the announcement says it: the number, kind and
/// title **as composed**, corrections included, and not as the archive holds them.
struct Found {
    std::optional<double> number;
    /// The number was read off the file's name and nobody said or confirmed it: the server
    /// shows such a file and never lands it.
    bool guessed = false;
    /// VOLUME or CHAPTER.
    QString kind;
    QString title;
    QString series;
    int pages = -1;
    QList<double> chapters;
};

struct Composed {
    QList<Manifest::Sidecar> sidecars;
    QHash<QString, Rewrite> rewrites;
    /// The archives no one will read, by their path: never rewritten, never announced.
    QSet<QString> leftOut;
    /// For each archive that travels, by its path.
    QHash<QString, Found> found;
};

/// The final content of a card, by one rule for `work.json`, `edition.json` and `entry.json`:
/// there and nothing changed, it is not written again — a declaration travels as its own
/// bytes, which the server reads to know what a folder is; there and something changed, it
/// is the original object, every key it had, completed with what was taken, guessed or
/// edited — a said field is never rewritten, an emptied one is removed, a missing one never
/// written; not there, it is created, always. A sidecar that was not readable travels as it
/// is. Every archive carrying a ComicInfo asks for it to go, read or not: Leaf reads none
/// after the import. A series that names nobody is given the credits the strict majority of
/// its volumes carries, and those volumes leave them to it. The client never writes an
/// archive: this is what it asks.
///
/// A folder whose role was guessed or chosen writes its sidecar even when its sheet adds
/// nothing: the scan reads a role from a file and never from a shape, so the file is the
/// confirmation. A book asks for a rewrite when a field adds something or when it has no
/// `oneshot.json` yet; its `oneShotJson` is the one it carried, or `{"leaf":1}`, completed,
/// and its `entryJson` the archive's own with the same content, recompacted. An archive no
/// one will read is in `leftOut` and asks for nothing.
Composed compose(const QList<Sheet> &sheets, const QList<Manifest::Sidecar> &said);

/// Whether the scan reads the volume at `a` before the one at `b`, both under the series at
/// `series`: the archives beside its folder first, then each edition folder, every name
/// compared byte by byte as the disk holds it — « Tome 10 » before « Tome 2 ». The order the
/// server meets credits in, and so the spelling it keeps of a person named two ways; the
/// client's own walk, sub-folders first and numbers in order, kept the other.
bool readBefore(const QString &series, const QString &a, const QString &b);

} // namespace Facts
