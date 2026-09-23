#pragma once

// The files of one edition, as the volumes tab draws them.
//
// **It marries two answers.** `Entry` carries no reading state at all — the contract is plain
// about it — and `/series/{id}/progress` holds « one record per entry that has been opened ».
// So a line is an entry and the record that may or may not exist beside it, joined here: one
// request for thirty volumes, not thirty.
//
// **The missing volumes are rows too**, and they come from neither answer: they are gaps
// `holding.missingVolumes` reports, and the page hands them over because it already knows
// them. A gap has no file, so it has no identifier, no pages and no menu — the list draws it
// and nothing can be done to it.
//
// **Nothing is re-sorted.** The server answers « volumes and standalone chapters, mixed, in
// reading order »; sorting by file name is how « Tome 10 » lands before « Tome 2 ».

#include "Api.h"
#include "Server.h"

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QQmlEngine>
#include <QString>
#include <QVariantList>

class Entries final : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(QString trouble READ trouble NOTIFY changed)
    /// What is being looked for inside the list, or nothing. Local: the whole list is already
    /// in hand — `/series/{id}/entries` takes no parameter — so the answer arrives before the
    /// next keystroke.
    Q_PROPERTY(QString query READ query NOTIFY changed)
    /// The edition this list is showing, so the screen can tell whether it is already the one
    /// asked for. Without it the shell would re-point the list on every change of the page —
    /// including the one its own answer caused.
    Q_PROPERTY(QString pointedAt READ pointedAt NOTIFY changed)
    /// True when a search emptied the list, which is not the same as a series with no files.
    Q_PROPERTY(bool narrowedToNothing READ narrowedToNothing NOTIFY changed)
    /// The number of the volume being read, and NaN when none is open. The universe block
    /// wants it to write « ici » on the right step: a reading order may send the same work
    /// round twice, and « where am I in the universe » is a question nothing else answers.
    /// Counted off the whole list and not the shown one, because a search narrows what is
    /// drawn here and has no business moving a mark on another block.
    Q_PROPERTY(double reading READ reading NOTIFY changed)

public:
    /// What a line shows. `State` and not three booleans: a line draws one mark, and three
    /// flags is three ways for a delegate to draw two.
    enum class State { NeverRead, InProgress, Read, Missing };
    Q_ENUM(State)

    /// What a row *is*. An arc is a separator and a range is a stretch of chapters inside one
    /// volume; neither is a file, and neither can be opened. One list and not three, because
    /// they are read in one column and a reader scrolls through them together.
    enum class Kind { File, Gap, Arc, Range };
    Q_ENUM(Kind)

    enum class Role {
        EntryId = Qt::UserRole,
        Number,
        Title,
        Pages,
        Weight,
        State_,
        HowFarRead,
        TimesFinished,
        Kind_,
        Detail,
        Depth,
    };
    Q_ENUM(Role)

    explicit Entries(Server *server, QObject *parent = nullptr);

    static Entries *create(QQmlEngine *engine, QJSEngine *);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_shown.size()); }
    bool loading() const { return m_loading; }
    QString trouble() const { return m_trouble; }
    QString query() const { return m_query; }
    QString pointedAt() const { return m_id; }
    bool narrowedToNothing() const { return m_shown.isEmpty() && !m_query.isEmpty(); }
    double reading() const;

    /// Points the list at an edition, with the gaps the page already knows about and how many
    /// arcs it declares. Keeps what it holds until the answer arrives, like everything else on
    /// this screen. The count is the guard the contract asks for — a screen knows before it
    /// asks whether there is anything to ask for.
    Q_INVOKABLE void point(const QString &seriesId, const QVariantList &missing = {},
                           int arcCount = 0);
    Q_INVOKABLE void reload();

    /// Narrows what is shown to the lines whose number or title match. Blank is not a search:
    /// a cleared field is the whole list back, the way a cleared chip is the whole shelf.
    Q_INVOKABLE void searchFor(const QString &query);

signals:
    void changed();

private:
    /// One line: a file, or a hole where one is not.
    struct Line {
        Api::Entry file;
        std::optional<Api::Progress> read;
        /// Set on a gap, which has no file behind it.
        std::optional<double> missingNumber;
        /// Set on a separator: the arc that begins here.
        std::optional<Api::Arc> arc;
        /// Set on a stretch of chapters inside the volume above — the two halves of a file an
        /// arc runs through.
        QString range;
        /// One level of indentation, and one only: a saga holds its arcs, and past that the
        /// eye loses the thread.
        int depth = 0;
    };

    void tookEntries(const Server::Answer &answer);
    void tookProgress(const Server::Answer &answer);
    void tookArcs(const Server::Answer &answer);
    /// Puts every separator where its arc begins, and cuts the one volume an arc runs
    /// through into the two stretches either side of the frontier.
    void weaveArcs();
    /// The line an arc begins at, or -1 when no file of this edition reaches it.
    qsizetype beginsAt(const Api::Arc &arc) const;
    /// Draws a frontier that falls *inside* a file: the file, then the stretch before it,
    /// the separator, and the stretch after — `first` being that file's own first chapter.
    void cutOpen(qsizetype at, double first, const Api::Arc &arc, const Line &separator);
    std::optional<double> nextKey(qsizetype after) const;
    void rebuild();

    Server *m_server;
    QString m_id;
    QList<double> m_missing;
    int m_arcCount = 0;
    QList<Api::Arc> m_arcs;
    /// The files alone, before any separator is woven in. Kept apart so a second answer can
    /// be woven again without asking for the first one twice.
    QList<Line> m_files;
    QList<Line> m_all;
    QList<Line> m_shown;
    QString m_query;
    bool m_loading = false;
    QString m_trouble;
    int m_generation = 0;
};
