// What the three dots do once something has been chosen from them.

#include "Commands.h"
#include "Editing.h"
#include "Pretend.h"
#include "Selection.h"
#include "Server.h"
#include "Settings.h"
#include "Words.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

class CommandsTheLibrary : public QObject
{
    Q_OBJECT

    Pretend *m_pretend = nullptr;
    Settings *m_settings = nullptr;
    Server *m_server = nullptr;
    Commands *m_menu = nullptr;

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
        m_menu = new Commands(m_server);
        m_pretend->answers(200, QByteArrayLiteral("[]"));
    }

    void cleanup()
    {
        delete m_menu;
        delete m_server;
        delete m_settings;
        delete m_pretend;
    }

    /// One statement on the server rather than one call per volume, which is what makes it a
    /// route and not a loop here.
    void a_whole_edition_is_marked_read_in_one_request()
    {
        QSignalSpy done(m_menu, &Commands::marked);
        m_menu->markSeries(u"albums"_s, true);
        QVERIFY(done.wait(3000));

        QVERIFY(m_pretend->heard.contains("PATCH /series/albums/progress "));
        QVERIFY(m_pretend->heard.contains("{\"finished\":true}"));
        QCOMPARE(m_pretend->heard.count("PATCH"), 1);
        QCOMPARE(done.first().at(0).toString(), u"albums"_s);
        // Empty: a whole edition was marked, and nothing on screen should reload one line.
        QVERIFY(done.first().at(1).toString().isEmpty());
        QVERIFY(!m_menu->busy());
        QVERIFY(m_menu->trouble().isEmpty());
    }

    /// Unread is the record forgotten, never a rewind: moving it to page nought would leave
    /// « in progress, at page 0 », which is not the same fact and is not what was asked.
    void unread_forgets_the_record_rather_than_rewinding_it()
    {
        m_pretend->answers(204, QByteArray());
        QSignalSpy done(m_menu, &Commands::marked);
        m_menu->markSeries(u"albums"_s, false);
        QVERIFY(done.wait(3000));

        QVERIFY(m_pretend->heard.contains("DELETE /series/albums/progress "));
        QVERIFY(!m_pretend->heard.contains("PATCH"));
        QVERIFY(!m_pretend->heard.contains("finished"));
    }

    void one_volume_goes_by_its_own_route_and_says_which_series_it_was()
    {
        QSignalSpy done(m_menu, &Commands::marked);
        m_menu->markEntry(u"v5"_s, u"albums"_s, true);
        QVERIFY(done.wait(3000));

        QVERIFY(m_pretend->heard.contains("PATCH /entries/v5/progress "));
        QCOMPARE(done.first().at(0).toString(), u"albums"_s);
        QCOMPARE(done.first().at(1).toString(), u"v5"_s);

        m_pretend->heard.clear();
        m_pretend->answers(204, QByteArray());
        m_menu->markEntry(u"v5"_s, u"albums"_s, false);
        QVERIFY(done.wait(3000));
        QVERIFY(m_pretend->heard.contains("DELETE /entries/v5/progress "));
    }

    /// A refusal says so and changes nothing: a screen that reloaded on a command the server
    /// turned down would draw the state it asked for rather than the state that is.
    void a_refusal_is_said_and_nothing_is_announced()
    {
        // A 500 and not a 403: a refused key stops this client for good, which is a fact
        // about the whole run rather than about one command.
        m_pretend->answers(500, QByteArrayLiteral(R"({"error":"le disque ne répond pas"})"));
        QSignalSpy done(m_menu, &Commands::marked);
        QSignalSpy moved(m_menu, &Commands::changed);
        m_menu->markSeries(u"albums"_s, true);
        QTRY_VERIFY(moved.size() >= 2);

        QCOMPARE(done.size(), 0);
        QVERIFY(!m_menu->busy());
        QVERIFY(m_menu->trouble().contains(u"le disque ne répond pas"_s));
    }

    /// The archive itself, written where the reader chose — and read as bytes, because a
    /// `.cbz` is not JSON and parsing it would turn the one answer that arrived whole into
    /// « the server said something this client cannot read ».
    void a_copy_is_written_whole_where_it_was_asked_for()
    {
        const QByteArray archive = QByteArrayLiteral("PK\x03\x04 not really a zip, but bytes");
        m_pretend->answer = "HTTP/1.1 200 .\r\nContent-Type: application/vnd.comicbook+zip\r\n"
                            "Content-Length: " + QByteArray::number(archive.size())
                            + "\r\n\r\n" + archive;

        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const QString path = folder.filePath(u"Tome 5.cbz"_s);
        QSignalSpy written(m_menu, &Commands::saved);
        m_menu->saveACopy(u"v5"_s, QUrl::fromLocalFile(path));
        QVERIFY(written.wait(3000));

        QVERIFY(m_pretend->heard.contains("GET /entries/v5/file "));
        QCOMPARE(written.first().at(0).toString(), path);
        QFile out(path);
        QVERIFY(out.open(QIODevice::ReadOnly));
        QCOMPARE(out.readAll(), archive);
        QVERIFY(m_menu->trouble().isEmpty());
    }

    /// A volume is a hundred megabytes and more. It goes to the disk as it arrives and not
    /// through a buffer held whole, and what lands is every byte of it.
    void a_large_copy_arrives_whole()
    {
        QByteArray archive(6 * 1024 * 1024, '\0');
        for (qsizetype i = 0; i < archive.size(); ++i)
            archive[i] = char((i * 31 + i / 4099) & 0xFF);
        m_pretend->answer = "HTTP/1.1 200 .\r\nContent-Type: application/vnd.comicbook+zip\r\n"
                            "Content-Length: " + QByteArray::number(archive.size())
                            + "\r\n\r\n" + archive;

        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const QString path = folder.filePath(u"Gros.cbz"_s);
        QSignalSpy written(m_menu, &Commands::saved);
        m_menu->saveACopy(u"v5"_s, QUrl::fromLocalFile(path));
        QVERIFY(written.wait(10000));

        QFile out(path);
        QVERIFY(out.open(QIODevice::ReadOnly));
        QCOMPARE(out.size(), archive.size());
        QVERIFY(out.readAll() == archive);
    }

    /// Nothing is written when nothing came back, and the file the reader named is not left
    /// behind empty: an archive that opens and is the wrong length is worse than none.
    void a_copy_that_was_refused_leaves_no_file_behind()
    {
        m_pretend->answers(404, QByteArrayLiteral(R"({"error":"ce fichier n’est plus là"})"));
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const QString path = folder.filePath(u"Tome 5.cbz"_s);
        QSignalSpy written(m_menu, &Commands::saved);
        QSignalSpy moved(m_menu, &Commands::changed);
        m_menu->saveACopy(u"v5"_s, QUrl::fromLocalFile(path));
        QTRY_VERIFY(moved.size() >= 2);

        QCOMPARE(written.size(), 0);
        QVERIFY(!QFile::exists(path));
        QVERIFY(m_menu->trouble().contains(u"ce fichier n’est plus là"_s));
    }

    /// A copy that cannot reach the disk is said by the name the reader chose, and the half
    /// of it that did reach is taken away again.
    void a_copy_that_cannot_be_written_says_so()
    {
        const QByteArray archive = QByteArrayLiteral("some bytes");
        m_pretend->answer = "HTTP/1.1 200 .\r\nContent-Length: "
                            + QByteArray::number(archive.size()) + "\r\n\r\n" + archive;
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        // A folder is not a file, and opening one for writing fails on every platform this
        // runs on — which is the only way to reach this branch without a mock.
        const QString path = folder.filePath(u"a-folder"_s);
        QVERIFY(QDir().mkpath(path));

        QSignalSpy written(m_menu, &Commands::saved);
        QSignalSpy moved(m_menu, &Commands::changed);
        m_menu->saveACopy(u"v5"_s, QUrl::fromLocalFile(path));
        QTRY_VERIFY(moved.size() >= 2);

        QCOMPARE(written.size(), 0);
        QVERIFY(m_menu->trouble().contains(u"a-folder"_s));
    }

    /// Two gestures in a row, and the first answer arrives after the second was asked for.
    /// It is dropped: a menu chosen twice quickly must not end with the older answer written
    /// over the newer one.
    void an_answer_to_a_command_already_replaced_is_dropped()
    {
        const QByteArray archive = QByteArrayLiteral("bytes");
        m_pretend->answer = "HTTP/1.1 200 .\r\nContent-Length: "
                            + QByteArray::number(archive.size()) + "\r\n\r\n" + archive;
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const QString path = folder.filePath(u"Tome 5.cbz"_s);

        QSignalSpy written(m_menu, &Commands::saved);
        m_menu->saveACopy(u"v5"_s, QUrl::fromLocalFile(path));
        // Before the file can have arrived, something else is asked for.
        m_menu->markSeries(u"albums"_s, true);
        QTest::qWait(400);

        QCOMPARE(written.size(), 0);
        QVERIFY(!QFile::exists(path));
    }

    void nothing_is_sent_about_nothing()
    {
        m_menu->markSeries(QString(), true);
        m_menu->markEntry(QString(), u"albums"_s, true);
        m_menu->saveACopy(QString(), QUrl::fromLocalFile(u"/tmp/x.cbz"_s));
        // A URL that is not a file at all — a `file:` dialog cancelled hands one back.
        m_menu->saveACopy(u"v5"_s, QUrl(u"https://elsewhere/x.cbz"_s));
        QTest::qWait(150);

        QVERIFY(m_pretend->heard.isEmpty());
        QVERIFY(!m_menu->busy());
    }

    /// The way a series is read is the caller's own choice, sent to its own route, and
    /// « Par défaut » is `null` taking it away rather than a field left out.
    void a_reading_direction_is_chosen_for_one_series_and_taken_back_with_null()
    {
        const QByteArray series =
            R"({"id":"albums","workId":"w","name":"Elfes","work":"Elfes","entryCount":1,)"
            R"("chapterCount":0,"arcCount":0,"readingDirectionChosen":"RIGHT_TO_LEFT"})";
        m_pretend->answers(200, series);
        QSignalSpy moved(m_menu, &Commands::directionChanged);
        m_menu->chooseDirection(u"albums"_s, 1);
        QVERIFY(moved.wait(3000));
        QVERIFY(m_pretend->heard.contains("PUT /series/albums/reading-direction "));
        QVERIFY(m_pretend->heard.contains("\"readingDirection\":\"RIGHT_TO_LEFT\""));
        QCOMPARE(m_menu->direction(), 1);

        m_pretend->answers(200, QByteArrayLiteral(
            R"({"id":"albums","workId":"w","name":"Elfes","work":"Elfes","entryCount":1,)"
            R"("chapterCount":0,"arcCount":0})"));
        m_pretend->heard.clear();
        m_menu->chooseDirection(u"albums"_s, -1);
        QVERIFY(moved.wait(3000));
        QVERIFY(m_pretend->heard.contains("\"readingDirection\":null"));
        QCOMPARE(m_menu->direction(), -1);

        // A value the contract does not have is not sent.
        m_pretend->heard.clear();
        m_menu->chooseDirection(u"albums"_s, 9);
        QVERIFY(m_pretend->heard.isEmpty());
    }

    /// A field emptied is `null` on the wire and a field not touched is not there: the two
    /// are the whole of « take it away » and « leave it ».
    void an_edit_sends_what_changed_and_null_for_what_was_taken_away()
    {
        m_pretend->answers(200, QByteArrayLiteral("{}"));
        QSignalSpy done(m_menu, &Commands::edited);
        m_menu->editSeries(u"albums"_s, {{u"title"_s, u"Elfes"_s},
                                         {u"ageRating"_s, QVariant()},
                                         {u"genres"_s, QStringList{u"Fantasy"_s}}});
        QVERIFY(done.wait(3000));
        QVERIFY(m_pretend->heard.contains("PATCH /series/albums "));
        QVERIFY(m_pretend->heard.contains("\"ageRating\":null"));
        QVERIFY(m_pretend->heard.contains("\"title\":\"Elfes\""));
        QVERIFY(m_pretend->heard.contains("\"genres\":[\"Fantasy\"]"));
        QVERIFY(!m_pretend->heard.contains("summary"));
        QCOMPARE(done.first().at(0).toString(), u"albums"_s);
        QVERIFY(done.first().at(1).toString().isEmpty());

        m_pretend->heard.clear();
        m_menu->editEntry(u"v2"_s, u"albums"_s, {{u"isbn"_s, QVariant()}});
        QVERIFY(done.wait(3000));
        QVERIFY(m_pretend->heard.contains("PATCH /entries/v2 "));
        QVERIFY(m_pretend->heard.contains("\"isbn\":null"));
        QCOMPARE(done.last().at(1).toString(), u"v2"_s);
    }

    /// The series' window starts from what `work.json` declares, never from every name its
    /// volumes carry: Jul wrote three albums, and saving the window made him the series'
    /// writer for the ones that name nobody.
    void the_series_dialog_starts_from_what_work_json_declares()
    {
        m_pretend->answers(200, QByteArray(R"({"id":"lucky","workId":"lucky","name":"Lucky Luke",
            "work":"Lucky Luke","entryCount":2,"chapterCount":0,"arcCount":0,"medium":"bd",
            "authors":["René Goscinny","Jul"],"artists":["Morris","Achdé"],
            "declaredAuthors":["René Goscinny"],"declaredArtists":["Morris"]})"));
        Editing window(m_server);
        QSignalSpy loaded(&window, &Editing::seriesLoaded);
        window.loadSeries(u"lucky"_s);
        QVERIFY(loaded.wait(3000));
        const QVariantMap said = loaded.first().at(0).toMap();
        QCOMPARE(said.value(u"authors"_s).toStringList(), QStringList{u"René Goscinny"_s});
        QCOMPARE(said.value(u"artists"_s).toStringList(), QStringList{u"Morris"_s});
        QCOMPARE(said.value(u"authorsDefault"_s).toString(),
                 Words::creditDefault(false, Api::Medium::Bd));
        QCOMPARE(said.value(u"artistsDefault"_s).toString(),
                 Words::creditDefault(true, Api::Medium::Bd));
    }

    /// A refusal is said, and nothing is announced as written.
    void an_edit_the_server_refuses_is_said_and_not_announced()
    {
        m_pretend->answers(400, QByteArrayLiteral(
            R"({"error":"another file of this edition already has that number"})"));
        QSignalSpy done(m_menu, &Commands::edited);
        QSignalSpy told(m_menu, &Commands::changed);
        m_menu->editEntry(u"v2"_s, u"albums"_s, {{u"number"_s, 1}});
        QTRY_VERIFY(!m_menu->trouble().isEmpty());
        QVERIFY(done.isEmpty());
    }

    void with_no_server_the_menus_open_and_command_nothing()
    {
        Commands orphan(nullptr);
        orphan.markSeries(u"albums"_s, true);
        orphan.markEntry(u"v5"_s, u"albums"_s, false);
        orphan.saveACopy(u"v5"_s, QUrl::fromLocalFile(u"/tmp/x.cbz"_s));

        QVERIFY(!orphan.busy());
        QVERIFY(orphan.trouble().isEmpty());
    }

    /// A selection of editions is one request each and one announcement each: a tile is
    /// redrawn from the edition it names, and nothing else would tell it.
    void several_editions_are_marked_read_by_a_request_each()
    {
        QSignalSpy done(m_menu, &Commands::marked);
        m_menu->markMany({u"albums"_s, u"romans"_s, u"revues"_s}, QString(), true);
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 3, 3000);

        QCOMPARE(m_pretend->heard.count("PATCH /series/"), 3);
        QVERIFY(m_pretend->heard.contains("PATCH /series/romans/progress "));
        QCOMPARE(m_pretend->heard.count("{\"finished\":true}"), 3);
        QVERIFY(!m_menu->busy());
    }

    /// The volumes of one edition say it once, after the last answer: five announcements for
    /// one page would be five reloads of the same list.
    void several_volumes_are_forgotten_and_announced_once()
    {
        m_pretend->answers(204, QByteArray());
        QSignalSpy done(m_menu, &Commands::marked);
        m_menu->markMany({u"v1"_s, u"v2"_s, u"v3"_s}, u"albums"_s, false);
        QVERIFY(done.wait(3000));
        QTest::qWait(100);

        QCOMPARE(m_pretend->heard.count("DELETE /entries/"), 3);
        QCOMPARE(done.count(), 1);
        QCOMPARE(done.first().at(0).toString(), u"albums"_s);
        QVERIFY(done.first().at(1).toString().isEmpty());
    }

    /// Nothing picked is nothing asked: an empty list that still said `marked` would reload
    /// a page for a command that was never given.
    void marking_nothing_asks_nothing()
    {
        QSignalSpy done(m_menu, &Commands::marked);
        m_menu->markMany({}, QString(), true);
        QTest::qWait(100);
        QVERIFY(m_pretend->heard.isEmpty());
        QCOMPARE(done.count(), 0);
        QVERIFY(!m_menu->busy());
    }

    /// A refusal is said and nothing is announced: the tiles keep the state that is true.
    void a_refused_batch_says_so_and_announces_nothing()
    {
        m_pretend->answers(500, QByteArrayLiteral(R"({"error":"le disque ne répond pas"})"));
        QSignalSpy done(m_menu, &Commands::marked);
        QSignalSpy moved(m_menu, &Commands::changed);
        m_menu->markMany({u"albums"_s, u"romans"_s}, QString(), true);
        QTRY_VERIFY_WITH_TIMEOUT(!m_menu->busy() && moved.count() >= 2, 3000);

        QVERIFY(!m_menu->trouble().isEmpty());
        QCOMPARE(done.count(), 0);
    }

    /// Picking the same one twice puts it back, and picking across kinds starts again: a
    /// series and a volume have no command in common that means the same to both.
    void picking_again_puts_back_and_another_kind_starts_over()
    {
        Selection picked;
        picked.toggle(u"albums"_s);
        picked.toggle(u"romans"_s);
        QCOMPARE(picked.ids(), (QStringList{u"albums"_s, u"romans"_s}));
        picked.toggle(u"albums"_s);
        QCOMPARE(picked.ids(), QStringList{u"romans"_s});

        picked.toggle(u"v1"_s, u"romans"_s);
        QCOMPARE(picked.ids(), QStringList{u"v1"_s});
        QCOMPARE(picked.seriesId(), u"romans"_s);
        // Another edition's volume is another selection, too.
        picked.toggle(u"w9"_s, u"albums"_s);
        QCOMPARE(picked.ids(), QStringList{u"w9"_s});

        picked.clear();
        QVERIFY(!picked.active());
        QVERIFY(picked.seriesId().isEmpty());
    }

    /// The bar says what it holds in French, singular for one: « 1 série sélectionnée ».
    void the_selection_counts_in_french()
    {
        Selection picked;
        picked.toggle(u"albums"_s);
        QCOMPARE(picked.label(), u"1 série sélectionnée"_s);
        picked.toggle(u"romans"_s);
        QCOMPARE(picked.label(), u"2 séries sélectionnées"_s);
        picked.clear();
        picked.toggle(u"v1"_s, u"albums"_s);
        picked.toggle(u"v2"_s, u"albums"_s);
        QCOMPARE(picked.label(), u"2 tomes sélectionnés"_s);
    }

    /// Twenty-one entries and the word that closes a window, each a sentence naming what it acts on,
    /// and no two the same: a menu with two rows saying the same thing is a menu nobody can
    /// use.
    void the_entries_are_worded_once_and_differently()
    {
        const QVariantMap said = m_menu->words();
        QCOMPARE(said.size(), 22);
        QSet<QString> seen;
        for (auto it = said.constBegin(); it != said.constEnd(); ++it) {
            QVERIFY2(!it.value().toString().isEmpty(), qPrintable(it.key()));
            seen.insert(it.value().toString());
        }
        QCOMPARE(seen.size(), 22);
        QCOMPARE(said.value(u"markSeriesRead"_s).toString(),
                 u"Marquer toute la série comme lue"_s);
        QCOMPARE(said.value(u"saveACopy"_s).toString(), u"Enregistrer une copie…"_s);
        QCOMPARE(said.value(u"replaceChapter"_s).toString(), u"Remplacer un chapitre…"_s);
    }
};

QTEST_MAIN(CommandsTheLibrary)
#include "commands_the_library.moc"
