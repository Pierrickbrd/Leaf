// The volumes of one edition: two answers married into one list, and the gaps that belong to
// neither of them.

#include "Entries.h"
#include "SeriesCaptions.h"
#include "Pretend.h"
#include "Server.h"
#include "Settings.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

QJsonObject volume(double number, const QString &title, int pages = 54)
{
    return {
        {u"id"_s, u"v%1"_s.arg(number)},
        {u"type"_s, u"VOLUME"_s},
        {u"number"_s, number},
        {u"title"_s, title},
        {u"pageCount"_s, pages},
        {u"chapterCount"_s, 0},
        {u"file"_s, u"Tome %1.cbz"_s.arg(number)},
        {u"size"_s, 48000000},
    };
}

/// A bound volume, which is where a chapter arc can fall inside a file: `sortKey` is the
/// number of the first chapter it holds.
QJsonObject bound(double number, double firstChapter)
{
    QJsonObject one = volume(number, u"Tome %1"_s.arg(number));
    one[u"sortKey"_s] = firstChapter;
    return one;
}

QJsonObject anArc(const QString &id, const QString &name, double from, double to,
                  const QString &unit = u"CHAPTER"_s, int position = 0)
{
    return {{u"id"_s, id},       {u"name"_s, name}, {u"unit"_s, unit},
            {u"from"_s, from},   {u"to"_s, to},     {u"position"_s, position}};
}

QJsonObject standing(const QString &entryId, int page, bool finished, int times = 0)
{
    QJsonObject where{{u"entryId"_s, entryId},
                      {u"page"_s, page},
                      {u"pageCount"_s, 54},
                      {u"finished"_s, finished}};
    if (times > 0)
        where.insert(u"timesFinished"_s, times);
    return where;
}

QByteArray rows(const QJsonArray &of)
{
    return QJsonDocument(of).toJson(QJsonDocument::Compact);
}

} // namespace

class HoldsTheVolumes : public QObject
{
    Q_OBJECT

    Pretend *m_pretend = nullptr;
    Settings *m_settings = nullptr;
    Server *m_server = nullptr;
    Entries *m_list = nullptr;

    void settle()
    {
        for (int i = 0; i < 200 && m_list->loading(); ++i)
            QTest::qWait(10);
        QTest::qWait(40);
    }

    void serve(const QByteArray &files, const QByteArray &states,
               const QByteArray &ranges = QByteArrayLiteral("[]"))
    {
        m_pretend->answerFor = [files, states, ranges](const QByteArray &request) {
            const QByteArray &chosen = request.contains("/progress ") ? states
                                     : request.contains("/arcs ")     ? ranges
                                                                      : files;
            return "HTTP/1.1 200 .\r\nContent-Type: application/json\r\nContent-Length: "
                   + QByteArray::number(chosen.size()) + "\r\n\r\n" + chosen;
        };
    }

    QVariant at(int row, const char *role) const
    {
        const QHash<int, QByteArray> named = m_list->roleNames();
        for (auto it = named.constBegin(); it != named.constEnd(); ++it) {
            if (it.value() == role)
                return m_list->data(m_list->index(row, 0), it.key());
        }
        return {};
    }

private slots:
    void init()
    {
        QStandardPaths::setTestModeEnabled(true);
        qunsetenv("LEAF_ADDRESS");
        qunsetenv("LEAF_KEY");
        m_pretend = new Pretend;
        QVERIFY(m_pretend->listen(QHostAddress::LocalHost));
        m_settings = new Settings;
        QSignalSpy loaded(m_settings, &Settings::changed);
        QVERIFY(loaded.wait(5000));
        m_settings->setAddress(
            QStringLiteral("http://127.0.0.1:%1").arg(m_pretend->serverPort()));
        m_settings->setKey(QStringLiteral("8f3a92c1d4e5b6a7"));
        m_server = new Server(m_settings);
        m_list = new Entries(m_server);
        serve(rows({volume(1, u"Le Crystal"_s), volume(2, u"L'Honneur"_s)}), rows({}));
    }

