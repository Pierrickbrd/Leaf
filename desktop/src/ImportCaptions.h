#pragma once

// What the import dialog says, as opposed to what it is doing to a file.
//
// The dialog, and only the dialog: what one card of the queue and one line of its tree say
// about themselves is `CardCaptions`, split off when this class in its turn reached
// forty-one methods — the very defect the paragraph below describes, repeated one floor
// down. Everything left here is a constant read once when the screen is built, plus the one
// count that moves; everything there is an answer about one thing being imported.
//
// These twenty-eight methods used to hang off `Imports` itself, beside the transfer queue,
// under a block of `Q_PROPERTY` that only ever called `Words`. Measured before this split:
// `Imports.h` declared a hundred and five members and `Imports.cpp` ran to nearly thirteen
// hundred lines, forty-three of them a call to `Words::` and nothing else — Sonar's
// `cpp:S1448` on a class whose actual job, a queue of transfers with one slot, two routes and
// retries, was only half of what the file held.
//
// `Captions.h` names the same shape of defect on `Search` — "the class carried fifty-six
// methods, half of them captions, and neither half was easy to find inside the other" — and
// this follows that file rather than growing into it: pouring the import's words into
// `Captions` would have made one object carry two screens with nothing in common, which is
// the defect `Captions` exists to avoid repeating.
//
// Nothing here decides anything. `Words` holds the French and the typography; `Imports` holds
// the queue; this binds the two and re-announces itself when `Imports` changes, so a QML
// binding on `waitingLabel` — which quotes however many files are still waiting for an answer
// — is refreshed by the same change that refreshed the count it quotes.

#include <QObject>
#include <QQmlEngine>
#include <QString>

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
    Q_PROPERTY(QString dropHereLabel READ dropHereLabel CONSTANT)
    Q_PROPERTY(QString dropHowLabel READ dropHowLabel CONSTANT)
    Q_PROPERTY(QString cancelLabel READ cancelLabel CONSTANT)
    Q_PROPERTY(QString backLabel READ backLabel CONSTANT)
    Q_PROPERTY(QString startLabel READ startLabel CONSTANT)
    Q_PROPERTY(QString pauseLabel READ pauseLabel CONSTANT)
    Q_PROPERTY(QString resumeLabel READ resumeLabel CONSTANT)
    Q_PROPERTY(QString abandonLabel READ abandonLabel CONSTANT)
    Q_PROPERTY(QString noSeriesLabel READ noSeriesLabel CONSTANT)
    Q_PROPERTY(QString acceptLabel READ acceptLabel CONSTANT)
    Q_PROPERTY(QString verifyLabel READ verifyLabel CONSTANT)
    Q_PROPERTY(QString verifyAside READ verifyAside CONSTANT)
    Q_PROPERTY(QString chooseLeadLabel READ chooseLeadLabel CONSTANT)
    Q_PROPERTY(QString dropSomethingElseLabel READ dropSomethingElseLabel CONSTANT)
    Q_PROPERTY(QString chooseFilesLabel READ chooseFilesLabel CONSTANT)
    Q_PROPERTY(QString chooseFilesTitle READ chooseFilesTitle CONSTANT)
    Q_PROPERTY(QString chooseFolderLabel READ chooseFolderLabel CONSTANT)
    Q_PROPERTY(QString chooseFolderTitle READ chooseFolderTitle CONSTANT)
    /// What the bar's button says while the dialog is shut. A transfer that finished and a
    /// question nobody saw look the same from there, so both are counted.
    Q_PROPERTY(QString waitingLabel READ waitingLabel NOTIFY changed)

public:
    explicit ImportCaptions(Imports *imports, QObject *parent = nullptr);

    static ImportCaptions *create(QQmlEngine *engine, QJSEngine *);

    QString title() const;
    QString dropHereLabel() const;
    QString dropHowLabel() const;
    QString cancelLabel() const;
    QString backLabel() const;
    QString startLabel() const;
    QString pauseLabel() const;
    QString resumeLabel() const;
    QString abandonLabel() const;
    QString noSeriesLabel() const;
    QString acceptLabel() const;
    QString verifyLabel() const;
    QString verifyAside() const;
    QString chooseLeadLabel() const;
    QString dropSomethingElseLabel() const;
    QString chooseFilesLabel() const;
    QString chooseFilesTitle() const;
    QString chooseFolderLabel() const;
    QString chooseFolderTitle() const;
    QString waitingLabel() const;

signals:
    void changed();

private:
    Imports *m_imports;
};
