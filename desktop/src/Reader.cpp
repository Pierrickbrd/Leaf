#include "Reader.h"

#include "Library.h"
#include "Preferences.h"
#include "Words.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

/// One second without turning before a position is written: leafing through twenty pages
/// is one request, not twenty (spec §08).
constexpr int Delay = 1000;

/// The step the width asked of the server goes up by. Asked at the exact width, every change
/// of the window — a drag, the panel opened, full screen — was a new address for every page
/// on screen, which went blank and was fetched again for a copy a few pixels off the one
/// drawn. Two hundred pixels is at most a fifth more than a page of a thousand needs, which
/// the scaling down hides, and a window dragged across the screen crosses a step a handful of
/// times instead of at every pixel.
constexpr int WidthStep = 200;

/// Three pages from the end is where what follows starts to matter: its cover has to be
/// there whole when the end card comes up (spec §10).
constexpr int NearTheEnd = 3;

static_assert(int(Reader::Direction::LeftToRight) == int(Api::ReadingDirection::LeftToRight)
                  && int(Reader::Direction::RightToLeft)
                         == int(Api::ReadingDirection::RightToLeft)
                  && int(Reader::Direction::Vertical) == int(Api::ReadingDirection::Vertical),
              "QML compares Reader.direction with Reader.Direction: the two must agree");

/// Every element of an array answer, or the first reason one of them is not what it says.
template <typename T>
QString readAll(const QJsonDocument &body, Api::Read<T> (*one)(const QJsonObject &),
                QList<T> &into)
{
    if (!body.isArray())
        return u"expected an array"_s;
    into.clear();
    for (const QJsonValue &value : body.array()) {
        if (!value.isObject())
            return u"expected an object"_s;
        const Api::Read<T> read = one(value.toObject());
        if (!read.ok())
            return read.trouble;
        into.append(*read.value);
    }
    return {};
}

/// `/entries/{id}` and `/series/{id}`, the id percent-encoded. An id is the server's word and
/// may hold anything a path does not: a `#` left as it was ended the path there — the request
/// went out for a shorter id with a fragment the server never saw — and a `/` was a step.
/// The output is ASCII, so reading it as UTF-8 is exact.
QString entryPath(const QString &id)
{
    return u"/entries/"_s + QString::fromUtf8(QUrl::toPercentEncoding(id));
}

QString seriesPath(const QString &id)
{
    return u"/series/"_s + QString::fromUtf8(QUrl::toPercentEncoding(id));
}

QString entryName(const Api::Entry &one)
{
    if (!one.number)
        return one.title.value_or(QString());
    return one.kind == Api::Entry::Kind::Chapter ? Words::readerChapter(*one.number)
                                                 : Words::readerVolume(*one.number);
}

} // namespace

Reader *Reader::create(QQmlEngine *engine, QJSEngine *)
{
    auto *server = engine->singletonInstance<Server *>(qmlTypeId("Leaf", 1, 0, "Server"));
    if (!server) {
        qWarning().noquote()
            << QStringLiteral("error resolving the Server singleton — the reader will open "
                              "nothing");
    }
    auto *library = engine->singletonInstance<Library *>(qmlTypeId("Leaf", 1, 0, "Library"));
    auto *preferences =
        engine->singletonInstance<Preferences *>(qmlTypeId("Leaf", 1, 0, "Preferences"));
    return new Reader(server, library, preferences);
}

Reader::Reader(Server *server, Library *library, Preferences *preferences, QObject *parent)
    : QObject(parent)
    , m_server(server)
    , m_library(library)
    , m_preferences(preferences)
{
    m_delay.setSingleShot(true);
    m_delay.setInterval(Delay);
    connect(&m_delay, &QTimer::timeout, this, &Reader::write);
    // The layout is chosen in the reader's own card, over the page: the change has to show
    // behind the card at once, not at the next page turned.
    if (m_preferences) {
        connect(m_preferences, &Preferences::changed, this, [this] {
            if (m_pages.isEmpty())
                return;
            lay();
            emit changed();
        });
    }
}