    void cleanup()
    {
        delete m_list;
        delete m_server;
        delete m_settings;
        delete m_pretend;
    }

    void a_new_list_holds_nothing_and_asks_for_nothing()
    {
        QCOMPARE(m_list->count(), 0);
        QVERIFY(!m_list->loading());
        QVERIFY(m_pretend->heard.isEmpty());
    }

    /// An entry carries no reading state at all, and the records are a second answer holding
    /// « one per entry that has been opened ». One request for thirty volumes, not thirty.
    void a_line_is_an_entry_and_the_record_beside_it()
    {
        serve(rows({volume(1, u"Le Crystal"_s), volume(2, u"L'Honneur"_s),
                    volume(3, u"L'Élu"_s)}),
              rows({standing(u"v1"_s, 53, true), standing(u"v2"_s, 12, false)}));
        m_list->point(u"albums"_s);
        settle();

        QVERIFY(m_pretend->heard.contains("GET /series/albums/entries "));
        QVERIFY(m_pretend->heard.contains("GET /series/albums/progress "));
        QCOMPARE(m_list->count(), 3);

        QCOMPARE(at(0, "number").toString(), u"1"_s);
        QCOMPARE(at(0, "title").toString(), u"Le Crystal"_s);
        QCOMPARE(at(0, "pages").toString(), u"54 p."_s);
        QCOMPARE(at(0, "state").value<Entries::State>(), Entries::State::Read);
        QCOMPARE(at(1, "state").value<Entries::State>(), Entries::State::InProgress);
        QVERIFY(at(1, "howFarRead").toDouble() > 0.2);
        // Never opened is the absence of a record, not a record saying nothing.
        QCOMPARE(at(2, "state").value<Entries::State>(), Entries::State::NeverRead);
        QCOMPARE(at(2, "howFarRead").toDouble(), 0.0);
    }

    /// `finished` says where the reader stands now. A volume opened again is in progress,
    /// whatever it has been before — and the count of endings says what it has been.
    void a_volume_being_read_again_is_in_progress_and_says_how_often_it_was_finished()
    {
        serve(rows({volume(1, u"Le Crystal"_s)}),
              rows({standing(u"v1"_s, 4, false, 2)}));
        m_list->point(u"albums"_s);
        settle();

        QCOMPARE(at(0, "state").value<Entries::State>(), Entries::State::InProgress);
        QCOMPARE(at(0, "timesFinished").toString(), u"×2"_s);
    }

    /// From two. A finished series would otherwise carry « ×1 » on every line for no news.
    void a_volume_finished_once_says_nothing_about_how_often()
    {
        serve(rows({volume(1, u"Le Crystal"_s)}), rows({standing(u"v1"_s, 53, true, 1)}));
        m_list->point(u"albums"_s);
        settle();

        QCOMPARE(at(0, "state").value<Entries::State>(), Entries::State::Read);
        QVERIFY(at(0, "timesFinished").toString().isEmpty());
    }

    /// A gap comes from neither answer: it is what `holding.missingVolumes` reports, and the
    /// page hands it over because it already knows it.
    void the_gaps_are_rows_and_sit_where_their_numbers_put_them()
    {
        serve(rows({volume(6, u"Alyana"_s), volume(8, u"Le Crépuscule"_s)}), rows({}));
        m_list->point(u"albums"_s, QVariantList{7.0});
        settle();

        QCOMPARE(m_list->count(), 3);
        QCOMPARE(at(1, "number").toString(), u"7"_s);
        QCOMPARE(at(1, "state").value<Entries::State>(), Entries::State::Missing);
        QCOMPARE(at(1, "title").toString(), u"Manquant"_s);
        // No file behind it, so nothing can be done to it: a delegate reading an empty
        // identifier draws no menu.
        QVERIFY(at(1, "entryId").toString().isEmpty());
        QVERIFY(at(1, "pages").toString().isEmpty());
    }

