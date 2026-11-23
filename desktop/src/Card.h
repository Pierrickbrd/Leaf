#pragma once

// One thing on its way into the library, and everything known about it.
//
// Split out of `Imports` rather than nested inside it, and the reason is the same one
// `ImportCaptions.h` gives: a class that carries two subjects makes both harder to find.
// `Imports` is a list a screen draws; this is one line of that list. Nothing here talks to
// a server, touches the disk or knows what row it sits at — a card answers about itself and
// nothing else, which is why every one of its methods can be read without the queue.
//
// **The two enumerations live here and not on `Imports`** because they describe a card:
// where it is on its journey, and how loudly the screen should say so. `Imports` keeps an
// alias of each, so `Imports::Stage::Ready` still means what it always did to a test and to
// every other file.

#include "Api.h"
#include "Facts.h"
#include "Manifest.h"
#include "Shape.h"

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QString>
#include <QVariantList>

#include <optional>

class Card
{
    Q_GADGET

public:
    /// How loudly a node's state is said, decided here and dressed by the screen.
    ///
    /// Three and not seven, for the argument `FilterChip.qml` makes about colour: if
    /// « terminées » were always green, that green would mean nothing, and the one thing
    /// worth seeing would become the harder one to see. `Quiet` is a node nothing will
    /// happen to, `Moving` the one thing happening right now, `Attention` something that
    /// will be destroyed or already went wrong. Everything else is `Ordinary`.
    ///
    /// An enum rather than a colour: a model that named a colour would decide what the dark
    /// theme looks like from the wrong end of the application.
    enum class Tone { Quiet, Ordinary, Moving, Attention };
    Q_ENUM(Tone)

    /// Where one file is in its journey.
    ///
    /// `Deciding` and `Ready` are deliberately apart. A proposal the server is sure of
    /// needs no answer and goes straight to `Ready`; one that names candidates, or names
    /// nothing, stops here until somebody says which series. Merging the two would mean a
    /// file that sails past a question nobody was asked.
    enum class Stage {
        Asking,
        Deciding,
        Ready,
        Sending,
        Filing,
        Filed,
        Paused,
        Failed,
        /// Announced, and stopped on what nobody said: a role guessed, a required field
        /// missing, a doubt, an archive that would not open (`Facts::needsReview`). Last
        /// and not beside `Deciding`, because `ImportRecap.qml` mirrors these as numbers and
        /// a value inserted in the middle would move every stage after it.
        Reviewing,
    };
    Q_ENUM(Stage)

    /// This card, and no other — never reused, and never equal to another card's, even
    /// one dropped from the same path after this one is gone. `path` is not that: a
    /// folder abandoned and redropped while its walk was still on the pool let that
    /// walk's own answer land on the new row wearing the old one's path, once per
    /// folder before the checking count, once per volume since. Set by `appendRow`.
    quint64 token = 0;
    QString path;
    QString name;
    qint64 size = 0;
    /// A folder travels the other road: announced whole, sent file by file, committed
    /// in one rename. Everything below that is `QList` rather than a single value is
    /// there for it.
    bool folder = false;
    Stage stage = Stage::Asking;
    /// What the pre-flight held for it. Empty until the server answers.
    QString id;
    std::optional<Api::Proposal> proposal;
    QString chosen;
    qint64 sent = 0;
    QString trouble;
    /// Seconds until the next attempt, and zero when nothing is pending. Shown, because
    /// a queue that retries in silence is a queue that looks stuck.
    int retryIn = 0;
    int attempts = 0;

