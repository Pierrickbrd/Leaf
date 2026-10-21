// The open book: what it asks for, where it lands, what it writes and what comes after it.
//
// Against the forty-line server and no window, because every decision here is a request or
// its absence — and the ones that matter most are the requests that must not happen: twenty
// writes for twenty pages turned, a `rewind` missing from a page written backwards, a whole
// shelf asked for again because one page was read.

#include "Library.h"
#include "Preferences.h"
#include "Pretend.h"
#include "Reader.h"
#include "Server.h"
#include "Settings.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <QUrl>

using namespace Qt::StringLiterals;

namespace {

QByteArray json(const QJsonValue &value)
{
    return value.isArray() ? QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact)
                           : QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
}

QByteArray answering(int status, const QByteArray &body)
{
    return "HTTP/1.1 " + QByteArray::number(status) + " .\r\nContent-Type: application/json\r\n"
           "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
}

/// One request as the server read it: its first line and its body.
struct Heard {
    QByteArray line;
    QJsonObject body;
};

} // namespace

class HoldsTheReader : public QObject
{
    Q_OBJECT

    Pretend *m_pretend = nullptr;
    Settings *m_settings = nullptr;
    Server *m_server = nullptr;
    Library *m_library = nullptr;
    Preferences *m_preferences = nullptr;
    Reader *m_reader = nullptr;

    // What the server holds, changed by each test before it opens anything.
    QString m_medium = u"manga"_s;
    QJsonValue m_declared = u"RIGHT_TO_LEFT"_s;
    QJsonValue m_chosen = QJsonValue::Undefined;
    int m_pageCount = 190;
    QJsonValue m_progress = QJsonValue::Undefined;
    QJsonArray m_chapters;
    /// What the open volume carries beyond its number: title, isbn, date, summary.
    QJsonObject m_more;
    QStringList m_entries{u"e11"_s, u"e12"_s, u"e13"_s};
    bool m_writesFail = false;
    bool m_wayOnFails = false;
    /// A write to tome 12 changes what its progress answers afterwards, as the server would.
    bool m_keepsWrites = false;

    /// Tome 11 is shorter, so that an answer about it landing on tome 12 shows.
    int pagesIn(const QString &id) const { return id == u"e11"_s ? 100 : m_pageCount; }

    QJsonObject series() const
    {
        QJsonObject one{{u"id"_s, u"s1"_s},
                        {u"workId"_s, u"w1"_s},
                        {u"name"_s, u"Assassination Classroom"_s},
                        {u"work"_s, u"Assassination Classroom"_s},
                        {u"medium"_s, m_medium},
                        {u"ownedVolumes"_s, 3},
                        {u"entryCount"_s, 3},
                        {u"chapterCount"_s, 0},
                        {u"arcCount"_s, 0}};
        if (!m_declared.isUndefined())
            one[u"readingDirection"_s] = m_declared;
        if (!m_chosen.isUndefined())
            one[u"readingDirectionChosen"_s] = m_chosen;
        return one;
    }

    QJsonObject entry(const QString &id) const
    {
        QJsonObject one{{u"id"_s, id},
                        {u"type"_s, u"VOLUME"_s},
                        {u"number"_s, id.mid(1).toInt()},
                        {u"pageCount"_s, pagesIn(id)},
                        {u"chapterCount"_s, m_chapters.size()},
                        {u"file"_s, id + u".cbz"_s}};
        for (auto it = m_more.begin(); it != m_more.end(); ++it)
            one[it.key()] = it.value();
        return one;
    }

    QJsonArray pages(const QString &id) const
    {
        QJsonArray all;
        for (int i = 0; i < pagesIn(id); ++i)
            all.append(QJsonObject{{u"number"_s, i},
                                   {u"name"_s, u"%1.jpg"_s.arg(i, 3, 10, QChar(u'0'))},
                                   {u"mediaType"_s, u"image/jpeg"_s},
                                   {u"width"_s, 1000},
                                   {u"height"_s, 1500}});
        return all;
    }

    /// Every route the reader may ask for, answered from the fields above.
    QByteArray route(const QByteArray &request)
    {
        const QByteArray line = request.left(request.indexOf("\r\n"));
        const QList<QByteArray> words = line.split(' ');
        const QByteArray verb = words.value(0);
        const QString path = QString::fromUtf8(words.value(1).split('?').first());

        if (verb == "PATCH" && m_writesFail)
            return answering(500, "{}");
        if (verb == "PATCH" && path == u"/entries/e12/progress"_s && m_keepsWrites) {
            // The server's rule: a lower page only with `rewind`, `finished` only when said.
            const QJsonObject said =
                QJsonDocument::fromJson(request.mid(request.indexOf("\r\n\r\n") + 4)).object();
            QJsonObject now = m_progress.isObject() ? m_progress.toObject()
                                                    : QJsonObject{{u"page"_s, 0},
                                                                  {u"finished"_s, false}};
            const int page = said.value(u"page"_s).toInt();
            if (said.value(u"rewind"_s).toBool() || page > now.value(u"page"_s).toInt())
                now[u"page"_s] = page;
            if (said.contains(u"finished"_s))
                now[u"finished"_s] = said.value(u"finished"_s);
            m_progress = now;
        }
        if (verb == "PATCH" && path.endsWith(u"/progress"_s))
            return answering(200, json(QJsonObject{{u"entryId"_s, path.split(u'/').at(2)},
                                                   {u"page"_s, 1},
                                                   {u"pageCount"_s, m_pageCount}}));
        if (verb == "PUT" && path == u"/series/s1/reading-direction"_s) {
            QJsonObject now = series();
            const QJsonValue said =
                QJsonDocument::fromJson(request.mid(request.indexOf("\r\n\r\n") + 4))
                    .object()
                    .value(u"readingDirection"_s);
            if (said.isNull())
                now.remove(u"readingDirectionChosen"_s);
            else
                now[u"readingDirectionChosen"_s] = said;
            return answering(200, json(now));
        }
        if (path == u"/series/s1"_s)
            return answering(200, json(series()));
        if (path == u"/series/s1/entries"_s && m_wayOnFails)
            return answering(500, "{}");
        if (path == u"/series/s1/entries"_s) {
            QJsonArray all;
            for (const QString &id : m_entries)
                all.append(entry(id));
            return answering(200, json(all));
        }
        if (path.startsWith(u"/entries/"_s)) {
            // Read as a server reads it: the id is the segment, decoded.
            const QString id = QUrl::fromPercentEncoding(path.split(u'/').at(2).toUtf8());
            if (path.endsWith(u"/pages"_s))
                return answering(200, json(pages(id)));
            if (path.endsWith(u"/chapters"_s))
                return answering(200, json(m_chapters));
            if (path.endsWith(u"/progress"_s)) {
                if (m_progress.isUndefined())
                    return "HTTP/1.1 204 .\r\nContent-Length: 0\r\n\r\n";
                QJsonObject where = m_progress.toObject();
                where[u"entryId"_s] = id;
                where[u"pageCount"_s] = m_pageCount;
                return answering(200, json(where));
            }
            return answering(200, json(entry(id)));
        }
        return answering(404, "{}");
    }