    /// A page of volumes with no ring on any line is still the page. A banner over it would
    /// say the series could not be read at all, which is not what happened.
    void a_refusal_of_the_reading_states_costs_the_marks_and_not_the_list()
    {
        m_pretend->answerFor = [](const QByteArray &request) -> QByteArray {
            if (request.contains("/progress "))
                return "HTTP/1.1 500 .\r\nContent-Length: 0\r\n\r\n";
            const QByteArray files = rows({volume(1, u"Le Crystal"_s)});
            return "HTTP/1.1 200 .\r\nContent-Type: application/json\r\nContent-Length: "
                   + QByteArray::number(files.size()) + "\r\n\r\n" + files;
        };
        m_list->point(u"albums"_s);
        settle();

        QCOMPARE(m_list->count(), 1);
        QVERIFY(m_list->trouble().isEmpty());
        QCOMPARE(at(0, "state").value<Entries::State>(), Entries::State::NeverRead);
    }

    void an_answer_that_is_not_a_list_is_refused_by_name()
    {
        serve(QByteArrayLiteral("{\"nope\":1}"), rows({}));
        m_list->point(u"albums"_s);
        settle();

        QCOMPARE(m_list->count(), 0);
        QCOMPARE(m_list->trouble(), u"entries: expected an array"_s);
    }

    /// The server answers « in reading order » and the client does not re-sort: sorting by
    /// file name is how « Tome 10 » lands before « Tome 2 ».
    void the_order_is_the_one_that_was_answered()
    {
        serve(rows({volume(2, u"Deux"_s), volume(10, u"Dix"_s), volume(3, u"Trois"_s)}),
              rows({}));
        m_list->point(u"albums"_s);
        settle();

        QCOMPARE(at(0, "number").toString(), u"2"_s);
        QCOMPARE(at(1, "number").toString(), u"10"_s);
        QCOMPARE(at(2, "number").toString(), u"3"_s);
    }

    void searching_narrows_what_is_shown_and_clearing_brings_it_back()
    {
        serve(rows({volume(1, u"Le Crystal des Elfes bleus"_s), volume(2, u"L'Honneur"_s),
                    volume(3, u"Cendres"_s)}),
              rows({}));
        m_list->point(u"albums"_s);
        settle();
        QCOMPARE(m_list->count(), 3);

        // Local: the whole list is already in hand, so nothing is asked of the server.
        const qsizetype asked = m_pretend->heard.size();
        m_list->searchFor(u"cendres"_s);
        QCOMPARE(m_list->count(), 1);
        QCOMPARE(at(0, "title").toString(), u"Cendres"_s);
        QCOMPARE(m_pretend->heard.size(), asked);

        // A number is written on the line too, so it is searchable on the line.
        m_list->searchFor(u"2"_s);
        QCOMPARE(m_list->count(), 1);
        QCOMPARE(at(0, "title").toString(), u"L'Honneur"_s);

        m_list->searchFor(QString());
        QCOMPARE(m_list->count(), 3);
        QVERIFY(!m_list->narrowedToNothing());
    }

    /// An emptied search is not a series with no files, and the screen has to be able to tell
    /// them apart to say why there is nothing.
    void a_search_that_empties_the_list_says_that_is_what_happened()
    {
        serve(rows({volume(1, u"Le Crystal"_s)}), rows({}));
        m_list->point(u"albums"_s);
        settle();

        m_list->searchFor(u"introuvable"_s);
        QCOMPARE(m_list->count(), 0);
        QVERIFY(m_list->narrowedToNothing());
    }

