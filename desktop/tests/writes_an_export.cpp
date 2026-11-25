// What an export writes to a disk, and what it asks before it does.
//
// Against the fake server, because what is under test is the client's own behaviour: how the
// manifests of several series become one tree, which files are fetched, and that a stopped or
// failed export leaves nothing half-written.

#include "Api.h"
#include "Exports.h"
#include "Pretend.h"
#include "Server.h"
#include "Settings.h"
#include "Words.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <limits>

using namespace Qt::StringLiterals;

namespace {

QByteArray http(int status, const QByteArray &body, const char *type = "application/json")
{
    return "HTTP/1.1 " + QByteArray::number(status) + " .\r\nContent-Type: " + type
           + "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
}

QJsonObject folder(const QString &path, const QString &level, const QString &sidecar = {},
                   const QString &content = {})
{
    QJsonObject one{{u"path"_s, path}, {u"level"_s, level}};
    if (!sidecar.isEmpty()) {
        one.insert(u"sidecar"_s, sidecar);
        one.insert(u"content"_s, content);
    }
    return one;
}

QJsonObject file(const QString &path, const QString &kind, qint64 size,
                 const QString &entryId = {})
{
    QJsonObject one{{u"path"_s, path}, {u"kind"_s, kind}, {u"size"_s, double(size)}};
    if (!entryId.isEmpty())
        one.insert(u"entryId"_s, entryId);
    return one;
}

QByteArray tree(const QJsonArray &folders, const QJsonArray &files)
{
    return QJsonDocument(QJsonObject{{u"folders"_s, folders}, {u"files"_s, files}})
        .toJson(QJsonDocument::Compact);
}

QByteArray read(const QString &path)
{
    QFile in(path);
    return in.open(QIODevice::ReadOnly) ? in.readAll() : QByteArray("<absent>");
}

/// Every file under `root` including the hidden ones a half-written file would be.
QStringList everything(const QString &root)
{
    QStringList all;
    QDirIterator walk(root, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (walk.hasNext())
        all << QDir(root).relativeFilePath(walk.next());
    all.sort();
    return all;
}

} // namespace

class WritesAnExport : public QObject
{
    Q_OBJECT

    Pretend *m_pretend = nullptr;
    Settings *m_settings = nullptr;
    Server *m_server = nullptr;
    Exports *m_exports = nullptr;
    QTemporaryDir m_into;
    QStringList m_asked;
    qint64 m_free = std::numeric_limits<qint64>::max();

    QUrl into() const { return QUrl::fromLocalFile(m_into.path()); }