    /// Everything the server was asked, one request at a time.
    QList<Heard> heard() const
    {
        QList<Heard> all;
        QByteArray rest = m_pretend->heard;
        while (true) {
            const qsizetype end = rest.indexOf("\r\n\r\n");
            if (end < 0)
                break;
            const QByteArray head = rest.left(end);
            qint64 length = 0;
            for (const QByteArray &one : head.split('\n')) {
                const QByteArray trimmed = one.trimmed();
                if (trimmed.toLower().startsWith("content-length:"))
                    length = trimmed.mid(trimmed.indexOf(':') + 1).trimmed().toLongLong();
            }
            all.append({head.left(head.indexOf("\r\n")),
                        QJsonDocument::fromJson(rest.mid(end + 4, length)).object()});
            rest.remove(0, end + 4 + length);
        }
        return all;
    }

    /// How many requests began with `start` — a verb and a path, the query included.
    int asked(const QByteArray &start) const
    {
        int count = 0;
        for (const Heard &one : heard())
            count += one.line.startsWith(start) ? 1 : 0;
        return count;
    }

    QList<QJsonObject> writes() const
    {
        QList<QJsonObject> all;
        for (const Heard &one : heard())
            if (one.line.startsWith("PATCH /entries/e12/progress "))
                all.append(one.body);
        return all;
    }

    void opened(const QString &entryId = u"e12"_s)
    {
        m_reader->open(u"s1"_s, entryId);
        QTRY_VERIFY_WITH_TIMEOUT(!m_reader->loading(), 3000);
        QVERIFY2(m_reader->trouble().isEmpty(), qPrintable(m_reader->trouble()));
    }

    /// The reader in two pages: a landscape window and a preference that says so.
    void inPairs()
    {
        m_preferences->choosePageLayout(Preferences::PageLayout::Double);
        m_reader->relayout(QSizeF(1600, 1000));
    }

    QString page(int number) const { return m_reader->pageUrl(number, 800); }

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void init()
    {
        qunsetenv("LEAF_ADDRESS");
        qunsetenv("LEAF_KEY");
        m_pretend = new Pretend;
        QVERIFY(m_pretend->listen(QHostAddress::LocalHost));
        m_pretend->answerFor = [this](const QByteArray &request) { return route(request); };
        m_settings = new Settings;
        QSignalSpy loaded(m_settings, &Settings::changed);
        QVERIFY(loaded.wait(5000));
        m_settings->setAddress(u"http://127.0.0.1:%1"_s.arg(m_pretend->serverPort()));
        m_settings->setKey(u"8f3a92c1d4e5b6a7"_s);
        m_server = new Server(m_settings);
        m_library = new Library(m_server);
        m_preferences = new Preferences;
        m_preferences->choosePageLayout(Preferences::PageLayout::Single);
        m_reader = new Reader(m_server, m_library, m_preferences);
        m_reader->relayout(QSizeF(1000, 1600));

        m_medium = u"manga"_s;
        m_declared = u"RIGHT_TO_LEFT"_s;
        m_chosen = QJsonValue::Undefined;
        m_pageCount = 190;
        m_progress = QJsonValue::Undefined;
        m_chapters = {};
        m_more = {};
        m_entries = {u"e11"_s, u"e12"_s, u"e13"_s};
        m_writesFail = false;
        m_wayOnFails = false;
        m_keepsWrites = false;
    }

    void cleanup()
    {
        delete m_reader;
        delete m_preferences;
        delete m_library;
        delete m_server;
        delete m_settings;
        m_pretend->answerFor = nullptr;
        m_pretend->holdBack = nullptr;
        delete m_pretend;
    }

    /// Five questions, each once, in parallel — the pages to lay out, where reading stopped,
    /// the series for its direction, the entry for its labels and the chapters for their
    /// marks — and the page that opens is the one the record holds.
    void opening_asks_each_thing_once_and_lands_where_reading_stopped()
    {
        inPairs();
        m_progress = QJsonObject{{u"page"_s, 46}, {u"finished"_s, false}};
        opened();

        QCOMPARE(asked("GET /entries/e12 "), 1);
        QCOMPARE(asked("GET /entries/e12/pages?width=800 "), 1);
        QCOMPARE(asked("GET /entries/e12/chapters "), 1);
        QCOMPARE(asked("GET /entries/e12/progress "), 1);
        QCOMPARE(asked("GET /series/s1 "), 1);
        QCOMPARE(heard().size(), 5);

        QCOMPARE(m_reader->pageCount(), 190);
        QVERIFY(m_reader->firstPage() == 46 || m_reader->secondPage() == 46);
        QCOMPARE(m_reader->firstPage(), 45);
        QCOMPARE(m_reader->secondPage(), 46);
        QCOMPARE(m_reader->seriesName(), u"Assassination Classroom"_s);
        QCOMPARE(m_reader->entryLabel(), u"Tome 12"_s);
        // Landing is not reading: nothing is written for a page nobody has turned yet.
        QTest::qWait(1100);
        QVERIFY(writes().isEmpty());
    }

    /// A 204 is « never opened », which is an answer: the cover, and nothing to rewind.
    void a_volume_never_opened_starts_at_the_cover_and_rewinds_nothing()
    {
        opened();
        QCOMPARE(m_reader->firstPage(), 0);
        QCOMPARE(m_reader->secondPage(), -1);

        m_reader->forward();
        QTRY_COMPARE_WITH_TIMEOUT(writes().size(), 1, 3000);
        QVERIFY(!writes().first().contains(u"rewind"_s));
    }

    /// A finished volume reopens at its cover, and the server has to be told that is on
    /// purpose: progress never moves backwards on its own.
    void reopening_a_finished_volume_starts_over_and_says_rewind()
    {
        m_progress = QJsonObject{{u"page"_s, 189}, {u"finished"_s, true}};
        opened();
        QCOMPARE(m_reader->firstPage(), 0);

        m_reader->forward();
        QTRY_COMPARE_WITH_TIMEOUT(writes().size(), 1, 3000);
        QCOMPARE(writes().at(0).value(u"page"_s).toInt(), 1);
        QCOMPARE(writes().at(0).value(u"rewind"_s).toBool(), true);
        // The server's rewind leaves `finished` alone: read again is not finished, and that
        // has to be said.
        QCOMPARE(writes().at(0).value(u"finished"_s), QJsonValue(false));

        // Once: the second write is ordinary reading again.
        m_reader->forward();
        QTRY_COMPARE_WITH_TIMEOUT(writes().size(), 2, 3000);
        QVERIFY(!writes().at(1).contains(u"rewind"_s));
    }

    void turning_twenty_pages_quickly_writes_once()
    {
        opened();
        for (int i = 0; i < 20; ++i)
            m_reader->forward();
        QCOMPARE(m_reader->firstPage(), 20);
        QTest::qWait(500);
        QVERIFY2(writes().isEmpty(), "written before a second had passed without turning");

        QTRY_COMPARE_WITH_TIMEOUT(writes().size(), 1, 3000);
        QTest::qWait(300);
        QCOMPARE(writes().size(), 1);
        QCOMPARE(writes().first().value(u"page"_s).toInt(), 20);
    }

