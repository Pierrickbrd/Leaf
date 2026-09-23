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
#include <QHash>
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
    /// How many of this edition's entries are finished, and how far into the ones that are
    /// not — « eleven volumes, and 0.82 of a twelfth ».
    ///
    /// `readStatus` says unread, started or finished and cannot say *how far*: a tile drew
    /// a full-width mark the moment a series was opened, because "started" was the only
    /// thing it had. Two numbers and not one, because each is a plain fact and their sum is
    /// what a reader asks for.
    int readEntries = 0;
    double partRead = 0.0;
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

/// What the publisher put out, and what it says exists — as opposed to `Holding`, which is
/// what this library actually has of it.
///
/// `declaredVolumes` sits here and not beside the count of entries on purpose: it is the
/// publisher speaking, not the shelf counting. Read against `holding.ownedVolumes`, the pair
/// the contract insists on — « *what you own. Counted. The two do not say the same thing* » —
/// finally reads as the two different claims it is.
struct Publication {
    std::optional<QString> publisher;
    /// The publisher's imprint, a sibling of `publisher` rather than a replacement for it.
    std::optional<QString> collection;
    std::optional<QString> language;
    /// What exists out in the world. Declared, never counted.
    std::optional<int> declaredVolumes;
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
    /// The same universe, addressable. `/universes/{id}/orders` wants an id, and a screen
    /// holding only the name had to fetch every universe in the library and match strings
    /// to find one — a whole request, and an answer that goes wrong the day somebody
    /// renames a folder.
    std::optional<QString> universeId;
    std::optional<QString> edition;
    /// The one file of a book that is a whole book, and empty for everything else. Opening
    /// such a tile goes to the reader rather than to a series page, and the id is here so
    /// that the click needs no request of its own to find out what to open.
    std::optional<QString> oneShotEntry;
    /// The prose about the work, shown as it stands — the one thing a description tab is
    /// opened for.
    std::optional<QString> summary;
    Credits credits;
    Publication publication;
    std::optional<Medium> medium;
    std::optional<ReadingDirection> readingDirection;
    std::optional<Run> run;
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

/// How far through a series, as the fraction a tile draws. Nought when nothing is counted,
/// so a series with no entries recorded draws nothing rather than a full bar out of a
/// division by nought.
///
/// Here rather than in the shelf that first needed it: the universe block of a series page
/// draws the same tile from the same answer, and two copies of this arithmetic would be two
/// answers the day one of them is corrected.
double howFarRead(const Series &one);

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
    /// Reading positions carried onto a new identity, and those that could not be.
    ///
    /// Nought on every scan but the one after a library stops being identified by the paths
    /// of its folders. Read as absent rather than required, because a number that is nought
    /// almost always is not a reason to refuse an answer that otherwise says everything.
    int progressCarried = 0;
    int progressLost = 0;
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

/// Where the reader stands in one file.
///
/// Absent means never opened — the contract answers 204 rather than an empty object, and a
/// list of them holds « one record per entry that has been opened ». So a screen marries two
/// answers rather than reading a state off the entry, which carries none.
struct Progress {
    QString entryId;
    int page = 0;
    int pageCount = 0;
    bool finished = false;
    /// How many times this file has been finished. `finished` says where the reader stands
    /// now and a rewind clears it; this says whether they ever reached the end, and only the
    /// second answers « have I read this ».
    int timesFinished = 0;
};

/// One file of an edition — a volume, or a chapter that arrived on its own.
///
/// `number` identifies and `sortKey` orders, and they are not the same: two editions may
/// number differently, so the order comes from the edition and the identity from the volume.
/// The client never re-sorts what it receives — the server answers « in reading order », and
/// sorting by file name is how « Tome 10 » lands before « Tome 2 ».
struct Entry {
    enum class Kind { Volume, Chapter };

    QString id;
    Kind kind = Kind::Volume;
    std::optional<double> number;
    std::optional<QString> title;
    int pageCount = 0;
    int chapterCount = 0;
    /// The file name alone, never a path. What a deletion names, and all the client is told.
    QString file;
    qint64 size = 0;
    std::optional<double> sortKey;
};

/// A stretch of the story, and not a property of a volume.
///
/// **A range.** Four volumes can belong to two arcs, because an arc does not end where a
/// volume ends — so it can be neither a column of a list nor a label on a line. `unit` says
/// what the bounds are counted in, and the two do not mean the same thing: a CHAPTER range
/// over volumes is the one case where a frontier falls inside a file.
///
/// `parentId` is a saga holding its arcs, declared and never deduced: two ranges that happen
/// to contain one another do not make one.
struct Arc {
    enum class Unit { Volume, Chapter };

