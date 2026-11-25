#pragma once

// The book that is open: which pages are on screen, how far reading has gone, and what
// comes after it.
//
// One object for the whole reader screen, built like `Resume`: a `Server *` handed in, so a
// test drives it against a real socket and the application gets the singleton. What it
// writes is the reason it is C++ rather than QML — every page turned is a request, and the
// two ways that goes wrong cost nothing on screen: twenty writes for twenty pages leafed
// through, and a write that never leaves because the window was closed first. The first is
// counted in `holds_the_reader.cpp`; the second is `Main.qml`'s to prevent, and
// `crosses_the_seam.cpp`'s to watch.
//
// Pages are indexes from 0, the same number the server gives a page and the same one a
// progress record holds, so nothing here converts between them.

#include "Api.h"
#include "Preferences.h"
#include "Server.h"
#include "Spreads.h"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrlQuery>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <optional>

class Library;
class Preferences;

class Reader : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(QString trouble READ trouble NOTIFY changed)

    Q_PROPERTY(QString seriesId READ seriesId NOTIFY changed)
    Q_PROPERTY(QString entryId READ entryId NOTIFY changed)
    Q_PROPERTY(QString seriesName READ seriesName NOTIFY changed)
    /// « Tome 12 ».
    Q_PROPERTY(QString entryLabel READ entryLabel NOTIFY changed)

    Q_PROPERTY(int pageCount READ pageCount NOTIFY changed)
    /// Which screen of the volume, from 0 — not a page: in two pages there are about half
    /// as many of them.
    Q_PROPERTY(int display READ display NOTIFY changed)
    Q_PROPERTY(int displayCount READ displayCount NOTIFY changed)
    Q_PROPERTY(int firstPage READ firstPage NOTIFY changed)
    /// −1 when the screen holds one page.
    Q_PROPERTY(int secondPage READ secondPage NOTIFY changed)
    /// The width one page is drawn in — the window's, or half of it in two pages.
    Q_PROPERTY(int pageWidth READ pageWidth NOTIFY changed)
    /// The width the images are asked at: `pageWidth` rounded up to a step, and never lower
    /// than it has been since the volume opened. It is what the list of pages was asked at
    /// and what `pageUrl` must be handed, so the screen asks the server's cache for the file
    /// it already prepared.
    Q_PROPERTY(int askedWidth READ askedWidth NOTIFY changed)

    /// The direction applied, as a `Direction`: the one chosen, else the one the files
    /// declare, else left to right.
    Q_PROPERTY(int direction READ direction NOTIFY changed)
    /// What the files say, −1 when they say nothing — the line under the choices.
    Q_PROPERTY(int declaredDirection READ declaredDirection NOTIFY changed)
    /// What was chosen for this series, −1 for « Par défaut ».
    Q_PROPERTY(int chosenDirection READ chosenDirection NOTIFY changed)
    Q_PROPERTY(bool webtoon READ webtoon NOTIFY changed)
    /// The width most of this volume's pages are, in the file's own pixels, 0 when the server
    /// measured none. A webtoon's chapter is drawn from it, page by page in proportion, so a
    /// credits page narrower than the rest stays narrower instead of setting the width of all.
    Q_PROPERTY(int commonWidth READ commonWidth NOTIFY changed)
    /// What a webtoon's strip keeps on this device for this chapter (`Preferences::Strip`),
    /// in logical pixels, 0 before the series was ever opened here: the width it is drawn at
    /// — the chapter's own when it has one, `stripOwn` — and the width that is its 100 %.
    Q_PROPERTY(qreal stripWidth READ stripWidth NOTIFY stripChanged)
    Q_PROPERTY(qreal stripBase READ stripBase NOTIFY stripChanged)
    Q_PROPERTY(bool stripOwn READ stripOwn NOTIFY stripChanged)
    /// Where this entry stands among the series', from 1, and how many there are; 0 until the
    /// series has answered. A webtoon asks at once: its band says « Chapitre 12 sur 120 ».
    Q_PROPERTY(int entryPosition READ entryPosition NOTIFY changed)
    Q_PROPERTY(int entryCount READ entryCount NOTIFY changed)
    /// An entry before this one is known: the band's step back has somewhere to go.
    Q_PROPERTY(bool hasPrevious READ hasPrevious NOTIFY changed)

    /// `[{ label, title, startPage, current }]`, in reading order; `startPage` is −1 when the
    /// chapter does not say where it begins, which is no mark on the bar.
    Q_PROPERTY(QVariantList chapters READ chapters NOTIFY changed)

    /// What the panel beside the pages says of the open volume, and only what it has: `cover`,
    /// `pageCount`, `page` (from 1, the first on screen), `started`, `finished`, `howFarRead`
    /// always, then `title`, `isbn`, `publishedOn` (as the server wrote it), `summary` and
    /// `chapters` (« 98 à 106 ») when the volume carries them. A key that is missing is a
    /// line the panel does not draw. Empty until a volume is open.
    Q_PROPERTY(QVariantMap volume READ volume NOTIFY changed)

    /// The end card is up: the reader went forward from the last screen.
    Q_PROPERTY(bool atTheEnd READ atTheEnd NOTIFY changed)
    /// `{ entryId, label, cover, kind, action }`, empty when nothing follows or before the
    /// rest of the series has been asked for. `kind` is `Volume` or `Chapter`.
    Q_PROPERTY(QVariantMap next READ next NOTIFY changed)
    /// The rest of the series answered and nothing follows this entry: the one fact that
    /// lets the end card drop « Rester sur cette page ». Loading and failed are not it.
    Q_PROPERTY(bool lastOfSeries READ lastOfSeries NOTIFY changed)
    /// « Tome 12 terminé », and « · dernier tome de la série » after it once that is known.
    Q_PROPERTY(QString ending READ ending NOTIFY changed)

