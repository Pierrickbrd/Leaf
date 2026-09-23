#pragma once

// What a deletion would take, asked before it is done — and then the deletion itself.
//
// **There is no trash.** The library is the files and the index is rebuilt by scanning them,
// so this is the one screen in the client that unmakes something a scan will not bring back.
// The confirmation is therefore not a formality: it says what goes, what it weighs, what is
// lost besides the files, and what the collection will look like afterwards.
//
// **It finds out rather than being told.** The same modal opens from a shelf tile, from the
// header of a series page and from a line of its list, and only one of those three has the
// volumes already in hand. A model that trusted its caller would be right on two screens and
// wrong on the third, so it asks — two requests for a file, three for a whole edition, on a
// gesture that is about to unlink files for ever.
//
// **What it says about a hole is computed, not written once.** Erasing the last volume brings
// the ceiling down and erasing the first raises the floor; neither leaves anything missing.
// Erasing one in the middle leaves a gap for ever. A confirmation that said the same thing
// every time would only ever be clicked through.

#include "Api.h"
#include "Server.h"

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QVariantMap>

class Erasure : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// Whether the confirmation is up. Nothing is asked for and nothing is deleted while it
    /// is false — this is the only thing that opens and closes the modal.
    Q_PROPERTY(bool asking READ asking NOTIFY changed)
    /// Still finding out what the deletion would take. The modal is drawn either way, because
    /// a question that appears after a wait is a question nobody expected.
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    /// A whole edition, rather than one file. It is the one that asks for the name to be
    /// typed: thirty files and nothing to come back to is worth the weight, and the same
    /// weight on a single volume would be ceremony.
    Q_PROPERTY(bool whole READ whole NOTIFY changed)
    /// Everything the modal says, by key: `question`, `what`, `file`, `leaves`, `warning`,
    /// `untouched` and `confirm`. One map rather than seven properties, the way a description
    /// hands over its rows — the modal draws what is there and skips what is empty.
    Q_PROPERTY(QVariantMap said READ said NOTIFY changed)
    /// Whether the button that deletes is alive: not while it is still finding out, and not
    /// until the name has been typed when one is asked for.
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    /// What went wrong, in the server's own words — or which files would not go.
    Q_PROPERTY(QString trouble READ trouble NOTIFY changed)
    /// The two buttons. Here rather than in a captions class of their own: they belong to
    /// this one modal and nothing else in the client says « Supprimer » as a command.
    Q_PROPERTY(QString cancelLabel READ cancelLabel CONSTANT)
    Q_PROPERTY(QString eraseLabel READ eraseLabel CONSTANT)

public:
    explicit Erasure(Server *server, QObject *parent = nullptr);

    static Erasure *create(QQmlEngine *engine, QJSEngine *);

    bool asking() const { return m_asking; }
    bool loading() const { return m_pending > 0; }
    bool whole() const { return m_entryId.isEmpty(); }
    QVariantMap said() const;
    bool ready() const;
    QString trouble() const { return m_trouble; }
    QString cancelLabel() const;
    QString eraseLabel() const;

    /// Asks about one file of an edition. The edition is named because an entry does not
    /// carry it, and what remains afterwards cannot be worked out from the file alone.
    Q_INVOKABLE void aboutEntry(const QString &entryId, const QString &seriesId);
    /// Asks about a whole edition.
    Q_INVOKABLE void aboutSeries(const QString &seriesId);
    /// What has been typed into the field, when one is asked for.
    Q_INVOKABLE void typed(const QString &what);
    /// Does it. Refuses to be called twice, and refuses while `ready` is false — a button
    /// that is drawn dead is not the only thing standing between a reader and thirty files.
    Q_INVOKABLE void go();
    /// Closes the confirmation without doing anything.
    Q_INVOKABLE void dismiss();

signals:
    void changed();
    /// Gone from the disk. `entryId` is empty when a whole edition went, and the screen that
    /// was showing it has to stop showing it.
    void erased(const QString &seriesId, const QString &entryId);

private:
    void ask(const QString &seriesId);
    void tookSeries(const Server::Answer &answer);
    void tookEntries(const Server::Answer &answer);
    void tookSiblings(const Server::Answer &answer);
    void tookErasure(const Server::Answer &answer);
    /// The file this is about, and nothing when the list does not hold it.
    const Api::Entry *theEntry() const;
    /// The two shapes of the same question, kept apart: a whole edition and one file of it
    /// say different things, and one function saying both was one nobody could read.
    QVariantMap saidOfEdition() const;
    QVariantMap saidOfFile() const;

    Server *m_server;
    QString m_seriesId;
    QString m_entryId;
    std::optional<Api::Series> m_one;
    QList<Api::Entry> m_files;
    QList<Api::Series> m_siblings;
    QString m_written;
    QString m_trouble;
    bool m_asking = false;
    bool m_going = false;
    int m_pending = 0;
    int m_generation = 0;
};