Reader::~Reader()
{
    // A last attempt, and not the one that counts: by the time the engine tears this down
    // the event loop has stopped, and a request queued now rarely leaves. Closing the window
    // is what writes a page turned less than a second ago (spec §08): `Main.qml` holds the
    // close until the write it sends is answered.
    flush();
}

QString Reader::seriesName() const
{
    return m_series ? m_series->name : QString();
}

QString Reader::entryLabel() const
{
    if (!m_entry)
        return {};
    // A chapter that arrived on its own is called what its chapter is called, « Chap. 13 »
    // or « Level.01 », and not « Chapitre 13 » made up here from its number: the bar read
    // « Chapitre 13 · Chap. 13 » of one file.
    if (m_entry->kind == Api::Entry::Kind::Chapter && m_chapters.size() == 1
        && !m_chapters.front().label.trimmed().isEmpty())
        return m_chapters.front().label;
    return entryName(*m_entry);
}

int Reader::firstPage() const
{
    return m_displays.isEmpty() ? -1 : m_displays.at(m_display).first;
}

int Reader::secondPage() const
{
    return m_displays.isEmpty() ? -1 : m_displays.at(m_display).second.value_or(-1);
}

int Reader::direction() const
{
    if (m_series && m_series->readingDirectionChosen)
        return int(*m_series->readingDirectionChosen);
    if (m_series && m_series->readingDirection)
        return int(*m_series->readingDirection);
    return int(Api::ReadingDirection::LeftToRight);
}

int Reader::declaredDirection() const
{
    return m_series && m_series->readingDirection ? int(*m_series->readingDirection) : -1;
}

int Reader::chosenDirection() const
{
    return m_series && m_series->readingDirectionChosen
               ? int(*m_series->readingDirectionChosen)
               : -1;
}

bool Reader::webtoon() const
{
    return m_series && m_series->medium == Api::Medium::Webtoon;
}

QVariantList Reader::chapters() const
{
    const int current = currentChapter();
    QVariantList all;
    for (int i = 0; i < m_chapters.size(); ++i) {
        const Api::Chapter &one = m_chapters.at(i);
        all.append(QVariantMap{{u"label"_s, one.label},
                               {u"title"_s, one.title.value_or(QString())},
                               {u"startPage"_s, one.startPage.value_or(-1)},
                               {u"current"_s, i == current}});
    }
    return all;
}

QVariantMap Reader::volume() const
{
    if (!m_entry || !m_server)
        return {};
    const int shown = std::max(firstPage(), secondPage());
    const bool finished = m_finishedBefore && !m_moved;
    QVariantMap about{{u"cover"_s, m_server->address() + entryPath(m_entry->id) + u"/cover"_s},
                      {u"pageCount"_s, pageCount()},
                      {u"page"_s, firstPage() + 1},
                      {u"started"_s, m_hadProgress || m_moved},
                      {u"finished"_s, finished},
                      // The shelf's own count (`Entries`): the page index over the pages, and
                      // full once finished — so the bar here and the row on the series page
                      // never disagree by a page about one record.
                      {u"howFarRead"_s,
                       finished ? 1.0
                                : pageCount() > 0
                                      ? std::clamp(qreal(std::max(0, shown)) / pageCount(), 0.0, 1.0)
                                      : 0.0}};
    const auto when = [&about](const QString &key, const std::optional<QString> &value) {
        if (value && !value->isEmpty())
            about.insert(key, *value);
    };
    when(u"title"_s, m_entry->title);
    // A chapter has no ISBN of its own: one in its file is the volume's it was cut from, and
    // the panel would offer to copy the number of another book.
    if (m_entry->kind != Api::Entry::Kind::Chapter)
        when(u"isbn"_s, m_entry->isbn);
    when(u"publishedOn"_s, m_entry->publishedOn);
    when(u"summary"_s, m_entry->summary);

    // The stretch the chapters cover, from their numbers: a chapter that names none says
    // nothing about it, and a volume none of whose chapters does has no stretch.
    std::optional<double> from;
    std::optional<double> to;
    for (const Api::Chapter &one : m_chapters) {
        if (!one.number)
            continue;
        from = from ? std::min(*from, *one.number) : *one.number;
        to = to ? std::max(*to, *one.number) : *one.number;
    }
    if (from && to)
        about.insert(u"chapters"_s, Words::readerChapterSpan(*from, *to));
    return about;
}