    /// Two editions of one work in one universe, `a` with two volumes and a cover of its own.
    void library()
    {
        const QByteArray universe = "{\"name\":\"Dragon Ball\",\"id\":\"u\"}\r\n";
        const QJsonArray chainA{folder(u"Dragon Ball"_s, u"UNIVERSE"_s, u"universe.json"_s,
                                       QString::fromUtf8(universe)),
                                folder(u"Dragon Ball/Dragon Ball"_s, u"WORK"_s),
                                folder(u"Dragon Ball/Dragon Ball/Perfect"_s, u"EDITION"_s,
                                       u"edition.json"_s, u"{\"é\":1}\n"_s)};
        const QJsonArray chainB{folder(u"Dragon Ball"_s, u"UNIVERSE"_s, u"universe.json"_s,
                                       QString::fromUtf8(universe)),
                                folder(u"Dragon Ball/Dragon Ball"_s, u"WORK"_s),
                                folder(u"Dragon Ball/Dragon Ball/Original"_s, u"EDITION"_s)};
        const QByteArray manifestA = tree(
            chainA, {file(u"Dragon Ball/Dragon Ball/Perfect/Tome 01.cbz"_s, u"VOLUME"_s, 5, u"e1"_s),
                     file(u"Dragon Ball/Dragon Ball/Perfect/Tome 01.jpg"_s, u"IMAGE"_s, 3, u"e1"_s),
                     file(u"Dragon Ball/Dragon Ball/Perfect/Tome 02.cbz"_s, u"VOLUME"_s, 5, u"e2"_s),
                     file(u"Dragon Ball/Dragon Ball/Perfect/cover.jpg"_s, u"IMAGE"_s, 4)});
        const QByteArray manifestB = tree(
            chainB, {file(u"Dragon Ball/Dragon Ball/Original/Tome 01.cbz"_s, u"VOLUME"_s, 6, u"f1"_s)});
        m_pretend->answerFor = [=](const QByteArray &request) {
            const QByteArray line = request.left(request.indexOf("\r\n"));
            if (line.contains("/series/a/folders"))
                return http(200, manifestA);
            if (line.contains("/series/b/folders"))
                return http(200, manifestB);
            if (line.contains("/series/a/files/0"))
                return http(200, "AAAAA", "application/octet-stream");
            if (line.contains("/series/a/files/1"))
                return http(200, "JPG", "application/octet-stream");
            if (line.contains("/series/a/files/2"))
                return http(200, "BBBBB", "application/octet-stream");
            if (line.contains("/series/a/files/3"))
                return http(200, "COVR", "application/octet-stream");
            if (line.contains("/series/b/files/0"))
                return http(200, "CCCCCC", "application/octet-stream");
            return http(404, "{\"error\":\"not found\"}");
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
        m_free = std::numeric_limits<qint64>::max();
        m_exports = new Exports(m_server, [this](const QString &) { return m_free; });
        QVERIFY(m_into.isValid());
        for (const QFileInfo &one : QDir(m_into.path()).entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden))
            one.isDir() ? QDir(one.filePath()).removeRecursively() : QFile::remove(one.filePath());
        library();
    }

    void cleanup()
    {
        delete m_exports;
        delete m_server;
        delete m_settings;
        delete m_pretend;
    }

    /// The contract's shape, every field read: a server that grows one and a client that does
    /// not read it is what `client_knows_the_contract.py` exists to say.
    void a_manifest_is_read_whole()
    {
        const QJsonObject body =
            QJsonDocument::fromJson(
                tree({folder(u"U"_s, u"UNIVERSE"_s, u"universe.json"_s, u"{}"_s),
                      folder(u"U/W"_s, u"WORK"_s)},
                     {file(u"U/W/a.cbz"_s, u"VOLUME"_s, 12345678901, u"e"_s),
                      file(u"U/W/cover.jpg"_s, u"IMAGE"_s, 7)}))
                .object();
        const Api::Read<Api::Tree> read = Api::tree(body);
        QVERIFY(read.ok());
        QCOMPARE(read.value->folders.size(), 2);
        QCOMPARE(read.value->folders.at(0).sidecar.value(), u"universe.json"_s);
        QCOMPARE(read.value->folders.at(0).content.value(), u"{}"_s);
        QVERIFY(!read.value->folders.at(1).sidecar.has_value());
        QCOMPARE(read.value->files.at(0).size, qint64(12345678901));
        QCOMPARE(read.value->files.at(0).entryId.value(), u"e"_s);
        QVERIFY(!read.value->files.at(1).entryId.has_value());
        QVERIFY(!Api::tree(QJsonObject{{u"folders"_s, QJsonArray()}}).ok());
    }

