// File matches above the shelf, against the real HTTP seam and without a window.

#include "Pretend.h"
#include "Search.h"
#include "Server.h"
#include "Settings.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

QJsonObject aHit(const QString &kind, const QString &id, const QString &label,
                 const QString &seriesId = {}, const QString &seriesName = {},
                 const QString &entryId = {}, bool approximate = false)
{
    QJsonObject hit{{u"kind"_s, kind}, {u"id"_s, id}, {u"label"_s, label}};
    if (!seriesId.isEmpty())
        hit.insert(u"seriesId"_s, seriesId);
    if (!seriesName.isEmpty())
        hit.insert(u"seriesName"_s, seriesName);
    if (!entryId.isEmpty())
        hit.insert(u"entryId"_s, entryId);
    if (approximate)
        hit.insert(u"approximate"_s, true);
    return hit;
}

QByteArray hits(const QJsonArray &rows)
{
    return QJsonDocument(rows).toJson(QJsonDocument::Compact);
}

QByteArray hitPage(const QJsonArray &rows, int total, int fileTotal, int page, int size)
{
    return QJsonDocument(QJsonObject{{u"items"_s, rows},
                                     {u"total"_s, total},
                                     {u"fileTotal"_s, fileTotal},
                                     {u"page"_s, page},
                                     {u"size"_s, size}})
        .toJson(QJsonDocument::Compact);
}

QByteArray reply(const QByteArray &body)
{
    return "HTTP/1.1 200 .\r\nContent-Type: application/json\r\nContent-Length: "
           + QByteArray::number(body.size()) + "\r\n\r\n" + body;
}

int role(Search::Role value)
{
    return qToUnderlying(value);
}

} // namespace

class HoldsTheSearch : public QObject
{
    Q_OBJECT

    Pretend *m_pretend = nullptr;
    Settings *m_settings = nullptr;
    Server *m_server = nullptr;
    Search *m_search = nullptr;

    void settle()
    {
        for (int i = 0; i < 300 && m_search->loading(); ++i)
            QTest::qWait(10);
    }

    int requests() const
    {
        return m_pretend->heard.count("GET /search?");
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
        m_search = new Search(m_server, nullptr, nullptr);
        m_pretend->answers(200, hits({}));
    }

    void cleanup()
    {
        delete m_search;
        delete m_server;
        delete m_settings;
        delete m_pretend;
    }

    void a_new_search_holds_nothing_and_asks_for_nothing()
    {
        QVERIFY(!m_search->active());
        QVERIFY(!m_search->loading());
        QCOMPARE(m_search->count(), 0);
        QVERIFY(m_pretend->heard.isEmpty());
        QCOMPARE(m_search->placeholder(), u"Rechercher une série, un tome, un chapitre…"_s);
        QCOMPARE(m_search->shortPlaceholder(), u"Rechercher…"_s);
        QCOMPARE(m_search->clearLabel(), u"Effacer la recherche"_s);
        QCOMPARE(m_search->filterLabel(), u"Filtrer"_s);
        QCOMPARE(m_search->settingsLabel(), u"Réglages"_s);
        QCOMPARE(m_search->noSeriesLabel(), u"Aucune série ne porte ce nom."_s);
        QCOMPARE(m_search->overviewLabel(), u"Aperçu"_s);
        QCOMPARE(m_search->filesHeading(), u"Fichiers · 0"_s);
        QCOMPARE(m_search->sortLabel(), u"Trier : Nom · A → Z"_s);
        QCOMPARE(m_search->sortOptions().size(), 4);
    }

