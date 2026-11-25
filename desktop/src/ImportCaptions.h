#pragma once

// What the import dialog's frame says, as opposed to what it is doing to a file.
//
// The frame, and only the frame: its title, the bar's count, the three steps and the buttons
// that move between them. What each step says inside itself lives beside it —
// `DropCaptions` for the choosing, `ReviewCaptions` and `SheetCaptions` for the checking,
// `RecapCaptions` for the sending — and what one card of the queue says about itself is
// `CardCaptions`. This class reached a hundred and three methods holding all five, Sonar's
// `cpp:S1448` on a class that had already been split twice for the same defect: off
// `Imports`, where twenty-eight of these hung beside the transfer queue, and then off itself
// into `CardCaptions` at forty-one. Each time the cure was a second class, and each time the
// first one grew back, because "what the import says" is not a subject — every step of the
// dialog said something, so everything fitted. One class per step is a subject a new word
// can be filed under without growing this one.
//
// Nothing here decides anything. `Words` holds the French and the typography; `Imports` holds
// the queue; this binds the two and re-announces itself when `Imports` changes, so a QML
// binding on `waitingLabel` — which quotes however many files are still waiting for an answer
// — is refreshed by the same change that refreshed the count it quotes. It is the only one of
// the five that needs `Imports` at all, which is why it is the one that keeps the name.

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>

class QJSEngine;
class QQmlEngine;
class Imports;

class ImportCaptions final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// The dialog's own French. Here rather than in the QML for the reason `Words.h` gives
    /// at the top of itself: a string in a `.qml` file is a string nobody tests, and these
    /// carry the typography — a non-breaking space, a real ellipsis, an accented capital.
    Q_PROPERTY(QString title READ title CONSTANT)
    /// What the bar's button says while the dialog is shut. A transfer that finished and a
    /// question nobody saw look the same from there, so both are counted.
    Q_PROPERTY(QString waitingLabel READ waitingLabel NOTIFY changed)
    /// The three steps over the window, in their order.
    Q_PROPERTY(QStringList stepLabels READ stepLabels CONSTANT)
    /// The way on from the first step and from the second.
    Q_PROPERTY(QString nextLabel READ nextLabel CONSTANT)
    /// The way back from the second step and from the third.
    Q_PROPERTY(QString previousLabel READ previousLabel CONSTANT)
    Q_PROPERTY(QString cancelLabel READ cancelLabel CONSTANT)
    /// The frame's button when there is nothing left to import.
    Q_PROPERTY(QString closeLabel READ closeLabel CONSTANT)
    Q_PROPERTY(QString backLabel READ backLabel CONSTANT)
    /// The frame's way back to the cards, which gives nothing up — not `backLabel`, which does.
    Q_PROPERTY(QString returnLabel READ returnLabel CONSTANT)
    Q_PROPERTY(QString pauseLabel READ pauseLabel CONSTANT)
    Q_PROPERTY(QString resumeLabel READ resumeLabel CONSTANT)
    Q_PROPERTY(QString abandonLabel READ abandonLabel CONSTANT)
    /// The third step's button, the one that starts every line, and what it becomes after.
    Q_PROPERTY(QString sendLabel READ sendLabel CONSTANT)
    Q_PROPERTY(QString finishLabel READ finishLabel CONSTANT)
    /// On the sheet of a card refused: what takes it out of the queue.
    Q_PROPERTY(QString removeLabel READ removeLabel CONSTANT)
    Q_PROPERTY(QString emptyLabel READ emptyLabel CONSTANT)

public:
    explicit ImportCaptions(Imports *imports, QObject *parent = nullptr);

    static ImportCaptions *create(QQmlEngine *engine, QJSEngine *);

    QString title() const;
    QString waitingLabel() const;
    QStringList stepLabels() const;
    QString nextLabel() const;
    QString previousLabel() const;
    QString cancelLabel() const;
    QString closeLabel() const;
    QString backLabel() const;
    QString returnLabel() const;
    QString pauseLabel() const;
    QString resumeLabel() const;
    QString abandonLabel() const;
    QString sendLabel() const;
    QString finishLabel() const;
    QString removeLabel() const;
    QString emptyLabel() const;
    /// What the foot offers instead of emptying the queue when the dialog was aimed at one
    /// thing: another file for it.
    Q_INVOKABLE QString anotherFileLabel() const;

    /// The caption of a reimport, from what the menu aimed it at: « Réimporter le tome 2 ».
    Q_INVOKABLE QString reimportTitle(const QString &kind, const QString &name) const;
    /// What is held of what it aims at, under that title; empty when nothing is known of it.
    Q_INVOKABLE QString reimportHolds(const QString &kind, int volumes) const;
    Q_INVOKABLE QString reimportUniverseLabel(const QString &name) const;

signals:
    void changed();

private:
    Imports *m_imports;
};