    /// Two editions of one universe share its folder and its sidecar: the tree is the union,
    /// each path once, and each file asked of the series that holds it at its own position.
    void two_series_merge_into_one_tree_with_each_path_once()
    {
        QSignalSpy done(m_exports, &Exports::finished);
        m_exports->exportSeries({u"a"_s, u"b"_s}, into(), u"Dragon Ball"_s);
        QVERIFY(done.wait(5000));

        const QString root = m_into.path() + u"/Dragon Ball"_s;
        QCOMPARE(everything(m_into.path()),
                 (QStringList{u"Dragon Ball/Dragon Ball/Original/Tome 01.cbz"_s,
                              u"Dragon Ball/Dragon Ball/Perfect/Tome 01.cbz"_s,
                              u"Dragon Ball/Dragon Ball/Perfect/Tome 01.jpg"_s,
                              u"Dragon Ball/Dragon Ball/Perfect/Tome 02.cbz"_s,
                              u"Dragon Ball/Dragon Ball/Perfect/cover.jpg"_s,
                              u"Dragon Ball/Dragon Ball/Perfect/edition.json"_s,
                              u"Dragon Ball/universe.json"_s}));
        QCOMPARE(read(root + u"/Dragon Ball/Perfect/Tome 02.cbz"_s), QByteArray("BBBBB"));
        QCOMPARE(read(root + u"/Dragon Ball/Original/Tome 01.cbz"_s), QByteArray("CCCCCC"));
        QCOMPARE(read(root + u"/Dragon Ball/Perfect/cover.jpg"_s), QByteArray("COVR"));
        QVERIFY(!m_exports->going());
    }

    /// Byte for byte: the accent, the CRLF and the final newline are the reader's, and a
    /// sidecar edited by hand then read again must not have been tidied on the way.
    void sidecars_are_written_exactly_as_received()
    {
        QSignalSpy done(m_exports, &Exports::finished);
        m_exports->exportSeries({u"a"_s}, into(), u"Perfect"_s);
        QVERIFY(done.wait(5000));
        QCOMPARE(read(m_into.path() + u"/Dragon Ball/universe.json"_s),
                 QByteArray("{\"name\":\"Dragon Ball\",\"id\":\"u\"}\r\n"));
        QCOMPARE(read(m_into.path() + u"/Dragon Ball/Dragon Ball/Perfect/edition.json"_s),
                 QString::fromUtf8("{\"é\":1}\n").toUtf8());
    }

    /// Not rebuilt: the work folder has no work.json on this disk, and the export says which
    /// instead of inventing one.
    void a_missing_sidecar_is_reported_and_the_export_goes_on()
    {
        QSignalSpy done(m_exports, &Exports::finished);
        m_exports->exportSeries({u"a"_s}, into(), u"Perfect"_s);
        QVERIFY(done.wait(5000));
        const QStringList missing = done.first().at(1).toStringList();
        QCOMPARE(missing, (QStringList{u"Dragon Ball/Dragon Ball/work.json"_s}));
        QVERIFY(!QFileInfo::exists(m_into.path() + u"/Dragon Ball/Dragon Ball/work.json"_s));
        QVERIFY(QFileInfo::exists(m_into.path() + u"/Dragon Ball/Dragon Ball/Perfect/Tome 01.cbz"_s));
    }

    /// Something of the export is already there: nothing is written until the reader says
    /// whether it is replaced or only completed.
    void an_existing_path_asks_and_completing_keeps_what_is_there()
    {
        const QString kept = m_into.path() + u"/Dragon Ball/Dragon Ball/Perfect/Tome 01.cbz"_s;
        QVERIFY(QDir().mkpath(QFileInfo(kept).absolutePath()));
        QFile old(kept);
        QVERIFY(old.open(QIODevice::WriteOnly));
        old.write("OLD");
        old.close();

        QSignalSpy asked(m_exports, &Exports::conflict);
        QSignalSpy done(m_exports, &Exports::finished);
        m_exports->exportSeries({u"a"_s}, into(), u"Perfect"_s);
        QVERIFY(asked.wait(5000));
        QCOMPARE(asked.first().at(0).toInt(), 1);
        QCOMPARE(read(kept), QByteArray("OLD"));
        QVERIFY(done.isEmpty());

        m_pretend->heard.clear();
        m_exports->complete();
        QVERIFY(done.wait(5000));
        QCOMPARE(read(kept), QByteArray("OLD"));
        QVERIFY(!m_pretend->heard.contains("/files/0 "));
        QVERIFY(m_pretend->heard.contains("/files/2 "));
        QCOMPARE(read(m_into.path() + u"/Dragon Ball/Dragon Ball/Perfect/Tome 02.cbz"_s),
                 QByteArray("BBBBB"));
    }

