#pragma once

// The checking frame, as what a screen binds to: a tree of what was dropped, the list of the
// node chosen in it, four counters, the sheet of what was clicked, and a footer.
//
// Nothing here is decided twice. What a value is and where it came from is `Facts`; what the
// library already holds is the server's answer on each card (`alreadyThere`); whether a card
// may still be corrected is `Imports`. This only arranges them, and recomputes on every
// change of the queue rather than keeping a copy: a copy is a second truth, and the first
// version of every list in this client that kept one drew a card as it was two answers ago.
//
// **Ids are strings**, `"all"`, `"loose"` and `"<token>/<at>"`, because the frame mixes
// nodes that are folders with two that are not, and the card's `token` and not its row: a
// card abandoned and redropped must not receive a click aimed at the old one (`Card::token`).
// A shelf belongs to no card, so it is `"drop/<drop>/<at>"`, by the drop's own token
// (`Imports::dropOf`) and its place in the dropped folder.
//
// **Four objects and not one.** `Review` is what the frame draws; what moves it — where it
// stands, what is ticked, what is written into a sheet — are three parts of it, `view`,
// `ticks` and `edits`, sharing one `Frame` it owns. As one class it carried seventy-nine
// methods, and the one a screen called could only be found by reading past the other
// seventy-eight. They are parts of one object rather than four singletons because they are
// one state: a tick is forgotten when the node changes, a sheet edited is re-read by the
// frame, and four singletons would each have had to find the others to say so. All four are
// declared and defined here, beside the helpers of `Review.cpp` they all read.
//
// **Roles are read from the drop each time, never kept.** `Imports::dropOf` hands a pointer
// valid until the queue changes, and the frame is rebuilt on every change: a pointer kept
// across one would read a drop already forgotten.

#include "Facts.h"

#include <QHash>
#include <QObject>
#include <QQmlEngine>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <memory>
#include <optional>
#include <utility>

class QJSEngine;
class Card;
class Imports;
class ReviewEdits;
class ReviewTicks;
class ReviewView;

class Review final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// « 1 série · 14 fichiers · ComicInfo.xml ».
    /// « Réimporter le tome 2 » over the frame of a card aimed at something; empty otherwise,
    /// where the screen's own title stands.
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString subtitle READ subtitle NOTIFY changed)
    /// `{id, level, name, depth, count, expandable, expanded, selected, role, roleLabel,
    /// roleTone}`, in the order drawn. `role` is a `Shape::Role`, -1 on the two nodes that
    /// are no folder; `roleTone` an `Origin`, how surely the role is known.
    Q_PROPERTY(QVariantList rail READ rail NOTIFY changed)
    /// « Apport », or « Apport · rôle proposé » once a role in the tree is not said by a file.
    Q_PROPERTY(QString railTitle READ railTitle NOTIFY changed)
    /// How a role is changed, under the tree when one is proposed; empty otherwise.
    Q_PROPERTY(QString railHint READ railHint NOTIFY changed)
    /// The chosen node's content, grouped, each group after a `{header: true, text}` line.
    Q_PROPERTY(QVariantList rows READ rows NOTIFY changed)
    /// Four `{label, count, lit}`, in the order of `Filter`.
    Q_PROPERTY(QVariantList counts READ counts NOTIFY changed)
    Q_PROPERTY(QString listTitle READ listTitle NOTIFY changed)
    Q_PROPERTY(QString railNote READ railNote NOTIFY changed)
    /// The editions an import adds that the server asks a name for, as `Aimed::editionRows`.
    Q_PROPERTY(QVariantList editionRows READ editionRows NOTIFY changed)
    Q_PROPERTY(QString editionNameHelp READ editionNameHelp CONSTANT)
    Q_PROPERTY(QString note READ note NOTIFY changed)
    Q_PROPERTY(QVariantMap sheet READ sheet NOTIFY changed)
    Q_PROPERTY(QString footer READ footer NOTIFY changed)
    Q_PROPERTY(QString importLabel READ importLabel NOTIFY changed)
    Q_PROPERTY(bool importable READ importable NOTIFY changed)
    Q_PROPERTY(bool nothingToImport READ nothingToImport NOTIFY changed)
    /// Some file of the drop is held byte for byte: the frame has something to show, even
    /// when nothing goes.
    Q_PROPERTY(bool sparesSomething READ sparesSomething NOTIFY changed)
    /// The cards « Revoir la suivante » walks: each one refused, or with a sheet that goes
    /// and still asks something.
    Q_PROPERTY(int toReview READ toReview NOTIFY changed)
    /// Where the frame stands, what is ticked in it, and what is written into its sheets.
    Q_PROPERTY(ReviewView *view READ view CONSTANT)
    Q_PROPERTY(ReviewTicks *ticks READ ticks CONSTANT)
    Q_PROPERTY(ReviewEdits *edits READ edits CONSTANT)