QVariantMap Reader::next() const
{
    if (!m_next || !m_server)
        return {};
    const bool chapter = m_next->kind == Api::Entry::Kind::Chapter;
    return {{u"entryId"_s, m_next->id},
            {u"label"_s, entryName(*m_next)},
            {u"cover"_s, m_server->address() + entryPath(m_next->id) + u"/cover"_s},
            {u"kind"_s, chapter ? u"Chapter"_s : u"Volume"_s},
            {u"action"_s,
             m_next->number ? Words::readOn(m_next->kind, *m_next->number) : QString()}};
}

QString Reader::ending() const
{
    if (!m_entry || !m_entry->number)
        return {};
    // Only once the series has answered: before that, no suite is not yet the same fact as
    // nothing after this one.
    return Words::ending(m_entry->kind, *m_entry->number, lastOfSeries());
}

void Reader::open(const QString &seriesId, const QString &entryId)
{
    openAt(seriesId, entryId, false);
}

void Reader::openAt(const QString &seriesId, const QString &entryId, bool atTheEnd)
{
    // Moving on from the end card is leaving this volume: its last page is written before
    // the next one replaces it, or the delay would write it under the wrong entry.
    flush();

    ++m_generation;
    m_seriesId = seriesId;
    m_entryId = entryId;
    m_series.reset();
    m_entry.reset();
    m_pages.clear();
    m_chapters.clear();
    m_displays.clear();
    m_display = 0;
    // The copy kept larger belongs to the volume it was asked for; this one starts from the
    // window it opens in, and asks its list of pages at that width.
    m_asked = 0;
    reach();
    m_rewind = false;
    m_held = 0;
    m_unsent = false;
    m_atTheEnd = false;
    m_wayOnAsked = false;
    m_wayOnKnown = false;
    m_next.reset();
    m_previous.reset();
    m_backOwed = false;
    m_landAtTheEnd = atTheEnd;
    m_trouble.clear();

    if (!m_server) {
        m_loading = false;
        m_trouble = Words::notSetUp(Words::Asking::Reader);
        emit changed();
        return;
    }

    m_loading = true;
    m_arriving = {};
    m_arriving.waiting = 5;
    emit changed();

    // Five questions at once rather than one after another: each one waited for would be a
    // round trip more before the first page, and none of them depends on another.
    const QString entry = entryPath(entryId);
    ask(entry, {}, [this](const Server::Answer &answer) {
        if (!answer.body.isObject())
            return u"entry: expected an object"_s;
        const Api::Read<Api::Entry> read = Api::entry(answer.body.object());
        if (read.ok())
            m_arriving.entry = read.value;
        return read.trouble;
    });
    // The width tells the server what the first pages will be asked for at, so it prepares
    // them while this is still being laid out. Unknown, nothing is said: a guessed width is
    // work thrown away.
    QUrlQuery sized;
    if (m_asked > 0)
        sized.addQueryItem(u"width"_s, QString::number(m_asked));
    ask(entry + u"/pages"_s, sized, [this](const Server::Answer &answer) {
        QList<Api::PageInfo> pages;
        const QString why = readAll(answer.body, &Api::pageInfo, pages);
        if (why.isEmpty())
            m_arriving.pages = pages;
        return why;
    });
    ask(entry + u"/chapters"_s, {}, [this](const Server::Answer &answer) {
        QList<Api::Chapter> chapters;
        const QString why = readAll(answer.body, &Api::chapter, chapters);
        if (why.isEmpty())
            m_arriving.chapters = chapters;
        return why;
    });
    ask(entry + u"/progress"_s, {}, [this](const Server::Answer &answer) {
        // Never opened: an answer, and the cover is where it starts.
        if (answer.status == 204) {
            m_arriving.progress = std::optional<Api::Progress>();
            return QString();
        }
        if (!answer.body.isObject())
            return u"progress: expected an object"_s;
        const Api::Read<Api::Progress> read = Api::progress(answer.body.object());
        if (read.ok())
            m_arriving.progress = read.value;
        return read.trouble;
    });
    ask(seriesPath(seriesId), {}, [this](const Server::Answer &answer) {
        if (!answer.body.isObject())
            return u"series: expected an object"_s;
        const Api::Read<Api::Series> read = Api::series(answer.body.object());
        if (read.ok())
            m_arriving.series = read.value;
        return read.trouble;
    });
}