    void replacing_overwrites_what_is_there()
    {
        const QString kept = m_into.path() + u"/Dragon Ball/Dragon Ball/Perfect/Tome 01.cbz"_s;
        QVERIFY(QDir().mkpath(QFileInfo(kept).absolutePath()));
        QFile old(kept);
        QVERIFY(old.open(QIODevice::WriteOnly));
        old.write("OLD");
        old.close();

        QSignalSpy asked(m_exports, &Exports::conflict);
        QSignalSpy done(m_exports, &Exports::finished);
        m_exports->exportSeries({u"a"_s}, into(), u"Perfect"_s);
        QVERIFY(asked.wait(5000));
        m_exports->replace();
        QVERIFY(done.wait(5000));
        QCOMPARE(read(kept), QByteArray("AAAAA"));
    }

    /// « Annuler » at the question: nothing was written, and the export is over.
    void cancelling_the_question_writes_nothing()
    {
        const QString kept = m_into.path() + u"/Dragon Ball/universe.json"_s;
        QVERIFY(QDir().mkpath(QFileInfo(kept).absolutePath()));
        QFile old(kept);
        QVERIFY(old.open(QIODevice::WriteOnly));
        old.write("OLD");
        old.close();
        QSignalSpy asked(m_exports, &Exports::conflict);
        m_exports->exportSeries({u"a"_s}, into(), u"Perfect"_s);
        QVERIFY(asked.wait(5000));
        m_exports->cancel();
        QVERIFY(!m_exports->going());
        QCOMPARE(everything(m_into.path()), QStringList{u"Dragon Ball/universe.json"_s});
    }

    /// Stopped while a file is on its way: the file is not there, not even as the temporary
    /// one beside it, and the export is over.
    void cancelling_mid_file_leaves_no_partial_file()
    {
        m_pretend->holdBack = [](const QByteArray &request) {
            return request.startsWith("GET /series/a/files/2 ");
        };
        QSignalSpy done(m_exports, &Exports::finished);
        m_exports->exportSeries({u"a"_s}, into(), u"Perfect"_s);
        QTRY_VERIFY_WITH_TIMEOUT(m_pretend->heard.contains("/files/2 "), 5000);
        QVERIFY(m_exports->going());
        m_exports->cancel();
        m_pretend->release();
        QVERIFY(!m_exports->going());
        QTest::qWait(200);
        QVERIFY(done.isEmpty());
        const QStringList left = everything(m_into.path());
        QVERIFY(!left.join(u'\n').contains(u"Tome 02"_s));
        QVERIFY(!left.join(u'\n').contains(u"cover"_s));
    }

    void a_refused_file_fails_the_export_and_says_why()
    {
        m_pretend->answerFor = [prior = m_pretend->answerFor](const QByteArray &request) {
            if (request.startsWith("GET /series/a/files/2 "))
                return http(404, "{\"error\":\"gone\"}");
            return prior(request);
        };
        QSignalSpy failed(m_exports, &Exports::failed);
        m_exports->exportSeries({u"a"_s}, into(), u"Perfect"_s);
        QVERIFY(failed.wait(5000));
        QVERIFY(!failed.first().at(0).toString().isEmpty());
        QVERIFY(!m_exports->going());
        QVERIFY(!everything(m_into.path()).join(u'\n').contains(u"Tome 02"_s));
    }