public:
    /// The four filters, and the three groups a line falls in (every value but `All`).
    enum class Filter { All, ToReview, Ready, Identical };
    Q_ENUM(Filter)

    /// `Facts::Origin`, as numbers a `.qml` file can compare — the screen dresses each one in
    /// its own tokens; a colour named here would decide the dark theme from the wrong end.
    enum class Origin { Said, Taken, Guessed, Missing };
    Q_ENUM(Origin)

    /// What building the frame cost, for a test to assert on, as `Cx` counts queries: the
    /// lists asked for and the walk's entries looked at to answer them. A list that reads
    /// every file once per file returns the right list, only slower each time a series
    /// grows — measured, one list of a thousand chapters read a million and a half entries.
    ///
    /// `looks` counts the roles looked at — a step up a drop for a confidence, a card visited
    /// for its shelf — and `proposals` how many times the frame asked whether anything in it
    /// is proposed: four properties read that answer, and each asking it again walked every
    /// sheet of every card.
    struct Cost {
        qint64 rows = 0;
        qint64 entries = 0;
        qint64 looks = 0;
        qint64 proposals = 0;
        /// The volumes walked for a series' credits: the sheet, the list, its title and the
        /// footer all say them, and each walking the series again read it four times over.
        qint64 credits = 0;
        /// The steps of a universe's orders looked at to draw them: built again for each
        /// inclusion and each order, ten thousand chained orders were a hundred million.
        qint64 orders = 0;
        /// The sheets walked for the words a list field offers.
        qint64 vocabulary = 0;
        /// The sheets of the ticked series walked to apply a value to them.
        qint64 grouped = 0;
        /// The calls to `Facts::compose` the frame made.
        qint64 composes = 0;
    };
    static Cost cost();

    /// What the frame and its three parts share: where it stands, what was typed and ticked
    /// in it, and what it read of the queue since the last change. Kept once, here, so that a
    /// part never holds a copy of what another one moves.
    struct Frame {
        /// What five properties read of the whole frame, asked once per change: each of them
        /// asking it again walked every sheet of every card, and `Imports::readings` reads
        /// every card's role.
        struct State {
            bool foldersProposed = false;
            bool anyProposed = false;
            int readings = 0;
            /// The reading orders the drop's universes declare, read once and not by each line.
            int orders = 0;
            /// What each card composes to (`Facts::compose`), by `Card::token`, filled as each
            /// is first asked after a change: `footer` and `railNote` composed every card each,
            /// on every change, and the sheet of a suggested series its own card once more.
            QHash<quint64, Facts::Composed> composed;
        };

        /// What a series' volumes credit, as the sheet, the list, its title and the footer say
        /// it: `all` the people once each, in the order of the volumes; `alike` when every
        /// volume credits the same people, which says nothing a series' own line does not.
        /// Walked once per change and series, not once per property.
        struct Credits {
            QStringList all;
            int volumes = 0;
            bool alike = true;
        };

        Imports *imports = nullptr;
        QString node = QStringLiteral("all");
        QString focus = QStringLiteral("all");
        Filter filter = Filter::All;
        QString search;
        /// `search` as `Fold::searchKey` folds it, what every line is compared with.
        QString searchKey;
        QSet<QString> folded;
        bool showIdentical = false;
        /// What could not be read, by `id + '\n' + key`: kept here because nothing reached the
        /// card, and cleared as soon as something does.
        QHash<QString, QString> doubts;
        /// The lines ticked, by id, forgotten with their card as `doubts` is, and all of them
        /// when the filter, the search or the node changes: what a value is applied to is
        /// always what is seen ticked.
        QSet<QString> ticked;
        /// The last gesture was a tick: the grouped values are shown, not a sheet.
        bool grouped = false;
        /// What the last `ReviewTicks::applyToTicked` did, until the ticks change.
        QString outcome;
        /// `ReviewEdits::setLibraryWords`: the names the library holds, offered after the
        /// import's.
        QVariantMap libraryWords;
        mutable std::optional<State> stateCache;
        mutable QHash<QString, Credits> creditsCache;

        const State &state() const;
        /// `State::composed` for `card`, composed on the first asking since the last change. A
        /// copy, which shares what it holds: a reference into the table would not outlive the
        /// next card composed into it.
        Facts::Composed composedOf(const Card &card) const;
        Credits creditsOf(quint64 token, const QString &at) const;
        /// The credits of the series whose list is shown, when its volumes are not credited
        /// alike: then its list, its title and its footer say them.
        std::optional<Credits> creditsShown() const;
        /// The one card of the frame, when it is laid on something the library holds. Several
        /// cards — a drop of folders aimed at one place — keep the frame they always had.
        const Card *aimed() const;
        /// Unticks every line.
        void untickAll();
        /// After a role change: the node or sheet shown may be a card that went (a shelf
        /// turned universe makes a new one), and is found again by its place in the drop.
        /// Whether it moved.
        bool refocus(quint64 drop, const QString &at);
    };

    explicit Review(Imports *imports, QObject *parent = nullptr);

    static Review *create(QQmlEngine *engine, QJSEngine *);

    QString title() const;
    QString subtitle() const;
    QVariantList rail() const;
    QVariantList rows() const;
    QVariantList counts() const;
    QString listTitle() const;
    QString railTitle() const;
    QString railHint() const;
    QString railNote() const;
    QVariantList editionRows() const;
    QString editionNameHelp() const;
    QString note() const;
    QVariantMap sheet() const;
    QString footer() const;
    QString importLabel() const;
    bool importable() const;
    bool nothingToImport() const;
    bool sparesSomething() const;
    int toReview() const;
    ReviewView *view() const { return m_view; }
    ReviewTicks *ticks() const { return m_ticks; }
    ReviewEdits *edits() const { return m_edits; }