    QString id;
    QString name;
    Unit unit = Unit::Chapter;
    double from = 0;
    double to = 0;
    int position = 0;
    std::optional<QString> parentId;
};

/// One stretch of one work, inside a named way through a universe.
///
/// `seriesId` is « always present on a VOLUME step and never on a CHAPTER one », because
/// "volumes 1 to 7" is different content in a 42-volume edition and a 34-volume one, while a
/// chapter number identifies the same story in both. `work` and `series` carry their names so
/// a step can be drawn without a request of its own.
struct ReadingStep {
    enum class Unit { Volume, Chapter };

    QString workId;
    QString work;
    /// Absent for the whole work, which the contract calls the common case.
    std::optional<Unit> unit;
    std::optional<QString> seriesId;
    std::optional<QString> series;
    std::optional<double> from;
    std::optional<double> to;
};

/// One named way through a universe — ordered stretches of works, and not a flat list of
/// editions. The same work may appear more than once, which is what lets an order say
/// « work A part 1, work B, work A part 2 ».
struct ReadingOrder {
    QString id;
    QString name;
    /// At most one order of a universe carries it.
    bool isDefault = false;
    QList<ReadingStep> steps;
};

/// A universe, by name — and how many ways through it it declares.
///
/// `orderCount` is the guard: a screen knows before it asks whether there is anything to ask
/// for, exactly as `counts.arcs` does for the arcs. Usually nought.
struct Universe {
    QString id;
    QString name;
    int orderCount = 0;
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

// ——— L'import ———————————————————————————————————————————————————————————————

/// What a file said about itself, from the `entry.json` it carries.
///
/// Every field optional because the file may say nothing at all, and a file that says
/// nothing is an ordinary file from somewhere else rather than a broken one.
struct FileReading {
    std::optional<QString> work;
    std::optional<QString> edition;
    /// VOLUME or CHAPTER, and absent when the file did not say.
    std::optional<QString> kind;
    std::optional<double> number;
    std::optional<QString> title;
    int chapterCount = 0;
};

/// One series a file might belong to.
struct Candidate {
    QString seriesId;
    QString name;
};

/// Where the server would put a file, and how sure it is.
struct Proposal {
    /// How sure, and what the screen has to ask because of it.
    ///
    /// `Other` for a word this client has not learned: the item stands and is shown by its
    /// `reason`, because the server may grow a confidence before the client does — and the
    /// alternative is a file that vanishes from a list for being described too well.
    enum class Confidence { Certain, Replacement, Ambiguous, Unknown, Other };