    /// Closing does not wait out the delay: what was read is written as the reader leaves.
    void leaving_sends_what_the_delay_was_holding()
    {
        opened();
        m_reader->forward();
        m_reader->close();
        // Well under the second the delay would have taken.
        QTRY_COMPARE_WITH_TIMEOUT(writes().size(), 1, 400);
        QCOMPARE(writes().first().value(u"page"_s).toInt(), 1);
    }

    /// What draws a volume's state asks again once the server has kept the position, and not
    /// when it was sent: asked at leaving, the answer could come back with the page before.
    /// `close()` says whether there is such a write to wait for, and a refusal says nothing.
    void a_write_that_lands_says_so_and_leaving_says_whether_one_left()
    {
        opened();
        QSignalSpy recorded(m_reader, &Reader::recorded);
        QVERIFY(!m_reader->close());
        opened();
        m_reader->forward();
        QVERIFY(m_reader->close());
        QTRY_COMPARE_WITH_TIMEOUT(recorded.size(), 1, 2000);
        QCOMPARE(recorded.first().first().toString(), u"s1"_s);
        // Nothing owed once it has gone.
        QVERIFY(!m_reader->close());

        m_writesFail = true;
        opened();
        QSignalSpy refused(m_reader, &Reader::refused);
        m_reader->forward();
        QVERIFY(m_reader->close());
        QTRY_COMPARE_WITH_TIMEOUT(refused.size(), 1, 2000);
        QCOMPARE(recorded.size(), 1);
    }

    /// Read up to the later of the two pages on screen, which is where reading reached.
    void a_pair_is_recorded_at_its_second_page()
    {
        inPairs();
        opened();
        m_reader->forward();
        QCOMPARE(m_reader->firstPage(), 1);
        QCOMPARE(m_reader->secondPage(), 2);
        QTRY_COMPARE_WITH_TIMEOUT(writes().size(), 1, 3000);
        QCOMPARE(writes().first().value(u"page"_s).toInt(), 2);
    }

    void reaching_the_last_page_says_finished()
    {
        opened();
        m_reader->goToPage(188);
        QTRY_COMPARE_WITH_TIMEOUT(writes().size(), 1, 3000);
        QVERIFY(!writes().last().contains(u"finished"_s));

        m_reader->forward();
        QTRY_COMPARE_WITH_TIMEOUT(writes().size(), 2, 3000);
        QCOMPARE(writes().last().value(u"page"_s).toInt(), 189);
        QCOMPARE(writes().last().value(u"finished"_s).toBool(), true);
    }

    /// The page shown is the page recorded, backwards too. Tried by hand on 22/10/2026: gone
    /// back from 46 to 40 and left, the volume reopened at 46, because a lower page without
    /// `rewind` is one the server ignores.
    void going_back_records_the_page_shown_and_reopens_there()
    {
        m_keepsWrites = true;
        m_progress = QJsonObject{{u"page"_s, 46}, {u"finished"_s, false}};
        opened();
        for (int i = 0; i < 6; ++i)
            m_reader->backward();
        QCOMPARE(m_reader->firstPage(), 40);
        QVERIFY(m_reader->close());
        QTRY_COMPARE_WITH_TIMEOUT(writes().size(), 1, 3000);
        QCOMPARE(writes().first().value(u"page"_s).toInt(), 40);
        QCOMPARE(writes().first().value(u"rewind"_s).toBool(), true);

        opened();
        QCOMPARE(m_reader->firstPage(), 40);
    }

    /// Going forward again after that is ordinary reading: no `rewind` above the page held.
    void going_forward_after_going_back_says_no_rewind()
    {
        m_progress = QJsonObject{{u"page"_s, 46}, {u"finished"_s, false}};
        opened();
        m_reader->backward();
        QTRY_COMPARE_WITH_TIMEOUT(writes().size(), 1, 3000);
        QCOMPARE(writes().first().value(u"rewind"_s).toBool(), true);
        m_reader->forward();
        m_reader->forward();
        QTRY_COMPARE_WITH_TIMEOUT(writes().size(), 2, 3000);
        QCOMPARE(writes().last().value(u"page"_s).toInt(), 47);
        QVERIFY(!writes().last().contains(u"rewind"_s));
        QVERIFY(!writes().last().contains(u"finished"_s));
    }

    /// Read to the end, then paged back: the record follows to the page shown and is no
    /// longer finished — the server keeps `timesFinished`, which is what says it was read.
    void paging_back_from_the_end_is_no_longer_finished()
    {
        m_keepsWrites = true;
        m_progress = QJsonObject{{u"page"_s, 188}, {u"finished"_s, false}};
        opened();
        m_reader->forward();
        QTRY_COMPARE_WITH_TIMEOUT(writes().size(), 1, 3000);
        QCOMPARE(writes().last().value(u"finished"_s).toBool(), true);

        for (int i = 0; i < 4; ++i)
            m_reader->backward();
        QTRY_COMPARE_WITH_TIMEOUT(writes().size(), 2, 3000);
        QCOMPARE(writes().last().value(u"page"_s).toInt(), 185);
        QCOMPARE(writes().last().value(u"rewind"_s).toBool(), true);
        QCOMPARE(writes().last().value(u"finished"_s), QJsonValue(false));
        QCOMPARE(m_progress.toObject().value(u"finished"_s).toBool(), false);
    }

    /// One series asked for, and only that one: every screen drawing it follows that answer.
    void every_write_refreshes_the_one_series_and_nothing_else()
    {
        opened();
        m_pretend->heard.clear();
        m_reader->forward();
        QTRY_COMPARE_WITH_TIMEOUT(asked("GET /series/s1 "), 1, 3000);
        QTest::qWait(200);
        QCOMPARE(asked("GET /series/s1 "), 1);
        QCOMPARE(asked("GET /series?"), 0);
        QCOMPARE(asked("GET /series "), 0);
        QCOMPARE(heard().size(), 2);
    }

    void a_series_that_declares_nothing_reads_left_to_right()
    {
        m_declared = QJsonValue::Undefined;
        opened();
        QCOMPARE(m_reader->direction(), int(Reader::Direction::LeftToRight));
        QCOMPARE(m_reader->declaredDirection(), -1);
        QCOMPARE(m_reader->chosenDirection(), -1);
    }

