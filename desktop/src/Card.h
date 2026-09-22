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
#include "Manifest.h"

#include <QList>
#include <QObject>
#include <QSet>
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

    /// The other road, and nothing but a file's twin ever travels it.
    ///
    /// A folder is announced whole, sent file by file and committed in one rename, and
    /// every field below belongs to that journey alone — a dropped `.cbz` leaves all
    /// seventeen of them at their default and always has. They sat beside `stage` and
    /// `sent` as one flat list of thirty, where reading the four a file actually uses
    /// meant reading past the thirteen it never touches; `folder` tells the two roads
    /// apart everywhere else in the queue, and this is the same distinction said once in
    /// the shape of the data.
    struct Road {
        Manifest::Folder tree;
        /// What the server asked for, in its order, and where in that list this is. The
        /// server decides: it knows what it already holds, and a client that sent the
        /// whole folder anyway would send a library twice.
        QList<QString> toSend;
        /// What would land on a file the library already holds, with what is there.
        QList<Api::Replacement> replaces;
        /// The declarations this folder would write over one the library already holds.
        QList<Api::Declaration> declarations;
        /// Of those, the ones a reader ticked — cleared with every announcement, like the
        /// rest.
        QSet<QString> declaring;
        /// Of those, the ones a reader ticked. Empty by default and cleared with every
        /// announcement, exactly like `filing`: six volumes that could be replaced are six
        /// decisions, and a folder dropped on a library is not a request to overwrite it.
        QSet<QString> replacing;
        /// What the server already held among the files this folder announced — the one
        /// list that actually says "already there". `creates` cannot stand in for it at the
        /// container level: it is built from sidecars alone (`bulk_import.rs::would_create`),
        /// so a folder of archives with no sidecar at all — the spec's own "implicit
        /// edition" — never appears in it, declared or not.
        QList<QString> alreadyThere;
        int at = 0;
        /// How much of the file at `at` has gone up. `sent` counts the whole folder, so
        /// one progress bar covers forty volumes.
        qint64 sentInFile = 0;
        QList<Api::Creation> creates;
        QList<Api::Relocation> moves;
        /// Whether the walk over this folder has finished. Between that and the server's
        /// answer the card is neither being read nor queued, and said « En attente » like
        /// both — so nothing on screen told a reader a verification was over.
        bool checked = false;
        /// The identities of those the reader ticked. Empty until somebody ticks one.
        QSet<QString> filing;
        /// What `Manifest::found` saw, before a single checksum. The card is this node —
        /// present as soon as finding is done, which is what tells a folder being checked
        /// apart from a folder that found nothing.
        Manifest::Node node;
        /// The nodes unfolded, by their `at`, `""` for the card's own root. Empty by
        /// default: every node starts folded, and a folded node still says what it holds
        /// through `holds`, so it is never a card that shows only its own name. Unfolding a
        /// sixty-volume series by default would be a wall — the accordion opens only what a
        /// reader digs into.
        QSet<QString> open;
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

    qint64 sizeOf(const QString &relative) const;

    /// Whether committing this card would move a byte or write a line.
    ///
    /// A folder the library already holds whole, with nothing ticked, is a card whose
    /// « Importer » does nothing at all. It says so rather than reading « Prêt », and it
    /// goes when the transfer starts — its session cleaned up with it, which is the same
    /// leak abandoning one closes.
    bool nothingToDo() const;
};