signals:
    void changed();

private:
    std::unique_ptr<Frame> m_frame = std::make_unique<Frame>();
    ReviewView *m_view;
    ReviewTicks *m_ticks;
    ReviewEdits *m_edits;
};

/// Where the frame stands: the node opened, the sheet shown, the folds of the tree, the
/// filter and the search — and the gestures that move them.
class ReviewView final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Review.view, one per frame")

    Q_PROPERTY(int filter READ filter WRITE setFilter NOTIFY changed)
    /// What is typed over the list, as typed: the list and its counters keep the lines it
    /// is found in, the tree all of them.
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY changed)

public:
    ReviewView(Review *review, Review::Frame *frame);

    int filter() const { return std::to_underlying(m_frame->filter); }
    Q_INVOKABLE void setFilter(int filter);
    QString search() const { return m_frame->search; }
    /// Says it changed only when the folded text does: « Mon » after « mon » finds the same
    /// lines, and rebuilding the frame for it would be a rebuild for nothing.
    void setSearch(const QString &text);

    /// Chooses the first thing to review — a container's sheet before a file's — or else the
    /// series when there is only one, or else the whole drop.
    Q_INVOKABLE void open();
    /// Opens, as `open` would for it, the card to review that follows the one shown in the
    /// tree's order, coming back to the first after the last; the filter and the search stay.
    /// False, and nothing changed, when no card is to review.
    Q_INVOKABLE bool reviewNext();
    /// Shows the first thing that stops a series from going — a required value missing, two
    /// files at one number — in the tree's order: its container's sheet before a file's, the
    /// field on the screen. What « Suivant » does instead of leaving the step while a series
    /// is stopped. False, and nothing changed, when no series is.
    Q_INVOKABLE bool showBlocked();
    /// The last click: a node of the tree (or a line that is one) shows its content and its
    /// sheet; a file shows its sheet, and the list stays where it was.
    Q_INVOKABLE void select(const QString &id);
    /// Folds or unfolds a node of the tree.
    Q_INVOKABLE void toggle(const QString &id);
    /// Shows or folds the group of identical lines.
    Q_INVOKABLE void toggleIdentical();

