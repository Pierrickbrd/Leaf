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
// slot, pause yields it, and a paused card takes it back by itself — except one counting
// seconds towards a retry, which `pump` steps over.

#include "Card.h"
#include "Manifest.h"
#include "Server.h"

#include <QObject>
#include <QString>
#include <QTimer>

class Imports;

class Carrier final : public QObject
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

    void pump();

    void settle(int row, Stage stage, const QString &trouble = {});

    /// Moves a row, telling the model. The queue's order is the model's order, so that a
    /// screen never has to sort what it is handed.
    int rowOf(const QString &path) const;

    int holding() const;

    void announce(int row);

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

    void retryLater(int row);

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
