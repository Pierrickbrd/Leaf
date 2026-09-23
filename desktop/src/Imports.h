#pragma once

// The queue of files on their way into the library, as a list a screen can draw.
//
// A singleton, and that is the whole shape of it: an import outlives the screen that
// started it. You drop files, you answer the questions, you close the dialog and go back to
// reading, and this keeps going. `ImportDialog` is one view of it and never its owner.
//
// **What a card *is* lives in `Card.h`, and what happens to it in `Carrier.h`.** This holds
// the list, answers the roles a `.qml` file binds to, and takes what a reader clicks. It
// was one class of sixty-one methods doing all three, which is what `cpp:S1448` had been
// saying: a queue with one slot, two roads and retries is not a list model, and neither
// half was easy to find inside the other.
//
// **One transfer at a time.** Two on a domestic line steal each other's bandwidth and both
// finish later than they would have in series.
//
// **Pause yields a place, it does not stop.** That is the rule the whole queue turns on:
// pausing the one in flight lets the next go ahead, and resuming it puts the other one
// behind. There is exactly one slot and pause decides who holds it — which is why a paused
// file starts again on its own when the one ahead of it is done, without anybody clicking.
// Stopping for good is `abandon`, and it is a different button because it is a different
// decision: the server cleans its copy up. `Carrier` is where all of that is written.

#include "Api.h"
#include "Card.h"
#include "Server.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QQmlEngine>
#include <QString>
#include <QVariantList>
#include <QUrl>

class Carrier;

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
    /// Where one card is on its journey, and how loudly to say so — both on `Card`, which
    /// is what they describe. Aliased here because `Imports::Stage::Ready` is how the rest
    /// of this client and every test already spell it, and a rename would be churn for
    /// nothing.
    using Stage = Card::Stage;
    using Tone = Card::Tone;

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

    /// Says whether one of those is to be filed here after all.
    ///
    /// **Clear by default, and the only reason a folder ever moves.** Dropping a universe
    /// is not a request to rearrange a library, and a series quietly leaving the place a
    /// reader put it is exactly the surprise nothing afterwards would explain. The answer
    /// travels with the commit, so it can be changed right up to the moment the folder
    /// goes.
    Q_INVOKABLE void setFiling(int row, const QString &workId, bool filing);

    /// Says whether one volume may land on the one the library already holds. `move`'s twin,
    /// and for the same reason: the commit installs over nothing it was not told to.
    Q_INVOKABLE void setReplacing(int row, const QString &path, bool replacing);

    /// Says whether one declaration may be written over the one the library already holds.
    /// `replace`'s twin one floor up: a `work.json` carries a title and a summary somebody
    /// may have edited through the API, and dropping the folder again is not a request to
    /// undo that.
    Q_INVOKABLE void setDeclaring(int row, const QString &path, bool declaring);

    /// The three things a reader can tick, and the one question asked of all of them.
    ///
    /// The setters stay apart: each says what it costs in its own words, and a `.qml` file
    /// reads the name it calls. Reading one back is the same question three times over —
    /// « is this one ticked » — and three methods for it were three spellings of one line.
    enum class Choice { Filing, Replacing, Declaring };
    Q_ENUM(Choice)

    Q_INVOKABLE bool ticked(int row, Choice what, const QString &key) const;

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
    /// A card has reached the end of its road, one way or the other. What warns listens to
    /// this and not to `changed`: a card moves a dozen times on its way and only the last of
    /// those is news — it is the commit that warns, not each byte.
    void settled(bool went, const QString &subject, const QString &said);

private:
    /// The carrier reaches into `m_rows` and into the model's own `beginInsertRows` and
    /// `dataChanged`, because it is what changes a card. One list, one object drawing it.
    friend class Carrier;

    /// The one copy of the cards, in the order they were dropped. `Carrier` reaches in
    /// here rather than keeping a second list, which is what that friendship buys.
    QList<Card> m_rows;
    bool m_verifying = true;
    /// The one folder actually on the pool right now, or `0` — no card's own token, since
    /// `Carrier`'s counter starts at `1`. A dropped shelf of a dozen series queues every
    /// one of them at `Stage::Asking`, and only this one has a walk under way — the other
    /// eleven are not stuck, they are waiting their turn, and showing them a count frozen
    /// at zero would say the opposite of that.
    quint64 m_walkingToken = 0;
    /// Set when the queue stopped for a reason that is not one card's fault. Written by
    /// `Carrier::retryLater`, read by the property above.
    QString m_trouble;
    /// Parented to this, so it goes when the model does.
    Carrier *m_carrier;
};