    /// The words of the screen follow the list without asking it anything: a caption is
    /// Which volume is being read, for a block that is not this list: a reading order may
    /// send the same work round twice, and « ici » has to land on the stretch holding it.
    void the_list_says_which_volume_is_open()
    {
        serve(rows({volume(1, u"Le Crépuscule des elfes"_s), volume(2, u"La Guerre"_s),
                    volume(3, u"La Dryade"_s)}),
              rows({standing(u"v1"_s, 54, true), standing(u"v2"_s, 12, false)}));
        m_list->point(u"elfes"_s);
        settle();
        QCOMPARE(m_list->reading(), 2.0);

        // A search narrows what is drawn and has no business moving a mark on another block.
        m_list->searchFor(u"Dryade"_s);
        QCOMPARE(m_list->count(), 1);
        QCOMPARE(m_list->reading(), 2.0);

        // Nothing open is NaN and not nought: nought is a volume, and some series have one.
        serve(rows({volume(1, u"Le Crépuscule des elfes"_s)}),
              rows({standing(u"v1"_s, 54, true)}));
        m_list->point(u"nains"_s);
        settle();
        QVERIFY(std::isnan(m_list->reading()));
    }

    /// refreshed by the change that refreshed what it quotes.
    void the_screen_says_why_a_list_is_empty_only_when_a_search_emptied_it()
    {
        SeriesCaptions words(m_list, nullptr);
        QSignalSpy moved(&words, &SeriesCaptions::changed);

        serve(rows({volume(1, u"Le Crystal"_s)}), rows({}));
        m_list->point(u"albums"_s);
        settle();
        QVERIFY(moved.size() >= 1);
        QVERIFY(!words.narrowedToNothing());
        QVERIFY(words.nothingFound().isEmpty());

        m_list->searchFor(u"introuvable"_s);
        QVERIFY(words.narrowedToNothing());
        QVERIFY(words.nothingFound().endsWith(u"."_s));

        // And the three tabs, which are the same words wherever they are read.
        QCOMPARE(words.volumesTab(), u"Tomes"_s);
        QCOMPARE(words.descriptionTab(), u"Description"_s);
        QCOMPARE(words.elsewhereTab(), u"Voir aussi"_s);
        QCOMPARE(words.neverRead(), u"Non lu"_s);
        QCOMPARE(words.volumesAxis(), u"les tomes"_s);
        QVERIFY(!words.inThisLibrary().isEmpty());

        // Bound to a page with no universe block at all — a headless run of the tests, or an
        // engine that failed to hand one over — the tab's own headings are still words, and
        // what would have counted a block that is not there says nothing.
        QVERIFY(!words.sameWorkHeading().isEmpty());
        QVERIFY(!words.universeHeading().isEmpty());
        QVERIFY(!words.outsideHeading().isEmpty());
        QVERIFY(words.universeLine().isEmpty());
        QVERIFY(words.outsideLine().isEmpty());
    }

    /// Every role, on a row that has them all — including the two a delegate reads only when
    /// it draws a menu, and the one a deletion needs.
    void a_line_answers_for_every_role_it_declares()
    {
        serve(rows({volume(5, u"La Dryade"_s)}), rows({standing(u"v5"_s, 53, true, 3)}));
        m_list->point(u"albums"_s);
        settle();

        QCOMPARE(at(0, "entryId").toString(), u"v5"_s);
        QCOMPARE(at(0, "weight").toLongLong(), qint64(48000000));
        QCOMPARE(at(0, "timesFinished").toString(), u"×3"_s);
        // Finished is the whole of it, whatever page the record happens to hold.
        QCOMPARE(at(0, "howFarRead").toDouble(), 1.0);
        QCOMPARE(m_list->pointedAt(), u"albums"_s);
    }

    /// A row that is not there answers nothing rather than reaching past the list — a view
    /// asks for one while it is being replaced, and an index is a promise nobody keeps.
    void a_row_that_is_not_there_answers_nothing()
    {
        serve(rows({volume(1, u"Le Crystal"_s)}), rows({}));
        m_list->point(u"albums"_s);
        settle();

        QVERIFY(!m_list->data(m_list->index(9, 0), Qt::UserRole).isValid());
        QVERIFY(!m_list->data(m_list->index(-1, 0), Qt::UserRole).isValid());
        // And a role it does not declare is not an answer either.
        QVERIFY(!m_list->data(m_list->index(0, 0), Qt::DisplayRole).isValid());
    }

