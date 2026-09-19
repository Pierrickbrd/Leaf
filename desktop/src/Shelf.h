#pragma once

// The shelf: what this library holds, kept for a grid to draw.
//
// The first model this client has. `Server` knows how to ask, `Api` knows how to read the
// answer, and until now nothing kept one — so every pixel of the shelf would have been
// written against nothing. A `GridView` binds to a model or to no shelf at all.
//
// **It words its own rows.** `Api::Series` carries numbers and enumerations and says so:
// "whether it reads 21 tomes or 7 albums is the shelf's business". So a role hands QML a
// sentence out of `Words`, never a value to switch on, and no `.qml` file spells a French
// string or a medium's name.
//
// **A `Server *` through the constructor**, the way `Server` takes a `Settings *`: one
// instance in the application, and a test builds its own against a server of forty lines.
// Nothing here needs a window.
//
// Whether a series is being read is a **fact the tile draws**, not a word: §01 asks for an
// emerald bar on a cover that is in progress, nothing at all on one never opened, and no mark
// on one finished. So the role is a boolean and not the enumeration — QML draws or does not
// draw, and never sorts three cases. The *word* is still missing on purpose: the plural from
// `Words::readStatus` says « Terminées », which is right on a filter pill and wrong on one
// tile. It arrives when `Words` has the singular.

#include "Api.h"
#include "Server.h"

#include <QAbstractListModel>
#include <QHash>
#include <QQmlEngine>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QTimer>

class Shelf final : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int total READ total NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(QString trouble READ trouble NOTIFY changed)
    /// The lit pills, in the contract's own spelling — "UNREAD", "manga" — because that is
    /// what goes on the wire and a second spelling here would be a second thing to keep in
    /// step. Readable so the pills can light themselves from the shelf rather than keep a
    /// copy of what they asked for.
    Q_PROPERTY(QStringList readStatuses READ readStatuses NOTIFY changed)
    Q_PROPERTY(QStringList media READ media NOTIFY changed)
    /// The order asked for, in the contract's spelling. Reported back so the bar can show the
    /// value in full — the value is the information, which is why it is not an icon alone.
    Q_PROPERTY(QVariantMap narrowing READ narrowing NOTIFY changed)
    Q_PROPERTY(QString sort READ sort NOTIFY changed)
    Q_PROPERTY(bool sortReversed READ sortReversed NOTIFY changed)
    Q_PROPERTY(QString sortDirection READ sortDirection NOTIFY changed)
    /// What is being looked for, or nothing. The shelf carries it rather than a search object
    /// holding a second list beside this one: the grid shows series either way, and two models
    /// for one grid is two ways for it to be wrong.
    Q_PROPERTY(QString query READ query NOTIFY changed)