public:
    /// `Api::ReadingDirection`, for QML to compare against: `Reader.RightToLeft`.
    enum class Direction { LeftToRight, RightToLeft, Vertical };
    Q_ENUM(Direction)

    /// The library and the preferences may be null: a test that counts requests needs the
    /// first and not always the second, and a reader with neither still reads.
    Reader(Server *server, Library *library, Preferences *preferences,
           QObject *parent = nullptr);
    ~Reader() override;

    static Reader *create(QQmlEngine *engine, QJSEngine *);

    bool loading() const { return m_loading; }
    QString trouble() const { return m_trouble; }
    QString seriesId() const { return m_seriesId; }
    QString entryId() const { return m_entryId; }
    QString seriesName() const;
    QString entryLabel() const;
    int pageCount() const { return int(m_pages.size()); }
    int display() const { return m_display; }
    int displayCount() const { return int(m_displays.size()); }
    int firstPage() const;
    int secondPage() const;
    int pageWidth() const;
    int askedWidth() const { return m_asked; }
    int direction() const;
    int declaredDirection() const;
    int chosenDirection() const;
    bool webtoon() const;
    int commonWidth() const { return m_common; }
    qreal stripWidth() const { return m_strip.chapter > 0 ? m_strip.chapter : m_strip.width; }
    qreal stripBase() const { return m_strip.base; }
    bool stripOwn() const { return m_strip.chapter > 0; }
    int entryPosition() const { return m_entryPosition; }
    int entryCount() const { return m_entryCount; }
    bool hasPrevious() const { return m_previous.has_value(); }
    QVariantList chapters() const;
    QVariantMap volume() const;
    bool atTheEnd() const { return m_atTheEnd; }
    QVariantMap next() const;
    QString ending() const;
    bool lastOfSeries() const { return m_wayOnKnown && !m_next; }

    Q_INVOKABLE void open(const QString &seriesId, const QString &entryId);
    /// Leaving the reader: what the delay was holding is written now. True when that sent a
    /// write — the screens drawing the series then wait for `recorded` rather than asking
    /// at once, which the server could answer before it applied the write.
    Q_INVOKABLE bool close();

    /// In the direction of *reading*: the QML turns a click on the left of a manga into a
    /// `forward()`. On the last screen, the first `forward()` brings the end card and the
    /// second opens what follows; on the first, `backward()` opens what precedes, at its last
    /// page, and does nothing in the first volume of a series.
    Q_INVOKABLE void forward();
    Q_INVOKABLE void backward();
    Q_INVOKABLE void goToPage(int page);
    /// A webtoon is scrolled and not turned: the strip says which page crosses the middle of
    /// the view, or that its bottom is reached (the last page). Becomes the page shown and
    /// goes through the same delay as a turn. A page already shown changes nothing, since a
    /// scroll reports at every pixel.
    Q_INVOKABLE void scrolledTo(int page);
    Q_INVOKABLE void nextChapter();
    Q_INVOKABLE void previousChapter();
    /// The entry after this one, or before it, opened where its reading stopped: a webtoon's
    /// chapters are its files, and its band steps from one to the next. Nothing when the
    /// series has not said what is there.
    Q_INVOKABLE void openNextEntry();
    Q_INVOKABLE void openPreviousEntry();

    /// −1 is « Par défaut ». Nothing happens for a webtoon, which has no direction to choose.
    Q_INVOKABLE void chooseDirection(int direction);

    /// The series' strip opened here for the first time, drawn `width` wide: that width is
    /// kept as the series' and as its 100 %. Nothing once the series has one.
    Q_INVOKABLE void startStrip(qreal width);
    /// A lasting zoom of the strip, from `drawn` to `to`, on a chapter whose files are `native`
    /// wide on this screen — all three in logical pixels. A chapter drawn at the series' width
    /// changes the series' width, up to its own files' and never past it; a chapter held
    /// narrower by its files, or zoomed past them, keeps a width of its own — the blur of an
    /// image drawn larger than it is was asked for, by this chapter only.
    Q_INVOKABLE void zoomStrip(qreal drawn, qreal to, qreal native);
    /// Ctrl 0: the chapter's own width forgotten, or else the series back at its 100 %.
    Q_INVOKABLE void resetStrip();

    /// The window the pages are drawn in. Lays them out again for it, keeping the page being
    /// read on screen. Nothing laid over the pages — the volume's panel — changes it.
    Q_INVOKABLE void relayout(QSizeF window);

    /// Which of `chapters` a page belongs to, −1 for none: the chapter the bar names for the
    /// first page on screen, and the one the bubble over the progress bar names while it
    /// slides — one rule, so the two never disagree about a page.
    Q_INVOKABLE int chapterAt(int page) const;

    Q_INVOKABLE QString pageUrl(int page, int width) const;
    /// Height over width of a page as the server measured it, 0 when it did not: what the
    /// strip reserves for a page before its image arrives.
    Q_INVOKABLE qreal pageRatio(int page) const;
    /// Width in pixels of a page as the server measured it, 0 when it did not: what a strip
    /// at its original size is drawn at before the image has arrived to say so.
    Q_INVOKABLE int pageNativeWidth(int page) const;
    /// What to fetch before it is needed: the pages of the next two screens and of the one
    /// before, and the cover of what follows once the end is three pages away. For a webtoon,
    /// whose strip holds its own pages, the first pages of the next chapter and its cover.
    Q_INVOKABLE QStringList ahead(int width) const;

