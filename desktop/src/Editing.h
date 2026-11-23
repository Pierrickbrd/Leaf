#pragma once

// What the two editing windows read before they change anything: their words, the series and
// the volumes they open on, and the names the library already uses.
//
// **Apart from `Commands`, which changes.** Everything here only asks and says, and touches
// none of what a command keeps — whether one is on its way, what refused it: a window reading
// its series is not a gesture in flight, and the menus do not wait on it. Together the two were
// forty methods, past what a reader holds in mind, for two jobs that share only the server.

#include "Server.h"

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class Editing final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// What the editing windows call their fields.
    Q_PROPERTY(QVariantMap words READ words CONSTANT)
    /// `[{ value, label }]`, the media in the contract's order, for the choice in the window.
    Q_PROPERTY(QVariantList mediums READ mediums CONSTANT)

public:
    explicit Editing(Server *server, QObject *parent = nullptr);

    static Editing *create(QQmlEngine *engine, QJSEngine *);

    QVariantMap words() const;
    QVariantList mediums() const;

    /// « fr » as « Français », for the field that holds the tag and shows the name.
    Q_INVOKABLE QString languageName(const QString &tag) const;
    /// A word as the server folds it before two words are one: `Fold::searchKey`, so that a
    /// field of words offers, keeps and refuses as the library will. Folded in QML, the
    /// apostrophe separated, the ligatures stayed whole, and `\p{L}` — which this engine's
    /// regular expressions do not read — left the hyphen of « Jean-David » standing.
    Q_INVOKABLE QString searchKey(const QString &text) const;
    /// Reads one series as the editing window needs it, and says it in `seriesLoaded`.
    Q_INVOKABLE void loadSeries(const QString &seriesId);
    /// The volumes of a series that can be its cover, in the order the server gives them, as
    /// `[{ number, label, cover }]` in `volumesLoaded`.
    Q_INVOKABLE void loadVolumes(const QString &seriesId);
    /// Reads every value the library already uses — authors, genres, tags… — and says it in
    /// `vocabularyLoaded` as `{ key: [names] }`, the most used first.
    Q_INVOKABLE void loadVocabulary();

signals:
    void seriesLoaded(const QVariantMap &series);
    void volumesLoaded(const QString &seriesId, const QVariantList &volumes);
    void vocabularyLoaded(const QVariantMap &vocabulary);

private:
    Server *m_server;
};
