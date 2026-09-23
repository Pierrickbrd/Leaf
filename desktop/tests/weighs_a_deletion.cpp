// The one confirmation that stands in front of something a scan will not undo.

#include "Erasure.h"
#include "Pretend.h"
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

QJsonObject aVolume(double number, const QString &title, qint64 size = 48000000)
{
    return {{u"id"_s, u"v%1"_s.arg(number)},
            {u"type"_s, u"VOLUME"_s},
            {u"number"_s, number},
            {u"title"_s, title},
            {u"pageCount"_s, 54},
            {u"chapterCount"_s, 0},
            {u"file"_s, u"Elfes - Tome %1.cbz"_s.arg(number)},
            {u"size"_s, size}};
}

QJsonObject anEdition(const QString &id, const QString &edition, int owned, int read = 0,
                      double part = 0.0)
{
    return {{u"id"_s, id},
            {u"workId"_s, u"elfes"_s},
            {u"name"_s, u"Terres d'Arran · Elfes"_s},
            {u"work"_s, u"Elfes"_s},
            {u"edition"_s, edition},
            {u"medium"_s, u"bd"_s},
            {u"ownedVolumes"_s, owned},
            {u"entryCount"_s, owned},
            {u"chapterCount"_s, 0},
            {u"arcCount"_s, 0},
            {u"readEntries"_s, read},
            {u"partRead"_s, part}};
}

QByteArray answering(const QByteArray &body)
{
    return "HTTP/1.1 200 .\r\nContent-Type: application/json\r\nContent-Length: "
           + QByteArray::number(body.size()) + "\r\n\r\n" + body;
}

QByteArray compact(const QJsonArray &rows)
{
    return QJsonDocument(rows).toJson(QJsonDocument::Compact);
}

QByteArray compact(const QJsonObject &one)
{
    return QJsonDocument(one).toJson(QJsonDocument::Compact);
}

} // namespace

class WeighsADeletion : public QObject
{
    Q_OBJECT

    Pretend *m_pretend = nullptr;
    Settings *m_settings = nullptr;
    Server *m_server = nullptr;
    Erasure *m_asking = nullptr;

    void settle()
    {
        for (int i = 0; i < 300 && m_asking->loading(); ++i)
            QTest::qWait(10);
        QTest::qWait(60);
    }