    /// A pill or an order must not put sixty lines out and a spinner in. What is on screen
    /// answers the previous criteria until there is an answer to the new ones, and the files
    /// both questions hold keep their rows — the same rule the shelf above them follows.
    void changing_the_criteria_keeps_the_files_until_the_answer_arrives()
    {
        m_pretend->answers(200, hits({aHit(u"ENTRY"_s, u"v1"_s, u"Tome 1"_s),
                                      aHit(u"ENTRY"_s, u"v2"_s, u"Tome 2"_s),
                                      aHit(u"ENTRY"_s, u"v3"_s, u"Tome 3"_s)}));
        m_search->searchFor(u"tome"_s, {}, {});
        settle();
        QCOMPARE(m_search->count(), 3);

        QSignalSpy reset(m_search, &QAbstractItemModel::modelAboutToBeReset);
        m_pretend->answers(200, hits({aHit(u"ENTRY"_s, u"v2"_s, u"Tome 2"_s),
                                      aHit(u"ENTRY"_s, u"v3"_s, u"Tome 3"_s),
                                      aHit(u"ENTRY"_s, u"v4"_s, u"Tome 4"_s)}));
        m_search->searchFor(u"tome"_s, {u"UNREAD"_s}, {});

        // Asked for, not yet answered: every line is still there.
        QCOMPARE(m_search->count(), 3);
        QVERIFY(m_search->loading());

        settle();
        QCOMPARE(m_search->count(), 3);
        QCOMPARE(reset.count(), 0);
        QCOMPARE(m_search->data(m_search->index(0),
                                static_cast<int>(Search::Role::Label)).toString(),
                 u"Tome 2"_s);
    }

    /// Clearing the field is different: nothing is being looked for, so the emptiness is the
    /// answer rather than a gap on the way to one.
    void clearing_what_was_typed_does_empty_the_rows()
    {
        m_pretend->answers(200, hits({aHit(u"ENTRY"_s, u"v1"_s, u"Tome 1"_s)}));
        m_search->searchFor(u"tome"_s, {}, {});
        settle();
        QCOMPARE(m_search->count(), 1);

        m_search->searchFor(QString(), {}, {});
        QCOMPARE(m_search->count(), 0);
        QVERIFY(!m_search->active());
    }

    void entries_and_chapters_become_rows_but_editions_do_not()
    {
        m_pretend->answers(
            200,
            hits({aHit(u"EDITION"_s, u"ac"_s, u"Assassination Classroom"_s),
                  aHit(u"ENTRY"_s, u"v1"_s, u"Assassinat"_s, u"ac"_s,
                       u"Assassination Classroom"_s),
                  aHit(u"CHAPTER"_s, u"c98"_s, u"Chapitre 98"_s, u"ac"_s,
                       u"Assassination Classroom"_s, u"v12"_s)}));

        m_search->searchFor(u"  assassinat & l’été  "_s, {u"UNREAD"_s}, {u"manga"_s});
        settle();

        QCOMPARE(requests(), 1);
        QVERIFY(m_pretend->heard.contains("GET /search?"));
        QVERIFY(m_pretend->heard.contains("q=assassinat%20%26%20l%E2%80%99%C3%A9t%C3%A9"));
        QVERIFY(m_pretend->heard.contains("limit=200"));
        QVERIFY(m_pretend->heard.contains("size=50"));
        QVERIFY(m_pretend->heard.contains("page=0"));
        QVERIFY(m_pretend->heard.contains("kind=EDITION"));
        QVERIFY(m_pretend->heard.contains("kind=ENTRY"));
        QVERIFY(m_pretend->heard.contains("kind=CHAPTER"));
        QVERIFY(m_pretend->heard.contains("read=UNREAD"));
        QVERIFY(m_pretend->heard.contains("medium=manga"));

        QCOMPARE(m_search->total(), 2);
        QCOMPARE(m_search->count(), 2);
        QCOMPARE(m_search->heading(), u"Fichiers · 2"_s);
        QCOMPARE(m_search->data(m_search->index(0), role(Search::Role::Label)).toString(),
                 u"Assassinat"_s);
        QCOMPARE(m_search->data(m_search->index(0), role(Search::Role::EntryId)).toString(),
                 u"v1"_s);
        QCOMPARE(m_search->data(m_search->index(1), role(Search::Role::EntryId)).toString(),
                 u"v12"_s);
        QVERIFY(m_search->data(m_search->index(1), role(Search::Role::Cover))
                    .toString()
                    .endsWith(u"/entries/v12/cover"_s));

        const auto names = m_search->roleNames();
        QCOMPARE(names.value(role(Search::Role::ResultId)), QByteArray("resultId"));
        QCOMPARE(names.value(role(Search::Role::Context)), QByteArray("context"));
        QVERIFY(!m_search->data({}, role(Search::Role::Label)).isValid());
        QVERIFY(!m_search->data(m_search->index(0), Qt::DisplayRole).isValid());
        QCOMPARE(m_search->rowCount(m_search->index(0)), 0);
    }