void Reader::ask(const QString &path, const QUrlQuery &query,
                 std::function<QString(const Server::Answer &)> take)
{
    const int mine = m_generation;
    m_server->get(path, query, this, [this, mine, take](const Server::Answer &answer) {
        if (mine != m_generation)
            return;
        if (!answer.went()) {
            stopOpening(answer.trouble);
            return;
        }
        const QString why = take(answer);
        if (!why.isEmpty()) {
            stopOpening(why);
            return;
        }
        arrived();
    });
}

void Reader::stopOpening(const QString &why)
{
    // The first refusal is the one said; the four answers still out belong to nobody now.
    ++m_generation;
    m_loading = false;
    m_trouble = why;
    emit changed();
}

void Reader::arrived()
{
    if (--m_arriving.waiting > 0)
        return;

    m_entry = m_arriving.entry;
    m_series = m_arriving.series;
    m_pages = m_arriving.pages.value_or(QList<Api::PageInfo>());
    m_chapters = m_arriving.chapters.value_or(QList<Api::Chapter>());
    const std::optional<Api::Progress> where =
        m_arriving.progress.value_or(std::optional<Api::Progress>());
    m_arriving = {};

    // A finished volume opened again is a volume read again: it starts at the cover, and
    // its first write is below the page the server holds, so it says `rewind` (`write`).
    const bool finished = where && where->finished;
    m_held = where ? where->page : 0;
    m_hadProgress = where.has_value();
    m_finishedBefore = finished;
    m_moved = false;
    const int page = m_landAtTheEnd ? int(m_pages.size()) - 1
                     : where && !finished ? where->page
                                          : 0;
    m_landAtTheEnd = false;

    lay();
    m_display = std::max(0, Spreads::displayOf(m_displays, page));
    m_loading = false;
    emit changed();
    if (nearTheEnd())
        askTheWayOn();
}

Spreads::Layout Reader::layout() const
{
    // Two pages only where a book is read page by page across: a vertical one turns down,
    // and a webtoon is one strip (spec §04).
    if (webtoon() || direction() == int(Api::ReadingDirection::Vertical) || !m_preferences)
        return Spreads::Layout::Single;
    return m_preferences->layoutFor(m_window);
}

int Reader::pageWidth() const
{
    if (m_window.width() <= 0)
        return 0;
    const qreal room = m_window.width();
    const qreal slot = layout() == Spreads::Layout::Double ? room / 2 : room;
    return int(std::ceil(slot));
}

void Reader::lay()
{
    // The page being read is what stays: a window turned on its side changes how many
    // screens there are, so the screen's index means nothing across the change.
    const int keep = firstPage();
    m_displays = Spreads::lay(m_pages, layout());
    m_display = keep < 0 ? 0 : std::max(0, Spreads::displayOf(m_displays, keep));
    reach();
}