public:
    /// What a tile shows, one role each. `Q_ENUM` so a test names them rather than counting
    /// from `Qt::UserRole` and hoping.
    enum class Role {
        SeriesId = Qt::UserRole,
        Name,
        Work,
        Cover,
        Medium,
        Volumes,
        InProgress,
    };
    Q_ENUM(Role)

    explicit Shelf(Server *server, QObject *parent = nullptr);

    /// The shelf the application shows, built by the engine from the `Server` singleton.
    static Shelf *create(QQmlEngine *engine, QJSEngine *);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// Qt's own paging hooks rather than a `loadNextPage()` of this client's invention:
    /// `libQt6QmlModels` carries undefined references to both `QAbstractItemModel::fetchMore`
    /// and `::canFetchMore`, so the delegate model behind every `GridView` calls them itself
    /// as the view runs past the rows it has. A method of our own would be a second way to
    /// say the same thing, and QML would have to remember which one.
    ///
    /// What they cannot do is start. A view showing nothing asks for nothing, so the first
    /// page is `reload`'s job and never theirs.
    bool canFetchMore(const QModelIndex &parent) const override;
    void fetchMore(const QModelIndex &parent) override;

    int count() const;
    /// How many there are behind the current answer, which is not how many are held.
    int total() const;
    bool loading() const;
    /// Empty while nothing is wrong. The only thing a screen should ever show about a shelf
    /// that did not fill.
    QString trouble() const;

    QStringList readStatuses() const { return chosen(u"read"_qs); }
    QStringList media() const { return chosen(u"medium"_qs); }
    /// The whole selection, by the contract's own axis names. One value rather than eight
    /// getters: the panel repeats over it, and a ninth axis costs nothing.
    QVariantMap narrowing() const { return m_narrowing; }
    QStringList chosen(const QString &axis) const;
    /// The base's own `sort(int, Qt::SortOrder)` brought back into scope beside this one.
    /// Without it this getter hid it, and `model->sort(0)` — which nothing here calls, and
    /// which anything reaching this model through `QAbstractItemModel` might — stopped
    /// compiling for a reason no reader of either signature could see.
    using QAbstractListModel::sort;
    QString sort() const { return m_sort; }
    bool sortReversed() const { return m_reversed.value(m_sort, false); }
    QString sortDirection() const;
    QString query() const { return m_query; }

    /// What a row of pills does. The contract settles what it means: repeating a parameter
    /// widens the choice and naming a second one narrows it, so two lit pills on one axis are
    /// an "or" and one on each axis is an "and". Nothing here interprets them — they are
    /// passed through, and blank values are dropped the way the contract says they are.
    ///
    /// An unchanged selection asks nothing. A pill that is lit again by a stray binding must
    /// not cost a page, and a shelf that reloads on every notify is one nobody can read while
    /// it works.
    Q_INVOKABLE void filterBy(const QVariantMap &narrowing);

    /// The order, normalised through `Api` on the way in: a word the client does not know
    /// becomes `name`, which is what the server falls back to. Reporting anything else would
    /// have the bar name an order nobody is looking at. Asking for the order already in force
    /// reverses its direction — the second click on a criterion, rather than a fifth entry —
    /// and every criterion remembers the direction it was left in.
    Q_INVOKABLE void sortBy(const QString &order);

    /// What was typed. Blank is not a search — a cleared field means the whole shelf, the way
    /// a cleared chip does — and the lit chips still apply, because a search runs inside what
    /// is showing rather than beside it.
    ///
    /// **What is typed is kept at once; what is asked waits for the typing to stop.** A key at
    /// a time on a five-hundred-series library is a page of a hundred rows fetched per letter,
    /// plus a search of its own, and every one of them but the last is read by nobody. The
    /// field still shows the letter the moment it is pressed — the delay is on the question,
    /// never on the answer to the reader.
    ///
    /// Clearing does not wait. Coming back to the whole shelf is not typing, and a field that
    /// takes a fifth of a second to empty feels broken in a way that a field that takes a
    /// fifth of a second to answer does not.
    Q_INVOKABLE void searchFor(const QString &query);

    /// Forget everything and ask again from the first page. Also the retry after a refusal,
    /// and what a changed filter does.
    Q_INVOKABLE void reload();

signals:
    /// One signal for the four properties above. It fires on every transition of every one of
    /// them — a `NOTIFY` that only fires sometimes is a binding that is sometimes wrong.
    void changed();

    /// Only the part of changed that alters what a search is allowed to find. Loading a page
    /// and changing its total must not ask /search again; changing a chip or the field must.
    /// A search-results model can therefore follow the shelf without guessing which of its
    /// many transitions mattered.
    void criteriaChanged();

private:
    void ask(int page);
    void replaceWith(const QList<Api::Series> &fresh);
    void took(int page, const Server::Answer &answer);

    Server *m_server;
    /// Long enough to swallow a burst of keystrokes, short enough that a pause between two
    /// words is not felt as a stall. Two hundred milliseconds is the usual answer, and it is
    /// what a fast typist leaves between letters.
    static constexpr int Settling = 200;

    QString m_sort = Api::spell(Api::Sort::Name);
    /// The way each criterion was last being read, by its contract spelling. Held per
    /// criterion rather than once for the shelf: coming back to « Ajout » from an alphabet
    /// turned round should give the dates the way they were left, and a single flag made the
    /// reversal of one order silently become the reversal of the next.
    QHash<QString, bool> m_reversed;
    /// Whether the answer in flight replaces the shelf rather than extending it. Held here
    /// because the reply arrives long after the criteria changed, and what is on screen in
    /// between is the old shelf, deliberately.
    bool m_replacing = false;
    QString m_query;
    QTimer m_settling;
    /// Axis name to the values lit on it — `read`, `medium`, `universe`, `genre`, `author`,
    /// `publisher`, `language`, `status`. Held as one map because it goes out as one query
    /// and comes back from one panel; two lists could disagree about what is being asked.
    QVariantMap m_narrowing;
    QList<Api::Series> m_held;
    int m_total = 0;
    /// The page to ask for next, counted here rather than read from the answer's echo: a
    /// server repeating the page it was given would otherwise fetch the same one for ever.
    int m_next = 0;
    /// Cleared by a page that arrives empty. A `total` disagreeing with what actually comes
    /// back is the shape of an endless loop — the view asks, nothing arrives, the count still
    /// falls short, the view asks again.
    bool m_more = true;
    bool m_loading = false;
    QString m_trouble;
    /// Which shelf an answer belongs to. `Server` has no cancel, so a request already out
    /// arrives whatever happens next; without this a `reload` during one would see the old
    /// page land on top of the new. Bumped by every `reload`, and an answer carrying a stale
    /// number is dropped unread.
    int m_generation = 0;
};
