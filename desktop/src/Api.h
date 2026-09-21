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

#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <optional>

namespace Api {

enum class Medium { Manga, Bd, Comics, Manhwa, Manhua, Webtoon, Artbook, Other };
enum class ReadingDirection { LeftToRight, RightToLeft, Vertical };
enum class Run { Ongoing, Completed };
enum class ReadStatus { Unread, InProgress, Read };
/// The four the bar offers, in the order it offers them. `Name` is the contract's default and
/// the fallback it applies to a word it does not know, so it is first here too.
enum class Sort { Name, Added, Volumes, Read };

/// What this library holds of an edition, as opposed to what the edition is. The same
/// printing on somebody else's shelf carries the same name, the same author and the same
/// volume count, and none of these: they are facts about a collection, not about a work.
struct Holding {
    /// Absent when the server used a word this client has not been taught. UNREAD is one of
    /// the three answers, not the answer to give when there is none — see the note above on
    /// vocabulary.
    std::optional<ReadStatus> readStatus;
    int ownedVolumes = 0;
    QList<double> missingVolumes;
    QList<double> missingChapters;
    std::optional<qint64> addedAt;
    std::optional<qint64> lastAddedAt;
};

/// Who made the work — not how much of it this library holds, which is `Holding`'s concern.
/// `author` is the contract's original singular; `authors` and `artists` replaced it once a
/// series needed several writers or a separate illustrator named. Grouping the old word with
/// its replacements, instead of leaving the three scattered through `Series`'s field list,
/// makes the relationship between them — one screen still reading the retired word — visible
/// where before it was only implicit in three names that happened to sit near each other.
struct Credits {
    /// The former singular, kept for a shelf that has not been taught `authors` yet. A new
    /// screen reads `authors`; this stays for one that has not been rebuilt.
    std::optional<QString> author;
    /// The writers. Several populate — *Les Terres d'Arran* carries five.
    QList<QString> authors;
    /// The illustrators — penciller, inker and cover artist in one.
    QList<QString> artists;
};

/// How large a series is, counted three ways the shelf shows separately: the entries
/// (volumes or one-shots), the chapters inside them, and the arcs that group chapters. Three
/// answers to one question — how much is there — grouped for the same reason as `Credits`,
/// so they read as one fact instead of three numbers that happen to be declared in sequence.
struct Counts {
    int entries = 0;
    int chapters = 0;
    int arcs = 0;
};

/// A row of the shelf. "Series" is the API's word; in the model it is an EDITION.
struct Series {
    // Required by the contract.
    QString id;
    QString workId;
    QString name;
    QString work;
    Counts counts;

    // Everything the contract allows to be absent. `std::optional` rather than an empty
    // string, because "no author recorded" and "author recorded as nothing" are different
    // facts and only one of them is worth showing.
    std::optional<QString> universe;
    std::optional<QString> edition;
    Credits credits;
    std::optional<QString> publisher;
    /// The publisher's imprint, a sibling of `publisher` rather than a replacement for it.
    std::optional<QString> collection;
    std::optional<QString> language;
    std::optional<Medium> medium;
    std::optional<ReadingDirection> readingDirection;
    std::optional<Run> run;
    std::optional<int> declaredVolumes;
    QList<QString> genres;
    /// Beside `genres`, never folded into them.
    QList<QString> tags;
    /// A free string, never an enum: "16+" at Kana, "T" elsewhere.
    std::optional<QString> ageRating;
    /// Positive form, never `blackAndWhite`.
    std::optional<bool> colour;