    /// The choice wins over the files, and « Par défaut » is taking the choice away — which
    /// the server reads from a `null`, not from an absent field.
    void a_direction_chosen_wins_and_default_follows_the_files()
    {
        m_chosen = u"LEFT_TO_RIGHT"_s;
        opened();
        QCOMPARE(m_reader->direction(), int(Reader::Direction::LeftToRight));
        QCOMPARE(m_reader->declaredDirection(), int(Reader::Direction::RightToLeft));
        QCOMPARE(m_reader->chosenDirection(), int(Reader::Direction::LeftToRight));

        m_pretend->heard.clear();
        m_reader->chooseDirection(-1);
        QTRY_COMPARE_WITH_TIMEOUT(m_reader->chosenDirection(), -1, 3000);
        QCOMPARE(m_reader->direction(), int(Reader::Direction::RightToLeft));

        QCOMPARE(asked("PUT /series/s1/reading-direction "), 1);
        for (const Heard &one : heard())
            if (one.line.startsWith("PUT "))
                QCOMPARE(json(one.body), QByteArray(R"({"readingDirection":null})"));
        QVERIFY(m_pretend->heard.contains("Content-Type: application/json"));
        // And everything else that draws the series hears of it.
        QTRY_COMPARE_WITH_TIMEOUT(asked("GET /series/s1 "), 1, 3000);

        m_reader->chooseDirection(int(Reader::Direction::Vertical));
        QTRY_COMPARE_WITH_TIMEOUT(m_reader->chosenDirection(),
                                  int(Reader::Direction::Vertical), 3000);
        for (const Heard &one : heard())
            if (one.line.startsWith("PUT ") && !one.body.value(u"readingDirection"_s).isNull())
                QCOMPARE(one.body.value(u"readingDirection"_s).toString(), u"VERTICAL"_s);
        // Turned down the page, a series has no second page to show beside the first.
        QCOMPARE(m_reader->secondPage(), -1);
    }

    /// Two choices in flight: the last click wins whichever answer lands last. Held back and
    /// released both ways round, because the order the network picks is not the order the
    /// reader clicked in.
    void the_last_direction_clicked_wins_whatever_order_the_answers_come_back()
    {
        for (const bool backwards : {false, true}) {
            m_chosen = QJsonValue::Undefined;
            opened();
            m_pretend->heard.clear();
            m_pretend->holdBack = [](const QByteArray &request) {
                return request.startsWith("PUT ");
            };
            m_reader->chooseDirection(int(Reader::Direction::LeftToRight));
            m_reader->chooseDirection(int(Reader::Direction::Vertical));
            QTRY_COMPARE_WITH_TIMEOUT(asked("PUT /series/s1/reading-direction "), 2, 3000);
            m_pretend->holdBack = nullptr;
            m_pretend->release(backwards);
            QTRY_COMPARE_WITH_TIMEOUT(m_reader->chosenDirection(),
                                      int(Reader::Direction::Vertical), 3000);
            QTest::qWait(200);
            QCOMPARE(m_reader->chosenDirection(), int(Reader::Direction::Vertical));
            QCOMPARE(m_reader->direction(), int(Reader::Direction::Vertical));
        }
    }

    void a_webtoon_has_no_direction_to_choose()
    {
        m_medium = u"webtoon"_s;
        m_declared = u"VERTICAL"_s;
        opened();
        QVERIFY(m_reader->webtoon());

        m_pretend->heard.clear();
        m_reader->chooseDirection(1);
        QTest::qWait(300);
        QVERIFY(m_pretend->heard.isEmpty());
        QCOMPARE(m_reader->direction(), int(Reader::Direction::Vertical));
    }

