// One series, as its own page holds it — against a real HTTP boundary and no window.

#include "Pretend.h"
#include "Series.h"
#include "Server.h"
#include "Settings.h"
#include "Words.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

/// Elfes, chez Soleil: the only shape with all three levels — a universe, a work, and more
/// than one edition — which is why the page's tests are written against it.
QJsonObject elfes()
{
    return {
        {u"id"_s, u"albums"_s},
        {u"workId"_s, u"elfes"_s},
        {u"name"_s, u"Terres d'Arran · Elfes"_s},
        {u"universe"_s, u"Terres d'Arran"_s},
        {u"universeId"_s, u"u-arran"_s},
        {u"work"_s, u"Elfes"_s},
        {u"edition"_s, u"Albums"_s},
        {u"authors"_s, QJsonArray{u"Jean-Luc Istin"_s, u"Nicolas Jarry"_s}},
        {u"artists"_s, QJsonArray{u"Kyko Duarte"_s}},
        {u"publisher"_s, u"Soleil"_s},
        {u"collection"_s, u"Soleil Celtic"_s},
        {u"language"_s, u"fr"_s},
        {u"medium"_s, u"bd"_s},
        {u"readingDirection"_s, u"LEFT_TO_RIGHT"_s},
        {u"status"_s, u"ongoing"_s},
        {u"declaredVolumes"_s, 30},
        {u"ownedVolumes"_s, 29},
        {u"missingVolumes"_s, QJsonArray{7}},
        {u"readEntries"_s, 4},
        {u"partRead"_s, 0.22},
        {u"entryCount"_s, 29},
        {u"chapterCount"_s, 0},
        {u"arcCount"_s, 5},
        {u"genres"_s, QJsonArray{u"Fantasy"_s}},
        {u"tags"_s, QJsonArray{u"Elfes"_s}},
        {u"ageRating"_s, u"Tout public"_s},
        {u"colour"_s, true},
        {u"summary"_s, u"Cinq peuples elfiques."_s},
    };
}

/// The same work, bound differently. Ten volumes against twenty-nine is often the only thing
/// that really tells two editions apart.
QJsonObject integrale()
{
    return {
        {u"id"_s, u"integrale"_s},   {u"workId"_s, u"elfes"_s},
        {u"name"_s, u"Elfes"_s},     {u"work"_s, u"Elfes"_s},
        {u"edition"_s, u"Intégrale"_s}, {u"medium"_s, u"bd"_s},
        {u"ownedVolumes"_s, 10},     {u"entryCount"_s, 10},
        {u"chapterCount"_s, 0},      {u"arcCount"_s, 5},
    };
}

QByteArray body(const QJsonObject &of)
{
    return QJsonDocument(of).toJson(QJsonDocument::Compact);
}

QByteArray shelfOf(const QJsonArray &rows)
{
    return body({{u"items"_s, rows},
                 {u"total"_s, rows.size()},
                 {u"page"_s, 0},
                 {u"size"_s, rows.size()}});
}

QString valueOf(const QVariantList &rows, const QString &label)
{
    for (const QVariant &row : rows) {
        const QVariantMap one = row.toMap();
        if (one.value(u"label"_s).toString() == label)
            return one.value(u"value"_s).toString();
    }
    return {};
}

/// One of the four columns `facts` hands over, by its name. The page groups them in a map;
/// a test that reads one of them should not have to say `.toList()` twenty times.
QVariantList rowsOf(const Series *page, const QString &which)
{
    return page->description()->facts().value(which).toList();
}

/// A volume credited `authors` and `artists`, each its own or taken from the series.
Api::Entry aVolume(const QStringList &authors, const QStringList &artists,
                   bool authorsInherited = false, bool artistsInherited = false)
{
    Api::Entry one;
    one.credits.authors = authors;
    one.credits.artists = artists;
    one.authorsInherited = authorsInherited;
    one.artistsInherited = artistsInherited;
    return one;
}