void Reader::reach()
{
    // Narrower keeps the larger copy, scaled down: what was drawn stays drawn, and nothing
    // is asked again for a window that will likely be widened back — full screen left, a
    // window dragged back.
    const int drawn = pageWidth();
    const int step = drawn > 0 ? (drawn + WidthStep - 1) / WidthStep * WidthStep : 0;
    m_asked = std::max(m_asked, step);
}

void Reader::relayout(QSizeF window)
{
    m_window = window;
    reach();
    if (m_pages.isEmpty())
        return;
    lay();
    emit changed();
}

void Reader::show(int display)
{
    m_display = display;
    m_moved = true;
    m_atTheEnd = false;
    m_backOwed = false;
    m_delay.start();
    emit changed();
    if (nearTheEnd())
        askTheWayOn();
}

bool Reader::close()
{
    const bool sent = flush();
    // Closed is closed: an opening still answering, or a choice still out, belongs to a
    // screen nobody is looking at any more and must not bring it back.
    m_delay.stop();
    ++m_generation;
    if (m_loading) {
        m_loading = false;
        emit changed();
    }
    return sent;
}

bool Reader::flush()
{
    return (m_delay.isActive() || m_unsent) && write();
}

void Reader::forward()
{
    if (m_displays.isEmpty())
        return;
    if (m_display + 1 < m_displays.size()) {
        show(m_display + 1);
        return;
    }
    if (!m_atTheEnd) {
        m_atTheEnd = true;
        emit changed();
        askTheWayOn();
        return;
    }
    // Once the card is up, turning the page does what its button does (spec §07).
    if (m_next)
        open(m_seriesId, m_next->id);
    else
        // The way on failed, or is still out: a turn is the reader asking again. A no-op
        // while one is out or once answered.
        askTheWayOn();
}

void Reader::backward()
{
    if (m_displays.isEmpty())
        return;
    if (m_atTheEnd) {
        m_atTheEnd = false;
        emit changed();
        return;
    }
    if (m_display > 0) {
        show(m_display - 1);
        return;
    }
    // Back from the first page is the end of the volume before, as forward from the last is
    // the start of the next: tried by hand on 22/10/2026, the back button on a first page
    // did nothing, where a reader paging back through a series expected the previous tome.
    // What precedes comes from the same list as what follows, asked now if it was not.
    if (m_wayOnKnown) {
        if (m_previous)
            openAt(m_seriesId, m_previous->id, true);
        return;
    }
    m_backOwed = true;
    askTheWayOn();
}

void Reader::goToPage(int page)
{
    const int display = Spreads::displayOf(m_displays, page);
    if (display >= 0)
        show(display);
}

void Reader::scrolledTo(int page)
{
    // Single layout in a webtoon, so a page is a display. Anything else is a strip that
    // reported before the book was laid out, or past it.
    const int display = Spreads::displayOf(m_displays, page);
    if (display < 0 || display == m_display)
        return;
    show(display);
}

void Reader::nextChapter()
{
    const int here = firstPage();
    std::optional<int> target;
    for (const Api::Chapter &one : std::as_const(m_chapters)) {
        // No start page is no jump: « I do not know » is not the cover.
        if (one.startPage && *one.startPage > here && (!target || *one.startPage < *target))
            target = one.startPage;
    }
    if (target)
        goToPage(*target);
}

void Reader::previousChapter()
{
    const int here = firstPage();
    std::optional<int> target;
    for (const Api::Chapter &one : std::as_const(m_chapters)) {
        if (one.startPage && *one.startPage < here && (!target || *one.startPage > *target))
            target = one.startPage;
    }
    if (target)
        goToPage(*target);
}

int Reader::currentChapter() const
{
    return chapterAt(firstPage());
}

int Reader::chapterAt(int page) const
{
    // The server's own rule, so the reader and the resume band name the same chapter: a
    // file holding one chapter is that chapter, and otherwise the last mark at or before
    // the page — a mark with no start page never answers.
    if (m_chapters.size() == 1)
        return 0;
    int found = -1;
    for (int i = 0; i < m_chapters.size(); ++i) {
        const std::optional<int> start = m_chapters.at(i).startPage;
        if (start && *start <= page
            && (found < 0 || *start > *m_chapters.at(found).startPage))
            found = i;
    }
    return found;
}