    /// The strip tells `Reader` which page crosses the middle of the view, and that is the
    /// page recorded: the same delay as a turn, so a flick across forty pages is one write.
    void a_webtoon_records_the_page_crossing_the_middle()
    {
        m_medium = u"webtoon"_s;
        m_declared = u"VERTICAL"_s;
        opened();
        QVERIFY(m_reader->webtoon());

        m_pretend->heard.clear();
        for (int page = 10; page <= 50; ++page)
            m_reader->scrolledTo(page);
        // It is the page shown, as a turn would have made it, and never a pair.
        QCOMPARE(m_reader->firstPage(), 50);
        QCOMPARE(m_reader->secondPage(), -1);
        QTest::qWait(300);
        QCOMPARE(writes().size(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(writes().size(), 1, 3000);
        QCOMPARE(writes().first().value(u"page"_s).toInt(), 50);
        QVERIFY(!writes().first().contains(u"finished"_s));
    }

    /// The bottom of the strip is the last page shown even when that page is too short to
    /// ever cross the middle: the strip says so with the last page, and it is `finished`.
    void a_webtoon_scrolled_to_its_last_page_says_finished()
    {
        m_medium = u"webtoon"_s;
        m_declared = u"VERTICAL"_s;
        opened();
        m_reader->scrolledTo(m_pageCount - 1);
        QTRY_COMPARE_WITH_TIMEOUT(writes().size(), 1, 3000);
        QCOMPARE(writes().last().value(u"page"_s).toInt(), m_pageCount - 1);
        QCOMPARE(writes().last().value(u"finished"_s).toBool(), true);
    }

    /// The strip reports where it already is at every pixel of a scroll; the same page again
    /// is not a new reading, so it neither restarts the delay nor redraws the screen.
    void the_page_already_shown_is_not_scrolled_to_again()
    {
        m_medium = u"webtoon"_s;
        m_declared = u"VERTICAL"_s;
        opened();
        m_reader->scrolledTo(7);
        QSignalSpy changed(m_reader, &Reader::changed);
        m_reader->scrolledTo(7);
        m_reader->scrolledTo(-1);
        m_reader->scrolledTo(m_pageCount);
        QCOMPARE(changed.size(), 0);
        QCOMPARE(m_reader->firstPage(), 7);
    }

    /// The strip draws a page at its known proportions before the image arrives, so it does
    /// not jump while pages load; a page that does not say gives none.
    void a_page_gives_its_proportions_for_the_strip_to_reserve_room()
    {
        m_medium = u"webtoon"_s;
        opened();
        QCOMPARE(m_reader->pageRatio(0), 1.5);
        QCOMPARE(m_reader->pageRatio(-1), 0.0);
        QCOMPARE(m_reader->pageRatio(m_pageCount), 0.0);
    }

    /// The bubble over the progress bar names the chapter of the page under the pointer, not
    /// of the page on screen — by the rule that names the chapter on screen, so the two never
    /// disagree about one page.
    void the_chapter_under_any_page_is_the_last_one_begun_by_then()
    {
        m_chapters = {
            QJsonObject{{u"id"_s, u"c98"_s}, {u"label"_s, u"Chap.098"_s},
                        {u"kind"_s, u"CHAPTER"_s}, {u"entryId"_s, u"e12"_s},
                        {u"startPage"_s, 0}},
            QJsonObject{{u"id"_s, u"c99"_s}, {u"label"_s, u"Chap.099"_s},
                        {u"kind"_s, u"CHAPTER"_s}, {u"entryId"_s, u"e12"_s}},
            QJsonObject{{u"id"_s, u"c100"_s}, {u"label"_s, u"Chap.100"_s},
                        {u"kind"_s, u"CHAPTER"_s}, {u"entryId"_s, u"e12"_s},
                        {u"startPage"_s, 25}},
        };
        opened();
        QCOMPARE(m_reader->chapterAt(0), 0);
        QCOMPARE(m_reader->chapterAt(24), 0);
        QCOMPARE(m_reader->chapterAt(25), 2);
        QCOMPARE(m_reader->chapterAt(189), 2);
        m_reader->goToPage(30);
        QVERIFY(m_reader->chapters().at(2).toMap().value(u"current"_s).toBool());
    }

    /// An unknown start is unknown, not the first page: no mark on the bar, and the next
    /// chapter is the next one that says where it begins.
    void a_chapter_without_a_start_page_has_no_mark_and_no_jump()
    {
        m_chapters = {
            // Label and title apart, as the server sends them: it cuts « Chap.098 : Le temps
            // des examens » in two before anything reads it.
            QJsonObject{{u"id"_s, u"c98"_s}, {u"label"_s, u"Chap.098"_s},
                        {u"title"_s, u"Le temps des examens"_s}, {u"kind"_s, u"CHAPTER"_s},
                        {u"entryId"_s, u"e12"_s}, {u"startPage"_s, 0}},
            QJsonObject{{u"id"_s, u"c99"_s}, {u"label"_s, u"Chap.099"_s},
                        {u"title"_s, u"Coup de sifflet"_s}, {u"kind"_s, u"CHAPTER"_s},
                        {u"entryId"_s, u"e12"_s}},
            QJsonObject{{u"id"_s, u"c100"_s}, {u"label"_s, u"Chap.100"_s},
                        {u"title"_s, u"Ridicule"_s}, {u"kind"_s, u"CHAPTER"_s},
                        {u"entryId"_s, u"e12"_s}, {u"startPage"_s, 25}},
        };
        m_progress = QJsonObject{{u"page"_s, 10}, {u"finished"_s, false}};
        opened();

        const QVariantList marks = m_reader->chapters();
        QCOMPARE(marks.size(), 3);
        QCOMPARE(marks.at(0).toMap().value(u"startPage"_s).toInt(), 0);
        QCOMPARE(marks.at(1).toMap().value(u"startPage"_s).toInt(), -1);
        QCOMPARE(marks.at(2).toMap().value(u"startPage"_s).toInt(), 25);
        QCOMPARE(marks.at(0).toMap().value(u"title"_s).toString(), u"Le temps des examens"_s);
        QVERIFY(marks.at(0).toMap().value(u"current"_s).toBool());
        QVERIFY(!marks.at(1).toMap().value(u"current"_s).toBool());
        QCOMPARE(m_reader->chapterAt(m_reader->firstPage()), 0);

        m_reader->nextChapter();
        QCOMPARE(m_reader->firstPage(), 25);
        QCOMPARE(m_reader->chapterAt(m_reader->firstPage()), 2);
        QVERIFY(m_reader->chapters().at(2).toMap().value(u"current"_s).toBool());

        // Past the last mark there is nowhere to go, and the page stays.
        m_reader->nextChapter();
        QCOMPARE(m_reader->firstPage(), 25);

        m_reader->previousChapter();
        QCOMPARE(m_reader->firstPage(), 0);
    }

    /// What the panel beside the pages says of the volume: the fields the server gave, in the
    /// words that need no French, and the stretch of chapters it holds.
    void the_open_volume_tells_the_panel_what_it_is()
    {
        m_more = QJsonObject{{u"title"_s, u"Le temps des examens"_s},
                             {u"isbn"_s, u"978-2-505-06215-9"_s},
                             {u"publishedOn"_s, u"2015-03-04"_s},
                             {u"summary"_s, u"Les examens approchent."_s}};
        m_chapters = {
            QJsonObject{{u"id"_s, u"c98"_s}, {u"label"_s, u"Chap.098"_s}, {u"number"_s, 98},
                        {u"kind"_s, u"CHAPTER"_s}, {u"entryId"_s, u"e12"_s}, {u"startPage"_s, 0}},
            QJsonObject{{u"id"_s, u"c106"_s}, {u"label"_s, u"Chap.106"_s}, {u"number"_s, 106},
                        {u"kind"_s, u"CHAPTER"_s}, {u"entryId"_s, u"e12"_s}, {u"startPage"_s, 150}},
        };
        m_progress = QJsonObject{{u"page"_s, 46}, {u"finished"_s, false}};
        opened();

        const QVariantMap volume = m_reader->volume();
        QCOMPARE(volume.value(u"title"_s).toString(), u"Le temps des examens"_s);
        QCOMPARE(volume.value(u"isbn"_s).toString(), u"978-2-505-06215-9"_s);
        QCOMPARE(volume.value(u"publishedOn"_s).toString(), u"2015-03-04"_s);
        QCOMPARE(volume.value(u"summary"_s).toString(), u"Les examens approchent."_s);
        QCOMPARE(volume.value(u"pageCount"_s).toInt(), 190);
        QCOMPARE(volume.value(u"chapters"_s).toString(), u"98 à 106"_s);
        QVERIFY(volume.value(u"cover"_s).toString().endsWith(u"/entries/e12/cover"_s));
        QVERIFY(volume.value(u"started"_s).toBool());
        QCOMPARE(volume.value(u"page"_s).toInt(), 47);
        QCOMPARE(volume.value(u"howFarRead"_s).toReal(), 46 / 190.0);
    }

    /// A field the volume does not have has no key, so the panel has no line for it — an
    /// empty value would be a label standing over nothing.
    void a_volume_without_an_isbn_has_no_isbn_in_what_the_panel_is_told()
    {
        opened();
        const QVariantMap volume = m_reader->volume();
        QVERIFY(!volume.contains(u"isbn"_s));
        QVERIFY(!volume.contains(u"publishedOn"_s));
        QVERIFY(!volume.contains(u"summary"_s));
        QVERIFY(!volume.contains(u"title"_s));
        QVERIFY(!volume.contains(u"chapters"_s));
    }

    /// A chapter on its own is not the book an ISBN names: what its file carries is the
    /// volume's, and the panel is told nothing to draw or copy.
    void a_chapter_tells_the_panel_no_isbn()
    {
        m_more = QJsonObject{{u"type"_s, u"CHAPTER"_s}, {u"isbn"_s, u"978-2-505-06533-3"_s}};
        opened();
        QVERIFY(!m_reader->volume().contains(u"isbn"_s));
    }

    /// The bar says what the shelf's row says about the same record: the page index over the
    /// pages, and full once the volume is finished — never a page ahead of the row.
    void the_panel_bar_counts_as_the_shelf_does()
    {
        m_progress = QJsonObject{{u"page"_s, 95}, {u"finished"_s, false}};
        opened();
        QCOMPARE(m_reader->volume().value(u"howFarRead"_s).toReal(), 95 / 190.0);
        QCOMPARE(m_reader->volume().value(u"page"_s).toInt(), 96);
    }

    void a_finished_volume_has_a_full_bar_until_it_is_turned()
    {
        m_progress = QJsonObject{{u"page"_s, 189}, {u"finished"_s, true}};
        opened();
        QVERIFY(m_reader->volume().value(u"finished"_s).toBool());
        QCOMPARE(m_reader->volume().value(u"howFarRead"_s).toReal(), 1.0);
        m_reader->forward();
        QVERIFY(!m_reader->volume().value(u"finished"_s).toBool());
        QVERIFY(m_reader->volume().value(u"howFarRead"_s).toReal() < 1.0);
    }

    /// Never opened is not « page 1 / 190 »: no record, no turn, no line. The first page
    /// turned makes it a volume that has been started.
    void a_volume_never_opened_is_not_started_until_a_page_is_turned()
    {
        opened();
        QVERIFY(!m_reader->volume().value(u"started"_s).toBool());
        m_reader->forward();
        QVERIFY(m_reader->volume().value(u"started"_s).toBool());
    }

    /// The rest of the series is asked for once, and only once it can matter.
    void the_way_on_is_asked_once_and_only_near_the_end()
    {
        opened();
        m_reader->goToPage(150);
        QTest::qWait(100);
        QCOMPARE(asked("GET /series/s1/entries "), 0);

        m_reader->goToPage(186);
        QTRY_COMPARE_WITH_TIMEOUT(asked("GET /series/s1/entries "), 1, 3000);
        m_reader->forward();
        m_reader->forward();
        m_reader->backward();
        m_reader->goToPage(188);
        QTest::qWait(200);
        QCOMPARE(asked("GET /series/s1/entries "), 1);

        const QVariantMap next = m_reader->next();
        QCOMPARE(next.value(u"entryId"_s).toString(), u"e13"_s);
        QCOMPARE(next.value(u"label"_s).toString(), u"Tome 13"_s);
        QCOMPARE(next.value(u"kind"_s).toString(), u"Volume"_s);
        QCOMPARE(next.value(u"action"_s).toString(), u"Lire le tome 13"_s);
        QVERIFY(next.value(u"cover"_s).toString().endsWith(u"/entries/e13/cover"_s));
    }

    /// Back from the first page is the volume before, at its last page — as forward from the
    /// last is the next one. Tried by hand on 22/10/2026, the back button on a first page did
    /// nothing.
    void going_back_from_the_first_page_opens_the_volume_before_at_its_last_page()
    {
        opened();
        QCOMPARE(m_reader->firstPage(), 0);
        m_reader->backward();
        QTRY_COMPARE_WITH_TIMEOUT(m_reader->entryId(), u"e11"_s, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!m_reader->loading(), 3000);
        QCOMPARE(m_reader->entryLabel(), u"Tome 11"_s);
        QCOMPARE(m_reader->pageCount(), 100);
        QCOMPARE(m_reader->firstPage(), 99);
        // Landing is not reading: nothing written for tome 11 before a page is turned.
        QCOMPARE(asked("PATCH /entries/e11/progress"), 0);
    }

    /// What was read of this volume is written before the one before replaces it, and it is
    /// the cover, below where reading was: `rewind`.
    void going_back_to_the_volume_before_writes_this_one_first()
    {
        m_progress = QJsonObject{{u"page"_s, 1}, {u"finished"_s, false}};
        opened();
        m_reader->backward();
        m_reader->backward();
        QTRY_COMPARE_WITH_TIMEOUT(m_reader->entryId(), u"e11"_s, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(writes().size(), 1, 3000);
        QCOMPARE(writes().first().value(u"page"_s).toInt(), 0);
        QCOMPARE(writes().first().value(u"rewind"_s).toBool(), true);
    }

    /// The first volume of a series has nothing before it, and going back stays put.
    void the_first_volume_has_nothing_before_it()
    {
        m_entries = {u"e12"_s, u"e13"_s};
        opened();
        m_reader->backward();
        QTRY_COMPARE_WITH_TIMEOUT(asked("GET /series/s1/entries "), 1, 3000);
        QTest::qWait(100);
        QCOMPARE(m_reader->entryId(), u"e12"_s);
        QCOMPARE(m_reader->firstPage(), 0);
        QVERIFY(!m_reader->loading());
        // Known now: a second turn back asks nothing more.
        m_reader->backward();
        QTest::qWait(100);
        QCOMPARE(asked("GET /series/s1/entries "), 1);
    }

    /// A turn forward while the list is still out cancels the turn back it was asked for.
    void a_turn_forward_while_asking_cancels_going_back()
    {
        m_pretend->holdBack = [](const QByteArray &request) {
            return request.startsWith("GET /series/s1/entries");
        };
        opened();
        m_reader->backward();
        QTRY_COMPARE_WITH_TIMEOUT(asked("GET /series/s1/entries "), 1, 3000);
        m_reader->forward();
        m_pretend->holdBack = nullptr;
        m_pretend->release();
        QObject asker;
        bool behind = false;
        m_server->get(u"/series/s1"_s, &asker,
                      [&behind](const Server::Answer &) { behind = true; });
        QTRY_VERIFY_WITH_TIMEOUT(behind, 3000);
        QCOMPARE(m_reader->entryId(), u"e12"_s);
        QCOMPARE(m_reader->firstPage(), 1);
    }

    /// The end card, then the next volume — and what was read of this one is written before
    /// the reader moves on, not lost with the timer.
    void past_the_last_page_the_next_volume_opens()
    {
        m_progress = QJsonObject{{u"page"_s, 188}, {u"finished"_s, false}};
        opened();
        m_reader->forward();
        QCOMPARE(m_reader->firstPage(), 189);
        QVERIFY(!m_reader->atTheEnd());

        m_reader->forward();
        QVERIFY(m_reader->atTheEnd());
        QCOMPARE(m_reader->firstPage(), 189);
        QTRY_COMPARE_WITH_TIMEOUT(m_reader->next().value(u"entryId"_s).toString(), u"e13"_s,
                                  3000);
        QCOMPARE(m_reader->ending(), u"Tome 12 terminé"_s);

        m_reader->forward();
        QTRY_COMPARE_WITH_TIMEOUT(m_reader->entryId(), u"e13"_s, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!m_reader->loading(), 3000);
        QVERIFY(!m_reader->atTheEnd());
        QCOMPARE(m_reader->entryLabel(), u"Tome 13"_s);
        QCOMPARE(asked("GET /entries/e13/pages"), 1);
        QTRY_COMPARE_WITH_TIMEOUT(writes().size(), 1, 400);
        QCOMPARE(writes().first().value(u"page"_s).toInt(), 189);
        QCOMPARE(writes().first().value(u"finished"_s).toBool(), true);
    }

    void the_last_volume_offers_nothing_after_it()
    {
        m_entries = {u"e11"_s, u"e12"_s};
        m_progress = QJsonObject{{u"page"_s, 189}, {u"finished"_s, false}};
        opened();
        QTRY_COMPARE_WITH_TIMEOUT(asked("GET /series/s1/entries "), 1, 3000);
        m_reader->forward();
        QVERIFY(m_reader->atTheEnd());
        QTest::qWait(100);
        QVERIFY(m_reader->next().isEmpty());
        QCOMPARE(m_reader->ending(),
                 u"Tome 12 terminé"_s + QChar(0x00A0) + u"· dernier tome de la série"_s);

        m_reader->forward();
        QTest::qWait(100);
        QCOMPARE(m_reader->entryId(), u"e12"_s);
        QVERIFY(m_reader->atTheEnd());
    }

    /// The two screens after this one and the one before it — in pairs, that is eight pages
    /// ahead of nothing, and the next volume's cover once the end is three pages away.
    void ahead_holds_two_displays_forward_and_one_back()
    {
        m_progress = QJsonObject{{u"page"_s, 5}, {u"finished"_s, false}};
        opened();
        QCOMPARE(m_reader->ahead(800), (QStringList{page(6), page(7), page(4)}));
        QVERIFY(page(6).endsWith(u"/entries/e12/pages/6?width=800"_s));

        inPairs();
        QCOMPARE(m_reader->firstPage(), 5);
        QCOMPARE(m_reader->ahead(800),
                 (QStringList{page(7), page(8), page(9), page(10), page(3), page(4)}));

        m_reader->goToPage(186);
        QTRY_VERIFY_WITH_TIMEOUT(!m_reader->next().isEmpty(), 3000);
        const QStringList near = m_reader->ahead(800);
        QCOMPARE(near.last(), m_reader->next().value(u"cover"_s).toString());
    }

    /// The screen asks for each image at the width the list of pages was asked at, and reads
    /// it here rather than working it out: halving the window is a decision about the layout,
    /// and two places deciding it would ask two different files of the server's cache.
    void the_screen_is_told_the_width_of_one_page()
    {
        opened();
        QCOMPARE(m_reader->pageWidth(), 1000);
        QSignalSpy told(m_reader, &Reader::changed);
        inPairs();
        QCOMPARE(m_reader->pageWidth(), 800);
        QVERIFY(!told.isEmpty());
        // A vertical book is read one page at a time whatever the preference says.
        m_declared = u"VERTICAL"_s;
        opened();
        QCOMPARE(m_reader->pageWidth(), 1600);
    }

    /// The width the images are asked at goes up by steps and never comes down while a volume
    /// is open. Asked at the exact width, every pixel of a window dragged wider, the panel
    /// full screen entered, was a new address for every page on screen: each one went
    /// blank and was fetched again for a copy a few pixels apart from the one already drawn.
    void the_width_asked_goes_up_by_steps_and_never_comes_down()
    {
        opened();
        QCOMPARE(m_reader->pageWidth(), 1000);
        QCOMPARE(m_reader->askedWidth(), 1000);

        m_reader->relayout(QSizeF(1010, 1600));
        QCOMPARE(m_reader->pageWidth(), 1010);
        QCOMPARE(m_reader->askedWidth(), 1200);
        m_reader->relayout(QSizeF(1150, 1600));
        QCOMPARE(m_reader->askedWidth(), 1200);
        // Narrower — a window made smaller — keeps the larger copy, scaled down.
        m_reader->relayout(QSizeF(575, 1600));
        QCOMPARE(m_reader->pageWidth(), 575);
        QCOMPARE(m_reader->askedWidth(), 1200);
        m_reader->relayout(QSizeF(600, 1600));
        QCOMPARE(m_reader->askedWidth(), 1200);
        m_reader->relayout(QSizeF(1300, 1600));
        QCOMPARE(m_reader->askedWidth(), 1400);
        QVERIFY(m_reader->pageUrl(3, m_reader->askedWidth()).endsWith(u"?width=1400"_s));

        // Another volume starts from the window it opens in, and asks its list at that width.
        m_reader->relayout(QSizeF(900, 1600));
        m_pretend->heard.clear();
        opened(u"e13"_s);
        QCOMPARE(m_reader->askedWidth(), 1000);
        QCOMPARE(asked("GET /entries/e13/pages?width=1000 "), 1);
    }

    /// An id is a word of the server's, and goes into a path percent-encoded: a `#` left as
    /// it is cut the path there and asked for another entry — or for the list of entries,
    /// with a fragment the server never saw — and a `/` would have been a step in the path.
    void an_id_goes_into_a_path_encoded()
    {
        const QString odd = u"t 12#b/c"_s;
        m_reader->open(u"s1"_s, odd);
        QTRY_VERIFY_WITH_TIMEOUT(!m_reader->loading(), 3000);
        QVERIFY2(m_reader->trouble().isEmpty(), qPrintable(m_reader->trouble()));

        const QByteArray encoded = "/entries/t%2012%23b%2Fc";
        QCOMPARE(asked("GET " + encoded + " "), 1);
        QCOMPARE(asked("GET " + encoded + "/pages?width=1000 "), 1);
        QCOMPARE(asked("GET " + encoded + "/chapters "), 1);
        QCOMPARE(asked("GET " + encoded + "/progress "), 1);
        QVERIFY(m_reader->pageUrl(3, 1000).endsWith(QString::fromUtf8(encoded)
                                                    + u"/pages/3?width=1000"_s));
        QVERIFY(m_reader->volume().value(u"cover"_s).toString().endsWith(
            QString::fromUtf8(encoded) + u"/cover"_s));

        m_reader->forward();
        QVERIFY(m_reader->close());
        QTRY_COMPARE_WITH_TIMEOUT(asked("PATCH " + encoded + "/progress "), 1, 3000);
    }

    /// A window turned on its side lays the pages out again, and the page being read stays
    /// on screen; so does a preference changed from the reader's own card.
    void turning_the_window_keeps_the_page_being_read()
    {
        m_preferences->choosePageLayout(Preferences::PageLayout::Auto);
        m_progress = QJsonObject{{u"page"_s, 46}, {u"finished"_s, false}};
        opened();
        QCOMPARE(m_reader->firstPage(), 46);
        QCOMPARE(m_reader->secondPage(), -1);

        m_reader->relayout(QSizeF(1600, 1000));
        QCOMPARE(m_reader->firstPage(), 45);
        QCOMPARE(m_reader->secondPage(), 46);
        QCOMPARE(m_reader->displayCount(), 96);

        m_preferences->choosePageLayout(Preferences::PageLayout::Single);
        QCOMPARE(m_reader->firstPage(), 45);
        QCOMPARE(m_reader->secondPage(), -1);
        QCOMPARE(m_reader->displayCount(), 190);
    }

    /// Answers about a volume left behind — its opening, and the rest of the series asked
    /// from near the end of another — are held back until a newer volume is open, then let
    /// through. None of them may land on it.
    void an_answer_for_a_volume_left_behind_is_dropped()
    {
        m_pretend->holdBack = [](const QByteArray &request) {
            return request.startsWith("GET /entries/e11") || request.startsWith("GET /series/s1/entries");
        };
        m_progress = QJsonObject{{u"page"_s, 188}, {u"finished"_s, false}};
        opened();
        QTRY_COMPARE_WITH_TIMEOUT(asked("GET /series/s1/entries "), 1, 3000);

        m_reader->open(u"s1"_s, u"e11"_s);
        QTRY_COMPARE_WITH_TIMEOUT(asked("GET /entries/e11/progress "), 1, 3000);
        m_progress = QJsonObject{{u"page"_s, 5}, {u"finished"_s, false}};
        opened();

        m_pretend->holdBack = nullptr;
        m_pretend->release();
        // A question of the test's own, asked after the old answers were written and waited
        // for: its answer comes back behind theirs, so by then they have landed or been
        // dropped. A fixed wait said the same on a quiet machine and nothing on a loaded one.
        // Handed to a receiver of its own, so a test that fails first is not written to after.
        QObject asker;
        bool behind = false;
        m_server->get(u"/series/s1"_s, &asker,
                      [&behind](const Server::Answer &) { behind = true; });
        QTRY_VERIFY_WITH_TIMEOUT(behind, 3000);

        QVERIFY(!m_reader->loading());
        QCOMPARE(m_reader->entryId(), u"e12"_s);
        QCOMPARE(m_reader->pageCount(), 190);
        QCOMPARE(m_reader->entryLabel(), u"Tome 12"_s);
        QCOMPARE(m_reader->firstPage(), 5);
        QVERIFY(m_reader->next().isEmpty());
    }

    /// A refused write is not a lost one: the bubble hears of it once, and the next write
    /// still carries the page reached since — and the `rewind` it was carrying, without
    /// which the server keeps the old place and ignores the whole re-read below it.
    void a_refused_write_is_said_and_owed_with_its_rewind()
    {
        m_progress = QJsonObject{{u"page"_s, 189}, {u"finished"_s, true}};
        opened();
        QSignalSpy refusals(m_reader, &Reader::refused);
        m_writesFail = true;
        m_reader->forward();
        QTRY_COMPARE_WITH_TIMEOUT(refusals.size(), 1, 3000);
        QVERIFY(!refusals.first().first().toString().isEmpty());
        QCOMPARE(m_reader->trouble(), QString());

        m_writesFail = false;
        m_reader->forward();
        QTRY_COMPARE_WITH_TIMEOUT(writes().size(), 2, 3000);
        QCOMPARE(writes().last().value(u"page"_s).toInt(), 2);
        QCOMPARE(writes().last().value(u"rewind"_s).toBool(), true);
        QTest::qWait(200);
        QCOMPARE(refusals.size(), 1);
    }

    /// A refused write left owed goes out when the reader leaves, without waiting for a
    /// page to be turned.
    void a_refused_write_still_leaves_with_the_reader()
    {
        opened();
        m_writesFail = true;
        m_reader->forward();
        QTRY_COMPARE_WITH_TIMEOUT(writes().size(), 1, 3000);
        QTest::qWait(100);
        m_writesFail = false;
        m_reader->close();
        QTRY_COMPARE_WITH_TIMEOUT(writes().size(), 2, 400);
        QCOMPARE(writes().last().value(u"page"_s).toInt(), 1);
    }

    /// A refused choice is said in a bubble, and the book stays open.
    void a_refused_choice_is_said_and_changes_nothing()
    {
        opened();
        QSignalSpy refusals(m_reader, &Reader::refused);
        m_pretend->answerFor = [this](const QByteArray &request) -> QByteArray {
            if (request.startsWith("PUT "))
                return answering(500, "{}");
            return route(request);
        };
        m_reader->chooseDirection(int(Reader::Direction::LeftToRight));
        QTRY_COMPARE_WITH_TIMEOUT(refusals.size(), 1, 3000);
        QCOMPARE(m_reader->trouble(), QString());
        QCOMPARE(m_reader->chosenDirection(), -1);
        QCOMPARE(m_reader->direction(), int(Reader::Direction::RightToLeft));
    }

    /// Closed while still opening, or with a choice still out: neither answer brings the
    /// closed reader back.
    void a_closed_reader_stays_closed()
    {
        m_reader->open(u"s1"_s, u"e12"_s);
        m_reader->close();
        QVERIFY(!m_reader->loading());
        QTest::qWait(300);
        QCOMPARE(m_reader->pageCount(), 0);
        QVERIFY(!m_reader->loading());

        opened();
        m_reader->chooseDirection(int(Reader::Direction::LeftToRight));
        m_reader->close();
        QTest::qWait(300);
        QCOMPARE(m_reader->chosenDirection(), -1);

        // And the timer is not running behind it.
        m_reader->forward();
        m_reader->close();
        QTest::qWait(1200);
        QCOMPARE(writes().size(), 1);
    }

    /// The rest of the series unanswered is not « nothing follows »: the next screen asks
    /// again, and the card then has its suite.
    void a_way_on_that_failed_is_asked_again()
    {
        m_wayOnFails = true;
        opened();
        m_reader->goToPage(186);
        QTRY_COMPARE_WITH_TIMEOUT(asked("GET /series/s1/entries "), 1, 3000);
        QTest::qWait(100);
        QVERIFY(m_reader->next().isEmpty());
        QCOMPARE(m_reader->ending(), u"Tome 12 terminé"_s);

        m_wayOnFails = false;
        m_reader->forward();
        QTRY_COMPARE_WITH_TIMEOUT(asked("GET /series/s1/entries "), 2, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(m_reader->next().value(u"entryId"_s).toString(), u"e13"_s,
                                  3000);
    }

    /// Four pages from the end is not yet near it.
    void four_pages_from_the_end_asks_nothing()
    {
        opened();
        m_reader->goToPage(185);
        QTest::qWait(300);
        QCOMPARE(asked("GET /series/s1/entries "), 0);
    }

    /// In pairs, the last screen can be a pair: it is the last page all the same.
    void a_pair_ending_on_the_last_page_says_finished()
    {
        m_pageCount = 191;
        inPairs();
        m_progress = QJsonObject{{u"page"_s, 187}, {u"finished"_s, false}};
        opened();
        QCOMPARE(m_reader->firstPage(), 187);
        m_reader->forward();
        QCOMPARE(m_reader->firstPage(), 189);
        QCOMPARE(m_reader->secondPage(), 190);
        QTRY_COMPARE_WITH_TIMEOUT(writes().size(), 1, 3000);
        QCOMPARE(writes().first().value(u"page"_s).toInt(), 190);
        QCOMPARE(writes().first().value(u"finished"_s).toBool(), true);
    }

    void a_server_that_says_no_says_why()
    {
        m_pretend->answerFor = nullptr;
        m_pretend->answers(500, "{}");
        m_reader->open(u"s1"_s, u"e12"_s);
        QTRY_VERIFY_WITH_TIMEOUT(!m_reader->loading(), 3000);
        QVERIFY(!m_reader->trouble().isEmpty());
        QCOMPARE(m_reader->pageCount(), 0);
    }

    void no_server_is_an_explanation_instead_of_a_crash()
    {
        Reader nowhere(nullptr, nullptr, nullptr);
        nowhere.open(u"s1"_s, u"e12"_s);
        QVERIFY(!nowhere.loading());
        QVERIFY(!nowhere.trouble().isEmpty());
        nowhere.forward();
        nowhere.close();
        QVERIFY(nowhere.ahead(800).isEmpty());
    }
};

QTEST_GUILESS_MAIN(HoldsTheReader)
#include "holds_the_reader.moc"