/// A series called `work`, credited `credits` by its volumes, declaring `authors` and
/// `artists` as its default.
QJsonObject aSeries(const QString &work, const QStringList &authors, const QStringList &artists)
{
    QJsonObject one = elfes();
    one.insert(u"id"_s, u"one"_s);
    one.insert(u"name"_s, work);
    one.insert(u"work"_s, work);
    one.remove(u"universe"_s);
    one.remove(u"universeId"_s);
    one.insert(u"declaredAuthors"_s, QJsonArray::fromStringList(authors));
    one.insert(u"declaredArtists"_s, QJsonArray::fromStringList(artists));
    // Credited with its defaults, as the server's union of volumes that inherit them is —
    // and not with Elfes' writers, who are nobody's in these series.
    one.insert(u"authors"_s, QJsonArray::fromStringList(authors));
    one.insert(u"artists"_s, QJsonArray::fromStringList(artists));
    return one;
}

} // namespace

class HoldsTheSeries : public QObject
{
    Q_OBJECT

    Pretend *m_pretend = nullptr;
    Settings *m_settings = nullptr;
    Server *m_server = nullptr;
    Series *m_page = nullptr;

    void settle()
    {
        for (int i = 0; i < 200 && m_page->loading(); ++i)
            QTest::qWait(10);
        // The siblings are asked for after the series answers, so the page stops loading
        // before they arrive. A few more turns, or the switcher is read too early.
        QTest::qWait(60);
    }

    /// One body for the series, another for the shelf its siblings come from.
    void serve(const QByteArray &one, const QByteArray &others)
    {
        m_pretend->answerFor = [one, others](const QByteArray &request) {
            const bool asksForSiblings = request.startsWith("GET /series?");
            const QByteArray &chosen = asksForSiblings ? others : one;
            return "HTTP/1.1 200 .\r\nContent-Type: application/json\r\nContent-Length: "
                   + QByteArray::number(chosen.size()) + "\r\n\r\n" + chosen;
        };
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
        m_page = new Series(m_server);
        serve(body(elfes()), shelfOf({elfes(), integrale()}));
    }

    void cleanup()
    {
        delete m_page;
        delete m_server;
        delete m_settings;
        delete m_pretend;
    }

    void a_new_page_holds_nothing_and_asks_for_nothing()
    {
        QVERIFY(!m_page->available());
        QVERIFY(!m_page->loading());
        QVERIFY(m_pretend->heard.isEmpty());
    }

    /// A webtoon holds chapters and no volume: it is counted in chapters. « 0 tome » over a
    /// hundred and twenty chapters said the series was empty (test of 24/11/2026).
    void a_series_of_chapters_is_counted_in_chapters()
    {
        const QJsonObject webtoon{
            {u"id"_s, u"cohabitation"_s}, {u"workId"_s, u"cohabitation"_s},
            {u"name"_s, u"Cohabitation"_s}, {u"work"_s, u"Cohabitation"_s},
            {u"medium"_s, u"webtoon"_s},  {u"readingDirection"_s, u"VERTICAL"_s},
            {u"edition"_s, u"Cohabitation"_s}, {u"ownedVolumes"_s, 0},
            {u"entryCount"_s, 120},       {u"chapterCount"_s, 120},
            {u"arcCount"_s, 0},           {u"ageRating"_s, u"18+"_s},
        };
        serve(body(webtoon), shelfOf({}));
        m_page->point(u"cohabitation"_s);
        settle();
        QCOMPARE(m_page->description()->weights(), u"120 chapitres · 18+ · Verticale"_s);
    }

    void the_three_levels_are_kept_apart_and_worded()
    {
        m_page->point(u"albums"_s);
        settle();

        QVERIFY(m_pretend->heard.contains("GET /series/albums "));
        QVERIFY(m_page->available());
        QVERIFY(m_page->trouble().isEmpty());
        QCOMPARE(m_page->universe(), u"Terres d'Arran"_s);
        QCOMPARE(m_page->universeId(), u"u-arran"_s);
        QCOMPARE(m_page->work(), u"Elfes"_s);
        QCOMPARE(m_page->edition(), u"Albums"_s);
        // Who made it — who wrote and who drew — then what it weighs, and the count follows
        // the medium, so a BD is counted in albums.
        QCOMPARE(m_page->description()->makers(),
                 u"Jean-Luc Istin, Nicolas Jarry, Kyko Duarte · Soleil · BD · En parution"_s);
        QCOMPARE(m_page->description()->weights(),
                 u"29 albums · 5 arcs · Tout public · Gauche à droite"_s);
        QVERIFY(m_page->cover().endsWith(u"/series/albums/cover"_s));
    }