    /// Three volumes, one of which has been read, and one other edition of the same work.
    void serve(const QJsonArray &files, const QJsonArray &editions = {})
    {
        const QByteArray one = compact(anEdition(u"albums"_s, u"Albums"_s,
                                                 int(files.size()), 4, 0.25));
        const QByteArray all = compact(files);
        const QJsonArray siblings = editions.isEmpty()
                                        ? QJsonArray{anEdition(u"albums"_s, u"Albums"_s, 29)}
                                        : editions;
        const QByteArray shelf = compact(QJsonObject{{u"items"_s, siblings},
                                                     {u"total"_s, siblings.size()},
                                                     {u"page"_s, 0},
                                                     {u"size"_s, siblings.size()}});
        m_pretend->answerFor = [one, all, shelf](const QByteArray &request) {
            if (request.startsWith("DELETE "))
                return answering(QByteArrayLiteral(R"({"files":1,"bytes":48000000})"));
            if (request.contains("/entries "))
                return answering(all);
            if (request.startsWith("GET /series?"))
                return answering(shelf);
            return answering(one);
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
        m_asking = new Erasure(m_server);
        serve({aVolume(22, u"Les Portes de Nyn"_s), aVolume(23, u"La Nuit des Sylvains"_s),
               aVolume(24, u"Le Serment de Lanawyn"_s)});
    }

    void cleanup()
    {
        delete m_asking;
        delete m_server;
        delete m_settings;
        delete m_pretend;
    }

    /// Nothing is asked for and nothing is deleted until the confirmation is up.
    void nothing_is_asked_for_before_the_question_is()
    {
        QVERIFY(!m_asking->asking());
        QVERIFY(m_asking->said().isEmpty());
        QVERIFY(!m_asking->ready());
        m_asking->go();
        QVERIFY(m_pretend->heard.isEmpty());
    }

    /// A file in the middle leaves a hole, for ever, and the confirmation says so. It is the
    /// one sentence that changes with the volume, and the reason it is composed at all.
    void a_file_in_the_middle_says_the_hole_it_leaves()
    {
        m_asking->aboutEntry(u"v23"_s, u"albums"_s);
        settle();

        QVERIFY(m_asking->asking());
        QVERIFY(!m_asking->whole());
        const QVariantMap said = m_asking->said();
        QCOMPARE(said.value(u"question"_s).toString(), u"Supprimer le tome 23 ?"_s);
        QCOMPARE(said.value(u"what"_s).toString(),
                 u"La Nuit des Sylvains — 54 pages, 45,8 Mio"_s);
        QCOMPARE(said.value(u"file"_s).toString(), u"Fichier : Elfes - Tome 23.cbz"_s);
        QVERIFY(said.value(u"leaves"_s).toString().startsWith(u"Il restera 2 albums"_s));
        QVERIFY(said.value(u"leaves"_s).toString().contains(u"rejoindra les manquants"_s));
        // And the numbering does not move: the twenty-fourth stays the twenty-fourth.
        QVERIFY(said.value(u"leaves"_s).toString().contains(u"le 24 reste le 24"_s));
        QVERIFY(said.value(u"warning"_s).toString().contains(u"corbeille"_s));
        // No name to type for one file: the same weight on a single volume would be ceremony.
        QVERIFY(said.value(u"confirm"_s).toString().isEmpty());
        QVERIFY(m_asking->ready());
    }

    /// The last one takes the ceiling down with it, and the first raises the floor. Neither
    /// is reported missing, and a confirmation saying otherwise would be a lie about what is
    /// left behind.
    void an_end_of_the_run_leaves_no_hole()
    {
        m_asking->aboutEntry(u"v24"_s, u"albums"_s);
        settle();
        QVERIFY(!m_asking->said().value(u"leaves"_s).toString().contains(u"manquants"_s));
        // Nothing is held after it, so there is no number to promise stays where it is.
        QVERIFY(!m_asking->said().value(u"leaves"_s).toString().contains(u"reste le"_s));

        m_asking->aboutEntry(u"v22"_s, u"albums"_s);
        settle();
        QVERIFY(!m_asking->said().value(u"leaves"_s).toString().contains(u"manquants"_s));
        QVERIFY(m_asking->said().value(u"leaves"_s).toString().contains(u"le 23 reste le 23"_s));
    }

    /// Thirty files and nothing to come back to: the name is typed, or the button stays dead.
    void a_whole_edition_asks_for_its_name_to_be_typed()
    {
        serve({aVolume(1, u"Le Crystal"_s), aVolume(2, u"L'Honneur"_s)},
              {anEdition(u"albums"_s, u"Albums"_s, 29),
               anEdition(u"integrale"_s, u"Intégrale"_s, 10)});
        m_asking->aboutSeries(u"albums"_s);
        settle();

        QVERIFY(m_asking->whole());
        const QVariantMap said = m_asking->said();
        QCOMPARE(said.value(u"question"_s).toString(), u"Supprimer « Albums » ?"_s);
        QVERIFY(said.value(u"what"_s).toString().startsWith(u"2 fichiers, 91,6 Mio."_s));
        // What goes besides the files, in the same sentence: a reader who has to look twice
        // finds it afterwards.
        QVERIFY(said.value(u"what"_s).toString().contains(u"4 tomes lus"_s));
        QVERIFY(said.value(u"what"_s).toString().contains(u"position dans le 5"_s));
        // And the other edition of the same work, named — « Supprimer Albums » is ambiguous
        // exactly on the day it must not be.
        QCOMPARE(said.value(u"untouched"_s).toString(),
                 u"L’édition Intégrale de la même œuvre n’est pas touchée."_s);
        QCOMPARE(said.value(u"confirm"_s).toString(), u"Tapez Albums pour confirmer"_s);

        QVERIFY(!m_asking->ready());
        m_asking->typed(u"albums"_s);
        QVERIFY(!m_asking->ready());
        m_asking->typed(u"Albums"_s);
        QVERIFY(m_asking->ready());
    }

    /// A button drawn dead is one way in; an invokable is another. The one screen that unmakes
    /// files says no twice.
    void it_refuses_to_go_before_the_name_is_right()
    {
        serve({aVolume(1, u"Le Crystal"_s)});
        m_asking->aboutSeries(u"albums"_s);
        settle();
        m_pretend->heard.clear();

        m_asking->go();
        QVERIFY(!m_pretend->heard.contains("DELETE"));

        m_asking->typed(u"Albums"_s);
        QSignalSpy gone(m_asking, &Erasure::erased);
        m_asking->go();
        QVERIFY(gone.wait(3000));
        QCOMPARE(gone.first().at(0).toString(), u"albums"_s);
        QVERIFY(gone.first().at(1).toString().isEmpty());
        QVERIFY(m_pretend->heard.contains("DELETE /series/albums "));
        // And the confirmation is gone with it.
        QVERIFY(!m_asking->asking());
    }

    void one_file_goes_by_its_own_route()
    {
        m_asking->aboutEntry(u"v23"_s, u"albums"_s);
        settle();
        QSignalSpy gone(m_asking, &Erasure::erased);
        m_asking->go();
        QVERIFY(gone.wait(3000));

        QVERIFY(m_pretend->heard.contains("DELETE /entries/v23 "));
        QCOMPARE(gone.first().at(1).toString(), u"v23"_s);
    }

    /// A file that would not go keeps its row and keeps the edition alive with it, so the
    /// confirmation stays up and names them rather than closing on a job half done.
    void a_file_that_would_not_go_is_named_and_the_question_stays()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.startsWith("DELETE "))
                return answering(QByteArrayLiteral(
                    R"({"files":0,"bytes":0,"refused":["Elfes - Tome 23.cbz"]})"));
            if (request.contains("/entries "))
                return answering(compact(QJsonArray{aVolume(23, u"La Nuit des Sylvains"_s)}));
            if (request.startsWith("GET /series?"))
                return answering(QByteArrayLiteral(
                    R"({"items":[],"total":0,"page":0,"size":0})"));
            return answering(compact(anEdition(u"albums"_s, u"Albums"_s, 1)));
        };
        m_asking->aboutEntry(u"v23"_s, u"albums"_s);
        settle();
        QSignalSpy gone(m_asking, &Erasure::erased);
        m_asking->go();
        QTest::qWait(300);

        QCOMPARE(gone.size(), 0);
        QVERIFY(m_asking->asking());
        QVERIFY(m_asking->trouble().contains(u"Elfes - Tome 23.cbz"_s));
    }

    /// A refusal of the edition costs the question: nothing is deleted from a screen that
    /// could not say what it was about to delete.
    void a_refusal_leaves_nothing_to_confirm()
    {
        m_pretend->answerFor = nullptr;
        m_pretend->answers(500, QByteArrayLiteral("{}"));
        m_asking->aboutSeries(u"albums"_s);
        settle();

        QVERIFY(m_asking->asking());
        QVERIFY(!m_asking->ready());
        QVERIFY(!m_asking->trouble().isEmpty());
        QVERIFY(m_asking->said().isEmpty());
    }

    /// An edition whose answer does not hold what an edition holds. The confirmation stays
    /// empty rather than asking about a name it had to invent.
    void an_edition_that_cannot_be_read_leaves_nothing_to_confirm()
    {
        m_pretend->answerFor = [](const QByteArray &request) {
            if (request.contains("/entries "))
                return answering(QByteArrayLiteral("[]"));
            return answering(QByteArrayLiteral(R"({"id":"albums"})"));
        };
        m_asking->aboutSeries(u"albums"_s);
        settle();

        QVERIFY(m_asking->said().isEmpty());
        QVERIFY(!m_asking->ready());
        QVERIFY(m_asking->trouble().contains(u"series"_s));
    }

    /// Two answers a deletion cannot make anything of: one that is not an object at all, and
    /// one that is an object saying nothing about what went. Neither closes the question —
    /// the files may or may not be there, and only the server knows.
    void a_deletion_that_answers_nothing_readable_keeps_the_question_up()
    {
        const auto answerDelete = [this](const QByteArray &body) {
            m_pretend->answerFor = [body](const QByteArray &request) {
                if (request.startsWith("DELETE "))
                    return answering(body);
                if (request.contains("/entries "))
                    return answering(compact(QJsonArray{aVolume(23, u"La Nuit"_s)}));
                return answering(compact(anEdition(u"albums"_s, u"Albums"_s, 1)));
            };
        };

        answerDelete(QByteArrayLiteral("[]"));
        m_asking->aboutEntry(u"v23"_s, u"albums"_s);
        settle();
        QSignalSpy gone(m_asking, &Erasure::erased);
        m_asking->go();
        QTRY_VERIFY(!m_asking->trouble().isEmpty());
        QCOMPARE(gone.size(), 0);
        QVERIFY(m_asking->asking());

        answerDelete(QByteArrayLiteral(R"({"bytes":0})"));
        m_asking->aboutEntry(u"v23"_s, u"albums"_s);
        settle();
        m_asking->go();
        QTRY_VERIFY(m_asking->trouble().contains(u"erased"_s));
        QCOMPARE(gone.size(), 0);
        QVERIFY(m_asking->asking());
    }

    /// The same name typed twice is not news, and a field that redrew the modal on every
    /// keystroke that changed nothing would redraw it on every keystroke.
    void typing_the_same_thing_twice_changes_nothing()
    {
        serve({aVolume(1, u"Le Crystal"_s)});
        m_asking->aboutSeries(u"albums"_s);
        settle();
        m_asking->typed(u"Alb"_s);

        QSignalSpy moved(m_asking, &Erasure::changed);
        m_asking->typed(u"Alb"_s);
        QCOMPARE(moved.size(), 0);
    }

    void dismissing_forgets_what_it_was_about()
    {
        m_asking->aboutEntry(u"v23"_s, u"albums"_s);
        settle();
        QVERIFY(!m_asking->said().isEmpty());

        m_asking->dismiss();
        QVERIFY(!m_asking->asking());
        QVERIFY(m_asking->said().isEmpty());
        QVERIFY(!m_asking->ready());
        QVERIFY(m_asking->trouble().isEmpty());
    }

    /// The answer to a question nobody is asking any more is dropped: a confirmation closed
    /// while its three requests were out would otherwise fill itself in behind the reader.
    void an_answer_to_a_dismissed_question_is_dropped()
    {
        m_asking->aboutEntry(u"v23"_s, u"albums"_s);
        m_asking->dismiss();
        QTest::qWait(300);

        QVERIFY(!m_asking->asking());
        QVERIFY(m_asking->said().isEmpty());
    }

    void with_no_server_it_says_so_rather_than_offering_a_button()
    {
        Erasure orphan(nullptr);
        orphan.aboutSeries(u"albums"_s);

        QVERIFY(orphan.asking());
        QVERIFY(!orphan.ready());
        QVERIFY(!orphan.trouble().isEmpty());
    }
};

QTEST_MAIN(WeighsADeletion)
#include "weighs_a_deletion.moc"