signals:
    void changed();
    /// Something the reader did was refused: a position not recorded, a direction not
    /// chosen. Not `trouble`, which says the book did not open — the book is open, and the
    /// screen says this in a bubble over it.
    void refused(const QString &why);
    /// A position reached the server and was kept. What draws the volume's state — its row,
    /// the band — asks again on this and not before, so it never reads the page before.
    void recorded(const QString &seriesId);
    /// What the strip keeps changed: its width, its 100 %, a chapter's own.
    void stripChanged();

private:
    /// The five answers an opening waits for, kept until the last one is in.
    struct Arriving {
        std::optional<Api::Entry> entry;
        std::optional<QList<Api::PageInfo>> pages;
        std::optional<QList<Api::Chapter>> chapters;
        /// Present once answered; the inner value is absent for a volume never opened.
        std::optional<std::optional<Api::Progress>> progress;
        std::optional<Api::Series> series;
        int waiting = 0;
    };

    /// `open`, landing on the last page rather than where reading stopped: the volume before,
    /// reached by going back from the first page of this one.
    void openAt(const QString &seriesId, const QString &entryId, bool atTheEnd);
    void ask(const QString &path, const QUrlQuery &query,
             std::function<QString(const Server::Answer &)> take);
    void arrived();
    void stopOpening(const QString &why);
    Spreads::Layout layout() const;
    void lay();
    /// `m_asked` brought up to what the layout now draws, never down.
    void reach();
    void show(int display);
    /// False when there was nothing to write to.
    bool write();
    bool flush();
    void askTheWayOn();
    bool nearTheEnd() const;
    int currentChapter() const;
    /// Held, written to the preferences, and said — unless nothing changed.
    void keepStrip(const Preferences::Strip &kept);

    /// Guarded, unlike `Resume`'s: the destructor writes, and the engine tears its singletons
    /// down in an order nobody promised.
    QPointer<Server> m_server;
    QPointer<Library> m_library;
    QPointer<Preferences> m_preferences;

    bool m_loading = false;
    QString m_trouble;
    int m_generation = 0;
    /// Counts the directions chosen, so that an answer to anything but the latest is dropped.
    int m_choice = 0;
    Arriving m_arriving;

    QString m_seriesId;
    QString m_entryId;
    std::optional<Api::Series> m_series;
    std::optional<Api::Entry> m_entry;
    QList<Api::PageInfo> m_pages;
    QList<Api::Chapter> m_chapters;
    QList<Spreads::Display> m_displays;
    int m_display = 0;
    QSizeF m_window;
    int m_asked = 0;
    /// See `commonWidth` and `stripWidth`.
    int m_common = 0;
    /// See `entryPosition`.
    int m_entryPosition = 0;
    int m_entryCount = 0;
    Preferences::Strip m_strip;

    /// The write waiting out its second, and a `rewind` still owed by a write that failed.
    QTimer m_delay;
    bool m_rewind = false;
    /// The page the server holds for this volume, as far as this reader knows: what it said
    /// at opening, then each page written since. A page below it is written with `rewind`.
    int m_held = 0;
    /// A write the server refused, still owed: the next one carries the page reached since.
    bool m_unsent = false;

    /// A progress record existed when the volume opened, and whether it said finished; and
    /// whether a page has been turned since. « Never opened » is none of the three.
    bool m_hadProgress = false;
    bool m_finishedBefore = false;
    bool m_moved = false;

    bool m_atTheEnd = false;
    bool m_wayOnAsked = false;
    /// Answered: the rest of the series came back, whether or not anything follows.
    bool m_wayOnKnown = false;
    std::optional<Api::Entry> m_next;
    /// The entry before this one, from the same answer as `m_next`.
    std::optional<Api::Entry> m_previous;
    /// A turn back from the first page waiting for the rest of the series to say what precedes.
    bool m_backOwed = false;
    /// The volume opening is entered from its end: it lands on its last page.
    bool m_landAtTheEnd = false;
};