    void not_enough_room_refuses_before_writing_anything()
    {
        m_free = 10;
        QSignalSpy failed(m_exports, &Exports::failed);
        m_exports->exportSeries({u"a"_s}, into(), u"Perfect"_s);
        QVERIFY(failed.wait(5000));
        QCOMPARE(failed.first().at(0).toString(), Words::notEnoughRoom(17, 10));
        QVERIFY(everything(m_into.path()).isEmpty());
    }

    /// A path that climbs out of the destination is the server's mistake, and not one to
    /// carry out on the reader's disk.
    void a_path_that_leaves_the_destination_is_refused()
    {
        m_pretend->answerFor = [](const QByteArray &) {
            return http(200, tree({folder(u"../out"_s, u"UNIVERSE"_s)},
                                  {file(u"../out/x.cbz"_s, u"VOLUME"_s, 1, u"e"_s)}));
        };
        QSignalSpy failed(m_exports, &Exports::failed);
        m_exports->exportSeries({u"a"_s}, into(), u"x"_s);
        QVERIFY(failed.wait(5000));
        QVERIFY(everything(m_into.path()).isEmpty());
    }

    /// The bytes counted are the ones received, so the bar the window draws moves with the
    /// download and ends at the whole.
    void progress_counts_bytes_to_the_total()
    {
        QSignalSpy done(m_exports, &Exports::finished);
        m_exports->exportSeries({u"a"_s}, into(), u"Perfect"_s);
        QVERIFY(done.wait(5000));
        QCOMPARE(m_exports->totalBytes(), qint64(17));
        QCOMPARE(m_exports->sentBytes(), qint64(17));
    }

    /// A work is every edition it has: the client asks which series share the work of the one
    /// it was given, then exports the lot as it would a selection.
    void a_work_is_all_the_editions_that_share_it_and_a_universe_all_its_series()
    {
        const auto series = [](const QString &id) {
            return QJsonObject{{u"id"_s, id},          {u"workId"_s, u"w"_s},
                               {u"name"_s, id},        {u"work"_s, u"Dragon Ball"_s},
                               {u"universe"_s, u"Dragon Ball"_s},
                               {u"entryCount"_s, 1},   {u"chapterCount"_s, 0},
                               {u"arcCount"_s, 0}};
        };
        const QByteArray both = QJsonDocument(QJsonObject{{u"items"_s, QJsonArray{series(u"a"_s), series(u"b"_s)}},
                                                          {u"total"_s, 2}, {u"page"_s, 1}, {u"size"_s, 0}})
                                    .toJson(QJsonDocument::Compact);
        m_pretend->answerFor = [prior = m_pretend->answerFor, both, one = series(u"a"_s)](const QByteArray &request) {
            if (request.startsWith("GET /series/a "))
                return http(200, QJsonDocument(one).toJson(QJsonDocument::Compact));
            if (request.startsWith("GET /series?"))
                return http(200, both);
            return prior(request);
        };
        QSignalSpy done(m_exports, &Exports::finished);
        m_exports->exportWork(u"a"_s, into());
        QVERIFY(done.wait(5000));
        QVERIFY(m_pretend->heard.contains("GET /series?size=0&work=w ")
                || m_pretend->heard.contains("GET /series?work=w&size=0 "));
        QVERIFY(QFileInfo::exists(m_into.path() + u"/Dragon Ball/Dragon Ball/Original/Tome 01.cbz"_s));
        QVERIFY(QFileInfo::exists(m_into.path() + u"/Dragon Ball/Dragon Ball/Perfect/Tome 02.cbz"_s));

        m_pretend->heard.clear();
        QSignalSpy again(m_exports, &Exports::conflict);
        m_exports->exportUniverse(u"a"_s, into());
        QTRY_VERIFY_WITH_TIMEOUT(again.count() == 1, 5000);
        QVERIFY(m_pretend->heard.contains("universe=Dragon%20Ball"));
        m_exports->cancel();
    }
};

QTEST_MAIN(WritesAnExport)
#include "writes_an_export.moc"
