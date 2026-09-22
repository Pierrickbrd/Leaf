#pragma once

// The queue of files on their way into the library.
//
// A singleton, and that is the whole shape of it: an import outlives the screen that
// started it. You drop files, you answer the questions, you close the dialog and go back to
// reading, and this keeps going. `ImportDialog` is one view of it and never its owner.
//
// **One transfer at a time.** Two on a domestic line steal each other's bandwidth and both
// finish later than they would have in series.
//
// **Pause yields a place, it does not stop.** That is the rule the whole queue turns on:
// pausing the one in flight lets the next go ahead, and resuming it puts the other one
// behind. There is exactly one slot and pause decides who holds it — which is why a paused
// file starts again on its own when the one ahead of it is done, without anybody clicking.
// Stopping for good is `abandon`, and it is a different button because it is a different
// decision: the server cleans its copy up.

#include "Api.h"
#include "Manifest.h"
#include "Server.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QSet>
#include <QFutureWatcher>
#include <QQmlEngine>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QUrl>

#include <optional>

class Imports : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(int count READ count NOTIFY changed)
    /// How many are still to go — anything not filed and not failed. What the bar's button
    /// shows while the dialog is shut.
    Q_PROPERTY(int inFlight READ inFlight NOTIFY changed)
    /// How many are waiting for an answer. The other half of what that button says: a
    /// transfer that finished and a question nobody saw look the same from a closed dialog.
    Q_PROPERTY(int deciding READ deciding NOTIFY changed)
    /// How many are still being looked at — a folder being walked, a file the server has
    /// not answered about yet.
    ///
    /// The start button waits on this. Without it, pressing « Démarrer » while a folder is
    /// still being read starts nothing at all: the queue looks for what is ready, finds
    /// none of it, and the dialog closes on a transfer that never began.
    Q_PROPERTY(int reading READ reading NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    /// Set when the queue stopped for a reason that is not one file's fault — a key that
    /// lost its right to import, said once rather than fifty times.
    Q_PROPERTY(QString trouble READ trouble NOTIFY changed)
    /// Whether each file is fingerprinted before it is announced.
    ///
    /// Set **before** anything is dropped, because that is when it costs: it is a full read
    /// of every file in the folder. It buys the two things nothing else can — a file
    /// recognised as changed when its length did not change, and a volume verified before
    /// it is installed. A volume that travelled wrong is worse than one that did not
    /// travel, because nothing afterwards would say so.
    Q_PROPERTY(bool verifying READ verifying WRITE setVerifying NOTIFY changed)

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

    enum class Role {
        Name = Qt::UserRole,
        Stage_,
        Sent,
        Size,
        Reason,
        Confidence,
        Candidates,
        Concerns,
        Chosen,
        Trouble,
        RetryIn,
        Folder,
        Creates,
        Moves,
        Nodes,
        Checking,
    };
    Q_ENUM(Role)

    explicit Imports(Server *server, QObject *parent = nullptr);

    static Imports *create(QQmlEngine *engine, QJSEngine *);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_rows.size()); }
    int inFlight() const;
    int deciding() const;
    int reading() const;
    bool busy() const;
    QString trouble() const { return m_trouble; }
    bool verifying() const { return m_verifying; }
    void setVerifying(bool verifying);

    /// Takes local files in and asks the server where each would go. Nothing is sent.
    ///
    /// A file this client cannot open at all is offered anyway, without its sidecar: the
    /// proposal then rests on the name, is weaker for it, and the row says so rather than
    /// pretending. That is the spec's own rule.
    Q_INVOKABLE void offer(const QStringList &paths);

    /// The same, from what a picker or a drop hands over. Returns how many were taken, so a
    /// dialog knows whether to move on — a drop of three folders and a shortcut takes none,
    /// and moving to a list of nothing would be the wrong answer.
    ///
    /// Turning a URL into a path belongs here and not in the QML: `file:///…` is the only
    /// scheme this can do anything with, and a `http:` dropped from a browser has to be
    /// left on the floor rather than sent on as a path no server could open.
    Q_INVOKABLE int offerUrls(const QList<QUrl> &urls);

    /// Throws away everything still being set up, and leaves alone whatever is moving.
    ///
    /// What the × means. The server is told about each, because it keeps what nobody named
    /// — a place reserved and then forgotten holds the library's own disk for good.
    Q_INVOKABLE void giveUpPreparing();

    /// Answers the question a row is asking, by naming the series it belongs to. A file
    /// only: a folder is not asked which series it is, it *declares* one.
    Q_INVOKABLE void decide(int row, const QString &seriesId);

    /// Says yes to what a folder announced it would create. The other half of `decide`,
    /// and a different word because it answers a different question — "which series" for a
    /// file, "make these" for a folder.
    Q_INVOKABLE void accept(int row);

    /// What a folder would create, as `[{ kind, name, at }]`, for the row to draw.
    Q_INVOKABLE QVariantList createsOf(int row) const;

    /// What a folder declares that the library already holds elsewhere, as
    /// `[{ workId, name, from, at, filing }]`.
    Q_INVOKABLE QVariantList movesOf(int row) const;

    /// Says whether one of those is to be filed here after all.
    ///
    /// **Clear by default, and the only reason a folder ever moves.** Dropping a universe
    /// is not a request to rearrange a library, and a series quietly leaving the place a
    /// reader put it is exactly the surprise nothing afterwards would explain. The answer
    /// travels with the commit, so it can be changed right up to the moment the folder
    /// goes.
    Q_INVOKABLE void setFiling(int row, const QString &workId, bool filing);
    Q_INVOKABLE bool isFiling(int row, const QString &workId) const;

    /// Says whether one volume may land on the one the library already holds. `move`'s twin,
    /// and for the same reason: the commit installs over nothing it was not told to.
    Q_INVOKABLE void setReplacing(int row, const QString &path, bool replacing);
    Q_INVOKABLE bool isReplacing(int row, const QString &path) const;

    /// Says whether one declaration may be written over the one the library already holds.
    /// `replace`'s twin one floor up: a `work.json` carries a title and a summary somebody
    /// may have edited through the API, and dropping the folder again is not a request to
    /// undo that.
    Q_INVOKABLE void setDeclaring(int row, const QString &path, bool declaring);
    Q_INVOKABLE bool isDeclaring(int row, const QString &path) const;

    /// Unfolds or folds a node. `at` is its path relative to the card, `""` for its own
    /// root.
    Q_INVOKABLE void toggle(int row, const QString &at);

    /// Everything answered goes into the queue, and the first of them starts.
    Q_INVOKABLE void send();

    /// Yields the one slot: the file in flight steps back and the next one takes over.
    Q_INVOKABLE void pause(int row);

    /// Takes the slot back. Whatever held it steps behind, exactly as if it had been
    /// paused — which is what "now it is that one that waits" means.
    Q_INVOKABLE void resume(int row);

    /// Out of the queue, and the server told to clean up its copy. A refusal is this card's
    /// own fact: it comes back as itself, `Stage::Failed`, rather than a queue-wide
    /// `trouble` that would stop every other row from sending over one session the server
    /// would not drop.
    Q_INVOKABLE void abandon(int row);