    /// « Rien ne se vide pour se remplir » — the rule the whole page is arranged around.
    void the_page_keeps_what_it_holds_while_the_next_arrives()
    {
        m_page->point(u"albums"_s);
        settle();
        QCOMPARE(m_page->edition(), u"Albums"_s);

        m_page->point(u"integrale"_s);
        // Asked, not yet answered: the screen is still showing what it had.
        QVERIFY(m_page->loading());
        QVERIFY(m_page->available());
        QCOMPARE(m_page->edition(), u"Albums"_s);

        serve(body(integrale()), shelfOf({elfes(), integrale()}));
        settle();
        QCOMPARE(m_page->edition(), u"Intégrale"_s);
    }

    /// A volume marked read changes the header's counts, handed over by `Library` from the
    /// one answer it asked for: the page asks nothing itself. It asked for itself and then
    /// for its siblings, two questions for « 4 lus » becoming « 5 lus ».
    void a_series_taken_changes_the_page_without_a_question()
    {
        m_page->point(u"albums"_s);
        settle();
        m_pretend->heard.clear();
        QSignalSpy changed(m_page, &Series::changed);

        QJsonObject later = elfes();
        later[u"edition"_s] = u"Albums cartonnés"_s;
        m_page->take(*Api::series(later).value);
        QCOMPARE(m_page->edition(), u"Albums cartonnés"_s);
        QCOMPARE(changed.count(), 1);

        // Another series, not on this page nor among its editions, changes nothing.
        QJsonObject stranger = elfes();
        stranger[u"id"_s] = u"ailleurs"_s;
        m_page->take(*Api::series(stranger).value);
        QCOMPARE(changed.count(), 1);
        QVERIFY(m_pretend->heard.isEmpty());
    }

    void a_series_with_nothing_above_it_says_nothing_above_it()
    {
        QJsonObject alone = integrale();
        alone.remove(u"edition"_s);
        serve(body(alone), shelfOf({alone}));

        m_page->point(u"integrale"_s);
        settle();

        QVERIFY(m_page->universe().isEmpty());
        QVERIFY(m_page->edition().isEmpty());
        QCOMPARE(m_page->work(), u"Elfes"_s);
    }

    /// One person who writes and draws is one author on the page: « Scénario » and « Dessin »
    /// with the same name read as two people.
    void the_description_says_author_once_for_one_person()
    {
        QJsonObject titan = elfes();
        titan[u"authors"_s] = QJsonArray{u"Hajime Isayama"_s};
        titan[u"artists"_s] = QJsonArray{u"Hajime Isayama"_s};
        serve(body(titan), shelfOf({titan}));

        m_page->point(u"albums"_s);
        settle();

        QCOMPARE(valueOf(rowsOf(m_page, u"credits"_s), u"Auteur"_s), u"Hajime Isayama"_s);
        QVERIFY(valueOf(rowsOf(m_page, u"credits"_s), u"Scénario"_s).isEmpty());
        QVERIFY(valueOf(rowsOf(m_page, u"credits"_s), u"Dessin"_s).isEmpty());
    }