    void the_preview_can_show_four_while_the_model_pages_all_files()
    {
        QJsonArray first;
        for (int i = 1; i <= 4; ++i)
            first << aHit(u"ENTRY"_s, u"v%1"_s.arg(i), u"Tome %1"_s.arg(i));
        const QJsonArray second{
            aHit(u"ENTRY"_s, u"v5"_s, u"Tome 5"_s),
            aHit(u"ENTRY"_s, u"v6"_s, u"Tome 6"_s),
        };
        const QByteArray page0 = hitPage(first, 6, 6, 0, 4);
        const QByteArray page1 = hitPage(second, 6, 6, 1, 4);
        m_pretend->answerFor = [page0, page1](const QByteArray &request) {
            return reply(request.contains("page=1") ? page1 : page0);
        };

        m_search->searchFor(u"tome"_s, {}, {});
        settle();

        QCOMPARE(m_search->total(), 6);
        QCOMPARE(m_search->count(), 4);
        QCOMPARE(m_search->remaining(), 2);
        QCOMPARE(m_search->moreLabel(), u"Voir les 2 autres"_s);
        QVERIFY(!m_search->expanded());

        m_search->expand();
        settle();
        QVERIFY(m_search->expanded());
        QCOMPARE(m_search->count(), 6);
        QCOMPARE(m_search->remaining(), 0);
        QVERIFY(m_pretend->heard.contains("page=1"));

        m_search->expand();
        QCOMPARE(m_search->count(), 6);
    }

    void a_chapter_row_names_its_series_volume_and_page_count()
    {
        QJsonObject chapter = aHit(u"CHAPTER"_s, u"c98"_s, u"Assaut"_s, u"pd"_s,
                                   u"Parasite · Édition Deluxe"_s, u"v8"_s);
        chapter.insert(u"entryKind"_s, u"VOLUME"_s);
        chapter.insert(u"entryNumber"_s, 8.0);
        chapter.insert(u"entryTitle"_s, u"Invasion"_s);
        chapter.insert(u"entryPageCount"_s, 190);
        m_pretend->answers(200, hits({chapter}));

        m_search->searchFor(u"assaut"_s, {}, {});
        settle();

        QCOMPARE(m_search->data(m_search->index(0), role(Search::Role::Context)).toString(),
                 u"Parasite · Édition Deluxe · Tome 8 · Invasion · 190 pages"_s);
    }

    void five_files_need_no_more_action()
    {
        QJsonArray rows;
        for (int i = 1; i <= 5; ++i)
            rows << aHit(u"ENTRY"_s, u"v%1"_s.arg(i), u"Tome %1"_s.arg(i));
        m_pretend->answers(200, hits(rows));

        m_search->searchFor(u"tome"_s, {}, {});
        settle();

        QCOMPARE(m_search->count(), 5);
        QCOMPARE(m_search->remaining(), 0);
    }

    void nothing_exact_behind_a_chip_is_counted_once_without_the_chip()
    {
        const QByteArray empty = hits({});
        const QByteArray outside = hits({
            aHit(u"EDITION"_s, u"dn"_s, u"Death Note"_s),
            aHit(u"ENTRY"_s, u"dn1"_s, u"Tome 1"_s),
            aHit(u"CHAPTER"_s, u"dn2"_s, u"Chapitre 2"_s),
        });
        m_pretend->answerFor = [empty, outside](const QByteArray &request) {
            return reply(request.contains("medium=manga") ? empty : outside);
        };

        m_search->searchFor(u"ohba"_s, {}, {u"manga"_s});
        settle();

        QCOMPARE(requests(), 2);
        QCOMPARE(m_search->outsideFilters(),
                 u"Aucun résultat dans Manga · 3 sans les filtres"_s);
    }