signals:
    void changed();

private:
    /// One file, and everything known about it.
    struct Row {
        /// This row, and no other — never reused, and never equal to another row's, even
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
        /// meant reading past the thirteen it never touches; `Row::folder` tells the two
        /// roads apart everywhere else in this file, and this is the same distinction said
        /// once in the shape of the data.
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

        /// This card's tree, flattened for the screen, each node carrying what becomes of
        /// it. Here and not in `data()`: building it reads eleven of `Road`'s own fields
        /// and nothing else, and `data()` carried it inline until the switch it sat in was
        /// the most tangled function in the client.
        QVariantList nodes() const;

        /// What this card says about itself while it is still being read — a count of
        /// volumes hashed, or the word for a wait. `walking` is the queue's
        /// `m_walkingToken`: only the one folder actually on the pool names a hash in
        /// progress, and the rest are waiting their turn.
        QString checking(quint64 walking) const;

        qint64 sizeOf(const QString &relative) const;

        /// Whether committing this card would move a byte or write a line.
        ///
        /// A folder the library already holds whole, with nothing ticked, is a card whose
        /// « Importer » does nothing at all. It says so rather than reading « Prêt », and
        /// it goes when the transfer starts — its session cleaned up with it, which is the
        /// same leak abandoning one closes.
        bool nothingToDo() const;
    };

    void ask(int row);

    /// Reads a dropped folder — **off this thread, always**.
    ///
    /// It was read here, in `offer`, with a comment saying the full pass was "paid once
    /// against a transfer measured in gigabytes". The thread paying it was the one drawing
    /// the window: dropping a real series froze the application until the desktop offered
    /// to kill it, before a single byte had moved. Checksums make it minutes rather than
    /// seconds, but the walk alone is enough on a folder of any size.
    ///
    /// The row exists before the read starts, so the dialog says « Vérification » against
    /// the folder's name while it works, rather than showing nothing at all.
    void describe(int row);
    /// `token` and not `path`: a folder abandoned and redropped while its old walk was
    /// still on the pool shares the new row's path, and the old walk's own answer has to
    /// land on nobody rather than on the row wearing its old name.
    void described(quint64 token, const Manifest::Folder &tree);
    /// One volume further into the hash walk. Reached from `QFutureWatcher`'s own
    /// `progressValueChanged`, never from the pool thread `Manifest::of` runs on: the
    /// watcher already marshals that signal onto this object's thread the same way it
    /// marshals `finished`, so nothing here has to reach back into `this` from the pool by
    /// hand — which is the mistake, resolving a QML singleton off a network thread, that
    /// once crashed this client in one launch out of five, and that a raw
    /// `QMetaObject::invokeMethod` from that same pool would have repeated in a new shape.
    /// `token`, for the same reason `described` takes one rather than a path.
    void hashedSoFar(quint64 token, qint64 done);
    void announceFolder(int row, const Manifest::Folder &tree);
    /// A row for one thing `offer` found, folder or file — the model insert lives here once
    /// rather than twice.
    void appendRow(const QString &path, const QString &name, bool folder, qint64 size);
    /// The next queued folder, and only one at a time.
    ///
    /// Twelve walks over the disk at once would get in each other's way, and the first card
    /// would be ready no sooner for it — so `offer` hands folders to this one at a time
    /// rather than starting every `describe` at once.
    void describeNext();
    void tookFolder(const QString &path, const Server::Answer &answer);
    void sendMoreOfFolder(int row);
    /// One chunk arrived, and what that changes. Out of the callback that used to hold it
    /// because a lambda long enough to carry its own reasoning is a function that has not
    /// been given a name.
    void chunkLanded(int row, qint64 went, qint64 whole);
    void commitFolder(int row);
    /// What the server answered the commit. Same reason as `chunkLanded`, and it is where
    /// « ce qui est arrivé » is decided — installed, still coming, corrupt, orphaned.
    void committed(int row, const Server::Answer &answer);
    void resumeFrom(int row);
    void took(const QString &path, const Server::Answer &answer);
    void pump();
    void sendMore(int row);
    void confirm(int row);
    void settle(int row, Stage stage, const QString &trouble = {});
    void retryLater(int row);
    /// Moves a row, telling the model. The queue's order is the model's order, so that a
    /// screen never has to sort what it is handed.
    int rowOf(const QString &path) const;
    /// Only `describe`, `described` and `hashedSoFar` use this — the rest of the queue
    /// still finds its row by `path`, matching every request the server itself tracks by
    /// path. Narrowing the walk alone to `token` is the whole of what this round fixes;
    /// widening it to every network callback here would be a much bigger change than one
    /// commit correcting the checking count should carry.
    int rowOfToken(quint64 token) const;
    int holding() const;
    void announce(int row);

    Server *m_server;
    QList<Row> m_rows;
    bool m_verifying = true;
    /// Set while a folder is being walked, so `describeNext` never starts a second one.
    bool m_describing = false;
    /// Handed to the next row `appendRow` creates. Only ever grows, so no two rows —
    /// including one dropped after an identically-named one was abandoned — ever compare
    /// equal.
    quint64 m_nextToken = 1;
    /// The one folder actually on the pool right now, or `0` — no row's own token, since
    /// `m_nextToken` starts at `1`. A dropped shelf of a dozen series queues every one of
    /// them at `Stage::Asking`, and only this one has a walk under way — the other eleven
    /// are not stuck, they are waiting their turn, and showing them a count frozen at zero
    /// would say the opposite of that.
    quint64 m_walkingToken = 0;
    QString m_trouble;
    QTimer m_retry;
};