    void the_description_leaves_out_what_is_not_recorded()
    {
        m_page->point(u"albums"_s);
        settle();

        QCOMPARE(valueOf(rowsOf(m_page, u"credits"_s), u"Scénario"_s),
                 u"Jean-Luc Istin, Nicolas Jarry"_s);
        QCOMPARE(valueOf(rowsOf(m_page, u"credits"_s), u"Éditeur"_s), u"Soleil"_s);
        QCOMPARE(valueOf(rowsOf(m_page, u"credits"_s), u"Collection"_s), u"Soleil Celtic"_s);
        // What the publisher announces sits beside the state of the run: « en cours » alone
        // leaves « of how many? » unanswered.
        QCOMPARE(valueOf(rowsOf(m_page, u"nature"_s), u"Statut"_s), u"En parution\u00A0· 30 albums annoncés"_s);
        QCOMPARE(valueOf(rowsOf(m_page, u"nature"_s), u"Couleur"_s), u"Couleur"_s);

        QJsonObject bare = elfes();
        for (const QString &gone : {u"collection"_s, u"colour"_s, u"ageRating"_s})
            bare.remove(gone);
        serve(body(bare), shelfOf({bare}));
        m_page->point(u"albums"_s);
        settle();

        // Not drawn, rather than drawn empty: « Collection : — » says a fact is missing and
        // takes a line to do it.
        QVERIFY(valueOf(rowsOf(m_page, u"credits"_s), u"Collection"_s).isEmpty());
        QVERIFY(valueOf(rowsOf(m_page, u"nature"_s), u"Couleur"_s).isEmpty());
        QVERIFY(valueOf(rowsOf(m_page, u"nature"_s), u"Âge"_s).isEmpty());
    }

    void the_holding_block_is_the_only_place_a_missing_volume_is_named()
    {
        m_page->point(u"albums"_s);
        settle();

        QCOMPARE(valueOf(rowsOf(m_page, u"holding"_s), u"Tomes détenus"_s), u"29 sur 30"_s);
        QCOMPARE(valueOf(rowsOf(m_page, u"holding"_s), u"Manquant"_s), u"Tome 7"_s);
        QCOMPARE(valueOf(rowsOf(m_page, u"holding"_s), u"Lus"_s), u"4 · le 5ᵉ en cours"_s);
        // And it is the one line painted in the alert colour.
        bool marked = false;
        for (const QVariant &row : rowsOf(m_page, u"holding"_s)) {
            if (row.toMap().value(u"label"_s).toString() == u"Manquant"_s)
                marked = row.toMap().value(u"alarming"_s).toBool();
        }
        QVERIFY(marked);
    }

    void several_missing_volumes_are_listed_with_commas_to_the_end()
    {
        QJsonObject holed = elfes();
        holed[u"missingVolumes"_s] = QJsonArray{7, 9, 12, 18};
        serve(body(holed), shelfOf({holed}));

        m_page->point(u"albums"_s);
        settle();

        // The label agrees and the value enumerates; no « et », because this is a list of
        // identifiers and not a sentence.
        QCOMPARE(valueOf(rowsOf(m_page, u"holding"_s), u"Manquants"_s), u"Tomes 7, 9, 12, 18"_s);
    }

    void a_complete_collection_says_nothing_about_missing_volumes()
    {
        QJsonObject whole = elfes();
        whole.remove(u"missingVolumes"_s);
        serve(body(whole), shelfOf({whole}));

        m_page->point(u"albums"_s);
        settle();

        for (const QVariant &row : rowsOf(m_page, u"holding"_s))
            QVERIFY(!row.toMap().value(u"label"_s).toString().startsWith(u"Manquant"_s));
    }

    void the_switcher_is_not_offered_below_two_editions()
    {
        serve(body(elfes()), shelfOf({elfes()}));
        m_page->point(u"albums"_s);
        settle();

        QVERIFY(m_page->editions().isEmpty());
        // Nothing to choose between is nothing to say.
        QVERIFY(m_page->editionsLabel().isEmpty());
    }

    void the_switcher_names_both_and_marks_the_one_being_read()
    {
        m_page->point(u"albums"_s);
        settle();

        const QVariantList offered = m_page->editions();
        QCOMPARE(offered.size(), 2);
        QCOMPARE(m_page->editionsLabel(), u"2 éditions"_s);
        QCOMPARE(offered.at(0).toMap().value(u"name"_s).toString(), u"Albums"_s);
        // The count is often the only thing that really tells two editions apart, so it is
        // on both — including the one being read.
        QCOMPARE(offered.at(0).toMap().value(u"detail"_s).toString(), u"29 albums"_s);
        QCOMPARE(offered.at(1).toMap().value(u"detail"_s).toString(), u"10 albums"_s);
        // The same keys the universe block draws its tiles from, cover included: one shape,
        // one delegate, and a series drawn the same way wherever it appears.
        QVERIFY(offered.at(1).toMap().value(u"cover"_s).toString().endsWith(
            u"/series/integrale/cover"_s));
        QVERIFY(offered.at(0).toMap().value(u"here"_s).toBool());
        QVERIFY(!offered.at(1).toMap().value(u"here"_s).toBool());
    }