    void a_list_pointed_at_nothing_empties_and_asks_nothing()
    {
        serve(rows({volume(1, u"Le Crystal"_s)}), rows({}));
        m_list->point(u"albums"_s);
        settle();
        QCOMPARE(m_list->count(), 1);

        const qsizetype asked = m_pretend->heard.size();
        m_list->point(QString());
        QCOMPARE(m_list->count(), 0);
        QVERIFY(!m_list->loading());
        QVERIFY(m_list->trouble().isEmpty());
        QCOMPARE(m_pretend->heard.size(), asked);
    }

    void a_list_with_no_server_says_so_instead_of_waiting()
    {
        Entries orphan(nullptr);
        orphan.point(u"albums"_s);

        QVERIFY(!orphan.loading());
        QVERIFY(!orphan.trouble().isEmpty());
        QCOMPARE(orphan.count(), 0);
    }

    void a_refusal_of_the_list_itself_is_reported()
    {
        m_pretend->answerFor = nullptr;
        m_pretend->answers(500, QByteArrayLiteral("[]"));
        m_list->point(u"albums"_s);
        settle();

        QCOMPARE(m_list->count(), 0);
        QVERIFY(!m_list->trouble().isEmpty());
    }

    /// One bad row spoils the list rather than being skipped: a page missing the volume it
    /// could not read is a page lying about what the edition holds.
    void a_row_that_is_not_an_object_is_refused_by_name()
    {
        serve(QByteArrayLiteral("[42]"), rows({}));
        m_list->point(u"albums"_s);
        settle();

        QCOMPARE(m_list->count(), 0);
        QCOMPARE(m_list->trouble(), u"entries: expected an object"_s);
    }

    void a_row_missing_what_it_must_carry_is_refused_by_name()
    {
        serve(QByteArrayLiteral("[{\"type\":\"VOLUME\"}]"), rows({}));
        m_list->point(u"albums"_s);
        settle();

        QCOMPARE(m_list->count(), 0);
        QVERIFY(m_list->trouble().startsWith(u"entry"_s));
    }

    /// A record this client cannot read costs that one mark and nothing else. The list is
    /// still the list, and the volumes beside it still say where the reader stands.
    void a_record_that_cannot_be_read_costs_its_own_mark_only()
    {
        serve(rows({volume(1, u"Le Crystal"_s), volume(2, u"L'Honneur"_s)}),
              QByteArrayLiteral("[7, {\"entryId\":\"v2\",\"page\":12,\"pageCount\":54,"
                                "\"finished\":false}]"));
        m_list->point(u"albums"_s);
        settle();

        QCOMPARE(m_list->count(), 2);
        QCOMPARE(at(0, "state").value<Entries::State>(), Entries::State::NeverRead);
        QCOMPARE(at(1, "state").value<Entries::State>(), Entries::State::InProgress);
    }

    /// Asking for what is already being looked for changes nothing — a field re-announcing
    /// its own text must not rebuild the list under the reader.
    void asking_again_for_the_same_words_rebuilds_nothing()
    {
        serve(rows({volume(1, u"Le Crystal"_s)}), rows({}));
        m_list->point(u"albums"_s);
        settle();

        QSignalSpy moved(m_list, &Entries::changed);
        m_list->searchFor(u"crystal"_s);
        const int announced = moved.size();
        m_list->searchFor(u"  crystal  "_s);
        QCOMPARE(moved.size(), announced);
    }

    // ——— Les arcs ————————————————————————————————————————————————————————————

    /// A screen knows before it asks: no arc declared is no request, which is the guard the
    /// contract puts on the count for exactly this.
    void a_series_with_no_arc_asks_for_none()
    {
        serve(rows({volume(1, u"Le Crystal"_s)}), rows({}));
        m_list->point(u"albums"_s, {}, 0);
        settle();

        QVERIFY(!m_pretend->heard.contains("/arcs "));
        QCOMPARE(m_list->count(), 1);
    }