signals:
    void changed();

private:
    /// The tree with every node unfolded, as `Review::rail` would draw it.
    QVariantList wholeRail();
    /// Of that tree, the nodes left unfolded around what is opened: what leads to the node
    /// and the sheet shown, and every node alone under its parent.
    QSet<QString> shownBy(const QVariantList &whole) const;
    /// Folds every node of the tree but those `shownBy` keeps.
    void foldAround();

    Review *m_review;
    Review::Frame *m_frame;
};

/// The lines ticked in the list, and the values applied to them together.
class ReviewTicks final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Review.ticks, one per frame")

    /// 0 when no line is ticked, 1 when some are, 2 when every line that can be is, among
    /// those shown — what « Tout » says and does.
    Q_PROPERTY(int tickState READ tickState NOTIFY changed)
    Q_PROPERTY(int ticked READ ticked NOTIFY changed)
    /// « 4 séries cochées », what a tick box says aloud.
    Q_PROPERTY(QString tickedLabel READ tickedLabel NOTIFY changed)
    /// Whether the right of the frame shows what the ticked series take, rather than a sheet:
    /// while the last gesture is a tick.
    Q_PROPERTY(bool groupedShown READ groupedShown NOTIFY changed)
    /// `{title, aside, fields: [{key, label, choices: [{value, label}]}], applyLabel, note,
    /// outcome}`, each field's first choice `{value: null}`, « Ne pas changer ».
    Q_PROPERTY(QVariantMap grouped READ grouped NOTIFY changed)

public:
    ReviewTicks(Review *review, Review::Frame *frame);

    int tickState() const;
    int ticked() const;
    QString tickedLabel() const;
    bool groupedShown() const { return m_frame->grouped && ticked() > 0; }
    QVariantMap grouped() const;

    /// Ticks or unticks the line `id`: a series that goes and can still be corrected, and
    /// nothing else.
    Q_INVOKABLE void tick(const QString &id, bool ticked);
    /// Ticks every line shown that can be — filter and search included — or unticks all.
    Q_INVOKABLE void tickShown(bool ticked);
    /// Ticks every line the list draws from `from` to `to`, both included, in the order it
    /// draws them — what Maj and an arrow do — and says it once. How many were ticked.
    Q_INVOKABLE int tickRange(const QString &from, const QString &to);
    /// Writes each value of `values` (`readingDirection`, `language`, `status`; a key absent
    /// or null changes nothing) into every sheet at or under each ticked series that has the
    /// field, through one `Imports::edit` batch. How many series changed.
    Q_INVOKABLE int applyToTicked(const QVariantMap &values);

signals:
    void changed();

private:
    Review *m_review;
    Review::Frame *m_frame;
};

/// What is written into the drop from the frame: a value of a sheet, a role, a number, the
/// chapters and arcs of their windows, and the choices of a card aimed at something held.
/// Each one goes through `Imports`, which the frame then reads anew.
class ReviewEdits final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Review.edits, one per frame")