    Holding holding;
};

/// One page of the shelf. `total` is the count behind the current filters, not the library.
struct Page {
    QList<Series> items;
    int total = 0;
    int page = 0;
    int size = 0;
};

/// What the server is, and whether it is up.
///
/// Read rather than assumed: a client that guesses its server's version is a client that
/// will one day read a field the other end stopped sending.
struct Health {
    QString status;
    int api = 0;
    int format = 0;
    /// How many series it holds. The one number that says the library is really there — a
    /// server answering perfectly over an unmounted disk answers zero.
    int library = 0;
    bool localDrop = false;
};

/// Where the scan is, and what the last one found.
///
/// `summary` is prose the server wrote, shown as it stands. The scanner reports rather than
/// guesses, and until now nothing could read what it reported.
struct ScanCounts {
    int universes = 0;
    int works = 0;
    int editions = 0;
    int entries = 0;
    int chapters = 0;
    int pages = 0;
    /// What was opened and read rather than skipped as unchanged. The only count that
    /// describes the work done; the others describe the library.
    int reanalysed = 0;
};

/// One kind of thing a scan found, and the first of them.
struct Finding {
    /// The contract's word — ERRORS, DISREGARDED… The client has a French sentence for
    /// each, and shows an unfamiliar one by its items alone rather than dropping it.
    QString kind;
    /// How many there are, which is not how many are listed.
    int total = 0;
    QList<QString> items;
};

struct ScanFindings {
    ScanCounts counts;
    QList<Finding> findings;
    int chaptersWithoutStartPage = 0;
    /// Set when the scan itself failed, and empty otherwise.
    QString failure;
};

struct ScanStatus {
    enum class State { Idle, Running, Done, Other };

    State state = State::Other;
    std::optional<qint64> startedAt;
    std::optional<qint64> finishedAt;
    std::optional<ScanFindings> report;
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

/// One thing the search found, ready to be opened without asking again.
///
/// `label` is what the server decided to show — « Assassinat », « Tome 1 » — and the client
/// does not rebuild it: the server knows whether a loose chapter carries a title and this does
/// not. `seriesName` is what it belongs to, which is how a file says where it comes from.
///
/// An `approximate` hit is a guess and has to reach the screen looking like one: shown like an
/// exact match it costs more trust than finding nothing.
struct Hit {
    /// The three the contract lists, and `Other` for a fourth it might learn — the item still
    /// stands, the way an unfamiliar medium does, and the screen is free not to draw what it
    /// cannot place.
    enum class Kind { Edition, Entry, Chapter, Other };

    Kind kind = Kind::Other;
    QString id;
    QString label;
    std::optional<QString> seriesId;
    std::optional<QString> seriesName;
    std::optional<QString> entryId;
    /// The file the hit lives in, when it lives in one — a chapter inside a volume needs both
    /// levels on screen, « Chapitre 98 » alone not saying what opening it opens. The same
    /// vocabulary as `UpNext::Kind`, because it is the same question asked of the same rows.
    std::optional<UpNext::Kind> entryKind;
    std::optional<double> entryNumber;
    std::optional<QString> entryTitle;
    std::optional<int> entryPageCount;
    std::optional<double> chapterNumber;
    std::optional<QString> chapterTitle;
    bool approximate = false;
};

/// A page of hits, and the two counts a heading needs: how many there are in all, and how
/// many of those are files. Asked for by naming a page; a server that has never heard of one
/// answers the bare list it always has, which is not a fault — the counts are then simply
/// what the page in hand holds, and a heading saying « Fichiers · 9 » about nine lines is
/// telling the truth about everything it knows.
struct Hits {
    QList<Hit> items;
    int total = 0;
    int fileTotal = 0;
    int page = 0;
    int size = 0;
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
Read<Health> health(const QJsonObject &from);
Read<ScanStatus> scanStatus(const QJsonObject &from);
Read<UpNext> upNext(const QJsonObject &from);
Read<Hit> hit(const QJsonObject &from);
/// Both shapes, because both cross the wire: the envelope when a page was asked for, the bare
/// list from a server that predates it.
Read<Hits> hits(const QJsonDocument &from);

/// The contract's spellings, so that a test can walk them rather than trust a switch.
Medium medium(const QString &word);
QString spell(Medium value);
/// `name` for anything unknown, which is what the server does with it — a client and a server
/// that disagree about the fallback disagree about the shelf they are looking at.
Sort sort(const QString &word);
QString spell(Sort value);
Hit::Kind hitKind(const QString &word);
QString spell(Hit::Kind value);
std::optional<ReadStatus> readStatus(const QString &word);
QString spell(ReadStatus value);

} // namespace Api