    /// The page is perfectly readable without its switcher, so a refusal there must not put a
    /// banner over it. A work with one edition looks exactly like this.
    /// Keeping is for a page being replaced under the eye, not for a page one has left.
    /// Held on to, the next series opened on the last one's cover and title for as long as
    /// its answer took — and a shelf seen in between did not make that any less wrong.
    void leaving_the_page_lets_go_of_what_it_was_showing()
    {
        m_page->point(u"albums"_s);
        settle();
        QVERIFY(m_page->available());

        m_page->forget();
        QVERIFY(!m_page->available());
        QVERIFY(m_page->identifier().isEmpty());
        QVERIFY(m_page->editions().isEmpty());
        QVERIFY(!m_page->loading());
        QVERIFY(m_page->trouble().isEmpty());

        // And an answer still on the wire for what was let go does not land on the page
        // afterwards: it is the generation that says so, not the timing.
        m_page->point(u"albums"_s);
        m_page->forget();
        QTest::qWait(250);
        QVERIFY(!m_page->available());
    }

    void a_refusal_of_the_other_editions_costs_the_switcher_and_nothing_else()
    {
        serve(body(elfes()), QByteArrayLiteral("{\"nope\":1}"));
        m_page->point(u"albums"_s);
        settle();

        QVERIFY(m_page->available());
        QVERIFY(m_page->trouble().isEmpty());
        QVERIFY(m_page->editions().isEmpty());
        QCOMPARE(m_page->work(), u"Elfes"_s);
    }

    void an_answer_that_is_not_a_series_is_refused_by_name()
    {
        serve(QByteArrayLiteral("{\"id\":1}"), shelfOf({}));
        m_page->point(u"albums"_s);
        settle();

        QVERIFY(!m_page->available());
        QVERIFY(m_page->trouble().contains(u"series"_s));
    }

    /// Genres and tags side by side, in that order and never folded together: measured, a
    /// work carries one genre and seven tags with nothing in common between the two lists.
    void the_genres_and_the_tags_are_one_row_and_keep_their_order()
    {
        m_page->point(u"albums"_s);
        settle();
        QCOMPARE(m_page->description()->genres(), QStringList({u"Fantasy"_s, u"Elfes"_s}));
        QCOMPARE(m_page->description()->summary(), u"Cinq peuples elfiques."_s);
        QCOMPARE(m_page->missingVolumes(), QVariantList({7.0}));
    }

    /// The former singular, kept for a file written before `authors` existed. A page that
    /// read only the plural would credit nobody on every work imported before it.
    void a_work_credited_in_the_old_singular_is_still_credited()
    {
        QJsonObject old = elfes();
        old.remove(u"authors"_s);
        old[u"author"_s] = u"Manu Larcenet"_s;
        serve(body(old), shelfOf({old}));

        m_page->point(u"albums"_s);
        settle();

        QVERIFY(m_page->description()->makers().startsWith(u"Manu Larcenet"_s));
        // And the description credits the same name: the two read the writers by different
        // routes, and only one of them was ever taught the old singular.
        QCOMPARE(valueOf(rowsOf(m_page, u"credits"_s), u"Scénario"_s), u"Manu Larcenet"_s);
    }

    void the_dates_are_said_when_the_library_recorded_them()
    {
        QJsonObject stamped = elfes();
        stamped[u"addedAt"_s] = QJsonValue(qint64(1700000000000));
        stamped[u"lastAddedAt"_s] = QJsonValue(qint64(1758500000000));
        serve(body(stamped), shelfOf({stamped}));

        m_page->point(u"albums"_s);
        settle();

        // Beside what is held and not among it: the card draws the two in two columns, so
        // « quand » is a list of its own — a date is not a count of volumes.
        QVERIFY(!valueOf(rowsOf(m_page, u"received"_s), u"Premier reçu"_s).isEmpty());
        QVERIFY(!valueOf(rowsOf(m_page, u"received"_s), u"Dernier reçu"_s).isEmpty());
        QVERIFY(valueOf(rowsOf(m_page, u"holding"_s), u"Premier reçu"_s).isEmpty());
    }