    void a_guess_is_named_as_a_guess_and_never_drawn_as_a_file()
    {
        m_pretend->answers(
            200, hits({aHit(u"EDITION"_s, u"dn"_s, u"Tsugumi Ōba"_s,
                            {}, {}, {}, true)}));

        m_search->searchFor(u"ohba"_s, {}, {});
        settle();

        QCOMPARE(m_search->count(), 0);
        QCOMPARE(m_search->suggestion(), u"Vouliez-vous dire Tsugumi Ōba ?"_s);
        QVERIFY(m_search->outsideFilters().isEmpty());
    }

    void clearing_the_field_clears_rows_without_asking_for_an_empty_search()
    {
        m_pretend->answers(200, hits({aHit(u"ENTRY"_s, u"v1"_s, u"Tome 1"_s)}));
        m_search->searchFor(u"tome"_s, {}, {});
        settle();
        QCOMPARE(m_search->count(), 1);

        m_pretend->heard.clear();
        m_search->searchFor(u"   "_s, {}, {});

        QVERIFY(!m_search->active());
        QCOMPARE(m_search->count(), 0);
        QCOMPARE(requests(), 0);
    }

    void the_same_search_twice_goes_out_once()
    {
        m_search->searchFor(u"parasite"_s, {}, {});
        settle();
        m_pretend->heard.clear();

        m_search->searchFor(u"parasite"_s, {}, {});
        QCOMPARE(requests(), 0);
    }

    void a_broken_hit_refuses_the_answer_and_says_which_one()
    {
        m_pretend->answers(200, hits({aHit(u"ENTRY"_s, u"v1"_s, u"Tome 1"_s),
                                     QJsonObject{{u"kind"_s, u"ENTRY"_s}}}));

        m_search->searchFor(u"tome"_s, {}, {});
        settle();

        QCOMPARE(m_search->count(), 0);
        QVERIFY(m_search->trouble().contains(u"hits[1]"_s));
    }

    /// An object is an envelope now, so it is refused by the name of what it does not carry
    /// rather than for not being a list — « hits: total is missing » says where to look.
    void an_envelope_without_its_counts_is_refused_by_name()
    {
        m_pretend->answers(200, "{}");
        m_search->searchFor(u"tome"_s, {}, {});
        settle();

        QVERIFY2(m_search->trouble().contains(u"total"_s),
                 qPrintable(m_search->trouble()));
    }

    /// And something that is neither shape at all is refused before this ever sees it: a
    /// document that is not an object or a list does not survive being read off the wire,
    /// which is the right place for that to be noticed and said.
    void an_answer_that_is_neither_shape_is_refused()
    {
        m_pretend->answers(200, "\"non\"");
        m_search->searchFor(u"tome"_s, {}, {});
        settle();

        QVERIFY(m_search->count() == 0);
        QVERIFY2(!m_search->trouble().isEmpty(), "nothing was said about an unreadable answer");
    }

    void a_server_that_says_no_leaves_no_rows_and_says_why()
    {
        m_pretend->answers(500, "{}");
        m_search->searchFor(u"tome"_s, {}, {});
        settle();

        QCOMPARE(m_search->count(), 0);
        QVERIFY(!m_search->loading());
        QVERIFY(!m_search->trouble().isEmpty());
    }

    void no_server_is_an_explanation_instead_of_a_crash()
    {
        Search nowhere(nullptr, nullptr, nullptr);
        nowhere.searchFor(u"tome"_s, {}, {});

        QVERIFY(!nowhere.loading());
        QVERIFY(!nowhere.trouble().isEmpty());
    }
};

QTEST_GUILESS_MAIN(HoldsTheSearch)
#include "holds_the_search.moc"