    /// A range and not a label: a separator says where an arc begins and writes its own
    /// bounds beside it, because an arc does not end where the next one starts.
    void an_arc_puts_a_separator_where_it_begins_and_writes_its_range()
    {
        serve(rows({volume(1, u"Un"_s), volume(2, u"Deux"_s), volume(3, u"Trois"_s)}),
              rows({}),
              rows({anArc(u"a1"_s, u"Premier cycle"_s, 2, 3, u"VOLUME"_s)}));
        m_list->point(u"albums"_s, {}, 1);
        settle();

        QCOMPARE(m_list->count(), 4);
        QCOMPARE(at(1, "kind").value<Entries::Kind>(), Entries::Kind::Arc);
        QCOMPARE(at(1, "title").toString(), u"Premier cycle"_s);
        QCOMPARE(at(1, "detail").toString(), u"tomes 2 à 3"_s);
        // Not a file: nothing to open, nothing to command.
        QVERIFY(at(1, "entryId").toString().isEmpty());
        QVERIFY(!at(1, "state").isValid());
        QCOMPARE(at(2, "number").toString(), u"2"_s);
    }

    /// The case of a bound manga, where the unit of the arc and the unit of the files differ:
    /// the frontier is inside a line, so it is drawn inside the line.
    void an_arc_that_begins_inside_a_volume_cuts_it_into_two_stretches()
    {
        serve(rows({bound(7, 56), bound(8, 64), bound(9, 73)}), rows({}),
              rows({anArc(u"a1"_s, u"Arlong Park"_s, 69, 95)}));
        m_list->point(u"albums"_s, {}, 1);
        settle();

        // The volume keeps its place and its line; the marker sits between the two halves.
        QCOMPARE(m_list->count(), 6);
        QCOMPARE(at(1, "kind").value<Entries::Kind>(), Entries::Kind::File);
        QCOMPARE(at(1, "number").toString(), u"8"_s);
        QCOMPARE(at(2, "kind").value<Entries::Kind>(), Entries::Kind::Range);
        QCOMPARE(at(2, "detail").toString(), u"chapitres 64 à 68"_s);
        QCOMPARE(at(3, "kind").value<Entries::Kind>(), Entries::Kind::Arc);
        QCOMPARE(at(3, "detail").toString(), u"chapitres 69 à 95"_s);
        QCOMPARE(at(4, "kind").value<Entries::Kind>(), Entries::Kind::Range);
        QCOMPARE(at(4, "detail").toString(), u"chapitres 69 à 72"_s);
    }

    /// The last file of an edition has no neighbour to bound it, and the format writes its
    /// ranges open at either end.
    void a_frontier_inside_the_last_volume_leaves_its_stretch_open()
    {
        serve(rows({bound(7, 56), bound(8, 64)}), rows({}),
              rows({anArc(u"a1"_s, u"Arlong Park"_s, 69, 95)}));
        m_list->point(u"albums"_s, {}, 1);
        settle();

        QCOMPARE(at(4, "detail").toString(), u"à partir du chapitre 69"_s);
    }

    /// An arc that begins exactly where a file does needs no cutting: the frontier is a
    /// border between two lines, which a separator draws on its own.
    void an_arc_that_begins_on_a_volume_is_a_separator_and_nothing_more()
    {
        serve(rows({bound(7, 56), bound(8, 64)}), rows({}),
              rows({anArc(u"a1"_s, u"Baratié"_s, 64, 68)}));
        m_list->point(u"albums"_s, {}, 1);
        settle();

        QCOMPARE(m_list->count(), 3);
        QCOMPARE(at(1, "kind").value<Entries::Kind>(), Entries::Kind::Arc);
        QCOMPARE(at(2, "number").toString(), u"8"_s);
    }