    /// What a card laid on something the library holds carries: what it is aimed at, what the
    /// server compared it with, and what its reader chose to keep, splice and replace. A plain
    /// drop leaves every field of it at its default.
    struct Aim {
        /// What a reimport is aimed at, as the reader picked it.
        struct Target {
            QString kind;
            QString id;
            QString name;
        };
        /// Absent for a plain drop. With one, the card is laid on the target's folder, is
        /// always stopped for its reader to check, and ticks what replaces what on its own.
        std::optional<Target> target;
        /// What the server found each archive to be among the volumes of its edition, what
        /// the target holds that the drop does not bring, the series of a universe it does
        /// not bring, the way down to the target (`place`) and what replacing a chapter does
        /// to its volume (`spliced`).
        QList<Api::VolumeMatch> volumes;
        QList<Api::HeldVolume> absent;
        QList<Api::AbsentSeries> absentSeries;
        std::optional<Api::TargetPlace> place;
        std::optional<Api::SpliceOutcome> spliced;
        /// The paths whose volume the asking profile starts again from the first page. Empty
        /// until somebody ticks one, and sent with the commit only when it is not.
        QSet<QString> resetting;
        /// The pages of the file that replace the aimed chapter, counted from 0.
        struct Splice {
            int from = 0;
            int to = 0;
        };
        std::optional<Splice> splice;
        /// The number of the chapter aimed at, which the source is asked for the pages of.
        std::optional<double> chapterNumber;
        /// `"<sidecar>\n<key>"`: the values the library holds that stay over the ones the
        /// drop brings.
        QSet<QString> keeping;
        /// What the library's declarations held, by sidecar, as the answers said it. Kept
        /// across announcements: once every key is kept the declaration no longer differs,
        /// the answer stops naming it, and the values to keep would be lost with it.
        QHash<QString, QJsonObject> held;
        /// What the commit installed over a volume the library held, and where the old
        /// files wait.
        QList<Api::ReplacedFile> replaced;
    };

    /// Where the server files what the card brings, and the names it asks for or refuses on
    /// the way: what the step of names draws, and what is typed back into it.
    struct Names {
        /// Where the server will file each volume and under what name, and the files that
        /// would meet on one name. Empty from a server that says none.
        QList<Api::Landing> landsAs;
        QList<Api::NamesMeet> collisions;
        QList<Api::Reshelved> reshelved;
        QList<Api::NeedsAName> editionsNeedingAName;
        QList<Api::NameProblem> problems;
        /// What the reader typed for the editions an import adds, by the path the server named
        /// (`@current` for the edition the series holds). Travels with the announcement.
        QHash<QString, QString> editionNames;
        /// The paths of the rows of names, in the order they first appeared: a row stays where it
        /// was once the name typed in it is accepted and the server stops asking for it.
        QStringList editionOrder;
        /// `editionNames` as the last announcement carried them, so that a name typed while the
        /// answer was awaited is known not to be in it.
        QHash<QString, QString> namesSent;
    };

    /// What the announcement's answer said of the folder, beyond names: what it would send,
    /// create, move, replace and declare, and what the library already holds. Replaced whole
    /// by each answer.
    struct Answer {
        /// The series it would create that resemble one held.
        QList<Api::Resemblance> resembles;
        /// What the server asked for, in its order (`Road::at` is where the sending is in it).
        /// The server decides: it knows what it already holds, and a client that sent the
        /// whole folder anyway would send a library twice.
        QList<QString> toSend;
        /// What would land on a file the library already holds, with what is there.
        QList<Api::Replacement> replaces;
        /// The declarations this folder would write over one the library already holds.
        QList<Api::Declaration> declarations;
        /// What the server already held among the files this folder announced — the one
        /// list that actually says "already there". `creates` cannot stand in for it at the
        /// container level: it is built from sidecars alone (`bulk_import.rs::would_create`),
        /// so a folder of archives with no sidecar at all — the spec's own "implicit
        /// edition" — never appears in it, declared or not.
        ///
        /// A set and not the answer's list: the frame asks it of every file on every change,
        /// and a list of a thousand chapters asked a thousand times is a million comparisons.
        QSet<QString> alreadyThere;
        /// Every folder of this card, relative to it and `""` for its root, that holds at
        /// any depth a file outside `alreadyThere` — what `Carrier::travels` reads for a
        /// container. Kept with `alreadyThere` and rebuilt with it, never apart: asked by
        /// walking `tree.files` instead, each container of each line walked the files of
        /// its card on every rebuild: a universe of fourteen series of thirty volumes was
        /// twenty thousand comparisons for one list and its counters, and it grows as the
        /// series times the files.
        QSet<QString> travelling;
        QList<Api::Creation> creates;
        QList<Api::Relocation> moves;
    };