    /// Pointed at nothing is not an error: it is what a page holds before anything is opened,
    /// and it has to answer for every one of its properties without a series behind it.
    void a_page_pointed_at_nothing_answers_for_everything_and_holds_none_of_it()
    {
        m_page->point(u"albums"_s);
        settle();
        QVERIFY(m_page->available());

        m_page->point(QString());
        QVERIFY(!m_page->available());
        QVERIFY(!m_page->loading());
        QVERIFY(m_page->universe().isEmpty());
        QVERIFY(m_page->universeId().isEmpty());
        QVERIFY(m_page->work().isEmpty());
        QVERIFY(m_page->edition().isEmpty());
        QVERIFY(m_page->cover().isEmpty());
        QVERIFY(m_page->description()->makers().isEmpty());
        QVERIFY(m_page->description()->weights().isEmpty());
        QVERIFY(m_page->description()->genres().isEmpty());
        QVERIFY(m_page->description()->summary().isEmpty());
        QVERIFY(rowsOf(m_page, u"credits"_s).isEmpty());
        QVERIFY(rowsOf(m_page, u"nature"_s).isEmpty());
        QVERIFY(rowsOf(m_page, u"holding"_s).isEmpty());
        QVERIFY(m_page->missingVolumes().isEmpty());
        QVERIFY(m_page->oneShotEntry().isEmpty());
    }

    /// No server is not a fault of the page, and it says so rather than staying blank for
    /// ever — the same sentence the shelf shows when nothing is set up.
    void a_page_with_no_server_says_so_instead_of_waiting()
    {
        Series orphan(nullptr);
        orphan.point(u"albums"_s);

        QVERIFY(!orphan.loading());
        QVERIFY(!orphan.trouble().isEmpty());
        QVERIFY(!orphan.available());
    }

    void a_refusal_is_reported_and_leaves_the_page_empty()
    {
        m_pretend->answerFor = nullptr;
        m_pretend->answers(500, QByteArrayLiteral("{}"));
        m_page->point(u"albums"_s);
        settle();

        QVERIFY(!m_page->available());
        QVERIFY(!m_page->trouble().isEmpty());
    }

    void an_answer_that_is_not_an_object_is_refused_by_name()
    {
        serve(QByteArrayLiteral("[]"), shelfOf({}));
        m_page->point(u"albums"_s);
        settle();

        QVERIFY(!m_page->available());
        QCOMPARE(m_page->trouble(), u"series: expected an object"_s);
    }

    void a_book_that_is_a_whole_book_says_which_file_to_open()
    {
        QJsonObject alone = integrale();
        alone[u"oneShotEntry"_s] = u"le-combat-ordinaire"_s;
        serve(body(alone), shelfOf({alone}));

        m_page->point(u"integrale"_s);
        settle();

        QCOMPARE(m_page->oneShotEntry(), u"le-combat-ordinaire"_s);
    }

    /// The series' people are those of all its editions, and the volumes shown only count
    /// them: the list of volumes is one edition's, and a writer credited in the other
    /// vanished from the header and the description as soon as they had answered.
    void the_people_are_the_same_whichever_edition_is_shown()
    {
        QJsonObject series = aSeries(u"Lucky Luke"_s, {u"Morris"_s}, {u"Morris"_s});
        series.insert(u"authors"_s, QJsonArray{u"Morris"_s, u"Jul"_s});
        series.insert(u"artists"_s, QJsonArray{u"Morris"_s, u"Achdé"_s});
        serve(body(series), shelfOf({}));
        m_page->point(u"one"_s);
        settle();
        m_page->description()->takeVolumes(u"one"_s,
                                           {aVolume({u"Morris"_s}, {u"Morris"_s}, true, true)});

        QStringList names;
        for (const Description::Person &one : m_page->description()->peopleOrdered())
            names << one.name;
        QCOMPARE(names, (QStringList{u"Morris"_s, u"Jul"_s, u"Achdé"_s}));
        QCOMPARE(m_page->description()->peopleOrdered().constFirst().volumes, 1);
        QVERIFY(m_page->description()->makers().startsWith(u"Morris, Jul, Achdé"_s));
        const QVariantList credits = rowsOf(m_page, u"credits"_s);
        QCOMPARE(valueOf(credits, u"Scénario"_s), u"Jul"_s);
        QCOMPARE(valueOf(credits, u"Dessin"_s), u"Achdé"_s);
    }

