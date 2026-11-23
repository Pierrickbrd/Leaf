#pragma once

// The queue of files on their way into the library, as a list a screen can draw: the cards,
// the drops they were read out of, and the roles a `.qml` delegate binds to.
//
// **Why the import is five classes.** `Imports` was one class of ninety-four methods —
// `cpp:S1448` for the third time, after `Card` and `Carrier` had each been carried out of it
// — because everything about an import fitted there: the list, what a reader corrects on
// it, what moves along it, what the bar and the banner count, and what brings a file in.
// Those are five subjects, and each is now a class a new method can be filed under:
//
// - this, the list itself and the few counts the checking frame reads off it;
// - `Imports`, what a reader says of a card before it goes — a role, a value, a tick;
// - `ImportQueue`, what lets the cards go, pauses them and starts them again;
// - `ImportTally`, what the bar's button and the banner of the transfers count;
// - `ImportOffers`, what brings a dropped path onto the list, or aims it at a series.
//
// **`Imports` derives from this rather than holding it**, and only those two: the checking
// frame (`Review`), the order editor and `Carrier` reach the list and its corrections through
// one `Imports *`, and the model a `.qml` file names is the same object that takes the
// corrections — a list drawn by one object and corrected through another is two objects that
// must agree on which row is which. The other three hold an `Imports *`, as `ImportCaptions`
// does.
//
// **One transfer at a time.** Two on a domestic line steal each other's bandwidth and both
// finish later than they would have in series.
//
// **Pause yields a place, and nothing takes it back but its reader.** That is the rule the
// whole queue turns on: pausing the one in flight lets the next that was let go go ahead,
// and a paused card stays paused until somebody sends it again. It used to start again by
// itself once the one ahead was done, which is not a pause at all — it is a queue with an
// extra step, and a reader who had stopped a transfer for the evening found it running at
// midnight. Stopping for good is `cancel`, and it is a different button because it is a
// different decision: the server cleans its copy up and the line goes back to zero. `Carrier`
// is where all of that is written.

#include "Card.h"
#include "Server.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QQmlEngine>
#include <QSettings>
#include <QString>
#include <QStringList>

class Carrier;
class Imports;
class ImportOffers;
class ImportQueue;
class ImportTally;

namespace Lines {
/// What one card would put on the wire: the files of a folder that the library does not
/// already hold, or the file itself. A role of the list and a sum of `ImportTally`, which say
/// the same thing because they are the same sum.
int filesToSend(const Card &card);
qint64 bytesToSend(const Card &card);
} // namespace Lines

class ImportRows : public QAbstractListModel
{
    Q_OBJECT
    QML_ANONYMOUS

    Q_PROPERTY(int count READ count NOTIFY changed)
    /// How many are waiting for an answer. Half of what the bar's button says: a transfer that
    /// finished and a question nobody saw look the same from a closed dialog.
    Q_PROPERTY(int deciding READ deciding NOTIFY changed)
    /// How many cards stopped to be checked. Apart from `deciding`: those wait for a tick
    /// on a series that would move, these for a look at values nobody said.
    Q_PROPERTY(int reviewing READ reviewing NOTIFY changed)
    /// How many readings of the shape « Importer » would confirm: each card that a guessed
    /// folder role, or a series whose title is still to give, asks about, and each archive at
    /// a universe's root whose role is still a guess. A role chosen asks nothing any more.
    /// Counted on what travels: a reading of a series the library holds whole confirms
    /// nothing.
    Q_PROPERTY(int readings READ readings NOTIFY changed)
    /// The folders whose walk is over and the ones whose walk is not, failures in neither:
    /// at fifty series the walk outlasts the opening of the frame, which has to say how far
    /// it has come.
    Q_PROPERTY(int foldersRead READ foldersRead NOTIFY changed)
    Q_PROPERTY(int foldersToRead READ foldersToRead NOTIFY changed)
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
        ReadHow,
        Progress,
        Volumes,
        Bytes,
        /// What the third step's line says under the name, and whether it is an alarm.
        About,
        Alarm,
        /// How far the card has gone, 0 to 1, and whether it was let go.
        Fraction,
        Released,
        /// Whether the third step draws the card at all (`Card::shown`).
        Shown,
    };
    Q_ENUM(Role)

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_rows.size()); }
    int deciding() const;
    int reviewing() const;
    int readings() const;
    int foldersRead() const;
    int foldersToRead() const;
    QString trouble() const { return m_trouble; }
    bool verifying() const { return m_verifying; }
    void setVerifying(bool verifying);

    /// The cards themselves, read-only, for what reads a card's sheets rather than the
    /// roles a delegate binds to.
    const QList<Card> &cards() const { return m_rows; }

    /// The drop a card was read out of (`Card::Road::drop`), or null once its last card is
    /// gone. A pointer into the model's own list: valid until the next change to the queue.
    const Shape::Drop *dropOf(quint64 token) const;