public:
    ReviewEdits(Review *review, Review::Frame *frame);

    /// Puts `text`, read as the field `key` expects, into the sheet `id` (`Imports::edit`).
    /// A number that does not read changes nothing and says why under its field.
    Q_INVOKABLE bool edit(const QString &id, const QString &key, const QString &text);
    /// Puts `words` into the list field `key` — authors, artists, genres, tags — of the sheet
    /// `id`; no word leaves it Missing. A list is given as words and never typed: `edit`
    /// refuses these four keys, since a comma inside a name cut it in two.
    Q_INVOKABLE bool editWords(const QString &id, const QString &key, const QStringList &words);
    /// The names the library already holds, as `Commands::vocabularyLoaded` says them: offered
    /// by a list field after those of the import.
    Q_INVOKABLE void setLibraryWords(const QVariantMap &words);

    /// The role menu of the folder or archive `id`: `[{role, label, detail, possible,
    /// chosen}]`, in `Shape::options`' order. « Livre seul » is greyed on an archive whose
    /// `oneshot.json` did not read, which `Shape` cannot know — it opens no archive.
    Q_INVOKABLE QVariantList roleOptions(const QString &id) const;
    /// Gives `id` the role `role` (a `Shape::Role`) through `Imports::setRole`; false, and
    /// nothing changed, where the menu greys it.
    Q_INVOKABLE bool setRole(const QString &id, int role);
    /// Gives the volumes `ids`, in this order, the numbers they hold between them: the set
    /// of numbers stays, and who holds which follows the order. What a drag in the list says.
    /// Refused — nothing written — when one of them has no number or cannot be corrected.
    Q_INVOKABLE bool renumber(const QStringList &ids);
    /// The chapters a volume lists, replaced by `rows` — `number`, `title`, `page` (text, as
    /// typed) and `source`, the index of the line it was before, -1 for a new one, so that what
    /// the line does not show (`raw`, `after`, `volume`…) is kept. Refused, nothing changed, for a
    /// number missing, not a number or twice, and a page that is not a whole number.
    Q_INVOKABLE bool setChapters(const QString &id, const QVariantList &rows);
    /// `rows` numbered again from `first`, each the one before plus one, all else of a line
    /// kept: the chapters follow their order. `rows` as they were when `first` is no number.
    Q_INVOKABLE QVariantList chaptersNumbered(const QVariantList &rows, const QString &first) const;
    /// Where line `at` of `rows` goes by the page it starts on: before the first other line
    /// starting further on, else after the last one starting before it. `at` itself when it
    /// has no page, or already sits between the pages on either side of it.
    Q_INVOKABLE int chapterPlace(const QVariantList &rows, int at) const;
    /// The arcs and sagas of the series `id` names, replacing what it held. Refused — and
    /// nothing changed — for a name twice or empty, a range that runs backwards, a unit other
    /// than CHAPTER and VOLUME, or a saga that is not in the list.
    Q_INVOKABLE bool setArcs(const QString &id, const QVariantList &arcs);
    /// The arcs of a sheet as the window lists them, sagas as separators (`Sagas`), and back.
    Q_INVOKABLE QVariantList arcRows(const QVariantList &arcs) const;
    Q_INVOKABLE QVariantList arcsFromRows(const QVariantList &rows) const;
    /// One door for the window's edits of those rows: `how` is "move" (`arg` -1 or +1),
    /// "start" (`arg` the arc's row), "remove" or "regap".
    Q_INVOKABLE QVariantList arcRowsEdited(const QVariantList &rows, const QString &how, int at,
                                           int arg) const;
    /// Abandons the card `id` names (`Imports::abandon`) when it failed, and goes back to the
    /// whole drop; false, and nothing done, for a card that can still go.
    Q_INVOKABLE bool remove(const QString &id);
    /// Puts `id` in its reading `index` — the `index` of an entry of the sheet's `readings`.
    Q_INVOKABLE bool chooseReading(const QString &id, int index);

    /// What to do with the file at `id` that is another volume than the one aimed at:
    /// `"own"` gives it the number it reads of itself, `"aimed"` the number of the volume
    /// aimed at. Both are corrections of its number, like any other.
    Q_INVOKABLE bool choose(const QString &id, const QString &choice);
    /// The pages of the source that replace the chapter aimed at, counted from 1 as on screen.
    Q_INVOKABLE bool setSplice(const QString &id, int from, int to);
    /// Whether the volume at `id` starts again from its first page, for the asking profile.
    Q_INVOKABLE bool setReset(const QString &id, bool reset);
    /// The switch of a line: whether the volume at `id` is replaced.
    Q_INVOKABLE bool setSwitch(const QString &id, bool on);
    /// Types the name of an edition the import adds; the folder is announced again with it.
    Q_INVOKABLE bool setEditionName(const QString &path, const QString &name);
    /// The image at `fileUrl` as the cover of the folder `at` of the sheet's card; an empty
    /// url goes back to the one the folder holds.
    Q_INVOKABLE bool setCover(const QString &at, const QString &fileUrl);
    /// Whether the value the library holds under `key` of the declaration at `id` stays over
    /// the one the drop brings.
    Q_INVOKABLE bool setKeep(const QString &id, const QString &key, bool keep);

private:
    Review *m_review;
    Review::Frame *m_frame;
};