void Reader::chooseDirection(int direction)
{
    if (!m_server || !m_series || webtoon() || direction < -1
        || direction > int(Api::ReadingDirection::Vertical))
        return;
    // `null` and not an absent field: « Par défaut » is taking the choice away.
    const QJsonValue said =
        direction < 0 ? QJsonValue(QJsonValue::Null)
                      : QJsonValue(Api::spell(Api::ReadingDirection(direction)));
    const QByteArray body =
        QJsonDocument(QJsonObject{{u"readingDirection"_s, said}}).toJson(QJsonDocument::Compact);
    const int mine = m_generation;
    // The last click wins, and not the last answer: two choices in flight come back in the
    // network's order, and the older one landing second put the series back on the direction
    // the reader had just left.
    const int latest = ++m_choice;
    const QString seriesId = m_seriesId;
    m_server->set(seriesPath(seriesId) + u"/reading-direction"_s, body, this,
                  [this, mine, latest, seriesId](const Server::Answer &answer) {
                      if (mine != m_generation || latest != m_choice)
                          return;
                      if (!answer.went() || !answer.body.isObject()) {
                          emit refused(answer.went() ? u"series: expected an object"_s
                                                     : answer.trouble);
                          return;
                      }
                      const Api::Read<Api::Series> read = Api::series(answer.body.object());
                      if (!read.ok()) {
                          emit refused(read.trouble);
                          return;
                      }
                      m_series = read.value;
                      // A series turned vertical has no second page any more.
                      lay();
                      emit changed();
                      if (m_library)
                          m_library->touched(seriesId);
                  });
}

bool Reader::write()
{
    m_delay.stop();
    if (!m_server || m_displays.isEmpty())
        return false;
    // A pair counts as its later page: that is how far reading went (spec §08).
    const int page = Spreads::lastPageOf(m_displays.at(m_display));
    QJsonObject body{{u"page"_s, page}};
    // The page shown is the page recorded, backwards too. Tried by hand on 22/10/2026: a
    // reader gone back from page 46 to 40 and leaving found the shelf, and the next opening,
    // still at 46 — the server keeps the higher page unless told `rewind`, which was said
    // only once, after reopening a finished volume. Now any page below the one the server
    // holds says it.
    const bool rewind = m_rewind || page < m_held;
    if (page >= m_pages.size() - 1)
        body[u"finished"_s] = true;
    else if (rewind)
        // A volume read to its end and paged back must not stay « Lu » on the shelf at page
        // 40. The server clears `finished` on a rewind below the last page, as the contract
        // says — it did not until the test of 22/10/2026 — and this says it outright as
        // well, so the write means the same thing whatever the server makes of a rewind:
        // `finished` is where the reader stands now, `timesFinished` keeps that the end was
        // reached.
        body[u"finished"_s] = false;
    if (rewind)
        body[u"rewind"_s] = true;
    m_held = rewind ? page : std::max(m_held, page);
    m_rewind = false;
    m_unsent = false;

    // Bound to the server rather than to this reader: a write sent from the destructor still
    // has a series to refresh when it lands.
    QPointer<Library> library = m_library;
    QPointer<Reader> self = this;
    const QString seriesId = m_seriesId;
    const QString entryId = m_entryId;
    const int mine = m_generation;
    m_server->patch(entryPath(entryId) + u"/progress"_s,
                    QJsonDocument(body).toJson(QJsonDocument::Compact), nullptr,
                    [library, self, seriesId, entryId, mine, rewind](
                        const Server::Answer &answer) {
                        if (answer.went()) {
                            // One question for the series touched, and everything drawing it
                            // follows: the shelf, the series page, the resume band.
                            if (library)
                                library->touched(seriesId);
                            if (self)
                                emit self->recorded(seriesId);
                            return;
                        }
                        if (!self)
                            return;
                        // Still owed, and still owed a `rewind`: lost, the server keeps the
                        // old position and ignores every lower page of the re-read after it.
                        // The next screen writes again, still after the delay.
                        if (self->m_generation == mine && self->m_entryId == entryId) {
                            self->m_unsent = true;
                            self->m_rewind = self->m_rewind || rewind;
                        }
                        emit self->refused(answer.trouble);
                    });
    return true;
}

