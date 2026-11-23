#pragma once

// What lets the cards of `Imports` go, and what stops them: « Importer », the round button of
// each line, pause and resume, cancel, and the × that gives up whatever is still being set up.
//
// The rule it turns on is the one `ImportRows.h` states — one slot, pause yields it, and a
// paused card waits for its reader — and `Carrier` is where it is carried out. This only
// decides which cards a press concerns. Why it is a class of its own is written at the top of
// `ImportRows.h`.

#include <QObject>
#include <QQmlEngine>

class Imports;

class ImportQueue final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit ImportQueue(Imports *imports, QObject *parent = nullptr);

    static ImportQueue *create(QQmlEngine *engine, QJSEngine *);

    /// « Importer » over the checking step: every card in `Reviewing` that no sheet blocks
    /// is confirmed — re-announced when it was corrected, let go otherwise — and the queue
    /// starts. A blocked series (a required value missing, two files at one number) stays,
    /// and the others do not wait for it.
    Q_INVOKABLE void confirm();

    /// « Suivant » over the checking step: every card in `Reviewing` that no sheet blocks and
    /// that nobody corrected becomes what `confirm` would make it — `Ready`, or `Deciding`
    /// for a series that would move — and nothing is let go. The press that starts the queue
    /// is « Importer », one step on, so a card is still corrected from there (`Carrier::
    /// correctable`) and the checking step is still one « Précédent » away. A card corrected
    /// stays `Reviewing`: its re-announcement is the only thing that starts by itself, and
    /// it waits for « Importer » as well.
    Q_INVOKABLE void prepare();

    /// Throws away everything still being set up, and leaves alone whatever is moving.
    ///
    /// What the × means. The server is told about each, because it keeps what nobody named
    /// — a place reserved and then forgotten holds the library's own disk for good.
    Q_INVOKABLE void giveUpPreparing();

    /// Everything answered goes into the queue, and the first of them starts. What is ready
    /// is confirmed as it goes in: let go, it waits its turn and is no longer corrected.
    Q_INVOKABLE void send();

    /// The lines that arrived leave the queue; the others stay where they are. Said when the
    /// window is shut, so that it reopens on what is left and not on a send that is over.
    Q_INVOKABLE void clearFiled();

    /// Lets one line go, the round button of the third step: whatever its state asks.
    /// A card still to be checked is agreed to first (`confirm` for that one card); a card
    /// that was let go and stopped, or failed, is sent again. It takes the slot now, and
    /// whoever held it waits its turn behind — which is not a pause.
    Q_INVOKABLE void start(int row);

    /// « Importer tout »: every line `ImportTally::startable` counts goes, in the order of the
    /// list and one at a time, none of them taking the slot from one already moving.
    Q_INVOKABLE void startAll();

    /// Yields the one slot: the file in flight steps back and the next one let go takes over.
    /// The card stays paused until `resume`, however long that is.
    Q_INVOKABLE void pause(int row);

    /// Takes the slot back. Whatever held it steps behind and waits its turn.
    Q_INVOKABLE void resume(int row);

    /// Puts a line back to zero and keeps it on the list: the server is told to clean up its
    /// copy, the card is announced again, and it waits for its reader to send it. What
    /// `Imports::abandon` does, except that the line stays.
    Q_INVOKABLE void cancel(int row);

private:
    Imports *m_imports;
};
