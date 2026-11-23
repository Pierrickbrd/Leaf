#pragma once

// What brings a dropped path onto the list of `Imports`: a drop on the window or the dialog,
// a folder chosen in a picker, and a reimport aimed at a series, a volume or a chapter the
// library already holds — with the two signals a menu far from the dialog says it through.
//
// Nothing is sent from here: a card offered is announced to the server, which says where it
// would go, and `ImportQueue` is what lets it go. Why it is a class of its own is written at
// the top of `ImportRows.h`.

#include <QHash>
#include <QList>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QUrl>

class Imports;

class ImportOffers final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit ImportOffers(Imports *imports, QObject *parent = nullptr);

    static ImportOffers *create(QQmlEngine *engine, QJSEngine *);

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

    /// Takes local files or folders in and aims every card at `{kind, id}` — an `ENTRY`, a
    /// `SERIES`, a `UNIVERSE` or a `CHAPTER` the library already holds, `name` being what the
    /// reader sees of it. One card per path (per universe or series, for a folder), **always
    /// by the route of a folder**, a single file included: the server has one road for what
    /// is dropped on a target, and a file travels as a folder of one (`Manifest::single`).
    /// Nothing is sent.
    Q_INVOKABLE void reimport(const QString &kind, const QString &id, const QString &name,
                              const QStringList &paths);

    /// `reimport` for what a picker or a drop hands over, as `offerUrls` is for `offer`: how
    /// many cards were added.
    Q_INVOKABLE int reimportUrls(const QString &kind, const QString &id, const QString &name,
                                 const QList<QUrl> &urls);

    /// Aims the dialog at the first chapter of the volume `entryId` (named `name` in the
    /// menu): asks the server for its chapters, then says `chapterAimed` with the one to
    /// aim at. Nothing is said for a volume that has none.
    Q_INVOKABLE void aimAtChapterOf(const QString &entryId, const QString &name);

    /// What a tile's menu says to the window: the import dialog belongs to the window, and a
    /// tile on the shelf, in a search and on a series page has no other way to reach it.
    /// Said as `reimportWanted`, which the window answers by opening the dialog on `{kind, id}`.
    Q_INVOKABLE void wantReimport(const QString &kind, const QString &id, const QString &name,
                                  int volumes = 0);

signals:
    /// The chapter to aim a reimport at, asked for by `aimAtChapterOf`: its id and its name.
    void chapterAimed(const QString &id, const QString &name);
    /// A menu asked for a reimport of `{kind, id}`, `name` being what the reader sees of it.
    void reimportWanted(const QString &kind, const QString &id, const QString &name,
                        int volumes);

private:
    Imports *m_imports;
    /// The number of each chapter the menu offered to aim at, by its id: a card aimed at a
    /// chapter carries it (`Card::Road::chapterNumber`).
    QHash<QString, double> m_chapterNumbers;
};
