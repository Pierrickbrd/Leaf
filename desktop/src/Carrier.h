#pragma once

// What actually moves a card from a dropped path to a filed volume.
//
// `Imports` is the list a screen draws; this is what happens to one line of it. The two
// were one class of sixty-one methods, and the split is along the seam the header of
// `Imports.h` already described: a queue with one slot, two roads and retries on one side,
// and a model a `.qml` file binds to on the other. Neither half was easy to find inside the
// other, and `cpp:S1448` had been saying so.
//
// **It holds the model, not the cards.** Every insert, removal and change still goes
// through the one object that draws them — this reaches into `Imports` for the list rather
// than keeping a second copy of it, because two lists of the same cards is how one of them
// goes stale. That is what the `friend` declaration in `Imports.h` is for, and it is the
// whole of what the two share.
//
// Everything here is either the disk (walking a dropped folder, off the thread that draws
// the window) or the server (announcing, sending by the chunk, committing, retrying). The
// rule the whole thing turns on is still the one `Imports.h` states: there is exactly one
// slot, pause yields it, and a paused card waits for its reader — except one counting
// seconds towards a retry, which the timer sets going again.

#include "Card.h"
#include "Manifest.h"
#include "Server.h"

#include <QObject>
#include <QString>
#include <QTimer>

class Imports;

/// What a card's own fields say about it — whether it travels, is held back, can still be
/// corrected — read off the card alone: no queue, no server, no model.
///
/// A base of `Carrier` and not a part of it because `cpp:S1448` counted forty-two methods on
/// the carrier, and these seven are the ones that need none of its state. Public, so that
/// `Carrier::travels` still names this one function from `Imports`, `Review` and the tests
/// that read it: a second name for the same answer is how two callers drift apart.
struct Cargo
{
    /// Whether a sheet will reach the library at all: false for a file the server said it
    /// already holds, and for a container holding only such files whose own sidecar is not
    /// written — neither created by the server nor a differing declaration a reader ticked. What decides that a card stops (`tookFolder`), what decides that
    /// it is held back (`Imports::confirm`) and what the checking frame calls identical
    /// (`Review`) read this one answer — read apart, a duplicate number on two files already
    /// there kept a card in `Reviewing` that nothing could ever let go.
    static bool travels(const Card &card, const Facts::Sheet &sheet);

    /// The files of `tree.files` that `travels` has walked since the application started,
    /// for a test to assert a cost on, as `Review::cost` does.
    static qint64 looked();

    /// Whether this card has started going up: a byte sent, a file finished. A correction
    /// made after that could only reach the library by an upload nobody asked for.
    static bool startedSending(const Card &card);

    /// Whether a sheet of what the card would send holds it back (`Facts::blocks` on what
    /// `travels`): the one filter `Imports::confirm` leaves a card in `Reviewing` by, and
    /// the one the frame reads to say « Importer » can let it go.
    static bool heldBack(const Card &card);

    /// Whether a card may still be corrected: not yet sending, and not let go by
    /// « Importer » to wait its turn — corrected there, it went back to `Reviewing` behind
    /// a frame opened on the transfers, where nothing offers to import it again.
    /// `Imports::edit` refuses by it and the frame draws its sheet read-only by it.
    static bool correctable(const Card &card);

    /// Reads a walked card's sheets from its node, its tree and its roles, then puts back each
    /// of its corrections whose field still exists, forgetting the others. No disk: the walk
    /// is already in `tree`. What `described` does once and a role change does again.
    static void readSheets(Card &card);

protected:
    /// Whether a sheet of this card that will travel stops the checking step.
    static bool asksToBeChecked(const Card &card);
};

class Carrier final : public QObject, public Cargo
{
    Q_OBJECT

public:
    using Stage = Card::Stage;

    explicit Carrier(Imports *of, Server *server);

    // ——— What the model asks of it —————————————————————————————————————————

    /// Whether a folder is on the pool right now. `Imports::reading` counts it: a folder
    /// queued behind another is waiting its turn, not being looked at.
    bool walkingAFolder() const { return m_describing; }

    /// Out of the queue, and the server told to clean up its copy — which is the point,
    /// not the card disappearing. Here and not on the model because the cleanup is a
    /// request, and the model makes none.
    void abandon(int row);

    void ask(int row);

    /// A row for one thing `offer` found, folder or file — the model insert lives here once
    /// rather than twice.
    void appendRow(const QString &path, const QString &name, bool folder, qint64 size);

    /// The next queued folder, and only one at a time.
    ///
    /// Twelve walks over the disk at once would get in each other's way, and the first card
    /// would be ready no sooner for it — so `offer` hands folders to this one at a time
    /// rather than starting every `describe` at once.
    void describeNext();

    /// Starts the next card that was let go (`Card::Road::released`), or `first` before the
    /// others when it is one. Cards not let go stay where they are: it is what makes the
    /// third step's lines wait for their button.
    void pump(int first = -1);

    /// Lets the card at `row` go, as the reader asked: ready if it was stopped or failed,
    /// and — when `now` — before the others, whoever holds the slot stepping behind.
    void release(int row, bool now);

    /// Puts a card back to zero for `Imports::cancel`: its session dropped on the server and
    /// the card announced again, still on the list.
    void restart(int row);

    void settle(int row, Stage stage, const QString &trouble = {});

    /// Moves a row, telling the model. The queue's order is the model's order, so that a
    /// screen never has to sort what it is handed.
    int rowOf(const QString &path) const;

    int holding() const;

    void announce(int row);

    /// Drops the card's session and announces the folder again, carrying what was corrected
    /// since. `DELETE /import/{id}` first: the server remembers the rewrites a session was
    /// announced with. A refusal fails the card, as `abandon` does.
    void reannounce(int row);

    // ——— And what it does with a card once it has one ——————————————————————
    //
    // Nothing outside reaches these: a screen answers a question or presses a button, and
    // every one of them follows from that. The two roads, the walk and the retry are this
    // object's own business.
private:
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

    void tookFolder(const QString &path, const Server::Answer &answer);

    /// A card whose session the server would not drop, put back as the failed row it now
    /// is. `abandon` and `reannounce` both end here when their `DELETE` is refused and the
    /// row is already gone: the session it named is still on the server, and a refusal said
    /// to nobody is the box of bytes nothing afterwards would sweep up.
    void cameBack(Card kept);

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

    void sendMore(int row);

    void confirm(int row);

    /// A transfer that did not go: waited on and tried again, unless `answer` says waiting
    /// will not mend it.
    void retryLater(int row, const Server::Answer &answer);

    /// Only `describe`, `described` and `hashedSoFar` use this — the rest of the queue
    /// still finds its row by `path`, matching every request the server itself tracks by
    /// path. Narrowing the walk alone to `token` is the whole of what this round fixes;
    /// widening it to every network callback here would be a much bigger change than one
    /// commit correcting the checking count should carry.
    int rowOfToken(quint64 token) const;

    /// The model this carries for, and the one copy of the cards. Never null: it is the
    /// object that made this one, and it outlives it by construction.
    Imports *m_of;
    Server *m_server;
    /// Set while a folder is being walked, so `describeNext` never starts a second one.
    bool m_describing = false;
    /// Handed to the next card `appendRow` creates. Only ever grows, so no two cards —
    /// including one dropped after an identically-named one was abandoned — ever compare
    /// equal.
    quint64 m_nextToken = 1;
    QTimer m_retry;
};
