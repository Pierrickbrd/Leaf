#pragma once

// The axes worth offering, and the counts that make them worth offering.
//
// `/filters` answers every filterable value with its count — "counted from the same
// expressions the filter runs over, so a chip can never return an empty shelf". This keeps
// the two the bandeau draws, read status and medium, and drops the rest on the floor: author,
// genre and publisher are names, and a name is something you type into the search rather than
// something the bandeau offers.
//
// **The rule that decides whether an axis exists lives here, not in the QML.** A filter earns
// the bandeau when it cuts the library in two: a value covering everything filters nothing,
// so an axis holding a single value never reaches the drawing at all. Six series, all manga,
// all unread — the shelf this was first run against — draws no pill whatsoever, and that is
// the right screen. An axis nobody can see is a decision, and decisions are tested.
//
// Above four values the artifact asks for a button and a menu instead of a row, so an axis
// that long is not offered as pills either. The menu is not drawn yet; until it is, a fifth
// medium would silently stop offering the medium axis, which is why `tooManyValues` says so
// out loud rather than leaving an empty row to be read as "nothing to filter".

#include "Api.h"
#include "Server.h"

class Shelf;

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QVariantList>

class Filters : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// One entry per value: `value` in the contract's spelling, to be handed straight back to
    /// `Shelf::filterBy`; `label` already worded; `count` as the server counted it.
    Q_PROPERTY(QVariantList readStatuses READ readStatuses NOTIFY changed)
    Q_PROPERTY(QVariantList media READ media NOTIFY changed)
    /// The same two axes counted over files rather than over series, for the row drawn above
    /// a list of files. « Non lues 5 » over sixty file rows counts five *series*, which is
    /// neither what the reader is looking at nor what the pill under their finger would
    /// leave. Narrowed by what is being searched for, for the same reason.
    Q_PROPERTY(QVariantList fileReadStatuses READ fileReadStatuses NOTIFY changed)
    Q_PROPERTY(QVariantList fileMedia READ fileMedia NOTIFY changed)
    /// Every axis the library can be narrowed by, for the panel that offers all of them:
    /// `{ axis, title, values: [{ value, label, count }] }`. One list rather than eight
    /// properties, so a ninth axis costs the panel nothing — and the row above keeps reading
    /// its own two, which are the same answer read twice.
    Q_PROPERTY(QVariantList axes READ axes NOTIFY changed)
    Q_PROPERTY(QVariantList fileAxes READ fileAxes NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(QString trouble READ trouble NOTIFY changed)
    /// True when an axis was dropped for being longer than a row can hold, rather than for
    /// having nothing to say. The difference matters to whoever wonders where the pills went.
    Q_PROPERTY(bool tooManyValues READ tooManyValues NOTIFY changed)

public:
    /// Two is the floor: one value covers the whole library and filters nothing. Four is the
    /// ceiling: the artifact gives a row of pills four values at most and a menu beyond.
    static constexpr int Fewest = 2;
    static constexpr int Most = 4;

    explicit Filters(Server *server, Shelf *shelf = nullptr,
                     QObject *parent = nullptr);

    static Filters *create(QQmlEngine *engine, QJSEngine *);

    QVariantList readStatuses() const { return m_readStatuses; }
    QVariantList media() const { return m_media; }
    QVariantList fileReadStatuses() const { return m_fileReadStatuses; }
    QVariantList fileMedia() const { return m_fileMedia; }
    QVariantList axes() const { return m_axes; }
    QVariantList fileAxes() const { return m_fileAxes; }
    bool loading() const { return m_loading; }
    QString trouble() const { return m_trouble; }
    bool tooManyValues() const { return m_tooMany; }

    Q_INVOKABLE void reload();

signals:
    void changed();

private:
    void took(const Server::Answer &answer);
    /// Asked for separately from the series counts and against the query in force, because
    /// the two answer different questions about different populations.
    void countFiles();
    void tookFiles(const Server::Answer &answer);

    Server *m_server;
    Shelf *m_shelf;
    QVariantList m_readStatuses;
    QVariantList m_media;
    QVariantList m_fileReadStatuses;
    QVariantList m_fileMedia;
    QVariantList m_axes;
    QVariantList m_fileAxes;
    int m_fileGeneration = 0;
    bool m_loading = false;
    bool m_tooMany = false;
    QString m_trouble;
    int m_generation = 0;
};