signals:
    void changed();
    /// A walk has counted another volume, or a folder has said how many it holds.
    void progressed();
    /// A card has reached the end of its road, one way or the other. What warns listens to
    /// this and not to `changed`: a card moves a dozen times on its way and only the last of
    /// those is news — it is the commit that warns, not each byte.
    void settled(bool went, const QString &subject, const QString &said);

protected:
    /// Only as `Imports`: there is one list, and it is the one the corrections are made on.
    explicit ImportRows(Server *server, QObject *parent = nullptr);

private:
    /// The sessions this client opened on a server and has sent nothing to, kept in a file so
    /// that a quit application can remove them at its next start.
    ///
    /// The contract says « Nothing removes one on its own », and a session is a place reserved
    /// on the library's disk: an application closed between the announcement and the first
    /// byte left it there for ever, one box per folder that was dropped. Only these are
    /// dropped, and only these are safe to drop: opened by this client, holding nothing, and
    /// with nothing left that would pick them up again. One that may hold bytes — forgotten
    /// the moment the first byte leaves, before the server has answered — is worth what it
    /// holds (`GET /import`) and is never touched.
    void remember(const QString &id);
    /// Forgets `id` under the current address; a no-op, and no write, for one never kept —
    /// called before every chunk is sent, a write each would be two thousand for a large
    /// folder.
    void forget(const QString &id);
    /// `DELETE /import/{id}` for every id kept under the current address, each forgotten when
    /// the server says it is gone or not ours (success, 404, 403) and kept for the next start
    /// on any other answer, including none.
    void dropWhatWasLeft();
    QString kept(const QString &id) const;

    /// Lets go of every drop no card reads any more.
    void forgetDropsWithoutCards();

    /// The carrier reaches into `m_rows` and into the model's own `beginInsertRows` and
    /// `dataChanged`, because it is what changes a card; the four classes of the import reach
    /// in for the same reason. One list, one object drawing it.
    friend class Carrier;
    friend class Imports;
    friend class ImportOffers;
    friend class ImportQueue;
    friend class ImportTally;

    /// The one copy of the cards, in the order they were dropped. `Carrier` reaches in
    /// here rather than keeping a second list, which is what that friendship buys.
    QList<Card> m_rows;
    /// Set while a batch of corrections is applied: `Carrier::announce` says nothing then,
    /// and the batch says it once when it ends.
    bool m_quiet = false;
    /// Every dropped folder that still has a card, its roles as they now stand. Kept because
    /// a role is a fact about the drop and not about one card: turning a shelf into a
    /// universe makes one card out of two, and only the drop knows both.
    QList<Shape::Drop> m_drops;
    /// Handed to the next drop. Only ever grows, for `Card::token`'s reason: a drop forgotten
    /// and dropped again must not answer to a change aimed at the old one.
    quint64 m_nextDrop = 1;
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
    Server *m_server;
    QSettings m_file{QSettings::IniFormat, QSettings::UserScope, QStringLiteral("Leaf"),
                     QStringLiteral("preferences")};
    /// `"<address> <id>"`, as written in the file.
    QStringList m_left;
    /// Parented to the model, so it goes when the model does. Made by `Imports`, which is
    /// the type it carries for.
    Carrier *m_carrier = nullptr;
};