    /// The reading of the dropped folder: where it sits in its drop, what the walk found and
    /// how far it has come, and the sheets read out of it.
    struct Walk {
        /// The drop this card was read out of (`Imports::dropOf`), and where its root sits in
        /// it — `""` when the dropped folder is the card. A role changed on a folder is a
        /// change to the drop, and it is by these two that the cards it touches are found.
        quint64 drop = 0;
        QString within;
        /// `Shape::rolesOf` the drop at `within`: what `Facts::read` reads the sheets by.
        Shape::Roles roles;
        /// What `Manifest::found` saw, before a single checksum. The card is this node —
        /// present as soon as finding is done, which is what tells a folder being checked
        /// apart from a folder that found nothing.
        Manifest::Node node;
        Manifest::Folder tree;
        /// Whether the walk over this folder has finished. Between that and the server's
        /// answer the card is neither being read nor queued, and said « En attente » like
        /// both — so nothing on screen told a reader a verification was over.
        bool checked = false;
        /// Whether the checksummed walk (`Manifest::of`) has finished for this folder.
        /// `describeNext` reads only this — a guard that also looked at `id`, `stage` and
        /// whether `tree.files` was empty asked the same question three ways, which is
        /// three ways to get it wrong.
        bool described = false;
        /// How many volumes the hash walk has read so far, so that « Vérification » carries
        /// a count instead of sitting on the one word. Fixed for thirty seconds, the word
        /// alone once made the application look crashed for long enough that the desktop
        /// offered to kill it.
        qint64 hashed = 0;
        /// Whether this card's walk computes checksums, fixed when the walk starts: the box
        /// can be unticked while a folder is being read, and the word on the card names the
        /// work the walk is doing, not the box's state now.
        bool hashing = false;
        /// What was read about each container and file, and from where (`Facts::read`).
        /// Built when the walk ends, and what every announcement composes from.
        QList<Facts::Sheet> sheets;
    };

    /// The other road, and nothing but a file's twin ever travels it.
    ///
    /// A folder is announced whole, sent file by file and committed in one rename, and
    /// every field below belongs to that journey alone — a dropped `.cbz` leaves all
    /// seventeen of them at their default and always has. They sat beside `stage` and
    /// `sent` as one flat list of thirty, where reading the four a file actually uses
    /// meant reading past the thirteen it never touches; `folder` tells the two roads
    /// apart everywhere else in the queue, and this is the same distinction said once in
    /// the shape of the data.
    ///
    /// Grouped by what each part is about — the aim, the names, the answer, the walk — and
    /// what stays here is what the reader decided and how far the sending has come. The
    /// parts are bases rather than members so that a field keeps its one name everywhere it
    /// is read: the queue and its carrier read them in over two hundred places, and none of
    /// those is about which part holds it.
    struct Road : Aim, Names, Answer, Walk {
        /// Where in `toSend` the sending is.
        int at = 0;
        /// How much of the file at `at` has gone up. `sent` counts the whole folder, so
        /// one progress bar covers forty volumes.
        qint64 sentInFile = 0;
        /// The nodes unfolded, by their `at`, `""` for the card's own root. Empty by
        /// default: every node starts folded, and a folded node still says what it holds
        /// through `holds`, so it is never a card that shows only its own name. Unfolding a
        /// sixty-volume series by default would be a wall — the accordion opens only what a
        /// reader digs into.
        QSet<QString> open;
        /// Of `declarations`, the ones a reader ticked — cleared with every announcement, like the
        /// rest.
        QSet<QString> declaring;
        /// Of `replaces`, the ones a reader ticked. Empty by default and cleared with every
        /// announcement, exactly like `filing`: six volumes that could be replaced are six
        /// decisions, and a folder dropped on a library is not a request to overwrite it.
        QSet<QString> replacing;
        /// The identities of those the reader ticked. Empty until somebody ticks one.
        QSet<QString> filing;
        /// The folders of the drop the reader chose to create a second series for, though the
        /// library holds one that looks like it: they stop the card no more.
        QSet<QString> second;
        /// A value was corrected since the last announcement: « Importer » has to drop that
        /// session and announce again, since the server remembers the rewrites it was told.
        bool edited = false;
        /// « Importer » was pressed for this card: its next answer is not stopped on again, and
        /// once `Ready` it waits its turn, no longer counted as going nor corrected.
        bool confirmed = false;
        /// « Importer » was pressed while this card was still read. At fifty series the walk
        /// outlasts the first press, and a complete series read after it waited « Prête »
        /// for a second press once the ones before it were filed: it now goes in its turn,
        /// and one that asks to be checked still stops — the press agreed to what was seen.
        bool awaited = false;
        /// The reader let this card go: « Importer tout », or the round button of its own line.
        /// `Carrier::pump` takes only these. It is not `confirmed`, which says the card was
        /// agreed to and can no longer be corrected: « Annuler » on a line puts a card back
        /// to zero and keeps it agreed to, but nothing may set it moving again until its
        /// reader asks — which is what a pause is.
        bool released = false;
        /// One value a reader typed, kept so that a role change can read the sheets again
        /// without losing it: read afresh, a number typed on a volume went back to the one
        /// its name gave.
        struct Correction {
            QString at;
            QString key;
            QJsonValue value;
        };
        /// Every correction accepted since the card was read, the last one per field.
        QList<Correction> corrections;
        /// The series of this card whose folder name is no title — « Vrac », « Mangas »
        /// (`Shape::Suggestion::titleMissing`) — by their `at` in the card. Their title is
        /// missing until somebody types one: guessed from the folder, one press of
        /// « Importer » filed a series called « Vrac ».
        QSet<QString> untitled;
    };