    /// The id to confirm or abandon it by.
    QString received;
    QString name;
    qint64 size = 0;
    FileReading read;
    Confidence confidence = Confidence::Other;
    /// In words, written by the server and shown as it stands.
    QString reason;
    QList<Candidate> candidates;
    /// The entry it would replace, when something occupies the place.
    std::optional<QString> replaces;
    /// What the file says about itself that does not hold together. Never a refusal.
    QList<QString> concerns;
};

/// A place held for a file, and where it would go — answered before a byte moves.
struct Reserved {
    QString id;
    Proposal proposal;
};

/// What the server holds of one reserved file. `received` is where to resume.
struct Staged {
    QString id;
    QString name;
    qint64 size = 0;
    qint64 received = 0;
};

/// A file waiting for a decision, or for its bytes.
struct Waiting {
    QString id;
    QString name;
    qint64 size = 0;
    qint64 lastTouchedAt = 0;
    /// UPLOAD or DROP. Kept as the contract's word: only `onlyCopy` changes a decision.
    QString origin;
    /// True when abandoning it does not send you back to a file you still have.
    bool onlyCopy = false;
    qint64 received = 0;
};

/// Where a file went, once it was filed.
struct Filed {
    QString entryId;
    QString path;
    /// A file that was there is gone, whichever way that was decided.
    bool replacement = false;
    /// Filed under a name of its own because the one it wanted was taken.
    bool renamed = false;
    /// Filled in when the declared count moved, or should have.
    std::optional<QString> note;
};

/// A file of that name is already there, and nobody has said which one wins.
///
/// It carries what each of the two says about itself so the question put to a person is
/// about the volumes rather than about the file names — the only level at which it can be
/// answered.
struct Collision {
    QString path;
    std::optional<QString> entryId;
    FileReading occupies;
    FileReading arriving;
    /// Whether the two describe the same volume. Never decided on the title.
    bool sameVolume = false;
    /// The declared fields the two agree on, the title included.
    QList<QString> agrees;
    /// The same bytes on both sides. Settles it.
    bool identical = false;
    /// The name it would take under RENAME.
    QString wouldBecome;
};

/// Something the library does not hold yet and would gain.
struct Creation {
    /// UNIVERSE, WORK or EDITION. Kept as the contract's word: the client has a French
    /// sentence for each and shows an unfamiliar one by its name alone rather than
    /// dropping it.
    QString kind;
    QString name;
    /// Where under the root, so two works of the same name are told apart.
    QString at;
};

/// A folder announced, and what the server makes of it — before a byte moves.
/// A work the library already holds, that a dropped folder declares somewhere else.
///
/// The sixth case of an import: a universe arrives and one of the series it declares is
/// already on the disk under another parent. It is a **move**, not a creation, and the two
/// are told apart only because a folder's identity travels with it in its sidecar.
struct Relocation {
    /// What a move is aimed with — the identity the library already files it under.
    QString workId;
    QString name;
    /// The folder it is in now, so a reader sees what is about to change.
    QString from;
    /// Where under the root it would go, the same way a `Creation` says it.
    QString at;
};

/// One file that would land on another, and what the library already holds there.
///
/// The size alone says « different » without saying how: two archives six hundred and
/// eighty-two bytes apart differed entirely inside a declaration neither file listing nor
/// weight could show. `presentRead` is how the screen tells « it carries no title » from
/// « the server did not open it » — past a ceiling on one preflight, it does not.
struct Replacement {
    QString path;
    qint64 size = 0;
    qint64 presentSize = 0;
    QString presentTitle;
    std::optional<double> presentNumber;
    bool presentRead = false;
};

/// One declaration that would be written over another, and what the two disagree about.
///
/// The field names and not the values: a summary is four hundred words and a line of a tree
/// is one line. « résumé, arcs » says what is at stake, and it is what a reader needs — the
/// question is never « which of these two strings » but « did I edit this here ».
struct Declaration {
    QString path;
    /// What the library's own declaration calls itself. Empty when it names itself nothing.
    QString presentName;
    QList<QString> differs;
};

struct Opened {
    QString id;
    QString root;
    QList<Creation> creates;
    /// Empty when nothing this folder declares is already elsewhere. **Nothing here moves
    /// unless somebody says so**: the dialog asks, one box per line, and the boxes start
    /// clear.
    QList<Relocation> moves;
    /// What would land on a file the library already holds. Their paths are in `toSend`
    /// too, on purpose: sending them is still what a commit does, and this is the list that
    /// says doing so replaces something rather than adds it. **Nothing here is replaced
    /// unless the commit names it**, the same way nothing moves unless it does.
    QList<Replacement> replaces;
    /// The declarations this manifest would rewrite. A folder every volume of which the
    /// library already holds still carries its `work.json`, and installing it over a title
    /// edited through `PATCH` is a decision, not a side effect of dropping the folder again.
    QList<Declaration> declarations;
    QList<QString> toSend;
    QList<QString> alreadyThere;
    qint64 bytesToSend = 0;
};

/// How much of one file the server now holds.
struct Received {
    QString path;
    qint64 received = 0;
};

/// The offset asked for was past what the server holds, and here is what it holds.
struct BadOffset {
    QString error;
    qint64 received = 0;
};

/// Where a whole session stands, enough for a broken transfer to pick up.
struct Session {
    QString id;
    QString root;
    /// Path to the number of bytes held. The only map that crosses this seam, and it has
    /// to be one: asking per file would be one request per volume of a forty-file folder.
    QHash<QString, qint64> received;
    QList<QString> missing;
};

/// What a commit installed, and what it could not.
struct Installed {
    QString root;
    int installed = 0;
    /// On the server, absent from the manifest. **Never deleted** — reported, and shown by
    /// name before anybody decides.
    QList<QString> orphans;
    /// Arrived whole and not matching the checksum announced for them. To send again: a
    /// volume that travelled wrong is worse than one that did not travel, because nothing
    /// afterwards would say so.
    QList<QString> corrupt;
    /// Announced and not here in full. Nothing is wrong with them.
    QList<QString> pending;
    /// The session is still there and the rest can go against the same id.
    bool open = false;
    /// The works this commit filed under the folder it installed, by identity. Only the
    /// ones that were asked for.
    QList<QString> moved;
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
Read<Entry> entry(const QJsonObject &from);
Read<Progress> progress(const QJsonObject &from);
Read<Arc> arc(const QJsonObject &from);
Read<ReadingStep> readingStep(const QJsonObject &from);
Read<ReadingOrder> readingOrder(const QJsonObject &from);
Read<Universe> universe(const QJsonObject &from);
Read<Hit> hit(const QJsonObject &from);
/// Both shapes, because both cross the wire: the envelope when a page was asked for, the bare
/// list from a server that predates it.
Read<Hits> hits(const QJsonDocument &from);
Read<Proposal> proposal(const QJsonObject &from);
Read<Reserved> reserved(const QJsonObject &from);
Read<Staged> staged(const QJsonObject &from);
Read<Waiting> waiting(const QJsonObject &from);
Read<Filed> filed(const QJsonObject &from);
Read<Collision> collision(const QJsonObject &from);
Read<Opened> opened(const QJsonObject &from);
Read<Received> received(const QJsonObject &from);
Read<BadOffset> badOffset(const QJsonObject &from);
Read<Session> session(const QJsonObject &from);
Read<Installed> installed(const QJsonObject &from);

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
