#pragma once

// What a reader says of a card before it goes: a role given to a folder, a value corrected,
// a series ticked to move, a volume allowed to land on the one the library holds.
//
// The list itself is `ImportRows`, which this derives from, and why the import is five
// classes is written at the top of `ImportRows.h`. This is the one a `.qml` file names as
// the model, and the one `Review`, the order editor and `Carrier` hold: every correction is
// made on the list that draws it.
//
// **A correction is refused once the card has gone** (`Carrier::correctable`): the bytes on
// their way carry what was agreed to, and a value changed under them could only reach the
// library by an upload nobody asked for.

#include "Api.h"
#include "Card.h"
#include "ImportRows.h"
#include "Server.h"

#include <QJsonValue>
#include <QList>
#include <QQmlEngine>
#include <QString>

#include <functional>

class Imports : public ImportRows
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit Imports(Server *server, QObject *parent = nullptr);

    static Imports *create(QQmlEngine *engine, QJSEngine *);

    /// Gives the folder or archive at `at` — relative to the dropped folder — the role
    /// `role`, and reads again what that changes (`Shape::apply`).
    ///
    /// Inside one card, its sheets are read again from the walk already made, the
    /// corrections whose field still exists put back, and the card is edited: « Importer »
    /// announces it again, the way it carries a correction. When the cards themselves change
    /// — a shelf turned universe, a universe turned shelf — the cards touched are abandoned
    /// and the new ones read as a drop is.
    ///
    /// False, and nothing changed, when `Shape` refuses the role or when a card it would
    /// touch can no longer be corrected (`Carrier::correctable`).
    bool setRole(quint64 drop, const QString &at, Shape::Role role);

    /// The same for a whole reading of a folder (`Shape::applyReading`).
    bool setReading(quint64 drop, const QString &at, int index);

    /// Whether `setRole` would refuse `role` at `at` only because a card it touches cannot be
    /// corrected now — still announced, or gone: the role is possible, the cards are not.
    bool roleWaits(quint64 drop, const QString &at, Shape::Role role) const;

    /// What asking `roleWaits` cost, for a test to assert on, as `Review::cost` does: the
    /// roles tried on a copy of a whole drop.
    struct Cost {
        qint64 trials = 0;
    };
    static Cost cost();

    /// Corrects one value of one card: the field `key` of the sheet at `at` (`Facts::edit`).
    ///
    /// By `token` and not by row, for the reason `Card::token` gives: a card abandoned and
    /// redropped under the same row must not receive a correction typed against the old one.
    /// Only while nothing has gone — `Reviewing`, `Deciding` or `Ready`, and not `Ready`
    /// once « Importer » let it go to wait its turn (`Carrier::correctable`) — and a card
    /// that was ready goes back to `Reviewing`: the announcement it was ready with no longer
    /// says what will be written. Nothing is sent: a correction travels with « Importer ».
    /// Nor is anything changed by the value a field already holds (`Facts::edit`).
    bool edit(quint64 token, const QString &at, const QString &key, const QJsonValue &value);

    /// One correction of a batch, what the single form takes.
    struct Edit {
        quint64 token = 0;
        QString at;
        QString key;
        QJsonValue value;
    };
    /// Each correction as the single form makes it — the same refusals, the same
    /// `corrections`, the same return to `Reviewing` — and the queue said to change once, at
    /// the end, when one was taken. How many were taken.
    int edit(const QList<Edit> &edits);

    /// Whether the asking profile starts the volume landing at `path` again from its first
    /// page. Travels with the commit, and only when asked. False when the card can no longer
    /// be corrected (`Carrier::correctable`).
    bool setResetting(quint64 token, const QString &path, bool reset);

    /// Whether the value the library holds under `key` of the declaration at `sidecar` stays
    /// over the one the drop brings. A correction like any other: the card goes back to
    /// `Reviewing` and « Importer » announces it again with the library's value in place of
    /// the drop's.
    bool keep(quint64 token, const QString &sidecar, const QString &key, bool keep);

    /// Announces the card again now, for a range whose effect on its volume the server is the
    /// one to say: `confirm` would do it too, but only after the reader has pressed the button
    /// that sends, over a note about the range they had before.
    bool askAgain(quint64 token);

    /// The reader wants a second series for the folder `at` of the card, whatever looks like
    /// it in the library: the card stops asking about it.
    bool chooseSecond(quint64 token, const QString &at);

    /// The pages of the file that replace the chapter aimed at, counted from 0. A correction
    /// like `keep`: nothing is uploaded until « Importer ».
    bool setSplice(quint64 token, int from, int to);

    /// The name the reader typed for an edition the import adds (`@current` for the one the
    /// series holds): kept on the card, and the folder announced again with it.
    bool setEditionName(quint64 token, const QString &path, const QString &name);

    /// The image the reader chose as the cover of the folder `at` of the card, from their
    /// disk; empty to go back to the one the folder holds. Announced again with it.
    bool setCover(quint64 token, const QString &at, const QString &source);

    /// Aims the card at another `{kind, id}` — a series that `resembles` the one the drop
    /// would create, another chapter of the same volume — from the walk already made: the
    /// session is dropped and the folder announced again, the files not read a second time.
    /// `at` is the folder of the drop the answer named, and only the card's own root, `""`,
    /// can be aimed elsewhere.
    bool retarget(quint64 token, const QString &at, const QString &kind, const QString &id);

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

    /// Out of the queue, and the server told to clean up its copy. A refusal is this card's
    /// own fact: it comes back as itself, `Stage::Failed`, rather than a queue-wide
    /// `trouble` that would stop every other row from sending over one session the server
    /// would not drop.
    Q_INVOKABLE void abandon(int row);

private:
    /// `ImportOffers` adds the cards a drop reads through `addCard`, the one road onto the list
    /// that a role changed later takes too.
    friend class ImportOffers;

    /// One card of `drop` rooted at `within`, unless a card already reads that folder; a
    /// second folder of the same name in one drop is posed failed (`Words::sameDestination`).
    /// True when a card of the drop was added.
    bool addCard(const Shape::Drop &drop, const QString &within);

    /// What `setRole` and `setReading` share: `change` is tried on a copy of the drop, and
    /// what it does to the cards decides the rest.
    bool reshape(quint64 token, const std::function<bool(Shape::Drop &)> &change);
    /// `change` tried on a copy of the drop into `trial`, and the rows it would read again or
    /// abandon; true when `Shape` takes it and every one of those rows can be corrected.
    template <typename Change>
    bool tried(quint64 token, const Change &change, Shape::Drop &trial, QList<int> &reread,
               QList<int> &gone) const;

    /// One correction of `edit`'s batch, said by nobody: the batch says it once.
    bool editOne(quint64 token, const QString &at, const QString &key, const QJsonValue &value);
};