    Road road;

    /// This card's tree, flattened for the screen, each node carrying what becomes of it.
    QVariantList nodes() const;

    /// What this folder would create, as `[{ kind, name, at }]`, and what it declares that
    /// the library already holds elsewhere, as `[{ workId, name, from, at, filing }]` —
    /// both for the row to draw. Read through the model's own roles and nowhere else: an
    /// invokable beside them said the same thing twice, and only one of the two was what
    /// a `.qml` file actually bound to.
    QVariantList creates() const;
    QVariantList moves() const;

    /// What this card says about itself while it is still being read — a count of volumes
    /// hashed, or the word for a wait. `walking` is the queue's own `m_walkingToken`: only
    /// the one folder actually on the pool names a hash in progress, and the rest are
    /// waiting their turn.
    QString checking(quint64 walking) const;

    /// Where a folder stands in the walk that reads it — what a line of the choosing step
    /// counts with. Apart from `checking`'s string because the line draws a bar from the
    /// numbers and shortens the words by its width, and a sentence cannot be shortened.
    enum class Counting { None, Waiting, Walking, Over };

    /// `{ state: Counting, done, whole, hashing, uncovered }`: the volumes read so far over the volumes
    /// the folder holds, and whether the walk hashes them. A walk that is over has read every
    /// one, whatever `road.hashed` last heard — the count only ever says how far it had come.
    /// `uncovered` is the reason some archive's members could not be hashed, or empty.
    QVariantMap progress(quint64 walking) const;

    /// Whether the walk of the dropped folder met this path. A path the server asks for that
    /// it never did is not one to read from the disk.
    bool walked(const QString &relative) const;
    qint64 sizeOf(const QString &relative) const;

    /// Whether committing this card would move a byte or write a line.
    ///
    /// A folder the library already holds whole, with nothing ticked, is a card whose
    /// « Importer » does nothing at all. It says so rather than reading « Prêt », and it
    /// goes when the transfer starts — its session cleaned up with it, which is the same
    /// leak abandoning one closes.
    bool nothingToDo() const;

    /// Whether the third step draws a line for this card: every one but a card that failed
    /// before anything was agreed to, whose reason the first step already says.
    bool shown() const { return stage != Stage::Failed || road.confirmed; }

    /// How far it has gone, from 0 to 1, for the ring round the line's button.
    double fraction() const;

    /// What the line says under the name: what the card is, what becomes of it and how it
    /// was read — only what the client knows, nothing it would have to guess.
    QString about() const;
};