    /// The people of a series, read off its volumes: the series' default first, then whoever
    /// has the most volumes, then whoever came first. The series listed them in the order its
    /// volumes were read, so a writer of one late volume could stand before the one of sixty.
    void people_put_the_series_default_first_then_the_most_volumes()
    {
        const QStringList pair{u"Morris"_s, u"René Goscinny"_s};
        serve(body(aSeries(u"Lucky Luke"_s, pair, {u"Morris"_s})), shelfOf({}));
        m_page->point(u"one"_s);
        settle();
        QList<Api::Entry> volumes;
        volumes << aVolume({u"Bob de Groot"_s}, {u"Morris"_s}, false, true);
        for (int at = 0; at < 6; ++at)
            volumes << aVolume(pair, {u"Morris"_s}, true, true);
        for (int at = 0; at < 3; ++at)
            volumes << aVolume({u"Jul"_s}, {u"Achdé"_s});
        volumes << aVolume({u"Bob de Groot"_s}, {u"Morris"_s}, false, true);
        volumes << aVolume({u"Laurent Gerra"_s}, {u"Achdé"_s});
        m_page->description()->takeVolumes(u"one"_s, volumes);

        QStringList names;
        for (const Description::Person &one : m_page->description()->peopleOrdered())
            names << one.name;
        QCOMPARE(names, (QStringList{u"Morris"_s, u"René Goscinny"_s, u"Achdé"_s, u"Jul"_s,
                                     u"Bob de Groot"_s, u"Laurent Gerra"_s}));
        QCOMPARE(m_page->description()->peopleOrdered().constFirst().volumes, 8);
        QVERIFY(m_page->description()->makers().startsWith(u"Morris, René Goscinny et 4 autres"_s));
        const QVariantList credits = rowsOf(m_page, u"credits"_s);
        QCOMPARE(valueOf(credits, u"Auteur"_s), u"Morris"_s);
        QCOMPARE(valueOf(credits, u"Scénario"_s), u"René Goscinny, Jul, Bob de Groot, Laurent Gerra"_s);
        QCOMPARE(valueOf(credits, u"Dessin"_s), u"Achdé"_s);

        const QVariantList people = m_page->description()->people();
        QCOMPARE(people.size(), 6);
        QCOMPARE(people.at(0).toMap().value(u"section"_s).toString(), u"Auteur"_s);
        QCOMPARE(people.at(0).toMap().value(u"count"_s).toString(), u"8 albums"_s);
        QCOMPARE(people.at(1).toMap().value(u"name"_s).toString(), u"René Goscinny"_s);
        QCOMPARE(people.at(1).toMap().value(u"section"_s).toString(), u"Scénario"_s);
        QCOMPARE(people.at(5).toMap().value(u"name"_s).toString(), u"Achdé"_s);
        QCOMPARE(people.at(5).toMap().value(u"section"_s).toString(), u"Dessin"_s);
        QCOMPARE(m_page->description()->peopleTitle(), u"Les auteurs de Lucky Luke"_s);
        QCOMPARE(m_page->description()->peopleCount(), Words::peopleCount(6, Api::Medium::Bd));
    }

    /// A volume credited with what its series declares says nothing under its title: sixty
    /// grey lines repeating « Morris, René Goscinny » hide the three where the team changed.
    void a_volume_that_inherits_draws_no_credit_line()
    {
        QVERIFY(Words::volumeCredits(aVolume({u"Morris"_s}, {u"Morris"_s}, true, true)).isEmpty());
    }

