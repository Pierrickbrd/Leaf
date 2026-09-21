// The single offer above the shelf, against a real HTTP boundary and no window.

#include "Pretend.h"
#include "Resume.h"
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

QJsonObject anOffer(const QString &reason = u"IN_PROGRESS"_s,
                    const QJsonValue &progress = QJsonObject{
                        {u"page"_s, 47},
                        {u"pageCount"_s, 190},
                        {u"chapter"_s, QJsonObject{{u"label"_s, u"Chapitre 98"_s}}},
                    },
                    double number = 12.0)
{
    return {
        {u"seriesId"_s, u"ac"_s},
        {u"seriesName"_s, u"Assassination Classroom"_s},
        {u"reason"_s, reason},
        {u"entry"_s,
         QJsonObject{
             {u"id"_s, u"volume-12"_s},
             {u"type"_s, u"VOLUME"_s},
             {u"number"_s, number},
             {u"pageCount"_s, 190},
         }},
        {u"progress"_s, progress},
    };
}

QByteArray offers(const QJsonArray &rows)
{
    return QJsonDocument(rows).toJson(QJsonDocument::Compact);
}

} // namespace

class HoldsTheResume : public QObject
{
    Q_OBJECT

    Pretend *m_pretend = nullptr;
    Settings *m_settings = nullptr;
    Server *m_server = nullptr;
    Resume *m_resume = nullptr;

    void settle()
    {
        for (int i = 0; i < 200 && m_resume->loading(); ++i)
            QTest::qWait(10);
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
        m_resume = new Resume(m_server);
        m_pretend->answers(200, offers({}));
    }

    void cleanup()
    {
        delete m_resume;
        delete m_server;
        delete m_settings;
        delete m_pretend;
    }

    void a_new_resume_holds_nothing_and_asks_for_nothing()
    {
        QVERIFY(!m_resume->available());
        QVERIFY(!m_resume->loading());
        QVERIFY(m_pretend->heard.isEmpty());
    }

    void the_first_offer_fills_the_band_and_words_it()
    {
        m_pretend->answers(200, offers({anOffer()}));
        m_resume->reload();
        settle();

        QVERIFY(m_pretend->heard.contains("GET /next?limit=1 "));
        QVERIFY(m_resume->available());
        QVERIFY(!m_resume->loading());
        QVERIFY(m_resume->trouble().isEmpty());
        QCOMPARE(m_resume->seriesId(), u"ac"_s);
        QCOMPARE(m_resume->seriesName(), u"Assassination Classroom"_s);
        QCOMPARE(m_resume->entryId(), u"volume-12"_s);
        QCOMPARE(m_resume->where(), u"Tome 12 · Page 47/190 · Chapitre 98"_s);
        QCOMPARE(m_resume->action(), u"Reprendre"_s);
        QVERIFY(m_resume->hasProgress());
        QVERIFY(qAbs(m_resume->progress() - (47.0 / 190.0)) < 0.0001);
        // The entry's cover, not the series' — a series' is its first volume's, and a
        // band offering tome 12 that showed tome 1 would be the one picture that lies.
        QVERIFY(m_resume->cover().endsWith(u"/entries/volume-12/cover"_s));
    }

    /// The same offer, for a band that has lost the width for the long line.
    void a_band_at_half_a_screen_gets_a_shorter_line()
    {
        m_pretend->answers(200, offers({anOffer()}));
        m_resume->reload();
        settle();

        QCOMPARE(m_resume->whereShort(), u"T12 · Page 47/190"_s);
    }

    void the_next_unread_entry_has_no_made_up_progress()
    {
        m_pretend->answers(200, offers({anOffer(u"NEXT_UP"_s, QJsonValue::Null, 13)}));
        m_resume->reload();
        settle();

        QVERIFY(m_resume->available());
        QCOMPARE(m_resume->where(), u"Tome 13"_s);
        QCOMPARE(m_resume->action(), u"Continuer"_s);
        QVERIFY(!m_resume->hasProgress());
        QCOMPARE(m_resume->progress(), 0.0);
    }

    void an_empty_answer_removes_the_band_without_calling_it_an_error()
    {
        m_pretend->answers(200, offers({anOffer()}));
        m_resume->reload();
        settle();
        QVERIFY(m_resume->available());

        m_pretend->answers(200, offers({}));
        m_resume->reload();
        settle();

        QVERIFY(!m_resume->available());
        QVERIFY(m_resume->trouble().isEmpty());
        QVERIFY(m_resume->seriesName().isEmpty());
        QVERIFY(m_resume->cover().isEmpty());
    }

    void a_server_that_says_no_leaves_no_stale_offer_and_says_why()
    {
        m_pretend->answers(500, "{}");
        m_resume->reload();
        settle();

        QVERIFY(!m_resume->available());
        QVERIFY(!m_resume->loading());
        QVERIFY(!m_resume->trouble().isEmpty());
    }

    void a_non_array_answer_is_refused()
    {
        m_pretend->answers(200, "{}");
        m_resume->reload();
        settle();

        QVERIFY(!m_resume->available());
        QVERIFY(m_resume->trouble().contains(u"array"_s));
    }

    void a_broken_first_offer_is_refused()
    {
        m_pretend->answers(200, offers({QJsonObject{{u"seriesName"_s, u"Sans identifiant"_s}}}));
        m_resume->reload();
        settle();

        QVERIFY(!m_resume->available());
        QVERIFY(!m_resume->trouble().isEmpty());
    }

    void no_server_is_an_explanation_instead_of_a_crash()
    {
        Resume nowhere(nullptr);
        nowhere.reload();

        QVERIFY(!nowhere.available());
        QVERIFY(!nowhere.loading());
        QVERIFY(!nowhere.trouble().isEmpty());
        QVERIFY(nowhere.action().isEmpty());
    }
};

QTEST_GUILESS_MAIN(HoldsTheResume)
#include "holds_the_resume.moc"