bool Reader::nearTheEnd() const
{
    if (m_displays.isEmpty())
        return false;
    return m_pages.size() - 1 - Spreads::lastPageOf(m_displays.at(m_display)) <= NearTheEnd;
}

void Reader::askTheWayOn()
{
    // Once per volume, and only near its end or on a turn back from its first page: most
    // volumes opened are not read to either end in one sitting, and the rest of the series
    // is a list of every entry in it.
    if (m_wayOnAsked || !m_server || m_seriesId.isEmpty())
        return;
    m_wayOnAsked = true;
    const int mine = m_generation;
    m_server->get(seriesPath(m_seriesId) + u"/entries"_s, this,
                  [this, mine](const Server::Answer &answer) {
                      if (mine != m_generation)
                          return;
                      QList<Api::Entry> all;
                      // Unanswered is not « nothing follows »: asked again on the next screen.
                      if (!answer.went() || !readAll(answer.body, &Api::entry, all).isEmpty()) {
                          m_wayOnAsked = false;
                          return;
                      }
                      // The entries on either side of this one in the order received,
                      // which is the edition's: the client never re-sorts what the server
                      // ordered.
                      for (int i = 0; i < all.size(); ++i) {
                          if (all.at(i).id != m_entryId)
                              continue;
                          if (i + 1 < all.size())
                              m_next = all.at(i + 1);
                          if (i > 0)
                              m_previous = all.at(i - 1);
                          break;
                      }
                      m_wayOnKnown = true;
                      emit changed();
                      // Still on the first page, and still going back: nothing turned since.
                      if (m_backOwed) {
                          m_backOwed = false;
                          if (m_previous && m_display == 0 && !m_atTheEnd)
                              openAt(m_seriesId, m_previous->id, true);
                      }
                  });
}

QString Reader::pageUrl(int page, int width) const
{
    if (!m_server || m_entryId.isEmpty() || page < 0 || page >= m_pages.size())
        return {};
    QString url = m_server->address() + entryPath(m_entryId) + u"/pages/"_s
                  + QString::number(page);
    if (width > 0)
        url += u"?width="_s + QString::number(width);
    return url;
}

qreal Reader::pageRatio(int page) const
{
    if (page < 0 || page >= m_pages.size())
        return 0;
    const Api::PageInfo &one = m_pages.at(page);
    return one.width && one.height && *one.width > 0 ? qreal(*one.height) / *one.width : 0;
}

int Reader::pageNativeWidth(int page) const
{
    if (page < 0 || page >= m_pages.size())
        return 0;
    return m_pages.at(page).width.value_or(0);
}

QStringList Reader::ahead(int width) const
{
    QStringList wanted;
    if (m_displays.isEmpty())
        return wanted;
    const auto pagesOf = [this, width, &wanted](qsizetype display) {
        if (display < 0 || display >= m_displays.size())
            return;
        const Spreads::Display &one = m_displays.at(display);
        wanted << pageUrl(one.first, width);
        if (one.second)
            wanted << pageUrl(*one.second, width);
    };
    // Two screens forward so that turning never shows a gap, one back so that turning back
    // is instant (spec §10).
    pagesOf(m_display + 1);
    pagesOf(m_display + 2);
    pagesOf(m_display - 1);
    if (nearTheEnd() && m_next)
        wanted << next().value(u"cover"_s).toString();
    return wanted;
}