    /// One that says its own names draws them, both roles, even the one it inherits: the line
    /// says who made this volume, not which half of it differs.
    void a_volume_that_differs_draws_its_own()
    {
        QCOMPARE(Words::volumeCredits(aVolume({u"Jul"_s}, {u"Achdé"_s})),
                 u"Scénario\u00A0: Jul\u00A0· Dessin\u00A0: Achdé"_s);
        QCOMPARE(Words::volumeCredits(aVolume({u"Jul"_s}, {u"Achdé"_s}, false, true)),
                 u"Scénario\u00A0: Jul\u00A0· Dessin\u00A0: Achdé"_s);
    }

    /// 5b, first column: one person writes and draws every volume. One « Auteur » line, and
    /// no volume says a word under its title.
    void the_three_cases_of_5b_one_person_makes_it_all()
    {
        const QStringList isayama{u"Hajime Isayama"_s};
        serve(body(aSeries(u"L’Attaque des Titans"_s, isayama, isayama)), shelfOf({}));
        m_page->point(u"one"_s);
        settle();
        m_page->description()->takeVolumes(u"one"_s, {aVolume(isayama, isayama, true, true),
                                      aVolume(isayama, isayama, true, true)});
        const QVariantList credits = rowsOf(m_page, u"credits"_s);
        QCOMPARE(valueOf(credits, u"Auteur"_s), u"Hajime Isayama"_s);
        QVERIFY(valueOf(credits, u"Scénario"_s).isEmpty());
        QVERIFY(m_page->description()->makers().startsWith(u"Hajime Isayama ·"_s));
    }

    /// 5b, second column: a team, then takeovers. The volumes that inherit say nothing, the
    /// takeovers say theirs, and the header names the default first.
    void the_three_cases_of_5b_a_team_then_takeovers()
    {
        const QStringList pair{u"Morris"_s, u"René Goscinny"_s};
        serve(body(aSeries(u"Lucky Luke"_s, pair, {u"Morris"_s})), shelfOf({}));
        m_page->point(u"one"_s);
        settle();
        const Api::Entry nine = aVolume(pair, {u"Morris"_s}, true, true);
        const Api::Entry seventyTwo = aVolume({u"Jul"_s}, {u"Achdé"_s});
        m_page->description()->takeVolumes(u"one"_s, {seventyTwo, nine, seventyTwo});
        QVERIFY(Words::volumeCredits(nine).isEmpty());
        QVERIFY(!Words::volumeCredits(seventyTwo).isEmpty());
        QVERIFY(m_page->description()->makers().startsWith(u"Morris, René Goscinny et 2 autres"_s));
    }

    /// 5b, third column: no default, a duo per volume. Every role lists its people by the
    /// order they came, three of them and the rest counted.
    void the_three_cases_of_5b_a_duo_per_volume()
    {
        serve(body(aSeries(u"Sept"_s, {}, {})), shelfOf({}));
        m_page->point(u"one"_s);
        settle();
        QList<Api::Entry> volumes;
        for (const auto &[writer, drawer] :
             {std::pair(u"Fabien Vehlmann"_s, u"Sean Phillips"_s),
              std::pair(u"Alain Ayroles"_s, u"Luigi Critone"_s),
              std::pair(u"Pierre Boisserie"_s, u"Éric Stalner"_s),
              std::pair(u"David Chauvel"_s, u"Michel Durand"_s),
              std::pair(u"Jean Dufaux"_s, u"Christian Rossi"_s)})
            volumes << aVolume({writer}, {drawer});
        m_page->description()->takeVolumes(u"one"_s, volumes);
        const QVariantList credits = rowsOf(m_page, u"credits"_s);
        QCOMPARE(valueOf(credits, u"Scénario"_s), u"Fabien Vehlmann, Alain Ayroles, Pierre Boisserie et 2 autres"_s);
        QCOMPARE(valueOf(credits, u"Dessin"_s), u"Sean Phillips, Luigi Critone, Éric Stalner et 2 autres"_s);
        QVERIFY(m_page->description()->makers().startsWith(
            u"Fabien Vehlmann, Sean Phillips et 8 autres"_s));
        QVERIFY(!Words::volumeCredits(volumes.at(1)).isEmpty());
    }
};

QTEST_MAIN(HoldsTheSeries)
#include "holds_the_series.moc"
