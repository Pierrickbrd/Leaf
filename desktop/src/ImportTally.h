#pragma once

// What the import counts for the screens that watch it: the bar's button while the dialog is
// shut, the line under the drop zone while the walks run, and the banner of the transfers.
//
// Every one of these is a sum over the cards of `Imports`, asked again whenever the list says
// it moved; none of them decides anything. Why they are a class of their own is written at
// the top of `ImportRows.h`.

#include <QObject>
#include <QQmlEngine>
#include <QString>

class Imports;

class ImportTally final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// How many are still to go — anything not filed and not failed. What the bar's button
    /// shows while the dialog is shut.
    Q_PROPERTY(int inFlight READ inFlight NOTIFY changed)
    /// How many are still being looked at — a folder being walked, a file the server has
    /// not answered about yet.
    ///
    /// The dialog opened from the bar reads this: something still looked at is something to
    /// show as it is proposed, not a transfer to watch.
    Q_PROPERTY(int reading READ reading NOTIFY changed)
    /// How many cards « Importer » let go, whether they are moving, done or failed since. The
    /// dialog opened from the bar reads this to tell a transfer to watch from a drop still
    /// being prepared: a read that finished while the window was shut is not a transfer.
    Q_PROPERTY(int launched READ launched NOTIFY changed)
    /// How many cards stopped on what a sheet lacks — a required value missing, two files at
    /// one number — and so cannot be made ready (`ImportQueue::prepare`). Not
    /// `Imports::reviewing`, which also counts the card corrected and waiting for « Importer »
    /// to be announced again.
    Q_PROPERTY(int blocked READ blocked NOTIFY changed)
    /// The volumes read so far over the volumes the drop holds, folders that did not fail only
    /// — what the line under the drop zone counts while the walks run. Their own signal and
    /// not `changed`: a count moves once per volume, and everything that listens to `changed`
    /// rebuilds something heavier than a number.
    Q_PROPERTY(qint64 volumesChecked READ volumesChecked NOTIFY progressed)
    Q_PROPERTY(qint64 volumesToCheck READ volumesToCheck NOTIFY progressed)
    /// How many cards are on the list and did not fail: the series the drop would import.
    Q_PROPERTY(int listed READ listed NOTIFY changed)
    /// What the drop would send and what the library already holds of it, over every card
    /// that did not fail: the files not in `alreadyThere` with their bytes, and the ones in
    /// it. A card not yet answered counts all it holds as to send — it has not been told
    /// otherwise — and the figures settle as the answers land.
    Q_PROPERTY(int toSendFiles READ toSendFiles NOTIFY changed)
    Q_PROPERTY(qint64 toSendBytes READ toSendBytes NOTIFY changed)
    Q_PROPERTY(int heldFiles READ heldFiles NOTIFY changed)
    /// The first drop the library already holds part of, said by name — empty when none.
    Q_PROPERTY(QString heldNote READ heldNote NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    /// Whether a line of the third step has gone: let go, moving, stopped part-way, done or
    /// failed after leaving. What turns « Prêt à envoyer » into « Envoi en cours », and lets the
    /// frame be left on « Suivant » with nothing to confirm — a line put back to zero is not one.
    Q_PROPERTY(bool started READ started NOTIFY changed)
    /// How many lines « Importer tout » would let go: not moving, not done, not waiting
    /// their turn behind one that is.
    Q_PROPERTY(int startable READ startable NOTIFY changed)
    /// The lines drawn, every one has either arrived or failed after leaving.
    Q_PROPERTY(bool finished READ finished NOTIFY changed)
    /// What the banner of the transfers is about: the volumes and the bytes of the lines that
    /// were launched, and of those alone. `toSendFiles` and `toSendBytes` hold every line, which
    /// is what the recap says before anything goes.
    Q_PROPERTY(int sendingFiles READ sendingFiles NOTIFY changed)
    Q_PROPERTY(qint64 sendingBytes READ sendingBytes NOTIFY changed)
    /// The lines drawn, every one has arrived: nothing is left to send, send again or look at.
    /// What turns « Importer tout » into « Terminer ».
    Q_PROPERTY(bool completed READ completed NOTIFY changed)
    /// How many lines failed after leaving, and what has arrived of the whole: the volumes
    /// and the bytes. The banner says them while the lines move.
    Q_PROPERTY(int failedLines READ failedLines NOTIFY changed)
    Q_PROPERTY(int sentFiles READ sentFiles NOTIFY progressed)
    Q_PROPERTY(qint64 sentBytes READ sentBytes NOTIFY progressed)

public:
    explicit ImportTally(Imports *imports, QObject *parent = nullptr);

    static ImportTally *create(QQmlEngine *engine, QJSEngine *);

    int inFlight() const;
    int reading() const;
    int launched() const;
    int blocked() const;
    qint64 volumesChecked() const;
    qint64 volumesToCheck() const;
    int listed() const;
    int toSendFiles() const;
    qint64 toSendBytes() const;
    int heldFiles() const;
    QString heldNote() const;
    bool busy() const;
    bool started() const;
    int startable() const;
    bool finished() const;
    int sendingFiles() const;
    qint64 sendingBytes() const;
    bool completed() const;
    int failedLines() const;
    int sentFiles() const;
    qint64 sentBytes() const;

signals:
    /// Said whenever `Imports` says it, and for the same reasons.
    void changed();
    void progressed();

private:
    Imports *m_imports;
};