    /// A saga holds its arcs, and one level is drawn: past that the eye loses the thread.
    /// A parent naming nothing indents nothing — the scan refuses such a link, and a screen
    /// that indented on it anyway would draw a hierarchy nobody wrote.
    void a_saga_indents_the_arcs_it_holds_and_nothing_else()
    {
        QJsonObject inside = anArc(u"a2"_s, u"Baratié"_s, 42, 68, u"CHAPTER"_s, 1);
        inside[u"parentId"_s] = u"a1"_s;
        QJsonObject orphan = anArc(u"a3"_s, u"Orphelin"_s, 70, 80, u"CHAPTER"_s, 2);
        orphan[u"parentId"_s] = u"nowhere"_s;

        serve(rows({bound(1, 1), bound(7, 42), bound(8, 70)}), rows({}),
              rows({anArc(u"a1"_s, u"East Blue"_s, 1, 100), inside, orphan}));
        m_list->point(u"albums"_s, {}, 3);
        settle();

        int saga = -1;
        int held = -1;
        int alone = -1;
        for (int row = 0; row < m_list->count(); ++row) {
            const QString name = at(row, "title").toString();
            if (name == u"East Blue"_s)
                saga = row;
            else if (name == u"Baratié"_s)
                held = row;
            else if (name == u"Orphelin"_s)
                alone = row;
        }
        QVERIFY(saga >= 0);
        QVERIFY(held >= 0);
        QVERIFY(alone >= 0);
        QCOMPARE(at(saga, "depth").toInt(), 0);
        QCOMPARE(at(held, "depth").toInt(), 1);
        QCOMPARE(at(alone, "depth").toInt(), 0);
    }

    /// A list of volumes with no arc drawn over it is still the list. A banner would say the
    /// series could not be read, which is not what happened.
    void a_refusal_of_the_arcs_costs_the_separators_and_not_the_list()
    {
        m_pretend->answerFor = [](const QByteArray &request) -> QByteArray {
            if (request.contains("/arcs "))
                return "HTTP/1.1 500 .\r\nContent-Length: 0\r\n\r\n";
            const QByteArray body = request.contains("/progress ")
                                        ? QByteArrayLiteral("[]")
                                        : rows({volume(1, u"Un"_s)});
            return "HTTP/1.1 200 .\r\nContent-Type: application/json\r\nContent-Length: "
                   + QByteArray::number(body.size()) + "\r\n\r\n" + body;
        };
        m_list->point(u"albums"_s, {}, 1);
        settle();

        QCOMPARE(m_list->count(), 1);
        QVERIFY(m_list->trouble().isEmpty());
    }

    /// One unreadable range costs itself and not the others, the way one unreadable record
    /// costs its own mark.
    void an_arc_that_cannot_be_read_costs_itself_only()
    {
        serve(rows({volume(1, u"Un"_s), volume(2, u"Deux"_s)}), rows({}),
              QByteArrayLiteral("[7, {\"id\":\"a1\",\"name\":\"Cycle\",\"unit\":\"VOLUME\","
                                "\"from\":2,\"to\":2,\"position\":0}]"));
        m_list->point(u"albums"_s, {}, 2);
        settle();

        QCOMPARE(m_list->count(), 3);
        QCOMPARE(at(1, "kind").value<Entries::Kind>(), Entries::Kind::Arc);
        QCOMPARE(at(1, "detail").toString(), u"tome 2"_s);
    }

    void the_list_keeps_what_it_holds_while_the_next_arrives()
    {
        serve(rows({volume(1, u"Le Crystal"_s)}), rows({}));
        m_list->point(u"albums"_s);
        settle();
        QCOMPARE(m_list->count(), 1);

        m_list->point(u"integrale"_s);
        QVERIFY(m_list->loading());
        QCOMPARE(m_list->count(), 1);
    }
};

QTEST_MAIN(HoldsTheVolumes)
#include "holds_the_volumes.moc"
