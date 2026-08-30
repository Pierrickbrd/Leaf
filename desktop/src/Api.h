#pragma once

// What the server answers, as C++ rather than as free-form JSON.
//
// The contract in `contract/openapi.yaml` marks some fields required and the rest optional,
// and that distinction is worth keeping all the way here. A required field that is absent is
// not a series with a blank name — it is a server that broke its own contract, and saying so
// once beats every screen discovering it separately.
//
// **Structure is strict, vocabulary is not.** A missing `name` refuses the item and names the
// field. An unfamiliar `medium` becomes `Medium::Other` and the item stands: the server may
// learn a new word before this client does, and a shelf that emptied itself over one would be
// worse than a shelf with one odd tile.
//
// Nothing here knows how anything is worded or drawn. `Holding::ownedVolumes` is a number;
// whether it reads "21 tomes" or "7 albums" is the shelf's business.

#include <QJsonObject>
#include <QList>
#include <QString>

#include <optional>

namespace Api {

enum class Medium { Manga, Bd, Comics, Manhwa, Manhua, Webtoon, Artbook, Other };
enum class ReadingDirection { LeftToRight, RightToLeft, Vertical };
enum class Run { Ongoing, Completed };
enum class ReadStatus { Unread, InProgress, Read };

/// What this library holds of an edition, as opposed to what the edition is. The same
/// printing on somebody else's shelf carries the same name, the same author and the same
/// volume count, and none of these: they are facts about a collection, not about a work.
struct Holding {
    ReadStatus readStatus = ReadStatus::Unread;
    int ownedVolumes = 0;
    QList<double> missingVolumes;
    QList<double> missingChapters;
    std::optional<qint64> addedAt;
    std::optional<qint64> lastAddedAt;
};

/// A row of the shelf. "Series" is the API's word; in the model it is an EDITION.
struct Series {
    // Required by the contract.
    QString id;
    QString workId;
    QString name;
    QString work;
    int entryCount = 0;
    int chapterCount = 0;
    int arcCount = 0;

    // Everything the contract allows to be absent. `std::optional` rather than an empty
    // string, because "no author recorded" and "author recorded as nothing" are different
    // facts and only one of them is worth showing.
    std::optional<QString> universe;
    std::optional<QString> edition;
    std::optional<QString> author;
    std::optional<QString> publisher;
    std::optional<QString> language;
    std::optional<Medium> medium;
    std::optional<ReadingDirection> readingDirection;
    std::optional<Run> run;
    std::optional<int> declaredVolumes;
    QList<QString> genres;

    Holding holding;
};

/// One page of the shelf. `total` is the count behind the current filters, not the library.
struct Page {
    QList<Series> items;
    int total = 0;
    int page = 0;
    int size = 0;
};

/// A value you own, and how much of it. The count is the point: the panel exists to show
/// what you have, not to offer a checkbox for everything imaginable.
struct Facet {
    QString value;
    int count = 0;
};

struct Facets {
    QList<Facet> readStatuses;
    QList<Facet> universes;
    QList<Facet> authors;
    QList<Facet> genres;
    QList<Facet> media;
    QList<Facet> statuses;
    QList<Facet> languages;
    QList<Facet> publishers;
};

/// A card of the resume band. `reason` separates "you are inside this one" from "you finished
/// the last one and here is the next": the band words them differently.
///
/// The band reads "Tome 12 · Page 47/190 · Chapitre 98", and every one of those three comes
/// from a different place — the entry, the progress, and the chapter the page falls in. The
/// chapter's wording is `label`, which the server has already settled: it knows the markers,
/// and a client inventing "Chapitre" from a number would disagree with the reader screen.
struct UpNext {
    enum class Reason { InProgress, NextUp };
    enum class Kind { Volume, Chapter };

    QString seriesId;
    QString seriesName;
    QString entryId;
    Kind entryKind = Kind::Volume;
    std::optional<double> entryNumber;
    std::optional<QString> entryTitle;
    int pageCount = 0;

    /// Absent when nothing is started — which is exactly `reason == NextUp`.
    std::optional<int> page;
    std::optional<QString> chapterLabel;

    Reason reason = Reason::NextUp;
};

/// What a parse produced, or what stopped it.
///
/// A `QString` and not a bool: "series[3]: name is missing" is something a person can act on,
/// and it is the only thing that will ever be shown about a broken answer.
template <typename T>
struct Read {
    std::optional<T> value;
    QString trouble;

    bool ok() const { return value.has_value(); }
};

Read<Series> series(const QJsonObject &from);
Read<Page> page(const QJsonObject &from);
Read<Facets> facets(const QJsonObject &from);
Read<UpNext> upNext(const QJsonObject &from);

/// The contract's spellings, so that a test can walk them rather than trust a switch.
Medium medium(const QString &word);
QString spell(Medium value);
ReadStatus readStatus(const QString &word);
QString spell(ReadStatus value);

} // namespace Api
