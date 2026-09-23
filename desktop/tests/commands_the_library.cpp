// What the three dots do once something has been chosen from them.

#include "Commands.h"
#include "Pretend.h"
#include "Server.h"
#include "Settings.h"

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

    void with_no_server_the_menus_open_and_command_nothing()
    {
        Commands orphan(nullptr);
        orphan.markSeries(u"albums"_s, true);
        orphan.markEntry(u"v5"_s, u"albums"_s, false);
        orphan.saveACopy(u"v5"_s, QUrl::fromLocalFile(u"/tmp/x.cbz"_s));

        QVERIFY(!orphan.busy());
        QVERIFY(orphan.trouble().isEmpty());
    }

    /// Nine entries, each a sentence naming what it acts on, and no two the same: a menu with
    /// two rows saying the same thing is a menu nobody can use.
    void the_nine_entries_are_worded_once_and_differently()
    {
        const QVariantMap said = m_menu->words();
        QCOMPARE(said.size(), 9);
        QSet<QString> seen;
        for (auto it = said.constBegin(); it != said.constEnd(); ++it) {
            QVERIFY2(!it.value().toString().isEmpty(), qPrintable(it.key()));
            seen.insert(it.value().toString());
        }
        QCOMPARE(seen.size(), 9);
        QCOMPARE(said.value(u"markSeriesRead"_s).toString(),
                 u"Marquer toute la série comme lue"_s);
        QCOMPARE(said.value(u"saveACopy"_s).toString(), u"Enregistrer une copie…"_s);
    }
};

QTEST_MAIN(CommandsTheLibrary)
#include "commands_the_library.moc"
